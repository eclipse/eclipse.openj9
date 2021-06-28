/*******************************************************************************
 * Copyright (c) 2000, 2021 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#ifndef J9_PROFILER_INCL
#define J9_PROFILER_INCL

#include <map>
#include <stddef.h>
#include <stdint.h>
#include "env/FrontEnd.hpp"
#include "compile/Compilation.hpp"
#include "env/TRMemory.hpp"
#include "env/jittypes.h"
#include "il/DataTypes.hpp"
#include "il/Node.hpp"
#include "infra/Flags.hpp"
#include "infra/Link.hpp"
#include "optimizer/Optimization.hpp"
#include "optimizer/OptimizationManager.hpp"
#include "env/VMJ9.h"
#include "infra/TRlist.hpp"
#include "runtime/J9ValueProfiler.hpp"

#define PROFILING_INVOCATION_COUNT (2)

class TR_ValueProfiler;
class TR_BlockFrequencyProfiler;
class TR_BitVector;
class TR_BlockFrequencyInfo;
class TR_CallSiteInfo;
class TR_CatchBlockProfileInfo;
class TR_ExternalProfiler;
class TR_HWProfiler;
class TR_OpaqueClassBlock;
class TR_OpaqueMethodBlock;
namespace TR { class Recompilation; }
class TR_ResolvedMethod;
class TR_Structure;
class TR_ValueProfileInfo;
namespace TR { class Block; }
namespace TR { class SymbolReference; }
namespace TR { class TreeTop; }
class TR_PersistentMethodInfo;
struct TR_ByteCodeInfo;
struct TR_InlinedCallSite;
template <typename ListKind> class List;

//////////////////////////
// PersistentProfileInfos
//////////////////////////

/**
 * Persistent storage for profiling information.
 *
 * Contains logic to control profiling accuracy and overhead.
 *
 * Holds profiling results in three classes. It will allocate and free these as necessary.
 *   TR_CatchBlockProfileInfo
 *   TR_BlockFrequencyInfo
 *   TR_ValueProfileInfo
 *
 * Also contains call site information, to assist with matching profiling information to
 * future compiles.
 *
 * These objects have complicated lifetimes due to how they are used throughout
 * recompiles and the desired to reduce memory overhead. They contain a reference count,
 * which should be incremented and decremented accordingly. Upon reaching zero, the
 * object will be destroyed by the JProfilerThread.
 *
 * One reference is held on the corresponding body info, ensuring its available as long
 * as the body hasn't been reclaimed. This reference is implicitly added as the initial
 * ref count of 1. Accesses through the body info will not manipulate the ref count by
 * default, as they should only access it as long as the body has not be reclaimed,
 * eg. during its compilation. This reduces overhead for common, safe accesses.
 *
 * Two references are held on the corresponding method info, tracking the best and
 * most recent information. These can extend the lifetime of profile information beyond that
 * of its body and must manage ref counts accordingly. Example accesses to these fields
 * would be in control logic. See the methods `getBestProfileInfo` and `setBestProfileInfo`
 * for more details on the proper behaviour here.
 *
 * During a compilation, TR_AccessedProfileInfo will manage any accesses to profile info
 * and ensure it remains available until at least the end of the compilation. This
 * is the approach almost all accesses to profiling information should use. It will
 * increment and decrement ref counts as needed, so that optimizations aren't required
 * to manage them. References to memory accesses through this object mustn't be placed
 * in persistent memory, as they could result in an invalid access later on.
 */
class TR_PersistentProfileInfo
   {
   public:
   TR_PERSISTENT_ALLOC(TR_Memory::PersistentProfileInfo)

   friend class TR_JProfilerThread;

   TR_PersistentProfileInfo(int32_t frequency, int32_t count)
      : _maxCount(count),
        _catchBlockProfileInfo(NULL),
        _blockFrequencyInfo(NULL),
        _valueProfileInfo(NULL),
        _callSiteInfo(NULL),
        _next(NULL),
        _active(false),
        _refCount(1)
      {
      for (int i=0; i < PROFILING_INVOCATION_COUNT; i++)
         {
         _profilingFrequency[i] = frequency;
         _profilingCount[i] = (count/PROFILING_INVOCATION_COUNT);
         }
      }

   ~TR_PersistentProfileInfo();

   static TR_PersistentProfileInfo * get(TR::Compilation *comp);
   static TR_PersistentProfileInfo * getCurrent(TR::Compilation *comp);
   static TR_PersistentProfileInfo * get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod);

   /**
    * Methods for managing reference count to this info from persistent data structures, such
    * as MethodInfo and from jitted code.
    */
   static void decRefCount(TR_PersistentProfileInfo* info);
   static void incRefCount(TR_PersistentProfileInfo* info);

   int32_t &getProfilingFrequency() { return _profilingFrequency[0]; }
   int32_t *getProfilingFrequencyArray() { return _profilingFrequency; }
   void    setProfilingFrequency(int32_t f) {for (int i=0; i<PROFILING_INVOCATION_COUNT; i++) _profilingFrequency[i] = f;}
   void    setProfilingFrequency(int32_t *f) {for (int i=0; i<PROFILING_INVOCATION_COUNT; i++) _profilingFrequency[i] = f[i];}
   int32_t &getProfilingCount()     { return _profilingCount[0]; }
   int32_t *getProfilingCountArray(){ return _profilingCount; }
   void    setProfilingCount(int32_t c)
      {
      for (int i=0; i < PROFILING_INVOCATION_COUNT; i++)
         _profilingCount[i] = c/PROFILING_INVOCATION_COUNT;
      _maxCount = c;
      }
   void    setProfilingCount(int32_t *c) {for (int i=0; i<PROFILING_INVOCATION_COUNT; i++) _profilingCount[i] = c[i];}
   int32_t getMaxCount() { return _maxCount;}

   void setActive(bool active = true) { _active = active; }
   bool isActive() { return _active; }

   TR_CatchBlockProfileInfo *getCatchBlockProfileInfo() {return _catchBlockProfileInfo;}
   TR_CatchBlockProfileInfo *findOrCreateCatchBlockProfileInfo(TR::Compilation *comp);

   TR_BlockFrequencyInfo    *getBlockFrequencyInfo() {return _blockFrequencyInfo;}
   TR_BlockFrequencyInfo    *findOrCreateBlockFrequencyInfo(TR::Compilation *comp);

   TR_ValueProfileInfo      *getValueProfileInfo() {return _valueProfileInfo;}
   TR_ValueProfileInfo      *findOrCreateValueProfileInfo(TR::Compilation *comp);

   TR_CallSiteInfo          *getCallSiteInfo() {return _callSiteInfo;}

   void dumpInfo(TR::FILE *);

   /**
    * @brief Determines the size of the serialized data for this object
    *
    * @return Number of bytes required for serializing this object
    */
   uint32_t getSizeForSerialization() const;

   /**
    * @brief Serialize this object and store it in the memory buffer
    *
    * @param buffer Memory buffer where serialized data is to be stored
    *
    * @note The caller should ensure buffer is large enough to accommodate the serialized data.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), deserialize(uint8_t * &).
    */
   void serialize(uint8_t * &buffer) const;

   /**
    * @brief Method for creating TR_PersistentProfileInfo from serialized data
    *
    * @param buffer Memory buffer where serialized data is stored
    *
    * @return Pointer to TR_PersistentProfileInfo created from serialized data
    *
    * @note This method does not check against buffer over-reads. The caller should ensure that the
    * the memory buffer has been populated by calling the serialized() method to avoid buffer over-reads.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), serialize().
    */
   static TR_PersistentProfileInfo * deserialize(uint8_t * &buffer)
      {
      return new (PERSISTENT_NEW) TR_PersistentProfileInfo(buffer);
      }

   private:
   /**
    * This data structure contains all the fields required for serializing an object of TR_PersistentProfileInfo.
    * Members of primitive type are represented as it is, and the pointer types which can have NULL value are
    * represented as bool to indicate their absence/presence instead of storing the raw address.
    */
   struct SerializedPPI {
      bool hasCallSiteInfo;
      bool hasBlockFrequencyInfo;
      bool hasValueProfileInfo;
   };

   /**
    * @brief Constructor for creating TR_PersistentProfileInfo from serialized data
    *
    * @param buffer Memory area that contains the serialized PersistentProfileInfo object
    */
   TR_PersistentProfileInfo(uint8_t * &buffer);

   void prepareForProfiling(TR::Compilation *comp);

   // Forms a linked list, managed by TR_JProfilerThread
   TR_PersistentProfileInfo *_next;

   TR_CallSiteInfo          *_callSiteInfo;
   TR_CatchBlockProfileInfo *_catchBlockProfileInfo;
   TR_BlockFrequencyInfo    *_blockFrequencyInfo;
   TR_ValueProfileInfo      *_valueProfileInfo;


   // NOTE: if the element size of the following two arrays changes
   // one have to adjust the code in createProfiledMethod() in ProfilerGenerator.cpp
   // Currently the code assumes that the element size is 4 bytes (sizeof(int32_t))
   int32_t _profilingFrequency[PROFILING_INVOCATION_COUNT];
   int32_t _profilingCount[PROFILING_INVOCATION_COUNT];
   int32_t _maxCount;

   // Manage several uses of this info
   volatile intptr_t _refCount;

   // Flag to determine whether the information is being actively updated
   bool _active;
   };


///////////////////////////
// TR_RecompilationModifier
///////////////////////////

class TR_RecompilationModifier : public TR::Optimization
   {
   public:

   TR_RecompilationModifier(TR::OptimizationManager *manager);
   static TR::Optimization *create(TR::OptimizationManager *manager)
      {
      return new (manager->allocator()) TR_RecompilationModifier(manager);
      }

   virtual int32_t perform();
   virtual const char * optDetailString() const throw();

   TR::Recompilation *_recompilation;
   };


///////////////////////////
// Recompilation Profilers
///////////////////////////

class TR_RecompilationProfiler : public TR_Link<TR_RecompilationProfiler>
   {
   public:

   TR_RecompilationProfiler(TR::Compilation * c, TR::Recompilation * r, uint32_t flags = 0)
      : _compilation(c), _recompilation(r), _trMemory(c->trMemory()), _flags(flags) { }

   virtual void modifyTrees() { }
   virtual void removeTrees() { }

   bool getHasModifiedTrees()       { return _flags.testAny(treesModified); }
   void setHasModifiedTrees(bool b) { _flags.set(treesModified, b); }

   bool getInitialCompilation()        { return _flags.testAny(initialCompilation); }
   void setInitialCompilation(bool b)  { _flags.set(initialCompilation, b); }

   virtual TR_ValueProfiler * asValueProfiler() { return NULL; }
   virtual TR_BlockFrequencyProfiler * asBlockFrequencyProfiler() { return NULL; }

   protected:

   TR::Compilation   * _compilation;
   TR::Recompilation * _recompilation;
   TR_Memory        * _trMemory;

   TR::Compilation *          comp()                        { return _compilation; }

   TR_Memory *               trMemory()                    { return _trMemory; }
   TR_StackMemory            trStackMemory()               { return _trMemory; }
   TR_HeapMemory             trHeapMemory()                { return _trMemory; }
   TR_PersistentMemory *     trPersistentMemory()          { return _trMemory->trPersistentMemory(); }

   enum
      {
      treesModified            = 0x00000001,
      initialCompilation       = 0x00000002,
      dummyLastEnum
      };
   flags32_t          _flags;
   };

class TR_LocalRecompilationCounters : public TR_RecompilationProfiler
   {
   public:

   TR_LocalRecompilationCounters(TR::Compilation * c, TR::Recompilation * r)
      : TR_RecompilationProfiler(c, r) { }

   virtual void modifyTrees();
   virtual void removeTrees();
   };

class TR_GlobalRecompilationCounters : public TR_LocalRecompilationCounters
   {
   public:

   TR_GlobalRecompilationCounters(TR::Compilation * c, TR::Recompilation * r)
      : TR_LocalRecompilationCounters(c, r) { }

   virtual void modifyTrees();

   private:

   void examineStructure(TR_Structure *, TR_BitVector &);
   };

class TR_CatchBlockProfiler : public TR_RecompilationProfiler
   {
   public:

   TR_CatchBlockProfiler(TR::Compilation * c, TR::Recompilation * r, bool initialCompilation = false);

   virtual void modifyTrees();
   virtual void removeTrees();

   private:

   TR_CatchBlockProfileInfo * _profileInfo;
   TR::SymbolReference * _catchCounterSymRef;
   TR::SymbolReference * _throwCounterSymRef;
   };

class TR_BlockFrequencyProfiler : public TR_RecompilationProfiler
   {
   public:

   TR_BlockFrequencyProfiler(TR::Compilation * c, TR::Recompilation * r);

   virtual TR_BlockFrequencyProfiler *asBlockFrequencyProfiler() { return this; }
   virtual void modifyTrees();
   };

/**
 * TR_ValueProfiler manages the creation of value profiling instrumentation.
 * It consists of an optimization pass, capable of identifying common profiling
 * candidates, and helper functions so that other optimizations can request
 * profiling instrumentation.
 */
class TR_ValueProfiler : public TR_RecompilationProfiler
   {
   public:
   TR_ValueProfiler(TR::Compilation * c, TR::Recompilation * r, TR_ValueInfoSource profiler = LinkedListProfiler) :
      TR_RecompilationProfiler(c, r),
      _bdClass(NULL),
      _stringClass(NULL),
      _defaultProfiler(profiler),
      _postLowering(false)
      {
      }

   TR_ValueProfiler * asValueProfiler() { return this; }

   void modifyTrees();

   void addProfilingTrees(TR::Node *node, TR::TreeTop *treetop, size_t numExpandedValues = 0,
      TR_ValueInfoKind kind = LastValueInfo, TR_ValueInfoSource source = LastProfiler,
      bool commonNode = true, bool decrementRecompilationCounter = false);

   void addProfilingTrees(TR::Node *node, TR::TreeTop *treetop, TR_ByteCodeInfo &bci, size_t numExpandedValues = 0,
      TR_ValueInfoKind kind = LastValueInfo, TR_ValueInfoSource source = LastProfiler,
      bool commonNode = true, bool decrementRecompilationCounter = false);

   void setPostLowering(bool post = true) { _postLowering = post; }

   private:

   bool validConfiguration(TR::DataType dataType, TR_ValueInfoKind kind);

   void addListOrArrayProfilingTrees(TR::Node *node, TR::TreeTop *treetop, TR_ByteCodeInfo &bci, size_t numExpandedValues = 0,
      TR_ValueInfoKind kind = LastValueInfo, TR_ValueInfoSource source = LastProfiler,
      bool commonNode = true, bool decrementRecompilationCounter = false);

   void addHashTableProfilingTrees(TR::Node *node, TR::TreeTop *treetop, TR_ByteCodeInfo &bci,
      TR_ValueInfoKind kind = LastValueInfo, TR_ValueInfoSource source = LastProfiler,
      bool commonNode = true);

   // Cache profiled classes
   TR_OpaqueClassBlock * _bdClass;
   TR_OpaqueClassBlock * _stringClass;

   // Default instrumentation to use
   TR_ValueInfoSource    _defaultProfiler;
   bool _postLowering;
   };

/**
 * External profiler information.
 * Heap allocated list of profiling information, registered against a method.
 */
class TR_ExternalValueProfileInfo
   {
   public:
   TR_ALLOC(TR_Memory::ExternalValueProfileInfo)

   TR_ExternalValueProfileInfo(TR_OpaqueMethodBlock *vmMethod, TR_ExternalProfiler *profiler)
      : _method(vmMethod), _profiler(profiler), _info(NULL)
      {}

   TR_OpaqueMethodBlock *getPersistentIdentifier() { return _method; }

   /**
    * Static methods to manage the collection of external value profile info.
    * Currently, this hangs of the compilation object.
    */
   static TR_ExternalValueProfileInfo *addInfo(TR_OpaqueMethodBlock *method, TR_ExternalProfiler *profiler, TR::Compilation *comp);
   static TR_ExternalValueProfileInfo *getInfo(TR_OpaqueMethodBlock *method, TR::Compilation *comp);

   /**
    * Get and create value information
    */
   TR_AbstractInfo *getValueInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp);
   TR_AbstractInfo *createAddressInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp,
      uintptr_t initialValue, uint32_t initialFreq, TR_LinkedListProfilerInfo<ProfileAddressType> **list=NULL);

   private:
   TR_OpaqueMethodBlock *_method;
   TR_ExternalProfiler  *_profiler;
   TR_AbstractInfo      *_info;
   };

/**
 * This class creates and stores the persistent information for any JIT value profiling instrumentation.
 * One exists per body info. Due to this, a compilation may have access to two TR_ValueProfileInfos at
 * once: the results from a prior compile and the current.
 *
 * The TR_ValueProfiler will always access the TR_ValueProfileInfo generated for the current compile, whilst
 * TR_ValueProfileInfoManager will always access the prior compile's TR_ValueProfileInfo.
 * Once the corresponding body shall no longer execute, this class is deallocated.
 */
class TR_ValueProfileInfo
   {
   public:
   TR_PERSISTENT_ALLOC(TR_Memory::ValueProfileInfo)

   TR_ValueProfileInfo(TR_CallSiteInfo *info);
   ~TR_ValueProfileInfo();

   static inline TR_ValueProfileInfo * get(TR_PersistentProfileInfo * profileInfo);
   static inline TR_ValueProfileInfo * get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod);
   static inline TR_ValueProfileInfo * get(TR::Compilation *comp);
   static inline TR_ValueProfileInfo * getCurrent(TR::Compilation *comp);

   TR_AbstractInfo *getValueInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind,
      TR_ValueInfoSource source, bool fuzz = false, TR::Region *optRegion = NULL);

   TR_AbstractProfilerInfo *getProfilerInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind,
      TR_ValueInfoSource source, bool fuzz = false);
   TR_AbstractProfilerInfo *createAndInitializeProfilerInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind,
      TR_ValueInfoSource source, uint64_t initialValue = CONSTANT64(0xdeadf00ddeadf00d));
   TR_AbstractProfilerInfo *getOrCreateProfilerInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind,
      TR_ValueInfoSource source, uint64_t initialValue = CONSTANT64(0xdeadf00ddeadf00d));

   void resetLowFreqValues(TR::FILE *);

   void dumpInfo(TR::FILE *);

   private:
   TR_AbstractProfilerInfo *_values[LastProfiler];
   TR_CallSiteInfo         *_callSiteInfo;
   };

TR_ValueProfileInfo * TR_ValueProfileInfo::get(TR_PersistentProfileInfo * profileInfo)
   {
   return profileInfo ? profileInfo->getValueProfileInfo() : NULL;
   }
TR_ValueProfileInfo * TR_ValueProfileInfo::get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod)
   {
   return get(TR_PersistentProfileInfo::get(comp, vmMethod));
   }
TR_ValueProfileInfo * TR_ValueProfileInfo::get(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::get(comp));
   }
TR_ValueProfileInfo * TR_ValueProfileInfo::getCurrent(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::getCurrent(comp));
   }

/**
 * The ValueProfileInfoManager serves as a facade for multiple sources of value
 * profiling information.
 *
 */
class TR_ValueProfileInfoManager
   {
   public:
   TR_ALLOC(TR_Memory::ValueProfileInfoManager)

   TR_ValueProfileInfoManager(TR::Compilation *comp);

   enum
      {
      allProfileInfo                   = 0x00000001, ///< Search all profile info types
      justJITProfileInfo               = 0x00000002, ///< Only return JIT Profile info
      justInterpreterProfileInfo       = 0x00000003  ///< Only return IProfiler info
      };

   /**
    * Return TR_AbstractInfo that can be downcast to a particular kind of value profiling info.
    *
    * @param node               The node for which value info is desired.
    * @param comp               Compilation
    * @param type               Acceptable return record types.
    * @param source             Source selection.
    */
   TR_AbstractInfo *getValueInfo(TR::Node *node,
                                 TR::Compilation *comp,
                                 TR_ValueInfoKind type,
                                 uint32_t source = allProfileInfo);

   static TR_AbstractInfo *getProfiledValueInfo(TR::Node *node,
                                 TR::Compilation *comp,
                                 TR_ValueInfoKind type,
                                 uint32_t source = allProfileInfo);

   /**
    * Return TR_AbstractInfo that can be downcast to a particular kind of value profiling info.
    *
    * @param bcInfo             The ByteCodeInfo for which info is desired.
    * @param comp               Compilation
    * @param type               Acceptable return record types.
    * @param source             Source selection.
    */
   TR_AbstractInfo *getValueInfo(TR_ByteCodeInfo & bcInfo,
                                 TR::Compilation *comp,
                                 TR_ValueInfoKind type,
                                 uint32_t source = allProfileInfo);

   static TR_AbstractInfo *getProfiledValueInfo(TR_ByteCodeInfo & bcInfo,
                                 TR::Compilation *comp,
                                 TR_ValueInfoKind type,
                                 uint32_t source = allProfileInfo);

   void             updateCallGraphProfilingCount(TR::Block *block, TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp);
   bool             isColdCall(TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp);
   bool             isColdCall(TR_OpaqueMethodBlock *calleeMethod, TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp);
   bool             isColdCall(TR::Node *node, TR::Compilation *comp);
   bool             isWarmCallGraphCall(TR::Node *node, TR_OpaqueMethodBlock *method, TR::Compilation *comp);
   bool             isWarmCall(TR::Node *node, TR::Compilation *comp);
   bool             isCallGraphProfilingEnabled(TR::Compilation *comp);
   int32_t          getCallGraphProfilingCount(TR::Node *node, TR_OpaqueMethodBlock *method, TR::Compilation *comp);
   int32_t          getCallGraphProfilingCount(TR::Node *node, TR::Compilation *comp);
   int32_t          getCallGraphProfilingCount(TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp);
   int32_t          getCallGraphProfilingCount(TR_OpaqueMethodBlock *calleeMethod, TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp);
   float            getAdjustedInliningWeight(TR::Node *node, int32_t weight, TR::Compilation *comp);
   bool             isHotCall(TR::Node *node, TR::Compilation *comp);

   static inline TR_ValueProfileInfoManager * get(TR::Compilation *comp);


   private:
   TR_ValueProfileInfo   *_jitValueProfileInfo;
   TR_BlockFrequencyInfo *_jitBlockFrequencyInfo;
   TR_OpaqueMethodBlock  *_cachedJ9Method;
   bool                   _isCountZero;
   };

TR_ValueProfileInfoManager *TR_ValueProfileInfoManager::get(TR::Compilation *comp)
   {
   if (!comp->getValueProfileInfoManager())
      {
      comp->setValueProfileInfoManager( new (comp->trHeapMemory()) TR_ValueProfileInfoManager (comp) );
      }
   return comp->getValueProfileInfoManager();
   }


class TR_BranchProfileInfoManager
   {
   public:
   TR_ALLOC(TR_Memory::BranchProfileInfoManager)
   TR_BranchProfileInfoManager(TR::Compilation *comp)
      {
      _iProfiler = (TR_ExternalProfiler *)comp->fej9()->getIProfiler();
      }

   TR_ExternalProfiler     *_iProfiler;

   static inline TR_BranchProfileInfoManager * get(TR::Compilation *comp);
   float getCallFactor(int32_t callSiteIndex, TR::Compilation *comp);
   void getBranchCounters(TR::Node *node, TR::TreeTop *block, int32_t *branchToCount, int32_t *fallThroughCount, TR::Compilation *comp);
   };


TR_BranchProfileInfoManager *TR_BranchProfileInfoManager::get(TR::Compilation *comp)
   {
   if (!comp->getBranchProfileInfoManager())
      {
      comp->setBranchProfileInfoManager( new (comp->trHeapMemory()) TR_BranchProfileInfoManager (comp) );
      }
   return comp->getBranchProfileInfoManager();
   }


class TR_MethodBranchProfileInfo
   {
public:
   TR_ALLOC(TR_Memory::MethodBranchProfileInfo)

   TR_MethodBranchProfileInfo (uint32_t callSiteIndex, TR::Compilation *comp)
      : _callSiteIndex(callSiteIndex), _factor(-1.0f), _initialBlockFrequency(0)
      {
      }

   static TR_MethodBranchProfileInfo * addMethodBranchProfileInfo (uint32_t callSiteIndex, TR::Compilation *comp);
   static TR_MethodBranchProfileInfo * getMethodBranchProfileInfo(uint32_t callSiteIndex, TR::Compilation *comp);
   static void resetMethodBranchProfileInfos(int32_t oldMaxFrequency, int32_t oldMaxEdgeFrequency, TR::Compilation *comp);

   uint32_t getCallSiteIndex() { return _callSiteIndex; }
   float    getCallFactor() { return _factor; }
   uint32_t getInitialBlockFrequency() { return _initialBlockFrequency; }

   void     setCallFactor(float factor) { _factor = factor; }
   void     setInitialBlockFrequency(uint32_t initialFrequency) { _initialBlockFrequency = initialFrequency; }

public:
   int32_t  _oldMaxFrequency;
   int32_t  _oldMaxEdgeFrequency;

private:
   uint32_t _callSiteIndex;
   float    _factor;
   uint32_t _initialBlockFrequency;
   };

// To be used for checking if _counterDerivationInfo[i] is a bit vector or not
#define IS_VALID_BIT_VECTOR(cdi) (!((uintptr_t)cdi & 0x1))

class TR_BlockFrequencyInfo
   {
   public:

   TR_ALLOC(TR_Memory::BlockFrequencyInfo)

   TR_BlockFrequencyInfo(TR::Compilation *comp, TR_AllocationKind allocKind);

   TR_BlockFrequencyInfo(TR_CallSiteInfo *callSiteInfo, int32_t numBlocks, TR_ByteCodeInfo *blocks, int32_t *frequencies);

   ~TR_BlockFrequencyInfo();

   static inline TR_BlockFrequencyInfo * get(TR_PersistentProfileInfo * profileInfo);
   static inline TR_BlockFrequencyInfo * get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod);
   static inline TR_BlockFrequencyInfo * get(TR::Compilation *comp);
   static inline TR_BlockFrequencyInfo * getCurrent(TR::Compilation *comp);

   void   *getFrequencyArrayBase() const { return _frequencies; }
   void   *getFrequencyForBlock(int32_t blockNum) {return &_frequencies[blockNum];}
   int32_t getFrequencyInfo(TR::Block *block, TR::Compilation *comp);
   int32_t getFrequencyInfo(TR_ByteCodeInfo &bci, TR::Compilation *comp, bool normalizeForCallers = true, bool trace = true);
   void    setCounterDerivationInfo(TR_BitVector **counterDerivationInfo) { _counterDerivationInfo = counterDerivationInfo; }
   void    setEntryBlockNumber(int32_t number) { _entryBlockNumber = number; }
   bool    isJProfilingData() { return _counterDerivationInfo != NULL; }
   static int32_t *getEnableJProfilingRecompilation() { return &_enableJProfilingRecompilation; }
   static void    enableJProfilingRecompilation() { _enableJProfilingRecompilation = -1; }
   void setIsQueuedForRecompilation() { _isQueuedForRecompilation = -1; }
   int32_t *getIsQueuedForRecompilation() { return &_isQueuedForRecompilation; }

   TR::Node* generateBlockRawCountCalculationSubTree(TR::Compilation *comp, int32_t blockNumber, TR::Node *node);
   TR::Node* generateBlockRawCountCalculationSubTree(TR::Compilation *comp, TR::Node *node, bool trace);
   void dumpInfo(TR::FILE *);

   int32_t getCallCount();
   int32_t getMaxRawCount(int32_t callerIndex);
   int32_t getMaxRawCount();

   /**
    * @brief Determines the size of the serialized data for this object
    *
    * @return Number of bytes required for serializing this object
    */
   uint32_t getSizeForSerialization() const;

   /**
    * @brief serialize this object and store it in the memory buffer
    *
    * @param buffer Memory buffer where serialized data is to be stored
    *
    * @note The caller should ensure buffer is large enough to accommodate the serialized data.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), deserialize(uint8_t * &).
    */
   void serialize(uint8_t * &buffer) const;

   /**
    * @brief Method for creating TR_BlockFrequencyInfo from serialized data
    *
    * @param buffer Memory buffer where serialized data is stored
    * @param currentProfile Pointer to TR_PersistentProfileInfo object at which TR_BlockFrequencyInfo object is anchored at
    *
    * @return Pointer to TR_BlockFrequencyInfo created from serialized data
    *
    * @note This method does not check against buffer over-reads. The caller should ensure that the
    * the memory buffer has been populated by calling the serialized() method to avoid buffer over-reads.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), serialize().
    */
   static TR_BlockFrequencyInfo * deserialize(uint8_t * &buffer, TR_PersistentProfileInfo *currentProfileInfo);

   private:

   /**
    * @brief Helper method to check if counter derivation info is pointing to valid bit vector or not
    *
    * @param counterDerivationInfo pointer to counter derivation info
    *
    * @return true if the pointer passed in is an actual pointer to TR_BitVector, false otherwise
    * See _counterDerivationInfo.
    */
   static inline bool isCounterDerivationInfoValidBitVector(TR_BitVector *counterDerivationInfo);

   /**
    * This data structure contains all the fields required for serializing an object of TR_PersistentProfileInfo.
    * Members of primitive type are represented as it is, and the pointer types which can have NULL value are
    * represented as bool to indicate their absence/presence instead of storing the raw address.
    */
   struct SerializedBFI
      {
      int32_t numBlocks;
      };

   /**
    * @brief Constructor for creating TR_BlockFrequencyInfo from serialized data
    *
    * @param serializedData pointer to SerializedBFI that contains serialized data for the member fields of primitive type
    * @param buffer memory area that contains serialized data for the member fields of pointer type
    * @param currentProfile pointer to TR_PersistentProfileInfo object at which TR_BlockFrequencyInfo object is anchored at
    */
   TR_BlockFrequencyInfo(const SerializedBFI *serializedData, uint8_t * &buffer, TR_PersistentProfileInfo *currentProfile);

   int32_t getRawCount(TR::ResolvedMethodSymbol *resolvedMethod, TR_ByteCodeInfo &bci, TR_CallSiteInfo *callSiteInfo, int64_t maxCount, TR::Compilation *comp);
   int32_t getRawCount(TR_ByteCodeInfo &bci, TR_CallSiteInfo *callSiteInfo, int64_t maxCount, TR::Compilation *comp);
   int32_t getOriginalBlockNumberToGetRawCount(TR_ByteCodeInfo &bci, TR::Compilation *comp, bool trace);

   TR_CallSiteInfo * _callSiteInfo;
   int32_t const _numBlocks;
   TR_ByteCodeInfo * const _blocks;
   int32_t         * const _frequencies;

   // counterDerivationInfo is used by JProfiling to store which counters to add and subtract to derive
   // the frequency of a basic block. A NULL entry represents no counters, a low tagged entry represents
   // a single block which can be obtained by right shifting the entry by 1, otherwise it will be a pointer
   // to a TR_BitVector holding the counters to add together
   TR_BitVector    ** _counterDerivationInfo;
   int32_t         _entryBlockNumber;
   static int32_t  _enableJProfilingRecompilation;
   // Following flag is checked at runtime to know if we have queued this method for recompilation and skip the profiling code
   int32_t         _isQueuedForRecompilation;
   };

TR_BlockFrequencyInfo * TR_BlockFrequencyInfo::get(TR_PersistentProfileInfo * profileInfo)
   {
   return profileInfo ? profileInfo->getBlockFrequencyInfo() : 0;
   }
TR_BlockFrequencyInfo * TR_BlockFrequencyInfo::get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod)
   {
   return get(TR_PersistentProfileInfo::get(comp, vmMethod));
   }
TR_BlockFrequencyInfo * TR_BlockFrequencyInfo::get(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::get(comp));
   }
TR_BlockFrequencyInfo * TR_BlockFrequencyInfo::getCurrent(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::getCurrent(comp));
   }
bool TR_BlockFrequencyInfo::isCounterDerivationInfoValidBitVector(TR_BitVector *counterDerivationInfo)
   {
   return (counterDerivationInfo != NULL) && !(reinterpret_cast<uintptr_t>(counterDerivationInfo) & 0x1);
   }

class TR_CatchBlockProfileInfo
   {
   public:

   TR_ALLOC(TR_Memory::CatchBlockProfileInfo)

   TR_CatchBlockProfileInfo() : _catchCounter(0), _throwCounter(0) { }

   static inline TR_CatchBlockProfileInfo * get(TR_PersistentProfileInfo * profileInfo);
   static inline TR_CatchBlockProfileInfo * get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod);
   static inline TR_CatchBlockProfileInfo * get(TR::Compilation *comp);
   static inline TR_CatchBlockProfileInfo * getCurrent(TR::Compilation *comp);

   static const uint32_t EDOThreshold;

   uint32_t & getCatchCounter() { return _catchCounter; }
   uint32_t & getThrowCounter() { return _throwCounter; }

   void dumpInfo(TR::FILE *);

   private:

   uint32_t _catchCounter;
   uint32_t _throwCounter;
   };

TR_CatchBlockProfileInfo * TR_CatchBlockProfileInfo::get(TR_PersistentProfileInfo * profileInfo)
   {
   return profileInfo ? profileInfo->getCatchBlockProfileInfo() : 0;
   }
TR_CatchBlockProfileInfo * TR_CatchBlockProfileInfo::get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod)
   {
   return get(TR_PersistentProfileInfo::get(comp, vmMethod));
   }
TR_CatchBlockProfileInfo * TR_CatchBlockProfileInfo::get(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::get(comp));
   }
TR_CatchBlockProfileInfo * TR_CatchBlockProfileInfo::getCurrent(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::getCurrent(comp));
   }

class TR_CallSiteInfo
   {
   public:

   // Placement new support, used when updating call site info
   void * operator new (size_t s, void * loc) {return loc;}
   void operator delete(void *p, void *) {}

   TR_ALLOC(TR_Memory::CallSiteInfo)

   TR_CallSiteInfo(TR::Compilation * comp, TR_AllocationKind allocKind);
   ~TR_CallSiteInfo();

   static inline TR_CallSiteInfo * get(TR_PersistentProfileInfo * profileInfo);
   static inline TR_CallSiteInfo * get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod);
   static inline TR_CallSiteInfo * get(TR::Compilation *comp);
   static inline TR_CallSiteInfo * getCurrent(TR::Compilation *comp);

   bool computeEffectiveCallerIndex(TR::Compilation *comp, TR::list<std::pair<TR_OpaqueMethodBlock *, TR_ByteCodeInfo> > &callStack, int32_t &effectiveCallerIndex);
   bool hasSameBytecodeInfo(TR_ByteCodeInfo & persistentByteCodeInfo, TR_ByteCodeInfo & currentByteCodeInfo, TR::Compilation *comp);
   int32_t hasSamePartialBytecodeInfo(TR_ByteCodeInfo & persistentBytecodeInfo, TR_ByteCodeInfo & currentBytecodeInfo, TR::Compilation *comp);
   size_t getNumCallSites() { return _numCallSites;}
   TR_OpaqueMethodBlock *inlinedMethod(TR_ByteCodeInfo & persistentByteCodeInfo, TR::Compilation *comp);

   void dumpInfo(TR::FILE *);

   /**
    * @brief Determines the size of the serialized data for this object
    *
    * @return Number of bytes required for serializing this object
    */
   uint32_t getSizeForSerialization() const;

   /**
    * @brief Serialize this object and store it in the memory buffer
    *
    * @param buffer Memory buffer where serialized data is to be stored
    *
    * @note The caller should ensure buffer is large enough to accommodate the serialized data.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), deserialize(uint8_t * &).
    */
   void serialize(uint8_t * &buffer) const;

   /**
    * @brief Method for creating TR_CallSiteInfo from serialized data
    *
    * @param buffer Memory buffer where the serialized data is stored
    *
    * @return Pointer to TR_CallSiteInfo created from serialized data
    *
    * @note This method does not check against buffer over-reads. The caller should ensure that the
    * the memory buffer has been populated by calling the serialized() method to avoid buffer over-reads.
    * On return the buffer gets updated to point to the location past the serialized data.
    * Also see getSizeForSerialization(), serialize().
    */
   static TR_CallSiteInfo * deserialize(uint8_t * &buffer);

   private:

   /**
    * This data structure contains all the fields required for serializing an object of TR_PersistentProfileInfo.
    * Members of primitive type are represented as it is, and the pointer types which can have NULL value are
    * represented as bool to indicate their absence/presence instead of storing the raw address.
    */
   struct SerializedCSI
      {
      size_t numCallSites;
      };

   /**
    * @brief constructor for creating TR_CallSiteInfo from serialized data
    *
    * @param serializedData pointer to SerializedCSI that contains serialized data for the member fields of primitive type
    * @param buffer memory area that contains serialized data for the member fields of pointer type
    */
   TR_CallSiteInfo(const SerializedCSI *serializedData, uint8_t * &buffer);

   size_t const _numCallSites;
   TR_InlinedCallSite * const _callSites;
   TR_AllocationKind const _allocKind;
   };

TR_CallSiteInfo * TR_CallSiteInfo::get(TR_PersistentProfileInfo * profileInfo)
   {
   return profileInfo ? profileInfo->getCallSiteInfo() : 0;
   }
TR_CallSiteInfo * TR_CallSiteInfo::get(TR::Compilation *comp, TR_ResolvedMethod * vmMethod)
   {
   return get(TR_PersistentProfileInfo::get(comp, vmMethod));
   }
TR_CallSiteInfo * TR_CallSiteInfo::get(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::get(comp));
   }
TR_CallSiteInfo * TR_CallSiteInfo::getCurrent(TR::Compilation *comp)
   {
   return get(TR_PersistentProfileInfo::getCurrent(comp));
   }

/**
 * PersistentProfileInfo have strange lifetimes.
 * They are always at least as long as the body info they are attributed to.
 * They may be extended if they are referenced as the best profiling information.
 * They may be extended if they are actively being used by a compilation.
 *
 * This class is used to track profiling information being used by a compilation.
 */
class TR_AccessedProfileInfo
   {
   public:
   typedef TR::typed_allocator<std::pair< TR_ResolvedMethod* const, TR_PersistentProfileInfo*>, TR::Region&> InfoMapAllocator;
   typedef std::less<TR_ResolvedMethod*> InfoMapComparator;
   typedef std::map<TR_ResolvedMethod*, TR_PersistentProfileInfo*, InfoMapComparator, InfoMapAllocator> InfoMap;

   TR_AccessedProfileInfo(TR::Region &region);
   ~TR_AccessedProfileInfo();

   TR_PersistentProfileInfo *compare(TR_PersistentMethodInfo *methodInfo);
   TR_PersistentProfileInfo *get(TR::Compilation *comp);
   TR_PersistentProfileInfo *get(TR_ResolvedMethod *vmMethod);

   protected:
   InfoMap                   _usedInfo;
   TR_PersistentProfileInfo *_current;
   bool _searched;
   };

/**
 * JProfile Info Thread
 *
 * This thread manages persistent profile info. It has a couple of jobs that it will
 * carry out at runtime. To achieve these, it maintains a list of all persistent profile
 * info. It will:
 *
 * 1) Deallocate persistent profile info. Once persistent profile info has a reference count
 *    of 0, its no longer needed. The profile info thread will detect this, removing the
 *    info from the list and deallocating its persistent memory.
 *
 * 2) Clear bad value profiling information. Value profiling information is built up with
 *    the first values seen. These may not reflect the most frequent. Instead detect this
 *    case and clear the bad information accordingly.
 */
class TR_JProfilerThread
   {
   public:
   TR_PERSISTENT_ALLOC(TR_Memory::IProfiler);

   TR_JProfilerThread() :
       _listHead(NULL),
       _footprint(0),
       _jProfilerMonitor(NULL),
       _jProfilerOSThread(NULL),
       _jProfilerThread(NULL),
       _state(Initial)
      {}
   ~TR_JProfilerThread();

   static TR_JProfilerThread* allocate();

   void addProfileInfo(TR_PersistentProfileInfo *newHead);

   void start(J9JavaVM *javaVM);
   void stop(J9JavaVM *javaVM);
   void processWorkingQueue();

   enum State
      {
      Initial,
      Run,
      Stop,
      };

   J9VMThread* getJProfilerThread() { return _jProfilerThread; }
   void setJProfilerThread(J9VMThread* thread) { _jProfilerThread = thread; }
   j9thread_t getJProfilerOSThread() { return _jProfilerOSThread; }
   TR::Monitor* getJProfilerMonitor() { return _jProfilerMonitor; }

   void setAttachAttempted() { _state = Run; }

   size_t getProfileInfoFootprint() { return _footprint * sizeof(TR_PersistentProfileInfo); }

   protected:
   TR_PersistentProfileInfo *deleteProfileInfo(TR_PersistentProfileInfo * volatile *prevNext, TR_PersistentProfileInfo *info);

   TR_PersistentProfileInfo * volatile  _listHead;
   volatile uintptr_t                   _footprint;
   TR::Monitor                         *_jProfilerMonitor;
   j9thread_t                           _jProfilerOSThread;
   J9VMThread                          *_jProfilerThread;
   volatile State                       _state;
   static const uint32_t                _waitMillis = 500;
   };

#endif

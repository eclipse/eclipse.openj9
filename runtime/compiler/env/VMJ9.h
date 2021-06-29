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

#ifndef VMJ9_h
#define VMJ9_h

#include <string.h>
#include "j9.h"
#include "j9protos.h"
#include "j9cp.h"
#include "j9cfg.h"
#include "rommeth.h"
#include "env/FrontEnd.hpp"
#include "env/KnownObjectTable.hpp"
#include "codegen/CodeGenPhase.hpp"
#include "compile/CompilationTypes.hpp"
#include "compile/Method.hpp"
#include "control/OptionsUtil.hpp"
#include "env/ExceptionTable.hpp"
#include "env/IO.hpp"
#include "env/jittypes.h"
#include "il/DataTypes.hpp"
#include "il/ILOpCodes.hpp"
#include "il/J9Symbol.hpp"
#include "infra/Annotations.hpp"
#include "optimizer/OptimizationStrategies.hpp"
#include "env/J9SharedCache.hpp"
#include "infra/Array.hpp"
#include "env/CompilerEnv.hpp"

class TR_CallStack;
namespace TR { class CompilationInfo; }
namespace TR { class CompilationInfoPerThread; }
class TR_IProfiler;
class TR_HWProfiler;
class TR_JProfilerThread;
class TR_Debug;
class TR_OptimizationPlan;
class TR_ExternalValueProfileInfo;
class TR_AbstractInfo;
class TR_ExternalProfiler;
class TR_JitPrivateConfig;
class TR_DataCacheManager;
class TR_EstimateCodeSize;
class TR_PersistentClassInfo;
#if defined(J9VM_OPT_JITSERVER)
class TR_Listener;
class JITServerStatisticsThread;
#endif /* defined(J9VM_OPT_JITSERVER) */
struct TR_CallSite;
struct TR_CallTarget;
namespace J9 { class ObjectModel; }
namespace TR { class CodeCache; }
namespace TR { class CodeCacheManager; }
namespace TR { class Compilation; }
namespace TR { class StaticSymbol; }
namespace TR { class ParameterSymbol; }

// XLC cannot determine uintptr_t is equivalent to uint32_t
// or uint64_t, so make it clear with this test
template <typename T> struct TR_ProfiledValue;
#if TR_HOST_64BIT
typedef TR_ProfiledValue<uint64_t> TR_ExtraAddressInfo;
#else
typedef TR_ProfiledValue<uint32_t> TR_ExtraAddressInfo;
#endif

char * feGetEnv2(const char *, const void *);   /* second param should be a J9JavaVM* */

void acquireVPMutex();
void releaseVPMutex();

bool acquireVMaccessIfNeeded(J9VMThread *vmThread, TR_YesNoMaybe isCompThread);
void releaseVMaccessIfNeeded(J9VMThread *vmThread, bool haveAcquiredVMAccess);

// <PHASE PROFILER>
#define J9VMTHREAD_TRACINGBUFFER_CURSOR_FIELD      debugEventData5
#define J9VMTHREAD_TRACINGBUFFER_TOP_FIELD         debugEventData4
#define J9VMTHREAD_TRACINGBUFFER_FH_FIELD          debugEventData3

#define VMTHREAD_TRACINGBUFFER_FH(x)               (x->J9VMTHREAD_TRACINGBUFFER_FH_FIELD)
#define VMTHREAD_TRACINGBUFFER_TOP(x)              (x->J9VMTHREAD_TRACINGBUFFER_TOP_FIELD)
#define VMTHREAD_TRACINGBUFFER_CURSOR(x)           (x->J9VMTHREAD_TRACINGBUFFER_CURSOR_FIELD)

#define VMTHREAD_TRACINGBUFFER_START_MARKER        -1

#define J9_PUBLIC_FLAGS_PATCH_HOOKS_IN_JITTED_METHODS 0x8000000
// </PHASE PROFILER>

inline void getClassNameSignatureFromMethod(J9Method *method, J9UTF8 *& methodClazz, J9UTF8 *& methodName, J9UTF8 *& methodSignature)
   {
   methodClazz = J9ROMCLASS_CLASSNAME(J9_CLASS_FROM_METHOD(method)->romClass);
   methodName = J9ROMMETHOD_NAME(J9_ROM_METHOD_FROM_RAM_METHOD(method));
   methodSignature = J9ROMMETHOD_SIGNATURE(J9_ROM_METHOD_FROM_RAM_METHOD(method));
   }

typedef struct TR_JitPrivateConfig
   {
   TR::FILE      *vLogFile;
   char          *vLogFileName;
   uint64_t       verboseFlags;
   TR::FILE      *rtLogFile;
   char          *rtLogFileName;
   char          *itraceFileNamePrefix;
   TR_IProfiler  *iProfiler;
   TR_HWProfiler *hwProfiler;
   TR_JProfilerThread  *jProfiler;
#if defined(J9VM_OPT_JITSERVER)
   TR_Listener   *listener;
   JITServerStatisticsThread   *statisticsThreadObject;
#endif /* defined(J9VM_OPT_JITSERVER) */
   TR::CodeCacheManager *codeCacheManager; // reachable from JitPrivateConfig for kca's benefit
   TR_DataCacheManager *dcManager;  // reachable from JitPrivateConfig for kca's benefit
   bool          annotationClassesAlreadyLoaded;
   TR_YesNoMaybe aotValidHeader;
   void          (*j9jitrt_lock_log)(void *voidConfig);
   void          (*j9jitrt_unlock_log)(void *voidConfig);
   int           (*j9jitrt_printf)(void *voidConfig, char *format, ...) ;

   // Runtime phase profiling buffer
   //
   int32_t        maxRuntimeTraceBufferSizeInBytes;
   int32_t        maxTraceBufferEntrySizeInBytes;
   TR_AOTStats *aotStats;
   } TR_JitPrivateConfig;

// Union containing all possible datatypes of static final fields
union
TR_StaticFinalData
   {
   int8_t dataInt8Bit;
   int16_t dataInt16Bit;
   int32_t dataInt32Bit;
   int64_t dataInt64Bit;
   float dataFloat;
   double dataDouble;
   uintptr_t dataAddress;
   };

#ifdef __cplusplus
extern "C" {
#endif

   J9VMThread * getJ9VMThreadFromTR_VM(void * vm);
   J9JITConfig * getJ9JitConfigFromFE(void *vm);
   TR::FILE *j9jit_fopen(char *fileName, const char *mode, bool useJ9IO, bool encrypt);
   void j9jit_fclose(TR::FILE *pFile);
   void j9jit_seek(void *voidConfig, TR::FILE *pFile, IDATA offset, I_32 whence);
   IDATA j9jit_read(void *voidConfig, TR::FILE *pFile, void *buf, IDATA nbytes);
   void j9jit_fflush(TR::FILE *pFile);
   void j9jit_lock_vlog(void *voidConfig);
   void j9jit_unlock_vlog(void *voidConfig);
   void j9jit_printf(void *voidConfig, char *format, ...);
   I_32 j9jit_vprintf(void *voidConfig, char *format, va_list args);
   I_32 j9jit_fprintf(TR::FILE *pFile, char *format, ...);
   I_32 j9jit_vfprintf(TR::FILE *pFile, char *format, va_list args);

   void j9jitrt_lock_log(void *voidConfig);
   void j9jitrt_unlock_log(void *voidConfig);
   I_32 j9jitrt_printf(void *voidConfig, char *format, ...);

   I_32 j9jit_fopenName(char *fileName);
   I_32 j9jit_fopen_existing(char *fileName);
   I_32 j9jit_fmove(char * pathExist, char * pathNew);
   void j9jit_fcloseId(I_32 fileId);
   I_32 j9jit_fread(I_32 fd, void * buf, IDATA nbytes);
   I_32 j9jit_fseek(I_32 fd, I_32 whence);
   I_64 j9jit_time_current_time_millis();
   I_32 j9jit_vfprintfId(I_32 fileId, char *format, ...);
   I_32 j9jit_fprintfId(I_32 fileId, char *format, ...);

   void jitHookClassLoadHelper(J9VMThread *vmThread,
                               J9JITConfig * jitConfig,
                               J9Class * cl,
                               TR::CompilationInfo *compInfo,
                               UDATA *classLoadEventFailed);

   void jitHookClassPreinitializeHelper(J9VMThread *vmThread,
                                        J9JITConfig *jitConfig,
                                        J9Class *cl,
                                        UDATA *classPreinitializeEventFailed);

#ifdef __cplusplus
}
#endif


static TR_Processor portLibCall_getARMProcessorType();
static TR_Processor portLibCall_getX86ProcessorType();
static bool portLibCall_sysinfo_has_resumable_trap_handler();
static bool portLibCall_sysinfo_has_fixed_frame_C_calling_convention();
static bool portLibCall_sysinfo_has_floating_point_unit();

TR::CompilationInfo *getCompilationInfo(J9JITConfig *jitConfig);

class TR_J9VMBase : public TR_FrontEnd
   {
public:
   void * operator new (size_t, void * placement) { return placement; }
   void operator delete (void *, void *) {}
   TR_PERSISTENT_ALLOC(TR_Memory::FrontEnd)

   enum VM_TYPE
      {
      DEFAULT_VM = 0
      , J9_VM
#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
      , AOT_VM
#endif
#if defined(J9VM_OPT_JITSERVER)
      , J9_SERVER_VM // for JITServer
      , J9_SHARED_CACHE_SERVER_VM // for Remote AOT JITServer
#endif /* defined(J9VM_OPT_JITSERVER) */
      };

   TR_J9VMBase(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo, J9VMThread * vmContext);
   virtual ~TR_J9VMBase() {}

   virtual bool isAOT_DEPRECATED_DO_NOT_USE() { return false; }
   virtual bool needsContiguousCodeAndDataCacheAllocation() { return false; }
   virtual bool supportsJitMethodEntryAlignment() { return true; }
   virtual bool canUseSymbolValidationManager() { return false; }

/////
   // Inlining optimization
   //
   virtual void setInlineThresholds ( TR::Compilation *comp, int32_t &callerWeightLimit, int32_t &maxRecursiveCallByteCodeSizeEstimate, int32_t &methodByteCodeSizeThreshold, int32_t &methodInWarmBlockByteCodeSizeThreshold, int32_t &methodInColdBlockByteCodeSizeThreshold, int32_t &nodeCountThreshold, int32_t size);
   virtual int32_t adjustedInliningWeightBasedOnArgument(int32_t origWeight, TR::Node *argNode, TR::ParameterSymbol *parmSymbol,  TR::Compilation *comp);
   virtual bool canAllowDifferingNumberOrTypesOfArgsAndParmsInInliner();
/////
   virtual bool isGetImplInliningSupported();

   virtual uintptr_t getClassDepthAndFlagsValue(TR_OpaqueClassBlock * classPointer);
   virtual uintptr_t getClassFlagsValue(TR_OpaqueClassBlock * classPointer);

   virtual bool isAbstractClass(TR_OpaqueClassBlock * clazzPointer);
   virtual bool isCloneable(TR_OpaqueClassBlock *);
   virtual bool isInterfaceClass(TR_OpaqueClassBlock * clazzPointer);
   virtual bool isConcreteClass(TR_OpaqueClassBlock * clazzPointer);
   virtual bool isEnumClass(TR_OpaqueClassBlock * clazzPointer, TR_ResolvedMethod *method);
   virtual bool isPrimitiveClass(TR_OpaqueClassBlock *clazz);
   virtual bool isPrimitiveArray(TR_OpaqueClassBlock *);
   virtual bool isReferenceArray(TR_OpaqueClassBlock *);
   virtual bool hasFinalizer(TR_OpaqueClassBlock * classPointer);
   virtual bool isClassInitialized(TR_OpaqueClassBlock *);
   virtual bool isClassVisible(TR_OpaqueClassBlock * sourceClass, TR_OpaqueClassBlock * destClass);
   virtual bool sameClassLoaders(TR_OpaqueClassBlock *, TR_OpaqueClassBlock *);
   virtual bool jitStaticsAreSame(TR_ResolvedMethod *, int32_t, TR_ResolvedMethod *, int32_t);
   virtual bool jitFieldsAreSame(TR_ResolvedMethod *, int32_t, TR_ResolvedMethod *, int32_t, int32_t);

   virtual uintptr_t getPersistentClassPointerFromClassPointer(TR_OpaqueClassBlock * clazz);//d169771 [2177]

   virtual bool needRelocationsForHelpers() { return false; }
   virtual bool needClassAndMethodPointerRelocations() { return false; }
   virtual bool needRelocationsForStatics() { return false; }
   virtual bool needRelocationsForCurrentMethodPC() { return false; }
   virtual bool needRelocationsForBodyInfoData() { return false; }
   virtual bool needRelocationsForPersistentInfoData() { return false; }
   virtual bool needRelocationsForLookupEvaluationData() { return false; }
   virtual bool nopsAlsoProcessedByRelocations() { return false; }
   virtual bool needRelocatableTarget() { return false; }

   bool supportsContextSensitiveInlining () { return true; }

   virtual bool callTargetsNeedRelocations() { return false; }

   virtual TR::DataType dataTypeForLoadOrStore(TR::DataType dt) { return (dt == TR::Int8 || dt == TR::Int16) ? TR::Int32 : dt; }

   static bool createGlobalFrontEnd(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo);
   static TR_J9VMBase * get(J9JITConfig *, J9VMThread *, VM_TYPE vmType=DEFAULT_VM);
   static char *getJ9FormattedName(J9JITConfig *, J9PortLibrary *, char *, size_t, char *, char *, bool suffix=false);

   int32_t *getStringClassEnableCompressionFieldAddr(TR::Compilation *comp, bool isVettedForAOT);
   virtual bool stringEquals(TR::Compilation * comp, uintptr_t* stringLocation1, uintptr_t* stringLocation2, int32_t& result);
   virtual bool getStringHashCode(TR::Compilation * comp, uintptr_t* stringLocation, int32_t& result);

   virtual bool isThunkArchetype(J9Method * method);

   J9VMThread * vmThread();

   bool traceIsEnabled() { return _flags.testAny(TraceIsEnabled); }
   void setTraceIsEnabled(bool b) { _flags.set(TraceIsEnabled, b); }

   bool fsdIsEnabled() { return _flags.testAny(FSDIsEnabled); }
   void setFSDIsEnabled(bool b) { _flags.set(FSDIsEnabled, b); }

   uint32_t getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName, char * sig)
      {
      return getInstanceFieldOffset(classPointer, fieldName, (uint32_t)strlen(fieldName), sig, (uint32_t)strlen(sig));
      }
   uint32_t getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName, char * sig, uintptr_t options)
      {
      return getInstanceFieldOffset(classPointer, fieldName, (uint32_t)strlen(fieldName), sig, (uint32_t)strlen(sig), options);
      }

   virtual uint32_t getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName,
                                                     uint32_t fieldLen, char * sig, uint32_t sigLen, UDATA options);
   virtual uint32_t getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName,
                                                     uint32_t fieldLen, char * sig, uint32_t sigLen);

   // Not implemented
   virtual TR_ResolvedMethod * getObjectNewInstanceImplMethod(TR_Memory *) { return 0; }

   virtual bool stackWalkerMaySkipFrames(TR_OpaqueMethodBlock *method, TR_OpaqueClassBlock *methodClass) = 0;

   virtual bool supportsEmbeddedHeapBounds()  { return true; }
   virtual bool supportsFastNanoTime();
   virtual bool supportsGuardMerging()        { return true; }
   virtual bool canDevirtualizeDispatch()     { return true; }
   virtual bool doStringPeepholing()          { return true; }

   virtual bool forceUnresolvedDispatch() { return false; }

   virtual TR_OpaqueMethodBlock * getMethodFromClass(TR_OpaqueClassBlock *, char *, char *, TR_OpaqueClassBlock * = NULL);

   TR_OpaqueMethodBlock * getMatchingMethodFromNameAndSignature(TR_OpaqueClassBlock * classPointer, const char* methodName, const char *signature, bool validate = true);

   virtual void getResolvedMethods(TR_Memory *, TR_OpaqueClassBlock *, List<TR_ResolvedMethod> *);
   /**
   * @brief Create a TR_ResolvedMethod given a class, method name and signature
   *
   * The function will scan all methods from the given class until a match for
   * the method name and signature is found
   *
   * @param trMemory     Pointer to TR_Memory object used for memory allocation
   * @param classPointer The j9class that is searched for method name/signature
   * @param methodName   Null terminated string denoting the method name we are looking for
   * @param signature    Null terminated string denoting the signature of the method we want
   * @return             A pointer to a TR_ResolvedMethod for the indicated j9method, or null if not found
   */
   virtual TR_ResolvedMethod *getResolvedMethodForNameAndSignature(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer, const char* methodName, const char *signature);
   virtual TR_OpaqueMethodBlock *getResolvedVirtualMethod(TR_OpaqueClassBlock * classObject, int32_t cpIndex, bool ignoreReResolve = true);
   virtual TR_OpaqueMethodBlock *getResolvedInterfaceMethod(TR_OpaqueMethodBlock *ownerMethod, TR_OpaqueClassBlock * classObject, int32_t cpIndex);

   TR_OpaqueMethodBlock *getResolvedInterfaceMethod(J9ConstantPool *ownerCP, TR_OpaqueClassBlock * classObject, int32_t cpIndex);

   uintptr_t getReferenceField(uintptr_t objectPointer, char *fieldName, char *fieldSignature)
      {
      return getReferenceFieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, fieldSignature));
      }
   uintptr_t getVolatileReferenceField(uintptr_t objectPointer, char *fieldName, char *fieldSignature)
      {
      return getVolatileReferenceFieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, fieldSignature));
      }

   virtual TR::Method * createMethod(TR_Memory *, TR_OpaqueClassBlock *, int32_t);
   virtual TR_ResolvedMethod * createResolvedMethod(TR_Memory *, TR_OpaqueMethodBlock *, TR_ResolvedMethod * = 0, TR_OpaqueClassBlock * = 0);
   virtual TR_ResolvedMethod * createResolvedMethodWithVTableSlot(TR_Memory *, uint32_t vTableSlot, TR_OpaqueMethodBlock * aMethod, TR_ResolvedMethod * owningMethod = 0, TR_OpaqueClassBlock * classForNewInstance = 0);
   virtual TR_ResolvedMethod * createResolvedMethodWithSignature(TR_Memory *, TR_OpaqueMethodBlock *, TR_OpaqueClassBlock *, char *signature, int32_t signatureLength, TR_ResolvedMethod *, uint32_t = 0);
   virtual void * getStaticFieldAddress(TR_OpaqueClassBlock *, unsigned char *, uint32_t, unsigned char *, uint32_t);
   virtual int32_t getInterpreterVTableSlot(TR_OpaqueMethodBlock *, TR_OpaqueClassBlock *);
   virtual int32_t getVTableSlot(TR_OpaqueMethodBlock *, TR_OpaqueClassBlock *);
   virtual uint32_t           offsetOfIsOverriddenBit();
   virtual uint32_t           offsetOfMethodIsBreakpointedBit();

   virtual TR_Debug *     createDebug( TR::Compilation * comp = NULL);

   virtual void               acquireCompilationLock();
   virtual void               releaseCompilationLock();

   virtual void               acquireLogMonitor();
   virtual void               releaseLogMonitor();

   virtual bool               isAsyncCompilation();
   virtual uintptr_t         getProcessID();
   virtual char *             getFormattedName(char *, int32_t, char *, char *, bool);

   virtual void               invalidateCompilationRequestsForUnloadedMethods(TR_OpaqueClassBlock *, bool);

   virtual void               initializeHasFixedFrameC_CallingConvention() {}
   virtual void               initializeHasResumableTrapHandler() {}
   virtual void               initializeProcessorType() {}

   void                       initializeSystemProperties();
   virtual bool               hasFPU();
   virtual bool               hasResumableTrapHandler();
   virtual bool               hasFixedFrameC_CallingConvention();
   virtual bool               pushesOutgoingArgsAtCallSite( TR::Compilation *comp);

   virtual TR::PersistentInfo * getPersistentInfo()  { return ((TR_PersistentMemory *)_jitConfig->scratchSegment)->getPersistentInfo(); }
   void                       unknownByteCode( TR::Compilation *, uint8_t opcode);
   void                       unsupportedByteCode( TR::Compilation *, uint8_t opcode);

   virtual bool isBenefitInliningCheckIfFinalizeObject() { return false; }


   virtual char*              printAdditionalInfoOnAssertionFailure( TR::Compilation *comp);

   virtual bool               vmRequiresSelector(uint32_t mask);

   virtual int32_t            getLineNumberForMethodAndByteCodeIndex(TR_OpaqueMethodBlock *method, int32_t bcIndex);
   virtual bool               isJavaOffloadEnabled();
   virtual uint32_t           getMethodSize(TR_OpaqueMethodBlock *method);
   virtual void *             getMethods(TR_OpaqueClassBlock *classPointer);
   virtual uint32_t           getNumInnerClasses(TR_OpaqueClassBlock *classPointer);
   virtual uint32_t           getNumMethods(TR_OpaqueClassBlock *classPointer);

   uintptr_t                  getMethodIndexInClass(TR_OpaqueClassBlock *classPointer, TR_OpaqueMethodBlock *methodPointer);

   virtual uint8_t *          allocateCodeMemory( TR::Compilation *, uint32_t warmCodeSize, uint32_t coldCodeSize, uint8_t **coldCode, bool isMethodHeaderNeeded=true);

   virtual void               releaseCodeMemory(void *startPC, uint8_t bytesToSaveAtStart);
   virtual uint8_t *          allocateRelocationData( TR::Compilation *, uint32_t numBytes);

   virtual bool               supportsCodeCacheSnippets();
#if defined(TR_TARGET_X86)
   void *                     getAllocationPrefetchCodeSnippetAddress( TR::Compilation * comp);
   void *                     getAllocationNoZeroPrefetchCodeSnippetAddress( TR::Compilation * comp);
#endif

   virtual TR::TreeTop *lowerTree(TR::Compilation *, TR::Node *, TR::TreeTop *);

   virtual bool canRelocateDirectNativeCalls() {return true; }

   virtual bool storeOffsetToArgumentsInVirtualIndirectThunks() { return false; }


   virtual bool               tlhHasBeenCleared();
   virtual bool               isStaticObjectFlags();
   virtual uint32_t           getStaticObjectFlags();
   virtual uintptr_t         getOverflowSafeAllocSize();
   virtual bool               callTheJitsArrayCopyHelper();
   virtual void *             getReferenceArrayCopyHelperAddress();

   virtual uintptr_t         thisThreadGetStackLimitOffset();
   virtual uintptr_t         thisThreadGetCurrentExceptionOffset();
   virtual uintptr_t         thisThreadGetPublicFlagsOffset();
   virtual uintptr_t         thisThreadGetJavaPCOffset();
   virtual uintptr_t         thisThreadGetJavaSPOffset();
   virtual uintptr_t         thisThreadGetJavaLiteralsOffset();

   // Move to CompilerEnv VM?
   virtual uintptr_t         thisThreadGetSystemSPOffset();

   virtual uintptr_t         thisThreadGetMachineSPOffset();
   virtual uintptr_t         thisThreadGetJavaFrameFlagsOffset();
   virtual uintptr_t         thisThreadGetCurrentThreadOffset();
   virtual uintptr_t         thisThreadGetFloatTemp1Offset();
   virtual uintptr_t         thisThreadGetFloatTemp2Offset();
   virtual uintptr_t         thisThreadGetTempSlotOffset();
   virtual uintptr_t         thisThreadGetReturnValueOffset();
   virtual uintptr_t         getThreadDebugEventDataOffset(int32_t index);
   virtual uintptr_t         thisThreadGetDLTBlockOffset();
   uintptr_t                 getDLTBufferOffsetInBlock();
   virtual int32_t *          getCurrentLocalsMapForDLT( TR::Compilation *);
   virtual bool               compiledAsDLTBefore(TR_ResolvedMethod *);
   virtual uintptr_t         getObjectHeaderSizeInBytes();
   uintptr_t                 getOffsetOfSuperclassesInClassObject();
   uintptr_t                 getOffsetOfBackfillOffsetField();

   virtual TR_OpaqueClassBlock * getBaseComponentClass(TR_OpaqueClassBlock * clazz, int32_t & numDims) { return 0; }

   bool vftFieldRequiresMask(){ return ~TR::Compiler->om.maskOfObjectVftField() != 0; }

   virtual uintptr_t         getOffsetOfDiscontiguousArraySizeField();
   virtual uintptr_t         getOffsetOfContiguousArraySizeField();
   virtual uintptr_t         getJ9ObjectContiguousLength();
   virtual uintptr_t         getJ9ObjectDiscontiguousLength();
   virtual uintptr_t         getOffsetOfArrayClassRomPtrField();
   virtual uintptr_t         getOffsetOfClassRomPtrField();
   virtual uintptr_t         getOffsetOfClassInitializeStatus();

   virtual uintptr_t         getOffsetOfJ9ObjectJ9Class();
   virtual uintptr_t         getObjectHeaderHasBeenMovedInClass();
   virtual uintptr_t         getObjectHeaderHasBeenHashedInClass();
   virtual uintptr_t         getJ9ObjectFlagsMask32();
   virtual uintptr_t         getJ9ObjectFlagsMask64();
   uintptr_t                 getOffsetOfJ9ThreadJ9VM();
   uintptr_t                 getOffsetOfJ9ROMArrayClassArrayShape();
   virtual uintptr_t         getOffsetOfJLThreadJ9Thread();
   uintptr_t                 getOffsetOfJavaVMIdentityHashData();
   uintptr_t                 getOffsetOfJ9IdentityHashData1();
   uintptr_t                 getOffsetOfJ9IdentityHashData2();
   uintptr_t                 getOffsetOfJ9IdentityHashData3();
   uintptr_t                 getOffsetOfJ9IdentityHashDataHashSaltTable();
   uintptr_t                 getJ9IdentityHashSaltPolicyStandard();
   uintptr_t                 getJ9IdentityHashSaltPolicyRegion();
   uintptr_t                 getJ9IdentityHashSaltPolicyNone();
   virtual uintptr_t         getIdentityHashSaltPolicy();
   virtual uintptr_t         getJ9JavaClassRamShapeShift();
   virtual uintptr_t         getObjectHeaderShapeMask();
#if defined(TR_TARGET_64BIT)
   virtual uintptr_t         getOffsetOfContiguousDataAddrField();
   virtual uintptr_t         getOffsetOfDiscontiguousDataAddrField();
#endif /* TR_TARGET_64BIT */

   virtual bool               assumeLeftMostNibbleIsZero();

   virtual uintptr_t         getOSRFrameHeaderSizeInBytes();
   virtual uintptr_t         getOSRFrameSizeInBytes(TR_OpaqueMethodBlock* method);
   virtual bool               ensureOSRBufferSize(TR::Compilation *comp, uintptr_t osrFrameSizeInBytes, uintptr_t osrScratchBufferSizeInBytes, uintptr_t osrStackFrameSizeInBytes);

   virtual bool               generateCompressedPointers();
   virtual bool               generateCompressedLockWord();

   virtual void                 printVerboseLogHeader(TR::Options *);
   virtual void                 printPID();

   virtual void                 emitNewPseudoRandomNumberVerbosePrefix();
   virtual void                 emitNewPseudoRandomNumberVerbose(int32_t i);
   virtual void                 emitNewPseudoRandomVerboseSuffix();
   virtual void                 emitNewPseudoRandomNumberVerboseLine(int32_t i);

   virtual void                 emitNewPseudoRandomStringVerbosePrefix();
   virtual void                 emitNewPseudoRandomStringVerbose(char *);
   virtual void                 emitNewPseudoRandomStringVerboseLine(char *);

   virtual uint32_t             getAllocationSize(TR::StaticSymbol *classSym, TR_OpaqueClassBlock * clazz);

   virtual TR_OpaqueClassBlock *getObjectClass(uintptr_t objectPointer);
   virtual TR_OpaqueClassBlock *getObjectClassAt(uintptr_t objectAddress);
   virtual TR_OpaqueClassBlock *getObjectClassFromKnownObjectIndex(TR::Compilation *comp, TR::KnownObjectTable::Index idx);
   virtual uintptr_t           getReferenceFieldAt(uintptr_t objectPointer, uintptr_t offsetFromHeader);
   virtual uintptr_t           getVolatileReferenceFieldAt(uintptr_t objectPointer, uintptr_t offsetFromHeader);
   virtual uintptr_t           getReferenceFieldAtAddress(uintptr_t fieldAddress);
   virtual uintptr_t           getReferenceFieldAtAddress(void *fieldAddress){ return getReferenceFieldAtAddress((uintptr_t)fieldAddress); }
   virtual uintptr_t           getStaticReferenceFieldAtAddress(uintptr_t fieldAddress);
   virtual int32_t              getInt32FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset);

   int32_t getInt32Field(uintptr_t objectPointer, char *fieldName)
      {
      return getInt32FieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, "I"));
      }

   int64_t getInt64Field(uintptr_t objectPointer, char *fieldName)
      {
      return getInt64FieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, "J"));
      }
   virtual int64_t              getInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset);
   virtual void                 setInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset, int64_t newValue);
   void setInt64Field(uintptr_t objectPointer, char *fieldName, int64_t newValue)
      {
      setInt64FieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, "J"), newValue);
      }

   virtual bool                 compareAndSwapInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset, int64_t oldValue, int64_t newValue);
   bool compareAndSwapInt64Field(uintptr_t objectPointer, char *fieldName, int64_t oldValue, int64_t newValue)
      {
      return compareAndSwapInt64FieldAt(objectPointer, getInstanceFieldOffset(getObjectClass(objectPointer), fieldName, "J"), oldValue, newValue);
      }

   virtual intptr_t            getArrayLengthInElements(uintptr_t objectPointer);
   int32_t                      getInt32Element(uintptr_t objectPointer, int32_t elementIndex);
   virtual uintptr_t           getReferenceElement(uintptr_t objectPointer, intptr_t elementIndex);

   virtual TR_OpaqueClassBlock *getClassFromJavaLangClass(uintptr_t objectPointer);
   virtual TR_arrayTypeCode    getPrimitiveArrayTypeCode(TR_OpaqueClassBlock* clazz);
   virtual TR_OpaqueClassBlock * getSystemClassFromClassName(const char * name, int32_t length, bool callSiteVettedForAOT=false) { return 0; }
   virtual TR_OpaqueClassBlock * getByteArrayClass();

   virtual uintptr_t         getOffsetOfLastITableFromClassField();
   virtual uintptr_t         getOffsetOfInterfaceClassFromITableField();
   virtual int32_t            getITableEntryJitVTableOffset();  // Must add this to the negation of an itable entry to get a jit vtable offset
   virtual int32_t            convertITableIndexToOffset(uintptr_t itableIndex);
   virtual uintptr_t         getOffsetOfJavaLangClassFromClassField();
   virtual uintptr_t         getOffsetOfInitializeStatusFromClassField();
   virtual uintptr_t         getOffsetOfClassFromJavaLangClassField();
   virtual uintptr_t         getOffsetOfRamStaticsFromClassField();
   virtual uintptr_t         getOffsetOfInstanceShapeFromClassField();
   virtual uintptr_t         getOffsetOfInstanceDescriptionFromClassField();
   virtual uintptr_t         getOffsetOfDescriptionWordFromPtrField();

   virtual uintptr_t         getConstantPoolFromMethod(TR_OpaqueMethodBlock *);
   virtual uintptr_t         getConstantPoolFromClass(TR_OpaqueClassBlock *);

   virtual uintptr_t         getOffsetOfIsArrayFieldFromRomClass();
   virtual uintptr_t         getOffsetOfClassAndDepthFlags();
   virtual uintptr_t         getOffsetOfClassFlags();
   virtual uintptr_t         getOffsetOfArrayComponentTypeField();
   virtual uintptr_t         getOffsetOfIndexableSizeField();
   virtual uintptr_t         constReleaseVMAccessOutOfLineMask();
   virtual uintptr_t         constReleaseVMAccessMask();
   virtual uintptr_t         constAcquireVMAccessOutOfLineMask();
   virtual uintptr_t         constJNICallOutFrameFlagsOffset();
   virtual uintptr_t         constJNICallOutFrameType();
   virtual uintptr_t         constJNICallOutFrameSpecialTag();
   virtual uintptr_t         constJNICallOutFrameInvisibleTag();
   virtual uintptr_t         constJNICallOutFrameFlags();
   virtual uintptr_t         constJNIReferenceFrameAllocatedFlags();
   virtual uintptr_t         constClassFlagsPrimitive();
   virtual uintptr_t         constClassFlagsAbstract();
   virtual uintptr_t         constClassFlagsFinal();
   virtual uintptr_t         constClassFlagsPublic();

   virtual uintptr_t         getGCForwardingPointerOffset();

   virtual bool               jniRetainVMAccess(TR_ResolvedMethod *method);
   virtual bool               jniNoGCPoint(TR_ResolvedMethod *method);
   virtual bool               jniNoNativeMethodFrame(TR_ResolvedMethod *method);
   virtual bool               jniNoExceptionsThrown(TR_ResolvedMethod *method);
   virtual bool               jniNoSpecialTeardown(TR_ResolvedMethod *method);
   virtual bool               jniDoNotWrapObjects(TR_ResolvedMethod *method);
   virtual bool               jniDoNotPassReceiver(TR_ResolvedMethod *method);
   virtual bool               jniDoNotPassThread(TR_ResolvedMethod *method);

   virtual uintptr_t         thisThreadJavaVMOffset();
   uintptr_t                 getMaxObjectSizeForSizeClass();
   uintptr_t                 thisThreadAllocationCacheCurrentOffset(uintptr_t);
   uintptr_t                 thisThreadAllocationCacheTopOffset(uintptr_t);
   virtual uintptr_t         getCellSizeForSizeClass(uintptr_t);
   virtual uintptr_t         getObjectSizeClass(uintptr_t);

   uintptr_t                 thisThreadMonitorCacheOffset();
   uintptr_t                 thisThreadOSThreadOffset();

   uintptr_t                 getMonitorNextOffset();
   uintptr_t                 getMonitorOwnerOffset();
   uintptr_t                 getMonitorEntryCountOffset();

   uintptr_t                 getRealtimeSizeClassesOffset();
   uintptr_t                 getSmallCellSizesOffset();
   uintptr_t                 getSizeClassesIndexOffset();

   virtual int32_t            getFirstArrayletPointerOffset( TR::Compilation *comp);
   int32_t                    getArrayletFirstElementOffset(int8_t elementSize,  TR::Compilation *comp);

   virtual uintptr_t         thisThreadGetProfilingBufferCursorOffset();
   virtual uintptr_t         thisThreadGetProfilingBufferEndOffset();
   virtual uintptr_t         thisThreadGetOSRBufferOffset();
   virtual uintptr_t         thisThreadGetOSRScratchBufferOffset();
   virtual uintptr_t         thisThreadGetOSRFrameIndexOffset();
   virtual uintptr_t         thisThreadGetOSRReturnAddressOffset();

#if defined(TR_TARGET_S390)
   virtual uint16_t           thisThreadGetTDBOffset();
#endif

   virtual uintptr_t         thisThreadGetGSIntermediateResultOffset();
   virtual uintptr_t         thisThreadGetConcurrentScavengeActiveByteAddressOffset();
   virtual uintptr_t         thisThreadGetEvacuateBaseAddressOffset();
   virtual uintptr_t         thisThreadGetEvacuateTopAddressOffset();
   virtual uintptr_t         thisThreadGetGSOperandAddressOffset();
   virtual uintptr_t         thisThreadGetGSHandlerAddressOffset();

   virtual int32_t            getArraySpineShift(int32_t);
   virtual int32_t            getArrayletMask(int32_t);
   virtual int32_t            getArrayletLeafIndex(int64_t, int32_t);
   virtual int32_t            getLeafElementIndex(int64_t , int32_t);
   virtual bool               CEEHDLREnabled();

   virtual int32_t            getCAASaveOffset();
   virtual int32_t            getFlagValueForPrimitiveTypeCheck();
   virtual int32_t            getFlagValueForArrayCheck();
   virtual int32_t            getFlagValueForFinalizerCheck();
   uint32_t                   getWordOffsetToGCFlags();
   uint32_t                   getWriteBarrierGCFlagMaskAsByte();
   virtual int32_t            getByteOffsetToLockword(TR_OpaqueClassBlock *clazzPointer);
   virtual int32_t            getInitialLockword(TR_OpaqueClassBlock* clazzPointer);

   virtual bool               isEnableGlobalLockReservationSet();

   virtual bool               javaLangClassGetModifiersImpl(TR_OpaqueClassBlock * clazzPointer, int32_t &result);
   virtual int32_t            getJavaLangClassHashCode(TR::Compilation * comp, TR_OpaqueClassBlock * clazzPointer, bool &hashCodeComputed);

   virtual bool traceableMethodsCanBeInlined() { return false; }

   bool isAnyMethodTracingEnabled(TR_OpaqueMethodBlock *method);


   bool                       isLogSamplingSet();

   virtual bool               hasFinalFieldsInClass(TR_OpaqueClassBlock * classPointer);

   virtual TR_OpaqueClassBlock *getProfiledClassFromProfiledInfo(TR_ExtraAddressInfo *profiledInfo);

   virtual bool               scanReferenceSlotsInClassForOffset( TR::Compilation * comp, TR_OpaqueClassBlock * classPointer, int32_t offset);
   virtual int32_t            findFirstHotFieldTenuredClassOffset( TR::Compilation *comp, TR_OpaqueClassBlock *opclazz);

   virtual bool               isInlineableNativeMethod( TR::Compilation *, TR::ResolvedMethodSymbol * methodSymbol);
   //receiverClass is for specifying a more specific receiver type. otherwise it is determined from the call.
   virtual bool               maybeHighlyPolymorphic(TR::Compilation *, TR_ResolvedMethod *caller, int32_t cpIndex, TR::Method *callee, TR_OpaqueClassBlock *receiverClass = NULL);
   virtual bool isQueuedForVeryHotOrScorching(TR_ResolvedMethod *calleeMethod, TR::Compilation *comp);

   //getSymbolAndFindInlineTarget queries
   virtual bool supressInliningRecognizedInitialCallee(TR_CallSite* callsite, TR::Compilation* comp);
   virtual int checkInlineableWithoutInitialCalleeSymbol (TR_CallSite* callsite, TR::Compilation* comp);

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   virtual bool inlineRecognizedCryptoMethod(TR_CallTarget* target, TR::Compilation* comp);
   virtual bool inlineNativeCryptoMethod(TR::Node *callNode, TR::Compilation *comp);
#endif

   virtual TR::Node * inlineNativeCall(TR::Compilation *,  TR::TreeTop *, TR::Node *) { return 0; }

   virtual bool               inlinedAllocationsMustBeVerified() { return false; }

   virtual int32_t            getInvocationCount(TR_OpaqueMethodBlock *methodInfo);
   virtual bool               setInvocationCount(TR_OpaqueMethodBlock *methodInfo, int32_t oldCount, int32_t newCount);
   virtual bool               startAsyncCompile(TR_OpaqueMethodBlock *methodInfo, void *oldStartPC, bool *queued, TR_OptimizationPlan *optimizationPlan  = NULL);
   virtual bool               isBeingCompiled(TR_OpaqueMethodBlock *methodInfo, void *startPC);
   virtual uint32_t           virtualCallOffsetToVTableSlot(uint32_t offset);
   virtual uint32_t           vTableSlotToVirtualCallOffset(uint32_t vTableSlot);
   virtual void *             addressOfFirstClassStatic(TR_OpaqueClassBlock *);

   virtual TR_ResolvedMethod * getDefaultConstructor(TR_Memory *, TR_OpaqueClassBlock *);

   virtual uint32_t           getNewInstanceImplVirtualCallOffset();

   char * classNameChars(TR::Compilation *comp, TR::SymbolReference * symRef, int32_t & length);

   virtual char *             getClassNameChars(TR_OpaqueClassBlock * clazz, int32_t & length);
   virtual char *             getClassSignature_DEPRECATED(TR_OpaqueClassBlock * clazz, int32_t & length, TR_Memory *);
   virtual char *             getClassSignature(TR_OpaqueClassBlock * clazz, TR_Memory *);
   virtual int32_t            printTruncatedSignature(char *sigBuf, int32_t bufLen, TR_OpaqueMethodBlock *method);
   virtual int32_t            printTruncatedSignature(char *sigBuf, int32_t bufLen, J9UTF8 *className, J9UTF8 *name, J9UTF8 *signature);

   virtual bool               isClassFinal(TR_OpaqueClassBlock *);
   virtual bool               isClassArray(TR_OpaqueClassBlock *);
   virtual bool               isFinalFieldPointingAtJ9Class(TR::SymbolReference *symRef, TR::Compilation *comp);

   virtual void *getJ2IThunk(char *signatureChars, uint32_t signatureLength,  TR::Compilation *comp);
   virtual void *setJ2IThunk(char *signatureChars, uint32_t signatureLength, void *thunkptr,  TR::Compilation *comp);
   virtual void *setJ2IThunk(TR::Method *method, void *thunkptr, TR::Compilation *comp);  // DMDM: J9 now

   // JSR292 {{{

   // J2I thunk support
   //
   virtual void * getJ2IThunk(TR::Method *method, TR::Compilation *comp);  // DMDM: J9 now

   virtual char    *getJ2IThunkSignatureForDispatchVirtual(char *invokeHandleSignature, uint32_t signatureLength,  TR::Compilation *comp);
   virtual TR::Node *getEquivalentVirtualCallNodeForDispatchVirtual(TR::Node *node,  TR::Compilation *comp);
   virtual bool     needsInvokeExactJ2IThunk(TR::Node *node,  TR::Compilation *comp);
   virtual void setInvokeExactJ2IThunk(void *thunkptr, TR::Compilation *comp);

   virtual void *findPersistentJ2IThunk(char *signatureChars);
   virtual void *findPersistentThunk(char *signatureChars, uint32_t signatureLength);
   virtual void * persistJ2IThunk(void *thunk);


   // Object manipulation
   //
   virtual uintptr_t  methodHandle_thunkableSignature(uintptr_t methodHandle);
   virtual void *      methodHandle_jitInvokeExactThunk(uintptr_t methodHandle);
   virtual uintptr_t  methodHandle_type(uintptr_t methodHandle);
   virtual uintptr_t  methodType_descriptor(uintptr_t methodType);
   virtual uintptr_t *mutableCallSite_bypassLocation(uintptr_t mutableCallSite);
   virtual uintptr_t *mutableCallSite_findOrCreateBypassLocation(uintptr_t mutableCallSite);

   virtual TR_OpaqueMethodBlock *lookupArchetype(TR_OpaqueClassBlock *clazz, char *name, char *signature);
   virtual TR_OpaqueMethodBlock *lookupMethodHandleThunkArchetype(uintptr_t methodHandle);
   virtual TR_ResolvedMethod    *createMethodHandleArchetypeSpecimen(TR_Memory *, uintptr_t *methodHandleLocation, TR_ResolvedMethod *owningMethod = 0);
   virtual TR_ResolvedMethod    *createMethodHandleArchetypeSpecimen(TR_Memory *, TR_OpaqueMethodBlock *archetype, uintptr_t *methodHandleLocation, TR_ResolvedMethod *owningMethod = 0); // more efficient if you already know the archetype

   virtual uintptr_t mutableCallSiteCookie(uintptr_t mutableCallSite, uintptr_t potentialCookie=0);
   TR::KnownObjectTable::Index mutableCallSiteEpoch(TR::Compilation *comp, uintptr_t mutableCallSite);

   bool hasMethodTypesSideTable();

   // Openjdk implementation
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
   /*
    * \brief
    *    Return MemberName.vmtarget, a J9method pointer for method represented by `memberName`
    *    Caller must acquire VM access
    */
   virtual TR_OpaqueMethodBlock* targetMethodFromMemberName(uintptr_t memberName);
   /*
    * \brief
    *    Return MemberName.vmtarget, a J9method pointer for method represented by `memberName`
    *    VM access is not required
    */
   virtual TR_OpaqueMethodBlock* targetMethodFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex);
   /*
    * \brief
    *    Return MethodHandle.form.vmentry.vmtarget, J9method for the underlying java method
    *    The J9Method is the target to be invoked intrinsically by MethodHandle.invokeBasic
    *    Caller must acquire VM access
    */
   virtual TR_OpaqueMethodBlock* targetMethodFromMethodHandle(uintptr_t methodHandle);
   /*
    * \brief
    *    Return MethodHandle.form.vmentry.vmtarget, J9method for the underlying java method
    *    The J9Method is the target to be invoked intrinsically by MethodHandle.invokeBasic
    *    VM access is not required
    */
   virtual TR_OpaqueMethodBlock* targetMethodFromMethodHandle(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex);
   /*
    * \brief
    *    Return MemberName.vmindex, a J9JNIMethodID pointer containing vtable/itable offset for the MemberName method
    *    Caller must acquire VM access
    */
   virtual J9JNIMethodID* jniMethodIdFromMemberName(uintptr_t memberName);
   /*
    * \brief
    *    Return MemberName.vmindex, a J9JNIMethodID pointer containing vtable/itable offset for the MemberName method
    *    VM access is not required
    */
   virtual J9JNIMethodID* jniMethodIdFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex);
   /*
    * \brief
    *    Return vtable or itable index of a method represented by MemberName
    *    Caller must acquire VM access
    */
   virtual int32_t vTableOrITableIndexFromMemberName(uintptr_t memberName);
   /*
    * \brief
    *    Return vtable or itable index of a method represented by MemberName
    *    VM access is not required
    */
   virtual int32_t vTableOrITableIndexFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex);
   /*
    * \brief
    *    Create and return a resolved method from member name index of an invoke cache array.
    *    Encapsulates code that requires VM access to make JITServer support easier.
    */
   virtual TR_ResolvedMethod* targetMethodFromInvokeCacheArrayMemberNameObj(TR::Compilation *comp, TR_ResolvedMethod *owningMethod, uintptr_t *invokeCacheArray);

   /*
    * \brief
    *    Return a Known Object Table index of an invoke cache array appendix element.
    *    Encapsulates code that requires VM access to make JITServer support easier.
    */
   virtual TR::KnownObjectTable::Index getKnotIndexOfInvokeCacheArrayAppendixElement(TR::Compilation *comp, uintptr_t *invokeCacheArray);

   /**
    * \brief
    *    Refines invokeCache element symRef with known object index for invokehandle and invokedynamic bytecode
    *
    * \param comp the compilation object
    * \param originalSymRef the original symref to refine
    * \param invokeCacheArray the array containing the element we use to get known object index
    * \return TR::SymbolReference* the refined symRef
    */
   virtual TR::SymbolReference* refineInvokeCacheElementSymRefWithKnownObjectIndex(TR::Compilation *comp, TR::SymbolReference *originalSymRef, uintptr_t *invokeCacheArray);

   /*
    * \brief
    *    Get the signature For MethodHandle.linkToStatic call for unresolved invokehandle
    *
    *    For unresolved invokeHandle, we do not know the adapter method at
    *    compile time. The call is expressed as a call to the signature-polymorphic
    *    MethodHandle.linkToStatic method. In addition to the arguments of the original call,
    *    we need to provide the memberName and appendix objects as the last two arguments, in
    *    addition to the MethodHandle object as the first argument. Therefore, we need to modify
    *    the signature of the original call and adapt it to accept three extra arguments.
    *
    * \param comp the current compilation
    * \param romMethodSignature the ROM Method signature to be processed
    * \param signatureLength the length of the resulting signature
    * \return char * the signature for linkToStatic
    */
   char * getSignatureForLinkToStaticForInvokeHandle(TR::Compilation* comp, J9UTF8* romMethodSignature, int32_t &signatureLength);


   /*
    * \brief
    *    Get the signature for MethodHandle.linkToStatic call for unresolved invokedynamic
    *
    *    For unresolved invokeDynamic, we do not know the adapter method at
    *    compile time. The call is expressed as a call to the signature-polymorphic
    *    MethodHandle.linkToStatic method. In addition to the arguments of the original call,
    *    we need to provide the memberName and appendix objects as the last two arguments.
    *    Therefore, we need to modify the signature of the original call and adapt it to accept
    *    the two extra arguments.
    *
    * \param comp the current compilation
    * \param romMethodSignature the ROM Method signature to be processed
    * \param signatureLength the length of the resulting signature
    * \return char * the signature for linkToStatic
    */
   char * getSignatureForLinkToStaticForInvokeDynamic(TR::Compilation* comp, J9UTF8* romMethodSignature, int32_t &signatureLength);
#endif

   // JSR292 }}}

   /**
    * \brief
    *   Return a Known Object Table index of a java/lang/invoke/MemberName field
    *
    * \param comp the compilation object
    * \param mhIndex known object index of the java/lang/invoke/MemberName object
    * \param fieldName the name of the field for which we return the known object index
    */
   virtual TR::KnownObjectTable::Index getMemberNameFieldKnotIndexFromMethodHandleKnotIndex(TR::Compilation *comp, TR::KnownObjectTable::Index mhIndex, char *fieldName);

   virtual uintptr_t getFieldOffset( TR::Compilation * comp, TR::SymbolReference* classRef, TR::SymbolReference* fieldRef);
   /*
    * \brief
    *    tell whether it's possible to dereference a field given the field symbol reference at compile time
    *
    * \fieldRef
    *    symbol reference of the field
    */
   virtual bool canDereferenceAtCompileTime(TR::SymbolReference *fieldRef,  TR::Compilation *comp);

   /*
    * \brief
    *    tell whether it's possible to dereference a field given the field symbol at compile time
    *
    * \fieldSymbol
    *    symbol of the field
    *
    * \cpIndex
    *    constant pool index
    *
    * \owningMethod
    *    the method accessing the field
    */
   virtual bool canDereferenceAtCompileTimeWithFieldSymbol(TR::Symbol *fieldSymbol, int32_t cpIndex, TR_ResolvedMethod *owningMethod);

   virtual bool getStringFieldByName( TR::Compilation *, TR::SymbolReference *stringRef, TR::SymbolReference *fieldRef, void* &pResult);
   virtual bool      isString(TR_OpaqueClassBlock *clazz);
   virtual TR_YesNoMaybe typeReferenceStringObject(TR_OpaqueClassBlock *clazz);
   virtual bool      isJavaLangObject(TR_OpaqueClassBlock *clazz);
   virtual bool      isString(uintptr_t objectPointer);
   virtual int32_t   getStringLength(uintptr_t objectPointer);
   virtual uint16_t  getStringCharacter(uintptr_t objectPointer, int32_t index);
   virtual intptr_t getStringUTF8Length(uintptr_t objectPointer);
   virtual char     *getStringUTF8      (uintptr_t objectPointer, char *buffer, intptr_t bufferSize);

   virtual uint32_t getVarHandleHandleTableOffset(TR::Compilation *);

   virtual void reportILGeneratorPhase();
   virtual void reportOptimizationPhase(OMR::Optimizations);
   virtual void reportAnalysisPhase(uint8_t id);
   virtual void reportOptimizationPhaseForSnap(OMR::Optimizations, TR::Compilation *comp = NULL);
   virtual void reportCodeGeneratorPhase(TR::CodeGenPhase::PhaseValue phase);
   virtual int32_t saveCompilationPhase();
   virtual void restoreCompilationPhase(int32_t phase);

   virtual void reportPrexInvalidation(void * startPC);

   virtual bool compilationShouldBeInterrupted( TR::Compilation *, TR_CallingContext);
   bool checkForExclusiveAcquireAccessRequest( TR::Compilation *);
   virtual bool haveAccess();
   virtual bool haveAccess(TR::Compilation *);
   virtual void releaseAccess(TR::Compilation *);
   virtual bool tryToAcquireAccess( TR::Compilation *, bool *);

   virtual bool compileMethods(TR::OptionSet*, void *);
   virtual void waitOnCompiler(void *);

   bool tossingCode();

   virtual TR::KnownObjectTable::Index getCompiledMethodReceiverKnownObjectIndex( TR::Compilation *comp);
   virtual bool methodMayHaveBeenInterpreted( TR::Compilation *comp);
   virtual bool canRecompileMethodWithMatchingPersistentMethodInfo( TR::Compilation *comp);

   virtual void               revertToInterpreted(TR_OpaqueMethodBlock *method);

   virtual bool               argumentCanEscapeMethodCall(TR::MethodSymbol *method, int32_t argIndex);

   virtual bool               hasTwoWordObjectHeader();
   virtual int32_t *          getReferenceSlotsInClass( TR::Compilation *, TR_OpaqueClassBlock *classPointer);
   virtual void               initializeLocalObjectHeader( TR::Compilation *, TR::Node * allocationNode,  TR::TreeTop * allocationTreeTop);
   virtual void               initializeLocalArrayHeader ( TR::Compilation *, TR::Node * allocationNode,  TR::TreeTop * allocationTreeTop);
   virtual TR::TreeTop*        initializeClazzFlagsMonitorFields(TR::Compilation*, TR::TreeTop* prevTree, TR::Node* allocationNode, TR::Node* classNode, TR_OpaqueClassBlock* ramClass);

   bool shouldPerformEDO(TR::Block *catchBlock,  TR::Compilation * comp);

   // Multiple code cache support
   virtual TR::CodeCache *getDesignatedCodeCache(TR::Compilation *comp); // MCT
   virtual void setHasFailedCodeCacheAllocation(); // MCT
   void *getCCPreLoadedCodeAddress(TR::CodeCache *codeCache, TR_CCPreLoadedCode h, TR::CodeGenerator *cg);

   virtual void reserveTrampolineIfNecessary( TR::Compilation *, TR::SymbolReference *symRef, bool inBinaryEncoding);
   virtual TR::CodeCache* getResolvedTrampoline( TR::Compilation *, TR::CodeCache* curCache, J9Method * method, bool inBinaryEncoding) = 0;

   // Interpreter profiling support
   virtual TR_IProfiler  *getIProfiler();
   virtual TR_AbstractInfo *createIProfilingValueInfo( TR_ByteCodeInfo &bcInfo,  TR::Compilation *comp);
   virtual TR_ExternalValueProfileInfo *getValueProfileInfoFromIProfiler(TR_ByteCodeInfo &bcInfo,  TR::Compilation *comp);
   uint32_t *getAllocationProfilingDataPointer(TR_ByteCodeInfo &bcInfo, TR_OpaqueClassBlock *clazz, TR_OpaqueMethodBlock *method,  TR::Compilation *comp);
   uint32_t *getGlobalAllocationDataPointer();
   virtual TR_ExternalProfiler   *hasIProfilerBlockFrequencyInfo(TR::Compilation& comp);
   virtual int32_t getCGEdgeWeight(TR::Node *callerNode, TR_OpaqueMethodBlock *callee,  TR::Compilation *comp);
   virtual int32_t getIProfilerCallCount(TR_OpaqueMethodBlock *caller, int32_t bcIndex,  TR::Compilation *);
   virtual int32_t getIProfilerCallCount(TR_OpaqueMethodBlock *callee, TR_OpaqueMethodBlock *caller, int32_t bcIndex,  TR::Compilation *);
   virtual void setIProfilerCallCount(TR_OpaqueMethodBlock *caller, int32_t bcIndex, int32_t count,  TR::Compilation *);
   virtual int32_t getIProfilerCallCount(TR_ByteCodeInfo &bcInfo,  TR::Compilation *comp);
   virtual void    setIProfilerCallCount(TR_ByteCodeInfo &bcInfo, int32_t count,  TR::Compilation *comp);
   virtual bool    isCallGraphProfilingEnabled();
   virtual bool    isClassLibraryMethod(TR_OpaqueMethodBlock *method, bool vettedForAOT = false);
   virtual bool    isClassLibraryClass(TR_OpaqueClassBlock *clazz);
   virtual int32_t getMaxCallGraphCallCount();

   virtual bool    getSupportsRecognizedMethods();

   // Hardware profiling support
   virtual void    createHWProfilerRecords(TR::Compilation *comp);

   virtual uint64_t getUSecClock();
   virtual uint64_t getHighResClock();
   virtual uint64_t getHighResClockResolution();

   TR_JitPrivateConfig *getPrivateConfig();
   static TR_JitPrivateConfig *getPrivateConfig(void *jitConfig);

   TR_YesNoMaybe vmThreadIsCompilationThread() { return _vmThreadIsCompilationThread; }

   virtual bool releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded( TR::Compilation *comp, bool *hadClassUnloadMonitor);
   virtual void acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded( TR::Compilation *comp, bool hadVMAccess, bool hadClassUnloadMonitor);
   // The following two methods are defined here so that they can be easily inlined
   virtual bool acquireVMAccessIfNeeded() { return acquireVMaccessIfNeeded(vmThread(), _vmThreadIsCompilationThread); }
   virtual void releaseVMAccessIfNeeded(bool haveAcquiredVMAccess) { releaseVMaccessIfNeeded(vmThread(), haveAcquiredVMAccess); }
   virtual uintptr_t         getBytecodePC(TR_OpaqueMethodBlock *method, TR_ByteCodeInfo &bcInfo);

   virtual bool hardwareProfilingInstructionsNeedRelocation() { return false; }

   // support for using compressed class pointers
   // in the future we may add a base value to the classOffset to transform it
   // into a real J9Class pointer
   virtual TR_OpaqueClassBlock *convertClassPtrToClassOffset(J9Class *clazzPtr);
   virtual J9Method *convertMethodOffsetToMethodPtr(TR_OpaqueMethodBlock *methodOffset);
   virtual TR_OpaqueMethodBlock *convertMethodPtrToMethodOffset(J9Method *methodPtr);

   virtual TR_OpaqueClassBlock *getHostClass(TR_OpaqueClassBlock *clazzOffset);
   virtual bool canAllocateInlineClass(TR_OpaqueClassBlock *clazz);
   virtual bool isClassLoadedBySystemClassLoader(TR_OpaqueClassBlock *clazz);
   virtual intptr_t getVFTEntry(TR_OpaqueClassBlock *clazz, int32_t offset);

   TR::TreeTop * lowerAsyncCheck(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   virtual bool isMethodTracingEnabled(TR_OpaqueMethodBlock *method);
   virtual bool isMethodTracingEnabled(J9Method *j9method)
      {
      return isMethodTracingEnabled((TR_OpaqueMethodBlock *)j9method);
      }

   // Is method generated for LambdaForm
   virtual bool isLambdaFormGeneratedMethod(TR_OpaqueMethodBlock *method);
   virtual bool isLambdaFormGeneratedMethod(TR_ResolvedMethod *method);

   virtual bool isSelectiveMethodEnterExitEnabled();

   virtual bool canMethodEnterEventBeHooked();
   virtual bool canMethodExitEventBeHooked();
   virtual bool methodsCanBeInlinedEvenIfEventHooksEnabled(TR::Compilation *comp);

   virtual bool canExceptionEventBeHooked();

   TR::TreeTop * lowerMethodHook(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   TR::TreeTop * lowerArrayLength(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   TR::TreeTop * lowerContigArrayLength(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   TR::TreeTop * lowerMultiANewArray(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   TR::TreeTop * lowerToVcall(TR::Compilation *, TR::Node * root,  TR::TreeTop * treeTop);
   virtual U_8 * fetchMethodExtendedFlagsPointer(J9Method *method); // wrapper method of fetchMethodExtendedFlagsPointer in util/extendedmethodblockaccess.c, for JITServer override purpose
   virtual void * getStaticHookAddress(int32_t event);

   TR::Node * initializeLocalObjectFlags(TR::Compilation *, TR::Node * allocationNode, TR_OpaqueClassBlock * ramClass);

   /**
    * \brief Load class flags field of the specified class and test whether any of the
    *        specified flags is set.
    * \param j9ClassRefNode A node representing a reference to a \ref J9Class
    * \param flagsToTest    The class field flags that are to be checked
    * \return \ref TR::Node that evaluates to a non-zero integer if any of the specified
    *         flags is set; or evaluates to zero, otherwise.
    */
   TR::Node * testAreSomeClassFlagsSet(TR::Node *j9ClassRefNode, uint32_t flagsToTest);

   /**
    * \brief Load class flags field of the specified class and test whether the value type
    *        field is set.
    * \param j9ClassRefNode A node representing a reference to a \ref J9Class
    * \return \ref TR::Node that evaluates to a non-zero integer if the class is a value type,
    *         or zero if the class is an identity type
    */
   TR::Node * testIsClassValueType(TR::Node *j9ClassRefNode);

   /**
    * \brief Check whether or not the array component class is value type
    * \param arrayBaseAddressNode A node representing a reference to the array base address
    * \param ifCmpOp If comparison opCode such as ifcmpeq or ificmpne
    * \return \ref TR::Node that compares the array component class J9ClassIsValueType flag to a zero integer
    */
   TR::Node * checkArrayCompClassValueType(TR::Node *arrayBaseAddressNode, TR::ILOpCodes ifCmpOp);

   virtual J9JITConfig *getJ9JITConfig() { return _jitConfig; }

   virtual int32_t getCompThreadIDForVMThread(void *vmThread);
   virtual uint8_t getCompilationShouldBeInterruptedFlag();

   bool shouldSleep() { _shouldSleep = !_shouldSleep; return _shouldSleep; } // no need for virtual; we need this per thread

   virtual bool isAnonymousClass(TR_OpaqueClassBlock *j9clazz) { return (J9_ARE_ALL_BITS_SET(((J9Class*)j9clazz)->romClass->extraModifiers, J9AccClassAnonClass)); }
   virtual bool isAnonymousClass(J9ROMClass *romClass) { return (J9_ARE_ALL_BITS_SET(romClass->extraModifiers, J9AccClassAnonClass)); }
   virtual int64_t getCpuTimeSpentInCompThread(TR::Compilation * comp); // resolution is 0.5 sec or worse. Returns -1 if unavailable

   virtual void * getClassLoader(TR_OpaqueClassBlock * classPointer);
   virtual bool getReportByteCodeInfoAtCatchBlock();

   J9VMThread *            _vmThread;
   J9PortLibrary *         _portLibrary;
   J9JITConfig *           _jitConfig;
   J9InternalVMFunctions * _vmFunctionTable;
   TR::CompilationInfo *    _compInfo; // storing _compInfo in multiple places could spell trouble when we free the structure
   TR_IProfiler  *         _iProfiler;
   int32_t                 _hwProfilerShouldNotProcessBuffers;
   uint8_t*                _bufferStart;

   // To minimize the overhead of testing if vmThread is the compilation thread (which may be
   // performed very often when compiling without VMaccess) we store this information here
   // The TR_J9VMBase is attached to the thread in vmThread->jitVMwithThreadInfo and
   // vmThread->aotVMwithThreadInfo
   TR_YesNoMaybe          _vmThreadIsCompilationThread;
   TR::CompilationInfoPerThread *_compInfoPT;
   bool                   _shouldSleep; // used to throttle application threads

   TR_J9SharedCache *     _sharedCache;

   static int32_t *       staticDFPHWAvailField;
   static bool            cachedStaticDFPAvailField;

   static int32_t *       staticStringEnableCompressionFieldAddr;

   static char            x86VendorID[13];
   static bool            x86VendorIDInitialized;

   virtual J9Class * matchRAMclassFromROMclass(J9ROMClass * clazz,  TR::Compilation * comp);
   virtual J9VMThread * getCurrentVMThread();

   uint8_t *allocateDataCacheRecord(uint32_t numBytes,  TR::Compilation *comp, bool contiguous,
                                bool *shouldRetryAllocation, uint32_t allocationType, uint32_t *size);

   virtual int findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* classSig, char** methodSig, TR::SymbolReference** symRefs, int methodCount);
   virtual int findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char** methodSig, TR::SymbolReference** symRefs, int methodCount);
   virtual TR::SymbolReference* findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* classSig, char* methodSig);
   virtual TR::SymbolReference* findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* methodSig);

   virtual bool isOwnableSyncClass(TR_OpaqueClassBlock *clazz);
   const char *getJ9MonitorName(J9ThreadMonitor* monitor);

   virtual TR_J9SharedCache *sharedCache() { return _sharedCache; }
   virtual void              freeSharedCache();

   const char *getByteCodeName(uint8_t opcode);

   virtual int32_t maxInternalPlusPinningArrayPointers(TR::Compilation* comp);

   virtual void *getSystemClassLoader();

   virtual TR_EstimateCodeSize *getCodeEstimator( TR::Compilation *comp);
   virtual void releaseCodeEstimator( TR::Compilation *comp, TR_EstimateCodeSize *estimator);

   virtual bool acquireClassTableMutex();
   virtual void releaseClassTableMutex(bool);

   virtual bool classInitIsFinished(TR_OpaqueClassBlock *clazz) { return (((J9Class*)clazz)->initializeStatus & J9ClassInitStatusMask) == J9ClassInitSucceeded; }

   virtual TR_OpaqueClassBlock * getClassFromNewArrayType(int32_t arrayType);
   virtual TR_OpaqueClassBlock * getClassFromNewArrayTypeNonNull(int32_t arrayType);

   virtual TR_OpaqueClassBlock *getClassFromCP(J9ConstantPool *cp);
   virtual J9ROMMethod *getROMMethodFromRAMMethod(J9Method *ramMethod);

   // --------------------------------------------------------------------------
   // Object model
   // --------------------------------------------------------------------------

   virtual bool getNurserySpaceBounds(uintptr_t *base, uintptr_t *top);
   virtual UDATA getLowTenureAddress();
   virtual UDATA getHighTenureAddress();
   virtual uintptr_t getThreadLowTenureAddressPointerOffset();
   virtual uintptr_t getThreadHighTenureAddressPointerOffset();
   virtual uintptr_t thisThreadRememberedSetFragmentOffset();
   virtual uintptr_t getFragmentParentOffset();
   virtual uintptr_t getRememberedSetGlobalFragmentOffset();
   virtual uintptr_t getLocalFragmentOffset();
   virtual int32_t getLocalObjectAlignmentInBytes();

   uint32_t getInstanceFieldOffsetIncludingHeader(char* classSignature, char * fieldName, char * fieldSig, TR_ResolvedMethod* method);

   virtual void markHotField( TR::Compilation *, TR::SymbolReference *, TR_OpaqueClassBlock *, bool);
   virtual void reportHotField(int32_t reducedCpuUtil, J9Class* clazz, uint8_t fieldOffset,  uint32_t reducedFrequency);
   virtual bool isHotReferenceFieldRequired();
   virtual void markClassForTenuredAlignment( TR::Compilation *comp, TR_OpaqueClassBlock *opclazz, uint32_t alignFromStart);

   virtual bool shouldDelayAotLoad() { return false; }

   virtual void *getLocationOfClassLoaderObjectPointer(TR_OpaqueClassBlock *classPointer);
   virtual bool isMethodBreakpointed(TR_OpaqueMethodBlock *method);

   virtual bool instanceOfOrCheckCast(J9Class *instanceClass, J9Class* castClass);
   virtual bool instanceOfOrCheckCastNoCacheUpdate(J9Class *instanceClass, J9Class* castClass);
   virtual bool canRememberClass(TR_OpaqueClassBlock *classPtr) { return false; }
   virtual bool isStringCompressionEnabledVM();
   virtual void *getInvokeExactThunkHelperAddress(TR::Compilation *comp, TR::SymbolReference *glueSymRef, TR::DataType dataType);

   bool inSnapshotMode();

protected:

   enum // _flags
      {
      TraceIsEnabled = 0x0000001,
      FSDIsEnabled   = 0x0000002,
      DummyLastEnum
      };

   flags32_t _flags;

   };

class TR_J9VM : public TR_J9VMBase
   {
public:
   TR_J9VM(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo, J9VMThread * vmContext);

   virtual bool               classHasBeenExtended(TR_OpaqueClassBlock *);
   virtual bool               classHasBeenReplaced(TR_OpaqueClassBlock *);
   virtual TR::Node *         inlineNativeCall( TR::Compilation *,  TR::TreeTop *, TR::Node *);
   virtual bool               transformJlrMethodInvoke(J9Method *callerMethod, J9Class *callerClass);
   virtual TR_OpaqueClassBlock *getClassOfMethod(TR_OpaqueMethodBlock *method);
   virtual int32_t            getObjectAlignmentInBytes();

   virtual void               initializeProcessorType();
   virtual void               initializeHasResumableTrapHandler();
   virtual void               initializeHasFixedFrameC_CallingConvention();

   virtual bool               isPublicClass(TR_OpaqueClassBlock *clazz);
   virtual TR_OpaqueMethodBlock *getMethodFromName(char * className, char *methodName, char *signature);

   virtual TR_OpaqueClassBlock * getComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass);
   virtual TR_OpaqueClassBlock * getArrayClassFromComponentClass(TR_OpaqueClassBlock *componentClass);
   virtual TR_OpaqueClassBlock * getLeafComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass);
   virtual int32_t               getNewArrayTypeFromClass(TR_OpaqueClassBlock *clazz);
   virtual TR_OpaqueClassBlock * getClassFromSignature(const char * sig, int32_t length, TR_ResolvedMethod *method, bool isVettedForAOT=false);
   virtual TR_OpaqueClassBlock * getClassFromSignature(const char * sig, int32_t length, TR_OpaqueMethodBlock *method, bool isVettedForAOT=false);
   virtual TR_OpaqueClassBlock * getBaseComponentClass(TR_OpaqueClassBlock * clazz, int32_t & numDims);
   virtual TR_OpaqueClassBlock * getSystemClassFromClassName(const char * name, int32_t length, bool isVettedForAOT=false);
   virtual TR_YesNoMaybe      isInstanceOf(TR_OpaqueClassBlock *instanceClass, TR_OpaqueClassBlock *castClass, bool instanceIsFixed, bool castIsFixed = true, bool optimizeForAOT=false);

   virtual bool               stackWalkerMaySkipFrames(TR_OpaqueMethodBlock *method, TR_OpaqueClassBlock *methodClass);

   virtual TR_OpaqueClassBlock *             getSuperClass(TR_OpaqueClassBlock *classPointer);
   virtual bool               isSameOrSuperClass(J9Class *superClass, J9Class *subClass);
   virtual bool               isUnloadAssumptionRequired(TR_OpaqueClassBlock *, TR_ResolvedMethod *);

   virtual TR_OpaqueClassBlock * getClassClassPointer(TR_OpaqueClassBlock *);

   // for replay
   virtual TR_OpaqueClassBlock * getClassFromMethodBlock(TR_OpaqueMethodBlock *);

   virtual const char *       sampleSignature(TR_OpaqueMethodBlock * aMethod, char *buf, int32_t bufLen, TR_Memory *memory);

   virtual TR_ResolvedMethod * getObjectNewInstanceImplMethod(TR_Memory *);
   virtual TR_OpaqueMethodBlock * getObjectNewInstanceImplMethod();

   virtual intptr_t          methodTrampolineLookup( TR::Compilation *, TR::SymbolReference *symRef, void *callSite);
   virtual TR::CodeCache*    getResolvedTrampoline( TR::Compilation *, TR::CodeCache *curCache, J9Method * method, bool inBinaryEncoding);

   virtual TR_IProfiler *         getIProfiler();

   virtual void                   createHWProfilerRecords(TR::Compilation *comp);

   virtual TR_OpaqueClassBlock *  getClassOffsetForAllocationInlining(J9Class *clazzPtr);
   virtual J9Class *              getClassForAllocationInlining( TR::Compilation *comp, TR::SymbolReference *classSymRef);
   virtual bool                   supportAllocationInlining( TR::Compilation *comp, TR::Node *node);
   virtual TR_OpaqueClassBlock *  getPrimitiveArrayAllocationClass(J9Class *clazz);
   virtual uint32_t               getPrimitiveArrayOffsetInJavaVM(uint32_t arrayType);

   virtual TR_StaticFinalData dereferenceStaticFinalAddress(void *staticAddress, TR::DataType addressType);

   TR_OpaqueClassBlock * getClassFromSignature(const char * sig, int32_t sigLength, J9ConstantPool * constantPool);

private:
   void transformJavaLangClassIsArrayOrIsPrimitive( TR::Compilation *, TR::Node * callNode,  TR::TreeTop * treeTop, int32_t andMask);
   void transformJavaLangClassIsArray( TR::Compilation *, TR::Node * callNode,  TR::TreeTop * treeTop);
   };

#if defined(J9VM_OPT_SHARED_CLASSES) && defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
class TR_J9SharedCacheVM : public TR_J9VM
   {
public:
   TR_J9SharedCacheVM(J9JITConfig * aJitConfig, TR::CompilationInfo * compInfo, J9VMThread * vmContext);

   // in process of removing this query in favour of more meaningful queries below
   virtual bool               isAOT_DEPRECATED_DO_NOT_USE()                                         { return true; }

   // replacing calls to isAOT
   virtual bool               canUseSymbolValidationManager() { return true; }
   virtual bool               supportsCodeCacheSnippets()                     { return false; }
   virtual bool               canRelocateDirectNativeCalls()                  { return false; }
   virtual bool               needClassAndMethodPointerRelocations()          { return true; }
   virtual bool               inlinedAllocationsMustBeVerified()              { return true; }
   virtual bool               needRelocationsForHelpers()                     { return true; }
   virtual bool               supportsEmbeddedHeapBounds()                    { return false; }
   virtual bool               supportsFastNanoTime()                          { return false; }
   virtual bool               needRelocationsForStatics()                     { return true; }
   virtual bool               needRelocationsForCurrentMethodPC()             { return true; }
   virtual bool               needRelocationsForLookupEvaluationData()        { return true; }
   virtual bool               needRelocationsForBodyInfoData()                { return true; }
   virtual bool               needRelocationsForPersistentInfoData()          { return true; }
   virtual bool               forceUnresolvedDispatch()                       { return true; }
   virtual bool               nopsAlsoProcessedByRelocations()                { return true; }
   virtual bool               supportsGuardMerging()                          { return false; }
   virtual bool               canDevirtualizeDispatch()                       { return false; }
   virtual bool               storeOffsetToArgumentsInVirtualIndirectThunks() { return true; }
   virtual bool               callTargetsNeedRelocations()                    { return true; }
   virtual bool               doStringPeepholing()                            { return false; }
   virtual bool               hardwareProfilingInstructionsNeedRelocation()   { return true; }
   virtual bool               supportsJitMethodEntryAlignment()               { return false; }
   virtual bool               isBenefitInliningCheckIfFinalizeObject()        { return true; }
   virtual bool               needsContiguousCodeAndDataCacheAllocation()     { return true; }
   virtual bool               needRelocatableTarget()                          { return true; }
   virtual bool               shouldDelayAotLoad();

   virtual bool               isClassLibraryMethod(TR_OpaqueMethodBlock *method, bool vettedForAOT = false);

   virtual bool               stackWalkerMaySkipFrames(TR_OpaqueMethodBlock *method, TR_OpaqueClassBlock *methodClass);

   virtual bool               isMethodTracingEnabled(TR_OpaqueMethodBlock *method);
   virtual bool               traceableMethodsCanBeInlined();

   virtual bool               canMethodEnterEventBeHooked();
   virtual bool               canMethodExitEventBeHooked();
   virtual bool               methodsCanBeInlinedEvenIfEventHooksEnabled(TR::Compilation *comp);
   virtual TR_OpaqueClassBlock *getClassOfMethod(TR_OpaqueMethodBlock *method);
   virtual void               getResolvedMethods(TR_Memory *, TR_OpaqueClassBlock *, List<TR_ResolvedMethod> *);
   virtual TR_ResolvedMethod *getResolvedMethodForNameAndSignature(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer, const char* methodName, const char *signature);
   virtual uint32_t           getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName,
                                                     uint32_t fieldLen, char * sig, uint32_t sigLen, UDATA options);

   virtual TR_OpaqueMethodBlock *getResolvedVirtualMethod(TR_OpaqueClassBlock * classObject, int32_t cpIndex, bool ignoreReResolve = true);
   virtual TR_OpaqueMethodBlock *getResolvedInterfaceMethod(TR_OpaqueMethodBlock *ownerMethod, TR_OpaqueClassBlock * classObject, int32_t cpIndex);

   virtual int32_t            getJavaLangClassHashCode(TR::Compilation * comp, TR_OpaqueClassBlock * clazzPointer, bool &hashCodeComputed);
   virtual bool               javaLangClassGetModifiersImpl(TR_OpaqueClassBlock * clazzPointer, int32_t &result);
   virtual TR_OpaqueClassBlock *             getSuperClass(TR_OpaqueClassBlock *classPointer);

   virtual bool               sameClassLoaders(TR_OpaqueClassBlock *, TR_OpaqueClassBlock *);
   virtual bool               isUnloadAssumptionRequired(TR_OpaqueClassBlock *, TR_ResolvedMethod *);
   virtual bool               classHasBeenExtended(TR_OpaqueClassBlock *);
   virtual bool               isGetImplInliningSupported();
   virtual bool               isPublicClass(TR_OpaqueClassBlock *clazz);
   virtual bool               hasFinalizer(TR_OpaqueClassBlock * classPointer);
   virtual uintptr_t         getClassDepthAndFlagsValue(TR_OpaqueClassBlock * classPointer);
   virtual uintptr_t         getClassFlagsValue(TR_OpaqueClassBlock * classPointer);
   virtual TR_OpaqueMethodBlock * getMethodFromClass(TR_OpaqueClassBlock *, char *, char *, TR_OpaqueClassBlock * = NULL);
   virtual bool               isPrimitiveClass(TR_OpaqueClassBlock *clazz);
   virtual TR_OpaqueClassBlock * getComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass);
   virtual TR_OpaqueClassBlock * getArrayClassFromComponentClass(TR_OpaqueClassBlock *componentClass);
   virtual TR_OpaqueClassBlock * getLeafComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass);
   virtual TR_OpaqueClassBlock * getBaseComponentClass(TR_OpaqueClassBlock * clazz, int32_t & numDims);
   virtual TR_OpaqueClassBlock * getClassFromNewArrayType(int32_t arrayType);
   virtual TR_YesNoMaybe      isInstanceOf(TR_OpaqueClassBlock *instanceClass, TR_OpaqueClassBlock *castClass, bool instanceIsFixed, bool castIsFixed = true, bool optimizeForAOT=false);
   virtual bool               isClassVisible(TR_OpaqueClassBlock * sourceClass, TR_OpaqueClassBlock * destClass);
   virtual bool               isPrimitiveArray(TR_OpaqueClassBlock *);
   virtual bool               isReferenceArray(TR_OpaqueClassBlock *);
   virtual TR_OpaqueClassBlock * getClassFromSignature(const char * sig, int32_t length, TR_ResolvedMethod *method, bool isVettedForAOT=false);
   virtual TR_OpaqueClassBlock * getClassFromSignature(const char * sig, int32_t length, TR_OpaqueMethodBlock *method, bool isVettedForAOT=false);
   virtual TR_OpaqueClassBlock * getSystemClassFromClassName(const char * name, int32_t length, bool isVettedForAOT=false);

   virtual intptr_t          methodTrampolineLookup( TR::Compilation *, TR::SymbolReference *symRef, void *callSite);
   virtual TR::CodeCache *    getResolvedTrampoline( TR::Compilation *, TR::CodeCache* curCache, J9Method * method, bool inBinaryEncoding);

   virtual TR_OpaqueMethodBlock *getInlinedCallSiteMethod(TR_InlinedCallSite *ics);
   virtual TR_OpaqueClassBlock *getProfiledClassFromProfiledInfo(TR_ExtraAddressInfo *profiledInfo);

   // Multiple code cache support
   virtual TR::CodeCache *getDesignatedCodeCache(TR::Compilation *comp); // MCT

   virtual void *getJ2IThunk(char *signatureChars, uint32_t signatureLength,  TR::Compilation *comp);
   virtual void *setJ2IThunk(char *signatureChars, uint32_t signatureLength, void *thunkptr,  TR::Compilation *comp);

   virtual void * persistJ2IThunk(void *thunk);
   virtual void *persistThunk(char *signatureChars, uint32_t signatureLength, uint8_t *thunkStart, uint32_t totalSize);

   virtual TR_ResolvedMethod * getObjectNewInstanceImplMethod(TR_Memory *);
   virtual bool                   supportAllocationInlining( TR::Compilation *comp, TR::Node *node);

   virtual J9Class *              getClassForAllocationInlining( TR::Compilation *comp, TR::SymbolReference *classSymRef);
   virtual bool canRememberClass(TR_OpaqueClassBlock *classPtr);

   virtual bool               ensureOSRBufferSize(TR::Compilation *comp, uintptr_t osrFrameSizeInBytes, uintptr_t osrScratchBufferSizeInBytes, uintptr_t osrStackFrameSizeInBytes);
   };

#endif

#define J9RAM_CP_BASE(x) ((x*) (vm->literals))

struct TR_PCMap
   {
   uint32_t      _numberOfEntries;
   TR_PCMapEntry _mapEntries[1]; // size is allocated dynamically
   };

typedef J9JITExceptionTable TR_MethodMetaData;

TR_MethodMetaData * createMethodMetaData(TR_J9VMBase &, TR_ResolvedMethod *, TR::Compilation *);

extern J9JITConfig * jitConfig;

static inline char * utf8Data(J9UTF8 * name)
   {
   return (char *)J9UTF8_DATA(name);
   }

static inline char * utf8Data(J9UTF8 * name, uint32_t & len)
   {
   len = J9UTF8_LENGTH(name);
   return (char *)J9UTF8_DATA(name);
   }

static inline char * utf8Data(J9UTF8 * name, int32_t & len)
   {
   len = J9UTF8_LENGTH(name);
   return (char *)J9UTF8_DATA(name);
   }

inline bool isValidVmStateIndex(uint32_t &index)
   {
   // basic bounds checking
   //
   if (index <= 0 ||
       (index >= 10 && index != 0x11))
      return false;
   //HACK: is there a better way to set the correct index?
   if (index == 0x11) index = 0x9;
   return true;
   }

inline TR_PersistentMemory * persistentMemory(J9JITConfig * jitConfig) { return (TR_PersistentMemory *)jitConfig->scratchSegment; }
bool signalOutOfMemory(J9JITConfig *);

#endif // VMJ9

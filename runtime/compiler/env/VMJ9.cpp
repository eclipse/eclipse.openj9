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

#define J9_EXTERNAL_TO_VM

#if defined(J9VM_OPT_JITSERVER)
#include "env/VMJ9Server.hpp"
#endif /* defined(J9VM_OPT_JITSERVER) */

#if defined (_MSC_VER) && (_MSC_VER < 1900)
#define snprintf _snprintf
#endif

#include "env/VMJ9.h"

#include <algorithm>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include "bcnames.h"
#include "cache.h"
#include "fastJNI.h"
#include "j9cfg.h"
#include "j9comp.h"
#include "j9consts.h"
#include "j9jitnls.h"
#include "j9list.h"
#include "j9modron.h"
#include "j9port.h"
#include "j9protos.h"
#include "j9version.h"
#include "jilconsts.h"
#include "jitprotos.h"
#include "locknursery.h"
#include "stackwalk.h"
#include "thrtypes.h"
#include "vmaccess.h"
#undef min
#undef max
#include "codegen/AheadOfTimeCompile.hpp"
#include "codegen/CodeGenerator.hpp"
#include "env/KnownObjectTable.hpp"
#include "codegen/GCStackMap.hpp"
#include "codegen/GCStackAtlas.hpp"
#include "codegen/Instruction.hpp"
#include "codegen/Snippet.hpp"
#include "compile/Compilation.hpp"
#include "compile/CompilationTypes.hpp"
#include "compile/Method.hpp"
#include "compile/ResolvedMethod.hpp"
#include "compile/VirtualGuard.hpp"
#include "control/OptionsUtil.hpp"
#include "control/CompilationController.hpp"
#include "env/StackMemoryRegion.hpp"
#include "compile/CompilationException.hpp"
#include "runtime/CodeCacheExceptions.hpp"
#include "exceptions/JITShutDown.hpp"
#include "exceptions/DataCacheError.hpp"
#include "exceptions/AOTFailure.hpp"
#include "env/exports.h"
#include "env/CompilerEnv.hpp"
#include "env/CHTable.hpp"
#include "env/ClassLoaderTable.hpp"
#include "env/IO.hpp"
#include "env/PersistentCHTable.hpp"
#include "env/PersistentInfo.hpp"
#include "env/jittypes.h"
#include "env/VMAccessCriticalSection.hpp"
#include "env/VerboseLog.hpp"
#include "infra/Monitor.hpp"
#include "infra/MonitorTable.hpp"
#include "il/DataTypes.hpp"
#include "il/J9DataTypes.hpp"
#include "il/LabelSymbol.hpp"
#include "il/MethodSymbol.hpp"
#include "il/Node.hpp"
#include "il/NodePool.hpp"
#include "il/Node_inlines.hpp"
#include "il/ParameterSymbol.hpp"
#include "il/RegisterMappedSymbol.hpp"
#include "il/ResolvedMethodSymbol.hpp"
#include "il/StaticSymbol.hpp"
#include "il/Symbol.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "ilgen/IlGeneratorMethodDetails_inlines.hpp"
#include "ilgen/IlGenRequest.hpp"
#include "infra/SimpleRegex.hpp"
#include "optimizer/Optimizer.hpp"
#include "optimizer/TransformUtil.hpp"
#include "runtime/asmprotos.h"
#include "runtime/CodeCache.hpp"
#include "runtime/CodeCacheManager.hpp"
#include "runtime/codertinit.hpp"
#include "runtime/DataCache.hpp"
#include "runtime/HookHelpers.hpp"
#include "runtime/RelocationRuntime.hpp"
#include "control/CompilationRuntime.hpp"
#include "control/CompilationThread.hpp"
#include "control/MethodToBeCompiled.hpp"
#include "env/J2IThunk.hpp"
#include "env/j9fieldsInfo.h"
#include "env/j9method.h"
#include "env/ut_j9jit.h"
#include "optimizer/J9EstimateCodeSize.hpp"
#include "runtime/J9VMAccess.hpp"
#include "runtime/IProfiler.hpp"
#include "runtime/HWProfiler.hpp"
#include "runtime/J9Profiler.hpp"
#include "env/J9JitMemory.hpp"
#include "infra/Bit.hpp"               //for trailingZeroes
#include "VMHelpers.hpp"
#include "env/JSR292Methods.h"

#ifdef LINUX
#include <signal.h>
#endif

#ifdef J9_CODE_CACHE_RECLAMATION
#include "runtime/CodeCacheReclamation.h"
#endif

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#ifndef J9_FINDKNOWNCLASS_FLAG_EXISTING_ONLY
#define J9_FINDKNOWNCLASS_FLAG_EXISTING_ONLY 2
#endif

#define PSEUDO_RANDOM_NUMBER_PREFIX "#num"
#define PSEUDO_RANDOM_STRING_PREFIX "#str"
#define PSEUDO_RANDOM_STRING_PREFIX_LEN 4
#define PSEUDO_RANDOM_SUFFIX '#'


#define REFERENCEFIELD "referent"
#define REFERENCEFIELDLEN 8
#define REFERENCERETURNTYPE "Ljava/lang/Object;"
#define REFERENCERETURNTYPELEN 18

#if SOLARIS || AIXPPC || LINUX
#include <strings.h>
#endif

#define notImplemented(A) TR_ASSERT(0, "TR_FrontEnd::%s is undefined", (A) )

#define FIELD_OFFSET_NOT_FOUND -1
struct TR_MethodToBeCompiled;

enum { IS_VOLATILE=1 };

extern "C" J9Method * getNewInstancePrototype(J9VMThread *context);


// psuedo-call to asm function
extern "C" void _getSTFLEBits(int numDoubleWords, uint64_t * bits);  /* 390 asm stub */
extern "C" bool _isPSWInProblemState();  /* 390 asm stub */

// Returns -1 if given vmThread is not a compilation thread
int32_t
TR_J9VMBase::getCompThreadIDForVMThread(void *vmThread)
   {
   int32_t id = -1; // unknown
   if (vmThread)
      {
      if (_vmThread == (J9VMThread*)vmThread)
         {
         if (_vmThreadIsCompilationThread == TR_yes)
            {
            TR_ASSERT(_compInfoPT, "if this is a verified fe for a comp thread, _compInfoPT must exist");
            id = _compInfoPT->getCompThreadId();
            }
         else if (_vmThreadIsCompilationThread == TR_maybe) // Let's find out the compilation thread and cache it
            {
            _compInfoPT = _compInfo->getCompInfoForThread((J9VMThread*)vmThread);
            id = _compInfoPT ? _compInfoPT->getCompThreadId() : -1;
            }
         }
      else // Thread given as parameter is unrelated to this frontEnd
         {
         TR::CompilationInfoPerThread * cipt = _compInfo->getCompInfoForThread((J9VMThread*)vmThread);
         id = (cipt ? cipt->getCompThreadId() : -1);
         }
      }
   return id;
   }


bool
TR_J9VM::stackWalkerMaySkipFrames(TR_OpaqueMethodBlock *method, TR_OpaqueClassBlock *methodClass)
   {
   if(!method)
      return false;

   TR::VMAccessCriticalSection stackWalkerMaySkipFrames(this);

         // Maybe I should be using isSameMethod ?
   if (vmThread()->javaVM->jlrMethodInvoke == NULL || ((J9Method *)method) == vmThread()->javaVM->jlrMethodInvoke )
      {
      return true;
      }

   if(!methodClass)
      {
      return false;
      }

#if defined(J9VM_OPT_SIDECAR)
   if ( ( vmThread()->javaVM->srMethodAccessor != NULL) &&
          TR_J9VM::isInstanceOf(methodClass, (TR_OpaqueClassBlock*) J9VM_J9CLASS_FROM_JCLASS(vmThread(), vmThread()->javaVM->srMethodAccessor), false) )
      {
      return true;
      }

   if ( ( vmThread()->javaVM->srConstructorAccessor != NULL) &&
          TR_J9VM::isInstanceOf(methodClass, (TR_OpaqueClassBlock*) J9VM_J9CLASS_FROM_JCLASS(vmThread(), vmThread()->javaVM->srConstructorAccessor), false) )
      {
      return true;
      }
#endif // J9VM_OPT_SIDECAR

   return false;
   }

bool
TR_J9VMBase::isMethodBreakpointed(TR_OpaqueMethodBlock *method)
   {
   return jitIsMethodBreakpointed(vmThread(), (J9Method *)method);
   }

bool
TR_J9VMBase::instanceOfOrCheckCast(J9Class *instanceClass, J9Class* castClass)
   {
   return ::instanceOfOrCheckCast(instanceClass, castClass);
   }

bool
TR_J9VMBase::instanceOfOrCheckCastNoCacheUpdate(J9Class *instanceClass, J9Class* castClass)
   {
   return ::instanceOfOrCheckCastNoCacheUpdate(instanceClass, castClass);
   }

// Points to address: what if vmThread is not the compilation thread.
// What if the compilation thread does not have the classUnloadMonitor.
// What if jitConfig does not exist.
// What if we already have the compilationShouldBeInterrupted flag set.
// How do we set the error code when we throw an exception

// IMPORTANT: acquireVMAccessIfNeeded could throw an exception,
// hence it is important to call this within a try block.
bool acquireVMaccessIfNeeded(J9VMThread *vmThread, TR_YesNoMaybe isCompThread)
      {
      bool haveAcquiredVMAccess = false;
      if (TR::Options::getCmdLineOptions() == 0 || // if options haven't been created yet, there is no danger
          TR::Options::getCmdLineOptions()->getOption(TR_DisableNoVMAccess))
          return false; // don't need to acquire VM access

      // we need to test if the thread has VM access
      TR_ASSERT(vmThread, "vmThread must be not null");

      // We need to acquire VMaccess only for the compilation thread
      if (isCompThread == TR_no)
         {
         TR_ASSERT((vmThread->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS), "Must have vm access if this is not a compilation thread");
         return false;
         }

      // scan all compilation threads
      J9JITConfig        *jitConfig = vmThread->javaVM->jitConfig;
      TR::CompilationInfo *compInfo  = TR::CompilationInfo::get(jitConfig);
      TR::CompilationInfoPerThread *compInfoPT = compInfo->getCompInfoForThread(vmThread);

      // We need to acquire VMaccess only for the compilation thread
      if (isCompThread == TR_maybe)
         {
         if (!compInfoPT)
            {
            TR_ASSERT((vmThread->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS), "Must have vm access if this is not a compilation thread");
            return false;
            }
         }

      // At this point we know we deal with a compilation thread
      TR_ASSERT(compInfoPT, "A compilation thread must have an associated compilationInfo");

      if (!(vmThread->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS)) // I don't already have VM access
         {
         if (0 == vmThread->javaVM->internalVMFunctions->internalTryAcquireVMAccessWithMask(vmThread, J9_PUBLIC_FLAGS_HALT_THREAD_ANY_NO_JAVA_SUSPEND))
            {
            haveAcquiredVMAccess = true;
            }
         else // the GC is having exclusive VM access
            {
            // compilationShouldBeInterrupted flag is reset by the compilation
            // thread when a new compilation starts.

            // must test if we have the class unload monitor
            //vmThread->osThread == classUnloadMonitor->owner()

            bool hadClassUnloadMonitor = false;
#if !defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
            if (TR::Options::getCmdLineOptions()->getOption(TR_EnableHCR) || TR::Options::getCmdLineOptions()->getOption(TR_FullSpeedDebug))
#endif
               hadClassUnloadMonitor = TR::MonitorTable::get()->readReleaseClassUnloadMonitor(compInfoPT->getCompThreadId()) >= 0;
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
            // We must have had classUnloadMonitor by the way we architected the application
            TR_ASSERT(hadClassUnloadMonitor, "Comp thread must hold classUnloadMonitor when compiling without VMaccess");
#endif

            //--- GC CAN INTERVENE HERE ---

            // fprintf(stderr, "Have released class unload monitor temporarily\n"); fflush(stderr);

            // At this point we must not hold any JIT monitors that can also be accessed by the GC
            // As we don't know how the GC code will be modified in the future we will
            // scan the entire list of known monitors
#if defined(DEBUG) || defined(PROD_WITH_ASSUMES)
            TR::Monitor * heldMonitor = TR::MonitorTable::get()->monitorHeldByCurrentThread();
            TR_ASSERT(!heldMonitor, "Current thread must not hold TR::Monitor %p called %s when trying to acquire VM access\n",
                    heldMonitor, TR_J9VMBase::get(jitConfig, NULL)->getJ9MonitorName((J9ThreadMonitor*)heldMonitor->getVMMonitor()));
#endif // #if defined(DEBUG) || defined(PROD_WITH_ASSUMES)

            TR_ASSERT_FATAL(!compInfo->getCompilationMonitor()->owned_by_self(),
               "Current VM thread [%p] holds the comp monitor [%p] while attempting to acquire VM access", vmThread, compInfo->getCompilationMonitor());

            TR::Compilation *comp = compInfoPT->getCompilation();
            if ((comp && comp->getOptions()->realTimeGC()) ||
                 TR::Options::getCmdLineOptions()->realTimeGC())
               compInfoPT->waitForGCCycleMonitor(false); // used only for real-time

            acquireVMAccessNoSuspend(vmThread);   // blocking. Will wait for the entire GC
            if (hadClassUnloadMonitor)
               {
               TR::MonitorTable::get()->readAcquireClassUnloadMonitor(compInfoPT->getCompThreadId());
               //fprintf(stderr, "Have acquired class unload monitor again\n"); fflush(stderr);
               }

            // Now we can check if the GC has done some unloading or redefinition happened
            if (compInfoPT->compilationShouldBeInterrupted())
               {
               // bail out
               // fprintf(stderr, "Released classUnloadMonitor and will throw an exception because GC unloaded classes\n"); fflush(stderr);
               //TR::MonitorTable::get()->readReleaseClassUnloadMonitor(0); // Main code should do it.
               // releaseVMAccess(vmThread);

               if (comp)
                  {
                  comp->failCompilation<TR::CompilationInterrupted>("Compilation interrupted by GC unloading classes");
                  }
               else // I am not sure we are in a compilation; better release the monitor
                  {
                  if (hadClassUnloadMonitor)
                     TR::MonitorTable::get()->readReleaseClassUnloadMonitor(compInfoPT->getCompThreadId()); // Main code should do it.
                  throw TR::CompilationInterrupted();
                  }
               }
            else // GC did not do any unloading
               {
               haveAcquiredVMAccess = true;
               }
            }
         }

      /*
       * At shutdown time the compilation thread executes Java code and it may receive a sample (see D174900)
       * Only abort the compilation if we're explicitly prepared to handle it.
       */
      if (compInfoPT->compilationShouldBeInterrupted())
         {
         TR_ASSERT(compInfoPT->compilationShouldBeInterrupted() != GC_COMP_INTERRUPT, "GC should not have cut in _compInfoPT=%p\n", compInfoPT);
         // in prod builds take some corrective action
         throw J9::JITShutdown();
         }

      return haveAcquiredVMAccess;
      }

void releaseVMaccessIfNeeded(J9VMThread *vmThread, bool haveAcquiredVMAccess)
   {
   if (haveAcquiredVMAccess)
      {
      TR_ASSERT((vmThread->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS), "Must have VM access"); // I don't already have VM access
      releaseVMAccess(vmThread);
      }
   }


bool
TR_J9VMBase::isAnyMethodTracingEnabled(TR_OpaqueMethodBlock *method)
   {
   return isMethodTracingEnabled(method);
   }

int32_t * TR_J9VMBase::staticStringEnableCompressionFieldAddr = NULL;

bool
TR_J9VMBase::releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(TR::Compilation *comp, bool *hadClassUnloadMonitor)
   {
  // When allocating a new code cache we must not hold any monitor because
   // of deadlock possibilities
   // This must be executed on the compilation thread so options must exist at this point
   TR_ASSERT(comp, "Compilation object must always be given as parameter\n");
   *hadClassUnloadMonitor = false;
   bool hadVMAccess = true;
   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableNoVMAccess))
      {
      TR_ASSERT(_vmThreadIsCompilationThread != TR_no, "releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded should only be called for compilation threads\n");
      // We need to acquire VMaccess only for the compilation thread
      TR_ASSERT(vmThread()==_jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM),
        "fe for thread %p is used on thread %p", vmThread(),
        _jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM));
      TR_ASSERT(_compInfoPT, "_compInfoPT must exist here\n");
      TR_ASSERT(_compInfo->getCompInfoForThread(vmThread()) == _compInfoPT, "assertion failure");

      if (_vmThreadIsCompilationThread == TR_maybe)
         {
         _vmThreadIsCompilationThread = TR_yes;
         }

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
      *hadClassUnloadMonitor = TR::MonitorTable::get()->readReleaseClassUnloadMonitor(_compInfoPT->getCompThreadId()) >= 0;
      TR_ASSERT(*hadClassUnloadMonitor, "releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded without classUnloadMonitor\n");
#else
      // We need to use classUnloadMonitor to stop the Compilation Thread when a class is redefined
      if (TR::Options::getCmdLineOptions()->getOption(TR_EnableHCR) || TR::Options::getCmdLineOptions()->getOption(TR_FullSpeedDebug))
         {
         *hadClassUnloadMonitor = TR::MonitorTable::get()->readReleaseClassUnloadMonitor(_compInfoPT->getCompThreadId()) >= 0;
         TR_ASSERT(*hadClassUnloadMonitor, "releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded without classUnloadMonitor\n");
         }
#endif

      if (!(vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS)) // I don't already have VM access
         {
         hadVMAccess = false;

#if defined(DEBUG) || defined(PROD_WITH_ASSUMES)
         TR::Monitor * heldMonitor = TR::MonitorTable::get()->monitorHeldByCurrentThread();
         TR_ASSERT(!heldMonitor, "Current thread must not hold TR::Monitor %p called %s when trying to acquire VM access\n",
                  heldMonitor, getJ9MonitorName((J9ThreadMonitor*)heldMonitor->getVMMonitor()));
#endif // #if defined(DEBUG) || defined(PROD_WITH_ASSUMES)

         //--- GC can happen here ---

         acquireVMAccessNoSuspend(vmThread());
         // If the GC unloaded some classes, or HCR happened we must abort this compilation
         if (_compInfoPT->compilationShouldBeInterrupted())
            {
            //releaseVMAccess(vmThread()); // release VM access before blocking on the next acquire operation
            //if (*hadClassUnloadMonitor)
            //   TR::MonitorTable::get()->readAcquireClassUnloadMonitor(_compInfoPT->getCompThreadId());
            comp->failCompilation<TR::CompilationInterrupted>("Compilation interrupted");
            // After the exception throw we will release the classUnloadMonitor and reacquire the VM access
            }
         }
      else
         {
         hadVMAccess = true;
         }
      }
      return hadVMAccess;
   }


void
TR_J9VMBase::acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(TR::Compilation *comp, bool hadVMAccess, bool hadClassUnloadMonitor)
   {
   if (TR::Options::getCmdLineOptions() && // if options haven't been created yet, there is no danger
       !TR::Options::getCmdLineOptions()->getOption(TR_DisableNoVMAccess))
      {
      TR_ASSERT(vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS, "vmThread must have vmAccess at this point");
      if (_compInfoPT->compilationShouldBeInterrupted())
         {
         comp->failCompilation<TR::CompilationInterrupted>("Compilation interrupted");
         }
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
      if (hadClassUnloadMonitor)
         TR::MonitorTable::get()->readAcquireClassUnloadMonitor(_compInfoPT->getCompThreadId());
#else
      // We need to use classUnloadMonitor to stop the Compilation Thread when a class is redefined
      if ((TR::Options::getCmdLineOptions()->getOption(TR_EnableHCR) || TR::Options::getCmdLineOptions()->getOption(TR_FullSpeedDebug)) && hadClassUnloadMonitor)
         TR::MonitorTable::get()->readAcquireClassUnloadMonitor(_compInfoPT->getCompThreadId());
#endif // J9VM_GC_DYNAMIC_CLASS_UNLOADING
      if (!hadVMAccess)
         releaseVMAccess(vmThread());
      }
   }

bool
TR_J9SharedCacheVM::shouldDelayAotLoad()
   {
   return isAOT_DEPRECATED_DO_NOT_USE();
   }

J9Class *
TR_J9VMBase::matchRAMclassFromROMclass(J9ROMClass * clazz, TR::Compilation * comp)
   {
   TR::VMAccessCriticalSection matchRAMclassFromROMclass(this);
   J9UTF8 *utf8 = J9ROMCLASS_CLASSNAME(clazz);
   J9Class *ramClass = jitGetClassInClassloaderFromUTF8(vmThread(), ((TR_ResolvedJ9Method *)comp->getCurrentMethod())->getClassLoader(),
      (char *) J9UTF8_DATA(utf8), J9UTF8_LENGTH(utf8));
   if (!ramClass)
      {
      ramClass = jitGetClassInClassloaderFromUTF8(vmThread(), (J9ClassLoader *) vmThread()->javaVM->systemClassLoader,
         (char *) J9UTF8_DATA(utf8), J9UTF8_LENGTH(utf8));
      }
   return ramClass;
   }

J9VMThread *
TR_J9VMBase::getCurrentVMThread()
   {
   return _vmThread ? _vmThread : _jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM);
   }

bool
TR_J9VMBase::createGlobalFrontEnd(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo)
   {
   TR_ASSERT(!jitConfig->compilationInfo, "Global front end already exists");
   TR_J9VM * vmWithoutThreadInfo = 0;

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
   TR_ASSERT(!jitConfig->aotCompilationInfo, "Global AOT front end already exists");
   TR_J9SharedCacheVM * aotVMWithoutThreadInfo = 0;
#endif

   try
      {
      vmWithoutThreadInfo = new (PERSISTENT_NEW) TR_J9VM(jitConfig, compInfo, NULL);
      if (!vmWithoutThreadInfo) throw std::bad_alloc();

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
      try
         {
         aotVMWithoutThreadInfo = new (PERSISTENT_NEW) TR_J9SharedCacheVM(jitConfig, compInfo, NULL);
         if (!aotVMWithoutThreadInfo) throw std::bad_alloc();
         }
      catch (...)
         {
         vmWithoutThreadInfo->~TR_J9VM();
         jitPersistentFree(vmWithoutThreadInfo);
         throw;
         }
#endif

      }
   catch (...)
      {
      return false;
      }

   jitConfig->compilationInfo = vmWithoutThreadInfo;

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
   jitConfig->aotCompilationInfo = aotVMWithoutThreadInfo;
#endif

   return true;
   }

TR_J9VMBase *
TR_J9VMBase::get(J9JITConfig * jitConfig, J9VMThread * vmThread, VM_TYPE vmType)
   {

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) && !defined (J9VM_OPT_SHARED_CLASSES)
#error "Unsupported AOT configuration"
#endif

   TR_ASSERT(vmThread || vmType==DEFAULT_VM, "Specific VM type ==> must supply vmThread");
   TR_J9VMBase * vmWithoutThreadInfo = static_cast<TR_J9VMBase *>(jitConfig->compilationInfo);
#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
   TR_J9VMBase * aotVMWithoutThreadInfo = static_cast<TR_J9VMBase *>(jitConfig->aotCompilationInfo);
#endif

   if (vmThread)
      {
      // Check if this thread has cached the frontend inside

#if defined(J9VM_OPT_JITSERVER)
      if (vmType==J9_SERVER_VM || vmType==J9_SHARED_CACHE_SERVER_VM)
         {
         TR_ASSERT(vmWithoutThreadInfo->_compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER, "J9_SERVER_VM and J9_SHARED_CACHE_SERVER_VM should only be instantiated in JITServer::SERVER mode");
         TR::CompilationInfoPerThread *compInfoPT = NULL;
         // Get the compInfoPT from the cached J9_VM with thread info
         // or search using the compInfo from the J9_VM without thread info
         if (vmThread->jitVMwithThreadInfo)
            {
            TR_J9VMBase * vmWithThreadInfo = static_cast<TR_J9VMBase *>(vmThread->jitVMwithThreadInfo);
            compInfoPT = vmWithThreadInfo->_compInfoPT;
            }
         if (!compInfoPT && vmWithoutThreadInfo->_compInfo)
            {
            compInfoPT = vmWithoutThreadInfo->_compInfo->getCompInfoForThread(vmThread);
            }
         TR_ASSERT(compInfoPT, "Tried to create a TR_J9ServerVM without compInfoPT");

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
         if (vmType==J9_SHARED_CACHE_SERVER_VM)
            {
            TR_J9SharedCacheServerVM *sharedCacheServerVM = compInfoPT->getSharedCacheServerVM();
            if (!sharedCacheServerVM)
               {
               PORT_ACCESS_FROM_JITCONFIG(jitConfig);
               void * alloc = j9mem_allocate_memory(sizeof(TR_J9SharedCacheServerVM), J9MEM_CATEGORY_JIT);
               if (alloc)
                  sharedCacheServerVM = new (alloc) TR_J9SharedCacheServerVM(jitConfig, vmWithoutThreadInfo->_compInfo, vmThread);
               if (sharedCacheServerVM)
                  {
                  sharedCacheServerVM->_vmThreadIsCompilationThread = TR_yes;
                  sharedCacheServerVM->_compInfoPT = compInfoPT;
                  compInfoPT->setSharedCacheServerVM(sharedCacheServerVM);
                  }
               else
                  throw std::bad_alloc();
               }
            return sharedCacheServerVM;
            }
         else
#endif /* defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) */
            {
#if !defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
            TR_ASSERT((vmType != J9_SHARED_CACHE_SERVER_VM), "vmType cannot be J9_SHARED_CACHE_SERVER_VM when J9VM_INTERP_AOT_COMPILE_SUPPORT is disabled");
#endif /* !defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) */

            TR_J9ServerVM *serverVM = compInfoPT->getServerVM();
            if (!serverVM)
               {
               PORT_ACCESS_FROM_JITCONFIG(jitConfig);
               void * alloc = j9mem_allocate_memory(sizeof(TR_J9ServerVM), J9MEM_CATEGORY_JIT);
               if (alloc)
                  serverVM = new (alloc) TR_J9ServerVM(jitConfig, vmWithoutThreadInfo->_compInfo, vmThread);
               if (serverVM)
                  {
                  serverVM->_vmThreadIsCompilationThread = TR_yes;
                  serverVM->_compInfoPT = compInfoPT;
                  compInfoPT->setServerVM(serverVM);
                  }
               else
                  throw std::bad_alloc();
               }
            return serverVM;
            }
         }
#endif /* defined(J9VM_OPT_JITSERVER) */
#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)
      if (vmType==AOT_VM)
         {
         TR_J9VMBase * aotVMWithThreadInfo = static_cast<TR_J9VMBase *>(vmThread->aotVMwithThreadInfo);

         if (!aotVMWithThreadInfo)
            {
            PORT_ACCESS_FROM_JITCONFIG(jitConfig);
            void * alloc = j9mem_allocate_memory(sizeof(TR_J9SharedCacheVM), J9MEM_CATEGORY_JIT);
            if (alloc)
               {
               aotVMWithThreadInfo = new (alloc) TR_J9SharedCacheVM(jitConfig, vmWithoutThreadInfo->_compInfo, vmThread);
               }
            if (aotVMWithThreadInfo)
               {
               vmThread->aotVMwithThreadInfo = aotVMWithThreadInfo;
               if (vmWithoutThreadInfo->_compInfo)
                  {
                  TR::CompilationInfoPerThread *compInfoPT = vmWithoutThreadInfo->_compInfo->getCompInfoForThread(vmThread);
                  aotVMWithThreadInfo->_vmThreadIsCompilationThread = (compInfoPT ? TR_yes : TR_no);
                  aotVMWithThreadInfo->_compInfoPT = compInfoPT;
                  }
               }
            else
               aotVMWithThreadInfo = aotVMWithoutThreadInfo; // may be incorrect because they are of different type
            }
         return aotVMWithThreadInfo;
         }
      else // We need to create a J9_VM
#endif /* defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) */
         {
         TR_J9VMBase * vmWithThreadInfo = (TR_J9VMBase *)vmThread->jitVMwithThreadInfo;
         TR_ASSERT(vmType==DEFAULT_VM || vmType==J9_VM, "assertion failure");
         if (!vmWithThreadInfo) // This thread has not cached a frontend
            {
            PORT_ACCESS_FROM_JITCONFIG(jitConfig);
            void * alloc = j9mem_allocate_memory(sizeof(TR_J9VM), J9MEM_CATEGORY_JIT);
            if (alloc)
               {
               vmWithThreadInfo =  new (alloc) TR_J9VM(jitConfig, vmWithoutThreadInfo->_compInfo, vmThread); // allocate the frontend
               }
            if (vmWithThreadInfo)
               {
               vmThread->jitVMwithThreadInfo = vmWithThreadInfo; // cache it
               // Cache ths compilation thread as well
               if (vmWithoutThreadInfo->_compInfo)
                  {
                  TR::CompilationInfoPerThread *compInfoPT = vmWithoutThreadInfo->_compInfo->getCompInfoForThread(vmThread);
                  vmWithThreadInfo->_vmThreadIsCompilationThread = (compInfoPT ? TR_yes : TR_no);
                  vmWithThreadInfo->_compInfoPT = compInfoPT;
                  }
               }
            else
               vmWithThreadInfo = vmWithoutThreadInfo;
            }
         return vmWithThreadInfo;
         }
      }
   return vmWithoutThreadInfo;
   }

TR_J9VMBase::TR_J9VMBase(
   J9JITConfig * jitConfig,
   TR::CompilationInfo * compInfo,
   J9VMThread * vmThread
   )
   : TR_FrontEnd(),
     _vmThread(vmThread),
     _portLibrary(jitConfig->javaVM->portLibrary),
     _jitConfig(jitConfig),
     _vmFunctionTable(jitConfig->javaVM->internalVMFunctions),
     _compInfo(compInfo),
     _iProfiler(0),
     _hwProfilerShouldNotProcessBuffers(TR::Options::_hwProfilerRIBufferProcessingFrequency),
     _bufferStart(NULL),
     _vmThreadIsCompilationThread(TR_maybe),
     _compInfoPT(NULL),
     _shouldSleep(false)
   {
   for (int32_t i = 0; i < UT_MODULE_INFO.count; ++i)
      if (UT_ACTIVE[i])
         {
         setTraceIsEnabled(true);
         break;
         }

   if (TR::Options::getCmdLineOptions() && TR::Options::getCmdLineOptions()->getOption(TR_FullSpeedDebug))
      setFSDIsEnabled(true);

   _sharedCache = NULL;
   if (TR::Options::sharedClassCache()
#if defined(J9VM_OPT_JITSERVER)
      || (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
#endif /* defined(J9VM_OPT_JITSERVER) */
      )
      // shared classes and AOT must be enabled, or we should be on the JITServer with remote AOT enabled
      {
#if defined(J9VM_OPT_JITSERVER)
      if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
         {
         _sharedCache = new (compInfo->persistentMemory()) TR_J9JITServerSharedCache(this);
         }
      else
#endif /* defined(J9VM_OPT_JITSERVER) */
         {
         _sharedCache = new (PERSISTENT_NEW) TR_J9SharedCache(this);
         }
      if (!_sharedCache)
         {
         TR::Options::getAOTCmdLineOptions()->setOption(TR_NoStoreAOT);
         TR::Options::getAOTCmdLineOptions()->setOption(TR_NoLoadAOT);
         static_cast<TR_JitPrivateConfig *>(jitConfig->privateConfig)->aotValidHeader = TR_no;
         TR_J9SharedCache::setSharedCacheDisabledReason(TR_J9SharedCache::J9_SHARED_CACHE_FAILED_TO_ALLOCATE);
         }
      else
         {
         TR_PersistentMemory * persistentMemory = (TR_PersistentMemory *)(jitConfig->scratchSegment);
         TR_PersistentClassLoaderTable *loaderTable = persistentMemory->getPersistentInfo()->getPersistentClassLoaderTable();
         _sharedCache->setPersistentClassLoaderTable(loaderTable);
         }
      }
   }

void
TR_J9VMBase::freeSharedCache()
   {
   if (_sharedCache)        // shared classes and AOT must be enabled
      {
#if defined(J9VM_OPT_JITSERVER)
      if (_compInfo && (_compInfo->getPersistentInfo()->getRemoteCompilationMode() != JITServer::SERVER))
#endif /* defined(J9VM_OPT_JITSERVER) */
         {
         TR_ASSERT(TR::Options::sharedClassCache(), "Found shared cache with option disabled");
         }
      jitPersistentFree(_sharedCache);
      _sharedCache = NULL;
      }
   }

TR::CompilationInfo * getCompilationInfo(J9JITConfig * jitConfig)
   {
   return TR::CompilationInfo::get(jitConfig);
   }

J9VMThread *
TR_J9VMBase::vmThread()
   {
   TR_ASSERT(_vmThread, "TR_J9VMBase::vmThread() TR_J9VMBase was created without thread information");
   return _vmThread;
   }

void
TR_J9VMBase::acquireCompilationLock()
   {
   if (_compInfo)
      _compInfo->acquireCompilationLock();
   }

void
TR_J9VMBase::releaseCompilationLock()
   {
   if (_compInfo)
      _compInfo->releaseCompilationLock();
   }

void
TR_J9VMBase::acquireLogMonitor()
   {
   if (_compInfo)
      _compInfo->acquireLogMonitor();
   }

void
TR_J9VMBase::releaseLogMonitor()
   {
   if (_compInfo)
      _compInfo->releaseLogMonitor();
   }

bool
TR_J9VMBase::isAsyncCompilation()
   {
   return _compInfo ? _compInfo->asynchronousCompilation() : false;
   }


uintptr_t
TR_J9VMBase::getProcessID()
   {
   PORT_ACCESS_FROM_ENV(_jitConfig->javaVM);
   TR::VMAccessCriticalSection getProcessID(this);
   uintptr_t result = j9sysinfo_get_pid();
   return result;
   }

// static method
char *
TR_J9VMBase::getJ9FormattedName(
      J9JITConfig *jitConfig,
      J9PortLibrary *portLibrary,
      char *buf,
      size_t bufLength,
      char *name,
      char *format,
      bool suffix)
   {
   PORT_ACCESS_FROM_ENV(jitConfig->javaVM);
   J9VMThread *vmThread = jitConfig->javaVM->internalVMFunctions->currentVMThread(jitConfig->javaVM);
   I_64 curTime = j9time_current_time_millis();
   J9StringTokens *tokens = j9str_create_tokens(curTime);
   if (tokens == NULL)
      {
      return NULL;
      }

   char tmp[1025];
   size_t nameLength = strlen(name);
   uintptr_t substLength = j9str_subst_tokens(tmp, sizeof(tmp), name, tokens);

   if (substLength >= std::min(sizeof(tmp), bufLength))
      {
      j9str_free_tokens(tokens);
      return NULL; // not enough room for the name or the token expansion
      }

   if (strcmp(tmp, name) != 0) // only append if there isn't a format specifier
      {
      memcpy(buf, tmp, substLength + 1); // +1 to get the null terminator
      }
   else
      {
      memcpy(buf, name, nameLength);
      char *suffixBuf = &buf[nameLength];
      if (format)
         j9str_subst_tokens(suffixBuf, bufLength - nameLength, format, tokens);
      else if (suffix)
         {
         // We have to break the string up to prevent CMVC keyword expansion
         j9str_subst_tokens(suffixBuf, bufLength - nameLength, ".%Y" "%m" "%d." "%H" "%M" "%S.%pid", tokens);
         }
      else
         {
         buf = name;
         }
      }

   j9str_free_tokens(tokens);
   return buf;
   }


char *
TR_J9VMBase::getFormattedName(char *buf, int32_t bufLength, char *name, char *format, bool suffix)
   {
   return getJ9FormattedName(_jitConfig, _portLibrary, buf, bufLength, name, format, suffix);
   }


void
TR_J9VMBase::invalidateCompilationRequestsForUnloadedMethods(TR_OpaqueClassBlock *clazz, bool hotCodeReplacement)
   {
   // Only called from jitHookClassUnload so we don't need to acquire VM access
   if (_compInfo)
      _compInfo->invalidateRequestsForUnloadedMethods(clazz, vmThread(), hotCodeReplacement);
   }


// -----------------------------------------------------------------------------


void
TR_J9VM::initializeHasResumableTrapHandler()
   {
   TR_ASSERT(_compInfo,"compInfo not defined");
    if(!TR::Options::getCmdLineOptions()->getOption(TR_NoResumableTrapHandler) &&
      portLibCall_sysinfo_has_resumable_trap_handler())
      {
      _compInfo->setHasResumableTrapHandler();
      }
    else
      {
      // In the case where the user specified -Xrs initializeHasResumableTrapHandler
      // will be called twice and we must unset the flag as it will have been set on
      // during the first call.
      _compInfo->setHasResumableTrapHandler(false);
      }
   }

bool
TR_J9VMBase::hasResumableTrapHandler()
   {
   return _compInfo ? _compInfo->getHasResumableTrapHandler() : false;
   }

void
TR_J9VM::initializeHasFixedFrameC_CallingConvention()
   {
   TR_ASSERT(_compInfo,"compInfo not defined");

   if(portLibCall_sysinfo_has_fixed_frame_C_calling_convention())
      {
      _compInfo->setHasFixedFrameC_CallingConvention();
      }
   }

bool
TR_J9VMBase::hasFixedFrameC_CallingConvention()
   {
   return _compInfo ? _compInfo->getHasFixedFrameC_CallingConvention() : false;
   }


bool
TR_J9VMBase::pushesOutgoingArgsAtCallSite(TR::Compilation *comp)
   {
   // TODO: What about cross-compilation?  Is there any way to know whether the
   // target should be using a variable frame even if the host is not?
#if defined(J9SW_NEEDS_JIT_2_INTERP_CALLEE_ARG_POP)
   return true;
#else
   return false;
#endif
   }

bool
TR_J9VMBase::assumeLeftMostNibbleIsZero()
   {
   return false;
   }

// -----------------------------------------------------------------------------

UDATA TR_J9VMBase::thisThreadGetStackLimitOffset()                  {return offsetof(J9VMThread, stackOverflowMark);}
UDATA TR_J9VMBase::thisThreadGetCurrentExceptionOffset()            {return offsetof(J9VMThread, currentException);}
UDATA TR_J9VMBase::thisThreadGetPublicFlagsOffset()                 {return offsetof(J9VMThread, publicFlags);}
UDATA TR_J9VMBase::thisThreadGetJavaPCOffset()                      {return offsetof(J9VMThread, pc);}
UDATA TR_J9VMBase::thisThreadGetJavaSPOffset()                      {return offsetof(J9VMThread, sp);}

UDATA TR_J9VMBase::thisThreadGetSystemSPOffset()
   {
#if defined(J9VM_JIT_FREE_SYSTEM_STACK_POINTER)
   return offsetof(J9VMThread, systemStackPointer);
#else
   notImplemented("thisThreadGetSystemSPOffset"); return 0;
#endif
   }

UDATA TR_J9VMBase::thisThreadGetJavaLiteralsOffset()                {return offsetof(J9VMThread, literals);}
UDATA TR_J9VMBase::thisThreadGetJavaFrameFlagsOffset()              {return offsetof(J9VMThread, jitStackFrameFlags);}
UDATA TR_J9VMBase::thisThreadGetMachineSPOffset()                   {return sizeof(J9VMThread);}
UDATA TR_J9VMBase::thisThreadGetCurrentThreadOffset()               {return offsetof(J9VMThread, threadObject);}
UDATA TR_J9VMBase::thisThreadGetFloatTemp1Offset()                  {return offsetof(J9VMThread, floatTemp1);}
UDATA TR_J9VMBase::thisThreadGetFloatTemp2Offset()                  {return offsetof(J9VMThread, floatTemp2);}
UDATA TR_J9VMBase::thisThreadGetTempSlotOffset()                    {return offsetof(J9VMThread, tempSlot);}
UDATA TR_J9VMBase::thisThreadGetReturnValueOffset()                 {return offsetof(J9VMThread, returnValue);}
UDATA TR_J9VMBase::getThreadDebugEventDataOffset(int32_t index) {J9VMThread *dummy=0; return offsetof(J9VMThread, debugEventData1) + (index-1)*sizeof(dummy->debugEventData1);} // index counts from 1
UDATA TR_J9VMBase::getThreadLowTenureAddressPointerOffset()         {return offsetof(J9VMThread, lowTenureAddress);}
UDATA TR_J9VMBase::getThreadHighTenureAddressPointerOffset()        {return offsetof(J9VMThread, highTenureAddress);}
UDATA TR_J9VMBase::getObjectHeaderSizeInBytes()                     {return TR::Compiler->om.objectHeaderSizeInBytes();}

UDATA TR_J9VMBase::getOffsetOfSuperclassesInClassObject()           {return offsetof(J9Class, superclasses);}
UDATA TR_J9VMBase::getOffsetOfBackfillOffsetField()                 {return offsetof(J9Class, backfillOffset);}

UDATA TR_J9VMBase::getOffsetOfContiguousArraySizeField()            {return TR::Compiler->om.offsetOfContiguousArraySizeField();}
UDATA TR_J9VMBase::getOffsetOfDiscontiguousArraySizeField()         {return TR::Compiler->om.offsetOfDiscontiguousArraySizeField();}
#if defined(TR_TARGET_64BIT)
UDATA TR_J9VMBase::getOffsetOfContiguousDataAddrField()             {return TR::Compiler->om.offsetOfContiguousDataAddrField();}
UDATA TR_J9VMBase::getOffsetOfDiscontiguousDataAddrField()          {return TR::Compiler->om.offsetOfDiscontiguousDataAddrField();}
#endif /* TR_TARGET_64BIT */
UDATA TR_J9VMBase::getJ9ObjectContiguousLength()                    {return TR::Compiler->om.offsetOfContiguousArraySizeField();}
UDATA TR_J9VMBase::getJ9ObjectDiscontiguousLength()                 {return TR::Compiler->om.offsetOfContiguousArraySizeField();}

UDATA TR_J9VMBase::getOffsetOfArrayClassRomPtrField()               {return offsetof(J9ArrayClass, romClass);}
UDATA TR_J9VMBase::getOffsetOfClassRomPtrField()                    {return offsetof(J9Class, romClass);}
UDATA TR_J9VMBase::getOffsetOfClassInitializeStatus()               {return offsetof(J9Class, initializeStatus);}
UDATA TR_J9VMBase::getOffsetOfJ9ObjectJ9Class()                     {return offsetof(J9Object, clazz);}
UDATA TR_J9VMBase::getObjectHeaderHasBeenMovedInClass()             {return OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS;}
UDATA TR_J9VMBase::getObjectHeaderHasBeenHashedInClass()            {return OBJECT_HEADER_HAS_BEEN_HASHED_IN_CLASS;}
UDATA TR_J9VMBase::getJ9ObjectFlagsMask32()                         {return (J9_REQUIRED_CLASS_ALIGNMENT - 1);}
UDATA TR_J9VMBase::getJ9ObjectFlagsMask64()                         {return (J9_REQUIRED_CLASS_ALIGNMENT - 1);}
UDATA TR_J9VMBase::getOffsetOfJ9ThreadJ9VM()                        {return offsetof(J9VMThread, javaVM);}
UDATA TR_J9VMBase::getOffsetOfJ9ROMArrayClassArrayShape()           {return offsetof(J9ROMArrayClass, arrayShape);}
UDATA TR_J9VMBase::getOffsetOfJavaVMIdentityHashData()              {return offsetof(J9JavaVM, identityHashData);}
UDATA TR_J9VMBase::getOffsetOfJ9IdentityHashData1()                 {return offsetof(J9IdentityHashData, hashData1);}
UDATA TR_J9VMBase::getOffsetOfJ9IdentityHashData2()                 {return offsetof(J9IdentityHashData, hashData2);}
UDATA TR_J9VMBase::getOffsetOfJ9IdentityHashData3()                 {return offsetof(J9IdentityHashData, hashData3);}
UDATA TR_J9VMBase::getOffsetOfJ9IdentityHashDataHashSaltTable()     {return offsetof(J9IdentityHashData, hashSaltTable);}
UDATA TR_J9VMBase::getJ9IdentityHashSaltPolicyStandard()            {return J9_IDENTITY_HASH_SALT_POLICY_STANDARD;}
UDATA TR_J9VMBase::getJ9IdentityHashSaltPolicyRegion()              {return J9_IDENTITY_HASH_SALT_POLICY_REGION;}
UDATA TR_J9VMBase::getJ9IdentityHashSaltPolicyNone()                {return J9_IDENTITY_HASH_SALT_POLICY_NONE;}
UDATA TR_J9VMBase::getJ9JavaClassRamShapeShift()                    {return J9AccClassRAMShapeShift; }
UDATA TR_J9VMBase::getObjectHeaderShapeMask()                       {return OBJECT_HEADER_SHAPE_MASK; }

UDATA TR_J9VMBase::getIdentityHashSaltPolicy()
   {
   TR::VMAccessCriticalSection getIdentityHashSaltPolicy(this);
   J9VMThread * vmthread = vmThread();
   J9IdentityHashData *hashData = vmthread->javaVM->identityHashData;
   jint saltPolicy = hashData->hashSaltPolicy;
   return saltPolicy;
   }

UDATA TR_J9VMBase::getOffsetOfJLThreadJ9Thread()
   {
   TR::VMAccessCriticalSection getOffsetOfJLThreadJ9Thread(this);
   J9VMThread * vmthread = vmThread();
   jint offset;
   offset = (jint) J9VMJAVALANGTHREAD_THREADREF_OFFSET(vmthread);
   return offset;
   }

UDATA TR_J9VMBase::getOSRFrameHeaderSizeInBytes()                   {return sizeof(J9OSRFrame);}
UDATA TR_J9VMBase::getOSRFrameSizeInBytes(TR_OpaqueMethodBlock* method)  {return osrFrameSize((J9Method*) method);}

bool TR_J9VMBase::ensureOSRBufferSize(TR::Compilation *comp, uintptr_t osrFrameSizeInBytes, uintptr_t osrScratchBufferSizeInBytes, uintptr_t osrStackFrameSizeInBytes)
   {
   J9JavaVM *vm = _jitConfig->javaVM;
   return ::ensureOSRBufferSize(vm, osrFrameSizeInBytes, osrScratchBufferSizeInBytes, osrStackFrameSizeInBytes);
   }


UDATA TR_J9VMBase::thisThreadGetDLTBlockOffset()
   {
#if defined(J9VM_JIT_DYNAMIC_LOOP_TRANSFER)
   return offsetof(J9VMThread, dltBlock);
#else
   return 0;
#endif
   }

UDATA TR_J9VMBase::getDLTBufferOffsetInBlock()
   {
#if defined(J9VM_JIT_DYNAMIC_LOOP_TRANSFER)
   return offsetof(J9DLTInformationBlock, temps);
#else
   return 0;
#endif
   }

bool
TR_J9VMBase::compiledAsDLTBefore(TR_ResolvedMethod * method)
   {
#if defined(J9VM_JIT_DYNAMIC_LOOP_TRANSFER)
   return _compInfo->searchForDLTRecord(((TR_ResolvedJ9Method*)method)->ramMethod(), -1) != NULL;
#else
   return 0;
#endif
   }


TR_OpaqueClassBlock *
TR_J9VMBase::getObjectClass(uintptr_t objectPointer)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getObjectClass");
   J9Class *j9class = J9OBJECT_CLAZZ(vmThread(), objectPointer);
   return convertClassPtrToClassOffset(j9class);
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getObjectClassAt(uintptr_t objectAddress)
   {
   TR::VMAccessCriticalSection getObjectClassAt(this);
   return getObjectClass(getStaticReferenceFieldAtAddress(objectAddress));
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getObjectClassFromKnownObjectIndex(TR::Compilation *comp, TR::KnownObjectTable::Index idx)
   {
   TR::VMAccessCriticalSection getObjectClassFromKnownObjectIndex(comp, TR::VMAccessCriticalSection::tryToAcquireVMAccess);
   TR_OpaqueClassBlock *clazz = NULL;
   if (getObjectClassFromKnownObjectIndex.hasVMAccess())
      clazz = getObjectClass(comp->getKnownObjectTable()->getPointer(idx));
   return clazz;
   }

uintptr_t
TR_J9VMBase::getStaticReferenceFieldAtAddress(uintptr_t fieldAddress)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getStaticReferenceFieldAtAddress");
   return (uintptr_t)J9STATIC_OBJECT_LOAD(vmThread(), NULL, fieldAddress);
   }

uintptr_t
TR_J9VMBase::getReferenceFieldAtAddress(uintptr_t fieldAddress)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getReferenceFieldAtAddress");
   TR::Compilation* comp = TR::comp();
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
   // Emit read barrier
   if (TR::Compiler->om.readBarrierType() != gc_modron_readbar_none)
      vmThread()->javaVM->javaVM->memoryManagerFunctions->J9ReadBarrier(vmThread(), (fj9object_t *)fieldAddress);
#endif

   if (TR::Compiler->om.compressObjectReferences())
      {
      uintptr_t compressedResult = *(uint32_t*)fieldAddress;
      return (compressedResult << TR::Compiler->om.compressedReferenceShift());
      }
   return *(uintptr_t*)fieldAddress;
   }

uintptr_t
TR_J9VMBase::getReferenceFieldAt(uintptr_t objectPointer, uintptr_t fieldOffset)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getReferenceFieldAt");
   return (uintptr_t)J9OBJECT_OBJECT_LOAD(vmThread(), objectPointer, TR::Compiler->om.objectHeaderSizeInBytes() + fieldOffset);
   }

uintptr_t
TR_J9VMBase::getVolatileReferenceFieldAt(uintptr_t objectPointer, uintptr_t fieldOffset)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getVolatileReferenceFieldAt");
   return (uintptr_t)vmThread()->javaVM->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadObject(vmThread(),
      (J9Object*)objectPointer, TR::Compiler->om.objectHeaderSizeInBytes() + fieldOffset, IS_VOLATILE);
   }

int32_t
TR_J9VMBase::getInt32FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getInt32Field");
   return *(int32_t*)(objectPointer + getObjectHeaderSizeInBytes() + fieldOffset);
   }

int64_t
TR_J9VMBase::getInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getInt64Field");
   return *(int64_t*)(objectPointer + getObjectHeaderSizeInBytes() + fieldOffset);
   }

void
TR_J9VMBase::setInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset, int64_t newValue)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in setInt64Field");
   *(int64_t*)(objectPointer + getObjectHeaderSizeInBytes() + fieldOffset) = newValue;
   }

bool
TR_J9VMBase::compareAndSwapInt64FieldAt(uintptr_t objectPointer, uintptr_t fieldOffset, int64_t oldValue, int64_t newValue)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in compareAndSwapInt64FieldAt");
   UDATA success = vmThread()->javaVM->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectCompareAndSwapLong(vmThread(),
      (J9Object*)objectPointer, fieldOffset + getObjectHeaderSizeInBytes(), oldValue, newValue);
   return success != 0;
   }

intptr_t
TR_J9VMBase::getArrayLengthInElements(uintptr_t objectPointer)
   {
   TR_ASSERT(haveAccess(), "Must haveAccess in getArrayLengthInElements");
   int32_t result = *(int32_t*)(objectPointer + getOffsetOfContiguousArraySizeField());
   if (TR::Compiler->om.useHybridArraylets() && (result == 0))
      result = *(int32_t*)(objectPointer + getOffsetOfDiscontiguousArraySizeField());
   return (intptr_t)result;
   }

int32_t
TR_J9VMBase::getInt32Element(uintptr_t objectPointer, int32_t elementIndex)
   {
   TR_ASSERT(haveAccess(), "getInt32Element requires VM access");
   return J9JAVAARRAYOFINT_LOAD(vmThread(), objectPointer, elementIndex);
   }

uintptr_t
TR_J9VMBase::getReferenceElement(uintptr_t objectPointer, intptr_t elementIndex)
   {
   TR_ASSERT(haveAccess(), "getReferenceElement requires VM access");
   return (uintptr_t)J9JAVAARRAYOFOBJECT_LOAD(vmThread(), objectPointer, elementIndex);
   }

TR_arrayTypeCode TR_J9VMBase::getPrimitiveArrayTypeCode(TR_OpaqueClassBlock* clazz)
   {
   TR_ASSERT(isPrimitiveClass(clazz), "Expect primitive class in TR_J9VMBase::getPrimitiveArrayType");

   J9Class* j9clazz = (J9Class*)clazz;
   if (j9clazz == jitConfig->javaVM->booleanReflectClass)
      return atype_boolean;
   else if (j9clazz == jitConfig->javaVM->charReflectClass)
      return atype_char;
   else if (j9clazz == jitConfig->javaVM->floatReflectClass)
      return atype_float;
   else if (j9clazz == jitConfig->javaVM->doubleReflectClass)
      return atype_double;
   else if (j9clazz == jitConfig->javaVM->byteReflectClass)
      return atype_byte;
   else if (j9clazz == jitConfig->javaVM->shortReflectClass)
      return atype_short;
   else if (j9clazz == jitConfig->javaVM->intReflectClass)
      return atype_int;
   else if (j9clazz == jitConfig->javaVM->longReflectClass)
      return atype_long;
   else
      {
      TR_ASSERT(false, "TR_arrayTypeCode is not defined for the j9clazz");
      return (TR_arrayTypeCode)0;
      }
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getClassFromJavaLangClass(uintptr_t objectPointer)
   {
   return (TR_OpaqueClassBlock*)J9VM_J9CLASS_FROM_HEAPCLASS(_vmThread, objectPointer);
   }

uintptr_t
TR_J9VMBase::getConstantPoolFromMethod(TR_OpaqueMethodBlock *method)
   {
   return (uintptr_t)J9_CP_FROM_METHOD((J9Method *)method);
   }

uintptr_t
TR_J9VMBase::getConstantPoolFromClass(TR_OpaqueClassBlock *clazz)
   {
   return (uintptr_t)J9_CP_FROM_CLASS((J9Class *)clazz);
   }

void TR_J9VMBase::printVerboseLogHeader(TR::Options *cmdLineOptions)
   {
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"Version Information:");
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     JIT Level  - %s", getJ9JITConfig()->jitLevelName);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     JVM Level  - %s", EsBuildVersionString);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     GC Level   - %s", J9VM_VERSION_STRING);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"");

   const char *vendorId;
   const char* processorName = TR::Compiler->target.cpu.getProcessorName();

#if defined(TR_TARGET_X86)
   vendorId =  TR::Compiler->target.cpu.getX86ProcessorVendorId();
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"Processor Information:");
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Platform Info:%s",processorName);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Vendor:%s",vendorId);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     numProc=%u",TR::Compiler->target.numberOfProcessors());
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"");
#endif

#if !defined(TR_TARGET_X86) //CrossCompilation, will be removed
#if defined(TR_TARGET_POWER)
   vendorId = "Unknown";
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"Processor Information:");
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Platform Info:%s", processorName);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports HardwareSQRT:%d", TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_HW_SQRT_FIRST));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports HardwareRound:%d", TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_HW_ROUND_FIRST));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports HardwareCopySign:%d", TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_HW_COPY_SIGN_FIRST));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports FPU:%d", TR::Compiler->target.cpu.hasFPU());
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports DFP:%d", TR::Compiler->target.cpu.supportsDecimalFloatingPoint());
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports VMX:%d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_PPC_HAS_ALTIVEC));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports VSX:%d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports AES:%d", TR::Compiler->target.cpu.getPPCSupportsAES());
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Supports  TM:%d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_PPC_HTM));
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     Vendor:%s",vendorId);
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"     numProc=%u",TR::Compiler->target.numberOfProcessors());
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"");
#endif

#if defined(TR_TARGET_S390)
   vendorId = "IBM";
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "Processor Information:");
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "        Name: %s", processorName);
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "      Vendor: %s", vendorId);
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "     numProc: %u", TR::Compiler->target.numberOfProcessors());
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "         DFP: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_DFP));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "         FPE: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_FPE));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "         HPR: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_HIGH_WORD));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "          RI: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_RI));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "          TM: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_TE));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "          VF: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_VECTOR_FACILITY));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "          GS: %d", TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_GUARDED_STORAGE));
   TR_VerboseLog::writeLine(TR_Vlog_INFO, "");
#endif
#endif
   }

void TR_J9VMBase::printPID()
   {
   PORT_ACCESS_FROM_PORT(_portLibrary);

#if defined(TR_HOST_S390)
   struct tm *p;
   struct timeval tp;
   gettimeofday(&tp,0);
   p = localtime((const long *)&tp);

   #if defined(J9ZOS390)
   // print out the ASID (address space ID) to make it easier to run MVS-style tools like
   // slip traps, dumps, traces, etc.

   struct     oscb_ascb {         // MVS ASCB
     int eye;                    // EYE CATCHER
     int do_not_care[8];
     short ascbasid;              // ASID
   } * __ptr32 pascb;
   struct oscb_psa {              // MVS PSA CB
     int do_not_care[137];
     struct oscb_ascb * __ptr32 psaaold;  // POINTER TO ASCB
   };

   struct oscb_psa * ppsa = 0;     // 0 -> PSA

   pascb = ppsa -> psaaold;       // PSAAOLD -> ASCB
   TR_ASSERT( pascb->eye == 0xc1e2c3c2, "eye catcher not found");
   TR_VerboseLog::write("ASID=%d, ", (int) pascb->ascbasid);

   #endif /* z/OS specific ASID printing */

   TR_VerboseLog::write("PID=%d, %4d/%02d/%02d",j9sysinfo_get_pid(),
                 (p->tm_year)+1900,(p->tm_mon)+1,p->tm_mday);
#endif
   }

void TR_J9VMBase::emitNewPseudoRandomNumberVerbosePrefix()
   {
   TR_VerboseLog::vlogAcquire();
   TR_VerboseLog::write(TR_Vlog_INFO, "%s ", PSEUDO_RANDOM_NUMBER_PREFIX);
   //vlogRelease();
   }

void TR_J9VMBase::emitNewPseudoRandomNumberVerbose(int32_t i)
   {
   //vlogAcquire();
   TR_VerboseLog::write("%d ", i);
   //vlogRelease();
   }

void TR_J9VMBase::emitNewPseudoRandomVerboseSuffix()
   {
   //vlogAcquire();
   TR_VerboseLog::write("%c ", PSEUDO_RANDOM_SUFFIX);
   TR_VerboseLog::writeLine("");
   TR_VerboseLog::vlogRelease();
   }


void TR_J9VMBase::emitNewPseudoRandomNumberVerboseLine(int32_t i)
   {
   //vlogAcquire();
   emitNewPseudoRandomNumberVerbosePrefix();
   emitNewPseudoRandomNumberVerbose(i);
   emitNewPseudoRandomVerboseSuffix();
   //vlogRelease();
   }



void TR_J9VMBase::emitNewPseudoRandomStringVerbosePrefix()
   {
   TR_VerboseLog::vlogAcquire();
   TR_VerboseLog::writeLine(TR_Vlog_INFO,"%s ", PSEUDO_RANDOM_STRING_PREFIX);
   //vlogRelease();
   }

void TR_J9VMBase::emitNewPseudoRandomStringVerbose(char *c)
   {
   //vlogAcquire();
   TR_VerboseLog::write("%s ", c);
   //vlogRelease();
   }


void TR_J9VMBase::emitNewPseudoRandomStringVerboseLine(char *c)
   {
     //vlogAcquire();
   emitNewPseudoRandomStringVerbosePrefix();
   emitNewPseudoRandomStringVerbose(c);
   emitNewPseudoRandomVerboseSuffix();
   //vlogRelease();
   }

UDATA TR_J9VMBase::getLowTenureAddress()
   {
   return (UDATA) _vmThread->lowTenureAddress;
   }

UDATA TR_J9VMBase::getHighTenureAddress()
   {
   return (UDATA) _vmThread->highTenureAddress;
   }


bool TR_J9VMBase::generateCompressedPointers()
   {
   return TR::Compiler->om.compressObjectReferences();
   }


bool TR_J9VMBase::generateCompressedLockWord()
   {
   return TR::Compiler->om.compressObjectReferences();
   }


int32_t *TR_J9VMBase::getCurrentLocalsMapForDLT(TR::Compilation *comp)
   {
   int32_t           *currentBundles = NULL;

#if defined(J9VM_JIT_DYNAMIC_LOOP_TRANSFER)
   TR_ResolvedMethod *currentMethod = comp->getCurrentMethod();
   J9Method          *j9method = (J9Method *)(currentMethod->getPersistentIdentifier());
   int32_t            numBundles = currentMethod->numberOfTemps() + currentMethod->numberOfParameterSlots();

   numBundles = (numBundles+31)/32;
   currentBundles = (int32_t *)comp->trMemory()->allocateHeapMemory(numBundles * sizeof(int32_t));
   jitConfig->javaVM->localMapFunction(_portLibrary, J9_CLASS_FROM_METHOD(j9method)->romClass, getOriginalROMMethod(j9method), comp->getDltBcIndex(), (U_32 *)currentBundles, NULL, NULL, NULL);
#endif

   return currentBundles;
   }

UDATA TR_J9VMBase::getOffsetOfInitializeStatusFromClassField()
   {
   return offsetof(J9Class, initializeStatus);
   }

UDATA TR_J9VMBase::getOffsetOfJavaLangClassFromClassField()
   {
   return offsetof(J9Class, classObject);
   }

UDATA TR_J9VMBase::getOffsetOfLastITableFromClassField()
   {
   return offsetof(J9Class, lastITable);

   // Note: the "iTable" field is also a suitable choice here, except it can
   // sometimes be null.  It was useful for early experimentation, but not for
   // production unless we add null checks where necessary.
   }

UDATA TR_J9VMBase::getOffsetOfInterfaceClassFromITableField()
   {
   return offsetof(J9ITable, interfaceClass);
   }

int32_t TR_J9VMBase::getITableEntryJitVTableOffset()
   {
   return sizeof(J9Class);
   }

int32_t TR_J9VMBase::convertITableIndexToOffset(uintptr_t itableIndex)
   {
   return (int32_t)(sizeof(J9ITable) + itableIndex*sizeof(UDATA));
   }
UDATA TR_J9VMBase::getOffsetOfInstanceShapeFromClassField()
   {
   return offsetof(J9Class, totalInstanceSize);
   }

UDATA TR_J9VMBase::getOffsetOfInstanceDescriptionFromClassField()
   {
   return offsetof(J9Class, instanceDescription);
   }

UDATA TR_J9VMBase::getOffsetOfDescriptionWordFromPtrField()
   {
   return 0;
   }


UDATA TR_J9VMBase::getOffsetOfClassFromJavaLangClassField()
   {
   return J9VMCONSTANTPOOL_ADDRESS_OFFSET(J9VMTHREAD_JAVAVM(_vmThread), J9VMCONSTANTPOOL_JAVALANGCLASS_VMREF);
   }


UDATA TR_J9VMBase::getOffsetOfRamStaticsFromClassField()            {return offsetof(J9Class, ramStatics);}
UDATA TR_J9VMBase::getOffsetOfIsArrayFieldFromRomClass()            {return offsetof(J9ROMClass, modifiers);}
UDATA TR_J9VMBase::getOffsetOfClassAndDepthFlags()                  {return offsetof(J9Class, classDepthAndFlags);}
UDATA TR_J9VMBase::getOffsetOfClassFlags()                          {return offsetof(J9Class, classFlags);}
UDATA TR_J9VMBase::getOffsetOfArrayComponentTypeField()             {return offsetof(J9ArrayClass, componentType);}
UDATA TR_J9VMBase::constReleaseVMAccessOutOfLineMask()              {return J9_PUBLIC_FLAGS_VMACCESS_RELEASE_BITS;}
UDATA TR_J9VMBase::constReleaseVMAccessMask()                       {return ~constAcquireVMAccessOutOfLineMask();}
UDATA TR_J9VMBase::constAcquireVMAccessOutOfLineMask()              {return J9_PUBLIC_FLAGS_VMACCESS_ACQUIRE_BITS;}
UDATA TR_J9VMBase::constJNICallOutFrameFlags()                      {return J9_SSF_JIT_JNI_CALLOUT;}
UDATA TR_J9VMBase::constJNICallOutFrameType()                       {return J9SF_FRAME_TYPE_JIT_JNI_CALLOUT;}
UDATA TR_J9VMBase::constJNICallOutFrameSpecialTag()                 {return 0;}
UDATA TR_J9VMBase::constJNICallOutFrameInvisibleTag()               {return J9SF_A0_INVISIBLE_TAG;}
UDATA TR_J9VMBase::constJNICallOutFrameFlagsOffset()                {return offsetof(J9SFMethodFrame, specialFrameFlags);}

UDATA TR_J9VMBase::constJNIReferenceFrameAllocatedFlags()       {return J9_SSF_JIT_JNI_FRAME_COLLAPSE_BITS;}

UDATA TR_J9VMBase::constClassFlagsPrimitive()   {return J9AccClassInternalPrimitiveType;}
UDATA TR_J9VMBase::constClassFlagsAbstract()    {return J9AccAbstract;}
UDATA TR_J9VMBase::constClassFlagsFinal()       {return J9AccFinal;}
UDATA TR_J9VMBase::constClassFlagsPublic()      {return J9AccPublic;}

int32_t TR_J9VMBase::getFlagValueForPrimitiveTypeCheck()        {return J9AccClassInternalPrimitiveType;}
int32_t TR_J9VMBase::getFlagValueForArrayCheck()                {return J9AccClassArray;}
int32_t TR_J9VMBase::getFlagValueForFinalizerCheck()            {return J9AccClassFinalizeNeeded | J9AccClassOwnableSynchronizer;}


UDATA TR_J9VMBase::getGCForwardingPointerOffset()               {
                                                                   return 0;
                                                                }

bool TR_J9VMBase::getNurserySpaceBounds(uintptr_t *base, uintptr_t *top)
   {
   J9JavaVM *vm = _jitConfig->javaVM;
   J9MemoryManagerFunctions * mmf = vm->memoryManagerFunctions;
   mmf->j9mm_get_guaranteed_nursery_range(getJ9JITConfig()->javaVM, (void**) base, (void**) top);

   return true;
   }

bool TR_J9VMBase::jniRetainVMAccess(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_RETAIN_VM_ACCESS) != 0; }
bool TR_J9VMBase::jniNoGCPoint(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_NOT_GC_POINT) != 0; }
bool TR_J9VMBase::jniNoNativeMethodFrame(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_NO_NATIVE_METHOD_FRAME) != 0; }
bool TR_J9VMBase::jniNoExceptionsThrown(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_NO_EXCEPTION_THROW) != 0; }
bool TR_J9VMBase::jniNoSpecialTeardown(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_NO_SPECIAL_TEAR_DOWN) != 0; }
bool TR_J9VMBase::jniDoNotWrapObjects(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_DO_NOT_WRAP_OBJECTS) != 0; }
bool TR_J9VMBase::jniDoNotPassReceiver(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_DO_NOT_PASS_RECEIVER) != 0; }
bool TR_J9VMBase::jniDoNotPassThread(TR_ResolvedMethod *method) { return (((TR_ResolvedJ9Method *)method)->getJNIProperties() & J9_FAST_JNI_DO_NOT_PASS_THREAD) != 0; }

UDATA
TR_J9VMBase::thisThreadRememberedSetFragmentOffset()
   {

#if defined(J9VM_GC_REALTIME)
      return offsetof(J9VMThread, sATBBarrierRememberedSetFragment);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getFragmentParentOffset()
   {

#if defined(J9VM_GC_REALTIME)
    return offsetof(MM_GCRememberedSetFragment, fragmentParent);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getRememberedSetGlobalFragmentOffset()
   {
#if defined(J9VM_GC_REALTIME)
      return offsetof(MM_GCRememberedSet, globalFragmentIndex);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getLocalFragmentOffset()
   {
#if defined(J9VM_GC_REALTIME)
       return offsetof(MM_GCRememberedSetFragment, localFragmentIndex);
#endif
    return 0;
   }

UDATA
TR_J9VMBase::thisThreadJavaVMOffset()
   {
   return offsetof(J9VMThread, javaVM);
   }

UDATA
TR_J9VMBase::getMaxObjectSizeForSizeClass()
   {
#if defined(J9VM_GC_REALTIME)
      return J9VMGC_SIZECLASSES_MAX_SMALL_SIZE_BYTES;
#endif
   return 0;
   }

UDATA
TR_J9VMBase::thisThreadAllocationCacheCurrentOffset(uintptr_t sizeClass)
   {
#if defined(J9VM_GC_REALTIME)
   return J9_VMTHREAD_SEGREGATED_ALLOCATION_CACHE_OFFSET +
      sizeClass * sizeof(J9VMGCSegregatedAllocationCacheEntry) +
      offsetof(J9VMGCSegregatedAllocationCacheEntry, current);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::thisThreadAllocationCacheTopOffset(uintptr_t sizeClass)
   {
#if defined(J9VM_GC_REALTIME)
   return J9_VMTHREAD_SEGREGATED_ALLOCATION_CACHE_OFFSET +
      sizeClass * sizeof(J9VMGCSegregatedAllocationCacheEntry) +
      offsetof(J9VMGCSegregatedAllocationCacheEntry, top);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getCellSizeForSizeClass(uintptr_t sizeClass)
   {
#if defined(J9VM_GC_REALTIME)
   J9JavaVM * javaVM = _jitConfig->javaVM;
   return javaVM->realtimeSizeClasses->smallCellSizes[sizeClass];
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getObjectSizeClass(uintptr_t objectSize)
   {
#if defined(J9VM_GC_REALTIME)
   J9JavaVM * javaVM = _jitConfig->javaVM;
   return javaVM->realtimeSizeClasses->sizeClassIndex[objectSize / sizeof(UDATA)];
#endif
   return 0;
   }

UDATA
TR_J9VMBase::thisThreadMonitorCacheOffset()
   {
#if defined(J9VM_OPT_REAL_TIME_LOCKING_SUPPORT)
   return offsetof(J9VMThread, monitorCache);
#else
   TR_ASSERT(0,"no monitorCache thread slot");
   return 0;
#endif
   }

UDATA
TR_J9VMBase::thisThreadOSThreadOffset()
   {
   return offsetof(J9VMThread, osThread);
   }

UDATA
TR_J9VMBase::getMonitorNextOffset()
   {
#if defined(J9VM_OPT_REAL_TIME_LOCKING_SUPPORT)
   return offsetof(J9ThreadAbstractMonitor, next);
#else
   TR_ASSERT(0,"no next field in J9ThreadAbstractMonitor");
   return 0;
#endif
   }

UDATA
TR_J9VMBase::getMonitorOwnerOffset()
   {
   return offsetof(J9ThreadAbstractMonitor, owner);
   }

UDATA
TR_J9VMBase::getMonitorEntryCountOffset()
   {
   return offsetof(J9ThreadAbstractMonitor, count);
   }


UDATA
TR_J9VMBase::getRealtimeSizeClassesOffset()
   {
#if defined(J9VM_GC_REALTIME)
   return offsetof(J9JavaVM, realtimeSizeClasses);
#endif
   return 0;
    }

UDATA
TR_J9VMBase::getSmallCellSizesOffset()
   {
#if defined(J9VM_GC_REALTIME)
   return offsetof(J9VMGCSizeClasses, smallCellSizes);
#endif
   return 0;
   }

UDATA
TR_J9VMBase::getSizeClassesIndexOffset()
   {
#if defined(J9VM_GC_REALTIME)
   return offsetof(J9VMGCSizeClasses, sizeClassIndex);
#endif
   return 0;
   }

UDATA TR_J9VMBase::thisThreadGetProfilingBufferCursorOffset()
   {
   return offsetof(J9VMThread, profilingBufferCursor);
   }

UDATA TR_J9VMBase::thisThreadGetProfilingBufferEndOffset()
   {
   return offsetof(J9VMThread, profilingBufferEnd);
   }

UDATA TR_J9VMBase::thisThreadGetOSRBufferOffset()
   {
   return offsetof(J9VMThread, osrBuffer);
   }

UDATA TR_J9VMBase::thisThreadGetOSRScratchBufferOffset()
   {
   return offsetof(J9VMThread, osrScratchBuffer);
   }

UDATA TR_J9VMBase::thisThreadGetOSRFrameIndexOffset()
   {
   return offsetof(J9VMThread, osrFrameIndex);
   }

UDATA TR_J9VMBase::thisThreadGetOSRReturnAddressOffset()
   {
   return offsetof(J9VMThread, osrReturnAddress);
   }

#if defined(TR_TARGET_S390)
/**
 * @brief TDB is Transaction Diagnostic Block used for Transactional Memory Debugging.
 * It is a 256 Byte block stored in the J9VMThread and must be 8 byte boundary aligned.
 */
uint16_t TR_J9VMBase::thisThreadGetTDBOffset()
   {
#if !defined(J9ZTPF)
   // This assume is valid to confirm the TDB is on an 8 byte boundary because we know the VMThread is already aligned on a 256 byte boundary
   // If this assume fails, then that means someone modified the J9VMThread structure which has thrown off the alignment.
   TR_ASSERT(offsetof(J9VMThread, transactionDiagBlock) % 8 == 0, "The Transaction Diagnostic Block must be aligned on a doubleword i.e. 8 byte boundary");
   return offsetof(J9VMThread, transactionDiagBlock);
#else
   Assert_JIT_unreachable();
   return 0;
#endif
   }
#endif

/**
 * @brief Returns offset from the current thread to the intermediate result field.
 * The field contains intermediate result from the latest guarded load during concurrent scavenge.
 */
UDATA TR_J9VMBase::thisThreadGetGSIntermediateResultOffset()
   {
#if defined(OMR_GC_CONCURRENT_SCAVENGER) && (defined(S390) || defined(J9ZOS390))
   return offsetof(J9VMThread, gsParameters.intermediateResult);
#else /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   TR_ASSERT(0,"Field gsParameters.intermediateResult does not exists in J9VMThread.");
   return 0;
#endif /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   }

/**
 * @brief Returns offset from the current thread to the flags to check if concurrent scavenge is active
 */
UDATA TR_J9VMBase::thisThreadGetConcurrentScavengeActiveByteAddressOffset()
   {
   int32_t privateFlagsConcurrentScavengerActiveByteOffset = 5;
   return offsetof(J9VMThread, privateFlags) + privateFlagsConcurrentScavengerActiveByteOffset;
   }

/**
 * @brief Returns offset from the current thread to the field with the base address of the evacuate memory region
 */
UDATA TR_J9VMBase::thisThreadGetEvacuateBaseAddressOffset()
   {
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
#if defined(OMR_GC_COMPRESSED_POINTERS)
  if (TR::Compiler->om.compressObjectReferences())
      return offsetof(J9VMThread, readBarrierRangeCheckBaseCompressed);
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
   return offsetof(J9VMThread, readBarrierRangeCheckBase);
#else /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   TR_ASSERT(0,"Field readBarrierRangeCheckBase does not exists in J9VMThread.");
   return 0;
#endif /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   }

/**
  * @brief Returns offset from the current thread to the field with the top address of the evacuate memory region
  */
UDATA TR_J9VMBase::thisThreadGetEvacuateTopAddressOffset()
   {
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
#if defined(OMR_GC_COMPRESSED_POINTERS)
   if (TR::Compiler->om.compressObjectReferences())
      return offsetof(J9VMThread, readBarrierRangeCheckTopCompressed);
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
   return offsetof(J9VMThread, readBarrierRangeCheckTop);
#else /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   TR_ASSERT(0,"Field readBarrierRangeCheckTop does not exists in J9VMThread.");
   return 0;
#endif /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   }

/**
 * @brief Returns offset from the current thread to the operand address field.
 * It contains data from the most recent guarded load during concurrent scavenge
 */
UDATA TR_J9VMBase::thisThreadGetGSOperandAddressOffset()
   {
#if defined(OMR_GC_CONCURRENT_SCAVENGER) && (defined(S390) || defined(J9ZOS390))
   return offsetof(J9VMThread, gsParameters.operandAddr);
#else /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   TR_ASSERT(0,"Field gsParameters.operandAddr does not exists in J9VMThread.");
   return 0;
#endif /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   }

/**
 * @brief Returns offset from the current thread to the filed with the read barrier handler address
 */
UDATA TR_J9VMBase::thisThreadGetGSHandlerAddressOffset()
   {
#if defined(OMR_GC_CONCURRENT_SCAVENGER) && (defined(S390) || defined(J9ZOS390))
   return offsetof(J9VMThread, gsParameters.handlerAddr);
#else /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   TR_ASSERT(0,"Field gsParameters.handlerAddr does not exists in J9VMThread.");
   return 0;
#endif /* defined(OMR_GC_CONCURRENT_SCAVENGER) */
   }

// This query answers whether or not the commandline option -XCEEHDLR was passed in.
//
bool TR_J9VMBase::CEEHDLREnabled()
   {
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
   J9JavaVM * vm = _jitConfig->javaVM;

   if (J9_SIG_ZOS_CEEHDLR == (vm->sigFlags & J9_SIG_ZOS_CEEHDLR))
      return true;
#endif

   return false;
   }


int32_t TR_J9VMBase::getArraySpineShift(int32_t width)
   {
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");
   TR_ASSERT(width >= 0, "unexpected arraylet datatype width");

   // for elements larger than bytes, need to reduce the shift because fewer elements
   // fit into each arraylet

   int32_t shift=-1;
   int32_t maxShift = TR::Compiler->om.arrayletLeafLogSize();

   switch(width)
      {
      case 1 : shift = maxShift-0; break;
      case 2 : shift = maxShift-1; break;
      case 4 : shift = maxShift-2; break;
      case 8 : shift = maxShift-3; break;
      default: TR_ASSERT(0,"unexpected element width");
      }
   return shift;
   }

int32_t TR_J9VMBase::getArrayletMask(int32_t width)
   {
   TR::Compilation* comp = TR::comp();
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");
   TR_ASSERT(width >= 0, "unexpected arraylet datatype width");
   int32_t mask=(1 << getArraySpineShift(width))-1;
   return mask;
   }

int32_t TR_J9VMBase::getArrayletLeafIndex(int64_t index, int32_t elementSize)
   {
   TR::Compilation* comp = TR::comp();
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");
   TR_ASSERT(elementSize >= 0, "unexpected arraylet datatype width");

   if (index<0)
      return -1;
   int32_t arrayletIndex = (index >> getArraySpineShift(elementSize));
   return  arrayletIndex;
   }

int32_t TR_J9VMBase::getLeafElementIndex(int64_t index , int32_t elementSize)
   {
   TR::Compilation* comp = TR::comp();
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");
   TR_ASSERT(elementSize >= 0, "unexpected arraylet datatype width");

   if (index<0)
      return -1;
   int32_t leafIndex = (index & getArrayletMask(elementSize));
   return leafIndex;
   }


int32_t TR_J9VMBase::getFirstArrayletPointerOffset(TR::Compilation *comp)
   {
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");

   int32_t headerSize = TR::Compiler->om.useHybridArraylets()
           ? TR::Compiler->om.discontiguousArrayHeaderSizeInBytes()
           : TR::Compiler->om.contiguousArrayHeaderSizeInBytes();

   return (headerSize + TR::Compiler->om.sizeofReferenceField()-1) & (-1)*(intptr_t)(TR::Compiler->om.sizeofReferenceField());
   }

int32_t TR_J9VMBase::getArrayletFirstElementOffset(int8_t elementSize, TR::Compilation *comp)
   {
   TR_ASSERT(TR::Compiler->om.canGenerateArraylets(), "not supposed to be generating arraylets!");
   TR_ASSERT(elementSize >= 0, "unexpected arraylet element size");

   int32_t offset;
   if (TR::Compiler->om.compressObjectReferences())
      {
      offset = (getFirstArrayletPointerOffset(comp) + TR::Compiler->om.sizeofReferenceField() + sizeof(UDATA)-1) & (-(int32_t)sizeof(UDATA));
      TR_ASSERT((offset & sizeof(UDATA)-1) == 0, "unexpected alignment for first arraylet element");
      }
   else
      {
      if (elementSize > sizeof(UDATA))
         offset = (getFirstArrayletPointerOffset(comp) + sizeof(UDATA) + elementSize-1) & (-elementSize);
      else
         offset = getFirstArrayletPointerOffset(comp) + sizeof(UDATA);

      TR_ASSERT((offset & (elementSize-1)) == 0, "unexpected alignment for first arraylet element");
      }

   return offset;
   }

// This is used on Z/OS direct JNI to restore the CAA register before calling to C.
//
int32_t TR_J9VMBase::getCAASaveOffset()
   {
   #if defined(J9TR_CAA_SAVE_OFFSET)
      return J9TR_CAA_SAVE_OFFSET;
   #else
      return 0;
   #endif
   }

uint32_t
TR_J9VMBase::getWordOffsetToGCFlags()
   {
#if defined(J9VM_INTERP_FLAGS_IN_CLASS_SLOT) && defined(TR_TARGET_64BIT)
   if (!TR::Compiler->om.compressObjectReferences())
     return TR::Compiler->om.offsetOfHeaderFlags() + 4;
#endif
   return TR::Compiler->om.offsetOfHeaderFlags();
   }

uint32_t
TR_J9VMBase::getWriteBarrierGCFlagMaskAsByte()
   {
   return (OBJECT_HEADER_OLD) >> 8; //shift right 8 bits
   }


int32_t
TR_J9VMBase::getByteOffsetToLockword(TR_OpaqueClassBlock * clazzPointer)
   {
   if (clazzPointer == NULL)
      return 0;

   return TR::Compiler->cls.convertClassOffsetToClassPtr(clazzPointer)->lockOffset;
   }

int32_t
TR_J9VMBase::getInitialLockword(TR_OpaqueClassBlock* ramClass)
   {
   /* If ramClass is NULL for some reason, initial lockword is set to 0. */
   if (!ramClass)
      {
      return 0;
      }

   return VM_ObjectMonitor::getInitialLockword(_jitConfig->javaVM, TR::Compiler->cls.convertClassOffsetToClassPtr(ramClass));
   }

bool
TR_J9VMBase::isEnableGlobalLockReservationSet()
   {
   return (1 == _jitConfig->javaVM->enableGlobalLockReservation) ? true : false;
   }

bool
TR_J9VMBase::isLogSamplingSet()
   {
   return TR::Options::getJITCmdLineOptions() && TR::Options::getJITCmdLineOptions()->getVerboseOption(TR_VerboseSampling);
   }


uintptr_t
TR_J9VMBase::getOffsetOfIndexableSizeField()
   {
   return offsetof(J9ROMArrayClass, arrayShape);
   }


// This method can be called by an app thread in onLoadInternal
TR_Debug *
TR_J9VMBase::createDebug(TR::Compilation *comp)
   {
   if (!_jitConfig->tracingHook)
      {
      _jitConfig->tracingHook = (void*) (TR_CreateDebug_t)createDebugObject;
      }

   TR_Debug *result = ((TR_CreateDebug_t)_jitConfig->tracingHook)(comp);

   return result;
   }

TR_ResolvedMethod *
TR_J9VMBase::getDefaultConstructor(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer)
   {
   TR::VMAccessCriticalSection getDefaultConstructor(this);
   List<TR_ResolvedMethod> list(trMemory);
   getResolvedMethods(trMemory, classPointer, &list);
   ListIterator<TR_ResolvedMethod> methods(&list);
   TR_ResolvedMethod * m = methods.getFirst();
   for (; m; m = methods.getNext())
      if (m->isConstructor() && m->signatureLength() == 3 && !strncmp(m->signatureChars(), "()V", 3))
         break;
   return m;
   }

extern "C" J9NameAndSignature newInstancePrototypeNameAndSig;

uint32_t
TR_J9VMBase::getNewInstanceImplVirtualCallOffset()
   {
   return offsetof(J9Class, romableAotITable); // should be something like offsetof(J9Class, newInstanceImpl)
   }


uint8_t *
TR_J9VMBase::allocateDataCacheRecord(uint32_t numBytes, TR::Compilation *comp,
                                     bool contiguous, bool *shouldRetryAllocation,
                                     uint32_t allocationType, uint32_t *allocatedSizePtr)
   {
   U_8* retValue = NULL;



   if (contiguous || ((_jitConfig->runtimeFlags & J9JIT_TOSS_CODE) && comp) )
      {
      // need to allocate space for header too and do some alignment
      uint32_t size = TR_DataCacheManager::alignToMachineWord(numBytes + sizeof(J9JITDataCacheHeader));
      U_8* ptr = NULL;
      TR_ASSERT(comp, "Contiguous allocations require compilation object");
      *shouldRetryAllocation = false;
      // Increment the space needed by this compilation in the data cache,
      // even if the allocation might fail
      //
      comp->incrementTotalNeededDataCacheSpace(size);
      TR_DataCache *dataCache = (TR_DataCache*)comp->getReservedDataCache();
      // If we have a reserved data cache, use that in preference of any other
      if (dataCache)
         {
         ptr = dataCache->allocateDataCacheSpace(size);
         if (!ptr) // designated data cache is not big enough
            {
            TR_DataCacheManager::getManager()->retireDataCache(dataCache);
            // Reserve a new segment that is capable to hold the entire size
            // TODO: find the appropriate vmThread
            dataCache = TR_DataCacheManager::getManager()->reserveAvailableDataCache(_vmThread, comp->getTotalNeededDataCacheSpace());
            comp->setReservedDataCache(dataCache);
            if (dataCache)
               {
               *shouldRetryAllocation = true;
               }
            }
         }
      else // No reserved data cache
         {
         // If we want contiguous allocation we must reserve a data cache now
         dataCache = TR_DataCacheManager::getManager()->reserveAvailableDataCache(_vmThread, comp->getTotalNeededDataCacheSpace());
         comp->setReservedDataCache(dataCache);
         if (dataCache)
            {
            ptr = dataCache->allocateDataCacheSpace(size);
            }
         }
      if (ptr) // I managed to allocate some space
         {
         // Complete the header
         TR_DataCacheManager::getManager()->fillDataCacheHeader((J9JITDataCacheHeader*)ptr, allocationType, size);
         if (allocatedSizePtr)
            *allocatedSizePtr = size - sizeof(J9JITDataCacheHeader); // communicate back the allocated size
          // Return the location after the header
         retValue = (ptr + sizeof(J9JITDataCacheHeader));
         }
      }
   else // not a contiguous allocation, use data cache manager.
      {
      retValue = TR_DataCacheManager::getManager()->allocateDataCacheRecord(numBytes, allocationType, allocatedSizePtr);
      }

   return retValue;
   }



uint8_t *
TR_J9VMBase::allocateRelocationData(TR::Compilation * comp, uint32_t numBytes)
   {
   uint8_t * relocationData = NULL;
   uint32_t size = 0;
   bool shouldRetryAllocation;
   relocationData = allocateDataCacheRecord(numBytes, comp, needsContiguousCodeAndDataCacheAllocation(), &shouldRetryAllocation,
                                            J9_JIT_DCE_RELOCATION_DATA, &size);
   if (!relocationData)
      {
      if (shouldRetryAllocation)
         {
         // force a retry
         comp->failCompilation<J9::RecoverableDataCacheError>("Failed to allocate relocation data");
         }
      comp->failCompilation<J9::DataCacheError>("Failed to allocate relocation data");
      }

   return relocationData;
   }


uint8_t *
TR_J9VMBase::allocateCodeMemory(TR::Compilation * comp, uint32_t warmCodeSize, uint32_t coldCodeSize, uint8_t ** coldCode, bool isMethodHeaderNeeded)
   {
   return comp->cg()->allocateCodeMemoryInner(warmCodeSize, coldCodeSize, coldCode, isMethodHeaderNeeded);
   }

bool
TR_J9VMBase::supportsCodeCacheSnippets()
   {
#if defined(TR_TARGET_X86)
   return  (!TR::Options::getCmdLineOptions()->getOption(TR_DisableCodeCacheSnippets));
#else
   return false;
#endif
   }

#if defined(TR_TARGET_X86)
void *
TR_J9VMBase::getAllocationPrefetchCodeSnippetAddress(TR::Compilation * comp)
   {
   TR::CodeCache * codeCache = comp->cg()->getCodeCache();
   return codeCache->getCCPreLoadedCodeAddress(TR_AllocPrefetch, comp->cg());
   }

void *
TR_J9VMBase::getAllocationNoZeroPrefetchCodeSnippetAddress(TR::Compilation * comp)
   {
   TR::CodeCache * codeCache = comp->cg()->getCodeCache();
   return codeCache->getCCPreLoadedCodeAddress(TR_NonZeroAllocPrefetch, comp->cg());
   }
#endif

// This routine may be called on the compilation thread or from a runtime hook
//
void
TR_J9VMBase::releaseCodeMemory(void *startPC, uint8_t bytesToSaveAtStart)
   {
   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableCodeCacheReclamation))
      {
      TR::VMAccessCriticalSection releaseCodeMemory(this);
      J9JavaVM            *vm        = jitConfig->javaVM;
      J9VMThread          *vmContext = vm->internalVMFunctions->currentVMThread(vm);
      J9JITExceptionTable *metaData  = jitConfig->jitGetExceptionTableFromPC(vmContext, (UDATA)startPC);
      vlogReclamation("Queuing for reclamation", metaData, bytesToSaveAtStart);
      TR::CodeCacheManager::instance()->addFaintCacheBlock(metaData, bytesToSaveAtStart);
      }
   }

bool
TR_J9VMBase::vmRequiresSelector(uint32_t mask)
   {
   return true;
   }

bool
TR_J9VMBase::callTheJitsArrayCopyHelper()
   {
   return true;
   }

void*
TR_J9VMBase::getReferenceArrayCopyHelperAddress()
   {
   // Get the function descriptor of referenceArrayCopy ( a C routine in the GC module)
   // Each platform will have its own implementation to set up the call to referenceArrayCopy
   J9JavaVM * jvm = _jitConfig->javaVM;
   return (void*) jvm->memoryManagerFunctions->referenceArrayCopy;
   }

bool
TR_J9VMBase::isClassFinal(TR_OpaqueClassBlock * clazz)
   {
   return (TR::Compiler->cls.romClassOf(clazz)->modifiers & J9AccFinal) ? true : false;
   }

bool
TR_J9VMBase::hasFinalFieldsInClass(TR_OpaqueClassBlock * clazz)
   {
   J9Class *clazzPtr = TR::Compiler->cls.convertClassOffsetToClassPtr(clazz);
   return (clazzPtr->classDepthAndFlags & J9AccClassHasFinalFields)!=0;
   }

static uint32_t offsetOfHotFields() { return offsetof(J9Class, instanceHotFieldDescription); }

class TR_MarkHotField : public TR_SubclassVisitor
   {
public:
   TR_MarkHotField(TR::Compilation * comp, TR::SymbolReference * symRef)
      : TR_SubclassVisitor(comp), _symRef(symRef) { }

   void mark(J9Class *, bool);

   virtual bool visitSubclass(TR_PersistentClassInfo *);

private:

   bool markHotField(J9Class * clazz, bool baseClass);

   TR::SymbolReference * _symRef;
   UDATA                _bitValue;
   UDATA                _slotIndex;
   };

void
TR_MarkHotField::mark(J9Class * clazz, bool isFixedClass)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(_comp->fe());
   if (fej9->isAOT_DEPRECATED_DO_NOT_USE())
      return;

   if ((*(UDATA *)((char *)clazz + offsetOfHotFields()) & 0x1))
      {
      // temporary hack: tenure aligned classes can't have
      // hot fields marked, we need another word for this
      if (_comp->getOption(TR_TraceMarkingOfHotFields))
         {
         J9ROMClass* romClass = TR::Compiler->cls.romClassOf((TR_OpaqueClassBlock *)clazz);
         J9UTF8* name = J9ROMCLASS_CLASSNAME(romClass);
         printf("Rejected class %.*s for hot field marking because it's marked for tenured alignment\n", J9UTF8_LENGTH(name), J9UTF8_DATA(name));
         }
      return;
      }

   if (!_symRef->getSymbol()->getShadowSymbol() || _symRef->isUnresolved() || !clazz)
      return;

   if ((uintptr_t)_symRef->getOffset() < fej9->getObjectHeaderSizeInBytes())
      return;

   _slotIndex = ((_symRef->getOffset() - fej9->getObjectHeaderSizeInBytes()) / TR::Compiler->om.sizeofReferenceField()) + 1; // +1 because low order bit means tenured alignment
   if (_slotIndex > 30) // not 31 because low order bit means tenured alignment
      return;

   _bitValue = (UDATA)1 << _slotIndex;

   if (!markHotField(clazz, true))
      return;

   if (!isFixedClass)
      {
      setTracing(_comp->getOption(TR_TraceMarkingOfHotFields));
      visit(fej9->convertClassPtrToClassOffset(clazz));
      }
   }

bool
TR_MarkHotField::visitSubclass(TR_PersistentClassInfo * subclassInfo)
   {
   return markHotField(TR::Compiler->cls.convertClassOffsetToClassPtr(subclassInfo->getClassId()), false);
   }

bool
TR_MarkHotField::markHotField(J9Class * clazz, bool rootClass)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(_comp->fe());
   if (fej9->isAOT_DEPRECATED_DO_NOT_USE())
      return false;

   // If the bit is already marked in the class then we don't need to walk this classes subclasses.
   // Returning false indicates to the visitor to not walk this classes subclasses.
   //
   UDATA noncoldWord= *(UDATA *)((char *)clazz + offsetOfHotFields());
   if (noncoldWord & _bitValue)
      return false;

   UDATA * descriptorPtr = clazz->instanceDescription;
   UDATA descriptorWord;
   if (((UDATA) descriptorPtr) & BCT_J9DescriptionImmediate)
      descriptorWord = ((UDATA) descriptorPtr) >> 1;
   else
      descriptorWord = descriptorPtr[0];

   // Check that the field is a member of the class.  At the time this code was written there were cases
   // when value propation would call this function with the class being Object and the field being String.value
   //
   if (!(descriptorWord & _bitValue))
      return false;

   if (_comp->getOption(TR_TraceMarkingOfHotFields))
      {
      if (rootClass)
         {
         int32_t len; char * s = _symRef->getOwningMethod(_comp)->fieldName(_symRef->getCPIndex(), len, _comp->trMemory());
         printf("hot field %*s with bitValue=%" OMR_PRIuPTR " and slotIndex=%" OMR_PRIuPTR " found while compiling \n   %s\n", len, s, _bitValue, _slotIndex, _comp->signature());
         }

      J9ROMClass* romClass = TR::Compiler->cls.romClassOf((TR_OpaqueClassBlock*)clazz);
      J9UTF8* name = J9ROMCLASS_CLASSNAME(romClass);
      printf("%*smarked field as hot in class %.*s\n", depth(), " ", J9UTF8_LENGTH(name), J9UTF8_DATA(name));
      }

   *(UDATA *)((char *)clazz + offsetOfHotFields()) = noncoldWord | _bitValue;

   return true;
   }

void
TR_J9VMBase::markHotField(TR::Compilation * comp, TR::SymbolReference * symRef, TR_OpaqueClassBlock * clazz, bool isFixedClass)
   {
   TR_MarkHotField marker(comp, symRef);
   marker.mark(TR::Compiler->cls.convertClassOffsetToClassPtr(clazz), isFixedClass);
   }


/**
 * Report a hot field if the JIT has determined that the field has met appropriate thresholds to be determined a hot field.
 * Valid if dynamicBreadthFirstScanOrdering is enabled.
 *
 * @param reducedCpuUtil normalized cpu utilization of the hot field for the method being compiled
 * @param clazz pointer to the class where a hot field should be added
 * @param fieldOffset value of the field offset that should be added as a hot field for the given class
 * @param reducedFrequency normalized block frequency of the hot field for the method being compiled
 */
void
TR_J9VMBase::reportHotField(int32_t reducedCpuUtil, J9Class* clazz, uint8_t fieldOffset,  uint32_t reducedFrequency)
{
   J9JavaVM * javaVM = _jitConfig->javaVM;
   javaVM->internalVMFunctions->reportHotField(javaVM, reducedCpuUtil, clazz, fieldOffset, reducedFrequency);
}

/**
 * Query if hot reference field is reqired for dynamicBreadthFirstScanOrdering
 *  @return true if scavenger dynamicBreadthFirstScanOrdering is enabled, 0 otherwise
 */
bool
TR_J9VMBase::isHotReferenceFieldRequired()
   {
   return TR::Compiler->om.isHotReferenceFieldRequired();
   }


bool
TR_J9VMBase::scanReferenceSlotsInClassForOffset(TR::Compilation * comp, TR_OpaqueClassBlock * classPointer, int32_t offset)
   {
   if (isAOT_DEPRECATED_DO_NOT_USE())
      return false;
   TR_VMFieldsInfo fields(comp, TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer), 1);

   if (!fields.getFields()) return false;

   ListIterator <TR_VMField> iter(fields.getFields());
   for (TR_VMField * field = iter.getFirst(); field != NULL; field = iter.getNext())
      {
      if (field->offset > offset)
         return false;

      if (field->isReference())
         {
         char *fieldSignature = field->signature;
         char *fieldName = field->name;

         int32_t fieldOffset = getInstanceFieldOffset(classPointer, fieldName, (uint32_t)strlen(fieldName), fieldSignature, (uint32_t)strlen(fieldSignature));
         if (fieldOffset == offset)
            {
            J9Class *fieldClass = TR::Compiler->cls.convertClassOffsetToClassPtr(getClassFromSignature(fieldSignature, (int32_t)strlen(fieldSignature), comp->getCurrentMethod()));

            if (fieldClass != NULL)
               {
               UDATA hotWordValue = *(UDATA *)((char *)fieldClass + offsetOfHotFields());

               if (hotWordValue & 0x1) return true;
               }
            }
         }
      }

   return false;
   }

int32_t
TR_J9VMBase::findFirstHotFieldTenuredClassOffset(TR::Compilation *comp, TR_OpaqueClassBlock *opclazz)
   {
   if (!isAOT_DEPRECATED_DO_NOT_USE())
      {
      J9Class *clazz = TR::Compiler->cls.convertClassOffsetToClassPtr(opclazz);
      UDATA hotFieldsWordValue = *(UDATA *)((char *)clazz + offsetOfHotFields());

      if (!hotFieldsWordValue)
         return -1;

      if (hotFieldsWordValue & 0x1)
         {
         // this class is marked for tenured alignment ignore it
         return -1;
         }

      for (int i = 1; i<31; i++)
         {
         uint32_t flag = (uint32_t)(hotFieldsWordValue & ((UDATA)1<<i));

         // if the field is marked find the
         // class type of this field and see if that class
         // is marked for tenured alignment
         if (flag)
            {
            uint32_t offset = (i-1) * TR::Compiler->om.sizeofReferenceField();
            if (scanReferenceSlotsInClassForOffset(comp, opclazz, offset))
               return (int32_t)(offset + getObjectHeaderSizeInBytes());
            }
         }
      }

   return -1;
   }



#if defined(TR_TARGET_X86)
#define CACHE_LINE_SIZE 64
#elif defined(TR_HOST_POWER)
#define CACHE_LINE_SIZE 128
#else
#define CACHE_LINE_SIZE 256
#endif


void
TR_J9VMBase::markClassForTenuredAlignment(TR::Compilation *comp, TR_OpaqueClassBlock *opclazz, uint32_t alignFromStart)
   {
   if (!jitConfig->javaVM->memoryManagerFunctions->j9gc_hot_reference_field_required(jitConfig->javaVM) && !isAOT_DEPRECATED_DO_NOT_USE())
      {
      J9Class *clazz = TR::Compiler->cls.convertClassOffsetToClassPtr(opclazz);
      UDATA hotFieldsWordValue = 0x1; // mark for alignment

      TR_ASSERT(0==(alignFromStart % TR::Compiler->om.objectAlignmentInBytes()), "alignment undershot should be multiple of %d bytes", TR::Compiler->om.objectAlignmentInBytes());
      TR_ASSERT((alignFromStart < 128), "alignment undershot should be less than 128 (124 max)");

      hotFieldsWordValue |= (((alignFromStart & 0x7f)/TR::Compiler->om.objectAlignmentInBytes()) << 1);

      //printf("Class %p, hotFieldsWordValue %p\n", opclazz,  hotFieldsWordValue);

      *(UDATA *)((char *)clazz + offsetOfHotFields()) = hotFieldsWordValue;
      }
   }


char *
TR_J9VMBase::getClassSignature_DEPRECATED(TR_OpaqueClassBlock * clazz, int32_t & length, TR_Memory * trMemory)
   {
   int32_t   numDims = 0;

   TR_OpaqueClassBlock * myClass = getBaseComponentClass(clazz, numDims);

   int32_t len;
   char * name = getClassNameChars(myClass, len);
   length = len + numDims;
   if (* name != '[')
      length += 2;

   char * sig = (char *)trMemory->allocateStackMemory(length);
   int32_t i;
   for (i = 0; i < numDims; i++)
      sig[i] = '[';
   if (* name != '[')
      sig[i++] = 'L';
   memcpy(sig+i, name, len);
   i += len;
   if (* name != '[')
      sig[i++] = ';';
   return sig;
   }


char *
TR_J9VMBase::getClassSignature(TR_OpaqueClassBlock * clazz, TR_Memory * trMemory)
   {
   int32_t   numDims = 0;

   TR_OpaqueClassBlock * myClass = getBaseComponentClass(clazz, numDims);

   int32_t len;
   char * name = getClassNameChars(myClass, len);
   int32_t length = len + numDims;
   if (* name != '[')
      length += 2;

    length++; //for null-termination
   char * sig = (char *)trMemory->allocateStackMemory(length);
   int32_t i;
   for (i = 0; i < numDims; i++)
      sig[i] = '[';
   if (* name != '[')
      sig[i++] = 'L';
   memcpy(sig+i, name, len);
   i += len;
   if (* name != '[')
      sig[i++] = ';';

   sig[length-1] = '\0';
   return sig;
   }


int32_t
TR_J9VMBase::printTruncatedSignature(char *sigBuf, int32_t bufLen, J9UTF8 *className, J9UTF8 *name, J9UTF8 *signature)
   {
   int32_t sigLen = J9UTF8_LENGTH(className) + J9UTF8_LENGTH(name) + J9UTF8_LENGTH(signature)+2;
   if (sigLen < bufLen)
      {
      sigLen = sprintf(sigBuf, "%.*s.%.*s%.*s", J9UTF8_LENGTH(className), utf8Data(className),
                       J9UTF8_LENGTH(name), utf8Data(name),
                       J9UTF8_LENGTH(signature), utf8Data(signature));
      }
   else // truncate some parts of the signature
      {
      if (sigLen - bufLen < J9UTF8_LENGTH(signature)) // classname and methodname can fit
         {
         sigLen = sprintf(sigBuf, "%.*s.%.*s%.*s", J9UTF8_LENGTH(className), utf8Data(className),
                          J9UTF8_LENGTH(name), utf8Data(name),
                          (J9UTF8_LENGTH(signature) - (sigLen-bufLen)), utf8Data(signature));
         }
      else
         {
         int32_t nameLen = std::min<int32_t>(bufLen-3, J9UTF8_LENGTH(name));
         if (nameLen == bufLen-3) // not even the method name can be printed entirely
            sigLen = sprintf(sigBuf, "*.%.*s", nameLen, utf8Data(name));
         else
            sigLen = sprintf(sigBuf, "%.*s.%.*s", std::min<int32_t>(bufLen-2 - nameLen, J9UTF8_LENGTH(className)), utf8Data(className), nameLen, utf8Data(name));
         }
      }
   return sigLen;
   }


int32_t
TR_J9VMBase::printTruncatedSignature(char *sigBuf, int32_t bufLen, TR_OpaqueMethodBlock *method)
   {
   // avoid using sampleSignature due to malloc
   J9Method *j9method = (J9Method *)method;
   J9UTF8 * className;
   J9UTF8 * name;
   J9UTF8 * signature;
   getClassNameSignatureFromMethod(j9method, className, name, signature);
   return printTruncatedSignature(sigBuf, bufLen, className, name, signature);
   }


int32_t *
TR_J9VMBase::getReferenceSlotsInClass(TR::Compilation * comp, TR_OpaqueClassBlock * classPointer)
   {
   // Get the offsets of all the reference slots in the given class as a
   // zero-terminated array of slot numbers. Return NULL if no reference slots
   // in the class.
   //
   TR_VMFieldsInfo fields(comp, TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer), 0);
   int32_t * slots = fields.getGCDescriptor();
   if (* slots == 0)
      return NULL;
   return (int32_t*)slots;
   }

bool
TR_J9VMBase::shouldPerformEDO(
      TR::Block *catchBlock,
      TR::Compilation * comp)
   {
   TR_ASSERT(catchBlock->isCatchBlock(), "shouldPerformEDO expected block_%d to be a catch block", catchBlock->getNumber());

   if (comp->getOption(TR_DisableEDO))
      {
      return false;
      }

   if (catchBlock->isOSRCatchBlock()) // Can't currently induce recompilation from an OSR block
      {
      return false;
      }

   static char *disableEDORecomp = feGetEnv("TR_disableEDORecomp");
   if (disableEDORecomp)
      return false;

   TR::Recompilation *recomp = comp->getRecompilationInfo();

   if (recomp
      && comp->getOptions()->allowRecompilation()
      && recomp->useSampling()
      && recomp->shouldBeCompiledAgain()
      && comp->getMethodHotness() < hot
      && comp->getNodeCount() < TR::Options::_catchSamplingSizeThreshold)
      {
      return true;
      }
   else
      {
      return false;
      }
   }

bool
TR_J9VMBase::isClassArray(TR_OpaqueClassBlock *klass)
   {
   return J9ROMCLASS_IS_ARRAY(TR::Compiler->cls.romClassOf(klass)) ? true : false;
   }

char *
TR_J9VMBase::getClassNameChars(TR_OpaqueClassBlock * ramClass, int32_t & length)
   {
   return utf8Data(J9ROMCLASS_CLASSNAME(TR::Compiler->cls.romClassOf(ramClass)), length);
   }

bool
TR_J9VMBase::isInlineableNativeMethod(TR::Compilation * comp, TR::ResolvedMethodSymbol * methodSymbol)
   {
   if (!comp->getOption(TR_DisableInliningOfNatives))
      switch(methodSymbol->getRecognizedMethod())
         {
         //case TR::java_lang_System_identityHashCode:
         case TR::com_ibm_oti_vm_VM_callerClass:
            // return true if these are INLs.  If they've become JNIs as a result of project clear work
            // then we don't know how to handle them.   Specifically identityHashCode is a static JNI
            // so that calls to them get an inserted first argument.  The inliner and other code has
            // trouble matching up parameters to arguments in this case.
            //
            return !methodSymbol->isJNI();
         default:
            return false;
         }

   return false;
   }

bool
TR_J9VMBase::isQueuedForVeryHotOrScorching(TR_ResolvedMethod *calleeMethod, TR::Compilation *comp)
   {
   bool isQueuedForVeryHotOrScorching = false;

   _compInfo->acquireCompMonitor(_vmThread);
   //Check again in case another thread has already upgraded this request

   for (TR_MethodToBeCompiled *cur = TR::CompilationController::getCompilationInfo()->getMethodQueue(); cur; cur = cur->_next)
      {
      if (cur->getMethodDetails().getMethod() == (J9Method*) calleeMethod->getPersistentIdentifier() && cur->getMethodDetails().isOrdinaryMethod())
         {
         isQueuedForVeryHotOrScorching = cur->_optimizationPlan->getOptLevel() >= veryHot;
         break;
         }
      }

   _compInfo->releaseCompMonitor(_vmThread);
   return isQueuedForVeryHotOrScorching;
   }

bool
TR_J9VMBase::maybeHighlyPolymorphic(TR::Compilation *comp, TR_ResolvedMethod *caller, int32_t cpIndex, TR::Method *callee, TR_OpaqueClassBlock * receiverClass)
   {
   //if (method->isInterface())
     {
      TR_OpaqueClassBlock *classOfMethod = NULL;
      if(receiverClass)
         {
         classOfMethod = receiverClass;
         }
      else
         {
         int32_t len = callee->classNameLength();
         char *s = classNameToSignature(callee->classNameChars(), len, comp);
         classOfMethod = getClassFromSignature(s, len, caller, true);
         }
      if (classOfMethod)
         {
         int len = 1;
         traceMsg(comp, "maybeHighlyPolymorphic classOfMethod: %s yizhang\n", getClassNameChars(classOfMethod, len));
         TR_PersistentCHTable *chTable = comp->getPersistentInfo()->getPersistentCHTable();
         if (chTable->hasThreeOrMoreCompiledImplementors(classOfMethod, cpIndex, caller, comp, warm))
            {
            return true;
            }
         }
      }
      return false;
   }


bool TR_J9VMBase::supressInliningRecognizedInitialCallee(TR_CallSite* callsite, TR::Compilation* comp)
   {
   static const char* OPT_DETAILS = "O^O INLINER: " ;

   TR::Node *callNode = callsite->_callNode;
   TR::TreeTop *callNodeTreeTop  = callsite->_callNodeTreeTop;

   bool dontInlineRecognizedMethod = callsite->_initialCalleeSymbol->canReplaceWithHWInstr();
   if (callNode->getSymbol()->getResolvedMethodSymbol())
      {
      // Methods we may prefer not to inline, for heuristic reasons.
      // (Methods we must not inline for correctness don't go in the next switch below.)
      //
      switch (callNode->getSymbol()->getResolvedMethodSymbol()->getRecognizedMethod())
         {
         // ByteArray Marshalling methods
         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeShort_:
         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeShortLength_:

         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeInt_:
         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeIntLength_:

         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeLong_:
         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeLongLength_:

         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeFloat_:
         case TR::com_ibm_dataaccess_ByteArrayMarshaller_writeDouble_:

         // ByteArray Unmarshalling methods
         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readShort_:
         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readShortLength_:

         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readInt_:
         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readIntLength_:

         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readLong_:
         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readLongLength_:

         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readFloat_:
         case TR::com_ibm_dataaccess_ByteArrayUnmarshaller_readDouble_:
          if (!comp->getOption(TR_DisableMarshallingIntrinsics))
             dontInlineRecognizedMethod = true;
          break;

         // DAA Packed Decimal arithmetic methods
         case TR::com_ibm_dataaccess_PackedDecimal_addPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_subtractPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_multiplyPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_dividePackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_remainderPackedDecimal_:

         // DAA Packed Decimal comparison methods
         case TR::com_ibm_dataaccess_PackedDecimal_lessThanPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_lessThanOrEqualsPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_greaterThanPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_greaterThanOrEqualsPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_equalsPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_notEqualsPackedDecimal_:

         // DAA Packed Decimal shift methods
         case TR::com_ibm_dataaccess_PackedDecimal_shiftLeftPackedDecimal_:
         case TR::com_ibm_dataaccess_PackedDecimal_shiftRightPackedDecimal_:

         // DAA Packed Decimal check method
         case TR::com_ibm_dataaccess_PackedDecimal_checkPackedDecimal_:

         // DAA Packed Decimal <-> Integer
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToInteger_:
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToInteger_ByteBuffer_:
         case TR::com_ibm_dataaccess_DecimalData_convertIntegerToPackedDecimal_:
         case TR::com_ibm_dataaccess_DecimalData_convertIntegerToPackedDecimal_ByteBuffer_:

         // DAA Packed Decimal <-> Long
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToLong_:
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToLong_ByteBuffer_:
         case TR::com_ibm_dataaccess_DecimalData_convertLongToPackedDecimal_:
         case TR::com_ibm_dataaccess_DecimalData_convertLongToPackedDecimal_ByteBuffer_:

            // DAA Packed Decimal <-> External Decimal
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToExternalDecimal_:
         case TR::com_ibm_dataaccess_DecimalData_convertExternalDecimalToPackedDecimal_:

         // DAA Packed Decimal <-> Unicode Decimal
         case TR::com_ibm_dataaccess_DecimalData_convertPackedDecimalToUnicodeDecimal_:
         case TR::com_ibm_dataaccess_DecimalData_convertUnicodeDecimalToPackedDecimal_:

         case TR::java_math_BigDecimal_noLLOverflowAdd:
         case TR::java_math_BigDecimal_noLLOverflowMul:
         case TR::java_math_BigDecimal_slowSubMulSetScale:
         case TR::java_math_BigDecimal_slowAddAddMulSetScale:
         case TR::java_math_BigDecimal_slowMulSetScale:
            if (comp->cg()->getSupportsBDLLHardwareOverflowCheck())
               dontInlineRecognizedMethod = true;
            break;
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_getAndDecrement:
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_getAndIncrement:
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_getAndAdd:
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_decrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_incrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicIntegerFieldUpdater_addAndGet:
            if (comp->cg()->getSupportsAtomicLoadAndAdd())
               dontInlineRecognizedMethod = true;
            break;
         case TR::java_math_BigDecimal_valueOf:
         case TR::java_math_BigDecimal_add:
         case TR::java_math_BigDecimal_subtract:
         case TR::java_math_BigDecimal_multiply:
            if (comp->isProfilingCompilation())
               {
               dontInlineRecognizedMethod = true;
               break;
               }
            // fall through
         case TR::java_math_BigInteger_add:
         case TR::java_math_BigInteger_subtract:
         case TR::java_math_BigInteger_multiply:
            if (callNode->getOpCode().isCallDirect())
               {
               bool dontInline = false;
               if (callNode->getReferenceCount() == 1)
                  dontInline = true;
               else if (callNode->getReferenceCount() == 2)
                  {
                  TR::TreeTop *cursor = callNodeTreeTop->getNextTreeTop();
                  while (cursor)
                     {
                     TR::Node *cursorNode = cursor->getNode();

                     if (cursorNode->getOpCodeValue() == TR::BBEnd)
                        break;

                     if (cursorNode->getOpCodeValue() == TR::treetop)
                        {
                        if (cursorNode->getFirstChild() == callNode)
                           {
                           dontInline = true;
                           break;
                           }
                        }

                     cursor = cursor->getNextTreeTop();
                     }
                  }

               if (dontInline &&
                     performTransformation(comp, "%sNot inlining dead BigDecimal/BigInteger call node [" POINTER_PRINTF_FORMAT "]\n", OPT_DETAILS, callNode))
                  dontInlineRecognizedMethod = true;
               }
            break;
         case TR::com_ibm_ws_webcontainer_channel_WCCByteBufferOutputStream_printUnencoded:
            if (comp->isServerInlining())
               {
               // Prefer arrayTranslate to kick in as often as possible
               dontInlineRecognizedMethod = true;
               }
            break;
         case TR::com_ibm_jit_JITHelpers_toUpperIntrinsicLatin1:
         case TR::com_ibm_jit_JITHelpers_toLowerIntrinsicLatin1:
         case TR::com_ibm_jit_JITHelpers_toUpperIntrinsicUTF16:
         case TR::com_ibm_jit_JITHelpers_toLowerIntrinsicUTF16:
            if(comp->cg()->getSupportsInlineStringCaseConversion())
               {
               dontInlineRecognizedMethod = true;
               }
            break;
         case TR::java_lang_StringLatin1_indexOf:
         case TR::java_lang_StringUTF16_indexOf:
         case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfStringLatin1:
         case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfStringUTF16:
         case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfLatin1:
         case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfUTF16:
            if (comp->cg()->getSupportsInlineStringIndexOf())
               {
               dontInlineRecognizedMethod = true;
               }
            break;
         case TR::java_lang_Math_max_D:
         case TR::java_lang_Math_min_D:
            if(comp->cg()->getSupportsVectorRegisters() && !comp->getOption(TR_DisableSIMDDoubleMaxMin))
               {
               dontInlineRecognizedMethod = true;
               }
            break;
         case TR::java_lang_String_hashCodeImplDecompressed:
            /*
             * X86 and z want to avoid inlining both java_lang_String_hashCodeImplDecompressed and java_lang_String_hashCodeImplCompressed
             * so they can be recognized and replaced with a custom fast implementation.
             * Power currently only has the custom fast implementation for java_lang_String_hashCodeImplDecompressed.
             * As a result, Power only wants to prevent inlining of java_lang_String_hashCodeImplDecompressed.
             * When Power gets a fast implementation of TR::java_lang_String_hashCodeImplCompressed, this case can be merged into the case
             * for java_lang_String_hashCodeImplCompressed instead of using a fallthrough.
             */
            if (!TR::Compiler->om.canGenerateArraylets() &&
                comp->target().cpu.isPower() && comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) && comp->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX) && !comp->compileRelocatableCode())
                  {
                  dontInlineRecognizedMethod = true;
                  break;
                  }
            // Intentional fallthrough here.
         case TR::java_lang_String_hashCodeImplCompressed:
            if (comp->cg()->getSupportsInlineStringHashCode())
               {
               dontInlineRecognizedMethod = true;
               }
            break;
         default:
            break;
         }

      // Methods we must not inline for correctness
      //
      switch (callNode->getSymbol()->getResolvedMethodSymbol()->getMandatoryRecognizedMethod())
         {
         case TR::java_nio_Bits_keepAlive: // This is an empty method whose only purpose is to serve as an anchored use of a given object, so it doesn't get collected
         case TR::java_lang_ref_Reference_reachabilityFence: // This is an empty method whose only purpose is to serve as an anchored use of a given object, so it doesn't get collected
         case TR::java_lang_Object_newInstancePrototype:
         //case TR::java_lang_String_init_String:
            dontInlineRecognizedMethod = true;
            break;
         default:
            break;
         }

      }

   if (dontInlineRecognizedMethod)
      {
      return true;
      }

   return false;
   }

int TR_J9VMBase::checkInlineableWithoutInitialCalleeSymbol (TR_CallSite* callsite, TR::Compilation* comp)
   {
   if (!callsite->_isInterface)
      {
      return Unresolved_Callee;
      }

   return InlineableTarget;
   }

static TR::ILOpCodes udataIndirectLoadOpCode(TR::Compilation * comp)
   {
   if (comp->target().is64Bit())
      {
      return TR::lloadi;
      }
   else
      {
      return TR::iloadi;
      }
   }

static TR::ILOpCodes udataIndirectStoreOpCode(TR::Compilation * comp)
   {
   if (comp->target().is64Bit())
      {
      return TR::lstorei;
      }
   else
      {
      return TR::istorei;
      }
   }

static TR::ILOpCodes udataConstOpCode(TR::Compilation * comp)
   {
   if (comp->target().is64Bit())
      {
      return TR::lconst;
      }
   else
      {
      return TR::iconst;
      }
   }

static TR::ILOpCodes udataLoadOpCode(TR::Compilation * comp)
   {
   if (comp->target().is64Bit())
      {
      return TR::lload;
      }
   else
      {
      return TR::iload;
      }
   }

static TR::ILOpCodes udataCmpEqOpCode(TR::Compilation * comp)
   {
   if (comp->target().is64Bit())
      {
      return TR::lcmpeq;
      }
   else
      {
      return TR::icmpeq;
      }
   }

TR::Node *
TR_J9VMBase::testAreSomeClassFlagsSet(TR::Node *j9ClassRefNode, uint32_t flagsToTest)
   {
   TR::SymbolReference *classFlagsSymRef = TR::comp()->getSymRefTab()->findOrCreateClassFlagsSymbolRef();

   TR::Node *loadClassFlags = TR::Node::createWithSymRef(TR::iloadi, 1, 1, j9ClassRefNode, classFlagsSymRef);
   TR::Node *isValueTypeNode = TR::Node::create(TR::iand, 2, loadClassFlags, TR::Node::iconst(j9ClassRefNode, flagsToTest));

   return isValueTypeNode;
   }

TR::Node *
TR_J9VMBase::testIsClassValueType(TR::Node *j9ClassRefNode)
   {
   return testAreSomeClassFlagsSet(j9ClassRefNode, J9ClassIsValueType);
   }

TR::Node *
TR_J9VMBase::checkArrayCompClassValueType(TR::Node *arrayBaseAddressNode, TR::ILOpCodes ifCmpOp)
   {
   TR::SymbolReference *vftSymRef = TR::comp()->getSymRefTab()->findOrCreateVftSymbolRef();
   TR::SymbolReference *arrayCompSymRef = TR::comp()->getSymRefTab()->findOrCreateArrayComponentTypeSymbolRef();

   TR::Node *vft = TR::Node::createWithSymRef(TR::aloadi, 1, 1, arrayBaseAddressNode, vftSymRef);
   TR::Node *arrayCompClass = TR::Node::createWithSymRef(TR::aloadi, 1, 1, vft, arrayCompSymRef);
   TR::Node *testIsValueTypeNode = testIsClassValueType(arrayCompClass);
   TR::Node *ifNode = TR::Node::createif(ifCmpOp, testIsValueTypeNode, TR::Node::iconst(arrayBaseAddressNode, 0));

   return ifNode;
   }

TR::TreeTop *
TR_J9VMBase::lowerAsyncCheck(TR::Compilation * comp, TR::Node * root, TR::TreeTop * treeTop)
   {
   // Generate the inline test as a child of the asynccheck node
   //
   TR::SymbolReference * stackOverflowSymRef =
      new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), TR::RegisterMappedSymbol::createMethodMetaDataSymbol(comp->trHeapMemory(), "stackOverflowMark"));

   stackOverflowSymRef->setOffset(offsetof(J9VMThread, stackOverflowMark));

   TR::Node * loadNode  = TR::Node::createWithSymRef(root, udataLoadOpCode(comp), 0, stackOverflowSymRef);
   TR::Node * constNode = TR::Node::create(root, udataConstOpCode(comp), 0, 0);
   if (comp->target().is64Bit())
      {
      constNode->setLongInt(-1L);
      }
   else
      constNode->setInt(-1);

   root->setAndIncChild(0, TR::Node::create(udataCmpEqOpCode(comp), 2, loadNode, constNode));

   // Insert the address of the helper as a symref into the asynccheck node
   root->setSymbolReference(comp->getSymRefTab()->findOrCreateAsyncCheckSymbolRef());
   root->setNumChildren(1);

   return treeTop;
   }

bool
TR_J9VMBase::isMethodTracingEnabled(TR_OpaqueMethodBlock *method)
   {
   return VM_VMHelpers::methodBeingTraced(_jitConfig->javaVM, (J9Method *)method);
   }

bool
TR_J9VMBase::isLambdaFormGeneratedMethod(TR_OpaqueMethodBlock *method)
   {
   return VM_VMHelpers::isLambdaFormGeneratedMethod(vmThread(), (J9Method *)method);
   }

bool
TR_J9VMBase::isLambdaFormGeneratedMethod(TR_ResolvedMethod *method)
   {
   return isLambdaFormGeneratedMethod(method->getPersistentIdentifier());
   }

bool
TR_J9VMBase::isSelectiveMethodEnterExitEnabled()
   {
   return false;
   }

bool
TR_J9VMBase::canMethodEnterEventBeHooked()
   {
   J9JavaVM * javaVM = _jitConfig->javaVM;
   J9HookInterface * * vmHooks = javaVM->internalVMFunctions->getVMHookInterface(javaVM);

   return ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_METHOD_ENTER) != 0);
   }

bool
TR_J9VMBase::canMethodExitEventBeHooked()
   {
   J9JavaVM * javaVM = _jitConfig->javaVM;
   J9HookInterface * * vmHooks = javaVM->internalVMFunctions->getVMHookInterface(javaVM);

   return ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_METHOD_RETURN) != 0);
   }

bool
TR_J9VMBase::methodsCanBeInlinedEvenIfEventHooksEnabled(TR::Compilation *comp)
   {
   return false;
   }

bool
TR_J9VMBase::canExceptionEventBeHooked()
   {
   J9JavaVM * javaVM = _jitConfig->javaVM;
   J9HookInterface * * vmHooks = javaVM->internalVMFunctions->getVMHookInterface(javaVM);

   bool catchCanBeHooked = ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_EXCEPTION_CATCH) != 0);
   bool throwCanBeHooked = ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_EXCEPTION_THROW) != 0);

   return (catchCanBeHooked || throwCanBeHooked);
   }


TR::TreeTop *
TR_J9VMBase::lowerMethodHook(TR::Compilation * comp, TR::Node * root, TR::TreeTop * treeTop)
   {
   J9Method * j9method = (J9Method *) root->getOwningMethod();
   TR::Node * ramMethod = TR::Node::aconst(root, (uintptr_t)j9method);
   ramMethod->setIsMethodPointerConstant(true);

   bool isTrace = isMethodTracingEnabled(j9method);

   TR::Node * methodCall;
   if (root->getNumChildren() == 0)
      methodCall = TR::Node::createWithSymRef(TR::call, 1, 1, ramMethod, root->getSymbolReference());
   else
      {
      TR::Node * child = root->getChild(0);
      if (isAOT_DEPRECATED_DO_NOT_USE() || (!isTrace && comp->cg()->getSupportsPartialInlineOfMethodHooks()))
         child = child->duplicateTree();

      methodCall = TR::Node::createWithSymRef(TR::call, 2, 2, child, ramMethod, root->getSymbolReference());
      root->getChild(0)->recursivelyDecReferenceCount();
      }

   if (isAOT_DEPRECATED_DO_NOT_USE())
      {
      // Add an ifcmpne branch for AOT
      TR::TreeTop *ifTree = TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, methodCall));

      TR::Node *cmp = comp->createAOTGuard(comp, methodCall->getInlinedSiteIndex(), root, 0, TR_MethodEnterExitGuard);
      TR::TreeTop *cmpTree = TR::TreeTop::create(comp, cmp);

      root->setNumChildren(0);

      TR::Block * followOnBlock = NULL;
      TR::Block *enclosingBlock = treeTop->getEnclosingBlock();
      TR::Block *remainderBlock = enclosingBlock->createConditionalBlocksBeforeTree(treeTop, cmpTree, ifTree, 0, comp->getFlowGraph());

      TR::Block *ifBlock = ifTree->getEnclosingBlock();

      bool enableDupTree = comp->getOption(TR_EnableDupRetTree);
      if ((root->getOpCodeValue() == TR::MethodExitHook) && enableDupTree)
         {
         // Remove the goto block
         TR::TransformUtil::removeTree(comp, ifBlock->getLastRealTreeTop());

         // remove the edge
         comp->getFlowGraph()->removeEdge(ifBlock, remainderBlock);

         // to create dup tree
         TR::TreeTop *dupTree = remainderBlock->getFirstRealTreeTop()->duplicateTree();

         // append the duplicated tree
         ifBlock->append(dupTree);

         TR::TreeTop *exitTree = remainderBlock->getLastRealTreeTop();
         TR::TreeTop *curTree = remainderBlock->getFirstRealTreeTop();
         TR::TreeTop *nextTree = curTree->getNextRealTreeTop();
         while (curTree != exitTree)
            {
            curTree = nextTree;
            ifBlock->append(curTree->duplicateTree());
            nextTree = curTree->getNextRealTreeTop();
            }

         // mark return block as extension of prev block
         //remainderBlock->setIsExtensionOfPreviousBlock(true);

         // do I need to add edge from ifBlock to out?  what is the Out Block?
         comp->getFlowGraph()->addEdge(ifBlock, comp->getFlowGraph()->getEnd());
         }

      if (methodCall->getNumChildren() != 0)
         {
         //enclosingBlock->getNextBlock()->setIsExtensionOfPreviousBlock();

         TR::Node *child = methodCall->getChild(0);
         if (child->getOpCodeValue() == TR::aRegLoad)
            {
            TR::Node *ifNode = ifTree->getNode();
            ifNode->setNumChildren(3);
            TR::Node *glRegDeps = enclosingBlock->getEntry()->getNode()->getChild(0);

            TR::Node *duplicateGlRegDeps = glRegDeps->duplicateTree();
            TR::Node *originalDuplicateGlRegDeps = duplicateGlRegDeps;
            duplicateGlRegDeps = TR::Node::copy(glRegDeps);
            ifNode->setChild(2, duplicateGlRegDeps);

            for (int32_t i = glRegDeps->getNumChildren() - 1; i >= 0; --i)
               {
               TR::Node * dep = glRegDeps->getChild(i);
               duplicateGlRegDeps->setAndIncChild(i, dep);
               if (dep->getGlobalRegisterNumber() == child->getGlobalRegisterNumber())
                  originalDuplicateGlRegDeps->setAndIncChild(i, child);
               }

            TR::Block *callTreeBlock = ifTree->getEnclosingBlock();
            TR::Node *bbstartNode = callTreeBlock->getEntry()->getNode();
            bbstartNode->setNumChildren(1);
            bbstartNode->setChild(0, originalDuplicateGlRegDeps);
            }
         }

      return cmpTree;
      }
   else if (!isTrace && comp->cg()->getSupportsPartialInlineOfMethodHooks())
      {
      // The method enter and exit hooks must be modified to check to see if the event is hooked
      // in the new interface rather than the old. This is a simple bit test at a known address.
      // The JIT should check the status of the J9HOOK_FLAG_HOOKED bit in the hook interface,
      // rather than the vmThread->eventFlags field.
      //
      // create
      // iand
      //    bu2i
      //      bload &vmThread()->javaVM->hookInterface->flags[J9HOOK_VM_METHOD_ENTER/J9HOOK_VM_METHOD_RETURN];
      //    iconst J9HOOK_FLAG_HOOKED
      //
      int32_t event = root->getOpCodeValue() == TR::MethodEnterHook ? J9HOOK_VM_METHOD_ENTER : J9HOOK_VM_METHOD_RETURN;
      TR::StaticSymbol * addressSym = TR::StaticSymbol::create(comp->trHeapMemory(),TR::Address);
      addressSym->setStaticAddress(getStaticHookAddress(event));

      TR::TreeTop * hookedTest =  TR::TreeTop::create(comp,
         TR::Node::createif(TR::ificmpne,
            TR::Node::create(TR::iand, 2,
               TR::Node::create(TR::bu2i, 1,
                  TR::Node::createWithSymRef(root, TR::bload, 0, new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), addressSym))),
               TR::Node::create(root, TR::iconst, 0, J9HOOK_FLAG_HOOKED)),
            TR::Node::create(root, TR::iconst, 0, 0)));

      TR::TreeTop *result = hookedTest;

      TR::TreeTop *callTree = TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, methodCall));

      root->setNumChildren(0);

      TR::Block *enclosingBlock = treeTop->getEnclosingBlock();
      if (comp->getOption(TR_EnableSelectiveEnterExitHooks))
         {
         // Mainline test is whether this method has been selected for entry/exit hooks

         TR::StaticSymbol * extendedFlagsSym = TR::StaticSymbol::create(comp->trHeapMemory(),TR::Address);
         extendedFlagsSym->setStaticAddress(fetchMethodExtendedFlagsPointer(j9method));

         TR::TreeTop * selectedTest = TR::TreeTop::create(comp,
            TR::Node::createif(TR::ificmpne,
               TR::Node::create(TR::bu2i, 1,
                  TR::Node::createWithSymRef(root, TR::bload, 0, new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), extendedFlagsSym))),
               TR::Node::create(root, TR::iconst, 0, 0)));

         result = selectedTest;

         enclosingBlock->createConditionalBlocksBeforeTree(treeTop, selectedTest, callTree, 0, comp->getFlowGraph());

         // Test of whether the hook is enabled should instead branch back to the mainline code if it's NOT enabled
         //
         TR::Block *callBlock    = callTree->getEnclosingBlock();
         TR::Block *restartBlock = selectedTest->getEnclosingBlock()->getNextBlock();
         TR::Node::recreate(hookedTest->getNode(), hookedTest->getNode()->getOpCode().getOpCodeForReverseBranch());
         hookedTest->getNode()->setBranchDestination(restartBlock->getEntry());
         callTree->insertBefore(hookedTest);
         callBlock->split(callTree, comp->getFlowGraph());
         comp->getFlowGraph()->addEdge(callBlock, enclosingBlock->getNextBlock());
         }
      else
         {
         enclosingBlock->createConditionalBlocksBeforeTree(treeTop, hookedTest, callTree, 0, comp->getFlowGraph());
         }

      if (methodCall->getNumChildren() != 0)
         {
         //enclosingBlock->getNextBlock()->setIsExtensionOfPreviousBlock();

         TR::Node *child = methodCall->getChild(0);
         if (child->getOpCodeValue() == TR::aRegLoad)
            {
            TR::Node *ifNode = hookedTest->getNode();
            ifNode->setNumChildren(3);
            TR::Node *glRegDeps = enclosingBlock->getEntry()->getNode()->getChild(0);

            TR::Node *duplicateGlRegDeps = glRegDeps->duplicateTree();
            TR::Node *originalDuplicateGlRegDeps = duplicateGlRegDeps;
            duplicateGlRegDeps = TR::Node::copy(glRegDeps);
            ifNode->setChild(2, duplicateGlRegDeps);

            for (int32_t i = glRegDeps->getNumChildren() - 1; i >= 0; --i)
               {
               TR::Node * dep = glRegDeps->getChild(i);
               duplicateGlRegDeps->setAndIncChild(i, dep);
               if (dep->getGlobalRegisterNumber() == child->getGlobalRegisterNumber())
                  originalDuplicateGlRegDeps->setAndIncChild(i, child);
               }

            TR::Block *callTreeBlock = callTree->getEnclosingBlock();
            TR::Node *bbstartNode = callTreeBlock->getEntry()->getNode();
            bbstartNode->setNumChildren(1);
            bbstartNode->setChild(0, originalDuplicateGlRegDeps);
            }
         }

      return result;
      }

   // replace mainline Hook node with straight call to the helper

   treeTop->setNode(methodCall);

   return treeTop;
   }

U_8 *
TR_J9VMBase::fetchMethodExtendedFlagsPointer(J9Method *method)
   {
   return ::fetchMethodExtendedFlagsPointer(method);
   }

void *
TR_J9VMBase::getStaticHookAddress(int32_t event)
   {
   return &vmThread()->javaVM->hookInterface.flags[event];
   }

static void lowerContiguousArrayLength(TR::Compilation *comp, TR::Node *root)
   {
   // Array size occupies 4 bytes in ALL header shapes.
   //
   TR::SymbolReferenceTable *symRefTab = comp->getSymRefTab();
   TR::Node::recreate(root, TR::iloadi);
   root->setSymbolReference(symRefTab->findOrCreateContiguousArraySizeSymbolRef());
   }

static void lowerDiscontiguousArrayLength(TR::Compilation *comp, TR::Node *root)
   {
   // Array size occupies 4 bytes in ALL header shapes.
   //
   TR::SymbolReferenceTable *symRefTab = comp->getSymRefTab();
   TR::Node::recreate(root, TR::iloadi);
   root->setSymbolReference(symRefTab->findOrCreateDiscontiguousArraySizeSymbolRef());
   }

TR::TreeTop *
TR_J9VMBase::lowerArrayLength(TR::Compilation *comp, TR::Node *root, TR::TreeTop *treeTop)
   {
   // True hybrid arraylet arraylengths must provide their own evaluator because
   // they may involve control flow or instructions that can't be represented in
   // trees.
   //
   if (!TR::Compiler->om.useHybridArraylets())
      {
      lowerContiguousArrayLength(comp, root);
      }

   return treeTop;
   }


TR::TreeTop *
TR_J9VMBase::lowerContigArrayLength(TR::Compilation *comp, TR::Node *root, TR::TreeTop *treeTop)
   {
   lowerContiguousArrayLength(comp, root);
   return treeTop;
   }


TR::TreeTop *
TR_J9VMBase::lowerMultiANewArray(TR::Compilation * comp, TR::Node * root, TR::TreeTop * treeTop)
   {
   // Get the number of dimensions
   //
   int32_t dims;
   if (root->getFirstChild()->getOpCode().isLoadConst())
      {
      dims = root->getFirstChild()->getInt();
      } //check is the const is in the literal pool
   else if ((root->getFirstChild()->getSymbolReference()!=NULL) && root->getFirstChild()->getSymbolReference()->isLiteralPoolAddress())
      {
      dims = ((TR::Node *) (root->getFirstChild()->getSymbolReference()->getOffset()))->getInt();
      }
   else
      TR_ASSERT(false, "Number of dims in multianewarray is not constant");

   bool secondDimConstNonZero = (root->getChild(2)->getOpCode().isLoadConst() && (root->getChild(2)->getInt() != 0));

   // Allocate a temp to hold the array of dimensions
   //
   TR::AutomaticSymbol * temp = TR::AutomaticSymbol::create(comp->trHeapMemory(),TR::Int32,sizeof(int32_t)*dims);
   comp->getMethodSymbol()->addAutomatic(temp);

   // Generate stores of each dimension into the array of dimensions
   // The last dimension is stored first in the array
   //
   int32_t offset = 0;
   for (int32_t i = dims; i > 0; i--)
          {
          TR::SymbolReference * symRef = new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), temp, offset);
          TR::TreeTop::create(comp, treeTop->getPrevTreeTop(),
               TR::Node::createWithSymRef(TR::istore, 1, 1, root->getChild(i),
                               symRef));
          //symRef->setStackAllocatedArrayAccess();
          root->getChild(i)->decReferenceCount();
          offset += sizeof(int32_t);
          }

   // Change the node into a call to the helper with the arguments
   //    1) Pointer to the array of dimensions
   //    2) Number of dimensions
   //    3) Class object for the element
   //
   root->setChild(2,root->getChild(dims+1));
   root->setChild(1,root->getChild(0));
   TR::Node * tempRef = TR::Node::createWithSymRef(root, TR::loadaddr,0,new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), temp));
   root->setAndIncChild(0,tempRef);
   root->setNumChildren(3);

   static bool recreateRoot = feGetEnv("TR_LowerMultiANewArrayRecreateRoot") ? true : false;
   if (!comp->target().is64Bit() || recreateRoot || dims > 2 || secondDimConstNonZero)
      TR::Node::recreate(root, TR::acall);

   return treeTop;
   }

TR::TreeTop *
TR_J9VMBase::lowerToVcall(TR::Compilation * comp, TR::Node * root, TR::TreeTop * treeTop)
   {
   // Change the TR::checkcast node into a call to the jitCheckCast helper
   //
   TR::Node::recreate(root, TR::call);
   return treeTop;
   }

TR::TreeTop *
TR_J9VMBase::lowerTree(TR::Compilation * comp, TR::Node * root, TR::TreeTop * treeTop)
   {
   switch(root->getOpCodeValue())
      {
      case TR::asynccheck:           return lowerAsyncCheck(comp, root, treeTop);
      case TR::arraylength:          return lowerArrayLength(comp, root, treeTop);
      case TR::contigarraylength:    return lowerContigArrayLength(comp, root, treeTop);
      case TR::discontigarraylength: { lowerDiscontiguousArrayLength(comp, root); return treeTop; }
      case TR::multianewarray:       return lowerMultiANewArray(comp, root, treeTop);
      case TR::athrow:               return lowerToVcall(comp, root, treeTop);
      case TR::MethodEnterHook:      return lowerMethodHook(comp, root, treeTop);
      case TR::MethodExitHook:       return lowerMethodHook(comp, root, treeTop);
      default:                        return treeTop;
      }
   return treeTop;
   }

// Call the following method either with VM access or with classUnloadMonitor in hand
// Call this method only on the frontend that is attached to the calling thread
uint8_t
TR_J9VMBase::getCompilationShouldBeInterruptedFlag()
   {
   // Some assumes to make sure that the code hasn't changed in an incompatible way
   TR::CompilationInfoPerThreadBase *cp = _compInfo->getCompInfoForCompOnAppThread();
   if (cp)
      {
      return cp->compilationShouldBeInterrupted();
      }
   else
      {
#ifdef DEBUG // make sure that what is true today stays true in the future
      J9VMThread *vmThread = _jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM);
      TR::CompilationInfoPerThread *compInfoPT = _compInfo->getCompInfoForThread(vmThread);
      TR_ASSERT(compInfoPT == _compInfoPT, "Discrepancy compInfoPT=%p _compInfoPT=%p\n", compInfoPT, _compInfoPT);
#endif
      return _compInfoPT->compilationShouldBeInterrupted();
      }
   }

// Resolution is 0.5 sec or worse. Returns negative value for not available
// Can only be used if we compile on a separate thread.
int64_t
TR_J9VMBase::getCpuTimeSpentInCompThread(TR::Compilation * comp)
   {
   if (!_compInfo->getCompInfoForCompOnAppThread()) // filter out cases when we compile on app thread
      {
      _compInfoPT->getCompThreadCPU().update();
      return _compInfoPT->getCompThreadCPU().getCpuTime();
      }
   return -1;
   }

bool
TR_J9VMBase::compilationShouldBeInterrupted(TR::Compilation * comp, TR_CallingContext callingContext)
   {
   if (comp->getUpdateCompYieldStats())
      comp->updateCompYieldStatistics(callingContext);

   bool const compilingOnApplicationThread = _compInfo->getCompInfoForCompOnAppThread() != NULL;
   TR::CompilationInfoPerThreadBase * const compInfoPTB =
      compilingOnApplicationThread ?
         _compInfo->getCompInfoForCompOnAppThread() :
         _compInfoPT;

   // Update the time spent in compilation thread
   //
   // TODO: use this only under an option
   if (!compilingOnApplicationThread) // filter out cases when we compile on app thread; another test could be
      {
      // make sure current thread is the same as the thread stored in the FrontEnd
      TR_ASSERT(vmThread() == _jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM),
                "Error: %p thread using the frontend of another thread %p",
                _jitConfig->javaVM->internalVMFunctions->currentVMThread(_jitConfig->javaVM), vmThread());
      // Update CPU time (update actually happens only every 0.5 seconds)
      if (_compInfoPT->getCompThreadCPU().update()) // returns true if an update happened and metric looks good
         {
         // We may also want to print it
         if (TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseCompilationThreads))
            {
            int32_t CPUmillis = _compInfoPT->getCompThreadCPU().getCpuTime() / 1000000;

            // May issue a trace point if enabled
            Trc_JIT_CompCPU(vmThread(), _compInfoPT->getCompThreadId(), CPUmillis);

            TR_VerboseLog::writeLineLocked(
               TR_Vlog_PERF,
               "t=%6llu CPU time spent so far in compThread:%d = %d ms",
               static_cast<unsigned long long>(_compInfo->getPersistentInfo()->getElapsedTime()),
               _compInfoPT->getCompThreadId(),
               CPUmillis
               );
            }
         }
      }

   if (comp->getOption(TR_EnableYieldVMAccess) &&
       comp->getOption(TR_DisableNoVMAccess) &&
       checkForExclusiveAcquireAccessRequest(comp))
      {
      releaseVMAccess(vmThread());

      if (!compilingOnApplicationThread && comp->getOptions()->realTimeGC())
         {
         // no compilation on application thread
         TR_ASSERT(_compInfoPT, "Missing compilation info per thread.");
         _compInfoPT->waitForGCCycleMonitor(false);
         }

      acquireVMAccessNoSuspend(vmThread());
      }

   if (compInfoPTB->compilationShouldBeInterrupted())
      return true;

   if (!comp->getOption(TR_DisableNoVMAccess))
      {
      bool exitClassUnloadMonitor = persistentMemory(_jitConfig)->getPersistentInfo()->GCwillBlockOnClassUnloadMonitor();
      if (comp->getOptions()->realTimeGC())
         {
#if defined (J9VM_GC_REALTIME)
         J9JavaVM *vm = _jitConfig->javaVM;
         exitClassUnloadMonitor = exitClassUnloadMonitor || vm->omrVM->_gcCycleOn;
#endif
         }
      if (exitClassUnloadMonitor)
         {
         // release the classUnloadMonitor and then reacquire it. This will give GC a chance to cut in.
         persistentMemory(_jitConfig)->getPersistentInfo()->resetGCwillBlockOnClassUnloadMonitor();

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
         bool hadClassUnloadMonitor = TR::MonitorTable::get()->readReleaseClassUnloadMonitor(compInfoPTB->getCompThreadId()) >= 0;
         TR_ASSERT(hadClassUnloadMonitor, "Comp thread must hold classUnloadMonitor when compiling without VMaccess");
#else // Class unloading is not possible
         bool hadClassUnloadMonitor = false;
         if (TR::Options::getCmdLineOptions()->getOption(TR_EnableHCR) || TR::Options::getCmdLineOptions()->getOption(TR_FullSpeedDebug))
            {
            hadClassUnloadMonitor = TR::MonitorTable::get()->readReleaseClassUnloadMonitor(compInfoPTB->getCompThreadId()) >= 0;
            TR_ASSERT(hadClassUnloadMonitor, "Comp thread must hold classUnloadMonitor when compiling without VMaccess");
            }
#endif
         //--- GC CAN INTERVENE HERE ---
         TR_ASSERT((vmThread()->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS) == 0, "comp thread must not have vm access");
         if (!compilingOnApplicationThread && comp->getOptions()->realTimeGC())
            {
            // no compilation on application thread
            TR_ASSERT(_compInfoPT, "Missing compilation info per thread.");
            _compInfoPT->waitForGCCycleMonitor(false);
            }

         TR::MonitorTable::get()->readAcquireClassUnloadMonitor(compInfoPTB->getCompThreadId());

         if (compInfoPTB->compilationShouldBeInterrupted())
            {
            return true;
            }
         }
      }

   return false;
   }


bool
TR_J9VMBase::checkForExclusiveAcquireAccessRequest(TR::Compilation *)
   {
   if (vmThread()->publicFlags & J9_PUBLIC_FLAGS_HALT_THREAD_EXCLUSIVE)
      return true;

   return false;
   }


bool
TR_J9VMBase::haveAccess()
   {
   if (vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS)
      return true;
   return false;
   }

bool
TR_J9VMBase::haveAccess(TR::Compilation * comp)
   {
   if (!comp->getOption(TR_DisableNoVMAccess))
      {
      if (vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS)
         return true;
      return false;
      }
   return true;
   }


void
TR_J9VMBase::releaseAccess(TR::Compilation * comp)
   {
   if (!comp->getOption(TR_DisableNoVMAccess))
      {
      if (vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS)
         releaseVMAccess(vmThread());
      }
   }

bool
TR_J9VMBase::tryToAcquireAccess(TR::Compilation * comp, bool *haveAcquiredVMAccess)
   {
   bool hasVMAccess;
   *haveAcquiredVMAccess = false;

#if defined(J9VM_OPT_JITSERVER)
   // JITServer TODO: For now, we always take the "safe path" on the server
   if (comp->isOutOfProcessCompilation())
      return false;
#endif /* defined(J9VM_OPT_JITSERVER) */

   if (!comp->getOption(TR_DisableNoVMAccess))
      {
      if (!(vmThread()->publicFlags &  J9_PUBLIC_FLAGS_VM_ACCESS))
         {
         if (!vmThread()->javaVM->internalVMFunctions->internalTryAcquireVMAccessWithMask(vmThread(), J9_PUBLIC_FLAGS_HALT_THREAD_ANY_NO_JAVA_SUSPEND))
            {
            hasVMAccess = true;
            *haveAcquiredVMAccess = true;
            }
         else
            {
            hasVMAccess = false;
            }
         }
      else
         {
         hasVMAccess = true;
         }
      }
   else
      {
      hasVMAccess = true;
      }


   if (!hasVMAccess)
      {
      traceMsg(comp, "tryToAcquireAccess couldn't acquire vm access");
      }
   return hasVMAccess;
   }


// The following is called from TR::Options::jitLatePostProcess
bool
TR_J9VMBase::compileMethods(TR::OptionSet *optionSet, void *config)
   {
   J9JITConfig *jitConfig = (J9JITConfig*)config;
   TR_ASSERT(optionSet != NULL && jitConfig != NULL, "Invalid optionSet or jitConfig passed in");

   TR_Debug * debug = NULL;
   if (!(debug = TR::Options::getDebug()))
      {
      TR::Options::createDebug();
      debug = TR::Options::getDebug();
      if (!debug)
         {
         return false;
         }
      }

   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   J9JavaVM *javaVM = jitConfig->javaVM;
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   J9InternalVMFunctions * intFunc = javaVM->internalVMFunctions;
   J9VMThread *vmThread = intFunc->currentVMThread(javaVM);

   J9Method * newInstanceThunk = NULL;

   int   maxMethodNameLen = 2048;
   char* fullMethodName   = (char*)j9mem_allocate_memory(maxMethodNameLen, J9MEM_CATEGORY_JIT);
   if (!fullMethodName)
      {
      return false;
      }

   TR::SimpleRegex * regex = optionSet->getMethodRegex();

   compInfo->debugPrint(vmThread, "\tcompile methods entering monitor before compile\n");
   compInfo->getCompilationMonitor()->enter();
   compInfo->debugPrint(vmThread, "+CM\n");

   J9ClassWalkState classWalkState;
   J9Class * clazz = javaVM->internalVMFunctions->allLiveClassesStartDo(&classWalkState, javaVM, NULL);
   while (clazz)
      {
      if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(clazz->romClass))
         {
         if (newInstanceThunk == NULL)
            {
            // this is done inside the loop so that we can survive an early call
            // to this routine before the new instance prototype has been established
            // (and there are no classes loaded) from cmd line processing
            newInstanceThunk = getNewInstancePrototype(vmThread);
            }

         J9ROMMethod * romMethod = (J9ROMMethod *) J9ROMCLASS_ROMMETHODS(clazz->romClass);
         J9Method * ramMethods = (J9Method *) (clazz->ramMethods);
         for (uint32_t m = 0; m < clazz->romClass->romMethodCount; m++)
            {
            J9Method * method = &ramMethods[m];

            if (!(romMethod->modifiers & (J9AccNative | J9AccAbstract))
                 && method != newInstanceThunk &&
                 !TR::CompilationInfo::isCompiled(method))
               {
               J9UTF8 *className;
               J9UTF8 *name;
               J9UTF8 *signature;
               getClassNameSignatureFromMethod(method, className, name, signature);
               if (J9UTF8_LENGTH(className) + J9UTF8_LENGTH(name) + J9UTF8_LENGTH(signature) + 1 > maxMethodNameLen)
                  {
                  maxMethodNameLen = J9UTF8_LENGTH(className) + J9UTF8_LENGTH(name) + J9UTF8_LENGTH(signature) + 1;
                  j9mem_free_memory(fullMethodName);
                  fullMethodName   = (char*)j9mem_allocate_memory(maxMethodNameLen, J9MEM_CATEGORY_JIT);
                  if (!fullMethodName)
                     {
                     break;
                     }
                  }

               sprintf(fullMethodName, "%.*s.%.*s%.*s",
                  J9UTF8_LENGTH(className), J9UTF8_DATA(className),
                  J9UTF8_LENGTH(name), J9UTF8_DATA(name),
                  J9UTF8_LENGTH(signature), J9UTF8_DATA(signature));

               if (TR::SimpleRegex::match(regex, fullMethodName))
                  {
                  bool queued = false;
                  TR_MethodEvent event;
                  event._eventType = TR_MethodEvent::InterpreterCounterTripped;
                  event._j9method = method;
                  event._oldStartPC = 0;
                  event._vmThread = vmThread;
                  event._classNeedingThunk = 0;
                  bool newPlanCreated;
                  TR_OptimizationPlan *plan = TR::CompilationController::getCompilationStrategy()->processEvent(&event, &newPlanCreated);
                  // If the controller decides to compile this method, trigger the compilation and wait here
                  if (plan)
                     {
                     TR::IlGeneratorMethodDetails details(method);
                     IDATA result = (IDATA)compInfo->compileMethod(vmThread, details, 0, TR_yes, NULL, &queued, plan);

                     if (!queued && newPlanCreated)
                        TR_OptimizationPlan::freeOptimizationPlan(plan);
                     }
                  else // OOM
                     {
                     break; // OOM, so abort all other compilations
                     }
                  }
               }

            romMethod = nextROMMethod(romMethod);
            }
         }
      clazz = javaVM->internalVMFunctions->allLiveClassesNextDo(&classWalkState);
      }

   javaVM->internalVMFunctions->allLiveClassesEndDo(&classWalkState);

   compInfo->debugPrint(vmThread, "\tcompile methods releasing monitor before compile\n");
   compInfo->debugPrint(vmThread, "-CM\n");
   compInfo->getCompilationMonitor()->exit();

   if (fullMethodName)
      {
      j9mem_free_memory(fullMethodName);
      }

   return true;
   }

// The following is called from TR::Options::jitLatePostProcess
void
TR_J9VMBase::waitOnCompiler(void *config)
   {
   J9JITConfig *jitConfig = (J9JITConfig*)config;
   // There is nothing to wait for if the compilations are not performed asynchronous on separate thread
   if (!isAsyncCompilation())
      return;
   if (!_compInfo)
      return;
   if (_compInfo->getNumCompThreadsActive() == 0)
      return;
  J9JavaVM   *vm        = jitConfig->javaVM;
  J9VMThread *vmContext = vm->internalVMFunctions->currentVMThread(vm);
   releaseVMAccess(vmContext);  // Release VM Access before going to sleep
   _compInfo->acquireCompilationLock();
   while (_compInfo->peekNextMethodToBeCompiled())
      _compInfo->getCompilationMonitor()->wait();
   _compInfo->releaseCompilationLock();
   acquireVMAccess(vmContext); // Reacquire VM access. This is a java thread so it can get suspended
   }

bool
TR_J9VMBase::tossingCode()
   {
   if ((_jitConfig->runtimeFlags & J9JIT_TOSS_CODE) ||
       (_jitConfig->runtimeFlags & J9JIT_TESTMODE))
      return true;
   return false;
   }

TR::KnownObjectTable::Index
TR_J9VMBase::getCompiledMethodReceiverKnownObjectIndex(TR::Compilation *comp)
   {
   TR::KnownObjectTable *knot = comp->getOrCreateKnownObjectTable();
   if (knot)
      {
      TR::IlGeneratorMethodDetails & details = comp->ilGenRequest().details();
      if (details.isMethodHandleThunk())
         {
         J9::MethodHandleThunkDetails & thunkDetails = static_cast<J9::MethodHandleThunkDetails &>(details);
         if (thunkDetails.isCustom())
            {
            return knot->getOrCreateIndexAt(thunkDetails.getHandleRef());
            }
         }
      }

   return TR::KnownObjectTable::UNKNOWN;
   }

bool
TR_J9VMBase::methodMayHaveBeenInterpreted(TR::Compilation *comp)
   {
   if (comp->ilGenRequest().details().isMethodHandleThunk())
      return false;
   else
      {
      int32_t initialCount = comp->mayHaveLoops() ? comp->getOptions()->getInitialBCount() :
                                                    comp->getOptions()->getInitialCount();
      if (initialCount == 0)
         return false;
      }

   return true;
   }

bool
TR_J9VMBase::canRecompileMethodWithMatchingPersistentMethodInfo(TR::Compilation *comp)
   {
   return (comp->ilGenRequest().details().isJitDumpMethod() || // for a log recompilation, it's okay to compile at the same level
           comp->getOption(TR_EnableHCR)
          );                     // TODO: Why does this assume sometimes fail in HCR mode?
   }


//
// A few predicates describing shadow symbols that we can reason about at
// compile time.  Note that "final field" here doesn't rule out a pointer to a
// Java object, as long as it always points at the same object.
//
// {{{
//

static bool isFinalFieldOfNativeStruct(TR::SymbolReference *symRef, TR::Compilation *comp)
   {
   switch (symRef->getReferenceNumber() - comp->getSymRefTab()->getNumHelperSymbols())
      {
      case TR::SymbolReferenceTable::componentClassSymbol:
      case TR::SymbolReferenceTable::arrayClassRomPtrSymbol:
      case TR::SymbolReferenceTable::indexableSizeSymbol:
      case TR::SymbolReferenceTable::isArraySymbol:
      case TR::SymbolReferenceTable::classRomPtrSymbol:
      case TR::SymbolReferenceTable::ramStaticsFromClassSymbol:
         TR_ASSERT(symRef->getSymbol()->isShadow(), "isFinalFieldOfNativeStruct expected shadow symbol");
         return true;
      default:
         return false;
      }
   }

static bool isFinalFieldPointingAtNativeStruct(TR::SymbolReference *symRef, TR::Compilation *comp)
   {
   switch (symRef->getReferenceNumber() - comp->getSymRefTab()->getNumHelperSymbols())
      {
      case TR::SymbolReferenceTable::componentClassSymbol:
      case TR::SymbolReferenceTable::arrayClassRomPtrSymbol:
      case TR::SymbolReferenceTable::classRomPtrSymbol:
      case TR::SymbolReferenceTable::classFromJavaLangClassSymbol:
      case TR::SymbolReferenceTable::classFromJavaLangClassAsPrimitiveSymbol:
      case TR::SymbolReferenceTable::ramStaticsFromClassSymbol:
      case TR::SymbolReferenceTable::vftSymbol:
         TR_ASSERT(symRef->getSymbol()->isShadow(), "isFinalFieldPointingAtNativeStruct expected shadow symbol");
         return true;
      default:
         return false;
      }
   }


bool TR_J9VMBase::isFinalFieldPointingAtJ9Class(TR::SymbolReference *symRef, TR::Compilation *comp)
   {
   switch (symRef->getReferenceNumber() - comp->getSymRefTab()->getNumHelperSymbols())
      {
      case TR::SymbolReferenceTable::componentClassSymbol:
      case TR::SymbolReferenceTable::classFromJavaLangClassSymbol:
      case TR::SymbolReferenceTable::classFromJavaLangClassAsPrimitiveSymbol:
      case TR::SymbolReferenceTable::vftSymbol:
         TR_ASSERT(symRef->getSymbol()->isShadow(), "isFinalFieldPointingAtJ9Class expected shadow symbol");
         return true;
      default:
         return false;
      }
   }

// }}}  (end of predicates)

bool
TR_J9VMBase::canDereferenceAtCompileTimeWithFieldSymbol(TR::Symbol * fieldSymbol, int32_t cpIndex, TR_ResolvedMethod *owningMethod)
   {
   TR::Compilation *comp = TR::comp();
   switch (fieldSymbol->getRecognizedField())
      {
      case TR::Symbol::Java_lang_invoke_PrimitiveHandle_rawModifiers:
      case TR::Symbol::Java_lang_invoke_PrimitiveHandle_defc:
      case TR::Symbol::Java_lang_invoke_VarHandle_handleTable:
      case TR::Symbol::Java_lang_invoke_MethodHandleImpl_LoopClauses_clauses:
         {
         return true;
         }
      default:
         {
         if (!fieldSymbol->isFinal())
            return false;

         // Sadly, it's common for deserialization-like code to strip final
         // specifiers off instance fields so they can be filled in during
         // deserialization.  To support these shenanigans, we must restrict
         // ourselves to fold instance fields only in classes where
         // this is known to be safe.

         const char* name;
         int32_t len;

         // Get class name for fabricated java field
         if (cpIndex < 0 &&
             fieldSymbol->getRecognizedField() != TR::Symbol::UnknownField)
            {
            name = fieldSymbol->owningClassNameCharsForRecognizedField(len);
            }
         else
            {
            TR_OpaqueClassBlock *fieldClass = owningMethod->getClassFromFieldOrStatic(comp, cpIndex);
            if (!fieldClass)
               return false;

            name = getClassNameChars((TR_OpaqueClassBlock*)fieldClass, len);
            }

         bool isStatic = false;
         TR_OpaqueClassBlock *clazz = NULL; // only used for static fields
         return TR::TransformUtil::foldFinalFieldsIn(clazz, name, len, isStatic, comp);
         }
      }
   return false;
   }

bool
TR_J9VMBase::canDereferenceAtCompileTime(TR::SymbolReference *fieldRef, TR::Compilation *comp)
   {
   // Note that this query only looks at the field shadow symref; it says
   // nothing about the underlying object in which fieldRef is located.  For
   // example, if the field is a java field, this can return true, yet the
   // compiler still can't dereference the field unless the underlying object
   // is known to have finished initialization.
   //
   if (fieldRef->isUnresolved())
      return false;
   if (comp->getSymRefTab()->isImmutableArrayShadow(fieldRef))
      return true;
   if (fieldRef->getSymbol()->isShadow())
      {
      if (fieldRef->getReferenceNumber() < comp->getSymRefTab()->getNumPredefinedSymbols())
         {
         return isFinalFieldOfNativeStruct(fieldRef, comp) || isFinalFieldPointingAtNativeStruct(fieldRef, comp);
         }
      else return canDereferenceAtCompileTimeWithFieldSymbol(fieldRef->getSymbol(), fieldRef->getCPIndex(), fieldRef->getOwningMethodSymbol(comp)->getResolvedMethod());
      }
   else
      return false;
   }


// Creates a node to initialize the local object flags field
//
TR::Node *
TR_J9VMBase::initializeLocalObjectFlags(TR::Compilation * comp, TR::Node * allocationNode, TR_OpaqueClassBlock * ramClass)
   {
   TR::VMAccessCriticalSection initializeLocalObjectFlags(this);

#if defined(J9VM_INTERP_FLAGS_IN_CLASS_SLOT)
   int32_t initValue = 0;
#else
   int32_t initValue = TR::Compiler->cls.romClassOf(ramClass)->instanceShape;
#endif

   if (!comp->getOptions()->realTimeGC())
      {
      initValue |= vmThread()->allocateThreadLocalHeap.objectFlags;
      }

   TR::Node * result;
   result = TR::Node::create(allocationNode, TR::iconst, 0, initValue);

   return result;
   }

bool TR_J9VMBase::hasTwoWordObjectHeader()
  {
  return true;
  }

// Create trees to initialize the header of an object that is being created
// on the stack.
//
void
TR_J9VMBase::initializeLocalObjectHeader(TR::Compilation * comp, TR::Node * allocationNode, TR::TreeTop * allocationTreeTop)
   {
   TR::VMAccessCriticalSection initializeLocalObjectHeader(this);
   TR::TreeTop * prevTree = allocationTreeTop;

   TR::Node             * classNode = allocationNode->getFirstChild();
   TR::StaticSymbol     * classSym  = classNode->getSymbol()->castToStaticSymbol();
   TR_OpaqueClassBlock * ramClass  = (TR_OpaqueClassBlock *) classSym->getStaticAddress();

   prevTree = initializeClazzFlagsMonitorFields(comp, prevTree, allocationNode, classNode, ramClass);
   }

// Create trees to initialize the header of an array that is being allocated on the stack
// works for newarray and anewarray allocations
//
void
TR_J9VMBase::initializeLocalArrayHeader(TR::Compilation * comp, TR::Node * allocationNode, TR::TreeTop * allocationTreeTop)
   {
   /*
   TR_ASSERT(!comp->compileRelocatableCode(), "We create a class symbol with a CPI of -1!\n"
                    "Enabling recognized methods might trigger this code to be executed in AOT!\n"
                    "We need to figure out how to validate and relocate this symbol safely before removing this assertion!\n");
   */
   TR::TreeTop * prevTree = allocationTreeTop;
   TR::ILOpCodes kind = allocationNode->getOpCodeValue();
   TR_OpaqueClassBlock * ramClass = 0;

   switch (kind)
      {
      case TR::newarray:
         {
         TR_ASSERT(allocationNode->getSecondChild()->getOpCode().isLoadConst(), "Expecting const child \n");
         int32_t arrayClassIndex = allocationNode->getSecondChild()->getInt();

         ramClass = getClassFromNewArrayTypeNonNull(arrayClassIndex);
         }
         break;

      case TR::anewarray:
         {
         TR::Node            * classRef    = allocationNode->getSecondChild();
         TR::SymbolReference * classSymRef = classRef->getSymbolReference();
         TR::StaticSymbol    * classSym    = classSymRef->getSymbol()->getStaticSymbol();
         TR_ASSERT(!classSymRef->isUnresolved(), "Cannot allocate an array with unresolved base class");
         TR_OpaqueClassBlock* clazz = (TR_OpaqueClassBlock*)classSym->getStaticAddress();
         ramClass = getArrayClassFromComponentClass(clazz);
         }
         break;

      default:
         TR_ASSERT(0, "Expecting TR::newarray or TR::anewarray opcodes only");
      }


   J9ROMClass * romClass = TR::Compiler->cls.romClassOf(ramClass);
   TR::Node *classNode = TR::Node::createWithSymRef(allocationNode, TR::loadaddr, 0, comp->getSymRefTab()->findOrCreateClassSymbol(comp->getMethodSymbol(), -1, ramClass));

   prevTree = initializeClazzFlagsMonitorFields(comp, prevTree, allocationNode, classNode, ramClass);

   // -----------------------------------------------------------------------------------
   // Initialize the size field
   // -----------------------------------------------------------------------------------

   int32_t elementSize = TR::Compiler->om.getSizeOfArrayElement(allocationNode);
   TR_ASSERT(allocationNode->getFirstChild()->getOpCode().isLoadConst(), "Expecting const child \n");

   int32_t instanceSize = allocationNode->getFirstChild()->getInt();

   TR::SymbolReference *arraySizeSymRef;
   if (TR::Compiler->om.canGenerateArraylets() && TR::Compiler->om.useHybridArraylets() && TR::Compiler->om.isDiscontiguousArray(instanceSize))
      {
      TR_ASSERT(instanceSize == 0, "arbitrary discontiguous stack allocated objects not supported yet");

      // Contiguous size field is zero (mandatory)
      //
      TR::Node* node = TR::Node::create(allocationNode, TR::iconst, 0, instanceSize);
      arraySizeSymRef = comp->getSymRefTab()->findOrCreateContiguousArraySizeSymbolRef();
      node = TR::Node::createWithSymRef(TR::istorei, 2, 2, allocationNode, node, arraySizeSymRef);
      prevTree = TR::TreeTop::create(comp, prevTree, node);

      arraySizeSymRef = comp->getSymRefTab()->findOrCreateDiscontiguousArraySizeSymbolRef();
      }
#if defined(TR_HOST_S390) && defined(TR_TARGET_S390)
   //TODO remove define s390 flags when x and power enable support for inlining 0 size arrays
   // clean up canGenerateArraylets() && TR::Compiler->om.useHybridArraylets() && TR::Compiler->om.isDiscontiguousArray(instanceSize) queries?
   else if (!comp->getOptions()->realTimeGC() && instanceSize == 0)
      {
      // Contiguous size field is zero (mandatory)
      // For J9VM_GC_COMBINATION_SPEC only 0 size discontiguous arrays are supported
      TR::Node* node = TR::Node::create(allocationNode, TR::iconst, 0, instanceSize);
      arraySizeSymRef = comp->getSymRefTab()->findOrCreateContiguousArraySizeSymbolRef();
      node = TR::Node::createWithSymRef(TR::istorei, 2, 2, allocationNode, node, arraySizeSymRef);
      prevTree = TR::TreeTop::create(comp, prevTree, node);

      arraySizeSymRef = comp->getSymRefTab()->findOrCreateDiscontiguousArraySizeSymbolRef();
      }
#endif
   else
      {
      arraySizeSymRef = comp->getSymRefTab()->findOrCreateContiguousArraySizeSymbolRef();
      }

   TR::Node* node = TR::Node::create(allocationNode, TR::iconst, 0, instanceSize);
   node = TR::Node::createWithSymRef(TR::istorei, 2, 2, allocationNode, node, arraySizeSymRef);
   prevTree = TR::TreeTop::create(comp, prevTree, node);
   }


TR::TreeTop* TR_J9VMBase::initializeClazzFlagsMonitorFields(TR::Compilation* comp, TR::TreeTop* prevTree,
   TR::Node* allocationNode, TR::Node* classNode, TR_OpaqueClassBlock* ramClass)
   {
   // -----------------------------------------------------------------------------------
   // Initialize the clazz field
   // -----------------------------------------------------------------------------------
   TR::Node* node;
#if !defined(J9VM_INTERP_FLAGS_IN_CLASS_SLOT)
   node = TR::Node::createWithSymRef(TR::astorei, 2, 2, allocationNode, classNode,
      comp->getSymRefTab()->findOrCreateVftSymbolRef());

   prevTree = TR::TreeTop::create(comp, prevTree, node);
#endif

   // -----------------------------------------------------------------------------------
   // Initialize the flags field
   // -----------------------------------------------------------------------------------

   node = initializeLocalObjectFlags(comp, allocationNode, ramClass);

#if defined(J9VM_INTERP_FLAGS_IN_CLASS_SLOT)
   node = TR::Node::create(TR::aiadd, 2, classNode, node);
   node = TR::Node::createWithSymRef(TR::astorei, 2, 2, allocationNode, node,
                          comp->getSymRefTab()->findOrCreateVftSymbolRef());
#else
   node = TR::Node::createWithSymRef(TR::istorei, 2, 2, allocationNode, node,
                          comp->getSymRefTab()->findOrCreateHeaderFlagsSymbolRef());
#endif

   prevTree = TR::TreeTop::create(comp, prevTree, node);

   // -----------------------------------------------------------------------------------
   // Initialize the monitor field
   // -----------------------------------------------------------------------------------

   int32_t lwOffset = getByteOffsetToLockword(ramClass);
   if (lwOffset > 0)
      {
      // Initialize the monitor field
      //
      int32_t lwInitialValue = getInitialLockword(ramClass);

      if (!comp->target().is64Bit() || generateCompressedLockWord())
         {
         node = TR::Node::iconst(allocationNode, lwInitialValue);
         node = TR::Node::createWithSymRef(TR::istorei, 2, 2, allocationNode, node,
            comp->getSymRefTab()->findOrCreateGenericIntNonArrayShadowSymbolReference(lwOffset));
         }
      else
         {
         node = TR::Node::lconst(allocationNode, lwInitialValue);
         node = TR::Node::createWithSymRef(TR::lstorei, 2, 2, allocationNode, node,
            comp->getSymRefTab()->findOrCreateGenericIntNonArrayShadowSymbolReference(lwOffset));
         }
      prevTree = TR::TreeTop::create(comp, prevTree, node);
      }
   return prevTree;
   }

bool
TR_J9VMBase::tlhHasBeenCleared()
   {
#if defined(J9VM_GC_BATCH_CLEAR_TLH)
   TR::VMAccessCriticalSection tlhHasBeenCleared(this);
   J9JavaVM * jvm = _jitConfig->javaVM;
   bool result = jvm->memoryManagerFunctions->isAllocateZeroedTLHPagesEnabled(jvm);
   return result;
#else
   return false;
#endif
   }

bool
TR_J9VMBase::isStaticObjectFlags()
   {
   TR::VMAccessCriticalSection isStaticObjectFlags(this);
   J9JavaVM * jvm = _jitConfig->javaVM;
   bool result = jvm->memoryManagerFunctions->isStaticObjectAllocateFlags(jvm) != 0;
   return result;
   }

uint32_t
TR_J9VMBase::getStaticObjectFlags()
   {
   TR::VMAccessCriticalSection getStaticObjectFlags(this);
   uint32_t staticFlag;
   TR_ASSERT(isStaticObjectFlags(), "isStaticObjectFlags must be true to invoke getStaticObjectFlags");
   J9JavaVM * jvm = _jitConfig->javaVM;
   staticFlag = (uint32_t)jvm->memoryManagerFunctions->getStaticObjectAllocateFlags(jvm);
   return staticFlag;
   }

uintptr_t
TR_J9VMBase::getOverflowSafeAllocSize()
   {
   TR::VMAccessCriticalSection getOverflowSafeAllocSize(this);
   J9JavaVM *jvm = _jitConfig->javaVM;
   uintptr_t result = jvm->memoryManagerFunctions->j9gc_get_overflow_safe_alloc_size(jvm);
   return result;
   }

void
TR_J9VMBase::unsupportedByteCode(TR::Compilation * comp, U_8 opcode)
   {
   char errMsg[40];
   snprintf(errMsg, 40, "bytecode %d not supported by JIT", opcode);
   comp->failCompilation<TR::CompilationException>(errMsg);
   }

void
TR_J9VMBase::unknownByteCode(TR::Compilation * comp, U_8 opcode)
   {
   TR_ASSERT_FATAL(0, "Unknown bytecode to JIT %d \n", opcode);
   }

char*
TR_J9VMBase::printAdditionalInfoOnAssertionFailure(TR::Compilation *comp)
   {
   char *c = (char *)comp->trMemory()->allocateHeapMemory(20);

   sprintf(c, "VMState: %#010" OMR_PRIxPTR, vmThread()->omrVMThread->vmState);

   return c;
   }


#if defined(TR_TARGET_X86) && !defined(J9HAMMER)
extern "C" void * jitExitInterpreterX; /* SSE float */
extern "C" void * jitExitInterpreterY; /* SSE double */
#endif // defined(TR_TARGET_X86) && !defined(J9HAMMER)


J9VMThread * getJ9VMThreadFromTR_VM(void * fe)
   {
   return ((TR_J9VMBase *)fe)->_vmThread;
   }

J9JITConfig * getJ9JitConfigFromFE(void * fe)
   {
   return ((TR_J9VMBase *)fe)->getJ9JITConfig();
   }

void *
TR_J9VMBase::setJ2IThunk(TR::Method *method, void *thunkptr, TR::Compilation *comp)
   {
   return setJ2IThunk(method->signatureChars(), method->signatureLength(), thunkptr, comp);
   }

void *
TR_J9VMBase::getJ2IThunk(TR::Method *method, TR::Compilation *comp)
   {
   return getJ2IThunk(method->signatureChars(), method->signatureLength(), comp);
   }

void *
TR_J9VMBase::getJ2IThunk(char *signatureChars, uint32_t signatureLength, TR::Compilation *comp)
   {
   TR::VMAccessCriticalSection getJ2IThunk(this);
   void * result = j9ThunkLookupSignature(_jitConfig, signatureLength, signatureChars);
   return result;
   }

void *
TR_J9VMBase::setJ2IThunk(char *signatureChars, uint32_t signatureLength, void *thunkptr, TR::Compilation *comp)
   {
   TR::VMAccessCriticalSection setJ2IThunk(this);

   if (j9ThunkNewSignature(_jitConfig, signatureLength, signatureChars, thunkptr))
      {
      comp->failCompilation<TR::CompilationException>("J9Thunk new signature");
      }

#define THUNK_NAME "JIT virtual thunk"

   if (J9_EVENT_IS_HOOKED(jitConfig->javaVM->hookInterface, J9HOOK_VM_DYNAMIC_CODE_LOAD) && !comp->compileRelocatableCode())
      ALWAYS_TRIGGER_J9HOOK_VM_DYNAMIC_CODE_LOAD(jitConfig->javaVM->hookInterface, jitConfig->javaVM->internalVMFunctions->currentVMThread(jitConfig->javaVM), NULL, (void *) thunkptr, *((uint32_t *)thunkptr - 2), THUNK_NAME, NULL);
#ifdef LINUX
   if (TR::CompilationInfoPerThreadBase::getPerfFile())
      j9jit_fprintf(TR::CompilationInfoPerThreadBase::getPerfFile(), "%p %lX %s\n", thunkptr, *((uint32_t *)thunkptr - 2), THUNK_NAME);
#endif

   return thunkptr;
   }

void
TR_J9VMBase::setInvokeExactJ2IThunk(void *thunkptr, TR::Compilation *comp)
   {
   comp->getPersistentInfo()->getInvokeExactJ2IThunkTable()->addThunk((TR_J2IThunk*) thunkptr, this);
   }

void *
TR_J9VMBase::findPersistentJ2IThunk(char *signatureChars)
   {
   return findPersistentThunk(signatureChars, strlen(signatureChars));
   }

void *
TR_J9VMBase::findPersistentThunk(char *signatureChars, uint32_t signatureLength)
   {
#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) && defined(J9VM_OPT_SHARED_CLASSES) && (defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_S390) || defined(TR_HOST_ARM) || defined(TR_HOST_ARM64))
   J9SharedDataDescriptor firstDescriptor;
   J9VMThread *curThread = getCurrentVMThread();
   firstDescriptor.address = NULL;

   _jitConfig->javaVM->sharedClassConfig->findSharedData(curThread, signatureChars, signatureLength,
                                                         J9SHR_DATA_TYPE_AOTTHUNK, false, &firstDescriptor, NULL);
   return firstDescriptor.address;
#else
   return NULL;
#endif
   }

void *
TR_J9VMBase::persistJ2IThunk(void *thunk)
   {
   return NULL;  // only needed for AOT compilations
   }

static char *
getJ2IThunkSignature(char *invokeHandleSignature, uint32_t signatureLength, int argsToSkip, char *description, TR::Compilation *comp)
   {
   char *argsToCopy;
   for (argsToCopy = invokeHandleSignature+1; argsToSkip > 0; argsToSkip--)
      argsToCopy = nextSignatureArgument(argsToCopy);
   uint32_t lengthToCopy = signatureLength - (argsToCopy - invokeHandleSignature);

   char *resultBuf = (char*)comp->trMemory()->allocateMemory(2+lengthToCopy, stackAlloc);
   sprintf(resultBuf, "(%.*s", lengthToCopy, argsToCopy);

   if (comp->getOption(TR_TraceCG))
      traceMsg(comp, "JSR292: j2i-thunk signature for %s of '%.*s' is '%s'\n", description, signatureLength, invokeHandleSignature, resultBuf);
   return resultBuf;
   }

static TR::Node *
getEquivalentVirtualCallNode(TR::Node *callNode, int argsToSkip, char *description, TR::Compilation *comp)
   {
   TR::Node *j2iThunkCall = TR::Node::createWithSymRef(callNode, callNode->getOpCodeValue(), callNode->getNumChildren() - argsToSkip + 1, callNode->getSymbolReference());
   j2iThunkCall->setChild(0, callNode->getFirstChild()); // first child should be vft pointer but we don't have one
   for (int32_t i = argsToSkip; i < callNode->getNumChildren(); i++) // Skip target address, vtable index
      j2iThunkCall->setChild(i-argsToSkip+1, callNode->getChild(i));
   if (comp->getOption(TR_TraceCG))
      {
      traceMsg(comp, "JSR292: j2i-thunk call node for %s is %p:\n", description, j2iThunkCall);
      comp->getDebug()->print(comp->getOutFile(), j2iThunkCall, 2, true);
      }
   return j2iThunkCall;
   }

char *
TR_J9VMBase::getJ2IThunkSignatureForDispatchVirtual(char *invokeHandleSignature, uint32_t signatureLength, TR::Compilation *comp)
   {
   // Skip target address, vtable index, eventual receiver
   return getJ2IThunkSignature(invokeHandleSignature, signatureLength, 3, "dispatchVirtual", comp);
   }

TR::Node *
TR_J9VMBase::getEquivalentVirtualCallNodeForDispatchVirtual(TR::Node *callNode, TR::Compilation *comp)
   {
   // Skip target address, vtable index, but leave the ultimate receiver
   return getEquivalentVirtualCallNode(callNode, 2, "dispatchVirtual", comp);
   }

bool
TR_J9VMBase::needsInvokeExactJ2IThunk(TR::Node *callNode, TR::Compilation *comp)
   {
   TR_ASSERT(callNode->getOpCode().isCall(), "needsInvokeExactJ2IThunk expects call node; found %s", callNode->getOpCode().getName());

   TR::MethodSymbol *methodSymbol = callNode->getSymbol()->castToMethodSymbol();
   TR::Method       *method       = methodSymbol->getMethod();
   if (  methodSymbol->isComputed()
      && (  method->getMandatoryRecognizedMethod() == TR::java_lang_invoke_MethodHandle_invokeExact
         || method->isArchetypeSpecimen()))
      {
      // We need a j2i thunk when this call executes, in case the target MH has
      // no invokeExact thunk yet.

      TR_J2IThunkTable *thunkTable = comp->getPersistentInfo()->getInvokeExactJ2IThunkTable();
      TR_J2IThunk      *thunk      = thunkTable->findThunk(methodSymbol->getMethod()->signatureChars(), this);
      return (thunk == NULL);
      }
   else
      return false;
   }

uintptr_t
TR_J9VMBase::methodHandle_thunkableSignature(uintptr_t methodHandle)
   {
   TR_ASSERT(haveAccess(), "methodHandle_thunkableSignature requires VM access");
   return getReferenceField(getReferenceField(
      methodHandle,
      "thunks",             "Ljava/lang/invoke/ThunkTuple;"),
      "thunkableSignature", "Ljava/lang/String;");
   }

uintptr_t
TR_J9VMBase::methodHandle_type(uintptr_t methodHandle)
   {
   TR_ASSERT(haveAccess(), "methodHandle_type requires VM access");
   return getReferenceField(
      methodHandle,
      "type", "Ljava/lang/invoke/MethodType;");
   }

uintptr_t
TR_J9VMBase::methodType_descriptor(uintptr_t methodType)
   {
   TR_ASSERT(haveAccess(), "methodType_descriptor requires VM access");
   return getReferenceField(
      methodType,
      "methodDescriptor", "Ljava/lang/String;");
   }

static uint8_t *bypassBaseAddress(uintptr_t mutableCallSite, TR_J9VMBase *fej9)
   {
   /* The location of the JNI global ref is actually stored in the form of an
      offset from the ramStatics area of whatever object was returned by
      getUnsafe().staticFieldBase(MutableCallSite.class).  This extra complexity
      is necessary because the java code uses Unsafe.putObject to update the
      global ref, so it must have a "base object" and "offset" just as though
      it were a static field.
   */
   uintptr_t *fieldAddress = (uintptr_t*)fej9->getStaticFieldAddress(
         fej9->getObjectClass(mutableCallSite),
         (unsigned char*)"bypassBase", 10, (unsigned char*)"Ljava/lang/Object;", 18
         );
   uintptr_t bypassBaseObject = *fieldAddress; // Statics are not compressed refs
   TR_OpaqueClassBlock *bypassClass = fej9->getClassFromJavaLangClass(bypassBaseObject);
   return (uint8_t*)TR::Compiler->cls.convertClassOffsetToClassPtr(bypassClass)->ramStatics;
   }

uintptr_t *
TR_J9VMBase::mutableCallSite_bypassLocation(uintptr_t mutableCallSite)
   {
   TR_ASSERT(haveAccess(), "mutableCallSite_bypassLocation requires VM access");

   int64_t bypassOffset = getInt64Field(getReferenceField(
      mutableCallSite,
      "globalRefCleaner", "Ljava/lang/invoke/GlobalRefCleaner;"),
      "bypassOffset");
   if (bypassOffset == 0)
      return NULL;

   uint8_t *baseAddress = bypassBaseAddress(mutableCallSite, this);
   bypassOffset &= -2L; // mask off low tag if present
   return (uintptr_t*)(baseAddress + bypassOffset);
   }

uintptr_t *
TR_J9VMBase::mutableCallSite_findOrCreateBypassLocation(uintptr_t mutableCallSite)
   {
   TR_ASSERT(haveAccess(), "mutableCallSite_bypassLocation requires VM access");

   uintptr_t cleaner   = getReferenceField(mutableCallSite, "globalRefCleaner", "Ljava/lang/invoke/GlobalRefCleaner;");
   uint32_t fieldOffset = getInstanceFieldOffset(getObjectClass(cleaner), "bypassOffset", "J");
   int64_t bypassOffset = getInt64FieldAt(cleaner, fieldOffset);
   if (bypassOffset == 0)
      {
      uintptr_t target = getReferenceField(mutableCallSite, "target", "Ljava/lang/invoke/MethodHandle;");
      jobject handleRef = vmThread()->javaVM->internalVMFunctions->j9jni_createGlobalRef((JNIEnv*)vmThread(), (j9object_t)target, false);
      uint8_t *baseAddress = bypassBaseAddress(mutableCallSite, this);
      bypassOffset = ((int64_t)handleRef) - ((int64_t)baseAddress);
      bypassOffset |= 1; // Low tag to pretend it's a static field
      if (!compareAndSwapInt64FieldAt(cleaner, fieldOffset, 0, bypassOffset))
         {
         // Another thread beat us to it
         vmThread()->javaVM->internalVMFunctions->j9jni_deleteGlobalRef((JNIEnv*)vmThread(), handleRef, false);
         }
      }
   return mutableCallSite_bypassLocation(mutableCallSite);
   }

static TR_OpaqueMethodBlock *findClosestArchetype(TR_OpaqueClassBlock *clazz, char *name, char *signature, char *currentArgument, TR_FrontEnd *fe, J9VMThread *vmThread)
   {
   // NOTE: signature will be edited in-place

   TR_J9VMBase *fej9 = (TR_J9VMBase *)fe;

   bool details = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseMethodHandleDetails);

   if (currentArgument[1] == ')')
      {
      TR_ASSERT(!strncmp(currentArgument, "I)", 2), "Must be pointing at the placeholder arg");
      }
   else
      {
      // We're not yet pointing at the last (placeholder) argument,
      // so let's see if we can get away with deleting fewer arguments
      //
      TR_OpaqueMethodBlock *result = findClosestArchetype(clazz, name, signature, nextSignatureArgument(currentArgument), fej9, vmThread);
      if (result)
         return result;

      // Otherwise, truncate the argument list
      //
      *currentArgument++ = 'I';
      char *tail = strchr(currentArgument, ')');
      while ((*currentArgument++ = *tail++));
      }

   TR_OpaqueMethodBlock *result = fej9->getMethodFromClass(clazz, name, signature);
   if (result)
      {
      TR_OpaqueClassBlock *methodClass = fej9->getClassFromMethodBlock(result);
      int32_t methodClassNameLength;
      char *methodClassName = fej9->getClassNameChars(methodClass, methodClassNameLength);

      if (methodClass == clazz)
         {
         if (details)
            TR_VerboseLog::writeLineLocked(TR_Vlog_MHD, "%p   - Found matching archetype %.*s.%s%s", vmThread, methodClassNameLength, methodClassName, name, signature);
         }
      else
         {
         // It's generally dangerous to use an inherited archetype because
         // usually the reason for creating a new MethodHandle class is that
         // its semantics are different, so the inherited archetypes are likely
         // to be unsuitable.  We'd rather revert to interpreter than compile
         // an unsuitable thunk.
         //
         result = NULL;
         if (details)
           TR_VerboseLog::writeLineLocked(TR_Vlog_MHD, "%p   - Ignoring inherited archetype %.*s.%s%s", vmThread, methodClassNameLength, methodClassName, name, signature);
         }
      }
   return result;
   }

TR_OpaqueMethodBlock *
TR_J9VMBase::lookupArchetype(TR_OpaqueClassBlock *clazz, char *name, char *signature)
   {
   // Find the best match for the signature.  Start by appending an "I"
   // placeholder argument.  findClosestArchetype will progressively truncate
   // the other arguments until the best match is found.
   //
   char *truncatedSignature = (char*)alloca(strlen(signature)+2); // + 'I' + null terminator
   strcpy(truncatedSignature, signature);
   char toInsert = 'I';
   char *cur;
   for (cur = strrchr(truncatedSignature, ')'); toInsert; cur++)
      {
      char toInsertNext = *cur;
      *cur = toInsert;
      toInsert = toInsertNext;
      }
   *cur = 0;
   return findClosestArchetype(clazz, name, truncatedSignature, truncatedSignature+1, this, getCurrentVMThread());
   }

TR_OpaqueMethodBlock *
TR_J9VMBase::lookupMethodHandleThunkArchetype(uintptr_t methodHandle)
   {
   TR_ASSERT(haveAccess(), "methodHandle_jitInvokeExactThunk requires VM access");

   // Compute thunk's asignature and archetype's name
   //
   uintptr_t thunkableSignatureString = methodHandle_thunkableSignature((uintptr_t)methodHandle);
   intptr_t  thunkableSignatureLength = getStringUTF8Length(thunkableSignatureString);
   char *thunkSignature = (char*)alloca(thunkableSignatureLength+1);
   getStringUTF8(thunkableSignatureString, thunkSignature, thunkableSignatureLength+1);

   char *archetypeSpecimenSignature = (char*)alloca(thunkableSignatureLength+20);
   strcpy(archetypeSpecimenSignature, thunkSignature);
   char *returnType = (1+strchr(archetypeSpecimenSignature, ')'));
   switch (returnType[0])
      {
      case '[':
      case 'L':
         // The thunkable signature might return some other class, but archetypes
         // returning a reference are always declared to return Object.
         //
         sprintf(returnType, "Ljava/lang/Object;");
         break;
      }
   char methodName[50];
   sprintf(methodName, "invokeExact_thunkArchetype_%c", returnType[0]);

   TR_OpaqueMethodBlock *result = lookupArchetype(getObjectClass((uintptr_t)methodHandle), methodName, archetypeSpecimenSignature);
   if (!result)
      {
      strcpy(returnType, "I");
      result = lookupArchetype(getObjectClass((uintptr_t)methodHandle), "invokeExact_thunkArchetype_X", archetypeSpecimenSignature);
      }
   return result;
   }

TR_ResolvedMethod *
TR_J9VMBase::createMethodHandleArchetypeSpecimen(TR_Memory *trMemory, TR_OpaqueMethodBlock *archetype, uintptr_t *methodHandleLocation, TR_ResolvedMethod *owningMethod)
   {
   intptr_t length;
   char *thunkableSignature;

      {
      TR::VMAccessCriticalSection createMethodHandleArchetypeSpecimen(this);
      TR_ASSERT(archetype, "Explicitly provided archetype must not be null");
      TR_ASSERT(archetype == lookupMethodHandleThunkArchetype(*methodHandleLocation), "Explicitly provided archetype must be the right one");
      uintptr_t signatureString = getReferenceField(getReferenceField(
         *methodHandleLocation,
         "thunks",             "Ljava/lang/invoke/ThunkTuple;"),
         "thunkableSignature", "Ljava/lang/String;");
      length = getStringUTF8Length(signatureString);
      thunkableSignature = (char*)trMemory->allocateStackMemory(length+1);
      getStringUTF8(signatureString, thunkableSignature, length+1);
      }

   TR_ResolvedMethod *result = createResolvedMethodWithSignature(trMemory, archetype, NULL, thunkableSignature, length, owningMethod);
   result->convertToMethod()->setArchetypeSpecimen();
   result->setMethodHandleLocation(methodHandleLocation);
   return result;
   }

TR_ResolvedMethod *
TR_J9VMBase::createMethodHandleArchetypeSpecimen(TR_Memory *trMemory, uintptr_t *methodHandleLocation, TR_ResolvedMethod *owningMethod)
   {
   TR::VMAccessCriticalSection createMethodHandleArchetypeSpecimenCS(this);
   TR_OpaqueMethodBlock *archetype = lookupMethodHandleThunkArchetype(*methodHandleLocation);
   TR_ResolvedMethod *result;
   if (archetype)
      result = createMethodHandleArchetypeSpecimen(trMemory, archetype, methodHandleLocation, owningMethod);
   else
      result = NULL;

   return result;
   }

uintptr_t TR_J9VMBase::mutableCallSiteCookie(uintptr_t mutableCallSite, uintptr_t potentialCookie)
   {
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
   uint32_t offset = _jitConfig->javaVM->mutableCallSiteInvalidationCookieOffset;
   // The *FieldAt() functions below will add the header size, but that's
   // already included in mutableCallSiteInvalidationCookieOffset.
   offset -= getObjectHeaderSizeInBytes();
#else
   uint32_t offset = getInstanceFieldOffset(
      getObjectClass(mutableCallSite), "invalidationCookie", "J");
#endif

   uintptr_t result=0;
   if (potentialCookie && compareAndSwapInt64FieldAt(mutableCallSite, offset, 0, potentialCookie))
      result =  potentialCookie;
   else
      result = (uintptr_t)getInt64FieldAt(mutableCallSite, offset);

   return result;
   }

TR::KnownObjectTable::Index TR_J9VMBase::mutableCallSiteEpoch(TR::Compilation *comp, uintptr_t mutableCallSite)
   {
   TR_ASSERT_FATAL(haveAccess(), "mutableCallSiteEpoch requires VM access");

   TR::KnownObjectTable *knot = comp->getKnownObjectTable();
   if (knot == NULL)
      return TR::KnownObjectTable::UNKNOWN;

   // getVolatileReferenceField() doesn't accept const char*
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
   char *fieldName = "target"; // There is no separate epoch field.
#else
   char *fieldName = "epoch";
#endif

   uintptr_t mh = getVolatileReferenceField(
      mutableCallSite, fieldName, "Ljava/lang/invoke/MethodHandle;");

   return mh == 0 ? TR::KnownObjectTable::UNKNOWN : knot->getOrCreateIndex(mh);
   }

bool
TR_J9VMBase::hasMethodTypesSideTable()
   {
   return true;
   }

void *
TR_J9VMBase::methodHandle_jitInvokeExactThunk(uintptr_t methodHandle)
   {
   TR_ASSERT(haveAccess(), "methodHandle_jitInvokeExactThunk requires VM access");
   return (void*)(intptr_t)getInt64Field(getReferenceField(
      methodHandle,
      "thunks", "Ljava/lang/invoke/ThunkTuple;"),
      "invokeExactThunk");
   }

#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
TR_OpaqueMethodBlock*
TR_J9VMBase::targetMethodFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex)
   {
   auto knot = comp->getKnownObjectTable();
   if (objIndex != TR::KnownObjectTable::UNKNOWN &&
       knot &&
       !knot->isNull(objIndex))
      {
      TR::VMAccessCriticalSection getTarget(this);
      uintptr_t object = knot->getPointer(objIndex);
      return targetMethodFromMemberName(object);
      }
   return NULL;
   }

TR_OpaqueMethodBlock*
TR_J9VMBase::targetMethodFromMemberName(uintptr_t memberName)
   {
   TR_ASSERT(haveAccess(), "targetFromMemberName requires VM access");
   return (TR_OpaqueMethodBlock*)J9OBJECT_U64_LOAD(vmThread(), memberName, vmThread()->javaVM->vmtargetOffset);
   }

TR_OpaqueMethodBlock*
TR_J9VMBase::targetMethodFromMethodHandle(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex)
   {
   auto knot = comp->getKnownObjectTable();
   if (objIndex != TR::KnownObjectTable::UNKNOWN &&
       knot &&
       !knot->isNull(objIndex))
      {
      TR::VMAccessCriticalSection getTarget(this);
      uintptr_t object = knot->getPointer(objIndex);
      return targetMethodFromMethodHandle(object);
      }
   return NULL;
   }

TR_OpaqueMethodBlock*
TR_J9VMBase::targetMethodFromMethodHandle(uintptr_t methodHandle)
   {
   TR_ASSERT(haveAccess(), "targetFromMethodHandle requires VM access");
   uintptr_t form = getReferenceField(
      methodHandle,
      "form",             "Ljava/lang/invoke/LambdaForm;");
   uintptr_t vmentry = getReferenceField(
      form,
      "vmentry",             "Ljava/lang/invoke/MemberName;");
   return targetMethodFromMemberName(vmentry);
   }

J9JNIMethodID*
TR_J9VMBase::jniMethodIdFromMemberName(uintptr_t memberName)
   {
   TR_ASSERT(haveAccess(), "jniMethodIdFromMemberName requires VM access");
   return (J9JNIMethodID *)J9OBJECT_U64_LOAD(vmThread(), memberName, vmThread()->javaVM->vmindexOffset);
   }

J9JNIMethodID*
TR_J9VMBase::jniMethodIdFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex)
   {
   auto knot = comp->getKnownObjectTable();
   if (objIndex != TR::KnownObjectTable::UNKNOWN &&
       knot &&
       !knot->isNull(objIndex))
      {
      TR::VMAccessCriticalSection jniMethodId(this);
      uintptr_t object = knot->getPointer(objIndex);
      return jniMethodIdFromMemberName(object);
      }
   return NULL;
   }

int32_t
TR_J9VMBase::vTableOrITableIndexFromMemberName(uintptr_t memberName)
   {
   TR_ASSERT(haveAccess(), "vTableOrITableIndexFromMemberName requires VM access");
   auto methodID = jniMethodIdFromMemberName(memberName);
   return (int32_t)methodID->vTableIndex;
   }

int32_t
TR_J9VMBase::vTableOrITableIndexFromMemberName(TR::Compilation* comp, TR::KnownObjectTable::Index objIndex)
   {
   auto knot = comp->getKnownObjectTable();
   if (objIndex != TR::KnownObjectTable::UNKNOWN &&
       knot &&
       !knot->isNull(objIndex))
      {
      TR::VMAccessCriticalSection vTableOrITableIndex(this);
      uintptr_t object = knot->getPointer(objIndex);
      return vTableOrITableIndexFromMemberName(object);
      }
   return -1;
   }

TR::KnownObjectTable::Index
TR_J9VMBase::getKnotIndexOfInvokeCacheArrayAppendixElement(TR::Compilation *comp, uintptr_t *invokeCacheArray)
   {
   TR::KnownObjectTable *knot = comp->getOrCreateKnownObjectTable();
   if (!knot) return TR::KnownObjectTable::UNKNOWN;

   TR::VMAccessCriticalSection vmAccess(this);
   uintptr_t appendixElementRef = (uintptr_t) getReferenceElement(*invokeCacheArray, JSR292_invokeCacheArrayAppendixIndex);
   return knot->getOrCreateIndex(appendixElementRef);
   }

TR_ResolvedMethod *
TR_J9VMBase::targetMethodFromInvokeCacheArrayMemberNameObj(TR::Compilation *comp, TR_ResolvedMethod *owningMethod, uintptr_t *invokeCacheArray)
   {
   TR_OpaqueMethodBlock *targetMethodObj = 0;
      {
      TR::VMAccessCriticalSection vmAccess(this);
      uintptr_t memberNameElementRef = (uintptr_t) getReferenceElement(*invokeCacheArray, JSR292_invokeCacheArrayMemberNameIndex);
      targetMethodObj = targetMethodFromMemberName(memberNameElementRef);
      }
   return createResolvedMethod(comp->trMemory(), targetMethodObj, owningMethod);
   }

TR::SymbolReference*
TR_J9VMBase::refineInvokeCacheElementSymRefWithKnownObjectIndex(TR::Compilation *comp, TR::SymbolReference *originalSymRef, uintptr_t *invokeCacheArray)
   {
   TR::VMAccessCriticalSection vmAccess(this);
   uintptr_t arrayElementRef = (uintptr_t) getReferenceElement(*invokeCacheArray, JSR292_invokeCacheArrayAppendixIndex);
   TR::KnownObjectTable *knot = comp->getOrCreateKnownObjectTable();
   if (!knot) return originalSymRef;
   TR::KnownObjectTable::Index arrayElementKnotIndex = TR::KnownObjectTable::UNKNOWN;
   arrayElementKnotIndex = knot->getOrCreateIndex(arrayElementRef);
   TR::SymbolReference *newRef = comp->getSymRefTab()->findOrCreateSymRefWithKnownObject(originalSymRef, arrayElementKnotIndex);
   return newRef;
   }

static char *
getSignatureForLinkToStatic(
   const char * const extraParamsBefore,
   const char * const extraParamsAfter,
   TR::Compilation* comp,
   J9UTF8* romMethodSignature,
   int32_t &signatureLength)
   {
   const size_t extraParamsLength = strlen(extraParamsBefore) + strlen(extraParamsAfter);

   const char * const origSignature = utf8Data(romMethodSignature);
   const int origSignatureLength = J9UTF8_LENGTH(romMethodSignature);
   signatureLength = origSignatureLength + extraParamsLength;

   const int32_t signatureAllocSize = signatureLength + 1; // +1 for NUL terminator
   char * linkToStaticSignature =
      (char *)comp->trMemory()->allocateMemory(signatureAllocSize, heapAlloc);

   TR_ASSERT_FATAL(
      origSignature[0] == '(',
      "method signature must begin with '(': `%.*s'",
      origSignatureLength,
      origSignature);

   // Can't simply strchr() for ')' because that can appear within argument
   // types, in particular within class names. It's necessary to parse the
   // signature.
   const char * const paramsStart = origSignature + 1;
   const char * paramsEnd = paramsStart;
   while (*paramsEnd != ')')
      paramsEnd = nextSignatureArgument(paramsEnd);

   const char * const returnType = paramsEnd + 1;
   const char * const returnTypeEnd = nextSignatureArgument(returnType);

   // Check that the parsed length is sensible. This also ensures that it's
   // safe to truncate the lengths of the params and the return type to int
   // (since they'll be less than parsedLength).
   const ptrdiff_t parsedLength = returnTypeEnd - origSignature;
   TR_ASSERT_FATAL(
      0 <= parsedLength && parsedLength <= INT_MAX,
      "bad parsedLength %lld for romMethodSignature (J9UTF8*)%p",
      romMethodSignature);

   TR_ASSERT_FATAL(
      (int)parsedLength == origSignatureLength,
      "parsed method signature length %d differs from claimed length %d "
      "(origSignature `%.*s')",
      (int)parsedLength,
      origSignatureLength,
      parsedLength > origSignatureLength ? (int)parsedLength : origSignatureLength,
      origSignature);

   // Put together the new signature.
   const int formattedLength = snprintf(
      linkToStaticSignature,
      signatureAllocSize,
      "(%s%.*s%s)%.*s",
      extraParamsBefore,
      (int)(paramsEnd - paramsStart),
      paramsStart,
      extraParamsAfter,
      (int)(returnTypeEnd - returnType),
      returnType);

   // This condition implies that (formattedLength < signatureAllocSize), so
   // given that the assertion passes, we can be sure that the signature was
   // not truncated.
   TR_ASSERT_FATAL(
      formattedLength == signatureLength,
      "expected linkToStatic signature length %d but got %d "
      "(origSignature `%.*s', extraParamsBefore `%s', extraParamsAfter `%s')",
      signatureLength,
      formattedLength,
      origSignatureLength,
      origSignature,
      extraParamsBefore,
      extraParamsAfter);

   return linkToStaticSignature;
   }

char *
TR_J9VMBase::getSignatureForLinkToStaticForInvokeHandle(TR::Compilation* comp, J9UTF8* romMethodSignature, int32_t &signatureLength)
   {
   return getSignatureForLinkToStatic(
        "Ljava/lang/Object;",
        "Ljava/lang/Object;Ljava/lang/Object;",
        comp,
        romMethodSignature,
        signatureLength);
   }

char *
TR_J9VMBase::getSignatureForLinkToStaticForInvokeDynamic(TR::Compilation* comp, J9UTF8* romMethodSignature, int32_t &signatureLength)
   {
   return getSignatureForLinkToStatic(
        "",
        "Ljava/lang/Object;Ljava/lang/Object;",
        comp,
        romMethodSignature,
        signatureLength);
   }
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

TR::KnownObjectTable::Index
TR_J9VMBase::getMemberNameFieldKnotIndexFromMethodHandleKnotIndex(TR::Compilation *comp, TR::KnownObjectTable::Index mhIndex, char *fieldName)
   {
   TR::VMAccessCriticalSection dereferenceKnownObjectField(this);
   TR::KnownObjectTable *knot = comp->getKnownObjectTable();
   uintptr_t mhObject = knot->getPointer(mhIndex);
   uintptr_t mnObject = getReferenceField(mhObject, fieldName, "Ljava/lang/invoke/MemberName;");
   return knot->getOrCreateIndex(mnObject);
   }

/**
 * \brief
 *    Check if two java/lang/String objects are equal. Equivalent to java/lang/String.equals.
 *
 * \parm comp
 *    The compilation object.
 *
 * \parm stringLocation1
 *    The location to the first java/lang/String object reference.
 *
 * \parm stringLocation2
 *    The location to the second java/lang/String object reference.
 *
 * \parm result
 *    Place to store the result of the comparison.
 *
 * \return
 *    True if comparison succeeds, false otherwise.
 */
bool
TR_J9VMBase::stringEquals(TR::Compilation * comp, uintptr_t* stringLocation1, uintptr_t* stringLocation2, int32_t& result)
   {
   TR::VMAccessCriticalSection stringEquals(this,
                                             TR::VMAccessCriticalSection::tryToAcquireVMAccess,
                                             comp);

   if (!stringEquals.hasVMAccess())
      return false;

   J9InternalVMFunctions * intFunc = vmThread()->javaVM->internalVMFunctions;
   result = intFunc->compareStrings(vmThread(), (j9object_t)*stringLocation1, (j9object_t)*stringLocation2);
   return true;
   }

/**
 * \brief
 *    Get the hash code of a java/lang/String object
 *
 * \parm comp
 *    The compilation object.
 *
 * \parm string
 *    The location to the java/lang/String object reference.
 *
 * \parm result
 *    Place to store the hash code when the query succeeds.
 *
 * \return
 *    True if the query succeeds, otherwise false.
 */
bool
TR_J9VMBase::getStringHashCode(TR::Compilation * comp, uintptr_t* stringLocation, int32_t& result)
   {
   TR::VMAccessCriticalSection getStringHashCodeCriticalSection(this,
                                                                TR::VMAccessCriticalSection::tryToAcquireVMAccess,
                                                                comp);

   if (!getStringHashCodeCriticalSection.hasVMAccess())
      return false;

   result = vmThread()->javaVM->memoryManagerFunctions->j9gc_stringHashFn((void *)stringLocation, vmThread()->javaVM);
   return true;
   }

bool
TR_J9VMBase::getStringFieldByName(TR::Compilation * comp, TR::SymbolReference * stringRef, TR::SymbolReference * fieldRef, void * & pResult)
   {
   TR::VMAccessCriticalSection getStringFieldCriticalSection(this,
                                                              TR::VMAccessCriticalSection::tryToAcquireVMAccess,
                                                              comp);

   if (!getStringFieldCriticalSection.hasVMAccess())
      return false;

   TR_ASSERT(!stringRef->isUnresolved(), "don't handle unresolved constant strings yet");

   uintptr_t stringStaticAddr = (uintptr_t)stringRef->getSymbol()->castToStaticSymbol()->getStaticAddress();
   j9object_t string = (j9object_t)getStaticReferenceFieldAtAddress(stringStaticAddr);

   TR::Symbol::RecognizedField   field = fieldRef->getSymbol()->getRecognizedField();

   if (field == TR::Symbol::Java_lang_String_count)
      pResult = (U_8*)string + J9VMJAVALANGSTRING_COUNT_OFFSET(vmThread());
   else if (field == TR::Symbol::Java_lang_String_hashCode)
      {
      if (J9VMJAVALANGSTRING_HASH(vmThread(), string) == 0)
         {
         // If not already computed, compute and clobber
         //
         int32_t sum   = 0;
         int32_t scale = 1;

         for (int32_t i = J9VMJAVALANGSTRING_LENGTH(vmThread(), string) - 1; i >= 0; --i, scale *= 31)
            {
            uint16_t thisChar = getStringCharacter((uintptr_t)string, i);
            sum += thisChar * scale;
            }

         J9VMJAVALANGSTRING_SET_HASH(vmThread(), string, sum);
         }
      pResult = (U_8*)string + J9VMJAVALANGSTRING_HASH_OFFSET(vmThread());
      }
   else if (field == TR::Symbol::Java_lang_String_value)
      pResult = (U_8*)string + J9VMJAVALANGSTRING_VALUE_OFFSET(vmThread());
   else
      return false;

   return true;
   }

uintptr_t
TR_J9VMBase::getFieldOffset(TR::Compilation * comp, TR::SymbolReference* classRef, TR::SymbolReference* fieldRef)
   {
   TR_ResolvedMethod* method = classRef->getOwningMethod(comp);
   TR::StaticSymbol* classSym = classRef->getSymbol()->castToStaticSymbol();
   j9object_t classString = (j9object_t)getStaticReferenceFieldAtAddress((uintptr_t)classSym->getStaticAddress());
   TR::StaticSymbol* fieldSym = fieldRef->getSymbol()->castToStaticSymbol();
   j9object_t fieldString = (j9object_t)getStaticReferenceFieldAtAddress((uintptr_t)fieldSym->getStaticAddress());

   int32_t len = (int32_t)jitConfig->javaVM->internalVMFunctions->getStringUTF8Length(vmThread(), classString);
   U_8* u8ClassString = (U_8*)comp->trMemory()->allocateStackMemory(len + 1);

   jitConfig->javaVM->internalVMFunctions->copyStringToUTF8Helper(vmThread(), classString, J9_STR_NULL_TERMINATE_RESULT | J9_STR_XLAT, 0, J9VMJAVALANGSTRING_LENGTH(vmThread(), classString), u8ClassString, len + 1);

   /**
   //fprintf(stderr,"name is (res is %d) classString is %p\n",res, classString); fflush(stderr);
   for (int i =0; i<len; i++)
      {
      fprintf(stderr,"%c",u8ClassString[i]);
      }
   fprintf(stderr,"  (len is %d)\n",len);fflush(stderr);
   **/

   char* classSignature = classNameToSignature((char*)u8ClassString, len, comp);

   /**
   fprintf(stderr,"classSignature is \n");
   for (int i =0; i <len; i++){
      fprintf(stderr,"%c",classSignature[i]);
   }
   fprintf(stderr,"  (len is %d)\n",len);
   **/

   TR_OpaqueClassBlock * j9ClassPtr = getClassFromSignature(classSignature, len, method);
   //fprintf(stderr,"Class looked up to be %p \n", j9ClassPtr);

   if (!j9ClassPtr) return 0;

   TR_VMFieldsInfo fields(comp, (J9Class*)j9ClassPtr, 1);

   len = (int32_t)jitConfig->javaVM->internalVMFunctions->getStringUTF8Length(vmThread(), fieldString);
   U_8* u8FieldString = (U_8*)comp->trMemory()->allocateStackMemory(len + 1);

   jitConfig->javaVM->internalVMFunctions->copyStringToUTF8Helper(vmThread(), fieldString, J9_STR_NULL_TERMINATE_RESULT, 0, J9VMJAVALANGSTRING_LENGTH(vmThread(), fieldString), u8FieldString, len + 1);

   ListIterator<TR_VMField> itr(fields.getFields()) ;
   TR_VMField* field;
   uint32_t offset = 0;
   for (field = itr.getFirst(); field != NULL; field= itr.getNext())
      {
      // fprintf(stderr, "fieldName %s fieldOffset %d fieldSig %s\n",field->name, field->offset, field->signature);
      if (!strncmp(field->name, (const char*)u8FieldString, len+1))
         {
         offset = (uint32_t)(field->offset + getObjectHeaderSizeInBytes());
         // Do we Need this?
         // offset = getInstanceFieldOffset(j9ClassPtr, field->name, strlen(field->name), field->signature, strlen(field->signature),
         //                               J9_LOOK_NO_JAVA);

         // fprintf(stderr,">>>>> Offset for %s determined to be : %d\n", field->name,offset);
         return (uintptr_t)offset;
         }
      }

   void * staticAddr = 0;
   itr = fields.getStatics() ;
   for (field = itr.getFirst(); field != NULL; field=itr.getNext())
      {
      if (!strncmp(field->name, (const char*)u8FieldString, len+1))
         {
         // Do we Need to acquire VM Access? getInstanceFieldOffset does it?
         TR::VMAccessCriticalSection staticFieldAddress(this);
         staticAddr = jitConfig->javaVM->internalVMFunctions->staticFieldAddress(_vmThread,
                          (J9Class*)j9ClassPtr, u8FieldString, len,  (U_8*)field->signature, (UDATA)strlen(field->signature),
                          NULL, NULL, J9_LOOK_NO_JAVA, NULL);
         }
      }


   return (uintptr_t)staticAddr;
   }

bool
TR_J9VMBase::isJavaLangObject(TR_OpaqueClassBlock *clazz)
   {
   return (J9Class*)clazz == J9VMJAVALANGOBJECT(jitConfig->javaVM);
   }

/**
 * \brief
 *    Check if the giving J9Class is pointing to a java/lang/String object.
 *
 * \parm clazz
 *    The J9Class pointer to be checked.
 *
 * \return
 *    TR_yes if clazz is java/lang/String, TR_maybe if clazz is a super class of
 *    String or interface classes implemented by String.
 */
TR_YesNoMaybe
TR_J9VMBase::typeReferenceStringObject(TR_OpaqueClassBlock *clazz)
   {
   if (isClassArray(clazz) || isPrimitiveClass(clazz))
      return TR_no;
   if (isJavaLangObject(clazz))
      return TR_maybe;
   if (isInterfaceClass(clazz))
      {
      J9UTF8 * className = J9ROMCLASS_CLASSNAME(TR::Compiler->cls.romClassOf(clazz));
      int32_t len = J9UTF8_LENGTH(className);
      if ((len == 20 && strncmp(utf8Data(className), "java/io/Serializable", 20) == 0) ||
          (len == 22 && strncmp(utf8Data(className), "java/lang/CharSequence",22) == 0) ||
          (len == 20 && strncmp(utf8Data(className), "java/lang/Comparable", 20) == 0))
         return TR_maybe;
      else
         return TR_no;
      }
   return isString(clazz) ? TR_yes : TR_no;
   }

bool
TR_J9VMBase::isString(TR_OpaqueClassBlock *clazz)
   {
   return (J9Class*)clazz == J9VMJAVALANGSTRING(jitConfig->javaVM);
   }

bool
TR_J9VMBase::isString(uintptr_t objectPointer)
   {
   TR_ASSERT(haveAccess(), "isString requires VM access");
   return isString(getObjectClass(objectPointer));
   }

int32_t
TR_J9VMBase::getStringLength(uintptr_t objectPointer)
   {
   TR_ASSERT(haveAccess(), "getStringLength requires VM access");
   return J9VMJAVALANGSTRING_LENGTH(vmThread(), (j9object_t)objectPointer);
   }

uint16_t
TR_J9VMBase::getStringCharacter(uintptr_t objectPointer, int32_t index)
   {
   TR_ASSERT(haveAccess(), "getStringCharacter requires VM access");

   j9object_t bytes = J9VMJAVALANGSTRING_VALUE(vmThread(), (j9object_t)objectPointer);

   if (IS_STRING_COMPRESSED(vmThread(), (j9object_t)objectPointer))
      {
      return static_cast<uint16_t>(J9JAVAARRAYOFBYTE_LOAD(vmThread(), bytes, index)) & static_cast<uint16_t>(0xFF);
      }
   else
      {
      return static_cast<uint16_t>(J9JAVAARRAYOFCHAR_LOAD(vmThread(), bytes, index));
      }
   }

intptr_t
TR_J9VMBase::getStringUTF8Length(uintptr_t objectPointer)
   {
   TR_ASSERT(haveAccess(), "Must have VM access to call getStringUTF8Length");
   TR_ASSERT(objectPointer, "assertion failure");
   return vmThread()->javaVM->internalVMFunctions->getStringUTF8Length(vmThread(), (j9object_t)objectPointer);
   }

char *
TR_J9VMBase::getStringUTF8(uintptr_t objectPointer, char *buffer, intptr_t bufferSize)
   {
   TR_ASSERT(haveAccess(), "Must have VM access to call getStringAscii");

   vmThread()->javaVM->internalVMFunctions->copyStringToUTF8Helper(vmThread(), (j9object_t)objectPointer, J9_STR_NULL_TERMINATE_RESULT, 0, J9VMJAVALANGSTRING_LENGTH(vmThread(), objectPointer), (U_8*)buffer, (UDATA)bufferSize);

   return buffer;
   }

uint32_t
TR_J9VMBase::getVarHandleHandleTableOffset(TR::Compilation * comp)
   {
   return uint32_t(J9VMJAVALANGINVOKEVARHANDLE_HANDLETABLE_OFFSET(vmThread()));
   }

// set a 32 bit field that will be printed if the VM crashes
// typically, this should be used to represent the state of the
// compilation
//

void
TR_J9VMBase::reportILGeneratorPhase()
   {
   if (!_vmThread)
      return;

   enum { DEFAULT_LOW_BYTE=0x80 };

   vmThread()->omrVMThread->vmState = J9VMSTATE_JIT | (DEFAULT_LOW_BYTE & 0xFF);
   }

void
TR_J9VMBase::reportOptimizationPhase(OMR::Optimizations opts)
   {
   if (!_vmThread)
      return;

   vmThread()->omrVMThread->vmState = J9VMSTATE_JIT_OPTIMIZER | ((static_cast<int32_t>(opts) & 0xFF) << 8);
   }

void
TR_J9VMBase::reportAnalysisPhase(uint8_t id)
   {
   if (!_vmThread)
      return;

   vmThread()->omrVMThread->vmState = (vmThread()->omrVMThread->vmState & ~0xFF) | id;
   }

void
TR_J9VMBase::reportOptimizationPhaseForSnap(OMR::Optimizations opts, TR::Compilation *comp)
   {
   if (!_vmThread)
      return;

   if (TrcEnabled_Trc_JIT_optimizationPhase && comp)
      Trc_JIT_optimizationPhase(vmThread(), comp->getOptimizer()->getOptimization(opts)->name());

   }

void
TR_J9VMBase::reportCodeGeneratorPhase(TR::CodeGenPhase::PhaseValue phase)
   {
   if (!_vmThread)
      return;

   vmThread()->omrVMThread->vmState = J9VMSTATE_JIT_CODEGEN | phase;

   if (TrcEnabled_Trc_JIT_codeGeneratorPhase)
      Trc_JIT_codeGeneratorPhase(vmThread(), TR::CodeGenPhase::getName(phase));
   }

int32_t
TR_J9VMBase::saveCompilationPhase()
   {
   if (vmThread())
      return vmThread()->omrVMThread->vmState;
   else
      return 0xdead1138;
   }

void
TR_J9VMBase::restoreCompilationPhase(int32_t phase)
   {
   if (vmThread())
      vmThread()->omrVMThread->vmState = phase;
   }

void
TR_J9VMBase::reportPrexInvalidation(void * startPC)
   {
   if (!_vmThread)
      return;
   // Generate a trace point
   Trc_JIT_MethodPrexInvalidated(vmThread(), startPC);
   }


// Multiple codeCache support

void
TR_J9VMBase::setHasFailedCodeCacheAllocation()
   {
   if (!_compInfo->getRampDownMCT())
      {
      _compInfo->setRampDownMCT();
      if (TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseCompilationThreads))
         {
         TR_VerboseLog::writeLineLocked(TR_Vlog_INFO,"t=%u setRampDownMCT", (uint32_t)_compInfo->getPersistentInfo()->getElapsedTime());
         }
      }
   }

TR::CodeCache *
TR_J9VMBase::getDesignatedCodeCache(TR::Compilation *comp) // MCT
   {
   int32_t numReserved;
   int32_t compThreadID = comp ? comp->getCompThreadID() : -1;

   bool hadClassUnloadMonitor;
   bool hadVMAccess = releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(comp, &hadClassUnloadMonitor);

   TR::CodeCache * result = TR::CodeCacheManager::instance()->reserveCodeCache(false, 0, compThreadID, &numReserved);

   acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(comp, hadVMAccess, hadClassUnloadMonitor);
   if (!result)
      {
      // If this is a temporary condition due to all code caches being reserved for the moment
      // we should retry this compilation
      if (!(jitConfig->runtimeFlags & J9JIT_CODE_CACHE_FULL))
         {
         // If this is a temporary condition due to all code caches being reserved for the moment
         // we should retry this compilation
         if (numReserved > 0)
            {
            // set an error code so that the compilation is retried
            if (comp)
               {
               comp->failCompilation<TR::RecoverableCodeCacheError>("Cannot reserve code cache");
               }
            }
         }
      }
   return result;
   }

void *
TR_J9VMBase::getCCPreLoadedCodeAddress(TR::CodeCache *codeCache, TR_CCPreLoadedCode h, TR::CodeGenerator *cg)
   {
    return codeCache->getCCPreLoadedCodeAddress(h, cg);
   }

void
TR_J9VMBase::reserveTrampolineIfNecessary(TR::Compilation * comp, TR::SymbolReference * symRef, bool inBinaryEncoding)
   {
   TR::VMAccessCriticalSection reserveTrampolineIfNecessary(this);
   TR::CodeCache *curCache = comp->cg()->getCodeCache();
   bool isRecursive = false;

   if (NULL == curCache)
      {
      if (isAOT_DEPRECATED_DO_NOT_USE())
         {
         comp->failCompilation<TR::RecoverableCodeCacheError>("Failed to get current code cache");
         }
#ifdef MCT_DEBUG
      fprintf(stderr, "Aborting compilation no space for trampoline area %p\n", comp);
#endif
      comp->failCompilation<TR::CodeCacheError>("Failed to get current code cache");
      }

   TR_ASSERT(curCache->isReserved(), "assertion failure"); // MCT

   if (!symRef->isUnresolved() && !comp->isDLT())
      {
      TR_ResolvedMethod *resolvedMethod = symRef->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod();
      isRecursive = resolvedMethod->isSameMethod(comp->getCurrentMethod());
      }

   TR::CodeCache *newCache = curCache; // optimistically assume that we will manage to allocate trampoline from current code cache
   if (isAOT_DEPRECATED_DO_NOT_USE() && isRecursive)
      {
      TR_AOTMethodHeader *aotMethodHeaderEntry =  comp->getAotMethodHeaderEntry();
      aotMethodHeaderEntry->flags |= TR_AOTMethodHeader_NeedsRecursiveMethodTrampolineReservation; // Set flag in TR_AOTMethodHeader
      //newCache = curCache; // done above
      }
   else if (symRef->isUnresolved() || isAOT_DEPRECATED_DO_NOT_USE())
      {
      void *cp = (void *)symRef->getOwningMethod(comp)->constantPool();
      I_32 cpIndex = symRef->getCPIndexForVM();

#if 0
      if (isAOT_DEPRECATED_DO_NOT_USE() && (comp->getOption(TR_TraceRelocatableDataCG) || comp->getOption(TR_TraceRelocatableDataDetailsCG)) )
         {
          traceMsg(comp, "<relocatableDataTrampolinesCG>\n");
          traceMsg(comp, "%s\n", comp->signature());
          traceMsg(comp, "%-8s", "cpIndex");
          traceMsg(comp, "cp\n");
          traceMsg(comp, "%-8x", cpIndex);
          traceMsg(comp, "%x\n", cp);
          traceMsg(comp, "</relocatableDataTrampolinesCG>\n");
         }
#endif

      // AOT compiles create a relocation at the snippet to do the trampoline reservation
      // would be better to implement this code via a virtual function that's empty for TR_J9SharedCacheVM
      if (!isAOT_DEPRECATED_DO_NOT_USE())
         {
         bool hadClassUnloadMonitor;
         bool hadVMAccess = releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(comp, &hadClassUnloadMonitor);

         int32_t retValue = curCache->reserveUnresolvedTrampoline(cp, cpIndex);
         if (retValue != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
            {
            // We couldn't allocate trampoline in this code cache
            curCache->unreserve(); // delete the old reservation
            if (retValue == OMR::CodeCacheErrorCode::ERRORCODE_INSUFFICIENTSPACE && !inBinaryEncoding) // code cache full, allocate a new one
               {
               // Allocate a new code cache and try again
               newCache = TR::CodeCacheManager::instance()->getNewCodeCache(comp->getCompThreadID()); // class unloading may happen here
               if (newCache)
                  {
                  // check for class unloading that can happen in getNewCodeCache
                  TR::CompilationInfoPerThreadBase * const compInfoPTB =
                     _compInfo->getCompInfoForCompOnAppThread() ?
                        _compInfo->getCompInfoForCompOnAppThread() :
                        _compInfoPT;
                  if (compInfoPTB->compilationShouldBeInterrupted())
                     {
                     newCache->unreserve(); // delete the reservation
                     newCache = NULL;
                     comp->failCompilation<TR::CompilationInterrupted>("Compilation Interrupted when reserving trampoline if necessary");
                     }
                  else
                     {
                     retValue = ((TR::CodeCache*)newCache)->reserveUnresolvedTrampoline(cp, cpIndex);
                     if (retValue != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
                        {
                        newCache->unreserve(); // delete the reservation
                        newCache = NULL;
                        comp->failCompilation<TR::TrampolineError>("Failed to reserve unresolved trampoline");
                        }
                     }
                  }
               else // cannot allocate a new code cache
                  {
                  comp->failCompilation<TR::TrampolineError>("Failed to allocate new code cache");
                  }
               }
            else
               {
               newCache = 0;
               if (inBinaryEncoding)
                  {
                  comp->failCompilation<TR::RecoverableTrampolineError>("Failed to delete the old reservation"); // RAS only
                  }
               else
                  {
                  comp->failCompilation<TR::TrampolineError>("Failed to delete the old reservation"); // RAS only
                  }
               }
            }
         acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(comp, hadVMAccess, hadClassUnloadMonitor);
         }
      }
   else // asking for resolved trampoline
      {
      newCache = getResolvedTrampoline(comp, curCache, (J9Method *)symRef->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod()->getPersistentIdentifier(), inBinaryEncoding);
      }

   if (newCache != curCache)
      {
      comp->cg()->switchCodeCacheTo(newCache);
      }
   TR_ASSERT(newCache->isReserved(), "assertion failure"); // MCT
   }

// interpreter profiling support
TR_IProfiler *
TR_J9VMBase::getIProfiler()
   {
   return NULL;
   }

bool
TR_J9VMBase::isClassLibraryMethod(TR_OpaqueMethodBlock *method, bool vettedForAOT)
   {
   if (!_vmThread || !_vmThread->javaVM)
      return false;
   J9ClassLoader *classLoader = TR::Compiler->cls.convertClassOffsetToClassPtr((TR_OpaqueClassBlock*)J9_CLASS_FROM_METHOD(((J9Method *)method)))->classLoader;
   if (((J9JavaVM *)_vmThread->javaVM)->systemClassLoader == classLoader)
      return true;

   return false;
   }

bool
TR_J9VMBase::isClassLibraryClass(TR_OpaqueClassBlock *clazz)
   {
   if (!_vmThread || !_vmThread->javaVM)
      return false;
   J9ClassLoader *classLoader = TR::Compiler->cls.convertClassOffsetToClassPtr(clazz)->classLoader;
   return ((J9JavaVM *)_vmThread->javaVM)->systemClassLoader == classLoader;
   }

//currently supported in 390, x86 and ppc codegens
bool
TR_J9VMBase::getSupportsRecognizedMethods()
   {
   TR_ASSERT(TR::Compiler->target.cpu.isZ() ||
      TR::Compiler->target.cpu.isX86() ||
      TR::Compiler->target.cpu.isPower() ||
      TR::Compiler->target.cpu.isARM() ||
      TR::Compiler->target.cpu.isARM64() ||
      !isAOT_DEPRECATED_DO_NOT_USE(),
      "getSupportsRecognizedMethods must be called only on X,P,Z or only for non-AOT");
   return true;
   }


int32_t
TR_J9VMBase::getMaxCallGraphCallCount()
   {
   TR_IProfiler *profiler = getIProfiler();

   if (!profiler)
      return -1;

   return profiler->getMaxCallCount();
   }

int32_t
TR_J9VMBase::getIProfilerCallCount(TR_OpaqueMethodBlock *caller, int32_t bcIndex, TR::Compilation * comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      return profiler->getCallCount(caller, bcIndex, comp);

   return -1;
   }

int32_t
TR_J9VMBase::getIProfilerCallCount(TR_OpaqueMethodBlock *callee, TR_OpaqueMethodBlock *caller, int32_t bcIndex, TR::Compilation * comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      return profiler->getCallCount(callee, caller, bcIndex, comp);

   return -1;
   }

int32_t
TR_J9VMBase::getIProfilerCallCount(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      return profiler->getCallCount(bcInfo, comp);

   return 0;
   }

void
TR_J9VMBase::setIProfilerCallCount(TR_OpaqueMethodBlock *caller, int32_t bcIndex, int32_t count, TR::Compilation * comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      profiler->setCallCount(caller, bcIndex, count, comp);
   }

void
TR_J9VMBase::setIProfilerCallCount(TR_ByteCodeInfo &bcInfo, int32_t count, TR::Compilation *comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      profiler->setCallCount(bcInfo, count, comp);
   }

int32_t
TR_J9VMBase::getCGEdgeWeight(TR::Node *callerNode, TR_OpaqueMethodBlock *callee, TR::Compilation *comp)
   {
   TR_IProfiler *profiler = getIProfiler();
   if (profiler)
      return profiler->getCGEdgeWeight(callerNode, callee, comp);

   return 0;
   }

bool
TR_J9VMBase::isCallGraphProfilingEnabled()
   {
   if (getIProfiler())
      {
      return getIProfiler()->isCallGraphProfilingEnabled();
      }

   return false;
   }

TR_AbstractInfo *
TR_J9VMBase::createIProfilingValueInfo( TR_ByteCodeInfo &bcInfo, TR::Compilation *comp)
   {
   TR_IProfiler *iProfiler = getIProfiler();
   if (iProfiler == NULL) return NULL;
   return iProfiler->createIProfilingValueInfo (bcInfo, comp);
   }

TR_ExternalValueProfileInfo *
TR_J9VMBase::getValueProfileInfoFromIProfiler(TR_ByteCodeInfo & bcInfo, TR::Compilation *comp)
   {
   TR_IProfiler *iProfiler = getIProfiler();
   if (iProfiler == NULL) return NULL;
   return iProfiler->getValueProfileInfo (bcInfo, comp);
   }

uint32_t *
TR_J9VMBase::getAllocationProfilingDataPointer(TR_ByteCodeInfo &bcInfo, TR_OpaqueClassBlock *clazz, TR_OpaqueMethodBlock *method, TR::Compilation *comp)
   {
   TR_IProfiler *iProfiler = getIProfiler();
   if (iProfiler == NULL) return NULL;
   return iProfiler->getAllocationProfilingDataPointer(bcInfo, clazz, method, comp);
   }

uint32_t *
TR_J9VMBase::getGlobalAllocationDataPointer()
   {
   TR_IProfiler *iProfiler = getIProfiler();
   if (iProfiler == NULL) return NULL;
   return iProfiler->getGlobalAllocationDataPointer(isAOT_DEPRECATED_DO_NOT_USE());
   }

TR_ExternalProfiler *
TR_J9VMBase:: hasIProfilerBlockFrequencyInfo(TR::Compilation& comp)
   {
   TR_IProfiler *iProfiler = getIProfiler();
   if (iProfiler == NULL) return NULL;
   return iProfiler->canProduceBlockFrequencyInfo(comp);
   }

void
TR_J9VMBase::createHWProfilerRecords(TR::Compilation *comp)
   {
   return;
   }

uint32_t
TR_J9VMBase::getMethodSize(TR_OpaqueMethodBlock *method)
   {
   J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD((J9Method *)method);

   return (uint32_t)(J9_BYTECODE_END_FROM_ROM_METHOD(romMethod) -
                     J9_BYTECODE_START_FROM_ROM_METHOD(romMethod));
   }

int32_t TR_J9VMBase::getLineNumberForMethodAndByteCodeIndex(TR_OpaqueMethodBlock *method, int32_t bcIndex)
   {
   return isAOT_DEPRECATED_DO_NOT_USE() ? -1 : (int32_t)getLineNumberForROMClass(_jitConfig->javaVM, (J9Method *) method, bcIndex);
   }


bool
TR_J9VMBase::isJavaOffloadEnabled()
   {
   #if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
      return (vmThread()->javaVM->javaOffloadSwitchOnWithReasonFunc != NULL);
   #else
      return false;
   #endif
   }


int32_t
TR_J9VMBase::getInvocationCount(TR_OpaqueMethodBlock * methodInfo)
   {
   J9Method * method = (J9Method*)methodInfo;
   return TR::CompilationInfo::getInvocationCount(method);
   }

bool
TR_J9VMBase::setInvocationCount(TR_OpaqueMethodBlock * methodInfo, int32_t oldCount, int32_t newCount)
   {
   J9Method * method = (J9Method*)methodInfo;
   return TR::CompilationInfo::setInvocationCount(method,oldCount,newCount);
   }

bool
TR_J9VMBase::startAsyncCompile(TR_OpaqueMethodBlock * method, void *oldStartPC, bool *queued, TR_OptimizationPlan *optimizationPlan)
   {
   if (_compInfo && _compInfo->useSeparateCompilationThread())
      {
      TR::VMAccessCriticalSection startAsyncCompile(this);

      TR::IlGeneratorMethodDetails details((J9Method *)method);
      _compInfo->compileMethod(vmThread(), details, oldStartPC, TR_yes, NULL, queued, optimizationPlan);

      return true;
      }
   return false;
   }

bool
TR_J9VMBase::isBeingCompiled(TR_OpaqueMethodBlock * method, void * startPC)
   {
   return _compInfo->isQueuedForCompilation((J9Method *)method, startPC);
   }

U_32
TR_J9VMBase::vTableSlotToVirtualCallOffset(U_32 vTableSlot)
   {
   return TR::Compiler->vm.getInterpreterVTableOffset() - vTableSlot;
   }

U_32
TR_J9VMBase:: virtualCallOffsetToVTableSlot(U_32 offset)
   {
   return TR::Compiler->vm.getInterpreterVTableOffset() - offset;
   }

void *
TR_J9VMBase:: addressOfFirstClassStatic(TR_OpaqueClassBlock * j9Class)
   {
   return (void *)(TR::Compiler->cls.convertClassOffsetToClassPtr(j9Class)->ramStatics);
   }

U_32
TR_J9VMBase::offsetOfIsOverriddenBit()
   {
   return J9_STARTPC_METHOD_IS_OVERRIDDEN;
   }

void *
TR_J9VMBase::getStaticFieldAddress(TR_OpaqueClassBlock * clazz, unsigned char * fieldName, uint32_t fieldLen,
                                   unsigned char * sig, uint32_t sigLen)
   {
   TR::VMAccessCriticalSection getStaticFieldAddress(this);
   void * result = vmThread()->javaVM->internalVMFunctions->staticFieldAddress(
    vmThread(), TR::Compiler->cls.convertClassOffsetToClassPtr(clazz), fieldName, fieldLen, sig, sigLen, NULL, NULL, J9_LOOK_NO_JAVA,  NULL);
   return result;
   }

int32_t
TR_J9VMBase::getInterpreterVTableSlot(TR_OpaqueMethodBlock * mBlock, TR_OpaqueClassBlock * clazz)
   {
   TR::VMAccessCriticalSection getInterpreterVTableSlot(this);
   int32_t result =  vmThread()->javaVM->internalVMFunctions->getVTableOffsetForMethod((J9Method*)mBlock, (J9Class*)clazz, vmThread());
   return result;
   }
int32_t
TR_J9VMBase::getVTableSlot(TR_OpaqueMethodBlock * mBlock, TR_OpaqueClassBlock * clazz)
   {
   return TR::Compiler->vm.getInterpreterVTableOffset() - getInterpreterVTableSlot(mBlock, clazz);
   }
uint64_t
TR_J9VMBase::getUSecClock()
   {
   PORT_ACCESS_FROM_JITCONFIG(_jitConfig);
   return j9time_usec_clock();
   }

uint64_t
TR_J9VMBase::getHighResClock()
   {
   PORT_ACCESS_FROM_JITCONFIG(_jitConfig);
   return j9time_hires_clock();
   }

uint64_t
TR_J9VMBase::getHighResClockResolution()
   {
   PORT_ACCESS_FROM_JITCONFIG(_jitConfig);
   return j9time_hires_frequency();
   }

TR_JitPrivateConfig *
TR_J9VMBase::getPrivateConfig()
   {
   return (TR_JitPrivateConfig*) _jitConfig->privateConfig;
   }

TR_JitPrivateConfig *
TR_J9VMBase::getPrivateConfig(void *jitConfig)
   {
   return (TR_JitPrivateConfig*) ((J9JITConfig*)jitConfig)->privateConfig;
   }

void
TR_J9VMBase::revertToInterpreted(TR_OpaqueMethodBlock * method)
   {
   revertMethodToInterpreted((J9Method*)method);
   }

int32_t *
TR_J9VMBase::getStringClassEnableCompressionFieldAddr(TR::Compilation *comp, bool isVettedForAOT)
   {
   if (!TR_J9VMBase::staticStringEnableCompressionFieldAddr) // Not yet cached
      {
      int32_t *enableCompressionFieldAddr = NULL;
      TR_OpaqueClassBlock *stringClass = getSystemClassFromClassName("java/lang/String", 16, isVettedForAOT);
      if (stringClass)
         {
         TR_PersistentClassInfo * classInfo = (comp->getPersistentInfo()->getPersistentCHTable() == NULL) ?
            NULL :
            comp->getPersistentInfo()->getPersistentCHTable()->findClassInfoAfterLocking(stringClass, comp, isVettedForAOT);
         if (classInfo && classInfo->isInitialized())
            {
            enableCompressionFieldAddr = (int32_t *)getStaticFieldAddress(stringClass,
               (unsigned char *)"COMPACT_STRINGS", 15, (unsigned char *)"Z", 1);
            if (enableCompressionFieldAddr)
               {
               // Cache the address
               TR_J9VMBase::staticStringEnableCompressionFieldAddr = enableCompressionFieldAddr;
               }
            }
         }
      }
   return TR_J9VMBase::staticStringEnableCompressionFieldAddr;
   }


extern "C" {
void revertMethodToInterpreted(J9Method * method)
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   compInfo->acquireCompilationLock();
   J9JIT_REVERT_METHOD_TO_INTERPRETED(jitConfig->javaVM, method);
   compInfo->releaseCompilationLock();
   }
}


// See if a call argument can escape the current method via the call
//

struct TrustedClass
   {
   char   * name;
   int32_t length;
   int32_t argNum;
   };

static TrustedClass trustedClasses[] =
   {
   "java/lang/String",       16, -1, // trusted in both jclMax and J2SE
   "java/lang/StringBuffer", 22, -1, // trusted in both jclMax and J2SE
   "java/util/Hashtable",    19,  0, // trusted in both jclMax and J2SE
   "java/util/Vector",       16,  0, // trusted in both jclMax and J2SE
   "java/io/DataInputStream",23,  0, // not yet trusted in J2SE
   "java/io/File",           12,  0, // not yet trusted in J2SE
   "java/net/URL",           12,  0, // not yet trusted in J2SE
   "java/util/Stack",        15,  0, // not yet trusted in J2SE
   NULL,                      0,  0  // Mark end of trusted classes
   };

struct TrustedMethod
   {
   TR::RecognizedMethod method;
   int32_t                                   argNum;
   };

static TrustedMethod trustedMethods[] =
   {
   TR::java_util_Vector_contains,  1,
   TR::unknownMethod,             -1    // Mark end of trusted methods
   };

static TrustedMethod untrustedMethods[] =
   {
   TR::java_util_Vector_subList,  -1,
   TR::unknownMethod,             -1    // Mark end of untrusted methods
   };

bool
TR_J9VMBase::argumentCanEscapeMethodCall(TR::MethodSymbol * method, int32_t argIndex)
   {
   int32_t numberOfTrustedClasses = INT_MAX;
   if (_jitConfig->javaVM->j2seVersion != 0)
      numberOfTrustedClasses = 4;

   int32_t i;
   TR::RecognizedMethod methodId = method->getRecognizedMethod();

   // See if the method is a member of a trusted class. If so the argument
   // will not escape via this call, unless it is in the list of untrusted
   // methods.
   //
   char * className    = method->getMethod()->classNameChars();
   int32_t nameLength = method->getMethod()->classNameLength();

   for (i = 0; trustedClasses[i].name && i < numberOfTrustedClasses; ++i)
      {
      if (nameLength == trustedClasses[i].length &&
          !strncmp(className, trustedClasses[i].name, nameLength))
         {
         if (trustedClasses[i].argNum < 0 ||
             trustedClasses[i].argNum == argIndex)
            {
            if (methodId == TR::unknownMethod)
               return false;

            for (i = 0; untrustedMethods[i].method != TR::unknownMethod; ++i)
               {
               if (untrustedMethods[i].method == methodId)
                  {
                  if (untrustedMethods[i].argNum < 0 ||
                      untrustedMethods[i].argNum == argIndex)
                     {
                     return true;
                     }
                  }
               }
            return false;
            }
         }
      }

   // See if the method is a trusted method for the argument in this position.
   // If so, the argument will not escape via this call.
   //
   // Note that an argument position of "-1" in the list of trusted methods
   // means that any argument is OK.
   //
   if (methodId == TR::unknownMethod)
      return true;

   for (i = 0; trustedMethods[i].method != TR::unknownMethod; ++i)
      {
      if (trustedMethods[i].method == methodId)
         {
         if (trustedMethods[i].argNum < 0 ||
             trustedMethods[i].argNum == argIndex)
            {
            return false;
            }
         }
      }

   return true;
   }

bool
TR_J9VMBase::isThunkArchetype(J9Method * method)
   {
#if defined(J9VM_OPT_METHOD_HANDLE)
   J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
   J9ROMClass  *romClass  = J9_CLASS_FROM_METHOD(method)->romClass;
   if (_J9ROMMETHOD_J9MODIFIER_IS_SET(romMethod, J9AccMethodFrameIteratorSkip))
      {
      J9UTF8 *classUTF8 = J9ROMCLASS_CLASSNAME(romClass);
      const char *jliPrefix = "java/lang/invoke";

      bool isInJLI =
           J9UTF8_LENGTH(classUTF8) >= strlen(jliPrefix)
        && !strncmp((char*)J9UTF8_DATA(classUTF8), jliPrefix, strlen(jliPrefix));

      J9UTF8 *nameUTF8  = J9ROMMETHOD_NAME(romMethod);
      const char *thunkArchetypePrefix = "invokeExact_thunkArchetype_";

      bool isThunkArchetype =
           J9UTF8_LENGTH(nameUTF8) >= strlen(thunkArchetypePrefix)
        && !strncmp((char*)J9UTF8_DATA(nameUTF8), thunkArchetypePrefix, strlen(thunkArchetypePrefix));

      return isInJLI && isThunkArchetype;
      }
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */
   return false;
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getHostClass(TR_OpaqueClassBlock *clazzOffset)
   {
   J9Class *clazzPtr = TR::Compiler->cls.convertClassOffsetToClassPtr(clazzOffset);
   return convertClassPtrToClassOffset(clazzPtr->hostClass);
   }

bool
TR_J9VMBase::canAllocateInlineClass(TR_OpaqueClassBlock *clazzOffset)
   {
   J9Class *clazz = TR::Compiler->cls.convertClassOffsetToClassPtr(clazzOffset);
   // Can not inline the allocation if the class is not fully initialized
   if (clazz->initializeStatus != 1)
      return false;

   // Can not inline the allocation if the class is an interface, abstract or identityless
   // (a value type) or if it is a class with identityless fields
   if ((clazz->romClass->modifiers & (J9AccAbstract | J9AccInterface | J9AccValueType))
       || (clazz->classFlags & J9ClassContainsUnflattenedFlattenables))
      return false;
   return true;
   }

bool
TR_J9VMBase::isClassLoadedBySystemClassLoader(TR_OpaqueClassBlock *clazz)
   {
   return getSystemClassLoader() == getClassLoader(clazz);
   }

intptr_t
TR_J9VMBase::getVFTEntry(TR_OpaqueClassBlock *clazz, int32_t offset)
   {
   return *(intptr_t*) (((uint8_t *)clazz) + offset);
   }

TR_OpaqueClassBlock *
TR_J9VMBase::convertClassPtrToClassOffset(J9Class *clazzPtr)
   {
   //return (TR_OpaqueClassBlock*)clazzPtr;
   // NOTE : We could pass down vmThread() in the call below if the conversion
   // required the VM thread. Currently it does not. If we did change that
   // such that the VM thread was reqd, we would need to handle AOT where the
   // TR_FrontEnd is created with a NULL J9VMThread object.
   //
   return J9JitMemory::convertClassPtrToClassOffset(clazzPtr);
   }

J9Method *
TR_J9VMBase::convertMethodOffsetToMethodPtr(TR_OpaqueMethodBlock *methodOffset)
   {
   return J9JitMemory::convertMethodOffsetToMethodPtr(methodOffset);
   }

TR_OpaqueMethodBlock *
TR_J9VMBase::convertMethodPtrToMethodOffset(J9Method *methodPtr)
   {
   return J9JitMemory::convertMethodPtrToMethodOffset(methodPtr);
   }

const char *
TR_J9VMBase::getJ9MonitorName(J9ThreadMonitor* monitor) { return monitor->name; }

TR_OpaqueClassBlock *
TR_J9VMBase::getClassFromNewArrayType(int32_t arrayType)
   {
   struct J9Class ** arrayClasses = &_jitConfig->javaVM->booleanArrayClass;
   return convertClassPtrToClassOffset(arrayClasses[arrayType - 4]);
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getClassFromNewArrayTypeNonNull(int32_t arrayType)
   {
   // This query is needed for inline allocation, which requires array class to
   // be non-NULL, but getClassFromNewArrayType returns NULL in AOT mode
   auto clazz = TR_J9VMBase::getClassFromNewArrayType(arrayType);
   TR_ASSERT(clazz, "class must not be NULL");
   return clazz;
   }

bool
TR_J9VMBase::getReportByteCodeInfoAtCatchBlock()
   {
   return _compInfoPT->getCompilation()->getOptions()->getReportByteCodeInfoAtCatchBlock();
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getClassFromCP(J9ConstantPool *cp)
   {
   return reinterpret_cast<TR_OpaqueClassBlock *>(J9_CLASS_FROM_CP(cp));
   }

J9ROMMethod *
TR_J9VMBase::getROMMethodFromRAMMethod(J9Method *ramMethod)
   {
   return fsdIsEnabled() ? getOriginalROMMethod(ramMethod) : J9_ROM_METHOD_FROM_RAM_METHOD(ramMethod);
   }

/////////////////////////////////////////////////////
// TR_J9VM
/////////////////////////////////////////////////////

TR_J9VM::TR_J9VM(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo, J9VMThread * vmThread)
   : TR_J9VMBase(jitConfig, compInfo, vmThread)
   {
   }

//d169771 [2177]
uintptr_t
TR_J9VMBase::getPersistentClassPointerFromClassPointer(TR_OpaqueClassBlock * clazz)
   {
   return (uintptr_t)TR::Compiler->cls.romClassOf(clazz);
   }

void *
TR_J9VMBase::getClassLoader(TR_OpaqueClassBlock * classPointer)
   {
   return (void *) (TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)->classLoader);
   }

void *
TR_J9VMBase::getLocationOfClassLoaderObjectPointer(TR_OpaqueClassBlock *classPointer)
   {
   return (void *)&(TMP_J9CLASSLOADER_CLASSLOADEROBJECT(TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)->classLoader));
   }


int32_t
TR_J9VMBase::maxInternalPlusPinningArrayPointers(TR::Compilation* comp)
 {
 if (comp->getOption(TR_DisableInternalPointers))
 return 0;
 else
 return 256;
 }


TR_OpaqueClassBlock *
TR_J9VM::getClassOfMethod(TR_OpaqueMethodBlock *method)
   {
   return (TR_OpaqueClassBlock *)J9_CLASS_FROM_METHOD(((J9Method *)method));
   }


I_32
TR_J9VMBase::getLocalObjectAlignmentInBytes()
   {
   return 1 << TR::Compiler->om.compressedReferenceShift();
   }

I_32
TR_J9VM::getObjectAlignmentInBytes()
   {
   J9JavaVM *jvm = _jitConfig->javaVM;
   if (!jvm)
      return 0;
   J9MemoryManagerFunctions * mmf = jvm->memoryManagerFunctions;
   uintptr_t result = 0;
   result = mmf->j9gc_modron_getConfigurationValueForKey(jvm, j9gc_modron_configuration_objectAlignment, &result) ? result : 0;
   return (I_32)result;
   }


TR_ResolvedMethod *
TR_J9VM::getObjectNewInstanceImplMethod(TR_Memory * trMemory)
   {
   TR_OpaqueMethodBlock *protoMethod = getObjectNewInstanceImplMethod();
   TR_ResolvedMethod * result = createResolvedMethod(trMemory, protoMethod, 0);
   return result;
   }

TR_OpaqueMethodBlock *
TR_J9VM::getObjectNewInstanceImplMethod()
   {
   TR::VMAccessCriticalSection getObjectNewInstanceImplMethod(this);
   J9Method * protoMethod;
   J9InternalVMFunctions * intFunc = vmThread()->javaVM->internalVMFunctions;

   // make sure that internal functions return true class pointers
   J9Class * jlObject = intFunc->internalFindKnownClass(vmThread(), J9VMCONSTANTPOOL_JAVALANGOBJECT, J9_FINDKNOWNCLASS_FLAG_EXISTING_ONLY);
   protoMethod = (J9Method *) intFunc->javaLookupMethod(vmThread(), jlObject, (J9ROMNameAndSignature *) &newInstancePrototypeNameAndSig, NULL, J9_LOOK_DIRECT_NAS | J9_LOOK_VIRTUAL | J9_LOOK_NO_JAVA);
   protoMethod->constantPool = (J9ConstantPool *) ((UDATA) protoMethod->constantPool | J9_STARTPC_METHOD_IS_OVERRIDDEN);
   return (TR_OpaqueMethodBlock *)protoMethod;
   }

uintptr_t
TR_J9VMBase::getBytecodePC(TR_OpaqueMethodBlock *method, TR_ByteCodeInfo &bcInfo)
   {
   uintptr_t methodStart = TR::Compiler->mtd.bytecodeStart(method);
   return methodStart + (uintptr_t)(bcInfo.getByteCodeIndex());
   }


//Given a class signature classSig, find the symbol references of all classSig's methods
//whose signatures are given in methodSig. methodCount is the number of methods.
//Returns the number of methods found in the class
//This function handles static and virtual functions only.
int TR_J9VMBase::findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* classSig, char** methodSig, TR::SymbolReference** symRefs, int methodCount)
   {
   TR_OpaqueClassBlock *c = getClassFromSignature(classSig,
                                                  strlen(classSig),
                                                  comp->getCurrentMethod());
   if (!c)
      {
      if (comp->getOption(TR_TraceILGen))
         traceMsg(comp, "class %s not found\n", classSig);
      return 0;
      }

   // From here, on, stack memory allocations will expire / die when the function returns
   TR::StackMemoryRegion stackMemoryRegion(*comp->trMemory());

   TR_ScratchList<TR_ResolvedMethod> methods(comp->trMemory());
   getResolvedMethods(comp->trMemory(), c, &methods);
   ListIterator<TR_ResolvedMethod> it(&methods);
   int numMethodsFound = 0;
   //I couldn't make "new (comp->trStackMemory()) int[methodCount]" to compile so I
   //wrote the following ugly code. Please fix this if you know how to do that
   int *methodSigLen = (int*)comp->trMemory()->allocateStackMemory(sizeof(int)*methodCount);
   for (int i = 0; i < methodCount; i++)
      {
      methodSigLen[i] = strlen(methodSig[i]);
      if (symRefs[i])
         numMethodsFound++;
      }
   for (TR_ResolvedMethod *method = it.getCurrent(); method && numMethodsFound < methodCount; method = it.getNext())
      {
      if (method->isConstructor()) continue;
      const char *sig = method->signature(comp->trMemory());
      int32_t len = strlen(sig);
      for (int i = 0; i < methodCount; i++)
         {
         if (!symRefs[i] && !strncmp(sig, methodSig[i], methodSigLen[i]))
            {
            if (method->isStatic())
               {
               symRefs[i] = comp->getSymRefTab()->
                  findOrCreateMethodSymbol(owningMethodSym
                                           ?owningMethodSym->getResolvedMethodIndex()
                                           :JITTED_METHOD_INDEX,
                                           -1, method, TR::MethodSymbol::Static);
               }
            else
               {
               symRefs[i] = comp->getSymRefTab()->
                  findOrCreateMethodSymbol(owningMethodSym
                                           ?owningMethodSym->getResolvedMethodIndex()
                                           :JITTED_METHOD_INDEX,
                                           -1, method, TR::MethodSymbol::Virtual);
               symRefs[i]->setOffset(getVTableSlot(method->getPersistentIdentifier() ,c));
               }
            numMethodsFound++;
            }
         }
      }

   return numMethodsFound;
   }

//Given a class signature and a method signature returns a symref for that method.
//If the method is not resolved or doesn't exist in that class, it returns NULL
//This function handles static and virtual functions only.
TR::SymbolReference* TR_J9VMBase::findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* classSig, char* methodSig)
   {
   TR::SymbolReference* symRef = NULL;
   int numMethodsFound = findOrCreateMethodSymRef(comp, owningMethodSym, classSig, &methodSig, &symRef, 1);
   return ( numMethodsFound == 1)? symRef : NULL;
   }

//Given a complete method signature (i.e., including the full class name)
//returns a symref for that method. If the method is not resolved or doesn't exist
//in that class, it returns NULL
//This function handles static and virtual functions only.
TR::SymbolReference* TR_J9VMBase::findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char* methodSig) {
   int methodSigLen = strlen(methodSig);
   char* classSig = (char*)comp->trMemory()->allocateStackMemory(sizeof(char)*methodSigLen);
   char* separator = strchr(methodSig, '.');
   TR_ASSERT(separator, ". not found in method name");
   int classSigLen = separator - methodSig;
   strncpy(classSig, methodSig, classSigLen);
   classSig[classSigLen] = 0;
   TR::SymbolReference* result = findOrCreateMethodSymRef(comp, owningMethodSym, classSig, methodSig);
   return result;
}

//Given an array of full method signatures (i.e., including full class name), this method
//gives symrefs for those methods. The number of input methods are given in methodCount.
//The function returns the number of methods found
//this function handles static and virtual functions only
int TR_J9VMBase::findOrCreateMethodSymRef(TR::Compilation* comp, TR::ResolvedMethodSymbol* owningMethodSym, char** methodSig, TR::SymbolReference** symRefs, int methodCount) {
   int numMethodsFound = 0;
   for (int i = 0; i < methodCount; i++) {
      if (!methodSig[i]) continue;
      symRefs[i] = findOrCreateMethodSymRef(comp, owningMethodSym, methodSig[i]);
      if (symRefs[i])
    numMethodsFound++;
   }
   return numMethodsFound;
}

//returns true if a subclass of java.util.concurrent.locks.AbstractOwnableSynchronizer
bool
TR_J9VMBase::isOwnableSyncClass(TR_OpaqueClassBlock *clazz)
   {
   J9Class* j9class = TR::Compiler->cls.convertClassOffsetToClassPtr(clazz);
   return ((J9CLASS_FLAGS(j9class) & J9AccClassOwnableSynchronizer) != 0);
   }

const char *
TR_J9VMBase::getByteCodeName(uint8_t opcode)
   {
   return JavaBCNames[opcode];
   }

void
TR_J9VMBase::getResolvedMethods(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer, List<TR_ResolvedMethod> * resolvedMethodsInClass)
   {
   TR::VMAccessCriticalSection getResolvedMethods(this); // Prevent HCR
   J9Method * resolvedMethods = (J9Method *) getMethods(classPointer);
   uint32_t i;
   uint32_t numMethods = getNumMethods(classPointer);
   for (i=0;i<numMethods;i++)
      {
      resolvedMethodsInClass->add(createResolvedMethod(trMemory, (TR_OpaqueMethodBlock *) &(resolvedMethods[i]), 0));
      }
   }

/*
 * Should be called with VMAccess
 */
TR_OpaqueMethodBlock *
TR_J9VMBase::getMatchingMethodFromNameAndSignature(TR_OpaqueClassBlock * classPointer,
                                                   const char* methodName, const char *signature, bool validate)
   {
   size_t nameLength = strlen(methodName);
   size_t sigLength = strlen(signature);

   J9ROMClass *romClass = TR::Compiler->cls.romClassOf(classPointer);
   J9Method *j9Methods = (J9Method *)getMethods(classPointer);
   uint32_t numMethods = getNumMethods(classPointer);

   J9ROMMethod *romMethod = J9ROMCLASS_ROMMETHODS(romClass);

   TR_OpaqueMethodBlock *method = NULL;

   // Iterate over all romMethods until the desired one is found
   for (uint32_t i = 0; i < numMethods; i++)
      {
      J9UTF8 *mName = J9ROMMETHOD_NAME(romMethod);
      J9UTF8 *mSig = J9ROMMETHOD_SIGNATURE(romMethod);
      if (J9UTF8_LENGTH(mName) == nameLength &&
         J9UTF8_LENGTH(mSig) == sigLength &&
         memcmp(utf8Data(mName), methodName, nameLength) == 0 &&
         memcmp(utf8Data(mSig), signature, sigLength) == 0)
         {
         method = (TR_OpaqueMethodBlock *)(j9Methods + i);
         if (validate)
            {
            TR::Compilation *comp = TR::comp();
            if (comp && comp->getOption(TR_UseSymbolValidationManager))
               {
               comp->getSymbolValidationManager()->addMethodFromClassRecord(method, classPointer, i);
               }
            }
         break;
         }
      romMethod = nextROMMethod(romMethod);
      }

   return method;
   }

TR_ResolvedMethod *
TR_J9VMBase::getResolvedMethodForNameAndSignature(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer,
                                                  const char* methodName, const char *signature)
   {
   TR::VMAccessCriticalSection vmCS(this); // Prevent HCR
   TR_ResolvedMethod *rm = NULL;

   TR_OpaqueMethodBlock *method = getMatchingMethodFromNameAndSignature(classPointer, methodName, signature);
   if (method)
      rm = createResolvedMethod(trMemory, method, 0);

   return rm;
   }



void *
TR_J9VMBase::getMethods(TR_OpaqueClassBlock * classPointer)
   {
   return (J9Method *) TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)->ramMethods;
   }

uint32_t
TR_J9VMBase::getNumMethods(TR_OpaqueClassBlock * classPointer)
   {
   return TR::Compiler->cls.romClassOf(classPointer)->romMethodCount;
   }

/* This API takes a J9Class (TR_OpaqueClassBlock) and a J9Method (TR_OpaqueMethodBlock) and returns the
 * index of the J9Method in the J9Class' array of J9Methods.
 */
uintptr_t
TR_J9VMBase::getMethodIndexInClass(TR_OpaqueClassBlock *classPointer, TR_OpaqueMethodBlock *methodPointer)
   {
   void *methodsInClass = getMethods(classPointer);
   uint32_t numMethods = getNumMethods(classPointer);

   uintptr_t methodOffset = reinterpret_cast<uintptr_t>(methodPointer) - reinterpret_cast<uintptr_t>(methodsInClass);
   TR_ASSERT_FATAL((methodOffset % sizeof (J9Method)) == 0, "methodOffset %llx isn't a multiple of sizeof(J9Method)\n",
                                                             static_cast<uint64_t>(methodOffset));

   uintptr_t methodIndex = methodOffset / sizeof (J9Method);
   TR_ASSERT_FATAL(methodIndex < numMethods, "methodIndex %llx greater than numMethods %llx for method %p in class %p\n",
                                              static_cast<uint64_t>(methodIndex),
                                              static_cast<uint64_t>(numMethods),
                                              methodPointer, classPointer);

   return methodIndex;
   }

uint32_t
TR_J9VMBase::getNumInnerClasses(TR_OpaqueClassBlock * classPointer)
   {
   return TR::Compiler->cls.romClassOf(classPointer)->innerClassCount;
   }


uint32_t
TR_J9VMBase::getInstanceFieldOffsetIncludingHeader(char* classSignature, char * fieldName, char * fieldSig,
   TR_ResolvedMethod* method)
   {
   TR_OpaqueClassBlock *classBlock=getClassFromSignature(classSignature, strlen(classSignature), method, true);
   return getInstanceFieldOffset(classBlock, fieldName, fieldSig) + getObjectHeaderSizeInBytes();
   }


TR_OpaqueClassBlock *
TR_J9VMBase::getProfiledClassFromProfiledInfo(TR_ExtraAddressInfo *profiledInfo)
   {
   return (TR_OpaqueClassBlock *)(profiledInfo->_value);
   }

uint32_t
TR_J9VMBase::getInstanceFieldOffset(TR_OpaqueClassBlock * clazz, char * fieldName, uint32_t fieldLen,
                               char * sig, uint32_t sigLen, UDATA options)
   {
   TR::VMAccessCriticalSection getInstanceFieldOffset(this);
   TR_ASSERT(clazz, "clazz should be set!");
   uint32_t result = (uint32_t) vmThread()->javaVM->internalVMFunctions->instanceFieldOffset(
                        vmThread(), (J9Class *)clazz, (unsigned char *) fieldName,
                        fieldLen, (unsigned char *)sig, sigLen, (J9Class **)&clazz, (UDATA *)NULL, options);
   if (result == FIELD_OFFSET_NOT_FOUND)
      return ~0;
   return result;
   }

uint32_t
TR_J9VMBase::getInstanceFieldOffset(TR_OpaqueClassBlock * clazz, char * fieldName, uint32_t fieldLen,
                                    char * sig, uint32_t sigLen)
   {
   return getInstanceFieldOffset(clazz, fieldName, fieldLen, sig, sigLen, J9_LOOK_NO_JAVA);
   }

TR_OpaqueClassBlock *
TR_J9VM::getSuperClass(TR_OpaqueClassBlock * classPointer)
   {
   J9Class * clazz = TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer);
   UDATA classDepth = J9CLASS_DEPTH(clazz) - 1;
   return convertClassPtrToClassOffset(classDepth >= 0 ? clazz->superclasses[classDepth]: 0);
   }

bool
TR_J9VM::isSameOrSuperClass(J9Class * superClass, J9Class * subClass)
   {
   return VM_VMHelpers::isSameOrSuperclass(superClass, subClass);
   }

bool
TR_J9VMBase::sameClassLoaders(TR_OpaqueClassBlock * class1, TR_OpaqueClassBlock * class2)
   {
   return (getClassLoader(class1) == getClassLoader(class2));
   }

bool
TR_J9VM::isUnloadAssumptionRequired(TR_OpaqueClassBlock * clazzPointer, TR_ResolvedMethod * methodBeingCompiled)
   {
   TR_OpaqueClassBlock *classOfMethod = methodBeingCompiled->classOfMethod();

   if (clazzPointer == classOfMethod ||
       ((getClassLoader(clazzPointer) == getSystemClassLoader() ||
       sameClassLoaders(clazzPointer, classOfMethod)) && !isAnonymousClass(clazzPointer))
      )
      return false;

   return true;
   }

bool
TR_J9VM::classHasBeenExtended(TR_OpaqueClassBlock * clazzPointer)
   {
   return (J9CLASS_FLAGS(TR::Compiler->cls.convertClassOffsetToClassPtr(clazzPointer)) & J9AccClassHasBeenOverridden) != 0;
   }

bool
TR_J9VM::classHasBeenReplaced(TR_OpaqueClassBlock * clazzPointer)
   {
   return (J9CLASS_FLAGS(TR::Compiler->cls.convertClassOffsetToClassPtr(clazzPointer)) & J9AccClassHotSwappedOut) != 0;
   }

bool
TR_J9VMBase::isGetImplInliningSupported()
   {
   J9JavaVM * jvm = _jitConfig->javaVM;
   return jvm->memoryManagerFunctions->j9gc_modron_isFeatureSupported(jvm, j9gc_modron_feature_inline_reference_get) != 0;
   }

/** \brief
 *     Get the raw modifier from the class pointer.
 *
 *  \param clazzPointer
 *     The class whose raw modifier is requested.
 *
 *  \return
 *     The raw modifier of clazzPointer.
 *
 *  \note
 *     Value should not be inspected by the JIT but simply returned, capability queries should be used to extract bits from the field.
 */
bool
TR_J9VMBase::javaLangClassGetModifiersImpl(TR_OpaqueClassBlock * clazzPointer, int32_t &result)
   {
   J9ROMClass *romClass = NULL;

   bool isArray = isClassArray(clazzPointer);
   if (isArray)
      romClass = TR::Compiler->cls.romClassOf(getLeafComponentClassFromArrayClass(clazzPointer));
   else
      romClass = TR::Compiler->cls.romClassOf(clazzPointer);

   result = 0;
   if (J9_ARE_NO_BITS_SET(romClass->extraModifiers, J9AccClassInnerClass))
      {
      // Not an inner class - use the modifiers field
      result = romClass->modifiers;
      }
   else
      {
      // Use memberAccessFlags if the receiver is an inner class
      result = romClass->memberAccessFlags;
      }

   if (isArray)
      {
      // OR in the required Sun bits
      result |= (J9AccAbstract + J9AccFinal);
      }
   return true;
   }

/** \brief
 *     Get hash code for the java/lang/Class object for a class
 *
 *  \param comp
 *     The compilation object.
 *
 *  \param clazzPointer
 *     The J9Class pointer whose java/lang/Class object's hash code is requested.
 *
 *  \param hashCodeComputed
 *     A boolean to be modified by the API to reflect whether the query is successful.

 *  \return
 *     The hash code of clazzPointer's java/lang/Class object if the query is successful (indicated by hashCodeComputed), otherwise return 0.
 */
int32_t
TR_J9VMBase::getJavaLangClassHashCode(TR::Compilation * comp, TR_OpaqueClassBlock * clazzPointer, bool &hashCodeComputed)
   {
   J9MemoryManagerFunctions * mmf = jitConfig->javaVM->memoryManagerFunctions;
   bool haveAcquiredVMAccess = false;
   if (tryToAcquireAccess(comp, &haveAcquiredVMAccess))
      {
      uintptr_t structure = (uintptr_t) clazzPointer;
      J9Object *ref = *(J9Object **)(structure + getOffsetOfJavaLangClassFromClassField());
      int32_t hashCode = mmf->j9gc_objaccess_getObjectHashCode(jitConfig->javaVM, ref);

      if (haveAcquiredVMAccess)
         releaseAccess(comp);

      hashCodeComputed = true;
      return hashCode;
      }
   else
      hashCodeComputed = false;

   return 0;
   }

bool
TR_J9VMBase::isInterfaceClass(TR_OpaqueClassBlock * clazzPointer)
   {
   return (TR::Compiler->cls.romClassOf(clazzPointer)->modifiers & J9AccInterface) ? true : false;
   }

bool
TR_J9VMBase::isEnumClass(TR_OpaqueClassBlock * clazzPointer, TR_ResolvedMethod *method)
   {
   int32_t modifiersForClass = 0;
   if (isClassArray(clazzPointer))
      return false;
   bool success = javaLangClassGetModifiersImpl(clazzPointer, modifiersForClass);
   if (!success)
      return false;
   bool isModFlagSet = (modifiersForClass & J9AccEnum) ? true : false;
   TR_OpaqueClassBlock *enumClass = getClassFromSignature("java/lang/Enum", 14, method);
   TR_OpaqueClassBlock *superClass = getSuperClass(clazzPointer);
   return (isModFlagSet && (enumClass == superClass));
   }


bool
TR_J9VMBase::isAbstractClass(TR_OpaqueClassBlock * clazzPointer)
   {
   return ((TR::Compiler->cls.romClassOf(clazzPointer)->modifiers & (J9AccInterface | J9AccAbstract)) == J9AccAbstract) ? true : false;
   }

bool
TR_J9VMBase::isConcreteClass(TR_OpaqueClassBlock * clazzPointer)
   {
   return (TR::Compiler->cls.romClassOf(clazzPointer)->modifiers & (J9AccAbstract | J9AccInterface)) ? false : true;
   }

bool
TR_J9VM::isPublicClass(TR_OpaqueClassBlock * clazz)
   {
   return (TR::Compiler->cls.romClassOf(clazz)->modifiers & J9AccPublic) ? true : false;
   }

bool
TR_J9VMBase::hasFinalizer(TR_OpaqueClassBlock * classPointer)
   {
   return (J9CLASS_FLAGS(TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)) & (J9AccClassFinalizeNeeded | J9AccClassOwnableSynchronizer)) != 0;
   }

uintptr_t
TR_J9VMBase::getClassDepthAndFlagsValue(TR_OpaqueClassBlock * classPointer)
   {
   return (TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)->classDepthAndFlags);
   }

uintptr_t
TR_J9VMBase::getClassFlagsValue(TR_OpaqueClassBlock * classPointer)
   {
   return (TR::Compiler->cls.convertClassOffsetToClassPtr(classPointer)->classFlags);
   }

#define LOOKUP_OPTION_JNI 1024
#define LOOKUP_OPTION_NO_CLIMB 32
#define LOOKUP_OPTION_NO_THROW 8192

TR_OpaqueMethodBlock *
TR_J9VM::getMethodFromName(char *className, char *methodName, char *signature)
   {
   TR::VMAccessCriticalSection getMethodFromName(this);
   TR_OpaqueClassBlock *methodClass = getSystemClassFromClassName(className, strlen(className), true);
   TR_OpaqueMethodBlock * result = NULL;
   if (methodClass)
      result = (TR_OpaqueMethodBlock *)getMethodFromClass(methodClass, methodName, signature);

   return result;
   }

/** \brief
 *     Look up a method from a given class by name
 *
 *  \parm methodClass
 *     Class of the method
 *
 *  \parm methodName
 *     The method name in a char array
 *
 *  \parm signature
 *     The method signature in a char array
 *
 *  \parm callingClass
 *     The caller class where the method will be used
 *
 *  \return
 *     The method being asked for when look up succeeds or null when it fails.
 *
 *  \note
 *     If callingClass is non-null, a visibility check will be done during the look up.
 *     Only methods visible to the callingClass will be returned.
 */
TR_OpaqueMethodBlock *
TR_J9VMBase::getMethodFromClass(TR_OpaqueClassBlock * methodClass, char * methodName, char * signature, TR_OpaqueClassBlock * callingClass)
   {
   J9JNINameAndSignature nameAndSig;
   nameAndSig.name = methodName;
   nameAndSig.nameLength = (U_32)strlen(methodName);
   nameAndSig.signature = signature;
   nameAndSig.signatureLength = (U_32) strlen(signature);
   TR_OpaqueMethodBlock *result;

      {
      TR::VMAccessCriticalSection getMethodFromClass(this);
      result = (TR_OpaqueMethodBlock *) vmThread()->javaVM->internalVMFunctions->javaLookupMethod(vmThread(),
         (J9Class *)methodClass, (J9ROMNameAndSignature *) &nameAndSig, (J9Class *)callingClass, J9_LOOK_JNI | J9_LOOK_NO_JAVA);
      }

   return result;
   }

const char *
TR_J9VM::sampleSignature(TR_OpaqueMethodBlock * aMethod, char *buf, int32_t bufLen, TR_Memory *memory)
   {
   J9UTF8 * className;
   J9UTF8 * name;
   J9UTF8 * signature;
   getClassNameSignatureFromMethod(((J9Method *)aMethod), className, name, signature);

   int32_t len = J9UTF8_LENGTH(className)+J9UTF8_LENGTH(name)+J9UTF8_LENGTH(signature)+3;
   char * s = len <= bufLen ? buf : (memory ? (char*)memory->allocateHeapMemory(len) : NULL);
   if (s)
      sprintf(s, "%.*s.%.*s%.*s", J9UTF8_LENGTH(className), utf8Data(className), J9UTF8_LENGTH(name), utf8Data(name), J9UTF8_LENGTH(signature), utf8Data(signature));
   return s;
   }

bool
TR_J9VMBase::isPrimitiveClass(TR_OpaqueClassBlock * clazz)
   {
   return J9ROMCLASS_IS_PRIMITIVE_TYPE(TR::Compiler->cls.romClassOf(clazz)) ? true : false;
   }

bool
TR_J9VMBase::isClassInitialized(TR_OpaqueClassBlock *clazz)
   {
   if (!clazz)
      return false;

   return (TR::Compiler->cls.convertClassOffsetToClassPtr(clazz)->initializeStatus == 1);
   }

/** \brief
 *     Query to check whether a class is visible to other class
 *
 *  \parm sourceClass
 *
 *  \parm destClass
 *
 *  \return
 *     True if destClass is visible to sourceClass
 */
bool
TR_J9VMBase::isClassVisible(TR_OpaqueClassBlock * sourceClass, TR_OpaqueClassBlock * destClass)
   {
   TR::VMAccessCriticalSection isClassVisible(this);
   J9InternalVMFunctions * intFunc = vmThread()->javaVM->internalVMFunctions;
   J9Class *sourceJ9Class = TR::Compiler->cls.convertClassOffsetToClassPtr(sourceClass);
   J9Class *destJ9Class = TR::Compiler->cls.convertClassOffsetToClassPtr(destClass);

   IDATA result = intFunc->checkVisibility(vmThread(), sourceJ9Class, destJ9Class, destJ9Class->romClass->modifiers, J9_LOOK_REFLECT_CALL | J9_LOOK_NO_JAVA);
   if (result != J9_VISIBILITY_ALLOWED)
      return false;
   else
      return true;
   }

TR_OpaqueClassBlock *
TR_J9VM::getComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass)
   {
   return convertClassPtrToClassOffset(((J9ArrayClass*)TR::Compiler->cls.convertClassOffsetToClassPtr(arrayClass))->componentType);
   }

TR_OpaqueClassBlock *
TR_J9VM::getArrayClassFromComponentClass(TR_OpaqueClassBlock * componentClass)
   {
   return convertClassPtrToClassOffset(TR::Compiler->cls.convertClassOffsetToClassPtr(componentClass)->arrayClass);
   }

TR_OpaqueClassBlock *
TR_J9VM::getLeafComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass)
   {
   return convertClassPtrToClassOffset(((J9ArrayClass*)TR::Compiler->cls.convertClassOffsetToClassPtr(arrayClass))->leafComponentType);
   }

int32_t
TR_J9VM::getNewArrayTypeFromClass(TR_OpaqueClassBlock *clazz)
   {
   struct J9Class ** arrayClasses = &_jitConfig->javaVM->booleanArrayClass;
   for (int32_t i = 0; i < 8; ++i)
      {
      if ((void*)convertClassPtrToClassOffset(arrayClasses[i]) == (void*)clazz)
         return i + 4;
      }
   return -1;
   }

uint32_t
TR_J9VM::getPrimitiveArrayOffsetInJavaVM(uint32_t arrayType)
   {
   TR_ASSERT(arrayType >= 4 && arrayType <= 11, "primitive array types must be between 4 and 11");
   return offsetof(J9JavaVM, booleanArrayClass) + (arrayType-4) * sizeof(J9Class*);
   }

bool
TR_J9VMBase::isCloneable(TR_OpaqueClassBlock *clazzPointer)
   {
   return (J9CLASS_FLAGS(TR::Compiler->cls.convertClassOffsetToClassPtr(clazzPointer)) & J9AccClassCloneable) != 0;
   }

bool
TR_J9VMBase::isReferenceArray(TR_OpaqueClassBlock *klass)
   {
   return isClassArray(klass) && !isPrimitiveArray(klass);
   }

TR_OpaqueClassBlock *
TR_J9VM::getSystemClassFromClassName(const char * name, int32_t length, bool isVettedForAOT)
   {
   TR::VMAccessCriticalSection getSystemClassFromClassName(this);
   TR_OpaqueClassBlock * result = convertClassPtrToClassOffset(jitGetClassInClassloaderFromUTF8(vmThread(),
                                                                                                (J9ClassLoader*)vmThread()->javaVM->systemClassLoader,
                                                                                                (void *)name,
                                                                                                length));
   return result;
   }

TR_OpaqueClassBlock *
TR_J9VMBase::getByteArrayClass()
   {
   return convertClassPtrToClassOffset(_jitConfig->javaVM->byteArrayClass);
   }

void *
TR_J9VMBase::getSystemClassLoader()
   {
   return vmThread()->javaVM->systemClassLoader;
   }

bool
TR_J9VMBase::acquireClassTableMutex()
   {
   // Get VM access before acquiring the ClassTableMutex and keep it until after
   // we release the ClassTableMutex
   bool haveAcquiredVMAccess = acquireVMAccessIfNeeded();
   jitAcquireClassTableMutex(vmThread());
   return haveAcquiredVMAccess;
   }

void
TR_J9VMBase::releaseClassTableMutex(bool releaseVMAccess)
   {
   jitReleaseClassTableMutex(vmThread());
   releaseVMAccessIfNeeded(releaseVMAccess);
   }

bool
TR_J9VMBase::jitFieldsAreSame(TR_ResolvedMethod * method1, I_32 cpIndex1, TR_ResolvedMethod * method2, I_32 cpIndex2, int32_t isStatic)
   {
   TR::VMAccessCriticalSection jitFieldsAreSame(this);
   bool result = false;

   bool sigSame = true;
   if (method1->fieldsAreSame(cpIndex1, method2, cpIndex2, sigSame))
      result = true;
   else
      {
      if (sigSame)
         result = jitFieldsAreIdentical(vmThread(), (J9ConstantPool *)method1->ramConstantPool(), cpIndex1, (J9ConstantPool *)method2->ramConstantPool(), cpIndex2, isStatic) != 0;
      }

   return result;
   }

bool
TR_J9VMBase::jitStaticsAreSame(TR_ResolvedMethod *method1, I_32 cpIndex1, TR_ResolvedMethod *method2, I_32 cpIndex2)
   {
   TR::VMAccessCriticalSection jitStaticsAreSame(this);
   bool result = false;
   bool sigSame = true;
   if (method1->staticsAreSame(cpIndex1, method2, cpIndex2, sigSame))
      result = true;
   else
      {
      if (sigSame)
         result = jitFieldsAreIdentical(vmThread(), (J9ConstantPool *)method1->ramConstantPool(), cpIndex1, (J9ConstantPool *)method2->ramConstantPool(), cpIndex2, true) != 0;
      }

   return result;
   }


TR::CodeCache *
TR_J9VM::getResolvedTrampoline(TR::Compilation *comp, TR::CodeCache* curCache, J9Method * method, bool inBinaryEncoding)
   {
   bool hadClassUnloadMonitor;
   bool hadVMAccess = releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(comp, &hadClassUnloadMonitor);
   TR::CodeCache* newCache = curCache; // optimistically assume as can allocate from current code cache

   int32_t retValue = curCache->reserveResolvedTrampoline((TR_OpaqueMethodBlock *)method, inBinaryEncoding);
   if (retValue != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
      {
      curCache->unreserve();  // delete the old reservation
      if (retValue == OMR::CodeCacheErrorCode::ERRORCODE_INSUFFICIENTSPACE && !inBinaryEncoding) // code cache full, allocate a new one
         {
         if (!isAOT_DEPRECATED_DO_NOT_USE())
            {
            // Allocate a new code cache and try again
            newCache = TR::CodeCacheManager::instance()->getNewCodeCache(comp->getCompThreadID()); // class unloading may happen here
            if (newCache)
               {
               // check for class unloading that can happen in getNewCodeCache
               TR::CompilationInfoPerThreadBase * const compInfoPTB =
                  _compInfo->getCompInfoForCompOnAppThread() ?
                     _compInfo->getCompInfoForCompOnAppThread() :
                     _compInfoPT;
               if (compInfoPTB->compilationShouldBeInterrupted())
                  {
                  newCache->unreserve(); // delete the reservation
                  newCache = NULL;
                  // this will allow retrial of the compilation
                  comp->failCompilation<TR::CompilationInterrupted>("Compilation interrupted in getResolvedTrampoline");
                  }
               else
                  {
                  int32_t retValue = newCache->reserveResolvedTrampoline((TR_OpaqueMethodBlock *) method, inBinaryEncoding);
                  if (retValue != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
                     {
                     newCache->unreserve(); // delete the reservation
                     newCache = NULL;
                     comp->failCompilation<TR::TrampolineError>("Failed to reserve resolved trampoline");
                     }
                  }
               }
            else
               {
               comp->failCompilation<TR::TrampolineError>("Failed to allocate new code cache");
               }
            }
         else
            {
            comp->failCompilation<TR::TrampolineError>("AOT Compile failed to delete the old reservation");
            }
         }
      else
         {
         newCache = NULL;
         if (inBinaryEncoding)
            {
            comp->failCompilation<TR::RecoverableTrampolineError>("Failed to delete the old reservation");
            }
         else
            {
            comp->failCompilation<TR::TrampolineError>("Failed to delete the old reservation");
            }
         }
      }
   acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(comp, hadVMAccess, hadClassUnloadMonitor);

   return newCache;
   }

intptr_t
TR_J9VM::methodTrampolineLookup(TR::Compilation *comp, TR::SymbolReference * symRef, void * callSite)
   {
   TR::VMAccessCriticalSection methodTrampolineLookup(this);
   TR_ASSERT(!symRef->isUnresolved(), "No need to lookup trampolines for unresolved methods.\n");
   TR_OpaqueMethodBlock * method = symRef->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod()->getPersistentIdentifier();

   intptr_t tramp;
   TR::MethodSymbol *methodSym = symRef->getSymbol()->castToMethodSymbol();
   switch (methodSym->getMandatoryRecognizedMethod())
      {
      case TR::java_lang_invoke_ComputedCalls_dispatchJ9Method:
         tramp = TR::CodeCacheManager::instance()->findHelperTrampoline(TR_j2iTransition, callSite);
         break;
      default:
         tramp = (intptr_t)TR::CodeCacheManager::instance()->findMethodTrampoline(method, callSite);
         break;
      }

   TR_ASSERT(tramp, "It should not fail since it is reserved first.\n");
   return tramp;
   }

TR_OpaqueClassBlock *
TR_J9VM::getBaseComponentClass(TR_OpaqueClassBlock * clazz, int32_t & numDims)
   {
   J9Class * myClass = TR::Compiler->cls.convertClassOffsetToClassPtr(clazz);
   while (J9ROMCLASS_IS_ARRAY(myClass->romClass))
      {
      J9Class * componentClass = (J9Class *)(((J9ArrayClass*)myClass)->componentType);
      if (J9ROMCLASS_IS_PRIMITIVE_TYPE(componentClass->romClass))
         break;
      numDims++;
      myClass = componentClass;
      }
   return convertClassPtrToClassOffset(myClass);
   }





TR_YesNoMaybe
TR_J9VM::isInstanceOf(TR_OpaqueClassBlock * a, TR_OpaqueClassBlock *b, bool objectTypeIsFixed, bool castTypeIsFixed, bool optimizeForAOT)
   {
   TR::VMAccessCriticalSection isInstanceOf(this);
   TR_YesNoMaybe result = TR_maybe;
   while (isClassArray(a) && isClassArray(b))
      {
      // too many conversions back and forth in this loop
      // maybe we should have specialized methods that work on J9Class pointers
      a = getComponentClassFromArrayClass(a);
      b = getComponentClassFromArrayClass(b);
      }
   J9Class * objectClass   = TR::Compiler->cls.convertClassOffsetToClassPtr(a);
   J9Class * castTypeClass = TR::Compiler->cls.convertClassOffsetToClassPtr(b);
   bool objectClassIsInstanceOfCastTypeClass = jitCTInstanceOf(objectClass, castTypeClass) != 0;

   if (castTypeIsFixed && objectClassIsInstanceOfCastTypeClass)
      result = TR_yes;
   else if (objectTypeIsFixed && !objectClassIsInstanceOfCastTypeClass)
      result = TR_no;
   else if (!isInterfaceClass(b) && !isInterfaceClass(a) &&
            !objectClassIsInstanceOfCastTypeClass &&
            !jitCTInstanceOf(castTypeClass, objectClass))
      result = TR_no;

   return result;
   }

bool
TR_J9VMBase::isPrimitiveArray(TR_OpaqueClassBlock *klass)
   {
   J9Class * j9class = TR::Compiler->cls.convertClassOffsetToClassPtr(klass);
   if (!J9ROMCLASS_IS_ARRAY(j9class->romClass))
      return false;
   j9class = (J9Class *)(((J9ArrayClass*)j9class)->componentType);
   return J9ROMCLASS_IS_PRIMITIVE_TYPE(j9class->romClass) ? true : false;
   }

TR_OpaqueClassBlock *
TR_J9VM::getClassFromSignature(const char * sig, int32_t sigLength, TR_ResolvedMethod * method, bool isVettedForAOT)
   {
   return getClassFromSignature(sig, sigLength, (TR_OpaqueMethodBlock *)method->getPersistentIdentifier(), isVettedForAOT);
   }

TR_OpaqueClassBlock *
TR_J9VM::getClassFromSignature(const char * sig, int32_t sigLength, TR_OpaqueMethodBlock * method, bool isVettedForAOT)
   {
   J9ConstantPool * constantPool = (J9ConstantPool *) (J9_CP_FROM_METHOD((J9Method*)method));
   return getClassFromSignature(sig, sigLength, constantPool);
   }

TR_OpaqueClassBlock *
TR_J9VM::getClassFromSignature(const char * sig, int32_t sigLength, J9ConstantPool * constantPool)
   {
   TR::VMAccessCriticalSection getClassFromSignature(this);
   J9Class * j9class = NULL;
   TR_OpaqueClassBlock * returnValue = NULL;

   // For a non-array class type, strip off the first 'L' and last ';' of the
   // signature
   //
   if ((*sig == 'L' || *sig == 'Q') && sigLength > 2)
      {
      sig += 1;
      sigLength -= 2;
      }

   j9class = jitGetClassFromUTF8(vmThread(), constantPool, (void *)sig, sigLength);

   if (j9class == NULL)
      {
      // special case -- classes in java.* and jit helper packages MUST be defined in the bootstrap class loader
      // however callers must account for the possibility that certain ClassLoaders may refuse to
      // allow these classes to be loaded at all. i.e. a non-NULL return from this helper DOES NOT
      // mean that a resolve can be safely removed
      // TODO: this will not find arrays
      if ((sigLength > 5 && strncmp(sig, "java/", 5) == 0) ||
         (sigLength == 31 && strncmp(sig, "com/ibm/jit/DecimalFormatHelper", 31) == 0) ||
         (sigLength > 21 && strncmp(sig, "com/ibm/jit/JITHelpers", 22) == 0))
         {
         returnValue = getSystemClassFromClassName(sig, sigLength);
         }
      }
   else
      {
      returnValue = convertClassPtrToClassOffset(j9class);
      }
   return returnValue; // 0 means failure
   }

TR_EstimateCodeSize *
TR_J9VMBase::getCodeEstimator(TR::Compilation *comp)
   {
   return new (comp->allocator()) TR_J9EstimateCodeSize();
   }

void
TR_J9VMBase::releaseCodeEstimator(TR::Compilation *comp, TR_EstimateCodeSize *estimator)
   {
   comp->allocator().deallocate(estimator, sizeof(TR_J9EstimateCodeSize));
   }

void
TR_J9VM::transformJavaLangClassIsArray(TR::Compilation * comp, TR::Node * callNode, TR::TreeTop * treeTop)
   {
   // Example for the transformation
   // treetop (may be null check)
   //   icalli                   <= callNode
   //     aload <parm 1>         <= jlClass
   //
   //
   // Final: (when target.is32Bit() == true)
   //
   // NULLCHK (if there is a null check)
   //   PassThrough
   //     aload <parm 1>         <= jlClass
   // treetop
   //   iushr
   //    iand
   //     iloadi <classAndDepthFlags>
   //       aloadi <classFromJavaLangClass>
   //        aload <parm 1>         <= jlClass
   //    iconst J9AccClassArray
   //   iconst shiftAmount

   int andMask = comp->fej9()->getFlagValueForArrayCheck();
   TR::Node * classFlag, *jlClass, * andConstNode;
   TR::SymbolReferenceTable *symRefTab = comp->getSymRefTab();

   jlClass = callNode->getFirstChild();

   if (treeTop->getNode()->getOpCode().isNullCheck())
      {
      // Put the call under its own tree after the NULLCHK
      //
      TR::TreeTop::create(comp, treeTop, TR::Node::create(TR::treetop, 1, callNode));

      // Replace the call under the nullchk with a PassThrough of the jlClass
      //
      TR::Node *nullChk = treeTop->getNode();
      nullChk->getAndDecChild(0); // Decrement ref count of callNode
      nullChk->setAndIncChild(0, TR::Node::create(TR::PassThrough, 1, jlClass));
      }

   TR::Node * vftLoad = TR::Node::createWithSymRef(callNode, TR::aloadi, 1, jlClass, comp->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef());

   if (comp->target().is32Bit())
      {
      classFlag = TR::Node::createWithSymRef(callNode, TR::iloadi, 1, vftLoad, symRefTab->findOrCreateClassAndDepthFlagsSymbolRef());
      }
   else
      {
      classFlag = TR::Node::createWithSymRef(callNode, TR::lloadi, 1, vftLoad, symRefTab->findOrCreateClassAndDepthFlagsSymbolRef());
      classFlag = TR::Node::create(callNode, TR::l2i, 1, classFlag);
      }

   // Decrement the ref count of jlClass since the call is going to be transmuted and its first child is not needed anymore
   callNode->getAndDecChild(0);
   TR::Node::recreate(callNode, TR::iushr);
   callNode->setNumChildren(2);

   andConstNode = TR::Node::create(callNode, TR::iconst, 0, andMask);
   TR::Node * andNode   = TR::Node::create(TR::iand, 2, classFlag, andConstNode);
   callNode->setAndIncChild(0, andNode);

   int32_t shiftAmount = trailingZeroes(andMask);
   callNode->setAndIncChild(1, TR::Node::iconst(callNode, shiftAmount));
   }

void
TR_J9VM::transformJavaLangClassIsArrayOrIsPrimitive(TR::Compilation * comp, TR::Node * callNode, TR::TreeTop * treeTop, int32_t andMask)
   {
   // Original for J2SE (jclmax may be different):
   //
   // treetop (may be resolve check and/or null check)
   //   iicall                   <= callNode
   //     aload <parm 1>         <= vftField
   //
   //
   // Final:
   //
   // treetop
   //   i2b                      <= callNode
   //     ishr                   <= shiftNode
   //       iand                 <= andNode
   //         iiload             <= isArrayField
   //           iaload
   // if (generateClassesOnHeap())
   //             iaload
   // endif
   //               aload <parm 1> <= vftField
   //         iconst <andMask>   <= andConstNode
   //       iconst 16
   //
   TR::Node * isArrayField, * vftField, * andConstNode;
   TR::SymbolReferenceTable *symRefTab = comp->getSymRefTab();

   vftField = callNode->getFirstChild();

   TR::Node * vftFieldInd;

   if (TR::Compiler->cls.classesOnHeap())
      {
      vftFieldInd = TR::Node::createWithSymRef(TR::aloadi, 1, 1, vftField, comp->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef());
      isArrayField = TR::Node::createWithSymRef(TR::aloadi, 1, 1,vftFieldInd,symRefTab->findOrCreateClassRomPtrSymbolRef());
      }
   else
      {
      isArrayField = TR::Node::createWithSymRef(TR::aloadi, 1, 1,vftField,symRefTab->findOrCreateClassRomPtrSymbolRef());
      vftFieldInd  = isArrayField; //pkalle
      }

   if (treeTop->getNode()->getOpCode().isNullCheck())
      {
      // Original tree had a null check before we transformed it, so we still
      // need one after
      //
      TR::ResolvedMethodSymbol *owningMethodSymbol = treeTop->getNode()->getSymbolReference()->getOwningMethodSymbol(comp);
      TR::TreeTop::create(comp, treeTop->getPrevTreeTop(),
         TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, vftFieldInd, symRefTab->findOrCreateNullCheckSymbolRef(owningMethodSymbol)));
      }

   TR::Node::recreate(callNode, TR::idiv);
   callNode->setNumChildren(2);

   isArrayField = TR::Node::createWithSymRef(TR::iloadi, 1, 1, isArrayField, comp->getSymRefTab()->findOrCreateClassIsArraySymbolRef());
   andConstNode = TR::Node::create(isArrayField, TR::iconst, 0, andMask);

   TR::Node * andNode   = TR::Node::create(TR::iand, 2, isArrayField, andConstNode);

   callNode->setAndIncChild(0, andNode);
   callNode->setAndIncChild(1, TR::Node::create(TR::iconst, 0, andMask));
   TR::Node::recreate(treeTop->getNode(), TR::treetop);

   vftField->decReferenceCount();
   }

// Hack markers
//
#define HELPERS_ARE_NEITHER_RESOLVED_NOR_UNRESOLVED (1)

TR::Node *
TR_J9VM::inlineNativeCall(TR::Compilation * comp, TR::TreeTop * callNodeTreeTop, TR::Node * callNode)
   {
   // Mandatory recognized methods: if we don't handle these specially, we
   // generate incorrect code.
   //
   TR::RecognizedMethod mandatoryMethodID = callNode->getSymbol()->castToResolvedMethodSymbol()->getMandatoryRecognizedMethod();
   switch (mandatoryMethodID)
      {
      case TR::java_lang_invoke_ComputedCalls_dispatchDirect:
      case TR::java_lang_invoke_ComputedCalls_dispatchVirtual:
         {
         // The first argument to these calls is actually the target address masquerading as a long argument
         TR::MethodSymbol *methodSymbol = callNode->getSymbol()->castToMethodSymbol();
         methodSymbol->setMethodKind(TR::MethodSymbol::ComputedStatic);
         TR::Node::recreate(callNode, methodSymbol->getMethod()->indirectCallOpCode());
         return callNode;
         }
      case TR::java_lang_invoke_ComputedCalls_dispatchJ9Method:
         {
         TR::MethodSymbol *sym = callNode->getSymbol()->castToMethodSymbol();
         sym->setVMInternalNative(false);
         sym->setInterpreted(false);
         TR::Method *method = sym->getMethod();

         TR::SymbolReference *helperSymRef = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_j2iTransition, true, true, false);
         sym->setMethodAddress(helperSymRef->getMethodAddress());
#if defined(J9VM_OPT_JITSERVER)
         // JITServer: store the helper reference number in the node for use in creating TR_HelperAddress relocation
         callNode->getSymbolReference()->setReferenceNumber(helperSymRef->getReferenceNumber());
#endif /* defined(J9VM_OPT_JITSERVER) */
         return callNode;
         }
      default:
        break;
      }


   TR_ResolvedMethod *resolvedMethod = callNode->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod();

   //this is commented out to test recognized methods for AOT
   //if (isAnyMethodTracingEnabled(resolvedMethod->getPersistentIdentifier()))
   //   return NULL;

   // Ordinary recognized methods: these are for performance, and can be
   // disabled without harming correctness.
   //
   TR::RecognizedMethod methodID = callNode->getSymbol()->castToResolvedMethodSymbol()->getRecognizedMethod();
   switch (methodID)
      {
      case TR::java_lang_Class_isArray:
         transformJavaLangClassIsArray(comp, callNode, callNodeTreeTop);
         return callNode;
      case TR::java_lang_Class_isPrimitive:
         transformJavaLangClassIsArrayOrIsPrimitive(comp, callNode, callNodeTreeTop, J9AccClassInternalPrimitiveType);
         return callNode;
      case TR::java_lang_Class_isInstance:
         if (  comp->cg()->supportsInliningOfIsInstance()
            && performTransformation(comp, "O^O inlineNativeCall: generate instanceof [%p] for Class.isInstance at bytecode %d\n", callNode, callNode->getByteCodeInfo().getByteCodeIndex()) )
            {
            if (comp->getOption(TR_TraceILGen) && comp->getDebug())
               {
               TR_BitVector nodeChecklistBeforeDump(comp->getNodeCount(), comp->trMemory(), stackAlloc, growable);
               traceMsg(comp, "   /--- Class.isInstance call tree --\n");

               comp->getDebug()->saveNodeChecklist(nodeChecklistBeforeDump);
               comp->getDebug()->dumpSingleTreeWithInstrs(callNodeTreeTop, NULL, true, false, true, false);
               comp->getDebug()->restoreNodeChecklist(nodeChecklistBeforeDump);

               traceMsg(comp, "\n");
               }

            TR_ASSERT(!callNode->getOpCode().isIndirect(), "Expecting direct call to Class.isInstance");

            TR::Node *jlClass = callNode->getChild(0);
            TR::Node *object  = callNode->getChild(1);

            // If there's a NULLCHK on the j/l/Class, we must retain that.  We
            // do this by pulling the call out from under the nullchk before
            // attempting to transform the call.
            //
            if (callNodeTreeTop->getNode()->getOpCode().isNullCheck())
               {
               // Put the call under its own tree after the NULLCHK
               //
               TR::TreeTop::create(comp, callNodeTreeTop, TR::Node::create(TR::treetop, 1, callNode));

               // Replace the call under the nullchk with a PassThrough of the jlClass
               //
               TR::Node *nullchk = callNodeTreeTop->getNode();
               nullchk->getAndDecChild(0);
               nullchk->setAndIncChild(0, TR::Node::create(TR::PassThrough, 1, jlClass));
               }

            // Transmute the call into an instanceof.
            // Note that the arguments on the call are backward.
            //
            TR::Node *j9class = TR::Node::createWithSymRef(TR::aloadi, 1, 1, jlClass,
                                                comp->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef());
            TR::Node::recreate(callNode, TR::instanceof);
            callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateInstanceOfSymbolRef(comp->getMethodSymbol()));
            callNode->getAndDecChild(0); // jlClass
            callNode->setChild(0, object);
            callNode->setAndIncChild(1, j9class);

            return callNode;
            }
         else
            {
            return NULL;
            }
      case TR::java_lang_Object_getClass:
         TR::Node::recreate(callNode, TR::aloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateVftSymbolRef());
         if (TR::Compiler->cls.classesOnHeap())
            {
            callNode = TR::Node::createWithSymRef(TR::aloadi, 1, 1, callNode, comp->getSymRefTab()->findOrCreateJavaLangClassFromClassSymbolRef());
            }
         return callNode;

      // Note: these cases are not tested and thus are commented out:
      // - com.ibm.oti.vm.VM.getStackClass(int)
      // - com.ibm.oti.vm.VM.getStackClassLoader(int)
      // - java/lang/Class.getStackClass(int)

      //case TR::com_ibm_oti_vm_VM_getStackClass:
      //   // A() -> B() -> c/i/o/v/VM.getStackClass(1) => A()
      //   // 1      0      -1
      //   // fall through intended
      //case TR::com_ibm_oti_vm_VM_getStackClassLoader:
      //   // A() -> B() -> c/i/o/v/VM.getStackClassLoader(1) => A()
      //   // 1      0      -1
      //   // fall through intended
      //case TR::java_lang_Class_getStackClass:
      //   // A() -> B() -> j/l/C.getStackClass(1) => A()
      //   // 1      0      -1
      //   // fall through intended

      case TR::java_lang_ClassLoader_getStackClassLoader:
         // A() -> B() -> j/l/CL.getStackClassLoader(1) => A()
         // 1      0      -1
         // fall through intended
         return 0;   // FIXME:: disabled for now

      case TR::java_lang_invoke_MethodHandles_getStackClass:
         return 0;

      case TR::sun_reflect_Reflection_getCallerClass:
         {
         // We need to bail out since we create a class pointer const with cpIndex of -1
         if (isAOT_DEPRECATED_DO_NOT_USE() && !comp->getOption(TR_UseSymbolValidationManager))
            {
            return 0;
            }

         static const bool disableGetCallerClassReduction = feGetEnv("TR_DisableGetCallerClassReduction") != NULL;
         if (disableGetCallerClassReduction)
            {
            return 0;
            }
            
         int32_t targetInlineDepth = 1;
         
         int32_t callerIndex = callNode->getByteCodeInfo().getCallerIndex();
         J9Class *callerClass = NULL;
         J9Method *callerMethod = NULL;

         // We need to keep track of whether we are asked to skip the frame at depth 0 which is the caller of the
         // method that calls getCallerClass(). We could encounter the case where we have reached caller index -1
         // in which case we need to know whether we were asked to keep skipping frames. If we are asked to keep
         // skipping then we cannot do this reduction because we could have a case like this:
         //
         // Depth  Caller Index  Method
         //   [3]             2  getCallerClass()
         //   [2]             1  Method.invoke()
         //   [1]             0  Method.invoke()
         //   [0]            -1  Method.invoke()
         //
         // We cannot do this reduction in such a case because we want to get the class of the non-skipped caller
         // of the call at depth [0] but depths [0] is the top level method we are compiling.
         bool skipFrameAtDepth0 = false;

         while (true)
            {
            if (callerIndex == -1)
               {
               TR::ResolvedMethodSymbol *callerMethodSymbol = comp->getJittedMethodSymbol();
               callerMethod = reinterpret_cast<J9Method *>(callerMethodSymbol->getResolvedMethod()->getPersistentIdentifier());
               callerClass = reinterpret_cast<J9Class *>(callerMethodSymbol->getResolvedMethod()->classOfMethod());

               // It is possible that we've reached the top-level method being compiled and that the said method is
               // marked with the FrameIteratorSkip annotation. In such a case targetInlineDepth may be 0 already
               // and we want to skip the callerIndex == -1 frame, but we cannot because the JIT cannot statically
               // determine the caller of the top-level method being compiled. We cannot perform this optimization
               // in such a case, so we make skipFrameAtDepth0 at this point and proceed.
               if (TR::Compiler->mtd.isFrameIteratorSkipMethod(callerMethod))
                  skipFrameAtDepth0 = true;
               }
            else
               {
               callerMethod = reinterpret_cast<J9Method *>(comp->getInlinedCallSite(callerIndex)._methodInfo);
               callerClass = reinterpret_cast<J9Class *>(comp->getInlinedResolvedMethod(callerIndex)->containingClass());
               }

            if (callerIndex == -1)
               break;

            // Skip methods with java.lang.invoke.FrameIteratorSkip annotation
            if (!TR::Compiler->mtd.isFrameIteratorSkipMethod(callerMethod))
               {
               if (targetInlineDepth == 0)
                  {
                  // Logic for whether we should skip an inlined frame should match up with what the VM is doing. See the VM
                  // implementation of `getCallerClassIterator` for details.
                  skipFrameAtDepth0 = transformJlrMethodInvoke(callerMethod, callerClass);
                  if (skipFrameAtDepth0)
                     {
                     callerIndex = comp->getInlinedCallSite(callerIndex)._byteCodeInfo.getCallerIndex();
                     continue;
                     }

                  break;
                  }

               targetInlineDepth--;
               }

            callerIndex = comp->getInlinedCallSite(callerIndex)._byteCodeInfo.getCallerIndex();
            }

         if (!skipFrameAtDepth0 && targetInlineDepth == 0)
            {
            int32_t classNameLength;
            char *className = getClassNameChars(reinterpret_cast<TR_OpaqueClassBlock *>(callerClass), classNameLength);
            if (performTransformation(comp, "O^O inlineNativeCall: inline class load [%p] of %.*s for '%s' at bytecode %d\n", callNode,
                  classNameLength, className,
                  callNode->getSymbolReference()->getName(comp->getDebug()), callNode->getByteCodeInfo().getByteCodeIndex()))
               {
               TR::Node::recreate(callNode, TR::loadaddr);
               callNode->removeAllChildren();
               TR::SymbolReference *callerClassSymRef = comp->getSymRefTab()->findOrCreateClassSymbol(comp->getMethodSymbol(), -1, convertClassPtrToClassOffset(callerClass));
               callNode->setSymbolReference(callerClassSymRef);
               if (TR::Compiler->cls.classesOnHeap())
                  {
                  callNode = TR::Node::createWithSymRef(TR::aloadi, 1, 1, callNode, comp->getSymRefTab()->findOrCreateJavaLangClassFromClassSymbolRef());
                  }
               }
            }
         else
            {
            return 0;
            }
         }
         return callNode;
         
      case TR::java_lang_Thread_currentThread:
         if (comp->cg()->getGRACompleted())
            {
            return 0;
            }
         else
            {
            comp->cg()->setInlinedGetCurrentThreadMethod();
            TR::Node::recreate(callNode, TR::aload);
            callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateCurrentThreadSymbolRef());

            return callNode;
            }
      case TR::java_lang_Float_intBitsToFloat:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            TR::Node::recreate(callNode, TR::ibits2f);
         return callNode;
      case TR::java_lang_Float_floatToIntBits:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            {
            TR::Node::recreate(callNode, TR::fbits2i);
            callNode->setNormalizeNanValues(true);
            }
         return callNode;
      case TR::java_lang_Float_floatToRawIntBits:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            {
            TR::Node::recreate(callNode, TR::fbits2i);
            callNode->setNormalizeNanValues(false);
            }
         return callNode;
      case TR::java_lang_Double_longBitsToDouble:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            {
            TR::Node::recreate(callNode, TR::lbits2d);
            }
         return callNode;
      case TR::java_lang_Double_doubleToLongBits:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            {
            TR::Node::recreate(callNode, TR::dbits2l);
            callNode->setNormalizeNanValues(true);
            }
         return callNode;
      case TR::java_lang_Double_doubleToRawLongBits:
         if (comp->cg()->getSupportsInliningOfTypeCoersionMethods())
            {
            TR::Node::recreate(callNode, TR::dbits2l);
            callNode->setNormalizeNanValues(false);
            }
         return callNode;
      case TR::java_lang_ref_Reference_getImpl:
         if (comp->getGetImplInlineable())
            {
            // Retrieve the offset of the instance "referent" field from Reference class
            TR::SymbolReference * callerSymRef = callNode->getSymbolReference();
            TR_ResolvedMethod * owningMethod = callerSymRef->getOwningMethod(comp);
            int32_t len = owningMethod->classNameLength();
            char * s = classNameToSignature(owningMethod->classNameChars(), len, comp);
            TR_OpaqueClassBlock * ReferenceClass = getClassFromSignature(s, len, owningMethod);
            // defect 143867, ReferenceClass == 0 and crashed later in findFieldInClass()
            if (!ReferenceClass)
               return 0;
            int32_t offset =
               getInstanceFieldOffset(ReferenceClass, REFERENCEFIELD, REFERENCEFIELDLEN, REFERENCERETURNTYPE, REFERENCERETURNTYPELEN, J9_RESOLVE_FLAG_INIT_CLASS);
            offset += (int32_t)getObjectHeaderSizeInBytes();  // size of a J9 object header to move past it

            // Generate reference symbol
            TR::SymbolReference * symRefField = comp->getSymRefTab()->findOrCreateJavaLangReferenceReferentShadowSymbol(callerSymRef->getOwningMethodSymbol(comp),
                                                                                                              true, TR::Address, offset, false);

            // This pointer
            TR::Node * thisNode = callNode->getFirstChild();

            // Generate indirect load of referent into the callNode
            TR::Node::recreate(callNode, comp->il.opCodeForIndirectLoad(TR::Address));
            callNode->setSymbolReference(symRefField);
            callNode->removeAllChildren();
            callNode->setNumChildren(1);
            callNode->setAndIncChild(0, thisNode);
            }
            return callNode;
      case TR::java_lang_J9VMInternals_rawNewArrayInstance:
         {
         TR::Node::recreate(callNode, TR::variableNewArray);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateANewArraySymbolRef(callNode->getSymbol()->castToResolvedMethodSymbol()));
         TR::Node *newNode = TR::Node::createWithSymRef(callNode, TR::aloadi, 1, comp->getSymRefTab()->findOrCreateArrayComponentTypeSymbolRef());
         TR::Node *newNodeChild = TR::Node::createWithSymRef(callNode, TR::aloadi, 1, comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef());
         newNode->setAndIncChild(0, newNodeChild);
         newNode->setNumChildren(1);
         newNodeChild->setChild(0, callNode->getChild(1));
         newNodeChild->setNumChildren(1);
         callNode->setAndIncChild(1,newNode);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getArrayLengthAsObject:
         {
         TR::Node::recreate(callNode, TR::iloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateContiguousArraySizeSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_rawNewInstance:
         {
         TR::Node::recreate(callNode, TR::variableNew);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateNewObjectSymbolRef(callNode->getSymbol()->castToResolvedMethodSymbol()));
         TR::Node *newNode = TR::Node::createWithSymRef(callNode, TR::aloadi, 1, comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef());
         newNode->setChild(0, callNode->getFirstChild());
         newNode->setNumChildren(1);
         callNode->setAndIncChild(0,newNode);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_defaultClone:
         {
         TR_OpaqueClassBlock *bdClass = getClassFromSignature("java/lang/Object", 16, comp->getCurrentMethod());
         TR_ScratchList<TR_ResolvedMethod> bdMethods(comp->trMemory());
         getResolvedMethods(comp->trMemory(), bdClass, &bdMethods);
         ListIterator<TR_ResolvedMethod> bdit(&bdMethods);

         TR_ResolvedMethod *method = NULL;
         for (method = bdit.getCurrent(); method; method = bdit.getNext())
            {
            const char *sig = method->signature(comp->trMemory());
            int32_t len = strlen(sig);
            if(!strncmp(sig, "java/lang/Object.clone()Ljava/lang/Object;", len))
               {
               callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateMethodSymbol(callNode->getSymbolReference()->getOwningMethodIndex(), -1, method, TR::MethodSymbol::Special));
               break;
               }
            }
         return callNode;
         }
      case TR::java_lang_J9VMInternals_isClassModifierPublic:
         {
         TR::Node::recreate(callNode, TR::iand);
         callNode->setAndIncChild(1, TR::Node::create(TR::iconst, 0, J9AccPublic));
         callNode->setNumChildren(2);
         return callNode;
         }
      case TR::com_ibm_jit_JITHelpers_is32Bit:
         {
         // instance method so receiver object is first child, and other nodes may hold onto it so need to decrement its ref count
         // following code hammers this call node into an iconst which disconnects the child
         callNode->getFirstChild()->decReferenceCount();
         // intentionally fall through to next case
         }
      case TR::java_lang_J9VMInternals_is32Bit:
      case TR::com_ibm_oti_vm_ORBVMHelpers_is32Bit:
         {
         // these methods are static so there are no child to worry about
         int32_t intValue = 0;
         if (comp->target().is32Bit())
            intValue = 1;
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         if (callNodeTreeTop &&
               callNodeTreeTop->getNode()->getOpCode().isResolveOrNullCheck())
            TR::Node::recreate(callNodeTreeTop->getNode(), TR::treetop);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getNumBitsInReferenceField:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getNumBitsInReferenceField:
      case TR::com_ibm_jit_JITHelpers_getNumBitsInReferenceField:
         {
         int32_t intValue = 8*TR::Compiler->om.sizeofReferenceField();
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getNumBytesInReferenceField:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getNumBytesInReferenceField:
      case TR::com_ibm_jit_JITHelpers_getNumBytesInReferenceField:
         {
         int32_t intValue = TR::Compiler->om.sizeofReferenceField();
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getNumBitsInDescriptionWord:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getNumBitsInDescriptionWord:
      case TR::com_ibm_jit_JITHelpers_getNumBitsInDescriptionWord:
         {
         int32_t intValue = 8*sizeof(UDATA);
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getNumBytesInDescriptionWord:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getNumBytesInDescriptionWord:
      case TR::com_ibm_jit_JITHelpers_getNumBytesInDescriptionWord:
         {
         int32_t intValue = sizeof(UDATA);
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getNumBytesInJ9ObjectHeader:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getNumBytesInJ9ObjectHeader:
      case TR::com_ibm_jit_JITHelpers_getNumBytesInJ9ObjectHeader:
         {
         int32_t intValue = getObjectHeaderSizeInBytes();
         TR::Node::recreate(callNode, TR::iconst);
         callNode->setNumChildren(0);
         callNode->setInt(intValue);
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getJ9ClassFromClass32:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass32:
         {
         TR::Node::recreate(callNode, TR::iloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef());

         return callNode;
         }
      case TR::java_lang_J9VMInternals_getInstanceShapeFromJ9Class32:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getInstanceShapeFromJ9Class32:
         {
         TR::Node::recreate(callNode, TR::iloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateInstanceShapeSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getInstanceDescriptionFromJ9Class32:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getInstanceDescriptionFromJ9Class32:
         {
         TR::Node::recreate(callNode, TR::iloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateInstanceDescriptionSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getDescriptionWordFromPtr32:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr32:
      case TR::com_ibm_jit_JITHelpers_getDescriptionWordFromPtr32:
         {
         TR::Node::recreate(callNode, TR::iloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateDescriptionWordFromPtrSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getJ9ClassFromClass64:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass64:
         {
         TR::Node::recreate(callNode, TR::lloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getInstanceShapeFromJ9Class64:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getInstanceShapeFromJ9Class64:
         {
         TR::Node::recreate(callNode, TR::lloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateInstanceShapeSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getInstanceDescriptionFromJ9Class64:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getInstanceDescriptionFromJ9Class64:
         {
         TR::Node::recreate(callNode, TR::lloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateInstanceDescriptionSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getDescriptionWordFromPtr64:
      case TR::com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr64:
      case TR::com_ibm_jit_JITHelpers_getDescriptionWordFromPtr64:
         {
         TR::Node::recreate(callNode, TR::lloadi);
         callNode->setSymbolReference(comp->getSymRefTab()->findOrCreateDescriptionWordFromPtrSymbolRef());
         return callNode;
         }
      case TR::java_lang_J9VMInternals_getSuperclass:
         {
         TR_OpaqueClassBlock *jitHelpersClass = comp->getJITHelpersClassPointer();
         ///traceMsg(comp, "jithelpersclass = %p\n", jitHelpersClass);
         if (jitHelpersClass && isClassInitialized(jitHelpersClass))
            {
            // fish for the getSuperclass method in JITHelpers
            //
            const char *callerSig = comp->signature();
            const char *getHelpersSig = "jitHelpers";
            int32_t getHelpersSigLength = 10;
            TR::SymbolReference *getSuperclassSymRef = NULL;
            TR::SymbolReference *getHelpersSymRef = NULL;
            TR_ScratchList<TR_ResolvedMethod> helperMethods(comp->trMemory());
            getResolvedMethods(comp->trMemory(), jitHelpersClass, &helperMethods);
            ListIterator<TR_ResolvedMethod> it(&helperMethods);
            for (TR_ResolvedMethod *m = it.getCurrent(); m; m = it.getNext())
               {
               char *sig = m->nameChars();
               if (!strncmp(sig, "getSuperclass", 13))
                  {
                  getSuperclassSymRef = comp->getSymRefTab()->findOrCreateMethodSymbol(callNode->getSymbolReference()->getOwningMethodIndex(), -1, m, TR::MethodSymbol::Virtual);
                  getSuperclassSymRef->setOffset(getVTableSlot(m->getPersistentIdentifier(), jitHelpersClass));
                  ///break;
                  }
               else if (!strncmp(sig, getHelpersSig, getHelpersSigLength))
                  {
                  getHelpersSymRef = comp->getSymRefTab()->findOrCreateMethodSymbol(callNode->getSymbolReference()->getOwningMethodIndex(), -1, m, TR::MethodSymbol::Static);
                  }
               }

            // redirect the call to the new Java implementation
            // instead of calling the native
            //
            if (getSuperclassSymRef && getHelpersSymRef)
               {
               // first get the helpers object
               // acall getHelpers
               //
               TR::Method *method = getHelpersSymRef->getSymbol()->castToMethodSymbol()->getMethod();
               TR::Node *helpersCallNode = TR::Node::createWithSymRef(callNode, method->directCallOpCode(), 0, getHelpersSymRef);
               TR::TreeTop *helpersCallTT = TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, helpersCallNode));
               callNodeTreeTop->insertBefore(helpersCallTT);

               //TR::Node *vftLoad = TR::Node::create(TR::aloadi, 1, helpersCallNode, comp->getSymRefTab()->findOrCreateVftSymbolRef());
               method = getSuperclassSymRef->getSymbol()->castToMethodSymbol()->getMethod();
               TR::Node::recreate(callNode, method->directCallOpCode());
               TR::Node *firstChild = callNode->getFirstChild();
               firstChild->decReferenceCount();
               callNode->setNumChildren(2);
               //callNode->setAndIncChild(0, vftLoad);
               callNode->setAndIncChild(0, helpersCallNode);
               callNode->setAndIncChild(1, firstChild);
               ///traceMsg(comp, "replaced call node for getSuperclass at node = %p\n", callNode);
               callNode->setSymbolReference(getSuperclassSymRef);
               }
            }
         return callNode;
         }
      case TR::com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getRomClassFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getRomClassFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getArrayShapeFromRomClass32:
      case TR::com_ibm_jit_JITHelpers_getArrayShapeFromRomClass64:
      case TR::com_ibm_jit_JITHelpers_getModifiersFromRomClass32:
      case TR::com_ibm_jit_JITHelpers_getModifiersFromRomClass64:
      case TR::com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class64:
         {
         // TODO: These methods should really have proper symrefs
         TR::Node *firstChild = callNode->getFirstChild();
         TR::Node *secondChild = callNode->getSecondChild();
         firstChild->decReferenceCount();
         secondChild->decReferenceCount();
         TR::Node *konstNode = NULL;
         TR::ILOpCodes addOp = TR::BadILOp;
         TR::DataType type = TR::NoType;
         switch (methodID)
            {
            case TR::com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class32:
               {
               int32_t konst = (int32_t)getOffsetOfBackfillOffsetField();
               konstNode = TR::Node::iconst(callNode, konst);
               addOp = TR::iadd;
               type = TR::Int32;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class64:
               {
               int64_t konst = getOffsetOfBackfillOffsetField();
               konstNode = TR::Node::lconst(callNode, konst);
               addOp = TR::ladd;
               type = TR::Int64;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getRomClassFromJ9Class32:
               {
               int32_t konst = (int32_t)getOffsetOfClassRomPtrField();
               konstNode = TR::Node::iconst(callNode, konst);
               addOp = TR::iadd;
               type = TR::Int32;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getRomClassFromJ9Class64:
               {
               int64_t konst = getOffsetOfClassRomPtrField();
               konstNode = TR::Node::lconst(callNode, konst);
               addOp = TR::ladd;
               type = TR::Int64;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getArrayShapeFromRomClass32:
            case TR::com_ibm_jit_JITHelpers_getArrayShapeFromRomClass64:
               {
               int32_t konst = (int32_t)getOffsetOfIndexableSizeField();
               konstNode = TR::Node::iconst(callNode, konst);
               addOp = TR::iadd;
               type = TR::Int32;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getModifiersFromRomClass32:
            case TR::com_ibm_jit_JITHelpers_getModifiersFromRomClass64:
               {
               int32_t konst = (int32_t)getOffsetOfIsArrayFieldFromRomClass();
               konstNode = TR::Node::iconst(callNode, konst);
               addOp = TR::iadd;
               type = TR::Int32;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class32:
               {
               int32_t konst = (int32_t)getOffsetOfSuperclassesInClassObject();
               konstNode = TR::Node::iconst(callNode, konst);
               addOp = TR::iadd;
               type = TR::Int32;
               break;
               }
            case TR::com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class64:
               {
               int64_t konst = getOffsetOfSuperclassesInClassObject();
               konstNode = TR::Node::lconst(callNode, konst);
               addOp = TR::ladd;
               type = TR::Int64;
               break;
               }
            default:
                break;
            }
         secondChild = TR::Node::create(addOp, 2, secondChild, konstNode);
         TR::SymbolReference *newSymRef = comp->getSymRefTab()->findOrCreateUnsafeSymbolRef(type);
         TR::Node::recreate(callNode, comp->il.opCodeForIndirectArrayLoad(type));
         // now remove the receiver (firstChild)
         //
         callNode->setSymbolReference(newSymRef);
         callNode->setAndIncChild(0, secondChild);
         callNode->setNumChildren(1);
         if (callNodeTreeTop && callNodeTreeTop->getNode()->getOpCode().isCheck())
            TR::Node::recreate(callNodeTreeTop->getNode(), TR::treetop);

         return callNode;
         }
      case TR::com_ibm_jit_JITHelpers_getJ9ClassFromClass32:
      case TR::com_ibm_jit_JITHelpers_getJ9ClassFromClass64:
      case TR::com_ibm_jit_JITHelpers_getClassFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getClassFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class64:
      case TR::com_ibm_jit_JITHelpers_getComponentTypeFromJ9Class32:
      case TR::com_ibm_jit_JITHelpers_getComponentTypeFromJ9Class64:
         {
         TR::Node *firstChild = callNode->getFirstChild();
         TR::Node *secondChild = callNode->getSecondChild();
         firstChild->decReferenceCount();
         secondChild->decReferenceCount();
         TR::ILOpCodes loadOp = TR::BadILOp;
         TR::SymbolReference *newSymRef = NULL;

         switch (methodID)
            {
         case TR::com_ibm_jit_JITHelpers_getJ9ClassFromClass32:
            {
            newSymRef = comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef();
            loadOp = TR::iloadi;
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getJ9ClassFromClass64:
            {
            newSymRef = comp->getSymRefTab()->findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef();
            loadOp = TR::lloadi;
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getClassFromJ9Class32:
         case TR::com_ibm_jit_JITHelpers_getClassFromJ9Class64:
            {
            newSymRef = comp->getSymRefTab()->findOrCreateJavaLangClassFromClassSymbolRef();
            loadOp = TR::aloadi;
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class32:
         case TR::com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class64:
            {
            loadOp = TR::iloadi;
            newSymRef = comp->getSymRefTab()->findOrCreateClassFlagsSymbolRef();
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class32:
            {
            loadOp = TR::iloadi;
            newSymRef = comp->getSymRefTab()->findOrCreateClassAndDepthFlagsSymbolRef();
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class64:
            {
            loadOp = TR::lloadi;
            newSymRef = comp->getSymRefTab()->findOrCreateClassAndDepthFlagsSymbolRef();
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getComponentTypeFromJ9Class32:
            {
            loadOp = TR::lloadi;
            newSymRef = comp->getSymRefTab()->findOrCreateArrayComponentTypeAsPrimitiveSymbolRef();
            break;
            }
         case TR::com_ibm_jit_JITHelpers_getComponentTypeFromJ9Class64:
            {
            loadOp = TR::lloadi;
            newSymRef = comp->getSymRefTab()->findOrCreateArrayComponentTypeAsPrimitiveSymbolRef();
            break;
            }
         default:
            break;
            }
         // now remove the receiver (firstChild)
         //
         TR::Node::recreate(callNode, loadOp);
         callNode->setSymbolReference(newSymRef);
         callNode->setAndIncChild(0, secondChild);
         callNode->setNumChildren(1);
         return callNode;
         }

      default:
         break;
      }

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   if (inlineNativeCryptoMethod(callNode, comp))
      {
      return callNode;
      }
#endif

   return 0;
   }

bool TR_J9VM::transformJlrMethodInvoke(J9Method *callerMethod, J9Class *callerClass)
   {
      {
      TR::VMAccessCriticalSection jlrMethodInvoke(this);

      if (vmThread()->javaVM->jlrMethodInvoke == NULL)
         return false;
      }

   return stackWalkerMaySkipFrames((TR_OpaqueMethodBlock *)callerMethod, (TR_OpaqueClassBlock *)callerClass);
   }

int32_t
TR_J9VMBase::adjustedInliningWeightBasedOnArgument(int32_t origWeight, TR::Node *argNode, TR::ParameterSymbol *parmSymbol, TR::Compilation *comp)
   {
   int32_t weight = origWeight;
   int32_t argTypeLength, parmTypeLength;

   const char * argType = argNode->getTypeSignature(argTypeLength);
   const char * parmType = parmSymbol->getTypeSignature(parmTypeLength);
   if (argType && parmType && (argTypeLength != parmTypeLength || strncmp(argType, parmType, argTypeLength)))
      {
      int32_t fraction = comp->getOptions()->getInlinerArgumentHeuristicFraction();
      weight = weight * (fraction-1) / fraction;
      }
   return weight;
   }

bool
TR_J9VMBase::canAllowDifferingNumberOrTypesOfArgsAndParmsInInliner()
   {
   return false;
   }

void
TR_J9VMBase::setInlineThresholds (TR::Compilation *comp, int32_t &callerWeightLimit, int32_t &maxRecursiveCallByteCodeSizeEstimate, int32_t &methodByteCodeSizeThreshold, int32_t &methodInWarmBlockByteCodeSizeThreshold, int32_t &methodInColdBlockByteCodeSizeThreshold, int32_t &nodeCountThreshold, int32_t size)
   {

   if (comp->isServerInlining())
      {
      callerWeightLimit = 4096;
      methodInWarmBlockByteCodeSizeThreshold = methodByteCodeSizeThreshold = 200;
      }


   int32_t _adjustSizeBoundary = 1750;
   int32_t _adjustMaxCGCutOff = 2500;

   static const char * adjustSizeBoundary = feGetEnv("TR_WarmInlineAdjustSizeBoundary");
   static const char * adjustMaxCGCutOff = feGetEnv("TR_WarmInlineAdjustCallGraphMaxCutOff");

   if (adjustSizeBoundary)
     _adjustSizeBoundary = atoi(adjustSizeBoundary);

   if (adjustMaxCGCutOff)
     _adjustMaxCGCutOff = atoi(adjustMaxCGCutOff);

   if (comp->isServerInlining())
     {
     maxRecursiveCallByteCodeSizeEstimate = (int)((float)maxRecursiveCallByteCodeSizeEstimate * (float)((float)(_adjustSizeBoundary)/(float)(size)));
     if (maxRecursiveCallByteCodeSizeEstimate > _adjustMaxCGCutOff) maxRecursiveCallByteCodeSizeEstimate = _adjustMaxCGCutOff;
     }


   int32_t _adjustMaxCutOff = 200; //decrease the threshold to make it similar to r11 while we are big app

   static const char * adjustMaxCutOff = feGetEnv("TR_WarmInlineAdjustMaxCutOff");

   if (adjustMaxCutOff)
      _adjustMaxCutOff = atoi(adjustMaxCutOff);

   if (comp->isServerInlining())
      {
      methodInWarmBlockByteCodeSizeThreshold = (int)((float)150 * (float)((float)(_adjustSizeBoundary)/(float)(size)));
      if (methodInWarmBlockByteCodeSizeThreshold > _adjustMaxCutOff)
         methodInWarmBlockByteCodeSizeThreshold = _adjustMaxCutOff;
      }
   else
      {
      if (methodInWarmBlockByteCodeSizeThreshold > methodByteCodeSizeThreshold)
         methodInWarmBlockByteCodeSizeThreshold = methodByteCodeSizeThreshold;
      }


   return;
   }

bool
TR_J9VMBase::isStringCompressionEnabledVM()
   {
   return IS_STRING_COMPRESSION_ENABLED_VM(getJ9JITConfig()->javaVM);
   }

void *
TR_J9VMBase::getInvokeExactThunkHelperAddress(TR::Compilation *comp, TR::SymbolReference *glueSymRef, TR::DataType dataType)
   {
   return glueSymRef->getMethodAddress();
   }

TR_OpaqueClassBlock *
TR_J9VM::getClassClassPointer(TR_OpaqueClassBlock *objectClassPointer)
   {
   TR::VMAccessCriticalSection getClassClassPointer(this);
   J9Class *j9class;
   j9class = TR::Compiler->cls.convertClassOffsetToClassPtr(objectClassPointer);

   if (TR::Compiler->cls.classesOnHeap())
      {
      void *javaLangClass = J9VM_J9CLASS_TO_HEAPCLASS(TR::Compiler->cls.convertClassOffsetToClassPtr(objectClassPointer));

      // j9class points to the J9Class corresponding to java/lang/Object
      if (TR::Compiler->om.generateCompressedObjectHeaders())
         j9class = (J9Class *)(uintptr_t) *((uint32_t *) ((uintptr_t) javaLangClass + (uintptr_t) TR::Compiler->om.offsetOfObjectVftField()));
      else
         j9class = (J9Class *)(*((J9Class **) ((uintptr_t) javaLangClass + (uintptr_t) TR::Compiler->om.offsetOfObjectVftField())));
      }
   else
      j9class = (J9Class *)(*((J9Class **) ((uintptr_t)j9class + (uintptr_t) TR::Compiler->om.offsetOfObjectVftField())));

   j9class = (J9Class *)((uintptr_t)j9class & TR::Compiler->om.maskOfObjectVftField());

   return convertClassPtrToClassOffset(j9class);
   }


TR_OpaqueClassBlock *
TR_J9VM::getClassFromMethodBlock(TR_OpaqueMethodBlock *mb)
   {
   J9Class *ramClass = J9_CLASS_FROM_METHOD((J9Method *)mb);
   return (TR_OpaqueClassBlock *)ramClass;
   }

// interpreter profiling support
TR_IProfiler *
TR_J9VM::getIProfiler()
   {
   if (!_iProfiler)
      {
      // This used to use a global variable called 'jitConfig' instead of the local object's '_jitConfig'.  In early out of memory scenarios, the global jitConfig may be NULL.
      _iProfiler = ((TR_JitPrivateConfig*)(_jitConfig->privateConfig))->iProfiler;
      }
   return _iProfiler;
   }

// HWProfiler support
void
TR_J9VM::createHWProfilerRecords(TR::Compilation *comp)
   {
   if(comp->getPersistentInfo()->isRuntimeInstrumentationEnabled())
      _compInfo->getHWProfiler()->createRecords(comp);
   }


TR_OpaqueClassBlock *
TR_J9VM::getClassOffsetForAllocationInlining(J9Class *clazzPtr)
   {
   return convertClassPtrToClassOffset(clazzPtr);
   }

J9Class *
TR_J9VM::getClassForAllocationInlining(TR::Compilation *comp, TR::SymbolReference *classSymRef)
   {
   if (!classSymRef->isUnresolved())
      return TR::Compiler->cls.convertClassOffsetToClassPtr((TR_OpaqueClassBlock *)classSymRef->getSymbol()->getStaticSymbol()->getStaticAddress());

   return NULL;
   }

uint32_t
TR_J9VMBase::getAllocationSize(TR::StaticSymbol *classSym, TR_OpaqueClassBlock * opaqueClazz)
   {
   J9Class * clazz = (J9Class * ) opaqueClazz;
   int32_t headerSize = getObjectHeaderSizeInBytes();
   int32_t objectSize = (int32_t)clazz->totalInstanceSize + headerSize;

   // gc requires objects to have
   // header + size >= 16
   objectSize = objectSize >= J9_GC_MINIMUM_OBJECT_SIZE ? objectSize : J9_GC_MINIMUM_OBJECT_SIZE;
   return objectSize;
   }

bool
TR_J9VM::supportAllocationInlining(TR::Compilation *comp, TR::Node *node)
   {
   return true;
   }

TR_OpaqueClassBlock *
TR_J9VM::getPrimitiveArrayAllocationClass(J9Class *clazz)
   {
   return (TR_OpaqueClassBlock *) clazz;
   }

TR_StaticFinalData
TR_J9VM::dereferenceStaticFinalAddress(void *staticAddress, TR::DataType addressType)
   {
   TR_StaticFinalData data;
   if (!staticAddress)
      {
      data.dataAddress = 0;
      return data;
      }
   TR::VMAccessCriticalSection dereferenceStaticFinalAddress(this);
   switch (addressType)
      {
      case TR::Int8:
         data.dataInt8Bit = *(int8_t *) staticAddress;
         break;
      case TR::Int16:
         data.dataInt16Bit = *(int16_t *) staticAddress;
         break;
      case TR::Int32:
         data.dataInt32Bit = *(int32_t *) staticAddress;
         break;
      case TR::Int64:
         data.dataInt64Bit = *(int64_t *) staticAddress;
         break;
      case TR::Float:
         data.dataFloat = *(float *) staticAddress;
         break;
      case TR::Double:
         data.dataDouble = *(double *) staticAddress;
         break;
      case TR::Address:
         data.dataAddress = *(uintptr_t *) staticAddress;
         break;
      default:
         TR_ASSERT(0, "Unexpected type %s", addressType.toString());
      }
   return data;
   }

//////////////////////////////////////////////////////////
// TR_J9SharedCacheVM
//////////////////////////////////////////////////////////

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT)

bool
TR_J9SharedCacheVM::isClassVisible(TR_OpaqueClassBlock * sourceClass, TR_OpaqueClassBlock * destClass)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      TR::SymbolValidationManager *svm = comp->getSymbolValidationManager();
      SVM_ASSERT_ALREADY_VALIDATED(svm, sourceClass);
      SVM_ASSERT_ALREADY_VALIDATED(svm, destClass);
      validated = true;
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) sourceClass) &&
                  ((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) destClass);
      }

   return (validated ? TR_J9VMBase::isClassVisible(sourceClass, destClass) : false);
   }

bool
TR_J9SharedCacheVM::stackWalkerMaySkipFrames(TR_OpaqueMethodBlock *method, TR_OpaqueClassBlock *methodClass)
   {
   bool skipFrames = TR_J9VM::stackWalkerMaySkipFrames(method, methodClass);
   TR::Compilation *comp = TR::comp();
   if (comp && comp->getOption(TR_UseSymbolValidationManager))
      {
      bool recordCreated = comp->getSymbolValidationManager()->addStackWalkerMaySkipFramesRecord(method, methodClass, skipFrames);
      SVM_ASSERT(recordCreated, "Failed to validate addStackWalkerMaySkipFramesRecord");
      }

   return skipFrames;
   }

bool
TR_J9SharedCacheVM::isMethodTracingEnabled(TR_OpaqueMethodBlock *method)
   {
   // We want to return the same answer as TR_J9VMBase unless we want to force it to allow tracing
   return TR_J9VMBase::isMethodTracingEnabled(method) || TR::Options::getAOTCmdLineOptions()->getOption(TR_EnableAOTMethodEnter) || TR::Options::getAOTCmdLineOptions()->getOption(TR_EnableAOTMethodExit);
   }

bool
TR_J9SharedCacheVM::traceableMethodsCanBeInlined()
   {
   return true;
   }

bool
TR_J9SharedCacheVM::canMethodEnterEventBeHooked()
   {
   // We want to return the same answer as TR_J9VMBase unless we want to force it to allow tracing
   return TR_J9VMBase::canMethodEnterEventBeHooked() || TR::Options::getAOTCmdLineOptions()->getOption(TR_EnableAOTMethodEnter);
   }

bool
TR_J9SharedCacheVM::canMethodExitEventBeHooked()
   {
   // We want to return the same answer as TR_J9VMBase unless we want to force it to allow tracing
   return TR_J9VMBase::canMethodExitEventBeHooked() || TR::Options::getAOTCmdLineOptions()->getOption(TR_EnableAOTMethodExit);
   }

bool
TR_J9SharedCacheVM::methodsCanBeInlinedEvenIfEventHooksEnabled(TR::Compilation *comp)
   {
   return !comp->getOption(TR_FullSpeedDebug);
   }

int32_t
TR_J9SharedCacheVM::getJavaLangClassHashCode(TR::Compilation * comp, TR_OpaqueClassBlock * clazzPointer, bool &hashCodeComputed)
   {
   hashCodeComputed = false;
   return 0;
   }

bool
TR_J9SharedCacheVM::javaLangClassGetModifiersImpl(TR_OpaqueClassBlock * clazzPointer, int32_t &result)
   {
   return false;
   }

uint32_t
TR_J9SharedCacheVM::getInstanceFieldOffset(TR_OpaqueClassBlock * classPointer, char * fieldName, uint32_t fieldLen,
                                    char * sig, uint32_t sigLen, UDATA options)
   {

   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method*) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class*)classPointer);
      }

   if (validated)
      return this->TR_J9VM::getInstanceFieldOffset (classPointer, fieldName, fieldLen, sig, sigLen, options);

   return ~0;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getClassOfMethod(TR_OpaqueMethodBlock *method)
   {
   TR_OpaqueClassBlock *classPointer = TR_J9VM::getClassOfMethod(method);

   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer);
      }

   if (validated)
      return classPointer;

   return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getSuperClass(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_OpaqueClassBlock *superClass = TR_J9VM::getSuperClass(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      validated = comp->getSymbolValidationManager()->addSuperClassFromClassRecord(superClass, classPointer);
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer);
      }

   if (validated)
      return superClass;

   return NULL;
   }

void
TR_J9SharedCacheVM::getResolvedMethods(TR_Memory *trMemory, TR_OpaqueClassBlock * classPointer, List<TR_ResolvedMethod> * resolvedMethodsInClass)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer);
      }

   if (validated)
      {
      if (comp->getOption(TR_UseSymbolValidationManager))
         {
         TR::VMAccessCriticalSection getResolvedMethods(this); // Prevent HCR
         J9Method * resolvedMethods = (J9Method *) getMethods(classPointer);
         uint32_t indexIntoArray;
         uint32_t numMethods = getNumMethods(classPointer);
         for (indexIntoArray = 0; indexIntoArray < numMethods; indexIntoArray++)
            {
            comp->getSymbolValidationManager()->addMethodFromClassRecord((TR_OpaqueMethodBlock *) &(resolvedMethods[indexIntoArray]),
                                                                         classPointer,
                                                                         indexIntoArray);
            }
         }

      TR_J9VM::getResolvedMethods(trMemory, classPointer, resolvedMethodsInClass);
      }
   }


TR_ResolvedMethod *
TR_J9SharedCacheVM::getResolvedMethodForNameAndSignature(TR_Memory * trMemory, TR_OpaqueClassBlock * classPointer,
                                                         const char* methodName, const char *signature)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_ResolvedMethod *resolvedMethod = TR_J9VM::getResolvedMethodForNameAndSignature(trMemory, classPointer, methodName, signature);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      TR_OpaqueMethodBlock *method = (TR_OpaqueMethodBlock *)((TR_ResolvedJ9Method *)resolvedMethod)->ramMethod();
      TR_OpaqueClassBlock *clazz = getClassFromMethodBlock(method);
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), clazz);
      validated = true;
      }
   else
      {
      validated = ((TR_ResolvedRelocatableJ9Method *)comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *)classPointer);
      }

   if (validated)
      return resolvedMethod;
   else
      return NULL;
   }

bool
TR_J9SharedCacheVM::isClassLibraryMethod(TR_OpaqueMethodBlock *method, bool vettedForAOT)
   {
   TR_ASSERT(vettedForAOT, "The TR_J9SharedCacheVM version of this method is expected to be called only from isClassLibraryMethod.\n"
                                          "Please consider whether you call is vetted for AOT!");

   if (getSupportsRecognizedMethods())
      return this->TR_J9VM::isClassLibraryMethod(method, vettedForAOT);

   return false;
   }

TR_OpaqueMethodBlock *
TR_J9SharedCacheVM::getMethodFromClass(TR_OpaqueClassBlock * methodClass, char * methodName, char * signature, TR_OpaqueClassBlock *callingClass)
   {
   TR_OpaqueMethodBlock* omb = this->TR_J9VM::getMethodFromClass(methodClass, methodName, signature, callingClass);
   if (omb)
      {
      TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
      TR_ASSERT(comp, "Should be called only within a compilation");

      if (comp->getOption(TR_UseSymbolValidationManager))
         {
         bool validated = comp->getSymbolValidationManager()->addMethodFromClassAndSignatureRecord(omb, methodClass, callingClass);
         if (!validated)
            omb = NULL;
         }
      else
         {
         if (!((TR_ResolvedRelocatableJ9Method*) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class*)methodClass))
            omb = NULL;
         if (callingClass && !((TR_ResolvedRelocatableJ9Method*) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class*)callingClass))
            omb = NULL;
         }
      }

   return omb;
   }

bool
TR_J9SharedCacheVM::supportAllocationInlining(TR::Compilation *comp, TR::Node *node)
   {
   if (comp->getOptions()->realTimeGC())
      return false;

   if ((comp->target().cpu.isX86() ||
        comp->target().cpu.isPower() ||
        comp->target().cpu.isZ() ||
        comp->target().cpu.isARM64()) &&
       !comp->getOption(TR_DisableAllocationInlining))
      return true;

   return false;
   }

TR_YesNoMaybe
TR_J9SharedCacheVM::isInstanceOf(TR_OpaqueClassBlock * a, TR_OpaqueClassBlock *b, bool objectTypeIsFixed, bool castTypeIsFixed, bool optimizeForAOT)
   {
   TR::Compilation *comp = TR::comp();
   TR_YesNoMaybe isAnInstanceOf = TR_J9VM::isInstanceOf(a, b, objectTypeIsFixed, castTypeIsFixed);
   bool validated = false;

   if (comp && comp->getOption(TR_UseSymbolValidationManager))
      {
      if (isAnInstanceOf != TR_maybe)
         validated = comp->getSymbolValidationManager()->addClassInstanceOfClassRecord(a, b, objectTypeIsFixed, castTypeIsFixed, (isAnInstanceOf == TR_yes));
      }
   else
      {
      validated = optimizeForAOT;
      }

   if (validated)
      return isAnInstanceOf;
   else
      return TR_maybe;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getClassFromSignature(const char * sig, int32_t sigLength, TR_ResolvedMethod * method, bool isVettedForAOT)
   {
   return getClassFromSignature(sig, sigLength, (TR_OpaqueMethodBlock *)method->getPersistentIdentifier(), isVettedForAOT);
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getClassFromSignature(const char * sig, int32_t sigLength, TR_OpaqueMethodBlock * method, bool isVettedForAOT)
   {
   TR_OpaqueClassBlock* j9class = TR_J9VM::getClassFromSignature(sig, sigLength, method, true);
   bool validated = false;
   TR::Compilation* comp = TR::comp();

   if (j9class)
      {
      if (comp->getOption(TR_UseSymbolValidationManager))
         {
         TR::SymbolValidationManager *svm = comp->getSymbolValidationManager();
         SVM_ASSERT_ALREADY_VALIDATED(svm, method);
         validated = svm->addClassByNameRecord(j9class, getClassFromMethodBlock(method));
         }
      else
         {
         if (isVettedForAOT)
            {
            if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) j9class))
               validated = true;
            }
         }
      }

   if (validated)
      return j9class;
   else
      return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getSystemClassFromClassName(const char * name, int32_t length, bool isVettedForAOT)
   {
   TR::Compilation *comp = TR::comp();
   TR_OpaqueClassBlock *classPointer = TR_J9VM::getSystemClassFromClassName(name, length);
   bool validated = false;

   if (comp && comp->getOption(TR_UseSymbolValidationManager))
      {
      validated = comp->getSymbolValidationManager()->addSystemClassByNameRecord(classPointer);
      }
   else
      {
      if (isVettedForAOT)
         {
         if (((TR_ResolvedRelocatableJ9Method *) TR::comp()->getCurrentMethod())->validateArbitraryClass(TR::comp(), (J9Class *) classPointer))
            validated = true;
         }
      }

   if (validated)
      return classPointer;
   else
      return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getProfiledClassFromProfiledInfo(TR_ExtraAddressInfo *profiledInfo)
   {
   TR_OpaqueClassBlock * classPointer = (TR_OpaqueClassBlock *)(profiledInfo->_value);
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   if (comp->getPersistentInfo()->isObsoleteClass((void *)classPointer, comp->fe()))
      return NULL;

   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      validated = comp->getSymbolValidationManager()->addProfiledClassRecord(classPointer);
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method*) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class*)classPointer))
         validated = true;
      }

   if (validated)
      return classPointer;
   else
      return NULL;
   }

bool
TR_J9SharedCacheVM::isPublicClass(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool publicClass = TR_J9VM::isPublicClass(classPointer);
   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return publicClass;
   else
      return true;
   }

bool
TR_J9SharedCacheVM::hasFinalizer(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool classHasFinalizer = TR_J9VM::hasFinalizer(classPointer);
   bool validated = false;

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return classHasFinalizer;
   else
      return true;
   }

uintptr_t
TR_J9SharedCacheVM::getClassDepthAndFlagsValue(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   uintptr_t classDepthFlags = TR_J9VM::getClassDepthAndFlagsValue(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return classDepthFlags;
   else
      return 0;
   }

uintptr_t
TR_J9SharedCacheVM::getClassFlagsValue(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   uintptr_t classFlags = TR_J9VM::getClassFlagsValue(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }

   if (validated)
      return classFlags;
   else
      return 0;
   }

bool
TR_J9SharedCacheVM::isPrimitiveClass(TR_OpaqueClassBlock * classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   bool isPrimClass = TR_J9VMBase::isPrimitiveClass(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return isPrimClass;
   else
      return false;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_OpaqueClassBlock *componentClass = TR_J9VM::getComponentClassFromArrayClass(arrayClass);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), componentClass);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) arrayClass))
         validated = true;
      }

   if (validated)
      return componentClass;
   else
      return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getArrayClassFromComponentClass(TR_OpaqueClassBlock * componentClass)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_OpaqueClassBlock *arrayClass = TR_J9VM::getArrayClassFromComponentClass(componentClass);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      validated = comp->getSymbolValidationManager()->addArrayClassFromComponentClassRecord(arrayClass, componentClass);
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) componentClass))
         validated = true;
      }

   if (validated)
      return arrayClass;
   else
      return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getLeafComponentClassFromArrayClass(TR_OpaqueClassBlock * arrayClass)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_OpaqueClassBlock *leafComponent = TR_J9VM::getLeafComponentClassFromArrayClass(arrayClass);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), leafComponent);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) arrayClass))
         validated = true;
      }

   if (validated)
      return leafComponent;
   else
      return NULL;
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getBaseComponentClass(TR_OpaqueClassBlock * classPointer, int32_t & numDims)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   TR_OpaqueClassBlock *baseComponent = TR_J9VM::getBaseComponentClass(classPointer, numDims);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), baseComponent);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return baseComponent;
   else
      return classPointer;  // not sure about this return value, but it's what we used to return before we got "smart"
   }

TR_OpaqueClassBlock *
TR_J9SharedCacheVM::getClassFromNewArrayType(int32_t arrayType)
   {
   TR::Compilation *comp = TR::comp();
   if (comp && comp->getOption(TR_UseSymbolValidationManager))
      return TR_J9VM::getClassFromNewArrayType(arrayType);
   return NULL;
   }


bool
TR_J9SharedCacheVM::isPrimitiveArray(TR_OpaqueClassBlock *classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   bool isPrimArray = TR_J9VMBase::isPrimitiveArray(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return isPrimArray;
   else
      return false;
   }

bool
TR_J9SharedCacheVM::isReferenceArray(TR_OpaqueClassBlock *classPointer)
   {
   TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
   TR_ASSERT(comp, "Should be called only within a compilation");

   bool validated = false;
   bool isRefArray = TR_J9VMBase::isReferenceArray(classPointer);

   if (comp->getOption(TR_UseSymbolValidationManager))
      {
      SVM_ASSERT_ALREADY_VALIDATED(comp->getSymbolValidationManager(), classPointer);
      validated = true;
      }
   else
      {
      if (((TR_ResolvedRelocatableJ9Method *) comp->getCurrentMethod())->validateArbitraryClass(comp, (J9Class *) classPointer))
         validated = true;
      }

   if (validated)
      return isRefArray;
   else
      return false;
   }

TR_OpaqueMethodBlock *
TR_J9SharedCacheVM::getInlinedCallSiteMethod(TR_InlinedCallSite *ics)
   {
   return ics->_methodInfo;
   }

// Answer the following conservatively until we know how to do better
bool
TR_J9SharedCacheVM::sameClassLoaders(TR_OpaqueClassBlock * class1, TR_OpaqueClassBlock * class2)
   {
   // conservative answer: need a relocation to validate
   return false;
   }

bool
TR_J9SharedCacheVM::isUnloadAssumptionRequired(TR_OpaqueClassBlock *, TR_ResolvedMethod *)
   {
   return true;
   }

bool
TR_J9SharedCacheVM::classHasBeenExtended(TR_OpaqueClassBlock * classPointer)
   {
   return true;
   }

bool
TR_J9SharedCacheVM::isGetImplInliningSupported()
   {
   return false;
   }

TR_ResolvedMethod *
TR_J9SharedCacheVM::getObjectNewInstanceImplMethod(TR_Memory *)
   {
   return NULL;
   }

////////////////// Under evaluation


TR::CodeCache *
TR_J9SharedCacheVM::getResolvedTrampoline(TR::Compilation *comp, TR::CodeCache *, J9Method * method, bool inBinaryEncoding)
   {
   return 0;
   }

intptr_t
TR_J9SharedCacheVM::methodTrampolineLookup(TR::Compilation *comp, TR::SymbolReference * symRef, void * callSite)
   {
   TR_ASSERT(0, "methodTrampolineLookup not implemented for AOT");
   return 0;
   }

// Multiple codeCache support
TR::CodeCache *
TR_J9SharedCacheVM::getDesignatedCodeCache(TR::Compilation *comp)
   {
   int32_t numReserved;
   int32_t compThreadID = comp ? comp->getCompThreadID() : -1;
   bool hadClassUnloadMonitor;
   bool hadVMAccess = releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(comp, &hadClassUnloadMonitor);
   TR::CodeCache * codeCache = TR::CodeCacheManager::instance()->reserveCodeCache(true, 0, compThreadID, &numReserved);
   acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(comp, hadVMAccess, hadClassUnloadMonitor);
   // For AOT we need some alignment
   if (codeCache)
      {
      codeCache->alignWarmCodeAlloc(_jitConfig->codeCacheAlignment);

      // For AOT we must install the beginning of the code cache
      comp->setRelocatableMethodCodeStart((uint32_t *)codeCache->getWarmCodeAlloc());
      }
   else
      {
      // If this is a temporary condition due to all code caches being reserved for the moment
      // we should retry this compilation
      if (!(jitConfig->runtimeFlags & J9JIT_CODE_CACHE_FULL))
         {
         if (numReserved > 0)
            {
            // set an error code so that the compilation is retried
            if (comp)
               {
               comp->failCompilation<TR::RecoverableCodeCacheError>("Cannot reserve code cache");
               }
            }
         }
      }

   return codeCache;
   }


void *
TR_J9SharedCacheVM::getJ2IThunk(char *signatureChars, uint32_t signatureLength, TR::Compilation *comp)
   {
   return (void *)findPersistentThunk(signatureChars, signatureLength);
   }

void *
TR_J9SharedCacheVM::setJ2IThunk(char *signatureChars, uint32_t signatureLength, void *thunkptr, TR::Compilation *comp)
   {
   return persistThunk(signatureChars, signatureLength, (U_8*)thunkptr - 8 /* start of thunk */, *((U_32 *)((U_8*)thunkptr -8)) + 8 /* size of thunk */);
   }

void *
TR_J9SharedCacheVM::persistJ2IThunk(void *thunk)
   {
   return persistThunk(((TR_J2IThunk *)thunk)->terseSignature(), strlen(((TR_J2IThunk *)thunk)->terseSignature()), (uint8_t*)thunk, ((TR_J2IThunk *)thunk)->totalSize());
   }

void *
TR_J9SharedCacheVM::persistThunk(char *signatureChars, uint32_t signatureLength, uint8_t *thunkStart, uint32_t totalSize)
   {
#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) && defined(J9VM_OPT_SHARED_CLASSES) && (defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_S390) || defined(TR_HOST_ARM) || defined(TR_HOST_ARM64))
   J9SharedDataDescriptor dataDescriptor;
   J9VMThread *curThread = getCurrentVMThread();

   dataDescriptor.address = (U_8*)thunkStart;
   dataDescriptor.length = totalSize;
   dataDescriptor.type = J9SHR_DATA_TYPE_AOTTHUNK;
   dataDescriptor.flags = 0;

   if (TR::Options::getAOTCmdLineOptions()->getOption(TR_TraceRelocatableDataDetailsCG))
      {
      TR_VerboseLog::writeLine("<relocatableDataThunksDetailsCG>");

      TR_VerboseLog::writeLine("%.*s", signatureLength, signatureChars);
      TR_VerboseLog::writeLine("thunkAddress: %p, thunkSize: %x", dataDescriptor.address, dataDescriptor.length);
      TR_VerboseLog::writeLine("thunkStart: %p", thunkStart);

      TR_VerboseLog::writeLine("</relocatableDataThunksDetailsCG>");
      }

   const void* store= _jitConfig->javaVM->sharedClassConfig->storeSharedData(curThread, signatureChars, signatureLength, &dataDescriptor);
   if (!store) /* Store failed */
      {
      TR::Compilation* comp = _compInfo->getCompInfoForCompOnAppThread() ? _compInfo->getCompInfoForCompOnAppThread()->getCompilation() : _compInfoPT->getCompilation();
      if (comp)
         comp->failCompilation<TR::CompilationException>("Failed to persist thunk");
      else
         throw TR::CompilationException();
      }

#endif
   return thunkStart;
   }

bool
TR_J9SharedCacheVM::canRememberClass(TR_OpaqueClassBlock *classPtr)
   {
   if (_sharedCache)
      return (_sharedCache->rememberClass((J9Class *) classPtr, false) != NULL);
   return false;
   }

bool
TR_J9SharedCacheVM::ensureOSRBufferSize(TR::Compilation *comp, uintptr_t osrFrameSizeInBytes, uintptr_t osrScratchBufferSizeInBytes, uintptr_t osrStackFrameSizeInBytes)
   {
   bool valid = TR_J9VMBase::ensureOSRBufferSize(comp, osrFrameSizeInBytes, osrScratchBufferSizeInBytes, osrStackFrameSizeInBytes);
   if (valid)
      {
      TR_AOTMethodHeader *aotMethodHeaderEntry = comp->getAotMethodHeaderEntry();
      aotMethodHeaderEntry->flags |= TR_AOTMethodHeader_UsesOSR;
      aotMethodHeaderEntry->_osrBufferInfo._frameSizeInBytes = osrFrameSizeInBytes;
      aotMethodHeaderEntry->_osrBufferInfo._scratchBufferSizeInBytes = osrScratchBufferSizeInBytes;
      aotMethodHeaderEntry->_osrBufferInfo._stackFrameSizeInBytes = osrStackFrameSizeInBytes;
      }
   return valid;
   }

//////////////////////////////////////////////////////////
// TR_J9SharedCacheVM
//////////////////////////////////////////////////////////

#if defined(J9VM_OPT_SHARED_CLASSES)

TR_J9SharedCacheVM::TR_J9SharedCacheVM(J9JITConfig * jitConfig, TR::CompilationInfo * compInfo, J9VMThread * vmThread)
   : TR_J9VM(jitConfig, compInfo, vmThread)
   {
   }


//d169771 [2177]
J9Class *
TR_J9SharedCacheVM::getClassForAllocationInlining(TR::Compilation *comp, TR::SymbolReference *classSymRef)
   {
   bool returnClassForAOT = true;
   if (!classSymRef->isUnresolved())
      return TR_J9VM::getClassForAllocationInlining(comp, classSymRef);
   else
      return (J9Class *) classSymRef->getOwningMethod(comp)->getClassFromConstantPool(comp, classSymRef->getCPIndex(), returnClassForAOT);
   }

#endif // defined J9VM_OPT_SHARED_CLASSES

#endif // defined J9VM_INTERP_AOT_COMPILE_SUPPORT



char *
TR_J9VMBase::classNameChars(TR::Compilation *comp, TR::SymbolReference * symRef, int32_t & length)
   {
   TR::Symbol * sym = symRef->getSymbol();
   if (sym == NULL || !sym->isClassObject() || symRef->getCPIndex() <= 0)
      {
      if (!symRef->isUnresolved() && (sym->isClassObject() || sym->isAddressOfClassObject()))
         {
         void * classObject = (void *) (sym->castToStaticSymbol()->getStaticAddress());
         if (sym->isAddressOfClassObject())
            classObject = *(void**)classObject;

         return getClassNameChars((TR_OpaqueClassBlock *) classObject, length);
         }

      length = 0;
      return NULL;
      }

   if (sym->addressIsCPIndexOfStatic())
      return symRef->getOwningMethod(comp)->classNameOfFieldOrStatic(symRef->getCPIndex(), length);

   TR_ResolvedMethod * method = symRef->getOwningMethod(comp);
   int32_t cpIndex = symRef->getCPIndex();

   TR_ASSERT(cpIndex != -1, "cpIndex shouldn't be -1");

   uint32_t len;
   char * n = method->getClassNameFromConstantPool(cpIndex, len);
   length = len;
   return n;
   }

bool
TR_J9VMBase::inSnapshotMode()
   {
   return J9_ARE_ANY_BITS_SET(getJ9JITConfig()->javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_CRIU_SUPPORT);
   }


// Native method bodies
//
#if defined(TR_HOST_X86)
JIT_HELPER(initialInvokeExactThunkGlue);
#else
JIT_HELPER(_initialInvokeExactThunkGlue);
#endif

JNIEXPORT jlong JNICALL Java_java_lang_invoke_ThunkTuple_initialInvokeExactThunk
   (JNIEnv *env, jclass clazz)
   {
#if defined(J9ZOS390)
   return (jlong)TOC_UNWRAP_ADDRESS(_initialInvokeExactThunkGlue);
#elif defined(TR_HOST_POWER) && (defined(TR_HOST_64BIT) || defined(AIXPPC)) && !defined(__LITTLE_ENDIAN__)
   return (jlong)(*(void **)_initialInvokeExactThunkGlue);
#elif defined(TR_HOST_X86)
   return (jlong)initialInvokeExactThunkGlue;
#else
   return (jlong)_initialInvokeExactThunkGlue;
#endif
   }

/* Note this is the underlying implementation of InterfaceHandle.vTableOffset(). Any special cases
 * (private interface method, methods in Object) have been adapted away by the java code, so this
 * native only ever deals with iTable interface methods.
 */
JNIEXPORT jint JNICALL Java_java_lang_invoke_InterfaceHandle_convertITableIndexToVTableIndex
  (JNIEnv *env, jclass InterfaceMethodHandle, jlong interfaceArg, jint itableIndex, jlong receiverClassArg)
   {
   J9Class  *interfaceClass = (J9Class*)(intptr_t)interfaceArg;
   J9Class  *receiverClass  = (J9Class*)(intptr_t)receiverClassArg;
   J9ITable *itableEntry;
   for (itableEntry = (J9ITable*)receiverClass->iTable; itableEntry; itableEntry = itableEntry->next)
      if (itableEntry->interfaceClass == interfaceClass)
         break;
   TR_ASSERT(itableEntry, "Shouldn't call convertITableIndexToVTableIndex without first ensuring the receiver implements the interface");
   UDATA *itableArray = (UDATA*)(itableEntry+1);
   return (itableArray[itableIndex] - sizeof(J9Class))/sizeof(intptr_t);
#if 0 // TODO:JSR292: We probably want to do something more like this instead, so it will properly handle exceptional cases
   struct
      {
      UDATA interfaceClass;
      UDATA methodIndex;
      } indexAndLiterals =
      {
      (UDATA)interfaceClass,
      (UDATA)itableIndex
      };
   J9Object *receiver = (J9Object*)receiverArg;
   return (jint)jitLookupInterfaceMethod(receiver->clazz, &indexAndLiterals, 0);
#endif
   }


extern "C" {
JNIEXPORT void JNICALL Java_java_lang_invoke_MutableCallSite_invalidate
  (JNIEnv *env, jclass MutableCallSite, jlongArray cookieArrayObject);
}

JNIEXPORT void JNICALL Java_java_lang_invoke_MutableCallSite_invalidate
  (JNIEnv *env, jclass MutableCallSite, jlongArray cookieArrayObject)
   {
   J9VMThread          *vmThread  = (J9VMThread*)env;
   J9JITConfig         *jitConfig = vmThread->javaVM->jitConfig;
   TR_FrontEnd         *fe        = TR_J9VMBase::get(jitConfig, vmThread);
   TR_RuntimeAssumptionTable *rat = TR::CompilationInfo::get(jitConfig)->getPersistentInfo()->getRuntimeAssumptionTable();

   bool verbose = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseHooks);
   bool details = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseHookDetails);
   int threadID = (int)(intptr_t)vmThread;

   if (verbose)
      TR_VerboseLog::writeLineLocked(TR_Vlog_HK, "%x hook %s vmThread=%p ", threadID, "MutableCallSite.invalidate", vmThread);

   jint numSites = env->GetArrayLength(cookieArrayObject);
   if (numSites <= 0)
      {
      if (verbose)
         TR_VerboseLog::writeLineLocked(TR_Vlog_HK, "%x   finished -- nothing to do", threadID);
      return;
      }

   jlong *cookies = (jlong*)alloca(numSites * sizeof(cookies[0]));
   env->GetLongArrayRegion(cookieArrayObject, 0, numSites, cookies);
   if (!env->ExceptionCheck())
      {
      // Because this is a JNI call, a java thread entering this function will not have
      // VM Access. Therefore we should explicitly acquire it.
      bool alreadyHasVMAccess = (vmThread->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS);
      if (!alreadyHasVMAccess)
         vmThread->javaVM->internalVMFunctions->internalEnterVMFromJNI(vmThread);
      jitAcquireClassTableMutex(vmThread);

      for (int32_t i=0; i < numSites; i++)
         {
         jlong cookie = cookies[i];
         if (cookie)
            {
            if (details)
               TR_VerboseLog::writeLineLocked(TR_Vlog_HD, "%x     notifying cookies[%3d] " UINT64_PRINTF_FORMAT_HEX, threadID, i, cookie);
            rat->notifyMutableCallSiteChangeEvent(fe, cookie);
            }
         else
            {
            if (details)
               TR_VerboseLog::writeLineLocked(TR_Vlog_HD, "%x     skipping nonexistent cookies[%3d]", threadID, i);
            }
         }

      jitReleaseClassTableMutex(vmThread);
      if (!alreadyHasVMAccess)
         vmThread->javaVM->internalVMFunctions->internalExitVMToJNI(vmThread);

      if (verbose)
         TR_VerboseLog::writeLineLocked(TR_Vlog_HK, "%x   finished %d CallSites", threadID, numSites);
      }
   else
      {
      if (verbose)
         TR_VerboseLog::writeLineLocked(TR_Vlog_HK, "%x hook %s vmThread=%p failed exception check", threadID, "MutableCallSite.invalidate", vmThread);
      }
   }

// Log file handling
//
extern "C" {

void feLockRTlog(TR_FrontEnd * fe)
   {
   J9JITConfig * config = getJ9JitConfigFromFE(fe);
   j9jitrt_lock_log(config);
   }

void feUnlockRTlog(TR_FrontEnd * fe)
   {
   J9JITConfig * config = getJ9JitConfigFromFE(fe);
   j9jitrt_unlock_log(config);
   }

void TR_VerboseLog::vlogAcquire()
   {
   j9jit_lock_vlog(_config);
   }

void TR_VerboseLog::vlogRelease()
   {
   j9jit_unlock_vlog(_config);
   }

void TR_VerboseLog::vwrite(const char *format, va_list args)
   {
   j9jit_vprintf((J9JITConfig *)_config, (char*)format, args);
   }

void TR_VerboseLog::writeTimeStamp()
   {
   if (TR::Options::getCmdLineOptions()->getOption(TR_PrintAbsoluteTimestampInVerboseLog))
      {
      char timestamp[32];
      TR::CompilationInfo *compInfo = TR::CompilationInfo::get();
      PORT_ACCESS_FROM_JITCONFIG(compInfo->getJITConfig());
      OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);
      omrstr_ftime_ex(timestamp, sizeof(timestamp), "%b-%d-%Y_%H:%M:%S ", j9time_current_time_millis(), OMRSTR_FTIME_FLAG_LOCAL);
      write(timestamp);
      }
   }

}


// -----------------------------------------------------------------------------


static bool
portLibCall_sysinfo_has_resumable_trap_handler()
   {
   // remove TR_HOST_ and TR_TARGET_ defines once code is moved to each architectures port library

   /* PPC */
   #if defined(TR_HOST_POWER) && defined(TR_TARGET_POWER)
      #if defined(AIXPPC) || defined(LINUX)
         return true;
      #else
         return false;
      #endif
   #endif

   /* X86 */
   #if defined(TR_HOST_X86) && defined(TR_TARGET_X86)
      #if defined(J9HAMMER) || defined(WINDOWS) || defined(LINUX) // LINUX_POST22
         return true;
      #else
         return false;
      #endif
   #endif

   /* ARM */
   #if defined(TR_HOST_ARM) && defined(TR_TARGET_ARM)
      #ifdef LINUX
         return true;
      #else
         return false;
      #endif
   #endif

   /* AArch64 */
   #if defined(TR_HOST_ARM64) && defined(TR_TARGET_ARM64)
      #ifdef LINUX
         return true;
      #else
         return false;
      #endif
   #endif

   /* 390 */
   #if defined(TR_HOST_S390) && defined(TR_TARGET_S390)
      #if defined(J9ZOS390)
         if (!_isPSWInProblemState())
            {
            return false;
            }
      #endif
      #if defined(J9ZTPF)
         return false;
      #else
         return true;
      #endif
   #endif

   }

static bool
portLibCall_sysinfo_has_fixed_frame_C_calling_convention()
   {
   // remove TR_HOST_ and TR_TARGET_ defines once code is moved to each architectures port library

   /* PPC */
  #if defined(TR_HOST_POWER) && defined(TR_TARGET_POWER)
      #if defined(AIXPPC) || defined(LINUX)
         return true;
      #else
         return false;
      #endif
   #endif

   /* S390 */
   #if defined(TR_HOST_S390) && defined(TR_TARGET_S390)
       return true;
   #endif

   /* ARM */
   #if defined(TR_HOST_ARM) && defined(TR_TARGET_ARM)
      return false;
   #endif

   /* AArch64 */
   #if defined(TR_HOST_ARM64) && defined(TR_TARGET_ARM64)
      return false;
   #endif

   /* X86 */
   #if defined(TR_HOST_X86) && defined(TR_TARGET_X86)
      return false;
   #endif

   }

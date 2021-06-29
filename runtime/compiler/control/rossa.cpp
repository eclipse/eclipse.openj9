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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINDOWS
// Undefine the winsockapi because winsock2 defines it. Removes warnings.
#if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#undef _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <process.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#ifdef J9ZOS390
#include <arpa/inet.h>
#endif
#endif
#ifdef J9ZTPF
#include <tpf/c_eb0eb.h>
#include <tpf/sysapi.h>
#include <tpf/tpfapi.h>
#include <tpf/c_cinfc.h>
#endif

#include "env/annotations/AnnotationBase.hpp"

#define J9_EXTERNAL_TO_VM
#include "codegen/PrivateLinkage.hpp"
#include "control/CompilationRuntime.hpp"
#include "control/CompilationThread.hpp"
#include "control/JitDump.hpp"
#include "control/Recompilation.hpp"
#include "control/RecompilationInfo.hpp"
#include "runtime/ArtifactManager.hpp"
#include "runtime/CodeCache.hpp"
#include "runtime/CodeCacheManager.hpp"
#include "runtime/CodeCacheReclamation.h"
#include "runtime/codertinit.hpp"
#include "runtime/IProfiler.hpp"
#include "runtime/HWProfiler.hpp"
#include "runtime/RelocationRuntime.hpp"
#include "env/PersistentInfo.hpp"
#include "env/ClassLoaderTable.hpp"
#include "env/J2IThunk.hpp"
#include "env/PersistentCHTable.hpp"
#include "env/CompilerEnv.hpp"
#include "env/jittypes.h"
#include "env/ClassTableCriticalSection.hpp"
#include "env/VerboseLog.hpp"

#include "ilgen/IlGeneratorMethodDetails_inlines.hpp"

/* Hardware Profiling */
#if defined(TR_HOST_S390) && defined(BUILD_Z_RUNTIME_INSTRUMENTATION)
#include "z/runtime/ZHWProfiler.hpp"
#elif defined(TR_HOST_POWER)
#include "p/runtime/PPCHWProfiler.hpp"
#endif

#include "control/rossa.h"
#include "control/OptimizationPlan.hpp"
#include "control/CompilationController.hpp"
#include "runtime/IProfiler.hpp"

#define _UTE_STATIC_
#include "env/ut_j9jit.h"

#include "jitprotos.h"
#if defined(J9VM_OPT_SHARED_CLASSES)
#include "j9jitnls.h"
#endif
#include "env/J9SharedCache.hpp"

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
#include "runtime/Crypto.hpp"
#endif

#include "infra/Monitor.hpp"

#include "j9.h"
#include "j9cfg.h"
#include "vmaccess.h"
#include "jvminit.h"
#include "j9port.h"
#include "env/exports.h"
#if defined(J9VM_OPT_JITSERVER)
#include "env/JITServerPersistentCHTable.hpp"
#include "net/CommunicationStream.hpp"
#include "net/ClientStream.hpp"
#include "net/LoadSSLLibs.hpp"
#include "runtime/JITClientSession.hpp"
#include "runtime/JITServerAOTCache.hpp"
#include "runtime/JITServerAOTDeserializer.hpp"
#include "runtime/JITServerIProfiler.hpp"
#include "runtime/JITServerSharedROMClassCache.hpp"
#include "runtime/JITServerStatisticsThread.hpp"
#include "runtime/Listener.hpp"
#endif /* defined(J9VM_OPT_JITSERVER) */

extern "C" int32_t encodeCount(int32_t count);

extern "C" {
struct J9RASdumpContext;
}

#if defined(TR_TARGET_X86) && defined(TR_HOST_32BIT)
extern TR_X86CPUIDBuffer *queryX86TargetCPUID(void * javaVM);
#endif

extern void setupCodeCacheParameters(int32_t *, OMR::CodeCacheCodeGenCallbacks *callBacks, int32_t *numHelpers, int32_t *CCPreLoadedCodeSize);
extern "C" void stopInterpreterProfiling(J9JITConfig *jitConfig);
extern "C" void restartInterpreterProfiling();

#ifdef J9VM_JIT_RUNTIME_INSTRUMENTATION
// extern until these functions are added to oti/jitprotos.h
extern "C" UDATA initializeJITRuntimeInstrumentation(J9JavaVM *vm);
#endif

extern void *ppcPicTrampInit(TR_FrontEnd *, TR::PersistentInfo *);
extern TR_Debug *createDebugObject(TR::Compilation *);


bool isQuickstart = false;

#define TRANSLATE_METHODHANDLE_TAKES_FLAGS

TR::Monitor *vpMonitor = 0;


char *compilationErrorNames[]={
   "compilationOK",                  // 0
   "compilationFailure",             // 1
   "compilationRestrictionILNodes",  // 2
   "compilationRestrictionRecDepth", // 3
   "compilationRestrictedMethod",    // 4
   "compilationExcessiveComplexity", // 5
   "compilationNotNeeded",           // 6
   "compilationSuspended",           // 7
   "compilationExcessiveSize",       // 8
   "compilationInterrupted",         // 9
   "compilationMetaDataFailure",     //10
   "compilationInProgress",          //11
   "compilationCHTableCommitFailure",//12
   "compilationMaxCallerIndexExceeded",//13
   "compilationKilledByClassReplacement",//14
   "compilationHeapLimitExceeded",   //15
   "compilationNeededAtHigherLevel", //16
   "compilationAotValidateFieldFailure", //17
   "compilationAotStaticFieldReloFailure", //18
   "compilationAotClassReloFailure", //19
   "compilationAotThunkReloFailure", //20
   "compilationAotTrampolineReloFailure", //21
   "compilationAotPicTrampolineReloFailure", //22
   "compilationAotCacheFullReloFailure", //23
   "compilationAotUnknownReloTypeFailure", //24
   "compilationCodeReservationFailure", //25
   "compilationAotHasInvokehandle", //26
   "compilationTrampolineFailure", //27
   "compilationRecoverableTrampolineFailure", // 28
   "compilationIlGenFailure", // 29
   "compilationIllegalCodeCacheSwitch", // 30
   "compilationNullSubstituteCodeCache", // 31
   "compilationCodeMemoryExhausted", // 32
   "compilationGCRPatchFailure", // 33
   "compilationAotValidateMethodExitFailure", // 34
   "compilationAotValidateMethodEnterFailure", // 35
   "compilationAotArrayClassReloFailure", // 36
   "compilationLambdaEnforceScorching", // 37
   "compilationInternalPointerExceedLimit", // 38
   "compilationAotRelocationInterrupted", // 39
   "compilationAotClassChainPersistenceFailure", // 40
   "compilationLowPhysicalMemory", // 41
   "compilationDataCacheError", // 42
   "compilationCodeCacheError", // 43
   "compilationRecoverableCodeCacheError", // 44
   "compilationAotHasInvokeVarHandle", //45
   "compilationAotValidateStringCompressionFailure", // 46
   "compilationFSDHasInvokeHandle", //47
   "compilationVirtualAddressExhaustion", //48
   "compilationEnforceProfiling", //49
   "compilationSymbolValidationManagerFailure", //50
   "compilationAOTNoSupportForAOTFailure", //51
   "compilationAOTValidateTMFailure", //52
   "compilationILGenUnsupportedValueTypeOperationFailure", //53
   "compilationAOTRelocationRecordGenerationFailure", //54
   "compilationAotPatchedCPConstant", //55
   "compilationAotHasInvokeSpecialInterface", //56
   "compilationAotValidateExceptionHookFailure", //57
   "compilationAotBlockFrequencyReloFailure", //58
   "compilationAotRecompQueuedFlagReloFailure", //59
   "compilationAOTValidateOSRFailure", //60
#if defined(J9VM_OPT_JITSERVER)
   "compilationStreamFailure", //compilationFirstJITServerFailure=61
   "compilationStreamLostMessage", // compilationFirstJITServerFailure+1
   "compilationStreamMessageTypeMismatch", //compilationFirstJITServerFailure+2
   "compilationStreamVersionIncompatible", //compilationFirstJITServerFailure+3
   "compilationStreamInterrupted", //compilationFirstJITServerFailure+4
#endif /* defined(J9VM_OPT_JITSERVER) */
   "compilationMaxError",
};

int32_t aggressiveOption = 0;


// Tells the sampler thread whether it is being interrupted to resume it or to
// shut it down.
//
volatile bool shutdownSamplerThread = false; // only set once


extern "C" void *compileMethodHandleThunk(j9object_t methodHandle, j9object_t arg, J9VMThread *vmThread, U_32 flags);
extern "C" J9NameAndSignature newInstancePrototypeNameAndSig;

extern "C" void getOutOfIdleStates(TR::CompilationInfo::TR_SamplerStates expectedState, TR::CompilationInfo *compInfo, const char* reason);


extern "C" IDATA
launchGPU(J9VMThread *vmThread, jobject invokeObject,
          J9Method *method, int deviceId,
          I_32 gridDimX, I_32 gridDimY, I_32 gridDimZ,
          I_32 blockDimX, I_32 blockDimY, I_32 blockDimZ,
          void **args);

extern "C" void promoteGPUCompile(J9VMThread *vmThread);

extern "C" int32_t setUpHooks(J9JavaVM * javaVM, J9JITConfig * jitConfig, TR_FrontEnd * vm);
extern "C" int32_t startJITServer(J9JITConfig *jitConfig);
extern "C" int32_t waitJITServerTermination(J9JITConfig *jitConfig);

char *AOTcgDiagOn="1";



// -----------------------------------------------------------------------------
// Method translation
// -----------------------------------------------------------------------------

extern "C" IDATA j9jit_testarossa(struct J9JITConfig * jitConfig, J9VMThread * vmThread, J9Method * method, void * oldStartPC)
   {
   return j9jit_testarossa_err(jitConfig, vmThread, method, oldStartPC, NULL);
   }


extern "C" IDATA
j9jit_testarossa_err(
      struct J9JITConfig *jitConfig,
      J9VMThread *vmThread,
      J9Method *method,
      void *oldStartPC,
      TR_CompilationErrorCode *compErrCode)
   {
   // The method is called by the interpreter when a method's invocation count has reached zero.
   // Rather than just compiling blindly, pass the controller an event, and let it decide what to do.
   //
   bool queued = false;
   TR_YesNoMaybe async = TR_maybe;
   TR_MethodEvent event;
   if (oldStartPC)
      {
      // any recompilation attempt using fixUpMethodCode would go here
      J9::PrivateLinkage::LinkageInfo *linkageInfo = J9::PrivateLinkage::LinkageInfo::get(oldStartPC);
      TR_PersistentJittedBodyInfo* jbi = TR::Recompilation::getJittedBodyInfoFromPC(oldStartPC);

      if (!jbi)
         {
         return 0;
         }

      TR_PersistentMethodInfo *pmi = jbi->getMethodInfo();

      if (pmi && pmi->hasBeenReplaced()) // HCR
         {
         // Obsolete method bodies are invalid.
         //
         TR::Recompilation::fixUpMethodCode(oldStartPC);
         jbi->setIsInvalidated();
         }

      if (jbi->getIsInvalidated())
         {
         event._eventType = TR_MethodEvent::MethodBodyInvalidated;
         async = TR_no;
         }
      else
         {
         // Async compilation is disabled or recompilations triggered from jitted code
         //
         // Check if the method has already been scheduled for compilation and return
         // abruptly if so. This will reduce contention on the optimizationPlanMonitor
         // when a profiled compilation is followed by a very slow recompilation
         // (until the recompilation is over, the java threads will try to trigger
         // recompilations again and again. See defect 193193)
         //
         if (linkageInfo->isBeingCompiled())
            {
            TR_J9VMBase *fe = TR_J9VMBase::get(jitConfig, vmThread);
            if (fe->isAsyncCompilation())
               return 0; // early return because the method is queued for compilation
            }
         // If PersistentJittedBody contains the profile Info and has BlockFrequencyInfo, it will set the
         // isQueuedForRecompilation field which can be used by the jitted code at runtime to skip the profiling
         // code if it has made request to recompile this method.
         if (jbi->getProfileInfo() != NULL && jbi->getProfileInfo()->getBlockFrequencyInfo() != NULL)
            jbi->getProfileInfo()->getBlockFrequencyInfo()->setIsQueuedForRecompilation();

         event._eventType = TR_MethodEvent::OtherRecompilationTrigger;
         }
      }
   else
      {
      event._eventType = TR_MethodEvent::InterpreterCounterTripped;
      // Experimental code: user may want to artificially delay the compilation
      // of methods to gather more IProfiler info
      TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
      if (TR::Options::_compilationDelayTime > 0)
         {
         if (!TR::CompilationInfo::isJNINative(method))
            {
            if (compInfo->getPersistentInfo()->getElapsedTime() < 1000 * TR::Options::_compilationDelayTime)
               {
               // Add 2 to the invocationCount
               int32_t count = TR::CompilationInfo::getInvocationCount(method);
               if (count >= 0)
                  {
                  TR::CompilationInfo::setInvocationCount(method, 2);
                  return 0;
                  }
               }
            }
         }
#if defined(J9VM_OPT_JITSERVER)
      // Do not allow local compilations in JITServer server mode
      if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
         return 0;
#endif
      }

   event._j9method = method;
   event._oldStartPC = oldStartPC;
   event._vmThread = vmThread;
   event._classNeedingThunk = 0;
   bool newPlanCreated;
   IDATA result = 0;
   TR_OptimizationPlan *plan = TR::CompilationController::getCompilationStrategy()->processEvent(&event, &newPlanCreated);

   // if the controller decides to compile this method, trigger the compilation
   if (plan)
      {
      TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);

      // Check to see if we need to wake up the sampling thread should it be in DEEPIDLE state
      if (compInfo->getSamplerState() == TR::CompilationInfo::SAMPLER_DEEPIDLE &&
          compInfo->_intervalStats._numFirstTimeCompilationsInInterval >= 1)
          getOutOfIdleStates(TR::CompilationInfo::SAMPLER_DEEPIDLE, compInfo, "comp req");
      else if (compInfo->getSamplerState() == TR::CompilationInfo::SAMPLER_IDLE &&
          compInfo->_intervalStats._numFirstTimeCompilationsInInterval >= TR::Options::_numFirstTimeCompilationsToExitIdleMode)
          getOutOfIdleStates(TR::CompilationInfo::SAMPLER_IDLE, compInfo, "comp req");

         { // scope for details
         TR::IlGeneratorMethodDetails details(method);
         result = (IDATA)compInfo->compileMethod(vmThread, details, oldStartPC, async, compErrCode, &queued, plan);
         }

      if (!queued && newPlanCreated)
         TR_OptimizationPlan::freeOptimizationPlan(plan);
      }
   else // OOM; Very rare case
      {
      // Invalidation requests MUST not be ignored even if OOM
      if (event._eventType == TR_MethodEvent::MethodBodyInvalidated)
         {
         // Allocate a plan on the stack and force a synchronous compilation
         // so that we wait until the compilation finishes
         TR_OptimizationPlan plan;
         plan.init(noOpt); // use the cheapest compilation; It's unlikely the compilation will
                           // succeed because we are running low on native memory
         plan.setIsStackAllocated(); // mark that this plan is not dynamically allocated
         TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);

            { // scope for details
            TR::IlGeneratorMethodDetails details(method);
            result = (IDATA)compInfo->compileMethod(vmThread, details, oldStartPC, async, compErrCode, &queued, &plan);
            }

         // We should probably prevent any future non-essential compilation
         compInfo->getPersistentInfo()->setDisableFurtherCompilation(true);
         if (TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerbosePerformance))
            {
            TR_VerboseLog::writeLineLocked(TR_Vlog_PERF,"t=%6u Disable further compilation due to OOM while creating an optimization plan", (uint32_t)compInfo->getPersistentInfo()->getElapsedTime());
            }
         }
      }
   return result;
   }

extern "C" IDATA
retranslateWithPreparationForMethodRedefinition(
      struct J9JITConfig *jitConfig,
      J9VMThread *vmThread,
      J9Method *method,
      void *oldStartPC)
   {
   return retranslateWithPreparation(jitConfig, vmThread, method, oldStartPC, TR_PersistentMethodInfo::RecompDueToInlinedMethodRedefinition);
   }

extern "C" IDATA
retranslateWithPreparation(
      struct J9JITConfig *jitConfig,
      J9VMThread *vmThread,
      J9Method *method,
      void *oldStartPC,
      UDATA reason)
   {
   if (!TR::CompilationInfo::get()->asynchronousCompilation() && !J9::PrivateLinkage::LinkageInfo::get(oldStartPC)->recompilationAttempted())
      {
      TR::Recompilation::fixUpMethodCode(oldStartPC);
      }

   TR_PersistentJittedBodyInfo* jbi = TR::Recompilation::getJittedBodyInfoFromPC(oldStartPC);
   if (jbi)
      {
      TR_PersistentMethodInfo *pmi = jbi->getMethodInfo();
      if (pmi)
         pmi->setReasonForRecompilation(reason);
      }
   return j9jit_testarossa(jitConfig, vmThread, method, oldStartPC);
   }


extern "C" void *
old_translateMethodHandle(J9VMThread *currentThread, j9object_t methodHandle)
   {
   void *result = compileMethodHandleThunk(methodHandle, NULL, currentThread, 0);
   if (result)
      {
      static char *returnNullFromTranslateMethodHandle = feGetEnv("TR_returnNullFromTranslateMethodHandle");
      if (returnNullFromTranslateMethodHandle)
         result = NULL;
      }

   return result;
   }


extern "C" void *
translateMethodHandle(J9VMThread *currentThread, j9object_t methodHandle, j9object_t arg, U_32 flags)
   {
   void *result = compileMethodHandleThunk(methodHandle, arg, currentThread, flags);
   if (result)
      {
      static char *returnNullFromTranslateMethodHandle = feGetEnv("TR_returnNullFromTranslateMethodHandle");
      if (returnNullFromTranslateMethodHandle)
         result = NULL;
      }

   return result;
   }


// -----------------------------------------------------------------------------
// NewInstanceImpl Thunks
// -----------------------------------------------------------------------------

extern "C" J9Method *
getNewInstancePrototype(J9VMThread * context)
   {
   J9Method *newInstanceProto = 0;
   J9InternalVMFunctions *intFunc = context->javaVM->internalVMFunctions;

   J9Class * jlClass = intFunc->internalFindKnownClass(context, J9VMCONSTANTPOOL_JAVALANGCLASS, J9_FINDKNOWNCLASS_FLAG_EXISTING_ONLY);
   if (jlClass)
      {
      newInstanceProto = (J9Method *) intFunc->javaLookupMethod(
            context,
            jlClass,
            (J9ROMNameAndSignature *) &newInstancePrototypeNameAndSig,
            0,
            J9_LOOK_DIRECT_NAS | J9_LOOK_VIRTUAL | J9_LOOK_NO_JAVA);
      }

   return newInstanceProto;
   }


extern "C" UDATA
j9jit_createNewInstanceThunk_err(
      struct J9JITConfig *jitConfig,
      J9VMThread *vmThread,
      J9Class *classNeedingThunk,
      TR_CompilationErrorCode *compErrCode)
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   J9Method *method = getNewInstancePrototype(vmThread);
   if (!method)
      {
      *compErrCode = compilationFailure;
      return 0;
      }

#if defined(J9VM_OPT_JITSERVER)
   // Do not allow local compilations in JITServer server mode
   if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
      return 0;
#endif

   bool queued = false;

   TR_MethodEvent event;
   event._eventType = TR_MethodEvent::NewInstanceImpl;
   event._j9method = method;
   event._oldStartPC = 0;
   event._vmThread = vmThread;
   bool newPlanCreated;
   TR_OptimizationPlan *plan = TR::CompilationController::getCompilationStrategy()->processEvent(&event, &newPlanCreated);
   UDATA result = 0;
   if (plan)
      {
      // if the controller decides to compile this method, trigger the compilation and wait here

         { // scope for details
         J9::NewInstanceThunkDetails details(method, classNeedingThunk);
         result = (UDATA)compInfo->compileMethod(vmThread, details, 0, TR_maybe, compErrCode, &queued, plan);
         }

      if (!queued && newPlanCreated)
         TR_OptimizationPlan::freeOptimizationPlan(plan);
      }

   return result;
   }


extern "C" UDATA
j9jit_createNewInstanceThunk(struct J9JITConfig * jitConfig, J9VMThread * vmThread, J9Class * classNeedingThunk)
   {
   return j9jit_createNewInstanceThunk_err(jitConfig, vmThread, classNeedingThunk, NULL);
   }


// -----------------------------------------------------------------------------
// JIT shutdown
// -----------------------------------------------------------------------------

extern "C" void
stopSamplingThread(J9JITConfig * jitConfig)
   {
   // Was the samplerThread even created?
   if (jitConfig->samplerThread)
      {
      // TODO: add a trace point in this routine
      //Trc_JIT_stopSamplingThread_Entry();
      TR::CompilationInfo *compInfo = getCompilationInfo(jitConfig);
      j9thread_monitor_enter(jitConfig->samplerMonitor);
      // In most cases the state of the sampling thread should be SAMPLE_THR_INITIALIZED
      // However, if an early error happens, stage 15 might not be run (this is where we initialize
      // the thread) and so the thread could be in ATTACHED_STATE
      TR_ASSERT(compInfo->getSamplingThreadLifetimeState() == TR::CompilationInfo::SAMPLE_THR_INITIALIZED ||
              compInfo->getSamplingThreadLifetimeState() == TR::CompilationInfo::SAMPLE_THR_ATTACHED,
         "sampling thread must be initialized or at least attached if we try to stop it");
      // inform samplerThread to exit its loop
      shutdownSamplerThread = true;
      compInfo->setSamplingThreadLifetimeState(TR::CompilationInfo::SAMPLE_THR_STOPPING);
      // interrupt possible nap time for the samplerThread
      j9thread_interrupt(jitConfig->samplerThread);
      // Wait for samplerThread to exit
      while (compInfo->getSamplingThreadLifetimeState() != TR::CompilationInfo::SAMPLE_THR_DESTROYED)
         j9thread_monitor_wait(jitConfig->samplerMonitor);
      // At this point samplingThread has gone away
      compInfo->setSamplerThread(NULL);
      jitConfig->samplerThread = 0; // just in case we enter this routine again
      j9thread_monitor_exit(jitConfig->samplerMonitor);
      // sampleMonitor is no longer needed
      j9thread_monitor_destroy(jitConfig->samplerMonitor);
      jitConfig->samplerMonitor = 0;
      //Trc_JIT_stopSamplingThread_Exit();
      }
   }


extern "C" void
freeJITConfig(J9JITConfig * jitConfig)
   {
   // This method must be called only when we are sure that there are no other java threads running
   if (jitConfig)
      {
      // Do all JIT compiler present freeing.
      //
      J9JavaVM * javaVM = jitConfig->javaVM;
      PORT_ACCESS_FROM_JAVAVM(javaVM);

      // stopSamplingThread(jitConfig); // done during JitShutdown below

      jitConfig->runtimeFlags &= ~J9JIT_JIT_ATTACHED;

      // If the jitConfig will be freed,
      // there is no chance to do all te JIT cleanup.
      // Do the JIT cleanup now, even if we tried it before
      //
      JitShutdown(jitConfig);

      // free the compilation info before freeing the frontend without thread info (jitConfig->compilationInfo)
      // which is done on codert_OnUnload
      TR::CompilationInfo::freeCompilationInfo(jitConfig);

      // Tell the runtime to unload.
      //
      codert_OnUnload(javaVM);
      }
   }


// exclusive shutdown.  This normally occurs when System.exit() is called by
// the usercode.  The VM needs to notify the compilation thread before going
// exclusive - otherwise we end up with a deadlock scenario upon exit
//
extern "C" void
jitExclusiveVMShutdownPending(J9VMThread * vmThread)
   {
#ifndef SMALL_APPTHREAD
   J9JavaVM *javaVM = vmThread->javaVM;
#if defined(J9VM_OPT_JITSERVER)
   TR::CompilationInfo * compInfo = getCompilationInfo(javaVM->jitConfig);
   if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
      {
      TR_Listener *listener = ((TR_JitPrivateConfig*)(javaVM->jitConfig->privateConfig))->listener;
      if (listener)
         {
         listener->stop();
         }
      }
#endif /* defined(J9VM_OPT_JITSERVER) */

   getCompilationInfo(javaVM->jitConfig)->stopCompilationThreads();
#endif
   }

// Code cache callbacks to be used by the VM
//
extern "C" U_8*
getCodeCacheWarmAlloc(void *codeCache)
   {
   TR::CodeCache * cc = static_cast<TR::CodeCache*>(codeCache);
   return cc->getWarmCodeAlloc();
   }

extern "C" U_8*
getCodeCacheColdAlloc(void *codeCache)
   {
   TR::CodeCache * cc = static_cast<TR::CodeCache*>(codeCache);
   return cc->getColdCodeAlloc();
   }


// -----------------------------------------------------------------------------
// JIT control
// -----------------------------------------------------------------------------

// Temporarily disable the jit so that no more methods are compiled
extern "C" void
disableJit(J9JITConfig * jitConfig)
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   J9JavaVM * vm = jitConfig->javaVM;
   if (compInfo && compInfo->getNumCompThreadsActive() > 0)
      {
      // Suspend compilation.
      // This will also flush any methods waiting for compilation
      //
      compInfo->suspendCompilationThread();
      // Generate a trace point
      Trc_JIT_DisableJIT(vm->internalVMFunctions->currentVMThread(vm));

      // Turn off interpreter profiling
      //
#if defined(J9VM_INTERP_PROFILING_BYTECODES)
      stopInterpreterProfiling(jitConfig);
#endif
      j9thread_monitor_enter(vm->vmThreadListMutex);
      // Set the sampling frequency to a high value - this will send the sampling
      // thread to sleep for a long time. If the jit is enabled later we will have
      // to interrupt the sampling thread to get it going again.
      //
      TR::CompilationInfo::TR_SamplerStates samplerState = compInfo->getSamplerState();
      if (samplerState != TR::CompilationInfo::SAMPLER_NOT_INITIALIZED && //jitConfig->samplerThread && // Must have a sampler thread
          samplerState != TR::CompilationInfo::SAMPLER_SUSPENDED && // Nothing to do if sampler is already suspended
          samplerState != TR::CompilationInfo::SAMPLER_STOPPED)
         {
         TR::PersistentInfo *persistentInfo = compInfo->getPersistentInfo();
         compInfo->setSamplerState(TR::CompilationInfo::SAMPLER_SUSPENDED);
         jitConfig->samplingFrequency = MAX_SAMPLING_FREQUENCY;
         persistentInfo->setLastTimeSamplerThreadWasSuspended(persistentInfo->getElapsedTime());
         if (TR::Options::getVerboseOption(TR_VerbosePerformance))
            {
            TR_VerboseLog::writeLineLocked(TR_Vlog_PERF,"t=%u\tSampling thread suspended and changed frequency to %d ms",
                         persistentInfo->getElapsedTime(), jitConfig->samplingFrequency);
            }
         }
      // TODO: what are the implications of remaining in an appropriate JIT state?
      // Should we change the state to STEADY just to be on the safe side?
      // What if this happens during startup? We will kill the startup time?

      //set the countDelta
      J9VMThread * currentThread = vm->mainThread;
      do
         {
         currentThread->jitCountDelta = 0;
         } while ((currentThread = currentThread->linkNext) != vm->mainThread);

      j9thread_monitor_exit(vm->vmThreadListMutex);
      }
   }


// Re-enable the jit after it has been disabled.
extern "C" void
enableJit(J9JITConfig * jitConfig)
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   J9JavaVM * vm = jitConfig->javaVM;
   if (compInfo && compInfo->getNumCompThreadsActive() == 0)
      {
      // Re-enable interpreter profiling if needed
      //
#if defined(J9VM_INTERP_PROFILING_BYTECODES)
      restartInterpreterProfiling();
#endif
      // Resume compilation.
      //
      compInfo->resumeCompilationThread();
      // Generate a trace point
      Trc_JIT_EnableJIT(vm->internalVMFunctions->currentVMThread(vm));

      j9thread_monitor_enter(vm->vmThreadListMutex);

      // Set the sampling frequency back to its proper value and kick the
      // sampler thread back into life.
      //
      TR::CompilationInfo::TR_SamplerStates samplerState = compInfo->getSamplerState();
      if (samplerState == TR::CompilationInfo::SAMPLER_SUSPENDED) // Nothing to do if sampler is not suspended
         {
         TR::PersistentInfo *persistentInfo = compInfo->getPersistentInfo();
         compInfo->setSamplerState(TR::CompilationInfo::SAMPLER_DEFAULT);
         jitConfig->samplingFrequency = TR::Options::getCmdLineOptions()->getSamplingFrequency();
         // this wake up call counts as a tick, so mark that we have seen ticks active
         // so that we need to wait another 5 seconds to enter idle
         persistentInfo->setLastTimeThreadsWereActive(persistentInfo->getElapsedTime());
         j9thread_interrupt(jitConfig->samplerThread); // interrupt the sampler to look at its new state
         if (TR::Options::getVerboseOption(TR_VerbosePerformance))
            {
            TR_VerboseLog::writeLineLocked(TR_Vlog_SAMPLING,"t=%u\tSampling thread interrupted and changed frequency to %d ms",
                         persistentInfo->getElapsedTime(), jitConfig->samplingFrequency);
            }
         }

      // Set the countDelta
      J9VMThread * currentThread = vm->mainThread;
      do
         {
         currentThread->jitCountDelta = 2;
         } while ((currentThread = currentThread->linkNext) != vm->mainThread);

      j9thread_monitor_exit(vm->vmThreadListMutex);

      }
   } // enableJit


// -----------------------------------------------------------------------------
// Programmatic compile interface
// -----------------------------------------------------------------------------

extern "C" I_32
command(J9VMThread * vmThread, const char * cmdString)
   {
   TR::CompilationInfo * compInfo = TR::CompilationInfo::get();

   if (strncmp(cmdString, "beginningOfStartup", 18) == 0)
      {
      TR::Options::getCmdLineOptions()->setOption(TR_AssumeStartupPhaseUntilToldNotTo); // let the JIT know it should wait for endOfStartup hint
      if (compInfo)
         {
         TR::PersistentInfo *persistentInfo = compInfo->getPersistentInfo();
         // If we trust the application completely, then set CLP right away
         // Otherwise, let JIT heuristics decide at next decision point
         if (TR::Options::getCmdLineOptions()->getOption(TR_UseStrictStartupHints))
            persistentInfo->setClassLoadingPhase(true); // Enter CLP right away
         if (TR::Options::isAnyVerboseOptionSet(TR_VerboseCLP, TR_VerbosePerformance))
            {
            TR_VerboseLog::writeLineLocked(TR_Vlog_INFO,"Compiler.command(beginningOfStartup)");
            }
         }
      return 0;
      }
   if (strncmp(cmdString, "endOfStartup", 12) == 0)
      {
      if (TR::Options::getCmdLineOptions()->getOption(TR_AssumeStartupPhaseUntilToldNotTo)) // refuse to do anything if JIT option was not specified
         {
         if (compInfo)
            {
            TR::PersistentInfo *persistentInfo = compInfo->getPersistentInfo();
            persistentInfo->setExternalStartupEndedSignal(true);
            // If we trust the application completely, then set CLP right away
            // Otherwise, let JIT heuristics decide at next decision point
            if (TR::Options::getCmdLineOptions()->getOption(TR_UseStrictStartupHints))
               persistentInfo->setClassLoadingPhase(false);
            if (TR::Options::isAnyVerboseOptionSet(TR_VerboseCLP, TR_VerbosePerformance))
               {
               TR_VerboseLog::writeLineLocked(TR_Vlog_INFO,"Compiler.command(endOfStartup)");
               }
            }
         }
      return 0;
      }

   return 0;
   }

static IDATA
internalCompileClass(J9VMThread * vmThread, J9Class * clazz)
   {
   J9JavaVM * javaVM = vmThread->javaVM;
   J9JITConfig * jitConfg = javaVM->jitConfig;
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   TR_J9VMBase *fe = TR_J9VMBase::get(jitConfg, NULL);

   // To prevent class unloading we need VM access
   bool threadHadNoVMAccess = (!(vmThread->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS));
   if (threadHadNoVMAccess)
      acquireVMAccess(vmThread);

   J9Method * newInstanceThunk = getNewInstancePrototype(vmThread);

   J9ROMMethod * romMethod = (J9ROMMethod *) J9ROMCLASS_ROMMETHODS(clazz->romClass);
   J9Method * ramMethods = (J9Method *) (clazz->ramMethods);
   for (uint32_t m = 0; m < clazz->romClass->romMethodCount; m++)
      {
      J9Method * method = &ramMethods[m];
      if (!(romMethod->modifiers & (J9AccNative | J9AccAbstract))
         && method != newInstanceThunk
         && !TR::CompilationInfo::isCompiled(method)
         && !fe->isThunkArchetype(method))
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
         if (plan)
            {
            plan->setIsExplicitCompilation(true);
            // If the controller decides to compile this method, trigger the compilation and wait here

               { // scope for details
               TR::IlGeneratorMethodDetails details(method);
               compInfo->compileMethod(vmThread, details, 0, TR_no, NULL, &queued, plan);
               }

            if (!queued && newPlanCreated)
               TR_OptimizationPlan::freeOptimizationPlan(plan);
            }
         else // No memory to create an optimizationPlan
            {
            // There is no point to try to compile other methods if we cannot
            // create an optimization plan. Bail out
            break;
            }
         }
      romMethod = nextROMMethod(romMethod);
      }

   if (threadHadNoVMAccess)
      releaseVMAccess(vmThread);

   return 1;
   }


extern "C" IDATA
compileClass(J9VMThread * vmThread, jclass clazzParm)
   {
   J9JavaVM * javaVM = vmThread->javaVM;
   J9Class * clazz;
   IDATA rc;

   acquireVMAccess(vmThread);
   clazz = J9VM_J9CLASS_FROM_JCLASS(vmThread, clazzParm);
   rc = internalCompileClass(vmThread, clazz);
   releaseVMAccess(vmThread);

   return rc;
   }


extern "C" IDATA
compileClasses(J9VMThread * vmThread, const char * pattern)
   {
   IDATA foundClassToCompile = 0;
   J9JavaVM * javaVM = vmThread->javaVM;
   J9JITConfig * jitConfig = javaVM->jitConfig;
   TR_FrontEnd * vm = TR_J9VMBase::get(jitConfig, vmThread);
   TR::CompilationInfo * compInfo = TR::CompilationInfo::get(jitConfig);
   if (!compInfo)
      return foundClassToCompile;
   PORT_ACCESS_FROM_JAVAVM(javaVM);
   J9ClassWalkState classWalkState;

   #define PATTERN_BUFFER_LENGTH 256
   char patternBuffer[PATTERN_BUFFER_LENGTH];
   char * patternString = patternBuffer;
   int32_t patternLength = strlen(pattern);
   bool freePatternString = false;

   if (patternLength >= PATTERN_BUFFER_LENGTH)
      {
      patternString = (char*)j9mem_allocate_memory(patternLength + 1, J9MEM_CATEGORY_JIT);
      if (!patternString)
         return false;
      freePatternString = true;
      }
   #undef PATTERN_BUFFER_LENGTH

   memcpy(patternString, (char *)pattern, patternLength + 1);

   /* Slashify the className */
   for (int32_t i = 0; i < patternLength; ++i)
      if (patternString[i] == '.')
         patternString[i] = '/';

   bool threadHadNoVMAccess = (!(vmThread->publicFlags & J9_PUBLIC_FLAGS_VM_ACCESS));
   if (threadHadNoVMAccess)
      acquireVMAccess(vmThread);

   J9Class * clazz = javaVM->internalVMFunctions->allLiveClassesStartDo(&classWalkState, javaVM, NULL);
   TR_LinkHead0<TR_ClassHolder> *classList = compInfo->getListOfClassesToCompile();
   bool classListWasEmpty = classList ? false : true;

   bool abortFlag = false;
   while (clazz && !abortFlag)
      {
      if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(clazz->romClass))
         {
         J9UTF8 * clazzUTRF8 = J9ROMCLASS_CLASSNAME(clazz->romClass);

         // j9tty_printf(PORTLIB, "Class %.*s\n", J9UTF8_LENGTH(clazz), J9UTF8_DATA(clazz));

         #define CLASSNAME_BUFFER_LENGTH 1000
         char classNameBuffer[CLASSNAME_BUFFER_LENGTH];
         char * classNameString = classNameBuffer;
         bool freeClassNameString = false;
         if (J9UTF8_LENGTH(clazzUTRF8) >= CLASSNAME_BUFFER_LENGTH)
            {
            classNameString = (char*)j9mem_allocate_memory((J9UTF8_LENGTH(clazzUTRF8)+1)*sizeof(char), J9MEM_CATEGORY_JIT);
            if (!classNameString)
               {
               clazz = javaVM->internalVMFunctions->allLiveClassesNextDo(&classWalkState);
               continue;
               }
            freeClassNameString = true;
            }
         #undef CLASSNAME_BUFFER_LENGTH

         strncpy(classNameString, (char*)J9UTF8_DATA(clazzUTRF8), J9UTF8_LENGTH(clazzUTRF8));

         if (strstr(classNameString, patternString))
            {
            TR_ClassHolder * c = NULL;
            if (!classListWasEmpty) // If the list was not empty we must search for duplicates
               {
               for (c = classList->getFirst(); c; c = c->getNext())
                  if (c->getClass() == clazz)
                     break; // duplicate found
               }

            if (!c) // no duplicate found
               {
               TR_ClassHolder *ch = new (PERSISTENT_NEW) TR_ClassHolder(clazz);
               if (ch)
                  {
                  classList->add(ch);
                  foundClassToCompile = 1;
                  }
               else
                  {
                  // cleanup after yourself
                  abortFlag = true; // If we cannot allocate a small piece of persistent memory we must bail out
                  }
               }
            }

         if (freeClassNameString)
            j9mem_free_memory(classNameString);
         } // end if

      clazz = javaVM->internalVMFunctions->allLiveClassesNextDo(&classWalkState);
      }

   javaVM->internalVMFunctions->allLiveClassesEndDo(&classWalkState);

   if (freePatternString)
      j9mem_free_memory(patternString);

   // now compile all classes from the list
   TR_ClassHolder * trj9class;
   do {
         {
         TR::ClassTableCriticalSection compileClasses(vm);
         trj9class = classList->pop();
         }

      if (trj9class)
         {
         // compile all methods from this class
         if (!abortFlag)
            internalCompileClass(vmThread, trj9class->getClass());
         // free the persistent memory
         TR_Memory::jitPersistentFree(trj9class);
         }
      } while (trj9class);

   if (threadHadNoVMAccess)
       releaseVMAccess(vmThread);
   return foundClassToCompile;
   }


// -----------------------------------------------------------------------------
// JIT initialization
// -----------------------------------------------------------------------------

TR_PersistentMemory * initializePersistentMemory(J9JITConfig * jitConfig);

extern "C" jint
onLoadInternal(
      J9JavaVM *javaVM,
      J9JITConfig *jitConfig,
      char *xjitCommandLine,
      char *xaotCommandLine,
      UDATA flagsParm,
      void *reserved0,
      I_32 xnojit)
   {
   PORT_ACCESS_FROM_JAVAVM(javaVM);

   jitConfig->javaVM = javaVM;
   jitConfig->jitLevelName = TR_BUILD_NAME;

   TR::CodeCache *firstCodeCache;

   /* Allocate the code and data cache lists */
   if (!(jitConfig->codeCacheList))
      {
      if ((jitConfig->codeCacheList = javaVM->internalVMFunctions->allocateMemorySegmentList(javaVM, 3, J9MEM_CATEGORY_JIT)) == NULL)
         return -1;
      }
   if (!(jitConfig->dataCacheList))
      {
      if ((jitConfig->dataCacheList = javaVM->internalVMFunctions->allocateMemorySegmentList(javaVM, 3, J9MEM_CATEGORY_JIT)) == NULL)
         return -1;
      }

   // Callbacks for code cache allocation pointers
   jitConfig->codeCacheWarmAlloc = getCodeCacheWarmAlloc;
   jitConfig->codeCacheColdAlloc = getCodeCacheColdAlloc;

   /* Allocate the privateConfig structure.  Note that the AOTRT DLL does not allocate this structure */
   jitConfig->privateConfig = j9mem_allocate_memory(sizeof(TR_JitPrivateConfig), J9MEM_CATEGORY_JIT);
   if (jitConfig->privateConfig == NULL)  // Memory Allocation Failure.
      return -1;

   memset(jitConfig->privateConfig, 0, sizeof(TR_JitPrivateConfig));
   static_cast<TR_JitPrivateConfig *>(jitConfig->privateConfig)->aotValidHeader = TR_maybe;
   uintptr_t initialFlags = flagsParm;

   // Force use of register maps
   //
   // todo: this has to be a runtime test for x-compilation
   //
   initialFlags |= J9JIT_CG_REGISTER_MAPS;

   initialFlags |= J9JIT_GROW_CACHES;

   jitConfig->runtimeFlags |= initialFlags;  // assumes default of bestAvail if nothing set from cmdline

   jitConfig->j9jit_printf = j9jit_printf;
   ((TR_JitPrivateConfig*)jitConfig->privateConfig)->j9jitrt_printf = j9jitrt_printf;
   ((TR_JitPrivateConfig*)jitConfig->privateConfig)->j9jitrt_lock_log = j9jitrt_lock_log;
   ((TR_JitPrivateConfig*)jitConfig->privateConfig)->j9jitrt_unlock_log = j9jitrt_unlock_log;

   // set up the entryPoints for compiling and for support of the
   // java.lang.Compiler class.
   jitConfig->entryPoint = j9jit_testarossa;
   jitConfig->retranslateWithPreparation = retranslateWithPreparation;
   jitConfig->retranslateWithPreparationForMethodRedefinition = retranslateWithPreparationForMethodRedefinition;
   jitConfig->entryPointForNewInstance = j9jit_createNewInstanceThunk;
#if defined(TRANSLATE_METHODHANDLE_TAKES_FLAGS)
   jitConfig->translateMethodHandle = translateMethodHandle;
#else
   jitConfig->translateMethodHandle = old_translateMethodHandle;
#endif
   jitConfig->disableJit = disableJit;
   jitConfig->enableJit = enableJit;
   jitConfig->compileClass = compileClass;
   jitConfig->compileClasses = compileClasses;
#ifdef ENABLE_GPU
   jitConfig->launchGPU = launchGPU;
#endif
   jitConfig->promoteGPUCompile = promoteGPUCompile;
   jitConfig->command = command;
   jitConfig->bcSizeLimit = 0xFFFF;

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   //Add support for AES methods.
   jitConfig->isAESSupportedByHardware = isAESSupportedByHardware;
   jitConfig->expandAESKeyInHardware = expandAESKeyInHardware;
   jitConfig->doAESInHardware = doAESInHardware;

   //Add support for SHA methods.
   jitConfig->getCryptoHardwareFeatures = getCryptoHardwareFeatures;
   jitConfig->doSha256InHardware = doSha256InHardware;
   jitConfig->doSha512InHardware = doSha512InHardware;
#endif

   //set up our vlog Manager
   TR_VerboseLog::initialize(jitConfig);
   initializePersistentMemory(jitConfig);

   // set up entry point for starting JITServer
#if defined(J9VM_OPT_JITSERVER)
   jitConfig->startJITServer = startJITServer;
   jitConfig->waitJITServerTermination = waitJITServerTermination;
#endif /* J9VM_OPT_JITSERVER */

   TR_PersistentMemory * persistentMemory = (TR_PersistentMemory *)jitConfig->scratchSegment;
   if (persistentMemory == NULL)
      return -1;

   // JITServer: persistentCHTable used to be inited here, but we have to move it after JITServer commandline opts
   // setting it to null here to catch anything that assumes it's set between here and the new init code.
   persistentMemory->getPersistentInfo()->setPersistentCHTable(NULL);

   if (!TR::CompilationInfo::createCompilationInfo(jitConfig))
      return -1;

   if (!TR_J9VMBase::createGlobalFrontEnd(jitConfig, TR::CompilationInfo::get()))
      return -1;

   TR_J9VMBase * feWithoutThread(TR_J9VMBase::get(jitConfig, NULL));
   if (!feWithoutThread)
      return -1;

   /*aotmcc-move it here from below !!!! this is after the jitConfig->runtimeFlags=AOT is set*/
   /*also jitConfig->javaVM = javaVM has to be set for the case we run j9.exe -Xnoaot ....*/
   J9VMThread *curThread = javaVM->internalVMFunctions->currentVMThread(javaVM);
   TR_J9VMBase * fe = TR_J9VMBase::get(jitConfig, curThread);

   int32_t numCodeCachesToCreateAtStartup = 1;

#if defined(J9ZOS390) && !defined(TR_TARGET_64BIT)
    // zOS 31-bit
    J9JavaVM * vm = javaVM; //macro FIND_AND_CONSUME_ARG refers to javaVM as vm
    if (isQuickstart)
       {
       jitConfig->codeCacheKB = 1024;
       jitConfig->dataCacheKB = 1024;
       }
   else
       {
       jitConfig->codeCacheKB = 2048;
       jitConfig->dataCacheKB = 2048;
       }
#else
#if (defined(TR_HOST_POWER) || defined(TR_HOST_S390) || (defined(TR_HOST_X86) && defined(TR_HOST_64BIT)))
      jitConfig->codeCacheKB = 2048;
      jitConfig->dataCacheKB = 2048;

      //zOS will set the code cache to create at startup after options are enabled below
#if !defined(J9ZOS390)
      if (!isQuickstart) // for -Xquickstart start with one code cache
         numCodeCachesToCreateAtStartup = 4;
#endif
#else
      jitConfig->codeCacheKB = 2048; // WAS-throughput guided change
      //jitConfig->codeCacheKB = J9_JIT_CODE_CACHE_SIZE / 1024;  // bytes -> k
      jitConfig->dataCacheKB = 512;  // bytes -> k
#endif
#endif


#if defined(J9VM_OPT_JITSERVER)
   if (javaVM->internalVMFunctions->isJITServerEnabled(javaVM))
      {
      // Use smaller caches on the server because
      // just one method's data is going to be stored there at a time
      jitConfig->codeCacheKB = 1024;
      jitConfig->dataCacheKB = 1024;
      }
#endif

   if (fe->isAOT_DEPRECATED_DO_NOT_USE())
      {
      jitConfig->codeCacheTotalKB = 128 * 1024; // default limit on amount of code that can be generated (128MB)
      jitConfig->dataCacheTotalKB =  64 * 1024; // default limit on amount of meta-data that can be generated (64MB)
      }
   else
      {
#if defined(TR_TARGET_64BIT)
         jitConfig->codeCacheTotalKB = 256 * 1024;
         jitConfig->dataCacheTotalKB = 384 * 1024;
#else
         jitConfig->codeCacheTotalKB = 64 * 1024;
         jitConfig->dataCacheTotalKB = 192 * 1024;
#endif

#if defined(J9ZTPF)
         /*
          * The z/TPF OS does not have the ability to reserve memory. It allocates whatever it
          * is asked to allocate. Allocate the code cache based on a percentage (20%) of memory
          * the process is allowed to have; cap the maximum using the default 256m and use a 1m floor.
          * MAXXMMES is specified as the number of 1MB frames.
          * Use the default 2048 KB data cache. The default values for z/TPF can be
          * overridden via the command line interface.
          */
         const uint16_t ZTPF_CODE_CACHE_DIVISOR = 5;

         /* Retrieve the maximum number of 1MB frames that a z/TPF process can have and */
         /* then take 20% of that value to use for the code cache size. */
         const uint16_t physMemory = *(static_cast<uint16_t*>(cinfc_fast(CINFC_CMMMMES)) + 4) / ZTPF_CODE_CACHE_DIVISOR;

         /* Provide a 1MB floor and 256MB ceiling for the code cache size */
         if (physMemory <= 1)
            {
            jitConfig->codeCacheKB = 1024;
            jitConfig->codeCacheTotalKB = 1024;
            }
         else if (physMemory < 256)
            {
            jitConfig->codeCacheKB = 2048;
            jitConfig->codeCacheTotalKB = physMemory * 1024;
            }
         else
            {
            jitConfig->codeCacheKB = 2048;
            jitConfig->codeCacheTotalKB = 256 * 1024;
            }

         jitConfig->dataCacheKB = 2048;
         jitConfig->dataCacheTotalKB = 384 * 1024;
#endif
      }

   TR::Options::setScratchSpaceLimit(DEFAULT_SCRATCH_SPACE_LIMIT_KB * 1024);
   TR::Options::setScratchSpaceLowerBound(DEFAULT_SCRATCH_SPACE_LOWER_BOUND_KB * 1024);

   jitConfig->samplingFrequency = TR::Options::getSamplingFrequency();

#if defined(DEBUG)
      static char * breakOnLoad = feGetEnv("TR_BreakOnLoad");
      if (breakOnLoad)
         TR::Compiler->debug.breakPoint();
#endif

   // Parse the command line options
   //
   if (xaotCommandLine)
      {
      char *endAOTOptions = TR::Options::processOptionsAOT(xaotCommandLine, jitConfig, feWithoutThread);
      if (*endAOTOptions)
         {
         // Generate AOT error message only if error occurs in -Xaot processing
         if (0 != (TR::Options::_processOptionsStatus & TR_AOTProcessErrorAOTOpts))
            {
            scan_failed(PORTLIB, "AOT", endAOTOptions);
            printf("<AOT: fatal error, invalid command line>\n");
#ifdef DEBUG
            printf("   for help use -Xaot:help\n\n");
#endif
            }
         return -1;
         }
      }

   char *endJITOptions = TR::Options::processOptionsJIT(xjitCommandLine, jitConfig, feWithoutThread);

   if (*endJITOptions)
      {
      // Generate JIT error message only if error occurs in -Xjit processing
      if (0 != (TR::Options::_processOptionsStatus & TR_JITProcessErrorJITOpts))
         {
         scan_failed(PORTLIB, "JIT", endJITOptions);
         printf("<JIT: fatal error, invalid command line>\n");
#ifdef DEBUG
            printf("   for help use -Xjit:help\n\n");
#endif
         }
      return -1;
      }

   // Now that the options have been processed we can initialize the RuntimeAssumptionTables
   // If we cannot allocate various runtime assumption hash tables, fail the JVM

   fe->initializeSystemProperties();

   // Allocate trampolines for z/OS 64-bit
#if defined(J9ZOS390)
   if (TR::Options::getCmdLineOptions()->getOption(TR_EnableRMODE64) && !isQuickstart)
      numCodeCachesToCreateAtStartup = 4;
#endif


#if defined(J9VM_OPT_JITSERVER)
   if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() != JITServer::SERVER)
#endif
      {
      if (!persistentMemory->getPersistentInfo()->getRuntimeAssumptionTable()->init())
         return -1;
      }

   TR_PersistentClassLoaderTable *loaderTable = new (PERSISTENT_NEW) TR_PersistentClassLoaderTable(persistentMemory);
   if (loaderTable == NULL)
      return -1;
   persistentMemory->getPersistentInfo()->setPersistentClassLoaderTable(loaderTable);

   jitConfig->iprofilerBufferSize = TR::Options::_iprofilerBufferSize;   //1024;

   javaVM->minimumSuperclassArraySize = TR::Options::_minimumSuperclassArraySize;

   if (!jitConfig->tracingHook && TR::Options::requiresDebugObject())
      {
      jitConfig->tracingHook = (void*) (TR_CreateDebug_t)createDebugObject;
      }

   if (!jitConfig->tracingHook)
      TR::Options::suppressLogFileBecauseDebugObjectNotCreated();

   TR_AOTStats *aotStats = (TR_AOTStats *)j9mem_allocate_memory(sizeof(TR_AOTStats), J9MEM_CATEGORY_JIT);
   memset(aotStats, 0, sizeof(TR_AOTStats));
   ((TR_JitPrivateConfig*)jitConfig->privateConfig)->aotStats = aotStats;

   TR::CodeCacheManager *codeCacheManager = (TR::CodeCacheManager *) j9mem_allocate_memory(sizeof(TR::CodeCacheManager), J9MEM_CATEGORY_JIT);
   if (codeCacheManager == NULL)
      return -1;
   memset(codeCacheManager, 0, sizeof(TR::CodeCacheManager));

   // must initialize manager using the global (not thread specific) fe because current thread isn't guaranteed to live longer than the manager
   new (codeCacheManager) TR::CodeCacheManager(feWithoutThread, TR::Compiler->rawAllocator);

   TR::CodeCacheConfig &codeCacheConfig = codeCacheManager->codeCacheConfig();

   // Do not allow code caches smaller than 128KB
   if (jitConfig->codeCacheKB < 128)
      jitConfig->codeCacheKB = 128;
   if (!fe->isAOT_DEPRECATED_DO_NOT_USE())
      {
      if (jitConfig->codeCacheKB > 32768)
         jitConfig->codeCacheKB = 32768;
      }

   if (jitConfig->codeCacheKB > jitConfig->codeCacheTotalKB)
      {
      /* Handle case where user has set codeCacheTotalKB smaller than codeCacheKB (e.g. -Xjit:codeTotal=256) by setting codeCacheKB = codeCacheTotalKB */
      jitConfig->codeCacheKB = jitConfig->codeCacheTotalKB;
      }

   if (jitConfig->dataCacheKB > jitConfig->dataCacheTotalKB)
      {
      /* Handle case where user has set dataCacheTotalKB smaller than dataCacheKB (e.g. -Xjit:dataTotal=256) by setting dataCacheKB = dataCacheTotalKB */
      jitConfig->dataCacheKB = jitConfig->dataCacheTotalKB;
      }

   I_32 maxNumberOfCodeCaches = jitConfig->codeCacheTotalKB / jitConfig->codeCacheKB;
   if (fe->isAOT_DEPRECATED_DO_NOT_USE())
      maxNumberOfCodeCaches = 1;

   // setupCodeCacheParameters must stay before  TR::CodeCacheManager::initialize() because it needs trampolineCodeSize
   setupCodeCacheParameters(&codeCacheConfig._trampolineCodeSize, &codeCacheConfig._mccCallbacks, &codeCacheConfig._numOfRuntimeHelpers, &codeCacheConfig._CCPreLoadedCodeSize);

   if (!TR_TranslationArtifactManager::initializeGlobalArtifactManager(jitConfig->translationArtifacts, jitConfig->javaVM))
      return -1;

   codeCacheConfig._allowedToGrowCache = (javaVM->jitConfig->runtimeFlags & J9JIT_GROW_CACHES) != 0;
   codeCacheConfig._lowCodeCacheThreshold = TR::Options::_lowCodeCacheThreshold;
   codeCacheConfig._verboseCodeCache = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseCodeCache);
   codeCacheConfig._verbosePerformance = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerbosePerformance);
   codeCacheConfig._verboseReclamation = TR::Options::getCmdLineOptions()->getVerboseOption(TR_VerboseCodeCacheReclamation);
   codeCacheConfig._doSanityChecks = TR::Options::getCmdLineOptions()->getOption(TR_CodeCacheSanityCheck);
   codeCacheConfig._codeCacheTotalKB = jitConfig->codeCacheTotalKB;
   codeCacheConfig._codeCacheKB = jitConfig->codeCacheKB;
   codeCacheConfig._codeCachePadKB = jitConfig->codeCachePadKB;
   codeCacheConfig._codeCacheAlignment = jitConfig->codeCacheAlignment;
   codeCacheConfig._codeCacheFreeBlockRecylingEnabled = !TR::Options::getCmdLineOptions()->getOption(TR_DisableFreeCodeCacheBlockRecycling);
   codeCacheConfig._largeCodePageSize = jitConfig->largeCodePageSize;
   codeCacheConfig._largeCodePageFlags = jitConfig->largeCodePageFlags;
   codeCacheConfig._maxNumberOfCodeCaches = maxNumberOfCodeCaches;
   codeCacheConfig._canChangeNumCodeCaches = (TR::Options::getCmdLineOptions()->getNumCodeCachesToCreateAtStartup() <= 0);

   static char * segmentCarving = feGetEnv("TR_CodeCacheConsolidation");
   bool useConsolidatedCodeCache = segmentCarving != NULL ||
                                   TR::Options::getCmdLineOptions()->getOption(TR_EnableCodeCacheConsolidation);

   if (TR::Options::getCmdLineOptions()->getNumCodeCachesToCreateAtStartup() > 0) // user has specified option
      numCodeCachesToCreateAtStartup = TR::Options::getCmdLineOptions()->getNumCodeCachesToCreateAtStartup();
   // We cannot create at startup more code caches than the maximum
   if (numCodeCachesToCreateAtStartup > maxNumberOfCodeCaches)
      numCodeCachesToCreateAtStartup = maxNumberOfCodeCaches;

   firstCodeCache = codeCacheManager->initialize(useConsolidatedCodeCache, numCodeCachesToCreateAtStartup);

   // large code page size may have been updated by initialize(), so copy back to jitConfig
   jitConfig->largeCodePageSize = (UDATA) codeCacheConfig.largeCodePageSize();

   if (firstCodeCache == NULL)
      return -1;
   jitConfig->codeCache = firstCodeCache->j9segment();
   ((TR_JitPrivateConfig *) jitConfig->privateConfig)->codeCacheManager = codeCacheManager; // for kca's benefit

   if (fe->isAOT_DEPRECATED_DO_NOT_USE())
      {
      jitConfig->codeCache->heapAlloc = firstCodeCache->getCodeTop();
#if defined(TR_TARGET_X86) && defined(TR_HOST_32BIT)
      javaVM->jitConfig = (J9JITConfig *)jitConfig;
      queryX86TargetCPUID(javaVM);
#endif
      }

   if (!fe->isAOT_DEPRECATED_DO_NOT_USE())
      {
      J9VMThread *currentVMThread = javaVM->internalVMFunctions->currentVMThread(javaVM);

      if (currentVMThread)
         {
         currentVMThread->maxProfilingCount = (UDATA)encodeCount(TR::Options::_maxIprofilingCountInStartupMode);
         }

      #if defined(TR_TARGET_S390)
         // allocate 4K
         jitConfig->pseudoTOC = j9mem_allocate_memory(4096, J9MEM_CATEGORY_JIT);
         if (!jitConfig->pseudoTOC)
            return -1;

         if (currentVMThread)
            currentVMThread->codertTOC = (void *)jitConfig->pseudoTOC;
      #endif

      }

   /* allocate the data cache */
   if (jitConfig->dataCacheKB < 1)
      {
      printf("<JIT: fatal error, data cache size must be at least 1 Kb>\n");
      return -1;
      }
   if (!TR_DataCacheManager::initialize(jitConfig))
      {
      printf("{JIT: fatal error, failed to allocate %" OMR_PRIuPTR " Kb data cache}\n", jitConfig->dataCacheKB);
      return -1;
      }

   jitConfig->thunkLookUpNameAndSig = &j9ThunkLookupNameAndSig;

   TR::CompilationInfo * compInfo = TR::CompilationInfo::get();

   // Now that we have all options (and before starting the compilation thread) we
   // should initialize the compilationController
   //
   TR::CompilationController::init(compInfo);

   if (!TR::CompilationController::useController()) // test if initialization succeeded
      return -1;

   // Create the CpuUtilization object after options processing
   {
   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   CpuUtilization* cpuUtil = new (PERSISTENT_NEW) CpuUtilization(jitConfig);
   compInfo->setCpuUtil(cpuUtil);
   }

   // disable CPU utilization monitoring if told to do so on command line
   if (TR::Options::getCmdLineOptions()->getOption(TR_DisableCPUUtilization))
      compInfo->getCpuUtil()->disable();
   else
      compInfo->getCpuUtil()->updateCpuUtil(jitConfig);

   // Need to let VM know that we will be using a machines vector facility (so it can save/restore preserved regs),
   // early in JIT startup to prevent subtle FP bugs
#ifdef TR_TARGET_S390
   if (TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_VECTOR_FACILITY) && !TR::Options::getCmdLineOptions()->getOption(TR_DisableSIMD))
      {
      javaVM->extendedRuntimeFlags |= J9_EXTENDED_RUNTIME_USE_VECTOR_REGISTERS;
      }
#elif defined(TR_TARGET_POWER) && defined(TR_TARGET_64BIT)
   if(TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) && TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX) && !TR::Options::getCmdLineOptions()->getOption(TR_DisableSIMD))
      {
      javaVM->extendedRuntimeFlags |= J9_EXTENDED_RUNTIME_USE_VECTOR_REGISTERS;
      }
#endif

   // create comp threads if compiling on separate thread
   if (compInfo->useSeparateCompilationThread())
      {
      // Address the case where the number of compilation threads is higher than the
      // maximum number of code caches
      if (TR::Options::_numUsableCompilationThreads > maxNumberOfCodeCaches)
         {
#if defined(J9VM_OPT_JITSERVER)
         if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
            {
            fprintf(stderr,
               "Requested number of compilation threads is larger than the maximum number of code caches: %d > %d.\n"
               "Use the -Xcodecachetotal option to increase the maximum total size of the code cache.",
               TR::Options::_numUsableCompilationThreads, maxNumberOfCodeCaches
            );
            return -1;
            }
#endif /* defined(J9VM_OPT_JITSERVER) */
         TR::Options::_numUsableCompilationThreads = maxNumberOfCodeCaches;
         }

      compInfo->updateNumUsableCompThreads(TR::Options::_numUsableCompilationThreads);

      if (!compInfo->allocateCompilationThreads(TR::Options::_numUsableCompilationThreads))
         {
         fprintf(stderr, "onLoadInternal: Failed to set up %d compilation threads\n", TR::Options::_numUsableCompilationThreads);
         return -1;
         }

      // create regular compilation threads
      int32_t highestThreadID = 0;
      for (; highestThreadID < TR::Options::_numUsableCompilationThreads; highestThreadID++)
         {
         if (compInfo->startCompilationThread(-1, highestThreadID, /* isDiagnosticThread */ false) != 0)
            {
            // Failed to start compilation thread.
            Trc_JIT_startCompThreadFailed(curThread);
            return -1;
            }
         }

      // If more than one diagnostic compilation thread is created, MAX_DIAGNOSTIC_COMP_THREADS needs to be updated
      // create diagnostic compilation thread
      if (compInfo->startCompilationThread(-1, highestThreadID, /* isDiagnosticThread */ true) != 0)
         {
         // Failed to start compilation thread.
         Trc_JIT_startCompThreadFailed(curThread);
         highestThreadID++;
         return -1;
         }

      // Create the monitor used for log handling in presence of multiple compilation threads
      // TODO: postpone this when we know that a log is actually used
      if (!compInfo->createLogMonitor())
         {
         fprintf(stderr, "cannot create log monitor\n");
         return -1;
         }
      }
   else
      {
      // Compilation on application thread
      if (!compInfo->initializeCompilationOnApplicationThread())
         return -1; // failure
      }

   if (!fe->isAOT_DEPRECATED_DO_NOT_USE() && !(jitConfig->runtimeFlags & J9JIT_TOSS_CODE))
      {
      javaVM->runtimeFlags |= J9_RUNTIME_JIT_ACTIVE;
      jitConfig->jitExclusiveVMShutdownPending = jitExclusiveVMShutdownPending;
      }

   // initialize the IProfiler

   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableInterpreterProfiling))
      {
#if defined(J9VM_OPT_JITSERVER)
      if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
         {
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->iProfiler = JITServerIProfiler::allocate(jitConfig);
         }
      else if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() == JITServer::CLIENT)
         {
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->iProfiler = JITClientIProfiler::allocate(jitConfig);
         }
      else
#endif
         {
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->iProfiler = TR_IProfiler::allocate(jitConfig);
         }
      if (!(((TR_JitPrivateConfig*)(jitConfig->privateConfig))->iProfiler))
         {
         // Warn that IProfiler was not allocated
         TR::Options::getCmdLineOptions()->setOption(TR_DisableInterpreterProfiling);
         // Warn that Interpreter Profiling was disabled
         }
      }
   else
      {
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->iProfiler = NULL;
      }

   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableJProfilerThread))
      {
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->jProfiler = TR_JProfilerThread::allocate();
      if (!(((TR_JitPrivateConfig*)(jitConfig->privateConfig))->jProfiler))
         {
         TR::Options::getCmdLineOptions()->setOption(TR_DisableJProfilerThread);
         }
      }
   else
      {
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->jProfiler = NULL;
      }

   vpMonitor = TR::Monitor::create("ValueProfilingMutex");

   // initialize the HWProfiler
   UDATA riInitializeFailed = 1;
   if (TR::Options::_hwProfilerEnabled == TR_yes)
      {
#if defined(TR_HOST_S390) && defined(BUILD_Z_RUNTIME_INSTRUMENTATION)
      if (TR::Compiler->target.cpu.supportsFeature(OMR_FEATURE_S390_RI))
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler = TR_ZHWProfiler::allocate(jitConfig);
#elif defined(TR_HOST_POWER)
#if !defined(J9OS_I5)
/* We disable it on current releases. May enable in future. */
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler = TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) ? TR_PPCHWProfiler::allocate(jitConfig) : NULL;
#else
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler = NULL;
#endif /* !defined(J9OS_I5) */
#endif

      //Initialize VM support for RI.
#ifdef J9VM_JIT_RUNTIME_INSTRUMENTATION
      if (NULL != ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler)
         riInitializeFailed = initializeJITRuntimeInstrumentation(javaVM);
#endif
      }
   else
      {
      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler = NULL;
      }

   if (riInitializeFailed || ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->hwProfiler == NULL)
      {
      // This will also internally call setRuntimeInstrumentationRecompilationEnabled(false)
      persistentMemory->getPersistentInfo()->setRuntimeInstrumentationEnabled(false);
      }
   else
      {
      persistentMemory->getPersistentInfo()->setRuntimeInstrumentationEnabled(true);

      if (TR::Options::getCmdLineOptions()->getOption(TR_EnableHardwareProfileRecompilation) &&
          !(TR::Options::getCmdLineOptions()->getFixedOptLevel() == -1 &&
            TR::Options::getCmdLineOptions()->getOption(TR_InhibitRecompilation)))
         persistentMemory->getPersistentInfo()->setRuntimeInstrumentationRecompilationEnabled(true);
      }

#if defined(J9VM_OPT_JITSERVER)
   // server-side CH table is initialized per-client
   if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() != JITServer::SERVER)
#endif
      {
      TR_PersistentCHTable *chtable;
#if defined(J9VM_OPT_JITSERVER)
      if (persistentMemory->getPersistentInfo()->getRemoteCompilationMode() == JITServer::CLIENT)
         {
         chtable = new (PERSISTENT_NEW) JITClientPersistentCHTable(persistentMemory);
         }
      else
#endif
         {
         chtable = new (PERSISTENT_NEW) TR_PersistentCHTable(persistentMemory);
         }
      if (chtable == NULL)
         return -1;
      persistentMemory->getPersistentInfo()->setPersistentCHTable(chtable);
      }

#if defined(J9VM_OPT_JITSERVER)
   if (JITServer::CommunicationStream::useSSL())
      {
      if (!JITServer::loadLibsslAndFindSymbols())
         return -1;
      }
   else
      {
      bool shareROMClasses = TR::Options::_shareROMClasses &&
                             (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER);
      bool useAOTCache = compInfo->getPersistentInfo()->getJITServerUseAOTCache();

      // ROMClass sharing and AOT cache use a hash implementation from SSL.
      // Disable them (with an error message to vlog) if we can't load the library.
      if ((shareROMClasses || useAOTCache) && !JITServer::loadLibsslAndFindSymbols())
         {
         TR::Options::_shareROMClasses = false;
         compInfo->getPersistentInfo()->setJITServerUseAOTCache(false);

         if (TR::Options::getVerboseOption(TR_VerboseJITServer))
            {
            if (shareROMClasses)
               TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer,
                                              "ERROR: Failed to load SSL library, disabling ROMClass sharing");
            if (useAOTCache)
               TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer,
                                              "ERROR: Failed to load SSL library, disabling AOT cache");
            }
         }
      }

   if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
      {
      JITServer::CommunicationStream::initConfigurationFlags();

      // Allocate the hashtable that holds information about clients
      compInfo->setClientSessionHT(ClientSessionHT::allocate());

      ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->listener = TR_Listener::allocate();
      if (!((TR_JitPrivateConfig*)(jitConfig->privateConfig))->listener)
         {
         // warn that Listener was not allocated
         j9tty_printf(PORTLIB, "JITServer Listener not allocated, abort.\n");
         return -1;
         }
      if (jitConfig->samplingFrequency != 0)
         {
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->statisticsThreadObject = JITServerStatisticsThread::allocate();
         if (!((TR_JitPrivateConfig*)(jitConfig->privateConfig))->statisticsThreadObject)
            {
            // warn that Statistics Thread was not allocated
            j9tty_printf(PORTLIB, "JITServer Statistics thread not allocated, abort.\n");
            return -1;
            }
         }
      else
         {
         ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->statisticsThreadObject = NULL;
         }

      //NOTE: This must be done only after the SSL library has been successfully loaded
      if (TR::Options::_shareROMClasses)
         {
         size_t numPartitions = std::max(1, TR::Options::_sharedROMClassCacheNumPartitions);
         auto cache = new (PERSISTENT_NEW) JITServerSharedROMClassCache(numPartitions);
         if (!cache)
            return -1;
         compInfo->setJITServerSharedROMClassCache(cache);
         }

      //NOTE: This must be done only after the SSL library has been successfully loaded
      if (compInfo->getPersistentInfo()->getJITServerUseAOTCache())
         {
         auto aotCacheMap = new (PERSISTENT_NEW) JITServerAOTCacheMap();
         if (!aotCacheMap)
            return -1;
         compInfo->setJITServerAOTCacheMap(aotCacheMap);
         }
      }
   else if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::CLIENT)
      {
      compInfo->setUnloadedClassesTempList(new (PERSISTENT_NEW) PersistentVector<TR_OpaqueClassBlock*>(
         PersistentVector<TR_OpaqueClassBlock*>::allocator_type(TR::Compiler->persistentAllocator())));

      compInfo->setIllegalFinalFieldModificationList(new (PERSISTENT_NEW) PersistentVector<TR_OpaqueClassBlock*>(
         PersistentVector<TR_OpaqueClassBlock*>::allocator_type(TR::Compiler->persistentAllocator())));

      compInfo->setNewlyExtendedClasses(new (PERSISTENT_NEW) PersistentUnorderedMap<TR_OpaqueClassBlock*, uint8_t>(
         PersistentUnorderedMap<TR_OpaqueClassBlock*, uint8_t>::allocator_type(TR::Compiler->persistentAllocator())));

      // Try to initialize SSL
      if (JITServer::ClientStream::static_init(compInfo->getPersistentInfo()) != 0)
         return -1;

      JITServer::CommunicationStream::initConfigurationFlags();

      if (compInfo->getPersistentInfo()->getJITServerUseAOTCache())
         {
         auto deserializer = new (PERSISTENT_NEW) JITServerAOTDeserializer(loaderTable);
         if (!deserializer)
            return -1;
         compInfo->setJITServerAOTDeserializer(deserializer);
         }
      }
#endif // J9VM_OPT_JITSERVER

#if defined(TR_HOST_S390)
   if (TR::Compiler->om.readBarrierType() != gc_modron_readbar_none)
      {
      // TODO (Guarded Storage): With the change to prevent lower trees, there's
      // an outstanding DLT issue where a LLGFSG is generated when loading the
      // DLT block from the J9VMThread instead of a LG. Disabling DLT until that
      // issue is fixed.
      TR::Options::getCmdLineOptions()->setOption(TR_DisableDynamicLoopTransfer);
      }
#endif

#if defined(TR_HOST_ARM64)
   // ArrayCopy transformations are not available in AArch64 yet.
   // OpenJ9 issue #6438 tracks the work to enable.
   //
   TR::Options::getCmdLineOptions()->setOption(TR_DisableArrayCopyOpts);
#endif

#ifdef J9VM_RAS_DUMP_AGENTS
   jitConfig->runJitdump = runJitdump;
#endif

   jitConfig->printAOTHeaderProcessorFeatures = printAOTHeaderProcessorFeatures;

   if (!TR::Compiler->target.cpu.isI386())
      {
      TR_J2IThunkTable *ieThunkTable = new (PERSISTENT_NEW) TR_J2IThunkTable(persistentMemory, "InvokeExactJ2IThunkTable");
      if (ieThunkTable == NULL)
         return -1;
      persistentMemory->getPersistentInfo()->setInvokeExactJ2IThunkTable(ieThunkTable);
      }
   return 0;
   }


extern "C" int32_t
aboutToBootstrap(J9JavaVM * javaVM, J9JITConfig * jitConfig)
   {
   char isSMP;
   PORT_ACCESS_FROM_JAVAVM(javaVM);
   bool isSharedAOT = false;

   if (!jitConfig)
      return -1;

#if defined(J9VM_INTERP_AOT_COMPILE_SUPPORT) && defined(J9VM_OPT_SHARED_CLASSES) && (defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_S390) || defined(TR_HOST_ARM) || defined(TR_HOST_ARM64))
   isSharedAOT = TR::Options::sharedClassCache();
#endif /* J9VM_INTERP_AOT_COMPILE_SUPPORT && J9VM_OPT_SHARED_CLASSES && TR_HOST_X86 && TR_HOST_S390 */

#if defined(J9VM_OPT_SHARED_CLASSES)
   // must be called before latePostProcess
   if (isSharedAOT)
      {
      TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
      J9SharedClassJavacoreDataDescriptor *javacoreDataPtr = compInfo->getAddrOfJavacoreData();

      TR_ASSERT(javaVM->sharedClassConfig, "sharedClassConfig must exist");
      if (javaVM->sharedClassConfig->getJavacoreData)
         {
         memset(javacoreDataPtr, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
         if (javaVM->sharedClassConfig->getJavacoreData(javaVM, javacoreDataPtr))
            {
            if (javacoreDataPtr->numAOTMethods > TR::Options::_aotWarmSCCThreshold)
               compInfo->setIsWarmSCC(TR_yes);
            else
               compInfo->setIsWarmSCC(TR_no);
            }
         }

      if (TR::Options::getAOTCmdLineOptions()->getOption(TR_DisablePersistIProfile) ||
          TR::Options::getJITCmdLineOptions()->getOption(TR_DisablePersistIProfile))
         {
         javaVM->sharedClassConfig->runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_JITDATA;
         }
      else if ((javaVM->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_JITDATA) == 0)
         {
         printf("\n ** sc disabled **\n");

         TR::Options::getAOTCmdLineOptions()->setOption(TR_DisablePersistIProfile);
         }
      }
#endif

   char * endOptionsAOT = TR::Options::latePostProcessAOT(jitConfig);
   if ((intptr_t)endOptionsAOT == 1)
      return 1;

   if (endOptionsAOT)
      {
      scan_failed(PORTLIB, "AOT", endOptionsAOT);
      printf("<JIT: fatal error, invalid command line>\n");
      #ifdef DEBUG
         printf("   for help use -Xaot:help\n\n");
      #endif
      return -1;
      }

   char * endOptions = TR::Options::latePostProcessJIT(jitConfig);
   if ((intptr_t)endOptions == 1)
      return 1;

   if (endOptions)
      {
      scan_failed(PORTLIB, "JIT", endOptions);
      printf("<JIT: fatal error, invalid command line>\n");
      #ifdef DEBUG
         printf("   for help use -Xjit:help\n\n");
      #endif
      return -1;
      }

   if (!TR::Options::getCmdLineOptions()->allowRecompilation() || !TR::Options::getAOTCmdLineOptions()->allowRecompilation())
      {
      TR::Options::getCmdLineOptions()->setOption(TR_DisableCHOpts);
      TR::Options::getAOTCmdLineOptions()->setOption(TR_DisableCHOpts);
      }

   // Get local var names if available (ie. classfile was compiled with -g).
   // We just check getDebug() for lack of a reliable way to check whether there are any methods being logged.
   //
   // Do this after all option processing because we don't want the option
   // processing to think the user has requested a local variable table.  That
   // makes it drop into FSD mode.
   //
   if (TR::Options::getDebug())
      javaVM->requiredDebugAttributes |= J9VM_DEBUG_ATTRIBUTE_LOCAL_VARIABLE_TABLE;

   J9VMThread *curThread = javaVM->internalVMFunctions->currentVMThread(javaVM);
   TR_J9VMBase *vm = TR_J9VMBase::get(jitConfig, curThread);
   TR::CompilationInfo * compInfo = TR::CompilationInfo::get(jitConfig);

   /* Initialize helpers */
   codert_init_helpers_and_targets(jitConfig, TR::Compiler->target.isSMP());

   if (vm->isAOT_DEPRECATED_DO_NOT_USE() || (jitConfig->runtimeFlags & J9JIT_TOSS_CODE))
      return 0;


#if defined(J9VM_OPT_JITSERVER)
   if (compInfo->getPersistentInfo()->getRemoteCompilationMode() != JITServer::SERVER)
#endif
      {
      /* jit specific helpers */
      initializeJitRuntimeHelperTable(TR::Compiler->target.isSMP());
      }

#if defined(TR_TARGET_POWER)
   if (TR::Compiler->target.cpu.isPower())
      {
      void  * tocBase = ppcPicTrampInit(vm, compInfo->getPersistentInfo());
      if ( tocBase == reinterpret_cast<void *>(0x1) )
         {
         printf("<JIT: Cannot allocate TableOfConstants\n");
         return -1; // fail the JVM
         }

   #if defined(TR_TARGET_64BIT) && defined(TR_HOST_POWER)
      jitConfig->pseudoTOC = tocBase;
   #endif
      }
#endif


#if defined(J9VM_OPT_SHARED_CLASSES)
   if (isSharedAOT)
      {
      bool validateSCC = true;

#if defined(J9VM_OPT_JITSERVER)
      if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
         validateSCC = false;
#endif

      if (validateSCC)
         {
         /* If AOT Shared Classes is turned ON, perform compatibility checks for AOT Shared Classes
          *
          * This check has to be done after latePostProcessJIT so that all the necessary JIT options
          * can be set
          */
         TR_J9VMBase *fe = TR_J9VMBase::get(jitConfig, curThread);
         if (!compInfo->reloRuntime()->validateAOTHeader(fe, curThread))
            {
            TR_ASSERT_FATAL(static_cast<TR_JitPrivateConfig *>(jitConfig->privateConfig)->aotValidHeader != TR_yes,
                            "aotValidHeader is TR_yes after failing to validate AOT header\n");

            /* If this is the second run, then failing to validate AOT header will cause aotValidHeader
             * to be TR_no, in which case the SCC is not valid for use. However, if this is the first
             * run, then aotValidHeader will be TR_maybe; try to store the AOT Header in this case.
             */
            if (static_cast<TR_JitPrivateConfig *>(jitConfig->privateConfig)->aotValidHeader == TR_no
                || !compInfo->reloRuntime()->storeAOTHeader(fe, curThread))
               {
               static_cast<TR_JitPrivateConfig *>(jitConfig->privateConfig)->aotValidHeader = TR_no;
               TR::Options::getAOTCmdLineOptions()->setOption(TR_NoLoadAOT);
               TR::Options::getAOTCmdLineOptions()->setOption(TR_NoStoreAOT);
               TR::Options::setSharedClassCache(false);
               TR_J9SharedCache::setSharedCacheDisabledReason(TR_J9SharedCache::AOT_DISABLED);
               }
            }
         else
            {
            TR::Compiler->relocatableTarget.cpu = TR::CPU::customize(compInfo->reloRuntime()->getProcessorDescriptionFromSCC(fe, curThread));
            jitConfig->relocatableTargetProcessor = TR::Compiler->relocatableTarget.cpu.getProcessorDescription();
            }
         }

      if (TR::Options::getAOTCmdLineOptions()->getOption(TR_NoStoreAOT))
         {
         javaVM->sharedClassConfig->runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_AOT;
         TR_J9SharedCache::setSharedCacheDisabledReason(TR_J9SharedCache::AOT_DISABLED);
#if defined(J9VM_OPT_JITSERVER)
         if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
            {
            fprintf(stderr, "Error: -Xaot:nostore option is not compatible with JITServer mode.");
            return -1;
            }
#endif
         }
      else if ((javaVM->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_AOT) == 0)
         {
         TR::Options::getAOTCmdLineOptions()->setOption(TR_NoStoreAOT);
         TR_J9SharedCache::setSharedCacheDisabledReason(TR_J9SharedCache::AOT_DISABLED);
#if defined(J9VM_OPT_JITSERVER)
         if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
            {
            fprintf(stderr, "Error: -Xnoaot option must not be specified for JITServer.");
            return -1;
            }
#endif
         }
      }
#endif

   /* If the JVM is in CRIU mode, then the JIT should be limited to the same processor
    * features as those used in Portable AOT mode. This is because, the restore run may
    * not be on the same machine as the one that created the snapshot; thus the JIT code
    * must be portable.
    */
   if (J9_ARE_ANY_BITS_SET(javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_CRIU_SUPPORT))
      {
      TR::Compiler->target.cpu = TR::CPU::detectRelocatable(TR::Compiler->omrPortLib);
      jitConfig->targetProcessor = TR::Compiler->target.cpu.getProcessorDescription();
      }

   #if defined(TR_TARGET_S390)
      uintptr_t * tocBase = (uintptr_t *)jitConfig->pseudoTOC;

      // Initialize the helper function table (0 to TR_S390numRuntimeHelpers-2)
      for (int32_t idx=1; idx<TR_S390numRuntimeHelpers; idx++)
         tocBase[idx-1] = (uintptr_t)runtimeHelperValue((TR_RuntimeHelper)idx);
   #endif

   TR::CodeCacheManager::instance()->lateInitialization();

   /* Do not set up the following hooks if we are in testmode */
   if (!(jitConfig->runtimeFlags & J9JIT_TOSS_CODE))
      {
      if (setUpHooks(javaVM, jitConfig, vm))
         return -1;
      }

   // For RI enabled we may want to start as off and only enable when there is
   // compilation pressure
   if (compInfo->getPersistentInfo()->isRuntimeInstrumentationEnabled() &&
       TR::Options::getCmdLineOptions()->getOption(TR_UseRIOnlyForLargeQSZ))
      {
      TR_HWProfiler *hwProfiler = compInfo->getHWProfiler();
      hwProfiler->turnBufferProcessingOffTemporarily();
      }

   UT_MODULE_LOADED(J9_UTINTERFACE_FROM_VM(javaVM));
   Trc_JIT_VMInitStages_Event1(curThread);
   Trc_JIT_portableSharedCache_enabled_or_disabled(curThread, J9_ARE_ANY_BITS_SET(javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_PORTABLE_SHARED_CACHE) ? 1 : 0);
   return 0;
   }


// -----------------------------------------------------------------------------
// GPU
// -----------------------------------------------------------------------------

#ifdef ENABLE_GPU
extern I_32 jitCallGPU(J9VMThread *vmThread, J9Method *method,
                       char * programSource, jobject invokeObject, int deviceId,
                       I_32 gridDimX, I_32 gridDimY, I_32 gridDimZ,
                       I_32 blockDimX, I_32 blockDimY, I_32 blockDimZ,
                       I_32 argCount, void **args);

IDATA  launchGPU(J9VMThread *vmThread, jobject invokeObject,
                 J9Method *method, int deviceId,
                 I_32 gridDimX, I_32 gridDimY, I_32 gridDimZ,
                 I_32 blockDimX, I_32 blockDimY, I_32 blockDimZ,
                 void **args)
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   bool queued = false;
   TR_MethodEvent event;
   I_32 result = 0;
   event._eventType = TR_MethodEvent::InterpreterCounterTripped;
   event._j9method = method;
   event._oldStartPC = 0;
   event._vmThread = vmThread;
   event._classNeedingThunk = 0;
   bool newPlanCreated;
   TR_OptimizationPlan *plan = TR::CompilationController::getCompilationStrategy()->processEvent(&event, &newPlanCreated);
   if (plan)
      {
      plan->setIsExplicitCompilation(true);

      plan->setIsGPUCompilation(true);
      plan->setGPUBlockDimX(blockDimX);
      plan->setGPUParms(args);
      if (!args)
         plan->setIsGPUParallelStream(true);

      static char *GPUCompileCPUCode = feGetEnv("TR_GPUCompileCPUCode");
      if (GPUCompileCPUCode)
         plan->setIsGPUCompileCPUCode(true);

      // If the controller decides to compile this method, trigger the compilation and wait here

         { // scope for details
         TR::IlGeneratorMethodDetails details(method);

         clock_t start = clock();
         if(!compInfo->isCompiled(method) || GPUCompileCPUCode)
            compInfo->compileMethod(vmThread, details, 0, TR_no, NULL, &queued, plan);
         clock_t end = clock();
         printf ("\tJitted Java Kernel %6.3f msec\n", (double)(end-start)*1000/CLOCKS_PER_SEC);

         result = plan->getGPUResult();

         if (result == 0)
            {
            J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
            I_32 argCount = J9_ARG_COUNT_FROM_ROM_METHOD(romMethod) - 1;     // for this (this is not passed to GPU)

            if (GPUCompileCPUCode)
               {
               typedef I_32 (*gpuCodePtr)(J9VMThread *vmThread, J9Method *method,
                                           char * programSource, jobject invokeObject, int deviceId,
                                           I_32 gridDimX, I_32 gridDimY, I_32 gridDimZ,
                                           I_32 blockDimX, I_32 blockDimY, I_32 blockDimZ,
                                           I_32 argCount, void **args);

               printf ("In    launchGPU: %p %p %p %p %d %d %d %d %d %d %d %d %p\n",
                                   vmThread, method, plan->getGPUIR(), invokeObject, deviceId,
                                   gridDimX, gridDimY, gridDimZ,
                                   blockDimX, blockDimY, blockDimZ,
                                   argCount, args);


               gpuCodePtr gpuCode = (gpuCodePtr)TR::CompilationInfo::getJ9MethodExtra(method);

               if ((int64_t)gpuCode != 0x1)
                  result = gpuCode(vmThread, method, plan->getGPUIR(), invokeObject, deviceId,
                                   gridDimX, gridDimY, gridDimZ,
                                   blockDimX, blockDimY, blockDimZ,
                                   argCount, args);
               }
            else
               {
               result = jitCallGPU(vmThread, method, plan->getGPUIR(), invokeObject, deviceId,
                                      gridDimX, gridDimY, gridDimZ,
                                      blockDimX, blockDimY, blockDimZ,
                                      argCount, args);
               }

            printf ("launchGPU result = %d\n", result);
            fflush(NULL);
            }
         }

      if (!queued && newPlanCreated)
         TR_OptimizationPlan::freeOptimizationPlan(plan);
      }

   return result;
   }


static UDATA forEachIterator(J9VMThread *vmThread, J9StackWalkState *walkState)
   {
   // stop iterating if the walk state is null
   if (!walkState)
      {
      return J9_STACKWALK_STOP_ITERATING;
      }

   J9Method *j9method = walkState->method;
   J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(j9method);

   J9JITConfig* jitConfig = vmThread->javaVM->jitConfig;
   TR::CompilationInfo *compInfo = TR::CompilationInfo::get(jitConfig);

   compInfo->acquireCompMonitor(vmThread);

   intptr_t count = TR::CompilationInfo::getJ9MethodExtra(j9method) >> 1;

   if (!(romMethod->modifiers & J9AccNative) && // Never change the extra field of a native method
       !(walkState->jitInfo) &&                 // If the frame has jit metadata, it was already compiled
       !(TR::CompilationInfo::isCompiled(j9method)) &&  //If isCompiled is true it was already compiled
       !(TR::CompilationInfo::getJ9MethodVMExtra(j9method) == J9_JIT_QUEUED_FOR_COMPILATION) && //No change needed if already queued
       (count > 0))                             // No change needed if count is already 0
      {
      TR::CompilationInfo::setInitialInvocationCountUnsynchronized(j9method,0); //set count to 0
      }

   compInfo->releaseCompMonitor(vmThread);

   return J9_STACKWALK_STOP_ITERATING;
   }
#endif


void promoteGPUCompile(J9VMThread *vmThread)
   {
#ifdef ENABLE_GPU
   if (TR::Options::getCmdLineOptions()->getEnableGPU(TR_EnableGPU))
      {
      J9StackWalkState walkState;

      walkState.walkThread = vmThread;
      walkState.skipCount = 3; //method to compile is 3 up from the forEach

      walkState.flags = J9_STACKWALK_VISIBLE_ONLY |
                        J9_STACKWALK_INCLUDE_NATIVES |
                        J9_STACKWALK_ITERATE_FRAMES;
      walkState.frameWalkFunction = forEachIterator;

      vmThread->javaVM->walkStackFrames(vmThread, &walkState);
      }
#endif
   }

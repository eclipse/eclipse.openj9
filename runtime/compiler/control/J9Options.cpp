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

#include "control/J9Options.hpp"

#include <algorithm>
#if defined(J9VM_OPT_JITSERVER)
#include <random>
#endif /* defined(J9VM_OPT_JITSERVER) */
#include <ctype.h>
#include <stdint.h>
#include "jitprotos.h"
#include "j2sever.h"
#include "j9.h"
#include "j9cfg.h"
#include "j9modron.h"
#include "jvminit.h"
#if defined(J9VM_OPT_JITSERVER)
#include "j9vmnls.h"
#include "omrformatconsts.h"
#endif /* defined(J9VM_OPT_JITSERVER) */
#include "codegen/CodeGenerator.hpp"
#include "compile/Compilation.hpp"
#include "control/Recompilation.hpp"
#include "control/RecompilationInfo.hpp"
#include "env/CompilerEnv.hpp"
#include "env/IO.hpp"
#include "env/VMJ9.h"
#include "env/VerboseLog.hpp"
#include "env/jittypes.h"
#include "infra/SimpleRegex.hpp"
#include "control/CompilationRuntime.hpp"
#include "control/CompilationThread.hpp"
#include "runtime/IProfiler.hpp"
#if defined(J9VM_OPT_JITSERVER)
#include "env/j9methodServer.hpp"
#include "control/JITServerCompilationThread.hpp"
#endif /* defined(J9VM_OPT_JITSERVER) */

#if defined(J9VM_OPT_SHARED_CLASSES)
#include "j9jitnls.h"
#endif

#define SET_OPTION_BIT(x)   TR::Options::setBit,   offsetof(OMR::Options,_options[(x)&TR_OWM]), ((x)&~TR_OWM)

// For use with TPROF only, disable JVMPI hooks even if -Xrun is specified.
// The only hook that is required is J9HOOK_COMPILED_METHOD_LOAD.
//
bool enableCompiledMethodLoadHookOnly = false;

// -----------------------------------------------------------------------------
// Static data initialization
// -----------------------------------------------------------------------------

bool J9::Options::_doNotProcessEnvVars = false; // set through XX options in Java
bool J9::Options::_reportByteCodeInfoAtCatchBlock = false;
int32_t J9::Options::_samplingFrequencyInIdleMode = 1000; // ms
#if defined(J9VM_OPT_JITSERVER)
int32_t J9::Options::_statisticsFrequency = 0; // ms
uint32_t J9::Options::_compilationSequenceNumber = 0;
static const size_t JITSERVER_LOG_FILENAME_MAX_SIZE = 1025;
#endif /* defined(J9VM_OPT_JITSERVER) */
int32_t J9::Options::_samplingFrequencyInDeepIdleMode = 100000; // ms
int32_t J9::Options::_resetCountThreshold = 0; // Disable the feature
int32_t J9::Options::_scorchingSampleThreshold = 240;
int32_t J9::Options::_conservativeScorchingSampleThreshold = 80; // used when many CPUs (> _upperBoundNumProc)
int32_t J9::Options::_upperBoundNumProcForScaling = 64; // used for scaling _scorchingSampleThreshold based on numProc
int32_t J9::Options::_lowerBoundNumProcForScaling = 8;  // used for scaling _scorchingSampleThreshold based on numProd]c
int32_t J9::Options::_veryHotSampleThreshold = 480;
int32_t J9::Options::_relaxedCompilationLimitsSampleThreshold = 120; // normally should be lower than the scorchingSampleThreshold
int32_t J9::Options::_sampleThresholdVariationAllowance = 30;

int32_t J9::Options::_maxCheckcastProfiledClassTests = 3;
int32_t J9::Options::_maxOnsiteCacheSlotForInstanceOf = 0; // Setting this value to zero will disable onsite cache in instanceof.
int32_t J9::Options::_cpuEntitlementForConservativeScorching = 801; // 801 means more than 800%, i.e. 8 cpus
                                                                    // A very large number disables the feature
int32_t J9::Options::_sampleHeartbeatInterval = 10;
int32_t J9::Options::_sampleDontSwitchToProfilingThreshold = 3000; // default=1% use large value to disable// To be tuned
int32_t J9::Options::_stackSize = 1024;
int32_t J9::Options::_profilerStackSize = 128;

int32_t J9::Options::_smallMethodBytecodeSizeThreshold = 0;
int32_t J9::Options::_smallMethodBytecodeSizeThresholdForCold = -1; // -1 means not set (or disabled)

int32_t J9::Options::_countForMethodsCompiledDuringStartup = 10;

int32_t J9::Options::_countForLoopyBootstrapMethods = -1; // -1 means feature disabled
int32_t J9::Options::_countForLooplessBootstrapMethods = -1; // -1 means feature disabled

TR::SimpleRegex *J9::Options::_jniAccelerator = NULL;

int32_t J9::Options::_classLoadingPhaseInterval = 500; // ms
int32_t J9::Options::_experimentalClassLoadPhaseInterval = 40;
int32_t J9::Options::_classLoadingPhaseThreshold = 155; // classes per second
int32_t J9::Options::_classLoadingPhaseVariance = 70; // percentage  0..99
int32_t J9::Options::_classLoadingRateAverage = 800; // classes per second
int32_t J9::Options::_secondaryClassLoadingPhaseThreshold = 10000;
int32_t J9::Options::_numClassLoadPhaseQuiesceIntervals = 1;
int32_t J9::Options::_userClassLoadingPhaseThreshold = 5;
bool J9::Options::_userClassLoadingPhase = false;

int32_t J9::Options::_bigAppSampleThresholdAdjust = 3; //amount to shift the hot and scorching threshold
int32_t J9::Options::_availableCPUPercentage = 100;
int32_t J9::Options::_cpuCompTimeExpensiveThreshold = 4000;
uintptr_t J9::Options::_compThreadAffinityMask = 0;
#if defined(J9VM_OPT_JITSERVER)
int64_t J9::Options::_oldAge = 1000*60*1000; // 1000 minutes
int64_t J9::Options::_oldAgeUnderLowMemory = 1000*60*5; // 5 minute
int64_t J9::Options::_timeBetweenPurges = 1000*60*1; // 1 minute
bool J9::Options::_shareROMClasses = false;
int32_t J9::Options::_sharedROMClassCacheNumPartitions = 16;
int32_t J9::Options::_highActiveThreadThreshold = -1;
int32_t J9::Options::_veryHighActiveThreadThreshold = -1;
#endif /* defined(J9VM_OPT_JITSERVER) */

int32_t J9::Options::_interpreterSamplingThreshold = 300;
int32_t J9::Options::_interpreterSamplingDivisor = TR_DEFAULT_INTERPRETER_SAMPLING_DIVISOR;
int32_t J9::Options::_interpreterSamplingThresholdInStartupMode = TR_DEFAULT_INITIAL_BCOUNT; // 3000
int32_t J9::Options::_interpreterSamplingThresholdInJSR292 = TR_DEFAULT_INITIAL_COUNT - 2; // Run stuff twice before getting too excited about interpreter ticks
int32_t J9::Options::_activeThreadsThreshold = 0; // -1 means 'determine dynamically', 0 means feature disabled
int32_t J9::Options::_samplingThreadExpirationTime = -1;
int32_t J9::Options::_compilationExpirationTime = -1;

int32_t J9::Options::_minSamplingPeriod = 10; // ms
int32_t J9::Options::_compilationBudget = 0;  // ms; 0 means disabled

int32_t J9::Options::_catchSamplingSizeThreshold = -1; // measured in nodes; -1 means not initialized
int32_t J9::Options::_compilationThreadPriorityCode = 4; // these codes are converted into
                                                         // priorities in startCompilationThread
int32_t J9::Options::_disableIProfilerClassUnloadThreshold = 20000;// The usefulness of IProfiling is questionable at this point
int32_t J9::Options::_iprofilerReactivateThreshold=10;
int32_t J9::Options::_iprofilerIntToTotalSampleRatio=2;
int32_t J9::Options::_iprofilerSamplesBeforeTurningOff = 1000000; // samples
int32_t J9::Options::_iprofilerNumOutstandingBuffers = 10;
int32_t J9::Options::_iprofilerBufferMaxPercentageToDiscard = 0;
int32_t J9::Options::_iProfilerBufferInterarrivalTimeToExitDeepIdle = 5000; // 5 seconds
int32_t J9::Options::_iprofilerBufferSize = 1024;
#ifdef TR_HOST_64BIT
int32_t J9::Options::_iProfilerMemoryConsumptionLimit=32*1024*1024;
#else
int32_t J9::Options::_iProfilerMemoryConsumptionLimit=18*1024*1024;
#endif
int32_t J9::Options::_IprofilerOffSubtractionFactor = 500;
int32_t J9::Options::_IprofilerOffDivisionFactor = 16;



int32_t J9::Options::_maxIprofilingCount = TR_DEFAULT_INITIAL_COUNT; // 3000
int32_t J9::Options::_maxIprofilingCountInStartupMode = TR_QUICKSTART_INITIAL_COUNT; // 1000
int32_t J9::Options::_iprofilerFailRateThreshold = 70; // percent 1-100
int32_t J9::Options::_iprofilerFailHistorySize = 10; // percent 1-100

int32_t J9::Options::_compYieldStatsThreshold = 1000; // usec
int32_t J9::Options::_compYieldStatsHeartbeatPeriod = 0; // ms
int32_t J9::Options::_numberOfUserClassesLoaded = 0;
int32_t J9::Options::_compPriorityQSZThreshold = 200;
int32_t J9::Options::_numQueuedInvReqToDowngradeOptLevel = 20; // If more than 20 inv req are queued we compiled them at cold
int32_t J9::Options::_qszThresholdToDowngradeOptLevel = -1; // not yet set
int32_t J9::Options::_qsziThresholdToDowngradeDuringCLP = 0; // -1 or 0 disables the feature and reverts to old behavior
int32_t J9::Options::_qszThresholdToDowngradeOptLevelDuringStartup = 100000; // a large number disables the feature
int32_t J9::Options::_cpuUtilThresholdForStarvation = 25; // 25%

// If too many GCR are queued we stop counting.
// Use a large value to disable the feature. 400 is a good default
// Don't use a value smaller than GCR_HYSTERESIS==100
int32_t J9::Options::_GCRQueuedThresholdForCounting = 1000000; // 400;

int32_t J9::Options::_minimumSuperclassArraySize = 5;
int32_t J9::Options::_TLHPrefetchSize = 0;
int32_t J9::Options::_TLHPrefetchLineSize = 0;
int32_t J9::Options::_TLHPrefetchLineCount = 0;
int32_t J9::Options::_TLHPrefetchStaggeredLineCount = 0;
int32_t J9::Options::_TLHPrefetchBoundaryLineCount = 0;
int32_t J9::Options::_TLHPrefetchTLHEndLineCount = 0;

int32_t J9::Options::_numFirstTimeCompilationsToExitIdleMode = 25; // Use a large number to disable the feature
int32_t J9::Options::_waitTimeToEnterIdleMode = 5000; // ms
int32_t J9::Options::_waitTimeToEnterDeepIdleMode = 50000; // ms
int32_t J9::Options::_waitTimeToExitStartupMode = DEFAULT_WAIT_TIME_TO_EXIT_STARTUP_MODE; // ms
int32_t J9::Options::_waitTimeToGCR = 10000; // ms
int32_t J9::Options::_waitTimeToStartIProfiler = 1000; // ms
int32_t J9::Options::_compilationDelayTime = 0; // sec; 0 means disabled

int32_t J9::Options::_invocationThresholdToTriggerLowPriComp = 250;

int32_t J9::Options::_aotMethodThreshold = 200;
int32_t J9::Options::_aotMethodCompilesThreshold = 200;
int32_t J9::Options::_aotWarmSCCThreshold = 200;

int32_t J9::Options::_largeTranslationTime = -1; // usec
int32_t J9::Options::_weightOfAOTLoad = 1; // must be between 0 and 256
int32_t J9::Options::_weightOfJSR292 = 12; // must be between 0 and 256

TR_YesNoMaybe J9::Options::_hwProfilerEnabled = TR_maybe;
int32_t J9::Options::_hwprofilerNumOutstandingBuffers = 256; // 1MB / 4KB buffers

// These numbers are cast into floats divided by 10000
uint32_t J9::Options::_hwprofilerWarmOptLevelThreshold      = 1;       // 0.0001
uint32_t J9::Options::_hwprofilerReducedWarmOptLevelThreshold=0;       // 0 ==> upgrade methods with just 1 tick in any given interval
uint32_t J9::Options::_hwprofilerAOTWarmOptLevelThreshold   = 10;      // 0.001
uint32_t J9::Options::_hwprofilerHotOptLevelThreshold       = 100;     // 0.01
uint32_t J9::Options::_hwprofilerScorchingOptLevelThreshold = 1250;    // 0.125

uint32_t J9::Options::_hwprofilerLastOptLevel               = warm; // warm
uint32_t J9::Options::_hwprofilerRecompilationInterval      = 10000;
uint32_t J9::Options::_hwprofilerRIBufferThreshold          = 50; // process buffer when it is at least 50% full
uint32_t J9::Options::_hwprofilerRIBufferPoolSize           = 1 * 1024 * 1024; // 1 MB
int32_t J9::Options::_hwProfilerRIBufferProcessingFrequency= 0; // process buffer every time
int32_t J9::Options::_hwProfilerRecompFrequencyThreshold   = 5000; // less than 1 in 5000 will turn RI off

int32_t J9::Options::_hwProfilerRecompDecisionWindow       = 5000; // Should be at least as big as _hwProfilerRecompFrequencyThreshold
int32_t J9::Options::_numDowngradesToTurnRION              = 250;
int32_t J9::Options::_qszThresholdToTurnRION               = 100;
int32_t J9::Options::_qszMaxThresholdToRIDowngrade         = 250;
int32_t J9::Options::_qszMinThresholdToRIDowngrade         = 50; // should be smaller than _qszMaxThresholdToRIDowngrade
uint32_t J9::Options::_hwprofilerPRISamplingRate            = 500000;

int32_t J9::Options::_hwProfilerBufferMaxPercentageToDiscard = 5;
uint32_t J9::Options::_hwProfilerExpirationTime             = 0; // ms;  0 means disabled
uint32_t J9::Options::_hwprofilerZRIBufferSize              = 4 * 1024; // 4 kb
uint32_t J9::Options::_hwprofilerZRIMode                    = 0; // cycle based profiling
uint32_t J9::Options::_hwprofilerZRIRGS                     = 0; // only collect instruction records
uint32_t J9::Options::_hwprofilerZRISF                      = 10000000;

int32_t J9::Options::_LoopyMethodSubtractionFactor = 500;
int32_t J9::Options::_LoopyMethodDivisionFactor = 16;

int32_t J9::Options::_localCSEFrequencyThreshold = 1000;
int32_t J9::Options::_profileAllTheTime = 0;

int32_t J9::Options::_seriousCompFailureThreshold = 10; // above this threshold we generate a trace point in the Snap file

bool J9::Options::_useCPUsToDetermineMaxNumberOfCompThreadsToActivate = false;
int32_t J9::Options::_numCodeCachesToCreateAtStartup = 0; // 0 means no change from default which is 1

int32_t J9::Options::_dataCacheQuantumSize = 64;
int32_t J9::Options::_dataCacheMinQuanta = 2;

int32_t J9::Options::_updateFreeMemoryMinPeriod = 500;  // 500 ms


size_t J9::Options::_scratchSpaceLimitKBWhenLowVirtualMemory = 64*1024; // 64MB; currently, only used on 32 bit Windows

int32_t J9::Options::_scratchSpaceFactorWhenJSR292Workload = JSR292_SCRATCH_SPACE_FACTOR;
int32_t J9::Options::_lowVirtualMemoryMBThreshold = 300; // Used on 32 bit Windows, Linux, 31 bit z/OS, Linux
int32_t J9::Options::_safeReservePhysicalMemoryValue = 32 << 20;  // 32 MB

int32_t J9::Options::_numDLTBufferMatchesToEagerlyIssueCompReq = 8; //a value of 1 or less disables the DLT tracking mechanism
int32_t J9::Options::_dltPostponeThreshold = 2;

int32_t J9::Options::_expensiveCompWeight = TR::CompilationInfo::JSR292_WEIGHT;
int32_t J9::Options::_jProfilingEnablementSampleThreshold = 10000;

bool J9::Options::_aggressiveLockReservation = false;

//************************************************************************
//
// Options handling - the following code implements the VM-specific
// jit command-line options.
//
// Options processing is table-driven, the table for VM-specific options
// here (see Options.hpp for a description of the table entries).
//
//************************************************************************

// Helper routines to parse and format -Xlp:codecache Options
enum TR_XlpCodeCacheOptions
   {
   XLPCC_PARSING_FIRST_OPTION,
   XLPCC_PARSING_OPTION,
   XLPCC_PARSING_COMMA,
   XLPCC_PARSING_ERROR
   };

// Returns large page flag type string for error handling.
char *
getLargePageTypeString(UDATA pageFlags)
   {
   if (0 != (J9PORT_VMEM_PAGE_FLAG_PAGEABLE & pageFlags))
      return "pageable";
   else if (0 != (J9PORT_VMEM_PAGE_FLAG_FIXED & pageFlags))
      return "nonpageable";
   else
      return "not used";
   }

// Formats size to be in terms of X bytes to XX(K/M/G) for printing
void
qualifiedSize(UDATA *byteSize, char **qualifier)
{
   UDATA size;

   size = *byteSize;
   *qualifier = "";
   if(!(size % 1024)) {
      size /= 1024;
      *qualifier = "K";
      if(size && !(size % 1024)) {
         size /= 1024;
         *qualifier = "M";
         if(size && !(size % 1024)) {
            size /= 1024;
            *qualifier = "G";
         }
      }
   }
   *byteSize = size;
}


bool
J9::Options::useCompressedPointers()
   {
   return TR::Compiler->om.compressObjectReferences();
   }


#ifdef DEBUG
#define BUILD_TYPE "(debug)"
#else
#define BUILD_TYPE ""
#endif

char *
J9::Options::versionOption(char * option, void * base, TR::OptionTable *entry)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   j9tty_printf(PORTLIB, "JIT: using build \"%s %s\" %s\n",  __DATE__, __TIME__, BUILD_TYPE);
   j9tty_printf(PORTLIB, "JIT level: %s\n", TR_BUILD_NAME);
   return option;
   }
#undef BUILD_TYPE


char *
J9::Options::limitOption(char * option, void * base, TR::OptionTable *entry)
   {
   if (!J9::Options::getDebug() && !J9::Options::createDebug())
      return 0;

   if (J9::Options::getJITCmdLineOptions() == NULL)
      {
      // if JIT options are NULL, means we're processing AOT options now
      return J9::Options::getDebug()->limitOption(option, base, entry, TR::Options::getAOTCmdLineOptions(), false);
      }
   else
      {
      // otherwise, we're processing JIT options
      return J9::Options::getDebug()->limitOption(option, base, entry, TR::Options::getJITCmdLineOptions(), false);
      }
   }


char *
J9::Options::limitfileOption(char * option, void * base, TR::OptionTable *entry)
   {
   if (!J9::Options::getDebug() && !J9::Options::createDebug())
      return 0;

   J9JITConfig * jitConfig = (J9JITConfig*)base;
   TR_PseudoRandomNumbersListElement **pseudoRandomNumbersListPtr = NULL;
   if (jitConfig != 0)
      {
      TR::CompilationInfo * compInfo = TR::CompilationInfo::get(jitConfig);
      pseudoRandomNumbersListPtr = compInfo->getPersistentInfo()->getPseudoRandomNumbersListPtr();
      }

   if (J9::Options::getJITCmdLineOptions() == NULL)
      {
      // if JIT options are NULL, means we're processing AOT options now
      return J9::Options::getDebug()->limitfileOption(option, base, entry, TR::Options::getAOTCmdLineOptions(), false, pseudoRandomNumbersListPtr);
      }
   else
      {
      // otherwise, we're processing JIT options
      return J9::Options::getDebug()->limitfileOption(option, base, entry, TR::Options::getJITCmdLineOptions(), false, pseudoRandomNumbersListPtr);
      }
   }

char *
J9::Options::inlinefileOption(char * option, void * base, TR::OptionTable *entry)
   {
   if (!J9::Options::getDebug() && !J9::Options::createDebug())
      return 0;

   if (J9::Options::getJITCmdLineOptions() == NULL)
      {
      // if JIT options are NULL, means we're processing AOT options now
      return J9::Options::getDebug()->inlinefileOption(option, base, entry, TR::Options::getAOTCmdLineOptions());
      }
   else
      {
      // otherwise, we're processing JIT options
      return J9::Options::getDebug()->inlinefileOption(option, base, entry, TR::Options::getJITCmdLineOptions());
      }
   }


struct vmX
   {
   uint32_t _xstate;
   const char *_xname;
   int32_t _xsize;
   };


static const struct vmX vmSharedStateArray[] =
   {
      {J9VMSTATE_SHAREDCLASS_FIND, "J9VMSTATE_SHAREDCLASS_FIND", 0},                      //9  0x80001
      {J9VMSTATE_SHAREDCLASS_STORE, "J9VMSTATE_SHAREDCLASS_STORE", 0},                    //10 0x80002
      {J9VMSTATE_SHAREDCLASS_MARKSTALE, "J9VMSTATE_SHAREDCLASS_MARKSTALE", 0},            //11 0x80003
      {J9VMSTATE_SHAREDAOT_FIND, "J9VMSTATE_SHAREDAOT_FIND", 0},                          //12 0x80004
      {J9VMSTATE_SHAREDAOT_STORE, "J9VMSTATE_SHAREDAOT_STORE", 0},                        //13 0x80005
      {J9VMSTATE_SHAREDDATA_FIND, "J9VMSTATE_SHAREDDATA_FIND", 0},                        //14 0x80006
      {J9VMSTATE_SHAREDDATA_STORE, "J9VMSTATE_SHAREDDATA_STORE", 0},                      //15 0x80007
      {J9VMSTATE_SHAREDCHARARRAY_FIND, "J9VMSTATE_SHAREDCHARARRAY_FIND", 0},              //16 0x80008
      {J9VMSTATE_SHAREDCHARARRAY_STORE, "J9VMSTATE_SHAREDCHARARRAY_STORE", 0},            //17 0x80009
      {J9VMSTATE_ATTACHEDDATA_STORE, "J9VMSTATE_ATTACHEDDATA_STORE", 0},                  //18 0x8000a
      {J9VMSTATE_ATTACHEDDATA_FIND, "J9VMSTATE_ATTACHEDDATA_FIND", 0},                    //19 0x8000b
      {J9VMSTATE_ATTACHEDDATA_UPDATE, "J9VMSTATE_ATTACHEDDATA_UPDATE", 0},                //20 0x8000c
   };


static const struct vmX vmJniStateArray[] =
   {
      {J9VMSTATE_JNI, "J9VMSTATE_JNI", 0},                                                //4  0x40000
      {J9VMSTATE_JNI_FROM_JIT, "J9VMSTATE_JNI_FROM_JIT", 0},                              //   0x40001
   };


static const struct vmX vmStateArray[] =
   {
      {0xdead, "unknown", 0},                                                             //0
      {J9VMSTATE_INTERPRETER, "J9VMSTATE_INTERPRETER", 0},                                //1  0x10000
      {J9VMSTATE_GC, "J9VMSTATE_GC", 0},                                                  //2  0x20000
      {J9VMSTATE_GROW_STACK, "J9VMSTATE_GROW_STACK", 0},                                  //3  0x30000
      {J9VMSTATE_JNI, "special", 2},                                                      //4  0x40000
      {J9VMSTATE_JIT, "J9VMSTATE_JIT", 0},                                                //5  0x50000
      {J9VMSTATE_BCVERIFY, "J9VMSTATE_BCVERIFY", 0},                                      //6  0x60000
      {J9VMSTATE_RTVERIFY, "J9VMSTATE_RTVERIFY", 0},                                      //7  0x70000
      {J9VMSTATE_SHAREDCLASS_FIND, "special", 12},                                        //8  0x80000
      {J9VMSTATE_SNW_STACK_VALIDATE, "J9VMSTATE_SNW_STACK_VALIDATE", 0},                  //9  0x110000
      {J9VMSTATE_GP, "J9VMSTATE_GP", 0}                                                   //10 0xFFFF0000
   };


namespace J9
{

char *
Options::gcOnResolveOption(char * option, void * base, TR::OptionTable *entry)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;

   jitConfig->gcOnResolveThreshold = 0;
   jitConfig->runtimeFlags |= J9JIT_SCAVENGE_ON_RESOLVE;
   if (* option == '=')
      {
      for (option++; * option >= '0' && * option <= '9'; option++)
         jitConfig->gcOnResolveThreshold = jitConfig->gcOnResolveThreshold *10 + * option - '0';
      }
   entry->msgInfo = jitConfig->gcOnResolveThreshold;
   return option;
   }


char *
Options::vmStateOption(char * option, void * base, TR::OptionTable *entry)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   char *p = option;
   int32_t state = strtol(option, &p, 16);
   if (state > 0)
      {
      uint32_t index = (state >> 16) & 0xFF;
      bool invalidState = false;
      if (!isValidVmStateIndex(index))
         invalidState = true;

      if (!invalidState)
         {
         uint32_t origState = vmStateArray[index]._xstate;
         switch (index)
            {
            case ((J9VMSTATE_JNI>>16) & 0xF):
               invalidState = true;
               if ((state & 0xFFFF0) == origState)
                  {
                  int32_t lowState = state & 0xF;
                  if (lowState >= 0 && lowState < vmStateArray[index]._xsize)
                     {
                     invalidState = false;
                     j9tty_printf(PORTLIB, "vmState [0x%x]: {%s}\n", state, vmJniStateArray[lowState]._xname);
                     }
                  }
               break;
            case ((J9VMSTATE_SHAREDCLASS_FIND>>16) & 0xF):
               invalidState = true;
               if ((state & 0xFFFF0) == (origState & 0xFFFF0))
                  {
                  int32_t lowState = state & 0xF;
                  if (lowState >= 0x1 && lowState <= vmStateArray[index]._xsize)
                     {
                     invalidState = false;
                     j9tty_printf(PORTLIB, "vmState [0x%x]: {%s}\n", state, vmSharedStateArray[--lowState]._xname);
                     }
                  }
               break;
            case ((J9VMSTATE_JIT >> 16) & 0xF):
               {
               if ((state & 0xFF00) == 0) // ILGeneratorPhase
                  {
                  j9tty_printf(PORTLIB, "vmState [0x%x]: {%s} {ILGeneration}\n", state, vmStateArray[index]._xname);
                  }
               else if ((state & J9VMSTATE_JIT_OPTIMIZER) == J9VMSTATE_JIT_OPTIMIZER)
                  {
                  OMR::Optimizations opts = static_cast<OMR::Optimizations>((state & 0xFF00) >> 8);
                  if (opts < OMR::numOpts)
                     {
                      j9tty_printf(PORTLIB, "vmState [0x%x]: {%s} {%s}\n", state, vmStateArray[index]._xname, OMR::Optimizer::getOptimizationName(opts));
                     }
                  else
                     j9tty_printf(PORTLIB, "vmState [0x%x]: {%s} {Illegal optimization number}\n", state, vmStateArray[index]._xname);
                  }
               else if ((state & J9VMSTATE_JIT_CODEGEN) == J9VMSTATE_JIT_CODEGEN)
                  {
                  TR::CodeGenPhase::PhaseValue phase = static_cast<TR::CodeGenPhase::PhaseValue>(state & 0xFF);
                  if ( phase < TR::CodeGenPhase::getNumPhases())
                     j9tty_printf(PORTLIB, "vmState [0x%x]: {%s} {%s}\n", state, vmStateArray[index]._xname, TR::CodeGenPhase::getName(phase));
                  else
                     j9tty_printf(PORTLIB, "vmState [0x%x]: {%s} {Illegal codegen phase number}\n", state, vmStateArray[index]._xname);
                  }
               else
                  invalidState = true;
               }
               break;
            default:
               if (state != origState)
                  invalidState = true;
               else
                  j9tty_printf(PORTLIB, "vmState [0x%x]: {%s}\n", state, vmStateArray[index]._xname);
               break;
            }
         }

      if (invalidState)
         j9tty_printf(PORTLIB, "vmState [0x%x]: not a valid vmState\n", state);
      }
   else
      {
      // a bad vmState, eat it up atleast
      //
      j9tty_printf(PORTLIB, "vmState [0x%x]: not a valid vmState\n", state);
      }
   for (; *p; p++);

   return p;
   }


char *
Options::loadLimitOption(char * option, void * base, TR::OptionTable *entry)
   {
   if (!TR::Options::getDebug() && !TR::Options::createDebug())
      return 0;
   if (TR::Options::getJITCmdLineOptions() == NULL)
      {
      // if JIT options are NULL, means we're processing AOT options now
      return TR::Options::getDebug()->limitOption(option, base, entry, TR::Options::getAOTCmdLineOptions(), true);
      }
   else
      {
      // otherwise, we're processing JIT options
      J9JITConfig * jitConfig = (J9JITConfig*)base;
      PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
      // otherwise, we're processing JIT options
      j9tty_printf(PORTLIB, "<JIT: loadLimit option should be specified on -Xaot --> '%s'>\n", option);
      return option;
      //return J9::Options::getDebug()->limitOption(option, base, entry, getJITCmdLineOptions(), true);
      }
   }


char *
Options::loadLimitfileOption(char * option, void * base, TR::OptionTable *entry)
   {
   if (!TR::Options::getDebug() && !TR::Options::createDebug())
      return 0;

   J9JITConfig * jitConfig = (J9JITConfig*)base;
   TR_PseudoRandomNumbersListElement **pseudoRandomNumbersListPtr = NULL;
   if (jitConfig != 0)
      {
      TR::CompilationInfo * compInfo = TR::CompilationInfo::get(jitConfig);
      pseudoRandomNumbersListPtr = compInfo->getPersistentInfo()->getPseudoRandomNumbersListPtr();
      }

   if (TR::Options::getJITCmdLineOptions() == NULL)
      {
      // if JIT options are NULL, means we're processing AOT options now
      return TR::Options::getDebug()->limitfileOption(option, base, entry, TR::Options::getAOTCmdLineOptions(), true /* new param */, pseudoRandomNumbersListPtr);
      }
   else
      {
      J9JITConfig * jitConfig = (J9JITConfig*)base;
      PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
      // otherwise, we're processing JIT options
      j9tty_printf(PORTLIB, "<JIT: loadLimitfile option should be specified on -Xaot --> '%s'>\n", option);
      return option;
      }
   }


char *
Options::tprofOption(char * option, void * base, TR::OptionTable *entry)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   enableCompiledMethodLoadHookOnly = true;
   return option;
   }

char *
Options::setJitConfigRuntimeFlag(char *option, void *base, TR::OptionTable *entry)
   {
   J9JITConfig *jitConfig = (J9JITConfig*)_feBase;
   jitConfig->runtimeFlags |= entry->parm2;
   return option;
   }

char *
Options::resetJitConfigRuntimeFlag(char *option, void *base, TR::OptionTable *entry)
   {
   J9JITConfig *jitConfig = (J9JITConfig*)_feBase;
   jitConfig->runtimeFlags &= ~(entry->parm2);
   return option;
   }

char *
Options::setJitConfigNumericValue(char *option, void *base, TR::OptionTable *entry)
   {
   char *jitConfig = (char*)_feBase;
   // All numeric fields in jitConfig are declared as UDATA
   *((intptr_t*)(jitConfig + entry->parm1)) = (intptr_t)TR::Options::getNumericValue(option);
   return option;
   }

}

#define SET_JITCONFIG_RUNTIME_FLAG(x)   J9::Options::setJitConfigRuntimeFlag,   0, (x), "F", NOT_IN_SUBSET
#define RESET_JITCONFIG_RUNTIME_FLAG(x)   J9::Options::resetJitConfigRuntimeFlag,   0, (x), "F", NOT_IN_SUBSET

// DMDM: hack
TR::OptionTable OMR::Options::_feOptions[] = {

   {"activeThreadsThresholdForInterpreterSampling=", "M<nnn>\tSampling does not affect invocation count beyond this threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_activeThreadsThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"aotMethodCompilesThreshold=", "R<nnn>\tIf this many AOT methods are compiled before exceeding aotMethodThreshold, don't stop AOT compiling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_aotMethodCompilesThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"aotMethodThreshold=", "R<nnn>\tNumber of methods found in shared cache after which we stop AOTing",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_aotMethodThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"aotWarmSCCThreshold=", "R<nnn>\tNumber of methods found in shared cache at startup to declare SCC as warm",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_aotWarmSCCThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"availableCPUPercentage=", "M<nnn>\tUse it when java process has a fraction of a CPU. Number 1..99 ",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_availableCPUPercentage, 0, "F%d", NOT_IN_SUBSET},
   {"bcLimit=",           "C<nnn>\tbytecode size limit",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, bcSizeLimit), 0, "P%d"},
   {"bcountForBootstrapMethods=", "M<nnn>\tcount for loopy methods belonging to bootstrap classes. "
                                   "Used in no AOT cases",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_countForLoopyBootstrapMethods, 250, "F%d", NOT_IN_SUBSET },
   {"bigAppSampleThresholdAdjust=", "O\tadjust the hot and scorching threshold for certain 'big' apps",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_bigAppSampleThresholdAdjust, 0, "F%d", NOT_IN_SUBSET},
   {"catchSamplingSizeThreshold=", "R<nnn>\tThe sample counter will not be decremented in a catch block "
                                   "if the number of nodes in the compiled method exceeds this threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_catchSamplingSizeThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"classLoadPhaseInterval=", "O<nnn>\tnumber of sampling ticks before we run "
                               "again the code for a class loading phase detection",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_classLoadingPhaseInterval, 0, "P%d", NOT_IN_SUBSET},
   {"classLoadPhaseQuiesceIntervals=",  "O<nnn>\tnumber of intervals we remain in classLoadPhase after it ended",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numClassLoadPhaseQuiesceIntervals, 0, "F%d", NOT_IN_SUBSET},
   {"classLoadPhaseThreshold=", "O<nnn>\tnumber of classes loaded per sampling tick that "
                                "needs to be attained to enter the class loading phase. "
                                "Specify a very large value to disable this optimization",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_classLoadingPhaseThreshold, 0, "P%d", NOT_IN_SUBSET},
   {"classLoadPhaseVariance=", "O<nnn>\tHow much the classLoadPhaseThreshold can deviate from "
                               "its average value (as a percentage). Specify an integer 0-99",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_classLoadingPhaseVariance, 0, "F%d", NOT_IN_SUBSET},
   {"classLoadRateAverage=",  "O<nnn>\tnumber of classes loaded per second on an average machine",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_classLoadingRateAverage, 0, "F%d", NOT_IN_SUBSET},
   {"clinit",             "D\tforce compilation of <clinit> methods", SET_JITCONFIG_RUNTIME_FLAG(J9JIT_COMPILE_CLINIT) },
   {"code=",              "C<nnn>\tcode cache size, in KB",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, codeCacheKB), 0, "F%d (KB)"},
   {"codepad=",              "C<nnn>\ttotal code cache pad size, in KB",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, codeCachePadKB), 0, "F%d (KB)"},
   {"codetotal=",              "C<nnn>\ttotal code memory limit, in KB",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, codeCacheTotalKB), 0, "F%d (KB)"},
   {"compilationBudget=",      "O<nnn>\tnumber of usec. Used to better interleave compilation"
                               "with computation. Use 80000 as a starting point",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compilationBudget, 0, "P%d", NOT_IN_SUBSET},
   {"compilationDelayTime=", "M<nnn>\tnumber of seconds after which we allow compiling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compilationDelayTime, 0, "F%d", NOT_IN_SUBSET },
   {"compilationExpirationTime=", "R<nnn>\tnumber of seconds after which point we will stop compiling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compilationExpirationTime, 0, "F%d", NOT_IN_SUBSET},
   {"compilationPriorityQSZThreshold=", "M<nnn>\tCompilation queue size threshold when priority of post-profiling"
                               "compilation requests is increased",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compPriorityQSZThreshold , 0, "F%d", NOT_IN_SUBSET},
   {"compilationThreadAffinityMask=", "M<nnn>\taffinity mask for compilation threads. Use hexa without 0x",
        TR::Options::setStaticHexadecimal, (intptr_t)&TR::Options::_compThreadAffinityMask, 0, "F%d", NOT_IN_SUBSET},
   {"compilationYieldStatsHeartbeatPeriod=", "M<nnn>\tperiodically print stats about compilation yield points "
                                       "Period is in ms. Default is 0 which means don't do it. "
                                       "Values between 1 and 99 ms will be upgraded to 100 ms.",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compYieldStatsHeartbeatPeriod, 0, "F%d", NOT_IN_SUBSET},
   {"compilationYieldStatsThreshold=", "M<nnn>\tprint stats about compilation yield points if the "
                                       "threshold is exceeded. Default 1000 usec. ",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compYieldStatsThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"compThreadPriority=",    "M<nnn>\tThe priority of the compilation thread. "
                              "Use an integer between 0 and 4. Default is 4 (highest priority)",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_compilationThreadPriorityCode, 0, "F%d", NOT_IN_SUBSET},
   {"conservativeScorchingSampleThreshold=", "R<nnn>\tLower bound for scorchingSamplingThreshold when scaling based on numProc",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_conservativeScorchingSampleThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"countForBootstrapMethods=", "M<nnn>\tcount for loopless methods belonging to bootstrap classes. "
                                 "Used in no AOT cases",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_countForLooplessBootstrapMethods, 1000, "F%d", NOT_IN_SUBSET },
   {"cpuCompTimeExpensiveThreshold=", "M<nnn>\tthreshold for when hot & very-hot compilations occupied enough cpu time to be considered expensive in millisecond",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_cpuCompTimeExpensiveThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"cpuEntitlementForConservativeScorching=", "M<nnn>\tPercentage. 200 means two full cpus",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_cpuEntitlementForConservativeScorching, 0, "F%d", NOT_IN_SUBSET },
   {"cpuUtilThresholdForStarvation=", "M<nnn>\tThreshold for deciding that a comp thread is not starved",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_cpuUtilThresholdForStarvation , 0, "F%d", NOT_IN_SUBSET},
   {"data=",                          "C<nnn>\tdata cache size, in KB",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, dataCacheKB), 0, "F%d (KB)"},
   {"dataCacheMinQuanta=",            "I<nnn>\tMinimum number of quantums per data cache allocation",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_dataCacheMinQuanta, 0, "F%d", NOT_IN_SUBSET},
   {"dataCacheQuantumSize=",          "I<nnn>\tLargest guaranteed common byte multiple of data cache allocations.  This value will be rounded up for pointer alignment.",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_dataCacheQuantumSize, 0, "F%d", NOT_IN_SUBSET},
   {"datatotal=",              "C<nnn>\ttotal data memory limit, in KB",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, dataCacheTotalKB), 0, "F%d (KB)"},
   {"disableIProfilerClassUnloadThreshold=",      "R<nnn>\tNumber of classes that can be unloaded before we disable the IProfiler",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_disableIProfilerClassUnloadThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"dltPostponeThreshold=",      "M<nnn>\tNumber of dlt attempts inv. count for a method is seen not advancing",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_dltPostponeThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"exclude=",           "D<xxx>\tdo not compile methods beginning with xxx", TR::Options::limitOption, 1, 0, "P%s"},
   {"expensiveCompWeight=", "M<nnn>\tweight of a comp request to be considered expensive",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_expensiveCompWeight, 0, "F%d", NOT_IN_SUBSET },
   {"experimentalClassLoadPhaseInterval=", "O<nnn>\tnumber of sampling ticks to stay in a class load phase",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_experimentalClassLoadPhaseInterval, 0, "P%d", NOT_IN_SUBSET},
   {"gcNotify",           "L\tlog scavenge/ggc notifications to stdout",  SET_JITCONFIG_RUNTIME_FLAG(J9JIT_GC_NOTIFY) },
   {"gcOnResolve",        "D[=<nnn>]\tscavenge on every resolve, or every resolve after nnn",
        TR::Options::gcOnResolveOption, 0, 0, "F=%d"},
   {"GCRQueuedThresholdForCounting=", "M<nnn>\tDisable GCR counting if number of queued GCR requests exceeds this threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_GCRQueuedThresholdForCounting , 0, "F%d", NOT_IN_SUBSET},
#ifdef DEBUG
   {"gcTrace=",           "D<nnn>\ttrace gc stack walks after gc number nnn",
        TR::Options::setJitConfigNumericValue, offsetof(J9JITConfig, gcTraceThreshold), 0, "F%d"},
#endif
#if defined(J9VM_OPT_JITSERVER)
   {"highActiveThreadThreshold=", " \tDefines what is a high Threshold for active compilations",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_highActiveThreadThreshold, 0, "F%d"},
#endif /* defined(J9VM_OPT_JITSERVER) */
   {"HWProfilerAOTWarmOptLevelThreshold=", "O<nnn>\tAOT Warm Opt Level Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerAOTWarmOptLevelThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerBufferMaxPercentageToDiscard=", "O<nnn>\tpercentage of HW profiling buffers "
                                          "that JIT is allowed to discard instead of processing",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwProfilerBufferMaxPercentageToDiscard, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerDisableAOT",           "O<nnn>\tDisable RI AOT",
        SET_OPTION_BIT(TR_HWProfilerDisableAOT), "F", NOT_IN_SUBSET},
   {"HWProfilerDisableRIOverPrivageLinkage","O<nnn>\tDisable RI over private linkage",
        SET_OPTION_BIT(TR_HWProfilerDisableRIOverPrivateLinkage), "F", NOT_IN_SUBSET},
   {"HWProfilerExpirationTime=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwProfilerExpirationTime, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerHotOptLevelThreshold=", "O<nnn>\tHot Opt Level Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerHotOptLevelThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerLastOptLevel=",        "O<nnn>\tLast Opt level",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerLastOptLevel, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerNumDowngradesToTurnRION=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numDowngradesToTurnRION, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerNumOutstandingBuffers=", "O<nnn>\tnumber of outstanding hardware profiling buffers "
                                       "allowed in the system. Specify 0 to disable this optimization",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerNumOutstandingBuffers, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerPRISamplingRate=",     "O<nnn>\tP RI Scaling Factor",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerPRISamplingRate, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerQSZMaxThresholdToRIDowngrade=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qszMaxThresholdToRIDowngrade, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerQSZMinThresholdToRIDowngrade=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qszMinThresholdToRIDowngrade, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerQSZToTurnRION=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qszThresholdToTurnRION, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerRecompilationDecisionWindow=", "R<nnn>\tNumber of decisions to wait for before looking at stats decision outcome",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwProfilerRecompDecisionWindow, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerRecompilationFrequencyThreshold=", "R<nnn>\tLess than 1 in N decisions to recompile, turns RI off",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwProfilerRecompFrequencyThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"HWProfilerRecompilationInterval=", "O<nnn>\tRecompilation Interval",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerRecompilationInterval, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerReducedWarmOptLevelThreshold=", "O<nnn>\tReduced Warm Opt Level Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerReducedWarmOptLevelThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerRIBufferPoolSize=",   "O<nnn>\tRI Buffer Pool Size",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerRIBufferPoolSize, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerRIBufferProcessingFrequency=",   "O<nnn>\tRI Buffer Processing Frequency",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwProfilerRIBufferProcessingFrequency, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerRIBufferThreshold=",  "O<nnn>\tRI Buffer Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerRIBufferThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerScorchingOptLevelThreshold=", "O<nnn>\tScorching Opt Level Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerScorchingOptLevelThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerWarmOptLevelThreshold=", "O<nnn>\tWarm Opt Level Threshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerWarmOptLevelThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerZRIBufferSize=",       "O<nnn>\tZ RI Buffer Size",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerZRIBufferSize, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerZRIMode=",             "O<nnn>\tZ RI Mode",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerZRIMode, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerZRIRGS=",              "O<nnn>\tZ RI Reporting Group Size",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerZRIRGS, 0, "F%d", NOT_IN_SUBSET},
   {"HWProfilerZRISF=",               "O<nnn>\tZ RI Scaling Factor",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_hwprofilerZRISF, 0, "F%d", NOT_IN_SUBSET},
   {"inlinefile=",        "D<filename>\tinline filter defined in filename.  "
                          "Use inlinefile=filename", TR::Options::inlinefileOption, 0, 0, "F%s"},
   {"interpreterSamplingDivisor=",    "R<nnn>\tThe divisor used to decrease the invocation count when an interpreted method is sampled",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_interpreterSamplingDivisor, 0, "F%d", NOT_IN_SUBSET},
   {"interpreterSamplingThreshold=",    "R<nnn>\tThe maximum invocation count at which a sampling hit will result in the count being divided by the value of interpreterSamplingDivisor",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_interpreterSamplingThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"interpreterSamplingThresholdInJSR292=",    "R<nnn>\tThe maximum invocation count at which a sampling hit will result in the count being divided by the value of interpreterSamplingDivisor on a MethodHandle-oriented workload",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_interpreterSamplingThresholdInJSR292, 0, "F%d", NOT_IN_SUBSET},
   {"interpreterSamplingThresholdInStartupMode=",    "R<nnn>\tThe maximum invocation count at which a sampling hit will result in the count being divided by the value of interpreterSamplingDivisor",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_interpreterSamplingThresholdInStartupMode, 0, "F%d", NOT_IN_SUBSET},
   {"invocationThresholdToTriggerLowPriComp=",    "M<nnn>\tNumber of times a loopy method must be invoked to be eligible for LPQ",
       TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_invocationThresholdToTriggerLowPriComp, 0, "F%d", NOT_IN_SUBSET },
   {"iprofilerBufferInterarrivalTimeToExitDeepIdle=", "M<nnn>\tIn ms. If 4 IP buffers arrive back-to-back more frequently than this value, JIT exits DEEP_IDLE",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iProfilerBufferInterarrivalTimeToExitDeepIdle, 0, "F%d", NOT_IN_SUBSET },
   {"iprofilerBufferMaxPercentageToDiscard=", "O<nnn>\tpercentage of interpreter profiling buffers "
                                       "that JIT is allowed to discard instead of processing",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerBufferMaxPercentageToDiscard, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerBufferSize=", "I<nnn>\t set the size of each iprofiler buffer",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerBufferSize, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerFailHistorySize=", "I<nnn>\tNumber of entries for the failure history buffer maintained by Iprofiler",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerFailHistorySize, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerFailRateThreshold=", "I<nnn>\tReactivate Iprofiler if fail rate exceeds this threshold. 1-100",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerFailRateThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerIntToTotalSampleRatio=", "O<nnn>\tRatio of Interpreter samples to Total samples",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerIntToTotalSampleRatio, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerMaxCount=", "O<nnn>\tmax invocation count for IProfiler to be active",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_maxIprofilingCount, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerMaxCountInStartupMode=", "O<nnn>\tmax invocation count for IProfiler to be active in STARTUP phase",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_maxIprofilingCountInStartupMode, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerMemoryConsumptionLimit=",    "O<nnn>\tlimit on memory consumption for interpreter profiling data",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iProfilerMemoryConsumptionLimit, 0, "P%d", NOT_IN_SUBSET},
   {"iprofilerNumOutstandingBuffers=", "O<nnn>\tnumber of outstanding interpreter profiling buffers "
                                       "allowed in the system. Specify 0 to disable this optimization",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerNumOutstandingBuffers, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerOffDivisionFactor=", "O<nnn>\tCounts Division factor when IProfiler is Off",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_IprofilerOffDivisionFactor, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerOffSubtractionFactor=", "O<nnn>\tCounts Subtraction factor when IProfiler is Off",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_IprofilerOffSubtractionFactor, 0, "F%d", NOT_IN_SUBSET},
   {"iprofilerSamplesBeforeTurningOff=", "O<nnn>\tnumber of interpreter profiling samples "
                                "needs to be taken after the profiling starts going off to completely turn it off. "
                                "Specify a very large value to disable this optimization",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_iprofilerSamplesBeforeTurningOff, 0, "P%d", NOT_IN_SUBSET},
   {"itFileNamePrefix=",  "L<filename>\tprefix for itrace filename",
        TR::Options::setStringForPrivateBase, offsetof(TR_JitPrivateConfig,itraceFileNamePrefix), 0, "P%s"},
   {"jProfilingEnablementSampleThreshold=", "M<nnn>\tNumber of global samples to allow generation of JProfiling bodies",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_jProfilingEnablementSampleThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"kcaoffsets",         "I\tGenerate a header file with offset data for use with KCA", TR::Options::kcaOffsets, 0, 0, "F" },
   {"largeTranslationTime=", "D<nnn>\tprint IL trees for methods that take more than this value (usec)"
                             "to compile. Need to have a log file defined on command line",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_largeTranslationTime, 0, "F%d", NOT_IN_SUBSET},
   {"limit=",             "D<xxx>\tonly compile methods beginning with xxx", TR::Options::limitOption, 0, 0, "P%s"},
   {"limitfile=",         "D<filename>\tfilter method compilation as defined in filename.  "
                          "Use limitfile=(filename,firstLine,lastLine) to limit lines considered from firstLine to lastLine",
        TR::Options::limitfileOption, 0, 0, "F%s"},
   {"loadExclude=",           "D<xxx>\tdo not relocate AOT methods beginning with xxx", TR::Options::loadLimitOption, 1, 0, "P%s"},
   {"loadLimit=",             "D<xxx>\tonly relocate AOT methods beginning with xxx", TR::Options::loadLimitOption, 0, 0, "P%s"},
   {"loadLimitFile=",         "D<filename>\tfilter AOT method relocation as defined in filename.  "
                          "Use loadLimitfile=(filename,firstLine,lastLine) to limit lines considered from firstLine to lastLine",
        TR::Options::loadLimitfileOption, 0, 0, "P%s"},
   {"localCSEFrequencyThreshold=", "O<nnn>\tBlocks with frequency lower than the threshold will not be considered by localCSE",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_localCSEFrequencyThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"loopyMethodDivisionFactor=", "O<nnn>\tCounts Division factor for Loopy methods",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_LoopyMethodDivisionFactor, 0, "F%d", NOT_IN_SUBSET},
   {"loopyMethodSubtractionFactor=", "O<nnn>\tCounts Subtraction factor for Loopy methods",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_LoopyMethodSubtractionFactor, 0, "F%d", NOT_IN_SUBSET},
   {"lowerBoundNumProcForScaling=", "M<nnn>\tLower than this numProc we'll use the default scorchingSampleThreshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_lowerBoundNumProcForScaling, 0, "F%d", NOT_IN_SUBSET},
   {"lowVirtualMemoryMBThreshold=","M<nnn>\tThreshold when we declare we are running low on virtual memory. Use 0 to disable the feature",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_lowVirtualMemoryMBThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"maxCheckcastProfiledClassTests=", "R<nnn>\tnumber inlined profiled classes for profiledclass test in checkcast/instanceof",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_maxCheckcastProfiledClassTests, 0, "F%d", NOT_IN_SUBSET},
   {"maxOnsiteCacheSlotForInstanceOf=", "R<nnn>\tnumber of onsite cache slots for instanceOf",
      TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_maxOnsiteCacheSlotForInstanceOf, 0, "F%d", NOT_IN_SUBSET},
   {"minSamplingPeriod=", "R<nnn>\tminimum number of milliseconds between samples for hotness",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_minSamplingPeriod, 0, "P%d", NOT_IN_SUBSET},
   {"minSuperclassArraySize=", "I<nnn>\t set the size of the minimum superclass array size",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_minimumSuperclassArraySize, 0, "F%d", NOT_IN_SUBSET},
   {"noregmap",           0, RESET_JITCONFIG_RUNTIME_FLAG(J9JIT_CG_REGISTER_MAPS) },
   {"numCodeCachesOnStartup=",   "R<nnn>\tnumber of code caches to create at startup",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numCodeCachesToCreateAtStartup, 0, "F%d", NOT_IN_SUBSET},
    {"numDLTBufferMatchesToEagerlyIssueCompReq=", "R<nnn>\t",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numDLTBufferMatchesToEagerlyIssueCompReq, 0, "F%d", NOT_IN_SUBSET},
   {"numInterpCompReqToExitIdleMode=", "M<nnn>\tNumber of first time comp. req. that takes the JIT out of idle mode",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numFirstTimeCompilationsToExitIdleMode, 0, "F%d", NOT_IN_SUBSET },
#if defined(J9VM_OPT_JITSERVER)
   {"oldAge=", " \tDefines what an old JITServer cache entry means",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_oldAge,  0, "F%d"},
   {"oldAgeUnderLowMemory=", " \tDefines what an old JITServer cache entry means when memory is low",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_oldAgeUnderLowMemory,  0, "F%d" },
#endif /* defined(J9VM_OPT_JITSERVER) */
   {"profileAllTheTime=",    "R<nnn>\tInterpreter profiling will be on all the time",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_profileAllTheTime, 0, "F%d", NOT_IN_SUBSET},
   {"queuedInvReqThresholdToDowngradeOptLevel=", "M<nnn>\tDowngrade opt level if too many inv req",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_numQueuedInvReqToDowngradeOptLevel , 0, "F%d", NOT_IN_SUBSET},
   {"queueSizeThresholdToDowngradeDuringCLP=", "M<nnn>\tCompilation queue size threshold (interpreted methods) when opt level is downgraded during class load phase",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qsziThresholdToDowngradeDuringCLP, 0, "F%d", NOT_IN_SUBSET },
   {"queueSizeThresholdToDowngradeOptLevel=", "M<nnn>\tCompilation queue size threshold when opt level is downgraded",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qszThresholdToDowngradeOptLevel , 0, "F%d", NOT_IN_SUBSET},
   {"queueSizeThresholdToDowngradeOptLevelDuringStartup=", "M<nnn>\tCompilation queue size threshold when opt level is downgraded during startup phase",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_qszThresholdToDowngradeOptLevelDuringStartup , 0, "F%d", NOT_IN_SUBSET },
   {"regmap",             0, SET_JITCONFIG_RUNTIME_FLAG(J9JIT_CG_REGISTER_MAPS) },
   {"relaxedCompilationLimitsSampleThreshold=", "R<nnn>\tGlobal samples below this threshold means we can use higher compilation limits",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_relaxedCompilationLimitsSampleThreshold, 0, "F%d", NOT_IN_SUBSET },
   {"resetCountThreshold=", "R<nnn>\tThe number of global samples which if exceed during a method's sampling interval will cause the method's sampling counter to be incremented by the number of samples in a sampling interval",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_resetCountThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"rtlog=",             "L<filename>\twrite verbose run-time output to filename",
        TR::Options::setStringForPrivateBase,  offsetof(TR_JitPrivateConfig,rtLogFileName), 0, "P%s"},
   {"rtResolve",          "D\ttreat all data references as unresolved", SET_JITCONFIG_RUNTIME_FLAG(J9JIT_RUNTIME_RESOLVE) },
   {"safeReservePhysicalMemoryValue=",    "C<nnn>\tsafe buffer value before we risk running out of physical memory, in KB",
        TR::Options::setStaticNumericKBAdjusted, (intptr_t)&TR::Options::_safeReservePhysicalMemoryValue, 0, "F%d (KB)"},
   {"sampleDontSwitchToProfilingThreshold=", "R<nnn>\tThe maximum number of global samples taken during a sample interval for which the method is denied swithing to profiling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_sampleDontSwitchToProfilingThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"sampleThresholdVariationAllowance=",  "R<nnn>\tThe percentage that we add or subtract from"
                                           " the original threshold to adjust for method code size."
                                           " Must be 0--100. Make it 0 to disable this optimization.",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_sampleThresholdVariationAllowance, 0, "P%d", NOT_IN_SUBSET},
   {"samplingFrequencyInDeepIdleMode=", "R<nnn>\tnumber of milliseconds between samples for hotness - in deep idle mode",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_samplingFrequencyInDeepIdleMode, 0, "F%d", NOT_IN_SUBSET},
   {"samplingFrequencyInIdleMode=", "R<nnn>\tnumber of milliseconds between samples for hotness - in idle mode",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_samplingFrequencyInIdleMode, 0, "F%d", NOT_IN_SUBSET},
   {"samplingHeartbeatInterval=", "R<nnn>\tnumber of 100ms periods before sampling heartbeat",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_sampleHeartbeatInterval, 0, "F%d", NOT_IN_SUBSET},
   {"samplingThreadExpirationTime=", "R<nnn>\tnumber of seconds after which point we will stop the sampling thread",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_samplingThreadExpirationTime, 0, "F%d", NOT_IN_SUBSET},
   {"scorchingSampleThreshold=", "R<nnn>\tThe maximum number of global samples taken during a sample interval for which the method will be recompiled as scorching",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_scorchingSampleThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"scratchSpaceFactorWhenJSR292Workload=","M<nnn>\tMultiplier for scratch space limit when MethodHandles are in use",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_scratchSpaceFactorWhenJSR292Workload, 0, "F%d", NOT_IN_SUBSET},
   {"scratchSpaceLimitKBWhenLowVirtualMemory=","M<nnn>\tLimit for memory used by JIT when running on low virtual memory",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_scratchSpaceLimitKBWhenLowVirtualMemory, 0, "F%d", NOT_IN_SUBSET},
   {"secondaryClassLoadPhaseThreshold=", "O<nnn>\tWhen class load rate just dropped under the CLP threshold  "
                                         "we use this secondary threshold to determine class load phase",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_secondaryClassLoadingPhaseThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"seriousCompFailureThreshold=",     "M<nnn>\tnumber of srious compilation failures after which we write a trace point in the snap file",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_seriousCompFailureThreshold, 0, "F%d", NOT_IN_SUBSET},
#if defined(J9VM_OPT_JITSERVER)
   {"sharedROMClassCacheNumPartitions=", " \tnumber of JITServer ROMClass cache partitions (each has its own monitor)",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_sharedROMClassCacheNumPartitions, 0, "F%d", NOT_IN_SUBSET},
   {"shareROMClasses", " \tstore a single copy of each distinct ROMClass shared by all clients at JITServer",
        TR::Options::setStaticBool, (intptr_t)&TR::Options::_shareROMClasses, 1, "F", NOT_IN_SUBSET},
#endif /* defined(J9VM_OPT_JITSERVER) */
   {"singleCache", "C\tallow only one code cache and one data cache to be allocated", RESET_JITCONFIG_RUNTIME_FLAG(J9JIT_GROW_CACHES) },
   {"smallMethodBytecodeSizeThreshold=", "O<nnn> Threshold for determining small methods\t "
                                         "(measured in number of bytecodes)",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_smallMethodBytecodeSizeThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"smallMethodBytecodeSizeThresholdForCold=", "O<nnn> Threshold for determining small methods at cold\t "
                                         "(measured in number of bytecodes)",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_smallMethodBytecodeSizeThresholdForCold, 0, "F%d", NOT_IN_SUBSET},
   {"stack=",             "C<nnn>\tcompilation thread stack size in KB",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_stackSize, 0, "F%d", NOT_IN_SUBSET},
#if defined(J9VM_OPT_JITSERVER)
   {"statisticsFrequency=", "R<nnn>\tnumber of milliseconds between statistics print",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_statisticsFrequency, 0, "F%d", NOT_IN_SUBSET},
#endif /* defined(J9VM_OPT_JITSERVER) */
   {"testMode",           "D\tcompile but do not run the compiled code",  SET_JITCONFIG_RUNTIME_FLAG(J9JIT_TESTMODE) },
#if defined(J9VM_OPT_JITSERVER)
   {"timeBetweenPurges=", " \tDefines how often we are willing to scan for old entries to be purged",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_timeBetweenPurges,  0, "F%d"},
#endif /* defined(J9VM_OPT_JITSERVER) */
#if defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_ARM64)
   {"tlhPrefetchBoundaryLineCount=",    "O<nnn>\tallocation prefetch boundary line for allocation prefetch",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchBoundaryLineCount, 0, "P%d", NOT_IN_SUBSET},
   {"tlhPrefetchLineCount=",    "O<nnn>\tallocation prefetch line count for allocation prefetch",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchLineCount, 0, "P%d", NOT_IN_SUBSET},
   {"tlhPrefetchLineSize=",    "O<nnn>\tallocation prefetch line size for allocation prefetch",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchLineSize, 0, "P%d", NOT_IN_SUBSET},
   {"tlhPrefetchSize=",    "O<nnn>\tallocation prefetch size for allocation prefetch",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchSize, 0, "P%d", NOT_IN_SUBSET},
   {"tlhPrefetchStaggeredLineCount=",    "O<nnn>\tallocation prefetch staggered line for allocation prefetch",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchStaggeredLineCount, 0, "P%d", NOT_IN_SUBSET},
   {"tlhPrefetchTLHEndLineCount=",    "O<nnn>\tallocation prefetch line count for end of TLH check",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_TLHPrefetchTLHEndLineCount, 0, "P%d", NOT_IN_SUBSET},
#endif
   {"tossCode",           "D\tthrow code and data away after compiling",  SET_JITCONFIG_RUNTIME_FLAG(J9JIT_TOSS_CODE) },
   {"tprof",              "D\tgenerate time profiles with SWTRACE (requires -Xrunjprof12x:jita2n)",
        TR::Options::tprofOption, 0, 0, "F"},
   {"updateFreeMemoryMinPeriod=", "R<nnn>\tnumber of milliseconds after which point we will update the free physical memory available",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_updateFreeMemoryMinPeriod, 0, "F%d", NOT_IN_SUBSET},
   {"upperBoundNumProcForScaling=", "M<nnn>\tHigher than this numProc we'll use the conservativeScorchingSampleThreshold",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_upperBoundNumProcForScaling, 0, "F%d", NOT_IN_SUBSET},
   { "userClassLoadPhaseThreshold=", "O<nnn>\tnumber of user classes loaded per sampling tick that "
           "needs to be attained to enter the class loading phase. "
           "Specify a very large value to disable this optimization",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_userClassLoadingPhaseThreshold, 0, "P%d", NOT_IN_SUBSET },
   {"verbose",            "L\twrite compiled method names to vlog file or stdout in limitfile format",
        TR::Options::setVerboseBitsInJitPrivateConfig, offsetof(J9JITConfig, privateConfig), 5, "F=1"},
   {"verbose=",           "L{regex}\tlist of verbose output to write to vlog or stdout",
        TR::Options::setVerboseBitsInJitPrivateConfig, offsetof(J9JITConfig, privateConfig), 0, "F"},
   {"version",            "L\tdisplay the jit build version",
        TR::Options::versionOption, 0, 0, "F"},
#if defined(J9VM_OPT_JITSERVER)
   {"veryHighActiveThreadThreshold=", " \tDefines what is a very high Threshold for active compilations",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_veryHighActiveThreadThreshold, 0, "F%d"},
#endif /* defined(J9VM_OPT_JITSERVER) */
   {"veryHotSampleThreshold=",          "R<nnn>\tThe maximum number of global samples taken during a sample interval for which the method will be recompiled at hot with normal priority",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_veryHotSampleThreshold, 0, "F%d", NOT_IN_SUBSET},
   {"vlog=",              "L<filename>\twrite verbose output to filename",
        TR::Options::setString,  offsetof(J9JITConfig,vLogFileName), 0, "F%s"},
   {"vmState=",           "L<vmState>\tdecode a given vmState",
        TR::Options::vmStateOption, 0, 0, "F"},
   {"waitTimeToEnterDeepIdleMode=",  "M<nnn>\tTime spent in idle mode (ms) after which we enter deep idle mode sampling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_waitTimeToEnterDeepIdleMode, 0, "F%d", NOT_IN_SUBSET},
   {"waitTimeToEnterIdleMode=",      "M<nnn>\tIdle time (ms) after which we enter idle mode sampling",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_waitTimeToEnterIdleMode, 0, "F%d", NOT_IN_SUBSET},
   {"waitTimeToExitStartupMode=",     "M<nnn>\tTime (ms) spent outside startup needed to declare NON_STARTUP mode",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_waitTimeToExitStartupMode, 0, "F%d", NOT_IN_SUBSET},
   {"waitTimeToGCR=",                 "M<nnn>\tTime (ms) spent outside startup needed to start guarded counting recompilations",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_waitTimeToGCR, 0, "F%d", NOT_IN_SUBSET},
   {"waitTimeToStartIProfiler=",                 "M<nnn>\tTime (ms) spent outside startup needed to start IProfiler if it was off",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_waitTimeToStartIProfiler, 0, "F%d", NOT_IN_SUBSET},
   {"weightOfAOTLoad=",              "M<nnn>\tWeight of an AOT load. 0 by default",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_weightOfAOTLoad, 0, "F%d", NOT_IN_SUBSET},
   {"weightOfJSR292=", "M<nnn>\tWeight of an JSR292 compilation. Number between 0 and 255",
        TR::Options::setStaticNumeric, (intptr_t)&TR::Options::_weightOfJSR292, 0, "F%d", NOT_IN_SUBSET },
   {0}
};


bool J9::Options::showOptionsInEffect()
   {
   if (this == TR::Options::getAOTCmdLineOptions() && self()->getOption(TR_NoLoadAOT) && self()->getOption(TR_NoStoreAOT))
      return false;
   else
      return (TR::Options::isAnyVerboseOptionSet(TR_VerboseOptions, TR_VerboseExtended));
   }

// z/OS tool FIXMAPC doesn't allow duplicated ASID statements.
// For the case of -Xjit:verbose={mmap} and -Xaot:verbose={mmap} will generate two ASID statements.
// Make sure only have one ASID statement
bool J9::Options::showPID()
   {
   static bool showedAlready=false;

   if (!showedAlready)
      {
      if (TR::Options::getVerboseOption(TR_VerboseMMap))
         {
         showedAlready = true;
         return true;
         }
      }
   return false;
   }

#if defined(J9VM_OPT_JITSERVER)
static std::string readFileToString(char *fileName)
   {
   PORT_ACCESS_FROM_PORT(TR::Compiler->portLib);
   FILE *f = fopen(fileName, "rb");
   if (!f)
      {
      j9tty_printf(PORTLIB, "Fatal Error: Unable to open file (%s)\n", fileName);
      return "";
      }
   const uint32_t BUFFER_SIZE = 4096; // 4KB
   const uint32_t MAX_FILE_SIZE_IN_PAGES = 16; // 64KB
   char buf[BUFFER_SIZE];
   std::string fileStr("");
   int readSize = 0;
   int iter = 0;
   do {
      readSize = fread(buf, 1, BUFFER_SIZE, f);
      fileStr = fileStr.append(buf, readSize);
      ++iter;
   } while ((readSize == BUFFER_SIZE) && (iter <= MAX_FILE_SIZE_IN_PAGES));
   fclose(f);

   if (iter <= MAX_FILE_SIZE_IN_PAGES)
      {
      return fileStr;
      }
   else
      {
      j9tty_printf(
         PORTLIB,
         "Fatal Error: File (%s) is too large, max allowed size is %dKB\n",
         fileName,
         BUFFER_SIZE * MAX_FILE_SIZE_IN_PAGES / 1000);
      return "";
      }
   }

static bool JITServerParseCommonOptions(J9JavaVM *vm, TR::CompilationInfo *compInfo)
   {
   const char *xxJITServerPortOption = "-XX:JITServerPort=";
   const char *xxJITServerTimeoutOption = "-XX:JITServerTimeout=";
   const char *xxJITServerSSLKeyOption = "-XX:JITServerSSLKey=";
   const char *xxJITServerSSLCertOption = "-XX:JITServerSSLCert=";
   const char *xxJITServerSSLRootCertsOption = "-XX:JITServerSSLRootCerts=";
   const char *xxJITServerUseAOTCacheOption = "-XX:+JITServerUseAOTCache";
   const char *xxDisableJITServerUseAOTCacheOption = "-XX:-JITServerUseAOTCache";
   const char *xxRequireJITServerOption = "-XX:+RequireJITServer";
   const char *xxDisableRequireJITServerOption = "-XX:-RequireJITServer";

   int32_t xxJITServerPortArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerPortOption, 0);
   int32_t xxJITServerTimeoutArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerTimeoutOption, 0);
   int32_t xxJITServerSSLKeyArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerSSLKeyOption, 0);
   int32_t xxJITServerSSLCertArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerSSLCertOption, 0);
   int32_t xxJITServerSSLRootCertsArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerSSLRootCertsOption, 0);
   int32_t xxJITServerUseAOTCacheArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxJITServerUseAOTCacheOption, 0);
   int32_t xxDisableJITServerUseAOTCacheArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxDisableJITServerUseAOTCacheOption, 0);
   int32_t xxRequireJITServerArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxRequireJITServerOption, 0);
   int32_t xxDisableRequireJITServerArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxDisableRequireJITServerOption, 0);

   if (xxJITServerPortArgIndex >= 0)
      {
      uint32_t port=0;
      IDATA ret = GET_INTEGER_VALUE(xxJITServerPortArgIndex, xxJITServerPortOption, port);
      if (ret == OPTION_OK)
         compInfo->getPersistentInfo()->setJITServerPort(port);
      }

   if (xxRequireJITServerArgIndex > xxDisableRequireJITServerArgIndex)
      {
      // If a debugging option to require JITServer connection is enabled, increase socket timeout,
      // to prevent false positives from streams failing due to timeout.
      // User-provided values still take priority.
      compInfo->getPersistentInfo()->setRequireJITServer(true);
      compInfo->getPersistentInfo()->setSocketTimeout(60000);
      }

   if (xxJITServerTimeoutArgIndex >= 0)
      {
      uint32_t timeoutMs=0;
      IDATA ret = GET_INTEGER_VALUE(xxJITServerTimeoutArgIndex, xxJITServerTimeoutOption, timeoutMs);
      if (ret == OPTION_OK)
         compInfo->getPersistentInfo()->setSocketTimeout(timeoutMs);
      }

   // key and cert have to be set as a pair at the server
   if ((xxJITServerSSLKeyArgIndex >= 0) && (xxJITServerSSLCertArgIndex >= 0))
      {
      char *keyFileName = NULL;
      char *certFileName = NULL;
      GET_OPTION_VALUE(xxJITServerSSLKeyArgIndex, '=', &keyFileName);
      GET_OPTION_VALUE(xxJITServerSSLCertArgIndex, '=', &certFileName);
      std::string key = readFileToString(keyFileName);
      std::string cert = readFileToString(certFileName);

      if (!key.empty() && !cert.empty())
         {
         compInfo->addJITServerSslKey(key);
         compInfo->addJITServerSslCert(cert);
         }
      else
         {
         return false;
         }
      }

   if (xxJITServerSSLRootCertsArgIndex >= 0)
      {
      char *fileName = NULL;
      GET_OPTION_VALUE(xxJITServerSSLRootCertsArgIndex, '=', &fileName);
      std::string cert = readFileToString(fileName);
      if (!cert.empty())
         compInfo->setJITServerSslRootCerts(cert);
      else
         return false;
      }

   if (xxJITServerUseAOTCacheArgIndex > xxDisableJITServerUseAOTCacheArgIndex)
      compInfo->getPersistentInfo()->setJITServerUseAOTCache(true);

   return true;
   }
#endif /* defined(J9VM_OPT_JITSERVER) */

void J9::Options::preProcessMmf(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   J9MemoryManagerFunctions * mmf = vm->memoryManagerFunctions;

#if defined(J9VM_GC_HEAP_CARD_TABLE)
   TR_J9VMBase * fe = TR_J9VMBase::get(jitConfig, 0);
   if (!fe->isAOT_DEPRECATED_DO_NOT_USE())
      {
      self()->setGcCardSize(mmf->j9gc_concurrent_getCardSize(vm));
      self()->setHeapBase(mmf->j9gc_concurrent_getHeapBase(vm));
      self()->setHeapTop(mmf->j9gc_concurrent_getHeapBase(vm) + mmf->j9gc_get_initial_heap_size(vm));
      }
#endif

   uintptr_t value;

   value = mmf->j9gc_modron_getConfigurationValueForKey(vm, j9gc_modron_configuration_heapBaseForBarrierRange0_isVariable, &value) ? value : 0;
   self()->setIsVariableHeapBaseForBarrierRange0(value);

   value = mmf->j9gc_modron_getConfigurationValueForKey(vm, j9gc_modron_configuration_heapSizeForBarrierRange0_isVariable, &value) ? value : 0;
   self()->setIsVariableHeapSizeForBarrierRange0(value);

   value = mmf->j9gc_modron_getConfigurationValueForKey(vm, j9gc_modron_configuration_activeCardTableBase_isVariable, &value) ? value : 0;
   self()->setIsVariableActiveCardTableBase(value);

   value = mmf->j9gc_modron_getConfigurationValueForKey(vm, j9gc_modron_configuration_heapAddressToCardAddressShift, &value) ? value : 0;
   self()->setHeapAddressToCardAddressShift(value);

   // Pull the constant heap parameters from a VMThread (it doesn't matter which one).
   //
   J9VMThread *vmThread = jitConfig->javaVM->internalVMFunctions->currentVMThread(jitConfig->javaVM);

   if (vmThread)
      {
      self()->setHeapBaseForBarrierRange0((uintptr_t)vmThread->heapBaseForBarrierRange0);
      self()->setHeapSizeForBarrierRange0((uintptr_t)vmThread->heapSizeForBarrierRange0);
      self()->setActiveCardTableBase((uintptr_t)vmThread->activeCardTableBase);
      }
   else
      {
      // The heap information could not be found at compile-time.  Make sure this information
      // is loaded from the vmThread at runtime.
      //
      self()->setIsVariableHeapBaseForBarrierRange0(true);
      self()->setIsVariableHeapSizeForBarrierRange0(true);
      self()->setIsVariableActiveCardTableBase(true);
      }

   if (J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_PORTABLE_SHARED_CACHE)
       || J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_CRIU_SUPPORT))
      {
      // Disable any fixed-size heap optimizations under portable shared cache mode
      self()->setIsVariableHeapSizeForBarrierRange0(true);
      }

#if defined(TR_TARGET_64BIT) && defined(J9ZOS390)
   PORT_ACCESS_FROM_JAVAVM(vm);

   OMROSDesc desc;
   j9sysinfo_get_os_description(&desc);

   // Enable RMODE64 if and only if the z/OS version has proper kernel support
   if (j9sysinfo_os_has_feature(&desc, OMRPORT_ZOS_FEATURE_RMODE64))
      {
      self()->setOption(TR_EnableRMODE64);
      }
#endif

   // { RTSJ Support Begin

   #if defined(J9VM_OPT_REAL_TIME_LOCKING_SUPPORT)
      self()->setOption(TR_DisableMonitorOpts);
   #endif


   value = mmf->j9gc_modron_getConfigurationValueForKey(vm, j9gc_modron_configuration_allocationType,&value) ?value:0;
   if (j9gc_modron_allocation_type_segregated == value)
      self()->setRealTimeGC(true);
   else
      self()->setRealTimeGC(false);
   // } RTSJ Support End
   }

void J9::Options::preProcessMode(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   // Determine the mode we want to be in
   // Possible options: client/Quickstart, server, aggressive, noquickstart
   if (jitConfig->runtimeFlags & J9JIT_QUICKSTART)
      {
      self()->setQuickStart();
      }
   else
      {
      // if the server mode is set
      if ((FIND_ARG_IN_VMARGS(EXACT_MATCH, "-server", 0)) >=0) // I already know I cannot be in -client mode
         self()->setOption(TR_Server);
      }

   if (vm->runtimeFlags & J9_RUNTIME_AGGRESSIVE)
      {
      self()->setOption(TR_AggressiveOpts);
      }

   // The aggressivenessLevel can only be specified with a VM option (-XaggressivenessLevel)
   // The level should be only set once (it's a static)
   // This is a second hand citizen option; if other options contradict it, this option is
   // ignored even if it appears later
   if (!self()->getOption(TR_AggressiveOpts) &&
       !(jitConfig->runtimeFlags & J9JIT_QUICKSTART) &&
       !self()->getOption(TR_Server))
      {
      // Xtune:virtualized will put us in aggressivenessLevel3, but only if other options
      // like Xquickstart, -client, -server, -Xaggressive are not specified
      if (vm->runtimeFlags & J9_RUNTIME_TUNE_VIRTUALIZED)
         {
         _aggressivenessLevel = TR::Options::AGGRESSIVE_AOT;
         }
      if (_aggressivenessLevel == -1) // not yet set
         {
         char *aggressiveOption = "-XaggressivenessLevel";
         int32_t argIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, aggressiveOption, 0);
         if (argIndex >= 0)
            {
            UDATA aggressivenessValue = 0;
            IDATA ret = GET_INTEGER_VALUE(argIndex, aggressiveOption, aggressivenessValue);
            if (ret == OPTION_OK && aggressivenessValue >= 0)
               {
               _aggressivenessLevel = aggressivenessValue;
               }
            }
         else // option not specified on command line
            {
            // Automatically set an aggressiveness level based on CPU resources
#if 0 // Do not change the default behavior just yet; needs more testing
            TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
            if (compInfo->getJvmCpuEntitlement() < 100.0) // less than a processor available
               _aggressivenessLevel = TR::Options::CONSERVATIVE_QUICKSTART;
            else if (compInfo->getJvmCpuEntitlement() < 200.0) // less than 2 processors
               _aggressivenessLevel = TR::Options::AGGRESSIVE_QUICKSTART;
#endif
            }
         }
      }
   }

void J9::Options::preProcessJniAccelerator(J9JavaVM *vm)
   {
   static bool doneWithJniAcc = false;
   char *jniAccOption = "-XjniAcc:";
   if (!doneWithJniAcc)
      {
      int32_t argIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, jniAccOption, 0);
      if (argIndex >= 0)
         {
         char *optValue;
         doneWithJniAcc = true;
         GET_OPTION_VALUE(argIndex, ':', &optValue);
         if (*optValue == '{')
            {
            if (!_debug)
               TR::Options::createDebug();
            if (_debug)
               {
               TR::SimpleRegex *mRegex;
               mRegex = TR::SimpleRegex::create(optValue);
               if (!mRegex || *optValue != 0)
                  {
                  TR_VerboseLog::writeLine(TR_Vlog_FAILURE, "Bad regular expression at --> '%s'", optValue);
                  }
               else
                  {
                  TR::Options::setJniAccelerator(mRegex);
                  }
               }
            }
         }
      }
   }

void J9::Options::preProcessCodeCacheIncreaseTotalSize(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   PORT_ACCESS_FROM_JAVAVM(vm);
   OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

   // Check for option to increase code cache total size
   static bool codecachetotalAlreadyParsed = false;
   if (!codecachetotalAlreadyParsed) // avoid processing twice for AOT and JIT and produce duplicate messages
      {
      codecachetotalAlreadyParsed = true;
      char *xccOption  = "-Xcodecachetotal";
      char *xxccOption = "-XX:codecachetotal=";
      int32_t codeCacheTotalArgIndex   = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, xccOption, 0);
      int32_t XXcodeCacheTotalArgIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, xxccOption, 0);
      int32_t argIndex = 0;
      // Check if option is at all specified
      if (codeCacheTotalArgIndex >= 0 || XXcodeCacheTotalArgIndex >= 0)
         {
         char *ccTotalOption;
         if (XXcodeCacheTotalArgIndex > codeCacheTotalArgIndex)
            {
            argIndex = XXcodeCacheTotalArgIndex;
            ccTotalOption = xxccOption;
            }
         else
            {
            argIndex = codeCacheTotalArgIndex;
            ccTotalOption = xccOption;
            }
         UDATA ccTotalSize;
         IDATA returnCode = GET_MEMORY_VALUE(argIndex, ccTotalOption, ccTotalSize);
         if (OPTION_OK == returnCode)
            {
            ccTotalSize >>= 10; // convert to KB

            // Impose a minimum value of 2 MB
            if (ccTotalSize < 2048)
               ccTotalSize = 2048;

            // Restriction: total size must be a multiple of the size of one code cache
            UDATA fragmentSize = ccTotalSize % jitConfig->codeCacheKB;
            if (fragmentSize > 0)   // TODO: do we want a message here?
               ccTotalSize += jitConfig->codeCacheKB - fragmentSize; // round-up

            // Proportionally increase the data cache as well
            // Use 'double' to avoid truncation/overflow
            UDATA dcTotalSize = (double)ccTotalSize / (double)(jitConfig->codeCacheTotalKB) *
               (double)(jitConfig->dataCacheTotalKB);

            // Round up to a multiple of the data cache size
            fragmentSize = dcTotalSize % jitConfig->dataCacheKB;
            if (fragmentSize > 0)
               dcTotalSize += jitConfig->dataCacheKB - fragmentSize;
            // Now write the values in jitConfig
            jitConfig->codeCacheTotalKB = ccTotalSize;
            // Make sure that the new value for dataCacheTotal doesn't shrink the default
            if (dcTotalSize > jitConfig->dataCacheTotalKB)
               jitConfig->dataCacheTotalKB = dcTotalSize;
            }
         else // Error with the option
            {
            // TODO: do we want a message here?
            j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_JIT_OPTIONS_INCORRECT_MEMORY_SIZE, ccTotalOption);
            }
         }
      }
   }

void J9::Options::preProcessCodeCachePrintCodeCache(J9JavaVM *vm)
   {
   // -XX:+PrintCodeCache will be parsed twice into both AOT and JIT options here.
   const char *xxPrintCodeCacheOption = "-XX:+PrintCodeCache";
   const char *xxDisablePrintCodeCacheOption = "-XX:-PrintCodeCache";
   int32_t xxPrintCodeCacheArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxPrintCodeCacheOption, 0);
   int32_t xxDisablePrintCodeCacheArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxDisablePrintCodeCacheOption, 0);

   if (xxPrintCodeCacheArgIndex > xxDisablePrintCodeCacheArgIndex)
      {
      self()->setOption(TR_PrintCodeCacheUsage);
      }
   }

bool J9::Options::preProcessCodeCacheXlpCodeCache(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   PORT_ACCESS_FROM_JAVAVM(vm);
   OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

   // Enable on X and Z, also on P.
   // PPC supports -Xlp:codecache option.. since it's set via environment variables.  JVM should always request 4k pages.
   // fePreProcess is called twice - for AOT and JIT options parsing, which is redundant in terms of
   // processing the -Xlp:codecache options.
   // We should parse the -Xlp:codecache options only once though to avoid duplicate NLS messages.
   static bool parsedXlpCodeCacheOptions = false;

   if (!parsedXlpCodeCacheOptions)
      {
      parsedXlpCodeCacheOptions = true;

      UDATA requestedLargeCodePageSize = 0;
      UDATA requestedLargeCodePageFlags = J9PORT_VMEM_PAGE_FLAG_NOT_USED;
      UDATA largePageSize = 0;
      UDATA largePageFlags = 0;
      int32_t xlpCodeCacheIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, "-Xlp:codecache:", NULL);
      int32_t xlpIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xlp", NULL);

      // Parse -Xlp:codecache:pagesize=<size> as the right most option
      if (xlpCodeCacheIndex > xlpIndex)
         {
         TR_XlpCodeCacheOptions parsingState = XLPCC_PARSING_FIRST_OPTION;
         UDATA optionNumber = 1;
         bool extraCommaWarning = false;
         char *previousOption = NULL;
         char *errorString = NULL;

         UDATA pageSizeHowMany = 0;
         UDATA pageSizeOptionNumber = 0;
         UDATA pageableHowMany = 0;
         UDATA pageableOptionNumber = 0;
         UDATA nonPageableHowMany = 0;
         UDATA nonPageableOptionNumber = 0;

         char *optionsString = NULL;

         // Get Pointer to entire "-Xlp:codecache:" options string
         GET_OPTION_OPTION(xlpCodeCacheIndex, ':', ':', &optionsString);

         // optionsString can not be NULL here, though it may point to null ('\0') character
         char *scanLimit = optionsString + strlen(optionsString);

         // Parse -Xlp:codecache:pagesize=<size> option.
         // The proper formed -Xlp:codecache: options include
         //      For X and zLinux platforms:
         //              -Xlp:codecache:pagesize=<size> or
         //              -Xlp:codecache:pagesize=<size>,pageable or
         //              -Xlp:codecache:pagesize=<size>,nonpageable
         //      For zOS platforms
         //              -Xlp:codecache:pagesize=<size>,pageable or
         //              -Xlp:codecache:pagesize=<size>,nonpageable
         while (optionsString < scanLimit)
            {
            if (try_scan(&optionsString, ","))
               {
               // Comma separator is discovered
               switch (parsingState)
                  {
                  case XLPCC_PARSING_FIRST_OPTION:
                     // leading comma - ignored but warning required
                     extraCommaWarning = true;
                     parsingState = XLPCC_PARSING_OPTION;
                     break;
                  case XLPCC_PARSING_OPTION:
                     // more then one comma - ignored but warning required
                     extraCommaWarning = true;
                     break;
                  case XLPCC_PARSING_COMMA:
                     // expecting for comma here, next should be an option
                     parsingState = XLPCC_PARSING_OPTION;
                     // next option number
                     optionNumber += 1;
                     break;
                  case XLPCC_PARSING_ERROR:
                  default:
                     return false;
                  }
               }
            else
               {
               // Comma separator has not been found. so
               switch (parsingState)
                  {
                  case XLPCC_PARSING_FIRST_OPTION:
                     // still looking for parsing of first option - nothing to do
                     parsingState = XLPCC_PARSING_OPTION;
                     break;
                  case XLPCC_PARSING_OPTION:
                     // Can not recognize an option case
                     errorString = optionsString;
                     parsingState = XLPCC_PARSING_ERROR;
                     break;
                  case XLPCC_PARSING_COMMA:
                     // can not find comma after option - so this is something unrecognizable at the end of known option
                     errorString = previousOption;
                     parsingState = XLPCC_PARSING_ERROR;
                     break;
                  case XLPCC_PARSING_ERROR:
                  default:
                     // must be unreachable states
                     return false;
                  }
               }

            if (XLPCC_PARSING_ERROR == parsingState)
               {
               char *xlpOptionErrorString = errorString;
               int32_t xlpOptionErrorStringSize = 0;

               // try to find comma to isolate unrecognized option
               char *commaLocation = strchr(errorString,',');

               if (NULL != commaLocation)
                  {
                  // Comma found
                  xlpOptionErrorStringSize = (size_t)(commaLocation - errorString);
                  }
               else
                  {
                  // comma not found - print to end of string
                  xlpOptionErrorStringSize = strlen(errorString);
                  }
               j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_XLP_UNRECOGNIZED_OPTION, xlpOptionErrorStringSize, xlpOptionErrorString);
               return false;
               }

            previousOption = optionsString;

            if (try_scan(&optionsString, "pagesize="))
               {
               // Try to get memory value.
               // Given substring, we can't use GET_MEMORY_VALUE.
               // Scan for UDATA and M/m,G/g,K/k manually.
               UDATA scanResult = scan_udata(&optionsString, &requestedLargeCodePageSize);

               // First scan for the integer string.
               if (0 != scanResult)
                  {
                  if (1 == scanResult)
                     j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_MUST_BE_NUMBER, "pagesize=");
                  else
                     j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_VALUE_OVERFLOWED, "pagesize=");
                  j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_INCORRECT_MEMORY_SIZE, "-Xlp:codecache:pagesize=<size>");
                  return false;
                  }

               // Check for size qualifiers (G/M/K)
               UDATA qualifierShiftAmount = 0;
               if(try_scan(&optionsString, "G") || try_scan(&optionsString, "g"))
                  qualifierShiftAmount = 30;
               else if(try_scan(&optionsString, "M") || try_scan(&optionsString, "m"))
                  qualifierShiftAmount = 20;
               else if(try_scan(&optionsString, "K") || try_scan(&optionsString, "k"))
                  qualifierShiftAmount = 10;

               if (0 != qualifierShiftAmount)
                  {
                  // Check for overflow
                  if (requestedLargeCodePageSize <= (((UDATA)-1) >> qualifierShiftAmount))
                     {
                     requestedLargeCodePageSize <<= qualifierShiftAmount;
                     }
                  else
                     {
                     j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_VALUE_OVERFLOWED, "pagesize=");
                     j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_INCORRECT_MEMORY_SIZE, "-Xlp:codecache:pagesize=<size>");
                     return false;
                     }
                  }

               pageSizeHowMany += 1;
               pageSizeOptionNumber = optionNumber;

               parsingState = XLPCC_PARSING_COMMA;
               }
            else if (try_scan(&optionsString, "pageable"))
               {
               pageableHowMany += 1;
               pageableOptionNumber = optionNumber;

               parsingState = XLPCC_PARSING_COMMA;
               }
            else if (try_scan(&optionsString, "nonpageable"))
               {
               nonPageableHowMany += 1;
               nonPageableOptionNumber = optionNumber;

               parsingState = XLPCC_PARSING_COMMA;
               }
            }

         // post-parse check for trailing comma(s)
         switch (parsingState)
            {
            // if loop ended in one of these two states extra comma warning required
            case XLPCC_PARSING_FIRST_OPTION:
            case XLPCC_PARSING_OPTION:
               // trailing comma(s) or comma(s) alone
               extraCommaWarning = true;
               break;
            case XLPCC_PARSING_COMMA:
               // loop ended at comma search state - do nothing
               break;
            case XLPCC_PARSING_ERROR:
            default:
               // must be unreachable states
               return false;
            }

          // Verify "pagesize=<size>" option.
          // This option must be specified for all platforms.
         if (0 == pageSizeHowMany)
            {
            j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_XLP_INCOMPLETE_OPTION, "-Xlp:codecache:", "pagesize=");
            return false;
            }

         #if defined(J9ZOS390)
         //  [non]pageable option must be specified for Z platforms
         if ((0 == pageableHowMany) && (0 == nonPageableHowMany))
            {
            // [non]pageable not found
            char *xlpOptionErrorString = "-Xlp:codecache:";
            char *xlpMissingOptionString = "[non]pageable";

            j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JIT_OPTIONS_XLP_INCOMPLETE_OPTION, xlpOptionErrorString, xlpMissingOptionString);
            return false;
            }

         // Check for the right most option is most right
         if (pageableOptionNumber > nonPageableOptionNumber)
            requestedLargeCodePageFlags = J9PORT_VMEM_PAGE_FLAG_PAGEABLE;
         else
            requestedLargeCodePageFlags = J9PORT_VMEM_PAGE_FLAG_FIXED;

         #endif // defined(J9ZOS390)

         // print extra comma ignored warning
         if (extraCommaWarning)
            j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_JIT_OPTIONS_XLP_EXTRA_COMMA);
         }
      // Parse Size -Xlp<size>
      else if (xlpIndex >= 0)
         {
         // GET_MEMORY_VALUE macro casts it's second parameter to (char**)&, so a pointer to the option string is passed rather than the string literal.
         char *lpOption = "-Xlp";
         GET_MEMORY_VALUE(xlpIndex, lpOption, requestedLargeCodePageSize);
         }

      if (requestedLargeCodePageSize != 0)
         {
         // Check to see if requested size is valid
         BOOLEAN isRequestedSizeSupported = FALSE;
         largePageSize = requestedLargeCodePageSize;
         largePageFlags = requestedLargeCodePageFlags;


         // j9vmem_find_valid_page_size happened to be changed to always return 0
         // However formally the function type still be IDATA so assert if it returns anything else
         j9vmem_find_valid_page_size(J9PORT_VMEM_MEMORY_MODE_EXECUTE, &largePageSize, &largePageFlags, &isRequestedSizeSupported);

         if (!isRequestedSizeSupported)
            {
            // Generate warning message for user that requested page sizes / type is not supported.
            char *oldQualifier, *newQualifier;
            char *oldPageType = NULL;
            char *newPageType = NULL;
            UDATA oldSize = requestedLargeCodePageSize;
            UDATA newSize = largePageSize;

            // Convert size to K,M,G qualifiers.
            qualifiedSize(&oldSize, &oldQualifier);
            qualifiedSize(&newSize, &newQualifier);

            if (0 == (J9PORT_VMEM_PAGE_FLAG_NOT_USED & requestedLargeCodePageFlags))
            oldPageType = getLargePageTypeString(requestedLargeCodePageFlags);

            if (0 == (J9PORT_VMEM_PAGE_FLAG_NOT_USED & largePageFlags))
            newPageType = getLargePageTypeString(largePageFlags);

            if (NULL == oldPageType)
               {
               if (NULL == newPageType)
                  j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_JIT_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED, oldSize, oldQualifier, newSize, newQualifier);
               else
                  j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_JIT_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_NEW_PAGETYPE, oldSize, oldQualifier, newSize, newQualifier, newPageType);
               }
            else
               {
               if (NULL == newPageType)
                  j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_JIT_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_REQUESTED_PAGETYPE, oldSize, oldQualifier, oldPageType, newSize, newQualifier);
               else
                  j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_JIT_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_PAGETYPE, oldSize, oldQualifier, oldPageType, newSize, newQualifier, newPageType);
               }
            }
         }
      // When no -Xlp arguments are passed, we should use preferred page sizes
      else
         {
         UDATA *pageSizes = j9vmem_supported_page_sizes();
         UDATA *pageFlags = j9vmem_supported_page_flags();
         largePageSize = pageSizes[0]; // Default page size is always the first element
         largePageFlags = pageFlags[0];

         UDATA preferredPageSize = 0;
         UDATA hugePreferredPageSize = 0;
         #if defined(TR_TARGET_POWER)
         preferredPageSize = 65536;
         #elif (defined(LINUX) && defined(TR_TARGET_X86))
         preferredPageSize = 2097152;
         hugePreferredPageSize = 0x40000000;
         #elif (defined(TR_TARGET_S390))
         preferredPageSize = 1048576;
         #endif
         if (0 != preferredPageSize)
            {
            for (UDATA pageIndex = 0; 0 != pageSizes[pageIndex]; ++pageIndex)
               {
               if ( (preferredPageSize == pageSizes[pageIndex] || hugePreferredPageSize == pageSizes[pageIndex])
               #if defined(J9ZOS390)
                     /* zOS doesn't support non-pageable large pages for JIT code cache. */
                     && (0 != (J9PORT_VMEM_PAGE_FLAG_PAGEABLE & pageFlags[pageIndex]))
               #endif
               )
                  {
                  largePageSize = pageSizes[pageIndex];
                  largePageFlags = pageFlags[pageIndex];
                  }
               }
            }
         }

      if (largePageSize > (0) && isNonNegativePowerOf2((int32_t)largePageSize))
         {
         jitConfig->largeCodePageSize = (int32_t)largePageSize;
         jitConfig->largeCodePageFlags = (int32_t)largePageFlags;
         }
      }

      return true;
   }

bool J9::Options::preProcessCodeCache(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   PORT_ACCESS_FROM_JAVAVM(vm);
   OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

   char *ccOption = "-Xcodecache";
   int32_t argIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, ccOption, 0);
   if (argIndex >= 0)
      {
      UDATA ccSize;
      GET_MEMORY_VALUE(argIndex, ccOption, ccSize);
      ccSize >>= 10;
      jitConfig->codeCacheKB = ccSize;
      }

   self()->preProcessCodeCacheIncreaseTotalSize(vm, jitConfig);

   self()->preProcessCodeCachePrintCodeCache(vm);

   if (!self()->preProcessCodeCacheXlpCodeCache(vm, jitConfig))
      {
         return false;
      }

   return true;
   }

void J9::Options::preProcessSamplingExpirationTime(J9JavaVM *vm)
   {
   char *samplingOption = "-XsamplingExpirationTime";
   int32_t argIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, samplingOption, 0);
   if (argIndex >= 0)
      {
      UDATA expirationTime;
      IDATA ret = GET_INTEGER_VALUE(argIndex, samplingOption, expirationTime);
      if (ret == OPTION_OK)
         _samplingThreadExpirationTime = expirationTime;
      }
   }

void J9::Options::preProcessCompilationThreads(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
   TR::CompilationInfo *compInfo = getCompilationInfo(jitConfig);
   char *compThreadsOption = "-XcompilationThreads";
   int32_t argIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, compThreadsOption, 0);
   if (argIndex >= 0)
      {
      UDATA numCompThreads;
      IDATA ret = GET_INTEGER_VALUE(argIndex, compThreadsOption, numCompThreads);

      if (ret == OPTION_OK && numCompThreads > 0)
         {
         _numUsableCompilationThreads = numCompThreads;
         compInfo->updateNumUsableCompThreads(_numUsableCompilationThreads);
         }
      }
   }

void J9::Options::preProcessTLHPrefetch(J9JavaVM *vm)
   {
#if defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_S390) || defined(TR_HOST_ARM64)
   bool preferTLHPrefetch;
#if defined(TR_HOST_POWER)
   preferTLHPrefetch = TR::Compiler->target.cpu.isAtLeast(OMR_PROCESSOR_PPC_P6) && TR::Compiler->target.cpu.isAtMost(OMR_PROCESSOR_PPC_P7);
#elif defined(TR_HOST_S390)
   preferTLHPrefetch = true;
#elif defined(TR_HOST_ARM64)
   preferTLHPrefetch = true;
#else // TR_HOST_X86
   preferTLHPrefetch = true;
   // Disable TM on x86 because we cannot tell whether a Haswell chip supports TM or not, plus it's killing the performance on dayTrader3
   self()->setOption(TR_DisableTM);
#endif

   IDATA notlhPrefetch = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XnotlhPrefetch", 0);
   IDATA tlhPrefetch = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XtlhPrefetch", 0);
   if (preferTLHPrefetch)
      {
      if (notlhPrefetch <= tlhPrefetch)
         {
         self()->setOption(TR_TLHPrefetch);
         }
      }
   else
      {
      if (notlhPrefetch < tlhPrefetch)
         {
         self()->setOption(TR_TLHPrefetch);
         }
      }
#endif // defined(TR_HOST_X86) || defined(TR_HOST_POWER) || defined(TR_HOST_S390)
   }

void J9::Options::preProcessHwProfiler(J9JavaVM *vm)
   {
   // If the user didn't specifically ask for RI, let's enable it on some well defined platforms
   if (TR::Options::_hwProfilerEnabled == TR_maybe)
      {
#if defined (TR_HOST_S390)
      if (vm->runtimeFlags & J9_RUNTIME_TUNE_VIRTUALIZED)
         {
         TR::Options::_hwProfilerEnabled = TR_yes;
         }
      else
         {
         TR::Options::_hwProfilerEnabled = TR_no;
         }
#else
      TR::Options::_hwProfilerEnabled = TR_no;
#endif
      }

   // If RI is to be enabled, set other defaults as well
   if (TR::Options::_hwProfilerEnabled == TR_yes)
      {
      // Enable RI Based Recompilation by default
      self()->setOption(TR_EnableHardwareProfileRecompilation);

      // Disable the RI Reduced Warm Heuristic
      self()->setOption(TR_DisableHardwareProfilerReducedWarm);

#if (defined(TR_HOST_POWER) && defined(TR_HOST_64BIT))
#if defined(LINUX)
      // On Linux on Power downgrade compilations only when the compilation queue grows large
      self()->setOption(TR_UseRIOnlyForLargeQSZ);
#endif
      self()->setOption(TR_DisableHardwareProfilerDuringStartup);
#elif defined (TR_HOST_S390)
      self()->setOption(TR_DisableDynamicRIBufferProcessing);
#endif
      }
   }

void J9::Options::preProcessDeterministicMode(J9JavaVM *vm)
   {
   // Process the deterministic mode
   if (TR::Options::_deterministicMode == -1) // not yet set
      {
      char *deterministicOption = "-XX:deterministic=";
      const UDATA MAX_DETERMINISTIC_MODE = 9; // only levels 0-9 are allowed
      int32_t argIndex = FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, deterministicOption, 0);
      if (argIndex >= 0)
         {
         UDATA deterministicMode;
         IDATA ret = GET_INTEGER_VALUE(argIndex, deterministicOption, deterministicMode);
         if (ret == OPTION_OK && deterministicMode <= MAX_DETERMINISTIC_MODE)
            {
            TR::Options::_deterministicMode = deterministicMode;
            }
         }
      }
   }

bool J9::Options::preProcessJitServer(J9JavaVM *vm, J9JITConfig *jitConfig)
   {
#if defined(J9VM_OPT_JITSERVER)
   PORT_ACCESS_FROM_JAVAVM(vm);
   OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

   static bool JITServerAlreadyParsed = false;
   TR::CompilationInfo *compInfo = getCompilationInfo(jitConfig);
   if (!JITServerAlreadyParsed) // Avoid processing twice for AOT and JIT and produce duplicate messages
      {
      JITServerAlreadyParsed = true;
      if (vm->internalVMFunctions->isJITServerEnabled(vm))
         {
         J9::PersistentInfo::_remoteCompilationMode = JITServer::SERVER;
         // Increase the default timeout value for JITServer.
         // It can be overridden with -XX:JITServerTimeout= option in JITServerParseCommonOptions().
         compInfo->getPersistentInfo()->setSocketTimeout(DEFAULT_JITSERVER_TIMEOUT);
         }
      else
         {
         // Check option -XX:+UseJITServer
         // -XX:-UseJITServer disables JITServer at the client
         const char *xxUseJITServerOption = "-XX:+UseJITServer";
         const char *xxDisableUseJITServerOption = "-XX:-UseJITServer";

         int32_t xxUseJITServerArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxUseJITServerOption, 0);
         int32_t xxDisableUseJITServerArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxDisableUseJITServerOption, 0);

         // Check if option is at all specified
         if (xxUseJITServerArgIndex > xxDisableUseJITServerArgIndex)
            {
            J9::PersistentInfo::_remoteCompilationMode = JITServer::CLIENT;
            compInfo->getPersistentInfo()->setSocketTimeout(DEFAULT_JITCLIENT_TIMEOUT);

            // Check if the technology preview message should be displayed.
            const char *xxJITServerTechPreviewMessageOption = "-XX:+JITServerTechPreviewMessage";
            const char *xxDisableJITServerTechPreviewMessageOption = "-XX:-JITServerTechPreviewMessage";

            int32_t xxJITServerTechPreviewMessageArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxJITServerTechPreviewMessageOption, 0);
            int32_t xxDisableJITServerTechPreviewMessageArgIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, xxDisableJITServerTechPreviewMessageOption, 0);

            if (xxJITServerTechPreviewMessageArgIndex >= xxDisableJITServerTechPreviewMessageArgIndex)
               {
               j9tty_printf(PORTLIB, "JITServer is currently a technology preview. Its use is not yet supported\n");
               }

            const char *xxJITServerAddressOption = "-XX:JITServerAddress=";
            int32_t xxJITServerAddressArgIndex = FIND_ARG_IN_VMARGS(STARTSWITH_MATCH, xxJITServerAddressOption, 0);

            if (xxJITServerAddressArgIndex >= 0)
               {
               char *address = NULL;
               GET_OPTION_VALUE(xxJITServerAddressArgIndex, '=', &address);
               compInfo->getPersistentInfo()->setJITServerAddress(address);
               }
            }
         }
      if (!JITServerParseCommonOptions(vm, compInfo))
         {
         // Could not parse JITServer options successfully
         return false;
         }

      if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::CLIENT)
         {
         // Generate a random identifier for this JITServer instance.
         // Collisions are possible, but very unlikely.
         // Using more bits for the client UID can reduce the probability of a collision further.
         std::random_device rd;
         std::mt19937_64 rng(rd());
         std::uniform_int_distribution<uint64_t> dist;
         // clientUID != 0 when running in client mode
         uint64_t clientUID = dist(rng);
         while (0 == clientUID)
            clientUID = dist(rng);
         compInfo->getPersistentInfo()->setClientUID(clientUID);
         jitConfig->clientUID = clientUID;

         // _safeReservePhysicalMemoryValue is set as 0 for the JITClient because compilations
         // are done remotely. The user can still override it with a command line option
         J9::Options::_safeReservePhysicalMemoryValue = 0;
         }
      else
         {
         // clientUID == 0 when running in server mode / regular JVM
         compInfo->getPersistentInfo()->setClientUID(0);
         jitConfig->clientUID = 0;
         }
      }
#endif /* defined(J9VM_OPT_JITSERVER) */
   return true;
   }

bool
J9::Options::fePreProcess(void * base)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   J9JavaVM * vm = jitConfig->javaVM;

   PORT_ACCESS_FROM_JAVAVM(vm);
   OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

   #if defined(DEBUG) || defined(PROD_WITH_ASSUMES)
      bool forceSuffixLogs = false;
   #else
      bool forceSuffixLogs = true;
   #endif


  /* Using traps on z/OS for NullPointerException and ArrayIndexOutOfBound checks instead of the
   * old way of using explicit compare and branching off to a helper is causing several issues on z/OS:
   *
   * 1. When a trap fires because an exception needs to be thrown, the OS signal has to go through z/OS RTM
   * (Recovery Termination Management) and in doing so it gets serialized and has to acquire this CML lock.
   *  When many concurrent exceptions are thrown, this lock gets contention.
   *
   * 2. on z/OS Management Facility, disabling traps gave performance boost because of fewer exceptions from the JCL.
   * The path length on z/OS for a trap is longer than on other operating systems so the trade-off and
   * penalty for using traps on z/OS is significantly worse than other platforms. This gives the potential
   * for significant performance loss caused by traps.
   *
   * 3. Depending on sysadmin settings, every time a trap fires, the system may get a "system dump"
   * message which shows a 0C7 abend and with lots of exceptions being thrown this clutters up the syslog.
   *
   * 4. Certain products can get in the way of the JIT signal handler so the JIT doesn't
   * receive the 0C7 signal causing the product process to get killed
   *
   * Therefore, the recommendation is to disable traps on z/OS by default.
   * Users can choose to enable traps using the "enableTraps" option.
   */
   #if defined(J9ZOS390)
      self()->setOption(TR_DisableTraps);
   #endif

   if (self()->getOption(TR_AggressiveOpts))
      self()->setOption(TR_DontDowngradeToCold, true);

   if (forceSuffixLogs)
      self()->setOption(TR_EnablePIDExtension);

   if (jitConfig->runtimeFlags & J9JIT_CG_REGISTER_MAPS)
      self()->setOption(TR_RegisterMaps);

   jitConfig->tLogFile     = -1;
   jitConfig->tLogFileTemp = -1;

   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   uint32_t numProc = compInfo->getNumTargetCPUs();
   TR::Compiler->host.setNumberOfProcessors(numProc);
   TR::Compiler->target.setNumberOfProcessors(numProc);
   TR::Compiler->relocatableTarget.setNumberOfProcessors(numProc);

   self()->preProcessMmf(vm, jitConfig);

   if (FIND_ARG_IN_VMARGS(EXACT_MATCH, "-Xnoclassgc", 0) >= 0)
      self()->setOption(TR_NoClassGC);

   self()->preProcessMode(vm, jitConfig);

   self()->preProcessJniAccelerator(vm);

   if (!self()->preProcessCodeCache(vm, jitConfig))
      {
         return false;
      }

   self()->preProcessSamplingExpirationTime(vm);

   self()->preProcessCompilationThreads(vm, jitConfig);

   self()->preProcessTLHPrefetch(vm);

#if defined(TR_HOST_X86) || defined(TR_TARGET_POWER) || defined (TR_HOST_S390)
   self()->setOption(TR_ReservingLocks);
#endif

   self()->preProcessHwProfiler(vm);

#if defined (TR_HOST_S390)
   // On z Systems inlining very large compiled bodies proved to be worth a significant amount in terms of throughput
   // on benchmarks that we track. As such the throughput-compile-time tradeoff was significant enough to warrant the
   // inlining of very large compiled bodies on z Systems by default.
   if (vm->runtimeFlags & J9_RUNTIME_TUNE_VIRTUALIZED)
      {
      // Disable inlining of very large compiled methods only under -Xtune:virtualized
      self()->setOption(TR_InlineVeryLargeCompiledMethods, false);
      }
   else
      {
      self()->setOption(TR_InlineVeryLargeCompiledMethods);
      }
#endif

   // On big machines we can afford to spend more time compiling
   // (but not on zOS where customers care about CPU or on Xquickstart
   // which should be skimpy on compilation resources).
   // TR_SuspendEarly is set on zOS because test results indicate that
   // it does not benefit much by spending more time compiling.
#if !defined(J9ZOS390)
   if (!self()->isQuickstartDetected())
      {
      // Power uses a larger SMT than X or Z
      uint32_t largeNumberOfCPUs = TR::Compiler->target.cpu.isPower() ? 64 : 32;
      if (compInfo->getNumTargetCPUs() >= largeNumberOfCPUs)
         {
         self()->setOption(TR_ConcurrentLPQ);
         self()->setOption(TR_EarlyLPQ);
         TR::Options::_expensiveCompWeight = 99; // default 20
         TR::Options::_invocationThresholdToTriggerLowPriComp = 50; // default is 250
         TR::Options::_numIProfiledCallsToTriggerLowPriComp = 100; // default is 250
         TR::Options::_numDLTBufferMatchesToEagerlyIssueCompReq = 1;
         }
      }
#else
   self()->setOption(TR_SuspendEarly);
#endif

   // samplingJProfiling is disabled globally. It will be enabled on a method by
   // method basis based on selected heuristic
   self()->setDisabled(OMR::samplingJProfiling, true);

#if defined (TR_HOST_S390)
   // Disable lock reservation due to a functional problem causing a deadlock situation in an ODM workload in Java 8
   // SR5. In addition several performance issues on SPARK workloads have been reported which seem to concurrently
   // access StringBuffer objects from multiple threads.
   self()->setOption(TR_DisableLockResevation);
   // Setting number of onsite cache slots for instanceOf node to 4 on IBM Z
   self()->setMaxOnsiteCacheSlotForInstanceOf(4);
#endif
   // Set a value for _safeReservePhysicalMemoryValue that is proportional
   // to the amount of free physical memory at boot time
   // The user can still override it with a command line option
   bool incomplete = false;
   uint64_t phMemAvail = compInfo->computeAndCacheFreePhysicalMemory(incomplete);
   if (phMemAvail != OMRPORT_MEMINFO_NOT_AVAILABLE && !incomplete)
      {
      const uint64_t reserveLimit = 32 * 1024 * 1024;
      uint64_t proposed = phMemAvail >> 6; // 64 times less
      if (proposed > reserveLimit)
         proposed = reserveLimit;
      J9::Options::_safeReservePhysicalMemoryValue = (int32_t)proposed;
      }

   // enable TR_SelfTuningScratchMemoryUsageBeforeCompile if there is no swap memory
   J9MemoryInfo memInfo;
   if ((omrsysinfo_get_memory_info(&memInfo) == 0) && (0 == memInfo.totalSwap))
      {
      self()->setOption(TR_EnableSelfTuningScratchMemoryUsageBeforeCompile);
      }

   self()->preProcessDeterministicMode(vm);

   if (!TR::Compiler->target.cpu.isZ())
      self()->setOption(TR_DisableAOTBytesCompression);

   if (!self()->preProcessJitServer(vm, jitConfig))
      {
         return false;
      }

#if (defined(TR_HOST_X86) || defined(TR_HOST_S390) || defined(TR_HOST_POWER)) && defined(TR_TARGET_64BIT)
   self()->setOption(TR_EnableSymbolValidationManager);
#endif

   return true;
   }

static TR::FILE *fileOpen(TR::Options *options, J9JITConfig *jitConfig, char *name, char *permission, bool b1, bool b2)
   {
   PORT_ACCESS_FROM_ENV(jitConfig->javaVM);
   char tmp[1025];
   char *formattedTmp = NULL;
   if (!options->getOption(TR_EnablePIDExtension))
      {
      formattedTmp = TR_J9VMBase::getJ9FormattedName(jitConfig, PORTLIB, tmp, sizeof(tmp), name, NULL, false);
      }
   else
      {
      formattedTmp = TR_J9VMBase::getJ9FormattedName(jitConfig, PORTLIB, tmp, sizeof(tmp), name, options->getSuffixLogsFormat(), true);
      }
   if (NULL != formattedTmp)
      {
      return j9jit_fopen(formattedTmp, permission, b1, b2);
      }
   return NULL;
   }

void
J9::Options::openLogFiles(J9JITConfig *jitConfig)
   {
   char *vLogFileName = ((TR_JitPrivateConfig*)jitConfig->privateConfig)->vLogFileName;
   if (vLogFileName)
      ((TR_JitPrivateConfig*)jitConfig->privateConfig)->vLogFile = fileOpen(self(), jitConfig, vLogFileName, "wb", true, false);

   char *rtLogFileName = ((TR_JitPrivateConfig*)jitConfig->privateConfig)->rtLogFileName;
   if (rtLogFileName)
      ((TR_JitPrivateConfig*)jitConfig->privateConfig)->rtLogFile = fileOpen(self(), jitConfig, rtLogFileName, "wb", true, false);
   }

#if defined(J9VM_OPT_JITSERVER)
void
J9::Options::setupJITServerOptions()
   {
   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);

   if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER ||
       compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::CLIENT)
      {
      self()->setOption(TR_DisableSamplingJProfiling);
      self()->setOption(TR_DisableProfiling); // JITServer limitation, JIT profiling data is not available to remote compiles yet
      self()->setOption(TR_DisableEDO); // JITServer limitation, EDO counters are not relocatable yet
      self()->setOption(TR_DisableMethodIsCold); // Shady heuristic; better to disable to reduce client/server traffic
      self()->setOption(TR_DisableJProfilerThread);
      self()->setOption(TR_EnableJProfiling, false);

      if (compInfo->getPersistentInfo()->getRemoteCompilationMode() == JITServer::SERVER)
         {
         // The server can compile with VM access in hand because GC is not a factor here
         // For the same reason we don't have to use TR_EnableYieldVMAccess
         self()->setOption(TR_DisableNoVMAccess);

         // IProfiler thread is not needed at JITServer because
         // no IProfiler info is collected at the server itself
         self()->setOption(TR_DisableIProfilerThread);
         J9::Compilation::setOutOfProcessCompilation();
         }

      // In the JITServer world, expensive compilations are performed remotely so there is no risk of blowing the footprint limit on the JVM
      // Setting _expensiveCompWeight to a large value so that JSR292/hot/scorching compilation are allowed to be executed concurrently
      TR::Options::_expensiveCompWeight = TR::CompilationInfo::MAX_WEIGHT;
      }

   if (TR::Options::getVerboseOption(TR_VerboseJITServer))
      {
      JITServer::CommunicationStream::printJITServerVersion();
      TR::PersistentInfo *persistentInfo = compInfo->getPersistentInfo();
      if (persistentInfo->getRemoteCompilationMode() == JITServer::SERVER)
         {
         TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer, "JITServer Server Mode. Port: %d. Connection Timeout %ums",
               persistentInfo->getJITServerPort(), persistentInfo->getSocketTimeout());
         }
      else if (persistentInfo->getRemoteCompilationMode() == JITServer::CLIENT)
         {
         TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer, "JITServer Client Mode. Server address: %s port: %d. Connection Timeout %ums",
               persistentInfo->getJITServerAddress().c_str(), persistentInfo->getJITServerPort(),
               persistentInfo->getSocketTimeout());
         TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer, "Identifier for current client JVM: %llu\n",
               (unsigned long long) compInfo->getPersistentInfo()->getClientUID());
         }
      }
   }
#endif /* defined(J9VM_OPT_JITSERVER) */

bool
J9::Options::fePostProcessJIT(void * base)
   {
   // vmPostProcess is called indirectly from the JIT_INITIALIZED phase
   // vmLatePostProcess is called indirectly from the aboutToBootstrap hook
   //
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   J9JavaVM * javaVM = jitConfig->javaVM;
   PORT_ACCESS_FROM_JAVAVM(javaVM);

   TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
   // If user has not specified a value for compilation threads, do it now.
   // This code does not have to stay in the fePostProcessAOT because in an AOT only
   // scenario we don't need more than one compilation thread to load code.
   //
   if (_numUsableCompilationThreads <= 0)
      {
      _useCPUsToDetermineMaxNumberOfCompThreadsToActivate = true;
      if (TR::Compiler->target.isLinux())
         {
         // For linux we may want to create more threads to overcome thread
         // starvation due to lack of priorities
         //
         if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableRampupImprovements) &&
            !TR::Options::getAOTCmdLineOptions()->getOption(TR_DisableRampupImprovements))
            {
            compInfo->updateNumUsableCompThreads(_numUsableCompilationThreads);
            }
         }
      if (_numUsableCompilationThreads <= 0)
         {
         // Determine the number of compilation threads based on number of online processors
         // Do not create more than numProc-1 compilation threads, but at least one
         //
         uint32_t numOnlineCPUs = j9sysinfo_get_number_CPUs_by_type(J9PORT_CPU_ONLINE);
         compInfo->updateNumUsableCompThreads(_numUsableCompilationThreads);
         _numUsableCompilationThreads = numOnlineCPUs > 1 ? std::min((numOnlineCPUs - 1), static_cast<uint32_t>(_numUsableCompilationThreads)) : 1;
         }
      }

   // patch Lock OR if mfence is not being used
   //
   if (!self()->getOption(TR_X86UseMFENCE) && (jitConfig->runtimeFlags & J9JIT_PATCHING_FENCE_TYPE))
      jitConfig->runtimeFlags ^= J9JIT_PATCHING_FENCE_TYPE; // clear the bit

   // Main options
   //
   // Before we do anything, hack the flag field into place
   uint32_t flags = *(uint32_t *)(&jitConfig->runtimeFlags);
   jitConfig->runtimeFlags |= flags;

   if (jitConfig->runtimeFlags & J9JIT_TESTMODE || jitConfig->runtimeFlags & J9JIT_TOSS_CODE)
      self()->setOption(TR_DisableCompilationThread, true);

   if (jitConfig->runtimeFlags & J9JIT_RUNTIME_RESOLVE)
      jitConfig->gcOnResolveThreshold = 0;

   if (_samplingFrequency > MAX_SAMPLING_FREQUENCY/10000) // Cap the user specified sampling frequency to "max"/10000
      _samplingFrequency = MAX_SAMPLING_FREQUENCY/10000;  // Too large a value can make samplingPeriod
                                                          // negative when we multiply by loadFactor
   jitConfig->samplingFrequency = _samplingFrequency;

   // grab vLogFileName from jitConfig and put into jitPrivateConfig, where it will be found henceforth
   TR_JitPrivateConfig *privateConfig = (TR_JitPrivateConfig*)jitConfig->privateConfig;
   privateConfig->vLogFileName = jitConfig->vLogFileName;
   self()->openLogFiles(jitConfig);

   // Copy verbose flags from jitConfig into TR::Options static fields
   //
   TR::Options::setVerboseOptions(privateConfig->verboseFlags);

   if (TR::Options::getVerboseOption(TR_VerboseFilters))
      {
      if (TR::Options::getDebug() && TR::Options::getDebug()->getCompilationFilters())
         {
         TR_VerboseLog::writeLine(TR_Vlog_INFO,"JIT limit filters:");
         TR::Options::getDebug()->printFilters();
         }
      }

#if defined(J9VM_OPT_JITSERVER)
   self()->setupJITServerOptions();
#endif /* defined(J9VM_OPT_JITSERVER) */

   return true;
   }

bool
J9::Options::fePostProcessAOT(void * base)
   {
   J9JITConfig * jitConfig = (J9JITConfig*)base;
   J9JavaVM * javaVM = jitConfig->javaVM;
   PORT_ACCESS_FROM_JAVAVM(javaVM);

   self()->openLogFiles(jitConfig);

   if (TR::Options::getVerboseOption(TR_VerboseFilters))
      {
      if (TR::Options::getDebug() && TR::Options::getDebug()->getCompilationFilters())
         {
         TR_VerboseLog::writeLine(TR_Vlog_INFO,"AOT limit filters:");
         TR::Options::getDebug()->printFilters();
         }
      }

#if defined(J9VM_OPT_JITSERVER)
   self()->setupJITServerOptions();
#endif /* defined(J9VM_OPT_JITSERVER) */

   return true;
   }

bool J9::Options::feLatePostProcess(void * base, TR::OptionSet * optionSet)
   {
   // vmPostProcess is called indirectly from the JIT_INITIALIZED phase
   // vmLatePostProcess is called indirectly from the aboutToBootstrap hook
   //
   bool doAOT = true;
   if (optionSet)
      {
      // nothing option set specific to do
      return true;
      }

   J9JITConfig * jitConfig = (J9JITConfig*)base;
   J9JavaVM * javaVM = jitConfig->javaVM;
   J9HookInterface * * vmHooks = javaVM->internalVMFunctions->getVMHookInterface(javaVM);

   TR_J9VMBase * vm = TR_J9VMBase::get(jitConfig, 0);
   TR::CompilationInfo * compInfo = TR::CompilationInfo::get(jitConfig);

   // runtimeFlags are properly setup only in fePostProcessJit,
   // so for AOT we can properly set dependent options only here
   if (jitConfig->runtimeFlags & J9JIT_TESTMODE ||
       jitConfig->runtimeFlags & J9JIT_TOSS_CODE)
      self()->setOption(TR_DisableCompilationThread, true);

   PORT_ACCESS_FROM_JAVAVM(jitConfig->javaVM);
   if (vm->isAOT_DEPRECATED_DO_NOT_USE() || (jitConfig->runtimeFlags & J9JIT_TOSS_CODE))
      return true;

#if defined(J9VM_INTERP_ATOMIC_FREE_JNI) && !defined(TR_HOST_S390) && !defined(TR_HOST_POWER) && !defined(TR_HOST_X86) && !defined(TR_HOST_ARM64)
    // Atomic-free JNI dispatch needs codegen support, currently only prototyped on a few platforms
   setOption(TR_DisableDirectToJNI);
#endif

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
   // 31-64 Interop currently lacks codegen DirectToJNI support to invoke cross AMODE calls.
   // This is a temporarily measure to ensure functional correctness. At the least, we can
   // enable DirectToJNI for resolved JNI calls to 64-bit targets.
   if (J9_ARE_ALL_BITS_SET(javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_3164_INTEROPERABILITY)) {
      setOption(TR_DisableDirectToJNI);
   }
#endif

   // Determine whether or not to call the hooked helpers
   //
   if (
#if defined(J9VM_JIT_FULL_SPEED_DEBUG)
       (javaVM->requiredDebugAttributes & J9VM_DEBUG_ATTRIBUTE_CAN_ACCESS_LOCALS) ||
#endif
#if defined (J9VM_INTERP_HOT_CODE_REPLACEMENT)
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_POP_FRAMES_INTERRUPT) ||
#endif
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_BREAKPOINT) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_FRAME_POPPED) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_FRAME_POP) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_GET_FIELD) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_PUT_FIELD) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_GET_STATIC_FIELD) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_PUT_STATIC_FIELD) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_SINGLE_STEP))
      {
         static bool TR_DisableFullSpeedDebug = (feGetEnv("TR_DisableFullSpeedDebug") != NULL);
         static bool TR_DisableFullSpeedDebugAOT = (feGetEnv("TR_DisableFullSpeedDebugAOT") != NULL);
#if defined(J9VM_JIT_FULL_SPEED_DEBUG)
         if (TR_DisableFullSpeedDebug)
            {
            return false;
            }
         else if (TR_DisableFullSpeedDebugAOT)
            {
            doAOT = false;
            }

         self()->setOption(TR_FullSpeedDebug);
         self()->setOption(TR_DisableDirectToJNI);
         //setOption(TR_DisableNoVMAccess);
         //setOption(TR_DisableAsyncCompilation);
         //setOption(TR_DisableInterpreterProfiling, true);

         initializeFSD(javaVM);
#else
         return false;
#endif
      }

   bool exceptionEventHooked = false;
   if ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_EXCEPTION_CATCH))
      {
      jitConfig->jitExceptionCaught = jitExceptionCaught;
      exceptionEventHooked = true;
      }
   if ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_EXCEPTION_THROW))
      {
      exceptionEventHooked = true;
      }
   if (exceptionEventHooked)
      {
      self()->setOption(TR_DisableThrowToGoto);
      self()->setReportByteCodeInfoAtCatchBlock();
      }

   // Determine whether or not to generate method enter and exit hooks
   //
   if ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_METHOD_ENTER))
      {
      self()->setOption(TR_ReportMethodEnter);
#if !defined(TR_HOST_S390) && !defined(TR_HOST_POWER) && !defined(TR_HOST_X86)
      doAOT = false;
#endif
      }
   if ((*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_METHOD_RETURN))
      {
      self()->setOption(TR_ReportMethodExit);
#if !defined(TR_HOST_S390) && !defined(TR_HOST_POWER) && !defined(TR_HOST_X86)
      doAOT = false;
#endif
      }

   // Determine whether or not to disable allocation inlining
   //
   J9MemoryManagerFunctions * mmf = javaVM->memoryManagerFunctions;

   if (!mmf->j9gc_jit_isInlineAllocationSupported(javaVM))
      {
      self()->setOption(TR_DisableAllocationInlining);
      doAOT = false;
      }

#if 0
   // Determine whether or not to disable inlining
   //
   if ((TR::Options::getCmdLineOptions()->getOption(TR_ReportMethodEnter) ||
        TR::Options::getCmdLineOptions()->getOption(TR_ReportMethodExit)))
      {
      self()->setDisabled(inlining, true);
      printf("disabling inlining due to trace setting\n");
      doAOT = false;
      }
#endif

#if defined(J9VM_OPT_JITSERVER)
   // Change the values of the two thresholds if the user didn't
   // input values to the options.
   if (_veryHighActiveThreadThreshold == -1)
      _veryHighActiveThreadThreshold = getNumUsableCompilationThreads() * 0.9;

   if (_highActiveThreadThreshold == -1)
      _highActiveThreadThreshold = getNumUsableCompilationThreads() * 0.8;

   // Check if the user inputted the correct values.
   // If the thresholds are bigger than the usableCompilationThreads, Then set them to be equal to the number of compilation threads.
   if (_veryHighActiveThreadThreshold > getNumUsableCompilationThreads())
      _veryHighActiveThreadThreshold = getNumUsableCompilationThreads();
   if (_highActiveThreadThreshold > getNumUsableCompilationThreads())
      _highActiveThreadThreshold = getNumUsableCompilationThreads();
   if (_highActiveThreadThreshold  > _veryHighActiveThreadThreshold)
      _highActiveThreadThreshold  = _veryHighActiveThreadThreshold;

#endif /* defined(J9VM_OPT_JITSERVER) */

   // Determine whether or not to inline monitor enter/exit
   //
   if (self()->getOption(TR_DisableLiveMonitorMetadata))
      {
      self()->setOption(TR_DisableInlineMonEnt);
      self()->setOption(TR_DisableInlineMonExit);
      doAOT = false;
      }

   // If the VM -Xrs or -Xrs:sync option has been specified the user is requesting
   // that we remove signals. Set the noResumableTrapHandler option to note this
   // request.
   // Also disable the packed decimal part of DAA because some PD instructions
   // trigger hardware exceptions. A new option has been added to disable traps
   // Which allows the disabling of DAA and traps to be decoupled from the handler
   // Multiple variants of -Xrs option are available now:
   // -Xrs Ignore all signals (J9_SIG_XRS_SYNC, J9_SIG_XRS_ASYNC)
   // -Xrs:sync Ignore synchronous signals (J9_SIG_XRS_SYNC)
   // -Xrs:async Ignore asynchronous signals (J9_SIG_XRS_ASYNC)
   if (J9_ARE_ALL_BITS_SET(javaVM->sigFlags, J9_SIG_XRS_SYNC))
      {
      self()->setOption(TR_NoResumableTrapHandler);
      self()->setOption(TR_DisablePackedDecimalIntrinsics);
      self()->setOption(TR_DisableTraps);

      // Call initialize again to reset the flag as it will have been set on
      // in an earlier call
      vm->initializeHasResumableTrapHandler();
      }

   // The trap handler currently is working (jitt fails) on Ottawa's IA32 Hardhat machine.
   // The platform isn't shipping so the priority of fixing the problem is currently low.
   //
   #if defined(HARDHAT) && defined(TR_TARGET_X86)
      self()->setOption(TR_NoResumableTrapHandler);
   #endif

   // Determine whether or not to inline meta data maps have to represent every inline transition point
   //
   // The J9VM_DEBUG_ATTRIBUTE_MAINTAIN_FULL_INLINE_MAP is currently undefined in the Real Time builds.  Once
   // the real time VM line has been merged back into the dev line then these 3 preprocessor statements can
   // be removed
   //
   #ifndef J9VM_DEBUG_ATTRIBUTE_MAINTAIN_FULL_INLINE_MAP
      #define J9VM_DEBUG_ATTRIBUTE_MAINTAIN_FULL_INLINE_MAP 0
   #endif
   if (javaVM->requiredDebugAttributes & J9VM_DEBUG_ATTRIBUTE_MAINTAIN_FULL_INLINE_MAP)
      {
      self()->setOption(TR_GenerateCompleteInlineRanges);
      doAOT = false;
      }

   // If class redefinition is the only debug capability - FSD off, HCR on
   // If other capabilities are specified, such as break points - FSD on, HCR off
   static char *disableHCR = feGetEnv("TR_DisableHCR");
   if ((javaVM->requiredDebugAttributes & J9VM_DEBUG_ATTRIBUTE_CAN_REDEFINE_CLASSES) && !self()->getOption(TR_FullSpeedDebug))
      if (!self()->getOption(TR_EnableHCR) && !disableHCR)
         {
         self()->setOption(TR_EnableHCR);
         }

   // Check NextGenHCR is supported by the VM
   if (!(javaVM->extendedRuntimeFlags & J9_EXTENDED_RUNTIME_OSR_SAFE_POINT) ||
       (*vmHooks)->J9HookDisable(vmHooks, J9HOOK_VM_OBJECT_ALLOCATE_INSTRUMENTABLE) || disableHCR)
      {
      self()->setOption(TR_DisableNextGenHCR);
      }

#if !defined(TR_HOST_X86) && !defined(TR_HOST_S390) && !defined(TR_HOST_POWER)
   //The bit is set when -XX:+JITInlineWatches is specified
   if (J9_ARE_ANY_BITS_SET(javaVM->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_JIT_INLINE_WATCHES))
      TR_ASSERT_FATAL(false, "this platform doesn't support JIT inline field watch");
#endif

   // GCR and JProfiling are disabled under FSD for a number of reasons
   // First, there is confusion between the VM and the JIT as to whether a call to jitRetranslateCallerWithPreparation is a decompilation point.
   // Having the JIT agree with the VM could not be done by simply marking the symbol canGCandReturn, as this would affect other parts of the JIT
   // Second, in a sync method this call will occur before the temporary sync object has been stored to its temp
   // This results in the sync object not being available when the VM calls the buffer filling code
   //
   if (self()->getOption(TR_FullSpeedDebug))
      {
      self()->setReportByteCodeInfoAtCatchBlock();
      self()->setOption(TR_DisableGuardedCountingRecompilations);
      self()->setOption(TR_EnableJProfiling, false);
      //might move around asyn checks and clone the OSRBlock which are not safe under the current OSR infrastructure
      self()->setOption(TR_DisableProfiling);
      //the VM side doesn't support fsd for this event yet
      self()->setOption(TR_DisableNewInstanceImplOpt);
      //the following 2 opts might insert async checks at new bytecode index where the OSR infrastructures doesn't exist
      self()->setDisabled(OMR::redundantGotoElimination, true);
      self()->setDisabled(OMR::loopReplicator, true);
      }

#if defined(J9VM_OPT_SHARED_CLASSES)
   if (TR::Options::sharedClassCache())
      {
      if (!doAOT)
         {
         if (this == TR::Options::getAOTCmdLineOptions())
            {
            TR::Options::getAOTCmdLineOptions()->setOption(TR_NoLoadAOT);
            TR::Options::getAOTCmdLineOptions()->setOption(TR_NoStoreAOT);
            TR::Options::setSharedClassCache(false);
            if (javaVM->sharedClassConfig->verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE)
               j9nls_printf( PORTLIB, J9NLS_WARNING,  J9NLS_RELOCATABLE_CODE_NOT_AVAILABLE_WITH_FSD_JVMPI);
            }
         }
      else // do AOT
         {
         if (!self()->getOption(TR_DisablePersistIProfile))
            {
            // Turn off Iprofiler for the warm runs, but not if we cache only bootstrap classes
            // This is because we may be missing IProfiler information for non-bootstrap classes
            // that could not be stored in SCC
            if (J9_ARE_ALL_BITS_SET(javaVM->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES))
               {
               TR::CompilationInfo * compInfo = getCompilationInfo(jitConfig);
               static char * dnipdsp = feGetEnv("TR_DisableNoIProfilerDuringStartupPhase");
               if (compInfo->isWarmSCC() == TR_yes && !dnipdsp)
                  {
                  self()->setOption(TR_NoIProfilerDuringStartupPhase);
                  }
               }
            }
         }
      }
#endif

   // Divide by 0 checks
   if (TR::Options::_LoopyMethodDivisionFactor == 0)
      TR::Options::_LoopyMethodDivisionFactor = 16; // Reset it back to the default value
   if (TR::Options::_IprofilerOffDivisionFactor == 0)
      TR::Options::_IprofilerOffDivisionFactor = 16; // Reset it back to the default value

   // Some options consistency fixes for 2 options objects
   //
   if ((TR::Options::getAOTCmdLineOptions()->getFixedOptLevel() != -1) && (TR::Options::getJITCmdLineOptions()->getFixedOptLevel() == -1))
      TR::Options::getJITCmdLineOptions()->setFixedOptLevel(TR::Options::getAOTCmdLineOptions()->getFixedOptLevel());
   if ((TR::Options::getJITCmdLineOptions()->getFixedOptLevel() != -1) && (TR::Options::getAOTCmdLineOptions()->getFixedOptLevel() == -1))
      TR::Options::getAOTCmdLineOptions()->setFixedOptLevel(TR::Options::getJITCmdLineOptions()->getFixedOptLevel());

   if (compInfo->getPersistentInfo()->isRuntimeInstrumentationRecompilationEnabled())
      {
      if (!TR::Options::getCmdLineOptions()->getOption(TR_EnableJitSamplingUpgradesDuringHWProfiling))
         {
         TR::Options::getCmdLineOptions()->setOption(TR_DisableUpgrades);
         }

      // Under RI based recompilation, disable GCR recompilation
      TR::Options::getCmdLineOptions()->setOption(TR_DisableGuardedCountingRecompilations);
      TR::Options::getAOTCmdLineOptions()->setOption(TR_DisableGuardedCountingRecompilations);

      // If someone enabled the mechanism to turn off RI during steady state,
      // we must disable the mechanism that toggle RI on/off dynamically
      if (self()->getOption(TR_InhibitRIBufferProcessingDuringDeepSteady))
         self()->setOption(TR_DisableDynamicRIBufferProcessing);
      }

   // If the user or JIT heuristics (Xquickstart) want us to restrict the inlining during startup
   // now it's time to set the flag that enables the feature
   if (self()->getOption(TR_RestrictInlinerDuringStartup))
      {
      compInfo->getPersistentInfo()->setInlinerTemporarilyRestricted(true);
      }

   // If we don't want to collect any type of information from samplingJProfiling
   // the disable the opt completely
   if (!TR::Options::getCmdLineOptions()->isAnySamplingJProfilingOptionSet())
      self()->setOption(TR_DisableSamplingJProfiling);

#if defined(J9VM_JIT_DYNAMIC_LOOP_TRANSFER)
   if (!compInfo->getDLT_HT() && TR::Options::_numDLTBufferMatchesToEagerlyIssueCompReq > 1)
      compInfo->setDLT_HT(new (PERSISTENT_NEW) DLTTracking(compInfo->getPersistentInfo()));
#endif
   // The exploitation of idle time is done by a tracking mechanism done
   // on the IProfiler thread. If this thread does not exist, then we
   // must turn this feature off to avoid allocating a useless hashtable
   TR_IProfiler *iProfiler = vm->getIProfiler();
   if (!iProfiler || !iProfiler->getIProfilerThread())
      self()->setOption(TR_UseIdleTime, false);

   // If NoResumableTrapHandler is set, disable packed decimal intrinsics inlining because
   // PD instructions exceptions can't be handled without the handler.
   // Add a new option to disable traps explicitly so DAA and trap instructions can be disabled separately
   // but are both covered by the NoResumableTrapHandler option
   if (self()->getOption(TR_NoResumableTrapHandler))
      {
      self()->setOption(TR_DisablePackedDecimalIntrinsics);
      self()->setOption(TR_DisableTraps);
      }

   // Take care of the main option TR_DisableIntrinsics and the two sub-options
   if (self()->getOption(TR_DisableIntrinsics))
      {
      self()->setOption(TR_DisableMarshallingIntrinsics);
      self()->setOption(TR_DisablePackedDecimalIntrinsics);
      }
   else if (self()->getOption(TR_DisableMarshallingIntrinsics) &&
            self()->getOption(TR_DisablePackedDecimalIntrinsics))
      {
      self()->setOption(TR_DisableIntrinsics);
      }

   /* Temporary (until SVM is the default)
    *
    * If the SVM is not enabled, use the old _coldUpgradeSampleThreshold
    * for AGGRESSIVE_AOT
    */
   if (!self()->getOption(TR_EnableSymbolValidationManager)
       && TR::Options::_aggressivenessLevel == TR::Options::AGGRESSIVE_AOT)
      {
      TR::Options::_coldUpgradeSampleThreshold = 10;
      }

#if defined(TR_HOST_ARM64)
   // ArraySet support is not available in AArch64 yet.
   // OpenJ9 issue #6443 tracks the work to enable.
   //
   self()->setOption(TR_DisableArraySetOpts);

   // EDO catch block profiling support is not available in AArch64 yet.
   // OpenJ9 issue #6538 tracks the work to enable.
   //
   self()->setOption(TR_DisableEDO);
#endif

   return true;
   }


void
J9::Options::printPID()
   {
   ((TR_J9VMBase *)_fe)->printPID();
   }

#if defined(J9VM_OPT_JITSERVER)
void getTRPID(char *buf);

static void
appendRegex(TR::SimpleRegex *&regexPtr, uint8_t *&curPos)
   {
   if (!regexPtr)
      return;
   size_t len = regexPtr->regexStrLen();
   memcpy(curPos, regexPtr->regexStr(), len);
   TR_ASSERT(curPos[len - 1], "not expecting null terminator");
   // Replace the regex pointer with self-referring-pointer which is the offset of regex data
   // with respect to the regex pointer
   regexPtr = (TR::SimpleRegex*) ((uint8_t*)curPos - (uint8_t *)&regexPtr);
   curPos[len] = '\0';
   curPos += len + 1;
   }

static void
unpackRegex(TR::SimpleRegex *&regexPtr)
   {
   if (!regexPtr)
      return;
   char *str = (char*)((uintptr_t)&regexPtr + (uintptr_t)regexPtr);
   regexPtr = TR::SimpleRegex::create(str);
   }

static void
addRegexStringSize(TR::SimpleRegex *regexPtr, size_t &len)
   {
   if (regexPtr)
      len += regexPtr->regexStrLen() + 1;
   }

static uint8_t *
appendContent(char * &charPtr, uint8_t * curPos, size_t length)
   {
   if (charPtr == NULL)
      return curPos;
   // Copy charPtr's content to the location pointed by curPos
   memcpy(curPos, charPtr, length);
   // Compute the offset from the address of charPtr to curPos and store it to charPtr
   charPtr = (char *)(curPos - (uint8_t *)&(charPtr));
   // Update current position
   return curPos += length;
   }

// Pack a TR::Options object into a std::string to be transfered to the server
std::string
J9::Options::packOptions(const TR::Options *origOptions)
   {
   size_t logFileNameLength = 0;
   size_t suffixLogsFormatLength = 0;
   size_t blockShufflingSequenceLength = 0;
   size_t induceOSRLength = 0;

   char buf[JITSERVER_LOG_FILENAME_MAX_SIZE];
   char *origLogFileName = NULL;
   if (origOptions->_logFileName)
      {
      origLogFileName = origOptions->_logFileName;
      char pidBuf[20];
      memset(pidBuf, 0, 20);
      getTRPID(pidBuf);
      logFileNameLength = strlen(origOptions->_logFileName) + strlen(".") + strlen(pidBuf) + strlen(".server") + 1;
      // If logFileNameLength is greater than JITSERVER_LOG_FILENAME_MAX_SIZE, PID might not be appended to the log file name
      // and the log file name could be truncated as well.
      if (logFileNameLength > JITSERVER_LOG_FILENAME_MAX_SIZE)
         logFileNameLength = JITSERVER_LOG_FILENAME_MAX_SIZE;
      snprintf(buf, logFileNameLength, "%s.%s.server", origOptions->_logFileName, pidBuf);
      }
   if (origOptions->_suffixLogsFormat)
      suffixLogsFormatLength = strlen(origOptions->_suffixLogsFormat) + 1;
   if (origOptions->_blockShufflingSequence)
      blockShufflingSequenceLength = strlen(origOptions->_blockShufflingSequence) + 1;
   if (origOptions->_induceOSR)
      induceOSRLength = strlen(origOptions->_induceOSR) + 1;

   // sizeof(bool) is reserved to pack J9JIT_RUNTIME_RESOLVE
   size_t totalSize = sizeof(TR::Options) + logFileNameLength + suffixLogsFormatLength + blockShufflingSequenceLength + induceOSRLength + sizeof(bool);

   addRegexStringSize(origOptions->_disabledOptTransformations, totalSize);
   addRegexStringSize(origOptions->_disabledInlineSites, totalSize);
   addRegexStringSize(origOptions->_disabledOpts, totalSize);
   addRegexStringSize(origOptions->_optsToTrace, totalSize);
   addRegexStringSize(origOptions->_dontInline, totalSize);
   addRegexStringSize(origOptions->_onlyInline, totalSize);
   addRegexStringSize(origOptions->_tryToInline, totalSize);
   addRegexStringSize(origOptions->_slipTrap, totalSize);
   addRegexStringSize(origOptions->_lockReserveClass, totalSize);
   addRegexStringSize(origOptions->_breakOnOpts, totalSize);
   addRegexStringSize(origOptions->_breakOnCreate, totalSize);
   addRegexStringSize(origOptions->_debugOnCreate, totalSize);
   addRegexStringSize(origOptions->_breakOnThrow, totalSize);
   addRegexStringSize(origOptions->_breakOnPrint, totalSize);
   addRegexStringSize(origOptions->_enabledStaticCounterNames, totalSize);
   addRegexStringSize(origOptions->_enabledDynamicCounterNames, totalSize);
   addRegexStringSize(origOptions->_counterHistogramNames, totalSize);
   addRegexStringSize(origOptions->_verboseOptTransformationsRegex, totalSize);
   addRegexStringSize(origOptions->_packedTest, totalSize);
   addRegexStringSize(origOptions->_memUsage, totalSize);
   addRegexStringSize(origOptions->_classesWithFolableFinalFields, totalSize);
   addRegexStringSize(origOptions->_disabledIdiomPatterns, totalSize);

   std::string optionsStr(totalSize, '\0');
   TR::Options * options = (TR::Options *)optionsStr.data();
   memcpy(options, origOptions, sizeof(TR::Options));

   if (origOptions->_logFileName)
      options->_logFileName = buf;

   uint8_t *curPos = ((uint8_t *)options) + sizeof(TR::Options);

   options->_optionSets = NULL;
   options->_startOptions = NULL;
   options->_envOptions = NULL;
   options->_logFile = NULL;
   options->_optFileName = NULL;
   options->_customStrategy = NULL;
   options->_customStrategySize = 0;
   options->_countString = NULL;
   appendRegex(options->_disabledOptTransformations, curPos);
   appendRegex(options->_disabledInlineSites, curPos);
   appendRegex(options->_disabledOpts, curPos);
   appendRegex(options->_optsToTrace, curPos);
   appendRegex(options->_dontInline, curPos);
   appendRegex(options->_onlyInline, curPos);
   appendRegex(options->_tryToInline, curPos);
   appendRegex(options->_slipTrap, curPos);
   appendRegex(options->_lockReserveClass, curPos);
   appendRegex(options->_breakOnOpts, curPos);
   appendRegex(options->_breakOnCreate, curPos);
   appendRegex(options->_debugOnCreate, curPos);
   appendRegex(options->_breakOnThrow, curPos);
   appendRegex(options->_breakOnPrint, curPos);
   appendRegex(options->_enabledStaticCounterNames, curPos);
   appendRegex(options->_enabledDynamicCounterNames, curPos);
   appendRegex(options->_counterHistogramNames, curPos);
   appendRegex(options->_verboseOptTransformationsRegex, curPos);
   appendRegex(options->_packedTest, curPos);
   appendRegex(options->_memUsage, curPos);
   appendRegex(options->_classesWithFolableFinalFields, curPos);
   appendRegex(options->_disabledIdiomPatterns, curPos);
   options->_osVersionString = NULL;
   options->_logListForOtherCompThreads = NULL;
   options->_objectFileName = NULL;

   // Append the data pointed by a pointer to the content and patch the pointer
   // as a self-referring-pointer, or a relative pointer, which is
   // the offset of the data with respect to the pointer.
   curPos = appendContent(options->_logFileName, curPos, logFileNameLength);
   curPos = appendContent(options->_suffixLogsFormat, curPos, suffixLogsFormatLength);
   curPos = appendContent(options->_blockShufflingSequence, curPos, blockShufflingSequenceLength);
   curPos = appendContent(options->_induceOSR, curPos, induceOSRLength);

   // Send rtResolve option to the server:
   // Temporary solution until we can send jitConfig->runtimeFlags to the server
   // or make rtResolve part of TR::Options instead of runtime flags
   auto *jitConfig = (J9JITConfig *) _feBase;
   bool rtResolve = jitConfig->runtimeFlags & J9JIT_RUNTIME_RESOLVE;
   char *rtResolveStr = (char *) &rtResolve;
   curPos = appendContent(rtResolveStr, curPos, sizeof(bool));

   return optionsStr;
   }

TR::Options *
J9::Options::unpackOptions(char *clientOptions, size_t clientOptionsSize, TR::CompilationInfoPerThreadBase* compInfoPT, TR_J9VMBase *fe, TR_Memory *trMemory)
   {
   TR::Options *options = (TR::Options *)trMemory->allocateHeapMemory(clientOptionsSize);
   memcpy(options, clientOptions, clientOptionsSize);

   // Convert relative pointers to absolute pointers
   // pointer = address of field + offset
   if (options->_logFileName)
      options->_logFileName = (char *)((uint8_t *)&(options->_logFileName) + (ptrdiff_t)options->_logFileName);
   if (options->_suffixLogsFormat)
      options->_suffixLogsFormat = (char *)((uint8_t *)&(options->_suffixLogsFormat) + (ptrdiff_t)options->_suffixLogsFormat);
   if (options->_blockShufflingSequence)
      options->_blockShufflingSequence = (char *)((uint8_t *)&(options->_blockShufflingSequence) + (ptrdiff_t)options->_blockShufflingSequence);
   if (options->_induceOSR)
      options->_induceOSR = (char *)((uint8_t *)&(options->_induceOSR) + (ptrdiff_t)options->_induceOSR);

   // Receive rtResolve: J9JIT_RUNTIME_RESOLVE
   // NOTE: This relies on rtResolve being the last option in clientOptions
   // the J9JIT_RUNTIME_RESOLVE flag from JITClient
   // On JITServer, we store this value for each client in ClientSessionData
   bool rtResolve = (bool) *((uint8_t *) options + clientOptionsSize - sizeof(bool));
   compInfoPT->getClientData()->setRtResolve(rtResolve);
   unpackRegex(options->_disabledOptTransformations);
   unpackRegex(options->_disabledInlineSites);
   unpackRegex(options->_disabledOpts);
   unpackRegex(options->_optsToTrace);
   unpackRegex(options->_dontInline);
   unpackRegex(options->_onlyInline);
   unpackRegex(options->_tryToInline);
   unpackRegex(options->_slipTrap);
   unpackRegex(options->_lockReserveClass);
   unpackRegex(options->_breakOnOpts);
   unpackRegex(options->_breakOnCreate);
   unpackRegex(options->_debugOnCreate);
   unpackRegex(options->_breakOnThrow);
   unpackRegex(options->_breakOnPrint);
   unpackRegex(options->_enabledStaticCounterNames);
   unpackRegex(options->_enabledDynamicCounterNames);
   unpackRegex(options->_counterHistogramNames);
   unpackRegex(options->_verboseOptTransformationsRegex);
   unpackRegex(options->_packedTest);
   unpackRegex(options->_memUsage);
   unpackRegex(options->_classesWithFolableFinalFields);
   unpackRegex(options->_disabledIdiomPatterns);

   return options;
   }

// Pack the log file generated at the server to be sent to the client
std::string
J9::Options::packLogFile(TR::FILE *fp)
   {
   if (fp == NULL)
      return "";
   const size_t BUFFER_SIZE = 4096; // 4KB
   char buf[BUFFER_SIZE + 1];
   std::string logFileStr("");
   int readSize = 0;
   ::rewind(fp->_stream);
   do {
      readSize = ::fread(buf, 1, BUFFER_SIZE, fp->_stream);
      buf[readSize] = '\0';
      logFileStr.append(buf);
      } while (readSize == BUFFER_SIZE);

   logFileStr.append("</jitlog>\n");
   return logFileStr;
   }

// Create a log file at the client based on the log file string sent from the server
int
J9::Options::writeLogFileFromServer(const std::string& logFileContent)
   {
   if (logFileContent.empty() || !_logFileName)
      return 0;

   char buf[JITSERVER_LOG_FILENAME_MAX_SIZE];
   _fe->acquireLogMonitor();
   snprintf(buf, sizeof(buf), "%s.%d.REMOTE", _logFileName, ++_compilationSequenceNumber);
   int sequenceNumber = _compilationSequenceNumber;
   _fe->releaseLogMonitor();

   int32_t len = strlen(buf);
   // maximum length for suffix (dot + 8-digit date + dot + 6-digit time + dot + 5-digit pid + null terminator)
   int32_t MAX_SUFFIX_LENGTH = 23;
   if (len + MAX_SUFFIX_LENGTH > JITSERVER_LOG_FILENAME_MAX_SIZE)
      {
      if (TR::Options::getVerboseOption(TR_VerboseJITServer))
         {
         TR_VerboseLog::writeLineLocked(TR_Vlog_JITServer, "Trace log not generated due to filename being too long");
         }
      return 0; // may overflow the buffer
      }
   char tmp[JITSERVER_LOG_FILENAME_MAX_SIZE];
   char * filename = _fe->getFormattedName(tmp, JITSERVER_LOG_FILENAME_MAX_SIZE, buf, _suffixLogsFormat, true);

   TR::FILE *logFile = trfopen(filename, "wb", false);
   ::fputs(logFileContent.c_str(), logFile->_stream);
   trfflush(logFile);
   trfclose(logFile);

   return sequenceNumber;
   }

TR_Debug *createDebugObject(TR::Compilation *);

// JITServer: Create a log file for each client compilation request
// Side effect: set _logFile
// At the client: Triggered when a remote compilation is followed by a local compilation.
//                suffixNumber is the compilationSequenceNumber used for the remote compilation.
// At the server: suffixNumber is set as 0.
void
J9::Options::setLogFileForClientOptions(int suffixNumber)
   {
   if (_logFileName)
      {
      _fe->acquireLogMonitor();
      if (suffixNumber)
         {
         self()->setOption(TR_EnablePIDExtension, true);
         self()->openLogFile(suffixNumber);
         }
      else
         {
         _compilationSequenceNumber++;
         self()->setOption(TR_EnablePIDExtension, false);
         self()->openLogFile(_compilationSequenceNumber);
         }

      if (_logFile)
         {
         J9JITConfig *jitConfig = (J9JITConfig*)_feBase;
         if (!jitConfig->tracingHook)
            {
            jitConfig->tracingHook = (void*) (TR_CreateDebug_t)createDebugObject;
            suppressLogFileBecauseDebugObjectNotCreated(false);
            _hasLogFile = true;
            }
         }
      _fe->releaseLogMonitor();
      }
   }

void
J9::Options::closeLogFileForClientOptions()
   {
   if (_logFile)
      {
      TR::Options::closeLogFile(_fe, _logFile);
      _logFile = NULL;
      }
   }
#endif /* defined(J9VM_OPT_JITSERVER) */

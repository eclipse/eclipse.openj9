
/*******************************************************************************
 * Copyright (c) 1991, 2021 IBM Corp. and others
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
 
/**
 * @file
 * @ingroup GC_Modron_Startup
 */

static const char *versionString = "Modron $Name: not supported by cvs2svn $";

#include "j9.h"
#include "j9cfg.h"
#include "j9protos.h"
#include "j9consts.h"
#include "jni.h"
#include "jvminit.h"
#include "j9port.h"
#include "modronnls.h"
#include "gcutils.h"

#include "mmparse.h"

#include "GCExtensions.hpp"
#include "Math.hpp"

/**
 * Consume arguments found in the -Xgc: (gc_colon) argument list.
 * @params scan_start address of the pointer to the string to parse
 * @return 0 if there was an error parsing
 * @return 1 if parsing was successful
 * @return 2 if the option was not handled
 */
static UDATA
j9gc_initialize_parse_gc_colon(J9JavaVM *javaVM, char **scan_start)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	PORT_ACCESS_FROM_JAVAVM(javaVM);

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)
	if(try_scan(scan_start, "tlhInitialSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhInitialSize, "tlhInitialSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "tlhMinimumSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhMinimumSize, "tlhMinimumSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "tlhMaximumSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhMaximumSize, "tlhMaximumSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "tlhIncrementSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhIncrementSize, "tlhIncrementSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "tlhSurvivorDiscardThreshold=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhSurvivorDiscardThreshold, "tlhSurvivorDiscardThreshold=")) {
			goto _error;
		}
		goto _exit;
	}	
	if(try_scan(scan_start, "tlhTenureDiscardThreshold=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->tlhTenureDiscardThreshold, "tlhTenureDiscardThreshold=")) {
			goto _error;
		}
		goto _exit;
	}

#endif /* defined(J9VM_GC_THREAD_LOCAL_HEAP) */
#if defined(J9VM_GC_SEGREGATED_HEAP)
	if(try_scan(scan_start, "allocationCacheMinimumSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->allocationCacheMinimumSize, "allocationCacheMinimumSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "allocationCacheMaximumSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->allocationCacheMaximumSize, "allocationCacheMaximumSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "allocationCacheInitialSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->allocationCacheInitialSize, "allocationCacheInitialSize=")) {
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "allocationCacheIncrementSize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->allocationCacheIncrementSize, "allocationCacheMaximumSize=")) {
			goto _error;
		}
		goto _exit;
	}	
#endif /* defined(J9VM_GC_SEGREGATED_HEAP) */


#if defined(J9VM_GC_REALTIME)
	if (try_scan(scan_start, "synchronousGCOnOOM")) {
		extensions->synchronousGCOnOOM = true;
		goto _exit;
	}		

	if (try_scan(scan_start, "noSynchronousGCOnOOM")) {
		extensions->synchronousGCOnOOM = false;
		goto _exit;
	}		
	if (try_scan(scan_start, "targetUtilization=")) {
		if(!scan_udata_helper(javaVM, scan_start, &(extensions->targetUtilizationPercentage), "targetUtilization=")) {
			goto _error;
		}
		if ((extensions->targetUtilizationPercentage < 1) || (99 < extensions->targetUtilizationPercentage)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "targetUtilization=", (UDATA)1, (UDATA)99);
			goto _error;
		}		
		goto _exit;
	}
	if (try_scan(scan_start, "threads=")) {
		if(!scan_udata_helper(javaVM, scan_start, &(extensions->gcThreadCount), "threads=")) {
			goto _error;
		}

		if(0 == extensions->gcThreadCount) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "threads=", (UDATA)0);
			goto _error;
		}

		extensions->gcThreadCountForced = true;
		goto _exit;
	}		
	if(try_scan(scan_start, "noClassGC")) {
		/* Metronome currently does not unload classes */
		/* We do not care if this option is set or not, but if it is, it is just silently parsed */
		goto _exit;
	}

	if (try_scan(scan_start, "overrideHiresTimerCheck")) {
		extensions->overrideHiresTimerCheck = true;
		goto _exit;
	}

#endif /* J9VM_GC_REALTIME */

#if defined(J9VM_GC_REALTIME)|| defined(J9VM_GC_VLHGC)
	if (try_scan(scan_start, "targetPausetime=")) {
		/* the unit of target pause time option is in milliseconds */
		UDATA beatMilli = 0;
		if(!scan_udata_helper(javaVM, scan_start, &beatMilli, "targetPausetime=")) {
			goto _error;
		}
		if(0 == beatMilli) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "targetPausetime=", (UDATA)0);
			goto _error;
		}

#if defined(J9VM_GC_REALTIME)
			/* convert the unit to microseconds and store in extensions */
			extensions->beatMicro = beatMilli * 1000;
#endif /* J9VM_GC_REALTIME */
		
#if defined(J9VM_GC_VLHGC)
		    /* Save soft pause target for balanced */
			extensions->tarokTargetMaxPauseTime = beatMilli;
#endif /* J9VM_GC_VLHGC */

		goto _exit;
	}
#endif /* J9VM_GC_REALTIME || J9VM_GC_VLHGC */

//todo temporary option to allow LOA to be enabled for testing with non-default gc policies
//Remove once LOA code stable 
#if defined(J9VM_GC_LARGE_OBJECT_AREA)

	if(try_scan(scan_start, "largeObjectMinimumSize=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "largeObjectMinimumSize=")) {
			goto _error;
		}
		
		extensions->largeObjectMinimumSize = MM_Math::roundToCeiling(extensions->heapAlignment, value);
		
		goto _exit;
	}
	
	if(try_scan(scan_start, "debugLOAFreelist")) {
		extensions->debugLOAFreelist = true;
		goto _exit;
	}
	
	if(try_scan(scan_start, "debugLOAAllocate")) {
		extensions->debugLOAAllocate = true;
		goto _exit;
	}
	
#endif /* J9VM_GC_LARGE_OBJECT_AREA) */	


	if(try_scan(scan_start, "threadCount=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->gcThreadCount, "threadCount=")) {
			goto _error;
		}
		
		if(0 == extensions->gcThreadCount) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "threadCount=", (UDATA)0);
			goto _error;
		}

		extensions->gcThreadCountForced = true;
		goto _exit;
	}

	if (try_scan(scan_start, "snapshotAtTheBeginningBarrier")) {
		extensions->configurationOptions._forceOptionWriteBarrierSATB = true;
		goto _exit;
	}

#if defined(J9VM_GC_MODRON_SCAVENGER)

	if(try_scan(scan_start, "tenureBytesDeviationBoost=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "tenureBytesDeviationBoost=")) {
			goto _error;
		}

		extensions->tenureBytesDeviationBoost = value / (float)10.0; 
		goto _exit;
	}

	if(try_scan(scan_start, "scavenge")) {
		extensions->configurationOptions._forceOptionScavenge = true;
		extensions->scavengerEnabled = true;
		goto _exit;
	}

	if(try_scan(scan_start, "noScavenge")) {
		extensions->configurationOptions._forceOptionScavenge = true;
		extensions->scavengerEnabled = false;
		goto _exit;
	}

	if(try_scan(scan_start, "concurrentKickoffTenuringHeadroom=")) {
		UDATA value = 0;
		if(!scan_udata_helper(javaVM, scan_start, &value, "concurrentKickoffTenuringHeadroom=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "concurrentKickoffTenuringHeadroom=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->concurrentKickoffTenuringHeadroom = ((float)value) / 100.0f;
		goto _exit;
	}


#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	/* Parsing of concurrentScavengeBackground/Slack must happen before concurrentScavenge, since the later option is a substring of the former(s).
	 * However, there is no effective limitation on relative order of these options in a command line. */
	if(try_scan(scan_start, "concurrentScavengeSlack=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->concurrentScavengerSlack, "concurrentScavengeSlack=")) {
			goto _error;
		}
		goto _exit;
	}

	if(try_scan(scan_start, "concurrentScavengeAllocDeviationBoost=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "concurrentScavengeAllocDeviationBoost=")) {
			goto _error;
		}

		extensions->concurrentScavengerAllocDeviationBoost = value / (float)10.0;
		goto _exit;
	}

	if(try_scan(scan_start, "concurrentScavengeBackground=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->concurrentScavengerBackgroundThreads, "concurrentScavengeBackground=")) {
			goto _error;
		}
		extensions->concurrentScavengerBackgroundThreadsForced = true;
		goto _exit;
	}
	/* Must be parsed after concurrentScavengeBackground/Slack. */
	if(try_scan(scan_start, "concurrentScavenge")) {
		extensions->concurrentScavengerForced = true;
		goto _exit;
	}

	if(try_scan(scan_start, "noConcurrentScavenge")) {
		extensions->concurrentScavengerForced = false;
		goto _exit;
	}
#endif /* OMR_GC_CONCURRENT_SCAVENGER */

	if(try_scan(scan_start, "failedTenureThreshold=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->scavengerFailedTenureThreshold, "failedTenureThreshold=")) {
			goto _error;	
		}	
		goto _exit;
	}
	
	if(try_scan(scan_start, "maxScavengeBeforeGlobal=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->maxScavengeBeforeGlobal, "maxScavengeBeforeGlobal=")) {
			goto _error;	
		}
		goto _exit;
	}
	
	if(try_scan(scan_start, "scvCollectorExpandRatio=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvCollectorExpandRatio=")) {
			goto _error;
		}
		if((0 == value) || (100 < value)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvCollectorExpandRatio=", (UDATA)1, (UDATA)100);
		}

		extensions->scavengerCollectorExpandRatio = value / (double)100.0;
		goto _exit;
	}
	
	if(try_scan(scan_start, "scvMaximumCollectorExpandSize=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvMaximumCollectorExpandSize=")) {
			goto _error;
		}
		
		extensions->scavengerMaximumCollectorExpandSize = MM_Math::roundToCeiling(extensions->heapAlignment, value);
		goto _exit;
	}
	
	if (try_scan(scan_start, "scvTenureStrategy=")) {
		/* Reset all tenure strategies because we will be setting them explicitly now. */
		extensions->scvTenureStrategyFixed = false;
		extensions->scvTenureStrategyAdaptive = false;
		extensions->scvTenureStrategyLookback = false;
		extensions->scvTenureStrategyHistory = false;

		/* Scan all desired tenure strategies. */
		do {
			if (try_scan(scan_start, "fixed")) {
				extensions->scvTenureStrategyFixed = true;
			} else if (try_scan(scan_start, "adaptive")) {
				extensions->scvTenureStrategyAdaptive = true;
			} else if (try_scan(scan_start, "lookback")) {
				extensions->scvTenureStrategyLookback = true;
			} else if (try_scan(scan_start, "history")) {
				extensions->scvTenureStrategyHistory = true;
			} else {
				/* TODO: Add NLS error message once message promotes through VM silo. */
				goto _error;
			}
		} while (try_scan(scan_start, "+"));
		goto _exit;
	}

	if(try_scan(scan_start, "scvTenureSurvivalThreshold=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvTenureSurvivalThreshold=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvTenureSurvivalThreshold=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->scvTenureStrategySurvivalThreshold = ((double)value) / 100.0;
		goto _exit;
	}

#if defined(J9VM_GC_ADAPTIVE_TENURING)
	if(try_scan(scan_start, "adaptiveTenure")) {
		extensions->scvTenureStrategyAdaptive = true;
		goto _exit;
	}

	if(try_scan(scan_start, "noAdaptiveTenure")) {
		extensions->scvTenureStrategyFixed = true;
		extensions->scvTenureStrategyAdaptive = false;
		extensions->scvTenureStrategyLookback = false;
		extensions->scvTenureStrategyHistory = false;
		goto _exit;
	}

	if(try_scan(scan_start, "tenureAge=")) {
		UDATA tenureAge = 0;
		if(!scan_udata_helper(javaVM, scan_start, &tenureAge, "tenureAge=")) {
			goto _error;
		}
		if((tenureAge > OBJECT_HEADER_AGE_MAX) || (tenureAge < OBJECT_HEADER_AGE_MIN)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "tenureAge=", (UDATA)OBJECT_HEADER_AGE_MIN, (UDATA)OBJECT_HEADER_AGE_MAX);
			goto _error;
		}
		extensions->scvTenureAdaptiveTenureAge = tenureAge;
		extensions->scvTenureFixedTenureAge = tenureAge;
		goto _exit;
	}
	
	if(try_scan(scan_start, "scvNoAdaptiveTenure")) {
		extensions->scvTenureStrategyFixed = true;
		extensions->scvTenureStrategyAdaptive = false;
		extensions->scvTenureStrategyLookback = false;
		extensions->scvTenureStrategyHistory = false;
		goto _exit;
	}

	if(try_scan(scan_start, "scvTenureAge=")) {
		UDATA tenureAge = 0;
		if(!scan_udata_helper(javaVM, scan_start, &tenureAge, "scvTenureAge=")) {
			goto _error;
		}
		if((tenureAge > OBJECT_HEADER_AGE_MAX) || (tenureAge < OBJECT_HEADER_AGE_MIN)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvTenureAge=", (UDATA)OBJECT_HEADER_AGE_MIN, (UDATA)OBJECT_HEADER_AGE_MAX);
			goto _error;
		}
		extensions->scvTenureAdaptiveTenureAge = tenureAge;
		extensions->scvTenureFixedTenureAge = tenureAge;
		goto _exit;
	}
	
	if(try_scan(scan_start, "scvAdaptiveTenureAge=")) {
		UDATA adaptiveTenureAge = 0;
		if(!scan_udata_helper(javaVM, scan_start, &adaptiveTenureAge, "scvAdaptiveTenureAge=")) {
			goto _error;
		}
		if((adaptiveTenureAge > OBJECT_HEADER_AGE_MAX) || (adaptiveTenureAge < OBJECT_HEADER_AGE_MIN)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvAdaptiveTenureAge=", (UDATA)OBJECT_HEADER_AGE_MIN, (UDATA)OBJECT_HEADER_AGE_MAX);
			goto _error;
		}
		extensions->scvTenureAdaptiveTenureAge = adaptiveTenureAge;
		goto _exit;
	}
	
	if(try_scan(scan_start, "scvFixedTenureAge=")) {
		UDATA fixedTenureAge = 0;
		if(!scan_udata_helper(javaVM, scan_start, &fixedTenureAge, "scvFixedTenureAge=")) {
			goto _error;
		}
		if((fixedTenureAge > OBJECT_HEADER_AGE_MAX) || (fixedTenureAge < OBJECT_HEADER_AGE_MIN)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvFixedTenureAge=", (UDATA)OBJECT_HEADER_AGE_MIN, (UDATA)OBJECT_HEADER_AGE_MAX);
			goto _error;
		}
		extensions->scvTenureFixedTenureAge = fixedTenureAge;
		goto _exit;
	}

	if(try_scan(scan_start, "scvth=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->scvTenureRatioHigh, "scvth=")) {
			goto _error;
		}
		if(extensions->scvTenureRatioHigh > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvth=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		goto _exit;
	}
	if(try_scan(scan_start, "scvtl=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->scvTenureRatioLow, "scvtl=")) {
			goto _error;
		}
		if(extensions->scvTenureRatioLow > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvtl=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		goto _exit;
	}
#endif /* J9VM_GC_ADAPTIVE_TENURING */

#if defined(J9VM_GC_TILTED_NEW_SPACE)
	/* NOTE: the tilt ratio is specified as the percentage used by new space. This is consistent
	 * with what's printed in -verbose:gc and is intuitive. However we store these ratios internally
	 * as the percentage of survivor space. So a specified value of 30 is stored as 70.
	 */
	if(try_scan(scan_start, "scvTiltRatioMax=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvTiltRatioMax=")) {
			goto _error;
		}
		if((50 > value) || (90 < value)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvTiltRatioMax=", (UDATA)50, (UDATA)90);
		}

		extensions->survivorSpaceMinimumSizeRatio = (100 - value) / (double)100.0;
		goto _exit;
	}
	if(try_scan(scan_start, "scvTiltRatioMin=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvTiltRatioMin=")) {
			goto _error;
		}
		if((50 > value) || (90 < value)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "scvTiltRatioMin=", (UDATA)50, (UDATA)90);
		}

		extensions->survivorSpaceMaximumSizeRatio = (100 - value) / (double)100.0;
		goto _exit;
	}
	if(try_scan(scan_start, "scvTiltIncreaseMax=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "scvTiltIncreaseMax=")) {
			goto _error;
		}
		if(0 == value) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "scvTiltIncreaseMax=", (UDATA)0);
			goto _error;
		}
		
		extensions->tiltedScavengeMaximumIncrease = value / (double)100.0;
		goto _exit;
	}	
	if(try_scan(scan_start, "scvDebugTiltedNursery")) {
		extensions->debugTiltedScavenge = true;
		goto _exit;
	}
	if(try_scan(scan_start, "scvTiltedNursery")) {
		extensions->tiltedScavenge = true;
		goto _exit;
	}
	if(try_scan(scan_start, "scvNoTiltedNursery")) {
		extensions->tiltedScavenge = false;
		goto _exit;
	}
#endif /* J9VM_GC_TILTED_NEW_SPACE */


	if(try_scan(scan_start, "noHeapExpansionAfterExpansionGCCount=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "noHeapExpansionAfterExpansionGCCount=")) {
			goto _error;
		}
		extensions->heapExpansionStabilizationCount = value;
		goto _exit;
	}

	if(try_scan(scan_start, "noHeapContractionAfterExpansionGCCount=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "noHeapContractionAfterExpansionGCCount=")) {
			goto _error;
		}
		extensions->heapContractionStabilizationCount = value;
		goto _exit;
	}

	if(try_scan(scan_start, "ignoreHeapStatsAfterHeapExpansion")) {
		/* this is the same as setting these options to 3 -- the size of the heap stats */
		extensions->heapExpansionStabilizationCount = 3;
		extensions->heapContractionStabilizationCount = 3;
		goto _exit;
	}

#if defined(J9VM_GC_DYNAMIC_NEW_SPACE_SIZING)
	if(try_scan(scan_start, "dynamicNewSpaceSizing")) {
		extensions->dynamicNewSpaceSizing = true;
		goto _exit;
	}

	if(try_scan(scan_start, "noDynamicNewSpaceSizing")) {
		extensions->dynamicNewSpaceSizing = false;
		goto _exit;
	}

	if(try_scan(scan_start, "debugDynamicNewSpaceSizing")) {
		extensions->debugDynamicNewSpaceSizing = true;
		goto _exit;
	}

	/* VMDESIGN 1690: (default) hint to try to avoid moving objects during dynamic new space resizing */
	if(try_scan(scan_start, "dnssAvoidMovingObjects")) {
		extensions->dnssAvoidMovingObjects = true;
		goto _exit;
	}
	
	/* VMDESIGN 1690: use pre-Java 6 new space resizing policy */
	if(try_scan(scan_start, "dnssNoAvoidMovingObjects")) {
		extensions->dnssAvoidMovingObjects = false;
		goto _exit;
	}
	
	if(try_scan(scan_start, "dnssMaximumContraction=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssMaximumContraction=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssMaximumContraction=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssMaximumContraction = ((double)value) / ((double)100);
		goto _exit;
	}
	
	if(try_scan(scan_start, "dnssMaximumExpansion=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssMaximumExpansion=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssMaximumExpansion=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssMaximumExpansion = ((double)value) / ((double)100);
		goto _exit;
	}
	
	if(try_scan(scan_start, "dnssMinimumContraction=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssMinimumContraction=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssMinimumContraction=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssMinimumContraction = ((double)value) / ((double)100);
		goto _exit;
	}
	
	if(try_scan(scan_start, "loaFreeHistorySize=")) {
				UDATA value;
				if(!scan_udata_helper(javaVM, scan_start, &value, "loaFreeHistorySize=")) {
					goto _error;
				}
				if(value > 100) {
					j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "loaFreeHistorySize=", (UDATA)0, (UDATA)100);
					goto _error;
				}
				extensions->loaFreeHistorySize = ((int)value);
				goto _exit;
     }

	if(try_scan(scan_start, "dnssMinimumExpansion=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssMinimumExpansion=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssMinimumExpansion=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssMinimumExpansion = ((double)value) / ((double)100);
		goto _exit;
	}	
	
	if(try_scan(scan_start, "dnssExpectedTimeRatioMinimum=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssExpectedTimeRatioMinimum=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssExpectedTimeRatioMinimum=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssExpectedTimeRatioMinimum._wasSpecified = true;
		extensions->dnssExpectedTimeRatioMinimum._valueSpecified = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "dnssExpectedTimeRatioMaximum=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssExpectedTimeRatioMaximum=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssExpectedTimeRatioMaximum=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssExpectedTimeRatioMaximum._wasSpecified = true;
		extensions->dnssExpectedTimeRatioMaximum._valueSpecified = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "dnssWeightedTimeRatioFactorIncreaseSmall=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssWeightedTimeRatioFactorIncreaseSmall=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssWeightedTimeRatioFactorIncreaseSmall=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssWeightedTimeRatioFactorIncreaseSmall = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "dnssWeightedTimeRatioFactorIncreaseMedium=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssWeightedTimeRatioFactorIncreaseMedium=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssWeightedTimeRatioFactorIncreaseMedium=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssWeightedTimeRatioFactorIncreaseMedium = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "dnssWeightedTimeRatioFactorIncreaseLarge=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssWeightedTimeRatioFactorIncreaseLarge=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssWeightedTimeRatioFactorIncreaseLarge=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssWeightedTimeRatioFactorIncreaseLarge = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "dnssWeightedTimeRatioFactorDecrease=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "dnssWeightedTimeRatioFactorDecrease=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "dnssWeightedTimeRatioFactorDecrease=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->dnssWeightedTimeRatioFactorDecrease = ((double)value) / ((double)100);
		goto _exit;
	}	

#endif /* J9VM_GC_DYNAMIC_NEW_SPACE_SIZING */
#endif /* J9VM_GC_MODRON_SCAVENGER */

	if(try_scan(scan_start, "globalMaximumContraction=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "globalMaximumContraction=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "globalMaximumContraction=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->globalMaximumContraction = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "globalMinimumContraction=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "globalMinimumContraction=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "globalMinimumContraction=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->globalMinimumContraction = ((double)value) / ((double)100);
		goto _exit;
	}

	/* Alternate option to set globalMaximumContraction */
	if(try_scan(scan_start, "maxContractPercent=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "maxContractPercent=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "maxContractPercent=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->globalMaximumContraction = ((double)value) / ((double)100);
		goto _exit;
	}

	/* Alternate option to set globalMinimumContraction */
	if(try_scan(scan_start, "minContractPercent=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "minContractPercent=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "minContractPercent=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->globalMinimumContraction = ((double)value) / ((double)100);
		goto _exit;
	}

	if(try_scan(scan_start, "excessiveGCdebug")) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_DEPRECATED, "-Xgc:excessiveGCdebug=", "-Xtgc:excessivegc");
		goto _error;
	}

	if(try_scan(scan_start, "excessiveGCratio=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "excessiveGCratio=")) {
			goto _error;
		}
		if((value > 100) || (value < 10)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "excessiveGCratio=", (UDATA)10, (UDATA)100);
			goto _error;
		}
		extensions->excessiveGCratio = value;
		goto _exit;
	}

	if(try_scan(scan_start, "excessiveGCFreeSizeRatio=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "excessiveGCFreeSizeRatio=")) {
			goto _error;
		}
		if(value > 100) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "excessiveGCFreeSizeRatio=", (UDATA)0, (UDATA)100);
			goto _error;
		}
		extensions->excessiveGCFreeSizeRatio = ((float)value) / 100.0f;
		goto _exit;
	}
	
	if(try_scan(scan_start, "excessiveGCnewRatioWeight=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "excessiveGCnewRatioWeight=")) {
			goto _error;
		}
		if((value < 1) || (value > 90)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "excessiveGCnewRatioWeight=", (UDATA)1, (UDATA)90);
			goto _error;
		}
		extensions->excessiveGCnewRatioWeight =((float)value) / 100.0f;
		goto _exit;
	}

#if defined(J9VM_GC_MODRON_COMPACTION)
	if(try_scan(scan_start, "stdGlobalCompactToSatisfyAllocate")) {
		extensions->compactToSatisfyAllocate = true;
		goto _exit;
	}
	
	/* Incremental compaction is no longer supported.
	 * Options are supported but deprecated.
	 */
	if(try_scan(scan_start, "icompact=")) {
		UDATA incrementalCompactSteps;
		if(!scan_udata_helper(javaVM, scan_start, &incrementalCompactSteps, "icompact=")) {
			goto _error;
		}
		goto _exit;
	}
#endif /* J9VM_GC_MODRON_COMPACTION */

#if defined (OMR_GC_MODRON_CONCURRENT_MARK)
	if(try_scan(scan_start, "concurrentMark")) {
		extensions->configurationOptions._forceOptionConcurrentMark = true;
		extensions->concurrentMark = true;
		goto _exit;
	}
	
	if(try_scan(scan_start, "noConcurrentMarkKO")) {
		extensions->concurrentKickoffEnabled = false;
		goto _exit;
	}

	if(try_scan(scan_start, "concurrentSlack=")) {
	
		/* if concurrentSlack=macrofrag is set, we will count estimateFragmentation when deciding concurrentgc kickoff , concurrentSlackFragmentationAdjustmentWeight is set as 1.0 as default */
		if (try_scan(scan_start, "macrofrag")) {
			extensions->estimateFragmentation = (GLOBALGC_ESTIMATE_FRAGMENTATION | LOCALGC_ESTIMATE_FRAGMENTATION);
			extensions->processLargeAllocateStats = true;
			extensions->concurrentSlackFragmentationAdjustmentWeight = 1.0;
			goto _exit;
		} else {
			UDATA value;
			if(!scan_udata_helper(javaVM, scan_start, &value, "concurrentSlack=")) {
				goto _error;
			}
			extensions->concurrentSlack = value;
			goto _exit;
		}
	}

	if (try_scan(scan_start, "concurrentSlackFragmentationAdjustmentWeight=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "concurrentSlackFragmentationAdjustmentWeight=")) {
			goto _error;
		}
		if(value > 500) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "concurrentSlackFragmentationAdjustmentWeight==", (UDATA)0, (UDATA)500);
			goto _error;
		}
		extensions->concurrentSlackFragmentationAdjustmentWeight = ((double)value) / 100.0;
		goto _exit;
	}

	if(try_scan(scan_start, "noConcurrentMark")) {
		extensions->configurationOptions._forceOptionConcurrentMark = true;
		extensions->concurrentMark = false;
		goto _exit;
	}		

	if(try_scan(scan_start, "optimizeConcurrentWB")) {
		extensions->optimizeConcurrentWB = true;
		goto _exit;
	}
	
	if(try_scan(scan_start, "noOptimizeConcurrentWB")) {
		extensions->optimizeConcurrentWB = false;
		goto _exit;
	}	
	
	if(try_scan(scan_start, "debugConcurrentMark")) {
		extensions->debugConcurrentMark = true;
		goto _exit;
	}
	
	if(try_scan(scan_start, "cardCleaningPasses=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "cardCleaningPasses=")) {
			goto _error;
		}
		if(value > 2) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "cardCleaningPasses=", (UDATA)0, (UDATA)2);
			goto _error;
		}
		extensions->cardCleaningPasses = value;
		goto _exit;
	}
	
	if(try_scan(scan_start, "cardCleanPass2Boost=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->cardCleanPass2Boost, "cardCleanPass2Boost=")) {
			goto _error;
		}
		goto _exit;
	}
#endif /* OMR_GC_MODRON_CONCURRENT_MARK */

	if(try_scan(scan_start, "version")) {
		j9tty_printf(PORTLIB, "GC Version: %s\n", versionString);
		goto _exit;
	}

#if defined(J9VM_GC_BATCH_CLEAR_TLH)
	if(try_scan(scan_start, "batchClearTLH")) {
		extensions->batchClearTLH = 1;
		goto _exit;
	}
#endif /* J9VM_GC_BATCH_CLEAR_TLH */

#if defined(J9VM_GC_SUBPOOLS_ALIAS)
	/*
	 * Temporary: Accept but silently ignore any subpool option
	 * subpool gc policy is not supported any more just it is an alias for optthruput
	 */
	if(try_scan(scan_start, "subPoolsDebug")) {
		goto _exit;
	}
		
	if(try_scan(scan_start, "disableSubPoolLargeHeap")) {
		goto _exit;
	}	
	
	if(try_scan(scan_start, "microFragmentationDetectionThreshold=")) {
		UDATA value;
		if(!scan_udata_helper(javaVM, scan_start, &value, "microFragmentationDetectionThreshold=")) {
			goto _error;
		}
		goto _exit;
	}
		
	if(try_scan(scan_start, "microFragmentationDetection")) {
		goto _exit;
	}	
#endif /* defined(J9VM_GC_SUBPOOLS_ALIAS) */

#if defined(J9VM_GC_CONCURRENT_SWEEP)
	if(try_scan(scan_start, "concurrentSweep")) {
		extensions->configurationOptions._forceOptionConcurrentSweep = true;
		extensions->concurrentSweep = true;
		goto _exit;
	}

	if(try_scan(scan_start, "noConcurrentSweep")) {
		extensions->configurationOptions._forceOptionConcurrentSweep = true;
		extensions->concurrentSweep = false;
		goto _exit;
	}
#endif /* J9VM_GC_CONCURRENT_SWEEP */

	/* Additional -Xgc:fvtest options */
	if(try_scan(scan_start, "fvtest=")) {
#if defined(J9VM_GC_MODRON_SCAVENGER) || defined(J9VM_GC_VLHGC)
#if defined(J9VM_GC_MODRON_SCAVENGER)
		/* This option forces the scavenger to backout every other collect */
		if (try_scan(scan_start, "forceScavengerBackout")) {
			extensions->fvtest_forceScavengerBackout = true;
			goto _exit;
		}
		
		/* This option forces the scavenger to poison evacuate space after every collection */
		if (try_scan(scan_start, "forcePoisonEvacuate")) {
			extensions->fvtest_forcePoisonEvacuate = true;
			goto _exit;
		}

		/* This forces the nursery space to resize - 3 expands followed by 3 contracts */
		if (try_scan(scan_start, "forceNurseryResize")) {
			extensions->fvtest_forceNurseryResize = true;
			goto _exit;
		}
		
		/* This forces the tenure space to resize - 5 expands followed by 5 contracts */
		if (try_scan(scan_start, "forceTenureResize")) {
			extensions->fvtest_forceOldResize = true;
			goto _exit;
		}
#endif /* J9VM_GC_MODRON_SCAVENGER */
		
		/* This takes a UDATA that restricts the number of scan caches - used to force scan cache overflow */
		if (try_scan(scan_start, "scanCacheCount")) {
			/* Read in restricted scan cache size */
			if(!scan_udata_helper(javaVM, scan_start, &extensions->fvtest_scanCacheCount, "scanCacheCount")) {
				goto _error;
			}
			goto _exit;
		}
#endif /* J9VM_GC_MODRON_SCAVENGER || J9VM_GC_VLHGC */
		
		if (try_scan(scan_start, "alwaysApplyOverflowRounding")) {
			/* see CMVC 12157 -- this option allows us to test overflow 
			 * rounding which is otherwise only applied if the heap is 
			 * allocated very high in the address space 
			 */
			extensions->fvtest_alwaysApplyOverflowRounding = true;
			goto _exit;
		}
		
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		if (try_scan(scan_start, "forceFinalizeClassLoaders")) {
			/* CMVC 122432 - forces all class loaders to be cleaned up via a FinalizerFreeClassLoaderJob
			 * during the finalization phase.  Can be useful for testing that facility or to ensure that
			 * there are no timing assumptions regarding class unloading in the rest of the VM
			 */
			extensions->fvtest_forceFinalizeClassLoaders = true;
			goto _exit;
		}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

		/* Force an excessive GC to throw OOM after this many global GCs */
		if (try_scan(scan_start, "forceExcessiveAllocFailureAfter=")) {
			if(!scan_udata_helper(javaVM, scan_start, &(extensions->fvtest_forceExcessiveAllocFailureAfter), "forceExcessiveAllocFailureAfter=")) {
				goto _error;
			}
			goto _exit;
		}

		/* VM Design 1869: force the heap to be allocated between these values */
		if (try_scan(scan_start, "verifyHeapAbove=")) {
			if(!scan_udata_memory_size_helper(javaVM, scan_start, (UDATA *)&(extensions->fvtest_verifyHeapAbove), "verifyHeapAbove=")) {
				goto _error;
			}
			goto _exit;
		}
		if (try_scan(scan_start, "verifyHeapBelow=")) {
			if(!scan_udata_memory_size_helper(javaVM, scan_start, (UDATA *)&(extensions->fvtest_verifyHeapBelow), "verifyHeapBelow=")) {
				goto _error;
			}
			goto _exit;
		}
#if defined(J9VM_GC_VLHGC)
		if (try_scan(scan_start, "tarokVerifyMarkMapClosure")) {
			extensions->fvtest_tarokVerifyMarkMapClosure = true;
			goto _exit;
		}
#endif /* defined(J9VM_GC_VLHGC) */
		
		if( try_scan(scan_start, "disableInlineAllocation")) {
			extensions->fvtest_disableInlineAllocation = true;
			goto _exit;
		}

		/* test option not recognised */
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_FVTEST_UNKNOWN_TYPE, *scan_start);
		goto _error;
	}
#if defined(J9VM_GC_VLHGC)
#if defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD)
	if (try_scan(scan_start, "concurrentCopyForward")) {
		extensions->_isConcurrentCopyForward = true;
		goto _exit;
	}
	if (try_scan(scan_start, "noConcurrentCopyForward")) {
		extensions->_isConcurrentCopyForward = false;
		goto _exit;
	}
#endif /* defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD) */
#endif /* defined(J9VM_GC_VLHGC) */

#if defined(J9VM_GC_MODRON_SCAVENGER) || defined(J9VM_GC_VLHGC)
	/* If dynamicBreadthFirstScanOrdering is enabled, set scavengerScanOrdering and other required options */
	if(try_scan(scan_start, "dynamicBreadthFirstScanOrdering")) {
		extensions->scavengerScanOrdering = MM_GCExtensions::OMR_GC_SCAVENGER_SCANORDERING_DYNAMIC_BREADTH_FIRST;
		/* Below options are required options for dynamicBreadthFirstScanOrdering */
		extensions->scavengerAlignHotFields = false;
		goto _exit;
	}
#endif /* defined(J9VM_GC_MODRON_SCAVENGER) || defined (J9VM_GC_VLHGC) */

#if defined(J9VM_GC_MODRON_SCAVENGER)
	if (try_scan(scan_start, "scanCacheSize=")) {
		/* Read in restricted scan cache size */
		if(!scan_udata_helper(javaVM, scan_start, &extensions->scavengerScanCacheMaximumSize, "scanCacheSize=")) {
			goto _error;
		}
		if(0 == extensions->scavengerScanCacheMaximumSize) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "scanCacheSize=", (UDATA)0);
			goto _error;
		}
		goto _exit;
	}
	
	if(try_scan(scan_start, "hierarchicalScanOrdering")) {
		extensions->scavengerScanOrdering = MM_GCExtensions::OMR_GC_SCAVENGER_SCANORDERING_HIERARCHICAL;
		goto _exit;
	}
	
	if(try_scan(scan_start, "breadthFirstScanOrdering")) {
		extensions->scavengerScanOrdering = MM_GCExtensions::OMR_GC_SCAVENGER_SCANORDERING_BREADTH_FIRST;
		goto _exit;
	}
#endif /* J9VM_GC_MODRON_SCAVENGER */

	if(try_scan(scan_start, "alwaysCallWriteBarrier")) {
		extensions->alwaysCallWriteBarrier = true;
		goto _exit;
	}
	
	if(try_scan(scan_start, "alwaysCallReadBarrier")) {
		extensions->alwaysCallReadBarrier = true;
		goto _exit;
	}

	if(try_scan(scan_start, "sweepchunksize=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->parSweepChunkSize, "sweepchunksize=")) {
			goto _error;
		}
		if(0 == extensions->parSweepChunkSize) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "sweepchunksize=", (UDATA)0);
			goto _error;
		}
		extensions->parSweepChunkSize *= 1024; /* the value is specified in kB on the command-line */

		goto _exit;
	}

	if(try_scan(scan_start, "verbosegcCycleTime=")) {
		if(!scan_udata_helper(javaVM, scan_start, &extensions->verbosegcCycleTime, "verbosegcCycleTime=")) {
			goto _error;
		}
		if(0 == extensions->verbosegcCycleTime) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "verbosegcCycleTime=", (UDATA)0);
			goto _error;
		}

		goto _exit;
	}
	
	if (try_scan(scan_start, "verboseExtensions")) {
		extensions->verboseExtensions = true;
		goto _exit;
	}
	
	if (try_scan(scan_start, "bufferedLogging")) {
		extensions->bufferedLogging = true;
		goto _exit;
	}

#if defined(J9VM_GC_VLHGC) || defined(J9VM_GC_GENERATIONAL)
	/* currently only used by VLHGC -- consider promoting if required for other policies */
	if (try_scan(scan_start, "numa")) {
		extensions->_numaManager.shouldEnablePhysicalNUMA(true);
		extensions->numaForced = true;
		goto _exit;
	}		

	if (try_scan(scan_start, "noNuma")) {
		extensions->_numaManager.shouldEnablePhysicalNUMA(false);
		extensions->numaForced = true;
		goto _exit;
	}		
#endif /* defined(J9VM_GC_VLHGC) || defined(J9VM_GC_GENERATIONAL) */

	if (try_scan(scan_start, "hybridMemoryPool")) {
		extensions->enableHybridMemoryPool = true;
		goto _exit;
	}
	
	/* Couldn't find a match for arguments */
	return 2;

_error:
	return 0;

_exit:
	return 1;
}

/**
 * Parse the command line looking for -Xgc options.
 * @params optArg string to be parsed
 */
jint
gcParseXgcArguments(J9JavaVM *vm, char *optArg)
{
	char *scan_start = optArg;
	char *scan_limit = optArg + strlen(optArg);
	char *error_scan;
	MM_GCExtensions *extensions;
	PORT_ACCESS_FROM_JAVAVM(vm);

	jint returnValue = JNI_OK;

	extensions = MM_GCExtensions::getExtensions(vm);

	while (scan_start < scan_limit) {
		UDATA scanResult;

		/* ignore separators */
		try_scan(&scan_start, ",");

		error_scan = scan_start;

		/* TODO: inline when merged? */
		scanResult = j9gc_initialize_parse_gc_colon(vm, &scan_start);
		if (0 == scanResult) {
			returnValue = JNI_EINVAL;
			break;
		} else if (1 == scanResult) {
			continue;
		}
		/* if scanResult is 2, we will continue through */

#if defined(J9VM_GC_JNI_ARRAY_CACHE)
		if (try_scan(&scan_start, "jniArrayCacheMax=")) {
			if(try_scan(&scan_start, "unlimited")) {
				vm->jniArrayCacheMaxSize=((UDATA)-1);
			} else {
				if(!scan_udata_helper(vm, &scan_start, &vm->jniArrayCacheMaxSize, "jniArrayCacheMax=")) {
					returnValue = JNI_EINVAL;
					break;
				}
			}
			continue;
		}
#endif /* J9VM_GC_JNI_ARRAY_CACHE */

#if defined(J9VM_GC_FINALIZATION)
		if (try_scan(&scan_start, "finInterval=")) {
			if (try_scan(&scan_start, "nodelay")) {
				extensions->finalizeCycleInterval = -1;
			} else {
				if(!scan_udata_helper(vm, &scan_start, (UDATA *)&extensions->finalizeCycleInterval, "finInterval=")) {
					returnValue = JNI_EINVAL;
					break;
				}
			}

			if (extensions->finalizeCycleInterval == 0) {
				extensions->finalizeCycleInterval = -2;
			}
			continue;
		}
		if (try_scan(&scan_start, "finalizeMainPriority=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->finalizeMainPriority, "finalizeMainPriority=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			if((extensions->finalizeMainPriority < J9THREAD_PRIORITY_USER_MIN) || (extensions->finalizeMainPriority > J9THREAD_PRIORITY_USER_MAX)) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "-Xgc:finalizeMainPriority", (UDATA)J9THREAD_PRIORITY_USER_MIN, (UDATA)J9THREAD_PRIORITY_USER_MAX);
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		if (try_scan(&scan_start, "finalizeWorkerPriority=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->finalizeWorkerPriority, "finalizeWorkerPriority=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			if((extensions->finalizeWorkerPriority < J9THREAD_PRIORITY_USER_MIN) || (extensions->finalizeWorkerPriority > J9THREAD_PRIORITY_USER_MAX)) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "-Xgc:finalizeWorkerPriority", (UDATA)J9THREAD_PRIORITY_USER_MIN, (UDATA)J9THREAD_PRIORITY_USER_MAX);
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
#endif /* J9VM_GC_FINALIZATION */

#if defined(J9MODRON_USE_CUSTOM_SPINLOCKS)
		if (try_scan(&scan_start, "spinCount1=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->lnrlOptions.spinCount1, "spinCount1=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		if (try_scan(&scan_start, "spinCount2=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->lnrlOptions.spinCount2, "spinCount2=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		if (try_scan(&scan_start, "spinCount3=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->lnrlOptions.spinCount3, "spinCount3=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
#endif /* J9MODRON_USE_CUSTOM_SPINLOCKS */

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		if (try_scan(&scan_start, "deadClassLoaderCache=")) {
			if(!scan_udata_memory_size_helper(vm, &scan_start, &extensions->deadClassLoaderCacheSize, "deadClassLoaderCache=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		
		if (try_scan(&scan_start, "classUnloadingThreshold=")) {
			if ( !scan_udata_helper(vm, &scan_start, &extensions->dynamicClassUnloadingThreshold, "classUnloadingThreshold=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			extensions->dynamicClassUnloadingThresholdForced = true;
			continue;
		}
		
		if (try_scan(&scan_start,"classUnloadingKickoffThreshold=")) {
			if ( !scan_udata_helper(vm, &scan_start, &extensions->dynamicClassUnloadingKickoffThreshold, "classUnloadingKickoffThreshold=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			extensions->dynamicClassUnloadingKickoffThresholdForced = true;
			continue;
		}

		if (try_scan(&scan_start, "classUnloadingAnonymousClassWeight=")) {
			UDATA divisor = 0;
			if(!scan_udata_helper(vm, &scan_start, &divisor, "classUnloadingAnonymousClassWeight=")) {
				returnValue = JNI_EINVAL;
				break;
			}

			if (0 == divisor) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "classUnloadingAnonymousClassWeight=", (UDATA)0);
				returnValue = JNI_EINVAL;
				break;
			} else {
				extensions->classUnloadingAnonymousClassWeight = (1.0 / (double)divisor);
			}
			continue;
		}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */


		if (try_scan(&scan_start, "allocationSamplingGranularity=")) {
			if ( !scan_udata_memory_size_helper(vm, &scan_start, &extensions->oolObjectSamplingBytesGranularity, "allocationSamplingGranularity=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			extensions->doOutOfLineAllocationTrace = true;
			continue;
		}

		if (try_scan(&scan_start, "allocationSamplingEnable")) {
			extensions->doOutOfLineAllocationTrace = true;
			continue;
		}
		
		if (try_scan(&scan_start, "allocationSamplingDisable")) {
			extensions->doOutOfLineAllocationTrace = false;
			continue;
		}

		/* see if we are forcing shifting to a specific value */
		if (try_scan(&scan_start, "preferredHeapBase=")) {
			UDATA preferredHeapBase = 0;
			if(!scan_hex_helper(vm, &scan_start, &preferredHeapBase, "preferredHeapBase=")) {
				returnValue = JNI_EINVAL;
				break;
			}

#if defined(J9ZOS390)
			j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_GC_OPTIONS_PREFERREDHEAPBASE_NOT_SUPPORTED_ON_ZOS_WARN);
#else
			extensions->preferredHeapBase = preferredHeapBase;
#endif /* defined(J9ZOS390) */
			continue;
		}

		/* see if they are requesting an initial suballocator heap size */
		if (try_scan(&scan_start, "suballocatorInitialSize=")) {
			if(!scan_udata_memory_size_helper(vm, &scan_start, &extensions->suballocatorInitialSize, "suballocatorInitialSize=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			if(0 == extensions->suballocatorInitialSize) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "-Xgc:suballocatorInitialSize=", (UDATA)0);
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		
		/* see if they are requesting a commit suballocator heap size */
		if (try_scan(&scan_start, "suballocatorCommitSize=")) {
			if(!scan_udata_memory_size_helper(vm, &scan_start, &extensions->suballocatorCommitSize, "suballocatorCommitSize=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			if(0 == extensions->suballocatorCommitSize) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "-Xgc:suballocatorCommitSize=", (UDATA)0);
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}

		/* for testing and service reasons, split heaps is currently restricted to Win32 only */
#if defined(J9VM_GC_GENERATIONAL) && (defined(WIN32) && !defined(WIN64))
		/* see if we are supposed to enable split heaps */
		if (try_scan(&scan_start, "splitheap")) {
			extensions->enableSplitHeap = true;
			/* If -Xgc:splitheap is specified, -Xgcpolicy:gencon is implicitly specified */
			extensions->configurationOptions._gcPolicy = gc_policy_gencon;
			continue;
		}
#endif /* defined(J9VM_GC_GENERATIONAL) && (defined(WIN32) && !defined(WIN64)) */

		/* Check for a user-specified region size for the fixed-sized table regions */
		if (try_scan(&scan_start, "regionSize=")) {
			if(!scan_udata_memory_size_helper(vm, &scan_start, &extensions->regionSize, "regionSize=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}

#if defined(J9VM_GC_ENABLE_DOUBLE_MAP)
		if (try_scan(&scan_start, "enableArrayletDoubleMapping")) {
			extensions->isArrayletDoubleMapRequested = true;
			continue;
		}
		if (try_scan(&scan_start, "disableArrayletDoubleMapping")) {
			extensions->isArrayletDoubleMapRequested = false;
			continue;
		}
#endif /* J9VM_GC_ENABLE_DOUBLE_MAP */

#if defined (J9VM_GC_VLHGC)
		if (try_scan(&scan_start, "fvtest_tarokForceNUMANode=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->fvtest_tarokForceNUMANode, "fvtest_tarokForceNUMANode=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
		if (try_scan(&scan_start, "fvtest_tarokFirstContext=")) {
			if(!scan_udata_helper(vm, &scan_start, &extensions->fvtest_tarokFirstContext, "fvtest_tarokFirstContext=")) {
				returnValue = JNI_EINVAL;
				break;
			}
			continue;
		}
#endif /* defined (J9VM_GC_VLHGC) */

		if (try_scan(&scan_start, "verboseFormat=")) {
			if (try_scan(&scan_start, "default")) {
				extensions->verboseNewFormat = true;
				continue;
			}
			if (try_scan(&scan_start, "deprecated")) {
				extensions->verboseNewFormat = false;
				continue;
			}
			/* verbose format not recognised J9NLS_GC_OPTION_UNKNOWN*/
			/* j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_VERBOSEFORMAT_UNKNOWN_FORMAT, *scan_start); */
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_UNKNOWN, error_scan);
			returnValue = JNI_EINVAL;
			break;
		}

		/* Couldn't find a match for arguments */
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_UNKNOWN, error_scan);
		returnValue = JNI_EINVAL;
		break;

	}/* end loop */

	return returnValue;
}

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
 * @ingroup GC_Modron_Tarok
 */

#include "j9.h"
#include "j9cfg.h"
#include "j9port.h"
#include "ModronAssertions.h"

#include <math.h>
#include <string.h>

#include "SchedulingDelegate.hpp"

#include "CompactGroupPersistentStats.hpp"
#include "CycleState.hpp"
#include "CompactGroupManager.hpp"
#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"
#include "GlobalAllocationManagerTarok.hpp"
#include "Heap.hpp"
#include "HeapRegionIteratorVLHGC.hpp"
#include "HeapRegionManager.hpp"
#include "IncrementalGenerationalGC.hpp"
#include "MemoryPoolAddressOrderedList.hpp"

/* NOTE: old logic for determining incremental thresholds has been deleted. Please 
 * see CVS history, version 1.14, if you need to find this logic
 */

/* Arbitrarily given historical averaging weight for scan rate measurement.
 * We want to give much more weight to GMP info than PGC,
 * since scan rate is used for GMP duration estimation
 */
const double measureScanRateHistoricWeightForGMP = 0.50;
const double measureScanRateHistoricWeightForPGC = 0.95;
const double partialGCTimeHistoricWeight = 0.80;
const double incrementalScanTimePerGMPHistoricWeight = 0.50;
const double bytesScannedConcurrentlyPerGMPHistoricWeight = 0.50;
const uintptr_t minimumPgcTime = 5;
const uintptr_t minimumEdenRegions = 1;
const uintptr_t consecutivePGCToChangeEden = 16; /* Keeping this as power of 2 allows bitwise operations to be used instead of modulus */

MM_SchedulingDelegate::MM_SchedulingDelegate (MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager)
	: MM_BaseNonVirtual()
	, _extensions(MM_GCExtensions::getExtensions(env))
	, _regionManager(manager)
	, _taxationIndex(0)
	, _remainingGMPIntermissionIntervals(0)
	, _nextIncrementWillDoPartialGarbageCollection(false)
	, _nextIncrementWillDoGlobalMarkPhase(false)
	, _nextPGCShouldCopyForward(_extensions->tarokPGCShouldCopyForward)
	, _currentlyPerformingGMP(false)
	, _globalSweepRequired(false)
	, _disableCopyForwardDuringCurrentGlobalMarkPhase(false)
	, _idealEdenRegionCount(0)
	, _minimumEdenRegionCount(0)
	, _edenRegionCount(0)
	, _edenSurvivalRateCopyForward(1.0)
	, _nonEdenSurvivalCountCopyForward(0)
	, _numberOfHeapRegions(0)
	, _previousReclaimableRegions(0)
	, _previousDefragmentReclaimableRegions(0)
	, _regionConsumptionRate(0.0)
	, _defragmentRegionConsumptionRate(0.0)
	, _bytesCompactedToFreeBytesRatio(0.0)
	, _averageCopyForwardBytesCopied(0.0)
	, _averageCopyForwardBytesDiscarded(0.0)
	, _averageSurvivorSetRegionCount(0.0)
	, _averageCopyForwardRate(1.0)
	, _averageMacroDefragmentationWork(0.0)
	, _currentMacroDefragmentationWork(0)
	, _didGMPCompleteSinceLastReclaim(false)
	, _liveSetBytesAfterPartialCollect(0)
	, _heapOccupancyTrend(1.0)
	, _liveSetBytesBeforeGlobalSweep(0)
	, _liveSetBytesAfterGlobalSweep(0)
	, _previousLiveSetBytesAfterGlobalSweep(0)
	, _scannableBytesRatio(1.0)
	, _historicTotalIncrementalScanTimePerGMP(0)
	, _historicBytesScannedConcurrentlyPerGMP(0)
	, _estimatedFreeTenure(0)
	, _maxEdenPercent(0.75)
	, _minEdenPercent(0.01)
	, _partialGcStartTime(0)
	, _partialGcOverhead(0.07)
	, _historicalPartialGCTime(0)
	, _globalMarkIncrementsTotalTime(0)
	, _globalMarkIntervalStartTime(0)
	, _globalMarkOverhead(0.0)
	, _globalSweepTimeUs(0)
	, _concurrentMarkGCThreadsTotalWorkTime(0)
	, _dynamicGlobalMarkIncrementTimeMillis(50)
	, _pgcTimeIncreasePerEdenRegionFactor(1.0001)
	, _edenSizeFactor(0)
	, _pgcCountSinceGMPEnd(0)
	, _averagePgcInterval(0)
	, _totalGMPWorkTimeUs(0)
	, _scanRateStats()
{
	_typeId = __FUNCTION__;
}



uintptr_t
MM_SchedulingDelegate::getInitialTaxationThreshold(MM_EnvironmentVLHGC *env)
{
	/* reset all stored state and call getNextTaxationThreshold() */
	_nextIncrementWillDoGlobalMarkPhase = false;
	_nextIncrementWillDoPartialGarbageCollection = false;
	_taxationIndex = 0;
	_remainingGMPIntermissionIntervals = _extensions->tarokGMPIntermission;
	calculateEdenSize(env);

	/* initial value for _averageSurvivorSetRegionCount is arbitrarily chosen as 30% of Eden size (after first Eden is selected) */
	_averageSurvivorSetRegionCount = 0.3 * (double)getCurrentEdenSizeInBytes(env) / _regionManager->getRegionSize();

	return getNextTaxationThreshold(env);
}

void 
MM_SchedulingDelegate::globalMarkCycleStart(MM_EnvironmentVLHGC *env){
	calculateGlobalMarkOverhead(env);

	_currentlyPerformingGMP = true;
	/* Reset the total time taken for each increment of global mark phase, along with the time for concurrent mark GC work*/
	_globalMarkIncrementsTotalTime = 0;
	_concurrentMarkGCThreadsTotalWorkTime = 0;
}

void 
MM_SchedulingDelegate::calculateGlobalMarkOverhead(MM_EnvironmentVLHGC *env) {
	/* Calculate statistics regarding GMP overhead */
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	/* Determine how long it has been since previous global mark cycle started */
	uint64_t globalMarkIntervalEndTime = j9time_hires_clock();
	uint64_t globalMarkIntervalTime = j9time_hires_delta(_globalMarkIntervalStartTime, globalMarkIntervalEndTime, J9PORT_TIME_DELTA_IN_MICROSECONDS);

	/* Determine the time cost we attribute to concurrent GMP work from previous cycle */
	uint64_t concurrentCostUs = _concurrentMarkGCThreadsTotalWorkTime / 1000;

	/* 
	 * Total GMP overhead is time taken for all increments + the time we attribute for concurrent GC parts of GMP, and global sweep time.
	 * Since it's possible mutator threads were idle, only give 0.5 weight for concurrent GMP work
	 */
	uint64_t potentialGMPWorkTime =  _globalMarkIncrementsTotalTime + _globalSweepTimeUs + (uint64_t)(concurrentCostUs * 0.5);
	double potentialOverhead = (double)potentialGMPWorkTime/globalMarkIntervalTime;

	if ((0.0 < potentialOverhead) && (1.0 > potentialOverhead) && (0 != _globalMarkIntervalStartTime)) {
		/* Make sure no clock error occured */
		_totalGMPWorkTimeUs = potentialGMPWorkTime;
	} else if (0 == _totalGMPWorkTimeUs) {
		/* At the very beggining of a run, assume GMP time is 5x larger than avg pgc time.
		 * This is a very rough approximation, but it gives us enough data to make decision about eden size
		 */
		_totalGMPWorkTimeUs = (_historicalPartialGCTime * 1000) * 5;
	} 

	_globalMarkOverhead = (double)_totalGMPWorkTimeUs/globalMarkIntervalTime;
	
	Trc_MM_SchedulingDelegate_calculateGlobalMarkOverhead(env->getLanguageVMThread(), _globalMarkOverhead, _globalMarkIncrementsTotalTime, concurrentCostUs, globalMarkIntervalTime / 1000);

	/* Set start time of next GMP phase, as end of current one */
	_globalMarkIntervalStartTime = globalMarkIntervalEndTime;

}

void MM_SchedulingDelegate::globalMarkCycleEnd(MM_EnvironmentVLHGC *env) {
	_currentlyPerformingGMP = false;
}

void 
MM_SchedulingDelegate::globalMarkPhaseCompleted(MM_EnvironmentVLHGC *env)
{
	/* Taking a snapshot of _liveSetBytesAfterPartialCollect from the last PGC.
	 * This is slightly incorrect. We should take liveSetBytes at the beginning of next PGC
	 * (just before sweep is done)
	 */
	_liveSetBytesBeforeGlobalSweep = _liveSetBytesAfterPartialCollect;

	_remainingGMPIntermissionIntervals = _extensions->tarokGMPIntermission;
	
	/* reset the reclaimable estimate, since we just created more reclaimable data */
	_previousReclaimableRegions = 0;

	_didGMPCompleteSinceLastReclaim = true;

	_globalSweepRequired = true;

	_disableCopyForwardDuringCurrentGlobalMarkPhase = false;

	updateGMPStats(env);
}

void 
MM_SchedulingDelegate::globalMarkIncrementCompleted(MM_EnvironmentVLHGC *env)
{
	measureScanRate(env, measureScanRateHistoricWeightForGMP);
	/* Time how long the last global mark increment took */
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	uint64_t globalMarkIncrementStartTime = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._startTime;
	uint64_t globalMarkIncrementEndTime = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._endTime;

	uint64_t globalMarkIncrementElapsedTime = j9time_hires_delta(globalMarkIncrementStartTime, globalMarkIncrementEndTime, J9PORT_TIME_DELTA_IN_MICROSECONDS);

	_globalMarkIncrementsTotalTime += globalMarkIncrementElapsedTime;
}

void 
MM_SchedulingDelegate::globalGarbageCollectCompleted(MM_EnvironmentVLHGC *env, uintptr_t reclaimableRegions, uintptr_t defragmentReclaimableRegions)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	/* Re-estimate the reclaimable region set but don't measure region consumption, since this wasn't a PGC */
	_previousReclaimableRegions = reclaimableRegions;
	_previousDefragmentReclaimableRegions = defragmentReclaimableRegions;

	/* Global GC will do full compact of the heap. No work is left for PGCs */
	_bytesCompactedToFreeBytesRatio = 0.0;

	/* since we did full sweep, there is no need for next PGC to do it again */
	_globalSweepRequired = false;

	/* if GMP ended up with AF, we need to clear this flag as if GMP normally completed */
	_disableCopyForwardDuringCurrentGlobalMarkPhase = false;

	Trc_MM_SchedulingDelegate_globalGarbageCollectCompleted(env->getLanguageVMThread(), _bytesCompactedToFreeBytesRatio);

	TRIGGER_J9HOOK_MM_PRIVATE_VLHGC_GARBAGE_COLLECT_COMPLETED(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock()
	);
}

void
MM_SchedulingDelegate::partialGarbageCollectStarted(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	/* Don't count the very first PGC */
	if (0 != _partialGcStartTime) {
		double pgcIntervalHistoricWeight = 0.5;
		uint64_t recentPgcInterval = j9time_hires_delta(_partialGcStartTime, j9time_hires_clock(), J9PORT_TIME_DELTA_IN_MICROSECONDS);
		_averagePgcInterval = (uintptr_t)(pgcIntervalHistoricWeight * _averagePgcInterval) + (uintptr_t)((1- pgcIntervalHistoricWeight) * recentPgcInterval);
	}

	/* Record the GC start time in order to track Partial GC times (and averages) over the course of the application lifetime */
	_partialGcStartTime = j9time_hires_clock();
	calculatePartialGarbageCollectOverhead(env);
}

void
MM_SchedulingDelegate::calculatePartialGarbageCollectOverhead(MM_EnvironmentVLHGC *env) {

	if ((0 == _averagePgcInterval) || (0 == _historicalPartialGCTime)) {
		/* On the very first PGC, we can't calculate overhead */
		return;
	}
	
	double recentOverhead = (double)(_historicalPartialGCTime * 1000)/_averagePgcInterval;
	_partialGcOverhead = MM_Math::weightedAverage(_partialGcOverhead, recentOverhead, 0.5);
	
	Trc_MM_SchedulingDelegate_calculatePartialGarbageCollectOverhead(env->getLanguageVMThread(), _partialGcOverhead, _averagePgcInterval/1000, _historicalPartialGCTime);
}

void
MM_SchedulingDelegate::determineNextPGCType(MM_EnvironmentVLHGC *env)
{
	/* if we have no historic scan rate info, we want to force M/S/C collect */
	if (0.0 == _scanRateStats.microSecondsPerByteScanned) {
		env->_cycleState->_reasonForMarkCompactPGC = MM_CycleState::reason_calibration;
		_nextPGCShouldCopyForward = false;
	}

	/* Aborted CopyForward happened in near past. The rest of PGCs until GMP completes, should not try CopyForward. */
	/* try only mark partial nursery regions instead of mark all of collectionSet in CopyForwardHybrid mode */

	env->_cycleState->_shouldRunCopyForward = _nextPGCShouldCopyForward;
	if (_nextPGCShouldCopyForward && _extensions->tarokPGCShouldMarkCompact) {
		/* we are going to perform a copy-forward and are allowed to compact so the next cycle should compact */
		_nextPGCShouldCopyForward = false;
	} else if (!_nextPGCShouldCopyForward && _extensions->tarokPGCShouldCopyForward) {
		/* we are going to perform a compact and are allowed to copy-forward so the next cycle should copy-forward */
		_nextPGCShouldCopyForward = true;
	} else {
		/* we aren't allowed to change from our current mode so leave it as is */
	}
}

void
MM_SchedulingDelegate::calculateGlobalMarkIncrementTimeMillis(MM_EnvironmentVLHGC *env, uint64_t pgcTime)
{
	if(U_32_MAX < pgcTime) {
		/* Time likely traveled backwards due to a clock adjustment - just ignore this round */
	} else {
		/* Prime or calculate the running weighted average for PGC times */
		if (0 == _historicalPartialGCTime) {
			_historicalPartialGCTime = pgcTime;
		} else {
			_historicalPartialGCTime = (uint64_t) ((_historicalPartialGCTime * partialGCTimeHistoricWeight) + (pgcTime * (1-partialGCTimeHistoricWeight)));
		}

		Assert_MM_true(U_32_MAX >= _historicalPartialGCTime);
		/* we just take a fraction (1/3) of the recent average, so that we do not impede mutator utilization significantly */
		/* (note that we need to assume a mark increment took at least 1 millisecond or else we will divide by zero in later calculations) */
		_dynamicGlobalMarkIncrementTimeMillis = OMR_MAX((uintptr_t)(_historicalPartialGCTime / 3), 1);
	}

}

void
MM_SchedulingDelegate::resetPgcTimeStatistics(MM_EnvironmentVLHGC *env) 
{
	_pgcCountSinceGMPEnd = 0;
}

void
MM_SchedulingDelegate::partialGarbageCollectCompleted(MM_EnvironmentVLHGC *env, uintptr_t reclaimableRegions, uintptr_t defragmentReclaimableRegions)
{
	Trc_MM_SchedulingDelegate_partialGarbageCollectCompleted_Entry(env->getLanguageVMThread(), reclaimableRegions, defragmentReclaimableRegions);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CopyForwardStats *copyForwardStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats;
	bool globalSweepHappened = _globalSweepRequired;
	_globalSweepRequired = false;
	/* copy out the Eden size of the previous interval (between the last PGC and this one) before we recalculate the next one */
	uintptr_t edenCountBeforeCollect = getCurrentEdenSizeInRegions(env);
	
	Trc_MM_SchedulingDelegate_partialGarbageCollectCompleted_stats(env->getLanguageVMThread(),
			copyForwardStats->_edenEvacuateRegionCount,
			copyForwardStats->_nonEdenEvacuateRegionCount,
			copyForwardStats->_edenSurvivorRegionCount,
			copyForwardStats->_nonEdenSurvivorRegionCount,
			edenCountBeforeCollect);

	if (env->_cycleState->_shouldRunCopyForward) {
		uintptr_t regionSize = _regionManager->getRegionSize();
		
		/* count the number of survivor regions allocated specifically to support Eden survivors */
		uintptr_t edenSurvivorCount = copyForwardStats->_edenSurvivorRegionCount;
		uintptr_t nonEdenSurvivorCount = copyForwardStats->_nonEdenSurvivorRegionCount;
		
		/* estimate how many more regions we would have needed to avoid abort */
		Assert_MM_true( (0 == copyForwardStats->_scanBytesEden) || copyForwardStats->_aborted || (0 != copyForwardStats->_nonEvacuateRegionCount));
		Assert_MM_true( (0 == copyForwardStats->_scanBytesNonEden) || copyForwardStats->_aborted || (0 != copyForwardStats->_nonEvacuateRegionCount));
		edenSurvivorCount += (copyForwardStats->_scanBytesEden + regionSize - 1) / regionSize;
		nonEdenSurvivorCount += (copyForwardStats->_scanBytesNonEden + regionSize - 1) / regionSize;

		/* Eden count could be 0 in special case, after compaction if there is still no free region for scheduling eden(eden count = 0),
		   will skip update Survival Rate */
		if (0 != edenCountBeforeCollect) {
			double thisSurvivalRate = (double)edenSurvivorCount / (double)edenCountBeforeCollect;
			updateSurvivalRatesAfterCopyForward(thisSurvivalRate, nonEdenSurvivorCount);
		}

		if (copyForwardStats->_aborted && (0 ==_remainingGMPIntermissionIntervals)) {
			_disableCopyForwardDuringCurrentGlobalMarkPhase = true;
		}
	} else {
		/* measure scan rate in PGC, only if we did M/S/C collect */
		measureScanRate(env, measureScanRateHistoricWeightForPGC);
	}
	measureConsumptionForPartialGC(env, reclaimableRegions, defragmentReclaimableRegions);

	/* Calculate the time spent in the current Partial GC */
	uint64_t partialGcEndTime = j9time_hires_clock();
	uint64_t pgcTime = j9time_hires_delta(_partialGcStartTime, partialGcEndTime, J9PORT_TIME_DELTA_IN_MILLISECONDS);
	
	_pgcCountSinceGMPEnd += 1;

	/* Check eden size based off of new PGC stats */
	checkEdenSizeAfterPgc(env, globalSweepHappened);
	calculateEdenSize(env);
	/* Recalculate GMP intermission after (possibly) resizing eden */
	calculateAutomaticGMPIntermission(env);
	estimateMacroDefragmentationWork(env);

	calculateGlobalMarkIncrementTimeMillis(env, pgcTime);
	updatePgcTimePrediction(env);

	TRIGGER_J9HOOK_MM_PRIVATE_VLHGC_GARBAGE_COLLECT_COMPLETED(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		partialGcEndTime
	);
	
	Trc_MM_SchedulingDelegate_partialGarbageCollectCompleted_Exit(env->getLanguageVMThread());
}

uintptr_t
MM_SchedulingDelegate::getNextTaxationThresholdInternal(MM_EnvironmentVLHGC *env)
{
	/* these must be in their initial invalid state (both false) when this is called */
	Assert_MM_false(_nextIncrementWillDoPartialGarbageCollection);
	Assert_MM_false(_nextIncrementWillDoGlobalMarkPhase);
	 
	uintptr_t threshold = (_edenRegionCount * _regionManager->getRegionSize());
	uintptr_t nextTaxationIndex = _taxationIndex;
	
	if(_extensions->tarokEnableIncrementalGMP) {
		uintptr_t numerator = _extensions->tarokPGCtoGMPNumerator;
		uintptr_t denominator = _extensions->tarokPGCtoGMPDenominator;
		if (1 == numerator) {
			/* the PGC:GMP ratio is 1:n. Therefore every (n+1)th taxation point is a PGC, and the remainder are GMPs.
			 * e.g. --GMP--PGC--GMP--GMP--GMP--PGC--GMP--GMP--GMP--PGC--
			 */
			if (0 == (nextTaxationIndex % (denominator + 1))) {
				_nextIncrementWillDoGlobalMarkPhase = true;
			} else {
				_nextIncrementWillDoPartialGarbageCollection = true;
			}
			/* divide the gap between PGCs up into n+1 taxation points */
			threshold /= (denominator + 1);
		} else if (1 == denominator) {
			/* The PGC:GMP ratio is n:1. Therefore every (n+1)th taxation point is a GMP, and the remainder are PGCs.
			 * The GMP should occur half way between two PGCs.
			 * e.g. ------PGC------PGC---GMP---PGC------PGC---GMP---PGC------ 
			 */
			if (0 == (nextTaxationIndex % (numerator + 1))) {
				/* we just completed a PGC, and the next increment is a GMP */
				_nextIncrementWillDoGlobalMarkPhase = true;
				threshold /= 2;
			} else if (0 == ((nextTaxationIndex - 1) % (numerator + 1))) {
				/* we just completed a GMP, and the next increment is a PGC */
				_nextIncrementWillDoPartialGarbageCollection = true;
				threshold /= 2;
			} else {
				/* we just completed a PGC, and the next increment is also a PGC */
				_nextIncrementWillDoPartialGarbageCollection = true;
			}
		} else {
			/* the ratio must be 1:n or n:1 */
			Assert_MM_unreachable();
		}
	} else {
		/* Incremental GMP is disabled, so every increment just does a PGC.
		 * e.g. ------PGC------PGC------PGC------PGC------PGC------
		 */ 
		_nextIncrementWillDoPartialGarbageCollection = true;
	}
	
	_taxationIndex += 1;
		
	return threshold;
}

uintptr_t
MM_SchedulingDelegate::getNextTaxationThreshold(MM_EnvironmentVLHGC *env)
{
	/* TODO: eventually this should be some adaptive number which the 
	 * delegate calculates based on survival rates, collection times, ... 
	 */
	
	Trc_MM_SchedulingDelegate_getNextTaxationThreshold_Entry(env->getLanguageVMThread());
	
	uintptr_t nextTaxationIndex = _taxationIndex;
	uintptr_t threshold = 0;
	
	/* consume thresholds until we complete the GMP intermission or we encounter a PGC. */
	/* TODO: this could be time consuming if the intermission were very large */
	do {
		threshold += getNextTaxationThresholdInternal(env);
		
		/* skip the next GMP interval if necessary */
		if ( (0 < _remainingGMPIntermissionIntervals) && _nextIncrementWillDoGlobalMarkPhase ) {
			_remainingGMPIntermissionIntervals -= 1;
			_nextIncrementWillDoGlobalMarkPhase = false;
		}
	} while (!_nextIncrementWillDoGlobalMarkPhase && !_nextIncrementWillDoPartialGarbageCollection);

	uintptr_t regionSize = _regionManager->getRegionSize();
	threshold = OMR_MAX(regionSize, MM_Math::roundToFloor(regionSize, threshold));
	
	Trc_MM_SchedulingDelegate_getNextTaxationThreshold_Exit(env->getLanguageVMThread(),
			nextTaxationIndex, 
			(_edenRegionCount * regionSize),
			threshold,
			_nextIncrementWillDoGlobalMarkPhase ? 1 : 0,
			_nextIncrementWillDoPartialGarbageCollection ? 1 : 0);
	
	return threshold;
}

void 
MM_SchedulingDelegate::getIncrementWork(MM_EnvironmentVLHGC *env, bool* doPartialGarbageCollection, bool* doGlobalMarkPhase)
{
	*doPartialGarbageCollection = _nextIncrementWillDoPartialGarbageCollection;
	*doGlobalMarkPhase = _nextIncrementWillDoGlobalMarkPhase;
	
	/* invalidate the remembered values */
	_nextIncrementWillDoPartialGarbageCollection = false;
	_nextIncrementWillDoGlobalMarkPhase = false;
}

void 
MM_SchedulingDelegate::measureScanRate(MM_EnvironmentVLHGC *env, double historicWeight)
{
	Trc_MM_SchedulingDelegate_measureScanRate_Entry(env->getLanguageVMThread(), env->_cycleState->_collectionType);
	uintptr_t currentBytesScanned = 0;
	uint64_t scantime = 0;
	if (env->_cycleState->_collectionType == MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION) {
		/* mark/compact PGC has been replaced with CopyForwardHybrid collector, so retrieve scan stats from  */
		MM_CopyForwardStats *copyforwardStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats;
		currentBytesScanned = copyforwardStats->_scanBytesTotal + copyforwardStats->_bytesCardClean;
		scantime = copyforwardStats->_endTime - copyforwardStats->_startTime;
	} else {
		MM_MarkVLHGCStats *markStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats;
		currentBytesScanned = markStats->_bytesScanned + markStats->_bytesCardClean;
		scantime = markStats->getScanTime();
	}


	if (0 != currentBytesScanned) {
		PORT_ACCESS_FROM_ENVIRONMENT(env);
		uintptr_t historicalBytesScanned = _scanRateStats.historicalBytesScanned;
		uint64_t historicalScanMicroseconds = _scanRateStats.historicalScanMicroseconds;
		/* NOTE: scan time is the total time all threads spent scanning */
		uint64_t currentScanMicroseconds = j9time_hires_delta(0, scantime, J9PORT_TIME_DELTA_IN_MICROSECONDS);

		if (0 != historicalBytesScanned) {
			/* Keep a historical count of bytes scanned and scan times and re-derive microsecondsperBytes every time we receive new data */
			_scanRateStats.historicalBytesScanned = (uintptr_t) ((historicalBytesScanned * historicWeight) + (currentBytesScanned * (1.0 - historicWeight)));
			_scanRateStats.historicalScanMicroseconds = (uint64_t) ((historicalScanMicroseconds * historicWeight) + (currentScanMicroseconds * (1.0 - historicWeight)));
		} else {
			/* if we have no historic data, do not use averaging */
			_scanRateStats.historicalBytesScanned = currentBytesScanned;
			_scanRateStats.historicalScanMicroseconds = currentScanMicroseconds;
		}

		if (0 != _scanRateStats.historicalBytesScanned) {
			double microSecondsPerByte = (double)_scanRateStats.historicalScanMicroseconds / (double)_scanRateStats.historicalBytesScanned;
			_scanRateStats.microSecondsPerByteScanned = microSecondsPerByte;
		}

		Trc_MM_SchedulingDelegate_measureScanRate_summary(env->getLanguageVMThread(), _extensions->gcThreadCount, currentBytesScanned, currentScanMicroseconds, _scanRateStats.historicalBytesScanned, _scanRateStats.historicalScanMicroseconds, _scanRateStats.microSecondsPerByteScanned);
	}

	Trc_MM_SchedulingDelegate_measureScanRate_Exit(env->getLanguageVMThread(), _scanRateStats.microSecondsPerByteScanned );
}

void
MM_SchedulingDelegate::estimateMacroDefragmentationWork(MM_EnvironmentVLHGC *env)
{
	const double historicWeight = 0.80; /* arbitrarily give 80% weight to historical result, 20% to newest result */
	_averageMacroDefragmentationWork = (_averageMacroDefragmentationWork * historicWeight) + (_currentMacroDefragmentationWork * (1.0 - historicWeight));
	Trc_MM_SchedulingDelegate_estimateMacroDefragmentationWork(env->getLanguageVMThread(), _currentMacroDefragmentationWork, _averageMacroDefragmentationWork);

	_currentMacroDefragmentationWork = 0;
}

void
MM_SchedulingDelegate::updateCurrentMacroDefragmentationWork(MM_EnvironmentVLHGC *env, MM_HeapRegionDescriptorVLHGC *region)
{
<<<<<<< db526a5fa88bac9f84bec07dab5b7ac3a4db3470
	MM_MemoryPool *memoryPool = region->getMemoryPool();
	UDATA freeMemory = memoryPool->getFreeMemoryAndDarkMatterBytes();
	UDATA liveData = _regionManager->getRegionSize() - freeMemory;
=======
	MM_MemoryPoolBumpPointer *memoryPool = (MM_MemoryPoolBumpPointer *)region->getMemoryPool();
	uintptr_t freeMemory = memoryPool->getFreeMemoryAndDarkMatterBytes();
	uintptr_t liveData = _regionManager->getRegionSize() - freeMemory;
>>>>>>> Refactor 3 files with standard integer types

	double bytesDiscardedPerByteCopied = (_averageCopyForwardBytesCopied > 0.0) ? (_averageCopyForwardBytesDiscarded / _averageCopyForwardBytesCopied) : 0.0;
	uintptr_t estimatedFreeMemoryDiscarded = (uintptr_t)(liveData * bytesDiscardedPerByteCopied);
	uintptr_t recoverableFreeMemory = MM_Math::saturatingSubtract(freeMemory, estimatedFreeMemoryDiscarded);

	/* take a min out of free memory and live data.
	 * However, this is an overestimate, since the work will often be calculated twice (both as source and as destination).
	 * More correct estimate requires knowledge of all regions in oldest age group (knapsack problem) .
	 */
	_currentMacroDefragmentationWork += OMR_MIN(recoverableFreeMemory , liveData);
}

void
MM_SchedulingDelegate::updateLiveBytesAfterPartialCollect()
{
	/* Measure the amount of data to be scanned.
	 * This is an approximate upper bound. The actual amount will be lower, since:
	 * a) not everything measured is actually live
	 * b) the measured data includes primitive arrays, which don't need to be scanned
	 */
	_liveSetBytesAfterPartialCollect = 0;
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			MM_MemoryPool *memoryPool = region->getMemoryPool();
			Assert_MM_true(NULL != memoryPool);
			_liveSetBytesAfterPartialCollect += region->getSize();
			_liveSetBytesAfterPartialCollect -= memoryPool->getActualFreeMemorySize();
			_liveSetBytesAfterPartialCollect -= memoryPool->getDarkMatterBytes();
		} else if (region->isArrayletLeaf()) {
			if (_extensions->objectModel.isObjectArray(region->_allocateData.getSpine())) {
				_liveSetBytesAfterPartialCollect += region->getSize();
			}
		} 
	}
}

double
MM_SchedulingDelegate::calculateEstimatedGlobalBytesToScan() const
{
	/* If historic occupancy trend is negative (due to high death rate),
	 * liveSetAdjustedByOccupancyTrend should still be no less than current _liveSetBytesAfterGlobalSweep
	 * (we do not want to extrapolate negative trend)
	 */
	double heapOccupancyTrendAdjusted = OMR_MAX(0.0, _heapOccupancyTrend);
	/* If current occupancy trend is negative (due to strong DCS effect), liveSetAdjustedByOccupancyTrend should equal current liveSetBytesAfterPartialCollect */
	double liveSetBytesAfterPartialDeltaSinceLastGlobalSweep = OMR_MAX(0.0, (double)_liveSetBytesAfterPartialCollect - (double)_liveSetBytesAfterGlobalSweep);
	double liveSetAdjustedForOccupancyTrend = _liveSetBytesAfterPartialCollect - (liveSetBytesAfterPartialDeltaSinceLastGlobalSweep * (1.0 - heapOccupancyTrendAdjusted));

	double liveSetAdjustedForScannableBytesRatio = liveSetAdjustedForOccupancyTrend * _scannableBytesRatio;
	return liveSetAdjustedForScannableBytesRatio;
}

uintptr_t
MM_SchedulingDelegate::calculateRecommendedEdenSize(MM_EnvironmentVLHGC *env) 
{

	if (0 == _pgcCountSinceGMPEnd) {
		/* No statistics have been collected - just return the current eden size */
		return getCurrentEdenSizeInBytes(env);
	}
	
	/* 
	 * Several statistics have observed which are needed to predict best eden size. 
	 * These statistics are used to predict what eden size will lead to the lowest overhead, where overhead, is a hybrid 
	 * between % of time spent in gc, and pgc pause times. The goal is to minimize % of time spent in gc,
	 * while staying below the specific gc pause time threshold
	 */

	uint64_t avgPgcTimeUs = _historicalPartialGCTime * 1000;
	/* 
	 * Since _averagePgcInterval measures from start of one PGC to the next, we subtract the avg PGC duration
	 * to get the avg time between end and start of consecutive PGC's
	 */
	uint64_t avgPgcIntervalUs = _averagePgcInterval - avgPgcTimeUs;
	uintptr_t currentIdealEdenSize = getIdealEdenSizeInBytes(env);
	uintptr_t currentHeapSize = _regionManager->getRegionSize() * _numberOfHeapRegions;
	
	double freeTenureHeadroom = 0.75;
	
	/*  
	 * _estimatedFreeTenure is free space outside of eden and survivor space, plus some additional headroom.
	 *  We add additional headroom so that we don't ever exhaust that free space
	 */
	uintptr_t freeTenure = OMR_MAX((uintptr_t)(_estimatedFreeTenure * freeTenureHeadroom), 1);

	if (0 == _totalGMPWorkTimeUs) {
		/* We haven't seen a GMP yet, so _estimatedFreeTenure will still be 0, which is not accurate. Use another estimate for free tenure until a GMP happens*/
		intptr_t freeTenureFromPGCInfo = (intptr_t)currentHeapSize - currentIdealEdenSize - _liveSetBytesAfterPartialCollect - (intptr_t)_averageSurvivorSetRegionCount;
		freeTenure = freeTenureFromPGCInfo > 0 ? freeTenureFromPGCInfo : 1;
	}

	Assert_MM_true(freeTenure != 0);

	/* Determine how far we can increase or decrease eden from where eden currently stands. */
	intptr_t minEdenChange = (intptr_t)currentIdealEdenSize * -1; 
	intptr_t maxEdenChange = (intptr_t)freeTenure;

	/* How many samples we want to test between minEdenChange and maxEdenChange? */
	uintptr_t numberOfSamples = 100;

	/* 
	 * Initially, we suggest the current eden size as the best size - until proven there is a better size.
	 * The "better" size, will have a better blend of gc overhead (% of time gc is active relative to mutator), 
	 * and more satisfactory pgc pause time (below target pgc pause is the goal).
	 */
	intptr_t recommendedEdenChange = 0;
	double currentCpuEdenOverhead = predictCpuOverheadForEdenSize(env, currentIdealEdenSize, recommendedEdenChange, freeTenure, avgPgcIntervalUs);
	double currentEdenHybridOverhead = calculateHybridEdenOverhead(env, _historicalPartialGCTime, currentCpuEdenOverhead);
	double bestOverheadPrediction = currentEdenHybridOverhead;

	/* How large the hops (in bytes) between samples should be */
	uintptr_t samplingGranularity = (uintptr_t)(maxEdenChange - minEdenChange) / numberOfSamples;

	/* Try "numberOfSamples" tests on the hybrid overhead curve, to determine which eden change will have best hybrid overhead */
	for (uintptr_t i = 0; i < numberOfSamples; i++) {
		/* Start from the right side of the curve */
		intptr_t edenChange = (intptr_t)maxEdenChange - (samplingGranularity * i);

		/* Predict what the pgc pause time, and gc overhead will be, if eden changes by 'edenChange' bytes*/
		double estimatedCpuOverhead = predictCpuOverheadForEdenSize(env, currentIdealEdenSize, edenChange, freeTenure, avgPgcIntervalUs);
		double estimatedPGCAvgTime = predictPgcTime(env, currentIdealEdenSize, edenChange);
		double estimatedHybridOverhead = calculateHybridEdenOverhead(env, (uintptr_t)estimatedPGCAvgTime / 1000, estimatedCpuOverhead);
		
		if (estimatedHybridOverhead < bestOverheadPrediction) {
			/* The hybrid between pgc pause time, and gc overhead (% time gc is active), is better than what was previously thought to be the best, save the eden size */
			recommendedEdenChange = edenChange;
			bestOverheadPrediction = estimatedHybridOverhead;
		} 
	}

	uintptr_t recommendedSize = currentIdealEdenSize + recommendedEdenChange;
	Trc_MM_SchedulingDelegate_calculateRecommendedEdenSize(env->getLanguageVMThread(), freeTenure, _totalGMPWorkTimeUs / 1000, avgPgcTimeUs, avgPgcIntervalUs, _edenSurvivalRateCopyForward, recommendedSize, bestOverheadPrediction);

	return recommendedSize;
}

double 
MM_SchedulingDelegate::predictCpuOverheadForEdenSize(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uintptr_t freeTenure, uint64_t pgcAvgIntervalTime)
{
	double predictedNumberOfCollections = predictNumberOfCollections(env, currentEdenSize, edenSizeChange, freeTenure);
	double predictedIntervalTime = predictIntervalBetweenCollections(env, currentEdenSize, edenSizeChange, pgcAvgIntervalTime);
	double predictedAvgPgcTime = predictPgcTime(env, currentEdenSize, edenSizeChange);
	
	uint64_t gmpTime = _totalGMPWorkTimeUs;
	if (0 == gmpTime) {
		/* GMP has not yet happened, so make a rough guess - but a high guess, so that eden thinks GMP is very expensive relative to PGC */
		gmpTime = 20 * _historicalPartialGCTime;
	}

	double gcActiveTime = (double)gmpTime + (predictedAvgPgcTime * predictedNumberOfCollections);
	double totalIntervalTime = (double)gmpTime + ((predictedAvgPgcTime + predictedIntervalTime) * predictedNumberOfCollections);

	double estimatedOverhead = gcActiveTime / totalIntervalTime;
	return estimatedOverhead;
}

double 
MM_SchedulingDelegate::predictIntervalBetweenCollections(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uint64_t pgcAvgIntervalTime)
{	
	/* The interval between PGC collections is proportional to eden size. Ex. If eden size doubles, we expect the interval between PGC collections to double as well */
	double intervalChange = (double)(currentEdenSize + edenSizeChange)/currentEdenSize;
	return (double)pgcAvgIntervalTime * intervalChange;
}

double 
MM_SchedulingDelegate::predictNumberOfCollections(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uintptr_t freeTenure)
{
	/* The number of PGC collections is proportional to how much free tenure will be left after we expand/contract eden */
	double collectionCountChange = (double)(freeTenure - edenSizeChange)/freeTenure;
	return (double)env->getRepresentativePgcPerGmpCount() * collectionCountChange;
}

double
MM_SchedulingDelegate::predictPgcTime(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange) 
{	
	/* 
	 * PGC avg time MAY be related to eden size. Certain applications/allocation patterns, will cause pgc time to increase as eden increases,
	 * while certain different workloads may keep pgc time relatively constant even as eden size increases. 
	 * Create a model to determine how pgc time will be afffected by eden size - keeping in mind that _pgcTimeIncreasePerEdenRegionFactor can vary depending on the application
	 */
	double edenRegionChange = (double)edenSizeChange / (double)_regionManager->getRegionSize();	
	double currentEdenRegions = (double)getCurrentEdenSizeInRegions(env);
	double edenChangeRatio = (edenRegionChange + currentEdenRegions + 1.0) / (currentEdenRegions + 1.0);

	/* Use a math workaround for "log base _pgcTimeIncreasePerEdenRegionFactor (edenChangeRatio) "*/
	double pgcTimeChangeForEdenChange = (log(edenChangeRatio) / log(_pgcTimeIncreasePerEdenRegionFactor));
	double predictedPgcTime = (double)_historicalPartialGCTime + pgcTimeChangeForEdenChange;

	/* If the prediction returned a value less than minimumPgcTime, then there may have been a small rounding mistake */
	predictedPgcTime = OMR_MAX(predictedPgcTime, (double)minimumPgcTime);

	/* Convert from ms to us */
	return predictedPgcTime * 1000;
}


uintptr_t
MM_SchedulingDelegate::estimateGlobalMarkIncrements(MM_EnvironmentVLHGC *env, double liveSetAdjustedForScannableBytesRatio) const
{
	Trc_MM_SchedulingDelegate_estimateGlobalMarkIncrements_Entry(env->getLanguageVMThread());

	/* we can consider liveSetAdjustedForScannableBytesRatio to be the total bytes the GMP needs to scan */
	Assert_MM_true(0 != _extensions->gcThreadCount);
	double estimatedScanMillis = liveSetAdjustedForScannableBytesRatio * _scanRateStats.microSecondsPerByteScanned / _extensions->gcThreadCount / 1000.0;
	uintptr_t currentMarkIncrementMillis = currentGlobalMarkIncrementTimeMillis(env);
	Assert_MM_true(0 != currentMarkIncrementMillis);
	double estimatedGMPIncrements = estimatedScanMillis / currentMarkIncrementMillis;
	Trc_MM_SchedulingDelegate_estimateGlobalMarkIncrements_liveSetBytes(env->getLanguageVMThread(), _liveSetBytesAfterPartialCollect, (uintptr_t)0, (uintptr_t)liveSetAdjustedForScannableBytesRatio);
	Trc_MM_SchedulingDelegate_estimateGlobalMarkIncrements_summary(env->getLanguageVMThread(), estimatedScanMillis, estimatedGMPIncrements);
	
	/* adding 1 increment for final GMP phase (most importantly clearable processing) */
	uintptr_t result = (uintptr_t)ceil(estimatedGMPIncrements) + 1;
	Trc_MM_SchedulingDelegate_estimateGlobalMarkIncrements_Exit(env->getLanguageVMThread(), result);
	return result;
}

uintptr_t
MM_SchedulingDelegate::getBytesToScanInNextGMPIncrement(MM_EnvironmentVLHGC *env) const
{
	uintptr_t targetPauseTimeMillis = currentGlobalMarkIncrementTimeMillis(env);
	double calculatedWorkTargetDouble = (((double)targetPauseTimeMillis * 1000.0) / _scanRateStats.microSecondsPerByteScanned) * (double)_extensions->gcThreadCount;

	/* minimum to UDATA_MAX in case we overflowed */
	uintptr_t calculatedWorkTarget = (uintptr_t) OMR_MIN(calculatedWorkTargetDouble, (double)UDATA_MAX);

	uintptr_t workTarget = OMR_MAX(calculatedWorkTarget, _extensions->tarokMinimumGMPWorkTargetBytes._valueSpecified);

	Trc_MM_SchedulingDelegate_getBytesToScanInNextGMPIncrement(env->getLanguageVMThread(), targetPauseTimeMillis, _scanRateStats.microSecondsPerByteScanned, _extensions->gcThreadCount, workTarget);

	return workTarget;
}

void 
MM_SchedulingDelegate::measureConsumptionForPartialGC(MM_EnvironmentVLHGC *env, uintptr_t currentReclaimableRegions, uintptr_t currentDefragmentReclaimableRegions)
{
	/* check to see if we have a valid previous data point */
	if (0 == _previousReclaimableRegions) {
		/* this must be the first PGC after a GMP. Since the GMP affected reclaimable memory, we have no reliable way to measure consumption for this cycle */ 
		Trc_MM_SchedulingDelegate_measureConsumptionForPartialGC_noPreviousData(env->getLanguageVMThread());
	} else {
		/* Use a signed number. The PGC may have negative consumption if it recovered more than an Eden-worth of memory, or if the estimates are a bit off */
		IDATA regionsConsumed = (IDATA)_previousReclaimableRegions - (IDATA)currentReclaimableRegions;
		const double historicWeight = 0.80; /* arbitrarily give 80% weight to historical result, 20% to newest result */
		_regionConsumptionRate = (_regionConsumptionRate * historicWeight) + (regionsConsumed * (1.0 - historicWeight));
		Trc_MM_SchedulingDelegate_measureConsumptionForPartialGC_consumptionRate(env->getLanguageVMThread(), regionsConsumed, _previousReclaimableRegions, currentReclaimableRegions, _regionConsumptionRate);
	}
	_previousReclaimableRegions = currentReclaimableRegions;

	/* check to see if we have a valid previous data point */
	if (0 == _previousDefragmentReclaimableRegions) {
		/* this must be the first PGC after a GMP. Since the GMP affected reclaimable memory, we have no reliable way to measure consumption for this cycle */
		Trc_MM_SchedulingDelegate_measureConsumptionForPartialGC_noPreviousData(env->getLanguageVMThread());
	} else {
		/* Use a signed number. The PGC may have negative consumption if it recovered more than an Eden-worth of memory, or if the estimates are a bit off */
		IDATA defragmentRegionsConsumed = (IDATA)_previousDefragmentReclaimableRegions - (IDATA)currentDefragmentReclaimableRegions;
		const double historicWeight = 0.80; /* arbitrarily give 80% weight to historical result, 20% to newest result */
		_defragmentRegionConsumptionRate = (_defragmentRegionConsumptionRate * historicWeight) + (defragmentRegionsConsumed * (1.0 - historicWeight));
		Trc_MM_SchedulingDelegate_measureConsumptionForPartialGC_defragmentConsumptionRate(env->getLanguageVMThread(), defragmentRegionsConsumed, _previousDefragmentReclaimableRegions, currentDefragmentReclaimableRegions, _defragmentRegionConsumptionRate);
	}
	_previousDefragmentReclaimableRegions = currentDefragmentReclaimableRegions;
}

uintptr_t
MM_SchedulingDelegate::estimatePartialGCsRemaining(MM_EnvironmentVLHGC *env) const
{
	Trc_MM_SchedulingDelegate_estimatePartialGCsRemaining_Entry(env->getLanguageVMThread(), _regionConsumptionRate, _previousDefragmentReclaimableRegions);

	uintptr_t partialCollectsRemaining = UDATA_MAX;
	if (_regionConsumptionRate > 0.0) {
		/* TODO: decide how to reconcile kick-off with dynamic Eden size */
		uintptr_t edenRegions = _idealEdenRegionCount;

		/* TODO:  This kick-off logic needs to be adapted to work with a dynamic mix of copy-forward and compact PGC increments.  For now, use the cycle state flags since they at least will let us test both code paths here. */
		if (env->_cycleState->_shouldRunCopyForward) {

			/* Calculate the number of regions that we need for copy forward destination */
			double survivorRegions = _averageSurvivorSetRegionCount;
			/* if _extensions->fvtest_forceCopyForwardHybridRatio is set(testing purpose), correct required survivor region count to avoid underestimating the remaining. */
			if ((0 != _extensions->fvtest_forceCopyForwardHybridRatio) && (100 >= _extensions->fvtest_forceCopyForwardHybridRatio)) {
				survivorRegions = survivorRegions * (100 - _extensions->fvtest_forceCopyForwardHybridRatio) / 100;
			}
			Trc_MM_SchedulingDelegate_estimatePartialGCsRemaining_survivorNeeds(env->getLanguageVMThread(), (uintptr_t)_averageSurvivorSetRegionCount, MM_GCExtensions::getExtensions(env)->tarokKickoffHeadroomInBytes, (uintptr_t)survivorRegions);

			double freeRegions = (double)((MM_GlobalAllocationManagerTarok *)_extensions->globalAllocationManager)->getFreeRegionCount();

			/* _previousReclaimableRegions has just been calculated (it's still current). It's a total (including free regions) */
			Assert_MM_true(_previousDefragmentReclaimableRegions >= freeRegions);
			double recoverableRegions = (double)_previousDefragmentReclaimableRegions - freeRegions;

			/* Copy PGC has compact selection goal work drive, so it optimistically relies on our projected compact work to indeed recover all reclaimable regions */
			if ((freeRegions + recoverableRegions) > (edenRegions + survivorRegions)) {
				partialCollectsRemaining = (uintptr_t)((freeRegions + recoverableRegions - edenRegions - survivorRegions) / _regionConsumptionRate);
			} else {
				partialCollectsRemaining = 0;
			}
		} else {
			/* MarkSweepCompact PGC has compact selection driven by free region goal, so it counts on reclaimable regions */
			if (_previousDefragmentReclaimableRegions > edenRegions) {
				partialCollectsRemaining = (uintptr_t)((double)(_previousDefragmentReclaimableRegions - edenRegions) / _regionConsumptionRate);
			} else {
				partialCollectsRemaining = 0;
			}
		}
	}

	Trc_MM_SchedulingDelegate_estimatePartialGCsRemaining_Exit(env->getLanguageVMThread(), partialCollectsRemaining);
	
	return partialCollectsRemaining;
}

void
MM_SchedulingDelegate::calculateHeapOccupancyTrend(MM_EnvironmentVLHGC *env)
{
	_previousLiveSetBytesAfterGlobalSweep = _liveSetBytesAfterGlobalSweep;
	_liveSetBytesAfterGlobalSweep = _liveSetBytesAfterPartialCollect;
	Trc_MM_SchedulingDelegate_calculateHeapOccupancyTrend_liveSetBytes(env->getLanguageVMThread(), _previousLiveSetBytesAfterGlobalSweep, _liveSetBytesBeforeGlobalSweep, _liveSetBytesAfterGlobalSweep);

	Assert_MM_true(_liveSetBytesAfterGlobalSweep <= _liveSetBytesAfterGlobalSweep);

	_heapOccupancyTrend = 1.0;
	if (0 != (_liveSetBytesBeforeGlobalSweep - _previousLiveSetBytesAfterGlobalSweep)) {
		_heapOccupancyTrend = ((double)_liveSetBytesAfterGlobalSweep - (double)_previousLiveSetBytesAfterGlobalSweep)
										/ ((double)_liveSetBytesBeforeGlobalSweep - (double)_previousLiveSetBytesAfterGlobalSweep);
	}
	
	Trc_MM_SchedulingDelegate_calculateHeapOccupancyTrend_heapOccupancy(env->getLanguageVMThread(), _heapOccupancyTrend);
}

void
MM_SchedulingDelegate::calculateScannableBytesRatio(MM_EnvironmentVLHGC *env)
{
	uintptr_t scannableBytes = 0;
	uintptr_t nonScannableBytes = 0;

	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			MM_MemoryPool *memoryPool = region->getMemoryPool();
			scannableBytes += memoryPool->getScannableBytes();
			nonScannableBytes += memoryPool->getNonScannableBytes();
		}
	}

	if (0 == (scannableBytes + nonScannableBytes)) {
		/* assume all is scannable */
		_scannableBytesRatio = 1.0;
	} else {
		_scannableBytesRatio = (double)scannableBytes / (double)(scannableBytes + nonScannableBytes);
	}
}

void
MM_SchedulingDelegate::recalculateRatesOnFirstPGCAfterGMP(MM_EnvironmentVLHGC *env)
{
	if (isFirstPGCAfterGMP()) {
		calculatePGCCompactionRate(env, getCurrentEdenSizeInRegions(env) * _regionManager->getRegionSize());
		calculateHeapOccupancyTrend(env);
		calculateScannableBytesRatio(env);

		firstPGCAfterGMPCompleted();
	}
}

double
MM_SchedulingDelegate::getAverageEmptinessOfCopyForwardedRegions()
{
	return ((_averageCopyForwardBytesCopied + _averageCopyForwardBytesDiscarded) > 0.0)
			? (_averageCopyForwardBytesDiscarded / (_averageCopyForwardBytesCopied + _averageCopyForwardBytesDiscarded))
			: 0.0;
}

double
MM_SchedulingDelegate::getDefragmentEmptinessThreshold(MM_EnvironmentVLHGC *env)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(env);
	double averageEmptinessofCopyForwardedRegions = getAverageEmptinessOfCopyForwardedRegions();
	double defragmentEmptinessThreshold = 0.0;

	if (extensions->tarokAutomaticDefragmentEmptinessThreshold) {
		defragmentEmptinessThreshold = OMR_MAX(_automaticDefragmentEmptinessThreshold, averageEmptinessofCopyForwardedRegions);
	} else {
		if (0.0 != _extensions->tarokDefragmentEmptinessThreshold) {
			/* Was set on the command line */
			defragmentEmptinessThreshold = _extensions->tarokDefragmentEmptinessThreshold;
		} else {
			defragmentEmptinessThreshold = averageEmptinessofCopyForwardedRegions;
		}
	}

	return defragmentEmptinessThreshold;

}

void
MM_SchedulingDelegate::updateHeapSizingData(MM_EnvironmentVLHGC *env) 
{	
	/* Determine how much space needs to be reserved for eden + survivor space */
	uintptr_t regionSize = _regionManager->getRegionSize();
	uintptr_t survivorSize = (uintptr_t)(regionSize * _averageSurvivorSetRegionCount);
	uintptr_t reservedFreeMemory =  getCurrentEdenSizeInBytes(env) + survivorSize;

	env->_heapSizingData.gmpTime = _totalGMPWorkTimeUs == 0 ? 1 : _totalGMPWorkTimeUs;
	env->_heapSizingData.pgcCountSinceGMPEnd = _pgcCountSinceGMPEnd;
	env->_heapSizingData.avgPgcTimeUs = _historicalPartialGCTime * 1000;

	/* After the first PGC, _averagePgcInterval will still be 0, so make a very rough estimate as to how big the interval between PGC's will be */
	env->_heapSizingData.avgPgcIntervalUs = _averagePgcInterval != 0 ? (_averagePgcInterval - (_historicalPartialGCTime * 1000)) : (_historicalPartialGCTime * 5);
	env->_heapSizingData.reservedSize = reservedFreeMemory;
	/* Note that env->_heapSizingData.freeTenure will be updated right before PGC begins, and should not be included here */
}

uintptr_t
MM_SchedulingDelegate::estimateTotalFreeMemory(MM_EnvironmentVLHGC *env, uintptr_t freeRegionMemory, uintptr_t defragmentedMemory, uintptr_t reservedFreeMemory)
{
	uintptr_t estimatedFreeMemory = 0;

	/* Adjust estimatedFreeMemory - we are only interested in area that shortfall can be fed from.
	 * Thus exclude reservedFreeMemory(Eden and Survivor size).
	 */
	estimatedFreeMemory = MM_Math::saturatingSubtract(defragmentedMemory + freeRegionMemory, reservedFreeMemory);

	Trc_MM_SchedulingDelegate_estimateTotalFreeMemory(env->getLanguageVMThread(), estimatedFreeMemory, reservedFreeMemory, defragmentedMemory, freeRegionMemory);
	return estimatedFreeMemory;
}

uintptr_t
MM_SchedulingDelegate::calculateKickoffHeadroom(MM_EnvironmentVLHGC *env, uintptr_t totalFreeMemory)
{
	if (_extensions->tarokForceKickoffHeadroomInBytes) {
		return _extensions->tarokKickoffHeadroomInBytes;
	}
	uintptr_t newHeadroom = totalFreeMemory * _extensions->tarokKickoffHeadroomRegionRate / 100;
	Trc_MM_SchedulingDelegate_calculateKickoffHeadroom(env->getLanguageVMThread(), _extensions->tarokKickoffHeadroomInBytes, newHeadroom);
	_extensions->tarokKickoffHeadroomInBytes = newHeadroom;
	return newHeadroom;
}

uintptr_t
MM_SchedulingDelegate::initializeKickoffHeadroom(MM_EnvironmentVLHGC *env)
{
	/* total free memory = total heap size - eden size */
	uintptr_t totalFreeMemory = _regionManager->getTotalHeapSize() - getCurrentEdenSizeInBytes(env);
	return calculateKickoffHeadroom(env, totalFreeMemory);
}

void
MM_SchedulingDelegate::calculatePGCCompactionRate(MM_EnvironmentVLHGC *env, uintptr_t edenSizeInBytes)
{
	/* Ideally, copy-forwarded regions should be 100% full (i.e. 0% empty), but there are inefficiencies due to parallelism and compact groups.
	 * We measure this so that we can detect regions which are unlikely to become less empty if we copy-and-forward them.
	 */
	const double defragmentEmptinessThreshold = getDefragmentEmptinessThreshold(env);
	Assert_MM_true( (defragmentEmptinessThreshold >= 0.0) && (defragmentEmptinessThreshold <= 1.0) );
	const uintptr_t regionSize = _regionManager->getRegionSize();

	uintptr_t totalLiveDataInCollectableRegions = 0;
	uintptr_t totalLiveDataInNonCollectibleRegions = 0;
	uintptr_t fullyCompactedData = 0;

	uintptr_t freeMemoryInCollectibleRegions = 0;
	uintptr_t freeMemoryInNonCollectibleRegions = 0;
	uintptr_t freeMemoryInFullyCompactedRegions = 0;
	uintptr_t freeRegionMemory = 0;

	uintptr_t collectibleRegions = 0;
	uintptr_t nonCollectibleRegions = 0;
	uintptr_t freeRegions = 0;
	uintptr_t fullyCompactedRegions = 0;

	uintptr_t estimatedFreeMemory = 0;
	uintptr_t defragmentedMemory = 0;

	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	MM_HeapRegionDescriptorVLHGC *region = NULL;

	while (NULL != (region = regionIterator.nextRegion())) {
		region->_defragmentationTarget = false;
		if (region->containsObjects()) {
			Assert_MM_true(region->_sweepData._alreadySwept);
			uintptr_t freeMemory = region->getMemoryPool()->getFreeMemoryAndDarkMatterBytes();
			if (!region->getRememberedSetCardList()->isAccurate()) {
				/* Overflowed regions or those that RSCL is being rebuilt will not be compacted */
				nonCollectibleRegions += 1;
				freeMemoryInNonCollectibleRegions += freeMemory;
				totalLiveDataInNonCollectibleRegions += (regionSize - freeMemory);
			} else {
				double emptiness = (double)freeMemory / (double)regionSize;
				Assert_MM_true( (emptiness >= 0.0) && (emptiness <= 1.0) );

				/* Only consider regions which are likely to become more dense if we copy-and-forward them */
				if (emptiness > defragmentEmptinessThreshold) {
					collectibleRegions += 1;
					freeMemoryInCollectibleRegions += freeMemory;
					/* see ReclaimDelegate::deriveCompactScore() for an explanation of potentialWastedWork */
					uintptr_t compactGroup = MM_CompactGroupManager::getCompactGroupNumber(env, region);
					double weightedSurvivalRate = MM_GCExtensions::getExtensions(env)->compactGroupPersistentStats[compactGroup]._weightedSurvivalRate;
					double potentialWastedWork = (1.0 - weightedSurvivalRate) * (1.0 - emptiness);

					/* the probability that we'll recover the free memory is determined by the potential gainful work, so use that determine how much memory we're likely to actually compact */
					defragmentedMemory += (uintptr_t)((double)freeMemory * (1.0 - potentialWastedWork));
					totalLiveDataInCollectableRegions += (uintptr_t)((double)(regionSize - freeMemory) * (1.0 - potentialWastedWork));
					region->_defragmentationTarget = true;

				} else {
					/* if method calculatePGCCompactionRate() is called right after the sweep before PGC(the first PGC after GMP), half of Eden regions were allocated after the final GMP, those Eden regions didn't have been marked, they would be showed as fullyCompacted regions */
					fullyCompactedRegions += 1;
					freeMemoryInFullyCompactedRegions += freeMemory;
					fullyCompactedData += (regionSize - freeMemory);
				}
			}
		} else if (region->isFreeOrIdle()) {
			freeRegions += 1;
			freeRegionMemory += regionSize;
		}
	}

	/* Survivor space needs to accommodate for Nursery set, Dynamic collection set and Compaction set
	 */
	/* estimate totalFreeMemory for recalculating kickoffHeadroomRegionCount */	 
	uintptr_t surivivorSize = (uintptr_t)(regionSize * _averageSurvivorSetRegionCount);
	uintptr_t reservedFreeMemory = edenSizeInBytes + surivivorSize;
	estimatedFreeMemory = estimateTotalFreeMemory(env, freeRegionMemory, defragmentedMemory, reservedFreeMemory);
	calculateKickoffHeadroom(env, estimatedFreeMemory);

	/* estimate totalFreeMemory for recalculating PGCCompactionRate with tarokKickoffHeadroomInBytes */
	reservedFreeMemory += _extensions->tarokKickoffHeadroomInBytes;
	estimatedFreeMemory = estimateTotalFreeMemory(env, freeRegionMemory, defragmentedMemory, reservedFreeMemory);
	/* Remeber the total free memory estimate, so it can be used to calculate how big eden should be */
	_estimatedFreeTenure = estimatedFreeMemory;

	double bytesDiscardedPerByteCopied = (_averageCopyForwardBytesCopied > 0.0) ? (_averageCopyForwardBytesDiscarded / _averageCopyForwardBytesCopied) : 0.0;
	double estimatedFreeMemoryDiscarded = (double)totalLiveDataInCollectableRegions * bytesDiscardedPerByteCopied;
	double recoverableFreeMemory = (double)estimatedFreeMemory - estimatedFreeMemoryDiscarded;

	if (0.0 < recoverableFreeMemory) {
		_bytesCompactedToFreeBytesRatio = ((double)totalLiveDataInCollectableRegions)/recoverableFreeMemory;
	} else {
		_bytesCompactedToFreeBytesRatio = (double)(_regionManager->getTableRegionCount() + 1);
	}

	Trc_MM_SchedulingDelegate_calculatePGCCompactionRate_liveToFreeRatio1(env->getLanguageVMThread(), (totalLiveDataInCollectableRegions + totalLiveDataInNonCollectibleRegions + fullyCompactedData), totalLiveDataInCollectableRegions, totalLiveDataInNonCollectibleRegions, fullyCompactedData);
	Trc_MM_SchedulingDelegate_calculatePGCCompactionRate_liveToFreeRatio2(env->getLanguageVMThread(), (freeMemoryInCollectibleRegions + freeMemoryInNonCollectibleRegions + freeRegionMemory), freeMemoryInCollectibleRegions, freeMemoryInNonCollectibleRegions, freeRegionMemory, freeMemoryInFullyCompactedRegions);
	Trc_MM_SchedulingDelegate_calculatePGCCompactionRate_liveToFreeRatio3(env->getLanguageVMThread(), (collectibleRegions + nonCollectibleRegions + fullyCompactedRegions + freeRegions), collectibleRegions, nonCollectibleRegions, fullyCompactedRegions, freeRegions);
	Trc_MM_SchedulingDelegate_calculatePGCCompactionRate_liveToFreeRatio4(env->getLanguageVMThread(), _bytesCompactedToFreeBytesRatio, edenSizeInBytes, surivivorSize, reservedFreeMemory, defragmentEmptinessThreshold, defragmentedMemory, estimatedFreeMemory);
}

uintptr_t
MM_SchedulingDelegate::getDesiredCompactWork()
{
	/* compact work (mostly) driven by M/S from GMP */
	uintptr_t desiredCompactWork = (uintptr_t)(_bytesCompactedToFreeBytesRatio * OMR_MAX(0.0, _regionConsumptionRate) * _regionManager->getRegionSize());
	
	/* defragmentation work (mostly) driven by compact group merging (maxAge - 1 into maxAge) */
	desiredCompactWork += (uintptr_t)_averageMacroDefragmentationWork;

	return desiredCompactWork;
}

bool
MM_SchedulingDelegate::isFirstPGCAfterGMP()
{
	return _didGMPCompleteSinceLastReclaim;
}

void
MM_SchedulingDelegate::firstPGCAfterGMPCompleted()
{
	_didGMPCompleteSinceLastReclaim = false;
}

void
MM_SchedulingDelegate::copyForwardCompleted(MM_EnvironmentVLHGC *env)
{
	MM_CopyForwardStats * copyForwardStats = &(static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats);
	uintptr_t bytesCopied = copyForwardStats->_copyBytesTotal;
	uintptr_t bytesDiscarded = copyForwardStats->_copyDiscardBytesTotal;
	uintptr_t bytesScanned = copyForwardStats->_scanBytesTotal;
	uintptr_t bytesCompacted = copyForwardStats->_externalCompactBytes;
	uintptr_t regionSize = _regionManager->getRegionSize();
	double copyForwardRate = calculateAverageCopyForwardRate(env);
	
	const double historicWeight = 0.50; /* arbitrarily give 50% weight to historical result, 50% to newest result */
	_averageCopyForwardBytesCopied = (_averageCopyForwardBytesCopied * historicWeight) + ((double)bytesCopied * (1.0 - historicWeight));
	_averageCopyForwardBytesDiscarded = (_averageCopyForwardBytesDiscarded * historicWeight) + ((double)bytesDiscarded * (1.0 - historicWeight));

	/* calculate the number of additional regions which would have been required to complete the copy-forward without aborting */
	uintptr_t failedEvacuateRegionCount = (bytesScanned + regionSize - 1) / regionSize;
	uintptr_t compactSetSurvivorRegionCount = (bytesCompacted + regionSize - 1) / regionSize;
	uintptr_t survivorSetRegionCount = env->_cycleState->_pgcData._survivorSetRegionCount + failedEvacuateRegionCount + compactSetSurvivorRegionCount;
	
	_averageSurvivorSetRegionCount = (_averageSurvivorSetRegionCount * historicWeight) + ((double)survivorSetRegionCount * (1.0 - historicWeight));
	_averageCopyForwardRate = (_averageCopyForwardRate * historicWeight) + (copyForwardRate * (1.0 - historicWeight));

	Trc_MM_SchedulingDelegate_copyForwardCompleted_efficiency(
		env->getLanguageVMThread(),
		bytesCopied,
		bytesDiscarded,
		(double)bytesDiscarded / (double)(bytesCopied + bytesDiscarded),
		_averageCopyForwardBytesCopied,
		_averageCopyForwardBytesDiscarded,
		_averageCopyForwardBytesDiscarded / (_averageCopyForwardBytesCopied + _averageCopyForwardBytesDiscarded),
		survivorSetRegionCount,
		failedEvacuateRegionCount,
		compactSetSurvivorRegionCount,
		_averageSurvivorSetRegionCount,
		copyForwardRate,
		_averageCopyForwardRate
		);
}

double
MM_SchedulingDelegate::calculateAverageCopyForwardRate(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CopyForwardStats * copyForwardStats = &(static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats);
	uintptr_t bytesCopied = copyForwardStats->_copyBytesTotal;
	uint64_t timeSpentReferenceClearing = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._irrsStats._clearFromRegionReferencesTimesus;
	uint64_t timeSpentInCopyForward = j9time_hires_delta(copyForwardStats->_startTime, copyForwardStats->_endTime, J9PORT_TIME_DELTA_IN_MICROSECONDS);

	double copyForwardRate = 0.0;
	if (timeSpentInCopyForward > timeSpentReferenceClearing) {
		/* theoretically could happen with clock skew */
		copyForwardRate = (double)bytesCopied / ((double)(timeSpentInCopyForward - timeSpentReferenceClearing));
	} else {
		if (0 != timeSpentInCopyForward) {
			/* Ignore time spent in clearing then */
			copyForwardRate = (double)bytesCopied / ((double)timeSpentInCopyForward);
		} else {
			/* Since timeSpentInCopyForward seems to be less than a microsecond, just use the number of bytes we copied as an underestimate */
			copyForwardRate = (double)bytesCopied;
		}
	}

	return copyForwardRate;
}

void
MM_SchedulingDelegate::calculateAutomaticGMPIntermission(MM_EnvironmentVLHGC *env)
{
	Trc_MM_SchedulingDelegate_calculateAutomaticGMPIntermission_Entry(env->getLanguageVMThread(), _extensions->tarokAutomaticGMPIntermission ? "true" : "false", _remainingGMPIntermissionIntervals);
	
	/* call these even if automatic intermissions aren't enabled, so that we get the trace data. This is useful for debugging */
	uintptr_t partialCollectsRemaining = estimatePartialGCsRemaining(env);
	updateLiveBytesAfterPartialCollect();
	
	if (_extensions->tarokAutomaticGMPIntermission) {
		/* we assume that the default value is MAX when automatic intermissions are enabled */
		Assert_MM_true(UDATA_MAX == _extensions->tarokGMPIntermission);
		
		/* if we haven't kicked off yet, recalculate the intermission until kick-off based on current estimates */
		if (_remainingGMPIntermissionIntervals > 0) {
			double liveSetAdjustedForScannableBytesRatio = calculateEstimatedGlobalBytesToScan();
			uintptr_t incrementHeadroom = calculateGlobalMarkIncrementHeadroom(env);
			uintptr_t globalMarkIncrementsRequired = estimateGlobalMarkIncrements(env, liveSetAdjustedForScannableBytesRatio);
			uintptr_t globalMarkIncrementsRequiredWithHeadroom = globalMarkIncrementsRequired + incrementHeadroom;
			uintptr_t globalMarkIncrementsRemaining = partialCollectsRemaining * _extensions->tarokPGCtoGMPDenominator / _extensions->tarokPGCtoGMPNumerator;
			_remainingGMPIntermissionIntervals = MM_Math::saturatingSubtract(globalMarkIncrementsRemaining, globalMarkIncrementsRequiredWithHeadroom);
		}
	}

	Trc_MM_SchedulingDelegate_calculateAutomaticGMPIntermission_1_Exit(env->getLanguageVMThread(), _remainingGMPIntermissionIntervals, _extensions->tarokKickoffHeadroomInBytes);
}

void
MM_SchedulingDelegate::updateSurvivalRatesAfterCopyForward(double thisEdenSurvivalRate, uintptr_t thisNonEdenSurvivorCount)
{
	/* Note that this weight value is currently arbitrary */
	double historicalWeight = 0.5;
	double newWeight = 1.0 - historicalWeight;
	_edenSurvivalRateCopyForward =  (historicalWeight * _edenSurvivalRateCopyForward) + (newWeight * thisEdenSurvivalRate);
	_nonEdenSurvivalCountCopyForward = (uintptr_t)((historicalWeight * _nonEdenSurvivalCountCopyForward) + (newWeight * thisNonEdenSurvivorCount));
}

void 
MM_SchedulingDelegate::calculateEdenSize(MM_EnvironmentVLHGC *env)
{
	uintptr_t regionSize = _regionManager->getRegionSize();
	uintptr_t previousEdenSize = _edenRegionCount * regionSize;
	Trc_MM_SchedulingDelegate_calculateEdenSize_Entry(env->getLanguageVMThread(), previousEdenSize);
	
	MM_GlobalAllocationManagerTarok *globalAllocationManager = (MM_GlobalAllocationManagerTarok *)_extensions->globalAllocationManager;
	uintptr_t freeRegions = globalAllocationManager->getFreeRegionCount();
	
	/* Eden sizing logic may have suggested a change to eden size. Apply those changes, while still respecting -Xmns/-Xmnx, and (_max/_min)EdenPercent */
	adjustIdealEdenRegionCount(env);

	uintptr_t edenMinimumCount = _minimumEdenRegionCount;
	uintptr_t edenMaximumCount = _idealEdenRegionCount;

	Assert_MM_true(edenMinimumCount >= 1);
	Assert_MM_true(edenMaximumCount >= 1);
	Assert_MM_true(edenMaximumCount >= edenMinimumCount);

	uintptr_t desiredEdenCount = freeRegions;
	if (desiredEdenCount > edenMaximumCount) {
		desiredEdenCount = edenMaximumCount;
	} else if (desiredEdenCount < edenMinimumCount) {
		desiredEdenCount = edenMinimumCount;
	}
	Trc_MM_SchedulingDelegate_calculateEdenSize_dynamic(env->getLanguageVMThread(), desiredEdenCount, _edenSurvivalRateCopyForward, _nonEdenSurvivalCountCopyForward, freeRegions, edenMinimumCount, edenMaximumCount);
	if (desiredEdenCount <= freeRegions) {
		_edenRegionCount = desiredEdenCount;
	} else {
		/* there isn't enough memory left for a desired Eden. Allow Eden to shrink to free size(could be less than minimum size or 0) before
		 * triggering an allocation failure collection (i.e. a global STW collect)
		 */ 
		_edenRegionCount = freeRegions;
		Trc_MM_SchedulingDelegate_calculateEdenSize_reduceToFreeBytes(env->getLanguageVMThread(), desiredEdenCount, _edenRegionCount);
	}
	Trc_MM_SchedulingDelegate_calculateEdenSize_Exit(env->getLanguageVMThread(), (_edenRegionCount * regionSize));
}

void
MM_SchedulingDelegate::moveTowardRecommendedEden(MM_EnvironmentVLHGC *env, double edenChangeSpeed) 
{
	Assert_MM_true(edenChangeSpeed <= 1 && edenChangeSpeed >= 0);

	if ((0 == _historicalPartialGCTime) || (0 == _averagePgcInterval)) {
		/* Until we have collected any information about PGC time, we don't have the data we need to make informed decision about eden size  */
		return;
	}

	uintptr_t recommendedEdenSizeBytes = calculateRecommendedEdenSize(env);

	uintptr_t currentIdealEdenBytes = getIdealEdenSizeInBytes(env);
	uintptr_t currentIdealEdenRegions = _idealEdenRegionCount;

	/* 
	 * The closer edenChangeSpeed is to 1, the larger the move towards recommendedEdenSizeBytes will be. 
	 * 1 implies that eden should move all the way towards recommendedEdenSizeBytes.
	 */
	intptr_t edenChange = (intptr_t)recommendedEdenSizeBytes - currentIdealEdenBytes;
	intptr_t targetEdenChange = (intptr_t)(edenChange * edenChangeSpeed);
	uintptr_t targetEdenBytes = currentIdealEdenBytes + targetEdenChange;
	uintptr_t targetEdenRegions = targetEdenBytes / _regionManager->getRegionSize();

	_edenSizeFactor = (intptr_t)targetEdenRegions - currentIdealEdenRegions;
}

void
MM_SchedulingDelegate::checkEdenSizeAfterPgc(MM_EnvironmentVLHGC *env, bool globalSweepHappened)
{
	if (!_extensions->statupPhaseFinished) {
		/* Don't change eden size during startup phase - keep it at default */
		return;
	}

	if (_currentlyPerformingGMP && !globalSweepHappened) {
		/* 
		 * Don't change eden size while GMP cycle is running - 
		 * Unless a global sweep just happened, in which case we allow eden to change on first PGC after GMP 
		 */
		return;
	}

	if (heapIsFullyExpanded(env)) {
		/* 
		 * When heap is full, try to get eden to such a size to minimize overhead, while keeping into account the limits imposed by
		 * the heap size, and how much free space is available, since "blindly" increasing eden is no longer an option
		 */
		if (globalSweepHappened) {
			/* 
			 * Take a more aggreessive step towards ideal eden. 
			 * At this point we have the most accuate information about liveness in the heap, so we make the most informed decision
			 */
			moveTowardRecommendedEden(env, 0.5);
			resetPgcTimeStatistics(env);
		} else if ((_pgcCountSinceGMPEnd & (consecutivePGCToChangeEden - 1)) == 0) {
			/* Every consecutivePGCToChangeEden number of pgc's, re-evaluate eden size, and move towards it */
			moveTowardRecommendedEden(env, 0.25);
		}
	} else if (0 == _pgcCountSinceGMPEnd % 3) {
		/*
		 * On every third pgc, make an adjustment to eden size based on observed pgc values. 
		 * Waiting for every third PGC, allows some statistics (which are being averaged) to settle to their new true averages
 		 */

		intptr_t edenRegionChange = 0;
		intptr_t edenChangeMagnitude = (intptr_t)ceil((0.1 * getIdealEdenSizeInBytes(env)) / _regionManager->getRegionSize());

		double hybridEdenOverhead = calculateHybridEdenOverhead(env, _historicalPartialGCTime, _partialGcOverhead);
		
		/* 
		 * Aim to get hybrid PGC overhead between extensions->dnssExpectedTimeRatioMinimum and extensions->dnssExpectedTimeRatioMaximum 
		 * by increasing or decreasing eden by 10% 
		 */
		if (_extensions->dnssExpectedTimeRatioMinimum._valueSpecified > hybridEdenOverhead ) {
			/* Shrink eden a bit */
			edenRegionChange = edenChangeMagnitude * -1;
		} else if (_extensions->dnssExpectedTimeRatioMaximum._valueSpecified < hybridEdenOverhead) {
			/* Expand eden a bit */
			edenRegionChange = edenChangeMagnitude;
		}

		_edenSizeFactor += edenRegionChange;	
	}
}

double 
MM_SchedulingDelegate::mapPgcTimeToPgcOverhead(MM_EnvironmentVLHGC *env, uintptr_t partialGcTimeMs) {
	
	/* Convert expectedTimeRatioMinimum/Maximum to 0-100 based for this formula */
	double xminpct = _extensions->dnssExpectedTimeRatioMinimum._valueSpecified * 100;
	double xmaxpct = _extensions->dnssExpectedTimeRatioMaximum._valueSpecified * 100;
	double xmaxt = (double)_extensions->tarokTargetMaxPauseTime;

	double overhead;

	if (heapIsFullyExpanded(env)) {
		/* 
		 * Eden size is being driven by heuristic which is trying to MINIMIZE hybrid overead. A low avg pgc time, is more desirable than high avg time,
		 * So the overhead logic needs to map a low avg pgc time, to a low overhead value (aka, a "better"/more desirable value)
		 * Ex 20ms -> 5% (good/desirable), 1000ms -> 80% (bad/undesirable/eden should probably shrink)
		 */
		double midpointPct = (xmaxpct + xminpct)/2.0;
		if (partialGcTimeMs <= xmaxt) {
			/* Once the pgc time is at, or below the max pgc time, there is no "benefit" from shrinking it further, since we are already satisfying tarokTargetMaxPauseTime */
			overhead = midpointPct;
		} else {
			/* 
			 * If pgc time is above the max pgc time, map high PGC time values as very very high overhead, in efforts to bring the PGC time down to tarokTargetMaxPauseTime
			 * If pgc time is only slightly above tarokTargetMaxPauseTime, then there is only a very small overhead penalty, 
			 * wheras being 2x higher than the target pause time leads to a significantly bigger penalty  
			 */
			double overheadCurve = pow(1.03, ((double)partialGcTimeMs - xmaxt)) + midpointPct - 1;
			overhead = OMR_MIN(100.0, overheadCurve);
		}
		
	} else {
		/* 
		 * Eden sizing logic is trying to keep hybrid overhead between xminpct and xmaxpct, while trying to respect xmaxt. 
		 * In this situation, when pgc times are very high (above xmaxt), the overhead score needs to return a low number, suggesting contraction.
		 * If partialGcTimeMs is less than half of xmaxt, eden can expand without any fear of getting close to xmaxt - the mapped cpu overhead here is > xmaxpct (suggesting eden expansion)
		 * Ex: 20ms -> 12% (suggest expansion), 2000ms -> 0.00% (suggest contraction)
		 */
		double slope = (xmaxpct - xminpct) / ((xmaxt/2) - xmaxt);
		overhead = (slope * (double)partialGcTimeMs) + ((2.0 * xmaxpct) - xminpct);
		overhead = OMR_MAX(overhead, 0.0);	
		/*  Expanding simply because pgc time is very small is not a good idea, so return xmaxpct, so that if the pgc cpu overhead wants to expand, only then eden expands */
		overhead = OMR_MIN(overhead, xmaxpct);
	}

	return overhead;
}

double 
MM_SchedulingDelegate::calculateHybridEdenOverhead(MM_EnvironmentVLHGC *env, uintptr_t partialGcTimeMs, double overhead)
{
	/* 
	 * When trying to size eden, there is a delicate balance between pgc overhead (here, overhead is cpu %, or % of time that pgc is active 
	 * versus inactive -> ex: pgc = 100ms, over 1000ms, overhead = 10%). In certain applications, with certain allocation patterns/liveness, 
	 * pgc average time may be negatively impacted by growing eden unbounded. 
	 * This function blends the pgc average time (whether it be the actual pgc historic time, or a "predicted" pgc pause time, is left up to the caller) with overhead (% of time gc is active relative to mutator).
	 * This strikes a much better balance between pgc pause times, and gc cpu overhead, than if just cpu overhead was used.
	 * 
	 * By mapping a pgc time to a corresponding overhead (% of time gc is active relative to mutator), eden sizing logic can make a decision as to whether 
	 * it wants to contract/expand, based on how much it will change the overhead and pgc times.
	 */
	double actualPGCOverheadWeight = 0.5;
	assert(overhead >= 0.0 && overhead <= 1.0);
	double pgcTimeOverhead = mapPgcTimeToPgcOverhead(env, partialGcTimeMs);
	double hybridEdenOverheadHundredBased = (actualPGCOverheadWeight * (overhead * 100)) + ((1- actualPGCOverheadWeight) * pgcTimeOverhead);
	return hybridEdenOverheadHundredBased / 100;	
}

void 
MM_SchedulingDelegate::adjustIdealEdenRegionCount(MM_EnvironmentVLHGC *env) 
{

	intptr_t edenChange = _edenSizeFactor;
	/* Be clear that we have already consumed _edenSizeFactor */
	_edenSizeFactor = 0;

	if (!_extensions->statupPhaseFinished) {
		/* If currently in startup phase, eden size is being driven by a different set of heuristics - see MM_SchedulingDelegate::heapReconfigured() */
		return;
	}

	uintptr_t maxEdenCount = (uintptr_t)(_numberOfHeapRegions * _maxEdenPercent);
	uintptr_t minEdenCount = (uintptr_t)(_numberOfHeapRegions * _minEdenPercent);

	/* If there are any user specific eden sizing options, these take precendence over _maxEdenPercent and  _minEdenPercent */
	if (_extensions->userSpecifiedParameters._Xmn._wasSpecified || _extensions->userSpecifiedParameters._Xmns._wasSpecified) {
		minEdenCount = _extensions->tarokIdealEdenMinimumBytes / _regionManager->getRegionSize();
	}
	if (_extensions->userSpecifiedParameters._Xmn._wasSpecified || _extensions->userSpecifiedParameters._Xmnx._wasSpecified) {
		maxEdenCount = _extensions->tarokIdealEdenMaximumBytes / _regionManager->getRegionSize();
	}

	/* Do not allow eden to grow/shrink past the min/max eden count */
	intptr_t possibleEdenRegionCount = (intptr_t)_idealEdenRegionCount + edenChange;
	if ((intptr_t)minEdenCount > possibleEdenRegionCount) {
		edenChange = minEdenCount - _idealEdenRegionCount;
	} else if ((intptr_t)maxEdenCount < possibleEdenRegionCount){
		edenChange = maxEdenCount - _idealEdenRegionCount;
	}

	Trc_MM_SchedulingDelegate_adjustIdealEdenRegionCount(env->getLanguageVMThread(), minEdenCount, maxEdenCount, _idealEdenRegionCount, edenChange);

	/* Inform the _idealEdenRegionCount that we need to change from current value. If there are not enough free regions, then eden will only as big as the amount of free regions */
	_idealEdenRegionCount += edenChange;
	
	/* Make sure we request at least 1 eden region as max */
	_idealEdenRegionCount = OMR_MAX(1, _idealEdenRegionCount);
	/* Make sure Min <= Max */
	_minimumEdenRegionCount = OMR_MIN(_minimumEdenRegionCount, _idealEdenRegionCount);
}

bool
MM_SchedulingDelegate::heapIsFullyExpanded(MM_EnvironmentVLHGC *env)
{
	/* 
	 * If the heap is the size of softmx or larger, eden should use heuristic that looks at free memory, rather than PGC overhead, 
	 * since there are now free memory constraints eden must be aware of
	 */
	uintptr_t currentHeapSize = _regionManager->getRegionSize() * _numberOfHeapRegions;
	uintptr_t maxHeapSize = _extensions->softMx == 0 ? _extensions->memoryMax : _extensions->softMx;
	return currentHeapSize >= maxHeapSize;
}

uintptr_t
MM_SchedulingDelegate::currentGlobalMarkIncrementTimeMillis(MM_EnvironmentVLHGC *env) const
{
	uintptr_t markIncrementMillis = 0;
	
	if (0 == _extensions->tarokGlobalMarkIncrementTimeMillis) {
		uintptr_t partialCollectsRemaining = estimatePartialGCsRemaining(env);

		if (0 == partialCollectsRemaining) {
			/* We're going to AF very soon so we need to finish the GMP this increment.  Set current global mark increment time to max */
			markIncrementMillis = UDATA_MAX;
		} else {
			uintptr_t desiredGlobalMarkIncrementMillis = _dynamicGlobalMarkIncrementTimeMillis;
			double remainingMillisToScan = estimateRemainingTimeMillisToScan();
			uintptr_t minimumGlobalMarkIncrementMillis = (uintptr_t) (remainingMillisToScan / (double)partialCollectsRemaining);

			markIncrementMillis = OMR_MAX(desiredGlobalMarkIncrementMillis, minimumGlobalMarkIncrementMillis);
		}
	} else {
		markIncrementMillis = _extensions->tarokGlobalMarkIncrementTimeMillis;
	}
	Trc_MM_SchedulingDelegate_currentGlobalMarkIncrementTimeMillis_summary(env->getLanguageVMThread(), markIncrementMillis);
	
	return markIncrementMillis;
}

uintptr_t
MM_SchedulingDelegate::getCurrentEdenSizeInBytes(MM_EnvironmentVLHGC *env)
{
	return (_edenRegionCount * _regionManager->getRegionSize());
}

uintptr_t
MM_SchedulingDelegate::getIdealEdenSizeInBytes(MM_EnvironmentVLHGC *env)
{
	return (_idealEdenRegionCount * _regionManager->getRegionSize());
}

uintptr_t
MM_SchedulingDelegate::getCurrentEdenSizeInRegions(MM_EnvironmentVLHGC *env)
{
	return _edenRegionCount;
}

void
MM_SchedulingDelegate::heapReconfigured(MM_EnvironmentVLHGC *env)
{
	uintptr_t edenMaximumBytes = _extensions->tarokIdealEdenMaximumBytes;
	uintptr_t edenMinimumBytes = _extensions->tarokIdealEdenMinimumBytes;
	Trc_MM_SchedulingDelegate_heapReconfigured_Entry(env->getLanguageVMThread(), edenMaximumBytes, edenMinimumBytes);
	
	uintptr_t regionSize = _regionManager->getRegionSize();
	_numberOfHeapRegions = 0;
	
	/* walk the managed regions (skipping cold area) to determine how large the managed heap is */
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	while (NULL != regionIterator.nextRegion()) {
		_numberOfHeapRegions += 1;
	}
	uintptr_t currentHeapSize = _numberOfHeapRegions * regionSize;
	/* since the heap is allowed to be one region less than the size requested (due to "acceptLess" in Virtual Memory), make sure that we consider the "reachable minimum" to be the real minimum heap size */
	uintptr_t minimumHeap = OMR_MIN(_extensions->initialMemorySize, currentHeapSize);
	uintptr_t edenIdealBytes = 0;
	uintptr_t maximumHeap = _extensions->memoryMax;
	if (_extensions->statupPhaseFinished) {
		/* The eden size is currently being driven by GC overhead and time - Keep eden size the same. If eden needs to change, it will change elsewhere */
		edenIdealBytes = getIdealEdenSizeInBytes(env);
	} else if (currentHeapSize == maximumHeap) {
		/* we are fully expanded or mx == ms so just return the maximum ideal eden */
		edenIdealBytes = edenMaximumBytes;
	} else {
		/* interpolate between the maximum and minimum */
		/* This logic follows the formula given in JAZZ 39694
		 * for:  -XmsA -XmxB -XmnsC -XmnxD, "current heap size" W, "current Eden size" Z:
		 * Z := C + ((W-A)/(B-A))(D-C)
		 * 
		 * If heap is fully expanded, eden bytes will be edenMaximumBytes
		 */
		uintptr_t heapBytesOverMinimum = currentHeapSize - minimumHeap;
		uintptr_t maximumHeapVariation = maximumHeap - minimumHeap;
		/* if this is 0, we should have taken the else if */
		Assert_MM_true(0 != maximumHeapVariation);
		double ratioOfHeapExpanded = ((double)heapBytesOverMinimum) / ((double)maximumHeapVariation);
		uintptr_t maximumEdenVariation = edenMaximumBytes - edenMinimumBytes;
		uintptr_t edenLinearScale = (uintptr_t)(ratioOfHeapExpanded * (double)maximumEdenVariation);
		edenIdealBytes = edenMinimumBytes + edenLinearScale;
	}

	_idealEdenRegionCount = (edenIdealBytes + regionSize - 1) / regionSize;

	Assert_MM_true(_idealEdenRegionCount > 0);
	_minimumEdenRegionCount = OMR_MIN(_idealEdenRegionCount, ((MM_GlobalAllocationManagerTarok *)_extensions->globalAllocationManager)->getManagedAllocationContextCount());
	Assert_MM_true(_minimumEdenRegionCount > 0);

	Trc_MM_SchedulingDelegate_heapReconfigured_Exit(env->getLanguageVMThread(), _numberOfHeapRegions, _idealEdenRegionCount, _minimumEdenRegionCount);
	Assert_MM_true(_idealEdenRegionCount >= _minimumEdenRegionCount);
	
	/* recalculate Eden Size after resize heap */
	calculateEdenSize(env);
}

uintptr_t
MM_SchedulingDelegate::calculateGlobalMarkIncrementHeadroom(MM_EnvironmentVLHGC *env) const
{
	uintptr_t headroomIncrements = 0;

	if (_regionConsumptionRate > 0.0) {
		double headroomRegions = (double) _extensions->tarokKickoffHeadroomInBytes / _regionManager->getRegionSize();
		double headroomPartialGCs = headroomRegions / _regionConsumptionRate;
		double headroomGlobalMarkIncrements = headroomPartialGCs * (double)_extensions->tarokPGCtoGMPDenominator / (double)_extensions->tarokPGCtoGMPNumerator;
		headroomIncrements = (uintptr_t) ceil(headroomGlobalMarkIncrements);
	}
	return headroomIncrements;
}

uintptr_t
MM_SchedulingDelegate::estimateRemainingGlobalBytesToScan() const
{
	uintptr_t expectedGlobalBytesToScan = (uintptr_t) calculateEstimatedGlobalBytesToScan();
	uintptr_t globalBytesScanned = ((MM_IncrementalGenerationalGC *)_extensions->getGlobalCollector())->getBytesScannedInGlobalMarkPhase();
	uintptr_t remainingGlobalBytesToScan = MM_Math::saturatingSubtract(expectedGlobalBytesToScan, globalBytesScanned);

	return remainingGlobalBytesToScan;
}

double
MM_SchedulingDelegate::estimateRemainingTimeMillisToScan() const
{
	Assert_MM_true(0 != _extensions->gcThreadCount);

	double remainingBytesToScan = (double) estimateRemainingGlobalBytesToScan();
	double estimatedScanMillis = remainingBytesToScan * _scanRateStats.microSecondsPerByteScanned / ((double)_extensions->gcThreadCount) / 1000.0;

	return estimatedScanMillis;
}

void
MM_SchedulingDelegate::updateGMPStats(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	/* We should have just finished the last GMP increment, so persistentGlobalMarkPhaseState should contain
	 * information for the whole GMP cycle.
	 */

	MM_CycleStateVLHGC * persistentGMPState = ((MM_IncrementalGenerationalGC *)_extensions->getGlobalCollector())->getPersistentGlobalMarkPhaseState();
	Assert_MM_true(MM_CycleState::CT_GLOBAL_MARK_PHASE == persistentGMPState->_collectionType);
	Assert_MM_true(0 != _extensions->gcThreadCount);

	MM_MarkVLHGCStats * incrementalMarkStats = &(persistentGMPState->_vlhgcCycleStats._incrementalMarkStats);
	MM_MarkVLHGCStats * concurrentMarkStats = &(persistentGMPState->_vlhgcCycleStats._concurrentMarkStats);

	uint64_t incrementalScanTime = (uint64_t) (((double) j9time_hires_delta(0, incrementalMarkStats->getScanTime(), J9PORT_TIME_DELTA_IN_MICROSECONDS)) / ((double) _extensions->gcThreadCount));
	uintptr_t concurrentBytesScanned = concurrentMarkStats->_bytesScanned;

	_historicTotalIncrementalScanTimePerGMP = (uint64_t) ((_historicTotalIncrementalScanTimePerGMP * incrementalScanTimePerGMPHistoricWeight) + (incrementalScanTime * (1 - incrementalScanTimePerGMPHistoricWeight)));
	_historicBytesScannedConcurrentlyPerGMP = (uintptr_t) ((_historicBytesScannedConcurrentlyPerGMP * bytesScannedConcurrentlyPerGMPHistoricWeight) + (concurrentBytesScanned * (1 - bytesScannedConcurrentlyPerGMPHistoricWeight)));

	Trc_MM_SchedulingDelegate_updateGMPStats(env->getLanguageVMThread(), _historicTotalIncrementalScanTimePerGMP, incrementalScanTime, _historicBytesScannedConcurrentlyPerGMP, concurrentBytesScanned);
}

void 
MM_SchedulingDelegate::updatePgcTimePrediction(MM_EnvironmentVLHGC *env)
{
	/* 
	 * Create a model that passes through (minimumEdenRegions,minimumPgcTime) and (current eden size in regions, pgcTime) 
	 * By remembering historic values of _pgcTimeIncreasePerEdenRegionFactor, it is possible to reasonably accuratly predict how long PGC will take, if eden were to change size
	 */
	double x1 = (double)minimumEdenRegions;
	double y1 = (double)minimumPgcTime;

	double x2 = (double)getCurrentEdenSizeInRegions(env);
	double y2 = (double)_historicalPartialGCTime;

	/* 
	 * Calculate how closely related PGC is to eden time. The closer _pgcTimeIncreasePerEdenRegionFactor is to 1.0, the more directly changing eden size will impact pgc time.
	 * The higher _pgcTimeIncreasePerEdenRegionFactor is from 1, the less changing eden size will affect pgc time. 
	 * In certain edge cases where eden is very small (minimumEdenRegions in size), or pgc time is very small, skip this set of calculation, since the results will not be correct
	 */
	if ((x1 < x2) && (y1 < y2)) {
		double timeDiff = y1 - y2;
		double edenSizeRatio = ((x1 + 1.0) / (x2 + 1.0));
		_pgcTimeIncreasePerEdenRegionFactor = pow(edenSizeRatio, (1.0/timeDiff));
	} 
}


uint64_t
MM_SchedulingDelegate::getScanTimeCostPerGMP(MM_EnvironmentVLHGC *env)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(env);
	double incrementalCost = (double)_historicTotalIncrementalScanTimePerGMP;
	double concurrentCost = 0.0;
	double scanRate = _scanRateStats.microSecondsPerByteScanned / (double)extensions->gcThreadCount;
	
	if (scanRate > 0.0) {
		concurrentCost = extensions->tarokConcurrentMarkingCostWeight * ((double)_historicBytesScannedConcurrentlyPerGMP * scanRate );
	}

	return (uint64_t) (incrementalCost + concurrentCost);
}

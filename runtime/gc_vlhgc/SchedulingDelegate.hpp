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

#if !defined(THRESHOLDDELEGATE_HPP_)
#define THRESHOLDDELEGATE_HPP_

#include "j9.h"
#include "j9cfg.h"

#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"

class MM_HeapRegionDescriptorVLHGC;

class MM_SchedulingDelegate : public MM_BaseNonVirtual
{
	/* Data Members */
private:
	MM_GCExtensions *_extensions; /**< The GC extensions structure */
	MM_HeapRegionManager *_regionManager; /** A cached pointer to the region manager */
	uintptr_t _taxationIndex;  /**< Counter of the number of times that taxation has occurred. This is used to determine if the current taxation point should perform a GMP and/or PGC */
	uintptr_t _remainingGMPIntermissionIntervals; /**< Counter of the remaining intervals to skip before starting another GMP cycle */ 
	bool _nextIncrementWillDoPartialGarbageCollection; /**< Record whether a PGC is planned for the next taxation point */
	bool _nextIncrementWillDoGlobalMarkPhase; /**< Record whether a GMP increment is planned for the next taxation point */
	bool _nextPGCShouldCopyForward;	/**< True if the next PGC increment will be attempted using copy-forward (read and set when PGC starts).  False implies compaction will be used, instead */
	bool _currentlyPerformingGMP; /**< Set to true when a GMP cycle is active */
	bool _globalSweepRequired;	/**< Set when a GMP finishes so that the next PGC to run knows that it is responsible for the first global sweep on the new mark map */
	bool _disableCopyForwardDuringCurrentGlobalMarkPhase; /**< Set to true when a PGC Abort happens during GMP. Reset only when GMP is complete. */
	uintptr_t _idealEdenRegionCount;	/**< The ideal number of regions for eden space at the current heap size */
	uintptr_t _minimumEdenRegionCount;	/**< The minimum number of regions for eden space at the current heap size */
	uintptr_t _edenRegionCount; /**< The current size of Eden, in regions */
	double _edenSurvivalRateCopyForward;	/**< The running average ratio of the number of regions consumed to copy-forward Eden to the number of regions allocated as Eden */
	uintptr_t _nonEdenSurvivalCountCopyForward;	/**< The running average count of the number of non-Eden regions allocated by copy-forward for its survival set (Eden regions are excluded since they are tracked with _edenSurvivalRateCopyForward) */
	uintptr_t _numberOfHeapRegions; /**< The number of heap regions according to the region manager */

	uintptr_t _previousReclaimableRegions; /**< The number of reclaimable regions at the end of the last reclaim. Used to estimate consumption rate. */
	uintptr_t _previousDefragmentReclaimableRegions; /**< The number of reclaimable regions in the defragment set at the end of the last reclaim. Used to estimate GMP kickoff and compact rate. */
	double _regionConsumptionRate; /**< The average number of regions consumed per PGC */
	double _defragmentRegionConsumptionRate; /**< The average number of defragment regions consumed per PGC */
	double _bytesCompactedToFreeBytesRatio; /**< Ratio of bytes to be compacted / bytes to be freed, for regions created after GMP, and used to spread the work until next GMP */
	double _averageCopyForwardBytesCopied; /**< Weighted average of bytes copied by the copy-forward scheme */
	double _averageCopyForwardBytesDiscarded; /**< Weighted average of bytes discarded (lost) by the copy-forward scheme */
	double _averageSurvivorSetRegionCount; /**< Weighted average of survivor regions */
	double _averageCopyForwardRate; /**< Weighted average of (bytesCopied / timeSpentInCopyForward).  Disregards time spent related RSCL clearing. Measured in bytes/microseconds */
	double _averageMacroDefragmentationWork; /**< Average work to be done to mitigate influx of fragmented regions into the oldest age */
	uintptr_t _currentMacroDefragmentationWork;	 /**< As we age out regions and find macro defrag work, we sum it up */
	bool _didGMPCompleteSinceLastReclaim; /**< true if a GMP completed since the last reclaim cycle */
	uintptr_t _liveSetBytesAfterPartialCollect;		/**< Live set estimate for the current (at the end of) PGC */
	double _heapOccupancyTrend;			/**< Expected ratio of survival of newly created (since last GMP) live set */
	uintptr_t _liveSetBytesBeforeGlobalSweep;		/**< Live set estimate recorded value for PGC before last/current GMP sweep */
	uintptr_t _liveSetBytesAfterGlobalSweep;		/**< Live set estimate recorded value for PGC after last/current GMP sweep */
	uintptr_t _previousLiveSetBytesAfterGlobalSweep; /**< Live set estimate recorded value for PGC after previous GMP sweep */
	double _scannableBytesRatio;			/**< Ratio of scannable byte vs total bytes (scannable + non-scannable) as measured from past */
	uint64_t _historicTotalIncrementalScanTimePerGMP;  /**< Historic average of the total wall-clock time we spend scanning in all stop-the-world global mark increments over a GMP cycle in microseconds */
	uintptr_t _historicBytesScannedConcurrentlyPerGMP; /**< Historic average amount of bytes we scan concurrently per GMP cycle */
	uintptr_t _estimatedFreeTenure; /**< The amount of free memory we estimate in the total heap - includes some headroom for GMP kickoff */

	uintptr_t _maxEdenRegionCount; /**< The maximum size of eden, in regions */
	uintptr_t _minEdenRegionCount; /**< The minimum size of eden, in regions */

	uint64_t _partialGcStartTime;  /**< Start time of the in progress Partial GC in hi-resolution format (recorded to track total time spent in Partial GC) */
	double _partialGcOverhead; /**< Used to keep track of relative Partial GC overhead. Is calculated by dividing total time spent in a single PGC phase, by the time interval since the end of the previous PGC */
	uint64_t _historicalPartialGCTime;  /**< Weighted historical average of Partial GC times, measured in ms */
	uint64_t _recentPartialGCTime; /**< Most recent PGC time, measured in ms */

	uint64_t _globalMarkIncrementsTotalTime; /**< Agregate of time spent doing GMP increments for current phase */
	uint64_t _globalMarkIntervalStartTime; /**< Time interval between start of sucessive GMP increments */
	double _globalMarkOverhead; /**< Used to keep track of relative Global Mark overhead. Is calculated by dividing total time spent in Global Mark increments, Concurrent GMP work, and global sweep, by the time interval since the completion of the last global mark  */
	uint64_t _globalSweepTimeUs; /**< Used to keep track of time spent doing global sweep, corresponding to previous GMP cycle */
	uint64_t _concurrentMarkGCThreadsTotalWorkTime; /**< Sum of all time intervals (cpu) for GC threads doing concurrent work */

	uintptr_t _dynamicGlobalMarkIncrementTimeMillis;  /**< The dynamically calculated current time to be spent per GMP increment (subject to change over the course of the run) */

	double _pgcTimeIncreasePerEdenFactor; /**< Used to keep track of how much pgc time will increase as eden size increases */

	intptr_t _edenSizeFactor; /**< Used to indicate how far eden should increase or decrease from current value. Positive values indicate max eden should be larger, while - values indicate max eden should shrink. Value corresponds to number of regions */
	uintptr_t _pgcCountSinceGMPEnd; /**< Counts the number of PGC's from end of last GMP cycle, to the end of current(next) GMP cycle */
	uint64_t _averagePgcInterval; /**< The average interval time between start of 2 consecutive PGC's, measured in Us */

	uint64_t _totalGMPWorkTimeUs; /**< Represents the total time that the previous GMP cycle took. Includes concurrent work, increments of work, and global sweep time  */

	struct MM_SchedulingDelegate_ScanRateStats {
		uintptr_t historicalBytesScanned;		/**< Historical number of bytes scanned for mark operations */
		uint64_t historicalScanMicroseconds;	/**< Historical scan times for mark operations */
		double microSecondsPerByteScanned; /**< The average scan rate, measured in microseconds per byte */
		
		MM_SchedulingDelegate_ScanRateStats() :
			historicalBytesScanned(0),
			historicalScanMicroseconds(0),
			microSecondsPerByteScanned(0.0)
		{
		}
	} _scanRateStats;

	double _automaticDefragmentEmptinessThreshold; /**< Recommended automatic value for defragmentEmptinessThreshold*/

protected:
public:
	
	/* Member Functions */
	
	
	/* Add up macro defragmentation work for this region for total work accumulated during this PGC, as we find out
	 * that the region is retired and its RSCL is still valid.
	 * The work is the min of two: free spece (used as dest region) or occupied space (used as source).
	 * Eventually, estimateMacroDefragmentationWork is called to smooth out the current work with historical values
	 * @param region[in] region for which we are adding up defrag work
	 */
	void updateCurrentMacroDefragmentationWork(MM_EnvironmentVLHGC *env, MM_HeapRegionDescriptorVLHGC *region);

	/**
	 * Updates statistics required for total heap sizing
	 */ 
	void updateHeapSizingData(MM_EnvironmentVLHGC *env); 

	uintptr_t getPgcCountSinceGMPEnd(MM_EnvironmentVLHGC *env) { return _pgcCountSinceGMPEnd; }

private:
	/**
	 * Internal helper for determining the next taxation threshold. This does all
	 * of the work, except that it does not honour the GMP intermission. 
	 * getNextTaxationThreshold calls this in a loop until the intermission has 
	 * been consumed.
	 */
	uintptr_t getNextTaxationThresholdInternal(MM_EnvironmentVLHGC *env);

	/**
	 * Called to update the _liveSetBytesAfterPartialCollect instance variable based
	 * on the used memory found in the heap's memory pools.
	 */
	void updateLiveBytesAfterPartialCollect();

	/**
	 * @return The estimated number of bytes which a global collect would need to scan (based on live bytes, heap occupancy trends, and the ratio of scannable bytes)
	 */
	double calculateEstimatedGlobalBytesToScan() const;

	/**
	 * Aim to find the change in eden size with the best blend of gc overhead (% of time gc is active relative to mutators), and pgc average time
	 * @return The recommended change in eden size (in bytes) given the current heap sizing (eden, tenure, etc), and PGC and GMP data.
	 */ 
	intptr_t calculateRecommendedEdenChangeForExpandedHeap(MM_EnvironmentVLHGC *env);

	/**
	 * @return The predicted overhead (% of time gc is active relative to mutators) if eden changes by 'edenChange' bytes
	 * @param env[in] The main GC thread
	 * @param currentEdenSize The current size of eden, in bytes
	 * @param edenSizeChange How far away from currentEdenSize we want to observe overhead
	 * @param freeTenure The amount of free space in tenure, in bytes
	 * @param pgcAvgIntervalTime The observed avg time between PGC's (in us) since the last GMP ended
	 */ 
	double predictCpuOverheadForEdenSize(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uintptr_t freeTenure, uint64_t pgcAvgIntervalTime);

	/**
	 * @return The predicted interval between PGC collections. We expect the interval between collections to be linearly related to how much eden grows
	 * 	ie, if we double eden size, we expect the interval between collections to double as well
	 * @param env[in] The main GC thread
	 * @param currentEdenSize The current eden size, in bytes
	 * @param edenSizeChange How much we want to increase/decrease eden by, in bytes
	 * @param pgcAvgIntervalTime The observed pgc avg interval time
	 */ 
	double predictIntervalBetweenCollections(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uint64_t pgcAvgIntervalTime);

	/**
	 * @return The predicted number of PGC collections per GMP cycle. We expect the number of PGC collections to be proportional to free tenure.
	 * ie, if we have twice as much free tenure given the new eden size, we would expect twice as many PGC collections
	 * @param env[in] The main GC thread
	 * @param currentEdenSize The current eden size, in bytes
	 * @param edenSizeChange How much we want to increase/decrease eden by, in bytes
	 * @param freeTenure How much free tenure we have
	 */ 
	double predictNumberOfCollections(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange, uintptr_t freeTenure);

	/**
	 * @return The predicted avg time that PGC will take, if eden becomes currentEdenSize +/- edenSizeChange
	 * PGC time is somewhat dependent on eden size. Larger eden will typically take more time to collect than smaller eden.
	 * We can use this general observation to get a rough estimate of how long PGC will take
	 * @param env[in] The main GC thread
	 * @param currentEdenSize The current size of eden
	 * @param edenSizeChange How much we want to increase/decrease eden by, in bytes
	 */
	double predictPgcTime(MM_EnvironmentVLHGC *env, uintptr_t currentEdenSize, intptr_t edenSizeChange); 

	/**
	 * @return The estimated number of bytes which we have remaining to scan for the current GMP cycle.  
	 * If there is not currently a GMP running, returns calculateEstimatedGlobalBytesToScan()
	 */
	uintptr_t estimateRemainingGlobalBytesToScan() const;

	/**
	 * @return The estimated amount of time (wall-clock time in millis) we need to spend scanning to finish the current GMP.  
	 * If there is no GMP currently running, returns the estimated amount of time we'd need to scan
	 * to finish a GMP.
	 */
	double estimateRemainingTimeMillisToScan() const;

	/**
	 * Based on the estimated amount of live data, the historical scanning rate and
	 * the desired GMP increment time, estimate the number of GMP increments which
	 * would be required to complete a Global Mark Phase.
	 * @param env[in] the current thread
	 * @param liveSetAdjustedForScannableBytesRatio[in] The estimated number of scannable bytes on the heap
	 * @return the estimated number of increments required
	 */
	uintptr_t estimateGlobalMarkIncrements(MM_EnvironmentVLHGC *env, double liveSetAdjustedForScannableBytesRatio) const;
	
	/**
	 * Monitor influx of regions into the oldest age group,
	 * measure their occupancy and estimate how much extra compact work
	 * has to be done do defragment those regions.
 	 * @param env[in] the current thread
	 */
	void estimateMacroDefragmentationWork(MM_EnvironmentVLHGC *env);

	/**
	 * Using the region reclaimability and consumption data collected after the most recent PGC, 
	 * estimate the number of PGCs remaining until allocation failure will occur. 
	 * @param env[in] The main GC thread
	 * @return the estimated number of PGCs remaining before AF (anything from 0 to UDATA_MAX)
	 */
	uintptr_t estimatePartialGCsRemaining(MM_EnvironmentVLHGC *env) const;
	
	/**
	 * Called after a PGC has been completed in order to measure the region consumption and update
	 * the consumption rate.
	 * @param env[in] The main GC thread
	 * @param currentReclaimableRegions the estimated number of reclaimable regions
	 * @param defragmentReclaimableRegions he estimated number of reclaimable defragment regions
	 */
	void measureConsumptionForPartialGC(MM_EnvironmentVLHGC *env, uintptr_t currentReclaimableRegions, uintptr_t defragmentReclaimableRegions);
	
	/**
	 * Called after marking to update statistics related to the scan rate. This data is used
	 * to estimate how long a GMP will take.
	 * param historicWeight weight used for historic data when averaging
	 * @param env[in] the main GC thread
	 */
	void measureScanRate(MM_EnvironmentVLHGC *env, double historicWeight);
	
	/**
	 * Recalculate the intermission until kick-off based on current estimates, if automatic
	 * intermissions are enabled. Store the result in _remainingGMPIntermissionIntervals.
	 * @param env[in] the main GC thread
	 */
	void calculateAutomaticGMPIntermission(MM_EnvironmentVLHGC *env);
	
	/**
	 * Following a GC, recalculate the Eden size for the next PGC.
	 * This is typically the same as GCExtensions->tarokeEdenSize, but may be smaller if 
	 * insufficient memory is available
	 * @param env[in] the main GC thread
	 */
	void calculateEdenSize(MM_EnvironmentVLHGC *env);

	/**
	 * Compute what the ideal eden size should be, and return by how many regions eden should change
	 * @param env[in] the main GC thread
	 * @param edenChangeSpeed How quickly eden should jump from current eden size to the ideal eden size. edenChangeSpeed must be between 0 and 1. 
	 * @return by how many regions eden should change. Positive values mean eden should expand, while negative values means eden should shrink
	 * 0.1 indicates that current eden size should move a little bit towards ideal eden size
	 * 0.9 indicates that current eden size should move very aggresively towards ideal eden size
	 * 0.5 indicates that current eden size should move halfway towards ideal eden size
	 */
	intptr_t moveTowardRecommendedEdenForExpandedHeap(MM_EnvironmentVLHGC *env, double edenChangeSpeed);

	/**
	 * Calculate how much the heap has expanded, relative to -Xms and -Xmx/-Xsoftmx. 
	 * Ex: If -Xmx1G and -Xmx5G, and current heap is 3G, then we return 0.5
	 * @param env[in] the main GC thread
	 * @return Percent of heap expanded, as ratio between 0-1
	 */
	double calculatePercentOfHeapExpanded(MM_EnvironmentVLHGC *env);

	/**
	 * Following a PGC, check how PGC overhead and PGC times are behaving, and modyfing _edenSizeFactor to increase/decrease eden size if needed
	 * @param env[in] the main GC thread
	 * @param globalSweepHappened true if a global sweep was performed during the PGC that just completed
	 */
	void checkEdenSizeAfterPgc(MM_EnvironmentVLHGC *env, bool globalSweepHappened);

	/**
	 * Calculates how much eden size should change when heap is not fully expanded, based off of pgc time, and pgc cpu overhead
	 * @return by how many regions eden should change
	 */
	intptr_t calculateEdenChangeHeapNotFullyExpanded(MM_EnvironmentVLHGC *env);

	/**
	 * Maps a pgc time, to a corresponding GC overhead (as a % of time being active). This is used for calculating hybrid eden overhead.
	 * Depending on if the heap is fully expanded (ie, heap size >= Xsoftmx), the returned overhead will take a slightly different meaning
	 * @param env[in] the main GC thread
	 * @param pgcPauseTimeMs the pgc pause time in Ms that needs to be mapped to a cpu overhead
	 * @param heapIsFullyExpanded set to true will use the mapping for when heap is fully expanded, which differs from the false case.
	 * @return PGC overhead corresponding to pgc time, as a % between 0 and 100
	 */
	double mapPgcPauseOverheadToPgcCPUOverhead(MM_EnvironmentVLHGC *env, uintptr_t pgcPauseTimeMs, bool heapIsFullyExpanded);

	/**
	 * Blends a pgc average time, and a GC overhead, and returns the hybrid overhead of the 2 values.
	 * Note that if heap is fully expanded (current heap size > softmx/mx), the returned values mean slightly different things
	 * @param env[in] the main GC thread
	 * @param pgcPauseTimeMs the pgc time in Ms that needs to be mapped to a cpu overhead
	 * @param overhead The overhead we need to blend. Must be value between 0 and 1
	 * @param heapIsFullyExpanded set to true will use the pgc time mapping for when heap is fully expanded, which differs from the false case.
	 * @return Hybrid overhead for pgc avg time and pgc overhead, as a % between 0 and 1
	 */ 
	double calculateHybridEdenOverhead(MM_EnvironmentVLHGC *env, uintptr_t pgcPauseTimeMs, double overhead, bool heapIsFullyExpanded);

	/**
	 * Modify the _idealEdenRegionCount count based on _edenSizeFactor. If _edenSizeFactor is postive, we increase eden. 
	 * If _edenSizeFactor is negative, we shrink eden.  
	 * Eden will be bounded by _maxEdenPercent and _minEdenPercent, or by any -Xmn/s/x options if provided
	 * @param env[in] the main GC thread
	 */
	void adjustIdealEdenRegionCount(MM_EnvironmentVLHGC *env);

	/**
	 * Calculate the new Global Mark increment time given the most recent Partial GC time.
	 * Attempt to keep the GMP times in line with the times in PGC.  Keep track of a weighted
	 * historic average and set the new GMP increment time to be a result of the adjusted
	 * calculations.
	 * @param env[in] the main GC thread
	 * @param pgcTime[in] New PGC-time to update weighted average with
	 */
	void calculateGlobalMarkIncrementTimeMillis(MM_EnvironmentVLHGC *env, uint64_t pgcTime);

	/**
	 * Updates the _edenSurvivalRateCopyForward and _nonEdenSurvivalCountCopyForward running averages by including thisEdenSurvivalRate and
	 * thisNonEdenSurvivorCount, respectively, for a copy-forward which has just completed.
	 * @param thisEdenSurvivalRate[in] A value >= 0.0 (and typically <= 1.0) representing the number of regions out of those allocated for this Eden which survived
	 * @param thisNonEdenSurvivorCount[in] The number of non-Eden regions consumed by this a Copy-Forward
	 */
	void updateSurvivalRatesAfterCopyForward(double thisEdenSurvivalRate, uintptr_t thisNonEdenSurvivorCount);

	/**
	 * Get number of GMP increments we wish to have as headroom to ensure that the GMP cycle finishes before AF with the desired pause time.
	 * @param env[in] the main GC thread
	 */
	uintptr_t calculateGlobalMarkIncrementHeadroom(MM_EnvironmentVLHGC *env) const;

	/**
	 * Called after a GMP completes to update GMP-related statistics necessary for scheduling.  This is done by examining env->cycleState.
	 * @param env[in] the main GC thread
	 */
	void updateGMPStats(MM_EnvironmentVLHGC *env);

	/**
	 * Updates statistics used to predict PGC time, with the PGC historic time. 
	 * @param env[in] the main GC thread
	 */ 
	void updatePgcTimePrediction(MM_EnvironmentVLHGC *env);

	/**
	 * Called after a copy forward rate to update the averageCopyForwardRate
	 * @param env[in] the main GC thread
	 */
	double calculateAverageCopyForwardRate(MM_EnvironmentVLHGC *env);

	/**
	 * Estimate total free memory
	 * @param env[in] the main GC thread
	 * @oaram freeRegionMemory[in]
	 * @param defragmentedMemory[in]
	 * @oaram reservedFreeMemory[in]
	 * @return total free memory(bytes)
	 */
	uintptr_t estimateTotalFreeMemory(MM_EnvironmentVLHGC *env, uintptr_t freeRegionMemory, uintptr_t defragmentedMemory, uintptr_t reservedFreeMemory);

	/**
	 * Calculate GMP Kickoff Headroom In Bytes
	 * the
	 */
	uintptr_t calculateKickoffHeadroom(MM_EnvironmentVLHGC *env, uintptr_t totalFreeMemory);

protected:
	
public:
	/**
	 * Initialize the receiver.
	 * @param env[in] The thread initializing the collector
	 * @return Whether or not the initialization succeeded
	 */
	bool initialize(MM_EnvironmentVLHGC *env);

	uintptr_t initializeKickoffHeadroom(MM_EnvironmentVLHGC *env);

	/**
	 * Calculate the allocation threshold for the first taxation period. This should be called
	 * to initialize the threshold any time that the heap is reset (e.g. initialized or a global
	 * collect is performed).
	 */
	uintptr_t getInitialTaxationThreshold(MM_EnvironmentVLHGC *env);

	/**
	 * Calculate the allocation threshold for the next taxation.  Note that this must only
	 * be called once per increment, as it also advances the receiver to the next increment.
	 */
	uintptr_t getNextTaxationThreshold(MM_EnvironmentVLHGC *env);

	/**
	 * Determine what work should be performed during the current increment.
	 * 
	 * @param env[in] the main GC thread
	 * @param doPartialGarbageCollection[out] set to true if a PGC should be performed, false otherwise
	 * @param doGlobalMarkPhase[out] set to true if a GMP increment should be performed, false otherwise
	 */
	void getIncrementWork(MM_EnvironmentVLHGC *env, bool* doPartialGarbageCollection, bool* doGlobalMarkPhase);
	
	/**
	 * Return region consumption rate.
	 */
	double getRegionConsumptionRate() { return 	_regionConsumptionRate; }
	double getDefragmentRegionConsumptionRate() { return _defragmentRegionConsumptionRate; }
	double getTotalRegionConsumptionRate() { return _regionConsumptionRate + _defragmentRegionConsumptionRate; }

	/**
	 * @return the average survivor set in regions
	 */
	double getAverageSurvivorSetRegionCount() { return _averageSurvivorSetRegionCount; }

	/**
	 * Returns the average copy forward rate.  Disregards time spent related to RSCL clearing. Measured in bytes/microseconds
	 * 
	 * @return the average copy forward rate.
	 */
	double getAverageCopyForwardRate() { return _averageCopyForwardRate; }

	/*
	 * Returns the scan time cost (in microseconds) we attribute to performing a GMP.  Attempts to
	 * factor in stop-the-world global mark increment time as well as any concurrent global marking which
	 * slows down the mutator.
	 * 
	 * @return the scan time cost (in microseconds) we attribute to performing a GMP.  
	 * 
	 */
	uint64_t getScanTimeCostPerGMP(MM_EnvironmentVLHGC *env);

	/**
	 * Return measured average scan rate.
	 */
	double getMicrosecondsPerByteScanned() { return _scanRateStats.microSecondsPerByteScanned; }

	/**
	 * Return average loss of free memory due to Copy-Forward inefficiency
	 */
	double getAverageEmptinessOfCopyForwardedRegions();

	/**
	 * Return the minimum emptiness(free and dark matter bytes) a region must have in order to be considered for defragmentation
	 */
	double getDefragmentEmptinessThreshold(MM_EnvironmentVLHGC *env);

	/**
	 * Return the minimum emptiness(free and dark matter bytes) a region must have in order to be considered for defragmentation
	 */
	void setAutomaticDefragmentEmptinessThreshold(double defragmentEmptinessThreshold) { _automaticDefragmentEmptinessThreshold = defragmentEmptinessThreshold; }

	/**
	 * Calculate compact-bytes/free-bytes ratio after global sweep, to be used for determining amount of compact work on each PGC
	 * @param env[in] The main GC thread
	 * @param edenSizeInBytes[in] The size of the Eden space which preceded this PGC, in bytes
	 */
	void calculatePGCCompactionRate(MM_EnvironmentVLHGC *env, uintptr_t edenSizeInBytes);

	/**
	 * Calculate ratio of scannable bytes vs total live set (scannable + non-scannable)
	 * @param env[in] The main GC thread
	 */
	void calculateScannableBytesRatio(MM_EnvironmentVLHGC *env);


	/**
	 * Calculate how much of live-set-data-after-PGC increase is real and how much of it garbage
	 */
	void calculateHeapOccupancyTrend(MM_EnvironmentVLHGC *env);
	
	/**
	 * recalculate PGCCompactionRate, HeapOccupancyTrend, ScannableBytesRatio at the end of First PGC After GMP
	 * it should be called before estimating defragmentReclaimableRegions in order to calculate GMPIntermission more accurate.
	 * TODO: might need to recalculate desiredCompactWork for sliding Compact of PGC (MacroDefragment part, right now it is calculated at the end of TaxationEntryPoint,
	 * but we need to decide sliding compaction before Copyforward PGC).
	 */
	void recalculateRatesOnFirstPGCAfterGMP(MM_EnvironmentVLHGC *env);

	/**
	 * Calculate desired amount of work to be compacted this PGC cycle
	 * @param env[in] the main GC thread
	 * @return desired bytes to be compacted
	 */
	uintptr_t getDesiredCompactWork();

	/**
	 * @return true if it is first PGC after GMP completed (so we can calculate compact-bytes/free-bytes ratio, etc.)
	 */
	bool isFirstPGCAfterGMP();
	/**
	 * clear the flag that indicate this was the first PGC after GMP completed
	 */
	void firstPGCAfterGMPCompleted();

	/**
	 * return whether a PGC Abort happens during GMP
	 */
	bool isPGCAbortDuringGMP() {return _disableCopyForwardDuringCurrentGlobalMarkPhase;}

	/**
	 * return whether the following PGC is required to do global sweep (typically, first PGC after GMP completed)
	 */
	bool isGlobalSweepRequired() { return _globalSweepRequired; }

	/*
	 * Sets the time taken for global sweep in microseconds
 	 */
	void setGlobalSweepTime(uint64_t globalSweepTime) { _globalSweepTimeUs = globalSweepTime; }

	/*
	 * Adds the time taken for concurrent mark
	 */
	void setConcurrentGlobalMarkTime(uint64_t concurrentMarkTotalTime) { _concurrentMarkGCThreadsTotalWorkTime = concurrentMarkTotalTime; }

	/**
	 * Inform the receiver that a copy-forward cycle has completed
	 * @param env[in] the main GC thread
	 */
	void copyForwardCompleted(MM_EnvironmentVLHGC *env);

	/**
	 * Inform the receiver that a Global Mark phase is starting
	 * @param env[in] the main GC thread
	 */ 
	void globalMarkCycleStart(MM_EnvironmentVLHGC *env);

	/**
	 *  Calculate GMP overhead
	 *  @param env[in] the main GC thread
	 */
	void calculateGlobalMarkOverhead(MM_EnvironmentVLHGC *env);

	/**
	 * Inform the receiver that a Global Mark phase is ending
	 * @param env[in] the main GC thread
	 */ 
	void globalMarkCycleEnd(MM_EnvironmentVLHGC *env);

	/**
	 * Inform the receiver that a Global Mark Phase has completed and that a GMP intermission should begin
	 * @param env[in] the main GC thread
	 */
	void globalMarkPhaseCompleted(MM_EnvironmentVLHGC *env);
	
	/**
	 * Inform the receiver that an increment in the Global Mark Phase, or the mark portion of a global collect, has completed
	 * @param env[in] the main GC thread
	 */
	void globalMarkIncrementCompleted(MM_EnvironmentVLHGC *env);

	/**
	 * Inform the receiver that a Global GC has completed.
	 * @param env[in] the main GC thread
	 * @param reclaimableRegions[in] an estimate of the reclaimable memory
	 * @param defragmentReclaimableRegions[in] an estimate of the reclaimable defragment memory
	 */
	void globalGarbageCollectCompleted(MM_EnvironmentVLHGC *env, uintptr_t reclaimableRegions, uintptr_t defragmentReclaimableRegions);
	
	/**
	 * Inform the receiver that a Partial GC has started.
	 * @param env[in] the main GC thread
	 */
	void partialGarbageCollectStarted(MM_EnvironmentVLHGC *env);
	

	/**
	 *  Calculate interval between consecutive start PGC increment starts, and calculate PGC overhead
	 *  @param env[in] the main GC thread
	 */
	void calculatePartialGarbageCollectOverhead(MM_EnvironmentVLHGC *env);

	/**
	 * Resets statistics which keep track of PGC count
	 * @param env[in] the main GC thread 
	 */ 
	void resetPgcTimeStatistics(MM_EnvironmentVLHGC *env);

	/**
	 * Inform the receiver that a Partial GC has completed.
	 * @param env[in] the main GC thread
	 * @param reclaimableRegions[in] an estimate of the reclaimable memory
	 * @param defragmentReclaimableRegions[in] an estimate of the reclaimable defragment memory
	 */
	void partialGarbageCollectCompleted(MM_EnvironmentVLHGC *env, uintptr_t reclaimableRegions, uintptr_t defragmentReclaimableRegions);

	/**
	 * Determine what type of PGC should be run next PGC cycle (Copy-Forward, Mark-Sweep-Compact etc)
	 * The result is not explicitly returned, but implicitly through CycleState, class member flag etc.
	 * @param env[in] the main GC thread
	 */
	void determineNextPGCType(MM_EnvironmentVLHGC *env);

	/**
	 * Answer the current expected time to be spent in a Global Mark Phase (GMP) increment.
	 * @param env[in] the main GC thread
	 * @return Time in milliseconds for Global Mark increment
	 */
	uintptr_t currentGlobalMarkIncrementTimeMillis(MM_EnvironmentVLHGC *env) const;

	/**
	 * Used to request the size, in bytes, of the active calculated Eden space.
	 * @param env[in] The main GC thread
	 * @return The current size of Eden, in bytes
	 */
	uintptr_t getCurrentEdenSizeInBytes(MM_EnvironmentVLHGC *env);

	/**
	 * Used to request the size, in bytes, of the ideal calculated Eden space.
	 * @param env[in] The main GC thread
	 * @return The ideal size of Eden, in bytes
	 */
	uintptr_t getIdealEdenSizeInBytes(MM_EnvironmentVLHGC *env);

	/**
	 * Used to request the size, in regions, of the active calculated Eden space.
	 * @param env[in] The main GC thread
	 * @return The current size of Eden, in regions
	 */
	uintptr_t getCurrentEdenSizeInRegions(MM_EnvironmentVLHGC *env);

	/**
	 * @return The number of bytes which we expect to scan during the next GMP increment
	 */
	uintptr_t getBytesToScanInNextGMPIncrement(MM_EnvironmentVLHGC *env) const;

	/**
	 * Adjust internal structures to reflect the change in heap size.
	 */
	void heapReconfigured(MM_EnvironmentVLHGC *env);
	
	double getAvgEdenSurvivalRateCopyForward(MM_EnvironmentVLHGC *env) { return _edenSurvivalRateCopyForward; }

	MM_SchedulingDelegate(MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager);
};


#endif /* THRESHOLDDELEGATE_HPP_ */

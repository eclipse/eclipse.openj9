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
#include "gcutils.h"
#include "ModronAssertions.h"
#include <math.h>
#include <float.h>

#include "ReclaimDelegate.hpp"

#include "AllocationContextTarok.hpp"
#include "ClassLoaderManager.hpp"
#include "CollectionSetDelegate.hpp"
#include "CompactGroupManager.hpp"
#include "CompactGroupPersistentStats.hpp"
#include "CompactStats.hpp"
#include "EnvironmentVLHGC.hpp"
#include "GlobalAllocationManagerTarok.hpp"
#include "InterRegionRememberedSet.hpp"
#include "MemorySubSpace.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"
#include "HeapRegionIteratorVLHGC.hpp"
#include "HeapRegionManagerTarok.hpp"
#include "MemoryPool.hpp"
#include "ObjectAllocationInterface.hpp"
#include "ParallelDispatcher.hpp"
#include "ParallelSweepSchemeVLHGC.hpp"
#include "VMThreadListIterator.hpp"
#include "WriteOnceCompactor.hpp"


MM_ReclaimDelegate::MM_ReclaimDelegate(MM_EnvironmentBase *env, MM_HeapRegionManager *manager, MM_CollectionSetDelegate *collectionSetDelegate)
	: MM_BaseNonVirtual()
	, _regionManager(manager)
	, _sweepScheme(NULL)
#if defined(J9VM_GC_MODRON_COMPACTION)
	, _writeOnceCompactor(NULL)
#endif /* J9VM_GC_MODRON_COMPACTION */
	, _collectionSetDelegate(collectionSetDelegate)
	, _regionsSortedByEmptinessArray(NULL)
	, _regionsSortedByEmptinessArraySize(0)
	, _compactRateOfReturn(1.0)
	, _compactGroups(NULL)
	, _compactGroupMaxCount(MM_CompactGroupManager::getCompactGroupMaxCount(MM_EnvironmentVLHGC::getEnvironment(env)))
{
	_typeId = __FUNCTION__;
	memset(_regionSortedByCompactScore, 0x0, sizeof(_regionSortedByCompactScore));
}

bool
MM_ReclaimDelegate::initialize(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	_dispatcher = extensions->dispatcher;
	UDATA regionCount = extensions->getHeap()->getHeapRegionManager()->getTableRegionCount();

	if(NULL == (_sweepScheme = MM_ParallelSweepSchemeVLHGC::newInstance(env))) {
		goto error_no_memory;
	}
#if defined(J9VM_GC_MODRON_COMPACTION)
	if(NULL == (_writeOnceCompactor = MM_WriteOnceCompactor::newInstance(env))) {
		goto error_no_memory;
	}
#endif /* defined(J9VM_GC_MODRON_COMPACTION) */

	if(extensions->tarokEnableScoreBasedAtomicCompact) {
		_compactGroups = (MM_ReclaimDelegate_ScoreBaseCompactTable *)j9mem_allocate_memory(sizeof(MM_ReclaimDelegate_ScoreBaseCompactTable) * _compactGroupMaxCount, OMRMEM_CATEGORY_MM);
		if(NULL == _compactGroups) {
			goto error_no_memory;
		}
	}

	_regionsSortedByEmptinessArray = (MM_HeapRegionDescriptorVLHGC **)j9mem_allocate_memory(sizeof(MM_HeapRegionDescriptorVLHGC *) * regionCount, OMRMEM_CATEGORY_MM);
	if (NULL == _regionsSortedByEmptinessArray) {
		goto error_no_memory;
	}

	return true;

error_no_memory:
	return false;
}

void
MM_ReclaimDelegate::tearDown(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	_dispatcher = NULL;
	
	if (NULL != _sweepScheme) {
		_sweepScheme->kill(env);
		_sweepScheme = NULL;
	}
	if (NULL != _writeOnceCompactor) {
		_writeOnceCompactor->kill(env);
		_writeOnceCompactor = NULL;
	}
	if (NULL != _compactGroups) {
		j9mem_free_memory(_compactGroups);
		_compactGroups = NULL;
	}
	if (NULL != _regionsSortedByEmptinessArray) {
		j9mem_free_memory(_regionsSortedByEmptinessArray);
		_regionsSortedByEmptinessArray = NULL;
	}
}

void 
MM_ReclaimDelegate::tagRegionsBeforeSweep(MM_EnvironmentVLHGC *env)
{
	/* NOTE:  This method currently only provided to symmetry.
	 * 
	 * This method currently does nothing since sweep will sweep all regions which have not already been swept but have a valid mark map.
	 * In the future, this may be incrementalized:  see JAZZ 20233
	 */
}

UDATA
MM_ReclaimDelegate::tagRegionsBeforeCompact(MM_EnvironmentVLHGC *env, UDATA *regionRequiringSweep)
{
	Trc_MM_ReclaimDelegate_tagRegionsBeforeCompact_Entry(env->getLanguageVMThread());
	UDATA regionCount = 0;
	UDATA skippedRegionCount = 0;
	UDATA skippedRegionCountRequiringSweep = 0;
	bool isPartialCollect = (MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType);
	
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->hasValidMarkMap()) {
			if (region->_reclaimData._shouldReclaim) {
				/* do not compact overflowed regions and those that RSCL is being rebuilt (unless it's global collect) */
				bool hasCriticalRegions = (region->_criticalRegionsInUse > 0);
				if ((region->getRememberedSetCardList()->isAccurate() || !isPartialCollect) && !hasCriticalRegions) {
					region->_compactData._shouldCompact = true;
					region->_defragmentationTarget = false;
					regionCount += 1;
				} else {
					skippedRegionCount += 1;
					if (!region->_sweepData._alreadySwept) {
						skippedRegionCountRequiringSweep += 1;
					}
				}
			}
			Assert_MM_true(!region->_compactData._shouldFixup);
		}
	}

	Trc_MM_ReclaimDelegate_tagRegionsBeforeCompact_Exit(env->getLanguageVMThread(), regionCount, skippedRegionCount);
	*regionRequiringSweep = skippedRegionCountRequiringSweep;
	return regionCount;
}

UDATA
MM_ReclaimDelegate::tagRegionsBeforeCompactWithWorkGoal(MM_EnvironmentVLHGC *env, bool isCopyForward, UDATA desiredWorkToDo, UDATA *skippedRegionCountRequiringSweep)
{
	UDATA regionSize = _regionManager->getRegionSize();
	UDATA expectedBytesToReclaimThroughCompaction = 0;
	UDATA bytesToBeMoved = 0;

	Trc_MM_ReclaimDelegate_tagRegionsBeforeCompactWithWorkGoal_Entry(env->getLanguageVMThread(), desiredWorkToDo);
	Assert_MM_true(MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType);
	double lowestCompactScore = 100.0;
	UDATA regionCount = 0;

	deriveCompactScore(env);
	
	if (!isCopyForward) {
		/* Everything already Marked should be Compacted */
		regionCount += tagRegionsBeforeCompact(env, skippedRegionCountRequiringSweep);
	}

	Trc_MM_ReclaimDelegate_tagRegionsBeforeCompactWithWorkGoal_searching(env->getLanguageVMThread(), _currentSortedRegionCount);
	bool maxBytesToMoveReached = false;
	for (UDATA i = 0; (i < _currentSortedRegionCount) && !maxBytesToMoveReached; i++) {
		MM_HeapRegionDescriptorVLHGC *region = _regionSortedByCompactScore[i];
		Assert_MM_true(NULL != region);

		/* skip regions with score 0 (nulled as not being useful to recover a full region in its compact group) */
		if (0.0 < region->_compactData._compactScore) {
			Assert_MM_true(region->getRememberedSetCardList()->isAccurate());
			Assert_MM_true(region->hasValidMarkMap());
			Assert_MM_true(region->_sweepData._alreadySwept);
			if (isCopyForward) {
				Assert_MM_true(!region->_markData._shouldMark);
			} else {
				Assert_MM_true(!region->_compactData._shouldFixup);
				Assert_MM_true(!region->_compactData._shouldCompact);
			}

			MM_MemoryPool *memoryPool = region->getMemoryPool();
			Assert_MM_true(NULL != memoryPool);
			UDATA freeMemory = memoryPool->getFreeMemoryAndDarkMatterBytes();
			UDATA compactGroup = MM_CompactGroupManager::getCompactGroupNumber(env, region);
			Assert_MM_true(compactGroup < _compactGroupMaxCount);

			/* If we get more work then desired, we'll raise a flag, but still include that region */
			if (bytesToBeMoved + (regionSize - freeMemory) > desiredWorkToDo) {
				maxBytesToMoveReached = true;
			}

			expectedBytesToReclaimThroughCompaction += freeMemory;
			regionCount += 1;
			UDATA regionIndex = _regionManager->mapDescriptorToRegionTableIndex(region);
			Trc_MM_ReclaimDelegate_tagRegionsBeforeCompactWithWorkGoal_addingToCompactSet(env->getLanguageVMThread(), regionIndex, compactGroup, region->getLogicalAge(), region->_compactData._compactScore, (100 * freeMemory)/regionSize, MM_GCExtensions::getExtensions(env)->compactGroupPersistentStats[compactGroup]._weightedSurvivalRate);

			if (isCopyForward) {
				region->_markData._shouldMark = true;
				region->_reclaimData._shouldReclaim = true;
			} else {
				Assert_MM_true(0 == region->_criticalRegionsInUse);
				region->_compactData._shouldCompact = true;
			}
			Assert_MM_true(region->_defragmentationTarget);
			/* Collected regions are no longer targets for defragmentation until next GMP */
			region->_defragmentationTarget = false;

			bytesToBeMoved += (regionSize - freeMemory);
			Assert_MM_true(region->_compactData._compactScore <= lowestCompactScore);
			lowestCompactScore = region->_compactData._compactScore;
		}
	}

	Trc_MM_ReclaimDelegate_tagRegionsBeforeCompactWithWorkGoal_Exit(env->getLanguageVMThread(), bytesToBeMoved, expectedBytesToReclaimThroughCompaction / regionSize);
	return regionCount;
}

void
MM_ReclaimDelegate::estimateReclaimableRegions(MM_EnvironmentVLHGC *env, double averageEmptinessOfCopyForwardedRegions, UDATA *reclaimableRegions, UDATA *defragmentReclaimableRegions)
{
	Trc_MM_ReclaimDelegate_estimateReclaimableRegions_Entry(env->getLanguageVMThread());
	
	UDATA regionSize = _regionManager->getRegionSize();
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_GlobalAllocationManagerTarok *globalAllocationManager = (MM_GlobalAllocationManagerTarok *)extensions->globalAllocationManager;
	UDATA freeRegions = globalAllocationManager->getFreeRegionCount();
	
	*reclaimableRegions = freeRegions;
	*defragmentReclaimableRegions = freeRegions;
	
	if (extensions->tarokEnableScoreBasedAtomicCompact) {
		/* Track reclaimable memory in each generation, since we don't compact across generations. */
		memset(_compactGroups, 0, sizeof(MM_ReclaimDelegate_ScoreBaseCompactTable) * _compactGroupMaxCount);

		UDATA reclaimableCompactRegions = 0;
		UDATA defragmentReclaimableCompactRegions = 0;
		GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		while (NULL != (region = regionIterator.nextRegion())) {
			/* regions that are overflowed or RSCL is being rebuilt are not included into estimate */
			if (region->hasValidMarkMap() && region->getRememberedSetCardList()->isAccurate()) {
				MM_MemoryPool *memoryPool = region->getMemoryPool();
				Assert_MM_true(NULL != memoryPool);
				UDATA freeMemory = memoryPool->getFreeMemoryAndDarkMatterBytes();
			
				/* keep track of the recoverable memory in each compact group, since we don't compact between groups */
				UDATA compactGroup = MM_CompactGroupManager::getCompactGroupNumber(env, region);
				Assert_MM_true(compactGroup < _compactGroupMaxCount);
				_compactGroups[compactGroup].freeBytes += freeMemory;
				_compactGroups[compactGroup].regionCount += 1;

				if (env->_cycleState->_shouldRunCopyForward) {
					UDATA estimatedNonrecoverableMemory = (UDATA)((double)(regionSize - freeMemory) * averageEmptinessOfCopyForwardedRegions);
					UDATA recoverableBytes = OMR_MAX(0, ((IDATA)freeMemory - (IDATA)estimatedNonrecoverableMemory));
					_compactGroups[compactGroup].recoverableBytes += recoverableBytes;
					if (region->_defragmentationTarget) {
						_compactGroups[compactGroup].defragmentRecoverableBytes += recoverableBytes;
					}
				} else {
					UDATA recoverableBytes = (UDATA)(freeMemory * _compactRateOfReturn);
					_compactGroups[compactGroup].recoverableBytes += recoverableBytes;
					if (region->_defragmentationTarget) {
						_compactGroups[compactGroup].defragmentRecoverableBytes += recoverableBytes;
					}
				}

			}
		}

		for (UDATA compactGroup = 0; compactGroup < _compactGroupMaxCount; compactGroup++) {
			UDATA reclaimableRegionsInGeneration = _compactGroups[compactGroup].recoverableBytes / regionSize;
			UDATA defragmentReclaimableRegionsInGeneration = _compactGroups[compactGroup].defragmentRecoverableBytes / regionSize;
			if (reclaimableRegionsInGeneration > 0) {
				Trc_MM_ReclaimDelegate_estimateReclaimableRegions_generationSummary(env->getLanguageVMThread(), compactGroup, _compactGroups[compactGroup].regionCount, _compactGroups[compactGroup].freeBytes, _compactGroups[compactGroup].recoverableBytes, reclaimableRegionsInGeneration, defragmentReclaimableRegionsInGeneration);
				reclaimableCompactRegions += reclaimableRegionsInGeneration;
				defragmentReclaimableCompactRegions += defragmentReclaimableRegionsInGeneration;
			}
		}
		
		*reclaimableRegions += reclaimableCompactRegions;
		*defragmentReclaimableRegions += defragmentReclaimableCompactRegions;
	}
	
	Trc_MM_ReclaimDelegate_estimateReclaimableRegions_Exit(env->getLanguageVMThread(), freeRegions, *reclaimableRegions, *defragmentReclaimableRegions);
}


/*
 * Helper function used by J9_SORT to sort regions by emptiness in descending order.
 *
 * @param[in] element1 first element to compare
 * @param[in] element2 second element to compare 
 */
static int
compareEmptinessFunc(const void *element1, const void *element2)
{
	MM_HeapRegionDescriptorVLHGC *region1 = *(MM_HeapRegionDescriptorVLHGC **)element1;
	MM_HeapRegionDescriptorVLHGC *region2 = *(MM_HeapRegionDescriptorVLHGC **)element2;

	UDATA emptiness1 = region1->getMemoryPool()->getFreeMemoryAndDarkMatterBytes();
	UDATA emptiness2 = region2->getMemoryPool()->getFreeMemoryAndDarkMatterBytes();

	if (emptiness1 == emptiness2) {
		 return 0;
	 } else if (emptiness1 < emptiness2) {
		 return 1;
	 } else{
		 return -1;
	 }
}


void
MM_ReclaimDelegate::rebuildRegionsSortedByEmptinessArray(MM_EnvironmentVLHGC *env)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(env);
	MM_HeapRegionManager *regionManager = extensions->heapRegionManager;

	GC_HeapRegionIteratorVLHGC regionIterator(regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;

	_regionsSortedByEmptinessArraySize = 0;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->hasValidMarkMap()) {
			bool regionHasCriticalRegions = (0 != region->_criticalRegionsInUse);
			if (region->getRememberedSetCardList()->isAccurate() && (!regionHasCriticalRegions)) {
				/* Don't want to double-count regions that would be selected by other collection set algorithms 
				 * (i.e. nursery or DCSS) when calculating optimalEmptinessRegionThreshold.
				 * Going to only filter out nursery for now as with exponential aging, there may be alot of objects
				 * that might never reach maximum age.
				 */
				 if (!MM_CompactGroupManager::isRegionInNursery(env, region)) {
					_regionsSortedByEmptinessArray[_regionsSortedByEmptinessArraySize] = region;
					_regionsSortedByEmptinessArraySize += 1;
				}
			}
		}
	}

	J9_SORT(_regionsSortedByEmptinessArray, _regionsSortedByEmptinessArraySize, sizeof(MM_HeapRegionDescriptorVLHGC *), compareEmptinessFunc);
}


double
MM_ReclaimDelegate::calculateOptimalEmptinessRegionThreshold(MM_EnvironmentVLHGC *env, double regionConsumptionRate, double avgSurvivorRegions, double avgCopyForwardRate, U_64 scanTimeCostPerGMP)
{
	const MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(env);
	MM_GlobalAllocationManagerTarok * allocationmanager = (MM_GlobalAllocationManagerTarok *)extensions->globalAllocationManager;
	const UDATA regionSize = _regionManager->getRegionSize();
	const UDATA defragmentRecoveryTargetPerPGC = (UDATA) ceil(regionConsumptionRate * (double)regionSize);
	const UDATA freeRegions = allocationmanager->getFreeRegionCount();

	UDATA defragmentBytesCopyForwardedPerGMP = 0;
	double minimumTotalCostPerPGC = DBL_MAX; /* Measured in microseconds*/
	double optimalDefragmentEmptinessRegionThreshold = 0.0;
	UDATA gmpPeriod = 0; /* Measured in PGCs */
	bool outOfRegions = false;
	UDATA optimalGMPPeriod = 0;
	UDATA regionSortedByEmptinessIndex = 0;
	UDATA defragmentRecoveryTargetPerGMP = 0;
	/* Initially set bytesRecovered to current amount of non-required free regions.  PGC hasn't completed yet, so don't factor in Eden Regions */
	UDATA bytesRecovered = MM_Math::saturatingSubtract(freeRegions, (UDATA) ceil(avgSurvivorRegions)) * regionSize;
	/* Initially bytesRecovered includes free regions */
	UDATA freeAndDarkMatterBytes = regionSize;

	/* Sanity checking */
	if (regionConsumptionRate <= 0.0) {
		/* It does not look like we are fragmenting at all right now, just never defragment */
		optimalGMPPeriod = UDATA_MAX;
		optimalDefragmentEmptinessRegionThreshold = 1.0;
		goto done;
	}

	Assert_MM_true(defragmentRecoveryTargetPerPGC > 0);
	Assert_MM_true(avgCopyForwardRate > 0.0);

	/* TODO: lpnguyen reconcile this with dynamic eden resizing.  GMP scheduling will have to be modified for dynamic eden re-sizing as well 	
	 * 
	 * See Jazz 50737: Idea 1: Increase GMP rate for an explanation of this code
	 */
	while (regionSortedByEmptinessIndex < _regionsSortedByEmptinessArraySize) {
		gmpPeriod += 1;
		defragmentRecoveryTargetPerGMP += defragmentRecoveryTargetPerPGC;

		while (bytesRecovered < defragmentRecoveryTargetPerGMP) {
			if (regionSortedByEmptinessIndex >= _regionsSortedByEmptinessArraySize) {
				outOfRegions = true;
				break;
			}

			MM_HeapRegionDescriptorVLHGC * region = _regionsSortedByEmptinessArray[regionSortedByEmptinessIndex++];
			freeAndDarkMatterBytes = region->getMemoryPool()->getFreeMemoryAndDarkMatterBytes();
			bytesRecovered += freeAndDarkMatterBytes;
			defragmentBytesCopyForwardedPerGMP += (regionSize - freeAndDarkMatterBytes);
		}

		/* Out of regions to look at we can't correctly calculate totalCostRelatedToGMP for this gmpPeriod so bail out */
		if (outOfRegions) {
			break;
		}

		double avgGMPScanTimeCostPerPGC = (double) scanTimeCostPerGMP / (double) gmpPeriod;
		double avgDefragmentCopyForwardDurationPerPGC = ((double)defragmentBytesCopyForwardedPerGMP / avgCopyForwardRate) / (double) gmpPeriod;

		double totalCostPerPGC = avgGMPScanTimeCostPerPGC + avgDefragmentCopyForwardDurationPerPGC;
		Assert_MM_true(totalCostPerPGC >= 0.0);

		if (totalCostPerPGC < minimumTotalCostPerPGC) {
			minimumTotalCostPerPGC = totalCostPerPGC;
			optimalDefragmentEmptinessRegionThreshold = (double)freeAndDarkMatterBytes / (double)regionSize;
			optimalGMPPeriod = gmpPeriod;
		}
	}

done:

	Assert_MM_true((optimalDefragmentEmptinessRegionThreshold >= 0.0) && (optimalDefragmentEmptinessRegionThreshold <= 1.0));

	Trc_MM_ReclaimDelegate_calculateOptimalEmptinessRegionThreshold(
			env->getLanguageVMThread(),
			freeRegions,
			regionConsumptionRate,
			avgSurvivorRegions,
			avgCopyForwardRate,
			scanTimeCostPerGMP,
			minimumTotalCostPerPGC,
			optimalGMPPeriod,
			optimalDefragmentEmptinessRegionThreshold
	);

	return optimalDefragmentEmptinessRegionThreshold;

}

void
MM_ReclaimDelegate::runGlobalSweepBeforePGC(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode)
{
	env->_cycleState->_noCompactionAfterSweep = true;
	performAtomicSweep(env, allocDescription, activeSubSpace, gcCode);
	
	/* Now that dark matter and free bytes data have been updated in all memoryPools, we want to rebuild the _regionsSortedByEmptinessArray table */
	rebuildRegionsSortedByEmptinessArray(env);
}

void 
MM_ReclaimDelegate::untagRegionsAfterSweep()
{
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (!region->_sweepData._alreadySwept) {
			/* the region may still be in use or it may have been recycled during sweep */
			Assert_MM_true(region->hasValidMarkMap() || region->isFreeOrIdle());
			region->_sweepData._alreadySwept = true;
		}
	}
}

void 
MM_ReclaimDelegate::doSweep(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats._startTime = j9time_hires_clock();
	reportSweepStart(env);

	_sweepScheme->sweepForMinimumSize(env, env->_cycleState->_activeSubSpace,  allocDescription);
	_sweepScheme->completeSweep(env, COMPACTION_REQUIRED);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats._endTime = j9time_hires_clock();
	reportSweepEnd(env);
}

void
MM_ReclaimDelegate::performAtomicSweep(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_CompactGroupPersistentStats *persistentStats = extensions->compactGroupPersistentStats;

	tagRegionsBeforeSweep(env);

	MM_CompactGroupPersistentStats::updateStatsBeforeSweep(env, persistentStats);

	_collectionSetDelegate->rateOfReturnCalculationBeforeSweep(env);

	doSweep(env, allocDescription, activeSubSpace, gcCode);
	
	_collectionSetDelegate->rateOfReturnCalculationAfterSweep(env);

	MM_CompactGroupPersistentStats::updateStatsAfterSweep(env, persistentStats);

	untagRegionsAfterSweep();
}

void
MM_ReclaimDelegate::createRegionCollectionSetForPartialGC(MM_EnvironmentVLHGC *env, UDATA desiredWorkToDo)
{
	UDATA ignoreSkippedRegions = 0;
	tagRegionsBeforeCompactWithWorkGoal(env, true, desiredWorkToDo, &ignoreSkippedRegions);
}

void
MM_ReclaimDelegate::deriveCompactScore(MM_EnvironmentVLHGC *env)
{
	Trc_MM_ReclaimDelegate_deriveCompactScore_Entry(env->getLanguageVMThread());

	/* Set the size of the compact score sorted list to 0 */
	_currentSortedRegionCount = 0;
	
	/* Track reclaimable memory in each generation, since we don't compact across generations. */
	memset(_compactGroups, 0, sizeof(MM_ReclaimDelegate_ScoreBaseCompactTable) * _compactGroupMaxCount);

	/* count each type of region for the final tracepoint */
	UDATA overflowedRegionCount = 0;
	UDATA unsweptRegionCount = 0;
	UDATA alreadySelectedRegionCount = 0;
	UDATA fullRegionCount = 0;
	UDATA noncontributingRegionCount = 0;
	UDATA contributingRegionCount = 0;
	UDATA jniCriticalReservedRegions = 0;
	UDATA notDefragmentationTarget = 0;

	const UDATA regionSize = _regionManager->getRegionSize();
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->hasValidMarkMap()) {
			if (!region->_defragmentationTarget)	{
				/* Only select regions that are targeted for defrag (have been GMPed but need collected) */
				region->_compactData._compactScore = 0.0;
				notDefragmentationTarget += 1;
			} else if (!region->getRememberedSetCardList()->isAccurate()) {
				/* Overflowed regions or those that RSCL is being rebuilt will not be compacted (not even put into the sorted list) */
				region->_compactData._compactScore = 0.0;
				overflowedRegionCount += 1;
			} else if(!region->_sweepData._alreadySwept) {
				/* Skipping regions on first PGC after GMP that are not swept yet */
				region->_compactData._compactScore = 0.0;
				unsweptRegionCount += 1;
			} else if (region->_criticalRegionsInUse > 0) {
				/* make sure that we don't compact any regions which have critical sections in them */
				region->_compactData._compactScore = 0.0;
				jniCriticalReservedRegions += 1;
			} else {
				UDATA compactGroup = MM_CompactGroupManager::getCompactGroupNumber(env, region);
				MM_MemoryPool *memoryPool = region->getMemoryPool();
				UDATA freeMemory = memoryPool->getFreeMemoryAndDarkMatterBytes();

				if (region->_markData._shouldMark) {
					/* Skipping regions that are already selected by DCS */
					region->_compactData._compactScore = 0.0;
					/* Still, update recoverableBytes, since we compact this for sure */
					_compactGroups[compactGroup].recoverableBytes += freeMemory;
					alreadySelectedRegionCount += 1;
				} else {
					double emptiness = (double)freeMemory / (double)regionSize;
					double compactScore = 0.0;
					
					/* Coalescing free memory always gainful, but moving objects is wasteful if they're dead or dying, 
					 * especially since it will contribute to fragmentation in the resulting regions.
					 * potentialWastedWork = the fraction of the region which consists of objects likely to die soon.
					 * Regions which low potentialWastedWork are better candidates for compaction.
					 */
					double weightedSurvivalRate = MM_GCExtensions::getExtensions(env)->compactGroupPersistentStats[compactGroup]._weightedSurvivalRate;
					double potentialWastedWork = (1.0 - weightedSurvivalRate) * (1.0 - emptiness);
					
					if (env->_cycleState->_shouldRunCopyForward) {
						double sparsity = (double)memoryPool->getDarkMatterBytes() / (double)regionSize;
						compactScore = 100.0 * sparsity * (1.0 - potentialWastedWork);
					} else {
						compactScore = 100.0 * emptiness * (1.0 - potentialWastedWork);
					}

					Assert_MM_true( (compactScore >= 0.0) && (compactScore <= 100.0) );
					region->_compactData._compactScore = compactScore;

					/* Do not put full regions into the list */
					if (0.0 < region->_compactData._compactScore) {
						/* Update Top MAX_SORTED_REGIONS sorted list of regions... */
						MM_HeapRegionDescriptorVLHGC *regionToSwap = region;
						for (UDATA scanForRegion = 0 ; scanForRegion < _currentSortedRegionCount; scanForRegion++) {
							if (regionToSwap->_compactData._compactScore > _regionSortedByCompactScore[scanForRegion]->_compactData._compactScore) {
								/* insert here and swap for next iteration */
								MM_HeapRegionDescriptorVLHGC *temp = _regionSortedByCompactScore[scanForRegion];
								_regionSortedByCompactScore[scanForRegion] = regionToSwap;
								regionToSwap = temp;
							}
						}
						/* Keep track for only top MAX_SORTED_REGIONS */
						if (_currentSortedRegionCount < MAX_SORTED_REGIONS) {
							_regionSortedByCompactScore[_currentSortedRegionCount] = regionToSwap;
							_currentSortedRegionCount += 1;
							contributingRegionCount += 1;
						}
						
						_compactGroups[compactGroup].recoverableBytes += freeMemory;
					} else {
						fullRegionCount += 1;
					}
				}
			}
		} 

	}

	/* Walk the sorted list bottom up, and remove regions that do not contribute to recovering a full free region */
	for (IDATA i = _currentSortedRegionCount - 1; i >= 0; i--) {
		region = _regionSortedByCompactScore[i];
		UDATA freeMemory = region->getMemoryPool()->getFreeMemoryAndDarkMatterBytes();

		UDATA compactGroup = MM_CompactGroupManager::getCompactGroupNumber(env, region);
		Assert_MM_true(compactGroup < _compactGroupMaxCount);

		UDATA recoverableBytesBefore = _compactGroups[compactGroup].recoverableBytes;
		Assert_MM_true(recoverableBytesBefore >= freeMemory);
		UDATA recoverableBytesAfter = recoverableBytesBefore - freeMemory;

		/* if number of regions being recovered does not change, remove this region from the sorted list */
		if (MM_Math::roundToFloor(regionSize, recoverableBytesAfter) == MM_Math::roundToFloor(regionSize, recoverableBytesBefore)) {
			_compactGroups[compactGroup].recoverableBytes -= freeMemory;
			/* simply set the score to 0; later, selection logic will just skip over this region */
			region->_compactData._compactScore = 0.0;
			noncontributingRegionCount += 1;
			contributingRegionCount -= 1;
		}
	}
	
	Assert_MM_true((contributingRegionCount + noncontributingRegionCount) == _currentSortedRegionCount);
	Trc_MM_ReclaimDelegate_deriveCompactScore_Exit(env->getLanguageVMThread(), overflowedRegionCount, unsweptRegionCount, alreadySelectedRegionCount, fullRegionCount, jniCriticalReservedRegions, noncontributingRegionCount, contributingRegionCount, notDefragmentationTarget);
	
}

void
MM_ReclaimDelegate::postCompactCleanup(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode)
{
	/* Restart the allocation caches associated to all threads */
	mainThreadRestartAllocationCaches(env);

	/* ----- start of cleanupAfterCollect ------*/
	
	reportGlobalGCCollectComplete(env);

	/* in the present implementation, unset all the flags to catch invalid assumptions */
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *currentRegion = NULL;
	UDATA overFlowObjectsRemembered = 0;
	UDATA overflowRegions = 0;
	UDATA nonOverFlowObjectsRemembered = 0;
	UDATA nonOverflowRegions = 0;
	UDATA zeroRememberedRegions = 0;
	UDATA totalFixupRegions = 0;
	while(NULL != (currentRegion = regionIterator.nextRegion())) {
		if (currentRegion->_compactData._shouldFixup && !currentRegion->_compactData._shouldCompact) {
			totalFixupRegions += 1;
		}
		if (currentRegion->_compactData._shouldCompact) {
			currentRegion->_compactData._shouldCompact = false;
			/* compaction updates memory pool statistics, so set the _alreadySwept flag */
			currentRegion->_sweepData._alreadySwept = true;
		}
		currentRegion->_compactData._shouldFixup = false;
	}
	
	Trc_MM_ReclaimDelegate_rememberedObjectCount(env->getLanguageVMThread(), totalFixupRegions,
			nonOverflowRegions, (0 == nonOverflowRegions) ? 0 : nonOverFlowObjectsRemembered/nonOverflowRegions,
			overflowRegions, (0 == overflowRegions) ? 0 : overFlowObjectsRemembered/overflowRegions,
			zeroRememberedRegions); 

	/* ----- end of cleanupAfterCollect ------*/
}

void
MM_ReclaimDelegate::runReclaimCompleteSweep(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_GlobalAllocationManagerTarok *globalAllocationManager = (MM_GlobalAllocationManagerTarok *)extensions->globalAllocationManager;
	
	Assert_MM_false(env->_cycleState->_shouldRunCopyForward);
	
	UDATA freeBefore = globalAllocationManager->getFreeRegionCount();
	Trc_MM_ReclaimDelegate_runReclaimComplete_freeBeforeReclaim(env->getLanguageVMThread(), freeBefore);
	performAtomicSweep(env, allocDescription, activeSubSpace, gcCode);
	UDATA freeAfterSweep = globalAllocationManager->getFreeRegionCount();
	Trc_MM_ReclaimDelegate_runReclaimComplete_freeAfterSweep(env->getLanguageVMThread(), freeAfterSweep);
}

void
MM_ReclaimDelegate::runReclaimCompleteCompact(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode, MM_MarkMap *nextMarkMap, UDATA compactSelectionGoalInBytes)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_GlobalAllocationManagerTarok *globalAllocationManager = (MM_GlobalAllocationManagerTarok *)extensions->globalAllocationManager;
	
	Assert_MM_false(env->_cycleState->_shouldRunCopyForward);
	
	UDATA ignoreSkippedRegions = 0;
	runCompact(env, allocDescription, activeSubSpace, compactSelectionGoalInBytes, gcCode, nextMarkMap, &ignoreSkippedRegions);
	Trc_MM_ReclaimDelegate_runReclaimComplete_freeAfterCompact(env->getLanguageVMThread(), globalAllocationManager->getFreeRegionCount());
}

void
MM_ReclaimDelegate::runReclaimForAbortedCopyForward(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, MM_GCCode gcCode, MM_MarkMap *nextMarkMap, UDATA *skippedRegionCountRequiringSweep)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_CompactGroupPersistentStats *persistentStats = extensions->compactGroupPersistentStats;

	Trc_MM_ReclaimDelegate_runReclaimForAbortedCopyForward_Entry(env->getLanguageVMThread(), ((MM_GlobalAllocationManagerTarok *)MM_GCExtensions::getExtensions(env)->globalAllocationManager)->getFreeRegionCount());

	/* Perform Atomic Sweep on nonEvacuated regions before compacting in order to maintain accurate projectedLiveByte for the regions
	 * projectedLiveByte will be used in selecting RateOfReturnCollectionSet in future collection */
	performAtomicSweep(env, allocDescription, activeSubSpace, gcCode);

	UDATA regionsCompacted = tagRegionsBeforeCompact(env, skippedRegionCountRequiringSweep);
	MM_CompactGroupPersistentStats::updateStatsBeforeCompact(env, persistentStats);
	compactAndCorrectStats(env, allocDescription, nextMarkMap);
	MM_CompactGroupPersistentStats::updateStatsAfterCompact(env, persistentStats);
	// TODO: we should calculate _compactRateOfReturn here
	
	/* general cleanup */
	postCompactCleanup(env, allocDescription, activeSubSpace, gcCode);
	
	Trc_MM_ReclaimDelegate_runReclaimForAbortedCopyForward_Exit(env->getLanguageVMThread(), ((MM_GlobalAllocationManagerTarok *)MM_GCExtensions::getExtensions(env)->globalAllocationManager)->getFreeRegionCount(), regionsCompacted);
}

void 
MM_ReclaimDelegate::runCompact(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *activeSubSpace, UDATA compactSelectionGoalInBytes, MM_GCCode gcCode, MM_MarkMap *nextMarkMap, UDATA *skippedRegionCountRequiringSweep)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	MM_CompactGroupPersistentStats *persistentStats = extensions->compactGroupPersistentStats;

	Trc_MM_ReclaimDelegate_runCompact_Entry(env->getLanguageVMThread(), compactSelectionGoalInBytes);
	
	UDATA regionsCompacted = 0;

	if (extensions->tarokEnableScoreBasedAtomicCompact && (MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType) ) {
		regionsCompacted = tagRegionsBeforeCompactWithWorkGoal(env, false, compactSelectionGoalInBytes, skippedRegionCountRequiringSweep);
	} else {
		regionsCompacted = tagRegionsBeforeCompact(env, skippedRegionCountRequiringSweep);
	}
	MM_CompactGroupPersistentStats::updateStatsBeforeCompact(env, persistentStats);
	compactAndCorrectStats(env, allocDescription, nextMarkMap);
	MM_CompactGroupPersistentStats::updateStatsAfterCompact(env, persistentStats);
	// TODO: we should calculate _compactRateOfReturn here
	
	/* general cleanup */
	postCompactCleanup(env, allocDescription, activeSubSpace, gcCode);
	
	Trc_MM_ReclaimDelegate_runCompact_Exit(env->getLanguageVMThread(), regionsCompacted);
}

#if defined(J9VM_GC_MODRON_COMPACTION)
void 
MM_ReclaimDelegate::compactAndCorrectStats(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MarkMap *nextMarkMap)
{
	/* clear the stats so that we don't get stale data from the previous compaction - this is normally done at the beginning of a collect (as part of the
	 * clear fall for the stats structure) but we need to do it before each compact increment, as well
	 */
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats.clear();
	/* run the compactor and we will read the stats, afterward */
	mainThreadCompact(env, allocDescription, nextMarkMap);
}
#endif /* defined(J9VM_GC_MODRON_COMPACTION) */	

bool
MM_ReclaimDelegate::heapAddRange(MM_EnvironmentVLHGC *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress)
{
	return _sweepScheme->heapAddRange(env, subspace, size, lowAddress, highAddress);
}

bool
MM_ReclaimDelegate::heapRemoveRange(MM_EnvironmentVLHGC *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress, void *lowValidAddress, void *highValidAddress)
{
	return _sweepScheme->heapRemoveRange(env, subspace, size, lowAddress, highAddress, lowValidAddress, highValidAddress);
}

void
MM_ReclaimDelegate::heapReconfigured(MM_EnvironmentVLHGC *env)
{
	_sweepScheme->heapReconfigured(env);
}

void *
MM_ReclaimDelegate::createSweepPoolState(MM_EnvironmentBase *env, MM_MemoryPool *memoryPool)
{
	return _sweepScheme->createSweepPoolState(env, memoryPool);
}

void
MM_ReclaimDelegate::deleteSweepPoolState(MM_EnvironmentBase *env, void *sweepPoolState)
{
	_sweepScheme->deleteSweepPoolState(env, sweepPoolState);
}

#if defined(J9VM_GC_MODRON_COMPACTION)
void
MM_ReclaimDelegate::mainThreadCompact(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription, MM_MarkMap *nextMarkMap)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats._startTime = j9time_hires_clock();
	reportCompactStart(env);

	extensions->interRegionRememberedSet->setupForPartialCollect(env);

	MM_ParallelWriteOnceCompactTask compactTask(env, _dispatcher, _writeOnceCompactor, env->_cycleState, nextMarkMap);
	_dispatcher->run(env, &compactTask);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats._endTime = j9time_hires_clock();
	reportCompactEnd(env);
}
#endif /* J9VM_GC_MODRON_COMPACTION */

void
MM_ReclaimDelegate::mainThreadRestartAllocationCaches(MM_EnvironmentVLHGC *env)
{
	GC_VMThreadListIterator vmThreadListIterator((J9JavaVM *)env->getLanguageVM());
	J9VMThread *walkThread;
	while((walkThread = vmThreadListIterator.nextVMThread()) != NULL) {
		MM_EnvironmentBase *walkEnv = MM_EnvironmentBase::getEnvironment(walkThread->omrVMThread);
		/* CMVC 123281: setThreadScanned(false) is called here because the concurrent collector
		 * uses this as the reset point for all threads scanned state. This should really be moved
		 * into the STW thread scan phase.
		 */  
		walkEnv->setThreadScanned(false);

		walkEnv->_objectAllocationInterface->restartCache(env);
	}
}

void
MM_ReclaimDelegate::reportSweepStart(MM_EnvironmentBase *env)
{
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_SweepStart(vmThread);

	TRIGGER_J9HOOK_MM_PRIVATE_SWEEP_START(
		MM_GCExtensions::getExtensions(env)->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_SWEEP_START);

	TRIGGER_J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_START(MM_GCExtensions::getExtensions(env)->privateHookInterface, env->getOmrVMThread(), &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats);
}

void
MM_ReclaimDelegate::reportSweepEnd(MM_EnvironmentBase *env)
{
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_SweepVLHGCStats sweepStats = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats;

	Trc_MM_SweepEndBalancedGC(vmThread, j9time_hires_delta(sweepStats._startTime, sweepStats._endTime, J9PORT_TIME_DELTA_IN_MICROSECONDS));

	TRIGGER_J9HOOK_MM_PRIVATE_SWEEP_END(
		MM_GCExtensions::getExtensions(env)->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_SWEEP_END);

	TRIGGER_J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_END(MM_GCExtensions::getExtensions(env)->privateHookInterface, env->getOmrVMThread(), &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats);
}

void
MM_ReclaimDelegate::reportGlobalGCCollectComplete(MM_EnvironmentBase *env)
{
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_GlobalGCCollectComplete(vmThread);
	
	TRIGGER_J9HOOK_MM_PRIVATE_GLOBAL_GC_COLLECT_COMPLETE(
		MM_GCExtensions::getExtensions(env)->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_GLOBAL_GC_COLLECT_COMPLETE
	);
}

#if defined(J9VM_GC_MODRON_COMPACTION)
void
MM_ReclaimDelegate::reportCompactStart(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	CompactReason compactReason = (CompactReason)(static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats._compactReason);
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_CompactStart(vmThread, getCompactionReasonAsString(compactReason));

	TRIGGER_J9HOOK_MM_PRIVATE_COMPACT_START(
		extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_COMPACT_START,
		extensions->globalVLHGCStats.gcCount);

	TRIGGER_J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_START(extensions->privateHookInterface, env->getOmrVMThread(), &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats);
}

void
MM_ReclaimDelegate::reportCompactEnd(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CompactVLHGCStats *stats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._compactStats;

	Trc_MM_CompactEnd(vmThread, stats->_movedBytes);
	
	TRIGGER_J9HOOK_MM_OMR_COMPACT_END(
		extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_COMPACT_END);

	TRIGGER_J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_END(
		extensions->privateHookInterface,
		env->getOmrVMThread(),
		stats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._irrsStats);
}
#endif /* J9VM_GC_MODRON_COMPACTION */

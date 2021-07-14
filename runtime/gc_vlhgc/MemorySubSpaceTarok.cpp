
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

#include <math.h>

#include "j9.h"
#include "j9cfg.h"
#include "modronopt.h"

#include "MemorySubSpaceTarok.hpp"

#include "AllocationContextBalanced.hpp"
#include "AllocationContextTarok.hpp"
#include "AllocateDescription.hpp"
#include "CardTable.hpp"
#include "Collector.hpp"
#include "Debug.hpp"
#include "EnvironmentBase.hpp"
#include "GlobalAllocationManagerTarok.hpp"
#include "GlobalCollector.hpp"
#include "Heap.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"
#include "HeapRegionManager.hpp"
#include "HeapStats.hpp"
#include "IncrementalGenerationalGC.hpp"
#include "Math.hpp"
#include "MarkMap.hpp"
#include "MarkMapManager.hpp"
#include "MemoryPool.hpp"
#include "MemorySpace.hpp"
#include "MemorySubSpace.hpp"
#include "MemorySubSpaceRegionIterator.hpp"
#include "ModronTypes.hpp"
#include "ObjectAllocationInterface.hpp"
#include "PhysicalSubArena.hpp"

#define HEAP_FREE_RATIO_EXPAND_DIVISOR		100
#define HEAP_FREE_RATIO_EXPAND_MULTIPLIER	17
#define GMP_OVERHEAD_WEIGHT					0.4

/**
 * Return the memory pool associated to the receiver.
 * @return MM_MemoryPool
 */
MM_MemoryPool *
MM_MemorySubSpaceTarok::getMemoryPool()
{
	Assert_MM_unreachable();
	return NULL;
}

/**
 * Return the number of memory pools associated to the receiver.
 * @return count of number of memory pools 
 */
uintptr_t
MM_MemorySubSpaceTarok::getMemoryPoolCount()
{
	Assert_MM_unreachable();
	return UDATA_MAX;
}

/**
 * Return the number of active memory pools associated to the receiver.
 * @return count of number of memory pools 
 */
uintptr_t
MM_MemorySubSpaceTarok::getActiveMemoryPoolCount()
{
	Assert_MM_unreachable();
	return UDATA_MAX;
}

/**
 * Return the memory pool associated with a given storage location 
 * @param Address of storage location 
 * @return MM_MemoryPool
 */
MM_MemoryPool *
MM_MemorySubSpaceTarok::getMemoryPool(void * addr)
{
	MM_MemoryPool *pool = NULL;

	if (NULL != addr) {
		MM_HeapRegionDescriptorVLHGC *descriptor = (MM_HeapRegionDescriptorVLHGC *)_heapRegionManager->tableDescriptorForAddress(addr);
		if (descriptor->containsObjects()) {
			pool = descriptor->getMemoryPool();
		}
	}
	return pool;
}

/**
 * Return the memory pool associated with a given allocation size
 * @param Size of allocation request
 * @return MM_MemoryPool
 */
MM_MemoryPool *
MM_MemorySubSpaceTarok::getMemoryPool(uintptr_t size)
{
	/* this function is only used by ConcurrentSweepScheme, which is disabled in Tarok */
	Assert_MM_unreachable();
	return NULL;
}

/**
 * Return the memory pool associated with a specified range of storage locations. 
 * 
 * @param addrBase Low address in specified range  
 * @param addrTop High address in  specified range 
 * @param highAddr If range spans end of memory pool set to address of first byte
 * which does not belong in returned pool.
 * @return MM_MemoryPool for storage location addrBase
 */
MM_MemoryPool *
MM_MemorySubSpaceTarok::getMemoryPool(MM_EnvironmentBase *env, 
							void *addrBase, void *addrTop, 
							void * &highAddr)
{
	MM_MemoryPool *pool = NULL;

	if ((NULL != addrBase) && (NULL != addrTop)) {
		MM_HeapRegionDescriptorVLHGC *descriptor = (MM_HeapRegionDescriptorVLHGC *)_heapRegionManager->tableDescriptorForAddress(addrBase);
		MM_HeapRegionDescriptorVLHGC *highDescriptor = (MM_HeapRegionDescriptorVLHGC *)_heapRegionManager->tableDescriptorForAddress((void *)((uintptr_t)addrTop-1));
		/* we can only work on committed regions with in-use memory pools */
		if (descriptor->containsObjects()) {
			pool = descriptor->getMemoryPool();
			if (descriptor != highDescriptor) {
				/* they requested a spanning area so set the highAddr to indicate a split */
				highAddr = descriptor->getHighAddress();
			} else {
				/* they requested a single region so set the highAddr to NULL to communicate that it is fully contained in the region */
				highAddr = NULL;
			}
		}
	}
	return pool;
}		


/* ***************************************
 * Allocation
 * ***************************************
 */

/**
 * @copydoc MM_MemorySubSpace::getActualFreeMemorySize()
 */
uintptr_t
MM_MemorySubSpaceTarok::getActualFreeMemorySize()
{
	if (isActive()) {
		return _globalAllocationManagerTarok->getActualFreeMemorySize();
	} else {
		return 0;
	}
}

/**
 * @copydoc MM_MemorySubSpace::getApproximateFreeMemorySize()
 */
uintptr_t
MM_MemorySubSpaceTarok::getApproximateFreeMemorySize()
{
	if (isActive()) {
		return _globalAllocationManagerTarok->getApproximateFreeMemorySize();
	} else {
		return 0;
	}
}

uintptr_t
MM_MemorySubSpaceTarok::getActiveMemorySize()
{
	return getCurrentSize();
}

uintptr_t
MM_MemorySubSpaceTarok::getActiveMemorySize(uintptr_t includeMemoryType)
{
	if (getTypeFlags() & includeMemoryType) {
		return getCurrentSize();
	} else { 
		return 0;
	}	
}

uintptr_t
MM_MemorySubSpaceTarok::getActiveLOAMemorySize(uintptr_t includeMemoryType)
{
	/* LOA is not supported in Tarok */
	return 0;
}	

/**
 * @copydoc MM_MemorySubSpace::getActualActiveFreeMemorySize()
 */
uintptr_t
MM_MemorySubSpaceTarok::getActualActiveFreeMemorySize()
{
	return _globalAllocationManagerTarok->getActualFreeMemorySize();
}

/**
 * @copydoc MM_MemorySubSpace::getActualActiveFreeMemorySize(uintptr_t)
 */
uintptr_t
MM_MemorySubSpaceTarok::getActualActiveFreeMemorySize(uintptr_t includeMemoryType)
{
	if (getTypeFlags() & includeMemoryType ) {
		return _globalAllocationManagerTarok->getActualFreeMemorySize();
	} else {
		return 0;
	}		 	
}

/**
 * @copydoc MM_MemorySubSpace::getApproximateActiveFreeMemorySize()
 */
uintptr_t
MM_MemorySubSpaceTarok::getApproximateActiveFreeMemorySize()
{
	return _globalAllocationManagerTarok->getApproximateFreeMemorySize();
}

/**
 * @copydoc MM_MemorySubSpace::getApproximateActiveFreeMemorySize(uintptr_t)
 */
uintptr_t
MM_MemorySubSpaceTarok::getApproximateActiveFreeMemorySize(uintptr_t includeMemoryType)
{
	if (getTypeFlags() & includeMemoryType ) {
		return _globalAllocationManagerTarok->getApproximateFreeMemorySize();
	} else {
		return 0;
	}		 	
}


/**
 * @copydoc MM_MemorySubSpace::getApproximateActiveFreeLOAMemorySize()
 */
uintptr_t
MM_MemorySubSpaceTarok::getApproximateActiveFreeLOAMemorySize()
{
	/* LOA is not supported in Tarok */
	return 0;
}

/**
 * @copydoc MM_MemorySubSpace::getApproximateActiveFreeLOAMemorySize(uintptr_t)
 */
uintptr_t
MM_MemorySubSpaceTarok::getApproximateActiveFreeLOAMemorySize(uintptr_t includeMemoryType)
{
	/* LOA is not supported in Tarok */
	return 0;
}		 	

void
MM_MemorySubSpaceTarok::mergeHeapStats(MM_HeapStats *heapStats)
{
	Assert_MM_unreachable();
}

void
MM_MemorySubSpaceTarok::mergeHeapStats(MM_HeapStats *heapStats, uintptr_t includeMemoryType)
{
	_globalAllocationManagerTarok->mergeHeapStats(heapStats, includeMemoryType);
}

void
MM_MemorySubSpaceTarok::resetHeapStatistics(bool globalCollect)
{
	_globalAllocationManagerTarok->resetHeapStatistics(globalCollect);
}

/**
 * Return the allocation failure stats for this subSpace.
 */
MM_AllocationFailureStats *
MM_MemorySubSpaceTarok::getAllocationFailureStats()
{
	/* this subspace doesn't have a parent so it must have a collector. */
	Assert_MM_true(NULL != _collector);
	return MM_MemorySubSpace::getAllocationFailureStats();
}

/****************************************
 * Allocation
 ****************************************
 */

void *
MM_MemorySubSpaceTarok::allocateObject(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure)
{
	Assert_MM_unreachable();
	return NULL;
}

/**
 * Allocate the arraylet spine in immortal or scoped memory.
 */
void *
MM_MemorySubSpaceTarok::allocateArrayletLeaf(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure)
{
	Assert_MM_unreachable();
	return NULL;
}

void *
MM_MemorySubSpaceTarok::allocationRequestFailed(MM_EnvironmentBase *env, MM_AllocateDescription *allocateDescription, AllocationType allocationType, MM_ObjectAllocationInterface *objectAllocationInterface, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace)
{
	Assert_MM_unreachable();
	return NULL;
}

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)
void *
MM_MemorySubSpaceTarok::allocateTLH(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_ObjectAllocationInterface *objectAllocationInterface, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure)
{
	Assert_MM_unreachable();
	return NULL;
}
#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

/****************************************
 * Internal Allocation
 ****************************************
 */
void *
MM_MemorySubSpaceTarok::collectorAllocate(MM_EnvironmentBase *env, MM_Collector *requestCollector,  MM_AllocateDescription *allocDescription)
{
	Assert_MM_unreachable();
	return NULL;
}

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)

void *
MM_MemorySubSpaceTarok::collectorAllocateTLH(MM_EnvironmentBase *env, MM_Collector *requestCollector, MM_AllocateDescription *allocDescription, 
												uintptr_t maximumBytesRequired, void * &addrBase, void * &addrTop)
{
	Assert_MM_unreachable();
	return NULL;
}

#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

void
MM_MemorySubSpaceTarok::abandonHeapChunk(void *addrBase, void *addrTop)
{
	if (addrBase != addrTop) {
		MM_HeapRegionDescriptorVLHGC *base = (MM_HeapRegionDescriptorVLHGC *)_heapRegionManager->tableDescriptorForAddress(addrBase);
		MM_HeapRegionDescriptorVLHGC *verify = (MM_HeapRegionDescriptorVLHGC *)_heapRegionManager->tableDescriptorForAddress((void *)((uintptr_t)addrTop - 1));
		Assert_MM_true(base == verify);
		/* we can only work on committed regions with in-use memory pools */
		Assert_MM_true(base->containsObjects());
		base->getMemoryPool()->abandonHeapChunk(addrBase, addrTop);
	}
}

/****************************************
 *	Sub Space Categorization
 ****************************************
 */
MM_MemorySubSpace *
MM_MemorySubSpaceTarok::getDefaultMemorySubSpace()
{
	return this;
}

MM_MemorySubSpace *
MM_MemorySubSpaceTarok::getTenureMemorySubSpace()
{
	return this;
}

bool
MM_MemorySubSpaceTarok::isActive()
{
	Assert_MM_true(NULL == _parent);
	return true;
}

/**
 * Ask memory pools if a complete rebuild of freelist is required
 */
bool
MM_MemorySubSpaceTarok::completeFreelistRebuildRequired(MM_EnvironmentBase *env)
{
	/* 
	 * this function is only used by MemoryPoolLargeObjects. Since we don't 
	 * support LOA in Tarok we can just return false. 
	 */
	return false;
}	

/****************************************
 * Free list building
 ****************************************
 */

void
MM_MemorySubSpaceTarok::reset()
{
	/* unused in Tarok collectors */
	Assert_MM_unreachable();
}

/**
 * As opposed to reset, which will empty out, this will fill out as if everything is free
 */
void
MM_MemorySubSpaceTarok::rebuildFreeList(MM_EnvironmentBase *env)
{
	Assert_MM_unreachable();
}

void
MM_MemorySubSpaceTarok::resetLargestFreeEntry()
{
	_globalAllocationManagerTarok->resetLargestFreeEntry();
	Assert_MM_true(NULL == getChildren());
}

void
MM_MemorySubSpaceTarok::recycleRegion(MM_EnvironmentBase *env, MM_HeapRegionDescriptor *region)
{
	MM_EnvironmentVLHGC *envVLHGC = (MM_EnvironmentVLHGC *)env;
	MM_HeapRegionDescriptorVLHGC *regionStandard = (MM_HeapRegionDescriptorVLHGC *)region;
	/* first try to recycle into the original owning context, if there is one */
	MM_AllocationContextTarok *context = regionStandard->_allocateData._originalOwningContext;

	//TODO: move selection logic into ACT to minimize #ifdefs

	if (NULL == context) {
		context = regionStandard->_allocateData._owningContext;
	}

	switch (region->getRegionType()) {
		case MM_HeapRegionDescriptor::ADDRESS_ORDERED:
		case MM_HeapRegionDescriptor::ADDRESS_ORDERED_MARKED:
			/* declare previous mark map cleared, except if the region is arraylet leaf.
			 * leaving _nextMarkMapCleared unchanged
			 */
			regionStandard->_previousMarkMapCleared = true;
		case MM_HeapRegionDescriptor::ARRAYLET_LEAF:
			context->recycleRegion(envVLHGC, regionStandard);
			break;
		default:
			Assert_MM_unreachable();
	}
}

uintptr_t
MM_MemorySubSpaceTarok::findLargestFreeEntry(MM_EnvironmentBase *env, MM_AllocateDescription *allocateDescription)
{
	return _globalAllocationManagerTarok->getLargestFreeEntry();
}

/**
 * Initialization
 */
MM_MemorySubSpaceTarok *
MM_MemorySubSpaceTarok::newInstance(MM_EnvironmentBase *env, MM_PhysicalSubArena *physicalSubArena, MM_GlobalAllocationManagerTarok *gamt, bool usesGlobalCollector, uintptr_t minimumSize, uintptr_t initialSize, uintptr_t maximumSize, uintptr_t memoryType, U_32 objectFlags)
{
	MM_MemorySubSpaceTarok *memorySubSpace;
	
	memorySubSpace = (MM_MemorySubSpaceTarok *)env->getForge()->allocate(sizeof(MM_MemorySubSpaceTarok), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (NULL != memorySubSpace) {
		MM_HeapRegionManager *heapRegionManager = MM_GCExtensions::getExtensions(env)->heapRegionManager;
		new(memorySubSpace) MM_MemorySubSpaceTarok(env, physicalSubArena, gamt, heapRegionManager, usesGlobalCollector, minimumSize, initialSize, maximumSize, memoryType, objectFlags);
		if (!memorySubSpace->initialize(env)) {
			memorySubSpace->kill(env);
			memorySubSpace = NULL;
		}
	}
	return memorySubSpace;
}

bool
MM_MemorySubSpaceTarok::initialize(MM_EnvironmentBase *env)
{
	if(!MM_MemorySubSpace::initialize(env)) {
		return false;
	}
	
	if (!_expandLock.initialize(env, &MM_GCExtensions::getExtensions(env)->lnrlOptions, "MM_MemorySubSpaceTarok:_expandLock")) {
		return false;
	}

	return true;
}

void
MM_MemorySubSpaceTarok::tearDown(MM_EnvironmentBase *env)
{
	/* shutdown all regions with pools */
	GC_MemorySubSpaceRegionIterator regionIterator(this);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = (MM_HeapRegionDescriptorVLHGC*)regionIterator.nextRegion())) {
		/* first try to teardown with the original owning context, if there is one */
		MM_AllocationContextTarok *context = region->_allocateData._originalOwningContext;
		if (NULL == context) {
			context = region->_allocateData._owningContext;
		}
		if (NULL != context) {
			context->tearDownRegion(env, region);
		}
	}

	_expandLock.tearDown();

	MM_MemorySubSpace::tearDown(env);
}

/**
 * Memory described by the range has added to the heap and been made available to the subspace as free memory.
 */
bool
MM_MemorySubSpaceTarok::expanded(
	MM_EnvironmentBase *env,
	MM_PhysicalSubArena *subArena,
	MM_HeapRegionDescriptor *region,
	bool canCoalesce)
{
	void* regionLowAddress = region->getLowAddress();
	void* regionHighAddress = region->getHighAddress();

	/* Inform the sub space hierarchy of the size change */
	bool result = heapAddRange(env, this, region->getSize(), regionLowAddress, regionHighAddress);

	if (result) {
		/* Expand the valid range for arraylets. */
		MM_GCExtensions::getExtensions(_extensions)->indexableObjectModel.expandArrayletSubSpaceRange(this, regionLowAddress, regionHighAddress, largestDesirableArraySpine());

		/* this region should be reserved when we first expand into it */
		Assert_MM_true(MM_HeapRegionDescriptor::RESERVED == region->getRegionType());

		/* Region about to be set reserved better not to be marked overflowed in GMP or PGC */
		Assert_MM_true(0 == ((MM_HeapRegionDescriptorVLHGC *)region)->_markData._overflowFlags);

		/* now, mark the region as free and pass it to the region pool for management in its free list */
		region->setRegionType(MM_HeapRegionDescriptor::FREE);
		((MM_HeapRegionDescriptorVLHGC *)region)->_previousMarkMapCleared = false;
		((MM_HeapRegionDescriptorVLHGC *)region)->_nextMarkMapCleared = false;

		if (_extensions->tarokEnableExpensiveAssertions) {
			/* dirty the mark map (not assert code per se, but helps enhance other asserts when checkBitsForRegion is called) */
			MM_MarkMapManager *markMapManager = ((MM_IncrementalGenerationalGC *)_extensions->getGlobalCollector())->getMarkMapManager();
			markMapManager->getPartialGCMap()->setBitsForRegion(env, region, false);
			markMapManager->getGlobalMarkPhaseMap()->setBitsForRegion(env, region, false);
		}
	
		result = _extensions->cardTable->commitCardsForRegion(env, region);

		if (result) {
			_extensions->cardTable->clearCardsInRange(env, region->getLowAddress(), region->getHighAddress());
			_globalAllocationManagerTarok->expand(env, (MM_HeapRegionDescriptorVLHGC *)region);
		} else {
			heapRemoveRange(env, this, region->getSize(), regionLowAddress, regionHighAddress, NULL, NULL);
		}
	}
	return result;
}

/**
 * Memory described by the range which was already part of the heap has been made available to the subspace
 * as free memory.
 * @note Size information (current) is not updated.
 * @warn This routine is fairly hacky - is there a better way?
 */
void
MM_MemorySubSpaceTarok::addExistingMemory(
	MM_EnvironmentBase *env,
	MM_PhysicalSubArena *subArena,
	uintptr_t size,
	void *lowAddress,
	void *highAddress,
	bool canCoalesce)
{
	Assert_MM_unreachable();
}

/**
 * Memory described by the range which was already part of the heap is being removed from the current memory spaces
 * ownership.  Adjust accordingly.
 * @note Size information (current) is not updated.
 */
void *
MM_MemorySubSpaceTarok::removeExistingMemory(
	MM_EnvironmentBase *env,
	MM_PhysicalSubArena *subArena,
	uintptr_t contractSize,
	void *lowAddress,
	void *highAddress)
{
	/* Routine is normally used to contract within a memory pool (removing free memory) or adjusting barrier ranges, neither of which in this
	 * configuration are handled here (a contract is a full pool / region removal, and there is no barrier range "change" as a result of removing
	 * regions).
	 */

	return lowAddress;
}

MM_HeapRegionDescriptor * 
MM_MemorySubSpaceTarok::selectRegionForContraction(MM_EnvironmentBase *env, uintptr_t numaNode)
{
	MM_AllocationContextTarok * allocationContext = _globalAllocationManagerTarok->getAllocationContextForNumaNode(numaNode);
	Assert_MM_true(NULL != allocationContext);
	Assert_MM_true(allocationContext->getNumaNode() == numaNode);
	
	MM_HeapRegionDescriptorVLHGC * region = allocationContext->selectRegionForContraction(env);

	return region;
}

void *
MM_MemorySubSpaceTarok::lockedReplenishAndAllocate(MM_EnvironmentBase *env, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType)
{
	Trc_MM_MemorySubSpaceTarok_lockedReplenishAndAllocate_Entry(env->getLanguageVMThread());
	
	/* cast the context type since we know that we are operating on the Tarok context and we are its friend */
	MM_AllocationContextTarok *tarokContext = (MM_AllocationContextTarok *) context;
	
	void *result = tarokContext->lockedReplenishAndAllocate(env, objectAllocationInterface, allocateDescription, allocationType);
	if (NULL == result) {
		Trc_MM_MemorySubSpaceTarok_lockedReplenishAndAllocate_Failure(env->getLanguageVMThread(), _bytesRemainingBeforeTaxation);
	} else {
		Trc_MM_MemorySubSpaceTarok_lockedReplenishAndAllocate_Success(env->getLanguageVMThread(), result, _bytesRemainingBeforeTaxation);
	}
	
	return result;
	
}

void
MM_MemorySubSpaceTarok::setBytesRemainingBeforeTaxation(uintptr_t remaining)
{
	Trc_MM_setBytesRemainingBeforeTaxation(remaining);
	_bytesRemainingBeforeTaxation = remaining;
}

bool 
MM_MemorySubSpaceTarok::consumeFromTaxationThreshold(MM_EnvironmentBase *env, uintptr_t bytesToConsume)
{
	bool success = false;
	
	/* loop until we either subtract bytesToConsume from _bytesRemainingBeforeTaxation, 
	 * or set _bytesRemainingBeforeTaxation to zero 
	 */
	bool thresholdUpdated = false;
	do {
		uintptr_t oldBytesRemaining = _bytesRemainingBeforeTaxation;
		if (oldBytesRemaining < bytesToConsume) {
			_bytesRemainingBeforeTaxation = 0;
			success = false;
			thresholdUpdated = true;
		} else {
			uintptr_t newBytesRemaining = oldBytesRemaining - bytesToConsume;
			success = true;
			thresholdUpdated = (MM_AtomicOperations::lockCompareExchange(&_bytesRemainingBeforeTaxation, oldBytesRemaining, newBytesRemaining) == oldBytesRemaining);
		}
	} while (!thresholdUpdated);

	return success;
}

void *
MM_MemorySubSpaceTarok::replenishAllocationContextFailed(MM_EnvironmentBase *env, MM_MemorySubSpace *replenishingSpace, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType)
{
	void *result = NULL;
	Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_Entry(env->getLanguageVMThread(), context, (uintptr_t)allocationType, allocateDescription->getContiguousBytes());
	Assert_MM_true(this == replenishingSpace);

	/* we currently have no design for handling AC replenishment in a multi-subspace world, so reach for the collector */
	MM_IncrementalGenerationalGC *collector = (MM_IncrementalGenerationalGC*)MM_GCExtensions::getExtensions(env)->getGlobalCollector();
	Assert_MM_true(NULL != collector);

	allocateDescription->saveObjects(env);
	if(!env->acquireExclusiveVMAccessForGC(collector, true, true)) {
		allocateDescription->restoreObjects(env);
		/* don't have exclusive access - another thread beat us to the GC.  retry the allocate using the standard locking path */
		result = context->allocate(env, objectAllocationInterface, allocateDescription, allocationType);

		if(NULL == result) {
			allocateDescription->saveObjects(env);
			/* still can't satisfy - grab exclusive at all costs and retry the allocate */
			if(!env->acquireExclusiveVMAccessForGC(collector)) {
				allocateDescription->restoreObjects(env);
				/* now that we have exclusive, see if there is memory to satisfy the allocate (since we might not be the first to get in here) */
				result = lockedAllocate(env, context, objectAllocationInterface, allocateDescription, allocationType);

				if(NULL != result) {
					/* Satisfied the allocate after having grabbed exclusive access to perform a GC (without actually performing the GC).  Raise
					 * an event for tracing / verbose to report the occurrence.
					 */
					reportAcquiredExclusiveToSatisfyAllocate(env, allocateDescription);
				}
			} else {
				allocateDescription->restoreObjects(env);
			}
		}
	} else {
		allocateDescription->restoreObjects(env);
	}

	if (NULL == result) {
		Assert_MM_mustHaveExclusiveVMAccess(env->getOmrVMThread());

		/* check if this is a taxation point or a true failure */
		if (0 == _bytesRemainingBeforeTaxation) {
			allocateDescription->saveObjects(env);
			collector->taxationEntryPoint(env, this, allocateDescription);
			allocateDescription->restoreObjects(env);
			result = lockedAllocate(env, context, objectAllocationInterface, allocateDescription, allocationType);
			Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_didPerformTaxationAndReplenish(env->getLanguageVMThread(), context, (uintptr_t)allocationType, allocateDescription->getContiguousBytes(), result);
		}
	}
	if (NULL == result) {
		Assert_MM_mustHaveExclusiveVMAccess(env->getOmrVMThread());

		/* we failed, so this thread will handle the allocation failure */
		reportAllocationFailureStart(env, allocateDescription);
		/* first, try a resize to satisfy without invoking the collector */
		performResize(env, allocateDescription);
		result = lockedAllocate(env, context, objectAllocationInterface, allocateDescription, allocationType);
		Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_didPerformResizeAndReplenish(env->getLanguageVMThread(), context, (uintptr_t)allocationType, allocateDescription->getContiguousBytes(), result);
		if (NULL == result) {
			/* the resize wasn't enough so invoke the collector */
			allocateDescription->saveObjects(env);
			allocateDescription->setAllocationType(allocationType);
			result = collector->garbageCollect(env, this, allocateDescription, J9MMCONSTANT_IMPLICIT_GC_DEFAULT, objectAllocationInterface, replenishingSpace, context);
			Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_didPerformCollect(env->getLanguageVMThread(), context, (uintptr_t)allocationType, allocateDescription->getContiguousBytes(), result);
			allocateDescription->restoreObjects(env);
			if (NULL == result) {
				/* we _still_ failed so invoke an aggressive collect */
				allocateDescription->saveObjects(env);
				result = collector->garbageCollect(env, this, allocateDescription, J9MMCONSTANT_IMPLICIT_GC_AGGRESSIVE, objectAllocationInterface, replenishingSpace, context);
				Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_didPerformAggressiveCollect(env->getLanguageVMThread(), context, (uintptr_t)allocationType, allocateDescription->getContiguousBytes(), result);
				allocateDescription->restoreObjects(env);
			}
		}
		/* allocation failure is over, whether we satisfied the allocate or not */
		reportAllocationFailureEnd(env);
	}

	Trc_MM_MemorySubSpaceTarok_replenishAllocationContextFailed_Exit(env->getLanguageVMThread(), result);
	return result;
}

void* 
MM_MemorySubSpaceTarok::lockedAllocate(MM_EnvironmentBase *env, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType)
{
	void* result = NULL;
	
	/* arraylet leaves need to be allocated directly after a new region has been found so fall through to the replenish path in that case */
	if (MM_MemorySubSpace::ALLOCATION_TYPE_LEAF != allocationType) {
		result = ((MM_AllocationContextTarok *)context)->lockedAllocate(env, objectAllocationInterface, allocateDescription, allocationType);
	}

	if (NULL == result) {
		/* we failed so do the locked replenish */
		result = lockedReplenishAndAllocate(env, context, objectAllocationInterface, allocateDescription, allocationType);
	}
	
	return result;
}


/****************************************
 * Expansion/Contraction
 ****************************************
 */

/**
 * Adjust the specified expansion amount by the specified user increment amount (i.e. -Xmoi)
 * @return the updated expand size
 */
uintptr_t
MM_MemorySubSpaceTarok::adjustExpansionWithinUserIncrement(MM_EnvironmentBase *env, uintptr_t expandSize)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env->getOmrVM());

	if (extensions->allocationIncrementSetByUser) {
		uintptr_t expandIncrement = extensions->allocationIncrement;

		/* increment of 0 means no expansion is to occur. Don't round  to a size of 0 */
		if (0 == expandIncrement) {
			return expandSize;
		}

		/* Round to the Xmoi value */
		return MM_Math::roundToCeiling(expandIncrement, expandSize);
	}
	
	return MM_MemorySubSpace::adjustExpansionWithinUserIncrement(env, expandSize);
}

/**
 * Determine the maximum expansion amount the memory subspace can expand by.
 * The amount returned is restricted by values within the receiver of the call, as well as those imposed by
 * the parents of the receiver and the owning MemorySpace of the receiver.
 * 
 * @return the amount by which the receiver can expand
 */
uintptr_t
MM_MemorySubSpaceTarok::maxExpansionInSpace(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env->getOmrVM());
	uintptr_t expandIncrement = extensions->allocationIncrement;
	uintptr_t maxExpandAmount;
	
	if (extensions->allocationIncrementSetByUser) {
		/* increment of 0 means no expansion */
		if (0 == expandIncrement) {
			return 0;
		}
	}

	maxExpandAmount = MM_MemorySubSpace::maxExpansionInSpace(env);

	return maxExpandAmount;
}

/**
 * Get the size of heap available for contraction.
 * Return the amount of heap available to be contracted, factoring in any potential allocate that may require the
 * available space.
 * @return The amount of heap available for contraction factoring in the size of the allocate (if applicable)
 */
uintptr_t 
MM_MemorySubSpaceTarok::getAvailableContractionSize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription)
{
	return _physicalSubArena->getAvailableContractionSize(env, this, allocDescription);
}

uintptr_t
MM_MemorySubSpaceTarok::collectorExpand(MM_EnvironmentBase *env, MM_Collector *requestCollector, MM_AllocateDescription *allocDescription)
{
	/* we inherit this collectorExpand method, but don't implement it with this signature. */
	Assert_MM_unreachable();
	return 0;
}

uintptr_t
MM_MemorySubSpaceTarok::collectorExpand(MM_EnvironmentBase *env)
{
	Trc_MM_MemorySubSpaceTarok_collectorExpand_Entry(env->getLanguageVMThread());
	
	_expandLock.acquire();

	/* Determine the amount to expand the heap */
	uintptr_t expandSize = calculateCollectorExpandSize(env);
	Assert_MM_true((0 == expandSize) || (_heapRegionManager->getRegionSize() == expandSize));

	_extensions->heap->getResizeStats()->setLastExpandReason(SATISFY_COLLECTOR);
	
	/* expand by a single region */
	/* for the most part the code path is not multi-threaded safe, so we do this under expandLock */
	uintptr_t expansionAmount= expand(env, expandSize);
	Assert_MM_true((0 == expansionAmount) || (expandSize == expansionAmount));

	/* Inform the requesting collector that an expand attempt took place (even if the expansion failed) */
	MM_IncrementalGenerationalGC *collector = (MM_IncrementalGenerationalGC*)MM_GCExtensions::getExtensions(env)->getGlobalCollector();
	Assert_MM_true(NULL != collector);
	collector->collectorExpanded(env, this, expansionAmount);

	_expandLock.release();

	Trc_MM_MemorySubSpaceTarok_collectorExpand_Exit(env->getLanguageVMThread(), expansionAmount);
	
	return expansionAmount;
}

/**
 * Perform the contraction/expansion based on decisions made by checkResize.
 * Adjustments in contraction size is possible (because compaction might have yielded less then optimal results),
 * therefore allocDescriptor is still passed.
 * @return the actual amount of resize (having intptr_t return result will contain valid value only if contract/expand size is half of maxOfuintptr_t)
 */
intptr_t
MM_MemorySubSpaceTarok::performResize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription)
{
	uintptr_t oldVMState = env->pushVMstate(OMRVMSTATE_GC_PERFORM_RESIZE);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	/* If -Xgc:fvtest=forceTenureResize is specified, then repeat a sequence of 5 expands followed by 5 contracts */	
	if (extensions->fvtest_forceOldResize) {
		uintptr_t resizeAmount = 0;
		uintptr_t regionSize = _extensions->regionSize;
		resizeAmount = 2*regionSize;
		if (5 > extensions->fvtest_oldResizeCounter) {
			uintptr_t expansionSize = MM_Math::roundToCeiling(extensions->heapAlignment, resizeAmount);
			expansionSize = MM_Math::roundToCeiling(regionSize, expansionSize);
			if (canExpand(env, expansionSize)) {
				extensions->heap->getResizeStats()->setLastExpandReason(FORCED_NURSERY_EXPAND);
				_contractionSize = 0;
				_expansionSize = expansionSize;
				extensions->fvtest_oldResizeCounter += 1;
			}
		} else if (10 > extensions->fvtest_oldResizeCounter) {
			uintptr_t contractionSize = MM_Math::roundToCeiling(extensions->heapAlignment, resizeAmount);
			contractionSize = MM_Math::roundToCeiling(regionSize, contractionSize);
			if (canContract(env, contractionSize)) {
				_contractionSize = contractionSize;
				extensions->heap->getResizeStats()->setLastContractReason(FORCED_NURSERY_CONTRACT);
				_expansionSize = 0;
				extensions->fvtest_oldResizeCounter += 1;
			}
		} 
		
		if (10 <= extensions->fvtest_oldResizeCounter) {
			extensions->fvtest_oldResizeCounter = 0;
		}
	}	
	
	intptr_t resizeAmount = 0;

	if (_contractionSize != 0) {
		resizeAmount = -(intptr_t)performContract(env, allocDescription);
	} else if (_expansionSize != 0) {
		resizeAmount = performExpand(env);
	}
	
	env->popVMstate(oldVMState);

	return resizeAmount;
}

/**
 * Calculate the contraction/expansion size required (if any). Do not perform anything yet.
 */
void
MM_MemorySubSpaceTarok::checkResize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool _systemGC)
{
	uintptr_t oldVMState = env->pushVMstate(OMRVMSTATE_GC_CHECK_RESIZE);

	Trc_MM_MemorySubSpaceTarok_checkResize_1(env->getLanguageVMThread(), _extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd ? "true" : "false");

	intptr_t heapSizeChange = calculateHeapSizeChange(env, allocDescription, _systemGC);

	intptr_t edenChangeRegions = _extensions->globalVLHGCStats._heapSizingData.edenRegionChange;
	intptr_t edenChangeRegionsBytes = edenChangeRegions * (intptr_t)_heapRegionManager->getRegionSize();

	Trc_MM_MemorySubSpaceTarok_checkResize_2(env->getLanguageVMThread(), heapSizeChange, edenChangeRegionsBytes);

	ExpandReason nonEdenHeapLastExpandReason = _extensions->heap->getResizeStats()->getLastExpandReason();
	ContractReason nonEdenHeapLastContractReason = _extensions->heap->getResizeStats()->getLastContractReason();

	if (edenChangeRegionsBytes != 0) {
		/* 
		 * Report eden sizing by itself, as well as why eden is being resized.
		 * When contract/expand is actually performed, VGC will report the -overall- change in heap size, and report it accordingly.
		 * This -overall- change in heap size, is change in eden size + change in non-eden size 
		 */
		if (edenChangeRegionsBytes > 0) {
			_extensions->heap->getResizeStats()->setLastExpandReason(EDEN_EXPANDING);
			reportHeapResizeAttempt(env, edenChangeRegionsBytes, HEAP_EXPAND, MEMORY_TYPE_NEW);
		} else if (edenChangeRegionsBytes < 0) {
			_extensions->heap->getResizeStats()->setLastContractReason(EDEN_CONTRACTING);
			reportHeapResizeAttempt(env, (edenChangeRegionsBytes * -1), HEAP_CONTRACT, MEMORY_TYPE_NEW);
		}

		/* 
		 * Now that eden resizing has been reported, revert back to previous expand/contract reasons if non-eden sizing logic set any. 
		 * This makes sure that when VGC reports total heap size change, the reason that is used is non-eden resizing reason, instead of repeating eden sizing resize reason 
		 */
		if (heapSizeChange > 0) {
			_extensions->heap->getResizeStats()->setLastExpandReason(nonEdenHeapLastExpandReason);
		} else if (heapSizeChange < 0) {
			_extensions->heap->getResizeStats()->setLastContractReason(nonEdenHeapLastContractReason);
		}
	}

	/* Adjust the heap size by both the required amount for eden AND non-eden. Non-eden size should generally be kept the same size, so that GMP kickoff, and incremental defragmentation timing stays accurate */
	heapSizeChange += edenChangeRegionsBytes;

	if (0 > heapSizeChange) {
		_contractionSize = (uintptr_t)(heapSizeChange * -1);
		_expansionSize = 0;
	} else if (0 < heapSizeChange) {
		_contractionSize = 0;
		_expansionSize = (uintptr_t)heapSizeChange;
	} else {
		_contractionSize = 0;
		_expansionSize = 0;
	}

	_extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd = false;

	env->popVMstate(oldVMState);
}

/**
 * Calculate by how many bytes we should change the current heap size. 
 * @return positive number of bytes represents how many bytes the heap should expand. 
 * 		   If heap should contract, a negative number representing how many bytes the heap should contract is returned.
 */
intptr_t
MM_MemorySubSpaceTarok::calculateHeapSizeChange(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool _systemGC) 
{
	intptr_t sizeChange = 0;

	bool expandToSatisfy = false;
	uintptr_t sizeInRegionsRequired = 0;

	if (NULL != allocDescription) {
		sizeInRegionsRequired = 1;
		if (allocDescription->isArrayletSpine()) {
			sizeInRegionsRequired += allocDescription->getNumArraylets();
		}

		if (sizeInRegionsRequired > _globalAllocationManagerTarok->getFreeRegionCount()) {
			expandToSatisfy = true;
		}
	}

	/* 
	 * Heap sizing is driven by "hybrid Heap overhead". This is a blended value, which combines free memory in "tenure" along with observed GC overhead.
	 * If the hybrid score is too high (can be acheived by low free memory %, high gc %, or a hybrid of the 2), then the heap calculates an expansion value.
	 * If the hybrid score is too low (can be acheived with high free memory %, low gc %, or hybrid of the 2), then the heap calculates a contraction value.
	 * 
	 * Notes: - If gc % is very low (suggesting contraction), and memory % is also very low (suggesting expansion), the hybrid heap score will likely suggest expansion to avoid OOM error
	 *        - If the hybrid value falls within the acceptable thresholds, the heap will not resize
	 */
	double hybridHeapScore = calculateCurrentHybridHeapOverhead(env);
	
	/* Based on the hybrid overhead of gc cpu, and free memory, decide if heap should expand or contract */
	if ((hybridHeapScore > (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified) || expandToSatisfy) {
		/* Try to expand the heap. Note: We enter this block even if readyToResizeAtGlobalEnd might be false, since expansion might be necessary if free space is very small and allocation failure will occur */
		sizeChange = (intptr_t)calculateExpansionSize(env, allocDescription, _systemGC, expandToSatisfy, sizeInRegionsRequired);
	} else if ((hybridHeapScore < (double)_extensions->heapContractionGCTimeThreshold._valueSpecified) && _extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd) {
		/* Try to contract the heap */
		sizeChange = calculateContractionSize(env, allocDescription, _systemGC, true);
	}

	if ((0 == sizeChange) && ((double)_extensions->heapContractionGCTimeThreshold._valueSpecified <= hybridHeapScore)) {
		/* 
		 * There are certain edge cases where the heap should shrink in order to respect Xsoftmx, that will not be picked up if hybrid heap score is ABOVE heapContractionGCTimeThreshold 
		 * We need to inform the calculateContractionSize() that it should not try to get the hybrid heap score within acceptable bounds, but rather, should 
		 * just make sure -Xsoftmx is being respected
		 */
		sizeChange = calculateContractionSize(env, allocDescription, _systemGC, false);
	}

	return sizeChange;
}	

double
MM_MemorySubSpaceTarok::calculateHybridHeapOverhead(MM_EnvironmentBase *env, intptr_t heapChange)
{
	double gcPercentage = calculateGcPctForHeapChange(env, heapChange);
	double freeMemComponant = mapMemoryPercentageToGcOverhead(env, heapChange);

	if (0 == heapChange) {
		/* Do not trigger this tracepoint for heapChange != 0, since this function is run dozens of time when changing heap size */
		Trc_MM_MemorySubSpaceTarok_calculateHybridHeapOverhead(env->getLanguageVMThread(), gcPercentage, freeMemComponant);
	}
	return MM_Math::weightedAverage(gcPercentage, freeMemComponant, GMP_OVERHEAD_WEIGHT);
}

double MM_MemorySubSpaceTarok::mapMemoryPercentageToGcOverhead(MM_EnvironmentBase *env, intptr_t heapSizeChange)
{	
	double memoryScore = 0.0;

	/* At this point, eden is full, so all the free space is all part of tenure */
	uintptr_t tenureSize = getActiveMemorySize() - (uintptr_t)_extensions->globalVLHGCStats._heapSizingData.reservedSize;
	uintptr_t freeTenure = (uintptr_t)_extensions->globalVLHGCStats._heapSizingData.freeTenure;

	if (0 == heapSizeChange) {
		Trc_MM_MemorySubSpaceTarok_mapMemoryPercentageToGcOverhead_1(env->getLanguageVMThread(), tenureSize, freeTenure);
	}
	
	if (tenureSize < freeTenure) {
		/* 
		 * In certain edge cases (usually in startup), "tenure" is measured slightly innacuratly (due to dynamics of survivor space), resulting in free tenure memory being innacurate. 
		 * Counter-intuitively, free tenure memory here is very small, so suggest expansion 
		 */
		memoryScore = 2 * _extensions->heapExpansionGCTimeThreshold._valueSpecified;

	} else {

		intptr_t newFreeTenureSize = (intptr_t)freeTenure + heapSizeChange;
		intptr_t newTotalMemorySize = (intptr_t)tenureSize + heapSizeChange;
		double freeMemoryRatio = ((double)newFreeTenureSize/ (double)newTotalMemorySize) * 100;
		if (0 != heapSizeChange) {
			Trc_MM_MemorySubSpaceTarok_mapMemoryPercentageToGcOverhead_2(env->getLanguageVMThread(), heapSizeChange, freeMemoryRatio);
		}

		if ((0 == freeMemoryRatio) || (0 >= newTotalMemorySize) || (0 >= newFreeTenureSize)) {
			/* The heap size change will result in no free memory left - return a very high score, suggesting expansion */
			memoryScore = 100.0;
		} else {
			uintptr_t xminf = _extensions->heapFreeMinimumRatioMultiplier;
			uintptr_t xmaxf = _extensions->heapFreeMaximumRatioMultiplier;
			uintptr_t xmint = _extensions->heapContractionGCTimeThreshold._valueSpecified;
			uintptr_t xmaxt = _extensions->heapExpansionGCTimeThreshold._valueSpecified;

			double freeSpaceToGcPctRatio = (double)(xmaxt - xmint) / (xmaxf - xminf);

			double linearMemoryScore = xmaxt - ((freeMemoryRatio - xminf) *  freeSpaceToGcPctRatio);

			/* Adjust the weight for when free memory is low - the function maps to a higher gc cpu overhead (suggesting expansion) */
			double adjustedMemoryScore = linearMemoryScore * ( (freeMemoryRatio + 10) / freeMemoryRatio);

			memoryScore = OMR_MAX(adjustedMemoryScore, 0);
		}
	}

	Trc_MM_MemorySubSpaceTarok_mapMemoryPercentageToGcOverhead_3(env->getLanguageVMThread(), memoryScore);

	return memoryScore;
}


uintptr_t 
MM_MemorySubSpaceTarok::calculateExpansionSize(MM_EnvironmentBase * env, MM_AllocateDescription *allocDescription, bool systemGc, bool expandToSatisfy, uintptr_t sizeInRegionsRequired) 
{
	
	if((NULL == _physicalSubArena) || !_physicalSubArena->canExpand(env) || (maxExpansionInSpace(env) == 0 )) {
		/* The PSA or memory sub space cannot be expanded ... we are done */
		return 0;
	} 

	uintptr_t sizeInBytesRequired = sizeInRegionsRequired * _heapRegionManager->getRegionSize();
	uintptr_t expansionSize = calculateExpansionSizeInternal(env, sizeInBytesRequired, expandToSatisfy);
	
	return expansionSize;	
}

intptr_t 
MM_MemorySubSpaceTarok::calculateContractionSize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool systemGC, bool shouldIncreaseHybridHeapScore)
{
	Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Entry(env->getLanguageVMThread(), systemGC ? "true" : "false");

	/* If PSA or memory sub space can't be shrunk don't bother trying */
	if ( (NULL == _physicalSubArena) || !_physicalSubArena->canContract(env) || (maxContraction(env) == 0)) {
		Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit1(env->getLanguageVMThread());
		return 0;
	}

	/* Don't shrink if we have not met the allocation request... we will be expanding soon if possible anyway */
	if (NULL != allocDescription) {
		uintptr_t sizeInRegionsRequired = 1;
		if (allocDescription->isArrayletSpine()) {
			sizeInRegionsRequired += allocDescription->getNumArraylets();
		}
		uintptr_t freeRegionCount = _globalAllocationManagerTarok->getFreeRegionCount();
		if (sizeInRegionsRequired >= freeRegionCount) {
			Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit4(env->getLanguageVMThread(), sizeInRegionsRequired, freeRegionCount);
			return 0;
		}
	}

	/* Don't shrink if we expanded in last extensions->heapContractionStabilizationCount global collections */
	/* Note that the gcCount includes System GCs, PGCs, AFs and GMP increments */
	uintptr_t gcCount = _extensions->globalVLHGCStats.gcCount;
	if (_extensions->heap->getResizeStats()->getLastHeapExpansionGCCount() + _extensions->heapContractionStabilizationCount > gcCount) {
		Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit5(env->getLanguageVMThread());
		return 0;	
	}	

	/* Don't shrink if its a system GC and we had less than -Xminf free at 
	 * the start of the garbage collection 
	 */ 
	 if (systemGC) {
	 	uintptr_t minimumFree = (getActiveMemorySize() / _extensions->heapFreeMinimumRatioDivisor) 
								* _extensions->heapFreeMinimumRatioMultiplier;
		uintptr_t freeBytesAtSystemGCStart = _extensions->heap->getResizeStats()->getFreeBytesAtSystemGCStart();
		
		if (freeBytesAtSystemGCStart < minimumFree) {
	 		Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit6(env->getLanguageVMThread(), freeBytesAtSystemGCStart, minimumFree);
	 		return 0;	
		}	
	 }

	MM_Heap * heap = MM_GCExtensions::getExtensions(_extensions)->getHeap();
	uintptr_t actualSoftMx = heap->getActualSoftMxSize(env);

	if(0 != actualSoftMx) {
		if(actualSoftMx < getActiveMemorySize()) {
			/* the softmx is less than the currentsize so we're going to attempt an aggressive contract */
			intptr_t contractionSize = (intptr_t)(getActiveMemorySize() - actualSoftMx) * -1;
			_extensions->heap->getResizeStats()->setLastContractReason(SATISFY_SOFT_MX);
			return contractionSize;
		}
	}

	/* No need to shrink if we will not be above -Xmaxf after satisfying the allocate */
	uintptr_t allocSize = allocDescription ? allocDescription->getBytesRequested() : 0;
		
	/* 
	 * How much, if any, do we need to contract by? If we entered this function in attempts to increase hybrid heap score, we need to determine
	 * by how much the heap should shrink to meet the desired hybrid heap score. 
	 * If not, we only entered this function in attempts to respect -Xsoftmx, and should skip this block entirely
	 */
	uintptr_t contractSize = 0;
	if (shouldIncreaseHybridHeapScore || _extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd) {
		contractSize = calculateTargetContractSize(env, allocSize);
	}
	
	if (0 == contractSize) {
		Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit3(env->getLanguageVMThread());
		return 0;
	}		
	
	/* Remember reason for contraction for later */
	_extensions->heap->getResizeStats()->setLastContractReason(FREE_SPACE_HIGH_OR_GC_LOW);
		
	Trc_MM_MemorySubSpaceTarok_timeForHeapContract_Exit7(env->getLanguageVMThread(), contractSize);
	return (intptr_t)contractSize * -1;
}

/**
 * Expand the heap by required amount
 * @return
 */
uintptr_t
MM_MemorySubSpaceTarok::performExpand(MM_EnvironmentBase *env)
{
	uintptr_t actualExpandAmount;
	
	Trc_MM_MemorySubSpaceTarok_performExpand_Entry(env->getLanguageVMThread(), _expansionSize);

	actualExpandAmount= expand(env, _expansionSize);
	
	_expansionSize = 0;
	
	if (actualExpandAmount > 0 ) {
	
		/* Remember the gc count at the time last expansion. If expand is outside a gc this will be 
		 * number of last gc.
	 	 */ 
		/* Note that the gcCount includes System GCs, PGCs, AFs and GMP increments */
		uintptr_t gcCount = _extensions->globalVLHGCStats.gcCount;

		_extensions->heap->getResizeStats()->setLastHeapExpansionGCCount(gcCount);
	}	

	Trc_MM_MemorySubSpaceTarok_performExpand_Exit(env->getLanguageVMThread(), actualExpandAmount);
	return actualExpandAmount;
}

/**
 * Determine how much we should attempt to contract heap by and call contract()
 * @return The amount we actually managed to contract the heap
 */
uintptr_t
MM_MemorySubSpaceTarok::performContract(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription)
{
	uintptr_t contractSize, targetContractSize, maximumContractSize;
	uintptr_t allocationSize = 0;
	if (NULL != allocDescription) {
		allocationSize = allocDescription->getBytesRequested();
	}
	
	Trc_MM_MemorySubSpaceTarok_performContract_Entry(env->getLanguageVMThread(), allocationSize);
	
	/* Work out the upper limit of the contract size.  We may not be able to contract
	 * by this much as we may not have this much storage free at end of heap in first place
	*/ 
	targetContractSize = _contractionSize;
	
	/* Contract no longer outstanding so reset */
	_contractionSize = 0;
	
	if (targetContractSize == 0 ) {
		Trc_MM_MemorySubSpaceTarok_performContract_Exit1(env->getLanguageVMThread());
		return 0;	
	}	
	
	/* We can only contract within the limits of the last free chunk and we 
	 * need to make sure we don't contract and lose the only chunk of free storage
	 * available to satisfy the allocation request.
	 * The call will adjust for the allocation requirements (if any)
	 */
	maximumContractSize = getAvailableContractionSize(env, allocDescription);  
	
	/* round down to multiple of heap alignment */
	maximumContractSize= MM_Math::roundToFloor(_extensions->heapAlignment, maximumContractSize);
	
	/* Decide by how much to contract */
	if (targetContractSize > maximumContractSize) {
		contractSize= maximumContractSize;
		Trc_MM_MemorySubSpaceTarok_performContract_Event1(env->getLanguageVMThread(), targetContractSize, maximumContractSize, contractSize);
	} else {
		contractSize= targetContractSize;
		Trc_MM_MemorySubSpaceTarok_performContract_Event2(env->getLanguageVMThread(), targetContractSize, maximumContractSize, contractSize);
	}
	
	contractSize = MM_Math::roundToFloor(_extensions->regionSize, contractSize);
	
	if (contractSize == 0 ) {
		Trc_MM_MemorySubSpaceTarok_performContract_Exit2(env->getLanguageVMThread());
		return 0;	
	} else {
		uintptr_t actualContractSize= contract(env, contractSize);
		if (actualContractSize > 0 ) {
			/* Remember the gc count at the time of last contraction. If contract is outside a gc 
	 		 * this will be number of last gc.
	 		 */
			/* Note that the gcCount includes System GCs, PGCs, AFs and GMP increments */
			uintptr_t gcCount = _extensions->globalVLHGCStats.gcCount;

			_extensions->heap->getResizeStats()->setLastHeapContractionGCCount(gcCount);
		}	

		Trc_MM_MemorySubSpaceTarok_performContract_Exit3(env->getLanguageVMThread(), actualContractSize);
		return actualContractSize;
	}	
}


/**
 * Determine the amount of heap to contract.
 * Calculate the contraction size while factoring in the pending allocate and whether a contract based on
 * percentage of GC time to total time is required.  If there is room to contract, the value is derived from,
 * 1) The heap free ratio multipliers
 * 2) The heap maximum/minimum contraction sizes
 * 3) The heap alignment
 * @note We use the approximate heap size to account for defered work that may during execution free up more memory.
 * @return the recommended amount of heap in bytes to contract.
 */
uintptr_t
MM_MemorySubSpaceTarok::calculateTargetContractSize(MM_EnvironmentBase *env, uintptr_t allocSize)
{
	Trc_MM_MemorySubSpaceTarok_calculateTargetContractSize_Entry(env->getLanguageVMThread(), allocSize);
	uintptr_t contractionSize = 0;

	/* If there is not enough memory to satisfy the alloc, don't contract.  If allocSize is greater than the total free memory,
	 * the currentFree value is a large positive number (negative unsigned number calculated above).
	 */
	if (allocSize > getApproximateActiveFreeMemorySize()) {
		return 0;
	}

	/* At this point, we know that our hybrid heap score is too high, so we aim to simply get within acceptable bounds */
	uintptr_t heapSizeWithinGoodHybridRange = getHeapSizeWithinBounds(env);

	if (0 != heapSizeWithinGoodHybridRange) {
		/* This means the heap size we are aiming for is actually possible. This is likely always true for contraction of the heap - but may not always be true for expansion */

		contractionSize = getActiveMemorySize() - heapSizeWithinGoodHybridRange;

		if (contractionSize > heapSizeWithinGoodHybridRange) {
			/* A calculation went wrong due to overflow - do not contract */
			contractionSize = 0;
		} else if (getApproximateActiveFreeMemorySize() < (allocSize + contractionSize)) {
			/* After we contract, we will not have enough space to satisfy allocation, so don't suggest contraction at all */
			contractionSize = 0;
		} 
	}
	
	Trc_MM_MemorySubSpaceTarok_calculateTargetContractSize_Exit1(env->getLanguageVMThread(), contractionSize);

	return contractionSize;
}	


/**
 * Determine how much space we need to expand the heap by on this GC cycle to get to a better hybrid heap score
 * @note We use the approximate heap size to account for defered work that may during execution free up more memory.
 * @param expandToSatisfy - if TRUE ensure we epxand heap by at least "byteRequired" bytes
 * @return Number of bytes to expand. 0 if no expansion is required
 */
uintptr_t
MM_MemorySubSpaceTarok::calculateExpansionSizeInternal(MM_EnvironmentBase *env, uintptr_t bytesRequired, bool expandToSatisfy)
{
	uintptr_t expandSize = 0;
	
	Trc_MM_MemorySubSpaceTarok_calculateExpandSize_Entry(env->getLanguageVMThread(), bytesRequired);

	uintptr_t gcCount = _extensions->globalVLHGCStats.gcCount;

	if ((_extensions->heap->getResizeStats()->getLastHeapExpansionGCCount() + _extensions->heapExpansionStabilizationCount <= gcCount) && (_extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd || (0 == _extensions->globalVLHGCStats._heapSizingData.freeTenure))) {
		/* Only expand if we didn't expand in last _extensions->heapExpansionStabilizationCount global collections */
		/* Note that the gcCount includes System GCs, PGCs, AFs and GMP increments */
		uintptr_t heapSizeWithinGoodHybridRange = getHeapSizeWithinBounds(env);

		expandSize = heapSizeWithinGoodHybridRange - getActiveMemorySize();
		if (0 != expandSize) {
			_extensions->heap->getResizeStats()->setLastExpandReason(FREE_SPACE_LOW_OR_GC_HIGH);
		}
	}

	if (expandToSatisfy){
		// TO DO - If the last free chunk abuts the end of the heap we only need
		// to expand by (bytesRequired - size of last free chunk) to satisfy the 
		// request.
		expandSize = OMR_MAX(bytesRequired, expandSize);
		_extensions->heap->getResizeStats()->setLastExpandReason(EXPAND_DESPERATE);
	}

	if (expandSize > 0) {
		/* Adjust the expand size within the specified minimum and maximum expansion amounts
		 * (-Xmine and -Xmaxe command line options) if expansion is required
		 */ 
		expandSize = adjustExpansionWithinFreeLimits(env, expandSize);
	
		/* and adjust to user increment values (Xmoi) */
		expandSize = adjustExpansionWithinUserIncrement(env, expandSize);
	}
		
	/* Adjust within -XsoftMx limit */
	if (expandToSatisfy){
		/* we need at least bytesRequired or we will get an OOM */
		expandSize = adjustExpansionWithinSoftMax(env, expandSize, bytesRequired);
	} else {
		/* we are adjusting based on other command line options, so fully respect softmx,
		 * the minimum expand it can allow in this case is 0
		 */
		expandSize = adjustExpansionWithinSoftMax(env, expandSize, 0);
	}
	
	Trc_MM_MemorySubSpaceTarok_calculateExpandSize_Exit1(env->getLanguageVMThread(), expandSize);
	return expandSize;
}

uintptr_t
MM_MemorySubSpaceTarok::getHeapSizeWithinBounds(MM_EnvironmentBase *env)
{
	double currentHybridHeapScore = calculateCurrentHybridHeapOverhead(env);
	uintptr_t recommendedHeapSize = getActiveMemorySize();
	double maxHeapDeviation = 0.25;

	/* 
	 * If the hybrid overhead is too high, we attempt to bring it back down to an acceptable level. 
	 * Conversely, if hybrid overhead is too low, we aim to increase the hybrid overhead to an acceptable level.
	 * 
	 * In order to decrease hybrid overhead, heap must expand. When the heap expands, gc cpu % will decrease, and free memory % will increase, resulting in a lower overhead.
	 * In order to increase hybrid overhead, heap must contract. When heap contracts, gc cpu % will increase, and free memory % will decrease, resulting in a higher hybrid overhead.
	 */
	bool hybridOverheadTooHigh = currentHybridHeapScore > (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified;
	bool foundAcceptableHeapSizeChange = false;
	/* in order to decrease the hybrid overhead, we need to expand the heap. Conversely, to increase hybrid overhead, we contract the heap  */
	intptr_t heapSizeChangeGranularity = hybridOverheadTooHigh ? (intptr_t)_heapRegionManager->getRegionSize() : (-1 * (intptr_t)_heapRegionManager->getRegionSize());
	uintptr_t maxHeapExpansion = (uintptr_t)((1 + maxHeapDeviation) * (double)recommendedHeapSize);
	uintptr_t maxHeapContraction = (uintptr_t)(_extensions->globalVLHGCStats._heapSizingData.freeTenure * maxHeapDeviation);

	intptr_t suggestedChange = heapSizeChangeGranularity;

	/* Aiming to expand the heap so that hybrid heap score is 0.1 below heapExpansionGCTimeThreshold, prevents head from expanding again due to noise */
	double maxHybridOverheadScore = (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified - 0.1;
	double minHybridOverheadScore = (double)_extensions->heapContractionGCTimeThreshold._valueSpecified;

	/* Move the heap size in the right direction (expand/contract) to see what the memory overhead, and gc cpu overhead will be, until we find an acceptable change in heap size */
	while (!foundAcceptableHeapSizeChange) {

		if (hybridOverheadTooHigh) {
			/* We are trying to expand - but the potential expansion amount is too high*/
			if ((recommendedHeapSize + suggestedChange) > maxHeapExpansion) {
				break;
			}
		} else {
			/* 
			 * Leave headroom for free tenure (ie. do not contract by more than 25% of current free tenure space)
			 * This is both to remain symetric with max expansion of 25%, and to prevent overly aggressive contraction
			 */
			if ((suggestedChange * -1) >= (intptr_t)maxHeapContraction) {
				break;
			}
		}

		/* Test what will happen to gc cpu % and free memory % if we expand/contract by heapSizeChange bytes */
		double potentialHybridOverhead = calculateHybridHeapOverhead(env, suggestedChange);

		if ((potentialHybridOverhead <= maxHybridOverheadScore) && (potentialHybridOverhead >= minHybridOverheadScore)) {
			/* The heap size we tested will give us an acceptable amount of free space, and better gc cpu % */
			recommendedHeapSize += suggestedChange;
			foundAcceptableHeapSizeChange = true;
			Trc_MM_MemorySubSpaceTarok_getHeapSizeWithinBounds_1(env->getLanguageVMThread(), recommendedHeapSize, potentialHybridOverhead);
		} else {
			/* The heap size we tried was not satisfactory. Keep searching */
			suggestedChange += heapSizeChangeGranularity;
		}
	}

	/* 
	 * If looking at the overhead curve did not work, base the expansion/contraction off of how high/low the hybrid overhead is. 
	 * For each % above heapExpansionGCTimeThreshold, expand more aggresively. 
	 * Ie, if heapExpansionGCTimeThreshold = 13, and measured hybrid overhead is 20, we want to expand by a larger amount than if measured hybrid overhead is 14.
	 */
	if (!foundAcceptableHeapSizeChange) {
		/* Expansion and contraction may increase/decrease by larger/smaller amounts */
		uintptr_t sizeChangeFactor = 0;

		/* percentDiff is represented as percent between 0 - 100 */
		double percentDiff = 0.0;

		if (currentHybridHeapScore >= (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified) {
			/* Try to bring hybridHeapScore a little bit below _extensions->heapExpansionGCTimeThreshold */
			percentDiff = currentHybridHeapScore - (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified;

			/* Limit the percent difference to prevent any accidental big changes. 
			 * This helps deal with cases with one GC that is too long for whatever reason, causing a massive, undesired increase in heap size 
			 */
			percentDiff = OMR_MAX(percentDiff, 5.0);

			/* Be a bit more aggressive with expansion than contraction.  */	
			sizeChangeFactor = 2;	

		} else if (currentHybridHeapScore <= (double)_extensions->heapContractionGCTimeThreshold._valueSpecified) {
			/* Try to bring hybridHeapScore a little bit above _extensions->heapContractionGCTimeThreshold.
			 * If this doesn't cause the hybrid heap score to increase enough so that it is within acceptable bounds, this path will be taken 
			 * again later, and another small contraction will occur.
			 */
			percentDiff = currentHybridHeapScore - (double)_extensions->heapContractionGCTimeThreshold._valueSpecified;
			sizeChangeFactor = 1;
		} 

		/* Since percentDiff is 0 - 100, make sure to convert it to 0-1 percentage */
		double heapSizePercentChange = (1.0 + ((double)sizeChangeFactor * (percentDiff / 100 )));
		Trc_MM_MemorySubSpaceTarok_getHeapSizeWithinBounds_2(env->getLanguageVMThread(), heapSizePercentChange);
		recommendedHeapSize = (uintptr_t)(heapSizePercentChange * recommendedHeapSize);
	}

	return recommendedHeapSize;
}


double 
MM_MemorySubSpaceTarok::calculateGcPctForHeapChange(MM_EnvironmentBase *env, intptr_t heapSizeChange)
{
	/* 
	 * If we are resizing after PGC, calculate by how much gc % will change for given change in heap size.
	 * Otherwise, we are resizing after a Global GC, and some statistics might be unusable 
	 */

	if (MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType) {
		/* Resizing after a PGC */

		uintptr_t pgcCount = _extensions->globalVLHGCStats.getRepresentativePgcPerGmpCount();

		if ((0 == pgcCount) && (0 == _lastObservedGcPercentage)) {
			/* Very first time we are resizing, assume GC % is heapExpansionGCTimeThreshold. This makes it slightly easier to expand the heap */
			_lastObservedGcPercentage = (double)_extensions->heapExpansionGCTimeThreshold._valueSpecified;

		} else {

			if (0 != heapSizeChange) {
				/* 
				 * Determine what the gc cpu % would be if we changed the heap by heapSizeChange bytes
				 * Main idea is that the change in number of pgc's (per gmp) will be proportional to how much free tenure changes;
				 */
				uintptr_t currentFreeTenure = (uintptr_t)_extensions->globalVLHGCStats._heapSizingData.freeTenure;
				uintptr_t potentialFreeTenure = 0;
				if (heapSizeChange <= (-1 * (intptr_t)currentFreeTenure) ) {
					/* If we try to shrink too much, too fast, tenure will be way too small, causing lots of GC work */
					potentialFreeTenure = 1;
				} else {
					potentialFreeTenure = currentFreeTenure + heapSizeChange;
				}
				pgcCount = (uintptr_t)(((double)potentialFreeTenure / currentFreeTenure) * pgcCount);
			}

			/* For total heap resizing, only consider GMP cpu overhead. PGC overhead is being controlled by eden sizing logic, so no need to include it here */
			double gcActiveTime = (double)_extensions->globalVLHGCStats._heapSizingData.gmpTime; 
			double gcInterval = (double)(pgcCount  * (_extensions->globalVLHGCStats._heapSizingData.avgPgcTimeUs + _extensions->globalVLHGCStats._heapSizingData.avgPgcIntervalUs));
			double gcActiveRatio = gcActiveTime / gcInterval;

			_lastObservedGcPercentage = gcActiveRatio * 100;
		}

	} else {
		Assert_MM_true(MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType);
		/* 
		 * Resizing the heap after global GC. The PGC and GMP timing data MAY no longer be accurate - look at backup methods. 
		 * The values below do not change if heapSizeChange != 0, since global GC is proportional to live data in heap, rather than heap size. 
		 * This means changing the heap size will not change the gc% by much
		 */
		if (NULL != _collector) {		
			_lastObservedGcPercentage = (double)_collector->getGCTimePercentage(env);	
		} else {	
			_lastObservedGcPercentage = (double)_extensions->getGlobalCollector()->getGCTimePercentage(env);	
		}
	}	

	return _lastObservedGcPercentage;
}

uintptr_t
MM_MemorySubSpaceTarok::calculateCollectorExpandSize(MM_EnvironmentBase *env)
{
	Trc_MM_MemorySubSpaceTarok_calculateCollectorExpandSize_Entry(env->getLanguageVMThread());
	
	/* expand by a single region */
	uintptr_t expandSize = _heapRegionManager->getRegionSize(); 
	
	/* Adjust within -XsoftMx limit */
	expandSize = adjustExpansionWithinSoftMax(env, expandSize,0);
	
	Trc_MM_MemorySubSpaceTarok_calculateCollectorExpandSize_Exit1(env->getLanguageVMThread(), expandSize);

	return expandSize; 
}

/**
 * Compare the specified expand amount with the specified minimum and maximum expansion amounts
 * (-Xmine and -Xmaxe command line options) and round the amount to within these limits
 * @return Updated expand size
 */		
MMINLINE uintptr_t		
MM_MemorySubSpaceTarok::adjustExpansionWithinFreeLimits(MM_EnvironmentBase *env, uintptr_t expandSize)
{		
	uintptr_t result = expandSize;
	
	if (expandSize > 0 ) { 
		if(_extensions->heapExpansionMinimumSize > 0 ) {
			result = OMR_MAX(_extensions->heapExpansionMinimumSize, expandSize);
		}
		
		if(_extensions->heapExpansionMaximumSize > 0 ) {
			result = OMR_MIN(_extensions->heapExpansionMaximumSize, expandSize);
		}	
	}
	return result;
}

/**
 * Compare the specified expand amount with -XsoftMX value
 * @return Updated expand size
 */		
MMINLINE uintptr_t		
MM_MemorySubSpaceTarok::adjustExpansionWithinSoftMax(MM_EnvironmentBase *env, uintptr_t expandSize, uintptr_t minimumBytesRequired)
{
	MM_Heap * heap = env->getExtensions()->getHeap();
	uintptr_t actualSoftMx = heap->getActualSoftMxSize(env);
	uintptr_t activeMemorySize = getActiveMemorySize();
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	
	if (0 != actualSoftMx) {
		if ((minimumBytesRequired != 0) && ((activeMemorySize + minimumBytesRequired) > actualSoftMx)) {
			if (J9_EVENT_IS_HOOKED(MM_GCExtensions::getExtensions(env)->omrHookInterface, J9HOOK_MM_OMR_OOM_DUE_TO_SOFTMX)){
				ALWAYS_TRIGGER_J9HOOK_MM_OMR_OOM_DUE_TO_SOFTMX(MM_GCExtensions::getExtensions(env)->omrHookInterface, env->getOmrVMThread(),
						j9time_hires_clock(), heap->getMaximumMemorySize(), heap->getActiveMemorySize(), MM_GCExtensions::getExtensions(env)->softMx, minimumBytesRequired);
				actualSoftMx = heap->getActualSoftMxSize(env);
			}
		}
		if (actualSoftMx < activeMemorySize) {
			/* if our softmx is smaller than our currentsize, we should be contracting not expanding */
			expandSize = 0;
		} else if((activeMemorySize + expandSize) > actualSoftMx) {
			/* we would go past our -XsoftMx so just expand up to it instead */
			expandSize = actualSoftMx - activeMemorySize;
		}
	}
	return expandSize;
}	

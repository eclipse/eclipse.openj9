
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
 * @ingroup GC_Modron_Standard
 */

#if !defined(MEMORYSUBSPACETAROK_HPP_)
#define MEMORYSUBSPACETAROK_HPP_

#include "j9cfg.h"

#include "MemorySubSpace.hpp"

class MM_AllocateDescription;
class MM_EnvironmentBase;
class MM_HeapStats;
class MM_MemoryPool;
class MM_MemorySpace;
class MM_ObjectAllocationInterface;
class MM_AllocationContext;
class MM_AllocationContextTarok;
class MM_GlobalAllocationManagerTarok;
class MM_RegionListTarok;
class MM_HeapRegionManager;
class MM_PhysicalSubArena;

/**
 * @todo Provide class documentation
 * @ingroup GC_Modron_Standard
 */
class MM_MemorySubSpaceTarok : public MM_MemorySubSpace
{
/* data members */
private:
	MM_GlobalAllocationManagerTarok *_globalAllocationManagerTarok;	/**< Provides the API for accessing information about the underlying regions (owned by the AllocationContextTarok instances) */
	bool _allocateAtSafePointOnly;
	volatile uintptr_t _bytesRemainingBeforeTaxation;	/**< The number of bytes which this subspace can use for allocation before triggering an early allocation failure */

	MM_HeapRegionManager *_heapRegionManager;	/**< Stored so that we can resolve the table descriptor for given addresses when asked for a pool corresponding to a specific address */
	MM_LightweightNonReentrantLock _expandLock; /**< Most of the common expand code is not multi-threaded safe (since it used in standard collectors on alloc path fail path which is single threaded)  */
	double _lastObservedGcPercentage; /**< The most recently observed GC percentage (time gc is active / time gc is not active) */
	
protected:
public:
	
/* function members */
private:
	bool initialize(MM_EnvironmentBase *env);
	uintptr_t adjustExpansionWithinFreeLimits(MM_EnvironmentBase *env, uintptr_t expandSize);
	uintptr_t adjustExpansionWithinSoftMax(MM_EnvironmentBase *env, uintptr_t expandSize, uintptr_t minimumBytesRequired);
	uintptr_t calculateExpansionSizeInternal(MM_EnvironmentBase *env, uintptr_t bytesRequired, bool expandToSatisfy);

	/**
	 * @return Current GC percentage expressed as value between 0-100
	 */
	double calculateCurrentGcPct(MM_EnvironmentBase *env) { return calculateGcPctForHeapChange(env, 0); }

	/**
	 *	Calculates the GC percentage if the heap were to change "heapSizeChange" bytes.
	 *  Uses values in VLHGCEnvironment to get most accurate GC percentage available, and resorts to alternative methods if these stats are unavailable
	 *  @param heapSizeChange represents how many bytes we increase/decrease the heap by
	 *  @return GC percentage expressed as a value between 0 and 100, that will occur if we change heap by heapSizeChange bytes. 
	 */
	double calculateGcPctForHeapChange(MM_EnvironmentBase *env, intptr_t heapSizeChange);

	/**
	 * Calculate by how many bytes we should change the current heap size. 
	 * @return positive number of bytes representing how many bytes the heap should expand. 
	 * 		   If heap should contract, a negative representing how many bytes the heap should contract is returned.
	 */
	intptr_t calculateHeapSizeChange(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool _systemGC);

	/**
	 * Calculate the 'hybrid overhead' for the current heap size. It is a hybrid value combining gc ratio, and free tenure heap space 
	 * @return the current hybrid heap overhead
	 */
	double calculateCurrentHybridHeapOverhead(MM_EnvironmentBase *env) { return calculateHybridHeapOverhead(env, 0); }

	/**
	 * Calculates the hybrid heap if the heap were to change by heapSizeChange bytes. 
	 * The hybrid heap score is a value which blends free memory % and gc %, giving each appropriate weight depending on user specified thresholds
	 * @param heapSizeChange by how many bytes the heap will change
	 * @return the hybrid heap overhead if the heap were to change by heapSizeChange bytes
	 */ 
	double calculateHybridHeapOverhead(MM_EnvironmentBase *env, intptr_t heapSizeChange);

	/**
	 * Maps free memory percentage into a "equivalent" gc percentage. 
	 * Memory is measured as thought it would change by heapSizeChange bytes. If "tenure" currently is 2G, and 1G is free, while heapSizeChange is +1G
	 * Memory will be mapped as though "tenure" changed by 1G (ie, Tenure is 3G, and 2G is free)
	 * @param heapSizeChange how much the heapSize will change. If passing in 0, this function will return the current memory overhead
	 * @return the mapped value of free memory to gc%
	 */ 
	double mapMemoryPercentageToGcOverhead(MM_EnvironmentBase *env, intptr_t heapSizeChange);

	/**
	 * Calculates by how many bytes the heap should expand.
	 * @param env[in] the current thread 
	 * @param allocDescription[in] information about the allocation
	 * @param systemGc[in] if system gc just occured
	 * @param expandToSatisfy GC needs to expand by at least `sizeInRegionsRequired` to avoid Global GC/OOM error
	 * @param sizeInRegionsRequired by how many regions heap needs to expand to avoid OOM
	 * @return the number of bytes by which the heap should expand. Return 0 if expansion is not desired, or not possible.
	 */ 
	uintptr_t calculateExpansionSize(MM_EnvironmentBase * env, MM_AllocateDescription *allocDescription, bool systemGc, bool expandToSatisfy, uintptr_t sizeInRegionsRequired); 

	/**
	 * @param shouldIncreaseHybridHeapScore informs the function that it should try to contract in order to meet the hybrid heap score requirements. If this is set to false, then we enter this function only to meet -Xsoftmx
	 * @return the number of bytes by which the heap should contract. Return 0 if contraction is not desired, or not possible
	 */
	intptr_t calculateContractionSize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool systemGC, bool shouldIncreaseHybridHeapScore);

	/**
	 * Attempts to calculate what size of heap will give a hybrid heap score within the acceptable bounds (between heapExpansionGCTimeThreshold and heapContractionGCTimeThreshold). 
	 * @return size of heap that acheives a hybrid heap score within acceptable range. 0 is returned if it is not possible to get heap to a range with an acceptable heap score
	 */ 
	uintptr_t getHeapSizeWithinBounds(MM_EnvironmentBase *env);

	/**
	 * Determine how much space we need to expand the heap by on this GC cycle to meet the collector's requirement.
	 * @param env[in] the current thread  
	 * @return Number of bytes required
	 */
	uintptr_t calculateCollectorExpandSize(MM_EnvironmentBase *env);
	uintptr_t calculateTargetContractSize(MM_EnvironmentBase *env, uintptr_t allocSize);
	uintptr_t performExpand(MM_EnvironmentBase *env);
	uintptr_t performContract(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription);

	/**
	 * This function is inherited but we can't implement it with this signature.
	 * Overridden as private to prevent invocation, and implemented as Assert_MM_unreachable()
	 */
	virtual uintptr_t collectorExpand(MM_EnvironmentBase *env, MM_Collector *requestCollector, MM_AllocateDescription *allocDescription);

	/**
	 * Attempt to allocate the described entity (object, TLH or leaf), replenishing the allocate region if necessary.
	 *
	 * @param env[in] the current thread
	 * @param context[in] the allocation context
	 * @param objectAllocationInterface[in] the interface to use in allocation
	 * @param allocateDescription[in] information about the allocation
	 * @param allocationType[in] the type of entity being allocated
	 * 
	 * @return the entity or NULL on failure 
	 */
	void* lockedAllocate(MM_EnvironmentBase *env, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType);
	
protected:
	virtual void tearDown(MM_EnvironmentBase *env);
	
public:
	static MM_MemorySubSpaceTarok *newInstance(MM_EnvironmentBase *env, MM_PhysicalSubArena *physicalSubArena, MM_GlobalAllocationManagerTarok *gamt, bool usesGlobalCollector, uintptr_t minimumSize, uintptr_t initialSize, uintptr_t maximumSize, uintptr_t memoryType, U_32 objectFlags);
	
	virtual const char *getName() { return MEMORY_SUBSPACE_NAME_GENERIC; }
	virtual const char *getDescription() { return MEMORY_SUBSPACE_DESCRIPTION_GENERIC; }

	virtual MM_MemoryPool *getMemoryPool();
	virtual MM_MemoryPool *getMemoryPool(void *addr);
	virtual MM_MemoryPool *getMemoryPool(uintptr_t size);
	virtual MM_MemoryPool *getMemoryPool(MM_EnvironmentBase *env, 
										void *addrBase, void *addrTop, 
										void * &highAddr);
	virtual uintptr_t getMemoryPoolCount();
	virtual uintptr_t getActiveMemoryPoolCount();
	
	virtual uintptr_t getActiveMemorySize();
	virtual uintptr_t getActiveMemorySize(uintptr_t includeMemoryType);
	
	virtual uintptr_t getActualFreeMemorySize();
	virtual uintptr_t getApproximateFreeMemorySize();
	
	virtual uintptr_t getActualActiveFreeMemorySize();
	virtual uintptr_t getActualActiveFreeMemorySize(uintptr_t includememoryType);
	virtual uintptr_t getApproximateActiveFreeMemorySize();
	virtual uintptr_t getApproximateActiveFreeMemorySize(uintptr_t includememoryType);
	
	virtual uintptr_t getActiveLOAMemorySize(uintptr_t includememoryType);
	virtual uintptr_t getApproximateActiveFreeLOAMemorySize();
	virtual uintptr_t getApproximateActiveFreeLOAMemorySize(uintptr_t includememoryType);
		
	virtual	void mergeHeapStats(MM_HeapStats *heapStats);
	virtual	void mergeHeapStats(MM_HeapStats *heapStats, uintptr_t includeMemoryType);
	virtual void resetHeapStatistics(bool globalCollect);	
	virtual MM_AllocationFailureStats *getAllocationFailureStats();

	virtual void *allocationRequestFailed(MM_EnvironmentBase *env, MM_AllocateDescription *allocateDescription, AllocationType allocationType, MM_ObjectAllocationInterface *objectAllocationInterface, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace);
	virtual void *allocateObject(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure);

	virtual uintptr_t largestDesirableArraySpine()
	{
		return _extensions->getOmrVM()->_arrayletLeafSize;
	}

	virtual void *allocateArrayletLeaf(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure);

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)
	virtual void *allocateTLH(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, MM_ObjectAllocationInterface *objectAllocationInterface, MM_MemorySubSpace *baseSubSpace, MM_MemorySubSpace *previousSubSpace, bool shouldCollectOnFailure);
#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

	virtual void setAllocateAtSafePointOnly(MM_EnvironmentBase *env, bool safePoint) { _allocateAtSafePointOnly = safePoint; };

	/* Calls for internal collection routines */
	virtual void *collectorAllocate(MM_EnvironmentBase *env, MM_Collector *requestCollector, MM_AllocateDescription *allocDescription);
#if defined(J9VM_GC_THREAD_LOCAL_HEAP)
	virtual void *collectorAllocateTLH(MM_EnvironmentBase *env, MM_Collector *requestCollector, MM_AllocateDescription *allocDescription, uintptr_t maximumBytesRequired, void * &addrBase, void * &addrTop);
#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

	/**
	 * Expand the heap by a single region.
	 * Called during a garbage collection operation, typically to avoid aborting.
	 * @param env[in] the current thread
	 * @return the number of bytes expanded
	 */
	uintptr_t collectorExpand(MM_EnvironmentBase *env);

	virtual uintptr_t adjustExpansionWithinUserIncrement(MM_EnvironmentBase *env, uintptr_t expandSize);
	virtual uintptr_t maxExpansionInSpace(MM_EnvironmentBase *env);
	virtual bool expanded(MM_EnvironmentBase *env, MM_PhysicalSubArena *subArena, MM_HeapRegionDescriptor *region, bool canCoalesce);
	virtual uintptr_t getAvailableContractionSize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription);	

	virtual void checkResize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription = NULL, bool _systemGC = false);
	virtual intptr_t performResize(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription = NULL);

	virtual void abandonHeapChunk(void *addrBase, void *addrTop);

	virtual MM_MemorySubSpace *getDefaultMemorySubSpace();
	virtual MM_MemorySubSpace *getTenureMemorySubSpace();

	virtual void reset();
	virtual void rebuildFreeList(MM_EnvironmentBase *env);
	
	virtual void resetLargestFreeEntry();
	virtual void recycleRegion(MM_EnvironmentBase *env, MM_HeapRegionDescriptor *region);
	virtual uintptr_t findLargestFreeEntry(MM_EnvironmentBase *env, MM_AllocateDescription *allocateDescription);

	virtual bool completeFreelistRebuildRequired(MM_EnvironmentBase *env);
	
	virtual void addExistingMemory(MM_EnvironmentBase *env, MM_PhysicalSubArena *subArena, uintptr_t size, void *lowAddress, void *highAddress, bool canCoalesce);
	virtual void *removeExistingMemory(MM_EnvironmentBase *env, MM_PhysicalSubArena *subArena, uintptr_t size, void *lowAddress, void *highAddress);

	virtual bool isActive();

	virtual void *lockedReplenishAndAllocate(MM_EnvironmentBase *env, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType);

	/**
	 * Called by IncrementalGenerationalGC to permit the subspace to do more work before the next taxation.
	 */
	void setBytesRemainingBeforeTaxation(uintptr_t remaining);

	/**
	 * @return the number of bytes which can still be allocated by this subspace before triggering taxation 
	 */
	MMINLINE uintptr_t getBytesRemainingBeforeTaxation() { return _bytesRemainingBeforeTaxation; }
	
	/**
	 * @see MM_MemorySubSpace::selectRegionForContraction()
	 */
	virtual MM_HeapRegionDescriptor * selectRegionForContraction(MM_EnvironmentBase *env, uintptr_t numaNode);

	/**
	 * Called when an allocation context replenishment request fails in order to invoke a GC.
	 * 
	 * @param[in] env The current thread
	 * @param[in] replenishingSpace The subspace which was initially asked to fill the replenishment request and which will be aksed to replenish it once the GC is complete
	 * @param[in] context The allocation context which the sender failed to replenish
	 * @param[in] allocateDescription The allocation request which initiated the allocation failure
	 * @param[in] allocationType The type of allocation request we eventually must satisfy
	 * 
	 * @return The result of the allocation attempted under exclusive
	 */
	void *replenishAllocationContextFailed(MM_EnvironmentBase *env, MM_MemorySubSpace *replenishingSpace, MM_AllocationContext *context, MM_ObjectAllocationInterface *objectAllocationInterface, MM_AllocateDescription *allocateDescription, AllocationType allocationType);

	/**
	 * Attempt to consume the specified number of bytes from the subspace's taxation threshold.
	 * @param env[in] the current thread
	 * @param bytesToConsume the number of bytes about to be allocated
	 * @return true if the bytes were available, false if the threshold has been reached
	 */
	bool consumeFromTaxationThreshold(MM_EnvironmentBase *env, uintptr_t bytesToConsume);

	/**
	 * Create a MemorySubSpaceGeneric object
	 */
	MM_MemorySubSpaceTarok(MM_EnvironmentBase *env, MM_PhysicalSubArena *physicalSubArena, MM_GlobalAllocationManagerTarok *gamt, MM_HeapRegionManager *heapRegionManager, bool usesGlobalCollector, uintptr_t minimumSize, uintptr_t initialSize, uintptr_t maximumSize, uintptr_t memoryType, U_32 objectFlags)
		: MM_MemorySubSpace(env, NULL, physicalSubArena, usesGlobalCollector, minimumSize, initialSize, maximumSize, memoryType, objectFlags)
		, _globalAllocationManagerTarok(gamt)
		, _allocateAtSafePointOnly(false)
		, _bytesRemainingBeforeTaxation(0)
		, _heapRegionManager(heapRegionManager)
		, _lastObservedGcPercentage(0)
	{
		_typeId = __FUNCTION__;
	}
};

#endif /* MEMORYSUBSPACETAROK_HPP_ */

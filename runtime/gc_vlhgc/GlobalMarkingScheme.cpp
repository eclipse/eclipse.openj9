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



#include "j9.h"
#include "j9cfg.h"
#include "j9consts.h"
#include "j2sever.h"
#include "ModronAssertions.h"

#include <string.h>

#include "ArrayObjectModel.hpp"
#include "AtomicOperations.hpp"
#include "CardTable.hpp"
#include "ClassHeapIterator.hpp"
#include "ClassIterator.hpp"
#include "ClassLoaderIterator.hpp"
#include "ClassLoaderClassesIterator.hpp"
#include "ClassLoaderRememberedSet.hpp"
#include "ClassLoaderSegmentIterator.hpp"
#include "ClassModel.hpp"
#include "CycleState.hpp"
#include "Debug.hpp"
#include "EnvironmentVLHGC.hpp"
#if defined(J9VM_GC_FINALIZATION)
#include "FinalizableObjectBuffer.hpp"
#include "FinalizableReferenceBuffer.hpp"
#include "FinalizeListManager.hpp"
#endif /* J9VM_GC_FINALIZATION*/
#include "GCExtensions.hpp"
#include "GlobalCollectionCardCleaner.hpp"
#include "GlobalCollectionNoScanCardCleaner.hpp"
#include "GlobalMarkCardCleaner.hpp"
#include "GlobalMarkingScheme.hpp"
#include "GlobalMarkNoScanCardCleaner.hpp"
#include "Heap.hpp"
#include "HeapMapIterator.hpp"
#include "HeapMapWordIterator.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"
#include "HeapRegionIteratorVLHGC.hpp"
#include "IncrementalGenerationalGC.hpp"
#include "InterRegionRememberedSet.hpp"
#include "MarkMap.hpp"
#include "MarkMapManager.hpp"
#include "MemorySubSpaceRegionIterator.hpp"
#include "ModronTypes.hpp"
#include "ObjectAccessBarrier.hpp"
#include "ObjectModel.hpp"
#include "PacketSlotIterator.hpp"
#include "ParallelDispatcher.hpp"
#include "ParallelTask.hpp"
#include "PointerArrayIterator.hpp"
#include "ReferenceObjectList.hpp"
#include "ReferenceStats.hpp"
#include "RegionBasedOverflowVLHGC.hpp"
#include "RootScanner.hpp"
#include "SegmentIterator.hpp"
#include "StackSlotValidator.hpp"
#include "SublistIterator.hpp"
#include "SublistPool.hpp"
#include "SublistPuddle.hpp"
#include "SublistSlotIterator.hpp"
#include "VMInterface.hpp"
#include "WorkPacketsIterator.hpp"
#include "WorkPacketsVLHGC.hpp"
#include "WorkStack.hpp"

void
MM_ParallelGlobalMarkTask::mainSetup(MM_EnvironmentBase *env)
{
	bool dynamicClassUnloadingEnabled = false;
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	dynamicClassUnloadingEnabled = _cycleState->_dynamicClassUnloadingEnabled;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
	_markingScheme->setCachedState(_cycleState->_markMap, dynamicClassUnloadingEnabled);
}

void
MM_ParallelGlobalMarkTask::mainCleanup(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);

	_markingScheme->clearCachedState();

	MM_GCExtensions::getExtensions(env)->interRegionRememberedSet->resetOverflowedList();
}

void
MM_ParallelGlobalMarkTask::run(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);

	env->_workStack.prepareForWork(env, (MM_WorkPackets *)(_cycleState->_workPackets));

	switch (_action) {
	case MARK_ALL:
		_markingScheme->markLiveObjectsInit(env);
		_markingScheme->markLiveObjectsRoots(env);
		_markingScheme->markLiveObjectsScan(env);
		_markingScheme->markLiveObjectsComplete(env);
		Assert_MM_false(env->_cycleState->_workPackets->getOverflowFlag());
		break;
	case MARK_INIT:
		_markingScheme->markLiveObjectsInit(env);
		Assert_MM_false(env->_cycleState->_workPackets->getOverflowFlag());
		break;
	case MARK_ROOTS:
		_markingScheme->markLiveObjectsRoots(env);
		_markingScheme->resolveOverflow(env);
		Assert_MM_false(env->_cycleState->_workPackets->getOverflowFlag());
		break;
	case MARK_SCAN:
		_markingScheme->markLiveObjectsScan(env);
		Assert_MM_false(env->_cycleState->_workPackets->getOverflowFlag());
		break;
	case MARK_COMPLETE:
		_markingScheme->markLiveObjectsComplete(env);
		Assert_MM_false(env->_cycleState->_workPackets->getOverflowFlag());
		break;
	default:
		Assert_MM_unreachable();
	}

	_markingScheme->flushBuffers(env);
}

void
MM_ParallelGlobalMarkTask::setup(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	if (!env->isMainThread()) {
		Assert_MM_true(NULL == env->_cycleState);
		env->_cycleState = _cycleState;
	} else {
		Assert_MM_true(_cycleState == env->_cycleState);
	}
	env->_markVLHGCStats.clear();
	env->_workPacketStats.clear();

	/* 
	 * Get gc threads cpu time for when mark started, and add it to the stats structure.
	 * If the current platform does not support this operation, there are failsafes in place - no need for an else condition here
	 */
	int64_t gcThreadCpuTime = omrthread_get_cpu_time(env->getOmrVMThread()->_os_thread);
	if (-1 != gcThreadCpuTime) {
		env->_markVLHGCStats._concurrentGCThreadsStartTimeSum += gcThreadCpuTime;
	} 
	
	/* record that this thread is participating in this cycle */
	env->_markVLHGCStats._gcCount = MM_GCExtensions::getExtensions(env)->globalVLHGCStats.gcCount;
	env->_workPacketStats._gcCount = MM_GCExtensions::getExtensions(env)->globalVLHGCStats.gcCount;
}

void
MM_ParallelGlobalMarkTask::cleanup(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	/* 
	 * Get gc threads cpu time for when mark finished, and add it to the stats structure 
	 * If the current platform does not support this operation, there are failsafes in place - no need for an else condition here
	 */
	int64_t gcThreadCpuTime = omrthread_get_cpu_time(env->getOmrVMThread()->_os_thread);
	if (-1 != gcThreadCpuTime) {
		env->_markVLHGCStats._concurrentGCThreadsEndTimeSum += gcThreadCpuTime;
	} 

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats.merge(&env->_markVLHGCStats);
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.merge(&env->_workPacketStats);
	
	if(!env->isMainThread()) {
		env->_cycleState = NULL;
	}

	env->_lastOverflowedRsclWithReleasedBuffers = NULL;
	
	/* record the thread-specific parallelism stats in the trace buffer. This partially duplicates info in -Xtgc:parallel */ 
	Trc_MM_ParallelGlobalMarkTask_parallelStats(
			env->getLanguageVMThread(),
			(U_32)env->getWorkerID(),
			(U_32)j9time_hires_delta(0, env->_workPacketStats._workStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
			(U_32)j9time_hires_delta(0, env->_workPacketStats._completeStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
			(U_32)j9time_hires_delta(0, env->_markVLHGCStats._syncStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
			(U_32)env->_workPacketStats._workStallCount,
			(U_32)env->_workPacketStats._completeStallCount,
			(U_32)env->_markVLHGCStats._syncStallCount,
			env->_workPacketStats.workPacketsAcquired,
			env->_workPacketStats.workPacketsReleased,
			env->_workPacketStats.workPacketsExchanged,
			env->_markVLHGCStats._splitArraysProcessed);
}

bool
MM_ParallelGlobalMarkTask::shouldYieldFromTask(MM_EnvironmentBase *env)
{
	if (!_timeLimitWasHit) {
		PORT_ACCESS_FROM_ENVIRONMENT(env);
		I_64 currentTime = j9time_current_time_millis();
						
		if (currentTime >= _timeThreshold) {
			_timeLimitWasHit = true;
		}
	}
	return _timeLimitWasHit;
}

#if defined(J9MODRON_TGC_PARALLEL_STATISTICS)
void
MM_ParallelGlobalMarkTask::synchronizeGCThreads(MM_EnvironmentBase *envBase, const char *id)
{
	MM_EnvironmentVLHGC* env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 startTime = j9time_hires_clock();
	MM_ParallelTask::synchronizeGCThreads(env, id);
	U_64 endTime = j9time_hires_clock();
	env->_markVLHGCStats.addToSyncStallTime(startTime, endTime);
}

bool
MM_ParallelGlobalMarkTask::synchronizeGCThreadsAndReleaseMain(MM_EnvironmentBase *envBase, const char *id)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 startTime = j9time_hires_clock();
	bool result = MM_ParallelTask::synchronizeGCThreadsAndReleaseMain(env, id);
	U_64 endTime = j9time_hires_clock();
	env->_markVLHGCStats.addToSyncStallTime(startTime, endTime);
	
	return result;	
}

bool
MM_ParallelGlobalMarkTask::synchronizeGCThreadsAndReleaseSingleThread(MM_EnvironmentBase *envBase, const char *id)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 startTime = j9time_hires_clock();
	bool result = MM_ParallelTask::synchronizeGCThreadsAndReleaseSingleThread(env, id);
	U_64 endTime = j9time_hires_clock();
	env->_markVLHGCStats.addToSyncStallTime(startTime, endTime);

	return result;
}

#endif /* J9MODRON_TGC_PARALLEL_STATISTICS */



/**
 * Allocate and initialize a new instance of the receiver.
 * @return a new instance of the receiver, or NULL on failure.
 */
MM_GlobalMarkingScheme *
MM_GlobalMarkingScheme::newInstance(MM_EnvironmentVLHGC *env)
{
	MM_GlobalMarkingScheme *markingScheme;

	markingScheme = (MM_GlobalMarkingScheme *)env->getForge()->allocate(sizeof(MM_GlobalMarkingScheme), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (markingScheme) {
		new(markingScheme) MM_GlobalMarkingScheme(env);
		if (!markingScheme->initialize(env)) {
			markingScheme->kill(env);
			markingScheme = NULL;
		}
	}

	return markingScheme;
}

/**
 * Free the receiver and all associated resources.
 */
void
MM_GlobalMarkingScheme::kill(MM_EnvironmentVLHGC *env)
{
	tearDown(env);
	env->getForge()->free(this);
}

/**
 * Initialize the receivers internal structures and resources.
 * @return true if initialization is successful, false otherwise.
 */
bool
MM_GlobalMarkingScheme::initialize(MM_EnvironmentVLHGC *env)
{
	/* TODO: how to determine this value? It should be large enough that each thread does
	 * real work, but small enough to give good sharing
	 */
	/* Note: this value should divide evenly into the arraylet leaf size so that each chunk
	 * is a block of contiguous memory
	 */
	_arraySplitSize = 4096;
	_interRegionRememberedSet = MM_GCExtensions::getExtensions(env)->interRegionRememberedSet;
	return true;
}

/**
 * Free the receivers internal structures and resources.
 */
void
MM_GlobalMarkingScheme::tearDown(MM_EnvironmentVLHGC *env)
{
}

/**
 * Adjust internal structures to reflect the change in heap size.
 *
 * @param size The amount of memory added to the heap
 * @param lowAddress The base address of the memory added to the heap
 * @param highAddress The top address (non-inclusive) of the memory added to the heap
 * @return true if operation completes with success (always)
 */
bool
MM_GlobalMarkingScheme::heapAddRange(MM_EnvironmentVLHGC *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress)
{
	/* Record the range in which valid objects appear */
	_heapBase = _extensions->heap->getHeapBase();
	_heapTop = _extensions->heap->getHeapTop();
	return true;
}

/**
 * Adjust internal structures to reflect the change in heap size.
 *
 * @param size The amount of memory added to the heap
 * @param lowAddress The base address of the memory added to the heap
 * @param highAddress The top address (non-inclusive) of the memory added to the heap
 * @param lowValidAddress The first valid address previous to the lowest in the heap range being removed
 * @param highValidAddress The first valid address following the highest in the heap range being removed
 * @return true if operation completes with success (always)
 */
bool
MM_GlobalMarkingScheme::heapRemoveRange(MM_EnvironmentVLHGC *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress, void *lowValidAddress, void *highValidAddress)
{
	/* Record the range in which valid objects appear */
	_heapBase = _extensions->heap->getHeapBase();
	_heapTop = _extensions->heap->getHeapTop();
	return true;
}

/****************************************
 * Runtime initialization
 ****************************************
 */
void
MM_GlobalMarkingScheme::mainSetupForGC(MM_EnvironmentVLHGC *env)
{
	/* Initialize the marking stack */
	env->_cycleState->_workPackets->reset(env);
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.clear();

	_interRegionRememberedSet->prepareOverflowedRegionsForRebuilding(env);
}

void
MM_GlobalMarkingScheme::mainCleanupAfterGC(MM_EnvironmentVLHGC *env)
{
	_interRegionRememberedSet->setRegionsAsRebuildingComplete(env);
}

void
MM_GlobalMarkingScheme::workerSetupForGC(MM_EnvironmentVLHGC *env)
{
	env->_workStack.reset(env, env->_cycleState->_workPackets);

	Assert_MM_true(NULL == env->_lastOverflowedRsclWithReleasedBuffers);
}

void
MM_GlobalMarkingScheme::setCachedState(MM_MarkMap *markMap, bool dynamicClassUnloadingEnabled)
{
	Assert_MM_true(NULL == _markMap);
	_markMap = markMap;
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	_dynamicClassUnloadingEnabled = dynamicClassUnloadingEnabled;
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
}

void
MM_GlobalMarkingScheme::clearCachedState()
{
	_markMap = NULL;
}

/****************************************
 * Marking Helpers
 ****************************************
 */
bool
MM_GlobalMarkingScheme::isMarked(J9Object *objectPtr)
{
	bool shouldCheck = isHeapObject(objectPtr);
	if(shouldCheck) {
		return _markMap->isBitSet(objectPtr);
	}

	/* Everything else is considered marked */
	return true;
}

MMINLINE bool
MM_GlobalMarkingScheme::markObjectNoCheck(MM_EnvironmentVLHGC *env, J9Object *objectPtr, bool leafType)
{
	/* If bit not already set in mark map then set it */
	if (!_markMap->atomicSetBit(objectPtr)) {
		return false;
	}

	/* mark successful - Attempt to add to the work stack */
	if(!leafType) {
		env->_workStack.push(env, objectPtr);
	}

	env->_markVLHGCStats._objectsMarked += 1;

	return true;
}

MMINLINE bool
MM_GlobalMarkingScheme::markObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr, bool leafType)
{
	bool didMark = false;

	if (NULL != objectPtr) {
		/* basic sanity tests of object */
		Assert_MM_true(objectPtr != J9_INVALID_OBJECT);
		Assert_MM_objectAligned(env, objectPtr);
		Assert_MM_true(isHeapObject(objectPtr));

		didMark = markObjectNoCheck(env, objectPtr, leafType);
	}

	return didMark;
}

MMINLINE void
MM_GlobalMarkingScheme::rememberReferenceIfRequired(MM_EnvironmentVLHGC *env, J9Object *from, J9Object *to)
{
	if (NULL != to) {
		if ((((UDATA)from) ^ ((UDATA)to)) >= _regionSize) {
			/* Note that we have to set the inter-region reference even if the object is already marked
			 * because it may have been marked because of a reference in another region. ie: this call
			 * unfortunately can't go in the above if(result) block.
			 */
			_interRegionRememberedSet->rememberReferenceForMark(env, from, to);
		}
	}
}

MMINLINE void
MM_GlobalMarkingScheme::markObjectClass(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	_extensions->classLoaderRememberedSet->rememberInstance(env, objectPtr);
	if(isDynamicClassUnloadingEnabled()) {
		j9object_t classObject = (j9object_t)J9GC_J9OBJECT_CLAZZ(objectPtr, env)->classObject;
		Assert_MM_true(J9_INVALID_OBJECT != classObject);
		markObjectNoCheck(env, classObject, false);
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
}

MMINLINE void
MM_GlobalMarkingScheme::updateScanStats(MM_EnvironmentVLHGC *env, UDATA bytesScanned, ScanReason reason)
{
	if (SCAN_REASON_DIRTY_CARD == reason) {
		env->_markVLHGCStats._objectsCardClean += 1;
		env->_markVLHGCStats._bytesCardClean += bytesScanned;
	} else if (SCAN_REASON_PACKET == reason) {
		env->_markVLHGCStats._objectsScanned += 1;
		env->_markVLHGCStats._bytesScanned += bytesScanned;
	} else {
		Assert_MM_true(SCAN_REASON_OVERFLOWED_REGION == reason);
		env->_markVLHGCStats._bytesScanned += bytesScanned;
	} 
}

/****************************************
 * Scanning
 ****************************************
 */
void
MM_GlobalMarkingScheme::scanMixedObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr, ScanReason reason)
{
	fj9object_t *endScanPtr;
	UDATA *descriptionPtr;
	UDATA descriptionBits;
	UDATA descriptionIndex;
#if defined(J9VM_GC_LEAF_BITS)
	UDATA *leafPtr;
	UDATA leafBits;
#endif /* J9VM_GC_LEAF_BITS */
	bool const compressed = env->compressObjectReferences();

	markObjectClass(env, objectPtr);

	/* Object slots */
	volatile fj9object_t *scanPtr = _extensions->mixedObjectModel.getHeadlessObject(objectPtr);
	UDATA objectSize = _extensions->mixedObjectModel.getSizeInBytesWithHeader(objectPtr);

	updateScanStats(env, objectSize, reason);

	endScanPtr = (fj9object_t*)(((U_8 *)objectPtr) + objectSize);
	descriptionPtr = (UDATA *)J9GC_J9OBJECT_CLAZZ(objectPtr, env)->instanceDescription;
#if defined(J9VM_GC_LEAF_BITS)
	leafPtr = (UDATA *)J9GC_J9OBJECT_CLAZZ(objectPtr, env)->instanceLeafDescription;
#endif /* J9VM_GC_LEAF_BITS */

	if (((UDATA)descriptionPtr) & 1) {
		descriptionBits = ((UDATA)descriptionPtr) >> 1;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits = ((UDATA)leafPtr) >> 1;
#endif /* J9VM_GC_LEAF_BITS */
	} else {
		descriptionBits = *descriptionPtr++;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits = *leafPtr++;
#endif /* J9VM_GC_LEAF_BITS */
	}
	descriptionIndex = J9_OBJECT_DESCRIPTION_SIZE - 1;

	while (scanPtr < endScanPtr) {
		/* Determine if the slot should be processed */
		if(descriptionBits & 1) {
			/* As this function can be invoked during concurrent mark the slot is
			 * volatile so we must ensure that the compiler generates the correct
			 * code if markObject() is inlined.
			 */

			GC_SlotObject slotObject(_javaVM->omrVM, scanPtr);
			J9Object* slot = slotObject.readReferenceFromSlot();
			
#if defined(J9VM_GC_LEAF_BITS)
			markObject(env, slot, 1 == (leafBits & 1));
#else /* J9VM_GC_LEAF_BITS */
			markObject(env, slot, false);
#endif /* J9VM_GC_LEAF_BITS */

			rememberReferenceIfRequired(env, objectPtr, slot);
		}
		descriptionBits >>= 1;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits >>= 1;
#endif /* J9VM_GC_LEAF_BITS */
		if (descriptionIndex-- == 0) {
			descriptionBits = *descriptionPtr++;
#if defined(J9VM_GC_LEAF_BITS)
			leafBits = *leafPtr++;
#endif /* J9VM_GC_LEAF_BITS */
			descriptionIndex = J9_OBJECT_DESCRIPTION_SIZE - 1;
		}
		scanPtr = GC_SlotObject::addToSlotAddress((fomrobject_t*)scanPtr, 1, compressed);
	}
}

void
MM_GlobalMarkingScheme::scanReferenceMixedObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr, ScanReason reason)
{
	fj9object_t *endScanPtr;
	UDATA *descriptionPtr;
	UDATA descriptionBits;
	UDATA descriptionIndex;
#if defined(J9VM_GC_LEAF_BITS)
	UDATA *leafPtr;
	UDATA leafBits;
#endif /* J9VM_GC_LEAF_BITS */

	markObjectClass(env, objectPtr);

	I_32 referenceState = J9GC_J9VMJAVALANGREFERENCE_STATE(env, objectPtr);

	bool isReferenceCleared = (GC_ObjectModel::REF_STATE_CLEARED == referenceState) || (GC_ObjectModel::REF_STATE_ENQUEUED == referenceState);
	bool referentMustBeMarked = isReferenceCleared;
	bool referentMustBeCleared = false;
	UDATA referenceObjectOptions = env->_cycleState->_referenceObjectOptions;
	UDATA referenceObjectType = J9CLASS_FLAGS(J9GC_J9OBJECT_CLAZZ(objectPtr, env)) & J9AccClassReferenceMask;
	switch (referenceObjectType) {
	case J9AccClassReferenceWeak:
		referentMustBeCleared = (0 != (referenceObjectOptions & MM_CycleState::references_clear_weak));
		break;
	case J9AccClassReferenceSoft:
		referentMustBeCleared = (0 != (referenceObjectOptions & MM_CycleState::references_clear_soft));
		referentMustBeMarked = referentMustBeMarked || (
			((0 == (referenceObjectOptions & MM_CycleState::references_soft_as_weak))
			&& ((UDATA)J9GC_J9VMJAVALANGSOFTREFERENCE_AGE(env, objectPtr) < _extensions->getDynamicMaxSoftReferenceAge())));
		break;
	case J9AccClassReferencePhantom:
		referentMustBeCleared = (0 != (referenceObjectOptions & MM_CycleState::references_clear_phantom));
		break;
	default:
		Assert_MM_unreachable();
	}
	
	GC_SlotObject referentPtr(_javaVM->omrVM, J9GC_J9VMJAVALANGREFERENCE_REFERENT_ADDRESS(env, objectPtr));

	if (SCAN_REASON_OVERFLOWED_REGION == reason) {
		/* handled when we empty packet to overflow */
	} else {
		if (referentMustBeCleared) {
			/* Discovering this object at this stage in the GC indicates that it is being resurrected. Clear its referent slot. */
			referentPtr.writeReferenceToSlot(NULL);
			/* record that the reference has been cleared if it's not already in the cleared or enqueued state */
			if (!isReferenceCleared) {
				J9GC_J9VMJAVALANGREFERENCE_STATE(env, objectPtr) = GC_ObjectModel::REF_STATE_CLEARED;
			}
		} else if (SCAN_REASON_PACKET == reason) {
			/* we don't need to process cleared or enqueued references */
			if (!isReferenceCleared) {
				env->getGCEnvironment()->_referenceObjectBuffer->add(env, objectPtr);
			}
		}
	}

	volatile fj9object_t * scanPtr = _extensions->mixedObjectModel.getHeadlessObject(objectPtr);
	UDATA objectSize = _extensions->mixedObjectModel.getSizeInBytesWithHeader(objectPtr);
	endScanPtr = (fj9object_t*)(((U_8 *)objectPtr) + objectSize);

	updateScanStats(env, objectSize, reason);

	descriptionPtr = (UDATA *)J9GC_J9OBJECT_CLAZZ(objectPtr, env)->instanceDescription;
#if defined(J9VM_GC_LEAF_BITS)
	leafPtr = (UDATA *)J9GC_J9OBJECT_CLAZZ(objectPtr, env)->instanceLeafDescription;
#endif /* J9VM_GC_LEAF_BITS */

	if(((UDATA)descriptionPtr) & 1) {
		descriptionBits = ((UDATA)descriptionPtr) >> 1;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits = ((UDATA)leafPtr) >> 1;
#endif /* J9VM_GC_LEAF_BITS */
	} else {
		descriptionBits = *descriptionPtr++;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits = *leafPtr++;
#endif /* J9VM_GC_LEAF_BITS */
	}
	descriptionIndex = J9_OBJECT_DESCRIPTION_SIZE - 1;

	bool const compressed = env->compressObjectReferences();
	while(scanPtr < endScanPtr) {
		/* Determine if the slot should be processed */
		if ((descriptionBits & 1) && ((scanPtr != referentPtr.readAddressFromSlot()) || referentMustBeMarked)) {
			/* As this function can be invoked during concurrent mark the slot is
			 * volatile so we must ensure that the compiler generates the correct
			 * code if markObject() is inlined.
			 */

			GC_SlotObject slotObject(_javaVM->omrVM, scanPtr);
			J9Object* slot = slotObject.readReferenceFromSlot();

#if defined(J9VM_GC_LEAF_BITS)
			markObject(env, slot, 1 == (leafBits & 1));
#else /* J9VM_GC_LEAF_BITS */
			markObject(env, slot, false);
#endif /* J9VM_GC_LEAF_BITS */

			rememberReferenceIfRequired(env, objectPtr, slot);
		}
		descriptionBits >>= 1;
#if defined(J9VM_GC_LEAF_BITS)
		leafBits >>= 1;
#endif /* J9VM_GC_LEAF_BITS */
		if(descriptionIndex-- == 0) {
			descriptionBits = *descriptionPtr++;
#if defined(J9VM_GC_LEAF_BITS)
			leafBits = *leafPtr++;
#endif /* J9VM_GC_LEAF_BITS */
			descriptionIndex = J9_OBJECT_DESCRIPTION_SIZE - 1;
		}
		scanPtr = GC_SlotObject::addToSlotAddress((fomrobject_t*)scanPtr, 1, compressed);
	}
}

UDATA
MM_GlobalMarkingScheme::scanPointerArrayObjectSplit(MM_EnvironmentVLHGC *env, J9IndexableObject *objectPtr, UDATA startIndex, ScanReason reason)
{
	UDATA slotsToScan = 0;
	UDATA sizeInElements = _extensions->indexableObjectModel.getSizeInElements(objectPtr);
	if (sizeInElements > 0) {
		Assert_MM_true(startIndex < sizeInElements);
		slotsToScan = sizeInElements - startIndex;
		
		if (slotsToScan > _arraySplitSize) {
			slotsToScan = _arraySplitSize;
			
			/* immediately make the next chunk available for another thread to start processing */
			UDATA nextIndex = startIndex + slotsToScan;
			Assert_MM_true(nextIndex < sizeInElements);
			void *element1 = (void *)objectPtr;
			void *element2 = (void *)((nextIndex << PACKET_ARRAY_SPLIT_SHIFT) | PACKET_ARRAY_SPLIT_TAG);
			Assert_MM_true(nextIndex == (((UDATA)element2) >> PACKET_ARRAY_SPLIT_SHIFT));
			env->_workStack.push(env, element1, element2);
			env->_workStack.flushOutputPacket(env);
			env->_markVLHGCStats._splitArraysProcessed += 1;
		}
		
		/* TODO: this iterator scans the array backwards - change it to forward, and optimize it since we can guarantee the range will be contiguous */
		GC_PointerArrayIterator pointerArrayIterator(_javaVM, (J9Object *)objectPtr);
		pointerArrayIterator.setIndex(startIndex + slotsToScan);
		for (UDATA scanCount = 0; scanCount < slotsToScan; scanCount++) {
			GC_SlotObject *slotObject = pointerArrayIterator.nextSlot();
			if (NULL == slotObject) {
				/* this can happen if the array is only partially allocated */
				break;
			}
			J9Object * value = slotObject->readReferenceFromSlot();
	
			markObject(env, value);
			
			rememberReferenceIfRequired(env, (J9Object *)objectPtr, value);
		}
	}

	/* Return number of bytes scanned */
	uintptr_t const referenceSize = env->compressObjectReferences() ? sizeof(uint32_t) : sizeof(uintptr_t);
	return (slotsToScan * referenceSize);
}

void
MM_GlobalMarkingScheme::scanPointerArrayObject(MM_EnvironmentVLHGC *env, J9IndexableObject *objectPtr, ScanReason reason)
{
	UDATA bytesScanned = 0;
	UDATA workItem = (UDATA)env->_workStack.peek(env);
	if( PACKET_ARRAY_SPLIT_TAG == (workItem & PACKET_ARRAY_SPLIT_TAG) ) {
		env->_workStack.pop(env);
		UDATA index = workItem >> PACKET_ARRAY_SPLIT_SHIFT;
		bytesScanned = scanPointerArrayObjectSplit(env, objectPtr, index, reason);

		Assert_MM_true(SCAN_REASON_PACKET == reason);
		env->_markVLHGCStats._bytesScanned += bytesScanned;
	} else {
		/* do this after peeking so as not to disrupt the workStack before we peek it */
		/* only mark the class the first time we scan any array */
		markObjectClass(env, (J9Object*)objectPtr);

		bytesScanned = scanPointerArrayObjectSplit(env, objectPtr, 0, reason);
		/* ..and account for header size on first scan */
		bytesScanned += _extensions->indexableObjectModel.getHeaderSize(objectPtr);

		updateScanStats(env, bytesScanned, reason);
	}
}

void 
MM_GlobalMarkingScheme::scanClassObject(MM_EnvironmentVLHGC *env, J9Object *classObject, ScanReason reason)
{
	scanMixedObject(env, classObject, reason);

	J9Class *classPtr = J9VM_J9CLASS_FROM_HEAPCLASS((J9VMThread*)env->getLanguageVMThread(), classObject);
	if (NULL != classPtr) {
		J9Object * volatile * slotPtr = NULL;

		do {
			/*
			 * Scan J9Class internals using general iterator
			 * - scan statics fields
			 * - scan call sites
			 * - scan MethodTypes
			 * - scan VarHandle MethodTypes
			 * - scan constants pool objects
			 */
			GC_ClassIterator classIterator(env, classPtr, false);
			while (NULL != (slotPtr = classIterator.nextSlot())) {
				J9Object *value = *slotPtr;
				markObject(env, value);
				rememberReferenceIfRequired(env, classObject, value);
			}

			/*
			 * Usually we don't care about class to class references because its can be marked as a part of alive classloader or find in Hash Table
			 * However we need to scan them for case of Anonymous classes. Its are unloaded on individual basis so it is important to reach each one
			 */
			if (J9_ARE_ANY_BITS_SET(J9CLASS_EXTENDED_FLAGS(classPtr), J9ClassIsAnonymous)) {
				GC_ClassIteratorClassSlots classSlotIterator(_javaVM, classPtr);
				J9Class *classPtr;
				while (NULL != (classPtr = classSlotIterator.nextSlot())) {
					J9Object *value = classPtr->classObject;
					markObject(env, value);
					rememberReferenceIfRequired(env, classObject, value);
				}
			}
			classPtr = classPtr->replacedClass;
		} while (NULL != classPtr);
	}
}

void 
MM_GlobalMarkingScheme::scanClassLoaderObject(MM_EnvironmentVLHGC *env, J9Object *classLoaderObject, ScanReason reason)
{
	scanMixedObject(env, classLoaderObject, reason);

	J9ClassLoader *classLoader = J9VMJAVALANGCLASSLOADER_VMREF((J9VMThread*)env->getLanguageVMThread(), classLoaderObject);
	if ((NULL != classLoader) && (0 == (classLoader->flags & J9CLASSLOADER_ANON_CLASS_LOADER))) {
		GC_VMInterface::lockClasses(_extensions);
		/* (NULL == classLoader->classHashTable) is true ONLY for DEAD class loaders */
		Assert_MM_true(NULL != classLoader->classHashTable);
		GC_ClassLoaderClassesIterator iterator(_extensions, classLoader);
		J9Class *clazz = NULL;
		while (NULL != (clazz = iterator.nextClass())) {
			J9Object * classObject = J9VM_J9CLASS_TO_HEAPCLASS(clazz);
			Assert_MM_true(NULL != classObject);
			markObject(env, classObject);
			rememberReferenceIfRequired(env, classLoaderObject, classObject);
		}
		GC_VMInterface::unlockClasses(_extensions);

		if (NULL != classLoader->moduleHashTable) {
			J9HashTableState walkState;
			J9Module **modulePtr = (J9Module **)hashTableStartDo(classLoader->moduleHashTable, &walkState);
			while (NULL != modulePtr) {
				J9Module * const module = *modulePtr;
				Assert_MM_true(NULL != module->moduleObject);
				markObject(env, module->moduleObject);
				rememberReferenceIfRequired(env, classLoaderObject, module->moduleObject);
				if (NULL != module->moduleName) {
					markObject(env, module->moduleName);
					rememberReferenceIfRequired(env, classLoaderObject, module->moduleName);
				}
				if (NULL != module->version) {
					markObject(env, module->version);
					rememberReferenceIfRequired(env, classLoaderObject, module->version);
				}
				modulePtr = (J9Module**)hashTableNextDo(&walkState);
			}
		}
	}
}

void
MM_GlobalMarkingScheme::scanClassLoaderSlots(MM_EnvironmentVLHGC *env, J9ClassLoader *classLoader, ScanReason reason)
{
	if(NULL != classLoader){
		/* This method should only be necessary for the system class loader and application class loader since they may not
		 * necessarily have a class loader object but still need to keep their loaded classes alive
		 */
		Assert_MM_true((classLoader == _javaVM->systemClassLoader) || (classLoader == _javaVM->applicationClassLoader));
		if (NULL != classLoader->classLoaderObject) {
			markObject(env, classLoader->classLoaderObject);
		} else {
			/* Just scan classes */
			GC_VMInterface::lockClasses(_extensions);
			GC_ClassLoaderClassesIterator iterator(_extensions, classLoader);
			J9Class *clazz = NULL;
			bool success = true;

			while (success && (NULL != (clazz = iterator.nextClass()))) {
				Assert_MM_true(NULL != clazz->classObject);
				/* mark the slot reference*/
				markObject(env, (J9Object *) clazz->classObject);
			}
			GC_VMInterface::unlockClasses(_extensions);
		}
	}
}

void
MM_GlobalMarkingScheme::scanObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr, ScanReason reason)
{
	if (PACKET_INVALID_OBJECT == (UDATA)objectPtr) {
		/* This means that the object was pushed during a GMP cycle but collected by an intervening PGC cycle and invalidated */
		Assert_MM_true(SCAN_REASON_PACKET == reason);
	} else {
		J9Class* clazz = J9GC_J9OBJECT_CLAZZ(objectPtr, env);
		Assert_MM_mustBeClass(clazz);
		switch(_extensions->objectModel.getScanType(clazz)) {
			case GC_ObjectModel::SCAN_MIXED_OBJECT_LINKED:
			case GC_ObjectModel::SCAN_ATOMIC_MARKABLE_REFERENCE_OBJECT:
			case GC_ObjectModel::SCAN_MIXED_OBJECT:
			case GC_ObjectModel::SCAN_OWNABLESYNCHRONIZER_OBJECT:
				scanMixedObject(env, objectPtr, reason);
				break;
			case GC_ObjectModel::SCAN_CLASS_OBJECT:
				scanClassObject(env, objectPtr, reason);
				break;
			case GC_ObjectModel::SCAN_CLASSLOADER_OBJECT:
				scanClassLoaderObject(env, objectPtr, reason);
				break;
			case GC_ObjectModel::SCAN_POINTER_ARRAY_OBJECT:
				scanPointerArrayObject(env, (J9IndexableObject *)objectPtr, reason);
				break;
			case GC_ObjectModel::SCAN_REFERENCE_MIXED_OBJECT:
				scanReferenceMixedObject(env, objectPtr, reason);
				break;
			case GC_ObjectModel::SCAN_PRIMITIVE_ARRAY_OBJECT:
				/* nothing to do */
				break;
			default:
				Trc_MM_GlobalMarkingScheme_scanObject_invalid(env->getLanguageVMThread(), objectPtr, reason);
				Assert_MM_unreachable();
		}
	}
}
	
/**
 * Scan until there are no more work packets to be processed.
 * @note This is a joining scan: a thread will not exit this method until
 * all threads have entered and all work packets are empty.
 */
MMINLINE void
MM_GlobalMarkingScheme::completeScan(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	do {
		J9Object *objectPtr = NULL;
		while (NULL != (objectPtr = (J9Object *)env->_workStack.pop(env))) {
			U_64 scanStartTime = j9time_hires_clock();
			do {
				scanObject(env, objectPtr, SCAN_REASON_PACKET);
				objectPtr = (J9Object *)env->_workStack.popNoWait(env);
			} while (NULL != objectPtr);
			U_64 scanEndTime = j9time_hires_clock();
			env->_markVLHGCStats.addToScanTime(scanStartTime, scanEndTime);
		}
		/* A GMP can create an inconsistency in how we observe overflow:  one thread might fall out of the above loop
		 * due to a timeout while another falls out due to overflow.  This will create a deadlock in overflow processing
		 * since the timeout thread can leave the outer-most loop before the overflowing thread incurs overflow.
		 * Adding a synchronization point here means one additional synchronization per increment but means that a thread
		 * which times out will wait for the overflow to occur before deciding if it needs to handle the overflow or exit
		 * the loop
		 */
		env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
	} while (handleOverflow(env));
}

/****************************************
 * Marking Core Functionality
 ****************************************
 */

#if defined(J9VM_GC_FINALIZATION)
void
MM_GlobalMarkingScheme::scanUnfinalizedObjects(MM_EnvironmentVLHGC *env)
{
	GC_FinalizableObjectBuffer buffer(_extensions);

	/* we need to sync to ensure that all clearable processing is complete up to this point since this phase resurrects objects */ 
	env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);

	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (!region->getUnfinalizedObjectList()->wasEmpty()) {
				if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
					J9Object *object = region->getUnfinalizedObjectList()->getPriorList();
					while (NULL != object) {
						Assert_MM_true(region->isAddressInRegion(object));
						env->_markVLHGCStats._unfinalizedCandidates += 1;

						J9Object* next = _extensions->accessBarrier->getFinalizeLink(object);
						if (markObject(env, object)) {
							/* object was not previously marked -- it is now finalizable so push it to the local buffer */
							env->_markVLHGCStats._unfinalizedEnqueued += 1;
							buffer.add(env, object);
							env->_cycleState->_finalizationRequired = true;
						} else {
							/* object was already marked. It is still unfinalized */
							env->getGCEnvironment()->_unfinalizedObjectBuffer->add(env, object);
						}
						object = next;
					}

					/* Flush the local buffer of finalizable objects to the global list.
				 	 * This is done once per region to ensure that multi-tenant lists
				 	 * only contain objects from the same allocation context
				 	 */
					buffer.flush(env);
				}
			}
		}
	}
	
	/* restore everything to a flushed state before exiting */
	env->getGCEnvironment()->_unfinalizedObjectBuffer->flush(env);
}
#endif /* J9VM_GC_FINALIZATION */

void
MM_GlobalMarkingScheme::scanOwnableSynchronizerObjects(MM_EnvironmentVLHGC *env)
{
	env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);

	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (!region->getOwnableSynchronizerObjectList()->wasEmpty()) {
				if (J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
					J9Object *object = region->getOwnableSynchronizerObjectList()->getPriorList();
					while (NULL != object) {
						Assert_MM_true(region->isAddressInRegion(object));
						env->_markVLHGCStats._ownableSynchronizerCandidates += 1;

						/* read the next link before we add it to the buffer */
						J9Object* next = _extensions->accessBarrier->getOwnableSynchronizerLink(object);
						if (isMarked(object)) {
							env->getGCEnvironment()->_ownableSynchronizerObjectBuffer->add(env, object);
						} else {
							env->_markVLHGCStats._ownableSynchronizerCleared += 1;
						}
						object = next;
					}
				}
			}
		}
	}
	/* restore everything to a flushed state before exiting */
	env->getGCEnvironment()->_ownableSynchronizerObjectBuffer->flush(env);
}

/**
 * The root set scanner for MM_GlobalMarkingScheme.
 * @copydoc MM_RootScanner
 * @ingroup GC_Modron_Standard
 */
class MM_GlobalMarkingSchemeRootMarker : public MM_RootScanner
{
private:
	MM_GlobalMarkingScheme *_markingScheme; /**< the MarkingScheme which created this scanner */
	
private:
	virtual void doSlot(J9Object **slotPtr) {
		_markingScheme->markObject((MM_EnvironmentVLHGC *)_env, *slotPtr);
	}

	virtual void doStackSlot(J9Object **slotPtr, void *walkState, const void* stackLocation) {
		J9Object *object = *slotPtr;
		if (_markingScheme->isHeapObject(object)) {
			/* heap object - validate and mark */
			Assert_MM_validStackSlot(MM_StackSlotValidator(0, *slotPtr, stackLocation, walkState).validate(_env));
			_markingScheme->markObject((MM_EnvironmentVLHGC *)_env, object);
		} else if (NULL != object) {
			/* stack object - just validate */
			Assert_MM_validStackSlot(MM_StackSlotValidator(MM_StackSlotValidator::NOT_ON_HEAP, *slotPtr, stackLocation, walkState).validate(_env));
		}
	}
	
	virtual void doVMThreadSlot(J9Object **slotPtr, GC_VMThreadIterator *vmThreadIterator) {
		J9Object *object = *slotPtr;
		if (_markingScheme->isHeapObject(object)) {
			_markingScheme->markObject((MM_EnvironmentVLHGC *)_env, object);
		} else if (NULL != object) {
			Assert_MM_true(vmthreaditerator_state_monitor_records == vmThreadIterator->getState());
		}
	}

	virtual void doClass(J9Class *clazz) {
		/* we only discover classes through their loaders or other references */
		Assert_MM_unreachable();
	}

	virtual void doClassLoader(J9ClassLoader *classLoader) {
		if(0 == (classLoader->gcFlags & J9_GC_CLASS_LOADER_DEAD)) {
			_markingScheme->markObject((MM_EnvironmentVLHGC *)_env, classLoader->classLoaderObject);
		}
	}

	virtual void doFinalizableObject(j9object_t object) {
		_markingScheme->markObject((MM_EnvironmentVLHGC *)_env, object);
	}

protected:
	
public:
	/**
	 * Scan all root set references from the VM into the heap.
	 * For all slots that are hard root references into the heap, the appropriate slot handler will be called.
	 */
	void 
	scanRoots(MM_EnvironmentBase *env)
	{
		if (_classDataAsRoots) {
			/* The classLoaderObject of a class loader might be in the nursery, but a class loader
			 * can never be in the remembered set, so include class loaders here.
			 */
			scanClassLoaders(env);
			setIncludeStackFrameClassReferences(false);
		} else {
			setIncludeStackFrameClassReferences(true);
		}

		scanThreads(env);
#if defined(J9VM_GC_FINALIZATION)
		scanFinalizableObjects(env);
#endif /* J9VM_GC_FINALIZATION */
		scanJNIGlobalReferences(env);

		if(_stringTableAsRoot){
			scanStringTable(env);
		}
	}

	MM_GlobalMarkingSchemeRootMarker(MM_EnvironmentBase *env, MM_GlobalMarkingScheme *markingScheme)
		: MM_RootScanner(env)
		, _markingScheme(markingScheme)
	{
		_typeId = __FUNCTION__;
	};
};

/**
 * The clearable reference set scanner for MM_GlobalMarkingScheme.
 * @copydoc MM_RootScanner
 * @ingroup GC_Modron_Standard
 */
class MM_GlobalMarkingSchemeRootClearer : public MM_RootScanner
{
private:
	MM_GlobalMarkingScheme *_markingScheme;

	virtual void doSlot(J9Object **slotPtr) {
		assume0(0);  /* Should not have gotten here - how do you clear a generic slot? */
	}

	virtual void doClass(J9Class *clazz) {
		assume0(0);  /* Should not have gotten here - how do you clear a class? */
	}

	virtual void scanWeakReferenceObjects(MM_EnvironmentBase *env) {
		reportScanningStarted(RootScannerEntity_WeakReferenceObjects);
		_markingScheme->scanWeakReferenceObjects(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_WeakReferenceObjects);
	}

	virtual CompletePhaseCode scanWeakReferencesComplete(MM_EnvironmentBase *env) {
		/* No new objects could have been discovered by soft / weak reference processing,
		 * but we must complete this phase prior to unfinalized processing to ensure that
		 * finalizable referents get cleared */
		env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
		return complete_phase_OK;
	}

	virtual void scanSoftReferenceObjects(MM_EnvironmentBase *env) {
		reportScanningStarted(RootScannerEntity_SoftReferenceObjects);
		_markingScheme->scanSoftReferenceObjects(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_SoftReferenceObjects);
	}

	virtual CompletePhaseCode scanSoftReferencesComplete(MM_EnvironmentBase *env) {
		/* do nothing -- no new objects could have been discovered by soft reference processing */
		return complete_phase_OK;
	}

	virtual void scanPhantomReferenceObjects(MM_EnvironmentBase *env) {
		reportScanningStarted(RootScannerEntity_PhantomReferenceObjects);
		_markingScheme->scanPhantomReferenceObjects(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_PhantomReferenceObjects);
	}

	virtual CompletePhaseCode scanPhantomReferencesComplete(MM_EnvironmentBase *env) {
		reportScanningStarted(RootScannerEntity_PhantomReferenceObjectsComplete);
		if (env->_currentTask->synchronizeGCThreadsAndReleaseSingleThread(env, UNIQUE_ID)) {
			env->_cycleState->_referenceObjectOptions |= MM_CycleState::references_clear_phantom;
			env->_currentTask->releaseSynchronizedGCThreads(env);
		}
		/* phantom reference processing may resurrect objects - scan them now */
		_markingScheme->completeScan(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_PhantomReferenceObjectsComplete);
		return complete_phase_OK;
	}

#if defined(J9VM_GC_FINALIZATION)
	virtual void scanUnfinalizedObjects(MM_EnvironmentBase *env) {
		/* allow the marking scheme to handle this, since it knows which regions are interesting */
		reportScanningStarted(RootScannerEntity_UnfinalizedObjects);
		_markingScheme->scanUnfinalizedObjects(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_UnfinalizedObjects);
	}

	virtual CompletePhaseCode scanUnfinalizedObjectsComplete(MM_EnvironmentBase *env) {
		reportScanningStarted(RootScannerEntity_UnfinalizedObjectsComplete);
		/* ensure that all unfinalized processing is complete before we start marking additional objects */
		env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
		_markingScheme->completeScan(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_UnfinalizedObjectsComplete);
		return complete_phase_OK;
	}
#endif /* J9VM_GC_FINALIZATION */

	virtual void scanOwnableSynchronizerObjects(MM_EnvironmentBase *env) {
		/* allow the marking scheme to handle this, since it knows which regions are interesting */
		reportScanningStarted(RootScannerEntity_OwnableSynchronizerObjects);
		_markingScheme->scanOwnableSynchronizerObjects(MM_EnvironmentVLHGC::getEnvironment(env));
		reportScanningEnded(RootScannerEntity_OwnableSynchronizerObjects);
	}

	virtual void doMonitorReference(J9ObjectMonitor *objectMonitor, GC_HashTableIterator *monitorReferenceIterator) {
		J9ThreadAbstractMonitor * monitor = (J9ThreadAbstractMonitor*)objectMonitor->monitor;
		MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._monitorReferenceCandidates += 1;
		if(!_markingScheme->isMarked((J9Object *)monitor->userData)) {
			monitorReferenceIterator->removeSlot();
			MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._monitorReferenceCleared += 1;
			/* We must call objectMonitorDestroy (as opposed to omrthread_monitor_destroy) when the
			 * monitor is not internal to the GC */
			static_cast<J9JavaVM*>(_omrVM->_language_vm)->internalVMFunctions->objectMonitorDestroy(static_cast<J9JavaVM*>(_omrVM->_language_vm), (J9VMThread *)_env->getLanguageVMThread(), (omrthread_monitor_t)monitor);
		}
	}

	virtual CompletePhaseCode scanMonitorReferencesComplete(MM_EnvironmentBase *envBase) {
		MM_EnvironmentVLHGC* env = MM_EnvironmentVLHGC::getEnvironment(envBase);
		reportScanningStarted(RootScannerEntity_MonitorReferenceObjectsComplete);
		((J9JavaVM *)env->getLanguageVM())->internalVMFunctions->objectMonitorDestroyComplete((J9JavaVM *)env->getLanguageVM(), (J9VMThread *)env->getLanguageVMThread());
		reportScanningEnded(RootScannerEntity_MonitorReferenceObjectsComplete);
		return complete_phase_OK;
	}

	virtual void doJNIWeakGlobalReference(J9Object **slotPtr) {
		J9Object *objectPtr = *slotPtr;
		if(objectPtr && !_markingScheme->isMarked(objectPtr)) {
			*slotPtr = NULL;
		}
	}

#if defined(J9VM_GC_MODRON_SCAVENGER)
	virtual void doRememberedSetSlot(J9Object **slotPtr, GC_RememberedSetSlotIterator *rememberedSetSlotIterator) {
		J9Object *objectPtr = *slotPtr;
		if( (objectPtr == NULL) || !_markingScheme->isMarked(objectPtr)) {
			rememberedSetSlotIterator->removeSlot();
		}
	}
#endif /* defined(J9VM_GC_MODRON_SCAVENGER) */

	virtual void doStringTableSlot(J9Object **slotPtr, GC_StringTableIterator *stringTableIterator) {
		MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._stringConstantsCandidates += 1;
		if(!_markingScheme->isMarked(*slotPtr)) {
			MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._stringConstantsCleared += 1;
			stringTableIterator->removeSlot();
		}
	}

#if defined(J9VM_GC_ENABLE_DOUBLE_MAP)
	virtual void doDoubleMappedObjectSlot(J9Object *objectPtr, struct J9PortVmemIdentifier *identifier) {
		MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._doubleMappedArrayletsCandidates += 1;
		if (!_markingScheme->isMarked(objectPtr)) {
			MM_EnvironmentVLHGC::getEnvironment(_env)->_markVLHGCStats._doubleMappedArrayletsCleared += 1;
			OMRPORT_ACCESS_FROM_OMRVM(_omrVM);
			omrvmem_release_double_mapped_region(identifier->address, identifier->size, identifier);
		}
    }
#endif /* J9VM_GC_ENABLE_DOUBLE_MAP */

	/**
	 * @Clear the string table cache slot if the object is not marked
	 */
	virtual void doStringCacheTableSlot(J9Object **slotPtr) {
		J9Object *objectPtr = *slotPtr;
		if((NULL != objectPtr) && (!_markingScheme->isMarked(*slotPtr))) {
			*slotPtr = NULL;
		}	
	}
	
#if defined(J9VM_GC_FINALIZATION)
	virtual void doFinalizableObject(j9object_t object) {
		Assert_MM_unreachable();
	}
#endif /* J9VM_GC_FINALIZATION */

#if defined(J9VM_OPT_JVMTI)
	virtual void doJVMTIObjectTagSlot(J9Object **slotPtr, GC_JVMTIObjectTagTableIterator *objectTagTableIterator) {
		if(!_markingScheme->isMarked(*slotPtr)) {
			objectTagTableIterator->removeSlot();
		}
	}
#endif /* defined(J9VM_OPT_JVMTI) */

public:
	MM_GlobalMarkingSchemeRootClearer(MM_EnvironmentBase *env, MM_GlobalMarkingScheme *markingScheme) :
		MM_RootScanner(env),
		_markingScheme(markingScheme)
	{
		_typeId = __FUNCTION__;
	};
};

void
MM_GlobalMarkingScheme::cleanCardTableForGlobalCollect(MM_EnvironmentVLHGC *env, MM_CardCleaner *cardCleaner)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 cleanStartTime = j9time_hires_clock();

	GC_HeapRegionIterator regionIterator(_heapRegionManager);
	MM_HeapRegionDescriptor *region = NULL;
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
				_extensions->cardTable->cleanCardsInRegion(env, cardCleaner, region);
			}
		}
	}

	U_64 cleanEndTime = j9time_hires_clock();
	env->_cardCleaningStats.addToCardCleaningTime(cleanStartTime, cleanEndTime);
	env->_markVLHGCStats.addToScanTime(cleanStartTime, cleanEndTime);
}

void
MM_GlobalMarkingScheme::initializeMarkMap(MM_EnvironmentVLHGC *env)
{
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_extensions->getHeap()->getHeapRegionManager());
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->isCommitted()) {
			if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
				if (region->_nextMarkMapCleared) {
					region->_nextMarkMapCleared = false;
					if (_extensions->tarokEnableExpensiveAssertions) {
						Assert_MM_true(_markMap->checkBitsForRegion(env, region));
					}
				} else {
					_markMap->setBitsForRegion(env, region, true);
				}
			}
		}
	}
}

/**
 *  Initialization for Mark
 *  Actual startup for Mark procedure
 *  @param[in] env - passed Environment 
 */
void
MM_GlobalMarkingScheme::markLiveObjectsInit(MM_EnvironmentVLHGC *env)
{
	workerSetupForGC(env);

	if (MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType) {
		/* clear all card states (since we will run as a global collection but then be stored as the data used by the next PGC) */
		MM_GlobalCollectionNoScanCardCleaner cardCleaner;
		cleanCardTableForGlobalCollect(env, &cardCleaner);
	}

	initializeMarkMap(env);

	env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
}

/**
 *  Mark Roots
 *  Create Root Scanner and Mark all roots including classes and classloaders if dynamic class unloading is enabled
 *  @param[in] env - passed Environment 
 *  @param[in] shouldScan instruct should scanning also be active while roots are marked   
 */
void
MM_GlobalMarkingScheme::markLiveObjectsRoots(MM_EnvironmentVLHGC *env)
{
	/* cleaning cards is now considered a root marking operation */
	switch(env->_cycleState->_collectionType) {
	case MM_CycleState::CT_GLOBAL_MARK_PHASE:
	{
		if (0 == env->_cycleState->_currentIncrement) {
			/* First GMP cycle. Just transition cards without scanning. */
			MM_GlobalMarkNoScanCardCleaner cardCleaner;
			cleanCardTableForGlobalCollect(env, &cardCleaner);
		} else {
			/* Final (or intermediate) GMP cycle. Scan dirty cards. */
			MM_GlobalMarkCardCleaner cardCleaner(this, _markMap);
			cleanCardTableForGlobalCollect(env, &cardCleaner);
		}
		break;
	}
	case MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION:
	{
		MM_GlobalCollectionCardCleaner cardCleaner(this);
		cleanCardTableForGlobalCollect(env, &cardCleaner);
		break;
	}
	default:
		Assert_MM_unreachable();
	}
	
	MM_GlobalMarkingSchemeRootMarker rootScanner(env, this);
	rootScanner.setStringTableAsRoot(!isCollectStringConstantsEnabled());
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	rootScanner.setClassDataAsRoots(!isDynamicClassUnloadingEnabled());
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
		
	/* Mark root set classes */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if(isDynamicClassUnloadingEnabled()) {
		/* TODO: This code belongs somewhere else? */
		/* Setting the permanent class loaders to scanned without a locked operation is safe
		 * Class loaders will not be rescanned until a thread synchronize is executed
		 */
		if(env->isMainThread()) {
			scanClassLoaderSlots(env, _javaVM->systemClassLoader);
			scanClassLoaderSlots(env, _javaVM->applicationClassLoader);
		}
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

	rootScanner.scanRoots(env);
}

void
MM_GlobalMarkingScheme::markLiveObjectsScan(MM_EnvironmentVLHGC *env)
{
	completeScan(env);
}

/**
 *  Final Mark services including scanning of Clearables
 *  @param[in] env - passed Environment 
 */
void
MM_GlobalMarkingScheme::markLiveObjectsComplete(MM_EnvironmentVLHGC *env)
{
	/* ensure that all buffers have been flushed before we start reference processing */
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
	if (env->_currentTask->synchronizeGCThreadsAndReleaseSingleThread(env, UNIQUE_ID)) {
		/* since we need a sync point here anyway, use this opportunity to determine which regions contain weak and soft references or unfinalized objects */
		/* (we can't do phantom references yet because unfinalized processing may find more of them) */
		/* weak and soft references resurrected by finalization need to be cleared immediately since weak and soft processing has already completed */
		env->_cycleState->_referenceObjectOptions |= MM_CycleState::references_clear_soft;
		env->_cycleState->_referenceObjectOptions |= MM_CycleState::references_clear_weak;
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
		while(NULL != (region = regionIterator.nextRegion())) {
			if (region->containsObjects()) {
				region->getReferenceObjectList()->startSoftReferenceProcessing();
				region->getReferenceObjectList()->startWeakReferenceProcessing();
				region->getUnfinalizedObjectList()->startUnfinalizedProcessing();
				region->getOwnableSynchronizerObjectList()->startOwnableSynchronizerProcessing();
			}
		}
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}
	
	MM_GlobalMarkingSchemeRootClearer rootClearer(env, this);
	rootClearer.setStringTableAsRoot(!isCollectStringConstantsEnabled());
	rootClearer.scanClearable(env);

	/* If this weren't a GMP, we'd need to deleteDeadObjectsFromExternalCycle here, but since it is there can't be an external cycle state */
	Assert_MM_true(NULL == env->_cycleState->_externalCycleState);
}

void
MM_GlobalMarkingScheme::resolveOverflow(MM_EnvironmentVLHGC *env)
{
	do {
		/* do nothing - just make sure we keep handling overflow until it is cleared */
		env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
	} while (handleOverflow(env));
}

bool
MM_GlobalMarkingScheme::handleOverflow(MM_EnvironmentVLHGC *env)
{
	MM_WorkPackets *packets = (MM_WorkPackets *)(env->_cycleState->_workPackets);
	bool result = false;
	
	if (packets->getOverflowFlag()) {
		result = true;
		if (env->_currentTask->synchronizeGCThreadsAndReleaseMain(env, UNIQUE_ID)) {
			packets->clearOverflowFlag();
			env->_currentTask->releaseSynchronizedGCThreads(env);
		}
		/* our overflow handling mechanism is to set flags in the region descriptor so clean those regions */
		U_8 flagToRemove = MM_RegionBasedOverflowVLHGC::overflowFlagForCollectionType(env, env->_cycleState->_collectionType);
		GC_HeapRegionIteratorVLHGC regionIterator = GC_HeapRegionIteratorVLHGC(_heapRegionManager);
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		while (NULL != (region = regionIterator.nextRegion())) {
			if (J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
				/*
				 * CMVC 176903:
				 * Check for containsObjects must be inside handle_next_work_unit
				 * This code might be executed concurrently, so this flag might be changed OFF to ON at any time
				 * and different threads might find different number of work units and be out of sync
				 * Also this code assumes that regions with this flag OFF can not be marked overflowed
				 * and this flag can not be set OFF for regions marked overflowed
				 */
				if (region->containsObjects()) {
					cleanRegion(env, region, flagToRemove);	
				}
			}
		}
		env->_currentTask->synchronizeGCThreads(env, UNIQUE_ID);
	}
	return result;
}

void
MM_GlobalMarkingScheme::cleanRegion(MM_EnvironmentVLHGC *env, MM_HeapRegionDescriptorVLHGC *region, U_8 flagToClean)
{
	Assert_MM_true(region->containsObjects());
	/* do we need to clean this region? */
	U_8 flags = region->_markData._overflowFlags;
	if (flagToClean == (flags & flagToClean)) {
		/* Region must be cleaned */
		/* save back the new flags, first, in case we re-overflow in another thread (or this thread) */
		U_8 newFlags = flags & ~flagToClean;
		region->_markData._overflowFlags = newFlags;
		/* Force our write of the overflow flags from our cache and ensure that we have no stale mark map data before we walk */
		MM_AtomicOperations::sync();
		UDATA *heapBase = (UDATA *)region->getLowAddress();
		UDATA *heapTop = (UDATA *)region->getHighAddress();
		MM_HeapMapIterator objectIterator = MM_HeapMapIterator(MM_GCExtensions::getExtensions(env), env->_cycleState->_markMap, heapBase, heapTop);

		PORT_ACCESS_FROM_ENVIRONMENT(env);
		U_64 scanStartTime = j9time_hires_clock();
		J9Object *object = NULL;
		while (NULL != (object = objectIterator.nextObject())) {
			scanObject(env, object, SCAN_REASON_OVERFLOWED_REGION);
		}
		U_64 scanEndTime = j9time_hires_clock();
		env->_markVLHGCStats.addToScanTime(scanStartTime, scanEndTime);
	}
}

void
MM_GlobalMarkingScheme::scanObjectsInRange(MM_EnvironmentVLHGC *env, void *lowAddress, void *highAddress)
{
	/* NOTE: we don't update the _markVLHGCStats.scanTime here. The caller is responsible for that */

	/* we only support scanning exactly one card at a time */
	Assert_MM_true(0 == ((UDATA)lowAddress & (J9MODRON_HEAP_BYTES_PER_UDATA_OF_HEAP_MAP - 1)));
	Assert_MM_true(((UDATA)lowAddress + CARD_SIZE) == (UDATA)highAddress);
	
	/* never add any references from the collection set into the RSCL since they will be added during compact fixup */
	for (UDATA bias = 0; bias < CARD_SIZE; bias += J9MODRON_HEAP_BYTES_PER_UDATA_OF_HEAP_MAP) {
		void *scanAddress = (void *)((UDATA)lowAddress + bias);
		MM_HeapMapWordIterator markedObjectIterator(_markMap, scanAddress);
		J9Object *fromObject = NULL;
		while (NULL != (fromObject = markedObjectIterator.nextObject())) {
			/* this object needs to be re-scanned (to update next mark map and RSM) */
			scanObject(env, fromObject, SCAN_REASON_DIRTY_CARD);
		}
	}
}

void
MM_GlobalMarkingScheme::scanWeakReferenceObjects(MM_EnvironmentVLHGC *env)
{
	Assert_MM_true(env->getGCEnvironment()->_referenceObjectBuffer->isEmpty());

	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (!region->getReferenceObjectList()->wasWeakListEmpty()) {
				if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
					processReferenceList(env, region->getReferenceObjectList()->getPriorWeakList(), &env->_markVLHGCStats._weakReferenceStats);
				}
			}
		}
	}

	/* processReferenceList() may have pushed remembered references back onto the buffer if a GMP is active */
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
}

void
MM_GlobalMarkingScheme::scanSoftReferenceObjects(MM_EnvironmentVLHGC *env)
{
	Assert_MM_true(env->getGCEnvironment()->_referenceObjectBuffer->isEmpty());

	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (!region->getReferenceObjectList()->wasSoftListEmpty()) {
				if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
					processReferenceList(env, region->getReferenceObjectList()->getPriorSoftList(), &env->_markVLHGCStats._softReferenceStats);
				}
			}
		}
	}

	/* processReferenceList() may have pushed remembered references back onto the buffer if a GMP is active */
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
}

void
MM_GlobalMarkingScheme::scanPhantomReferenceObjects(MM_EnvironmentVLHGC *env)
{
	/* unfinalized processing may discover more phantom reference objects */
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
	if (env->_currentTask->synchronizeGCThreadsAndReleaseSingleThread(env, UNIQUE_ID)) {
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
		while(NULL != (region = regionIterator.nextRegion())) {
			if (region->containsObjects()) {
				region->getReferenceObjectList()->startPhantomReferenceProcessing();
			}
		}
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}
	
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	GC_HeapRegionIteratorVLHGC regionIterator(_heapRegionManager);
	while(NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (!region->getReferenceObjectList()->wasPhantomListEmpty()) {
				if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
					processReferenceList(env, region->getReferenceObjectList()->getPriorPhantomList(), &env->_markVLHGCStats._phantomReferenceStats);
				}
			}
		}
	}

	/* processReferenceList() may have pushed remembered references back onto the buffer if a GMP is active */
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
}

void
MM_GlobalMarkingScheme::processReferenceList(MM_EnvironmentVLHGC *env, J9Object* headOfList, MM_ReferenceStats *referenceStats)
{
	/* no list can possibly contain more reference objects than there are bytes in a region. */
	const UDATA maxObjects = _heapRegionManager->getRegionSize();
	UDATA objectsVisited = 0;
	GC_FinalizableReferenceBuffer buffer(_extensions);
	
	J9Object* referenceObj = headOfList;
	while (NULL != referenceObj) {
		objectsVisited += 1;
		referenceStats->_candidates += 1;

		Assert_MM_true(isMarked(referenceObj));
		Assert_MM_true(objectsVisited < maxObjects);
		Assert_MM_true(GC_ObjectModel::REF_STATE_REMEMBERED != J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj)); /* we should only see REMEMBERED references in partial collections */
	
		J9Object* nextReferenceObj = _extensions->accessBarrier->getReferenceLink(referenceObj);

		GC_SlotObject referentSlotObject(_extensions->getOmrVM(), J9GC_J9VMJAVALANGREFERENCE_REFERENT_ADDRESS(env, referenceObj));
		J9Object *referent = referentSlotObject.readReferenceFromSlot();
		if (NULL != referent) {
			UDATA referenceObjectType = J9CLASS_FLAGS(J9GC_J9OBJECT_CLAZZ(referenceObj, env)) & J9AccClassReferenceMask;
			if (isMarked(referent)) {
				if (J9AccClassReferenceSoft == referenceObjectType) {
					U_32 age = J9GC_J9VMJAVALANGSOFTREFERENCE_AGE(env, referenceObj);
					if (age < _extensions->getMaxSoftReferenceAge()) {
						/* Soft reference hasn't aged sufficiently yet - increment the age */
						J9GC_J9VMJAVALANGSOFTREFERENCE_AGE(env, referenceObj) = age + 1;
					}
				}
				_interRegionRememberedSet->rememberReferenceForMark(env, referenceObj, referent);
			} else {
				/* transition the state to cleared */
				Assert_MM_true((GC_ObjectModel::REF_STATE_INITIAL == J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj)) || (GC_ObjectModel::REF_STATE_REMEMBERED == J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj)));

				referenceStats->_cleared += 1;
				J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj) = GC_ObjectModel::REF_STATE_CLEARED;

				/* Phantom references keep it's referent alive in Java 8 and doesn't in Java 9 and later */
				if ((J9AccClassReferencePhantom == referenceObjectType) && ((J2SE_VERSION(_javaVM) & J2SE_VERSION_MASK) <= J2SE_18)) {
					/* Scanning will be done after the enqueuing */
					markObject(env, referent);
					_interRegionRememberedSet->rememberReferenceForMark(env, referenceObj, referent);
				} else {
					referentSlotObject.writeReferenceToSlot(NULL);
				}

				/* Check if the reference has a queue */
				if (0 != J9GC_J9VMJAVALANGREFERENCE_QUEUE(env, referenceObj)) {
					/* Reference object can be enqueued onto the finalizable list */
					referenceStats->_enqueued += 1;
					buffer.add(env, referenceObj);
					env->_cycleState->_finalizationRequired = true;
				}
			}
		}
		
		referenceObj = nextReferenceObj;
	}
	buffer.flush(env);
}

void 
MM_GlobalMarkingScheme::flushBuffers(MM_EnvironmentVLHGC *env)
{
	env->_workStack.flush(env);
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
}


bool
MM_ConcurrentGlobalMarkTask::shouldYieldFromTask(MM_EnvironmentBase *envBase)
{
	bool shouldReturnEarly = *_forceExit;

	if (!shouldReturnEarly) {
		MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
		UDATA bytesScannedSinceGMPStart = env->_markVLHGCStats._bytesScanned;
		Assert_MM_true(bytesScannedSinceGMPStart >= env->_previousConcurrentYieldCheckBytesScanned);
		UDATA bytesScannedSinceLastCheck = bytesScannedSinceGMPStart - env->_previousConcurrentYieldCheckBytesScanned;
		/* add the number of bytes this thread has scanned since the last concurrent yield check */
		if (bytesScannedSinceLastCheck > 0) {
			env->_previousConcurrentYieldCheckBytesScanned = bytesScannedSinceGMPStart;
			MM_AtomicOperations::add(&_bytesScanned, bytesScannedSinceLastCheck);
		}
		shouldReturnEarly = _bytesScanned >= _bytesToScan;
	}

	if (shouldReturnEarly) {
		_didReturnEarly = shouldReturnEarly;
	}
	return shouldReturnEarly;
}

void
MM_ConcurrentGlobalMarkTask::setup(MM_EnvironmentBase *envBase)
{
	MM_ParallelGlobalMarkTask::setup(envBase);

	/* initialize _previousConcurrentYieldCheckBytesScanned since we need it to check the change in the scanned stats between shouldYieldFromTask calls */
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	env->_previousConcurrentYieldCheckBytesScanned = env->_markVLHGCStats._bytesScanned;
}

void
MM_ConcurrentGlobalMarkTask::cleanup(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	Assert_MM_true(env->_markVLHGCStats._bytesScanned >= env->_previousConcurrentYieldCheckBytesScanned);
	UDATA finalScannedBytes = env->_markVLHGCStats._bytesScanned - env->_previousConcurrentYieldCheckBytesScanned;
	_bytesScanned += finalScannedBytes;

	MM_ParallelGlobalMarkTask::cleanup(env);
}


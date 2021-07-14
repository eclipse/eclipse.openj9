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

#include "j9.h"
#include "j9cfg.h"
#include "j9protos.h"
#include "j9consts.h"
#include "modronapicore.hpp"
#include "modronopt.h"
#include "ModronAssertions.h"

#include <string.h>

#include "mmhook_internal.h"

#include "AllocateDescription.hpp"
#include "AllocationContextTarok.hpp"
#include "AllocationFailureStats.hpp"
#include "CardListFlushTask.hpp"
#include "CardTable.hpp"
#include "ClassLoaderIterator.hpp"
#include "ClassLoaderManager.hpp"
#include "ClassLoaderRememberedSet.hpp"
#include "CollectionStatisticsVLHGC.hpp"
#include "CompactGroupManager.hpp"
#include "CompactGroupPersistentStats.hpp"
#include "ConcurrentGMPStats.hpp"
#include "CycleState.hpp"
#include "Debug.hpp"
#include "EnvironmentVLHGC.hpp"
#include "EnvironmentBase.hpp"
#include "FinalizeListManager.hpp"
#include "FinalizerSupport.hpp"
#include "GlobalAllocationManager.hpp"
#include "HeapRegionIteratorVLHGC.hpp"
#include "HeapStats.hpp"
#include "IncrementalGenerationalGC.hpp"
#include "InterRegionRememberedSet.hpp"
#include "MarkMap.hpp"
#include "MarkMapManager.hpp"
#include "MarkMapSegmentChunkIterator.hpp"
#include "MemorySpace.hpp"
#include "MemorySubSpace.hpp"
#include "MemorySubSpaceTarok.hpp"
#include "OMRVMInterface.hpp"
#include "ParallelDispatcher.hpp"
#include "ParallelTask.hpp"
#include "ReferenceChainWalker.hpp"
#include "VLHGCAccessBarrier.hpp"
#include "WorkPacketsIterator.hpp"
#include "WorkPacketsVLHGC.hpp"
#include "WorkStack.hpp"

/**
 * Initialization
 */
MM_IncrementalGenerationalGC::MM_IncrementalGenerationalGC(MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager)
	: MM_GlobalCollector()
	, _javaVM((J9JavaVM*)env->getOmrVM()->_language_vm)
	, _extensions(MM_GCExtensions::getExtensions(env))
	, _portLibrary(((J9JavaVM *)(env->getLanguageVM()))->portLibrary)
	, _regionManager(manager)		
	, _configuredSubspace(NULL)
	, _markMapManager(NULL)
	, _interRegionRememberedSet(NULL)
	, _classLoaderRememberedSet(NULL)
	, _copyForwardDelegate(env)
	, _globalMarkDelegate(env)
	, _reclaimDelegate(env, manager, &_collectionSetDelegate)
	, _schedulingDelegate(env, manager)
	, _collectionSetDelegate(env, manager)
	, _projectedSurvivalCollectionSetDelegate(env, manager)
	, _globalCollectionStatistics()
	, _partialCollectionStatistics()
	, _concurrentPhaseStats(OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_MARK_PHASE)
	, _workPacketsForPartialGC(NULL)
	, _workPacketsForGlobalGC(NULL)
	, _taxationThreshold(0)
	, _allocatedSinceLastPGC(0)
	, _mainGCThread(env)
	, _persistentGlobalMarkPhaseState()
	, _forceConcurrentTermination(false)
	, _globalMarkPhaseIncrementBytesStillToScan(0)
{
	_typeId = __FUNCTION__;
}

MM_IncrementalGenerationalGC *
MM_IncrementalGenerationalGC::newInstance(MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager)
{
	MM_IncrementalGenerationalGC *globalGC;
		
	globalGC = (MM_IncrementalGenerationalGC *)env->getForge()->allocate(sizeof(MM_IncrementalGenerationalGC), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (globalGC) {
		new(globalGC) MM_IncrementalGenerationalGC(env, manager);
		if (!globalGC->initialize(env)) { 
        	globalGC->kill(env);
        	globalGC = NULL;
		}
	}
	
	return globalGC;
}

void
MM_IncrementalGenerationalGC::kill(MM_EnvironmentBase *env)
{
	MM_EnvironmentVLHGC *envVLHGC = MM_EnvironmentVLHGC::getEnvironment(env);
	
	tearDown(envVLHGC);
	envVLHGC->getForge()->free(this);
}

/**
 * Initialize the collector's internal structures and values.
 * @return true if initialization completed, false otherwise
 */
bool
MM_IncrementalGenerationalGC::initialize(MM_EnvironmentVLHGC *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	J9HookInterface** mmPrivateHooks = J9_HOOK_INTERFACE(extensions->privateHookInterface);
	
	if (NULL == (extensions->accessBarrier = MM_VLHGCAccessBarrier::newInstance(env))) {
		goto error_no_memory;
	}
 	
 	if (NULL == (_markMapManager = MM_MarkMapManager::newInstance(env))) {
 		goto error_no_memory;
 	}

 	if (NULL == (_interRegionRememberedSet = MM_InterRegionRememberedSet::newInstance(env, extensions->heapRegionManager))) {
 		goto error_no_memory;
 	}
	extensions->interRegionRememberedSet = _interRegionRememberedSet;

 	if (NULL == (_classLoaderRememberedSet = MM_ClassLoaderRememberedSet::newInstance(env))) {
 		goto error_no_memory;
 	}
	extensions->classLoaderRememberedSet = _classLoaderRememberedSet;

	if(!_copyForwardDelegate.initialize(env)) {
		goto error_no_memory;
	}

 	if(!_globalMarkDelegate.initialize(env)) {
 		goto error_no_memory;
 	}

 	
 	if(!_reclaimDelegate.initialize(env)) {
 		goto error_no_memory;
 	}

	if (!_schedulingDelegate.initialize(env)) {
		goto error_no_memory;
	}

 	if(!_collectionSetDelegate.initialize(env)) {
 		goto error_no_memory;
 	}

 	if(!_projectedSurvivalCollectionSetDelegate.initialize(env)) {
 		goto error_no_memory;
 	}

	if (NULL == (_workPacketsForPartialGC = MM_WorkPacketsVLHGC::newInstance(env, MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION))) {
		goto error_no_memory;
	}

	if (NULL == (_workPacketsForGlobalGC = MM_WorkPacketsVLHGC::newInstance(env, MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION))) {
		goto error_no_memory;
	}

	if (!_mainGCThread.initialize(this)) {
		goto error_no_memory;
	}

	if (!_delegate.initialize(env, NULL, NULL)) {
		goto error_no_memory;
	}

	/*
	 * Set base unit for allocation-based aging system here as an estimated "ideal" taxation interval
	 * except it has not been hard coded in command line
	 */
	if (0 == extensions->tarokAllocationAgeUnit) {
		extensions->tarokAllocationAgeUnit = extensions->tarokIdealEdenMaximumBytes;
		extensions->tarokAllocationAgeExponentBase = 1.0;
	}

	/* The Tarok policy always compacts since it can't handle dark matter in scan-only regions */
	extensions->compactOnGlobalGC = true;

	if (!extensions->heapExpansionGCTimeThreshold._wasSpecified) {
		extensions->heapExpansionGCTimeThreshold._valueSpecified = 5;
	}

	if (!extensions->heapContractionGCTimeThreshold._wasSpecified) {
		extensions->heapContractionGCTimeThreshold._valueSpecified = 2;
	}

	/**
	 * Set maximum allocation age based on logical maximum age
	 * except it has not been hard coded in command line
	 */
	if (0 == extensions->tarokMaximumAgeInBytes) {
		extensions->tarokMaximumAgeInBytes = MM_CompactGroupManager::calculateMaximumAllocationAge(env, extensions->tarokRegionMaxAge);
	} else {
		/*
		 * Maximum allocation age is specified in command line
		 * For Allocation-based aging system take is as a primary value and set logical maximum age
		 */
		if (extensions->tarokAllocationAgeEnabled) {
			UDATA maxLogicalAge = MM_CompactGroupManager::calculateLogicalAgeForRegion(env, extensions->tarokMaximumAgeInBytes);
			/*
			 * There are a few data structures have been allocated already with size based on tarokRegionMaxAge(in TGC for example)
			 * so new value can not be larger then old one
			 */
			Assert_MM_true(maxLogicalAge <= extensions->tarokRegionMaxAge);
			extensions->tarokRegionMaxAge = maxLogicalAge;
		}
	}

	extensions->compactGroupPersistentStats = MM_CompactGroupPersistentStats::allocateCompactGroupPersistentStats(env);
	if (NULL == extensions->compactGroupPersistentStats) {
		goto error_no_memory;
	}

	/*
	 * Set threshold for Nursery collection set for allocation-based aging system here as a doubled estimated "ideal" taxation interval
	 * except it has not been hard coded in command line
	 */
	if (0 == extensions->tarokMaximumNurseryAgeInBytes) {
		extensions->tarokMaximumNurseryAgeInBytes = extensions->tarokIdealEdenMaximumBytes * 2;
	}

	/*
	 * For Allocation-based aging system take tarokMaximumNurseryAgeInBytes as a primary value and set tarokNurseryMaxAge
	 */
	if (extensions->tarokAllocationAgeEnabled) {
		extensions->tarokNurseryMaxAge._valueSpecified = MM_CompactGroupManager::calculateLogicalAgeForRegion(env, extensions->tarokMaximumNurseryAgeInBytes);
	}

	/* Attach to hooks required by the global collector's
	 * heap resize (expand/contraction) functions
	 */
	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_CYCLE_START, globalGCHookAFCycleStart, OMR_GET_CALLSITE(), NULL);
	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_CYCLE_END, globalGCHookAFCycleEnd, OMR_GET_CALLSITE(), NULL);

	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_START, globalGCHookSysStart, OMR_GET_CALLSITE(), NULL);
	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_END, globalGCHookSysEnd, OMR_GET_CALLSITE(), NULL);

	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START, globalGCHookIncrementStart, OMR_GET_CALLSITE(), NULL);
	(*mmPrivateHooks)->J9HookRegisterWithCallSite(mmPrivateHooks, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_END, globalGCHookIncrementEnd, OMR_GET_CALLSITE(), NULL);
	
	return true;
	
error_no_memory:
	return false;
}

/**
 * Free any internal structures associated to the receiver.
 */
void
MM_IncrementalGenerationalGC::tearDown(MM_EnvironmentVLHGC *env)
{
	_delegate.tearDown(env);

	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	if (extensions->accessBarrier) {
		extensions->accessBarrier->kill(env);
		extensions->accessBarrier = NULL;
	}

	_copyForwardDelegate.tearDown(env);

	_globalMarkDelegate.tearDown(env);
	_reclaimDelegate.tearDown(env);

	_collectionSetDelegate.tearDown(env);
	_projectedSurvivalCollectionSetDelegate.tearDown(env);

	_mainGCThread.tearDown(env);
	
	if(NULL != _markMapManager) {
		_markMapManager->kill(env);
		_markMapManager = NULL;
	}

	if(NULL != _interRegionRememberedSet) {
		_interRegionRememberedSet->kill(env);
		_interRegionRememberedSet = NULL;
		extensions->interRegionRememberedSet = NULL;
	}

	if(NULL != _classLoaderRememberedSet) {
		_classLoaderRememberedSet->kill(env);
		_classLoaderRememberedSet = NULL;
	}
	
	if (NULL != extensions->compactGroupPersistentStats) {
		MM_CompactGroupPersistentStats::killCompactGroupPersistentStats(env, extensions->compactGroupPersistentStats);
		extensions->compactGroupPersistentStats = NULL;
	}

	if(NULL != _workPacketsForPartialGC) {
		_workPacketsForPartialGC->kill(env);
		_workPacketsForPartialGC = NULL;
	}

	if(NULL != _workPacketsForGlobalGC) {
		_workPacketsForGlobalGC->kill(env);
		_workPacketsForGlobalGC = NULL;
	}
}		
 
/****************************************
 * Thread work routines
 ****************************************
 */
void
MM_IncrementalGenerationalGC::setupBeforeGC(MM_EnvironmentBase *env)
{
#if defined(J9VM_GC_FINALIZATION)
	/* this should not be set by the GC since it is used by components in order to record that they performed some operation which will require that we do some finalization */
	env->_cycleState->_finalizationRequired = false;
#endif /* J9VM_GC_FINALIZATION */
	
	_classLoaderRememberedSet->setupBeforeGC(env);
	
}

void
MM_IncrementalGenerationalGC::mainThreadGarbageCollect(MM_EnvironmentBase *envBase, MM_AllocateDescription *allocDescription, bool initMarkMap, bool rebuildMarkBits)
{
	J9VMThread 	*vmThread = (J9VMThread *)envBase->getOmrVMThread()->_language_vmthread;
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);

	/* We might be running in a context of either main or main thread, but either way we must have exclusive access */
	Assert_MM_mustHaveExclusiveVMAccess(env->getOmrVMThread());

	Assert_MM_true(NULL != _extensions->rememberedSetCardBucketPool);
	
	if (_extensions->trackMutatorThreadCategory) {
		/* This thread is doing GC work, account for the time spent into the GC bucket */
		omrthread_set_category(vmThread->osThread, J9THREAD_CATEGORY_SYSTEM_GC_THREAD, J9THREAD_TYPE_SET_GC);
	}

	switch(env->_cycleState->_collectionType) {
	case MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION:
		runPartialGarbageCollect(env, allocDescription);
		break;
	case MM_CycleState::CT_GLOBAL_MARK_PHASE:
		runGlobalMarkPhaseIncrement(env);
		break;
	case MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION:
		runGlobalGarbageCollection(env, allocDescription);
		break;
	default:
		Assert_MM_unreachable();
		break;
	}

	if (_extensions->trackMutatorThreadCategory) {
		/* Done doing GC, reset the category back to the old one */
		omrthread_set_category(vmThread->osThread, 0, J9THREAD_TYPE_SET_GC);
	}
	
	/* The flag to terminate concurrent operation is only valid until the next pause.  Since that pause is now complete,
	 * allow concurrent operations.
	 */
	_forceConcurrentTermination = false;

	/* Release any resources that might be bound to this main thread,
	 * since it may be implicit and change for other phases of the cycle */
	_interRegionRememberedSet->releaseCardBufferControlBlockListForThread(env, env);
}

void
MM_IncrementalGenerationalGC::globalMarkPhase(MM_EnvironmentVLHGC *env, bool incrementalMark)
{
	bool performExpensiveAssertions = _extensions->tarokEnableExpensiveAssertions;
	bool incrementalMarkIsComplete = false;

	if(isGlobalMarkPhaseRunning()) {
		/* We have already started in a previous increment - we therefore must clean cards */
		Assert_MM_true(env->_cycleState->_markMap == _markMapManager->getGlobalMarkPhaseMap());
		Assert_MM_true(env->_cycleState->_workPackets == _workPacketsForGlobalGC);
		Assert_MM_true(env->_cycleState->_dynamicClassUnloadingEnabled);
	} else {
		if (performExpensiveAssertions) {
			assertWorkPacketsEmpty(env, _workPacketsForGlobalGC);
		}
	}
	if (performExpensiveAssertions) {
		assertWorkPacketsEmpty(env, _workPacketsForPartialGC);
	}

	Assert_MM_false(_workPacketsForGlobalGC->getOverflowFlag());
	Assert_MM_false(_workPacketsForPartialGC->getOverflowFlag());
	Assert_MM_true(&_persistentGlobalMarkPhaseState == env->_cycleState);
	
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.clear();
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._startTime = j9time_hires_clock();

	if (incrementalMark) {
		reportGMPMarkStart(env);
		I_64 endTime = j9time_current_time_millis() + _schedulingDelegate.currentGlobalMarkIncrementTimeMillis(env);
		if (env->_cycleState->_markDelegateState == MM_CycleState::state_mark_idle) {
			_globalMarkDelegate.performMarkSetInitialState(env);
		}
		incrementalMarkIsComplete = _globalMarkDelegate.performMarkIncremental(env, endTime);

		Assert_MM_true( (incrementalMarkIsComplete) == (MM_CycleState::state_mark_idle == env->_cycleState->_markDelegateState));
	} else {
		reportGlobalGCMarkStart(env);
		/* a global collect will enter with a different cycle state instance than the _persistentGlobalMarkPhaseState so we need to copy the _markDelegateState
		 * out and back.
		 */
		_globalMarkDelegate.performMarkForGlobalGC(env);
		Assert_MM_true(MM_CycleState::state_mark_idle == env->_cycleState->_markDelegateState);
	}
	/* clearing the mark map and work packets is the caller's responsibility */
	Assert_MM_true(NULL != env->_cycleState->_markMap);
	Assert_MM_true(NULL != env->_cycleState->_workPackets);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._endTime = j9time_hires_clock();
	/* Accumulate the mark increment stats into persistent GMP state*/
	_persistentGlobalMarkPhaseState._vlhgcCycleStats.merge(&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats);
	

	if (incrementalMark) {
		reportGMPMarkEnd(env);
		if (incrementalMarkIsComplete) {
			postMarkMapCompletion(env);
			_globalMarkDelegate.postMarkCleanup(env);
		}
	} else {
		reportGlobalGCMarkEnd(env);
		postMarkMapCompletion(env);
		_globalMarkDelegate.postMarkCleanup(env);
	}

	_schedulingDelegate.globalMarkIncrementCompleted(env);
	
	Assert_MM_false(_workPacketsForGlobalGC->getOverflowFlag());
	Assert_MM_false(_workPacketsForPartialGC->getOverflowFlag());
}

/****************************************
 * VM Garbage Collection API
 ****************************************
 */
void
MM_IncrementalGenerationalGC::internalPreCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription, U_32 gcCode)
{
	/* Determine if this is a global collection - the state will have already been setup for an increment */
	if(NULL == env->_cycleState) {
		/* Global collection.  Use the internal persistent state for collection */
		env->_cycleState = &_persistentGlobalMarkPhaseState;
		env->_cycleState->_gcCode = MM_GCCode(gcCode);
		env->_cycleState->_activeSubSpace = subSpace;
		env->_cycleState->_collectionType = MM_CycleState::CT_GLOBAL_GARBAGE_COLLECTION;
		env->_cycleState->_collectionStatistics = &_globalCollectionStatistics;
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.clear();
		
		/* Regardless if we are transitioning from GMP , the cycle type will be set to Global GC. */
		env->_cycleState->_type = OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_GARBAGE_COLLECT;

		/* If we are in an excessiveGC level beyond normal then an aggressive GC is
		 * conducted to free up as much space as possible
		 */
		if (!env->_cycleState->_gcCode.isExplicitGC()) {
			if(excessive_gc_normal != _extensions->excessiveGCLevel) {
				/* convert the current mode to excessive GC mode */
				env->_cycleState->_gcCode = MM_GCCode(J9MMCONSTANT_IMPLICIT_GC_EXCESSIVE);
			}
		}
	} else {
		/* Expected types of pre-existing cycle states */
		Assert_MM_true((env->_cycleState->_collectionType == MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION) || (env->_cycleState->_collectionType == MM_CycleState::CT_GLOBAL_MARK_PHASE));
	}

	/* Flush any VM level changes to prepare for a safe slot walk. Moved to internalPreCollect()
	 * so that TLH flushed before we clean cards if concurrent mark active
	 */
	GC_OMRVMInterface::flushCachesForGC(env);

	return;
}

void
MM_IncrementalGenerationalGC::internalPostCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace)
{
	MM_GlobalCollector::internalPostCollect(env, subSpace);
}

void 
MM_IncrementalGenerationalGC::setupForGC(MM_EnvironmentBase *env)
{
}	

bool
MM_IncrementalGenerationalGC::internalGarbageCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription)
{
	MM_EnvironmentVLHGC *envVLHGC = MM_EnvironmentVLHGC::getEnvironment(env);
	
	_extensions->globalVLHGCStats.gcCount += 1;

	/* we make the decision to treat soft refs as weak once we know what kind of cycle this is to be.  If it is an OOM, we need to clear all possible soft refs in the GC before we throw this exception */
	env->_cycleState->_referenceObjectOptions = MM_CycleState::references_default;
	if (env->_cycleState->_gcCode.isOutOfMemoryGC()) {
		env->_cycleState->_referenceObjectOptions |= MM_CycleState::references_soft_as_weak;
	}

	bool didAttemptCollect = _mainGCThread.garbageCollect(envVLHGC, static_cast<MM_AllocateDescription*>(allocDescription));

	env->_cycleState->_activeSubSpace = NULL;

	return didAttemptCollect;
}

/* (non-doxygen)
 * @see MM_GlobalCollector::heapAddRange()
 */
bool
MM_IncrementalGenerationalGC::heapAddRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, UDATA size, 
									void *lowAddress, void *highAddress)
{
	MM_EnvironmentVLHGC *envVLHGC = MM_EnvironmentVLHGC::getEnvironment(env);
	
	bool result = _markMapManager->heapAddRange(envVLHGC, subspace, size, lowAddress, highAddress);
	if (result) {
		result = _globalMarkDelegate.heapAddRange(envVLHGC, subspace, size, lowAddress, highAddress);
		if (result) {
			result = _reclaimDelegate.heapAddRange(envVLHGC, subspace, size, lowAddress, highAddress);
			if (result) {
				if(NULL != _extensions->referenceChainWalkerMarkMap) {
					result = _extensions->referenceChainWalkerMarkMap->heapAddRange(envVLHGC, size, lowAddress, highAddress);
					if (!result) {
						/* Reference Chain Walker Mark Map expansion has failed
						 * Mark Map Manager, Global Mark Delegate, Partial Mark Delegate and Reclaim Delegate expansions must be reversed
						 */
						_reclaimDelegate.heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
						_globalMarkDelegate.heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
						_markMapManager->heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
					}
				}
			} else {
				/* Reclaim Delegate expansion has failed
				 * Mark Map Manager, Global Mark Delegate and Partial Mark Delegate expansions must be reversed
				 */
				_globalMarkDelegate.heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
				_markMapManager->heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
			}
		} else {
			/* Global Mark Delegate expansion has failed
			 * Mark Map Manager expansion must be reversed
			 */
			_markMapManager->heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, NULL, NULL);
		}
	}
	return result;
}

/* (non-doxygen)
 * @see MM_GlobalCollector::heapRemoveRange()
 */
bool
MM_IncrementalGenerationalGC::heapRemoveRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace,UDATA size,
										void *lowAddress, void *highAddress, void *lowValidAddress, void *highValidAddress)
{
	MM_EnvironmentVLHGC *envVLHGC = MM_EnvironmentVLHGC::getEnvironment(env);

	bool result = _markMapManager->heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, lowValidAddress, highValidAddress);

	result = result && _globalMarkDelegate.heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, lowValidAddress, highValidAddress);

	result = result && _reclaimDelegate.heapRemoveRange(envVLHGC, subspace, size, lowAddress, highAddress, lowValidAddress, highValidAddress);

	if(NULL != _extensions->referenceChainWalkerMarkMap) {
		result = result && _extensions->referenceChainWalkerMarkMap->heapRemoveRange(envVLHGC, size, lowAddress, highAddress, lowValidAddress, highValidAddress);
	}
	return result;
}

/* (non-doxygen)
 * @see MM_GlobalCollector::heapReconfigured()
 */
void
MM_IncrementalGenerationalGC::heapReconfigured(MM_EnvironmentBase *env, HeapReconfigReason reason, MM_MemorySubSpace *subspace, void *lowAddress, void *highAddress)
{
	MM_EnvironmentVLHGC *envVLHGC = MM_EnvironmentVLHGC::getEnvironment(env);
	
	_reclaimDelegate.heapReconfigured(envVLHGC);
	_schedulingDelegate.heapReconfigured(envVLHGC);
}

void
MM_IncrementalGenerationalGC::preMainGCThreadInitialize(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	_interRegionRememberedSet->setRememberedSetCardBucketPoolForMainThread(env);

	if (!_markMapManager->collectorStartup(_extensions)) {
		Assert_MM_unreachable();
	}
}

void
MM_IncrementalGenerationalGC::initializeTaxationThreshold(MM_EnvironmentVLHGC *env)
{
	/**
	 * This may be called from either real Main GC thread or acting Main GC thread (any thread that caused GC in absence of real Main GC thread).
	 *  taxationThreshold will be initialized only once.
	 */
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	/* we need to calculate the taxation threshold after the initial heap inflation, which happens before collectorStartup */
	_taxationThreshold = _schedulingDelegate.getInitialTaxationThreshold(env);
	/* initialize GMP Kickoff Headroom Region Count */
	_schedulingDelegate.initializeKickoffHeadroom(env);


	UDATA minimumKickOff = extensions->regionSize * 2;
	if (_taxationThreshold < minimumKickOff) {
		/* first taxation can't happen before any of the heap has been allocated so allocate at least 2 regions, first (note that this number is purely a placeholder until a less arbitrary value is determined) */
		_taxationThreshold = minimumKickOff;
	}
	Assert_MM_true(NULL != _configuredSubspace);
	_configuredSubspace->setBytesRemainingBeforeTaxation(_taxationThreshold);
	_allocatedSinceLastPGC = 0;
	
	initialRegionAgesSetup(env, _taxationThreshold);
}

bool
MM_IncrementalGenerationalGC::collectorStartup(MM_GCExtensionsBase *extensions)
{
	/*
	 * WARNING:  GCs can occur before this function is run!  Any initialization which a GC MUST rely on should be in initialize.
	 */
	/*
	 * Note that _mainGCThread.startup() can invoke a GC (allocates a Thread object) so it must be the last part of
	 * collector startup.
	 */
	if (!_mainGCThread.startup()) {
		return false;
	}
	return true;
}

void
MM_IncrementalGenerationalGC::collectorShutdown(MM_GCExtensionsBase *extensions)
{
	_mainGCThread.shutdown();
}

/**
 * Determine if a  expand is required 
 * @return expand size if rator expand required or 0 otherwise
 */
U_32
MM_IncrementalGenerationalGC::getGCTimePercentage(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env->getOmrVM());
	
	/* Calculate what ratio of time we are spending in GC */	
	U_32 ratio= extensions->heap->getResizeStats()->calculateGCPercentage();

	return ratio;
}

void
MM_IncrementalGenerationalGC::globalGCHookSysStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_SystemGCStartEvent* event = (MM_SystemGCStartEvent*)eventData;
	J9VMThread* vmThread = static_cast<J9VMThread*>(event->currentThread->_language_vmthread);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);

	Trc_MM_IncrementalGenerationalGC_globalGCHookSysStart(vmThread, extensions->globalVLHGCStats.gcCount);
	
	extensions->heap->getResizeStats()->resetRatioTicks();
}

void
MM_IncrementalGenerationalGC::globalGCHookSysEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_SystemGCEndEvent* event = (MM_SystemGCEndEvent*)eventData;
	J9VMThread* vmThread = static_cast<J9VMThread*>(event->currentThread->_language_vmthread);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	PORT_ACCESS_FROM_VMC((J9VMThread*)event->currentThread->_language_vmthread);

	Trc_MM_IncrementalGenerationalGC_globalGCHookSysEnd(vmThread, extensions->globalVLHGCStats.gcCount);
	
	/* Save end time so at next AF we can get a realistic time outside GC.
	 * While it will never be used it may be useful for debugging. 
	 */
	extensions->heap->getResizeStats()->setLastAFEndTime(j9time_hires_clock());
}

void
MM_IncrementalGenerationalGC::globalGCHookAFCycleStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_AllocationFailureCycleStartEvent* event = (MM_AllocationFailureCycleStartEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	PORT_ACCESS_FROM_VMC((J9VMThread*)event->currentThread->_language_vmthread);
	
	Trc_MM_IncrementalGenerationalGC_globalGCHookAFStart(event->currentThread->_language_vmthread, extensions->globalVLHGCStats.gcCount);
	
	extensions->heap->getResizeStats()->setThisAFStartTime(j9time_hires_clock());
	extensions->heap->getResizeStats()->setLastTimeOutsideGC();
	extensions->heap->getResizeStats()->setGlobalGCCountAtAF(extensions->globalVLHGCStats.gcCount);
}

void
MM_IncrementalGenerationalGC::globalGCHookAFCycleEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_AllocationFailureCycleEndEvent* event = (MM_AllocationFailureCycleEndEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	PORT_ACCESS_FROM_VMC((J9VMThread*)event->currentThread->_language_vmthread);
	
	Trc_MM_IncrementalGenerationalGC_globalGCHookAFEnd(event->currentThread->_language_vmthread, extensions->globalVLHGCStats.gcCount);
	
	/* ..and remember time of last AF end */
	extensions->heap->getResizeStats()->setLastAFEndTime(j9time_hires_clock());
	extensions->heap->getResizeStats()->updateHeapResizeStats();
}


void
MM_IncrementalGenerationalGC::globalGCHookIncrementStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_TarokIncrementStartEvent* event = (MM_TarokIncrementStartEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	PORT_ACCESS_FROM_VMC((J9VMThread*)event->currentThread->_language_vmthread);
	
	Trc_MM_IncrementalGenerationalGC_globalGCHookIncrementStart(event->currentThread->_language_vmthread, extensions->globalVLHGCStats.gcCount);
	
	/* treat increments like allocation failures for the purposes of resize statistics */
	extensions->heap->getResizeStats()->setThisAFStartTime(j9time_hires_clock());
	extensions->heap->getResizeStats()->setLastTimeOutsideGC();
	extensions->heap->getResizeStats()->setGlobalGCCountAtAF(extensions->globalVLHGCStats.gcCount);
}

void
MM_IncrementalGenerationalGC::globalGCHookIncrementEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	MM_TarokIncrementEndEvent* event = (MM_TarokIncrementEndEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	PORT_ACCESS_FROM_VMC((J9VMThread*)event->currentThread->_language_vmthread);
	
	Trc_MM_IncrementalGenerationalGC_globalGCHookIncrementEnd(event->currentThread->_language_vmthread, extensions->globalVLHGCStats.gcCount);
	
	/* ..and remember time of last AF end */
	extensions->heap->getResizeStats()->setLastAFEndTime(j9time_hires_clock());
	extensions->heap->getResizeStats()->updateHeapResizeStats();
}

/**
 * Request to create sweepPoolState class for pool
 * @param  memoryPool memory pool to attach sweep state to
 * @return pointer to created class
 */
void *
MM_IncrementalGenerationalGC::createSweepPoolState(MM_EnvironmentBase *env, MM_MemoryPool *memoryPool)
{
	return _reclaimDelegate.createSweepPoolState(env, memoryPool);
}

/**
 * Request to destroy sweepPoolState class for pool
 * @param  sweepPoolState class to destroy
 */
void
MM_IncrementalGenerationalGC::deleteSweepPoolState(MM_EnvironmentBase *env, void *sweepPoolState)
{
	_reclaimDelegate.deleteSweepPoolState(env, sweepPoolState);
}

void
MM_IncrementalGenerationalGC::taxationEntryPoint(MM_EnvironmentBase *envModron, MM_MemorySubSpace *subspace, MM_AllocateDescription *allocDescription)
{
	MM_EnvironmentVLHGC *env = (MM_EnvironmentVLHGC *)envModron;
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	Assert_MM_mustHaveExclusiveVMAccess(env->getOmrVMThread());

	bool doPartialGarbageCollection = false;
	bool doGlobalMarkPhase = false;
	_schedulingDelegate.getIncrementWork(env, &doPartialGarbageCollection, &doGlobalMarkPhase);
	Assert_MM_true(doPartialGarbageCollection != doGlobalMarkPhase);
	Assert_MM_true(0 == _configuredSubspace->getBytesRemainingBeforeTaxation());

	/* Accumulate allocated bytes since last PGC */
	_allocatedSinceLastPGC += _taxationThreshold;

	/* Report the start of the increment */
	_extensions->globalVLHGCStats.incrementCount += 1;
	if (J9_EVENT_IS_HOOKED(_extensions->privateHookInterface, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START)) {
		MM_CommonGCStartData commonData;
		_extensions->heap->initializeCommonGCStartData(env, &commonData);

		ALWAYS_TRIGGER_J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START(
			_extensions->privateHookInterface,
			env->getOmrVMThread(),
			j9time_hires_clock(),
			J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START,
			_extensions->globalVLHGCStats.incrementCount,
			&commonData,
			_taxationThreshold
		);
	}

	/* Perform PGC if needed */
	if(doPartialGarbageCollection) {
		Assert_MM_true(NULL == env->_cycleState);
		MM_CycleStateVLHGC cycleState;
		env->_cycleState = &cycleState;
		cycleState._schedulingDelegate = &_schedulingDelegate;
		env->_cycleState->_gcCode = MM_GCCode(J9MMCONSTANT_IMPLICIT_GC_DEFAULT);
		env->_cycleState->_collectionType = MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION;
		env->_cycleState->_type = OMR_GC_CYCLE_TYPE_VLHGC_PARTIAL_GARBAGE_COLLECT;
		env->_cycleState->_activeSubSpace = subspace;
		env->_cycleState->_referenceObjectOptions = MM_CycleState::references_default;
		env->_cycleState->_collectionStatistics = &_partialCollectionStatistics;
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.clear();

		bool didAttemptCollect = _mainGCThread.garbageCollect(env, allocDescription);
		Assert_MM_true(didAttemptCollect);

		env->_cycleState->_activeSubSpace = NULL;
		env->_cycleState = NULL;
	}

	/* Perform GMP if needed */
	if(doGlobalMarkPhase) {
		Assert_MM_true(_extensions->tarokEnableIncrementalGMP);
		Assert_MM_true(!doPartialGarbageCollection);

		Assert_MM_true(NULL == env->_cycleState);
		env->_cycleState = &_persistentGlobalMarkPhaseState;
		env->_cycleState->_gcCode = MM_GCCode(J9MMCONSTANT_IMPLICIT_GC_DEFAULT);
		env->_cycleState->_collectionType = MM_CycleState::CT_GLOBAL_MARK_PHASE;
		env->_cycleState->_type = OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_MARK_PHASE;
		env->_cycleState->_activeSubSpace = subspace;
		env->_cycleState->_referenceObjectOptions = MM_CycleState::references_default;
		env->_cycleState->_collectionStatistics = &_globalCollectionStatistics;

		bool didAttemptCollect = _mainGCThread.garbageCollect(env, allocDescription);
		Assert_MM_true(didAttemptCollect);

		env->_cycleState->_activeSubSpace = NULL;
		Assert_MM_true(&_persistentGlobalMarkPhaseState == env->_cycleState);
		env->_cycleState = NULL;
		
		if (!isGlobalMarkPhaseRunning()) {
			_schedulingDelegate.globalMarkPhaseCompleted(env);
		}
	}

	/* Set the next threshold for collection work */
	_taxationThreshold = _schedulingDelegate.getNextTaxationThreshold(env);
	_configuredSubspace->setBytesRemainingBeforeTaxation(_taxationThreshold);

	/* Report the end of the increment */
	if (J9_EVENT_IS_HOOKED(_extensions->privateHookInterface, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_END)) {
		MM_CommonGCEndData commonData;
		_extensions->heap->initializeCommonGCEndData(env, &commonData);

		ALWAYS_TRIGGER_J9HOOK_MM_PRIVATE_TAROK_INCREMENT_END(
			_extensions->privateHookInterface,
			env->getOmrVMThread(),
			j9time_hires_clock(),
			J9HOOK_MM_PRIVATE_TAROK_INCREMENT_END,
			env->getExclusiveAccessTime(),
			&commonData
		);
	}
}

void
MM_IncrementalGenerationalGC::partialGarbageCollectPreWork(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	Assert_MM_true(NULL != env->_cycleState->_activeSubSpace);

	/*
	 * Collection preparation work including cache flushing and stats reporting.
	 */

	/* Flush non-allocation caches for update purposes (verbose) and safety (member deletion) */
	GC_OMRVMInterface::flushNonAllocationCaches(env);
	/* Flush allocation caches */
	MM_GlobalAllocationManager *gam = _extensions->globalAllocationManager;
	if (NULL != gam) {
		gam->flushAllocationContexts(env);
	}

	preCollect(env, env->_cycleState->_activeSubSpace, NULL, J9MMCONSTANT_IMPLICIT_GC_DEFAULT);

	/* Perform any main-specific setup */
	_extensions->globalVLHGCStats.gcCount += 1;

	/*
	 * Core collection work.
	 */
	bool performExpensiveAssertions = _extensions->tarokEnableExpensiveAssertions;
	if (performExpensiveAssertions) {
		assertWorkPacketsEmpty(env, _workPacketsForPartialGC);
	}

	_schedulingDelegate.determineNextPGCType(env);

	Assert_MM_false(_workPacketsForGlobalGC->getOverflowFlag());
	Assert_MM_false(_workPacketsForPartialGC->getOverflowFlag());
	Assert_MM_true(0 == static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.getTotalStallTime());

	reportGCCycleStart(env);
	reportPGCStart(env);
	reportGCIncrementStart(env, "partial collect", 0);

	setupBeforePartialGC(env, env->_cycleState->_gcCode);
	if (isGlobalMarkPhaseRunning()) {
		/* since we have a GMP running, the PGC will need to know about it to find roots in its mark map */
		env->_cycleState->_externalCycleState = &_persistentGlobalMarkPhaseState;
	}
	MM_CompactGroupPersistentStats *persistentStats = _extensions->compactGroupPersistentStats;
	MM_CompactGroupPersistentStats::updateStatsBeforeCollect(env, persistentStats);
	if (_schedulingDelegate.isGlobalSweepRequired()) {
		Assert_MM_true(NULL == env->_cycleState->_externalCycleState);

		PORT_ACCESS_FROM_ENVIRONMENT(env);

		_reclaimDelegate.runGlobalSweepBeforePGC(env, allocDescription, env->_cycleState->_activeSubSpace, env->_cycleState->_gcCode);

		U_64 sweepTimeStart = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats._startTime;
		U_64 sweepTimeEnd = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._sweepStats._endTime;
		U_64 globalSweepTimeUs = j9time_hires_delta(sweepTimeStart, sweepTimeEnd, J9PORT_TIME_DELTA_IN_MICROSECONDS);

		/* TODO: lpnguyen make another statisticsDelegate or something that both schedulingDelegate and reclaimDelegate can see
		 * so that we can avoid this kind of stats-passing mess.
		 */
		double regionConsumptionRate = _schedulingDelegate.getTotalRegionConsumptionRate();
		double avgSurvivorRegions = _schedulingDelegate.getAverageSurvivorSetRegionCount();
		double avgCopyForwardRate = _schedulingDelegate.getAverageCopyForwardRate();
		U_64 scanTimeCostPerGMP = _schedulingDelegate.getScanTimeCostPerGMP(env);

		double optimalEmptinessRegionThreshold = _reclaimDelegate.calculateOptimalEmptinessRegionThreshold(env, regionConsumptionRate, avgSurvivorRegions, avgCopyForwardRate, scanTimeCostPerGMP);
		_schedulingDelegate.setAutomaticDefragmentEmptinessThreshold(optimalEmptinessRegionThreshold);
		_schedulingDelegate.setGlobalSweepTime(globalSweepTimeUs);
	}

	/* Determine if there are enough regions available to attempt a copy-forward collection.
	 * Note that this check is done after we sweep, since that might have recovered enough
	 * regions to make copy-forward feasible.
	 */
	if (env->_cycleState->_shouldRunCopyForward) {
		MM_GlobalAllocationManagerTarok *allocationmanager = (MM_GlobalAllocationManagerTarok *)_extensions->globalAllocationManager;
		/* We need at least one region per context for surviving Eden objects. Older objects can move into tail-fill regions. */
		/* (We could require more than one region, but experimental evidence shows little value in doing so.) */
		UDATA minimumRegionsForCopyForward = allocationmanager->getManagedAllocationContextCount();
		UDATA freeRegions = allocationmanager->getFreeRegionCount();
		if (freeRegions < minimumRegionsForCopyForward) {
			env->_cycleState->_shouldRunCopyForward = false;
			env->_cycleState->_reasonForMarkCompactPGC = MM_CycleState::reason_insufficient_free_space;
		}
	}

	/* This call does report events, but does not collect statistics. */
	preProcessPGCUsingCopyForward(env, allocDescription);
}

void
MM_IncrementalGenerationalGC::partialGarbageCollectPostWork(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	/* This call does report events, but does not collect statistics. */
	postProcessPGCUsingCopyForward(env, allocDescription);

	env->_cycleState->_workPackets = NULL;
	env->_cycleState->_markMap = NULL;

	if (attemptHeapResize(env, allocDescription)) {
		/* Check was it successful contraction */
		if (env->_cycleState->_activeSubSpace->wasContractedThisGC(_extensions->globalVLHGCStats.gcCount)) {
			_interRegionRememberedSet->setShouldFlushBuffersForDecommitedRegions();
		}
	}

	env->_cycleState->_externalCycleState = NULL;

	incrementRegionAges(env, _taxationThreshold, true);

	reportGCCycleFinalIncrementEnding(env);
	reportGCIncrementEnd(env);
	reportPGCEnd(env);
	reportGCCycleEnd(env);

	/* Reset amount allocated for next PGC */
	_allocatedSinceLastPGC = 0;

	_extensions->allocationStats.clear();

	bool performExpensiveAssertions = _extensions->tarokEnableExpensiveAssertions;
	if (performExpensiveAssertions) {
		assertWorkPacketsEmpty(env, _workPacketsForPartialGC);
		assertTableClean(env, isGlobalMarkPhaseRunning() ? CARD_GMP_MUST_SCAN : CARD_CLEAN);
	}

	/*
	 * Collection end work
	 */

	postCollect(env, env->_cycleState->_activeSubSpace);
}

void
MM_IncrementalGenerationalGC::runPartialGarbageCollect(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	/* If concurrent Copy Forward is enabled we only want to pre process at first PGC increment
	 * and isConcurrentCycleInProgress() tells us if this is the first PGC increment or not */
	if (!_copyForwardDelegate.isConcurrentCycleInProgress()) {
		partialGarbageCollectPreWork(env, allocDescription);
	}

	_copyForwardDelegate.performCopyForwardForPartialGC(env);

	/* If concurrent Copy Forward is enabled we only want to post process at last PGC incrememnt
	 * and isConcurrentCycleInProgress() tells us if this is the last PGC increment or not */
	if (!_copyForwardDelegate.isConcurrentCycleInProgress()) {
		partialGarbageCollectPostWork(env, allocDescription);
	}
}

void
MM_IncrementalGenerationalGC::runGlobalMarkPhaseIncrement(MM_EnvironmentVLHGC *env)
{
	/*
	 * Collection preparation work including cache flushing and stats reporting.
	 */
	Assert_MM_true(NULL != env->_cycleState->_activeSubSpace);

	/* Flush non-allocation caches for update purposes (verbose) and safety (member deletion) */
	GC_OMRVMInterface::flushNonAllocationCaches(env);
	/* Flush allocation caches */
	MM_GlobalAllocationManager *gam = _extensions->globalAllocationManager;
	if (NULL != gam) {
		gam->flushAllocationContexts(env);
	}

	preCollect(env, env->_cycleState->_activeSubSpace, NULL, J9MMCONSTANT_IMPLICIT_GC_DEFAULT);
	setupBeforeGlobalGC(env, env->_cycleState->_gcCode);

	/* If a GMP hasn't already begun, this will be the first increment of a new cycle */
	if(!isGlobalMarkPhaseRunning()) {
		reportGMPCycleStart(env);
		/* Inform scheduling delegate that it's internal metrics need to update/reset */
		_schedulingDelegate.globalMarkCycleStart(env);
		_persistentGlobalMarkPhaseState._vlhgcCycleStats.clear();
	}

	/* TODO: TEMPORARY: This is a temporary call that should be deleted once the new verbose format is in place */
	/* NOTE: May want to move any tracepoints up into this routine */
	reportGMPIncrementStart(env);
	reportGCIncrementStart(env, "GMP increment", env->_cycleState->_currentIncrement);

	/* Perform any main-specific setup */
	_extensions->globalVLHGCStats.gcCount += 1;

	if ((_globalMarkPhaseIncrementBytesStillToScan > 0) || (MM_CycleState::state_process_work_packets_after_initial_mark != _persistentGlobalMarkPhaseState._markDelegateState)) {
		globalMarkPhase(env, true);
	}

	/* see if we finished the global mark phase */
	if (isGlobalMarkPhaseRunning()) {
		env->_cycleState->_currentIncrement += 1;
	} else {
		Assert_MM_true(env->_cycleState->_workPackets->isAllPacketsEmpty());
		if (_extensions->fvtest_tarokVerifyMarkMapClosure) {
			verifyMarkMapClosure(env, env->_cycleState->_markMap);
		}

		if (J9_EVENT_IS_HOOKED(_extensions->omrHookInterface, J9HOOK_MM_OMR_OBJECT_DELETE)) {
			_markMapManager->reportDeletedObjects(env, _markMapManager->getPartialGCMap(), _markMapManager->getGlobalMarkPhaseMap());
		}

		/* all objects in all regions are now marked, so change the type of these regions */
		declareAllRegionsAsMarked(env);

		/* swap the mark maps since we just finished building the new complete one */
		_markMapManager->swapMarkMaps();

		env->_cycleState->_markMap = NULL;
		env->_cycleState->_workPackets = NULL;
		env->_cycleState->_currentIncrement = 0;
	}

	incrementRegionAges(env, _taxationThreshold, false);

	/* If the GMP is no longer running, then we have run the final increment of the cycle. */
	Assert_MM_true(0 == static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats.getStallTime());
	if(!isGlobalMarkPhaseRunning()) {
		reportGCCycleFinalIncrementEnding(env);
		/* TODO: TEMPORARY: This is a temporary call that should be deleted once the new verbose format is in place */
		/* NOTE: May want to move any tracepoints up into this routine */
		reportGCIncrementEnd(env);
		reportGMPIncrementEnd(env);
		reportGMPCycleEnd(env);
		/* Remember how many PGC's occured per GMP cycle, so that we can weight GMP cost properly */
		_extensions->globalVLHGCStats._previousPgcPerGmpCount = _schedulingDelegate.getPgcCountSinceGMPEnd(env);
		_schedulingDelegate.globalMarkCycleEnd(env);
		_extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd = true;
		/* clear new OwnableSynchronizerObject count after scanOwnableSynchronizerObject in clearable phase */
		_extensions->allocationStats.clearOwnableSynchronizer();
	} else {
		/* TODO: TEMPORARY: This is a temporary call that should be deleted once the new verbose format is in place */
		/* NOTE: May want to move any tracepoints up into this routine */
		reportGCIncrementEnd(env);
		reportGMPIncrementEnd(env);
	}

	postCollect(env, env->_cycleState->_activeSubSpace);

	if (isGlobalMarkPhaseRunning()) {
		/* the GMP is going to require another increment so allow concurrent to run */
		_globalMarkPhaseIncrementBytesStillToScan = _schedulingDelegate.getBytesToScanInNextGMPIncrement(env);
	}
}

void
MM_IncrementalGenerationalGC::runGlobalGarbageCollection(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	/* If a GMP is already running, then the cycle will be commandeered.  Otherwise, start a new cycle representing the global collection.  */
	if(isGlobalMarkPhaseRunning()) {
		reportGMPCycleContinue(env);
	} else {
		reportGCCycleStart(env);
	}
	reportGlobalGCStart(env);
	reportGCIncrementStart(env, "global collect", env->_cycleState->_currentIncrement);

	/* Perform any main-specific setup */
	/* Tell the GAM to flush its contexts */
	MM_GlobalAllocationManager *gam = _extensions->globalAllocationManager;
	if (NULL != gam) {
		gam->flushAllocationContexts(env);
	}

	/* perform a full GC cycle */
	setupBeforeGlobalGC(env, env->_cycleState->_gcCode);

	if (_extensions->tarokUseProjectedSurvivalCollectionSet) {
		_projectedSurvivalCollectionSetDelegate.createRegionCollectionSetForGlobalGC(env);
	} else {
		_collectionSetDelegate.createRegionCollectionSetForGlobalGC(env);
	}
	
	_interRegionRememberedSet->prepareRegionsForGlobalCollect(env, isGlobalMarkPhaseRunning());

	globalMarkPhase(env, false);
	Assert_MM_false(isGlobalMarkPhaseRunning());
	if (J9_EVENT_IS_HOOKED(_extensions->omrHookInterface, J9HOOK_MM_OMR_OBJECT_DELETE)) {
		_markMapManager->reportDeletedObjects(env, _markMapManager->getPartialGCMap(), _markMapManager->getGlobalMarkPhaseMap());
	}
	if (_extensions->fvtest_tarokVerifyMarkMapClosure) {
		verifyMarkMapClosure(env, env->_cycleState->_markMap);
	}
	env->_cycleState->_markMap = NULL;
	env->_cycleState->_workPackets = NULL;

	/* all objects in all regions are now marked, so change the type of these regions */
	declareAllRegionsAsMarked(env);

	/* swap the mark maps since we just finished building the new complete one */
	_markMapManager->swapMarkMaps();

	/* install the previous mark map for use by sweep and compact */
	env->_cycleState->_markMap = _markMapManager->getPartialGCMap();
	/* we compact everything; compactSelectionGoalInBytes is irrelevant */
	UDATA compactSelectionGoalInBytes = 0;
	{
		MM_CompactGroupPersistentStats *persistentStats = _extensions->compactGroupPersistentStats;
		MM_CompactGroupPersistentStats::updateStatsBeforeCollect(env, persistentStats);
		Trc_MM_ReclaimDelegate_runReclaimComplete_Entry(env->getLanguageVMThread(), compactSelectionGoalInBytes, 0);
		_reclaimDelegate.runReclaimCompleteSweep(env, allocDescription, env->_cycleState->_activeSubSpace, env->_cycleState->_gcCode);
		_reclaimDelegate.runReclaimCompleteCompact(env, allocDescription, env->_cycleState->_activeSubSpace, env->_cycleState->_gcCode, _markMapManager->getGlobalMarkPhaseMap(), compactSelectionGoalInBytes);
		Trc_MM_ReclaimDelegate_runReclaimComplete_Exit(env->getLanguageVMThread(), 0);
	}

	UDATA defragmentReclaimableRegions = 0;
	UDATA reclaimableRegions = 0;
	_reclaimDelegate.estimateReclaimableRegions(env, _schedulingDelegate.getAverageEmptinessOfCopyForwardedRegions(), &reclaimableRegions, &defragmentReclaimableRegions);
	_schedulingDelegate.globalGarbageCollectCompleted(env, reclaimableRegions, defragmentReclaimableRegions);

	if (_extensions->tarokUseProjectedSurvivalCollectionSet) {
		_projectedSurvivalCollectionSetDelegate.deleteRegionCollectionSetForGlobalGC(env);
	} else {
		_collectionSetDelegate.deleteRegionCollectionSetForGlobalGC(env);
	}

	env->_cycleState->_markMap = NULL;
	env->_cycleState->_currentIncrement = 0;

	_extensions->globalVLHGCStats._heapSizingData.readyToResizeAtGlobalEnd = true;
	if (attemptHeapResize(env, allocDescription)) {
		/* Check was it successful contraction */
		if (env->_cycleState->_activeSubSpace->wasContractedThisGC(_extensions->globalVLHGCStats.gcCount)) {
			_interRegionRememberedSet->setShouldFlushBuffersForDecommitedRegions();
		}
	}

	_taxationThreshold = _schedulingDelegate.getInitialTaxationThreshold(env);
	_configuredSubspace->setBytesRemainingBeforeTaxation(_taxationThreshold);
	_allocatedSinceLastPGC = _taxationThreshold;

	/*
	 * Currently we set age of all active regions to maximum after each Global collection
	 * This logic need to be revisited eventually
	 */
	/* Global Collection - we max out ages on all live regions to remove them from the nursery collection set */
	setRegionAgesToMax(env);
	Assert_MM_true(0 == static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats.getStallTime());
	reportGCCycleFinalIncrementEnding(env);
	/* TODO: TEMPORARY: This is a temporary call that should be deleted once the new verbose format is in place */
	/* NOTE: May want to move any tracepoints up into this routine */
	reportGCIncrementEnd(env);
	reportGlobalGCEnd(env);
	reportGCCycleEnd(env);

	_extensions->allocationStats.clear();
}

void
MM_IncrementalGenerationalGC::reportGCCycleFinalIncrementEnding(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_CommonGCData commonData;
	TRIGGER_J9HOOK_MM_OMR_GC_CYCLE_END(
		extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_GC_CYCLE_END,
		extensions->getHeap()->initializeCommonGCData(env, &commonData),
		env->_cycleState->_type,
		omrgc_condYieldFromGC
	);
}

void
MM_IncrementalGenerationalGC::reportGCStart(MM_EnvironmentBase *env)
{
	/* this function is deprecated in IGGC since we need to report more specific events for PGC, GMP, and global */
	Assert_MM_unreachable();
}

void
MM_IncrementalGenerationalGC::reportGCEnd(MM_EnvironmentBase *env)
{
	/* this function is deprecated in IGGC since we need to report more specific events for PGC, GMP, and global */
	Assert_MM_unreachable();
}

void
MM_IncrementalGenerationalGC::flushRememberedSetIntoCardTable(MM_EnvironmentVLHGC *env)
{
	MM_ParallelDispatcher *dispatcher = _extensions->dispatcher;
	MM_CardListFlushTask flushTask(env, dispatcher, _regionManager, _interRegionRememberedSet);
	dispatcher->run(env, &flushTask);
}

bool
MM_IncrementalGenerationalGC::attemptHeapResize(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	bool isSystemGC = env->_cycleState->_gcCode.isExplicitGC();

	/* Take a snapshot of the current information relevant to heap sizing (PGC/GMP time, eden + survivor space, etc.) */
	_schedulingDelegate.updateHeapSizingData(env);

	env->_cycleState->_activeSubSpace->checkResize(env, allocDescription, isSystemGC);
	env->_cycleState->_activeSubSpace->performResize(env, allocDescription);

	/* Heap size now fixed for next cycle so reset heap statistics */
	_extensions->heap->resetHeapStatistics(true);

	return true;
}

void
MM_IncrementalGenerationalGC::preProcessPGCUsingCopyForward(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CycleStateVLHGC *cycleState = static_cast<MM_CycleStateVLHGC*>(env->_cycleState);

	/* Record stats before a copy forward */
	UDATA freeMemoryForSurvivor = _extensions->getHeap()->getActualFreeMemorySize();
	/* 
	 * In certain situations (eg, startup, change in allocation pattern, etc), the amount of free tenure might be overestimated.
	 * This overestimate, can cause "tenure" to be too small, resulting in excessive GMP's or GCC's.
	 * 
	 * At this moment in time (right before copy forward), eden is 100% full, so all free memory is part of "tenure"
	 * 
	 * NOTE: A second estimate for free tenure is later made - The lowest estimate for free tenure is used in heap sizing calculations
	 */
	_extensions->globalVLHGCStats._heapSizingData.freeTenure = freeMemoryForSurvivor;

	cycleState->_vlhgcIncrementStats._copyForwardStats._freeMemoryBefore = freeMemoryForSurvivor;
	cycleState->_vlhgcIncrementStats._copyForwardStats._totalMemoryBefore = _extensions->getHeap()->getMemorySize();

	if (_extensions->tarokUseProjectedSurvivalCollectionSet) {
		_projectedSurvivalCollectionSetDelegate.createRegionCollectionSetForPartialGC(env);
	} else {
		_collectionSetDelegate.createRegionCollectionSetForPartialGC(env);
	}

	UDATA desiredCompactWork = _schedulingDelegate.getDesiredCompactWork();
	cycleState->_desiredCompactWork = desiredCompactWork;
	UDATA estimatedSurvivorRequired = _copyForwardDelegate.estimateRequiredSurvivorBytes(env);

	MM_GlobalAllocationManagerTarok *allocationmanager = (MM_GlobalAllocationManagerTarok *)_extensions->globalAllocationManager;
	UDATA freeRegions = allocationmanager->getFreeRegionCount();
	double estimatedReguiredSurvivorRegions = _schedulingDelegate.getAverageSurvivorSetRegionCount();
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	/* Adjust estimatedReguiredSurvivorRegions if extensions->fvtest_forceCopyForwardHybridRatio is set(for testing purpose) */
	if ((0 != extensions->fvtest_forceCopyForwardHybridRatio) && (100 >= extensions->fvtest_forceCopyForwardHybridRatio)) {
		estimatedReguiredSurvivorRegions = estimatedReguiredSurvivorRegions * (100 - extensions->fvtest_forceCopyForwardHybridRatio) / 100;
	}

	if ((_schedulingDelegate.isPGCAbortDuringGMP() || _schedulingDelegate.isFirstPGCAfterGMP()) && (estimatedReguiredSurvivorRegions > freeRegions)) {
		double edenSurvivorRate = _schedulingDelegate.getAvgEdenSurvivalRateCopyForward(env);
		UDATA regionCountRequiredMarkOnly = 0;
		if (0 != edenSurvivorRate) {
			regionCountRequiredMarkOnly = (UDATA)((estimatedReguiredSurvivorRegions - freeRegions) / edenSurvivorRate);
		} else {
			regionCountRequiredMarkOnly = _schedulingDelegate.getCurrentEdenSizeInRegions(env);
		}
		/* set the number of the selected eden regions to nonEvacuated region to avoid potential abort case */
		_copyForwardDelegate.setReservedNonEvacuatedRegions(regionCountRequiredMarkOnly);
	}

	bool useSlidingCompactor = ((estimatedSurvivorRequired + desiredCompactWork) > freeMemoryForSurvivor);
	cycleState->_useSlidingCompactor = useSlidingCompactor;
	Trc_MM_IncrementalGenerationalGC_partialGarbageCollectUsingCopyForward_ChooseCompactor(env->getLanguageVMThread(), estimatedSurvivorRequired, desiredCompactWork, freeMemoryForSurvivor, useSlidingCompactor ? "sliding" : "copying");

	if (!useSlidingCompactor) {
		_reclaimDelegate.createRegionCollectionSetForPartialGC(env, desiredCompactWork);
		/* no external compact work -- it's all being done using copy-forward */
		cycleState->_vlhgcIncrementStats._copyForwardStats._externalCompactBytes = 0;
	}

	_schedulingDelegate.partialGarbageCollectStarted(env);

	/* flush the RSList and RSM from our currently selected regions into the card table since we will rebuild them as we process the table */
	flushRememberedSetIntoCardTable(env);

	_interRegionRememberedSet->flushBuffersForDecommitedRegions(env);

	Assert_MM_true(cycleState->_markMap == _markMapManager->getPartialGCMap());
	Assert_MM_true(cycleState->_workPackets == _workPacketsForPartialGC);
	
	_copyForwardDelegate.preCopyForwardSetup(env);

	reportCopyForwardStart(env);
	cycleState->_vlhgcIncrementStats._copyForwardStats._cycleStartTime = j9time_hires_clock();

	MM_CompactGroupPersistentStats::updateStatsBeforeCopyForward(env, _extensions->compactGroupPersistentStats);
}

void
MM_IncrementalGenerationalGC::postProcessPGCUsingCopyForward(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CycleStateVLHGC *cycleState = static_cast<MM_CycleStateVLHGC*>(env->_cycleState);

	MM_CompactGroupPersistentStats::updateStatsAfterCopyForward(env, _extensions->compactGroupPersistentStats);
	U_64 endTimeOfCopyForward = j9time_hires_clock();

	/* Record stats after a copy forward */
	cycleState->_vlhgcIncrementStats._copyForwardStats._freeMemoryAfter = _extensions->getHeap()->getActualFreeMemorySize();
	cycleState->_vlhgcIncrementStats._copyForwardStats._totalMemoryAfter = _extensions->getHeap()->getMemorySize();

	reportCopyForwardEnd(env, endTimeOfCopyForward - cycleState->_vlhgcIncrementStats._copyForwardStats._cycleStartTime);

	postMarkMapCompletion(env);
	_copyForwardDelegate.postCopyForwardCleanup(env);

	if (_extensions->tarokEnableExpensiveAssertions) {
		/* all objects in collection set regions are now marked so ensure that there are no more bump-allocated-only regions */
		GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		while (NULL != (region = regionIterator.nextRegion())) {
			Assert_MM_false(region->getRegionType() == MM_HeapRegionDescriptor::ADDRESS_ORDERED);
		}
	}

	_schedulingDelegate.copyForwardCompleted(env);

	/* It is possible that we could end up with evacuate regions which were not compacted (inaccurate RSCL) so we need to detect that case and sweep such regions, before completing the PGC */
	UDATA regionsSkippedByCompactorRequiringSweep = 0;
	if (cycleState->_useSlidingCompactor) {
		/* compact to meet compaction targets, as well as compacting any unsuccessfully evacuated regions */
		uintptr_t desiredCompactWork = cycleState->_desiredCompactWork;
		_reclaimDelegate.runCompact(env, allocDescription, cycleState->_activeSubSpace, desiredCompactWork, cycleState->_gcCode, _markMapManager->getGlobalMarkPhaseMap(), &regionsSkippedByCompactorRequiringSweep);
		/* we can't tell exactly how many bytes were added to meet the compact goal, so we'll assume it was an exact match. The precise amount could be lower or higher */ 
		cycleState->_vlhgcIncrementStats._copyForwardStats._externalCompactBytes = desiredCompactWork;
	/* cycleState->_abortFlagRaisedDuringPGC dictates if PGC was indeed successful or not */
	} else if (!cycleState->_abortFlagRaisedDuringPGC || _copyForwardDelegate.isHybrid(env)) {
		/* compact any unsuccessfully evacuated regions (include reclaiming jni critical eden regions)*/
		_reclaimDelegate.runReclaimForAbortedCopyForward(env, allocDescription, cycleState->_activeSubSpace, cycleState->_gcCode, _markMapManager->getGlobalMarkPhaseMap(), &regionsSkippedByCompactorRequiringSweep);
	}

	if (regionsSkippedByCompactorRequiringSweep > 0) {
		/* there were regions which we needed to compact but we couldn't compact so at least sweep them to ensure that their stats are correct */
		_reclaimDelegate.performAtomicSweep(env, allocDescription, cycleState->_activeSubSpace, cycleState->_gcCode);
	}

	/* calculatePGCCompactionRate() has to be after PGC due to half of Eden regions has not been marked after final GMP (the sweep could not collect those regions) */
	/* calculatePGCCompactionRate() has to be before estimateReclaimableRegions(), which need to use the result of calculatePGCCompactionRate() - region->_defragmentationTarget */
	_schedulingDelegate.recalculateRatesOnFirstPGCAfterGMP(env);

	/* Need to understand how to do the estimates here found within the following two calls */
	UDATA defragmentReclaimableRegions = 0;
	UDATA reclaimableRegions = 0;
	_reclaimDelegate.estimateReclaimableRegions(env, _schedulingDelegate.getAverageEmptinessOfCopyForwardedRegions(), &reclaimableRegions, &defragmentReclaimableRegions);
	_schedulingDelegate.partialGarbageCollectCompleted(env, reclaimableRegions, defragmentReclaimableRegions);

	if (_extensions->tarokUseProjectedSurvivalCollectionSet) {
		_projectedSurvivalCollectionSetDelegate.deleteRegionCollectionSetForPartialGC(env);
	} else {
		_collectionSetDelegate.deleteRegionCollectionSetForPartialGC(env);
	}

	Assert_MM_false(_workPacketsForGlobalGC->getOverflowFlag());
	Assert_MM_false(_workPacketsForPartialGC->getOverflowFlag());

	if (_extensions->fvtest_tarokVerifyMarkMapClosure) {
		verifyMarkMapClosure(env, cycleState->_markMap);
	}
}

void 
MM_IncrementalGenerationalGC::setupBeforePartialGC(MM_EnvironmentVLHGC *env, MM_GCCode gcCode)
{
	env->_cycleState->_workPackets = _workPacketsForPartialGC;
	env->_cycleState->_markMap = _markMapManager->getPartialGCMap();

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	env->_cycleState->_dynamicClassUnloadingEnabled = _extensions->tarokEnableIncrementalClassGC;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

	setupBeforeGC(env);
}

void
MM_IncrementalGenerationalGC::setupBeforeGlobalGC(MM_EnvironmentVLHGC *env, MM_GCCode gcCode)
{
	/* ensure heap base is aligned to region size */
	UDATA heapBase = (UDATA)_extensions->heap->getHeapBase();
	UDATA regionSize = _extensions->regionSize;
	Assert_MM_true((0 != regionSize) && (0 == (heapBase % regionSize)));

	Assert_MM_true(&_persistentGlobalMarkPhaseState == env->_cycleState);
	
	if (isGlobalMarkPhaseRunning()) {
		/* these must already be set up correctly if a global mark is in progress */
		Assert_MM_true(_workPacketsForGlobalGC == env->_cycleState->_workPackets);
		Assert_MM_true(_markMapManager->getGlobalMarkPhaseMap() == env->_cycleState->_markMap);
	} else {
		Assert_MM_true(NULL == env->_cycleState->_workPackets);
		Assert_MM_true(NULL == env->_cycleState->_markMap);

		/* set the work packets and mark map for our cycle */
		env->_cycleState->_workPackets = _workPacketsForGlobalGC;
		env->_cycleState->_markMap = _markMapManager->getGlobalMarkPhaseMap();
	}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/* a global or a GMP is always performing class unloading, in Tarok, at this time */
	env->_cycleState->_dynamicClassUnloadingEnabled = true;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

	setupBeforeGC(env);
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.clear();
}

void
MM_IncrementalGenerationalGC::setConfiguredSubspace(MM_EnvironmentBase *env, MM_MemorySubSpaceTarok *configuredSubspace)
{
	Assert_MM_true(NULL == _configuredSubspace);
	Assert_MM_true(NULL != configuredSubspace);
	_configuredSubspace = configuredSubspace;

	Assert_MM_true(_configuredSubspace->getActualFreeMemorySize() <= _configuredSubspace->getCurrentSize());
}

void 
MM_IncrementalGenerationalGC::initialRegionAgesSetup(MM_EnvironmentVLHGC *env, UDATA givenAge)
{
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	MM_HeapRegionDescriptorVLHGC *region = NULL;

	U_64 age = (U_64)givenAge;
	if (age > _extensions->tarokMaximumAgeInBytes) {
		age = _extensions->tarokMaximumAgeInBytes;
	}

	while (NULL != (region = regionIterator.nextRegion())) {
		/* Adjust age for non-empty regions. */
		if(region->containsObjects() || region->isArrayletLeaf()) {

			/*
			 * At the moment of creation taxation point was artificially set to tarokMaximumAgeInBytes
			 * Correct age of regions allocated before initialization
			 * Assume that all this regions are allocated _NOW_
			 */
			region->resetAge(env, age);
		}
	}
}

void
MM_IncrementalGenerationalGC::incrementRegionAges(MM_EnvironmentVLHGC *env, UDATA increment, bool isPGC)
{
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	MM_AllocationContextTarok *commonContext = (MM_AllocationContextTarok *)env->getCommonAllocationContext();
	_interRegionRememberedSet->setUnusedRegionThreshold(env, _schedulingDelegate.getDefragmentEmptinessThreshold(env));

	while (NULL != (region = regionIterator.nextRegion())) {
		/* Adjust age for non-empty regions. */
		if(region->containsObjects() || region->isArrayletLeaf()) {

			UDATA previousLogicalAge = region->getLogicalAge();
			/* Increment ages up to the maximum allowable region age */
			incrementRegionAge(env, region, increment, isPGC);

			MM_AllocationContextTarok *owner = region->_allocateData._owningContext;
			if (owner->shouldMigrateRegionToCommonContext(env, region)) {
				/* migrate to common context */
				if (owner != commonContext) {
					if ((NULL == region->_allocateData._originalOwningContext) && (commonContext->getNumaNode() != owner->getNumaNode())) {
						region->_allocateData._originalOwningContext = owner;
					}
					region->_allocateData._owningContext = commonContext;
					owner->migrateRegionToAllocationContext(region, commonContext);
				}
			}

			if (region->containsObjects() && (region->getLogicalAge() == env->getExtensions()->tarokRegionMaxAge)) {
				/* regions that are full and age out are considered 'stable' */
				_interRegionRememberedSet->overflowIfStableRegion(env, region);

				/* regions that age out, but are not full (thus not stable => accurate), should merge with other old non-full regions (in same AC) */
				if (region->getRememberedSetCardList()->isAccurate()) {
					if (previousLogicalAge < _extensions->tarokRegionMaxAge) {
						_schedulingDelegate.updateCurrentMacroDefragmentationWork(env, region);
					}
				}
			}
		}
	}

	/* Overflowing stable regions releases buffers to thread local pool. Move them now to the locked global pool.
	 * (if this is run in context of non-GC thread there will be not further opportunities to do it).
	 */
	_interRegionRememberedSet->releaseCardBufferControlBlockListForThread(env, env);
}

void 
MM_IncrementalGenerationalGC::incrementRegionAge(MM_EnvironmentVLHGC *env, MM_HeapRegionDescriptorVLHGC *region, UDATA increment, bool isPGC)
{
	/* Update age for bytes-allocated-based aging system */
	U_64 allocationAge = region->getAllocationAge();

	U_64 allocationAgeBefore = allocationAge;
	UDATA logicalAgeBefore = region->getLogicalAge();

	U_64 maxAgeInBytes = _extensions->tarokMaximumAgeInBytes;
	if (allocationAge < maxAgeInBytes) {
		U_64 value = allocationAge + increment;
		if (allocationAge <= value) {
			/* no overflow */
			if (value <= maxAgeInBytes) {
				/* still be undo maximum age, increment it */
				allocationAge = value;
			} else {
				/* Maximum age is exceeded, saturate */
				allocationAge = maxAgeInBytes;
			}
		} else {
			/* overflow, set to the maximum possible age */
			allocationAge = maxAgeInBytes;
		}
	}

	UDATA logicalAge = 0;
	if (_extensions->tarokAllocationAgeEnabled) {
		logicalAge = MM_CompactGroupManager::calculateLogicalAgeForRegion(env, allocationAge);
	} else {
		/* Calculate age for PGC-count-based (old) aging system */
		logicalAge = region->getLogicalAge();
		if (isPGC) {
			if (logicalAge < _extensions->tarokRegionMaxAge) {
				logicalAge += 1;
			}
		}
	}
	
	region->incrementAgeBounds(increment);

	Trc_MM_IncrementalGenerationalGC_incrementRegionAge(env->getLanguageVMThread(),
			_regionManager->mapDescriptorToRegionTableIndex(region),
			isPGC,
			(double)increment/(1024*1024),
			(double)allocationAgeBefore/(1024*1024),
			(double)allocationAge/(1024*1024),
			(double)region->getLowerAgeBound() / (1024 * 1024),
			(double)region->getUpperAgeBound() / (1024 * 1024),
			logicalAgeBefore,
			logicalAge);

	region->setAge(allocationAge, logicalAge);

}

void
MM_IncrementalGenerationalGC::setRegionAgesToMax(MM_EnvironmentVLHGC *env)
{
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager, MM_HeapRegionDescriptor::MANAGED);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	MM_AllocationContextTarok *commonContext = (MM_AllocationContextTarok *)env->getCommonAllocationContext();
	while (NULL != (region = regionIterator.nextRegion())) {
		/* Adjust age for non-empty regions.  Note: Currently object artifact regions (e.g., arraylet leaves) won't
		 * have their age adjusted.  The parent of the artifact will have its region age, which it implicitly inherits.
		 */
		if(region->containsObjects()) {
			region->setAge(_extensions->tarokMaximumAgeInBytes, _extensions->tarokRegionMaxAge);
			/* migrate to common context */
			MM_AllocationContextTarok *owner = region->_allocateData._owningContext;
			if ( (owner != commonContext) && owner->shouldMigrateRegionToCommonContext(env, region) ) {
				if ((NULL == region->_allocateData._originalOwningContext) && (commonContext->getNumaNode() != owner->getNumaNode())) {
					region->_allocateData._originalOwningContext = owner;
				}
				region->_allocateData._owningContext = commonContext;
				owner->migrateRegionToAllocationContext(region, commonContext);
			}
		} else if (region->isArrayletLeaf()) {
			/* adjust age for arraylet leaves */
			region->setAge(_extensions->tarokMaximumAgeInBytes, _extensions->tarokRegionMaxAge);
		}
	}
}

void
MM_IncrementalGenerationalGC::declareAllRegionsAsMarked(MM_EnvironmentVLHGC *env)
{
	bool isPartialCollect = (MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType);
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			if (region->getRegionType() == MM_HeapRegionDescriptor::ADDRESS_ORDERED) {
				/* if this is a partial collect, then this region must have been part of the collection set */
				Assert_MM_true(!isPartialCollect || region->_markData._shouldMark);
				region->setRegionType(MM_HeapRegionDescriptor::ADDRESS_ORDERED_MARKED);
			}
			
			if (isPartialCollect) {
				Assert_MM_false(region->_previousMarkMapCleared);
			} else {
				Assert_MM_false(region->_nextMarkMapCleared);
			}
	
			/* tag any regions we marked as unswept since sweep depends on the mark map */
			if (!isPartialCollect || region->_markData._shouldMark) {
				region->_sweepData._alreadySwept = false;
			}
		}
	}
}

bool
MM_IncrementalGenerationalGC::isMarked(void *objectPtr)
{
	/* the mark map used for PGC should be the most accurate */
	return _markMapManager->getPartialGCMap()->isBitSet(static_cast<J9Object*>(objectPtr));
}

void
MM_IncrementalGenerationalGC::assertWorkPacketsEmpty(MM_EnvironmentVLHGC *env, MM_WorkPacketsVLHGC *packets)
{
	MM_WorkPacketsIterator iterator = MM_WorkPacketsIterator(env, packets);
	MM_Packet *packet = iterator.nextPacket(env);
	while (NULL != packet)
	{
		Assert_MM_true(packet->isEmpty());
		packet = iterator.nextPacket(env);
	}
}

void
MM_IncrementalGenerationalGC::verifyMarkMapClosure(MM_EnvironmentVLHGC *env, MM_MarkMap *markMap)
{
	Assert_MM_true(NULL != markMap);
	Assert_MM_true(_extensions->fvtest_tarokVerifyMarkMapClosure);
	
	GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
	MM_HeapRegionDescriptorVLHGC *region = NULL;
	while (NULL != (region = regionIterator.nextRegion())) {
		if (region->containsObjects()) {
			UDATA *regionBase = (UDATA *)region->getLowAddress();
			UDATA *regionTop = (UDATA *)region->getHighAddress();

			MM_HeapMapIterator iterator = MM_HeapMapIterator(_extensions, markMap, regionBase, regionTop, false);
			J9Object *object = NULL;
			while (NULL != (object = iterator.nextObject())) {
				/* first, check the validity of the object's class */
				J9Class *clazz = J9GC_J9OBJECT_CLAZZ(object, env);
				Assert_MM_true((UDATA)0x99669966 == clazz->eyecatcher);
				/* second, verify that it is an instance of a marked class */
				J9Object *classObject = (J9Object *)clazz->classObject;
				Assert_MM_true(markMap->isBitSet(classObject));

				/* now that we know the class is valid, verify that this object only refers to other marked objects */
				switch(_extensions->objectModel.getScanType(object)) {
				case GC_ObjectModel::SCAN_REFERENCE_MIXED_OBJECT:
					Assert_MM_true(GC_ObjectModel::REF_STATE_REMEMBERED != J9GC_J9VMJAVALANGREFERENCE_STATE(env, object));
					/* fall through */
				case GC_ObjectModel::SCAN_MIXED_OBJECT_LINKED:
				case GC_ObjectModel::SCAN_ATOMIC_MARKABLE_REFERENCE_OBJECT:
				case GC_ObjectModel::SCAN_MIXED_OBJECT:
				case GC_ObjectModel::SCAN_CLASS_OBJECT:
				case GC_ObjectModel::SCAN_CLASSLOADER_OBJECT:
				{
					Assert_MM_true(_extensions->classLoaderRememberedSet->isInstanceRemembered(env, object));
					
					GC_MixedObjectIterator mixedObjectIterator(_javaVM->omrVM, object);
					GC_SlotObject *slotObject = NULL;
		
					while (NULL != (slotObject = mixedObjectIterator.nextSlot())) {
						J9Object *target = slotObject->readReferenceFromSlot();
						Assert_MM_true((NULL == target) || markMap->isBitSet(target));
					}
				}
				break;
				case GC_ObjectModel::SCAN_POINTER_ARRAY_OBJECT:
				{
					Assert_MM_true(_extensions->classLoaderRememberedSet->isInstanceRemembered(env, object));
	
					GC_PointerArrayIterator pointerArrayIterator(_javaVM, object);
					GC_SlotObject *slotObject = NULL;
		
					while (NULL != (slotObject = pointerArrayIterator.nextSlot())) {
						J9Object *target = slotObject->readReferenceFromSlot();
						Assert_MM_true((NULL == target) || markMap->isBitSet(target));
					}
				}
				break;
				case GC_ObjectModel::SCAN_PRIMITIVE_ARRAY_OBJECT:
					/* nothing to do */
					break;
				default:
					Assert_MM_unreachable();
				}
			}
		}
	}
}

void
MM_IncrementalGenerationalGC::assertTableClean(MM_EnvironmentVLHGC *env, Card additionalCleanState)
{
	/* first, walk all regions which have subspaces */
	GC_HeapRegionIterator regionIterator(_regionManager);
	MM_HeapRegionDescriptor *region = NULL;
	while(NULL != (region = regionIterator.nextRegion())) {
		/* we will be tolerant of dirty cards in regions which don't contain objects but we may want a more strict definition
		 * of what that means, in the future.
		 */
		if (region->containsObjects()) {
			/* look up the card range for this region and walk it */
			Card *lowCard = _extensions->cardTable->heapAddrToCardAddr(env, region->getLowAddress());
			Card *highCard = _extensions->cardTable->heapAddrToCardAddr(env, region->getHighAddress());
			for (Card *thisCard = lowCard; thisCard < highCard; thisCard++) {
				Card cardValue = *thisCard;
				Assert_GC_true_with_message2(env, ((additionalCleanState == cardValue) || (CARD_CLEAN == cardValue)), "The card %p is not clean, value %u\n", thisCard, cardValue);
			}
		}
	}
}

void
MM_IncrementalGenerationalGC::reportPGCStart(MM_EnvironmentVLHGC *env)
{
	UDATA incrementNumber = 0;
	if (isGlobalMarkPhaseRunning()) {
		incrementNumber = _persistentGlobalMarkPhaseState._currentIncrement;
	}

	/* currently we only differ for the tracepoint since we still need to fire the global hook */
	Trc_MM_PGCStart(env->getLanguageVMThread(), _extensions->globalVLHGCStats.gcCount, incrementNumber);
	triggerGlobalGCStartHook(env);
}

void
MM_IncrementalGenerationalGC::reportPGCEnd(MM_EnvironmentVLHGC *env)
{
	Trc_MM_PGCEnd(env->getLanguageVMThread(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowOccured(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowCount(),
		_extensions->heap->getApproximateActiveFreeMemorySize(MEMORY_TYPE_OLD),
		_extensions->heap->getActiveMemorySize(MEMORY_TYPE_OLD)
	);

	triggerGlobalGCEndHook(env);
}

void
MM_IncrementalGenerationalGC::reportGMPIncrementStart(MM_EnvironmentVLHGC *env)
{
	UDATA incrementNumber = env->_cycleState->_currentIncrement;
	
	/* currently we only differ for the tracepoint since we still need to fire the global hook */
	Trc_MM_GMPIncrementStart(env->getLanguageVMThread(), _extensions->globalVLHGCStats.gcCount, incrementNumber);
	triggerGlobalGCStartHook(env);
}

void
MM_IncrementalGenerationalGC::reportGMPIncrementEnd(MM_EnvironmentVLHGC *env)
{
	/* note that this will be the next increment number so it will be 0 if the GMP finished */
	UDATA incrementNumber = env->_cycleState->_currentIncrement;
	
	Trc_MM_GMPIncrementEnd(env->getLanguageVMThread(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowOccured(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowCount(),
		_extensions->heap->getApproximateActiveFreeMemorySize(MEMORY_TYPE_OLD),
		_extensions->heap->getActiveMemorySize(MEMORY_TYPE_OLD),
		incrementNumber
	);

	triggerGlobalGCEndHook(env);
}

void
MM_IncrementalGenerationalGC::reportGlobalGCStart(MM_EnvironmentVLHGC *env)
{
	/* currently we only differ for the tracepoint since we still need to fire the global hook */
	Trc_MM_GlobalGCStart(env->getLanguageVMThread(), _extensions->globalVLHGCStats.gcCount);
	triggerGlobalGCStartHook(env);
}

void
MM_IncrementalGenerationalGC::reportGlobalGCEnd(MM_EnvironmentVLHGC *env)
{
	Trc_MM_GlobalGCEnd(env->getLanguageVMThread(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowOccured(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowCount(),
		_extensions->heap->getApproximateActiveFreeMemorySize(MEMORY_TYPE_OLD),
		_extensions->heap->getActiveMemorySize(MEMORY_TYPE_OLD)
	);

	triggerGlobalGCEndHook(env);
}

void
MM_IncrementalGenerationalGC::triggerGlobalGCStartHook(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	
	/* passed as UDATA since that is what the hook requires */
	UDATA isExplicitGC = 0;
	UDATA isAggressiveGC = 0;
	if (NULL != env->_cycleState) {
		isExplicitGC = env->_cycleState->_gcCode.isExplicitGC() ? 1 : 0;
		isAggressiveGC = env->_cycleState->_gcCode.isAggressiveGC() ? 1 : 0;
	}
	
	TRIGGER_J9HOOK_MM_OMR_GLOBAL_GC_START(
		_extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_GLOBAL_GC_START,
		_extensions->globalVLHGCStats.gcCount,
		0,
		isExplicitGC,
		isAggressiveGC,
		_bytesRequested);
}

void
MM_IncrementalGenerationalGC::triggerGlobalGCEndHook(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	
	/* these are assigned to temporary variable out-of-line since some preprocessors get confused if you have directives in macros */
	UDATA approximateActiveFreeMemorySize = 0;
	UDATA activeMemorySize = 0;
	
	TRIGGER_J9HOOK_MM_OMR_GLOBAL_GC_END(
		_extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_GLOBAL_GC_END,
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowOccured(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowCount(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkpacketCountAtOverflow(),
		_extensions->heap->getApproximateActiveFreeMemorySize(MEMORY_TYPE_NEW),
		_extensions->heap->getActiveMemorySize(MEMORY_TYPE_NEW),
		_extensions->heap->getApproximateActiveFreeMemorySize(MEMORY_TYPE_OLD),
		_extensions->heap->getActiveMemorySize(MEMORY_TYPE_OLD),
		(_extensions-> largeObjectArea ? 1 : 0),
		(_extensions-> largeObjectArea ? _extensions->heap->getApproximateActiveFreeLOAMemorySize(MEMORY_TYPE_OLD) : 0 ),
		(_extensions-> largeObjectArea ? _extensions->heap->getActiveLOAMemorySize(MEMORY_TYPE_OLD) : 0 ),
		/* We can't just ask the heap for everything of type FIXED, because that includes scopes as well */
		approximateActiveFreeMemorySize,
		activeMemorySize,
		FIXUP_NONE,
		0
	);
}

void
MM_IncrementalGenerationalGC::reportCopyForwardStart(MM_EnvironmentVLHGC *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_CopyForwardStart(env->getLanguageVMThread());
	TRIGGER_J9HOOK_MM_PRIVATE_COPY_FORWARD_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_COPY_FORWARD_START,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats);
}

void
MM_IncrementalGenerationalGC::reportCopyForwardEnd(MM_EnvironmentVLHGC *env, U_64 timeTaken)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	Trc_MM_CopyForwardEnd(env->getLanguageVMThread());
	TRIGGER_J9HOOK_MM_PRIVATE_COPY_FORWARD_END(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_COPY_FORWARD_END,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._copyForwardStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._irrsStats);
}

bool
MM_IncrementalGenerationalGC::isConcurrentWorkAvailable(MM_EnvironmentBase *env)
{
	bool isConcurrentEnabled = _extensions->tarokEnableConcurrentGMP;
	bool isGMPRunning = isGlobalMarkPhaseRunning();
	bool isProcessingWorkPackets = MM_CycleState::state_process_work_packets_after_initial_mark == _persistentGlobalMarkPhaseState._markDelegateState;
	bool isStillPermittedToRun = !_forceConcurrentTermination;
	bool isGMPWorkAvailable = _globalMarkPhaseIncrementBytesStillToScan > 0;
	
	return isConcurrentEnabled && isGMPRunning && isProcessingWorkPackets && isStillPermittedToRun && isGMPWorkAvailable;
}

void
MM_IncrementalGenerationalGC::preConcurrentInitializeStatsAndReport(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats)
{
	Assert_MM_true(isConcurrentWorkAvailable(env));
	Assert_MM_true(NULL == env->_cycleState);
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	stats->_cycleID = _persistentGlobalMarkPhaseState._verboseContextID;
	stats->_scanTargetInBytes = _globalMarkPhaseIncrementBytesStillToScan;
	env->_cycleState = &_persistentGlobalMarkPhaseState;
	U_64 markStartTime = j9time_hires_clock();
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._startTime = markStartTime;

	/* Get an estimate of total process time when concurrent mark started. This is used to determine how much GMP costs*/
	omrthread_process_time_t processStart;
	omrthread_get_process_times(&processStart);
	U_64 concurrentMarkStartTime = processStart._systemTime + processStart._userTime;
	stats->_concurrentMarkProcessStartTime = (uintptr_t)concurrentMarkStartTime;

	TRIGGER_J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_START(
			_extensions->privateHookInterface,
			env->getOmrVMThread(),
			markStartTime,
			J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_START,
			stats);
}

uintptr_t
MM_IncrementalGenerationalGC::mainThreadConcurrentCollect(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);

	/* note that we can't check isConcurrentWorkAvailable at this point since another thread could have set _forceConcurrentTermination since the
	 * main thread calls this outside of the control monitor
	 */
	Assert_MM_true(env->_cycleState == &_persistentGlobalMarkPhaseState);
	Assert_MM_true(isGlobalMarkPhaseRunning());
	Assert_MM_true(MM_CycleState::state_process_work_packets_after_initial_mark == _persistentGlobalMarkPhaseState._markDelegateState);

	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.clear();
	
	/* We pass a pointer to _forceConcurrentTermination so that we can cause the concurrent to terminate early by setting the
	 * flag to true if we want to interrupt it so that the main thread returns to the control mutex in order to receive a
	 * new GC request.
	 */
	UDATA bytesConcurrentlyScanned = _globalMarkDelegate.performMarkConcurrent(env, _globalMarkPhaseIncrementBytesStillToScan, &_forceConcurrentTermination);
	_globalMarkPhaseIncrementBytesStillToScan = MM_Math::saturatingSubtract(_globalMarkPhaseIncrementBytesStillToScan, bytesConcurrentlyScanned);
	
	/* Accumulate the mark increment stats into persistent GMP state*/
	_persistentGlobalMarkPhaseState._vlhgcCycleStats.merge(&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats);

	/* Release any resources that might be bound to this main thread,
	 * since it may be implicit and more importantly change for other phases of the cycle */
	_interRegionRememberedSet->releaseCardBufferControlBlockListForThread(env, env);
	
	/* return the number of bytes scanned since the caller needs to pass it into postConcurrentUpdateStatsAndReport for stats reporting */
	return bytesConcurrentlyScanned;
}

void
MM_IncrementalGenerationalGC::postConcurrentUpdateStatsAndReport(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats, UDATA bytesConcurrentlyScanned)
{
	Assert_MM_false(isConcurrentWorkAvailable(env));
	Assert_MM_true(env->_cycleState == &_persistentGlobalMarkPhaseState);
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	stats->_bytesScanned = bytesConcurrentlyScanned;
	stats->_terminationWasRequested = _forceConcurrentTermination;
	static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats._endTime = j9time_hires_clock();

	calculateConcurrentMarkWorkTime(env, stats);

	TRIGGER_J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_END(
			_extensions->privateHookInterface,
			env->getOmrVMThread(),
			j9time_hires_clock(),
			J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_END,
			stats);
	env->_cycleState = NULL;
}

void
MM_IncrementalGenerationalGC::calculateConcurrentMarkWorkTime(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats)
{
	/* Get an estimate of total process time now that concurrent mark increment has finished*/
	omrthread_process_time_t processEnd;
	omrthread_get_process_times(&processEnd);
	U_64 processEndTime = processEnd._systemTime + processEnd._userTime;

	/* Calculate how much process time has elapsed since the start of the concurrent mark increment */
	U_64 concurrentElapsedTime = processEndTime - stats->_concurrentMarkProcessStartTime;

	/* Work time we attribute to concurrent work is difference of process time of gc threads now vs when concurrent phase started */
	MM_MarkVLHGCStats markStats = _persistentGlobalMarkPhaseState._vlhgcIncrementStats._markStats;
	double concurrentGCRatio = 0.5;

	if (markStats._concurrentGCThreadsEndTimeSum != markStats._concurrentGCThreadsStartTimeSum) {
		/* If the platform supports getting time for each thread, calculate what % of time gc threads were active relative to application threads. */
		/* Determine the ratio of time spent doing GC work vs non-gc related work during the concurrent phase */
		U_64 concurrentGCWorkTime = markStats._concurrentGCThreadsEndTimeSum - markStats._concurrentGCThreadsStartTimeSum;
		concurrentGCRatio = (double)concurrentGCWorkTime/concurrentElapsedTime;

		/* If there was a clock error, concurrentGCWorkTime might be too high or negative. Make sure ratio is between 0.1 and 0.9 */
		concurrentGCRatio = OMR_MIN(concurrentGCRatio, 0.9);
		concurrentGCRatio = OMR_MAX(concurrentGCRatio, 0.1);
	} 

	/* The GC time we attribute to concurrent phase is (ratio of time doing GC work instead of mutator work) * time interval of concurrent mark increment */
	U_64 concurrentMarkGCThreadsWorkTime = (U_64)(concurrentElapsedTime * concurrentGCRatio);
	_persistentGlobalMarkPhaseState._vlhgcCycleStats._concurrentMarkStats._concurrentMarkGCThreadsTotalWorkTime += concurrentMarkGCThreadsWorkTime;

	Trc_MM_IncrementalGenerationalGC_calculateConcurrentMarkWorkTime(env->getLanguageVMThread(), concurrentGCRatio, concurrentMarkGCThreadsWorkTime / 1000, _persistentGlobalMarkPhaseState._vlhgcCycleStats._concurrentMarkStats._concurrentMarkGCThreadsTotalWorkTime / 1000);

	_schedulingDelegate.setConcurrentGlobalMarkTime(_persistentGlobalMarkPhaseState._vlhgcCycleStats._concurrentMarkStats._concurrentMarkGCThreadsTotalWorkTime);
}

void
MM_IncrementalGenerationalGC::forceConcurrentFinish()
{
	/* A pointer to _forceConcurrentTermination is passed into the concurrent increment manager so we can cause it to return
	 * early by setting this flag.
	 */
	_forceConcurrentTermination = true;
}


void
MM_IncrementalGenerationalGC::reportGMPCycleStart(MM_EnvironmentBase *env)
{
	reportGCCycleStart(env);
	Trc_MM_GMPCycleStart(env->getLanguageVMThread());

}

void
MM_IncrementalGenerationalGC::reportGMPCycleContinue(MM_EnvironmentBase *env)
{
	Trc_MM_GMPCycleEnd(env->getLanguageVMThread());
	reportGCCycleContinue(env, OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_MARK_PHASE);
}

void
MM_IncrementalGenerationalGC::reportGMPCycleEnd(MM_EnvironmentBase *env)
{
	Trc_MM_GMPCycleEnd(env->getLanguageVMThread());
	reportGCCycleEnd(env);
}

void
MM_IncrementalGenerationalGC::reportGCCycleStart(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_CommonGCData commonData;

	Trc_MM_CycleStart(env->getLanguageVMThread(), env->_cycleState->_type, _extensions->getHeap()->getActualFreeMemorySize());

	TRIGGER_J9HOOK_MM_OMR_GC_CYCLE_START(
		extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_GC_CYCLE_START,
		extensions->getHeap()->initializeCommonGCData(env, &commonData),
		env->_cycleState->_type);
}

void
MM_IncrementalGenerationalGC::reportGCCycleContinue(MM_EnvironmentBase *env, UDATA oldCycleStateType)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_CommonGCData commonData;

	Trc_MM_CycleContinue(env->getLanguageVMThread(), oldCycleStateType, env->_cycleState->_type, _extensions->getHeap()->getActualFreeMemorySize());

	TRIGGER_J9HOOK_MM_OMR_GC_CYCLE_CONTINUE(
		extensions->omrHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_OMR_GC_CYCLE_CONTINUE,
		extensions->getHeap()->initializeCommonGCData(env, &commonData),
		oldCycleStateType,
		env->_cycleState->_type);
}

void
MM_IncrementalGenerationalGC::reportGCCycleEnd(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_CommonGCData commonData;

	Trc_MM_CycleEnd(env->getLanguageVMThread(), env->_cycleState->_type, _extensions->getHeap()->getActualFreeMemorySize());

	TRIGGER_J9HOOK_MM_PRIVATE_GC_POST_CYCLE_END(
		extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_GC_POST_CYCLE_END,
		extensions->getHeap()->initializeCommonGCData(env, &commonData),
		env->_cycleState->_type,
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowOccured(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkStackOverflowCount(),
		static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats.getSTWWorkpacketCountAtOverflow(),
		FIXUP_NONE,
		0
	);
}


void
MM_IncrementalGenerationalGC::exportStats(MM_EnvironmentVLHGC *env, MM_CollectionStatisticsVLHGC *stats, bool classesPotentiallyUnloaded)
{
	_interRegionRememberedSet->exportStats(env, stats);

	stats->_edenFreeHeapSize = 0;
	stats->_edenHeapSize = 0;

	stats->_arrayletReferenceObjects = 0;
	stats->_arrayletReferenceLeaves = 0;
	stats->_largestReferenceArraylet = 0;
	stats->_arrayletPrimitiveObjects = 0;
	stats->_arrayletPrimitiveLeaves = 0;
	stats->_largestPrimitiveArraylet = 0;
	stats->_arrayletUnknownObjects = 0;
	stats->_arrayletUnknownLeaves = 0;

	stats->_commonNumaNodeBytes = 0;
	stats->_localNumaNodeBytes = 0;
	stats->_nonLocalNumaNodeBytes = 0;
	stats->_numaNodes = 0;

	if (MM_CycleState::CT_GLOBAL_MARK_PHASE != env->_cycleState->_collectionType) {
		/* numaNodes is just used as indication to verbose GC that stats we collected are valid and indeed should be reported */
		stats->_numaNodes = _extensions->_numaManager.getAffinityLeaderCount();
		UDATA regionSize = _regionManager->getRegionSize();
		UDATA allocateEdenTotal = 0;
		stats->_edenHeapSize = getCurrentEdenSizeInBytes(env);

		GC_HeapRegionIteratorVLHGC regionIterator(_regionManager);
		MM_HeapRegionDescriptorVLHGC *region = NULL;
		while (NULL != (region = regionIterator.nextRegion())) {
			if (!region->isFreeOrIdle()) {
				/* Eden and NUMA stats */
				UDATA usedMemory = 0;
				if (region->containsObjects()) {
					MM_MemoryPool *memoryPool = region->getMemoryPool();
					Assert_MM_true(NULL != memoryPool);
					/* Eden region containing objects, Allocation Age must be smaller then amount allocated since last PGC,
					 * more accurately, its logical age must be equal to zero */
					if (0 == region->getLogicalAge()) {
						UDATA size = memoryPool->getActualFreeMemorySize();
						stats->_edenFreeHeapSize += size;
						usedMemory = regionSize - size;
						allocateEdenTotal += regionSize;
					} else {
						usedMemory = regionSize - memoryPool->getFreeMemoryAndDarkMatterBytes();
					}
				} else {
					Assert_MM_true(region->isArrayletLeaf());
					usedMemory = regionSize;
					if (0 == region->getLogicalAge()) {
						allocateEdenTotal += regionSize;
					}
				}

				if (env->getCommonAllocationContext() == region->_allocateData._owningContext) {
					stats->_commonNumaNodeBytes += usedMemory;
				} else if (NULL == region->_allocateData._originalOwningContext) {
					stats->_localNumaNodeBytes += usedMemory;
				} else {
					stats->_nonLocalNumaNodeBytes += usedMemory;
				}
			}
			if (region->isArrayletLeaf()) {
				J9IndexableObject *spine = region->_allocateData.getSpine();

				/* if we recently (end of GMP) unloaded classes, but have not done sweep yet (just about to do it),
				 * there might be unswept arraylet leaf regions, for which we must not try to access class data (for scan type).
				 * Therefore, we count these arraylets as 'unknown' type.
				 */
				if (classesPotentiallyUnloaded && !isMarked((J9Object *)spine)) {
					stats->_arrayletUnknownLeaves += 1;
					/* is this first arraylet leaf? */
					GC_SlotObject firstArrayletLeafSlot(_javaVM->omrVM, _extensions->indexableObjectModel.getArrayoidPointer(spine));
					if (region->getLowAddress() == firstArrayletLeafSlot.readReferenceFromSlot()) {
						stats->_arrayletUnknownObjects += 1;
					}
				} else if (GC_ObjectModel::SCAN_POINTER_ARRAY_OBJECT == _extensions->objectModel.getScanType((J9Object *)spine)) {
					stats->_arrayletReferenceLeaves += 1;
					/* is this first arraylet leaf? */
					GC_SlotObject firstArrayletLeafSlot(_javaVM->omrVM, _extensions->indexableObjectModel.getArrayoidPointer(spine));
					if (region->getLowAddress() == firstArrayletLeafSlot.readReferenceFromSlot()) {
						stats->_arrayletReferenceObjects += 1;
						UDATA numExternalArraylets = _extensions->indexableObjectModel.numExternalArraylets(spine);
						if (stats->_largestReferenceArraylet < numExternalArraylets) {
							stats->_largestReferenceArraylet = numExternalArraylets;
						}
					}
				} else {
					Assert_MM_true(GC_ObjectModel::SCAN_PRIMITIVE_ARRAY_OBJECT == _extensions->objectModel.getScanType((J9Object *)spine));
					stats->_arrayletPrimitiveLeaves += 1;
					GC_SlotObject firstArrayletLeafSlot(_javaVM->omrVM, _extensions->indexableObjectModel.getArrayoidPointer(spine));
					if (region->getLowAddress() == firstArrayletLeafSlot.readReferenceFromSlot()) {
						stats->_arrayletPrimitiveObjects += 1;
						UDATA numExternalArraylets = _extensions->indexableObjectModel.numExternalArraylets(spine);
						if (stats->_largestPrimitiveArraylet < numExternalArraylets) {
							stats->_largestPrimitiveArraylet = numExternalArraylets;
						}
					}
				}
			}

		}
		/* there would be a case that mutators use more than assigned regions, correct totalRegionEdenSize to avoid inconsistent Exception */
		if (allocateEdenTotal > stats->_edenHeapSize) {
			stats->_edenHeapSize = allocateEdenTotal;
		}
		stats->_edenFreeHeapSize += (stats->_edenHeapSize - allocateEdenTotal);
	}
}

void
MM_IncrementalGenerationalGC::reportGCIncrementStart(MM_EnvironmentBase *env, const char *incrementDescription, UDATA incrementCount)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CollectionStatisticsVLHGC *stats = (MM_CollectionStatisticsVLHGC *)env->_cycleState->_collectionStatistics;
	stats->collectCollectionStatistics(env, stats);
	stats->_incrementDescription = incrementDescription;
	stats->_incrementCount = incrementCount;
	/* TODO: we could find if we did any class unloading in last GMP and pass more precise info to exportStats */
	exportStats((MM_EnvironmentVLHGC *)env, stats, _schedulingDelegate.isGlobalSweepRequired());
	stats->_startTime = j9time_hires_clock();

	intptr_t rc = omrthread_get_process_times(&stats->_startProcessTimes);
	switch (rc){
	case -1: /* Error: Function un-implemented on architecture */
	case -2: /* Error: getrusage() or GetProcessTimes() returned error value */
		stats->_endProcessTimes._userTime = I_64_MAX;
		stats->_endProcessTimes._systemTime = I_64_MAX;
		break;
	case  0:
		break; /* Success */
	default:
		Assert_MM_unreachable();
	}

	TRIGGER_J9HOOK_MM_PRIVATE_GC_INCREMENT_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		stats->_startTime,
		J9HOOK_MM_PRIVATE_GC_INCREMENT_START,
		stats);
}

void
MM_IncrementalGenerationalGC::reportGCIncrementEnd(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_CollectionStatisticsVLHGC *stats = (MM_CollectionStatisticsVLHGC *)env->_cycleState->_collectionStatistics;
	stats->collectCollectionStatistics(env, stats);
	exportStats((MM_EnvironmentVLHGC *)env, stats);

	intptr_t rc = omrthread_get_process_times(&stats->_endProcessTimes);
	switch (rc){
	case -1: /* Error: Function un-implemented on architecture */
	case -2: /* Error: getrusage() or GetProcessTimes() returned error value */
		stats->_endProcessTimes._userTime = 0;
		stats->_endProcessTimes._systemTime = 0;
		break;
	case  0:
		break; /* Success */
	default:
		Assert_MM_unreachable();
	}

	stats->_endTime = j9time_hires_clock();
	stats->_stallTime = static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats.getTotalStallTime();
	
	TRIGGER_J9HOOK_MM_PRIVATE_GC_INCREMENT_END(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		stats->_endTime,
		J9HOOK_MM_PRIVATE_GC_INCREMENT_END,
		stats
	);
}

void
MM_IncrementalGenerationalGC::reportMarkStart(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_MarkStart(env->getLanguageVMThread());

	TRIGGER_J9HOOK_MM_PRIVATE_MARK_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_MARK_START);
}

void
MM_IncrementalGenerationalGC::reportMarkEnd(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_MarkEnd(env->getLanguageVMThread());

	TRIGGER_J9HOOK_MM_PRIVATE_MARK_END(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_MARK_END);
}

void
MM_IncrementalGenerationalGC::reportGMPMarkStart(MM_EnvironmentBase *env)
{
	reportMarkStart(env);

	TRIGGER_J9HOOK_MM_PRIVATE_GMP_MARK_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats);
}

void
MM_IncrementalGenerationalGC::reportGMPMarkEnd(MM_EnvironmentBase *env)
{
	reportMarkEnd(env);

	TRIGGER_J9HOOK_MM_PRIVATE_GMP_MARK_END(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats);
}

void
MM_IncrementalGenerationalGC::reportGlobalGCMarkStart(MM_EnvironmentBase *env)
{
	reportMarkStart(env);

	TRIGGER_J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats);
}

void
MM_IncrementalGenerationalGC::reportGlobalGCMarkEnd(MM_EnvironmentBase *env)
{
	reportMarkEnd(env);

	TRIGGER_J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_END(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats,
		&static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._workPacketStats);
}

void
MM_IncrementalGenerationalGC::collectorExpanded(MM_EnvironmentBase *envBase, MM_MemorySubSpace *subSpace, UDATA expandSize)
{
	MM_EnvironmentVLHGC* env = MM_EnvironmentVLHGC::getEnvironment(envBase);

	/* this even can only happen during a copy-forward PGC */
	Assert_MM_true(MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType);
	/* if _shouldRunCopyForward == false, it is copyforwardhybrid with all regions need to be marked/compacted, for this case we don't need to collectorExpanded. */
	Assert_MM_true(env->_cycleState->_shouldRunCopyForward);

	MM_Collector::collectorExpanded(env, subSpace, expandSize);
	
	MM_HeapResizeStats *resizeStats = _extensions->heap->getResizeStats();
	Assert_MM_true(SATISFY_COLLECTOR == resizeStats->getLastExpandReason());

	env->_copyForwardStats._heapExpandedCount += 1;
	env->_copyForwardStats._heapExpandedBytes += expandSize;
	env->_copyForwardStats._heapExpandedTime += resizeStats->getLastExpandTime();
}

void
MM_IncrementalGenerationalGC::postMarkMapCompletion(MM_EnvironmentVLHGC *env)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/* Unload classloaders which weren't reached by the end of mark */
	if (env->_cycleState->_dynamicClassUnloadingEnabled) {
		unloadDeadClassLoaders(env);
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

#if defined(J9VM_GC_FINALIZATION)
   /* Alert the finalizer if work needs to be done */
	if(env->_cycleState->_finalizationRequired) {
		omrthread_monitor_enter(_javaVM->finalizeMainMonitor);
		_javaVM->finalizeMainFlags |= J9_FINALIZE_FLAGS_MAIN_WAKE_UP;
		omrthread_monitor_notify_all(_javaVM->finalizeMainMonitor);
		omrthread_monitor_exit(_javaVM->finalizeMainMonitor);
	}
#endif /* J9VM_GC_FINALIZATION */
}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
void
MM_IncrementalGenerationalGC::unloadDeadClassLoaders(MM_EnvironmentVLHGC *env)
{
	Trc_MM_IncrementalGenerationalGC_unloadDeadClassLoaders_entry(env->getLanguageVMThread());
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_ClassUnloadStats *classUnloadStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._classUnloadStats;
	/* it's not safe to read or write the classLoader->gcFlags unless we're in a class unloading cycle */
	Assert_MM_true(env->_cycleState->_dynamicClassUnloadingEnabled);

	/* set the vmState whilst we're unloading classes */
	UDATA vmState = env->pushVMstate(OMRVMSTATE_GC_CLEANING_METADATA);
	/* we are going to report that we started unloading, even though we might decide that there is no work to do */
	reportClassUnloadingStart(env);
	classUnloadStats->_startTime = j9time_hires_clock();

	/* Count the classes we're unloading and perform class-specific clean up work for each unloading class.
	 * If we're unloading any classes, perform common class-unloading clean up.
	 */
	classUnloadStats->_startSetupTime = j9time_hires_clock();
	J9ClassLoader *classLoadersUnloadedList = _extensions->classLoaderManager->identifyClassLoadersToUnload(env, env->_cycleState->_markMap, classUnloadStats);
	_extensions->classLoaderManager->cleanUpClassLoadersStart(env, classLoadersUnloadedList, env->_cycleState->_markMap, classUnloadStats);
	classUnloadStats->_endSetupTime = j9time_hires_clock();
	if (0 < (classUnloadStats->_classesUnloadedCount + classUnloadStats->_classLoaderUnloadedCount)) {
		U_64 quiesceTime = _extensions->classLoaderManager->enterClassUnloadMutex(env);
		classUnloadStats->_classUnloadMutexQuiesceTime = quiesceTime;

		classUnloadStats->_startScanTime = j9time_hires_clock();

		/* The list of classLoaders to be unloaded by cleanUpClassLoadersEnd is rooted in unloadLink */
		J9ClassLoader *unloadLink = NULL;
		J9MemorySegment *reclaimedSegments = NULL;
		_extensions->classLoaderManager->cleanUpClassLoaders(env, classLoadersUnloadedList, &reclaimedSegments, &unloadLink, &env->_cycleState->_finalizationRequired);

		/* Free the class memory segments associated with dead classLoaders, unload (free) the dead classLoaders that don't
		 * require finalization, and perform any final clean up after the dead classLoaders are gone.
		 */
		classUnloadStats->_endScanTime = j9time_hires_clock();
		classUnloadStats->_startPostTime = classUnloadStats->_endScanTime;
		/* enqueue all the segments we just salvaged from the dead class loaders for delayed free (this work was historically attributed in the unload end operation so it goes after the timer start) */
		_extensions->classLoaderManager->enqueueUndeadClassSegments(reclaimedSegments);
		_extensions->classLoaderManager->cleanUpClassLoadersEnd(env, unloadLink);
		/* we can now flush these since we don't need to walk any dead objects in Balanced */
		if (_extensions->classLoaderManager->reclaimableMemory() > 0) {
			Trc_MM_FlushUndeadSegments_Entry(env->getLanguageVMThread(), "Mark Map Completed");
			_extensions->classLoaderManager->flushUndeadSegments(env);
			Trc_MM_FlushUndeadSegments_Exit(env->getLanguageVMThread());
		}
		classUnloadStats->_endPostTime = j9time_hires_clock();

		_extensions->classLoaderManager->exitClassUnloadMutex(env);
	}
	/* If there was dynamic class unloading checks during the run, record the new number of class loaders last seen during a DCU pass */
	_extensions->classLoaderManager->setLastUnloadNumOfClassLoaders();
	_extensions->classLoaderManager->setLastUnloadNumOfAnonymousClasses();

	classUnloadStats->_endTime = j9time_hires_clock();
	reportClassUnloadingEnd(env);
	env->popVMstate(vmState);

	Trc_MM_IncrementalGenerationalGC_unloadDeadClassLoaders_exit(env->getLanguageVMThread());
}

void
MM_IncrementalGenerationalGC::reportClassUnloadingStart(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	Trc_MM_ClassUnloadingStart(env->getLanguageVMThread());

	TRIGGER_J9HOOK_MM_PRIVATE_CLASS_UNLOADING_START(
		_extensions->privateHookInterface,
		env->getOmrVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_PRIVATE_CLASS_UNLOADING_START);
}

void
MM_IncrementalGenerationalGC::reportClassUnloadingEnd(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_ClassUnloadStats *classUnloadStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._classUnloadStats;

	Trc_MM_ClassUnloadingEnd(env->getLanguageVMThread(),
		classUnloadStats->_classLoaderUnloadedCount,
		classUnloadStats->_classesUnloadedCount);

	TRIGGER_J9HOOK_MM_CLASS_UNLOADING_END(
		_extensions->hookInterface,
		(J9VMThread *)env->getLanguageVMThread(),
		j9time_hires_clock(),
		J9HOOK_MM_CLASS_UNLOADING_END,
		classUnloadStats->_endTime - classUnloadStats->_startTime,
		classUnloadStats->_classLoaderUnloadedCount,
		classUnloadStats->_classesUnloadedCount,
		classUnloadStats->_classUnloadMutexQuiesceTime,
		classUnloadStats->_endSetupTime - classUnloadStats->_startSetupTime,
		classUnloadStats->_endScanTime - classUnloadStats->_startScanTime,
		classUnloadStats->_endPostTime - classUnloadStats->_startPostTime);
}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

UDATA
MM_IncrementalGenerationalGC::getBytesScannedInGlobalMarkPhase()
{
	UDATA bytesScanned = 0;
	if (isGlobalMarkPhaseRunning()) {
		bytesScanned = _persistentGlobalMarkPhaseState._vlhgcCycleStats._markStats._bytesScanned;
	}
	return bytesScanned;
}

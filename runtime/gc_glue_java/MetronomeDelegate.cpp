/*******************************************************************************
 * Copyright (c) 2019, 2021 IBM Corp. and others
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

#include "MetronomeDelegate.hpp"

#if defined(J9VM_GC_REALTIME)

#include "omr.h"

#include "ClassHeapIterator.hpp"
#include "ClassLoaderIterator.hpp"
#include "ClassLoaderLinkedListIterator.hpp"
#include "ClassLoaderManager.hpp"
#include "ClassLoaderSegmentIterator.hpp"
#include "EnvironmentRealtime.hpp"
#include "FinalizableClassLoaderBuffer.hpp"
#include "FinalizableObjectBuffer.hpp"
#include "FinalizableReferenceBuffer.hpp"
#include "FinalizeListManager.hpp"
#include "FinalizerSupport.hpp"
#include "GCExtensionsBase.hpp"
#include "Heap.hpp"
#include "HeapRegionDescriptorRealtime.hpp"
#include "MetronomeAlarmThread.hpp"
#include "JNICriticalRegion.hpp"
#include "OwnableSynchronizerObjectBufferRealtime.hpp"
#include "OwnableSynchronizerObjectList.hpp"
#include "RealtimeAccessBarrier.hpp"
#include "RealtimeGC.hpp"
#include "RealtimeMarkingScheme.hpp"
#include "RealtimeMarkingSchemeRootMarker.hpp"
#include "RealtimeMarkingSchemeRootClearer.hpp"
#include "RealtimeMarkTask.hpp"
#include "RealtimeRootScanner.hpp"
#include "ReferenceObjectBufferRealtime.hpp"
#include "ReferenceObjectList.hpp"
#include "Scheduler.hpp"
#include "UnfinalizedObjectBufferRealtime.hpp"
#include "UnfinalizedObjectList.hpp"

void
MM_MetronomeDelegate::yieldWhenRequested(MM_EnvironmentBase *env)
{
	MM_GCExtensionsBase *ext = env->getExtensions();
	UDATA accessMask;
	MM_Scheduler *sched = (MM_Scheduler *)ext->dispatcher;
	if (sched->_mode != MM_Scheduler::MUTATOR) {
		MM_JNICriticalRegion::releaseAccess((J9VMThread *)env->getOmrVMThread()->_language_vmthread, &accessMask);
		while (sched->_mode != MM_Scheduler::MUTATOR) {	
			omrthread_sleep(10);
		}
		MM_JNICriticalRegion::reacquireAccess((J9VMThread *)env->getOmrVMThread()->_language_vmthread, accessMask);
	}
}

/**
 * C entrypoint for the newly created alarm thread.
 */
int J9THREAD_PROC
MM_MetronomeDelegate::metronomeAlarmThreadWrapper(void* userData)
{
	MM_MetronomeAlarmThread *alarmThread = (MM_MetronomeAlarmThread *)userData;
	J9JavaVM *javaVM = (J9JavaVM *)alarmThread->getScheduler()->_extensions->getOmrVM()->_language_vm;
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	uintptr_t rc;

	j9sig_protect(MM_MetronomeDelegate::signalProtectedFunction, (void*)userData,
		javaVM->internalVMFunctions->structuredSignalHandlerVM, javaVM,
		J9PORT_SIG_FLAG_SIGALLSYNC | J9PORT_SIG_FLAG_MAY_CONTINUE_EXECUTION,
		&rc);

	omrthread_monitor_enter(alarmThread->_mutex);
	alarmThread->_alarmThreadActive = MM_MetronomeAlarmThread::ALARM_THREAD_SHUTDOWN;
	omrthread_monitor_notify(alarmThread->_mutex);
	omrthread_exit(alarmThread->_mutex);

	return 0;
}

uintptr_t
MM_MetronomeDelegate::signalProtectedFunction(J9PortLibrary *privatePortLibrary, void* userData)
{
	MM_MetronomeAlarmThread *alarmThread = (MM_MetronomeAlarmThread *)userData;
	J9JavaVM *javaVM = (J9JavaVM *)alarmThread->getScheduler()->_extensions->getOmrVM()->_language_vm;
	J9VMThread *vmThread = NULL;
	MM_EnvironmentRealtime *env = NULL;
	
	if (JNI_OK != (javaVM->internalVMFunctions->attachSystemDaemonThread(javaVM, &vmThread, "GC Alarm"))) {
		return 0;
	}
	
	env = MM_EnvironmentRealtime::getEnvironment(vmThread->omrVMThread);
	
	alarmThread->run(env);
	
	javaVM->internalVMFunctions->DetachCurrentThread((JavaVM*)javaVM);
	
	return 0;
}

void
MM_MetronomeDelegate::clearGCStats()
{
	_extensions->markJavaStats.clear();
}

void
MM_MetronomeDelegate::clearGCStatsEnvironment(MM_EnvironmentRealtime *env)
{
	env->_markStats.clear();
	env->getGCEnvironment()->_markJavaStats.clear();
	env->_workPacketStats.clear();
}

void
MM_MetronomeDelegate::mergeGCStats(MM_EnvironmentRealtime *env)
{
	GC_Environment *gcEnv = env->getGCEnvironment();

	MM_GlobalGCStats *finalGCStats= &_extensions->globalGCStats;
	finalGCStats->markStats.merge(&env->_markStats);
	_extensions->markJavaStats.merge(&gcEnv->_markJavaStats);
	finalGCStats->workPacketStats.merge(&env->_workPacketStats);
}

uintptr_t
MM_MetronomeDelegate::getSplitArraysProcessed(MM_EnvironmentRealtime *env)
{
	GC_Environment *gcEnv = env->getGCEnvironment();
	return gcEnv->_markJavaStats.splitArraysProcessed;
}

bool
MM_MetronomeDelegate::initialize(MM_EnvironmentBase *env)
{
	_scheduler = _realtimeGC->_sched;
	_markingScheme = _realtimeGC->getMarkingScheme();

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	_unmarkedImpliesClasses = false;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
	_unmarkedImpliesCleared = false;
	_unmarkedImpliesStringsCleared = false;

	/* allocate and initialize the global reference object lists */
	if (!allocateAndInitializeReferenceObjectLists(env)){
		return false;
	}

	/* allocate and initialize the global unfinalized object lists */
	if (!allocateAndInitializeUnfinalizedObjectLists(env)) {
		return false;
	}

	/* allocate and initialize the global ownable synchronizer object lists */
	if (!allocateAndInitializeOwnableSynchronizerObjectLists(env)) {
		return false;
	}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if (!_extensions->dynamicClassUnloadingThresholdForced) {
		_extensions->dynamicClassUnloadingThreshold = 1;
	}
	if (!_extensions->dynamicClassUnloadingKickoffThresholdForced) {
		_extensions->dynamicClassUnloadingKickoffThreshold = 0;
	}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

	/* Create the appropriate access barrier for Metronome */
	MM_RealtimeAccessBarrier *accessBarrier = NULL;
	accessBarrier = allocateAccessBarrier(env);
	if (NULL == accessBarrier) {
		return false;
	}
	MM_GCExtensions::getExtensions(_javaVM)->accessBarrier = (MM_ObjectAccessBarrier *)accessBarrier;

	_javaVM->realtimeHeapMapBasePageRounded = _markingScheme->_markMap->getHeapMapBaseRegionRounded();
	_javaVM->realtimeHeapMapBits = _markingScheme->_markMap->getHeapMapBits();

	return true;
}

bool
MM_MetronomeDelegate::allocateAndInitializeReferenceObjectLists(MM_EnvironmentBase *env)
{
	const UDATA listCount = getReferenceObjectListCount(env);
	Assert_MM_true(0 < listCount);
	_extensions->referenceObjectLists = (MM_ReferenceObjectList *)env->getForge()->allocate((sizeof(MM_ReferenceObjectList) * listCount), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (NULL == _extensions->referenceObjectLists) {
		return false;
	}
	for (UDATA index = 0; index < listCount; index++) {
		new(&_extensions->referenceObjectLists[index]) MM_ReferenceObjectList();
	}
	return true;
}

bool
MM_MetronomeDelegate::allocateAndInitializeUnfinalizedObjectLists(MM_EnvironmentBase *env)
{
	const UDATA listCount = getUnfinalizedObjectListCount(env);
	Assert_MM_true(0 < listCount);
	MM_UnfinalizedObjectList *unfinalizedObjectLists = (MM_UnfinalizedObjectList *)env->getForge()->allocate((sizeof(MM_UnfinalizedObjectList) * listCount), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (NULL == unfinalizedObjectLists) {
		return false;
	}
	for (UDATA index = 0; index < listCount; index++) {
		new(&unfinalizedObjectLists[index]) MM_UnfinalizedObjectList();
		/* add each list to the global list. we need to maintain the doubly linked list
		 * to ensure uniformity with SE/Balanced.
		 */
		MM_UnfinalizedObjectList *previousUnfinalizedObjectList = (0 == index) ? NULL : &unfinalizedObjectLists[index-1];
		MM_UnfinalizedObjectList *nextUnfinalizedObjectList = ((listCount - 1) == index) ? NULL : &unfinalizedObjectLists[index+1];

		unfinalizedObjectLists[index].setNextList(nextUnfinalizedObjectList);
		unfinalizedObjectLists[index].setPreviousList(previousUnfinalizedObjectList);
	}
	_extensions->unfinalizedObjectLists = unfinalizedObjectLists;
	return true;
}

bool
MM_MetronomeDelegate::allocateAndInitializeOwnableSynchronizerObjectLists(MM_EnvironmentBase *env)
{
	const UDATA listCount = getOwnableSynchronizerObjectListCount(env);
	Assert_MM_true(0 < listCount);
	MM_OwnableSynchronizerObjectList *ownableSynchronizerObjectLists = (MM_OwnableSynchronizerObjectList *)env->getForge()->allocate((sizeof(MM_OwnableSynchronizerObjectList) * listCount), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (NULL == ownableSynchronizerObjectLists) {
		return false;
	}
	for (UDATA index = 0; index < listCount; index++) {
		new(&ownableSynchronizerObjectLists[index]) MM_OwnableSynchronizerObjectList();
		/* add each list to the global list. we need to maintain the doubly linked list
		 * to ensure uniformity with SE/Balanced.
		 */
		MM_OwnableSynchronizerObjectList *previousOwnableSynchronizerObjectList = (0 == index) ? NULL : &ownableSynchronizerObjectLists[index-1];
		MM_OwnableSynchronizerObjectList *nextOwnableSynchronizerObjectList = ((listCount - 1) == index) ? NULL : &ownableSynchronizerObjectLists[index+1];

		ownableSynchronizerObjectLists[index].setNextList(nextOwnableSynchronizerObjectList);
		ownableSynchronizerObjectLists[index].setPreviousList(previousOwnableSynchronizerObjectList);
	}
	_extensions->setOwnableSynchronizerObjectLists(ownableSynchronizerObjectLists);
	return true;
}

void
MM_MetronomeDelegate::tearDown(MM_EnvironmentBase *env)
{
	if (NULL != _extensions->referenceObjectLists) {
		env->getForge()->free(_extensions->referenceObjectLists);
		_extensions->referenceObjectLists = NULL;
	}

	if (NULL != _extensions->unfinalizedObjectLists) {
		env->getForge()->free(_extensions->unfinalizedObjectLists);
		_extensions->unfinalizedObjectLists = NULL;
	}

	if (NULL != _extensions->getOwnableSynchronizerObjectLists()) {
		env->getForge()->free(_extensions->getOwnableSynchronizerObjectLists());
		_extensions->setOwnableSynchronizerObjectLists(NULL);
	}
	
	if (NULL != _extensions->accessBarrier) {
		_extensions->accessBarrier->kill(env);
		_extensions->accessBarrier = NULL;
	}

	_javaVM->realtimeHeapMapBits = NULL;
}

void
MM_MetronomeDelegate::mainSetupForGC(MM_EnvironmentBase *env)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/* Set the dynamic class unloading flag based on command line and runtime state */
	switch (_extensions->dynamicClassUnloading) {
		case MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_NEVER:
			_extensions->runtimeCheckDynamicClassUnloading = false;
			break;
		case MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_ALWAYS:
			_extensions->runtimeCheckDynamicClassUnloading = true;
			break;
		case MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_ON_CLASS_LOADER_CHANGES:
			 _extensions->runtimeCheckDynamicClassUnloading = (_extensions->aggressive || _extensions->classLoaderManager->isTimeForClassUnloading(env));
			break;
		default:
			break;
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

#if defined(J9VM_GC_FINALIZATION)
	_finalizationRequired = false;
#endif /* J9VM_GC_FINALIZATION */
}

void
MM_MetronomeDelegate::mainCleanupAfterGC(MM_EnvironmentBase *env)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/* flush the dead class segments if their size exceeds the CacheSize mark.
	 * Heap fixup should have been completed in this cycle.
	 */
	if (_extensions->classLoaderManager->reclaimableMemory() > _extensions->deadClassLoaderCacheSize) {
		Trc_MM_FlushUndeadSegments_Entry(env->getLanguageVMThread(), "Non-zero reclaimable memory available");
		_extensions->classLoaderManager->flushUndeadSegments(env);
		Trc_MM_FlushUndeadSegments_Exit(env->getLanguageVMThread());
	}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

void
MM_MetronomeDelegate::incrementalCollectStart(MM_EnvironmentRealtime *env)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	_dynamicClassUnloadingEnabled = ((_extensions->runtimeCheckDynamicClassUnloading != 0) ? true : false);
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

void
MM_MetronomeDelegate::incrementalCollect(MM_EnvironmentRealtime *env)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	_dynamicClassUnloadingEnabled = ((_extensions->runtimeCheckDynamicClassUnloading != 0) ? true : false);

	if (_extensions->runtimeCheckDynamicClassUnloading != 0) {
		MM_ClassUnloadStats *classUnloadStats = &_extensions->globalGCStats.classUnloadStats;
		_realtimeGC->setCollectorUnloadingClassLoaders();
		reportClassUnloadingStart(env);
		classUnloadStats->_startTime = j9time_hires_clock();
		unloadDeadClassLoaders(env);
		classUnloadStats->_endTime = j9time_hires_clock();
		reportClassUnloadingEnd(env);
		
		/* If there was dynamic class unloading checks during the run, record the new number of class
		 * loaders last seen during a DCU pass
		 */
		_extensions->classLoaderManager->setLastUnloadNumOfClassLoaders();		
		_extensions->classLoaderManager->setLastUnloadNumOfAnonymousClasses();
	}
	
	/* Handling of classes done. Return back to "mark if necessary" mode */
	_unmarkedImpliesClasses = false;

	/* Clear the appropriate flags of all classLoaders */
	GC_ClassLoaderIterator classLoaderIterator(_javaVM->classLoaderBlocks);
	J9ClassLoader *classLoader;
	while((classLoader = classLoaderIterator.nextSlot()) != NULL) {
		classLoader->gcFlags &= ~J9_GC_CLASS_LOADER_SCANNED;
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

	/* If the J9VM_DEBUG_ATTRIBUTE_ALLOW_USER_HEAP_WALK flag is set,
	 * or if we are about to unload classes and free class memory segments
	 * then fix the heap so that it can be walked by debugging tools
	 */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	bool fixupForClassUnload = (_extensions->classLoaderManager->reclaimableMemory() > _extensions->deadClassLoaderCacheSize);
#else /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
	bool fixupForClassUnload = false;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
	if (J9VM_DEBUG_ATTRIBUTE_ALLOW_USER_HEAP_WALK == (((J9JavaVM *)env->getLanguageVM())->requiredDebugAttributes & J9VM_DEBUG_ATTRIBUTE_ALLOW_USER_HEAP_WALK)
			|| fixupForClassUnload) {
		_realtimeGC->_fixHeapForWalk = true;
	}
}

void
MM_MetronomeDelegate::doAuxiliaryGCWork(MM_EnvironmentBase *env)
{
#if defined(J9VM_GC_FINALIZATION)
	if(isFinalizationRequired()) {
		omrthread_monitor_enter(_javaVM->finalizeMainMonitor);
		_javaVM->finalizeMainFlags |= J9_FINALIZE_FLAGS_MAIN_WAKE_UP;
		omrthread_monitor_notify_all(_javaVM->finalizeMainMonitor);
		omrthread_monitor_exit(_javaVM->finalizeMainMonitor);
	}
#endif /* J9VM_GC_FINALIZATION */
}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
void
MM_MetronomeDelegate::processDyingClasses(MM_EnvironmentRealtime *env, UDATA* classUnloadCountResult, UDATA* anonymousClassUnloadCountResult, UDATA* classLoaderUnloadCountResult, J9ClassLoader** classLoaderUnloadListResult)
{
	J9ClassLoader *classLoader = NULL;
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	UDATA classUnloadCount = 0;
	UDATA anonymousClassUnloadCount = 0;
	UDATA classLoaderUnloadCount = 0;
	J9ClassLoader *unloadLink = NULL;
	J9Class *classUnloadList = NULL;
	J9Class *anonymousClassUnloadList = NULL;

	/*
	 * Verify that boolean array class has been marked. Assertion is done to ensure correctness
	 * of an optimization in ClassIteratorClassSlots that only checks booleanArrayClass Interfaces
	 * since all array claseses share the same ITable.
	 */
	Assert_MM_true(_markingScheme->isMarked(_javaVM->booleanArrayClass->classObject));

	/*
	 * Walk anonymous classes and set unmarked as dying
	 *
	 * Do this walk before classloaders to be unloaded walk to create list of anonymous classes to be unloaded and use it
	 * as sublist to continue to build general list of classes to be unloaded
	 *
	 * Anonymous classes suppose to be allocated one per segment
	 * This is not relevant here however becomes important at segment removal time
	 */
	anonymousClassUnloadList = addDyingClassesToList(env, _javaVM->anonClassLoader, false, anonymousClassUnloadList, &anonymousClassUnloadCount);

	/* class unload list includes anonymous class unload list */
	classUnloadList = anonymousClassUnloadList;
	classUnloadCount += anonymousClassUnloadCount;
	
	GC_ClassLoaderLinkedListIterator classLoaderIterator(env, _extensions->classLoaderManager);
	while(NULL != (classLoader = (J9ClassLoader *)classLoaderIterator.nextSlot())) {
		if (0 == (classLoader->gcFlags & J9_GC_CLASS_LOADER_DEAD)) {
			Assert_MM_true(NULL == classLoader->unloadLink);
			if(_markingScheme->isMarked(classLoader->classLoaderObject) ) {
				classLoader->gcFlags &= ~J9_GC_CLASS_LOADER_SCANNED;
			} else {
				/* Anonymous classloader should not be unloaded */
				Assert_MM_true(0 == (classLoader->flags & J9CLASSLOADER_ANON_CLASS_LOADER));

				classLoaderUnloadCount += 1;
				classLoader->gcFlags |= J9_GC_CLASS_LOADER_DEAD;
				
				/* add this loader to the linked list of loaders being unloaded in this cycle */
				classLoader->unloadLink = unloadLink;
				unloadLink = classLoader;

				classUnloadList = addDyingClassesToList(env, classLoader, true, classUnloadList, &classUnloadCount);
			}
		}
		yieldFromClassUnloading(env);
	}
	
	if (0 != classUnloadCount) {
		/* Call classes unload hook */
		TRIGGER_J9HOOK_VM_CLASSES_UNLOAD(_javaVM->hookInterface, vmThread, classUnloadCount, classUnloadList);
		yieldFromClassUnloading(env);
	}
	
	if (0 != anonymousClassUnloadCount) {
		/* Call anonymous classes unload hook */
		TRIGGER_J9HOOK_VM_ANON_CLASSES_UNLOAD(_javaVM->hookInterface, vmThread, anonymousClassUnloadCount, anonymousClassUnloadList);
		yieldFromClassUnloading(env);
	}

	if (0 != classLoaderUnloadCount) {
		/* Call classloader unload hook */
		TRIGGER_J9HOOK_VM_CLASS_LOADERS_UNLOAD(_javaVM->hookInterface, vmThread, unloadLink);
		yieldFromClassUnloading(env);
	}

	/* Ensure that the VM has an accurate anonymous class count */
	_javaVM->anonClassCount -= anonymousClassUnloadCount;

	*classUnloadCountResult = classUnloadCount;
	*anonymousClassUnloadCountResult = anonymousClassUnloadCount;
	*classLoaderUnloadCountResult = classLoaderUnloadCount;
	*classLoaderUnloadListResult = unloadLink;
}

J9Class *
MM_MetronomeDelegate::addDyingClassesToList(MM_EnvironmentRealtime *env, J9ClassLoader * classLoader, bool setAll, J9Class *classUnloadListStart, UDATA *classUnloadCountResult)
{
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	J9Class *classUnloadList = classUnloadListStart;
	UDATA classUnloadCount = 0;

	if (NULL != classLoader) {
		GC_ClassLoaderSegmentIterator segmentIterator(classLoader, MEMORY_TYPE_RAM_CLASS);
		J9MemorySegment *segment = NULL;
		while(NULL != (segment = segmentIterator.nextSegment())) {
			GC_ClassHeapIterator classHeapIterator(_javaVM, segment);
			J9Class *clazz = NULL;
			while(NULL != (clazz = classHeapIterator.nextClass())) {

				J9CLASS_EXTENDED_FLAGS_CLEAR(clazz, J9ClassGCScanned);

				J9Object *classObject = clazz->classObject;
				if (setAll || !_markingScheme->isMarked(classObject)) {

					/* with setAll all classes must be unmarked */
					Assert_MM_true(!_markingScheme->isMarked(classObject));

					classUnloadCount += 1;

					/* Remove the class from the subclass traversal list */
					_extensions->classLoaderManager->removeFromSubclassHierarchy(env, clazz);
					/* Mark class as dying */
					clazz->classDepthAndFlags |= J9AccClassDying;

					/* Call class unload hook */
					Trc_MM_cleanUpClassLoadersStart_triggerClassUnload(env->getLanguageVMThread(),clazz,
								(UDATA) J9UTF8_LENGTH(J9ROMCLASS_CLASSNAME(clazz->romClass)),
								J9UTF8_DATA(J9ROMCLASS_CLASSNAME(clazz->romClass)));
					TRIGGER_J9HOOK_VM_CLASS_UNLOAD(_javaVM->hookInterface, vmThread, clazz);
						
					/* add class to dying anonymous classes link list */
					clazz->gcLink = classUnloadList;
					classUnloadList = clazz;
				}
			}
		}
	}

	*classUnloadCountResult += classUnloadCount;
	return classUnloadList;
}

/**
 * Free classloaders which are being unloaded during this GC cycle.  Also remove all
 * dead classes from the traversal list.
 * @note the traversal code belongs in its own function or possibly processDyingClasses
 * or possible processDyingClasses.  It is currently here for historic reasons.
 * 
 * @param deadClassLoaders Linked list of classloaders dying during this GC cycle
 */
void
MM_MetronomeDelegate::processUnlinkedClassLoaders(MM_EnvironmentBase *envModron, J9ClassLoader *deadClassLoaders)
{
	MM_EnvironmentRealtime *env = MM_EnvironmentRealtime::getEnvironment(envModron);
	J9ClassLoader *unloadLink = deadClassLoaders;
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	J9JavaVM *javaVM = (J9JavaVM *)env->getLanguageVM();

	/* Remove dead classes from the traversal list (if necessary) */
	J9Class *jlObject = J9VMJAVALANGOBJECT_OR_NULL(javaVM);
	J9Class *previousClass = jlObject;
	J9Class *nextClass = (NULL != jlObject) ? jlObject->subclassTraversalLink : jlObject;
	while ((NULL != nextClass) && (jlObject != nextClass)) {
		if (J9CLASS_FLAGS(nextClass) & J9AccClassDying) {
			while ((NULL != nextClass->subclassTraversalLink) && (jlObject != nextClass) && (J9CLASS_FLAGS(nextClass) & 0x08000000)) {
				nextClass = nextClass->subclassTraversalLink;
			}
			previousClass->subclassTraversalLink = nextClass;
		}
		previousClass = nextClass;
		nextClass = nextClass->subclassTraversalLink;
		/* TODO CRGTMP Do we need to yield here?  Is yielding safe? */
	}

	/* Free memory for dead classloaders */
	while (NULL != unloadLink) {
		J9ClassLoader *nextUnloadLink = unloadLink->unloadLink;
		_javaVM->internalVMFunctions->freeClassLoader(unloadLink, _javaVM, vmThread, 1);
		unloadLink = nextUnloadLink;
		yieldFromClassUnloading(env);
	}
}

void
MM_MetronomeDelegate::updateClassUnloadStats(MM_EnvironmentBase *env, UDATA classUnloadCount, UDATA anonymousClassUnloadCount, UDATA classLoaderUnloadCount)
{
	MM_ClassUnloadStats *classUnloadStats = &_extensions->globalGCStats.classUnloadStats;

	/* TODO CRGTMP move global stats into super class implementation once it is created */
	classUnloadStats->_classesUnloadedCount = classUnloadCount;
	classUnloadStats->_anonymousClassesUnloadedCount = anonymousClassUnloadCount;
	classUnloadStats->_classLoaderUnloadedCount = classLoaderUnloadCount;

	/* Record increment stats */
	_extensions->globalGCStats.metronomeStats.classesUnloadedCount = classUnloadCount;
	_extensions->globalGCStats.metronomeStats.anonymousClassesUnloadedCount = anonymousClassUnloadCount;
	_extensions->globalGCStats.metronomeStats.classLoaderUnloadedCount = classLoaderUnloadCount;
}

/**
 * Unload classcloaders that are no longer referenced.  If the classloader has shared
 * libraries open place it on the finalize queue instead of freeing it.
 * 
 */
void
MM_MetronomeDelegate::unloadDeadClassLoaders(MM_EnvironmentBase *envModron)
{
	MM_EnvironmentRealtime *env = MM_EnvironmentRealtime::getEnvironment(envModron);
	J9ClassLoader *unloadLink = NULL;
	UDATA classUnloadCount = 0;
	UDATA anonymousClassUnloadCount = 0;
	UDATA classLoaderUnloadCount = 0;
	J9ClassLoader *classLoadersUnloadedList = NULL;
	J9MemorySegment *reclaimedSegments = NULL;

	/* set the vmState whilst we're unloading classes */
	UDATA vmState = env->pushVMstate(OMRVMSTATE_GC_CLEANING_METADATA);
	
	lockClassUnloadMonitor(env);
	
	processDyingClasses(env, &classUnloadCount, &anonymousClassUnloadCount, &classLoaderUnloadCount, &classLoadersUnloadedList);

	/* cleanup segments in anonymous classloader */
	_extensions->classLoaderManager->cleanUpSegmentsInAnonymousClassLoader(env, &reclaimedSegments);
	
	/* enqueue all the segments we just salvaged from the dead class loaders for delayed free (this work was historically attributed in the unload end operation so it goes after the timer start) */
	_extensions->classLoaderManager->enqueueUndeadClassSegments(reclaimedSegments);

	yieldFromClassUnloading(env);

	GC_FinalizableClassLoaderBuffer buffer(_extensions);
	
	while (NULL != classLoadersUnloadedList) {
		/* fetch the next loader immediately, since we will re-use the unloadLink in this loop */
		J9ClassLoader* classLoader = classLoadersUnloadedList;
		classLoadersUnloadedList = classLoader->unloadLink;
		
		Assert_MM_true(0 == (classLoader->gcFlags & J9_GC_CLASS_LOADER_SCANNED));
		Assert_MM_true(J9_GC_CLASS_LOADER_DEAD == (classLoader->gcFlags & J9_GC_CLASS_LOADER_DEAD));
		Assert_MM_true(0 == (classLoader->gcFlags & (J9_GC_CLASS_LOADER_UNLOADING | J9_GC_CLASS_LOADER_ENQ_UNLOAD)));

		/* Class loader died this collection, so do cleanup work */
		reclaimedSegments = NULL;
		
		/* Perform classLoader-specific clean up work, including freeing the classLoader's class hash table and
		 * class path entries.
		 */
		 _javaVM->internalVMFunctions->cleanUpClassLoader((J9VMThread *)env->getLanguageVMThread(), classLoader);
		 /* free any ROM classes now and enqueue any RAM classes */
		 _extensions->classLoaderManager->cleanUpSegmentsAlongClassLoaderLink(_javaVM, classLoader->classSegments, &reclaimedSegments);
		 /* we are taking responsibility for cleaning these here so free them */
		 classLoader->classSegments = NULL;
		 /* enqueue all the segments we just salvaged from the dead class loaders for delayed free (this work was historically attributed in the unload end operation so it goes after the timer start) */
		 _extensions->classLoaderManager->enqueueUndeadClassSegments(reclaimedSegments);

		 /* Remove this classloader slot */
		 _extensions->classLoaderManager->unlinkClassLoader(classLoader);

#if defined(J9VM_GC_FINALIZATION)
		/* Determine if the classLoader needs to be enqueued for finalization (for shared library unloading),
		 * otherwise add it to the list of classLoaders to be unloaded by cleanUpClassLoadersEnd.
		 */
		if(((NULL != classLoader->sharedLibraries)
		&& (0 != pool_numElements(classLoader->sharedLibraries)))
		|| (_extensions->fvtest_forceFinalizeClassLoaders)) {
			/* Attempt to enqueue the class loader for the finalizer */
			buffer.add(env, classLoader);
			classLoader->gcFlags |= J9_GC_CLASS_LOADER_ENQ_UNLOAD;
			_finalizationRequired = true;
		} else
#endif /* J9VM_GC_FINALIZATION */
		{
			/* Add the classLoader to the list of classLoaders to unloaded by cleanUpClassLoadersEnd */
			classLoader->unloadLink = unloadLink;
			unloadLink = classLoader;
		}
		yieldFromClassUnloading(env);
	}
	
	buffer.flush(env);

	updateClassUnloadStats(env, classUnloadCount, anonymousClassUnloadCount, classLoaderUnloadCount);
	
	processUnlinkedClassLoaders(env, unloadLink);
	
	unlockClassUnloadMonitor(env);
	
	env->popVMstate(vmState);
}

/**
 * Check to see if it is time to yield.  If it is time to yield the GC
 * must release the classUnloadMonitor before yielding.  Once the GC
 * comes back from the yield it is required to acquire the classUnloadMonitor
 * again.
 */
void
MM_MetronomeDelegate::yieldFromClassUnloading(MM_EnvironmentRealtime *env)
{
	if (_realtimeGC->shouldYield(env)) {
		unlockClassUnloadMonitor(env);
		_realtimeGC->yield(env);
		lockClassUnloadMonitor(env);
	}
}

/**
 * The GC is required to hold the classUnloadMonitor while it is unloading classes.
 * This will ensure that the JIT will abort and ongoing compilations
 */
void
MM_MetronomeDelegate::lockClassUnloadMonitor(MM_EnvironmentRealtime *env)
{
	/* Grab the classUnloadMonitor so that the JIT and the GC will not interfere with each other */
#if defined(J9VM_JIT_CLASS_UNLOAD_RWMONITOR)
	if (0 != omrthread_rwmutex_try_enter_write(_javaVM->classUnloadMutex)) {
#else
	if (0 != omrthread_monitor_try_enter(_javaVM->classUnloadMutex)) {
#endif /* J9VM_JIT_CLASS_UNLOAD_RWMONITOR */
		/* Failed acquire the monitor so interrupt the JIT.  This will allow the GC
		 * to continue unloading classes.
		 */
		TRIGGER_J9HOOK_MM_INTERRUPT_COMPILATION(_extensions->hookInterface, (J9VMThread *)env->getLanguageVMThread());
#if defined(J9VM_JIT_CLASS_UNLOAD_RWMONITOR)
		omrthread_rwmutex_enter_write(_javaVM->classUnloadMutex);
#else
		omrthread_monitor_enter(_javaVM->classUnloadMutex);
#endif /* J9VM_JIT_CLASS_UNLOAD_RWMONITOR */
	}
}

/**
 * Release the classUnloadMonitor.  This will allow the JIT to compile new methods.
 */
void
MM_MetronomeDelegate::unlockClassUnloadMonitor(MM_EnvironmentRealtime *env)
{
#if defined(J9VM_JIT_CLASS_UNLOAD_RWMONITOR)
	omrthread_rwmutex_exit_write(_javaVM->classUnloadMutex);
#else
	omrthread_monitor_exit(_javaVM->classUnloadMutex);
#endif /* J9VM_JIT_CLASS_UNLOAD_RWMONITOR */
}

void
MM_MetronomeDelegate::reportClassUnloadingStart(MM_EnvironmentBase *env)
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
MM_MetronomeDelegate::reportClassUnloadingEnd(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	MM_ClassUnloadStats *classUnloadStats = &_extensions->globalGCStats.classUnloadStats;

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

void
MM_MetronomeDelegate::reportSyncGCEnd(MM_EnvironmentBase *env)
{
	OMRPORT_ACCESS_FROM_ENVIRONMENT(env);
	UDATA approximateFreeMemorySize = _extensions->heap->getApproximateActiveFreeMemorySize();
#if defined(OMR_GC_DYNAMIC_CLASS_UNLOADING)
	MM_ClassUnloadStats *classUnloadStats = &_extensions->globalGCStats.classUnloadStats;
	UDATA classLoaderUnloadCount = classUnloadStats->_classLoaderUnloadedCount;
	UDATA classUnloadCount = classUnloadStats->_classesUnloadedCount;
	UDATA anonymousClassUnloadCount = classUnloadStats->_anonymousClassesUnloadedCount;
#else /* defined(OMR_GC_DYNAMIC_CLASS_UNLOADING) */
	UDATA classLoaderUnloadCount = 0;
	UDATA classUnloadCount = 0;
	UDATA anonymousClassUnloadCount = 0;
#endif /* defined(OMR_GC_DYNAMIC_CLASS_UNLOADING) */
	UDATA weakReferenceCount = _extensions->markJavaStats._weakReferenceStats._cleared;
	UDATA softReferenceCount = _extensions->markJavaStats._softReferenceStats._cleared;
	UDATA maxSoftReferenceAge = _extensions->getMaxSoftReferenceAge();
	UDATA softReferenceAge = _extensions->getDynamicMaxSoftReferenceAge();
	UDATA phantomReferenceCount = _extensions->markJavaStats._phantomReferenceStats._cleared;
	UDATA finalizerCount = _extensions->globalGCStats.metronomeStats.getWorkPacketOverflowCount();
	UDATA packetOverflowCount = _extensions->globalGCStats.metronomeStats.getWorkPacketOverflowCount();
	UDATA objectOverflowCount = _extensions->globalGCStats.metronomeStats.getObjectOverflowCount();
			
	Trc_MM_SynchGCEnd(env->getLanguageVMThread(),
		approximateFreeMemorySize,
		0,
		classLoaderUnloadCount,
		classUnloadCount,
		weakReferenceCount,
		softReferenceCount,
		maxSoftReferenceAge,
		softReferenceAge,
		phantomReferenceCount,
		finalizerCount,
		packetOverflowCount,
		objectOverflowCount
	);
	
	TRIGGER_J9HOOK_MM_PRIVATE_METRONOME_SYNCHRONOUS_GC_END(_extensions->privateHookInterface,
		env->getOmrVMThread(), omrtime_hires_clock(),
		J9HOOK_MM_PRIVATE_METRONOME_SYNCHRONOUS_GC_END,
		approximateFreeMemorySize,
		0,
		classLoaderUnloadCount,
		classUnloadCount,
		anonymousClassUnloadCount,
		weakReferenceCount,
		softReferenceCount,
		maxSoftReferenceAge,
		softReferenceAge,
		phantomReferenceCount,
		finalizerCount,
		packetOverflowCount,
		objectOverflowCount
	);
}
/**
 * Factory method for creating the access barrier. Note that the default realtime access barrier
 * doesn't handle the RTSJ checks.
 */
MM_RealtimeAccessBarrier*
MM_MetronomeDelegate::allocateAccessBarrier(MM_EnvironmentBase *env)
{
	return MM_RealtimeAccessBarrier::newInstance(env);
}

/**
 * Iterates over all threads and enables the double barrier for each thread by setting the
 * remembered set fragment index to the reserved index.
 */
void
MM_MetronomeDelegate::enableDoubleBarrier(MM_EnvironmentBase *env)
{
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_RealtimeAccessBarrier* realtimeAccessBarrier = (MM_RealtimeAccessBarrier*)extensions->accessBarrier;
	GC_VMThreadListIterator vmThreadListIterator(_javaVM);
	
	/* First, enable the global double barrier flag so new threads will have the double barrier enabled. */
	realtimeAccessBarrier->setDoubleBarrierActive();
	while(J9VMThread* thread = vmThreadListIterator.nextVMThread()) {
		/* Second, enable the double barrier on all threads individually. */
		realtimeAccessBarrier->setDoubleBarrierActiveOnThread(MM_EnvironmentBase::getEnvironment(thread->omrVMThread));
	}
}

/**
 * Disables the double barrier for the specified thread.
 */
void
MM_MetronomeDelegate::disableDoubleBarrierOnThread(MM_EnvironmentBase* env, OMR_VMThread* vmThread)
{
	/* This gets called on a per thread basis as threads get scanned. */
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_RealtimeAccessBarrier* realtimeAccessBarrier = (MM_RealtimeAccessBarrier*)extensions->accessBarrier;
	realtimeAccessBarrier->setDoubleBarrierInactiveOnThread(MM_EnvironmentBase::getEnvironment(vmThread));
}

/**
 * Disables the global double barrier flag. This should be called after all threads have been scanned
 * and disableDoubleBarrierOnThread has been called on each of them.
 */
void
MM_MetronomeDelegate::disableDoubleBarrier(MM_EnvironmentBase* env)
{
	/* The enabling of the double barrier must traverse all threads, but the double barrier gets disabled
	 * on a per thread basis as threads get scanned, so no need to traverse all threads in this method.
	 */
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	MM_RealtimeAccessBarrier* realtimeAccessBarrier = (MM_RealtimeAccessBarrier*)extensions->accessBarrier;
	realtimeAccessBarrier->setDoubleBarrierInactive();
}


#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
/**
 * Walk all class loaders marking their classes if the classLoader object has been
 * marked.
 *
 * @return true if any classloaders/classes are marked, false otherwise
 */
bool
MM_MetronomeDelegate::doClassTracing(MM_EnvironmentRealtime *env)
{
	J9ClassLoader *classLoader;
	bool didWork = false;
	
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	GC_ClassLoaderLinkedListIterator classLoaderIterator(env, extensions->classLoaderManager);
	
	while((classLoader = (J9ClassLoader *)classLoaderIterator.nextSlot()) != NULL) {
		if (0 == (classLoader->gcFlags & J9_GC_CLASS_LOADER_DEAD)) {
			if(J9CLASSLOADER_ANON_CLASS_LOADER == (classLoader->flags & J9CLASSLOADER_ANON_CLASS_LOADER)) {
				/* Anonymous classloader should be scanned on level of classes every time */
				GC_ClassLoaderSegmentIterator segmentIterator(classLoader, MEMORY_TYPE_RAM_CLASS);
				J9MemorySegment *segment = NULL;
				while(NULL != (segment = segmentIterator.nextSegment())) {
					GC_ClassHeapIterator classHeapIterator(_javaVM, segment);
					J9Class *clazz = NULL;
					while(NULL != (clazz = classHeapIterator.nextClass())) {
						if((0 == (J9CLASS_EXTENDED_FLAGS(clazz) & J9ClassGCScanned)) && _markingScheme->isMarked(clazz->classObject)) {
							J9CLASS_EXTENDED_FLAGS_SET(clazz, J9ClassGCScanned);

							/* Scan class */
							GC_ClassIterator objectSlotIterator(env, clazz);
							volatile j9object_t *objectSlotPtr = NULL;
							while((objectSlotPtr = objectSlotIterator.nextSlot()) != NULL) {
								didWork |= _markingScheme->markObject(env, *objectSlotPtr);
							}

							GC_ClassIteratorClassSlots classSlotIterator(_javaVM, clazz);
							J9Class *classPtr;
							while (NULL != (classPtr = classSlotIterator.nextSlot())) {
								didWork |= markClass(env, classPtr);
							}
						}
					}
					_realtimeGC->condYield(env, 0);
				}
			} else {
				/* Check if the class loader has not been scanned but the class loader is live */
				if( !(classLoader->gcFlags & J9_GC_CLASS_LOADER_SCANNED) && _markingScheme->isMarked((J9Object *)classLoader->classLoaderObject)) {
					/* Flag the class loader as being scanned */
					classLoader->gcFlags |= J9_GC_CLASS_LOADER_SCANNED;

					GC_ClassLoaderSegmentIterator segmentIterator(classLoader, MEMORY_TYPE_RAM_CLASS);
					J9MemorySegment *segment = NULL;
					J9Class *clazz;

					while(NULL != (segment = segmentIterator.nextSegment())) {
						GC_ClassHeapIterator classHeapIterator(_javaVM, segment);
						while(NULL != (clazz = classHeapIterator.nextClass())) {
							/* Scan class */
							GC_ClassIterator objectSlotIterator(env, clazz);
							volatile j9object_t *objectSlotPtr = NULL;
							while((objectSlotPtr = objectSlotIterator.nextSlot()) != NULL) {
								didWork |= _markingScheme->markObject(env, *objectSlotPtr);
							}

							GC_ClassIteratorClassSlots classSlotIterator(_javaVM, clazz);
							J9Class *classPtr;
							while (NULL != (classPtr = classSlotIterator.nextSlot())) {
								didWork |= markClass(env, classPtr);
							}
						}
						_realtimeGC->condYield(env, 0);
					}

					/* CMVC 131487 */
					J9HashTableState walkState;
					/*
					 * We believe that (NULL == classLoader->classHashTable) is set ONLY for DEAD class loader
					 * so, if this pointer happened to be NULL at this point let it crash here
					 */
					Assert_MM_true(NULL != classLoader->classHashTable);
					/*
					 * CMVC 178060 : disable hash table growth to prevent hash table entries from being rehashed during GC yield
					 * while GC was in the middle of iterating the hash table.
					 */
					hashTableSetFlag(classLoader->classHashTable, J9HASH_TABLE_DO_NOT_REHASH);
					clazz = _javaVM->internalVMFunctions->hashClassTableStartDo(classLoader, &walkState, 0);
					while (NULL != clazz) {
						didWork |= markClass(env, clazz);
						clazz = _javaVM->internalVMFunctions->hashClassTableNextDo(&walkState);

						/**
						 * Jazz103 55784: We cannot rehash the table in the middle of iteration and the Space-opt hashtable cannot grow if
						 * J9HASH_TABLE_DO_NOT_REHASH is enabled. Don't yield if the hashtable is space-optimized because we run the
						 * risk of the mutator not being able to grow to accommodate new elements.
						 */
						if (!hashTableIsSpaceOptimized(classLoader->classHashTable)) {
							_realtimeGC->condYield(env, 0);
						}
					}
					/*
					 * CMVC 178060 : re-enable hash table growth. disable hash table growth to prevent hash table entries from being rehashed during GC yield
					 * while GC was in the middle of iterating the hash table.
					 */
					hashTableResetFlag(classLoader->classHashTable, J9HASH_TABLE_DO_NOT_REHASH);

					if (NULL != classLoader->moduleHashTable) {
						J9Module **modulePtr = (J9Module **)hashTableStartDo(classLoader->moduleHashTable, &walkState);
						while (NULL != modulePtr) {
							J9Module * const module = *modulePtr;

							didWork |= _markingScheme->markObject(env, module->moduleObject);
							didWork |= _markingScheme->markObject(env, module->moduleName);
							didWork |= _markingScheme->markObject(env, module->version);
							modulePtr = (J9Module**)hashTableNextDo(&walkState);
						}
					}
				}
			}
		}
		/* This yield point is for the case when there are lots of classloaders that will be unloaded */
		_realtimeGC->condYield(env, 0);
	}
	return didWork;
}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

bool
MM_MetronomeDelegate::doTracing(MM_EnvironmentRealtime* env)
{
	/* TODO CRGTMP make class tracing concurrent */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if(isDynamicClassUnloadingEnabled()) {	
		return doClassTracing(env);
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
	return false;
}

void
MM_MetronomeDelegate::defaultMemorySpaceAllocated(MM_GCExtensionsBase *extensions, void* defaultMemorySpace)
{
	J9JavaVM* vm = (J9JavaVM *)extensions->getOmrVM()->_language_vm;
	
	vm->heapBase = extensions->heap->getHeapBase();
	vm->heapTop = extensions->heap->getHeapTop();
}

/**
 * This function has to be called at the beginning of continueGC because requestExclusiveVMAccess
 * assumes the current J9VMThread does not have VM Access.  All java threads that cause a GC (either
 * System.gc or allocation failure) will have VM access when entering the GC so we have to give it up.
 *
 * @param threadRequestingExclusive the J9VMThread for the MetronomeGCThread that will
 * be requesting exclusive vm access.
 */
void
MM_MetronomeDelegate::preRequestExclusiveVMAccess(OMR_VMThread *threadRequestingExclusive)
{
	if (threadRequestingExclusive == NULL) {
		return;
	}
	J9VMThread *vmThread = (J9VMThread *)threadRequestingExclusive->_language_vmthread;
	vmThread->javaVM->internalVMFunctions->internalReleaseVMAccess(vmThread);
}

/**
 * This function is called when leaving continueGC so the J9VMThread associated with current
 * MetronomeGCThread will get its VM Access back before returning to run Java code.
 *
 * @param threadRequestingExclusive the J9VMThread for the MetronomeGCThread that requested
 * exclusive vm access.
 */
void
MM_MetronomeDelegate::postRequestExclusiveVMAccess(OMR_VMThread *threadRequestingExclusive)
{
	if (NULL == threadRequestingExclusive) {
		return;
	}
	J9VMThread *vmThread = (J9VMThread *)threadRequestingExclusive->_language_vmthread;
	vmThread->javaVM->internalVMFunctions->internalAcquireVMAccess(vmThread);
}



/**
 * A call to requestExclusiveVMAccess must be followed by a call to waitForExclusiveVMAccess,
 * but not necessarily by the same thread.
 *
 * @param env the requesting thread.
 * @param block boolean input parameter specifying whether we should block and wait, if another party is requesting at the same time, or we return
 * @return boolean returning whether request was successful or not (make sense only if block is set to FALSE)
 */
uintptr_t
MM_MetronomeDelegate::requestExclusiveVMAccess(MM_EnvironmentBase *env, uintptr_t block, uintptr_t *gcPriority)
{
	return _javaVM->internalVMFunctions->requestExclusiveVMAccessMetronomeTemp(_javaVM, block, &_vmResponsesRequiredForExclusiveVMAccess, &_jniResponsesRequiredForExclusiveVMAccess, gcPriority);
}

/**
 * Block until the earlier request for exclusive VM access completes.
 * @note This can only be called by the MainGC thread.
 * @param The requesting thread.
 */
void 
MM_MetronomeDelegate::waitForExclusiveVMAccess(MM_EnvironmentBase *env, bool waitRequired)
{
	J9VMThread *mainGCThread = (J9VMThread *)env->getLanguageVMThread();
	
	if (waitRequired) {
		_javaVM->internalVMFunctions->waitForExclusiveVMAccessMetronomeTemp((J9VMThread *)env->getLanguageVMThread(), _vmResponsesRequiredForExclusiveVMAccess, _jniResponsesRequiredForExclusiveVMAccess);
	}
	++(mainGCThread->omrVMThread->exclusiveCount);
}

/**
 * Acquire (request and block until success) exclusive VM access.
 * @note This can only be called by the MainGC thread.
 * @param The requesting thread.
 */
void 
MM_MetronomeDelegate::acquireExclusiveVMAccess(MM_EnvironmentBase *env, bool waitRequired)
{
	J9VMThread *mainGCThread = (J9VMThread *)env->getLanguageVMThread();

	if (waitRequired) {
		_javaVM->internalVMFunctions->acquireExclusiveVMAccessFromExternalThread(_javaVM);
	}
	++(mainGCThread->omrVMThread->exclusiveCount);

}

/**
 * Release the held exclusive VM access.
 * @note This can only be called by the MainGC thread.
 * @param The requesting thread.
 */
void 
MM_MetronomeDelegate::releaseExclusiveVMAccess(MM_EnvironmentBase *env, bool releaseRequired)
{
	J9VMThread *mainGCThread = (J9VMThread *)env->getLanguageVMThread();

	--(mainGCThread->omrVMThread->exclusiveCount);
	if (releaseRequired) {
		_javaVM->internalVMFunctions->releaseExclusiveVMAccessMetronome(mainGCThread);
		/* Set the exclusive access response counts to an unusual value,
		 * just for debug purposes, so we can detect scenarios, when main
		 * thread is waiting for Xaccess with noone requesting it before.
		 */
		_vmResponsesRequiredForExclusiveVMAccess = 0x7fffffff;
		_jniResponsesRequiredForExclusiveVMAccess = 0x7fffffff;
	}
}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
/**
 * Mark this class
 */
bool 
MM_MetronomeDelegate::markClass(MM_EnvironmentRealtime *env, J9Class *clazz)
{
	bool result = false;
	if (clazz != NULL) {
		result = markClassNoCheck(env, clazz);
	}
	return result;
}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

#if defined(J9VM_GC_FINALIZATION)
void
MM_MetronomeDelegate::scanUnfinalizedObjects(MM_EnvironmentRealtime *env)
{
	const UDATA maxIndex = getUnfinalizedObjectListCount(env);
	/* first we need to move the current list to the prior list and process the prior list,
	 * because if object has not yet become finalizable, we have to re-insert it back to the current list.
	 */
	if (env->_currentTask->synchronizeGCThreadsAndReleaseMain(env, UNIQUE_ID)) {
		GC_OMRVMInterface::flushNonAllocationCaches(env);
		UDATA listIndex;
		for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
			MM_UnfinalizedObjectList *unfinalizedObjectList = &_extensions->unfinalizedObjectLists[listIndex];
			unfinalizedObjectList->startUnfinalizedProcessing();
		}
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}

	GC_Environment *gcEnv = env->getGCEnvironment();
	GC_FinalizableObjectBuffer buffer(_extensions);
	UDATA listIndex;
	for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
		if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
			MM_UnfinalizedObjectList *unfinalizedObjectList = &_extensions->unfinalizedObjectLists[listIndex];
			J9Object *object = unfinalizedObjectList->getPriorList();
			UDATA objectsVisited = 0;

			while (NULL != object) {
				objectsVisited += 1;
				gcEnv->_markJavaStats._unfinalizedCandidates += 1;
				J9Object* next = _extensions->accessBarrier->getFinalizeLink(object);
				if (_markingScheme->markObject(env, object)) {
					/* object was not previously marked -- it is now finalizable so push it to the local buffer */
					buffer.add(env, object);
					gcEnv->_markJavaStats._unfinalizedEnqueued += 1;
					_finalizationRequired = true;
				} else {
					/* object was already marked. It is still unfinalized */
					gcEnv->_unfinalizedObjectBuffer->add(env, object);
				}
				object = next;
				if (UNFINALIZED_OBJECT_YIELD_CHECK_INTERVAL == objectsVisited ) {
					_scheduler->condYieldFromGC(env);
					objectsVisited = 0;
				}
			}
			_scheduler->condYieldFromGC(env);
		}
	}

	/* Flush the local buffer of finalizable objects to the global list */
	buffer.flush(env);

	/* restore everything to a flushed state before exiting */
	gcEnv->_unfinalizedObjectBuffer->flush(env);
}
#endif /* J9VM_GC_FINALIZATION */

void
MM_MetronomeDelegate::scanOwnableSynchronizerObjects(MM_EnvironmentRealtime *env)
{
	const UDATA maxIndex = getOwnableSynchronizerObjectListCount(env);

	/* first we need to move the current list to the prior list and process the prior list,
	 * because if object has been marked, we have to re-insert it back to the current list.
	 */
	if (env->_currentTask->synchronizeGCThreadsAndReleaseMain(env, UNIQUE_ID)) {
		GC_OMRVMInterface::flushNonAllocationCaches(env);
		UDATA listIndex;
		for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
			MM_OwnableSynchronizerObjectList *ownableSynchronizerObjectList = &_extensions->getOwnableSynchronizerObjectLists()[listIndex];
			ownableSynchronizerObjectList->startOwnableSynchronizerProcessing();
		}
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}

	GC_Environment *gcEnv = env->getGCEnvironment();
	MM_OwnableSynchronizerObjectBuffer *buffer = gcEnv->_ownableSynchronizerObjectBuffer;
	UDATA listIndex;
	for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
		MM_OwnableSynchronizerObjectList *list = &_extensions->getOwnableSynchronizerObjectLists()[listIndex];
		if (!list->wasEmpty()) {
			if (J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
				J9Object *object = list->getPriorList();
				UDATA objectsVisited = 0;
				while (NULL != object) {
					objectsVisited += 1;
					gcEnv->_markJavaStats._ownableSynchronizerCandidates += 1;

					/* Get next before adding it to the buffer, as buffer modifies OwnableSynchronizerLink */
					J9Object* next = _extensions->accessBarrier->getOwnableSynchronizerLink(object);
					if (_markingScheme->isMarked(object)) {
						buffer->add(env, object);
					} else {
						gcEnv->_markJavaStats._ownableSynchronizerCleared += 1;
					}
					object = next;

					if (OWNABLE_SYNCHRONIZER_OBJECT_YIELD_CHECK_INTERVAL == objectsVisited ) {
						_scheduler->condYieldFromGC(env);
						objectsVisited = 0;
					}
				}
				_scheduler->condYieldFromGC(env);
			}
		}
	}
	/* restore everything to a flushed state before exiting */
	buffer->flush(env);
}

void
MM_MetronomeDelegate::scanWeakReferenceObjects(MM_EnvironmentRealtime *env)
{
	GC_Environment *gcEnv = env->getGCEnvironment();
	Assert_MM_true(gcEnv->_referenceObjectBuffer->isEmpty());
	const UDATA maxIndex = getReferenceObjectListCount(env);
	UDATA listIndex;
	for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
		if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
			MM_ReferenceObjectList *referenceObjectList = &_extensions->referenceObjectLists[listIndex];
			referenceObjectList->startWeakReferenceProcessing();
			processReferenceList(env, NULL, referenceObjectList->getPriorWeakList(), &gcEnv->_markJavaStats._weakReferenceStats);
			_scheduler->condYieldFromGC(env);
		}
	}
	Assert_MM_true(gcEnv->_referenceObjectBuffer->isEmpty());
}

void
MM_MetronomeDelegate::scanSoftReferenceObjects(MM_EnvironmentRealtime *env)
{
	GC_Environment *gcEnv = env->getGCEnvironment();
	Assert_MM_true(gcEnv->_referenceObjectBuffer->isEmpty());
	const UDATA maxIndex = getReferenceObjectListCount(env);
	UDATA listIndex;
	for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
		if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
			MM_ReferenceObjectList *referenceObjectList = &_extensions->referenceObjectLists[listIndex];
			referenceObjectList->startSoftReferenceProcessing();
			processReferenceList(env, NULL, referenceObjectList->getPriorSoftList(), &gcEnv->_markJavaStats._softReferenceStats);
			_scheduler->condYieldFromGC(env);
		}
	}
	Assert_MM_true(gcEnv->_referenceObjectBuffer->isEmpty());
}

void
MM_MetronomeDelegate::scanPhantomReferenceObjects(MM_EnvironmentRealtime *env)
{
	GC_Environment *gcEnv = env->getGCEnvironment();
	/* unfinalized processing may discover more phantom reference objects */
	gcEnv->_referenceObjectBuffer->flush(env);
	const UDATA maxIndex = getReferenceObjectListCount(env);
	UDATA listIndex;
	for (listIndex = 0; listIndex < maxIndex; ++listIndex) {
		if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
			MM_ReferenceObjectList *referenceObjectList = &_extensions->referenceObjectLists[listIndex];
			referenceObjectList->startPhantomReferenceProcessing();
			processReferenceList(env, NULL, referenceObjectList->getPriorPhantomList(), &gcEnv->_markJavaStats._phantomReferenceStats);
			_scheduler->condYieldFromGC(env);
		}
	}
	Assert_MM_true(gcEnv->_referenceObjectBuffer->isEmpty());
}

void
MM_MetronomeDelegate::processReferenceList(MM_EnvironmentRealtime *env, MM_HeapRegionDescriptorRealtime *region, J9Object* headOfList, MM_ReferenceStats *referenceStats)
{
	UDATA objectsVisited = 0;
#if defined(J9VM_GC_FINALIZATION)
	GC_FinalizableReferenceBuffer buffer(_extensions);
#endif /* J9VM_GC_FINALIZATION */
	J9Object* referenceObj = headOfList;

	while (NULL != referenceObj) {
		objectsVisited += 1;
		referenceStats->_candidates += 1;

		Assert_MM_true(_markingScheme->isMarked(referenceObj));

		J9Object* nextReferenceObj = _extensions->accessBarrier->getReferenceLink(referenceObj);

		GC_SlotObject referentSlotObject(_extensions->getOmrVM(), J9GC_J9VMJAVALANGREFERENCE_REFERENT_ADDRESS(env, referenceObj));
		J9Object *referent = referentSlotObject.readReferenceFromSlot();
		if (NULL != referent) {
			UDATA referenceObjectType = J9CLASS_FLAGS(J9GC_J9OBJECT_CLAZZ(referenceObj, env)) & J9AccClassReferenceMask;
			if (_markingScheme->isMarked(referent)) {
				if (J9AccClassReferenceSoft == referenceObjectType) {
					U_32 age = J9GC_J9VMJAVALANGSOFTREFERENCE_AGE(env, referenceObj);
					if (age < _extensions->getMaxSoftReferenceAge()) {
						/* Soft reference hasn't aged sufficiently yet - increment the age */
						J9GC_J9VMJAVALANGSOFTREFERENCE_AGE(env, referenceObj) = age + 1;
					}
				}
			} else {
				/* transition the state to cleared */
				Assert_MM_true(GC_ObjectModel::REF_STATE_INITIAL == J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj));
				J9GC_J9VMJAVALANGREFERENCE_STATE(env, referenceObj) = GC_ObjectModel::REF_STATE_CLEARED;

				referenceStats->_cleared += 1;

				/* Phantom references keep it's referent alive in Java 8 and doesn't in Java 9 and later */
				if ((J9AccClassReferencePhantom == referenceObjectType) && ((J2SE_VERSION(_javaVM) & J2SE_VERSION_MASK) <= J2SE_18)) {
					/* Scanning will be done after the enqueuing */
					_markingScheme->markObject(env, referent);
				} else {
					referentSlotObject.writeReferenceToSlot(NULL);
				}
#if defined(J9VM_GC_FINALIZATION)
				/* Check if the reference has a queue */
				if (0 != J9GC_J9VMJAVALANGREFERENCE_QUEUE(env, referenceObj)) {
					/* Reference object can be enqueued onto the finalizable list */
					buffer.add(env, referenceObj);
					referenceStats->_enqueued += 1;
					/* Flag for the finalizer */
					_finalizationRequired = true;
				}
#endif /* J9VM_GC_FINALIZATION */
			}
		}
		referenceObj = nextReferenceObj;
		if (REFERENCE_OBJECT_YIELD_CHECK_INTERVAL == objectsVisited) {
			_scheduler->condYieldFromGC(env);
			objectsVisited = 0;
		}
	}
#if defined(J9VM_GC_FINALIZATION)
	buffer.flush(env);
#endif /* J9VM_GC_FINALIZATION */
}

/**
 * Mark all of the roots. The system and application classloaders need to be set
 * to marked/scanned before root marking begins.
 *
 * @note Once the root lists all have barriers this code may change to call rootScanner.scanRoots();
 * 
 */
void 
MM_MetronomeDelegate::markLiveObjectsRoots(MM_EnvironmentRealtime *env)
{
	MM_RealtimeMarkingSchemeRootMarker rootMarker(env, _realtimeGC);
	env->setRootScanner(&rootMarker);
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	rootMarker.setClassDataAsRoots(!isDynamicClassUnloadingEnabled());
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

	/* Mark root set classes */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if(env->isMainThread()) {
		/* TODO: This code belongs somewhere else? */
		/* Setting the permanent class loaders to scanned without a locked operation is safe
		 * Class loaders will not be rescanned until a thread synchronize is executed
		 */
		if(isDynamicClassUnloadingEnabled()) {
			((J9ClassLoader *)_javaVM->systemClassLoader)->gcFlags |= J9_GC_CLASS_LOADER_SCANNED;
			_markingScheme->markObject(env, (J9Object *)((J9ClassLoader *)_javaVM->systemClassLoader)->classLoaderObject);
			if(_javaVM->applicationClassLoader) {
				((J9ClassLoader *)_javaVM->applicationClassLoader)->gcFlags |= J9_GC_CLASS_LOADER_SCANNED;
				_markingScheme->markObject(env, (J9Object *)((J9ClassLoader *)_javaVM->applicationClassLoader)->classLoaderObject);
			}
		}
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
	
	/* Note: it's important to scan the finalizable objects queue (atomically) before the
	 * threads, because the finalizer threads are among the threads and, once any one of
	 * them is scanned and then allowed to execute, any object it takes off the finalizer
	 * queue had better also be scanned.  An alternative would be to put a special read
	 * barrier in the queue-removal action but controlling the order is an easy solution.
	 *
	 * It is also important to scan JNI global references after scanning threads, because
	 * the JNI global reference barrier is executed at deletion time, not creation time.
	 * We could have barriers in both, but controlling the order is an easy solution.
	 *
	 *
	 * The Metronome write barrier ensures that no unscanned thread can expose an object
	 * to other threads without it becoming a root and no scanned thread can make an
	 * object that was once reachable unreachable until it has been traced ("snapshot at
	 * the beginning with a fuzzy snapshot").  This eliminates other order dependencies
	 * between portions of the scan or requirements that multiple phases be done as an
	 * atomic unit.  However, some phases are still done atomically because we have not
	 * yet determined whether the iterators that they use are safe and complete and have
	 * not even analyzed in all cases whether correctness depends on completeness.
	 */
	if (env->_currentTask->synchronizeGCThreadsAndReleaseMain(env, UNIQUE_ID)) {
#if defined(J9VM_GC_FINALIZATION)
		/* Note: if iterators are safe in scanFinalizableObjects, disableYield() could be
		 * removed.
		 */
		env->disableYield();
		rootMarker.scanFinalizableObjects(env);
		env->enableYield();
		_scheduler->condYieldFromGC(env);
#endif /* J9VM_GC_FINALIZATION */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		if (!isDynamicClassUnloadingEnabled()) {
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
			/* We are scanning all classes, no need to include stack frame references */
			rootMarker.setIncludeStackFrameClassReferences(false);
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		} else {
			rootMarker.setIncludeStackFrameClassReferences(true);
		}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}

	rootMarker.scanThreads(env);

	if (env->_currentTask->synchronizeGCThreadsAndReleaseMain(env, UNIQUE_ID)) {
		_extensions->newThreadAllocationColor = GC_MARK;
		_realtimeGC->disableDoubleBarrier(env);
		if (_realtimeGC->verbose(env) >= 3) {
			rootMarker.reportThreadCount(env);
		}

		/* Note: if iterators are safe for some or all remaining atomic root categories,
		 * disableYield() could be removed or moved inside scanAtomicRoots.
		 */
		env->disableYield();
		rootMarker.scanAtomicRoots(env);
		env->enableYield();
		rootMarker.scanIncrementalRoots(env);
	
		env->_currentTask->releaseSynchronizedGCThreads(env);
	}

	env->setRootScanner(NULL);
}

void
MM_MetronomeDelegate::markLiveObjectsScan(MM_EnvironmentRealtime *env)
{
	env->getGCEnvironment()->_referenceObjectBuffer->flush(env);
}

void
MM_MetronomeDelegate::markLiveObjectsComplete(MM_EnvironmentRealtime *env)
{
	/* Process reference objects and finalizable objects. */
	MM_RealtimeMarkingSchemeRootClearer rootScanner(env, _realtimeGC);
	env->setRootScanner(&rootScanner);
	rootScanner.scanClearable(env);
	env->setRootScanner(NULL);
	Assert_MM_true(env->getGCEnvironment()->_referenceObjectBuffer->isEmpty());
}

void
MM_MetronomeDelegate::checkReferenceBuffer(MM_EnvironmentRealtime *env)
{
	Assert_MM_true(env->getGCEnvironment()->_referenceObjectBuffer->isEmpty());
}

void
MM_MetronomeDelegate::setUnmarkedImpliesCleared()
{
	_unmarkedImpliesCleared = true;
}

void
MM_MetronomeDelegate::unsetUnmarkedImpliesCleared()
{		
	/* This flag is set during the soft reference scanning just before unmarked references are to be
	 * cleared. It's used to prevent objects that are going to be cleared (e.g. referent that is not marked,
	 * or unmarked string constant) from escaping.
	 */
	_unmarkedImpliesCleared = false;
	_unmarkedImpliesStringsCleared = false;

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/* enable to use mark information to detect is class dead */
	_unmarkedImpliesClasses = true;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

#endif /* defined(J9VM_GC_REALTIME) */


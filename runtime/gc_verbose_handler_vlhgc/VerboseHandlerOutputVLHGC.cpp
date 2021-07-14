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

#include "VerboseHandlerOutputVLHGC.hpp"

#include "CollectionStatisticsVLHGC.hpp"
#include "ConcurrentPhaseStatsBase.hpp"
#include "CopyForwardStats.hpp"
#include "CycleStateVLHGC.hpp"
#include "EnvironmentBase.hpp"
#include "GCExtensions.hpp"
#include "MarkVLHGCStats.hpp"
#include "ReferenceStats.hpp"
#include "VerboseManager.hpp"
#include "VerboseWriterChain.hpp"
#include "VerboseHandlerJava.hpp"

#include "mmhook.h"

static void verboseHandlerGCStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerGCEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerTaxationEntryPoint(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerCycleStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerCycleContinue(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerCycleEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerExclusiveStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerExclusiveEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerSystemGCStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerSystemGCEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerAllocationFailureStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerFailedAllocationCompleted(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerAllocationFailureEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerCopyForwardStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerCopyForwardEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerConcurrentStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerConcurrentEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerGMPMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerGMPMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerGlobalGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerGlobalGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerPGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerPGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerReclaimSweepStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerReclaimSweepEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerReclaimCompactStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerReclaimCompactEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerExcessiveGCRaised(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerAcquiredExclusiveToSatisfyAllocation(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
static void verboseHandlerClassUnloadingEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);

MM_VerboseHandlerOutput *
MM_VerboseHandlerOutputVLHGC::newInstance(MM_EnvironmentBase *env, MM_VerboseManager *manager)
{
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env->getOmrVM());

	MM_VerboseHandlerOutputVLHGC *verboseHandlerOutput = (MM_VerboseHandlerOutputVLHGC *)extensions->getForge()->allocate(sizeof(MM_VerboseHandlerOutputVLHGC), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (NULL != verboseHandlerOutput) {
		new(verboseHandlerOutput) MM_VerboseHandlerOutputVLHGC(extensions);
		if(!verboseHandlerOutput->initialize(env, manager)) {
			verboseHandlerOutput->kill(env);
			verboseHandlerOutput = NULL;
		}
	}
	return verboseHandlerOutput;
}

bool
MM_VerboseHandlerOutputVLHGC::initialize(MM_EnvironmentBase *env, MM_VerboseManager *manager)
{
	bool initSuccess = MM_VerboseHandlerOutput::initialize(env, manager);

	_mmHooks = J9_HOOK_INTERFACE(MM_GCExtensions::getExtensions(_extensions)->hookInterface);

	return initSuccess;
}

void
MM_VerboseHandlerOutputVLHGC::tearDown(MM_EnvironmentBase *env)
{
	MM_VerboseHandlerOutput::tearDown(env);
}

void
MM_VerboseHandlerOutputVLHGC::enableVerbose()
{
	MM_VerboseHandlerOutput::enableVerbose();

	/* GCLaunch */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_START, verboseHandlerSystemGCStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_END, verboseHandlerSystemGCEnd, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_START, verboseHandlerAllocationFailureStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_FAILED_ALLOCATION_COMPLETED, verboseHandlerFailedAllocationCompleted, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_END, verboseHandlerAllocationFailureEnd, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START, verboseHandlerTaxationEntryPoint, OMR_GET_CALLSITE(), (void *)this);

	/* Exclusive */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_EXCLUSIVE_ACCESS_ACQUIRE, verboseHandlerExclusiveStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_EXCLUSIVE_ACCESS_RELEASE, verboseHandlerExclusiveEnd, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ACQUIRED_EXCLUSIVE_TO_SATISFY_ALLOCATION, verboseHandlerAcquiredExclusiveToSatisfyAllocation, OMR_GET_CALLSITE(), (void *)this);

	/* STW GC increment */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_INCREMENT_START, verboseHandlerGCStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_INCREMENT_END, verboseHandlerGCEnd, OMR_GET_CALLSITE(), (void *)this);

	/* Cycle */
	(*_mmOmrHooks)->J9HookRegisterWithCallSite(_mmOmrHooks, J9HOOK_MM_OMR_GC_CYCLE_START, verboseHandlerCycleStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmOmrHooks)->J9HookRegisterWithCallSite(_mmOmrHooks, J9HOOK_MM_OMR_GC_CYCLE_CONTINUE, verboseHandlerCycleContinue, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_POST_CYCLE_END, verboseHandlerCycleEnd, OMR_GET_CALLSITE(), (void *)this);

	/* Mark */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GMP_MARK_START, verboseHandlerGMPMarkStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GMP_MARK_END, verboseHandlerGMPMarkEnd, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_START, verboseHandlerGlobalGCMarkStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_END, verboseHandlerGlobalGCMarkEnd, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_PGC_MARK_START, verboseHandlerPGCMarkStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_PGC_MARK_END, verboseHandlerPGCMarkEnd, OMR_GET_CALLSITE(), (void *)this);

	/* Sweep */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_START, verboseHandlerReclaimSweepStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_END, verboseHandlerReclaimSweepEnd, OMR_GET_CALLSITE(), (void *)this);

	/* Compact */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_START, verboseHandlerReclaimCompactStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_END, verboseHandlerReclaimCompactEnd, OMR_GET_CALLSITE(), (void *)this);

	/* Copy Forward */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_COPY_FORWARD_START, verboseHandlerCopyForwardStart, OMR_GET_CALLSITE(), (void *)this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_COPY_FORWARD_END, verboseHandlerCopyForwardEnd, OMR_GET_CALLSITE(), (void *)this);
	
	/* Concurrent GMP */
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_START, verboseHandlerConcurrentStart, OMR_GET_CALLSITE(), this);
	(*_mmPrivateHooks)->J9HookRegisterWithCallSite(_mmPrivateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_END, verboseHandlerConcurrentEnd, OMR_GET_CALLSITE(), this);

	/* Excessive GC */
	(*_mmOmrHooks)->J9HookRegisterWithCallSite(_mmOmrHooks, J9HOOK_MM_OMR_EXCESSIVEGC_RAISED, verboseHandlerExcessiveGCRaised, OMR_GET_CALLSITE(), this);

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	(*_mmHooks)->J9HookRegisterWithCallSite(_mmHooks, J9HOOK_MM_CLASS_UNLOADING_END, verboseHandlerClassUnloadingEnd, OMR_GET_CALLSITE(), (void *)this);
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

void
MM_VerboseHandlerOutputVLHGC::disableVerbose()
{
	MM_VerboseHandlerOutput::disableVerbose();

	/* GCLaunch */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_START, verboseHandlerSystemGCStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_SYSTEM_GC_END, verboseHandlerSystemGCEnd, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_START, verboseHandlerAllocationFailureStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_FAILED_ALLOCATION_COMPLETED, verboseHandlerFailedAllocationCompleted, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ALLOCATION_FAILURE_END, verboseHandlerAllocationFailureEnd, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_TAROK_INCREMENT_START, verboseHandlerTaxationEntryPoint, NULL);

	/* Cycle */
	(*_mmOmrHooks)->J9HookUnregister(_mmOmrHooks, J9HOOK_MM_OMR_GC_CYCLE_START, verboseHandlerCycleStart, NULL);
	(*_mmOmrHooks)->J9HookUnregister(_mmOmrHooks, J9HOOK_MM_OMR_GC_CYCLE_CONTINUE, verboseHandlerCycleContinue, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_POST_CYCLE_END, verboseHandlerCycleEnd, NULL);

	/* STW GC increment */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_INCREMENT_START, verboseHandlerGCStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GC_INCREMENT_END, verboseHandlerGCEnd, NULL);

	/* Exclusive */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_EXCLUSIVE_ACCESS_ACQUIRE, verboseHandlerExclusiveStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_EXCLUSIVE_ACCESS_RELEASE, verboseHandlerExclusiveEnd, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_ACQUIRED_EXCLUSIVE_TO_SATISFY_ALLOCATION, verboseHandlerAcquiredExclusiveToSatisfyAllocation, NULL);

	/* Mark */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GMP_MARK_START, verboseHandlerGMPMarkStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_GMP_MARK_END, verboseHandlerGMPMarkEnd, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_START, verboseHandlerGlobalGCMarkStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_VLHGC_GLOBAL_GC_MARK_END, verboseHandlerGlobalGCMarkEnd, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_PGC_MARK_START, verboseHandlerPGCMarkStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_PGC_MARK_END, verboseHandlerPGCMarkEnd, NULL);

	/* Sweep */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_START, verboseHandlerReclaimSweepStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_SWEEP_END, verboseHandlerReclaimSweepEnd, NULL);

	/* Compact */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_START, verboseHandlerReclaimCompactStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_RECLAIM_COMPACT_END, verboseHandlerReclaimCompactEnd, NULL);

	/* Copy Forward */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_COPY_FORWARD_START, verboseHandlerCopyForwardStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_COPY_FORWARD_END, verboseHandlerCopyForwardEnd, NULL);
	
	/* Concurrent GMP */
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_START, verboseHandlerConcurrentStart, NULL);
	(*_mmPrivateHooks)->J9HookUnregister(_mmPrivateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_PHASE_END, verboseHandlerConcurrentEnd, NULL);

	/* Excessive GC */
	(*_mmOmrHooks)->J9HookUnregister(_mmOmrHooks, J9HOOK_MM_OMR_EXCESSIVEGC_RAISED, verboseHandlerExcessiveGCRaised, NULL);

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	(*_mmHooks)->J9HookUnregister(_mmHooks, J9HOOK_MM_CLASS_UNLOADING_END, verboseHandlerClassUnloadingEnd, NULL);
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

bool
MM_VerboseHandlerOutputVLHGC::getThreadName(char *buf, UDATA bufLen, OMR_VMThread *vmThread)
{
	return MM_VerboseHandlerJava::getThreadName(buf,bufLen,vmThread);
}

void
MM_VerboseHandlerOutputVLHGC::writeVmArgs(MM_EnvironmentBase* env, MM_VerboseBuffer* buffer)
{
	MM_VerboseHandlerJava::writeVmArgs(env, buffer, static_cast<J9JavaVM*>(_omrVM->_language_vm));
}

void
MM_VerboseHandlerOutputVLHGC::handleTaxationEntryPoint(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_TarokIncrementStartEvent* event = (MM_TarokIncrementStartEvent*)eventData;
	MM_VerboseManager* manager = getManager();
	MM_VerboseWriterChain* writer = manager->getWriterChain();
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 currentTime = event->timestamp;
	U_64 previousTime = manager->getLastTaxationEntryPointTime();
	manager->setLastTaxationEntryPointTime(currentTime);
	if (0 == previousTime) {
		previousTime = manager->getInitializedTime();
	}
	U_64 deltaTime = 0;
	bool deltaTimeSuccess = getTimeDeltaInMicroSeconds(&deltaTime, previousTime, currentTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!deltaTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}	
	writer->formatAndOutput(env, 0, "<allocation-taxation id=\"%zu\" taxation-threshold=\"%zu\" %s intervalms=\"%llu.%03llu\" />", manager->getIdAndIncrement(), event->taxationThreshold, tagTemplate, deltaTime / 1000 , deltaTime % 1000);
	writer->flush(env);
	exitAtomicReportingBlock();
}

void
MM_VerboseHandlerOutputVLHGC::outputUnfinalizedInfo(MM_EnvironmentBase *env, UDATA indent, UDATA unfinalizedCandidates, UDATA unfinalizedEnqueued)
{
	if(0 != unfinalizedCandidates) {
		_manager->getWriterChain()->formatAndOutput(env, indent, "<finalization candidates=\"%zu\" enqueued=\"%zu\" />", unfinalizedCandidates, unfinalizedEnqueued);
	}
}

void
MM_VerboseHandlerOutputVLHGC::outputOwnableSynchronizerInfo(MM_EnvironmentBase *env, UDATA indent, UDATA ownableSynchronizerCandidates, UDATA ownableSynchronizerCleared)
{
	if (0 != ownableSynchronizerCandidates) {
		_manager->getWriterChain()->formatAndOutput(env, indent, "<ownableSynchronizers candidates=\"%zu\" cleared=\"%zu\" />", ownableSynchronizerCandidates, ownableSynchronizerCleared);
	}
}

void
MM_VerboseHandlerOutputVLHGC::outputReferenceInfo(MM_EnvironmentBase *env, UDATA indent, const char *referenceType, MM_ReferenceStats *referenceStats, UDATA dynamicThreshold, UDATA maxThreshold)
{
	if(0 != referenceStats->_candidates) {
		if (0 != maxThreshold) {
			_manager->getWriterChain()->formatAndOutput(env, indent, "<references type=\"%s\" candidates=\"%zu\" cleared=\"%zu\" enqueued=\"%zu\" dynamicThreshold=\"%zu\" maxThreshold=\"%zu\" />",
					referenceType, referenceStats->_candidates, referenceStats->_cleared, referenceStats->_enqueued, dynamicThreshold, maxThreshold);
		} else {
			_manager->getWriterChain()->formatAndOutput(env, indent, "<references type=\"%s\" candidates=\"%zu\" cleared=\"%zu\" enqueued=\"%zu\" />",
					referenceType, referenceStats->_candidates, referenceStats->_cleared, referenceStats->_enqueued);
		}
	}
}

void
MM_VerboseHandlerOutputVLHGC::outputInitializedInnerStanza(MM_EnvironmentBase *env, MM_VerboseBuffer *buffer){
	outputInitializedRegion(env, buffer);
}

void
MM_VerboseHandlerOutputVLHGC::printAllocationStats(MM_EnvironmentBase* env)
{
	if (MM_CycleState::CT_GLOBAL_MARK_PHASE != env->_cycleState->_collectionType) {
		MM_VerboseHandlerOutput::printAllocationStats(env);
	}
}

bool
MM_VerboseHandlerOutputVLHGC::hasOutputMemoryInfoInnerStanza()
{
	return true;
}

const char *
MM_VerboseHandlerOutputVLHGC::getSubSpaceType(uintptr_t typeFlags)
{
	const char *subSpaceType = NULL;
	if (MEMORY_TYPE_NEW == typeFlags) {
		subSpaceType = "eden";
	} else {
		subSpaceType = "total heap";
	}
	return subSpaceType;
}

void
MM_VerboseHandlerOutputVLHGC::outputMemoryInfoInnerStanza(MM_EnvironmentBase *env, UDATA indent, MM_CollectionStatistics *statsBase)
{
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_CollectionStatisticsVLHGC *stats = MM_CollectionStatisticsVLHGC::getCollectionStatistics(statsBase);

	if (0 != stats->_edenHeapSize) {
		writer->formatAndOutput(env, indent, "<mem type=\"eden\" free=\"%zu\" total=\"%zu\" percent=\"%zu\" />",
				stats->_edenFreeHeapSize, stats->_edenHeapSize,
				((UDATA)(((U_64)stats->_edenFreeHeapSize*100) / (U_64)stats->_edenHeapSize)));
	}

	if (0 != stats->_arrayletReferenceObjects) {
		writer->formatAndOutput(env, indent, "<arraylet-reference objects=\"%zu\" leaves=\"%zu\" largest=\"%zu\" />",
				stats->_arrayletReferenceObjects, stats->_arrayletReferenceLeaves, stats->_largestReferenceArraylet);
	}
	if (0 != stats->_arrayletPrimitiveObjects) {
		writer->formatAndOutput(env, indent, "<arraylet-primitive objects=\"%zu\" leaves=\"%zu\" largest=\"%zu\" />",
				stats->_arrayletPrimitiveObjects, stats->_arrayletPrimitiveLeaves, stats->_largestPrimitiveArraylet);
	}
	if (0 != stats->_arrayletUnknownObjects) {
		writer->formatAndOutput(env, indent, "<arraylet-unknown objects=\"%zu\" leaves=\"%zu\" />",
				stats->_arrayletUnknownObjects, stats->_arrayletUnknownLeaves);
	}

	if (0 != stats->_numaNodes) {
		UDATA total = stats->_commonNumaNodeBytes + stats->_localNumaNodeBytes + stats->_nonLocalNumaNodeBytes;
		UDATA nonLocalPercent = 0;
		if (0 != total) {
			nonLocalPercent = (UDATA)((100 * (U_64)stats->_nonLocalNumaNodeBytes) / ((U_64)total));
		}
		writer->formatAndOutput(env, indent, "<numa common=\"%zu\" local=\"%zu\" non-local=\"%zu\" non-local-percent=\"%zu\" />",
				stats->_commonNumaNodeBytes, stats->_localNumaNodeBytes, stats->_nonLocalNumaNodeBytes,  nonLocalPercent);
	}

	MM_VerboseHandlerJava::outputFinalizableInfo(_manager, env, indent);

	UDATA rememberedSetFreePercent = (UDATA)((100 * (U_64)stats->_rememberedSetBytesFree) / ((U_64)stats->_rememberedSetBytesTotal));

	writer->formatAndOutput(env, indent, "<remembered-set count=\"%zu\" freebytes=\"%zu\" totalbytes=\"%zu\" percent=\"%zu\" regionsoverflowed=\"%zu\" regionsstable=\"%zu\" regionsrebuilding=\"%zu\"/>",
			stats->_rememberedSetCount, stats->_rememberedSetBytesFree, stats->_rememberedSetBytesTotal, rememberedSetFreePercent,
			stats->_rememberedSetOverflowedRegionCount, stats->_rememberedSetStableRegionCount, stats->_rememberedSetBeingRebuiltRegionCount);
}

void
MM_VerboseHandlerOutputVLHGC::outputRememberedSetClearedInfo(MM_EnvironmentBase *env, MM_InterRegionRememberedSetStats *irrsStats)
{
	_manager->getWriterChain()->formatAndOutput(env, 1, "<remembered-set-cleared processed=\"%zu\" cleared=\"%zu\" durationms=\"%llu.%03.3llu\" />",
			irrsStats->_clearFromRegionReferencesCardsProcessed,
			irrsStats->_clearFromRegionReferencesCardsCleared,
			irrsStats->_clearFromRegionReferencesTimesus / 1000, irrsStats->_clearFromRegionReferencesTimesus % 1000);
}

void
MM_VerboseHandlerOutputVLHGC::handleCopyForwardStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
}

void
MM_VerboseHandlerOutputVLHGC::handleCopyForwardEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_CopyForwardEndEvent * event = (MM_CopyForwardEndEvent *)eventData;
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env->getOmrVM());
	MM_CopyForwardStats *copyForwardStats = (MM_CopyForwardStats *)event->copyForwardStats;
	MM_WorkPacketStats *workPacketStats = (MM_WorkPacketStats *)event->workPacketStats;
	MM_InterRegionRememberedSetStats *irrsStats = (MM_InterRegionRememberedSetStats *)event->irrsStats;
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 totalTime = 0;
	bool totalTimeSuccess = getTimeDeltaInMicroSeconds(&totalTime, copyForwardStats->_startTime, copyForwardStats->_endTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), _manager->getIdAndIncrement(), "copy forward", env->_cycleState->_verboseContextID, totalTime, j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!totalTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}	
	writer->formatAndOutput(env, 0, "<gc-op %s>", tagTemplate);

	writer->formatAndOutput(env, 1, "<memory-copied type=\"eden\" objects=\"%zu\" bytes=\"%zu\" bytesdiscarded=\"%zu\" />",
				copyForwardStats->_copyObjectsEden, copyForwardStats->_copyBytesEden, copyForwardStats->_copyDiscardBytesEden);
	writer->formatAndOutput(env, 1, "<memory-copied type=\"other\" objects=\"%zu\" bytes=\"%zu\" bytesdiscarded=\"%zu\" />",
				copyForwardStats->_copyObjectsNonEden, copyForwardStats->_copyBytesNonEden, copyForwardStats->_copyDiscardBytesNonEden);
	writer->formatAndOutput(env, 1, "<memory-cardclean objects=\"%zu\" bytes=\"%zu\" />",
				copyForwardStats->_objectsCardClean, copyForwardStats->_bytesCardClean);
	if(copyForwardStats->_aborted || (0 != copyForwardStats->_nonEvacuateRegionCount)) {
		writer->formatAndOutput(env, 1, "<memory-traced type=\"eden\" objects=\"%zu\" bytes=\"%zu\" />",
					copyForwardStats->_scanObjectsEden, copyForwardStats->_scanBytesEden);
		writer->formatAndOutput(env, 1, "<memory-traced type=\"other\" objects=\"%zu\" bytes=\"%zu\" />",
					copyForwardStats->_scanObjectsNonEden, copyForwardStats->_scanBytesNonEden);
	}
	if (0 == copyForwardStats->_nonEvacuateRegionCount) {
		writer->formatAndOutput(env, 1, "<regions eden=\"%zu\" other=\"%zu\" />",
				copyForwardStats->_edenEvacuateRegionCount, copyForwardStats->_nonEdenEvacuateRegionCount);
	} else {
		writer->formatAndOutput(env, 1, "<regions eden=\"%zu\" other=\"%zu\" evacuated=\"%zu\" marked=\"%zu\" />",
				copyForwardStats->_edenEvacuateRegionCount, copyForwardStats->_nonEdenEvacuateRegionCount,
				(copyForwardStats->_edenEvacuateRegionCount + copyForwardStats->_nonEdenEvacuateRegionCount - copyForwardStats->_nonEvacuateRegionCount),
				copyForwardStats->_nonEvacuateRegionCount);
	}
	outputRememberedSetClearedInfo(env, irrsStats);

	outputUnfinalizedInfo(env, 1, copyForwardStats->_unfinalizedCandidates, copyForwardStats->_unfinalizedEnqueued);
	outputOwnableSynchronizerInfo(env, 1, copyForwardStats->_ownableSynchronizerCandidates, (copyForwardStats->_ownableSynchronizerCandidates-copyForwardStats->_ownableSynchronizerSurvived));

	outputReferenceInfo(env, 1, "soft", &copyForwardStats->_softReferenceStats, extensions->getDynamicMaxSoftReferenceAge(), extensions->getMaxSoftReferenceAge());
	outputReferenceInfo(env, 1, "weak", &copyForwardStats->_weakReferenceStats, 0, 0);
	outputReferenceInfo(env, 1, "phantom", &copyForwardStats->_phantomReferenceStats, 0, 0);

	outputStringConstantInfo(env, 1, copyForwardStats->_stringConstantsCandidates, copyForwardStats->_stringConstantsCleared);
	outputMonitorReferenceInfo(env, 1, copyForwardStats->_monitorReferenceCandidates, copyForwardStats->_monitorReferenceCleared);

	if(0 != copyForwardStats->_heapExpandedCount) {
		U_64 expansionMicros = j9time_hires_delta(0, copyForwardStats->_heapExpandedTime, J9PORT_TIME_DELTA_IN_MICROSECONDS);
		outputCollectorHeapResizeInfo(env, 1, HEAP_EXPAND, copyForwardStats->_heapExpandedBytes, copyForwardStats->_heapExpandedCount, MEMORY_TYPE_OLD, SATISFY_COLLECTOR, expansionMicros);
	}
	
	if(copyForwardStats->_scanCacheOverflow) {
		writer->formatAndOutput(env, 1, "<warning details=\"scan cache overflow (storage acquired from heap)\" />");
	}

	if(copyForwardStats->_aborted) {
		writer->formatAndOutput(env, 1, "<warning details=\"operation aborted due to insufficient free space\" />");
	}

	if(workPacketStats->getSTWWorkStackOverflowOccured()) {
		writer->formatAndOutput(env, 1, "<warning details=\"work packet overflow\" count=\"%zu\" packetcount=\"%zu\" />",
				workPacketStats->getSTWWorkStackOverflowCount(), workPacketStats->getSTWWorkpacketCountAtOverflow());
	}

	writer->formatAndOutput(env, 0, "</gc-op>");
	writer->flush(env);
	exitAtomicReportingBlock();
}

void
MM_VerboseHandlerOutputVLHGC::handleConcurrentStartInternal(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_ConcurrentPhaseEndEvent *event = (MM_ConcurrentPhaseEndEvent *)eventData;
	MM_ConcurrentPhaseStatsBase *stats = (MM_ConcurrentPhaseStatsBase *)event->concurrentStats;
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(event->currentThread);

	writer->formatAndOutput(env, 1, "<concurrent-mark-start scanTarget=\"%zu\" />", stats->_scanTargetInBytes);
}

void
MM_VerboseHandlerOutputVLHGC::handleConcurrentEndInternal(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_ConcurrentPhaseEndEvent *event = (MM_ConcurrentPhaseEndEvent *)eventData;
	MM_ConcurrentPhaseStatsBase *stats = (MM_ConcurrentPhaseStatsBase *)event->concurrentStats;
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(event->currentThread);

	uint64_t duration = 0;
	MM_MarkVLHGCStats *markStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._markStats;
	bool deltaTimeSuccess = getTimeDeltaInMicroSeconds(&duration, markStats->_startTime, markStats->_endTime);

	handleGCOPOuterStanzaStart(env, "mark increment", stats->_cycleID, duration, deltaTimeSuccess);

	UDATA bytesScanned = stats->_bytesScanned;
	writer->formatAndOutput(env, 1, "<trace-info scanbytes=\"%zu\" />", bytesScanned);
	handleGCOPOuterStanzaEnd(env);
}

void
MM_VerboseHandlerOutputVLHGC::handleGMPMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	/* No implementation */
}

void
MM_VerboseHandlerOutputVLHGC::handleGMPMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_GMPMarkEndEvent * event = (MM_GMPMarkEndEvent *)eventData;
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_MarkVLHGCStats *markStats = (MM_MarkVLHGCStats *)event->markStats;
	MM_WorkPacketStats *workPacketStats = (MM_WorkPacketStats *)event->workPacketStats;

	outputMarkSummary(env, "mark increment", markStats, workPacketStats, NULL);
}

void
MM_VerboseHandlerOutputVLHGC::handleGlobalGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	/* No implementation */
}

void
MM_VerboseHandlerOutputVLHGC::handleGlobalGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_VLHGCGlobalGCMarkEndEvent * event = (MM_VLHGCGlobalGCMarkEndEvent *)eventData;
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_MarkVLHGCStats *markStats = (MM_MarkVLHGCStats *)event->markStats;
	MM_WorkPacketStats *workPacketStats = (MM_WorkPacketStats *)event->workPacketStats;

	outputMarkSummary(env, "global mark", markStats, workPacketStats, NULL);
}

void
MM_VerboseHandlerOutputVLHGC::handlePGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	/* No implementation */
}

void
MM_VerboseHandlerOutputVLHGC::handlePGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_PGCMarkEndEvent * event = (MM_PGCMarkEndEvent *)eventData;
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_MarkVLHGCStats *markStats = (MM_MarkVLHGCStats *)event->markStats;
	MM_WorkPacketStats *workPacketStats = (MM_WorkPacketStats *)event->workPacketStats;
	MM_InterRegionRememberedSetStats *irrsStats = (MM_InterRegionRememberedSetStats *)event->irrsStats;

	outputMarkSummary(env, "mark", markStats, workPacketStats, irrsStats);
}

void
MM_VerboseHandlerOutputVLHGC::outputMarkSummary(MM_EnvironmentBase *env, const char *markType, MM_MarkVLHGCStats *markStats, MM_WorkPacketStats *workPacketStats, MM_InterRegionRememberedSetStats *irrsStats)
{
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env->getOmrVM());

	PORT_ACCESS_FROM_ENVIRONMENT(env);

	U_64 totalTime = 0;
	bool totalTimeSuccess = getTimeDeltaInMicroSeconds(&totalTime, markStats->_startTime, markStats->_endTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), _manager->getIdAndIncrement(), markType, env->_cycleState->_verboseContextID, totalTime, j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!totalTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}	
	writer->formatAndOutput(env, 0, "<gc-op %s>", tagTemplate);

	writer->formatAndOutput(env, 1, "<trace-info objectcount=\"%zu\" scancount=\"%zu\" scanbytes=\"%zu\" />",
		markStats->_objectsMarked, markStats->_objectsScanned, markStats->_bytesScanned);

	if (0 != markStats->_objectsCardClean) {
		writer->formatAndOutput(env, 1, "<cardclean-info objects=\"%zu\" bytes=\"%zu\" />",
				markStats->_objectsCardClean, markStats->_bytesCardClean);
	}

	if (NULL != irrsStats) {
		/* report only for PGC */
		outputRememberedSetClearedInfo(env, irrsStats);
	}

	outputUnfinalizedInfo(env, 1, markStats->_unfinalizedCandidates, markStats->_unfinalizedEnqueued);
	outputOwnableSynchronizerInfo(env, 1, markStats->_ownableSynchronizerCandidates, markStats->_ownableSynchronizerCleared);

	outputReferenceInfo(env, 1, "soft", &markStats->_softReferenceStats, extensions->getDynamicMaxSoftReferenceAge(), extensions->getMaxSoftReferenceAge());
	outputReferenceInfo(env, 1, "weak", &markStats->_weakReferenceStats, 0, 0);
	outputReferenceInfo(env, 1, "phantom", &markStats->_phantomReferenceStats, 0, 0);

	outputStringConstantInfo(env, 1, markStats->_stringConstantsCandidates, markStats->_stringConstantsCleared);
	outputMonitorReferenceInfo(env, 1, markStats->_monitorReferenceCandidates, markStats->_monitorReferenceCleared);

	switch (env->_cycleState->_reasonForMarkCompactPGC) {
	case MM_CycleState::reason_not_exceptional:
		/* nothing to report */
		break;
	case MM_CycleState::reason_JNI_critical_in_Eden:
		writer->formatAndOutput(env, 1, "<warning details=\"Mark invoked due to active JNI critical regions\" />");
		break;
	case MM_CycleState::reason_calibration:
		writer->formatAndOutput(env, 1, "<warning details=\"Mark for calibration purposes\" />");
		break;
	case MM_CycleState::reason_recent_abort:
		writer->formatAndOutput(env, 1, "<warning details=\"Mark invoked due to recent Copy-Forward abort\" />");
		break;
	case MM_CycleState::reason_insufficient_free_space:
		writer->formatAndOutput(env, 1, "<warning details=\"Mark invoked due to insufficient free space for Copy-Forward\" />");
		break;
	default:
		writer->formatAndOutput(env, 1, "<warning details=\"Unknown reason for Mark-Compact collect: %zu\" />", (UDATA)env->_cycleState->_reasonForMarkCompactPGC);
		break;
	}

	if(workPacketStats->getSTWWorkStackOverflowOccured()) {
		writer->formatAndOutput(env, 1, "<warning details=\"work packet overflow\" count=\"%zu\" packetcount=\"%zu\" />",
				workPacketStats->getSTWWorkStackOverflowCount(), workPacketStats->getSTWWorkpacketCountAtOverflow());
	}

	writer->formatAndOutput(env, 0, "</gc-op>");
	writer->flush(env);
	exitAtomicReportingBlock();
}

void
MM_VerboseHandlerOutputVLHGC::handleReclaimSweepStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	/* No implementation */
}

void
MM_VerboseHandlerOutputVLHGC::handleReclaimSweepEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_ReclaimSweepEndEvent * event = (MM_ReclaimSweepEndEvent *)eventData;
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_SweepVLHGCStats *sweepStats = (MM_SweepVLHGCStats *)event->sweepStats;
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 totalTime = 0;
	bool totalTimeSuccess = getTimeDeltaInMicroSeconds(&totalTime, sweepStats->_startTime, sweepStats->_endTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), _manager->getIdAndIncrement(), "sweep", env->_cycleState->_verboseContextID, totalTime, j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!totalTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}	
	writer->formatAndOutput(env, 0, "<gc-op %s />", tagTemplate);
	writer->flush(env);
	exitAtomicReportingBlock();
}

void
MM_VerboseHandlerOutputVLHGC::handleReclaimCompactStart(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	/* No implementation */
}

void
MM_VerboseHandlerOutputVLHGC::handleReclaimCompactEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_ReclaimCompactEndEvent * event = (MM_ReclaimCompactEndEvent *)eventData;
	MM_VerboseWriterChain* writer = _manager->getWriterChain();
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(event->currentThread);
	MM_CompactVLHGCStats *compactStats = (MM_CompactVLHGCStats *)event->compactStats;
	MM_InterRegionRememberedSetStats *irrsStats = (MM_InterRegionRememberedSetStats *)event->irrsStats;
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 totalTime = 0;
	bool totalTimeSuccess = getTimeDeltaInMicroSeconds(&totalTime, compactStats->_startTime, compactStats->_endTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), _manager->getIdAndIncrement(), "compact", env->_cycleState->_verboseContextID, totalTime, j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!totalTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}
	writer->formatAndOutput(env, 0, "<gc-op %s>", tagTemplate);
	writer->formatAndOutput(env, 1, "<compact-info movecount=\"%zu\" movebytes=\"%zu\" />", compactStats->_movedObjects, compactStats->_movedBytes);

	outputRememberedSetClearedInfo(env, irrsStats);

	writer->formatAndOutput(env, 0, "</gc-op>");
	writer->flush(env);
	exitAtomicReportingBlock();
}

void
MM_VerboseHandlerOutputVLHGC::handleClassUnloadEnd(J9HookInterface** hook, UDATA eventNum, void* eventData)
{
	MM_ClassUnloadingEndEvent* event = (MM_ClassUnloadingEndEvent*)eventData;
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment((OMR_VMThread *)event->currentThread->omrVMThread);
	MM_VerboseManager* manager = getManager();
	MM_VerboseWriterChain* writer = manager->getWriterChain();
	MM_ClassUnloadStats *classUnloadStats = &static_cast<MM_CycleStateVLHGC*>(env->_cycleState)->_vlhgcIncrementStats._classUnloadStats;
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_64 totalTime = 0;
	bool totalTimeSuccess = getTimeDeltaInMicroSeconds(&totalTime, classUnloadStats->_startTime, classUnloadStats->_endTime);

	char tagTemplate[200];
	getTagTemplate(tagTemplate, sizeof(tagTemplate), _manager->getIdAndIncrement(), "classunload", env->_cycleState->_verboseContextID, totalTime, j9time_current_time_millis());
	enterAtomicReportingBlock();
	if (!totalTimeSuccess) {
		writer->formatAndOutput(env, 0, "<warning details=\"clock error detected, following timing may be inaccurate\" />");
	}
	writer->formatAndOutput(env, 0, "<gc-op %s>", tagTemplate);


	UDATA classLoaderUnloadedCount = classUnloadStats->_classLoaderUnloadedCount;
	UDATA classLoaderCandidates = classUnloadStats->_classLoaderCandidates;
	UDATA classesUnloadedCount = classUnloadStats->_classesUnloadedCount;
	UDATA anonymousClassesUnloadedCount = classUnloadStats->_anonymousClassesUnloadedCount;

	U_64 setupTime   = 0;
	bool partialTimeSuccess = getTimeDeltaInMicroSeconds(&setupTime, classUnloadStats->_startSetupTime, classUnloadStats->_endSetupTime);
	U_64 scanTime    = 0;
	partialTimeSuccess = (partialTimeSuccess && getTimeDeltaInMicroSeconds(&scanTime, classUnloadStats->_startScanTime, classUnloadStats->_endScanTime));
	U_64 postTime    = 0;
	partialTimeSuccess = (partialTimeSuccess && getTimeDeltaInMicroSeconds(&postTime, classUnloadStats->_startPostTime, classUnloadStats->_endPostTime));
	/* !!!Note: classUnloadStats->_classUnloadMutexQuiesceTime is in us already, do not convert it again!!!*/
	U_64 quiesceTime = classUnloadStats->_classUnloadMutexQuiesceTime;

	writer->formatAndOutput(
			env, 1,
			"<classunload-info classloadercandidates=\"%zu\" classloadersunloaded=\"%zu\" classesunloaded=\"%zu\" anonymousclassesunloaded=\"%zu\" quiescems=\"%llu.%03.3llu\" setupms=\"%llu.%03.3llu\" scanms=\"%llu.%03.3llu\" postms=\"%llu.%03.3llu\" />",
			classLoaderCandidates, classLoaderUnloadedCount, classesUnloadedCount, anonymousClassesUnloadedCount,
			quiesceTime / 1000, quiesceTime % 1000,
			setupTime / 1000, setupTime % 1000,
			scanTime / 1000, scanTime % 1000,
			postTime / 1000, postTime % 1000);

	if (!partialTimeSuccess) {
		writer->formatAndOutput(env, 1, "<warning details=\"clock error detected, previous timing may be inaccurate\" />");
	}

	writer->formatAndOutput(env, 0, "</gc-op>");
	writer->flush(env);
	exitAtomicReportingBlock();
}

const char *
MM_VerboseHandlerOutputVLHGC::getCycleType(UDATA type)
{
	const char *cycleType = NULL;

	switch (type) {
	case OMR_GC_CYCLE_TYPE_VLHGC_PARTIAL_GARBAGE_COLLECT:
		cycleType = "partial gc";
		break;
	case OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_MARK_PHASE:
		cycleType = "global mark phase";
		break;
	case OMR_GC_CYCLE_TYPE_VLHGC_GLOBAL_GARBAGE_COLLECT:
		cycleType = "global garbage collect";
		break;
	default:
		cycleType = "unknown";
		break;
	}

	return cycleType;
}

const char *
MM_VerboseHandlerOutputVLHGC::getConcurrentTerminationReason(MM_ConcurrentPhaseStatsBase *stats)
{
	const char *reasonForTermination = NULL;
	UDATA bytesScanned = stats->_bytesScanned;
	bool didComplete = bytesScanned >= stats->_scanTargetInBytes;

	if (stats->_terminationWasRequested) {
		if (didComplete) {
			reasonForTermination = "Work target met and termination requested";
		} else {
			reasonForTermination = "Termination requested";
		}
	} else {
		if (didComplete) {
			reasonForTermination = "Work target met";
		} else {
			reasonForTermination = "Completed all work in GC phase";
		}
	}

	return reasonForTermination;
}

void
verboseHandlerTaxationEntryPoint(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleTaxationEntryPoint(hook, eventNum, eventData);
}

void
verboseHandlerGCStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleGCStart(hook, eventNum, eventData);
}

void
verboseHandlerGCEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleGCEnd(hook, eventNum, eventData);
}

void
verboseHandlerCycleStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleCycleStart(hook, eventNum, eventData);
}

void
verboseHandlerCycleContinue(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleCycleContinue(hook, eventNum, eventData);
}

void
verboseHandlerCycleEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleCycleEnd(hook, eventNum, eventData);
}

void
verboseHandlerExclusiveStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput*)userData)->handleExclusiveStart(hook, eventNum, eventData);
}

void
verboseHandlerExclusiveEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput*)userData)->handleExclusiveEnd(hook, eventNum, eventData);
}

void
verboseHandlerSystemGCStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleSystemGCStart(hook, eventNum, eventData);
}

void
verboseHandlerSystemGCEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleSystemGCEnd(hook, eventNum, eventData);
}

void
verboseHandlerAllocationFailureStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleAllocationFailureStart(hook, eventNum, eventData);
}

void
verboseHandlerFailedAllocationCompleted(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleFailedAllocationCompleted(hook, eventNum, eventData);
}

void
verboseHandlerAllocationFailureEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleAllocationFailureEnd(hook, eventNum, eventData);
}

void
verboseHandlerCopyForwardStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleCopyForwardStart(hook, eventNum, eventData);
}

void
verboseHandlerCopyForwardEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleCopyForwardEnd(hook, eventNum, eventData);
}

void
verboseHandlerConcurrentStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleConcurrentStart(hook, eventNum, eventData);
}

void
verboseHandlerConcurrentEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutput *)userData)->handleConcurrentEnd(hook, eventNum, eventData);
}

void
verboseHandlerGMPMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleGMPMarkStart(hook, eventNum, eventData);
}

void
verboseHandlerGMPMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleGMPMarkEnd(hook, eventNum, eventData);
}

void
verboseHandlerGlobalGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleGlobalGCMarkStart(hook, eventNum, eventData);
}

void
verboseHandlerGlobalGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleGlobalGCMarkEnd(hook, eventNum, eventData);
}

void
verboseHandlerPGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handlePGCMarkStart(hook, eventNum, eventData);
}

void
verboseHandlerPGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handlePGCMarkEnd(hook, eventNum, eventData);
}

void
verboseHandlerReclaimSweepStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleReclaimSweepStart(hook, eventNum, eventData);
}

void
verboseHandlerReclaimSweepEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleReclaimSweepEnd(hook, eventNum, eventData);
}

void
verboseHandlerReclaimCompactStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleReclaimCompactStart(hook, eventNum, eventData);
}

void
verboseHandlerReclaimCompactEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleReclaimCompactEnd(hook, eventNum, eventData);
}

void
verboseHandlerExcessiveGCRaised(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleExcessiveGCRaised(hook, eventNum, eventData);
}

void verboseHandlerAcquiredExclusiveToSatisfyAllocation(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleAcquiredExclusiveToSatisfyAllocation(hook, eventNum, eventData);
}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
void verboseHandlerClassUnloadingEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	((MM_VerboseHandlerOutputVLHGC *)userData)->handleClassUnloadEnd(hook, eventNum, eventData);
}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */


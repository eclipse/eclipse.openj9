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
#include "j9port.h"
#include "modronopt.h"
#include "mmhook.h"
#include "ModronAssertions.h"

#if defined(J9VM_GC_VLHGC)
#include "CycleStateVLHGC.hpp"
#endif /* J9VM_GC_VLHGC */
#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"
#include "TgcExtensions.hpp"
#include "VMThreadListIterator.hpp"

#if defined(J9VM_GC_MODRON_SCAVENGER)
#include "Scavenger.hpp"
#endif /* OMR_GC_MODRON_SCAVENGER */

/****************************************
 * Hook callbacks
 ****************************************
 */

static void
tgcHookGlobalGcEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_GlobalGCEndEvent* event = (MM_GlobalGCEndEvent*)eventData;
	J9VMThread* vmThread = (J9VMThread*)event->currentThread->_language_vmthread;
	J9JavaVM *javaVM = vmThread->javaVM;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;
	PORT_ACCESS_FROM_JAVAVM(javaVM);


	uint64_t RSScanTotalTime = parallelExtensions->RSScanEndTime - parallelExtensions->RSScanStartTime;

	if (0 != RSScanTotalTime) {
		tgcExtensions->printf("RS  :   busy  stall  acquire   release  exchange \n");

		GC_VMThreadListIterator markThreadListIterator(vmThread);
		J9VMThread *walkThread = NULL;
		while ((walkThread = markThreadListIterator.nextVMThread()) != NULL) {
			MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(walkThread->omrVMThread);
			if ((walkThread == vmThread) || (env->getThreadType() == GC_WORKER_THREAD)) {
				bool shouldIncludeThread = true;
				if (extensions->isStandardGC()) {
#if defined(J9VM_GC_MODRON_STANDARD)
					/* Concurrent RS Scan Task does not have its own stats struct,
					 * so rely on Mark Task stats gcCount, which is atm identical (all phases of one GC are run with same number of threads)  */
					shouldIncludeThread = env->_markStats._gcCount == extensions->globalGCStats.gcCount;
#endif /* defined(J9VM_GC_MODRON_STANDARD) */
				}
				
				/* Get RS busy and stall times and convert time from microseconds to milliseconds for reporting */
				uint64_t rsStallTimeInMillis = ((uint64_t)(env->_workPacketStats.getStallTime())) / 1000;
				uint64_t rsBusyTimeInMillis =  (RSScanTotalTime / 1000) - rsStallTimeInMillis;

				if (shouldIncludeThread) {
					tgcExtensions->printf("%4zu:  %5llu  %5llu   %5zu     %5zu     %5zu\n",
						env->getWorkerID(),
						rsBusyTimeInMillis,
						rsStallTimeInMillis,
						env->_workPacketStatsRSScan.workPacketsAcquired,
						env->_workPacketStatsRSScan.workPacketsReleased,
						env->_workPacketStatsRSScan.workPacketsExchanged);
				}
			}
		}
	}

	uint64_t markTotalTime = parallelExtensions->markEndTime - parallelExtensions->markStartTime;

	if (0 != markTotalTime) {
		tgcExtensions->printf("Mark:   busy  stall  acquire   release  exchange split array  split size\n");

		GC_VMThreadListIterator markThreadListIterator(vmThread);
		J9VMThread *walkThread = NULL;
		while ((walkThread = markThreadListIterator.nextVMThread()) != NULL) {
			/* TODO: Are we guaranteed to get the threads in the right order? */
			MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(walkThread);
			if ((walkThread == vmThread) || (env->getThreadType() == GC_WORKER_THREAD)) {
				uint64_t markStatsStallTime = 0;
				intptr_t splitArraysProcessed = 0;
				intptr_t splitArraysAmount = 0;
				bool shouldIncludeThread = true;
				if (extensions->isVLHGC()) {
#if defined(J9VM_GC_VLHGC)
					markStatsStallTime = env->_markVLHGCStats.getStallTime();
					splitArraysProcessed = env->_markVLHGCStats._splitArraysProcessed;
					shouldIncludeThread = env->_markVLHGCStats._gcCount == extensions->globalVLHGCStats.gcCount;
#endif /* J9VM_GC_VLHGC */
				} else if (extensions->isStandardGC()) {
#if defined(J9VM_GC_MODRON_STANDARD)
					markStatsStallTime = env->_markStats.getStallTime();
					splitArraysProcessed = env->getGCEnvironment()->_markJavaStats.splitArraysProcessed;
					splitArraysAmount =  env->getGCEnvironment()->_markJavaStats.splitArraysAmount;
					shouldIncludeThread = env->_markStats._gcCount == extensions->globalGCStats.gcCount;
#endif /* defined(J9VM_GC_MODRON_STANDARD) */
				}

				if (shouldIncludeThread) {
					intptr_t avgSplitSize = 0;
					if (0 != splitArraysProcessed) {
						avgSplitSize = splitArraysAmount / splitArraysProcessed;
					}
					tgcExtensions->printf("%4zu:  %5llu  %5llu    %5zu     %5zu     %5zu       %5zu     %7zu\n",
						env->getWorkerID(),
						j9time_hires_delta(0, markTotalTime - (markStatsStallTime + env->_workPacketStats.getStallTime()), J9PORT_TIME_DELTA_IN_MILLISECONDS),
						j9time_hires_delta(0, markStatsStallTime + env->_workPacketStats.getStallTime(), J9PORT_TIME_DELTA_IN_MILLISECONDS),
						env->_workPacketStats.workPacketsAcquired,
						env->_workPacketStats.workPacketsReleased,
						env->_workPacketStats.workPacketsExchanged,
						splitArraysProcessed,
						avgSplitSize);
				}
	
				/* TODO: VLHGC doesn't record gc count yet, allowing us to determine if thread participated */			
				if (extensions->isVLHGC()) {
					/* TODO: Must reset thread-local stats after using them -- is there another answer? */
					/* When it becomes possible for a GC to not use all of the worker threads, this becomes
					 * necessary, otherwise we might use outdated stats.
					 */
					env->_workPacketStats.clear();
				}
				
				parallelExtensions->markStartTime = 0;
				parallelExtensions->markEndTime = 0;
			}
		}
	}

	uint64_t sweepTotalTime = parallelExtensions->sweepEndTime - parallelExtensions->sweepStartTime;

	if (0 != sweepTotalTime) {
		intptr_t mainSweepChunksTotal = 0;
		uint64_t mainSweepMergeTime = 0;
		if (extensions->isVLHGC()) {
#if defined(J9VM_GC_VLHGC)
			MM_EnvironmentVLHGC *mainEnv = MM_EnvironmentVLHGC::getEnvironment(vmThread);
			mainSweepChunksTotal = mainEnv->_sweepVLHGCStats.sweepChunksTotal;
			mainSweepMergeTime = mainEnv->_sweepVLHGCStats.mergeTime;
#endif /* J9VM_GC_VLHGC */
		} else if (extensions->isStandardGC()) {
#if defined(J9VM_GC_MODRON_STANDARD)
			MM_EnvironmentBase *mainEnv = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
			mainSweepChunksTotal = mainEnv->_sweepStats.sweepChunksTotal;
			mainSweepMergeTime = mainEnv->_sweepStats.mergeTime;
#endif /* defined(J9VM_GC_MODRON_STANDARD) */
		}

		tgcExtensions->printf("Sweep:  busy   idle sections %zu  merge %llu\n",
			mainSweepChunksTotal,
			j9time_hires_delta(0, mainSweepMergeTime, J9PORT_TIME_DELTA_IN_MILLISECONDS));


		GC_VMThreadListIterator sweepThreadListIterator(vmThread);
		J9VMThread *walkThread = NULL;
		while ((walkThread = sweepThreadListIterator.nextVMThread()) != NULL) {
			/* TODO: Are we guaranteed to get the threads in the right order? */
			MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(walkThread);
			if ((walkThread == vmThread) || (env->getThreadType() == GC_WORKER_THREAD)) {
				uint64_t sweepIdleTime = 0;
				intptr_t sweepChunksProcessed = 0;
				bool shouldIncludeThread = true;
				if (extensions->isVLHGC()) {
#if defined(J9VM_GC_VLHGC)
					sweepIdleTime = env->_sweepVLHGCStats.idleTime;
					sweepChunksProcessed = env->_sweepVLHGCStats.sweepChunksProcessed;
					shouldIncludeThread = env->_sweepVLHGCStats._gcCount == extensions->globalVLHGCStats.gcCount;
#endif /* J9VM_GC_VLHGC */
				} else if (extensions->isStandardGC()) {
#if defined(J9VM_GC_MODRON_STANDARD)
					sweepIdleTime = env->_sweepStats.idleTime;
					sweepChunksProcessed = env->_sweepStats.sweepChunksProcessed;
					shouldIncludeThread = env->_sweepStats._gcCount == extensions->globalGCStats.gcCount;
#endif /* defined(J9VM_GC_MODRON_STANDARD) */
				}

				parallelExtensions->sweepStartTime = 0;
				parallelExtensions->sweepEndTime = 0;

				if (shouldIncludeThread) {
					tgcExtensions->printf("%4zu: %6llu %6llu %8zu\n",
						env->getWorkerID(),
						j9time_hires_delta(0, sweepTotalTime - sweepIdleTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
						j9time_hires_delta(0, sweepIdleTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
						sweepChunksProcessed);
				}
			}
		}
	}
}

static void
tgcHookGlobalGcMarkStart(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_MarkStartEvent* event = (MM_MarkStartEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;	
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->markStartTime = omrtime_hires_clock();
}

static void
tgcHookGlobalGcMarkEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_MarkEndEvent* event = (MM_MarkEndEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;	
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->markEndTime = omrtime_hires_clock();
}

#if defined(J9VM_GC_MODRON_SCAVENGER)
static void
tgcHookLocalGcEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_LocalGCStartEvent* event = (MM_LocalGCStartEvent*)eventData;
	J9VMThread* vmThread = (J9VMThread*)event->currentThread->_language_vmthread;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vmThread->javaVM);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);

	J9VMThread *walkThread;
	uint64_t scavengeTotalTime;
	PORT_ACCESS_FROM_VMC(vmThread);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	char timestamp[32];
	int64_t timeInMillis = j9time_current_time_millis();
	omrstr_ftime_ex(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeInMillis, OMRSTR_FTIME_FLAG_LOCAL);
	tgcExtensions->printf("\n");
	tgcExtensions->printf("Scavenger parallel and progress stats, timestamp=\"%s.%ld\"\n", timestamp, timeInMillis % 1000);

	tgcExtensions->printf("          gc thrID     busy    stall   acquire   release   acquire   release   acquire     split avg split  alias to    deep      total deepest\n");
	tgcExtensions->printf("                   (micros) (micros)  freelist  freelist  scanlist  scanlist      lock    arrays arraysize copycache   lists  deep objs    list\n");

	scavengeTotalTime = extensions->scavenger->_cycleEndTime - extensions->scavenger->_cycleStartTime;
	uintptr_t gcCount = extensions->scavengerStats._gcCount;

	GC_VMThreadListIterator scavengeThreadListIterator(vmThread);
	while ((walkThread = scavengeThreadListIterator.nextVMThread()) != NULL) {
		/* TODO: Are we guaranteed to get the threads in the right order? */
		MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(walkThread->omrVMThread);
		if ((walkThread == vmThread) || (env->getThreadType() == GC_WORKER_THREAD)) {
			/* check if this thread participated in the GC */ 
			if (env->_scavengerStats._gcCount == extensions->scavengerStats._gcCount) {
				intptr_t avgArraySplitAmount = 0;
				if (0 != env->_scavengerStats._arraySplitCount) {
					avgArraySplitAmount = env->_scavengerStats._arraySplitAmount / env->_scavengerStats._arraySplitCount;
				}
				tgcExtensions->printf("SCV.T %6zu  %4zu %8llu %8llu     %5zu     %5zu     %5zu     %5zu     %5zu     %5zu     %5zu   %7zu  %6zu     %6zu  %6zu \n",
					gcCount,
					env->getWorkerID(),
					j9time_hires_delta(0, scavengeTotalTime - env->_scavengerStats.getStallTime(), J9PORT_TIME_DELTA_IN_MICROSECONDS),
					j9time_hires_delta(0, env->_scavengerStats.getStallTime(), J9PORT_TIME_DELTA_IN_MICROSECONDS),
					env->_scavengerStats._acquireFreeListCount,
					env->_scavengerStats._releaseFreeListCount,
					env->_scavengerStats._acquireScanListCount,
					env->_scavengerStats._releaseScanListCount,
					env->_scavengerStats._acquireListLockCount,
					env->_scavengerStats._arraySplitCount,
					avgArraySplitAmount,
					env->_scavengerStats._aliasToCopyCacheCount,
					env->_scavengerStats._totalDeepStructures,
					env->_scavengerStats._totalObjsDeepScanned,
					env->_scavengerStats._depthDeepestStructure);
			}
		}
	}

	tgcExtensions->printf("\n");
	tgcExtensions->printf("          gc micros   idle   busy active  lists   caches   copied  scanned updates scaling");
	if (extensions->isConcurrentScavengerEnabled()) {
		tgcExtensions->printf("  rb-copy rb-update\n");
	} else {
		tgcExtensions->printf("\n");
	}

	uintptr_t recordCount = 0;
	uintptr_t totalRecordUpdates = 0;
	MM_ScavengerCopyScanRatio::UpdateHistory *historyRecord = extensions->copyScanRatio.getHistory(&recordCount);
	MM_ScavengerCopyScanRatio::UpdateHistory *endRecord = historyRecord + recordCount;
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	uint64_t prevReadObjectBarrierCopy = 0;
	uint64_t prevReadObjectBarrierUpdate = 0;
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
	while (historyRecord < endRecord) {
		totalRecordUpdates += historyRecord->updates;
		uint64_t elapsedMicros = extensions->copyScanRatio.getSpannedMicros(env, historyRecord);
		double majorUpdates = (double)historyRecord->majorUpdates;
		double lists = (double)historyRecord->lists / majorUpdates;
		double caches = (double)historyRecord->caches / majorUpdates;
		double threads = (double)historyRecord->threads / majorUpdates;
		double waitingThreads = (double)historyRecord->waits / (double)historyRecord->updates;
		double busyThreads = threads - waitingThreads;
		double scalingFactor = extensions->copyScanRatio.getScalingFactor(env, historyRecord);
		tgcExtensions->printf("SCV.H %6zu %6zu %6.1f %6.1f %6.1f %6.1f   %6.1f %8zu %8zu %7zu  %0.4f",
				gcCount, elapsedMicros, waitingThreads, busyThreads, threads, lists, caches,
				historyRecord->copied, historyRecord->scanned, historyRecord->updates, scalingFactor);
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
		if (extensions->isConcurrentScavengerEnabled()) {
			tgcExtensions->printf("  %7zu %9zu\n", historyRecord->readObjectBarrierCopy - prevReadObjectBarrierCopy, historyRecord->readObjectBarrierUpdate - prevReadObjectBarrierUpdate);
			prevReadObjectBarrierCopy = historyRecord->readObjectBarrierCopy;
			prevReadObjectBarrierUpdate = historyRecord->readObjectBarrierUpdate;
		} else
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
		{
			tgcExtensions->printf("\n");
		}
		historyRecord += 1;
	}

	tgcExtensions->printf("\n");
	tgcExtensions->printf("     \tgc\tleaf\talias\tsync-#\tsync-ms\twork-#\twork-ms\tend-#\tend-ms\tscaling\tupdates(major)  overflow  missed(minor)\n");
	tgcExtensions->printf("SCV.M\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%.3f\t%lu\t\t%lu\t  %lu\n", gcCount,
		extensions->scavengerStats._leafObjectCount, extensions->scavengerStats._aliasToCopyCacheCount,
		extensions->scavengerStats._syncStallCount, j9time_hires_delta(0, extensions->scavengerStats._syncStallTime, J9PORT_TIME_DELTA_IN_MICROSECONDS),
		extensions->scavengerStats._workStallCount, j9time_hires_delta(0, extensions->scavengerStats._workStallTime, J9PORT_TIME_DELTA_IN_MICROSECONDS),
		extensions->scavengerStats._completeStallCount, j9time_hires_delta(0, extensions->scavengerStats._completeStallTime, J9PORT_TIME_DELTA_IN_MICROSECONDS),
		extensions->copyScanRatio.getScalingFactor(env), extensions->copyScanRatio.getScalingUpdateCount(), extensions->copyScanRatio.getOverflowCount(), (extensions->scavengerStats._copyScanUpdates -  totalRecordUpdates)
	);

	tgcExtensions->printf("\n");
	tgcExtensions->printf("SCV.D\t%lu", gcCount);
	for (uint32_t i = 0; i < OMR_SCAVENGER_DISTANCE_BINS; i++) {
		tgcExtensions->printf("\t%lu", extensions->scavengerStats._copy_distance_counts[i]);
	}
	tgcExtensions->printf("\n");

	tgcExtensions->printf("\n");
	tgcExtensions->printf("SCV.C\t%lu", gcCount);
	for (uint32_t i = 0; i < 16; i++) {
		tgcExtensions->printf("\t%lu", extensions->scavengerStats._copy_cachesize_counts[i]);
	}
	tgcExtensions->printf("\t%lu\n", extensions->scavengerStats._copy_cachesize_sum);
}
#endif /* J9VM_GC_MODRON_SCAVENGER */

#if defined(J9VM_GC_VLHGC)
static void
tgcHookCopyForwardEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_LocalGCStartEvent* event = (MM_LocalGCStartEvent*)eventData;
	J9VMThread* vmThread = (J9VMThread*)event->currentThread->_language_vmthread;
	MM_EnvironmentVLHGC *mainEnv = MM_EnvironmentVLHGC::getEnvironment(vmThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(vmThread);

	J9VMThread *walkThread;
	uint64_t copyForwardTotalTime;
	PORT_ACCESS_FROM_VMC(vmThread);

	tgcExtensions->printf("CP-FW:  total           | rem-set | copy                                                             | mark\n");
	tgcExtensions->printf("        busy    stall   | stall   | stall   acquire   release   acquire   release    split terminate | stall   acquire   release   exchange   split\n");
	tgcExtensions->printf("         (ms)    (ms)   |  (ms)   |  (ms)   freelist  freelist  scanlist  scanlist   arrays   (ms)   |  (ms)   packets   packets   packets    arrays\n");

	MM_CopyForwardStats *copyForwardStats = &static_cast<MM_CycleStateVLHGC*>(mainEnv->_cycleState)->_vlhgcIncrementStats._copyForwardStats;
	copyForwardTotalTime = copyForwardStats->_endTime - copyForwardStats->_startTime;

	GC_VMThreadListIterator threadListIterator(vmThread);
	while ((walkThread = threadListIterator.nextVMThread()) != NULL) {
		/* TODO: Are we guaranteed to get the threads in the right order? */
		MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(walkThread);
		if ((walkThread == vmThread) || (env->getThreadType() == GC_WORKER_THREAD)) {
			if (env->_copyForwardStats._gcCount == MM_GCExtensions::getExtensions(env)->globalVLHGCStats.gcCount) {
				uint64_t totalStallTime = env->_copyForwardStats.getStallTime() + env->_workPacketStats.getStallTime();
				tgcExtensions->printf("%4zu:   %5llu   %5llu     %5llu     %5llu    %5zu     %5zu     %5zu     %5zu    %5zu    %5llu     %5llu    %5zu     %5zu     %5zu     %5zu\n",
					env->getWorkerID(),
					j9time_hires_delta(0, copyForwardTotalTime - totalStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
					j9time_hires_delta(0, totalStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
					j9time_hires_delta(0, env->_copyForwardStats._irrsStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
					j9time_hires_delta(0, env->_copyForwardStats.getCopyForwardStallTime(), J9PORT_TIME_DELTA_IN_MILLISECONDS),
					env->_copyForwardStats._acquireFreeListCount,
					env->_copyForwardStats._releaseFreeListCount,
					env->_copyForwardStats._acquireScanListCount,
					env->_copyForwardStats._releaseScanListCount,
					env->_copyForwardStats._copiedArraysSplit,
					j9time_hires_delta(0, env->_copyForwardStats._abortStallTime, J9PORT_TIME_DELTA_IN_MILLISECONDS),
					j9time_hires_delta(0, env->_copyForwardStats._markStallTime + env->_workPacketStats.getStallTime(), J9PORT_TIME_DELTA_IN_MILLISECONDS),
					env->_workPacketStats.workPacketsAcquired,
					env->_workPacketStats.workPacketsReleased,
					env->_workPacketStats.workPacketsExchanged,
					env->_copyForwardStats._markedArraysSplit);
			}
		}
	}
}
#endif /* J9VM_GC_VLHGC */


static void
tgcHookGlobalGcSweepStart(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_SweepStartEvent* event = (MM_SweepStartEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;	
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->sweepStartTime = omrtime_hires_clock();
}

static void
tgcHookGlobalGcSweepEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_SweepEndEvent* event = (MM_SweepEndEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;	
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->sweepEndTime = omrtime_hires_clock();
}

static void
tgcHookConcurrentRSStart(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_ConcurrentRememberedSetScanStartEvent* event = (MM_ConcurrentRememberedSetScanStartEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->RSScanStartTime = omrtime_hires_clock();
}

static void
tgcHookConcurrentRSEnd(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData)
{
	MM_ConcurrentRememberedSetScanEndEvent* event = (MM_ConcurrentRememberedSetScanEndEvent*)eventData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(event->currentThread);
	MM_TgcExtensions *tgcExtensions = MM_TgcExtensions::getExtensions(extensions);
	TgcParallelExtensions *parallelExtensions = &tgcExtensions->_parallel;
	OMRPORT_ACCESS_FROM_OMRVMTHREAD(event->currentThread);

	parallelExtensions->RSScanEndTime = omrtime_hires_clock();
}


/****************************************
 * Initialization
 ****************************************
 */

bool
tgcParallelInitialize(J9JavaVM *javaVM) 
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	bool result = true;

	J9HookInterface** omrHooks = J9_HOOK_INTERFACE(extensions->omrHookInterface);
	J9HookInterface** privateHooks = J9_HOOK_INTERFACE(extensions->privateHookInterface);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_MARK_START, tgcHookGlobalGcMarkStart, OMR_GET_CALLSITE(), NULL);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_MARK_END, tgcHookGlobalGcMarkEnd, OMR_GET_CALLSITE(), NULL);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_SWEEP_START, tgcHookGlobalGcSweepStart, OMR_GET_CALLSITE(), NULL);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_SWEEP_END, tgcHookGlobalGcSweepEnd, OMR_GET_CALLSITE(), NULL);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_REMEMBERED_SET_SCAN_START, tgcHookConcurrentRSStart, OMR_GET_CALLSITE(), NULL);
	(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_CONCURRENT_REMEMBERED_SET_SCAN_END, tgcHookConcurrentRSEnd, OMR_GET_CALLSITE(), NULL);

	if (extensions->isVLHGC()) {
#if defined(J9VM_GC_VLHGC)
		(*privateHooks)->J9HookRegisterWithCallSite(privateHooks, J9HOOK_MM_PRIVATE_COPY_FORWARD_END, tgcHookCopyForwardEnd, OMR_GET_CALLSITE(), NULL);
#endif /* J9VM_GC_VLHGC*/
	}
	(*omrHooks)->J9HookRegisterWithCallSite(omrHooks, J9HOOK_MM_OMR_GLOBAL_GC_END, tgcHookGlobalGcEnd, OMR_GET_CALLSITE(), NULL);
	if (extensions->isStandardGC()) {
#if defined(J9VM_GC_MODRON_SCAVENGER)
		(*omrHooks)->J9HookRegisterWithCallSite(omrHooks, J9HOOK_MM_OMR_LOCAL_GC_END, tgcHookLocalGcEnd, OMR_GET_CALLSITE(), NULL);
#endif /* J9VM_GC_MODRON_SCAVENGER */
	}

	return result;
}

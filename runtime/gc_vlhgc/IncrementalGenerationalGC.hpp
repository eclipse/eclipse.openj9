
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

#if !defined(INCREMENTALGENERATIONALGC_HPP_)
#define INCREMENTALGENERATIONALGC_HPP_

#include "j9.h"
#include "j9cfg.h"
#include "modronopt.h"

#include "CollectionSetDelegate.hpp"
#include "CollectionStatisticsVLHGC.hpp"
#include "ConcurrentPhaseStatsBase.hpp"
#include "CopyForwardDelegate.hpp"
#include "CycleState.hpp"
#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"
#include "GlobalCollector.hpp"
#include "GlobalMarkDelegate.hpp"
#include "MainGCThread.hpp"
#include "ModronTypes.hpp"
#include "ProjectedSurvivalCollectionSetDelegate.hpp"
#include "ReclaimDelegate.hpp"
#include "SchedulingDelegate.hpp"

class MM_EnvironmentBase;
class MM_HeapRegionDescriptorVLHGC;
class MM_MarkMap;
class MM_MarkMapManager;
class MM_MemorySubSpace;
class MM_MemorySubSpaceTarok;
class MM_WorkPacketsVLHGC;

/**
 * Multi-threaded mark and sweep global collector.
 * @ingroup GC_Modron_Standard
 */
class MM_IncrementalGenerationalGC : public MM_GlobalCollector
{
protected:
	J9JavaVM *_javaVM;
	MM_GCExtensions *_extensions;

private:
	J9PortLibrary *_portLibrary;
	
	MM_HeapRegionManager *_regionManager;	/**< The manager which will be walked to configure the collection flags on regions in the heap */
	
	MM_MemorySubSpaceTarok *_configuredSubspace;	/**< The subspace which is configured in the system and triggers early AF - must be reset after a collection to allow the mutator to proceed */
	
	MM_MarkMapManager *_markMapManager;  /**< Tracking and managing all mark map assets */

	MM_InterRegionRememberedSet *_interRegionRememberedSet;	/**< An abstraction layer over the tracking of inter-region references which is built by marking and used/updated by compact */

	MM_ClassLoaderRememberedSet *_classLoaderRememberedSet;	/**< An abstraction layer over the tracking where instances defined by each class loader can be found*/
	
	MM_CopyForwardDelegate _copyForwardDelegate;  /**< Delegate responsible for handling copy and forward GC operations */

	MM_GlobalMarkDelegate _globalMarkDelegate; /**< Delegate for marking in global collections */

	MM_ReclaimDelegate _reclaimDelegate;
	
	MM_SchedulingDelegate _schedulingDelegate;
	
	MM_CollectionSetDelegate _collectionSetDelegate;
	MM_ProjectedSurvivalCollectionSetDelegate _projectedSurvivalCollectionSetDelegate;	/** Collection set delegate based on using a region's projected survival rate in order to select collection set */

	MM_CollectionStatisticsVLHGC _globalCollectionStatistics;	 /** Common collect stats (memory, time etc.), specifically for Global collects */
	MM_CollectionStatisticsVLHGC _partialCollectionStatistics;	 /** Common collect stats (memory, time etc.), specifically for Partial collects */
	
	MM_ConcurrentPhaseStatsBase _concurrentPhaseStats; /**< GMP concurrent stats */

	MM_WorkPacketsVLHGC *_workPacketsForPartialGC; /**< WorkPackets used by Partial Mark-Sweep Collector */
	MM_WorkPacketsVLHGC *_workPacketsForGlobalGC; /**< WorkPackets used by Global Mark-Sweep Collector */

	UDATA _taxationThreshold;	/**< The number of bytes which can be allocated between taxation points */
	UDATA _allocatedSinceLastPGC;	/**< The number of bytes which can be allocated between PGCs */

	MM_MainGCThread _mainGCThread; /**< An object which manages the state of the main GC thread */ 
	
	MM_CycleStateVLHGC _persistentGlobalMarkPhaseState; /**< Since the GMP can be fragmented into increments running across several pauses, we need to store the cycle state data */
	volatile bool _forceConcurrentTermination;	/**< Setting this to true will cause any concurrent GMP work being done for this collector to stop and return.  It is volatile because it is shared state between this and the concurrent task's increment manager */
	
	UDATA _globalMarkPhaseIncrementBytesStillToScan;	/**< The number of bytes which must be scanned in the next GMP increment.  This is used by the concurrent GMP task to determine when it can terminate */

private:
	/* hook routines to be called on AF start and End */
	static void globalGCHookAFCycleStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
	static void globalGCHookAFCycleEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
	static void globalGCHookSysStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
	static void globalGCHookSysEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
	static void globalGCHookIncrementStart(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData); 
	static void globalGCHookIncrementEnd(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData); 

	/**
	 * Called after an operation which has completed the env's mark map (either a GMP completed, a global mark
	 * completed, a partial mark completed, or a copy-forward completed) so that operations which rely on a
	 * mark map can be performed (class unloading and finalization).
	 * 
	 * @param env[in] The main GC thread
	 */
	void postMarkMapCompletion(MM_EnvironmentVLHGC *env);

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/**
	 * Examine and unload class loaders post mark.
	 * Note that this event will report a class unloading event but it might not actually do any unloading work
	 * since this routine also checks if there is any work (that is, it should be called as long as class unloading
	 * is enabled).
	 * In the case where no work is done, the event will only list the number of candidate classloaders evaluated
	 * and a setup time which will only include how long it took to count those loaders.  It is also worth noting
	 * that the JIT will not be quiesced, via the class unload mutex, if there are no classloaders to unload.
	 * @param env[in] The Main GC thread
	 */
	void unloadDeadClassLoaders(MM_EnvironmentVLHGC *env);

	/**
	 * Tooling report the start of an unload phase.
	 */
	void reportClassUnloadingStart(MM_EnvironmentBase *env);

	/**
	 * Tooling report the end of an unload phase.
	 */
	void reportClassUnloadingEnd(MM_EnvironmentBase *env);
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

protected:
	bool initialize(MM_EnvironmentVLHGC *env);
	void tearDown(MM_EnvironmentVLHGC *env);

	virtual	void mainThreadGarbageCollect(MM_EnvironmentBase *env, MM_AllocateDescription *allocDescription, bool initMarkMap = false, bool rebuildMarkBits = false);

	virtual void setupForGC(MM_EnvironmentBase *env);
	virtual bool internalGarbageCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription);
	virtual void internalPreCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription, U_32 gcCode);
	virtual void internalPostCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace);

	void reportGCCycleStart(MM_EnvironmentBase *env);
	void reportGCCycleContinue(MM_EnvironmentBase *env, UDATA oldCycleStateType);
	void reportGCCycleEnd(MM_EnvironmentBase *env);
	void reportGCCycleFinalIncrementEnding(MM_EnvironmentBase *env);

	/**
	 * Report the start of a GMP cycle.
	 * @param env a GC thread.
	 */
	void reportGMPCycleStart(MM_EnvironmentBase *env);
	/**
	 * Report the transition from a GMP cycle to another cycle (type already set in env cycle state).
	 * @param env a GC thread.
	 */
	void reportGMPCycleContinue(MM_EnvironmentBase *env);

	/**
	 * Report the env of a GMP cycle.
	 * @param env a GC thread.
	 */
	void reportGMPCycleEnd(MM_EnvironmentBase *env);
	void reportGCIncrementStart(MM_EnvironmentBase *env, const char *incrementDescription, UDATA incrementCount);
	void reportGCIncrementEnd(MM_EnvironmentBase *env);
	void reportMarkStart(MM_EnvironmentBase *env);
	void reportMarkEnd(MM_EnvironmentBase *env);

	/**
	 * Report the start of a GMP mark increment.
	 */
	void reportGMPMarkStart(MM_EnvironmentBase *env);

	/**
	 * Report the end of a GMP mark increment.
	 */
	void reportGMPMarkEnd(MM_EnvironmentBase *env);

	/**
	 * Report the start of a global gc mark operation.
	 */
	void reportGlobalGCMarkStart(MM_EnvironmentBase *env);

	/**
	 * Report the end of a global gc mark operation.
	 */
	void reportGlobalGCMarkEnd(MM_EnvironmentBase *env);

public:
	static MM_IncrementalGenerationalGC *newInstance(MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager);
	/**
	 * initialize TaxationThreshold during jvm initialize
	 * @param env[in] the current thread
	 */
	void initializeTaxationThreshold(MM_EnvironmentVLHGC *env);

	virtual void kill(MM_EnvironmentBase *env);
	
	virtual UDATA getVMStateID() { return OMRVMSTATE_GC_COLLECTOR_GLOBALGC; };
	
	virtual bool collectorStartup(MM_GCExtensionsBase* extensions);
	virtual void collectorShutdown(MM_GCExtensionsBase *extensions);
	
	virtual bool heapAddRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress);
	virtual bool heapRemoveRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, UDATA size, void *lowAddress, void *highAddress, void *lowValidAddress, void *highValidAddress);
	virtual void heapReconfigured(MM_EnvironmentBase *env, HeapReconfigReason reason, MM_MemorySubSpace *subspace, void *lowAddress, void *highAddress);
	/**
	 * @see MM_Collector::collectorExpanded
	 */
	virtual void collectorExpanded(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, UDATA expandSize);
	/**
	 * Check if heap resizes as needed and perform it accordingly.
	 * Update heap stats.
	 * @param env[in] The main GC thread
	 * @parm allocDescription[in] description of current allocation request
	 * @return true if resize is successful
	 */
	bool attemptHeapResize(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);
	
	virtual	U_32 getGCTimePercentage(MM_EnvironmentBase *env);

	/**
	 * Get the IGGC's markMapManager.
	 * @returns @ref _markMapManager
	 */
	MMINLINE MM_MarkMapManager *getMarkMapManager()
	{
		return _markMapManager;
	}

	/**
	 * Get the last (actual) sden size
	 * @returns eden size
	 */
	
	UDATA getAllocatedSinceLastPGC() {
		return _allocatedSinceLastPGC;
	}

	/**
	 * The _configuredSubspace ivar is mutable via this method only to simplify start-up in the Configuration framework but it should only be set once (implementation asserts this).
	 */
	void setConfiguredSubspace(MM_EnvironmentBase *env, MM_MemorySubSpaceTarok *configuredSubspace);
		
	/**
	 * Increment the nursery age of every region which contains objects, up to the maximum age.
	 * @param env[in] The main GC thread
	 * @param increment number of allocated bytes age should be incremented for
	 * @param isPGC set to true if it is a PCG cycle - for compatibility with old aging system
	 */
	void incrementRegionAges(MM_EnvironmentVLHGC *env, UDATA increment, bool isPGC);

	/**
	 * Initial increment of region ages
	 * As far as allocation is acceptable before Collector is started up
	 * all regions allocated at this time required to have their age corrected
	 * Currently age just set up to time of Collector's start up
	 * @param env[in] The main GC thread
	 * @param increment number of allocated bytes age should be set up
	 */
	void initialRegionAgesSetup(MM_EnvironmentVLHGC *env, UDATA age);

	/**
	 * Increment the nursery age of region.
	 * @param env[in] The main GC thread
	 * @param increment number of allocated bytes age should be incremented for
	 * @param isPGC set to true if it is a PCG cycle - for compatibility with old aging system
	 */
	void incrementRegionAge(MM_EnvironmentVLHGC *env, MM_HeapRegionDescriptorVLHGC *region, UDATA increment, bool isPGC);

	/**
	 * Set the nursery age of every region which contains objects to the maximum age.
	 * @param env[in] The main GC thread
	 */
	void setRegionAgesToMax(MM_EnvironmentVLHGC *env);
	
	/**
	 * flush RS Lists for CollectionSet and dirty card table
	 */
	void flushRememberedSetIntoCardTable(MM_EnvironmentVLHGC *env);

	/**
	 * Setup before GC cycle
	 * @param env[in] The main GC thread
	 * @param gcCode[in] A code describing the current GC cycle
	 */
	void setupBeforePartialGC(MM_EnvironmentVLHGC *env, MM_GCCode gcCode);
	void setupBeforeGlobalGC(MM_EnvironmentVLHGC *env, MM_GCCode gcCode);
	/**
	 * @return True if the global mark phase has active persistent data stored in the collector
	 */
	bool isGlobalMarkPhaseRunning() { return (MM_CycleState::state_mark_idle != _persistentGlobalMarkPhaseState._markDelegateState); }

	/**
	 * @return The number of bytes we have scanned so far in the current Global Mark Phase.  
	 * If we are not currently running in a GMP, returns 0.
	 */
	UDATA getBytesScannedInGlobalMarkPhase();

	/**
	 *	Common (Partial and Global) initializations before GC cycle
	 */
	void setupBeforeGC(MM_EnvironmentBase *env);

	/**
 	* Request to create sweepPoolState class for pool
 	* @param  memoryPool memory pool to attach sweep state to
 	* @return pointer to created class
 	*/
	virtual void *createSweepPoolState(MM_EnvironmentBase *env, MM_MemoryPool *memoryPool);

	/**
 	* Request to destroy sweepPoolState class for pool
 	* @param  sweepPoolState class to destroy
 	*/
	virtual void deleteSweepPoolState(MM_EnvironmentBase *env, void *sweepPoolState);

	/**
 	* Get InterRegionRememberedSet. InterRegionRememberedSet exposed only if tgc. 
 	* TODO: Try fetching it through cycleState (currently not doable, due to relative order of reportGCStart and cycleState initialization)
 	*/
	MM_InterRegionRememberedSet *getInterRegionRememberedSet() { return _interRegionRememberedSet; }

	/**
	* Get persistentGlobalMarkPhaseState. Necessary for SchedulingDelegate to gather stats.
	*/
	MM_CycleStateVLHGC *getPersistentGlobalMarkPhaseState() { return &_persistentGlobalMarkPhaseState; }

	/**
	 * Called after a successful allocation if the given environment has been flagged as responsible for running GC related work.
	 * The implementation can assume that the thread is at a safe point but does not have exclusive access.
	 * @param env[in] The thread which has been tasked with leading this increment (it will act as "main" in the collect).  This thread does not have exclusive but is at a safe point
	 * @param subspace[in] the subspace to be collected
	 * @param allocDescription[in] a description of the allocate which triggered the collection
	 */
	void taxationEntryPoint(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, MM_AllocateDescription *allocDescription);

	virtual bool isMarked(void *objectPtr);

	/**
	 * @return true if the collector has a pending concurrent request
	 */
	virtual bool isConcurrentWorkAvailable(MM_EnvironmentBase *env);

	/**
	 * Called by the MainGCThread while it still owns the GC control monitor in order to allow for the initial population of stats
	 * and reporting of triggers to occur in-order relative to threads outside the GC.
	 * @param env[in] The main GC thread
	 * @param stats[out] The stats data structure meant to be populated with the state of the collector before the concurrent operation starts
	 */
	virtual void preConcurrentInitializeStatsAndReport(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats);

	/**
	 * The entry-point used by the main GC thread to perform concurrent GMP work.  isConcurrentWorkAvailable must be true.
	 * @param env[in] The main GC thread
	 * @return The number of bytes scanned by this invocation of the concurrent task
	 */
	virtual uintptr_t mainThreadConcurrentCollect(MM_EnvironmentBase *env);

	/**
	 * Called by the MainGCThread after it re-acquires the GC control monitor in order to allow for the final population of stats
	 * and reporting of triggers to occur in-order relative to threads outside the GC.
	 * @param env[in] The main GC thread
	 * @param stats[in/out] The stats data structure meant to be populated with the state of the collector after the concurrent operation ends (data populated before the collect is still in the structure)
	 * @param bytesConcurrentlyScanned[in] The number of bytes scanned during the concurrent task
	 */
	virtual void postConcurrentUpdateStatsAndReport(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats, UDATA bytesConcurrentlyScanned);

	/**
	 * Called by the MainGCThread after it re-acquires the GC control monitor in order to calculate statistics 
	 * regarding how long GC threads spent doing concurrent mark work relative to threads outside the GC. 
	 * @param env[in] The main GC thread
	 * @param stats[in/out] The stats data structure meant to be populated with the state of the collector after the concurrent operation ends (data populated before the collect is still in the structure)
	 */ 
	virtual void calculateConcurrentMarkWorkTime(MM_EnvironmentBase *env, MM_ConcurrentPhaseStatsBase *stats);

	/**
	 * Triggers a termination flag used to interrupt the concurrent task so that it will return.  This is called in the
	 * cases where an external thread requires that the main GC thread return to its control mutex to receive a new
	 * GC request.
	 * Note that this method returns immediately.
	 */
	virtual void forceConcurrentFinish();

	/**
	 * perform initializing before Main thread startup or first non gcthread garbage collection
	 * @param env[in] the current thread
	 */
	virtual void preMainGCThreadInitialize(MM_EnvironmentBase *env);
	
	virtual MM_ConcurrentPhaseStatsBase *getConcurrentPhaseStats() { return &_concurrentPhaseStats; }

	MMINLINE UDATA getCurrentEdenSizeInBytes(MM_EnvironmentVLHGC *env)
	{
		return _schedulingDelegate.getCurrentEdenSizeInBytes(env);
	}

	MM_IncrementalGenerationalGC(MM_EnvironmentVLHGC *env, MM_HeapRegionManager *manager);

	friend class MM_MainGCThread;

protected:
	virtual void reportGCStart(MM_EnvironmentBase *env);
	virtual void reportGCEnd(MM_EnvironmentBase *env);
private:

	/**
	 * Perform collection preparation work including cache flushing and stats reporting.
	 */
	void partialGarbageCollectPreWork(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Perform collection end of cycle work
	 */
	void partialGarbageCollectPostWork(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Perform a partial garbage collection (PGC) operation.  The main (internal) entry point to running a PGC operation.
	 * @param env[in] The main GC thread
	 * @param allocDescription[in] The allocation request which triggered the collect.
	 */
	void runPartialGarbageCollect(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Perform an increment of a global mark phase (GMP).  The main (internal) entry point to running GMP increment.
	 */
	void runGlobalMarkPhaseIncrement(MM_EnvironmentVLHGC *env);

	/**
	 * Perform a full heap global garbage collection (GGC).  The main (internal) entry point to running a GGC.
	 * @param env[in] The main GC thread.
	 * @param allocDescription[in] The allocation request which triggered the collect.
	 */
	void runGlobalGarbageCollection(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Runs a GMP (global mark only).  This call only runs the mark, collects mark time statistics, and calls the RSM updated hook.
	 * @param env[in] The main GC thread
	 * @param incrementalMark True if we should perform only one increment of the global mark (resuming where we left off) or false if we want to
	 * complete the global mark in this call
	 */
	void globalMarkPhase(MM_EnvironmentVLHGC *env, bool incrementalMark);

	/**
	 * Pre processing partial garbage collection step using the copy forward mechanism.
	 */
	void preProcessPGCUsingCopyForward(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Post processing partial garbage collection step using the copy forward mechanism.
	 */
	void postProcessPGCUsingCopyForward(MM_EnvironmentVLHGC *env, MM_AllocateDescription *allocDescription);

	/**
	 * Change all unmarked regions to marked regions.
	 * This should be called as soon as the mark map has been brought up to date for these regions.
	 * @param env[in] The main GC thread
	 */
	void declareAllRegionsAsMarked(MM_EnvironmentVLHGC *env);


	/**
	 * Walks all packets in the given work packets collection and asserts that they are all empty.
	 * @param env[in] A GC thread (typically the main but this could be anyone)
	 * @param packets[in] The packets structure to verify is empty
	 */
	void assertWorkPacketsEmpty(MM_EnvironmentVLHGC *env, MM_WorkPacketsVLHGC *packets);
	
	/**
	 * Tests that all marked objects in the given mark map have the following properties:
	 * 1)  refer only to other marked objects
	 * 2)  have valid classes
	 * 3)  are instances of marked classes only
	 * Fails assertion if any of these conditions are not met.
	 * @param env[in] The main GC thread
	 * @param markMap[in] The mark map to be tested for logical closure
	 */
	void verifyMarkMapClosure(MM_EnvironmentVLHGC *env, MM_MarkMap *markMap);

	/**
	 * Gather generic heap/collector stats and export then into an external stats structure.
	 * @param env[in] The main GC thread
	 * @param stats[out] The stat structure, info is exported to
	 * @param classesPotentiallyUnloaded[in], let the method if classes have recently been unloaded, so it does not do any unsafe operation that may be based on class info (like scan type)
	 */
	void exportStats(MM_EnvironmentVLHGC *env, MM_CollectionStatisticsVLHGC *stats, bool classesPotentiallyUnloaded = false);

	/**
	 * Called at the beginning of a PGC to call tracepoints and hooks required before we can begin doing increment-specific work.
	 * @param env[in] The main GC thread
	 */
	void reportPGCStart(MM_EnvironmentVLHGC *env);

	/**
	 * Called at the end of a PGC to call tracepoints and hooks required before we can return from the collector.
	 * @param env[in] The main GC thread
	 */
	void reportPGCEnd(MM_EnvironmentVLHGC *env);


	/**
	 * Called at the beginning of a GMP increment to call tracepoints and hooks required before we can begin doing increment-specific work.
	 * @param env[in] The main GC thread
	 */
	void reportGMPIncrementStart(MM_EnvironmentVLHGC *env);

	/**
	 * Called at the end of a GMP increment to call tracepoints and hooks required before we can return from the collector.
	 * @param env[in] The main GC thread
	 */
	void reportGMPIncrementEnd(MM_EnvironmentVLHGC *env);

	/**
	 * Called at the beginning of a global collection (OOM or SYS) to call tracepoints and hooks required before we can begin executing the collect.
	 * @param env[in] The main GC thread
	 */
	void reportGlobalGCStart(MM_EnvironmentVLHGC *env);

	/**
	 * Called at the end of a global collection (OOM or SYS) to call tracepoints and hooks required before we can return from the collector.
	 * @param env[in] The main GC thread
	 */
	void reportGlobalGCEnd(MM_EnvironmentVLHGC *env);

	/**
	 * Called by the above report*Start methods in order to call the common GC start hook.
	 * @param env[in] The main GC thread
	 */
	void triggerGlobalGCStartHook(MM_EnvironmentVLHGC *env);

	/**
	 * Called by the above report*End methods in order to call the common GC end hook.
	 * @param env[in] The main GC thread
	 */
	void triggerGlobalGCEndHook(MM_EnvironmentVLHGC *env);
	
	/**
	 * Report the start of a copy forward operation.
	 * @param env a GC thread.
	 */
	void reportCopyForwardStart(MM_EnvironmentVLHGC *env);

	/**
	 * Report the end of a copy forward operation.
	 * @param env a GC thread.
	 * @param totalTime time taken for the copy forward operation.
	 */
	void reportCopyForwardEnd(MM_EnvironmentVLHGC *env, U_64 totalTime);

	/**
	 * Walks all cards on the heap in the context of the calling thread and asserts that they are clean.
	 * @param env[in] A GC thread
	 * @param additionalCleanState An additional state which will be interpreted as "clean" from the point of view of this verification
	 */
	void assertTableClean(MM_EnvironmentVLHGC *env, Card additionalCleanState);
};

#endif /* INCREMENTALGENERATIONALGC_HPP_ */

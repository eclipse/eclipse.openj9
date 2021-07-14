
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
 * @ingroup GC_Base
 */

#if !defined(GCEXTENSIONS_HPP_)
#define GCEXTENSIONS_HPP_

#include <string.h>

#include "arrayCopyInterface.h"
#include "j9cfg.h"
#include "j9comp.h"
#include "omrhookable.h"
#include "mmhook_internal.h"
#include "modronbase.h"
#include "omr.h"
#include "omrmodroncore.h"

#include "EnvironmentBase.hpp"
#include "GCExtensionsBase.hpp"
#include "MarkJavaStats.hpp"
#include "OMRVMThreadListIterator.hpp"
#include "VMThreadListIterator.hpp"
#include "VerboseGCInterface.h"

#if defined(J9VM_GC_MODRON_SCAVENGER)
#include "ScavengerJavaStats.hpp"
#endif /* J9VM_GC_MODRON_SCAVENGER */

class MM_ClassLoaderManager;
class MM_EnvironmentBase;
class MM_HeapMap;
class MM_MemorySubSpace;
class MM_ObjectAccessBarrier;
class MM_OwnableSynchronizerObjectList;
class MM_StringTable;
class MM_UnfinalizedObjectList;
class MM_Wildcard;

#if defined(J9VM_GC_FINALIZATION)
class GC_FinalizeListManager;
#endif /* J9VM_GC_FINALIZATION */

#if defined(J9VM_GC_REALTIME)
class MM_ReferenceObjectList;
#endif /* J9VM_GC_REALTIME */

#if defined(OMR_GC_IDLE_HEAP_MANAGER)
class MM_IdleGCManager;
#endif

#define DEFAULT_SURVIVOR_MINIMUM_FREESIZE 	2048
#define DEFAULT_SURVIVOR_THRESHOLD 			512
#define MAXIMUM_SURVIVOR_MINIMUM_FREESIZE 	524288
#define MAXIMUM_SURVIVOR_THRESHOLD 			8192
#define MINIMUM_SURVIVOR_MINIMUM_FREESIZE 	512
#define MINIMUM_SURVIVOR_THRESHOLD 			512

/**
 * @todo Provide class documentation
 * @ingroup GC_Base
 */
class MM_GCExtensions : public MM_GCExtensionsBase {
private:
	MM_OwnableSynchronizerObjectList* ownableSynchronizerObjectLists; /**< The global linked list of ownable synchronizer object lists. */
public:
	MM_StringTable* stringTable; /**< top level String Table structure (internally organized as a set of hash sub-tables */

	void* gcchkExtensions;

	void* tgcExtensions;
	J9MemoryManagerVerboseInterface verboseFunctionTable;

#if defined(J9VM_GC_FINALIZATION)
	IDATA finalizeCycleInterval;
	IDATA finalizeCycleLimit;
#endif /* J9VM_GC_FINALIZATION */

	MM_HookInterface hookInterface;

	bool collectStringConstants;

	MM_MarkJavaStats markJavaStats;
#if defined(J9VM_GC_MODRON_SCAVENGER)
	MM_ScavengerJavaStats scavengerJavaStats;
#endif /* J9VM_GC_MODRON_SCAVENGER */

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	enum DynamicClassUnloading {
		DYNAMIC_CLASS_UNLOADING_NEVER,
		DYNAMIC_CLASS_UNLOADING_ON_CLASS_LOADER_CHANGES,
		DYNAMIC_CLASS_UNLOADING_ALWAYS
	};
	DynamicClassUnloading dynamicClassUnloading;

	bool dynamicClassUnloadingSet; /**< is true if value for dynamicClassUnloading was specified in command line */

	UDATA runtimeCheckDynamicClassUnloading;
	bool dynamicClassUnloadingKickoffThresholdForced; /**< true if classUnloadingKickoffThreshold is specified in java options. */
	bool dynamicClassUnloadingThresholdForced; /**< true if classUnloadingThresholdForced is specified in java options. */
	UDATA dynamicClassUnloadingKickoffThreshold; /**< the threshold to kickoff a concurrent global GC from a scavenge */
	UDATA dynamicClassUnloadingThreshold; /**< the threshold to trigger class unloading during a global GC */
	double classUnloadingAnonymousClassWeight; /**< The weight factor to apply to anonymous classes for threshold comparisons */
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

	U_32 _stringTableListToTreeThreshold; /**< Threshold at which we start using trees instead of lists for collision resolution in the String table */

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	bool fvtest_forceFinalizeClassLoaders;
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

	UDATA maxSoftReferenceAge; /**< The fixed age specified as the soft reference threshold which acts as our baseline for the dynamicMaxSoftReferenceAge */
	UDATA dynamicMaxSoftReferenceAge; /**< The age which represents the clearing age of soft references for a globalGC cycle.  At the end of a GC cycle, it will be updated for the following cycle by taking the percentage of free heap in the oldest generation as a fraction of the maxSoftReferenceAge */
#if defined(J9VM_GC_FINALIZATION)
	GC_FinalizeListManager* finalizeListManager;
#endif /* J9VM_GC_FINALIZATION */

	J9ReferenceArrayCopyTable referenceArrayCopyTable;

#if defined(J9VM_GC_REALTIME)
	MM_ReferenceObjectList* referenceObjectLists; /**< A global array of lists of reference objects (i.e. weak/soft/phantom) */
#endif /* J9VM_GC_REALTIME */
	MM_ObjectAccessBarrier* accessBarrier;

#if defined(J9VM_GC_FINALIZATION)
	UDATA finalizeMainPriority; /**< cmd line option to set finalize main thread priority */
	UDATA finalizeWorkerPriority; /**< cmd line option to set finalize worker thread priority */
#endif /* J9VM_GC_FINALIZATION */

	MM_ClassLoaderManager* classLoaderManager; /**< Pointer to the gc's classloader manager to process classloaders/classes */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	UDATA deadClassLoaderCacheSize;
#endif /*defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

	MM_UnfinalizedObjectList* unfinalizedObjectLists; /**< The global linked list of unfinalized object lists. */
	
	UDATA objectListFragmentCount; /**< the size of Local Object Buffer(per gc thread), used by referenceObjectBuffer, UnfinalizedObjectBuffer and OwnableSynchronizerObjectBuffer */

	MM_Wildcard* numaCommonThreadClassNamePatterns; /**< A linked list of thread class names which should be associated with the common context */

	struct {
		MM_UserSpecifiedParameterUDATA _Xmn; /**< Initial value of -Xmn specified by the user */
		MM_UserSpecifiedParameterUDATA _Xmns; /**< Initial value of -Xmns specified by the user */
		MM_UserSpecifiedParameterUDATA _Xmnx; /**< Initial value of -Xmnx specified by the user */
	} userSpecifiedParameters;

	/**
	 * Values for com.ibm.oti.vm.VM.J9_JIT_STRING_DEDUP_POLICY
	 * must hava the same values as J9_JIT_STRING_DEDUP_POLICY_DISABLED, J9_JIT_STRING_DEDUP_POLICY_FAVOUR_LOWER and J9_JIT_STRING_DEDUP_POLICY_FAVOUR_HIGHER.
	 */
	enum JitStringDeDupPolicy {
		J9_JIT_STRING_DEDUP_POLICY_DISABLED = 0,
		J9_JIT_STRING_DEDUP_POLICY_FAVOUR_LOWER = 1,
		J9_JIT_STRING_DEDUP_POLICY_FAVOUR_HIGHER = 2,
		J9_JIT_STRING_DEDUP_POLICY_UNDEFINED = 3,
	};
	JitStringDeDupPolicy stringDedupPolicy;

	IDATA _asyncCallbackKey;
	IDATA _TLHAsyncCallbackKey;

	bool _HeapManagementMXBeanBackCompatibilityEnabled;

#if defined(OMR_GC_IDLE_HEAP_MANAGER)
	MM_IdleGCManager* idleGCManager; /**< Manager which registers for VM Runtime State notification & manages free heap on notification */
#endif

	double maxRAMPercent; /**< Value of -XX:MaxRAMPercentage specified by the user */
	double initialRAMPercent; /**< Value of -XX:InitialRAMPercentage specified by the user */
	UDATA minimumFreeSizeForSurvivor; /**< minimum free size can be reused by collector as survivor, for balanced GC only */
	UDATA freeSizeThresholdForSurvivor; /**< if average freeSize(freeSize/freeCount) of the region is smaller than the Threshold, the region would not be reused by collector as survivor, for balanced GC only */
protected:
private:
protected:
	virtual bool initialize(MM_EnvironmentBase* env);
	virtual void tearDown(MM_EnvironmentBase* env);

public:
	static MM_GCExtensions* newInstance(MM_EnvironmentBase* env);
	virtual void kill(MM_EnvironmentBase* env);

	void computeDefaultMaxHeapForJava(bool enableOriginalJDK8HeapSizeCompatibilityOption);

	MMINLINE J9HookInterface** getHookInterface() { return J9_HOOK_INTERFACE(hookInterface); };

	/**
	 * Fetch the global string table instance.
	 * @return the string table
	 */
	MMINLINE MM_StringTable* getStringTable() { return stringTable; }

	MMINLINE UDATA getDynamicMaxSoftReferenceAge()
	{
		return dynamicMaxSoftReferenceAge;
	}

	MMINLINE UDATA getMaxSoftReferenceAge()
	{
		return maxSoftReferenceAge;
	}

	virtual void identityHashDataAddRange(MM_EnvironmentBase* env, MM_MemorySubSpace* subspace, UDATA size, void* lowAddress, void* highAddress);
	virtual void identityHashDataRemoveRange(MM_EnvironmentBase* env, MM_MemorySubSpace* subspace, UDATA size, void* lowAddress, void* highAddress);

	void updateIdentityHashDataForSaltIndex(UDATA index);

	/**
	 * Set Tenure address range
	 * @param base low address of Old subspace range
	 * @param size size of Old subspace in bytes
	 */
	virtual void setTenureAddressRange(void* base, UDATA size)
	{
		_tenureBase = base;
		_tenureSize = size;

		/* todo: dagar move back to MemorySubSpaceGeneric addTenureRange() and removeTenureRange() once
		 * heapBaseForBarrierRange0 heapSizeForBarrierRange0 can be removed from J9VMThread
		 *
		 * setTenureAddressRange() can be removed fromo GCExtensions.hpp and made inline again
		 */
		GC_OMRVMThreadListIterator omrVMThreadListIterator(_omrVM);
		while (OMR_VMThread* walkThread = omrVMThreadListIterator.nextOMRVMThread()) {
			walkThread->lowTenureAddress = heapBaseForBarrierRange0;
			walkThread->highTenureAddress = (void*)((UDATA)heapBaseForBarrierRange0 + heapSizeForBarrierRange0);
			walkThread->heapBaseForBarrierRange0 = heapBaseForBarrierRange0;
			walkThread->heapSizeForBarrierRange0 = heapSizeForBarrierRange0;
		}

		GC_VMThreadListIterator vmThreadListIterator((J9JavaVM*)_omrVM->_language_vm);
		while (J9VMThread* walkThread = vmThreadListIterator.nextVMThread()) {
			walkThread->lowTenureAddress = heapBaseForBarrierRange0;
			walkThread->highTenureAddress = (void*)((UDATA)heapBaseForBarrierRange0 + heapSizeForBarrierRange0);
			walkThread->heapBaseForBarrierRange0 = heapBaseForBarrierRange0;
			walkThread->heapSizeForBarrierRange0 = heapSizeForBarrierRange0;
		}
	}


	static MM_GCExtensions* getExtensions(OMR_VM* omrVM) { return static_cast<MM_GCExtensions*>(MM_GCExtensionsBase::getExtensions(omrVM)); }
	static MM_GCExtensions* getExtensions(OMR_VMThread* omrVMThread) { return static_cast<MM_GCExtensions*>(MM_GCExtensionsBase::getExtensions(omrVMThread->_vm)); }
	static MM_GCExtensions* getExtensions(J9JavaVM* javaVM) { return static_cast<MM_GCExtensions*>(MM_GCExtensionsBase::getExtensions(javaVM->omrVM)); }
	static MM_GCExtensions* getExtensions(J9VMThread* vmThread) { return getExtensions(vmThread->javaVM); }
	static MM_GCExtensions* getExtensions(MM_GCExtensionsBase* ext) { return static_cast<MM_GCExtensions*>(ext); }
	static MM_GCExtensions* getExtensions(MM_EnvironmentBase* env) { return getExtensions(env->getExtensions()); }

	MMINLINE J9JavaVM* getJavaVM() {return static_cast<J9JavaVM*>(_omrVM->_language_vm);}
	
	/**
	 * Return ownable synchronizer object lists by first ensuring that the lists are in a consistent state (e.g., during concurrent gc).
	 * This should be used by any external consumer (non-GC consuming the list)     
	 * @param vmThread The current J9VMThread thread (used to invoke j9gc apis if required)
	 * @return Linked list of ownable synchronizer objects
	 */
	MM_OwnableSynchronizerObjectList* getOwnableSynchronizerObjectListsExternal(J9VMThread *vmThread);
	MMINLINE MM_OwnableSynchronizerObjectList* getOwnableSynchronizerObjectLists() { return ownableSynchronizerObjectLists; }
	MMINLINE void setOwnableSynchronizerObjectLists(MM_OwnableSynchronizerObjectList* newOwnableSynchronizerObjectLists) { ownableSynchronizerObjectLists = newOwnableSynchronizerObjectLists; }

	/**
	 * Create a GCExtensions object
	 */
	MM_GCExtensions()
		: MM_GCExtensionsBase()
		, ownableSynchronizerObjectLists(NULL)
		, stringTable(NULL)
		, gcchkExtensions(NULL)
		, tgcExtensions(NULL)
#if defined(J9VM_GC_FINALIZATION)
		, finalizeCycleInterval(J9_FINALIZABLE_INTERVAL)  /* 1/2 second */
		, finalizeCycleLimit(0)  /* 0 seconds (i.e. no time limit) */
#endif /* J9VM_GC_FINALIZATION */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		, dynamicClassUnloadingSet(false)
		, dynamicClassUnloadingKickoffThresholdForced(false)
		, dynamicClassUnloadingThresholdForced(false)
		, dynamicClassUnloadingKickoffThreshold(0)
		, dynamicClassUnloadingThreshold(0)
		, classUnloadingAnonymousClassWeight(1.0)
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
		, _stringTableListToTreeThreshold(1024)
		, maxSoftReferenceAge(32)
#if defined(J9VM_GC_FINALIZATION)
		, finalizeMainPriority(J9THREAD_PRIORITY_NORMAL)
		, finalizeWorkerPriority(J9THREAD_PRIORITY_NORMAL)
#endif /* J9VM_GC_FINALIZATION */
		, classLoaderManager(NULL)
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
		, deadClassLoaderCacheSize(1024 * 1024) /* default is one MiB */
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
		, unfinalizedObjectLists(NULL)
		, objectListFragmentCount(0)
		, numaCommonThreadClassNamePatterns(NULL)
		, stringDedupPolicy(J9_JIT_STRING_DEDUP_POLICY_UNDEFINED)
		, _asyncCallbackKey(-1)
		, _TLHAsyncCallbackKey(-1)
		, _HeapManagementMXBeanBackCompatibilityEnabled(false)
#if defined(OMR_GC_IDLE_HEAP_MANAGER)
		, idleGCManager(NULL)
#endif
		, maxRAMPercent(0.0) /* this would get overwritten by user specified value */
		, initialRAMPercent(0.0) /* this would get overwritten by user specified value */
		, minimumFreeSizeForSurvivor(DEFAULT_SURVIVOR_MINIMUM_FREESIZE)
		, freeSizeThresholdForSurvivor(DEFAULT_SURVIVOR_THRESHOLD)
	{
		_typeId = __FUNCTION__;
	}
};

#endif /* GCEXTENSIONS_HPP_ */

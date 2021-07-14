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
 * @ingroup GC_Modron_Startup
 */

#if defined (J9VM_GC_VLHGC)
#include <math.h>
#endif /* J9VM_GC_VLHGC */
#include <string.h>

#include "gcmspace.h"
#include "gcutils.h"
#include "j2sever.h"
#include "j9.h"
#include "j9cfg.h"
#include "j9comp.h"
#include "j9consts.h"
#include "j9modron.h"
#include "j9port.h"
#include "j9protos.h"
#include "jni.h"
#include "jvminit.h"
#include "mminit.h"
#include "mminitcore.h"
#include "mmparse.h"
#include "modronnls.h"
#include "omr.h"
#if defined(J9VM_GC_MODRON_TRACE) && !defined(J9VM_GC_REALTIME)
#include "Tgc.hpp"
#endif /* J9VM_GC_MODRON_TRACE && !defined(J9VM_GC_REALTIME) */

#if defined (J9VM_GC_HEAP_CARD_TABLE)
#include "CardTable.hpp"
#endif /* defined (J9VM_GC_HEAP_CARD_TABLE) */
#include "CollectorLanguageInterfaceImpl.hpp"
#if defined(OMR_GC_MODRON_CONCURRENT_MARK)
#include "ConcurrentCardTable.hpp"
#include "ConcurrentGC.hpp"
#endif /* OMR_GC_MODRON_CONCURRENT_MARK */
#include "Configuration.hpp"
#if defined(J9VM_GC_MODRON_STANDARD)
#include "ConfigurationFlat.hpp"
#include "ConfigurationGenerational.hpp"
#endif /* J9VM_GC_MODRON_STANDARD */
#if defined(J9VM_GC_VLHGC)
#include "ConfigurationIncrementalGenerational.hpp"
#endif /* J9VM_GC_VLHGC */
#if defined(J9VM_GC_REALTIME)
#include "ConfigurationRealtime.hpp"
#endif /* J9VM_GC_REALTIME */
#include "ClassLoaderManager.hpp"
#include "Debug.hpp"
#include "EnvironmentBase.hpp"
#if defined(J9VM_GC_FINALIZATION)
#include "FinalizeListManager.hpp"
#endif /* J9VM_GC_FINALIZATION */
#include "GCExtensions.hpp"
#include "GlobalAllocationManager.hpp"
#include "GlobalCollector.hpp"
#include "HeapRegionDescriptor.hpp"
#include "HeapRegionManager.hpp"
#include "LargeObjectAllocateStats.hpp"
#include "Math.hpp"
#include "MemorySpace.hpp"
#include "MemorySubSpace.hpp"
#include "ModronAssertions.h"
#include "ObjectAccessBarrier.hpp"
#include "ObjectAllocationInterface.hpp"
#include "OMRVMInterface.hpp"
#include "OMRVMThreadInterface.hpp"
#include "ParallelDispatcher.hpp"
#if defined(J9VM_GC_VLHGC)
#include "RememberedSetCardList.hpp"
#endif /* J9VM_GC_VLHGC */
#if defined(J9VM_GC_SEGRGATED_HEAP)
#include "ObjectHeapIteratorSegregated.hpp"
#include "SizeClasses.hpp"
#endif /* J9VM_GC_SEGRGATED_HEAP */
#if defined(J9VM_GC_REALTIME)
#include "RememberedSetSATB.hpp"
#endif /* J9VM_GC_REALTIME */
#include "Scavenger.hpp"
#include "StringTable.hpp"
#include "Validator.hpp"
#if defined(OMR_GC_IDLE_HEAP_MANAGER)
#include "IdleGCManager.hpp"
#endif

/**
 * If we fail to allocate heap structures with the default Xmx value,
 * we will try again with a smaller value. These parameters define
 * the percentage by which to reduce the Xmx value.
 */
#define DEFAULT_XMX_REDUCTION_NUMERATOR 4
#define DEFAULT_XMX_REDUCTION_DENOMINATOR 5

#define NONE ((UDATA) 0x0)
#define XMS ((UDATA) 0x1)
#define XMOS ((UDATA) 0x2)
#define XMNS ((UDATA) 0x4)
#define XMDX ((UDATA) 0x8)

#define XMS_XMOS ((UDATA) XMS | XMOS)
#define XMOS_XMNS ((UDATA) XMOS | XMNS)
#define XMDX_XMS ((UDATA) XMDX | XMS)

#define ROUND_TO(granularity, number) (((UDATA)(number) + (granularity) - 1) & ~((UDATA)(granularity) - 1))

extern "C" {
extern J9MemoryManagerFunctions MemoryManagerFunctions;
extern void initializeVerboseFunctionTableWithDummies(J9MemoryManagerVerboseInterface *table);

static void hookValidatorVMThreadCrash(J9HookInterface * * hookInterface, UDATA eventNum, void * eventData, void * userData);
static bool gcInitializeVMHooks(MM_GCExtensionsBase *extensions);
static void gcCleanupVMHooks(MM_GCExtensionsBase *extensions);

static const char * displayXmxOrMaxRAMPercentage(IDATA* memoryParameters);
static const char * displayXmsOrInitialRAMPercentage(IDATA* memoryParameters);

/**
 * Initialize the threads mutator information (RS pointers, reference list pointers etc) for GC/MM purposes.
 *
 * @note vmThread MAY NOT be initialized completely from an execution model perspective.
 * @return 0 if OK, or non 0 if error
 */
IDATA
initializeMutatorModelJava(J9VMThread* vmThread)
{
	if (0 != initializeMutatorModel(vmThread->omrVMThread)) {
		return -1;
	}

	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(vmThread);
	vmThread->gcExtensions = vmThread->omrVMThread->_gcOmrVMThreadExtensions;

	if (extensions->isStandardGC()) {
		if (extensions->isConcurrentScavengerEnabled()) {
			/* Ensure that newly created threads invoke VM access using slow path, so that the associated hook is invoked.
			 * GC will register to the hook to enable local thread resources if a thread happens to be created in a middle of Concurrent Scavenge */
			setEventFlag(vmThread, J9_PUBLIC_FLAGS_DISABLE_INLINE_VM_ACCESS);
		}

#if defined(J9VM_GC_GENERATIONAL)
		vmThread->gcRememberedSet.fragmentCurrent = NULL;
		vmThread->gcRememberedSet.fragmentTop = NULL;
		vmThread->gcRememberedSet.fragmentSize = OMR_SCV_REMSET_FRAGMENT_SIZE;
#endif /* J9VM_GC_GENERATIONAL */

		void *lowAddress = extensions->heapBaseForBarrierRange0;
		void *highAddress = (void *)((UDATA)extensions->heapBaseForBarrierRange0 + extensions->heapSizeForBarrierRange0);

		// todo: dagar lowTenureAddress, highTenureAddress, heapBaseForBarrierRange0, heapSizeForBarrierRange0 are duplicated
		vmThread->lowTenureAddress = lowAddress;
		vmThread->highTenureAddress = highAddress;

		/* replacement values for lowTenureAddress and highTenureAddress */
		// todo: dagar remove duplicate fields
		vmThread->heapBaseForBarrierRange0 = extensions->heapBaseForBarrierRange0;
		vmThread->heapSizeForBarrierRange0 = extensions->heapSizeForBarrierRange0;
#if defined (J9VM_GC_HEAP_CARD_TABLE)
		if (NULL != extensions->cardTable) {
			vmThread->activeCardTableBase = extensions->cardTable->getCardTableStart();
		}
#endif /* J9VM_GC_HEAP_CARD_TABLE */
	} else if(extensions->isVLHGC()) {
		MM_Heap *heap = extensions->getHeap();
		void *heapBase = heap->getHeapBase();
		void *heapTop = heap->getHeapTop();

		/* replacement values for lowTenureAddress and highTenureAddress */
		vmThread->heapBaseForBarrierRange0 = heapBase;
		vmThread->heapSizeForBarrierRange0 = (UDATA)heapTop - (UDATA)heapBase;

		/* lowTenureAddress and highTenureAddress are actually supposed to be the low and high addresses of the heap for which card
		 * dirtying is required (the JIT uses this as a range check to determine if it needs to dirty a card when writing into an
		 * object).  Setting these for Tarok is just a work-around until a more generic solution is implemented
		 */
		vmThread->lowTenureAddress = heapBase;
		vmThread->highTenureAddress = heapTop;
#if defined (J9VM_GC_HEAP_CARD_TABLE)
		vmThread->activeCardTableBase = extensions->cardTable->getCardTableStart();
#endif /* J9VM_GC_HEAP_CARD_TABLE */
	}
	return 0;
}

/**
 * Cleanup Mutator specific resources (TLH, thread extension, etc) on shutdown.
 */
void
cleanupMutatorModelJava(J9VMThread* vmThread)
{
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);

	if (NULL != env) {
		J9JavaVM *vm = vmThread->javaVM;
		J9VMDllLoadInfo *loadInfo = getGCDllLoadInfo(vm);

		/* cleanupMutatorModelJava is called as part of the main vmThread shutdown, which happens after
		 * gcCleanupHeapStructures has been called. We should therefore only flush allocation caches
		 * if there is still a heap.
		 */
		if (!IS_STAGE_COMPLETED(loadInfo->completedBits, HEAP_STRUCTURES_FREED)) {
			/* this can only be called if the heap still exists since it will ask the TLH chunk to be abandoned with crashes if the heap is deallocated */
			GC_OMRVMThreadInterface::flushCachesForGC(env);
		}
	}

	cleanupMutatorModel(vmThread->omrVMThread, 0);

	vmThread->gcExtensions = NULL;
}

/**
 * Triggers hook for deleting private heap.
 * @param memorySpace pointer to the list (pool) of memory spaces
 */
static void
reportPrivateHeapDelete(J9JavaVM * javaVM, void * memorySpace)
{
	MM_EnvironmentBase env(javaVM->omrVM);

	MM_MemorySpace *modronMemorySpace = (MM_MemorySpace *)memorySpace;
	if  (modronMemorySpace) {
		if (!(javaVM->runtimeFlags & J9_RUNTIME_SHUTDOWN)) {
			TRIGGER_J9HOOK_MM_PRIVATE_HEAP_DELETE(
				MM_GCExtensions::getExtensions(javaVM)->privateHookInterface,
				env.getOmrVMThread(),
				modronMemorySpace);
		}
	}
}

/**
 * Cleanup passive heap structures
 */
void
gcCleanupHeapStructures(J9JavaVM * vm)
{
	/* If shutdown occurs early (due to command line parsing errors, for example) there may not be
	 * a J9VMThread, so allocate a fake environment.
	 */
	MM_EnvironmentBase env(vm->omrVM);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);

	/* remove hooks installed by Validator */
	gcCleanupVMHooks(extensions);

	/* Flush any allocation contexts so that their memory is returned to the memory spaces before we tear down the memory spaces */
	MM_GlobalAllocationManager *gam = extensions->globalAllocationManager;
	if (NULL != gam) {
		gam->flushAllocationContextsForShutdown(&env);
	}

	if (vm->memorySegments) {
		vm->internalVMFunctions->freeMemorySegmentList(vm, vm->memorySegments);
	}
	if (vm->classMemorySegments) {
		vm->internalVMFunctions->freeMemorySegmentList(vm, vm->classMemorySegments);
	}

#if defined(J9VM_GC_FINALIZATION)
	if (extensions->finalizeListManager) {
		extensions->finalizeListManager->kill(&env);
		extensions->finalizeListManager = NULL;
	}
#endif /* J9VM_GC_FINALIZATION */

	if (vm->mainThread && vm->mainThread->threadObject) {
		/* main thread has not been deallocated yet, but heap has gone */
		vm->mainThread->threadObject = NULL;
	}
	return;
}

/**
 * Initialized passive and active heap components
 */
IDATA
j9gc_initialize_heap(J9JavaVM *vm, IDATA *memoryParameterTable, UDATA heapBytesRequested)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	MM_EnvironmentBase env(vm->omrVM);
	MM_GlobalCollector *globalCollector;
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9VMDllLoadInfo *loadInfo = getGCDllLoadInfo(vm);

	if (J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_PORTABLE_SHARED_CACHE)
		|| J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_CRIU_SUPPORT)) {
		extensions->shouldForceLowMemoryHeapCeilingShiftIfPossible = true;
	}

#if defined(J9VM_GC_BATCH_CLEAR_TLH)
	/* Record batch clear state in VM so inline allocates can decide correct initialization procedure */
	vm->initializeSlotsOnTLHAllocate = (extensions->batchClearTLH == 0) ? 1 : 0;
#endif /* J9VM_GC_BATCH_CLEAR_TLH */

	extensions->heap = extensions->configuration->createHeap(&env, heapBytesRequested);

	if (NULL == extensions->heap) {
		const char *splitFailure = NULL;

		/* If error reason was not explicitly set use general error message */
		if(MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_NO_ERROR == extensions->heapInitializationFailureReason) {
			extensions->heapInitializationFailureReason = MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_INSTANTIATE_HEAP;
		}

		switch(extensions->heapInitializationFailureReason) {

		/* see if we set the split-heap specific error since we want to be more verbose in that case */
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_INSTANTIATE_SPLIT_HEAP_OLD_SPACE:
			splitFailure = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_ALLOCATE_OLD_SPACE, "Failed to allocate old space");
			break;
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_INSTANTIATE_SPLIT_HEAP_NEW_SPACE:
			splitFailure = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_ALLOCATE_NEW_SPACE, "Failed to allocate new space");
			break;
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_INSTANTIATE_SPLIT_HEAP_GEOMETRY:
			splitFailure = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_SPLIT_HEAP_ENTEXTS_WRONG_ORDER, "Required split heap memory geometry could not be allocated");
			break;

		/* failed an attempt to allocate low memory reserve */
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_ALLOCATE_LOW_MEMORY_RESERVE:
		{
			/* Obtain the qualified size (e.g. 2k) */
			UDATA size = extensions->suballocatorInitialSize;
			const char* qualifier = NULL;
			qualifiedSize(&size, &qualifier);

			char *format = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_LOW_MEMORY_RESERVE_SIZE_REQUESTED, "Failed to instantiate compressed references metadata.  %zu%s requested");
			UDATA formatLength = strlen(format) + 32; /* 2^64 is 20 digits, so have a few extra */

			loadInfo->fatalErrorStr = (char *)j9mem_allocate_memory(formatLength, OMRMEM_CATEGORY_MM);
			if (loadInfo->fatalErrorStr) {
				j9str_printf(PORTLIB, loadInfo->fatalErrorStr, formatLength, format, size, qualifier);
				loadInfo->loadFlags |= FREE_ERROR_STRING; /* indicates that buffer should be freed later */
			}
			break;
		}

		/* general error message - can not instantiate heap */
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_INSTANTIATE_HEAP:
		{
			/* Obtain the qualified size (e.g. 2k) */
			UDATA size = heapBytesRequested;
			const char* qualifier = NULL;
			qualifiedSize(&size, &qualifier);

			char *format = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_HEAP_SIZE_REQUESTED, "Failed to instantiate heap.  %zu%s requested");
			UDATA formatLength = strlen(format) + 32; /* 2^64 is 20 digits, so have a few extra */

			loadInfo->fatalErrorStr = (char *)j9mem_allocate_memory(formatLength, OMRMEM_CATEGORY_MM);
			if (loadInfo->fatalErrorStr) {
				j9str_printf(PORTLIB, loadInfo->fatalErrorStr, formatLength, format, size, qualifier);
				loadInfo->loadFlags |= FREE_ERROR_STRING; /* indicates that buffer should be freed later */
			}
			break;
		}

		/* general error message - can not instantiate heap */
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_CAN_NOT_SATISFY_REQUESTED_PAGE_SIZE:
		{
			/* Obtain the qualified size (e.g. 2k) */
			UDATA heapSize = extensions->memoryMax;
			const char* heapSizeQualifier = NULL;
			qualifiedSize(&heapSize, &heapSizeQualifier);

			UDATA pageSize = extensions->requestedPageSize;
			const char* pageSizeQualifier = NULL;
			qualifiedSize(&pageSize, &pageSizeQualifier);

			char *format = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_OPTIONS_XLP_PAGE_NOT_AVAILABLE_STRICT, "Unable to satisfy heap size %zu%s with page size %zu%s. Heap size can be specified with -Xmx");
			UDATA formatLength = strlen(format) + 32; /* 2^64 is 20 digits, so have a few extra */

			loadInfo->fatalErrorStr = (char *)j9mem_allocate_memory(formatLength, OMRMEM_CATEGORY_MM);
			if (loadInfo->fatalErrorStr) {
				j9str_printf(PORTLIB, loadInfo->fatalErrorStr, formatLength, format, heapSize, heapSizeQualifier, pageSize, pageSizeQualifier);
				loadInfo->loadFlags |= FREE_ERROR_STRING; /* indicates that buffer should be freed later */
			}
			extensions->largePageFailedToSatisfy = true;
			break;
		}

		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_NO_ERROR:
		case MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_METRONOME_DOES_NOT_SUPPORT_4BIT_SHIFT:
		default:
			Assert_MM_unreachable();
			break;
		}

		/* Handle split heap failures cases */
		if (NULL != splitFailure) {
			const char *format = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_SPLIT_HEAP, "Failed to instantiate split heap: %s (new size %zu%s, old size %zu%s)");
			UDATA oldSpaceSize = extensions->oldSpaceSize;
			const char* oldQualifier = NULL;
			qualifiedSize(&oldSpaceSize, &oldQualifier);
			UDATA newSpaceSize = extensions->newSpaceSize;
			const char* newQualifier = NULL;
			qualifiedSize(&newSpaceSize, &newQualifier);
			UDATA formatLength = j9str_printf(PORTLIB, NULL, 0, format, splitFailure, newSpaceSize, newQualifier, oldSpaceSize, oldQualifier);

			loadInfo->fatalErrorStr = (char *)j9mem_allocate_memory(formatLength, OMRMEM_CATEGORY_MM);
			if (loadInfo->fatalErrorStr) {
				j9str_printf(PORTLIB, loadInfo->fatalErrorStr, formatLength, format, splitFailure, newSpaceSize, newQualifier, oldSpaceSize, oldQualifier);
				loadInfo->loadFlags |= FREE_ERROR_STRING; /* indicates that buffer should be freed later */
			}
		}

		/* failed to generate error string - use default */
		if(NULL == loadInfo->fatalErrorStr) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_HEAP, "Failed to instantiate heap.");
		}

		goto error_no_memory;
	}

	extensions->dispatcher = extensions->configuration->createParallelDispatcher(&env, (omrsig_handler_fn)vm->internalVMFunctions->structuredSignalHandlerVM, vm, vm->defaultOSStackSize);
	if (NULL == extensions->dispatcher) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_TASK_DISPATCHER, "Failed to instantiate task dispatcher.");
		goto error_no_memory;
	}

	/* Initialize VM interface extensions */
	GC_OMRVMInterface::initializeExtensions(extensions);

	/* Initialize the global collector */
	globalCollector = extensions->configuration->createGlobalCollector(&env);
	if (NULL == globalCollector) {
		if(MM_GCExtensionsBase::HEAP_INITIALIZATION_FAILURE_REASON_METRONOME_DOES_NOT_SUPPORT_4BIT_SHIFT == extensions->heapInitializationFailureReason) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_OVERFLOW, displayXmxOrMaxRAMPercentage(memoryParameterTable));
		}
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INSTANTIATE_GLOBAL_GARBAGE_COLLECTOR, "Failed to instantiate global garbage collector.");
		goto error_no_memory;
	}
	/* Mark this collector as a global collector so that we will check for excessive gc after it collects */
	globalCollector->setGlobalCollector(true);
	extensions->setGlobalCollector(globalCollector);

	/* Create the environments pool */
	extensions->environments = extensions->configuration->createEnvironmentPool(&env);
	if (NULL == extensions->environments) {
		goto error_no_memory;
	}

	extensions->classLoaderManager = MM_ClassLoaderManager::newInstance(&env, globalCollector);
	if (NULL == extensions->classLoaderManager) {
		goto error_no_memory;
	}

	extensions->stringTable = MM_StringTable::newInstance(&env, extensions->dispatcher->threadCountMaximum());
	if (NULL == extensions->stringTable) {
		goto error_no_memory;
	}

	/* Initialize statistic locks */
	if (omrthread_monitor_init_with_name(&extensions->gcStatsMutex, 0, "MM_GCExtensions::gcStats")) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_MUTEX, "Failed to initialize mutex for GC statistics.");
		goto error_no_memory;
	}

#if defined(OMR_GC_IDLE_HEAP_MANAGER)
	if (extensions->gcOnIdle) {
		/* Enable idle tuning only for gencon policy */
		if (gc_policy_gencon == extensions->configurationOptions._gcPolicy) {
			extensions->idleGCManager = MM_IdleGCManager::newInstance(&env);
			if (NULL == extensions->idleGCManager) {
				goto error_no_memory;
			}
		}
	}
#endif

	return JNI_OK;

error_no_memory:
	return JNI_ENOMEM;
}

/**
 * Creates and initialized VM owned structures related to the heap
 * Calls low level heap initialization function
 * @return J9VMDLLMAIN_OK or J9VMDLLMAIN_FAILED
 */
jint
gcInitializeHeapStructures(J9JavaVM *vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	MM_EnvironmentBase env(vm->omrVM);

	MM_MemorySpace *defaultMemorySpace;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	J9VMDllLoadInfo *loadInfo = getGCDllLoadInfo(vm);

	/* For now, number of segments to default in pool */
	if ((vm->memorySegments = vm->internalVMFunctions->allocateMemorySegmentList(vm, 10, OMRMEM_CATEGORY_VM)) == NULL) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_ALLOCATE_VM_MEMORY_SEGMENTS, "Failed to allocate VM memory segments.");
		goto error;
	}

	/* For now, number of segments to default in pool */
	if ((vm->classMemorySegments = vm->internalVMFunctions->allocateMemorySegmentListWithFlags(vm, 10, MEMORY_SEGMENT_LIST_FLAG_SORT, J9MEM_CATEGORY_CLASSES)) == NULL) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_ALLOCATE_VM_CLASS_MEMORY_SEGMENTS, "Failed to allocate VM class memory segments.");
		goto error;
	}

	/* j9gc_initialize_heap is now called from gcInitializeDefaults */

	/* Create and initialize the default memory space */
	defaultMemorySpace = internalAllocateMemorySpaceWithMaximum(vm, extensions->initialMemorySize, extensions->minNewSpaceSize, extensions->newSpaceSize, extensions->maxNewSpaceSize, extensions->minOldSpaceSize, extensions->oldSpaceSize, extensions->maxOldSpaceSize, extensions->maxSizeDefaultMemorySpace, 0, MEMORY_TYPE_DISCARDABLE);
	if (defaultMemorySpace == NULL) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_ALLOCATE_DEFAULT_MEMORY_SPACE, "Failed to allocate default memory space.");
		goto error;
	}

	extensions->configuration->defaultMemorySpaceAllocated(extensions, defaultMemorySpace);

#if defined(J9VM_GC_FINALIZATION)
		if(!(extensions->finalizeListManager = GC_FinalizeListManager::newInstance(&env))) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_FINALIZER_MANAGEMENT, "Failed to initialize finalizer management.");
			goto error;
		}
#endif /* J9VM_GC_FINALIZATION */

	/* install hooks for the Validator */
	if (!gcInitializeVMHooks(extensions)) {
		goto error;
	}

	vm->defaultMemorySpace = defaultMemorySpace;

	return J9VMDLLMAIN_OK;

error:
	return J9VMDLLMAIN_FAILED;
}

/**
 * Starts the Finalizer and the Heap management components
 * @return 0 if OK, non zero if error
 */
int
gcStartupHeapManagement(J9JavaVM *javaVM)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	int result = 0;

#if defined(J9VM_GC_FINALIZATION)
	result = j9gc_finalizer_startup(javaVM);
	if (JNI_OK != result) {
		PORT_ACCESS_FROM_JAVAVM(javaVM);
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_FAILED_TO_INITIALIZE_FINALIZE_SUPPORT);
		return result;
	}
#endif /* J9VM_GC_FINALIZATION */

	/* Kickoff secondary initialization for the global collector */
	if (!extensions->getGlobalCollector()->collectorStartup(extensions)) {
		result = JNI_ENOMEM;
	}

	if (!extensions->dispatcher->startUpThreads()) {
		extensions->dispatcher->shutDownThreads();
		result = JNI_ENOMEM;
	}

	if (JNI_OK != result) {
		PORT_ACCESS_FROM_JAVAVM(javaVM);
		extensions->getGlobalCollector()->collectorShutdown(extensions);
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_FAILED_TO_STARTUP_GARBAGE_COLLECTOR);
		return result;
	}

	return result;
}

void j9gc_jvmPhaseChange(J9VMThread *currentThread, UDATA phase)
{
	J9JavaVM *vm = currentThread->javaVM;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	MM_EnvironmentBase env(currentThread->omrVMThread);
	if (J9VM_PHASE_NOT_STARTUP == phase) {

		if ((NULL != vm->sharedClassConfig) && extensions->useGCStartupHints) {
			if (extensions->isStandardGC()) {
				/* read old values from SC */
				uintptr_t hintDefaultOld = 0;
				uintptr_t hintTenureOld = 0;
				vm->sharedClassConfig->findGCHints(currentThread, &hintDefaultOld, &hintTenureOld);
				/* Nothing to do if read fails, we'll just assume the old values are 0 */

				/* Get the current heap size values.
				 * Default/Tenure MemorySubSpace is of type Generic (which is MemoryPool owner, while the parents are of type Flat/SemiSpace).
				 * For SemiSpace the latter (parent) ones are what we want to deal with (expand), since it's what includes both Allocate And Survivor children.
				 * For Flat it would probably make no difference if we used parent or child, but let's be consistent and use parent, too.
				 */
				MM_MemorySubSpace *defaultMemorySubSpace = extensions->heap->getDefaultMemorySpace()->getDefaultMemorySubSpace()->getParent();
				MM_MemorySubSpace *tenureMemorySubspace = extensions->heap->getDefaultMemorySpace()->getTenureMemorySubSpace()->getParent();

				uintptr_t hintDefault = defaultMemorySubSpace->getActiveMemorySize();
				uintptr_t hintTenure = 0;

				/* Standard GCs always have Default MSS (which is equal to Tenure for flat heap configuration).
				 * So the simplest is always fetch Default, regardless if's generational haep configuration or not.
				 * We fetch Tenure only if only not equal to Default (which implies it's generational) */
				if (defaultMemorySubSpace != tenureMemorySubspace) {
					hintTenure = tenureMemorySubspace->getActiveMemorySize();
				}

				/* Gradually learn, by averaging new values with old values - it may take a few restarts before hint converge to stable values */
				hintDefault = (uintptr_t)MM_Math::weightedAverage((float)hintDefaultOld, (float)hintDefault, (1.0f - extensions->heapSizeStartupHintWeightNewValue));
				hintTenure = (uintptr_t)MM_Math::weightedAverage((float)hintTenureOld, (float)hintTenure, (1.0f - extensions->heapSizeStartupHintWeightNewValue));

				vm->sharedClassConfig->storeGCHints(currentThread, hintDefault, hintTenure, true);
				/* Nothing to do if store fails, storeGCHints already issues a trace point */
			}
		}
	}
}


void
gcExpandHeapOnStartup(J9JavaVM *javaVM)
{
	J9SharedClassConfig *sharedClassConfig = javaVM->sharedClassConfig;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	J9VMThread *currentThread = javaVM->internalVMFunctions->currentVMThread(javaVM);
	MM_EnvironmentBase env(currentThread->omrVMThread);

	if ((NULL != sharedClassConfig) && extensions->useGCStartupHints) {
		if (extensions->isStandardGC()) {
			uintptr_t hintDefault = 0;
			uintptr_t hintTenure = 0;

			if (0 == sharedClassConfig->findGCHints(currentThread, &hintDefault, &hintTenure)) {

				/* Default/Tenure MemorySubSpace is of type Generic (which is MemoryPool owner, while the parents are of type Flat/SemiSpace).
				 * For SemiSpace the latter (parent) ones are what we want to deal with (expand), since it's what includes both Allocate And Survivor children.
				 * For Flat it would probably make no difference if we used parent or child, but let's be consistent and use parent, too.
				 */
				MM_MemorySubSpace *defaultMemorySubSpace = extensions->heap->getDefaultMemorySpace()->getDefaultMemorySubSpace()->getParent();
				MM_MemorySubSpace *tenureMemorySubspace = extensions->heap->getDefaultMemorySpace()->getTenureMemorySubSpace()->getParent();


				/* Standard GCs always have Default MSS (which is equal to Tenure for flat heap configuration).
				 * So the simplest is always deal with Default, regardless if's generational heap configuration or not.
				 * We deal with Tenure only if only not equal to Default (which implies it's generational)
				 * We are a bit conservative and aim for slightly lower values that historically recorded by hints.
				 */
				uintptr_t hintDefaultAdjusted = (uintptr_t)(hintDefault * extensions->heapSizeStartupHintConservativeFactor);
				uintptr_t defaultCurrent = defaultMemorySubSpace->getActiveMemorySize();

				if (hintDefaultAdjusted > defaultCurrent) {
					extensions->heap->getResizeStats()->setLastExpandReason(HINT_PREVIOUS_RUNS);
					defaultMemorySubSpace->expand(&env, hintDefaultAdjusted - defaultCurrent);
				}

				if (defaultMemorySubSpace != tenureMemorySubspace) {
					uintptr_t hintTenureAdjusted = (uintptr_t)(hintTenure * extensions->heapSizeStartupHintConservativeFactor);
					uintptr_t tenureCurrent = tenureMemorySubspace->getActiveMemorySize();

					if (hintTenureAdjusted > tenureCurrent) {
						extensions->heap->getResizeStats()->setLastExpandReason(HINT_PREVIOUS_RUNS);
						tenureMemorySubspace->expand(&env, hintTenureAdjusted - tenureCurrent);
					}
				}

			}
			/* Nothing to do if findGCHints failed. It already issues a trace point - no need to duplicate it here */
		}
		/* todo: Balanced GC */
	}
}


/**
 * Cleanup Finalizer and Heap components
 */
void
gcShutdownHeapManagement(J9JavaVM *javaVM)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	MM_Collector *globalCollector = extensions->getGlobalCollector();

#if defined(J9VM_GC_FINALIZATION)
	/* wait for finalizer shutdown */
	j9gc_finalizer_shutdown(javaVM);
#endif /* J9VM_GC_FINALIZATION */

	/* Kickoff shutdown of global collector */
	if (NULL != globalCollector) {
		globalCollector->collectorShutdown(extensions);
	}

	if (extensions->dispatcher) {
		extensions->dispatcher->shutDownThreads();
	}
}

/**
 * Free any resources allocated by gcInitializeWithDefaultValues
 */
void
gcCleanupInitializeDefaults(OMR_VM* omrVM)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(omrVM);
	MM_EnvironmentBase env(omrVM);
	J9JavaVM *vm = (J9JavaVM*) omrVM->_language_vm;

	if (NULL == extensions) {
		return;
	}

	if (vm->defaultMemorySpace) {
		reportPrivateHeapDelete(vm, vm->defaultMemorySpace);
	}

	/* defaultMemorySpace is cleared as part of configuration tear down */
	if (NULL != extensions->configuration) {
		extensions->configuration->kill(&env);
	}

	extensions->kill(&env);

	omrVM->_gcOmrVMExtensions = NULL;
	((J9JavaVM*)omrVM->_language_vm)->gcExtensions = NULL;
}

static UDATA
normalizeParameter(UDATA parameter, UDATA numerator, UDATA denominator, UDATA max, UDATA min, UDATA roundTo)
{
	UDATA value = (parameter / denominator) * numerator;

	value = MM_Math::roundToCeiling(roundTo, value);
	value = (value > max) ? max : value;
	value = (value < min) ? min : value;

	return value;
}

/**
 * Calculate the memory parameter value.
 * Calculate and store memory parameters in destinationStruct based on the data found in sourceStruct
 * @param parameterInfo pointer to parameter info structure
 * @param memoryParameters array of parameter values
 */
static void
gcCalculateAndStoreMemoryParameter(MM_GCExtensions *destinationStruct, MM_GCExtensions *sourceStruct, const J9GcMemoryParameter *parameterInfo, IDATA *memoryParameters)
{
	if (-1 == memoryParameters[parameterInfo->optionName]) {
		/* Only parameters not specified by the user may be massaged based on other values. */
		destinationStruct->*(parameterInfo->fieldOffset) =
			normalizeParameter(sourceStruct->*(parameterInfo->valueBaseOffset),
												parameterInfo->scaleNumerator,
												parameterInfo->scaleDenominator,
												parameterInfo->valueMax,
												parameterInfo->valueMin,
												parameterInfo->valueRound);
	}
}

/**
 * Calculate memory parameter values.
 * Only parameters not specified by the user may be massaged based on other values.
 * @param memoryParameters array of parameter values
 */
static jint
gcInitializeCalculatedValues(J9JavaVM *javaVM, IDATA* memoryParameters)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	jint result = JNI_OK;

	/* Set initial Xms value: 8M
	 *
	 * Note:  Will need to verify Xms/Xmos/Xmns fit in user provided values of Xmx.
	 * This used to be for free, calculations based on Xmx, now it is a manual check
	 * in setConfigurationSpecificMemoryParameters
	 */
	/* TODO aryoung: eventually segregated heaps will allow heap expansion, although metronome
	 * itself will still require a fully expanded heap on startup */
	const bool shouldMaximizeInitialHeap = (extensions->isSegregatedHeap() || extensions->isMetronomeGC());
	const UDATA initialXmsValueMax = shouldMaximizeInitialHeap ? J9_MEMORY_MAX : (8 * 1024 * 1024);
	const UDATA initialXmsValueMin = shouldMaximizeInitialHeap ? UDATA_MAX     : (8 * 1024 * 1024);

	/**
	 * GC memory parameters to be store in GCExtensions
	 * opt_Xms will be set to initialXmsValue due to valueMax and valueMin being set
	 * opt_Xmns will be 50% of Xms
	 * opt_Xmos will be 50% of Xms
	 *
	 * Note: MM_GCExtensions::newSpaceSize should be set here based on opt_Xmns option
	 */
	const struct J9GcMemoryParameter GCExtensionsParameterTable [] = {
		{ &MM_GCExtensions::initialMemorySize, opt_Xms, initialXmsValueMax, initialXmsValueMin, &MM_GCExtensions::maxSizeDefaultMemorySpace, 1, 1, extensions->regionSize },
		{ &MM_GCExtensions::minNewSpaceSize, opt_Xmns, (UDATA)-1, 2*MINIMUM_NEW_SPACE_SIZE, &MM_GCExtensions::initialMemorySize, 1, 4, 2*extensions->regionSize },
		{ &MM_GCExtensions::newSpaceSize, opt_Xmns, (UDATA)-1, 2*MINIMUM_NEW_SPACE_SIZE, &MM_GCExtensions::initialMemorySize, 1, 4, 2*extensions->regionSize },
		{ &MM_GCExtensions::maxNewSpaceSize, opt_Xmnx, (UDATA)-1, 2*MINIMUM_NEW_SPACE_SIZE, &MM_GCExtensions::maxSizeDefaultMemorySpace, 1, 4, 2*extensions->regionSize },
		{ &MM_GCExtensions::minOldSpaceSize, opt_Xmos, initialXmsValueMax, MINIMUM_OLD_SPACE_SIZE, &MM_GCExtensions::initialMemorySize, 3, 4, extensions->regionSize },
		{ &MM_GCExtensions::oldSpaceSize, opt_Xmos, initialXmsValueMax, MINIMUM_OLD_SPACE_SIZE, &MM_GCExtensions::initialMemorySize, 3, 4, extensions->regionSize },
		{ &MM_GCExtensions::maxOldSpaceSize, opt_Xmox, (UDATA)-1, MINIMUM_OLD_SPACE_SIZE, &MM_GCExtensions::maxSizeDefaultMemorySpace, 1, 1, extensions->regionSize },
		{ &MM_GCExtensions::allocationIncrement, opt_Xmoi, J9_ALLOCATION_INCREMENT_MAX, J9_ALLOCATION_INCREMENT_MIN, &MM_GCExtensions::maxSizeDefaultMemorySpace, J9_ALLOCATION_INCREMENT_NUMERATOR, J9_ALLOCATION_INCREMENT_DENOMINATOR, extensions->regionSize },
		{ &MM_GCExtensions::fixedAllocationIncrement, opt_none, J9_FIXED_SPACE_SIZE_MAX, J9_FIXED_SPACE_SIZE_MIN, &MM_GCExtensions::maxSizeDefaultMemorySpace, J9_FIXED_SPACE_SIZE_NUMERATOR, J9_FIXED_SPACE_SIZE_DENOMINATOR, extensions->regionSize },
	};

	const IDATA GCExtensionsParameterTableSize = (sizeof(GCExtensionsParameterTable) / sizeof(struct J9GcMemoryParameter));
	IDATA tableIndex;

	/* Set the values which live in the JavaVM since they can't use the common GCExtensions member pointer approach */
	if (-1 == memoryParameters[opt_Xmca]) {
		javaVM->ramClassAllocationIncrement = normalizeParameter(extensions->maxSizeDefaultMemorySpace, J9_RAM_CLASS_ALLOCATION_INCREMENT_NUMERATOR, J9_RAM_CLASS_ALLOCATION_INCREMENT_DENOMINATOR, J9_RAM_CLASS_ALLOCATION_INCREMENT_MAX, J9_RAM_CLASS_ALLOCATION_INCREMENT_MIN, J9_RAM_CLASS_ALLOCATION_INCREMENT_ROUND_TO);
	}
	if (-1 == memoryParameters[opt_Xmco]) {
		javaVM->romClassAllocationIncrement = normalizeParameter(extensions->maxSizeDefaultMemorySpace, J9_ROM_CLASS_ALLOCATION_INCREMENT_NUMERATOR, J9_ROM_CLASS_ALLOCATION_INCREMENT_DENOMINATOR, J9_ROM_CLASS_ALLOCATION_INCREMENT_MAX, J9_ROM_CLASS_ALLOCATION_INCREMENT_MIN, J9_ROM_CLASS_ALLOCATION_INCREMENT_ROUND_TO);
	}

	/* Walk the dependency table fixing unspecified parameters to calculated defaults (mapping GCExtensions values to other GCExtensions values) */
	for(tableIndex=0; tableIndex < GCExtensionsParameterTableSize; tableIndex++) {
		gcCalculateAndStoreMemoryParameter(extensions, extensions, &(GCExtensionsParameterTable[tableIndex]), memoryParameters);
	}

#if defined (J9VM_GC_VLHGC)
	if (0 == extensions->tarokRememberedSetCardListSize) {
		uintptr_t cardSize = MM_RememberedSetCard::cardSize(extensions->compressObjectReferences());
		/* 4% of region size is allocated for region's RSCL memory */
		extensions->tarokRememberedSetCardListSize = extensions->regionSize * 4 / 100 / cardSize;
	}

	if (0 == extensions->tarokRememberedSetCardListMaxSize) {
		 /* Individual RSCL can grow up to 8x of its memory size */
		extensions->tarokRememberedSetCardListMaxSize = 8 * extensions->tarokRememberedSetCardListSize;
	}

#endif /* defined (J9VM_GC_VLHGC) */

	/* Number of GC threads must be initialized at this point */
	Assert_MM_true(0 < extensions->gcThreadCount);

	/* initialize the size of Local Object Buffer */
	if (0 == extensions->objectListFragmentCount) {
		extensions->objectListFragmentCount = (4 * extensions->gcThreadCount) + 4;
	}
	return result;
}

/**
 * Verify memory parameters.
 *
 * Some configurations do not honour all memory parameters provided by the user.  Set these
 * parameters to default values and make it appear the user did not specify any values for these
 * parameters.  A routine that thus checks for user provided input will not validate these
 * parameters.  Routines that modify configuration specific parameters will need to ensure
 * they do not modify these same parameters.
 *
 * For a flat configuration -Xmn is ignored:
 * 		-Xmn/-Xmns are set to 0
 * 		-Xmnx is set to -Xmx
 * 		memoryParameters structure is modified to make it look like the user did not specify these values.
 *
 * @param memoryParameters array of parameter values
 * @return JNI_OK on success, JNI_ERR on failure
 */
jint
setConfigurationSpecificMemoryParameters(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	bool opt_XmsSet = (-1 != memoryParameters[opt_Xms]);
	bool opt_XmnsSet = (-1 != memoryParameters[opt_Xmns]);
	bool opt_XmosSet = (-1 != memoryParameters[opt_Xmos]);
	bool opt_XmnxSet = (-1 != memoryParameters[opt_Xmnx]);

	if (flatConfiguration) {
		/* Xmns = 0, override fact user may have provided a value */
		extensions->minNewSpaceSize = 0;
		extensions->newSpaceSize = 0;
		extensions->maxNewSpaceSize = 0;
		memoryParameters[opt_Xmns] = memoryParameters[opt_Xmnx] = memoryParameters[opt_Xmn] = -1;

		extensions->absoluteMinimumOldSubSpaceSize = MINIMUM_VM_SIZE;
	}

	/* Emulation of sovereign behaviour results in Xmx, and Xms being hardcoded.  If a value smaller than
	 * the hardcoded minimum for Xmx is supplied, and this value is less than the hardcoded value of Xms
	 * then Xms, Xmns and Xmos need to be re-calculated.
	 */
	/* If Xms was set by the user, then initial guesses for Xmos and Xmns will
	 * be used in final calculations of those values (i.e. the current values
	 * are good enough).
	 */
	if (!opt_XmsSet) {
		if (extensions->initialMemorySize > extensions->maxSizeDefaultMemorySpace) {
			extensions->initialMemorySize = extensions->maxSizeDefaultMemorySpace;

			if (!opt_XmosSet) {
				extensions->oldSpaceSize = MM_Math::roundToFloor(extensions->heapAlignment, extensions->initialMemorySize/2);
				extensions->oldSpaceSize = MM_Math::roundToFloor(extensions->regionSize, extensions->oldSpaceSize);
				extensions->oldSpaceSize = (extensions->oldSpaceSize >= extensions->absoluteMinimumOldSubSpaceSize) ? extensions->oldSpaceSize : extensions->absoluteMinimumOldSubSpaceSize;
				extensions->minOldSpaceSize = extensions->oldSpaceSize;
			}

			if (!flatConfiguration) {
				if (!opt_XmnsSet) {
					extensions->newSpaceSize = MM_Math::roundToFloor(extensions->heapAlignment, extensions->initialMemorySize/2);
					extensions->newSpaceSize = MM_Math::roundToFloor(extensions->regionSize, extensions->newSpaceSize);
					extensions->newSpaceSize = (extensions->newSpaceSize >= (2*extensions->absoluteMinimumNewSubSpaceSize)) ? extensions->newSpaceSize : (2*extensions->absoluteMinimumNewSubSpaceSize);
					extensions->minNewSpaceSize = extensions->newSpaceSize;
				}
			}
		}
	}

	if (!flatConfiguration && !opt_XmnxSet) {
		/* If Xmnx is not set by the user, limit it to the 1/4 of the maximum heap size */
		extensions->maxNewSpaceSize = MM_Math::roundToFloor(extensions->heapAlignment * 2,extensions->memoryMax / 4);
		extensions->maxNewSpaceSize = MM_Math::roundToFloor(extensions->regionSize * 2, extensions->maxNewSpaceSize);
	}
	return JNI_OK;
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xmx" or "-XX:MaxRAMPercentage"
 */
static const char *
displayXmxOrMaxRAMPercentage(IDATA* memoryParameters)
{
	if ((-1 != memoryParameters[opt_maxRAMPercent])
		&& (memoryParameters[opt_Xmx] == memoryParameters[opt_maxRAMPercent])
	) {
		return "-Xmx (as set by -XX:MaxRAMPercentage)";
	} else {
		return "-Xmx";
	}
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xms" or "-XX:InitialRAMPercentage"
 */
static const char *
displayXmsOrInitialRAMPercentage(IDATA* memoryParameters)
{
	if ((-1 != memoryParameters[opt_initialRAMPercent])
		&& (memoryParameters[opt_Xms] == memoryParameters[opt_initialRAMPercent])
	) {
		return "-Xms (as set by -XX:InitialRAMPercentage)";
	} else {
		return "-Xms";
	}
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xmn" or "-Xmns"
 */
const char *
displayXmnOrXmns(IDATA* memoryParameters)
{
	return (memoryParameters[opt_Xmn] == memoryParameters[opt_Xmns]) ? "-Xmn" : "-Xmns";
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xmn" or "-Xmnx" *
 */
const char *
displayXmnOrXmnx(IDATA* memoryParameters)
{
	return (memoryParameters[opt_Xmn] == memoryParameters[opt_Xmnx]) ? "-Xmn" : "-Xmnx";
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xmo" or "-Xmos" *
 */
const char *
displayXmoOrXmos(IDATA* memoryParameters)
{
	return (memoryParameters[opt_Xmo] == memoryParameters[opt_Xmos]) ? "-Xmo" : "-Xmos";
}

/**
 * Verify memory parameters.
 *
 * Determine which memory parameter to display to the user in the event of an error.
 * @param memoryParameters array of parameter values
 * @return pointer to "-Xmo" or "-Xmox"
 */
const char *
displayXmoOrXmox(IDATA* memoryParameters)
{
	return (memoryParameters[opt_Xmo] == memoryParameters[opt_Xmox]) ? "-Xmo" : "-Xmox";
}
/**
 * Verify memory parameters.
 *
 * Ensure that Xmx is larger than the minimum size.
 * Ensure that Xmdx is larger than the minimum size, less than Xmx.
 *
 * The following is guaranteed after this method
 *
 * 1/ minimumConfigurationSize <= Xmx
 * 2/ minimumConfigurationSize <= Xmdx <= Xmx
 *
 * @param javaVM The javaVM
 * @param memoryParameters Memory options provided by user
 * @param flatConfiguration True if running without scavenger
 * @param minimumSize The size for lower bound testing
 * @param memoryOption1 If non NULL then this and memoryOption2 are used in error messages as a sum (e.g. sum of memoryOption1 and memoryOption2 is ...)
 * @param memoryOption2 See memoryOption1
 *
 * @return JNI_OK on success, JNI_ERR on failure.
 * @note error message is printed out indicating type of failure
 */
static jint
gcInitializeXmxXmdxVerification(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration, UDATA minimumSizeValue, const char *memoryOption1, const char *memoryOption2)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);

	/* Error reporting */
	const char *subSpaceTooLargeOption;

	bool opt_XmxSet = (-1 != memoryParameters[opt_Xmx]);
	bool opt_XmdxSet = (-1 != memoryParameters[opt_Xmdx]);
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	/* Align both Xmx and Xmdx */
	extensions->memoryMax = MM_Math::roundToFloor(extensions->heapAlignment, extensions->memoryMax);
	extensions->maxSizeDefaultMemorySpace = MM_Math::roundToFloor(extensions->heapAlignment, extensions->maxSizeDefaultMemorySpace);
	extensions->memoryMax = MM_Math::roundToFloor(extensions->regionSize, extensions->memoryMax);
	extensions->maxSizeDefaultMemorySpace = MM_Math::roundToFloor(extensions->regionSize, extensions->maxSizeDefaultMemorySpace);

#if defined (OMR_GC_COMPRESSED_POINTERS)
	if (extensions->compressObjectReferences()) {
		if (extensions->shouldAllowShiftingCompression) {
			if (extensions->shouldForceSpecifiedShiftingCompression) {
				extensions->heapCeiling = NON_SCALING_LOW_MEMORY_HEAP_CEILING << extensions->forcedShiftingCompressionAmount;
			} else {
				extensions->heapCeiling = LOW_MEMORY_HEAP_CEILING;
			}
		} else {
			extensions->heapCeiling = NON_SCALING_LOW_MEMORY_HEAP_CEILING;
		}

#if defined(J9ZOS39064)
		{
			/*
			 * In order to support Compressed References ZOS should support one of:
			 * - 2_TO_64 to support heaps allocation below 64GB
			 * - 2_TO_32 to support heaps allocation below 32GB
			 */
			U_64 maxHeapForCR = zosGetMaxHeapSizeForCR();
			if (0 == maxHeapForCR) {
				/* Redirector should not allow to run Compressed References JVM if options are not available */
				Assert_MM_unreachable();
				/* Fail to initialize if assertions are off */
				return JNI_ERR;
			}
	
			/* Adjust heap ceiling value if it is necessary */
			if (extensions->heapCeiling > maxHeapForCR) {
				extensions->heapCeiling = maxHeapForCR;
			}
		}
#endif /* defined(J9ZOS39064) */

		if (extensions->memoryMax > (extensions->heapCeiling - J9GC_COMPRESSED_POINTER_NULL_REGION_SIZE)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_OVERFLOW, displayXmxOrMaxRAMPercentage(memoryParameters));
			return JNI_ERR;
		}
	}
#endif /* defined (OMR_GC_COMPRESSED_POINTERS) */

	/* Verify Xmx is too small */
	if (extensions->memoryMax < minimumSizeValue) {
		if (NULL == memoryOption1) {
			memoryOption1 = displayXmxOrMaxRAMPercentage(memoryParameters);
			memoryOption2 = NULL;
			goto _subSpaceTooSmallForValue;
		} else {
			if (opt_XmxSet) {
				subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}
	}

#if defined(J9ZTPF)
	/* Only if Xmx is specified, verify the value doesn't go over usable memory. */
	if (opt_XmxSet) {
		if (extensions->memoryMax > extensions->usablePhysicalMemory) {
			memoryOption1 = "-Xmx";
			memoryOption2 = NULL;
			goto _subSpaceTooLargeForHeap;
		}
	}
#endif /* defined(J9ZTPF) */

	/* Verify Xmdx is not too small, or too big */
	if (opt_XmdxSet) {
		if (extensions->maxSizeDefaultMemorySpace < minimumSizeValue) {
			if (NULL == memoryOption1) {
				memoryOption1 = "-Xmdx";
				memoryOption2 = NULL;
				goto _subSpaceTooSmallForValue;
			} else {
				subSpaceTooLargeOption = "-Xmdx";
				goto _subSpaceTooLarge;
			}
		}

		if (extensions->maxSizeDefaultMemorySpace > extensions->memoryMax) {
			memoryOption1 = "-Xmdx";
			memoryOption2 = NULL;
			if (opt_XmxSet) {
				subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}
	} else {
		/* Special case for Xmdx.  The Xmdx value is calculated as a fraction of Xmx.
		 * It is possible for small enough values of Xmx that Xmdx will be less than
		 * the size of a subSpace.  Ensure Xmdx is at least as large as the configuration
		 * size in this case.  Xmx is already known to be larger than that size.
		 */
		if (extensions->maxSizeDefaultMemorySpace < minimumSizeValue) {
			extensions->maxSizeDefaultMemorySpace = minimumSizeValue;
		}
	}

	/* Still need to verify the minimum size of Xmx/Xmdx is not less than the required
	 * minimum subSpace size (oldSpace/NewSpace).  Do this verification after those minimum
	 * values are verified.
	 */
	return JNI_OK;

_subSpaceTooSmallForValue:
	{
		const char *qualifier = NULL;
		qualifiedSize(&minimumSizeValue, &qualifier);
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_SMALL_FOR_VALUE, memoryOption1, minimumSizeValue, qualifier);
		return JNI_ERR;
	}

_subSpaceTooLarge:
	if (NULL == memoryOption2) {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE, memoryOption1, subSpaceTooLargeOption);
	} else {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE, memoryOption1, memoryOption2, subSpaceTooLargeOption);
	}
	return JNI_ERR;

_subSpaceTooLargeForHeap:
	if (NULL == memoryOption2) {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE_FOR_HEAP, memoryOption1);
	} else {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE_FOR_HEAP, memoryOption1, memoryOption2);
	}
	return JNI_ERR;
}

/**
 * Verify memory parameters.
 *
 * For each memory parameter specified by the user verify the following conditions independent
 * of any other memory parameter: alignment, minimum size, maximum size.
 *
 * Maximum size is compared against either Xmx, Xmdx or Xms.  The largest of these values
 * that is set by the user is used where applicable.
 *
 * For all user provided memory parameters, the following are guaranteed upon completion
 *
 * 1/ minimumConfigurationSize <= Xms <= Xmdx
 * 2/ absoluteMinimumOldSubSpaceSize <= Xmos <= Xmox <= Xms|Xmdx|Xmx
 * 3/ absoluteMinimumNewSubSpaceSize <= Xmns <= Xmnx <= Xms|Xmdx|Xmx
 *
 * @return JNI_OK on success, JNI_ERR on failure.
 * @note error message is printed out indicating type of failure
 */
jint
independentMemoryParameterVerification(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration)
{
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);

	UDATA minimumSizeValue; /* For displaying error message */
	const char *memoryOption = NULL;
	const char *memoryOption2 = NULL;
	const char *subSpaceTooLargeOption;

	/* Maximum size for Xmdx, Xmox, Xmnx */
	UDATA maximumXmdxValue = extensions->memoryMax;
	const char *maximumXmdxValueParameter = NULL;

	/* Maximum size for Xms, Xmos, Xmns */
	UDATA maximumXmsValue = extensions->memoryMax;
	const char *maximumXmsValueParameter = NULL;
	UDATA XmsMinimumValue = 0;

	bool opt_XmxSet = (-1 != memoryParameters[opt_Xmx]);
	bool opt_XmdxSet = (-1 != memoryParameters[opt_Xmdx]);
	bool opt_XmsSet = (-1 != memoryParameters[opt_Xms]);
	bool opt_XmnsSet = (-1 != memoryParameters[opt_Xmns]);
	bool opt_XmnxSet = (-1 != memoryParameters[opt_Xmnx]);
	bool opt_XmosSet = (-1 != memoryParameters[opt_Xmos]);
	bool opt_XmoxSet = (-1 != memoryParameters[opt_Xmox]);
	bool opt_XsoftmxSet = (-1 != memoryParameters[opt_Xsoftmx]);

	/* These values will be reset if Xmos/Xmns are user provided.
	 * Initially minimum size to verify Xmos/Xmns input.
	 *
	 * Can't use extensions->minNewSpaceSize as that value is pre-calculated
	 * to be a realistic value; a valid user input value may be less than this
	 * calculated value, but it must be greater than the minimum value.
	 */
	UDATA oldSpaceSizeMinimum = extensions->absoluteMinimumOldSubSpaceSize;
	UDATA newSpaceSizeMinimum = 2*extensions->absoluteMinimumNewSubSpaceSize;

	/* Xmx and Xmdx have already been verified
	 * Need to set maximumXmdxValue and maximumXmsValue as per Xmdx settings
	 * Can not verify minimum values until Xmns/Xmos have been verified.
	 */
	if (opt_XmxSet) {
		maximumXmdxValueParameter = displayXmxOrMaxRAMPercentage(memoryParameters);
		maximumXmsValueParameter = displayXmxOrMaxRAMPercentage(memoryParameters);
	}

	if (opt_XmdxSet) {
		/* When comparing to maximum boundary use the Xmdx value */
		maximumXmdxValue = extensions->maxSizeDefaultMemorySpace;
		maximumXmdxValueParameter = "-Xmdx";

		/* When comparing initialMemorySizes to maximum boundary, use the Xmdx value
		 * Just need to reset the reported parameter, value is currently set to Xmdx
		 */
		maximumXmsValue = extensions->maxSizeDefaultMemorySpace;
		maximumXmsValueParameter = "-Xmdx";
	}

	/* Align Xms, verify it is not too large */
	if (opt_XmsSet) {
		extensions->initialMemorySize = MM_Math::roundToFloor(extensions->heapAlignment, extensions->initialMemorySize);
		extensions->initialMemorySize = MM_Math::roundToFloor(extensions->regionSize, extensions->initialMemorySize);

		if (flatConfiguration) {
			/* Flat configuration Collector can start with one region */
			extensions->initialMemorySize = OMR_MAX(extensions->regionSize, extensions->initialMemorySize);
		} else {
			/* Gencon required at least three regions to start with (one for Tenure and two for Nursery - one for each half) */
			extensions->initialMemorySize = OMR_MAX(extensions->regionSize * 3, extensions->initialMemorySize);
		}

		if (extensions->initialMemorySize > maximumXmsValue) {
			memoryOption = displayXmsOrInitialRAMPercentage(memoryParameters);
			if (maximumXmsValueParameter) {
				subSpaceTooLargeOption = maximumXmsValueParameter;
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}

		/* When comparing to maximum boundary use the Xms value */
		maximumXmsValue = extensions->initialMemorySize;
		maximumXmsValueParameter = displayXmsOrInitialRAMPercentage(memoryParameters);
	}

	/* verify -Xsoftmx is set between -Xms and -Xmx */
	if (opt_XsoftmxSet) {
		extensions->softMx = MM_Math::roundToFloor(extensions->heapAlignment, extensions->softMx);
		extensions->softMx = MM_Math::roundToFloor(extensions->regionSize, extensions->softMx);

		if (extensions->softMx > extensions->memoryMax) {
			memoryOption = "-Xsoftmx";
			goto _subSpaceTooLargeForHeap;
		}

		if (extensions->softMx < extensions->initialMemorySize) {
			memoryOption = "-Xsoftmx";
			minimumSizeValue = extensions->initialMemorySize;
			goto _subSpaceTooSmallForValue;
		}
	}

	/* Align Xmns, verify it is not too large or too small.
	 * Defer checking that Xmns <= Xmnx as Xmnx has not been verified yet.
	 * There are two semi-spaces
	 */
	if (opt_XmnsSet) {
		extensions->newSpaceSize = MM_Math::roundToFloor(2*extensions->heapAlignment, extensions->newSpaceSize);
		extensions->newSpaceSize = MM_Math::roundToFloor(2*extensions->regionSize, extensions->newSpaceSize);
		extensions->newSpaceSize = OMR_MAX(extensions->regionSize * 2, extensions->newSpaceSize);

		if (extensions->newSpaceSize < newSpaceSizeMinimum) {
			memoryOption = displayXmnOrXmns(memoryParameters);
			minimumSizeValue = newSpaceSizeMinimum; /* display min size */
			goto _subSpaceTooSmallForValue;
		}

		if (extensions->newSpaceSize > maximumXmsValue) {
			memoryOption = displayXmnOrXmns(memoryParameters);
			if (maximumXmsValueParameter) {
				subSpaceTooLargeOption = maximumXmsValueParameter; /* display correct parameter */
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}

		/* Update newSpaceSizeMinimum  */
		newSpaceSizeMinimum = extensions->newSpaceSize;
		extensions->minNewSpaceSize = extensions->newSpaceSize;
	}

	/* Align Xmnx, verify it is not too large or too small.
	 * There are two semi-spaces
	 */
	if (opt_XmnxSet) {
		extensions->maxNewSpaceSize = MM_Math::roundToFloor(2*extensions->heapAlignment, extensions->maxNewSpaceSize);
		extensions->maxNewSpaceSize = MM_Math::roundToFloor(2*extensions->regionSize, extensions->maxNewSpaceSize);

		if (extensions->maxNewSpaceSize < newSpaceSizeMinimum) {
			memoryOption = displayXmnOrXmnx(memoryParameters);
			minimumSizeValue = newSpaceSizeMinimum; /* display min size */
			goto _subSpaceTooSmallForValue;
		}

		if (extensions->maxNewSpaceSize > maximumXmdxValue) {
			memoryOption = displayXmnOrXmnx(memoryParameters);
			if (maximumXmdxValueParameter) {
				subSpaceTooLargeOption = maximumXmdxValueParameter;
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}

		if (opt_XmnsSet && (extensions->maxNewSpaceSize < extensions->newSpaceSize)) {
			memoryOption = displayXmnOrXmns(memoryParameters);
			subSpaceTooLargeOption = displayXmnOrXmnx(memoryParameters);
			goto _subSpaceTooLarge;
		}
	}


	/* Align Xmos, verify it is not too large or too small.
	 * Defer checking that Xmos < Xmox as Xmox has not been verified yet.
	 */
	if (opt_XmosSet) {
		extensions->oldSpaceSize = MM_Math::roundToFloor(extensions->heapAlignment, extensions->oldSpaceSize);
		extensions->oldSpaceSize = MM_Math::roundToFloor(extensions->regionSize, extensions->oldSpaceSize);
		extensions->oldSpaceSize = OMR_MAX(extensions->regionSize, extensions->oldSpaceSize);

		if (extensions->oldSpaceSize < oldSpaceSizeMinimum) {
			memoryOption = displayXmoOrXmos(memoryParameters);
			minimumSizeValue = oldSpaceSizeMinimum;
			goto _subSpaceTooSmallForValue;
		}

		if (extensions->oldSpaceSize > maximumXmsValue) {
			memoryOption = displayXmoOrXmos(memoryParameters);
			if (maximumXmsValueParameter) {
				subSpaceTooLargeOption = maximumXmsValueParameter; /* display correct parameter */
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}

		/* Update local oldSpaceSizeMinimum */
		oldSpaceSizeMinimum = extensions->oldSpaceSize;
		extensions->minOldSpaceSize = extensions->oldSpaceSize;
	}

	/* Align Xmox, verify it is not too large or too small. */
	if (opt_XmoxSet) {
		extensions->maxOldSpaceSize = MM_Math::roundToFloor(extensions->heapAlignment, extensions->maxOldSpaceSize);
		extensions->maxOldSpaceSize = MM_Math::roundToFloor(extensions->regionSize, extensions->maxOldSpaceSize);

		if (extensions->maxOldSpaceSize < oldSpaceSizeMinimum) {
			/* Display minSubSpace size, or Xmos/Xmox value if both were set */
			if (opt_XmosSet) {
				subSpaceTooLargeOption = displayXmoOrXmox(memoryParameters);
				memoryOption = displayXmoOrXmos(memoryParameters);
				goto _subSpaceTooLarge;
			}
			memoryOption = displayXmoOrXmox(memoryParameters);
			minimumSizeValue = oldSpaceSizeMinimum;
			goto _subSpaceTooSmallForValue;
		}

		if (extensions->maxOldSpaceSize > maximumXmdxValue) {
			memoryOption = displayXmoOrXmox(memoryParameters);
			if (maximumXmdxValueParameter) {
				subSpaceTooLargeOption = maximumXmdxValueParameter;
				goto _subSpaceTooLarge;
			}
			goto _subSpaceTooLargeForHeap;
		}

		if (opt_XmosSet && (extensions->maxOldSpaceSize < extensions->oldSpaceSize)) {
			memoryOption = displayXmoOrXmos(memoryParameters);
			subSpaceTooLargeOption = displayXmoOrXmox(memoryParameters);
			goto _subSpaceTooLarge;
		}

	}

	/* Verify Xmx,Xmdx,Xms are not too small based on minimum oldSpace and newSpace values */
	XmsMinimumValue = oldSpaceSizeMinimum;

	if (!flatConfiguration) {
		XmsMinimumValue += newSpaceSizeMinimum;
	}

	/* Before verifying Xmx/Xmdx is large enough for user provided values of Xmos/Xmns, create an
	 * output buffer so the error messages can be displayed in relation to specified memory parameters,
	 * rather than a random number (the sume of Xmos/Xmns for example).  This buffer may never
	 * be displayed if all values are good.
	 *
	 * If the user did not specify either Xmos or Xmns then this buffer will be null, and a number
	 * will be displayed if Xmx/Xmdx was specified too small.
	 */
	if (opt_XmosSet) {
		if (opt_XmnsSet) {
			memoryOption = displayXmoOrXmos(memoryParameters);
			memoryOption2 = displayXmnOrXmns(memoryParameters);
		} else {
			memoryOption = displayXmoOrXmos(memoryParameters);
		}
	} else {
		if (opt_XmnsSet) {
			memoryOption = displayXmnOrXmns(memoryParameters);
		}
	}

	/* Will display error message with size error.  For example if the combination of Xmos/Xmns is greater
	 * than the specified Xmdx value, says Xmdx is too small, must be at least xxx bytes, rather than
	 * the Sum of Xmos + Xmns is too large for Xmdx.  It is correct, just possibly confusing
	 */
	if (gcInitializeXmxXmdxVerification(javaVM, memoryParameters, flatConfiguration, XmsMinimumValue, memoryOption, memoryOption2) != JNI_OK) {
		return JNI_ERR;
	}

	if (opt_XmsSet && (extensions->initialMemorySize < XmsMinimumValue)) {
		if (NULL == memoryOption) {
			memoryOption = displayXmsOrInitialRAMPercentage(memoryParameters);
			minimumSizeValue = XmsMinimumValue; /* display min size */
			goto _subSpaceTooSmallForValue;
		} else {
			subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
			goto _subSpaceTooLarge;
		}
	}

	/* Align Xmoi if set by the user */
	if (extensions->allocationIncrementSetByUser) {
		extensions->allocationIncrement = MM_Math::roundToCeiling(extensions->heapAlignment, extensions->allocationIncrement);
		extensions->allocationIncrement = MM_Math::roundToCeiling(extensions->regionSize, extensions->allocationIncrement);
	}

#if defined(OMR_GC_COMPRESSED_POINTERS)
	if (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(javaVM)) {
		/* Align the Xmcrs if necessary */
		extensions->suballocatorInitialSize = MM_Math::roundToCeiling(SUBALLOCATOR_ALIGNMENT, extensions->suballocatorInitialSize);
	}
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */

	return JNI_OK;

_subSpaceTooSmallForValue:
	{
		const char *qualifier = NULL;
		qualifiedSize(&minimumSizeValue, &qualifier);
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_SMALL_FOR_VALUE, memoryOption, minimumSizeValue, qualifier);
		return JNI_ERR;
	}

_subSpaceTooLarge:
	if (NULL == memoryOption2) {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE, memoryOption, subSpaceTooLargeOption);
	} else {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE, memoryOption, memoryOption2, subSpaceTooLargeOption);
	}
	return JNI_ERR;

_subSpaceTooLargeForHeap:
	if (NULL == memoryOption2) {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE_FOR_HEAP, memoryOption);
	} else {
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE_FOR_HEAP, memoryOption, memoryOption2);
	}
	return JNI_ERR;

}

/**
 * Compares a potentially specified parameter value against a given limit value
 * @param toTest[in] The parameter to test
 * @param maximum[in] The maximum against which toTest must be compared
 * @return true if the given parameter toTest is either unspecified or not greater than the given fixed maximum
 */
static bool
isLessThanEqualOrUnspecifiedAgainstFixed(MM_UserSpecifiedParameterUDATA *toTest, UDATA maximum)
{
	bool isSafe = true;
	if (toTest->_wasSpecified) {
		isSafe = (toTest->_valueSpecified <= maximum);
	}
	return isSafe;
}

/**
 * Compares two potentially specified parameter values against to ensure that they have the permitted relative values
 * @param minimum[in] The parameter which, if specified, must be no greater than maximum
 * @param maximum[in] The parameter which, if specified, must be no less than maximum
 * @return true if not both of the parameters are specified or minimum is no greater than maximum
 */
static bool
isLessThanEqualOrUnspecifiedAgainstOption(MM_UserSpecifiedParameterUDATA *minimum, MM_UserSpecifiedParameterUDATA *maximum)
{
	bool isSafe = true;
	if (minimum->_wasSpecified && maximum->_wasSpecified) {
		isSafe = (minimum->_valueSpecified <= maximum->_valueSpecified);
	}
	return isSafe;
}

/**
 * Verify memory parameters.
 *
 * For all user set values verify that the provided values are valid for all configurations.
 * Since individual verification has already occurred, need only verify that combinations are
 * correct.  For example if Xmns + Xmos = Xms.
 *
 * Recalculate non user specified values if required.
 *
 * @return JNI_OK on success, JNI_ERR on failure.
 * @note error message is printed out indicating type of failure
 */
jint
combinationMemoryParameterVerification(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);

	/* For displaying error messages */
	const char *memoryOption = NULL;
	const char *memoryOption2 = NULL;
	const char *subSpaceTooLargeOption = NULL;
	const char *subSpaceTooSmallOption = NULL;
	const char *memoryOptionMinimumDifference = NULL;
	float minValue, maxValue;
	UDATA minimumSizeValue;

	/* The absolute maximum value that Xms can take, and what memory parameter set it (Xmx/Xmdx/Xms) */
	UDATA maximumXmsValue;
	const char *maximumXmsValueParameter = NULL;

	/* Bitmap of memory parameters set by the user */
	UDATA setMemoryParameters;
	bool opt_XmxSet = (-1 != memoryParameters[opt_Xmx]);
	bool opt_XmdxSet = (-1 != memoryParameters[opt_Xmdx]);
	bool opt_XmsSet = (-1 != memoryParameters[opt_Xms]);
	bool opt_XmnsSet = (-1 != memoryParameters[opt_Xmns]);
	bool opt_XmnxSet = (-1 != memoryParameters[opt_Xmnx]);
	bool opt_XmosSet = (-1 != memoryParameters[opt_Xmos]);
	bool opt_XmoxSet = (-1 != memoryParameters[opt_Xmox]);
	bool opt_XsoftmxSet = (-1 != memoryParameters[opt_Xsoftmx]);
	bool opt_XmcrsSet = (-1 != memoryParameters[opt_Xmcrs]);

	PORT_ACCESS_FROM_JAVAVM(javaVM);

	/* User specified combinations of Xmdx and Xms */
	setMemoryParameters = NONE;
	if (opt_XmdxSet) {
		setMemoryParameters |= XMDX;
	}
	if (opt_XmsSet) {
		setMemoryParameters |= XMS;
	}

	/* Determine maximumXmsValue.
	 * Verify Xmdx >= Xms where applicable
	 */
	switch (setMemoryParameters) {
	case XMDX_XMS:
		/* Xmdx, Xms - already verified
		 */
		assume0(extensions->maxSizeDefaultMemorySpace <= extensions->memoryMax);
		assume0(extensions->initialMemorySize <= extensions->maxSizeDefaultMemorySpace);

		maximumXmsValue = extensions->initialMemorySize;
		maximumXmsValueParameter = displayXmsOrInitialRAMPercentage(memoryParameters);
		break;

	case XMDX:
		/* Xmdx, ^Xms - verification required
		 * adjust Xms if necessary
		 */
		if (extensions->initialMemorySize > extensions->maxSizeDefaultMemorySpace) {
			extensions->initialMemorySize = extensions->maxSizeDefaultMemorySpace;
		}

		maximumXmsValue = extensions->maxSizeDefaultMemorySpace;
		maximumXmsValueParameter = "-Xmdx";
		break;

	case XMS:
		/* ^Xmdx, Xms - verification required
		 * adjust Xmdx if necessary
		 */
		if (extensions->initialMemorySize > extensions->maxSizeDefaultMemorySpace) {
			extensions->maxSizeDefaultMemorySpace = extensions->initialMemorySize;
		}

		maximumXmsValue = extensions->initialMemorySize;
		maximumXmsValueParameter = displayXmsOrInitialRAMPercentage(memoryParameters);
		break;

	case NONE:
		/* ^Xmdx, ^Xms - both calculated, verification not required
		 */
		assume0(extensions->maxSizeDefaultMemorySpace <= extensions->memoryMax);
		assume0(extensions->initialMemorySize <= extensions->maxSizeDefaultMemorySpace);

		maximumXmsValue = extensions->memoryMax;
		if (opt_XmxSet) {
			maximumXmsValueParameter = displayXmxOrMaxRAMPercentage(memoryParameters);
		}
		break;

	default:
		j9tty_printf(PORTLIB, "Internal GC error %p\n", setMemoryParameters); /* No NLS */
		assume0(0); /* should never get here */
		return JNI_ERR;
		break;
	}

	/* It is now known that for all Xms <= Xmdx <= Xmx for all scenarios (user set or not)
	 * It is also known what the absolute maximum value for Xmos + Xmns (maximumXmsValue).
	 * Further it is known which user set parameter that maximum value is dictated by (maximumXmsValueParameter).
	 *
	 * First deal with the flat configuration.  In this configuration Xmos = Xms, unless the
	 * user has specified values.  Xmns is 0.
	 */
	if (flatConfiguration) {
		/* User specified combinations of Xms and Xmos
		 */
		setMemoryParameters = NONE;
		if (opt_XmsSet) {
			setMemoryParameters |= XMS;
		}
		if (opt_XmosSet) {
			setMemoryParameters |= XMOS;
		}

		/* Following the switch statement memory parameters not set by the user
		 * will be reset as required
		 */
		switch (setMemoryParameters) {
		case XMS_XMOS:
			/* Previously verified that
			 * 1/ Xms <= Xmx
			 * 2/ Xms <= Xmdx, if Xmdx was specified
			 * 3/ Xmos <= Xms
			 * 4/ Xmos <= Xmnx, if Xmnx was specified
			 *
			 * Enforce Xms = Xmos
			 */
			if (extensions->oldSpaceSize != extensions->initialMemorySize) {
				memoryOption = displayXmoOrXmos(memoryParameters);
				memoryOption2 = displayXmsOrInitialRAMPercentage(memoryParameters);
				goto _subSpaceNotEqualError;
			}
			break;

		case XMS:
			/* Previously verified
			 * 1/ Xms <= Xmx
			 * 2/ Xms <= Xmdx, if Xmdx was specified
			 *
			 * Set Xmos = Xms, (Enforce Xms = Xmos)
			 * Ensure Xmos <= Xmox, if Xmox was specified
			 */
			if (extensions->isSegregatedHeap() || extensions->isMetronomeGC()) {
				/* TODO aryoung: eventually segregated heaps will allow heap expansion, although metronome
				 * itself will still require a fully expanded heap on startup */
#if defined(J9VM_GC_REALTIME) || defined(J9VM_GC_SEGREGATED_HEAP)
				if (extensions->initialMemorySize != extensions->memoryMax) {
					/* Ignore the specified -Xms value.
					 *  Override it too, so that verbose:sizes displays correctly the actual value.
					 */
					extensions->initialMemorySize = extensions->memoryMax;
					/* Sometime in future, we may want to give a hard error, instead.
					memoryOption = "-Xms";
					memoryOption2 = "-Xmx";
					goto _subSpaceNotEqualError;
					*/
				}
#endif /* defined(J9VM_GC_REALTIME) || defined(J9VM_GC_SEGREGATED_HEAP) */
			}

			extensions->oldSpaceSize = extensions->initialMemorySize;
			extensions->minOldSpaceSize = extensions->initialMemorySize;

			if (opt_XmoxSet && (extensions->oldSpaceSize > extensions->maxOldSpaceSize)) {
				memoryOption = displayXmoOrXmox(memoryParameters);
				subSpaceTooSmallOption = displayXmsOrInitialRAMPercentage(memoryParameters);
				goto _subSpaceTooSmall;
			}
			break;

		case XMOS:
			/* Previously verified
			 * 1/ Xmos <= Xmx
			 * 2/ Xmos <= Xmdx, if Xmdx was specified
			 * 3/ Xmos <= Xmnx, if Xmnx was specified
			 *
			 * Nothing left to do
			 */

			break;

		case NONE:
			/* Previously ensured that Xms is <= Xmdx <= Xmx
			 * Set Xmo = Xms for sovereign compliance
			 */
			extensions->oldSpaceSize = extensions->initialMemorySize;
			extensions->minOldSpaceSize = extensions->initialMemorySize;
			break;

		default:
			j9tty_printf(PORTLIB, "Internal GC error %p\n", setMemoryParameters); /* No NLS */
			assume0(0); /* should never get here */
			return JNI_ERR;
			break;
		}

		/* Reset Xmdx if necessary */
		if (!opt_XmdxSet && (extensions->maxSizeDefaultMemorySpace < extensions->oldSpaceSize)) {
			extensions->maxSizeDefaultMemorySpace = extensions->oldSpaceSize;
		}

		/* Reset Xmox if not user provided */
		if (!opt_XmoxSet) {
			extensions->maxOldSpaceSize = extensions->maxSizeDefaultMemorySpace;
		}

		/* Reset Xms if not user provided */
		if (!opt_XmsSet) {
			extensions->initialMemorySize = extensions->oldSpaceSize;
		}

		/* Reset minimum values if necessary */
		if (extensions->minOldSpaceSize > extensions->oldSpaceSize) {
			extensions->minOldSpaceSize = extensions->oldSpaceSize;
		}
	} else {
		/* Non flat configurations
		 * User specified combinations of Xmos and Xmns
		 */
		setMemoryParameters = NONE;
		if (opt_XmosSet) {
			setMemoryParameters |= XMOS;
		}
		if (opt_XmnsSet) {
			setMemoryParameters |= XMNS;
		}

		/* Calculate Xmns/Xmos where required.
		 * Following the switch statement memory parameters not set by the user
		 * will be reset as required
		 */
		UDATA combinedXmosXmnsSize;
		UDATA candidateXmosValue;
		UDATA candidateXmnsValue;
		UDATA newSpaceSizeMinimum = opt_XmnsSet ? extensions->minNewSpaceSize : 2*extensions->absoluteMinimumNewSubSpaceSize;
		UDATA oldSpaceSizeMinimum = opt_XmosSet ? extensions->minOldSpaceSize : extensions->absoluteMinimumOldSubSpaceSize;

		switch (setMemoryParameters) {
		case XMOS_XMNS:
			/* Previously verified
			 * 1/ Xmos <= Xms|Xmdx|Xmx
			 * 2/ Xmns <= Xms|Xmdx|Xmx
			 * 3/ Xmos <= Xmox, if Xmox specified
			 * 4/ Xmns <= Xmnx, if Xmnx specified
			 *
			 * Ensure the combined value will fit in the Xms value.  It does not matter if Xms was set
			 * by the user for this test.
			 */
			if ((extensions->oldSpaceSize + extensions->newSpaceSize) > maximumXmsValue) {
				memoryOption = displayXmoOrXmos(memoryParameters);
				memoryOption2 =  displayXmnOrXmns(memoryParameters);
				if (maximumXmsValueParameter) {
					subSpaceTooLargeOption = maximumXmsValueParameter;
					goto _subSpaceCombinationTooLarge;
				}
				goto _subSpaceCombinationTooLargeForHeap;
			}

			assume0(extensions->newSpaceSize = extensions->minNewSpaceSize);
			assume0(extensions->oldSpaceSize = extensions->minOldSpaceSize);

			/* Enforce Xms = Xmns + Xmos */
			if (opt_XmsSet && ((extensions->oldSpaceSize + extensions->newSpaceSize) != extensions->initialMemorySize)) {
				memoryOption = displayXmoOrXmos(memoryParameters);
				memoryOption2 =  displayXmnOrXmns(memoryParameters);
				subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
				goto _subSpaceCombinationNotEqual;
			}
			break;

		case XMOS:
			/* Previously verified
			 * 1/ Xmos <= Xms|Xmdx|Xmx
			 * 2/ Xmos <= Xmox, if Xmox specified
			 *
			 * If Xms is set, then Xmns = Xms - Xmos, else use the previously calculated Xmns value
			 * Verify calculated value is not too small or too large.
			 *
			 * The same algorithm applies to the XMNS case
			 */
			assume0(extensions->oldSpaceSize = extensions->minOldSpaceSize);
			if(opt_XmsSet) {
				/* Live with user input */
				candidateXmnsValue = extensions->initialMemorySize - extensions->oldSpaceSize;

				/* Verify not too small */
				if (candidateXmnsValue < newSpaceSizeMinimum) {
					memoryOption = displayXmoOrXmos(memoryParameters);
					subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooLarge;
				}

				/* Ensure Xmns <= Xmnx */
				if (opt_XmnxSet && (candidateXmnsValue > extensions->maxNewSpaceSize)) {
					memoryOption = displayXmnOrXmnx(memoryParameters);
					subSpaceTooSmallOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooSmall;
				}

				/* Enforce Xms = Xmns + Xmos */
				if ((extensions->oldSpaceSize + candidateXmnsValue) != extensions->initialMemorySize) {
					memoryOption = displayXmoOrXmos(memoryParameters);
					subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooLarge;
				}
			} else {
				if (extensions->oldSpaceSize < (6 * extensions->regionSize)) {
					/*
					 * Minimum initial heap size to provide 75/25 split is 8 regions, six for Tenure and two for Nursery.
					 * If Tenure Initial size is smaller then six regions it is not large enough to provide 75/25 proportion
					 * So set initial Nursery size to minimum (two regions) and use reminder for Tenure
					 */
					candidateXmnsValue = extensions->regionSize * 2;
				} else {
					/* Make Xmns 1/4 of the Xms value, i.e. 1/3 of Xmos. Xms will be reset */
					candidateXmnsValue = MM_Math::roundToFloor(extensions->heapAlignment * 2, extensions->oldSpaceSize / 3);
					candidateXmnsValue = MM_Math::roundToFloor(extensions->regionSize * 2, candidateXmnsValue);
				}

				/* Ensure Xmos + Xmns < MaxXms */
				if ((extensions->oldSpaceSize + candidateXmnsValue) > maximumXmsValue) {
					candidateXmnsValue = maximumXmsValue - extensions->oldSpaceSize;
					if (candidateXmnsValue < newSpaceSizeMinimum) {
						memoryOption = displayXmoOrXmos(memoryParameters);
						if (maximumXmsValueParameter) {
							subSpaceTooLargeOption = maximumXmsValueParameter;
							goto _subSpaceTooLarge;
						}
						goto _subSpaceTooLargeForHeap;
					}
				}

				/* Ensure Xmns <= Xmnx */
				if (opt_XmnxSet && (candidateXmnsValue > extensions->maxNewSpaceSize)) {
					candidateXmnsValue = extensions->maxNewSpaceSize;
				}
			}

			/* Assign Xmns */
			extensions->newSpaceSize = candidateXmnsValue;
			extensions->minNewSpaceSize = candidateXmnsValue;
			break;

		case XMNS:
			/* Previously verified
			 * 1/ Xmns <= Xms|Xmdx|Xmx
			 * 2/ Xmns <= Xmnx, if Xmnx specified
			 *
			 * If Xms is set, then Xmos = Xms - Xmns, else use the previously calculated Xmos value
			 * Verify calculated value is not too small or too large.
			 *
			 * The same algorithm applies to the XMOS case
			 */
			assume0(extensions->newSpaceSize = extensions->minNewSpaceSize);
			if(opt_XmsSet) {
				/* Live with user input */
				candidateXmosValue = extensions->initialMemorySize - extensions->newSpaceSize;

				/* Verify not too small */
				if (candidateXmosValue < oldSpaceSizeMinimum) {
					memoryOption = displayXmnOrXmns(memoryParameters);
					subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooLarge;
				}

				/* Ensure Xmos <= Xmox */
				if (opt_XmoxSet && (candidateXmosValue > extensions->maxOldSpaceSize)) {
					memoryOption = displayXmoOrXmox(memoryParameters);
					subSpaceTooSmallOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooSmall;
				}

				/* Enforce Xms = Xmns + Xmos */
				if ((candidateXmosValue + extensions->newSpaceSize) != extensions->initialMemorySize) {
					memoryOption = displayXmnOrXmns(memoryParameters);
					subSpaceTooLargeOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceTooLarge;
				}
			} else {
				/* Keep the default value.  Xms will be reset */
				candidateXmosValue = extensions->oldSpaceSize;

				/* Ensure Xmos + Xmns < MaxXms */
				if ((candidateXmosValue + extensions->newSpaceSize) > maximumXmsValue) {
					candidateXmosValue = maximumXmsValue - extensions->newSpaceSize;
					if (candidateXmosValue < oldSpaceSizeMinimum) {
						memoryOption = displayXmnOrXmns(memoryParameters);
						if (maximumXmsValueParameter) {
							subSpaceTooLargeOption = maximumXmsValueParameter;
							goto _subSpaceTooLarge;
						}
						goto _subSpaceTooLargeForHeap;
					}
				}

				/* Ensure Xmos <= Xmox */
				if (opt_XmoxSet && (candidateXmosValue > extensions->maxOldSpaceSize)) {
					candidateXmosValue = extensions->maxOldSpaceSize;
				}
			}

			/* Assign Xmos */
			extensions->oldSpaceSize = candidateXmosValue;
			extensions->minOldSpaceSize = candidateXmosValue;
			break;

		case NONE:
			/* Know that Xms <= Xmdx <= Xmx
			 * If Xms is set, split the space 75/25 between Xmos and Xmns
			 * If Xms not set, try calculated values
			 * Honour Xmox and Xmnx
			 */
			if (opt_XmsSet) {
				if (extensions->initialMemorySize < (8 * extensions->regionSize)) {
					/*
					 * Minimum initial size to provide 75/25 split is 8 regions:
					 * (minimum Nursery size is 2 regions and it is 25%)
					 * If Initial size is smaller then 8 regions it is not large enough to provide 75/25 proportion
					 * So set initial Nursery size to minimum (two regions)
					 */
					candidateXmnsValue = extensions->regionSize * 2;
				} else {
					/* Split the available space 75/25 */
					candidateXmnsValue = MM_Math::roundToFloor(extensions->heapAlignment * 2, extensions->initialMemorySize * 1 / 4);
					candidateXmnsValue = MM_Math::roundToFloor(extensions->regionSize * 2, candidateXmnsValue);
				}
				/* Reminder goes to Tenure */
				candidateXmosValue = extensions->initialMemorySize - candidateXmnsValue;
			} else {
				candidateXmnsValue = extensions->newSpaceSize;
				candidateXmosValue = extensions->oldSpaceSize;
			}

			/* Enforce Xmox */
			if (opt_XmoxSet && (candidateXmosValue > extensions->maxOldSpaceSize)) {
				candidateXmosValue = extensions->maxOldSpaceSize;
				if (opt_XmsSet) {
					candidateXmnsValue = extensions->initialMemorySize - candidateXmosValue;
				}
			}

			/* Enforce Xmnx */
			if (opt_XmnxSet && (candidateXmnsValue > extensions->maxNewSpaceSize)) {
				candidateXmnsValue = extensions->maxNewSpaceSize;

				/* Since Xmnx just restricted Xmns, the Xmos value must be adjusted.
				 * It is possible that Xmos will be adjusted above Xmox.  This would mean that Xms > Xmox + Xmnx.
				 * Only adjust Xmos if Xmox is not set, or Xmos will be less than Xmox.
				 * Otherwise report the error in the check below.
				 *
				 * This check is also order dependant with the "Enforce Xmox" if block above.
				 */
				if ((opt_XmsSet)
				&&  (!opt_XmoxSet
					|| ((extensions->initialMemorySize - candidateXmnsValue) <= extensions->maxOldSpaceSize)))
				{
					/* If Xmox is not set it will be the maximum size of the heap.  This means that the new Xmos value
					 * could not possibly exceed the Xmox value, and there is no need to adjust the Xmox value.
					 */
					candidateXmosValue = extensions->initialMemorySize - candidateXmnsValue;
				}
			}

			/* Xmns and Xmos are now set, honouring Xmnx and Xmox.
			 * Enforce Xms = Xmos + Xmns
			 */
			if (opt_XmsSet) {
				if ((candidateXmosValue + candidateXmnsValue) != extensions->initialMemorySize) {
					/* The user specified values for Xmox/Xmnx and Xms prevent us from
					 * enforcing Xmos + Xmns = Xms
					 */
					memoryOption = "-Xmox";
					memoryOption2 = "-Xmnx";
					subSpaceTooSmallOption = displayXmsOrInitialRAMPercentage(memoryParameters);
					goto _subSpaceCombinationTooSmall;
				}
			} else {
				/* Know maximumXmsValue is at least minimum configuration size. Give maximum
				 * space to Xmos
				 */
				if ((candidateXmosValue + candidateXmnsValue) > maximumXmsValue) {
					candidateXmnsValue = newSpaceSizeMinimum;
					candidateXmosValue = maximumXmsValue - candidateXmnsValue;

					/* Verify not too large */
					if (opt_XmoxSet && (candidateXmosValue > extensions->maxOldSpaceSize)) {
						UDATA delta = extensions->maxOldSpaceSize - candidateXmosValue;
						candidateXmosValue -= delta;
						candidateXmnsValue += delta;
					}

					if (opt_XmnxSet && (candidateXmnsValue > extensions->maxNewSpaceSize)) {
						/* The user specified values for Xmox/Xmnx prevent us from succeeding */
						memoryOption = "-Xmox";
						memoryOption2 = "-Xmnx";
						if (maximumXmsValueParameter) {
							subSpaceTooLargeOption = maximumXmsValueParameter;
							goto _subSpaceCombinationTooLarge;
						}
						goto _subSpaceCombinationTooLargeForHeap;
					}
				}
			}

			/* Assign Xmos and Xmns values */
			extensions->oldSpaceSize = candidateXmosValue;
			extensions->minOldSpaceSize = candidateXmosValue;
			extensions->newSpaceSize = candidateXmnsValue;
			extensions->minNewSpaceSize = candidateXmnsValue;
			break;

		default:
			j9tty_printf(PORTLIB, "Internal GC error %p\n", setMemoryParameters); /* No NLS */
			assume0(0); /* should never get here */
			return JNI_ERR;
			break;
		}

		/* The Xmos + Xmns value */
		combinedXmosXmnsSize =  extensions->oldSpaceSize + extensions->newSpaceSize;

		/* Reset Xmdx if applicable */
		if (extensions->maxSizeDefaultMemorySpace < combinedXmosXmnsSize) {
			if (!opt_XmdxSet) {
				extensions->maxSizeDefaultMemorySpace = combinedXmosXmnsSize;
			} else {
				assume0(0); /* Previous stage checked Xmdx > minConfiguration */
				memoryOption = "-Xmdx";
				minimumSizeValue = extensions->absoluteMinimumOldSubSpaceSize + (2*extensions->absoluteMinimumNewSubSpaceSize); /* smallest configuration */
				goto _subSpaceTooSmallForValue;
			}
		}

		/* Reset Xms if applicable */
		if (extensions->initialMemorySize != combinedXmosXmnsSize) {
			if (!opt_XmsSet) {
				extensions->initialMemorySize = combinedXmosXmnsSize;
			} else {
				assume0(0); /* Previous stage checked Xms > minConfiguration */
				memoryOption = displayXmsOrInitialRAMPercentage(memoryParameters);
				minimumSizeValue = extensions->absoluteMinimumOldSubSpaceSize + (2*extensions->absoluteMinimumNewSubSpaceSize); /* smallest configuration */
				goto _subSpaceTooSmallForValue;
			}
		}

		/* Reset Xmox if applicable */
		if (!opt_XmoxSet) {
			/* We know initial Nursery size now so adjust maximum Tenure size */
			extensions->maxOldSpaceSize = extensions->memoryMax - extensions->newSpaceSize;
			if (extensions->oldSpaceSize > extensions->maxOldSpaceSize) {
				extensions->maxOldSpaceSize = extensions->oldSpaceSize;
			}
		}

		/* Reset Xmnx if applicable */
		if (extensions->newSpaceSize > extensions->maxNewSpaceSize) {
			if (!opt_XmnxSet) {
				extensions->maxNewSpaceSize = extensions->newSpaceSize;
			} else {
				/* Should have already been verified */
				assume0(0);
			}
		}

		/* Reset minimum values if necessary */
		if (extensions->minOldSpaceSize > extensions->oldSpaceSize) {
			extensions->minOldSpaceSize = extensions->oldSpaceSize;
		}

		if (extensions->minNewSpaceSize > extensions->newSpaceSize) {
			extensions->minNewSpaceSize = extensions->newSpaceSize;
		}

		/* Verify Xmox + Xmnx combination */
		if (opt_XmoxSet && opt_XmnxSet) {
			if ((extensions->maxOldSpaceSize + extensions->maxNewSpaceSize) > extensions->maxSizeDefaultMemorySpace) {
				memoryOption = displayXmoOrXmox(memoryParameters);
				memoryOption2 = displayXmnOrXmnx(memoryParameters);
				if (opt_XmdxSet) {
					subSpaceTooLargeOption = "-Xmdx";
					goto _subSpaceCombinationTooLarge;
				}
				if (opt_XmxSet) {
					subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
					goto _subSpaceCombinationTooLarge;
				}
				goto _subSpaceCombinationTooLargeForHeap;
			}
		}
	} /* of Non flat configurations */

	/* Verify that -Xminf is at least 0.05 less than -Xmaxf */
	if (extensions->heapFreeMinimumRatioMultiplier + 5 > extensions->heapFreeMaximumRatioMultiplier) {
		memoryOption = "-Xminf";
		minValue = (float)extensions->heapFreeMinimumRatioMultiplier / 100;
		memoryOption2 = "-Xmaxf";
		maxValue = (float)extensions->heapFreeMaximumRatioMultiplier / 100;
		memoryOptionMinimumDifference = "0.05";
		goto _subSpaceCombinationTooClose;
	}

#if defined(J9VM_GC_TILTED_NEW_SPACE)
	/* Verify that -Xgc:scvTiltRatioMin is no larger than -Xgc:scvTiltRatioMax */
	/* NOTE: (stored min) == (100 - specified max) */
	if (extensions->survivorSpaceMinimumSizeRatio > extensions->survivorSpaceMaximumSizeRatio) {
		memoryOption = "scvTiltRatioMin=";
		memoryOption2 = "scvTiltRatioMax=";
		goto _combinationLargerThan;
	}
#endif /* J9VM_GC_TILTED_NEW_SPACE */

#if defined(J9VM_GC_LARGE_OBJECT_AREA)
	/* Verify that -Xloainitial is not less than -Xloaminimum */
	if (extensions->largeObjectAreaInitialRatio < extensions->largeObjectAreaMinimumRatio) {
		memoryOption = "-Xloainitial";
		subSpaceTooSmallOption = "-Xloaminimum";
		goto _subSpaceTooSmall;
	}

	/* Verify that -Xloainitial is less than -Xloamaximum */
	if (extensions->largeObjectAreaInitialRatio > extensions->largeObjectAreaMaximumRatio) {
		memoryOption = "-Xloamaximum";
		subSpaceTooSmallOption = "-Xloainitial";
		goto _subSpaceTooSmall;
	}
#endif /* J9VM_GC_LARGE_OBJECT_AREA */

#if defined(J9VM_GC_GENERATIONAL)
	/* if the user asked for split heaps, apply our additional constraints here */
	if (extensions->enableSplitHeap) {
		if (opt_XmoxSet) {
			if (!opt_XmnxSet) {
				/* strong-arm the Xmnx to fit against this Xmox */
				extensions->maxNewSpaceSize = extensions->memoryMax - extensions->maxOldSpaceSize;
			}
		} else {
			if (opt_XmnxSet) {
				/* strong-arm the Xmox to fit against this Xmnx */
				extensions->maxOldSpaceSize = extensions->memoryMax - extensions->maxNewSpaceSize;
			} else {
				/* neither were set so just correct the old space size since it will be set too big for us to use correctly */
				extensions->maxOldSpaceSize = extensions->memoryMax - extensions->maxNewSpaceSize;
			}
		}
		/* Force the immediate expansion to maximum memory */
		extensions->initialMemorySize = extensions->memoryMax;
		/* Force all the new space sizes to be "locked-in" */
		extensions->minNewSpaceSize = extensions->maxNewSpaceSize;
		extensions->newSpaceSize = extensions->maxNewSpaceSize;
		/* Force all the old space sizes to be "locked-in" */
		extensions->minOldSpaceSize = extensions->maxOldSpaceSize;
		extensions->oldSpaceSize = extensions->maxOldSpaceSize;

		UDATA total = extensions->maxNewSpaceSize + extensions->maxOldSpaceSize;
		if (extensions->memoryMax > total) {
			goto _subSpaceCombinationTooSmall;
		} else if (extensions->memoryMax < total) {
			goto _subSpaceCombinationTooLarge;
		}
	}
#endif /* defined(J9VM_GC_GENERATIONAL) */

#if defined (J9VM_GC_VLHGC)
	{
		/* calculate our eden size boundaries based on -Xmn inputs */
		UDATA mx = extensions->memoryMax;
		UDATA ms = extensions->initialMemorySize;
		/* first, the error checking */
		if (!isLessThanEqualOrUnspecifiedAgainstFixed(&extensions->userSpecifiedParameters._Xmn, mx)) {
			memoryOption = "-Xmn";
			subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
			goto _subSpaceTooLarge;
		}
		if (!isLessThanEqualOrUnspecifiedAgainstFixed(&extensions->userSpecifiedParameters._Xmns, mx)) {
			memoryOption = "-Xmns";
			subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
			goto _subSpaceTooLarge;
		}
		if (!isLessThanEqualOrUnspecifiedAgainstFixed(&extensions->userSpecifiedParameters._Xmnx, mx)) {
			memoryOption = "-Xmnx";
			subSpaceTooLargeOption = displayXmxOrMaxRAMPercentage(memoryParameters);
			goto _subSpaceTooLarge;
		}
		if (!isLessThanEqualOrUnspecifiedAgainstOption(&extensions->userSpecifiedParameters._Xmns, &extensions->userSpecifiedParameters._Xmnx)) {
			memoryOption = "-Xmnx";
			subSpaceTooLargeOption = "-Xmns";
			goto _subSpaceTooLarge;
		}
		/* now interpret the values */
		UDATA idealEdenMin = 0;
		UDATA idealEdenMax = 0;
		/* this implementation is meant to follow this design from JAZZ 39694
		----------------------------------------------------------------
		Arguments specified	|	minEden				|	maxEden
		----------------------------------------------------------------
		XmnA				|	OMR_MIN(A,Y)		|	A
		XmnsB				|	B					|	B
		XmnxC				|	OMR_MIN(C,Y)		|	C
		XmnsB XmnxC			|	B					|	C
		(none)				|	OMR_MIN(X/4,Y)		|	X/4
		----------------------------------------------------------------
		 */
		if (extensions->userSpecifiedParameters._Xmn._wasSpecified) {
			/* earlier error checking would have ensured that we didn't specify -Xmns or -Xmnx with -Xmn */
			UDATA mn = extensions->userSpecifiedParameters._Xmn._valueSpecified;
			idealEdenMin = OMR_MIN(mn, ms);
			idealEdenMax = mn;
		} else if (extensions->userSpecifiedParameters._Xmns._wasSpecified) {
			UDATA mns = extensions->userSpecifiedParameters._Xmns._valueSpecified;
			if (extensions->userSpecifiedParameters._Xmnx._wasSpecified) {
				UDATA mnx = extensions->userSpecifiedParameters._Xmnx._valueSpecified;
				idealEdenMin = mns;
				idealEdenMax = mnx;
			} else {
				idealEdenMin = mns;
				idealEdenMax = mns;
			}
		} else if (extensions->userSpecifiedParameters._Xmnx._wasSpecified) {
			UDATA mnx = extensions->userSpecifiedParameters._Xmnx._valueSpecified;
			idealEdenMin = OMR_MIN(mnx, ms);
			idealEdenMax = mnx;
		} else {
			UDATA quarterMax = mx/4;
			idealEdenMin = OMR_MIN(quarterMax, ms);
			idealEdenMax = quarterMax;
		}

		/* eden size has to be aligned with region size */
		idealEdenMin = MM_Math::roundToFloor(extensions->regionSize, idealEdenMin);
		idealEdenMax = MM_Math::roundToFloor(extensions->regionSize, idealEdenMax);

		/* eden size can not be smaller than 2 * region size
		   1, during initialization for first collection, it needs 2 regions
		   2, if eden size is smaller than 2 times of region size, it would cause wrong calculating taxationThreshold for GMP and PGC(the PGC after GMP) */
		UDATA minSizeForMinEden = extensions->regionSize * 2;
		if (minSizeForMinEden > idealEdenMin) {
			idealEdenMin = minSizeForMinEden;
		}

		UDATA numaNodes = extensions->_numaManager.getAffinityLeaderCount() + 1;

		/* minimum 2 regions for each numa node */
		UDATA minSizeForMaxEden = extensions->regionSize * 2 * numaNodes;
		if (minSizeForMaxEden > idealEdenMax) {
			idealEdenMax = minSizeForMaxEden;
		}

		/* since our current implementation of Eden sizing only uses these values for end-points in our sizing interpolation, they don't need to be rounded */
		extensions->tarokIdealEdenMinimumBytes = idealEdenMin;
		extensions->tarokIdealEdenMaximumBytes = idealEdenMax;
	}
#endif /* J9VM_GC_VLHGC */

	if (opt_XmcrsSet) {
		/* Silently handle a size mismatch; don't report an error about undocumented options
		 * if the user has specified the official one. */
		if (extensions->suballocatorCommitSize > extensions->suballocatorInitialSize) {
				extensions->suballocatorCommitSize = extensions->suballocatorInitialSize;
		}
	} else {
		if (extensions->suballocatorCommitSize > extensions->suballocatorInitialSize) {
			memoryOption = "-Xgc:suballocatorCommitSize=";
			memoryOption2 = "-Xgc:suballocatorInitialSize=";
			goto _combinationLargerThan;
		}
	}

	/* verify -Xsoftmx is set between -Xms and -Xmx */
	if (opt_XsoftmxSet) {
		if (extensions->softMx > extensions->memoryMax) {
			memoryOption = "-Xsoftmx";
			goto _subSpaceTooLargeForHeap;
		}

		if (extensions->softMx < extensions->initialMemorySize) {
			memoryOption = "-Xsoftmx";
			minimumSizeValue = extensions->initialMemorySize;
			goto _subSpaceTooSmallForValue;
		}
	}

	return JNI_OK;

_subSpaceCombinationTooLarge:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE, memoryOption, memoryOption2, subSpaceTooLargeOption);
	return JNI_ERR;

_subSpaceCombinationTooLargeForHeap:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_LARGE_FOR_HEAP, memoryOption, memoryOption2);
	return JNI_ERR;

_subSpaceCombinationNotEqual:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_NOT_EQUAL, memoryOption, memoryOption2, subSpaceTooLargeOption);
	return JNI_ERR;

_subSpaceCombinationTooSmall:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_SUM_OF_TOO_SMALL, memoryOption, memoryOption2, subSpaceTooSmallOption);
	return JNI_ERR;

_subSpaceNotEqualError:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_NOT_EQUAL_ERROR, memoryOption, memoryOption2);
	return JNI_ERR;

_subSpaceTooSmallForValue:
	{
		const char *qualifier = NULL;
		qualifiedSize(&minimumSizeValue, &qualifier);
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_SMALL_FOR_VALUE, memoryOption, minimumSizeValue, qualifier);
		return JNI_ERR;
	}

_subSpaceTooSmall:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_SMALL, memoryOption, subSpaceTooSmallOption);
	return JNI_ERR;

_subSpaceTooLarge:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE, memoryOption, subSpaceTooLargeOption);
	return JNI_ERR;

_subSpaceTooLargeForHeap:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_SUBSPACE_TOO_LARGE_FOR_HEAP, memoryOption);
	return JNI_ERR;

_subSpaceCombinationTooClose:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_OPTION_MINIMUM_DIFFERENCE, memoryOption, minValue, memoryOptionMinimumDifference, memoryOption2, maxValue);
	return JNI_ERR;

_combinationLargerThan:
	j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_OPTIONS_MUST_BE_NO_GREATER_THAN, memoryOption, memoryOption2);
	return JNI_ERR;
}

/**
 * Recalculate Xms, Xmn, Xmo values based on user input.
 * @param memoryParameters array of parameter values
 * @param flatConfiguration with or without New memory space
 * @return JNI_OK if OK
 */
jint
gcCalculateMemoryParameters(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	jint result;

	/* Handle any configuration specific details.  For example in the flat configuration
	 * the -Xmns/-Xmnx values have no meaning
	 */
	result = setConfigurationSpecificMemoryParameters(javaVM, memoryParameters, flatConfiguration);
	if (JNI_OK != result) {
		return result;
	}

	/* Verify all memory parameters provided by the user are larger than the minimum size
	 * and smaller than the value of Xmx.  Only verify individual parameters, not combinations
	 */
	result = independentMemoryParameterVerification(javaVM, memoryParameters, flatConfiguration);
	if (JNI_OK != result) {
		return result;
	}

	/* Verify the combinations of memory parameters are valid. For example ensure that
	 * Xms >= Xmns + Xmos for non flat configurations.
	 */
	result = combinationMemoryParameterVerification(javaVM, memoryParameters, flatConfiguration);
	if (JNI_OK != result) {
		return result;
	}

	assume0(extensions->minOldSpaceSize >= OMR_MAX(extensions->absoluteMinimumOldSubSpaceSize, extensions->heapAlignment));
#if defined(DEBUG)
	if (!flatConfiguration) {
		assume0(extensions->minNewSpaceSize >= 2*OMR_MAX(extensions->absoluteMinimumNewSubSpaceSize, extensions->heapAlignment));
	}
#endif /* DEBUG */

	assume0(extensions->oldSpaceSize >= extensions->minOldSpaceSize);
	assume0(extensions->oldSpaceSize <= extensions->maxOldSpaceSize);

#if defined(DEBUG)
	if (!flatConfiguration) {
		assume0(extensions->newSpaceSize >= extensions->minNewSpaceSize);
		assume0(extensions->newSpaceSize <= extensions->maxNewSpaceSize);
	}
#endif /* DEBUG */

	assume0(extensions->initialMemorySize = (extensions->oldSpaceSize + extensions->newSpaceSize));
	assume0(extensions->initialMemorySize <= extensions->maxSizeDefaultMemorySpace);

	assume0(extensions->maxSizeDefaultMemorySpace <= extensions->memoryMax);
	assume0(extensions->maxSizeDefaultMemorySpace >= extensions->maxOldSpaceSize);
	assume0(extensions->maxSizeDefaultMemorySpace >= extensions->maxNewSpaceSize);

	/* initialize the dynamicMaxSoftReferenceAge to the maxSoftReferenceAge since the first GC starts with reference age at its maximum */
	extensions->dynamicMaxSoftReferenceAge = extensions->maxSoftReferenceAge;

	return result;
}

/**
 * Ensure all values are correct.
 * Values input by the user can not be changed, values that have been calculated
 * may be updated if required.
 * @param memoryParameters array of parameter values
 * @param flatConfiguration with or without New memory space
 * @return JNI_OK if OK
 */
jint
gcInitializeVerification(J9JavaVM *javaVM, IDATA* memoryParameters, bool flatConfiguration)
{
#if defined(J9VM_GC_THREAD_LOCAL_HEAP) || defined(J9VM_GC_MODRON_SCAVENGER)
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
#endif /* J9VM_GC_THREAD_LOCAL_HEAP || J9VM_GC_MODRON_SCAVENGER */
	jint result;

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)
	/* Make sure TLH sizes are appropriately rounded */
	extensions->tlhMinimumSize = MM_Math::roundToSizeofUDATA(extensions->tlhMinimumSize);
	extensions->tlhIncrementSize = MM_Math::roundToSizeofUDATA(extensions->tlhIncrementSize);
	extensions->tlhInitialSize = MM_Math::roundToCeiling(extensions->tlhIncrementSize, extensions->tlhInitialSize);
	extensions->tlhMaximumSize = MM_Math::roundToCeiling(extensions->tlhIncrementSize, extensions->tlhMaximumSize);
	extensions->tlhSurvivorDiscardThreshold = MM_Math::roundToSizeofUDATA(extensions->tlhSurvivorDiscardThreshold);
	extensions->tlhTenureDiscardThreshold = MM_Math::roundToSizeofUDATA(extensions->tlhTenureDiscardThreshold);
#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

#if defined(J9VM_GC_MODRON_SCAVENGER)
	if (extensions->scavengerScanCacheMaximumSize < extensions->scavengerScanCacheMinimumSize) {
		PORT_ACCESS_FROM_JAVAVM(javaVM);
		j9nls_printf(PORTLIB,J9NLS_ERROR,J9NLS_GC_OPTIONS_MUST_BE_NO_GREATER_THAN, "-XXgc:scanCacheMinimumSize", "-XXgc:scanCacheMaximumSize");
		return JNI_ERR;
	}
	/* make sure scavengerScanCacheMinimumSize and scavengerScanCacheMaximumSize are properly aligned */
	extensions->scavengerScanCacheMaximumSize = MM_Math::roundToCeiling(extensions->tlhMinimumSize, extensions->scavengerScanCacheMaximumSize);
	extensions->scavengerScanCacheMinimumSize = MM_Math::roundToCeiling(extensions->tlhMinimumSize, extensions->scavengerScanCacheMinimumSize);
#endif /* J9VM_GC_MODRON_SCAVENGER */

	/* Recalculate memory parameters based on user input (Xms, Xmo, Xmn) */
	result = gcCalculateMemoryParameters(javaVM, memoryParameters, flatConfiguration);
	if (JNI_OK != result) {
		return result;
	}

	return JNI_OK;
}

/**
 * If appropriate, lower the Xmx value before re-attempting to allocate basic heap structures.
 * Called after we have failed to allocate basic heap structures, this function
 * may resize the Xmx value so we can try again with a smaller Xmx value.
 *
 * @param memoryParameterTable the table containing user-specified memory parameters
 * @param memoryMinimum the minimum allowable Xmx value
 *
 * @note This function assumes that all memory parameters have been verified, and
 * will be re-verified based on the new Xmx value
 *
 * @return true - if the Xmx value was resized, and we should try again
 * @return false - otherwise
 */
bool
reduceXmxValueForHeapInitialization(J9JavaVM *javaVM, IDATA *memoryParameterTable, UDATA memoryMinimum)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	bool optXmxSet = (-1 != memoryParameterTable[opt_Xmx]);
	bool optXmdxSet = (-1 != memoryParameterTable[opt_Xmdx]);

	if (optXmxSet) {
		/* -Xmx was set by the user, so just fail */
		return false;
	} else if (extensions->memoryMax <= memoryMinimum) {
		/* We can't reduce the Xmx value any more -- fail */
		return false;
	}

	extensions->memoryMax = (extensions->memoryMax / DEFAULT_XMX_REDUCTION_DENOMINATOR) * DEFAULT_XMX_REDUCTION_NUMERATOR;
	extensions->memoryMax = MM_Math::roundToFloor(extensions->heapAlignment, extensions->memoryMax);
	extensions->memoryMax = MM_Math::roundToFloor(extensions->regionSize, extensions->memoryMax);

	/* Don't shrink below the minimum value */
	if (extensions->memoryMax < memoryMinimum) {
		extensions->memoryMax = memoryMinimum;
	}

	/* If Xmdx is now > Xmx, and the Xmdx value was not set by the user, change Xmdx also */
	if ((!optXmdxSet) && (extensions->memoryMax < extensions->maxSizeDefaultMemorySpace)) {
		extensions->maxSizeDefaultMemorySpace = extensions->memoryMax;
	}

	return true;
}

/**
 * If the requested page size does not match the actual page size, and the
 * user specified -Xlp:objectheap:warn, output a warning message.
 *
 * @param extensions the instance of GCExtensions
 * @param loadInfo The loadInfo object to store the error message
 */
void
warnIfPageSizeNotSatisfied(J9JavaVM* vm, MM_GCExtensions *extensions) {
	PORT_ACCESS_FROM_JAVAVM(vm);

	if(extensions == NULL || extensions->heap == NULL) {
		return;
	}

	if((extensions->heap->getPageSize() != extensions->requestedPageSize) && extensions->largePageWarnOnError) {
		/* Obtain the qualified sizes (e.g. 2k) */
		UDATA requestedSize = extensions->requestedPageSize;
		const char* requestedSizeQualifier = NULL;
		qualifiedSize(&requestedSize, &requestedSizeQualifier);

		UDATA actualSize = extensions->heap->getPageSize();
		const char* actualSizeQualifier = NULL;
		qualifiedSize(&actualSize, &actualSizeQualifier);

		j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_XLP_PAGE_NOT_AVAILABLE_WARN, requestedSize, requestedSizeQualifier, actualSize, actualSizeQualifier);
	}
}

/**
 * Set default configuration options for SE
 * @param extensions GCExtensions
 * @param scavenge value to set extensions->scavengerEnabled except it is already forced
 * @param concurrentMark value to set extensions->concurrentMark except it is already forced
 * @param concurrentSweep value to set extensions->concurrentSweep except it is already forced
 * @param largeObjectArea value to set extensions->largeObjectArea except it is already forced
 */
void
setDefaultConfigOptions(MM_GCExtensions *extensions, bool scavenge, bool concurrentMark, bool concurrentSweep, bool largeObjectArea)
{
#if defined(J9VM_GC_MODRON_SCAVENGER)
	if(!extensions->configurationOptions._forceOptionScavenge) {
		extensions->scavengerEnabled = scavenge;
	}
#endif /* defined(J9VM_GC_MODRON_SCAVENGER) */
#if defined (OMR_GC_MODRON_CONCURRENT_MARK)
	if(!extensions->configurationOptions._forceOptionConcurrentMark) {
		extensions->concurrentMark = concurrentMark;
	}
#endif /* defined (OMR_GC_MODRON_CONCURRENT_MARK) */
#if defined(J9VM_GC_CONCURRENT_SWEEP)
	if(!extensions->configurationOptions._forceOptionConcurrentSweep) {
		extensions->concurrentSweep = concurrentSweep;
	}
#endif /* defined(J9VM_GC_CONCURRENT_SWEEP) */
#if defined(J9VM_GC_LARGE_OBJECT_AREA)
	if(!extensions->configurationOptions._forceOptionLargeObjectArea) {
		extensions->largeObjectArea = largeObjectArea;
	}
#endif /* defined(J9VM_GC_LARGE_OBJECT_AREA) */
}

void
setConfigOptionsForNoGc(MM_GCExtensions *extensions)
{
	/* noScavenger noConcurrentMark noConcurrentSweep, noLOA */
#if defined(J9VM_GC_MODRON_SCAVENGER)
	extensions->configurationOptions._forceOptionScavenge  = true;
	extensions->scavengerEnabled = false;
#endif /* defined(J9VM_GC_MODRON_SCAVENGER) */
#if defined (OMR_GC_MODRON_CONCURRENT_MARK)
	extensions->configurationOptions._forceOptionConcurrentMark = true;
	extensions->concurrentMark = false;
#endif /* defined (OMR_GC_MODRON_CONCURRENT_MARK) */
#if defined(J9VM_GC_CONCURRENT_SWEEP)
	extensions->configurationOptions._forceOptionConcurrentSweep = true;
	extensions->concurrentSweep = false;
#endif /* defined(J9VM_GC_CONCURRENT_SWEEP) */
#if defined(J9VM_GC_LARGE_OBJECT_AREA)
	extensions->configurationOptions._forceOptionLargeObjectArea = true;
	extensions->largeObjectArea = false;
#endif /* defined(J9VM_GC_LARGE_OBJECT_AREA) */
	/* 1 gcThread */
	extensions->gcThreadCountForced = true;
	extensions->gcThreadCount = 1;

	extensions->splitFreeListSplitAmount = 1;
	extensions->objectListFragmentCount = 1;

	/* disable excessiveGC */
	extensions->excessiveGCEnabled._wasSpecified = true;
	extensions->excessiveGCEnabled._valueSpecified = false;
	/* disable estimate fragmentation */
	extensions->estimateFragmentation = 0;
	extensions->processLargeAllocateStats = false;
	/* disable system gc */
	extensions->disableExplicitGC = true;
}

/**
 * Create proper configuration for SE based on options
 * @param env pointer to Environment
 * @return pointer to created configuration or NULL if failed
 */
MM_Configuration *
configurateGCWithPolicyAndOptionsStandard(MM_EnvironmentBase *env)
{
	MM_Configuration *result = NULL;
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env->getOmrVM());

#if defined(J9VM_GC_MODRON_SCAVENGER)
	if (extensions->scavengerEnabled) {
#if defined(J9VM_GC_CONCURRENT_SWEEP)
		/* Scavenger does not support concurrent sweep */
		if (!extensions->concurrentSweep) {
#endif /* defined(J9VM_GC_CONCURRENT_SWEEP) */

			/*
			 * Set region size based on section size of Concurrent Scavenger Page
			 * - get an estimation of maximum Nursery size based at -Xmn, -Xmnx, -Xmns or -Xmx / 4
			 * 	(this is fast check without full parameters analysis for Concurrent Scavenger prototype, it is recommended to provide -Xmn explicitly)
			 * - select Concurrent Scavenger Page size based at estimated Nursery size: if should be rounded up to next power of 2 but not smaller then 32M
			 * - select region size as a Concurrent Scavenger Page Section size
			 */
			if (extensions->isConcurrentScavengerHWSupported()) {
				OMRPORT_ACCESS_FROM_ENVIRONMENT(env);
				/* Default maximum Nursery size estimation in case of none of -Xmn* options is specified */
				uintptr_t nurserySize = extensions->memoryMax / 4;

				/*
				 * correctness of -Xmn* values will be analyzed later on with initialization error in case of bad combination
				 * so assume here all of them are correct, just check none of them is larger then entire heap size
				 */
				if (extensions->userSpecifiedParameters._Xmn._wasSpecified) {
					/* maximum Nursery size was set in -Xmn */
					if (extensions->userSpecifiedParameters._Xmn._valueSpecified < extensions->memoryMax) {
						nurserySize = extensions->userSpecifiedParameters._Xmn._valueSpecified;
					}
				} else if (extensions->userSpecifiedParameters._Xmnx._wasSpecified) {
					/* maximum Nursery size was set in -Xmnx */
					if (extensions->userSpecifiedParameters._Xmnx._valueSpecified < extensions->memoryMax) {
						nurserySize = extensions->userSpecifiedParameters._Xmnx._valueSpecified;
					}
				} else if (extensions->userSpecifiedParameters._Xmns._wasSpecified) {
					/* maximum Nursery size was set in -Xmns, it is related in case if it is larger then default maximum Nursery size */
					if (extensions->userSpecifiedParameters._Xmns._valueSpecified < extensions->memoryMax) {
						if (extensions->userSpecifiedParameters._Xmns._valueSpecified > nurserySize) {
							nurserySize = extensions->userSpecifiedParameters._Xmns._valueSpecified;
						}
					}
				}

				/* Calculate Concurrent Scavenger Page parameters based at maximum Nursery size estimation */
				extensions->calculateConcurrentScavengerPageParameters(nurserySize);

				if (extensions->isDebugConcurrentScavengerPageAlignment()) {
					omrtty_printf("Nursery size early projection 0x%zx, Concurrent Scavenger Page size 0x%zx, Section size for heap alignment 0x%zx\n",
							nurserySize, extensions->getConcurrentScavengerPageSectionSize() * CONCURRENT_SCAVENGER_PAGE_SECTIONS, extensions->getConcurrentScavengerPageSectionSize());
				}
			}

			result = MM_ConfigurationGenerational::newInstance(env);

#if defined(J9VM_GC_CONCURRENT_SWEEP)
		}
#endif /* defined(J9VM_GC_CONCURRENT_SWEEP) */
	} else
#endif /* defined(J9VM_GC_MODRON_SCAVENGER) */
	{
		result = MM_ConfigurationFlat::newInstance(env);
	}

	return result;
}

/**
 * Create configuration based on GC policy
 * @param vm pointer to JavaVM
 * @return pointer to created configuration or NULL if failed
 */
MM_Configuration *
configurateGCWithPolicyAndOptions(OMR_VM* omrVM)
{
	MM_Configuration *result = NULL;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(omrVM);
	MM_EnvironmentBase env(omrVM);

	switch(extensions->configurationOptions._gcPolicy) {
	case gc_policy_optthruput:
		extensions->gcModeString = "-Xgcpolicy:optthruput";
		omrVM->gcPolicy = J9_GC_POLICY_OPTTHRUPUT;
		/* noScavenge, noConcurrentMark, noConcurrentSweep, loa */
		setDefaultConfigOptions(extensions, false, false, false, true);
		result = configurateGCWithPolicyAndOptionsStandard(&env);
		break;

	case gc_policy_optavgpause:
		extensions->gcModeString = "-Xgcpolicy:optavgpause";
		omrVM->gcPolicy = J9_GC_POLICY_OPTAVGPAUSE;
		/* noScavenge, concurrentMark, concurrentSweep, loa */
		setDefaultConfigOptions(extensions, false, true, true, true);
		result = configurateGCWithPolicyAndOptionsStandard(&env);
		break;

	case gc_policy_gencon:
		extensions->gcModeString = "-Xgcpolicy:gencon";
		omrVM->gcPolicy = J9_GC_POLICY_GENCON;
		/* scavenge, concurrentMark, noConcurrentSweep, loa */
		setDefaultConfigOptions(extensions, true, true, false, true);
		result = configurateGCWithPolicyAndOptionsStandard(&env);
		break;

	case gc_policy_metronome:
		extensions->gcModeString = "-Xgcpolicy:metronome";
		omrVM->gcPolicy = J9_GC_POLICY_METRONOME;
		result = MM_ConfigurationRealtime::newInstance(&env);
		break;

	case gc_policy_balanced:
		extensions->gcModeString = "-Xgcpolicy:balanced";
		omrVM->gcPolicy = J9_GC_POLICY_BALANCED;
		result = MM_ConfigurationIncrementalGenerational::newInstance(&env);
		break;

	case gc_policy_nogc:
		extensions->gcModeString = "-Xgcpolicy:nogc";
		omrVM->gcPolicy = J9_GC_POLICY_NOGC;
		/* noScavenge, noConcurrentMark, noConcurrentSweep, noLOA */
		setConfigOptionsForNoGc(extensions);
		result = configurateGCWithPolicyAndOptionsStandard(&env);
		break;

	case gc_policy_undefined:
	default:
		/* Undefined or unknown GC policy */
		Assert_MM_unreachable();
		break;
	}

	return result;
}

/**
 * Initialize GC parameters.
 * Initialize GC parameters with default values, parse the command line, massage values
 * as required and finally verify values.
 * @return J9VMDLLMAIN_OK or J9VMDLLMAIN_FAILED
 */
jint
gcInitializeDefaults(J9JavaVM* vm)
{
	J9VMDllLoadInfo *loadInfo = getGCDllLoadInfo(vm);
	UDATA tableSize = (opt_none + 1) * sizeof(IDATA);
	UDATA realtimeSizeClassesAllocationSize = ROUND_TO(sizeof(UDATA), sizeof(J9VMGCSizeClasses));
	IDATA *memoryParameterTable;
	UDATA minimumVMSize;
	bool flatConfiguration = true;
	MM_GCExtensions *extensions;
	MM_EnvironmentBase env(vm->omrVM);
	PORT_ACCESS_FROM_JAVAVM(vm);

	minimumVMSize = MINIMUM_VM_SIZE;

	memoryParameterTable = (IDATA *)j9mem_allocate_memory(tableSize, OMRMEM_CATEGORY_MM);
	if (!memoryParameterTable) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_OUT_OF_MEMORY, "Failed to initialize, out of memory.");
		goto error;
	}
	memset(memoryParameterTable, -1, tableSize);

	vm->memoryManagerFunctions = ((J9MemoryManagerFunctions *)GLOBAL_TABLE(MemoryManagerFunctions));

	// todo: dagar centralize language init
	//gcOmrInitializeDefaults(vm->omrVM);
	extensions = MM_GCExtensions::newInstance(&env);
	if (NULL == extensions) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_OUT_OF_MEMORY, "Failed to initialize, out of memory.");
		goto error;
	}
	extensions->setOmrVM(vm->omrVM);
	vm->omrVM->_gcOmrVMExtensions = (void *)extensions;
	vm->gcExtensions = vm->omrVM->_gcOmrVMExtensions;

	/* enable estimateFragmentation for all GCs as default for java, but not the estimated result would not affect concurrentgc kickoff by default */
	extensions->estimateFragmentation = (GLOBALGC_ESTIMATE_FRAGMENTATION | LOCALGC_ESTIMATE_FRAGMENTATION);
	extensions->processLargeAllocateStats = true;
	extensions->concurrentSlackFragmentationAdjustmentWeight = 0;

	/* allocate and set the collector language interface to Java */
	extensions->collectorLanguageInterface = MM_CollectorLanguageInterfaceImpl::newInstance(&env);
	if (NULL == extensions->collectorLanguageInterface) {
		goto error;
	}

	initializeVerboseFunctionTableWithDummies(&extensions->verboseFunctionTable);

	if (JNI_OK != gcParseCommandLineAndInitializeWithValues(vm, memoryParameterTable)) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_PARSING_COMMAND_LINE, "Failed to initialize, parsing command line.");
		goto error;
	}

	if ((-1 == memoryParameterTable[opt_Xms]) && (-1 != memoryParameterTable[opt_initialRAMPercent])) {
		extensions->initialMemorySize = (uintptr_t)(((double)extensions->usablePhysicalMemory / 100.0) * extensions->initialRAMPercent);
		/* Update memory parameter table to appear that -Xms was specified */
		memoryParameterTable[opt_Xms] = memoryParameterTable[opt_initialRAMPercent];
	}
	if ((-1 == memoryParameterTable[opt_Xmx]) && (-1 != memoryParameterTable[opt_maxRAMPercent])) {
		extensions->memoryMax = (uintptr_t)(((double)extensions->usablePhysicalMemory / 100.0) * extensions->maxRAMPercent);
		/* Update memory parameter table to appear that -Xmx was specified */
		memoryParameterTable[opt_Xmx] = memoryParameterTable[opt_maxRAMPercent];
	}

	if (gc_policy_metronome == extensions->configurationOptions._gcPolicy) {
		/* Heap is segregated; take into account segregatedAllocationCache. */
		vm->segregatedAllocationCacheSize = (J9VMGC_SIZECLASSES_NUM_SMALL + 1)*sizeof(J9VMGCSegregatedAllocationCacheEntry);

		vm->realtimeSizeClasses = (J9VMGCSizeClasses *)j9mem_allocate_memory(realtimeSizeClassesAllocationSize, OMRMEM_CATEGORY_VM);
		if (NULL == vm->realtimeSizeClasses) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_OUT_OF_MEMORY, "Failed to initialize, out of memory.");
			goto error;
		}
	}
	vm->vmThreadSize = J9_VMTHREAD_SEGREGATED_ALLOCATION_CACHE_OFFSET + vm->segregatedAllocationCacheSize + sizeof(OMR_VMThread);

#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	if (gc_policy_gencon == extensions->configurationOptions._gcPolicy) {
		/* after we parsed cmd line options, check if we can obey the request to run CS (valid for Gencon only) */
		if (extensions->concurrentScavengerForced) {
#if defined(J9VM_ARCH_X86) || defined(J9VM_ARCH_POWER) || defined(J9VM_ARCH_AARCH64)
			/*
			 * x86, POWER and AArch64 do not respect -XXgc:softwareRangeCheckReadBarrier and have it set to true always.
			 *
			 * Z is the only consumer that actually uses -XXgc:softwareRangeCheckReadBarrier
			 * to overwrite HW concurrent scavenge.
			 */
			extensions->softwareRangeCheckReadBarrier = true;
#endif /* J9VM_ARCH_X86 || J9VM_ARCH_POWER || J9VM_ARCH_AARCH64 */
			if (LOADED == (FIND_DLL_TABLE_ENTRY(J9_JIT_DLL_NAME)->loadFlags & LOADED)) {
				/* If running jitted, it must be on supported h/w */
				J9ProcessorDesc  processorDesc;
				j9sysinfo_get_processor_description(&processorDesc);
				bool hwSupported = j9sysinfo_processor_has_feature(&processorDesc, J9PORT_S390_FEATURE_GUARDED_STORAGE) &&
						j9sysinfo_processor_has_feature(&processorDesc, J9PORT_S390_FEATURE_SIDE_EFFECT_ACCESS);

				if (hwSupported) {
					/* Software Barrier request overwrites HW usage on supported HW */
					extensions->concurrentScavengerHWSupport = hwSupported 
						&& !extensions->softwareRangeCheckReadBarrier 
						&& !J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_PORTABLE_SHARED_CACHE)
						&& !J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_ENABLE_CRIU_SUPPORT);
					extensions->concurrentScavenger = hwSupported || extensions->softwareRangeCheckReadBarrier;
				} else {
					extensions->concurrentScavengerHWSupport = false;
#if defined(J9VM_ARCH_X86) || defined(J9VM_ARCH_POWER) || defined(J9VM_ARCH_AARCH64) || defined(J9VM_ARCH_S390)
					extensions->concurrentScavenger = true;
#endif /*J9VM_ARCH_X86 || J9VM_ARCH_POWER || J9VM_ARCH_AARCH64 || J9VM_ARCH_S390 */
				}
			} else {
				/* running interpreted is ok on any h/w */
				extensions->concurrentScavenger = true;
			}
		}
	}
#endif /* OMR_GC_CONCURRENT_SCAVENGER */

	extensions->configuration = configurateGCWithPolicyAndOptions(vm->omrVM);

	/* omrVM->gcPolicy is set by configurateGCWithPolicyAndOptions */
	((J9JavaVM*)env.getLanguageVM())->gcPolicy = vm->omrVM->gcPolicy;

	if (NULL == extensions->configuration) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE, "Failed to initialize.");
		goto error;
	}

	extensions->trackMutatorThreadCategory = J9_ARE_NO_BITS_SET(vm->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_REDUCE_CPU_MONITOR_OVERHEAD);


	if (!gcParseTGCCommandLine(vm)) {
		loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE_PARSING_COMMAND_LINE, "Failed to initialize, parsing command line.");
		goto error;
	}

#if defined(J9VM_GC_MODRON_SCAVENGER)
	if (extensions->scavengerEnabled) {
		flatConfiguration = false;
	}
#endif /* J9VM_GC_MODRON_SCAVENGER */

	while (true) {
		/* Verify Xmx and Xmdx before using the Xmdx value to calculate further values */
		if (JNI_OK != gcInitializeXmxXmdxVerification(vm, memoryParameterTable, flatConfiguration, minimumVMSize, NULL, NULL)) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE, "Failed to initialize.");
			goto error;
		}

		/* Calculate memory parameters based on Xmx/Xmdx */
		if (JNI_OK != gcInitializeCalculatedValues(vm, memoryParameterTable)) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE, "Failed to initialize.");
			goto error;
		}

		/* Verify all memory parameters */
		if (JNI_OK != gcInitializeVerification(vm, memoryParameterTable, flatConfiguration)) {
			loadInfo->fatalErrorStr = (char *)j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_GC_FAILED_TO_INITIALIZE, "Failed to initialize.");
			goto error;
		}

		/* Try to initialize basic heap structures with the memory parameters we currently have */
		if (JNI_OK == j9gc_initialize_heap(vm, memoryParameterTable, extensions->memoryMax)) {
			break;
		}

		if(extensions->largePageFailedToSatisfy) {
			/* We were unable to satisfy the user's request for a strict page size. */
			goto error;
		}

		if (!reduceXmxValueForHeapInitialization(vm, memoryParameterTable, minimumVMSize)) {
			/* Unable to reduce the Xmx value -- fail */
			/* Error string is set by j9gc_initialize_heap */
			goto error;
		}

		/* We are going to try again -- free any buffer we already have from j9gc_initialize_heap */
		if  ((loadInfo->loadFlags & FREE_ERROR_STRING) && (NULL != loadInfo->fatalErrorStr)) {
			j9mem_free_memory(loadInfo->fatalErrorStr);
			loadInfo->loadFlags &= ~FREE_ERROR_STRING;
		}
		loadInfo->fatalErrorStr = NULL;
	}

	/* initialize largeObjectAllocationProfilingVeryLargeObjectThreshold, largeObjectAllocationProfilingVeryLargeObjectSizeClass and freeMemoryProfileMaxSizeClasses for non segregated memoryPool case */
	if (gc_policy_metronome != extensions->configurationOptions._gcPolicy) {
		MM_LargeObjectAllocateStats::initializeFreeMemoryProfileMaxSizeClasses(&env, extensions->largeObjectAllocationProfilingVeryLargeObjectThreshold,
				(float)extensions->largeObjectAllocationProfilingSizeClassRatio / (float)100.0, extensions->heap->getMaximumMemorySize());
	}

	warnIfPageSizeNotSatisfied(vm,extensions);
	j9mem_free_memory(memoryParameterTable);
	return J9VMDLLMAIN_OK;

error:
	if (memoryParameterTable) {
		j9mem_free_memory(memoryParameterTable);
	}
	return J9VMDLLMAIN_FAILED;
}

#if defined(OMR_GC_CONCURRENT_SCAVENGER)
static void
hookAcquireVMAccess(J9HookInterface** hook, UDATA eventNum, void* voidEventData, void* userData)
{
	J9VMAcquireVMAccessEvent* eventData = (J9VMAcquireVMAccessEvent*)voidEventData;

	J9VMThread *currentThread = eventData->currentThread;
	MM_EnvironmentStandard *env = MM_EnvironmentStandard::getEnvironment(currentThread->omrVMThread);
	MM_GCExtensions* ext = MM_GCExtensions::getExtensions(currentThread);

	Assert_MM_true(ext->concurrentScavenger);

	ext->scavenger->switchConcurrentForThread(env);
}

static void
hookReleaseVMAccess(J9HookInterface** hook, UDATA eventNum, void* voidEventData, void* userData)
{
	J9VMReleaseVMAccessEvent* eventData = (J9VMReleaseVMAccessEvent*)voidEventData;

	J9VMThread *currentThread = eventData->currentThread;
	MM_EnvironmentStandard *env = MM_EnvironmentStandard::getEnvironment(currentThread->omrVMThread);
	MM_GCExtensions* ext = MM_GCExtensions::getExtensions(currentThread);

	if (ext->isConcurrentScavengerInProgress()) {
		/* Flush and final flags are false, which means that we will not release the copy caches to scan queue, but just make them inactive,
		 * for someone else to release them if needed. More often, however, this thread will re-acquire VM access and re-active the caches
		 */
		ext->scavenger->threadReleaseCaches(env, env, false, false);
	}
}

static void
hookAcquiringExclusiveInNative(J9HookInterface** hook, UDATA eventNum, void* voidEventData, void* userData)
{
	J9VMAcquringExclusiveInNativeEvent* eventData = (J9VMAcquringExclusiveInNativeEvent*)voidEventData;

	J9VMThread *targetThread = eventData->targetThread;
	MM_EnvironmentStandard *env = MM_EnvironmentStandard::getEnvironment(targetThread->omrVMThread);
	MM_GCExtensions* ext = MM_GCExtensions::getExtensions(targetThread);

	if (ext->isConcurrentScavengerInProgress()) {
		/* This call back occurs when Exclusive is being acquired. There are no reason for delaying final flush - do it now (Hence, the flags are true). */
		ext->scavenger->threadReleaseCaches(NULL, env, true, true);

	}
}

#endif /* OMR_GC_CONCURRENT_SCAVENGER */

/**
 * Report an event indicating that the GC is initialized
 */
jint
triggerGCInitialized(J9VMThread* vmThread)
{
	J9JavaVM* vm = vmThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);

	UDATA beatMicro = 0;
	UDATA timeWindowMicro = 0;
	UDATA targetUtilizationPercentage = 0;
	UDATA gcInitialTrigger = 0;
	UDATA headRoom = 0;
#if defined(J9VM_GC_REALTIME)
	beatMicro = extensions->beatMicro;
	timeWindowMicro = extensions->timeWindowMicro;
	targetUtilizationPercentage = extensions->targetUtilizationPercentage;
	gcInitialTrigger = extensions->gcInitialTrigger;
	headRoom = extensions->headRoom;
#endif /* J9VM_GC_REALTIME */

	UDATA numaNodes = extensions->_numaManager.getAffinityLeaderCount();

	UDATA regionSize = extensions->getHeap()->getHeapRegionManager()->getRegionSize();
	UDATA regionCount = extensions->getHeap()->getHeapRegionManager()->getTableRegionCount();

	UDATA arrayletLeafSize = 0;
	arrayletLeafSize = vm->arrayletLeafSize;

	TRIGGER_J9HOOK_MM_OMR_INITIALIZED(
		extensions->omrHookInterface,
		vmThread->omrVMThread,
		j9time_hires_clock(),
		j9gc_get_gcmodestring(vm),
		0, /* unused */
		j9gc_get_maximum_heap_size(vm),
		j9gc_get_initial_heap_size(vm),
		j9sysinfo_get_physical_memory(),
		j9sysinfo_get_number_CPUs_by_type(J9PORT_CPU_ONLINE),
		extensions->gcThreadCount,
		j9sysinfo_get_CPU_architecture(),
		j9sysinfo_get_OS_type(),
		j9sysinfo_get_OS_version(),
		extensions->accessBarrier->compressedPointersShift(),
		beatMicro,
		timeWindowMicro,
		targetUtilizationPercentage,
		gcInitialTrigger,
		headRoom,
		extensions->heap->getPageSize(),
		getPageTypeString(extensions->heap->getPageFlags()),
		extensions->requestedPageSize,
		getPageTypeString(extensions->requestedPageFlags),
		numaNodes,
		regionSize,
		regionCount,
		arrayletLeafSize);

	return J9VMDLLMAIN_OK;
}

static void
hookValidatorVMThreadCrash(J9HookInterface * * hookInterface, UDATA eventNum, void * eventData, void * userData)
{
	J9VMThread *vmThread = ((J9VMThreadCrashEvent *)eventData)->currentThread;
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
	if (NULL != env) {
		MM_Validator *activeValidator = env->_activeValidator;
		if (NULL != activeValidator) {
			env->_activeValidator = NULL; /* make absolutely sure we don't go recursive */
			activeValidator->threadCrash(env);
		}
	}
}

static void
hookVMRegistrationEvent(J9HookInterface** hook, UDATA eventNum, void* voidEventData, void* userData)
{
	J9HookRegistrationEvent* eventData = (J9HookRegistrationEvent*)voidEventData;

	switch (eventData->eventNum) {
		case J9HOOK_VM_OBJECT_ALLOCATE_WITHIN_THRESHOLD:
		case J9HOOK_VM_OBJECT_ALLOCATE_INSTRUMENTABLE: {
			J9JavaVM* vm = (J9JavaVM*)userData;
			J9VMThread * currentThread = vm->internalVMFunctions->currentVMThread(vm);
			if (currentThread != NULL) {
				j9gc_allocation_threshold_changed(currentThread);
			}
		}
	}
}

static bool
gcInitializeVMHooks(MM_GCExtensionsBase *extensions)
{
	bool result = true;
	J9JavaVM* javaVM = (J9JavaVM *)extensions->getOmrVM()->_language_vm;

	J9HookInterface **vmHookInterface = javaVM->internalVMFunctions->getVMHookInterface(javaVM);
	if (NULL == vmHookInterface) {
		result = false;
	} else if ((*vmHookInterface)->J9HookRegisterWithCallSite(vmHookInterface, J9HOOK_VM_THREAD_CRASH, hookValidatorVMThreadCrash, OMR_GET_CALLSITE(), NULL)) {
		result = false;
	} else if ((*vmHookInterface)->J9HookRegisterWithCallSite(vmHookInterface, J9HOOK_REGISTRATION_EVENT, hookVMRegistrationEvent, OMR_GET_CALLSITE(), javaVM)) {
		result = false;
	}
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	else if (extensions->concurrentScavenger) {
		if ((*vmHookInterface)->J9HookRegisterWithCallSite(vmHookInterface, J9HOOK_VM_ACQUIREVMACCESS, hookAcquireVMAccess, OMR_GET_CALLSITE(), NULL)) {
			result = false;
		} else if (extensions->concurrentScavengeExhaustiveTermination) {
			/* Register these hooks only if Exhaustive Termination optimization is enabled */
			if ((*vmHookInterface)->J9HookRegisterWithCallSite(vmHookInterface, J9HOOK_VM_RELEASEVMACCESS, hookReleaseVMAccess, OMR_GET_CALLSITE(), NULL)) {
				result = false;
			} else if ((*vmHookInterface)->J9HookRegisterWithCallSite(vmHookInterface, J9HOOK_VM_ACQUIRING_EXCLUSIVE_IN_NATIVE, hookAcquiringExclusiveInNative, OMR_GET_CALLSITE(), NULL)) {
				result = false;
			}
		}
	}
#endif /* OMR_GC_CONCURRENT_SCAVENGER */

	return result;
}

static void
gcCleanupVMHooks(MM_GCExtensionsBase *extensions)
{
	J9JavaVM* javaVM = (J9JavaVM *)extensions->getOmrVM()->_language_vm;
	J9HookInterface **vmHookInterface = javaVM->internalVMFunctions->getVMHookInterface(javaVM);
	if (NULL != vmHookInterface) {
		(*vmHookInterface)->J9HookUnregister(vmHookInterface, J9HOOK_VM_THREAD_CRASH, hookValidatorVMThreadCrash, NULL);
		(*vmHookInterface)->J9HookUnregister(vmHookInterface, J9HOOK_REGISTRATION_EVENT, hookVMRegistrationEvent, javaVM);
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
		if (extensions->concurrentScavenger) {
			(*vmHookInterface)->J9HookUnregister(vmHookInterface, J9HOOK_VM_ACQUIREVMACCESS, hookAcquireVMAccess, NULL);
			if (extensions->concurrentScavengeExhaustiveTermination) {
				(*vmHookInterface)->J9HookUnregister(vmHookInterface, J9HOOK_VM_RELEASEVMACCESS, hookReleaseVMAccess, NULL);
				(*vmHookInterface)->J9HookUnregister(vmHookInterface, J9HOOK_VM_ACQUIRING_EXCLUSIVE_IN_NATIVE, hookAcquiringExclusiveInNative, NULL);
			}
		}
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
	}
}

} /* extern "C" */

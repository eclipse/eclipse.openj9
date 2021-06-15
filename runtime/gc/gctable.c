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

#include "gc_internal.h"
#include "HeapIteratorAPI.h"
#include "modronapicore.hpp"

J9MemoryManagerFunctions MemoryManagerFunctions = {
	J9AllocateIndexableObject,
	J9AllocateObject,
	J9AllocateIndexableObjectNoGC,
	J9AllocateObjectNoGC,
	J9WriteBarrierPost,
	J9WriteBarrierBatch,
	J9WriteBarrierPostClass,
	J9WriteBarrierClassBatch,
	allocateMemoryForSublistFragment,
	j9gc_heap_free_memory,
	j9gc_heap_total_memory,
	j9gc_is_garbagecollection_disabled,
	j9gc_allsupported_memorypools,
	j9gc_allsupported_garbagecollectors,
	j9gc_pool_name,
	j9gc_garbagecollector_name,
	j9gc_is_managedpool_by_collector,
	j9gc_is_usagethreshold_supported,
	j9gc_is_collectionusagethreshold_supported,
	j9gc_is_local_collector,
	j9gc_get_collector_id,
	j9gc_pools_memory,
	j9gc_pool_maxmemory,
	j9gc_pool_memoryusage,
	j9gc_get_gc_action,
	j9gc_get_gc_cause,
	j9gc_get_private_hook_interface,
	gcStartupHeapManagement,
	gcShutdownHeapManagement,
	j9gc_jvmPhaseChange,
	initializeMutatorModelJava,
	cleanupMutatorModelJava,
#if defined(J9VM_GC_FINALIZATION)
	j9gc_finalizer_startup,
	j9gc_finalizer_shutdown,
	j9gc_wait_for_reference_processing,
	runFinalization,
#endif /* J9VM_GC_FINALIZATION */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	forceClassLoaderUnload,
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
#if defined(J9VM_GC_FINALIZATION)
	finalizeObjectCreated,
#endif /* J9VM_GC_FINALIZATION */
	j9gc_ext_is_marked,
#if defined(J9VM_GC_BATCH_CLEAR_TLH)
	allocateZeroedTLHPages,
	isAllocateZeroedTLHPagesEnabled,
#endif /* J9VM_GC_BATCH_CLEAR_TLH */
	isStaticObjectAllocateFlags,
	getStaticObjectAllocateFlags,
	j9gc_scavenger_enabled,
	j9gc_concurrent_scavenger_enabled,
	j9gc_software_read_barrier_enabled,
	j9gc_hot_reference_field_required,
	j9gc_max_hot_field_list_length,
#if defined(J9VM_GC_HEAP_CARD_TABLE)
	j9gc_concurrent_getCardSize,
	j9gc_concurrent_getHeapBase,
#endif /* J9VM_GC_HEAP_CARD_TABLE */
	j9gc_modron_getWriteBarrierType,
	j9gc_modron_getReadBarrierType,
	queryGCStatus,
	j9gc_flush_caches_for_walk,
	j9gc_flush_nonAllocationCaches_for_walk,
	j9gc_get_hook_interface,
	j9gc_get_omr_hook_interface,
	j9gc_get_overflow_safe_alloc_size,
	getVerboseGCFunctionTable,
	referenceArrayCopy,
	/* TODO: disable this entrypoint once the JIT has been updated */
	referenceArrayCopyIndex,
	alwaysCallReferenceArrayCopyHelper,
	j9gc_ext_reachable_objects_do,
	j9gc_ext_reachable_from_object_do,
	j9gc_jit_isInlineAllocationSupported,
	J9WriteBarrierPre,
	J9WriteBarrierPreClass,
	J9ReadBarrier,
	J9ReadBarrierClass,
	j9gc_weakRoot_readObject,
	j9gc_weakRoot_readObjectVM,
	j9gc_ext_check_is_valid_heap_object,
#if defined(J9VM_GC_FINALIZATION)
	j9gc_get_objects_pending_finalization_count,
#endif /* J9VM_GC_FINALIZATION */
	j9gc_set_softmx,
	j9gc_get_softmx,
	j9gc_get_initial_heap_size,
	j9gc_get_maximum_heap_size,
	j9gc_objaccess_checkClassLive,
#if defined(J9VM_GC_OBJECT_ACCESS_BARRIER)
	j9gc_objaccess_indexableReadI8,
	j9gc_objaccess_indexableReadU8,
	j9gc_objaccess_indexableReadI16,
	j9gc_objaccess_indexableReadU16,
	j9gc_objaccess_indexableReadI32,
	j9gc_objaccess_indexableReadU32,
	j9gc_objaccess_indexableReadI64,
	j9gc_objaccess_indexableReadU64,
	j9gc_objaccess_indexableReadObject,
	j9gc_objaccess_indexableReadAddress,
	j9gc_objaccess_indexableStoreI8,
	j9gc_objaccess_indexableStoreU8,
	j9gc_objaccess_indexableStoreI16,
	j9gc_objaccess_indexableStoreU16,
	j9gc_objaccess_indexableStoreI32,
	j9gc_objaccess_indexableStoreU32,
	j9gc_objaccess_indexableStoreI64,
	j9gc_objaccess_indexableStoreU64,
#if !defined(J9VM_ENV_DATA64)
	j9gc_objaccess_indexableStoreU64Split,
#endif /* !J9VM_ENV_DATA64 */
	j9gc_objaccess_indexableStoreObject,
	j9gc_objaccess_indexableStoreAddress,
	j9gc_objaccess_mixedObjectReadI32,
	j9gc_objaccess_mixedObjectReadU32,
	j9gc_objaccess_mixedObjectReadI64,
	j9gc_objaccess_mixedObjectReadU64,
	j9gc_objaccess_mixedObjectReadObject,
	j9gc_objaccess_mixedObjectReadAddress,
	j9gc_objaccess_mixedObjectStoreI32,
	j9gc_objaccess_mixedObjectStoreU32,
	j9gc_objaccess_mixedObjectStoreI64,
	j9gc_objaccess_mixedObjectStoreU64,
	j9gc_objaccess_mixedObjectStoreObject,
	j9gc_objaccess_mixedObjectStoreAddress,
#if !defined(J9VM_ENV_DATA64)
	j9gc_objaccess_mixedObjectStoreU64Split,
#endif /* !J9VM_ENV_DATA64 */
	j9gc_objaccess_mixedObjectCompareAndSwapInt,
	j9gc_objaccess_mixedObjectCompareAndSwapLong,
	j9gc_objaccess_mixedObjectCompareAndExchangeInt,
	j9gc_objaccess_mixedObjectCompareAndExchangeLong,
	j9gc_objaccess_staticReadI32,
	j9gc_objaccess_staticReadU32,
	j9gc_objaccess_staticReadI64,
	j9gc_objaccess_staticReadU64,
	j9gc_objaccess_staticReadObject,
	j9gc_objaccess_staticReadAddress,
	j9gc_objaccess_staticStoreI32,
	j9gc_objaccess_staticStoreU32,
	j9gc_objaccess_staticStoreI64,
	j9gc_objaccess_staticStoreU64,
	j9gc_objaccess_staticStoreObject,
	j9gc_objaccess_staticStoreAddress,
#if !defined(J9VM_ENV_DATA64)
	j9gc_objaccess_staticStoreU64Split,
#endif /* !J9VM_ENV_DATA64 */
	j9gc_objaccess_storeObjectToInternalVMSlot,
	j9gc_objaccess_readObjectFromInternalVMSlot,
	j9gc_objaccess_getArrayObjectDataAddress,
	j9gc_objaccess_getLockwordAddress,
	j9gc_objaccess_cloneObject,
	j9gc_objaccess_copyObjectFields,
	j9gc_objaccess_copyObjectFieldsToFlattenedArrayElement,
	j9gc_objaccess_copyObjectFieldsFromFlattenedArrayElement,
	j9gc_objaccess_structuralCompareFlattenedObjects,
	j9gc_objaccess_cloneIndexableObject,
	j9gc_objaccess_asConstantPoolObject,
	j9gc_objaccess_referenceGet,
	j9gc_objaccess_referenceReprocess,
	j9gc_objaccess_jniDeleteGlobalReference,
	j9gc_objaccess_compareAndSwapObject,
	j9gc_objaccess_staticCompareAndSwapObject,
	j9gc_objaccess_compareAndExchangeObject,
	j9gc_objaccess_staticCompareAndExchangeObject,
	j9gc_objaccess_fillArrayOfObjects,
	j9gc_objaccess_compressedPointersShift,
	j9gc_objaccess_compressedPointersShadowHeapBase,
	j9gc_objaccess_compressedPointersShadowHeapTop,
#endif /* J9VM_GC_OBJECT_ACCESS_BARRIER */
	j9gc_get_gcmodestring,
	j9gc_get_object_size_in_bytes,
	j9gc_get_object_total_footprint_in_bytes,
	j9gc_modron_global_collect,
	j9gc_modron_global_collect_with_overrides,
	j9gc_modron_local_collect,
	j9gc_all_object_and_vm_slots_do,
	j9mm_iterate_heaps,
	j9mm_iterate_spaces,
	j9mm_iterate_roots,
	j9mm_iterate_regions,
	j9mm_iterate_region_objects,
	j9mm_find_region_for_pointer,
	j9mm_iterate_object_slots,
	j9mm_initialize_object_descriptor,
	j9mm_iterate_all_objects,
	j9gc_modron_isFeatureSupported,
	j9gc_modron_getConfigurationValueForKey,
	omrgc_get_version,
	j9mm_abandon_object,
	j9mm_get_guaranteed_nursery_range,
	j9gc_arraylet_getLeafSize,
	j9gc_arraylet_getLeafLogSize,
	j9gc_set_allocation_sampling_interval,
	j9gc_set_allocation_threshold,
	j9gc_objaccess_recentlyAllocatedObject,
	j9gc_objaccess_postStoreClassToClassLoader,
	j9gc_objaccess_getObjectHashCode,
	j9gc_createJavaLangString,
	j9gc_internString,
#if defined(J9VM_GC_FINALIZATION)
	j9gc_runFinalizersOnExit,
#endif /* J9VM_GC_FINALIZATION */
	j9gc_objaccess_jniGetPrimitiveArrayCritical,
	j9gc_objaccess_jniReleasePrimitiveArrayCritical,
	j9gc_objaccess_jniGetStringCritical,
	j9gc_objaccess_jniReleaseStringCritical,
	j9gc_finalizer_completeFinalizersOnExit,
	j9gc_get_CPU_times,
	omrgc_walkLWNRLockTracePool,
#if defined(J9VM_GC_OBJECT_ACCESS_BARRIER)
	j9gc_objaccess_staticCompareAndSwapInt,
	j9gc_objaccess_staticCompareAndExchangeInt,
#if !defined(J9VM_ENV_DATA64)
	j9gc_objaccess_mixedObjectCompareAndSwapLongSplit,
#endif /* !J9VM_ENV_DATA64 */
	j9gc_objaccess_staticCompareAndSwapLong,
	j9gc_objaccess_staticCompareAndExchangeLong,
#if !defined(J9VM_ENV_DATA64)
	j9gc_objaccess_staticCompareAndSwapLongSplit,
#endif /* !J9VM_ENV_DATA64 */
#endif /* J9VM_GC_OBJECT_ACCESS_BARRIER */
	j9gc_get_bytes_allocated_by_thread,
	j9mm_iterate_all_ownable_synchronizer_objects,
	ownableSynchronizerObjectCreated,
	j9gc_notifyGCOfClassReplacement,
	j9gc_get_jit_string_dedup_policy,
	j9gc_stringHashFn,
	j9gc_stringHashEqualFn
};

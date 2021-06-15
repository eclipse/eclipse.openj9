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

#if !defined(GC_INTERNAL_H_)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define GC_INTERNAL_H_
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/*
 * This file defines extern "C" prototypes for C functions defined and used internally
 * in the GC module.
 * 
 * These are mostly used in the J9MemoryManagerFunctions function table definition
 * in gctable.c 
 * 
 * These prototypes were all moved here from j9protos.h as a first step in cleaning them up.
 * Many of these are still duplicated in other header files, and further clean up of this
 * file is required.
 */

#include "j9.h"
#include "omr.h"
#include "jvmti.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Card table helpers for incremental-update barriers */
extern J9_CFUNC UDATA j9gc_concurrent_getCardSize(J9JavaVM *javaVM); /* deprecated */
extern J9_CFUNC UDATA j9gc_concurrent_getHeapBase(J9JavaVM *javaVM); /* deprecated */
extern J9_CFUNC UDATA j9gc_incrementalUpdate_getCardSize(OMR_VM *omrVM);
extern J9_CFUNC UDATA j9gc_incrementalUpdate_getCardTableShiftValue(OMR_VM *omrVM);
extern J9_CFUNC UDATA j9gc_incrementalUpdate_getHeapBase(OMR_VM *omrVM);
extern J9_CFUNC UDATA j9gc_incrementalUpdate_getCardTableVirtualStart(OMR_VM *omrVM);


/* J9VMGCModronHLLPrototypeHolder */
extern J9_CFUNC UDATA j9gc_objaccess_indexableReadU16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC void j9gc_flush_caches_for_walk(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_flush_nonAllocationCaches_for_walk(J9JavaVM *javaVM);
extern J9_CFUNC UDATA alwaysCallReferenceArrayCopyHelper(J9JavaVM *javaVM);
extern J9_CFUNC void J9WriteBarrierBatch(J9VMThread *vmThread, j9object_t destinationObject);
extern J9_CFUNC IDATA j9gc_objaccess_indexableReadI16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC void J9WriteBarrierPost(J9VMThread *vmThread, j9object_t destinationObject, j9object_t storedObject);
extern J9_CFUNC UDATA j9gc_heap_total_memory(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_is_garbagecollection_disabled(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_allsupported_memorypools(J9JavaVM* javaVM);
extern J9_CFUNC UDATA j9gc_allsupported_garbagecollectors(J9JavaVM* javaVM);
extern J9_CFUNC const char* j9gc_pool_name(J9JavaVM* javaVM, UDATA poolID);
extern J9_CFUNC const char* j9gc_garbagecollector_name(J9JavaVM* javaVM, UDATA gcID);
extern J9_CFUNC UDATA j9gc_is_managedpool_by_collector(J9JavaVM* javaVM, UDATA gcID, UDATA poolID);
extern J9_CFUNC UDATA j9gc_is_usagethreshold_supported(J9JavaVM* javaVM, UDATA poolID);
extern J9_CFUNC UDATA j9gc_is_collectionusagethreshold_supported(J9JavaVM* javaVM, UDATA poolID);
extern J9_CFUNC UDATA j9gc_is_local_collector(J9JavaVM* javaVM, UDATA gcID);
extern J9_CFUNC UDATA j9gc_get_collector_id(OMR_VMThread *omrVMThread);
extern J9_CFUNC UDATA j9gc_pools_memory(J9JavaVM* javaVM, UDATA poolIDs, UDATA* totals, UDATA* frees, BOOLEAN gcEnd);
extern J9_CFUNC UDATA j9gc_pool_maxmemory(J9JavaVM* javaVM, UDATA poolID);
extern J9_CFUNC UDATA j9gc_pool_memoryusage(J9JavaVM *javaVM, UDATA poolID, UDATA *free, UDATA *total);
extern J9_CFUNC const char* j9gc_get_gc_action(J9JavaVM* javaVM, UDATA gcID);
extern J9_CFUNC const char* j9gc_get_gc_cause(OMR_VMThread* omrVMthread);
extern J9_CFUNC struct J9HookInterface** j9gc_get_private_hook_interface(J9JavaVM *javaVM);
extern J9_CFUNC UDATA isAllocateZeroedTLHPagesEnabled(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_indexableStoreI16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_all_object_and_vm_slots_do(J9JavaVM *javaVM, void *function, void *userData, UDATA walkFlags);
extern J9_CFUNC j9object_t j9gc_objaccess_referenceGet(J9VMThread *vmThread, j9object_t refObject);
extern J9_CFUNC void j9gc_objaccess_referenceReprocess(J9VMThread *vmThread, j9object_t refObject);
extern J9_CFUNC UDATA j9gc_ext_check_is_valid_heap_object(J9JavaVM *javaVM, j9object_t ptr, UDATA flags);
extern J9_CFUNC void J9WriteBarrierClassBatch(J9VMThread *vmThread, J9Class *destinationClazz);
extern J9_CFUNC UDATA mergeMemorySpaces(J9VMThread *vmThread, void *destinationMemorySpace, void *sourceMemorySpace);
extern J9_CFUNC UDATA j9gc_scavenger_enabled(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_concurrent_scavenger_enabled(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_software_read_barrier_enabled(J9JavaVM *javaVM);
extern J9_CFUNC BOOLEAN j9gc_hot_reference_field_required(J9JavaVM *javaVM);
extern J9_CFUNC uint32_t j9gc_max_hot_field_list_length(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_indexableStoreAddress(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, void *value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreAddress(J9VMThread *vmThread, j9object_t destObject, UDATA offset, void *value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_cloneObject(J9VMThread *vmThread, j9object_t srcObject, j9object_t destObject);
extern J9_CFUNC void j9gc_objaccess_copyObjectFields(J9VMThread *vmThread, J9Class *valueClass, J9Object *srcObject, UDATA srcOffset, J9Object *destObject, UDATA destOffset);
extern J9_CFUNC void j9gc_objaccess_copyObjectFieldsToFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t srcObject, J9IndexableObject *arrayRef, I_32 index);
extern J9_CFUNC void j9gc_objaccess_copyObjectFieldsFromFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t destObject, J9IndexableObject *arrayRef, I_32 index);
extern J9_CFUNC BOOLEAN j9gc_objaccess_structuralCompareFlattenedObjects(J9VMThread *vmThread, J9Class *valueClass, j9object_t lhsObject, j9object_t rhsObject, UDATA startOffset);
extern J9_CFUNC j9object_t j9gc_objaccess_asConstantPoolObject(J9VMThread *vmThread, j9object_t toConvert, UDATA allocationFlags);
extern J9_CFUNC jvmtiIterationControl j9mm_iterate_heaps(J9JavaVM *vm, J9PortLibrary *portLibrary, UDATA flags, jvmtiIterationControl(*func)(J9JavaVM *vm, struct J9MM_IterateHeapDescriptor *heapDesc, void *userData), void *userData);
extern J9_CFUNC int gcStartupHeapManagement(J9JavaVM * vm);
extern J9_CFUNC void j9gc_jvmPhaseChange(J9VMThread *currentThread, UDATA phase);
extern J9_CFUNC void j9gc_ext_reachable_from_object_do(J9VMThread *vmThread, j9object_t objectPtr, jvmtiIterationControl(*func)(j9object_t *slotPtr, j9object_t sourcePtr, void *userData, IDATA type, IDATA index, IDATA wasReportedBefore), void *userData, UDATA walkFlags);
extern J9_CFUNC UDATA moveObjectToMemorySpace(J9VMThread *vmThread, void *destinationMemorySpace, j9object_t objectPtr);
/* TODO: The signature of allocateMemoryForSublistFragment temporarily uses void* instead of OMR_VMThread* since the latter is a class */
extern J9_CFUNC UDATA allocateMemoryForSublistFragment(void* vmThread, J9VMGC_SublistFragment *fragmentPrimitive);
extern J9_CFUNC UDATA j9gc_objaccess_compressedPointersShadowHeapBase(J9VMThread *vmThread);
extern J9_CFUNC void j9gc_objaccess_indexableStoreU32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_compareAndSwapObject(J9VMThread *vmThread, j9object_t destObject, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject);
extern J9_CFUNC UDATA j9gc_objaccess_staticCompareAndSwapObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject);
extern J9_CFUNC UDATA j9gc_objaccess_mixedObjectCompareAndSwapInt(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 compareValue, U_32 swapValue);
extern J9_CFUNC UDATA j9gc_objaccess_staticCompareAndSwapInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue);
extern J9_CFUNC UDATA j9gc_objaccess_mixedObjectCompareAndSwapLong(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 compareValue, U_64 swapValue);
extern J9_CFUNC UDATA j9gc_objaccess_mixedObjectCompareAndSwapLongSplit(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1);
extern J9_CFUNC UDATA j9gc_objaccess_staticCompareAndSwapLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue);
extern J9_CFUNC UDATA j9gc_objaccess_staticCompareAndSwapLongSplit(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1);
extern J9_CFUNC j9object_t j9gc_objaccess_compareAndExchangeObject(J9VMThread *vmThread, j9object_t destObject, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject);
extern J9_CFUNC j9object_t j9gc_objaccess_staticCompareAndExchangeObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject);
extern J9_CFUNC U_32 j9gc_objaccess_mixedObjectCompareAndExchangeInt(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 compareValue, U_32 swapValue);
extern J9_CFUNC U_32 j9gc_objaccess_staticCompareAndExchangeInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue);
extern J9_CFUNC U_64 j9gc_objaccess_mixedObjectCompareAndExchangeLong(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 compareValue, U_64 swapValue);
extern J9_CFUNC U_64 j9gc_objaccess_staticCompareAndExchangeLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue);
extern J9_CFUNC void J9ResetThreadLocalHeap(J9VMThread *vmContext, UDATA flush);
extern J9_CFUNC void j9gc_objaccess_storeObjectToInternalVMSlot(J9VMThread *vmThread, j9object_t *destSlot, j9object_t value);
extern J9_CFUNC jint  JNICALL queryGCStatus(JavaVM *vm, jint *nHeaps, GCStatus *status, jint statusSize);
extern J9_CFUNC int j9gc_finalizer_startup(J9JavaVM * vm);
extern J9_CFUNC UDATA j9gc_wait_for_reference_processing(J9JavaVM *vm);
extern J9_CFUNC UDATA j9gc_get_maximum_heap_size(J9JavaVM *javaVM);
extern J9_CFUNC I_32 j9gc_get_jit_string_dedup_policy(J9JavaVM *javaVM);
extern J9_CFUNC void* j9gc_objaccess_staticReadAddress(J9VMThread *vmThread, J9Class *clazz, void **srcSlot, UDATA isVolatile);
extern J9_CFUNC IDATA j9gc_objaccess_indexableReadI32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC void allocateZeroedTLHPages(J9JavaVM *javaVM, UDATA flag);
extern J9_CFUNC I_64 j9gc_objaccess_mixedObjectReadI64(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_indexableReadU32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC j9objectmonitor_t* j9gc_objaccess_getLockwordAddress(J9VMThread *vmThread, j9object_t object);
extern J9_CFUNC j9object_t j9gc_objaccess_readObjectFromInternalVMSlot(J9VMThread *vmThread, J9JavaVM *vm, J9Object **srcSlot);
extern J9_CFUNC U_64 j9gc_objaccess_mixedObjectReadU64(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC BOOLEAN checkStringConstantsLive(J9JavaVM *javaVM, j9object_t stringOne, j9object_t stringTwo);
extern J9_CFUNC BOOLEAN checkStringConstantLive(J9JavaVM *javaVM, j9object_t string);
extern J9_CFUNC UDATA j9gc_objaccess_checkClassLive(J9JavaVM *javaVM, J9Class *classPtr);
extern J9_CFUNC UDATA j9gc_modron_getWriteBarrierType(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_modron_getReadBarrierType(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_indexableStoreI8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_staticStoreObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *destSlot, j9object_t value, UDATA isVolatile);
extern J9_CFUNC void gcShutdownHeapManagement(J9JavaVM * vm);
extern J9_CFUNC jvmtiIterationControl j9mm_iterate_regions(J9JavaVM *vm, J9PortLibrary *portLibrary, struct J9MM_IterateSpaceDescriptor *space, UDATA flags, jvmtiIterationControl(*func)(J9JavaVM *vm, struct J9MM_IterateRegionDescriptor *regionDesc, void *userData), void *userData);
extern J9_CFUNC UDATA j9mm_find_region_for_pointer(J9JavaVM* javaVM, void *pointer, struct J9MM_IterateRegionDescriptor *regionDesc);
extern J9_CFUNC void j9gc_objaccess_staticStoreU64Split(J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_compressedPointersShadowHeapTop(J9VMThread *vmThread);
extern J9_CFUNC U_64 j9gc_objaccess_staticReadU64(J9VMThread *vmThread, J9Class *clazz, U_64 *srcSlot, UDATA isVolatile);
extern J9_CFUNC IDATA j9gc_objaccess_indexableReadI8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC j9object_t j9gc_objaccess_staticReadObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *srcSlot, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_indexableReadU8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_get_softmx(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_set_softmx(J9JavaVM *javaVM, UDATA newsoftmx);
extern J9_CFUNC void j9gc_finalizer_shutdown(J9JavaVM * vm);
extern J9_CFUNC UDATA j9gc_get_object_size_in_bytes(J9JavaVM* javaVM, j9object_t objectPtr);
extern J9_CFUNC UDATA j9gc_get_object_total_footprint_in_bytes(J9JavaVM *javaVM, j9object_t objectPtr);
extern J9_CFUNC UDATA j9gc_objaccess_compressedPointersShift(J9VMThread *vmThread);
extern J9_CFUNC void j9gc_objaccess_indexableStoreU64Split(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile);
extern J9_CFUNC void cleanupMutatorModelJava(J9VMThread* vmThread);
extern J9_CFUNC j9object_t j9gc_objaccess_mixedObjectReadObject(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_get_objects_pending_finalization_count(J9JavaVM* vm);
extern J9_CFUNC void j9gc_objaccess_indexableStoreU16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_jniDeleteGlobalReference(J9VMThread *vmThread, j9object_t reference);
extern J9_CFUNC UDATA isObjectInMemorySpace(J9VMThread *vmThread, void *memorySpace, j9object_t objectPtr);
extern J9_CFUNC UDATA j9gc_modron_global_collect(J9VMThread *vmThread);
extern J9_CFUNC UDATA j9gc_modron_global_collect_with_overrides(J9VMThread *vmThread, U_32 overrideFlags);
extern J9_CFUNC UDATA j9gc_get_initial_heap_size(J9JavaVM *javaVM);
extern J9_CFUNC UDATA j9gc_modron_local_collect(J9VMThread *vmThread);
extern J9_CFUNC I_32 referenceArrayCopy(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, fj9object_t *srcAddress, fj9object_t *destAddress, I_32 lengthInSlots);
extern J9_CFUNC void j9gc_objaccess_indexableStoreI64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_64 value, UDATA isVolatile);
extern J9_CFUNC void J9WriteBarrierPostClass(J9VMThread *vmThread, J9Class *destinationClazz, j9object_t storedObject);
extern J9_CFUNC const char* j9gc_get_gcmodestring(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_indexableStoreU8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_staticStoreU64(J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_64 value, UDATA isVolatile);
extern J9_CFUNC U_8* j9gc_objaccess_getArrayObjectDataAddress(J9VMThread *vmThread, J9IndexableObject *arrayObject);
extern J9_CFUNC jvmtiIterationControl j9mm_iterate_spaces(J9JavaVM *vm, J9PortLibrary *portLibrary, struct J9MM_IterateHeapDescriptor *heap, UDATA flags, jvmtiIterationControl(*func)(J9JavaVM *vm, struct J9MM_IterateSpaceDescriptor *spaceDesc, void *userData), void *userData);
extern J9_CFUNC void memoryManagerTLHAsyncCallbackHandler(J9VMThread *vmThread, IDATA handlerKey, void *userData);
extern J9_CFUNC void j9gc_objaccess_staticStoreI64(J9VMThread *vmThread, J9Class *clazz, I_64 *destSlot, I_64 value, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_mixedObjectReadU32(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC j9object_t J9AllocateObjectNoGC(J9VMThread *vmThread, J9Class *clazz, UDATA allocateFlags);
extern J9_CFUNC j9object_t J9AllocateIndexableObjectNoGC(J9VMThread *vmThread, J9Class *clazz, U_32 numberIndexedFields, UDATA allocateFlags);
extern J9_CFUNC void memoryManagerAsyncCallbackHandler(J9VMThread *vmThread, IDATA handlerKey, void *userData);
extern J9_CFUNC UDATA j9gc_get_overflow_safe_alloc_size(J9JavaVM *javaVM);
extern J9_CFUNC IDATA j9gc_objaccess_mixedObjectReadI32(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC j9object_t J9AllocateObject(J9VMThread *vmContext, J9Class *clazz, UDATA allocateFlags);
extern J9_CFUNC UDATA j9gc_ext_is_marked(J9JavaVM *javaVM, j9object_t objectPtr);
extern J9_CFUNC void J9WriteBarrierPre(J9VMThread *vmThread, J9Object *dstObject, fj9object_t *dstAddress, J9Object *srcObject);
extern J9_CFUNC void J9WriteBarrierPreClass(J9VMThread *vmThread, J9Object *dstClassObject, J9Object **dstAddress, J9Object *srcObject);
extern J9_CFUNC void J9ReadBarrier(J9VMThread *vmThread, fj9object_t *srcAddress);
extern J9_CFUNC void J9ReadBarrierClass(J9VMThread *vmThread, j9object_t *srcAddress);
extern J9_CFUNC j9object_t j9gc_weakRoot_readObject(J9VMThread *vmThread, j9object_t *srcAddress);
extern J9_CFUNC j9object_t j9gc_weakRoot_readObjectVM(J9JavaVM *vm, j9object_t *srcAddress);
extern J9_CFUNC UDATA isStaticObjectAllocateFlags(J9JavaVM *javaVM);
extern J9_CFUNC void J9FlushThreadLocalHeap(J9VMThread *vmContext);
extern J9_CFUNC jvmtiIterationControl j9mm_iterate_region_objects(J9JavaVM *vm, J9PortLibrary *portLibrary, struct J9MM_IterateRegionDescriptor *region, UDATA flags, jvmtiIterationControl(*func)(J9JavaVM *vm, struct J9MM_IterateObjectDescriptor *objectDesc, void *userData), void *userData);
extern J9_CFUNC void j9gc_objaccess_cloneIndexableObject(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject);
extern J9_CFUNC I_32 referenceArrayCopyIndex(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots);
extern J9_CFUNC I_64 j9gc_objaccess_staticReadI64(J9VMThread *vmThread, J9Class *clazz, I_64 *srcSlot, UDATA isVolatile);
extern J9_CFUNC jvmtiIterationControl j9mm_iterate_object_slots(J9JavaVM *javaVM, J9PortLibrary *portLibrary, struct J9MM_IterateObjectDescriptor *object, UDATA flags, jvmtiIterationControl(*func)(J9JavaVM *javaVM, struct J9MM_IterateObjectDescriptor *objectDesc, struct J9MM_IterateObjectRefDescriptor *refDesc, void *userData), void *userData);
extern J9_CFUNC j9object_t J9AllocateIndexableObject(J9VMThread *vmContext, J9Class *clazz, U_32 size, UDATA allocateFlags);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreObject(J9VMThread *vmThread, j9object_t destObject, UDATA offset, j9object_t value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_indexableStoreI32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile);
extern J9_CFUNC void* j9gc_objaccess_mixedObjectReadAddress(J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile);
extern J9_CFUNC j9object_t j9gc_objaccess_indexableReadObject(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC struct J9HookInterface** j9gc_get_hook_interface(J9JavaVM *javaVM);
extern J9_CFUNC struct J9HookInterface** j9gc_get_omr_hook_interface(OMR_VM *omrVM);
extern J9_CFUNC void j9gc_objaccess_staticStoreU32(J9VMThread *vmThread, J9Class *clazz, U_32 *destSlot, U_32 value, UDATA isVolatile);
extern J9_CFUNC IDATA initializeMutatorModelJava(J9VMThread* vmThread);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreU64Split(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile);
extern J9_CFUNC IDATA j9gc_objaccess_staticReadI32(J9VMThread *vmThread, J9Class *clazz, I_32 *srcSlot, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_indexableStoreObject(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, j9object_t value, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_objaccess_staticReadU32(J9VMThread *vmThread, J9Class *clazz, U_32 *srcSlot, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_staticStoreAddress(J9VMThread *vmThread, J9Class *clazz, void **destSlot, void *value, UDATA isVolatile);
extern J9_CFUNC void* getVerboseGCFunctionTable(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_staticStoreI32(J9VMThread *vmThread, J9Class *clazz, I_32 *destSlot, I_32 value, UDATA isVolatile);
extern J9_CFUNC U_64 j9gc_objaccess_indexableReadU64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC void* j9gc_objaccess_indexableReadAddress(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC void j9gc_ext_reachable_objects_do(J9VMThread *vmThread, jvmtiIterationControl(*func)(j9object_t *slotPtr, j9object_t sourcePtr, void *userData, IDATA type, IDATA index, IDATA wasReportedBefore), void *userData, UDATA walkFlags);
extern J9_CFUNC I_64 j9gc_objaccess_indexableReadI64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_modron_isFeatureSupported(J9JavaVM *javaVM, UDATA feature);
extern J9_CFUNC UDATA j9gc_modron_getConfigurationValueForKey(J9JavaVM *javaVM, UDATA key, void *value);
extern J9_CFUNC UDATA j9gc_jit_isInlineAllocationSupported(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_indexableStoreU64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_64 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreU32(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 value, UDATA isVolatile);
extern J9_CFUNC UDATA j9gc_heap_free_memory(J9JavaVM *javaVM);
extern J9_CFUNC UDATA getStaticObjectAllocateFlags(J9JavaVM *javaVM);
extern J9_CFUNC void j9gc_objaccess_fillArrayOfObjects(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_32 count, j9object_t value);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreU64(J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreI32(J9VMThread *vmThread, j9object_t destObject, UDATA offset, I_32 value, UDATA isVolatile);
extern J9_CFUNC void j9gc_objaccess_mixedObjectStoreI64(J9VMThread *vmThread, j9object_t destObject, UDATA offset, I_64 value, UDATA isVolatile);
extern J9_CFUNC J9Object * j9gc_get_memoryController(J9VMThread *vmContext, J9Object *objectPtr);
extern J9_CFUNC void j9gc_set_memoryController(J9VMThread *vmThread, J9Object* objectPtr, J9Object *memoryController);
extern J9_CFUNC const char* omrgc_get_version(OMR_VM *omrVM);
extern J9_CFUNC void j9gc_startGCIfTimeExpired(OMR_VMThread* vmThread);
extern J9_CFUNC void j9gc_allocation_threshold_changed(J9VMThread* currentThread);
extern J9_CFUNC void j9gc_set_allocation_sampling_interval(J9JavaVM *vm, UDATA samplingInterval);
extern J9_CFUNC void j9gc_set_allocation_threshold(J9VMThread* vmThread, UDATA low, UDATA high);
extern J9_CFUNC void j9gc_objaccess_recentlyAllocatedObject(J9VMThread *vmThread, J9Object *dstObject);
extern J9_CFUNC void j9gc_objaccess_postStoreClassToClassLoader(J9VMThread *vmThread, J9ClassLoader* destClassLoader, J9Class* srcClass);
extern J9_CFUNC I_32 j9gc_objaccess_getObjectHashCode(J9JavaVM *vm, J9Object* object);
extern J9_CFUNC void* j9gc_objaccess_jniGetPrimitiveArrayCritical(J9VMThread* vmThread, jarray array, jboolean *isCopy);
extern J9_CFUNC void j9gc_objaccess_jniReleasePrimitiveArrayCritical(J9VMThread* vmThread, jarray array, void * elems, jint mode);
extern J9_CFUNC const jchar* j9gc_objaccess_jniGetStringCritical(J9VMThread* vmThread, jstring str, jboolean *isCopy);
extern J9_CFUNC void j9gc_objaccess_jniReleaseStringCritical(J9VMThread* vmThread, jstring str, const jchar * elems);
extern J9_CFUNC UDATA j9gc_arraylet_getLeafSize(J9JavaVM* javaVM);
extern J9_CFUNC UDATA j9gc_arraylet_getLeafLogSize(J9JavaVM* javaVM);
extern J9_CFUNC void j9gc_get_CPU_times(J9JavaVM *javaVM, U_64* mainCpuMillis, U_64* workerCpuMillis, U_32* maxThreads, U_32* currentThreads);


/* J9VMFinalizeSupport*/
extern J9_CFUNC void runFinalization(J9VMThread *vmThread);
extern J9_CFUNC UDATA finalizeObjectCreated(J9VMThread *vmThread, j9object_t object);
extern J9_CFUNC UDATA forceClassLoaderUnload(J9VMThread *vmThread, J9ClassLoader *classLoader);
extern J9_CFUNC void finalizeForcedUnfinalizedToFinalizable(J9VMThread *vmThread);
extern J9_CFUNC void* finalizeForcedClassLoaderUnload(J9VMThread *vmThread);
extern J9_CFUNC void j9gc_runFinalizersOnExit(J9VMThread* vmThread, UDATA run);
extern J9_CFUNC void j9gc_finalizer_completeFinalizersOnExit(J9VMThread* vmThread);

extern J9_CFUNC UDATA ownableSynchronizerObjectCreated(J9VMThread *vmThread, j9object_t object);

extern J9_CFUNC void j9gc_notifyGCOfClassReplacement(J9VMThread *vmThread, J9Class *originalClass, J9Class *replacementClass, UDATA isFastHCR);

/* GuaranteedNurseryRange.cpp */
void j9mm_get_guaranteed_nursery_range(J9JavaVM* javaVM, void** start, void** end);

/* StringTable.cpp */
extern J9_CFUNC j9object_t j9gc_createJavaLangString(J9VMThread *vmThread, U_8 *data, UDATA length, UDATA stringFlags);
extern J9_CFUNC j9object_t j9gc_internString(J9VMThread *vmThread, j9object_t sourceString);
extern UDATA j9gc_stringHashFn (void *key, void *userData);
extern BOOLEAN j9gc_stringHashEqualFn (void *leftKey, void *rightKey, void *userData);

/* modronapi.cpp */
extern J9_CFUNC UDATA j9gc_get_bytes_allocated_by_thread(J9VMThread* vmThread);

#ifdef __cplusplus
}
#endif

#endif /*GC_INTERNAL_H_*/

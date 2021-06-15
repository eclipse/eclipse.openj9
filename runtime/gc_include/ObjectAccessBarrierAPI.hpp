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

#if !defined(OBJECTACCESSBARRIERAPI_HPP_)

#define OBJECTACCESSBARRIERAPI_HPP_

#include "j9cfg.h"

#if defined(OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES)
#if OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES
#define MM_ObjectAccessBarrierAPI MM_ObjectAccessBarrierAPICompressed
#else /* OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES */
#define MM_ObjectAccessBarrierAPI MM_ObjectAccessBarrierAPIFull
#endif /* OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES */
#endif /* OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES */

#include "j9.h"
#include "j9modron.h"
#include "omrmodroncore.h"
#include "omr.h"

#include "AtomicSupport.hpp"
#include "ArrayCopyHelpers.hpp"
#include "j9nongenerated.h"

#define J9OAB_MIXEDOBJECT_EA(object, offset, type) (type *)(((U_8 *)(object)) + offset)

class MM_ObjectAccessBarrierAPI
{
	friend class MM_ObjectAccessBarrier;
	friend class MM_RealtimeAccessBarrier;

/* Data members & types */
public:

private:
	const UDATA _writeBarrierType;
	const UDATA _readBarrierType;
#if defined(OMR_GC_COMPRESSED_POINTERS)
	const UDATA _compressedPointersShift;
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */

protected:
#if defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS)
	bool const _compressObjectReferences;
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS) */

/* Methods */
public:

	/**
	 * Create an instance.
	 */
	MM_ObjectAccessBarrierAPI(J9VMThread *currentThread)
		: _writeBarrierType(currentThread->javaVM->gcWriteBarrierType)
		, _readBarrierType(currentThread->javaVM->gcReadBarrierType)
#if defined(OMR_GC_COMPRESSED_POINTERS)
		, _compressedPointersShift(currentThread->javaVM->compressedPointersShift)
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
#if defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS)
		, _compressObjectReferences(J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(currentThread))
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS) */
	{}

	/**
	 * Return back true if object references are compressed
	 * @return true, if object references are compressed
	 */
	MMINLINE bool compressObjectReferences() {
		return OMR_COMPRESS_OBJECT_REFERENCES(_compressObjectReferences);
	}

	static VMINLINE void
	internalPostBatchStoreObjectCardTableAndGenerational(J9VMThread *vmThread, j9object_t object)
	{
		/* Check to see if object is old.  If object is not old neither barrier is required
		 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
		 *
		 * Since card dirtying also requires this calculation remember these values
		 */
		UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
		UDATA objectDelta = (UDATA)object - base;
		UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
		if (objectDelta < size) {
			/* object is old so check for concurrent barrier */
			if (0 != (vmThread->privateFlags & J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE)) {
				/* concurrent barrier is enabled so dirty the card for object */
				dirtyCard(vmThread, objectDelta);
			}

			/* generational barrier is required if object is old*/
			rememberObject(vmThread, object);
		}
	}

	static VMINLINE void
	internalPostBatchStoreObjectGenerational(J9VMThread *vmThread, j9object_t object)
	{
		/* Check to see if object is old.  If object is not old neither barrier is required
		 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
		 *
		 * Since card dirtying also requires this calculation remember these values
		 */
		UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
		UDATA objectDelta = (UDATA)object - base;
		UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
		if (objectDelta < size) {
			/* generational barrier is required if object is old*/
			rememberObject(vmThread, object);
		}
	}

	static VMINLINE void
	internalPostBatchStoreObjectCardTableIncremental(J9VMThread *vmThread, j9object_t object)
	{
		/* Check to see if object is within the barrier range.
		 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0)
		 *
		 * Since card dirtying also requires this calculation remember these values
		 */
		UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
		UDATA objectDelta = (UDATA)object - base;
		UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
		if (objectDelta < size) {
			/* Object is within the barrier range.  Dirty the card */
			dirtyCard(vmThread, objectDelta);
		}
	}

	static VMINLINE void
	internalPostBatchStoreObjectCardTable(J9VMThread *vmThread, j9object_t object)
	{
		/* Check to see if object is old.  If object is not old neither barrier is required
		 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
		 *
		 * Since card dirtying also requires this calculation remember these values
		 */
		UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
		UDATA objectDelta = (UDATA)object - base;
		UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
		if (objectDelta < size) {
			/* object is old so check for concurrent barrier */
			if (0 != (vmThread->privateFlags & J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE)) {
				/* concurrent barrier is enabled so dirty the card for object */
				dirtyCard(vmThread, objectDelta);
			}
		}
	}

	/**
	 * Call after storing an object reference for barrier type
	 * j9gc_modron_wrtbar_cardmark_and_oldcheck
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 *
	 */
	static VMINLINE void
	internalPostObjectStoreCardTableAndGenerational(J9VMThread *vmThread, j9object_t object, j9object_t value)
	{
		/* if value is NULL neither barrier is required */
		if (NULL != value) {
			/* Check to see if object is old.  If object is not old neither barrier is required
			 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
			 *
			 * Since card dirtying also requires this calculation remember these values
			 */
			UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
			UDATA objectDelta = (UDATA)object - base;
			UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
			if (objectDelta < size) {
				/* object is old so check for concurrent barrier */
				if (0 != (vmThread->privateFlags & J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE)) {
					/* concurrent barrier is enabled so dirty the card for object */
					dirtyCard(vmThread, objectDelta);
				}

				/* generational barrier is required if object is old and value is new */
				/* Check to see if value is new using the same optimization as above */
				UDATA valueDelta = (UDATA)value - base;
				if (valueDelta >= size) {
					/* value is in new space so do generational barrier */
					rememberObject(vmThread, object);
				}
			}
		}
	}

	/**
	 * Call after storing an object reference for barrier type
	 * j9gc_modron_wrtbar_cardmark
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 *
	 */
	static VMINLINE void
	internalPostObjectStoreCardTable(J9VMThread *vmThread, j9object_t object, j9object_t value)
	{
		/* if value is NULL neither barrier is required */
		if (NULL != value) {
			/* Check to see if object is old.
			 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
			 *
			 * Since card dirtying also requires this calculation remember these values
			 */
			UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
			UDATA objectDelta = (UDATA)object - base;
			UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
			if (objectDelta < size) {
				/* object is old so check for concurrent barrier */
				if (0 != (vmThread->privateFlags & J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE)) {
					/* concurrent barrier is enabled so dirty the card for object */
					dirtyCard(vmThread, objectDelta);
				}
			}
		}
	}

	/**
	 * Call after storing an object reference for barrier type
	 * j9gc_modron_wrtbar_cardmark_incremental
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 *
	 */
	static VMINLINE void
	internalPostObjectStoreCardTableIncremental(J9VMThread *vmThread, j9object_t object, j9object_t value)
	{
		/* if value is NULL neither barrier is required */
		if (NULL != value) {
			/* Check to see if object is within the barrier range.
			 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0)
			 *
			 * Since card dirtying also requires this calculation remember these values
			 */
			UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
			UDATA objectDelta = (UDATA)object - base;
			UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
			if (objectDelta < size) {
				/* Object is within the barrier range.  Dirty the card */
				dirtyCard(vmThread, objectDelta);
			}
		}
	}

	/**
	 * Call after storing an object reference for barrier type
	 * j9gc_modron_wrtbar_oldcheck
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 *
	 */
	static VMINLINE void
	internalPostObjectStoreGenerational(J9VMThread *vmThread, j9object_t object, j9object_t value)
	{
		/* if value is NULL neither barrier is required */
		if (NULL != value) {
			/* Check to see if object is old.
			 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
			 *
			 * Since card dirtying also requires this calculation remember these values
			 */
			UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
			UDATA objectDelta = (UDATA)object - base;
			UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
			if (objectDelta < size) {
				/* generational barrier is required if object is old and value is new */
				/* Check to see if value is new using the same optimization as above */
				UDATA valueDelta = (UDATA)value - base;
				if (valueDelta >= size) {
					/* value is in new space so do generational barrier */
					rememberObject(vmThread, object);
				}
			}
		}
	}

	/**
	 * Call after storing an object reference for barrier type
	 * j9gc_modron_wrtbar_oldcheck
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 *
	 */
	static VMINLINE void
	internalPostObjectStoreGenerationalNoValueCheck(J9VMThread *vmThread, j9object_t object)
	{
		/* Check to see if object is old.
		 * if ((object - vmThread->omrVMThread->heapBaseForBarrierRange0) < vmThread->omrVMThread->heapSizeForBarrierRange0) then old
		 *
		 * Since card dirtying also requires this calculation remember these values
		 */
		UDATA base = (UDATA)vmThread->omrVMThread->heapBaseForBarrierRange0;
		UDATA objectDelta = (UDATA)object - base;
		UDATA size = vmThread->omrVMThread->heapSizeForBarrierRange0;
		if (objectDelta < size) {
			rememberObject(vmThread, object);
		}
	}

	/**
	 * Return the address of the lockword for the given object, or NULL if it
	 * does not have an inline lockword.
	 */
	VMINLINE j9objectmonitor_t *
	getLockwordAddress(J9VMThread *currentThread, J9Object *object)
	{
		j9objectmonitor_t *lockwordAddress = NULL;
		J9Class *clazz = J9OBJECT_CLAZZ(currentThread, object);
		if (!J9_IS_J9CLASS_VALUETYPE(clazz)) {
			UDATA lockOffset = clazz->lockOffset;
			if ((IDATA) lockOffset >= 0) {
				lockwordAddress = (j9objectmonitor_t *)(((U_8 *)object) + lockOffset);
			}
		}
		return lockwordAddress;
	}

	VMINLINE void
	cloneArray(J9VMThread *currentThread, j9object_t original, j9object_t copy, J9Class *objectClass, U_32 size)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) 
		currentThread->javaVM->memoryManagerFunctions->j9gc_objaccess_cloneIndexableObject(currentThread, (J9IndexableObject*)original, (J9IndexableObject*)copy);
#else /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
		bool copyLockword = true;
		
		if (OBJECT_HEADER_SHAPE_POINTERS == J9CLASS_SHAPE(objectClass)) {
			VM_ArrayCopyHelpers::referenceArrayCopy(currentThread, original, 0, copy, 0, size);
		} else {
			VM_ArrayCopyHelpers::primitiveArrayCopy(currentThread, original, 0, copy, 0, size, (((J9ROMArrayClass*)objectClass->romClass)->arrayShape & 0x0000FFFF));
		}
		if (copyLockword) {
			/* zero lockword, if present */
			j9objectmonitor_t *lockwordAddress = getLockwordAddress(currentThread, copy);
			if (NULL != lockwordAddress) {
				J9_STORE_LOCKWORD(currentThread, lockwordAddress, 0);
			}
		}
#endif /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
	}

	VMINLINE UDATA
	mixedObjectGetHeaderSize(J9Class *objectClass)
	{
		return (compressObjectReferences() ? sizeof(J9ObjectCompressed) : sizeof(J9ObjectFull)) + J9CLASS_PREPADDING_SIZE(objectClass);
	}

	VMINLINE UDATA
	mixedObjectGetDataSize(J9Class *objectClass)
	{
		return J9CLASS_UNPADDED_INSTANCE_SIZE(objectClass);
	}

	VMINLINE void
	cloneObject(J9VMThread *currentThread, j9object_t original, j9object_t copy, J9Class *objectClass)
	{
		UDATA offset = mixedObjectGetHeaderSize(objectClass);

		copyObjectFields(currentThread, objectClass, original, offset, copy, offset);
	}

	VMINLINE void 
	copyObjectFieldsToFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t srcObject, J9IndexableObject *arrayRef, UDATA index)
	{
		/* TODO optimizations for non-arraylet path will be added in the future */
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_copyObjectFieldsToFlattenedArrayElement(vmThread, arrayClazz, srcObject, arrayRef, (I_32)index);
	}
	
	VMINLINE void 
	copyObjectFieldsFromFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t destObject, J9IndexableObject *arrayRef, UDATA index)
	{
		/* TODO optimizations for non-arraylet path will be added in the future */
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_copyObjectFieldsFromFlattenedArrayElement(vmThread, arrayClazz, destObject, arrayRef, (I_32)index);
	}

	VMINLINE BOOLEAN
	structuralFlattenedCompareObjects(J9VMThread *vmThread, J9Class *valueClass, j9object_t lhsObject, j9object_t rhsObject, UDATA startOffset)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_structuralCompareFlattenedObjects(vmThread, valueClass, lhsObject, rhsObject, startOffset);
#else /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
		bool hasReferences = J9CLASS_HAS_REFERENCES(valueClass);
		if (hasReferences && (j9gc_modron_readbar_none != _readBarrierType)) {
			return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_structuralCompareFlattenedObjects(vmThread, valueClass, lhsObject, rhsObject, startOffset);
		} else {
			UDATA compareSize = mixedObjectGetDataSize(valueClass);
			startOffset += J9CLASS_PREPADDING_SIZE(valueClass);

			return (0 == memcmp((void*)((UDATA)lhsObject + startOffset), (void*)((UDATA)rhsObject + startOffset), (size_t)compareSize));
		}
#endif /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
	}

	/**
	 * Copy valueType from sourceObject to destObject
	 * See MM_ObjectAccessBarrier::copyObjectFields for detailed description
	 *
	 * @param vmThread vmthread token
	 * @param valueClass The valueType class
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param destObject The object being used.
	 * @param destOffset The offset of the field.
	 */
	VMINLINE void
	copyObjectFields(J9VMThread *vmThread, J9Class *objectClass, j9object_t srcObject, UDATA srcOffset, j9object_t destObject, UDATA destOffset)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_copyObjectFields(vmThread, objectClass, srcObject, srcOffset, destObject, destOffset);
#else /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
		bool hasReferences = J9CLASS_HAS_REFERENCES(objectClass);
		if (hasReferences && ((j9gc_modron_readbar_none != _readBarrierType) || (j9gc_modron_wrtbar_always == _writeBarrierType))) {
			vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_copyObjectFields(vmThread, objectClass, srcObject, srcOffset, destObject, destOffset);
		} else {
			UDATA offset = 0;
			UDATA limit = mixedObjectGetDataSize(objectClass);

			if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
				while (offset < limit) {
					*(uint32_t*)((UDATA)destObject + offset + destOffset) = *(uint32_t*)((UDATA)srcObject + offset + srcOffset);
					offset += sizeof(uint32_t);
				}
			} else {
				while (offset < limit) {
					*(uintptr_t*)((UDATA)destObject + offset + destOffset) = *(uintptr_t*)((UDATA)srcObject + offset + srcOffset);
					offset += sizeof(uintptr_t);
				}
			}
		
			/* zero lockword, if present */
			j9objectmonitor_t *lockwordAddress = getLockwordAddress(vmThread, destObject);
			if (NULL != lockwordAddress) {
				J9_STORE_LOCKWORD(vmThread, lockwordAddress, 0);
			}
			if (hasReferences) {
				postBatchStoreObject(vmThread, destObject);
			}
		}
#endif /* defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) */
	}

	/**
	 * Read an object field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 * @return result The object stored at srcOffset in srcObject
	 */
	VMINLINE j9object_t
	inlineMixedObjectReadObject(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadObject(vmThread, srcObject, srcOffset, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		fj9object_t *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, fj9object_t);
		
		preMixedObjectReadObject(vmThread, srcObject, actualAddress);
				
		protectIfVolatileBefore(isVolatile, true);
		j9object_t result = readObjectImpl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object pointer into an object.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcObject the object being stored into
	 * @param srcOffset the offset with in srcObject to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineMixedObjectStoreObject(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, j9object_t value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreObject(vmThread, srcObject, srcOffset, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreObject(vmThread, srcObject, srcOffset, value, isVolatile);
		} else {
			fj9object_t *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, fj9object_t);

			preMixedObjectStoreObject(vmThread, srcObject, actualAddress, value);

			protectIfVolatileBefore(isVolatile, false);
			storeObjectImpl(vmThread, actualAddress, value, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			postMixedObjectStoreObject(vmThread, srcObject, actualAddress, value);
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object pointer into an object atomically.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineMixedObjectCompareAndSwapObject(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
		fj9object_t *destAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, fj9object_t);

#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndSwapObject(vmThread, destObject, (J9Object **)destAddress, compareObject, swapObject));
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndSwapObject(vmThread, destObject, (J9Object **)destAddress, compareObject, swapObject));
		} else {
			preMixedObjectReadObject(vmThread, destObject, destAddress);
			preMixedObjectStoreObject(vmThread, destObject, destAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			bool result = compareAndSwapObjectImpl(vmThread, destAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			if (result) {
				postMixedObjectStoreObject(vmThread, destObject, destAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store an object pointer into an object atomically.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the object stored in the object field before the update
	 */
	VMINLINE j9object_t
	inlineMixedObjectCompareAndExchangeObject(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
		fj9object_t *destAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, fj9object_t);

#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndExchangeObject(vmThread, destObject, (J9Object **)destAddress, compareObject, swapObject);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndExchangeObject(vmThread, destObject, (J9Object **)destAddress, compareObject, swapObject);
		} else {
			preMixedObjectReadObject(vmThread, destObject, destAddress);
			preMixedObjectStoreObject(vmThread, destObject, destAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			fj9object_t resultToken = compareAndExchangeObjectImpl(vmThread, destAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			j9object_t result = internalConvertPointerFromToken(resultToken);

			if (result == compareObject) {
				postMixedObjectStoreObject(vmThread, destObject, destAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_32 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE I_32
	inlineMixedObjectReadI32(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (I_32)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadI32(vmThread, srcObject, srcOffset, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, I_32);

		protectIfVolatileBefore(isVolatile, true);
		I_32 result = readI32Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an I_32 value into an object.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcObject the object being stored into
	 * @param srcOffset the offset with in srcObject to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineMixedObjectStoreI32(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, I_32 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreI32(vmThread, srcObject, srcOffset, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, I_32);

		protectIfVolatileBefore(isVolatile, false);
		storeI32Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read a U_32 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE U_32
	inlineMixedObjectReadU32(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (U_32)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadU32(vmThread, srcObject, srcOffset, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, U_32);

		protectIfVolatileBefore(isVolatile, true);
		U_32 result = readU32Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an U_32 value into an object.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcObject the object being stored into
	 * @param srcOffset the offset with in srcObject to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineMixedObjectStoreU32(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, U_32 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreU32(vmThread, srcObject, srcOffset, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, U_32);

		protectIfVolatileBefore(isVolatile, false);
		storeU32Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an U_32 value into an object atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineMixedObjectCompareAndSwapU32(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectCompareAndSwapInt(vmThread, destObject, destOffset, compareValue, swapValue));
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, U_32);

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU32Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_32 value into an object atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the object field before the update
	 */
	VMINLINE U_32
	inlineMixedObjectCompareAndExchangeU32(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectCompareAndExchangeInt(vmThread, destObject, destOffset, compareValue, swapValue);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		U_32 *actualAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, U_32);

		protectIfVolatileBefore(isVolatile, false);
		U_32 result = compareAndExchangeU32Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_64 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE I_64
	inlineMixedObjectReadI64(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadI64(vmThread, srcObject, srcOffset, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, I_64);

		protectIfVolatileBefore(isVolatile, true);
		I_64 result = readI64Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an I_64 value into an object.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcObject the object being stored into
	 * @param srcOffset the offset with in srcObject to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineMixedObjectStoreI64(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, I_64 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreI64(vmThread, srcObject, srcOffset, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, I_64);

		protectIfVolatileBefore(isVolatile, false);
		storeI64Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read a U_64 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param srcObject The object being used.
	 * @param srcOffset The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE U_64
	inlineMixedObjectReadU64(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectReadU64(vmThread, srcObject, srcOffset, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, U_64);

		protectIfVolatileBefore(isVolatile, true);
		U_64 result = readU64Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an U_64 value into an object.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcObject the object being stored into
	 * @param srcOffset the offset with in srcObject to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineMixedObjectStoreU64(J9VMThread *vmThread, j9object_t srcObject, UDATA srcOffset, U_64 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectStoreU64(vmThread, srcObject, srcOffset, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(srcObject, srcOffset, U_64);

		protectIfVolatileBefore(isVolatile, false);
		storeU64Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an U_64 value into an object atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineMixedObjectCompareAndSwapU64(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectCompareAndSwapLong(vmThread, destObject, destOffset, compareValue, swapValue));
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, U_64);

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU64Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_64 value into an object atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destObject the object being stored into
	 * @param destOffset the offset with in srcObject to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the object field before the update
	 */
	VMINLINE U_64
	inlineMixedObjectCompareAndExchangeU64(J9VMThread *vmThread, j9object_t destObject, UDATA destOffset, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_mixedObjectCompareAndExchangeLong(vmThread, destObject, destOffset, compareValue, swapValue);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		U_64 *actualAddress = J9OAB_MIXEDOBJECT_EA(destObject, destOffset, U_64);

		protectIfVolatileBefore(isVolatile, false);
		U_64 result = compareAndExchangeU64Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read a static object field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param clazz The J9Class being written to.
	 * @param srcAddress The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE j9object_t
	inlineStaticReadObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *srcAddress, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticReadObject(vmThread, clazz, srcAddress, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		preStaticReadObject(vmThread, clazz, srcAddress);

		protectIfVolatileBefore(isVolatile, true);
		j9object_t result = staticReadObjectImpl(vmThread, srcAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		/* TODO handle postReadBarrier if RTJ every becomes combo */

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object value into a static.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param srcAddress the address of the static to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineStaticStoreObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *srcAddress, j9object_t value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticStoreObject(vmThread, clazz, srcAddress, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticStoreObject(vmThread, clazz, srcAddress, value, isVolatile);
		} else {
			j9object_t classObject = J9VM_J9CLASS_TO_HEAPCLASS(clazz);

			preStaticStoreObject(vmThread, classObject, srcAddress, value);

			protectIfVolatileBefore(isVolatile, false);
			staticStoreObjectImpl(vmThread, srcAddress, value, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			postStaticStoreObject(vmThread, classObject, srcAddress, value);
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineStaticCompareAndSwapObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndSwapObject(vmThread, clazz, destAddress, compareObject, swapObject));
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndSwapObject(vmThread, clazz, destAddress, compareObject, swapObject));
		} else {
			j9object_t classObject = J9VM_J9CLASS_TO_HEAPCLASS(clazz);

			preStaticReadObject(vmThread, clazz, destAddress);
			preStaticStoreObject(vmThread, classObject, destAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			bool result = staticCompareAndSwapObjectImpl(vmThread, destAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			if (result) {
				postStaticStoreObject(vmThread, classObject, destAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store an object value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the object stored in the static field before the update
	 */
	VMINLINE j9object_t
	inlineStaticCompareAndExchangeObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndExchangeObject(vmThread, clazz, destAddress, compareObject, swapObject);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndExchangeObject(vmThread, clazz, destAddress, compareObject, swapObject);
		} else {
			j9object_t classObject = J9VM_J9CLASS_TO_HEAPCLASS(clazz);

			preStaticReadObject(vmThread, clazz, destAddress);
			preStaticStoreObject(vmThread, classObject, destAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			j9object_t result = staticCompareAndExchangeObjectImpl(vmThread, destAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			if (result == compareObject) {
				postStaticStoreObject(vmThread, classObject, destAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read a static U_32 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 *
	 * @param clazz The J9Class being written to.
	 * @param srcAddress The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE U_32
	inlineStaticReadU32(J9VMThread *vmThread, J9Class *clazz, U_32 *srcAddress, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (U_32)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticReadU32(vmThread, clazz, srcAddress, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, true);
		U_32 result = readU32Impl(vmThread, srcAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_32 value into a static.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param srcAddress the address of the static to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineStaticStoreU32(J9VMThread *vmThread, J9Class *clazz, U_32 *srcAddress, U_32 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticStoreU32(vmThread, clazz, srcAddress, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, false);
		storeU32Impl(vmThread, srcAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_32 value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineStaticCompareAndSwapU32(J9VMThread *vmThread, J9Class *clazz, U_32 *destAddress, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndSwapInt(vmThread, clazz, destAddress, compareValue, swapValue));
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU32Impl(vmThread, destAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_32 value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the static field before the update
	 */
	VMINLINE U_32
	inlineStaticCompareAndExchangeU32(J9VMThread *vmThread, J9Class *clazz, U_32 *destAddress, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndExchangeInt(vmThread, clazz, destAddress, compareValue, swapValue);
#elif defined(J9VM_GC_COMBINATION_SPEC)

		protectIfVolatileBefore(isVolatile, false);
		U_32 result = compareAndExchangeU32Impl(vmThread, destAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read static U_64 field: perform any pre-use barriers, calculate an effective address
	 * and perform the work.
	 * @param clazz The J9Class being written to.
	 * @param srcAddress The offset of the field.
	 * @param isVolatile non-zero if the field is volatile.
	 */
	VMINLINE U_64
	inlineStaticReadU64(J9VMThread *vmThread, J9Class *clazz, U_64 *srcAddress, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticReadU64(vmThread, clazz, srcAddress, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, true);
		U_64 result =  readU64Impl(vmThread, srcAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_64 value into a static.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param srcAddress the address of the static to store value
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	inlineStaticStoreU64(J9VMThread *vmThread, J9Class *clazz, U_64 *srcAddress, U_64 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticStoreU64(vmThread, clazz, srcAddress, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, false);
		storeU64Impl(vmThread, srcAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_64 value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineStaticCompareAndSwapU64(J9VMThread *vmThread, J9Class *clazz, U_64 *destAddress, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndSwapLong(vmThread, clazz, destAddress, compareValue, swapValue));
#elif defined(J9VM_GC_COMBINATION_SPEC) 

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU64Impl(vmThread, destAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_64 value into a static atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param clazz the clazz whose static being stored into
	 * @param destAddress the address of the static to store value
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the static field before the update
	 */
	VMINLINE U_64
	inlineStaticCompareAndExchangeU64(J9VMThread *vmThread, J9Class *clazz, U_64 *destAddress, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_staticCompareAndExchangeLong(vmThread, clazz, destAddress, compareValue, swapValue));
#elif defined(J9VM_GC_COMBINATION_SPEC)

		protectIfVolatileBefore(isVolatile, false);
		U_64 result = compareAndExchangeU64Impl(vmThread, destAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_8 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE I_8
	inlineIndexableObjectReadI8(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (I_8)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadI8(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_8 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_8);

		protectIfVolatileBefore(isVolatile, true);
		I_8 result = readI8Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a I_8 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreI8(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, I_8 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreI8(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, (I_32)value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_8 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_8);

		protectIfVolatileBefore(isVolatile, false);
		storeI8Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an U_8 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE U_8
	inlineIndexableObjectReadU8(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (U_8)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadU8(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_8 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_8);

		protectIfVolatileBefore(isVolatile, true);
		U_8 result = readU8Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_8 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreU8(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, U_8 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreU8(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_8 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_8);

		protectIfVolatileBefore(isVolatile, false);
		storeU8Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_16 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE I_16
	inlineIndexableObjectReadI16(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (I_16)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadI16(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_16 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_16);

		protectIfVolatileBefore(isVolatile, true);
		I_16 result = readI16Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a I_16 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreI16(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, I_16 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreI16(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_16 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_16);

		protectIfVolatileBefore(isVolatile, false);
		storeI16Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an U_16 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE U_16
	inlineIndexableObjectReadU16(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (U_16)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadU16(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_16 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_16);

		protectIfVolatileBefore(isVolatile, true);
		U_16 result = readU16Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_16 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreU16(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, U_16 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreU16(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_16 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_16);

		protectIfVolatileBefore(isVolatile, false);
		storeU16Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_32 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE I_32
	inlineIndexableObjectReadI32(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (I_32)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadI32(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_32 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_32);

		protectIfVolatileBefore(isVolatile, true);
		I_32 result = readI32Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a I_32 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreI32(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, I_32 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreI32(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_32 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_32);

		protectIfVolatileBefore(isVolatile, false);
		storeI32Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an U_32 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE U_32
	inlineIndexableObjectReadU32(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (U_32)vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadU32(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_32);

		protectIfVolatileBefore(isVolatile, true);
		U_32 result = readU32Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_32 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreU32(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, U_32 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreI32(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_32);

		protectIfVolatileBefore(isVolatile, false);
		storeU32Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_32 into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineIndexableObjectCompareAndSwapU32(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) || defined(J9VM_GC_COMBINATION_SPEC) 
		U_32 *actualAddress = J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_32);

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU32Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_32 into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the array before the update
	 */
	VMINLINE U_32
	inlineIndexableObjectCompareAndExchangeU32(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, U_32 compareValue, U_32 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) || defined(J9VM_GC_COMBINATION_SPEC)
		U_32 *actualAddress = J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_32);

		protectIfVolatileBefore(isVolatile, false);
		U_32 result = compareAndExchangeU32Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an I_64 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE I_64
	inlineIndexableObjectReadI64(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadI64(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_64 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_64);

		protectIfVolatileBefore(isVolatile, true);
		I_64 result = readI64Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a I_64 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreI64(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, I_64 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreI64(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		I_64 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, I_64);

		protectIfVolatileBefore(isVolatile, false);
		storeI64Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an U_64 from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE U_64
	inlineIndexableObjectReadU64(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadU64(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_64);

		protectIfVolatileBefore(isVolatile, true);
		U_64 result = readU64Impl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_64 into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreU64(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, U_64 value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreU64(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_64);

		protectIfVolatileBefore(isVolatile, false);
		storeU64Impl(vmThread, actualAddress, value, isVolatile);
		protectIfVolatileAfter(isVolatile, false);
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store a U_64 into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineIndexableObjectCompareAndSwapU64(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) || defined(J9VM_GC_COMBINATION_SPEC) 
		U_64 *actualAddress = J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_64);

		protectIfVolatileBefore(isVolatile, false);
		bool result = compareAndSwapU64Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store a U_64 into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the value stored in the array before the update
	 */
	VMINLINE U_64
	inlineIndexableObjectCompareAndExchangeU64(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, U_64 compareValue, U_64 swapValue, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER) || defined(J9VM_GC_COMBINATION_SPEC)
		U_64 *actualAddress = J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_64);

		protectIfVolatileBefore(isVolatile, false);
		U_64 result = compareAndExchangeU64Impl(vmThread, actualAddress, compareValue, swapValue, isVolatile);
		protectIfVolatileAfter(isVolatile, false);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Read an object from an array.
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being read from
	 * @param srcIndex the index to be read
	 */
	VMINLINE j9object_t
	inlineIndexableObjectReadObject(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableReadObject(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC)  
		fj9object_t *actualAddress = NULL;
		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_32);
		} else {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, UDATA);
		}

		preIndexableObjectReadObject(vmThread, srcArray, actualAddress);

		protectIfVolatileBefore(isVolatile, true);
		j9object_t result = readObjectImpl(vmThread, actualAddress, isVolatile);
		protectIfVolatileAfter(isVolatile, true);

		return result;
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object into an array
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param srcArray the array being stored to
	 * @param srcIndex the index to be stored at
	 * @param value the value being store
	 */
	VMINLINE void
	inlineIndexableObjectStoreObject(J9VMThread *vmThread, j9object_t srcArray, UDATA srcIndex, j9object_t value, bool isVolatile = false)
	{
#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreObject(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_indexableStoreObject(vmThread, (J9IndexableObject *)srcArray, (I_32)srcIndex, value, isVolatile);
		} else {
			fj9object_t *actualAddress = NULL;
			if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
				actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, U_32);
			} else {
				actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, srcArray, srcIndex, UDATA);
			}

			preIndexableObjectStoreObject(vmThread, srcArray, actualAddress, value);

			protectIfVolatileBefore(isVolatile, false);
			storeObjectImpl(vmThread, actualAddress, value, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			postIndexableObjectStoreObject(vmThread, srcArray, actualAddress, value);
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Store an object into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 * 
	 * @return true on success, false otherwise
	 */
	VMINLINE bool
	inlineIndexableObjectCompareAndSwapObject(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
		fj9object_t *actualAddress = NULL;
		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_32);
		} else {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, destArray, destIndex, UDATA);
		}

#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndSwapObject(vmThread, destArray, (J9Object **)actualAddress, compareObject, swapObject));
#elif defined(J9VM_GC_COMBINATION_SPEC) 
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return (1 == vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndSwapObject(vmThread, destArray, (J9Object **)actualAddress, compareObject, swapObject));
		} else {
			preIndexableObjectReadObject(vmThread, destArray, actualAddress);
			preIndexableObjectStoreObject(vmThread, destArray, actualAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			bool result = compareAndSwapObjectImpl(vmThread, actualAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			if (result) {
				postIndexableObjectStoreObject(vmThread, destArray, actualAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/**
	 * Conditionally store an object into an array atomically
	 *
	 * This function performs all of the necessary barriers and calls OOL when it can not handle
	 * the barrier itself.
	 *
	 * @param destArray the array being stored to
	 * @param destIndex the index to be stored at
	 * @param compareObject the object to compare with
	 * @param swapObject the value to be swapped in
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 *
	 * @return the object stored in the array before the update
	 */
	VMINLINE j9object_t
	inlineIndexableObjectCompareAndExchangeObject(J9VMThread *vmThread, j9object_t destArray, UDATA destIndex, j9object_t compareObject, j9object_t swapObject, bool isVolatile = false)
	{
		fj9object_t *actualAddress = NULL;
		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, destArray, destIndex, U_32);
		} else {
			actualAddress = (fj9object_t*)J9JAVAARRAY_EA(vmThread, destArray, destIndex, UDATA);
		}

#if defined(J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER)
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndExchangeObject(vmThread, destArray, (J9Object **)actualAddress, compareObject, swapObject);
#elif defined(J9VM_GC_COMBINATION_SPEC)
		if (j9gc_modron_wrtbar_always == _writeBarrierType) {
			return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_compareAndExchangeObject(vmThread, destArray, (J9Object **)actualAddress, compareObject, swapObject);
		} else {
			preIndexableObjectReadObject(vmThread, destArray, actualAddress);
			preIndexableObjectStoreObject(vmThread, destArray, actualAddress, swapObject);

			protectIfVolatileBefore(isVolatile, false);
			fj9object_t resultToken = compareAndExchangeObjectImpl(vmThread, actualAddress, compareObject, swapObject, isVolatile);
			protectIfVolatileAfter(isVolatile, false);

			j9object_t result = internalConvertPointerFromToken(resultToken);

			if (result == compareObject) {
				postIndexableObjectStoreObject(vmThread, destArray, actualAddress, swapObject);
			}
			return result;
		}
#else /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
#error unsupported barrier
#endif /* J9VM_GC_ALWAYS_CALL_OBJECT_ACCESS_BARRIER */
	}

	/* Return an j9object_t that can be stored in the constantpool.
	 *
	 * Not all collectors scan the constantpool on every GC and therefore for
	 * these collectors the objects must be in tenure space.
	 *
	 * Note, the stack must be walkable as a GC may occur during this function.
	 * 
	 * Note, this doesn't handle arrays.
	 *
	 * @param vmThread The current vmThread
	 * @param toConvert The object to convert to a constantpool allowed form.
	 *
	 * @return an object that can be put in the constantpool or null if OOM.
	 */
	static VMINLINE j9object_t
	asConstantPoolObject(J9VMThread *vmThread, j9object_t toConvert, UDATA allocateFlags)
	{
		return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_asConstantPoolObject(vmThread, toConvert, allocateFlags);
	}

protected:

	/**
	 * Called before object references are stored into mixed objects to perform an prebarrier work.
	 *
	 * @param object - the object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	preMixedObjectStoreObject(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
		internalPreStoreObject(vmThread, object, destAddress, value);
	}

	/**
	 * Called before object references are stored into arrays to perform an prebarrier work.
	 *
	 * @param object - the object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	preIndexableObjectStoreObject(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
		internalPreStoreObject(vmThread, object, destAddress, value);
	}

	/**
	 * Called before object references are stored into class statics to perform an prebarrier work.
	 *
	 * @param dstClassObject - the class object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	preStaticStoreObject(J9VMThread *vmThread, j9object_t dstClassObject, j9object_t *destAddress, j9object_t value)
	{
		internalStaticPreStoreObject(vmThread, dstClassObject, destAddress, value);
	}

	/**
	 * Called after object references are stored into mixed objects to perform an postbarrier work.
	 *
	 * @param object - the object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	postMixedObjectStoreObject(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
		internalPostStoreObject(vmThread, object, value);
	}

	/**
	 * Called after object references are stored into arrays to perform an postbarrier work.
	 *
	 * @param object - the object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	postIndexableObjectStoreObject(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
		internalPostStoreObject(vmThread, object, value);
	}

	/**
	 * Called after object references are stored into class statics to perform an postbarrier work.
	 *
	 * @param object - the class object being stored into
	 * @param destAddress - the address being stored into
	 * @param value - the value being stored
	 *
	 */
	VMINLINE void
	postStaticStoreObject(J9VMThread *vmThread, j9object_t object, j9object_t *destAddress, j9object_t value)
	{
		internalPostStoreObject(vmThread, object, value);
	}

	VMINLINE void
	postBatchStoreObject(J9VMThread *vmThread, j9object_t object)
	{
		internalPostBatchStoreObject(vmThread, object);
	}
	
	/**
	 * Perform the preRead barrier for a reference slot within a mixed heap object
	 *
	 * @param object this is the heap object being read from
	 * @param sreAddress the address of the slot being read
	 */
	VMINLINE void
	preMixedObjectReadObject(J9VMThread *vmThread, j9object_t object, fj9object_t *srcAddress)
	{
		internalPreReadObject(vmThread, object, srcAddress);
	}
	
	/**
	 * Perform the preRead barrier for a reference slot within an indexable heap object
	 *
	 * @param object this is the heap object being read from
	 * @param sreAddress the address of the slot being read
	 */	
	VMINLINE void
	preIndexableObjectReadObject(J9VMThread *vmThread, j9object_t object, fj9object_t *srcAddress)
	{
		internalPreReadObject(vmThread, object, srcAddress);
	}
	
	/**
	 * Read a non-object address (pointer to internal VM data) from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void *
	readAddressImpl(J9VMThread *vmThread, void **srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a I_16 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE I_16
	readI16Impl(J9VMThread *vmThread, I_16 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a U_16 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE U_16
	readU16Impl(J9VMThread *vmThread, U_16 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a I_32 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE I_32
	readI32Impl(J9VMThread *vmThread, I_32 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a U_32 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE U_32
	readU32Impl(J9VMThread *vmThread, U_32 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a I_64 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE I_64
	readI64Impl(J9VMThread *vmThread, I_64 *srcAddress, bool isVolatile)
	{
#if !defined(J9VM_ENV_DATA64)
		if (isVolatile) {
			return longVolatileRead(vmThread, (U_64 *)srcAddress);
		}
#endif /* J9VM_ENV_DATA64 */
		return *srcAddress;
	}

	/**
	 * Read a U_64 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE U_64
	readU64Impl(J9VMThread *vmThread, U_64 *srcAddress, bool isVolatile)
	{
#if !defined(J9VM_ENV_DATA64)
		if (isVolatile) {
			return longVolatileRead(vmThread, srcAddress);
		}
#endif /* J9VM_ENV_DATA64 */
		return *srcAddress;
	}

	/**
	 * Read a I_8 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE I_8
	readI8Impl(J9VMThread *vmThread, I_8 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read a U_8 from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE U_8
	readU8Impl(J9VMThread *vmThread, U_8 *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Read an object pointer from an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE mm_j9object_t
	readObjectImpl(J9VMThread *vmThread, fj9object_t *srcAddress, bool isVolatile)
	{
		mm_j9object_t result = NULL;
		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			result = internalConvertPointerFromToken((fj9object_t)(UDATA)*(U_32*)srcAddress);
		} else {
			result = (mm_j9object_t)*(UDATA*)srcAddress;
		}
		return result;
	}
	
	/**
	 * Called before reading a reference class static slot
	 *
	 * @param object - the class object being read from
	 * @param srcAddress - the address the slot read from
	 */	
	VMINLINE void
	preStaticReadObject(J9VMThread *vmThread, J9Class *clazz, j9object_t *srcAddress)
	{
		if (j9gc_modron_readbar_none != _readBarrierType) {
			vmThread->javaVM->memoryManagerFunctions->J9ReadBarrierClass(vmThread, srcAddress);
		}
	}
	

	/**
	 * Read a static object field.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param srcAddress the address of the field to be read
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE mm_j9object_t
	staticReadObjectImpl(J9VMThread *vmThread, j9object_t *srcAddress, bool isVolatile)
	{
		return *srcAddress;
	}

	/**
	 * Store a non-object address into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeAddressImpl(J9VMThread *vmThread, void **destAddress, void *value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store a I_16 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeI16Impl(J9VMThread *vmThread, I_16 *destAddress, I_16 value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store a U_16 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeU16Impl(J9VMThread *vmThread, U_16 *destAddress, U_16 value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store a I_32 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeI32Impl(J9VMThread *vmThread, I_32 *destAddress, I_32 value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store a U_32 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeU32Impl(J9VMThread *vmThread, U_32 *destAddress, U_32 value, bool isVolatile)
	{
		*destAddress = value;
	}

	VMINLINE bool
	compareAndSwapU32Impl(J9VMThread *vmThread, U_32 *destAddress, U_32 compareValue, U_32 swapValue, bool isVolatile)
	{
		return (compareValue == VM_AtomicSupport::lockCompareExchangeU32(destAddress, compareValue, swapValue));
	}

	VMINLINE U_32
	compareAndExchangeU32Impl(J9VMThread *vmThread, U_32 *destAddress, U_32 compareValue, U_32 swapValue, bool isVolatile)
	{
		return VM_AtomicSupport::lockCompareExchangeU32(destAddress, compareValue, swapValue);
	}

	/**
	 * Store an I_64 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeI64Impl(J9VMThread *vmThread, I_64 *destAddress, I_64 value, bool isVolatile)
	{
#if !defined(J9VM_ENV_DATA64)
		if (isVolatile) {
			longVolatileWrite(vmThread, (U_64 *)destAddress, (U_64 *)&value);
			return;
		}
#endif /* J9VM_ENV_DATA64 */
		*destAddress = value;
	}

	/**
	 * Store an U_64 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeU64Impl(J9VMThread *vmThread, U_64 *destAddress, U_64 value, bool isVolatile)
	{
#if !defined(J9VM_ENV_DATA64)
		if (isVolatile) {
			longVolatileWrite(vmThread, destAddress, &value);
			return;
		}
#endif /* J9VM_ENV_DATA64 */
		*destAddress = value;
	}

	VMINLINE bool
	compareAndSwapU64Impl(J9VMThread *vmThread, U_64 *destAddress, U_64 compareValue, U_64 swapValue, bool isVolatile)
	{
		return (compareValue == VM_AtomicSupport::lockCompareExchangeU64(destAddress, compareValue, swapValue));
	}

	VMINLINE U_64
	compareAndExchangeU64Impl(J9VMThread *vmThread, U_64 *destAddress, U_64 compareValue, U_64 swapValue, bool isVolatile)
	{
		return VM_AtomicSupport::lockCompareExchangeU64(destAddress, compareValue, swapValue);
	}

	/**
	 * Store a I_8 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeI8Impl(J9VMThread *vmThread, I_8 *destAddress, I_8 value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store a U_8 into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 * unless the value is stored in a non-native format (e.g. compressed object pointers).
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeU8Impl(J9VMThread *vmThread, U_8 *destAddress, U_8 value, bool isVolatile)
	{
		*destAddress = value;
	}

	/**
	 * Store an object pointer into an object.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	storeObjectImpl(J9VMThread *vmThread, fj9object_t *destAddress, mm_j9object_t value, bool isVolatile)
	{
		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			*(U_32*)destAddress = (U_32)internalConvertTokenFromPointer(value);
		} else {
			*(UDATA*)destAddress = (UDATA)value;
		}
	}

	VMINLINE bool
	compareAndSwapObjectImpl(J9VMThread *vmThread, fj9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile)
	{
		fj9object_t compareValue = internalConvertTokenFromPointer(compareObject);
		fj9object_t swapValue = internalConvertTokenFromPointer(swapObject);
		bool result = true;

		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			result = ((U_32)(UDATA)compareValue == VM_AtomicSupport::lockCompareExchangeU32((U_32 *)destAddress, (U_32)(UDATA)compareValue, (U_32)(UDATA)swapValue));
		} else {
			result = ((UDATA)compareValue == VM_AtomicSupport::lockCompareExchange((UDATA *)destAddress, (UDATA)compareValue, (UDATA)swapValue));
		}

		return result;
	}

	VMINLINE fj9object_t
	compareAndExchangeObjectImpl(J9VMThread *vmThread, fj9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile)
	{
		fj9object_t compareValue = internalConvertTokenFromPointer(compareObject);
		fj9object_t swapValue = internalConvertTokenFromPointer(swapObject);
		fj9object_t result = (fj9object_t)NULL;

		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread)) {
			result = (fj9object_t)VM_AtomicSupport::lockCompareExchangeU32((U_32 *)destAddress, (U_32)(UDATA)compareValue, (U_32)(UDATA)swapValue);
		} else {
			result = (fj9object_t)VM_AtomicSupport::lockCompareExchange((UDATA *)destAddress, (UDATA)compareValue, (UDATA)swapValue);
		}

		return result;
	}

	/**
	 * Store a static field.
	 * This function is only concerned with moving the actual data. Do not re-implement
	 *
	 * @param destAddress the address of the field to be read
	 * @param value the value to be stored
	 * @param isVolatile non-zero if the field is volatile, zero otherwise
	 */
	VMINLINE void
	staticStoreObjectImpl(J9VMThread *vmThread, j9object_t *destAddress, mm_j9object_t value, bool isVolatile)
	{
		*destAddress = value;
	}

	VMINLINE bool
	staticCompareAndSwapObjectImpl(J9VMThread *vmThread, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile)
	{
		return ((UDATA)compareObject == VM_AtomicSupport::lockCompareExchange((UDATA *)destAddress, (UDATA)compareObject, (UDATA)swapObject));
	}

	VMINLINE j9object_t
	staticCompareAndExchangeObjectImpl(J9VMThread *vmThread, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject, bool isVolatile)
	{
		return (j9object_t)VM_AtomicSupport::lockCompareExchange((UDATA *)destAddress, (UDATA)compareObject, (UDATA)swapObject);
	}

	/**
	 * Call before a read or write of a possibly volatile field.
	 *
	 * @note This must be used in tandem with protectIfVolatileAfter()
	 * @param isVolatile true if the field is volatile, false otherwise
	 * @param isRead true if the field is being read, false if it is being written
	 */
	VMINLINE static void
	protectIfVolatileBefore(bool isVolatile, bool isRead)
	{
		if (isVolatile) {
			if (!isRead) {
				VM_AtomicSupport::writeBarrier();
			}
		}
	}

	/**
	 * Call after a read or write of a possibly volatile field.
	 *
	 * @note This must be used in tandem with protectIfVolatileBefore()
	 * @param isVolatile true if the field is volatile, false otherwise
	 * @param isRead true if the field is being read, false if it is being written
	 */
	VMINLINE static void
	protectIfVolatileAfter(bool isVolatile, bool isRead)
	{
		if (isVolatile) {
			if (isRead) {
				VM_AtomicSupport::readBarrier();
			} else {
				VM_AtomicSupport::readWriteBarrier();
			}
		}
	}

	/**
	 * Call to convert an object pointer to an object token
	 *
	 * @param pointer the object pointer to convert
	 * @return the object token
	 */
	VMINLINE fj9object_t
	internalConvertTokenFromPointer(j9object_t pointer)
	{
#if defined (OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			return (fj9object_t)((UDATA)pointer >> _compressedPointersShift);
		} else
#endif /* OMR_GC_COMPRESSED_POINTERS */
		{
			return (fj9object_t)(UDATA)pointer;
		}
	}

	/**
	 * Call to convert an object token to an object pointer
	 *
	 * @param token the object token to convert
	 * @return the object pointer
	 */
	VMINLINE j9object_t
	internalConvertPointerFromToken(fj9object_t token)
	{
#if defined (OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			return (mm_j9object_t)((UDATA)token << _compressedPointersShift);
		} else
#endif /* OMR_GC_COMPRESSED_POINTERS */
		{
			return (mm_j9object_t)(UDATA)token;
		}
	}

private:

	/**
	 * Perform the preStore barrier
	 *
	 * Currently pre store barriers are only required for WRT. Check the barrier type
	 * and forward accordingly
	 *
	 * @param object this is the class object being stored into
	 * @param destAddress the address being stored at
	 * @param value the value being stored
	 *
	 */
	VMINLINE void
	internalPreStoreObject(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
		if ((j9gc_modron_wrtbar_satb == _writeBarrierType) ||
				(j9gc_modron_wrtbar_satb_and_oldcheck == _writeBarrierType)) {
			internalPreStoreObjectSATB(vmThread, object, destAddress, value);
		}
	}

	/**
	 * Perform the static preStore barrier
	 *
	 * Currently pre store barriers are only required for WRT. Check the barrier type
	 * and forward accordingly
	 *
	 * @param dstClassObject this is the class object being stored into
	 * @param destAddress the address being stored at
	 * @param value the value being stored
	 *
	 */
	VMINLINE void
	internalStaticPreStoreObject(J9VMThread *vmThread, j9object_t dstClassObject, j9object_t *destAddress, j9object_t value)
	{
		if ((j9gc_modron_wrtbar_satb == _writeBarrierType) ||
				(j9gc_modron_wrtbar_satb_and_oldcheck == _writeBarrierType)) {
			internalStaticPreStoreObjectSATB(vmThread, dstClassObject, destAddress, value);
		}
	}

	/**
	 * Perform the preStore barrier for WRT
	 *
	 * @param object this is the class object being stored into
	 * @param destAddress the address being stored at
	 * @param value the value being stored
	 *
	 */
	VMINLINE void
	internalPreStoreObjectSATB(J9VMThread *vmThread, j9object_t object, fj9object_t *destAddress, j9object_t value)
	{
#if defined(J9VM_GC_REALTIME)
		MM_GCRememberedSetFragment *fragment =  &vmThread->sATBBarrierRememberedSetFragment;
		MM_GCRememberedSet *parent = fragment->fragmentParent;
		/* Check if the barrier is enabled.  No work if barrier is not enabled */
		if (0 != parent->globalFragmentIndex) {
			/* if the double barrier is enabled call OOL */
			if (0 == fragment->localFragmentIndex) {
				vmThread->javaVM->memoryManagerFunctions->J9WriteBarrierPre(vmThread, object, destAddress, value);
			} else {
				j9object_t oldObject = readObjectImpl(vmThread, destAddress, false);
				if (NULL != oldObject) {
					if (!isMarked(vmThread, oldObject)) {
						vmThread->javaVM->memoryManagerFunctions->J9WriteBarrierPre(vmThread, object, destAddress, value);
					}
				}
			}
		}
#endif /* J9VM_GC_REALTIME */
	}

	/**
	 * Perform the static preStore barrier for WRT
	 *
	 * @param dstClassObject this is the class object being stored into
	 * @param destAddress the address being stored at
	 * @param value the value being stored
	 *
	 */
	VMINLINE void
	internalStaticPreStoreObjectSATB(J9VMThread *vmThread, j9object_t dstClassObject, j9object_t *destAddress, j9object_t value)
	{
#if defined(J9VM_GC_REALTIME)
		MM_GCRememberedSetFragment *fragment =  &vmThread->sATBBarrierRememberedSetFragment;
		MM_GCRememberedSet *parent = fragment->fragmentParent;
		/* Check if the barrier is enabled.  No work if barrier is not enabled */
		if (0 != parent->globalFragmentIndex) {
			/* if the double barrier is enabled call OOL */
			if (0 == fragment->localFragmentIndex) {
				vmThread->javaVM->memoryManagerFunctions->J9WriteBarrierPreClass(vmThread, dstClassObject, destAddress, value);
			} else {
				j9object_t oldObject = *destAddress;
				if (NULL != oldObject) {
					if (!isMarked(vmThread, oldObject)) {
						vmThread->javaVM->memoryManagerFunctions->J9WriteBarrierPreClass(vmThread, dstClassObject, destAddress, value);
					}
				}
			}
		}
#endif /* J9VM_GC_REALTIME */
	}

#if defined(J9VM_GC_REALTIME)
	/**
	 * Check to see if the object is marked
	 *
	 * @NOTE only for SATB Concurrent Mark
	 *
	 * @param object to check
	 * @return true if the object was marked, false otherwise
	 */
	VMINLINE bool
	isMarked(J9VMThread *vmThread, j9object_t object)
	{
		J9JavaVM *const javaVM = vmThread->javaVM;

		if (J9_GC_POLICY_METRONOME == javaVM->gcPolicy) {
			UDATA heapMapBits = 0;
			UDATA heapDelta = (UDATA)object - javaVM->realtimeHeapMapBasePageRounded;
			UDATA slotIndex = heapDelta >> J9VMGC_SIZECLASSES_LOG_SMALLEST;
			UDATA bitIndex = slotIndex & J9_GC_MARK_MAP_UDATA_MASK;
			UDATA bitMask = ((UDATA)1) << bitIndex;
			slotIndex = slotIndex >> J9_GC_MARK_MAP_LOG_SIZEOF_UDATA;

			UDATA *heapMapBitsBase = javaVM->realtimeHeapMapBits;
			heapMapBits = heapMapBitsBase[slotIndex];
			heapMapBits &= bitMask;

			return 0 != heapMapBits;
		} else {
			/* TODO: Inline for SATB Standard Configuration */
			return javaVM->memoryManagerFunctions->j9gc_ext_is_marked(vmThread->javaVM, object);
		}
	}
#endif /* J9VM_GC_REALTIME */


	/**
	 * Call after storing an object reference
	 *
	 * @param object the object being stored into
	 * @param value the object reference being stored
	 */
	VMINLINE void
	internalPostStoreObject(J9VMThread *vmThread, j9object_t object, j9object_t value)
	{
		switch(_writeBarrierType) {
		case j9gc_modron_wrtbar_cardmark_and_oldcheck:
			internalPostObjectStoreCardTableAndGenerational(vmThread, object, value);
			break;
		case j9gc_modron_wrtbar_oldcheck:
			internalPostObjectStoreGenerational(vmThread, object, value);
			break;
		case j9gc_modron_wrtbar_cardmark_incremental:
			internalPostObjectStoreCardTableIncremental(vmThread, object, value);
			break;
		case j9gc_modron_wrtbar_cardmark:
			internalPostObjectStoreCardTable(vmThread, object, value);
			break;
		case j9gc_modron_wrtbar_none:
		case j9gc_modron_wrtbar_satb:
		case j9gc_modron_wrtbar_satb_and_oldcheck:
			//TODO SATB change to handle gencon, decide where to do it in pre/post store
			break;
		default:
			/* Should assert as all real types are handled.  Should never get here
			 * with always or illegal
			 */
			break;
		}
	}

	VMINLINE void
	internalPostBatchStoreObject(J9VMThread *vmThread, j9object_t object)
	{
		switch(_writeBarrierType) {
		case j9gc_modron_wrtbar_cardmark_and_oldcheck:
			internalPostBatchStoreObjectCardTableAndGenerational(vmThread, object);
			break;
		case j9gc_modron_wrtbar_oldcheck:
			internalPostBatchStoreObjectGenerational(vmThread, object);
			break;
		case j9gc_modron_wrtbar_cardmark_incremental:
			internalPostBatchStoreObjectCardTableIncremental(vmThread, object);
			break;
		case j9gc_modron_wrtbar_cardmark:
			internalPostBatchStoreObjectCardTable(vmThread, object);
			break;
		case j9gc_modron_wrtbar_none:
		case j9gc_modron_wrtbar_satb:
		case j9gc_modron_wrtbar_satb_and_oldcheck:
			break;
		default:
			/* Should assert as all real types are handled.  Should never get here
			 * with always or illegal
			 */
			break;
		}
	}
	
	/**
	 * Perform the preRead barrier for heap object reference slot
	 * It's common API for both mixed and indexable objects
	 *
	 * @param object this is the heap object being read from
	 * @param sreAddress the address of the slot being read
	 */
	VMINLINE void
	internalPreReadObject(J9VMThread *vmThread, j9object_t object, fj9object_t *srcAddress)
	{
		if (j9gc_modron_readbar_none != _readBarrierType) {
			vmThread->javaVM->memoryManagerFunctions->J9ReadBarrier(vmThread, srcAddress);
		}
	}

	/**
	 * Dirty the appropriate card for card table barriers
	 *
	 * @param objectDelta the delta from the object to heap start
	 *
	 */
	static VMINLINE void
	dirtyCard(J9VMThread *vmThread, UDATA objectDelta)
	{
		/* calculate the delta with in the card table */
		UDATA shiftedDelta = objectDelta >> CARD_SIZE_SHIFT;

		/* find the card and dirty it */
		U_8* card = (U_8*)vmThread->activeCardTableBase + shiftedDelta;
		*card = (U_8)CARD_DIRTY;
	}

	/**
	 * Add the object to the remembered set for generational barriers
	 *
	 * @param object the object to remember
	 *
	 */
	static VMINLINE void
	rememberObject(J9VMThread *vmThread, j9object_t object)
	{
#if defined (J9VM_GC_GENERATIONAL)
		if (atomicSetRemembered(vmThread, object)) {
			J9VMGCSublistFragment *fragment = &vmThread->gcRememberedSet;
			do {
				UDATA *slot = fragment->fragmentCurrent;
				UDATA *next = slot + 1;
				if (next > fragment->fragmentTop) {
					if (0 != vmThread->javaVM->memoryManagerFunctions->allocateMemoryForSublistFragment(vmThread->omrVMThread,(J9VMGC_SublistFragment*)fragment)) {
						break;
					}
				} else {
					fragment->fragmentCurrent = next;
					fragment->count += 1;
					*slot = (UDATA)object;
					break;
				}
			} while (true);
		}
#endif /* J9VM_GC_GENERATIONAL */
	}

#if defined (J9VM_GC_GENERATIONAL)
	/**
	 * Set the remembered but for the object pointer atomically
	 *
	 * @param object the object to set the remembered bit on
	 * @return true if the bit was set, false otherwise
	 */
	static VMINLINE bool
	atomicSetRemembered(J9VMThread *currentThread, j9object_t objectPtr)
	{
		bool result = true;

		if (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(currentThread)) {
			volatile U_32* flagsPtr = &(((J9ObjectCompressed*)objectPtr)->clazz);
			U_32 oldFlags = 0;
			U_32 newFlags = 0;
			do {
				oldFlags = *flagsPtr;
				if((oldFlags & J9_OBJECT_HEADER_REMEMBERED_MASK_FOR_TEST) >= J9_OBJECT_HEADER_REMEMBERED_BITS_TO_SET) {
					/* Remembered state in age was set by somebody else */
					result = false;
					break;
				}
				newFlags = (oldFlags & ~J9_OBJECT_HEADER_REMEMBERED_MASK_FOR_CLEAR) | J9_OBJECT_HEADER_REMEMBERED_BITS_TO_SET;
			} while (oldFlags != VM_AtomicSupport::lockCompareExchangeU32(flagsPtr, oldFlags, newFlags));
		} else {
			volatile UDATA* flagsPtr = &(((J9ObjectFull*)objectPtr)->clazz);
			UDATA oldFlags = 0;
			UDATA newFlags = 0;
			do {
				oldFlags = *flagsPtr;
				if((oldFlags & J9_OBJECT_HEADER_REMEMBERED_MASK_FOR_TEST) >= J9_OBJECT_HEADER_REMEMBERED_BITS_TO_SET) {
					/* Remembered state in age was set by somebody else */
					result = false;
					break;
				}
				newFlags = (oldFlags & ~J9_OBJECT_HEADER_REMEMBERED_MASK_FOR_CLEAR) | J9_OBJECT_HEADER_REMEMBERED_BITS_TO_SET;
			} while (oldFlags != VM_AtomicSupport::lockCompareExchange(flagsPtr, oldFlags, newFlags));
		}

		return result;
	}
#endif /* J9VM_GC_GENERATIONAL */


};

#endif /* OBJECTACCESSBARRIERAPI_HPP_ */

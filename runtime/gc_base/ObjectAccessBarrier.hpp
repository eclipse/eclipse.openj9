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

#if !defined(OBJECTACCESSBARRIER_HPP_)
#define OBJECTACCESSBARRIER_HPP_

#include "j9.h"
#include "j9cfg.h"
#include "modron.h"

#include <string.h>

#include "AtomicOperations.hpp"
#include "GCExtensions.hpp"
#include "Heap.hpp"
#include "ModronAssertions.h"
#include "ObjectModel.hpp"

/* These macros are temporary during the migration to the new object/indexable
 * access barrier. Once the 'real' versions can be included these will be removed
 */
#define J9OAB_MIXEDOBJECT_EA(object, offset, type) (type *)(((U_8 *)(object)) + offset)

class MM_ObjectAccessBarrier : public MM_BaseVirtual
{
	/* data members */
private:
protected:
	MM_GCExtensions *_extensions; 
	MM_Heap *_heap;
#if defined(OMR_GC_COMPRESSED_POINTERS)
#if defined(OMR_GC_FULL_POINTERS)
	bool _compressObjectReferences;
#endif /* defined(OMR_GC_FULL_POINTERS) */
	UDATA _compressedPointersShift; /**< the number of bits to shift by when converting between the compressed pointers heap and real heap */
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
	UDATA _referenceLinkOffset; /** Offset within java/lang/ref/Reference of the reference link field */
	UDATA _ownableSynchronizerLinkOffset; /** Offset within java/util/concurrent/locks/AbstractOwnableSynchronizer of the ownable synchronizer link field */
public:

	/* member function */
private:
	/**
	 * Find the finalize link field in the specified object.
	 * @param object[in] the object
	 * @param clazz[in] the class to read the finalizeLinkOffset from
	 * @return a pointer to the finalize link field in the object
	 */
	MMINLINE fj9object_t* getFinalizeLinkAddress(j9object_t object, J9Class *clazz)
	{
		UDATA fieldOffset = clazz->finalizeLinkOffset;
		if (0 == fieldOffset) {
			return NULL;
		}
		return (fj9object_t*)((UDATA)object + fieldOffset);
	}

protected:
	virtual bool initialize(MM_EnvironmentBase *env);
	virtual void tearDown(MM_EnvironmentBase *env);

	/**
	 * Find the finalize link field in the specified object.
	 * @param object[in] the object
	 * @return a pointer to the finalize link field in the object
	 */
	MMINLINE fj9object_t* getFinalizeLinkAddress(j9object_t object)
	{
		J9Class *clazz = J9GC_J9OBJECT_CLAZZ(object, this);
		return getFinalizeLinkAddress(object, clazz);
	}

	/**
	 * Effective address of array slot with a given slot index and slot size
	 * @param array the array for which the calculation is performed
	 * @param index the index of the slot for which address is returned
	 * @param elementSize size of the slot in the array
	 * @return address of the slot with the given index
	 */
	MMINLINE void *
	indexableEffectiveAddress(J9VMThread *vmThread, J9IndexableObject *array, I_32 index, UDATA elementSize)
	{
		MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vmThread);

		/* hybrid arraylet */
		GC_ArrayletObjectModel::ArrayLayout layout = extensions->indexableObjectModel.getArrayLayout(array);
		if (GC_ArrayletObjectModel::InlineContiguous == layout)	{
			UDATA data = (UDATA)extensions->indexableObjectModel.getDataPointerForContiguous(array);
			return (void *)(data + (elementSize * (UDATA)index));
		}

		/* discontiguous arraylet */
		fj9object_t *arrayoidPointer = extensions->indexableObjectModel.getArrayoidPointer(array);
		U_32 slotsPerArrayletLeaf = (U_32)(J9VMTHREAD_JAVAVM(vmThread)->arrayletLeafSize / elementSize);
		U_32 arrayletIndex = (U_32)index / slotsPerArrayletLeaf;
		U_32 arrayletOffset = (U_32)index % slotsPerArrayletLeaf;
		UDATA arrayletLeafBase = 0;
		fj9object_t *arrayletLeafSlot = GC_SlotObject::addToSlotAddress(arrayoidPointer, arrayletIndex, compressObjectReferences());
		if (compressObjectReferences()) {
			arrayletLeafBase = (UDATA)convertPointerFromToken(*(U_32*)arrayletLeafSlot);
		} else {
			arrayletLeafBase = *(UDATA*)arrayletLeafSlot;
		}
		return (void *)(arrayletLeafBase + (elementSize * (UDATA)arrayletOffset));
	}
	
	virtual mm_j9object_t readObjectImpl(J9VMThread *vmThread, mm_j9object_t srcObject, fj9object_t *srcAddress, bool isVolatile=false);
	virtual mm_j9object_t staticReadObjectImpl(J9VMThread *vmThread, J9Class *clazz, j9object_t *srcAddress, bool isVolatile=false);
	virtual mm_j9object_t readObjectFromInternalVMSlotImpl(J9VMThread *vmThread, j9object_t *srcAddress, bool isVolatile=false);

	virtual void *readAddressImpl(J9VMThread *vmThread, mm_j9object_t srcObject, void **srcAddress, bool isVolatile=false);
	virtual U_8 readU8Impl(J9VMThread *vmThread, mm_j9object_t srcObject, U_8 *srcAddress, bool isVolatile=false);
	virtual I_8 readI8Impl(J9VMThread *vmThread, mm_j9object_t srcObject, I_8 *srcAddress, bool isVolatile=false);
	virtual U_16 readU16Impl(J9VMThread *vmThread, mm_j9object_t srcObject, U_16 *srcAddress, bool isVolatile=false);
	virtual I_16 readI16Impl(J9VMThread *vmThread, mm_j9object_t srcObject, I_16 *srcAddress, bool isVolatile=false);
	virtual U_32 readU32Impl(J9VMThread *vmThread, mm_j9object_t srcObject, U_32 *srcAddress, bool isVolatile=false);
	virtual I_32 readI32Impl(J9VMThread *vmThread, mm_j9object_t srcObject, I_32 *srcAddress, bool isVolatile=false);
	virtual U_64 readU64Impl(J9VMThread *vmThread, mm_j9object_t srcObject, U_64 *srcAddress, bool isVolatile=false);
	virtual I_64 readI64Impl(J9VMThread *vmThread, mm_j9object_t srcObject, I_64 *srcAddress, bool isVolatile=false);

	virtual void storeObjectImpl(J9VMThread *vmThread, mm_j9object_t destObject, fj9object_t *destAddress, mm_j9object_t value, bool isVolatile=false);
	virtual void staticStoreObjectImpl(J9VMThread *vmThread, J9Class* clazz, j9object_t *destAddress, mm_j9object_t value, bool isVolatile=false);
	virtual void storeObjectToInternalVMSlotImpl(J9VMThread *vmThread, j9object_t *destAddress, mm_j9object_t value, bool isVolatile=false);

	virtual void storeAddressImpl(J9VMThread *vmThread, mm_j9object_t destObject, void **destAddress, void *value, bool isVolatile=false);
	virtual void storeU8Impl(J9VMThread *vmThread, mm_j9object_t destObject, U_8 *destAddress, U_8 value, bool isVolatile=false);
	virtual void storeI8Impl(J9VMThread *vmThread, mm_j9object_t destObject, I_8 *destAddress, I_8 value, bool isVolatile=false);
	virtual void storeU16Impl(J9VMThread *vmThread, mm_j9object_t destObject, U_16 *destAddress, U_16 value, bool isVolatile=false);
	virtual void storeI16Impl(J9VMThread *vmThread, mm_j9object_t destObject, I_16 *destAddress, I_16 value, bool isVolatile=false);
	virtual void storeU32Impl(J9VMThread *vmThread, mm_j9object_t destObject, U_32 *destAddress, U_32 value, bool isVolatile=false);
	virtual void storeI32Impl(J9VMThread *vmThread, mm_j9object_t destObject, I_32 *destAddress, I_32 value, bool isVolatile=false);
	virtual void storeU64Impl(J9VMThread *vmThread, mm_j9object_t destObject, U_64 *destAddress, U_64 value, bool isVolatile=false);
	virtual void storeI64Impl(J9VMThread *vmThread, mm_j9object_t destObject, I_64 *destAddress, I_64 value, bool isVolatile=false);

	void protectIfVolatileBefore(J9VMThread *vmThread, bool isVolatile, bool isRead, bool isWide);
	void protectIfVolatileAfter(J9VMThread *vmThread, bool isVolatile, bool isRead, bool isWide);
	void printNativeMethod(J9VMThread* vmThread);

public:
	virtual void kill(MM_EnvironmentBase *env);

	virtual J9Object *mixedObjectReadObject(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);
	virtual void *mixedObjectReadAddress(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);
	virtual U_32 mixedObjectReadU32(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);
	virtual I_32 mixedObjectReadI32(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);
	virtual U_64 mixedObjectReadU64(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);
	virtual I_64 mixedObjectReadI64(J9VMThread *vmThread, J9Object *srcObject, UDATA srcOffset, bool isVolatile=false);

	virtual void mixedObjectStoreObject(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, J9Object *value, bool isVolatile=false);
	virtual void mixedObjectStoreAddress(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, void *value, bool isVolatile=false);
	virtual void mixedObjectStoreU32(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, U_32 value, bool isVolatile=false);
	virtual void mixedObjectStoreI32(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, I_32 value, bool isVolatile=false);
	virtual void mixedObjectStoreU64(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, U_64 value, bool isVolatile=false);
	virtual void mixedObjectStoreI64(J9VMThread *vmThread, J9Object *destObject, UDATA destOffset, I_64 value, bool isVolatile=false);

	virtual J9Object *indexableReadObject(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual void *indexableReadAddress(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual U_8 indexableReadU8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual I_8 indexableReadI8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual U_16 indexableReadU16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual I_16 indexableReadI16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual U_32 indexableReadU32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual I_32 indexableReadI32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual U_64 indexableReadU64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);
	virtual I_64 indexableReadI64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 srcIndex, bool isVolatile=false);

	virtual void indexableStoreObject(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, J9Object *value, bool isVolatile=false);
	virtual void indexableStoreAddress(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, void *value, bool isVolatile=false);
	virtual void indexableStoreU8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, U_8 value, bool isVolatile=false);
	virtual void indexableStoreI8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_8 value, bool isVolatile=false);
	virtual void indexableStoreU16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, U_16 value, bool isVolatile=false);
	virtual void indexableStoreI16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_16 value, bool isVolatile=false);
	virtual void indexableStoreU32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, U_32 value, bool isVolatile=false);
	virtual void indexableStoreI32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_32 value, bool isVolatile=false);
	virtual void indexableStoreU64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, U_64 value, bool isVolatile=false);
	virtual void indexableStoreI64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_64 value, bool isVolatile=false);
	virtual void copyObjectFieldsToFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t srcObject, J9IndexableObject *arrayRef, I_32 index);
	virtual void copyObjectFieldsFromFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t destObject, J9IndexableObject *arrayRef, I_32 index);

	enum {
		ARRAY_COPY_SUCCESSFUL = -1,
		ARRAY_COPY_NOT_DONE = -2
	};

	virtual I_32 doCopyContiguousForward(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots);	
	virtual I_32 doCopyContiguousBackward(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots);	
	virtual I_32 backwardReferenceArrayCopyIndex(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots) { return -2; }
	virtual I_32 forwardReferenceArrayCopyIndex(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots) { return -2; }

	virtual J9Object *staticReadObject(J9VMThread *vmThread, J9Class *clazz, J9Object **srcSlot, bool isVolatile=false);
	virtual void *staticReadAddress(J9VMThread *vmThread, J9Class *clazz, void **srcSlot, bool isVolatile=false);
	virtual U_32 staticReadU32(J9VMThread *vmThread, J9Class *clazz, U_32 *srcSlot, bool isVolatile=false);
	virtual I_32 staticReadI32(J9VMThread *vmThread, J9Class *clazz, I_32 *srcSlot, bool isVolatile=false);
	virtual U_64 staticReadU64(J9VMThread *vmThread, J9Class *clazz, U_64 *srcSlot, bool isVolatile=false);
	virtual I_64 staticReadI64(J9VMThread *vmThread, J9Class *clazz, I_64 *srcSlot, bool isVolatile=false);

	virtual void staticStoreObject(J9VMThread *vmThread, J9Class *clazz, J9Object **destSlot, J9Object *value, bool isVolatile=false);
	virtual void staticStoreAddress(J9VMThread *vmThread, J9Class *clazz, void **destSlot, void *value, bool isVolatile=false);
	virtual void staticStoreU32(J9VMThread *vmThread, J9Class *clazz, U_32 *destSlot, U_32 value, bool isVolatile=false);
	virtual void staticStoreI32(J9VMThread *vmThread, J9Class *clazz, I_32 *destSlot, I_32 value, bool isVolatile=false);
	virtual void staticStoreU64(J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_64 value, bool isVolatile=false);
	virtual void staticStoreI64(J9VMThread *vmThread, J9Class *clazz, I_64 *destSlot, I_64 value, bool isVolatile=false);

	virtual U_8 *getArrayObjectDataAddress(J9VMThread *vmThread, J9IndexableObject *arrayObject);
	virtual j9objectmonitor_t *getLockwordAddress(J9VMThread *vmThread, J9Object *object);
	virtual void cloneObject(J9VMThread *vmThread, J9Object *srcObject, J9Object *destObject);
	virtual void copyObjectFields(J9VMThread *vmThread, J9Class *valueClass, J9Object *srcObject, UDATA srcOffset, J9Object *destObject, UDATA destOffset);
	virtual BOOLEAN structuralCompareFlattenedObjects(J9VMThread *vmThread, J9Class *valueClass, j9object_t lhsObject, j9object_t rhsObject, UDATA startOffset);
	virtual void cloneIndexableObject(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject);
	virtual J9Object *asConstantPoolObject(J9VMThread *vmThread, J9Object* toConvert, UDATA allocationFlags);
	virtual void storeObjectToInternalVMSlot(J9VMThread *vmThread, J9Object** destSlot, J9Object *value);
	virtual J9Object *readObjectFromInternalVMSlot(J9VMThread *vmThread, J9Object **srcSlot);
	virtual bool compareAndSwapObject(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *compareObject, J9Object *swapObject);
	virtual bool staticCompareAndSwapObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, J9Object *compareObject, J9Object *swapObject);
	virtual bool mixedObjectCompareAndSwapInt(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValue, U_32 swapValue);
	virtual bool staticCompareAndSwapInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue);
	virtual bool mixedObjectCompareAndSwapLong(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_64 compareValue, U_64 swapValue);
	virtual bool staticCompareAndSwapLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue);
	virtual J9Object *compareAndExchangeObject(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *compareObject, J9Object *swapObject);
	virtual J9Object *staticCompareAndExchangeObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, J9Object *compareObject, J9Object *swapObject);
	virtual U_32 mixedObjectCompareAndExchangeInt(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValue, U_32 swapValue);
	virtual U_32 staticCompareAndExchangeInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue);
	virtual U_64 mixedObjectCompareAndExchangeLong(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_64 compareValue, U_64 swapValue);
	virtual U_64 staticCompareAndExchangeLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue);

	virtual void deleteHeapReference(MM_EnvironmentBase *env, J9Object *object) {}

	virtual bool preObjectStore(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *value, bool isVolatile=false);
	virtual bool preObjectStore(J9VMThread *vmThread, J9Object *destClass, J9Object **destAddress, J9Object *value, bool isVolatile=false);
	virtual bool preObjectStore(J9VMThread *vmThread, J9Object **destAddress, J9Object *value, bool isVolatile=false);
	virtual void postObjectStore(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *value, bool isVolatile=false);
	virtual void postObjectStore(J9VMThread *vmThread, J9Class *destClass, J9Object **destAddress, J9Object *value, bool isVolatile=false);
	virtual void postObjectStore(J9VMThread *vmThread, J9Object **destAddress, J9Object *value, bool isVolatile=false);
	
	virtual bool preBatchObjectStore(J9VMThread *vmThread, J9Object *destObject, bool isVolatile=false);
	virtual bool preBatchObjectStore(J9VMThread *vmThread, J9Class *destClass, bool isVolatile=false);

	virtual bool preObjectRead(J9VMThread *vmThread, J9Object *srcObject, fj9object_t *srcAddress);
	virtual bool preObjectRead(J9VMThread *vmThread, J9Class *srcClass, j9object_t *srcAddress);
	virtual bool preWeakRootSlotRead(J9VMThread *vmThread, j9object_t *srcAddress);
	virtual bool preWeakRootSlotRead(J9JavaVM *vm, j9object_t *srcAddress);
	virtual bool postObjectRead(J9VMThread *vmThread, J9Object *srcObject, fj9object_t *srcAddress);
	virtual bool postObjectRead(J9VMThread *vmThread, J9Class *srcClass, J9Object **srcAddress);

	/**
	 * Return back true if object references are compressed
	 * @return true, if object references are compressed
	 */
	MMINLINE bool
	compressObjectReferences()
	{
		return OMR_COMPRESS_OBJECT_REFERENCES(_compressObjectReferences);
	}

	/**
	 * Special barrier for auto-remembering stack-referenced objects. This must be called 
	 * in two cases:
	 * 1) an object which was allocated directly into old space.
	 * 2) an object which is being constructed via JNI
	 * 
	 * @param vmThread[in] the current thread
	 * @param object[in] the object to be remembered 
	 */
	virtual void 
	recentlyAllocatedObject(J9VMThread *vmThread, J9Object *object) 
	{ 
		/* empty default implementation */
	};
	
	/**
	 * Record that the specified class is stored in the specified ClassLoader.
	 * There are three cases in which this must be called:
	 * 1 - when a newly loaded class stored in its defining loader's table
	 * 2 - when a newly loaded array class is stored in its component class's arrayClass field
	 * 3 - when a foreign class is stored in another loader's table
	 * 
	 * @param[in] vmThread the current thread
	 * @param[in] destClassLoader the referencing ClassLoader
	 * @param[in] srcClass the referenced class
	 */
	virtual void 
	postStoreClassToClassLoader(J9VMThread *vmThread, J9ClassLoader* destClassLoader, J9Class* srcClass)
	{
		/* Default behaviour is to do nothing. Only collectors which implement JAZZ 18309 require this barrier. */
	}
	
	/**
	 * Converts token (e.g. compressed pointer value) into real heap pointer.
	 * @return the heap pointer value.
	 * 
	 * @note this function is not virtual because it must be callable from out-of-process
	 */
	MMINLINE mm_j9object_t 
	convertPointerFromToken(fj9object_t token)
	{
		mm_j9object_t result = (mm_j9object_t)(uintptr_t)token;
#if defined(OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			result = (mm_j9object_t)((UDATA)token << compressedPointersShift());
		}
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
		return result;
	}
	
	/**
	 * Converts real heap pointer into token (e.g. compressed pointer value).
	 * @return the compressed pointer value.
	 * 
	 * @note this function is not virtual because it must be callable from out-of-process
	 */
	MMINLINE fj9object_t 
	convertTokenFromPointer(mm_j9object_t pointer)
	{
		fj9object_t result = (fj9object_t)(uintptr_t)pointer;
#if defined(OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			result = (fj9object_t)((UDATA)pointer >> compressedPointersShift());
		}
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
		return result;
	}

	/**
	 * Returns the value to shift a pointer by when converting between the compressed pointers heap and real heap
	 * @return The shift value.
	 * 
	 * @note this function is not virtual because it must be callable from out-of-process
	 * 
	 */
	MMINLINE UDATA 
	compressedPointersShift()
	{
		UDATA shift = 0;
#if defined(OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			shift = _compressedPointersShift;
		}
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
		return shift;
	}

	virtual UDATA compressedPointersShadowHeapBase(J9VMThread *vmThread);
	virtual UDATA compressedPointersShadowHeapTop(J9VMThread *vmThread);
	virtual void fillArrayOfObjects(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_32 count, J9Object *value);
	
	virtual void initializeForNewThread(MM_EnvironmentBase* env) {};
	
	/**
	 * Determine the basic hash code for the specified object. This may modify the object. For example, it may
	 * set the HAS_BEEN_HASHED bit in the object's header. Object must not be NULL.
	 * 
	 * @param vm[in] the VM
	 * @param object[in] the object to be hashed
	 * @return the persistent, basic hash code for the object 
	 */
	I_32 getObjectHashCode(J9JavaVM *vm, J9Object *object);
	
	/**
	 * Set the finalize link field of object to value.
	 * @param object the object to modify
	 * @param value[in] the value to store into the object's finalizeLink field
	 */
	void setFinalizeLink(j9object_t object, j9object_t value);
	
	/**
	 * Fetch the finalize link field of object.
	 * @param object[in] the object to read
	 * @return the value stored in the object's finalizeLink field
	 */
	MMINLINE j9object_t getFinalizeLink(j9object_t object)
	{
		fj9object_t* finalizeLink = getFinalizeLinkAddress(object);
		GC_SlotObject slot(_extensions->getOmrVM(), finalizeLink);		
		return slot.readReferenceFromSlot();
	}
	
	/**
	 * Fetch the finalize link field of object.
	 * @param object[in] the object to read
	 * @param clazz[in] the class to read the finalizeLinkOffset from
	 * @return the value stored in the object's finalizeLink field
	 */
	MMINLINE j9object_t getFinalizeLink(j9object_t object, J9Class *clazz)
	{
		fj9object_t* finalizeLink = getFinalizeLinkAddress(object, clazz);
		GC_SlotObject slot(_extensions->getOmrVM(), finalizeLink);		
		return slot.readReferenceFromSlot();
	}

	
	/**
	 * Set the reference link field of the specified reference object to value.
	 * @param object the object to modify
	 * @param value the value to store into the object's reference link field
	 */
	void setReferenceLink(j9object_t object, j9object_t value);

	/**
	 * Fetch the reference link field of the specified reference object.
	 * @param object the object to read
	 * @return the value stored in the object's reference link field
	 */
	j9object_t getReferenceLink(j9object_t object)
	{
		UDATA linkOffset = _referenceLinkOffset;
		fj9object_t *referenceLink = (fj9object_t*)((UDATA)object + linkOffset);
		GC_SlotObject slot(_extensions->getOmrVM(), referenceLink);		
		return slot.readReferenceFromSlot();
	}

	/**
	 * Set the ownableSynchronizerLink link field of the specified reference object to value.
	 * @param object the object to modify
	 * @param value the value to store into the object's reference link field
	 */
	void setOwnableSynchronizerLink(j9object_t object, j9object_t value);

	/**
	 * Fetch the ownableSynchronizerLink link field of the specified reference object.(for Compact or forwardedObject case)
	 * @param object the object(moved object -- new location) to read
	 * @param originalObject(old location, for checking if it is the last item in the list)
	 * @return the value stored in the object's reference link field
	 */
	j9object_t getOwnableSynchronizerLink(j9object_t object, j9object_t originalObject)
	{
		UDATA linkOffset = _ownableSynchronizerLinkOffset;
		fj9object_t *ownableSynchronizerLink = (fj9object_t*)((UDATA)object + linkOffset);
		GC_SlotObject slot(_extensions->getOmrVM(), ownableSynchronizerLink);		
		j9object_t next = slot.readReferenceFromSlot();
		if (originalObject == next) {
			/* reach end of list(last item points to itself), return NULL */
			next = NULL;
		}
		return next;
	}

	/**
	 * Fetch the ownableSynchronizerLink link field of the specified reference object.
	 * @param object the object to read
	 * @return the value stored in the object's reference link field
	 */
	j9object_t getOwnableSynchronizerLink(j9object_t object)
	{
		UDATA linkOffset = _ownableSynchronizerLinkOffset;
		fj9object_t *ownableSynchronizerLink = (fj9object_t*)((UDATA)object + linkOffset);
		GC_SlotObject slot(_extensions->getOmrVM(), ownableSynchronizerLink);		
		j9object_t next = slot.readReferenceFromSlot();
		if (object == next) {
			/* reach end of list(last item points to itself), return NULL */
			next = NULL;
		}
		return next;
	}

	/**
	 * check if the object in one of OwnableSynchronizerLists
	 * @param object the object pointer
	 * @return the value stored in the object's reference link field
	 * 		   if reference link field == NULL, it means the object isn't in the list
	 */
	j9object_t  isObjectInOwnableSynchronizerList(j9object_t object)
	{
		UDATA linkOffset = _ownableSynchronizerLinkOffset;
		fj9object_t *ownableSynchronizerLink = (fj9object_t*)((UDATA)object + linkOffset);
		GC_SlotObject slot(_extensions->getOmrVM(), ownableSynchronizerLink);		
		return slot.readReferenceFromSlot();
	}

	/**
	 * Implementation of the JNI GetPrimitiveArrayCritical API.
	 * See the JNI spec for full details.
	 *
	 * @param vmThread		current J9VMThread (aka JNIEnv)
	 * @param array			a JNI reference to the primitive array
	 * @param isCopy		NULL, or a pointer to a jboolean that will set to JNI_TRUE if a copy is made
	 * @return 				a pointer to the array data; this may be a malloc'd copy, or a pointer into the heap
	 */
	virtual void* jniGetPrimitiveArrayCritical(J9VMThread* vmThread, jarray array, jboolean *isCopy) = 0;
	/**
	 * Implementation of the JNI ReleasePrimitiveArrayCritical API.
	 * See the JNI spec for full details.
	 *
	 * @param vmThread		current J9VMThread (aka JNIEnv)
	 * @param array			a JNI reference to the primitive array
	 * @param elems			the pointer returned by GetPrimitiveArrayCritical
	 * @param mode			how to handle the data; only valid for copied data
	 */
	virtual void jniReleasePrimitiveArrayCritical(J9VMThread* vmThread, jarray array, void * elems, jint mode) = 0;
	/**
	 * Implementation of the JNI GetStringCritical API.
	 * See the JNI spec for full details.
	 *
	 * @param vmThread		current J9VMThread (aka JNIEnv)
	 * @param str			a JNI reference to the java/lang/String object
	 * @param isCopy		NULL, or a pointer to a jboolean that will set to JNI_TRUE if a copy is made
	 * @return 				a pointer to the character data; this may be a malloc'd copy, or a pointer into the heap
	 */
	virtual const jchar* jniGetStringCritical(J9VMThread* vmThread, jstring str, jboolean *isCopy) = 0;
	/**
	 * Implementation of the JNI ReleaseStringCritical API.
	 * See the JNI spec for full details.
	 *
	 * @param vmThread		current J9VMThread (aka JNIEnv)
	 * @param array			a JNI reference to the java/lang/String object
	 * @param elems			the pointer returned by GetStringCritical
	 */
	virtual void jniReleaseStringCritical(J9VMThread* vmThread, jstring str, const jchar* elems) = 0;
	/**
	 * Process objects that are forced onto the finalizable list at shutdown.
	 * Called from FinalizerSupport finalizeForcedUnfinalizedToFinalizable
	 *
	 * @param vmThread		current J9VMThread (aka JNIEnv)
	 * @param object		object forced onto the finalizable list
	 */
	virtual void forcedToFinalizableObject(J9VMThread* vmThread, J9Object *object)
	{
		/* no-op default implementation */
	}

	virtual J9Object *referenceGet(J9VMThread *vmThread, J9Object *refObject)
	{
		return J9VMJAVALANGREFREFERENCE_REFERENT_VM(vmThread->javaVM, refObject);
	}

	virtual void referenceReprocess(J9VMThread *vmThread, J9Object *refObject)
	{
		/* no-op default implementation */
	}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	/**
	 * Check is class alive
	 * Required to prevent visibility of dead classes in incremental GC policies 
	 * The real implementation done for Real Time GC
	 * @param javaVM pointer to J9JavaVM
	 * @param classPtr class to check
	 * @return true if class is alive
	 */
	virtual bool checkClassLive(J9JavaVM *javaVM, J9Class *classPtr)
	{
		return true;
	}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

	virtual bool checkStringConstantsLive(J9JavaVM *javaVM, j9object_t stringOne, j9object_t stringTwo)
	{
		return true;
	}

	virtual bool checkStringConstantLive(J9JavaVM *javaVM, j9object_t string)
	{
		return true;
	}

	virtual void jniDeleteGlobalReference(J9VMThread *vmThread, J9Object *reference)
	{
		/* no-op default implementation */
	}

	MM_ObjectAccessBarrier(MM_EnvironmentBase *env) : MM_BaseVirtual()
		, _extensions(NULL) 
		, _heap(NULL)
#if defined(OMR_GC_COMPRESSED_POINTERS)
#if defined(OMR_GC_FULL_POINTERS)
		, _compressObjectReferences(false)
#endif /* defined(OMR_GC_FULL_POINTERS) */
		, _compressedPointersShift(0)
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */
		, _referenceLinkOffset(UDATA_MAX)
		, _ownableSynchronizerLinkOffset(UDATA_MAX)
	{
		_typeId = __FUNCTION__;
	}

	friend class MM_ScavengerDelegate;
};

#endif /* OBJECTACCESSBARRIER_HPP_ */

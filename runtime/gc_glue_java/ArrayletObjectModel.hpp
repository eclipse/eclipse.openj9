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
#if !defined(ARRAYLETOBJECTMODEL_)
#define ARRAYLETOBJECTMODEL_

#include "j9.h"
#include "j9cfg.h"
#include "j9consts.h"
#include "modron.h"
#include "modronopt.h"

#include "ArrayletObjectModelBase.hpp"
#include "Bits.hpp"
#include "ForwardedHeader.hpp"
#include "Math.hpp"
#include "SlotObject.hpp"

class GC_ArrayletObjectModel : public GC_ArrayletObjectModelBase
{
/*
* Data members
*/
private:
protected:
public:

/*
* Function members
*/
private:
	void AssertBadElementSize();
protected:
	/* forward declare methods from parent class to avoid namespace issues */
	MMINLINE uintptr_t
	getSpineSizeWithoutHeader(ArrayLayout layout, uintptr_t numberArraylets, uintptr_t dataSize, bool alignData)
	{
		return GC_ArrayletObjectModelBase::getSpineSizeWithoutHeader(layout, numberArraylets, dataSize, alignData);
	}
public:
	/* forward declare methods from parent class to avoid namespace issues */
	MMINLINE uintptr_t
	arrayletSize(J9IndexableObject *objPtr, uintptr_t index, uintptr_t dataSizeInBytes, uintptr_t numberOfArraylets)
	{
		return GC_ArrayletObjectModelBase::arrayletSize(objPtr, index, dataSizeInBytes, numberOfArraylets);
	}
	MMINLINE uintptr_t
	numArraylets(uintptr_t unadjustedDataSizeInBytes)
	{
		return GC_ArrayletObjectModelBase::numArraylets(unadjustedDataSizeInBytes);
	}

	/**
	 * Get the header size for contiguous arrays
	 * @return header size
	 */
	MMINLINE uintptr_t
	contiguousHeaderSize()
	{
		return compressObjectReferences() ? sizeof(J9IndexableObjectContiguousCompressed) : sizeof(J9IndexableObjectContiguousFull);
	}

	/**
	 * Get the header size for discontiguous arrays
	 * @return header size
	 */
	MMINLINE uintptr_t
	discontiguousHeaderSize()
	{
		return compressObjectReferences() ? sizeof(J9IndexableObjectDiscontiguousCompressed) : sizeof(J9IndexableObjectDiscontiguousFull);
	}

	/**
	 * Get the spine size for the given indexable object
	 * @param objPtr Pointer to an array object
	 * @return The total size in bytes of objPtr's array spine;
	 * 			includes header, arraylet ptrs, and (if present) padding & inline data
	 */
	MMINLINE uintptr_t
	getSpineSize(J9IndexableObject* objPtr)
	{
		ArrayLayout layout = getArrayLayout(objPtr);
		return getSpineSize(objPtr, layout);
	}

	/**
	 * Get the spine size for an arraylet with these properties
	 * @param J9Class The class of the array.
	 * @param layout The layout of the indexable object
	 * @param numberArraylets Number of arraylets for this indexable object
	 * @param dataSize How many elements are in the indexable object
	 * @param alignData Should the data section be aligned
	 * @return spineSize The actual size in byte of the spine
	 */
	MMINLINE uintptr_t
	getSpineSize(J9Class *clazzPtr, ArrayLayout layout, uintptr_t numberArraylets, uintptr_t dataSize, bool alignData)
	{
		uintptr_t result = getHeaderSize(clazzPtr, layout) + getSpineSizeWithoutHeader(layout, numberArraylets, dataSize, alignData);
		return result;
	}

	/**
	 * Return the total number of arraylets for the given indexable object
	 * @param objPtr Pointer to an array object
	 * @return the number of arraylets used for an array of dataSizeInBytes bytes
	 */
	MMINLINE uintptr_t
	numArraylets(J9IndexableObject *objPtr)
	{
		return numArraylets(getDataSizeInBytes(objPtr));
	}

	/**
	 * Check the given indexable object is inline contiguous
	 * @param objPtr Pointer to an array object
	 * @return true of array is inline contiguous
	 */
	MMINLINE bool
	isInlineContiguousArraylet(J9IndexableObject *objPtr)
	{
		return (getArrayLayout(objPtr) == InlineContiguous);
	}

	/**
	 * Get the number of discontiguous arraylets for the given indexable object
	 * @param objPtr Pointer to an array object
	 * @return the number of arraylets stored external to the spine.
	 */
	MMINLINE uintptr_t
	numExternalArraylets(J9IndexableObject *objPtr)
	{
		ArrayLayout layout = getArrayLayout(objPtr);
		if (layout == InlineContiguous) {
			return 0;
		}

		uintptr_t numberArraylets = numArraylets(objPtr);
		if (layout == Hybrid) {
			/* last arrayoid pointer points into spine (remainder data is contiguous with header) */
			numberArraylets -= 1;
		} else if (layout == Discontiguous) {
			/* Data fits exactly within a whole number of arraylets */
			AssertArrayletIsDiscontiguous(objPtr);
		}

		return numberArraylets;
	}

	/**
	 * Get the total number of bytes consumed by arraylets external to the
	 * given indexable object.
	 * @param objPtr Pointer to an array object
	 * @return the number of bytes consumed external to the spine
	 */
	MMINLINE uintptr_t
	externalArrayletsSize(J9IndexableObject *objPtr)
	{
		uintptr_t numberArraylets = numExternalArraylets(objPtr);

		return numberArraylets * _omrVM->_arrayletLeafSize;
	}

	/**
	 * Determine if the specified array object includes any arraylet leaf pointers.
	 * @param objPtr[in] the object to test
	 * @return true if the array has any internal or external leaf pointers, false otherwise
	 */
	MMINLINE bool
	hasArrayletLeafPointers(J9IndexableObject *objPtr)
	{
		/* Contiguous arraylet has no implicit leaf pointer */
		return !isInlineContiguousArraylet(objPtr);
	}


	/**
	 * Get the size in bytes for the arraylet at index in indexable object objPtr
	 * @param objPtr Pointer to an array object
	 * @param index the index in question.  0<=index<numArraylets(objPtr)
	 * @return the size of the indexth arraylet
	 */
	MMINLINE uintptr_t
	arrayletSize(J9IndexableObject *objPtr, uintptr_t index)
	{
		uintptr_t dataSizeInBytes = getDataSizeInBytes(objPtr);
		uintptr_t numberOfArraylets = numArraylets(dataSizeInBytes);
		return arrayletSize(objPtr, index, dataSizeInBytes, numberOfArraylets);
	}
	
	/**
	 * Get the spine size for the given indexable object
	 * @param objPtr Pointer to an array object
 	 * @param layout layout for array object
 	 * @return The total size in bytes of objPtr's array spine;
	 * 			includes header, arraylet ptrs, and (if present) padding & inline data
	 */
	MMINLINE uintptr_t
	getSpineSize(J9IndexableObject* objPtr, ArrayLayout layout)
	{
		return getSpineSize(J9GC_J9OBJECT_CLAZZ(objPtr, this), layout, getSizeInElements(objPtr));
	}

	/**
	 * Get the spine size for the given indexable object
	 * @param clazzPtr Pointer to the preserved J9Class, used in scavenger
 	 * @param layout layout for array object
 	 * @param size Size of indexable object
	 * @return The total size in bytes of objPtr's array spine;
	 * 			includes header, arraylet ptrs, and (if present) padding & inline data
	 */
	MMINLINE uintptr_t
	getSpineSize(J9Class *clazzPtr, ArrayLayout layout, uintptr_t size)
	{
		return getHeaderSize(clazzPtr, layout) + getSpineSizeWithoutHeader(clazzPtr, layout, size);
	}

	/**
	 * Get the spine size without header for the given indexable object
	 * @param clazzPtr Pointer to the preserved J9Class, used in scavenger
 	 * @param layout layout for array object
 	 * @param size Size of indexable object
	 * @return The size in bytes of objPtr's array spine without header;
	 * 			includes arraylet ptrs, padding and inline data (if any is present)
	 */
	MMINLINE uintptr_t
	getSpineSizeWithoutHeader(J9Class *clazzPtr, ArrayLayout layout, uintptr_t size)
	{
		uintptr_t dataSize = getDataSizeInBytes(clazzPtr, size);
		uintptr_t numberArraylets = numArraylets(dataSize);
		bool alignData = shouldAlignSpineDataSection(clazzPtr);
		return getSpineSizeWithoutHeader(layout, numberArraylets, dataSize, alignData);
	}

#if defined(J9VM_GC_ENABLE_DOUBLE_MAP)
	/**
	 * Checks if arraylet falls into corner case of discontigous data
	 * Arraylet possible cases:
	 * 0: Empty arraylets, in this case the array is represented as
	 *		an arraylet however it does not contain any data, but it
	 *		does contain an arrayoid (leaf pointer) that points to NULL.
	 *		Even though this case is represented as a discontiguous arraylet
	 *		internally due to its implementation, it is actually a contiguous
	 *		array with length zero.
	 * 1: The total data size in arraylet is between 0 and region
	 *		size. Small enough to make the arraylet layout contiguous,
	 *		in which case this function is unreachable.
	 * 2: The total data size in arraylet is exacly the same size
	 *		of a region. In this case we do not need to double
	 *		map since we already have a contiguous representation of the
	 *		data at first leaf.
	 * 3: Similar to first case, the data portion is slightly smaller than
	 *		a region size, however not small enough to include header and data
	 *		at the same region to make it contiguous. In which case we would
	 *		have one leaf, where we also do not need to double map.
	 * 4: The total data size in arraylet is stricly greater than one region;
	 *		however, not multiple of region size. Since with enabled double map
	 *		layout is always discontiguous, we would have 2 or more arraylet leaves
	 *		therefore we always double map.
	 * 5: The total data size in arraylet is stricly greater than one region and
	 *		multiple of region size. Here we would have 2 or more arraylet leaves
	 *		containing data. We always double map in this case.
	 *
	 * @param spine Pointer to an array indexable object spine
	 * @return false in case corner cases 0, 2 or 3 are valid. On the other hand,
	 *		if cases 4 or 5 are true, the function returns true.
	 */
	MMINLINE bool
	isArrayletDataDiscontiguous(J9IndexableObject *spine)
	{
		return numArraylets(spine) > 1;
	}

	/**
	 * Checks if arraylet falls into corner case of contiguous data
	 * 
	 * @param spine Pointer to an array indexable object spine
	 * @return true in case corner cases 2 or 3 are valid. On the other hand,
	 * 		if cases 0, 4 or 5 are true, the function returns false.
	 */
	MMINLINE bool
	isArrayletDataContiguous(J9IndexableObject *spine)
	{
		return (1 == numArraylets(spine)) && (getSizeInElements(spine) > 0);
	}
#endif /* J9VM_GC_ENABLE_DOUBLE_MAP */

	/**
	 * We can't use memcpy because it may be not atomic for pointers, use this function instead
	 * Copy data in uintptr_t words
	 * If length is not times of uintptr_t one more word is copied
	 * @param destAddr address copy to
	 * @param sourceAddr address copy from
	 * @param lengthInBytes requested size in bytes
	 */
	MMINLINE void
	copyInWords (uintptr_t *destAddr, uintptr_t *sourceAddr, uintptr_t lengthInBytes)
	{
		uintptr_t lengthInWords = lengthInBytes / sizeof(uintptr_t);
		while (lengthInWords--) {
			*destAddr++ = *sourceAddr++;
		}
	}

	/**
	 * Returns the header size of an arraylet for a given layout.
	 * This method is actually private, but since ArrayLayout is public,
	 * it cannot be declared in private section.
	 * @param layout Layout of the arraylet
	 * @return Size of header in bytes
	 */
	MMINLINE uintptr_t
	getHeaderSize(J9Class *clazzPtr, ArrayLayout layout)
	{
		uintptr_t headerSize = contiguousHeaderSize();
		if (layout != InlineContiguous) {
			headerSize = discontiguousHeaderSize();
		}
		return headerSize;
	}

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves and alignment
	 * padding, excluding the header, or UDATA_MAX if an overflow occurs.
	 * @param arrayPtr Pointer to the indexable object whose size is required
	 * @return Aligned size in bytes excluding the header, or UDATA_MAX if an overflow occurs
	 */
	MMINLINE uintptr_t
	getDataSizeInBytes(J9IndexableObject *arrayPtr)
	{
		return getDataSizeInBytes(J9GC_J9OBJECT_CLAZZ(arrayPtr, this), getSizeInElements(arrayPtr));
	}

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves and alignment
	 * padding, excluding the header, or UDATA_MAX if an overflow occurs.
	 * @param clazzPtr Pointer to the class of the object
	 * @param numberOfElements size from indexable object
	 * @return Aligned size in bytes excluding the header, or UDATA_MAX if an overflow occurs
	 */
	MMINLINE uintptr_t
	getDataSizeInBytes(J9Class *clazzPtr, uintptr_t numberOfElements)
	{	
		uintptr_t stride = J9ARRAYCLASS_GET_STRIDE(clazzPtr);
		uintptr_t size = numberOfElements * stride;
		uintptr_t alignedSize = UDATA_MAX;
		if ((size / stride) == numberOfElements) {
			alignedSize = MM_Math::roundToSizeofUDATA(size);
			if (alignedSize < size) {
				alignedSize = UDATA_MAX;
			}
		}
		return alignedSize;
	}

	/**
	 * Get the size from the header for the given indexable object
	 * @param objPtr Pointer to an array object
	 * @return the size
	 */
	MMINLINE uintptr_t
	getArraySize(J9IndexableObject *objPtr)
	{
		return compressObjectReferences()
			? ((J9IndexableObjectContiguousCompressed*)objPtr)->size
			: ((J9IndexableObjectContiguousFull*)objPtr)->size;
	}

	/**
	 * Get the layout for the given indexable object
	 * @param objPtr Pointer to an array object
	 * @return the ArrayLayout for objectPtr
	 */
	MMINLINE ArrayLayout
	getArrayLayout(J9IndexableObject *objPtr)
	{
		GC_ArrayletObjectModel::ArrayLayout layout = GC_ArrayletObjectModel::InlineContiguous;
		/* Trivial check for InlineContiguous. */
		if (0 != getArraySize(objPtr)) {
			return GC_ArrayletObjectModel::InlineContiguous;
		}

		/* Check if the objPtr is in the allowed arraylet range. */
		if (((uintptr_t)objPtr >= (uintptr_t)_arrayletRangeBase) && ((uintptr_t)objPtr < (uintptr_t)_arrayletRangeTop)) {
			uintptr_t dataSizeInBytes = getDataSizeInBytes(objPtr);
			J9Class* clazz = J9GC_J9OBJECT_CLAZZ(objPtr, this);
			layout = getArrayletLayout(clazz, dataSizeInBytes);
		}
		return layout;
	}

	MMINLINE ArrayLayout
	getArrayletLayout(J9Class* clazz, uintptr_t dataSizeInBytes)
	{
		return getArrayletLayout(clazz, dataSizeInBytes, _largestDesirableArraySpineSize);
	}

	/**
	 * Get the layout of an indexable object given it's class, data size in bytes and the subspace's largestDesirableSpine.
	 * @param clazz The class of the object stored in the array.
	 * @param dataSizeInBytes the size in bytes of the data of the array.
	 * @param largestDesirableSpine The largest desirable spine of the arraylet.
	 */
	ArrayLayout getArrayletLayout(J9Class* clazz, uintptr_t dataSizeInBytes, uintptr_t largestDesirableSpine);

	/**
	 * Perform a safe memcpy of one array to another.
	 * Assumes that destObject and srcObject have the same shape and size.
	 */
	MMINLINE void
	memcpyArray(J9IndexableObject *destObject, J9IndexableObject *srcObject)
	{
		if (InlineContiguous == getArrayLayout(srcObject)) {
			/* assume that destObject must have the same shape! */
			uintptr_t sizeInBytes = getSizeInBytesWithoutHeader(srcObject);
			uintptr_t* srcData = (uintptr_t*)getDataPointerForContiguous(srcObject);
			uintptr_t* destData = (uintptr_t*)getDataPointerForContiguous(destObject);
			copyInWords(destData, srcData, sizeInBytes);
		} else {
			bool const compressed = compressObjectReferences();
			uintptr_t arrayletCount = numArraylets(srcObject);
			fj9object_t *srcArraylets = getArrayoidPointer(srcObject);
			fj9object_t *destArraylets = getArrayoidPointer(destObject);
			for (uintptr_t i = 0; i < arrayletCount; i++) {
				GC_SlotObject srcSlotObject(_omrVM, GC_SlotObject::addToSlotAddress(srcArraylets, i, compressed));
				GC_SlotObject destSlotObject(_omrVM, GC_SlotObject::addToSlotAddress(destArraylets, i, compressed));
				void* srcLeafAddress = srcSlotObject.readReferenceFromSlot();
				void* destLeafAddress = destSlotObject.readReferenceFromSlot();
				

				uintptr_t copySize = _omrVM->_arrayletLeafSize;
				
				if (i == arrayletCount - 1) {
					copySize = arrayletSize(srcObject, i);
				}
				copyInWords((uintptr_t *)destLeafAddress, (uintptr_t *)srcLeafAddress, copySize);
			}
		}
	}

	/**
	 * Perform a memcpy from a primitive array to native memory.
	 * Assumes that the destination is large enough.
	 *
	 * @param destData		the native memory buffer to copy into
	 * @param srcObject		the array to copy from
	 * @param elementIndex	the start index (element-relative) to copy from
	 * @param elementCount	the number of elements of data to copy
	 */
	/*MMINLINE*/ void
	memcpyFromArray(void *destData, J9IndexableObject *srcObject, int32_t elementIndex, int32_t elementCount)
	{
		uintptr_t elementSize = J9ARRAYCLASS_GET_STRIDE(J9GC_J9OBJECT_CLAZZ(srcObject, this));
		if (isInlineContiguousArraylet(srcObject)) {
			// If the data is stored contiguously, then a simple copy is sufficient.
			void* srcData = getDataPointerForContiguous(srcObject);
			switch(elementSize)
			{
			case 1:
				// byte/boolean
				{
					uint8_t* srcCursor = ((uint8_t*)srcData) + elementIndex;
					uint8_t* destCursor = (uint8_t*)destData;
					int32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 2:
				// short/char
				{
					uint16_t* srcCursor = ((uint16_t*)srcData) + elementIndex;
					uint16_t* destCursor = (uint16_t*)destData;
					int32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 4:
				// int/float
				{
					uint32_t* srcCursor = ((uint32_t*)srcData) + elementIndex;
					uint32_t* destCursor = (uint32_t*)destData;
					uint32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 8:
				// long/double
				{
					U_64* srcCursor = ((U_64*)srcData) + elementIndex;
					U_64* destCursor = (U_64*)destData;
					uint32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;
				
			default :
				// unreachable since we currently do not expect anything other than primitive arrays to be using them.
				{
					AssertBadElementSize();
				}
				break;
			}
		} else {
			bool const compressed = compressObjectReferences();
			fj9object_t *srcArraylets = getArrayoidPointer(srcObject);
			void *outerDestCursor = destData;
			uint32_t outerCount = elementCount;
			uintptr_t arrayletLeafElements = _omrVM->_arrayletLeafSize / elementSize;
			uintptr_t arrayletIndex = elementIndex / arrayletLeafElements;
			uintptr_t arrayletElementOffset = elementIndex % arrayletLeafElements;
			while(outerCount > 0) {
				GC_SlotObject srcSlotObject(_omrVM, GC_SlotObject::addToSlotAddress(srcArraylets, arrayletIndex++, compressed));
				void* srcLeafAddress = srcSlotObject.readReferenceFromSlot();
				uint32_t innerCount = outerCount;
				// Can we fulfill the remainder of the copy from this page?
				if (innerCount + arrayletElementOffset > arrayletLeafElements) {
					// Copy as much as we can
					innerCount = (uint32_t)(arrayletLeafElements - arrayletElementOffset);
				}
				switch(elementSize)
				{
				case 1:
					// byte/boolean
					{
						uint8_t* srcCursor = ((uint8_t*)srcLeafAddress) + arrayletElementOffset;
						uint8_t* destCursor = (uint8_t*)outerDestCursor;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerDestCursor = (void*)destCursor;
						arrayletElementOffset = 0;	// Consumed
					}
					break;

				case 2:
					// short/char
					{
						uint16_t* srcCursor = ((uint16_t*)srcLeafAddress) + arrayletElementOffset;
						uint16_t* destCursor = (uint16_t*)outerDestCursor;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerDestCursor = (void*)destCursor;
						arrayletElementOffset = 0;
					}
					break;

				case 4:
					// int/float
					{
						uint32_t* srcCursor = ((uint32_t*)srcLeafAddress) + arrayletElementOffset;
						uint32_t* destCursor = (uint32_t*)outerDestCursor;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerDestCursor = (void*)destCursor;
						arrayletElementOffset = 0;
					}
					break;

				case 8:
					// long/double
					{
						U_64* srcCursor = ((U_64*)srcLeafAddress) + arrayletElementOffset;
						U_64* destCursor = (U_64*)outerDestCursor;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerDestCursor = (void*)destCursor;
						arrayletElementOffset = 0;
					}
					break;

				default :
					// unreachable since we currently do not expect anything other than primitive arrays to be using them.
					{
						AssertBadElementSize();
					}
					break;
				}
			}
		}
	}

	/**
	 * Perform a memcpy from native memory to a primitive array.
	 * Assumes that the destination is large enough.
	 *
	 * @param destObject	the array to copy to
	 * @param elementIndex	the start index (element-relative) to copy from
	 * @param elementCount	the number of elements of data to copy
	 * @param srcData		the native memory buffer to copy from
	 */
	MMINLINE void
	memcpyToArray(J9IndexableObject *destObject, int32_t elementIndex, int32_t elementCount, void *srcData)
	{
		uintptr_t elementSize = J9ARRAYCLASS_GET_STRIDE(J9GC_J9OBJECT_CLAZZ(destObject, this));
		if (isInlineContiguousArraylet(destObject)) {
			// If the data is stored contiguously, then a simple copy is sufficient.
			void* destData = getDataPointerForContiguous(destObject);
			switch(elementSize)
			{
			case 1:
				// byte/boolean
				{
					uint8_t* srcCursor = (uint8_t*)srcData;
					uint8_t* destCursor = ((uint8_t*)destData) + elementIndex;
					int32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 2:
				// short/char
				{
					uint16_t* srcCursor = (uint16_t*)srcData;
					uint16_t* destCursor = ((uint16_t*)destData) + elementIndex;
					int32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 4:
				// int/float
				{
					uint32_t* srcCursor = (uint32_t*)srcData;
					uint32_t* destCursor = ((uint32_t*)destData) + elementIndex;
					uint32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			case 8:
				// long/double
				{
					U_64* srcCursor = (U_64*)srcData;
					U_64* destCursor = ((U_64*)destData) + elementIndex;
					uint32_t count = elementCount;
					while(count--) {
						*destCursor++ = *srcCursor++;
					}
				}
				break;

			default :
				// unreachable since we currently do not expect anything other than primitive arrays to be using them.
				{
					AssertBadElementSize();
				}
				break;
			}
		} else {
			bool const compressed = compressObjectReferences();
			fj9object_t *destArraylets = getArrayoidPointer(destObject);
			void *outerSrcCursor = srcData;
			uint32_t outerCount = elementCount;
			uintptr_t arrayletLeafElements = _omrVM->_arrayletLeafSize / elementSize;
			uintptr_t arrayletIndex = elementIndex / arrayletLeafElements;
			uintptr_t arrayletElementOffset = elementIndex % arrayletLeafElements;
			while(outerCount > 0) {
				GC_SlotObject destSlotObject(_omrVM, GC_SlotObject::addToSlotAddress(destArraylets, arrayletIndex++, compressed));
				void* destLeafAddress = destSlotObject.readReferenceFromSlot();
				uint32_t innerCount = outerCount;
				// Can we fulfill the remainder of the copy from this page?
				if (innerCount + arrayletElementOffset > arrayletLeafElements) {
					// Copy as much as we can
					innerCount = (uint32_t)(arrayletLeafElements - arrayletElementOffset);
				}
				switch(elementSize)
				{
				case 1:
					// byte/boolean
					{
						uint8_t* srcCursor = (uint8_t*)outerSrcCursor;
						uint8_t* destCursor = ((uint8_t*)destLeafAddress) + arrayletElementOffset;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerSrcCursor = (void*)srcCursor;
						arrayletElementOffset = 0;	// Consumed
					}
					break;

				case 2:
					// short/char
					{
						uint16_t* srcCursor = (uint16_t*)outerSrcCursor;
						uint16_t* destCursor = ((uint16_t*)destLeafAddress) + arrayletElementOffset;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerSrcCursor = (void*)srcCursor;
						arrayletElementOffset = 0;
					}
					break;

				case 4:
					// int/float
					{
						uint32_t* srcCursor = (uint32_t*)outerSrcCursor;
						uint32_t* destCursor = ((uint32_t*)destLeafAddress) + arrayletElementOffset;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerSrcCursor = (void*)srcCursor;
						arrayletElementOffset = 0;
					}
					break;

				case 8:
					// long/double
					{
						U_64* srcCursor = (U_64*)outerSrcCursor;
						U_64* destCursor = ((U_64*)destLeafAddress) + arrayletElementOffset;
						outerCount -= innerCount;	// Decrement the outer count before looping
						while(innerCount--) {
							*destCursor++ = *srcCursor++;
						}
						outerSrcCursor = (void*)srcCursor;
						arrayletElementOffset = 0;
					}
					break;
					
				default :
					// unreachable since we currently do not expect anything other than primitive arrays to be using them.
					{
						AssertBadElementSize();
					}
					break;
				}
			}
		}
	}

	/**
	 * Returns the size of an indexable object, in bytes, excluding the header.
	 * @param arrayPtr Pointer to the indexable object whose size is required
	 * @return Size of object in bytes excluding the header
	 */
	MMINLINE uintptr_t
	getSizeInBytesWithoutHeader(J9IndexableObject *arrayPtr)
	{
		ArrayLayout layout = getArrayLayout(arrayPtr);
		return getSpineSizeWithoutHeader(J9GC_J9OBJECT_CLAZZ(arrayPtr, this), layout, getSizeInElements(arrayPtr));
	}

	/**
	 * Returns the size of an indexable object, in bytes, including the header.
	 * WARNING: This implementation assumes the arraylet layout is InlineContiguous.
	 * @param clazz Pointer to the preserved indexable object class which may not be intact
	 * @param numberOfElements size from indexable object
	 * @return Size of object in bytes including the header
	 */
	MMINLINE uintptr_t
	getSizeInBytesWithHeader(J9Class *clazz, uintptr_t numberOfElements)
	{
		ArrayLayout layout = InlineContiguous; 
		if(0 == numberOfElements) {
			layout = Discontiguous;
		}
		return getSizeInBytesWithHeader(clazz, layout, numberOfElements);
	}

	/**
	 * Returns the size of an indexable object, in bytes, including the header.
	 * @param clazz Pointer to the preserved indexable object class which may not be intact
	 * @param flags Indexable object flags
	 * @param numberOfElements size from indexable object
	 * @return Size of object in bytes including the header
	 */
	MMINLINE uintptr_t
	getSizeInBytesWithHeader(J9Class *clazz, ArrayLayout layout, uintptr_t numberOfElements)
	{
		return getSpineSize(clazz, layout, numberOfElements);
	}

	/**
	 * Returns the size of an indexable object, in bytes, including the header.
	 * @param arrayPtr Pointer to the indexable object whose size is required
	 * @return Size of object in bytes including the header
	 */
	MMINLINE uintptr_t
	getSizeInBytesWithHeader(J9IndexableObject *arrayPtr)
	{
		ArrayLayout layout = getArrayLayout(arrayPtr);
		return getSpineSize(J9GC_J9OBJECT_CLAZZ(arrayPtr, this), layout, getSizeInElements(arrayPtr));
	}

#if defined(J9VM_ENV_DATA64)

	/**
	 * Gets data pointer of a contiguous indexable object.
	 * Helper to get dataAddr field of contiguous indexable objects.
	 *
	 * @return Pointer which points to indexable object data
	 */
	MMINLINE void **
	dataAddrSlotForContiguous(J9IndexableObject *arrayPtr)
	{
		AssertContiguousArrayletLayout(arrayPtr);
		bool const compressed = compressObjectReferences();
		void **dataAddrPtr = NULL;
		if (compressed) {
			dataAddrPtr = &((J9IndexableObjectContiguousCompressed *)arrayPtr)->dataAddr;
		} else {
			dataAddrPtr = &((J9IndexableObjectContiguousFull *)arrayPtr)->dataAddr;
		}
		return dataAddrPtr;
	}

	/**
	 * Gets data pointer of a discontiguous indexable object.
	 * Helper to get dataAddr field of discontiguous indexable objects.
	 *
	 * @return Pointer which points to discontiguous indexable object data
	 */
	MMINLINE void **
	dataAddrSlotForDiscontiguous(J9IndexableObject *arrayPtr)
	{
		/* If double mapping is enabled only, arraylet will have a discontiguous layout.
		 * If sparse-heap is enabled, arraylet will have a contiguous layout. For now
		 * we can't simply Assert only the discontiguous case because there could also
		 * exist hybrid arraylets (which will be dicontinued in the future) */
		bool const compressed = compressObjectReferences();
		void **dataAddrPtr = NULL;
		if (compressed) {
			dataAddrPtr = &((J9IndexableObjectDiscontiguousCompressed *)arrayPtr)->dataAddr;
		} else {
			dataAddrPtr = &((J9IndexableObjectDiscontiguousFull *)arrayPtr)->dataAddr;
		}
		return dataAddrPtr;
	}

	/**
	 * Sets data pointer of a contiguous indexable object.
	 * Sets the data pointer of a contiguous indexable object; in this case
	 * dataAddr will point directly into the data right after dataAddr field
	 * (data resides in heap).
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 */
	MMINLINE void
	setDataAddrForContiguous(J9IndexableObject *arrayPtr)
	{
		void **dataAddrPtr = dataAddrSlotForContiguous(arrayPtr);
		void *dataAddr = (void *)((uintptr_t)arrayPtr + contiguousHeaderSize());
		*dataAddrPtr = dataAddr;
	}

	/**
	 * Sets data pointer of a discontiguous indexable object.
	 * Sets the data pointer of a discontiguous indexable object; in this case
	 * dataAddr will point to the contiguous representation of the data
	 * which resides outside the heap (assuming double map/sparse-heap is enabled).
	 * In case double map is disabled, the dataAddr will point to the first arrayoid
	 * of the discontiguous indexable object, which also resides right after dataAddr
	 * field.
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 * @param address       Pointer which points to indexable object data
	 */
	MMINLINE void
	setDataAddrForDiscontiguous(J9IndexableObject *arrayPtr, void *address)
	{
		/* If double mapping is enabled only, arraylet will have a discontiguous layout.
		 * If sparse-heap is enabled, arraylet will have a contiguous layout. For now
		 * we can't simply Assert only the discontiguous case because there could also
		 * exist hybrid arraylets (which will be dicontinued in the future) */
		void *calculatedDataAddr = address;
		void **dataAddrPtr = dataAddrSlotForDiscontiguous(arrayPtr);

		/* If calculatedDataAddr is NULL then we make dataAddr point to the first arrayoid */
		/* Later on, when sparse-heap is enabled by default, we must assert dataAddr is not NULL */
		if (NULL == calculatedDataAddr) {
			calculatedDataAddr = (void *)((uintptr_t)arrayPtr + discontiguousHeaderSize());
		}

		*dataAddrPtr = calculatedDataAddr;
	}

	/**
	 * Asserts that an indexable object pointer is indeed an indexable object
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 */
	void AssertArrayPtrIsIndexable(J9IndexableObject *arrayPtr);

	/**
	 * Asserts that the dataAddr field of the indexable object is correct.
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 * @param dataAddr      Pointer to indexable object data address
	 */
	MMINLINE bool
	isCorrectDataAddrContiguous(J9IndexableObject *arrayPtr, void *dataAddr)
	{
		return (dataAddr == ((void *)((uintptr_t)arrayPtr + contiguousHeaderSize())));
	}
	
	/**
	 * Asserts that an indexable object pointer is indeed an indexable object
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 * @param dataAddr      Pointer to indexable object data address
	 */
	MMINLINE bool
	isCorrectDataAddrDiscontiguous(J9IndexableObject *arrayPtr, void *dataAddr)
	{
		return (dataAddr == ((void *)((uintptr_t)arrayPtr + discontiguousHeaderSize())));	
	}

	/**
	 * Returns data pointer associated with a contiguous Indexable object.
	 * Data pointer will always be pointing at the arraylet data. In this
	 * case the data pointer will be pointing to address immediately after
	 * the header.
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 * @return data address associated with the Indexable object
	 */
	MMINLINE void *
	getDataAddrForContiguous(J9IndexableObject *arrayPtr)
	{
		void *dataAddr = *dataAddrSlotForContiguous(arrayPtr);
		return dataAddr;
	}

	/**
	 * Returns data pointer associated with a discontiguous Indexable object.
	 * Data pointer will always be pointing at the arraylet data. In this
	 * case the data pointer will be pointing to address immediately after
	 * the header (the arrayoid), except when double mapping or sparse-heap
	 * is enabled. In these cases, the data pointer will point to the
	 * contiguous representation of the data; hence returning that pointer.
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 * @return data address associated with the Indexable object
	 */
	MMINLINE void *
	getDataAddrForDiscontiguous(J9IndexableObject *arrayPtr)
	{
		/* If double mapping is enabled only, arraylet will have a discontiguous layout.
		 * If sparse-heap is enabled, arraylet will have a contiguous layout. For now we
		 * Assert only the discontiguous case until sparse-heap is introduced. */
		AssertDiscontiguousArrayletLayout(arrayPtr);
		void *dataAddr = *dataAddrSlotForDiscontiguous(arrayPtr);
		return dataAddr;
	}

	/**
	 * Returns data pointer associated with the Indexable object.
	 * Data pointer will always be pointing at the arraylet data. In all
	 * cases the data pointer will be pointing to address immediately after
	 * the header, except when double mapping is enabled. In this case,
	 * if double mapping is enabled and arraylet was double mapped
	 * successfully the data pointer will point to the contiguous
	 * representation of the data; hence returning that pointer.
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 * @return data address associated with the Indexable object
	 */
	MMINLINE void *
	getDataAddrForIndexableObject(J9IndexableObject *arrayPtr)
	{
		ArrayLayout layout = getArrayLayout(arrayPtr);
		bool isDiscontiguous = (layout != InlineContiguous);

		return isDiscontiguous
			? getDataAddrForDiscontiguous(arrayPtr)
			: getDataAddrForContiguous(arrayPtr);
	}

	/**
	 * Asserts that the dataAddr field of the indexable object is correct.
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 */
	MMINLINE bool
	isCorrectDataAddr(J9IndexableObject *arrayPtr)
	{
		ArrayLayout layout = getArrayLayout(arrayPtr);
		bool isDiscontiguous = (layout != InlineContiguous);

		return isDiscontiguous
			? isCorrectDataAddrForDiscontiguousArraylet(arrayPtr)
			: isCorrectDataAddrForContiguousArraylet(arrayPtr);
	}

	/**
	 * Asserts that the dataAddr field of the contiguous arraylet is correct.
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 */
	MMINLINE bool
	isCorrectDataAddrForContiguousArraylet(J9IndexableObject *arrayPtr)
	{
		void *dataAddr = getDataAddrForContiguous(arrayPtr);
		return isCorrectDataAddrContiguous(arrayPtr, dataAddr);
	}

	/**
	 * Asserts that the dataAddr field of the indexable object is correct.
	 *
	 * @param arrayPtr      Pointer to the indexable object
	 */
	MMINLINE bool
	isCorrectDataAddrForDiscontiguousArraylet(J9IndexableObject *arrayPtr)
	{
		void *dataAddr = getDataAddrForDiscontiguous(arrayPtr);
		return isCorrectDataAddrDiscontiguous(arrayPtr, dataAddr);
	}
#endif /* J9VM_ENV_DATA64 */

	/**
	 * External fixup dataAddr API to update pointer of indexable objects.
	 * Used in concurrent GCs in case of mutator and GC thread races.
	 * Sets the dataAddr of either a contiguous or discomtiguous indexable
	 * object.
	 *
	 * @param forwardedHeader Forwarded header of arrayPtr
	 * @param arrayPtr        Pointer to the indexable object whose size is required
	 */
	MMINLINE void
	fixupDataAddr(MM_ForwardedHeader *forwardedHeader, omrobjectptr_t arrayPtr)
	{
#if defined(J9VM_ENV_DATA64)
		J9IndexableObject *j9ArrayPtr = (J9IndexableObject *)arrayPtr;

		ArrayLayout layout = getPreservedArrayLayout(forwardedHeader);
		bool isDiscontiguous = (layout != InlineContiguous);

		if (isDiscontiguous) {
			setDataAddrForDiscontiguous(j9ArrayPtr, NULL);
		} else {
			setDataAddrForContiguous(j9ArrayPtr);
		}
#endif /* J9VM_ENV_DATA64 */
	}
	/**
	 * External fixup dataAddr API to update pointer of indexable objects.
	 * Sets the dataAddr of either a contiguous or discomtiguous indexable
	 * object.
	 *
	 * @param arrayPtr      Pointer to the indexable object whose size is required
	 */
	MMINLINE void
	fixupDataAddr(omrobjectptr_t arrayPtr)
	{
#if defined(J9VM_ENV_DATA64)
		J9IndexableObject *j9ArrayPtr = (J9IndexableObject *)arrayPtr;
		AssertArrayPtrIsIndexable(j9ArrayPtr);

		ArrayLayout layout = getArrayLayout(j9ArrayPtr);
		bool isDiscontiguous = (layout != InlineContiguous);

		if (isDiscontiguous) {
			setDataAddrForDiscontiguous(j9ArrayPtr, NULL);
		} else {
			setDataAddrForContiguous(j9ArrayPtr);
		}
#endif /* J9VM_ENV_DATA64 */
	}

	/**
	 * Extract the class pointer from an unforwarded object.
	 *
	 * This method will assert if the object has been marked as forwarded.
	 *
	 * @param[in] pointer to forwardedHeader the MM_ForwardedHeader instance encapsulating the object
	 * @return pointer to the J9Class from the object encapsulated by forwardedHeader
	 * @see MM_ForwardingHeader::isForwardedObject()
	 */
	MMINLINE J9Class *
	getPreservedClass(MM_ForwardedHeader *forwardedHeader)
	{
		return (J9Class *)((uintptr_t)(forwardedHeader->getPreservedSlot()) & J9GC_J9OBJECT_CLAZZ_ADDRESS_MASK);
	}

	/**
	 * Get the preserved hashcode offset of forwarded header.
	 *
	 * @param[in]   Pointer to forwardedHeader the MM_ForwardedHeader instance encapsulating the object
	 */
	MMINLINE uintptr_t
	getPreservedHashcodeOffset(MM_ForwardedHeader *forwardedHeader)
	{
		J9Class *clazz = getPreservedClass(forwardedHeader);
		return getHashcodeOffset(clazz, getPreservedArrayLayout(forwardedHeader), getPreservedIndexableSize(forwardedHeader));
	}

	/**
	 * Extract the size (as getSizeInElements()) from an unforwarded object
	 *
	 * This method will assert if the object is not indexable or has been marked as forwarded.
	 *
	 * @param[in] forwardedHeader pointer to the MM_ForwardedHeader instance encapsulating the object
	 * @return the size (#elements) of the array encapsulated by forwardedHeader
	 * @see MM_ForwardingHeader::isForwardedObject()
	 */
	MMINLINE uint32_t
	getPreservedIndexableSize(MM_ForwardedHeader *forwardedHeader)
	{
		/* Asserts if object is indeed indexable */
		ForwardedHeaderAssert(J9GC_CLASS_IS_ARRAY(getPreservedClass(forwardedHeader)));

		/* in compressed headers, the size of the object is stored in the low-order half of the uintptr_t read when we read clazz
		 * so read it from there instead of the heap (since the heap copy would have been over-written by the forwarding
		 * pointer if another thread copied the object underneath us). In non-compressed, this field should still be readable
		 * out of the heap.
		 */
		uint32_t size = 0;
#if defined (OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			size = forwardedHeader->getPreservedOverlap();
		} else
#endif /* defined (OMR_GC_COMPRESSED_POINTERS) */
		{
			size = ((J9IndexableObjectContiguousFull *)forwardedHeader->getObject())->size;
		}

		if (0 == size) {
			/* Discontiguous */
			if (compressObjectReferences()) {
				size = ((J9IndexableObjectDiscontiguousCompressed *)forwardedHeader->getObject())->size;
			} else {
				size = ((J9IndexableObjectDiscontiguousFull *)forwardedHeader->getObject())->size;
			}
		}

		return size;
	}

	/**
	 * Extract the array layout from preserved info in Forwarded header
	 * (this mimics getArrayLayout())
	 *
	 * @param[in] forwardedHeader pointer to the MM_ForwardedHeader instance encapsulating the object
	 * @return the ArrayLayout for the forwarded object
	 */
	MMINLINE ArrayLayout
	getPreservedArrayLayout(MM_ForwardedHeader *forwardedHeader)
	{
		J9Class *clazz = getPreservedClass(forwardedHeader);
		/* Asserts if object is indeed indexable */
		ForwardedHeaderAssert(J9GC_CLASS_IS_ARRAY(clazz));

		ArrayLayout layout = InlineContiguous;
		uint32_t size = 0;
#if defined (OMR_GC_COMPRESSED_POINTERS)
		if (compressObjectReferences()) {
			size = forwardedHeader->getPreservedOverlap();
		} else
#endif /* defined (OMR_GC_COMPRESSED_POINTERS) */
		{
			size = ((J9IndexableObjectContiguousFull *)forwardedHeader->getObject())->size;
		}

		if (0 == size) {
			/* we know we are dealing with heap object, so we don't need to check against _arrayletRangeBase/Top, like getArrayLayout does */
			uintptr_t dataSizeInBytes = getDataSizeInBytes(clazz, getPreservedIndexableSize(forwardedHeader));
			layout = getArrayletLayout(clazz, dataSizeInBytes);
		}

		return layout;
	}

	/**
	 * Calculates object size in bytes with header and object's hashcode offset
	 *
	 * @param[in] forwardedHeader pointer to the MM_ForwardedHeader instance encapsulating the object
	 * @param[out] hashcodeOffset hashcode offset of object being moved
	 *
	 * @return object size in bytes with header
	 */
	MMINLINE uintptr_t
	calculateObjectSizeAndHashcode(MM_ForwardedHeader *forwardedHeader, uintptr_t *hashcodeOffset)
	{
		J9Class* clazz = getPreservedClass(forwardedHeader);
		uintptr_t numberOfElements = (uintptr_t)getPreservedIndexableSize(forwardedHeader);
		uintptr_t dataSizeInBytes = getDataSizeInBytes(clazz, numberOfElements);
		ArrayLayout layout = getArrayletLayout(clazz, dataSizeInBytes);
		*hashcodeOffset = getHashcodeOffset(clazz, layout, numberOfElements);
		return getSizeInBytesWithHeader(clazz, layout, numberOfElements);
	}

	/**
	 * Returns the header size of a given indexable object. The arraylet layout is determined base on "small" size.
	 * @param arrayPtr Ptr to an array for which header size will be returned
	 * @return Size of header in bytes
	 */
	MMINLINE uintptr_t
	getHeaderSize(J9IndexableObject *arrayPtr)
	{
		uintptr_t headerSize = 0;
		if (compressObjectReferences()) {
			uintptr_t size = ((J9IndexableObjectContiguousCompressed *)arrayPtr)->size;
			headerSize = sizeof(J9IndexableObjectContiguousCompressed);
			if (0 == size) {
				headerSize = sizeof(J9IndexableObjectDiscontiguousCompressed);
			}
		} else {
			uintptr_t size = ((J9IndexableObjectContiguousFull *)arrayPtr)->size;
			headerSize = sizeof(J9IndexableObjectContiguousFull);
			if (0 == size) {
				headerSize = sizeof(J9IndexableObjectDiscontiguousFull);
			}
		}
		return headerSize;
	}

	/**
	 * Returns the address of first arraylet leaf slot in the spine
	 * @param arrayPtr Ptr to an array
	 * @return Arrayoid ptr
	 */
	MMINLINE fj9object_t *
	getArrayoidPointer(J9IndexableObject *arrayPtr)
	{
		return (fj9object_t *)((uintptr_t)arrayPtr + (compressObjectReferences() ? sizeof(J9IndexableObjectDiscontiguousCompressed) : sizeof(J9IndexableObjectDiscontiguousFull)));
	}

	/**
	 * Returns the address of first data slot in the array
	 * @param arrayPtr Ptr to an array
	 * @return Address of first data slot in the array
	 */
	MMINLINE void *
	getDataPointerForContiguous(J9IndexableObject *arrayPtr)
	{
		return (void *)((uintptr_t)arrayPtr + contiguousHeaderSize());
	}


	/**
	 * Returns the offset of the hashcode slot, in bytes, from the beginning of the header.
	 * WARNING: This implementation assumes the arraylet layout is InlineContiguous.
	 * @param clazzPtr Pointer to the class of the object
	 * @param numberOfElements size from indexable object
	 * @return offset of the hashcode slot
	 */
	MMINLINE uintptr_t
	getHashcodeOffset(J9Class *clazzPtr, uintptr_t numberOfElements)
	{
		ArrayLayout layout = InlineContiguous; 
		if(0 == numberOfElements) {
			layout = Discontiguous;
		}
		return getHashcodeOffset(clazzPtr, layout, numberOfElements);
	}

	/**
	 * Returns the offset of the hashcode slot, in bytes, from the beginning of the header.
	 * @param clazzPtr Pointer to the class of the object
	 * @param layout The layout of the indexable object
	 * @param numberOfElements size from indexable object
	 * @return offset of the hashcode slot
	 */
	MMINLINE uintptr_t
	getHashcodeOffset(J9Class *clazzPtr, ArrayLayout layout, uintptr_t numberOfElements)
	{
		/* Don't call getDataSizeInBytes() since that rounds up to uintptr_t. */
		uintptr_t dataSize = numberOfElements * J9ARRAYCLASS_GET_STRIDE(clazzPtr);
		uintptr_t numberOfArraylets = numArraylets(dataSize);
		bool alignData = shouldAlignSpineDataSection(clazzPtr);
		uintptr_t spineSize = getSpineSize(clazzPtr, layout, numberOfArraylets, dataSize, alignData);
		return MM_Math::roundToSizeofU32(spineSize);
	}

	/**
	 * Returns the offset of the hashcode slot, in bytes, from the beginning of the header.
	 * @param arrayPtr Pointer to the array
	 * @return offset of the hashcode slot
	 */
	MMINLINE uintptr_t
	getHashcodeOffset(J9IndexableObject *arrayPtr)
	{
		ArrayLayout layout = getArrayLayout(arrayPtr);
		return getHashcodeOffset(J9GC_J9OBJECT_CLAZZ(arrayPtr, this), layout, getSizeInElements(arrayPtr));
	}
	
	MMINLINE void
	expandArrayletSubSpaceRange(MM_MemorySubSpace * subSpace, void * rangeBase, void * rangeTop, uintptr_t largestDesirableArraySpineSize)
	{
		GC_ArrayletObjectModelBase::expandArrayletSubSpaceRange(subSpace, rangeBase, rangeTop, largestDesirableArraySpineSize);
	}

	/**
	 * Updates leaf pointers that point to an address located within the indexable object.  For example,
	 * when the array layout is either inline continuous or hybrid, there will be leaf pointers that point
	 * to data that is contained within the indexable object.  These internal leaf pointers are updated by
	 * calculating their offset within the source arraylet, then updating the destination arraylet pointers
	 * to use the same offset.
	 *
	 * @param destinationPtr Pointer to the new indexable object
	 * @param sourcePtr      Pointer to the original indexable object that was copied
	 */
	void fixupInternalLeafPointersAfterCopy(J9IndexableObject *destinationPtr, J9IndexableObject *sourcePtr);

	/**
	 * Initialize the receiver, a new instance of GC_ObjectModel
	 * 
	 * @return true on success, false on failure
	 */
	bool initialize(MM_GCExtensionsBase *extensions);
	
	/**
	 * Tear down the receiver
	 */
	void tearDown(MM_GCExtensionsBase *extensions);

	/**
	 * Asserts that an Arraylet has indeed discontiguous layout
	 */
	void AssertArrayletIsDiscontiguous(J9IndexableObject *objPtr);

	/**
	 * Asserts that an Arraylet has true contiguous layout
	 */
	void AssertContiguousArrayletLayout(J9IndexableObject *objPtr);

	/**
	 * Asserts that an Arraylet has true discontiguous layout
	 */
	void AssertDiscontiguousArrayletLayout(J9IndexableObject *objPtr);
};
#endif /* ARRAYLETOBJECTMODEL_ */

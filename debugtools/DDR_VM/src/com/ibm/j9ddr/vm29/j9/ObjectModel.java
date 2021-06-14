/*******************************************************************************
 * Copyright (c) 1991, 2018 IBM Corp. and others
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
package com.ibm.j9ddr.vm29.j9;

import static com.ibm.j9ddr.vm29.events.EventManager.raiseCorruptDataEvent;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionDescriptor;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionIterator;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionManager;
import com.ibm.j9ddr.vm29.j9.gc.GCObjectModel;
import com.ibm.j9ddr.vm29.pointer.AbstractPointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9JavaVMPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_GCExtensionsPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_HeapRegionManagerPointer;
import com.ibm.j9ddr.vm29.types.I32;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.UDATA;

public final class ObjectModel
{
	protected static final GCObjectModel gcObjectModel;

	/* Do not instantiate. Static behaviour only. */
	private ObjectModel()
	{
	}
	
	static 
	{
		GCObjectModel objectModel = null; 
		try {
			objectModel = GCObjectModel.from();
		} catch (CorruptDataException cde) {
			raiseCorruptDataEvent("Error initializing the object model", cde, true);
			objectModel = null;
		}
		gcObjectModel = objectModel;
	}
	
	/**
	 * Object size should be at least minimumConsumedSize
	 * and 8 byte aligned.
	 * @param sizeInBytes Real size of an object
	 * @return Adjusted size
	 */
	public static UDATA adjustSizeInBytes(UDATA sizeInBytes)
	{
		return gcObjectModel.adjustSizeInBytes(sizeInBytes);
	}
	
	/**
	 * Object size should be at least minimumConsumedSize
	 * and 8 byte aligned.
	 * @param sizeInBytes Real size of an object
	 * @return Adjusted size
	 */
	public static long getObjectAlignmentInBytes()
	{
		return gcObjectModel.getObjectAlignmentInBytes();
	}
	
	/**
	 * Returns the shape of an object.
	 * @param objectPtr Pointer to object whose shape is required.
	 * @return The shape of the object
	 * @throws CorruptDataException 
	 */
	public static UDATA getClassShape(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getClassShape(object);
	}
	
	/**
	 * Returns the shape of an object.
	 * @param clazz Pointer to J9Class whose shape is required.
	 * @return The shape of the object
	 * @throws CorruptDataException 
	 */
	public static UDATA getClassShape(J9ClassPointer clazz) throws CorruptDataException
	{
		return gcObjectModel.getClassShape(clazz);
	}

	/**
	 * Returns TRUE if an object is indexable, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is indexable, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isIndexable(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.isIndexable(object);
	}
	
	/**
	 * Returns TRUE if an class is indexable, FALSE otherwise.
	 * @param clazz Pointer to an J9Class
	 * @return TRUE if an class is indexable, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isIndexable(J9ClassPointer clazz) throws CorruptDataException
	{
		return gcObjectModel.isIndexable(clazz);
	}

	/**
	 * Returns TRUE if an object is dead, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is dead, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isDeadObject(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.isDeadObject(object);
	}

	/**
	 * Returns TRUE if an object is a dead single slot object, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is a dead single slot object, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isSingleSlotDeadObject(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.isSingleSlotDeadObject(object);
	}

	/**
	 * Returns the size, in bytes, of a multi-slot dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size, in bytes, of a multi-slot dead object
	 * @throws CorruptDataException 
	 */
	public static UDATA getSizeInBytesMultiSlotDeadObject(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getSizeInBytesMultiSlotDeadObject(object);
	}

	/**
	 * Returns the size, in bytes, of a single slot dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size, in bytes, of a single slot dead object
	 */
	public static UDATA getSizeInBytesSingleSlotDeadObject(J9ObjectPointer object)
	{
		return gcObjectModel.getSizeInBytesSingleSlotDeadObject(object);
	}

	/**
	 * Returns the size in bytes of a dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size in bytes of a dead object
	 * @throws CorruptDataException 
	 */
	public static UDATA getSizeInBytesDeadObject(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getSizeInBytesDeadObject(object);
	}

	/**
	 * Returns the size of an object, in bytes, including the header.
	 * @param objectPtr Pointer to an object
	 * @return The size of an object, in bytes, including the header
	 * @throws CorruptDataException 
	 */
	public static UDATA getSizeInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getSizeInBytesWithHeader(object);
	}

	/**
	 * Get the total footprint of an object, in bytes, including the object header and all data.
	 * If the object has a discontiguous representation, this method should return the size of
	 * the root object plus the total of all the discontiguous parts of the object.
	 * @param object Pointer to an object
	 * @return the total size of an object, in bytes, including discontiguous parts
	 * @throws CorruptDataException 
	 */
	public static UDATA getTotalFootprintInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getTotalFootprintInBytesWithHeader(object);
	}

	/**
	 * Same as getSizeInBytesWithHeader,
	 * except it takes into account
	 * object alignment and minimum object size
	 * @param objectPtr Pointer to an object
	 * @return The consumed heap size of an object, in bytes, including the header
	 * @throws CorruptDataException 
	 */
	public static UDATA getConsumedSizeInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getConsumedSizeInBytesWithHeader(object);
	}

	/**
	 * Returns the size of an object, in slots, including the header.
	 * @param objectPtr Pointer to an object
	 * @return The size of an object, in slots, including the header
	 * @throws CorruptDataException 
	 */
	public UDATA getSizeInSlotsWithHeader(J9ObjectPointer object) throws CorruptDataException
	{
		return UDATA.convertBytesToSlots(getSizeInBytesWithHeader(object));
	}
	
	/**
	 * Same as getSizeInSlotsWithHeader,
	 * except it takes into account
	 * object alignment and minimum object size
	 * @param objectPtr Pointer to an object
	 * @return The consumed heap size of an object, in slots, including the header
	 * @throws CorruptDataException 
	 */
	public UDATA getConsumedSizeInSlotsWithHeader(J9ObjectPointer object) throws CorruptDataException
	{
		return UDATA.convertBytesToSlots(getConsumedSizeInBytesWithHeader(object));
	}
	
	/**
	 * Returns the age of an object.
	 * @param objectPtr Pointer to an object
	 * @return The age of the object
	 * @throws CorruptDataException 
	 */
	public static U32 getAge(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getAge(object);
	}
	

	/**
	 * Returns TRUE if an object is remembered, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is remembered, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isRemembered(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.isRemembered(object);
	}

	/**
	 * Returns the collector bits from object's header.
	 * @param objectPtr Pointer to an object
	 * @return collector bits
	 */
	public static UDATA getRememberedBits(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getRememberedBits(object);
	}
	
	/**
	 * Returns TRUE if an object is old, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is in the old area, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean isOld(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.isOld(object);
	}
	
	/**
	 * Returns the size of an indexable object in elements.
	 * @param array Pointer to the indexable object whose size is required
	 * @return Size of object in elements
	 * @throws IllegalArgumentException if the object is not an array 
	 * @throws CorruptDataException 
	 */
	public static UDATA getSizeInElements(J9ObjectPointer object) throws IllegalArgumentException, CorruptDataException
	{
		return gcObjectModel.getSizeInElements(object);
	}
	
	// GC_ObjectModel$ScanType
	public static long getScanType(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getScanType(object);
	}
	
	/**
	 * Returns TRUE if an object has been hashed, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object has been hashed, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean hasBeenHashed(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.hasBeenHashed(object);
	}
	
	/**
	 * Returns TRUE if an object has been moved after being hashed, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object has been moved after being hashed, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public static boolean hasBeenMoved(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.hasBeenMoved(object);
	}
	
	/**
	 * Determine the basic hash code for the specified object. 
	 * 
	 * @param object[in] the object to be hashed
	 * @return the persistent, basic hash code for the object 
	 * @throws CorruptDataException 
	 */
	public static I32 getObjectHashCode(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getObjectHashCode(object);
	}
	
	public static UDATA getHashcodeOffset(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getHashcodeOffset(object);
	}

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves, excluding the header.
	 *
	 * @param array pointer to the indexable object whose size is required
	 * @return the size of an object in bytes excluding the header
	 * @throws CorruptDataException 
	 */
	public static UDATA getDataSizeInBytes(J9IndexableObjectPointer array) throws CorruptDataException {
		return gcObjectModel.getDataSizeInBytes(array);
	}

	/**
	 * Returns the size of an object header, in bytes.
	 * <p>
	 * @param objectPtr Pointer to an object
	 * @return The size of an object header, in bytes.
	 */
	public static UDATA getHeaderSize(J9ObjectPointer object) throws CorruptDataException
	{
		return gcObjectModel.getHeaderSize(object);
	}

	/**
	 * Returns the address of an element.
	 * @param indexableObjectPointer Pointer to a J9 indexable object
	 * @param elementIndex Index of the element
	 * @param elementSize Size of the element
	 * @return The address of an element.
	 * @throws CorruptDataException
	 */
	public static VoidPointer getElementAddress(J9IndexableObjectPointer indexableObjectPointer, int elementIndex, int elementSize) throws CorruptDataException
	{
		return gcObjectModel.getElementAddress(indexableObjectPointer, elementIndex, elementSize);
	}
	
	/**
	 * Returns the heap region of a pointer.
	 * @param javaVM J9JavaVMPointer
	 * @param hrm The gc heap region manager
	 * @param pointer Any abstract pointer
	 * @param region A gc heap region descriptor
	 * @return The heap region descriptor of the given pointer.
	 */
	public static GCHeapRegionDescriptor findRegionForPointer(J9JavaVMPointer javaVM, GCHeapRegionManager hrm, AbstractPointer pointer, GCHeapRegionDescriptor region)
	{
		GCHeapRegionDescriptor regionDesc = null;

		if (region != null && region.isAddressInRegion(pointer)) {
			return region;
		}

		regionDesc = regionForAddress(javaVM, hrm, pointer);
		if (null != regionDesc) {
			return regionDesc;
		}

		// TODO kmt : this is tragically slow
		try {
			GCHeapRegionIterator iterator = GCHeapRegionIterator.from();
			while (iterator.hasNext()) {
				regionDesc = GCHeapRegionDescriptor.fromHeapRegionDescriptor(iterator.next());
				if (isPointerInRegion(pointer, regionDesc)) {
					return regionDesc;
				}
			}
		} catch (CorruptDataException e) {
		}
		return null;
	}
	
	/**
	 * Returns the heap region for address of a pointer.
	 * @param javaVM J9JavaVMPointer
	 * @param hrm The gc heap region manager
	 * @param pointer Any abstract pointer
	 * @return The heap region descriptor for address of a pointer.
	 */
	public static GCHeapRegionDescriptor regionForAddress(J9JavaVMPointer javaVM, GCHeapRegionManager hrm, AbstractPointer pointer)
	{
		try {
			if (null == hrm) {
				MM_HeapRegionManagerPointer hrmPtr = MM_GCExtensionsPointer.cast(javaVM.gcExtensions())
						.heapRegionManager();
				hrm = GCHeapRegionManager.fromHeapRegionManager(hrmPtr);
			}
			return hrm.regionDescriptorForAddress(pointer);
		} catch (CorruptDataException cde) {
		}
		return null;
	}

	/**
	 * Returns true if a pointer is in stored in specified region.
	 * @param pointer Any abstract pointer
	 * @param region Descriptor for a region in heap
	 * @return If the AbstractPointer pointer is in stored in the given region in heap.
	 */
	public static boolean isPointerInRegion(AbstractPointer pointer, GCHeapRegionDescriptor region)
	{
		return pointer.gte(region.getLowAddress()) && pointer.lt(region.getHighAddress());
	}
	
	/**
	 * Returns true if a pointer is in stored in heap.
	 * @param javaVM J9JavaVMPointer
	 * @param pointer Any abstract pointer
	 * @return If the AbstractPointer pointer is in stored in heap.
	 */
	public static boolean isPointerInHeap(J9JavaVMPointer javaVM, AbstractPointer pointer)
	{
		if (findRegionForPointer(javaVM, null, pointer, null) != null) {
			return true;
		} else {
			return false;
		}
	}

	/**
	 * @param arrayPtr array object who's data address validity we are checking
	 * @throws CorruptDataException 
	 */
	public static boolean isCorrectDataPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return gcObjectModel.isCorrectDataPointer(arrayPtr);
	}
}


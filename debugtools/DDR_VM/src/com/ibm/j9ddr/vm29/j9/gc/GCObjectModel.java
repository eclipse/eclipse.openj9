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
package com.ibm.j9ddr.vm29.j9.gc;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.j9.AlgorithmVersion;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ObjectPointer;
import com.ibm.j9ddr.vm29.types.I32;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.UDATA;

public abstract class GCObjectModel extends GCBase
{
	protected GCMixedObjectModel mixedObjectModel;
	protected GCArrayObjectModel indexableObjectModel;	
	
	/* Do not instantiate. Use the factory */
	protected GCObjectModel() throws CorruptDataException 
	{
		this.mixedObjectModel = GCMixedObjectModel.from();
		this.indexableObjectModel = GCArrayObjectModel.from();	
	}

	/**
	 * Factory method to construct an appropriate object model.
	 * @param structure the J9JavaVM structure to use
	 * 
	 * @return an instance of ObjectModel 
	 * @throws CorruptDataException 
	 */
	public static GCObjectModel from() throws CorruptDataException
	{
		AlgorithmVersion version = AlgorithmVersion.getVersionOf(AlgorithmVersion.GC_OBJECT_MODEL_VERSION);
		switch (version.getAlgorithmVersion()) {
			// Add case statements for new algorithm versions
			default:
				return new GCObjectModel_V1();
		}
	}
	
	/**
	 * Object size should be at least minimumConsumedSize
	 * and 8 byte aligned.
	 * @param sizeInBytes Real size of an object
	 * @return Adjusted size
	 */
	public abstract UDATA adjustSizeInBytes(UDATA sizeInBytes);
	
	/**
	 * Returns the shape of an object.
	 * @param objectPtr Pointer to object whose shape is required.
	 * @return The shape of the object
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getClassShape(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the shape of a class.
	 * @param objectPtr Pointer to J9Object whose shape is required.
	 * @return The shape of the class
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getClassShape(J9ClassPointer clazz) throws CorruptDataException;
	
	/**
	 * Returns TRUE if an object is indexable, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is indexable, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isIndexable(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns TRUE if an class is indexable, FALSE otherwise.
	 * @param clazz Pointer to an J9Class
	 * @return TRUE if an class is indexable, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isIndexable(J9ClassPointer clazz) throws CorruptDataException;

	/**
	 * Returns TRUE if an object is dead, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is dead, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isDeadObject(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns TRUE if an object is a dead single slot object, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is a dead single slot object, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isSingleSlotDeadObject(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the size, in bytes, of a multi-slot dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size, in bytes, of a multi-slot dead object
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getSizeInBytesMultiSlotDeadObject(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the size, in bytes, of a single slot dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size, in bytes, of a single slot dead object
	 */
	public abstract UDATA getSizeInBytesSingleSlotDeadObject(J9ObjectPointer object);

	/**
	 * Returns run-time object alignment in bytes.
	 * @return The alignment in bytes
	 */
	public abstract long getObjectAlignmentInBytes();

	/**
	 * Returns the size in bytes of a dead object.
	 * @param objectPtr Pointer to an object
	 * @return The size in bytes of a dead object
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getSizeInBytesDeadObject(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the size of an object, in bytes, including the header.
	 * @param objectPtr Pointer to an object
	 * @return The size of an object, in bytes, including the header
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getSizeInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Get the total footprint of an object, in bytes, including the object header and all data.
	 * If the object has a discontiguous representation, this method should return the size of
	 * the root object plus the total of all the discontiguous parts of the object.
	 * @param object Pointer to an object
	 * @return the total size of an object, in bytes, including discontiguous parts
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getTotalFootprintInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Same as getSizeInBytesWithHeader,
	 * except it takes into account
	 * object alignment and minimum object size
	 * @param objectPtr Pointer to an object
	 * @return The consumed heap size of an object, in bytes, including the header
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getConsumedSizeInBytesWithHeader(J9ObjectPointer object) throws CorruptDataException;

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
	public abstract U32 getAge(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns TRUE if an object is remembered, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is remembered, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isRemembered(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the collector bits from object's header.
	 * @param objectPtr Pointer to an object
	 * @return collector bits
	 */
	public abstract UDATA getRememberedBits(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns TRUE if an object is old, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object is in the old area, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean isOld(J9ObjectPointer object) throws CorruptDataException;	
	
	/**
	 * Returns the size of an indexable object in elements.
	 * @param array Pointer to the indexable object whose size is required
	 * @return Size of object in elements
	 * @throws IllegalArgumentException if the object is not an array 
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getSizeInElements(J9ObjectPointer object) throws IllegalArgumentException, CorruptDataException;
	
	// GC_ObjectModel$ScanType
	public abstract long getScanType(J9ObjectPointer object) throws CorruptDataException;
	
	/**
	 * Returns TRUE if an object has been hashed, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object has been hashed, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean hasBeenHashed(J9ObjectPointer object) throws CorruptDataException;
	
	/**
	 * Returns TRUE if an object has been moved after being hashed, FALSE otherwise.
	 * @param objectPtr Pointer to an object
	 * @return TRUE if an object has been moved after being hashed, FALSE otherwise
	 * @throws CorruptDataException 
	 */
	public abstract boolean hasBeenMoved(J9ObjectPointer object) throws CorruptDataException;
	
	/**
	 * Determine the basic hash code for the specified object. 
	 * 
	 * @param object[in] the object to be hashed
	 * @return the persistent, basic hash code for the object 
	 * @throws CorruptDataException 
	 */
	public abstract I32 getObjectHashCode(J9ObjectPointer object) throws CorruptDataException;
	
	public abstract UDATA getHashcodeOffset(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves, excluding the header.
	 * @param array Pointer to the indexable object whose size is required
	 * @return The size of an object in bytes excluding the header
	 * @throws CorruptDataException 
	 */
	public UDATA getDataSizeInBytes(J9IndexableObjectPointer array) throws CorruptDataException {
		return indexableObjectModel.getDataSizeInBytes(array);
	}

	/**
	 * Returns the size of an object header, in bytes.
	 * @param objectPtr Pointer to an object
	 * @return The size of an object header, in bytes.
	 */
	public abstract UDATA getHeaderSize(J9ObjectPointer object) throws CorruptDataException;

	/**
	 * Returns the address of the element at elementIndex logical offset into indexableObjectPointer, assuming that each
	 * element is dataSize bytes.
	 * 
	 * @param indexableObjectPointer The array from which we should look-up the address
	 * @param elementIndex The logical index number of the element within the array
	 * @param elementSize The size, in bytes, of a single element of the array
	 * @return A pointer to the element
	 * @throws CorruptDataException 
	 */
	public abstract VoidPointer getElementAddress(J9IndexableObjectPointer indexableObjectPointer, int elementIndex, int elementSize) throws CorruptDataException;

	
	/**
	 * @param arrayPtr array object who's data address validity we are checking
	 * @throws CorruptDataException 
	 */
	public boolean isValidDataAddressPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return indexableObjectModel.isValidDataAddressPointer(arrayPtr);
	}
}

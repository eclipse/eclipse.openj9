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
package com.ibm.j9ddr.vm29.j9.gc;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.j9.AlgorithmVersion;
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.helper.J9IndexableObjectHelper;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.UDATA;


abstract class GCArrayObjectModel extends GCBase
{
	/**
	 * Factory method to construct an appropriate array object model.
	 * @param structure the J9JavaVM structure to use
	 * @return an instance of ArrayObjectModel 
	 * @throws CorruptDataException 
	 */
	public static GCArrayObjectModel from() throws CorruptDataException
	{
		AlgorithmVersion version = AlgorithmVersion.getVersionOf(AlgorithmVersion.GC_ARRAYLET_OBJECT_MODEL_VERSION);
		switch (version.getAlgorithmVersion()) {
			// Add case statements to handle new algorithm versions
			case 0:
			case 1:
				return new GCArrayletObjectModel_V1();
			default:
				return new GCArrayletObjectModel_V2();
		}
	}

	/**
	 * Returns the size of an indexable object, in bytes, including the header.
	 * @param array Pointer to the indexable object whose size is required
	 * @return Size of object in bytes including the header
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getSizeInBytesWithHeader(J9IndexableObjectPointer array) throws CorruptDataException;

	/**
	 * Returns the size of an indexable object, in slots, including the header.
	 * @param array Pointer to the indexable object whose size is required
	 * @return Size of object in slots including the header
	 * @throws CorruptDataException 
	 */	
	public UDATA getSizeInSlotsWithHeader(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return UDATA.convertBytesToSlots(getSizeInBytesWithHeader(array));
	}
	
	/**
	 * Returns the size of an indexable object in elements.
	 * @param array Pointer to the indexable object whose size is required
	 * @return Size of object in elements
	 * @throws CorruptDataException 
	 */
	public UDATA getSizeInElements(J9IndexableObjectPointer array) throws CorruptDataException
	{
		U32 size = J9IndexableObjectHelper.size(array);
		if (size.anyBitsIn(0x80000000)) {
			throw new CorruptDataException("java array size with sign bit set");
		}
		return new UDATA(size); 
	}

	/**
	 * Returns the offset of the hashcode slot, in bytes, from the beginning of the header.
	 * @param arrayPtr Pointer to the object
	 * @return offset of the hashcode slot
	 */	
	public abstract UDATA getHashcodeOffset(J9IndexableObjectPointer array) throws CorruptDataException;

	/**
	 * Returns the header size of a given indexable object.
	 * @param arrayPtr Pointer to an array for which header size will be returned
	 * @return Size of header in bytes
	 */	
	public abstract UDATA getHeaderSize(J9IndexableObjectPointer array) throws CorruptDataException;

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves, excluding the header.
	 * @param arrayPtr Pointer to the indexable object whose size is required
	 * @return Size of object in bytes excluding the header
	 */
	public abstract UDATA getDataSizeInBytes(J9IndexableObjectPointer array) throws CorruptDataException;

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
	public abstract VoidPointer getElementAddress(J9IndexableObjectPointer indexableObjectPointer, int elementIndex,int elementSize) throws CorruptDataException;
	
	/**
	 * Returns the address of first data slot in the array
	 * @param arrayPtr pointer to an array
	 * @return Address of first data slot in the array
	 */		
	public abstract VoidPointer getDataPointerForContiguous(J9IndexableObjectPointer arrayPtr) throws CorruptDataException;
	
	/**
	 * @param arrayPtr array object who's data address validity we are checking
	 * @throws CorruptDataException 
	 */
	public abstract boolean isCorrectDataPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException;
	
	/**
	 * Returns the address of first arraylet leaf slot in the spine
	 * @param arrayPtr Ptr to an array
	 * @return Arrayoid ptr
	 */	
	public abstract ObjectReferencePointer getArrayoidPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException;

	/**
	 * Check the given indexable object is inline contiguous
	 * @param objPtr Pointer to an array object
	 * @return true of array is inline contiguous
	 */	
	public abstract boolean isInlineContiguousArraylet(J9IndexableObjectPointer array) throws CorruptDataException;

	/**
	 * Get the total footprint of an object, in bytes, including the object header and all data.
	 * If the object has a discontiguous representation, this method should return the size of
	 * the root object plus the total of all the discontiguous parts of the object.
	 * @param arrayPtr Pointer to an object
	 * @return the total size of an object, in bytes, including discontiguous parts
	 * @throws CorruptDataException 
	 */
	public abstract UDATA getTotalFootprintInBytesWithHeader(J9IndexableObjectPointer arrayPtr) throws CorruptDataException;
}

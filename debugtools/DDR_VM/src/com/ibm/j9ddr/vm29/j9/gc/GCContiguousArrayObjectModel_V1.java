/*******************************************************************************
 * Copyright (c) 1991, 2020 IBM Corp. and others
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
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ArrayClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ROMArrayClassPointer;
import com.ibm.j9ddr.vm29.pointer.helper.J9IndexableObjectHelper;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectContiguous;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectDiscontiguous;
import com.ibm.j9ddr.vm29.types.UDATA;

class GCContiguousArrayObjectModel_V1 extends GCArrayObjectModel 
{
	private static UDATA getSizeInBytesWithoutHeader(J9ArrayClassPointer clazz, J9IndexableObjectPointer array) throws CorruptDataException 
	{
		J9ROMArrayClassPointer romArrayClass = J9ROMArrayClassPointer.cast(clazz.romClass());
		return UDATA.roundToSizeofUDATA(new UDATA(J9IndexableObjectHelper.size(array)).leftShift(romArrayClass.arrayShape().bitAnd(0xFFFF).intValue()));
	}

	private static UDATA getSizeInBytesWithoutHeader(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return getSizeInBytesWithoutHeader(J9IndexableObjectHelper.clazz(array), array);
	}

	public UDATA getSizeInBytesWithHeader(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return getSizeInBytesWithoutHeader(array).add(new UDATA(J9IndexableObjectHelper.contiguousHeaderSize()));
	}

	public UDATA getHashcodeOffset(J9IndexableObjectPointer array) throws CorruptDataException
	{
		J9ArrayClassPointer clazz = J9IndexableObjectHelper.clazz(array);
		UDATA numberOfElements = getSizeInElements(array);
		J9ROMArrayClassPointer romArrayClass = J9ROMArrayClassPointer.cast(clazz.romClass());
		UDATA size = numberOfElements.leftShift(romArrayClass.arrayShape().bitAnd(0xFFFF).intValue());
		size = size.add(J9IndexableObjectHelper.contiguousHeaderSize());
		return UDATA.roundToSizeofU32(size);
	}
	
	public UDATA getHeaderSize(J9IndexableObjectPointer array)
	{
		return new UDATA(J9IndexableObjectHelper.discontiguousHeaderSize());
	}

	/**
	 * Returns the size of data in an indexable object, in bytes, including leaves, excluding the header.
	 * @param arrayPtr Pointer to the indexable object whose size is required
	 * @return Size of object in bytes excluding the header
	 */
	@Override
	public UDATA getDataSizeInBytes(J9IndexableObjectPointer array) throws CorruptDataException {
		return getSizeInBytesWithoutHeader(array);
	}

	public VoidPointer getElementAddress(J9IndexableObjectPointer indexableObjectPointer, int elementIndex, int elementSize) throws CorruptDataException
	{
		return VoidPointer.cast(indexableObjectPointer.addOffset(getHeaderSize(indexableObjectPointer).longValue() + (elementIndex * elementSize)));
	}

	@Override
	public VoidPointer getDataPointerForContiguous(J9IndexableObjectPointer arrayPtr) throws CorruptDataException 
	{
		return VoidPointer.cast(arrayPtr.addOffset(getHeaderSize(arrayPtr)));
	}

	@Override
	public ObjectReferencePointer getArrayoidPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException {
		throw new IllegalArgumentException("Not supported");
	}

	@Override
	public boolean isInlineContiguousArraylet(J9IndexableObjectPointer array) throws CorruptDataException {
		return true;
	}

	@Override
	public UDATA getTotalFootprintInBytesWithHeader(J9IndexableObjectPointer arrayPtr) throws CorruptDataException {
		return getSizeInBytesWithHeader(arrayPtr);
	}

	@Override
	public boolean isValidDataAddressPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return true;
	}
}

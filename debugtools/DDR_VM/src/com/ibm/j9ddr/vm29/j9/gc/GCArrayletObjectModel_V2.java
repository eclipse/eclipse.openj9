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
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.helper.J9IndexableObjectHelper;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.types.UDATA;

class GCArrayletObjectModel_V2 extends GCArrayletObjectModelBase
{
	
	public GCArrayletObjectModel_V2() throws CorruptDataException 
	{		
	}

	@Override
	public UDATA getSizeInBytesWithHeader(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return super.getSpineSize(array);
	}

	/**
	 * Return the total number of arraylets for an indexable object with a size of dataInSizeByte, including a (possibly empty) leaf in the spine.
	 * @param dataSizeInBytes size of an array in bytes (not elements)
	 * @return the number of arraylets used for an array of dataSizeInBytes bytes
	 */
	@Override
	protected UDATA numArraylets(UDATA dataSizeInBytes) throws CorruptDataException
	{
		UDATA numberOfArraylets = new UDATA(1);
		if (!UDATA.MAX.eq(arrayletLeafSize)) {
			/* CMVC 135307 : following logic for calculating the leaf count would not overflow dataSizeInBytes.
			 * the assumption is leaf size is order of 2. It's identical to:
			 * if (dataSizeInBytes % leafSize) is 0
			 * 	leaf count = dataSizeInBytes >> leafLogSize
			 * else
			 * 	leaf count = (dataSizeInBytes >> leafLogSize) + 1
			 */
			numberOfArraylets = dataSizeInBytes.rightShift(arrayletLeafLogSize).add(dataSizeInBytes.bitAnd(arrayletLeafSizeMask).add(arrayletLeafSizeMask).rightShift(arrayletLeafLogSize));
		}
		return numberOfArraylets;
	}

	@Override
	public UDATA getTotalFootprintInBytesWithHeader(J9IndexableObjectPointer arrayPtr) throws CorruptDataException {
		UDATA spineSize = getSizeInBytesWithHeader(arrayPtr);
		UDATA externalArrayletSize = externalArrayletsSize(arrayPtr);
		UDATA totalFootprint = spineSize.add(externalArrayletSize);
		return totalFootprint;
	}

	@Override
	public VoidPointer getDataPointerForContiguous(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return super.getDataPointerForContiguous(arrayPtr);
	}

	public VoidPointer getDataPointerForDiscontiguous(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		/* TODO */
		return VoidPointer.cast(arrayPtr.addOffset(J9IndexableObjectHelper.discontiguousHeaderSize()));
	}

	@Override
	public boolean isValidDataAddressPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		boolean isValid = true;
		if(super.isInlineContiguousArraylet(arrayPtr)) {
			isValid = (getDataPointerForContiguous(arrayPtr) == VoidPointer.cast(arrayPtr.addOffset(J9IndexableObjectHelper.contiguousHeaderSize())));
		} else {
			isValid = (getDataPointerForDiscontiguous(arrayPtr) == VoidPointer.cast(arrayPtr.addOffset(J9IndexableObjectHelper.discontiguousHeaderSize())));
		}
		
		return isValid;
	}
}

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
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ArrayClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.types.UDATA;

class GCArrayletObjectModel_V1 extends GCArrayletObjectModelBase
{
	
	public GCArrayletObjectModel_V1() throws CorruptDataException 
	{		
	}
	
	@Override
	protected long getArrayLayout(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return super.getArrayLayout(array);
	}
	
	@Override
	protected long getArrayLayout(J9ArrayClassPointer clazz, UDATA dataSizeInBytes) throws CorruptDataException
	{
		return super.getArrayLayout(clazz, dataSizeInBytes);
	}
	
	@Override
	public UDATA getHeaderSize(J9IndexableObjectPointer array) throws CorruptDataException
	{		
		return super.getHeaderSize(array);
	}

	@Override
	protected UDATA getSpineSize(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return super.getSpineSize(array);
	}

	@Override
	public UDATA getSizeInBytesWithHeader(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return getSpineSize(array);
	}
	
	@Override
	public UDATA getHashcodeOffset(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return super.getHashcodeOffset(array);
	}
	
	@Override
	public UDATA getDataSizeInBytes(J9IndexableObjectPointer array) throws CorruptDataException
	{
		return super.getDataSizeInBytes(array);
	}
	
	@Override
	public ObjectReferencePointer getArrayoidPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException 
	{
		return super.getArrayoidPointer(arrayPtr);
	}
	
	@Override
	public VoidPointer getDataPointerForContiguous(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return super.getDataPointerForContiguous(arrayPtr);
	}

	@Override
	public VoidPointer getElementAddress(J9IndexableObjectPointer array, int elementIndex, int elementSize) throws CorruptDataException
	{	
		return super.getElementAddress(array, elementIndex, elementSize);
	}

	@Override
	public UDATA getTotalFootprintInBytesWithHeader(J9IndexableObjectPointer arrayPtr) throws CorruptDataException {
		UDATA spineSize = getSizeInBytesWithHeader(arrayPtr);
		UDATA externalArrayletSize = externalArrayletsSize(arrayPtr);
		UDATA totalFootprint = spineSize.add(externalArrayletSize);
		return totalFootprint;
	}

	@Override
	public boolean isCorrectDataPointer(J9IndexableObjectPointer arrayPtr) throws CorruptDataException
	{
		return true;
	}
}

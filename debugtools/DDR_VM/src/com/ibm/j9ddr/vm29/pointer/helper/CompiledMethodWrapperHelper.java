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
package com.ibm.j9ddr.vm29.pointer.helper;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.pointer.U8Pointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ShrOffsetPointer;
import com.ibm.j9ddr.vm29.pointer.generated.CompiledMethodWrapperPointer;
import com.ibm.j9ddr.vm29.pointer.I32Pointer;
import com.ibm.j9ddr.vm29.pointer.PointerPointer;
import com.ibm.j9ddr.vm29.structure.CompiledMethodWrapper;
import com.ibm.j9ddr.vm29.structure.ShcItem;
import com.ibm.j9ddr.vm29.types.UDATA;
import com.ibm.j9ddr.vm29.types.IDATA;

public class CompiledMethodWrapperHelper {
	public static U8Pointer CMWROMMETHOD(CompiledMethodWrapperPointer ptr, U8Pointer[] cacheHeader) throws CorruptDataException {
		PointerPointer romMethodOffset = ptr.romMethodOffsetEA();
		if (null == cacheHeader) {
			return U8Pointer.cast(ptr).add(I32Pointer.cast(romMethodOffset.getAddress()).at(0));
		} else {
			try {
				J9ShrOffsetPointer j9shrOffset = J9ShrOffsetPointer.cast(romMethodOffset);
				IDATA offset = j9shrOffset.offset();
				if (offset.isZero()) {
					return U8Pointer.NULL;
				}
				int layer = SharedClassesMetaDataHelper.getCacheLayerFromJ9shrOffset(j9shrOffset);
				return cacheHeader[layer].add(offset);
			} catch (NoClassDefFoundError | NoSuchFieldException e) {
				// J9ShrOffset didn't exist in the VM that created this core file
				// even though it appears to support a multi-layer cache.
				throw new CorruptDataException(e);
			}
		}
	}

	// #define CMWDATA(cmw) (((U_8*)(cmw)) + sizeof(CompiledMethodWrapper))
	public static U8Pointer CMWDATA(CompiledMethodWrapperPointer ptr) {
		return U8Pointer.cast(ptr).add(CompiledMethodWrapper.SIZEOF);
	}

	// #define CMWCODE(cmw) (((U_8*)(cmw)) + sizeof(CompiledMethodWrapper) + J9SHR_READMEM((cmw)->dataLength))
	public static U8Pointer CMWCODE(CompiledMethodWrapperPointer ptr) throws CorruptDataException {
		return U8Pointer.cast(ptr).add(CompiledMethodWrapper.SIZEOF).add(ptr.dataLength());
	}

	// #define CMWITEM(cmw) (((U_8*)(cmw)) - sizeof(ShcItem))	
	public static U8Pointer CMWITEM(CompiledMethodWrapperPointer ptr) {
		return U8Pointer.cast(ptr).sub(ShcItem.SIZEOF);
	}
}

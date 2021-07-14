/*******************************************************************************
 * Copyright (c) 2001, 2021 IBM Corp. and others
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
package com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck;

import java.io.PrintStream;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.structure.J9Object;
import com.ibm.j9ddr.vm29.structure.MM_HeapLinkedFreeHeader;
import com.ibm.j9ddr.vm29.j9.ObjectModel;
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.U32Pointer;
import com.ibm.j9ddr.vm29.pointer.UDATAPointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ObjectPointer;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.UDATA;
import com.ibm.j9ddr.vm29.pointer.helper.J9ObjectHelper;

import static com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck.CheckBase.*;
import static com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck.CheckError.*;

public class CheckReporterTTY extends CheckReporter
{
	private PrintStream out;
	
	private static String[] errorTypes = {
		"no error", /* J9MODRON_GCCHK_RC_OK (0) */
		"not aligned", /* J9MODRON_GCCHK_RC_UNALIGNED (1) */
		"double array not aligned", /* J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED (2) */
		"object not in an object region", /* J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION (3) */
		"not in an object segment", /* J9MODRON_GCCHK_RC_NOT_FOUND (4) */
		"overlaps segment boundary", /* J9MODRON_GCCHK_RC_INVALID_RANGE (5) */
		"heap object on stack", /* J9MODRON_GCCHK_RC_STACK_OBJECT (6) */
		"class pointer is null", /* J9MODRON_GCCHK_RC_NULL_CLASS_POINTER (7) */
		"class pointer not aligned", /* J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED (8) */
		"class pointer not in a class segment", /* J9MODRON_GCCHK_RC_CLASS_NOT_FOUND (9) */
		"class pointer overlaps segment boundary", /* J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE (10) */
		"class pointer of class is not java.lang.Class", /* J9MODRON_GCCHK_RC_CLASS_POINTER_NOT_JLCLASS (11) */
		"class pointer on stack", /* J9MODRON_GCCHK_RC_CLASS_STACK_OBJECT (12) */
		"invalid flags", /* J9MODRON_GCCHK_RC_INVALID_FLAGS (13) */
		"in an old segment, old bit not set", /* J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS (14) */
		"in a new segment, old or remembered bit set", /* J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS (15) */
		"hole has size <= 0", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE (16) */
		"new pointer in old object without remembered bit set", /* J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED (17) */
		"not in an old segment or class segment", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT (18) */
		"old bit or remembered bit not set", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS (19) */
		"not in remembered set, new object reference", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT (20) */
		"pool and puddle newstore flag mismatch", /* J9MODRON_GCCHK_RC_PUDDLE_POOL_NEWSTORE_MISMATCH (21) */
		"puddle newstore flag invalid", /* J9MODRON_GCCHK_RC_PUDDLE_INVALID_NEWSTORE_FLAGS (22) */
		"heap object has remembered bit set when cardtable active", /* J9MODRON_GCCHK_RC_HEAP_OBJECT_REMEMBERED (23) */
		"new pointer in old object without card dirtied", /* J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED_IN_CARD_TABLE (24) */
		"dead object", /* J9MODRON_GCCHK_RC_DEAD_OBJECT (25) */
		"class header invalid", /* J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID (26) */
		"class object not java.lang.Class", /* J9MODRON_GCCHK_RC_CLASS_OBJECT_NOT_JLCLASS (27) */
		"scope internal pointer refers outside its scope", /* J9MODRON_GCCHK_RC_INTERNAL_POINTER_NOT_IN_SCOPE (28) */
		"class pointer is in an undead class segment", /* J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD (29) */
		"class ramStatics field points to wrong object", /* J9MODRON_GCCHK_RC_CLASS_STATICS_FIELD_POINTS_WRONG_OBJECT (30) */
		"class ramStatics must be NULL for hot swapped class", /* J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_POINTS_TO_STATICS (31) */
		"class ramStatics field points to object but out of GC scan range", /* J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE (32)*/
		"class ramStatics number of references not equal specified in ROM class", /* J9MODRON_GCCHK_RC_CLASS_STATICS_WRONG_NUMBER_OF_REFERENCES (33) */	
		"invalid indexable data address", /* J9MODRON_GCCHK_RC_INVALID_INDEXABLE_DATA_ADDRESS (34) */
		"obsolete code 35", /* obsolete code (35) */
		"obsolete code 36", /* obsolete code (36) */
		"obsolete code 37", /* obsolete code (37) */
		"class object not a subclass of java.util.concurrent.locks.AbstractOwnableSynchronizer", /* J9MODRON_GCCHK_RC_OWNABLE_SYNCHRONIZER_INVALID_CLASS (38) */
		"array class can not be hot swapped", /* J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_FOR_ARRAY (39) */
		"replaced class has no hot swapped out flag set", /* J9MODRON_GCCHK_RC_REPLACED_CLASS_HAS_NO_HOTSWAP_FLAG (40) */
		"object slot appears to contain a J9Class pointer", /* J9MODRON_GCCHK_RC_OBJECT_SLOT_POINTS_TO_J9CLASS (41) */
		"Ownable Synchronizer Object is not attached to the list", /* J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_OBJECT_IS_NOT_ATTACHED_TO_THE_LIST (42) */
		"Ownable Synchronizer List has a circular reference", /* J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_LIST_HAS_CIRCULAR_REFERENCE (43) */
		"hole size is not aligned", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE_NOT_ALIGNED (44) */
		"hole next is not a hole", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_HOLE (45) */
		"hole next is outside of current region", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_IN_REGION (46) */
		"hole next is pointed inside of the hole", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_POINTED_INSIDE (47) */
		"class is unloaded", /* J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED (48)*/
		"reversed forwarded pointed outside evacuate", /* J9MODRON_GCCHK_RC_REVERSED_FORWARDED_OUTSIDE_EVACUATE (49) */
	};
	
	public CheckReporterTTY(PrintStream out)
	{
		this.out = out;
	}
	
	public CheckReporterTTY()
	{
		this(System.out);
	}

	@Override
	public void report(CheckError error)
	{
		if(!shouldReport(error)) {
			return;
		}
		
		try {
			/* If slot is NULL, we are not scanning the slots of an object, but looking directly at an object/class on the heap. */
			if((error._slot != null) && (error._slot.notNull())) {
				VoidPointer slot = error._slot;
				UDATA slotValue;
				
				switch(error._objectType)
				{
					case check_type_object:
						J9ObjectPointer fieldValue = ObjectReferencePointer.cast(slot).at(0);
						slotValue = UDATA.cast(fieldValue);
						break;
					
					case check_type_thread:
						slotValue = UDATAPointer.cast(slot).at(0);
						slot = error._stackLocation;
						break;
						
					case check_type_unfinalized:
						// In this case, there isn't really a "slot", only the value
						// TODO kmt : This probably shouldn't use the object(offset) format.  
						slotValue = UDATA.cast(slot);
						slot = VoidPointer.NULL;
						break;
						
					default:
						slotValue = UDATAPointer.cast(slot).at(0);
				}
				out.println(String.format("  <gc check (%d): %s: %s: %sslot %x(%x) -> %x: %s>", error._errorNumber, "from debugger", error._check.getCheckName(), error._elementName, error._object.getAddress(), slot.getAddress(), slotValue.longValue(), getErrorType(error._errorCode)));
			} else {
				out.println(String.format("  <gc check (%d): %s: %s: %s%x: %s>", error._errorNumber, "from debugger", error._check.getCheckName(), error._elementName, error._object.getAddress(), getErrorType(error._errorCode)));
				
				/* If the basic checks have been made (alignment, etc.) display header info. */
				if (error._objectType == check_type_object) {
					reportObjectHeader(error, J9ObjectPointer.cast(error._object), "");
				}	
			}
		} catch (CorruptDataException cde) {
			out.println(String.format("  <gc check (%d): %s: %s: %s%x: %s>", error._errorNumber, "from debugger", error._check.getCheckName(), error._elementName, error._object.getAddress(), getErrorType(error._errorCode)));
		}
	}

	private String getErrorType(int errorCode)
	{
		if(errorCode == J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION){
			return "corrupt data exception";
		} else {
			return errorTypes[errorCode];
		}
	}

	@Override
	public void reportClass(CheckError error, J9ClassPointer clazz, String prefix)
	{
		String prefixString = prefix == null ? "" : prefix;
		
		if(!shouldReport(error)) {
			return;
		}		
		
		out.println(String.format("  <gc check (%d): %sClass %x>", error._errorNumber, prefixString, clazz.getAddress()));
	}

	@Override
	public void reportFatalError(CheckError error)
	{
		out.println(String.format("  <gc check (%d): Cannot resolve problem detected on heap, aborting check>", error._errorNumber));
	}

	@Override
	public void reportHeapWalkError(CheckError error, CheckElement previousObjectPtr1, CheckElement previousObjectPtr2, CheckElement previousObjectPtr3)
	{
		reportFatalError(error);
		if (!previousObjectPtr1.isNone()) {
			reportGenericType(error, previousObjectPtr1, "Previous ");
			if(!previousObjectPtr2.isNone()) {
				reportGenericType(error, previousObjectPtr2, "Previous ");
				if(!previousObjectPtr3.isNone()) {
					reportGenericType(error, previousObjectPtr3, "Previous ");
				}
			}
		} else {
			out.println(String.format("  <gc check (%d): %x was first object encountered on heap>", error._errorNumber, error._object.getAddress()));
		}
	}

	@Override
	public void reportObjectHeader(CheckError error, J9ObjectPointer object, String prefix)
	{
		String prefixString = prefix == null ? "" : prefix;
		
		if(!shouldReport(error)) {
			return;
		}
	
		boolean isValid = false;
		boolean isHole = false; 
		boolean isIndexable = false;
		try {
			isHole = ObjectModel.isDeadObject(object);
			if(!isHole) {
				isIndexable = ObjectModel.isIndexable(object);
			}
			isValid = true;
		} catch (CorruptDataException cde) {}

		if(isValid) {
			if(isIndexable) {
				out.print(String.format("  <gc check (%d): %sIObject %x header:", error._errorNumber, prefixString, object.getAddress()));
			} else {
				String elementName = isHole ? "Hole" : "Object";
				out.print(String.format("  <gc check (%d): %s%s %x header:", error._errorNumber, prefixString, elementName, object.getAddress()));
			}
		} else {
			out.print(String.format("  <gc check (%d): %s%s %x header:", error._errorNumber, prefixString, "Corrupt", object.getAddress()));
		}
		
		int headerSize = (int)J9ObjectHelper.headerSize();
		if(isHole) {
			headerSize = (int)MM_HeapLinkedFreeHeader.SIZEOF;
		} else {
			try {
				headerSize = ObjectModel.getHeaderSize(object).intValue();
			} catch (CorruptDataException cde) {}
		}

		try {
			U32Pointer data = U32Pointer.cast(object);
			for(int i = 0; i < headerSize / U32.SIZEOF; i++) {
				out.print(String.format(" %08X", data.at(i).longValue()));
			}
		} catch (CorruptDataException cde) {
		}
		out.println(">");
	}

	@Override
	public void println(String arg)
	{
		out.println(arg);
	}
	
	public void print(String arg)
	{
		out.print(arg);
	}

	@Override
	public void reportForwardedObject(J9ObjectPointer object, J9ObjectPointer newObject)
	{
		out.println(String.format("  <gc check: found forwarded pointer %x -> %x>", object.getAddress(), newObject.getAddress()));
	}
}

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
 * @ingroup GC_Check
 */

#include "CheckReporterTTY.hpp"

#include "Base.hpp"
#include "Check.hpp"
#include "CheckBase.hpp"
#include "CheckError.hpp"
#include "ObjectModel.hpp"

/**
 * Human readable strings representing the invokee defined by #GCCheckInvokedBy.
 */
const static char *invokedByStrings[] = {
	"unknown", /**<undefined */
	"before global GC", /**<J9HOOK_GLOBAL_GC_START */
	"after global GC", /**<J9HOOK_GLOBAL_GC_END */
	"before global GC sweep", /**<J9HOOK_GLOBAL_GC_SWEEP_START */
	"after global GC sweep", /**<J9HOOK_GLOBAL_GC_SWEEP_END */
	"before local GC", /**<J9HOOK_LOCAL_GC_START */
	"after local GC", /**<J9HOOK_LOCAL_GC_END */
	"scavenger backout", /**<J9HOOK_SCAVENGER_BACK_OUT (J9HOOK_LOCAL_GC_END) */
	"remembered set overflow", /**<J9HOOK_REMEMBEREDSET_OVERFLOW */
	"manual invocation", /**< J9HOOK_INVOKE_GC_CHECK */
	"from debugger" /**< via debugger extensions */
};

/**
 * Human readable error strings corresponding to
 * @ref GCCheckWalkStageErrorCodes from the various walk stages.
 * Offset into this table is determined by error code returned by methods such as GC_Check::checkJ9ObjectPointer
 */ 
const static char *errorTypes[] = {
	"no error", /* J9MODRON_GCCHK_RC_OK (0) */
	"not aligned", /* J9MODRON_GCCHK_RC_UNALIGNED (1) */
	"double array not aligned", /* J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED (2) */
	"object not in an object region", /* J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION (3) */
	"not in an object segment", /* J9MODRON_GCCHK_RC_NOT_FOUND (4) */
	"overlaps segment boundary", /* J9MODRON_GCCHK_RC_INVALID_RANGE (5) */
	"heap object on stack", /* J9MODRON_GCCHK_RC_STACK_OBJECT (6) */
	"class pointer is null", /* J9MODRON_GCCHK_RC_NULL_CLASS_POINTER (7)*/
	"class pointer not aligned", /* J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED (8)*/
	"class pointer not in a class segment", /* J9MODRON_GCCHK_RC_CLASS_NOT_FOUND (9)*/
	"class pointer overlaps segment boundary", /* J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE (10) */
	"class pointer of class is not java.lang.Class", /* J9MODRON_GCCHK_RC_CLASS_POINTER_NOT_JLCLASS (11) */
	"class pointer on stack", /* J9MODRON_GCCHK_RC_CLASS_STACK_OBJECT (12) */
	"invalid flags", /* J9MODRON_GCCHK_RC_INVALID_FLAGS (13) */
	"in an old segment, old bit not set", /* J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS (14) */
	"in a new segment, old or remembered bit set", /* J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS (15) */
	"size of hole is too large or 0", /* J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE (16) */
	"new pointer in old object without remembered bit set", /* J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED (17) */
	"not in an old segment or class segment", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT (18) */
	"old bit or remembered bit not set", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS (19) */
	"not in remembered set, new object reference", /* J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT (20) */
	"unused 21", /* J9MODRON_GCCHK_RC_UNUSED_21 (21) */
	"unused 22", /* J9MODRON_GCCHK_RC_UNUSED_22 (22) */
	"heap object has remembered bit set when cardtable active", /* J9MODRON_GCCHK_RC_HEAP_OBJECT_REMEMBERED (23) */
	"new pointer in old object without card dirtied", /* J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED_IN_CARD_TABLE (24) */
	"dead object", /* J9MODRON_GCCHK_RC_DEAD_OBJECT (25) */
	"class header invalid", /* J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID (26) */
	"class object not java.lang.Class", /* J9MODRON_GCCHK_RC_CLASS_OBJECT_NOT_JLCLASS (27) */
	"scope internal pointer refers outside its scope", /* J9MODRON_GCCHK_RC_INTERNAL_POINTER_NOT_IN_SCOPE (28) */
	"class pointer is in an undead class segment", /* J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD (29) */
	"class ramStatics field points to wrong object", /* J9MODRON_GCCHK_RC_CLASS_STATICS_FIELD_POINTS_WRONG_OBJECT (30) */
	"class ramStatics must be NULL for hot swapped class", /* J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_POINTS_TO_STATICS (31) */
	"class ramStatics field points to object but out of GC scan range", /* J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE (32) */
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
};

/**
 * Create a new instance of the report object, using the specified port library.
 */
GC_CheckReporterTTY *
GC_CheckReporterTTY::newInstance(J9JavaVM *javaVM)
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(javaVM)->getForge();

	GC_CheckReporterTTY *reporter = (GC_CheckReporterTTY *)forge->allocate(sizeof(GC_CheckReporterTTY), MM_AllocationCategory::DIAGNOSTIC, J9_GET_CALLSITE());
	if (reporter) {
		reporter = new(reporter) GC_CheckReporterTTY(javaVM);
	}
	return reporter;
}

/**
 * Destroy the instance of the reporter.
 */
void
GC_CheckReporterTTY::kill()
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(_javaVM)->getForge();
	forge->free(this);
}

/**
 * Report an error to the terminal.
 * 
 * Accepts an error object and outputs a report to the terminal.
 * @param  error The error to be reported
 */
void
GC_CheckReporterTTY::report(GC_CheckError *error)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	
	if (!shouldReport(error)) {
		return;
	}

	/* If slot is NULL, we are not scanning the slots of an object, but looking directly at an object/class on the heap. */
	if (error->_slot) {
		const void *slot = error->_slot;
		UDATA slotValue;
		
		if (error->_objectType == check_type_object) {
			GC_SlotObject slotObj(_javaVM->omrVM, (fomrobject_t*)slot);
			slotValue = (UDATA)slotObj.readReferenceFromSlot();
		} else {
			if (error->_objectType == check_type_thread) {
				/* slots from thread stacks are always local */
				slotValue = *((UDATA*)slot);
				slot = error->_stackLocaition;
			} else if(error->_objectType == check_type_unfinalized) {
				slotValue = *((UDATA*)slot);
			}  else if(error->_objectType == check_type_finalizable) {
				slotValue = *((UDATA*)slot);
			} else if(error->_objectType == check_type_ownable_synchronizer) {
				slotValue = *((UDATA*)slot);
			} else {
				slotValue = *((UDATA*)slot);
			}
		}
		
		if (invocation_manual == error->_cycle->getInvoker()) {
			j9tty_printf(PORTLIB, "  <gc check (%zu): %s (%zu): %s: %sslot %p(%p) -> %p: %s>\n", error->_errorNumber, invokedByStrings[error->_cycle->getInvoker()], error->_cycle->getManualCheckNumber(), error->_check->getCheckName(), error->_elementName, error->_object, slot, slotValue, errorTypes[error->_errorCode]);
		} else {
			j9tty_printf(PORTLIB, "  <gc check (%zu): %s: %s: %sslot %p(%p) -> %p: %s>\n", error->_errorNumber, invokedByStrings[error->_cycle->getInvoker()], error->_check->getCheckName(), error->_elementName, error->_object, slot, slotValue, errorTypes[error->_errorCode]);
		}
	} else {
		if (invocation_manual == error->_cycle->getInvoker()) {
			j9tty_printf(PORTLIB, "  <gc check (%zu): %s (%zu): %s: %s%p: %s>\n", error->_errorNumber, invokedByStrings[error->_cycle->getInvoker()], error->_cycle->getManualCheckNumber(), error->_check->getCheckName(), error->_elementName, error->_object, errorTypes[error->_errorCode]);
		} else {
			j9tty_printf(PORTLIB, "  <gc check (%zu): %s: %s: %s%p: %s>\n", error->_errorNumber, invokedByStrings[error->_cycle->getInvoker()], error->_check->getCheckName(), error->_elementName, error->_object, errorTypes[error->_errorCode]);
		}

		/* If the basic checks have been made (alignment, etc.) display header info. */
		if (error->_objectType == check_type_object) {
			reportObjectHeader(error, (J9Object*)error->_object, "");
		}	
	}	
}

/**
 * Print an object header to the terminal.
 * 
 * @param objectPtr the object whose header to print
 * @param prefix a string to prefix the object with, e.g. "Previous", or NULL
 * if no prefix is desired
 */
void 
GC_CheckReporterTTY::reportObjectHeader(GC_CheckError *error, J9Object *objectPtr, const char *prefix)
{
	UDATA headerSize;
	MM_GCExtensionsBase* extensions = MM_GCExtensions::getExtensions(_javaVM);
	const char *prefixString = prefix ? prefix : "";
	PORT_ACCESS_FROM_PORT(_portLibrary);

	if (!shouldReport(error) || NULL == J9GC_J9OBJECT_CLAZZ(objectPtr, extensions)) {
		return;
	}

	headerSize = extensions->objectModel.getHeaderSize(objectPtr);
	if (extensions->objectModel.isIndexable(objectPtr)) {
		j9tty_printf(PORTLIB, "  <gc check (%zu): %sIObject %p header:", error->_errorNumber, prefixString, objectPtr);
	} else {
		const char *elementName = (UDATA) extensions->objectModel.isDeadObject(objectPtr) ? "Hole" : "Object";
		j9tty_printf(PORTLIB, "  <gc check (%zu): %s%s %p header:", error->_errorNumber, prefixString, elementName, objectPtr);
	}
	
	U_32* cursor = (U_32*) objectPtr; 
	for (UDATA i = 0; i < headerSize / sizeof(U_32); i++) {
		j9tty_printf(PORTLIB, " %08X", *cursor);
		cursor++;
	}
	
	j9tty_printf(PORTLIB, ">\n");
}

/**
 * Print a class to the terminal.
 * 
 * @param clazz the class to print
 * @param prefix a string to prefix the class with, e.g. "Previous", or NULL
 * if no prefix is desired
 */
void 
GC_CheckReporterTTY::reportClass(GC_CheckError *error, J9Class *clazz, const char *prefix)
{
	const char *prefixString = prefix ? prefix : "";
	PORT_ACCESS_FROM_PORT(_portLibrary);

	if (!shouldReport(error)) {
		return;
	}

	j9tty_printf(PORTLIB, "  <gc check (%zu): %sClass %p>\n", 
		error->_errorNumber, 
		prefixString, 
		clazz);
}

/**
 * Report the fact that a fatal error has occurred.
 */
void
GC_CheckReporterTTY::reportFatalError(GC_CheckError *error) 
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	j9tty_printf(PORTLIB, "  <gc check (%zu): Cannot resolve problem detected on heap, aborting check>\n", error->_errorNumber);
}

/**
 * Print to the terminal that an error has occurred while walking the heap.
 */
void
GC_CheckReporterTTY::reportHeapWalkError(GC_CheckError *error, GC_CheckElement previousObjectPtr1, GC_CheckElement previousObjectPtr2, GC_CheckElement previousObjectPtr3) 
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	reportFatalError(error);
	if (previousObjectPtr1.type != GC_CheckElement::type_none) {
		reportGenericType(error, previousObjectPtr1, "Previous ");
		if(previousObjectPtr2.type != GC_CheckElement::type_none) {
			reportGenericType(error, previousObjectPtr2, "Previous ");
			if(previousObjectPtr3.type != GC_CheckElement::type_none) {
				reportGenericType(error, previousObjectPtr3, "Previous ");
			}
		}
	} else {
		j9tty_printf(PORTLIB, "  <gc check (%zu): %p was first object encountered on heap>\n", error->_errorNumber, error->_object);
	}
}


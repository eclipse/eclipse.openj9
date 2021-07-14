
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

#if !defined(CHECKBASE_HPP_)
#define CHECKBASE_HPP_

#include "j9.h"
#include "j9cfg.h"
#include "modronopt.h"

/**
 * @name Scan options supported by gcchk
 * @anchor scanFlags
 * @{
 */
 
/* scan flags */
#define J9MODRON_GCCHK_SCAN_ALL_SLOTS ((UDATA)0xFFFFFFF)
#define J9MODRON_GCCHK_SCAN_OBJECT_HEAP ((UDATA)0x00000001)
#define J9MODRON_GCCHK_SCAN_CLASS_HEAP ((UDATA)0x00000002)
#define J9MODRON_GCCHK_SCAN_REMEMBERED_SET ((UDATA)0x00000004)
#define J9MODRON_GCCHK_SCAN_UNFINALIZED ((UDATA)0x00000008)
#define J9MODRON_GCCHK_SCAN_FINALIZABLE ((UDATA)0x00000010)
#define J9MODRON_GCCHK_SCAN_OWNABLE_SYNCHRONIZER ((UDATA)0x00000020)
#define J9MODRON_GCCHK_SCAN_UNUSED1 ((UDATA)0x00000040)
#define J9MODRON_GCCHK_SCAN_UNUSED2 ((UDATA)0x00000080)
#define J9MODRON_GCCHK_SCAN_STRING_TABLE ((UDATA)0x00000100)
#define J9MODRON_GCCHK_SCAN_CLASS_LOADERS ((UDATA)0x00000200)
#define J9MODRON_GCCHK_SCAN_JNI_GLOBAL_REFERENCES ((UDATA)0x00000400)
#define J9MODRON_GCCHK_SCAN_JNI_WEAK_GLOBAL_REFERENCES ((UDATA)0x00000800)
#define J9MODRON_GCCHK_SCAN_DEBUGGER_REFERENCES ((UDATA)0x00001000)
#define J9MODRON_GCCHK_SCAN_DEBUGGER_CLASS_REFERENCES ((UDATA)0x00002000)
#define J9MODRON_GCCHK_SCAN_VM_CLASS_SLOTS ((UDATA)0x00004000)
#define J9MODRON_GCCHK_SCAN_VMTHREADS	((UDATA)0x00008000)
#define J9MODRON_GCCHK_SCAN_THREADSTACKS	((UDATA)0x00010000)
#define J9MODRON_GCCHK_SCAN_JVMTI_OBJECT_TAG_TABLES ((UDATA)0x00020000)
#define J9MODRON_GCCHK_SCAN_MONITOR_TABLE ((UDATA)0x00040000)
#define J9MODRON_GCCHK_SCAN_SCOPES ((UDATA)0x00080000)
/**
 * @}
 */
 
/**
 * @anchor checkFlags
 * @name Check options
 * @{
 */
#define J9MODRON_GCCHK_VERIFY_ALL ((UDATA)0xFFFFFFFF)
#define J9MODRON_GCCHK_VERIFY_CLASS_SLOT ((UDATA)0x00000001)

/* Range check only makes sense if the class pointer is verified first. */
#define J9MODRON_GCCHK_VERIFY_RANGE ((UDATA)0x00000002)
#define J9MODRON_GCCHK_VERIFY_SHAPE ((UDATA)0x00000004)
#define J9MODRON_GCCHK_VERIFY_FLAGS ((UDATA)0x00000008)
/** @} */

/**
 * @anchor miscFlags
 * @name Misc options
 * @{
 */ 
#define J9MODRON_GCCHK_VERBOSE ((UDATA)0x00000001)
#define J9MODRON_GCCHK_INTERVAL ((UDATA)0x00000002)
#define J9MODRON_GCCHK_GLOBAL_INTERVAL ((UDATA)0x00000004)
#define J9MODRON_GCCHK_LOCAL_INTERVAL ((UDATA)0x00000008)
#define J9MODRON_GCCHK_START_INDEX  ((UDATA)0x00000010)
#define J9MODRON_GCCHK_SCAVENGER_BACKOUT ((UDATA)0x00000020)
#define J9MODRON_GCCHK_SUPPRESS_LOCAL ((UDATA)0x00000040)
#define J9MODRON_GCCHK_SUPPRESS_GLOBAL ((UDATA)0x00000080)
#define J9MODRON_GCCHK_REMEMBEREDSET_OVERFLOW ((UDATA)0x00000100)
#define J9MODRON_GCCHK_MISC_SCAN ((UDATA)0x00000200)
#define J9MODRON_GCCHK_MISC_CHECK ((UDATA)0x00000400)
#define J9MODRON_GCCHK_MISC_QUIET ((UDATA)0x00000800)
#define J9MODRON_GCCHK_MISC_ABORT ((UDATA)0x00001000)
#define J9MODRON_GCCHK_MANUAL ((UDATA)0x00002000)
#define J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK ((UDATA)0x00004000)
#define J9MODRON_GCCHK_MISC_DARKMATTER ((UDATA)0x00008000)
#define J9MODRON_GCCHK_MISC_MIDSCAVENGE ((UDATA)0x00010000)
#define J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS ((UDATA)0x00020000)
/** @} */

/**
 * Return codes for iterator functions.
 * Verification continues if an iterator function returns J9MODRON_SLOT_ITERATOR_OK,
 * otherwise it terminates.
 * @name Iterator return codes
 * @{
 */  
#define J9MODRON_SLOT_ITERATOR_OK 	((UDATA)0x00000000) /**< Indicates success */
#define J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR	((UDATA)0x00000001) /**< Indicates that an unrecoverable error was detected */
#define J9MODRON_SLOT_ITERATOR_RECOVERABLE_ERROR ((UDATA)0x00000002) /** < Indicates that a recoverable error was detected */
/** @} */

/**
 * Error codes returned to the iterator methods by helper utilities.
 * @anchor GCCheckWalkStageErrorCodes
 * @name Error Codes 
 * @{ 
 */
/* Error codes applicable to all stages */ 
#define J9MODRON_GCCHK_RC_OK ((UDATA)0)
#define J9MODRON_GCCHK_RC_UNALIGNED ((UDATA)1)
#define J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED ((UDATA)2)
#define J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION ((UDATA)3)
#define J9MODRON_GCCHK_RC_NOT_FOUND ((UDATA)4)
#define J9MODRON_GCCHK_RC_INVALID_RANGE	 ((UDATA)5)
#define J9MODRON_GCCHK_RC_STACK_OBJECT ((UDATA) 6)
#define J9MODRON_GCCHK_RC_DEAD_OBJECT ((UDATA) 25)
#define J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID ((UDATA) 26)
#define J9MODRON_GCCHK_RC_OBJECT_SLOT_POINTS_TO_J9CLASS ((UDATA) 41)

/* Special error codes for when J9MODRON_GCCHK_VERIFY_CLASS_SLOT is set (all stages)*/ 
#define J9MODRON_GCCHK_RC_NULL_CLASS_POINTER ((UDATA)7)
#define J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED ((UDATA)8)
#define J9MODRON_GCCHK_RC_CLASS_NOT_FOUND ((UDATA)9)
#define J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE ((UDATA)10)
#define J9MODRON_GCCHK_RC_CLASS_POINTER_NOT_JLCLASS ((UDATA)11)
#define J9MODRON_GCCHK_RC_CLASS_STACK_OBJECT ((UDATA) 12)
#define J9MODRON_GCCHK_RC_CLASS_OBJECT_NOT_JLCLASS ((UDATA) 27)
#define J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD ((UDATA) 29)
#define J9MODRON_GCCHK_RC_CLASS_STATICS_FIELD_POINTS_WRONG_OBJECT ((UDATA) 30)
#define J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_POINTS_TO_STATICS ((UDATA) 31)
#define J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE ((UDATA) 32)
#define J9MODRON_GCCHK_RC_CLASS_STATICS_WRONG_NUMBER_OF_REFERENCES ((UDATA) 33)

/* obsolete code 35 */
/* obsolete code 36 */
/* obsolete code 37 */

#define J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_FOR_ARRAY ((UDATA) 39)
#define J9MODRON_GCCHK_RC_REPLACED_CLASS_HAS_NO_HOTSWAP_FLAG ((UDATA) 40)
#define J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED ((UDATA) 48)

/* Special error codes for when J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS is set */
#define J9MODRON_GCCHK_RC_INVALID_INDEXABLE_DATA_ADDRESS ((UDATA) 34)

/* Special error codes for when J9MODRON_GCCHK_VERIFY_FLAGS is set (all stages)*/
#define J9MODRON_GCCHK_RC_INVALID_FLAGS ((UDATA)13)
#define J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS ((UDATA)14)
#define J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS ((UDATA)15)

/* Error codes applicable only to stage_object_heap */
#define J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE ((UDATA)16)
#define J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED ((UDATA)17)

/* Error codes applicable only to stage_remembered_set */
#define J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT ((UDATA)18)
#define J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS ((UDATA)19)
#define J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT ((UDATA)20)

/* Deprecated error codes applicable to puddle flags */
#define J9MODRON_GCCHK_RC_UNUSED_21 ((UDATA)21)
#define J9MODRON_GCCHK_RC_UNUSED_22 ((UDATA)22)

/* Error codes applicable for card-marking remembered set */
#define J9MODRON_GCCHK_RC_HEAP_OBJECT_REMEMBERED ((UDATA)23)
#define J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED_IN_CARD_TABLE ((UDATA)24)

/* Error codes applicable for scopes */
#define J9MODRON_GCCHK_RC_INTERNAL_POINTER_NOT_IN_SCOPE ((UDATA)28)

/* Error codes for instances of java.util.concurrent.locks.AbstractOwnableSynchronizer */
#define J9MODRON_GCCHK_RC_OWNABLE_SYNCHRONIZER_INVALID_CLASS ((UDATA)38)
#define J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_OBJECT_IS_NOT_ATTACHED_TO_THE_LIST ((UDATA)42)
#define J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_LIST_HAS_CIRCULAR_REFERENCE ((UDATA)43)

#define J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE_NOT_ALIGNED ((UDATA)44)
#define J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_HOLE ((UDATA)45)
#define J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_IN_REGION ((UDATA)46)
#define J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_POINTED_INSIDE ((UDATA)47)

/** @} Error codes */

/**
 * What stage of GC invoked the check.
 * @note If you add to this enum, you MUST add a corresponding entry in invokedByStrings[]
 */
typedef enum {
	invocation_unknown, /**< Invocation unknown */
	invocation_global_start, /**< The check was invoked at the start of a global GC */
	invocation_global_end, /**< The check was invoked at the end of a global GC */
	invocation_global_sweep_start, /**< The action was invoked at the start of the sweep phase of a global GC */
	invocation_global_sweep_end, /**< The action was invoked at the end of the sweep phase of a global GC */
	invocation_local_start, /**< The check was invoked at the start of a local GC */
	invocation_local_end, /**< The check was invoked at the end of a local GC */
	invocation_scavenger_backout, /**< The check was invoked after a scavenger (local GC) backout operation */
	invocation_rememberedset_overflow, /**< The check was invoked when remembered set overflow was detected */
	invocation_manual, /**< The check was manually invoked using J9HOOK_INVOKE_GC_CHECK (see @ref hookInvokeGCCheck) */
	invocation_debugger /**< The check was triggered from the debugger extensions (windbg or gcc) */
}  GCCheckInvokedBy;

/**
 * Extensions used by GCCheck hooks.
 */
typedef struct GCCHK_Extensions {
	void *checkEngine; /**< The check engine */
	void *checkCycle; /**< The command-line check cycle */
	UDATA gcInterval;
	UDATA globalGcInterval;
	UDATA globalGcCount;
	UDATA gcStartIndex;
#if defined(J9VM_GC_MODRON_SCAVENGER)	
	UDATA localGcInterval;
	UDATA localGcCount;
#endif /* J9VM_GC_MODRON_SCAVENGER */
} GCCHK_Extensions;
#endif /* CHECKBASE_HPP_ */


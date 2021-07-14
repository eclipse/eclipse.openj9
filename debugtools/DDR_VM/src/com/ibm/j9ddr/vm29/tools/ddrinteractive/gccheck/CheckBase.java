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

import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;

public final class CheckBase
{
	/* Exists only to hold these constants */
	private CheckBase() {};
	
	/* Scan flags */
	public static final int J9MODRON_GCCHK_SCAN_ALL_SLOTS = 0xFFFFFFF;
	public static final int J9MODRON_GCCHK_SCAN_OBJECT_HEAP = 0x00000001;
	public static final int J9MODRON_GCCHK_SCAN_CLASS_HEAP = 0x00000002;
	public static final int J9MODRON_GCCHK_SCAN_REMEMBERED_SET = 0x00000004;
	public static final int J9MODRON_GCCHK_SCAN_UNFINALIZED = 0x00000008;
	public static final int J9MODRON_GCCHK_SCAN_FINALIZABLE = 0x00000010;
	public static final int J9MODRON_GCCHK_SCAN_WEAK_REFERENCES = 0x00000020;
	public static final int J9MODRON_GCCHK_SCAN_SOFT_REFERENCES = 0x00000040;
	public static final int J9MODRON_GCCHK_SCAN_PHANTOM_REFERENCES = 0x00000080;
	public static final int J9MODRON_GCCHK_SCAN_STRING_TABLE = 0x00000100;
	public static final int J9MODRON_GCCHK_SCAN_CLASS_LOADERS = 0x00000200;
	public static final int J9MODRON_GCCHK_SCAN_JNI_GLOBAL_REFERENCES = 0x00000400;
	public static final int J9MODRON_GCCHK_SCAN_JNI_WEAK_GLOBAL_REFERENCES = 0x00000800;
	public static final int J9MODRON_GCCHK_SCAN_DEBUGGER_REFERENCES = 0x00001000;
	public static final int J9MODRON_GCCHK_SCAN_DEBUGGER_CLASS_REFERENCES = 0x00002000;
	public static final int J9MODRON_GCCHK_SCAN_VM_CLASS_SLOTS = 0x00004000;
	public static final int J9MODRON_GCCHK_SCAN_VMTHREADS = 0x00008000;
	public static final int J9MODRON_GCCHK_SCAN_THREADSTACKS = 0x00010000;
	public static final int J9MODRON_GCCHK_SCAN_JVMTI_OBJECT_TAG_TABLES = 0x00020000;
	public static final int J9MODRON_GCCHK_SCAN_MONITOR_TABLE = 0x00040000;
	public static final int J9MODRON_GCCHK_SCAN_SCOPES = 0x00080000;
	public static final int J9MODRON_GCCHK_SCAN_OWNABLE_SYNCHRONIZER = 0x00100000;

	/* Check options */
	public static final int J9MODRON_GCCHK_VERIFY_ALL = 0xFFFFFFFF;
	public static final int J9MODRON_GCCHK_VERIFY_CLASS_SLOT = 0x00000001;
	
	/* Range check only makes sense if the class pointer is verified first. */
	public static final int J9MODRON_GCCHK_VERIFY_RANGE = 0x00000002;
	public static final int J9MODRON_GCCHK_VERIFY_SHAPE = 0x00000004;
	public static final int J9MODRON_GCCHK_VERIFY_FLAGS = 0x00000008;
	
	/* Misc flags */
	public static final int J9MODRON_GCCHK_VERBOSE = 0x00000001;
	public static final int J9MODRON_GCCHK_INTERVAL = 0x00000002;
	public static final int J9MODRON_GCCHK_GLOBAL_INTERVAL = 0x00000004;
	public static final int J9MODRON_GCCHK_LOCAL_INTERVAL = 0x00000008;
	public static final int J9MODRON_GCCHK_START_INDEX  = 0x00000010;
	public static final int J9MODRON_GCCHK_SCAVENGER_BACKOUT = 0x00000020;
	public static final int J9MODRON_GCCHK_SUPPRESS_LOCAL = 0x00000040;
	public static final int J9MODRON_GCCHK_SUPPRESS_GLOBAL = 0x00000080;
	public static final int J9MODRON_GCCHK_REMEMBEREDSET_OVERFLOW = 0x00000100;
	public static final int J9MODRON_GCCHK_MISC_SCAN = 0x00000200;
	public static final int J9MODRON_GCCHK_MISC_CHECK = 0x00000400;
	public static final int J9MODRON_GCCHK_MISC_QUIET = 0x00000800;
	public static final int J9MODRON_GCCHK_MISC_ABORT = 0x00001000;
	public static final int J9MODRON_GCCHK_MANUAL = 0x00002000;
	public static final int J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK = 0x00004000;
	public static final int J9MODRON_GCCHK_MISC_DARKMATTER = 0x00008000;
	public static final int J9MODRON_GCCHK_MISC_MIDSCAVENGE = 0x00010000;
	public static final int J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS = 0x00020000;
	public static final int J9MODRON_GCCHK_MISC_OWNABLESYNCHRONIZER_CONSISTENCY = 0x00040000;

	/*
	 * Return codes for iterator functions.
	 * Verification continues if an iterator function returns J9MODRON_SLOT_ITERATOR_OK,
	 * otherwise it terminates.
	 */  
	public static final int J9MODRON_SLOT_ITERATOR_OK = 0x00000000;
	public static final int J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR = 0x00000001;
	public static final int J9MODRON_SLOT_ITERATOR_RECOVERABLE_ERROR = 0x00000002;
	
	/* Error codes returned to the iterator methods by helper utilities. */
	
	/* Error codes applicable to all stages */ 
	public static final int J9MODRON_GCCHK_RC_OK = 0;
	public static final int J9MODRON_GCCHK_RC_UNALIGNED = 1;
	public static final int J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED = 2;
	public static final int J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION = 3;
	public static final int J9MODRON_GCCHK_RC_NOT_FOUND = 4;
	public static final int J9MODRON_GCCHK_RC_INVALID_RANGE	 = 5;
	public static final int J9MODRON_GCCHK_RC_STACK_OBJECT =  6;
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT =  25;
	public static final int J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID =  26;
	public static final int J9MODRON_GCCHK_RC_OBJECT_SLOT_POINTS_TO_J9CLASS = 41;

	/* Special error codes for when J9MODRON_GCCHK_VERIFY_CLASS_SLOT is set (all stages)*/ 
	public static final int J9MODRON_GCCHK_RC_NULL_CLASS_POINTER = 7;
	public static final int J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED = 8;
	public static final int J9MODRON_GCCHK_RC_CLASS_NOT_FOUND = 9;
	public static final int J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE = 10;
	public static final int J9MODRON_GCCHK_RC_CLASS_POINTER_NOT_JLCLASS = 11;
	public static final int J9MODRON_GCCHK_RC_CLASS_STACK_OBJECT =  12;
	public static final int J9MODRON_GCCHK_RC_CLASS_OBJECT_NOT_JLCLASS =  27;
	public static final int J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD =  29;
	public static final int J9MODRON_GCCHK_RC_CLASS_STATICS_FIELD_POINTS_WRONG_OBJECT = 30;
	public static final int J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_POINTS_TO_STATICS = 31;
	public static final int J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE = 32;
	public static final int J9MODRON_GCCHK_RC_CLASS_STATICS_WRONG_NUMBER_OF_REFERENCES = 33;
	public static final int J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_FOR_ARRAY = 39;
	public static final int J9MODRON_GCCHK_RC_REPLACED_CLASS_HAS_NO_HOTSWAP_FLAG = 40;
	public static final int J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED =  48;
	public static final int J9MODRON_GCCHK_RC_REVERSED_FORWARDED_OUTSIDE_EVACUATE = 49;

	public static final int J9MODRON_GCCHK_RC_OWNABLE_SYNCHRONIZER_INVALID_CLASS = 38;

	/* Special error codes for when J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS is set */
	public static final int J9MODRON_GCCHK_RC_INVALID_INDEXABLE_DATA_ADDRESS = 34;

	/* Special error codes for when J9MODRON_GCCHK_VERIFY_FLAGS is set (all stages)*/
	public static final int J9MODRON_GCCHK_RC_INVALID_FLAGS = 13;
	public static final int J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS = 14;
	public static final int J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS = 15;

	/* Error codes applicable only to stage_object_heap */
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE = 16;
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE_NOT_ALIGNED = 44;
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_HOLE = 45;
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_IN_REGION = 46;
	public static final int J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_POINTED_INSIDE = 47;
	public static final int J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED = 17;

	/* Error codes applicable only to stage_remembered_set */
	public static final int J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT = 18;
	public static final int J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS = 19;
	public static final int J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT = 20;

	/* Deprecated error codes applicable to puddle flags */
	public static final int J9MODRON_GCCHK_RC_PUDDLE_POOL_NEWSTORE_MISMATCH = 21;
	public static final int J9MODRON_GCCHK_RC_PUDDLE_INVALID_NEWSTORE_FLAGS = 22;

	/* Error codes applicable for card-marking remembered set */
	public static final int J9MODRON_GCCHK_RC_HEAP_OBJECT_REMEMBERED = 23;
	public static final int J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED_IN_CARD_TABLE = 24;

	/* Error codes applicable for scopes */
	public static final int J9MODRON_GCCHK_RC_INTERNAL_POINTER_NOT_IN_SCOPE = 28;

	/* Error codes applicable for Ownable Synchronizers  */
	public static final int J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_OBJECT_IS_NOT_ATTACHED_TO_THE_LIST = 42;
	public static final int J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_LIST_HAS_CIRCULAR_REFERENCE = 43;
	
	/* This is a special code for DDR */
	public static final int J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION = Integer.MAX_VALUE;

	
	/*
	 * What stage of GC invoked the check.
	 * @note If you add to this enum, you MUST add a corresponding entry in invokedByStrings[]
	 */
	public static final int invocation_unknown = 0;
	public static final int invocation_global_start = 1;
	public static final int invocation_global_end = 2;
	public static final int invocation_global_sweep_start = 3;
	public static final int invocation_global_sweep_end = 4;
	public static final int invocation_local_start = 5;
	public static final int invocation_local_end = 6;
	public static final int invocation_scavenger_backout = 7;
	public static final int invocation_rememberedset_overflow = 8;
	public static final int invocation_manual = 9;
	public static final int invocation_debugger = 10;
	
	/*
	 * Define alignment masks for J9Objects:
	 *
	 * o On 64 bit all objects are 8 byte aligned
	 * o On 31/32 bit:
	 * 		- Objects allocated on stack are 4 bytes aligned
	 * 		- Objects allocated on heap are either 4 or 8 byte aligned dependent
	 * 		  on platform
	 */
	public static final int J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK;
	public static final int J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK;
	public static final int J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK;	// TODO : wrong for flags-in-class-slot 
	static 
	{
		if(J9BuildFlags.env_data64) {
			J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK = 7;
			J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK = 7;
			J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK = 7;
		} else {
			J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK = 3;
			if(J9BuildFlags.gc_minimumObjectSize) {
				J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK = 7;
				J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK = 7;
			} else {
				J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK = 3;
				J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK = 3;
			}
		}
	}
	
	public static final long J9MODRON_GCCHK_J9CLASS_EYECATCHER = 0x99669966L;
	
	
	/* TODO : These belong in an analogue to j9modron.h */
	public static final int DEFERRED_RS_REMOVE_FLAG = 1;
	public static final int J9_GC_CLASS_LOADER_SCANNED = 1;
	public static final int J9_GC_CLASS_LOADER_DEAD = 2;
	public static final int J9_GC_CLASS_LOADER_UNLOADING = 4;
	public static final int J9_GC_CLASS_LOADER_ENQ_UNLOAD = 8;
}

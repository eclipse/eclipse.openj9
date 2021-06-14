
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

#include <string.h>

#include "vendor_version.h"
#include "CheckCycle.hpp"
#include "CheckClassHeap.hpp"
#include "CheckClassLoaders.hpp"
#include "CheckEngine.hpp"
#include "CheckFinalizableList.hpp"
#include "CheckJNIGlobalReferences.hpp"
#include "CheckJNIWeakGlobalReferences.hpp"
#include "CheckJVMTIObjectTagTables.hpp"
#include "CheckMonitorTable.hpp"
#include "CheckObjectHeap.hpp"
#include "CheckOwnableSynchronizerList.hpp"
#include "CheckRememberedSet.hpp"
#include "CheckStringTable.hpp"
#include "CheckUnfinalizedList.hpp"
#include "CheckVMClassSlots.hpp"
#include "CheckVMThreads.hpp"
#include "CheckVMThreadStacks.hpp"
#include "FixDeadObjects.hpp"
#include "GCExtensionsBase.hpp"

#define TOTAL_NUM_CHECKS (sizeof(GC_CheckCycle::funcArray) / sizeof(GC_CheckCycle::funcStruct))
const GC_CheckCycle::funcStruct GC_CheckCycle::funcArray[] = {
	/* move ownablesynchronizer check before objectheap check in order to ownablesynchronizer check runs after objectheap check(the checks in stack, last in first out) 
	 * for ownablesynchronizer consistency check, which defends on both ownablesynchronizer check before objectheap check. the verify consistency only at the end of ownablesynchronizer check. 
	 */
	{"ownablesynchronizer", J9MODRON_GCCHK_SCAN_OWNABLE_SYNCHRONIZER, GC_CheckOwnableSynchronizerList::newInstance},
 	{"objectheap", J9MODRON_GCCHK_SCAN_OBJECT_HEAP, GC_CheckObjectHeap::newInstance},
	{"classheap", J9MODRON_GCCHK_SCAN_CLASS_HEAP, GC_CheckClassHeap::newInstance},
#if defined(J9VM_GC_GENERATIONAL)
	{"rememberedset", J9MODRON_GCCHK_SCAN_REMEMBERED_SET, GC_CheckRememberedSet::newInstance},
#endif
#if defined(J9VM_GC_FINALIZATION)
	{"unfinalized", J9MODRON_GCCHK_SCAN_UNFINALIZED, GC_CheckUnfinalizedList::newInstance},
	{"finalizable", J9MODRON_GCCHK_SCAN_FINALIZABLE, GC_CheckFinalizableList::newInstance},
#endif
	{"stringtable", J9MODRON_GCCHK_SCAN_STRING_TABLE, GC_CheckStringTable::newInstance},
	{"classloaders", J9MODRON_GCCHK_SCAN_CLASS_LOADERS, GC_CheckClassLoaders::newInstance},
	{"jniglobalrefs", J9MODRON_GCCHK_SCAN_JNI_GLOBAL_REFERENCES, GC_CheckJNIGlobalReferences::newInstance},
	{"jniweakglobalrefs", J9MODRON_GCCHK_SCAN_JNI_WEAK_GLOBAL_REFERENCES, GC_CheckJNIWeakGlobalReferences::newInstance},
#if defined(J9VM_OPT_JVMTI)
	{"jvmtiobjecttagtables", J9MODRON_GCCHK_SCAN_JVMTI_OBJECT_TAG_TABLES, GC_CheckJVMTIObjectTagTables::newInstance},
#endif
	{"vmclassslots", J9MODRON_GCCHK_SCAN_VM_CLASS_SLOTS, GC_CheckVMClassSlots::newInstance},
	{"monitortable", J9MODRON_GCCHK_SCAN_MONITOR_TABLE, GC_CheckMonitorTable::newInstance},
	{"vmthreads", J9MODRON_GCCHK_SCAN_VMTHREADS, GC_CheckVMThreads::newInstance},
	{"threadstacks", J9MODRON_GCCHK_SCAN_THREADSTACKS, GC_CheckVMThreadStacks::newInstance}
};

/**
 * Provide user help.
 * Display GC_Check command line option help.
 */
void
GC_CheckCycle::printHelp(J9PortLibrary *portLib)
{
	PORT_ACCESS_FROM_PORT(portLib);

	j9tty_printf(PORTLIB, "gcchk for J9, Version " J9JVM_VERSION_STRING "\n");
	j9tty_printf(PORTLIB, J9_COPYRIGHT_STRING "\n\n");
	j9tty_printf(PORTLIB, "Usage: -Xcheck:gc[:scanOption,...][:verifyOption,...][:miscOption,...]\n");
	j9tty_printf(PORTLIB, "scan options (default is all):\n");
	j9tty_printf(PORTLIB, "  all               all object and VM slots\n");
	j9tty_printf(PORTLIB, "  none\n");
	
	/* print the name of each check supported */
	for(UDATA i = 0; i < TOTAL_NUM_CHECKS; i++) {
		j9tty_printf(PORTLIB, "  %s\n", funcArray[i].name);
	}

	j9tty_printf(PORTLIB, "  heap              object and class heaps\n");
	j9tty_printf(PORTLIB, "  novmthreads\n");
	j9tty_printf(PORTLIB, "  help              print this screen\n");

	j9tty_printf(PORTLIB, "\nverify options (default is all):\n");
	j9tty_printf(PORTLIB, "  all\n");
	j9tty_printf(PORTLIB, "  none\n");
	j9tty_printf(PORTLIB, "  classslot\n");
	j9tty_printf(PORTLIB, "  range\n");	
	j9tty_printf(PORTLIB, "  flags\n");

	j9tty_printf(PORTLIB, "\nmisc options (default is verbose,check):\n");
	j9tty_printf(PORTLIB, "  verbose\n");
	j9tty_printf(PORTLIB, "  quiet\n");
	j9tty_printf(PORTLIB, "  scan\n");
	j9tty_printf(PORTLIB, "  noscan\n");
	j9tty_printf(PORTLIB, "  check\n");
	j9tty_printf(PORTLIB, "  nocheck\n");
	j9tty_printf(PORTLIB, "  maxErrors=X\n");

	j9tty_printf(PORTLIB, "  abort\n");
	j9tty_printf(PORTLIB, "  noabort\n");
	j9tty_printf(PORTLIB, "  dumpstack\n");
	j9tty_printf(PORTLIB, "  nodumpstack\n");
	j9tty_printf(PORTLIB, "  interval=X\n");
	j9tty_printf(PORTLIB, "  globalinterval=X\n");
#if defined(J9VM_GC_MODRON_SCAVENGER)
	j9tty_printf(PORTLIB, "  localinterval=X\n");
#endif /* J9VM_GC_MODRON_SCAVENGER */
	j9tty_printf(PORTLIB, "  startindex=x\n");
#if defined(J9VM_GC_MODRON_SCAVENGER)
	j9tty_printf(PORTLIB, "  scavengerbackout\n");
	j9tty_printf(PORTLIB, "  suppresslocal\n");
#endif /* J9VM_GC_MODRON_SCAVENGER */
	j9tty_printf(PORTLIB, "  suppressglobal\n");
#if defined(J9VM_GC_GENERATIONAL)
	j9tty_printf(PORTLIB, "  rememberedsetoverflow\n");
#endif /* J9VM_GC_GENERATIONAL */
	j9tty_printf(PORTLIB, "\n");
}

/**
 * Generate the list of checks to perform in this cycle
 * Iterates over the scanFlags UDATA, instantiating a GC_Check subtype for
 * each bit turned on and adding it to the list.
 * 
 * @param scanFlags Bit vector containing a bit for each type of check we're
 * going to run.
 */
void
GC_CheckCycle::generateCheckList(UDATA scanFlags)
{
	/* Iterate over funcArray dereferencing the function pointer of the checks we want to instantiate */
	for(UDATA i = 0; i < TOTAL_NUM_CHECKS; i++) {
		if(scanFlags & funcArray[i].bitmask) {
			/* we've found a check we need to instantiate */
			GC_Check *check = (funcArray[i].function)(_javaVM, _engine);
			/* add it to the list of checks */
			if(check) {
				check->setNext(_checks);
				check->setBitId(funcArray[i].bitmask);
				_checks = check;
			}
		}
	}
}

/**
 * Parse command line.
 * Parse the command line for GCCheck options.
 * 
 * @param options a string containing the user-supplied options
 */
bool
GC_CheckCycle::initialize(const char *options)
{
	GCCHK_Extensions *extensions = (GCCHK_Extensions *)((MM_GCExtensions*)_javaVM->gcExtensions)->gcchkExtensions;
	UDATA scanFlags = 0, checkFlags = 0, miscFlags;
	char *scan_start = (char *)options;
	const char *scan_limit = options + strlen(options);

	/* set the default miscFlags */
	miscFlags = J9MODRON_GCCHK_VERBOSE | J9MODRON_GCCHK_MISC_CHECK;

	/* parse supplied options */
top:
	while (scan_start < scan_limit) {

		/* ignore separators */
		try_scan(&scan_start, ",");

		if (try_scan(&scan_start, "all")) {
			scanFlags |= J9MODRON_GCCHK_SCAN_ALL_SLOTS;
			continue;
		}

		if (try_scan(&scan_start, "none")) {
			scanFlags &= ~J9MODRON_GCCHK_SCAN_ALL_SLOTS;
			continue;
		}
		
		/* search for a supported check */
		for(UDATA i = 0; i < TOTAL_NUM_CHECKS; i++) {
			if (try_scan(&scan_start, funcArray[i].name)) {
				scanFlags |= funcArray[i].bitmask;
				goto top; /* we want to 'break' from the for-loop and 'continue' the while, so a goto is needed */
			}
		}

		/* now do the special compound options (that affect more than one check) */
		if (try_scan(&scan_start, "heap")) {
			scanFlags |= J9MODRON_GCCHK_SCAN_OBJECT_HEAP | J9MODRON_GCCHK_SCAN_CLASS_HEAP;
			continue;
		}

		if (try_scan(&scan_start, "novmthreads")) {
			scanFlags &= ~J9MODRON_GCCHK_SCAN_VMTHREADS;
			continue;
		}

		/* When ':' is encountered, everything that follows is a check option */
		if (try_scan(&scan_start, ":")) {
			while (scan_start < scan_limit) {
				/* ignore separators */
				try_scan(&scan_start, ",");

				if (try_scan(&scan_start, "all")) {
					checkFlags |= J9MODRON_GCCHK_VERIFY_ALL;
					continue;
				}

				if (try_scan(&scan_start, "none")) {
					checkFlags &= ~J9MODRON_GCCHK_VERIFY_ALL;
					continue;
				}

				if (try_scan(&scan_start, "classslot")) {
					checkFlags |= J9MODRON_GCCHK_VERIFY_CLASS_SLOT;
					continue;
				}

				if (try_scan(&scan_start, "range")) {
					checkFlags |= J9MODRON_GCCHK_VERIFY_RANGE;
					continue;
				}

				if (try_scan(&scan_start, "flags")) {
					checkFlags |= J9MODRON_GCCHK_VERIFY_FLAGS;
					continue;
				}

				/* Another ':' signals the start of misc options */
				if (try_scan(&scan_start, ":")) {
					while (scan_start < scan_limit) {
						/* ignore separators */
						try_scan(&scan_start, ",");

						if (try_scan(&scan_start, "verbose")) {
							miscFlags |= J9MODRON_GCCHK_VERBOSE;
							continue;
						}

						if (try_scan(&scan_start, "manual")) {
							miscFlags |= J9MODRON_GCCHK_MANUAL;
							continue;
						}

						if (try_scan(&scan_start, "quiet")) {
							miscFlags &= ~J9MODRON_GCCHK_VERBOSE;
							miscFlags |= J9MODRON_GCCHK_MISC_QUIET;
							continue;
						}

						if (try_scan(&scan_start, "scan")) {
							miscFlags |= J9MODRON_GCCHK_MISC_SCAN;
							continue;
						}
		
						if (try_scan(&scan_start, "noscan")) {
							miscFlags &= ~J9MODRON_GCCHK_MISC_SCAN;
							continue;
						}
		
						if (try_scan(&scan_start, "check")) {
							miscFlags |= J9MODRON_GCCHK_MISC_CHECK;
							continue;
						}
		
						if (try_scan(&scan_start, "nocheck")) {
							miscFlags &= ~J9MODRON_GCCHK_MISC_CHECK;
							continue;
						}

						if (try_scan(&scan_start, "maxerrors=")) {
							UDATA max;
							scan_udata(&scan_start, &max);
							_engine->setMaxErrorsToReport(max);
							continue;
						}

						if (try_scan(&scan_start, "darkmatter")) {
							miscFlags |= J9MODRON_GCCHK_MISC_DARKMATTER;
							continue;
						}

#if defined(J9VM_GC_MODRON_SCAVENGER) || defined(J9VM_GC_VLHGC)
						if (try_scan(&scan_start, "midscavenge")) {
							miscFlags |= J9MODRON_GCCHK_MISC_MIDSCAVENGE;
							continue;
						}

						if (try_scan(&scan_start, "indexabledataaddress")) {
							miscFlags |= J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS;
							continue;
						}
#endif /* J9VM_GC_MODRON_SCAVENGER  || defined(J9VM_GC_VLHGC) */

						if (try_scan(&scan_start, "abort")) {
							miscFlags |= J9MODRON_GCCHK_MISC_ABORT;
							continue;
						}

						if (try_scan(&scan_start, "noabort")) {
							miscFlags &= ~J9MODRON_GCCHK_MISC_ABORT;
							continue;
						}
						
						if (try_scan(&scan_start, "dumpstack")) {
							miscFlags |= J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK;
							continue;
						}

						if (try_scan(&scan_start, "nodumpstack")) {
							miscFlags &= ~J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK;
							continue;
						}

						if (try_scan(&scan_start, "interval=")) {
							scan_udata(&scan_start, &extensions->gcInterval);
							miscFlags |= J9MODRON_GCCHK_INTERVAL;
							continue;
						}
#if defined(J9VM_GC_MODRON_SCAVENGER)
						if (try_scan(&scan_start, "localinterval=")) {
							scan_udata(&scan_start, &extensions->localGcInterval);
							miscFlags |= J9MODRON_GCCHK_LOCAL_INTERVAL;
							continue;
						}
#endif /* J9VM_GC_MODRON_SCAVENGER */

						if (try_scan(&scan_start, "globalinterval=")) {
							scan_udata(&scan_start, &extensions->globalGcInterval);
							miscFlags |= J9MODRON_GCCHK_GLOBAL_INTERVAL;
							continue;
						}

						if (try_scan(&scan_start, "startindex=")) {
							scan_udata(&scan_start, &extensions->gcStartIndex);
							miscFlags |= J9MODRON_GCCHK_START_INDEX;
							continue;
						}

#if defined(J9VM_GC_MODRON_SCAVENGER)
						if (try_scan(&scan_start, "scavengerbackout")) {
							miscFlags |= J9MODRON_GCCHK_SCAVENGER_BACKOUT;
							continue;
						}

						if (try_scan(&scan_start, "suppresslocal")) {
							miscFlags |= J9MODRON_GCCHK_SUPPRESS_LOCAL;
							continue;
						}
#endif /* J9VM_GC_MODRON_SCAVENGER */

						if (try_scan(&scan_start, "suppressglobal")) {
							miscFlags |= J9MODRON_GCCHK_SUPPRESS_GLOBAL;
							continue;
						}

#if defined(J9VM_GC_GENERATIONAL)
						if (try_scan(&scan_start, "rememberedsetoverflow")) {
							miscFlags |= J9MODRON_GCCHK_REMEMBEREDSET_OVERFLOW;
							continue;
						}
#endif /* J9VM_GC_GENERATIONAL */
						/* Nothing matched */
						goto failure;
					}
					/* Reached end */
					goto done;
				}
				/* Nothing matched */
				goto failure;
			}
			/* Reached end */
			goto done;
		}

		goto failure;
	}

done:
	/* Set defaults if user did not specify */
	if (0 == scanFlags) {
		scanFlags = J9MODRON_GCCHK_SCAN_ALL_SLOTS;
	}
	if (0 == checkFlags) {
		checkFlags = J9MODRON_GCCHK_VERIFY_ALL;
	}

	generateCheckList(scanFlags);
	_checkFlags = checkFlags;
	_miscFlags = miscFlags;
	
	if (J9MODRON_GCCHK_SCAN_OBJECT_HEAP == (scanFlags & J9MODRON_GCCHK_SCAN_OBJECT_HEAP)) {
		/* initialize OwnableSynchronizerCount On Object Heap for ownableSynchronizer consistency check */
		_engine->initializeOwnableSynchronizerCountOnHeap();
	}
	if (J9MODRON_GCCHK_SCAN_OWNABLE_SYNCHRONIZER == (scanFlags & J9MODRON_GCCHK_SCAN_OWNABLE_SYNCHRONIZER)) {
		/* initialize OwnableSynchronizerCount On Lists for ownableSynchronizer consistency check */
		_engine->initializeOwnableSynchronizerCountOnList();
	}
	
	return true;

failure:
	/* no match */
	scan_failed(_portLibrary, "gcchk", scan_start);
	printHelp(_portLibrary);

	return false;
}

void
GC_CheckCycle::run(GCCheckInvokedBy invokedBy, UDATA filterFlags)
{
	_invokedBy = invokedBy;
	_engine->startCheckCycle(_javaVM, this);
	
	GC_Check *mover = _checks;
	while(mover) {
		/* Use this mover (checker) only if its bitmask set in the filter flags */
		if (mover->getBitId() == (filterFlags & mover->getBitId())) {
			bool check = (J9MODRON_GCCHK_MISC_CHECK == (_miscFlags & J9MODRON_GCCHK_MISC_CHECK));
			bool scan = (J9MODRON_GCCHK_MISC_SCAN == (_miscFlags & J9MODRON_GCCHK_MISC_SCAN));
			mover->run(check, scan);
		}
		mover = mover->getNext();
	}
	if (_miscFlags & J9MODRON_GCCHK_MISC_ABORT) {
		if (_errorCount > 0) {
			abort();
		}
	}
	_engine->endCheckCycle(_javaVM);
}

/**
 * Fix up dead objects.
 * 
 * This routine fixes up any dead objects in the heap so that the checkJ9ObjectPointer()
 * method can identify dead objects by virtue of the fact that their class
 * pointer is unaligned, ie low_tagged.
 * 
 * This is not necessary if the Object Map is available as we can perform the
 * necessary check by calling the memory manager j9gc_ext_is_liveObject() function
 * 
 */
void
GC_CheckCycle::fixDeadObjects(GCCheckInvokedBy invokedBy)
{
	_invokedBy = invokedBy;
	GC_FixDeadObjects fixer = GC_FixDeadObjects(_javaVM, _engine);
	fixer.run(true, false);
}

GC_CheckCycle *
GC_CheckCycle::newInstance(J9JavaVM *javaVM, GC_CheckEngine *engine, const char *args, UDATA manualCountInvocation)
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(javaVM)->getForge();
	
	GC_CheckCycle *checkCycle = (GC_CheckCycle *) forge->allocate(sizeof(GC_CheckCycle), MM_AllocationCategory::DIAGNOSTIC, J9_GET_CALLSITE());
	if(checkCycle) {
		new(checkCycle) GC_CheckCycle(javaVM, engine, manualCountInvocation);
		if (!checkCycle->initialize(args)) {
			checkCycle = NULL;	
		}
	}
	return checkCycle;
}

void
GC_CheckCycle::kill()
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(_javaVM)->getForge();

	/* free the list of checks */	
	GC_Check *mover = _checks;
	while(mover) {
		mover = mover->getNext();
		_checks->kill();
		_checks = mover;
	}

	/* then this object itself */
	forge->free(this);
}

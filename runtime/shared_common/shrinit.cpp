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

/**
 * @file
 * @ingroup Shared_Common
 */

/* Notes on string usage:
 * This details what strings are allocated where and which are null-terminated.
 * String data can either come in from JNI in the jcl helpers (shared.c) or from the bootstrap loader.
 * Strings we care about are:
 * - Class name
 * 		In the helper API, UTF bytes/length are obtained from the JNI calls. These are only required for the duration of the find/store call so are not copied.
 *		Class names from the bootstrap loader are also UTF bytes/length.
 *		Class name is passed to shrinit, where it is copied into a temporary stack buffer, "fixed up" and null-terminated.
 *		Class names are not written into the cache. Instead the UTF8 name in the ROMClass itself is used.
 * - URL/Token/Classpath strings
 *		In the helper API, getPath() is called on URLs and tokens are strings to begin with. UTF bytes/length are obtained for each.
 *		These are then copied and null-terminated because J9ClassPathEntrys are only created/allocated once, so they (and their strings) are kept for the lifetime of the JVM.
 *		The copied strings are copied into a single buffer (sharedClassConfig->jclStringFarm) which is freed on shutdown.
 *		The classpath strings from the bootstrap loader are not copied. It is assumed that these are not going to change or disappear.
 *		ClasspathItems created in shrinit use the same strings from the J9ClassPathEntries as we know these are already safe.
 *		When a ClasspathItem is serialized into the cache, its strings are no-longer null-terminated and the length is returned with the getPath(...) calls (see ClasspathItem.cpp).
 * - Partition strings
 *		These are treated the same way as the URL/Token/Classpath strings, except that partitions can only ever come in from the jcl helpers.
 * - Local hashtable keys
 *		Local hashtable keys are references to strings in the cache. These are not null-terminated.
 * - Cache name and modification context
 *		These are written into the same lump of memory as the sharedClassConfig
 */

extern "C" {
#include "j9user.h"
#include "j9.h"
#include "j9protos.h"
#include "j9consts.h"
#include "jvminit.h"
#include "j9port.h"
#include "shrinit.h"
#include "verbose_api.h"
#include "ut_j9shr.h"
#include "j9shrnls.h"
#include "j9exelibnls.h"
#include "j2sever.h"
#include "zip_api.h"
#include "vmzipcachehook.h"
#include "shcdatautils.h"
#include "simplepool_api.h"
#include "SCStringInternHelpers.h"
#include <string.h>
#include "util_api.h"
}

#include "hookhelpers.hpp"
#include "CacheLifecycleManager.hpp"
#include "CacheMap.hpp"
#include "OSCacheFile.hpp"
#include "SCImplementedAPI.hpp"
#include "UnitTest.hpp"
#include "OMR/Bytes.hpp"

#define SHRINIT_NAMEBUF_SIZE 256
#define SHRINIT_LOW_FREE_DISK_SIZE ((U_64)6 * 1024 * 1024 * 1024)

#define SHARED_STRING_POOL_KEY "j9stringpuddle"
#define SHARED_STRING_POOL_KEY_LENGTH 14
#define SHARED_STRING_PUDDLE_KEY_LENGTH 19 /* pool key + 5 */

#define SHR_MODULE_UPGRADE_PATH_SYS_PROP	SYSPROP_JDK_MODULE_UPGRADE_PATH

#define SHRINIT_CREATE_NEW_LAYER (-2)

#define SHRINIT_TRACE(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_INFO, var)
#define SHRINIT_TRACE1(verbose, var, p1) if (verbose) j9nls_printf(PORTLIB, J9NLS_INFO, var, p1)
#define SHRINIT_TRACE2(verbose, var, p1, p2) if (verbose) j9nls_printf(PORTLIB, J9NLS_INFO, var, p1, p2)
#define SHRINIT_TRACE3(verbose, var, p1, p2, p3) if (verbose) j9nls_printf(PORTLIB, J9NLS_INFO, var, p1, p2, p3)
#define SHRINIT_TRACE_NOTAG(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, var)
#define SHRINIT_TRACE1_NOTAG(verbose, var, p1) if (verbose) j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, var, p1)
#define SHRINIT_TRACE2_NOTAG(verbose, var, p1, p2) if (verbose) j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, var, p1, p2)
#define SHRINIT_TRACE3_NOTAG(verbose, var, p1, p2, p3) if (verbose) j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, var, p1, p2, p3)
#define SHRINIT_TRACE4_NOTAG(verbose, var, p1, p2, p3, p4) if (verbose) j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, var, p1, p2, p3, p4)
#define SHRINIT_INFO_TRACE(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_INFO, var)
#define SHRINIT_ERR_TRACE(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var)
#define SHRINIT_ERR_TRACE1(verbose, var, p1) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1)
#define SHRINIT_ERR_TRACE2(verbose, var, p1, p2) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1, p2)
#define SHRINIT_ERR_TRACE3(verbose, var, p1, p2, p3) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1, p2, p3)
#define SHRINIT_WARNING_TRACE(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_WARNING, var)
#define SHRINIT_WARNING_TRACE1(verbose, var, p1) if (verbose) j9nls_printf(PORTLIB, J9NLS_WARNING, var, p1)
#define SHRINIT_WARNING_TRACE2(verbose, var, p1, p2) if (verbose) j9nls_printf(PORTLIB, J9NLS_WARNING, var, p1, p2)
#define SHRINIT_WARNING_TRACE3(verbose, var, p1, p2, p3) if (verbose) j9nls_printf(PORTLIB, J9NLS_WARNING, var, p1, p2, p3)
#define SHRINIT_WARNING_TRACE4(verbose, var, p1, p2, p3, p4) if (verbose) j9nls_printf(PORTLIB, J9NLS_WARNING, var, p1, p2, p3, p4)
#define PERF_TRACE_EVERY_N_FINDS 100

#define SHRINIT_ASCII_OF_POUND_SYMBOL 0xa3 /* ASCII value of pound symbol */

#if defined(J9VM_OPT_INVARIANT_INTERNING)

#define SHR_TABLE_VERIFY_ASSERT(table, condition) \
do { \
	if (!condition) { \
		if (table != NULL) { \
			table->flags = (table->flags & (~J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS)); \
		} \
		Trc_SHR_Assert_TrueTreeVerify(condition); \
	} \
} while(0)

#define SHR_ENTER_TABLE_VERIFICATION(table) \
do { \
	if ((table != NULL) && (table->flags & J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS) == J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS) { \
		omrthread_monitor_t tablemonitor = table->tableInternFxMutex; \
		J9ThreadAbstractMonitor * abstablemonitor = (J9ThreadAbstractMonitor *) tablemonitor; \
		IDATA enterTableMonitorRC = -1; \
		SHR_TABLE_VERIFY_ASSERT(table, ((abstablemonitor->owner == NULL) || (omrthread_monitor_owned_by_self(tablemonitor) == 1))); \
		enterTableMonitorRC = omrthread_monitor_enter(tablemonitor); \
		if (enterTableMonitorRC != 0) { \
			SHR_TABLE_VERIFY_ASSERT(table, (enterTableMonitorRC == 0)); \
		} \
	} \
} while(0)

#define SHR_EXIT_TABLE_VERIFICATION(table) \
do { \
	if ((table != NULL) && (table->flags & J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS) == J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS) { \
		omrthread_monitor_t tablemonitor = table->tableInternFxMutex; \
		IDATA exitTableMonitorRC = omrthread_monitor_exit(tablemonitor); \
		if (exitTableMonitorRC != 0) { \
			SHR_TABLE_VERIFY_ASSERT(table, (exitTableMonitorRC == 0)); \
		} \
	} \
} while(0)

#else
#define SHR_TABLE_VERIFY_ASSERT(table, condition)
#define SHR_ENTER_TABLE_VERIFICATION(table)
#define SHR_EXIT_TABLE_VERIFICATION(table)
#endif

#define ROUND_TO_SIZEOF_UDATA(number) (((number) + (sizeof(UDATA) - 1)) & (~(sizeof(UDATA) - 1)))

J9SharedClassesHelpText J9SHAREDCLASSESHELPTEXT[] = {
	{OPTION_HELP, J9NLS_SHRC_SHRINIT_HELPTEXT_HELP, 0, 0},
#if defined(WIN32) || defined(WIN64)
	{HELPTEXT_NAMEEQUALS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_NAMEEQUALS_WIN, 0, 0},
#else
	{HELPTEXT_NAMEEQUALS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_NAMEEQUALS2, 0, 0},
	{OPTION_GROUP_ACCESS, J9NLS_SHRC_SHRINIT_HELPTEXT_GROUPACCESS, 0, 0},
#endif
	{HELPTEXT_CACHEDIR_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_CACHEDIR, 0, 0},
#if !defined(WIN32) && !defined(WIN64)
	{HELPTEXT_CACHEDIRPERM_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_CACHEDIRPERM, 0, 0},
	HELPTEXT_NEWLINE,
	{OPTION_SNAPSHOTCACHE, J9NLS_SHRC_SHRINIT_HELPTEXT_SNAPSHOTCACHE, 0, 0},
	{OPTION_RESTORE_FROM_SNAPSHOT, J9NLS_SHRC_SHRINIT_HELPTEXT_RESTORECACHE, 0, 0},
	{OPTION_DESTROYSNAPSHOT, J9NLS_SHRC_SHRINIT_HELPTEXT_DESTROYSNAPSHOT, 0, 0},
	{OPTION_DESTROYALLSNAPSHOTS, J9NLS_SHRC_SHRINIT_HELPTEXT_DESTROYALLSNAPSHOTS, 0, 0},
	{OPTION_PRINT_SNAPSHOTNAME, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINT_SNAPSHOT_FILENAME},
	HELPTEXT_NEWLINE,
#endif
	{OPTION_READONLY, J9NLS_SHRC_SHRINIT_HELPTEXT_READONLY, 0, 0},
	{OPTION_NONPERSISTENT, J9NLS_SHRC_SHRINIT_HELPTEXT_NONPERSISTENT, 0, 0},
	HELPTEXT_NEWLINE,
	{OPTION_DESTROY, J9NLS_SHRC_SHRINIT_HELPTEXT_DESTROY, 0, 0},
	{OPTION_DESTROYALL, J9NLS_SHRC_SHRINIT_HELPTEXT_DESTROYALL, 0, 0},
	{OPTION_DESTROYALLLAYERS, J9NLS_SHRC_SHRINIT_HELPTEXT_DESTROYALLLAYERS, 0, 0},
	HELPTEXT_NEWLINE,
	{OPTION_RESET, J9NLS_SHRC_SHRINIT_HELPTEXT_RESET, 0, 0},
	{HELPTEXT_EXPIRE_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_EXPIRE, 0, 0},
	HELPTEXT_NEWLINE,
#if defined(J9ZOS390)
	{HELPTEXT_STORAGE_KEY_EQUALS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_STORAGE_KEY},
	HELPTEXT_NEWLINE,
#endif
	{OPTION_LISTALLCACHES, J9NLS_SHRC_SHRINIT_HELPTEXT_LISTALLCACHES, 0, 0},
	{HELPTEXT_PRINTSTATS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_2, 0, 0},
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	{HELPTEXT_OPTION_PRINT_TOP_LAYER_STATS, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINT_TOP_LAYER_STATS, 0, 0},
#endif /* J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE */
	{OPTION_PRINTDETAILS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTDETAILS},
	{OPTION_PRINTALLSTATS, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTALLSTATS, 0, 0},
	{OPTION_PRINTORPHANSTATS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTORPHANSTATS},
	HELPTEXT_NEWLINE,
	{OPTION_VERBOSE, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE, 0, 0},
	{OPTION_VERBOSE_IO, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_IO, 0, 0},
	{OPTION_VERBOSE_HELPER, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_HELPER, 0, 0},
	{OPTION_VERBOSE_AOT, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_AOT, 0, 0},
	{OPTION_VERBOSE_JITDATA, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_JITDATA},
	/* Note that this is private for now */
	{OPTION_VERBOSE_DATA, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_DATA},
	{OPTION_VERBOSE_INTERN, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_INTERN},
	{OPTION_VERBOSE_PAGES, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_VERBOSE_PAGES},
	HELPTEXT_NEWLINE,
	{OPTION_SILENT, J9NLS_SHRC_SHRINIT_HELPTEXT_SILENT, 0, 0},
	{OPTION_NONFATAL, J9NLS_SHRC_SHRINIT_HELPTEXT_NONFATAL, 0, 0},
	{OPTION_FATAL, J9NLS_SHRC_SHRINIT_HELPTEXT_FATAL, 0, 0},
	{OPTION_NONE, J9NLS_SHRC_SHRINIT_HELPTEXT_NONE2, 0, 0},
	{OPTION_UTILITIES, J9NLS_SHRC_SHRINIT_HELPTEXT_UTILITIES, 0, 0},
	HELPTEXT_NEWLINE,
	{HELPTEXT_MODIFIEDEQUALS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_MODIFIEDEQUALS, 0, 0},
	HELPTEXT_NEWLINE,
#if defined(AIXPPC)
	{OPTION_PERSISTENT, J9NLS_SHRC_SHRINIT_HELPTEXT_PERSISTENT, 0, 0},
#endif
	{OPTION_NOAOT, J9NLS_SHRC_SHRINIT_HELPTEXT_NOAOT, 0, 0},
	{OPTION_NO_JITDATA, J9NLS_SHRC_SHRINIT_HELPTEXT_NOJITDATA, 0, 0},
	{HELPTEXT_MPROTECTEQUALS_PUBLIC_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_MPROTECT_EQUALS, 0, 0},
#if !defined(J9ZOS390) && !defined(AIXPPC)
	{HELPTEXT_MPROTECTEQUALS_NO_RW_PRIVATE_OPTION, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_MPROTECT_NO_RW},
#endif
#if defined(J9ZOS390) || defined(AIXPPC)
	{HELPTEXT_MPROTECTEQUALS_PARTIAL_PAGES_PRIVATE_OPTION, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_MPROTECT_PARTIAL_PAGES},
	{HELPTEXT_MPROTECTEQUALS_PARTIAL_PAGES_ON_STARTUP_PRIVATE_OPTION, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_MPROTECT_PARTIAL_PAGES_ON_STARTUP},
#endif
	{OPTION_CACHERETRANSFORMED, J9NLS_SHRC_SHRINIT_HELPTEXT_CACHERETRANSFORMED, 0, 0},
	{OPTION_NOBOOTCLASSPATH, J9NLS_SHRC_SHRINIT_HELPTEXT_NOBOOTCLASSPATH_V1, 0, 0},
	{OPTION_BOOTCLASSESONLY, J9NLS_SHRC_SHRINIT_HELPTEXT_BOOTCLASSESONLY, 0, 0},
	{OPTION_ENABLE_BCI, J9NLS_SHRC_SHRINIT_HELPTEXT_ENABLE_BCI, 0, 0},
	{OPTION_DISABLE_BCI, J9NLS_SHRC_SHRINIT_HELPTEXT_DISABLE_BCI, 0, 0},
	{OPTION_RESTRICT_CLASSPATHS, J9NLS_SHRC_SHRINIT_HELPTEXT_RESTRICT_CLASSPATHS, 0, 0},
	{OPTION_ALLOW_CLASSPATHS, J9NLS_SHRC_SHRINIT_HELPTEXT_ALLOW_CLASSPATHS, 0, 0},
	{OPTION_NO_PERSISTENT_DISK_SPACE_CHECK, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_PERSISTENT_DISK_SPACE_CHECK, 0, 0},
	HELPTEXT_NEWLINE,
	{HELPTEXT_INVALIDATE_AOT_METHODS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_INVALIDATE_AOT_METHODS, 0, 0},
	{HELPTEXT_REVALIDATE_AOT_METHODS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_REVALIDATE_AOT_METHODS, 0, 0},
	{HELPTEXT_FIND_AOT_METHODS_OPTION, J9NLS_SHRC_SHRINIT_HELPTEXT_FIND_AOT_METHODS, 0, 0},
	HELPTEXT_NEWLINE,
#if !defined(J9ZOS390)
	{HELPTEXT_RESIZE_EQUALS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_RESIZE_EQUALS},
#endif /* !defined(J9ZOS390) */
	{HELPTEXT_ADJUST_SOFTMX_EQUALS, J9NLS_SHRC_SHRINIT_HELPTEXT_ADJUST_SOFTMX_EQUALS, 0, 0},
	{HELPTEXT_ADJUST_MINAOT_EQUALS, J9NLS_SHRC_SHRINIT_HELPTEXT_ADJUST_MINAOT_EQUALS, 0, 0},
	{HELPTEXT_ADJUST_MAXAOT_EQUALS, J9NLS_SHRC_SHRINIT_HELPTEXT_ADJUST_MAXAOT_EQUALS, 0, 0},
	{HELPTEXT_ADJUST_MINJITDATA_EQUALS, J9NLS_SHRC_SHRINIT_HELPTEXT_ADJUST_MINJIT_EQUALS, 0, 0},
	{HELPTEXT_ADJUST_MAXJITDATA_EQUALS, J9NLS_SHRC_SHRINIT_HELPTEXT_ADJUST_MAXJIT_EQUALS, 0, 0},
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	HELPTEXT_NEWLINE,
	{HELPTEXT_LAYER_EQUALS,J9NLS_SHRC_SHRINIT_HELPTEXT_LAYER_EQUALS, 0, 0},
	{OPTION_CREATE_LAYER, J9NLS_SHRC_SHRINIT_HELPTEXT_CREATE_LAYER, 0, 0},
#endif /* J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE */
	{OPTION_NO_TIMESTAMP_CHECKS, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_TIMESTAMP_CHECKS_V1, 0, 0},
	{OPTION_NO_URL_TIMESTAMP_CHECK, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_URL_TIMESTAMP_CHECK},
	{OPTION_URL_TIMESTAMP_CHECK, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_URL_TIMESTAMP_CHECK},
	{OPTION_NO_CLASSPATH_CACHEING, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_CLASSPATH_CACHEING},
	{OPTION_NO_REDUCE_STORE_CONTENTION, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_REDUCE_STORE_CONTENTION},
	{OPTION_NO_ROUND_PAGES, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_ROUND_PAGES},
	{OPTION_NO_AUTOPUNT, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_AUTOPUNT},
	{OPTION_NO_DETECT_NETWORK_CACHE, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_DETECT_NETWORK_CACHE},
	{OPTION_NO_SEMAPHORE_CHECK, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_SEMAPHORE_CHECK},
#if defined(AIXPPC)
	{OPTION_NO_COREMMAP, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_NO_COREMMAP},
#endif
	HELPTEXT_NEWLINE,
	{OPTION_CHECK_STRING_TABLE, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_CHECK_STRING_TABLE},
	{OPTION_TEST_BAD_BUILDID, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_TEST_BAD_BUILDID},
	{OPTION_PRINT_CACHENAME, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINT_CACHE_FILENAME},
	{OPTION_FORCE_DUMP_IF_CORRUPT, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_FORCE_DUMP_IF_CORRUPT},
	{OPTION_FAKE_CORRUPTION, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_FAKE_CORRUPTION},
	{OPTION_DO_EXTRA_AREA_CHECKS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_DO_EXTRA_AREA_CHECKS},
	{OPTION_CREATE_OLD_GEN, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_CREATE_OLD_GEN},
	{OPTION_DISABLE_CORRUPT_CACHE_DUMPS, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_DISABLE_CORRUPT_CACHE_DUMPS},
	{OPTION_CHECK_STRINGTABLE_RESET, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_CHECK_STRINGTABLE_RESET},
	{OPTION_ADDTESTJITHINT, 0, 0, J9NLS_SHRC_SHRINIT_HELPTEXT_ADD_JIT_HINTS},
	{NULL, 0, 0, 0, 0}
};

J9SharedClassesOptions J9SHAREDCLASSESOPTIONS[] = {
	{ OPTION_MORE_HELP, PARSE_TYPE_EXACT, RESULT_DO_MORE_HELP, 0},
	{ OPTION_HELP, PARSE_TYPE_EXACT, RESULT_DO_HELP, 0},
	{ OPTION_NAME_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_NAME_EQUALS, 0},
	{ OPTION_DESTROY, PARSE_TYPE_EXACT, RESULT_DO_DESTROY, 0},
	{ OPTION_DESTROYALL, PARSE_TYPE_EXACT, RESULT_DO_DESTROYALL, 0},
	{ OPTION_DESTROYALLLAYERS, PARSE_TYPE_EXACT, RESULT_DO_DESTROYALLLAYERS, 0},
	{ OPTION_EXPIRE_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_EXPIRE, 0},
	{ OPTION_LISTALLCACHES, PARSE_TYPE_EXACT, RESULT_DO_LISTALLCACHES, 0},
	{ OPTION_PRINTSTATS, PARSE_TYPE_EXACT, RESULT_DO_PRINTSTATS, 0},
	{ OPTION_PRINTSTATS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_PRINTSTATS_EQUALS, 0},
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	{ OPTION_PRINT_TOP_LAYER_STATS, PARSE_TYPE_EXACT, RESULT_DO_PRINT_TOP_LAYER_STATS, 0},
	{ OPTION_PRINT_TOP_LAYER_STATS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS, 0},
#endif /* J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE */
	{ OPTION_PRINTDETAILS, PARSE_TYPE_EXACT, RESULT_DO_PRINTDETAILS, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS},
	{ OPTION_PRINTALLSTATS, PARSE_TYPE_EXACT, RESULT_DO_PRINTALLSTATS, 0},
	{ OPTION_PRINTALLSTATS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_PRINTALLSTATS_EQUALS, 0},
	{ OPTION_NO_TIMESTAMP_CHECKS, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_TIMESTAMP_CHECKS},
	{ OPTION_NO_CLASSPATH_CACHEING, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_LOCAL_CACHEING},
	{ OPTION_NO_REDUCE_STORE_CONTENTION, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION},
	{ OPTION_VERBOSE, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE},
	{ OPTION_VERBOSE_IO, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_IO},
	{ OPTION_VERBOSE_HELPER, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_HELPER},
	{ OPTION_VERBOSE_AOT, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_AOT},
	{ OPTION_VERBOSE_JITDATA, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA},
	{ OPTION_MODIFIED_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_MODIFIED_EQUALS, 0},
	{ OPTION_NO_BYTECODEFIX, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_BYTECODEFIX},
	{ OPTION_TRACECOUNT, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_TRACECOUNT},
	{ OPTION_GROUP_ACCESS, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS},
	{ OPTION_PRINTORPHANSTATS, PARSE_TYPE_EXACT, RESULT_DO_PRINTORPHANSTATS, 0},
	{ OPTION_SILENT, PARSE_TYPE_EXACT, RESULT_DO_SET_VERBOSEFLAG, 0},
	{ OPTION_NONFATAL, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL},
	{ OPTION_FATAL, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL},
	{ OPTION_CONTROLDIR_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_CTRLD_EQUALS, 0},
	{ OPTION_NOAOT, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_AOT},
	{ OPTION_NO_JITDATA, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_JITDATA},
	{ OPTION_VERBOSE_INTERN, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_INTERN},
	{ OPTION_VERBOSE_PAGES, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_PAGES},
	{ OPTION_NO_ROUND_PAGES, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_ROUND_TO_PAGE_SIZE},
	{ OPTION_MPROTECT_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_MPROTECT_EQUALS, 0},
	{ OPTION_CACHERETRANSFORMED, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_CACHERETRANSFORMED},
	{ OPTION_NOBOOTCLASSPATH, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_CACHEBOOTCLASSES},
	{ OPTION_BOOTCLASSESONLY, PARSE_TYPE_EXACT, RESULT_DO_BOOTCLASSESONLY, J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES},
	{ OPTION_VERBOSE_DATA, PARSE_TYPE_EXACT, RESULT_DO_ADD_VERBOSEFLAG, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DATA},
	{ OPTION_SINGLEJVM, PARSE_TYPE_EXACT, RESULT_DO_NOTHING, 0},
	{ OPTION_KEEP, PARSE_TYPE_EXACT, RESULT_DO_NOTHING, 0},
	{ OPTION_NO_COREMMAP, PARSE_TYPE_EXACT, RESULT_NO_COREMMAP_SET, 0},		/* Only valid on AIX, but will be ignored on other platforms */
	{ OPTION_CACHEDIR_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_CACHEDIR_EQUALS, 0},
	{ OPTION_RESET, PARSE_TYPE_EXACT, RESULT_DO_RESET, 0},
	{ OPTION_READONLY, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_READONLY},
	{ OPTION_PERSISTENT, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE | J9SHR_RUNTIMEFLAG_PERSISTENT_KEYWORD},
	{ OPTION_NONPERSISTENT, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE},
	{ OPTION_NO_AUTOPUNT, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID},
	{ OPTION_NO_DETECT_NETWORK_CACHE, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_DETECT_NETWORK_CACHE},
#if !defined(WIN32)
	{ OPTION_SNAPSHOTCACHE, PARSE_TYPE_EXACT, RESULT_DO_SNAPSHOTCACHE, J9SHR_RUNTIMEFLAG_SNAPSHOT},
	{ OPTION_DESTROYSNAPSHOT, PARSE_TYPE_EXACT, RESULT_DO_DESTROYSNAPSHOT, 0},
	{ OPTION_DESTROYALLSNAPSHOTS, PARSE_TYPE_EXACT, RESULT_DO_DESTROYALLSNAPSHOTS, 0},
	{ OPTION_RESTORE_FROM_SNAPSHOT, PARSE_TYPE_EXACT, RESULT_DO_RESTORE_FROM_SNAPSHOT, J9SHR_RUNTIMEFLAG_RESTORE},
	{ OPTION_PRINT_SNAPSHOTNAME, PARSE_TYPE_EXACT, RESULT_DO_PRINT_SNAPSHOTNAME, 0},
#endif /* !defined(WIN32) */
	{ OPTION_UTILITIES, PARSE_TYPE_EXACT, RESULT_DO_UTILITIES, 0},
	{ OPTION_PRINT_CACHENAME, PARSE_TYPE_EXACT, RESULT_DO_PRINT_CACHENAME, 0},
	{ OPTION_NO_SEMAPHORE_CHECK, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_SEMAPHORE_CHECK},
	{ OPTION_VERIFY_TREE, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_VERIFY_TREE_AND_TREE_ACCESS },
	{ OPTION_CHECK_STRING_TABLE, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_STRING_TABLE_CHECK},
	{ OPTION_TEST_BAD_BUILDID, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID},
	{ OPTION_FORCE_DUMP_IF_CORRUPT, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_FORCE_DUMP_IF_CORRUPT},
	{ OPTION_FAKE_CORRUPTION, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_FAKE_CORRUPTION},
	{ OPTION_DO_EXTRA_AREA_CHECKS, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS},
	{ OPTION_CREATE_OLD_GEN, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_CREATE_OLD_GEN},
	{ OPTION_DISABLE_CORRUPT_CACHE_DUMPS, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS},
	{ OPTION_CACHEDIRPERM_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_CACHEDIRPERM_EQUALS, 0},
	{ OPTION_CHECK_STRINGTABLE_RESET, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READONLY | J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READWRITE},
	{ OPTION_ENABLE_BCI, PARSE_TYPE_EXACT, RESULT_DO_ENABLE_BCI, J9SHR_RUNTIMEFLAG_ENABLE_BCI },
	{ OPTION_DISABLE_BCI, PARSE_TYPE_EXACT, RESULT_DO_DISABLE_BCI, J9SHR_RUNTIMEFLAG_DISABLE_BCI },
	{ OPTION_ADJUST_SOFTMX_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADJUST_SOFTMX_EQUALS, 0 },
	{ OPTION_ADJUST_MINAOT_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADJUST_MINAOT_EQUALS, 0 },
	{ OPTION_ADJUST_MAXAOT_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADJUST_MAXAOT_EQUALS, 0 },
	{ OPTION_ADJUST_MINJITDATA_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADJUST_MINJITDATA_EQUALS, 0 },
	{ OPTION_ADJUST_MAXJITDATA_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADJUST_MAXJITDATA_EQUALS, 0 },
	{ OPTION_ADDTESTJITHINT, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ADD_TEST_JITHINT},
	{ OPTION_STORAGE_KEY_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_ADD_STORAGE_KEY_EQUALS, 0},
	{ OPTION_RESTRICT_CLASSPATHS, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_RESTRICT_CLASSPATHS },
	{ OPTION_ALLOW_CLASSPATHS, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ALLOW_CLASSPATHS },
	{ OPTION_INVALIDATE_AOT_METHODS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS, J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE},
	{ OPTION_REVALIDATE_AOT_METHODS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS, J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE},
	{ OPTION_FIND_AOT_METHODS_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_FIND_AOT_METHODS_EQUALS, J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE},
	{ OPTION_NO_URL_TIMESTAMP_CHECK, PARSE_TYPE_EXACT, RESULT_DO_REMOVE_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_URL_TIMESTAMP_CHECK},
	{ OPTION_URL_TIMESTAMP_CHECK, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_ENABLE_URL_TIMESTAMP_CHECK},
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	{ OPTION_LAYER_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_LAYER_EQUALS, 0 },
	{ OPTION_CREATE_LAYER, PARSE_TYPE_EXACT, RESULT_DO_CREATE_LAYER, 0 },
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
	{ OPTION_NO_PERSISTENT_DISK_SPACE_CHECK, PARSE_TYPE_EXACT, RESULT_DO_ADD_RUNTIMEFLAG, J9SHR_RUNTIMEFLAG_NO_PERSISTENT_DISK_SPACE_CHECK},
	{ OPTION_RESIZE_EQUALS, PARSE_TYPE_STARTSWITH, RESULT_DO_RESIZE_CACHE_EQUALS, 0},
	{ NULL, 0, 0 }
};

IDATA UnitTest::unitTest = UnitTest::NO_TEST;
void * UnitTest::cacheMemory = NULL;
U_32 UnitTest::cacheSize = 0;

extern "C" {

static UDATA initializeSharedStringTable(J9JavaVM* vm);
static void resetSharedTable(J9SharedInvariantInternTable* sharedTable);
static BOOLEAN verifyStringTableElement(void *address, void *userData);
bool modifyCacheName(J9JavaVM *vm, const char* origName, UDATA verboseFlags, char** modifiedCacheName, UDATA bufLen);
static BOOLEAN j9shr_parseMemSize(char * str, UDATA & value);
static void addTestJitHint(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData);
static IDATA j9shr_restoreFromSnapshot(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, bool* cacheExist);
static void j9shr_print_snapshot_filename(J9JavaVM* vm, const char* cacheDirName, const char* snapshotName, I_8 layer);
static IDATA j9shr_aotMethodOperation(J9JavaVM* vm, char* methodSpecs, UDATA action);
static bool recoverMethodSpecSeparator(char* string, char* end);
static void adjustCacheSizes(J9PortLibrary* portlib, UDATA verboseFlags, J9SharedClassPreinitConfig* piconfig, U_64 newSize);
static IDATA checkIfCacheExists(J9JavaVM* vm, const char* ctrlDirName, char* cacheDirName, const char* cacheName, J9PortShcVersion* versionData, U_32 cacheType, I_8 layer);
static bool isClassFromPatchedModule(J9VMThread* vmThread, J9Module *j9module, U_8* className, UDATA classNameLength, J9ClassLoader* classLoader);
static J9Module* getModule(J9VMThread* vmThread, U_8* className, UDATA classNameLength, J9ClassLoader* classLoader);
static bool isFreeDiskSpaceLow(J9JavaVM *vm, U_64* maxsize, U_64 runtimeFlags);
static char* generateStartupHintsKey(J9JavaVM *vm);
static void fetchStartupHintsFromSharedCache(J9VMThread* vmThread);
static void findExistingCacheLayerNumbers(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, U_64 runtimeFlags, I_8 *maxLayerNo);
static void j9shr_resizeCache(J9VMThread* currentThread);

typedef struct J9SharedVerifyStringTable {
	void *romClassAreaStart;
	void *romClassAreaEnd;
	J9SimplePool *simplePool;
} J9SharedCheckStringTable;


/* Imples build flag J9VM_OPT_ZIP_SUPPORT - if we don't have this, we're stuffed */
void
j9shr_hookZipLoadEvent(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData)
{
	J9VMZipLoadEvent* event = (J9VMZipLoadEvent*)eventData;
	char* filename = (char*)event->cpPath;
	UDATA state = event->newState;
	J9JavaVM* vm = (J9JavaVM*)event->userData;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
	U_64 localRuntimeFlags = vm->sharedClassConfig->runtimeFlags;

	/* Don't call notifyClasspathEntryStateChange for JCL(non-bootstrap) callers with checkURLTimestamps specified */
	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE)
	|| (localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS)
	|| ((NULL == event->zipfile) && (localRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_URL_TIMESTAMP_CHECK))
	) {
		return;
	}

	/* Only trigger notifyClasspathEntryStateChange if the returnCode indicates success */
	if (vm && vm->sharedClassConfig && (0 == event->returnCode)) {
		((SH_CacheMap*)(vm->sharedClassConfig->sharedClassCache))->notifyClasspathEntryStateChange(currentThread, (const char*)filename, state);
	}
}

IDATA
j9shr_print_stats(J9JavaVM *vm, UDATA parseResult, U_64 runtimeFlags, UDATA printStatsOptions);

static void
j9shr_print_cache_filename(J9JavaVM* vm, const char* cacheDirName, U_64 runtimeFlags, const char* cacheName, I_8 layer);

static void
reportUtilityNotApplicable(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, UDATA verboseFlags, U_64 runtimeFlags, UDATA command);

static void
printRuntimeFlags (J9PortLibrary* portLibrary, U_64 runtimeFlags, UDATA verboseFlags)
{
	PORT_ACCESS_FROM_PORT(portLibrary);

	if (verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE) {
		if (!(runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_TIMESTAMP_CHECKS)) {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_TIMESTAMP_DISABLED_INFO);
		}

		if (!(runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_LOCAL_CACHEING)) {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_CPCACHEING_DISABLED_INFO);
		}

		if (!(runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_STCONT_REDUCE_DISABLED_INFO);
		}

		if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS)) {
			SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_GROUP_ACCESS_INFO);
		}
			if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE)) {
			SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_PERSISTENT_CACHE_ENABLED_INFO);
		} else {
			SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_PERSISTENT_CACHE_DISABLED_INFO);
		}

		if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_CACHERETRANSFORMED)) {
			SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_CACHERETRANSFORMED_INFO);
		}

		if (!(runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_CACHEBOOTCLASSES)) {
			SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_NOBOOTCLASSPATH_INFO);
		}

		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_ENABLED_INFO);
	}

	if ((verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_IO)) {
		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_IO_ENABLED_INFO);
	}

	if ((verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_HELPER)) {
		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_HELPER_ENABLED_INFO);
	}

	if ((verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_AOT)) {
		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_AOT_ENABLED_INFO);
	}

	if ((verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DATA)) {
		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_DATA_ENABLED_INFO);
	}

	if ((verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA)) {
		SHRINIT_TRACE_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_VERBOSE_JITDATA_ENABLED_INFO);
	}
}
/**
 * 	Parses the given string and returns the sequence of characters until the next delim in the string.
 *  If the next char after the found token is '\0', then it is the end of string and
 *  return false indicating that there are no more tokens.
 *  If the next char after the found token is delim,
 *  then more token(s) are expected after the found token separated by delim.
 *  Therefore, move the current string pointer (*string) after the delim character.
 *
 *
 * @param char ** string 	Original string to be split from delim char
 * @param char delim 		Delimiter character
 * @param char ** token 	Sequence of chars until the next delim in the given string
 *
 * @return bool				Returns false if the end of string is reached
 * 							meaning that there are no more chars in the string,
 * 							true otherwise
 */
static bool getNextToken (char ** string, char delim, char ** token)
{
	int i = 0;
	for (; (('\0' != (*string)[i]) && (delim != (*string)[i])); i++) {
		(*token)[i] = (*string)[i];
	}

	(*token)[i] = '\0';

	if ('\0' == (*string)[i]) {
		return false;
	} else {
		(*string) += i + 1;
		return true;
	}
}
/**
 * This function replaces '\0' with ',' within {} in the <method_specification> string provided for "invalidateAotMethods/revalidateAotMethods/findAotMethods" option
 * @param[in] string Pointer to the start of the method specs string
 * @param[in] end Pointer to the end of the method specs string
 *
 * @return false if '{' is found but '}' missing, true otherwise
 */
static bool
recoverMethodSpecSeparator(char* string, char* end)
{
	bool rc = false;
	bool inBrace = false;
	char* cursor = string;

	Trc_SHR_Assert_False((NULL == string) || (NULL == end));

	if (NULL == strchr(string, '{')) {
		/* do nothing */
		return true;
	}

	while(('}' != *cursor)
		&& (cursor < end)
	) {
		if ('{' == *cursor) {
			inBrace = true;
		} else if (inBrace && ('\0' == *cursor)) {
			*cursor = ',';
		}
		cursor += 1;
	}

	if (end == cursor) {
		/* '{' found but '}' missing */
		rc = false;
	} else {
		/* '}' found */
		inBrace = false;
		rc = true;
	}
	return rc;
}

UDATA
parseArgs(J9JavaVM* vm, char* options, U_64* runtimeFlags, UDATA* verboseFlags, char** cacheName,
		char** modContext, char** expireTime, char** ctrlDirName, char **cacheDirPerm, char** methodSpecs, UDATA* printStatsOptions, UDATA* storageKeyTesting)
{
	UDATA returnAction = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);
	char* tempStr;
	IDATA tempInt;
	bool cacheDirSet = false;
	char* optionEnd = options + SHR_SUBOPT_BUFLEN;
#if defined(AIXPPC)
	bool noCoreMmap;
	UDATA lastAction = UDATA_MAX;
#endif

	while (*options) {
		IDATA i=0;

		while(J9SHAREDCLASSESOPTIONS[i].option != NULL) {
			const char* compareTo = J9SHAREDCLASSESOPTIONS[i].option;
			UDATA compareToLen = strlen(compareTo);
			char* found = options;
			UDATA rc = try_scan(&found, compareTo);		/* provides case insensitive compare. "found" not used. */

			if (rc) {
				if (J9SHAREDCLASSESOPTIONS[i].parseType==PARSE_TYPE_EXACT) {
					if (options[compareToLen]=='\0') break;
				} else
				if (J9SHAREDCLASSESOPTIONS[i].parseType==PARSE_TYPE_STARTSWITH) {
					if (options[compareToLen]=='\0') {
						/* Potentially don't know verboseFlags yet, so this msg cannot be suppressed by "silent" */
						SHRINIT_ERR_TRACE1(1, J9NLS_SHRC_SHRINIT_REQUIRES_SUBOPTION, options);
						return RESULT_PARSE_FAILED;
					}
					break;
				} else
				if (J9SHAREDCLASSESOPTIONS[i].parseType==PARSE_TYPE_OPTIONAL) {
					break;
				}
			}
			i++;
		}

		if (NULL == J9SHAREDCLASSESOPTIONS[i].option) {
			/* Potentially don't know verboseFlags yet, so this msg cannot be suppressed by "silent" */
			SHRINIT_ERR_TRACE1(1, J9NLS_SHRC_SHRINIT_OPTION_UNRECOGNISED, options);
			return RESULT_PARSE_FAILED;
		}

		switch(J9SHAREDCLASSESOPTIONS[i].action) {
		case RESULT_DO_REMOVE_RUNTIMEFLAG:
			*runtimeFlags &= ~(J9SHAREDCLASSESOPTIONS[i].flag);
			break;

		case RESULT_DO_ADD_RUNTIMEFLAG:
			*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
			break;

		case RESULT_DO_SET_VERBOSEFLAG:
			*verboseFlags = (UDATA)J9SHAREDCLASSESOPTIONS[i].flag;
			break;

		case RESULT_DO_ADD_VERBOSEFLAG:
			*verboseFlags |= (UDATA)J9SHAREDCLASSESOPTIONS[i].flag;
			break;

		case RESULT_DO_NAME_EQUALS:
			*cacheName = options + strlen(OPTION_NAME_EQUALS);
			options += strlen(OPTION_NAME_EQUALS)+strlen(*cacheName)+1;
			continue;

		case RESULT_DO_CTRLD_EQUALS:
			/* Ignore controlDir unless cacheDir is not set */
			if (!cacheDirSet) {
				*ctrlDirName = options + strlen(OPTION_CONTROLDIR_EQUALS);
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_CACHEDIR_PRESENT;
			}
			options += strlen(options)+1;
			continue;

		case RESULT_DO_CACHEDIR_EQUALS:
			*ctrlDirName = options + strlen(OPTION_CACHEDIR_EQUALS);
			*runtimeFlags |= J9SHR_RUNTIMEFLAG_CACHEDIR_PRESENT;
			options += strlen(OPTION_CACHEDIR_EQUALS)+strlen(*ctrlDirName)+1;
			cacheDirSet = true;
			continue;

		case RESULT_DO_MODIFIED_EQUALS:
			*modContext = options + strlen(OPTION_MODIFIED_EQUALS);
			options += strlen(OPTION_MODIFIED_EQUALS)+strlen(*modContext)+1;
			continue;

		case RESULT_DO_LAYER_EQUALS:
		{
			UDATA temp = 0;
			char* layerString = options + strlen(OPTION_LAYER_EQUALS);
			char* cursor = layerString;
			if ((scan_udata(&cursor, &temp) == 0)
				&& (temp <= J9SH_LAYER_NUM_MAX_VALUE)
			) {
				vm->sharedCacheAPI->layer = (I_8)temp;
			} else {
				SHRINIT_ERR_TRACE3(1, J9NLS_SHRC_SHRINIT_OPTION_INVALID_LAYER_NUMBER, temp, OPTION_LAYER_EQUALS, J9SH_LAYER_NUM_MAX_VALUE + 1);
				return RESULT_PARSE_FAILED;
			}
			options += strlen(OPTION_LAYER_EQUALS)+ (cursor - layerString) +1;
			continue;
		}
		case RESULT_DO_CREATE_LAYER:
		{
			vm->sharedCacheAPI->layer = SHRINIT_CREATE_NEW_LAYER;
			break;
		}
		case RESULT_DO_ADJUST_SOFTMX_EQUALS:
		case RESULT_DO_ADJUST_MINAOT_EQUALS:
		case RESULT_DO_ADJUST_MAXAOT_EQUALS:
		case RESULT_DO_ADJUST_MINJITDATA_EQUALS:
		case RESULT_DO_ADJUST_MAXJITDATA_EQUALS:
		case RESULT_DO_RESIZE_CACHE_EQUALS:
		{
			UDATA tmpareasize = (UDATA)-1;
			char * areasize = options + strlen(J9SHAREDCLASSESOPTIONS[i].option);
			BOOLEAN parseAreaSize = j9shr_parseMemSize(areasize, tmpareasize);

			if (FALSE == parseAreaSize) {
				SHRINIT_ERR_TRACE1(1, J9NLS_SHRC_SHRINIT_OPTION_INVALID_MEMORY_SIZE, options);
				return RESULT_PARSE_FAILED;
			}
			if (J9_ARE_ALL_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_READONLY)) {
				*runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_READONLY;
				SHRINIT_WARNING_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_OPTION_IGNORED_WARNING, OPTION_READONLY, J9SHAREDCLASSESOPTIONS[i].option, OPTION_READONLY);
			}
			if (RESULT_DO_ADJUST_SOFTMX_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				vm->sharedCacheAPI->softMaxBytes = (U_32)tmpareasize;
			} else if (RESULT_DO_ADJUST_MINAOT_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				vm->sharedCacheAPI->minAOT = (I_32)tmpareasize;
			} else if (RESULT_DO_ADJUST_MAXAOT_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				vm->sharedCacheAPI->maxAOT = (I_32)tmpareasize;
			} else if (RESULT_DO_ADJUST_MINJITDATA_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				vm->sharedCacheAPI->minJIT = (I_32)tmpareasize;
			} else if (RESULT_DO_ADJUST_MAXJITDATA_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				vm->sharedCacheAPI->maxJIT = (I_32)tmpareasize;
			} else {
				vm->sharedCacheAPI->newCacheSize = (U_32)tmpareasize;
			}
			options += strlen(J9SHAREDCLASSESOPTIONS[i].option) + strlen(areasize) + 1;
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
			*runtimeFlags |= J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE;
			continue;
		}

		case RESULT_DO_EXPIRE:
			if (J9_ARE_ALL_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_SNAPSHOT)) {
				/* ignore "snapshotCache" if it is specified before expire option */
				*runtimeFlags &= ~J9SHR_RUNTIMEFLAG_SNAPSHOT;
			}
			*expireTime = options + strlen(OPTION_EXPIRE_EQUALS);
			options += strlen(OPTION_EXPIRE_EQUALS)+strlen(*expireTime)+1;
			returnAction = RESULT_DO_EXPIRE;
			continue;

		case RESULT_DO_CACHEDIRPERM_EQUALS:
			*cacheDirPerm = options + strlen(OPTION_CACHEDIRPERM_EQUALS);
			options += strlen(OPTION_CACHEDIRPERM_EQUALS)+strlen(*cacheDirPerm)+1;
			continue;

		case RESULT_DO_MPROTECT_EQUALS:
			tempStr = options + strlen(OPTION_MPROTECT_EQUALS);
			if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_NONE)) {
				*runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW
								| J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND);
				tempInt = strlen(SUB_OPTION_MPROTECT_NONE);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_ALL)) {
				*runtimeFlags |= (J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW
								| J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES
								| J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND);
				tempInt = strlen(SUB_OPTION_MPROTECT_ALL);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_DEF)) {
#if defined(J9ZOS390) || defined(AIXPPC)
				/* Calling mprotect on the RW when using persistent caches on AIX and ZOS
				 *  has a severe performance impact on class loading (approx ~20% slower)
				 */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT;
#else
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
				*runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND | J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP);
				tempInt = strlen(SUB_OPTION_MPROTECT_DEF);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_ONFIND)) {
				/* When enabling mprotect=onfind, ensure flags for mprotect=default along with J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES are also set */
#if defined(J9ZOS390) || defined(AIXPPC)
				/* Calling mprotect on the RW when using persistent caches on AIX and ZOS
				 *  has a severe performance impact on class loading (approx ~20% slower)
				 */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#else
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND;
				tempInt = strlen(SUB_OPTION_MPROTECT_ONFIND);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_PARTIAL_PAGES_ON_STARTUP)) {
				/* When enabling mprotect=partialpagesonstartup, ensure flags for mprotect=default are also set */
#if defined(J9ZOS390) || defined(AIXPPC)
				/* Calling mprotect on the RW when using persistent caches on AIX and ZOS
				 *  has a severe performance impact on class loading (approx ~20% slower)
				 */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#else
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP;
				tempInt = strlen(SUB_OPTION_MPROTECT_PARTIAL_PAGES_ON_STARTUP);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_PARTIAL_PAGES)) {
				/* When enabling mprotect=partialpages, ensure flags for mprotect=default are also set.
				 * Calling mprotect on the RW when using persistent caches on AIX and ZOS
				 * has a severe performance impact on class loading (approx ~20% slower).
				 */
#if defined(J9ZOS390)
				/* Moreover, on z/OS partial page protection needs to be enabled since JVM startup (which is flag J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP),
				 * otherwise if this JVM protects partially filled pages,
				 * then any subsequent JVM can crash if it attempts to write to cache during startup phase.
				 */
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP;
#elif defined(AIXPPC)
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#else
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
				tempInt = strlen(SUB_OPTION_MPROTECT_PARTIAL_PAGES);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_NO_RW)) {
				/* Never enable MPROTECT_ALL without MPROTECT_RW */
				if ((*runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL) == 0) {
					*runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
				}
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT;
				tempInt = strlen(SUB_OPTION_MPROTECT_NO_RW);
			} else if (0 == strcmp(tempStr, SUB_OPTION_MPROTECT_NO_PARTIAL_PAGES)) {
				*runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND);
				tempInt = strlen(SUB_OPTION_MPROTECT_NO_PARTIAL_PAGES);
			} else {
				/* Potentially don't know verboseFlags yet, so this msg cannot be suppressed by "silent" */
				SHRINIT_ERR_TRACE(1, J9NLS_SHRC_SHRINIT_MPROTECT_UNRECOGNISED);
				return RESULT_PARSE_FAILED;
			}
			options += strlen(OPTION_MPROTECT_EQUALS)+tempInt+1;
			continue;

		case RESULT_DO_PRINTDETAILS:
			if (0 == returnAction) {
				returnAction = RESULT_DO_PRINTSTATS;
			}
			*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
			break;
		case RESULT_DO_PRINTALLSTATS_EQUALS:
		case RESULT_DO_PRINTSTATS_EQUALS:
		case RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS:
		{
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			if (RESULT_DO_PRINTALLSTATS_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				tempStr = options + strlen(OPTION_PRINTALLSTATS_EQUALS);
				tempInt = strlen(OPTION_PRINTALLSTATS_EQUALS) + strlen(tempStr);
			} else if (RESULT_DO_PRINTSTATS_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
				tempStr = options + strlen(OPTION_PRINTSTATS_EQUALS);
				tempInt = strlen(OPTION_PRINTSTATS_EQUALS) + strlen(tempStr);
			} else {
				tempStr = options + strlen(OPTION_PRINT_TOP_LAYER_STATS_EQUALS);
				tempInt = strlen(OPTION_PRINT_TOP_LAYER_STATS_EQUALS) + strlen(tempStr);
				*printStatsOptions |= PRINTSTATS_SHOW_TOP_LAYER_ONLY;
			}

			char delim = '+';
			char  * filter = (char*)j9mem_allocate_memory(strlen(tempStr)+1, J9MEM_CATEGORY_CLASSES);
			if (NULL == filter) {
				SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_ALLOCATING_CHAR_ARRAY);
				return RESULT_PARSE_FAILED;
			}
			char * filterOriginal = filter;

			bool hasMoreToken = getNextToken(&tempStr, delim, &filter);

		    while(true) {
		    	int filterLength= ((int)strlen(filter)) + 1; /* 1 is added for the end char '\0' */

		    	/* -Xshareclasses:printallstats=<public options> */
		         if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_ALL))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_ALL))) {
		        	 *printStatsOptions |=  PRINTSTATS_SHOW_ALL;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_CLASSPATH))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_CLASSPATH))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_CLASSPATH;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_URL))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_URL))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_URL;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_TOKEN))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_TOKEN))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_TOKEN;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_ROMCLASS))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_ROMCLASS))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_ROMCLASS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_ROMMETHOD))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_ROMMETHOD))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_ROMMETHOD;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_AOT))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_AOT))) {
			         *printStatsOptions |=  PRINTSTATS_SHOW_AOT;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_INVALIDATEDAOT))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_INVALIDATEDAOT))) {
		        	 *printStatsOptions |=  PRINTSTATS_SHOW_INVALIDATEDAOT;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_JITPROFILE))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_JITPROFILE))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_JITPROFILE;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_JITHINT))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_JITHINT))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_JITHINT;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_ZIPCACHE))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_ZIPCACHE))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_ZIPCACHE;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_STALE))
		       		     && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_STALE))) {
		       		  *printStatsOptions |= PRINTSTATS_SHOW_ALL_STALE;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_STARTUPHINT))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_STARTUPHINT))) {
		        	  *printStatsOptions |= PRINTSTATS_SHOW_STARTUPHINT;
		         /* -Xshareclasses:printallstats=<private options> For private options, it is default to print details. */
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_EXTRA))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_EXTRA))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_EXTRA;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_ORPHAN))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_ORPHAN))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_ORPHAN;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_AOTCH))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_AOTCH))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_AOTCH;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_AOTTHUNK))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_AOTTHUNK))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_AOTTHUNK;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_AOTDATA))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_AOTDATA))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_AOTDATA;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_JCL))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_JCL))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_JCL;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_BYTEDATA))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_BYTEDATA))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_BYTEDATA;
		        	 *runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS;
	        	 /* -Xshareclasses:printallstats=<help options> */
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_HELP))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_HELP))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_HELP;
		         } else if ((filterLength == sizeof(SUB_OPTION_PRINTSTATS_MOREHELP))
		        		 && (0 != try_scan(&filter, SUB_OPTION_PRINTSTATS_MOREHELP))) {
		        	 *printStatsOptions |= PRINTSTATS_SHOW_MOREHELP;
		         } else {
		        	 if (RESULT_DO_PRINTALLSTATS_EQUALS == J9SHAREDCLASSESOPTIONS[i].action) {
		        		 SHRINIT_ERR_TRACE(1, J9NLS_SHRC_SHRINIT_PRINTALLSTATS_UNRECOGNISED);
		        	 } else {
		        		 SHRINIT_ERR_TRACE(1, J9NLS_SHRC_SHRINIT_PRINTSTATS_UNRECOGNISED);
		        	 }

					j9mem_free_memory(filterOriginal);
					return RESULT_PARSE_FAILED;
		         }
		         if (hasMoreToken) {
		        	 hasMoreToken = getNextToken(&tempStr, delim, &filter);
		         } else {
		        	 break;
		         }
		    }
		    j9mem_free_memory(filterOriginal);
		    options += tempInt + 1;
		    continue;
		}

		case RESULT_DO_PRINTALLSTATS:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			*printStatsOptions |= PRINTSTATS_SHOW_ALL;
			break;

		case RESULT_DO_PRINTORPHANSTATS:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			*printStatsOptions = (PRINTSTATS_SHOW_ORPHAN | PRINTSTATS_SHOW_ALL);
			break;

		case RESULT_DO_PRINTSTATS:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			*printStatsOptions |=  PRINTSTATS_SHOW_NONE;
			break;
		case RESULT_DO_PRINT_TOP_LAYER_STATS:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			*printStatsOptions |= (PRINTSTATS_SHOW_NONE | PRINTSTATS_SHOW_TOP_LAYER_ONLY);
			break;

		case RESULT_DO_DESTROY:
		case RESULT_DO_DESTROYALL:
		case RESULT_DO_DESTROYALLLAYERS:
		case RESULT_DO_PRINT_CACHENAME:
		case RESULT_DO_PRINT_SNAPSHOTNAME:
		case RESULT_DO_LISTALLCACHES:
		case RESULT_DO_TEST_INTERNAVL:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if defined(AIXPPC)
			lastAction = returnAction;
#endif /* defined(AIXPPC) */
			break;

		case RESULT_DO_RESET:
			if (J9_ARE_NO_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_SNAPSHOT)) {
				returnAction = J9SHAREDCLASSESOPTIONS[i].action;
#if !defined(WIN32)
			} else {
				/* ignore "reset" if option "snapshotCache" presents */
				SHRINIT_WARNING_TRACE3(*verboseFlags, J9NLS_SHRC_SHRINIT_OPTION_IGNORED_WARNING, OPTION_RESET, OPTION_SNAPSHOTCACHE, OPTION_RESET);
#endif /* !defined(WIN32) */
			}
			break;

		case RESULT_NO_COREMMAP_SET:
#if defined(AIXPPC)
			noCoreMmap = true;
#endif /* defined(AIXPPC) */
			break;

		case RESULT_DO_ENABLE_BCI:
			*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
			/* Clear J9SHR_RUNTIMEFLAG_DISABLE_BCI in case it was set earlier */
			*runtimeFlags &= (~J9SHR_RUNTIMEFLAG_DISABLE_BCI);
			break;

		case RESULT_DO_DISABLE_BCI:
			*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
			/* Clear J9SHR_RUNTIMEFLAG_ENABLE_BCI in case it was set earlier */
			*runtimeFlags &= (~J9SHR_RUNTIMEFLAG_ENABLE_BCI);
			break;

		case RESULT_DO_ADD_STORAGE_KEY_EQUALS:
			tempStr = options + strlen(OPTION_STORAGE_KEY_EQUALS);
			options += strlen(OPTION_STORAGE_KEY_EQUALS)+strlen(tempStr)+1;

			if (!scan_udata(&tempStr, storageKeyTesting)) {
				if (*storageKeyTesting < 16) {
					*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_STORAGEKEY_TESTING;
				}
			}

			continue;

		case RESULT_DO_NOTHING:
			break;
#if !defined(WIN32)
		case RESULT_DO_SNAPSHOTCACHE:
		case RESULT_DO_RESTORE_FROM_SNAPSHOT:
			*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
			break;
		case RESULT_DO_DESTROYSNAPSHOT:
		case RESULT_DO_DESTROYALLSNAPSHOTS:
			returnAction = J9SHAREDCLASSESOPTIONS[i].action;
			break;
#endif /* !defined(WIN32) */
		case RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS:
		case RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS:
		case RESULT_DO_FIND_AOT_METHODS_EQUALS:
			/* GET_OPTION_VALUES() in shrclsssup.c separate the options by ':' and ',',
			 * However, option values passed within {}, such as {a,b,c} should be treated as one value passed to one option.
			 */
			if (true == recoverMethodSpecSeparator(options + strlen(J9SHAREDCLASSESOPTIONS[i].option), optionEnd)) {
				*runtimeFlags |= J9SHAREDCLASSESOPTIONS[i].flag;
				*methodSpecs = options + strlen(J9SHAREDCLASSESOPTIONS[i].option);
				options += strlen(J9SHAREDCLASSESOPTIONS[i].option) + strlen(*methodSpecs) + 1;
				returnAction = J9SHAREDCLASSESOPTIONS[i].action;
			} else {
				SHRINIT_ERR_TRACE1(1, J9NLS_SHRC_SHRINIT_INVALID_METHODSPEC, J9SHAREDCLASSESOPTIONS[i].option);
				/* Also print out help info about method specification */
				SHRINIT_TRACE1_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_AOT_METHODS_OPERATION, J9SHAREDCLASSESOPTIONS[i].option);
				SHRINIT_TRACE1_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_METHOD_SPEC, J9SHAREDCLASSESOPTIONS[i].option);
				SHRINIT_TRACE2_NOTAG(1, J9NLS_SHRC_SHRINIT_INFOTEXT_METHOD_SPEC, J9SHAREDCLASSESOPTIONS[i].option, J9SHAREDCLASSESOPTIONS[i].option);
				return RESULT_PARSE_FAILED;
			}
			break;
		case RESULT_DO_BOOTCLASSESONLY:
			*runtimeFlags &= ~J9SHAREDCLASSESOPTIONS[i].flag;
			*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL;
			break;
 		default:
			return J9SHAREDCLASSESOPTIONS[i].action;
		}

		options += (strlen(J9SHAREDCLASSESOPTIONS[i].option)+1);
	}


#if defined(J9ZOS390)
	/* Persistent shared classes caches not currently available */
	/* on zOS due to level of memory mapped file support        */
	*runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE);
#endif

#if defined(AIXPPC)
	if ((*runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE) != 0) {
		I_32 coreMmapSet;
		/* check if the process attribute to include mmap regions in core file is set. If yes, skip rest of the checks */
		coreMmapSet = j9port_control(J9PORT_CTLDATA_AIX_PROC_ATTR, 0);
		if (1 == coreMmapSet) {
			/* Do not allow the shared cache to run in persistent mode unless either
			 * the environment variable CORE_MMAP=yes, or the noCoreMmap option is specified
			 * on the command line. If the persistent keyword was not specified on the
			 * command line, revert to nonpersistent when CORE_MMAP!=yes, by modifying
			 * the runtimeFlags.
			 */
			bool coreMmapYesSet = false;
			char infoString[EsMaxPath];
			UDATA lengthYesSetting = strlen(OPTION_YES);

			infoString[0] = '\0';
			if (j9sysinfo_get_env(OPTION_ENVVAR_COREMMAP, infoString, EsMaxPath) == 0) {
				if ((strlen(infoString) == lengthYesSetting) && (j9_cmdla_strnicmp(infoString, OPTION_YES, lengthYesSetting) == 0)) {
					coreMmapYesSet = true;
				}
			}

			if (!coreMmapYesSet) {
				if ((*runtimeFlags & J9SHR_RUNTIMEFLAG_PERSISTENT_KEYWORD) == 0) {
					/* If the persistent keyword isn't specified, run non-persistent when CORE_MMAP is not enabled */
					Trc_SHR_INIT_NoCoreMmapDisablePersistence();
					*runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE;
				} else if (lastAction != returnAction) {
					/* the persistent keyword was specified, print a warning or exit the vm */
					if (noCoreMmap) {
						SHRINIT_TRACE(*verboseFlags, J9NLS_SHRC_NO_COREMMAP_WARNING);
					} else {
						SHRINIT_TRACE(*verboseFlags, J9NLS_SHRC_COREMMAP_REQUIRED);
						/* Exit the JVM */
						return RESULT_PARSE_FAILED;
					}
				}
			}
		}
	}
#endif

	printRuntimeFlags(PORTLIB, *runtimeFlags, *verboseFlags);
	return returnAction;
}

static void
j9shr_dump_help(J9JavaVM* vm, UDATA more)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	const char* tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_SHRINIT_HELP_HEADER, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	IDATA i=0;
	while(J9SHAREDCLASSESHELPTEXT[i].option != NULL) {
		const char* helptext, *morehelptext;

		if (!(J9SHAREDCLASSESHELPTEXT[i].nlsHelp1 | J9SHAREDCLASSESHELPTEXT[i].nlsMoreHelp1)) {
			j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %s\n", J9SHAREDCLASSESHELPTEXT[i].option);
		} else {
			helptext = OMRPORT_FROM_J9PORT(PORTLIB)->nls_lookup_message(OMRPORT_FROM_J9PORT(PORTLIB), (J9NLS_INFO | J9NLS_DO_NOT_APPEND_NEWLINE | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),
					(J9SHAREDCLASSESHELPTEXT[i].nlsHelp1), (J9SHAREDCLASSESHELPTEXT[i].nlsHelp2), NULL);
			morehelptext = OMRPORT_FROM_J9PORT(PORTLIB)->nls_lookup_message(OMRPORT_FROM_J9PORT(PORTLIB), (J9NLS_INFO | J9NLS_DO_NOT_APPEND_NEWLINE | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),
					(J9SHAREDCLASSESHELPTEXT[i].nlsMoreHelp1), (J9SHAREDCLASSESHELPTEXT[i].nlsMoreHelp2), NULL);

			if (J9SHAREDCLASSESHELPTEXT[i].nlsHelp1) {
				if (strlen(J9SHAREDCLASSESHELPTEXT[i].option) > 27) {
				/* Some help text has more than 28 chars, print them in two lines, e.g.
				 * invalidateAotMethods=<method_specification>[,<method_specification>]
				 * Invalidate the AOT method(s) specified by the user.
				 */
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %s\n", J9SHAREDCLASSESHELPTEXT[i].option);
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %28s", "");
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, helptext);
				} else {
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %-28.28s", J9SHAREDCLASSESHELPTEXT[i].option);
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, helptext);
				}
				j9file_printf(PORTLIB, J9PORT_TTY_OUT, "\n");
			}
			if (more && (J9SHAREDCLASSESHELPTEXT[i].nlsMoreHelp1)) {
				if (strlen(J9SHAREDCLASSESHELPTEXT[i].option) > 27) {
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %s\n", J9SHAREDCLASSESHELPTEXT[i].option);
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %28s", "");
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, morehelptext);
				} else {
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, " %-28.28s", J9SHAREDCLASSESHELPTEXT[i].option);
					j9file_printf(PORTLIB, J9PORT_TTY_OUT, morehelptext);
				}
				j9file_printf(PORTLIB, J9PORT_TTY_OUT, "\n");
			}
		}
		i++;
	}

	/*Display other JVM arguments that are useful when using -Xshareclasses*/
	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_SHRINIT_HELP_OTHEROPTS_HEADER, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCMX_V1, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCDMX, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCMINAOT, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCMAXAOT, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCMINJITDATA, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XSCMAXJITDATA, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XZERONOSHAREZIP, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXSHARED_CACHE_HARD_LIMIT_EQUALS, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXSHARECLASSESENABLEBCI, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXSHARECLASSESDISABLEBCI, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXENABLESHAREANONYMOUSCLASSES, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXDISABLESHAREANONYMOUSCLASSES, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXENABLESHAREUNSAFECLASSES, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXDISABLESHAREUNSAFECLASSES, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);
	
	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXENABLEUSEGCSTARTUPHINTS, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXDISABLEUSEGCSTARTUPHINTS, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

#if defined(J9VM_ARCH_X86)
	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXPORTABLESHAREDCACHE, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);

	tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_EXELIB_INTERNAL_HELP_XXNOPORTABLESHAREDCACHE, NULL);
	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "%s", tmpcstr);
#endif /* defined(J9VM_ARCH_X86) */

	j9file_printf(PORTLIB, J9PORT_TTY_OUT, "\n\n");
}

/**
 * Notify the open/close state of jar/zip files so as to force a timestamp check.
 *
 * @param [in] vm The current J9JavaVM
 * @param [in] classPathEntries  A pointer to the J9ClassPathEntry
 * @param [in] entryCount  The count of entries on the classpath
 * @param [in] isOpen  A flag indicating the open state for jar/zip files
 */
void
j9shr_updateClasspathOpenState(J9JavaVM* vm, J9ClassPathEntry* classPathEntries, UDATA entryIndex, UDATA entryCount, BOOLEAN isOpen)
{
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
	UDATA newState = ((isOpen)? J9ZIP_STATE_OPEN : J9ZIP_STATE_CLOSED);
	UDATA i = 0;

	Trc_SHR_INIT_updateClasspathOpenState_entry(currentThread);

	for (i = entryIndex; i< entryCount; i++) {
		if (CPE_TYPE_JAR == classPathEntries[i].type) {
			((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->notifyClasspathEntryStateChange(currentThread, (const char*)classPathEntries[i].path, newState);
		}
	}
	Trc_SHR_INIT_updateClasspathOpenState_exit(currentThread);
}

/* classNameLength is the expected length of the utf8 string */
static void
fixUpString(J9InternalVMFunctions* functionTable, char* nameBuffer, UDATA bufferSize, const char* className, UDATA classNameLength)
{
	UDATA i;

	strncpy(nameBuffer, (char*)className, classNameLength);

	/* Slashify the className */
	for (i=0; i<classNameLength; i++) {
		if (nameBuffer[i] == '.') {
			nameBuffer[i] = '/';
		}
	}
	nameBuffer[classNameLength]='\0';
}

static void
killStringFarm(J9PortLibrary* portlib, J9SharedStringFarm* root)
{
	J9SharedStringFarm* walk = root;
	J9SharedStringFarm* next = NULL;

	PORT_ACCESS_FROM_PORT(portlib);

	do {
		next = walk->next;
		j9mem_free_memory(walk);
		walk = next;
	} while (walk);
}

static void
testForBytecodeModification(J9JavaVM* vm)
{
	if (J9_EVENT_IS_HOOKED(vm->hookInterface, J9HOOK_VM_CLASS_LOAD_HOOK) || J9_EVENT_IS_HOOKED(vm->hookInterface, J9HOOK_VM_CLASS_LOAD_HOOK2)) {
		vm->sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_BYTECODE_AGENT_RUNNING;
	}
}

/* THREADING: Should be protected by the class segment mutex */
void
registerStoreFilter(J9JavaVM* vm, J9ClassLoader* classloader, const char* fixedName, UDATA fixedNameSize, J9Pool** filterPoolPtr)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	
	Trc_SHR_Assert_ShouldHaveLocalMutex(vm->classMemorySegments->segmentMutex);

	if (*filterPoolPtr == NULL) {
		*filterPoolPtr = pool_new(sizeof(struct ClassNameFilterData), 0, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_CLASSES, POOL_FOR_PORT(vm->portLibrary));
	}
	/* If we fail to allocate the pool, just don't register the store filter - we're probably going down anyway */
	if (*filterPoolPtr) {
		struct ClassNameFilterData* anElement;
		struct ClassNameFilterData* theElement = NULL;
		pool_state aState;
		J9Pool* pool = *filterPoolPtr;

		/* Look for an existing entry for this classloader. If a classloader registered a store filter and a different classloader
		 * in the hierarchy loaded it, then the entry will still be in the pool and should be reused */
		anElement = (struct ClassNameFilterData*)pool_startDo(pool, &aState);
		while (anElement) {
			if (anElement->classloader == classloader) {
				theElement = anElement;
				if (theElement->classname != (char*)&(theElement->buffer)) {
					j9mem_free_memory(theElement->classname);
				}
				break;
			}
			anElement = (struct ClassNameFilterData*)pool_nextDo(&aState);
		}

		if ((theElement != NULL) || (theElement = (ClassNameFilterData*)pool_newElement(pool))) {
			theElement->classloader = classloader;
			/* By default, we allocate SHRINIT_NAMEBUF_SIZE space for the classname. If we need more, allocate this separately. */
			if ((fixedNameSize + 1) > SHRINIT_NAMEBUF_SIZE) {
				if (!(theElement->classname = (char*)j9mem_allocate_memory(fixedNameSize + 1, J9MEM_CATEGORY_CLASSES))) {
					pool_removeElement(pool, theElement);
					return;
				}
			} else {
				theElement->classname = (char*)&(theElement->buffer);
			}
			strncpy(theElement->classname, fixedName, fixedNameSize);
			theElement->classname[fixedNameSize] = '\0';
			theElement->classnameLen = fixedNameSize;
		}
	}
}

void
freeStoreFilterPool(J9JavaVM* vm, J9Pool* filterPool)
{
	struct ClassNameFilterData* anElement;
	pool_state aState;

	PORT_ACCESS_FROM_JAVAVM(vm);

	anElement = (struct ClassNameFilterData*)pool_startDo(filterPool, &aState);
	/* Free any separately allocated classnames, then free the pool */
	while (anElement) {
		if (anElement->classname != (char*)&(anElement->buffer)) {
			j9mem_free_memory(anElement->classname);
		}
		anElement = (struct ClassNameFilterData*)pool_nextDo(&aState);
	}
	pool_kill(filterPool);
}

/**
 * Find class in shared classes cache
 *
 * @param [in] hookInterface  Pointer to pointer to the hook interface structure
 * @param [in] eventNum  Not used
 * @param [in,out] voidData  Pointer to a J9VMFindLocalClassEvent struct
 * @param [in] userData  Not used
 */
void
hookFindSharedClass(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData) {
	J9VMFindLocalClassEvent* eventData = (J9VMFindLocalClassEvent*)voidData;
	J9VMThread* currentThread = eventData->currentThread;
	J9JavaVM* vm = currentThread->javaVM;
	char fnameBuf[SHRINIT_NAMEBUF_SIZE];
	char* fixedName = (char*)fnameBuf;
	UDATA fixedNameSize = SHRINIT_NAMEBUF_SIZE;
	void* cpExtraInfo = NULL;
	bool fixedNameSet = false;
	ClasspathItem* classpath = NULL;
	UDATA infoFound = 0;
	UDATA realClassNameLength = eventData->classNameLength;
	U_64 localRuntimeFlags;
	UDATA localVerboseFlags;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	bool isBootLoader = false;
	bool releaseSegmentMutex = false;
	omrthread_monitor_t classSegmentMutex = vm->classMemorySegments->segmentMutex;
	IDATA* entryIndex = eventData->foundAtIndex;

	/* default values for bootstrap: */
	IDATA helperID = 0;
	U_16 cpType = CP_TYPE_CLASSPATH;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_SHR_INIT_hookFindSharedClass_entry(currentThread);

	eventData->result = NULL;

	if (sharedClassConfig==NULL) {
		/* trace event is at level 1 and trace exit message is at level 2 as per CMVC 155318/157683  */
		Trc_SHR_INIT_hookFindSharedClass_ConfigNull(currentThread);
		Trc_SHR_INIT_hookFindSharedClass_exit_Noop(currentThread);
		return;
	}

	J9Module* module = eventData->j9module;
	if ((J2SE_VERSION(vm) >= J2SE_V11)
		&& (NULL == module)
	) {
		module = getModule(currentThread, (U_8*)eventData->className, realClassNameLength, eventData->classloader);
	}

	if (isClassFromPatchedModule(currentThread, module, (U_8*)eventData->className, realClassNameLength, eventData->classloader)) {
		Trc_SHR_INIT_hookFindSharedClass_exit_Noop(currentThread);
		return;
	}

	localRuntimeFlags = sharedClassConfig->runtimeFlags;
	localVerboseFlags = sharedClassConfig->verboseFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_BYTECODE_AGENT_RUNNING)) {
		testForBytecodeModification(vm);
	}

	if (J9_ARE_NO_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		J9_ARE_ANY_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS) ||
		(J9_ARE_ANY_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_BYTECODE_AGENT_RUNNING) &&
		(NULL == sharedClassConfig->modContext) &&
		J9_ARE_NO_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_BCI))
	) {
		/* trace event is at level 1 and trace exit message is at level 2 as per CMVC 155318/157683  */
		Trc_SHR_INIT_hookFindSharedClass_BadRunTimeFlags_Event(currentThread, localRuntimeFlags);
		Trc_SHR_INIT_hookFindSharedClass_exit_Noop(currentThread);
		return;
	}

	/* jcl calls from shared.c set the vmState, but bootstrap calls do not */
	if (*currentState != J9VMSTATE_SHAREDCLASS_FIND) {
		oldState = *currentState;
		*currentState = J9VMSTATE_SHAREDCLASS_FIND;
	}

	if (NULL != eventData->classPathEntries) {
		cpExtraInfo = eventData->classPathEntries->extraInfo;
		infoFound = translateExtraInfo(cpExtraInfo, &helperID, &cpType, &classpath);
	}

	/* Bootstrap loader does not provide meaningful extraInfo */
	if (!classpath && !infoFound) {
		UDATA pathEntryCount = eventData->entryCount;

		if (J2SE_VERSION(vm) >= J2SE_V11) {
			if (eventData->classloader == vm->systemClassLoader) {
				isBootLoader = true;
				pathEntryCount += 1;
				if (0 == omrthread_monitor_owned_by_self(classSegmentMutex)) {
					omrthread_monitor_enter(classSegmentMutex);
					releaseSegmentMutex = true;
				}
				classpath = getBootstrapClasspathItem(currentThread, vm->modulesPathEntry, pathEntryCount);
				if (releaseSegmentMutex) {
					omrthread_monitor_exit(classSegmentMutex);
					releaseSegmentMutex = false;
				}
			}
		} else {
			if (0 == omrthread_monitor_owned_by_self(classSegmentMutex)) {
				omrthread_monitor_enter(classSegmentMutex);
				releaseSegmentMutex = true;
			}
			classpath = getBootstrapClasspathItem(currentThread, eventData->classPathEntries, pathEntryCount);
			if (releaseSegmentMutex) {
				omrthread_monitor_exit(classSegmentMutex);
				releaseSegmentMutex = false;
			}
		}
	}

	if (!classpath) {
		/* No cached classpath found. Need to create a new one. */
		if ((NULL != eventData->classPathEntries) || (eventData->classloader == vm->systemClassLoader)) {
			if (0 == omrthread_monitor_owned_by_self(classSegmentMutex)) {
				omrthread_monitor_enter(classSegmentMutex);
				releaseSegmentMutex = true;
			}
			classpath = createClasspath(currentThread, eventData->classPathEntries, eventData->entryCount, helperID, cpType, infoFound);
			if (releaseSegmentMutex) {
				omrthread_monitor_exit(classSegmentMutex);
				releaseSegmentMutex = false;
			}
			if (classpath == NULL) {
				goto _done;
			}
		}
	}

	if (realClassNameLength > (SHRINIT_NAMEBUF_SIZE-1)) {
		fixedNameSize = realClassNameLength + 1;
		fixedName = (char*)j9mem_allocate_memory((fixedNameSize*sizeof(char)), J9MEM_CATEGORY_CLASSES);
		if (!fixedName) {
			SHRINIT_ERR_TRACE(localVerboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_STRBUF_FIND);
			goto _done;
		}
		fixedNameSet = true;
		Trc_SHR_INIT_hookFindSharedClass_allocateBuf(currentThread, fixedName);
	}

	fixUpString(vm->internalVMFunctions, fixedName, fixedNameSize, eventData->className, realClassNameLength);

	if (eventData->doPreventFind) {
		if (eventData->doPreventStore) {
			if (0 == omrthread_monitor_owned_by_self(classSegmentMutex)) {
				omrthread_monitor_enter(classSegmentMutex);
				releaseSegmentMutex = true;
			}
			registerStoreFilter(vm, eventData->classloader, fixedName, strlen(fixedName), &(sharedClassConfig->classnameFilterPool));
			if (releaseSegmentMutex) {
				omrthread_monitor_exit(classSegmentMutex);
				releaseSegmentMutex = false;
			}
		}
		goto _donePostFixedClassname;
	}

	eventData->result = (J9ROMClass*)((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->findROMClass(
					currentThread, fixedName, classpath, eventData->partition, sharedClassConfig->modContext, eventData->confirmedCount, entryIndex);

	if ((isBootLoader)
		&& (NULL != eventData->result)
		&& (NULL != entryIndex)
	) {
		/* As modulesPathEntry(Jimage) is put ahead of classpath entries, entryIndex has been increase by 1 (in j9shr_classStoreTransaction_start()) when stored in the cache.
		 * So when loaded from the shared cache, entryIndex needs to be decreased by 1 to recover the real classpath entryIndex here.
		 */
		*entryIndex = *entryIndex - 1;
		if ((-1 == *entryIndex)
			&& (NULL == module)
		) {
			/* entryIndex is -1 means class is from Jimage, do not return the class if its module is not resolved. */
			Trc_SHR_INIT_hookFindSharedClass_classFromUnresolvedModule(currentThread, fixedNameSize, fixedName);
			eventData->result = NULL;
			goto _donePostFixedClassname;
		}
	}

	if (eventData->doPreventStore && (NULL == eventData->result)) {
		if (0 == omrthread_monitor_owned_by_self(classSegmentMutex)) {
			omrthread_monitor_enter(classSegmentMutex);
			releaseSegmentMutex = true;
		}
		registerStoreFilter(vm, eventData->classloader, fixedName, strlen(fixedName), &(sharedClassConfig->classnameFilterPool));
		if (releaseSegmentMutex) {
			omrthread_monitor_exit(classSegmentMutex);
		}
	}

	if (localRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_TRACECOUNT) {
		UDATA oldNum, value, result;
		do {
			oldNum = sharedClassConfig->findClassCntr;
			value = oldNum + 1;
			result = VM_AtomicSupport::lockCompareExchange(&(sharedClassConfig->findClassCntr), oldNum, value);
		} while (result != oldNum);

		if (!(sharedClassConfig->findClassCntr % PERF_TRACE_EVERY_N_FINDS)) {
			J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

			Trc_SHR_INIT_PERFCNTR(currentThread, (sharedClassConfig->findClassCntr));
		}
	}

	if (localVerboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_IO) {
		/*
		 * if ROM class is successfully found, print found
		 * else print failed to find ROM class
		 */
		UDATA classpathItemType = classpath -> getType();
		UDATA vHelperID = classpath->getHelperID();

		if (classpathItemType ==CP_TYPE_CLASSPATH) {
			if (eventData->result) {
				SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FOUND_VERBOSE_MSG, fixedName, vHelperID);
			} else {
				SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FIND_FAILED_VERBOSE_MSG, fixedName, vHelperID);
			}
		} else {
			U_16 vPathLen;
			const char* vPath = classpath->itemAt(0)->getPath(&vPathLen);

			if (classpathItemType==CP_TYPE_URL) {
				if (eventData->result) {
					SHRINIT_TRACE4_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FOUND_VERBOSE_URL_MSG, fixedName, vHelperID, vPathLen, vPath);
				} else {
					SHRINIT_TRACE4_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FIND_FAILED_VERBOSE_URL_MSG, fixedName, vHelperID, vPathLen, vPath);
				}
			} else if (classpathItemType==CP_TYPE_TOKEN) {
				if (eventData->result) {
					SHRINIT_TRACE4_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FOUND_VERBOSE_TOKEN_MSG, fixedName, vHelperID, vPathLen, vPath);
				} else {
					SHRINIT_TRACE4_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FIND_FAILED_VERBOSE_TOKEN_MSG, fixedName, vHelperID, vPathLen, vPath);
				}
			}
		}
	}

_donePostFixedClassname:
	if (fixedNameSet) {
		j9mem_free_memory(fixedName);
		Trc_SHR_INIT_hookFindSharedClass_freeBuf(currentThread, fixedName);
	}
_done:
	if (oldState != (UDATA)-1) {
		*currentState = oldState;
	}

	/* no level 1 trace event here due to performance problem stated in CMVC 155318/157683 */
	Trc_SHR_INIT_hookFindSharedClass_exit(currentThread);
}

/**
 * Store compiled method in shared classes cache
 *
 * @param[in] currentThread  The current VM thread
 * @param[in] romMethod  A pointer to the J9ROMMethod that the code belongs to
 * @param[in] dataStart  A pointer to the start of the compiled data
 * @param[in] dataSize  The size of the compiled data in bytes
 * @param[in] codeStart  A pointer to the start of the compiled code
 * @param[in] codeSize  The size of the compiled code in bytes
 * @param[in] codeSize  The size of the compiled code in bytes
 * @param[in] forceReplace If non-zero, forces the compiled method to be stored,
 * regardless of whether it already exists or not. If it does exist, the existing
 * cached method is marked stale.
 *
 * @return Pointer to the shared data if it was successfully stored
 * @return J9SHR_RESOURCE_STORE_EXISTS if the method already exists in the cache
 * (will never return this if forceReplace is non-null)
 * @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the method
 * @return J9SHR_RESOURCE_STORE_FULL if the cache is full
 * @return NULL otherwise
 */
const U_8*
j9shr_storeCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, const U_8* dataStart, UDATA dataSize, const U_8* codeStart, UDATA codeSize, UDATA forceReplace)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	const U_8* returnVal = 0;

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_SHR_INIT_storeCompiledMethod_entry(currentThread);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_storeCompiledMethod_exit_Noop(currentThread);
		return NULL;
	}

	SH_CacheMap* cm = (SH_CacheMap*)(sharedClassConfig->sharedClassCache);
	cm->updateRuntimeFullFlags(currentThread);

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;
	UDATA localVerboseFlags = sharedClassConfig->verboseFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES)) {
		Trc_SHR_INIT_storeCompiledMethod_exit_Noop(currentThread);
		return NULL;
	}

	if (0 != (localRuntimeFlags & J9SHR_RUNTIMEFLAG_AOT_SPACE_FULL)) {
		return (U_8*)J9SHR_RESOURCE_STORE_FULL;
	}

	if (*currentState != J9VMSTATE_SHAREDAOT_STORE) {
		oldState = *currentState;
		*currentState = J9VMSTATE_SHAREDAOT_STORE;
	}

	returnVal = (U_8*)(cm->storeCompiledMethod(currentThread, romMethod, dataStart, dataSize, codeStart, codeSize, forceReplace));

	if (localVerboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_AOT) {
		if (returnVal) {
			SHRINIT_TRACE1_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_STORED_VERBOSE_AOT_MSG, romMethod);
		} else {
			SHRINIT_TRACE1_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_STORE_FAILED_VERBOSE_AOT_MSG, romMethod);
		}
	}

	if (oldState != (UDATA)-1) {
		*currentState = oldState;
	}

	Trc_SHR_INIT_storeCompiledMethod_exit(currentThread, returnVal);

	return returnVal;
}

/**
 * Polls the cache for data to write to a javacore file
 *
 * @param[in] vmThread  The current thread
 * @param[in] descriptor  The struct to fill with the javacore data
 */
UDATA
j9shr_getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor)
{
	SH_CacheMap* cm = (SH_CacheMap*)vm->sharedClassConfig->sharedClassCache;
	if (NULL != cm) {
		return cm->getJavacoreData(vm, descriptor);
	}
	return 0;
}

/**
 * Peeks to see whether compiled code exists for a given ROMMethod in the CompiledMethodManager hashtable
 *
 * @param[in] vmThread  The current thread
 * @param[in] romMethod  The ROMMethod to test
 */
UDATA
j9shr_existsCachedCodeForROMMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod)
{
	return (((SH_CacheMap*)(currentThread->javaVM->sharedClassConfig->sharedClassCache))->existsCachedCodeForROMMethodInline(currentThread, romMethod));
}

/**
 * Searches for data in the cache which has been stored against "key" which is a UTF8 string.
 * Populates descriptorPool with the J9SharedDataDescriptors found. Returns the number of elements found.
 * The data returned can optionally include private data of other JVMs or data of different types stored under the same key.
 *
 * Each J9SharedDataDescriptor returned will have the following data in each field:
 *   address  The address of the cached data record
 *   length  The length of the cached data record
 *   type  The type of the cached data record, which will be one of the J9SHR_DATA_TYPE_X constants defined in j9.h
 *   flags  Will have any one of the following set:
 *     J9SHRDATA_IS_PRIVATE indicates that the memory is private
 *     J9SHRDATA_PRIVATE_TO_DIFFERENT_JVM indicates that the data is not private to this JVM, but does not mean that the JVM
 *       is still alive. You can attempt to acquire this private data by calling acquirePrivateSharedData.
 *
 * @param[in] vmThread  The current thread
 * @param[in] key  The UTF8 key against which the data was stored
 * @param[in] keylen  The length of the key
 * @param[in] limitDataType  Optional. If used, only data of the type constant specified is returned.
 *								If 0, all data stored under that key is returned
 * @param[in] includePrivate  If non-zero, will also add private data of other JVMs stored under that key into the pool
 * @param[out] firstItem If non-NULL, is filled in with the first data item found
 * @param[out] descriptorPool  Must be a J9Pool of size J9SharedDataDescriptor which will be populated with the results.
 * 								The pool can be NULL if only the existence of data needs to be determined
 *
 * @return  The number of data elements found or -1 in the case of error
 */
IDATA
j9shr_findSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, UDATA limitDataType, UDATA includePrivateData, J9SharedDataDescriptor* firstItem, const J9Pool* descriptorPool)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	IDATA returnVal = -1;

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_SHR_INIT_findSharedData_entry(currentThread, keylen, key);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_findSharedData_exit_Noop(currentThread);
		return -1;
	}

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;
	UDATA localVerboseFlags = sharedClassConfig->verboseFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS)) {
		Trc_SHR_INIT_findSharedData_exit_Noop(currentThread);
		return -1;
	}

	/* jcl calls from shared.c set the vmState, but bootstrap calls do not */
	if (*currentState != J9VMSTATE_SHAREDDATA_FIND) {
		oldState = *currentState;
		*currentState = J9VMSTATE_SHAREDDATA_FIND;
	}

	returnVal = (((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->findSharedData(currentThread, key, keylen, limitDataType, includePrivateData, firstItem, descriptorPool));

	if (localVerboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DATA) {
		if (returnVal) {
			SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FOUND_VERBOSE_DATA_MSG, keylen, key);
		} else {
			SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FIND_FAILED_VERBOSE_DATA_MSG, keylen, key);
		}
	}

	if (oldState != (UDATA)-1) {
		*currentState = oldState;
	}

	Trc_SHR_INIT_findSharedData_exit(currentThread, returnVal);

	return returnVal;
}

/**
 * Stores data in the cache against "key" which is a UTF8 string.
 * If data of a different dataType uses the same key, this is added without affecting the other data stored under that key.
 * If data of the same dataType already exists for that key, the original data is marked "stale" and the new data is added.
 * If the exact same data already exists in the cache under the same key and dataType,
 *	the data is not duplicated and the cached version is returned.
 * If null is passed as the data argument, all existing data against that key is marked "stale" (except for private data owned by other JVMs)
 * Note that each JVM can create a unique private data record in the cache using the same key
 * The function returns null if the cache is full, otherwise it returns a pointer to the shared location of the data.
 *
 * J9SharedDataDescriptor describing the data should have the following fields completed:
 *   address  The address of the local data to copy. This is ignored if J9SHRDATA_ALLOCATE_ZEROD_MEMORY flag is used.
 *   length  The length in bytes of the data to copy (or allocate in the case of J9SHRDATA_ALLOCATE_ZEROD_MEMORY)
 *   type  The data type. This should be one of the J9SHR_DATA_TYPE_X constants defined in j9.h
 *   flags  Can optionally set any of the following:
 *     Use J9SHRDATA_IS_PRIVATE to allocate private memory
 *     Use J9SHRDATA_ALLOCATE_ZEROD_MEMORY to allocate zero'd memory (data->address is ignored)
 *     Use J9SHRDATA_NOT_INDEXED to indicate that the data does not need to be indexed (will not be returned by findSharedData)
 *
 * @param[in] vmThread  The current thread
 * @param[in] key  The UTF8 key to store the data against
 * @param[in] keylen  The length of the key
 * @param[in] data  The actual data
 *
 * @return  The new location of the cached data or null
 */
const U_8*
j9shr_storeSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, const J9SharedDataDescriptor* data)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	const U_8* returnVal = NULL;

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_SHR_INIT_storeSharedData_entry(currentThread, keylen, key);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_storeSharedData_exit_Noop(currentThread);
		return NULL;
	}

	SH_CacheMap* cm = (SH_CacheMap*)(sharedClassConfig->sharedClassCache);
	cm->updateRuntimeFullFlags(currentThread);

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;
	UDATA localVerboseFlags = sharedClassConfig->verboseFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(J9_ARE_ANY_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES))
	) {
		Trc_SHR_INIT_storeSharedData_exit_Noop(currentThread);
		return NULL;
	}

	if (J9_ARE_ANY_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_AVAILABLE_SPACE_FULL)) {
		if (J9_ARE_ALL_BITS_SET(data->flags, J9SHRDATA_USE_READWRITE)) {
			/* softmx has no effect on the read write area */
			if (J9_ARE_NO_BITS_SET(data->flags, J9SHRDATA_NOT_INDEXED | J9SHRDATA_IS_PRIVATE)) {
				cm->increaseUnstoredBytes(sizeof(ByteDataWrapper));
			}
		} else {
			U_32 wrapperLength = (J9_ARE_ALL_BITS_SET(data->flags, J9SHRDATA_NOT_INDEXED) ? 0 : sizeof(ByteDataWrapper));

			cm->increaseUnstoredBytes((U_32)data->length + wrapperLength);
		}
		Trc_SHR_INIT_storeSharedData_exit1(currentThread);
		return NULL;
	}

	/* jcl calls from shared.c set the vmState, but bootstrap calls do not */
	if (*currentState != J9VMSTATE_SHAREDDATA_STORE) {
		oldState = *currentState;
		*currentState = J9VMSTATE_SHAREDDATA_STORE;
	}

	returnVal = cm->storeSharedData(currentThread, key, keylen, data);

	if (localVerboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DATA) {
		if (returnVal) {
			SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_STORED_VERBOSE_DATA_MSG, keylen, key);
		} else {
			SHRINIT_TRACE2_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_STORE_FAILED_VERBOSE_DATA_MSG, keylen, key);
		}
	}

	if (oldState != (UDATA)-1) {
		*currentState = oldState;
	}

	Trc_SHR_INIT_storeSharedData_exit(currentThread, returnVal);

	return returnVal;
}

/**
* Store data in shared classes cache, keyed by the specified address in the shared cache.
* Typically this is jit or aot related data.
*
* @param[in] currentThread  The current VM thread
* @param[in] addressInCache  A pointer to the method or class in the cache used to key the data
* @param[in] data The data to store
* @param[in] forceReplace If non-zero, forces the attached data to be
* stored, regardless of whether it already exists or not. If it does exist,
* the existing cached data is marked stale.
*
* The J9SharedDataDescriptor->flags currently has no defined values and must
* be set to J9SHR_ATTACHED_DATA_NO_FLAGS.
*
* @return 0 if shared data was successfully stored
* @return J9SHR_RESOURCE_STORE_EXISTS if the attached data already exists
* in the cache (will never return this if forceReplace is non-zero)
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the data, or
*         This will occur if the addressInCache is not a valid cache address
*         This will occur if cache updates are denied, either the cache is
*		  	corrupt or there is a vm exit in progress
*         This will occur if the cache is read-only
* @return J9SHR_RESOURCE_STORE_FULL if the cache is full
* @return J9SHR_RESOURCE_PARAMETER_ERROR if shared class configuration is missing or
* 		  if parameter error such as invalid J9SharedDataDescriptor->type or J9SharedDataDescriptor->flags
*/
UDATA
j9shr_storeAttachedData(J9VMThread* currentThread, const void* addressInCache, const J9SharedDataDescriptor* data, UDATA forceReplace)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	UDATA returnVal = 0;

	Trc_SHR_INIT_storeAttachedData_entry(currentThread);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_storeAttachedData_exit_SccN(currentThread);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	SH_CacheMap* cm = (SH_CacheMap*)(sharedClassConfig->sharedClassCache);
	cm->updateRuntimeFullFlags(currentThread);

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY)
	){
		Trc_SHR_INIT_storeAttachedData_exit_localRTF(currentThread, localRuntimeFlags);
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	if (localRuntimeFlags & J9SHR_RUNTIMEFLAG_JIT_SPACE_FULL) {
		Trc_SHR_INIT_storeAttachedData_exit_CacheFull(currentThread);
		return J9SHR_RESOURCE_STORE_FULL;
	}

	if ((J9SHR_ATTACHED_DATA_TYPE_JITPROFILE != data->type)
		&& (J9SHR_ATTACHED_DATA_TYPE_JITHINT != data->type)) {
		Trc_SHR_INIT_storeAttachedData_exit_TypeUnknown(currentThread, data->type);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	if (J9SHR_ATTACHED_DATA_NO_FLAGS != data->flags) {
		Trc_SHR_INIT_storeAttachedData_exit_FlagErr(currentThread, data->flags);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	if (*currentState != J9VMSTATE_ATTACHEDDATA_STORE) {
		oldState = *currentState;
		*currentState = J9VMSTATE_ATTACHEDDATA_STORE;
	}

	returnVal = cm->storeAttachedData(currentThread, addressInCache, data, forceReplace);

	*currentState = oldState;

	Trc_SHR_INIT_storeAttachedData_exit1(currentThread, returnVal);

	return returnVal;
}

/**
* Find the data for the given shared cache address. If *corruptOffset is not -1,
* a vm exit occurred while a call to j9shr_updateAttachedData() was in progress.
* In such case, data->address is still filled with the corrupt data.
* Corrupt data can be overwritten by a subsequent update.
* If data->address was NULL on entry, then on return caller must free a non-NULL data->address.
*
* @param[in] currentThread  The current VM thread
* @param[in] addressInCache  A pointer to the method or class in the cache used to key the data
* @param[in/out] data The type of data to find must be specified,
* 				if data->address is NULL then data->length is ignored. On successful return data->address will point to allocated memory filled with the data,
* 				if data->address is non NULL then data->length is equal to size of the buffer pointed by data->address. On successful return, the buffer is filled with the data.
* @param[out] corruptOffset If non -1, the updateAtOffset supplied to j9shr_updateAttachedData() when the incomplete write occurred.
*
* The J9SharedDataDescriptor->flags currently has no defined values and must
* be set to J9SHR_ATTACHED_DATA_NO_FLAGS.
*
* @return A pointer to the start of the data
* @return J9SHR_RESOURCE_PARAMETER_ERROR if shared class configuration is missing or
* 		  if parameter error such as invalid J9SharedDataDescriptor->flags
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the data, or
*		  This will occur if data->address is non NULL and data->length, indicating the size of buffer pointed
*		  by data->address, is not enough to contain the data found.
* @return J9SHR_RESOURCE_BUFFER_ALLOC_FAILED if data->address is NULL and failed to allocate memory for data buffer
* @return J9SHR_RESOURCE_TOO_MANY_UPDATES if not able to read consistent data when running read-only
* @return NULL if non-stale data cannot be found, or the data is corrupt or cache access is denied.
* 		  In case data is corrupt, data->address should be freed by the caller if it was passed as NULL.
*/
const U_8*
j9shr_findAttachedData(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	const U_8* returnVal = 0;

	Trc_SHR_INIT_findAttachedData_entry(currentThread);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_findAttachedData_exit_SccN(currentThread);
		return (U_8*)J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS)
	){
		Trc_SHR_INIT_findAttachedData_exit_localRTF(currentThread, localRuntimeFlags);
		return NULL;
	}

	if (J9SHR_ATTACHED_DATA_NO_FLAGS != data->flags) {
		Trc_SHR_INIT_findAttachedData_FlagErr(currentThread, data->flags);
		Trc_SHR_INIT_findAttachedData_exit_FlagErr(currentThread, data->flags);
		return (U_8*)J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	if (*currentState != J9VMSTATE_ATTACHEDDATA_FIND) {
		oldState = *currentState;
		*currentState = J9VMSTATE_ATTACHEDDATA_FIND;
	}

	returnVal = (U_8*)(((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->findAttachedDataAPI(currentThread, addressInCache, data, corruptOffset));

	*currentState = oldState;

	Trc_SHR_INIT_findAttachedData_exit(currentThread, returnVal);
	return returnVal;
}

/**
* Update the existing data at updateAtOffset with the given new data.
*
* This will lock the cache to do the update so a read cannot occur at the
* same time. If a vm exit occurs while the data is being updated, the
* data is marked corrupt.
*
* @param[in] currentThread  The current VM thread
* @param[in] addressInCache  A pointer to the method or class in the cache used to key the data
* @param[in] updateAtOffset The offset into the data to make the update
* @param[in] updateData The type of data to update must be specified, as well as the update data and length.
*
* The J9SharedDataDescriptor->flags currently has no defined values and must
* be set to J9SHR_ATTACHED_DATA_NO_FLAGS.
*
* @return 0 on success
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the data
*       This will occur if the addressInCache is not a valid record
*       This will occur if cache updates are denied, either the cache is
* 			corrupt or there is a vm exit in progress
*       This will occur if the cache is read-only
*		This will occur if updateAtOffset is out of range, or updateAtOffset + dataSize is
* 		bigger than the record size.
* @return J9SHR_RESOURCE_PARAMETER_ERROR if problem with parameters or shared class configuration is missing.
*/
UDATA
j9shr_updateAttachedData(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, const J9SharedDataDescriptor* data)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	UDATA returnVal = 0;

	Trc_SHR_INIT_updateAttachedData_entry(currentThread);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_updateAttachedData_exit_SccN(currentThread);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY)
	){
		Trc_SHR_INIT_updateAttachedData_exit_localRTF(currentThread, localRuntimeFlags);
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	if (J9SHR_ATTACHED_DATA_NO_FLAGS != data->flags) {
		Trc_SHR_INIT_updateAttachedData_exit_FlagErr(currentThread, data->flags);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	if (*currentState != J9VMSTATE_ATTACHEDDATA_UPDATE) {
		oldState = *currentState;
		*currentState = J9VMSTATE_ATTACHEDDATA_UPDATE;
	}

	returnVal = ((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->updateAttachedData(currentThread, addressInCache,  updateAtOffset, data);

	*currentState = oldState;

	Trc_SHR_INIT_updateAttachedData_exit(currentThread, returnVal);
	return returnVal;
}

/**
* Update the existing data at the given offset to the given UDATA value.
*
* This will lock the cache to do the update. This is required because the
* data is memory protected, and a cache lock is required to unprotect
* the cache memory safely wrt other processes.
*
* @param[in] currentThread  The current VM thread
* @param[in] addressInCache  A pointer to the method or class in the cache used to key the data
* @param[in] type The type of the data
* @param[in] updateAtOffset The offset into the data to make the update
* @param[in] value The update data
*
* @return 0 on success
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the data
*       This will occur if the addressInCache is not a valid record
*       This will occur if cache updates are denied, either the cache is
*			corrupt or there is a vm exit in progress
*       This will occur if the cache is read-only
*       This will occur if updateAtOffset is out of range
*       This will occur if updateAtOffset is not UDATA aligned
* @return J9SHR_RESOURCE_PARAMETER_ERROR shared class configuration is missing.
*/
UDATA
j9shr_updateAttachedUDATA(J9VMThread* currentThread, const void* addressInCache, UDATA type, I_32 updateAtOffset, UDATA value)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	UDATA returnVal = 0;

	Trc_SHR_INIT_updateAttachedUDATA_entry(currentThread);

	if (sharedClassConfig == NULL) {
		Trc_SHR_INIT_updateAttachedUDATA_exit_SccN(currentThread);
		return J9SHR_RESOURCE_PARAMETER_ERROR;
	}

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;

	if (!(localRuntimeFlags & J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES) ||
		(localRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY)
	){
		Trc_SHR_INIT_updateAttachedUDATA_exit_localRTF(currentThread, localRuntimeFlags);
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	if (updateAtOffset % sizeof(UDATA)) {
		Trc_SHR_INIT_updateAttachedUDATA_exit_notAligned(currentThread, updateAtOffset, sizeof(UDATA));
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	if (*currentState != J9VMSTATE_ATTACHEDDATA_UPDATE) {
		oldState = *currentState;
		*currentState = J9VMSTATE_ATTACHEDDATA_UPDATE;
	}

	returnVal = ((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->updateAttachedUDATA(currentThread, addressInCache, type, updateAtOffset, value);
	*currentState = oldState;

	Trc_SHR_INIT_updateAttachedUDATA_exit(currentThread, returnVal);
	return returnVal;
}

/**
 * Free the memory in the J9SharedDataDescriptor->address field, and
 * set it to NULL.
 *
 * @param[in] currentThread  The current VM thread
 * @param[in] data The J9SharedDataDescriptor to free.
 */
void
j9shr_freeAttachedDataDescriptor(J9VMThread* currentThread, J9SharedDataDescriptor* data)
{
	J9JavaVM* vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (NULL != data->address) {
		j9mem_free_memory(data->address);
		data->address = NULL;
	}
}

/**
 * Attempts to acquire a private data entry which may or may not be in use by another JVM
 * The data descriptor passed to this function must have been returned by findSharedData with the
 * J9SHRDATA_PRIVATE_TO_DIFFERENT_JVM flag set. If the function succeeds, the data record is now
 * private to the caller JVM and cannot be acquired by another JVM. If the function does not succeed,
 * this means that the data area is still in use.
 *
 * @param[in] currentThread  The current thread
 * @param[in] data  A data descriptor that was obtained from calling findSharedData
 *
 * @return 1 if the data was successfully acquired or 0 otherwise
 */
static UDATA
j9shr_acquirePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data)
{
	if (currentThread->javaVM->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES) {
		return 0;
	}
	return ((SH_CacheMap*)(currentThread->javaVM->sharedClassConfig->sharedClassCache))->acquirePrivateSharedData(currentThread, data);
}

/**
 * If a JVM has finished using a piece of private data and wants to allow another JVM to acquire it, the data entry must be released.
 * This is done automatically when a JVM shuts down, but can also be achieved explicitly using this function.
 * The data descriptor passed to this function must have been returned by findSharedData with the
 * J9SHRDATA_IS_PRIVATE flag set and the J9SHRDATA_PRIVATE_TO_DIFFERENT_JVM flag not set.
 * A JVM can only release data entries which it is currently has "in use"
 * If the function succeeds, the data is released and can be acquired by any JVM using acquirePrivateSharedData
 *
 * @param[in] currentThread  The current thread
 * @param[in] data  A data descriptor that was obtained from calling findSharedData
 *
 * @return 1 if the data was successfully released or 0 otherwise
 */
static UDATA
j9shr_releasePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data)
{
	if (currentThread->javaVM->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES) {
		return 0;
	}
	return ((SH_CacheMap*)(currentThread->javaVM->sharedClassConfig->sharedClassCache))->releasePrivateSharedData(currentThread, data);
}


static void
resetSharedTable(J9SharedInvariantInternTable* sharedTable)
{
	*(sharedTable->sharedTailNodePtr) = 0;
	*(sharedTable->sharedHeadNodePtr) = 0;
	*(sharedTable->totalSharedNodesPtr) = 0;
	*(sharedTable->totalSharedWeightPtr) = 0;
	sharedTable->headNode = NULL;
	sharedTable->tailNode = NULL;
}

void
j9shr_resetSharedStringTable(J9JavaVM* vm)
{
	J9SharedInvariantInternTable* table = vm->sharedInvariantInternTable;

	UDATA verboseIntern = (vm->sharedClassConfig->verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_INTERN);

	PORT_ACCESS_FROM_JAVAVM(vm);

	if (table) {

		SHR_ENTER_TABLE_VERIFICATION(table);
		SH_CacheMap* cacheMap = (SH_CacheMap*)(vm->sharedClassConfig->sharedClassCache);
		if (verboseIntern) {
			j9tty_printf(PORTLIB, "Resetting shared string table...\n");
		}
    	resetSharedTable(table);
    	srpHashTableReset(
    						vm->portLibrary,
    						J9_GET_CALLSITE(),
    						table->sharedInvariantSRPHashtable,
    						cacheMap->getStringTableBase(),
    						(U_32) cacheMap->getStringTableBytes(),
    						(U_32) sizeof(J9SharedInternSRPHashTableEntry),
    						0,
    						sharedInternHashFn,
    						sharedInternHashEqualFn,
    						NULL,
    						vm);


    	SHR_EXIT_TABLE_VERIFICATION(table);
	}

}

static void
reportUtilityNotApplicable(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, UDATA verboseFlags, U_64 runtimeFlags, UDATA command)
{
	J9PortShcVersion versionData;
	PORT_ACCESS_FROM_JAVAVM(vm);
	char cacheDirName[J9SH_MAXPATH];
	const char *optionName;
	UDATA groupPerm = 0;
	I_8 layer = 0;

	if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS) != 0) {
		groupPerm = 1;
	}

	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE) != 0) {
		versionData.cacheType = J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
	} else {
		versionData.cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
	}

	if (RESULT_DO_PRINTSTATS == command) {
		optionName = ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS) != 0) ? OPTION_PRINTDETAILS : OPTION_PRINTSTATS;
	} else if (RESULT_DO_PRINT_TOP_LAYER_STATS == command) {
		optionName = OPTION_PRINT_TOP_LAYER_STATS;
	} else {
		optionName = OPTION_PRINTALLSTATS;
	}

	IDATA reportedIncompatibleNum = j9shr_report_utility_incompatible(vm, ctrlDirName, groupPerm, verboseFlags, cacheName, optionName);

	if (SH_OSCache::getCacheDir(vm, ctrlDirName, cacheDirName, J9SH_MAXPATH, versionData.cacheType) == -1) {
		return;
	}

	if ((NULL != vm->sharedClassConfig) && (vm->sharedClassConfig->layer > 0)) {
		layer = vm->sharedClassConfig->layer;
	}

	if ((reportedIncompatibleNum == 0) && (j9shr_stat_cache(vm, cacheDirName, 0, cacheName, &versionData, OSCACHE_CURRENT_CACHE_GEN, layer))) {
		if (versionData.cacheType == J9PORT_SHR_CACHE_TYPE_PERSISTENT) {
			SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_OTHER_PERS_TYPE_CACHE_EXISTS, optionName, cacheName);
		} else if (versionData.cacheType == J9PORT_SHR_CACHE_TYPE_NONPERSISTENT) {
			SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_OTHER_NONPERS_TYPE_CACHE_EXISTS, optionName, cacheName);
		}
	}
}

static void j9shr_printStats_dump_help(J9JavaVM* vm, bool moreHelp, UDATA command)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	
	const char* option = OPTION_PRINTSTATS_EQUALS;
	if (RESULT_DO_PRINTALLSTATS_EQUALS == command) {
		option = OPTION_PRINTALLSTATS_EQUALS;
	} else if (RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS == command) {
		option = OPTION_PRINT_TOP_LAYER_STATS_EQUALS;
	} 

	SHRINIT_TRACE2_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_HELP_V1, option, option);

	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_ALL);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_CLASSPATH);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_URL);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_TOKEN);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_ROMCLASS);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_ROMMETHOD);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_AOT);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_INVALIDATEDAOT);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_JITPROFILE);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_JITHINT);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_ZIPCACHE);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_STALE);
	SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_STARTUPHINT);
	j9tty_printf(PORTLIB, "\n");
	if (moreHelp) {
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_EXTRA);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_ORPHAN);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_AOTCH);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_AOTTHUNK);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_AOTDATA);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_JCL);
		SHRINIT_TRACE_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_PRINTSTATS_BYTEDATA);
	}
}


U_32
getCacheTypeFromRuntimeFlags(U_64 runtimeFlags)
{
        if (0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE)) {
                return J9PORT_SHR_CACHE_TYPE_PERSISTENT;
        } else {
                return J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
        }
}


static IDATA
performSharedClassesCommandLineAction(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, const char* cacheName, UDATA verboseFlags, U_64 runtimeFlags, char* expireTimeStr, 
		UDATA command, UDATA printStatsOptions) {
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9PortShcVersion versionData;
	U_32 cacheType = getCacheTypeFromRuntimeFlags(runtimeFlags);
	UDATA groupPerm = 0;
	I_8 layer = 0;

	if ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS) != 0) {
		groupPerm = 1;
	}
	char cacheDirName[J9SH_MAXPATH];

	switch(command)
	{
	case RESULT_DO_HELP:
	case RESULT_DO_MORE_HELP:
		j9shr_dump_help(vm, (command==RESULT_DO_MORE_HELP));
		break;
	case RESULT_DO_DESTROY:
	case RESULT_DO_DESTROYALLLAYERS:
	case RESULT_DO_RESET:
		{
			I_8 layerEnd = J9SH_DESTROY_TOP_LAYER_ONLY;
			if (RESULT_DO_DESTROYALLLAYERS == command) {
				layerEnd = J9SH_LAYER_NUM_MAX_VALUE;
			}
			setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
			versionData.cacheType = cacheType;
			j9shr_destroy_cache(vm, sharedClassConfig->ctrlDirName, verboseFlags, cacheName, OSCACHE_LOWEST_ACTIVE_GEN, OSCACHE_CURRENT_CACHE_GEN, &versionData, (RESULT_DO_RESET == command), -1, layerEnd);
			if (command == RESULT_DO_RESET) {
				/* Return with a return code intended for non-utility options */
				return J9VMDLLMAIN_NON_UTILITY_OK;
			}
			break;
		}
	case RESULT_DO_DESTROYALL:
		j9shr_destroy_all_cache(vm, sharedClassConfig->ctrlDirName, groupPerm, verboseFlags);
		break;
#if !defined(WIN32)
	case RESULT_DO_DESTROYSNAPSHOT:
		setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
		versionData.cacheType = J9PORT_SHR_CACHE_TYPE_SNAPSHOT;
		j9shr_destroy_snapshot(vm, sharedClassConfig->ctrlDirName, verboseFlags, cacheName, OSCACHE_LOWEST_ACTIVE_GEN, OSCACHE_CURRENT_CACHE_GEN, &versionData, -1, J9SH_DESTROY_TOP_LAYER_ONLY);
		break;
	case RESULT_DO_DESTROYALLSNAPSHOTS:
		j9shr_destroy_all_snapshot(vm, sharedClassConfig->ctrlDirName, groupPerm, verboseFlags);
		break;
	case RESULT_DO_RESTORE_FROM_SNAPSHOT:
	case RESULT_DO_SNAPSHOTCACHE:
		if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE)) {
			/* missing command-line option "nonpersistent" */
			const char *optionName = (RESULT_DO_RESTORE_FROM_SNAPSHOT == command) ? OPTION_RESTORE_FROM_SNAPSHOT: OPTION_SNAPSHOTCACHE;
			SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_OTHER_NONPERS_TYPE_CACHE_EXISTS, optionName, cacheName);
			break;
		}
		if (RESULT_DO_SNAPSHOTCACHE == command) {
			if (1 != checkIfCacheExists(vm, sharedClassConfig->ctrlDirName, cacheDirName, cacheName, &versionData, cacheType, layer)) {
				SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_SNAPSHOT, cacheName);
			} else {
				return J9VMDLLMAIN_OK;
			}
		} else {
			return J9VMDLLMAIN_OK;
		}
		break;
#endif /* !defined(WIN32) */
	case RESULT_DO_LISTALLCACHES:
		j9shr_list_caches(vm, sharedClassConfig->ctrlDirName, groupPerm, verboseFlags);
		break;
	case RESULT_DO_EXPIRE:
		/**
		 * Stop to expire cache for the option "expire=" if the runtime flag for the Tenant mode is set,
		 * and return with a return code intended for non-utility options.
		 */
		do {
			UDATA expireTimeVal = 0;
			char* scanstart = expireTimeStr;

			if (!scan_udata(&scanstart, &expireTimeVal)) {
				if(expireTimeVal < ((U_32) -1)) {
					j9shr_destroy_expire_cache(vm, sharedClassConfig->ctrlDirName, groupPerm, verboseFlags, expireTimeVal);
					return 0;
				}
			}

			SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_EXPIRE_WRONG_PARAM, cacheName);
			j9shr_dump_help(vm, 0);
		} while(false);
		break;
	case RESULT_DO_PRINTSTATS:
	case RESULT_DO_PRINTALLSTATS:
	case RESULT_DO_PRINTORPHANSTATS:
	case RESULT_DO_PRINT_TOP_LAYER_STATS:
		{
			/* Test for existence of cache first. If it exists, proceed with cache init */
			IDATA cacheExists = checkIfCacheExists(vm, sharedClassConfig->ctrlDirName, cacheDirName, cacheName, &versionData, cacheType, layer);

			if (0 == cacheExists) {
				if (verboseFlags != 0) {
					reportUtilityNotApplicable(vm, sharedClassConfig->ctrlDirName, cacheName, verboseFlags, runtimeFlags, command);
				}
			} else if (1 == cacheExists) {
				return J9VMDLLMAIN_OK;
			}
		}
		break;
	case RESULT_DO_PRINTALLSTATS_EQUALS:
	case RESULT_DO_PRINTSTATS_EQUALS:
	case RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS:
		{
			IDATA cacheExists = 0;

			if (printStatsOptions & PRINTSTATS_SHOW_MOREHELP) {
				j9shr_printStats_dump_help(vm, true, command);
				break;
			} else if (printStatsOptions & PRINTSTATS_SHOW_HELP) {
				j9shr_printStats_dump_help(vm, false, command);
				break;
			}
			/* Test for existence of cache first. If it exists, proceed with cache init */
			cacheExists = checkIfCacheExists(vm, sharedClassConfig->ctrlDirName, cacheDirName, cacheName, &versionData, cacheType, layer);
			if (0 == cacheExists) {
				if (verboseFlags != 0) {
					reportUtilityNotApplicable(vm, sharedClassConfig->ctrlDirName, cacheName, verboseFlags, runtimeFlags, command);
				}
			} else if (1 == cacheExists) {
				return J9VMDLLMAIN_OK;
			}
		}
		break;

	case RESULT_DO_PRINT_CACHENAME:
		if (SH_OSCache::getCacheDir(vm, sharedClassConfig->ctrlDirName, cacheDirName, J9SH_MAXPATH, cacheType) == -1) {
			return J9VMDLLMAIN_SILENT_EXIT_VM;
		}
		j9shr_print_cache_filename(vm, cacheDirName, runtimeFlags, cacheName, layer);
		break;

	case RESULT_DO_PRINT_SNAPSHOTNAME:
		if (-1 == SH_OSCache::getCacheDir(vm, sharedClassConfig->ctrlDirName, cacheDirName, J9SH_MAXPATH, J9PORT_SHR_CACHE_TYPE_SNAPSHOT)) {
			return J9VMDLLMAIN_SILENT_EXIT_VM;
		}
		j9shr_print_snapshot_filename(vm, cacheDirName, cacheName, layer);
		break;

	case RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS:
	case RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS:
	case RESULT_DO_FIND_AOT_METHODS_EQUALS:
		if (0 == strcmp(vm->sharedCacheAPI->methodSpecs, SUB_OPTION_AOT_METHODS_OPERATION_HELP)) {
			/* User has passed in findAotMethods/invalidateAotMethods/revalidateAotMethods=help" */
			const char* option = OPTION_FIND_AOT_METHODS_EQUALS;
			if (RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS == command) {
				option = OPTION_INVALIDATE_AOT_METHODS_EQUALS;
			} else if (RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS == command) {
				option = OPTION_REVALIDATE_AOT_METHODS_EQUALS;
			}
			SHRINIT_TRACE1_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_AOT_METHODS_OPERATION, option);
			SHRINIT_TRACE1_NOTAG(1, J9NLS_SHRC_SHRINIT_HELPTEXT_METHOD_SPEC, option);
			SHRINIT_TRACE2_NOTAG(1, J9NLS_SHRC_SHRINIT_INFOTEXT_METHOD_SPEC, option, option);
			break;
		}
		/* Fall through */
	case RESULT_DO_ADJUST_SOFTMX_EQUALS:
	case RESULT_DO_ADJUST_MINAOT_EQUALS:
	case RESULT_DO_ADJUST_MAXAOT_EQUALS:
	case RESULT_DO_ADJUST_MINJITDATA_EQUALS:
	case RESULT_DO_ADJUST_MAXJITDATA_EQUALS:
	case RESULT_DO_RESIZE_CACHE_EQUALS:
		if (1 == checkIfCacheExists(vm, sharedClassConfig->ctrlDirName, cacheDirName, cacheName, &versionData, cacheType, layer)) {
			return J9VMDLLMAIN_OK;
		}
		break;

	default:
		/* Return with a return code intended for non-utility options */
		return J9VMDLLMAIN_NON_UTILITY_OK;
	}

	return J9VMDLLMAIN_SILENT_EXIT_VM;
}

bool
modifyCacheName(J9JavaVM *vm, const char* origName, UDATA verboseFlags, char** modifiedCacheName, UDATA bufLen)
{
	UDATA origNameLen;
	UDATA bufLeft = bufLen;
	IDATA insertLen = 0;
	UDATA modIndex = 0;
	const char* cursorOrig = origName;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Ensure that "" cannot be used as a cache name */
	if (origName==NULL || strlen(origName)==0) {
		origName = CACHE_ROOT_PREFIX;
	}

	origNameLen = strlen(origName);
	if (origNameLen >= bufLen) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_NAME_LENGTH);
		return false;
	}

	/* Ensure null-termination */
	memset(*modifiedCacheName, 0, bufLen);

	/* If using default cache name, append '_' and username automatically */
	if (strcmp(origName, CACHE_ROOT_PREFIX)==0) {
		/* we will be adding atleast 2 chars -  '-' and atleast 1 character for 'username' */
		if( (origNameLen + 2) < bufLen) {
			strncpy((*modifiedCacheName), origName, origNameLen);
			(*modifiedCacheName)[origNameLen] = '_';
			modIndex = origNameLen + 1;
			bufLeft = bufLen - modIndex;

			if (j9sysinfo_get_username(&((*modifiedCacheName)[modIndex]), bufLeft) != 0) {
				char username[CACHE_ROOT_MAXLEN];
				if (j9sysinfo_get_username(username, CACHE_ROOT_MAXLEN) == 0) {
					IDATA usernamelen = (IDATA)strlen(username);
					SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_COPYING_USERNAME_TOOLONG,bufLeft,usernamelen);
				} else {
					SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_COPYING_USERNAME);
				}
				return false;
			}

			insertLen = strlen(&((*modifiedCacheName)[modIndex]));
			modIndex += insertLen;
			bufLeft = bufLen - modIndex;
		}
		else {
			/* code may not come here unless the
			 * length of default cache name +  2 >= allowed max length */
			bufLeft = 0;
		}
	}else {
		/* we are dealing with user-specified cache name */
		while (*cursorOrig && (bufLeft > 0)) {
			/* List of un-acceptable characters in cache name */
			if (*cursorOrig == '/' ||
			*cursorOrig == '\\' ||
			*cursorOrig == '$' ||
			*cursorOrig == '&' ||
			*cursorOrig == '*' ||
			((U_8)*cursorOrig) == SHRINIT_ASCII_OF_POUND_SYMBOL ||
			*cursorOrig == '=' ||
			*cursorOrig == '?')	{
				SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_NAME_CHAR, 1, cursorOrig);
				return false;
			}

			/* Decode escape sequences */
			if (*cursorOrig == '%') {
				char escapeChar = *(cursorOrig + 1);

				switch (escapeChar) {
				case 'u':
				case 'U':
					/* FALLTHRU is intentional due to same behaviour for 'u' and 'U'*/
					if (j9sysinfo_get_username(&((*modifiedCacheName)[modIndex]), bufLeft) != 0) {
						char username[CACHE_ROOT_MAXLEN];
						if (j9sysinfo_get_username(username, CACHE_ROOT_MAXLEN) == 0) {
							IDATA usernamelen = (IDATA)strlen(username);
							SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_COPYING_USERNAME_TOOLONG,bufLeft,usernamelen);
						} else {
							SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_COPYING_USERNAME);
						}
						return false;
					}
					break;

#if !(defined(WIN32) || defined(WIN64))
				case 'g':
				case 'G':
					/* FALLTHRU is intentional due to same behaviour for 'g' and 'G'
					 * Note that a value of -1 here is not an error - a NULL groupname is valid
					 */
					if (j9sysinfo_get_groupname(&((*modifiedCacheName)[modIndex]), bufLeft) > 0) {
						SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_COPYING_GROUPNAME);
						return false;
					}
					break;
#endif

				default:
						SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_UNREC_ESCAPE, 1, &escapeChar);
						return false;
				}

				insertLen = strlen(&((*modifiedCacheName)[modIndex]));
				modIndex += insertLen;
				cursorOrig += 2;			/* skip past the escape code */
			} else {
				(*modifiedCacheName)[modIndex++] = *cursorOrig;
				++cursorOrig;
			}

			bufLeft = bufLen - modIndex;
		}

		/* Check if the cache name contains only whitespace, or is empty */
		UDATA count = 0;
		bool onlyWhitespace = true;
		char* cursorModified = *modifiedCacheName;
		while (count < modIndex) {
			if ((*cursorModified != ' ') && (*cursorModified != '\t')) {
				onlyWhitespace = false;
				break;
			}
			count++;
			cursorModified++;
		}
		if (onlyWhitespace) {
			SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_NAME_ONLY_WHITESPACE, modIndex, *modifiedCacheName);
			return false;
		}

	}

	if (bufLeft == 0) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_NAME_LENGTH);
		return false;
	}

	return true;
}

UDATA
convertPermToDecimal(J9JavaVM *vm, const char *permStr) {
	I_32 permStrLen;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (NULL == permStr) {
		/* This means cacheDirPerm is not present */
		return J9SH_DIRPERM_ABSENT;
	}

	/* Valid values of cacheDirPerm are (0000, 1000, 0700 - 0777, 1700 - 1777) */
	permStrLen = (I_32)strlen(permStr);
	if ((3 == permStrLen) || (4 == permStrLen)) {
		I_32 i;
		UDATA decPerm = 0;
		IDATA term = 1;
		for (i = permStrLen-1; i >= 0; i--) {
			char digit = permStr[i];
			if ((digit < '0') || (digit > '7')) {
				goto _error;
			}
			decPerm += (digit - '0') * term;
			term *= 8;
		}
		if ((0 == decPerm) ||
			(01000 == decPerm) ||
			((0700 <= decPerm) && (0777 >= decPerm)) ||
			((01700 <= decPerm) && (01777 >= decPerm))
		) {
			return decPerm;
		} else {
			goto _error;
		}
	} else {
		goto _error;
	}
_error:
	SHRINIT_ERR_TRACE(1, J9NLS_SHRC_SHRINIT_FAILURE_INVALID_CACHEDIR_PERM);
	return (UDATA)-1;
}

/* Returns 0 for ok and 1 for error */
UDATA
ensureCorrectCacheSizes(J9JavaVM *vm, J9PortLibrary* portlib, U_64 runtimeFlags, UDATA verboseFlags, J9SharedClassPreinitConfig* piconfig)
{
	UDATA* cacheSize = &piconfig->sharedClassCacheSize;
	PORT_ACCESS_FROM_PORT(portlib);
	UDATA defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE;
	bool is64BitPlatDefaultSize = false;

#if defined(J9VM_ENV_DATA64)
#if defined(OPENJ9_BUILD)
	defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE_64BIT_PLATFORM;
#else /* OPENJ9_BUILD */
	if (J2SE_VERSION(vm) >= J2SE_V11) {
		defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE_64BIT_PLATFORM;
	}
#endif /* OPENJ9_BUILD */
#endif /* J9VM_ENV_DATA64 */

	if (*cacheSize == 0) {
		*cacheSize = defaultCacheSize;
		is64BitPlatDefaultSize = (J9_SHARED_CLASS_CACHE_DEFAULT_SIZE_64BIT_PLATFORM == defaultCacheSize);
	} else	if (*cacheSize < J9_SHARED_CLASS_CACHE_MIN_SIZE) {
		*cacheSize = J9_SHARED_CLASS_CACHE_MIN_SIZE;
	} else	if (*cacheSize > J9_SHARED_CLASS_CACHE_MAX_SIZE) {
		*cacheSize = J9_SHARED_CLASS_CACHE_MAX_SIZE;
	}

	U_64 maxSize = 0;
	if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == getCacheTypeFromRuntimeFlags(runtimeFlags)) {
		if ((J9PORT_LIMIT_LIMITED == j9sysinfo_get_limit(J9PORT_RESOURCE_SHARED_MEMORY, &maxSize))
			&& (*cacheSize > maxSize)
		) {
			adjustCacheSizes(portlib, verboseFlags, piconfig, maxSize);
		}
	} else {
		if (is64BitPlatDefaultSize) {
			if (isFreeDiskSpaceLow(vm, &maxSize, runtimeFlags)) {
				Trc_SHR_Assert_True(*cacheSize > maxSize);
				adjustCacheSizes(portlib, verboseFlags, piconfig, maxSize);
			}
		}
	}

	if (is64BitPlatDefaultSize && (piconfig->sharedClassCacheSize > J9_SHARED_CLASS_CACHE_MIN_DEFAULT_CACHE_SIZE_FOR_SOFTMAX)) {
		piconfig->sharedClassSoftMaxBytes = J9_SHARED_CLASS_CACHE_DEFAULT_SOFTMAX_SIZE_64BIT_PLATFORM;
	}

	if (piconfig->sharedClassSoftMaxBytes > (IDATA)*cacheSize) {
		SHRINIT_WARNING_TRACE1(verboseFlags, J9NLS_SHRC_SOFTMAX_TOO_BIG, *cacheSize);
		piconfig->sharedClassSoftMaxBytes = (IDATA)*cacheSize;
	}

	if ((piconfig->sharedClassMaxAOTSize >= 0) && (piconfig->sharedClassMinAOTSize > piconfig->sharedClassMaxAOTSize)) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_MINAOT_GRTHAN_MAXAOT);
		return 1;
	}
	if ((piconfig->sharedClassMaxJITSize >= 0) && (piconfig->sharedClassMinJITSize > piconfig->sharedClassMaxJITSize)) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_MINJITDATA_GRTHAN_MAXJITDATA);
		return 1;
	}
	bool softMaxSet = (piconfig->sharedClassSoftMaxBytes >= 0);

	if ((piconfig->sharedClassMinAOTSize > 0) && (piconfig->sharedClassMinJITSize > 0)) {
		if (softMaxSet) {
			if ((piconfig->sharedClassMinAOTSize + piconfig->sharedClassMinJITSize) > piconfig->sharedClassSoftMaxBytes) {
				SHRINIT_ERR_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_TOTAL_MINAOT_MINJIT_GRTHAN_SOFTMX, piconfig->sharedClassMinAOTSize, piconfig->sharedClassMinJITSize, piconfig->sharedClassSoftMaxBytes);
				return 1;
			}
		} else if (((piconfig->sharedClassMinAOTSize + piconfig->sharedClassMinJITSize) > (IDATA)*cacheSize)) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_TOTAL_MINAOT_MINJITDATA_GRTHAN_CACHESIZE);
			return 1;
		}
	}

	if (softMaxSet) {
		if (piconfig->sharedClassMinAOTSize > piconfig->sharedClassSoftMaxBytes) {
			SHRINIT_WARNING_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MINAOT_GRTHAN_SOFTMX, piconfig->sharedClassSoftMaxBytes);
			piconfig->sharedClassMinAOTSize = piconfig->sharedClassSoftMaxBytes;
		}
		if (piconfig->sharedClassMaxAOTSize > piconfig->sharedClassSoftMaxBytes) {
			SHRINIT_WARNING_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MAXAOT_GRTHAN_SOFTMX, piconfig->sharedClassSoftMaxBytes);
			piconfig->sharedClassMaxAOTSize = -1;
		}
		if (piconfig->sharedClassMinJITSize > piconfig->sharedClassSoftMaxBytes) {
			SHRINIT_WARNING_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MINJIT_GRTHAN_SOFTMX, piconfig->sharedClassSoftMaxBytes);
			piconfig->sharedClassMinJITSize = piconfig->sharedClassSoftMaxBytes;
		}
		if (piconfig->sharedClassMaxJITSize > piconfig->sharedClassSoftMaxBytes) {
			SHRINIT_WARNING_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MAXJIT_GRTHAN_SOFTMX, piconfig->sharedClassSoftMaxBytes);
			piconfig->sharedClassMaxJITSize = -1;
		}
	} else {
		if (piconfig->sharedClassMinAOTSize > (IDATA)*cacheSize) {
			SHRINIT_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MIN_SPACE_SIZE_VALUE_TOO_BIG, VMOPT_XSCMINAOT);
			piconfig->sharedClassMinAOTSize = *cacheSize;
		}
		if (piconfig->sharedClassMaxAOTSize > (IDATA)*cacheSize) {
			SHRINIT_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MAX_SPACE_SIZE_VALUE_TOO_BIG, VMOPT_XSCMAXAOT);
			piconfig->sharedClassMaxAOTSize = -1;
		}
		if (piconfig->sharedClassMinJITSize > (IDATA)*cacheSize) {
			SHRINIT_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MIN_SPACE_SIZE_VALUE_TOO_BIG, VMOPT_XSCMINJITDATA);
			piconfig->sharedClassMinJITSize = *cacheSize;
		}
		if (piconfig->sharedClassMaxJITSize > (IDATA)*cacheSize) {
			SHRINIT_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_MAX_SPACE_SIZE_VALUE_TOO_BIG, VMOPT_XSCMAXJITDATA);
			piconfig->sharedClassMaxJITSize = -1;
		}
	}

	/**
	 *  -Xitsn<tableSize> is used to set the number of elements for shared string intern table.
	 *  Therefore, it is preferable that it is dominant and
	 *  not restricted by SHRINIT_MAX_SHARED_STRING_TABLE_NODE_COUNT
	 *  If <tableSize> is 0, then we assume that user intention is not to have shared string intern table.
	 *  For any other value, required memory size is calculated and set to sharedClassReadWriteBytes with two exceptions:
	 *  	1.	<tableSize> should be in the supported range of primeNumberHelper,
	 *  		otherwise, error trace is printed and VM exits
	 *  	2.	required memory for shared string intern table with <tableSize> elements should
	 *  		not be bigger than shared cache size,
	 *  		otherwise, -Xitsn option is omitted and
	 *  		sharedClassReadWriteBytes is set to -1 meaning that it will set to a value
	 *  		proportional to cache size during composite cache creation in CompositeCache.c
	 *
	 */
	if (piconfig->sharedClassInternTableNodeCount > -1) {
		if (piconfig->sharedClassInternTableNodeCount == 0) {
			piconfig->sharedClassReadWriteBytes = 0;
		} else {
			piconfig->sharedClassReadWriteBytes = (UDATA)srpHashTable_requiredMemorySize((U_32)piconfig->sharedClassInternTableNodeCount, sizeof(J9SharedInternSRPHashTableEntry), TRUE);
			if (piconfig->sharedClassReadWriteBytes == PRIMENUMBERHELPER_OUTOFRANGE) {
				SHRINIT_ERR_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_VALUE_IS_NOT_SUPPORTED_BY_PRIMENUMBERHELPER, piconfig->sharedClassInternTableNodeCount, getSupportedBiggestNumberByPrimeNumberHelper());
				return 1;
			}
		}

		/* Do not need to compare sharedClassReadWriteBytes and sharedClassSoftMaxBytes here. ReadWrite area is always counted as used bytes within the softmx size.
		 * If sharedClassReadWriteBytes > sharedClassSoftMaxBytes, the softmx size will be increased in compositeCache.cpp to make sure softmx size include the ReadWrite area.
		 * */

		if ( (piconfig->sharedClassReadWriteBytes >= (IDATA)*cacheSize) ) {
			SHRINIT_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_SHARED_INTERN_TABLE_IS_BIGGER_THAN_SHARED_CACHE, piconfig->sharedClassReadWriteBytes,piconfig->sharedClassInternTableNodeCount,*cacheSize);
			piconfig->sharedClassReadWriteBytes = -1;
		}
	}
	return 0;
}

/*
 * Allocates and initialises SCAbstractAPI object.
 *
 * @param	vm
 *
 * @return	pointer to SCAbstractAPI on success, NULL on failure.
 */
SCAbstractAPI *
initializeSharedAPI(J9JavaVM *vm)
{
	SCAbstractAPI *scapi;

	PORT_ACCESS_FROM_JAVAVM(vm);

	scapi = (SCAbstractAPI *)j9mem_allocate_memory(sizeof(SCAbstractAPI), J9MEM_CATEGORY_CLASSES);
	if (NULL == scapi) {
		return NULL;
	}

	/*Set functions for SCStringTransaction*/
	scapi->stringTransaction_start = j9shr_stringTransaction_start;
	scapi->stringTransaction_stop = j9shr_stringTransaction_stop;
	scapi->stringTransaction_IsOK = j9shr_stringTransaction_IsOK;
	/*Set functions for SCStoreTransaction*/
	scapi->classStoreTransaction_start = j9shr_classStoreTransaction_start;
	scapi->classStoreTransaction_stop = j9shr_classStoreTransaction_stop;
	scapi->classStoreTransaction_nextSharedClassForCompare = j9shr_classStoreTransaction_nextSharedClassForCompare;
	scapi->classStoreTransaction_createSharedClass = j9shr_classStoreTransaction_createSharedClass;
	scapi->classStoreTransaction_updateSharedClassSize = j9shr_classStoreTransaction_updateSharedClassSize;
	scapi->classStoreTransaction_isOK = j9shr_classStoreTransaction_isOK;
	scapi->classStoreTransaction_hasSharedStringTableLock = j9shr_classStoreTransaction_hasSharedStringTableLock;
	scapi->classStoreTransaction_updateUnstoredBytes = j9shr_classStoreTransaction_updateUnstoredBytes;
	/*Set JCL functions*/
	scapi->jclUpdateROMClassMetaData = j9shr_jclUpdateROMClassMetaData;
	/*Set up functions for finishing share classes initialization*/
	scapi->sharedClassesFinishInitialization = j9shr_sharedClassesFinishInitialization;
	/*Set up functions to query the state of shared classes*/
	scapi->isCacheFull = j9shr_isCacheFull;
	scapi->isAddressInCache = j9shr_isAddressInCache;
	/* Set up fimcyopms to get shared classes defaults for -verbose:sizes */
	scapi->populatePreinitConfigDefaults = j9shr_populatePreinitConfigDefaults;

	return scapi;

}

static UDATA
initializeSharedStringTable(J9JavaVM* vm)
{
	J9SharedInvariantInternTable* table = vm->sharedInvariantInternTable;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA verboseIntern = (sharedClassConfig->verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_INTERN);
	SH_CacheMap* cacheMap = (SH_CacheMap*)(vm->sharedClassConfig->sharedClassCache);

	PORT_ACCESS_FROM_JAVAVM(vm);

	if (verboseIntern) {
		j9tty_printf(PORTLIB, "Initializing shared string table...\n");
	}

	if (!table) {
		if (verboseIntern) {
			j9tty_printf(PORTLIB, "   FAILED due to string interning disabled\n");
		}
		return 0;
	}

	cacheMap->getCompositeCacheAPI()->setInternCacheHeaderFields(
	    	&(table->sharedTailNodePtr),
	    	&(table->sharedHeadNodePtr),
		    &(table->totalSharedNodesPtr),
	    	&(table->totalSharedWeightPtr));

    if (verboseIntern) {
		j9tty_printf(PORTLIB, "   Created new shared string table.  sharedHead=%d, sharedTail=%d\n",
				*(table->sharedHeadNodePtr), *(table->sharedTailNodePtr));
	}

	if (verboseIntern) {
		j9tty_printf(PORTLIB, "   Creating a new string intern srphashtable...\n");
	}

	if (cacheMap->isStringTableInitialized()) {
		/* string table has already been initialized, use it */
		table->sharedInvariantSRPHashtable = srpHashTableRecreate(
															vm->portLibrary,
															J9_GET_CALLSITE(),
															cacheMap->getStringTableBase(),
															sharedInternHashFn,
															sharedInternHashEqualFn,
															NULL,
															vm);
	} else {
    	table->sharedInvariantSRPHashtable = srpHashTableNewInRegion(
    														vm->portLibrary,
    														J9_GET_CALLSITE(),
    														cacheMap->getStringTableBase(),
    														(U_32) cacheMap->getStringTableBytes(),
    														(U_32) sizeof(J9SharedInternSRPHashTableEntry),
    														0,
    														sharedInternHashFn,
    														sharedInternHashEqualFn,
    														NULL,
    														vm);

       	if ( NULL != table->sharedInvariantSRPHashtable) {
    		cacheMap->setStringTableInitialized(true);
    		Trc_SHR_INIT_sharedStringInternTableCreated(srpHashTable_tableSize(table->sharedInvariantSRPHashtable));
    	}
	}

	if (NULL == table->sharedInvariantSRPHashtable)
	{
		Trc_SHR_INIT_sharedStringInternTableIsNotCreated();
		return 0;
	}

	if (verboseIntern) {
		j9tty_printf(PORTLIB, "   Succeeded in getting a srphashtable. ");
	}
	if (*(table->sharedHeadNodePtr)) {
		table->headNode = NNSRP_PTR_GET(table->sharedHeadNodePtr, J9SharedInternSRPHashTableEntry*);
		table->tailNode = NNSRP_PTR_GET(table->sharedTailNodePtr, J9SharedInternSRPHashTableEntry*);
		if (verboseIntern) {
			j9tty_printf(PORTLIB, "Set up the table with the following values:\n");
			j9tty_printf(PORTLIB, "      sharedTable->headNode = %p\n", table->headNode);
			j9tty_printf(PORTLIB, "      sharedTable->tailNode = %p\n", table->tailNode);
		}
	} else {
		if (verboseIntern) {
			j9tty_printf(PORTLIB, "No existing shared table data\n");
		}
	}
	table->flags |= J9AVLTREE_SHARED_TREE_INITIALIZED;
	if ((sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_VERIFY_TREE_AND_TREE_ACCESS) != 0 ) {
		table->flags |= J9AVLTREE_DO_VERIFY_TREE_STRUCT_AND_ACCESS;
	}
	table->systemClassLoader = vm->systemClassLoader;

	if (verboseIntern) {
		j9tty_printf(PORTLIB, "Shared string table successfully initialized.\nShared table = %d nodes. \n\n", *table->totalSharedNodesPtr);
	}
    return 1;
}

static BOOLEAN
verifyStringTableElement(void *address, void *userData)
{
	J9SharedInternSRPHashTableEntry *node = (J9SharedInternSRPHashTableEntry *)address;
	J9SharedVerifyStringTable *verifyData = (J9SharedVerifyStringTable *)userData;
	void *utf8 = J9SHAREDINTERNSRPHASHTABLEENTRY_UTF8SRP(node);
	void *prevNode = J9SHAREDINTERNSRPHASHTABLEENTRY_PREVNODE(node);
	void *nextNode = J9SHAREDINTERNSRPHASHTABLEENTRY_NEXTNODE(node);
	if (((UDATA)utf8 & 1) == 1 || utf8 < verifyData->romClassAreaStart || utf8 >= verifyData->romClassAreaEnd) {
		Trc_SHR_INIT_verifyStringTableElement_utf8Corrupt(utf8, address, verifyData->simplePool);
		return FALSE;
	}
	if ((prevNode != NULL) && !simplepool_isElement(verifyData->simplePool, prevNode)) {
		Trc_SHR_INIT_verifyStringTableElement_lruCorrupt(prevNode, address, verifyData->simplePool);
		return FALSE;
	}
	if ((nextNode != NULL) && !simplepool_isElement(verifyData->simplePool, nextNode)) {
		Trc_SHR_INIT_verifyStringTableElement_lruCorrupt(nextNode, address, verifyData->simplePool);
		return FALSE;
	}
	if (0 != (node->flags & ~(STRINGINTERNTABLES_NODE_FLAG_UTF8_IS_SHARED))) {
		Trc_SHR_INIT_verifyStringTableElement_flagsCorrupt(node->flags, address, verifyData->simplePool);
		return FALSE;
	}
	return TRUE;
}

UDATA
j9shr_isBCIEnabled(J9JavaVM *vm)
{
	return (0 != (vm->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_BCI));
}

/**
 * JVM Initialisation processing for shared classes
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] loadFlags  Not used
 * @param [out] nonfatal  Set to 1 if "nonfatal" is specified as a command line suboption for shared classes
 *
 * @return Return code from processing a utility function, if requested by the command line arguments, otherwise
 *       an indication of whether the JVM should continue starting or should fail
 * @arg J9VMDLLMAIN_FAILED  JVM start up should fail
 * @arg J9VMDLLMAIN_SILENT_EXIT_VM  JVM should exit silently
 * @arg J9VMDLLMAIN_OK  JVM start up should continue
 */
IDATA
j9shr_init(J9JavaVM *vm, UDATA loadFlags, UDATA* nonfatal)
{
  	/* TODO: need to get root cache name from VM parameters. */
	const char * fname = "j9shr_init";
	UDATA memBytesNeeded = 0;
	SH_CacheMap* cmPtr = NULL;
	SH_CacheMap* cm = NULL;
	U_64 runtimeFlags = vm->sharedCacheAPI->runtimeFlags;
	UDATA verboseFlags = vm->sharedCacheAPI->verboseFlags;
	UDATA printStatsOptions = vm->sharedCacheAPI->printStatsOptions;
	J9HookInterface** hook = vm->internalVMFunctions->getVMHookInterface(vm);
	UDATA parseResult = vm->sharedCacheAPI->parseResult;
	IDATA rc, rcStartup = 0;
	const char* cacheName = vm->sharedCacheAPI->cacheName;
	char* copiedCacheName = NULL;
	char modifiedCacheName[CACHE_ROOT_MAXLEN];
	char* modifiedCacheNamePtr = (char*)modifiedCacheName;
	char* modContext = vm->sharedCacheAPI->modContext;
	char* expireTime = vm->sharedCacheAPI->expireTime;
	char* ctrlDirName = vm->sharedCacheAPI->ctrlDirName;
	I_8 layer = vm->sharedCacheAPI->layer;
	IDATA returnVal = J9VMDLLMAIN_FAILED;
	UDATA cmBytes, nameBytes, modContextBytes;
	J9SharedClassConfig* tempConfig;
	J9SharedClassPreinitConfig* piconfig = vm->sharedClassPreinitConfig;
	J9UTF8* mcPtr;
	bool doPrintStats = false;
	bool exitAfterBuildingTempConfig = false;
	bool cacheHasIntegrity;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
	I_32 cacheType = 0;
	I_8 maxLayer = -1;
	char cacheDirName[J9SH_MAXPATH];

	PORT_ACCESS_FROM_JAVAVM(vm);

	UnitTest::unitTest = UnitTest::NO_TEST;
	vm->sharedClassConfig = NULL;

	Trc_SHR_INIT_j9shr_init_Entry(currentThread);

	/* noTimestampChecks and checkURLTimestamps shouldn't coexist no matter what order
	 * they are specified on the command line. Thus, checkURLTimestamps will be ignored if specified.
	 */
	if (J9_ARE_NO_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_TIMESTAMP_CHECKS)
	&& J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_URL_TIMESTAMP_CHECK)
	) {
		SHRINIT_WARNING_TRACE2(1, J9NLS_SHRC_SHRINIT_INCOMPATIBLE_OPTION, OPTION_NO_TIMESTAMP_CHECKS, OPTION_URL_TIMESTAMP_CHECK);
		vm->sharedCacheAPI->runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_URL_TIMESTAMP_CHECK;
	}

	if (FALSE == vm->sharedCacheAPI->xShareClassesPresent) {
		Trc_SHR_Assert_True(vm->sharedCacheAPI->sharedCacheEnabled);
		Trc_SHR_Assert_True(J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL));
		Trc_SHR_Assert_True(J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_CACHEBOOTCLASSES));
		Trc_SHR_Assert_True(J9_ARE_NO_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES));
		Trc_SHR_Assert_True(0 == vm->sharedCacheAPI->verboseFlags);
		Trc_SHR_INIT_j9shr_init_BootClassSharingEnabledByDefault(currentThread);
	}

	if (((0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READONLY)) ||
		(0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READWRITE))) &&
		(0 == (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_ROUND_TO_PAGE_SIZE)))
	{
		/* CMVC 176498 : When testing string table reset,
		 * ensure J9SHR_RUNTIMEFLAG_ENABLE_ROUND_TO_PAGE_SIZE is set.
		 */
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_CHECK_STRINGTABLE_RESET_MISSING_FLAG);
		goto _error;
	}

	/* If the command line is ordered like this: -Xshareclasses:verifyInternTree,mprotect=all
	 * parseArgs() may still turn mprotect on when tree verification, or test, is enabled.
	 * So we must disable mprotect in this case.
	 *
	 * We must do this b/c in the case of a full cache we may only obtain readonly access to the cache.
	 * This will cause avl_intern_verify to fail marking avl tree nodes, b/c the shared memory is
	 * protected.
	 */
	if (((runtimeFlags & J9SHR_RUNTIMEFLAG_VERIFY_TREE_AND_TREE_ACCESS) == J9SHR_RUNTIMEFLAG_VERIFY_TREE_AND_TREE_ACCESS) ||
		(parseResult == RESULT_DO_TEST_INTERNAVL)
	) {
		if ( runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL ) {
			runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW);
			runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT;
		} else if ( runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW ) {
			runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW);
		}
	}

	if (ensureCorrectCacheSizes(vm, vm->portLibrary, runtimeFlags, verboseFlags, piconfig) != 0) {
		goto _error;
	}

    cacheType = getCacheTypeFromRuntimeFlags(runtimeFlags);

	/* Do not move this line - any failures after this are affected by nonfatal, any before are not */
	if (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL) {
		*nonfatal = 1;
	}

	if (!modifyCacheName(vm, cacheName, verboseFlags, &modifiedCacheNamePtr, USER_SPECIFIED_CACHE_NAME_MAXLEN)) {
		/* CMVC 141167: If we fail to modify the name we can't simply exit.
		* We must ensure the runtimeFlags are available during j9shr_lateInit()
		* otherwise we will not be able to see if nonfatal is used.
		*/
		exitAfterBuildingTempConfig = true;
	}

	if ((RESULT_DO_PRINTSTATS == parseResult) ||
		(RESULT_DO_PRINTALLSTATS == parseResult) ||
		(RESULT_DO_PRINTORPHANSTATS == parseResult) ||
		(RESULT_DO_PRINT_TOP_LAYER_STATS == parseResult) ||
		(RESULT_DO_PRINTALLSTATS_EQUALS == parseResult) ||
		(RESULT_DO_PRINTSTATS_EQUALS == parseResult) ||
		(RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS == parseResult)
	) {
		doPrintStats = true;
		/* Do not try to kill a cache if we just want to get stats on it */
		/* set J9SHR_RUNTIMEFLAG_ENABLE_READONLY. If not set, vmCntr will be increased in the cache header */
		runtimeFlags |= (J9SHR_RUNTIMEFLAG_ENABLE_STATS | J9SHR_RUNTIMEFLAG_ENABLE_READONLY);
		runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION;
		runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID;
		runtimeFlags &= ~J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID;

		/* Ignore incompatible options as we just want to get stats on it */
		runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_ENABLE_CACHERETRANSFORMED | J9SHR_RUNTIMEFLAG_ENABLE_BCI);
	}

	if ((RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS == parseResult)
		|| (RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS == parseResult)
	) {
		/* ignore 'readOnly' when invalidate/revalidate AOT methods */
		if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_READONLY)) {
			const char* option = (RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS == parseResult) ? OPTION_INVALIDATE_AOT_METHODS_EQUALS : OPTION_REVALIDATE_AOT_METHODS_EQUALS;
			runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_READONLY;
			SHRINIT_WARNING_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_OPTION_IGNORED_WARNING, OPTION_READONLY, option, OPTION_READONLY);
			if (verboseFlags > 0) {
				j9tty_printf(PORTLIB, "\n");
			}
		}
	}

	if (RESULT_DO_RESET == parseResult) {
		/* For 'reset', we need to check for incompatible options before taking any action in performSharedClassesCommandLineAction() */
		if ((0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_CACHERETRANSFORMED))
			&& (0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_BCI))
		) {
			SHRINIT_ERR_TRACE2(1, J9NLS_SHRC_SHRINIT_INCOMPATIBLE_OPTION, OPTION_ENABLE_BCI, OPTION_CACHERETRANSFORMED);
			goto _error;
		}
	}

	/* Allocate the cache */

	cmBytes = SH_CacheMap::getRequiredConstrBytes(false);
	nameBytes = (strlen(modifiedCacheNamePtr)+1) * sizeof(char);
	modContextBytes = modContext ? (((strlen(modContext) + 1) * sizeof(char)) + sizeof(J9UTF8)) : 0;
	memBytesNeeded = sizeof(J9SharedClassConfig) + sizeof(J9SharedClassCacheDescriptor) + cmBytes + nameBytes + modContextBytes;

	tempConfig = (J9SharedClassConfig*)j9mem_allocate_memory(memBytesNeeded, J9MEM_CATEGORY_CLASSES);
	if (!tempConfig) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_ALLOCATING_CONFIG);
		goto _error;
	}
	memset(tempConfig, 0, memBytesNeeded);

	tempConfig->ctrlDirName = ctrlDirName;
	tempConfig->layer = layer;

	rc = performSharedClassesCommandLineAction(vm, tempConfig, modifiedCacheNamePtr, verboseFlags, runtimeFlags, expireTime, parseResult, printStatsOptions);
	if ((J9VMDLLMAIN_FAILED == rc) || (J9VMDLLMAIN_SILENT_EXIT_VM == rc)) {
		*nonfatal = 0;		/* Nonfatal should be ignored for utilities */
		j9mem_free_memory(tempConfig);
		return rc;
	}

	/* check for incompatible options */
	if ((0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_CACHERETRANSFORMED))
		&& (0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_BCI))
	) {
		SHRINIT_ERR_TRACE2(1, J9NLS_SHRC_SHRINIT_INCOMPATIBLE_OPTION, OPTION_ENABLE_BCI, OPTION_CACHERETRANSFORMED);
		goto _error;
	}

	/* Initialize the cache */

	tempConfig->cacheDescriptorList = (J9SharedClassCacheDescriptor*)((UDATA)tempConfig + sizeof(J9SharedClassConfig));
	cmPtr = (SH_CacheMap*)((UDATA)tempConfig->cacheDescriptorList + sizeof(J9SharedClassCacheDescriptor));
	mcPtr = (J9UTF8*)((UDATA)cmPtr + cmBytes);
	copiedCacheName = (char*)((UDATA)mcPtr + modContextBytes);

	/* make this list circular */
	tempConfig->cacheDescriptorList->next = tempConfig->cacheDescriptorList;
	tempConfig->cacheDescriptorList->previous = tempConfig->cacheDescriptorList;

	/* Copy the cache name */
	strcpy(copiedCacheName, modifiedCacheNamePtr);
	cacheName = copiedCacheName;

	/* Copy the modification context */
	tempConfig->modContext = NULL;
	if (modContext) {
		J9UTF8_SET_LENGTH(mcPtr, (U_16)strlen(modContext));
		strcpy((char*)J9UTF8_DATA(mcPtr), modContext);
		tempConfig->modContext = mcPtr;
	}

	tempConfig->runtimeFlags = runtimeFlags;
	tempConfig->verboseFlags = verboseFlags;
	tempConfig->softMaxBytes = vm->sharedCacheAPI->softMaxBytes;
	tempConfig->minAOT = vm->sharedCacheAPI->minAOT;
	tempConfig->maxAOT = vm->sharedCacheAPI->maxAOT;
	tempConfig->minJIT = vm->sharedCacheAPI->minJIT;
	tempConfig->maxJIT = vm->sharedCacheAPI->maxJIT;

	/* Fill in the getJavacoreData address so we can dump information about the shared cache if
	 * the cache is found corrupted during startup.
	 */
	tempConfig->getJavacoreData = j9shr_getJavacoreData;

	if (exitAfterBuildingTempConfig == true) {
		vm->sharedClassConfig = tempConfig;
		goto _error;
	}

	if (omrthread_monitor_init(&(tempConfig->configMonitor), 0)) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_CFGMONITOR);
		goto _error;
	}

	if (omrthread_monitor_init(&(tempConfig->jclCacheMutex), 0)) {
		SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_JCLCACHEMUTEX);
		omrthread_monitor_destroy(tempConfig->configMonitor);
		goto _error;
	}
	/* Start up the cache */
	vm->sharedClassConfig = tempConfig;

	j9shr_getCacheDir(vm, ctrlDirName, cacheDirName, J9SH_MAXPATH, cacheType);
	findExistingCacheLayerNumbers(vm, cacheDirName, cacheName, runtimeFlags, &maxLayer);
	if (-1 == layer) {
		/* Neither of "layer=" nor "createLayer" are used in the command line */
		if (-1 == maxLayer) {
			/* There is no existing layers under cacheName. This JVM can create a new cache layer 0. */
			vm->sharedClassConfig->layer = 0;
		} else {
			/* There are existing layers under cacheName, use the existing maximum layer, but do not create new layer.
			 * J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE is not set here, so if maxLayer cache has a different build ID,
			 * it will be deleted and a new one will be created.
			 */
			vm->sharedClassConfig->layer = maxLayer;
		}
	} else if (SHRINIT_CREATE_NEW_LAYER == layer) {
		/* "createLayer" is used in the command line */
		vm->sharedClassConfig->layer = maxLayer + 1;
	} else {
		/* "layer=" is used in the command line */
		if (layer > (maxLayer + 1)) {
			SHRINIT_ERR_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_INVALIDATE_LAYER_NUMBER, layer, maxLayer, maxLayer + 1);
			goto _error;
		} else if ((maxLayer + 1) == layer) {
			/* Create a new layer */
		} else {
			/* layer <= maxLayer */
			/* An existing shared cache with higer layer number already exists */
			/* Use the layer number in the CML, but do not create new cache layer */
			vm->sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE;
		}
	}

	/*Add the cachemap before calling startup to enable debug extensions in jextract etc*/
	cm = SH_CacheMap::newInstance(vm, vm->sharedClassConfig, cmPtr, cacheName, cacheType);
	vm->sharedClassConfig->sharedClassCache = (void*)cm;

	if (RESULT_DO_RESTORE_FROM_SNAPSHOT == parseResult) {
		*nonfatal = 0;
		bool cacheExist = false;
		rcStartup = j9shr_restoreFromSnapshot(vm, ctrlDirName, cacheName, &cacheExist);
		if (0 == rcStartup) {
			SHRINIT_TRACE1(vm->sharedClassConfig->verboseFlags, J9NLS_SHRC_SHRINIT_SUCCESS_RESTORE_CACHE, cacheName);
			return J9VMDLLMAIN_SILENT_EXIT_VM;
		} else {
			/* "restoreFromSnapshot" option failed */
			if (false == cacheExist) {
				SHRINIT_ERR_TRACE1(vm->sharedClassConfig->verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_RESTORE_CACHE, cacheName);
			}
		}
	} else {
		rcStartup = cm->startup(currentThread, piconfig, cacheName, ctrlDirName, vm->sharedCacheAPI->cacheDirPerm, NULL, &cacheHasIntegrity);
	}

	if (rcStartup != 0) {
		J9SharedClassConfig* config = vm->sharedClassConfig;

		config->sharedClassCache = NULL;

		goto _error;
	} else {
		J9TranslationBufferSet* translationBuffers = vm->dynamicLoadBuffers;
		J9SharedClassConfig* config = vm->sharedClassConfig;
		J9HookInterface **shcHooks;

		/* Create the pools */

		config->jclClasspathCache = NULL;
		config->jclTokenCache = NULL;
		config->jclURLCache = NULL;
		config->jclURLHashTable = NULL;
		config->jclUTF8HashTable = NULL;
		config->jclJ9ClassPathEntryPool = pool_new(sizeof(struct J9ClassPathEntry), 0, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_CLASSES, POOL_FOR_PORT(vm->portLibrary));
		if (!(config->jclJ9ClassPathEntryPool)) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_POOL);
			goto _error;
		}

		config->getCacheSizeBytes = j9shr_getCacheSizeBytes;
		config->getTotalUsableCacheBytes = j9shr_getTotalUsableCacheBytes;
		config->getSharedClassCacheMode = j9shr_getSharedClassCacheMode;
		config->getMinMaxBytes = j9shr_getMinMaxBytes;
		config->setMinMaxBytes = j9shr_setMinMaxBytes;
		config->increaseUnstoredBytes = j9shr_increaseUnstoredBytes;
		config->getUnstoredBytes = j9shr_getUnstoredBytes;
		config->getFreeSpaceBytes = j9shr_getFreeAvailableSpaceBytes;
		config->findSharedData = j9shr_findSharedData;
		config->storeSharedData = j9shr_storeSharedData;
		config->findCompiledMethodEx1 = j9shr_findCompiledMethodEx1;
		config->storeCompiledMethod = j9shr_storeCompiledMethod;
		config->storeAttachedData = j9shr_storeAttachedData;
		config->findAttachedData = j9shr_findAttachedData;
		config->updateAttachedData = j9shr_updateAttachedData;
		config->updateAttachedUDATA = j9shr_updateAttachedUDATA;
		config->freeAttachedDataDescriptor = j9shr_freeAttachedDataDescriptor;
		config->existsCachedCodeForROMMethod = j9shr_existsCachedCodeForROMMethod;
		config->acquirePrivateSharedData = j9shr_acquirePrivateSharedData;
		config->releasePrivateSharedData = j9shr_releasePrivateSharedData;
		config->isBCIEnabled = j9shr_isBCIEnabled;
		config->freeClasspathData = j9shr_freeClasspathData;
		config->jvmPhaseChange = j9shr_jvmPhaseChange;
		config->findGCHints = j9shr_findGCHints;
		config->storeGCHints = j9shr_storeGCHints;
		config->updateClasspathOpenState = j9shr_updateClasspathOpenState;

		config->sharedAPIObject = initializeSharedAPI(vm);
		if (config->sharedAPIObject == NULL) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_API_CREATE_FAILURE);
			goto _error;
		}

		/* Register hooks */
		(*hook)->J9HookRegisterWithCallSite(hook, J9HOOK_VM_FIND_LOCALLY_DEFINED_CLASS, hookFindSharedClass, OMR_GET_CALLSITE(), NULL);

		/* We don't need the string table when running with -Xshareclasses:print<XXXX>Stats.
		 * 		This is because the JVM is terminated, after displaying the requested information.
		 * We also can't use a shared string table when we run in read-only mode.
		 * 		This is because we can't get any cross-process locks, which means that it's not safe for us to search the table.
		 */
		if (translationBuffers && (translationBuffers->flags & BCU_ENABLE_INVARIANT_INTERNING) &&
				(0 ==(runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_STATS)))
		{
			if (!(vm->sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY)) {
				UDATA doRebuildCacheData = 0;
				UDATA verifyError = 0;
				UDATA doRebuildLocalData = 0;
				SH_CacheMap* cm = (SH_CacheMap*)(vm->sharedClassConfig->sharedClassCache);
				omrthread_monitor_t tableInternFxMutex;

				/* This monitor is used to control thread access to the internavl tree.
				 * This monitor should always be uncontended, it was added during the investigation
				 * for CMVC 147834 to check if concurrent access to the string tree was causing
				 * corruption.
				 */
				if (omrthread_monitor_init_with_name(&tableInternFxMutex, 0, "XshareclassesVerifyInternTableMon") != 0) {
					goto _error;
				}

	        	if ((0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READONLY)) ||
					(0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_CHECK_STRINGTABLE_RESET_READWRITE))
				){
	        		/* CMVC 176498 : Ensure that string table size is large enough for -Xshareclasses:checkStringTableReset to work.
	        		 * String table should go beyond the page containing cache header (because cache header is not mprotected by default).
	        		 */
	        		SH_CompositeCacheImpl *cc = (SH_CompositeCacheImpl *)cm->getCompositeCacheAPI();
	        		UDATA osPageSize = (UDATA)cc->getOSPageSize();
	        		UDATA tableBase = (UDATA)cm->getStringTableBase();
	        		UDATA headerAddress = (UDATA)cc->getCacheHeaderAddress();
	        		UDATA totalSize = 0;
	        		/* below calculation is same as done in srpHashTableReset() to get string table size */
	        		UDATA tableSize = srpHashTable_calculateTableSize((U_32) cm->getStringTableBytes(), (U_32) sizeof(J9SharedInternSRPHashTableEntry), FALSE);

	        		if ((0 == tableSize) || (PRIMENUMBERHELPER_OUTOFRANGE == tableSize)) {
	        			SHRINIT_ERR_TRACE2(vm->sharedClassConfig->verboseFlags, J9NLS_SHRC_SHRINIT_CHECK_STRING_TABLE_RESET_MAY_FAIL, totalSize, osPageSize);
	        			goto _error;
	        		}

	        		totalSize = ROUND_TO_SIZEOF_UDATA(sizeof(J9SRP) * tableSize);
	        		if ((tableBase + totalSize) < (headerAddress + osPageSize)) {
	        			SHRINIT_ERR_TRACE2(vm->sharedClassConfig->verboseFlags, J9NLS_SHRC_SHRINIT_CHECK_STRING_TABLE_RESET_MAY_FAIL, totalSize, osPageSize);
	        			goto _error;
	        		}
	        	}

				if (cm->startClassTransaction(currentThread, false, fname) == 0) {
					if (cm->enterStringTableMutex(currentThread, FALSE, &doRebuildLocalData, &doRebuildCacheData) == 0) {
						UDATA resetReason = J9SHR_STRING_POOL_OK;

						/*-----SRPHASHTABLE------------------*/
						vm->sharedInvariantInternTable = (J9SharedInvariantInternTable *) j9mem_allocate_memory((UDATA) sizeof(J9SharedInvariantInternTable), J9MEM_CATEGORY_CLASSES);
						if (vm->sharedInvariantInternTable == NULL) {
							cm->exitStringTableMutex(currentThread, resetReason);
							cm->exitClassTransaction(currentThread, fname);
							goto _error;
						}

						/* SH_CacheMap::enterStringTableMutex() will not disable updates if the cache
						 * is full on the first call b/c sharedInvariantInternTable is NULL. We do the below
						 * check below to ensure the vm behaviour stays the same when a cache is found to
						 * be full.
						 */
						cm->updateRuntimeFullFlags(currentThread);
						if (J9_ARE_ANY_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_AVAILABLE_SPACE_FULL)) {
							/* Disable all updates to the shared tree */
							vm->sharedInvariantInternTable->flags |= J9AVLTREE_DISABLE_SHARED_TREE_UPDATES;
						}

						memset(vm->sharedInvariantInternTable, 0, sizeof(J9SharedInvariantInternTable));
						vm->sharedInvariantInternTable->performNodeAction = (UDATA (*)(J9SharedInvariantInternTable*, J9SharedInternSRPHashTableEntry*, UDATA, void*))sharedInternTable_performNodeAction;
						vm->sharedInvariantInternTable->tableInternFxMutex = tableInternFxMutex;
						if (initializeSharedStringTable(vm)) {
							if (!doRebuildCacheData &&(!cacheHasIntegrity || ((runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_STRING_TABLE_CHECK) != 0))) {
									J9SharedInvariantInternTable* table = vm->sharedInvariantInternTable;

									/**
									 * If we are here than there is shared string intern table  in shared cache.
									 * Since the changes in getStringTableBytes() implementation, old caches will return 0 for this function.
									 * In such cases, use all readWriteBytes to verify SRPHashTable,
									 * because old caches used to use all readWriteBytes to generate shared string intern table.
									 */
									UDATA stringTableBytes = cm->getStringTableBytes();
									if (stringTableBytes == 0) {
										stringTableBytes = cm->getReadWriteBytes();
									}

									/* Do an integrity check of the SRPHashTable. */
									if (srpHashTableVerify(table->sharedInvariantSRPHashtable, (U_32) cm->getStringTableBytes(), (U_32) sizeof(J9SharedInternSRPHashTableEntry))) {
										J9SharedVerifyStringTable verifyStringTable;
										UDATA numElements = srpHashTableGetCount(table->sharedInvariantSRPHashtable);
										if (numElements != *(table->totalSharedNodesPtr)) {
											Trc_SHR_INIT_failedSimplePoolIntegrity(*(table->totalSharedNodesPtr), numElements);
											verifyError = 1;
											resetReason = J9SHR_STRING_POOL_FAILED_VERIFY;
										} else {
											verifyStringTable.simplePool = J9SRPHASHTABLEINTERNAL_NODEPOOL(table->sharedInvariantSRPHashtable->srpHashtableInternal);
											cm->getRomClassAreaBounds(&verifyStringTable.romClassAreaStart, &verifyStringTable.romClassAreaEnd);
											if (!srpHashTable_checkConsistency(table->sharedInvariantSRPHashtable,
													PORTLIB,
													&verifyStringTableElement,
													(void *)&verifyStringTable,
													4096 / sizeof(J9SharedInternSRPHashTableEntry) / 2)
											) {
												Trc_SHR_INIT_failedSimplePoolConsistency();
												verifyError = 1;
												resetReason = J9SHR_STRING_POOL_FAILED_CONSISTENCY;
											}
										}
									} else {
										verifyError = 1;
										resetReason = J9SHR_STRING_POOL_FAILED_VERIFY;
									}
								}
							} else {
								j9mem_free_memory(vm->sharedInvariantInternTable);
								vm->sharedInvariantInternTable = NULL;
							}
							if (verifyError) {
								/* A reset at runtime is always a bad thing so we assert to capture information*/
								Trc_SHR_Assert_StringTableReset();
							}
							if (doRebuildCacheData || verifyError) {
								j9shr_resetSharedStringTable(vm);
							}
							cm->exitStringTableMutex(currentThread, resetReason);
						}
					cm->exitClassTransaction(currentThread, fname);
				} else {
					Trc_SHR_INIT_enterReadWriteLockFailed();
				}
			} else {
				Trc_SHR_INIT_enterWriteLockFailed();
			}

			/* TESTING ONLY */
			if (parseResult == RESULT_DO_TEST_INTERNAVL) {
				/* shared string table must be initialized */
				if (vm->sharedInvariantInternTable) {
					vm->sharedInvariantInternTable->flags |= J9AVLTREE_TEST_INTERNAVL;
				}
			}
		}
		shcHooks = zip_getVMZipCachePoolHookInterface((J9ZipCachePool *)vm->zipCachePool);
		(*shcHooks)->J9HookRegisterWithCallSite(shcHooks, J9HOOK_VM_ZIP_LOAD, j9shr_hookZipLoadEvent, OMR_GET_CALLSITE(), NULL);
	}

	if (0 != (runtimeFlags & J9SHR_RUNTIMEFLAG_ADD_TEST_JITHINT)) {
		J9HookInterface** hook = vm->internalVMFunctions->getVMHookInterface(vm);
		(*hook)->J9HookRegisterWithCallSite(hook, J9HOOK_VM_FIND_LOCALLY_DEFINED_CLASS, addTestJitHint, OMR_GET_CALLSITE(), NULL);
	}

	if (doPrintStats) {
		if (j9shr_print_stats(vm, parseResult, runtimeFlags, printStatsOptions) != -1) {
			*nonfatal = 0;		/* Nonfatal should be ignored for stats utilities */
			returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
		}
	} else {
		returnVal = J9VMDLLMAIN_OK;
	}

	if (RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS == parseResult) {
		IDATA numMethods = 0;

		*nonfatal = 0;
		numMethods = j9shr_aotMethodOperation(vm, vm->sharedCacheAPI->methodSpecs, SHR_INVALIDATE_AOT_METHOTHODS);
		if (numMethods > 0) {
			if (verboseFlags > 0) {
				j9tty_printf(PORTLIB, "\n");
			}
			SHRINIT_TRACE1_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_INVALIDATE_AOT_METHODS_OPERATION_SUCCESS, numMethods);
		} else if (numMethods < 0) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_INVALIDATE_AOT_METHODS_OPERATION_FAILURE);
		} else {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_NO_AOT_METHOD_FOUND);
		}
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}

	if (RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS == parseResult) {
		IDATA numMethods = 0;

		*nonfatal = 0;
		numMethods = j9shr_aotMethodOperation(vm, vm->sharedCacheAPI->methodSpecs, SHR_REVALIDATE_AOT_METHOTHODS);
		if (numMethods > 0) {
			if (verboseFlags > 0) {
				j9tty_printf(PORTLIB, "\n");
			}
			SHRINIT_TRACE1_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_REVALIDATE_AOT_METHODS_OPERATION_SUCCESS, numMethods);
		} else if (numMethods < 0) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_REVALIDATE_AOT_METHODS_OPERATION_FAILURE);
		} else {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_NO_AOT_METHOD_FOUND);
		}
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}

	if (RESULT_DO_FIND_AOT_METHODS_EQUALS == parseResult) {
		IDATA numMethods = 0;
		*nonfatal = 0;

		numMethods = j9shr_aotMethodOperation(vm, vm->sharedCacheAPI->methodSpecs, SHR_FIND_AOT_METHOTHODS);
		if (numMethods > 0) {
			if (verboseFlags > 0) {
				j9tty_printf(PORTLIB, "\n");
			}
			SHRINIT_TRACE1_NOTAG(verboseFlags, J9NLS_SHRC_SHRINIT_FIND_AOT_METHODS_OPERATION_SUCCESS, numMethods);
		} else if (numMethods < 0) {
			SHRINIT_ERR_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_FIND_AOT_METHODS_OPERATION_FAILURE);
		} else {
			SHRINIT_TRACE(verboseFlags, J9NLS_SHRC_SHRINIT_NO_AOT_METHOD_FOUND);
		}
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}

	vm->sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE;

	if (RESULT_DO_SNAPSHOTCACHE == parseResult) {
		*nonfatal = 0;
		if (0 == j9shr_createCacheSnapshot(vm, cacheName)) {
			SHRINIT_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_SUCCESS_CREATE_SNAPSHOT, cacheName);
		} else {
			SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_FAILURE_CREATE_SNAPSHOT, cacheName);
		}
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}

	if ((RESULT_DO_ADJUST_SOFTMX_EQUALS == parseResult)
		|| (RESULT_DO_ADJUST_MINAOT_EQUALS == parseResult)
		|| (RESULT_DO_ADJUST_MAXAOT_EQUALS == parseResult)
		|| (RESULT_DO_ADJUST_MINJITDATA_EQUALS == parseResult)
		|| (RESULT_DO_ADJUST_MAXJITDATA_EQUALS == parseResult)
	) {
		/* There is no need to check the return value of tryAdjustMinMaxSizes(). JVM will always exit and the corresponding NLS message
 		 * will be printed out inside tryAdjustMinMaxSizes() no matter whether the softmx/minAOT/maxAOT/minJIT/maxJIT has been adjusted as requested */
		cm->tryAdjustMinMaxSizes(currentThread);
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}
	
	if (RESULT_DO_RESIZE_CACHE_EQUALS == parseResult) {
		if (J9PORT_SHR_CACHE_TYPE_PERSISTENT == getCacheTypeFromRuntimeFlags(runtimeFlags)) {
			j9shr_resizeCache(currentThread);
		}
		returnVal = J9VMDLLMAIN_SILENT_EXIT_VM;
	}
	return returnVal;

_error:
	/* This needs to be done before freeing vm->sharedClassConfig */
	if ((doPrintStats) && (-2 == rcStartup)) {
		if (verboseFlags != 0) {
			reportUtilityNotApplicable(vm, ctrlDirName, cacheName, verboseFlags, runtimeFlags, parseResult);
		}
	}
	if (vm->sharedClassConfig) {
		if (*nonfatal) {
			/* Jazz 108033: Destroy the partially initialized share config immediately to void
			 * any potential issue in the later VM initialization.
			 */
			vm->sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_DO_DESTROY_CONFIG;
			j9shr_sharedClassesFinishInitialization(vm);
			Trc_SHR_INIT_j9shr_init_ExitOnNonFatal(currentThread);
		} else {
			if (vm->sharedClassConfig->sharedAPIObject != NULL) {
				j9mem_free_memory(vm->sharedClassConfig->sharedAPIObject);
			}
			j9mem_free_memory(vm->sharedClassConfig);
			vm->sharedClassConfig = NULL;
		}
	}
	if (((doPrintStats) && (-2 == rcStartup))
		||(RESULT_DO_RESTORE_FROM_SNAPSHOT == parseResult)
	) {
		return J9VMDLLMAIN_SILENT_EXIT_VM;
	}
	return J9VMDLLMAIN_FAILED;
}

/**
 * JVM Late Initialisation for shared classes
 *
 * This function is called at the last stage of JVM initialisation before classes are loaded
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [out] nonfatal  Set to 1 if "nonfatal" is specified as a shared classes command line suboption
 *
 * @return  Indication of whether the JVM start up should continue
 * @arg J9VMDLLMAIN_FAILED  JVM start up should fail
 * @arg J9VMDLLMAIN_OK  JVM start up should continue
 */
IDATA
j9shr_lateInit(J9JavaVM *vm, UDATA* nonfatal)
{
	IDATA returnVal = J9VMDLLMAIN_FAILED;
	PORT_ACCESS_FROM_PORT(vm->portLibrary);

	if (vm->sharedClassConfig) {
		U_64 rFlags = vm->sharedClassConfig->runtimeFlags;

		*nonfatal = (rFlags & J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL) != 0;
		if (rFlags & J9SHR_RUNTIMEFLAG_DO_DESTROY_CONFIG) {
			j9mem_free_memory(vm->sharedClassConfig);
			vm->sharedClassConfig = NULL;
			returnVal = J9VMDLLMAIN_FAILED;
		} else {
			/* If bytecode agent has hooked, try to detect this early on... this is also tested on each class load */
			testForBytecodeModification(vm);
			returnVal = J9VMDLLMAIN_OK;
		}
	}

	return returnVal;
}

/**
 * Print statistics for a shared classes cache
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] parseResult  The result of having parsed command line suboptions for shared classes
 * @arg RESULT_DO_PRINTALLSTATS  Print ROM classes and classpaths for the cache
 * @arg RESULT_DO_PRINTORPHANSTATS  Print orphan class details in addition to that printed by RESULT_DO_PRINTALLSTATS
 *      (meant for development only, not a publicised option)
 *
 * @return  0 for success or -1 for error
 */
IDATA
j9shr_print_stats(J9JavaVM *vm, UDATA parseResult, U_64 runtimeFlags, UDATA printStatsOptions)
{
	UDATA showFlags = 0;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

	if ((RESULT_DO_PRINTALLSTATS == parseResult) ||
		(RESULT_DO_PRINTALLSTATS_EQUALS == parseResult) ||
		(RESULT_DO_PRINTORPHANSTATS == parseResult) ||
		(RESULT_DO_PRINTSTATS_EQUALS == parseResult) ||
		(RESULT_DO_PRINTSTATS == parseResult) ||
		(RESULT_DO_PRINT_TOP_LAYER_STATS == parseResult) ||
		(RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS == parseResult)
	) {
		showFlags = printStatsOptions;
	}
	return ((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->printCacheStats(currentThread, showFlags, runtimeFlags);
}

/**
 * Print file name that will be used for cache.
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] cacheName user specified cache name (possibly null)
 *
 */
static void
j9shr_print_cache_filename(J9JavaVM* vm, const char* cacheDirName, U_64 runtimeFlags, const char* cacheName, I_8 layer)
{
	char cacheNameWithVGen[J9SH_MAXPATH];
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9PortShcVersion versionData;

	memset(cacheNameWithVGen, 0, J9SH_MAXPATH);

	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);

	versionData.cacheType = getCacheTypeFromRuntimeFlags(runtimeFlags);

	SH_OSCache::getCacheVersionAndGen(
			PORTLIB,
			vm,
			cacheNameWithVGen,
			J9SH_MAXPATH,
			cacheName,
			&versionData,
			SH_OSCache::getCurrentCacheGen(),
			true, layer);

	j9tty_printf(PORTLIB, "%s%s\n", cacheDirName, cacheNameWithVGen);
	return;
}

/**
 * Get the softmx bytes if it is set, or the total usable cache size if softmx is not set.
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 *
 * @return The softmx bytes or the total usable cache size.
 */
UDATA
j9shr_getCacheSizeBytes(J9JavaVM *vm)
{
	U_32 ret = 0;

	j9shr_getMinMaxBytes(vm, &ret, NULL, NULL, NULL, NULL);
	if ((U_32)-1 == ret) {
		ret = (U_32)j9shr_getTotalUsableCacheBytes(vm);
	}

	return ret;
}

/**
 * Determine the type of shared class cache that is enabled. Either the current default (Bootstrap Classes Only), 
 * or user defined shared cache, enabled via "-Xshareclasses" on the command line. 
 * 
 * @param [in] vm Pointer to the VM structure for the JVM
 *
 * @return J9SharedClassCacheMode enum that indicates the Shared Class Cache that is in effect
 * 
 */
J9SharedClassCacheMode
j9shr_getSharedClassCacheMode(J9JavaVM *vm)
{
	J9SharedClassCacheMode ret = J9SharedClassCacheBootstrapOnly;
	/* Only bootstrap classes are shared by default */
	if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_CACHEBOOTCLASSES)) {
		if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES)) {
			ret = J9SharedClassCacheUserDefined;
		} else {
			ret = J9SharedClassCacheBootstrapOnly;
		}
	}
	return ret;
}

/**
 * Get the total usable shared classes cache size in bytes
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 *
 * @return The total usable cache size in bytes
 */
UDATA
j9shr_getTotalUsableCacheBytes(J9JavaVM *vm)
{
	return ((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->getCompositeCacheAPI()->getTotalUsableCacheSize();
}

/**
 * Get the shared classes softmx in bytes
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [out] vm softmx value in bytes
 * @param [out] The minAOT value in bytes
 * @param [out] The maxAOT value in bytes
 * @param [out] The minJIT value in bytes
 * @param [out] The maxJIT value in bytes
 */
void
j9shr_getMinMaxBytes(J9JavaVM *vm, U_32 *softmx, I_32 *minAOT, I_32 *maxAOT, I_32 *minJIT, I_32 *maxJIT)
{
	((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->getCompositeCacheAPI()->getMinMaxBytes(softmx, minAOT, maxAOT, minJIT, maxJIT);
}

/**
 * set the shared classes softmx, minAOT, maxAOT, minJIT, maxJIT in bytes
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] The softmx value in bytes
 * @param [in] The minAOT value in bytes
 * @param [in] The maxAOT value in bytes
 * @param [in] The minJIT value in bytes
 * @param [in] The maxJIT value in bytes
 *
 * @return I_32	J9SHR_SOFTMX_ADJUSTED is set if softmx has been adjusted
 *				J9SHR_MIN_AOT_ADJUSTED is set if minAOT has been adjusted
 *				J9SHR_MAX_AOT_ADJUSTED is set if maxAOT has been adjusted
 *				J9SHR_MIN_JIT_ADJUSTED is set if minJIT has been adjusted
 *				J9SHR_MAX_JIT_ADJUSTED is set if maxJIT has been adjusted
 */
I_32
j9shr_setMinMaxBytes(J9JavaVM *vm, U_32 softmx, I_32 minAOT, I_32 maxAOT, I_32 minJIT, I_32 maxJIT)
{
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

	if ((U_32)-1 != softmx) {
		vm->sharedClassConfig->softMaxBytes = softmx;
	}
	if (minAOT > 0) {
		vm->sharedClassConfig->minAOT = minAOT;
	}
	if (maxAOT > 0) {
		vm->sharedClassConfig->maxAOT = maxAOT;
	}
	if (minJIT > 0) {
		vm->sharedClassConfig->minJIT = minJIT;
	}
	if (maxJIT > 0) {
		vm->sharedClassConfig->maxJIT = maxJIT;
	}

	return ((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->getCompositeCacheAPI()->tryAdjustMinMaxSizes(currentThread, TRUE);
}

/**
 * This function is used by JIT to increase the unstored aot and jit bytes.
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] aotBytes Unstored AOT bytes.
 * @param [in] aotBytes Unstored JIT bytes.
 */
void
j9shr_increaseUnstoredBytes(J9JavaVM *vm, U_32 aotBytes, U_32 jitBytes)
{
	((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->increaseUnstoredBytes(0, aotBytes, jitBytes);
}

/**
 * Get the unstored bytes
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 *
 * @param [out] softmxUnstoredBytes bytes that are not stored due to the setting of softmx
 * @param [out] maxAOTUnstoredBytes AOT bytes that are not stored due to the setting of maxAOT
 * @param [out] maxJITUnstoredBytes JIT bytes that are not stored due to the setting of maxJIT
 */
void
j9shr_getUnstoredBytes(J9JavaVM *vm, U_32 *softmxUnstoredBytes, U_32 *maxAOTUnstoredBytes, U_32 *maxJITUnstoredBytes)
{
	((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->getUnstoredBytes(softmxUnstoredBytes, maxAOTUnstoredBytes, maxJITUnstoredBytes);
}

/**
 * Get the shared classes cache free available space size in bytes (softmx - usedbytes)
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 *
 * @return The total cache free available space size in bytes
 */
UDATA
j9shr_getFreeAvailableSpaceBytes(J9JavaVM *vm)
{
	return ((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->getCompositeCacheAPI()->getFreeAvailableBytes();
}

static void
freeClasspathItemsForPool(J9JavaVM* vm, J9Pool* pool, UDATA alsoFreeCpEntries)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (pool) {
		pool_state aState;
		struct J9GenericByID* anElement;

		anElement = (struct J9GenericByID*)pool_startDo(pool, &aState);
		while (anElement) {
			if (anElement->cpData) {
				j9shr_freeClasspathData(vm, anElement->cpData);
			}
			/* For Tokens and URLs, jclData is allocated from pools, but for Classpaths, it is allocated from portlib */
			if (alsoFreeCpEntries && anElement->jclData) {
				j9mem_free_memory(anElement->jclData);
			}
			anElement = (struct J9GenericByID*)pool_nextDo(&aState);
		}
	}
}

/**
 * Perform JVM exit processing for shared classes
 *
 * This function is called whether the JVM is shutdown as a result of an exit or free, that is,
 * it is called for all shutdown circumstances
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 */
void
j9shr_guaranteed_exit(J9JavaVM *vm, BOOLEAN exitForDebug)
{
	/* Insert code which MUST run on shutdown */
	if (vm && vm->sharedClassConfig && vm->sharedClassConfig->sharedClassCache) {

		if (exitForDebug == TRUE) {
			PORT_ACCESS_FROM_JAVAVM(vm);
			j9tty_printf(PORTLIB, "CALL :\t j9shr_guaranteed_exit\n");
			SHRINIT_INFO_TRACE(vm->sharedClassConfig->verboseFlags, J9NLS_SHRC_SHRINIT_SHUTDOWN_NONDEBUG_CACHE);

			/* If debugging the cache may be shutdown early if it is not a debug enabled cache.
			 * In this case we make sure all the hooks registered by j9shr_init are disabled.
			 */
			J9HookInterface** hook = vm->internalVMFunctions->getVMHookInterface(vm);
			(*hook)->J9HookUnregister(hook, J9HOOK_VM_FIND_LOCALLY_DEFINED_CLASS, hookFindSharedClass, NULL);
			J9HookInterface **shcHooks;
			shcHooks = zip_getVMZipCachePoolHookInterface((J9ZipCachePool *)vm->zipCachePool);
			(*shcHooks)->J9HookUnregister(shcHooks, J9HOOK_VM_ZIP_LOAD, j9shr_hookZipLoadEvent, NULL);

			/* If the cache is being shutdown early the shared string table should also be disabled.
			 */
			if (vm->sharedInvariantInternTable != NULL) {
				if (vm->sharedInvariantInternTable->sharedInvariantSRPHashtable != NULL) {
					srpHashTableFree(vm->sharedInvariantInternTable->sharedInvariantSRPHashtable);
				}
				j9mem_free_memory(vm->sharedInvariantInternTable);
				vm->sharedInvariantInternTable = NULL;
			}
		}

		/*Perform the shutdown*/
		((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->runExitCode(vm->mainThread);
	}
}

/**
 * JVM shutdown processing for shared classes
 *
 * This function is only called when the JVM is shut down as the result of an exit call, not a free.
 * (See @ref j9shr_guaranteed_exit which is called in both cases)
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 */
void
j9shr_shutdown(J9JavaVM *vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (vm->sharedCacheAPI != NULL)
	{
		if (vm->sharedCacheAPI->cacheName != NULL) {
			j9mem_free_memory(vm->sharedCacheAPI->cacheName);
		}
		if (vm->sharedCacheAPI->ctrlDirName != NULL) {
			j9mem_free_memory(vm->sharedCacheAPI->ctrlDirName);
		}
		if (vm->sharedCacheAPI->modContext != NULL) {
			j9mem_free_memory(vm->sharedCacheAPI->modContext);
		}
		if (vm->sharedCacheAPI->expireTime != NULL) {
			j9mem_free_memory(vm->sharedCacheAPI->expireTime);
		}
		if (NULL != vm->sharedCacheAPI->methodSpecs) {
			j9mem_free_memory(vm->sharedCacheAPI->methodSpecs);
		}
		j9mem_free_memory(vm->sharedCacheAPI);
	}
	if (vm->sharedInvariantInternTable != NULL) {
		if (vm->sharedInvariantInternTable->sharedInvariantSRPHashtable != NULL) {
			srpHashTableFree(vm->sharedInvariantInternTable->sharedInvariantSRPHashtable);
			vm->sharedInvariantInternTable->sharedInvariantSRPHashtable = NULL;
		}
		j9mem_free_memory(vm->sharedInvariantInternTable);
		vm->sharedInvariantInternTable = NULL;
	}
	if (vm->sharedClassConfig) {
		J9SharedClassConfig* config = vm->sharedClassConfig;
		struct J9Pool* cpCachePool = config->jclClasspathCache;
		struct J9Pool* tokenCachePool = config->jclTokenCache;
		struct J9Pool* urlCachePool = config->jclURLCache;
		struct J9Pool* j9ClassPathEntryPool = config->jclJ9ClassPathEntryPool;
		struct J9Pool* classnameFilterPool = config->classnameFilterPool;
		J9SharedStringFarm* jclStringFarm = config->jclStringFarm;
		J9HashTable* urlHashTable = config->jclURLHashTable;
		J9HashTable* utfHashTable = config->jclUTF8HashTable;
		J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

		/* Free all of the cached ClasspathItems */

		freeClasspathItemsForPool(vm, cpCachePool, TRUE);
		freeClasspathItemsForPool(vm, tokenCachePool, FALSE);
		freeClasspathItemsForPool(vm, urlCachePool, FALSE);
		j9mem_free_memory(config->bootstrapCPI);

		/* Clean up and free the cache */

		((SH_CacheMap*)config->sharedClassCache)->cleanup(currentThread);
		if (config->configMonitor) {
			omrthread_monitor_destroy(config->configMonitor);
		}
		if (config->jclCacheMutex) {
			omrthread_monitor_destroy(config->jclCacheMutex);
		}
		j9mem_free_memory(config->sharedAPIObject);
		j9mem_free_memory(config);

		/* Kill the pools */

		if (cpCachePool) {
			pool_kill(cpCachePool);
		}
		if (tokenCachePool) {
			pool_kill(tokenCachePool);
		}
		if (urlCachePool) {
			pool_kill(urlCachePool);
		}
		if (j9ClassPathEntryPool) {
			pool_kill(j9ClassPathEntryPool);
		}
		if (classnameFilterPool) {
			freeStoreFilterPool(vm, classnameFilterPool);
		}
		if (urlHashTable) {
			hashTableFree(urlHashTable);
		}
		if (utfHashTable) {
			hashTableFree(utfHashTable);
		}

		/* Kill the string farm */

		if (jclStringFarm) {
			killStringFarm(vm->portLibrary, jclStringFarm);
		}
	}
}

IDATA
j9shr_sharedClassesFinishInitialization(J9JavaVM *vm)
{
	IDATA rc = J9VMDLLMAIN_OK;
	UDATA nonfatal = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if ((rc = j9shr_lateInit(vm, &nonfatal)) != J9VMDLLMAIN_OK) {
		if (nonfatal) {
			SHRINIT_TRACE(vm->sharedCacheAPI->verboseFlags, J9NLS_SHRC_SHRINIT_FAILED_NONFATAL_PRESENT);
			return J9VMDLLMAIN_OK;
		} else {
			return rc;
		}
	}

	return rc;
}

/* TODO:@hangshao Rename this method to j9shr_isCacheBlockSpaceFull() */
BOOLEAN
j9shr_isCacheFull(J9JavaVM *vm)
{
	BOOLEAN retval = FALSE;

	if (vm->sharedClassConfig) {
		SH_CacheMap* cm = (SH_CacheMap *)(vm->sharedClassConfig->sharedClassCache);
		J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

		cm->updateRuntimeFullFlags(currentThread);
		if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL)) {
			retval = TRUE;
		}
	}
	return retval;
}

/**
 * This function checks whether the memory segment;
 *  which starts at the given address and with the given length,
 *  is in the range of any cache in the cacheDescriptorList.
 *
 * If it is in the range of any one of the caches in the cacheDescriptorList,
 * then this functions returns true,
 * otherwise it returns false.
 *
 * @param vm  Currently running VM.
 * @param address Beginning of the memory segment to be checked whether in any cache boundaries.
 * @param Length of the memory segment.
 * @return TRUE if memory segment is in any cache, FALSE otherwise.
 */
BOOLEAN
j9shr_isAddressInCache(J9JavaVM *vm, void *address, UDATA length, BOOLEAN checkReadWriteCacheOnly)
{
	BOOLEAN retval = FALSE;

	if (NULL != vm->sharedClassConfig) {
		SH_CacheMap* cm = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
		retval = cm->isAddressInCache(address, length, true, (TRUE == checkReadWriteCacheOnly));
	}

	return retval;
}


/**
 * This method returns the default cache type depending on the platform.
 */
BOOLEAN
j9shr_isPlatformDefaultPersistent(struct J9JavaVM* vm)
{
	/* ZOS and z/TPF do not support persistent cache.
	 * Rest of the platforms use persistent cache by default. */
#if !(defined(J9ZOS390) || defined(J9ZTPF))
	return TRUE;
#else
	return FALSE;
#endif
}

U_64
getDefaultRuntimeFlags(void)
{
	return (
			J9SHR_RUNTIMEFLAG_ENABLE_LOCAL_CACHEING |
			J9SHR_RUNTIMEFLAG_ENABLE_TIMESTAMP_CHECKS |
			J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION |
			J9SHR_RUNTIMEFLAG_ENABLE_SHAREANONYMOUSCLASSES |
			J9SHR_RUNTIMEFLAG_ENABLE_SHAREUNSAFECLASSES |			
			J9SHR_RUNTIMEFLAG_ENABLE_ROUND_TO_PAGE_SIZE |
#if defined (J9SHR_MSYNC_SUPPORT)
			J9SHR_RUNTIMEFLAG_ENABLE_MSYNC |
#endif
			J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT |
#if !defined(J9ZOS390) && !defined(AIXPPC)
			/* Calling mprotect on the RW when using persistent caches on AIX and ZOS
			 *  has a severe performance impact on class loading (approx ~20% slower)
			 */
			J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW |
			/* Protecting partially filled pages is expected to have performance impact
			 * on AIX and ZOS. It should be enabled on need basis.
			 */
			J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES |
#endif
			J9SHR_RUNTIMEFLAG_ENABLE_CACHEBOOTCLASSES |
			J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES |
			J9SHR_RUNTIMEFLAG_ENABLE_BYTECODEFIX |
			J9SHR_RUNTIMEFLAG_ENABLE_AOT |
			J9SHR_RUNTIMEFLAG_ENABLE_JITDATA |
			J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID |
			J9SHR_RUNTIMEFLAG_DETECT_NETWORK_CACHE |
			J9SHR_RUNTIMEFLAG_ENABLE_SEMAPHORE_CHECK
	);
}

void
j9shr_populatePreinitConfigDefaults(J9JavaVM *vm, J9SharedClassPreinitConfig *updatedWithDefaults)
{
	J9SharedInvariantInternTable* table = vm->sharedInvariantInternTable;
	J9SharedClassJavacoreDataDescriptor descriptor;
	if (0 == ((SH_CacheMap*)(vm->sharedClassConfig->sharedClassCache))->getJavacoreData(vm, &descriptor)) {
		/*This should never happen. But if it does zero all the sizes in the structure.*/
		memset(updatedWithDefaults,0,sizeof(J9SharedClassPreinitConfig));
		return;
	}

	IDATA cacheSizeNoRW = descriptor.cacheSize - (descriptor.readWriteBytes + descriptor.debugAreaSize);

	updatedWithDefaults->sharedClassCacheSize  = (UDATA)descriptor.totalSize;
	updatedWithDefaults->sharedClassSoftMaxBytes  = (IDATA)descriptor.softMaxBytes;
	updatedWithDefaults->sharedClassMinAOTSize = (descriptor.minAOT==-1)?0:descriptor.minAOT;
	updatedWithDefaults->sharedClassMaxAOTSize = (descriptor.maxAOT==-1)?cacheSizeNoRW:descriptor.maxAOT;
	updatedWithDefaults->sharedClassMinJITSize = (descriptor.minJIT==-1)?0:descriptor.minJIT;
	updatedWithDefaults->sharedClassMaxJITSize = (descriptor.maxJIT==-1)?cacheSizeNoRW:descriptor.maxJIT;
	updatedWithDefaults->sharedClassReadWriteBytes = descriptor.readWriteBytes;
	updatedWithDefaults->sharedClassDebugAreaBytes = descriptor.debugAreaSize;
	updatedWithDefaults->sharedClassInternTableNodeCount = 0;

	if (NULL != table) {
		updatedWithDefaults->sharedClassInternTableNodeCount = (UDATA)srpHashTable_tableSize(table->sharedInvariantSRPHashtable);
	}
}

/**
 * Parses memory sizes (e.g. 512K) passed to Xshareclasses suboptions
 *
 * @param char * str 	The string to be parsed and converted to a UDATA
 * @param UDATA & value The result of parsing 'str' successfully
 *
 * @return BOOLEAN		Return TRUE if parsing str succeeded, and value is
 * 						will not overflow a UDATA.
 */
static BOOLEAN
j9shr_parseMemSize(char * str, UDATA & value) {
	UDATA oldValue = value;

	if (0 != scan_udata(&str,&value)) {
		/*MALFORMED UDATA*/
		return FALSE;
	}

	switch (*str) {
		case '\0':
				oldValue = value;
				value = OMR::align(value, sizeof(UDATA));		/* round to nearest pointer value */
				if (value < oldValue) {
					/*OVERFLOW*/
					return FALSE;
				}
				break;
		case 'k':
		case 'K':
			if (value <= (((UDATA)-1) >> 10)) {
				value <<= 10;
			} else {
				/*OVERFLOW*/
				return FALSE;
			}
			str++;
			break;
		case 'm':
		case 'M':
			if (value <= (((UDATA)-1) >> 20)) {
				value <<= 20;
			} else {
				/*OVERFLOW*/
				return FALSE;
			}
			str++;
			break;
		case 'g':
		case 'G':
			if (value <= (((UDATA)-1) >> 30)) {
				value <<= 30;
			} else {
				/*OVERFLOW*/
				return FALSE;
			}
			str++;
			break;
		default:
			/*MALFORMED UNITS*/
			return FALSE;
	}
	return TRUE;
}

void
j9shr_freeClasspathData(J9JavaVM *vm, void *cpData) {
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (NULL != cpData) {
		ClasspathItem *cpi = (ClasspathItem*)cpData;
		cpi->cleanup();
		j9mem_free_memory(cpi);
	}
}

/**
 * Add a dummy JIT hint to the cache.
 * Used for testing via -XXshareclasses:addTestJitHints is specified. Must be run without JIT.
 * @param [in] hookInterface  Pointer to pointer to the hook interface structure
 * @param [in] eventNum  Not used
 * @param [in,out] voidData  Pointer to a J9VMFindLocalClassEvent struct
 *
 * @param [in] userData  Not used
 */
static void
addTestJitHint(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData) {

	J9VMFindLocalClassEvent* eventData = (J9VMFindLocalClassEvent*)voidData;
	J9JavaVM* vm = eventData->currentThread->javaVM;

	PORT_ACCESS_FROM_JAVAVM(vm);

	/* find the class in the cache */
	hookFindSharedClass(hookInterface, eventNum, voidData, userData);

	J9ROMClass * romclass = eventData->result;
	if (NULL == romclass) {
		j9file_printf(PORTLIB, J9PORT_TTY_OUT, "addTestJitHint class %.*s not in the cache\n",
				eventData->classNameLength, eventData->className);
		return;
	}

	if (0 == romclass->romMethodCount) {
		return;
	}
	J9UTF8* romclassName = J9ROMCLASS_CLASSNAME(romclass);
	J9ROMMethod *romMethod = J9ROMCLASS_ROMMETHODS(romclass);
	if (NULL != romMethod) {
		J9UTF8 *methodName = J9ROMMETHOD_NAME(romMethod);

		j9file_printf(PORTLIB, J9PORT_TTY_OUT, "addTestJitHint adding hint to %.*s.%.*s\n",
				J9UTF8_LENGTH(romclassName), J9UTF8_DATA(romclassName), J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName));
		J9SharedDataDescriptor newHint;
		U_8 hintData[] = {0xDE, 0xAD, 0xBE, 0xEF};

		newHint.address = hintData;
		newHint.length = sizeof(hintData);
		newHint.type = J9SHR_ATTACHED_DATA_TYPE_JITHINT;
		newHint.flags = J9SHR_ATTACHED_DATA_NO_FLAGS;

		J9SharedClassConfig* config = vm->sharedClassConfig;
		config->storeAttachedData(eventData->currentThread, romMethod, &newHint, false);
	}


} /* addTestJitHint */

/**
 * This function create a snapshot of a non-persistent cache
 *
 * @param[in] vm The current J9JavaVM
 * @param[in] cacheName A pointer to the name of the non-persistent cache
 *
 * @return 0 on success and -1 on failure
 */
IDATA
j9shr_createCacheSnapshot(J9JavaVM* vm, const char* cacheName)
{
	IDATA rc = 0;
#if !defined(WIN32)
	PORT_ACCESS_FROM_JAVAVM(vm);
	char cacheDirName[J9SH_MAXPATH];
	UDATA verboseFlags = vm->sharedCacheAPI->verboseFlags;
	I_8 layer = vm->sharedClassConfig->layer;

	Trc_SHR_INIT_j9shr_createCacheSnapshot_Entry(cacheName);

	if (-1 == SH_OSCache::getCacheDir(vm, vm->sharedClassConfig->ctrlDirName, cacheDirName, J9SH_MAXPATH, J9PORT_SHR_CACHE_TYPE_SNAPSHOT)) {
		Trc_SHR_INIT_j9shr_createCacheSnapshot_getCacheDirFailed();
		/* NLS message has been printed out inside SH_OSCache::getCacheDir() if verbose flag is not 0 */
		rc = -1;
	} else {
		IDATA fd = 0;
		J9PortShcVersion versionData;
		char nameWithVGen[CACHE_ROOT_MAXLEN];	/* CACHE_ROOT_MAXLEN defined to be 88 */
		char pathFileName[J9SH_MAXPATH];		/* J9SH_MAXPATH defined to be EsMaxPath which is 1024 */
		I_32 mode = (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS)) ? J9SH_CACHE_FILE_MODE_DEFAULTDIR_WITH_GROUPACCESS : J9SH_CACHE_FILE_MODE_DEFAULTDIR_WITHOUT_GROUPACCESS;

		setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
		versionData.cacheType = J9PORT_SHR_CACHE_TYPE_SNAPSHOT;
		SH_OSCache::getCacheVersionAndGen(PORTLIB, vm, nameWithVGen, CACHE_ROOT_MAXLEN, cacheName, &versionData, OSCACHE_CURRENT_CACHE_GEN, false, layer);
		/* No check for the return value of getCachePathName() as it always returns 0 */
		SH_OSCache::getCachePathName(PORTLIB, cacheDirName, pathFileName, J9SH_MAXPATH, nameWithVGen);

		fd = j9file_open(pathFileName, EsOpenCreate | EsOpenWrite, mode);
		if (-1 == fd) {
			I_32 errorno = j9error_last_error_number();
			const char * errormsg = j9error_last_error_message();

			Trc_SHR_INIT_j9shr_createCacheSnapshot_fileOpenFailed(pathFileName);
			SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
			Trc_SHR_Assert_True(errormsg != NULL);
			SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
			SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_ERROR_SNAPSHOT_FILE_OPEN, pathFileName);
			rc = -1;
		} else {
			J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
			SH_CacheMap* cm = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
			SH_CompositeCacheImpl *cc = (SH_CompositeCacheImpl *)cm->getCompositeCacheAPI();
			J9SharedCacheHeader* theca = cc->getCacheHeaderAddress();
			UDATA cacheSize = cc->getTotalSize();
			UDATA headerSize = SH_OSCachesysv::getHeaderSize();
			OSCachesysv_header_version_current* headerStart = (OSCachesysv_header_version_current*)((UDATA)theca - headerSize);
			UDATA ignore = 0;
			I_32 lockRc1 = -1;
			I_32 lockRc2 = -1;
			I_32 lockRc3 = -1;
			UDATA readWriteBytes = cc->getReadWriteBytes();

			/* Verify if the group access has been set */
			if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS)) {
				LastErrorInfo lastErrorInfo;
				I_32 groupAccessRc = SH_OSCacheFile::verifyCacheFileGroupAccess(PORTLIB, fd, &lastErrorInfo);

				if (0 == groupAccessRc) {
					Trc_SHR_INIT_j9shr_createCacheSnapshot_setGroupAccessFailed(currentThread, pathFileName);
					SHRINIT_WARNING_TRACE(verboseFlags, J9NLS_SHRC_ERROR_SNAPSHOT_SET_GROUPACCESS_FAILED);
				} else if (-1 == groupAccessRc) {
					/* Failed to get stats of the snapshot file */
					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileStatFailed(currentThread, pathFileName);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, lastErrorInfo.lastErrorCode);
					Trc_SHR_Assert_True(lastErrorInfo.lastErrorMsg != NULL);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, lastErrorInfo.lastErrorMsg);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_OSCACHE_ERROR_FILE_STAT, pathFileName);
					rc = -1;
					goto done;
				}
			}

			lockRc1 = j9file_lock_bytes(fd, J9PORT_FILE_WRITE_LOCK | J9PORT_FILE_WAIT_FOR_LOCK, 0, cacheSize);

			if (lockRc1 < 0) {
				I_32 errorno = j9error_last_error_number();
				const char * errormsg = j9error_last_error_message();

				Trc_SHR_INIT_j9shr_createCacheSnapshot_fileLockFailed(currentThread, pathFileName);
				SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
				Trc_SHR_Assert_True(errormsg != NULL);
				SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
				SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_ERROR_SNAPSHOT_FILE_LOCK, pathFileName);
				rc = -1;
				goto done;
			}
			lockRc2 = cc->enterWriteMutex(currentThread, false, "j9shr_createCacheSnapshot");

			if (readWriteBytes > 0) {
				/* readWriteBytes can be 0, in such case, do not need to call enterReadWriteAreaMutex() */
				lockRc3 = cc->enterReadWriteAreaMutex(currentThread, false, &ignore, &ignore);
			}

			if ((lockRc2 < 0)
				|| ((lockRc3 < 0) && (readWriteBytes > 0))
			) {
				Trc_SHR_INIT_j9shr_createCacheSnapshot_enterMutexFailed(currentThread);
				SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_ENTER_MUTEX, cacheName);
				rc = -1;
			} else {
				UDATA nbytes = cacheSize;
				U_32 theCacheInitComplete = 0;
				IDATA cacheInitCompleteOffset = SH_OSCachesysv::getSysvHeaderFieldOffsetForGen(OSCACHE_CURRENT_CACHE_GEN, OSCACHE_HEADER_FIELD_CACHE_INIT_COMPLETE);
				IDATA fileRc = 0;
				I_64 fileSeekOffset = 0;
				I_64 fileSize = j9file_flength(fd);

				/* Check existence of the snapshot file in the file lock */
				if (fileSize > 0) {
					/* Snapshot file already exists. When fileSize > nbytes, writing nbytes into the snapshot file is not able to
					 * overwrite everything in the file, so truncate the file first.
					 */
					if (j9file_set_length(fd, 0) < 0) {
						I_32 errorno = j9error_last_error_number();
						const char * errormsg = j9error_last_error_message();

						Trc_SHR_INIT_j9shr_createCacheSnapshot_fileSetLengthFailed(currentThread, pathFileName);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
						Trc_SHR_Assert_True(errormsg != NULL);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_TRUNCATE, pathFileName);
						rc = -1;
						goto done;
					} else {
						SHRINIT_WARNING_TRACE2(verboseFlags, J9NLS_SHRC_SHRINIT_OVERWRITE_EXISTING_SNAPSHOT_WARNING, cacheName, pathFileName);
					}
				}

				fileRc = j9file_write(fd, (void*)headerStart, nbytes);
				if (fileRc < 0) {
					I_32 errorno = j9error_last_error_number();
					const char * errormsg = j9error_last_error_message();

					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed1(currentThread, pathFileName);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
					Trc_SHR_Assert_True(errormsg != NULL);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
					rc = -1;
					goto done;
				} else if (nbytes != (UDATA)fileRc) {
					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed2(currentThread, pathFileName, nbytes, fileRc);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
					rc = -1;
					goto done;
				}
				fileSeekOffset = j9file_seek(fd, cacheInitCompleteOffset, EsSeekSet);

				if (cacheInitCompleteOffset != fileSeekOffset) {
					I_32 errorno = j9error_last_error_number();
					const char * errormsg = j9error_last_error_message();

					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
					Trc_SHR_Assert_True(errormsg != NULL);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileSeekFailed(currentThread, pathFileName);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_SEEK, pathFileName);
					rc = -1;
					goto done;
				}
				nbytes = sizeof(theCacheInitComplete);
				/* Ensure oscHdr.cacheInitComplete is always 0 in the snapshot file */
				fileRc = j9file_write(fd, &theCacheInitComplete, nbytes);
				if (fileRc < 0) {
					I_32 errorno = j9error_last_error_number();
					const char * errormsg = j9error_last_error_message();

					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed1(currentThread, pathFileName);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
					Trc_SHR_Assert_True(errormsg != NULL);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
					rc = -1;
					goto done;
				} else if (nbytes != (UDATA)fileRc) {
					Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed2(currentThread, pathFileName, nbytes, fileRc);
					SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
					rc = -1;
					goto done;
				}

				if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID)) {
					/* J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID is for testing only, change the buildID in the snapshot */
					U_64 theBadBuildID = 0;
					IDATA buildIDOffset =  SH_OSCachesysv::getSysvHeaderFieldOffsetForGen(OSCACHE_CURRENT_CACHE_GEN, OSCACHE_HEADER_FIELD_BUILDID);

					fileSeekOffset = j9file_seek(fd, buildIDOffset, EsSeekSet);
					if (buildIDOffset != fileSeekOffset) {
						I_32 errorno = j9error_last_error_number();
						const char * errormsg = j9error_last_error_message();

						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
						Trc_SHR_Assert_True(errormsg != NULL);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
						Trc_SHR_INIT_j9shr_createCacheSnapshot_fileSeekFailed(currentThread, pathFileName);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_SEEK, pathFileName);
						rc = -1;
						goto done;
					}
					nbytes = sizeof(theBadBuildID);
					fileRc = j9file_write(fd, &theBadBuildID, nbytes);
					if (fileRc < 0) {
						I_32 errorno = j9error_last_error_number();
						const char * errormsg = j9error_last_error_message();

						Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed1(currentThread, pathFileName);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
						Trc_SHR_Assert_True(errormsg != NULL);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
						rc = -1;
					} else if (nbytes != (UDATA)fileRc) {
						Trc_SHR_INIT_j9shr_createCacheSnapshot_fileWriteFailed2(currentThread, pathFileName, nbytes, fileRc);
						SHRINIT_ERR_TRACE1(verboseFlags, J9NLS_SHRC_SHRINIT_ERROR_SNAPSHOT_FILE_WRITE, pathFileName);
						rc = -1;
					}
				}
			}
done:
			if (0 == lockRc3) {
				cc->exitReadWriteAreaMutex(currentThread, J9SHR_STRING_POOL_OK);
			}
			if (0 == lockRc2) {
				cc->exitWriteMutex(currentThread, "j9shr_createCacheSnapshot");
			}
			if (rc < 0) {
				/* delete the shared cache snapshot file in the file lock */
				j9file_unlink(pathFileName);
			}
			/* file lock will be released when closed */
			j9file_close(fd);
		}
	}
	Trc_SHR_INIT_j9shr_createCacheSnapshot_Exit(cacheName, rc);
#endif /* !defined(WIN32) */
	return rc;
}

/**
 * This function restore a non-persistent cache from a snapshot file.
 *
 * @param[in] vm The current J9JavaVM
 * @param[in] ctrlDirName The snapshot file directory
 * @param[in] cacheName The name of the cache
 * @param[in, out] cacheExist True if the cache to be restored already exits, false otherwise
 *
 * @return 0 on success and -1 on failure
 */
static IDATA
j9shr_restoreFromSnapshot(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, bool* cacheExist)
{
	IDATA rc = 0;
#if !defined(WIN32)
	PORT_ACCESS_FROM_JAVAVM(vm);
	UDATA verboseFlags = vm->sharedCacheAPI->verboseFlags;
	SH_CacheMap* cm = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	SH_CompositeCacheImpl* cc = (SH_CompositeCacheImpl *)cm->getCompositeCacheAPI();

	Trc_SHR_INIT_j9shr_restoreFromSnapshot_Entry(cacheName);

	/* ignore "readOnly" option if "restoreFromSnapshot" presents */
	if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_READONLY)) {
		vm->sharedClassConfig->runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_READONLY;
		SHRINIT_WARNING_TRACE3(verboseFlags, J9NLS_SHRC_SHRINIT_OPTION_IGNORED_WARNING, OPTION_READONLY, OPTION_RESTORE_FROM_SNAPSHOT, OPTION_READONLY);
	}

	rc = cc->restoreFromSnapshot(vm, cacheName, cacheExist);

	Trc_SHR_INIT_j9shr_restoreFromSnapshot_Exit(cacheName, rc);
#endif /* !defined(WIN32) */
	return rc;
}

/**
 * Print file name that will be used for the cache snapshot.
 *
 * @param [in] vm Pointer to the VM structure for the JVM
 * @param [in] cacheDirName The name of the cache directory
 * @param [in] snapshotName User specified snapshot name (possibly null)
 */
static void
j9shr_print_snapshot_filename(J9JavaVM* vm, const char* cacheDirName, const char* snapshotName, I_8 layer)
{
	char snapshotNameWithVGen[J9SH_MAXPATH];
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9PortShcVersion versionData;

	memset(snapshotNameWithVGen, 0, J9SH_MAXPATH);
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	versionData.cacheType = J9PORT_SHR_CACHE_TYPE_SNAPSHOT;

	SH_OSCache::getCacheVersionAndGen(
			PORTLIB,
			vm,
			snapshotNameWithVGen,
			J9SH_MAXPATH,
			snapshotName,
			&versionData,
			SH_OSCache::getCurrentCacheGen(),
			true, layer);

	j9tty_printf(PORTLIB, "%s%s\n", cacheDirName, snapshotNameWithVGen);
	return;
}

/**
 * Find compiled method in shared classes cache.
 *
 * @param[in] currentThread  The current VM thread
 * @param[in] romMethod  A pointer to the J9ROMMethod to find compiled code for
 * @param[out] flags Lowest bit set to J9SHR_AOT_METHOD_FLAG_INVALIDATED if the compiled method has been invalidated.
 *
 * @return A pointer to the start of compiled data for the J9ROMMethod
 * @return NULL if non-stale data cannot be found for the J9ROMMethod
 */
const U_8*
j9shr_findCompiledMethodEx1(J9VMThread* currentThread, const J9ROMMethod* romMethod, UDATA* flags)
{
	J9JavaVM* vm = currentThread->javaVM;
	J9SharedClassConfig* sharedClassConfig = vm->sharedClassConfig;
	UDATA oldState = (UDATA)-1;
	UDATA* currentState = &(currentThread->omrVMThread->vmState);
	const U_8* returnVal = 0;

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_SHR_INIT_findCompiledMethod_entry(currentThread);

	if (NULL == sharedClassConfig) {
		Trc_SHR_INIT_findCompiledMethod_exit_Noop(currentThread);
		return NULL;
	}

	U_64 localRuntimeFlags = sharedClassConfig->runtimeFlags;
	UDATA localVerboseFlags = sharedClassConfig->verboseFlags;

	if (J9_ARE_NO_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE) ||
		J9_ARE_ALL_BITS_SET(localRuntimeFlags, J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS)) {
		Trc_SHR_INIT_findCompiledMethod_exit_Noop(currentThread);
		return NULL;
	}

	if (J9VMSTATE_SHAREDAOT_FIND != *currentState) {
		oldState = *currentState;
		*currentState = J9VMSTATE_SHAREDAOT_FIND;
	}

	returnVal = (U_8*)(((SH_CacheMap*)(sharedClassConfig->sharedClassCache))->findCompiledMethod(currentThread, romMethod, flags));

	if (J9_ARE_ALL_BITS_SET(localVerboseFlags, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_AOT)) {
		if (returnVal) {
			SHRINIT_TRACE1_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FOUND_VERBOSE_AOT_MSG, romMethod);
		} else {
			SHRINIT_TRACE1_NOTAG(localVerboseFlags, J9NLS_SHRC_SHRINIT_FIND_FAILED_VERBOSE_AOT_MSG, romMethod);
		}
	}

	if ((UDATA)-1 != oldState) {
		*currentState = oldState;
	}

	Trc_SHR_INIT_findCompiledMethod_exit(currentThread, returnVal);

	return returnVal;
}

/**
 * This function will find the AOT method(s) that match the passed in method specification(s) and perform the required operation
 * @param[in] vm The current J9JavaVM
 * @param[in] string The pointer of the method specifications passed in by "invalidateAotMethods/revalidateAotMethods/findAotMethods=" option
 * @param[in] action SHR_FIND_AOT_METHOTHODS when listing the specified methods
 * 					 SHR_INVALIDATE_AOT_METHOTHODS when invalidating the specified methods
 * 					 SHR_REVALIDATE_AOT_METHOTHODS when revalidating the specified methods
 *
 * @return the number of AOT methods invalidated/revalidated/found on success or -1 on failure
 */
static IDATA
j9shr_aotMethodOperation(J9JavaVM* vm, char* methodSpecs, UDATA action)
{
	IDATA rc = 0;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

	Trc_SHR_INIT_j9shr_aotMethodOperation_Entry(currentThread, action);

	rc = ((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->aotMethodOperation(currentThread, methodSpecs, action);

	Trc_SHR_INIT_j9shr_aotMethodOperation_Exit(currentThread, rc);

	return rc;
}

/**
 * This function will be called if the value passed to the command line option (-Xscmx/-XX:SharedCacheHardLimit=) setting the shared cache size
 * is greater than SHMMAX. It will reduce the cache size to SHMMAX. If necessary, debugRegionSize will also be adjusted in proportion according
 * to its original ratio to the -Xscmx value.
 *
 * @param[in] portlib J9 Port Library
 * @param[in] verboseFlags Flag to control whether to print out NLS messages in stderr/stdout/
 * @param[in] piconfig Pointer to a configuration structure
 * @param[in] newSize The new value to be used as cache size
 * @param[in] usingDefaultSize Whether the default cache size is being used.
 */
static void
adjustCacheSizes(J9PortLibrary* portlib, UDATA verboseFlags, J9SharedClassPreinitConfig* piconfig, U_64 newSize)
{
	UDATA cacheSize = piconfig->sharedClassCacheSize;
	double ratio = (double)newSize/cacheSize;
	bool printMessage = J9_ARE_ALL_BITS_SET(verboseFlags, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE);
	PORT_ACCESS_FROM_PORT(portlib);

	if (piconfig->sharedClassDebugAreaBytes > 0) {
		piconfig->sharedClassDebugAreaBytes = SHC_PAD((IDATA)(piconfig->sharedClassDebugAreaBytes*ratio), SHC_WORDALIGN);
	}

	piconfig->sharedClassCacheSize = (UDATA)newSize;
	SHRINIT_WARNING_TRACE2(printMessage, J9NLS_SHRC_SHRINIT_SHARED_MEMORY_SIZE_CHANGED, cacheSize, (IDATA)newSize);

}

/**
 * This function is called when there is change in VM phase allowing shared class cache
 * to take any action based on change in phase.
 *
 * @param [in] currentThread pointer to current J9VMThread
 * @param [in] phase VM phase
 *
 * @return void
 */
void
j9shr_jvmPhaseChange(J9VMThread *currentThread, UDATA phase)
{
	if (J9VM_PHASE_NOT_STARTUP == phase) {
		J9JavaVM* vm = currentThread->javaVM;

		/* OpenJ9 issue; https://github.com/eclipse-openj9/openj9/issues/3743
		 * GC decides whether to calls vm->sharedClassConfig->storeGCHints() to store the GC hints into the shared cache. */
		storeStartupHintsToSharedCache(currentThread);
		if (J9_ARE_NO_BITS_SET(vm->sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP)) {
			((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->protectPartiallyFilledPages(currentThread);
		}
		((SH_CacheMap*)vm->sharedClassConfig->sharedClassCache)->dontNeedMetadata(currentThread);
	}
	return;
}

/**
 * This function test for the existence of a shared cache
 * @param[in] vm The current J9JavaVM
 * @param[in] ctrlDirName The control directory
 * @param[in] cacheDirName The cache directory
 * @param[in] cacheName The name of the cache
 * @param[in] versionData Cache version data
 * @param[in] cacheType The type of shared cache
 *
 * @return 1 if the cache exist, 0 if the cache does not exist, and -1 if failed to get cache directory.
 */
static IDATA
checkIfCacheExists(J9JavaVM* vm, const char* ctrlDirName, char* cacheDirName, const char* cacheName, J9PortShcVersion* versionData, U_32 cacheType, I_8 layer)
{
	IDATA ret = -1;
	if (-1 == SH_OSCache::getCacheDir(vm, ctrlDirName, cacheDirName, J9SH_MAXPATH, cacheType)) {
		/* NLS message has been printed out inside SH_OSCache::getCacheDir() if verbose flag is not 0 */
	} else {
		setCurrentCacheVersion(vm, J2SE_VERSION(vm), versionData);
		versionData->cacheType = cacheType;
		ret = j9shr_stat_cache(vm, cacheDirName, vm->sharedCacheAPI->verboseFlags, cacheName, versionData, OSCACHE_CURRENT_CACHE_GEN, layer);
	}
	return ret;
}

/**
 * This function checks if a class is from a patched module
 * @param[in] vmThread The current VM thread
 * @param[in] j9module Pointer to J9Module
 * @param[in] className The name of the class
 * @param[in] classNameLength The number of bytes in className
 * @param[in] classLoader Pointer to the classLoader
 *
 * @return true if the class is from a patched module, false otherwise.
 */
static bool
isClassFromPatchedModule(J9VMThread* vmThread, J9Module *j9module, U_8* className, UDATA classNameLength, J9ClassLoader* classLoader)
{
	bool ret = false;
	J9Module *module = j9module;
	J9JavaVM* vm = vmThread->javaVM;
	J9InternalVMFunctions const * const vmFuncs = vm->internalVMFunctions;

	if (J9_ARE_NO_BITS_SET(vm->jclFlags, J9_JCL_FLAG_JDK_MODULE_PATCH_PROP)) {
		return ret;
	}

	if (NULL != module) {
		if (NULL != classLoader->moduleExtraInfoHashTable) {
			J9ModuleExtraInfo *moduleInfo = NULL;

			omrthread_monitor_enter(vm->classLoaderModuleAndLocationMutex);
			moduleInfo = vmFuncs->findModuleInfoForModule(vmThread, classLoader, module);
			omrthread_monitor_exit(vm->classLoaderModuleAndLocationMutex);

			if ((NULL != moduleInfo) && (NULL != moduleInfo->patchPathEntries) && (moduleInfo->patchPathCount > 0)) {
#if UT_TRACE_OVERHEAD >= 1
				if (TrcEnabled_Trc_SHR_INIT_isClassFromPatchedModule_ClassFromPatchedModule_Event) {
					J9UTF8* url = getModuleJRTURL(vmThread, classLoader, module);
					Trc_SHR_INIT_isClassFromPatchedModule_ClassFromPatchedModule_Event(vmThread, classNameLength, className, (UDATA)J9UTF8_LENGTH(url), (const char*)J9UTF8_DATA(url));
				}
#endif /* UT_TRACE_OVERHEAD >= 1 */
				ret = true;

			}
		}
	}

	return ret;
}

/**
 * This function returns which module a class belongs to
 * @param[in] vmThread The current VM thread
 * @param[in] className The name of the class
 * @param[in] classNameLength The number of bytes in className
 * @param[in] classLoader Pointer to the classLoader
 *
 * @return The resolved module from which the class belongs to, NULL otherwise.
 */
static J9Module*
getModule(J9VMThread* vmThread, U_8* className, UDATA classNameLength, J9ClassLoader* classLoader)
{
	J9Module *module = NULL;
	J9JavaVM* vm = vmThread->javaVM;
	J9InternalVMFunctions const * const vmFuncs = vm->internalVMFunctions;

	if (J9_ARE_NO_BITS_SET(vm->runtimeFlags, J9_RUNTIME_JAVA_BASE_MODULE_CREATED)) {
		module = vm->javaBaseModule;
	} else {
		char* packageEnd = strnrchrHelper((const char*)className, '/', classNameLength);

		if (NULL != packageEnd) {
			omrthread_monitor_enter(vm->classLoaderModuleAndLocationMutex);
			module = vmFuncs->findModuleForPackage(vmThread, classLoader, className, (U_32)((U_8 *)packageEnd - className));
			omrthread_monitor_exit(vm->classLoaderModuleAndLocationMutex);
		}
	}
	return module;
}


/**
 * This function returns if the system is running low on free disk space
 * @param[in] vm The current J9JavaVM
 * @param[out] maxsize The maximum shared cache size allowed if the free disk space is low.
 *
 * @return False if the space free disk space >= SHRINIT_LOW_DISK_SIZE. True otherwise.
 */
static bool
isFreeDiskSpaceLow(J9JavaVM *vm, U_64* maxsize, U_64 runtimeFlags)
{
	char cacheDirName[J9SH_MAXPATH];
	memset(cacheDirName, 0, J9SH_MAXPATH);
	bool ret = true;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (J9_ARE_ANY_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_NO_PERSISTENT_DISK_SPACE_CHECK)) {
		ret = false;
		goto done;
	}

	if (-1 == j9shr_getCacheDir(vm, vm->sharedCacheAPI->ctrlDirName, cacheDirName, J9SH_MAXPATH, J9PORT_SHR_CACHE_TYPE_PERSISTENT)) {
		/* use j9shr_getCacheDir() instead of SH_OSCache::getCacheDir() to avoid duplicated NLS message if SH_OSCache::getCacheDir() failed */
		Trc_SHR_INIT_isFreeDiskSpaceLow_getDirFailed();
		goto done;
	}
	/* check for free disk space */
	J9FileStatFilesystem fileStatFilesystem;
	if (0 == j9file_stat_filesystem(cacheDirName, 0, &fileStatFilesystem)) {
		if (fileStatFilesystem.freeSizeBytes >= SHRINIT_LOW_FREE_DISK_SIZE) {
			ret = false;
		} else {
			Trc_SHR_INIT_isFreeDiskSpaceLow_DiskSpaceLow(fileStatFilesystem.freeSizeBytes);
		}
	} else {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();
		Trc_SHR_INIT_isFreeDiskSpaceLow_StatFileSystemFailed(cacheDirName, errorno, errormsg);
	}
done:
	if (ret) {
		*maxsize = J9_SHARED_CLASS_CACHE_DEFAULT_SOFTMAX_SIZE_64BIT_PLATFORM;
		Trc_SHR_INIT_isFreeDiskSpaceLow_SetMaxSize(*maxsize);
	}
	return ret;
}

/**
 * This function generates a key for the GC hints. The key is a string of all the commandline arguments passed to the current JVM
 * @param[in] vm The current J9JavaVM
 *
 * @return A string of all the commandline arguments, NULL if an error occurs.
 */
static char*
generateStartupHintsKey(J9JavaVM* vm)
{
	JavaVMInitArgs *actualArgs = vm->vmArgsArray->actualVMArgs;
	UDATA keyLength = 0;
	UDATA extraLen = 0;
	UDATA i = 0;
	UDATA nOptions = vm->vmArgsArray->nOptions;
	char* key = NULL;
	bool firstOption = true;
	PORT_ACCESS_FROM_JAVAVM(vm);

	for (i = 0; i < nOptions; ++i) {
		char* option = actualArgs->options[i].optionString;
		if ((NULL != option && strlen(option) > 0)
			&& (NULL == strstr(option,"-Dsun.java.launcher.pid="))
		) {
			keyLength += strlen(actualArgs->options[i].optionString);
			extraLen += 1;
		}
	}
	if (keyLength == 0) {
		goto done;
	}
	keyLength += extraLen; /* space between options and terminating null char */

	key = (char*)j9mem_allocate_memory(keyLength, J9MEM_CATEGORY_VM);
	if (NULL == key) {
		goto done;
	}
	memset(key, 0, keyLength);
	for (i = 0; i < nOptions; ++i) {
		char* option = actualArgs->options[i].optionString;
		if ((NULL != option && strlen(option) > 0)
			&& (NULL == strstr(option,"sun.java.launcher.pid"))
		) {
			if (firstOption) {
				firstOption = false;
				j9str_printf(PORTLIB, key, keyLength, "%s%s", key, option);
			} else {
				j9str_printf(PORTLIB, key, keyLength, "%s%s%s", key, " ", option);
			}
		}
	}
done:
	return key;
}


/**
 * This function fetches the startup hints from the shared cache and store it locally to vm->sharedClassConfig->localStartupHints.
 * @param[in] currentThread  The current VM thread
 *
 */
static void
fetchStartupHintsFromSharedCache(J9VMThread* currentThread)
{
	J9JavaVM* vm = currentThread->javaVM;

	if (NULL != vm->sharedClassConfig) {
		char *key = NULL;
		if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->localStartupHints.localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_FETCHED)) {
			/* Already fetched before, directly return */
			return;
		}
		key = generateStartupHintsKey(vm);
		if (NULL != key) {
			J9SharedDataDescriptor dataDescriptor = {0};
			PORT_ACCESS_FROM_JAVAVM(vm);
			if (0 < j9shr_findSharedData(currentThread, key, strlen(key), J9SHR_DATA_TYPE_STARTUP_HINTS, 0, &dataDescriptor, NULL)) {
				Trc_SHR_Assert_True(J9SHR_DATA_TYPE_STARTUP_HINTS == dataDescriptor.type);
				Trc_SHR_Assert_True(sizeof(J9SharedStartupHintsDataDescriptor) == dataDescriptor.length);
				memcpy(&vm->sharedClassConfig->localStartupHints.hintsData, dataDescriptor.address, sizeof(J9SharedStartupHintsDataDescriptor));
				vm->sharedClassConfig->localStartupHints.localStartupHintFlags |= J9SHR_LOCAL_STARTUPHINTS_FLAG_FETCHED;
				Trc_SHR_INIT_fetchStartupHintsFromSharedCache_Hints_Found(currentThread, vm->sharedClassConfig->localStartupHints.hintsData.flags,
						vm->sharedClassConfig->localStartupHints.hintsData.heapSize1, vm->sharedClassConfig->localStartupHints.hintsData.heapSize2);
			} else {
				Trc_SHR_INIT_fetchStartupHintsFromSharedCache_Hints_Not_Found(currentThread);
			}
			j9mem_free_memory(key);
		} else {
			Trc_SHR_INIT_fetchStartupHintsFromSharedCache_Null_key(currentThread);
		}
	}
}

/**
 * This function store the local startup hints vm->sharedClassConfig->localStartupHints to the shared cache
 * @param[in] currentThread  The current VM thread
 * @return  The location of the cached data or null
 *
 */
const U_8*
storeStartupHintsToSharedCache(J9VMThread* currentThread)
{
	J9JavaVM* vm = currentThread->javaVM;
	const U_8 *ret = NULL;
	if (J9_ARE_ANY_BITS_SET(vm->sharedClassConfig->localStartupHints.localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_WRITE_HINTS)) {
		J9SharedDataDescriptor dataDescriptor = {0};
		UDATA flag = J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE;
		char* key = generateStartupHintsKey(vm);

		if (NULL != key) {
			PORT_ACCESS_FROM_JAVAVM(vm);
			dataDescriptor.address = (U_8*)&vm->sharedClassConfig->localStartupHints.hintsData;
			dataDescriptor.type = J9SHR_DATA_TYPE_STARTUP_HINTS;
			dataDescriptor.length = sizeof(J9SharedStartupHintsDataDescriptor);
			if (J9_ARE_ANY_BITS_SET(vm->sharedClassConfig->localStartupHints.localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_OVERWRITE_HINTS)) {
				flag = J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE_OVERWRITE;
			}
			dataDescriptor.flags = flag;
			Trc_SHR_INIT_storeStartupHintsToSharedCache_Set_Flags(currentThread, flag);
			ret = j9shr_storeSharedData(currentThread, key, strlen(key), &dataDescriptor);
			if (NULL == ret) {
				Trc_SHR_INIT_storeStartupHintsToSharedCache_Store_Failed(currentThread);
			} else {
				Trc_SHR_INIT_storeStartupHintsToSharedCache_Store_Successful(currentThread, vm->sharedClassConfig->localStartupHints.hintsData.flags,
						vm->sharedClassConfig->localStartupHints.hintsData.heapSize1, vm->sharedClassConfig->localStartupHints.hintsData.heapSize2);
			}
			j9mem_free_memory(key);
		} else {
			Trc_SHR_INIT_storeStartupHintsToSharedCache_Null_key(currentThread);
		}
	} else {
		Trc_SHR_INIT_storeStartupHintsToSharedCache_Store_Nothing(currentThread);
	}
	return ret;
}

/**
 * Stores the GC hints into vm->sharedClassConfig->localStartupHints.hintsData. This function is not thread safe.
 * @param[in] vmThread  The current thread
 * @param[in] heapSize1  The first heap size param that is to be stored into the shared cache
 * @param[in] heapSize2  The second heap size param that is to be stored into the shared cache
 * @param[in] forceReplace TRUE Replace the existing GC hints under the same key (the same command line arguments) if there is one already in the shared cache.
 * 							FALSE Do not replace existing GC hints under the same key.
 */
void
j9shr_storeGCHints(J9VMThread* currentThread, UDATA heapSize1, UDATA heapSize2, BOOLEAN forceReplace)
{
	J9JavaVM* vm = currentThread->javaVM;
	bool heapSizesSet = J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->localStartupHints.hintsData.flags, J9SHR_STARTUPHINTS_HEAPSIZES_SET);

	if (forceReplace || !heapSizesSet) {
		vm->sharedClassConfig->localStartupHints.hintsData.heapSize1 = heapSize1;
		vm->sharedClassConfig->localStartupHints.hintsData.heapSize2 = heapSize2;
		vm->sharedClassConfig->localStartupHints.hintsData.flags |= J9SHR_STARTUPHINTS_HEAPSIZES_SET;
		if (forceReplace) {
			vm->sharedClassConfig->localStartupHints.localStartupHintFlags |= J9SHR_LOCAL_STARTUPHINTS_FLAG_OVERWRITE_HEAPSIZES;
			Trc_SHR_INIT_j9shr_storeGCHints_Overwrite_LocalHints(currentThread, heapSize1, heapSize2);
		} else {
			vm->sharedClassConfig->localStartupHints.localStartupHintFlags |= J9SHR_LOCAL_STARTUPHINTS_FLAG_STORE_HEAPSIZES;
			Trc_SHR_INIT_j9shr_storeGCHints_Write_To_LocalHints(currentThread, heapSize1, heapSize2);
		}
	}
}


/**
 * Find the GC hints from the shared classes cache
 * @param[in] vmThread  The current thread
 * @param[out] heapSize1  The first heap size that has been previously stored
 * @param[out] heapSize2  The second heap size that has been previously stored
 *
 * @return 0 on success, -1 otherwise.
 */
IDATA
j9shr_findGCHints(J9VMThread* currentThread, UDATA *heapSize1, UDATA *heapSize2)
{
	IDATA returnVal = -1;
	J9JavaVM* vm = currentThread->javaVM;

	fetchStartupHintsFromSharedCache(currentThread);
	if (J9_ARE_ALL_BITS_SET(vm->sharedClassConfig->localStartupHints.hintsData.flags, J9SHR_STARTUPHINTS_HEAPSIZES_SET)) {
		if (NULL != heapSize1) {
			*heapSize1 = vm->sharedClassConfig->localStartupHints.hintsData.heapSize1;
		}
		if (NULL != heapSize2) {
			*heapSize2 = vm->sharedClassConfig->localStartupHints.hintsData.heapSize2;
		}
		Trc_SHR_INIT_j9shr_findGCHints_Event_Found(currentThread, vm->sharedClassConfig->localStartupHints.hintsData.heapSize1,
				vm->sharedClassConfig->localStartupHints.hintsData.heapSize2);
		returnVal = 0;
	}
	return returnVal;
}

/**
 * Determine the directory to use for the cache file or control file(s)
 *
 * @param [in] vm  A J9JavaVM
 * @param [in] ctrlDirName  The control dir name
 * @param [out] buffer  The buffer to write the result into
 * @param [in] bufferSize  The size of the buffer in bytes
 * @param [in] cacheType  The Type of cache
 *
 * @return 0 on success or -1 for failure
 */
IDATA
j9shr_getCacheDir(J9JavaVM* vm, const char* ctrlDirName, char* buffer, UDATA bufferSize, U_32 cacheType)
{
	return SH_OSCache::getCacheDir(vm, ctrlDirName, buffer, bufferSize, cacheType, false);
}

/**
 * Determine existing top layer number
 *
 * @param [in] vm  The Java VM
 * @param [in] ctrlDirName  The control dir name
 * @param [in] cacheName  The cache name
 * @param [in] runtimeFlags  The runtime flags
 * @param [out] maxLayerNo  The existing top layer number
 */
static void
findExistingCacheLayerNumbers(J9JavaVM* vm, const char* ctrlDirName, const char* cacheName, U_64 runtimeFlags, I_8 *maxLayerNo)
{
	I_8 maxLayer = -1;

	J9PortShcVersion versionData;
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	versionData.cacheType = getCacheTypeFromRuntimeFlags(runtimeFlags);

	for (I_8 layer = 0; layer <= J9SH_LAYER_NUM_MAX_VALUE; layer++) {
		if (1 == j9shr_stat_cache(vm, ctrlDirName, 0 , cacheName, &versionData, OSCACHE_CURRENT_CACHE_GEN, layer)) {
			if (layer > maxLayer) {
				maxLayer = layer;
			}
		}
	}

	if (-1 != maxLayer) {
		*maxLayerNo = maxLayer;
	}
	return;
}

static void 
j9shr_resizeCache(J9VMThread* currentThread)
{
	J9JavaVM* vm = currentThread->javaVM;
	U_32 newSize = vm->sharedCacheAPI->newCacheSize;
	SH_CacheMap* cm = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;

	if ((U_32)-1 != newSize) {
		cm->resizeCache(currentThread, newSize);
	}
}

} /* extern "C" */

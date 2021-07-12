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

#if !defined(SHRINIT_H_INCLUDED)
#define SHRINIT_H_INCLUDED

/* @ddr_namespace: default */
#include "sharedconsts.h"
#include "SCAbstractAPI.h"

UDATA j9shr_storeAttachedData(J9VMThread* currentThread, const void* addressInCache, const J9SharedDataDescriptor* data, UDATA forceReplace);
const U_8* j9shr_findAttachedData(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset);
UDATA j9shr_updateAttachedData(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, const J9SharedDataDescriptor* data);
UDATA j9shr_updateAttachedUDATA(J9VMThread* currentThread, const void* addressInCache, UDATA type, I_32 updateAtOffset, UDATA value);
void j9shr_freeAttachedDataDescriptor(J9VMThread* currentThread, J9SharedDataDescriptor* data);
const U_8* j9shr_storeCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, const U_8* dataStart, UDATA dataSize, const U_8* codeStart, UDATA codeSize, UDATA forceReplace);
UDATA j9shr_getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor);
IDATA j9shr_init(J9JavaVM *vm, UDATA loadFlags, UDATA* nonfatal);
IDATA j9shr_lateInit(J9JavaVM *vm, UDATA* nonfatal);
IDATA j9shr_sharedClassesFinishInitialization(J9JavaVM *vm);
void j9shr_guaranteed_exit(J9JavaVM *vm, BOOLEAN exitForDebug);
void j9shr_shutdown(J9JavaVM *vm);
IDATA j9shr_print_stats(J9JavaVM *vm, UDATA parseResult, U_64 runtimeFlags, UDATA printStatsOptions);
void j9shr_updateClasspathOpenState(J9JavaVM* vm, J9ClassPathEntry* classPathEntries, UDATA entryIndex, UDATA entryCount, BOOLEAN isOpen);

void hookFindSharedClass(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData);
void hookSerializeSharedCache(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData);
void hookStoreSharedClass(J9HookInterface** hookInterface, UDATA eventNum, void* voidData, void* userData);
UDATA j9shr_getCacheSizeBytes(J9JavaVM *vm);
J9SharedClassCacheMode j9shr_getSharedClassCacheMode(J9JavaVM *vm);
UDATA j9shr_getTotalUsableCacheBytes(J9JavaVM *vm);
void j9shr_getMinMaxBytes(J9JavaVM *vm, U_32 *softmx, I_32 *minAOT, I_32 *maxAOT, I_32 *minJIT, I_32 *maxJIT);
I_32 j9shr_setMinMaxBytes(J9JavaVM *vm, U_32 softmx, I_32 minAOT, I_32 maxAOT, I_32 minJIT, I_32 maxJIT);
void j9shr_increaseUnstoredBytes(J9JavaVM *vm, U_32 aotBytes, U_32 jitBytes);
void j9shr_getUnstoredBytes(J9JavaVM *vm, U_32 *softmxUnstoredBytes, U_32 *maxAOTUnstoredBytes, U_32 *maxJITUnstoredBytes);
UDATA j9shr_getFreeAvailableSpaceBytes(J9JavaVM *vm);
void j9shr_hookZipLoadEvent(J9HookInterface** hook, UDATA eventNum, void* eventData, void* userData);
void j9shr_resetSharedStringTable(J9JavaVM* vm);
BOOLEAN j9shr_isCacheFull(J9JavaVM *vm);
BOOLEAN j9shr_isAddressInCache(J9JavaVM *vm, void *address, UDATA length, BOOLEAN checkReadWriteCacheOnly);
void j9shr_populatePreinitConfigDefaults(J9JavaVM *vm, J9SharedClassPreinitConfig *updatedWithDefaults);
BOOLEAN j9shr_isPlatformDefaultPersistent(struct J9JavaVM* vm);
UDATA j9shr_isBCIEnabled(J9JavaVM *vm);
UDATA ensureCorrectCacheSizes(J9JavaVM *vm, J9PortLibrary* portlib, U_64 runtimeFlags, UDATA verboseFlags, J9SharedClassPreinitConfig* piconfig);
UDATA parseArgs(J9JavaVM* vm, char* options, U_64* runtimeFlags, UDATA* verboseFlags, char** cacheName, char** modContext, char** expireTime, char** ctrlDirName, 
		char** cacheDirPerm, char** methodSpecs, UDATA* printStatsOptions, UDATA* storageKeyTesting);
UDATA convertPermToDecimal(J9JavaVM *vm, const char *permStr);
SCAbstractAPI * initializeSharedAPI(J9JavaVM *vm);
U_64 getDefaultRuntimeFlags(void);
void j9shr_freeClasspathData(J9JavaVM *vm, void *cpData);
IDATA j9shr_createCacheSnapshot(J9JavaVM* vm, const char* cacheName);
const U_8* j9shr_findCompiledMethodEx1(J9VMThread* currentThread, const J9ROMMethod* romMethod, UDATA* flags);
void j9shr_jvmPhaseChange(J9VMThread* currentThread, UDATA phase);
void j9shr_storeGCHints(J9VMThread* currentThread, UDATA heapSize1, UDATA heapSize2, BOOLEAN forceReplace);
IDATA j9shr_findGCHints(J9VMThread* currentThread, UDATA *heapSize1, UDATA *heapSize2);
const U_8* storeStartupHintsToSharedCache(J9VMThread* currentThread);
IDATA j9shr_getCacheDir(J9JavaVM* vm, const char* ctrlDirName, char* buffer, UDATA bufferSize, U_32 cacheType);
U_32 getCacheTypeFromRuntimeFlags(U_64 runtimeFlags);

typedef struct J9SharedClassesHelpText {
	const char* option;
	U_32 nlsHelp1;
	U_32 nlsHelp2;
	U_32 nlsMoreHelp1;
	U_32 nlsMoreHelp2;
} J9SharedClassesHelpText;

typedef struct J9SharedClassesOptions {
	const char *option;
	U_8 parseType;
	U_8 action;
	U_64 flag;
} J9SharedClassesOptions;

#define OPTION_NO_TIMESTAMP_CHECKS "noTimestampChecks"
#define OPTION_NO_CLASSPATH_CACHEING "noClasspathCacheing"
#define OPTION_NO_REDUCE_STORE_CONTENTION "noReduceStoreContention"
#define OPTION_PRINTSTATS "printStats"
#define OPTION_PRINTSTATS_EQUALS "printStats="
#define OPTION_PRINTDETAILS "printDetails"
#define OPTION_PRINTALLSTATS "printAllStats"
#define OPTION_PRINTALLSTATS_EQUALS "printAllStats="
#define OPTION_PRINT_TOP_LAYER_STATS "printTopLayerStats"
#define OPTION_PRINT_TOP_LAYER_STATS_EQUALS "printTopLayerStats="
#define OPTION_PRINT_CACHENAME "printCacheFilename"
#define OPTION_NAME_EQUALS "name="
#define OPTION_DESTROY "destroy"
#define OPTION_DESTROYALL "destroyAll"
#define OPTION_DESTROYALLLAYERS "destroyAllLayers"
#define OPTION_EXPIRE_EQUALS "expire="
#define OPTION_LISTALLCACHES "listAllCaches"
#define OPTION_HELP "help"
#define OPTION_MORE_HELP "morehelp"		/* Just for dev options */
#define OPTION_VERBOSE "verbose"
#define OPTION_VERBOSE_IO "verboseIO"
#define OPTION_VERBOSE_HELPER "verboseHelper"
#define OPTION_VERBOSE_AOT "verboseAOT"
#define OPTION_VERBOSE_JITDATA "verboseJITData"
#define OPTION_MODIFIED_EQUALS "modified="
#define OPTION_GROUP_ACCESS "groupAccess"
#define OPTION_NO_BYTECODEFIX "noBytecodeFix"
#define OPTION_TRACECOUNT "traceCount"
#define OPTION_PRINTORPHANSTATS "printOrphanStats"
#define OPTION_NONFATAL "nonfatal"
#define OPTION_FATAL "fatal"
#define OPTION_SILENT "silent"
#define OPTION_NONE "none"
#define OPTION_CONTROLDIR_EQUALS "controlDir="		/* purely for java5 compatibility */
#define OPTION_NOAOT "noaot"
#define OPTION_PERSISTENT "persistent"
#define OPTION_NONPERSISTENT "nonpersistent"
#define OPTION_VERBOSE_DATA "verboseData"
#define OPTION_VERBOSE_INTERN "verboseIntern"
#define OPTION_VERBOSE_PAGES "verbosePages"
#define OPTION_NO_ROUND_PAGES "noRoundPages"
#define OPTION_CACHERETRANSFORMED "cacheRetransformed"
#define OPTION_NOBOOTCLASSPATH "noBootclasspath"
#define OPTION_BOOTCLASSESONLY "bootClassesOnly"
#if !defined(WIN32)
#define OPTION_SNAPSHOTCACHE "snapshotCache"
#define OPTION_DESTROYSNAPSHOT "destroySnapshot"
#define OPTION_DESTROYALLSNAPSHOTS "destroyAllSnapshots"
#define OPTION_RESTORE_FROM_SNAPSHOT "restoreFromSnapshot"
#define OPTION_PRINT_SNAPSHOTNAME "printSnapshotFilename"
#endif /* !defined(WIN32) */
#define OPTION_SINGLEJVM "singleJVM"		/* purely for java5 compatibility */
#define OPTION_KEEP "keep"					/* purely for java5 compatibility */
#define OPTION_MPROTECT_EQUALS "mprotect="
#define SUB_OPTION_MPROTECT_ALL "all"
#define SUB_OPTION_MPROTECT_ONFIND "onfind"
#define SUB_OPTION_MPROTECT_DEF "default"
#define SUB_OPTION_MPROTECT_PARTIAL_PAGES "partialpages"
#define SUB_OPTION_MPROTECT_PARTIAL_PAGES_ON_STARTUP "partialpagesonstartup"
#define SUB_OPTION_MPROTECT_NO_RW "norw"
#define SUB_OPTION_MPROTECT_NO_PARTIAL_PAGES "nopartialpages"
#define SUB_OPTION_MPROTECT_NONE "none"
#define OPTION_CACHEDIR_EQUALS "cacheDir="
#define OPTION_RESET "reset"
#define OPTION_READONLY "readonly"
#define OPTION_NO_AUTOPUNT "noAutoPunt"
#define OPTION_NO_DETECT_NETWORK_CACHE "noDetectNetworkCache"
#define OPTION_NO_COREMMAP "noCoreMmap"
#define OPTION_UTILITIES "utilities"
#define OPTION_YES "yes"
#define OPTION_VERIFY_TREE "verifyInternTree"
#define OPTION_TEST_BAD_BUILDID "testBadBuildID"
#if defined(AIXPPC)
#define OPTION_ENVVAR_COREMMAP "CORE_MMAP"
#endif
#define OPTION_NO_SEMAPHORE_CHECK "noSemaphoreCheck"
#define OPTION_CHECK_STRING_TABLE "checkStringTable"
#define OPTION_NO_JITDATA "nojitdata"
#define OPTION_FORCE_DUMP_IF_CORRUPT "forceDumpIfCorrupt"
#define OPTION_FAKE_CORRUPTION "testFakeCorruption"
#define OPTION_DO_EXTRA_AREA_CHECKS "doExtraAreaChecks"
#define OPTION_CREATE_OLD_GEN "createOldGen"
#define OPTION_DISABLE_CORRUPT_CACHE_DUMPS "disablecorruptcachedumps"
#define OPTION_CACHEDIRPERM_EQUALS "cacheDirPerm="
#define OPTION_CHECK_STRINGTABLE_RESET "checkStringTableReset"
#define OPTION_ENABLE_BCI "enableBCI"
#define OPTION_DISABLE_BCI "disableBCI"
#define OPTION_ADDTESTJITHINT "addTestJitHints"
#define OPTION_STORAGE_KEY_EQUALS "storageKey="
#define OPTION_RESTRICT_CLASSPATHS "restrictClasspaths"
#define OPTION_ALLOW_CLASSPATHS	"allowClasspaths"
#define OPTION_INVALIDATE_AOT_METHODS_EQUALS "invalidateAotMethods="
#define OPTION_REVALIDATE_AOT_METHODS_EQUALS "revalidateAotMethods="
#define OPTION_FIND_AOT_METHODS_EQUALS "findAotMethods="
#define OPTION_ADJUST_SOFTMX_EQUALS "adjustsoftmx="
#define OPTION_ADJUST_MINAOT_EQUALS "adjustminaot="
#define OPTION_ADJUST_MAXAOT_EQUALS "adjustmaxaot="
#define OPTION_ADJUST_MINJITDATA_EQUALS "adjustminjitdata="
#define OPTION_ADJUST_MAXJITDATA_EQUALS "adjustmaxjitdata="
#define OPTION_NO_URL_TIMESTAMP_CHECK "noCheckURLTimestamps"
#define OPTION_URL_TIMESTAMP_CHECK "checkURLTimestamps"
#define OPTION_LAYER_EQUALS "layer="
#define OPTION_CREATE_LAYER "createLayer"
#define OPTION_NO_PERSISTENT_DISK_SPACE_CHECK "noPersistentDiskSpaceCheck"
#define OPTION_RESIZE_EQUALS "resize="

/* public options for printallstats= and printstats=  */
#define SUB_OPTION_PRINTSTATS_ALL "all"
#define SUB_OPTION_PRINTSTATS_CLASSPATH "classpath"
#define SUB_OPTION_PRINTSTATS_URL "url"
#define SUB_OPTION_PRINTSTATS_TOKEN "token"
#define SUB_OPTION_PRINTSTATS_ROMCLASS "romclass"
#define SUB_OPTION_PRINTSTATS_ROMMETHOD "rommethod"
#define SUB_OPTION_PRINTSTATS_AOT "aot"
#define SUB_OPTION_PRINTSTATS_INVALIDATEDAOT "invalidatedaot"
#define SUB_OPTION_PRINTSTATS_JITPROFILE "jitprofile"
#define SUB_OPTION_PRINTSTATS_ZIPCACHE "zipcache"
#define SUB_OPTION_PRINTSTATS_JITHINT "jithint"
#define SUB_OPTION_PRINTSTATS_STALE "stale"
#define SUB_OPTION_PRINTSTATS_STARTUPHINT "startuphint"
/* private options for printallstats= and printstats= */
#define SUB_OPTION_PRINTSTATS_EXTRA "extra"
#define SUB_OPTION_PRINTSTATS_ORPHAN "orphan"
#define SUB_OPTION_PRINTSTATS_AOTCH "aotch"
#define SUB_OPTION_PRINTSTATS_AOTTHUNK "aotthunk"
#define SUB_OPTION_PRINTSTATS_AOTDATA "aotdata"
#define SUB_OPTION_PRINTSTATS_JCL "jcl"
#define SUB_OPTION_PRINTSTATS_BYTEDATA "bytedata"
#define SUB_OPTION_PRINTSTATS_HELP "help"
#define SUB_OPTION_PRINTSTATS_MOREHELP "morehelp"
#define SUB_OPTION_AOT_METHODS_OPERATION_HELP "help"

#define RESULT_PARSE_FAILED 1
#define RESULT_DO_PRINTSTATS 2
#define RESULT_DO_PRINTALLSTATS 3
#define RESULT_DO_HELP 4
#define RESULT_DO_MORE_HELP 5
#define RESULT_DO_DESTROY 6
#define RESULT_DO_DESTROYALL 7
#define RESULT_DO_EXPIRE 8
#define RESULT_DO_LISTALLCACHES 9
#define RESULT_DO_ADD_RUNTIMEFLAG 10
#define RESULT_DO_REMOVE_RUNTIMEFLAG 11
#define RESULT_DO_NAME_EQUALS 12
#define RESULT_DO_MODIFIED_EQUALS 13
#define RESULT_DO_PRINTDETAILS 14
#define RESULT_DO_PRINTORPHANSTATS 15
#define RESULT_DO_ADD_VERBOSEFLAG 16
#define RESULT_DO_SET_VERBOSEFLAG 17
#define RESULT_DO_CTRLD_EQUALS 18
#define RESULT_DO_MPROTECT_EQUALS 19
#define RESULT_DO_NOTHING 20
#define RESULT_DO_RESET 21
#define RESULT_DO_CACHEDIR_EQUALS 22
#define RESULT_NO_COREMMAP_SET 23
#define RESULT_DO_PRINT_CACHENAME 24
#define RESULT_DO_TEST_INTERNAVL 25
#define RESULT_DO_UTILITIES 26
#define RESULT_DO_CACHEDIRPERM_EQUALS 27
#define RESULT_DO_PRINTALLSTATS_EQUALS 28
#define RESULT_DO_PRINTSTATS_EQUALS 29
#define RESULT_DO_RAW_DATA_AREA_SIZE_EQUALS 32
#define RESULT_DO_ENABLE_BCI 33
#define RESULT_DO_DISABLE_BCI 34
#define RESULT_DO_ADD_STORAGE_KEY_EQUALS 35

#define RESULT_DO_SNAPSHOTCACHE 36
#define RESULT_DO_DESTROYSNAPSHOT 37
#define RESULT_DO_DESTROYALLSNAPSHOTS 38
#define RESULT_DO_RESTORE_FROM_SNAPSHOT 39
#define RESULT_DO_PRINT_SNAPSHOTNAME 40
#define RESULT_DO_INVALIDATE_AOT_METHODS_EQUALS 41
#define RESULT_DO_REVALIDATE_AOT_METHODS_EQUALS 42
#define RESULT_DO_FIND_AOT_METHODS_EQUALS 43
#define RESULT_DO_ADJUST_SOFTMX_EQUALS 44
#define RESULT_DO_ADJUST_MINAOT_EQUALS 45
#define RESULT_DO_ADJUST_MAXAOT_EQUALS 46
#define RESULT_DO_ADJUST_MINJITDATA_EQUALS 47
#define RESULT_DO_ADJUST_MAXJITDATA_EQUALS 48
#define RESULT_DO_BOOTCLASSESONLY 49
#define RESULT_DO_DESTROYALLLAYERS 50
#define RESULT_DO_LAYER_EQUALS 51
#define RESULT_DO_CREATE_LAYER 52
#define RESULT_DO_PRINT_TOP_LAYER_STATS 53
#define RESULT_DO_PRINT_TOP_LAYER_STATS_EQUALS 54
#define RESULT_DO_RESIZE_CACHE_EQUALS 55

#define PARSE_TYPE_EXACT 1
#define PARSE_TYPE_STARTSWITH 2
#define PARSE_TYPE_OPTIONAL 3

#define HELPTEXT_NAMEEQUALS_OPTION OPTION_NAME_EQUALS"<name>"
#define HELPTEXT_EXPIRE_OPTION 	OPTION_EXPIRE_EQUALS"<t>"
#define HELPTEXT_OPTION_RAW_DATA_AREA_SIZE_EQUALS OPTION_RAW_DATA_AREA_SIZE_EQUALS"<size>"
#define HELPTEXT_MODIFIEDEQUALS_OPTION OPTION_MODIFIED_EQUALS"<modContext>"
#define HELPTEXT_CACHEDIR_OPTION OPTION_CACHEDIR_EQUALS"<directory>"
#define HELPTEXT_CACHEDIRPERM_OPTION OPTION_CACHEDIRPERM_EQUALS"<permission>"
#if defined(J9ZOS390) || defined(AIXPPC)
#define HELPTEXT_MPROTECTEQUALS_PUBLIC_OPTION OPTION_MPROTECT_EQUALS "[" SUB_OPTION_MPROTECT_ALL "|" SUB_OPTION_MPROTECT_DEF "|" SUB_OPTION_MPROTECT_NONE "]"
#define HELPTEXT_MPROTECTEQUALS_PARTIAL_PAGES_PRIVATE_OPTION OPTION_MPROTECT_EQUALS "" SUB_OPTION_MPROTECT_PARTIAL_PAGES
#define HELPTEXT_MPROTECTEQUALS_PARTIAL_PAGES_ON_STARTUP_PRIVATE_OPTION OPTION_MPROTECT_EQUALS "" SUB_OPTION_MPROTECT_PARTIAL_PAGES_ON_STARTUP
#else
#define HELPTEXT_MPROTECTEQUALS_PUBLIC_OPTION OPTION_MPROTECT_EQUALS "[" SUB_OPTION_MPROTECT_ALL "|" SUB_OPTION_MPROTECT_ONFIND "|" SUB_OPTION_MPROTECT_PARTIAL_PAGES_ON_STARTUP "|" SUB_OPTION_MPROTECT_DEF "|" SUB_OPTION_MPROTECT_NO_PARTIAL_PAGES "|" SUB_OPTION_MPROTECT_NONE "]"
#define HELPTEXT_MPROTECTEQUALS_NO_RW_PRIVATE_OPTION OPTION_MPROTECT_EQUALS "" SUB_OPTION_MPROTECT_NO_RW
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
#define HELPTEXT_PRINTALLSTATS_OPTION OPTION_PRINTALLSTATS"[=option[+s]]"
#define HELPTEXT_PRINTSTATS_OPTION OPTION_PRINTSTATS"[=option[+s]]"
#define HELPTEXT_OPTION_PRINT_TOP_LAYER_STATS OPTION_PRINT_TOP_LAYER_STATS"[=option[+s]]"
#define HELPTEXT_STORAGE_KEY_EQUALS OPTION_STORAGE_KEY_EQUALS"<key>"
#define HELPTEXT_INVALIDATE_AOT_METHODS_OPTION OPTION_INVALIDATE_AOT_METHODS_EQUALS"help|{<method_specification>[,<method_specification>]}"
#define HELPTEXT_REVALIDATE_AOT_METHODS_OPTION OPTION_REVALIDATE_AOT_METHODS_EQUALS"help|{<method_specification>[,<method_specification>]}"
#define HELPTEXT_FIND_AOT_METHODS_OPTION OPTION_FIND_AOT_METHODS_EQUALS"help|{<method_specification>[,<method_specification>]}"
#define HELPTEXT_RESIZE_EQUALS OPTION_RESIZE_EQUALS"<size>"
#define HELPTEXT_ADJUST_SOFTMX_EQUALS OPTION_ADJUST_SOFTMX_EQUALS"<size>"
#define HELPTEXT_ADJUST_MINAOT_EQUALS OPTION_ADJUST_MINAOT_EQUALS"<size>"
#define HELPTEXT_ADJUST_MAXAOT_EQUALS OPTION_ADJUST_MAXAOT_EQUALS"<size>"
#define HELPTEXT_ADJUST_MINJITDATA_EQUALS OPTION_ADJUST_MINJITDATA_EQUALS"<size>"
#define HELPTEXT_ADJUST_MAXJITDATA_EQUALS OPTION_ADJUST_MAXJITDATA_EQUALS"<size>"
#define HELPTEXT_LAYER_EQUALS OPTION_LAYER_EQUALS "<number>"

#define HELPTEXT_NEWLINE {"", 0, 0, 0, 0}

#define SHRINIT_MAX_SHARED_STRING_TABLE_NODE_COUNT 15000
#define SHRINIT_MAX_LOCAL_STRING_TABLE_BYTES 102400
#define SHRINIT_LOCAL_STRING_TABLE_SIZE_DIVISOR 500 	/* 1/500 of the free space in the cache */

#endif /* !defined(SHRINIT_H_INCLUDED) */


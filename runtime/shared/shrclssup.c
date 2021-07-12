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

#include "shrclssup.h"
#include "shared_internal.h"
#include "zip_api.h"
#include "j9shrnls.h"
#include "CacheLifecycleManager.hpp"

#define SHRCLSSUP_ERR_TRACE(verbose, var) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var)
#define SHRCLSSUP_ERR_TRACE1(verbose, var, p1) if (verbose) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1)

IDATA J9VMDllMain(J9JavaVM* vm, IDATA stage, void* reserved) 
{
	IDATA returnVal = J9VMDLLMAIN_OK;
	UDATA rc = 0;

	PORT_ACCESS_FROM_JAVAVM(vm);

	if (vm->sharedCacheAPI == NULL) {

		IDATA index = FIND_ARG_IN_VMARGS( OPTIONAL_LIST_MATCH, OPT_XSHARECLASSES, NULL);

		U_64 runtimeFlags = getDefaultRuntimeFlags();

		runtimeFlags |= ((j9shr_isPlatformDefaultPersistent(vm) == TRUE) ? J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE : 0);

		vm->sharedCacheAPI = (J9SharedCacheAPI*)j9mem_allocate_memory(sizeof(J9SharedCacheAPI), J9MEM_CATEGORY_CLASSES);
		if (vm->sharedCacheAPI == NULL) {
			return J9VMDLLMAIN_FAILED;
		}
		memset(vm->sharedCacheAPI, 0, sizeof(J9SharedCacheAPI));
		vm->sharedCacheAPI->softMaxBytes = (U_32)-1;
		vm->sharedCacheAPI->minAOT = -1;
		vm->sharedCacheAPI->maxAOT = -1;
		vm->sharedCacheAPI->minJIT = -1;
		vm->sharedCacheAPI->maxJIT = -1;
		vm->sharedCacheAPI->layer = -1;
		vm->sharedCacheAPI->newCacheSize = (U_32)-1;
		if (index >= 0) {
			char optionsBuffer[SHR_SUBOPT_BUFLEN];
			char* optionsBufferPtr = (char*)optionsBuffer;
			IDATA parseRc = OPTION_OK;

			vm->sharedCacheAPI->xShareClassesPresent = TRUE;
			vm->sharedCacheAPI->sharedCacheEnabled = TRUE;
			parseRc = GET_OPTION_VALUES(index, ':', ',', &optionsBufferPtr, SHR_SUBOPT_BUFLEN);
			if (OPTION_OK == parseRc) {
				UDATA verboseFlags = J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT;
				UDATA printStatsOptions = PRINTSTATS_SHOW_NONE;  /* Default */
				UDATA storageKeyTesting = 0;
				char* cacheName = CACHE_ROOT_PREFIX;	/* Default */
				char* modContext = NULL;
				char* expireTime = NULL;
				char* ctrlDirName = NULL;
				char* cacheDirPermStr = NULL;
				char* methodSpecs = NULL;
#if !defined(WIN32) && !defined(WIN64)
				char defaultCacheDir[J9SH_MAXPATH];
				IDATA ret = 0;
				BOOLEAN usingDefaultDir = TRUE;
#endif
				IDATA argIndex1 = -1;
				IDATA argIndex2 = -1;

				/* reset and disablecorruptcachedumps are special options
				 * They are appended to the next option which is not special
				 * Here, the options are separated by NULL characters*/
				BOOLEAN matchedOneSpecialOptions;
				UDATA bufSize = SHR_SUBOPT_BUFLEN;
				do {
					matchedOneSpecialOptions = FALSE;
					/* If reset is found as an extra option to the right of other options, add it to the command-line
					 * Also checks for complete option -Xshareclasses:reset
					 * -Xshareclasses:reset,printstats will not count*/
					if ((strcmp(optionsBufferPtr, OPTION_RESET)==0) && (optionsBufferPtr [strlen(OPTION_RESET) + 1] == 0)) {
						if ((index = FIND_NEXT_ARG_IN_VMARGS(OPTIONAL_LIST_MATCH, OPT_XSHARECLASSES, NULL, index))>=0) {
							UDATA resetLen = strlen(OPTION_RESET);
							optionsBufferPtr += resetLen + 1;
							bufSize -= resetLen + 1;
							rc = GET_OPTION_VALUES(index, ':', ',', &optionsBufferPtr, bufSize);
							matchedOneSpecialOptions = TRUE;
							if (OPTION_OK != rc) {
								if (OPTION_OVERFLOW == rc) {
									SHRCLSSUP_ERR_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_SHRC_SHRCLSSUP_FAILURE_OPTION_BUFFER_OVERFLOW, SHR_SUBOPT_BUFLEN);
								}
								return J9VMDLLMAIN_FAILED;
							}
						}
					}
					/* If disablecorruptcachedumps is found as an extra option to the right of other options, add it to the command-line */
					if ((strcmp(optionsBufferPtr, OPTION_DISABLE_CORRUPT_CACHE_DUMPS)==0) && (optionsBufferPtr [strlen(OPTION_DISABLE_CORRUPT_CACHE_DUMPS) + 1] == 0)) {
						if ((index = FIND_NEXT_ARG_IN_VMARGS(OPTIONAL_LIST_MATCH, OPT_XSHARECLASSES, NULL, index))>=0) {
							UDATA disableCorruptedCacheLen = strlen(OPTION_DISABLE_CORRUPT_CACHE_DUMPS);
							optionsBufferPtr += disableCorruptedCacheLen + 1;
							bufSize -= disableCorruptedCacheLen + 1;
							rc = GET_OPTION_VALUES(index, ':', ',', &optionsBufferPtr, bufSize);
							matchedOneSpecialOptions = TRUE;
							if (OPTION_OK != rc) {
								if (OPTION_OVERFLOW == rc) {
									SHRCLSSUP_ERR_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_SHRC_SHRCLSSUP_FAILURE_OPTION_BUFFER_OVERFLOW, SHR_SUBOPT_BUFLEN);
								}
								return J9VMDLLMAIN_FAILED;
							}
						}
					}
				} while (matchedOneSpecialOptions);
				optionsBufferPtr = optionsBuffer;

				/* Check for -XX:ShareClassesEnableBCI and -XX:ShareClassesDisableBCI; whichever comes later wins.
				 * These options should be checked before parseArgs() to allow -Xshareclasses:[enable|disable]BCI to override this option.
				 * 
				 * Note: Please also change the function checkArgsConsumed() in runtime/vm/jvminit.c when adding new options,
				 * in order to quietly consume the options if it is used without -Xshareclasses
				 */
				argIndex1 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXSHARECLASSESENABLEBCI, NULL);
				argIndex2 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXSHARECLASSESDISABLEBCI, NULL);
				if (argIndex1 > argIndex2) {
					runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_BCI;
				} else if (argIndex2 > argIndex1) {
					runtimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_BCI;
				}

				/* Check for -XX:+ShareAnonymousClasses and -XX:-ShareAnonymousClasses; whichever comes later wins. Enable is set by default so we just need to disable when that's the case. */
				argIndex1 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXENABLESHAREANONYMOUSCLASSES, NULL);
				argIndex2 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXDISABLESHAREANONYMOUSCLASSES, NULL);
				if (argIndex2 > argIndex1) {
					runtimeFlags &= (~J9SHR_RUNTIMEFLAG_ENABLE_SHAREANONYMOUSCLASSES);
				}

				/* Check for -XX:+ShareUnsafeClasses and -XX:-ShareUnsafeClasses; whichever comes later wins. Enable is set by default so we just need to disable when that's the case. */
				argIndex1 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXENABLESHAREUNSAFECLASSES, NULL);
				argIndex2 = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXDISABLESHAREUNSAFECLASSES, NULL);
				if (argIndex2 > argIndex1) {
					runtimeFlags &= (~J9SHR_RUNTIMEFLAG_ENABLE_SHAREUNSAFECLASSES);
				}
								
				vm->sharedCacheAPI->parseResult = parseArgs(vm, optionsBufferPtr, &runtimeFlags, &verboseFlags, &cacheName, &modContext,
								&expireTime, &ctrlDirName, &cacheDirPermStr, &methodSpecs, &printStatsOptions, &storageKeyTesting);
				if ((RESULT_PARSE_FAILED == vm->sharedCacheAPI->parseResult)
				){
					return J9VMDLLMAIN_FAILED;
				}

				if (cacheName != NULL) {
					vm->sharedCacheAPI->cacheName = (char *) j9mem_allocate_memory(strlen(cacheName)+1, J9MEM_CATEGORY_CLASSES);
					if (vm->sharedCacheAPI->cacheName == NULL) {
						return J9VMDLLMAIN_FAILED;
					}
					memcpy(vm->sharedCacheAPI->cacheName, cacheName, strlen(cacheName)+1);
				}
				if (ctrlDirName != NULL) {
					vm->sharedCacheAPI->ctrlDirName = (char *) j9mem_allocate_memory(strlen(ctrlDirName)+1, J9MEM_CATEGORY_CLASSES);
					if (vm->sharedCacheAPI->ctrlDirName == NULL) {
						return J9VMDLLMAIN_FAILED;
					}
					memcpy(vm->sharedCacheAPI->ctrlDirName, ctrlDirName, strlen(ctrlDirName)+1);
				}
				if (modContext != NULL) {
					vm->sharedCacheAPI->modContext = (char *) j9mem_allocate_memory(strlen(modContext)+1, J9MEM_CATEGORY_CLASSES);
					if (vm->sharedCacheAPI->modContext == NULL) {
						return J9VMDLLMAIN_FAILED;
					}
					memcpy(vm->sharedCacheAPI->modContext, modContext, strlen(modContext)+1);
				}
				if (expireTime != NULL) {
					vm->sharedCacheAPI->expireTime = (char *) j9mem_allocate_memory(strlen(expireTime)+1, J9MEM_CATEGORY_CLASSES);
					if (vm->sharedCacheAPI->expireTime == NULL) {
						return J9VMDLLMAIN_FAILED;
					}
					memcpy(vm->sharedCacheAPI->expireTime, expireTime, strlen(expireTime)+1);
				}
				if (NULL != methodSpecs) {
					vm->sharedCacheAPI->methodSpecs = (char *) j9mem_allocate_memory(strlen(methodSpecs) + 1, J9MEM_CATEGORY_CLASSES);
					if (NULL == vm->sharedCacheAPI->methodSpecs) {
						return J9VMDLLMAIN_FAILED;
					}
					memcpy(vm->sharedCacheAPI->methodSpecs, methodSpecs, strlen(methodSpecs) + 1);
				}

				if (runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE) {
					vm->sharedCacheAPI->cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
				} else {
					vm->sharedCacheAPI->cacheType = J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
				}
				/* set runtimeFlags and verboseFlags here as they will be used later in j9shr_getCacheDir() */
				vm->sharedCacheAPI->runtimeFlags = runtimeFlags;
				vm->sharedCacheAPI->verboseFlags = verboseFlags;

#if !defined(WIN32) && !defined(WIN64)
				if (NULL != ctrlDirName) {
					/* Get platform default cache directory */
					ret = j9shr_getCacheDir(vm, NULL, defaultCacheDir, J9SH_MAXPATH, vm->sharedCacheAPI->cacheType);
					if ((0 == ret)
						&& (0 != strcmp(defaultCacheDir, ctrlDirName))
					) {
						usingDefaultDir = FALSE;
					}
				}

				if (FALSE == usingDefaultDir) {
					vm->sharedCacheAPI->cacheDirPerm = convertPermToDecimal(vm, cacheDirPermStr);
					if ((UDATA)-1 == vm->sharedCacheAPI->cacheDirPerm) {
						return J9VMDLLMAIN_FAILED;
					}
				} else {
					/* We are using platform default cache directory. */
					vm->sharedCacheAPI->cacheDirPerm = J9SH_DIRPERM_ABSENT;
				}

				if (J9SH_DIRPERM_ABSENT == vm->sharedCacheAPI->cacheDirPerm) {
					if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS)) {
						/* if groupAccess is set, change J9SH_DIRPERM_ABSENT to J9SH_DIRPERM_ABSENT_GROUPACCESS */
						vm->sharedCacheAPI->cacheDirPerm = J9SH_DIRPERM_ABSENT_GROUPACCESS;
					}
				}
#else
				vm->sharedCacheAPI->cacheDirPerm = J9SH_DIRPERM_ABSENT;
#endif
				vm->sharedCacheAPI->printStatsOptions = printStatsOptions;
				vm->sharedCacheAPI->storageKeyTesting = storageKeyTesting;
			} else {
				if (OPTION_OVERFLOW == parseRc) {
					SHRCLSSUP_ERR_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_SHRC_SHRCLSSUP_FAILURE_OPTION_BUFFER_OVERFLOW, SHR_SUBOPT_BUFLEN);
				}
				return J9VMDLLMAIN_FAILED;
			}
		} else {
			OMRPORT_ACCESS_FROM_J9PORT(vm->portLibrary);
			vm->sharedCacheAPI->xShareClassesPresent = FALSE;
			if (J9_SHARED_CACHE_DEFAULT_BOOT_SHARING(vm)) {
				BOOLEAN inContainer = omrsysinfo_is_running_in_container();
				/* Do not enable shared classes by default if running in Container */

				if (FALSE == inContainer) {
					/* If -Xshareclasses is not used in the CML, let VM startup on non-fatal error.
					 * If shared cache failed to start, user can use -Xshareclasses:bootClassesOnly,fatal to debug. */
					runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_NONFATAL;
					runtimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_CACHE_NON_BOOT_CLASSES;
					vm->sharedCacheAPI->sharedCacheEnabled = TRUE;
				} else {
					vm->sharedCacheAPI->inContainer = TRUE;
				}
			}
			/* Initialize the default settings of the flags.
			 * The runtimeFlags are used by shared cache utilities even if there is no active cache
			 * (i.e. even if there is no -Xshareclasses option specified).
			 * In particular,  SH_CompositeCacheImpl::startupForStats() looks at the runtimeFlags to
			 * determine if mprotection should be done.
			 */
			if (vm->sharedCacheAPI->sharedCacheEnabled) {
				/* clear verboseflags if -Xshareclasses is not used in the CML */
				vm->sharedCacheAPI->verboseFlags = 0;
			} else {
				vm->sharedCacheAPI->verboseFlags = J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT;
			}
			vm->sharedCacheAPI->runtimeFlags = runtimeFlags;
		}
	}

	switch(stage) {
	/* This code detects "none" as part of Xshareclasses and unloads the DLL if it is found. Must therefore be in this stage or earlier */
	case DLL_LOAD_TABLE_FINALIZED :
	{
		IDATA index = FIND_ARG_IN_VMARGS( OPTIONAL_LIST_MATCH, OPT_XSHARECLASSES, NULL);
		if (index >= 0) {
			char optionsBuffer[SHR_SUBOPT_BUFLEN];
			char* optionsBufferPtr = (char*)optionsBuffer;

			if (GET_OPTION_VALUES(index, ':', ',', &optionsBufferPtr, SHR_SUBOPT_BUFLEN) == OPTION_OK) {
				while(*optionsBufferPtr) {
					if (try_scan(&optionsBufferPtr, OPT_NONE)) {
						J9VMDllLoadInfo* loadInfo = FIND_DLL_TABLE_ENTRY( THIS_DLL_NAME );

						if (loadInfo) {
							loadInfo->loadFlags |= FORCE_UNLOAD;
						}
						break;
					}
					optionsBufferPtr += strlen(optionsBufferPtr)+1;
				}
			}
		}
		break;
	}

	case ALL_VM_ARGS_CONSUMED :
		FIND_AND_CONSUME_ARG( OPTIONAL_LIST_MATCH, OPT_XSHARECLASSES, NULL );
		vm->sharedClassConfig = NULL;
		break;

	/* Initialize only when heap structures and system classloader are set */
	case JIT_INITIALIZED :
	{
		UDATA loadFlags = 0;
		UDATA nonfatal = 0;

		/* Register this module with trace */
		UT_MODULE_LOADED(J9_UTINTERFACE_FROM_VM(vm));
		Trc_SHR_VMInitStages_Event1(vm->mainThread);
		vm->sharedCacheAPI->iterateSharedCaches = j9shr_iterateSharedCaches;
		vm->sharedCacheAPI->destroySharedCache = j9shr_destroySharedCache;
		if (vm->sharedCacheAPI->inContainer) {
			Trc_SHR_VMInitStages_Event_RunningInContainer(vm->mainThread);
		}
		if ((vm->sharedCacheAPI->sharedCacheEnabled == TRUE) && (vm->sharedCacheAPI->parseResult != RESULT_DO_UTILITIES) ) {
			/* Modules wishing to determine whether shared classes initialized correctly or not should query
			 * vm->sharedClassConfig->runtimeFlags for J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE */
			if ((rc = j9shr_init(vm, loadFlags, &nonfatal)) != J9VMDLLMAIN_OK) {
				if (nonfatal) {
					return J9VMDLLMAIN_OK;
				} else {
					return rc;
				}
			}
			vm->systemClassLoader->flags |= J9CLASSLOADER_SHARED_CLASSES_ENABLED;
			if (FIND_ARG_IN_VMARGS(EXACT_MATCH, VMOPT_XNOLINENUMBERS, NULL) < 0) {
				vm->requiredDebugAttributes |= J9VM_DEBUG_ATTRIBUTE_RECORD_ALL;
			}
		}
		break;
	}

	case ALL_LIBRARIES_LOADED:
	{
		if ((vm->sharedCacheAPI->sharedCacheEnabled == TRUE) && (vm->sharedCacheAPI->parseResult != RESULT_DO_UTILITIES)) {
			if (0 != initZipLibrary(vm->portLibrary, vm->j2seRootDirectory)) {
				returnVal = J9VMDLLMAIN_FAILED;
			}
		}
		break;
	}

	/* MUST run before JCL_INITIALIZED, but as late as possible */
	case ABOUT_TO_BOOTSTRAP :
	{
		/*Note: 
		 * j9shr_sharedClassesFinishInitialization() is called by jvminit.c before this stage of startup.
		 */
		break;
	}

	case LIBRARIES_ONUNLOAD :
	case JVM_EXIT_STAGE :
		j9shr_guaranteed_exit(vm, FALSE);	
		break;

	case HEAP_STRUCTURES_FREED :
		if (vm != NULL) {
			j9shr_shutdown(vm);
		}
		break;

	default :
		/* do nothing */
		break;
	}

	return returnVal;
}

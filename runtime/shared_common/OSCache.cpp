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

#include <string.h>
#include "j9cfg.h"
#include "j9port.h"
#include "pool_api.h"
#include "j9shrnls.h"
#include "util_api.h"
#include "j2sever.h"
#include "shrinit.h"
#include "j9version.h"
#include "shchelp.h"

#include "OSCachesysv.hpp"
#include "OSCachemmap.hpp"
#include "CacheMap.hpp"

/**
 * Function which builds a cache filename from a cache name, version data and generation.
 * A cache file name is currently a composite of the cache name with a version prefix and generation postfix
 * Note that the function returns different results for Windows and Unix.
 *
 * @param [in] portlib  A portLibrary
 * @param [out] buffer  The buffer to write the filename into
 * @param [in] bufferSize  The size of the buffer in bytes
 * @param [in] cacheName  The name of the cache
 * @param [in] versionData  The version information for the cache
 * @param [in] generation  The generation
 * @param [in] isMemoryType  True if the file is a shared memory control file, false if for a semaphore
 * @param [in] layer  The shared cache layer number.
 */
void
SH_OSCache::getCacheVersionAndGen(J9PortLibrary* portlib, J9JavaVM* vm, char* buffer, UDATA bufferSize, const char* cacheName,
		J9PortShcVersion* versionData, UDATA generation, bool isMemoryType, I_8 layer)
{
	char genString[7];
	char versionStr[J9SH_VERSION_STRING_LEN+3];
	UDATA versionStrLen = 0;

	PORT_ACCESS_FROM_PORT(portlib);

	Trc_SHR_OSC_getCacheVersionAndGen_Entry_V1(cacheName, generation, layer);

	memset(versionStr, 0, J9SH_VERSION_STRING_LEN+1);
	if (generation <= J9SH_GENERATION_07) {
		J9SH_GET_VERSION_G07ANDLOWER_STRING(PORTLIB, versionStr, J9SH_VERSION(versionData->esVersionMajor, versionData->esVersionMinor), versionData->modlevel, versionData->addrmode);
	} else {
		U_64 curVMVersion = 0;
		U_64 oldVMVersion = 0;

		/* CMVC 163957:
		 * There is overlap Java 6 cache file generations and Java 7. For example:
		 * - Java 6 SR8: C240D2A64P_java6sr8_G08
		 * - Java 7 CVS HEAD: C260M3A64P_cvshead_G08
		 *
		 * If we use any thing older than Major=2, and Minor=60 we must use J9SH_GET_VERSION_G07ANDLOWER_STRING to get the file name.
		 *
		 * When called b/c of '-Xshareclasses:listAllCaches' if the wrong cache name is used the cache will fail to be started, and will
		 * not be listed.
		 */
		oldVMVersion = SH_OSCache::getCacheVersionToU64(2, 60);
		curVMVersion = SH_OSCache::getCacheVersionToU64(versionData->esVersionMajor, versionData->esVersionMinor);

		if ( curVMVersion >= oldVMVersion) {
			if (generation <= J9SH_GENERATION_29) {
				J9SH_GET_VERSION_G07TO29_STRING(PORTLIB, versionStr, J9SH_VERSION(versionData->esVersionMajor, versionData->esVersionMinor), versionData->modlevel, versionData->addrmode);
			} else if (versionData->modlevel < 10) {
				J9SH_GET_VERSION_STRING_JAVA9ANDLOWER(PORTLIB, versionStr, J9SH_VERSION(versionData->esVersionMajor, versionData->esVersionMinor), versionData->modlevel, versionData->feature, versionData->addrmode);
			} else {
				J9SH_GET_VERSION_STRING(PORTLIB, versionStr, J9SH_VERSION(versionData->esVersionMajor, versionData->esVersionMinor), versionData->modlevel, versionData->feature, versionData->addrmode);
			}
		} else {
			J9SH_GET_VERSION_G07ANDLOWER_STRING(PORTLIB, versionStr, J9SH_VERSION(versionData->esVersionMajor, versionData->esVersionMinor), versionData->modlevel, versionData->addrmode);
		}
	}

	versionStrLen = strlen(versionStr);
	if (J9PORT_SHR_CACHE_TYPE_PERSISTENT == versionData->cacheType) {
		versionStr[versionStrLen] = J9SH_PERSISTENT_PREFIX_CHAR;
	} else if (J9PORT_SHR_CACHE_TYPE_SNAPSHOT == versionData->cacheType) {
		versionStr[versionStrLen] = J9SH_SNAPSHOT_PREFIX_CHAR;
	}
	if (generation <= J9SH_GENERATION_37) {
		j9str_printf(PORTLIB, genString, 4, "G%02d", generation);
	} else {
		Trc_SHR_Assert_True(
							((0 <= layer) && (layer <= J9SH_LAYER_NUM_MAX_VALUE))
							|| (J9SH_LAYER_NUM_UNSET == layer)
							);
		j9str_printf(PORTLIB, genString, 7, "G%02dL%02d", generation, layer);
	}

#if defined(WIN32)
	j9str_printf(PORTLIB, buffer, bufferSize, "%s%c%s%c%s", versionStr, J9SH_PREFIX_SEPARATOR_CHAR, cacheName, J9SH_PREFIX_SEPARATOR_CHAR, genString);
#else
	/* In either case we avoid attaching the "_memory_" or "_semaphore_" substring. */
	if ((J9PORT_SHR_CACHE_TYPE_PERSISTENT == versionData->cacheType)
		|| (J9PORT_SHR_CACHE_TYPE_SNAPSHOT == versionData->cacheType)
		|| (J9PORT_SHR_CACHE_TYPE_CROSSGUEST == versionData->cacheType)
	) {
		j9str_printf(PORTLIB, buffer, bufferSize, "%s%c%s%c%s", versionStr, J9SH_PREFIX_SEPARATOR_CHAR, cacheName, J9SH_PREFIX_SEPARATOR_CHAR, genString);
	} else {
		const char* identifier = isMemoryType ? J9SH_MEMORY_ID : J9SH_SEMAPHORE_ID;

		j9str_printf(PORTLIB, buffer, bufferSize, "%s%s%s%c%s", versionStr, identifier, cacheName, J9SH_PREFIX_SEPARATOR_CHAR, genString);
	}
#endif
	Trc_SHR_OSC_getCacheVersionAndGen_Exit(buffer);
}

/**
 * Extract the cache name from a filename of the format created by getCacheVersionAndGen
 *
 * @param [out] buffer  Buffer to extract the cache name into
 * @param [in] bufferSize  The size of the buffer in bytes
 * @param [in] versionLen  Length of the version prefix (is variable for different types of cache)
 * @param [in] cacheNameWithVGen  The filename to extract the name from
 *
 * @return 0 on success or -1 for failure
 */
IDATA
SH_OSCache::removeCacheVersionAndGen(char* buffer, UDATA bufferSize, UDATA versionLen, const char* cacheNameWithVGen)
{
	char* nameStart;
	UDATA lengthMinusGenAndLayer = 0;
	UDATA generation = getGenerationFromName(cacheNameWithVGen);

	Trc_SHR_OSC_removeCacheVersionAndGen_Entry(versionLen, cacheNameWithVGen);

	/*
	 * cache names generated by earlier JVMs (generation <=29) don't have feature prefix char 'F' and the value
	 * adjust versionLen to parse cacheNameWithVGen generated by earlier JVMs according to generation number
	 */
	if (generation <= J9SH_GENERATION_29) {
		versionLen -= J9SH_VERSTRLEN_INCREASED_SINCEG29;
	}
	/* modLevel becomes 2 digits from Java 10 */
	if (getModLevelFromName(cacheNameWithVGen) < 10) {
		versionLen -= J9SH_VERSTRLEN_INCREASED_SINCEJAVA10;
	}

	nameStart = (char*)(cacheNameWithVGen + versionLen);
	if (generation <= J9SH_GENERATION_37) {
		lengthMinusGenAndLayer = strlen(nameStart) - strlen("_GXX");
	} else {
		lengthMinusGenAndLayer = strlen(nameStart) - strlen("_GXXLXX");
	}
	if (lengthMinusGenAndLayer >= bufferSize) {
		Trc_SHR_OSC_removeCacheVersionAndGen_overflow();
		return -1;
	}
	strncpy(buffer, nameStart, lengthMinusGenAndLayer);
	buffer[lengthMinusGenAndLayer] = '\0';
	Trc_SHR_OSC_removeCacheVersionAndGen_Exit();
	return 0;
}

/**
 * Determine the directory to use for the cache file or control file(s)
 *
 * @param [in] vm  A J9JavaVM
 * @param [in] ctrlDirName  The control dir name
 * @param [out] buffer  The buffer to write the result into
 * @param [in] bufferSize  The size of the buffer in bytes
 * @param [in] cacheType  The Type of cache
 * @param [in] allowVerbose  Whether to allow verbose message.
 *
 * @return 0 on success or -1 for failure
 */
IDATA
SH_OSCache::getCacheDir(J9JavaVM* vm, const char* ctrlDirName, char* buffer, UDATA bufferSize, U_32 cacheType, bool allowVerbose)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	IDATA rc;
	BOOLEAN appendBaseDir;
	U_32 flags = 0;

	Trc_SHR_OSC_getCacheDir_Entry();

	/* Cache directory used is the j9shmem dir, regardless of whether we're using j9shmem or j9mmap */
	appendBaseDir = (NULL == ctrlDirName) || (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == cacheType) || (J9PORT_SHR_CACHE_TYPE_SNAPSHOT == cacheType);
	if (appendBaseDir) {
		flags |= J9SHMEM_GETDIR_APPEND_BASEDIR;
	}

#if defined(OPENJ9_BUILD)
	if ((NULL == ctrlDirName)
		&& J9_ARE_NO_BITS_SET(vm->sharedCacheAPI->runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_GROUP_ACCESS)
	) {
		/* j9shmem_getDir() always tries the CSIDL_LOCAL_APPDATA directory (C:\Documents and Settings\username\Local Settings\Application Data)
		 * first on Windows if ctrlDirName is NULL, regardless of whether J9SHMEM_GETDIR_USE_USERHOME is set or not. So J9SHMEM_GETDIR_USE_USERHOME is effective on UNIX only.
		 */

		flags |= J9SHMEM_GETDIR_USE_USERHOME;
	}
#endif /*defined(OPENJ9_BUILD) */

	rc = j9shmem_getDir(ctrlDirName, flags, buffer, bufferSize);

	if (rc < 0) {
		if (allowVerbose
			&& J9_ARE_ANY_BITS_SET(vm->sharedCacheAPI->verboseFlags, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT | J9SHR_VERBOSEFLAG_ENABLE_VERBOSE)
		) {
		switch(rc) {
			case J9PORT_ERROR_SHMEM_GET_DIR_BUF_OVERFLOW:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_BUF_OVERFLOW);
				break;
			case J9PORT_ERROR_SHMEM_GET_DIR_FAILED_TO_GET_HOME:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_FAILED_TO_GET_HOME);
				break;
			case J9PORT_ERROR_SHMEM_GET_DIR_HOME_BUF_OVERFLOW:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_HOME_BUF_OVERFLOW);
				break;
			case J9PORT_ERROR_SHMEM_GET_DIR_HOME_ON_NFS:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_HOME_ON_NFS);
				break;
			case J9PORT_ERROR_SHMEM_GET_DIR_CANNOT_STAT_HOME:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_CANNOT_STAT_HOME, j9error_last_error_number());
				break;
			case J9PORT_ERROR_SHMEM_NOSPACE:
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_GET_DIR_NO_SPACE);
				break;
			default:
				break;
			}
		}
		Trc_SHR_OSC_getCacheDir_j9shmem_getDir_failed1(ctrlDirName);
		return -1;
	}

	Trc_SHR_OSC_getCacheDir_Exit();
	return 0;
}

/**
 * Create the directory to use for the cache file or control file(s)
 *
 * @param [in] portLibrary  A portLibrary
 * @param[in] cacheDirName  The name of the cache directory
 * @param[in] cleanMemorySegments  set TRUE to call cleanSharedMemorySegments. It will clean sysv memory segments.
 *              It should be set to TRUE if the ctrlDirName is NULL.
 *
 * Returns -1 for error, >=0 for success
 */
IDATA
SH_OSCache::createCacheDir(J9PortLibrary* portLibrary, char* cacheDirName, UDATA cacheDirPerm, bool cleanMemorySegments)
{
	PORT_ACCESS_FROM_PORT(portLibrary);
	IDATA rc;

	Trc_SHR_OSC_createCacheDir_Entry(cacheDirName, cleanMemorySegments);

	rc = j9shmem_createDir(cacheDirName, cacheDirPerm, cleanMemorySegments);

	Trc_SHR_OSC_createCacheDir_Exit();
	return rc;
}
/* Returns the full path of a cache based on the current cacheDir value */
IDATA
SH_OSCache::getCachePathName(J9PortLibrary* portLibrary, const char* cacheDirName, char* buffer, UDATA bufferSize, const char* cacheNameWithVGen)
{
	PORT_ACCESS_FROM_PORT(portLibrary);

	Trc_SHR_OSC_getCachePathName_Entry(cacheNameWithVGen);

	j9str_printf(PORTLIB, buffer, bufferSize, "%s%s", cacheDirName, cacheNameWithVGen);

	Trc_SHR_OSC_getCachePathName_Exit();
	return 0;
}

/**
 * Stat to see if a named cache exists
 *
 * @param [in] portLibrary  A portLibrary
 * @param [in] cacheNameWithVGen  The cache filename
 * @param [in] displayNotFoundMsg  If true, message is displayed if the cache is not found
 *
 * @return 1 if the cache exists and 0 otherwise
 */
UDATA
SH_OSCache::statCache(J9PortLibrary* portLibrary, const char* cacheDirName, const char* cacheNameWithVGen, bool displayNotFoundMsg)
{
	PORT_ACCESS_FROM_PORT(portLibrary);
	char fullPath[J9SH_MAXPATH];

	Trc_SHR_OSC_statCache_Entry(cacheNameWithVGen);

	j9str_printf(PORTLIB, fullPath, J9SH_MAXPATH, "%s%s", cacheDirName, cacheNameWithVGen);
	if (j9file_attr(fullPath) == EsIsFile) {
		Trc_SHR_OSC_statCache_cacheFound();
		return 1;
	}

	if (displayNotFoundMsg) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_SHRC_OSCACHE_NOT_EXIST);
	}
	Trc_SHR_OSC_statCache_cacheNotFound();
	return 0;
}

/* Returns the value of the generation from the cache filename */
UDATA
SH_OSCache::getGenerationFromName(const char* cacheNameWithVGen)
{
	char* cursor = (char*)cacheNameWithVGen;
	UDATA genValue;

	if ((cursor = strrchr(cursor, J9SH_PREFIX_SEPARATOR_CHAR)) == NULL) {
		return 0;
	}
	if (cursor[1] != 'G') {
		return 0;
	}
	cursor += strlen("_G");
	scan_udata(&cursor, &genValue);
	return genValue;
}

/* Common startup function used by all OSCache subclasses - sets up the common class variables */
IDATA
SH_OSCache::commonStartup(J9JavaVM* vm, const char* ctrlDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig, UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, J9PortShcVersion* versionData)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	UDATA cacheNameLen = 0;
	UDATA cachePathNameLen = 0;
	UDATA versionStrLen = 0;
	char fullPathName[J9SH_MAXPATH];

	Trc_SHR_OSC_commonStartup_Entry();

	_config = piconfig;
	_verboseFlags = verboseFlags;
	_openMode = openMode;
	_createFlags = createFlag;
	_runtimeFlags = runtimeFlags;
	_isUserSpecifiedCacheDir = (J9_ARE_ALL_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_CACHEDIR_PRESENT));

	/* get the cacheDirName for the first time */
	if (!(_cacheDirName = (char*)j9mem_allocate_memory(J9SH_MAXPATH, J9MEM_CATEGORY_CLASSES))) {
		Trc_SHR_OSC_commonStartup_nomem_cacheDirName();
		OSC_ERR_TRACE(J9NLS_SHRC_OSCACHE_ALLOC_FAILED);
		return -1;
	}
	IDATA rc = SH_OSCache::getCacheDir(vm, ctrlDirName, _cacheDirName, J9SH_MAXPATH, versionData->cacheType);
	if (rc == -1) {
		Trc_SHR_OSC_commonStartup_getCacheDir_fail();
		/* NLS message has been printed out inside SH_OSCache::getCacheDir() if verbose flag is not 0 */
		return -1;
	}
	rc = SH_OSCache::createCacheDir(PORTLIB, _cacheDirName, cacheDirPerm, ctrlDirName == NULL);
	if (rc == -1) {
		Trc_SHR_OSC_commonStartup_createCacheDir_fail();
		/* remove trailing '/' */
		_cacheDirName[strlen(_cacheDirName)-1] = '\0';
		OSC_ERR_TRACE1(J9NLS_SHRC_OSCACHE_CREATECACHEDIR_FAILED_V2, _cacheDirName);
		return -1;
	}

	/* A persistent cache's name has an extra character 'P' and a cache snapshot file name has an extra character "S",
	 * so we need an extra byte for them, while for cross guest shared caches we need three extra characters to
	 * accommodate the 'T??', where ?? stand for the platform encoding.
	 */
	if ((J9PORT_SHR_CACHE_TYPE_PERSISTENT == versionData->cacheType)
		|| (J9PORT_SHR_CACHE_TYPE_SNAPSHOT == versionData->cacheType)
	) {
		versionStrLen = J9SH_VERSION_STRING_LEN + 1;
	} else if (J9PORT_SHR_CACHE_TYPE_CROSSGUEST == versionData->cacheType) {
		versionStrLen = J9SH_VERSION_STRING_LEN + 3;
	} else {
		versionStrLen = J9SH_VERSION_STRING_LEN;
	}

	if(J9_ARE_ANY_BITS_SET(_createFlags, J9SH_OSCACHE_CREATE | J9SH_OSCACHE_OPEXIST_DESTROY | J9SH_OSCACHE_OPEXIST_STATS | J9SH_OSCACHE_OPEXIST_DO_NOT_CREATE)) {
		/* okay, one of the flags are set */
	} else {
		Trc_SHR_OSC_commonStartup_wrongCreateFlags();
		OSC_ERR_TRACE(J9NLS_SHRC_OSCACHE_INVALIDFLAG);
		return -1;
	}

	/* Allocate enough memory for _cacheName and _cacheNameWithVGen. J9SH_MEMORY_ID only used on sysv UNIX */
	cacheNameLen = (strlen(cacheName) * 2) + versionStrLen + strlen("_GXXLXX") + 2 + strlen(J9SH_MEMORY_ID);
	if (!(_cacheNameWithVGen = (char*)j9mem_allocate_memory(cacheNameLen, J9MEM_CATEGORY_CLASSES))) {
		Trc_SHR_OSC_commonStartup_nomem_cacheName();
		OSC_ERR_TRACE(J9NLS_SHRC_OSCACHE_ALLOC_FAILED);
		return -1;
	}
	memset(_cacheNameWithVGen, 0, cacheNameLen);
	getCacheVersionAndGen(PORTLIB, vm, _cacheNameWithVGen, cacheNameLen, cacheName, versionData, _activeGeneration, true, _layer);

	/* The assert below is just a precaution.
	 * Check if cacheName with version and generation string is less than maximum length.
	 * The assert below will fail when the cache name becomes too big to fit in the array
	 * possibly due to a large version string or generational string
	 * or a hidden bug. strlen() function is intentionally not used since buffer might have overflowed
	 * and NULL character might have been overwritten in the case of a bug.
	 *
	 * Note that cacheNameLen can be less than CACHE_ROOT_MAXLEN, hence the 'min' macro is used.
	 * Simply check for presence of '\0 char before index CACHE_ROOT_MAXLEN for isolating the bug.
	 */
	Trc_SHR_Assert_True(_cacheNameWithVGen[OMR_MIN(cacheNameLen, CACHE_ROOT_MAXLEN) - 1] == '\0');
	_cacheName = _cacheNameWithVGen + strlen(_cacheNameWithVGen) + 1;
	memcpy(_cacheName, cacheName, strlen(cacheName) + 1);
	setEnableVerbose(PORTLIB, vm, versionData, _cacheNameWithVGen);

	if (getCachePathName(PORTLIB, _cacheDirName, fullPathName, J9SH_MAXPATH, _cacheNameWithVGen) == 0) {
		cachePathNameLen = strlen(fullPathName);
		if ((_cachePathName = (char*)j9mem_allocate_memory(cachePathNameLen + 1, J9MEM_CATEGORY_CLASSES))) {
			strcpy(_cachePathName, fullPathName);
		} else {
			Trc_SHR_OSC_commonStartup_nomem_cachePathName();
			OSC_ERR_TRACE(J9NLS_SHRC_OSCACHE_ALLOC_FAILED);
			return -1;
		}
	}

	_doCheckBuildID = ((openMode & J9OSCACHE_OPEN_MODE_CHECKBUILDID) != 0);

	Trc_SHR_OSC_commonStartup_copied_cachePathName(_cachePathName, cachePathNameLen);

	Trc_SHR_OSC_commonStartup_Exit();
	return 0;
}

/* override if the cache is persistent */
void
SH_OSCache::dontNeedMetadata(J9VMThread* currentThread, const void* startAddress, size_t length) {
	return;
}

/* Function that initializes class variables common to OSCache subclasses */
void
SH_OSCache::commonInit(J9PortLibrary* portLibrary, UDATA generation, I_8 layer)
{
	_startupCompleted = false;
	_portLibrary = portLibrary;
	_activeGeneration = generation;
	_layer = layer;
	_cacheNameWithVGen = NULL;
	_cacheName = NULL;
	_cachePathName = NULL;
	_cacheUniqueID = NULL;
	_cacheDirName = NULL;
	_verboseFlags = 0;
	_createFlags = 0;
	_config = NULL;
	_openMode = 0;
	_dataStart = NULL;
	_dataLength = 0;
	_headerStart = NULL;
	_cacheSize = 0;
	_errorCode = 0;
	_runningReadOnly = false;
	_doCheckBuildID = false;
	_isUserSpecifiedCacheDir = false;
}

/* Function that cleans up resources common to OSCache subclasses */
void
SH_OSCache::commonCleanup()
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_commonCleanup_Entry();

	if (_cacheNameWithVGen) {
		j9mem_free_memory(_cacheNameWithVGen);
	}
	if (_cachePathName) {
		j9mem_free_memory(_cachePathName);
	}
	if (_cacheDirName) {
		j9mem_free_memory(_cacheDirName);
	}
	if (_cacheUniqueID) {
		j9mem_free_memory(_cacheUniqueID);
	}

	/* If the cache is destroyed and then restarted, we still need portLibrary, versionData and generation */
	commonInit(_portLibrary, _activeGeneration, _layer);

	Trc_SHR_OSC_commonCleanup_Exit();
}

/**
 * Static method to create a new OSCache instance
 *
 * @param [in] portLib  The portlib
 * @param [in] memForConstructor  Pointer to the memory to build the OSCache instance into
 * @param [in] cacheName  Name of the cache
 * @param [in] generation  Generation of the cache
 * @param [in] versionData  Version data of the cache
 * @param [in] layer  The layer number of the cache
 *
 * @return A pointer to the new OSCache object (which will be the same as the memForConstructor parameter)
 *          It never returns NULL.
 */
SH_OSCache*
SH_OSCache::newInstance(J9PortLibrary* portlib, SH_OSCache* memForConstructor, const char* cacheName, UDATA generation, J9PortShcVersion* versionData, I_8 layer)
{
	SH_OSCache* newOSC = (SH_OSCache*)memForConstructor;

	PORT_ACCESS_FROM_PORT(portlib);

	Trc_SHR_OSC_newInstance_Entry_V1(memForConstructor, cacheName, versionData->cacheType, layer);

	switch(versionData->cacheType) {
	case J9PORT_SHR_CACHE_TYPE_PERSISTENT :
		Trc_SHR_OSC_newInstance_creatingMmap(newOSC);
		new(newOSC) SH_OSCachemmap();
		break;
	case J9PORT_SHR_CACHE_TYPE_NONPERSISTENT :
		Trc_SHR_OSC_newInstance_creatingSysv(newOSC);
		new(newOSC) SH_OSCachesysv();
		break;
	case J9PORT_SHR_CACHE_TYPE_SNAPSHOT:
		break;
	}
	Trc_SHR_OSC_newInstance_initializingNewObject();
	newOSC->initialize(PORTLIB, ((char*)memForConstructor + SH_OSCache::getRequiredConstrBytes()), generation, layer);

	Trc_SHR_OSC_newInstance_Exit(newOSC);
	return newOSC;
}

/**
 * Static method to get the number of bytes required for an OSCache object
 *
 * @return The number of bytes required for an OSCache object
 */
UDATA
SH_OSCache::getRequiredConstrBytes(void)
{
	UDATA reqBytes = 0;

	if (sizeof(SH_OSCachesysv) > sizeof(SH_OSCachemmap)) {
		reqBytes += sizeof(SH_OSCachesysv);
	} else {
		reqBytes += sizeof(SH_OSCachemmap);
	}

	return reqBytes;
}

/**
 * Returns the current cache generation
 */
UDATA
SH_OSCache::getCurrentCacheGen(void)
{
	return OSCACHE_CURRENT_CACHE_GEN;
}

void
SH_OSCache::setEnableVerbose(J9PortLibrary* portLib, J9JavaVM* vm, J9PortShcVersion* versionData, char* cacheNameWithVGen)
{
	U_32 javaVersion = getJCLForShcModlevel(versionData->modlevel);

	/* Disable verbose when only J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT is set for older generation caches belonging to this VM release */
	if ((J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT ==_verboseFlags) && (_activeGeneration != OSCACHE_CURRENT_CACHE_GEN) &&
			isCompatibleShcFilePrefix(portLib, javaVersion, getJVMFeature(vm), cacheNameWithVGen)) {
		_verboseFlags = 0;
	}
}

/**
 * Return a list of all the caches/snapshots that can be found in the current cache directory
 * It is the responsibility of the caller to free the pool returned by this function
 * The function returns a semi-ordered pool with the compatible caches/snapshots first and incompatible ones last
 *
 * @param[in] vm  The Java VM
 * @param[in] ctrlDirName  Cache directory
 * @param[in] groupPerm  Group permissions to open the cache directory
 * @param[in] localVerboseFlags  Flags indicating the level of verbose output in use
 * @param[in] j2seVersion  The j2se version that the VM is running
 * @param[in] includeOldGenerations  If true, include caches of older generations
 * @param[in] ignoreCompatible  If true, only gets cache/snapshot statistics of incompatible caches
 * @param [in] reason Indicates the reason for getting cache/snapshot stats. Refer sharedconsts.h for valid values.
 * @param [in] isCache If true, get cache stats. If false, get snapshot stats.
 *
 * @return a J9Pool that contains a semi-ordered list of caches/snapshots, NULL if no caches/snapshots are found
 */
J9Pool*
SH_OSCache::getAllCacheStatistics(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, UDATA localVerboseFlags, UDATA j2seVersion, bool includeOldGenerations, bool ignoreCompatible, UDATA reason, bool isCache)
{
	J9PortLibrary* portlib = vm->portLibrary;
	PORT_ACCESS_FROM_PORT(portlib);
	UDATA snapshotFindHandle = (UDATA)-1;
	UDATA shmemFindHandle = (UDATA)-1;
	UDATA mmapFindHandle = (UDATA)-1;
	/*
	 * It is essential that the length of these three arrays
	 * is EsMaxPath otherwise j9file_findX will overflow.
	 */
	char snapshotNameWithVGen[EsMaxPath];
	char shmemNameWithVGen[EsMaxPath];
	char mmapNameWithVGen[EsMaxPath];
	char persistentCacheDir[J9SH_MAXPATH];
	char nonpersistentCacheDir[J9SH_MAXPATH];
	char* nameWithVGen = NULL;
	IDATA cntr = 0;
	J9Pool* incompatibleList = NULL;
	SH_OSCache_Info tempInfo;
	SH_OSCache_Info* newElement = NULL;
	J9Pool* cacheList = NULL;
	IDATA getShmDirVal = 0;
	IDATA getMmapDirVal = 0;
	bool doneSnapshot = true;
	bool doneShmem = false;
	bool donePerst = false;
	bool outOfMem = false;
	J9Pool* lowerLayerList = NULL;
	J9Pool* rc = NULL;

	Trc_SHR_OSC_getAllCacheStatistics_Entry();

	if (false == isCache) {
		/* return a list of snapshot files only */
		doneSnapshot = false;
		doneShmem = true;
		donePerst = true;
		/* use nonpersistentCacheDir for snapshot file */
		getShmDirVal = getCacheDir(vm, ctrlDirName, nonpersistentCacheDir, J9SH_MAXPATH, J9PORT_SHR_CACHE_TYPE_SNAPSHOT);
		if (-1 != getShmDirVal) {
			snapshotFindHandle = SH_OSCacheFile::findfirst(PORTLIB,
									nonpersistentCacheDir,
									snapshotNameWithVGen, J9PORT_SHR_CACHE_TYPE_SNAPSHOT);
			if ((UDATA)-1 == snapshotFindHandle) {
				doneSnapshot = true;
			} else {
				nameWithVGen = snapshotNameWithVGen;
			}
		} else {
			Trc_SHR_OSC_getAllCacheStatistics_Exit1();
			rc = NULL;
			goto cleanup;
		}
	} else {
		if ((getShmDirVal = getCacheDir(vm,
										ctrlDirName,
										nonpersistentCacheDir,
										J9SH_MAXPATH,
										J9PORT_SHR_CACHE_TYPE_NONPERSISTENT)) != -1) {
			shmemFindHandle = SH_OSCachesysv::findfirst(PORTLIB,
									nonpersistentCacheDir,
									shmemNameWithVGen);
			if (shmemFindHandle == (UDATA)-1) {
				doneShmem = true;
			} else {
				nameWithVGen = shmemNameWithVGen;
			}
		} else {
			Trc_SHR_OSC_getAllCacheStatistics_Exit1();
			rc = NULL;
			goto cleanup;
		}

		/* If a cacheDir has been specified, need to also search the cacheDir for persistent caches */
		if ((getMmapDirVal = getCacheDir(vm,
										 ctrlDirName,
										 persistentCacheDir,
										 J9SH_MAXPATH,
										 J9PORT_SHR_CACHE_TYPE_PERSISTENT)) != -1) {
			if ((mmapFindHandle = SH_OSCacheFile::findfirst(PORTLIB,
									persistentCacheDir,
									mmapNameWithVGen,
									J9PORT_SHR_CACHE_TYPE_PERSISTENT)) == (UDATA)-1) {
				donePerst = true;
			} else if (NULL == nameWithVGen) {
				/* Set it to mmap-name only if it wasn't set to shmem name. */
				nameWithVGen = mmapNameWithVGen;
			}
		} else {
			Trc_SHR_OSC_getAllCacheStatistics_Exit1();
			rc = NULL;
			goto cleanup;
		}
	}

	if (doneSnapshot && doneShmem && donePerst) {
		Trc_SHR_OSC_getAllCacheStatistics_Exit1();
		rc = NULL;
		goto cleanup;
	}

	if (!ignoreCompatible) {
		cacheList = pool_new(sizeof(SH_OSCache_Info), 0, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_CLASSES, POOL_FOR_PORT(PORTLIB));
		if (cacheList == NULL) {
			Trc_SHR_OSC_getAllCacheStatistics_Exit2();
			rc = NULL;
			goto cleanup;
		}
		cacheList->flags |= POOL_ALWAYS_KEEP_SORTED;
	}

	/* Incompatible caches are added after the compatible caches so are initially created in a different pool */
	incompatibleList = pool_new(sizeof(SH_OSCache_Info), 0, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_CLASSES, POOL_FOR_PORT(PORTLIB));
	if (incompatibleList == NULL) {
		Trc_SHR_OSC_getAllCacheStatistics_Exit3();
		rc = NULL;
		goto cleanup;
	}
	incompatibleList->flags |= POOL_ALWAYS_KEEP_SORTED;

	/* Walk all the cache files in the j9shmem dir and (optionally) the persistent cacheDir */
	do {
		J9Pool** lowerLayerListPtr = &lowerLayerList;
		bool isToplayer = isTopLayerCache(vm, ctrlDirName, nameWithVGen);
		bool getLowerLayerStats = false;

		if (getCacheStatistics(vm, ctrlDirName, (const char*)nameWithVGen, groupPerm, localVerboseFlags, j2seVersion, &tempInfo, reason, getLowerLayerStats, isToplayer, lowerLayerListPtr, NULL) != -1) {
			if (includeOldGenerations || !tempInfo.isCompatible || (getCurrentCacheGen() == tempInfo.generation)) {
				if (tempInfo.isCompatible) {
					if (!ignoreCompatible) {
						if (isToplayer) {
							/* nameWithVGen is a compatible top layer cache, lower layer stats are in lowerLayerList*/
							newElement = (SH_OSCache_Info*) pool_newElement(cacheList);
							if (NULL == newElement) {
								Trc_SHR_OSC_getAllCacheStatistics_pool_newElement_failed(0);
								outOfMem = true;
								goto done;
							}
							memcpy(newElement, &tempInfo, sizeof(SH_OSCache_Info));
							if (NULL != lowerLayerList) {
								if (0 < pool_numElements(lowerLayerList)) {
									pool_state poolState;
									SH_OSCache_Info* anElement = (SH_OSCache_Info*)pool_startDo(lowerLayerList, &poolState);
									do {
										newElement = (SH_OSCache_Info*) pool_newElement(cacheList);
										if (NULL == newElement) {
											Trc_SHR_OSC_getAllCacheStatistics_pool_newElement_failed(1);
											outOfMem = true;
											goto done;
										}
										memcpy(newElement, anElement, sizeof(SH_OSCache_Info));
										++cntr;
									} while ((anElement = (SH_OSCache_Info*)pool_nextDo(&poolState)) != NULL);
								}
							}
						} else {
							if (SHR_STATS_REASON_ITERATE != reason) {
								newElement = (SH_OSCache_Info*) pool_newElement(cacheList);
								if (NULL == newElement) {
									Trc_SHR_OSC_getAllCacheStatistics_pool_newElement_failed(2);
									outOfMem = true;
									goto done;
								}
								memcpy(newElement, &tempInfo, sizeof(SH_OSCache_Info));
							}
							/* nameWithVGen is a compatible lower layer cache and getLowerLayerStats is false, do nothing here. stats of non-top layers are included in the lowerLayerList of when calling getCacheStatistics() on the top layer */
						}
					} else {
						/* ignoreCompatible is true, do nothing here */
					}
				} else {
					newElement = (SH_OSCache_Info*) pool_newElement(incompatibleList);
					if (NULL == newElement) {
						Trc_SHR_OSC_getAllCacheStatistics_pool_newElement_failed(3);
						outOfMem = true;
						goto done;
					}
					memcpy(newElement, &tempInfo, sizeof(SH_OSCache_Info));
				}
			}
			++cntr;
		}
		
		if (NULL != lowerLayerList) {
			pool_kill(lowerLayerList);
			lowerLayerList = NULL;
		}
		
		if (!doneSnapshot) {
			if (-1 == SH_OSCacheFile::findnext(PORTLIB, snapshotFindHandle, snapshotNameWithVGen, J9PORT_SHR_CACHE_TYPE_SNAPSHOT)) {
				doneSnapshot = true;
				continue;
			}
			nameWithVGen = snapshotNameWithVGen;
		} else if (!doneShmem) {
			if (SH_OSCachesysv::findnext(PORTLIB, shmemFindHandle, shmemNameWithVGen) == -1) {
				doneShmem = true;
				if (!donePerst) {
					nameWithVGen = mmapNameWithVGen;
					continue;
				}
			} else {
				nameWithVGen = shmemNameWithVGen;
			}
		} else if (!donePerst) {
			if (SH_OSCacheFile::findnext(PORTLIB, mmapFindHandle, mmapNameWithVGen, J9PORT_SHR_CACHE_TYPE_PERSISTENT) == -1) {
				donePerst = true;
			} else {
				nameWithVGen = mmapNameWithVGen;
			}
		}
	} while(!doneSnapshot || !doneShmem || !donePerst);

	/* Copy the incompatible cache data into the cacheList pool */
	if (!ignoreCompatible) {
		if (pool_numElements(incompatibleList)) {
			pool_state poolState;
			SH_OSCache_Info* anElement;

			anElement = (SH_OSCache_Info*)pool_startDo(incompatibleList, &poolState);
			do {
				newElement = (SH_OSCache_Info*) pool_newElement(cacheList);
				if (NULL == newElement) {
					Trc_SHR_OSC_getAllCacheStatistics_pool_newElement_failed(4);
					outOfMem = true;
					goto done;
				}
				memcpy(newElement, anElement, sizeof(SH_OSCache_Info));
			} while ((anElement = (SH_OSCache_Info*)pool_nextDo(&poolState)));
		}
		pool_kill(incompatibleList);
	}
done:
	if ((UDATA)-1 != snapshotFindHandle) {
		SH_OSCacheFile::findclose(PORTLIB, snapshotFindHandle);
	}
	if (shmemFindHandle != (UDATA)-1) {
		SH_OSCachesysv::findclose(PORTLIB, shmemFindHandle);
	}
	if (mmapFindHandle != (UDATA)-1) {
		SH_OSCacheFile::findclose(PORTLIB, mmapFindHandle);
	}

	Trc_SHR_OSC_getAllCacheStatistics_Exit4();
	if (outOfMem) {
		if (NULL != lowerLayerList) {
			pool_kill(lowerLayerList);
		}
		if (NULL != cacheList) {
			pool_kill(cacheList);
		}
		if (NULL != incompatibleList) {
			pool_kill(cacheList);
		}
		rc = NULL;
	} else {
		if (!ignoreCompatible) {
			rc = cacheList;
		} else {
			rc = incompatibleList;
		}
	}
cleanup:
	return rc;
}

/**
 * Get statistics on a given named cache/snapshot
 *
 * @param[in] vm  The Java VM
 * @param[in] ctrlDirName  Cache directory
 * @param[in] cacheNameWithVGen  The filename of the cache/snapshot to query
 * @param[in] groupPerm  Group permissions to open the cache directory
 * @param[in] localVerboseFlags  Flags indicating the level of verbose output in use
 * @param[in] j2seVersion  The j2se version that the JVM is running
 * @param[out] result  A pointer to a SH_OSCache_Info should be provided, which is filled in with the results
 * @param[in] reason Indicates the reason for getting cache stats. Refer sharedconsts.h for valid values.
 * @param[in] getLowerLayerStats Indicates whether the statistics we are trying to get from cacheNameWithVGen is a lower (non-top) layer shared cache.
 * @param[in] isTopLayer Indicates whether the cache is top layer or not.
 * @param[out] lowerLayerStatsList A list of SH_OSCache_Info for all lower layer caches.
 * @param[in] oscache An SH_OSCache. It is not NULL only when we are getting a  statistics on a compatible non-top layer cache.
 * 
 * @return 0 if the operations has been successful, -1 if an error has occured
 */
IDATA 
SH_OSCache::getCacheStatistics(J9JavaVM* vm, const char* ctrlDirName, const char* cacheNameWithVGen, UDATA groupPerm, UDATA localVerboseFlags, UDATA j2seVersion, SH_OSCache_Info* result, UDATA reason, bool getLowerLayerStats, bool isTopLayer, J9Pool** lowerLayerList, SH_OSCache* oscache)
{
	J9PortLibrary* portlib = vm->portLibrary;
	PORT_ACCESS_FROM_PORT(portlib);
	bool displayErrorMsg, isCurrentGen;
	IDATA rc = -1;
	J9PortShcVersion currentVersionData;
	U_64 fileVMVersion = 0;
	U_64 curVMVersion = 0;
	char cacheDirName[J9SH_MAXPATH];

	Trc_SHR_OSC_getCacheStatistics_Entry();

	if (NULL == result) {
		Trc_SHR_OSC_getCacheStatistics_NullResult();
		return -1;
	}

	if ((result->generation = getGenerationFromName(cacheNameWithVGen)) > OSCACHE_CURRENT_CACHE_GEN) {
		Trc_SHR_OSC_getCacheStatistics_FutureGen();
		return -1;
	}

	result->layer = getLayerFromName(cacheNameWithVGen);
	Trc_SHR_Assert_True(
						((0 <= result->layer) && (result->layer <= J9SH_LAYER_NUM_MAX_VALUE))
						|| (J9SH_LAYER_NUM_UNSET == result->layer)
						);

	if (0 == getValuesFromShcFilePrefix(PORTLIB, cacheNameWithVGen, &(result->versionData))) {
		Trc_SHR_OSC_getCacheStatistics_getValuesFromShcFilePrefix_Failed_Exit();
		return -1;
	}

	if (getCacheDir(vm, ctrlDirName, cacheDirName, J9SH_MAXPATH, result->versionData.cacheType) == -1) {
		Trc_SHR_OSC_getCacheDir_Failed_Exit();
		return -1;
	}

	isCurrentGen = (result->generation == getCurrentCacheGen());
	displayErrorMsg = (localVerboseFlags && isCurrentGen);

	if (!statCache(portlib, cacheDirName, cacheNameWithVGen, displayErrorMsg)) {
		Trc_SHR_OSC_getCacheStatistics_StatFailure();
		return -1;
	}

	setCurrentCacheVersion(vm, j2seVersion, &currentVersionData);

	fileVMVersion = SH_OSCache::getCacheVersionToU64(result->versionData.esVersionMajor, result->versionData.esVersionMinor);
	curVMVersion = SH_OSCache::getCacheVersionToU64(currentVersionData.esVersionMajor, currentVersionData.esVersionMinor);

	if ( fileVMVersion <= curVMVersion ) {
		/* The current JVM version can only see caches generated by:
		 * - Equal or lower JVM versions
		 * - Equal or lower JCL versions
		 */
		if (getShcModlevelForJCL(j2seVersion) < result->versionData.modlevel) {
			Trc_SHR_OSC_getCacheStatistics_badModLevel_Exit(result->versionData.modlevel);
			return -1;
		}
	} else if (fileVMVersion > curVMVersion) {
		/*Ignore cache files from newer JVMs*/
		Trc_SHR_OSC_getCacheStatistics_NewerJVMFile_Exit(cacheNameWithVGen);
		return -1;
	}

	result->isCompatible = (isCurrentGen && isCompatibleShcFilePrefix(portlib, (U_32)JAVA_SPEC_VERSION_FROM_J2SE(j2seVersion), getJVMFeature(vm), cacheNameWithVGen));
	/* isCorrupt is only valid if SHR_STATS_REASON_ITERATE is specified. */
	result->isCorrupt = 0;
	result->isJavaCorePopulated = 0;
	memset(&(result->javacoreData), 0, sizeof(J9SharedClassJavacoreDataDescriptor));

	result->javacoreData.feature = getJVMFeature(vm);
	
	bool isCompatibleLowerLayer = (result->isCompatible) && (!isTopLayer);

	if (SHR_STATS_REASON_ITERATE == reason) {
		if (isCompatibleLowerLayer && !getLowerLayerStats) {
			/* SH_OSCachemmap::getNonTopLayerCacheInfo() and SH_OSCachesysv::getNonTopLayerCacheInfo() are only called when iterating the caches. 
			 * So check isCompatibleLowerLayer and getLowerLayerStats only when reason is SHR_STATS_REASON_ITERATE. */
			return 0;
		}
	}

	if (result->versionData.cacheType == J9PORT_SHR_CACHE_TYPE_PERSISTENT) {
		if (isCompatibleLowerLayer && getLowerLayerStats) {
			Trc_SHR_Assert_True(NULL != oscache);
			rc = SH_OSCachemmap::getNonTopLayerCacheInfo(vm, ctrlDirName, groupPerm, cacheNameWithVGen, result, reason, (SH_OSCachemmap*)oscache);
		} else {
			Trc_SHR_OSC_getCacheStatistics_stattingMmap();
			rc = SH_OSCachemmap::getCacheStats(vm, ctrlDirName, groupPerm, cacheNameWithVGen, result, reason, lowerLayerList);
		}
	} else if (result->versionData.cacheType == J9PORT_SHR_CACHE_TYPE_NONPERSISTENT) {
		if (isCompatibleLowerLayer && getLowerLayerStats) {
			Trc_SHR_Assert_True(NULL != oscache);
			rc = SH_OSCachesysv::getNonTopLayerCacheInfo(vm, ctrlDirName, groupPerm, cacheNameWithVGen, result, reason, (SH_OSCachesysv*)oscache);
		} else {
			Trc_SHR_OSC_getCacheStatistics_stattingSysv();
			rc = SH_OSCachesysv::getCacheStats(vm, ctrlDirName, groupPerm, cacheNameWithVGen, result, reason, lowerLayerList);
		}
	} else if (J9PORT_SHR_CACHE_TYPE_SNAPSHOT == result->versionData.cacheType) {
		Trc_SHR_OSC_getCacheStatistics_stattingSnapshot();
		if (0 == removeCacheVersionAndGen(result->name, CACHE_ROOT_MAXLEN, J9SH_VERSION_STRING_LEN + 1, cacheNameWithVGen)) {
			result->lastattach = J9SH_OSCACHE_UNKNOWN;
			result->lastdetach = J9SH_OSCACHE_UNKNOWN;
			result->createtime = J9SH_OSCACHE_UNKNOWN;
			result->os_shmid = (UDATA)J9SH_OSCACHE_UNKNOWN;
			result->os_semid = (UDATA)J9SH_OSCACHE_UNKNOWN;
			result->nattach = (UDATA)J9SH_OSCACHE_UNKNOWN;
			result->isCorrupt = (UDATA)J9SH_OSCACHE_UNKNOWN;
			/* rc is initialized to -1. If reach this point, getCacheStatistics() is successful for snapshot, set rc to 0 */
			rc = 0;
		}
	} else {
		/* TODO: No need for vmem */
	}

	Trc_SHR_OSC_getCacheStatistics_Exit(rc);
	return rc;
}


/* THREADING: Pre-req the cache header must be locked */
void
SH_OSCache::initOSCacheHeader(OSCache_header_version_current* header, J9PortShcVersion* versionData, UDATA headerLen)
{
	Trc_SHR_OSC_initOSCacheHeader_Entry(header, versionData, headerLen);
	PORT_ACCESS_FROM_PORT(_portLibrary);
	UDATA success = 0;

	memcpy(&(header->versionData), versionData, sizeof(J9PortShcVersion));
	header->size = (U_32)_cacheSize;
	SRP_SET(header->dataStart, _dataStart);
	header->dataLength = (U_32)_dataLength;
	header->generation = (U_32)_activeGeneration;
	header->buildID = getOpenJ9Sha();
	header->cacheInitComplete = 0;
	header->createTime = j9time_current_time_nanos(&success);

	Trc_SHR_OSC_initOSCacheHeader_Exit();
}

/* THREADING: Pre-req the cache header must be locked */
IDATA
SH_OSCache::checkOSCacheHeader(OSCache_header_version_current* header, J9PortShcVersion* versionData, UDATA headerLen)
{
	UDATA dataStartValue;
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_checkOSCacheHeader_Entry(header, versionData, headerLen);

	if (versionData == NULL) {
		/* If it's not the current generation, the cache header will simply not look right and can't be verified */
		if (header->generation != (U_32)_activeGeneration) {
			Trc_SHR_OSC_checkOSCacheHeader_differentGeneration();
			return J9SH_OSCACHE_HEADER_OK;
		}
	} else {
		/* Check whether the version data contained in the header indeed matches the platform
		 * specifications of the current target.
		 */

		if (memcmp(versionData, &(header->versionData), sizeof(J9PortShcVersion)) != 0) {
			Trc_SHR_OSC_checkOSCacheHeader_wrongVersion();
			return J9SH_OSCACHE_HEADER_WRONG_VERSION;
		}
	}

	if (header->dataLength != (header->size - headerLen)) {
		Trc_SHR_OSC_checkOSCacheHeader_wrongDataLength();
		OSC_ERR_TRACE1(J9NLS_SHRC_OSCACHE_CORRUPT_CACHE_HEADER_INCORRECT_DATA_LENGTH, header->dataLength);
		setCorruptionContext(CACHE_HEADER_INCORRECT_DATA_LENGTH, (UDATA)header->dataLength);
		return J9SH_OSCACHE_HEADER_CORRUPT;
	}

	dataStartValue = (UDATA)SRP_GET(header->dataStart, U_8*);
	if (dataStartValue != ((UDATA)_headerStart + headerLen)) {
		Trc_SHR_OSC_checkOSCacheHeader_wrongDataStartValue();
		OSC_ERR_TRACE1(J9NLS_SHRC_OSCACHE_CORRUPT_CACHE_HEADER_INCORRECT_DATA_START_ADDRESS, dataStartValue);
		setCorruptionContext(CACHE_HEADER_INCORRECT_DATA_START_ADDRESS, dataStartValue);
		return J9SH_OSCACHE_HEADER_CORRUPT;
	}

	if ((J9_ARE_ALL_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID))
		&& (J9_ARE_NO_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_SNAPSHOT))
	) {
		Trc_SHR_OSC_checkOSCacheHeader_testBadBuildID();
		/* The flag J9SHR_RUNTIMEFLAG_ENABLE_TEST_BAD_BUILDID is unset after
		 * the first call to SH_OSCache:startup() in SH_CompositeCacheImpl::startup().
		 * This ensures that this failure only occurs once. Running "snapshotCache"
		 * with "badBuildID" means to write a bad build ID into the snapshot, rather than running
		 * badBuildID testing on the original cache.
		 */
		return J9SH_OSCACHE_HEADER_DIFF_BUILDID;
	}

	U_64 OpenJ9Sha = getOpenJ9Sha();
	if (_doCheckBuildID && (header->buildID != OpenJ9Sha)) {
		Trc_SHR_OSC_checkOSCacheHeader_wrongBuildID_3(OpenJ9Sha, header->buildID);
		if (J9_ARE_ALL_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_RESTORE_CHECK)) {
			OSC_ERR_TRACE(J9NLS_SHRC_OSCACHE_CORRUPT_CACHE_HEADER_INCORRECT_BUILDID);
		}
		return J9SH_OSCACHE_HEADER_DIFF_BUILDID;
	}

	Trc_SHR_OSC_checkOSCacheHeader_Exit();
	return J9SH_OSCACHE_HEADER_OK;
}

bool
SH_OSCache::isRunningReadOnly() {
	return _runningReadOnly;
}

/* Used for connecting to legacy cache headers */
void*
SH_OSCache::getHeaderFieldAddressForGen(void* header, UDATA headerGen, UDATA fieldID)
{
	return (U_8*)header + getHeaderFieldOffsetForGen(headerGen, fieldID);
}

/* Used for connecting to legacy cache headers */
IDATA
SH_OSCache::getHeaderFieldOffsetForGen(UDATA headerGen, UDATA fieldID)
{
	if ((4 < headerGen) && (headerGen <= OSCACHE_CURRENT_CACHE_GEN)) {
		switch (fieldID) {
		case OSCACHE_HEADER_FIELD_SIZE :
			return offsetof(OSCache_header_version_current, size);
		case OSCACHE_HEADER_FIELD_DATA_START :
			return offsetof(OSCache_header_version_current, dataStart);
		case OSCACHE_HEADER_FIELD_DATA_LENGTH :
			return offsetof(OSCache_header_version_current, dataLength);
		case OSCACHE_HEADER_FIELD_GENERATION :
			return offsetof(OSCache_header_version_current, generation);
		case OSCACHE_HEADER_FIELD_BUILDID :
			return offsetof(OSCache_header_version_current, buildID);
		case OSCACHE_HEADER_FIELD_CACHE_INIT_COMPLETE :
			return offsetof(OSCache_header_version_current, cacheInitComplete);
		default :
			/* Should never happen */
			break;
		}
	} else if (4 == headerGen) {
		switch (fieldID) {
		case OSCACHE_HEADER_FIELD_SIZE :
			return offsetof(OSCache_header_version_G04, size);
		case OSCACHE_HEADER_FIELD_DATA_START :
			return offsetof(OSCache_header_version_G04, dataStart);
		case OSCACHE_HEADER_FIELD_DATA_LENGTH :
			return offsetof(OSCache_header_version_G04, dataLength);
		case OSCACHE_HEADER_FIELD_GENERATION :
			return offsetof(OSCache_header_version_G04, generation);
		case OSCACHE_HEADER_FIELD_BUILDID :
			return offsetof(OSCache_header_version_G04, buildID);
		case OSCACHE_HEADER_FIELD_CACHE_INIT_COMPLETE :
			return offsetof(OSCache_header_version_G04, cacheInitComplete);
		default :
			/* Should never happen */
			break;
		}
	} else if (3 == headerGen) {
		switch (fieldID) {
		case OSCACHE_HEADER_FIELD_SIZE :
			return offsetof(OSCache_header_version_G03, size);
		case OSCACHE_HEADER_FIELD_DATA_START :
			return offsetof(OSCache_header_version_G03, dataStart);
		case OSCACHE_HEADER_FIELD_DATA_LENGTH :
			return offsetof(OSCache_header_version_G03, dataLength);
		case OSCACHE_HEADER_FIELD_GENERATION :
			return offsetof(OSCache_header_version_G03, generation);
		case OSCACHE_HEADER_FIELD_BUILDID :
			return offsetof(OSCache_header_version_G03, buildID);
		default :
			/* Should never happen */
			break;
		}
	}
	Trc_SHR_Assert_ShouldNeverHappen();
	return 0;
}

/**
 * Returns the amount of bytes in the cache available for data
 *
 * @return the bytes available for data
 */
U_32
SH_OSCache::getDataSize()
{
	return (U_32)_dataLength;
}

/**
 * Generate a U_64 from the major, and minor numbers from a cache file name
 *
 * @return U_64
 */
U_64
SH_OSCache::getCacheVersionToU64(U_32 major, U_32 minor)
{
	U_64 retval = 0;
	/*Copy 'major' into a U_64 to avoid shift operator below exceeding type width*/
	U_64 _major = major;
	retval = (_major<<32) + minor;
	return retval;
}

/**
 * Get statistics on a given named cache
 *
 * @param[in] vm  The Java VM
 * @parma[in] ctrlDirName The cache directory
 * @param[in] groupPerm Group permissions to open the cache directory
 * @param[in] cache The OSCache to use
 * @param[in] cacheInfo Where to put statistics about this Cache
 * @param[out] cacheInfo A list of SH_OSCache_Info for all lower layer caches.
 *
 * @return true if the operations has been successful, false if an error has occured
 */
bool
SH_OSCache::getCacheStatsCommon(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, SH_OSCache *cache, SH_OSCache_Info *cacheInfo, J9Pool **lowerLayerList)
{
	bool retval = true;
	UDATA bytesRequired = 0;
	void * cmPtr = NULL;
	SH_CacheMapStats * cmStats = NULL;
	IDATA startedForStats = CC_STARTUP_FAILED;
	U_64 runtimeflags = 0;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (!(cacheInfo->isCompatible)) {
		retval = false;
		goto done;
	}

	bytesRequired = SH_CacheMap::getRequiredConstrBytes(true);

	cmPtr = (void *) j9mem_allocate_memory(bytesRequired, J9MEM_CATEGORY_CLASSES);
	if (NULL == cmPtr) {
		retval = false;
		goto done;
	}

	memset(cmPtr, 0, bytesRequired);

	cmStats = SH_CacheMap::newInstanceForStats(vm, (SH_CacheMap* )cmPtr, cacheInfo->name, cacheInfo->layer);
	if (cmStats == NULL) {
		retval = false;
		goto done;
	}

	startedForStats = cmStats->startupForStats(currentThread, ctrlDirName, groupPerm, cache, &runtimeflags, lowerLayerList);

	if (startedForStats != CC_STARTUP_OK) {
		if (startedForStats == CC_STARTUP_CORRUPT) {
			cacheInfo->isCorrupt = 1;
		}
		retval = false;
		goto done;
	}

	if (cmStats->getJavacoreData(vm, &(cacheInfo->javacoreData)) == 1) {
		cacheInfo->isJavaCorePopulated = 1;
	}

done:
	/*If startedForStats != CC_STARTUP_OK then shutdownForStats was already called by startupForStats in the failure condition*/
	if ((cmStats != NULL) && (startedForStats == CC_STARTUP_OK)) {
		cmStats->shutdownForStats(currentThread);
	}

	if (cmPtr != NULL) {
		j9mem_free_memory(cmPtr);
	}
	return retval;

}

void
SH_OSCache::setCorruptionContext(IDATA corruptionCode, UDATA corruptValue)
{
	_corruptionCode = corruptionCode;
	_corruptValue = corruptValue;
}

/**
 * Get the corruption context (corruption code and corrupt value).
 * Value returned is interpreted on the basis of _commonCCInfo->corruptionCode :
 * if corruptionCode = CACHE_CRC_INVALID, corruptValue contains cache CRC returned by getCacheCRC().
 * if corruptionCode = ROM_CLASS_CORRUPT, corruptValue contains address of corrupt ROMClass.
 * if corruptionCode = ITEM_TYPE_CORRUPT, corruptValue contains address of corrupt cache entry.
 * if corruptionCode = ITEM_LENGTH_CORRUPT, corruptValue contains address of corrupt cache entry header.
 * if corruptionCode = CACHE_HEADER_BAD_TARGET_SPECIFIED, corruptValue contains value of target field in cache header.
 * if corruptionCode = CACHE_HEADER_INCORRECT_DATA_LENGTH, corruptValue contains data length stored in cache header.
 * if corruptionCode = CACHE_HEADER_INCORRECT_DATA_START_ADDRESS, corruptValue is the address of cache data.
 * if corruptionCode = CACHE_HEADER_BAD_EYECATCHER, corruptValue is the address of eye-catcher string in cache header.
 * if corruptionCode = CACHE_HEADER_INCORRECT_CACHE_SIZE, corruptValue is the cache size stored in cache header.
 * if corruptionCode = ACQUIRE_HEADER_WRITE_LOCK_FAILED, corruptValue contains the error code for the failure to acquire lock.
 * if corruptionCode = CACHE_SIZE_INVALID, corruptValue contains invalid cache size.
 *
 * @param [out] corruptionCode  if non-null, it is populated with a code indicating corruption type.
 * @param [out] corruptValue    if non-null, it is populated with a value to be interpreted depending on corruptionCode.
 *
 * @return void
 */
void
SH_OSCache::getCorruptionContext(IDATA *corruptionCode, UDATA *corruptValue) {
	if (NULL != corruptionCode) {
		*corruptionCode = _corruptionCode;
	}
	if (NULL != corruptValue) {
		*corruptValue = _corruptValue;
	}
}

/**
 * Get the address of the first byte of a shared memory, or memory mapped file.
 *
 * @return address of byte 0
 */
void *
SH_OSCache::getOSCacheStart() {
	return _headerStart;
}

/**
 * Get the unique ID of the current cache
 * @param[in] currentThread  The current VM thread.
 * @param [in] createtime The cache create time which is stored in OSCache_header2.
 * @param[in] metadataBytes  The size of the metadata section of current oscache.
 * @param[in] classesBytes  The size of the classes section of current oscache.
 * @param[in] lineNumTabBytes  The size of the line number table section of current oscache.
 * @param[in] varTabBytes  The size of the variable table section of current oscache.
 *
 * @return the cache unique ID
 */
const char*
SH_OSCache::getCacheUniqueID(J9VMThread* currentThread, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	if (NULL != _cacheUniqueID) {
		return _cacheUniqueID;
	}
	Trc_SHR_Assert_True(NULL != _cacheDirName);
	Trc_SHR_Assert_True(NULL != _cacheName);

	U_32 cacheType = J9_ARE_ALL_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE) ? J9PORT_SHR_CACHE_TYPE_PERSISTENT : J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
	UDATA sizeRequired = generateCacheUniqueID(currentThread, _cacheDirName, _cacheName, _layer, cacheType, NULL, 0, createtime, metadataBytes, classesBytes, lineNumTabBytes,varTabBytes);

	_cacheUniqueID = (char*)j9mem_allocate_memory(sizeRequired, J9MEM_CATEGORY_VM);
	if (NULL == _cacheUniqueID) {
		return NULL;
	}
	generateCacheUniqueID(currentThread, _cacheDirName, _cacheName, _layer, cacheType, _cacheUniqueID, sizeRequired, createtime, metadataBytes, classesBytes, lineNumTabBytes, varTabBytes);
	return _cacheUniqueID;
}

/**
 * Generate a cache unique ID
 * @param[in] currentThread  The current VM thread.
 * @param[in] cacheDirName  The cache directory
 * @param[in] cacheName  The cache name
 * @param[in] layer  The cache layer number
 * @param[in] cacheType  The cache type
 * @param[out] buf  The buffer for the cache unique ID
 * @param[out] bufLen  The length of the buffer
 * @param[in] createtime The cache create time which is stored in OSCache_header2.
 * @param[in] metadataBytes  The size of the metadata section of current oscache.
 * @param[in] classesBytes  The size of the classes section of current oscache.
 * @param[in] lineNumTabBytes  The size of the line number table section of current oscache.
 * @param[in] varTabBytes  The size of the variable table section of current oscache.
 *
 * @return If buf is not NULL, the number of characters printed into buf is returned, not including the NUL terminator.
 *          If buf is NULL, the size of the buffer required to print to the unique ID, including the NUL terminator is returned.
 */
UDATA
SH_OSCache::generateCacheUniqueID(J9VMThread* currentThread, const char* cacheDirName, const char* cacheName, I_8 layer, U_32 cacheType, char* buf, UDATA bufLen, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes)
{
	char nameWithVGen[J9SH_MAXPATH];
	char cacheFilePathName[J9SH_MAXPATH];
	J9JavaVM* vm = currentThread->javaVM;
	PORT_ACCESS_FROM_VMC(currentThread);

	J9PortShcVersion versionData;
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	versionData.cacheType = cacheType;

	getCacheVersionAndGen(PORTLIB, vm, nameWithVGen, sizeof(nameWithVGen), cacheName, &versionData, OSCACHE_CURRENT_CACHE_GEN, true, layer);
	/* Directory is included here, so if the cache directory is renamed, caches with layer > 0 becomes unusable. */
	getCachePathName(PORTLIB, cacheDirName, cacheFilePathName, sizeof(cacheFilePathName), nameWithVGen);
	/*
	 * Use a format with fixed field widths so the length of the unique id does not
	 * depend on things like the length of the lower layer cache file.
	 */
#if defined(J9VM_ENV_DATA64)
	const char * const format = "%s-%016llx_%016llx_%016zx_%016zx_%016zx_%016zx";
#else
	const char * const format = "%s-%016llx_%016llx_%08zx_%08zx_%08zx_%08zx";
#endif
	I_64 fileSize = j9file_length(cacheFilePathName);
	if (NULL != buf) {
		UDATA bufLenRequired = j9str_printf(PORTLIB, NULL, 0, format, cacheFilePathName, fileSize, createtime, metadataBytes, classesBytes, lineNumTabBytes, varTabBytes);
		Trc_SHR_Assert_True(bufLenRequired <= bufLen);
	}
	return j9str_printf(PORTLIB, buf, bufLen, format, cacheFilePathName, fileSize, createtime, metadataBytes, classesBytes, lineNumTabBytes, varTabBytes);
}

/**
 * Get the cache name and layer number from the unique cache ID.
 * @param[in] vm  The Java VM.
 * @param[in] uniqueID  The cache unique ID
 * @param[in] idLen  The length of the cache unique ID
 * @param[out] nameBuf  The buffer for the cache name
 * @param[out] nameBuffLen  The length of cache name buffer
 * @param[out] layer  The layer number in the cache unique ID
 */
void
SH_OSCache::getCacheNameAndLayerFromUnqiueID(J9JavaVM* vm, const char* uniqueID, UDATA idLen, char* nameBuf, UDATA nameBuffLen, I_8* layer)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	char versionStr[J9SH_VERSION_STRING_LEN +3];
	J9PortShcVersion versionData;
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	J9SH_GET_VERSION_STRING(PORTLIB, versionStr, J9SH_VERSION(versionData.esVersionMajor, versionData.esVersionMinor), versionData.modlevel, versionData.feature, versionData.addrmode);
	const char* cacheNameWithVGenStart = strstr(uniqueID, versionStr);
	char* cacheNameWithVGenEnd = strnrchrHelper(cacheNameWithVGenStart, '-', idLen - (cacheNameWithVGenStart - uniqueID));
	if (NULL == cacheNameWithVGenStart || NULL == cacheNameWithVGenEnd) {
		Trc_SHR_Assert_ShouldNeverHappen();
	}

	char nameWithVGenCopy[J9SH_MAXPATH];
	memset(nameWithVGenCopy, 0, J9SH_MAXPATH);
	strncpy(nameWithVGenCopy, cacheNameWithVGenStart, cacheNameWithVGenEnd - cacheNameWithVGenStart);

	getValuesFromShcFilePrefix(PORTLIB, nameWithVGenCopy, &versionData);
	UDATA prefixLen = J9SH_VERSION_STRING_LEN + 1;
	/* 
	 * For example, a persistent cache file is C290M11F1A64P_Cache1_G41L00. The prefix size is the length of "C290M11F1A64P"(J9SH_VERSION_STRING_LEN) + the length of "_"(1), which is J9SH_VERSION_STRING_LEN + 1
	 * A non-persistent cache
	 * 		On Windows, a cache file is C290M11F1A64_Cache1_G41L00, the prefix size is the length of "C290M11F1A64"(J9SH_VERSION_STRING_LEN - 1) + the length of "_"(1), which is J9SH_VERSION_STRING_LEN.
	 * 		On non-Windows, a cache file is C290M11F1A64_memory_Cache1_G41L00, the prefix size is the length of "C290M11F1A64"(J9SH_VERSION_STRING_LEN - 1) + the length of "_memory_"(strlen(J9SH_MEMORY_ID)), 
	 * 		which is J9SH_VERSION_STRING_LEN + strlen(J9SH_MEMORY_ID) - 1.
	 */
	if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == versionData.cacheType) {
#if defined(WIN32)
		prefixLen = J9SH_VERSION_STRING_LEN;
#else
		prefixLen = J9SH_VERSION_STRING_LEN + strlen(J9SH_MEMORY_ID) - 1;
#endif
	}

	SH_OSCache::removeCacheVersionAndGen(nameBuf, nameBuffLen, prefixLen, nameWithVGenCopy);
	I_8 layerNo = getLayerFromName(nameWithVGenCopy);
	Trc_SHR_Assert_True(((layerNo >= 0)
						&& (layerNo <= J9SH_LAYER_NUM_MAX_VALUE))
						);
	*layer = layerNo;
}

/*
 * Return the layer number.
 */
I_8
SH_OSCache::getLayer()
{
	return _layer;
}

/* 
 * Return the cache type.
 */
U_32
SH_OSCache::getCacheType()
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	J9PortShcVersion versionData;
	getValuesFromShcFilePrefix(PORTLIB, _cacheNameWithVGen, &versionData);
		
	return versionData.cacheType;
}

/* 
 * Return the cache file name.
 */
const char*
SH_OSCache::getCacheNameWithVGen()
{
	return _cacheNameWithVGen;
}

/*
 * Returns if the cache is top layer or not.
 * 
 * @param[in] vm  The Java VM.
 * @param[in] ctrlDirName  The cache control directory
 * @param[in] nameWithVGen  The cache file name
 */
bool
SH_OSCache::isTopLayerCache(J9JavaVM* vm, const char* ctrlDirName, char* nameWithVGen)
{
	bool rc = true;
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9PortShcVersion versionData;
	char cacheName[CACHE_ROOT_MAXLEN];
	char cacheNameWithVGen[J9SH_MAXPATH];
	char cacheDirName[J9SH_MAXPATH];
	UDATA prefixLen = J9SH_VERSION_STRING_LEN + 1;
		
	if (0 == getValuesFromShcFilePrefix(PORTLIB, nameWithVGen, &versionData)) {
		goto done;
	}
	/* 
	 * For example, a persistent cache file is C290M11F1A64P_Cache1_G41L00. The prefix size is the length of "C290M11F1A64P"(J9SH_VERSION_STRING_LEN) + the length of "_"(1), which is J9SH_VERSION_STRING_LEN + 1.
	 * A non-persistent cache
	 * 		On Windows, a cache file is C290M11F1A64_Cache1_G41L00, the prefix size is the length of "C290M11F1A64"(J9SH_VERSION_STRING_LEN - 1) + the length of "_"(1), which is J9SH_VERSION_STRING_LEN.
	 * 		On non-Windows, a cache file is C290M11F1A64_memory_Cache1_G41L00, the prefix size is the length of "C290M11F1A64"(J9SH_VERSION_STRING_LEN - 1) + the length of "_memory_"(strlen(J9SH_MEMORY_ID)), 
	 * 		which is J9SH_VERSION_STRING_LEN + strlen(J9SH_MEMORY_ID) - 1.
	 */
	if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == versionData.cacheType) {
#if defined(WIN32)
		prefixLen = J9SH_VERSION_STRING_LEN;
#else
		prefixLen = J9SH_VERSION_STRING_LEN + strlen(J9SH_MEMORY_ID) - 1;
#endif
	}
	if (0 != removeCacheVersionAndGen(cacheName, CACHE_ROOT_MAXLEN, prefixLen, nameWithVGen)) {
		goto done;
	}
	
	getCacheVersionAndGen(PORTLIB, vm, cacheNameWithVGen, J9SH_MAXPATH, cacheName, &versionData, getGenerationFromName(nameWithVGen), true, getLayerFromName(nameWithVGen) + 1);
	getCacheDir(vm, ctrlDirName, cacheDirName, J9SH_MAXPATH, versionData.cacheType);
	
	if (1 == statCache(PORTLIB, cacheDirName, cacheNameWithVGen, false)) {
		rc = false;
	}
done:
	return rc;
}

const char*
SH_OSCache::getCacheFullPathName() const
{
	return _cachePathName;
}



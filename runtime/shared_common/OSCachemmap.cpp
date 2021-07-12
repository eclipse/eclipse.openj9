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
#include "j2sever.h"
#include "j9cfg.h"
#include "j9port.h"
#include "pool_api.h"
#include "ut_j9shr.h"
#include "j9shrnls.h"
#include "util_api.h"

#include "OSCachemmap.hpp"
#include "CompositeCacheImpl.hpp"
#include "UnitTest.hpp"
#include "CacheMap.hpp"

#define MMAP_CACHEDATASIZE(size) (size - MMAP_CACHEHEADERSIZE)

#define RETRY_OBTAIN_WRITE_LOCK_SLEEP_NS 100000
#define RETRY_OBTAIN_WRITE_LOCK_MAX_MS 160
#define NANOSECS_PER_MILLISEC (I_64)1000000

/**
 * Multi-argument constructor
 *
 * Constructs and initializes a SH_OSCachemmap object and calls startup to open/create
 * a shared classes cache.
 * This c'tor is currently used during unit testing only. Therefore we pass J9SH_DIRPERM_ABSENT as cacheDirPerm to startup().
 *
 * @param [in]  portLibrary The Port library
 * @param [in]  cacheName The name of the cache to be opened/created
 * @param [in]  piconfig Pointer to a configuration structure
 * @param [in]  numLocks The number of locks to be initialized
 * @param [in]  createFlag Indicates whether cache is to be opened or created.
 * \args J9SH_OSCACHE_CREATE Create the cache if it does not exists, otherwise open existing cache
 * \args J9SH_OSCACHE_OPEXIST Open an existing cache only, failed if it doesn't exist.
 * @param [in]  verboseFlags Verbose flags
 * @param [in]  openMode Mode to open the cache in. Any of the following flags:
 * \args J9OSCACHE_OPEN_MODE_DO_READONLY - open the cache readonly
 * \args J9OSCACHE_OPEN_MODE_TRY_READONLY_ON_FAIL - if the cache could not be opened read/write - try readonly
 * \args J9OSCACHE_OPEN_MODE_GROUPACCESS - creates a cache with group access. Only applies when a cache is created
 * \args J9OSCACHE_OPEN_MODE_CHECK_NETWORK_CACHE - checks whether we are attempting to connect to a networked cache
 * @param [in]  versionData Version data of the cache to connect to
 * @param [in]  initializer Pointer to an initializer to be used to initialize the data
 *              area of a new cache
 */
SH_OSCachemmap::SH_OSCachemmap(J9PortLibrary* portLibrary, J9JavaVM* vm, const char* cacheDirName, const char* cacheName, J9SharedClassPreinitConfig* piconfig,
		IDATA numLocks, UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, J9PortShcVersion* versionData, SH_OSCacheInitializer* initializer)
{
	Trc_SHR_OSC_Mmap_Constructor_Entry(cacheName, piconfig->sharedClassCacheSize, numLocks, createFlag, verboseFlags);
	initialize(portLibrary, NULL, OSCACHE_CURRENT_CACHE_GEN, OSCACHE_CURRENT_LAYER_LAYER);
	startup(vm, cacheDirName, J9SH_DIRPERM_ABSENT, cacheName, piconfig, numLocks, createFlag, verboseFlags, runtimeFlags, openMode, 0, versionData, initializer, SHR_STARTUP_REASON_NORMAL);
	Trc_SHR_OSC_Mmap_Constructor_Exit();
}

/**
 * Method to initialize object variables.  This is outside the constructor for
 * consistency with SH_OSCachesysv
 * Note:  This method is public as it is called by the factory method newInstance in SH_OSCache
 *
 * @param [in]  portLibraryArg The Port library
 * @param [in]  memForConstructorArg Pointer to the memory to build the OSCachemmap into
 * @param [in]  generation The generation of this cache
 * @param [in]  layer The layer number of this cache
 */
void
SH_OSCachemmap::initialize(J9PortLibrary* portLibrary, char* memForConstructor, UDATA generation, I_8 layer)
{
	Trc_SHR_OSC_Mmap_initialize_Entry(portLibrary, memForConstructor);
	commonInit(portLibrary, generation, layer);
	_fileHandle = -1;
	_actualFileLength = 0;
	_finalised = 0;
	_mapFileHandle = NULL;
	for (UDATA i = 0; i < J9SH_OSCACHE_MMAP_LOCK_COUNT; i++) {
		_lockMutex[i] = NULL;
	}
	_corruptionCode = NO_CORRUPTION;
	_corruptValue = NO_CORRUPTION;
	_cacheFileAccess = J9SH_CACHE_FILE_ACCESS_ALLOWED;
	Trc_SHR_OSC_Mmap_initialize_Exit();
}

/**
 * Method to free resources and re-initialize variables when
 * cache is no longer required
 */
void
SH_OSCachemmap::finalise()
{
	Trc_SHR_OSC_Mmap_finalise_Entry();

	commonCleanup();
	_fileHandle = -1;
	_actualFileLength = 0;
	_finalised = 1;
	_mapFileHandle = NULL;
	for (UDATA i = 0; i < J9SH_OSCACHE_MMAP_LOCK_COUNT; i++) {
		if(NULL != _lockMutex[i]) {
			omrthread_monitor_destroy(_lockMutex[i]);
		}
	}
	Trc_SHR_OSC_Mmap_finalise_Exit();
}

/**
 * Method to create or open a persistent shared classes cache
 * Should be able to successfully start up a cache on any version or generation
 *
 * @param [in]  portLibrary The Port library
 * @param [in]  cacheName The name of the cache to be opened/created
 * @param [in]  cacheDirName The directory for the cache file
 * @param [in]  piconfig Pointer to a configuration structure
 * @param [in]  numLocks The number of locks to be initialized
 * @param [in]  createFlag Indicates whether cache is to be opened or created.
 *              Included for consistency with SH_OSCachesysv, but need to open or create is
 *              determined by logic within this class
 * @param [in]  verboseFlags Verbose flags
 * @param [in]  openMode Mode to open the cache in. Any of the following flags:
 * \args J9OSCACHE_OPEN_MODE_DO_READONLY - open the cache readonly
 * \args J9OSCACHE_OPEN_MODE_TRY_READONLY_ON_FAIL - if the cache could not be opened read/write - try readonly
 * \args J9OSCACHE_OPEN_MODE_GROUPACCESS - creates a cache with group access. Only applies when a cache is created
 * \args J9OSCACHE_OPEN_MODE_CHECK_NETWORK_CACHE - checks whether we are attempting to connect to a networked cache
 * @param [in]  versionData Version data of the cache to connect to
 * @param [in]  initializer Pointer to an initializer to be used to initialize the data
 *              area of a new cache
 * @param [in]  reason Reason for starting up the cache. Used only when startup is called during destroy
 *
 * @return true on success, false on failure
 */
bool
SH_OSCachemmap::startup(J9JavaVM* vm, const char* ctrlDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig, IDATA numLocks,
		UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, UDATA storageKeyTesting, J9PortShcVersion* versionData, SH_OSCacheInitializer* initializer, UDATA reason)
{
	I_32 mmapCapabilities;
	IDATA retryCntr;
	bool creatingNewCache = false;
	struct J9FileStat statBuf;
	IDATA errorCode = J9SH_OSCACHE_FAILURE;
	LastErrorInfo lastErrorInfo;
	UDATA defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE;

#if defined(J9VM_ENV_DATA64)
#if defined(OPENJ9_BUILD)
	defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE_64BIT_PLATFORM;
#else /* OPENJ9_BUILD */
	if (J2SE_VERSION(vm) >= J2SE_V11) {
		defaultCacheSize = J9_SHARED_CLASS_CACHE_DEFAULT_SIZE_64BIT_PLATFORM;
	}
#endif /* OPENJ9_BUILD */
#endif /* J9VM_ENV_DATA64 */

	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_Mmap_startup_Entry(cacheName, ctrlDirName,
		(piconfig!= NULL)? piconfig->sharedClassCacheSize : defaultCacheSize,
		numLocks, createFlag, verboseFlags, openMode);

	versionData->cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
	mmapCapabilities = j9mmap_capabilities();
	if (!(mmapCapabilities & J9PORT_MMAP_CAPABILITY_WRITE) || !(mmapCapabilities & J9PORT_MMAP_CAPABILITY_MSYNC)) {
		Trc_SHR_OSC_Mmap_startup_nommap(mmapCapabilities);
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_MMAPCAP, NULL);
		goto _errorPreFileOpen;
	}

	if (commonStartup(vm, ctrlDirName, cacheDirPerm, cacheName, piconfig, createFlag, verboseFlags, runtimeFlags, openMode, versionData) != 0) {
		Trc_SHR_OSC_Mmap_startup_commonStartupFailure();
		goto _errorPreFileOpen;
	}
	Trc_SHR_OSC_Mmap_startup_commonStartupSuccess();

	/* Detect remote filesystem */
	if (openMode & J9OSCACHE_OPEN_MODE_CHECK_NETWORK_CACHE) {
		if (0 == j9file_stat(_cacheDirName, 0, &statBuf)) {
			if (statBuf.isRemote) {
				Trc_SHR_OSC_Mmap_startup_detectedNetworkCache();
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_NETWORK_CACHE, NULL);
				goto _errorPreFileOpen;
			}
		}
	}

	/* Open the file */
	if (!openCacheFile(_createFlags & J9SH_OSCACHE_CREATE, &lastErrorInfo)) {
		Trc_SHR_OSC_Mmap_startup_badfileopen(_cachePathName);
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_FILEOPEN_ERROR, &lastErrorInfo); /* TODO: ADD FILE NAME */
		goto _errorPostFileOpen;
	}
	Trc_SHR_OSC_Mmap_startup_goodfileopen(_cachePathName, _fileHandle);

	/* Avoid any checks for cache file access if
	 * - user has specified a cache directory, or
	 * - destroying an existing cache (if SHR_STARTUP_REASON_DESTROY or SHR_STARTUP_REASON_EXPIRE or J9SH_OSCACHE_OPEXIST_DESTROY is set)
	 */
	if (!_isUserSpecifiedCacheDir
		&& (J9_ARE_NO_BITS_SET(_createFlags, J9SH_OSCACHE_OPEXIST_DESTROY))
		&& (SHR_STARTUP_REASON_DESTROY != reason)
		&& (SHR_STARTUP_REASON_EXPIRE != reason)
	) {
		_cacheFileAccess = checkCacheFileAccess(_portLibrary, _fileHandle, _openMode, &lastErrorInfo);

		if (J9_ARE_ALL_BITS_SET(_createFlags, J9SH_OSCACHE_OPEXIST_STATS)
			|| (J9SH_CACHE_FILE_ACCESS_ALLOWED == _cacheFileAccess)
		) {
			Trc_SHR_OSC_Mmap_startup_fileaccessallowed(_cachePathName);
		} else {
			switch (_cacheFileAccess) {
			case J9SH_CACHE_FILE_ACCESS_GROUP_ACCESS_REQUIRED:
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_GROUPACCESS_REQUIRED, NULL);
				goto _errorPostFileOpen;
				break;
			case J9SH_CACHE_FILE_ACCESS_OTHERS_NOT_ALLOWED:
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_OTHERS_ACCESS_NOT_ALLOWED, NULL);
				goto _errorPostFileOpen;
				break;
			case J9SH_CACHE_FILE_ACCESS_CANNOT_BE_DETERMINED:
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_INTERNAL_ERROR_CHECKING_CACHEFILE_ACCESS, &lastErrorInfo);
				goto _errorPostFileOpen;
				break;
			default:
				Trc_SHR_Assert_ShouldNeverHappen();
			}
		}
	}

	/* CMVC 177634: When destroying the cache, it is sufficient to open it.
	 * We should avoid doing any processing that can detect cache as corrupt.
	 */
	if (SHR_STARTUP_REASON_DESTROY == reason) {
		Trc_SHR_OSC_Mmap_startup_openCacheForDestroy(_cachePathName);
		goto _exitForDestroy;
	}

	for (UDATA i = 0; i < J9SH_OSCACHE_MMAP_LOCK_COUNT; i++) {
		if (omrthread_monitor_init_with_name(&_lockMutex[i], 0, "Persistent shared classes lock mutex")) {
			Trc_SHR_OSC_Mmap_startup_failed_mutex_init(i);
			goto _errorPostFileOpen;
		}
	}
	Trc_SHR_OSC_Mmap_startup_initialized_mutexes();

	/* Get cache header write lock */
	if (-1 == acquireHeaderWriteLock(_activeGeneration, &lastErrorInfo)) {
		Trc_SHR_OSC_Mmap_startup_badAcquireHeaderWriteLock();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ACQUIREHEADERWRITELOCK_ERROR, &lastErrorInfo);
		errorCode = J9SH_OSCACHE_CORRUPT;
		OSC_ERR_TRACE1(J9NLS_SHRC_OSCACHE_CORRUPT_ACQUIRE_HEADER_WRITE_LOCK_FAILED, lastErrorInfo.lastErrorCode);
		setCorruptionContext(ACQUIRE_HEADER_WRITE_LOCK_FAILED, (UDATA)lastErrorInfo.lastErrorCode);
		goto _errorPostHeaderLock;
	}
	Trc_SHR_OSC_Mmap_startup_goodAcquireHeaderWriteLock();

	/* Check the length of the file */
#if defined(WIN32) || defined(WIN64)
	if ((_cacheSize = (U_32)j9file_blockingasync_flength(_fileHandle)) > 0) {
#else
	if ((_cacheSize = (U_32)j9file_flength(_fileHandle)) > 0) {
#endif
		IDATA rc;
		/* We are opening an existing cache */
		Trc_SHR_OSC_Mmap_startup_fileOpened();

		if (_cacheSize <= sizeof(OSCachemmap_header_version_current)) {
			Trc_SHR_OSC_Mmap_startup_cacheTooSmall();
			errorCode = J9SH_OSCACHE_CORRUPT;
			OSC_ERR_TRACE1(J9NLS_SHRC_CC_STARTUP_CORRUPT_CACHE_SIZE_INVALID, _cacheSize);
			setCorruptionContext(CACHE_SIZE_INVALID, (UDATA)_cacheSize);
			goto _errorPostHeaderLock;
		}

		/* At this point, don't check the cache version - we need to attach to older versions in order to destroy */
		rc = internalAttach(false, _activeGeneration);
		if (0 != rc) {
			errorCode = rc;
			Trc_SHR_OSC_Mmap_startup_badAttach();
			goto _errorPostAttach;
		}

		if (_runningReadOnly) {
			retryCntr = 0;
			U_32* initCompleteAddr = (U_32*)getMmapHeaderFieldAddressForGen(_headerStart, _activeGeneration, OSCACHE_HEADER_FIELD_CACHE_INIT_COMPLETE);

			/* In readonly, we can't get a header lock, so if the cache is mid-init, give it a chance to complete initialization */
			while ((!*initCompleteAddr) && (retryCntr < J9SH_OSCACHE_READONLY_RETRY_COUNT)) {
				omrthread_sleep(J9SH_OSCACHE_READONLY_RETRY_SLEEP_MILLIS);
				++retryCntr;
			}
			if (!*initCompleteAddr) {
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_READONLY_CACHE_NOTINITIALIZED, NULL);
				Trc_SHR_OSC_Mmap_startup_cacheNotInitialized();
				goto _errorPostAttach;
			}
		}

		if (_verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE) {
			if (_runningReadOnly) {
				OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_OPENED_READONLY, _cacheName);
			} else {
				OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_OPENED, _cacheName);
			}
		}

	} else {
		OSCachemmap_header_version_current *cacheHeader;
		IDATA rc;

		creatingNewCache = true;

		/* File is wrong length, so we are creating the cache */
		Trc_SHR_OSC_Mmap_startup_fileCreated();

		/* We can't create the cache when we're running read-only */
		if (_runningReadOnly) {
			Trc_SHR_OSC_Mmap_startup_runningReadOnlyAndWrongLength();
			errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_OPENING_CACHE_READONLY, NULL);
			goto _errorPostHeaderLock;
		}

		/* Set cache to the correct length */
		if (!setCacheLength((U_32)piconfig->sharedClassCacheSize, &lastErrorInfo)) {
			Trc_SHR_OSC_Mmap_startup_badSetCacheLength(piconfig->sharedClassCacheSize);
			errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_SETTING_CACHE_LENGTH, &lastErrorInfo);
			goto _errorPostHeaderLock;
		}
		Trc_SHR_OSC_Mmap_startup_goodSetCacheLength(piconfig->sharedClassCacheSize);

		/* Verify if the group access has been set */
		if (J9_ARE_ALL_BITS_SET(_openMode, J9OSCACHE_OPEN_MODE_GROUPACCESS)) {
			I_32 groupAccessRc = verifyCacheFileGroupAccess(_portLibrary, _fileHandle, &lastErrorInfo);

			if (0 == groupAccessRc) {
				Trc_SHR_OSC_Mmap_startup_setGroupAccessFailed(_cachePathName);
				OSC_WARNING_TRACE(J9NLS_SHRC_OSCACHE_MMAP_SET_GROUPACCESS_FAILED);
			} else if (-1 == groupAccessRc) {
				/* Failed to get stats of the cache file */
				Trc_SHR_OSC_Mmap_startup_badFileStat(_cachePathName);
				errorHandler(J9NLS_SHRC_OSCACHE_ERROR_FILE_STAT, &lastErrorInfo);
				goto _errorPostHeaderLock;
			}
		}

		rc = internalAttach(true, _activeGeneration);
		if (0 != rc) {
			errorCode = rc;
			Trc_SHR_OSC_Mmap_startup_badAttach();
			goto _errorPostAttach;
		}

		cacheHeader = (OSCachemmap_header_version_current *)_headerStart;

		/* Create the cache header */
		if (!createCacheHeader(cacheHeader, versionData)) {
			Trc_SHR_OSC_Mmap_startup_badCreateCacheHeader();
			errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_CREATING_CACHE_HEADER, NULL);
			goto _errorPostAttach;
		}
		Trc_SHR_OSC_Mmap_startup_goodCreateCacheHeader();

		if (initializer) {
			if (!initializeDataHeader(initializer)) {
				Trc_SHR_OSC_Mmap_startup_badInitializeDataHeader();
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_INITIALISING_DATA_HEADER, NULL);
				goto _errorPostAttach;
			}
			Trc_SHR_OSC_Mmap_startup_goodInitializeDataHeader();
		}

		if (_verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE) {
			OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_CREATED, _cacheName);
		}
	}

	if (creatingNewCache) {
		OSCachemmap_header_version_current *cacheHeader = (OSCachemmap_header_version_current *)_headerStart;

		cacheHeader->oscHdr.cacheInitComplete = 1;
	}

	/* Detach the memory-mapped area */
	internalDetach(_activeGeneration);

	/* Release cache header write lock */
	if (0 != releaseHeaderWriteLock(_activeGeneration, &lastErrorInfo)) {
		Trc_SHR_OSC_Mmap_startup_badReleaseHeaderWriteLock();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_RELEASING_HEADER_WRITE_LOCK, &lastErrorInfo);
		goto _errorPostFileOpen;
	}
	Trc_SHR_OSC_Mmap_startup_goodReleaseHeaderWriteLock();

_exitForDestroy:
	_finalised = 0;
	_startupCompleted = true;

	Trc_SHR_OSC_Mmap_startup_Exit();
	return true;

_errorPostAttach :
	internalDetach(_activeGeneration);
_errorPostHeaderLock :
	releaseHeaderWriteLock(_activeGeneration, NULL);
_errorPostFileOpen :
	closeCacheFile();
	if (creatingNewCache) {
		deleteCacheFile(NULL);
	}
_errorPreFileOpen :
	setError(errorCode);
	return false;
}

/**
 * Returns if the cache is accessible by current user or not
 *
 * @return enum SH_CacheAccess
 */
SH_CacheAccess
SH_OSCachemmap::isCacheAccessible(void) const
{
	if (J9SH_CACHE_FILE_ACCESS_ALLOWED == _cacheFileAccess) {
		return J9SH_CACHE_ACCESS_ALLOWED;
	} else if (J9SH_CACHE_FILE_ACCESS_GROUP_ACCESS_REQUIRED == _cacheFileAccess) {
		return J9SH_CACHE_ACCESS_ALLOWED_WITH_GROUPACCESS;
	} else {
		return J9SH_CACHE_ACCESS_NOT_ALLOWED;
	}
}

/**
 * Advise the OS to release resources used by a section of the shared classes cache
 */
void
SH_OSCachemmap::dontNeedMetadata(J9VMThread* currentThread, const void* startAddress, size_t length) {
/* AIX does not allow memory to be disclaimed for memory mapped files */
#if !defined(AIXPPC)
	PORT_ACCESS_FROM_VMC(currentThread);
	j9mmap_dont_need(startAddress, length);
#endif
}

/**
 * Destroy a persistent shared classes cache
 *
 * @param[in] suppressVerbose suppresses verbose output
 * @param[in] isReset True if reset option is being used, false otherwise.
 *
 * This method detaches from the cache, checks whether it is in use by any other
 * processes and if not, deletes it from the filesystem
 *
 * @return 0 for success and -1 for failure
 */
IDATA
SH_OSCachemmap::destroy(bool suppressVerbose, bool isReset)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	UDATA origVerboseFlags = _verboseFlags;
	IDATA returnVal = -1; /* Assume failure */
	LastErrorInfo lastErrorInfo;

	Trc_SHR_OSC_Mmap_destroy_Entry();

	if (suppressVerbose) {
		_verboseFlags = 0;
	}

	if (_headerStart != NULL) {
		detach();
	}

	if (!closeCacheFile()) {
		Trc_SHR_OSC_Mmap_destroy_closefilefailed();
		goto _done;
	}
	_mapFileHandle = 0;
	_actualFileLength = 0;

	Trc_SHR_OSC_Mmap_destroy_deletingfile(_cachePathName);
	if (!deleteCacheFile(&lastErrorInfo)) {
		Trc_SHR_OSC_Mmap_destroy_badunlink();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_DESTROY_ERROR_DELETING_FILE, &lastErrorInfo);
		goto _done;
	}
	Trc_SHR_OSC_Mmap_destroy_goodunlink();

	if (_verboseFlags) {
		if (isReset) {
			OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_DESTROY_SUCCESS, _cacheName);
		} else {
			J9PortShcVersion versionData;

			memset(&versionData, 0, sizeof(J9PortShcVersion));
			/* Do not care about the getValuesFromShcFilePrefix() return value */
			getValuesFromShcFilePrefix(PORTLIB, _cacheNameWithVGen, &versionData);
			if (J9SH_FEATURE_COMPRESSED_POINTERS == versionData.feature) {
				OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_DESTROY_SUCCESS_CR, _cacheName);
			} else if (J9SH_FEATURE_NON_COMPRESSED_POINTERS == versionData.feature) {
				OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_DESTROY_SUCCESS_NONCR, _cacheName);
			} else {
				OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_DESTROY_SUCCESS, _cacheName);
			}
		}
	}

	Trc_SHR_OSC_Mmap_destroy_finalising();
	finalise();

	returnVal = 0;
	Trc_SHR_OSC_Mmap_destroy_Exit();

_done :
	if (suppressVerbose) {
		_verboseFlags = origVerboseFlags;
	}

	return returnVal;
}

/**
 * Method to update the cache's last detached time, detach it from the
 * process and clean up the object's resources.  It is called when the
 * cache is no longer required by the JVM.
 */
void
SH_OSCachemmap::cleanup()
{
	Trc_SHR_OSC_Mmap_cleanup_Entry();

	if (_finalised) {
		Trc_SHR_OSC_Mmap_cleanup_alreadyfinalised();
		return;
	}

	if (_headerStart) {
		if (acquireHeaderWriteLock(_activeGeneration, NULL) != -1) {
			if (updateLastDetachedTime()) {
				Trc_SHR_OSC_Mmap_cleanup_goodUpdateLastDetachedTime();
			} else {
				Trc_SHR_OSC_Mmap_cleanup_badUpdateLastDetachedTime();
				errorHandler(J9NLS_SHRC_OSCACHE_MMAP_CLEANUP_ERROR_UPDATING_LAST_DETACHED_TIME, NULL);
			}
			if (releaseHeaderWriteLock(_activeGeneration, NULL) == -1) {
				PORT_ACCESS_FROM_PORT(_portLibrary);
				I_32 myerror = j9error_last_error_number();
				Trc_SHR_OSC_Mmap_cleanup_releaseHeaderWriteLock_Failed(myerror);
				Trc_SHR_Assert_ShouldNeverHappen();
			}
		} else {
			PORT_ACCESS_FROM_PORT(_portLibrary);
			I_32 myerror = j9error_last_error_number();
			Trc_SHR_OSC_Mmap_cleanup_acquireHeaderWriteLock_Failed(myerror);
			Trc_SHR_Assert_ShouldNeverHappen();
		}
	}

	if (_headerStart) {
		detach();
	}

	if (_fileHandle != -1) {
		closeCacheFile();
	}

	finalise();

	Trc_SHR_OSC_Mmap_cleanup_Exit();
	return;
}

/*
 * TODO:
 * There follows a series methods for acquiring/releasing various read/write
 * locks on the cache.  These all contain very similar code and while they
 * do not present a problem in their current form, it would probably be better
 * to reduce them to a few basic methods and have them operate on an array of
 * lock words.
 */

/**
 * Get an ID for a write area lock
 *
 * @return a non-negative lockID on success
 */
IDATA SH_OSCachemmap::getWriteLockID()
{
	return J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK;
}

/**
 * Get an ID for a readwrite area lock
 *
 * @return a non-negative lockID on success
 */
IDATA SH_OSCachemmap::getReadWriteLockID()
{
	return J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK;
}

/**
 * Method to acquire the write lock on the cache data region
 *
 * @return 0 on success, -1 on failure
 */
IDATA
SH_OSCachemmap::acquireWriteLock(UDATA lockID)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	I_32 lockFlags = J9PORT_FILE_WRITE_LOCK | J9PORT_FILE_WAIT_FOR_LOCK;
	U_64 lockOffset, lockLength;
	I_32 rc = 0;
	I_64 startLoopTime = 0;
	I_64 endLoopTime = 0;
	UDATA loopCount = 0;

	Trc_SHR_OSC_Mmap_acquireWriteLock_Entry(lockID);

	if ((lockID != J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK) && (lockID != J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK)) {
		Trc_SHR_OSC_Mmap_acquireWriteLock_BadLockID(lockID);
		return -1;
	}

	lockOffset = offsetof(OSCachemmap_header_version_current, dataLocks) + (lockID * sizeof(I_32));
	lockLength = sizeof(((OSCachemmap_header_version_current *)NULL)->dataLocks[0]);

	/* We enter a local mutex before acquiring the file lock. This is because file
	 * locks only work between processes, whereas we need to lock between processes
	 * AND THREADS. So we use a local mutex to lock between threads of the same JVM,
	 * then a file lock for locking between different JVMs
	 */
	Trc_SHR_OSC_Mmap_acquireWriteLock_entering_monitor(lockID);
	if (omrthread_monitor_enter(_lockMutex[lockID]) != 0) {
		Trc_SHR_OSC_Mmap_acquireWriteLock_failed_monitor_enter(lockID);
		return -1;
	}

	Trc_SHR_OSC_Mmap_acquireWriteLock_gettingLock(_fileHandle, lockFlags, lockOffset, lockLength);
#if defined(WIN32) || defined(WIN64)
	rc = j9file_blockingasync_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#else
	rc = j9file_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#endif

	while ((rc == -1) && (j9error_last_error_number() == J9PORT_ERROR_FILE_LOCK_EDEADLK)) {
		if (++loopCount > 1) {
			/* We time the loop so it doesn't loop forever. Try the lock algorithm below
			 * once before staring the timer. */
			if (startLoopTime == 0) {
				startLoopTime = j9time_nano_time();
			} else if (loopCount > 2) {
				/* Loop at least twice */
				endLoopTime = j9time_nano_time();
				if ((endLoopTime - startLoopTime) > ((I_64)RETRY_OBTAIN_WRITE_LOCK_MAX_MS * NANOSECS_PER_MILLISEC)) {
					break;
				}
			}
			omrthread_nanosleep(RETRY_OBTAIN_WRITE_LOCK_SLEEP_NS);
		}
		/* CMVC 153095: there are only three states our locks may be in if EDEADLK is detected.
		 * We can recover from cases 2 & 3 (see comments inline below). For case 1 our only option
		 * is to exit and let the caller retry.
		 */
		if (lockID == J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK && omrthread_monitor_owned_by_self(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK]) == 1) {
			/* CMVC 153095: Case 1
			 * Current thread:
			 *  - Owns W monitor, W lock, RW monitor, and gets EDADLK on RW lock
			 *
			 * Notes:
			 *  - This means other JVMs caused EDEADLK because they are holding RW, and
			 *    waiting on W in a sequence that gives fcntl the impression of deadlock
			 *  - If current thread owns the W monitor, it must also own the W lock
			 *    if the call stack ended up here.
			 *
			 * Recovery:
			 *  - In this case we can't do anything but retry RW, because EDEADLK is caused by other JVMs.
			 */
			Trc_SHR_OSC_Mmap_acquireWriteLockDeadlockMsg("Case 1: Current thread owns W lock & monitor, and RW monitor, but EDEADLK'd on RW lock");
#if defined(WIN32) || defined(WIN64)
			rc = j9file_blockingasync_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#else
			rc = j9file_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#endif

		} else if (lockID == J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK) {
			/* CMVC 153095: Case 2
			 * Another thread:
			 *  - Owns the W monitor, and is waiting on (or owns) the W lock
			 *
			 * Current thread:
			 *  - Current thread owns the RW monitor, and gets EDEADLK on RW lock
			 *
			 * Note:
			 *  - Deadlock might caused by the order in which threads have taken locks, when compared to another JVM.
			 *  - In the recovery code below the first release of the RW Monitor is to ensure SCStoreTransactions
			 *    can complete.
			 *
			 * Recovery
			 *  - Recover by trying to take the W monitor, then RW monitor and lock. This will
			 *    resolve any EDEADLK caused by this JVM, because it ensures no thread in this JVM will hold
			 *    the W lock.
			 */
			Trc_SHR_OSC_Mmap_acquireWriteLockDeadlockMsg("Case 2: Current thread owns RW mon, but EDEADLK'd on RW lock");
			omrthread_monitor_exit(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK]);

			if (omrthread_monitor_enter(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK]) != 0) {
				Trc_SHR_OSC_Mmap_acquireWriteLock_errorTakingWriteMonitor();
				return -1;
			}

			if (omrthread_monitor_enter(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK]) != 0) {
				Trc_SHR_OSC_Mmap_acquireWriteLock_errorTakingWriteMonitor();
				omrthread_monitor_exit(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK]);
				return -1;
			}
#if defined(WIN32) || defined(WIN64)
			rc = j9file_blockingasync_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#else
			rc = j9file_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#endif

			omrthread_monitor_exit(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK]);

		} else if (lockID == J9SH_OSCACHE_MMAP_LOCKID_WRITELOCK) {
			/* CMVC 153095: Case 3
			 * Another thread:
			 *  - Owns RW monitor, and is waiting on (or owns) the RW lock.
			 *
			 * Current thread:
			 *  - Owns W monitor, and gets EDEADLK on W lock.
			 *
			 * Note:
			 *  - If the 'call stack' ends up here then it is known the current thread
			 *    does not own the ReadWrite lock. The shared classes code always
			 *    takes the W lock, then RW lock, OR just the RW lock.
			 *
			 * Recovery:
			 *  - In this case we recover by waiting on the RW monitor before taking the W lock. This will
			 *    resolve any EDEADLK caused by this JVM, because it ensures no thread in this JVM will hold
			 *    the RW lock.
			 */
			Trc_SHR_OSC_Mmap_acquireWriteLockDeadlockMsg("Case 3: Current thread owns W mon, but EDEADLK'd on W lock");
			if (omrthread_monitor_enter(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK]) != 0) {
				Trc_SHR_OSC_Mmap_acquireWriteLock_errorTakingReadWriteMonitor();
				break;
			}
#if defined(WIN32) || defined(WIN64)
			rc = j9file_blockingasync_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#else
			rc = j9file_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#endif
			omrthread_monitor_exit(_lockMutex[J9SH_OSCACHE_MMAP_LOCKID_READWRITELOCK]);

		} else {
			Trc_SHR_Assert_ShouldNeverHappen();
		}
	}

	if (rc == -1) {
		Trc_SHR_OSC_Mmap_acquireWriteLock_badLock();
		omrthread_monitor_exit(_lockMutex[lockID]);
	} else {
		Trc_SHR_OSC_Mmap_acquireWriteLock_goodLock();
	}

	Trc_SHR_OSC_Mmap_acquireWriteLock_Exit(rc);
	return rc;
}

/**
 * Method to release the write lock on the cache data region
 *
 * @return 0 on success, -1 on failure
 */
IDATA
SH_OSCachemmap::releaseWriteLock(UDATA lockID)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	U_64 lockOffset, lockLength;
	I_32 rc = 0;

	Trc_SHR_OSC_Mmap_releaseWriteLock_Entry(lockID);

	if (lockID >= J9SH_OSCACHE_MMAP_LOCK_COUNT) {
		Trc_SHR_OSC_Mmap_releaseWriteLock_BadLockID(lockID);
		return -1;
	}

	lockOffset = offsetof(OSCachemmap_header_version_current, dataLocks) + (lockID * sizeof(I_32));
	lockLength = sizeof(((OSCachemmap_header_version_current *)NULL)->dataLocks[0]);

	Trc_SHR_OSC_Mmap_releaseWriteLock_gettingLock(_fileHandle, lockOffset, lockLength);
#if defined(WIN32) || defined(WIN64)
	rc = j9file_blockingasync_unlock_bytes(_fileHandle, lockOffset, lockLength);
#else
	rc = j9file_unlock_bytes(_fileHandle, lockOffset, lockLength);
#endif

	if (-1 == rc) {
		Trc_SHR_OSC_Mmap_releaseWriteLock_badLock();
	} else {
		Trc_SHR_OSC_Mmap_releaseWriteLock_goodLock();
	}

	Trc_SHR_OSC_Mmap_releaseWriteLock_exiting_monitor(lockID);
	if (omrthread_monitor_exit(_lockMutex[lockID]) != 0) {
		Trc_SHR_OSC_Mmap_releaseWriteLock_bad_monitor_exit(lockID);
		rc = -1;
	}

	Trc_SHR_OSC_Mmap_releaseWriteLock_Exit(rc);
	return rc;
}

/**
 * Get the createTime from the OSCache_header2
 * 
 * @return the createTime
 */
U_64
SH_OSCachemmap::getCreateTime()
{
	OSCachemmap_header_version_current *cacheHeader = (OSCachemmap_header_version_current *)_headerStart;
	return cacheHeader->oscHdr.createTime; 
}

/**
 * Method to acquire the read lock on the cache attach region
 *
 * Needs to be able to work with all generations
 *
 * @param [in] generation The generation of the cache header to use when calculating the lock offset
 *
 * @return 0 on success, -1 on failure
 */
IDATA
SH_OSCachemmap::acquireAttachReadLock(UDATA generation, LastErrorInfo *lastErrorInfo)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	I_32 lockFlags = J9PORT_FILE_READ_LOCK | J9PORT_FILE_WAIT_FOR_LOCK;
	U_64 lockOffset, lockLength;
	I_32 rc = 0;

	Trc_SHR_OSC_Mmap_acquireAttachReadLock_Entry();

	if (NULL != lastErrorInfo) {
		lastErrorInfo->lastErrorCode = 0;
	}

	lockOffset = (U_64)getMmapHeaderFieldOffsetForGen(generation, OSCACHEMMAP_HEADER_FIELD_ATTACH_LOCK);
	lockLength = sizeof(((OSCachemmap_header_version_current *)NULL)->attachLock);

	Trc_SHR_OSC_Mmap_acquireAttachReadLock_gettingLock(_fileHandle, lockFlags, lockOffset, lockLength);
#if defined(WIN32) || defined(WIN64)
	rc = j9file_blockingasync_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#else
	rc = j9file_lock_bytes(_fileHandle, lockFlags, lockOffset, lockLength);
#endif
	if (-1 == rc) {
		if (NULL != lastErrorInfo) {
			lastErrorInfo->lastErrorCode = j9error_last_error_number();
			lastErrorInfo->lastErrorMsg = j9error_last_error_message();
		}
		Trc_SHR_OSC_Mmap_acquireAttachReadLock_badLock();
	} else {
		Trc_SHR_OSC_Mmap_acquireAttachReadLock_goodLock();
	}

	Trc_SHR_OSC_Mmap_acquireAttachReadLock_Exit(rc);
	return rc;
}

/**
 * Method to release the read lock on the cache attach region
 *
 * Needs to be able to work with all generations
 *
 * @param [in] generation The generation of the cache header to use when calculating the lock offset
 *
 * @return 0 on success, -1 on failure
 */
IDATA
SH_OSCachemmap::releaseAttachReadLock(UDATA generation)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	U_64 lockOffset, lockLength;
	I_32 rc = 0;

	Trc_SHR_OSC_Mmap_releaseAttachReadLock_Entry();

	lockOffset = (U_64)getMmapHeaderFieldOffsetForGen(generation, OSCACHEMMAP_HEADER_FIELD_ATTACH_LOCK);
	lockLength = sizeof(((OSCachemmap_header_version_current *)NULL)->attachLock);

	Trc_SHR_OSC_Mmap_releaseAttachReadLock_gettingLock(_fileHandle, lockOffset, lockLength);
#if defined(WIN32) || defined(WIN64)
	rc = j9file_blockingasync_unlock_bytes(_fileHandle, lockOffset, lockLength);
#else
	rc = j9file_unlock_bytes(_fileHandle, lockOffset, lockLength);
#endif

	if (-1 == rc) {
		Trc_SHR_OSC_Mmap_releaseAttachReadLock_badLock();
	} else {
		Trc_SHR_OSC_Mmap_releaseAttachReadLock_goodLock();
	}

	Trc_SHR_OSC_Mmap_releaseAttachReadLock_Exit(rc);
	return rc;
}

/*
 * This function performs enough of an attach to start the cache, but nothing more
 * The internalDetach function is the equivalent for detach
 * isNewCache should be true if we're attaching to a completely uninitialized cache, false otherwise
 * THREADING: Pre-req caller holds the cache header write lock
 *
 * Needs to be able to work with all generations
 *
 * @param [in] isNewCache true if the cache is new and we should calculate cache size using the file size;
 *              false if the cache is pre-existing and we can read the size fields from the cache header
 * @param [in] generation The generation of the cache header to use when calculating the lock offset
 *
 * @return 0 on success, J9SH_OSCACHE_FAILURE on failure, J9SH_OSCACHE_CORRUPT for corrupt cache
 */
IDATA
SH_OSCachemmap::internalAttach(bool isNewCache, UDATA generation)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	U_32 accessFlags = _runningReadOnly ? J9PORT_MMAP_FLAG_READ : J9PORT_MMAP_FLAG_WRITE;
	LastErrorInfo lastErrorInfo;
	IDATA rc = J9SH_OSCACHE_FAILURE;

	Trc_SHR_OSC_Mmap_internalAttach_Entry();

	/* Get current length of file */
	accessFlags |= J9PORT_MMAP_FLAG_SHARED;

	_actualFileLength = _cacheSize;
	Trc_SHR_Assert_True(_actualFileLength > 0);

	if (0 != acquireAttachReadLock(generation, &lastErrorInfo)) {
		Trc_SHR_OSC_Mmap_internalAttach_badAcquireAttachedReadLock();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_ACQUIRING_ATTACH_READ_LOCK, &lastErrorInfo);
		rc = J9SH_OSCACHE_FAILURE;
		goto error;
	}
	Trc_SHR_OSC_Mmap_internalAttach_goodAcquireAttachReadLock();

#ifndef WIN32
	/* if the cache is read-only and not being written, no free disk space is required */
	if (!_runningReadOnly && J9_ARE_NO_BITS_SET(_runtimeFlags, J9SHR_RUNTIMEFLAG_NO_PERSISTENT_DISK_SPACE_CHECK)) {
		J9FileStatFilesystem fileStatFilesystem;
		/* check for free disk space */
		rc = j9file_stat_filesystem(_cachePathName, 0, &fileStatFilesystem);
		if (0 == rc) {
			if (fileStatFilesystem.freeSizeBytes < (U_64)_actualFileLength) {
				OSC_ERR_TRACE2(J9NLS_SHRC_OSCACHE_MMAP_DISK_FULL, (U_64)fileStatFilesystem.freeSizeBytes, (U_64)_actualFileLength);
				rc = J9SH_OSCACHE_FAILURE;
				goto error;
			}
		}
	}
#endif

	/* Map the file */
	_mapFileHandle = j9mmap_map_file(_fileHandle, 0, (UDATA)_actualFileLength, _cachePathName, accessFlags, J9MEM_CATEGORY_CLASSES_SHC_CACHE);
	if ((NULL == _mapFileHandle) || (NULL == _mapFileHandle->pointer)) {
		lastErrorInfo.lastErrorCode = j9error_last_error_number();
		lastErrorInfo.lastErrorMsg = j9error_last_error_message();
		Trc_SHR_OSC_Mmap_internalAttach_badmapfile();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_ATTACH_ERROR_MAPPING_FILE, &lastErrorInfo);
		rc = J9SH_OSCACHE_FAILURE;
		goto error;
	}
	_headerStart = _mapFileHandle->pointer;
	Trc_SHR_OSC_Mmap_internalAttach_goodmapfile(_headerStart);

	if (!isNewCache) {
		J9SRP* dataStartField;
		U_32* dataLengthField;

		/* Get the values from the header */
		if ((dataLengthField = (U_32*)getMmapHeaderFieldAddressForGen(_headerStart, generation, OSCACHE_HEADER_FIELD_DATA_LENGTH))) {
			_dataLength = *dataLengthField;
		}
		if ((dataStartField = (J9SRP*)getMmapHeaderFieldAddressForGen(_headerStart, generation, OSCACHE_HEADER_FIELD_DATA_START))) {
			_dataStart = MMAP_DATASTARTFROMHEADER(dataStartField);
		}
		if (NULL == _dataStart) {
			Trc_SHR_OSC_Mmap_internalAttach_corruptcachefile();
			OSC_ERR_TRACE1(J9NLS_SHRC_OSCACHE_CORRUPT_CACHE_DATA_START_NULL, _dataStart);
			setCorruptionContext(CACHE_DATA_NULL, (UDATA)_dataStart);
			rc = J9SH_OSCACHE_CORRUPT;
			goto error;
		}
	} else {
		/* We don't yet have a header to read - work out the values */
		_dataLength = (U_32)MMAP_CACHEDATASIZE(_actualFileLength);
		_dataStart = (void*)((UDATA)_headerStart + MMAP_CACHEHEADERSIZE);
	}

	Trc_SHR_OSC_Mmap_internalAttach_Exit(_dataStart, sizeof(OSCachemmap_header_version_current));
	return 0;

error:
	internalDetach(generation);
	return rc;
}

/**
 * Function to attach a persistent shared classes cache to the process
 * Function performs version checking on the cache if the version data is provided
 *
 * @param [in] expectedVersionData  If not NULL, function checks the version data of the cache against the values in this struct
 *
 * @return Pointer to the start of the cache data area on success, NULL on failure
 */
void *
SH_OSCachemmap::attach(J9VMThread *currentThread, J9PortShcVersion* expectedVersionData)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	OSCachemmap_header_version_current *cacheHeader;
	IDATA headerRc;
	LastErrorInfo lastErrorInfo;
	J9JavaVM *vm = currentThread->javaVM;
	bool doRelease = false;
	IDATA rc;

	Trc_SHR_OSC_Mmap_attach_Entry1(UnitTest::unitTest);

	/* If we are already attached, just return */
	if (_dataStart) {
		Trc_SHR_OSC_Mmap_attach_alreadyattached(_headerStart, _dataStart, _dataLength);
		return _dataStart;
	}

	if (acquireHeaderWriteLock(_activeGeneration, &lastErrorInfo) == -1) {
		Trc_SHR_OSC_Mmap_attach_acquireHeaderLockFailed();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_ATTACH_ACQUIREHEADERWRITELOCK_ERROR, &lastErrorInfo);
		return NULL;
	}

	doRelease = true;

	rc = internalAttach(false, _activeGeneration);
	if (0 != rc) {
		setError(rc);
		Trc_SHR_OSC_Mmap_attach_internalAttachFailed2();
		/* We've already detached, so just release the header write lock and exit */
		goto release;
	}

	cacheHeader = (OSCachemmap_header_version_current *)_headerStart;

	/* Verify the header */
	if ((headerRc = isCacheHeaderValid(cacheHeader, expectedVersionData)) != J9SH_OSCACHE_HEADER_OK) {
		if (headerRc == J9SH_OSCACHE_HEADER_CORRUPT) {
			Trc_SHR_OSC_Mmap_attach_corruptCacheHeader2();
			/* Cache is corrupt, trigger hook to generate a system dump.
			 * This is the last chance to get corrupt cache image in system dump.
			 * After this point, cache is detached.
			 */
			if (0 ==(_runtimeFlags & J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS)) {
				TRIGGER_J9HOOK_VM_CORRUPT_CACHE(vm->hookInterface, currentThread);
			}
			setError(J9SH_OSCACHE_CORRUPT);
		} else if (headerRc == J9SH_OSCACHE_HEADER_DIFF_BUILDID) {
			Trc_SHR_OSC_Mmap_attach_differentBuildID();
			setError(J9SH_OSCACHE_DIFF_BUILDID);
		} else {
			errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_INVALID_CACHE_HEADER, NULL);
			Trc_SHR_OSC_Mmap_attach_invalidCacheHeader2();
			setError(J9SH_OSCACHE_FAILURE);
		}
		goto detach;
	}
	Trc_SHR_OSC_Mmap_attach_validCacheHeader();

	if (!updateLastAttachedTime(cacheHeader)) {
		Trc_SHR_OSC_Mmap_attach_badupdatelastattachedtime2();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_STARTUP_ERROR_UPDATING_LAST_ATTACHED_TIME, NULL);
		setError(J9SH_OSCACHE_FAILURE);
		goto detach;
	}
	Trc_SHR_OSC_Mmap_attach_goodupdatelastattachedtime();

	if (releaseHeaderWriteLock(_activeGeneration, &lastErrorInfo) == -1) {
		Trc_SHR_OSC_Mmap_attach_releaseHeaderLockFailed2();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_ATTACH_ERROR_RELEASING_HEADER_WRITE_LOCK, &lastErrorInfo);
		/* doRelease set to false so we do not try to call release more than once which has failed in this block */
		doRelease = false;
		goto detach;
	}

	if ((_verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE) && _startupCompleted) {
		OSC_TRACE1(J9NLS_SHRC_OSCACHE_MMAP_ATTACH_ATTACHED, _cacheName);
	}

	Trc_SHR_OSC_Mmap_attach_Exit(_dataStart);
	return _dataStart;

detach:
	internalDetach(_activeGeneration);
release:
	if ((doRelease) && (releaseHeaderWriteLock(_activeGeneration, &lastErrorInfo) == -1)) {
		Trc_SHR_OSC_Mmap_attach_releaseHeaderLockFailed2();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_ATTACH_ERROR_RELEASING_HEADER_WRITE_LOCK, &lastErrorInfo);
	}
	Trc_SHR_OSC_Mmap_attach_ExitWithError();
	return NULL;
}

/**
 * Method to update the last attached time in a cache's header
 *
 * @param [in] headerArg  A pointer to the cache header
 *
 * @return true on success, false on failure
 * THREADING: Pre-req caller holds the cache header write lock
 */
I_32
SH_OSCachemmap::updateLastAttachedTime(OSCachemmap_header_version_current* header)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_Mmap_updateLastAttachedTime_Entry();

	if (_runningReadOnly) {
		Trc_SHR_OSC_Mmap_updateLastAttachedTime_ReadOnly();
		return true;
	}

	I_64 newTime = j9time_current_time_millis();
	Trc_SHR_OSC_Mmap_updateLastAttachedTime_time(newTime, header->lastAttachedTime);
	header->lastAttachedTime = newTime;

	Trc_SHR_OSC_Mmap_updateLastAttachedTime_Exit();
	return true;
}

/**
 * Method to update the last detached time in a cache's header
 *
 * @return true on success, false on failure
 * THREADING: Pre-req caller holds the cache header write lock
 */
I_32
SH_OSCachemmap::updateLastDetachedTime()
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	OSCachemmap_header_version_current* cacheHeader = (OSCachemmap_header_version_current*)_headerStart;
	I_64 newTime;

	Trc_SHR_OSC_Mmap_updateLastDetachedTime_Entry();

	if (_runningReadOnly) {
		Trc_SHR_OSC_Mmap_updateLastDetachedTime_ReadOnly();
		return true;
	}

	newTime = j9time_current_time_millis();
	Trc_SHR_OSC_Mmap_updateLastDetachedTime_time(newTime, cacheHeader->lastDetachedTime);
	cacheHeader->lastDetachedTime = newTime;

	Trc_SHR_OSC_Mmap_updateLastDetachedTime_Exit();
	return true;
}

/**
 * Method to create the cache header for a new persistent cache
 *
 * @param [in] cacheHeader  A pointer to the cache header
 * @param [in] versionData  The version data of the cache
 *
 * @return true on success, false on failure
 * THREADING: Pre-req caller holds the cache header write lock
 */
I_32
SH_OSCachemmap::createCacheHeader(OSCachemmap_header_version_current *cacheHeader, J9PortShcVersion* versionData)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);
	U_32 headerLen = MMAP_CACHEHEADERSIZE;

	if(NULL == cacheHeader) {
		return false;
	}

	Trc_SHR_OSC_Mmap_createCacheHeader_Entry(cacheHeader, headerLen, versionData);

	memset(cacheHeader, 0, headerLen);
	memcpy(cacheHeader->eyecatcher, J9SH_OSCACHE_MMAP_EYECATCHER, J9SH_OSCACHE_MMAP_EYECATCHER_LENGTH);

	initOSCacheHeader(&(cacheHeader->oscHdr), versionData, headerLen);

	cacheHeader->createTime = j9time_current_time_millis();
	cacheHeader->lastAttachedTime = j9time_current_time_millis();
	cacheHeader->lastDetachedTime = j9time_current_time_millis();

	Trc_SHR_OSC_Mmap_createCacheHeader_header(cacheHeader->eyecatcher,
													cacheHeader->oscHdr.size,
													cacheHeader->oscHdr.dataStart,
													cacheHeader->oscHdr.dataLength,
													cacheHeader->createTime,
													cacheHeader->lastAttachedTime);

	Trc_SHR_OSC_Mmap_createCacheHeader_Exit();
	return true;
}

/**
 * Method to set the length of a new cache file
 *
 * @param [in] cacheSize  The length of the cache in bytes
 *
 * @return true on success, false on failure
 * THREADING: Pre-req caller holds the cache header write lock
 */
bool
SH_OSCachemmap::setCacheLength(U_32 cacheSize, LastErrorInfo *lastErrorInfo)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_Mmap_setCacheLength_Entry(cacheSize);

	if (NULL != lastErrorInfo) {
		lastErrorInfo->lastErrorCode = 0;
	}

	if (cacheSize < sizeof(OSCachemmap_header_version_current)) {
		return false;
	}

#if defined(WIN32) || defined(WIN64)
	if (0 != j9file_blockingasync_set_length(_fileHandle, cacheSize)) {
#else
	if (0 != j9file_set_length(_fileHandle, cacheSize)) {
#endif
		LastErrorInfo localLastErrorInfo;
		localLastErrorInfo.lastErrorCode = j9error_last_error_number();
		localLastErrorInfo.lastErrorMsg = j9error_last_error_message();
		Trc_SHR_OSC_Mmap_setCacheLength_badfilesetlength();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_SETCACHELENGTH_FILESETLENGTH, &localLastErrorInfo);
		if (NULL != lastErrorInfo) {
			memcpy(lastErrorInfo, &localLastErrorInfo, sizeof(LastErrorInfo));
		}
		return false;
	}
	Trc_SHR_OSC_Mmap_setCacheLength_goodfilesetlength();

	_cacheSize = cacheSize;

	Trc_SHR_OSC_Mmap_setCacheLength_Exit();
	return true;
}

/**
 * Method to run the initializer supplied to the multi argument constructor
 * or the startup method against the data area of a new cache
 *
 * @param [in] initializer  Struct containing fields to initialize
 *
 * @return true on success, false on failure
 * THREADING: Pre-req caller holds the cache header write lock
 */
I_32
SH_OSCachemmap::initializeDataHeader(SH_OSCacheInitializer *initializer)
{
	U_32 readWriteBytes = (U_32)((_config->sharedClassReadWriteBytes > 0) ? _config->sharedClassReadWriteBytes : 0);
	U_32 softMaxBytes = (U_32)_config->sharedClassSoftMaxBytes;

	Trc_SHR_OSC_Mmap_initializeDataHeader_Entry();

	Trc_SHR_OSC_Mmap_initializeDataHeader_callinginit3(_dataStart,
															_dataLength,
															_config->sharedClassMinAOTSize,
															_config->sharedClassMaxAOTSize,
															_config->sharedClassMinJITSize,
															_config->sharedClassMaxJITSize,
															_config->sharedClassSoftMaxBytes,
															readWriteBytes);

	if (_config->sharedClassSoftMaxBytes < 0) {
		softMaxBytes = (U_32)-1;
	} else if (softMaxBytes > _actualFileLength) {
		softMaxBytes = (U_32)_actualFileLength;
		Trc_SHR_OSC_Mmap_initializeDataHeader_softMaxBytesTooBig(softMaxBytes);
	}

	initializer->init((char*)_dataStart,
								_dataLength,
								(I_32)_config->sharedClassMinAOTSize,
								(I_32)_config->sharedClassMaxAOTSize,
								(I_32)_config->sharedClassMinJITSize,
								(I_32)_config->sharedClassMaxJITSize,
								readWriteBytes,
								softMaxBytes);
	Trc_SHR_OSC_Mmap_initializeDataHeader_initialized();

	Trc_SHR_OSC_Mmap_initializeDataHeader_Exit();
	return true;
}

/**
 * Method to detach a persistent cache from the process
 */
IDATA
SH_OSCachemmap::detach()
{
	if (acquireHeaderWriteLock(_activeGeneration, NULL) != -1) {
		updateLastDetachedTime();
		if (releaseHeaderWriteLock(_activeGeneration, NULL) == -1) {
			PORT_ACCESS_FROM_PORT(_portLibrary);
			I_32 myerror = j9error_last_error_number();
			Trc_SHR_OSC_Mmap_detach_releaseHeaderWriteLock_Failed(myerror);
			Trc_SHR_Assert_ShouldNeverHappen();
		}
	} else {
		PORT_ACCESS_FROM_PORT(_portLibrary);
		I_32 myerror = j9error_last_error_number();
		Trc_SHR_OSC_Mmap_detach_acquireHeaderWriteLock_Failed(myerror);
		Trc_SHR_Assert_ShouldNeverHappen();
	}
	internalDetach(_activeGeneration);
	return 0;
}

/* Perform enough work to detach from the cache after having called internalAttach */
void
SH_OSCachemmap::internalDetach(UDATA generation)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_Mmap_internalDetach_Entry();

	if (NULL == _headerStart) {
		Trc_SHR_OSC_Mmap_internalDetach_notattached();
		return;
	}

	if (_mapFileHandle) {
		j9mmap_unmap_file(_mapFileHandle);
		_mapFileHandle = NULL;
	}

	if (0 != releaseAttachReadLock(generation)) {
		Trc_SHR_OSC_Mmap_internalDetach_badReleaseAttachReadLock();
	}
	Trc_SHR_OSC_Mmap_internalDetach_goodReleaseAttachReadLock();

	_headerStart = NULL;
	_dataStart = NULL;
	_dataLength = 0;
	/* The member variable '_actualFileLength' is not set to zero b/c
	 * the cache size may be needed to reset the cache (e.g. in the
	 * case of a build id mismatch, the cache may be reset, and
	 * ::getTotalSize() may be called to ensure the new cache is the
	 * same size).
	 */

	Trc_SHR_OSC_Mmap_internalDetach_Exit(_headerStart, _dataStart, _dataLength);
	return;
}

/**
 * Method to get the statistics for a shared classes cache
 *
 * Needs to be able to get stats for all cache generations
 *
 * This method returns the last attached, detached and created times,
 * whether the cache is in use and that it is a persistent cache.
 *
 * Details of data held in the cache data area are not accessed here
 *
 * @param [in] vm The Java VM
 * @param [in] ctrlDirName  Cache directory
 * @param [in] cacheNameWithVGen Filename of the cache to stat
 * @param [out] cacheInfo Pointer to the structure to be completed with the cache's details
 * @param [in] reason Indicates the reason for getting cache stats. Refer sharedconsts.h for valid values.
 * @param [out] lowerLayerList A list of SH_OSCache_Info for all lower layer caches.
 *
 * @return 0 on success and -1 for failure
 */
IDATA
SH_OSCachemmap::getCacheStats(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char *cacheNameWithVGen, SH_OSCache_Info *cacheInfo, UDATA reason, J9Pool** lowerLayerList)
{
	J9PortLibrary *portLibrary = vm->portLibrary;
	PORT_ACCESS_FROM_PORT(portLibrary);
	/* Using 'SH_OSCachemmap cacheStruct' breaks the pattern of calling getRequiredConstrBytes(), and then allocating memory.
	 * However it is consistent with 'SH_OSCachesysv::getCacheStats'.
	 */
	SH_OSCachemmap cacheStruct;
	SH_OSCachemmap *cache = NULL;
	void *cacheHeader;
	I_64 *timeValue;
	I_32 inUse;
	IDATA lockRc;
	J9SharedClassPreinitConfig piconfig;
	J9PortShcVersion versionData;
	UDATA reasonForStartup = SHR_STARTUP_REASON_NORMAL;

	Trc_SHR_OSC_Mmap_getCacheStats_Entry(cacheNameWithVGen, cacheInfo);

	getValuesFromShcFilePrefix(portLibrary, cacheNameWithVGen, &versionData);
	versionData.cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;

	if (removeCacheVersionAndGen(cacheInfo->name, CACHE_ROOT_MAXLEN, J9SH_VERSION_STRING_LEN+1, cacheNameWithVGen) != 0) {
		return -1;
	}

	if (SHR_STATS_REASON_DESTROY == reason) {
		reasonForStartup = SHR_STARTUP_REASON_DESTROY;
	} else if (SHR_STATS_REASON_EXPIRE == reason) {
		reasonForStartup = SHR_STARTUP_REASON_EXPIRE;
	}
	

	cache = (SH_OSCachemmap *) SH_OSCache::newInstance(PORTLIB, &cacheStruct, cacheInfo->name, cacheInfo->generation, &versionData, getLayerFromName(cacheNameWithVGen));

	/* We try to open the cache read/write */
	if (!cache->startup(vm, ctrlDirName, vm->sharedCacheAPI->cacheDirPerm, cacheInfo->name, &piconfig, SH_CompositeCacheImpl::getNumRequiredOSLocks(), J9SH_OSCACHE_OPEXIST_STATS, 0, 0/*runtime flags*/, 0, 0, &versionData, NULL, reasonForStartup)) {
		/* If that fails - try to open the cache read-only */
		if (!cache->startup(vm, ctrlDirName, vm->sharedCacheAPI->cacheDirPerm, cacheInfo->name, &piconfig, 0, J9SH_OSCACHE_OPEXIST_STATS, 0, 0/*runtime flags*/, J9OSCACHE_OPEN_MODE_DO_READONLY, 0, &versionData, NULL, reasonForStartup)) {
			cache->cleanup();
			return -1;
		}
		inUse = J9SH_OSCACHE_UNKNOWN; /* We can't determine whether there are JVMs attached to a read-only cache */

	} else {
		/* Try to acquire the attach write lock. This will only succeed if no one else
		 * has the attach read lock i.e. there is no one connected to the cache */
		lockRc = cache->tryAcquireAttachWriteLock(cacheInfo->generation);
		if (0 == lockRc) {
			Trc_SHR_OSC_Mmap_getCacheStats_cacheNotInUse();
			inUse = 0;
			cache->releaseAttachWriteLock(cacheInfo->generation);
		} else {
			Trc_SHR_OSC_Mmap_getCacheStats_cacheInUse();
			inUse = 1;
		}
	}

	cacheInfo->lastattach = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->lastdetach = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->createtime = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->os_shmid = (UDATA)J9SH_OSCACHE_UNKNOWN;
	cacheInfo->os_semid = (UDATA)J9SH_OSCACHE_UNKNOWN;
	cacheInfo->nattach = inUse;

	/* CMVC 177634: Skip calling internalAttach() when destroying the cache */
	if (SHR_STARTUP_REASON_DESTROY != reasonForStartup) {
		/* The offset of fields createTime, lastAttachedTime, lastDetachedTime in struct OSCache_mmap_header2 are different on 32-bit and 64-bit caches.
		 * This depends on OSCachemmap_header_version_current and OSCache_header_version_current not changing in an incompatible way.
		 */
		if (J9SH_ADDRMODE == cacheInfo->versionData.addrmode) {
			IDATA rc;
			/* Attach to the cache, so we can read the fields in the header */
			rc = cache->internalAttach(false, cacheInfo->generation);
			if (0 != rc) {
				cache->setError(rc);
				cache->cleanup();
				return -1;
			}
			cacheHeader = cache->_headerStart;

			/* Read the fields from the header and populate cacheInfo */
			if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_LAST_ATTACHED_TIME))) {
				cacheInfo->lastattach = *timeValue;
			}
			if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_LAST_DETACHED_TIME))) {
				cacheInfo->lastdetach = *timeValue;
			}
			if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_CREATE_TIME))) {
				cacheInfo->createtime = *timeValue;
			}
			if (SHR_STATS_REASON_ITERATE == reason) {
				getCacheStatsCommon(vm, ctrlDirName, groupPerm, cache, cacheInfo, lowerLayerList);
			}
			cache->internalDetach(cacheInfo->generation);
		}
	}

	Trc_SHR_OSC_Mmap_getCacheStats_Exit(cacheInfo->os_shmid,
											cacheInfo->os_semid,
											cacheInfo->lastattach,
											cacheInfo->lastdetach,
											cacheInfo->createtime,
											cacheInfo->nattach,
											cacheInfo->versionData.cacheType);
	/* Note that generation is not set here. This could be determined by parsing the filename,
	 * but is currently set by the caller */
	cache->cleanup();
	return 0;
}


/**
 * Method to get a SH_OSCache_Info for a compatible non-top layer shared classes cache.
 * 
 * This method returns the last attached, detached and created times,
 * 
 * Details of data held in the cache data area are not accessed here.
 * 
 * @param[in] vm The Java VM
 * @param[in] ctrlDirName Cache directory
 * @param[in] groupPerm Group permissions to open the cache directory
 * @param[in] cacheNameWithVGen Filename of the cache to stat
 * @param[out] cacheInfo Pointer to the structure to be completed with the cache's details
 * @param[in] reason Indicates the reason for getting cache stats. Refer sharedconsts.h for valid values.
 * @param[in] A pointer of SH_OSCachemmap
 * 
 * @return 0 on success and -1 for failure
 */
IDATA
SH_OSCachemmap::getNonTopLayerCacheInfo(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char *cacheNameWithVGen, SH_OSCache_Info *cacheInfo, UDATA reason, SH_OSCachemmap* oscache)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	void *cacheHeader = NULL;
	I_64 *timeValue = NULL;
	J9PortShcVersion versionData;
	IDATA ret = 0;
	
	Trc_SHR_OSC_Mmap_getNonTopLayerCacheInfo_Entry(ctrlDirName, groupPerm, cacheNameWithVGen, reason);

	Trc_SHR_Assert_True(SHR_STATS_REASON_ITERATE == reason);

	getValuesFromShcFilePrefix(PORTLIB, cacheNameWithVGen, &versionData);
	versionData.cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
	
	if (removeCacheVersionAndGen(cacheInfo->name, CACHE_ROOT_MAXLEN, J9SH_VERSION_STRING_LEN + 1, cacheNameWithVGen) != 0) {
		ret = -1;
		goto done;
	}

	cacheInfo->lastattach = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->lastdetach = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->createtime = J9SH_OSCACHE_UNKNOWN;
	cacheInfo->os_shmid = (UDATA)J9SH_OSCACHE_UNKNOWN;
	cacheInfo->os_semid = (UDATA)J9SH_OSCACHE_UNKNOWN;
	cacheInfo->nattach = J9SH_OSCACHE_UNKNOWN;

	/* The offset of fields createTime, lastAttachedTime, lastDetachedTime in struct OSCache_mmap_header2 are different on 32-bit and 64-bit caches. 
	 * This depends on OSCachemmap_header_version_current and OSCache_header_version_current not changing in an incompatible way.
	 */
	Trc_SHR_Assert_True(J9SH_ADDRMODE == cacheInfo->versionData.addrmode);

	cacheHeader = oscache->_headerStart;

	/* Read the fields from the header and populate cacheInfo */
	if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_LAST_ATTACHED_TIME))) {
		cacheInfo->lastattach = *timeValue;
	} 
	if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_LAST_DETACHED_TIME))) {
		cacheInfo->lastdetach = *timeValue;
	}
	if ((timeValue = (I_64*)getMmapHeaderFieldAddressForGen(cacheHeader, cacheInfo->generation, OSCACHEMMAP_HEADER_FIELD_CREATE_TIME))) {
		cacheInfo->createtime = *timeValue;
	}
done:
	Trc_SHR_OSC_Mmap_getNonTopLayerCacheInfo_Exit(ret, cacheInfo->name, cacheInfo->lastattach, cacheInfo->lastdetach, cacheInfo->createtime);
	return ret;
}

/**
 * Delete the cache file
 *
 * @return true on success, false on failure
 */
bool
SH_OSCachemmap::deleteCacheFile(LastErrorInfo *lastErrorInfo)
{
	bool result = true;
	PORT_ACCESS_FROM_PORT(_portLibrary);

	Trc_SHR_OSC_Mmap_deleteCacheFile_entry();

	if (NULL != lastErrorInfo) {
		lastErrorInfo->lastErrorCode = 0;
	}

	if (-1 == j9file_unlink(_cachePathName)) {
		I_32 errorCode = j9error_last_error_number();

		if (J9PORT_ERROR_FILE_NOENT != errorCode) {
			if (NULL != lastErrorInfo) {
				lastErrorInfo->lastErrorCode = errorCode;
				lastErrorInfo->lastErrorMsg = j9error_last_error_message();
			}
			Trc_SHR_OSC_Mmap_deleteCacheFile_failed();
			result = false;
		}
	}

	Trc_SHR_OSC_Mmap_deleteCacheFile_exit();
	return result;
}

/**
 * Method to perform processing required when the JVM is exiting
 *
 * Note: The JVM requires that memory should not be freed during
 *          exit processing
 */
void
SH_OSCachemmap::runExitCode()
{
	Trc_SHR_OSC_Mmap_runExitCode_Entry();

	if (acquireHeaderWriteLock(_activeGeneration, NULL) != -1) {
		if (updateLastDetachedTime()) {
			Trc_SHR_OSC_Mmap_runExitCode_goodUpdateLastDetachedTime();
		} else {
			Trc_SHR_OSC_Mmap_runExitCode_badUpdateLastDetachedTime();
			errorHandler(J9NLS_SHRC_OSCACHE_MMAP_CLEANUP_ERROR_UPDATING_LAST_DETACHED_TIME, NULL);
		}
		releaseHeaderWriteLock(_activeGeneration, NULL); /* No point checking return value - we're going down */
	} else {
		PORT_ACCESS_FROM_PORT(_portLibrary);
		I_32 myerror = j9error_last_error_number();
		Trc_SHR_OSC_Mmap_runExitCode_acquireHeaderWriteLock_Failed(myerror);
		Trc_SHR_Assert_ShouldNeverHappen();
	}

	Trc_SHR_OSC_Mmap_runExitCode_Exit();
}

#if defined (J9SHR_MSYNC_SUPPORT)
/**
 * Synchronise cache updates to disk
 *
 * This function call j9mmap_msync to synchronise the cache to disk
 *
 * @return 0 on success, -1 on failure
 */
IDATA
SH_OSCachemmap::syncUpdates(void* start, UDATA length, U_32 flags)
{
	PORT_ACCESS_FROM_PORT(_portLibrary);

	IDATA rc;

	Trc_SHR_OSC_Mmap_syncUpdates_Entry(start, length, flags);

	rc = j9mmap_msync(start, length, flags);
	if (-1 == rc) {
		Trc_SHR_OSC_Mmap_syncUpdates_badmsync();
		return -1;
	}
	Trc_SHR_OSC_Mmap_syncUpdates_goodmsync();

	Trc_SHR_OSC_Mmap_syncUpdates_Exit();
	return 0;
}
#endif

/**
 * Return the locking capabilities of this shared classes cache implementation
 *
 * Read and write locks are supported for this implementation
 *
 * @return J9OSCACHE_DATA_WRITE_LOCK | J9OSCACHE_DATA_READ_LOCK
 */
IDATA
SH_OSCachemmap::getLockCapabilities()
{
	return J9OSCACHE_DATA_WRITE_LOCK | J9OSCACHE_DATA_READ_LOCK;
}

/**
 * Sets the protection as specified by flags for the memory pages containing all or part of the interval address->(address+len)
 *
 * @param[in] portLibrary An instance of portLibrary
 * @param[in] address   Pointer to the shared memory region.
 * @param[in] length    The size of memory in bytes spanning the region in which we want to set protection
 * @param[in] flags     The specified protection to apply to the pages in the specified interval
 *
 * @return 0 if the operations has been successful, -1 if an error has occured
 */
IDATA
SH_OSCachemmap::setRegionPermissions(J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags)
{
	PORT_ACCESS_FROM_PORT(portLibrary);

	return j9mmap_protect(address, length, flags);
}

/**
 * Returns the minimum sized region of a shared classes cache on which the process can set permissions, in the number of bytes.
 *
 * @param[in] portLibrary An instance of portLibrary
 *
 * @return the minimum size of region on which we can control permissions size or 0 if this is unsupported
 */
UDATA
SH_OSCachemmap::getPermissionsRegionGranularity(J9PortLibrary* portLibrary)
{
	PORT_ACCESS_FROM_PORT(portLibrary);

	/* The code below is used as is in SH_CompositeCacheImpl::initialize()
	 * for initializing SH_CompositeCacheImpl::_osPageSize during unit testing.
	 */

	/* This call to capabilities is arguably unnecessary, but it is a good check to do */
	if (j9mmap_capabilities() & J9PORT_MMAP_CAPABILITY_PROTECT) {
		return j9mmap_get_region_granularity((void*)_headerStart);
	}
	return 0;
}

/**
 * Returns the total size of the cache memory
 *
 * This value is not derived from the cache header, so is reliable even if the cache is corrupted
 *
 * @return size of cache memory
 */
U_32
SH_OSCachemmap::getTotalSize()
{
	return (U_32)_actualFileLength;
}

bool 
SH_OSCachemmap::fixAndWriteOSCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 size)
{
	bool rc = false;
	U_32* fieldToFix = NULL;
	LastErrorInfo lastErrorInfo = {0};
	PORT_ACCESS_FROM_VMC(currentThread);
	OSCachemmap_header_version_current *headerCopy = (OSCachemmap_header_version_current *)j9mem_allocate_memory(MMAP_CACHEHEADERSIZE, J9MEM_CATEGORY_VM);
	
	if (NULL == headerCopy) {
		lastErrorInfo.lastErrorCode = j9error_last_error_number();
		lastErrorInfo.lastErrorMsg = j9error_last_error_message();
		errorHandler(J9NLS_SHRC_OSCACHE_ALLOC_FAILED, &lastErrorInfo);
		goto done;
	}
	memcpy(headerCopy, _headerStart, MMAP_CACHEHEADERSIZE);

	if ((fieldToFix = (U_32*)getMmapHeaderFieldAddressForGen(headerCopy, _activeGeneration, OSCACHE_HEADER_FIELD_SIZE))) {
		*fieldToFix += size;
	} else {
		Trc_SHR_Assert_ShouldNeverHappen();
		goto done;
	}
	if ((fieldToFix = (U_32*)getMmapHeaderFieldAddressForGen(headerCopy, _activeGeneration, OSCACHE_HEADER_FIELD_DATA_LENGTH))) {
		*fieldToFix += size;
	} else {
		Trc_SHR_Assert_ShouldNeverHappen();
		goto done;
	}
	
	if (MMAP_CACHEHEADERSIZE != j9file_write(fd, (void*)headerCopy, MMAP_CACHEHEADERSIZE)) {
		lastErrorInfo.lastErrorCode = j9error_last_error_number();
		lastErrorInfo.lastErrorMsg = j9error_last_error_message();
		errorHandler(J9NLS_SHRC_OSCACHE_MMAP_ERROR_WRITE_MMAP_HEADER, &lastErrorInfo);
		goto done;
	}
	rc = true;
done:
	if (NULL != headerCopy) {
		j9mem_free_memory(headerCopy);
	}
	return rc;
}

UDATA
SH_OSCachemmap::getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor)
{
	descriptor->cacheGen = _activeGeneration;
	descriptor->shmid = descriptor->semid = -2;
	descriptor->cacheDir = _cachePathName;

	return 1;
}

void *
SH_OSCachemmap::getAttachedMemory()
{
	/* This method should only be called between calls to
	 * internalAttach and internalDetach
	 */
	return _dataStart;
}

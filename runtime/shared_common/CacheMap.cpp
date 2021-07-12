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
#include "CacheMap.hpp"
#include "Managers.hpp"
#include "ClasspathManagerImpl2.hpp"
#include "ROMClassManagerImpl.hpp"
#include "TimestampManagerImpl.hpp"
#include "CompiledMethodManagerImpl.hpp"
#include "ScopeManagerImpl.hpp"
#include "ByteDataManagerImpl.hpp"
#include "AttachedDataManagerImpl.hpp"
#include "CompositeCacheImpl.hpp"
#include "UnitTest.hpp"
#include "AtomicSupport.hpp"
#include "ut_j9shr.h"
#include "j2sever.h"
#include "j9shrnls.h"
#include "j9comp.h"
#include "j9consts.h"
#include <string.h>
extern "C" {
#include "shrinit.h"
}

/* Trace macros should be used if messages should be affected by verboseLevel */
#define CACHEMAP_TRACE(verboseLevel, nlsFlags, var1) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1)
#define CACHEMAP_TRACE1(verboseLevel, nlsFlags, var1, p1) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1)
#define CACHEMAP_TRACE2(verboseLevel, nlsFlags, var1, p1, p2) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2)
#define CACHEMAP_TRACE3(verboseLevel, nlsFlags, var1, p1, p2, p3) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3)
#define CACHEMAP_TRACE4(verboseLevel, nlsFlags, var1, p1, p2, p3, p4) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4)

/* Print macros should be used if message should always be printed */
#define CACHEMAP_PRINT(nlsFlags, var1) j9nls_printf(PORTLIB, nlsFlags, var1)
#define CACHEMAP_PRINT1(nlsFlags, var1, p1) j9nls_printf(PORTLIB, nlsFlags, var1, p1)
#define CACHEMAP_PRINT2(nlsFlags, var1, p1, p2) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2)
#define CACHEMAP_PRINT3(nlsFlags, var1, p1, p2, p3) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3)
#define CACHEMAP_PRINT4(nlsFlags, var1, p1, p2, p3, p4) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4)
#define CACHEMAP_PRINT5(nlsFlags, var1, p1, p2, p3, p4, p5) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4, p5)
#define CACHEMAP_PRINT6(nlsFlags, var1, p1, p2, p3, p4, p5, p6) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4, p5, p6)
#define CACHEMAP_PRINT7(nlsFlags, var1, p1, p2, p3, p4, p5, p6, p7) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4, p5, p6, p7)
#define CACHEMAP_PRINT8(nlsFlags, var1, p1, p2, p3, p4, p5, p6, p7, p8) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4, p5, p6, p7, p8)

#define CACHEMAP_FMTPRINT1(nlsFlags, var1, p1) j9nls_printf(PORTLIB, nlsFlags, var1, 1,' ',p1)

static char* formatAttachedDataString(J9VMThread* currentThread, U_8 *attachedData, UDATA attachedDataLength, char *attachedDataStringBuffer, UDATA bufferLength);
static void checkROMClassUTF8SRPs(J9ROMClass *romClass);
/* If you make this sleep a lot longer, it almost eliminates store contention
 * because the VMs get out of step with each other, but you delay excessively */
#define WRITE_HASH_WAIT_MAX_MICROS 80000
#define DEFAULT_WRITE_HASH_WAIT_MILLIS 10
#define WRITE_HASH_DEFAULT_MAX_MICROS 20000

#define MARK_STALE_RETRY_TIMES 10
#define VERBOSE_BUFFER_SIZE 255

/* TODO: May want to make this cachelet size configurable */
#define J9SHR_DEFAULT_CACHELET_SIZE (1024 * 1024)
#define J9SHR_NESTED_CACHE_TEMP_NAME "nested_temp"

/* All flags should be false. Don't EVER update the cache when set. */
#define RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE (J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES | J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_AVAILABLE_SPACE_FULL)

#define RUNTIME_FLAGS_PREVENT_AOT_DATA_UPDATE (J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES | J9SHR_RUNTIMEFLAG_AOT_SPACE_FULL)

#define RUNTIME_FLAGS_PREVENT_JIT_DATA_UPDATE (J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES | J9SHR_RUNTIMEFLAG_JIT_SPACE_FULL)

#define MAX_INT 0x7fffffff

#define FIND_ATTACHED_DATA_RETRY_COUNT 1
#define FIND_ATTACHED_DATA_CORRUPT_WAIT_TIME 1

#define J9_SCC_TMP_FILE_EXT ".j9tmp"

struct TR_AOTHeader;

/**
 * @param currentThread - the currentThread or NULL when called to collect javacore data
 *
 * Assumes the mutex can't be entered recursively
 */
IDATA
SH_CacheMap::enterLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller)
{
	if (_isAssertEnabled) {
	Trc_SHR_Assert_ShouldNotHaveLocalMutex(monitor);
	}	

	/* WARNING - currentThread can be NULL */
	return enterReentrantLocalMutex(currentThread, monitor, name, caller);
}

/**
 * @param currentThread - the currentThread or NULL when called to collect javacore data
 *
 * Assumes the mutex can't be entered recursively
 */
IDATA
SH_CacheMap::exitLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller)
{
	if (_isAssertEnabled) {
	Trc_SHR_Assert_ShouldHaveLocalMutex(monitor);
	}

	/* WARNING - currentThread can be NULL */
	return exitReentrantLocalMutex(currentThread, monitor, name, caller);
}

/**
 * @param currentThread - the currentThread or NULL when called to collect javacore data
 */
IDATA
SH_CacheMap::enterReentrantLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller)
{
	IDATA rc = 0;

	/* WARNING - currentThread can be NULL */
	Trc_SHR_CM_enterLocalMutex_pre(currentThread, name, caller);
	rc = omrthread_monitor_enter(monitor);
	Trc_SHR_CM_enterLocalMutex_post(currentThread, name, rc, caller);

	return rc;
}

/**
 * @param currentThread - the currentThread or NULL when called to collect javacore data
 */
IDATA
SH_CacheMap::exitReentrantLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller)
{
	IDATA rc = 0;

	/* WARNING - currentThread can be NULL */
	Trc_SHR_CM_exitLocalMutex_pre(currentThread, name, caller);
	rc = omrthread_monitor_exit(monitor);
	Trc_SHR_CM_exitLocalMutex_post(currentThread, name, rc, caller);

	return rc;
}


IDATA
SH_CacheMap::enterRefreshMutex(J9VMThread* currentThread, const char* caller)
{
	IDATA rc;

	if ((rc = enterReentrantLocalMutex(currentThread, _refreshMutex, "_refreshMutex", caller)) == 0) {
		if (1 == ((J9ThreadAbstractMonitor*)_refreshMutex)->count) {
			/* nonrecursive enter */
			SH_CompositeCacheImpl* ccToUse = _ccHead;
			do {
				ccToUse->notifyRefreshMutexEntered(currentThread);
				ccToUse = ccToUse->getNext();
			} while (NULL != ccToUse);
		}
	}
	return rc;
}

IDATA
SH_CacheMap::exitRefreshMutex(J9VMThread* currentThread, const char* caller)
{
	IDATA rc;

	Trc_SHR_Assert_ShouldHaveLocalMutex(_refreshMutex);

	if (1 == ((J9ThreadAbstractMonitor*)_refreshMutex)->count) {
		/* nonrecursive exit */
		SH_CompositeCacheImpl* ccToUse = _ccHead;
		do {
			ccToUse->notifyRefreshMutexExited(currentThread);
			ccToUse = ccToUse->getNext();
		} while (NULL != ccToUse);
	}
	rc = exitReentrantLocalMutex(currentThread, _refreshMutex, "_refreshMutex", caller);
	return rc;
}

/**
 * Builds a new SH_CacheMap
 * THREADING: Only ever single threaded
 * 
 * @param [in] vm  A Java VM
 * @param [in] sharedClassConfig
 * @param [in] memForConstructor  Should be memory of the size from getRequiredConstrBytes
 * @param [in] cacheName  The name of the cache
 * @param [in] cacheTypeRequired  The cache type required
 *
 * @return A pointer to the new CacheMap 
 */
SH_CacheMap*
SH_CacheMap::newInstance(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, SH_CacheMap* memForConstructor, const char* cacheName, I_32 cacheTypeRequired)
{
	SH_CacheMap* newCacheMap = memForConstructor;
	I_8 topLayer = 0;
	if (NULL != sharedClassConfig) {
		/* sharedClassConfig can be null in shrtest */
		topLayer = sharedClassConfig->layer;
	}

	Trc_SHR_CM_newInstance_Entry(vm);

	new(newCacheMap) SH_CacheMap();
	newCacheMap->initialize(vm, sharedClassConfig, ((BlockPtr)memForConstructor + sizeof(SH_CacheMap)), cacheName, cacheTypeRequired, topLayer, false);

	Trc_SHR_CM_newInstance_Exit();

	return newCacheMap;
}

/**
 * Advise the OS to release resources associated with
 * the metadata which have been accessed to date.
 */
void
SH_CacheMap::dontNeedMetadata(J9VMThread* currentThread) 
{
	Trc_SHR_CM_j9shr_dontNeedMetadata(currentThread);
	SH_CompositeCacheImpl* ccToUse = _ccHead;

	if (_metadataReleased) {
		return;
	}
	_metadataReleased = true;
	do {
		ccToUse->dontNeedMetadata(currentThread);
		ccToUse = ccToUse->getNext();
	} while (NULL != ccToUse);

}

/**
 * Builds a new SH_CacheMap for retrieving cache statistics
 *
 * @param [in] vm  A Java VM
 * @param [in] memForConstructor  Should be memory of the size from getRequiredConstrBytes
 * @param [in] cacheName  The name of the cache
 * @param [in] topLayer  the top layer number
 *
 * @return A pointer to the CacheMapStats
 */
SH_CacheMapStats*
SH_CacheMap::newInstanceForStats(J9JavaVM* vm, SH_CacheMap* memForConstructor, const char* cacheName, I_8 topLayer)
{
	SH_CacheMap* newCacheMap = memForConstructor;

	Trc_SHR_CM_newInstanceForStats_Entry(vm);

	new(newCacheMap) SH_CacheMap();
	newCacheMap->initialize(vm, NULL, ((BlockPtr)memForConstructor + sizeof(SH_CacheMap)), cacheName, 0, topLayer, true);

	Trc_SHR_CM_newInstanceForStats_Exit();

	return newCacheMap;
}

/* THREADING: Only ever single threaded */
void
SH_CacheMap::initialize(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, BlockPtr memForConstructor, const char* cacheName, I_32 cacheTypeRequired, I_8 topLayer, bool startupForStats)
{
	BlockPtr allocPtr = memForConstructor;

	Trc_SHR_CM_initialize_Entry1(UnitTest::unitTest);

	_sharedClassConfig = sharedClassConfig;
	_portlib = vm->portLibrary;
	_cacheCorruptReported = false;
	_refreshMutex = NULL;
	_writeHashMaxWaitMicros = WRITE_HASH_DEFAULT_MAX_MICROS;
	_writeHashSavedMaxWaitMicros = 0;
	_writeHashAverageTimeMicros = 0;
	_writeHashContendedResetHash = 0;
	_bytesRead = 0;
	_isAssertEnabled = true;
	_metadataReleased = false;
	_ccPool = NULL;

	_managers = SH_Managers::newInstance(vm, (SH_Managers *)allocPtr);

	_ccHead = _cc = SH_CompositeCacheImpl::newInstance(vm, sharedClassConfig, (SH_CompositeCacheImpl*)(allocPtr += SH_Managers::getRequiredConstrBytes()), cacheName, cacheTypeRequired, startupForStats, topLayer);
	_ccHead->setNext(NULL);
	_ccHead->setPrevious(NULL);
	_ccTail = _ccHead;

	memset(_cacheAddressRangeArray, 0, sizeof(_cacheAddressRangeArray));
	
	_numOfCacheLayers = 0;

	_tsm = SH_TimestampManagerImpl::newInstance(vm, (SH_TimestampManagerImpl*)(allocPtr += SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, startupForStats)), sharedClassConfig);

	_cpm = SH_ClasspathManagerImpl2::newInstance(vm, this, _tsm, (SH_ClasspathManagerImpl2*)(allocPtr += SH_TimestampManagerImpl::getRequiredConstrBytes()));

	_scm = SH_ScopeManagerImpl::newInstance(vm, this, (SH_ScopeManagerImpl*)(allocPtr += SH_ClasspathManagerImpl2::getRequiredConstrBytes()));

	_rcm = SH_ROMClassManagerImpl::newInstance(vm, this, _tsm, (SH_ROMClassManagerImpl*)(allocPtr += SH_ScopeManagerImpl::getRequiredConstrBytes()));

	_cmm = SH_CompiledMethodManagerImpl::newInstance(vm, this, (SH_CompiledMethodManagerImpl*)(allocPtr += SH_ROMClassManagerImpl::getRequiredConstrBytes()));

	_bdm = SH_ByteDataManagerImpl::newInstance(vm, this, (SH_ByteDataManagerImpl*)(allocPtr += SH_CompiledMethodManagerImpl::getRequiredConstrBytes()));

	_adm = SH_AttachedDataManagerImpl::newInstance(vm, this, (SH_AttachedDataManagerImpl*)(allocPtr += SH_ByteDataManagerImpl::getRequiredConstrBytes()));

	Trc_SHR_CM_initialize_Exit();
}

/**
 * Returns memory bytes the constructor requires to build what it needs
 * THREADING: Only ever single threaded
 * 
 * @return bytes required
 */
UDATA
SH_CacheMap::getRequiredConstrBytes(bool startupForStats)
{
	UDATA reqBytes = 0;

	reqBytes += SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, startupForStats);
	reqBytes += SH_TimestampManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_ClasspathManagerImpl2::getRequiredConstrBytes();
	reqBytes += SH_ROMClassManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_ScopeManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_CompiledMethodManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_ByteDataManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_AttachedDataManagerImpl::getRequiredConstrBytes();
	reqBytes += SH_Managers::getRequiredConstrBytes();
	reqBytes += sizeof(SH_CacheMap);
	return reqBytes;
}

/**
 * Clean up resources 
 * 
 * THREADING: Only ever single threaded
 */
void
SH_CacheMap::cleanup(J9VMThread* currentThread)
{
	SH_Manager* walkManager;
	SH_Managers::ManagerWalkState state;
	SH_CompositeCacheImpl* theCC = _ccHead;
	PORT_ACCESS_FROM_PORT(_portlib);
	
	Trc_SHR_CM_cleanup_Entry(currentThread);

	walkManager = managers()->startDo(currentThread, 0, &state);
	while (walkManager) {
		walkManager->cleanup(currentThread);
		walkManager = managers()->nextDo(&state);
	}
	while (theCC) {
		SH_CompositeCacheImpl* nextCC = theCC->getNext();
		theCC->cleanup(currentThread);
		if (_ccHead != theCC) {
			/* _ccHead is deallocated together with sharedClassConfig in j9shr_shutdown() */
			j9mem_free_memory(theCC);
		}
		theCC = nextCC;
	}
	
	if (_sharedClassConfig) {
		this->resetCacheDescriptorList(currentThread, _sharedClassConfig);
	}
	
	if (_refreshMutex) {
		omrthread_monitor_destroy(_refreshMutex);
		_refreshMutex = NULL;
	}
	if (_ccPool) {
		pool_kill(_ccPool);
	}
	Trc_SHR_CM_cleanup_Exit(currentThread);
}

/* Walks the ROMClasses in the ROMClass segment performing a quick check to make
 * sure that the walk produces sensible results. Any failure in the walk should result
 * in a corrupted cache behaviour. */
UDATA
SH_CacheMap::sanityWalkROMClassSegment(J9VMThread* currentThread, SH_CompositeCacheImpl* cache)
{
	U_8 *walk, *prev, *endOfROMSegment;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_sanityWalkROMClassSegment_Entry(currentThread);

	endOfROMSegment = (U_8*)cache->getSegmentAllocPtr();
	walk = (U_8*)cache->getBaseAddress();
	while (walk < endOfROMSegment) {
		prev = walk;
		walk = walk + ((J9ROMClass*)walk)->romSize;
		/* Simply check that we didn't walk backwards, zero or beyond the end of the segment */
		if ((walk <= prev) || (walk > endOfROMSegment)) {
			Trc_SHR_CM_sanityWalkROMClassSegment_ExitBad(currentThread, prev, walk);
			CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_READ_CORRUPT_ROMCLASS, walk);
			cache->setCorruptCache(currentThread, ROMCLASS_CORRUPT, (UDATA)walk);
			return 0;
		}
	}

	Trc_SHR_CM_sanityWalkROMClassSegment_ExitOK(currentThread);
	return 1;
}
	
/**
 * Start up this CacheMap. Should be called after initialization.
 * Sets up access to (or creates) the shared cache and registers it as a ROMClassSegment with the vm.
 * THREADING: Only ever single threaded
 * 
 * @param [in] currentThread  The current thread
 * @param [in] piconfig  The shared class pre-init config
 * @param [in] rootName  The name of the cache to connect to
 * @param [in] cacheDirName  The location of the cache file(s)
 * @param [in] cacheMemoryUT  Used for unit testing. If provided, cache is built into this buffer. 
 * @param [out] cacheHasIntegrity Set to true if the cache is new or has been crc integrity checked, false otherwise
 * 
 * @return 0 on success or -1 for failure
 */
IDATA 
SH_CacheMap::startup(J9VMThread* currentThread, J9SharedClassPreinitConfig* piconfig, const char* rootName, const char* cacheDirName, UDATA cacheDirPerm, BlockPtr cacheMemoryUT, bool* cacheHasIntegrity)
{
	IDATA itemsRead = 0;
	IDATA rc = 0;
	const char* fnName = "startup";
	J9JavaVM* vm = currentThread->javaVM;

	IDATA deleteRC = 1;
	PORT_ACCESS_FROM_PORT(_portlib);
	SH_CompositeCacheImpl* ccToUse = _ccHead;
	SH_CompositeCacheImpl* ccNext = NULL;
	SH_CompositeCacheImpl* ccPrevious = NULL;
	bool isCacheUniqueIdStored = false;

	_actualSize = (U_32)piconfig->sharedClassCacheSize;

	Trc_SHR_CM_startup_Entry(currentThread, rootName, _actualSize);

	if (_sharedClassConfig) {
		_runtimeFlags = &(_sharedClassConfig->runtimeFlags);
		_verboseFlags = _sharedClassConfig->verboseFlags;
	}
	_cacheName = rootName;				/* Store the original name as the cache name */
	_cacheDir = cacheDirName;

	if (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY) {
		/* If running readonly, we can't recreate a cache after we delete it, so disable autopunt */
		*_runtimeFlags &= ~J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID;
	}

	if (omrthread_monitor_init(&_refreshMutex, 0)) {
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_CREATE_REFRESH_MUTEX);
		Trc_SHR_CM_startup_Exit5(currentThread);
		return -1;
	}

	/* _ccHead->startup will set the _actualSize to the real cache size */
	U_32 cacheFileSize = 0;
	bool doRetry = false;
	U_64* runtimeFlags = _runtimeFlags;
	char cacheUniqueID[J9SHR_UNIQUE_CACHE_ID_BUFSIZE];
	memset(cacheUniqueID, 0, sizeof(cacheUniqueID));

	do {
		IDATA tryCntr = 0;
		bool isCcHead = (ccToUse == _ccHead);
		bool storeToCcHead = (ccPrevious == _ccHead);
		const char* cacheUniqueIDPtr = NULL;

		/* start up _ccHead (the top layer cache) and then statrt its pre-requiste cache (ccNext). Contine to startup ccNext and its pre-requiste cache, util there is no more pre-requiste cache.
		 *     _ccHead -------------> ccNext ---------> ccNext --------> ........---------> ccTail
		 *   (top layer)          (middle layer)     (middle layer)      ........         (layer 0)
		 */

		if (!isCcHead) {
			runtimeFlags = &_sharedClassConfig->readOnlyCacheRuntimeFlags;
		}

		do {
			++tryCntr;
			doRetry = false;
			if ((rc == CC_STARTUP_SOFT_RESET) && (deleteRC == -1)) {
				/* If we've tried SOFT_RESET the first time around and the delete failed,
			 	 * remove AUTOKILL so that we start up with the existing cache */
				*runtimeFlags &= ~J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID;
			}

			rc = ccToUse->startup(currentThread, piconfig, cacheMemoryUT, runtimeFlags, _verboseFlags, _cacheName, cacheDirName, cacheDirPerm, &_actualSize, &_localCrashCntr, true, cacheHasIntegrity);
			if (rc == CC_STARTUP_OK) {
				if (sanityWalkROMClassSegment(currentThread, ccToUse) == 0) {
					rc = CC_STARTUP_CORRUPT;
					goto error;
				}

				if (!isCcHead) {
					if (NULL == appendCacheDescriptorList(currentThread, _sharedClassConfig, ccToUse)) {
						CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_ALLOC_DESCRIPTOR);
						Trc_SHR_CM_startup_Exit12(currentThread);
						return -1;
					}
				}
				
				UDATA idLen = 0;
				bool isReadOnly = ccToUse->isRunningReadOnly();
				char cacheDirBuf[J9SH_MAXPATH];
				U_32 cacheType = J9_ARE_ALL_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE) ? J9PORT_SHR_CACHE_TYPE_PERSISTENT : J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
				SH_OSCache::getCacheDir(vm, cacheDirName, cacheDirBuf, J9SH_MAXPATH, cacheType, false);

				if (storeToCcHead && !isCacheUniqueIdStored && !ccPrevious->isRunningReadOnly()) {
					if (ccPrevious->enterWriteMutex(currentThread, false, fnName) == 0) {
						storeCacheUniqueID(currentThread, cacheDirBuf, ccToUse->getCreateTime(), ccToUse->getMetadataBytes(), ccToUse->getClassesBytes(), ccToUse->getLineNumberTableBytes(), ccToUse->getLocalVariableTableBytes(), &cacheUniqueIDPtr, &idLen);
						Trc_SHR_Assert_True(idLen < sizeof(cacheUniqueID));
						memcpy(cacheUniqueID, cacheUniqueIDPtr, idLen);
						cacheUniqueID[idLen] = 0;
						ccPrevious->exitWriteMutex(currentThread, fnName);
					} else {
						CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_ENTER_WRITE_MUTEX_STARTUP);
						Trc_SHR_CM_startup_Exit7(currentThread);
						return -1;
					}
				}

				if (ccToUse->enterWriteMutex(currentThread, false, fnName) == 0) {

					if (false == isCcHead) {
						if (strlen(cacheUniqueID) > 0) {
							if (false == ccToUse->verifyCacheUniqueID(currentThread, cacheUniqueID)) {
								/* modification to a low layer cache has been detected */
								if (_ccHead->isNewCache()) {
									CACHEMAP_TRACE4(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_NEW_LAYER_CACHE_DESTROYED, _ccHead->getLayer(), ccToUse->getLayer(), cacheUniqueID, ccToUse->getCacheUniqueID(currentThread));
									_ccHead->deleteCache(currentThread, true);
								}
								CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_VERIFY_CACHE_ID_FAILED, cacheUniqueID);
								ccToUse->exitWriteMutex(currentThread, fnName);
								Trc_SHR_CM_startup_Exit8(currentThread);
								return -1;
							}
						}
					}

					if (J9_ARE_ANY_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION) && !isReadOnly) {
						ccToUse->setWriteHash(currentThread, 0);				/* Initialize to zero so that peek will work */
					}

					IDATA preqRC = getPrereqCache(currentThread, cacheDirBuf, ccToUse, false, &cacheUniqueIDPtr, &idLen, &isCacheUniqueIdStored);

					if (0 > preqRC) {
						if (CM_CACHE_CORRUPT == preqRC) {
							rc = CC_STARTUP_CORRUPT;
							SH_Managers::ManagerWalkState state;
							SH_Manager* walkManager = managers()->startDo(currentThread, 0, &state);
							while (walkManager) {
								walkManager->cleanup(currentThread);
								walkManager = managers()->nextDo(&state);
							}
							ccToUse->exitWriteMutex(currentThread, fnName);
							goto error;
						}
						CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_GET_PREREQ_CACHE_FAILED);
						ccToUse->exitWriteMutex(currentThread, fnName);
						Trc_SHR_CM_startup_Exit9(currentThread, preqRC);
						return -1;
					} else if (1 == preqRC) {
						UDATA reqBytes = SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, false);
						SH_CompositeCacheImpl* allocPtr = (SH_CompositeCacheImpl*)j9mem_allocate_memory(reqBytes, J9MEM_CATEGORY_CLASSES);
						if (NULL == allocPtr) {
							ccToUse->exitWriteMutex(currentThread, fnName);
							CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_MEMORY_ALLOC_FAILED, reqBytes);
							Trc_SHR_CM_startup_Exit10(currentThread);
							return -1;
						}
						if (0 == _sharedClassConfig->readOnlyCacheRuntimeFlags) {
							_sharedClassConfig->readOnlyCacheRuntimeFlags = (_sharedClassConfig->runtimeFlags | J9SHR_RUNTIMEFLAG_ENABLE_READONLY);
							_sharedClassConfig->readOnlyCacheRuntimeFlags &= ~J9SHR_RUNTIMEFLAG_AUTOKILL_DIFF_BUILDID;
							_readOnlyCacheRuntimeFlags = &_sharedClassConfig->readOnlyCacheRuntimeFlags;
						}
						I_8 preLayer = 0;
						const char* cacheName = _cacheName;
						char cacheNameBuf[USER_SPECIFIED_CACHE_NAME_MAXLEN];
						
						if (isCacheUniqueIdStored) {
							Trc_SHR_Assert_True(idLen < sizeof(cacheUniqueID));
							memcpy(cacheUniqueID, cacheUniqueIDPtr, idLen);
							cacheUniqueID[idLen] = 0;
							SH_OSCache::getCacheNameAndLayerFromUnqiueID(vm, cacheUniqueID, idLen, cacheNameBuf, USER_SPECIFIED_CACHE_NAME_MAXLEN, &preLayer);
							cacheName = cacheNameBuf;
						} else {
							/**
							 * 	The CacheUniqueID of the pre-requisite cache is not stored when a new layer of cache is created (using createLayer or layer=<num> option).
							 * 	Thus, we get the CacheUniqueID of the current cache and decrement the layer number by 1 to get the cacheName and layer number of the pre-requisite cache.
							 */
							preLayer = _sharedClassConfig->layer - 1; 
						}

						ccNext = SH_CompositeCacheImpl::newInstance(vm, _sharedClassConfig, allocPtr, cacheName, cacheType, false, preLayer);
						ccNext->setNext(NULL);
						ccNext->setPrevious(ccToUse);
						ccToUse->setNext(ccNext);
						ccPrevious = ccToUse;
						_ccTail = ccNext;
					} else {
						/* no prereq cache, do nothing */
					}
					ccToUse->exitWriteMutex(currentThread, fnName);
				} else {
					CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_ENTER_WRITE_MUTEX_STARTUP);
					Trc_SHR_CM_startup_Exit7(currentThread);
					return -1;
				}
			}
error:
			if (CC_STARTUP_OK != rc) {
				if (isCcHead) {
					cacheFileSize = _ccHead->getTotalSize();
				}
				handleStartupError(currentThread, ccToUse, rc, *runtimeFlags, _verboseFlags, &doRetry, &deleteRC);

				if (isCcHead && doRetry) {
					if (cacheFileSize > 0) {
						/* If we're recreating, make the new cache the same size as the old 
						 * Cache may be corrupt, so don't rely on values in the cache header to determine size */
						piconfig->sharedClassCacheSize = cacheFileSize;
					}
				}

			}
		} while (doRetry && (tryCntr < 2));
		ccToUse = ccToUse->getNext();
	} while (NULL != ccToUse && CC_STARTUP_OK == rc);

	if (CC_STARTUP_OK != rc) {
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_TO_START_UP);
		Trc_SHR_CM_startup_Exit11(currentThread);
		if (CC_STARTUP_NO_CACHE == rc) {
			return -2;
		}
		return rc;
	}

	setCacheAddressRangeArray();
	ccToUse = _ccTail;
	if (J9_ARE_ALL_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_STATS)) {
		if (UnitTest::CORRUPT_CACHE_TEST != UnitTest::unitTest) {
			Trc_SHR_Assert_True(J9_ARE_ALL_BITS_SET(*runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_READONLY));
		}
		if (J9_ARE_ALL_BITS_SET(vm->sharedCacheAPI->printStatsOptions, PRINTSTATS_SHOW_TOP_LAYER_ONLY)) {
			ccToUse = _ccHead;
		}
	}

	do {
		if (ccToUse == _ccHead) {
			runtimeFlags = _runtimeFlags;
		} else {
			runtimeFlags = _readOnlyCacheRuntimeFlags;
		}
		bool isReadOnly = ccToUse->isRunningReadOnly();
		/* THREADING: We want the cache mutex here as we are reading all available data. Don't want updates happening as we read. */

		if (ccToUse->enterWriteMutex(currentThread, false, fnName) == 0) {
			/* populate the hashtables */
			itemsRead = readCache(currentThread, ccToUse, -1, false);
			ccToUse->protectPartiallyFilledPages(currentThread);
			/* Two reasons for moving the code to check for full cache from SH_CompositeCacheImpl::startup()
			 * to SH_CacheMap::startup():
			 * 	- While marking cache full, last unsused pages are also protected, which ideally should be done
			 * 	  after protecting pages belonging to ROMClass area and metadata area.
			 * 	- Secondly, when setting cache full flags, the code expects to be holding the write mutex, which is not done in
			 * 	  SH_CompositeCacheImpl::startup().
			 *
			 * Do not call fillCacheIfNearlyFull() in readonly mode, as we cannot write anything to cache.
			 */
			if (!isReadOnly) {
				ccToUse->fillCacheIfNearlyFull(currentThread);
			}
			ccToUse->exitWriteMutex(currentThread, fnName);

			if (CM_READ_CACHE_FAILED == itemsRead) {
				Trc_SHR_CM_startup_Exit6(currentThread);
				return -1;
			}
			if (CM_CACHE_CORRUPT == itemsRead) {
				Trc_SHR_CM_startup_Exit13(currentThread);
				rc = CC_STARTUP_CORRUPT;

				SH_Managers::ManagerWalkState state;
				SH_Manager* walkManager = managers()->startDo(currentThread, 0, &state);
				while (walkManager) {
					walkManager->cleanup(currentThread);
					walkManager = managers()->nextDo(&state);
				}
			}
		} else {
			CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_ENTER_WRITE_MUTEX_STARTUP);
			Trc_SHR_CM_startup_Exit7(currentThread);
			return -1;
		}
		if (CC_STARTUP_OK == rc) {
			if (isReadOnly) {
				*runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_READONLY;
				/* If running read-only, treat the cache as full */
				ccToUse->markReadOnlyCacheFull();
			}
			ccToUse = ccToUse->getPrevious();
		}
	} while (NULL != ccToUse && CC_STARTUP_OK == rc);

	if (rc != CC_STARTUP_OK) {
		handleStartupError(currentThread, ccToUse, rc, *runtimeFlags, _verboseFlags, &doRetry, &deleteRC);
		Trc_SHR_CM_startup_Exit1(currentThread);
		return -1;
	}
	
	if (!initializeROMSegmentList(currentThread)) {
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_CREATE_ROMIMAGE);
		Trc_SHR_CM_startup_Exit4(currentThread);
		return -1;
	}

	updateROMSegmentList(currentThread, false, false);

	Trc_SHR_CM_startup_ExitOK(currentThread);
	return 0;
}

/**
 * Handle the SH_CompositeCacheImpl start up error
 * 
 * @param [in] currentThread  The current thread
 * @param [in] ccToUse  The SH_CompositeCacheImpl that was being started up
 * @param [in] errorCode  The SH_CompositeCacheImpl startup error code
 * @param [in] runtimeFlags  The runtime flags
 * @param [in] verboseFlags  Flags controlling the verbose output
 * @param [out] doRetry  Whether to retry starting up the cache
 * @param [out] deleteRC  0 if cache is successful deleted, -1 otherwise.
 */
void
SH_CacheMap::handleStartupError(J9VMThread* currentThread, SH_CompositeCacheImpl* ccToUse, IDATA errorCode, U_64 runtimeFlags, UDATA verboseFlags, bool *doRetry, IDATA *deleteRC)
{
	if (errorCode == CC_STARTUP_CORRUPT) {
		reportCorruptCache(currentThread, ccToUse);
	}
	if (J9_ARE_NO_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_STATS | J9SHR_RUNTIMEFLAG_FAKE_CORRUPTION) 
		&& (false == ccToUse->isRunningReadOnly())
	) {
		/* If the cache is readonly do not delete it or call cleanup().
		 * Cleanup is already called during j9shr_shutdown().
		 * Destroy and clean up only are needed if a 2nd cache is to be opened.
		 *
		 * If the cache is being opened to display stats then do not delete it.
		 */
		if ((errorCode == CC_STARTUP_CORRUPT) || (errorCode == CC_STARTUP_RESET) || (errorCode == CC_STARTUP_SOFT_RESET)) {
			/* If SOFT_RESET, suppress verbose unless "verbose" is explicitly set
				* This will ensure that if the VM can't destroy the cache, we don't get unwanted error messages */
			*deleteRC = ccToUse->deleteCache(currentThread, (errorCode == CC_STARTUP_SOFT_RESET) && !(verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE));
			ccToUse->cleanup(currentThread);
			if (0 == *deleteRC) {
				if (errorCode == CC_STARTUP_CORRUPT) {
					/* Recovering from a corrupted cache, clear the flags which prevent access */
					resetCorruptState(currentThread, FALSE);
				}
			}
			if (J9_ARE_NO_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_RESTORE_CHECK)) {
			/* If the restored cache is corrupted, return CC_STARTUP_CORRUPT and do not retry,
			 * as retry will create another empty cache that is not restored from the snapshot
			 */
				if ((0 == *deleteRC) || (errorCode == CC_STARTUP_SOFT_RESET)) {
					/* If we deleted the cache, or in the case of SOFT_RESET, even if we failed to delete the cache, retry */
					Trc_SHR_Assert_True(ccToUse == _ccHead);
					*_runtimeFlags &= ~J9SHR_RUNTIMEFLAG_DO_NOT_CREATE_CACHE;
					*doRetry = true;
				}
			}
		}
	}

}

/* Assume cc is initialized OK */
/* THREADING: Only ever single threaded */
/* Creates a new ROMClass memory segment and adds it to the avl tree */
J9MemorySegment* 
SH_CacheMap::addNewROMImageSegment(J9VMThread* currentThread, U_8* segmentBase, U_8* segmentEnd)
{
	J9MemorySegment* romSegment = NULL;
	J9JavaVM* vm = currentThread->javaVM;
	UDATA type = MEMORY_TYPE_ROM_CLASS | MEMORY_TYPE_ROM | MEMORY_TYPE_FIXEDSIZE;

	Trc_SHR_CM_addNewROMImageSegment_Entry(currentThread, segmentBase, segmentEnd);

	if ((romSegment = createNewSegment(currentThread, type, vm->classMemorySegments, segmentBase, segmentBase, segmentEnd, segmentBase)) != NULL) {
		avl_insert(&vm->classMemorySegments->avlTreeData, (J9AVLTreeNode *) romSegment);
	}

	Trc_SHR_CM_addNewROMImageSegment_Exit(currentThread, romSegment);

	return romSegment;
}

J9MemorySegment* 
SH_CacheMap::createNewSegment(J9VMThread* currentThread, UDATA type, J9MemorySegmentList* segmentList, 
		U_8* baseAddress, U_8* heapBase, U_8* heapTop, U_8* heapAlloc)
{
	J9MemorySegment* romSegment = NULL;
	J9JavaVM* vm = currentThread->javaVM;

	Trc_SHR_CM_createNewSegment_Entry(currentThread, type, segmentList, baseAddress, heapBase, heapTop, heapAlloc);

	if ((romSegment = vm->internalVMFunctions->allocateMemorySegmentListEntry(segmentList)) != NULL) {
		romSegment->type = type;
		romSegment->size = (heapTop - baseAddress);
		romSegment->baseAddress = baseAddress;
		romSegment->heapBase = heapBase;
		romSegment->heapTop = heapTop;
		romSegment->heapAlloc = heapAlloc;
		romSegment->classLoader = vm->systemClassLoader;
	}

	Trc_SHR_CM_createNewSegment_Exit(currentThread, romSegment);

	return romSegment;
}

/**
 * Updates the heapAlloc of the current ROMClass segment and creates a new segment if this is required.
 * Should be called whenever a cache update has occurred or after a ROMClass has been added to the cache 
 * THREADING: The only time that hasClassSegmentMutex can be false is if the caller does not hold the write mutex
 * storeROMClass prereq that the class segment mutex is held. 
 * Therefore, we can enter the write mutex if we have the class segment mutex, but NOT vice-versa.
 * 
 * @param [in] currentThread  The current thread
 * @param [in] hasClassSegmentMutex  Whether the currrent thread has ClassSegmentMutex
 * @param [in] topLayerOnly  Whether update romClass segment for top layer cache only
 */
void
SH_CacheMap::updateROMSegmentList(J9VMThread* currentThread, bool hasClassSegmentMutex, bool topLayerOnly)
{
	SH_CompositeCacheImpl* cache = _ccHead;
#if defined(J9VM_THR_PREEMPTIVE)
	omrthread_monitor_t classSegmentMutex = currentThread->javaVM->classMemorySegments->segmentMutex;
#endif
	
#if defined(J9VM_THR_PREEMPTIVE)
	if (!hasClassSegmentMutex) {
		Trc_SHR_Assert_ShouldNotHaveLocalMutex(classSegmentMutex);
		Trc_SHR_Assert_False(_ccHead->hasWriteMutex(currentThread));
		Trc_SHR_Assert_False(_ccHead->hasReadMutex(currentThread));
		enterLocalMutex(currentThread, classSegmentMutex, "class segment mutex", "updateROMSegmentList");
	} else {
		Trc_SHR_Assert_ShouldHaveLocalMutex(classSegmentMutex);
	}
#endif
	
	while (cache) {
		if (cache->isStarted()) {
			updateROMSegmentListForCache(currentThread, cache);
		}
		if (topLayerOnly) {
			break;
		}
		cache = cache->getNext();
	}

#if defined(J9VM_THR_PREEMPTIVE)
	if (!hasClassSegmentMutex) {
		exitLocalMutex(currentThread, classSegmentMutex, "class segment mutex", "updateROMSegmentList");
	}
#endif
}

/**
 * THREADING: Should have class segment mutex
 * @param[in] forCache The current supercache, if there are no cachelets. A started cachelet, otherwise.
 */
void
SH_CacheMap::updateROMSegmentListForCache(J9VMThread* currentThread, SH_CompositeCacheImpl* forCache)
{
	J9JavaVM* vm = currentThread->javaVM;
	U_8 *currentSegAlloc, *cacheAlloc;
	J9MemorySegment* currentSegment = forCache->getCurrentROMSegment();
	PORT_ACCESS_FROM_PORT(_portlib);
	
	Trc_SHR_CM_updateROMSegmentList_Entry(currentThread, currentSegment);

	/* TODO: I think we need a pass/fail return value from this */
	if (currentSegment == NULL) {
		if ((currentSegment = addNewROMImageSegment(currentThread, (U_8*)forCache->getBaseAddress(), (U_8*)forCache->getCacheLastEffectiveAddress())) == NULL) {
			Trc_SHR_CM_updateROMSegmentList_addFirstSegmentFailed(currentThread, forCache, forCache->getBaseAddress(), forCache->getCacheLastEffectiveAddress());
			return;
		}
		forCache->setCurrentROMSegment(currentSegment);
	}
	currentSegAlloc = currentSegment->heapAlloc;
	cacheAlloc = (U_8*)forCache->getSegmentAllocPtr();

	/* If there is a cache update which is not reflected in the current ROMClass segment... */
	if (currentSegAlloc < cacheAlloc) {
		U_8* currentROMClass = currentSegAlloc;
		UDATA currentSegSize = currentSegment->heapAlloc - currentSegment->heapBase;
		UDATA maxSegmentSize = vm->romClassAllocationIncrement;

		/* Walk ROMClasses to the limit of cacheAlloc */
		while (currentROMClass < cacheAlloc) {
			UDATA currentROMSize = ((J9ROMClass*)currentROMClass)->romSize;

			/* If the current segment is about to become too large, try to create a new one */
			if ((currentSegSize + currentROMSize) > maxSegmentSize) {
				J9MemorySegment* newSegment;

				/* Failure to create a new segment is fairly fatal - j9mem_allocate_memory would have failed. Continue to use existing segment. */
				if ((newSegment = addNewROMImageSegment(currentThread, currentROMClass, (U_8*)forCache->getCacheLastEffectiveAddress()))) {
					/* Now that we know the limits of the current segment, set these fields */
					currentSegment->heapTop = currentROMClass;
					currentSegment->heapAlloc = currentROMClass;
					currentSegment->size = (currentSegment->heapTop - currentSegment->heapBase);
					forCache->setCurrentROMSegment(newSegment);
					currentSegment = newSegment;
					currentSegSize = 0;
				} else {
					Trc_SHR_CM_updateROMSegmentList_addSegmentFailed(currentThread, forCache, 
							currentROMClass, forCache->getCacheLastEffectiveAddress(), currentSegment);
				}
			} else if (currentROMSize <= 0) {
				CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_READ_CORRUPT_ROMCLASS, currentROMClass);
				forCache->setCorruptCache(currentThread, ROMCLASS_CORRUPT, (UDATA)currentROMClass);
				reportCorruptCache(currentThread, forCache);
				break;
			}
			currentSegSize += currentROMSize;
			currentROMClass += currentROMSize;
		}
		currentSegment->heapAlloc = cacheAlloc;
		VM_AtomicSupport::writeBarrier();
		Trc_SHR_CM_updateROMSegmentList_NewHeapAlloc(currentThread, currentSegment, cacheAlloc);
	}

	Trc_SHR_CM_updateROMSegmentList_Exit(currentThread, currentSegment);
}

/** 
 * Assume cc is initialized OK
 * @retval 1 success
 * @retval 0 failure
 */
UDATA 
SH_CacheMap::initializeROMSegmentList(J9VMThread* currentThread)
{
	J9JavaVM* vm = currentThread->javaVM;
	UDATA result = 1;
	J9SharedClassConfig* config;
	U_8 *cacheBase, *cacheDebugAreaStart;
	BlockPtr firstROMClassAddress;
	omrthread_monitor_t classSegmentMutex = vm->classMemorySegments->segmentMutex;
	omrthread_monitor_t memorySegmentMutex = vm->memorySegments->segmentMutex;

	Trc_SHR_Assert_ShouldNotHaveLocalMutex(classSegmentMutex);
	Trc_SHR_Assert_True(_sharedClassConfig != NULL);
	Trc_SHR_CM_initializeROMSegmentList_Entry(currentThread);

	cacheBase = (U_8*)_ccHead->getBaseAddress();
	firstROMClassAddress = _ccHead->getFirstROMClassAddress();
	/* Subtract sizeof(ShcItemHdr) from end address, because when the cache is mapped
	 * to the end of memory, and -Xscdmx0 is used, then cacheDebugAreaStart may equal NULL
	 */
	cacheDebugAreaStart = (U_8*)_ccHead->getCacheEndAddress() - sizeof(ShcItemHdr);
	config = _sharedClassConfig;

	if (config->configMonitor) {
		enterLocalMutex(currentThread, config->configMonitor, "config monitor", "initializeROMSegmentList");
	}

	/* config->cacheDescriptorList always refers to the current supercache */
	if (config->cacheDescriptorList->cacheStartAddress) {
		Trc_SHR_Assert_True(config->cacheDescriptorList->cacheStartAddress == _ccHead->getCacheHeaderAddress());
	} else {
		config->cacheDescriptorList->cacheStartAddress = _ccHead->getCacheHeaderAddress();
	}
	Trc_SHR_Assert_True(config->cacheDescriptorList->cacheStartAddress != NULL);
	config->cacheDescriptorList->romclassStartAddress = firstROMClassAddress;
	config->cacheDescriptorList->metadataStartAddress = cacheDebugAreaStart;
	config->cacheDescriptorList->cacheSizeBytes = _ccHead->getCacheMemorySize();

#if defined(J9VM_THR_PREEMPTIVE)
	if (memorySegmentMutex) {
		enterLocalMutex(currentThread, memorySegmentMutex, "memory segment mutex", "initializeROMSegmentList");
	}
#endif

	SH_CompositeCacheImpl* ccToUse = _ccHead;
	J9SharedClassCacheDescriptor* cacheDesc = config->cacheDescriptorList;
	do {
		U_8* cacheDebugAreaStartCC = (U_8*)ccToUse->getCacheEndAddress() - sizeof(ShcItemHdr);
		Trc_SHR_Assert_True(cacheDebugAreaStartCC == cacheDesc->metadataStartAddress);
		cacheBase = (U_8*)ccToUse->getBaseAddress();

		J9MemorySegment* metaSegment = createNewSegment(currentThread, MEMORY_TYPE_SHARED_META, vm->memorySegments, cacheBase, (U_8*)ccToUse->getMetaAllocPtr(), cacheDebugAreaStartCC, cacheDebugAreaStartCC);
		if (NULL == metaSegment) {
			result = 0;
			break;
		}
		if ((UnitTest::COMPILED_METHOD_TEST != UnitTest::unitTest) 
			&& (UnitTest::CACHE_FULL_TEST != UnitTest::unitTest)
		) {
			Trc_SHR_Assert_True(NULL == cacheDesc->metadataMemorySegment);
		}
		cacheDesc->metadataMemorySegment = metaSegment;
		if (ccToUse == _ccHead) {
			config->metadataMemorySegment = metaSegment;
		} else {
			ccToUse->setMetadataMemorySegment(&cacheDesc->metadataMemorySegment);
		}
		ccToUse = ccToUse->getNext();
		cacheDesc = cacheDesc->next;
	} while (NULL != ccToUse);
	Trc_SHR_Assert_True(cacheDesc == config->cacheDescriptorList);

#if defined(J9VM_THR_PREEMPTIVE)
	if (memorySegmentMutex) {
		exitLocalMutex(currentThread, memorySegmentMutex, "memory segment mutex", "initializeROMSegmentList");
	}
#endif

	if (config->configMonitor) {
		exitLocalMutex(currentThread, config->configMonitor, "config monitor", "initializeROMSegmentList");
	}

	Trc_SHR_CM_initializeROMSegmentList_Exit(currentThread, result);

	return result;
}

/* Need to update bytesRead atomically because it's not protected by any kind of mutex */
void
SH_CacheMap::updateBytesRead(UDATA numBytes) 
{
	UDATA oldNum, value;
	
	oldNum = _bytesRead;
	do {
		value = oldNum + numBytes;
		oldNum = VM_AtomicSupport::lockCompareExchange(&_bytesRead, oldNum, value);
	} while (value != (oldNum + numBytes));
}

/* Returns 1 if manager is started successfully, 0 if the manager is shut down or -1 for error */
IDATA
SH_CacheMap::startManager(J9VMThread* currentThread, SH_Manager* manager)
{
	bool doExitRefreshMutex = false;

	/* If a known dataType, but the manager has not been started yet */
	if ((manager != NULL) && (manager->getState() != MANAGER_STATE_STARTED)) {
		IDATA rc;
		
		/* If manager has been shut down, don't try to re-start it */
		if (manager->getState() == MANAGER_STATE_SHUTDOWN) {
			Trc_SHR_Assert_ShouldNeverHappen();
			return 0;
		}
		/* Although the manager startup routine can handle multi-threading without a lock,
		 * it is important that it is not possible for another thread to call shutDown while this is running */
		if (!omrthread_monitor_owned_by_self(_refreshMutex)) {
			enterRefreshMutex(currentThread, "startManager");
			doExitRefreshMutex = true;
		}
		rc = (manager->startup(currentThread, _runtimeFlags, _verboseFlags, _actualSize) != 0);

		/* Manager either in wrong state to start or there was an error starting it */
		while ((rc != -1) && (manager->getState() != MANAGER_STATE_STARTED)) {
			/* Keep trying if it was in the wrong state */
			omrthread_sleep(10);
			rc = (manager->startup(currentThread, _runtimeFlags, _verboseFlags, _actualSize) != 0);
		}
		if (rc == -1) {
			return -1;
		}
		if (doExitRefreshMutex) {
			exitRefreshMutex(currentThread, "startManager");
		}
	}
	return 1;
}

/* THREADING: Could be called at any time by any thread. Concurrent manager startup handled
 * Returns the dataType of the successfully started manager, or 0 if the dataType is unknown or manager has been shutdown, or -1 for manager startup failure */
IDATA 
SH_CacheMap::getAndStartManagerForType(J9VMThread* currentThread, UDATA dataType, SH_Manager** startedManager)
{
	SH_Manager* manager;
	IDATA result = 0;
	
	if ((manager = managers()->getManagerForDataType(dataType))) {
		if ((result = startManager(currentThread, manager)) == 1) {
			result = dataType;
		}
	}

	*startedManager = manager;
	return result;
}

IDATA
SH_CacheMap::readCacheUpdates(J9VMThread* currentThread)
{
	IDATA itemsRead = 0;
	/* Lower layer caches are not expected to be updated. Only care about updates of _ccHead in mutiple layer cache scenario */
	SH_CompositeCacheImpl* cache = _ccHead;
	IDATA availableCacheUpdates = 0;
	
	while (cache) {
		if (cache->isStarted() && (availableCacheUpdates = cache->checkUpdates(currentThread))) {
			IDATA cacheResult;

			/* First read is the supercache - this in theory could add more cachelets */
			cacheResult = readCache(currentThread, cache, availableCacheUpdates, false);
			if ((CM_READ_CACHE_FAILED != cacheResult) && (CM_CACHE_CORRUPT != cacheResult)) {
				itemsRead += cacheResult;
			} else {
				return -1;
			}
		}
		cache = cache->getPrevious();
	}
	return itemsRead;
}

/* THREADING: MUST be single-threaded - protected by refreshMutex or cache write mutex 
 * If expectedUpdates == -1, this indicates to read until no more data is found. 
 * Otherwise, only read expectedUpdates entries.
 *
 * @return	number of entries successfully read, or
 * 			CM_READ_CACHE_FAILED if the call fails for some reason, or
 * 			CM_CACHE_CORRUPT if cache is corrupt
 */
IDATA
SH_CacheMap::readCache(J9VMThread* currentThread, SH_CompositeCacheImpl* cache, IDATA expectedUpdates, bool startupForStats)
{
	ShcItem* it = NULL;
	IDATA result = 0;
	IDATA expectedCntr = expectedUpdates;
	SH_Manager* manager = NULL;
	PORT_ACCESS_FROM_PORT(_portlib);
	
	if (!cache->hasWriteMutex(currentThread)) {
		Trc_SHR_Assert_ShouldHaveLocalMutex(_refreshMutex);
	}

	Trc_SHR_CM_readCache_Entry(currentThread, expectedUpdates);

	/* For each cached item, find a suitable manager and store it */
	do {
		it = (ShcItem*)cache->nextEntry(currentThread, NULL);		/* IMPORTANT: Do not skip stale entries (can end up with lone orphans) */
		if (it) {
			IDATA rc;
			UDATA itemType = ITEMTYPE(it);
			
			if ((itemType <= TYPE_UNINITIALIZED) || (itemType > MAX_DATA_TYPES)) {
				CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_READ_CORRUPT_DATA, it);
				if (startupForStats == false) {
					cache->setCorruptCache(currentThread, ITEM_TYPE_CORRUPT, (UDATA)it);
				}
				Trc_SHR_CM_readCache_Exit1(currentThread, it);
				result = CM_CACHE_CORRUPT;
			} else {
				rc = getAndStartManagerForType(currentThread, itemType, &manager);

				if (rc == -1) {
					/* Manager failed to start - ignore */
					Trc_SHR_CM_readCache_EventFailedStore(currentThread, it);
					++result;
				} else if ((rc > 0) && ((UDATA)rc == itemType)) {
					/* Success - we have a started manager */
					if (manager->storeNew(currentThread, it, cache)) {
						if (expectedCntr != -1) {
							--expectedCntr;
						}
						++result;
					} else {
						CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_HASHTABLE_ADD_FAILURE);
						Trc_SHR_CM_readCache_Exit2(currentThread);
						result = CM_READ_CACHE_FAILED;
					}
				} else {
					/* We found a manager, but for the wrong data type */
					Trc_SHR_Assert_ShouldNeverHappen();
					result = CM_READ_CACHE_FAILED;
				}
			}
		}
	} while ((it != NULL) && (result != CM_READ_CACHE_FAILED) && (result != CM_CACHE_CORRUPT) && (expectedCntr==-1 || expectedCntr>0));
	
	if ((false == startupForStats) && (cache->isCacheCorrupt())) {
		reportCorruptCache(currentThread, cache);
		if (NULL == it) {
			/* This happens when nextEntry() finds cache to be corrupt and return NULL */
			result = CM_CACHE_CORRUPT;
		}
	}

	if ((expectedUpdates != -1) && (result != expectedUpdates)) {
		Trc_SHR_CM_readCache_Event_NotMatched(currentThread, expectedUpdates, result);
	}

	cache->doneReadUpdates(currentThread, result);

	Trc_SHR_CM_readCache_Exit(currentThread, expectedUpdates, result);
	return result;
}

/* THREADING: MUST be protected by cache write mutex - therefore single-threaded within this JVM */
IDATA
SH_CacheMap::checkForCrash(J9VMThread* currentThread, bool hasClassSegmentMutex)
{
	IDATA rc = 0;
	PORT_ACCESS_FROM_PORT(_portlib);
	
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));

	if (_ccHead->crashDetected(&_localCrashCntr)) {
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_INFO, J9NLS_SHRC_CM_UNEXPECTED_TERMINATION_DETECTED);
		Trc_SHR_CM_checkForCrash_Exception(currentThread);

		if (resetAllManagers(currentThread) != 0) {
			return -1;
		}
		_cc->reset(currentThread);
		rc = refreshHashtables(currentThread, hasClassSegmentMutex);
	}
	return rc;
}

/**
 * Update hashtables with new data that might have appeared in the cache.
 * 
 * THREADING: This function is called multi-threaded and can be called with either the writeMutex held or not. 
 * Since we never try to get the writeMutex while holding the refreshMutex, there is no risk of deadlock
 * 
 * The refreshMutex is held throughout this function, so it's threadsafe.
 * 
 * @return the number of items read, or -1 on error
 */
IDATA 
SH_CacheMap::refreshHashtables(J9VMThread* currentThread, bool hasClassSegmentMutex)
{
	IDATA itemsRead = 0;

	Trc_SHR_CM_refreshHashtables_Entry(currentThread);

	_ccHead->updateRuntimeFullFlags(currentThread);

	if (enterRefreshMutex(currentThread, "refreshHashtables")==0) {
		itemsRead = readCacheUpdates(currentThread);
		if ((UnitTest::CACHE_FULL_TEST != UnitTest::unitTest)
			|| (itemsRead > 0)
		) {
			/* A previous call might enter here with hasClassSegmentMutex = false, which added romclasses to the hashtable without updating the 
			 * romClass segment list. In this case updateROMSegmentList() needs to be called this time if hasClassSegmentMutex is true, 
			 * regaredless of the itemsRead value.
			 */
			if (hasClassSegmentMutex) {
				/* Only refresh the segment list if we hold the class segment mutex. This is because:
				 * a) we need the mutex to call the function
				 * b) the class segment mutex cannot be entered if we hold the write mutex
				 * findROMClass and storeROMClass prereq holding the class segment mutex.
				 * For other types of find and store, the segment list is irrelevant */ 
				updateROMSegmentList(currentThread, true);
			}
		}
		_ccHead->updateMetadataSegment(currentThread);
		 if( _ccHead->isCacheCorrupt()) {
			 exitRefreshMutex(currentThread, "refreshHashtables");
			 Trc_SHR_CM_refreshHashtables_Corrupt_Exit(currentThread);
				return -1;
		}
		exitRefreshMutex(currentThread, "refreshHashtables");
	}

	Trc_SHR_CM_refreshHashtables_Exit(currentThread, itemsRead);
	return itemsRead;
}

/* THREADING: Will only guarantee to return correct results with either cache read or write mutex held. No need for local mutex. */
IDATA 
SH_CacheMap::isStale(const ShcItem* item)
{
	Trc_SHR_CM_isStale_Entry(item);

  	if (item) {
		if (!_ccHead->stale((BlockPtr)ITEMEND(item))) {
			Trc_SHR_CM_isStale_ExitNotStale(item);
		  	return 0;
		} else {
			Trc_SHR_CM_isStale_ExitStale(item);
			return 1;
		}
	}
	Trc_SHR_CM_isStale_ExitNoItem();
  	return -1;
}

IDATA
SH_CacheMap::resetAllManagers(J9VMThread* currentThread)
{		
	SH_Manager* walkManager;
	SH_Managers::ManagerWalkState state;
	
	walkManager = managers()->startDo(currentThread, 0, &state);
	while (walkManager) {
		if (walkManager->reset(currentThread)) {
			return -1;
		}
		walkManager = managers()->nextDo(&state);
	}
	return 0;
}

/* THREADING: Must have cache write mutex */
SH_CompositeCacheImpl*
SH_CacheMap::getCacheAreaForDataType(J9VMThread* currentThread, UDATA dataType, UDATA dataLength)
{
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
    return _ccHead;
}

void
SH_CacheMap::updateAverageWriteHashTime(UDATA actualTimeMicros) 
{
	if (actualTimeMicros > WRITE_HASH_WAIT_MAX_MICROS) {
		if ((IDATA)actualTimeMicros < 0) {
			/* Ignore negative values */
			return;
		}
		actualTimeMicros = WRITE_HASH_WAIT_MAX_MICROS;
	}
	
	if (actualTimeMicros > _writeHashMaxWaitMicros) {
		_writeHashMaxWaitMicros = actualTimeMicros;
	}
	if (_writeHashAverageTimeMicros != 0) {
		/* Calculate a weighted average where the existing average is 10 times as heavy
		 * as the latest sample.
		 */
		_writeHashAverageTimeMicros = ((_writeHashAverageTimeMicros * 10) + actualTimeMicros) / 11;
	} else {
		_writeHashAverageTimeMicros = actualTimeMicros;
	}
}

/* THREADING: It is essential to ensure that the cpeTable is up to date before doing these checks 
 * It is also important that no-one else monkeys with the timestamps in the cache or does a stale mark
 * while this routine is running, therefore it is protected with mutex. 
 * Note also that, because it is relatively expensive to compare against existing classpaths in the cache, 
 * we hold onto the mutex right through until all necessary cache updates have been made. This way, we
 * don't have to see if anyone has got there underneath us when we enter addClasspathToCache. */
/* THREADING: Can be invoked multi-threaded. Caller must hold the VM class segment mutex. */
ClasspathWrapper* 
SH_CacheMap::updateClasspathInfo(J9VMThread* currentThread, ClasspathItem* cp, I_16 cpeIndex, const J9UTF8* partition, const J9UTF8** cachedPartition, const J9UTF8* modContext, const J9UTF8** cachedModContext, bool haveWriteMutex) {
	ClasspathWrapper* answer = NULL;
	const char* fnName = "updateClasspathInfo";
	SH_ClasspathManager* localCPM;
	IDATA enteredWriteMutex = 0;
	
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);	

	if (!(localCPM = getClasspathManager(currentThread))) { 
		return NULL;
	}
	
	Trc_SHR_CM_updateClasspathInfo_Entry(currentThread, cp->getHelperID(), cpeIndex);	

	if (haveWriteMutex==false) {
		enteredWriteMutex = _ccHead->enterWriteMutex(currentThread, false, fnName);
	}

	if (enteredWriteMutex==0) {			/* false = do not lock cache */
		if (haveWriteMutex==false && (runEntryPointChecks(currentThread, NULL, NULL) == -1)) {
			Trc_SHR_CM_updateClasspathInfo_ExitNull1(currentThread);
			goto _exitNULLWithMutex;
		}
		if (localCPM->update(currentThread, cp, cpeIndex, &answer)) {
			Trc_SHR_CM_updateClasspathInfo_ExitNull2(currentThread);
			goto _exitNULLWithMutex;
		}
		if (!answer) {
			answer = addClasspathToCache(currentThread, cp);
		}
		if (partition || modContext) {
			SH_CompositeCacheImpl* preCC = _cc; 
			
			for (IDATA i=0; i<2; i++) {
				/* We're only interested in the scope manager if it has been started. It will have been started if there are scopes in the cache */
				if (_scm->getState() == MANAGER_STATE_STARTED) {
					*cachedPartition = _scm->findScopeForUTF(currentThread, partition);
					*cachedModContext = _scm->findScopeForUTF(currentThread, modContext);
				}
				/* Stick partition and modContext into main cache */
				if (partition && !(*cachedPartition)) {
					if (!(*cachedPartition = addScopeToCache(currentThread, partition))) {
						Trc_SHR_CM_updateClasspathInfo_ExitNull4(currentThread);
						goto _exitNULLWithMutex;
					}
				}
				if (modContext && !(*cachedModContext)) {
					if (!(*cachedModContext = addScopeToCache(currentThread, modContext))) {
						Trc_SHR_CM_updateClasspathInfo_ExitNull5(currentThread);
						goto _exitNULLWithMutex;
					}
				}
				/* If a new supercache was created during this block, go round it again */
				if (preCC == _cc) {
					break;
				}
			}
		}
		if (haveWriteMutex == false) {
			_ccHead->exitWriteMutex(currentThread, fnName);
		}
	}

	Trc_SHR_CM_updateClasspathInfo_Exit(currentThread, answer);	
	return answer;

_exitNULLWithMutex:
	if (haveWriteMutex == false) {
		_ccHead->exitWriteMutex(currentThread, fnName);
	}
	return NULL;
}

/* THREADING: MUST be called with cache write mutex held */
const J9UTF8*
SH_CacheMap::addScopeToCache(J9VMThread* currentThread, const J9UTF8* scope, U_16 type)
{
	const J9UTF8* result = NULL;
	ShcItem item;
	ShcItem* itemPtr = &item;
	ShcItem* itemInCache = NULL;
	U_32 totalSizeNeeded = J9UTF8_LENGTH(scope) + sizeof(struct J9UTF8);
	SH_ScopeManager* localSCM;
	SH_CompositeCacheImpl* cacheForAllocate;

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_True((TYPE_SCOPE == type) || (TYPE_PREREQ_CACHE == type));

	if (!(localSCM = getScopeManager(currentThread))) {
		return NULL;
	}

	if (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE) {
		/* Don't update the cache */
		increaseUnstoredBytes(totalSizeNeeded);
		return NULL;
	}

	Trc_SHR_CM_addScopeToCache_Entry(currentThread, J9UTF8_LENGTH(scope), J9UTF8_DATA(scope));

	_ccHead->initBlockData(&itemPtr, totalSizeNeeded, type);
	cacheForAllocate = getCacheAreaForDataType(currentThread, type, _ccHead->getBytesRequiredForItemWithAlign(itemPtr, SHC_WORDALIGN, 0));
	if (!cacheForAllocate) {
		/* This may indicate size required is bigger than the cachelet size. */
		/* TODO: In offline mode, should be fatal */
		return NULL;
	}
	
	itemInCache = (ShcItem*)cacheForAllocate->allocateBlock(currentThread, itemPtr, SHC_WORDALIGN, 0);
	if (itemInCache == NULL) {
		/* Not enough space in cache to accommodate this item. */
		Trc_SHR_CM_addScopeToCache_Exit_Null(currentThread);
		return NULL;
	}
	memcpy(ITEMDATA(itemInCache), scope, totalSizeNeeded);
	if (localSCM->storeNew(currentThread, itemInCache, cacheForAllocate)) {
		result = (const J9UTF8*)ITEMDATA(itemInCache);
	}
	cacheForAllocate->commitUpdate(currentThread, false);

	Trc_SHR_CM_addScopeToCache_Exit(currentThread, result);
	return result;
}

/* THREADING: We hold mutex for whole classpath update. 
 * Therefore, no need to check to see if anyone else has already stored same classpath */
/* THREADING: MUST be called with cache write mutex held */
ClasspathWrapper* 
SH_CacheMap::addClasspathToCache(J9VMThread* currentThread, ClasspathItem* obj) 
{
	ClasspathWrapper* result = NULL;
	ShcItem item;
	ShcItem* itemPtr = &item;
	ShcItem* itemInCache = NULL;
	ClasspathWrapper newCpw;
	U_32 cpiSize = obj->getSizeNeeded();
	U_32 totalSizeNeeded = sizeof(ClasspathWrapper) + cpiSize;
	SH_ClasspathManager* localCPM;
	SH_CompositeCacheImpl* cacheAreaForAllocate;

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));

	if (!(localCPM = getClasspathManager(currentThread))) { 
		return 0;
	}

	if (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE) {
		increaseUnstoredBytes(totalSizeNeeded);
		return NULL;
	}

	Trc_SHR_CM_addClasspathToCache_Entry(currentThread, obj->getHelperID());

	if (false == _ccHead->canStoreClasspaths()) {
		Trc_SHR_CM_addClasspathToCache_NotAllowedToStoreClasspaths_Exit_Null(currentThread);
		return NULL;
	}

	_ccHead->initBlockData(&itemPtr, totalSizeNeeded, TYPE_CLASSPATH);
	cacheAreaForAllocate = getCacheAreaForDataType(currentThread, TYPE_CLASSPATH, _ccHead->getBytesRequiredForItemWithAlign(itemPtr, SHC_WORDALIGN, 0));
	if (!cacheAreaForAllocate) {
		/* This may indicate size required is bigger than the cachelet size. */
		/* TODO: In offline mode, should be fatal */
		return NULL;
	}
	
	itemInCache = (ShcItem*)cacheAreaForAllocate->allocateBlock(currentThread, itemPtr, SHC_WORDALIGN, 0);
	if (itemInCache == NULL) {
		/* Not enough space in cache to accommodate this item. */
		Trc_SHR_CM_addClasspathToCache_Exit_Null(currentThread);
		return NULL;
	}
	newCpw.staleFromIndex = CPW_NOT_STALE;
	newCpw.classpathItemSize = cpiSize;
	/* No name to write for classpathItem */
	memcpy(ITEMDATA(itemInCache), &newCpw, sizeof(ClasspathWrapper));
	obj->writeToAddress((BlockPtr)CPWDATA(ITEMDATA(itemInCache)));
	if (obj->getType() != CP_TYPE_TOKEN) {
		localCPM->setTimestamps(currentThread, (ClasspathWrapper*)ITEMDATA(itemInCache));
	}
	if (localCPM->storeNew(currentThread, itemInCache, cacheAreaForAllocate)) {
		result = (ClasspathWrapper*)ITEMDATA(itemInCache);
	}
	cacheAreaForAllocate->commitUpdate(currentThread, false);

	Trc_SHR_CM_addClasspathToCache_Exit(currentThread, obj->getHelperID(), result);
	return result;
}

/* Should be run before any find/store operation on the cache. 
 * THREADING: Should be called with either the read mutex or the write mutex held.
 * 
 * @return the number of items read, or -1 on error
 */
IDATA 
SH_CacheMap::runEntryPointChecks(J9VMThread* currentThread, void* address, const char** p_subcstr)
{
	bool hasClassSegmentMutex = false;
	IDATA itemsAdded;
	IDATA rc;
	
	PORT_ACCESS_FROM_VMC(currentThread);
	Trc_SHR_CM_runEntryPointChecks_Entry(currentThread);

	hasClassSegmentMutex = omrthread_monitor_owned_by_self(currentThread->javaVM->classMemorySegments->segmentMutex) != 0;
	if (_ccHead->isCacheCorrupt()) {
		reportCorruptCache(currentThread, _ccHead);
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_CACHE_CORRUPT, "cache is corrupt");
		}
		Trc_SHR_CM_runEntryPointChecks_Exit_Failed1(currentThread);
		return -1;
	}

	/* Optional check */
	if (address) {
		if (!isAddressInCache(address, 0, true, false)) {
			if (NULL != p_subcstr) {
				*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_ADDRESS_NOT_IN_CACHE, "address is not in cache");
			}
			Trc_SHR_CM_runEntryPointChecks_Exit_Failed2(currentThread);
			return -1;
		}
	}

	if (!_ccHead->isRunningReadOnly()) {
		if (_ccHead->hasWriteMutex(currentThread)) {
			/* Can only call this function if we have the write mutex */
			rc = checkForCrash(currentThread, hasClassSegmentMutex);
			if(rc < 0) {
				Trc_SHR_CM_runEntryPointChecks_Exit_Failed4(currentThread);
				return rc;
			}
		}
	}

	itemsAdded = refreshHashtables(currentThread, hasClassSegmentMutex);
	if (-1 == itemsAdded) {
		/* Error reported */
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),  J9NLS_SHRC_CM_CACHE_REFRESH_FAILED, "cache refresh failed");
		}
		Trc_SHR_CM_runEntryPointChecks_Exit_Failed3(currentThread);
		return -1;
	} else if (itemsAdded > 0) {
		bool doExitMutex = false;
		bool hasMutex = false;

		/* If we don't have write mutex, then we are certainly being called during 'find' operation.
		 * In that case protect partially filled pages only if "mprotect=onfind" is set.
		 */
		if (_ccHead->hasWriteMutex(currentThread)) {
			hasMutex = true;
		} else if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND)) {
			hasMutex = (0 == _ccHead->enterWriteMutex(currentThread, false, "runEntryPointChecks"));
			if (hasMutex) {
				doExitMutex = true;
			}
		}

		if (true == hasMutex) {
			_ccHead->protectPartiallyFilledPages(currentThread);
			if (doExitMutex) {
				_ccHead->exitWriteMutex(currentThread, "runEntryPointChecks");
			}
		}
	}

	Trc_SHR_CM_runEntryPointChecks_Exit_OK(currentThread);
	return itemsAdded;
}

/**
 * Allocate memory for a Orphan ROMClass or a ROMClass from the shared classes cache, along 
 * with all its pieces specified in 'const J9RomClassRequirements * sizes'.
 * On success J9SharedRomClassPieces * pieces is updated to contain pointers memory that
 * was allocated for this ROMClass.
 * 
 * Note: The rom class is only added to the cache when SH_CacheMap::commitROMClass() is called.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] sizes Size of the ROMClass, and its parts.
 * @param [in] pieces The results of successfully calling this method.
 * @param [in] classnameLength length of the class name
 * @param [in] classnameData class name data
 * @param [in] cpw classpath wrapper
 * @param [in] partitionInCache partition info
 * @param [in] modContextInCache modification context info
 * @param [in] callerHelperID helper id
 * @param [in] modifiedNoContext true if class we are storing will have modified bytecode
 * @param [out] newItemInCache new item that we alloc'd from cache
 * @param [out] cacheAreaForAllocate where we alloc'd the new item from the cache
 *
 * @return true if pieces points to a newly allocated ROMClass
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
bool
SH_CacheMap::allocateROMClass(J9VMThread* currentThread, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, IDATA callerHelperID, bool modifiedNoContext, void * &newItemInCache, void * &cacheAreaForAllocate)
{
	bool retval = false;
	U_32 romclassSizeToUse = 0;
	bool allocatedDebugMem = false;

	Trc_SHR_CM_allocateROMClass_Entry3(currentThread, classnameLength, classnameData, sizes->romClassSizeFullSize, sizes->romClassMinimalSize, sizes->lineNumberTableSize, sizes->localVariableTableSize);

	Trc_SHR_Assert_True(NULL != sizes);
	Trc_SHR_Assert_True((sizes->romClassMinimalSize <= sizes->romClassSizeFullSize));

	/*Try to allocate debug data in the cache meta data.*/
	if ((sizes->lineNumberTableSize > 0) || (sizes->localVariableTableSize > 0)) {
		IDATA rc = -1;
		rc = this->allocateClassDebugData(currentThread, classnameLength, classnameData, sizes, pieces);
		/*Store the allocated memory in the transaction for debugging only.*/
		if (rc == -1) {
			Trc_SHR_CM_allocateROMClass_FailedToAllocDebugAttributes_Event(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, pieces->lineNumberTable, sizes->localVariableTableSize, pieces->localVariableTable);
			allocatedDebugMem = false;
		} else {
			Trc_SHR_CM_allocateROMClass_AllocatedDebugAttributes_Event(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, pieces->lineNumberTable, sizes->localVariableTableSize, pieces->localVariableTable);
			allocatedDebugMem = true;
		}
	}

	if (false == allocatedDebugMem) {
		romclassSizeToUse = sizes->romClassSizeFullSize;
		pieces->flags = J9SC_ROMCLASS_PIECES_USED_FULL_SIZE;
	} else {
		pieces->flags = 0;
		romclassSizeToUse = sizes->romClassMinimalSize;
		if (true == allocatedDebugMem) {
			pieces->flags |= J9SC_ROMCLASS_PIECES_DEBUG_DATA_OUT_OF_LINE;
		} else {
			romclassSizeToUse += (sizes->lineNumberTableSize + sizes->localVariableTableSize);
			/* If suddenly writing the debug data inline then we need to pad romclassSizeToUse to
			 * U_64. This is done because all romClass's must be 64 bit aligned, and debug data is
			 * aligned U_16 when calculated out of line.
			 */
			romclassSizeToUse += sizeof(U_64);
			romclassSizeToUse &= ~(sizeof(U_64)-1);
		}
	}
	
	/*Ensure the rom class size is always 64 bit aligned*/
	Trc_SHR_Assert_True(0 == (romclassSizeToUse & (sizeof(U_64)-1)));
	
	pieces->romClass = (void *) allocateROMClassOnly(currentThread, romclassSizeToUse, classnameLength, classnameData, cpw, partitionInCache, modContextInCache, callerHelperID, modifiedNoContext, newItemInCache, cacheAreaForAllocate);

	if ((NULL != newItemInCache)
		&& (_ccHead->isNewCache()) 
		&& (false == _metadataReleased)
	) {
		/* Update the min/max boundary with the stored metadata entry only when the cache is
		 * being created by the current VM.
		 */
#if !defined(J9ZOS390) && !defined(AIXPPC)
#if defined(LINUX)
		if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE))
#endif
		{
			updateAccessedShrCacheMetadataBounds(currentThread, (uintptr_t *) ITEMDATA(newItemInCache));
		}
#endif /* !defined(J9ZOS390) && !defined(AIXPPC) */
	}

	if ((true == allocatedDebugMem) && (NULL == pieces->romClass)) {
		Trc_SHR_CM_allocateROMClass_FailedToRomClassRollbackDebug_Event(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, pieces->lineNumberTable, sizes->localVariableTableSize, pieces->localVariableTable);
		this->rollbackClassDebugData(currentThread, classnameLength, classnameData);
		pieces->lineNumberTable = NULL;
		pieces->localVariableTable = NULL;
	}

	if (pieces->romClass == NULL) {
		retval = false;
		Trc_SHR_CM_allocateROMClass_Exit3(currentThread, 0, classnameLength, classnameData, pieces->romClass, pieces->lineNumberTable, pieces->localVariableTable, pieces->flags);
	} else {
		retval = true;
		Trc_SHR_CM_allocateROMClass_Exit3(currentThread, 1, classnameLength, classnameData, pieces->romClass, pieces->lineNumberTable, pieces->localVariableTable, pieces->flags);
	}
	return retval;
}


/**
 * Allocate memory for a Orphan ROMClass or a ROMClass from the shared class.
 * Returns a pointer to the memory to be used. Note: The rom class is only added
 * to the cache when SH_CacheMap::commitROMClass() is called.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] sizeToAlloc size in bytes of the new ROMClass to allocate
 * @param [in] classnameLength length of the class name
 * @param [in] classnameData class name data
 * @param [in] cpw classpath wrapper
 * @param [in] partitionInCache partition info
 * @param [in] modContextInCache modification context info
 * @param [in] callerHelperID helper id
 * @param [in] modifiedNoContext true if class we are storing will have modified bytecode
 * @param [out] newItemInCache new item that we alloc'd from cache
 * @param [out] cacheAreaForAllocate where we alloc'd the new item from the cache
 *
 * @return Pointer to the stored ROM Class memory or NULL if there is no room left in the cache.
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
const J9ROMClass*
SH_CacheMap::allocateROMClassOnly(J9VMThread* currentThread, U_32 sizeToAlloc, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, IDATA callerHelperID, bool modifiedNoContext, void * &newItemInCache, void * &cacheAreaForAllocate)
{
	const J9ROMClass* result = NULL;
	bool fullFlagSet = J9_ARE_ANY_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_AVAILABLE_SPACE_FULL);
	bool useScope = !((partitionInCache == NULL) && (modContextInCache == NULL));
	U_32 wrapperSize = 0;
	U_16 wrapperType = 0;

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_allocateROMClassOnly_Entry((UDATA)currentThread, (UDATA)sizeToAlloc, (UDATA)classnameLength, classnameData,(UDATA)cpw, (UDATA)partitionInCache, (UDATA)modContextInCache, callerHelperID, (UDATA)modifiedNoContext);

	Trc_SHR_Assert_False(fullFlagSet);
	
	if ((cpw == NULL) || (modifiedNoContext == true)) {
		Trc_SHR_CM_allocateROMClassOnly_WillAllocOrphan_Event(currentThread, (UDATA)classnameLength, classnameData);
		result = allocateFromCache(currentThread, sizeToAlloc, sizeof(OrphanWrapper), TYPE_ORPHAN, newItemInCache, cacheAreaForAllocate);
	} else {
		SH_ClasspathManager* localCPM;
		if (!(localCPM = getClasspathManager(currentThread))) {
			Trc_SHR_CM_allocateROMClassOnly_CPMan_Event(currentThread, (UDATA)classnameLength, classnameData);
			goto _exit;
		}

		/* If we are storing using a Token and there is already a class of the same name stored under that token, mark the original one stale*/ 
		this->tokenStoreStaleCheckAndMark(currentThread, classnameLength, classnameData, cpw, partitionInCache, modContextInCache, callerHelperID);

		if (localCPM->isStale(cpw)) {
			Trc_SHR_CM_allocateROMClassOnly_CPMStale_Event(currentThread, (UDATA)classnameLength, classnameData);
			goto _exit;
		}

		if (!useScope) {
			Trc_SHR_CM_allocateROMClassOnly_WillROMClass_Event(currentThread, (UDATA)classnameLength, classnameData);
			wrapperSize = sizeof(ROMClassWrapper);
			wrapperType = TYPE_ROMCLASS;
		} else {
			Trc_SHR_CM_allocateROMClassOnly_WillScpopedROMClass_Event(currentThread, (UDATA)classnameLength, classnameData);
			wrapperSize = sizeof(ScopedROMClassWrapper);
			wrapperType = TYPE_SCOPED_ROMCLASS;
		}

		result = allocateFromCache(currentThread, sizeToAlloc, wrapperSize, wrapperType, newItemInCache, cacheAreaForAllocate);
	}

_exit:
	Trc_SHR_CM_allocateROMClassOnly_Retval_Event(currentThread, result, (UDATA)classnameLength, classnameData);
	Trc_SHR_CM_allocateROMClassOnly_Exit(currentThread);
	return result;
}

/**
 * If a new ROMClass is being stored with a token, then older ROMClasses 
 * with the same name stored against the same token should be marked stale.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] classnameLength length of the rom class name we are storing
 * @param [in] classnameData the class name data
 * @param [in] cpw classpath info for this class
 * @param [in] partitionInCache partition info for this class
 * @param [in] modContextInCache mod context info for this class
 * @param [in] callerHelperID id of the classloader
 *
 * @return void
 *
 * THREADING: Called during a write transaction
 */
void
SH_CacheMap::tokenStoreStaleCheckAndMark(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, UDATA callerHelperID)
{
	/* If we are storing using a Token and there is already a class of the same name stored under that token, mark the original one stale */
	if (((ClasspathItem*) CPWDATA(cpw))->getType() == CP_TYPE_TOKEN) {
		LocateROMClassResult existingTokenResult;
		UDATA tokenRC = 0;

		tokenRC = _rcm->locateROMClass(currentThread, classnameData, classnameLength, ((ClasspathItem*) CPWDATA(cpw)), 0, -1, callerHelperID, NULL, partitionInCache, modContextInCache, &existingTokenResult);

		if (tokenRC & LOCATE_ROMCLASS_RETURN_FOUND) {
			markItemStale(currentThread, existingTokenResult.knownItem, false);
		}
	}
	return;
}

/**
 * Allocates Orphan ROMClass memory from the cache.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] sizeToAlloc size in bytes of the new ROMClass to allocate
 * @param [in] wrapperSize size of wrapper data for this ROMClass
 * @param [in] wrapperType type of wrapper being used for this ROMClass
 * @param [out] newItemInCache new item that we alloc'd from cache
 * @param [out] cacheAreaForAllocate where we alloc'd the new item from the cache
 *
 * @return Pointer to the stored ROM Class memory or NULL if there is no room left in the cache.
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
const J9ROMClass*
SH_CacheMap::allocateFromCache(J9VMThread* currentThread, U_32 sizeToAlloc, U_32 wrapperSize, U_16 wrapperType, void * &newItemInCache, void * &cacheAreaForAllocate)
{
	ShcItem item;
	ShcItem* itemPtr = &item;
	ShcItem* itemInCache = NULL;
	J9ROMClass* result = NULL;
	BlockPtr romClassBuffer = NULL;
	U_32 sizeToAllocPadded = SHC_ROMCLASSPAD(sizeToAlloc);
	SH_CompositeCacheImpl* localCacheAreaForAllocate;

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_allocateFromCache_Entry((UDATA)currentThread, (UDATA)sizeToAlloc, (UDATA)wrapperSize, (UDATA)wrapperType);

	if (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE) {
		/* Don't update the cache. We should never reach here, if RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE is set, we should return directly from j9shr_classStoreTransaction_createSharedClass().*/
		Trc_SHR_Assert_ShouldNeverHappen();
		Trc_SHR_CM_allocateFromCache_Flags_Event(currentThread);
		goto done;
	}

	localCacheAreaForAllocate = getCacheAreaForDataType(currentThread, wrapperType, (sizeToAllocPadded + sizeof(ShcItem) + sizeof(ShcItemHdr) + wrapperSize + 16));
	if (!localCacheAreaForAllocate) {
		/* This may indicate size required is bigger than the cachelet size. */
		/* TODO: In offline mode, should be fatal. Report unable to grow. */
		Trc_SHR_CM_allocateFromCache_getCacheArea_Failed_Event(currentThread, (UDATA)sizeToAlloc, (UDATA)wrapperSize, (UDATA)wrapperType);
		goto done;
	}

	_ccHead->initBlockData(&itemPtr, wrapperSize, wrapperType);
	itemInCache = (ShcItem*) localCacheAreaForAllocate->allocateWithSegment(currentThread, itemPtr, sizeToAllocPadded, &romClassBuffer);

	if (itemInCache == NULL) {
		/* TODO: With offline caches, this should never happen */
		/* Not enough space in cache to accommodate this item. */
		Trc_SHR_CM_allocateFromCache_Full_Event(currentThread);
		goto done;
	}

	result = (J9ROMClass*) romClassBuffer;

done:
	if (result == NULL) {
		newItemInCache = NULL;
		cacheAreaForAllocate = NULL;
	} else {
		newItemInCache = (void*) itemInCache;
		cacheAreaForAllocate = (void*) localCacheAreaForAllocate;
	}
	Trc_SHR_CM_allocateFromCache_Retval_Event(currentThread, result);
	Trc_SHR_CM_allocateFromCache_Exit(currentThread);
	return result;
}

/**
 * Allocates debug attribute data for a ROMClass
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] classnameLength ROMClass class name length
 * @param [in] classnameData ROMClass class name
 * @param [in] sizes structure containing sizes of ROMClass parts
 * @param [out] pieces results of calling this function
 *
 * @return 0 if this function passes, otherwise this function has failed
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
IDATA
SH_CacheMap::allocateClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces)
{
	return this->_ccHead->allocateClassDebugData(currentThread, classnameLength, classnameData, sizes, pieces);
}

/**
 * Roll back uncommitted changes made by the last call too 'allocateClassDebugData()'
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] classnameLength ROMClass class name length
 * @param [in] classnameData ROMClass class name
 *
 * @return void
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
void 
SH_CacheMap::rollbackClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData)
{
	this->_ccHead->rollbackClassDebugData(currentThread, classnameLength, classnameData);
}

/**
 * Roll back commited changes made by the last call too 'allocateClassDebugData()'
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] classnameLength ROMClass class name length
 * @param [in] classnameData ROMClass class name
 *
 * @return void
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
void 
SH_CacheMap::commitClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData)
{
	this->_ccHead->commitClassDebugData(currentThread, classnameLength, classnameData);
}

void
SH_CacheMap::updateLineNumberContentInfo(J9VMThread* currentThread)
{
	J9JavaVM * vm = currentThread->javaVM;

	if (false == this->_cc->getIsNoLineNumberContentEnabled()) {
		if (0 == (vm->requiredDebugAttributes &
			(J9VM_DEBUG_ATTRIBUTE_LINE_NUMBER_TABLE | J9VM_DEBUG_ATTRIBUTE_SOURCE_FILE)))
		{
			this->_cc->setNoLineNumberContentEnabled(currentThread);
		}
	}

	if (false == this->_cc->getIsLineNumberContentEnabled()) {
		if ((J9VM_DEBUG_ATTRIBUTE_LINE_NUMBER_TABLE | J9VM_DEBUG_ATTRIBUTE_SOURCE_FILE) ==
				(vm->requiredDebugAttributes & (J9VM_DEBUG_ATTRIBUTE_LINE_NUMBER_TABLE | J9VM_DEBUG_ATTRIBUTE_SOURCE_FILE)))
		{
			this->_cc->setLineNumberContentEnabled(currentThread);
		}
	}

}


/**
 * This function commits allocated ROMClass to the cache.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] itemInCache the newly alloc'd cache item
 * @param [in] cacheAreaForAllocate area the new cache item was allocated from
 * @param [in] cpw classpath wrapper obj
 * @param [in] cpeIndex classpath index where this object was found
 * @param [in] partitionInCache partition information
 * @param [in] modContextInCache modification context (if byte codes are modified)
 * @param [in] romClassBuffer a pointer to the J9ROMClass
 *
 * @return 0 if nothing was stored in the cache, 1 if we stores a class or meta data.
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
IDATA
SH_CacheMap::commitROMClass(J9VMThread* currentThread, ShcItem* itemInCache, SH_CompositeCacheImpl* cacheAreaForAllocate, ClasspathWrapper* cpw, I_16 cpeIndex, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, BlockPtr romClassBuffer, bool commitOutOfLineData, bool checkSRPs)
{
	IDATA retval = 0;
	bool storeResult = false;
	J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(((J9ROMClass *) romClassBuffer));
	UDATA classNameHash = 0;
	bool useScope = !((partitionInCache == NULL) && (modContextInCache == NULL));
	ScopedROMClassWrapper srcw;
	ScopedROMClassWrapper* srcwInCache = NULL;
	ClasspathEntryItem* cpeiInCache = NULL;
	U_32 wrapperSize = 0;
	bool useWriteHash = _ccHead->isUsingWriteHash();

	Trc_SHR_Assert_True(romClassBuffer != NULL);
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_commitROMClass_Entry((UDATA)currentThread, (UDATA)itemInCache, (UDATA)cacheAreaForAllocate, (UDATA)cpw, (UDATA)cpeIndex, (UDATA)partitionInCache, (UDATA)modContextInCache, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName));
	if (checkSRPs) {
		checkROMClassUTF8SRPs((J9ROMClass *)romClassBuffer);
	}

	if (true == commitOutOfLineData) {
		/* If called from commitMetaDataROMClassIfRequired then commitDebugData is false
		 * b/c only ROMClass meta data is being updated.
		 */
		this->commitClassDebugData(currentThread, J9UTF8_LENGTH(romClassName), (const char*)J9UTF8_DATA(romClassName));
	}
	
	if (!useScope) {
		wrapperSize = sizeof(ROMClassWrapper);
	} else {
		wrapperSize = sizeof(ScopedROMClassWrapper);
	}

	cpeiInCache = ((ClasspathItem*) CPWDATA(cpw))->itemAt(cpeIndex);
	srcw.cpeIndex = cpeIndex;
	srcw.timestamp = 0;
	if (cpeiInCache->protocol == PROTO_DIR) {
		srcw.timestamp = _tsm->checkROMClassTimeStamp(currentThread, (const char*) J9UTF8_DATA(romClassName), (UDATA) J9UTF8_LENGTH(romClassName), cpeiInCache, ((ROMClassWrapper*) &srcw));
	}

	srcwInCache = (ScopedROMClassWrapper*) ITEMDATA(itemInCache); /* must be after name is written */

	getJ9ShrOffsetFromAddress(cpw, false, &srcw.theCpOffset);
	/* Calculate the J9ShrOffset */
	getJ9ShrOffsetFromAddress(romClassBuffer, true, &srcw.romClassOffset);
	if (useScope) {
		const J9UTF8* mcToUse = modContextInCache;
		const J9UTF8* ptToUse = partitionInCache;

		if (NULL == modContextInCache) {
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
			srcw.modContextOffset.cacheLayer = 0;
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
			srcw.modContextOffset.offset = 0;
		} else {
			getJ9ShrOffsetFromAddress(mcToUse, false, &srcw.modContextOffset);
		}
		if (NULL == partitionInCache) {
			J9ShrOffset offset = {0};
			srcw.partitionOffset = offset;
		} else {
			getJ9ShrOffsetFromAddress(ptToUse, false, &srcw.partitionOffset);
		}
	}
	memcpy(srcwInCache, &srcw, wrapperSize);

	if ((true == useWriteHash) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
		if (_writeHashMaxWaitMicros == 0 && _writeHashContendedResetHash != 0) {
			classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
			if (classNameHash == _writeHashContendedResetHash) {
				/* When storage contention is enabled, and the max wait is zero, restore the saved max time
				 * if the hash was contended.
				 */
				_writeHashMaxWaitMicros = _writeHashSavedMaxWaitMicros;
				Trc_SHR_CM_commitROMClass_Event_SetMaxTime(currentThread, "store", _writeHashMaxWaitMicros, _writeHashAverageTimeMicros, 0);
			}
		}
	}

	storeResult = _rcm->storeNew(currentThread, itemInCache, cacheAreaForAllocate);

	/* If storeNew() fails to store a class in the RCM we return 0, to be consistent with old code when running verboseio.
	 */
	if (storeResult == false) {
		Trc_SHR_CM_commitROMClass_StoreFail_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), (const char*) J9UTF8_DATA(romClassName), (UDATA)itemInCache, (UDATA)cacheAreaForAllocate);
		retval = 0;
	} else {
		retval = 1;
		updateLineNumberContentInfo(currentThread);
	}
	/*If storeNew() fails, we still need to update the segment list, and commit the update.*/
	cacheAreaForAllocate->commitUpdate(currentThread, false);
	updateROMSegmentList(currentThread, true);

	/* Try to reset the writeHash field in the cache. We have loaded a class from disk have now stored it.
	 CMVC 93940 z/OS PERFORMANCE: Only reset the writeHash field on non-orphan store */
	if ((true == useWriteHash) && (cpw != NULL) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
		if (classNameHash == 0) {
			classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
		}
		_ccHead->tryResetWriteHash(currentThread, classNameHash); /* don't care about the return value */
	}

	if (retval == 0) {
		Trc_SHR_CM_commitROMClass_NothingStored_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
	} else if (retval == 1) {
		Trc_SHR_CM_commitROMClass_Stored_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
	} else {
		Trc_SHR_CM_commitROMClass_Error_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
		Trc_SHR_Assert_ShouldNeverHappen();
	}
	Trc_SHR_CM_commitROMClass_Exit(currentThread);
	return retval;
}


/**
 * This function commits allocated Orphan-ROMClass to the cache.
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] itemInCache the newly alloc'd cache item
 * @param [in] cacheAreaForAllocate area the new cache item was allocated from
 * @param [in] romClassBuffer a pointer to the J9ROMClass
 *
 * @return 0 if nothing was stored in the cache, 1 if we stores a class or meta data.
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
IDATA
SH_CacheMap::commitOrphanROMClass(J9VMThread* currentThread, ShcItem* itemInCache, SH_CompositeCacheImpl* cacheAreaForAllocate, ClasspathWrapper* cpw, BlockPtr romClassBuffer)
{
	IDATA retval = 0;
	bool storeResult = false;
	J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(((J9ROMClass *) romClassBuffer));
	UDATA classNameHash = 0;
	OrphanWrapper ow;
	OrphanWrapper* owInCache = NULL;
	bool useWriteHash = _ccHead->isUsingWriteHash();

	Trc_SHR_Assert_True(romClassBuffer != NULL);
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_commitOrphanROMClass_Entry((UDATA)currentThread, (UDATA)itemInCache, (UDATA)cacheAreaForAllocate, (UDATA)cpw, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName));
	checkROMClassUTF8SRPs((J9ROMClass *)romClassBuffer);

	/*If there was class debug allocated we need to commit it before the ROMClass*/
	this->commitClassDebugData(currentThread, J9UTF8_LENGTH(romClassName), (const char*)J9UTF8_DATA(romClassName));

	owInCache = (OrphanWrapper*) ITEMDATA(itemInCache);
	getJ9ShrOffsetFromAddress(romClassBuffer, true, &ow.romClassOffset);
	memcpy(owInCache, &ow, sizeof(OrphanWrapper));

	if ((true == useWriteHash) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
		if (_writeHashMaxWaitMicros == 0 && _writeHashContendedResetHash != 0) {
			classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
			if (classNameHash == _writeHashContendedResetHash) {
				/* When storage contention is enabled, and the max wait is zero, restore the saved max time
				 * if the hash was contended.
				 */
				_writeHashMaxWaitMicros = _writeHashSavedMaxWaitMicros;
				Trc_SHR_CM_commitOrphanROMClass_Event_SetMaxTime(currentThread, "store", _writeHashMaxWaitMicros, _writeHashAverageTimeMicros, 0);
			}
		}
	}


	storeResult = _rcm->storeNew(currentThread, itemInCache, cacheAreaForAllocate);

	/* If storeNew() fails to store a class in the RCM we return 0, to be consistent with old code when running verboseio.*/
	if (storeResult == false) {
		Trc_SHR_CM_commitOrphanROMClass_StoreFail_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), (const char*) J9UTF8_DATA(romClassName),  (UDATA)itemInCache, (UDATA)cacheAreaForAllocate);
		retval = 0;
	} else {
		retval = 1;
		updateLineNumberContentInfo(currentThread);
	}
	/*If storeNew() fails, we still need to update the segment list, and commit the update.*/
	cacheAreaForAllocate->commitUpdate(currentThread, false);
	updateROMSegmentList(currentThread, true);

	/* Try to reset the writeHash field in the cache. We have loaded a class from disk have now stored it.
	 CMVC 93940 z/OS PERFORMANCE: Only reset the writeHash field on non-orphan store */
	if ((true == useWriteHash) && (cpw != NULL) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
		if (classNameHash == 0) {
			classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
		}
		_ccHead->tryResetWriteHash(currentThread, classNameHash); /* don't care about the return value */
	}

	if (retval == 0) {
		Trc_SHR_CM_commitOrphanROMClass_NothingStored_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
	} else if (retval == 1) {
		Trc_SHR_CM_commitOrphanROMClass_Stored_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
	} else {
		Trc_SHR_CM_commitOrphanROMClass_Error_Event(currentThread, (UDATA) J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romClassBuffer);
		Trc_SHR_Assert_ShouldNeverHappen();
	}
	Trc_SHR_CM_commitOrphanROMClass_Exit(currentThread);
	return retval;
}

/**
 * This function may create and commit new meta data for an existing class to the cache
 *
 * @param [in] currentThread the thread calling this function
 * @param [in] cpw classpath wrapper obj
 * @param [in] cpeIndex classpath index where this object was found
 * @param [in] partitionInCache partition information
 * @param [in] modContextInCache modification context (if byte codes are modified)
 * @param [in] findNextIterator the last class looked at during a shared classes transaction
 *
 * @return 0 if nothing was stored in the cache, 1 if we stores a class or meta data, -1 if an error occured.
 *
 * THREADING: We assume the Segment Mutex, String Table Lock, and Write Area Lock is held by the transaction.
 */
IDATA
SH_CacheMap::commitMetaDataROMClassIfRequired(J9VMThread* currentThread, ClasspathWrapper* cpw, I_16 cpeIndex, IDATA helperID, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, J9ROMClass * romclass)
{
	IDATA retval = 0;
	J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(romclass);
	UDATA classNameHash = 0;
	bool useScope = !((partitionInCache == NULL) && (modContextInCache == NULL));
	U_32 wrapperSize = 0;
	U_16 wrapperType = 0;
	BlockPtr romClassBuffer = NULL;
	UDATA rc = 0;
	LocateROMClassResult locateResult;
	J9ROMClass * locateJ9ROMClass = NULL;
	ShcItem item;
	ShcItem* itemPtr = &item;
	SH_CompositeCacheImpl* cacheAreaForAllocate = NULL;
	ShcItem* itemInCache = NULL;
	UDATA bytesToReserve = 0;
	bool useWriteHash = _ccHead->isUsingWriteHash();

	Trc_SHR_Assert_True(romclass != NULL);
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_commitMetaDataROMClassIfRequired_Entry((UDATA)currentThread, (UDATA)cpw, (UDATA)cpeIndex, helperID, (UDATA)partitionInCache, (UDATA)modContextInCache, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName));

	romClassBuffer = (BlockPtr) romclass;

	if (!useScope) {
		wrapperSize = sizeof(ROMClassWrapper);
		wrapperType = TYPE_ROMCLASS;
	} else {
		wrapperSize = sizeof(ScopedROMClassWrapper);
		wrapperType = TYPE_SCOPED_ROMCLASS;
	}

	/* ROMClassBuilder is using an existing class so we check if the class has the correct meta data as well.
	 * e.g was it be another VM, between calls to hookFindSharedClass and j9shr_startSCStoreTransaction(),
	 * when a lock on the cache is not held.
	 */

	/* Although we may have the ROMClass we want to use, we may already also have the metadata we want (another JVM has added the class), so check for this */
	rc = _rcm->locateROMClass(currentThread, (const char*) J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName), ((ClasspathItem*) CPWDATA(cpw)), cpeIndex, -1, helperID, (J9ROMClass *) romClassBuffer, partitionInCache, modContextInCache,
			&locateResult);

	/* If we are storing using a Token and there is already a class of the same name stored under that token, mark the original one stale 
	 * Note also that the locateROMClass call above will not necessarily find what we want. If another class is being added under the same token,
	 * and the orphan has already been added, the above call will return the exact orphan match which is useful, but not what we need to do the stale mark 
	 */
	this->tokenStoreStaleCheckAndMark(currentThread, J9UTF8_LENGTH(romClassName), (const char*)J9UTF8_DATA(romClassName), cpw, partitionInCache, modContextInCache, helperID);

	/* If locateROMClass found a stale ClasspathEntryItem */
	if (rc & LOCATE_ROMCLASS_RETURN_DO_MARK_CPEI_STALE) {
		markStale(currentThread, locateResult.staleCPEI, true);
	}

	if (rc & LOCATE_ROMCLASS_RETURN_FOUND) {
		/* We only get here if another JVM has stored the same item underneath us.
		 * This race is resolved by checking that what we have in the cache is the
		 * same timestamp as on the filesystem (done in locateROMClass).
		 * If so, return the item already stored. */
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_FoundDuplicate(currentThread, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName));
		locateJ9ROMClass = (J9ROMClass*) getAddressFromJ9ShrOffset(&((locateResult.known)->romClassOffset));

		/* Try to reset the writeHash field in the cache. We have loaded a class from disk have now stored it.
		 CMVC 93940 z/OS PERFORMANCE: Only reset the writeHash field on non-orphan store */
		if ((true == useWriteHash) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
			/* If the max time is not zero, this must be undetected storage contention,
			 * continue with the current maximum.
			 */
			if (_writeHashMaxWaitMicros == 0) {
				/* There is storage contention, restore or bump up the maximum wait time.
				 * Distinguish two cases here
				 * 1) We didn't wait long enough and need to wait longer
				 * 2) There was a class not found which reduced the wait time, so restore the original wait time
				 */
				UDATA actualTimeMicros = 0;
				classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
				_writeHashMaxWaitMicros = _writeHashSavedMaxWaitMicros;
				if (classNameHash == _writeHashContendedResetHash) {
					PORT_ACCESS_FROM_VMC(currentThread);
					_writeHashContendedResetHash = 0;
					actualTimeMicros = (UDATA)(j9time_usec_clock() - _writeHashStartTime);
					updateAverageWriteHashTime(actualTimeMicros);
				} else {
					UDATA twice;
					if ((_writeHashAverageTimeMicros > 0) && (_writeHashMaxWaitMicros > (twice = _writeHashAverageTimeMicros * 2))) {
						/* If the maximum time is more than twice the average, reduce it */
						_writeHashMaxWaitMicros = twice;
					}
				}
				Trc_SHR_CM_commitMetaDataROMClassIfRequired_Event_SetMaxTime(currentThread, "found", _writeHashMaxWaitMicros, _writeHashAverageTimeMicros, actualTimeMicros);
			}
		}
	} else {
		/* Classpath could potentially have become stale during locateROMClass above. This is highly unlikely, but possible. We cannot add against a stale classpath,
		 * and we have missed our chance to add a new classpath, so in this case, totally fail the add. */
		SH_ClasspathManager* localCPM;
		if (!(localCPM = getClasspathManager(currentThread))) {
			Trc_SHR_CM_commitMetaDataROMClassIfRequired_CPMan_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName));
			retval = -1;
			goto done;
		}
		if (localCPM->isStale(cpw)) {
			Trc_SHR_CM_commitMetaDataROMClassIfRequired_CPMStale_Event(currentThread);
			goto done;
		}
	}

	/* If result is set, this means that we should not store, as we already have the result */
	if (locateJ9ROMClass != NULL) {
		/*Our class and meta data was already added so we can exit*/;
		updateBytesRead(locateJ9ROMClass->romSize);
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Existing_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romclass);
		goto done;
	}

	/* If we get here it means we are using an existing class but the meta data does not match what we need.
	 * In this case we need to create new metadata only.
	 */

	/* If the cache is marked full, we cannot add any new metadata as the last page would have been mprotected */
	if (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE) {
		/* Don't update the cache */
		increaseUnstoredBytes(wrapperSize);
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Flags_Event(currentThread);
		retval = -1;
		goto done;
	}

	/*Ensure enough supercache space*/
	bytesToReserve = sizeof(ShcItem) + sizeof(ShcItemHdr) + wrapperSize + 16;

	/* This forces allocation of a supercache, if necessary.
	 * THIS IS HACKISH. Currently, all cachelets are shared between all managers,
	 * so data type is irrelevant when attempting to reserve space.
	 */
	cacheAreaForAllocate = getCacheAreaForDataType(currentThread, wrapperType, bytesToReserve);

	if (cacheAreaForAllocate == NULL) {
		/* This may indicate size required is bigger than the cachelet size.
		 * In this case we simply indicate that we are storing nothing, and return 0.
		 */
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Area_Event(currentThread, (UDATA)wrapperType, (UDATA)bytesToReserve);
		retval = 0;
		goto done;
	}

	/*Allocate the meta data*/
	_ccHead->initBlockData(&itemPtr, wrapperSize, wrapperType);
	itemInCache = (ShcItem*) cacheAreaForAllocate->allocateBlock(currentThread, itemPtr, SHC_WORDALIGN, wrapperSize);

	if (itemInCache == NULL) {
		/* Not enough space in cache to accommodate this item. */
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Full_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romclass);
		retval = -1;
		goto done;
	}

	/* Update the min/max boundary with the stored metadata entry only when the cache is
	 * being created by the current VM.
	 */
	if (_ccHead->isNewCache() 
		&& (false == _metadataReleased)
	) {
#if !defined(J9ZOS390) && !defined(AIXPPC)
#if defined(LINUX)
		if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE))
#endif
		{
			updateAccessedShrCacheMetadataBounds(currentThread, (uintptr_t *) ITEMDATA(itemInCache));
		}
#endif /* !defined(J9ZOS390) && !defined(AIXPPC) */
	}

	/* SRPs has already been checked when committing the Orphan ROMClass, pass false to param checkSRPs */
	retval = commitROMClass(currentThread, itemInCache, cacheAreaForAllocate, cpw, cpeIndex, partitionInCache, modContextInCache, romClassBuffer, false, false);
	goto done_skipHashUpdate;

done:
	/* Try to reset the writeHash field in the cache. We have loaded a class from disk have now stored it.
	 CMVC 93940 z/OS PERFORMANCE: Only reset the writeHash field on non-orphan store */
	if ((true == useWriteHash) && (cpw != NULL) && (*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION)) {
		if (classNameHash == 0) {
			classNameHash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8(J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName));
		}
		_ccHead->tryResetWriteHash(currentThread, classNameHash); /* don't care about the return value */
	}

done_skipHashUpdate:
	if (retval == 0) {
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_NothingStored_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romclass);
	} else if (retval == 1) {
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Stored_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romclass);
	} else {
		Trc_SHR_CM_commitMetaDataROMClassIfRequired_Error_Event(currentThread, (UDATA)J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), (UDATA)romclass);
	}
	Trc_SHR_CM_commitMetaDataROMClassIfRequired_Exit(currentThread);
	return retval;
}



/**
 * Locate a shared ROM class in the cache
 *
 * @param [in] tobj transaction object 
 *
 * @return If the class is found, pointer to the J9ROMClass structure for the class, NULL otherwise
 * 
 * THREADING: This function can be called multi-threaded. Caller must have VM class segment mutex.
 */
const J9ROMClass *
SH_CacheMap::findNextROMClass(J9VMThread* currentThread, void * &findNextIterator, void * &firstFound, U_16 classnameLength, const char* classnameData)
{

	J9ROMClass* retval = NULL;
	Trc_SHR_Assert_ShouldHaveLocalMutex(currentThread->javaVM->classMemorySegments->segmentMutex);
	Trc_SHR_CM_findNextROMClass_Entry(currentThread);
	
	retval = (J9ROMClass*)_rcm->findNextExisting(currentThread, findNextIterator, firstFound, classnameLength, classnameData);

	Trc_SHR_CM_findNextROMClass_Retval_Event(currentThread, retval);
	Trc_SHR_CM_findNextROMClass_Exit(currentThread);
	return retval;
}

/**
 * Size of cache string table area
 *
 * @return size of the string table
 *
 * Caller should hold string table write lock
 */
UDATA
SH_CacheMap::getStringTableBytes(void)
{
	return this->_ccHead->getStringTableBytes();
}

/**
 *	Address of the start of the shared string table data.
 *
 * @return Address of start of shared readWrite area

 * Caller should hold string table write lock
 */
void*
SH_CacheMap::getStringTableBase(void)
{
	return this->_ccHead->getStringTableBase();
}

/**
 * Locate a shared ROM class in the cache
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] path Pointer to the class name to be located (null terminated string)
 * @param [in] cp ClasspathItem object representing the classpath of the caller classloader
 * @param [in] partition Partition string to use (optional)
 * @param [in] modContext Modification context string to use (optional)
 * @param [out] foundAtIndex Set to the index in the classpath from which the class would have been loaded from disk
 *
 * @return If the class is found, pointer to the J9ROMClass structure for the class, NULL otherwise
 * 
 * THREADING: This function can be called multi-threaded.
 */
const J9ROMClass*
SH_CacheMap::findROMClass(J9VMThread* currentThread, const char* path, ClasspathItem* cp, const J9UTF8* partition, const J9UTF8* modContext, IDATA confirmedEntries, IDATA* foundAtIndex)
{
	UDATA rc = 0;
	J9ROMClass* returnVal = NULL;
	U_16 pathLen = (U_16)strlen(path);
	const char* fnName = "findROMClass";
	LocateROMClassResult locateResult;
	SH_ROMClassManager* localRCM;
	UDATA hash = 0;
	bool useWriteHash = _ccHead->isUsingWriteHash();

	Trc_SHR_CM_findROMClass_Entry(currentThread, path, cp->getHelperID());
	
	if (!(localRCM = getROMClassManager(currentThread))) {
		/* trace exception is at level 1 and trace exit message is at level 2 as per CMVC 155318/157683 */
		Trc_SHR_CM_findROMClass_NoROMClassManager_Exception(currentThread, path, cp->getHelperID());
		Trc_SHR_CM_findROMClass_Exit_Null(currentThread);
		return NULL;
	}

	if (_ccHead->isRunningReadOnly()) {
		/* We've already identified that an element of this classpath is stale. Sharing is therefore disabled for this classpath. */
		if (cp->flags & MARKED_STALE_FLAG) {
			 /* no level 1 trace event here since this could cause performance problem stated in CMVC 155318/157683 */
			Trc_SHR_CM_findROMClass_ExitStaleClasspath(currentThread, path);
			return NULL;
		}
	}

	/* THREADING: We enter read mutex here. Multiple readers can read concurrently.
	 * Readers can also read at the same time as a writer is writing. The mutex therefore serves
	 * to indicate to the writers when readers have finished reading, incase they want a lock.
	 */
	if (_ccHead->enterReadMutex(currentThread, fnName) != 0) {
		Trc_SHR_CM_findROMClass_FailedMutex(currentThread, path, cp->getHelperID());
		Trc_SHR_CM_findROMClass_Exit_Null(currentThread);
		return NULL;
	}

	if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
		_ccHead->exitReadMutex(currentThread, fnName);
		/* trace event is at level 1 and trace exit message is at level 2 as per CMVC 155318/157683 */
		Trc_SHR_CM_findROMClass_Exit_Null_Event(currentThread, path, cp->getHelperID());
		Trc_SHR_CM_findROMClass_Exit_Null(currentThread);
		return NULL;
	}

	rc = localRCM->locateROMClass(currentThread, path, pathLen, cp, -1, confirmedEntries, cp->getHelperID(), NULL, partition, modContext, &locateResult);
	if ((rc & LOCATE_ROMCLASS_RETURN_MARKED_ITEM_STALE) != LOCATE_ROMCLASS_RETURN_MARKED_ITEM_STALE) {
		_ccHead->exitReadMutex(currentThread, fnName);
	}

	/* If ROMClass was not found (non-error case), look to see if another JVM is trying to load the same class.
		If it is, wait until the next update, which is more than likely to be the class we want. This removes the
		overhead of going to disk for the class, trying to store it and then throwing it away because it is already stored. 
	*/
	if ((*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION) && (rc & LOCATE_ROMCLASS_RETURN_DO_TRY_WAIT)) {
		if (true == useWriteHash) {
			PORT_ACCESS_FROM_VMC(currentThread);
			hash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8((U_8*)path, (U_16)pathLen);
			UDATA result = _ccHead->testAndSetWriteHash(currentThread, hash);
			if (result==1) {			/* Another JVM in the middle of loading class with same hash - wait for update */
				UDATA cntr = 0;
				U_64 startTime, endTime;
				BOOLEAN retry;
				UDATA actualTimeMicros = 0;
				UDATA waitMillis = 0;

				if (_writeHashMaxWaitMicros == 0) {
					endTime = startTime = 0;
				} else {
					if (_writeHashAverageTimeMicros != 0) {
						/* Start by waiting half of the average time, rounded up. The time
						 * will be adjusted later if there are too many waits.
						 */
						waitMillis = (_writeHashAverageTimeMicros / 2 / 1000) + 1;
					} else {
						waitMillis = DEFAULT_WRITE_HASH_WAIT_MILLIS;
					}
					endTime = startTime = j9time_usec_clock();
				}

				do {
					BOOLEAN updates;

					retry = FALSE;
	
					if (_writeHashMaxWaitMicros != 0) {
						/* Sleep until an update has appeared or until its not worth checking */
						while (!_ccHead->checkUpdates(currentThread)) {
							/* Check update for _ccHead only here because
							 * 1. Won't reach here if running in readOnly mode
							 * 2. Readonly caches are not supposed to be updated
							 */
							endTime = j9time_usec_clock();
							actualTimeMicros = (UDATA)(endTime - startTime);
							if (actualTimeMicros >= _writeHashMaxWaitMicros) {
								break;
							}
							cntr++;
							if (cntr == 3) {
								/* After waiting twice, which will be the average time, increase the wait time to
								 * half the remaining time until the max time is reached (rounded up).
								 */
								waitMillis = ((_writeHashMaxWaitMicros - actualTimeMicros) / 2 / 1000) + 1;
							}
							Trc_SHR_CM_findROMClass_Event_WaitingWriteHash_WithTime(currentThread, cntr, waitMillis);
							omrthread_sleep(waitMillis);
						}
					}
					
					if ((updates = _ccHead->checkUpdates(currentThread))) {
						/* Check update for _ccHead only here because
						 * 1. Won't reach here if running in readOnly mode
						 * 2. Readonly caches are not supposed to be updated
						 */
						if (_ccHead->enterReadMutex(currentThread, fnName) != 0) {
							Trc_SHR_CM_findROMClass_FailedMutex(currentThread, path, cp->getHelperID());
							break;
						}
						IDATA rv = refreshHashtables(currentThread, (0 != omrthread_monitor_owned_by_self(currentThread->javaVM->classMemorySegments->segmentMutex)));
						if (-1 == rv) {
							_ccHead->exitReadMutex(currentThread, fnName);
							break;
						}
						rc = localRCM->locateROMClass(currentThread, path, pathLen, cp, -1, confirmedEntries, cp->getHelperID(), NULL, partition, modContext, &locateResult);
						/* If locateROMClass() returns LOCATE_ROMCLASS_RETURN_MARKED_ITEM_STALE,
						 * read mutex should have been released by SH_CacheMap::markItemStaleCheckMutex().
						 */
						if ((rc & LOCATE_ROMCLASS_RETURN_MARKED_ITEM_STALE) != LOCATE_ROMCLASS_RETURN_MARKED_ITEM_STALE) {
							_ccHead->exitReadMutex(currentThread, fnName);
						}
						endTime = j9time_usec_clock();
						actualTimeMicros = (UDATA)(endTime - startTime);
						if (rc & LOCATE_ROMCLASS_RETURN_DO_TRY_WAIT) {
							/* continue checking until the max time as long as there are updates */
							if (actualTimeMicros < _writeHashMaxWaitMicros) {
								retry = TRUE;
								continue;
							}
							updates = FALSE;
						} else {
							/* Not getting LOCATE_ROMCLASS_RETURN_DO_TRY_WAIT is counted as success, even
							 * if the rom class isn't found. Usually only bootstrap classes are found because
							 * the bootstrap classpath is checked first.
							 */
							if (cntr > 0) {
								updateAverageWriteHashTime(actualTimeMicros);
							}
						}
					}
					if (!updates) {
						/* Waited for the maximum time, but there were no updates for this class. Could
						 * be looking for a class that doesn't exist or will never be stored in the cache.
						 * Stop waiting. If we detect storage contention the max wait will be restored.
						 */
						if (_writeHashMaxWaitMicros != 0) {
							/* Set the contended hash. If a contended store matches the hash, it will
							 * increase the wait time.
							 */
							_writeHashStartTime = startTime;
							_writeHashSavedMaxWaitMicros = _writeHashMaxWaitMicros;
							_writeHashContendedResetHash = hash;
							_writeHashMaxWaitMicros = 0;
						}
					}
				} while (retry);

				Trc_SHR_CM_findROMClass_Event_AfterWriteHash_WithStats(currentThread, locateResult.known, locateResult.foundAtIndex, rc, _writeHashMaxWaitMicros, _writeHashAverageTimeMicros, actualTimeMicros);
			}
		} else {
			if (enterRefreshMutex(currentThread, "findROMClass")==0) {
				useWriteHash = _ccHead->peekForWriteHash(currentThread);
				Trc_SHR_CM_findROMClass_Event_PeekForWriteHash(currentThread, useWriteHash);
				exitRefreshMutex(currentThread, "findROMClass");
			}
		}
	}

	/* If ClasspathEntryItem is stale, become a writer, lock the cache and do stale mark */
	if (rc & LOCATE_ROMCLASS_RETURN_DO_MARK_CPEI_STALE) {
		markStale(currentThread, locateResult.staleCPEI, false);
	}

	if (rc & LOCATE_ROMCLASS_RETURN_FOUND) {
		/* It is quite possible for another JVM to set the writeHash field, load a class and set it back to zero
		 * while we are pootling around in locateROMClass above. If so, we will incorrectly set the writeHash field,
		 * unaware that the class has already been loaded. This clause ensures that the writeHash field will
		 * *definitely* be reset in all circumstances */
		if ((*_runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION) && (true == useWriteHash)) {
			if (hash == 0) {
				hash = currentThread->javaVM->internalVMFunctions->computeHashForUTF8((U_8*)path, (U_16)pathLen);
			}

			_ccHead->tryResetWriteHash(currentThread, hash);
		}
		if (foundAtIndex) {
			*foundAtIndex = locateResult.foundAtIndex;
		}
		returnVal = (J9ROMClass*)getAddressFromJ9ShrOffset(&((locateResult.known)->romClassOffset));
#if !defined(J9ZOS390) && !defined(AIXPPC)
		if (_metadataReleased
#if defined(LINUX)
				&& J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE)
#endif
		) {
			if (TrcEnabled_Trc_SHR_CM_findROMClass_metadataAccess
				&& (isAddressInReleasedMetaDataBounds(currentThread, (UDATA)locateResult.known))
			) {
				Trc_SHR_CM_findROMClass_metadataAccess(currentThread, path, (U_8 *) locateResult.known);
			}
		} else
#if defined(LINUX)
			if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE))
#endif
		{
			updateAccessedShrCacheMetadataBounds(currentThread, (uintptr_t *) locateResult.known);
		}
#endif /* !defined(J9ZOS390) && !defined(AIXPPC) */
	}

	if (returnVal) {
		/* Call updateROMSegmentList() to ensure that heapAlloc of the romClass segment is always updated to include the returned romClass */
		updateROMSegmentList(currentThread, omrthread_monitor_owned_by_self(currentThread->javaVM->classMemorySegments->segmentMutex) != 0);
		updateBytesRead(returnVal->romSize);		/* This is kind of inaccurate as the strings are all external to the ROMClass */
		/* trace event is at level 1 and trace exit message is at level 2 as per CMVC 155318/157683 */
		Trc_SHR_CM_findROMClass_Exit_Found_Event(currentThread, path, returnVal, locateResult.foundAtIndex, cp->getHelperID());
		Trc_SHR_CM_findROMClass_Exit_Found(currentThread, path, returnVal, locateResult.foundAtIndex);
	} else {
		/* no level 1 trace event here due to performance problem stated in CMVC 155318/157683 */
		Trc_SHR_CM_findROMClass_Exit_NotFound(currentThread, path);
	}
	return returnVal;
}

/* THREADING: MUST be protected by cache write mutex - therefore single-threaded within this JVM */
const void*
SH_CacheMap::addROMClassResourceToCache(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, const char** p_subcstr)
{
	/* Calculate the amounts of space needed by the new records */
	U_32 wrapperLength = resourceDescriptor->getWrapperLength();
	U_32 align = resourceDescriptor->getAlign();
	U_32 dataLength = resourceDescriptor->getResourceLength();
	U_32 totalLength = dataLength + wrapperLength;
	U_16 resourceType = resourceDescriptor->getResourceType();
	U_16 resourceSubType = resourceDescriptor->getResourceDataSubType();
	void* resultWrapper = NULL;
	ShcItem item;
	ShcItem* itemPtr = &item;
	ShcItem* itemInCache = NULL;
	SH_CompositeCacheImpl* cacheAreaForAllocate;

	PORT_ACCESS_FROM_VMC(currentThread);
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));

	switch (resourceType) {
	case TYPE_COMPILED_METHOD:
		if (0 != (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_AOT_DATA_UPDATE)) {
			return NULL;
		}
		break;
	case TYPE_ATTACHED_DATA:
		if (0 != (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_JIT_DATA_UPDATE)) {
			return NULL;
		}
		break;
	default :
		if (0 != (*_runtimeFlags & RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE)) {
			increaseUnstoredBytes(totalLength);
			return NULL;
		}
		break;
	}

	Trc_SHR_CM_addROMClassResourceToCache_Entry(currentThread, romAddress, resourceDescriptor);

	_ccHead->initBlockData(&itemPtr, totalLength, resourceType);

	cacheAreaForAllocate = getCacheAreaForDataType(currentThread, resourceType, _ccHead->getBytesRequiredForItemWithAlign(itemPtr, align, wrapperLength));
	if (!cacheAreaForAllocate) {
		/* This may indicate size required is bigger than the cachelet size. */
		/* TODO: In offline mode, should be fatal */
		if (NULL != p_subcstr) {
			const char* tmpstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),J9NLS_SHRC_CM_CANNOT_ALLOC_DATA_SIZE,"no space in cache for %d bytes");
			j9str_printf(PORTLIB, (char *)*p_subcstr, VERBOSE_BUFFER_SIZE, tmpstr, dataLength);
		}
		return (void*)J9SHR_RESOURCE_STORE_ERROR;
	}
	
	if (!isAddressInCache(romAddress, 0, false, false)) {
		/* TODO: Tracepoint */
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),J9NLS_SHRC_CM_ADDRESS_NOT_IN_CACHE, "address is not in cache");
		}
		return (void*)J9SHR_RESOURCE_STORE_ERROR;
	}

	switch (resourceType) {
	case TYPE_COMPILED_METHOD :
		itemInCache = (ShcItem*)(cacheAreaForAllocate->allocateAOT(currentThread, itemPtr, dataLength));
		break;
	case TYPE_ATTACHED_DATA :
		if ((J9SHR_ATTACHED_DATA_TYPE_JITPROFILE == resourceSubType) || 
			(J9SHR_ATTACHED_DATA_TYPE_JITHINT == resourceSubType)
		){
			itemInCache = (ShcItem*)(cacheAreaForAllocate->allocateJIT(currentThread, itemPtr, dataLength));
		}
		break;
	default :
		itemInCache = (ShcItem*)(cacheAreaForAllocate->allocateBlock(currentThread, itemPtr, align, wrapperLength));
		break;
	}

	if (itemInCache == NULL) {
		if (NULL != p_subcstr) {
			const char* tmpstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),J9NLS_SHRC_CM_CANNOT_ALLOC_DATA_SIZE, "no space in cache for %d bytes");
			j9str_printf(PORTLIB, (char *)*p_subcstr, VERBOSE_BUFFER_SIZE, tmpstr, dataLength);
		}
		Trc_SHR_CM_addROMClassResourceToCache_Exit_Null(currentThread);
		return (void*)J9SHR_RESOURCE_STORE_FULL;
	}
	
	J9ShrOffset offset;
	getJ9ShrOffsetFromAddress(romAddress, true, &offset);

	resourceDescriptor->writeDataToCache(itemInCache, &offset);

	if (localRRM->storeNew(currentThread, itemInCache, cacheAreaForAllocate)) {
		resultWrapper = (void*)ITEMDATA(itemInCache);
	}
	cacheAreaForAllocate->commitUpdate(currentThread, false);

	Trc_SHR_CM_addROMClassResourceToCache_Exit(currentThread, resultWrapper);

	return resultWrapper;
}

/* THREADING: This function can be called multi-threaded */
const void*
SH_CacheMap::storeROMClassResource(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, UDATA forceReplace, const char** p_subcstr)
{
	const void* result = NULL;
	const char* fnName = "storeROMClassResource";
	const void* resourceWrapper;
	UDATA resourceKey;

	PORT_ACCESS_FROM_VMC(currentThread);
	Trc_SHR_CM_storeROMClassResource_Entry(currentThread, romAddress, resourceDescriptor, forceReplace);

	/* Check access before going to trouble of entering write mutex */
	if (!localRRM->permitAccessToResource(currentThread)) {
		if (p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_NO_ACCESS_TO_RESOURCE, "no access to resource");
		}
		Trc_SHR_CM_storeROMClassResource_Exit5(currentThread);
		return (void*)J9SHR_RESOURCE_STORE_ERROR;
	}
	
	if (_ccHead->enterWriteMutex(currentThread, false, fnName) != 0) {
		if (p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_ENTER_WRITE_MUTEX, "enterWriteMutex failed");
		}
		Trc_SHR_CM_storeROMClassResource_Exit1(currentThread);
		return (void*)J9SHR_RESOURCE_STORE_ERROR;
	}

	if (runEntryPointChecks(currentThread, (void*)romAddress, p_subcstr) == -1) {
		_ccHead->exitWriteMutex(currentThread, fnName);
		Trc_SHR_CM_storeROMClassResource_Exit2(currentThread);
		return (void*)J9SHR_RESOURCE_STORE_ERROR;
	}

	resourceKey = resourceDescriptor->generateKey(romAddress);

	/* Determine whether the record already exists in the cache */
	if ((resourceWrapper = (localRRM->findResource(currentThread, resourceKey))) != 0) {
		if (!forceReplace) {
			_ccHead->exitWriteMutex(currentThread, fnName);
			if (p_subcstr) {
				*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_DATA_EXISTS, "data already exists");
			}
			Trc_SHR_CM_storeROMClassResource_Exit3(currentThread);
			if ((TYPE_INVALIDATED_COMPILED_METHOD == ITEMTYPE(resourceDescriptor->wrapperToItem(resourceWrapper)))) {
				return (void*)J9SHR_RESOURCE_STORE_INVALIDATED;
			} else {
				return (void*)J9SHR_RESOURCE_STORE_EXISTS;
			}
		} else {
			localRRM->markStale(currentThread, resourceKey, resourceDescriptor->wrapperToItem(resourceWrapper));
		}
	}

	resourceWrapper = addROMClassResourceToCache(currentThread, romAddress, localRRM, resourceDescriptor, p_subcstr);
	
	if ((resourceWrapper == (void*)J9SHR_RESOURCE_STORE_FULL) || (resourceWrapper == (void*)J9SHR_RESOURCE_STORE_ERROR)) {
		result = resourceWrapper;
	} else if (resourceWrapper) {
		result = resourceDescriptor->unWrap(resourceWrapper);
	}

	/* Update the min/max boundary with the stored metadata entry only when the cache is
	 * being created by the current VM.
	 */
	if ((NULL != result)
	&& ((void*)J9SHR_RESOURCE_STORE_FULL != result)
	&& ((void*)J9SHR_RESOURCE_STORE_ERROR != result)
	&& _ccHead->isNewCache()
	&& (false == _metadataReleased)
	) {
#if !defined(J9ZOS390) && !defined(AIXPPC)
#if defined(LINUX)
		if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE))
#endif
			{
				updateAccessedShrCacheMetadataBounds(currentThread, (uintptr_t *) result);
			}
#endif /* !defined(J9ZOS390) && !defined(AIXPPC) */
	}

	_ccHead->exitWriteMutex(currentThread, fnName);
	Trc_SHR_CM_storeROMClassResource_Exit4(currentThread, result);
	return result;
}

/* THREADING: This function can be called multi-threaded */
UDATA
SH_CacheMap::updateROMClassResource(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, const J9SharedDataDescriptor* data, bool isUDATA , const char** p_subcstr)
{
	const char* fnName = "updateROMClassResource";
	IDATA result = 0;
	PORT_ACCESS_FROM_VMC(currentThread);
	bool hasWriteMutex = false;

	Trc_SHR_CM_updateROMClassResource_Entry1(currentThread, addressInCache, updateAtOffset, localRRM, resourceDescriptor, data, (I_32)isUDATA, UnitTest::unitTest);

	/* Check access before going to trouble of entering write mutex */
	if (!localRRM->permitAccessToResource(currentThread)) {
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_NO_ACCESS_TO_RESOURCE, "no access to resource");
		}
		Trc_SHR_CM_updateROMClassResource_Exit1(currentThread);
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	do {
		const void * resourceWrapper;
		UDATA resourceKey;

		/* lock the cache */
		if (_ccHead->enterWriteMutex(currentThread, true, fnName) != 0) {
			result = J9SHR_RESOURCE_STORE_ERROR;
			if (NULL != p_subcstr) {
				*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_ENTER_WRITE_MUTEX, "enterWriteMutex failed");
			}
			Trc_SHR_CM_updateROMClassResource_Exit2(currentThread);
			break;
		}
		hasWriteMutex = true;

		if (runEntryPointChecks(currentThread, (void*)addressInCache, p_subcstr) == -1) {
			Trc_SHR_CM_updateROMClassResource_Exit3(currentThread);
			result = J9SHR_RESOURCE_STORE_ERROR;
			break;
		}

		resourceKey = resourceDescriptor->generateKey(addressInCache);
		/* Determine whether the record already exists in the cache */
		if ((resourceWrapper = (localRRM->findResource(currentThread, resourceKey))) != 0) {
			UDATA dataLength;
			dataLength = resourceDescriptor->resourceLengthFromWrapper(resourceWrapper);
			if ((UDATA)updateAtOffset+data->length > dataLength) {
				if (NULL != p_subcstr) {
					const char* tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_DATA_SIZE_LARGER, "data %d larger than available %d");
					j9str_printf(PORTLIB, (char *)*p_subcstr, VERBOSE_BUFFER_SIZE, tmpcstr, updateAtOffset+(data->length), dataLength);
				}
				Trc_SHR_CM_updateROMClassResource_Exit4(currentThread, updateAtOffset, data->length, dataLength);
				result = J9SHR_RESOURCE_STORE_ERROR;
				break;
			}
			U_8* updateAddress = (U_8*)resourceDescriptor->unWrap(resourceWrapper) + updateAtOffset;
			const ShcItem *itemInCache = resourceDescriptor->wrapperToItem(resourceWrapper);
			ShcItem *tmpItem = NULL;
			const ShcItem *itemToUse = itemInCache;
			bool addResourceInTopLayer = false;
			if (false == isAddressInCache((void*)updateAddress, data->length, false, true)) {
				/* We cannot overwrite the existing resource which is in a lower layer cache, instead we add a new resource. 
				 * The old resources will be removed from the hashtable. */
				Trc_SHR_Assert_True(isAddressInCache((void*)updateAddress, data->length, false, false));
				tmpItem = (ShcItem*)j9mem_allocate_memory(itemInCache->dataLen, J9MEM_CATEGORY_CLASSES);
				if (NULL == tmpItem) {
					Trc_SHR_CM_updateROMClassResource_Exit8(currentThread);
					result = J9SHR_RESOURCE_STORE_ERROR;
					break;
				} else {
					memcpy(tmpItem, itemInCache, itemInCache->dataLen);
					addResourceInTopLayer = true;
					itemToUse = tmpItem;
				}
			}

			if (false == isUDATA) {
				resourceDescriptor->updateDataInCache(itemToUse, updateAtOffset, data);
			} else {
				resourceDescriptor->updateUDATAInCache(itemToUse, updateAtOffset, *((UDATA *)data->address));
			}
			if (addResourceInTopLayer) {
				U_8* resourceWrapper = ITEMDATA(itemToUse);
				SH_AttachedDataManager::SH_AttachedDataResourceDescriptor tmpDescriptor(ADWDATA(resourceWrapper), (U_32)resourceDescriptor->resourceLengthFromWrapper(resourceWrapper), resourceDescriptor->getResourceDataSubType());
				const void* ret = addROMClassResourceToCache(currentThread, addressInCache, localRRM, &tmpDescriptor, p_subcstr);
				Trc_SHR_CM_updateROMClassResource_Exit7(currentThread, updateAddress, data->length);
				if (((void*)J9SHR_RESOURCE_STORE_FULL == ret)
					|| ((void*)J9SHR_RESOURCE_STORE_ERROR == ret)
					|| (NULL == ret)
				) {
					result = J9SHR_RESOURCE_STORE_ERROR;
				}
				j9mem_free_memory(tmpItem);
				break;
			}
		} else {
			if(NULL != p_subcstr) {
				*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG),J9NLS_SHRC_CM_NO_DATA_EXISTS,"no data exists");
			}
			Trc_SHR_CM_updateROMClassResource_Exit5(currentThread);
			result = J9SHR_RESOURCE_STORE_ERROR;
		}
	} while (false);

	if (hasWriteMutex) {
		_ccHead->exitWriteMutex(currentThread, fnName);
	}

	Trc_SHR_CM_updateROMClassResource_Exit6(currentThread, result);
	return result;
}
/**
 * Find a shared ROM class resource in the cache
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] romAddress The rom class resource address
 * @param [in] localRRM Rom class resource manager
 * @param [in] resourceDescriptor Resource descriptor
 * @param [in] useReadMutex Whether to acquire read mutex
 * @param [out] p_subcstr Verbose buffer
 * @param [out] flags J9SHR_AOT_METHOD_FLAG_INVALIDATED is set if the compiled method has been invalidated
 *
 * @return If the class is found, pointer to the rom class resource, NULL otherwise
 * 
 * THREADING: This function can be called multi-threaded.
 */
const void*
SH_CacheMap::findROMClassResource(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, bool useReadMutex, const char** p_subcstr, UDATA* flags)
{
	const void* result = NULL;
	const char* fnName = "findROMClassResource";
	const void* resourceWrapper;
	UDATA resourceKey;

	PORT_ACCESS_FROM_VMC(currentThread);
	Trc_SHR_CM_findROMClassResource_Entry(currentThread, romAddress);

	if (!localRRM->permitAccessToResource(currentThread)) {
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_NO_ACCESS_TO_RESOURCE, "no access to resource");
		}
		Trc_SHR_CM_findROMClassResource_Exit3(currentThread);
		return NULL;
	}

	if ((true == useReadMutex) && (_ccHead->enterReadMutex(currentThread, fnName) != 0)) {
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_ENTER_READ_MUTEX, "enterReadMutex failed");
		}
		Trc_SHR_CM_findROMClassResource_FailedMutex(currentThread, romAddress);
		Trc_SHR_CM_findROMClassResource_ExitFailedMutex(currentThread, romAddress);
		return NULL;
	}

	if (runEntryPointChecks(currentThread, (void*)romAddress, p_subcstr) == -1) {
		if (true == useReadMutex) {
			_ccHead->exitReadMutex(currentThread, fnName);
		}
		Trc_SHR_CM_findROMClassResource_Exit1(currentThread);
		return NULL;
	}

	resourceKey = resourceDescriptor->generateKey(romAddress);

	if ((resourceWrapper = localRRM->findResource(currentThread, resourceKey)) != 0) {
		if (TYPE_INVALIDATED_COMPILED_METHOD != ITEMTYPE(resourceDescriptor->wrapperToItem(resourceWrapper))) {
			result = resourceDescriptor->unWrap(resourceWrapper);
		} else {
			/* found an invalidated AOT method */
			if (NULL != flags) {
				*flags |= J9SHR_AOT_METHOD_FLAG_INVALIDATED;
			}
		}
	}

	if (true == useReadMutex) {
		_ccHead->exitReadMutex(currentThread, fnName);
	}

	if (resourceWrapper) {
		updateBytesRead(resourceDescriptor->resourceLengthFromWrapper(resourceWrapper));
	}
	
	Trc_SHR_CM_findROMClassResource_Exit2(currentThread, result);
	return result;
}

/**
 * This method adds the fixed data associated with a compiled method to the cache.
 * The data defined by dataStart and dataSize and the data defined by codeStart and
 * and codeSize are copied to the cache as a single contiguous block. The stored
 * data is indexed by the romMethod's address. The return value is a pointer to the
 * stored data or null in case of an error.
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] romMethod Pointer to the J9ROMMethod structure
 * @param [in] dataStart Pointer to the start of the data to be stored
 * @param [in] dataSize Length of the data to be stored
 * @param [in] codeStart Pointer to the start of the code to be stored
 * @param [in] codeSize Length of the data to be stored
 * @param[in] forceReplace If non-zero, forces the compiled method to be stored, 
 * regardless of whether it already exists or not. If it does exist, the existing
 * cached method is marked stale.
 *
 * @return Pointer to the shared data if it was successfully stored
 * @return J9SHR_RESOURCE_STORE_EXISTS if the method already exists in the cache
 * (will never return this if forceReplace is non-null)
 * @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the method
 * @return J9SHR_RESOURCE_STORE_FULL if the cache is full
 * @return J9SHR_RESOURCE_STORE_INVALIDATED if the method already exits in the cache and it has been invalidated
 * (will never return this value if forceReplace is non-null)
 * @return NULL otherwise
 * 
 * THREADING: This function can be called multi-threaded
 */
const U_8*
SH_CacheMap::storeCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, const U_8* dataStart, UDATA dataSize, const U_8* codeStart, UDATA codeSize, UDATA forceReplace)
{
	const U_8* result;
	SH_CompiledMethodManager::SH_CompiledMethodResourceDescriptor descriptor(dataStart, (U_32)dataSize, codeStart, (U_32)codeSize);
	SH_CompiledMethodManager* localCMM;
	
	if (!(localCMM = getCompiledMethodManager(currentThread))) { 
		return NULL;
	}

	result = (const U_8*)storeROMClassResource(currentThread, romMethod, localCMM, &descriptor, forceReplace, NULL);
	
	return result;
}

/**
 * This method finds the fixed data associated with a compiled method.
 * The stored data is indexed by the romMethod's address. The return value is a 
 * pointer to the stored data or null if no matching data exists in the cache.
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] romMethod Pointer to the J9ROMMethod structure for which the stored data is required
 * @param [out] flags J9SHR_AOT_METHOD_FLAG_INVALIDATED is set if the compiled method has been invalidated
 *
 * @return If found, pointer to the stored data, NULL if not found or an invalidated method is found.
 * 
 * THREADING: This function can be called multi-threaded
 */
const U_8*
SH_CacheMap::findCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, UDATA* flags)
{
	const U_8* result;
	SH_CompiledMethodManager::SH_CompiledMethodResourceDescriptor descriptor;
	SH_CompiledMethodManager* localCMM;

	if (!(localCMM = getCompiledMethodManager(currentThread))) { 
		return NULL;
	}

	result = (const U_8*)findROMClassResource(currentThread, romMethod, localCMM, &descriptor, true, NULL, flags);
	if (NULL != result) {
#if !defined(J9ZOS390) && !defined(AIXPPC)
		if (_metadataReleased
#if defined(LINUX)
		&& J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE)
#endif
		) {
			if (TrcEnabled_Trc_SHR_CM_findCompiledMethod_metadataAccess
			&& (isAddressInReleasedMetaDataBounds(currentThread, (UDATA)result))
			) {
				J9InternalVMFunctions* vmFunctions = currentThread->javaVM->internalVMFunctions;
				J9ClassLoader* loader = NULL;
				J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)romMethod, &loader);
				if (NULL != romClass) {
					J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(romClass);
					J9UTF8* romMethodName = J9ROMMETHOD_NAME(romMethod);
					J9UTF8* romMethodSig = J9ROMMETHOD_SIGNATURE(romMethod);
					Trc_SHR_CM_findCompiledMethod_metadataAccess(
							currentThread,
							J9UTF8_LENGTH(romClassName),
							J9UTF8_DATA(romClassName),
							J9UTF8_LENGTH(romMethodName),
							J9UTF8_DATA(romMethodName),
							J9UTF8_LENGTH(romMethodSig),
							J9UTF8_DATA(romMethodSig),
							result
							);
				}
			}
		} else
#if defined(LINUX)
		if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_PERSISTENT_CACHE))
#endif
		{
			updateAccessedShrCacheMetadataBounds(currentThread, (uintptr_t *) result);
		}
#endif /* !defined(J9ZOS390) && !defined(AIXPPC) */
	}

	return result;
}

/**
 * Record the minimum and maximum addresses accessed in the shared classes cache.
 * @param [in] metadataAddress address accessed in metadata
 * @note an argument of 0 will reset the minimum and maximum
 */
void
SH_CacheMap::updateAccessedShrCacheMetadataBounds(J9VMThread* currentThread, uintptr_t const * metadataAddress)
{
	SH_CompositeCacheImpl* ccToUse = _ccHead;
	bool rc = false;

	do {
		rc = ccToUse->updateAccessedShrCacheMetadataBounds(currentThread, metadataAddress);
		ccToUse = ccToUse->getNext();
	} while ((false == rc) && (NULL != ccToUse));

	return;
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
* @return 0 if shared data was successfully stored
* @return J9SHR_RESOURCE_STORE_EXISTS if the attached data already exists in the cache
* (will never return this if forceReplace is non-null)
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the attached data
* @return J9SHR_RESOURCE_STORE_FULL if the cache is full
**/
UDATA
SH_CacheMap::storeAttachedData(J9VMThread* currentThread, const void* addressInCache, const J9SharedDataDescriptor *data, UDATA forceReplace)
{
	UDATA result = 0;
	SH_AttachedDataManager::SH_AttachedDataResourceDescriptor descriptor(data->address, (U_32)data->length, (U_16)data->type);
	SH_AttachedDataManager* localADM;
	J9SharedClassConfig * scconfig = currentThread->javaVM->sharedClassConfig;
	UDATA localVerboseFlags = scconfig->verboseFlags;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_storeAttachedData_Entry(currentThread, addressInCache, data);
	if (!(localADM = getAttachedDataManager(currentThread))) {
		Trc_SHR_CM_storeAttachedData_Exit1(currentThread);
		return J9SHR_RESOURCE_STORE_ERROR;
	}

	if (localVerboseFlags  & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA) {
		char subcstr[VERBOSE_BUFFER_SIZE];
		const char *pSubcstr = subcstr;
		const char *pType = attachedTypeString(data->type);

		subcstr[0] = 0;
		result = (UDATA)storeROMClassResource(currentThread, addressInCache, localADM, &descriptor, forceReplace, &pSubcstr);
		if (J9SHR_RESOURCE_MAX_ERROR_VALUE < (UDATA)result) {
			result = 0;
		}

		if(addressInCache && isAddressInCache(addressInCache, 0, false, false)) {
			J9ClassLoader* loader;
			J9InternalVMFunctions *vmFunctions = currentThread->javaVM->internalVMFunctions;
			J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)addressInCache, &loader);
			J9UTF8* methodName = J9ROMMETHOD_NAME((J9ROMMethod *)addressInCache);
			J9UTF8* methodSig = J9ROMMETHOD_SIGNATURE((J9ROMMethod *)addressInCache);
 			J9UTF8* className = J9ROMCLASS_CLASSNAME(romClass);

			if ( 0 == result ) {
				if (J9SHR_ATTACHED_DATA_TYPE_JITHINT == data->type) {
					char attachedDataString[41]; /* 8 hex bytes + null */
					CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_STORED_VERBOSE_JIT_ATTACHED_DATA, pType,
							formatAttachedDataString(currentThread, data->address, data->length, attachedDataString,
									sizeof(attachedDataString)),
									J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				} else {
					CACHEMAP_PRINT7(localVerboseFlags, J9NLS_SHRC_CM_STORED_VERBOSE_ATTACHED_MSG, pType,
							J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				}
			} else {
				CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_STORE_FAILED_VERBOSE_ATTACHED_MSG1, pType, pSubcstr ,
						J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
			}
		} else {
			CACHEMAP_PRINT3(localVerboseFlags, J9NLS_SHRC_CM_STORE_FAILED_VERBOSE_ATTACHED_NOCLASSINFO_MSG, pType, addressInCache, pSubcstr);
		}
	} else {
		result = (UDATA)storeROMClassResource(currentThread, addressInCache, localADM, &descriptor, forceReplace, NULL);
		if (J9SHR_RESOURCE_MAX_ERROR_VALUE < (UDATA)result) {
			result = 0;
		}
	}

	Trc_SHR_CM_storeAttachedData_Exit3(currentThread, result);
	return result;
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
* @return Pointer to the shared data if it was successfully stored
*
* @return 0 on success
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the attached data
*/
UDATA
SH_CacheMap::updateAttachedData(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, const J9SharedDataDescriptor* data)
{
	UDATA result;
	SH_AttachedDataManager::SH_AttachedDataResourceDescriptor descriptor(data->address, (U_32)data->length, (U_16)data->type);
	SH_AttachedDataManager* localADM;
	J9SharedClassConfig * scconfig = currentThread->javaVM->sharedClassConfig;
	UDATA localVerboseFlags = scconfig->verboseFlags;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_updateAttachedData_Entry(currentThread, addressInCache, updateAtOffset);
	if (!(localADM = getAttachedDataManager(currentThread))) {
		Trc_SHR_CM_updateAttachedData_Exit1(currentThread);
		return J9SHR_RESOURCE_STORE_ERROR;
	}
	if (localVerboseFlags  & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA) {
		char subcstr[VERBOSE_BUFFER_SIZE];
		const char* pSubcstr = subcstr;
		const char *pType = attachedTypeString(data->type);

		subcstr[0] = 0;
		result = updateROMClassResource(currentThread, addressInCache, updateAtOffset, localADM, &descriptor, data, false, &pSubcstr);

		if(addressInCache && isAddressInCache(addressInCache, 0, false, false)) {
			J9ClassLoader* loader;
			J9InternalVMFunctions *vmFunctions = currentThread->javaVM->internalVMFunctions;
			J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)addressInCache, &loader);
			J9UTF8* methodName = J9ROMMETHOD_NAME((J9ROMMethod *)addressInCache);
			J9UTF8* methodSig = J9ROMMETHOD_SIGNATURE((J9ROMMethod *)addressInCache);
			J9UTF8* className = J9ROMCLASS_CLASSNAME(romClass);

			if (0 == result) {
				if (J9SHR_ATTACHED_DATA_TYPE_JITHINT == data->type) {
					char attachedDataString[41]; /* 8 hex bytes + null */
					CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_UPDATED_VERBOSE_JIT_ATTACHED_DATA, pType,
							formatAttachedDataString(currentThread, data->address, data->length, attachedDataString,
									sizeof(attachedDataString)),
									J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				} else {
					CACHEMAP_PRINT7(localVerboseFlags, J9NLS_SHRC_CM_UPDATED_VERBOSE_ATTACHED_MSG, pType,
							J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				}
			} else {
				CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_UPDATE_FAILED_VERBOSE_ATTACHED_MSG1, pType, pSubcstr,
						J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
			}
		} else {
			CACHEMAP_PRINT3(localVerboseFlags, J9NLS_SHRC_CM_UPDATE_FAILED_VERBOSE_ATTACHED_NOCLASSINFO_MSG, pType, addressInCache, pSubcstr);
		}
	} else {
		result = updateROMClassResource(currentThread, addressInCache, updateAtOffset, localADM, &descriptor, data, false, NULL);
	}

	Trc_SHR_CM_updateAttachedData_Exit2(currentThread, result);
	return result;
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
* @return 0 on success
* @return J9SHR_RESOURCE_STORE_ERROR if an error occurred storing the attached data
*/
UDATA
SH_CacheMap::updateAttachedUDATA(J9VMThread* currentThread, const void* addressInCache, UDATA type, I_32 updateAtOffset, UDATA value)
{
	UDATA result = 0;
	SH_AttachedDataManager::SH_AttachedDataResourceDescriptor descriptor((U_8 *)&value, (U_32)sizeof(UDATA), (U_16) type);
	SH_AttachedDataManager* localADM;
	J9SharedDataDescriptor data;
	J9SharedClassConfig * scconfig = currentThread->javaVM->sharedClassConfig;
	UDATA localVerboseFlags = scconfig->verboseFlags;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_updateAttachedUDATA_Entry(currentThread, addressInCache, updateAtOffset);

	if (!(localADM = getAttachedDataManager(currentThread))) {
		Trc_SHR_CM_updateAttachedUDATA_Exit1(currentThread);
		return J9SHR_RESOURCE_STORE_ERROR;
	}
	data.address = (U_8 *)&value;
	data.length = sizeof(UDATA);
	data.type = type;

	if (localVerboseFlags  & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA) {
		char subcstr[VERBOSE_BUFFER_SIZE];
		const char* pSubcstr = subcstr;
		const char *pType = attachedTypeString(data.type);

		subcstr[0] = 0;
		result = updateROMClassResource(currentThread, addressInCache, updateAtOffset, localADM, &descriptor, &data, true, &pSubcstr);

		if(addressInCache && isAddressInCache(addressInCache, 0, false, false)) {
			J9ClassLoader* loader;
			J9InternalVMFunctions *vmFunctions = currentThread->javaVM->internalVMFunctions;
			J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)addressInCache, &loader);
			J9UTF8* methodName = J9ROMMETHOD_NAME((J9ROMMethod *)addressInCache);
			J9UTF8* methodSig = J9ROMMETHOD_SIGNATURE((J9ROMMethod *)addressInCache);
			J9UTF8* className = J9ROMCLASS_CLASSNAME(romClass);

			if (0 == result) {
				CACHEMAP_PRINT7(localVerboseFlags, J9NLS_SHRC_CM_UPDATED_VERBOSE_ATTACHED_MSG, pType,
						J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
			} else {
				CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_UPDATE_FAILED_VERBOSE_ATTACHED_MSG1, pType, pSubcstr,
						J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
			}
		} else {
			CACHEMAP_PRINT3(localVerboseFlags, J9NLS_SHRC_CM_UPDATE_FAILED_VERBOSE_ATTACHED_NOCLASSINFO_MSG, pType, addressInCache, pSubcstr);
		}
	} else {
		result = updateROMClassResource(currentThread, addressInCache, updateAtOffset, localADM, &descriptor, &data, true, NULL);
	}

	Trc_SHR_CM_updateAttachedUDATA_Exit2(currentThread, result);
	return result;
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
* 				if data->address is NULL then data->length is ignored. On success, data->address will point to newly allocated memory filled with the data,
* 				if data->address is non NULL then data->length is the size of the buffer pointed by data->address. On success, the buffer is filled with the data.
* @param[out] corruptOffset If not -1, the updateAtOffset supplied to j9shr_updateAttachedData() when the incomplete write occurred.
*
* @return A pointer to the start of the data if successful
* @return NULL if non-stale data cannot be found, or the data is corrupt. In case data is corrupt, data->address should be freed by the caller if it was passed as NULL.
* @return J9SHR_RESOURCE_STORE_ERROR if the data buffer in the parameter is not sufficient to store the data found
* @return J9SHR_RESOURCE_BUFFER_ALLOC_FAILED if failed to allocate memory for data buffer
* @return J9SHR_RESOURCE_TOO_MANY_UPDATES if not able to read consistent data when running read-only
*
*/
const U_8*
SH_CacheMap::findAttachedDataAPI(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset)
{
	const U_8* result = NULL;
	J9SharedClassConfig * scconfig = currentThread->javaVM->sharedClassConfig;
	UDATA localVerboseFlags = scconfig->verboseFlags;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_findAttachedDataAPI_Entry(currentThread, addressInCache, addressInCache);

	if (localVerboseFlags  & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_JITDATA) {
		char subcstr[VERBOSE_BUFFER_SIZE];
		const char* pSubcstr = subcstr;
		subcstr[0] = 0;
		const char *pType = attachedTypeString(data->type);

		result = findAttachedData(currentThread, addressInCache, data, corruptOffset, &pSubcstr);

		if(addressInCache && isAddressInCache(addressInCache, 0, false, false)) {
			J9ClassLoader* loader;
			J9InternalVMFunctions *vmFunctions = currentThread->javaVM->internalVMFunctions;
			J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)addressInCache, &loader);
			J9UTF8* methodName = J9ROMMETHOD_NAME((J9ROMMethod *)addressInCache);
			J9UTF8* methodSig = J9ROMMETHOD_SIGNATURE((J9ROMMethod *)addressInCache);
			J9UTF8* className = J9ROMCLASS_CLASSNAME(romClass);

			if (( NULL == result) || (J9SHR_RESOURCE_MAX_ERROR_VALUE >= (UDATA)result)) {
				CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_FIND_FAILED_VERBOSE_ATTACHED_MSG1, pType, pSubcstr, J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName),
						J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
			} else {
				if (J9SHR_ATTACHED_DATA_TYPE_JITHINT == data->type) {
					char attachedDataString[41]; /* 8 hex bytes + null */
					CACHEMAP_PRINT8(localVerboseFlags, J9NLS_SHRC_CM_FOUND_VERBOSE_JIT_ATTACHED_DATA, pType,
							formatAttachedDataString(currentThread, data->address, data->length, attachedDataString, sizeof(attachedDataString)),
							J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName), J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig), J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				} else {
					CACHEMAP_PRINT7(localVerboseFlags, J9NLS_SHRC_CM_FOUND_VERBOSE_ATTACHED_MSG, pType, J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName),
							J9UTF8_LENGTH(methodSig), J9UTF8_DATA(methodSig),	J9UTF8_LENGTH(className), J9UTF8_DATA(className));
				}
			}
		} else {
			CACHEMAP_PRINT3(localVerboseFlags, J9NLS_SHRC_CM_FIND_FAILED_VERBOSE_ATTACHED_NOCLASSINFO_MSG, pType, addressInCache, pSubcstr);
		}
	} else {
		result = findAttachedData(currentThread, addressInCache, data, corruptOffset, NULL);
	}

	Trc_SHR_CM_findAttachedDataAPI_Exit(currentThread, result);
	return result;
}


const U_8*
SH_CacheMap::findAttachedData(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset, const char** p_subcstr)
{
	const U_8* result;
	const char* fnName = "findAttachedData";
	SH_AttachedDataManager* localADM ;
	bool bufferAllocated = false;
	PORT_ACCESS_FROM_VMC(currentThread);

	/* set the value to non-corrupt to prevent incorrect interpretation when NULL is returned due to some other error */
	*corruptOffset = -1;

	Trc_SHR_CM_findAttachedData_Entry(currentThread, addressInCache, data);
	if (!(localADM = getAttachedDataManager(currentThread))) {
		Trc_SHR_CM_findAttachedData_Exit1(currentThread);
		return NULL;
	}

	if (_ccHead->enterReadMutex(currentThread, fnName) != 0) {
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_ENTER_READ_MUTEX, "enterReadMutex failed");
		}
		Trc_SHR_CM_findAttachedData_Exit2(currentThread);
		return NULL;
	}

	SH_AttachedDataManager::SH_AttachedDataResourceDescriptor descriptor(NULL, 0, (U_16)data->type);
	result = (const U_8*)findROMClassResource(currentThread, addressInCache, localADM, &descriptor, false, p_subcstr, NULL);
	if (NULL != result) {
		I_32 corrupt;
		U_32 wrapperLength, dataLength;
		const AttachedDataWrapper *wrapper;

		wrapperLength = descriptor.getWrapperLength();
		wrapper = (AttachedDataWrapper *)(result - wrapperLength);

		dataLength = ADWLEN(wrapper);
		if (NULL != data->address) {
			if (data->length < dataLength) {
				result = (U_8 *)J9SHR_RESOURCE_STORE_ERROR;
				if (NULL != p_subcstr) {
					const char *tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_DATA_SIZE_LARGER, "data %d larger than available %d");
					j9str_printf(PORTLIB, (char *)*p_subcstr, VERBOSE_BUFFER_SIZE, tmpcstr, dataLength, data->length);
				}
				goto _exitWithError;
			}
		} else {
			data->address = (U_8 *)j9mem_allocate_memory(dataLength, J9MEM_CATEGORY_CLASSES);
			if (NULL == data->address) {
				result = (const U_8 *)J9SHR_RESOURCE_BUFFER_ALLOC_FAILED;
				if (NULL != p_subcstr) {
					const char *tmpcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_MEMORY_ALLOC_FAILED, "memory allocation of %d bytes failed");
					j9str_printf(PORTLIB, (char *)*p_subcstr, VERBOSE_BUFFER_SIZE, tmpcstr, dataLength);
				}
				goto _exit;
			}
			bufferAllocated = true;
		}

		if (false == _ccHead->isRunningReadOnly()) {
			memcpy(data->address, result, dataLength);
			data->length = dataLength;
			result = data->address;
			corrupt = ADWCORRUPT(wrapper);
			if (NULL != corruptOffset) {
				*corruptOffset = corrupt;
			}
			if (-1 != corrupt) {
				result = NULL;
			}
			goto _exit;
		} else {
			/* for read-only cache, we need to explicitly ensure that data read is consistent,
			 * since read-only cache does not honour cache lock or can not acquire read lock. */
			I_32 initialUpdateCount, finalUpdateCount;
			I_32 updateCountTryCount = 0;
			I_32 corruptTryCount = 0;

			do {
				initialUpdateCount = ADWUPDATECOUNT(wrapper);
				VM_AtomicSupport::readBarrier();

				memcpy(data->address, result, dataLength);
				data->length = dataLength;
				result = data->address;
				VM_AtomicSupport::readBarrier();

				/* Used during unit testing. See AttachedDataTest::testFindReadOnly(). */
				if (UnitTest::ATTACHED_DATA_UPDATE_COUNT_TEST == UnitTest::unitTest) {
					omrthread_suspend();
				}

				finalUpdateCount = ADWUPDATECOUNT(wrapper);
				if (initialUpdateCount != finalUpdateCount) {
					if((++updateCountTryCount) > FIND_ATTACHED_DATA_RETRY_COUNT) {
						result = (U_8 *)J9SHR_RESOURCE_TOO_MANY_UPDATES;
						if (NULL != p_subcstr) {
							*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_TOO_MANY_UPDATES, "too many updates while reading");
						}
						goto _exitWithError;
					} else {
						continue;
					}
				}

				/* Used during unit testing. See AttachedDataTest::testFindReadOnly().
				 * Need to suspend during first attempt only as data can be marked as corrupt only once.
				 */
				if ((UnitTest::ATTACHED_DATA_CORRUPT_COUNT_TEST == UnitTest::unitTest) && (corruptTryCount == 0)) {
					omrthread_suspend();
				}

				corrupt = ADWCORRUPT(wrapper);
				if (NULL != corruptOffset) {
					*corruptOffset = corrupt;
				}
				if (-1 != corrupt) {
					if ((++corruptTryCount) > FIND_ATTACHED_DATA_RETRY_COUNT) {
						/* exceeded retry count, data must be corrupt; return NULL */
						result = NULL;
						goto _exit;
					} else {
						omrthread_sleep(FIND_ATTACHED_DATA_CORRUPT_WAIT_TIME);
					}
				} else {
					goto _exit;
				}
			} while(true);
		}
	} else {
		if (NULL != p_subcstr) {
			*p_subcstr = j9nls_lookup_message((J9NLS_INFO | J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_CACHE_FIND_NOTFOUND, "no data in cache");
		}
	}

_exitWithError:
	if (true == bufferAllocated) {
		j9mem_free_memory(data->address);
		data->address = NULL;
	}
_exit:
	_ccHead->exitReadMutex(currentThread, fnName);
	Trc_SHR_CM_findAttachedData_Exit3(currentThread, result);
	return result;
}

/**
 * Note this calls itself recursively via getCacheAreaForDataType() -> createNewCachelet() -> addByteDataToCache().
 * Being called with (data->type == TYPE_CACHELET) ends the recursion.
 * 
 * For TYPE_CACHELET, returns NULL if the current supercache has insufficient space.
 * The caller, createNewCachelet(), can allocate another supercache and retry.
 *
 * THREADING: MUST be protected by cache write mutex - therefore single-threaded within this JVM
 */
SH_CacheMap::BlockPtr
SH_CacheMap::addByteDataToCache(J9VMThread* currentThread, SH_Manager* localBDM, const J9UTF8* tokenKeyInCache, 
		const J9SharedDataDescriptor* data, SH_CompositeCacheImpl* forceCache, bool writeWithoutMetadata)
{
	U_32 wrapperLength;
	ByteDataWrapper* bdwInCache = NULL;
	BlockPtr result = NULL;
	ShcItem item;
	ShcItem* itemPtr = &item;
	ShcItem* itemInCache = NULL;
	UDATA dataNotIndexed = (data->flags & J9SHRDATA_NOT_INDEXED);
	UDATA dataReadWrite = (data->flags & J9SHRDATA_USE_READWRITE);
	UDATA dataIsPrivate = (data->flags & J9SHRDATA_IS_PRIVATE);
	BlockPtr memToSet;
	BlockPtr externalBlock = NULL;
	SH_CompositeCacheImpl* cacheForAllocate = (forceCache) ? forceCache : _cc;
	U_16 wrapperType;
	bool localWriteWithoutMetadata = writeWithoutMetadata;
	SH_CompositeCacheImpl* preCC = _cc;
	const J9UTF8* tokenKeyToUse = tokenKeyInCache;

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));

	/* Note that readWrite data should not be private or unindexed */
	if ((dataNotIndexed || dataIsPrivate) && dataReadWrite) {
		return NULL;
	}

	Trc_SHR_CM_addByteDataToCache_Entry(currentThread, localBDM, tokenKeyToUse, data);

	/* Calculate the amounts of space needed by the new records */
	wrapperType = ((dataNotIndexed) ? TYPE_UNINDEXED_BYTE_DATA : TYPE_BYTE_DATA);
	wrapperLength = ((dataNotIndexed) ? 0 : sizeof(ByteDataWrapper));

	if (J9_ARE_ANY_BITS_SET(*_runtimeFlags, RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE)) {
		if (J9_ARE_ALL_BITS_SET(data->flags, J9SHRDATA_USE_READWRITE)) {
			increaseUnstoredBytes(wrapperLength);
		} else {
			increaseUnstoredBytes(wrapperLength + (U_32)data->length);
		}
		Trc_SHR_CM_addByteDataToCache_Exit1(currentThread);
		return NULL;
	}

	if (!dataReadWrite) {
		if (data->type != J9SHR_DATA_TYPE_CACHELET) {
			_ccHead->initBlockData(&itemPtr, wrapperLength + (U_32)data->length, wrapperType);
			if (!forceCache) {
				cacheForAllocate = getCacheAreaForDataType(currentThread, wrapperType, _ccHead->getBytesRequiredForItemWithAlign(itemPtr, SHC_WORDALIGN, sizeof(ByteDataWrapper)));
				if (!cacheForAllocate) {
					/* This may indicate size required is bigger than the cachelet size. */
					/* TODO: In offline mode, should be fatal */
					return NULL;
				}
			}
			if ((itemInCache = (ShcItem*)(cacheForAllocate->allocateBlock(currentThread, itemPtr, SHC_WORDALIGN, sizeof(ByteDataWrapper)))) == NULL) {
				/* Not enough space in cache to accommodate this item. */
				return NULL;
			}
		} else {
			/* J9SHR_DATA_TYPE_CACHELET is not supported anymore, this block can be removed. */
			_ccHead->initBlockData(&itemPtr, wrapperLength, TYPE_CACHELET);
			cacheForAllocate->allocateWithSegment(currentThread, itemPtr, (U_32)data->length, &externalBlock);
			if (externalBlock == NULL) {
				/* Do nothing - we should allocate a new supercache */
			}
		}
	} else {
		_ccHead->initBlockData(&itemPtr, wrapperLength, wrapperType);
		itemInCache = (ShcItem*)(cacheForAllocate->allocateWithReadWriteBlock(currentThread, itemPtr, (U_32)data->length, &externalBlock));
		/* Expect readWrite to become full before the cache does - so don't report it */
	}

	if ((preCC != _cc) && (tokenKeyInCache != NULL) && !dataNotIndexed) {
		/* Our token key is in a different supercache - we need to reallocate it */
		tokenKeyToUse = addScopeToCache(currentThread, tokenKeyInCache);
	}
	
	if (localWriteWithoutMetadata) {
		if (externalBlock) {
			memToSet = externalBlock;
		} else {
			memToSet = (BlockPtr)itemInCache;		/* Write over the ShcItem data - TODO: Remove this when tidying up */
		}
	} else if (dataNotIndexed) {
		memToSet = (BlockPtr)ITEMDATA(itemInCache);
	} else {
		if (itemInCache == NULL) {
			Trc_SHR_CM_addByteDataToCache_Exit_Null(currentThread);
			return NULL;
		}
		bdwInCache = (ByteDataWrapper*)ITEMDATA(itemInCache);
		bdwInCache->dataLength = (U_32)data->length;
		getJ9ShrOffsetFromAddress(tokenKeyToUse, false, &bdwInCache->tokenOffset);
		if (externalBlock) {
			/**
			 * externalBlock is either from J9SHRDATA_USE_READWRITE or J9SHR_DATA_TYPE_CACHELET.
			 * Currently no code is storing byte data using J9SHRDATA_USE_READWRITE. J9SHR_DATA_TYPE_CACHELET is not supported anymore. 
			 * This block can be removed.
			 */
			getJ9ShrOffsetFromAddress(externalBlock, true, &bdwInCache->externalBlockOffset);
		} else {
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
			bdwInCache->externalBlockOffset.cacheLayer = 0;
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
			bdwInCache->externalBlockOffset.offset = 0;
		}
		bdwInCache->dataType = (U_8)data->type;
		/* Only set privateOwnerID if the data is private - when JVM shuts down, all of its privateOwnerIDs are set to 0 */
		bdwInCache->privateOwnerID = (bdwInCache->inPrivateUse = (U_8)dataIsPrivate) ? _ccHead->getJVMID() : 0;
		memToSet = (BlockPtr)getDataFromByteDataWrapper(bdwInCache);
	}
	if (memToSet == NULL) {
		Trc_SHR_CM_addByteDataToCache_Exit_Null(currentThread);
		return NULL;
	}
	if (data->flags & J9SHRDATA_ALLOCATE_ZEROD_MEMORY) {
		memset(memToSet, 0, data->length);
	} else {
		memcpy(memToSet, data->address, data->length);
	}

	/* storeNew is effectively a no-op for data marked as J9SHRDATA_NOT_INDEXED */
	if (localWriteWithoutMetadata) {
		result = memToSet;
	} else if (localBDM->storeNew(currentThread, itemInCache, cacheForAllocate)) {
		if (dataNotIndexed) {
			result = (BlockPtr)ITEMDATA(itemInCache);
		} else {
			result = (BlockPtr)getDataFromByteDataWrapper((ByteDataWrapper*)ITEMDATA(itemInCache));
		}
	}
	cacheForAllocate->commitUpdate(currentThread, itemPtr->dataType == TYPE_CACHELET);

	if ((NULL != bdwInCache) && (J9SHR_DATA_TYPE_AOTHEADER == bdwInCache->dataType)) {
		this->_cc->setAOTHeaderPresent(currentThread);
	}

	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));
	Trc_SHR_CM_addByteDataToCache_Exit(currentThread, result);
	return result;
}

/**
 * Stores data in the cache which against "key" which is a UTF8 string.
 * If data of a different dataType uses the same key, this is added without affecting the other data stored under that key.
 * If J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE is set, one store for that key/dataType combination is allowed. Subsequent stores will simply return the existing data.
 * If J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE is not set, the following is true:
 *   If data of the same dataType already exists for that key, the original data is marked "stale" and the new data is added.
 *   If the exact same data already exists in the cache under the same key and dataType, the data is not duplicated and the cached version is returned.
 *   If null is passed as the data argument, all existing data against that key is marked "stale".
 * The function returns null if the cache is full, otherwise it returns a pointer to the shared location of the data.
 * 
 * data->flags can be any of the following:
 *   J9SHRDATA_IS_PRIVATE - the data is private to this JVM
 *   J9SHRDATA_ALLOCATE_ZEROD_MEMORY - allocate zero'd space in the cache to be written into later
 *      bear in mind that this should be either private memory which doesn't require locking, or read/write memory
 *   J9SHRDATA_USE_READWRITE - allocate into the cache read/write area
 *   J9SHRDATA_NOT_INDEXED - add the data, but don't index it (saves cache space)
 *      data must therefore be referenced by other data as it can never be retrieved by findSharedData
 *   J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE - only allow one store for a given key/type combination
 *      subsequent stores return the existing data regardless of whether it matches the input data
 *   J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE_OVERWRITE - Similar to J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE, only one record of key/dataType combination is allowed in the shared cache.
 *   	subsequent stores overwrite the existing data. This flag is ignored if J9SHRDATA_NOT_INDEXED, J9SHRDATA_ALLOCATE_ZEROD_MEMORY or J9SHRDATA_USE_READWRITE presents
 * 
 * @param[in] currentThread  The current thread
 * @param[in] key  The UTF8 key to store the data against
 * @param[in] keylen  The length of the key
 * @param[in] data  The actual data
 * 
 * @return  The new location of the cached data or null
 * 
 * THREADING: This function can be called multi-threaded
 */
const U_8* 
SH_CacheMap::storeSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, const J9SharedDataDescriptor* data)
{
	const U_8* result = NULL;
	const char* fnName = "storeSharedData";
	ByteDataWrapper* bdwInCache = NULL;
	UDATA foundDatalen = 0;
	char utfKey[STACK_STRINGBUF_SIZE];
	char* utfKeyPtr = (char*)&utfKey;
	J9UTF8* utfKeyStruct = NULL;
	UDATA dataNotIndexed = (data != NULL) ? (data->flags & J9SHRDATA_NOT_INDEXED) : 0;
	SH_ByteDataManager* localBDM;
	bool overwrite = false;

	PORT_ACCESS_FROM_VMC(currentThread);

	Trc_SHR_Assert_True(_sharedClassConfig != NULL);

	if (((key == NULL) || (keylen == 0) || (data->length > MAX_INT)) && !dataNotIndexed) {
		return NULL;
	}
	if (!(localBDM = getByteDataManager(currentThread))) { 
		return NULL;
	}

	Trc_SHR_CM_storeSharedData_Entry(currentThread, keylen, key, data);
	
	if (J9_ARE_ALL_BITS_SET(data->flags, J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE_OVERWRITE)) {
		if (J9_ARE_NO_BITS_SET(data->flags, J9SHRDATA_NOT_INDEXED | J9SHRDATA_ALLOCATE_ZEROD_MEMORY | J9SHRDATA_USE_READWRITE)
			&& (data->length > 0)
			&& (NULL != data->address)
		) {
			overwrite = true;
		}
	}

	if (_ccHead->enterWriteMutex(currentThread, overwrite, fnName) != 0) {
		Trc_SHR_CM_storeSharedData_Exit1(currentThread);
		return NULL;
	}

	if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
		_ccHead->exitWriteMutex(currentThread, fnName);
		Trc_SHR_CM_storeSharedData_Exit2(currentThread);
		return NULL;
	}

	/* Determine whether the record(s) already exist in the cache */
	if (!dataNotIndexed) {
		if (data != NULL) {
			bdwInCache = localBDM->findSingleEntry(currentThread, key, keylen, data->type, ((data->flags & J9SHRDATA_IS_PRIVATE) ? _ccHead->getJVMID() : 0), &foundDatalen);
			if ((J9SHR_DATA_TYPE_AOTHEADER == data->type)
				&& (this->_cc->isAOTHeaderPresent(currentThread))
			) {
				/* If AOT Header has already been added, above call to findSingleEntry() should ideally never return NULL.
				 * AOT Header is expected to be added to the shared cache only once.
				 * Once added, every JVM attached to the cache should be able to locate and re-use it.
				 */
				Trc_SHR_Assert_True(NULL != bdwInCache);
			}
			if (NULL != bdwInCache) {
				result = (const U_8*)getDataFromByteDataWrapper(bdwInCache);
				if (data->address == NULL) {
					/* We're being asked to allocate memory that has already been allocated */
					if (data->flags & J9SHRDATA_ALLOCATE_ZEROD_MEMORY) {
						goto _done;
					}
				} else {
					if (J9_ARE_ANY_BITS_SET(data->flags, J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE | J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE_OVERWRITE)) {
						if (J9SHR_DATA_TYPE_STARTUP_HINTS == data->type) {
							Trc_SHR_Assert_True(&_sharedClassConfig->localStartupHints.hintsData == (J9SharedStartupHintsDataDescriptor*)data->address);
							Trc_SHR_Assert_True(sizeof(J9SharedStartupHintsDataDescriptor) == data->length);
							updateLocalHintsData(currentThread,&_sharedClassConfig->localStartupHints, (const J9SharedStartupHintsDataDescriptor*)result, overwrite);
						}
						if (overwrite) {
							if (data->length == foundDatalen) {
								if (isAddressInCache(result, foundDatalen, false, true)) {
									if (memcmp(data->address, result, foundDatalen) != 0) {
										memcpy((void *)result, data->address, foundDatalen);
										Trc_SHR_CM_storeSharedData_OverwriteExisting(currentThread, result, data->address, foundDatalen);
									}
								} else if (isAddressInCache(result, foundDatalen, true, true)) {
									/* Do nothing here. We do not overwrite if the existing byteData is in readWrite area */ 
								} else if (isAddressInCache(result, foundDatalen, false, false)) {
									/* Existing byteData is in non-readwrite area of a lower layer cache, in this case, we store a new byteData.
									 * Even though we are unable to mark the existing byteData stale, SH_ByteDataManager always find the byteData under the same key in higher layer first. */
									Trc_SHR_CM_storeSharedData_OverwriteExisting_NotInTopLayer(currentThread, data->address, foundDatalen);
									goto _addData;
								}
							} else {
								Trc_SHR_Assert_ShouldNeverHappen();
							}
						}
						/* We've already got the data for our key/type, so return it */
						Trc_SHR_CM_storeSharedData_FoundExisting(currentThread);
						goto _done;
					} else if (data->length == foundDatalen) {
						/* Only return the data if is an exact byte match */
						if (memcmp(data->address, result, foundDatalen)==0) {
							Trc_SHR_CM_storeSharedData_FoundExisting(currentThread);
							goto _done;
						}
					}
				}
				markItemStale(currentThread, (ShcItem*)BDWITEM(bdwInCache), false);
			}
		} else {
			localBDM->markAllStaleForKey(currentThread, key, keylen);
		}
	}

_addData:
	/* If data is NULL or datalen <= 0, mark the original item(s) stale, but don't store anything */
	if ((data != NULL) && (data->length > 0) && ((data->address != NULL) || (data->flags & J9SHRDATA_ALLOCATE_ZEROD_MEMORY))) {
		const J9UTF8* tokenKey = NULL;	

		if (!dataNotIndexed) {
			SH_ScopeManager* localSCM = getScopeManager(currentThread);
			if (NULL == localSCM) {
				Trc_SHR_CM_storeSharedData_NoSCM(currentThread);
				result = NULL;
				goto _done;
			}
			/* Create J9UTF8 struct as key */
			if (keylen >= (STACK_STRINGBUF_SIZE - sizeof(J9UTF8))) {
				if (!(utfKeyPtr = (char*)j9mem_allocate_memory((keylen * sizeof(U_8)) + sizeof(ShcItem), J9MEM_CATEGORY_CLASSES))) {
					Trc_SHR_CM_storeSharedData_NoMem(currentThread);
					result = NULL;
					goto _done;
				}
			}
			utfKeyStruct = (J9UTF8*)utfKeyPtr;
			J9UTF8_SET_LENGTH(utfKeyStruct, (U_16)keylen);
			strncpy((char*)J9UTF8_DATA(utfKeyStruct), (char*)key, keylen);
	
			if (!(tokenKey = localSCM->findScopeForUTF(currentThread, utfKeyStruct))) {
				if (!(tokenKey = addScopeToCache(currentThread, utfKeyStruct))) {
					Trc_SHR_CM_storeSharedData_CannotAddScope(currentThread);
					result = NULL;
					goto _done;
				}
			}
		}
		result = (const U_8*)addByteDataToCache(currentThread, localBDM, tokenKey, data, NULL, false);
	}

_done:
	if (utfKeyPtr && (utfKeyPtr != (char*)&utfKey)) {
		j9mem_free_memory(utfKeyPtr);
	}

	_ccHead->exitWriteMutex(currentThread, fnName);

	Trc_SHR_CM_storeSharedData_Exit3(currentThread, result);
	return result;
}

/**
 * Retrieves data in the cache which has been stored against "key" which is a UTF8 string.
 * Populates descriptorPool with J9SharedDataDescriptors describing data elements. Returns the number of elements found.
 * The data returned can include private data of other JVMs or data of different types stored under the same key.
 * 
 * @param[in] vmThread  The current thread
 * @param[in] key  The UTF8 key against which the data was stored
 * @param[in] keylen  The length of the key
 * @param[in] limitDataType  Optional. If used, only data of the type constant specified is returned. If 0, all data stored under that key is returned
 * @param[in] includePrivateData  If non-zero, will also add private data of other JVMs stored under that key into the array
 * @param[out] firstItem The first result found. Can be NULL.
 * @param[out] descriptorPool Populated with all of the results. Note that the pool is not cleaned. Can be NULL - if this is the case, the number of entries is returned.
 * 
 * @return  The number of data elements found or -1 in the case of error
 * 
 * THREADING: This function can be called multi-threaded
 */
IDATA
SH_CacheMap::findSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, UDATA limitDataType, UDATA includePrivateData, J9SharedDataDescriptor* firstItem, const J9Pool* descriptorPool)
{
	IDATA result;
	const char* fnName = "findSharedData";
	SH_ByteDataManager* localBDM;

	Trc_SHR_Assert_True(_sharedClassConfig != NULL);

	if ((key == NULL) || (keylen == 0)) {
		return -1;
	}
	if (!(localBDM = getByteDataManager(currentThread))) { 
		return 0;
	}

	Trc_SHR_CM_findSharedData_Entry(currentThread, keylen, key);

	if (_ccHead->enterReadMutex(currentThread, fnName) != 0) {
		Trc_SHR_CM_findSharedData_ExitFailedMutex(currentThread, keylen, key);
		return -1;
	}

	if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
		_ccHead->exitReadMutex(currentThread, fnName);
		Trc_SHR_CM_findSharedData_Exit1(currentThread);
		return -1;
	}

	result = localBDM->find(currentThread, key, keylen, limitDataType, includePrivateData, firstItem, descriptorPool);

	_ccHead->exitReadMutex(currentThread, fnName);

	if (result > 0) {
		if (descriptorPool != NULL) {
			pool_state state;
			J9SharedDataDescriptor* anElement;

			anElement = (J9SharedDataDescriptor*)pool_startDo((J9Pool*)descriptorPool, &state);
			while (anElement) {
				updateBytesRead(anElement->length);
				anElement = (J9SharedDataDescriptor*)pool_nextDo(&state);
			}
		} else if (firstItem != NULL) {
			updateBytesRead(firstItem->length);
		}
	}

	Trc_SHR_CM_findSharedData_Exit2(currentThread, result);
	return result;
}

/* Attempts to transfer some private shared data from another JVM to this one
 * The data field should be an value returned from findSharedData, not one made up manually.
 * If the data entry is private to another JVM and is not in use, it will be made private to this JVM and will be marked "in use".
 * Public data cannot be made private.
 * 
 * @param[in] currentThread  The current thread
 * @param[in] data  The data descriptor returned by findSharedData
 * 
 * @return  1 if successful or 0 if unsuccessful
 */
UDATA 
SH_CacheMap::acquirePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data)
{
	UDATA result = 0;
	const char* fnName = "acquirePrivateSharedData";
	SH_ByteDataManager* localBDM;

	if (!(localBDM = getByteDataManager(currentThread))) { 
		return 0;
	}
	if (_ccHead->enterWriteMutex(currentThread, false, fnName) != 0) {
		return 0;
	}
	result = localBDM->acquirePrivateEntry(currentThread, data);
	_ccHead->exitWriteMutex(currentThread, fnName);
	return result;	
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
UDATA 
SH_CacheMap::releasePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data)
{
	SH_ByteDataManager* localBDM;

	if (!(localBDM = getByteDataManager(currentThread))) { 
		return 0;
	}
	return localBDM->releasePrivateEntry(currentThread, data);
}

/* THREADING: Can be called at any time by any thread. Should not try to get any locks as it may be being
 * called as a result of a deadlock. The only locks obtained are by the managers when querying their hashtables.
 * 
 * Get the J9SharedClassJavacoreDataDescriptor for all layers of the cache.
 * 
 * @param[in] vm  The J9JavaVM
 * @param[out] descriptor The J9SharedClassJavacoreDataDescriptor
 * 
 * @return 1 on success and 0 otherwise
 * */
UDATA 
SH_CacheMap::getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor)
{
	return getJavacoreData(vm, descriptor, false);
}

/* THREADING: Can be called at any time by any thread. Should not try to get any locks as it may be being
 * called as a result of a deadlock. The only locks obtained are by the managers when querying their hashtables. 
 * 
 * Get the J9SharedClassJavacoreDataDescriptor for the current shared cache.
 * 
 * @param[in] vm  The J9JavaVM
 * @param[out] descriptor The J9SharedClassJavacoreDataDescriptor
 * @param[in] topLayerOnly  Whether J9SharedClassJavacoreDataDescriptor from the top layer cache only or all layers
 * 
 * @return 1 on success and 0 otherwise
 * */
UDATA 
SH_CacheMap::getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor, bool topLayerOnly)
{
	UDATA stale, nonstale;
	SH_CompositeCacheImpl* walk = _ccTail;

	if ((NULL != _ccHead) && !_ccHead->getJavacoreData(vm, descriptor)) {
		return 0;
	}
	if (topLayerOnly) {
		walk = _ccHead;
	}
	/*
	 * Turn off assertion on local mutex now and turn on assertion at the end of this method. 
	 * reference CMVC 145844
	 */
	_isAssertEnabled = false;
	while (walk) {
		descriptor->ccCount++;
		if (walk->isStarted()) {
			descriptor->ccStartedCount++;
			if (walk == _ccHead) {
				descriptor->topLayer = walk->getLayer();
				descriptor->freeBytes = walk->getFreeAvailableBytes();
			}
			descriptor->aotBytes += walk->getAOTBytes();
			descriptor->romClassBytes += ((UDATA)(walk->getSegmentAllocPtr()) - (UDATA)(walk->getBaseAddress()));
		}
		walk = walk->getPrevious();
	}
	
	descriptor->runtimeFlags = *_runtimeFlags;
	descriptor->cacheName = _cacheName;
	descriptor->feature = getJVMFeature(vm);

	if (_bdm && (_bdm->getState() == MANAGER_STATE_STARTED)) {
		UDATA type;
		
		descriptor->unindexedDataBytes = _bdm->getUnindexedDataBytes();

		descriptor->indexedDataBytes = 0;
		for (type = 0; type <= J9SHR_DATA_TYPE_MAX; type++) {
			switch (type) {
			case J9SHR_DATA_TYPE_JCL:
				descriptor->jclDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numJclEntries = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_ZIPCACHE:
				descriptor->zipCacheDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numZipCaches = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_JITHINT:
				descriptor->jitHintDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numJitHints = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_AOTHEADER:
				descriptor->aotDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numAotDataEntries = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_AOTCLASSCHAIN:
				descriptor->aotClassChainDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numAotClassChains = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_AOTTHUNK:
				descriptor->aotThunkDataBytes = _bdm->getDataBytesForType(type);
				descriptor->numAotThunks = _bdm->getNumOfType(type);
				break;
			case J9SHR_DATA_TYPE_STARTUP_HINTS:
				descriptor->numStartupHints = _bdm->getNumOfType(type);
				descriptor->startupHintBytes = _bdm->getDataBytesForType(type);
				break;
			default:
				descriptor->indexedDataBytes += _bdm->getDataBytesForType(type);
			}
		}
	} else {
		descriptor->indexedDataBytes = descriptor->unindexedDataBytes = 0; 
		descriptor->jclDataBytes = 0;
		descriptor->zipCacheDataBytes = 0;
		descriptor->jitHintDataBytes = 0;
		descriptor->jitProfileDataBytes = 0;
		descriptor->aotDataBytes = 0;
		descriptor->aotClassChainDataBytes = 0;
		descriptor->aotThunkDataBytes = 0;
		descriptor->startupHintBytes = 0;
		descriptor->numJclEntries = 0;
		descriptor->numZipCaches = 0;
		descriptor->numJitHints = 0;
		descriptor->numJitProfiles = 0;
		descriptor->numAotDataEntries = 0;
		descriptor->numAotClassChains = 0;
		descriptor->numAotThunks = 0;
		descriptor->numStartupHints = 0;
	}

	descriptor->objectBytes = 0;
	descriptor->numObjects = 0;
	
	if (_adm && (MANAGER_STATE_STARTED == _adm->getState())) {
		UDATA type;
		for (type = 0; type <= J9SHR_ATTACHED_DATA_TYPE_MAX; type++) {
			switch (type) {
			case J9SHR_ATTACHED_DATA_TYPE_UNKNOWN:
				break;
			case J9SHR_ATTACHED_DATA_TYPE_JITPROFILE:
				descriptor->jitProfileDataBytes += _adm->getDataBytesForType(type);
				descriptor->numJitProfiles += _adm->getNumOfType(type);
				break;
			case J9SHR_ATTACHED_DATA_TYPE_JITHINT:
				descriptor->jitHintDataBytes += _adm->getDataBytesForType(type);
				descriptor->numJitHints += _adm->getNumOfType(type);
				break;
			default:
				Trc_SHR_CM_getJavacoreData_InvalidAttachedDataType(type);
				Trc_SHR_Assert_ShouldNeverHappen();
				break;
			}
		}
	}

	descriptor->romClassBytes += descriptor->unindexedDataBytes;
	if ((0 >= descriptor->topLayer)
		|| (true == topLayerOnly)
	) {
		descriptor->otherBytes = descriptor->cacheSize - ((UDATA)descriptor->metadataStart - (UDATA)descriptor->romClassEnd) - descriptor->aotBytes -
					descriptor->romClassBytes - descriptor->readWriteBytes - 
					descriptor->zipCacheDataBytes -
					descriptor->startupHintBytes-
					descriptor->jclDataBytes -
					descriptor->jitHintDataBytes -
					descriptor->jitProfileDataBytes -
					descriptor->aotDataBytes -
					descriptor->aotClassChainDataBytes -
					descriptor->aotThunkDataBytes -
					descriptor->indexedDataBytes -
					descriptor->objectBytes -
					descriptor->debugAreaSize;
	} else {
		/* otherBytes does not make sense to multi-layer cache */
		descriptor->otherBytes = 0;
	}
	
	if (_rcm && (_rcm->getState() == MANAGER_STATE_STARTED)) {
		_rcm->getNumItems(NULL, &nonstale, &stale);
		descriptor->numStaleClasses = stale;
		descriptor->numROMClasses = stale + nonstale;
		if (descriptor->numROMClasses > 0) {
			descriptor->percStale = ((descriptor->numStaleClasses*100) / descriptor->numROMClasses);
		} else {
			descriptor->percStale = 0;
		}
	} else {
		descriptor->numROMClasses = descriptor->numStaleClasses = descriptor->percStale = 0;
	}
	
	if (_cmm && (_cmm->getState() == MANAGER_STATE_STARTED)) {
		_cmm->getNumItems(NULL, &nonstale, &stale);
		descriptor->numAOTMethods = stale + nonstale;
	} else {
		descriptor->numAOTMethods = 0;
	}
	if (_cpm && (_cpm->getState() == MANAGER_STATE_STARTED)) {
		_cpm->getNumItemsByType(&(descriptor->numClasspaths), &(descriptor->numURLs), &(descriptor->numTokens));
	}

	if ((U_32)-1 != descriptor->softMaxBytes) {
		/* cache header size is not included in descriptor->cacheSize, but it is included in softmx as used bytes. To be consistent, subtract cache header size here */ 
		UDATA softmxWithoutHeader = descriptor->softMaxBytes - (descriptor->totalSize - descriptor->cacheSize);
		
		if (softmxWithoutHeader > descriptor->freeBytes) {
    		descriptor->percFull = ((softmxWithoutHeader - descriptor->freeBytes) / (softmxWithoutHeader/100));
    	}
	} else {
		if (descriptor->cacheSize > descriptor->freeBytes) {
    		descriptor->percFull = ((descriptor->cacheSize - descriptor->freeBytes) / (descriptor->cacheSize/100));
		}
	}

	/* turn on assertion on local mutex again. reference CMVC 145844 */
	_isAssertEnabled = true;

	return 1;
}

/* THREADING: This function can be called multi-threaded, but protects itself with cache write mutex and full cache lock
 * Since no other threads will be reading or writing once a lock is obtained, the cache header is guaranteed
 * not to change, therefore there is no need to update updateCount during this function.
 * Note that this function may be called more than once on same ClasspathEntryItem
 * because we don't hold mutex forever on a find. This is OK though as we set a flag
 * to prevent stale marking happening twice. 
 */
IDATA 
SH_CacheMap::markStale(J9VMThread* currentThread, ClasspathEntryItem* cpei, bool hasWriteMutex) 
{
	UDATA unused = 0;
	ShcItem* it = NULL;
	IDATA retryCount = 0;
	U_16 cpeiPathLen = 0;
	const char* cpeiPath = cpei->getLocation(&cpeiPathLen);
	UDATA oldState = currentThread->omrVMThread->vmState;
	IDATA returnVal = 0;
	const char* fnName = "markStale";
	UDATA numMarked = 0;
	SH_ClasspathManager* localCPM;
	PORT_ACCESS_FROM_PORT(_portlib);
	
	if ((_ccHead->isRunningReadOnly()) 
		|| (false == isAddressInCache(cpei, 0, false, true))
	) {
		return 0;
	}
	if (!(localCPM = getClasspathManager(currentThread))) { 
		return -1;
	}

	Trc_SHR_CM_markStale_Entry(currentThread, cpeiPathLen, cpeiPath, hasWriteMutex);

	if ((cpei->flags & MARKED_STALE_FLAG) > 0) {
		Trc_SHR_CM_markStale_Event_AlreadyMarked(currentThread, cpeiPathLen, cpeiPath);
		returnVal = 0;
		goto _done;
	}
	if (hasWriteMutex) {
		_ccHead->doLockCache(currentThread);		/* Wait till all readers stop and unprotect metadata area */
	}

	currentThread->omrVMThread->vmState = J9VMSTATE_SHAREDCLASS_MARKSTALE;
	while (retryCount < MARK_STALE_RETRY_TIMES) {
		if (hasWriteMutex || (_ccHead->enterWriteMutex(currentThread, true,fnName)==0)) {	/* true = lockCache */
			if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
				if (!hasWriteMutex) {
					_ccHead->exitWriteMutex(currentThread, fnName);		/* Will unlock cache */
				}
				Trc_SHR_CM_markStale_Exception1(currentThread);
				returnVal = -1;
				goto _done;
			}

			Trc_SHR_CM_markStale_Event_DoingMark(currentThread, cpeiPathLen, cpeiPath, cpei->timestamp);

			/* Note that this doesn't completely make sense for linked supercaches */
			_ccHead->startCriticalUpdate(currentThread);		/* Un-protects cache header */
			_ccHead->findStart(currentThread);

			cpei->flags |= MARKED_STALE_FLAG;
			localCPM->markClasspathsStale(currentThread, cpei);

			do {
				it = (ShcItem*)_ccHead->nextEntry(currentThread, &unused);		/* should ignore stale items */
				if ((it) && (ITEMTYPE(it) == TYPE_ROMCLASS)) {
					ROMClassWrapper* rcw = (ROMClassWrapper*)ITEMDATA(it);
	
					if (((ClasspathWrapper*)getAddressFromJ9ShrOffset(&(rcw->theCpOffset)))->staleFromIndex <= rcw->cpeIndex) {
						markItemStale(currentThread, it, true);
						++numMarked;
					}
				}
			} while (it);
			_cc->endCriticalUpdate(currentThread);	/* Re-protects cache header */

			CACHEMAP_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE, J9NLS_INFO, J9NLS_SHRC_CM_MARK_STALE, cpeiPathLen, cpeiPath, numMarked);

			if (!hasWriteMutex) {
				_ccHead->exitWriteMutex(currentThread, fnName);
			} else {
				_ccHead->doUnlockCache(currentThread); /* Re-protects metadata area */
			}
			break;
		}
	}
	if (retryCount == MARK_STALE_RETRY_TIMES) {
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_MARKSTALE_ATTEMPTS);
		Trc_SHR_CM_markStale_Exception2(currentThread, cpeiPathLen, cpeiPath);
		returnVal = -1;
		goto _done;
	}

	Trc_SHR_CM_markStale_Exit(currentThread, cpeiPathLen, cpeiPath, returnVal);

_done:
	currentThread->omrVMThread->vmState = oldState;
	return returnVal;
}

/* THREADING: This must always be called with write mutex */
void 
SH_CacheMap::markItemStale(J9VMThread* currentThread, const ShcItem* item, bool isCacheLocked)
{
	if ((_ccHead->isRunningReadOnly())
		|| (false == isAddressInCache(item, 0, false, true))
	) {
		return;
	}

	Trc_SHR_CM_markItemStale_Entry(currentThread, item);
	Trc_SHR_Assert_True(_ccHead->hasWriteMutex(currentThread));

	_ccHead->markStale(currentThread, (BlockPtr)ITEMEND(item), isCacheLocked);

	Trc_SHR_CM_markItemStale_Exit(currentThread, item);
} 

/* THREADING: This must always be called with write mutex OR read mutex held. If the
 * read mutex is held, it is released.
 */
void 
SH_CacheMap::markItemStaleCheckMutex(J9VMThread* currentThread, const ShcItem* item, bool isCacheLocked)
{
	const char *fnName = "markItemStaleCheckMutex";

	if ((_ccHead->isRunningReadOnly())
		|| (false == isAddressInCache(item, 0, false, true))
	) {
		return;
	}

	Trc_SHR_CM_markItemStaleCheckMutex_Entry(currentThread, item);

	if (_ccHead->hasWriteMutex(currentThread)) {
		if (!isCacheLocked) {
			_ccHead->doLockCache(currentThread);		/* Wait till all readers stop and unprotect metadata area */
		}
		_ccHead->markStale(currentThread, (BlockPtr)ITEMEND(item), true);
	} else {
		_ccHead->exitReadMutex(currentThread, fnName);
		if (_ccHead->enterWriteMutex(currentThread, true, fnName) == 0) {
			_ccHead->markStale(currentThread, (BlockPtr)ITEMEND(item), true);
			_ccHead->exitWriteMutex(currentThread, fnName);
		} else {
			Trc_SHR_CM_markItemStaleCheckMutex_Failed(currentThread, item);
		}
	}

	Trc_SHR_CM_markItemStaleCheckMutex_Exit(currentThread, item);
} 

/* THREADING: Only called single-threaded */
void 
SH_CacheMap::destroy(J9VMThread* currentThread)
{
	Trc_SHR_CM_destroy_Entry(currentThread);

	if (_ccHead->enterWriteMutex(currentThread, true, "destroy")==0) {
		resetAllManagers(currentThread);
		_ccHead->deleteCache(currentThread, false);
		/* No need to exit mutex as it has been destroyed. */
	}

	Trc_SHR_CM_destroy_Exit(currentThread);
}

/* THREADING: Can be called multi-threaded */
void
SH_CacheMap::reportCorruptCache(J9VMThread* currentThread, SH_CompositeCacheImpl* ccToUse)
{
	bool hasRefreshMutex = false;
	bool enteredRefreshMutex = false;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_reportCorruptCache_Entry(currentThread);
	
	hasRefreshMutex = (1 == omrthread_monitor_owned_by_self(_refreshMutex));
	if (!hasRefreshMutex) {
		enteredRefreshMutex = (0 == enterRefreshMutex(currentThread, "reportCorruptCache"));
	}

	if (hasRefreshMutex || enteredRefreshMutex) {
		if (!_cacheCorruptReported) {
			IDATA corruptionCode;
			UDATA corruptValue;
			ccToUse->getCorruptionContext(&corruptionCode, &corruptValue);
			Trc_SHR_Assert_True(NO_CORRUPTION != corruptionCode);
			CACHEMAP_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_WARN_CORRUPT_CACHE_V2, _cacheName, corruptionCode, corruptValue);
			/* Reset the writeHash field in the cache as no more updates can occur */
			if (J9_ARE_ANY_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION) && (false == ccToUse->isRunningReadOnly())) {
				ccToUse->setWriteHash(currentThread, 0);
			}
			_cacheCorruptReported = true;
			*_runtimeFlags |= (J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS | J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES);
		}
		if (enteredRefreshMutex) {
			exitRefreshMutex(currentThread, "reportCorruptCache");
		}
	}

	Trc_SHR_CM_reportCorruptCache_Exit(currentThread);
}

/* THREADING: Can be called multi-threaded */
void
SH_CacheMap::resetCorruptState(J9VMThread* currentThread, UDATA hasRefreshMutex)
{
	Trc_SHR_CM_resetCorruptState_Entry(currentThread);
	
	if (hasRefreshMutex || enterRefreshMutex(currentThread, "resetCorruptState")==0) {
		if (_cacheCorruptReported) {
			_cacheCorruptReported = false;
			*_runtimeFlags &= ~(J9SHR_RUNTIMEFLAG_DENY_CACHE_ACCESS | J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES);
		}
		if (!hasRefreshMutex) {
			exitRefreshMutex(currentThread, "resetCorruptState");
		}
		_ccHead->setCorruptionContext(NO_CORRUPTION, 0);
	}

	Trc_SHR_CM_resetCorruptState_Exit(currentThread);
}

/* THREADING: Only ever single-threaded */
IDATA
SH_CacheMap::printAllCacheStats(J9VMThread* currentThread, UDATA showFlags, SH_CompositeCacheImpl* cache, U_32* staleBytes)
{
	const char* fnName = "printAllCacheStats";
	J9InternalVMFunctions *vmFunctions = currentThread->javaVM->internalVMFunctions; 
	ClasspathItem* cpi = NULL;
	ClasspathWrapper* cpw = NULL;
	ShcItem* it;
	bool showAllStaleFlag = J9_ARE_ALL_BITS_SET(showFlags, PRINTSTATS_SHOW_ALL_STALE);
	bool isStale = false;
	PORT_ACCESS_FROM_PORT(_portlib);

	if (cache->enterWriteMutex(currentThread, false, fnName) != 0) {
		return -1;
	}
	
	/* TODO: Can't handle multiple supercaches - for offline deployment, this isn't a problem as we only ever stat the serialized cache */
	cache->findStart(currentThread);
	do {
		it = (ShcItem*)cache->nextEntry(currentThread, NULL);		/* Will not skip over stale items */
		if (it) {
			ShcItemHdr* ih = (ShcItemHdr*)ITEMEND(it);

			isStale = (0 != cache->stale((BlockPtr)ih));
			if (isStale) {
				*staleBytes += CCITEMLEN(ih);
			}
			switch (ITEMTYPE(it)) {
			case TYPE_ORPHAN : 
				if (showFlags & PRINTSTATS_SHOW_ORPHAN) {
					J9ROMClass* romClass = (J9ROMClass*)getAddressFromJ9ShrOffset(&(((OrphanWrapper*)ITEMDATA(it))->romClassOffset));
					J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(romClass);
					CACHEMAP_PRINT5(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ORPHAN_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), romClass);
				}
				break;

			case TYPE_BYTE_DATA :
			{
				ByteDataWrapper* bdw = (ByteDataWrapper*)ITEMDATA(it);
				J9UTF8* pointer =  (J9UTF8*)getAddressFromJ9ShrOffset(&(bdw->tokenOffset));
				UDATA type = (UDATA)BDWTYPE(bdw);

				if (J9SHR_DATA_TYPE_ZIPCACHE == type) {
					if ((PRINTSTATS_SHOW_ZIPCACHE == (showFlags & PRINTSTATS_SHOW_ZIPCACHE))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_ZIPCACHE_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_JITHINT == type) {
					if ((PRINTSTATS_SHOW_JITHINT == (showFlags & PRINTSTATS_SHOW_JITHINT))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_JITHINT_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_JCL == type) {
					if ((PRINTSTATS_SHOW_JCL == (showFlags & PRINTSTATS_SHOW_JCL))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_JCL_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_AOTCLASSCHAIN == type) {
					if ((PRINTSTATS_SHOW_AOTCH == (showFlags & PRINTSTATS_SHOW_AOTCH))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_AOTCH_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_AOTTHUNK == type) {
					if ((PRINTSTATS_SHOW_AOTTHUNK == (showFlags & PRINTSTATS_SHOW_AOTTHUNK))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_AOTTHUNK_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_AOTHEADER == type) {
					if ((PRINTSTATS_SHOW_AOTDATA == (showFlags & PRINTSTATS_SHOW_AOTDATA))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_AOTDATA_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_HELPER == type) {
					if ((PRINTSTATS_SHOW_BYTEDATA == (showFlags & PRINTSTATS_SHOW_BYTEDATA))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_HELPER_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_POOL == type) {
					if ((PRINTSTATS_SHOW_BYTEDATA == (showFlags & PRINTSTATS_SHOW_BYTEDATA))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_POOL_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_VM == type) {
					if ((PRINTSTATS_SHOW_BYTEDATA == (showFlags & PRINTSTATS_SHOW_BYTEDATA))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_VM_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_ROMSTRING == type) {
					if ((PRINTSTATS_SHOW_BYTEDATA == (showFlags & PRINTSTATS_SHOW_BYTEDATA))
						|| (isStale && showAllStaleFlag)
					) {
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_ROMSTRING_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
					}
				} else if (J9SHR_DATA_TYPE_STARTUP_HINTS == type) {
					if ((J9_ARE_ANY_BITS_SET(showFlags, PRINTSTATS_SHOW_STARTUPHINT))
						|| (isStale && showAllStaleFlag)
					) {
						J9SharedStartupHintsDataDescriptor* hints = (J9SharedStartupHintsDataDescriptor*)getDataFromByteDataWrapper(bdw);
						CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_STARTUP_HINTS_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
						CACHEMAP_PRINT3((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_STARTUP_HINTS_DISPLAY_DETAIL, hints->flags, hints->heapSize1, hints->heapSize2);
						if (isStale) {
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
						j9tty_printf(_portlib, "\n");
					}
				} else if ((PRINTSTATS_SHOW_BYTEDATA == (showFlags & PRINTSTATS_SHOW_BYTEDATA))
					|| (isStale && showAllStaleFlag)
				) {
					CACHEMAP_PRINT6((J9NLS_DO_NOT_PRINT_MESSAGE_TAG), J9NLS_SHRC_CM_PRINTSTATS_UNKNOWN_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(pointer), J9UTF8_DATA(pointer), getDataFromByteDataWrapper(bdw), BDWLEN(bdw));
					if (isStale) {
						CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
					}
				}
			}
				break;

			case TYPE_ROMCLASS : 
			case TYPE_SCOPED_ROMCLASS : 
			{
				ROMClassWrapper* rcw = (ROMClassWrapper*)ITEMDATA(it);
				J9ROMClass* romClass = (J9ROMClass*)getAddressFromJ9ShrOffset(&(rcw->romClassOffset));

				if (isStale) {
					if (NULL != romClass) {
						*staleBytes += romClass->romSize;
					}
				}

				if ((showFlags & PRINTSTATS_SHOW_ROMCLASS)
					|| (showFlags & PRINTSTATS_SHOW_ROMMETHOD)
					|| (showAllStaleFlag && isStale)
				) {
					J9UTF8* romClassName = J9ROMCLASS_CLASSNAME(romClass);
					cpw = (ClasspathWrapper*)getAddressFromJ9ShrOffset(&(((ROMClassWrapper*)ITEMDATA(it))->theCpOffset));
					U_32 i;
					
					cpi = (ClasspathItem*)CPWDATA(cpw);
					CACHEMAP_PRINT5((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), 
							J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), getAddressFromJ9ShrOffset(&(rcw->romClassOffset)));
					if (isStale) {
						CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
					}
					j9tty_printf(_portlib, "\n");
					if (cpi->getType()==CP_TYPE_CLASSPATH) {
						CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_INDEX_IN_CP, rcw->cpeIndex, (UDATA)cpw);
					} else 
					if (cpi->getType()==CP_TYPE_URL) {
						CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_URL_DISPLAY, (UDATA)cpw);
					} else 
					if (cpi->getType()==CP_TYPE_TOKEN) {
						CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_TOKEN_DISPLAY, (UDATA)cpw);
					}
					if (ITEMTYPE(it)==TYPE_SCOPED_ROMCLASS) {
						ScopedROMClassWrapper* srcw = (ScopedROMClassWrapper*)ITEMDATA(it);
						const J9UTF8* partition = (const J9UTF8*)getAddressFromJ9ShrOffset(&(srcw->partitionOffset));
						const J9UTF8* modContext = (const J9UTF8*)getAddressFromJ9ShrOffset(&(srcw->modContextOffset));

						if ((0 != srcw->partitionOffset.offset) && (0 == srcw->modContextOffset.offset)) {
							CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_PARTITION_DISPLAY, J9UTF8_LENGTH(partition), J9UTF8_DATA(partition));
						} else
						if ((0 != srcw->modContextOffset.offset) && (0 == srcw->partitionOffset.offset)) {
							CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_MODCONTEXT_DISPLAY, J9UTF8_LENGTH(modContext), J9UTF8_DATA(modContext));
						} else
						if ((0 != srcw->modContextOffset.offset) && (0 != srcw->partitionOffset.offset)) {
							CACHEMAP_PRINT4(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMCLASS_PARTITIONINMOD_DISPLAY, J9UTF8_LENGTH(partition), J9UTF8_DATA(partition), J9UTF8_LENGTH(modContext), J9UTF8_DATA(modContext));
						}
					}

					if (showFlags & PRINTSTATS_SHOW_ROMMETHOD) {
						J9ROMMethod *romMethod;
						romMethod = J9ROMCLASS_ROMMETHODS(romClass);
						for (i = 0; i < romClass->romMethodCount; i++) {
							J9UTF8* romMethodName;
							J9UTF8* romMethodSig;

							if (!romMethod) {
								break;
							}
						
							romMethodName = J9ROMMETHOD_NAME(romMethod);
							romMethodSig = J9ROMMETHOD_SIGNATURE(romMethod);
							if (romMethodName && romMethodSig) {
								CACHEMAP_PRINT5(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ROMMETHOD_DISPLAY,
										J9UTF8_LENGTH(romMethodName), J9UTF8_DATA(romMethodName), J9UTF8_LENGTH(romMethodSig), J9UTF8_DATA(romMethodSig), romMethod);
							}

							romMethod = nextROMMethod(romMethod);
						}
					}
				}
				break;
			}	

			case TYPE_CLASSPATH :
				cpw = (ClasspathWrapper*)ITEMDATA(it);
				cpi = (ClasspathItem*)CPWDATA(cpw);
				if (((showFlags & PRINTSTATS_SHOW_CLASSPATH) && (cpi->getType() == CP_TYPE_CLASSPATH)) ||
						((showFlags & PRINTSTATS_SHOW_URL) && (cpi->getType() == CP_TYPE_URL)) ||
						((showFlags & PRINTSTATS_SHOW_TOKEN) && (cpi->getType() == CP_TYPE_TOKEN))) {

					if (cpi->getType() == CP_TYPE_CLASSPATH) {
						CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CLASSPATH_DISPLAY, ITEMJVMID(it), (UDATA)cpw);
					} else if (cpi->getType() == CP_TYPE_URL) {
						CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CLASSPATH_URL_DISPLAY, ITEMJVMID(it), (UDATA)cpw);
					} else if (cpi->getType() == CP_TYPE_TOKEN) {
						CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CLASSPATH_TOKEN_DISPLAY, ITEMJVMID(it), (UDATA)cpw);
					}

					for (I_16 i=0; i<cpi->getItemsAdded(); i++) {
						U_16 cpeiPathLen = 0;
						const char* cpeiPath = cpi->itemAt(i)->getPath(&cpeiPathLen);
						if (i==(cpi->getItemsAdded()-1)) {
							CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CLASSPATH_DISPLAYLASTITEM, cpeiPathLen, cpeiPath);
						} else {
							CACHEMAP_PRINT2(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CLASSPATH_DISPLAYITEM, cpeiPathLen, cpeiPath);
						}
					}
				}
				break;

			case TYPE_INVALIDATED_COMPILED_METHOD :
			case TYPE_COMPILED_METHOD :
				if ((J9_ARE_ALL_BITS_SET(showFlags, PRINTSTATS_SHOW_AOT) && (TYPE_COMPILED_METHOD == ITEMTYPE(it)))
					|| (J9_ARE_ALL_BITS_SET(showFlags, PRINTSTATS_SHOW_INVALIDATEDAOT) && (TYPE_INVALIDATED_COMPILED_METHOD == ITEMTYPE(it)))
					|| (showAllStaleFlag && isStale)
				) {
					CompiledMethodWrapper* cmw = (CompiledMethodWrapper*)ITEMDATA(it);
					J9ROMMethod* romMethod = (J9ROMMethod*)getAddressFromJ9ShrOffset(&(cmw->romMethodOffset));
					J9ClassLoader* loader;
					J9UTF8* romClassName = NULL;
					J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)romMethod, &loader);
 					J9UTF8* romMethodName = J9ROMMETHOD_NAME(romMethod);
					J9UTF8* romMethodSig = J9ROMMETHOD_SIGNATURE(romMethod);
					
 					if (romClass) {
						romClassName = J9ROMCLASS_CLASSNAME(romClass);
 					}

 					if (romMethodName) {
						CACHEMAP_PRINT4((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
								J9NLS_SHRC_CM_PRINTSTATS_AOT_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(romMethodName), J9UTF8_DATA(romMethodName));
 					}

					if (romMethodSig) {
						CACHEMAP_PRINT3((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
								J9NLS_SHRC_CM_PRINTSTATS_AOT_DISPLAY_SIG_ADDR, J9UTF8_LENGTH(romMethodSig), J9UTF8_DATA(romMethodSig), romMethod);
					}

					if (isStale) {
						j9tty_printf(_portlib, " ");
						CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
					}
					if (TYPE_INVALIDATED_COMPILED_METHOD == ITEMTYPE(it)) {
						j9tty_printf(_portlib, " ");
						CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_INVALIDATED);
					}
					j9tty_printf(_portlib, "\n");
					if (romClassName) {
						CACHEMAP_PRINT3(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, 
								J9NLS_SHRC_CM_PRINTSTATS_AOT_FROM_ROMCLASS, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), romClass);
					}
				}
				break;

			case TYPE_ATTACHED_DATA :
				if (0 != (showFlags & (PRINTSTATS_SHOW_JITPROFILE | PRINTSTATS_SHOW_JITHINT))
					|| (showAllStaleFlag && isStale)
				) {
					AttachedDataWrapper* adw = (AttachedDataWrapper*)ITEMDATA(it);
					
					if ((J9SHR_ATTACHED_DATA_TYPE_JITPROFILE == ADWTYPE(adw))
							|| (J9SHR_ATTACHED_DATA_TYPE_JITHINT == ADWTYPE(adw))) {
						J9ROMMethod* romMethod = (J9ROMMethod*)getAddressFromJ9ShrOffset(&(((AttachedDataWrapper*)ITEMDATA(it))->cacheOffset));
						J9ClassLoader* loader;
						J9UTF8* romClassName = NULL;
						J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)romMethod, &loader);
						J9UTF8* romMethodName = J9ROMMETHOD_NAME(romMethod);
						J9UTF8* romMethodSig = J9ROMMETHOD_SIGNATURE(romMethod);

						if (romClass) {
							romClassName = J9ROMCLASS_CLASSNAME(romClass);
						}

						if (romMethodName) {

							if ((0 != (showFlags & PRINTSTATS_SHOW_JITHINT)) && (J9SHR_ATTACHED_DATA_TYPE_JITHINT == ADWTYPE(adw))) {
								CACHEMAP_PRINT4((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
										J9NLS_SHRC_CM_PRINTSTATS_ATTACHED_JITHINT_DISPLAY, ITEMJVMID(it), 
										(UDATA)it, J9UTF8_LENGTH(romMethodName), J9UTF8_DATA(romMethodName));
							} else if ((0 != (showFlags & PRINTSTATS_SHOW_JITPROFILE)) && (J9SHR_ATTACHED_DATA_TYPE_JITPROFILE == ADWTYPE(adw))){
								CACHEMAP_PRINT4((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
										J9NLS_SHRC_CM_PRINTSTATS_JITPROFILE_DISPLAY, ITEMJVMID(it), 
										(UDATA)it, J9UTF8_LENGTH(romMethodName), J9UTF8_DATA(romMethodName));
							}
						}

						if (romMethodSig) {
							CACHEMAP_PRINT3((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
									J9NLS_SHRC_CM_PRINTSTATS_JITPROFILE_DISPLAY_SIG_ADDR, J9UTF8_LENGTH(romMethodSig), J9UTF8_DATA(romMethodSig), romMethod);
						}

						if (isStale) {
							j9tty_printf(_portlib, " ");
							CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
						}
						j9tty_printf(_portlib, "\n");
						if (romClassName) {
							CACHEMAP_PRINT3(J9NLS_DO_NOT_PRINT_MESSAGE_TAG,
									J9NLS_SHRC_CM_PRINTSTATS_JITPROFILE_FROM_ROMCLASS, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), romClass);
						}
					}
				}
				break;
			default :
				break;
			}
		}
	} while (it); 

	cache->exitWriteMutex(currentThread, fnName);
	return 0;
}

/*
 * Helper funtion to print the statistics of the top layer cache.
 * 
 * @param[in] currentThread  The current thread
 * @param[in] showFlags  Flags controlling information printed
 * @param[in] runtimeFlags  The runtime flags
 * @param[in] javacoreData  Pointer to J9SharedClassJavacoreDataDescriptor
 * @param[in] multiLayerStats  Whether J9SharedClassJavacoreDataDescriptor is from a multi-layer cache
 * */
void
SH_CacheMap::printCacheStatsTopLayerStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData, bool multiLayerStats)
{
	PORT_ACCESS_FROM_PORT(_portlib);
#if !defined(WIN32)
	if (javacoreData->shmid >= 0) {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SHMID, javacoreData->shmid);
	}
#endif

	CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_CREATED_WITH);
		
	j9tty_printf(_portlib, "\t");
	if (true == this->_ccHead->getIsNoLineNumberEnabled()) {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_XNOLINENUMERS_ENABLED_TRUE);
	} else {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_XNOLINENUMERS_ENABLED_FALSE);
	}
	j9tty_printf(_portlib, "\t");
	if (true == this->_ccHead->getIsBCIEnabled()) {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_BCI_ENABLED_TRUE);
	} else {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_BCI_ENABLED_FALSE);
	}
	j9tty_printf(_portlib, "\t");
	if (true == this->_ccHead->isRestrictClasspathsSet(currentThread)) {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_RESTRICT_CLASSPATHS_TRUE);
	} else {
		CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_RESTRICT_CLASSPATHS_FALSE);
	}

	j9tty_printf(_portlib, "\t");
	if (J9_ARE_ALL_BITS_SET(javacoreData->feature, J9SH_FEATURE_COMPRESSED_POINTERS)) {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_FEATURE, "cr");
	} else if (J9_ARE_ALL_BITS_SET(javacoreData->feature, J9SH_FEATURE_NON_COMPRESSED_POINTERS)) {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_FEATURE, "non-cr");
	} else {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_FEATURE, "default");
	}

#if (defined(J9VM_ARCH_X86) || defined(J9VM_ARCH_S390) || defined(J9VM_ARCH_POWER))
	if (currentThread->javaVM->jitConfig) {
		j9tty_printf(_portlib, "\t");
		J9SharedDataDescriptor firstDescriptor;
		firstDescriptor.address = NULL;
		findSharedData(currentThread, "J9AOTHeader", sizeof("J9AOTHeader") - 1, J9SHR_DATA_TYPE_AOTHEADER, FALSE, &firstDescriptor, NULL);
		const size_t BUFF_SIZE = 800;
		char processorFeatures[BUFF_SIZE];
		currentThread->javaVM->jitConfig->printAOTHeaderProcessorFeatures((TR_AOTHeader *)firstDescriptor.address, processorFeatures, BUFF_SIZE);
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_PROCESSOR_FEATURES, processorFeatures);
	}
#endif /* defined(J9VM_ARCH_X86) || defined(J9VM_ARCH_S390) || defined(J9VM_ARCH_POWER) */

	j9tty_printf(_portlib, "\n");
	if (true == this->_ccHead->getIsNoLineNumberContentEnabled()) {
		if (true == this->_ccHead->getIsLineNumberContentEnabled()) {
			CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_CONTAINS_CONTENT_WITH_AND_WITHOUT_LINE_NUMBERS);
		} else {
			CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_CONTAINS_CONTENT_ONLY_WITHOUT_LINE_NUMBERS);
		}
	} else {
		if (true == this->_ccHead->getIsLineNumberContentEnabled()) {
			CACHEMAP_PRINT(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_CONTAINS_CONTENT_ONLY_WITH_LINE_NUMBERS);
		}
	}

	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_BASEADDRESS_V2, javacoreData->romClassStart);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_ENDADDRESS_V2, javacoreData->cacheEndAddress);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_ALLOCPTR_V2, javacoreData->romClassEnd);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_METADATA_STARTADDRESS, javacoreData->metadataStart);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_RUNTIME_FLAGS, javacoreData->runtimeFlags);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_GEN, javacoreData->cacheGen);
	}

	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_CACHE_LAYER, javacoreData->topLayer);

	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_CACHESIZE_V2, javacoreData->cacheSize);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_SOFTMXBYTES, javacoreData->softMaxBytes);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_FREEBYTES_V2, javacoreData->freeBytes);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_MIN, javacoreData->minAOT);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_MAX, javacoreData->maxAOT);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JIT_MIN, javacoreData->minJIT);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JIT_MAX, javacoreData->maxJIT);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_READWRITE_BYTES, javacoreData->readWriteBytes);
	}
	if (!multiLayerStats) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_META_BYTES_V2, javacoreData->otherBytes);
		if ((U_32)-1 == javacoreData->softMaxBytes) {
			/* similarly to the calculation of cache full percentage, take used debug area into account */
			CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_META_PERCENT_V2, ((javacoreData->otherBytes * 100) / (javacoreData->cacheSize - javacoreData->freeBytes)));
		} else {
			/* cache header size is not included in javacoreData->cacheSize, but it is included in softmx as used bytes. To be consistent, subtract cache header size here */ 
			CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_META_PERCENT_V2, ((javacoreData->otherBytes * 100) / (javacoreData->softMaxBytes - (javacoreData->totalSize - javacoreData->cacheSize)  /* subtract header size */
																																										- javacoreData->freeBytes)));
		}
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_DEBUGAREA_SIZE, javacoreData->debugAreaSize);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_DEBUGAREA_LINENUMBERTABLE_BYTES, javacoreData->debugAreaLineNumberTableBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_DEBUGAREA_LOCALVARIABLETABLE_BYTES_V2, javacoreData->debugAreaLocalVariableTableBytes);
	} else {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_DEBUGAREA_USED_BYTES, javacoreData->debugAreaLineNumberTableBytes + javacoreData->debugAreaLocalVariableTableBytes);
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_DEBUGAREA_USED, javacoreData->debugAreaUsed);
}
/*
 * Helper funtion to print the statistics summary of the top layer cache.
 * 
 * @param[in] currentThread  the current thread
 * @param[in] showFlags Flags controlling information printed
 * @param[in] runtimeFlags  The runtime flags
 * @param[in] javacoreData  Pointer to J9SharedClassJavacoreDataDescriptor
 * */
void
SH_CacheMap::printCacheStatsTopLayerSummaryStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData)
{
	PORT_ACCESS_FROM_PORT(_portlib);
	j9tty_printf(_portlib, "\n");
	const char *accessString = NULL;

	if (javacoreData->cacheSize == javacoreData->softMaxBytes) {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_PERC_FULL, javacoreData->percFull);
	} else {
		CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_PERC_SOFT_FULL, javacoreData->percFull);
	}
	switch (this->_ccHead->isCacheAccessible()) {
	case J9SH_CACHE_ACCESS_ALLOWED:
		accessString = "true";
		break;
	case J9SH_CACHE_ACCESS_ALLOWED_WITH_GROUPACCESS:
		accessString = "only with 'groupAccess' option";
		break;
	case J9SH_CACHE_ACCESS_ALLOWED_WITH_GROUPACCESS_READONLY:
		accessString = "only with 'groupAccess' and 'readonly' option";
		break;
	case J9SH_CACHE_ACCESS_NOT_ALLOWED:
		accessString = "false";
		break;
	default:
		accessString = "false";
		break;
	}	
	CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_IS_CACHE_ACCESSIBLE, accessString);
}

/*
 * Helper funtion to print the cache statistics of all layers.
 * 
 * @param[in] currentThread  the current thread
 * @param[in] showFlags Flags controlling information printed
 * @param[in] runtimeFlags  The runtime flags
 * @param[in] javacoreData  Pointer to J9SharedClassJavacoreDataDescriptor
 * @param[in] staleBytes  The stale bytes in the cache
 * */
void
SH_CacheMap::printCacheStatsAllLayersStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData, U_32 staleBytes)
{
	PORT_ACCESS_FROM_PORT(_portlib);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_ROMCLASS_BYTES_V2, javacoreData->romClassBytes);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_CODE_BYTES, javacoreData->aotBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_DATA_BYTES, javacoreData->aotDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_CLASS_HIERARCHY_BYTES, javacoreData->aotClassChainDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_THUNK_BYTES, javacoreData->aotThunkDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JIT_HINT_BYTES, javacoreData->jitHintDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JIT_PROFILE_BYTES, javacoreData->jitProfileDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JAVA_OBJECT_BYTES, javacoreData->objectBytes);
	} else {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_AOT_BYTES_V2, javacoreData->aotBytes + javacoreData->aotDataBytes + javacoreData->aotClassChainDataBytes + javacoreData->aotThunkDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JIT_DATA_BYTES_V2, javacoreData->jitHintDataBytes + javacoreData->jitProfileDataBytes);
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_ZIP_CACHE_DATA_BYTES_V2, javacoreData->zipCacheDataBytes);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_STARTUP_HINT_BYTES, javacoreData->startupHintBytes);

	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_JCL_DATA_BYTES, javacoreData->jclDataBytes);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_BYTE_DATA_BYTES, javacoreData->indexedDataBytes);
	} else {
		UDATA dataBytes = javacoreData->indexedDataBytes +
							javacoreData->readWriteBytes +
							javacoreData->jclDataBytes +
							javacoreData->objectBytes;
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_DATA_BYTES_V2, dataBytes);
		
	}
	if ((0 != showFlags) 
		&& (PRINTSTATS_SHOW_TOP_LAYER_ONLY != showFlags)
	) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_STALE_BYTES, staleBytes);
	}
	j9tty_printf(_portlib, "\n");
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_ROMCLASSES_V2, javacoreData->numROMClasses);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_AOT_DATA, javacoreData->numAotDataEntries);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_AOT_CLASS_HIERARCHY, javacoreData->numAotClassChains);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_AOT_THUNKS, javacoreData->numAotThunks);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_JIT_HINTS, javacoreData->numJitHints);
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_JIT_PROFILES, javacoreData->numJitProfiles);
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_AOT_V2, javacoreData->numAOTMethods);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_CLASSPATHS_V2, javacoreData->numClasspaths);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_URLS_V2, javacoreData->numURLs);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_TOKENS_V2, javacoreData->numTokens);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_JAVA_OBJECTS, javacoreData->numObjects);
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_ZIP_CACHES_V2, javacoreData->numZipCaches);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_STARTUP_HINTS, javacoreData->numStartupHints);
	if (J9_ARE_ALL_BITS_SET(runtimeFlags, J9SHR_RUNTIMEFLAG_ENABLE_DETAILED_STATS)) {
		CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_JCL_ENTRIES, javacoreData->numJclEntries);
	}
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_NUM_STALE_CLASSES_V2, javacoreData->numStaleClasses);
	CACHEMAP_FMTPRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_SUMMARY_PERC_STALE_CLASSES_V2, javacoreData->percStale);
}

/**
 * Print stats on an existing cache
 * 
 * Parameters:
 * @param[in] currentThread  The current thread
 * @param[in] showFlags Flags controlling information printed
 *
 * @return -1 if cache is new (stats are not printed), 0 otherwise
 * 
 * THREADING: Only ever single-threaded
 */				   
IDATA
SH_CacheMap::printCacheStats(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags)
{	
	J9SharedClassJavacoreDataDescriptor javacoreData;
	U_32 staleBytes = 0;
	bool multiLayerStats = J9_ARE_NO_BITS_SET(showFlags, PRINTSTATS_SHOW_TOP_LAYER_ONLY);
	PORT_ACCESS_FROM_PORT(_portlib);

	if (0 != showFlags) {
		SH_CompositeCacheImpl* cache = _ccTail;
		if (J9_ARE_ALL_BITS_SET(showFlags, PRINTSTATS_SHOW_TOP_LAYER_ONLY)) {
			cache = _ccHead;
		}
		
		while (cache) {
			if (printAllCacheStats(currentThread, showFlags, cache, &staleBytes) == -1) {
				Trc_SHR_Assert_ShouldNeverHappen();
				return -1;
			}
			cache = cache->getPrevious();
		}
	}

	memset(&javacoreData, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
	if ( 1 == getJavacoreData(currentThread->javaVM, &javacoreData, !multiLayerStats) ) {
		multiLayerStats = multiLayerStats && (0 < javacoreData.topLayer);

		/* all CompositeCaches must be started to get proper stats */
		Trc_SHR_Assert_True(javacoreData.ccCount == javacoreData.ccStartedCount);

		if (multiLayerStats) {
			CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_TOP_LAYER_TITLE, _cacheName);
			printCacheStatsTopLayerStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData, multiLayerStats);
			printCacheStatsTopLayerSummaryStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData);
			j9tty_printf(_portlib, "---------------------------------------------------------\n");
			CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_ALL_LAYERS_TITLE, _cacheName);
			printCacheStatsAllLayersStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData, staleBytes);
		} else {
			CACHEMAP_PRINT1(J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_SHRC_CM_PRINTSTATS_TITLE, _cacheName);
			printCacheStatsTopLayerStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData, multiLayerStats);
			j9tty_printf(_portlib, "\n");
			printCacheStatsAllLayersStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData, staleBytes);
			printCacheStatsTopLayerSummaryStatsHelper(currentThread, showFlags, runtimeFlags, &javacoreData);
		}
	}
	return 0;
}

bool
SH_CacheMap::isBytecodeAgentInstalled(void)
{
	return ((*_runtimeFlags & J9SHR_RUNTIMEFLAG_BYTECODE_AGENT_RUNNING) != 0);
}

/**
 * Print verbose stats on cache shutdown
 */
void
SH_CacheMap::printShutdownStats(void)
{
	UDATA bytesStored = 0;
	U_64 bytesRead = (U_64)_bytesRead;		/* U_64 for compatibility so that we don't have to change all the nls msgs */
	U_32 softmxUnstoredBytes = 0;
	U_32 maxAOTUnstoredBytes = 0;
	U_32 maxJITUnstoredBytes = 0;
	SH_CompositeCacheImpl* cache = _ccHead;
	PORT_ACCESS_FROM_PORT(_portlib);

	while (cache) {
		bytesStored += cache->getTotalStoredBytes();
		cache = cache->getNext();
	}
	getUnstoredBytes(&softmxUnstoredBytes, &maxAOTUnstoredBytes, &maxJITUnstoredBytes);
	
	CACHEMAP_TRACE2(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE, J9NLS_INFO, J9NLS_SHRC_CM_PRINTSHUTDOWNSTATS_READ_STORED, bytesRead, bytesStored);
	CACHEMAP_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE, J9NLS_INFO, J9NLS_SHRC_CM_PRINTSHUTDOWNSTATS_UNSTORED_V1, softmxUnstoredBytes, maxAOTUnstoredBytes, maxJITUnstoredBytes);
}

/**
 * Run required code on JVM exit
 */
void
SH_CacheMap::runExitCode(J9VMThread* currentThread)
{
	SH_Manager* walkManager;
	SH_Managers::ManagerWalkState state;
	SH_CompositeCacheImpl* cache = _ccHead;

	printShutdownStats();
	
	walkManager = managers()->startDo(currentThread, 0, &state);
	while (walkManager) {
		walkManager->runExitCode();
		walkManager = managers()->nextDo(&state);
	}
	
	while (cache) {
		cache->runExitCode(currentThread);
		cache = cache->getNext();
	}
}

/* Note: className can be NULL and if not, is not necessarily null-terminated */
/* THREADING: Can be called multi-threaded */
IDATA /*static */
SH_CacheMap::createPathString(J9VMThread* currentThread, J9SharedClassConfig* config, char** pathBuf, UDATA pathBufSize, ClasspathEntryItem* cpei, const char* className, UDATA classNameLen, bool* doFreeBuffer)
{
	char* fullPath = *pathBuf;
	U_16 cpeiPathLen = 0;
	const char* cpeiPath = cpei->getLocation(&cpeiPathLen);
	char* classNamePos = (char*)className;
	UDATA cNameLen = classNameLen;
	char* lastSlashPos = NULL;
	UDATA sizeNeeded = 0;
	const char* classNameTrc = NULL;
	UDATA classNameTrcLen = 0;
	J9PortLibrary* _portlib = currentThread->javaVM->portLibrary;

	PORT_ACCESS_FROM_PORT(_portlib);

	*doFreeBuffer = false;		/* Initialize to false */

	if (!className) {
		classNameTrc = "NULL";
		classNameTrcLen = strlen(classNameTrc);
	} else {
		classNameTrc = className;
		classNameTrcLen = classNameLen;
	}

	Trc_SHR_CM_createPathString_Entry(currentThread, cpeiPathLen, cpeiPath, classNameTrcLen, classNameTrc);
	
	/* cannot use strrchr as may not be null-terminated */
	if (classNamePos) {
		IDATA cntr = cNameLen-1;
		for (; cntr>=0; cntr--) {
			if (classNamePos[cntr]=='/' || classNamePos[cntr]=='.') {
				lastSlashPos = &(classNamePos[cntr]);
				break;
			}
		}
	}
	sizeNeeded = cpeiPathLen + cNameLen + SHARE_CLASS_FILE_EXT_STRLEN + 2;	/* 2 = pathsep + \0 */
	if (sizeNeeded > pathBufSize) {
		*pathBuf = (char*)j9mem_allocate_memory(sizeNeeded, J9MEM_CATEGORY_CLASSES);
		fullPath = *pathBuf;
		if (!fullPath) {
			if (config->verboseFlags & J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT) {
				CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_STRINGBUF_ALLOC);
			}
			Trc_SHR_CM_createPathString_Exception1(currentThread);
			return -1;
		}
		Trc_SHR_CM_createPathString_AllocateBuffer(currentThread, sizeNeeded, fullPath);
		*doFreeBuffer = true;
	}
	strncpy(fullPath, (char*)cpeiPath, cpeiPathLen);
	fullPath[cpeiPathLen]='\0';

	/* Create Package directory */
	if (lastSlashPos) {
		UDATA packageLen = (UDATA)(lastSlashPos - className);
		char* packagePos = fullPath + cpeiPathLen + 1;

		if (fullPath[strlen(fullPath)-1] != PATHSEP[0]) {
			strcat(fullPath, PATHSEP);
		}
		strncat(fullPath, className, packageLen);
		fullPath[cpeiPathLen + packageLen + 1] = '\0';
		while (*packagePos) {				/* guaranteed to be null-terminated */
			if ((*packagePos == '/') || (*packagePos == '.')) {
				*packagePos = PATHSEP[0];
			}
			packagePos++;
		}
		classNamePos = lastSlashPos+1;
		cNameLen = classNameLen - packageLen - 1;
	}
	if (classNamePos) {
		if (fullPath[strlen(fullPath)-1] != PATHSEP[0]) {
			strcat(fullPath, PATHSEP);
		}
		strncat(fullPath, classNamePos, cNameLen);
		fullPath[cpeiPathLen + classNameLen + 1] = '\0';
		strcat(fullPath, SHARE_CLASS_FILE_EXT_STRING);
	}

	Trc_SHR_CM_createPathString_Exit(currentThread, fullPath);
	return 0;
}

/**
 * Return the bounds of the cache area which contains the ROM classes.
 * Note there is no synchronization so the ending address may not be
 * accurate unless the write lock is acquired before calling this function.
 */
void
SH_CacheMap::getRomClassAreaBounds(void ** romClassAreaStart, void ** romClassAreaEnd)
{
	if (romClassAreaStart) {
		*romClassAreaStart = _ccHead->getBaseAddress();
	}
	if (romClassAreaEnd) {
		*romClassAreaEnd = (void *)_ccHead->getSegmentAllocPtr();
	}
}

IDATA 
SH_CacheMap::enterStringTableMutex(J9VMThread* currentThread, BOOLEAN readOnly, UDATA* doRebuildLocalData, UDATA* doRebuildCacheData)
{
	IDATA result = -1;

	J9SharedInvariantInternTable* table = currentThread->javaVM->sharedInvariantInternTable;

	Trc_SHR_Assert_True(_sharedClassConfig != NULL);
	Trc_SHR_CM_enterStringTableMutex_entry(currentThread);
	
	if ((result = _ccHead->enterReadWriteAreaMutex(currentThread, readOnly, doRebuildLocalData, doRebuildCacheData)) == 0) {
		/* First time we enter this mutex, we'll usually already have a non-shared tree */
		if (table != NULL) {
			/* This function brings the tree up-to-date with any cache changes */
			if (table->sharedHeadNodePtr == NULL) {
				table->headNode = NULL;
			} else {
				table->headNode = SRP_PTR_GET(table->sharedHeadNodePtr, J9SharedInternSRPHashTableEntry *);
			}

			if (table->sharedTailNodePtr == NULL) {
				table->tailNode = NULL;
			} else {
				table->tailNode = SRP_PTR_GET(table->sharedTailNodePtr, J9SharedInternSRPHashTableEntry *);
			}

			if (readOnly || J9_ARE_ANY_BITS_SET(_sharedClassConfig->runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_AVAILABLE_SPACE_FULL)) {
				/* Disable all updates to the shared tree */
				table->flags |= J9AVLTREE_DISABLE_SHARED_TREE_UPDATES;
			} else {
				table->flags &= ~J9AVLTREE_DISABLE_SHARED_TREE_UPDATES;
			}
		}
	}
		
	Trc_SHR_CM_enterStringTableMutex_exit(currentThread, result);
	return result;
}

IDATA 
SH_CacheMap::exitStringTableMutex(J9VMThread* currentThread, UDATA resetReason)
{
	IDATA result;
	J9SharedInvariantInternTable* table = currentThread->javaVM->sharedInvariantInternTable;
	
	Trc_SHR_CM_exitStringTableMutex_entry(currentThread);
	
	if ((table != NULL) && !_ccHead->isReadWriteAreaReadOnly()) {
		SRP_PTR_SET(table->sharedHeadNodePtr, table->headNode);
		SRP_PTR_SET(table->sharedTailNodePtr, table->tailNode);
	}

	result = _ccHead->exitReadWriteAreaMutex(currentThread, resetReason);
	
	Trc_SHR_CM_exitStringTableMutex_exit(currentThread, result);
	return result;
}

/* Simple utility function which uses the scope manager to find/store UTF strings in the cache
 * Searches for a matching string in the cache. If one is not found, it is added.
 * Returns NULL if error or if the cache is full
 * THREADING: Caller should not have either the read mutex or write mutex
 */
const J9UTF8*
SH_CacheMap::getCachedUTFString(J9VMThread* currentThread, const char* local, U_16 localLen)
{
	const char* fnName = "getCachedUTFString";
	const J9UTF8* pathUTF = NULL;
	U_8 temp[J9SH_MAXPATH + sizeof(J9UTF8)];
	J9UTF8* temputf = (J9UTF8*)&temp;
	char* tempstr = (char*)J9UTF8_DATA(temputf);
	SH_ScopeManager* localSCM;
	bool allowUpdate = true;

	Trc_SHR_Assert_False(_ccHead->hasWriteMutex(currentThread));

	if (!(localSCM = getScopeManager(currentThread))) {
		return NULL;
	}

	Trc_SHR_CM_getCachedUTFString_entry(currentThread, localLen, local);

	if (_ccHead->enterReadMutex(currentThread, fnName) != 0) {
		Trc_SHR_CM_getCachedUTFString_FailedMutex(currentThread, localLen, local);
		Trc_SHR_CM_getCachedUTFString_exit1(currentThread);
		return NULL;
	}

	if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
		_ccHead->exitReadMutex(currentThread, fnName);
		Trc_SHR_CM_getCachedUTFString_exit1(currentThread);
		return NULL;
	}

	if (J9_ARE_ANY_BITS_SET(*_runtimeFlags, RUNTIME_FLAGS_PREVENT_BLOCK_DATA_UPDATE)) {
		/* runtime flags will be updated in runEntryPointChecks(), check runtime flags after runEntryPointChecks() is called */
		allowUpdate = false;
	}

	J9UTF8_SET_LENGTH(temputf, localLen);
	strncpy(tempstr, local, J9UTF8_LENGTH(temputf));

	pathUTF = localSCM->findScopeForUTF(currentThread, temputf);
	_ccHead->exitReadMutex(currentThread, fnName);
	if (NULL == pathUTF) {
		if (allowUpdate){
			if (_ccHead->enterWriteMutex(currentThread, false, fnName) == 0) {
				IDATA itemsRead;
				/* Must refresh before doing any commits, since a commit will
				 * overwrite the update counter.
				 */
				if ((itemsRead = runEntryPointChecks(currentThread, NULL, NULL)) == -1) {
					_ccHead->exitWriteMutex(currentThread, fnName);
					Trc_SHR_CM_getCachedUTFString_exit3(currentThread);
					return NULL;
				}
				if ((itemsRead == 0) || !(pathUTF = localSCM->findScopeForUTF(currentThread, temputf))) {
					/* add to enclosing cache */
					pathUTF = addScopeToCache(currentThread, temputf);
				}
				_ccHead->exitWriteMutex(currentThread, fnName);
			}
		} else {
			increaseUnstoredBytes(J9UTF8_LENGTH(temputf) + sizeof(struct J9UTF8));
		}
	}
	
	Trc_SHR_CM_getCachedUTFString_exit2(currentThread, pathUTF);

	return pathUTF;
}

/**
 * Called when zip/jar files are opened or closed.
 * Unlikely that we have the VM class segment mutex here.
 */
void 
SH_CacheMap::notifyClasspathEntryStateChange(J9VMThread* currentThread, const char* path, UDATA newState)
{
	const J9UTF8* pathUTF;
	SH_ClasspathManager* localCPM;

	if (!(localCPM = getClasspathManager(currentThread))) { 
		return;
	}
	/* Path string passed to this function is likely to be transient - store it in the cache */ 
	if ((pathUTF = getCachedUTFString(currentThread, path, (U_16)strlen(path)))) {
		localCPM->notifyClasspathEntryStateChange(currentThread, pathUTF, newState);
	}
}

SH_CompositeCache*
SH_CacheMap::getCompositeCacheAPI()
{
	return (SH_CompositeCache*)_ccHead;
}

SH_ScopeManager*
SH_CacheMap::getScopeManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _scm) == 1) {
		return _scm;
	}
	return NULL;
}

SH_ClasspathManager*
SH_CacheMap::getClasspathManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _cpm) == 1) {
		return _cpm;
	}
	return NULL;
}

SH_ROMClassManager*
SH_CacheMap::getROMClassManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _rcm) == 1) {
		return _rcm;
	}
	return NULL;
}

SH_ByteDataManager*
SH_CacheMap::getByteDataManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _bdm) == 1) {
		return _bdm;
	}
	return NULL;
}

SH_CompiledMethodManager*
SH_CacheMap::getCompiledMethodManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _cmm) == 1) {
		return _cmm;
	}
	return NULL;
}

SH_AttachedDataManager*
SH_CacheMap::getAttachedDataManager(J9VMThread* currentThread)
{
	if (startManager(currentThread, _adm) == 1) {
		return _adm;
	}
	return NULL;
}

/**
 * Allocate, initialise and append a cache descriptor to the end of descriptor list.
 * @param[in] currentThread The current VMThread
 * @param[in] sharedClassConfig The J9SharedClassConfig struct
 * @param[in] ccToUse The SH_CompositeCacheImpl that the descriptor represents
 *
 * @@return Address of new J9SharedClassCacheDescriptor or NULL on failure.
 */
J9SharedClassCacheDescriptor*
SH_CacheMap::appendCacheDescriptorList(J9VMThread* currentThread, J9SharedClassConfig* sharedClassConfig, SH_CompositeCacheImpl* ccToUse)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	J9SharedClassCacheDescriptor *cacheDesc;

	cacheDesc = (J9SharedClassCacheDescriptor *)j9mem_allocate_memory(sizeof(J9SharedClassCacheDescriptor), J9MEM_CATEGORY_CLASSES);
	if (!cacheDesc) {
		return NULL;
	}
	memset(cacheDesc, 0, sizeof(J9SharedClassCacheDescriptor));

	if (sharedClassConfig->configMonitor) {
		enterLocalMutex(currentThread, sharedClassConfig->configMonitor, "config monitor", "appendCacheDescriptorList");
	}

	Trc_SHR_Assert_True(NULL != sharedClassConfig->cacheDescriptorList);
	Trc_SHR_Assert_True(_ccHead->getCacheHeaderAddress() == sharedClassConfig->cacheDescriptorList->cacheStartAddress);

	J9SharedClassCacheDescriptor* cacheDescriptorTail = sharedClassConfig->cacheDescriptorList->previous;
	cacheDesc->cacheStartAddress = ccToUse->getCacheHeaderAddress();
	cacheDesc->romclassStartAddress = ccToUse->getFirstROMClassAddress();
	cacheDesc->metadataStartAddress = (U_8*)ccToUse->getCacheEndAddress() - sizeof(ShcItemHdr);
	cacheDesc->cacheSizeBytes = ccToUse->getCacheMemorySize();

	cacheDescriptorTail->next = cacheDesc;
	cacheDesc->previous = cacheDescriptorTail;
	cacheDesc->next = sharedClassConfig->cacheDescriptorList;
	sharedClassConfig->cacheDescriptorList->previous = cacheDesc;

	if (sharedClassConfig->configMonitor) {
		exitLocalMutex(currentThread, sharedClassConfig->configMonitor, "config monitor", "appendCacheDescriptorList");
	}

	return cacheDesc;
}

/**
 * Frees an existing list and reinitializes it.
 */
void
SH_CacheMap::resetCacheDescriptorList(J9VMThread* currentThread, J9SharedClassConfig* sharedClassConfig)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	J9SharedClassCacheDescriptor *cacheDesc, *nextCacheDesc;

	if (sharedClassConfig->configMonitor) {
		enterLocalMutex(currentThread, sharedClassConfig->configMonitor, "config monitor", "initializeROMSegmentList");
	}
	
	/* set cacheDesc to the head of the list. note it was allocated as part of sharedClassConfig. */
	cacheDesc = sharedClassConfig->cacheDescriptorList;
	Trc_SHR_Assert_False(cacheDesc == NULL);
	Trc_SHR_Assert_False(cacheDesc->next == NULL);

	nextCacheDesc = cacheDesc->next;
	while (nextCacheDesc != cacheDesc) {
		cacheDesc->next = nextCacheDesc->next;
		j9mem_free_memory(nextCacheDesc);

		nextCacheDesc = cacheDesc->next;
	}
	
	/* reset cacheDescriptorList. not strictly necessary if sharedClassConfig should be destroyed after this. */
	sharedClassConfig->cacheDescriptorList = cacheDesc;
	Trc_SHR_Assert_True(sharedClassConfig->cacheDescriptorList 
			== sharedClassConfig->cacheDescriptorList->next);
	
	if (sharedClassConfig->configMonitor) {
		exitLocalMutex(currentThread, sharedClassConfig->configMonitor, "config monitor", "initializeROMSegmentList");
	}
}


/**
 * Enter write lock on 'write' area of the cache
 */
IDATA
SH_CacheMap::startClassTransaction(J9VMThread* currentThread, bool lockCache, const char* caller) {
	IDATA retval;
	Trc_SHR_CM_startClassTransactionEntry();
	retval = _ccHead->enterWriteMutex(currentThread, lockCache, caller);
	if (retval != 0) {
		Trc_SHR_CM_startClassTransaction_enterWriteMutex_Event();
		goto _exit;
	}
	if (runEntryPointChecks(currentThread, NULL, NULL) == -1) {
		Trc_SHR_CM_startClassTransaction_runEntryPointChecks_Event();
		exitClassTransaction(currentThread, "startClassTransaction");
		goto _exit;
	}
_exit:
	Trc_SHR_CM_startClassTransaction_Exit();
	return retval;
}

/**
 * Exit write lock on 'write' area of the cache
 */
IDATA
SH_CacheMap::exitClassTransaction(J9VMThread* currentThread, const char* caller) {
	return _ccHead->exitWriteMutex(currentThread, caller);
}

/**
 * Check if a ptr is in a shared ROMClassSegment
 */
bool
SH_CacheMap::isAddressInROMClassSegment(const void* address) {
	return _cc->isAddressInROMClassSegment(address);
}


/**
 *	Set the string table initialized state
 *
 * @param[in] isInitialized
 *
 * @return void

 * Caller should hold string table write lock
 */
void
SH_CacheMap::setStringTableInitialized(bool isInitialized)
{
	this->_ccHead->setStringTableInitialized(isInitialized);
}

/**
 *	Get the string table initialized state
 *
 * @return true if the readWrite area has been initialized, 
 * 		   false otherwise

 * Caller should hold string table write lock
 */
bool
SH_CacheMap::isStringTableInitialized(void)
{
	return this->_ccHead->isStringTableInitialized();
}

/**
 * Is address is in the caches debug area?
 *
 * @ Returns true if address is in the caches debug area
 */
BOOLEAN 
SH_CacheMap::isAddressInCacheDebugArea(void *address, UDATA length) 
{
	return this->_ccHead->isAddressInCacheDebugArea(address, length);
}

/**
 *	Get the size of the cache's class debug data area
 *
 * @return size in bytes
 */
U_32
SH_CacheMap::getDebugBytes(void)
{
	return this->_ccHead->getDebugBytes();
}

SH_Managers *
SH_CacheMap::managers()
{
	return this->_managers;
}

/**
 * Start up this CacheMap. Should be called after initialization.
 * This does not initialize the cache for use with the JVM. Only queries can be made to the cache,
 * in particular getJavacoreData().
 * Does not call sanityWalkROMClassSegment() or reportCorruptCache(). It will not delete the cache.
 * Does not call initializeROMSegmentList() or lock the classMemorySegments->segmentMutex.
 * It does read the cache contents.
 *
 * THREADING: Only ever single threaded
 *
 * @param [in] currentThread The current thread
 * @param [in] ctrlDirName The cache control directroy
 * @param [in] groupPerm Group permissions to open the cache directory
 * @param [in] oscache An exiting top layer SH_OSCache
 * @param [in] runtimeflags The runtime flags used by this cache
 * @param [out] lowerLayerList A list of SH_OSCache_Info for lower layer caches.
 *
 * @return CC_STARTUP_OK on success, CC_STARTUP_FAILED(-1) or CC_STARTUP_CORRUPT(-2) on failure
 */
IDATA
SH_CacheMap::startupForStats(J9VMThread* currentThread, const char* ctrlDirName, UDATA groupPerm, SH_OSCache *oscache, U_64 *runtimeflags, J9Pool **lowerLayerList)
{
	IDATA retval = 0;
	UDATA verboseFlags = 0;
	IDATA startuprc = CC_STARTUP_OK;
	IDATA itemsRead = -1;
	SH_CompositeCacheImpl* ccToUse = NULL;
	J9JavaVM *vm = currentThread->javaVM;

	/*The below runtime flags are used during:
	 * 	1.) 'SH_CacheMap::startManager'
	 * 	2.) 'SH_CacheMap::enterReentrantLocalMutex'
	 *
	 */
	_runtimeFlags = runtimeflags;

	/* When 'SH_CacheMap::startManager' is called it expected that the _refreshMutex is created. */
	if (omrthread_monitor_init(&_refreshMutex, 0) != 0) {
		_refreshMutex = NULL;
		retval = CC_STARTUP_FAILED;
		goto done;
	}
	
	
	startuprc = _ccHead->startupForStats(currentThread, oscache, _runtimeFlags, verboseFlags);
	if (startuprc != CC_STARTUP_OK) {
		if (startuprc == CC_STARTUP_CORRUPT) {
			retval = CC_STARTUP_CORRUPT;
		} else {
			retval = CC_STARTUP_FAILED;
		}
		goto done;
	} else {
		if (oscache->getLayer() > 0) {
			startuprc = startupLowerLayerForStats(currentThread, ctrlDirName, groupPerm, oscache, lowerLayerList);
			if (startuprc != CC_STARTUP_OK) {
				if (startuprc == CC_STARTUP_CORRUPT) {
					retval = CC_STARTUP_CORRUPT;
				} else {
					retval = CC_STARTUP_FAILED;
				}
				goto done;
			}
		}
	}
	setCacheAddressRangeArray();

	ccToUse = _ccTail;
	
	do {
		itemsRead = readCache(currentThread, ccToUse, -1, true);
		if (CM_READ_CACHE_FAILED == itemsRead) {
			retval = CC_STARTUP_FAILED;
		} else if (CM_CACHE_CORRUPT == itemsRead) {
			retval = CC_STARTUP_CORRUPT;
		}

		if (ccToUse != _ccHead) {
			if (NULL == *lowerLayerList) {
				*lowerLayerList = pool_new(sizeof(SH_OSCache_Info),  0, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_CLASSES, POOL_FOR_PORT(_portlib));
			}
			if (NULL == *lowerLayerList) {
				retval = CC_STARTUP_FAILED;
				break;
			}
			(*lowerLayerList)->flags |= POOL_ALWAYS_KEEP_SORTED;
			SH_OSCache_Info tempInfo;
			if (-1 != ccToUse->getNonTopLayerCacheInfo(vm, ctrlDirName, groupPerm, &tempInfo)) {
				if (CC_STARTUP_CORRUPT == retval) {
					tempInfo.isCorrupt = 1;
				}
				if (!ccToUse->getJavacoreData(vm, &tempInfo.javacoreData)) {
					retval = CC_STARTUP_FAILED;
					break;
				}
				tempInfo.javacoreData.freeBytes = ccToUse->getFreeAvailableBytes();
				tempInfo.isJavaCorePopulated = 1;
				SH_OSCache_Info* newElement = (SH_OSCache_Info*) pool_newElement(*lowerLayerList);
				if (NULL == newElement) {
					Trc_SHR_CM_startupForStats_pool_newElement_failed(currentThread);
					pool_kill(*lowerLayerList);
					*lowerLayerList = NULL;
					retval = CC_STARTUP_FAILED;
					break;
				}
				memcpy(newElement, &tempInfo, sizeof(SH_OSCache_Info));
			} else {
				retval = CC_STARTUP_FAILED;
				break;
			}
		}
		ccToUse = ccToUse->getPrevious();
	} while (NULL != ccToUse && CC_STARTUP_OK == retval);

done:
	if (retval != CC_STARTUP_OK) {
		shutdownForStats(currentThread);
	}
	return retval;
}

/**
 * Start up the lower layer shared cache for statistics.
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] oscache an exiting top layer SH_OSCache
 * @param [out] lowerLayerStatsList A list of SH_OSCache_Info for all lower layer caches.
 *
 * @retval CC_STARTUP_OK (0) success
 * @retval CC_STARTUP_FAILED(-1) or CC_STARTUP_CORRUPT (-2) on failure.
 */
IDATA
SH_CacheMap::startupLowerLayerForStats(J9VMThread* currentThread, const char* ctrlDirName, UDATA groupPerm, SH_OSCache *oscache, J9Pool** lowerLayerList)
{
	IDATA rc = CC_STARTUP_OK;
	SH_CompositeCacheImpl* ccToUse = _ccHead;
	const char* cacheName = NULL;
	U_32 cacheType = oscache->getCacheType();
	char cacheUniqueID[J9SHR_UNIQUE_CACHE_ID_BUFSIZE];
	J9JavaVM *vm = currentThread->javaVM;
	char cacheDirBuf[J9SH_MAXPATH];
	SH_OSCache::getCacheDir(vm, ctrlDirName, cacheDirBuf, J9SH_MAXPATH, cacheType, false);
	
	do {
		const char* cacheUniqueIDPtr = NULL;
		UDATA idLen = 0;
		bool isCacheUniqueIdStored = false;
		
		IDATA preqRC = getPrereqCache(currentThread, cacheDirBuf, ccToUse, true, &cacheUniqueIDPtr, &idLen, &isCacheUniqueIdStored);
		I_8 layer = 0;

		if (0 > preqRC) {
			if (CM_CACHE_CORRUPT == preqRC) {
				rc = CC_STARTUP_CORRUPT;
				SH_Managers::ManagerWalkState state;
				SH_Manager* walkManager = managers()->startDo(currentThread, 0, &state);
				while (walkManager) {
					/* Corruption on the metadata detected. We will return from this function. Clean up the managers before that. */ 
					walkManager->cleanup(currentThread);
					walkManager = managers()->nextDo(&state);
				}
			} else {
				rc = CC_STARTUP_FAILED;
			}
			break;
		} else if (1 == preqRC) {
			PORT_ACCESS_FROM_VMC(currentThread);
			UDATA reqBytes = SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, true);
			SH_CompositeCacheImpl* allocPtr = (SH_CompositeCacheImpl*)j9mem_allocate_memory(reqBytes, J9MEM_CATEGORY_CLASSES);
			if (NULL == allocPtr) {
				rc = CC_STARTUP_FAILED;
				break;
			}
			char cacheNameBuf[USER_SPECIFIED_CACHE_NAME_MAXLEN];
			Trc_SHR_Assert_True(idLen < sizeof(cacheUniqueID));
			memcpy(cacheUniqueID, cacheUniqueIDPtr, idLen);
			cacheUniqueID[idLen] = '\0';
			SH_OSCache::getCacheNameAndLayerFromUnqiueID(vm, cacheUniqueID, idLen, cacheNameBuf, USER_SPECIFIED_CACHE_NAME_MAXLEN, &layer);
			cacheName = cacheNameBuf;
			SH_CompositeCacheImpl* ccNext = SH_CompositeCacheImpl::newInstance(vm, _sharedClassConfig, allocPtr, cacheName, cacheType, true, layer);
			ccNext->setNext(NULL);
			ccNext->setPrevious(ccToUse);
			ccToUse->setNext(ccNext);
			_ccTail = ccNext;
		} else {
			/* no prereq cache */
			break;
		}
		ccToUse = ccToUse->getNext();
		if (NULL != ccToUse) {
			rc = ccToUse->startupNonTopLayerForStats(currentThread, ctrlDirName, cacheName, cacheType, layer, _runtimeFlags, 0);
		}
	} while (NULL != ccToUse && CC_STARTUP_OK == rc);

	return rc;
}


/**
 * Shut down a CacheMap started with startupForStats().
 *
 * THREADING: Only ever single threaded
 *
 * @param [in] currentThread  The current thread
 *
 * @return 0 on success or -1 for failure
 */
IDATA
SH_CacheMap::shutdownForStats(J9VMThread* currentThread)
{
	SH_Manager* walkManager;
	SH_Managers::ManagerWalkState state;
	IDATA retval = 0;

	walkManager = managers()->startDo(currentThread, 0, &state);
	while (walkManager) {
		walkManager->cleanup(currentThread);
		walkManager = managers()->nextDo(&state);
	}	

	SH_CompositeCacheImpl* ccToUse = _ccHead;
	
	while (ccToUse != NULL) {
		if (ccToUse->shutdownForStats(currentThread) != 0) {
			retval = -1;
		}
		ccToUse = ccToUse->getNext();
	}
	ccToUse = _ccHead;
	while (ccToUse != NULL) {
		SH_CompositeCacheImpl* ccNext = ccToUse->getNext();
		if (_ccHead != ccToUse) {
			PORT_ACCESS_FROM_VMC(currentThread);
			/* _ccHead is alloacated together with the SH_CacheMap instance, it will be free together with the SH_CacheMap instance */
			ccToUse->cleanup(currentThread);
			j9mem_free_memory(ccToUse);
		}
		ccToUse = ccNext;
	}

	if (_refreshMutex != NULL) {
		if (omrthread_monitor_destroy(_refreshMutex) != 0) {
			retval = -1;
		}
		/* Set it to NULL even destroy fails */
		_refreshMutex = NULL;
	}

	if (_ccPool) {
		pool_kill(_ccPool);
	}

	return retval;
}

J9SharedClassConfig*
SH_CacheMap::getSharedClassConfig()
{
	return _sharedClassConfig;
}

UDATA
SH_CacheMap::getReadWriteBytes(void)
{
	return this->_ccHead->getReadWriteBytes();
}

const char *
SH_CacheMap::attachedTypeString(UDATA type)
{
	switch(type) {
	case J9SHR_ATTACHED_DATA_TYPE_JITPROFILE:
		return "JITPROFILE";
	case J9SHR_ATTACHED_DATA_TYPE_JITHINT:
		return "JITHINT";
	default:
		Trc_SHR_CM_attachedTypeString_Error(type);
		Trc_SHR_Assert_ShouldNeverHappen();
		return "UNKNOWN";
	}
}

bool
SH_CacheMap::isCacheCorruptReported(void)
{
	return _cacheCorruptReported;
}

/**
 * Print a series of bytes as hexadecimal characters into a buffer.
 * The data are truncated silently if the buffer is too small.
 * When allocating the buffer, allow 5 characters per byte plus a null character to terminate the string.
 * @param attachedData Data to be printed
 * @param attachedDataLength Length of the data
 * @param attachedDataStringBuffer Output buffer to hold the string
 * @param bufferLength maximum number of characters
 */
static char*
formatAttachedDataString(J9VMThread* currentThread, U_8 *attachedData, UDATA attachedDataLength,
		char *attachedDataStringBuffer, UDATA bufferLength) {
	const int BYTE_STRING_LENGTH = 6; /* "0x" + 2 characters of hex data + " " + NUL */
	U_8 *dataCursor = attachedData;
	UDATA bytesRemaining = attachedDataLength;
	char *stringCursor = attachedDataStringBuffer;
	char *stringBufferEnd = attachedDataStringBuffer+bufferLength;

	PORT_ACCESS_FROM_VMC(currentThread);

	*stringCursor = '\0'; /* handle the zero-length case */
	while ((bytesRemaining > 0) && ((stringCursor+BYTE_STRING_LENGTH) < stringBufferEnd)){
		stringCursor += j9str_printf(PORTLIB, stringCursor, bufferLength, "0x%#02x ", *dataCursor); /* increment does not include the trailing '\0' */
		++dataCursor;
		--bytesRemaining;
	}
	return attachedDataStringBuffer;
}

/**
 * This function is called by j9shr_aotMethodOperation() to perform the required operation
 * @param[in] currentThread The current J9VMThread
 * @param[in] methodSpecs The pointer of the method specifications passed in by "invalidateAotMethods/revalidateAotMethods/findAotMethods=" option
 * @param[in] action SHR_FIND_AOT_METHOTHODS when listing the specified methods
 * 					 SHR_INVALIDATE_AOT_METHOTHODS when invalidating the specified methods
 * 					 SHR_REVALIDATE_AOT_METHOTHODS when revalidating the specified methods
 *
 * @return the number of AOT methods invalidated/revalidated/found on success or -1 on failure
 */
IDATA
SH_CacheMap::aotMethodOperation(J9VMThread* currentThread, char* methodSpecs, UDATA action)
{
	IDATA numMethods = 0;
	MethodSpecTable specTable[SHR_METHOD_SPEC_TABLE_MAX_SIZE];
	char* optionStartPos = methodSpecs;
	IDATA numSpecs = 0;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_aotMethodOperation_Entry(currentThread);

	memset(specTable, 0, SHR_METHOD_SPEC_TABLE_MAX_SIZE * sizeof(MethodSpecTable));

	while (('{' == *optionStartPos)
			|| (';' == *optionStartPos)
			|| (' ' == *optionStartPos)
			|| ('"' == *optionStartPos)
	) {
		/* strip unnecessary delims if they exist */
		optionStartPos += 1;
	}

	numSpecs = fillMethodSpecTable(specTable, optionStartPos);
	if (numSpecs <= 0) {
		Trc_SHR_CM_aotMethodOperation_numSpecsNegative(currentThread, numSpecs);
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_PARSE_METHOD_SPECS_ERROR);
		return -1;
	} else if (numSpecs > SHR_METHOD_SPEC_TABLE_MAX_SIZE) {
		Trc_SHR_CM_aotMethodOperation_numSpecsTooMany(currentThread, SHR_METHOD_SPEC_TABLE_MAX_SIZE);
		CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_PARSE_METHOD_SPECS_TOO_MANY_ERROR, SHR_METHOD_SPEC_TABLE_MAX_SIZE);
		return -1;
	}
	if (false == parseWildcardMethodSpecTable(specTable, numSpecs)) {
		/* wild card format is not valid, print out error message */
		Trc_SHR_CM_aotMethodOperation_methodSpecsFormatWrong(currentThread);
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_PARSE_METHOD_SPECS_ERROR);
		return -1;
	}

	numMethods = aotMethodOperationHelper(currentThread, specTable, numSpecs, action);
	
	Trc_SHR_CM_aotMethodOperation_Exit(currentThread, numMethods);

	return numMethods;
}

/**
 * This helper function is called by SH_CacheMap::aotMethodOperation() to perform the required operation
 * @param[in] currentThread The current J9VMThread
 * @param[in] specTable The method specification table
 * @param[in] numSpecs The number of the method specifications passed in by "invalidateAotMethods/revalidateAotMethods/findAotMethods=" option
 * @param[in] action SHR_FIND_AOT_METHOTHODS when listing the specified methods
 * 					 SHR_INVALIDATE_AOT_METHOTHODS when invalidating the specified methods
 * 					 SHR_REVALIDATE_AOT_METHOTHODS when revalidating the specified methods
 *
 * @return the number of AOT methods invalidated/revalidated/found on success or -1 on failure
 */
const IDATA
SH_CacheMap::aotMethodOperationHelper(J9VMThread* currentThread, MethodSpecTable* specTable, IDATA numSpecs, UDATA action)
{
	IDATA rc = 0;
	ShcItem* it = NULL;
	J9InternalVMFunctions* vmFunctions = currentThread->javaVM->internalVMFunctions;
	bool lockCache = (SHR_FIND_AOT_METHOTHODS == action) ? false : true;
	const char* fnName ="aotMethodOperationHelper";
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_CM_aotMethodOperationHelper_Entry(currentThread);

	if (0 != _ccHead->enterWriteMutex(currentThread, lockCache, fnName)) {
		/* cache is only locked when doing invalidate or revalidate operation */
		CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_FAILED_ENTER_WRITE_MUTEX);
		return -1;
	}

	_ccHead->findStart(currentThread);
	do {
		it = (ShcItem*)_ccHead->nextEntry(currentThread, NULL);	/*	Will not skip over stale items	*/
		if (NULL == it) {
			break;
		}
		if (TYPE_COMPILED_METHOD == ITEMTYPE(it) || TYPE_INVALIDATED_COMPILED_METHOD == ITEMTYPE(it)) {
			J9ROMMethod* romMethod = (J9ROMMethod*)getAddressFromJ9ShrOffset(&(((CompiledMethodWrapper*)ITEMDATA(it))->romMethodOffset));
			J9UTF8* romClassName = NULL;
			J9ClassLoader* loader = NULL;
			J9ROMClass* romClass = vmFunctions->findROMClassFromPC(currentThread, (UDATA)romMethod, &loader);
	 		J9UTF8* romMethodName = J9ROMMETHOD_NAME(romMethod);
			J9UTF8* romMethodSig = J9ROMMETHOD_SIGNATURE(romMethod);

			if (NULL != romClass) {
				romClassName = J9ROMCLASS_CLASSNAME(romClass);
			}

			if (matchAotMethod(specTable, numSpecs, romClassName, romMethodName, romMethodSig)) {
		 		if (NULL != romMethodName) {
		 			CACHEMAP_TRACE4(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, (J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
							J9NLS_SHRC_CM_PRINTSTATS_AOT_DISPLAY, ITEMJVMID(it), (UDATA)it, J9UTF8_LENGTH(romMethodName), J9UTF8_DATA(romMethodName));
		 		}

				if (NULL != romMethodSig) {
					CACHEMAP_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, (J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE),
							J9NLS_SHRC_CM_PRINTSTATS_AOT_DISPLAY_SIG_ADDR, J9UTF8_LENGTH(romMethodSig), J9UTF8_DATA(romMethodSig), romMethod);
				}

				if (_ccHead->stale((BlockPtr)ITEMEND(it))) {
					j9tty_printf(_portlib, " ");
					CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, (J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_STALE);
				}

				if ((SHR_FIND_AOT_METHOTHODS == action)
					&& TYPE_INVALIDATED_COMPILED_METHOD == ITEMTYPE(it)
				) {
					/* Do not print tag "INVALIDATED" when invalidating/revalidating, as user may be confused if this is based on the state before or after the (re)invalidate operation*/
					j9tty_printf(_portlib, " ");
					CACHEMAP_PRINT((J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE), J9NLS_SHRC_CM_PRINTSTATS_INVALIDATED);
				}

				if (_verboseFlags > 0) {
					j9tty_printf(_portlib, "\n");
				}

				if (NULL != romClassName) {
					CACHEMAP_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_DO_NOT_PRINT_MESSAGE_TAG,
							J9NLS_SHRC_CM_PRINTSTATS_AOT_FROM_ROMCLASS, J9UTF8_LENGTH(romClassName), J9UTF8_DATA(romClassName), romClass);
				}

				if (SHR_INVALIDATE_AOT_METHOTHODS == action) {
					ITEMTYPE(it) = TYPE_INVALIDATED_COMPILED_METHOD;
				} else if (SHR_REVALIDATE_AOT_METHOTHODS == action) {
					ITEMTYPE(it) = TYPE_COMPILED_METHOD;
				}
				rc += 1;
			}
			/* silently ignore if matchAotMethod() return false */
		}
	} while (NULL != it);

	_ccHead->exitWriteMutex(currentThread, fnName);

	Trc_SHR_CM_aotMethodOperationHelper_Exit(currentThread, rc);

	return rc;
}

/**
 * This function returns whether an AOT method matches any of the specifications passed in by "invalidateAotMethods/revalidateAotMethods/findAotMethods=" option
 *
 * @param[in] specTable The method specification table
 * @param[in] numSpecs The number of the method specifications passed in
 * @param[in] romClassName The name of the class
 * @param[in] romMethodName The name of the method
 * @param[in] romMethodSig The signature of the method
 *
 * @return true if the method matches any specification, false otherwise
 */
const bool
SH_CacheMap::matchAotMethod(MethodSpecTable* specTable, IDATA numSpecs, J9UTF8* romClassName, J9UTF8* romMethodName, J9UTF8* romMethodSig)
{
	bool rc = false;
	bool classMatch = false;
	bool methodMatch = false;
	bool sigMatch = false;
	IDATA i = 0;

	for (i = 0; i < numSpecs; i++) {
		if ((NULL == specTable[i].className)
			|| (UnitTest::COMPILED_METHOD_TEST == UnitTest::unitTest)
		) {
			/* In COMPILED_METHOD_TEST, romClassName is NULL. In order to let the method to be found, set classMatch to true */
			/* Any class */
			classMatch = true;
		} else if (NULL == romClassName) {
			/* Class name is missing */
			classMatch = false;
		} else {
			classMatch = (TRUE == wildcardMatch(specTable[i].classNameMatchFlag,
							(const char *)specTable[i].className, specTable[i].classNameLength,
							(const char *)J9UTF8_DATA(romClassName), J9UTF8_LENGTH(romClassName)));
		}
		if (false == classMatch) {
			continue;
		}

		if (NULL == specTable[i].methodName) {
			/* Any methods */
			methodMatch = true;
		} else if (NULL == romMethodName) {
			/* Method name is missing */
			methodMatch = false;
		} else {
			methodMatch = (TRUE == wildcardMatch(specTable[i].methodNameMatchFlag,
										(const char *)specTable[i].methodName, specTable[i].methodNameLength,
										(const char *)J9UTF8_DATA(romMethodName), J9UTF8_LENGTH(romMethodName)));
		}

		if (false == methodMatch) {
			continue;
		}

		if (NULL == specTable[i].methodSig) {
			/* Any signature */
			sigMatch = true;
		} else if (NULL == J9UTF8_DATA(romMethodSig)) {
			/* Signature is missing */
			sigMatch = false;
		} else {
			char* methodSig = (char *)J9UTF8_DATA(romMethodSig);
			char* sigStart = strchr(methodSig, '(') + 1;
			char* sigEnd = strchr(methodSig, ')');
			IDATA sigLength = sigEnd - sigStart;

			/* Use string between '(' and ')' only */
			if (sigLength < 0) {
				sigMatch = false;
			} else {
				sigMatch = (TRUE == wildcardMatch(specTable[i].methodSigMatchFlag,
										(const char *)specTable[i].methodSig, specTable[i].methodSigLength,
										(const char *)sigStart, sigLength));
			}
		}

		if ((classMatch && methodMatch && sigMatch)) {
			rc = specTable[i].matchFlag;
		}
	}
	return rc;
}

/**
 * This function fills the method specification table with specifications passed in by "invalidateAotMethods/revalidateAotMethods/findAotMethods=" option
 *
 * @param[in] specTable The method specification table
 * @param[in] inputOption Pointer to the method specification string
 *
 * @return the number of method specifications passed in or -1 on failure.
 */
const IDATA
SH_CacheMap::fillMethodSpecTable(MethodSpecTable* specTable, char* inputOption)
{
	char* start = inputOption;
	char* curPos = inputOption;
	IDATA methodIndex = 0;
	IDATA rc = 0;
	bool setClass = true;
	bool setMethod = false;
	bool setSig = false;

	Trc_SHR_CM_fillMethodSpecTable_Entry();

	if ('\0' == *start) {
		Trc_SHR_Assert_ShouldNeverHappen();
		rc = -1;
		goto done;
	}
	curPos = start;

	do {
		if (setClass) {
			if ((',' == *curPos)
				|| (' ' == *curPos)
			) {
				/* strip ' ' and ',' if they exist. It still works in case user pass in consecutive "," by mistake or there is a following white space. */
				curPos += 1;
				continue;
			} else if ('!' == *curPos) {
				specTable[methodIndex].matchFlag = false;
				curPos += 1;
				if ('!' == *curPos) {
					/* consecutive "!" found, which is not allowed */
					rc = -1;
					goto done;
				}
			} else {
				specTable[methodIndex].matchFlag = true;
			}
			specTable[methodIndex].className = curPos;
		} else if (setMethod) {
			if (('.' == *curPos)
				|| (' ' == *curPos)
			) {
				/* strip ' ' and '.' if they exist. It still works in case user pass in consecutive "." by mistake or there is a following white space. */
				curPos += 1;
				continue;
			}
			specTable[methodIndex].methodName = curPos;
		} else if (setSig) {
			if (('(' == *curPos)
				|| (' ' == *curPos)
			) {
				/* strip ' ' and '(' if they exist. It still works in case user pass in consecutive "(" by mistake or there is a following white space. */
				curPos += 1;
				continue;
			}
			specTable[methodIndex].methodSig = curPos;
		}
		if ((',' == *curPos)
			|| ('.' == *curPos)
			|| ('(' == *curPos)
		) {
			if (',' == *curPos) {
				setClass = true;
				*curPos = '\0';
				methodIndex += 1;
				if (methodIndex >= SHR_METHOD_SPEC_TABLE_MAX_SIZE) {
					break;
				}
			} else if ('.' == *curPos) {
				setClass = false;
				setMethod = true;
				*curPos = '\0';
			} else {
				setClass = false;
				setMethod = false;
				setSig = true;
				*curPos = '\0';
			}
		} else {
			if (('"' == *curPos)
				|| ('}' == *curPos)
				|| (')' == *curPos)
			) {
				*curPos = '\0';
			}
			setClass = false;
			setMethod = false;
			setSig = false;
		}
		curPos += 1;
	} while(('\0' != *curPos) && ('}' != *curPos));

	*curPos = '\0';
	rc = methodIndex + 1;
done:
	Trc_SHR_CM_fillMethodSpecTable_Exit(rc);

	return rc;
}

/**
 * This function parses the wild card in the method specifications
 *
 * @param[in] specTable the method specification table
 * @param[in] numSpecs the number of the method specifications passed in
 *
 * @return true on success and false on failure
 */
const bool
SH_CacheMap::parseWildcardMethodSpecTable(MethodSpecTable* specTable, IDATA numSpecs)
{
	bool rc = true;
	IDATA i = 0;

	Trc_SHR_Assert_True(numSpecs > 0);
	Trc_SHR_Assert_NotEquals(specTable, NULL);

	for(i = 0; i < numSpecs; i++) {
		const char* resultString = NULL;
		UDATA resultLength = 0;
		U_32 matchFlag = 0;
		char* className = specTable[i].className;
		char* methodName = specTable[i].methodName;
		char* methodSig = specTable[i].methodSig;

		if (NULL == className) {
			/* any class */
			continue;
		}

		if (0 != parseWildcard((const char*)className, strlen(className), &resultString, &resultLength, &matchFlag)) {
			rc = false;
			break;
		}
		specTable[i].className = (char*)resultString;
		specTable[i].classNameMatchFlag = matchFlag;
		specTable[i].classNameLength = resultLength;

		if (NULL == methodName) {
			/* any method */
			continue;
		}

		if (0 != parseWildcard((const char*)methodName, strlen(methodName), &resultString, &resultLength, &matchFlag)) {
			rc = false;
			break;
		}
		specTable[i].methodName = (char*)resultString;
		specTable[i].methodNameMatchFlag = matchFlag;
		specTable[i].methodNameLength = resultLength;


		if (NULL == methodSig) {
			/* any signature */
			continue;
		}

		if (0 != parseWildcard((const char*)methodSig, strlen(methodSig), &resultString, &resultLength, &matchFlag)) {
			rc = false;
			break;
		}
		specTable[i].methodSig = (char*)resultString;
		specTable[i].methodSigMatchFlag = matchFlag;
		specTable[i].methodSigLength = resultLength;

	}
	return rc;
}

/**
 * Protect the last used pages in the shared cache.
 *
 * @param [in] currentThread pointer to current J9VMThread
 *
 * @return void
 */
void
SH_CacheMap::protectPartiallyFilledPages(J9VMThread *currentThread)
{
	const char *fnName = "protectPartiallyFilledPages";
	IDATA result = 0;

	Trc_SHR_CM_protectPartiallyFilledPages_Entry(currentThread);

	if (_ccHead->isMemProtectPartialPagesEnabled()) {
		result = _ccHead->enterWriteMutex(currentThread, false, fnName);
		if (0 != result) {
			Trc_SHR_CM_protectPartiallyFilledPages_FailedToAcquireWriteMutex(currentThread);
			goto done;
		}
		_ccHead->protectPartiallyFilledPages(currentThread);

		_ccHead->exitWriteMutex(currentThread, fnName);
	} else {
		Trc_SHR_CM_protectPartiallyFilledPages_mprotectPartialPagesNotSet(currentThread);
	}

done:
	Trc_SHR_CM_protectPartiallyFilledPages_Exit(currentThread);
	return;
}

/* Adjust the minAOT, maxAOT, minJIT, maxJIT and softMaxBytes in the cache header.
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 * @param [in] isJCLCall True if JCL is invoking this function
 *
 * @return I_32	J9SHR_SOFTMX_ADJUSTED is set if softmx has been adjusted
 *				J9SHR_MIN_AOT_ADJUSTED is set if minAOT has been adjusted
 *				J9SHR_MAX_AOT_ADJUSTED is set if maxAOT has been adjusted
 *				J9SHR_MIN_JIT_ADJUSTED is set if minJIT has been adjusted
 *				J9SHR_MAX_JIT_ADJUSTED is set if maxJIT has been adjusted
 */
I_32
SH_CacheMap::tryAdjustMinMaxSizes(J9VMThread* currentThread, bool isJCLCall)
{
	return _ccHead->tryAdjustMinMaxSizes(currentThread, isJCLCall);
}

/* Update the runtime cache full flags according to cache full flags in the cache header
 *
 * @param [in] currentThread Pointer to J9VMThread structure for the current thread
 *
 * @return void
 */
void
SH_CacheMap::updateRuntimeFullFlags(J9VMThread* currentThread)
{
	_ccHead->updateRuntimeFullFlags(currentThread);
	return;
}

/* This method is called by j9shr_classStoreTransaction_createSharedClass() to increase the unstored bytes.
 *
 * @param [in] segmentAndDebugBytes Unstored segment and debug area bytes.
 * @param [in] obj Pointer to J9SharedClassTransaction
 *
 * @return void
 */
void
SH_CacheMap::increaseTransactionUnstoredBytes(U_32 segmentAndDebugBytes, J9SharedClassTransaction* obj)
{
	bool modifiedNoContext = ((1 == obj->isModifiedClassfile) && (NULL == obj->modContextInCache));
	U_32 metaBytes = 0;
	U_32 blockBytes = 0;
	U_16 dataType = TYPE_ROMCLASS;

	Trc_SHR_CM_increaseTransactionUnstoredBytes_Entry(segmentAndDebugBytes, (UDATA)obj->classnameLength, (const char *)obj->classnameData);

	if ((NULL == obj->ClasspathWrapper) || modifiedNoContext) {
		metaBytes = sizeof(OrphanWrapper);
		dataType = TYPE_ORPHAN;
	} else {
		bool useScope = !((NULL == obj->partitionInCache) && (NULL == obj->modContextInCache));

		if (!useScope) {
			metaBytes = sizeof(ROMClassWrapper);
		} else {
			metaBytes = sizeof(ScopedROMClassWrapper);
			dataType = TYPE_SCOPED_ROMCLASS;
		}
	}
	blockBytes = segmentAndDebugBytes + metaBytes;

	increaseUnstoredBytes(blockBytes);

	Trc_SHR_CM_increaseTransactionUnstoredBytes_Exit(blockBytes, dataType);
}

/* Increase the unstored bytes.
 * 
 * @param [in] blockBytes Unstored block bytes.
 * @param [in] aotBytes Unstored aot bytes.
 * @param [in] jitBytes Unstored jit bytes.
 *
 * @return void
 */
void
SH_CacheMap::increaseUnstoredBytes(U_32 blockBytes, U_32 aotBytes, U_32 jitBytes)
{
	U_32 itemAndHdrBytes = 0;

	Trc_SHR_CM_increaseUnstoredBytes_Entry(blockBytes, aotBytes, jitBytes);

	if (J9_ARE_ANY_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL | J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES)) {
		return;
	}

	itemAndHdrBytes = sizeof(ShcItem) + sizeof(ShcItemHdr);
	if (0 != blockBytes) {
		blockBytes += (itemAndHdrBytes + SHC_WORDALIGN);
	}
	if (0 != aotBytes) {
		aotBytes += (itemAndHdrBytes + sizeof(CompiledMethodWrapper) + SHC_WORDALIGN);
	}
	if (0 != jitBytes) {
		/* SH_CompositeCacheImpl::allocateJIT() passes SHC_DOUBLEALIGN to align parameter of SH_CompositeCacheImpl::allocate()*/
		jitBytes += (itemAndHdrBytes + sizeof(AttachedDataWrapper) + SHC_DOUBLEALIGN);
	}

	_ccHead->increaseUnstoredBytes(blockBytes, aotBytes, jitBytes);
	Trc_SHR_CM_increaseUnstoredBytes_Exit();
}

/**
 * Get the unstored bytes
 *
 * @param [out] softmxUnstoredBytes bytes that are not stored due to the setting of softmx
 * @param [out] maxAOTUnstoredBytes AOT bytes that are not stored due to the setting of maxAOT
 * @param [out] maxJITUnstoredBytes JIT bytes that are not stored due to the setting of maxJIT
 */
void
SH_CacheMap::getUnstoredBytes(U_32 *softmxUnstoredBytes, U_32 *maxAOTUnstoredBytes, U_32 *maxJITUnstoredBytes) const
{
	_ccHead->getUnstoredBytes(softmxUnstoredBytes, maxAOTUnstoredBytes, maxJITUnstoredBytes);
}


/**
 * Update the local startup hints _sharedClassConfig->localStartupHints with the one in the shared cache
 *
 * @param [out] localHints pointer to _sharedClassConfig->localStartupHints
 * @param [in] hintsDataInCache The start up hints in the shared cache
 * @param [in] overwrite Whether local startup hints _sharedClassConfig->localStartupHints will overwrite the exiting one in the cache.
 */
void
SH_CacheMap::updateLocalHintsData(J9VMThread* currentThread, J9SharedLocalStartupHints* localHints, const J9SharedStartupHintsDataDescriptor* hintsDataInCache, bool overwrite)
{
	J9SharedStartupHintsDataDescriptor updatedHintsData = {0};

	Trc_SHR_Assert_True(J9_ARE_ANY_BITS_SET(localHints->localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_WRITE_HINTS));
	memcpy(&updatedHintsData, hintsDataInCache, sizeof(J9SharedStartupHintsDataDescriptor));

	if (J9_ARE_ALL_BITS_SET(localHints->localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_OVERWRITE_HEAPSIZES)) {
		if (overwrite) {
			/* check whether to overwrite again, as localHints->runtimeFlags might have been updated by another thread */
			Trc_SHR_CM_updateLocalHintsData_OverwriteHeapSizes(currentThread, updatedHintsData.heapSize1, updatedHintsData.heapSize2, localHints->hintsData.heapSize1, localHints->hintsData.heapSize2);
			updatedHintsData.heapSize1 = localHints->hintsData.heapSize1;
			updatedHintsData.heapSize2 = localHints->hintsData.heapSize2;
			updatedHintsData.flags |= J9SHR_STARTUPHINTS_HEAPSIZES_SET;
		}
	} else if (J9_ARE_ALL_BITS_SET(localHints->localStartupHintFlags, J9SHR_LOCAL_STARTUPHINTS_FLAG_STORE_HEAPSIZES)) {
		if (J9_ARE_NO_BITS_SET(updatedHintsData.flags, J9SHR_STARTUPHINTS_HEAPSIZES_SET)) {
			Trc_SHR_CM_updateLocalHintsData_WriteHeapSizes(currentThread, localHints->hintsData.heapSize1, localHints->hintsData.heapSize2);
			/* heapSize1 and heapSize2 have not been set before */
			updatedHintsData.heapSize1 = localHints->hintsData.heapSize1;
			updatedHintsData.heapSize2 = localHints->hintsData.heapSize2;
			updatedHintsData.flags |= J9SHR_STARTUPHINTS_HEAPSIZES_SET;
		}
	}
	memcpy(&localHints->hintsData, &updatedHintsData, sizeof(J9SharedStartupHintsDataDescriptor));
}

static void
checkROMClassUTF8SRPs(J9ROMClass *romClass)
{
	if ((UnitTest::CORRUPT_CACHE_TEST == UnitTest::unitTest)
		|| (UnitTest::CACHE_FULL_TEST == UnitTest::unitTest)
		|| (UnitTest::PROTECTA_SHARED_CACHE_DATA_TEST == UnitTest::unitTest)
		|| (UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST == UnitTest::unitTest)
		|| (UnitTest::ATTACHED_DATA_TEST == UnitTest::unitTest)
	) {
		return;
	}

	UDATA romClassEnd = (UDATA)romClass + (UDATA)romClass->romSize;
	U_32 i = 0;

	Trc_SHR_Assert_True((UDATA)J9ROMCLASS_CLASSNAME(romClass) < romClassEnd);
	Trc_SHR_Assert_True((UDATA)J9ROMCLASS_SUPERCLASSNAME(romClass) < romClassEnd);
	Trc_SHR_Assert_True((UDATA)J9ROMCLASS_OUTERCLASSNAME(romClass) < romClassEnd);

	if (romClass->interfaceCount > 0) {
		J9SRP * interfaceNames = J9ROMCLASS_INTERFACES(romClass);
		for (i = 0; i < romClass->interfaceCount; i++) {
			Trc_SHR_Assert_True(NNSRP_PTR_GET(interfaceNames, UDATA) < romClassEnd);
			interfaceNames++;
		}
	}
	if (romClass->innerClassCount > 0) {
		J9SRP* innerClassNames = J9ROMCLASS_INNERCLASSES(romClass);
		for (i = 0; i < romClass->innerClassCount; i++) {
			Trc_SHR_Assert_True(NNSRP_PTR_GET(innerClassNames, UDATA) < romClassEnd);
			innerClassNames++;
		}
	}

#if JAVA_SPEC_VERSION >= 11
	Trc_SHR_Assert_True((UDATA)J9ROMCLASS_NESTHOSTNAME(romClass) < romClassEnd);

	if (romClass->nestMemberCount > 0) {
		J9SRP *nestMemberNames = J9ROMCLASS_NESTMEMBERS(romClass);
		for (i = 0; i < (U_32)romClass->nestMemberCount; i++) {
			Trc_SHR_Assert_True(NNSRP_PTR_GET(nestMemberNames, UDATA) < romClassEnd);
			nestMemberNames++;
		}
	}
#endif /* JAVA_SPEC_VERSION >= 11 */
}

/**
 * checks if an address is in the cache metadata area
 *
 *	@param [in] currentThread The current JVM thread
 *	@param [in] address The address to be checked
 *
 *	@return true if the address is in cache metadata area, false otherwise.
 */

bool
SH_CacheMap::isAddressInReleasedMetaDataBounds(J9VMThread* currentThread, UDATA address) const
{
	bool rc = false;
	SH_CompositeCacheImpl* ccToUse = _ccHead;

	do {
		rc = ccToUse->isAddressInReleasedMetaDataBounds(currentThread, address);
		ccToUse = ccToUse->getNext();
	} while ((false == rc) && (NULL != ccToUse));

	return rc;
}

/**
 *  Get the unique ID of a pre-requisite cache at a lower layer
 *
 *	@param [in] currentThread The current JVM thread
 *	@param [in] the cache directory
 *	@param [in] ccToUse The current cache that depends on lower layer cache.
 *	@param [in] startupForStats If the cache is started up for cache statistics
 *	@param [out] the Unique ID of a pre-requisite cache.
 *	@param [out] the length of the Unique ID string.
 *	@param [out] true if the unique id of pre-requisite cache is found, false otherwise.
 *
 *	@return 1 This cache depends on a lower layer cache. The unique ID of the pre-requisite cache is found in ccToUse as metadata or needs to be stored as metadata.
 *			0 This cache does not depend on a low layer cache.
 *			CM_CACHE_CORRUPT if cache is corrupted.
 *			CM_READ_CACHE_FAILED if failed to get the existing pre-requisite cache ID from metadata.
 *			CM_CACHE_STORE_PREREQ_ID_FAILED if failed to store the pre-requisite cache ID
 *
 *
 */
IDATA
SH_CacheMap::getPrereqCache(J9VMThread* currentThread, const char* cacheDir, SH_CompositeCacheImpl* ccToUse, bool startupForStats, const char** prereqCacheID, UDATA* idLen, bool *isCacheUniqueIdStored)
{
	ShcItem* it = NULL;
	IDATA result = 0;
	SH_Manager* manager = NULL;
	bool isReadOnly = ccToUse->isRunningReadOnly();
	IDATA rc = 0;
	PORT_ACCESS_FROM_PORT(_portlib);

	Trc_SHR_Assert_True(ccToUse->hasWriteMutex(currentThread));

	if (UnitTest::CORRUPT_CACHE_TEST == UnitTest::unitTest) {
		return 0;
	}

	/* If there is a pre-requisite cache, then re-requites cache ID is the first metadate item */
	it = (ShcItem*)ccToUse->nextEntry(currentThread, NULL);
	if (it) {
		UDATA itemType = ITEMTYPE(it);
		if ((itemType <= TYPE_UNINITIALIZED) || (itemType > MAX_DATA_TYPES)) {
			CACHEMAP_TRACE1(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_READ_CORRUPT_DATA, it);
			if ((false == startupForStats)
				&& (false == isReadOnly)
			) {
				ccToUse->setCorruptCache(currentThread, ITEM_TYPE_CORRUPT, (UDATA)it);
			}
			reportCorruptCache(currentThread, ccToUse);
			Trc_SHR_CM_getPrereqCache_InvalidType(currentThread, it);
			result = CM_CACHE_CORRUPT;
		} else if (TYPE_PREREQ_CACHE == itemType) {
			const J9UTF8* scopeUTF8 = (const J9UTF8*)ITEMDATA(it);
			*prereqCacheID = (const char*)J9UTF8_DATA(scopeUTF8);
			*idLen = J9UTF8_LENGTH(scopeUTF8);
			*isCacheUniqueIdStored = true;
			Trc_SHR_CM_getPrereqCache_Found(currentThread, J9UTF8_LENGTH(scopeUTF8), J9UTF8_DATA(scopeUTF8));
			result = 1;
		} else {
			Trc_SHR_CM_getPrereqCache_NotFound(currentThread);
			result = 0;
			/* There is no pre-requisite cache. Sets ccToUse->nextEntry() pointer back to the start of the cache. */
			ccToUse->findStart(currentThread);
		}

		if (result > 0) {
			/* A pre-requisite cache ID is found */
			rc = getAndStartManagerForType(currentThread, itemType, &manager);

			if (rc == -1) {
				/* failed to start the manager */
				Trc_SHR_CM_getPrereqCache_Failed_To_Start_Manager(currentThread);
				result = CM_READ_CACHE_FAILED;
			} else if ((rc > 0) && ((UDATA)rc == itemType)) {
				/* Success - we have a started manager */
				if (manager->storeNew(currentThread, it, ccToUse)) {
					/* do nothing */
				} else {
					CACHEMAP_TRACE(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_CM_HASHTABLE_ADD_FAILURE);
					Trc_SHR_CM_getPrereqCache_Failed_To_Add_ID(currentThread);
					result = CM_READ_CACHE_FAILED;
				}
			} else {
				/* We found a manager, but for the wrong data type */
				Trc_SHR_Assert_ShouldNeverHappen();
				result = CM_READ_CACHE_FAILED;
			}
			ccToUse->doneReadUpdates(currentThread, 1);
		}
	} else if (!startupForStats) {
		I_8 layer = _sharedClassConfig->layer;	
		if (0 == layer || isReadOnly) {
			result = 0;
		} else {
			/* A new layer is created and the CacheUniqueID of pre-requisite cache is not stored */
			result = 1;
		}
	}

	return result;
}

/**
 *  Store the actual cache unique ID in the previous cache 
 *
 *	@param [in] currentThread The current JVM thread
 *	@param [in] the cache directory
 *	@param [in] createtime The cache create time which is stored in OSCache_header2.
 *	@param [in] metadataBytes  The size of the metadata section of current oscache.
 *	@param [in] classesBytes  The size of the classes section of current oscache.
 *	@param [in] lineNumTabBytes  The size of the line number table section of current oscache.
 *	@param [in] varTabBytes  The size of the variable table section of current oscache.
 *	@param [out] the Unique ID of a pre-requisite cache.
 *	@param [out] the length of the Unique ID string.
 *
 *	@return 1 This cache depends on a lower layer cache. The unique ID of the pre-requisite cache is found in or stored to this cache as metadata.
 *			0 This cache does not depend on a low layer cache.
 *			CM_CACHE_CORRUPT if cache is corrupted.
 *			CM_READ_CACHE_FAILED if failed to get the existing pre-requisite cache ID from metadata.
 *			CM_CACHE_STORE_PREREQ_ID_FAILED if failed to store the pre-requisite cache ID
 *
 *
 */
IDATA
SH_CacheMap::storeCacheUniqueID(J9VMThread* currentThread, const char* cacheDir, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes, const char** prereqCacheID, UDATA* idLen)
{
	IDATA result = 0;

	if (UnitTest::CORRUPT_CACHE_TEST == UnitTest::unitTest) {
		return 0;
	}

	SH_ScopeManager* localSCM = getScopeManager(currentThread);
	if (NULL == localSCM) {
		Trc_SHR_CM_storeCacheUniqueID_Failed_To_Get_Manager(currentThread);
		result = CM_CACHE_STORE_PREREQ_ID_FAILED;
		return result;
	}

	const J9UTF8* tokenKey = NULL;
	char utfKey[J9SHR_UNIQUE_CACHE_ID_BUFSIZE + sizeof(J9UTF8)];
	char key[J9SHR_UNIQUE_CACHE_ID_BUFSIZE];
	char* utfKeyPtr = (char*)&utfKey;

	I_8 layer = _sharedClassConfig->layer;
	if (0 == layer) {
		result = 0;
		return result;
	}

	Trc_SHR_CM_storeCacheUniqueID_generateCacheUniqueID_before(currentThread, createtime, metadataBytes, classesBytes, lineNumTabBytes, varTabBytes);
	UDATA keylen = SH_OSCache::generateCacheUniqueID(currentThread, cacheDir, _cacheName, layer - 1, getCacheTypeFromRuntimeFlags(*_runtimeFlags), key, sizeof(key), createtime, metadataBytes, classesBytes, lineNumTabBytes, varTabBytes);
	Trc_SHR_CM_storeCacheUniqueID_generateCacheUniqueID_after(currentThread, keylen, key);

	J9UTF8* utfKeyStruct = (J9UTF8*)utfKeyPtr;
	J9UTF8_SET_LENGTH(utfKeyStruct, (U_16)keylen);
	memcpy((char*)J9UTF8_DATA(utfKeyStruct), key, keylen);

	tokenKey = addScopeToCache(currentThread, utfKeyStruct, TYPE_PREREQ_CACHE);
	if (NULL == tokenKey) {
		Trc_SHR_CM_storeCacheUniqueID_Failed_To_Store_Prereq_UniqueID(currentThread, J9UTF8_LENGTH(tokenKey), (char*)J9UTF8_DATA(tokenKey));
		result = CM_CACHE_STORE_PREREQ_ID_FAILED;
		return result;
	}

	result = 1;
	*prereqCacheID = (const char*)J9UTF8_DATA(tokenKey);
	*idLen = J9UTF8_LENGTH(tokenKey);

	return result;
}

/**
 *	Check if an address range is in the shared cache
 *
 *	@param [in] address The start of the address range
 *	@param [in] length The length of the address range
 *	@param [in] includeHeaderReadWriteArea Whether search the address in cache header and readWrite area
 *	@param [in] useCcHeadOnly Check the address range in _ccHead (the top layer cache) only or in all layers.
 *
 *	@return true if the address range is in the shared cache. False otherwise.
 */
bool
SH_CacheMap::isAddressInCache(const void *address, UDATA length, bool includeHeaderReadWriteArea, bool useCcHeadOnly) const
{
	bool rc = false;
	SH_CompositeCacheImpl* ccToUse = _ccHead;

	do {
		bool rc1 = ccToUse->isAddressInCache(address, includeHeaderReadWriteArea);
		bool rc2 = true;
		if (0 != length) {
			rc2 = ccToUse->isAddressInCache((void*)((UDATA)address + length), includeHeaderReadWriteArea);
		}
		rc = (rc1 && rc2);
		ccToUse = ccToUse->getNext();
	} while (!rc && !useCcHeadOnly && (NULL != ccToUse));

	return rc;
}

/**
 *  Set the _cacheAddressRangeArray
 */
void
SH_CacheMap::setCacheAddressRangeArray(void)
{
	_numOfCacheLayers = 0;
	SH_CompositeCacheImpl* ccToUse = _ccTail;
	do {
		Trc_SHR_Assert_True(_numOfCacheLayers <= J9SH_LAYER_NUM_MAX_VALUE);
		_cacheAddressRangeArray[_numOfCacheLayers].cacheBase = (void*)ccToUse->getBaseAddress();
		_cacheAddressRangeArray[_numOfCacheLayers].cacheEnd = (void*)ccToUse->getCacheEndAddress();
		ccToUse = ccToUse->getPrevious();
		_numOfCacheLayers += 1;
	} while (NULL != ccToUse);
	_numOfCacheLayers -= 1;
}

/**
 *	Helper method to turn an address into J9ShrOffset using _cacheAddressRangeArray
 *
 *	@param [in] address An address in the shared cache.
 *	@param [in] offsetFromBaseAddress Whether it is offset from cache base address (CASEGSTART)
 *	@param [out] offset The corresponding J9ShrOffset from the address.
 */
void
SH_CacheMap::getJ9ShrOffsetFromAddress(const void* address, bool offsetFromBaseAddress, J9ShrOffset* offset)
{
	if (UnitTest::OSCACHE_TEST == UnitTest::unitTest || UnitTest::SHAREDCACHE_API_TEST == UnitTest::unitTest) {
		if (NULL == _cacheAddressRangeArray[0].cacheBase) {
			setCacheAddressRangeArray();
		}
	}
	for (U_32 layer = 0; layer <= _numOfCacheLayers; layer++) {
		if ((_cacheAddressRangeArray[layer].cacheBase <= address)
			&& (address <= _cacheAddressRangeArray[layer].cacheEnd)
		) {
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
			offset->cacheLayer = layer;
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
			if (offsetFromBaseAddress) {
				offset->offset = (U_32)((U_8*)address - (U_8*)_cacheAddressRangeArray[layer].cacheBase);
			} else {
				offset->offset = (U_32)((U_8*)address - (U_8*)_cacheAddressRangeArray[layer].cacheEnd);
			}
			return;
		}
	}
	/* should never reach here */
	Trc_SHR_Assert_ShouldNeverHappen();
}

/**
 *	Helper method to turn a J9ShrOffset into an address in using _cacheAddressRangeArray
 *
 *	@param [in] offset A J9ShrOffset pointer.
 *
 *	@return The corresponding address from J9ShrOffset.
 */
void*
SH_CacheMap::getAddressFromJ9ShrOffset(const J9ShrOffset* offset)
{
	if (UnitTest::OSCACHE_TEST == UnitTest::unitTest) {
		if (NULL == _cacheAddressRangeArray[0].cacheBase) {
			setCacheAddressRangeArray();
		}
	}
	U_8* address = NULL;
	U_32 layer = 0;
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	layer = offset->cacheLayer;
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
	if (offset->offset >= 0) {
		address = (U_8*)_cacheAddressRangeArray[layer].cacheBase + offset->offset;
	} else {
		address = (U_8*)_cacheAddressRangeArray[layer].cacheEnd + offset->offset;
	}
	return address;
}

/**
 *	Get the byte data given the ByteDataWrapper
 *
 *	@param [in] bdw A ByteDataWrapper
 *
 *	@return The address pointed by externalBlockOffset field of ByteDataWrapper
 */
U_8*
SH_CacheMap::getDataFromByteDataWrapper(const ByteDataWrapper* bdw)
{
	U_8* ret =((U_8*)(bdw)) + sizeof(ByteDataWrapper);

	if (
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
		(0 != bdw->externalBlockOffset.cacheLayer) || 
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
		(0 != bdw->externalBlockOffset.offset)
	) {
		ret = (U_8*)getAddressFromJ9ShrOffset(&(bdw->externalBlockOffset));
	}
	return ret;
}

bool
SH_CacheMap::resizeCache(J9VMThread* currentThread, U_32 newSize)
{
	bool rc = false;
	bool tmpFileCreated = false;
	bool tmpFileClosed = false;
	PORT_ACCESS_FROM_VMC(currentThread);
	char cacheTempFileName[J9SH_MAXPATH + sizeof(J9_SCC_TMP_FILE_EXT) - 1];
	const char* cacheFileName = _ccHead->getCacheFullPathName();
	I_32 mode = J9SH_CACHE_FILE_MODE_DEFAULTDIR_WITHOUT_GROUPACCESS;
	IDATA lockRc1 = -1;
	IDATA lockRc2 = -1;
	I_32 lockRc3 = -1;
	IDATA fd = 0;
	J9FileStat statBuf = {0};
	UDATA readWriteBytes = 0;
	I_32 existingCacheFileSize = 0;
	I_32 dbgRegionSize = 0;
	I_32 existingFreeSpace = 0;
	I_32 existingFreeDbgSpace = 0;
	I_32 totalSizeToChange = 0;
	I_32 freeSpaceToChange = 0; 
	I_32 freeDebugSpaceToChange = 0; 
	I_32 align = (0 != _ccHead->getOSPageSize()) ? (I_32)_ccHead->getOSPageSize() : 0x1000;
	
	if (_ccHead->isRunningReadOnly()) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_READONLY_CACHE);
		goto done;
	}

	if (0 == j9file_stat(cacheFileName, 0, &statBuf)) {
		if (1 == statBuf.perm.isGroupReadable) {
			mode += 40;
		}
		if (1 == statBuf.perm.isGroupWriteable) {
			mode += 20;
		}
	} else {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();

		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
		Trc_SHR_Assert_True(errormsg != NULL);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_FILE_STAT);
		goto done;
	}
	if (newSize > MAX_CC_SIZE) {
		newSize = MAX_CC_SIZE;
	} else if (MIN_CC_SIZE > newSize) {
		newSize = MIN_CC_SIZE;
	}

	lockRc1 = _ccHead->enterWriteMutex(currentThread, false, "SH_CacheMap::resizeCache");

	readWriteBytes = _ccHead->getReadWriteBytes();
	if (readWriteBytes > 0) {
		/* readWriteBytes can be 0, in such case, do not need to call enterReadWriteAreaMutex() */
		UDATA ignore;
		bool readonly = true;
		/* 
		 * Pass readonly = true to enterReadWriteAreaMutex().
		 * This is because
		 * 1. We won't write anything into the RW area.
		 * 2. If we pass in readonly = false, _theca->readWriteCrashCntr will be inremented
		 * by 1. It is decremented when the RW mutex is release. However, it is possible that this JVM
		 * could crash holding the RW mutex (before releasing it). This will leave an incorrect
		 * readWriteCrashCntr value in the cache header, making other JVMs treat the shared StringTable as corrupted,
		 * which is not true.
		 */ 
		lockRc2 = _ccHead->enterReadWriteAreaMutex(currentThread, readonly, &ignore, &ignore);
	}

	if ((lockRc1 < 0)
		|| ((lockRc2 < 0) && (readWriteBytes > 0))
	) {
		Trc_SHR_CM_resizeCache_Error_Enter_Mutex(currentThread, lockRc1, lockRc2, readWriteBytes);
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_ENTER_MUTEX);
		goto done;
	} 
	updateRuntimeFullFlags(currentThread);
	/* get the sizes inside locks */
	existingCacheFileSize = (I_32)_ccHead->getTotalSize();
	dbgRegionSize = (I_32)_ccHead->getDebugBytes();
	totalSizeToChange = newSize - existingCacheFileSize;
	
	if (abs(totalSizeToChange) <= align) {
		/* ignore the resize request */
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_SIZE_TO_CHANGE_TOO_SMALL);
		goto done;
	}

	freeDebugSpaceToChange = (I_32)((I_64)totalSizeToChange * (I_64)dbgRegionSize/existingCacheFileSize);
	freeSpaceToChange = totalSizeToChange - freeDebugSpaceToChange;
	if (totalSizeToChange < 0) {
		if (J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL)) {
			/* cache is already full, no point to shrink */
			CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_SHRINK_FULL_CACHE);
			goto done;
		}
		existingFreeSpace = (I_32)_ccHead->getFreeBytes();
		if (existingFreeSpace + freeSpaceToChange < CC_MIN_SPACE_BEFORE_CACHE_FULL) {
			Trc_SHR_Assert_True(existingFreeSpace >= CC_MIN_SPACE_BEFORE_CACHE_FULL);
			freeSpaceToChange = CC_MIN_SPACE_BEFORE_CACHE_FULL - existingFreeSpace;
		}
		existingFreeDbgSpace = (I_32)_ccHead->getFreeDebugSpaceBytes();
		if (existingFreeDbgSpace + freeDebugSpaceToChange < 0) {
			freeDebugSpaceToChange = -existingFreeDbgSpace;
		}
		align = -align;
	} else {
		existingFreeSpace = (I_32)_ccHead->getFreeBytes();
		if (existingFreeSpace + freeSpaceToChange < CC_MIN_SPACE_BEFORE_CACHE_FULL) {
			Trc_SHR_Assert_True(J9_ARE_ALL_BITS_SET(*_runtimeFlags, J9SHR_RUNTIMEFLAG_BLOCK_SPACE_FULL));
			/* freeSpaceToChange is going to be round down to align. We are increasing the cache size here.
			 * Make sure existingFreeSpace + freeSpaceToChange is always >= CC_MIN_SPACE_BEFORE_CACHE_FULL
			 * There is an assertion on this a few lines down. 
			 */
			freeSpaceToChange = CC_MIN_SPACE_BEFORE_CACHE_FULL - existingFreeSpace + align;
		}
	}
	if ((0 != freeSpaceToChange) && (0 != freeDebugSpaceToChange)) {
		/* freeSpaceToChange and freeDebugSpaceToChange must have the same sign, both positive or both negative.
		 * No need to worry about the signs if one of them is 0. 
		 */
		Trc_SHR_Assert_True((freeSpaceToChange > 0) == (freeDebugSpaceToChange > 0));
	}
	freeSpaceToChange = ROUND_DOWN_TO(align, freeSpaceToChange);
	freeDebugSpaceToChange = ROUND_DOWN_TO(align, freeDebugSpaceToChange);
	if ((0 == freeSpaceToChange) 
		&& (0 == freeDebugSpaceToChange)
	) {
		/* Nothing to resize. Ignore the resize request */
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_SIZE_TO_CHANGE_TOO_SMALL);
		goto done;
	}

	newSize = existingCacheFileSize + freeSpaceToChange + freeDebugSpaceToChange;
	Trc_SHR_Assert_True((existingFreeSpace + freeSpaceToChange >= CC_MIN_SPACE_BEFORE_CACHE_FULL));
	j9str_printf(PORTLIB, cacheTempFileName, J9SH_MAXPATH + sizeof(J9_SCC_TMP_FILE_EXT) - 1, "%s%s", cacheFileName, J9_SCC_TMP_FILE_EXT);

#if defined(WIN32) || defined(WIN64)
	/* EsOpenShareDelete on Windows allows files to be deleted/renamed while they are still open, which is in line with unix semantics. */
	fd = j9file_open(cacheTempFileName, EsOpenCreate | EsOpenWrite | EsOpenShareDelete, mode);
#else /* defined(WIN32) || defined(WIN64) */
	fd = j9file_open(cacheTempFileName, EsOpenCreate | EsOpenWrite, mode);
#endif /* defined(WIN32) || defined(WIN64) */

	if (-1 == fd) {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();

		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
		Trc_SHR_Assert_True(errormsg != NULL);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_OPEN_FILE, cacheTempFileName);
		goto done;
	}
	tmpFileCreated = true;
	tmpFileClosed = false;
	lockRc3 = j9file_lock_bytes(fd, J9PORT_FILE_WRITE_LOCK | J9PORT_FILE_WAIT_FOR_LOCK, 0, newSize);
	if (lockRc3 < 0) {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();
		
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
		Trc_SHR_Assert_True(errormsg != NULL);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_LOCK_FILE, cacheTempFileName);
		goto done;
	}

	if (0 != j9file_set_length(fd, newSize)) {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
		Trc_SHR_Assert_True(errormsg != NULL);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_SET_FILE_LEN, cacheTempFileName);
		goto done;
	}

	if (!_ccHead->fixAndWriteOSCacheHeader(currentThread, fd, freeSpaceToChange + freeDebugSpaceToChange)) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_WRITE_OS_CACHE_HEADER);
		goto done;
	}
	if (!_ccHead->fixAndWriteJ9SharedCacheHeader(currentThread, fd, freeDebugSpaceToChange, freeSpaceToChange)) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_WRITE_J9SHAREDCACHE_HEADER);
		goto done;
	}
	if (!_ccHead->fixAndWriteDebugAreaAndClassSegment(currentThread, fd, freeDebugSpaceToChange)) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_WRITE_DEBUG_AREA_CLASS_SEG);
		goto done;
	}
	if (!_ccHead->writeReadWriteArea(currentThread, fd)) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_WRITE_RW_AREA);
		goto done;
	}
	if (!_ccHead->writeMetaDataArea(currentThread, fd)) {
		CACHEMAP_PRINT(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_WRITE_METADATA_AREA);
		goto done;
	}

	if (0 == lockRc2) {
		_ccHead->exitReadWriteAreaMutex(currentThread, J9SHR_STRING_POOL_OK);
		lockRc2 = -1;
	}
	_ccHead->exitWriteMutex(currentThread, "SH_CacheMap::resizeCache");
	lockRc1 = -1;
	j9file_close(fd);
	tmpFileClosed = true;
	/* show NLS message JVMSHRC806I about cache being deleted only when sub-option "verbose" is used. */
	if (0 != _ccHead->deleteCache(currentThread, J9_ARE_NO_BITS_SET(_verboseFlags, J9SHR_VERBOSEFLAG_ENABLE_VERBOSE))) {
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_DELETE, cacheFileName);
		goto done;
	}
	if (0 == j9file_move(cacheTempFileName, cacheFileName)) {
		rc = true;
	} else {
		I_32 errorno = j9error_last_error_number();
		const char * errormsg = j9error_last_error_message();

		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_NUMBER, errorno);
		Trc_SHR_Assert_True(errormsg != NULL);
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_PORT_ERROR_MESSAGE, errormsg);
		CACHEMAP_PRINT2(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_ERROR_FILE_MOVE, cacheTempFileName, cacheFileName);
	}
done:
	if (!rc) {
		if (tmpFileCreated) {
			j9file_unlink(cacheTempFileName);
			if (!tmpFileClosed) {
				j9file_close(fd);
			}
		}
		if (0 == lockRc2) {
			_ccHead->exitReadWriteAreaMutex(currentThread, J9SHR_STRING_POOL_OK);
		}
		if (0 == lockRc1) {
			_ccHead->exitWriteMutex(currentThread, "SH_CacheMap::resizeCache");
		}
		CACHEMAP_PRINT1(J9NLS_ERROR, J9NLS_SHRC_CM_RESIZE_FAILED_TO_RESIZE, _cacheName);
	} else {
		CACHEMAP_PRINT2(J9NLS_INFO, J9NLS_SHRC_CM_RESIZE_SUCCEED_TO_RESIZE, _cacheName, newSize);
	}
	return rc;
}

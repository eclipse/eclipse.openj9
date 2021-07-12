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

extern "C"
{
#include "shrinit.h"
#if !defined(WIN32)
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#endif
}

#include "SCStoreTransaction.hpp"
#include "CacheMap.hpp"
#include "CompositeCacheImpl.hpp"
#include "CompositeCache.cpp"
#include "OSCacheFile.hpp"
#include "OSCachesysv.hpp"
#include "CacheLifecycleManager.hpp"
#include "UnitTest.hpp"
#include "sharedconsts.h"
#include "j9port.h"
#include "main.h"
#include "j9.h"
#include "hookhelpers.hpp"

#define CACHE_SIZE (16 * 1024 * 1024)
#define ROMCLASS_SIZE (4 * 1024)
#define INVALID_EYECATCHER "XXXX"
#define INVALID_EYECATCHER_LENGTH 4
#define BROKEN_TEST_CACHE "BrokenTestCache"

#define ERRPRINTF(args) \
do { \
	j9tty_printf(PORTLIB, "\tERROR: %s(%d) %s ", __FILE__, __LINE__, testName); \
	j9tty_printf(PORTLIB, args); \
} while (0)

typedef enum CorruptionStage {
	INVALID_STAGE,
	DURING_STARTUP,
	AFTER_STARTUP
} CorruptionStage;

typedef enum CorruptionType {
	INVALID_CORRUPTION = 0,
	CACHE_CRC_INVALID_TYPE,
	WALK_ROMCLASS_CORRUPT_CASE1_TYPE,
	WALK_ROMCLASS_CORRUPT_CASE2_TYPE,
	ITEM_TYPE_CORRUPT_UNINITIALIZED_TYPE,
	ITEM_TYPE_CORRUPT_MAX_DATA_TYPES_TYPE,
	ITEM_LENGTH_CORRUPT_TYPE,
	ITEM_LENGTH_CORRUPT_TYPE_LEN2LONG,
	ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE,
	CACHE_HEADER_INCORRECT_DATA_LENGTH_TYPE,
	CACHE_HEADER_INCORRECT_DATA_START_ADDRESS_TYPE,
	CACHE_HEADER_BAD_EYECATCHER_TYPE,
	CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE,
	ACQUIRE_HEADER_WRITE_LOCK_FAILED_TYPE,
	CACHE_SIZE_INVALID_TYPE,
	UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE,
	UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE,
	BAD_FREESPACE_DEBUG_AREA,
	BAD_FREESPACE_SIZE_DEBUG_AREA,
	BAD_LVT_BOUNDS_DEBUG_AREA,
	BAD_LNT_BOUNDS_DEBUG_AREA,
	CACHE_DATA_NULL_TYPE,
	CACHE_SEMAPHORE_MISMATCH_TYPE,
	CACHE_CCINITCOMPLETE_UNINITIALIZED,
	NUM_CORRUPTION_TYPE
} CorruptionType;

struct CorruptionInfo {
	CorruptionType corruptionType;
	CorruptionStage corruptionStage;
} corruptionInfo[] = {
	{ INVALID_CORRUPTION, INVALID_STAGE },
	{ CACHE_CRC_INVALID_TYPE, DURING_STARTUP },
	{ WALK_ROMCLASS_CORRUPT_CASE1_TYPE, AFTER_STARTUP },
	{ WALK_ROMCLASS_CORRUPT_CASE2_TYPE, AFTER_STARTUP },
	{ ITEM_TYPE_CORRUPT_UNINITIALIZED_TYPE, AFTER_STARTUP },
	{ ITEM_TYPE_CORRUPT_MAX_DATA_TYPES_TYPE, AFTER_STARTUP },
	{ ITEM_LENGTH_CORRUPT_TYPE, AFTER_STARTUP },
	{ ITEM_LENGTH_CORRUPT_TYPE_LEN2LONG, AFTER_STARTUP },
	{ ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE, AFTER_STARTUP },
	{ CACHE_HEADER_INCORRECT_DATA_LENGTH_TYPE, DURING_STARTUP },
	{ CACHE_HEADER_INCORRECT_DATA_START_ADDRESS_TYPE, DURING_STARTUP },
	{ CACHE_HEADER_BAD_EYECATCHER_TYPE, DURING_STARTUP },
	{ CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE, DURING_STARTUP },
	{ ACQUIRE_HEADER_WRITE_LOCK_FAILED_TYPE, DURING_STARTUP },
	{ CACHE_SIZE_INVALID_TYPE, DURING_STARTUP },
	{ BAD_FREESPACE_DEBUG_AREA, DURING_STARTUP },
	{ BAD_FREESPACE_SIZE_DEBUG_AREA, DURING_STARTUP },
	{ BAD_LVT_BOUNDS_DEBUG_AREA, DURING_STARTUP },
	{ BAD_LNT_BOUNDS_DEBUG_AREA, DURING_STARTUP },
	{ UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE, AFTER_STARTUP },
	{ UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE, AFTER_STARTUP },
	{ CACHE_DATA_NULL_TYPE, DURING_STARTUP },
	{ CACHE_SEMAPHORE_MISMATCH_TYPE, DURING_STARTUP },
	{ CACHE_CCINITCOMPLETE_UNINITIALIZED, DURING_STARTUP },
};

/*Some strings to make the test output easier to read*/
static const char * CorruptionTypeStrings[] = {
	" ------- CORRUPTION TYPE START ------- ",
	"CACHE_CRC_INVALID_TYPE",
	"WALK_ROMCLASS_CORRUPT_CASE1_TYPE",
	"WALK_ROMCLASS_CORRUPT_CASE2_TYPE",
	"ITEM_TYPE_CORRUPT_UNINITIALIZED_TYPE",
	"ITEM_TYPE_CORRUPT_MAX_DATA_TYPES_TYPE",
	"ITEM_LENGTH_CORRUPT_TYPE",
	"ITEM_LENGTH_CORRUPT_TYPE_LEN2LONG",
	"ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE",
	"CACHE_HEADER_INCORRECT_DATA_LENGTH_TYPE",
	"CACHE_HEADER_INCORRECT_DATA_START_ADDRESS_TYPE",
	"CACHE_HEADER_BAD_EYECATCHER_TYPE",
	"CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE",
	"ACQUIRE_HEADER_WRITE_LOCK_FAILED_TYPE",
	"CACHE_SIZE_INVALID_TYPE",
	"UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE",
	"UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE",
	"BAD_FREESPACE_DEBUG_AREA",
	"BAD_FREESPACE_SIZE_DEBUG_AREA",
	"BAD_LVT_BOUNDS_DEBUG_AREA",
	"BAD_LNT_BOUNDS_DEBUG_AREA",
	"CACHE_DATA_NULL_TYPE",
	"CACHE_SEMAPHORE_MISMATCH_TYPE",
	"CACHE_CCINITCOMPLETE_UNINITIALIZED",
	"NUM_CORRUPTION_TYPE",
	" ------- CORRUPTION TYPE END ------- "
};

extern "C" {
	IDATA testCorruptCache(J9JavaVM* vm);
}

IDATA addDummyROMClass(J9JavaVM *vm, const char *romClassName, IDATA corruptionType);

class CorruptCacheTest {
private:
	typedef char* BlockPtr;
public:
	J9SharedClassPreinitConfig *piConfig, *origPiConfig;
	J9SharedClassConfig *sharedClassConfig, *origSharedClassConfig;

	IDATA corruptCache(J9JavaVM *vm, I_32 cacheType, IDATA corruptionType);
	IDATA openTestCache(J9JavaVM *vm, I_32 cacheType, I_32 cacheSize,
			U_64 extraRunTimeFlag=0,
			U_64 unsetTheseRunTimeFlag=(J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES),
			UDATA extraVerboseFlag = J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT,
			const char * testName="openTestCache",
			bool startupWillFail=false,
			bool doCleanupOnFail=true);
	IDATA closeTestCache(J9JavaVM *vm, I_32 cacheType, bool saveCRC);
	IDATA openCorruptCache(J9JavaVM *vm, I_32 cacheType, bool readOnly, I_32 cacheSize, U_64 extraRunTimeFlag = 0);
	IDATA closeAndRemoveCorruptCache(J9JavaVM *vm, I_32 cacheType);
	IDATA verifyCorruptionContext(J9JavaVM *vm, IDATA corruptionType);
	IDATA isCorruptionContextReset(J9JavaVM *vm);
	IDATA getSemId(J9JavaVM *vm);
	IDATA findDummyROMClass(J9JavaVM *vm, const char *romClassName);
};

IDATA
CorruptCacheTest::corruptCache(J9JavaVM *vm, I_32 cacheType, IDATA corruptionType) {
	const char *testName = "corruptCache";
	IDATA rc = PASS;
	SH_CompositeCacheImpl *cc;
	SH_CacheMap *cacheMap;
	J9SharedCacheHeader *ca;
	J9ROMClass *romClass;
	ShcItemHdr *ih;
	ShcItem item;
	ShcItem *itemPtr;
	void *header;
	OSCache_header_version_current *oscHeader;
	U_8 *areaStart, *endOfROMSegment;
	U_32 areaSize;

	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == cacheType) {
		header = (void *)((UDATA)cc->getCacheHeaderAddress() - SH_OSCachesysv::getHeaderSize());
		oscHeader = &((OSCachesysv_header_version_current *)header)->oscHdr;
	} else {
		header = (void *)((UDATA)cc->getCacheHeaderAddress() - MMAP_CACHEHEADERSIZE);
		oscHeader = &((OSCachemmap_header_version_current *)header)->oscHdr;
	}

	switch (corruptionType) {
	case CACHE_HEADER_INCORRECT_DATA_LENGTH_TYPE:
		/* set dataLength to invalid value */
		oscHeader->dataLength = 0;
		break;
	case CACHE_HEADER_INCORRECT_DATA_START_ADDRESS_TYPE:
		/* set dataStart to invalid value */
		oscHeader->dataStart = -1;
		break;
	case CACHE_HEADER_BAD_EYECATCHER_TYPE:
		/* set eye catcher to invalid value */
		if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == cacheType) {
			memcpy(((OSCachesysv_header_version_current *)header)->eyecatcher, INVALID_EYECATCHER, INVALID_EYECATCHER_LENGTH);
		} else {
			memcpy(((OSCachemmap_header_version_current *)header)->eyecatcher, INVALID_EYECATCHER, INVALID_EYECATCHER_LENGTH);
		}
		break;
	case CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE:
		/* set cache size field in header to invalid value */
		oscHeader->size = 0;
		break;
	case CACHE_CRC_INVALID_TYPE:
		/* zero out data part of cache to get invalid CRC */
		ca = cc->getCacheHeaderAddress();
		areaStart = (U_8*)CASEGSTART(ca);
		areaSize = (U_32)((U_8*)SEGUPDATEPTR(ca) - areaStart);
		memset(areaStart, 0, areaSize);
		areaStart = (U_8*)UPDATEPTR(ca);
		areaSize = (U_32)((U_8*)CAEND(ca) - areaStart);
		memset(areaStart, 0, areaSize);
		break;
	case WALK_ROMCLASS_CORRUPT_CASE1_TYPE:
		romClass = (J9ROMClass *)cc->getBaseAddress();

		/* set romclass size to zero */
		romClass->romSize = 0;
		break;
	case WALK_ROMCLASS_CORRUPT_CASE2_TYPE:
		endOfROMSegment = (U_8*)cc->getSegmentAllocPtr();
		romClass = (J9ROMClass *)cc->getBaseAddress();

		/* set romclass size more than ROM segment size */
		romClass->romSize = (U_32)(endOfROMSegment - (U_8*)romClass)*2;
		break;
	case ITEM_TYPE_CORRUPT_UNINITIALIZED_TYPE:
		/* set first cache entry type to TYPE_UNINITIALIZED */
		ca = cc->getCacheHeaderAddress();
		ih = (ShcItemHdr *)CCFIRSTENTRY(ca);
		itemPtr = (ShcItem *)CCITEM(ih);
		ITEMTYPE(itemPtr) = TYPE_UNINITIALIZED;
		break;
	case ITEM_TYPE_CORRUPT_MAX_DATA_TYPES_TYPE:
		/* set first cache entry type to invalid value */
		ca = cc->getCacheHeaderAddress();
		ih = (ShcItemHdr *)CCFIRSTENTRY(ca);
		itemPtr = (ShcItem *)CCITEM(ih);
		ITEMTYPE(itemPtr) = MAX_DATA_TYPES+1;
		break;
	case ITEM_LENGTH_CORRUPT_TYPE:
		/* set header length of first cache entry to invalid value */
		ca = cc->getCacheHeaderAddress();
		ih = (ShcItemHdr *)CCFIRSTENTRY(ca);
		CCSETITEMLEN(ih, 0);
		break;

	case ITEM_LENGTH_CORRUPT_TYPE_LEN2LONG:
		/* set header length of first cache entry to invalid value */
		ca = cc->getCacheHeaderAddress();
		ih = (ShcItemHdr *)CCFIRSTENTRY(ca);
		/*Set length larger than largest possible cache*/
		CCSETITEMLEN(ih, 0xefffffff);
		break;

	case ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE:
		{
			/* Create a temporary CompositeCache to add an invalid entry in the cache resulting in corrupt cache.
			 * Then call runEntryPointChecks() on original CacheMap to detect corruption.
			 */
			SH_CompositeCacheImpl *tempCC;

			tempCC = (SH_CompositeCacheImpl *)j9mem_allocate_memory(sizeof(SH_CompositeCacheImpl), J9MEM_CATEGORY_CLASSES);
			if (NULL == tempCC) {
				ERRPRINTF("failed to allocate memory for temporary CompositeCache\n");
				rc = FAIL;
				goto _end;
			}
			memcpy((void*)tempCC, (void*)cc, sizeof(SH_CompositeCacheImpl));
			tempCC->enterWriteMutex(vm->mainThread, false, "ItemLengthCorruptType");
			itemPtr = &item;
			memset(itemPtr, 0, sizeof(ShcItem));
			itemPtr->dataLen = 8;
			itemPtr = (ShcItem *)tempCC->allocateBlock(vm->mainThread, itemPtr, SHC_WORDALIGN, 0);
			tempCC->commitUpdate(vm->mainThread, false);
			ih = (ShcItemHdr *)ITEMEND(itemPtr);
			CCSETITEMLEN(ih, 0);
			cacheMap->enterLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "corruptCache");
			cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);
			cacheMap->exitLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "corruptCache");
			tempCC->exitWriteMutex(vm->mainThread, "ItemLengthCorruptType");
			j9mem_free_memory(tempCC);
			break;
		}
	case UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE:
		{
			/* Create a temporary CorruptCacheTest object. We need whole new CacheMap object.
			 * Add a corrupt dummy ROMClass and then call runEntryPointChecks() on original CacheMap to detect corruption.
			 */
			CorruptCacheTest tempCorruptCacheTest;
			tempCorruptCacheTest.openTestCache(vm, cacheType, CACHE_SIZE);
			addDummyROMClass(vm, "CorruptDummyClass", UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE);
			tempCorruptCacheTest.closeTestCache(vm, cacheType, true);
		}
		cc->enterWriteMutex(vm->mainThread, false, "updateROMClassCorruptType");
		cacheMap->enterLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "corruptCache");
		cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);
		cacheMap->exitLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "corruptCache");
		cc->exitWriteMutex(vm->mainThread, "updateROMClassCorruptType");
		break;
	case BAD_FREESPACE_DEBUG_AREA:
		{
			UDATA tmp = 0;
			ca = cc->getCacheHeaderAddress();
			tmp = ca->lineNumberTableNextSRP;
			ca->lineNumberTableNextSRP = ca->localVariableTableNextSRP;
			ca->localVariableTableNextSRP = tmp;
			break;
		}
	case BAD_FREESPACE_SIZE_DEBUG_AREA:
		/*Point the SRP around the start of the cache*/
		ca = cc->getCacheHeaderAddress();
		ca->lineNumberTableNextSRP = 0;
		break;
	case BAD_LVT_BOUNDS_DEBUG_AREA:
		/*Point the SRP around the end of the cache*/
		ca = cc->getCacheHeaderAddress();
		ca->lineNumberTableNextSRP += ca->totalBytes;
		ca->localVariableTableNextSRP += ca->totalBytes;
		break;
	case BAD_LNT_BOUNDS_DEBUG_AREA:
		/*Point the SRP around the start of the cache*/
		ca = cc->getCacheHeaderAddress();
		ca->lineNumberTableNextSRP = sizeof(UDATA);
		ca->localVariableTableNextSRP = (ca->debugRegionSize - sizeof(UDATA));
		break;
	case CACHE_CCINITCOMPLETE_UNINITIALIZED:
		/*set ccInitComplete to 0*/
		ca = cc->getCacheHeaderAddress();
		ca->ccInitComplete = 0;
		/* Set lockedPtr and corruptFlagPtr to zero to test the code
		 * can handle NULL values for these pointers when the cache
		 * is detected corrupt.
		 */
		ca->lockedPtr = 0;
		ca->corruptFlagPtr = 0;
		break;
	}

_end:
	return rc;
}

IDATA
CorruptCacheTest::openTestCache(J9JavaVM *vm, I_32 cacheType, I_32 cacheSize, U_64 extraRunTimeFlag, U_64 unsetTheseRunTimeFlag, UDATA extraVerboseFlag, const char * testName, bool startupWillFail, bool doCleanupOnFail)
{
	void* memory;
	IDATA rc = 0;
	IDATA cacheObjectSize;
	bool cacheHasIntegrity;
	SH_CacheMap *cacheMap = NULL;

	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "%s: Opening test cache\n", testName);

	piConfig = (J9SharedClassPreinitConfig *) j9mem_allocate_memory(sizeof(J9SharedClassPreinitConfig), J9MEM_CATEGORY_CLASSES);
	if (NULL == piConfig) {
		ERRPRINTF("failed to allocate memory for J9SharedClassPreinitConfig\n");
		rc = FAIL;
		goto done;
	}
	memset(piConfig, 0, sizeof(J9SharedClassPreinitConfig));
	piConfig->sharedClassDebugAreaBytes = -1;
	piConfig->sharedClassCacheSize = cacheSize;
	piConfig->sharedClassSoftMaxBytes = -1;

	origPiConfig = vm->sharedClassPreinitConfig;
	vm->sharedClassPreinitConfig = piConfig;

	sharedClassConfig = (J9SharedClassConfig *) j9mem_allocate_memory(sizeof(J9SharedClassConfig) + sizeof(J9SharedClassCacheDescriptor), J9MEM_CATEGORY_CLASSES);
	if (NULL == sharedClassConfig) {
		ERRPRINTF("failed to allocate memory for J9SharedClassConfig\n");
		rc = FAIL;
		goto done;
	}

	memset(sharedClassConfig, 0, sizeof(J9SharedClassConfig)+sizeof(J9SharedClassCacheDescriptor));
	origSharedClassConfig = vm->sharedClassConfig;
	vm->sharedClassConfig = sharedClassConfig;

	sharedClassConfig->cacheDescriptorList = (J9SharedClassCacheDescriptor*)((UDATA)sharedClassConfig + sizeof(J9SharedClassConfig));
	sharedClassConfig->cacheDescriptorList->next = sharedClassConfig->cacheDescriptorList;
	sharedClassConfig->cacheDescriptorList->previous = sharedClassConfig->cacheDescriptorList;
	sharedClassConfig->softMaxBytes = -1;
	sharedClassConfig->minAOT = -1;
	sharedClassConfig->maxAOT = -1;
	sharedClassConfig->minJIT = -1;
	sharedClassConfig->maxJIT = -1;

	sharedClassConfig->runtimeFlags = getDefaultRuntimeFlags();
	sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_BCI;
	sharedClassConfig->runtimeFlags |= extraRunTimeFlag;
	sharedClassConfig->runtimeFlags &= ~(unsetTheseRunTimeFlag);

	sharedClassConfig->verboseFlags |= extraVerboseFlag;

	sharedClassConfig->sharedAPIObject = initializeSharedAPI(vm);
	if (NULL == sharedClassConfig->sharedAPIObject) {
		ERRPRINTF("failed to allocate sharedAPIObject\n");
		rc = FAIL;
		goto done;
	}

	sharedClassConfig->getJavacoreData = j9shr_getJavacoreData;

	cacheObjectSize = SH_CacheMap::getRequiredConstrBytes(false);
	memory = j9mem_allocate_memory(cacheObjectSize, J9MEM_CATEGORY_CLASSES);
	if (NULL == memory) {
		ERRPRINTF("failed to allocate CacheMap object\n");
		rc = FAIL;
		goto done;
	}

	cacheMap = SH_CacheMap::newInstance(vm, sharedClassConfig, (SH_CacheMap*)memory, BROKEN_TEST_CACHE, cacheType);

	sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_CACHE_INITIALIZATION_COMPLETE;
	sharedClassConfig->runtimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;
	sharedClassConfig->sharedClassCache = (void*)cacheMap;

	rc = cacheMap->startup(vm->mainThread, piConfig, BROKEN_TEST_CACHE, NULL, J9SH_DIRPERM_ABSENT, NULL, &cacheHasIntegrity);
	if (startupWillFail) {
		if (0 == rc) {
			ERRPRINTF("CacheMap.startup() passed when a fail was expected\n");
			rc = FAIL;
			goto done;
		}
	} else {
		if (0 != rc) {
			ERRPRINTF("CacheMap.startup() failed when a pass was expected\n");
			rc = FAIL;
			goto done;
		}
	}

done:
	if ((FAIL == rc) && doCleanupOnFail) {
		if (NULL != cacheMap) {
			cacheMap->cleanup(vm->mainThread);
			j9mem_free_memory(cacheMap);
		}
		if (NULL != sharedClassConfig) {
			vm->sharedClassConfig = origSharedClassConfig;
			j9mem_free_memory(sharedClassConfig);
		}
		if (NULL != piConfig) {
			vm->sharedClassPreinitConfig = origPiConfig;
			j9mem_free_memory(piConfig);
		}
	}
	j9tty_printf(PORTLIB, "%s: Done opening test cache\n", testName);
	return rc;
}

IDATA
CorruptCacheTest::closeTestCache(J9JavaVM *vm, I_32 cacheType, bool saveCRC)
{
	IDATA rc = PASS;
	SH_CompositeCacheImpl *cc;
	SH_CacheMap *cacheMap;
	J9SharedCacheHeader *ca;

	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	if (NULL != cacheMap) {
		cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
		if (cc->isStarted()) {
			ca = cc->getCacheHeaderAddress();
			if (NULL != ca) {
				/* When ::openTestCacheForStats() is called CacheMap::startup() may fail,
				 * and cc->getCacheHeaderAddress() will return NULL because the cache was not started.
				 */
				if (saveCRC) {
					/* need to explicitly call SH_CacheMap::runExitCode() to populate cache CRC */
					cacheMap->runExitCode(vm->mainThread);
					/* clear J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES flag in runtimeFlags which is set in the call to SH_CompositeCacheImpl::runExitCode() above. */
					sharedClassConfig->runtimeFlags &= (~J9SHR_RUNTIMEFLAG_DENY_CACHE_UPDATES);
				} else {
					/* set this flag even if we do not save CRC.
					 * this is required when the cache is made corrupt for CACHE_CRC_INVALID_TYPE case.
					 * we don't want to save invalid CRC in cache header, rather
					 * we want cache header CRC to be different from computed CRC so that checkCacheCRC() can fail.
					 * This is not done for realtime cache as cache CRC in never computed for it.
					 */
					 if (0 == (sharedClassConfig->runtimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_READONLY) &&
						(J9PORT_SHR_CACHE_TYPE_VMEM != cacheType)
					 ) {
						/*Leave the cache alone if we are closing after opening with enable stats*/
						ca->crcValid = CC_CRC_VALID_VALUE;
					}
				}
			}
		}
		cacheMap->cleanup(vm->mainThread);
		j9mem_free_memory(cacheMap);
	}
	if (NULL != sharedClassConfig) {
		vm->sharedClassConfig = origSharedClassConfig;
		j9mem_free_memory(sharedClassConfig);
	}
	if (NULL != piConfig) {
		vm->sharedClassPreinitConfig = origPiConfig;
		j9mem_free_memory(piConfig);
	}
	return rc;
}

IDATA
CorruptCacheTest::openCorruptCache(J9JavaVM* vm, I_32 cacheType, bool readOnly, I_32 cacheSize, U_64 extraRuntimeFlags)
{
	U_64 unsetRuntimeFlags = 0;
	UDATA extraVerboseFlags = J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT;
	bool startupWillFail = false;

	if (extraRuntimeFlags & J9SHR_RUNTIMEFLAG_ENABLE_STATS) {
		startupWillFail = true;
	}

	if (readOnly) {
		extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_READONLY;
		startupWillFail = true;
	}
	return this->openTestCache(vm, cacheType, cacheSize, extraRuntimeFlags, unsetRuntimeFlags, extraVerboseFlags, "openCorruptCache", startupWillFail, false);
}

IDATA
CorruptCacheTest::closeAndRemoveCorruptCache(J9JavaVM *vm, I_32 cacheType)
{
	IDATA rc = PASS;
	const char * testName = "closeAndRemoveCorruptCache";
	SH_CacheMap *cacheMap;
	SH_CompositeCacheImpl *cc;
	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	if (NULL != cacheMap) {
		if (cc->isStarted()) {
			cacheMap->runExitCode(vm->mainThread);
		}
		cacheMap->cleanup(vm->mainThread);
		j9mem_free_memory(cacheMap);
	}
	rc = j9shr_destroySharedCache(vm, NULL, BROKEN_TEST_CACHE, cacheType, false);
	if (NULL != sharedClassConfig) {
		vm->sharedClassConfig = origSharedClassConfig;
		j9mem_free_memory(sharedClassConfig);
	}
	if (NULL != piConfig) {
		vm->sharedClassPreinitConfig = origPiConfig;
		j9mem_free_memory(piConfig);
	}
	if (J9SH_DESTROYED_ALL_CACHE != rc) {
		ERRPRINTF("Failed to destroy corrupt cache\n");
		j9tty_printf(PORTLIB, "\tj9shr_destroySharedCache()=%d\n", rc);
		rc = FAIL;
	} else {
		j9tty_printf(PORTLIB, "Cache destroyed\n");
	}
	return rc;
}

IDATA
CorruptCacheTest::isCorruptionContextReset(J9JavaVM *vm) {
	IDATA rc = PASS;
	const char * testName = "isCorruptionContextReset";
	SH_CacheMap *cacheMap;
	IDATA corruptionCode;
	UDATA corruptValue;
	SH_CompositeCacheImpl *cc;

	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;

	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	cc->getCorruptionContext(&corruptionCode, &corruptValue);
	if ((NO_CORRUPTION != corruptionCode) || (0 != corruptValue)) {
		ERRPRINTF("Corruption Context is not reset\n");
		rc = FAIL;
		goto _end;
	}
	if (cacheMap->isCacheCorruptReported() == true) {
		ERRPRINTF("reportCorruptCache should not be called\n");
		rc = FAIL;
		goto _end;
	}

_end:
	return rc;
}

IDATA
CorruptCacheTest::verifyCorruptionContext(J9JavaVM *vm, IDATA corruptionType) {
	const char * testName = "verifyCorruptionContext";
	SH_CacheMap *cacheMap;
	SH_CompositeCacheImpl *cc;
	IDATA corruptionCode, expectedCorruptionCode;
	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;

	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	cc->getCorruptionContext(&corruptionCode, NULL);

	switch (corruptionType) {
	case CACHE_CRC_INVALID_TYPE:
		expectedCorruptionCode = CACHE_CRC_INVALID;
		break;
	case WALK_ROMCLASS_CORRUPT_CASE1_TYPE:
	case WALK_ROMCLASS_CORRUPT_CASE2_TYPE:
	case UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE:
	case UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE:
		expectedCorruptionCode = ROMCLASS_CORRUPT;
		break;
	case ITEM_TYPE_CORRUPT_UNINITIALIZED_TYPE:
	case ITEM_TYPE_CORRUPT_MAX_DATA_TYPES_TYPE:
		expectedCorruptionCode = ITEM_TYPE_CORRUPT;
		break;
	case ITEM_LENGTH_CORRUPT_TYPE:
		expectedCorruptionCode = ITEM_LENGTH_CORRUPT;
		break;
	case ITEM_LENGTH_CORRUPT_TYPE_LEN2LONG:
		expectedCorruptionCode = ITEM_LENGTH_CORRUPT;
		break;
	case ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE:
		expectedCorruptionCode = ITEM_LENGTH_CORRUPT;
		break;
	case CACHE_HEADER_INCORRECT_DATA_LENGTH_TYPE:
		expectedCorruptionCode = CACHE_HEADER_INCORRECT_DATA_LENGTH;
		break;
	case CACHE_HEADER_INCORRECT_DATA_START_ADDRESS_TYPE:
		expectedCorruptionCode = CACHE_HEADER_INCORRECT_DATA_START_ADDRESS;
		break;
	case CACHE_HEADER_BAD_EYECATCHER_TYPE:
		expectedCorruptionCode = CACHE_HEADER_BAD_EYECATCHER;
		break;
	case CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE:
		expectedCorruptionCode = CACHE_HEADER_INCORRECT_CACHE_SIZE;
		break;
	case ACQUIRE_HEADER_WRITE_LOCK_FAILED_TYPE:
		expectedCorruptionCode = ACQUIRE_HEADER_WRITE_LOCK_FAILED;
		break;
	case CACHE_SIZE_INVALID_TYPE:
		expectedCorruptionCode = CACHE_SIZE_INVALID;
		break;
	case BAD_FREESPACE_DEBUG_AREA:
		expectedCorruptionCode = CACHE_DEBUGAREA_BAD_FREE_SPACE;
		break;
	case BAD_FREESPACE_SIZE_DEBUG_AREA:
		expectedCorruptionCode = CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE;
		break;
	case BAD_LVT_BOUNDS_DEBUG_AREA:
		expectedCorruptionCode = CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO;
		break;
	case BAD_LNT_BOUNDS_DEBUG_AREA:
		expectedCorruptionCode = CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO;
		break;
	case CACHE_DATA_NULL_TYPE:
		expectedCorruptionCode = CACHE_DATA_NULL;
		break;
	case CACHE_SEMAPHORE_MISMATCH_TYPE:
		expectedCorruptionCode = CACHE_SEMAPHORE_MISMATCH;
		break;
	case CACHE_CCINITCOMPLETE_UNINITIALIZED:
		expectedCorruptionCode = CACHE_BAD_CC_INIT;
		break;
	default:
		expectedCorruptionCode = NO_CORRUPTION;
		break;
	}
	if (corruptionCode != expectedCorruptionCode) {
		ERRPRINTF("incorrect corruption context. \n");
		j9tty_printf(PORTLIB, "\tExpected code: %d\t found: %d\n", expectedCorruptionCode, corruptionCode);
		return FAIL;
	} else {
		if (cacheMap->isCacheCorruptReported() == true) {
			return PASS;
		} else {
			ERRPRINTF("corruption context is correct but cache corruption is not reported. \n");
			return FAIL;
		}
		return PASS;
	}
}

IDATA
CorruptCacheTest::getSemId(J9JavaVM *vm)
{
	SH_CacheMap *cacheMap;
	J9SharedClassJavacoreDataDescriptor descriptor;
	IDATA rc;
	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	rc = cacheMap->getJavacoreData(vm, &descriptor);
	if (0 == rc) {
		j9tty_printf(PORTLIB, "testCorruptCache: failed to get javacoredata\n");
		return -1;
	}
	return descriptor.semid;
}

IDATA
CorruptCacheTest::findDummyROMClass(J9JavaVM *vm, const char *romClassName)
{
	SH_CacheMap *cacheMap;
	J9ClassPathEntry cpEntry;
	ClasspathItem *cpi;
	const char *currentDir = ".";
	IDATA rc = PASS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* create dummy class path item to pass to findROMClass() */
	memset(&cpEntry, 0, sizeof(J9ClassPathEntry));
	cpEntry.path = (U_8 *)currentDir;
	cpEntry.extraInfo = NULL;
	cpEntry.pathLength = 1;
	cpEntry.type = CPE_TYPE_DIRECTORY;

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cacheMap->enterLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "findDummyROMClass");
	cpi = createClasspath(vm->mainThread, &cpEntry, 1, 0, CP_TYPE_CLASSPATH, 0);
	cacheMap->exitLocalMutex(vm->mainThread, vm->classMemorySegments->segmentMutex, "class segment mutex", "findDummyROMClass");
	if (NULL == cpi) {
		j9tty_printf(PORTLIB, "testCorruptCache: failed to create dummy classpath\n");
		return FAIL;
	}

	cacheMap->findROMClass(vm->mainThread, romClassName, cpi, NULL, NULL, -1, NULL);	

	return rc;
}

/* zero out complete cache */
IDATA
zeroOutCache(J9JavaVM *vm, I_32 cacheType)
{
	char baseDir[J9SH_MAXPATH];
	const char * testName = "zeroOutCache";
	char cacheName[J9SH_MAXPATH];
	char fullPath[J9SH_MAXPATH];
	J9PortShcVersion versionData;
	J9MmapHandle *mapFileHandle;
	I_64 cacheSize;
	IDATA fd;
	IDATA rc = PASS;
	U_32 flags = J9SHMEM_GETDIR_APPEND_BASEDIR;
	PORT_ACCESS_FROM_JAVAVM(vm);

#if defined(OPENJ9_BUILD)
	flags |= J9SHMEM_GETDIR_USE_USERHOME;
#endif /* defined(OPENJ9_BUILD) */

	rc = j9shmem_getDir(NULL, flags, baseDir, J9SH_MAXPATH);
	if (rc < 0) {
		ERRPRINTF("Cannot get a directory\n");
	}
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	versionData.cacheType = cacheType;
	SH_OSCache::getCacheVersionAndGen(PORTLIB, vm, cacheName, J9SH_MAXPATH, BROKEN_TEST_CACHE, &versionData, OSCACHE_CURRENT_CACHE_GEN, true, 0);
	j9str_printf(PORTLIB, fullPath, J9SH_MAXPATH, "%s%s", baseDir, cacheName);

	fd = j9file_open(fullPath, EsOpenRead | EsOpenWrite, 0644);
	if (-1 == fd) {
		rc = FAIL;
		ERRPRINTF("Failed to open cache file\n");
		goto done;
	}

	cacheSize = j9file_length(fullPath);
	if (cacheSize <= 0) {
				rc = FAIL;
				ERRPRINTF("Failed to get cache file size\n");
				goto done;
	}
	mapFileHandle = j9mmap_map_file(fd, 0, (UDATA)cacheSize, fullPath, J9PORT_MMAP_FLAG_WRITE, J9MEM_CATEGORY_CLASSES_SHC_CACHE);
	if ((NULL == mapFileHandle) || (NULL == mapFileHandle->pointer)) {
		rc = FAIL;
		ERRPRINTF("Failed to map cache file\n");
		goto done;
	}
	memset(mapFileHandle->pointer, 0, (U_32)cacheSize);
	rc = j9mmap_msync(mapFileHandle->pointer, (UDATA)cacheSize, J9PORT_MMAP_SYNC_WAIT);
	if (-1 == rc) {
		rc = FAIL;
		ERRPRINTF("Failed to msync cache file\n");
		goto done;
	}
	j9mmap_unmap_file(mapFileHandle);
	rc = j9file_close(fd);
	if (FAIL == rc) {
		ERRPRINTF("Failed to close cache file\n");
		goto done;
	}
done:
	return rc;

}

/*
 * Reduce the size of cache to less than cache header size.
 */
IDATA
truncateCache(J9JavaVM *vm, I_32 cacheType)
{
	char baseDir[J9SH_MAXPATH];
	const char * testName = "truncateCache";
	char cacheName[J9SH_MAXPATH];
	char fullPath[J9SH_MAXPATH];
	J9PortShcVersion versionData;
	IDATA fd;
	IDATA rc = PASS;
	U_32 flags = J9SHMEM_GETDIR_APPEND_BASEDIR;
	PORT_ACCESS_FROM_JAVAVM(vm);

#if defined(OPENJ9_BUILD)
	flags |= J9SHMEM_GETDIR_USE_USERHOME;
#endif /* defined(OPENJ9_BUILD) */

	rc = j9shmem_getDir(NULL, flags, baseDir, J9SH_MAXPATH);
	if (rc < 0) {
		ERRPRINTF("Cannot get a directory\n");
	}
	setCurrentCacheVersion(vm, J2SE_VERSION(vm), &versionData);
	versionData.cacheType = cacheType;
	SH_OSCache::getCacheVersionAndGen(PORTLIB, vm, cacheName, J9SH_MAXPATH, BROKEN_TEST_CACHE, &versionData, OSCACHE_CURRENT_CACHE_GEN, true, 0);
	j9str_printf(PORTLIB, fullPath, J9SH_MAXPATH, "%s%s", baseDir, cacheName);

	fd = j9file_open(fullPath, EsOpenRead | EsOpenWrite, 0644);
	if (-1 == fd) {
		rc = FAIL;
		ERRPRINTF("Failed to open cache file\n");
		goto done;
	}
	rc = j9file_set_length(fd, sizeof(OSCachemmap_header_version_current)-10);
	if (FAIL == rc) {
		ERRPRINTF("Failed to truncate cache file\n");
		goto done;
	}
	rc = j9file_close(fd);
	if (FAIL == rc) {
		ERRPRINTF("Failed to close cache file\n");
		goto done;
	}
done:
	return rc;
}

#if !defined(WIN32)
IDATA
destroySemaphore(J9JavaVM *vm, IDATA semid)
{
	IDATA rc = PASS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	rc = semctl(semid, 0, IPC_RMID);
	if (-1 == rc) {
		j9tty_printf(PORTLIB, "Error in destroying semaphore, rc = %d", rc);
		rc = FAIL;
	}
	return rc;
}
#endif

void
setRomClassName(J9JavaVM* vm, J9ROMClass * rc, const char * name)
{
	J9UTF8 * romClassNameLocation = NULL;
	romClassNameLocation = (J9UTF8 *) (((IDATA) rc) + sizeof(J9ROMClass));
	J9UTF8_SET_LENGTH(romClassNameLocation, (U_16)strlen(name));
	strcpy((char *) (J9UTF8_DATA(romClassNameLocation)), name);
	NNSRP_SET(rc->className, romClassNameLocation);
}

/*
 * Add a dummy ROMClass to the cache. Only the romSize field of the dummy ROMClass holds a valid value.
 * In case of corruptionType = UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE,
 * romSize field of the dummy ROMClass is set to invalid value after adding it to cache.
 * In case of corruptionType = UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE,
 * romSize field of the dummy ROMClass is set to invalid value after committing the updates to cache.
 */
IDATA
addDummyROMClass(J9JavaVM *vm, const char *romClassName, IDATA corruptionType) {
	J9RomClassRequirements sizes;
	J9ROMClass *romClass;
	IDATA rc = PASS;
	I_32 count;

	PORT_ACCESS_FROM_JAVAVM(vm);
	{
		SCStoreTransaction transaction(vm->mainThread, (J9ClassLoader*) NULL, 0/*entryIndex*/, J9SHR_LOADTYPE_NORMAL, (U_16)strlen(romClassName), (U_8 *)romClassName, false);
		if (transaction.isOK() == false) {
			j9tty_printf(PORTLIB, "addDummyROMClass: could not allocate the transaction object\n");
			rc = FAIL;
			goto done;
		}

		memset(&sizes, 0, sizeof(J9RomClassRequirements));
		sizes.romClassSizeFullSize = ROMCLASS_SIZE;
		sizes.romClassMinimalSize = sizes.romClassSizeFullSize;

		if (transaction.allocateSharedClass((const J9RomClassRequirements *)&sizes) == false) {
			rc = FAIL;
			goto done;
		} else {
			romClass = (J9ROMClass *)transaction.getRomClass();
		}

		if (romClass == NULL) {
			rc = FAIL;
			goto done;
		}

		romClass->romSize = ROMCLASS_SIZE;

		/* fill ROMClass contents with some garbage */
		count = 4;
		while (count < ROMCLASS_SIZE-4) {
			memset((U_8 *)romClass+count, count%128, sizeof(char));
			count++;
		}

		/* Set ROMClass.intermediateClassData to NULL, it can't be set to garbage
		 * as there is as assert in j9shr_classStoreTransaction_stop() to verify its value */
		romClass->intermediateClassData = 0;
		romClass->intermediateClassDataLength = 0;

		setRomClassName(vm, romClass, romClassName);

		if (transaction.updateSharedClassSize(romClass->romSize) == -1) {
			rc = FAIL;
			goto done;
		}
		if ((UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE == corruptionType) &&
			(!strncmp(romClassName, "CorruptDummyClass", sizeof("CorruptDummyClass"))))
		{
			/* set ROMClass size to zero to cause corruption during SH_CacheMap::updateROMSegmentListForCache() */
			romClass->romSize = 0;
		}
	}

	/* In this case, we corrupt the ROMClass it has been successfully committed,
	 * so that next call to SH_CacheMap::refreshHashTables() can detect corruption during SH_CacheMap::updateROMSegmentListForCache().
	 */
	if ((UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE == corruptionType) &&
		(!strncmp(romClassName, "CorruptDummyClass", sizeof("CorruptDummyClass"))))
	{
		romClass->romSize = 0;
	}
done:
	return rc;
}

extern "C" {

IDATA
testCorruptCache(J9JavaVM* vm)
{
	IDATA rc = PASS;
	IDATA corruptionType;
	IDATA i;
	I_32 cacheType;
	const char * cacheTypeString = NULL;
	bool readOnly;

	PORT_ACCESS_FROM_JAVAVM(vm);

	REPORT_START("CorruptCacheTest");

	UnitTest::unitTest = UnitTest::CORRUPT_CACHE_TEST;
	vm->internalVMFunctions->internalEnterVMFromJNI(vm->mainThread);

	for (i = 0; i < 6; i++) {
		CorruptCacheTest corruptCacheTest;
		U_64 extraRuntimeFlags = 0;
		cacheType = 0;
		IDATA semid = -1;

		switch (i) {
		case 0:
#if !defined(J9ZOS390)
			cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_PERSISTENT";
			readOnly = false;
			extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;
#endif
			break;
		case 1:
			cacheType = J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_NONPERSISTENT";
			readOnly = false;
			extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;

			break;
		case 2:
#if !(defined(J9ZOS390))
			cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_PERSISTENT";
			readOnly = true;
			extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;
#endif
			break;
		case 3:
			cacheType = J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_NONPERSISTENT";
			readOnly = true;
			extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;

			break;
		case 4:
#if !defined(J9ZOS390)
			cacheType = J9PORT_SHR_CACHE_TYPE_PERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_PERSISTENT";
			readOnly = false;
			extraRuntimeFlags |= (J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW);
#endif
			break;
		case 5:
			cacheType = J9PORT_SHR_CACHE_TYPE_NONPERSISTENT;
			cacheTypeString = "J9PORT_SHR_CACHE_TYPE_NONPERSISTENT";
			readOnly = false;
			extraRuntimeFlags |= (J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ALL | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW);

			break;
		}

		if (0 != cacheType) {
			j9tty_printf(PORTLIB, "\nRunning tests with cacheType: %d(%s) and readOnly: %d with extraRuntimeFlags: 0x%llx\n", cacheType , cacheTypeString, readOnly, extraRuntimeFlags);

			for (corruptionType = CACHE_CRC_INVALID_TYPE; corruptionType < NUM_CORRUPTION_TYPE; corruptionType++) {
				I_32 cacheSize = CACHE_SIZE;

				j9tty_printf(PORTLIB, "\nCorrupt cache with corruption type: %d(%s)\n", corruptionType, CorruptionTypeStrings[corruptionType]);

				if (ACQUIRE_HEADER_WRITE_LOCK_FAILED_TYPE == corruptionType) {
					/* skip this type as it is not possible for now to create this case */
					j9tty_printf(PORTLIB, "skip this type as it is not possible to create this case\n");
					continue;
				}

				if (J9PORT_SHR_CACHE_TYPE_PERSISTENT == cacheType) {
					if (CACHE_SEMAPHORE_MISMATCH_TYPE == corruptionType) {
						j9tty_printf(PORTLIB, "skip this type as it is not checked for \"persistent\" cache\n");
						continue;
					}
				}
#if defined(WIN32)
				if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == cacheType) {
					if (CACHE_SEMAPHORE_MISMATCH_TYPE == corruptionType) {
						j9tty_printf(PORTLIB, "skip this type on Windows\n");
						continue;
					}
				}
#endif
				if (J9PORT_SHR_CACHE_TYPE_NONPERSISTENT == cacheType) {
					if ((CACHE_HEADER_INCORRECT_CACHE_SIZE_TYPE == corruptionType) ||
						(CACHE_SIZE_INVALID_TYPE == corruptionType) ||
						(CACHE_DATA_NULL_TYPE == corruptionType)) {
						j9tty_printf(PORTLIB, "skip this type as it is not checked for \"non-persistent\" cache\n");
						continue;
					}
				}
				if ((readOnly) && (CACHE_CCINITCOMPLETE_UNINITIALIZED == corruptionType)) {
					j9tty_printf(PORTLIB, "skip this type as it is not detected on a read-only cache\n");
					continue;
				}
				rc = corruptCacheTest.openTestCache(vm, cacheType, cacheSize);

				if (FAIL == rc) {
					j9tty_printf(PORTLIB, "testCorruptCache: failed to open test cache\n");
					break;
				}
				rc = addDummyROMClass(vm, "DummyClass1", corruptionType);
				if (FAIL == rc) {
					j9tty_printf(PORTLIB, "testCorruptCache: failed to add \"DummyClass1\" in test cache\n");
					break;
				}
				rc = addDummyROMClass(vm, "DummyClass2", corruptionType);
				if (FAIL == rc) {
					j9tty_printf(PORTLIB, "testCorruptCache: failed to add \"DummyClass2\" in test cache\n");
					break;
				}
				rc = addDummyROMClass(vm, "DummyClass3", corruptionType);
				if (FAIL == rc) {
					j9tty_printf(PORTLIB, "testCorruptCache: failed to add \"DummyClass3\" in test cache\n");
					break;
				}
				if (UPDATE_ROMCLASS_CORRUPT_CASE1_TYPE == corruptionType) {
					/*
					 * To simulate this case, we are going to add a corrupt ROMClass (i.e. set the ROMClass size to zero after adding it to cache),
					 * and the corruption will be caught in updateROMSegmentListForCache() called by the destructor of SCStoreTransaction.
					 *
					 * This case corrupts the cache after startup. So after verifying corruption context,
					 * we again open the cache to make sure cache corruption is reported on next usage.
					 */
					rc = addDummyROMClass(vm, "CorruptDummyClass", corruptionType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to add \"CorruptDummyClass\" in test cache\n");
						break;
					}
					rc = corruptCacheTest.verifyCorruptionContext(vm, corruptionType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: corruption context verification failed\n");
						break;
					}
					rc = corruptCacheTest.closeTestCache(vm, cacheType, true);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to close test cache\n");
						break;
					}

					corruptCacheTest.openCorruptCache(vm, cacheType, readOnly, cacheSize, extraRuntimeFlags);

					rc = corruptCacheTest.closeAndRemoveCorruptCache(vm, cacheType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to remove corrupt cache\n");
						break;
					}
					continue;
				}
				if (CACHE_SEMAPHORE_MISMATCH_TYPE == corruptionType) {
					semid = corruptCacheTest.getSemId(vm);
					if (-1 == semid) {
						rc = FAIL;
						j9tty_printf(PORTLIB, "testCorruptCache: getSemId failed\n");
						break;
					}
				}

				rc = corruptCacheTest.closeTestCache(vm, cacheType, true);
				if (FAIL == rc) {
					j9tty_printf(PORTLIB, "testCorruptCache: failed to close test cache\n");
					break;
				}

				if (CACHE_SIZE_INVALID_TYPE == corruptionType) {
					truncateCache(vm, cacheType);
				} else if (CACHE_DATA_NULL_TYPE == corruptionType) {
					zeroOutCache(vm, cacheType);
#if !defined(WIN32)
				} else if (CACHE_SEMAPHORE_MISMATCH_TYPE == corruptionType) {
					rc = destroySemaphore(vm, semid);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to destroy semaphore\n");
						break;
					}
#endif
				} else {
					rc = corruptCacheTest.openTestCache(vm, cacheType, cacheSize);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to open test cache to make it corrupt\n");
						break;
					}
					rc = corruptCacheTest.corruptCache(vm, cacheType, corruptionType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to corrupt the cache\n");
						break;
					}
					if ((ITEM_LENGTH_CORRUPT_AFTER_STARTUP_TYPE == corruptionType) ||
						(UPDATE_ROMCLASS_CORRUPT_CASE2_TYPE == corruptionType)
					) {
						rc = corruptCacheTest.verifyCorruptionContext(vm, corruptionType);
						if (FAIL == rc) {
							j9tty_printf(PORTLIB, "testCorruptCache: corruption context verification failed\n");
							break;
						}
					}
					rc = corruptCacheTest.closeTestCache(vm, cacheType, false);

					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to close test cache after making it corrupt\n");
						break;
					}
				}

				/*
				 * Jazz 40220: Design: Don't destroy caches with bad CRCs during -Xshareclasses:printStats
				 *
				 * Open the cache with J9SHR_RUNTIMEFLAG_ENABLE_STATS set to simulate using -Xshareclasses:printXXXStats.
				 * If the cache is deleted incorrectly during this, then the below call to verifyCorruptionContext() will
				 * not find the expected corruption context, because the cache was deleted.
				 *
				 * It should also be noted that opening the cache with J9SHR_RUNTIMEFLAG_ENABLE_STATS from this test
				 * will not set the cache header as corrupt.
				 */

				/* Set the flag to open the cache for stats so that cache is not recreated if corruption is detected during startup.
				 * This will ensure corruption context is preserved.
				 * Opening the cache for stats does not detect CACHE_SEMAPHORE_MISMATCH_TYPE corruption.
				 */
				if (CACHE_SEMAPHORE_MISMATCH_TYPE != corruptionType) {
					extraRuntimeFlags |= J9SHR_RUNTIMEFLAG_ENABLE_STATS;
					corruptCacheTest.openCorruptCache(vm, cacheType, readOnly, cacheSize, extraRuntimeFlags);
					rc = corruptCacheTest.verifyCorruptionContext(vm, corruptionType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: corruption context verification failed\n");
						break;
					}
					if (!((corruptionInfo[corruptionType].corruptionStage == DURING_STARTUP) && (false == readOnly))) {
						/* we don't need cache any more. remove it */
						rc = corruptCacheTest.closeAndRemoveCorruptCache(vm, cacheType);
					} else {
						rc = corruptCacheTest.closeTestCache(vm, cacheType, true);
					}
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to close test cache after making it corrupt\n");
						break;
					}
				}
				/* If the corruption can be detected during startup and cache is read-write,
				 * then corruption context will be reset when corrupt cache is destroyed and new cache is created.
				 * Verify that corruption context is reset.
				 */
				if ((corruptionInfo[corruptionType].corruptionStage == DURING_STARTUP) && (false == readOnly)) {
					/* Clear the flag for stats so that corrupt cache is removed and new cache is recreated in case corruption is detected during startup */
					extraRuntimeFlags &= ~J9SHR_RUNTIMEFLAG_ENABLE_STATS;
					corruptCacheTest.openCorruptCache(vm, cacheType, readOnly, cacheSize, extraRuntimeFlags);
					rc = corruptCacheTest.isCorruptionContextReset(vm);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: corruption context reset failed\n");
						break;
					}
					rc = corruptCacheTest.closeAndRemoveCorruptCache(vm, cacheType);
					if (FAIL == rc) {
						j9tty_printf(PORTLIB, "testCorruptCache: failed to remove corrupt cache\n");
						break;
					}
				}
			}
			if (FAIL == rc) {
				break;
			}
		}
	}

	UnitTest::unitTest = UnitTest::NO_TEST;

	vm->internalVMFunctions->internalExitVMToJNI(vm->mainThread);
	REPORT_SUMMARY("CorruptCacheTest", rc);
	return rc;
}

} /* extern "C" */

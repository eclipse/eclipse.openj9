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
}
#include "OSCachemmap.hpp"
#include "OpenCacheHelper.h"

class ProtectSharedCacheDataOSCache : public SH_OSCachemmap {
public:
	UDATA protectedLength;
	UDATA unprotectedLength;

	void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

	ProtectSharedCacheDataOSCache() {
		protectedLength = 0;
		unprotectedLength = 0;
	}

	static UDATA getRequiredConstrBytes(void)
	{
		return sizeof(ProtectSharedCacheDataOSCache);
	}

	IDATA releaseWriteLock(UDATA lockid)
	{
		return 0;
	}

	virtual IDATA acquireWriteLock(UDATA lockid)
	{
		return 0;
	}

	virtual void cleanup()
	{
		return;
	}

	IDATA setRegionPermissions(J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags) {
		if (J9PORT_PAGE_PROTECT_READ == (flags & (J9PORT_PAGE_PROTECT_WRITE | J9PORT_PAGE_PROTECT_READ))) {
			protectedLength += length;
		} else {
			unprotectedLength += length;
		}
		return 0;
	}

	void resetTestVars() {
		protectedLength = 0;
		unprotectedLength = 0;
	}
};

IDATA testProtectSharedCacheData_test1(J9JavaVM* vm);
IDATA testProtectSharedCacheData_test2(J9JavaVM* vm);
IDATA testProtectSharedCacheData_test3(J9JavaVM* vm);

extern "C" {

IDATA
testProtectSharedCacheData(J9JavaVM* vm)
{
	IDATA rc = PASS;

	PORT_ACCESS_FROM_JAVAVM(vm);
	REPORT_START("testProtectSharedCacheData");

	rc |= testProtectSharedCacheData_test1(vm);
	rc |= testProtectSharedCacheData_test2(vm);
	rc |= testProtectSharedCacheData_test3(vm);

	REPORT_SUMMARY("testProtectSharedCacheData", rc);
	return rc;
}

}

IDATA
testProtectSharedCacheData_test1(J9JavaVM* vm)
{
	IDATA rc = PASS;
	J9SharedCacheHeader *ca = NULL;
	SH_CompositeCacheImpl *cc = NULL;
	SH_CacheMap *cacheMap = NULL;
	const char* testName = "testProtectSharedCacheData_test1";
	OpenCacheHelper cachehelper(vm);
	U_32 romClassSize = 0;
	const UDATA pageSizeToUse = 4096;
#if defined(J9ZOS390) || defined(AIXPPC)
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT;
	U_64 unsetProtectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
#else
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
	U_64 unsetProtectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
	UDATA oldUpdateCount = 0;
	BlockPtr oldProtectEnd = NULL;
	void *segmentStartPtr = NULL;
	void *metadataStartPtr = NULL;
	void *beforeSegmentPtr = NULL;
	void *beforeMetadataPtr = NULL;
	void *currentSegmentPtr = NULL;
	void *currentMetadataPtr = NULL;
	UDATA expectedProtectedLength = 0;
	UDATA expectedUnprotectedLength = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Set the _oscache to a dummy class so how much memory is mprotected can be monitored */
	char *osCacheMem = NULL;
	ProtectSharedCacheDataOSCache *myfakeoscache = NULL;

#if defined(J9ZOS390) || defined(AIXPPC)
	INFOPRINTF("Case mprotect=default or mprotect=nopartialpages\n");
#else
	INFOPRINTF("Case mprotect=nopartialpages\n");
#endif

	vm->internalVMFunctions->internalEnterVMFromJNI(vm->mainThread);

	if (NULL == (osCacheMem = (char *)j9mem_allocate_memory(ProtectSharedCacheDataOSCache::getRequiredConstrBytes(), J9MEM_CATEGORY_CLASSES))) {
		ERRPRINTF("Failed to allocate memory for testProtectNewROMClassData_test1");
		rc = FAIL;
		goto done;
	}
	myfakeoscache = new((void *)osCacheMem) ProtectSharedCacheDataOSCache();
	cachehelper.setOscache(myfakeoscache);
	cachehelper.setOsPageSize(pageSizeToUse);
	cachehelper.setDoSegmentProtect(true);

	rc = cachehelper.openTestCache(J9PORT_SHR_CACHE_TYPE_PERSISTENT, CACHE_SIZE, NULL, true, NULL, NULL, NULL,
								protectRunTimeFlags, unsetProtectRunTimeFlags, UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST);

	if (FAIL == rc) {
		ERRPRINTF("Failed to open cache for testProtectSharedCacheData_test1");
		rc = FAIL;
		goto done;
	}
	UnitTest::unitTest = UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST;

	myfakeoscache->resetTestVars();

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	ca = cc->getCacheHeaderAddress();

	/* Back up original values in the cache header and composite cache */
	oldUpdateCount = cc->_oldUpdateCount;
	oldProtectEnd = cc->getRomClassProtectEnd();
	segmentStartPtr = (void *)((UDATA)ca + ca->segmentSRP);
	metadataStartPtr = (void *)((UDATA)ca + ca->updateSRP);

	beforeSegmentPtr = segmentStartPtr;
	beforeMetadataPtr = metadataStartPtr;
	/* Test 1: Test protecting new data in segment region. Store a J9ROMClass 2.5 pages in size */
	romClassSize = (pageSizeToUse * 2) + (pageSizeToUse / 2);
	if (cachehelper.addDummyROMClass("testProtectSharedCacheData_test1_DummyClass1", romClassSize) != PASS) {
		ERRPRINTF("Adding dummy class to the cache has failed");
		rc = FAIL;
		goto done;
	}

	currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
	currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

	if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
		/* Since partial page protection is disabled, no page needs to be unprotected before writing any data */
		expectedUnprotectedLength = 0;
		if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
			ERRPRINTF2("After calling addDummyROMClass() the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
					expectedUnprotectedLength, myfakeoscache->unprotectedLength);
			rc = FAIL;
			goto done;
		}
	}

	expectedProtectedLength = (ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)beforeSegmentPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling addDummyROMClass() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}
	INFOPRINTF("Correctly protected a newly added ROMClasses");

	/* Test 2: Test protecting cache data after discovering a cache update.
	 * To do this modify the cache header and composite cache to simulate the discovery of a cache update.
	 */
	cc->_oldUpdateCount = oldUpdateCount;
	cc->setRomClassProtectEnd(oldProtectEnd);
	cc->_scan = (ShcItemHdr *)((UDATA)metadataStartPtr - sizeof(ShcItemHdr));
	myfakeoscache->resetTestVars();

	/* ::runEntryPointChecks() is responsible for mprotecting new J9ROMClasses and metadata added by other JVMs. */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);

	expectedProtectedLength = (ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)beforeSegmentPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling runEntryPointChecks() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected shared cache data after discovering cache update when not holding write mutex");

	/* Test 3: Test protecting new data in metadata region. Store AOT data of 2.5 pages in size */
	{
		IDATA dataSize = (2 * pageSizeToUse) + (pageSizeToUse / 2);
		U_8 *data = NULL;
		J9ROMMethod *romMethod = NULL;
		U_8 *storedMethod = NULL;

		myfakeoscache->resetTestVars();

		data = (U_8 *)j9mem_allocate_memory(dataSize, J9MEM_CATEGORY_CLASSES);
		if (NULL == data) {
			rc = FAIL;
			goto done;
		}

		romMethod = (J9ROMMethod *)cc->getSegmentAllocPtr();
		beforeMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);
		storedMethod = (U_8 *)vm->sharedClassConfig->storeCompiledMethod(vm->mainThread, romMethod, data, dataSize, NULL, 0, FALSE);
		if (NULL == storedMethod) {
			rc = FAIL;
			goto done;
		}
		currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

		if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
			/* Since partial page protection is disabled, no page needs to be unprotected before writing any data */
			expectedUnprotectedLength = 0;
			if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
				ERRPRINTF2("After calling storeCompiledMethod() the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
						expectedUnprotectedLength, myfakeoscache->unprotectedLength);
				rc = FAIL;
				goto done;
			}
		}

		expectedProtectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
		if (myfakeoscache->protectedLength != expectedProtectedLength) {
			ERRPRINTF2("After calling storeCompiledMethod() the incorrect size was protected. Expected %zu, but %zu was protected",
					expectedProtectedLength, myfakeoscache->protectedLength);
			rc = FAIL;
			goto done;
		}

		INFOPRINTF("Correctly protected shared cache metadata after adding AOT data");
	}

	/* Test 4: Test un-protecting and protecting the metadata region when the cache is locked and unlocked respectively */
	myfakeoscache->resetTestVars();
	cc->enterWriteMutex(vm->mainThread, true, NULL);	/* locks the cache */
	/* ::runEntryPointChecks() is responsible for mprotecting new ROMClasses and metadata added by other JVMs and the partially filled pages. */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);
	/* When cache is unlocked, whole of metadata including partial pages are protected back */
	cc->exitWriteMutex(vm->mainThread, NULL);

	/* When metadata is unprotected during cache lock operation, then last partially filled page is also unprotected. */
	expectedUnprotectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
	if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
		if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
			ERRPRINTF2("After locking the cache the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
					expectedUnprotectedLength, myfakeoscache->unprotectedLength);
			rc = FAIL;
			goto done;
		}
	}

	/* When metadata is protected during cache unlock operation, then last partially filled page is not protected. */
	expectedProtectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After locking the cache the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly unprotected/protected shared cache metadata during cache lock/unlock operation");

	/* Test 5: Test reopening the cache protects the correct amount of memory during ::startup() */
	cachehelper.closeTestCache(false);
	myfakeoscache->resetTestVars();

	rc = cachehelper.openTestCache(J9PORT_SHR_CACHE_TYPE_PERSISTENT, CACHE_SIZE, NULL, true, NULL, NULL, NULL,
								protectRunTimeFlags, unsetProtectRunTimeFlags, UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST);
	if (FAIL == rc) {
		ERRPRINTF("Failed to open cache for test 3");
		rc = FAIL;
		goto done;
	}
	UnitTest::unitTest = UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST;

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	ca = cc->getCacheHeaderAddress();

	segmentStartPtr = (void *)cc->getBaseAddress();
	metadataStartPtr = (void *)((UDATA)ca + ca->totalBytes);
	currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
	currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

	expectedProtectedLength = (ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)segmentStartPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling openTestCache() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected existing ROMClasses when opening an existing cache\n");

	INFOPRINTF("Done\n");
done:
	cachehelper.closeTestCache(true);
	if (NULL != osCacheMem) {
		j9mem_free_memory(osCacheMem);
	}
	vm->internalVMFunctions->internalExitVMToJNI(vm->mainThread);
	UnitTest::unitTest = UnitTest::NO_TEST;
	return rc;
}

IDATA
testProtectSharedCacheData_test2(J9JavaVM* vm)
{
	IDATA rc = PASS;
	J9SharedCacheHeader *ca = NULL;
	SH_CompositeCacheImpl *cc = NULL;
	SH_CacheMap *cacheMap = NULL;
	const char* testName = "testProtectSharedCacheData_test2";
	OpenCacheHelper cachehelper(vm);
	U_32 romClassSize = 0;
	const UDATA pageSizeToUse = 4096;
#if defined(J9ZOS390) || defined(AIXPPC)
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
	U_64 unsetProtectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
#else
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
	U_64 unsetProtectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
	UDATA oldUpdateCount = 0;
	BlockPtr oldProtectEnd = NULL;
	void *segmentStartPtr = NULL;
	void *metadataStartPtr = NULL;
	void *beforeSegmentPtr = NULL;
	void *beforeMetadataPtr = NULL;
	void *currentSegmentPtr = NULL;
	void *currentMetadataPtr = NULL;
	UDATA expectedProtectedLength = 0;
	UDATA expectedUnprotectedLength = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Set the _oscache to a dummy class so how much memory is mprotected can be monitored */
	char *osCacheMem = NULL;
	ProtectSharedCacheDataOSCache *myfakeoscache = NULL;

#if defined(J9ZOS390) || defined(AIXPPC)
	INFOPRINTF("Case mprotect=partialpages\n");
#else
	INFOPRINTF("Case mprotect=default or mprotect=partialpages\n");
#endif

	vm->internalVMFunctions->internalEnterVMFromJNI(vm->mainThread);

	if (NULL == (osCacheMem = (char *)j9mem_allocate_memory(ProtectSharedCacheDataOSCache::getRequiredConstrBytes(), J9MEM_CATEGORY_CLASSES))) {
		ERRPRINTF("Failed to allocate memory for testProtectNewROMClassData_test2");
		rc = FAIL;
		goto done;
	}
	myfakeoscache = new((void *)osCacheMem) ProtectSharedCacheDataOSCache();
	cachehelper.setOscache(myfakeoscache);
	cachehelper.setOsPageSize(pageSizeToUse);
	cachehelper.setDoSegmentProtect(true);

	rc = cachehelper.openTestCache(J9PORT_SHR_CACHE_TYPE_PERSISTENT, CACHE_SIZE, NULL, true, NULL, NULL, NULL,
								protectRunTimeFlags, unsetProtectRunTimeFlags, UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST);

	if (FAIL == rc) {
		ERRPRINTF("Failed to open cache for testProtectSharedCacheData_test2");
		rc = FAIL;
		goto done;
	}
	UnitTest::unitTest = UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST;

	myfakeoscache->resetTestVars();

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	ca = cc->getCacheHeaderAddress();

	/* Back up original values in the cache header and composite cache */
	oldUpdateCount = cc->_oldUpdateCount;
	oldProtectEnd = cc->getRomClassProtectEnd();
	segmentStartPtr = (void *)((UDATA)ca + ca->segmentSRP);
	metadataStartPtr = (void *)((UDATA)ca + ca->updateSRP);

	beforeSegmentPtr = segmentStartPtr;
	beforeMetadataPtr = metadataStartPtr;
	/* Test 1: Test protecting new data in segment region. Store a J9ROMClass 2.5 pages in size */
	romClassSize = (pageSizeToUse * 2) + (pageSizeToUse / 2);
	if (cachehelper.addDummyROMClass("testProtectSharedCacheData_test2_DummyClass1", romClassSize) != PASS) {
		ERRPRINTF("Adding dummy class to the cache has failed");
		rc = FAIL;
		goto done;
	}

	currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
	currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

	if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
		expectedUnprotectedLength = 0;

		if (0 != ((UDATA)beforeSegmentPtr % pageSizeToUse)) {
			expectedUnprotectedLength += pageSizeToUse;
		}
		if (0 != ((UDATA)beforeMetadataPtr % pageSizeToUse)) {
			expectedUnprotectedLength += pageSizeToUse;
		}
		if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
			ERRPRINTF2("After calling addDummyROMClass() the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
					expectedUnprotectedLength, myfakeoscache->unprotectedLength);
			rc = FAIL;
			goto done;
		}
	}

	expectedProtectedLength = (ROUND_UP_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)beforeSegmentPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling addDummyROMClass() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}
	INFOPRINTF("Correctly protected a newly added ROMClass");

	/* Test 2: Test protecting cache data after discovering a cache update while not acquiring write mutex.
	 * To do this modify the cache header and composite cache to simulate the discovery of a cache update.
	 */
	cc->_oldUpdateCount = oldUpdateCount;
	cc->setRomClassProtectEnd(oldProtectEnd);
	cc->_scan = (ShcItemHdr *)((UDATA)metadataStartPtr - sizeof(ShcItemHdr));
	myfakeoscache->resetTestVars();

	/* ::runEntryPointChecks() is responsible for mprotecting new ROMClasses and metadata added by other JVMs. */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);

	/* When reading updates, if J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ON_FIND is not set or
	 * runEntryPointChecks() was called without acquiring write mutex, then partially filled pages are not mprotected.
	 */
	expectedProtectedLength = (ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)beforeSegmentPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_UP_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling runEntryPointChecks() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected shared cache data after discovering cache update when not holding write mutex");

	/* Test 3: Test protecting cache data after discovering a cache update while acquiring a write mutex.
	 * To do this modify the cache header and composite cache to simulate the discovery of a cache update.
	 */
	cc->_oldUpdateCount = oldUpdateCount;
	cc->setRomClassProtectEnd(oldProtectEnd);
	cc->_scan = (ShcItemHdr *)((UDATA)metadataStartPtr - sizeof(ShcItemHdr));
	myfakeoscache->resetTestVars();

	cc->enterWriteMutex(vm->mainThread, false, NULL);
	/* ::runEntryPointChecks() is responsible for mprotecting new ROMClasses and metadata added by other JVMs. */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);
	cc->exitWriteMutex(vm->mainThread, NULL);

	/* When reading updates, if J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ON_FIND is not set but
	 * runEntryPointChecks() was called after acquiring write mutex,
	 * then partially filled pages are mprotected.
	 */
	expectedProtectedLength = (ROUND_UP_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)beforeSegmentPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling runEntryPointChecks() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected shared cache data after discovering cache update when holding write mutex");

	/* Test 4: Test protecting new data in metadata region. Store AOT data of 2.5 pages in size */
	{
		IDATA dataSize = (2 * pageSizeToUse) + (pageSizeToUse / 2);
		U_8 *data = NULL;
		J9ROMMethod *romMethod = NULL;
		U_8 *storedMethod = NULL;

		myfakeoscache->resetTestVars();

		data = (U_8 *)j9mem_allocate_memory(dataSize, J9MEM_CATEGORY_CLASSES);
		if (NULL == data) {
			rc = FAIL;
			goto done;
		}

		romMethod = (J9ROMMethod *)cc->getSegmentAllocPtr();
		beforeMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);
		storedMethod = (U_8 *)vm->sharedClassConfig->storeCompiledMethod(vm->mainThread, romMethod, data, dataSize, NULL, 0, FALSE);
		if (NULL == storedMethod) {
			rc = FAIL;
			goto done;
		}
		currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
		currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

		if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
			expectedUnprotectedLength = 0;

			/* no data added to segment page, so it won't be unprotected */
			if (0 != ((UDATA)beforeMetadataPtr % pageSizeToUse)) {
				expectedUnprotectedLength += pageSizeToUse;
			}
			if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
				ERRPRINTF2("After calling storeCompiledMethod() the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
						expectedUnprotectedLength, myfakeoscache->unprotectedLength);
				rc = FAIL;
				goto done;
			}
		}

		expectedProtectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)beforeMetadataPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
		if (myfakeoscache->protectedLength != expectedProtectedLength) {
			ERRPRINTF2("After calling storeCompiledMethod() the incorrect size was protected. Expected %zu, but %zu was protected",
					expectedProtectedLength, myfakeoscache->protectedLength);
			rc = FAIL;
			goto done;
		}

		INFOPRINTF("Correctly protected shared cache metadata after adding AOT data");
	}

	/* Test 5: Test un-protecting and protecting the metadata region when the cache is locked and unlocked respectively */
	myfakeoscache->resetTestVars();
	cc->enterWriteMutex(vm->mainThread, true, NULL);	/* locks the cache */
	/* ::runEntryPointChecks() is responsible for mprotecting new ROMClasses added by other JVMs and the partially filled pages.
	 * If the cache is locked, then partially filled page in metadata is not protected.
	 */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);
	/* When cache is unlocked, whole of metadata including partial pages are protected back */
	cc->exitWriteMutex(vm->mainThread, NULL);

	expectedUnprotectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
	if (J9VM_PHASE_NOT_STARTUP == vm->phase) {
		if (myfakeoscache->unprotectedLength != expectedUnprotectedLength) {
			ERRPRINTF2("After locking the cache the incorrect size was unprotected. Expected %zu, but %zu was unprotected",
					expectedUnprotectedLength, myfakeoscache->unprotectedLength);
			rc = FAIL;
			goto done;
		}
	}
	expectedProtectedLength = ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr);
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After locking the cache the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly unprotected/protected shared cache metadata during cache lock/unlock operation");

	/* Test 6: Test reopening the cache protects the correct amount of memory during ::startup() */
	cachehelper.closeTestCache(false);
	myfakeoscache->resetTestVars();

	rc = cachehelper.openTestCache(J9PORT_SHR_CACHE_TYPE_PERSISTENT, CACHE_SIZE, NULL, true, NULL, NULL, NULL,
								protectRunTimeFlags, unsetProtectRunTimeFlags, UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST);
	if (FAIL == rc) {
		ERRPRINTF("Failed to open cache for test 3");
		rc = FAIL;
		goto done;
	}
	UnitTest::unitTest = UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST;

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	ca = cc->getCacheHeaderAddress();

	segmentStartPtr = (void *)cc->getBaseAddress();
	metadataStartPtr = (void *)((UDATA)ca + ca->totalBytes);
	currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
	currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

	expectedProtectedLength = (ROUND_UP_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)segmentStartPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling openTestCache() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected existing ROMClasses when opening an existing cache\n");

	INFOPRINTF("Done\n");
done:
	cachehelper.closeTestCache(true);
	if (NULL != osCacheMem) {
		j9mem_free_memory(osCacheMem);
	}
	vm->internalVMFunctions->internalExitVMToJNI(vm->mainThread);
	UnitTest::unitTest = UnitTest::NO_TEST;
	return rc;
}

IDATA
testProtectSharedCacheData_test3(J9JavaVM* vm)
{
	IDATA rc = PASS;
	J9SharedCacheHeader *ca = NULL;
	SH_CompositeCacheImpl *cc = NULL;
	SH_CacheMap *cacheMap = NULL;
	const char* testName = "testProtectSharedCacheData_test3";
	OpenCacheHelper cachehelper(vm);
	U_32 romClassSize = 0;
	const UDATA pageSizeToUse = 4096;
#if defined(J9ZOS390) || defined(AIXPPC)
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND;
	U_64 unsetProtectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW;
#else
	U_64 protectRunTimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ONFIND;
	U_64 unsetProtectRunTimeFlags = 0;
#endif /* defined(J9ZOS390) || defined(AIXPPC) */
	UDATA oldUpdateCount = 0;
	BlockPtr oldProtectEnd = NULL;
	void *segmentStartPtr = NULL;
	void *metadataStartPtr = NULL;
	void *currentSegmentPtr = NULL;
	void *currentMetadataPtr = NULL;
	UDATA expectedProtectedLength = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Set the _oscache to a dummy class so how much memory is mprotected can be monitored */
	char *osCacheMem = NULL;
	ProtectSharedCacheDataOSCache *myfakeoscache = NULL;

	INFOPRINTF("Case mprotect=onfind\n");

	vm->internalVMFunctions->internalEnterVMFromJNI(vm->mainThread);

	if (NULL == (osCacheMem = (char *)j9mem_allocate_memory(ProtectSharedCacheDataOSCache::getRequiredConstrBytes(), J9MEM_CATEGORY_CLASSES))) {
		ERRPRINTF("Failed to allocate memory for testProtectNewROMClassData_test1");
		rc = FAIL;
		goto done;
	}
	myfakeoscache = new((void *)osCacheMem) ProtectSharedCacheDataOSCache();
	cachehelper.setOscache(myfakeoscache);
	cachehelper.setOsPageSize(pageSizeToUse);
	cachehelper.setDoSegmentProtect(true);

	rc = cachehelper.openTestCache(J9PORT_SHR_CACHE_TYPE_PERSISTENT, CACHE_SIZE, NULL, true, NULL, NULL, NULL,
								protectRunTimeFlags, unsetProtectRunTimeFlags, UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST);

	if (FAIL == rc) {
		ERRPRINTF("Failed to open cache for testProtectSharedCacheData_test3");
		rc = FAIL;
		goto done;
	}
	UnitTest::unitTest = UnitTest::PROTECT_NEW_ROMCLASS_DATA_TEST;

	myfakeoscache->resetTestVars();

	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	ca = cc->getCacheHeaderAddress();

	/* Back up original values in the cache header and composite cache */
	oldUpdateCount = cc->_oldUpdateCount;
	oldProtectEnd = cc->getRomClassProtectEnd();

	segmentStartPtr = (void *)((UDATA)ca + ca->segmentSRP);
	metadataStartPtr = (void *)((UDATA)ca + ca->updateSRP);

	/* Add a J9ROMClass of 2.5 pages in size */
	romClassSize = (pageSizeToUse * 2) + (pageSizeToUse / 2);
	if (cachehelper.addDummyROMClass("testProtectSharedCacheData_test3_DummyClass1", romClassSize) != PASS) {
		ERRPRINTF("Adding dummy class to the cache has failed");
		rc = FAIL;
		goto done;
	}

	currentSegmentPtr = (void *)((UDATA)ca + ca->segmentSRP);
	currentMetadataPtr = (void *)((UDATA)ca + ca->updateSRP);

	/* Test 1: Test protecting cache data after discovering a cache update while not acquiring write mutex.
	 * To do this modify the cache header and composite cache to simulate the discovery of a cache update.
	 */
	myfakeoscache->resetTestVars();
	cc->_oldUpdateCount = oldUpdateCount;
	cc->setRomClassProtectEnd(oldProtectEnd);
	cc->_scan = (ShcItemHdr *)((UDATA)metadataStartPtr - sizeof(ShcItemHdr));

	/* ::runEntryPointChecks() is responsible for mprotecting new ROMClasses and metadata added by other JVMs. */
	cacheMap->runEntryPointChecks(vm->mainThread, NULL, NULL);

	/* When reading updates, if J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_ON_FIND is hen partially filled pages are not mprotected. */
	expectedProtectedLength = (ROUND_UP_TO(pageSizeToUse, (UDATA)currentSegmentPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)segmentStartPtr))
								+ (ROUND_UP_TO(pageSizeToUse, (UDATA)metadataStartPtr) - ROUND_DOWN_TO(pageSizeToUse, (UDATA)currentMetadataPtr));
	if (myfakeoscache->protectedLength != expectedProtectedLength) {
		ERRPRINTF2("After calling runEntryPointChecks() the incorrect size was protected. Expected %zu, but %zu was protected",
				expectedProtectedLength, myfakeoscache->protectedLength);
		rc = FAIL;
		goto done;
	}

	INFOPRINTF("Correctly protected shared cache data after discovering cache update\n");

	INFOPRINTF("Done\n");
done:
	cachehelper.closeTestCache(true);
	if (NULL != osCacheMem) {
		j9mem_free_memory(osCacheMem);
	}
	vm->internalVMFunctions->internalExitVMToJNI(vm->mainThread);
	UnitTest::unitTest = UnitTest::NO_TEST;
	return rc;
}

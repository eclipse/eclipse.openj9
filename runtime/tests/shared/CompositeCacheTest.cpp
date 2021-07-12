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

#include "CompositeCacheImpl.hpp"
#include "UnitTest.hpp"
#include "sharedconsts.h"
#include "j9port.h"
#include "main.h"
#include "j9.h"
#include <string.h>

#define _UTE_STATIC_
#include "ut_j9shr.h"

#define SMALL_CACHE_SIZE 10240
#define MAIN_CACHE_SIZE (1024 * 50) 

extern "C" 
{ 
	IDATA testCompositeCache(J9JavaVM* vm); 
}

class CompositeCacheTest {
	public:
		static IDATA createTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl** cc1, SH_CompositeCacheImpl** cc1a, SH_CompositeCacheImpl** cc2, SH_CompositeCacheImpl** cc2a, U_64 *runtimeFlags0, U_64 *runtimeFlags1);
		static IDATA allocateAndStats(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
		static IDATA basicMutexTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
		static IDATA updateTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
		static IDATA writeHashTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
		static IDATA crashCntrTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
		static IDATA corruptTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a);
	private:
		static IDATA localTestStats(J9JavaVM* vm, IDATA testCacheSize, UDATA metaBytes, UDATA allocBytes, UDATA AOTBytes, UDATA JITBytes, UDATA updateCntr, SH_CompositeCacheImpl* cc, char* cacheBase);
		static IDATA checkUpdateResults(J9JavaVM* vm, SH_CompositeCacheImpl* cc, UDATA expectedCheckUpdates, bool tryNextEntry, char* expectedNextEntry, 
				bool useStaleItems, UDATA expectedStale, bool expectSegment, char* segment);
};

IDATA
CompositeCacheTest::createTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl** cc1, SH_CompositeCacheImpl** cc1a, SH_CompositeCacheImpl** cc2, SH_CompositeCacheImpl** cc2a, U_64 *runtimeFlags0, U_64 *runtimeFlags1)
{
	IDATA result = PASS;
	UDATA requiredBytes;
	U_32 cacheSize = 0;
	SH_CompositeCacheImpl *memForCC, *actualCC;
	char* cache;
	UDATA totalSize;
	UDATA localCrashCntr = 0;
	bool cacheHasIntegrity;
	J9SharedClassPreinitConfig* piconfig;

	PORT_ACCESS_FROM_JAVAVM(vm);

	if (!(piconfig = (J9SharedClassPreinitConfig*)j9mem_allocate_memory(sizeof(J9SharedClassPreinitConfig), J9MEM_CATEGORY_CLASSES))) {
		return 15;
	}
	piconfig->sharedClassDebugAreaBytes = -1;
	piconfig->sharedClassCacheSize = SMALL_CACHE_SIZE;
	piconfig->sharedClassReadWriteBytes = 0;
	piconfig->sharedClassMinAOTSize = -1;
	piconfig->sharedClassMaxAOTSize = -1;
	piconfig->sharedClassMinJITSize = -1;
	piconfig->sharedClassMaxJITSize = -1;
	piconfig->sharedClassSoftMaxBytes = -1;

	requiredBytes = SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, false);
	totalSize = requiredBytes + piconfig->sharedClassCacheSize;
	if (!(memForCC = (SH_CompositeCacheImpl*)j9mem_allocate_memory(totalSize, J9MEM_CATEGORY_CLASSES))) {
		return 1;
	}

	/* Test normal scenario */
	memset((void*)memForCC, 0, totalSize);
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myRoot", false, false, 0);
	cache = (char*)((char*)actualCC + requiredBytes);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags1, 1, "myRoot", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 2;
	}
	if (cacheSize != SMALL_CACHE_SIZE) {
		return 3;
	}
	if (localCrashCntr != 0) {
		return 4;
	}
	actualCC->cleanup(vm->mainThread);

	/* Test small cache size */
	piconfig->sharedClassCacheSize = 10;
	memset((void*)memForCC, 0, totalSize);
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myRoot", false, false, 0);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags1, 1, "myRoot", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 5;
	}
	totalSize = actualCC->getTotalUsableCacheSize();
	if (totalSize != (MIN_CC_SIZE - sizeof(J9SharedCacheHeader))) {
		return 6;
	}
	actualCC->cleanup(vm->mainThread);
	j9mem_free_memory(memForCC);

	piconfig->sharedClassCacheSize = testCacheSize;
	totalSize = requiredBytes + piconfig->sharedClassCacheSize;
	if (!(memForCC = (SH_CompositeCacheImpl*)j9mem_allocate_memory(totalSize, J9MEM_CATEGORY_CLASSES))) {
		return 7;
	}

	/* Test normal scenario */
	memset((void*)memForCC, 0, totalSize);
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myCache1", false, false, 0);
	cache = (char*)((char*)actualCC + requiredBytes);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags1, 1, "myCache1", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 8;
	}
	*cc1 = actualCC;

	/* cc1a is another view on the same cache cc2 */
	if (!(memForCC = (SH_CompositeCacheImpl*)j9mem_allocate_memory(requiredBytes, J9MEM_CATEGORY_CLASSES))) {
		return 9;
	}
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myCache1a", false, false, 0);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags0, 1, "myCache1a", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 10;
	}
	*cc1a = actualCC;

	if (!(memForCC = (SH_CompositeCacheImpl*)j9mem_allocate_memory(totalSize, J9MEM_CATEGORY_CLASSES))) {
		return 11;
	}
	memset((void*)memForCC, 0, totalSize);
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myCache2", false, false, 0);
	cache = (char*)((char*)actualCC + requiredBytes);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags1, 1, "myCache2", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 12;
	}
	*cc2 = actualCC;

	/* cc2a is another view on the same cache cc2 */
	if (!(memForCC = (SH_CompositeCacheImpl*)j9mem_allocate_memory(requiredBytes, J9MEM_CATEGORY_CLASSES))) {
		return 13;
	}
	/* Currently requests non-persistent cache */
	actualCC = SH_CompositeCacheImpl::newInstance(vm, NULL, memForCC, "myCache2a", false, false, 0);
	if (actualCC->startup(vm->mainThread, piconfig, cache, runtimeFlags0, 1, "myCache2a", NULL, J9SH_DIRPERM_ABSENT, &cacheSize, &localCrashCntr, true, &cacheHasIntegrity) != 0) {
		return 14;
	}
	*cc2a = actualCC;
	
	return result;	
}

IDATA
CompositeCacheTest::localTestStats(J9JavaVM* vm, IDATA testCacheSize, UDATA metaBytes, UDATA allocBytes, UDATA AOTBytes,  UDATA JITBytes, UDATA updateCntr, SH_CompositeCacheImpl* cc, char* cacheBase)
{
	void* baseAddress = cc->getBaseAddress();
	void* endAddress = cc->getCacheEndAddress();
	void* segmentAllocPtr = cc->getSegmentAllocPtr();
	UDATA aotResult = cc->getAOTBytes();
	UDATA jitResult =  cc->getJITBytes();
	UDATA totalCacheSize = cc->getTotalUsableCacheSize();
	UDATA freeBytes = cc->getFreeBytes();
	UDATA debugBytes = cc->getDebugBytes();
	UDATA expectedTotalCacheSize = (testCacheSize - sizeof(struct J9SharedCacheHeader));
	UDATA uResult = 0;
	void* vResult = (void*)0;

	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "Testing stats for cache of size %d, with allocBytes=%d and metaBytes=%d\n", testCacheSize, allocBytes, metaBytes);

	vResult = (cacheBase + sizeof(struct J9SharedCacheHeader) + debugBytes);
	if (baseAddress != vResult) {
		j9tty_printf(PORTLIB, "1.) Got %p expected %p\n", baseAddress, vResult);
		return 1;
	}

	vResult = (cacheBase + testCacheSize);
	if (endAddress != vResult) {
		j9tty_printf(PORTLIB, "2.) Got %p expected %p\n", endAddress, vResult);
		return 2;
	}

	vResult = (void*)((char*)baseAddress + allocBytes);
	if (segmentAllocPtr != vResult) {
		j9tty_printf(PORTLIB, "3.) Got %p expected %p\n", segmentAllocPtr, vResult);
		return 3;
	}

	if (totalCacheSize != expectedTotalCacheSize) {
		j9tty_printf(PORTLIB, "4.) Got %d expected %d\n", totalCacheSize, expectedTotalCacheSize);
		return 4;
	}

	if (aotResult != AOTBytes) {
		j9tty_printf(PORTLIB, "5.) Got %d expected %d\n", aotResult, AOTBytes);
		return 5;
	}
	if (jitResult != JITBytes) {
			j9tty_printf(PORTLIB, "6.) Got %d expected %d\n", jitResult, JITBytes);
			return 6;
		}

	uResult = (expectedTotalCacheSize - allocBytes - (debugBytes + metaBytes + AOTBytes + JITBytes + (updateCntr * sizeof(ShcItemHdr))));
	if (freeBytes != uResult) {
		j9tty_printf(PORTLIB, "7.) Got %d expected %d\n", freeBytes, uResult);
		return 7;
	}
	return PASS;
}

IDATA
CompositeCacheTest::allocateAndStats(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	IDATA result = PASS;
	char* segBuf = NULL;
	char* address = NULL;
	char* cacheBase = (char*)((UDATA)cc1 + SH_CompositeCacheImpl::getRequiredConstrBytesWithCommonInfo(false, false));
	ShcItem item;
	ShcItem* itemPtr = &item;
	UDATA runningTotal = 0;
	
	PORT_ACCESS_FROM_JAVAVM(vm);

	memset(itemPtr, 0, sizeof(ShcItem));
	
	cc1->enterWriteMutex(vm->mainThread, false, "allocateAndStats");

	/* Check all is 0'd */
	if (0 != (result |= localTestStats(vm, testCacheSize, 0, 0, 0, 0, 0, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, 0, 0, 0, 0, 0, cc1a, cacheBase))) goto _done;

	itemPtr->dataLen = 64;
	
	address = cc1->allocateWithSegment(vm->mainThread, itemPtr, 0, &segBuf);
	if (0 != (result |= localTestStats(vm, testCacheSize, 0, 0, 0, 0, 0, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, 0, 0, 0, 0, 0, cc1a, cacheBase))) goto _done;

	/* Stats should not be updated until update is committed */
	cc1->commitUpdate(vm->mainThread, false);
	runningTotal += itemPtr->dataLen;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 0, 0, 0, 1, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 0, 0, 0, 1, cc1a, cacheBase))) goto _done;

	if (cc1->isAddressInROMClassSegment(address)) {
		j9tty_printf(PORTLIB, "Address %p should not be in romclass segment\n", address);
		result = 6;
		goto _done;
	}
	if (segBuf) {
		j9tty_printf(PORTLIB, "Segment buffer should be null\n");
		result = 7;
		goto _done;
	}

	address = cc1->allocateWithSegment(vm->mainThread, itemPtr, 256, &segBuf);
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 0, 0, 0, 1, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 0, 0, 0, 1, cc1a, cacheBase))) goto _done;
	cc1->commitUpdate(vm->mainThread, false);
	runningTotal += itemPtr->dataLen;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 256, 0, 0, 2, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 256, 0, 0, 2, cc1a, cacheBase))) goto _done;

	if (cc1->isAddressInROMClassSegment(address)) {
		j9tty_printf(PORTLIB, "Address %p should not be in romclass segment\n", address);
		result = 8;
		goto _done;
	}
	if (!cc1->isAddressInROMClassSegment(segBuf)) {
		j9tty_printf(PORTLIB, "Address %p should be in romclass segment\n", segBuf);
		result = 9;
		goto _done;
	}

	itemPtr->dataLen = 32;
	
	address = cc1->allocateWithSegment(vm->mainThread, itemPtr, 8, &segBuf);
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 256, 0, 0, 2, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 256, 0, 0, 2, cc1a, cacheBase))) goto _done;
	cc1->commitUpdate(vm->mainThread, false);
	runningTotal += itemPtr->dataLen;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 3, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 3, cc1a, cacheBase))) goto _done;
	if (cc1->isAddressInROMClassSegment(address)) {
		j9tty_printf(PORTLIB, "Address %p should not be in romclass segment\n", address);
		result = 10;
		goto _done;
	}
	if (!cc1->isAddressInROMClassSegment(segBuf)) {
		j9tty_printf(PORTLIB, "Address %p should be in romclass segment\n", segBuf);
		result = 11;
		goto _done;
	}

	itemPtr->dataLen = 100;

	address = cc1->allocateBlock(vm->mainThread, itemPtr, SHC_WORDALIGN, 0);
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 3, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 3, cc1a, cacheBase))) goto _done;
	cc1->commitUpdate(vm->mainThread, false);
	runningTotal += itemPtr->dataLen;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 4, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 4, cc1a, cacheBase))) goto _done;
	if (cc1->isAddressInROMClassSegment(address)) {
		j9tty_printf(PORTLIB, "Address %p should not be in romclass segment\n", address);
		result = 16;
		goto _done;
	}

	itemPtr->dataLen = 128;

	address = cc1->allocateAOT(vm->mainThread, itemPtr, 96);
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 4, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 0, 0, 4, cc1a, cacheBase))) goto _done;
	cc1->commitUpdate(vm->mainThread, false);
	runningTotal += itemPtr->dataLen - 96;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 96, 0, 5, cc1, cacheBase))) goto _done;
	if (0 != (result |= localTestStats(vm, testCacheSize, runningTotal, 264, 96, 0, 5, cc1a, cacheBase))) goto _done;
	if (cc1->isAddressInROMClassSegment(address)) {
		j9tty_printf(PORTLIB, "Address %p should not be in romclass segment\n", address);
		result = 17;
		goto _done;
	}

	/* Try some error cases */

	/* Negative test. Disable Trc_SHR_Assert_ShouldNeverHappen in SH_CompositeCacheImpl::allocate() */
	j9shr_UtActive[1009] = 0;

	if (cc1->allocateWithSegment(vm->mainThread, NULL, 0, NULL)) {
		j9shr_UtActive[1009] = 1;
		result = 12;
		goto _done;
	}
	/* Negative test done. Enable Trc_SHR_Assert_ShouldNeverHappen back */
	j9shr_UtActive[1009] = 1;

	itemPtr->dataLen = 0;

	if (cc1->allocateWithSegment(vm->mainThread, itemPtr, 0, NULL)) {
		result = 13;
		goto _done;
	}

	segBuf = NULL;
	if (cc1->allocateWithSegment(vm->mainThread, itemPtr, 0xFFFFFFF, &segBuf)) {
		result = 14;
		goto _done;
	} else {
		if (segBuf) {
			result = 15;
			goto _done;
		}
	}

_done:
	cc1->exitWriteMutex(vm->mainThread, "allocateAndStats");
	return result;
}

IDATA
CompositeCacheTest::basicMutexTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	UDATA i;

	/* Without multiple threads, this test is just to ensure that there are no unexpected hangs/crashes 
		Also, for unit testing, there is no global mutex. Local mutexes are used instead to simulate. */

	for (i = 0; i<1000; i++) {
		if (cc1->enterReadMutex(vm->mainThread, "")) {
			return 1;
		}
		if (cc1a->enterReadMutex(vm->mainThread, "")) {
			return 2;
		}
	}
	for (i = 0; i<1000; i++) {
		cc1->exitReadMutex(vm->mainThread, "");
		cc1a->exitReadMutex(vm->mainThread, "");
	}

	/* Should be able to get read mutex concurrent with write mutex if not locked */
	if (cc1->enterWriteMutex(vm->mainThread, false, "")) {
		return 3;
	}
	/* Negative test. Disable Trc_SHR_Assert_NotEquals() in SH_CompositeCacheImpl::enterReadMutex() */
	j9shr_UtActive[1015] = 0;
	if (cc1->enterReadMutex(vm->mainThread, "")) {
		j9shr_UtActive[1015] = 1;
		return 4;
	}
	/* Negative test done. Enable Trc_SHR_Assert_NotEquals() back */
	j9shr_UtActive[1015] = 1;

	if (cc1a->enterReadMutex(vm->mainThread, "")) {
		return 5;
	}

	/* Negative test. Disable Trc_SHR_Assert_NotEquals() in SH_CompositeCacheImpl::exitReadMutex() */
	j9shr_UtActive[1015] = 0;
	cc1->exitReadMutex(vm->mainThread, "");
	/* Negative test done . Enable Trc_SHR_Assert_NotEquals() back */
	j9shr_UtActive[1015] = 1;

	cc1a->exitReadMutex(vm->mainThread, "");
	if (cc1->exitWriteMutex(vm->mainThread, "")) {
		return 6;
	}

	/* Ensure that cc1 released its write mutex ok */
	if (cc1a->enterWriteMutex(vm->mainThread, false, "")) {
		return 7;
	}
	if (cc1a->exitWriteMutex(vm->mainThread, "")) {
		return 8;
	}

	/* Test locked */
	if (cc1->enterWriteMutex(vm->mainThread, true, "")) {
		return 9;
	}
	if (cc1->exitWriteMutex(vm->mainThread, "")) {
		return 10;
	}
	if (cc1a->enterWriteMutex(vm->mainThread, true, "")) {
		return 11;
	}
	if (cc1a->exitWriteMutex(vm->mainThread, "")) {
		return 12;
	}

	/* Test manual lock */
	if (cc1->enterWriteMutex(vm->mainThread, false, "")) {
		return 13;
	}
	cc1->doLockCache(vm->mainThread);
	cc1->doUnlockCache(vm->mainThread);
	if (cc1->exitWriteMutex(vm->mainThread, "")) {
		return 14;
	}

	/* Not a normal scenario, but forces enterReadMutex to go down different code path without waiting (as it should if the write mutex is held) */

	/* Negative test. Disable Trc_SHR_Assert_Equals() in SH_CompositeCacheImpl::doLockCache() */
	j9shr_UtActive[1014] = 0;
	cc1->doLockCache(vm->mainThread);
	/* Negative test done. Enable Trc_SHR_Assert_Equals() back */
	j9shr_UtActive[1014] = 1;

	if (cc1->enterReadMutex(vm->mainThread, "")) {
		return 15;
	}
	cc1->exitReadMutex(vm->mainThread, "");

	/* Negative test. Disable Trc_SHR_Assert_Equals() in SH_CompositeCacheImpl::doUnlockCache() */
	j9shr_UtActive[1014] = 0;
	cc1->doUnlockCache(vm->mainThread);
	/* Negative test done. Enable Trc_SHR_Assert_Equals() back */
	j9shr_UtActive[1014] = 1;


	return PASS;
}

IDATA 
CompositeCacheTest::checkUpdateResults(J9JavaVM* vm, SH_CompositeCacheImpl* cc, UDATA expectedCheckUpdates, bool tryNextEntry, char* expectedNextEntry, 
			bool useStaleItems, UDATA expectedStale, bool expectSegment, char* segment)
{
	UDATA staleItems;
	char* cResult;
	UDATA uResult;
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "Checking update results: expected checkUpdates=%d", expectedCheckUpdates);
	if (tryNextEntry) {
		j9tty_printf(PORTLIB, ", expected nextEntry=%p", expectedNextEntry);
	}
	if (useStaleItems) {
		j9tty_printf(PORTLIB, ", expected staleItems=%d", expectedStale);
	}
	if (expectSegment) {
		j9tty_printf(PORTLIB, ", expected segment=%p", segment);
	}
	j9tty_printf(PORTLIB, "\n");

	if ((uResult = cc->checkUpdates(vm->mainThread)) != expectedCheckUpdates) {
		j9tty_printf(PORTLIB, "checkUpdates(vm->mainThread) returned %d - expected %d\n", uResult, expectedCheckUpdates);
		return 1;
	}
	if (tryNextEntry) {
		if (useStaleItems) {
			cResult = cc->nextEntry(vm->mainThread, &staleItems);
		} else {
			cResult = cc->nextEntry(vm->mainThread, NULL);
		}
		if (cResult != expectedNextEntry) {
			j9tty_printf(PORTLIB, "nextEntry returned %p - expected %p\n", cResult, expectedNextEntry);
			return 2;
		}
		if (useStaleItems) {
			if (staleItems != expectedStale) {
				j9tty_printf(PORTLIB, "staleItems returned from nextEntry were %d - expected %d\n", staleItems, expectedStale);
				return 3;
			}
		}
	}
	if (!expectSegment) {
		if (segment) {
			j9tty_printf(PORTLIB, "segment should not have been created\n");
			return 4;
		}
	} else {
		if (!segment) {
			j9tty_printf(PORTLIB, "segment should have been created\n");
			return 5;
		} else {
			if (!cc->isAddressInROMClassSegment(segment)) {
				j9tty_printf(PORTLIB, "segment created is not in ROMClass area!\n");
				return 6;
			}
		}
	}
	return 0;
}

IDATA
CompositeCacheTest::updateTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	char* address[11];
	char* segment[11];
	UDATA staleItems;
	ShcItem item;
	ShcItem* itemPtr = &item;
	IDATA result = PASS;

	/* Tests:
			checkUpdates
			doneReadUpdates
			nextEntry
			markStale
			stale
			findStart
	*/

	/* IMPORTANT: cc1 and cc1a are two views on the same cache */

	/*When using two views on the same cache tracking the write counter doesn't make sense*/
	UnitTest::unitTest = UnitTest::COMPOSITE_CACHE_TEST_SKIP_WRITE_COUNTER_UPDATE;

	/* Test functions with an empty cache */
	cc1->enterWriteMutex(vm->mainThread, false, "cc1UpdateTest");
	cc1a->enterWriteMutex(vm->mainThread, false, "cc1aUpdateTest");

	memset(itemPtr, 0, sizeof(ShcItem));

	if (cc1->checkUpdates(vm->mainThread) || cc1a->checkUpdates(vm->mainThread)) {
		result = 1;
		goto _done;
	}
	cc1->doneReadUpdates(vm->mainThread, 0);
	cc1->findStart(vm->mainThread);
	staleItems = 5;
	if (cc1->nextEntry(vm->mainThread, &staleItems)) {			/* should clear staleItems to 0 */
		result = 2;
		goto _done;
	} else {
		if (staleItems) {
			result = 3;
			goto _done;
		}
	}
	
	/* Allocate various entries and check checkUpdates, doneReadUpdates and nextEntry */

	itemPtr->dataLen = 8;

	address[0] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 0, &segment[0]);
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 0, false, segment[0]))  {
		result = 4;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, NULL, true, 0, false, segment[0])) {
		result = 5;
		goto _done;
	}
	cc1->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 0, false, segment[0])) {
		result = 6;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 1, true, address[0], true, 0, false, segment[0])) {
		result = 7;
		goto _done;
	}

	itemPtr->dataLen = 16;

	address[1] = cc1a->allocateWithSegment(vm->mainThread, itemPtr, 0, &segment[1]);
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 0, false, segment[1])) {
		result = 8;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 1, true, NULL, true, 0, false, segment[1])) {
		result = 9;
		goto _done;
	}

	/* commitUpdate will reset checkUpdates() for CompositeCacheImpl that calls it. 
		Assumption is that CacheMap will have refreshed its hashtables and got a write mutex before allocating and committing. */

	cc1a->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1, 1, true, address[1], true, 0, false, segment[1])) {
		result = 10;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, NULL, true, 0, false, segment[1])) {
		result = 11;
		goto _done;
	}

	itemPtr->dataLen = 32;

	address[2] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 99, &segment[2]);
	cc1->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1a, 1, true, address[2], true, 0, true, segment[2])) {
		result = 12;
		goto _done;
	}
	
	itemPtr->dataLen = 64;

	address[3] = cc1a->allocateWithSegment(vm->mainThread, itemPtr, 30, &segment[3]);
	cc1a->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1, 1, true, address[3], true, 0, true, segment[2])) {
		result = 13;
		goto _done;
	}

	itemPtr->dataLen = 128;

	address[4] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 15, &segment[4]);
	cc1->commitUpdate(vm->mainThread, false);

	address[5] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 66, &segment[5]);
	cc1->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1a, 2, false, NULL, true, 0, false, NULL)) {
		result = 14;
		goto _done;
	}
	cc1a->doneReadUpdates(vm->mainThread, 1);
	if (checkUpdateResults(vm, cc1a, 1, false, NULL, true, 0, false, NULL)) {
		result = 15;
		goto _done;
	}
	cc1a->doneReadUpdates(vm->mainThread, 1);
	if (checkUpdateResults(vm, cc1a, 0, false, NULL, true, 0, false, NULL)) {
		result = 16;
		goto _done;
	}
	cc1a->doneReadUpdates(vm->mainThread, 1);			/* These next 2 calls should not normally happen */
	cc1->doneReadUpdates(vm->mainThread, 1);
	/* checkUpdates should not result = -1 and nextEntry should result = NULL */
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 0, true, segment[4])) {
		result = 17;
		goto _done;
	}

	if (checkUpdateResults(vm, cc1a, 0, true, address[4], true, 0, true, segment[5])) {
		result = 18;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[5], true, 0, false, NULL)) {
		result = 19;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, NULL, true, 0, false, NULL)) {
		result = 20;
		goto _done;
	}

	/* Allocate more entries and mark some stale and check checkUpdates, doneReadUpdates and nextEntry */

	itemPtr->dataLen = 8;

	address[6] = cc1a->allocateWithSegment(vm->mainThread, itemPtr, 50, &segment[6]);
	cc1a->commitUpdate(vm->mainThread, false);

	itemPtr->dataLen = 16;

	address[7] = cc1a->allocateWithSegment(vm->mainThread, itemPtr, 95, &segment[7]);
	cc1a->commitUpdate(vm->mainThread, false);

	/* Marking #7 stale */
	cc1a->markStale(vm->mainThread, (char*)(address[7] + 16 + sizeof(ShcItem)), false);
	if (!cc1a->stale((char*)(address[7] + 16 + sizeof(ShcItem)))) {
		result = 21;
		goto _done;
	}

	itemPtr->dataLen = 32;

	address[8] = cc1a->allocateWithSegment(vm->mainThread, itemPtr, 0, &segment[8]);
	cc1a->commitUpdate(vm->mainThread, false);
	if (checkUpdateResults(vm, cc1, 3, true, address[6], true, 0, true, segment[6])) {
		result = 22;
		goto _done;
	}
	cc1->doneReadUpdates(vm->mainThread, 1);
	/* Should skip over stale entry */
	if (checkUpdateResults(vm, cc1, 2, true, address[8], true, 1, true, segment[7])) {
		result = 23;
		goto _done;
	}
	cc1->doneReadUpdates(vm->mainThread, 2);
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 0, false, segment[8])) {
		result = 24;
		goto _done;
	}

	itemPtr->dataLen = 64;

	address[9] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 87, &segment[9]);
	cc1->commitUpdate(vm->mainThread, false);

	itemPtr->dataLen = 128;
	
	address[10] = cc1->allocateWithSegment(vm->mainThread, itemPtr, 65, &segment[10]);
	cc1->commitUpdate(vm->mainThread, false);

	/* Marking #10 stale */
	cc1->markStale(vm->mainThread, (char*)(address[10] + 128 + sizeof(ShcItem)), false);
	if (!cc1->stale((char*)(address[10] + 128 + sizeof(ShcItem)))) {
		result = 25;
		goto _done;
	}

	if (checkUpdateResults(vm, cc1a, 2, true, address[9], false, 0, true, segment[9])) {
		result = 26;
		goto _done;
	}
	cc1a->doneReadUpdates(vm->mainThread, 1);
	/* Should NOT skip over stale entry */
	if (checkUpdateResults(vm, cc1a, 1, true, address[10], false, 0, true, segment[10])) {
		result = 27;
		goto _done;
	}
	cc1a->doneReadUpdates(vm->mainThread, 1);
	if (checkUpdateResults(vm, cc1a, 0, true, NULL, true, 0, false, NULL)) {
		result = 28;
		goto _done;
	}

	/* Call findStart and then walk the allocated entries */
	cc1->findStart(vm->mainThread);
	cc1a->findStart(vm->mainThread);
	if (checkUpdateResults(vm, cc1, 0, true, address[0], true, 0, false, NULL)) {
		result = 29;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[0], true, 0, false, NULL)) {
		result = 30;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[1], true, 0, false, NULL)) {
		result = 31;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[1], true, 0, false, NULL)) {
		result = 32;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[2], true, 0, false, NULL)) {
		result = 33;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[2], true, 0, false, NULL)) {
		result = 34;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[3], true, 0, false, NULL)) {
		result = 35;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[3], true, 0, false, NULL)) {
		result = 36;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[4], true, 0, false, NULL)) {
		result = 37;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[4], true, 0, false, NULL)) {
		result = 38;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[5], true, 0, false, NULL)) {
		result = 39;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[5], true, 0, false, NULL)) {
		result = 40;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[6], true, 0, false, NULL)) {
		result = 41;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[6], true, 0, false, NULL)) {
		result = 42;
		goto _done;
	}
	/* should skip 7 */
	if (checkUpdateResults(vm, cc1, 0, true, address[8], true, 1, false, NULL)) {
		result = 43;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[8], true, 1, false, NULL)) {
		result = 44;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1, 0, true, address[9], true, 0, false, NULL)) {
		result = 45;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, address[9], true, 0, false, NULL)) {
		result = 46;
		goto _done;
	}
	/* should skip 10 */
	if (checkUpdateResults(vm, cc1, 0, true, NULL, true, 1, false, NULL)) {
		result = 47;
		goto _done;
	}
	if (checkUpdateResults(vm, cc1a, 0, true, NULL, true, 1, false, NULL)) {
		result = 48;
		goto _done;
	}

_done:
	cc1->exitWriteMutex(vm->mainThread, "cc1UpdateTest");
	cc1a->exitWriteMutex(vm->mainThread, "cc1aUpdateTest");
	UnitTest::unitTest = UnitTest::COMPOSITE_CACHE_TEST;
	return result;
}

IDATA
CompositeCacheTest::writeHashTest(J9JavaVM* vm, IDATA testCacheSize, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	/* Tests:
			testAndSetWriteHash
			tryResetWriteHash
			setWriteHash
			peekForWriteHash
	*/
	
	J9VMThread *currentThread = vm->internalVMFunctions->currentVMThread(vm);
	/* SH_CompositeCacheImpl::peekForWriteHash() has as a trace assert Trc_SHR_Assert_True
	 * to ensure caller holds SH_CacheMap::_refreshMutex, but SH_CacheMap is not created in this test.
	 * Therefore, for testing purpose disable this assert.
	 * Trc_SHR_Assert_True corresponds to trace point 1012.
	 */
	j9shr_UtActive[1012] = 0;
	/* Since cc1a was created after cc1, peekForWriteHash() should be true for cc1 and false for cc1a, 
		because cc1 knows that more VMs have joined but cc1a does not */
	if (!cc1->peekForWriteHash(currentThread)) {
		return 1;
	}
	if (cc1a->peekForWriteHash(currentThread)) {
		return 2;
	}
	cc1->setWriteHash(currentThread, 1);
	/* cc1a should now be savvy as it should see that cc1 is writeHashing */
	if (!cc1a->peekForWriteHash(currentThread)) {
		return 3;
	}

	/* Enable back trace assert Trc_SHR_Assert_True. */
	j9shr_UtActive[1012] = 1;
	/* reset to start */
	cc1->setWriteHash(currentThread, 0);

	/* cc1 sets it to 999 */
	if (cc1->testAndSetWriteHash(currentThread, 999) != 0) {
		return 4;
	}
	/* cc1a should get 1 for same hash telling it to wait */
	if (cc1a->testAndSetWriteHash(currentThread, 999) != 1) {
		return 5;
	}
	/* multiple calls should return same result */
	if (cc1a->testAndSetWriteHash(currentThread, 999) != 1) {
		return 6;
	}
	/* cc1 should not get 1 as it has set the value itself */
	if (cc1->testAndSetWriteHash(currentThread, 999) != 0) {
		return 7;
	}
	/* If cc1a tries to set it to something different, it should not wait (won't be set) */
	if (cc1a->testAndSetWriteHash(currentThread, 777) != 0) {
		return 8;
	}

	/* cc1a tries to reset, but this should fail as it is still set to 999 */
	if (cc1a->tryResetWriteHash(currentThread, 777) != 0) {
		return 9;
	}
	/* cc1 should now succeed to reset it */
	if (cc1a->tryResetWriteHash(currentThread, 999) != 1) {
		return 10;
	}

	return PASS;
}

IDATA
CompositeCacheTest::corruptTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	char* segBuf;
	ShcItem item;
	ShcItem* itemPtr = &item;

	memset(itemPtr, 0, sizeof(ShcItem));

	if (cc1->isCacheCorrupt() || cc1a->isCacheCorrupt()) {
		return 1;
	}

	/* pass dummy values as corruption context */
	cc1->setCorruptCache(vm->internalVMFunctions->currentVMThread(vm), NO_CORRUPTION, 0);
	if (!cc1->isCacheCorrupt()) {
		return 2;
	}
	if (!cc1a->isCacheCorrupt()) {
		return 3;
	}

	/* Check that we can't allocate now */
	itemPtr->dataLen = 32;

	cc1->enterWriteMutex(vm->mainThread, false, "cc1CorruptTest");
	if (cc1->allocateWithSegment(vm->mainThread, itemPtr, 8, &segBuf)) {
		cc1->exitWriteMutex(vm->mainThread, "cc1CorruptTest");
		return 4;
	}
	cc1->exitWriteMutex(vm->mainThread, "cc1CorruptTest");

	cc1a->enterWriteMutex(vm->mainThread, false, "cc1aCorruptTest");
	if (cc1a->allocateWithSegment(vm->mainThread, itemPtr, 8, &segBuf)) {
		cc1a->exitWriteMutex(vm->mainThread, "cc1aCorruptTest");
		return 5;
	}
	cc1a->exitWriteMutex(vm->mainThread, "cc1aCorruptTest");

	return PASS;
}

IDATA
CompositeCacheTest::crashCntrTest(J9JavaVM* vm, SH_CompositeCacheImpl* cc1, SH_CompositeCacheImpl* cc1a)
{
	UDATA localCrashCntr1 = 0;
	UDATA localCrashCntr1a = 0;
	J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);

	if (cc1->crashDetected(&localCrashCntr1) || cc1a->crashDetected(&localCrashCntr1a)) {
		return 1;
	}

	cc1->startCriticalUpdate(currentThread);
	cc1->endCriticalUpdate(currentThread);
	cc1a->startCriticalUpdate(currentThread);
	cc1a->endCriticalUpdate(currentThread);
	if (cc1->crashDetected(&localCrashCntr1) || cc1a->crashDetected(&localCrashCntr1a)) {
		return 2;
	}

	cc1->startCriticalUpdate(currentThread);
	if (!cc1->crashDetected(&localCrashCntr1)) {
		return 3;
	}
	if (!cc1a->crashDetected(&localCrashCntr1a)) {
		return 4;
	}
	if (cc1->crashDetected(&localCrashCntr1)) {
		return 5;
	}
	if (cc1a->crashDetected(&localCrashCntr1a)) {
		return 6;
	}
	cc1->endCriticalUpdate(currentThread);

	return PASS;
}

IDATA 
testCompositeCache(J9JavaVM* vm)
{
	PORT_ACCESS_FROM_PORT(vm->portLibrary);
	UDATA success = PASS;
	UDATA rc = 0;
	IDATA testCacheSize = MAIN_CACHE_SIZE;
	SH_CompositeCacheImpl* testCache1 = NULL;
	SH_CompositeCacheImpl* testCache1a = NULL;
	SH_CompositeCacheImpl* testCache2 = NULL;
	SH_CompositeCacheImpl* testCache2a = NULL;
	U_64 runtimeFlags0 = J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;
	U_64 runtimeFlags1 = J9SHR_RUNTIMEFLAG_DISABLE_CORRUPT_CACHE_DUMPS;

	REPORT_START("CompositeCache");

	UnitTest::unitTest = UnitTest::COMPOSITE_CACHE_TEST;
	SHC_TEST_ASSERT("createTest", CompositeCacheTest::createTest(vm, testCacheSize, &testCache1, &testCache1a, &testCache2, &testCache2a, &runtimeFlags0, &runtimeFlags1), success, rc);
	SHC_TEST_ASSERT("allocateAndStats", CompositeCacheTest::allocateAndStats(vm, testCacheSize, testCache1, testCache1a), success, rc);
	SHC_TEST_ASSERT("basicMutexTest", CompositeCacheTest::basicMutexTest(vm, testCache1, testCache1a), success, rc);
	/* use testCache2 and testCache2a because 1 and 1a have now got stuff in */
	SHC_TEST_ASSERT("updateTest", CompositeCacheTest::updateTest(vm, testCacheSize, testCache2, testCache2a), success, rc);
	/* TODO: Test made temporarily invalid by static _vmID
	SHC_TEST_ASSERT("writeHashTest", CompositeCacheTest::writeHashTest(vm, testCacheSize, testCache2, testCache2a), success, rc); */
	SHC_TEST_ASSERT("crashCntrTest", CompositeCacheTest::crashCntrTest(vm, testCache1, testCache1a), success, rc);
	SHC_TEST_ASSERT("corruptTest", CompositeCacheTest::corruptTest(vm, testCache1, testCache1a), success, rc);

	if (testCache1) j9mem_free_memory(testCache1);
	if (testCache1a) j9mem_free_memory(testCache1a);
	if (testCache2) j9mem_free_memory(testCache2);
	if (testCache2a) j9mem_free_memory(testCache2a);

	UnitTest::unitTest = UnitTest::NO_TEST;
	REPORT_SUMMARY("CompositeCache", success);

	return success;
}


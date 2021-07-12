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
 
/* Includes */
#include "CacheMap.hpp"
#include "ClasspathItem.hpp"
#include "CompositeCache.hpp"
#include "UnitTest.hpp"
#include "SCTestCommon.h"
#include "sharedconsts.h"
#include "j9port.h"
#include "main.h"
#include "j9.h"
#include <string.h>

#define TEST_METHOD_NAME "testMethod"
#define TEST_METHOD_SIG "()"
#define METHOD_SPEC "*." TEST_METHOD_NAME "" TEST_METHOD_SIG
#define METHOD_NAME_SIZE (sizeof(TEST_METHOD_NAME) + sizeof(J9UTF8))
#define METHOD_SIG_SIZE (sizeof(TEST_METHOD_SIG) + sizeof(J9UTF8))
#define NUM_ROMMETHOD 4

typedef char* BlockPtr;
/* Exports */
extern "C" 
{ 
	IDATA testCompiledMethod(J9JavaVM* vm); 
}

/* Function prototypes */
IDATA storeAndFindTest(J9JavaVM* currentThread);

/* Main test function */
IDATA testCompiledMethod(J9JavaVM* vm)
{
	UDATA success = PASS;
	UDATA rc = 0;
	
	PORT_ACCESS_FROM_JAVAVM(vm);
	
	REPORT_START("Compiled Method");

	UnitTest::unitTest = UnitTest::COMPILED_METHOD_TEST;

	SHC_TEST_ASSERT("Store and find test", storeAndFindTest(vm), success, rc);

	UnitTest::unitTest = UnitTest::NO_TEST;

	REPORT_SUMMARY("Compiled Method", success);

	return success;
}

/* Individual test functions... */
IDATA storeAndFindTest(J9JavaVM* vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	const char* testName = "testCompiledMethod";
	IDATA rc = PASS;
	BlockPtr cache = NULL;
	BlockPtr cacheAllocated = NULL;
	SH_CacheMap* cacheObject1 = NULL;
	SH_CacheMap* cacheObject2 = NULL;
	void* cacheObjectMemory1 = NULL;
	void* cacheObjectMemory2 = NULL;
	J9SharedClassPreinitConfig* origPiConfig = NULL;
	J9SharedClassPreinitConfig* sharedpiConfig = NULL;
	J9SharedClassConfig* origSharedClassConfig = NULL;
	J9SharedClassConfig* sharedConfig = NULL;
	bool cacheHasIntegrity = false;
	const char* codeArray[NUM_ROMMETHOD];
	const char* dataArray[NUM_ROMMETHOD];
	IDATA dataSize1 = 0;
	IDATA codeSize1 = 0;
	IDATA dataSize2 = 0;
	IDATA codeSize2 = 0;
	IDATA dataSize3 = 0;
	IDATA codeSize3 = 0;
	IDATA dataSize5 = 0;
	IDATA codeSize5 = 0;
	J9ROMMethod* romMethod1 = NULL;
	J9ROMMethod* romMethod2 = NULL;
	J9ROMMethod* romMethod3 = NULL;
	J9ROMMethod* romMethod4 = NULL;
	J9ROMMethod* romMethod5 = NULL;
	const U_8* storedMethod1 = NULL;
	const U_8* storedMethod2 = NULL;
	const U_8* storedMethod3 = NULL;
	const U_8* foundMethod1 = NULL;
	const U_8* foundMethod2 = NULL;
	const U_8* foundMethod3 = NULL;
	const U_8* foundMethod4 = NULL;
	const U_8* duplicateMethod = NULL;
	const U_8* newMethod15 = NULL;
	const U_8* newMethod25 = NULL;
	void* romStartAddr = NULL;
	UDATA flags = 0;
	BlockPtr methodNameAndSigMem = NULL;
	char methodspec[] = METHOD_SPEC;
	IDATA cacheObjectSize = SH_CacheMap::getRequiredConstrBytes(false);
	/* j9mmap_get_region_granularity returns 0 on zOS */
	UDATA osPageSize = j9mmap_get_region_granularity(NULL);

	INFOPRINTF("Entering Store And Find Test\n");

	/* Create a shared config object and attach it to the VM */
	sharedConfig = (J9SharedClassConfig*)j9mem_allocate_memory(sizeof(J9SharedClassConfig) + sizeof(J9SharedClassCacheDescriptor), J9MEM_CATEGORY_CLASSES);
	if (NULL == sharedConfig) {
		ERRPRINTF("Failed to allocate memory for sharedConfig\n");
		rc = FAIL;
		goto cleanup;
	}

	memset(sharedConfig, 0, sizeof(J9SharedClassConfig) + sizeof(J9SharedClassCacheDescriptor));
	sharedConfig->softMaxBytes = -1;
	sharedConfig->minAOT = -1;
	sharedConfig->maxAOT = -1;
	sharedConfig->minJIT = -1;
	sharedConfig->maxJIT = -1;
	sharedConfig->cacheDescriptorList = (J9SharedClassCacheDescriptor*)((UDATA)sharedConfig + sizeof(J9SharedClassConfig));
	sharedConfig->cacheDescriptorList->next = sharedConfig->cacheDescriptorList;
	sharedConfig->cacheDescriptorList->previous = sharedConfig->cacheDescriptorList;

	sharedpiConfig = (J9SharedClassPreinitConfig*)j9mem_allocate_memory(sizeof(J9SharedClassPreinitConfig), J9MEM_CATEGORY_CLASSES);
	if (NULL == sharedpiConfig) {
		ERRPRINTF("Failed to allocate memory for sharedpiConfig\n");
		rc = FAIL;
		goto cleanup;
	}

	memset(sharedpiConfig, 0, sizeof(J9SharedClassPreinitConfig));

	sharedConfig->runtimeFlags =
		J9SHR_RUNTIMEFLAG_ENABLE_LOCAL_CACHEING          |
		J9SHR_RUNTIMEFLAG_ENABLE_TIMESTAMP_CHECKS        | 
		J9SHR_RUNTIMEFLAG_ENABLE_REDUCE_STORE_CONTENTION |
		J9SHR_RUNTIMEFLAG_ENABLE_BYTECODEFIX;

	origSharedClassConfig = vm->sharedClassConfig;
	origPiConfig = vm->sharedClassPreinitConfig;

	vm->sharedClassConfig = sharedConfig;
	vm->sharedClassPreinitConfig = sharedpiConfig;

	if (osPageSize != 0) {
		sharedpiConfig->sharedClassCacheSize = ROUND_UP_TO(osPageSize, SMALL_CACHE_SIZE);
	} else {
		sharedpiConfig->sharedClassCacheSize = SMALL_CACHE_SIZE;
	}

	sharedpiConfig->sharedClassDebugAreaBytes = -1;
	sharedpiConfig->sharedClassReadWriteBytes = 0;
	sharedpiConfig->sharedClassMinAOTSize = -1;
	sharedpiConfig->sharedClassMaxAOTSize = -1;
	sharedpiConfig->sharedClassMinJITSize = -1;
	sharedpiConfig->sharedClassMaxJITSize = -1;
	sharedpiConfig->sharedClassSoftMaxBytes = -1;

	/* Allocate and initialize the required memory */
	cacheAllocated = (BlockPtr)j9mem_allocate_memory(sharedpiConfig->sharedClassCacheSize + osPageSize, J9MEM_CATEGORY_CLASSES);

	if (NULL == cacheAllocated) {
		ERRPRINTF("Failed to allocate memory for cacheAllocated\n");
		rc = FAIL;
		goto cleanup;
	}
	memset(cacheAllocated, 0, sharedpiConfig->sharedClassCacheSize + osPageSize);
	if (osPageSize != 0) {
		cache = (BlockPtr) ROUND_UP_TO(osPageSize, (UDATA)cacheAllocated);
	} else {
		cache = cacheAllocated;
	}

	cacheObjectMemory1 = j9mem_allocate_memory(cacheObjectSize, J9MEM_CATEGORY_CLASSES);
	if (NULL == cacheObjectMemory1) {
		ERRPRINTF("Failed to allocate memory for cacheObjectMemory1\n");
		rc = FAIL;
		goto cleanup;
	}

	memset(cacheObjectMemory1, 0, cacheObjectSize);

	/* Create and initialize the cache map object */
	cacheObject1 = SH_CacheMap::newInstance(vm, sharedConfig, (SH_CacheMap*)cacheObjectMemory1, "cache1", 0);

  	/* Start the cache object */

	rc = cacheObject1->startup(vm->mainThread, sharedpiConfig, "Root1", NULL, J9SH_DIRPERM_ABSENT, cache, &cacheHasIntegrity);

	/* Report progress so far */
	INFOPRINTF5("Store And Find Test cos=%d cs=%d co=%x cba=%x rc=%d\n", cacheObjectSize, sharedpiConfig->sharedClassCacheSize, cacheObject1, cache, rc);
	if (0 != rc) {
		ERRPRINTF("Failed to startup cacheObject1\n");
		rc = FAIL;
		goto cleanup;
	}

	/* Set the store methods' code and data */
	dataArray[0] = "0123456789";
	codeArray[0] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	dataSize1 = strlen(dataArray[0]);
	codeSize1 = strlen(codeArray[0]);

	dataArray[1] = "qwerty";
	codeArray[1] = "6782437329202232780";
	dataSize2 = strlen(dataArray[1]);
	codeSize2 = strlen(codeArray[1]);

	dataArray[2] = "98765432100123456789";
	codeArray[2] = "MNOPQRSTUVWXYZABCDEFGHIJKLXYZ";
	dataSize3 = strlen(dataArray[2]);
	codeSize3 = strlen(codeArray[2]);

	dataArray[3] = "Compiled Method 5 Data";
	codeArray[3] = "Compiled Method 5 Code Segment";
	dataSize5 = strlen(dataArray[3]);
	codeSize5 = strlen(codeArray[3]);

	/* Choose an address for each of 5 rom methods. Only romMethod1, romMethod2 and romMethod3 are added to the cache
	 * NB: This is more than a bit gash. It relies on the fact that the cache only requires the
	 * address of a rom method
	 */

	/* We will set method name and signature for romMethod1. Reserve enough space between romMethod1 and its next method (romMethod5) in the cache */
	cacheObject1->getRomClassAreaBounds(&romStartAddr, NULL);
	romMethod1 = (J9ROMMethod*)romStartAddr;
	romMethod5 = (J9ROMMethod*)((UDATA)romMethod1 + (sizeof(J9ROMMethod) + METHOD_NAME_SIZE + METHOD_SIG_SIZE));
	romMethod4 = (J9ROMMethod*)((UDATA)romMethod5 + sizeof(J9ROMMethod));
	romMethod3 = (J9ROMMethod*)((UDATA)romMethod4 + sizeof(J9ROMMethod));
	romMethod2 = (J9ROMMethod*)((UDATA)romMethod3 + sizeof(J9ROMMethod));

	/* Set the method name and signature */
	methodNameAndSigMem = (BlockPtr)((UDATA)romMethod1 + sizeof(J9ROMMethod));
	J9UTF8_SET_LENGTH(methodNameAndSigMem, (U_16)strlen(TEST_METHOD_NAME));
	j9str_printf(PORTLIB, (BlockPtr) (J9UTF8_DATA(methodNameAndSigMem)), sizeof(TEST_METHOD_NAME), "%s", TEST_METHOD_NAME);
	NNSRP_SET(romMethod1->nameAndSignature.name, (J9UTF8*)methodNameAndSigMem);

	J9UTF8_SET_LENGTH((methodNameAndSigMem + METHOD_NAME_SIZE), (U_16)strlen(TEST_METHOD_SIG));
	j9str_printf(PORTLIB, (BlockPtr) (J9UTF8_DATA(methodNameAndSigMem + METHOD_NAME_SIZE)), sizeof(TEST_METHOD_SIG), "%s",TEST_METHOD_SIG);
	NNSRP_SET(romMethod1->nameAndSignature.signature, (J9UTF8*)(methodNameAndSigMem + METHOD_NAME_SIZE));

	/* PHASE 1
  	 * Store the "compiled" methods in the cache
  	 */
	storedMethod1 = cacheObject1->storeCompiledMethod(vm->mainThread, romMethod1, (const U_8*)dataArray[0], dataSize1, (const U_8*)codeArray[0], codeSize1, FALSE);
	storedMethod2 = cacheObject1->storeCompiledMethod(vm->mainThread, romMethod2, (const U_8*)dataArray[1], dataSize2, (const U_8*)codeArray[1], codeSize2, FALSE);
	storedMethod3 = cacheObject1->storeCompiledMethod(vm->mainThread, romMethod3, (const U_8*)dataArray[2], dataSize3, (const U_8*)codeArray[2], codeSize3, FALSE);

	/* Check the store method's return values */
	if ((0 == storedMethod1) || (0 == storedMethod2) || (0 == storedMethod3)) {
		ERRPRINTF("A storeCompiledMethod returned null\n");
		rc = FAIL;
		goto cleanup;
	}

	if ( (memcmp(storedMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"             , dataSize1 + codeSize1) != 0) ||
			 (memcmp(storedMethod2, "qwerty6782437329202232780"                        , dataSize2 + codeSize2) != 0) ||
			 (memcmp(storedMethod3, "98765432100123456789MNOPQRSTUVWXYZABCDEFGHIJKLXYZ", dataSize3 + codeSize3) != 0)    ) {
		ERRPRINTF("storeCompiledMethod returned incorrect data\n");
		rc = FAIL;
		goto cleanup;
	}

	/* Find and retrieve the "compiled" methods from the cache */
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod1, NULL);
	foundMethod2 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod2, NULL);
	foundMethod3 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod3, NULL);
	foundMethod4 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod4, NULL);

	/* Check the store method's return value */
	if ((0 == foundMethod1) || (0 == foundMethod2) || (0 == foundMethod3) || (0 != foundMethod4)) {
		ERRPRINTF("A findCompiledMethod returned null or pointer inappropriately\n");
		rc = FAIL;
		goto cleanup;
	}

	if ( (memcmp(foundMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"             , dataSize1 + codeSize1) != 0) ||
			 (memcmp(foundMethod2, "qwerty6782437329202232780"                        , dataSize2 + codeSize2) != 0) ||
			 (memcmp(foundMethod3, "98765432100123456789MNOPQRSTUVWXYZABCDEFGHIJKLXYZ", dataSize3 + codeSize3) != 0)    ) {
		ERRPRINTF("findCompiledMethod returned incorrect data\n");
		rc = FAIL;
		goto cleanup;
	}

	INFOPRINTF("Phase1 passed: cacheObject1 has stored and found the compiled methods\n");

	/* PHASE 2
	 * Create and initialize a second cache map object using the same cache. This will test its
	 * ability to load from an already populated cache...
	 * Allocate and initialize the required memory
	 */
	cacheObjectMemory2 = j9mem_allocate_memory(cacheObjectSize, J9MEM_CATEGORY_CLASSES);
	if (NULL == cacheObjectMemory2) {
		ERRPRINTF("Failed to allocate memory for cacheObjectMemory2\n");
		rc = FAIL;
		goto cleanup;
	}

	memset(cacheObjectMemory2, 0, cacheObjectSize);

	/* Create a cache map object */
	cacheObject2 = SH_CacheMap::newInstance(vm, sharedConfig, (SH_CacheMap*)cacheObjectMemory2, "cache1", 0);

	/* Start the cache object */
	rc = cacheObject2->startup(vm->mainThread, sharedpiConfig, "Root1", NULL, J9SH_DIRPERM_ABSENT, cache, &cacheHasIntegrity);

	if (0 != rc) {
		ERRPRINTF("Failed to startup cacheObject2\n");
		rc = FAIL;
		goto cleanup;
	}

	/* Find and retrieve the same "compiled" methods via the second cache object */
	foundMethod1 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod1, NULL);
	foundMethod2 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod2, NULL);
	foundMethod3 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod3, NULL);
	foundMethod4 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod4, NULL);

	/* Check the store method's return value */
	if ((0 == foundMethod1) || (0 == foundMethod2) || (0 == foundMethod3)|| (0 != foundMethod4)) {
		ERRPRINTF("A findCompiledMethod returned null or pointer inappropriately via 2nd cache object\n");
		rc = FAIL;
		goto cleanup;
	}

	if ( (memcmp(foundMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"             , dataSize1 + codeSize1) != 0) ||
			 (memcmp(foundMethod2, "qwerty6782437329202232780"                        , dataSize2 + codeSize2) != 0) ||
			 (memcmp(foundMethod3, "98765432100123456789MNOPQRSTUVWXYZABCDEFGHIJKLXYZ", dataSize3 + codeSize3) != 0)    ) {
		ERRPRINTF("findCompiledMethod returned incorrect data via 2nd cache object\n");
		rc = FAIL;
		goto cleanup;
	}

	/* Check that they can still be found and retrieved via the first cache object */
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod1, NULL);
	foundMethod2 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod2, NULL);
	foundMethod3 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod3, NULL);
	foundMethod4 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod4, NULL);

	/* Check the store method's return value */
	if ((0 == foundMethod1) || (0 == foundMethod2) || (0 == foundMethod3)|| (0 != foundMethod4)) {
		ERRPRINTF("A findCompiledMethod returned null or pointer inappropriately via 2nd cache object\n");
		rc = FAIL;
		goto cleanup;
	}

	if ( (memcmp(foundMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"             , dataSize1 + codeSize1) != 0) ||
			 (memcmp(foundMethod2, "qwerty6782437329202232780"                        , dataSize2 + codeSize2) != 0) ||
			 (memcmp(foundMethod3, "98765432100123456789MNOPQRSTUVWXYZABCDEFGHIJKLXYZ", dataSize3 + codeSize3) != 0)    ) {
		ERRPRINTF("findCompiledMethod returned incorrect data via 2nd cache object\n");
		rc = FAIL;
		goto cleanup;
	}

	INFOPRINTF("Phase2 passed: cacheObject1 and cacheObject2 have found the compiled methods\n");


	/* PHASE 3
	 * Add an extra method via the second object and verify that it's visible via both...
	 * Store the extra "compiled" method in the cache using the second cache object
	 */
	cacheObject2->storeCompiledMethod(vm->mainThread, romMethod5, (const U_8*)dataArray[3], dataSize5, (const U_8*)codeArray[3], codeSize5, FALSE);

	/* Check that the new method can be found and retrieved via the first cache object (ie not the one
	 * by which it was added) and, afterwards via the second cache object
	 */
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod5, NULL);
	foundMethod2 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod5, NULL);

	/* Check the store method's return value */
	if ((0 == foundMethod1) || (0 == foundMethod2)) {
		ERRPRINTF("A findCompiledMethod returned null or pointer inappropriately after adding another\n");
		rc = FAIL;
		goto cleanup;
	}

	if ( (memcmp(foundMethod1, "Compiled Method 5 DataCompiled Method 5 Code Segment", dataSize5 + codeSize5) != 0) ||
			 (memcmp(foundMethod2, "Compiled Method 5 DataCompiled Method 5 Code Segment", dataSize5 + codeSize5) != 0)    ) {
		ERRPRINTF("findCompiledMethod returned incorrect data after adding another\n");
		rc = FAIL;
		goto cleanup;
	}

	INFOPRINTF("Phase3 passed: cacheObject1 and cacheObject2 have found the extra compiled method\n");


	/* PHASE 4
	 * Attempt to add a duplicate record. Just to be difficult, the attempt is made to a
	 * different cache object than the original was added through
	 */
	if (cacheObject1->storeCompiledMethod(vm->mainThread, romMethod5, (const U_8*)dataArray[3], dataSize5, (const U_8*)codeArray[3], codeSize5, FALSE) != (U_8*)J9SHR_RESOURCE_STORE_EXISTS) {
		ERRPRINTF("A storeCompiledMethod failed to return J9SHR_RESOURCE_STORE_EXISTS as expected\n");
		rc = FAIL;
		goto cleanup;
	}

	/* Force replacement of the method and force the original to become stale */
	duplicateMethod = cacheObject1->storeCompiledMethod(vm->mainThread, romMethod5, (const U_8*)dataArray[3], dataSize5, (const U_8*)codeArray[3], codeSize5, TRUE);
	newMethod15 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod5, NULL);
	newMethod25 = cacheObject2->findCompiledMethod(vm->mainThread, romMethod5, NULL);
	if ((duplicateMethod != newMethod25) || (newMethod25 == foundMethod2) || (newMethod15 != newMethod25)) {
		ERRPRINTF("A storeCompiledMethod failed to force replacement of an existing method\n");
		rc = FAIL;
		goto cleanup;
	}

	if (memcmp(duplicateMethod, "Compiled Method 5 DataCompiled Method 5 Code Segment", dataSize5 + codeSize5) != 0) {
		ERRPRINTF("storeCompiledMethod returned incorrect data after adding duplicate\n");
		rc = FAIL;
		goto cleanup;
	}

	INFOPRINTF("Phase4 passed: The compiled method is successfully replaced in the cache\n");

	/* PHASE 5 Test the invalidate/revalidate operation on compiled method.
	 * Attempt to invalidate the method
	 */
	if (0 == cacheObject1->aotMethodOperation(vm->mainThread, methodspec, SHR_INVALIDATE_AOT_METHOTHODS)) {
		ERRPRINTF("aotMethodOperation failed to invalidate method1\n");
		rc = FAIL;
		goto cleanup;
	}
	/* Try finding the method. It should return NULL. */
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod1, &flags);
	if (NULL != foundMethod1) {
		ERRPRINTF("A findCompiledMethod failed to return NULL for an invalidated method \n");
		rc = FAIL;
		goto cleanup;
	}
	if (J9_ARE_NO_BITS_SET(flags, J9SHR_AOT_METHOD_FLAG_INVALIDATED)) {
		ERRPRINTF("A findCompiledMethod failed to set J9SHR_AOT_METHOD_FLAG_INVALIDATED for an invalidated method \n");
		rc = FAIL;
		goto cleanup;
	}
	/* Try storing the method again. It should fail. */
	if (cacheObject1->storeCompiledMethod(vm->mainThread, romMethod1, (const U_8*)dataArray[0], dataSize1, (const U_8*)codeArray[0], codeSize1, FALSE) != (U_8*)J9SHR_RESOURCE_STORE_INVALIDATED) {
		ERRPRINTF("A storeCompiledMethod failed to return J9SHR_RESOURCE_STORE_INVALIDATED as expected\n");
		rc = FAIL;
		goto cleanup;
	}
	/* Revalidate the method */
	if (0 == cacheObject1->aotMethodOperation(vm->mainThread, methodspec, SHR_REVALIDATE_AOT_METHOTHODS)) {
		ERRPRINTF("aotMethodOperation failed to revalidate method1 as expected\n");
		rc = FAIL;
	}
	/* Try finding the method again, should be able to find */
	flags = 0;
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod1, &flags);
	if (NULL == foundMethod1) {
		ERRPRINTF("A findCompiledMethod failed to find the revalidated method as expected\n");
		rc = FAIL;
		goto cleanup;
	}
	if (0 != memcmp(foundMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", dataSize1 + codeSize1)) {
		ERRPRINTF("findCompiledMethod returned incorrect data when try finding the revalidated method\n");
		rc = FAIL;
		goto cleanup;
	}
	if (J9_ARE_ALL_BITS_SET(flags, J9SHR_AOT_METHOD_FLAG_INVALIDATED)) {
		ERRPRINTF("A findCompiledMethod should not set J9SHR_AOT_METHOD_FLAG_INVALIDATED for an revalidated method\n");
		rc = FAIL;
		goto cleanup;
	}
	/* Invalidate the method again */
	if (0 == cacheObject1->aotMethodOperation(vm->mainThread, methodspec, SHR_INVALIDATE_AOT_METHOTHODS)) {
		ERRPRINTF("aotMethodOperation failed to invalidate method1 as expected\n");
		rc = FAIL;
		goto cleanup;
	}
	/* Force replacing the invalidated method */
	duplicateMethod = cacheObject1->storeCompiledMethod(vm->mainThread, romMethod1, (const U_8*)dataArray[0], dataSize1, (const U_8*)codeArray[0], codeSize1, TRUE);
	if (NULL == duplicateMethod) {
		ERRPRINTF("A storeCompiledMethod failed to force replacing the invalidated method as expected\n");
		rc = FAIL;
	}
	/* Try finding again, should be able to find */
	foundMethod1 = cacheObject1->findCompiledMethod(vm->mainThread, romMethod1, &flags);
	if (NULL == foundMethod1) {
		ERRPRINTF("A findCompiledMethod failed to find the duplicated method as expected\n");
		rc = FAIL;
		goto cleanup;
	}
	if (0 != memcmp(foundMethod1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", dataSize1 + codeSize1)) {
		ERRPRINTF("findCompiledMethod returned incorrect data when try finding the duplicate method\n");
		rc = FAIL;
		goto cleanup;
	}
	INFOPRINTF("Phase5 passed: Invalidate and revalidate operation is successful\n");


cleanup:
	if (NULL != cacheObject1) {
		cacheObject1->cleanup(vm->mainThread);
		j9mem_free_memory(cacheObject1);
		cacheObject1 = NULL;
	}
	if (NULL != cacheObject2) {
		cacheObject2->cleanup(vm->mainThread);
		j9mem_free_memory(cacheObject2);
		cacheObject2 = NULL;
	}
	if (NULL != sharedConfig) {
		vm->sharedClassConfig = origSharedClassConfig;
		j9mem_free_memory(sharedConfig);
	}
	if (NULL != sharedpiConfig) {
		vm->sharedClassPreinitConfig = origPiConfig;
		j9mem_free_memory(sharedpiConfig);
	}

	if (NULL != cacheAllocated) {
		j9mem_free_memory(cacheAllocated);
		cacheAllocated = NULL;
		cache = NULL;
	}

	return rc;
}


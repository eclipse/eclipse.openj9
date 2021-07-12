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
#include "OpenCacheHelper.h"


struct AOTSharedData {
	U_8* romMethod;
	U_8* data;
	IDATA dataSize;
	U_8* code;
	IDATA codeSize;
};

struct minmaxStruct AOTminmaxvalue[] = {
		/* This test should fail when storing last object as it overshoot cache freesize */
		{-1,-1, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as it overshoot cache freesize(~maxAOT) */
		{-1, SMALL_CACHE_SIZE, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as cumulative size overshoot cache freesize, minAOT sets back to cachesize */
		{SMALL_CACHE_SIZE + 512, -1, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as cumulative size overshoot cache freesize(~maxAOT) */
		{SMALL_CACHE_SIZE/4, SMALL_CACHE_SIZE, NUM_DATA_OBJECTS-1},
		/* This should fail when storing first object as it overshoot maxAOT */
		{SMALL_CACHE_SIZE/4, SMALL_CACHE_SIZE/4, 0},
		/* This should fail when storing first object as maxAOT=0 meaning no space for AOT */
		{0, 0 , 0}
};

extern "C" {
	IDATA testAOTDataMinMax(J9JavaVM* vm);
}

class AOTDataMinMaxTest: public OpenCacheHelper {
private:
	U_8 nextValue;
	AOTSharedData *dataList;
	IDATA populateAOTDataList(AOTSharedData *dataList);

public:
	J9SharedClassPreinitConfig *cacheDetails;
	J9SharedClassConfig *sharedclassconfig;
	IDATA openTestCache(I_32 cacheSize=SMALL_CACHE_SIZE, I_32 minaot=-1, I_32 maxaot=-1);
	void closeTestCache(bool freeCache);
	IDATA initializeAOTData();
	IDATA freeAOTData();
	IDATA StoreAOTDataFailureOnMinMaxTest(IDATA iteration);

	AOTDataMinMaxTest(J9JavaVM* vm) :
		OpenCacheHelper(vm), nextValue(0), dataList(NULL), cacheDetails(NULL), sharedclassconfig(NULL)
	{
	}
};

IDATA
AOTDataMinMaxTest::openTestCache(I_32 cacheSize, I_32 minaot, I_32 maxaot)
{
	IDATA rc = 0;
	I_32 cacheType = 0;
	bool startupWillFail = false;
	U_64 extraRuntimeFlags ;
	U_64 unsetRuntimeFlags = J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_RW | J9SHR_RUNTIMEFLAG_ENABLE_MPROTECT_PARTIAL_PAGES;
	UDATA extraVerboseFlags = J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT;

	const char * testName = "openTestCache";

	PORT_ACCESS_FROM_JAVAVM(vm);

	cacheDetails = (J9SharedClassPreinitConfig *) j9mem_allocate_memory(sizeof(J9SharedClassPreinitConfig), J9MEM_CATEGORY_CLASSES);
	if (NULL == cacheDetails) {
		ERRPRINTF("failed to allocate memory for J9SharedClassPreinitConfig");
		rc = FAIL;
		goto done;
	}
	memset(cacheDetails, 0, sizeof(J9SharedClassPreinitConfig));

	cacheDetails->sharedClassDebugAreaBytes = 0;
	cacheDetails->sharedClassSoftMaxBytes = -1;
	cacheDetails->sharedClassCacheSize = cacheSize;
	cacheDetails->sharedClassMinAOTSize = minaot;
	cacheDetails->sharedClassMaxAOTSize = maxaot;
	ensureCorrectCacheSizes(vm, vm->portLibrary, 0, (UDATA)0, cacheDetails);
	extraRuntimeFlags = getDefaultRuntimeFlags();

	sharedclassconfig = (J9SharedClassConfig *) j9mem_allocate_memory(sizeof(J9SharedClassConfig) + sizeof(J9SharedClassCacheDescriptor), J9MEM_CATEGORY_CLASSES);
	if (NULL == sharedclassconfig) {
		ERRPRINTF("failed to allocate memory for J9SharedClassConfig");
		rc = FAIL;
		goto done;
	}
	memset(sharedclassconfig, 0, sizeof(J9SharedClassConfig)+sizeof(J9SharedClassCacheDescriptor));
	/* populate AttachedData APIs */
	sharedclassconfig->storeCompiledMethod = j9shr_storeCompiledMethod;
	sharedclassconfig->freeAttachedDataDescriptor = j9shr_freeAttachedDataDescriptor;

	rc = OpenCacheHelper::openTestCache(cacheType, cacheSize, NULL, true, NULL, NULL, NULL, extraRuntimeFlags, unsetRuntimeFlags, UnitTest::MINMAX_TEST, extraVerboseFlags, "openTestCache", startupWillFail, true, cacheDetails, sharedclassconfig);

done:
    if(FAIL == rc) {
		if (NULL != cacheDetails) {
			j9mem_free_memory(cacheDetails);
			cacheDetails = NULL;
		}
		if (NULL != sharedclassconfig) {
			j9mem_free_memory(sharedclassconfig);
			sharedclassconfig = NULL;
		}
    }
    return (rc);
}

void
AOTDataMinMaxTest::closeTestCache(bool freeCache)
{
	OpenCacheHelper::closeTestCache(freeCache);
	cacheDetails = NULL;
	sharedclassconfig = NULL;
}

IDATA
AOTDataMinMaxTest::populateAOTDataList(AOTSharedData *dataList)
{
	SH_CacheMap *cacheMap;
	SH_CompositeCacheImpl *cc;
	U_8 *keyBaseAddress;
	J9SharedClassJavacoreDataDescriptor descriptor;
	const char *testName = "populateAOTDataList";
	J9VMThread *currentThread;
	UDATA freeSegmentBytes = 0;
	IDATA rc = PASS;

	PORT_ACCESS_FROM_JAVAVM(vm);
	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	keyBaseAddress = (U_8 *)cc->getSegmentAllocPtr();
	memset(&descriptor, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
	currentThread = vm->internalVMFunctions->currentVMThread(vm);
	UnitTest::unitTest = UnitTest::MINMAX_TEST;
	if (0 == ((SH_CacheMap*)cacheMap)->getJavacoreData(currentThread->javaVM, &descriptor)) {
		rc = FAIL;
		ERRPRINTF("getJavacoreData() failed");
		return rc;
	}
	freeSegmentBytes = cc->getFreeBytes();
	UnitTest::unitTest = UnitTest::NO_TEST;
    for(IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
		IDATA j;
		/* use any address beyond segmentSRP */
		dataList[i].romMethod = keyBaseAddress + (100*i);
		dataList[i].dataSize = freeSegmentBytes / (NUM_DATA_OBJECTS*2);
		dataList[i].data = (U_8 *)j9mem_allocate_memory(dataList[i].dataSize, J9MEM_CATEGORY_CLASSES);
		for(j = 0; j < (I_32)dataList[i].dataSize; j++) {
			dataList[i].data[j] = (U_8)(nextValue++);
		}
		dataList[i].codeSize = freeSegmentBytes / (NUM_DATA_OBJECTS*2);
		dataList[i].code = (U_8 *)j9mem_allocate_memory(dataList[i].codeSize, J9MEM_CATEGORY_CLASSES);
		for(j = 0; j < (I_32)dataList[i].codeSize; j++) {
			dataList[i].code[j] = (U_8)(nextValue++);
		}
	}
	return rc;
}


IDATA
AOTDataMinMaxTest::initializeAOTData()
{
	IDATA rc = PASS;
	const char * testName = "initializeAOTData";
	PORT_ACCESS_FROM_JAVAVM(vm);

	nextValue = 0;
	dataList = (AOTSharedData *) j9mem_allocate_memory(sizeof(AOTSharedData)*NUM_DATA_OBJECTS, J9MEM_CATEGORY_CLASSES);
	if (NULL == dataList) {
		ERRPRINTF("failed to allocate memory for AOTData list");
		rc = FAIL;
	} else {
		rc = populateAOTDataList(dataList);
	}

	return rc;
}

IDATA
AOTDataMinMaxTest::freeAOTData()
{
	IDATA rc = PASS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (NULL != dataList) {
		for(IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
			if (NULL != dataList[i].data) {
				j9mem_free_memory(dataList[i].data);
				dataList[i].data = NULL;
			}
			if (NULL != dataList[i].code) {
				j9mem_free_memory(dataList[i].code);
				dataList[i].code = NULL;
			}
		}
		j9mem_free_memory(dataList);
		dataList = NULL;
	}

	return rc;
}


IDATA
AOTDataMinMaxTest::StoreAOTDataFailureOnMinMaxTest(IDATA iteration)
{
	IDATA rc = PASS;
	const U_8* storedMethod;
	J9VMThread *currentThread;
	J9SharedClassJavacoreDataDescriptor descriptor;
	SH_CacheMap *cacheMap;
	const char * testName = "StoreAOTDataFailureOnMinMaxTest";

	PORT_ACCESS_FROM_JAVAVM(vm);

	currentThread = vm->internalVMFunctions->currentVMThread(vm);
	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;

	for (IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
		UnitTest::unitTest = UnitTest::NO_TEST;
		storedMethod =  vm->sharedClassConfig->storeCompiledMethod(currentThread,(const J9ROMMethod *)dataList[i].romMethod, dataList[i].data, dataList[i].dataSize, dataList[i].code, dataList[i].codeSize, FALSE);
		UDATA rv = (UDATA)storedMethod;
		UnitTest::unitTest = UnitTest::MINMAX_TEST;
		memset(&descriptor, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
		if (0 == ((SH_CacheMap*)cacheMap)->getJavacoreData(currentThread->javaVM, &descriptor)) {
			rc = FAIL;
			ERRPRINTF("getJavacoreData failed");
			return rc;
		}
		switch(rv) {
			case 0:
				ERRPRINTF1("storeCompiledMethod returned null for data index %d", i);
				rc = FAIL;
				break;
			case J9SHR_RESOURCE_STORE_FULL:
				if ((FAIL != rc) && (descriptor.aotBytes <= (U_32) descriptor.maxAOT) && (AOTminmaxvalue[iteration].failingIndex <= i)) {
					INFOPRINTF5("Test passed with aotBytes=%d minAOT=%d maxAOT=%d failingIndex=%d for data index: %d", descriptor.aotBytes, descriptor.minAOT, descriptor.maxAOT, AOTminmaxvalue[iteration].failingIndex, i);
				} else {
					ERRPRINTF4("Failed with aotBytes=%d maxAOT=%d failingIndex=%d for data index: %d", descriptor.aotBytes, descriptor.maxAOT, AOTminmaxvalue[i].failingIndex, i);
					rc = FAIL;
				}
				break;
			case J9SHR_RESOURCE_STORE_EXISTS:
			case J9SHR_RESOURCE_STORE_ERROR:
				ERRPRINTF2("storeCompiledMethod failed with return code=%d for data index: %d",rv,i);
				rc = FAIL;
				break;
			default:
				INFOPRINTF1("storeCompiledMethod successful for data index: %d", i);
				break;
		}
		if (FAIL == rc) {
			break;
		}
	}
	UnitTest::unitTest = UnitTest::NO_TEST;

	return rc;
}



extern "C" {

IDATA
testAOTDataMinMax(J9JavaVM* vm)
{
	IDATA rc = PASS;
	const char *testName = "testAOTDataMinMax";

	PORT_ACCESS_FROM_JAVAVM(vm);

	REPORT_START("AOTDataMinMaxTest");

	AOTDataMinMaxTest adt(vm);

	/*
	 * Run tests with for max/min aot values with varied cache sizes
	 */
	const IDATA numtests = (sizeof(AOTminmaxvalue)/sizeof(struct minmaxStruct));
	for (IDATA i=0; i < numtests; i++) {
		j9tty_printf(PORTLIB, "\n");
		INFOPRINTF3("Running test with new cache for cache size:%d minAOT:%d maxAOT:%d", SMALL_CACHE_SIZE, AOTminmaxvalue[i].min, AOTminmaxvalue[i].max);
		rc = adt.openTestCache(SMALL_CACHE_SIZE, AOTminmaxvalue[i].min, AOTminmaxvalue[i].max);
		if (FAIL == rc) {
			ERRPRINTF("openTestCache failed");
			adt.closeTestCache(true);
			break;
		}
		rc = adt.initializeAOTData();
		if (FAIL == rc) {
			ERRPRINTF("initializeAOTData failed");
			adt.freeAOTData();
			adt.closeTestCache(true);
			break;
		}
		rc = adt.StoreAOTDataFailureOnMinMaxTest(i) ;
		if (FAIL == rc) {
			ERRPRINTF1("StoreAOTDataFailureOnMinMaxTest failed for iteration=%d",i);
			adt.freeAOTData();
			adt.closeTestCache(true);
			break;
		}

		adt.freeAOTData();
		adt.closeTestCache(true);
	}
	j9tty_printf(PORTLIB, "\n");
	REPORT_SUMMARY("AOTDataMinMaxTest", rc);
	return rc;
}
} /* extern "C" */

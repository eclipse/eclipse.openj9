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


struct SharedData {
	void *keyAddress;
	J9SharedDataDescriptor data;
};

struct minmaxStruct JITminmaxvalue[] = {
		/* This test should fail when storing last object as it overshoot cache freesize */
		{-1,-1, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as it overshoot cache freesize(~maxJIT) */
		{-1, SMALL_CACHE_SIZE, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as cumulative size overshoot cache freesize, minJIT sets back to cachesize */
		{SMALL_CACHE_SIZE + 512, -1, NUM_DATA_OBJECTS-1},
		/* This should fail when storing last object as cumulative size overshoot cache freesize(~maxJIT) */
		{SMALL_CACHE_SIZE/4, SMALL_CACHE_SIZE, NUM_DATA_OBJECTS-1},
		/* This should fail when storing first object as it overshoot maxJIT */
		{SMALL_CACHE_SIZE/4, SMALL_CACHE_SIZE/4, 0},
		/* This should fail when storing first object as maxAOT=0 meaning no space for JIT profile */
		{0, 0 , 0}
};

extern "C" {
	IDATA testAttachedDataMinMax(J9JavaVM* vm, U_16 attachedDataType);
}

class AttachedDataMinMaxTest: public OpenCacheHelper  {
private:
	U_8 nextValue;
	SharedData *dataList;
	IDATA populateAttachedDataList(SharedData *dataList);

public:
	J9SharedClassPreinitConfig *cacheDetails;
	J9SharedClassConfig *sharedclassconfig;
	IDATA openTestCache(I_32 cacheSize=SMALL_CACHE_SIZE, I_32 minjit=-1, I_32 maxjit=-1);
	void setType(U_16 type );
	void closeTestCache(bool freeCache);
	IDATA initializeAttachedData(I_32 cacheSize=SMALL_CACHE_SIZE);
	IDATA freeAttachedData();
	IDATA StoreAttachedDataFailureOnMinMaxTest(IDATA iteration, U_16 attachedDataType);

	AttachedDataMinMaxTest(J9JavaVM* vm) :
			OpenCacheHelper(vm), nextValue(0), dataList(NULL), cacheDetails(NULL), sharedclassconfig(NULL)
	{
	}
};

IDATA
AttachedDataMinMaxTest::openTestCache(I_32 cacheSize, I_32 minjit, I_32 maxjit)
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
	cacheDetails->sharedClassMinJITSize = minjit;
	cacheDetails->sharedClassMaxJITSize = maxjit;
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
	sharedclassconfig->storeAttachedData = j9shr_storeAttachedData;
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
AttachedDataMinMaxTest::closeTestCache(bool freeCache)
{
	OpenCacheHelper::closeTestCache(freeCache);
	cacheDetails = NULL;
	sharedclassconfig = NULL;
}

IDATA
AttachedDataMinMaxTest::populateAttachedDataList(SharedData *dataList)
{
	SH_CacheMap *cacheMap;
	SH_CompositeCacheImpl *cc;
	U_8 *keyBaseAddress;
	J9VMThread *currentThread;
	J9SharedClassJavacoreDataDescriptor descriptor;
	const char *testName = "populateAttachedDataList";
	UDATA freeSegmentBytes = 0;
	IDATA rc = PASS;

	PORT_ACCESS_FROM_JAVAVM(vm);
	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;
	cc = (SH_CompositeCacheImpl *)cacheMap->getCompositeCacheAPI();
	keyBaseAddress = (U_8 *)cc->getSegmentAllocPtr();
	memset(&descriptor, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
	UnitTest::unitTest = UnitTest::MINMAX_TEST;
	currentThread = vm->internalVMFunctions->currentVMThread(vm);
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
		dataList[i].keyAddress = keyBaseAddress + (100*i);
		dataList[i].data.length = freeSegmentBytes / NUM_DATA_OBJECTS;

		dataList[i].data.address = (U_8 *)j9mem_allocate_memory(dataList[i].data.length, J9MEM_CATEGORY_CLASSES);
		for(j = 0; j < (I_32)dataList[i].data.length; j++) {
			dataList[i].data.address[j] = (U_8)(nextValue++);
		}
	}
	return rc;
}

void
AttachedDataMinMaxTest::setType(U_16 type )
{
	for( IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
		dataList[i].data.type = type ;
		dataList[i].data.flags = J9SHR_ATTACHED_DATA_NO_FLAGS;
	}
}

IDATA
AttachedDataMinMaxTest::initializeAttachedData(I_32 cacheSize)
{
	IDATA rc = PASS;
	const char * testName = "initializeAttachedData";
	PORT_ACCESS_FROM_JAVAVM(vm);

	nextValue = 0;
	dataList = (SharedData *) j9mem_allocate_memory(sizeof(SharedData)*NUM_DATA_OBJECTS, J9MEM_CATEGORY_CLASSES);
	if (NULL == dataList) {
		ERRPRINTF("failed to allocate memory for AttachedData list");
		rc = FAIL;
	} else {
		rc = populateAttachedDataList(dataList);
	}
	return rc;
}

IDATA
AttachedDataMinMaxTest::freeAttachedData()
{
	IDATA rc = PASS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (NULL != dataList) {
		for(IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
			if (NULL != dataList[i].data.address) {
				j9mem_free_memory(dataList[i].data.address);
				dataList[i].data.address = NULL;
			}
		}
		j9mem_free_memory(dataList);
		dataList = NULL;
	}
	return rc;
}


IDATA
AttachedDataMinMaxTest::StoreAttachedDataFailureOnMinMaxTest(IDATA iteration, U_16 attachedDataType)
{
	IDATA rc = PASS;
	UDATA rV = 0;
	const char * testName = "StoreAttachedDataFailureOnMinMaxTest";
	J9SharedClassJavacoreDataDescriptor descriptor;
	SH_CacheMap *cacheMap;
	J9VMThread *currentThread;

	PORT_ACCESS_FROM_JAVAVM(vm);
	currentThread = vm->internalVMFunctions->currentVMThread(vm);
	cacheMap = (SH_CacheMap *)vm->sharedClassConfig->sharedClassCache;

	for (IDATA i = 0; i < NUM_DATA_OBJECTS; i++) {
		UnitTest::unitTest = UnitTest::NO_TEST;
		rV = (UDATA) vm->sharedClassConfig->storeAttachedData(currentThread, dataList[i].keyAddress, &dataList[i].data, false);
		UnitTest::unitTest = UnitTest::MINMAX_TEST;
		memset(&descriptor, 0, sizeof(J9SharedClassJavacoreDataDescriptor));
		if (0 == ((SH_CacheMap*)cacheMap)->getJavacoreData(vm, &descriptor)) {
			rc = FAIL;
		}
		switch(rV) {
		case 0:
			INFOPRINTF2("j9shr_storeAttachedData successfully stored data for data index %d with return code = %d", i, rV);
			break;
		case J9SHR_RESOURCE_STORE_FULL: {
			UDATA attachedDataBytes = (attachedDataType == J9SHR_ATTACHED_DATA_TYPE_JITPROFILE)?
					descriptor.jitProfileDataBytes: descriptor.jitHintDataBytes;
			const char *attachedDataTypeName = (attachedDataType == J9SHR_ATTACHED_DATA_TYPE_JITPROFILE)?
					"jitProfileDataBytes": "jitHintDataBytes";

			if ((FAIL != rc) && (attachedDataBytes <= (U_32) descriptor.maxJIT) && (JITminmaxvalue[iteration].failingIndex <= i)) {
				INFOPRINTF6("Test passed with %s=%d minJIT=%d maxJIT=%d failingIndex=%d for data index: %d",
						attachedDataTypeName, descriptor.jitProfileDataBytes, descriptor.minJIT, descriptor.maxJIT, JITminmaxvalue[iteration].failingIndex, i);
			} else {
				ERRPRINTF5("Failed with %s=%d maxJIT=%d failingIndex=%d for data index: %d",
						attachedDataTypeName, attachedDataBytes, descriptor.maxJIT, JITminmaxvalue[i].failingIndex, i);
				rc = FAIL;
			}
		}
		break;
		case J9SHR_RESOURCE_STORE_EXISTS:
		case J9SHR_RESOURCE_STORE_ERROR:
		case J9SHR_RESOURCE_PARAMETER_ERROR:
		default:
			ERRPRINTF2("j9shr_storeAttachedData failed with rc: 0x%x for data index: %d", rV, i);
			rc = FAIL;
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
testAttachedDataMinMax(J9JavaVM* vm, U_16 attachedDataType)
{
	IDATA rc = PASS;
	const char* testName = "testAttachedDataMinMax";

	PORT_ACCESS_FROM_JAVAVM(vm);
	REPORT_START("AttachedDataMinMaxTest");
	AttachedDataMinMaxTest adt(vm);
	/*
	 * Run tests with for max/min jit values with varied cache sizes
	 */
	const IDATA numtests = (sizeof(JITminmaxvalue)/sizeof(struct minmaxStruct));
	for (IDATA i=0; i < numtests; i++) {
		j9tty_printf(PORTLIB, "\n");
		INFOPRINTF3("Running test with new cache for cache size:%d minJIT:%d maxJIT:%d", SMALL_CACHE_SIZE, JITminmaxvalue[i].min, JITminmaxvalue[i].max);
		rc = adt.openTestCache(SMALL_CACHE_SIZE, JITminmaxvalue[i].min, JITminmaxvalue[i].max);
		if (FAIL == rc) {
			ERRPRINTF("openTestCache failed");
			adt.closeTestCache(true);
			break;
		}
		rc = adt.initializeAttachedData(SMALL_CACHE_SIZE);
		if (FAIL == rc) {
			ERRPRINTF1("initializeAttachedData with cacheSize=%d failed", SMALL_CACHE_SIZE);
			adt.freeAttachedData();
			adt.closeTestCache(true);
			break;
		}
		adt.setType(attachedDataType);
		rc = adt.StoreAttachedDataFailureOnMinMaxTest(i, attachedDataType);
		if (FAIL == rc) {
			ERRPRINTF("StoreAttachedDataFailureOnMinMaxTest failed");
			adt.freeAttachedData();
			adt.closeTestCache(true);
			break;
		}

		adt.freeAttachedData();
		adt.closeTestCache(true);
	}
	j9tty_printf(PORTLIB, "\n");
	REPORT_SUMMARY("AttachedDataMinMaxTest", rc);
	return rc;
}
} /* extern "C" */

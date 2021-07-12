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

#if !defined(CACHEMAP_H_INCLUDED)
#define CACHEMAP_H_INCLUDED

/* @ddr_namespace: default */
#include "CacheMapStats.hpp"
#include "SharedCache.hpp"
#include "CompositeCacheImpl.hpp"
#include "TimestampManager.hpp"
#include "ClasspathManager.hpp"
#include "ROMClassManager.hpp"
#include "ScopeManager.hpp"
#include "CompiledMethodManager.hpp"
#include "ByteDataManager.hpp"
#include "AttachedDataManager.hpp"

#define CM_READ_CACHE_FAILED -1
#define CM_CACHE_CORRUPT -2
#define CM_CACHE_STORE_PREREQ_ID_FAILED -3

/*
 * The maximum width of the hexadecimal representation of a value of type 'T'.
 */
#define J9HEX_WIDTH(T) (2 * sizeof(T))

/*
 * A unique cache id is a path followed by six hexadecimal values,
 * the first two of which express 64-bits values and the remaining
 * four express UDATA values. Additionally, there are six separator
 * characters and a terminating NUL character.
 */
#define J9SHR_UNIQUE_CACHE_ID_BUFSIZE (J9SH_MAXPATH + (2 * J9HEX_WIDTH(U_64)) + (4 * J9HEX_WIDTH(UDATA)) + 6 + 1)

typedef struct MethodSpecTable {
	char* className;
	char* methodName;
	char* methodSig;
	U_32 classNameMatchFlag;
	U_32 methodNameMatchFlag;
	U_32 methodSigMatchFlag;
	UDATA classNameLength;
	UDATA methodNameLength;
	UDATA methodSigLength;
	bool matchFlag;
} MethodSpecTable;

typedef struct CacheAddressRange {
	void* cacheBase;
	void* cacheEnd;
} CacheAddressRange;

/* 
 * Implementation of SH_SharedCache interface
 */
class SH_CacheMap : public SH_SharedCache, public SH_CacheMapStats
{
protected:
	void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

public:
	typedef char* BlockPtr;

	static SH_CacheMap* newInstance(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, SH_CacheMap* memForConstructor, const char* cacheName, I_32 newPersistentCacheReqd);

	static SH_CacheMapStats* newInstanceForStats(J9JavaVM* vm, SH_CacheMap* memForConstructor, const char* cacheName, I_8 topLayer);

	static UDATA getRequiredConstrBytes(bool startupForStats);

	IDATA startup(J9VMThread* currentThread, J9SharedClassPreinitConfig* piconfig, const char* rootName, const char* cacheDirName, UDATA cacheDirPerm, BlockPtr cacheMemoryUT, bool* cacheHasIntegrity);

	/* @see SharedCache.hpp */
	virtual IDATA enterLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller);

	/* @see SharedCache.hpp */
	virtual IDATA exitLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller);

	/* @see SharedCache.hpp */
	virtual IDATA isStale(const ShcItem* item);

	/* @see SharedCache.hpp */
	virtual const J9ROMClass* findROMClass(J9VMThread* currentThread, const char* path, ClasspathItem* cp, const J9UTF8* partition, const J9UTF8* modContext, IDATA confirmedEntries, IDATA* foundAtIndex);

	/* @see SharedCache.hpp */
	virtual const U_8* storeCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, const U_8* dataStart, UDATA dataSize, const U_8* codeStart, UDATA codeSize, UDATA forceReplace);

	/* @see SharedCache.hpp */
	virtual const U_8* findCompiledMethod(J9VMThread* currentThread, const J9ROMMethod* romMethod, UDATA* flags);

	/* @see SharedCache.hpp */
	virtual IDATA findSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, UDATA limitDataType, UDATA includePrivateData, J9SharedDataDescriptor* firstItem, const J9Pool* descriptorPool);

	/* @see SharedCache.hpp */
	virtual const U_8* storeSharedData(J9VMThread* currentThread, const char* key, UDATA keylen, const J9SharedDataDescriptor* data);

	/* @see SharedCache.hpp */
		virtual const U_8* findAttachedDataAPI(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset) ;

 	/* @see SharedCache.hpp */
	virtual UDATA storeAttachedData(J9VMThread* currentThread, const void* addressInCache, const J9SharedDataDescriptor* data, UDATA forceReplace) ;

	/* @see SharedCache.hpp */
	virtual UDATA updateAttachedData(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, const J9SharedDataDescriptor* data) ;

	/* @see SharedCache.hpp */
	virtual UDATA updateAttachedUDATA(J9VMThread* currentThread, const void* addressInCache, UDATA type, I_32 updateAtOffset, UDATA value) ;

	/* @see SharedCache.hpp */
	virtual UDATA acquirePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data);

	/* @see SharedCache.hpp */
	virtual UDATA releasePrivateSharedData(J9VMThread* currentThread, const J9SharedDataDescriptor* data);

	void dontNeedMetadata(J9VMThread* currentThread);

	/**
	 * This function is extremely hot.
	 * Peeks to see whether compiled code exists for a given ROMMethod in the CompiledMethodManager hashtable
	 *
	 * @param[in] currentThread  The current thread
	 * @param[in] romMethod  The ROMMethod to test
	 *
	 * @return 1 if the code exists in the hashtable, 0 otherwise
	 *
	 * THREADING: This function can be called multi-threaded
	 */
	inline UDATA existsCachedCodeForROMMethodInline(J9VMThread* currentThread, const J9ROMMethod* romMethod) {
		Trc_SHR_CM_existsCachedCodeForROMMethod_Entry(currentThread, romMethod);

		if (_cmm && _cmm->getState()==MANAGER_STATE_STARTED) {
			UDATA returnVal;
			returnVal = _cmm->existsResourceForROMAddress(currentThread, (UDATA)romMethod);
			Trc_SHR_CM_existsCachedCodeForROMMethod_Exit1(currentThread, returnVal);
			return returnVal;
		}

		Trc_SHR_CM_existsCachedCodeForROMMethod_Exit2(currentThread);
		return 0;
	};

	/* @see SharedCache.hpp */
	virtual UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor);

	/* @see SharedCache.hpp */
	virtual IDATA markStale(J9VMThread* currentThread, ClasspathEntryItem* cpei, bool hasWriteMutex);

	/* @see SharedCache.hpp */
	virtual void markItemStale(J9VMThread* currentThread, const ShcItem* item, bool isCacheLocked);

	/* @see SharedCache.hpp */
	virtual void markItemStaleCheckMutex(J9VMThread* currentThread, const ShcItem* item, bool isCacheLocked);

	/* @see SharedCache.hpp */
	virtual void destroy(J9VMThread* currentThread);

	/* @see SharedCache.hpp */
	virtual IDATA printCacheStats(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags);

	/* @see SharedCache.hpp */
	virtual void printShutdownStats(void);
	
	/* @see SharedCache.hpp */
	virtual void runExitCode(J9VMThread* currentThread);

	/* @see SharedCache.hpp */
	virtual void cleanup(J9VMThread* currentThread);

	/* @see SharedCache.hpp */
	virtual bool isBytecodeAgentInstalled(void);

	/* @see SharedCache.hpp */
	virtual IDATA enterStringTableMutex(J9VMThread* currentThread, BOOLEAN readOnly, UDATA* doRebuildLocalData, UDATA* doRebuildCacheData);

	/* @see SharedCache.hpp */
	virtual IDATA exitStringTableMutex(J9VMThread* currentThread, UDATA resetReason);

	/* @see SharedCache.hpp */
	virtual void notifyClasspathEntryStateChange(J9VMThread* currentThread, const char* path, UDATA newState);
	
	static IDATA createPathString(J9VMThread* currentThread, J9SharedClassConfig* config, char** pathBuf, UDATA pathBufSize, ClasspathEntryItem* cpei, const char* className, UDATA classNameLen, bool* doFreeBuffer);

	/* @see SharedCache.hpp */
	virtual IDATA getAndStartManagerForType(J9VMThread* currentThread, UDATA dataType, SH_Manager** startedManager);

	/* @see SharedCache.hpp */
	virtual SH_CompositeCache* getCompositeCacheAPI();

	/* @see SharedCache.hpp */
	virtual SH_Managers * managers();

	/* @see SharedCache.hpp */
	virtual IDATA aotMethodOperation(J9VMThread* currentThread, char* methodSpecs, UDATA action);

	/* @see CacheMapStats.hpp */
	IDATA startupForStats(J9VMThread* currentThread, const char* ctrlDirName, UDATA groupPerm, SH_OSCache * oscache, U_64 * runtimeflags, J9Pool **lowerLayerList);

	/* @see CacheMapStats.hpp */
	IDATA shutdownForStats(J9VMThread* currentThread);
	
	/* @see CacheMapStats.hpp */
	void* getAddressFromJ9ShrOffset(const J9ShrOffset* offset);
	
	/* @see CacheMapStats.hpp */
	U_8* getDataFromByteDataWrapper(const ByteDataWrapper* bdw);


	//New Functions To Support New ROM Class Builder
	IDATA startClassTransaction(J9VMThread* currentThread, bool lockCache, const char* caller);
	IDATA exitClassTransaction(J9VMThread* currentThread, const char* caller);
	SH_ROMClassManager* getROMClassManager(J9VMThread* currentThread);

	ClasspathWrapper* updateClasspathInfo(J9VMThread* currentThread, ClasspathItem* cp, I_16 cpeIndex, const J9UTF8* partition, const J9UTF8** cachedPartition, const J9UTF8* modContext, const J9UTF8** cachedModContext, bool haveWriteMutex);

	bool allocateROMClass(J9VMThread* currentThread, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, IDATA callerHelperID, bool modifiedNoContext, void * &newItemInCache, void * &cacheAreaForAllocate);

	IDATA commitROMClass(J9VMThread* currentThread, ShcItem* itemInCache, SH_CompositeCacheImpl* cacheAreaForAllocate, ClasspathWrapper* cpw, I_16 cpeIndex, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, BlockPtr romClassBuffer, bool commitOutOfLineData, bool checkSRPs = true);

	IDATA commitOrphanROMClass(J9VMThread* currentThread, ShcItem* itemInCache, SH_CompositeCacheImpl* cacheAreaForAllocate, ClasspathWrapper* cpw, BlockPtr romClassBuffer);

	IDATA commitMetaDataROMClassIfRequired(J9VMThread* currentThread, ClasspathWrapper* cpw, I_16 cpeIndex, IDATA helperID, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, J9ROMClass * romclass);

	const J9ROMClass* findNextROMClass(J9VMThread* currentThread, void * &findNextIterator, void * &firstFound, U_16 classnameLength, const char* classnameData);

	bool isAddressInROMClassSegment(const void* address);

	void getRomClassAreaBounds(void ** romClassAreaStart, void ** romClassAreaEnd);

	UDATA getReadWriteBytes(void);

	UDATA getStringTableBytes(void);
	
	void* getStringTableBase(void);

	void setStringTableInitialized(bool);
	
	bool isStringTableInitialized(void);
	
	BOOLEAN isAddressInCacheDebugArea(void *address, UDATA length);
	
	U_32 getDebugBytes(void);

	bool isCacheInitComplete(void);

	bool isCacheCorruptReported(void);

	IDATA runEntryPointChecks(J9VMThread* currentThread, void* isAddressInCache, const char** subcstr);

	void protectPartiallyFilledPages(J9VMThread *currentThread);

	I_32 tryAdjustMinMaxSizes(J9VMThread* currentThread, bool isJCLCall = false);

	void updateRuntimeFullFlags(J9VMThread* currentThread);

	void increaseTransactionUnstoredBytes(U_32 segmentAndDebugBytes, J9SharedClassTransaction* obj);

	void increaseUnstoredBytes(U_32 blockBytes, U_32 aotBytes = 0, U_32 jitBytes = 0);

	void getUnstoredBytes(U_32 *softmxUnstoredBytes, U_32 *maxAOTUnstoredBytes, U_32 *maxJITUnstoredBytes) const;
	
	bool isAddressInCache(const void *address, UDATA length, bool includeHeaderReadWriteArea, bool useCcHeadOnly) const;
	
	bool resizeCache(J9VMThread* currentThread, U_32 newSize);

private:
	SH_CompositeCacheImpl* _cc;					/* current cache */

	/* See other _writeHash fields below. Put U_64 at the top so the debug
	 * extensions can more easily mirror the shape.
	 */
	U_64 _writeHashStartTime;
	J9SharedClassConfig* _sharedClassConfig;
	
	SH_CompositeCacheImpl* _ccHead;				/* head of supercache list */
	SH_CompositeCacheImpl* _ccTail;
		
	CacheAddressRange _cacheAddressRangeArray[J9SH_LAYER_NUM_MAX_VALUE + 1];
	UDATA _numOfCacheLayers;
	
	SH_ClasspathManager* _cpm;
	SH_TimestampManager* _tsm;
	SH_ROMClassManager* _rcm;
	SH_ScopeManager* _scm;
	SH_CompiledMethodManager* _cmm;
	SH_ByteDataManager* _bdm;
	SH_AttachedDataManager* _adm;
	J9PortLibrary* _portlib;
	omrthread_monitor_t _refreshMutex;
	bool _cacheCorruptReported;
	U_64* _runtimeFlags;
	U_64* _readOnlyCacheRuntimeFlags;
	const char* _cacheName;
	const char* _cacheDir;
	UDATA _localCrashCntr;

	UDATA _writeHashAverageTimeMicros;
	UDATA _writeHashMaxWaitMicros;
	UDATA _writeHashSavedMaxWaitMicros;
	UDATA _writeHashContendedResetHash;
	/* Also see U_64 _writeHashStartTime above */

	UDATA _verboseFlags;
	UDATA _bytesRead;
	U_32 _actualSize;
	J9Pool* _ccPool;
	bool _metadataReleased;

	bool _isAssertEnabled; /* flag to turn on/off assertion before acquiring local mutex */
	
	SH_Managers * _managers;
	
	void initialize(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, BlockPtr memForConstructor, const char* cacheName, I_32 newPersistentCacheReqd, I_8 topLayer, bool startupForStats);

	IDATA readCacheUpdates(J9VMThread* currentThread);

	IDATA readCache(J9VMThread* currentThread, SH_CompositeCacheImpl* cache, IDATA expectedUpdates, bool startupForStats);

	IDATA refreshHashtables(J9VMThread* currentThread, bool hasClassSegmentMutex);

	ClasspathWrapper* addClasspathToCache(J9VMThread* currentThread, ClasspathItem* obj);

	const J9UTF8* addScopeToCache(J9VMThread* currentThread, const J9UTF8* scope, U_16 type = TYPE_SCOPE); 

	const void* addROMClassResourceToCache(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, const char** p_subcstr);

	BlockPtr addByteDataToCache(J9VMThread* currentThread, SH_Manager* localBDM, const J9UTF8* tokenKeyInCache, const J9SharedDataDescriptor* data, SH_CompositeCacheImpl* forceCache, bool writeWithoutMetadata);

	J9MemorySegment* addNewROMImageSegment(J9VMThread* currentThread, U_8* segmentBase, U_8* segmentEnd);
	
	J9MemorySegment* createNewSegment(J9VMThread* currentThread, UDATA type, J9MemorySegmentList* segmentList, U_8* baseAddress, U_8* heapBase, U_8* heapTop, U_8* heapAlloc);

	const void* storeROMClassResource(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, UDATA forceReplace, const char** p_subcstr);

	const void* findROMClassResource(J9VMThread* currentThread, const void* romAddress, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, bool useReadMutex, const char** p_subcstr, UDATA* flags);

	UDATA updateROMClassResource(J9VMThread* currentThread, const void* addressInCache, I_32 updateAtOffset, SH_ROMClassResourceManager* localRRM, SH_ROMClassResourceManager::SH_ResourceDescriptor* resourceDescriptor, const J9SharedDataDescriptor* data, bool isUDATA, const char** p_subcstr);

	const U_8* findAttachedData(J9VMThread* currentThread, const void* addressInCache, J9SharedDataDescriptor* data, IDATA *corruptOffset, const char** p_subcstr) ;

	void updateROMSegmentList(J9VMThread* currentThread, bool hasClassSegmentMutex, bool topLayerOnly = true);

	void updateROMSegmentListForCache(J9VMThread* currentThread, SH_CompositeCacheImpl* forCache);

	const char* attachedTypeString(UDATA type);

	UDATA initializeROMSegmentList(J9VMThread* currentThread);

	IDATA checkForCrash(J9VMThread* currentThread, bool hasClassSegmentMutex);
	
	void reportCorruptCache(J9VMThread* currentThread, SH_CompositeCacheImpl* _ccToUse);

	void resetCorruptState(J9VMThread* currentThread, UDATA hasRefreshMutex);

	IDATA enterRefreshMutex(J9VMThread* currentThread, const char* caller);
	IDATA exitRefreshMutex(J9VMThread* currentThread, const char* caller);
	IDATA enterReentrantLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller);
	IDATA exitReentrantLocalMutex(J9VMThread* currentThread, omrthread_monitor_t monitor, const char* name, const char* caller);

	UDATA sanityWalkROMClassSegment(J9VMThread* currentThread, SH_CompositeCacheImpl* cache);

	void updateBytesRead(UDATA numBytes); 

	const J9UTF8* getCachedUTFString(J9VMThread* currentThread, const char* local, U_16 localLen);

	IDATA printAllCacheStats(J9VMThread* currentThread, UDATA showFlags, SH_CompositeCacheImpl* cache, U_32* staleBytes);
	
	IDATA resetAllManagers(J9VMThread* currentThread);
	
	void updateAllManagersWithNewCacheArea(J9VMThread* currentThread, SH_CompositeCacheImpl* newArea);

	void updateAccessedShrCacheMetadataBounds(J9VMThread* currentThread, uintptr_t const  * result);
	
	bool isAddressInReleasedMetaDataBounds(J9VMThread* currentThread, UDATA address) const;

	SH_CompositeCacheImpl* getCacheAreaForDataType(J9VMThread* currentThread, UDATA dataType, UDATA dataLength);

	IDATA startManager(J9VMThread* currentThread, SH_Manager* manager);

	SH_ScopeManager* getScopeManager(J9VMThread* currentThread);

	SH_ClasspathManager* getClasspathManager(J9VMThread* currentThread);

	SH_ByteDataManager* getByteDataManager(J9VMThread* currentThread);

	SH_CompiledMethodManager* getCompiledMethodManager(J9VMThread* currentThread);
	
	SH_AttachedDataManager* getAttachedDataManager(J9VMThread* currentThread);

	void updateAverageWriteHashTime(UDATA actualTimeMicros);

	J9SharedClassCacheDescriptor* appendCacheDescriptorList(J9VMThread* currentThread, J9SharedClassConfig* sharedClassConfig, SH_CompositeCacheImpl* ccToUse);
	
	void resetCacheDescriptorList(J9VMThread* currentThread, J9SharedClassConfig* sharedClassConfig);

	const J9ROMClass* allocateROMClassOnly(J9VMThread* currentThread, U_32 sizeToAlloc, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, IDATA callerHelperID, bool modifiedNoContext, void * &newItemInCache, void * &cacheAreaForAllocate);

	const J9ROMClass* allocateFromCache(J9VMThread* currentThread, U_32 sizeToAlloc, U_32 wrapperSize, U_16 wrapperType, void * &newItemInCache, void * &cacheAreaForAllocate);

	IDATA allocateClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces);

	void rollbackClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData);

	void commitClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData);

	void tokenStoreStaleCheckAndMark(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, ClasspathWrapper* cpw, const J9UTF8* partitionInCache, const J9UTF8* modContextInCache, UDATA callerHelperID);

	J9SharedClassConfig* getSharedClassConfig();
	
	void updateLineNumberContentInfo(J9VMThread* currentThread);

	const IDATA aotMethodOperationHelper(J9VMThread* currentThread, MethodSpecTable* specTable, IDATA numSpecs, UDATA action);

	const bool matchAotMethod(MethodSpecTable* specTable, IDATA numSpecs, J9UTF8* romClassName, J9UTF8* romMethodName, J9UTF8* romMethodSig);

	const IDATA fillMethodSpecTable(MethodSpecTable* specTable, char* inputOption);

	const bool parseWildcardMethodSpecTable(MethodSpecTable* specTable, IDATA numSpecs);

	static void updateLocalHintsData(J9VMThread* currentThread, J9SharedLocalStartupHints* localHints, const J9SharedStartupHintsDataDescriptor* hintsDataInCache, bool overwrite);

	IDATA getPrereqCache(J9VMThread* currentThread, const char* cacheDir, SH_CompositeCacheImpl* ccToUse, bool startupForStats, const char** prereqCacheID, UDATA* idLen, bool *isCacheUniqueIdStored);

	IDATA storeCacheUniqueID(J9VMThread* currentThread, const char* cacheDir, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes, const char** prereqCacheID, UDATA* idLen);

	void handleStartupError(J9VMThread* currentThread, SH_CompositeCacheImpl* ccToUse, IDATA errorCode, U_64 runtimeFlags, UDATA verboseFlags, bool *doRetry, IDATA *deleteRC);
	
	void setCacheAddressRangeArray(void);
	
	void getJ9ShrOffsetFromAddress(const void* address, bool offsetFromBaseAddress, J9ShrOffset* offset);
	
	UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor, bool topLayerOnly);
	
	void printCacheStatsTopLayerStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData, bool multiLayerStats);
	
	void printCacheStatsTopLayerSummaryStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData);
	
	void printCacheStatsAllLayersStatsHelper(J9VMThread* currentThread, UDATA showFlags, U_64 runtimeFlags, J9SharedClassJavacoreDataDescriptor *javacoreData, U_32 staleBytes);

	IDATA startupLowerLayerForStats(J9VMThread* currentThread, const char* ctrlDirName, UDATA groupPerm, SH_OSCache *oscache, J9Pool** lowerLayerList);
};

#endif /* !defined(CACHEMAP_H_INCLUDED) */


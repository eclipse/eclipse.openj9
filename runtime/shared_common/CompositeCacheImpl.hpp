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

#if !defined(COMPOSITECACHEIMPL_H_INCLUDED)
#define COMPOSITECACHEIMPL_H_INCLUDED

/* @ddr_namespace: default */
#include "OSCache.hpp"
#include "OSCachesysv.hpp"
#include "CompositeCache.hpp"
#include "sharedconsts.h"
#include "ClassDebugDataProvider.hpp"
#include "SCTransactionCTypes.h"
#include "AbstractMemoryPermission.hpp"

#define MIN_CC_SIZE 0x1000
#define MAX_CC_SIZE 0x7FFFFFF8

#if defined(WIN32)
#define J9SHR_MIN_GAP_BEFORE_METADATA 1024
/* On Windows, 1K is reserved to try and avoid a cache corruption bug.
 * See SH_CompositeCacheImpl::allocate().
 * Use a higher threshold on Windows. 
 */
#define CC_MIN_SPACE_BEFORE_CACHE_FULL 3072
#else
#define J9SHR_MIN_GAP_BEFORE_METADATA 0
#define CC_MIN_SPACE_BEFORE_CACHE_FULL 2048
#endif

#define J9SHR_DUMMY_DATA_BYTE 0xD9
#define J9SHR_MIN_DUMMY_DATA_SIZE (sizeof(ShcItem) + sizeof(ShcItemHdr) + SHC_WORDALIGN)

#define CC_STARTUP_OK 0
#define CC_STARTUP_FAILED -1
#define CC_STARTUP_CORRUPT -2
#define CC_STARTUP_RESET -3
#define CC_STARTUP_SOFT_RESET -4
#define CC_STARTUP_NO_CACHE -5

/* How many bytes to sample from across the cache for calculating the CRC */
/* Need a value that has negligible impact on performance */
#define J9SHR_CRC_MAX_SAMPLES 100000

/**
 * Represents view of the shared cache at the level of blocks of memory.
 * 
 * Allocates memory backwards from the end of the cache, apart from segment
 * memory which it allocates from the start of the cache (see allocate below).
 * Provides a mutex for accessing the shared cache and functions 
 * for walking and reading the cache.
 */
class SH_CompositeCacheImpl : public SH_CompositeCache, public AbstractMemoryPermission
{
protected:
	void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

public:
	typedef char* BlockPtr;

	static UDATA getRequiredConstrBytes(bool isNested, bool startupForStats);
	
	static UDATA getRequiredConstrBytesWithCommonInfo(bool isNested, bool startupForStats);

	static SH_CompositeCacheImpl* newInstance(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, SH_CompositeCacheImpl* memForConstructor, const char* cacheName, I_32 newPersistentCacheReqd, bool startupForStats, I_8 layer);
	
	IDATA startup(J9VMThread* currentThread, J9SharedClassPreinitConfig* piconfig, BlockPtr cacheMemoryUT, U_64* runtimeFlags, UDATA verboseFlags,
			const char* rootName, const char* ctrlDirName, UDATA cacheDirPerm, U_32* actualSize, UDATA* localCrashCntr, bool isFirstStart, bool* cacheHasIntegrity);

	void cleanup(J9VMThread* currentThread);

	IDATA deleteCache(J9VMThread *currentThread, bool suppressVerbose);

	IDATA enterReadWriteAreaMutex(J9VMThread* currentThread, BOOLEAN readOnly, UDATA* doRebuildLocalData, UDATA* doRebuildCacheData);

	IDATA exitReadWriteAreaMutex(J9VMThread* currentThread, UDATA resetReason);

	IDATA enterWriteMutex(J9VMThread* currentThread, bool lockCache, const char* caller);

	IDATA exitWriteMutex(J9VMThread* currentThread, const char* caller, bool doDecWriteCounter=true);

	void doLockCache(J9VMThread* currentThread);

	void doUnlockCache(J9VMThread* currentThread);

	bool isLocked(void);
	
	IDATA enterReadMutex(J9VMThread* currentThread, const char* caller);

	void exitReadMutex(J9VMThread* currentThread, const char* caller);

	BlockPtr allocateBlock(J9VMThread* currentThread, ShcItem* itemToWrite, U_32 align, U_32 alignOffset);

	BlockPtr allocateWithSegment(J9VMThread* currentThread, ShcItem* itemToWrite, U_32 segBufSize, BlockPtr* segBuf);

	BlockPtr allocateAOT(J9VMThread* currentThread, ShcItem* itemToWrite, U_32 dataBytes);

	BlockPtr allocateJIT(J9VMThread* currentThread, ShcItem* itemToWrite, U_32 dataBytes);

	BlockPtr allocateWithReadWriteBlock(J9VMThread* currentThread, ShcItem* itemToWrite, U_32 readWriteBufferSize, BlockPtr* readWriteBuffer);

	UDATA checkUpdates(J9VMThread* currentThread);

	void doneReadUpdates(J9VMThread* currentThread, IDATA updates);

	void updateStoredSegmentUsedBytes(U_32 usedBytes);

	void commitUpdate(J9VMThread* currentThread, bool isCachelet);

	void initBlockData(ShcItem** itemBuf, U_32 dataLen, U_16 dataType);

	void rollbackUpdate(J9VMThread* currentThread);

	void startCriticalUpdate(J9VMThread* currentThread);

	void endCriticalUpdate(J9VMThread* currentThread);

	bool isCacheCorrupt(void);

	void setCorruptCache(J9VMThread *currentThread, IDATA corruptionCode, UDATA corruptValue);

	bool crashDetected(UDATA* localCrashCntr);

	void reset(J9VMThread* currentThread);

	BlockPtr nextEntry(J9VMThread* currentThread, UDATA* staleItems);
	
	void markStale(J9VMThread* currentThread, BlockPtr block, bool isCacheLocked);

	UDATA stale(BlockPtr block);
	
	void findStart(J9VMThread* currentThread);
	
	void* getBaseAddress(void);

	J9SharedCacheHeader* getCacheHeaderAddress(void);
	
	void* getStringTableBase(void);

	UDATA getCacheMemorySize(void);

	void* getCacheEndAddress(void);
	
	void* getCacheLastEffectiveAddress(void);

	void* getSegmentAllocPtr(void);

	void* getMetaAllocPtr(void);

	UDATA getTotalUsableCacheSize(void);

	void getMinMaxBytes(U_32 *softmx, I_32 *minAOT, I_32 *maxAOT, I_32 *minJIT, I_32 *maxJIT);

	UDATA getFreeBytes(void);

	UDATA getMetadataBytes(void) const;

	UDATA getClassesBytes(void) const;
	
	UDATA getFreeAvailableBytes(void);

	U_32 getUsedBytes(void);

	BOOLEAN isAddressInCacheDebugArea(void *address, UDATA length);

	U_32 getDebugBytes(void);

	U_32 getFreeDebugSpaceBytes(void);

	U_32 getLineNumberTableBytes(void) const;

	U_32 getLocalVariableTableBytes(void) const;

	UDATA getFreeReadWriteBytes(void);
	
	UDATA getTotalStoredBytes(void);

	void getUnstoredBytes(U_32* softmxUnstoredBytes, U_32* maxAOTUnstoredBytes, U_32* maxJITUnstoredBytes) const;

	UDATA getAOTBytes(void);

	UDATA getJITBytes(void);

	UDATA getReadWriteBytes(void);
	
	UDATA getStringTableBytes(void);
	
	UDATA testAndSetWriteHash(J9VMThread *currentThread, UDATA hash);

	UDATA tryResetWriteHash(J9VMThread *currentThread, UDATA hash);

	void setWriteHash(J9VMThread *currentThread, UDATA hash);

	bool peekForWriteHash(J9VMThread *currentThread);

	bool isAddressInROMClassSegment(const void* address);

	bool isAddressInMetaDataArea(const void* address) const;

	bool isAddressInCache(const void* address, bool includeHeaderReadWriteArea = true);

	void runExitCode(J9VMThread *currentThread);
	
	U_16 getJVMID(void);

	void setInternCacheHeaderFields(J9SRP** sharedTail, J9SRP** sharedHead, U_32** totalSharedNodes, U_32** totalSharedWeight);
	
	UDATA getReaderCount(J9VMThread* currentThread);

	bool hasWriteMutex(J9VMThread* currentThread);

	bool hasReadWriteMutex(J9VMThread* currentThread);

	void notifyRefreshMutexEntered(J9VMThread* currentThread);

	void notifyRefreshMutexExited(J9VMThread* currentThread);
	
	bool isRunningReadOnly(void);

	bool isVerbosePages(void);

	bool isMemProtectEnabled(void);

	bool isMemProtectPartialPagesEnabled(void);

	U_32 getTotalSize(void);
	
	UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor);

	void updateMetadataSegment(J9VMThread* currentThread);
	
	BOOLEAN isReadWriteAreaReadOnly();

	SH_CompositeCacheImpl* getNext(void);
	
	void setNext(SH_CompositeCacheImpl* next);

	void setPrevious(SH_CompositeCacheImpl* previous);
	
	SH_CompositeCacheImpl* getPrevious(void);

	U_32 getBytesRequiredForItemWithAlign(ShcItem* itemToWrite, U_32 align, U_32 alignOffset);

	U_32 getBytesRequiredForItem(ShcItem* itemToWrite);

	J9MemorySegment* getCurrentROMSegment(void);
	
	void setCurrentROMSegment(J9MemorySegment* segment);
	
	BlockPtr getFirstROMClassAddress();

	bool isStarted(void);
	
	bool getIsNoLineNumberEnabled(void);

	void setIsNoLineNumberEnabled(bool value);
	
	bool getIsNoLineNumberContentEnabled(void);

	void setNoLineNumberContentEnabled(J9VMThread *currentThread);

	bool getIsLineNumberContentEnabled(void);

	void setLineNumberContentEnabled(J9VMThread* currentThread);

	UDATA getOSPageSize(void);

	bool getContainsCachelets(void);

	void setStringTableInitialized(bool);
	
	bool isStringTableInitialized(void);

	IDATA allocateClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces);

	void rollbackClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData);

	void commitClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData);

	void * getClassDebugDataStartAddress(void);

	IDATA startupForStats(J9VMThread* currentThread, SH_OSCache * oscache, U_64 * runtimeFlags, UDATA verboseFlags);
	
	IDATA startupNonTopLayerForStats(J9VMThread* currentThread, const char* cacheDirName, const char* cacheName, U_32 cacheType, I_8 layer, U_64 * runtimeFlags, UDATA verboseFlags);
	
	IDATA getNonTopLayerCacheInfo(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, SH_OSCache_Info *cacheInfo);
	
	IDATA shutdownForStats(J9VMThread* currentThread);

	static IDATA getNumRequiredOSLocks();

	void getCorruptionContext(IDATA *corruptionCode, UDATA *corruptValue);

	void setCorruptionContext(IDATA corruptionCode, UDATA corruptValue);

	I_32 getFreeBlockBytes(void);

	bool getIsBCIEnabled(void);

	void setRuntimeCacheFullFlags(J9VMThread *currentThread);

	void protectLastUnusedPages(J9VMThread *currentThread);

	bool isAllRuntimeCacheFullFlagsSet(void) const;

	void markReadOnlyCacheFull(void);

	I_32 getAvailableReservedAOTBytes(J9VMThread *currentThread);

	I_32 getAvailableReservedJITBytes(J9VMThread *currentThread);

	bool isCacheMarkedFull(J9VMThread *currentThread);

	void setCacheHeaderFullFlags(J9VMThread *currentThread, UDATA flags, bool setRuntimeFlags);

	void clearCacheHeaderFullFlags(J9VMThread *currentThread);

	void fillCacheIfNearlyFull(J9VMThread* currentThread);

	bool isUsingWriteHash(void) const {
		return _useWriteHash;
	}

	void setCacheHeaderExtraFlags(J9VMThread *currentThread, UDATA extraFlags);

	bool checkCacheCompatibility(J9VMThread *currentThread);

	void setAOTHeaderPresent(J9VMThread *currentThread);

	bool isAOTHeaderPresent(J9VMThread *currentThread);

	bool isMprotectPartialPagesSet(J9VMThread *currentThread);

	bool isMprotectPartialPagesOnStartupSet(J9VMThread *currentThread);

	SH_CacheAccess isCacheAccessible(void) const;
	
	bool isRestrictClasspathsSet(J9VMThread *currentThread);

	bool canStoreClasspaths(void) const;

	IDATA restoreFromSnapshot(J9JavaVM* vm, const char* cacheName, bool* cacheExist);
	void dontNeedMetadata(J9VMThread *currentThread);

	void changePartialPageProtection(J9VMThread *currentThread, void *addr, bool readOnly, bool phaseCheck = true);

	void protectPartiallyFilledPages(J9VMThread *currentThread, bool protectSegmentPage = true, bool protectMetadataPage = true, bool protectDebugDataPages = true, bool phaseCheck = true);

	void unprotectPartiallyFilledPages(J9VMThread *currentThread, bool unprotectSegmentPage = true, bool unprotectMetadataPage = true, bool unprotectDebugDataPages = true, bool phaseCheck = true);

	I_32 tryAdjustMinMaxSizes(J9VMThread *currentThread, bool isJCLCall = false);

	void updateRuntimeFullFlags(J9VMThread* currentThread);
	
	void increaseUnstoredBytes(U_32 blockBytes, U_32 aotBytes, U_32 jitBytes);

	bool isNewCache(void);

	bool updateAccessedShrCacheMetadataBounds(J9VMThread* currentThread, uintptr_t const * metadataAddress);

	bool isAddressInReleasedMetaDataBounds(J9VMThread* currentThread, UDATA metadataAddress) const;

	const char* getCacheUniqueID(J9VMThread* currentThread) const;

	const char* getCacheName(void) const;
	
	I_8 getLayer(void) const;

	U_64 getCreateTime(void) const;

	bool verifyCacheUniqueID(J9VMThread* currentThread, const char* expectedCacheUniqueID) const;
	
	void setMetadataMemorySegment(J9MemorySegment** segment);
	
	const char* getCacheNameWithVGen(void) const;

	bool hasReadMutex(J9VMThread* currentThread) const;

	const char* getCacheFullPathName(void) const;
	
	bool fixAndWriteOSCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 size);
	
	bool fixAndWriteJ9SharedCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 freeDebugSpaceToChange, I_32 freeSpaceToChange);
	
	bool writeReadWriteArea(J9VMThread* currentThread, IDATA fd);
	
	bool fixAndWriteDebugAreaAndClassSegment(J9VMThread* currentThread, IDATA fd, I_32 freeDebugSpaceToChange);
	
	bool writeMetaDataArea(J9VMThread* currentThread, IDATA fd);

private:
	J9SharedClassConfig* _sharedClassConfig;
	SH_OSCache* _oscache;
	omrthread_monitor_t _utMutex, _headerProtectMutex, _runtimeFlagsProtectMutex;
	J9PortLibrary* _portlib;

	J9SharedCacheHeader* _theca;
	bool _started;
	const char* _cacheName;
	
	SH_CompositeCacheImpl* _next;
	SH_CompositeCacheImpl* _previous;
	SH_CompositeCacheImpl* _parent;
	SH_CompositeCacheImpl* _ccHead; /* first supercache, if chained */
	
	ShcItemHdr* _scan;
	ShcItemHdr* _prevScan;
	ShcItemHdr* _storedScan;
	ShcItemHdr* _storedPrevScan;
	BlockPtr _romClassProtectEnd;

	UDATA _oldUpdateCount;

	U_32 _storedSegmentUsedBytes;
	U_32 _storedMetaUsedBytes;
	U_32 _storedAOTUsedBytes;
	U_32 _storedJITUsedBytes;
	U_32 _storedReadWriteUsedBytes;
	U_32 _softmxUnstoredBytes;
	U_32 _maxAOTUnstoredBytes;
	U_32 _maxJITUnstoredBytes;
	I_32 _maxAOT;
	I_32 _maxJIT;

	U_64* _runtimeFlags;
	UDATA _verboseFlags;
	UDATA _cacheFullFlags;

	U_32 _totalStoredBytes;

	UDATA _lastFailedWriteHash;
	U_32 _lastFailedWHCount;

	BlockPtr _readWriteAreaStart;
	U_32 _readWriteAreaBytes;
	BlockPtr _readWriteAreaPageStart;
	U_32 _readWriteAreaPageBytes;
	
	BlockPtr _cacheHeaderPageStart;
	U_32 _cacheHeaderPageBytes;

	UDATA _osPageSize;
	
	UDATA _nestedSize;
	BlockPtr _nestedMemory;
	
	UDATA _localReadWriteCrashCntr;

	J9MemorySegment** _metadataSegmentPtr;
	J9MemorySegment* _currentROMSegment;
	
	bool _doReadWriteSync;
	bool _doHeaderReadWriteProtect;
	bool _readWriteAreaIsReadOnly;
	bool _doHeaderProtect;
	bool _doSegmentProtect;
	bool _doMetaProtect;
	bool _doPartialPagesProtect;
	bool _readOnlyOSCache;
#if defined (J9SHR_MSYNC_SUPPORT)
	bool _doMetaSync;
	bool _doHeaderSync;
	bool _doSegmentSync;
#endif
	
	IDATA _headerProtectCntr;
	IDATA _readWriteProtectCntr;
	IDATA _readOnlyReaderCount;
	bool _incrementedRWCrashCntr;
	
	bool _useWriteHash;
	
	bool _canStoreClasspaths;

	bool _reduceStoreContentionDisabled;

	bool _initializingNewCache;

	UDATA  _minimumAccessedShrCacheMetadata;

	UDATA _maximumAccessedShrCacheMetadata;
	
	I_8 _layer;

	/* All instances of this class share a common debug & raw class data region
	 */
	ClassDebugDataProvider * _debugData;
	
	/* All composite caches within a JVM point to the same 'J9ShrCompositeCacheCommonInfo' 
	 * structure.
	 */
	J9ShrCompositeCacheCommonInfo * _commonCCInfo;
	
	BlockPtr next(J9VMThread* currentThread);

	void incReaderCount(J9VMThread* currentThread);
	void decReaderCount(J9VMThread* currentThread);

	void initialize(J9JavaVM* vm, BlockPtr memForConstructor, J9SharedClassConfig* sharedClassConfig, const char* cacheName, I_32 cacheTypeRequired, bool startupForStats, I_8 layer);
	void initializeWithCommonInfo(J9JavaVM* vm, J9SharedClassConfig* sharedClassConfig, BlockPtr memForConstructor, const char* cacheName, I_32 newPersistentCacheReqd, bool startupForStats, I_8 layer);
	void initCommonCCInfoHelper();

	void commonInit(J9JavaVM* vm);
	
	BlockPtr allocate(J9VMThread* currentThread, U_8 type, ShcItem* itemToWrite, U_32 len2, U_32 segBufSize, 
			BlockPtr* segBuf, BlockPtr* readWriteBuffer, U_32 align, U_32 alignOffset);

	BlockPtr allocateMetadataEntry(J9VMThread* currentThread, BlockPtr allocPtr, ShcItem *itemToWrite, U_32 itemLen);

	void setCacheAreaBoundaries(J9VMThread* currentThread, J9SharedClassPreinitConfig* piConfig);

	void notifyPagesRead(BlockPtr start, BlockPtr end, UDATA expectedDirection, bool protect);
	void notifyPagesCommitted(BlockPtr start, BlockPtr end, UDATA expectedDirection);

	void unprotectHeaderReadWriteArea(J9VMThread* currentThread, bool changeReadWrite);
	void protectHeaderReadWriteArea(J9VMThread* currentThread, bool changeReadWrite);

	void unprotectReadWriteArea(J9VMThread* currentThread);
	void protectReadWriteArea(J9VMThread* currentThread);
	
	void unprotectHeader(J9VMThread* currentThread);
	void protectHeader(J9VMThread* currentThread);
	
	void unprotectMetadataArea();
	void protectMetadataArea(J9VMThread *currentThread);

	U_32 getCacheCRC(void);
	U_32 getCacheAreaCRC(U_8* areaStart, U_32 areaSize);
	void updateCacheCRC(void);
	bool checkCacheCRC(bool* cacheHasIntegrity, UDATA *crcValue);

	IDATA setRegionPermissions(J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags);

	void commitUpdateHelper(J9VMThread* currentThread, bool isCachelet);

	void setIsLocked(bool value);

	bool isCacheInitComplete(void);

	void setCorruptCache(J9VMThread *currentThread);

	I_32 getFreeAOTBytes(J9VMThread *currentThread);
	I_32 getFreeJITBytes(J9VMThread *currentThread);
	
	void setSoftMaxBytes(J9VMThread *currentThread, U_32 softMaxBytes, bool isJCLCall = false);

	void unsetCacheHeaderFullFlags(J9VMThread *currentThread, UDATA flagsToUnset);

	BlockPtr getRomClassProtectEnd() {
		return _romClassProtectEnd;
	}
	void setRomClassProtectEnd(BlockPtr pointer) {
		_romClassProtectEnd = pointer;
	}

	class SH_SharedCacheHeaderInit : public SH_OSCache::SH_OSCacheInitializer
	{
	protected:
		void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

	public:
		static SH_SharedCacheHeaderInit* newInstance(SH_SharedCacheHeaderInit* memForConstructor);
		virtual void init(BlockPtr data, U_32 len, I_32 minAOT, I_32 maxAOT, I_32 minJIT, I_32 maxJIT, U_32 readWriteLen, U_32 softMaxBytes);
	};

	SH_SharedCacheHeaderInit* _newHdrPtr;

	friend IDATA testProtectNewROMClassData_test1(J9JavaVM* vm);
	friend IDATA testProtectSharedCacheData_test1(J9JavaVM* vm);
	friend IDATA testProtectSharedCacheData_test2(J9JavaVM* vm);
	friend IDATA testProtectSharedCacheData_test3(J9JavaVM* vm);
	friend class OpenCacheHelper;
};

#endif /* !defined(COMPOSITECACHEIMPL_H_INCLUDED) */


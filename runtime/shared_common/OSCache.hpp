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
#if !defined(OSCACHE_HPP_INCLUDED)
#define OSCACHE_HPP_INCLUDED

/* @ddr_namespace: default */
#include "j9.h"
#include "pool_api.h"
#include "sharedconsts.h"
#include "shchelp.h"
#include "ut_j9shr.h"

#define J9SH_OSCACHE_CREATE 			0x1
#define J9SH_OSCACHE_OPEXIST_DESTROY	0x2
#define J9SH_OSCACHE_OPEXIST_STATS		0x4
#define J9SH_OSCACHE_OPEXIST_DO_NOT_CREATE	0x8
#define J9SH_OSCACHE_UNKNOWN -1

/* 
 * The different results from attempting to open/create a cache are
 * defined below. Failure cases MUST all be less than zero.
 */
#define J9SH_OSCACHE_OPENED 2
#define J9SH_OSCACHE_CREATED 1
#define J9SH_OSCACHE_FAILURE -1
#define J9SH_OSCACHE_CORRUPT -2
#define J9SH_OSCACHE_DIFF_BUILDID -3
#define J9SH_OSCACHE_OUTOFMEMORY -4
#define J9SH_OSCACHE_INUSE -5
#define J9SH_OSCACHE_NO_CACHE -6

#define J9SH_OSCACHE_HEADER_OK 0
#define J9SH_OSCACHE_HEADER_WRONG_VERSION -1
#define J9SH_OSCACHE_HEADER_CORRUPT -2
#define J9SH_OSCACHE_HEADER_MISSING -3
#define J9SH_OSCACHE_HEADER_DIFF_BUILDID -4
#define J9SH_OSCACHE_SEMAPHORE_MISMATCH -5

#define J9SH_OSCACHE_READONLY_RETRY_COUNT 10
#define J9SH_OSCACHE_READONLY_RETRY_SLEEP_MILLIS 10

#define OSCACHE_LOWEST_ACTIVE_GEN 1

/* Always increment this value by 2. For testing we use the (current generation - 1) and expect the cache contents to be compatible. */
#define OSCACHE_CURRENT_CACHE_GEN 43
#define OSCACHE_CURRENT_LAYER_LAYER 0

#define J9SH_VERSION(versionMajor, versionMinor) (versionMajor*100 + versionMinor)

#define J9SH_GET_VERSION_STRING(portLib, versionStr, version, modlevel, feature, addrmode)\
											 j9str_printf(portLib, versionStr, J9SH_VERSION_STRING_LEN, J9SH_VERSION_STRING_SPEC,\
											 version, modlevel, feature, addrmode)

#define J9SH_GET_VERSION_STRING_JAVA9ANDLOWER(portLib, versionStr, version, modlevel, feature, addrmode)\
											 j9str_printf(portLib, versionStr, J9SH_VERSION_STRING_LEN - J9SH_VERSTRLEN_INCREASED_SINCEJAVA10, J9SH_VERSION_STRING_SPEC,\
											 version, modlevel, feature, addrmode)

#define J9SH_GET_VERSION_G07TO29_STRING(portLib, versionStr, version, modlevel, addrmode)\
											 j9str_printf(portLib, versionStr, J9SH_VERSION_STRING_LEN - J9SH_VERSTRLEN_INCREASED_SINCEG29 - J9SH_VERSTRLEN_INCREASED_SINCEJAVA10, J9SH_VERSION_STRING_G07TO29_SPEC,\
											 version, modlevel, addrmode)

#define J9SH_GET_VERSION_G07ANDLOWER_STRING(portLib, versionStr, version, modlevel, addrmode)\
											 j9str_printf(portLib, versionStr, J9SH_VERSION_STRING_LEN - J9SH_VERSTRLEN_INCREASED_SINCEG29 - J9SH_VERSTRLEN_INCREASED_SINCEJAVA10, J9SH_VERSION_STRING_G07ANDLOWER_SPEC,\
											 version, modlevel, addrmode)

#define OSC_TRACE(var) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_INFO, var)
#define OSC_TRACE1(var, p1) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_INFO, var, p1)
#define OSC_TRACE2(var, p1, p2) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_INFO, var, p1, p2)
#define OSC_ERR_TRACE(var) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_ERROR, var)
#define OSC_ERR_TRACE1(var, p1) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1)
#define OSC_ERR_TRACE2(var, p1, p2) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1, p2)
#define OSC_ERR_TRACE4(var, p1, p2, p3, p4) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_ERROR, var, p1, p2, p3, p4)
#define OSC_WARNING_TRACE(var) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_WARNING, var)
#define OSC_WARNING_TRACE1(var, p1) if (_verboseFlags) j9nls_printf(PORTLIB, J9NLS_WARNING, var, p1)

/**
 * @struct SH_OSCache_Info
 * Information about a OSCache
 * If the information is not available, the value will be equals to @arg J9SH_OSCACHE_UNKNOWN
 */
typedef struct SH_OSCache_Info {
        char name[CACHE_ROOT_MAXLEN]; /** The name of the cache */
        UDATA os_shmid; /** Operating System specific shared memory id */
        UDATA os_semid; /** Operating System specific semaphore id */
        I_64 lastattach; /** time from which last attach has happened */
        I_64 lastdetach; /** time from which last detach has happened */
        I_64 createtime; /** time from which cache has been created */
        IDATA nattach; /** number of process attached to this region */
        J9PortShcVersion versionData; /** Cache version data */
        UDATA generation; /** cache generation number */
        UDATA isCompatible; /** Is the cache compatible with this VM */
        UDATA isCorrupt; /** Is set when the cache is found to be corrupt */
        UDATA isJavaCorePopulated; /** Is set when the javacoreData contains valid data */
        I_8 layer; /** cache layer number */
        J9SharedClassJavacoreDataDescriptor javacoreData; /** If isCompatible is true, then extra information about the cache is available in here*/
} SH_OSCache_Info;

/* DO NOT use UDATA/IDATA in the cache headers so that 32-bit/64-bit JVMs can read each others headers
 * 
 * Versioning is achieved by using the typedef aliases below
 */

typedef struct OSCache_header1 {
	J9PortShcVersion versionData;
	U_32 size;
	U_32 unused1;
	U_32 unused2;
	J9SRP dataStart;
	U_32 dataLength;
	U_32 generation;
	U_64 buildID;
} OSCache_header1;

typedef struct OSCache_header2 {
	J9PortShcVersion versionData;
	U_32 size;
	J9SRP dataStart;
	U_32 dataLength;
	U_32 generation;
	U_32 cacheInitComplete;
	U_64 buildID;
	U_32 unused32[5];
	U_64 createTime;
	U_64 unused64[4];
} OSCache_header2;

typedef OSCache_header2 OSCache_header_version_current;
typedef OSCache_header2 OSCache_header_version_G04;
typedef OSCache_header1 OSCache_header_version_G03;
/* STRUCT_G02 and STRUCT_G01 have not shipped, so are not included here */

#define OSCACHE_HEADER_FIELD_SIZE 1
#define OSCACHE_HEADER_FIELD_DATA_START 2
#define OSCACHE_HEADER_FIELD_DATA_LENGTH 3
#define OSCACHE_HEADER_FIELD_GENERATION 4
#define OSCACHE_HEADER_FIELD_BUILDID 5
#define OSCACHE_HEADER_FIELD_CACHE_INIT_COMPLETE 6

/* Structure to store last portable error code */
typedef struct LastErrorInfo {
	I_32 lastErrorCode;
	const char *lastErrorMsg;
} LastErrorInfo;

/**
 * A class to manage Shared Classes on Operating System level
 * 
 * This class provides and abstraction of a shared memory region and its control
 * mutex.
 *
 */
class SH_OSCache
{
public:
	class SH_OSCacheInitializer
	{
		public:
		virtual void init(char* data, U_32 len, I_32 minAOT, I_32 maxAOT, I_32 minJIT, I_32 maxJIT, U_32 readWriteLen, U_32 softMaxBytes) = 0;
	};
  
	/**
	 * Override new operator
	 * @param [in] size  The size of the object
	 * @param [in] memoryPtr  Pointer to the address where the object must be located
	 *
	 * @return The value of memoryPtr
	 */
	void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

	static SH_OSCache* newInstance(J9PortLibrary* portlib, SH_OSCache* memForConstructor, const char* cacheName, UDATA generation, J9PortShcVersion* versionData, I_8 layer);

	static UDATA getRequiredConstrBytes(void);
	
	static IDATA getCacheDir(J9JavaVM* vm, const char* ctrlDirName, char* buffer, UDATA bufferSize, U_32 cacheType, bool allowVerbose = true);
	
	static IDATA createCacheDir(J9PortLibrary* portLibrary, char* cacheDirName, UDATA cacheDirPerm, bool cleanMemorySegments);

	static void getCacheVersionAndGen(J9PortLibrary* portlib, J9JavaVM* vm, char* buffer, UDATA bufferSize, const char* cacheName,
			J9PortShcVersion* versionData, UDATA generation, bool isMemoryType, I_8 layer);

	static UDATA getCurrentCacheGen(void);

	static UDATA statCache(J9PortLibrary* portLibrary, const char* cacheDirName, const char* cacheNameWithVGen, bool displayNotFoundMsg);

	static J9Pool* getAllCacheStatistics(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, UDATA localVerboseFlags, UDATA j2seVersion, bool includeOldGenerations, bool ignoreCompatible, UDATA reason, bool isCache);

	static IDATA getCacheStatistics(J9JavaVM* vm, const char* ctrlDirName, const char* cacheNameWithVGen, UDATA groupPerm, UDATA localVerboseFlags, UDATA j2seVersion, SH_OSCache_Info* result, UDATA reason, bool getLowerLayerStats, bool isTopLayer, J9Pool** lowerLayerList, SH_OSCache* oscache);
	
	static bool isTopLayerCache(J9JavaVM* vm, const char* ctrlDirName, char* nameWithVGen);
	
	static IDATA getCachePathName(J9PortLibrary* portLibrary, const char* cacheDirName, char* buffer, UDATA bufferSize, const char* cacheNameWithVGen);
	
	static void getCacheNameAndLayerFromUnqiueID(J9JavaVM* vm, const char* uniqueID, UDATA idLen, char* nameBuf, UDATA nameBuffLen, I_8* layer);
	
	static UDATA generateCacheUniqueID(J9VMThread* currentThread, const char* cacheDir, const char* cacheName, I_8 layer, U_32 cacheType, char* buf, UDATA bufLen, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes);
	
	const char* getCacheUniqueID(J9VMThread* currentThread, U_64 createtime, UDATA metadataBytes, UDATA classesBytes, UDATA lineNumTabBytes, UDATA varTabBytes);

	I_8 getLayer();
	
	const char* getCacheNameWithVGen();

	bool isRunningReadOnly();
	
	U_32 getCacheType();
	
	const char* getCacheFullPathName() const;

	virtual bool startup(J9JavaVM* vm, const char* cacheDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig_, IDATA numLocks,
			UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, UDATA storageKeyTesting, J9PortShcVersion* versionData, SH_OSCacheInitializer* i, UDATA reason) = 0;

	virtual IDATA destroy(bool suppressVerbose, bool isReset = false) = 0;

	virtual void cleanup(void) = 0;

	virtual IDATA getWriteLockID(void) = 0;
	virtual IDATA getReadWriteLockID(void) = 0;
	virtual IDATA acquireWriteLock(UDATA lockid) = 0;
	virtual IDATA releaseWriteLock(UDATA lockid) = 0;
	virtual U_64 getCreateTime(void) = 0;
  	
	virtual void *attach(J9VMThread* currentThread, J9PortShcVersion* expectedVersionData) = 0;

#if defined (J9SHR_MSYNC_SUPPORT)
	virtual IDATA syncUpdates(void* start, UDATA length, U_32 flags) = 0;
#endif
	
	virtual IDATA getError(void) = 0;
	
	virtual void runExitCode(void) = 0;
	
	virtual IDATA getLockCapabilities(void) = 0;
	
	virtual IDATA setRegionPermissions(struct J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags) = 0;
	
	virtual UDATA getPermissionsRegionGranularity(struct J9PortLibrary* portLibrary) = 0;

	virtual U_32 getDataSize();

	virtual U_32 getTotalSize() = 0;

	virtual UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor) = 0;

	virtual void * getAttachedMemory() = 0;

	virtual void * getOSCacheStart();

	virtual void getCorruptionContext(IDATA *corruptionCode, UDATA *corruptValue);

	virtual void setCorruptionContext(IDATA corruptionCode, UDATA corruptValue);
	
	virtual SH_CacheAccess isCacheAccessible(void) const { return J9SH_CACHE_ACCESS_ALLOWED; }

	virtual void  dontNeedMetadata(J9VMThread* currentThread, const void* startAddress, size_t length);
	
	virtual IDATA detach(void) = 0;
	
	virtual bool fixAndWriteOSCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 size) = 0;

protected:	
	/*This constructor should only be used by this class*/
	SH_OSCache() {};

	virtual void initialize(J9PortLibrary* portLib_, char* memForConstructor, UDATA generation, I_8 layer) = 0;

	virtual void errorHandler(U_32 moduleName, U_32 id, LastErrorInfo *lastErrorInfo) = 0;

	static IDATA removeCacheVersionAndGen(char* buffer, UDATA bufferSize, UDATA versionLen, const char* cacheNameWithVGen);
	
	IDATA commonStartup(J9JavaVM* vm, const char* ctrlDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig_, UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, J9PortShcVersion* versionData);

	void commonInit(J9PortLibrary* portLibrary, UDATA generation, I_8 layer = 0);

	void commonCleanup();
	
	void initOSCacheHeader(OSCache_header_version_current* header, J9PortShcVersion* versionData, UDATA headerLen);
	
	IDATA checkOSCacheHeader(OSCache_header_version_current* header, J9PortShcVersion* versionData, UDATA headerLen);

	static void* getHeaderFieldAddressForGen(void* header, UDATA headerGen, UDATA fieldID);

	static IDATA getHeaderFieldOffsetForGen(UDATA headerGen, UDATA fieldID);
	
	static UDATA getGenerationFromName(const char* cacheNameWithVGen);
	
	static U_64 getCacheVersionToU64(U_32 major, U_32 minor);

	static bool getCacheStatsCommon(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, SH_OSCache *cache, SH_OSCache_Info *cacheInfo, J9Pool **lowerLayerList);

	char* _cacheName;
	/* Align the U_64 so we don't have too many platform specific structure padding
	 * issues in the debug extensions. Note C++ adds a pointer variable at the start
	 * of the structure so putting it second should align it.
	 */
	U_64 _runtimeFlags;
	U_32 _cacheSize;
	void* _headerStart;
	void* _dataStart;
	U_32 _dataLength;
	char* _cacheNameWithVGen;
	char* _cachePathName;
	char* _cacheUniqueID;
	UDATA _activeGeneration;
	I_8 _layer;
	UDATA _createFlags;
	UDATA _verboseFlags;
	IDATA _errorCode;
	const J9SharedClassPreinitConfig* _config;	
	I_32 _openMode;
	bool _runningReadOnly;
	J9PortLibrary* _portLibrary;
	char* _cacheDirName;
	bool _startupCompleted;
	bool _doCheckBuildID;
	IDATA _corruptionCode;
	UDATA _corruptValue;
	bool _isUserSpecifiedCacheDir;
	
private:
	void setEnableVerbose(J9PortLibrary* portLib, J9JavaVM* vm, J9PortShcVersion* versionData, char* cacheNameWithVGen);

};

#endif /* !defined(OSCACHE_HPP_INCLUDED) */

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
#if !defined(OSCACHESYSV_HPP_INCLUDED)
#define OSCACHESYSV_HPP_INCLUDED

/* @ddr_namespace: default */
#include "j9.h"
#include "j9port.h"
#include "OSCache.hpp"
#include "pool_api.h"

/* DO NOT use UDATA/IDATA in the cache headers so that 32-bit/64-bit JVMs can read each others headers
 * This is why OSCache_sysv_header3 was added.  
 * 
 * Versioning is achieved by using the typedef aliases below
 */

typedef struct OSCache_sysv_header1 {
	char eyecatcher[J9PORT_SHMEM_EYECATCHER_LENGTH+1];
	UDATA version;
	U_64 modlevel;
	UDATA size;
	UDATA maxGeneration;
	UDATA available;
} OSCache_sysv_header1;

typedef struct OSCache_sysv_header2 {
	char eyecatcher[J9PORT_SHMEM_EYECATCHER_LENGTH+1];
	OSCache_header1 oscHdr;
	UDATA inDefaultControlDir;
	UDATA cacheInitComplete;
	UDATA unused[9];
} OSCache_sysv_header2;

typedef struct OSCache_sysv_header3 {
	char eyecatcher[J9PORT_SHMEM_EYECATCHER_LENGTH+1];
	OSCache_header2 oscHdr;
	U_32 inDefaultControlDir;
	I_32 attachedSemid;
	U_32 unused32[4];
	U_64 unused64[5];
} OSCache_sysv_header3;

typedef OSCache_sysv_header3 OSCachesysv_header_version_current;
typedef OSCache_sysv_header3 OSCachesysv_header_version_G04;
typedef OSCache_sysv_header2 OSCachesysv_header_version_G03;
/* version_G02 has not shipped, so is not included here */
typedef OSCache_sysv_header1 OSCachesysv_header_version_G01;

#define OSCACHESYSV_HEADER_FIELD_IN_DEFAULT_CONTROL_DIR 1001
#define OSCACHESYSV_HEADER_FIELD_CACHE_INIT_COMPLETE 1002

/**
 * This enum contains constants that are used to indicate the reason for not allowing access to the semaphore set.
 * It is returned by @ref SH_OSCachesysv::checkSemaphoreAccess().
 */
typedef enum SH_SysvSemAccess {
	J9SH_SEM_ACCESS_ALLOWED 				= 0,
	J9SH_SEM_ACCESS_CANNOT_BE_DETERMINED,
	J9SH_SEM_ACCESS_OWNER_NOT_CREATOR,
	J9SH_SEM_ACCESS_GROUP_ACCESS_REQUIRED,
	J9SH_SEM_ACCESS_OTHERS_NOT_ALLOWED
} SH_SysvSemAccess;

/**
 * This enum contains constants that are used to indicate the reason for not allowing access to the shared memory.
 * It is returned by @ref SH_OSCachesysv::checkSharedMemoryAccess().
 */
typedef enum SH_SysvShmAccess {
	J9SH_SHM_ACCESS_ALLOWED 						= 0,
	J9SH_SHM_ACCESS_CANNOT_BE_DETERMINED,
	J9SH_SHM_ACCESS_OWNER_NOT_CREATOR,
	J9SH_SHM_ACCESS_GROUP_ACCESS_REQUIRED,
	J9SH_SHM_ACCESS_GROUP_ACCESS_READONLY_REQUIRED,
	J9SH_SHM_ACCESS_OTHERS_NOT_ALLOWED
} SH_SysvShmAccess;

/**
 * A class to manage Shared Classes on Operating System level
 * 
 * This class provides and abstraction of a shared memory region and its control
 * mutex.
 *
 */
class SH_OSCachesysv : public SH_OSCache
{
public:
	SH_OSCachesysv(J9PortLibrary* portlib, J9JavaVM* vm, const char* cachedirname, const char* cacheName, J9SharedClassPreinitConfig* piconfig_, IDATA numLocks, UDATA createFlag,
			UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, J9PortShcVersion* versionData, SH_OSCache::SH_OSCacheInitializer* initializer);

	virtual bool startup(J9JavaVM* vm, const char* ctrlDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig_, IDATA numLocks, UDATA createFlag,
			UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, UDATA storageKeyTesting, J9PortShcVersion* versionData, SH_OSCache::SH_OSCacheInitializer* i, UDATA reason);

	/**
	 * Override new operator
	 * @param [in] size  The size of the object
	 * @param [in] memoryPtr  Pointer to the address where the object must be located
	 *
	 * @return The value of memoryPtr
	 */
	void *operator new(size_t size, void *memoryPtr) { return memoryPtr; };

	static SH_OSCache* newInstance(J9PortLibrary* portlib, SH_OSCache* memForConstructor);

	static UDATA getRequiredConstrBytes(void);
	
	IDATA destroy(bool suppressVerbose, bool isReset = false);

	void cleanup(void);

	IDATA getWriteLockID(void);
	IDATA getReadWriteLockID(void);
	IDATA acquireWriteLock(UDATA lockID);
	IDATA releaseWriteLock(UDATA lockID);
	U_64 getCreateTime(void);
  	
	static IDATA getCacheStats(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char* cacheNameWithVGen, SH_OSCache_Info* cacheInfo, UDATA reason, J9Pool** lowerLayerList);
	
	static IDATA getNonTopLayerCacheInfo(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char *cacheNameWithVGen, SH_OSCache_Info *cacheInfo, UDATA reason, SH_OSCachesysv* oscache);
	
	void *attach(J9VMThread *currentThread, J9PortShcVersion* expectedVersionData);
	
	virtual IDATA detach(void);
	
#if defined (J9SHR_MSYNC_SUPPORT)
	IDATA syncUpdates(void* start, UDATA length, U_32 flags); 
#endif
	
	IDATA getError(void);
	
	void runExitCode(void);
	
	IDATA getLockCapabilities(void);
	
	IDATA setRegionPermissions(struct J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags);
	
	UDATA getPermissionsRegionGranularity(struct J9PortLibrary* portLibrary);

	virtual U_32 getTotalSize();
	
	virtual bool fixAndWriteOSCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 size);

	static UDATA getHeaderSize(void);

	static IDATA findAllKnownCaches(struct J9PortLibrary* portlib, UDATA j2seVersion, struct J9Pool* cacheList);

	static UDATA findfirst(struct J9PortLibrary *portLibrary, char *cacheDir, char *resultbuf);
	
	static I_32 findnext(struct J9PortLibrary *portLibrary, UDATA findHandle, char *resultbuf);
	
	static void findclose(struct J9PortLibrary *portLibrary, UDATA findhandle);

	static IDATA getSysvHeaderFieldOffsetForGen(UDATA headerGen, UDATA fieldID);

	virtual UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor);

	SH_CacheAccess isCacheAccessible(void) const;

	IDATA restoreFromSnapshot(J9JavaVM* vm, const char* snapshotName, UDATA numLocks, SH_OSCache::SH_OSCacheInitializer* i, bool* cacheExist);

/* protected: */
	/*This constructor should only be used by this class and parent*/
	SH_OSCachesysv() {};
	virtual void initialize(J9PortLibrary* portLib_, char* memForConstructor, UDATA generation, I_8 layer);

protected :
	
	virtual void errorHandler(U_32 moduleName, U_32 id, LastErrorInfo *lastErrorInfo);
	virtual void * getAttachedMemory();
  
private:
	j9shmem_handle* _shmhandle;
	j9shsem_handle* _semhandle;

	IDATA _attach_count;
	UDATA _totalNumSems;
	UDATA _userSemCntr;
	U_32 _actualCacheSize;

	char* _shmFileName;
	char* _semFileName;
	bool _openSharedMemory;
	
	UDATA _storageKeyTesting;

	const J9SharedClassPreinitConfig* config;

	SH_OSCache::SH_OSCacheInitializer* _initializer;
	UDATA _groupPerm;

	I_32 _semid;

	SH_SysvSemAccess _semAccess;
	SH_SysvShmAccess _shmAccess;

	J9ControlFileStatus _controlFileStatus;

	IDATA openCache(const char* ctrlDirName, J9PortShcVersion* versionData, bool semCreated);

	IDATA shmemOpenWrapper(const char *cacheName, LastErrorInfo *lastErrorInfo);

	IDATA initializeHeader(const char* ctrlDirName, J9PortShcVersion* versionData, LastErrorInfo lastErrorInfo);
	IDATA verifyCacheHeader(J9PortShcVersion* versionData);

	IDATA detachRegion(void);

	IDATA enterHeaderMutex(LastErrorInfo *lastErrorInfo);
	IDATA exitHeaderMutex(LastErrorInfo *lastErrorInfo);

	UDATA isCacheActive(void);

	/* Private Error handling functions */
	void printErrorMessage(LastErrorInfo *lastErrorInfo);

	void cleanupSysvResources(void);

	void setError(IDATA ec);

	static void* getSysvHeaderFieldAddressForGen(void* header, UDATA headerGen, UDATA fieldID);

	IDATA getNewWriteLockID(void);
	
	SH_SysvSemAccess checkSemaphoreAccess(LastErrorInfo *lastErrorInfo);
	SH_SysvShmAccess checkSharedMemoryAccess(LastErrorInfo *lastErrorInfo);

#if !defined(WIN32)
	/*Helpers for opening Unix SysV Semaphores and control files*/
	IDATA OpenSysVSemaphoreHelper(J9PortShcVersion* versionData, LastErrorInfo *lastErrorInfo);
	IDATA DestroySysVSemHelper();
	IDATA OpenSysVMemoryHelper(const char* cacheName, U_32 perm, LastErrorInfo *lastErrorInfo);
	static IDATA StatSysVMemoryHelper(J9PortLibrary* portLibrary, const char* cacheDirName, UDATA groupPerm, const char* cacheNameWithVGen, J9PortShmemStatistic * statbuf);
	IDATA DestroySysVMemoryHelper();
	static UDATA SysVCacheFileTypeHelper(U_64 currentVersion, UDATA genVersion);
	I_32 getControlFilePerm(char *cacheDirName, char *filetype, bool *isNotReadable, bool *isReadOnly);
	I_32 verifySemaphoreGroupAccess(LastErrorInfo *lastErrorInfo);
	I_32 verifySharedMemoryGroupAccess(LastErrorInfo *lastErrorInfo);
#endif

	static IDATA  getCacheStatsHelper(J9JavaVM* vm, const char* cacheDirName, UDATA groupPerm, const char* cacheNameWithVGen, SH_OSCache_Info* cacheInfo, UDATA reason);

};

#endif /* !defined(OSCACHESYSV_HPP_INCLUDED) */




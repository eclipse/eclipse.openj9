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
#if !defined(OSCACHEMMAP_HPP_INCLUDED)
#define OSCACHEMMAP_HPP_INCLUDED

/* @ddr_namespace: default */
#include "j9.h"
#include "j9port.h"
#include "pool_api.h"
#include "OSCacheFile.hpp"

#define J9SH_OSCACHE_MMAP_HEADER_LOCK_RETRY_COUNT 50
#define J9SH_OSCACHE_MMAP_HEADER_LOCK_RETRY_SLEEP_MILLIS 2 /* 2ms chosen after measuring the maximum time we hold the header lock for */

/**
 * A class to manage Shared Classes on Operating System level
 * 
 * This class provides an abstraction of a shared memory mapped file
 */
class SH_OSCachemmap : public SH_OSCacheFile
{
public:
	static IDATA getCacheStats(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char *cacheNameWithVGen, SH_OSCache_Info *cacheInfo, UDATA reason, J9Pool** lowerLayerList);
	
	static IDATA getNonTopLayerCacheInfo(J9JavaVM* vm, const char* ctrlDirName, UDATA groupPerm, const char *cacheNameWithVGen, SH_OSCache_Info *cacheInfo, UDATA reason, SH_OSCachemmap* oscache);
	
	SH_OSCachemmap(J9PortLibrary* portlib, J9JavaVM* vm, const char* cacheDirName, const char* cacheName, J9SharedClassPreinitConfig* piconfig, IDATA numLocks,
			UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, J9PortShcVersion* versionData, SH_OSCacheInitializer* initializer);
	/*This constructor should only be used by this class or its parent*/
	SH_OSCachemmap() {};

	/**
	 * Override new operator
	 * @param [in] sizeArg  The size of the object
	 * @param [in] memoryPtrArg  Pointer to the address where the object must be located
	 *
	 * @return The value of memoryPtrArg
	 */
	void *operator new(size_t sizeArg, void *memoryPtrArg) { return memoryPtrArg; };

	virtual bool startup(J9JavaVM* vm, const char* ctrlDirName, UDATA cacheDirPerm, const char* cacheName, J9SharedClassPreinitConfig* piconfig, IDATA numLocks,
			UDATA createFlag, UDATA verboseFlags, U_64 runtimeFlags, I_32 openMode, UDATA storageKeyTesting, J9PortShcVersion* versionData, SH_OSCacheInitializer* initializer, UDATA reason);

	virtual IDATA destroy(bool suppressVerbose, bool isReset = false);

	virtual void cleanup();
	
	virtual void* attach(J9VMThread *currentThread, J9PortShcVersion* expectedVersionData);

	virtual IDATA detach(void);
	
#if defined (J9SHR_MSYNC_SUPPORT)
	virtual IDATA syncUpdates(void* start, UDATA length, U_32 flags);
#endif
	
	virtual IDATA getWriteLockID(void);
	virtual IDATA getReadWriteLockID(void);
	virtual IDATA acquireWriteLock(UDATA lockID);
	virtual IDATA releaseWriteLock(UDATA lockID);
	virtual U_64 getCreateTime(void);
  	
	virtual void runExitCode();
	
	virtual IDATA getLockCapabilities();
	
	virtual void initialize(J9PortLibrary* portLibrary, char* memForConstructor, UDATA generation, I_8 layer);
	
	virtual IDATA setRegionPermissions(J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags);
	
	virtual UDATA getPermissionsRegionGranularity(J9PortLibrary* portLibrary);

	virtual U_32 getTotalSize();
	
	virtual bool fixAndWriteOSCacheHeader(J9VMThread* currentThread, IDATA fd, I_32 size);

	virtual UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor);

	SH_CacheAccess isCacheAccessible(void) const;
	virtual void dontNeedMetadata(J9VMThread* currentThread, const void* startAddress, size_t length);

protected:
	virtual void * getAttachedMemory();

private:
	I_64 _actualFileLength;
	J9MmapHandle *_mapFileHandle;
	
	UDATA _finalised;
	
	omrthread_monitor_t _lockMutex[J9SH_OSCACHE_MMAP_LOCK_COUNT];
	
	SH_CacheFileAccess _cacheFileAccess;

	IDATA acquireAttachReadLock(UDATA generation, LastErrorInfo *lastErrorInfo);
	IDATA releaseAttachReadLock(UDATA generation);

	IDATA internalAttach(bool isNewCache, UDATA generation);
	void internalDetach(UDATA generation);
	
	I_32 updateLastAttachedTime(OSCachemmap_header_version_current *cacheHeader);
	I_32 updateLastDetachedTime();
	I_32 createCacheHeader(OSCachemmap_header_version_current *cacheHeader, J9PortShcVersion* versionData);
	bool setCacheLength(U_32 cacheSize, LastErrorInfo *lastErrorInfo);
	I_32 initializeDataHeader(SH_OSCacheInitializer *initializer);
	
	bool deleteCacheFile(LastErrorInfo *lastErrorInfo);
	
	void finalise();
};

#endif /* OSCACHEMMAP_HPP_INCLUDED */

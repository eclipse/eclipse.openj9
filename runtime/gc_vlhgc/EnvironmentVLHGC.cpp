/*******************************************************************************
 * Copyright (c) 1991, 2021 IBM Corp. and others
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

#include "j9.h"
#include "j9cfg.h"

#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"
#include "HeapRegionManager.hpp"
#include "InterRegionRememberedSet.hpp"
#include "RememberedSetCardBucket.hpp"

MM_EnvironmentVLHGC::MM_EnvironmentVLHGC(OMR_VMThread *omrVMThread)
	: MM_EnvironmentBase(omrVMThread)
	,_survivorCopyScanCache(NULL)
	,_scanCache(NULL)
	,_deferredScanCache(NULL)
	, _copyForwardCompactGroups(NULL)
	, _previousConcurrentYieldCheckBytesScanned(0)
	, _rsclBufferControlBlockHead(NULL)
	, _rsclBufferControlBlockTail(NULL)
	, _rsclBufferControlBlockCount(0)
	, _rememberedSetCardBucketPool(NULL)
	, _lastOverflowedRsclWithReleasedBuffers(NULL)
{
	_typeId = __FUNCTION__;
}

/**
 * Create an Environment object.
 */
MM_EnvironmentVLHGC::MM_EnvironmentVLHGC(J9JavaVM *javaVM)
	: MM_EnvironmentBase(javaVM->omrVM)
	,_survivorCopyScanCache(NULL)
	,_scanCache(NULL)
	,_deferredScanCache(NULL)
	, _copyForwardCompactGroups(NULL)
	, _previousConcurrentYieldCheckBytesScanned(0)
	, _rsclBufferControlBlockHead(NULL)
	, _rsclBufferControlBlockTail(NULL)
	, _rsclBufferControlBlockCount(0)
	, _rememberedSetCardBucketPool(NULL)
	, _lastOverflowedRsclWithReleasedBuffers(NULL)
{
	_typeId = __FUNCTION__;
}

MM_EnvironmentVLHGC *
MM_EnvironmentVLHGC::newInstance(MM_GCExtensionsBase *extensions, OMR_VMThread *vmThread)
{
	MM_EnvironmentVLHGC *env = NULL;
	
	void* envPtr = (void *)pool_newElement(extensions->environments);
	if(NULL != envPtr) {
		env = new(envPtr) MM_EnvironmentVLHGC(vmThread);
		if (!env->initialize(extensions)) {
			env->kill();
			env = NULL;	
		}
	}

	return env;
}

void
MM_EnvironmentVLHGC::kill()
{
	MM_EnvironmentBase::kill();
}

bool
MM_EnvironmentVLHGC::initialize(MM_GCExtensionsBase *extensions )
{
	/* initialize base class */
	return MM_EnvironmentBase::initialize(extensions);
}

void
MM_EnvironmentVLHGC::initializeGCThread()
{
	Assert_MM_true(NULL == _rememberedSetCardBucketPool);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(this);
	uintptr_t threadPoolSize = extensions->getHeap()->getHeapRegionManager()->getTableRegionCount();
	/* Make this thread aware of its RSCL buckets for all regions */
	_rememberedSetCardBucketPool = &extensions->rememberedSetCardBucketPool[getWorkerID() * threadPoolSize];
    /* Associate buckets with appropriate RSCL (each RSCL maintains a list of its own buckets) */
	extensions->interRegionRememberedSet->threadLocalInitialize(this);
}

void
MM_EnvironmentVLHGC::tearDown(MM_GCExtensionsBase *extensions)
{
	/* tearDown base class */
	MM_EnvironmentBase::tearDown(extensions);
}

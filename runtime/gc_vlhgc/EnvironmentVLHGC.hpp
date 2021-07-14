
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

/**
 * @file
 * @ingroup GC_Modron_Env
 */

#if !defined(ENVIRONMENTVLHGC_HPP_)
#define ENVIRONMENTVLHGC_HPP_

#include "j9.h"
#include "j9cfg.h"
#include "j9port.h"
#include "j9protos.h"
#include "modronopt.h"

#if defined(J9VM_GC_MODRON_COMPACTION)
#include "CompactVLHGCStats.hpp"
#endif /* J9VM_GC_MODRON_COMPACTION */
#include "CycleStateVLHGC.hpp"
#include "EnvironmentBase.hpp"
#include "OwnableSynchronizerObjectBufferVLHGC.hpp"
#include "ReferenceObjectBufferVLHGC.hpp"
#include "UnfinalizedObjectBufferVLHGC.hpp"
#include "WorkStack.hpp"

class MM_GCExtensions;
class MM_CopyForwardCompactGroup;
class MM_CopyScanCache;
class MM_RememberedSetCardList;

struct MM_CardBufferControlBlock;
struct DepthStackTuple;

/**
 * @todo Provide class documentation
 * @ingroup GC_Modron_Env
 */
class MM_EnvironmentVLHGC : public MM_EnvironmentBase
{
/* Data Section */
public:
	MM_CopyScanCache *_survivorCopyScanCache; /**< the current copy cache for flipping */
	MM_CopyScanCache *_scanCache; /**< the current scan cache */
	MM_CopyScanCache *_deferredScanCache; /**< a partially scanned cache, to be scanned later */

	MM_CopyForwardCompactGroup *_copyForwardCompactGroups;  /**< List of copy-forward data for each compact group for the given GC thread (only for GC threads during copy forward operations) */
	
	uintptr_t _previousConcurrentYieldCheckBytesScanned;	/**< The number of bytes scanned in the mark stats at the end of the previous shouldYieldFromTask check in concurrent mark */

	MM_CardBufferControlBlock *_rsclBufferControlBlockHead; /**< head of BufferControlBlock thread local pool list */
	MM_CardBufferControlBlock *_rsclBufferControlBlockTail; /**< tail of BufferControlBlock thread local pool list */
	intptr_t _rsclBufferControlBlockCount;	/**< count of buffers in BufferControlBlock thread local pool list */
	MM_RememberedSetCardBucket *_rememberedSetCardBucketPool; /**< GC thread local pool of RS Card Buckets for each Region (its Card List) */
	MM_RememberedSetCardList *_lastOverflowedRsclWithReleasedBuffers; /**< in global list of overflowed RSCL, this is the last RSCL this thread visited */

	MM_CopyForwardStats _copyForwardStats;  /**< GC thread local statistics structure for copy forward collections */

	MM_MarkVLHGCStats _markVLHGCStats;
	MM_SweepVLHGCStats _sweepVLHGCStats;

#if defined(J9VM_GC_MODRON_COMPACTION)
	MM_CompactVLHGCStats _compactVLHGCStats;
#endif /* J9VM_GC_MODRON_COMPACTION */
	MM_InterRegionRememberedSetStats _irrsStats;

protected:
private:
/* Functionality Section */
public:
	static MM_EnvironmentVLHGC *newInstance(MM_GCExtensionsBase *extensions, OMR_VMThread *vmThread);
	virtual void kill();

	/**
	 * Initialization specifically for GC threads
	 */
	virtual void initializeGCThread();

	MMINLINE static MM_EnvironmentVLHGC *getEnvironment(J9VMThread *vmThread) { return static_cast<MM_EnvironmentVLHGC*>(vmThread->gcExtensions); }
	MMINLINE static MM_EnvironmentVLHGC *getEnvironment(OMR_VMThread *omrVMThread) { return static_cast<MM_EnvironmentVLHGC*>(omrVMThread->_gcOmrVMThreadExtensions); }
	MMINLINE static MM_EnvironmentVLHGC *getEnvironment(MM_EnvironmentBase* env) { return static_cast<MM_EnvironmentVLHGC*>(env); }

	/**
	 * Create an Environment object.
	 */
	MM_EnvironmentVLHGC(OMR_VMThread *vmThread);

	/**
	 * Create an Environment object.
	 */
	MM_EnvironmentVLHGC(J9JavaVM *javaVM);

protected:
	virtual bool initialize(MM_GCExtensionsBase *extensions);
	virtual void tearDown(MM_GCExtensionsBase *extensions);
	
private:
	
};

#endif /* ENVIRONMENTVLHGC_HPP_ */

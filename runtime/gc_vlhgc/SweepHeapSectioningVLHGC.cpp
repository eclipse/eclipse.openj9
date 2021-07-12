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
#include "omrcomp.h"

#include "SweepHeapSectioningVLHGC.hpp"
#include "SweepPoolManager.hpp"

#include "EnvironmentBase.hpp"
#include "HeapRegionIteratorVLHGC.hpp"
#include "HeapRegionManager.hpp"
#include "MemoryPool.hpp"
#include "MemorySubSpace.hpp"
#include "ParallelDispatcher.hpp"

/**
 * Walk all segments and calculate the maximum number of chunks needed to represent the current heap.
 * The chunk calculation is done on a per segment basis (no segment can represent memory from more than 1 chunk),
 * and partial sized chunks (ie: less than the chunk size) are reserved for any remaining space at the end of a
 * segment.
 * @return number of chunks required to represent the current heap memory.
 */
UDATA
MM_SweepHeapSectioningVLHGC::calculateActualChunkNumbers() const
{
	MM_Heap *heap = _extensions->heap;
	MM_HeapRegionManager *regionManager = heap->getHeapRegionManager();
	GC_HeapRegionIterator regionIterator(regionManager);
	UDATA regionCount = 0;
	while(regionIterator.nextRegion() != NULL) {
		regionCount += 1;
	}
	
	UDATA regionSize = regionManager->getRegionSize();
	UDATA sweepChunkSize = _extensions->parSweepChunkSize;
	UDATA totalChunkCount = regionCount * (MM_Math::roundToCeiling(sweepChunkSize, regionSize) / sweepChunkSize);

	return totalChunkCount;
}

/**
 * Allocate and initialize a new instance of the receiver.
 * @return pointer to the new instance.
 */
MM_SweepHeapSectioningVLHGC *
MM_SweepHeapSectioningVLHGC::newInstance(MM_EnvironmentBase *env)
{
	MM_SweepHeapSectioningVLHGC *sweepHeapSectioning = (MM_SweepHeapSectioningVLHGC *)env->getForge()->allocate(sizeof(MM_SweepHeapSectioningVLHGC), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (sweepHeapSectioning) {
		new(sweepHeapSectioning) MM_SweepHeapSectioningVLHGC(env);
		if (!sweepHeapSectioning->initialize(env)) {
			sweepHeapSectioning->kill(env);
			return NULL;
		}
	}
	return sweepHeapSectioning;
}

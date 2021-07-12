
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

#if !defined(SWEEPSCHEMESECTIONINGSEGMENTEDVLHGC_HPP_)
#define SWEEPSCHEMESECTIONINGSEGMENTEDVLHGC_HPP_

#include "SweepHeapSectioning.hpp"

#include "ParallelSweepChunk.hpp"
#include "GCExtensions.hpp"
#include "EnvironmentBase.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"

class MM_ParallelSweepChunkArray;

/**
 * Support for sectioning the heap into chunks useable by sweep (and compact).
 * 
 * @ingroup GC_Modron_Standard
 */
class MM_SweepHeapSectioningVLHGC : public MM_SweepHeapSectioning
{
private:
protected:
	virtual UDATA calculateActualChunkNumbers() const;
	virtual bool isReadyToSweep(MM_EnvironmentBase* env, MM_HeapRegionDescriptor* region)
	{
		return (!((MM_HeapRegionDescriptorVLHGC *)region)->_sweepData._alreadySwept && region->hasValidMarkMap());
	}

public:
	static MM_SweepHeapSectioningVLHGC *newInstance(MM_EnvironmentBase *env);

	MM_SweepHeapSectioningVLHGC(MM_EnvironmentBase *env)
		: MM_SweepHeapSectioning(env)
	{
		_typeId = __FUNCTION__;
	}
};

#endif /* SWEEPSCHEMESECTIONINGSEGMENTEDVLHGC_HPP_ */

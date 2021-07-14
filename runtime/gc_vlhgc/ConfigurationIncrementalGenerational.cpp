
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
 * @ingroup GC_Modron_Standard
 */

#include "j9.h"
#include "j9cfg.h"

#include "ConfigurationIncrementalGenerational.hpp"

#include "ClassLoaderRememberedSet.hpp"
#include "CompressedCardTable.hpp"
#include "EnvironmentVLHGC.hpp"
#include "GCExtensions.hpp"
#include "GlobalAllocationManagerTarok.hpp"
#include "GlobalCollector.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"
#include "HeapRegionManager.hpp"
#include "HeapRegionManagerVLHGC.hpp"
#if defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD)
#include "HeapRegionStateTable.hpp"
#endif /* defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD) */
#include "HeapVirtualMemory.hpp"
#include "MemorySpace.hpp"
#include "MemorySubSpaceTarok.hpp"
#include "IncrementalCardTable.hpp"
#include "IncrementalGenerationalGC.hpp"
#include "InterRegionRememberedSet.hpp"
#include "PhysicalArenaRegionBased.hpp"
#include "PhysicalSubArenaRegionBased.hpp"
#include "SweepPoolManagerAddressOrderedList.hpp"
#include "SweepPoolManagerVLHGC.hpp"

#define TAROK_MINIMUM_REGION_SIZE_BYTES (512 * 1024)

MM_Configuration *
MM_ConfigurationIncrementalGenerational::newInstance(MM_EnvironmentBase *env)
{
	MM_ConfigurationIncrementalGenerational *configuration = (MM_ConfigurationIncrementalGenerational *) env->getForge()->allocate(sizeof(MM_ConfigurationIncrementalGenerational), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	
	if(NULL != configuration) {
		new(configuration) MM_ConfigurationIncrementalGenerational(env);
		if(!configuration->initialize(env)) {
			configuration->kill(env);
			configuration = NULL;
		}
	}
	return configuration;
}

/**
 * Create the global collector for a Tarok configuration
 */
MM_GlobalCollector *
MM_ConfigurationIncrementalGenerational::createGlobalCollector(MM_EnvironmentBase *envBase)
{
	MM_GCExtensionsBase *extensions = envBase->getExtensions();
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	MM_HeapRegionManager *heapRegionManager = extensions->heapRegionManager;

	return MM_IncrementalGenerationalGC::newInstance(env, heapRegionManager);
}

/**
 * Create the heap for a region based configuration
 */
MM_Heap *
MM_ConfigurationIncrementalGenerational::createHeapWithManager(MM_EnvironmentBase *env, UDATA heapBytesRequested, MM_HeapRegionManager *regionManager)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	MM_Heap *heap = MM_HeapVirtualMemory::newInstance(env, extensions->heapAlignment, heapBytesRequested, regionManager);
	if (NULL == heap) {
		return NULL;
	}

#if defined(J9VM_GC_ENABLE_DOUBLE_MAP)
	/* Enable double mapping if glibc version 2.27 or newer is found. For double map to
	 * work we need a file descriptor, to get one we use shm_open(3)  or memfd_create(2);
	 * shm_open(3) has 2 drawbacks: [I] shared memory is used; [II] does not support
	 * anonymous huge pages. [I] shared memory in Linux systems has a limit (half of
	 * physical memory). [II] if we create a file descriptor using shm_open(3) and then
	 * try to mmap with huge pages with the respective file descriptor the mmap call
	 * fails. It would only succeed if MAP_ANON flag was provided, but doing so ignores
	 * the file descriptor which is the opposite of what we want. In a newer glibc
	 * version (glibc 2.27 onwards) there’s a new function that does exactly what we
	 * want, and that's memfd_create(2); however that's only supported in glibc 2.27. We
	 * also need to check if region size is a bigger or equal to multiple of page size.
	 *
	 */
	if (extensions->isArrayletDoubleMapRequested && extensions->isArrayletDoubleMapAvailable) {
		uintptr_t pagesize = heap->getPageSize();
		if (!extensions->memoryManager->isLargePage(env, pagesize) || (pagesize <= extensions->getOmrVM()->_arrayletLeafSize)) {
			extensions->indexableObjectModel.setEnableDoubleMapping(true);
		}
	}
#endif /* J9VM_GC_ENABLE_DOUBLE_MAP */

	/* when we try to attach this heap to a region manager, we will need the card table since it needs to be NUMA-affinitized using the same logic as the heap so initialize it here */
	extensions->cardTable = MM_IncrementalCardTable::newInstance(MM_EnvironmentVLHGC::getEnvironment(env), heap);
	if (NULL == extensions->cardTable) {
		heap->kill(env);
		return NULL;
	}

	if (extensions->tarokEnableCompressedCardTable) {
		extensions->compressedCardTable = MM_CompressedCardTable::newInstance(MM_EnvironmentVLHGC::getEnvironment(env), heap);
		if (NULL == extensions->compressedCardTable) {
			extensions->cardTable->kill(env);
			extensions->cardTable = NULL;
			heap->kill(env);
			return NULL;
		}
	}

#if defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD)
	if (extensions->isConcurrentCopyForwardEnabled()) {
		uintptr_t heapBase = (uintptr_t) heap->getHeapBase();
		uintptr_t regionShift = regionManager->getRegionShift();
		uintptr_t regionCount = heap->getMaximumPhysicalRange() >> regionShift;

		extensions->heapRegionStateTable =  OMR::GC::HeapRegionStateTable::newInstance(env->getForge(), heapBase, regionShift, regionCount);
		if (NULL == extensions->heapRegionStateTable) {
			extensions->compressedCardTable->kill(env);
			extensions->compressedCardTable = NULL;
			extensions->cardTable->kill(env);
			extensions->cardTable = NULL;
			heap->kill(env);
			return NULL;
		}
	}
#endif /* defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD) */

	return heap;
}

MM_EnvironmentBase *
MM_ConfigurationIncrementalGenerational::allocateNewEnvironment(MM_GCExtensionsBase *extensions, OMR_VMThread *omrVMThread)
{
	return MM_EnvironmentVLHGC::newInstance(extensions, omrVMThread);
}

J9Pool *
MM_ConfigurationIncrementalGenerational::createEnvironmentPool(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	uintptr_t numberOfElements = getConfigurationDelegate()->getInitialNumberOfPooledEnvironments(env);
	/* number of elements, pool flags = 0, 0 selects default pool configuration (at least 1 element, puddle size rounded to OS page size) */
	return pool_new(sizeof(MM_EnvironmentVLHGC), numberOfElements, sizeof(U_64), 0, J9_GET_CALLSITE(), OMRMEM_CATEGORY_MM, POOL_FOR_PORT(PORTLIB));
}

bool
MM_ConfigurationIncrementalGenerational::initializeEnvironment(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	OMR_VM *omrVM = env->getOmrVM();

	if (!MM_Configuration::initializeEnvironment(env)) {
		return false;
	}
	
	/* acquire an ACT for this env */
	if (!extensions->globalAllocationManager->acquireAllocationContext(env)) {
		return false;
	}

	vmThread->cardTableVirtualStart = (U_8 *)j9gc_incrementalUpdate_getCardTableVirtualStart(omrVM);
	vmThread->cardTableShiftSize = j9gc_incrementalUpdate_getCardTableShiftValue(omrVM);

	return true;
}

MM_MemorySpace *
MM_ConfigurationIncrementalGenerational::createDefaultMemorySpace(MM_EnvironmentBase *env, MM_Heap *heap, MM_InitializationParameters *parameters)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	MM_HeapRegionManager *regionManager = extensions->heapRegionManager;
	Assert_MM_true(NULL != regionManager);

	/* Create Sweep Pool Manager for VLHGC */
	extensions->sweepPoolManagerAddressOrderedList = (MM_SweepPoolManagerAddressOrderedList *) MM_SweepPoolManagerVLHGC::newInstance(env);
	if (NULL == extensions->sweepPoolManagerAddressOrderedList) {
		return NULL;
	}

	/* allocate size: (region count) X (max GC thread count) X (size of Bucket) */
	UDATA allocateSize = sizeof(MM_RememberedSetCardBucket);
	allocateSize *= extensions->getHeap()->getHeapRegionManager()->getTableRegionCount();
	allocateSize *= extensions->gcThreadCount;

	extensions->rememberedSetCardBucketPool = (MM_RememberedSetCardBucket *)extensions->getForge()->allocate(allocateSize, MM_AllocationCategory::REMEMBERED_SET, J9_GET_CALLSITE());
	if (NULL == extensions->rememberedSetCardBucketPool) {
		return NULL;
	}
	
	/* this is as good a place as any to create the global allocation manager */
	MM_GlobalAllocationManagerTarok *gamt = MM_GlobalAllocationManagerTarok::newInstance(env);
	if (NULL == gamt) {
		return NULL;
	}
	extensions->globalAllocationManager = gamt;
	
	MM_PhysicalSubArenaRegionBased *physicalSubArena = MM_PhysicalSubArenaRegionBased::newInstance(env, heap);
	if(NULL == physicalSubArena) {
		return NULL;
	}

	bool usesGlobalCollector = true;
	MM_MemorySubSpaceTarok *memorySubspaceTarok = MM_MemorySubSpaceTarok::newInstance(env, physicalSubArena, gamt, usesGlobalCollector, parameters->_minimumSpaceSize, parameters->_initialOldSpaceSize, parameters->_maximumSpaceSize, MEMORY_TYPE_OLD, 0);
	if(NULL == memorySubspaceTarok) {
		return NULL;
	}
	/* the subspace exists so now we can request that the allocation contexts are created (since they require the subspace) */
	if (!gamt->initializeAllocationContexts(env, memorySubspaceTarok)) {
		memorySubspaceTarok->kill(env);
		return NULL;
	}
	/* now, configure the collector with this subspace */
	((MM_IncrementalGenerationalGC *)extensions->getGlobalCollector())->setConfiguredSubspace(env, memorySubspaceTarok);
	

	MM_PhysicalArenaRegionBased *physicalArena = MM_PhysicalArenaRegionBased::newInstance(env, heap);
	if(NULL == physicalArena) {
		memorySubspaceTarok->kill(env);
		return NULL;
	}

	return MM_MemorySpace::newInstance(env, heap, physicalArena, memorySubspaceTarok, parameters, MEMORY_SPACE_NAME_FLAT, MEMORY_SPACE_DESCRIPTION_FLAT);
}


void 
MM_ConfigurationIncrementalGenerational::defaultMemorySpaceAllocated(MM_GCExtensionsBase *extensions, void* defaultMemorySpace)
{
	MM_EnvironmentVLHGC env((J9JavaVM *)extensions->getOmrVM()->_language_vm);
	MM_Configuration::defaultMemorySpaceAllocated(extensions, defaultMemorySpace);
	/* initialize TaxationThreshold and RememberedSetCardBucketPool before first gc occurs and all of dependance has been set. */
	((MM_IncrementalGenerationalGC *)extensions->getGlobalCollector())->initializeTaxationThreshold(&env);
	extensions->interRegionRememberedSet->initializeRememberedSetCardBucketPool(&env);
}

bool
MM_ConfigurationIncrementalGenerational::initialize(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	bool result = MM_Configuration::initialize(env);

	/* By default disable hot field depth copying */
	env->disableHotFieldDepthCopy();

	if (result) {
		if (MM_GCExtensions::OMR_GC_SCAVENGER_SCANORDERING_NONE == extensions->scavengerScanOrdering) {
			extensions->scavengerScanOrdering = MM_GCExtensions::OMR_GC_SCAVENGER_SCANORDERING_DYNAMIC_BREADTH_FIRST;
		}
		extensions->setVLHGC(true);
	}

#define DEFAULT_MAX_AGE_FOR_PGC_COUNT_BASED				24
#define DEFAULT_MAX_NURSERY_AGE							1
#define DEFAULT_MAX_AGE_FOR_ALLOCATION_BASED			5

	/* set default region maximum age if it is not specified yet */
	if (0 == extensions->tarokRegionMaxAge) {
		if (extensions->tarokAllocationAgeEnabled) {
			extensions->tarokRegionMaxAge = DEFAULT_MAX_AGE_FOR_ALLOCATION_BASED;
		} else {
			extensions->tarokRegionMaxAge = DEFAULT_MAX_AGE_FOR_PGC_COUNT_BASED;
		}
	}

	if (!extensions->tarokNurseryMaxAge._wasSpecified) {
		/* set default nursery age */
		extensions->tarokNurseryMaxAge._valueSpecified = DEFAULT_MAX_NURSERY_AGE;
	} else {
		/* specified nursery age is out of range - correct it to default */
		if (extensions->tarokNurseryMaxAge._valueSpecified >= extensions->tarokRegionMaxAge) {
			extensions->tarokNurseryMaxAge._valueSpecified = DEFAULT_MAX_NURSERY_AGE;
		}
	}

	if (!extensions->tarokMinimumGMPWorkTargetBytes._wasSpecified) {
		/* default to a region size.  No real reason for choosing this number other than that it is sized relative to the heap */
		extensions->tarokMinimumGMPWorkTargetBytes._valueSpecified = extensions->regionSize;
	}

	if (!extensions->dnssExpectedTimeRatioMaximum._wasSpecified) {
		extensions->dnssExpectedTimeRatioMaximum._valueSpecified = 0.05;
	} 

	if (!extensions->dnssExpectedTimeRatioMinimum._wasSpecified) {
		extensions->dnssExpectedTimeRatioMinimum._valueSpecified = 0.02;
	} 

	return result;
}

void
MM_ConfigurationIncrementalGenerational::tearDown(MM_EnvironmentBase *env)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

	if (NULL != extensions->sweepPoolManagerAddressOrderedList) {
		extensions->sweepPoolManagerAddressOrderedList->kill(env);
		extensions->sweepPoolManagerAddressOrderedList = NULL;
	}

	if (NULL != extensions->cardTable) {
		extensions->cardTable->kill(MM_EnvironmentVLHGC::getEnvironment(env));
		extensions->cardTable = NULL;
	}

	if (NULL != extensions->compressedCardTable) {
		extensions->compressedCardTable->kill(env);
		extensions->compressedCardTable = NULL;
	}

#if defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD)
	if (NULL != extensions->heapRegionStateTable) {
		extensions->heapRegionStateTable->kill(env->getForge());
		extensions->heapRegionStateTable = NULL;
	}
#endif /* defined(OMR_GC_VLHGC_CONCURRENT_COPY_FORWARD) */

	MM_Configuration::tearDown(env);

	// cleanup after extensions->heapRegionManager
	if (NULL != extensions->rememberedSetCardBucketPool) {
		extensions->getForge()->free(extensions->rememberedSetCardBucketPool);
		extensions->rememberedSetCardBucketPool = NULL;
	}
}

MM_HeapRegionManager *
MM_ConfigurationIncrementalGenerational::createHeapRegionManager(MM_EnvironmentBase *env)
{
	MM_GCExtensionsBase *extensions = env->getExtensions();
	return MM_HeapRegionManagerVLHGC::newInstance(env, extensions->regionSize, sizeof(MM_HeapRegionDescriptorVLHGC), MM_HeapRegionDescriptorVLHGC::initializer, MM_HeapRegionDescriptorVLHGC::destructor);
}

void 
MM_ConfigurationIncrementalGenerational::cleanUpClassLoader(MM_EnvironmentBase *env, J9ClassLoader* classLoader)
{
	MM_GCExtensionsBase *extensions = env->getExtensions();
	MM_ClassLoaderRememberedSet *classLoaderRememberedSet = extensions->classLoaderRememberedSet;
	if (MM_CycleState::CT_PARTIAL_GARBAGE_COLLECTION == env->_cycleState->_collectionType) {
		/* during PGCs we should never unload a class loader which is remembered because it could have instances */
		Assert_MM_false(classLoaderRememberedSet->isRemembered(env, classLoader));
	}
	classLoaderRememberedSet->killRememberedSet(env, classLoader);
}


void
MM_ConfigurationIncrementalGenerational::prepareParameters(OMR_VM *omrVM, UDATA minimumSpaceSize, UDATA minimumNewSpaceSize,
			     UDATA initialNewSpaceSize, UDATA maximumNewSpaceSize, UDATA minimumTenureSpaceSize, UDATA initialTenureSpaceSize,
			     UDATA maximumTenureSpaceSize, UDATA memoryMax, UDATA tenureFlags, MM_InitializationParameters *parameters)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(omrVM);
	UDATA contextCount = MM_GlobalAllocationManagerTarok::calculateIdealManagedContextCount(extensions);

	/* Each AC needs a region, so we adjust the calculated values as per generic formulas.
	 * Alternatively, we could leave min/initial size as is, but make early initialize fail (and thus fail to start VM),
	 * if specified sizes are lower than VLHGC specific values (regionSize*ACcount) */
	minimumSpaceSize = OMR_MAX(minimumSpaceSize, extensions->regionSize * contextCount);
	initialTenureSpaceSize = OMR_MAX(initialTenureSpaceSize, extensions->regionSize * contextCount);

	MM_Configuration::prepareParameters(omrVM, minimumSpaceSize, minimumNewSpaceSize, initialNewSpaceSize, maximumNewSpaceSize,
										 minimumTenureSpaceSize, initialTenureSpaceSize, maximumTenureSpaceSize,
										 memoryMax, tenureFlags, parameters);
}

bool
MM_ConfigurationIncrementalGenerational::verifyRegionSize(MM_EnvironmentBase *env, UDATA regionSize)
{
	return regionSize >= TAROK_MINIMUM_REGION_SIZE_BYTES;
}

bool
MM_ConfigurationIncrementalGenerational::initializeNUMAManager(MM_EnvironmentBase *env)
{
	MM_GCExtensionsBase *extensions = env->getExtensions();
	bool disabledByForce = extensions->numaForced && !extensions->_numaManager.isPhysicalNUMAEnabled();
	if (!disabledByForce) {
		extensions->_numaManager.shouldEnablePhysicalNUMA(true);
	}
	bool result = MM_Configuration::initializeNUMAManager(env);

	if (result && !disabledByForce) {
		/* shut off NUMA (even if enabled by force) if we are using too small a heap */
		UDATA affinityLeaderCount = 0;
		extensions->_numaManager.getAffinityLeaders(&affinityLeaderCount);
		if ((1 + affinityLeaderCount) != MM_GlobalAllocationManagerTarok::calculateIdealManagedContextCount(extensions)) {
			extensions->_numaManager.shouldEnablePhysicalNUMA(false);
			result = extensions->_numaManager.recacheNUMASupport(static_cast<MM_EnvironmentBase *>(env));
			/* startup can't fail if NUMAManager disabled */
			Assert_MM_true(result);
		}
	}
	
	return result;
}

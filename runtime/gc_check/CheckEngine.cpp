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
 * @ingroup GC_CheckEngine
 */

#include "j9.h"
#include "j9cfg.h"
#include "HeapIteratorAPI.h"
#include "mmhook_internal.h"

#include "VMHelpers.hpp"

#if !defined(UT_TRACE_OVERHEAD)
#define UT_TRACE_OVERHEAD -1 /* disable assertions and tracepoints since we're not in the GC module proper */
#endif

#include "ArrayletLeafIterator.hpp"
#include "CheckEngine.hpp"
#include "Base.hpp"
#include "CheckBase.hpp"
#include "CheckCycle.hpp"
#include "CheckError.hpp"
#include "CheckReporter.hpp"
#include "CheckReporterTTY.hpp"
#include "ClassModel.hpp"
#include "ForwardedHeader.hpp"
#include "GCExtensions.hpp"
#include "HeapRegionDescriptor.hpp"
#include "ModronTypes.hpp"
#include "ObjectModel.hpp"
#include "ObjectAccessBarrier.hpp"
#include "ScanFormatter.hpp"
#include "SublistPool.hpp"
#include "SublistPuddle.hpp"

/**
 * Private struct used as the user data for object slot iterator callbacks.
 */
typedef struct ObjectSlotIteratorCallbackUserData {
	GC_CheckEngine* engine; /* Input */
	J9MM_IterateRegionDescriptor* regionDesc; /* Input */
	UDATA result; /* Output */
} ObjectSlotIteratorCallbackUserData;

static jvmtiIterationControl check_objectSlotsCallback(J9JavaVM *javaVM, J9MM_IterateObjectDescriptor *objectDesc, J9MM_IterateObjectRefDescriptor *refDesc, void *userData);
static bool isPointerInRegion(void *pointer, J9MM_IterateRegionDescriptor *regionDesc);

/*
 * Define alignment masks for J9Objects:
 *
 * o On 64 bit all objects are 8 byte aligned
 * o On 31/32 bit:
 * 		- Objects allocated on stack are 4 bytes aligned
 * 		- Objects allocated on heap are either 4 or 8 byte aligned dependent
 * 		  on platform
 */
#define J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK 0xFF

#if defined(J9VM_ENV_DATA64)
#define J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK 0x7
#else /* J9VM_ENV_DATA64 */
#define J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK 0x3
#endif /* J9VM_ENV_DATA64*/

#define J9MODRON_GCCHK_J9CLASS_EYECATCHER (UDATA)0x99669966

/**
 * Clear the counts of OwnableSynchronizerObjects
 */
void
GC_CheckEngine::clearCountsForOwnableSynchronizerObjects()
{
	_ownableSynchronizerObjectCountOnList = UNINITIALIZED_SIZE_FOR_OWNABLESYNCHRONIER;
	_ownableSynchronizerObjectCountOnHeap = UNINITIALIZED_SIZE_FOR_OWNABLESYNCHRONIER;
}

/**
 *
 */
bool
GC_CheckEngine::verifyOwnableSynchronizerObjectCounts()
{
	bool ret = true;

	if ((UNINITIALIZED_SIZE_FOR_OWNABLESYNCHRONIER != _ownableSynchronizerObjectCountOnList) && (UNINITIALIZED_SIZE_FOR_OWNABLESYNCHRONIER != _ownableSynchronizerObjectCountOnHeap)) {
		if (_ownableSynchronizerObjectCountOnList != _ownableSynchronizerObjectCountOnHeap) {
			PORT_ACCESS_FROM_PORT(_portLibrary);
			j9tty_printf(PORTLIB, "  <gc check: found count=%zu of OwnableSynchronizerObjects on Heap doesn't match count=%zu on lists>\n", _ownableSynchronizerObjectCountOnHeap, _ownableSynchronizerObjectCountOnList);
			ret = false;
		}
	}

	return ret;
}


/**
 * Clear the cache of previously visited objects.
 */
void
GC_CheckEngine::clearPreviousObjects()
{
	_lastHeapObject1.type = GC_CheckElement::type_none;
	_lastHeapObject2.type = GC_CheckElement::type_none;
	_lastHeapObject3.type = GC_CheckElement::type_none;
}

/**
 * Push an object onto the list of previously visited objects
 * (and drop oldest object).
 */
void
GC_CheckEngine::pushPreviousObject(J9Object *objectPtr)
{
	_lastHeapObject3 = _lastHeapObject2;
	_lastHeapObject2 = _lastHeapObject1;

	_lastHeapObject1.type = GC_CheckElement::type_object;
	_lastHeapObject1.u.object = objectPtr;
}

/**
 * Push a class onto the list of previously visited objects
 * (and drop oldest object).
 */
void
GC_CheckEngine::pushPreviousClass(J9Class* clazz)
{
	_lastHeapObject3 = _lastHeapObject2;
	_lastHeapObject2 = _lastHeapObject1;

	_lastHeapObject1.type = GC_CheckElement::type_class;
	_lastHeapObject1.u.clazz = clazz;
}


/**
 * Determine whether or not the given object is contained in the given segment
 *
 * @return true if objectPtr is contained in the given segment
 * @return false otherwise
 */
bool
GC_CheckEngine::isPointerInSegment(void *pointer, J9MemorySegment *segment)
{
	return ( ((UDATA)pointer >= (UDATA)segment->heapBase) && ((UDATA)pointer < (UDATA)segment->heapAlloc) );
}

/**
 * Determine whether or not the given object is contained in the given stack
 *
 * @return true if objectPtr is contained in the given stack
 * @return false otherwise
 */
bool
GC_CheckEngine::isObjectOnStack(J9Object *objectPtr, J9JavaStack *stack)
{
	return ((UDATA)objectPtr < (UDATA)stack->end) && ((UDATA)objectPtr >= (UDATA)(stack+1));
}

/**
 * Find the class segment which contains a given class.
 *
 * @param clazz Pointer to the class to search for
 *
 * @return the segment which contains the class, or
 * NULL if the class was not found in any of the class segments
 */
J9MemorySegment*
GC_CheckEngine::findSegmentForClass(J9JavaVM *javaVM, J9Class *clazz)
{
	J9MemorySegmentList * segmentList = javaVM->classMemorySegments;
	J9MemorySegment * segment = (J9MemorySegment *) avl_search(&segmentList->avlTreeData, (UDATA)clazz);
	if (NULL != segment) {
		UDATA flags = MEMORY_TYPE_RAM_CLASS | MEMORY_TYPE_UNDEAD_CLASS;
		if (0 != (segment->type & flags)) {
			return segment;
		}
	}
	return NULL;
}

/**
 * Find the region which contains a given object. Return true and write to regionDescOutput if
 * the region is found, return false otherwise.
 */
bool
GC_CheckEngine::findRegionForPointer(J9JavaVM* javaVM, void *pointer, J9MM_IterateRegionDescriptor* regionDescOutput)
{
	bool found = false;
	
	/* Check the local cache of the previously found region */
	if (isPointerInRegion(pointer, &_regionDesc)) {
		copyRegionDescription(&_regionDesc, regionDescOutput);
		found = true;
	} else if (0 != javaVM->memoryManagerFunctions->j9mm_find_region_for_pointer(javaVM, pointer, regionDescOutput)) {
		/* If a region is found cache it in _regionDesc for the next lookup */
		copyRegionDescription(regionDescOutput, &_regionDesc);
		found = true;
	}

	return found;
}

/**
 * Perform basic verification on an object pointer.
 * Checks a pointer for alignment, and optionally searches memory segments for
 * this pointer.
 *
 * @param objectPtr[in] Pointer to the object to be verified
 * @param newObjectPtr[out] On return, contains the actual object pointer once forwarding pointers have been followed
 * @param regionDesc[out] A pointer to a region structure describing the region of objectPtr. The region will be 
 * searched for, and the result stored back to the location pointed to by regionDesc
 *
 * @note regionDesc must not be NULL
 * @see @ref findRegionForPointer
 *
 * @return @ref GCCheckWalkStageErrorCodes
 */
UDATA
GC_CheckEngine::checkJ9ObjectPointer(J9JavaVM *javaVM, J9Object *objectPtr, J9Object **newObjectPtr, J9MM_IterateRegionDescriptor *regionDesc)
{
	MM_GCExtensionsBase* extensions = MM_GCExtensions::getExtensions(_javaVM);
	assume0(regionDesc != NULL);

	*newObjectPtr = objectPtr;
	
	if (NULL == objectPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	bool foundRegion = findRegionForPointer(javaVM, objectPtr, regionDesc);
	if (!foundRegion) {
		/* Is the object on the stack? */
		J9VMThread *vmThread;
		GC_VMThreadListIterator threadListIterator(javaVM);
		while ((vmThread = threadListIterator.nextVMThread()) != NULL) {
			if (isObjectOnStack(objectPtr, vmThread->stackObject)) {
				return J9MODRON_GCCHK_RC_STACK_OBJECT;
			}
		}

		if (J9MODRON_GCCHK_J9CLASS_EYECATCHER == J9GC_J9OBJECT_CLAZZ_WITH_FLAGS_VM(objectPtr, javaVM)) {
			return J9MODRON_GCCHK_RC_OBJECT_SLOT_POINTS_TO_J9CLASS;
		}

		return J9MODRON_GCCHK_RC_NOT_FOUND;
	}
	
	if (0 == regionDesc->objectAlignment) {
		/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
		return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
	}

	/* Now we know object is not on stack we can check that it's correctly aligned
	 * for a J9Object.
	 */
	if ((UDATA)(objectPtr) & (regionDesc->objectAlignment - 1)) {
		return J9MODRON_GCCHK_RC_UNALIGNED;
	}

	if (_cycle->getMiscFlags() & J9MODRON_GCCHK_MISC_MIDSCAVENGE) {
		UDATA regionType = ((MM_HeapRegionDescriptor*)regionDesc->id)->getTypeFlags();
		if ((regionType & MEMORY_TYPE_NEW) || extensions->isVLHGC()) {
			// TODO: ideally, we should only check this in the evacuate segment
			// TODO: do some safety checks first -- is there enough room in the segment?
			MM_ForwardedHeader forwardedHeader(objectPtr, extensions->compressObjectReferences());
			if (forwardedHeader.isForwardedPointer()) {
				*newObjectPtr = forwardedHeader.getForwardedObject();
				
				if (_cycle->getMiscFlags() & J9MODRON_GCCHK_VERBOSE) {
					PORT_ACCESS_FROM_PORT(_portLibrary);
					j9tty_printf(PORTLIB, "  <gc check: found forwarded pointer %p -> %p>\n", objectPtr, *newObjectPtr);	
				}
				
				objectPtr = *newObjectPtr;
				if (!findRegionForPointer(javaVM, objectPtr, regionDesc)) {
					return J9MODRON_GCCHK_RC_NOT_FOUND;
				}
		
				if (0 == regionDesc->objectAlignment) {
					/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
					return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
				}
				
				/* make sure the forwarded pointer is also aligned */
				if ((UDATA)(objectPtr) & (regionDesc->objectAlignment - 1)) {
					return J9MODRON_GCCHK_RC_UNALIGNED;
				}
			}
		}
	}
	
	/* Check that elements of a double array are aligned on an 8-byte boundary.  For continuous
	 * arrays, verifying that the J9Indexable object is aligned on an 8-byte boundary is sufficient.
	 * For arraylets, depending on the layout, elements of the array may be stored on arraylet leafs
	 * or on the spine.  Arraylet leafs should always be aligned on 8-byte boundaries.  Checking both 
	 * the first and last element will ensure that we are always checking that elements are aligned 
	 * on the spine.
	 *  */ 
	if (extensions->objectModel.isDoubleArray(objectPtr)) {
		j9array_t array = (j9array_t)objectPtr;
		UDATA size = extensions->indexableObjectModel.getSizeInElements(array);
		U_64* elementPtr = NULL;
		
		if (0 != size) {
			J9VMThread *vmThread = javaVM->internalVMFunctions->currentVMThread(javaVM);
			elementPtr = J9JAVAARRAY_EA(vmThread, array, 0, U_64);
			if ((UDATA)elementPtr & (sizeof(U_64)-1)) {
				return J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED;
			}
			
			elementPtr = J9JAVAARRAY_EA(vmThread, array, size-1, U_64);
			if ((UDATA)elementPtr & (sizeof(U_64)-1)) {
				return J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED;
			}
		}
	}
	
	/* TODO: shouldn't the segment be checked for the objectPtr if segment was not NULL? */
	return J9MODRON_GCCHK_RC_OK;
}

/**
 * Perform basic verification on a class pointer.
 * Calls @ref checkPointer to perform the most basic checks (alignment, is
 * contained in a class segment), then performs some checks specific to a
 * class pointer.
 *
 * @param clazz Pointer to the class
 * @param allowUndead true if undead classes are allowed in this context
 *
 * @return @ref GCCheckWalkStageErrorCodes
 * @see @ref checkFlags
 * @see @ref findSegmentForClass
 */
UDATA
GC_CheckEngine::checkJ9ClassPointer(J9JavaVM *javaVM, J9Class *clazz, bool allowUndead )
{
	/* Short circuit if we've recently checked this class. 
	 * In JLTF, this cache is about 94% effective (when its size is 19).
	 */
	UDATA cacheIndex = ((UDATA)clazz) % CLASS_CACHE_SIZE;
	if (allowUndead && (_checkedClassCacheAllowUndead[cacheIndex] == clazz)) {
		return J9MODRON_GCCHK_RC_OK;
	} else if (_checkedClassCache[cacheIndex] == clazz) {
		return J9MODRON_GCCHK_RC_OK;
	}
	
	/* Verify class ptr in known segment.
	 * As it's a J9Class ptr we only search class segments
	 */
	if (NULL == clazz) {
		return J9MODRON_GCCHK_RC_NULL_CLASS_POINTER;
	}

	if ((UDATA)(clazz) & J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK) {
		return J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED;
	}

	J9MemorySegment *segment = findSegmentForClass(javaVM, clazz);
	if (segment == NULL) {
		return J9MODRON_GCCHK_RC_CLASS_NOT_FOUND;
	} else if (!allowUndead && (0 != (segment->type & MEMORY_TYPE_UNDEAD_CLASS))) {
		return J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD;
	}

	/* Check to ensure J9Class header has the correct eyecatcher.
	 */
	UDATA result = checkJ9ClassHeader(javaVM, clazz);
	if ( J9MODRON_GCCHK_RC_OK != result) {
		return result;
	}

	/* Check that class is not unloaded */
	result = checkJ9ClassIsNotUnloaded(javaVM, clazz);
	if ( J9MODRON_GCCHK_RC_OK != result) {
		return result;
	}

	if (_cycle->getCheckFlags() & J9MODRON_GCCHK_VERIFY_RANGE) {
		UDATA delta = (UDATA)segment->heapAlloc - (UDATA)clazz;

		/* Basic check that there is enough room for the class header */
		if (delta < sizeof(J9Class)) {
			return J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE;
		}
	}
	
	/* class checked out. Record it in the cache so we don't need to check it again. */
	if (allowUndead) {
		_checkedClassCacheAllowUndead[cacheIndex] = clazz;
	} else {
		_checkedClassCache[cacheIndex] = clazz;
	}
	
	return J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify the integrity of an object on the heap.
 * Checks various aspects of object integrity based on the checkFlags.
 *
 * @param objectPtr Pointer to the object
 * @param segment The segment containing the pointer
 * @param checkFlags Type/level of verification
 *
 * @return @ref GCCheckWalkStageErrorCodes
 *
 * @see @ref checkFlags
 */
UDATA
GC_CheckEngine::checkJ9Object(J9JavaVM *javaVM, J9Object* objectPtr, J9MM_IterateRegionDescriptor *regionDesc, UDATA checkFlags)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(javaVM);

	assume0(NULL != regionDesc);

	if (NULL == objectPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	if (0 == regionDesc->objectAlignment) {
		/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
		return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
	}
	
	if (((UDATA)objectPtr) & (regionDesc->objectAlignment - 1)) {
		return J9MODRON_GCCHK_RC_UNALIGNED;
	}

	if (checkFlags & J9MODRON_GCCHK_VERIFY_CLASS_SLOT) {
		/* Check that the class pointer points to the class heap, etc. */
		UDATA ret = checkJ9ClassPointer(javaVM, J9GC_J9OBJECT_CLAZZ_VM(objectPtr, javaVM), true);

		if (J9MODRON_GCCHK_RC_OK != ret) {
			return ret;
		}
	}

#if defined(J9VM_ARCH_X86)
	if (extensions->objectModel.isIndexable(objectPtr)) {
		/* Check that the indexable object has the correct data address pointer */
		if (extensions->indexableObjectModel.isInlineContiguousArraylet((J9IndexableObject*)objectPtr)) {
			if(extensions->indexableObjectModel.isCorrectDataAddrForContiguousArraylet((J9IndexableObject*)objectPtr)) {
				return J9MODRON_GCCHK_RC_INVALID_CONTIGUOUS_DATA_ADDRESS;
			}
		} else {
			if(extensions->indexableObjectModel.isCorrectDataAddrForDiscontiguousArraylet((J9IndexableObject*)objectPtr)) {
				return J9MODRON_GCCHK_RC_INVALID_DISCONTIGUOUS_DATA_ADDRESS;
			}
		}
	}
#endif /*(J9VM_ARCH_X86) */

	if (checkFlags & J9MODRON_GCCHK_VERIFY_RANGE) {
		UDATA regionEnd = ((UDATA)regionDesc->regionStart) + regionDesc->regionSize;
		UDATA delta = regionEnd - (UDATA)objectPtr;
		J9MM_IterateObjectDescriptor objectDesc;

		/* Basic check that there is enough room for the object header */
		if (delta < J9JAVAVM_OBJECT_HEADER_SIZE(javaVM)) {
			return J9MODRON_GCCHK_RC_INVALID_RANGE;
		}

		/* TODO: find out what the indexable header size should really be */
		if (extensions->objectModel.isIndexable(objectPtr) && (delta < J9JAVAVM_CONTIGUOUS_HEADER_SIZE(javaVM))) {
			return J9MODRON_GCCHK_RC_INVALID_RANGE;
		}

		javaVM->memoryManagerFunctions->j9mm_initialize_object_descriptor(javaVM, &objectDesc, objectPtr);

		if (delta < objectDesc.size) {
			return J9MODRON_GCCHK_RC_INVALID_RANGE;
		}
	}

	if (checkFlags & J9MODRON_GCCHK_VERIFY_FLAGS) {
		if (!extensions->objectModel.checkIndexableFlag(objectPtr)) {
			return J9MODRON_GCCHK_RC_INVALID_FLAGS;
		}

		if (extensions->isStandardGC()) {
#if defined(J9VM_GC_GENERATIONAL)
			UDATA regionType = ((MM_HeapRegionDescriptor*)regionDesc->id)->getTypeFlags();
			if (regionType & MEMORY_TYPE_OLD) {
				/* All objects in an old segment must have old bit set */
				if (!extensions->isOld(objectPtr)) {
					return J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS;
				}
			} else {
				if (regionType & MEMORY_TYPE_NEW) {
					/* Object in a new segment can't have old bit or remembered bit set */
					if (extensions->isOld(objectPtr)) {
						return J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS;
					}
				}
			}
#endif /* J9VM_GC_GENERATIONAL */
		}
	}

	return J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify the integrity of an J9Class object.
 * Checks various aspects of object integrity based on the checkFlags.
 *
 * @param clazzPtr Pointer to the J9Class object
 * @param segment The segment containing the pointer
 * @param checkFlags Type/level of verification
 *
 * @return @ref GCCheckWalkStageErrorCodes
 *
 * @see @ref checkFlags
 */
UDATA
GC_CheckEngine::checkJ9Class(J9JavaVM *javaVM, J9Class *clazzPtr, J9MemorySegment *segment, UDATA checkFlags)
{
	if (NULL == clazzPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	if (((UDATA)clazzPtr) & J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK) {
		return J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED;
	}

	/* Check that the class header contains the expected values */
	UDATA ret = checkJ9ClassHeader(javaVM, clazzPtr);
	if (J9MODRON_GCCHK_RC_OK != ret) {
		return ret;
	}

	/* Check that class is not unloaded */
	ret = checkJ9ClassIsNotUnloaded(javaVM, clazzPtr);
	if (J9MODRON_GCCHK_RC_OK != ret) {
		return ret;
	}

	if (checkFlags & J9MODRON_GCCHK_VERIFY_RANGE) {
		UDATA delta = (UDATA)segment->heapAlloc - (UDATA)clazzPtr;

		/* Basic check that there is enough room for the object header */
		if (delta < sizeof(J9Class)) {
			return J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE;
		}
	}

	return J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify J9Class header.
 * Checks various aspects of object integrity based on the checkFlags.
 *
 * @param clazz Pointer to the J9Class object
 *
 * @return @ref GCCheckWalkStageErrorCodes
 */
UDATA
GC_CheckEngine::checkJ9ClassHeader(J9JavaVM *javaVM, J9Class *clazz)
{
	/* Check to ensure J9Class header has the correct eyecatcher.
	 */
	if (clazz->eyecatcher != J9MODRON_GCCHK_J9CLASS_EYECATCHER) {
		return  J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID;
	}
	return  J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify J9Class is not unloaded.
 *
 * @param clazz Pointer to the J9Class object
 *
 * @return @ref GCCheckWalkStageErrorCodes
 */
UDATA
GC_CheckEngine::checkJ9ClassIsNotUnloaded(J9JavaVM *javaVM, J9Class *clazz)
{
	/* Check to ensure J9Class header has the correct eyecatcher.
	 */
	if (0 != (clazz->classDepthAndFlags & J9AccClassDying)) {
		return  J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED;
	}
	return  J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify a stack object.
 * Checks various aspects of object integrity based on the checkFlags.
 *
 * @param objectPtr Pointer to the object
 * @param checkFlags Type/level of verification
 *
 * @return @ref GCCheckWalkStageErrorCodes
 * @see @ref checkFlags
 *
 * @todo Fix checks for flags that are automatically set by the JIT
 */
UDATA
GC_CheckEngine::checkStackObject(J9JavaVM *javaVM, J9Object *objectPtr)
{
	MM_GCExtensionsBase * extensions = MM_GCExtensions::getExtensions(javaVM);

	if (NULL == objectPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	if ((UDATA)(objectPtr) & J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK) {
		return J9MODRON_GCCHK_RC_UNALIGNED;
	}

	if (_cycle->getCheckFlags() & J9MODRON_GCCHK_VERIFY_CLASS_SLOT) {
		/* Check that the class pointer points to the class heap, etc. */
		UDATA ret = checkJ9ClassPointer(javaVM, J9GC_J9OBJECT_CLAZZ_VM(objectPtr, javaVM));
		if (J9MODRON_GCCHK_RC_OK != ret) {
			return ret;
		}
	}

	if (_cycle->getCheckFlags() & J9MODRON_GCCHK_VERIFY_FLAGS) {

		if (!extensions->objectModel.checkIndexableFlag(objectPtr)) {
			return J9MODRON_GCCHK_RC_INVALID_FLAGS;
		}
	}

	return J9MODRON_GCCHK_RC_OK;
}

/**
 * Verify an object pointer.
 * Verify that the given pointer appears to be a valid heap pointer, and
 * if so, proceed to verify the object that it points to.
 * Calls checkJ9Object which does the actual checking.
 *
 * @param javaVM the J9JavaVM struct
 * @param objectPtr the object to check
 * @return @ref GCCheckWalkStageErrorCodes
 */
UDATA
GC_CheckEngine::checkObjectIndirect(J9JavaVM *javaVM, J9Object *objectPtr)
{
	if (NULL == objectPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	/* Short circuit if we've recently checked this object. 
	 * In JLTF, this cache is about 28% effective (size 19), 38% effective (size 61), or 49% effective (size 1021).
	 */
	UDATA cacheIndex = ((UDATA)objectPtr) % OBJECT_CACHE_SIZE;
	if (_checkedObjectCache[cacheIndex] == objectPtr) {
		return J9MODRON_GCCHK_RC_OK;
	}

	/* Check if reference to J9Object */
	J9Object *newObjectPtr = NULL;
	J9MM_IterateRegionDescriptor objectRegion; /* initialized by checkJ9ObjectPointer */
	UDATA result = checkJ9ObjectPointer(javaVM, objectPtr, &newObjectPtr, &objectRegion);

	if (J9MODRON_GCCHK_RC_OK == result) {
		result = checkJ9Object(javaVM, newObjectPtr, &objectRegion, _cycle->getCheckFlags());
	}
	
	if (J9MODRON_GCCHK_RC_OK == result) {
		/* Object is OK. Record it in the cache so we can short circuit if we see it again */
		_checkedObjectCache[cacheIndex] = objectPtr;
	}

	return result;
}

/**
 * Verify a class.
 *
 * @param clazz the class to be verified
 * @param segment the class memory segment which contains the class
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkClassHeap(J9JavaVM *javaVM, J9Class *clazz, J9MemorySegment *segment)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(javaVM);
	UDATA result;
	volatile j9object_t *slotPtr;

	/*
	 * Verify that this is, in fact, a class
	 */
	result = checkJ9Class(javaVM, clazz, segment, _cycle->getCheckFlags());
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(clazz, _cycle, _currentCheck, "Class ", result, _cycle->nextErrorCount());
		_reporter->report(&error);
	}

	/*
	 * Process object slots in the class
	 */
	GC_ClassIterator classIterator(extensions, clazz);
	while((slotPtr = classIterator.nextSlot()) != NULL) {
		int state = classIterator.getState();

		J9Object *objectPtr = *slotPtr;

		result = checkObjectIndirect(javaVM, objectPtr);

		if (J9MODRON_GCCHK_RC_OK != result) {
			const char *elementName = "";

			switch (state) {
			case classiterator_state_statics:
				elementName = "static "; break;
			case classiterator_state_constant_pool:
				elementName = "constant "; break;
			case classiterator_state_slots:
				elementName = "slots "; break;
			case classiterator_state_callsites:
				elementName = "callsite "; break;
			}
			GC_CheckError error(clazz, (void*)slotPtr, _cycle, _currentCheck, elementName, result, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
		if (extensions->isStandardGC()) {
#if defined(J9VM_GC_GENERATIONAL)
			/* If the slot has its old bit OFF, the class's remembered bit should be ON */
			if (objectPtr && !extensions->isOld(objectPtr)) {
				if (!extensions->objectModel.isRemembered((J9Object*)clazz->classObject)) {
					GC_CheckError error(clazz, (void*)slotPtr, _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT, _cycle->nextErrorCount());
					_reporter->report(&error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}
			}
#endif /* J9VM_GC_GENERATIONAL */
		}
	}

	if (J9MODRON_GCCHK_RC_OK != checkClassStatics(javaVM, clazz)) {
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	J9Class* replaced = clazz->replacedClass;
	if (NULL != replaced) {
		/* if class replaces another class the replaced class must have J9AccClassHotSwappedOut flag set */
		if (0 == (J9CLASS_FLAGS(replaced) & J9AccClassHotSwappedOut)) {
			GC_CheckError error(clazz, (void*)&(clazz->replacedClass), _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_REPLACED_CLASS_HAS_NO_HOTSWAP_FLAG, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
	}

	/*
	 * Process class slots in the class
	 */
	GC_ClassIteratorClassSlots classIteratorClassSlots(javaVM, clazz);
	J9Class *classPtr;
	while (NULL != (classPtr = classIteratorClassSlots.nextSlot())) {
		int state = classIteratorClassSlots.getState();
		const char *elementName = "";

		result = J9MODRON_GCCHK_RC_OK;

		switch (state) {
			case classiteratorclassslots_state_constant_pool:
				result = checkJ9ClassPointer(javaVM, classPtr);
				elementName = "constant ";
				break;
			case classiteratorclassslots_state_superclasses:
				result = checkJ9ClassPointer(javaVM, classPtr);
				elementName = "superclass ";
				break;
			case classiteratorclassslots_state_interfaces:
				result = checkJ9ClassPointer(javaVM, classPtr);
				elementName = "interface ";
				break;
			case classiteratorclassslots_state_array_class_slots:
				result = checkJ9ClassPointer(javaVM, classPtr);
				elementName = "array class ";
				break;
			case classiteratorclassslots_state_flattened_class_cache_slots:
				result = checkJ9ClassPointer(javaVM, classPtr);
				elementName = "flattened class cache ";
				break;
		}

		if (J9MODRON_GCCHK_RC_OK != result) {
			GC_CheckError error(clazz, &classPtr, _cycle, _currentCheck, elementName, result, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
	}

	return J9MODRON_SLOT_ITERATOR_OK;
}

UDATA
GC_CheckEngine::checkClassStatics(J9JavaVM* vm, J9Class* clazz)
{
	UDATA result = J9MODRON_GCCHK_RC_OK;
	bool validationRequired = true;

	if (0 != (J9CLASS_FLAGS(clazz) & J9AccClassHotSwappedOut)) {
		/* if class has been hot swapped (J9AccClassHotSwappedOut bit is set) in Fast HCR,
		 * the ramStatics of the existing class may be reused.  The J9ClassReusedStatics
		 * bit in J9Class->classFlags will be set if that's the case.
		 * In Extended HCR mode ramStatics might be not NULL and must be valid
		 * NOTE: If class is hot swapped and the value in ramStatics is NULL it is valid 
		 * to have the correspondent ROM Class value in objectStaticCount field greater then 0
		 */
		if (J9GC_CLASS_IS_ARRAY(clazz)) {
			/* j9arrayclass should not be hot swapped */
			result = J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_FOR_ARRAY;
			GC_CheckError error(clazz, _cycle, _currentCheck, "Class ", result, _cycle->nextErrorCount());
			_reporter->report(&error);
			validationRequired = false;
		}

		if (areExtensionsEnabled(vm)) {
			/* This is Extended HSR mode so hot swapped class might have NULL in ramStatics field */
			if (NULL == clazz->ramStatics) {
				validationRequired = false;
			}
		}
		if (J9ClassReusedStatics == (J9CLASS_EXTENDED_FLAGS(clazz) & J9ClassReusedStatics)) {
			/* This case can also occur when running -Xint with extensions enabled */
			validationRequired = false;
		}
	}

	if (validationRequired) {
		J9VMThread* currentThread = vm->internalVMFunctions->currentVMThread(vm);
		J9ClassLoader* classLoader = clazz->classLoader;
		J9ROMClass *romClazz = clazz->romClass;
		J9ROMFieldWalkState state;

		UDATA numberOfReferences = 0;
		j9object_t *sectionStart = NULL;
		j9object_t *sectionEnd = NULL;

		/*
		 * Note: we have no special recognition for J9ArrayClass here
		 * J9ArrayClass does not have a ramStatics field but something else at this place
		 * so direct check (NULL != clazz->ramStatics) would not be correct,
		 * however romClazz->objectStaticCount must be 0 for this case
		 */
		if (0 != romClazz->objectStaticCount) {
			sectionStart = (j9object_t *)clazz->ramStatics;
			sectionEnd = sectionStart + romClazz->objectStaticCount;
		}

		J9ROMFieldShape* romFieldCursor = romFieldsStartDo(romClazz, &state);

		/* Iterate all fields of ROM Class looking to statics fields pointed to java objects */
		while (NULL != romFieldCursor) {
			/* interested in static fields only */
			if (romFieldCursor->modifiers & J9AccStatic) {
				J9UTF8* nameUTF = J9ROMFIELDSHAPE_NAME(romFieldCursor);
				J9UTF8* sigUTF = J9ROMFIELDSHAPE_SIGNATURE(romFieldCursor);

				/* interested in objects and all kinds of arrays */
				if (('L' == J9UTF8_DATA(sigUTF)[0]) || ('[' == J9UTF8_DATA(sigUTF)[0])) {
					numberOfReferences += 1;

					/* get address of next field */
					j9object_t* address = (j9object_t*)vm->internalVMFunctions->staticFieldAddress(currentThread, clazz, J9UTF8_DATA(nameUTF), J9UTF8_LENGTH(nameUTF), J9UTF8_DATA(sigUTF), J9UTF8_LENGTH(sigUTF), NULL, NULL, J9_LOOK_NO_JAVA, NULL);

					/* an address must be in gc scan range */
					if (!((address >= sectionStart) && (address < sectionEnd))) {
						result = J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE;
						GC_CheckError error(clazz, address, _cycle, _currentCheck, "Class ", result, _cycle->nextErrorCount());
						_reporter->report(&error);
					}

					/* check only if we have an object */
					if (NULL != *address) {
						U_8* toSearchString = J9UTF8_DATA(sigUTF);
						UDATA toSearchLength = J9UTF8_LENGTH(sigUTF);

						if ('L' == J9UTF8_DATA(sigUTF)[0]) {
							/*  Convert signature to class name:
							 *  Entering 'L' as well as closing ';' must be removed to get a proper class name
							 */
							toSearchString += 1;
							toSearchLength -= 2;
						}

						J9Class* classToCast = vm->internalVMFunctions->internalFindClassUTF8(currentThread, toSearchString, toSearchLength, classLoader, J9_FINDCLASS_FLAG_EXISTING_ONLY);

						/*
						 * If class with name extracted from RAM Static field's signature is not found in current classloader (classToCast == NULL)
						 * unfortunately it is no way to find it. Skip check for this case.
						 * However if class found - it must fit object's class
						 */
						if (NULL != classToCast) {
							if (0 == instanceOfOrCheckCast(J9GC_J9OBJECT_CLAZZ_VM(*address, vm), classToCast)) {
								result = J9MODRON_GCCHK_RC_CLASS_STATICS_FIELD_POINTS_WRONG_OBJECT;
								GC_CheckError error(clazz, address, _cycle, _currentCheck, "Class ", result, _cycle->nextErrorCount());
								_reporter->report(&error);
							}
						}
					}
				}
			}
			romFieldCursor = romFieldsNextDo(&state);
		}

		if (numberOfReferences != romClazz->objectStaticCount) {
			result = J9MODRON_GCCHK_RC_CLASS_STATICS_WRONG_NUMBER_OF_REFERENCES;
			GC_CheckError error(clazz, _cycle, _currentCheck, "Class ", result, _cycle->nextErrorCount());
			_reporter->report(&error);
		}
	}

	return result;
}

/**
 * Verify a slot (double-indirect object pointer) in the object heap.
 *
 * @param objectIndirect the slot to be verified
 * @param segment the segment containing the object whose slot is being verified
 * @param objectIndirectBase the object whose slot is being verified
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotObjectHeap(J9JavaVM *javaVM, J9Object *objectPtr, fj9object_t *objectIndirect, J9MM_IterateRegionDescriptor *regionDesc, J9Object *objectIndirectBase)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(javaVM);

	if (NULL == objectPtr) {
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	
	/* might the heap include dark matter? If so, ignore most errors */
	if (_cycle->getMiscFlags() & J9MODRON_GCCHK_MISC_DARKMATTER) {
		/* only report a subset of errors -- the rest are expected to be found in dark matter */
		switch (result) {
		case J9MODRON_GCCHK_RC_OK:
		case J9MODRON_GCCHK_RC_UNALIGNED:
		case J9MODRON_GCCHK_RC_STACK_OBJECT:
			break;
		
		/* These errors are unlikely, but not impossible to find in dark matter. 
		 * Leave them enabled because they can help find real corruption
		 */
		case J9MODRON_GCCHK_RC_NOT_FOUND: /* can happen due to contraction */
			break;
			
		/* other errors in possible dark matter are expected, so ignore them and don't 
		 * investigate this pointer any further 
		 */
		default:
			return J9MODRON_SLOT_ITERATOR_OK;
		}
	}
	
	if (J9MODRON_GCCHK_RC_OK != result) {
		const char *elementName = extensions->objectModel.isIndexable(objectIndirectBase) ? "IObject " : "Object ";
		GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, (char *)elementName, result, _cycle->nextErrorCount());
		_reporter->report(&error);
		return J9MODRON_SLOT_ITERATOR_OK;
	}

#if defined(J9VM_GC_GENERATIONAL)
	if (MM_GCExtensions::getExtensions(javaVM)->scavengerEnabled) {
		/* Old objects with references to newspace should have their remembered bit ON */
		/* TODO: add a quick check to see if the object is in same region as the referring object? */
		J9MM_IterateRegionDescriptor objectRegion;
		if (!findRegionForPointer(javaVM, objectPtr, &objectRegion)) {
			/* should be impossible, since checkObjectIndirect() already verified that the object exists */
			const char *elementName = extensions->objectModel.isIndexable(objectIndirectBase) ? "IObject " : "Object ";
			GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, (char *)elementName, J9MODRON_GCCHK_RC_NOT_FOUND, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
		UDATA regionType = ((MM_HeapRegionDescriptor*)regionDesc->id)->getTypeFlags();
		UDATA objectRegionType = ((MM_HeapRegionDescriptor*)objectRegion.id)->getTypeFlags();

		if (objectPtr && (regionType & MEMORY_TYPE_OLD) && (objectRegionType & MEMORY_TYPE_NEW) && !extensions->objectModel.isRemembered(objectIndirectBase)) {
			const char *elementName = extensions->objectModel.isIndexable(objectIndirectBase) ? "IObject " : "Object ";
			GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, (char *)elementName, J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}

		/* Old objects that point to objects with old bit OFF should have remembered bit ON */
		if (objectPtr && (regionType & MEMORY_TYPE_OLD) && !extensions->isOld(objectPtr) && !extensions->objectModel.isRemembered(objectIndirectBase)) {
			const char *elementName = extensions->objectModel.isIndexable(objectIndirectBase) ? "IObject " : "Object ";
			GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, (char *)elementName, J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
	}
#endif /* J9VM_GC_GENERATIONAL */

	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Verify an object that was encountered directly while walking the heap.
 * Perform some basic checks on the object by calling @ref checkJ9Object,
 * and if the object appears to be valid, walk its slots and verify each
 * of them. We must be very careful here to avoid crashing gccheck itself.
 *
 * @param object the object to verify
 * @regionDesc the region which contains the object
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK on success, #J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR on failure
 */
UDATA
GC_CheckEngine::checkObjectHeap(J9JavaVM *javaVM, J9MM_IterateObjectDescriptor *objectDesc, J9MM_IterateRegionDescriptor *regionDesc)
{
	MM_GCExtensions * extensions = MM_GCExtensions::getExtensions(javaVM);
	J9Class* clazz = NULL;
	UDATA result = J9MODRON_SLOT_ITERATOR_OK;

	/* If we encounter a hole with size 0, the heap walk will enter an infinite loop -- prevent this. */
	/* Size of hole can not be larger then rest of the region */
	if (FALSE == objectDesc->isObject) {
		if ((0 == objectDesc->size) || (objectDesc->size > ((UDATA)regionDesc->regionStart +  regionDesc->regionSize - (UDATA)objectDesc->object))) {
			GC_CheckError error(objectDesc->object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE, _cycle->nextErrorCount());
			_reporter->report(&error);
			_reporter->reportHeapWalkError(&error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	result = checkJ9Object(javaVM, objectDesc->object, regionDesc, _cycle->getCheckFlags());
	if (J9MODRON_GCCHK_RC_OK != result) {
		const char *elementName = extensions->objectModel.isIndexable(objectDesc->object) ? "IObject " : "Object ";
		GC_CheckError error(objectDesc->object, _cycle, _currentCheck, (char *)elementName, result, _cycle->nextErrorCount());
		_reporter->report(&error);
		_reporter->reportHeapWalkError(&error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
		return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
	}

	clazz = J9GC_J9OBJECT_CLAZZ_VM(objectDesc->object, javaVM);
	result = checkJ9ClassPointer(javaVM, clazz, true);
	if (J9MODRON_GCCHK_RC_OK == result) {
		ObjectSlotIteratorCallbackUserData userData;
		userData.engine = this;
		userData.regionDesc = regionDesc;
		userData.result = result;
		javaVM->memoryManagerFunctions->j9mm_iterate_object_slots(javaVM, _portLibrary, objectDesc, j9mm_iterator_flag_exclude_null_refs, check_objectSlotsCallback, &userData);
		result = userData.result;
	}

	/* check Ownable Synchronizer Object consistency */
	if ((OBJECT_HEADER_SHAPE_MIXED == J9GC_CLASS_SHAPE(clazz)) && (0 != (J9CLASS_FLAGS(clazz) & J9AccClassOwnableSynchronizer))) {
		if (NULL == extensions->accessBarrier->isObjectInOwnableSynchronizerList(objectDesc->object)) {
			PORT_ACCESS_FROM_PORT(_portLibrary);
			j9tty_printf(PORTLIB, "  <gc check: found Ownable SynchronizerObject %p is not on the list >\n", objectDesc->object);
		} else {
			_ownableSynchronizerObjectCountOnHeap += 1;
		}
	}

	if (J9MODRON_SLOT_ITERATOR_OK == result) {
		/* this heap object is OK. Record it in the cache in case we find a pointer to it soon */
		UDATA cacheIndex = ((UDATA)objectDesc->object) % OBJECT_CACHE_SIZE;
		_checkedObjectCache[cacheIndex] = objectDesc->object;
	}
	
	return result;
}

/**
 * Verify a slot (double-indirect object pointer).
 *
 * @param objectIndirect the slot to be verified
 * @param objectIndirectBase the object which contains the slot
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotVMThread(J9JavaVM *javaVM, J9Object **objectIndirect, void *objectIndirectBase, UDATA objectType, GC_VMThreadIterator *vmthreadIterator)
{
	J9Object *objectPtr = *objectIndirect;

	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_STACK_OBJECT == result) {
		if (vmthreaditerator_state_monitor_records != vmthreadIterator->getState()) {
			GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount(), objectType);
			_reporter->report(&error);
		}
	} else if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount(), objectType);
		_reporter->report(&error);
	}
	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Verify a slot (double-indirect object pointer) on the stack.
 *
 * @param objectIndirect the slot to be verified
 * @param vmThread pointer to the thread whose stack contains the slot
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotStack(J9JavaVM *javaVM, J9Object **objectIndirect, J9VMThread *vmThread, const void *stackLocation)
{
	J9Object *objectPtr = *objectIndirect;

	UDATA result = checkObjectIndirect(javaVM, objectPtr);

	if (J9MODRON_GCCHK_RC_STACK_OBJECT == result) {
		result = checkStackObject(javaVM, objectPtr);
	}
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(vmThread, objectIndirect, stackLocation, _cycle, _currentCheck, result, _cycle->nextErrorCount());
		_reporter->report(&error);

		return J9MODRON_SLOT_ITERATOR_RECOVERABLE_ERROR;
	}
	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Verify a slot in a pool.
 *
 * @param objectIndirect the slot to be verified
 * @param objectIndirectBase the "object" containing the slot. Maybe or may not
 * be an actual <code>J9Object</code> (e.g. could be a <code>J9ClassLoader</code>
 * struct).  Note that this must be a remote pointer.
 *
 * @todo objectIndirectBase pointer should probably be changed to void *
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotPool(J9JavaVM *javaVM, J9Object **objectIndirect, void *objectIndirectBase)
{
	J9Object *objectPtr = *objectIndirect;
	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount(), check_type_other);
		_reporter->report(&error);
	}
	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Verify a remembered set slot.
 *
 * @param objectIndirect the slot to be verified
 * @param objectIndirectBase the sublist puddle which contains the slot
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotRememberedSet(J9JavaVM *javaVM, J9Object **objectIndirect, MM_SublistPuddle *puddle)
{
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(javaVM);
	J9Object *objectPtr = *objectIndirect;

	if (_cycle->getMiscFlags() & J9MODRON_GCCHK_MISC_MIDSCAVENGE) {
		/* during a scavenge, some RS entries may be tagged -- remove the tag */
		if ( DEFERRED_RS_REMOVE_FLAG == (((UDATA)objectPtr) & DEFERRED_RS_REMOVE_FLAG) ) {
			objectPtr = (J9Object*)( ((UDATA)objectPtr) & ~(UDATA)DEFERRED_RS_REMOVE_FLAG );
		}
	}
	
	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(puddle, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount());
		_reporter->report(&error);
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	/* Additional checks for the remembered set */
	if (NULL != objectPtr) {
		J9MM_IterateRegionDescriptor objectRegion;
		if (!findRegionForPointer(javaVM, objectPtr, &objectRegion)) {
			/* shouldn't happen, since checkObjectIndirect() already verified this object */
			GC_CheckError error(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_NOT_FOUND, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}

		/* we shouldn't have newspace references in the remembered set */
		UDATA regionType = ((MM_HeapRegionDescriptor*)objectRegion.id)->getTypeFlags();

		if (regionType & MEMORY_TYPE_NEW) {
			GC_CheckError error(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT, _cycle->nextErrorCount());
			_reporter->report(&error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}

		/* content of Remembered Set should be Old and Remembered */
		if ( !(extensions->isOld(objectPtr) && extensions->objectModel.isRemembered(objectPtr))) {
			GC_CheckError error(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS, _cycle->nextErrorCount());
			_reporter->report(&error);
			_reporter->reportObjectHeader(&error, objectPtr, NULL);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
	}

	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Verify a unfinalized object list slot.
 *
 * @param objectIndirect the slot to be verified
 * @param currentList the unfinalizedObjectList which contains the slot
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotUnfinalizedList(J9JavaVM *javaVM, J9Object **objectIndirect, MM_UnfinalizedObjectList *currentList)
{
	J9Object *objectPtr = *objectIndirect;

	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(currentList, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount());
		_reporter->report(&error);
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	return J9MODRON_SLOT_ITERATOR_OK;
}

UDATA
GC_CheckEngine::checkSlotOwnableSynchronizerList(J9JavaVM *javaVM, J9Object **objectIndirect, MM_OwnableSynchronizerObjectList *currentList)
{
	J9Object *objectPtr = *objectIndirect;
	UDATA rc = J9MODRON_SLOT_ITERATOR_OK;

	_ownableSynchronizerObjectCountOnList += 1;

	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(currentList, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount());
		_reporter->report(&error);
	} else {
		J9Class *instanceClass = J9GC_J9OBJECT_CLAZZ_VM(objectPtr, javaVM);
		if (0 == (J9CLASS_FLAGS(instanceClass) & J9AccClassOwnableSynchronizer)) {
			GC_CheckError error(currentList, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_INVALID_FLAGS, _cycle->nextErrorCount());
			_reporter->report(&error);
		}
		J9VMThread* currentThread = javaVM->internalVMFunctions->currentVMThread(javaVM);
		J9ClassLoader* classLoader = instanceClass->classLoader;
		const char* aosClassName = "java/util/concurrent/locks/AbstractOwnableSynchronizer";

		J9Class* castClass = javaVM->internalVMFunctions->internalFindClassUTF8(currentThread, (U_8*) aosClassName, strlen(aosClassName), classLoader, J9_FINDCLASS_FLAG_EXISTING_ONLY);
		if (NULL != castClass) {
			if (0 == instanceOfOrCheckCast(instanceClass, castClass)) {
				GC_CheckError error(currentList, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_OWNABLE_SYNCHRONIZER_INVALID_CLASS, _cycle->nextErrorCount());
				_reporter->report(&error);
			}
		}
	}
	return rc;
}

/**
 * Verify a finalized object queue slot.
 *
 * @param objectIndirect the slot to be verified
 * @param listManager the MM_FinalizeListManager which contains the slot
 *
 * @return #J9MODRON_SLOT_ITERATOR_OK
 */
UDATA
GC_CheckEngine::checkSlotFinalizableList(J9JavaVM *javaVM, J9Object **objectIndirect, GC_FinalizeListManager *listManager)
{
	J9Object *objectPtr = *objectIndirect;

	UDATA result = checkObjectIndirect(javaVM, objectPtr);
	if (J9MODRON_GCCHK_RC_OK != result) {
		GC_CheckError error(listManager, objectIndirect, _cycle, _currentCheck, result, _cycle->nextErrorCount());
		_reporter->report(&error);
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	return J9MODRON_SLOT_ITERATOR_OK;
}

/**
 * Start of a check
 * This function should be called before any of the check functions in
 * the engine. It ensures that the heap is walkable and TLHs are flushed
 */
void
GC_CheckEngine::startCheckCycle(J9JavaVM *javaVM, GC_CheckCycle *checkCycle)
{
	_cycle = checkCycle;
	_currentCheck = NULL;
#if defined(J9VM_GC_MODRON_SCAVENGER)
	_scavengerBackout = false;
	_rsOverflowState = false;
#endif /* J9VM_GC_MODRON_SCAVENGER */
	clearPreviousObjects();
	clearRegionDescription(&_regionDesc);
	clearCheckedCache();

	clearCountsForOwnableSynchronizerObjects();

	/* Flush any VM level changes to prepare for a safe slot walk */
	TRIGGER_J9HOOK_MM_PRIVATE_WALK_HEAP_START(MM_GCExtensions::getExtensions(javaVM)->privateHookInterface, javaVM->omrVM);
}

/**
 * End of a check
 * This function should be called at the end of a check. It triggers
 * the hook to inform interested parties that a heap walk has occurred.
 */
void GC_CheckEngine::endCheckCycle(J9JavaVM *javaVM)
{
	TRIGGER_J9HOOK_MM_PRIVATE_WALK_HEAP_END(MM_GCExtensions::getExtensions(javaVM)->privateHookInterface, javaVM->omrVM);
}

/**
 * Advance to the next stage of checking.
 * Sets the context for the next stage of checking.  Called every time verification
 * moves to a new structure.
 *
 * @param check Pointer to the check we're about to start
 */
void
GC_CheckEngine::startNewCheck(GC_Check *check)
{
	_currentCheck = check;
	clearPreviousObjects();
}

/**
 * Ensure the GC internal scope pointers refer to objects within the scope.
 *
 * @param scopedMemorySpace The memory space with the internal pointers to verify.
 * @param regionDesc The region descriptor which should contain all pointers.
 */

/**
 * Create new instance of check object, using the specified reporter object,
 * and the specified port library.
 */
GC_CheckEngine *
GC_CheckEngine::newInstance(J9JavaVM *javaVM, GC_CheckReporter *reporter)
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(javaVM)->getForge();

	GC_CheckEngine *check = (GC_CheckEngine *)forge->allocate(sizeof(GC_CheckEngine), MM_AllocationCategory::DIAGNOSTIC, J9_GET_CALLSITE());
	if (check) {
		check = new(check) GC_CheckEngine(javaVM, reporter);
		if (!check->initialize()) {
			check->kill();
			check = NULL;
		}
	}
	return check;
}

bool
GC_CheckEngine::initialize()
{
	clearPreviousObjects();
	clearRegionDescription(&_regionDesc);
	clearCheckedCache();
	return true;
}

void
GC_CheckEngine::kill()
{
	MM_Forge *forge = MM_GCExtensions::getExtensions(_javaVM)->getForge();
	if(_reporter) {
		_reporter->kill();
	}
	forge->free(this);
}

/**
 * Determine whether or not the a verbose stack dump should always be displayed.
 *
 * @return true if verbose stack dump is always displayed.
 * @return false if verbose stack dump is only displayed on error.
 */
bool
GC_CheckEngine::isStackDumpAlwaysDisplayed()
{
	if (NULL == _cycle) {
		return false;
	}
	return (J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK == (_cycle->getMiscFlags() & J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK));
}

/**
 * Copy the information from one regionDescription to the other.
 * @param from - the source region
 * @param to - the destination region
 *
 */
void
GC_CheckEngine::copyRegionDescription(J9MM_IterateRegionDescriptor* from, J9MM_IterateRegionDescriptor* to)
{
	to->name = from->name;
	to->id = from->id;
	to->objectAlignment = from->objectAlignment;
	to->objectMinimumSize = from->objectMinimumSize;
	to->regionStart = from->regionStart;
	to->regionSize = from->regionSize;
}

/**
 * Clear the region
 * @param toClear - the region to clear
 *
 */
void
GC_CheckEngine::clearRegionDescription(J9MM_IterateRegionDescriptor* toClear)
{
	memset(toClear, 0, sizeof(J9MM_IterateRegionDescriptor));
}

void 
GC_CheckEngine::clearCheckedCache()
{
	memset(_checkedClassCache, 0, sizeof(_checkedClassCache));
	memset(_checkedClassCacheAllowUndead, 0, sizeof(_checkedClassCacheAllowUndead));
	memset(_checkedObjectCache, 0, sizeof(_checkedObjectCache));
}

static jvmtiIterationControl
check_objectSlotsCallback(J9JavaVM *javaVM, J9MM_IterateObjectDescriptor *objectDesc, J9MM_IterateObjectRefDescriptor *refDesc, void *userData)
{
	ObjectSlotIteratorCallbackUserData* castUserData = (ObjectSlotIteratorCallbackUserData*)userData;
	castUserData->result = castUserData->engine->checkSlotObjectHeap(javaVM, (J9Object *)refDesc->object, (fj9object_t*)refDesc->fieldAddress, castUserData->regionDesc, objectDesc->object);
	if (J9MODRON_GCCHK_RC_OK != castUserData->result) {
		return JVMTI_ITERATION_ABORT;
	}
	return JVMTI_ITERATION_CONTINUE;
}

/**
 * Determine whether or not the given object is contained in the given region
 *
 * @return true if objectPtr is contained in the given region
 * @return false otherwise
 */
static bool
isPointerInRegion(void *pointer, J9MM_IterateRegionDescriptor *regionDesc)
{
	UDATA regionStart = (UDATA)regionDesc->regionStart;
	UDATA regionEnd = regionStart + regionDesc->regionSize;
	UDATA address = (UDATA)pointer;

	return ((address >= regionStart) && (address < regionEnd));
}

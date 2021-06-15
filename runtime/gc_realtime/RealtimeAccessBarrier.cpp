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

#include "ArrayletObjectModel.hpp"
#include "Bits.hpp"
#include "EnvironmentRealtime.hpp"
#include "HeapRegionDescriptorRealtime.hpp"
#include "HeapRegionManager.hpp"
#include "JNICriticalRegion.hpp"
#include "RealtimeAccessBarrier.hpp"
#include "RealtimeGC.hpp"
#include "RealtimeMarkingScheme.hpp"

#if defined(J9VM_GC_REALTIME)

/**
 * Static method for instantiating the access barrier.
 */
MM_RealtimeAccessBarrier *
MM_RealtimeAccessBarrier::newInstance(MM_EnvironmentBase *env)
{
	MM_RealtimeAccessBarrier *barrier;
	
	barrier = (MM_RealtimeAccessBarrier *)env->getForge()->allocate(sizeof(MM_RealtimeAccessBarrier), MM_AllocationCategory::FIXED, OMR_GET_CALLSITE());
	if (barrier) {
		new(barrier) MM_RealtimeAccessBarrier(env);
		if (!barrier->initialize(env)) {
			barrier->kill(env);
			barrier = NULL;
		}
	}
	return barrier;
}

bool 
MM_RealtimeAccessBarrier::initialize(MM_EnvironmentBase *env)
{
	if (!MM_ObjectAccessBarrier::initialize(env)) {
		return false;
	}
	_realtimeGC = MM_GCExtensions::getExtensions(env)->realtimeGC;
	_markingScheme = _realtimeGC->getMarkingScheme();
	
	return true;
}

void 
MM_RealtimeAccessBarrier::kill(MM_EnvironmentBase *env)
{
	tearDown(env);
	env->getForge()->free(this);
}

void
MM_RealtimeAccessBarrier::tearDown(MM_EnvironmentBase *env)
{
	MM_ObjectAccessBarrier::tearDown(env);
}

/**
 * Override of referenceGet.  This barriered version does two things.  (1) When the
 * collector is tracing, it makes any gotten object "grey" to ensure that it is eventually
 * traced.  (2) When the collector is in the unmarkedImpliesCleared phase (after
 * to-be-cleared soft and weak references have been identified and logically cleared), the
 * get() operation returns NULL instead of the referent if the referent is unmarked.
 *
 * @param refObject the SoftReference or WeakReference object on which get() is being called.
 *	This barrier must not be called for PhantomReferences.  The parameter must not be NULL.
 */
J9Object *
MM_RealtimeAccessBarrier::referenceGet(J9VMThread *vmThread, J9Object *refObject)
{
	UDATA offset = J9VMJAVALANGREFREFERENCE_REFERENT_OFFSET(vmThread);
	J9Object *referent = mixedObjectReadObject(vmThread, refObject, offset, false);

	/* Do nothing exceptional for NULL or marked referents */
	if (referent == NULL) {
		goto done;	
	}

	if (_markingScheme->isMarked(referent)) {
		goto done;
	}
	
	/* Now we know referent isn't NULL and isn't marked */
	
	if (_realtimeGC->getRealtimeDelegate()->_unmarkedImpliesCleared) {
		/* In phase indicated by this flag, all unmarked references are logically cleared
		 * (will be physically cleared by the end of the gc).
		 */
		return NULL;
	}

	/* Throughout tracing, we must turn any gotten reference into a root, because the
	 * thread doing the getting may already have been scanned.  However, since we are
	 * running on a mutator thread and not a gc thread we do this indirectly by putting
	 * the object in the barrier buffer.
	 */
	if (_realtimeGC->isBarrierEnabled()) {
		MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
		rememberObject(env, referent);
	}

done:
	/* We must return the external reference */
	return referent;
}

void
MM_RealtimeAccessBarrier::referenceReprocess(J9VMThread *vmThread, J9Object *refObject)
{
	referenceGet(vmThread, refObject);
}

/**
 * Barrier called from within j9jni_deleteGlobalRef to maintain the "double barrier"
 * invariant (see comment in preObjectStore).  To maintain EXACTLY what we do for ordinary
 * reference stores, we should trap globalref creation for unscanned threads (only) and
 * globalref deletion for all threads.  But, since creating a global ref can
 * only open a leak if that reference is subsequently deleted, it is sufficient to trap
 * deletions for all threads. Note that we do not attempt to guard against JNI threads
 * passing objects by back channels without creating global refs (we could not do so even
 * if we wished to).  Such JNI code is already treacherously unsafe and would sooner or
 * later crash with or without incremental thread scanning.
 *
 * @param reference the JNI global reference that is about to be deleted.  Must not be NULL
 *  (same requirement as j9jni_deleteGlobalRef).
 */
void 
MM_RealtimeAccessBarrier::jniDeleteGlobalReference(J9VMThread *vmThread, J9Object *reference)
{
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);

	if (!_realtimeGC->isBarrierEnabled()) {
		return;
	}

	deleteHeapReference(env, reference);
}

/**
 * Take any required action when a string constant has been fetched from the table.
 * If there is a yield point between the last marking work and when the string table
 * is cleared, then a thread could potentially get a reference to a string constant
 * that is about to be cleared. This method prevents that be adding the string to 
 * the remembered set.
 */
void 
MM_RealtimeAccessBarrier::stringConstantEscaped(J9VMThread *vmThread, J9Object *stringConst)
{
	MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);

	if (_realtimeGC->isBarrierEnabled()) {
		rememberObject(env, stringConst);
	}	
}

/**
 * Check that the two string constants should be considered truly "live".
 * This is a bit of a hack to enable us to scan the string table incrementally.
 * If we are still doing marking work, we treat a call to this method as meaning
 * that one of the strings has been fetched from the string table. If we are finished
 * marking work, but have yet to clear the string table, we treat any unmarked strings
 * as cleared, by returning false from this function.
 * NOTE: Because this function is called from the hash equals function, we can't
 * actually tell which one of the strings is actually the one in the table already,
 * so we have to assume they both are.
 * @return true if they can be considered live, false otherwise.
 */
bool
MM_RealtimeAccessBarrier::checkStringConstantsLive(J9JavaVM *javaVM, j9object_t stringOne, j9object_t stringTwo)
{
	if (_realtimeGC->isBarrierEnabled()) {
		if (_realtimeGC->getRealtimeDelegate()->_unmarkedImpliesStringsCleared) {
			/* If this flag is set, we will not scan the remembered set again, so we must
			 * treat any unmarked string constant as having been cleared.
			 */
			return (_realtimeGC->getMarkingScheme()->isMarked((J9Object *)stringOne) && _realtimeGC->getMarkingScheme()->isMarked((J9Object *)stringTwo));
		} else {
			J9VMThread* vmThread =  javaVM->internalVMFunctions->currentVMThread(javaVM);
			stringConstantEscaped(vmThread, (J9Object *)stringOne);
			stringConstantEscaped(vmThread, (J9Object *)stringTwo);
		}
	}
	return true;
}

/**
 *  Equivalent to checkStringConstantsLive but for a single string constant
 */
bool
MM_RealtimeAccessBarrier::checkStringConstantLive(J9JavaVM *javaVM, j9object_t string)
{
	if (_realtimeGC->isBarrierEnabled()) {
		if (_realtimeGC->getRealtimeDelegate()->_unmarkedImpliesStringsCleared) {
			/* If this flag is set, we will not scan the remembered set again, so we must
			 * treat any unmarked string constant as having been cleared.
			 */
			return _realtimeGC->getMarkingScheme()->isMarked((J9Object *)string);
		} else {
			J9VMThread* vmThread =  javaVM->internalVMFunctions->currentVMThread(javaVM);
			stringConstantEscaped(vmThread, (J9Object *)string);
		}
	}
	return true;
}

/**
 * Take any required action when a heap reference is deleted. This method is called on
 * all slots when finalizing a scoped object. It's also called from jniDeleteGlobalReference,
 * but only when the collector is tracing (therefore we shouldn't check for isBarrierEnabled
 * from this method).
 */
void
MM_RealtimeAccessBarrier::deleteHeapReference(MM_EnvironmentBase *env, J9Object *object)
{
	rememberObject(env, object);
}

/**
 * DEBUG method. Only called when MM_GCExtensions::debugWriteBarrier >= 1.
 */ 
void 
MM_RealtimeAccessBarrier::validateWriteBarrier(J9VMThread *vmThread, J9Object *dstObject, fj9object_t *dstAddress, J9Object *srcObject)
{
	J9JavaVM *javaVM = vmThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	bool const compressed = J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread);

	switch(_extensions->objectModel.getScanType(dstObject)) {
	case GC_ObjectModel::SCAN_MIXED_OBJECT_LINKED:
	case GC_ObjectModel::SCAN_ATOMIC_MARKABLE_REFERENCE_OBJECT:
	case GC_ObjectModel::SCAN_MIXED_OBJECT:
	case GC_ObjectModel::SCAN_OWNABLESYNCHRONIZER_OBJECT:
	case GC_ObjectModel::SCAN_CLASS_OBJECT:
	case GC_ObjectModel::SCAN_CLASSLOADER_OBJECT:
	case GC_ObjectModel::SCAN_REFERENCE_MIXED_OBJECT:
	{
		intptr_t slotIndex = GC_SlotObject::subtractSlotAddresses(dstAddress, (fj9object_t*)dstObject, compressed);
		if (slotIndex < 0) {
			j9tty_printf(PORTLIB, "validateWriteBarrier: slotIndex is negative dstAddress %d and dstObject %d\n", dstAddress, dstObject);
		}
		UDATA dataSizeInSlots = MM_Bits::convertBytesToSlots(_extensions->objectModel.getSizeInBytesWithHeader(dstObject));
		if ((UDATA)slotIndex >= dataSizeInSlots) {
			j9tty_printf(PORTLIB, "validateWriteBarrier: slotIndex (%d) >= object size in slots (%d)", slotIndex, dataSizeInSlots);
			printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
			j9tty_printf(PORTLIB, "\n");
		}
		/* Also consider validating that slot is a ptr slot */
		break;
	}

	case GC_ObjectModel::SCAN_POINTER_ARRAY_OBJECT:
	{
		MM_HeapRegionManager *regionManager = MM_GCExtensions::getExtensions(javaVM)->getHeap()->getHeapRegionManager();
		GC_ArrayletObjectModel::ArrayLayout layout = _extensions->indexableObjectModel.getArrayLayout((J9IndexableObject*)dstObject);
		switch (layout) {
			case GC_ArrayletObjectModel::InlineContiguous: {
				UDATA** arrayletPtr = (UDATA**)(((J9IndexableObject*)dstObject) + 1);
				UDATA* dataStart = *arrayletPtr;
				UDATA* dataEnd = dataStart + _extensions->indexableObjectModel.getSizeInElements((J9IndexableObject*)dstObject);
				if ((UDATA*)dstAddress < dataStart || (UDATA*)dstAddress >= dataEnd) {
					j9tty_printf(PORTLIB, "validateWriteBarrier: IC: store to %p not in data section of array %p to %p", dstAddress, dataStart, dataEnd);
					printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
					j9tty_printf(PORTLIB, "\n");
				}
				break;
			}
			case GC_ArrayletObjectModel::Discontiguous: {
				MM_HeapRegionDescriptorRealtime *region = (MM_HeapRegionDescriptorRealtime *)regionManager->tableDescriptorForAddress(dstAddress);
				if (!region->isArraylet()) {
					j9tty_printf(PORTLIB, "validateWriteBarrier: D: dstAddress (%p) is not on an arraylet region", dstAddress);
					printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
					j9tty_printf(PORTLIB, "\n");
				}
				else {
					UDATA* arrayletParent = region->getArrayletParent(region->whichArraylet((UDATA*)dstAddress, javaVM->arrayletLeafLogSize));
					if (arrayletParent != (UDATA*)dstObject) {
						j9tty_printf(PORTLIB, "validateWriteBarrier: D: parent of arraylet (%p) is not destObject (%p)", arrayletParent, dstObject);
						printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
						j9tty_printf(PORTLIB, "\n");
					}
				}
				break;
			}
			case GC_ArrayletObjectModel::Hybrid: {
				/* First check to see if it is in the last arraylet which is contiguous with the array spine. */
				UDATA numberArraylets = _extensions->indexableObjectModel.numArraylets((J9IndexableObject*)dstObject);
				UDATA** arrayletPtr = (UDATA**)(((J9IndexableObject*)dstObject)+1) + numberArraylets - 1;
				UDATA* dataStart = *arrayletPtr;
				UDATA spineSize = _extensions->indexableObjectModel.getSpineSize((J9IndexableObject*)dstObject);
				UDATA* dataEnd = (UDATA*)(((U_8*)dstObject) + spineSize);
				if ((UDATA*)dstAddress < dataStart || (UDATA*)dstAddress >= dataEnd) {
					/* store was _not_ to last arraylet; attempt to validate that
					 * it was to one of the other arraylets of this array.
					 */
					MM_HeapRegionDescriptorRealtime *region = (MM_HeapRegionDescriptorRealtime *)regionManager->tableDescriptorForAddress(dstAddress);
					if (!region->isArraylet()) {
						j9tty_printf(PORTLIB, "validateWriteBarrier: H: dstAddress (%p) is not on an arraylet region", dstAddress);
						printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
					}
					else {
						UDATA* arrayletParent = region->getArrayletParent(region->whichArraylet((UDATA*)dstAddress, javaVM->arrayletLeafLogSize));
						if (arrayletParent != (UDATA*)dstObject) {
							j9tty_printf(PORTLIB, "validateWriteBarrier: H: parent of arraylet (%p) is not destObject (%p)", arrayletParent, dstObject);
							printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
							j9tty_printf(PORTLIB, "\n");
						}
					}
				}
				break;
			}
			default: {
				j9tty_printf(PORTLIB, "validateWriteBarrier: unexpected arraylet type %d\n", layout);
				assert(0);
			}
		};
		break;
	}

	case GC_ObjectModel::SCAN_PRIMITIVE_ARRAY_OBJECT:
		j9tty_printf(PORTLIB, "validateWriteBarrier: writeBarrier called on array of primitive\n");
		j9tty_printf(PORTLIB, "value being overwritten is %d\n", GC_SlotObject::readSlot(dstAddress, compressed));
		printClass(javaVM, J9GC_J9OBJECT_CLAZZ_VM(dstObject, javaVM));
		j9tty_printf(PORTLIB, "\n");
		break;

	default:
		Assert_MM_unreachable();
	}
}

void 
MM_RealtimeAccessBarrier::printClass(J9JavaVM *javaVM, J9Class* clazz)
{
	J9ROMClass* romClass;
	J9UTF8* utf;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	/* TODO: In Sov, if the class is char[], the string is printed instead of the class name */
	romClass = clazz->romClass;
	if(romClass->modifiers & J9AccClassArray) {
		J9ArrayClass* arrayClass = (J9ArrayClass*) clazz;
		UDATA arity = arrayClass->arity;
		utf = J9ROMCLASS_CLASSNAME(arrayClass->leafComponentType->romClass);
		j9tty_printf(PORTLIB, "%.*s", (UDATA)J9UTF8_LENGTH(utf), J9UTF8_DATA(utf));
		while(arity--) {
			j9tty_printf(PORTLIB, "[]");
		}
	} else {
		utf = J9ROMCLASS_CLASSNAME(romClass);
		j9tty_printf(PORTLIB, "%.*s", (UDATA)J9UTF8_LENGTH(utf), J9UTF8_DATA(utf));
	}
}

/**
 * Unmarked, heap reference, about to be deleted (or overwritten), while marking
 * is in progress is to be remembered for later marking and scanning.
 */
void
MM_RealtimeAccessBarrier::rememberObject(MM_EnvironmentBase *env, J9Object *object)
{
	if (_markingScheme->markObject(MM_EnvironmentRealtime::getEnvironment(env), object, true)) {
		rememberObjectImpl(env, object);
	}
}

/**
 * Read an object from an internal VM slot (J9VMThread, J9JavaVM, named field of J9Class).
 * This function is only concerned with moving the actual data. Do not re-implement
 * unless the value is stored in a non-native format (e.g. compressed object pointers). 
 * See readObjectFromInternalVMSlot() for higher-level actions.
 * In realtime, we must remember the object being read in case it's being read from an
 * unmarked thread and stored on a marked threads' stack. If the unmarked thread terminates
 * before being marked, we will miss the object since a stack push doesn't invoke the write
 * barrier.
 * @param srcAddress the address of the field to be read
 * @param isVolatile non-zero if the field is volatile, zero otherwise
 */
mm_j9object_t
MM_RealtimeAccessBarrier::readObjectFromInternalVMSlotImpl(J9VMThread *vmThread, j9object_t *srcAddress, bool isVolatile)
{
	mm_j9object_t object = *srcAddress;
	if (NULL != vmThread) {
		rememberObjectIfBarrierEnabled(vmThread, object);
	}
	return object;
}

/**
 * Write an object to an internal VM slot (J9VMThread, J9JavaVM, named field of J9Class).
 * In realtime, we must explicitly remember the value when being stored in case it's being
 * read from an unmarked threads' stack and stored into a marked thread. If the unmarked
 * thread terminates before being marked, we will miss the object since a stack pop doesn't
 * invoke the barrier.
 * @param destSlot the slot to be used
 * @param value the value to be stored
 */
void 
MM_RealtimeAccessBarrier::storeObjectToInternalVMSlot(J9VMThread *vmThread, J9Object** destSlot, J9Object *value)
{
	if (preObjectStore(vmThread, destSlot, value, false)) {
		rememberObjectIfBarrierEnabled(vmThread, value);
		storeObjectToInternalVMSlotImpl(vmThread, destSlot, value, false);
		postObjectStore(vmThread, destSlot, value, false);
	}
}

/**
 * Call rememberObject() if realtimeGC->isBarrierEnabled() returns true.
 * @param object the object to remember
 */
void
MM_RealtimeAccessBarrier::rememberObjectIfBarrierEnabled(J9VMThread *vmThread, J9Object* object)
{
	MM_EnvironmentRealtime* env = MM_EnvironmentRealtime::getEnvironment(vmThread->omrVMThread);
	if (_realtimeGC->isBarrierEnabled()) {
		rememberObject(env, object);
	}
}

void*
MM_RealtimeAccessBarrier::jniGetPrimitiveArrayCritical(J9VMThread* vmThread, jarray array, jboolean *isCopy)
{
	void *data = NULL;
	J9JavaVM *javaVM = vmThread->javaVM;
	J9InternalVMFunctions *functions = javaVM->internalVMFunctions;

	J9IndexableObject *arrayObject = (J9IndexableObject*)J9_JNI_UNWRAP_REFERENCE(array);
	bool shouldCopy = false;
	if((javaVM->runtimeFlags & J9_RUNTIME_ALWAYS_COPY_JNI_CRITICAL) == J9_RUNTIME_ALWAYS_COPY_JNI_CRITICAL) {
		shouldCopy = true;
	} else if (!_extensions->indexableObjectModel.isInlineContiguousArraylet(arrayObject)) {
		/* an array having discontiguous extents is another reason to force the critical section to be a copy */
		shouldCopy = true;
	}

	if(shouldCopy) {
		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		GC_ArrayObjectModel* indexableObjectModel = &_extensions->indexableObjectModel;
		I_32 sizeInElements = (I_32)indexableObjectModel->getSizeInElements(arrayObject);
		UDATA sizeInBytes = indexableObjectModel->getDataSizeInBytes(arrayObject);
		data = functions->jniArrayAllocateMemoryFromThread(vmThread, sizeInBytes);
		if(NULL == data) {
			functions->setNativeOutOfMemoryError(vmThread, 0, 0);	// better error message here?
		} else {
			indexableObjectModel->memcpyFromArray(data, arrayObject, 0, sizeInElements);
			if(NULL != isCopy) {
				*isCopy = JNI_TRUE;
			}
		}
		vmThread->jniCriticalCopyCount += 1;
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	} else {
		// acquire access and return a direct pointer
		MM_JNICriticalRegion::enterCriticalRegion(vmThread, false);
		data = (void *)_extensions->indexableObjectModel.getDataPointerForContiguous(arrayObject);
		if(NULL != isCopy) {
			*isCopy = JNI_FALSE;
		}
	}
	return data;
}

void
MM_RealtimeAccessBarrier::jniReleasePrimitiveArrayCritical(J9VMThread* vmThread, jarray array, void * elems, jint mode)
{
	J9JavaVM *javaVM = vmThread->javaVM;
	J9InternalVMFunctions *functions = javaVM->internalVMFunctions;

	J9IndexableObject *arrayObject = (J9IndexableObject*)J9_JNI_UNWRAP_REFERENCE(array);
	bool shouldCopy = false;
	if((javaVM->runtimeFlags & J9_RUNTIME_ALWAYS_COPY_JNI_CRITICAL) == J9_RUNTIME_ALWAYS_COPY_JNI_CRITICAL) {
		shouldCopy = true;
	} else if (!_extensions->indexableObjectModel.isInlineContiguousArraylet(arrayObject)) {
		/* an array having discontiguous extents is another reason to force the critical section to be a copy */
		shouldCopy = true;
	}

	if(shouldCopy) {
		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		if(JNI_ABORT != mode) {
			GC_ArrayObjectModel* indexableObjectModel = &_extensions->indexableObjectModel;
			I_32 sizeInElements = (I_32)indexableObjectModel->getSizeInElements(arrayObject);
			_extensions->indexableObjectModel.memcpyToArray(arrayObject, 0, sizeInElements, elems);
		}

		// Commit means copy the data but do not free the buffer.
		// All other modes free the buffer.
		if(JNI_COMMIT != mode) {
			functions->jniArrayFreeMemoryFromThread(vmThread, elems);
		}

		if(vmThread->jniCriticalCopyCount > 0) {
			vmThread->jniCriticalCopyCount -= 1;
		} else {
			Assert_MM_invalidJNICall();
		}

		VM_VMAccess::inlineExitVMToJNI(vmThread);
	} else {
		/*
		 * Objects can not be moved if critical section is active
		 * This trace point will be generated if object has been moved or passed value of elems is corrupted
		 */
		void *data = (void *)_extensions->indexableObjectModel.getDataPointerForContiguous(arrayObject);
		if(elems != data) {
			Trc_MM_JNIReleasePrimitiveArrayCritical_invalid(vmThread, arrayObject, elems, data);
		}

		MM_JNICriticalRegion::exitCriticalRegion(vmThread, false);
	}
}

const jchar*
MM_RealtimeAccessBarrier::jniGetStringCritical(J9VMThread* vmThread, jstring str, jboolean *isCopy)
{
	jchar *data = NULL;
	J9JavaVM *javaVM = vmThread->javaVM;
	J9InternalVMFunctions *functions = javaVM->internalVMFunctions;
	bool isCompressed = false;
	bool shouldCopy = false;
	bool hasVMAccess = false;

	/* For now only copying is supported for arraylets */
	VM_VMAccess::inlineEnterVMFromJNI(vmThread);
	hasVMAccess = true;
	shouldCopy = true;

	if (shouldCopy) {
		J9Object *stringObject = (J9Object*)J9_JNI_UNWRAP_REFERENCE(str);
		J9IndexableObject *valueObject = (J9IndexableObject*)J9VMJAVALANGSTRING_VALUE(vmThread, stringObject);
		jint length = J9VMJAVALANGSTRING_LENGTH(vmThread, stringObject);
		UDATA sizeInBytes = length * sizeof(jchar);

		if (IS_STRING_COMPRESSED(vmThread, stringObject)) {
			isCompressed = true;
		}
		data = (jchar*)functions->jniArrayAllocateMemoryFromThread(vmThread, sizeInBytes);
		if (NULL == data) {
			functions->setNativeOutOfMemoryError(vmThread, 0, 0);	// better error message here?
		} else {
			GC_ArrayObjectModel* indexableObjectModel = &_extensions->indexableObjectModel;
			if (isCompressed) {
				jint i;
				
				for (i = 0; i < length; i++) {
					data[i] = (jchar)(U_8)J9JAVAARRAYOFBYTE_LOAD(vmThread, (j9object_t)valueObject, i);
				}
			} else {
				if (J9_ARE_ANY_BITS_SET(javaVM->runtimeFlags, J9_RUNTIME_STRING_BYTE_ARRAY)) {
					// This API determines the stride based on the type of valueObject so in the [B case we must passin the length in bytes
					indexableObjectModel->memcpyFromArray(data, valueObject, 0, (I_32)sizeInBytes);
				} else {
					indexableObjectModel->memcpyFromArray(data, valueObject, 0, length);
				}
			}
			if (NULL != isCopy) {
				*isCopy = JNI_TRUE;
			}
		}
		vmThread->jniCriticalCopyCount += 1;
	} else {
		// acquire access and return a direct pointer
		MM_JNICriticalRegion::enterCriticalRegion(vmThread, hasVMAccess);
		J9Object *stringObject = (J9Object*)J9_JNI_UNWRAP_REFERENCE(str);
		J9IndexableObject *valueObject = (J9IndexableObject*)J9VMJAVALANGSTRING_VALUE(vmThread, stringObject);

		data = (jchar*)_extensions->indexableObjectModel.getDataPointerForContiguous(valueObject);

		if (NULL != isCopy) {
			*isCopy = JNI_FALSE;
		}
	}
	if (hasVMAccess) {
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}
	return data;
}

void
MM_RealtimeAccessBarrier::jniReleaseStringCritical(J9VMThread* vmThread, jstring str, const jchar* elems)
{
	J9JavaVM *javaVM = vmThread->javaVM;
	J9InternalVMFunctions *functions = javaVM->internalVMFunctions;
	bool hasVMAccess = false;
	bool shouldCopy = false;

	/* For now only copying is supported for arraylets */
	shouldCopy = true;

	if (shouldCopy) {
		// String data is not copied back
		functions->jniArrayFreeMemoryFromThread(vmThread, (void*)elems);

		if(vmThread->jniCriticalCopyCount > 0) {
			vmThread->jniCriticalCopyCount -= 1;
		} else {
			Assert_MM_invalidJNICall();
		}
	} else {
		// direct pointer, just drop access
		MM_JNICriticalRegion::exitCriticalRegion(vmThread, hasVMAccess);
	}

	if (hasVMAccess) {
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}
}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
bool
MM_RealtimeAccessBarrier::checkClassLive(J9JavaVM *javaVM, J9Class *classPtr)
{
	J9ClassLoader *classLoader = classPtr->classLoader;
	bool result = false;

	if ((0 == (classLoader->gcFlags & J9_GC_CLASS_LOADER_DEAD)) && (0 == (J9CLASS_FLAGS(classPtr) & J9AccClassDying))) {
		/*
		 *  this class has not been discovered dead yet
		 *  so mark it if necessary to force it to be alive
		 */
		MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
		MM_RealtimeGC *realtimeGC = extensions->realtimeGC;
		J9Object *classLoaderObject = classLoader->classLoaderObject;

		if (NULL != classLoaderObject) {
			if (realtimeGC->getRealtimeDelegate()->_unmarkedImpliesClasses) {
				/*
				 * Mark is complete but GC cycle is still be in progress
				 * so we just can check is the correspondent class loader object marked
				 */
				result = realtimeGC->getMarkingScheme()->isMarked(classLoaderObject);
			} else {
				/*
				 * The return for this case is always true. If mark is active but not completed yet
				 * force this class to be marked to survive this GC
				 */

				J9VMThread* vmThread =  javaVM->internalVMFunctions->currentVMThread(javaVM);
				rememberObjectIfBarrierEnabled(vmThread, classLoaderObject);
				result = true;
			}
		} else {
			/* this class loader probably is in initialization process and class loader object has not been attached yet */
			result = true;
		}
	}

	return result;
}
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */

/**
 * Unmarked, heap reference, about to be deleted (or overwritten), while marking
 * is in progress is to be remembered for later marking and scanning.
 * This method is called by MM_RealtimeAccessBarrier::rememberObject()
 */
void
MM_RealtimeAccessBarrier::rememberObjectImpl(MM_EnvironmentBase *env, J9Object* object)
{
	J9VMThread *vmThread = (J9VMThread *)env->getLanguageVMThread();
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vmThread->javaVM);

	extensions->sATBBarrierRememberedSet->storeInFragment(env, &vmThread->sATBBarrierRememberedSetFragment, (UDATA *)object);
}

void
MM_RealtimeAccessBarrier::forcedToFinalizableObject(J9VMThread* vmThread, J9Object* object)
{
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
	if (isBarrierActive(env)) {
		rememberObject(env, object);
	}
}

/**
 * @copydoc MM_ObjectAccessBarrier::preObjectStore()
 * 
 * This is the implementation of the realtime write barrier.
 * 
 * Realtime uses a snapshot-at-the-beginning algorithm, but with a fuzzy snapshot in the
 * sense that threads are allowed to run during the root scan.  This requires a "double
 * barrier."  The barrier is active from the start of root scanning through the end of
 * tracing.  For an unscanned thread performing a store, the new value is remembered by
 * the collector.  For any thread performing a store (whether scanned or not), the old
 * value is remembered by the collector before being overwritten (thus this barrier must be
 * positioned as a pre-store barrier).  For the latter ("Yuasa barrier") aspect of the
 * double barrier, only the first overwritten value needs to be remembered (remembering 
 * others is harmless but not needed), and so we omit synchronization on the reading of the 
 * old value.
 */
bool
MM_RealtimeAccessBarrier::preObjectStoreInternal(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *value, bool isVolatile)
{
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
	
	if (isBarrierActive(env)) {
		if (NULL != destObject) {
			if (isDoubleBarrierActiveOnThread(vmThread)) {
				rememberObject(env, value);
			}
		
			J9Object *oldObject = NULL;
			protectIfVolatileBefore(vmThread, isVolatile, true, false);
			GC_SlotObject slotObject(vmThread->javaVM->omrVM, destAddress);
			oldObject = slotObject.readReferenceFromSlot();
			protectIfVolatileAfter(vmThread, isVolatile, true, false);
			rememberObject(env, oldObject);
		}
	}

	return true;
}

/**
 * @copydoc MM_ObjectAccessBarrier::preObjectStore()
 * 
 * This is the implementation of the realtime write barrier.
 * 
 * Realtime uses a snapshot-at-the-beginning algorithm, but with a fuzzy snapshot in the
 * sense that threads are allowed to run during the root scan.  This requires a "double
 * barrier."  The barrier is active from the start of root scanning through the end of
 * tracing.  For an unscanned thread performing a store, the new value is remembered by
 * the collector.  For any thread performing a store (whether scanned or not), the old
 * value is remembered by the collector before being overwritten (thus this barrier must be
 * positioned as a pre-store barrier).  For the latter ("Yuasa barrier") aspect of the
 * double barrier, only the first overwritten value needs to be remembered (remembering 
 * others is harmless but not needed), and so we omit synchronization on the reading of the 
 * old value.
 */
bool
MM_RealtimeAccessBarrier::preObjectStoreInternal(J9VMThread *vmThread, J9Object **destAddress, J9Object *value, bool isVolatile)
{
	MM_EnvironmentBase* env = MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread);
	
	if (isBarrierActive(env)) {
		if (isDoubleBarrierActiveOnThread(vmThread)) {
			rememberObject(env, value);
		}
		J9Object* oldObject = NULL;
		protectIfVolatileBefore(vmThread, isVolatile, true, false);
		oldObject = *destAddress;
		protectIfVolatileAfter(vmThread, isVolatile, true, false);
		rememberObject(env, oldObject);
	}
	
	return true;
}

bool
MM_RealtimeAccessBarrier::preObjectStoreInternal(J9VMThread *vmThread, J9Object* destClass, J9Object **destAddress, J9Object *value, bool isVolatile)
{
	/* the destClass argument is ignored, so just call the generic slot version */
	return preObjectStoreInternal(vmThread, destAddress, value, isVolatile);
}

/**
 * @copydoc MM_ObjectAccessBarrier::preObjectStore()
 * 
 * Metronome uses a snapshot-at-the-beginning algorithm, but with a fuzzy snapshot in the
 * sense that threads are allowed to run during the root scan.  This requires a "double
 * barrier."  The barrier is active from the start of root scanning through the end of
 * tracing.  For an unscanned thread performing a store, the new value is remembered by
 * the collector.  For any thread performing a store (whether scanned or not), the old
 * value is remembered by the collector before being overwritten (thus this barrier must be
 * positioned as a pre-store barrier).  For the latter ("Yuasa barrier") aspect of the
 * double barrier, only the first overwritten value needs to be remembered (remembering 
 * others is harmless but not needed), and so we omit synchronization on the reading of the 
 * old value.
 **/
bool 
MM_RealtimeAccessBarrier::preObjectStore(J9VMThread *vmThread, J9Object *destObject, fj9object_t *destAddress, J9Object *value, bool isVolatile)
{
	return preObjectStoreInternal(vmThread, destObject, destAddress, value, isVolatile);
}

/**
 * @copydoc MM_MetronomeAccessBarrier::preObjectStore()
 * 
 * Used for stores into classes
 */
bool
MM_RealtimeAccessBarrier::preObjectStore(J9VMThread *vmThread, J9Object *destClass, J9Object **destAddress, J9Object *value, bool isVolatile)
{
	return preObjectStoreInternal(vmThread, destClass, destAddress, value, isVolatile);
}

/**
 * @copydoc MM_MetronomeAccessBarrier::preObjectStore()
 * 
 * Used for stores into internal structures
 */
bool
MM_RealtimeAccessBarrier::preObjectStore(J9VMThread *vmThread, J9Object **destAddress, J9Object *value, bool isVolatile)
{
	return preObjectStoreInternal(vmThread, destAddress, value, isVolatile);
}

/**
 * Enables the double barrier on the provided thread.
 */
void
MM_RealtimeAccessBarrier::setDoubleBarrierActiveOnThread(MM_EnvironmentBase* env)
{
	MM_GCExtensions::getExtensions(env)->sATBBarrierRememberedSet->preserveLocalFragmentIndex(env, &(((J9VMThread *)env->getLanguageVMThread())->sATBBarrierRememberedSetFragment));
}

/**
 * Disables the double barrier on the provided thread.
 */
void
MM_RealtimeAccessBarrier::setDoubleBarrierInactiveOnThread(MM_EnvironmentBase* env)
{
	MM_GCExtensions::getExtensions(env)->sATBBarrierRememberedSet->restoreLocalFragmentIndex(env, &(((J9VMThread *)env->getLanguageVMThread())->sATBBarrierRememberedSetFragment));
}

void
MM_RealtimeAccessBarrier::initializeForNewThread(MM_EnvironmentBase* env)
{
	MM_GCExtensions* extensions = MM_GCExtensions::getExtensions(env);
	extensions->sATBBarrierRememberedSet->initializeFragment(env, &(((J9VMThread *)env->getLanguageVMThread())->sATBBarrierRememberedSetFragment));
	if (isDoubleBarrierActive()) {
		setDoubleBarrierActiveOnThread(env);
	}
}

/* TODO: meter this scanning and include into utilization tracking */
void
MM_RealtimeAccessBarrier::scanContiguousArray(MM_EnvironmentRealtime *env, J9IndexableObject *objectPtr)
{
	bool const compressed = env->compressObjectReferences();
	J9JavaVM *vm = (J9JavaVM *)env->getLanguageVM();
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if(_realtimeGC->getRealtimeDelegate()->isDynamicClassUnloadingEnabled()) {
		rememberObject(env, (J9Object *)objectPtr);
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */		

	/* if NUA is enabled, separate path for contiguous arrays */
	fj9object_t *scanPtr = (fj9object_t*) _extensions->indexableObjectModel.getDataPointerForContiguous(objectPtr);
	fj9object_t *endScanPtr = GC_SlotObject::addToSlotAddress(scanPtr, _extensions->indexableObjectModel.getSizeInElements(objectPtr), compressed);

	while(scanPtr < endScanPtr) {
		/* since this is done from an external thread, we do not markObject, but rememberObject */
		GC_SlotObject slotObject(vm->omrVM, scanPtr);
		J9Object *field = slotObject.readReferenceFromSlot();
		rememberObject(env, field);
		scanPtr = GC_SlotObject::addToSlotAddress(scanPtr, 1, compressed);
	}
	/* this method assumes the array is large enough to set scan bit */
	_markingScheme->setScanAtomic((J9Object *)objectPtr);
}

bool 
MM_RealtimeAccessBarrier::markAndScanContiguousArray(MM_EnvironmentRealtime *env, J9IndexableObject *objectPtr)
{
	UDATA arrayletSize = _extensions->indexableObjectModel.arrayletSize(objectPtr, /* arraylet index */ 0);
	
	/* Sufficiently large to have a scan bit? */
	if (arrayletSize < _extensions->minArraySizeToSetAsScanned) {
		return false;
	} else if (!_markingScheme->isScanned((J9Object *)objectPtr)) {
		/* No, not scanned yet. We are going to mark it and scan right away */
		_markingScheme->markAtomic((J9Object *)objectPtr);
		/* The array might have been marked already (which means it will be scanned soon,
		 * or even being scanned at the moment). Regardless, we will proceed with scanning it */
		scanContiguousArray(env, objectPtr);
	}
	
	return true;
}

/**
 * Finds opportunities for doing the copy without executing Metronome WriteBarrier.
 * @return ARRAY_COPY_SUCCESSFUL if copy was successful, ARRAY_COPY_NOT_DONE no copy is done
 */
I_32
MM_RealtimeAccessBarrier::backwardReferenceArrayCopyIndex(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots)
{
	MM_EnvironmentRealtime *env = MM_EnvironmentRealtime::getEnvironment(vmThread->omrVMThread);
	
	/* a high level caller ensured destObject == srcObject */
		
	if (_extensions->indexableObjectModel.isInlineContiguousArraylet(destObject)) {
		
		if (isBarrierActive(env)) {

			if (!markAndScanContiguousArray(env, destObject)) {
				return ARRAY_COPY_NOT_DONE;
			}
		}

		return doCopyContiguousBackward(vmThread, srcObject, destObject, srcIndex, destIndex, lengthInSlots);

	}
	
	return -2;
}

/**
 * Finds opportunities for doing the copy without executing Metronome WriteBarrier.
 * @return ARRAY_COPY_SUCCESSFUL if copy was successful, ARRAY_COPY_NOT_DONE no copy is done
 */
I_32
MM_RealtimeAccessBarrier::forwardReferenceArrayCopyIndex(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots)
{
	MM_EnvironmentRealtime *env = MM_EnvironmentRealtime::getEnvironment(vmThread->omrVMThread);

	if (_extensions->indexableObjectModel.isInlineContiguousArraylet(destObject)
			&& _extensions->indexableObjectModel.isInlineContiguousArraylet(srcObject)) {
		
		if (isBarrierActive(env) ) {
			
			if ((destObject != srcObject) && isDoubleBarrierActiveOnThread(vmThread)) {
				return ARRAY_COPY_NOT_DONE;
			} else {
				if (markAndScanContiguousArray(env, destObject)) {
					return doCopyContiguousForward(vmThread, srcObject, destObject, srcIndex, destIndex, lengthInSlots);
				}
			}
			
		} else {

			return doCopyContiguousForward(vmThread, srcObject, destObject, srcIndex, destIndex, lengthInSlots);
		
		}
	}

	return -2;
}

#endif /* J9VM_GC_REALTIME */


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
#include "clang_comp.h"

#include "j9cfg.h"
#if defined(J9VM_OPT_METHOD_HANDLE)
#include "VM_MethodHandleKinds.h"

#include "MHInterpreter.hpp"
#include "VMAccess.hpp"

#if defined(MH_TRACE)
static const char * names[] = {
	"J9_METHOD_HANDLE_KIND_BOUND",
	"J9_METHOD_HANDLE_KIND_GET_FIELD",
	"J9_METHOD_HANDLE_KIND_GET_STATIC_FIELD",
	"J9_METHOD_HANDLE_KIND_PUT_FIELD",
	"J9_METHOD_HANDLE_KIND_PUT_STATIC_FIELD",
	"J9_METHOD_HANDLE_KIND_VIRTUAL",
	"J9_METHOD_HANDLE_KIND_STATIC",
	"J9_METHOD_HANDLE_KIND_SPECIAL",
	"J9_METHOD_HANDLE_KIND_CONSTRUCTOR",
	"J9_METHOD_HANDLE_KIND_INTERFACE",
	"J9_METHOD_HANDLE_KIND_COLLECT",
	"J9_METHOD_HANDLE_KIND_INVOKE_EXACT",
	"J9_METHOD_HANDLE_KIND_INVOKE_GENERIC",
	"J9_METHOD_HANDLE_KIND_ASTYPE",
	"J9_METHOD_HANDLE_KIND_DYNAMIC_INVOKER",
	"J9_METHOD_HANDLE_KIND_FILTER_RETURN",
	"J9_METHOD_HANDLE_KIND_EXPLICITCAST",
	"J9_METHOD_HANDLE_KIND_VARARGS_COLLECTOR",
	"J9_METHOD_HANDLE_KIND_PASSTHROUGH",
	"J9_METHOD_HANDLE_KIND_SPREAD",
	"J9_METHOD_HANDLE_KIND_INSERT",
	"J9_METHOD_HANDLE_KIND_PERMUTE",
	"J9_METHOD_HANDLE_KIND_CONSTANT_OBJECT",
	"J9_METHOD_HANDLE_KIND_CONSTANT_INT",
	"J9_METHOD_HANDLE_KIND_CONSTANT_FLOAT",
	"J9_METHOD_HANDLE_KIND_CONSTANT_LONG",
	"J9_METHOD_HANDLE_KIND_CONSTANT_DOUBLE",
	"J9_METHOD_HANDLE_KIND_FOLD_HANDLE",
	"J9_METHOD_HANDLE_KIND_GUARD_WITH_TEST",
	"J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS",
	"J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_EXACT",
	"J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_GENERIC",
	"",
	"J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS_WITH_COMBINER",
};
#endif /* MH_TRACE */

VM_BytecodeAction
VM_MHInterpreter::dispatchLoop(j9object_t methodHandle)
{
	VM_BytecodeAction nextAction = EXECUTE_BYTECODE;
	J9Method *method = NULL;

	while(true) {
		U_32 kind = getMethodHandleKind(methodHandle);
		Assert_VM_mhStackHandleMatch(doesMHandStackMHMatch(methodHandle));
#if defined(MH_TRACE)
		printf("#(%p)# dispatchLoop: MH=%p\tkind=%x\t%s\n", _currentThread, methodHandle, kind, names[kind]);
#endif
		switch(kind) {
		case J9_METHOD_HANDLE_KIND_CONSTANT_OBJECT:
			dropAllArgumentsLeavingMH(methodHandle);
			*((j9object_t *) _currentThread->sp) = J9VMJAVALANGINVOKECONSTANTOBJECTHANDLE_VALUE(_currentThread, methodHandle);
			goto returnFromSend;
		case J9_METHOD_HANDLE_KIND_CONSTANT_INT:
			dropAllArgumentsLeavingMH(methodHandle);
			*((I_32 *) _currentThread->sp) = J9VMJAVALANGINVOKECONSTANTINTHANDLE_VALUE(_currentThread, methodHandle);
			goto returnFromSend;
		case J9_METHOD_HANDLE_KIND_CONSTANT_FLOAT:
			dropAllArgumentsLeavingMH(methodHandle);
			*((U_32 *) _currentThread->sp) = J9VMJAVALANGINVOKECONSTANTFLOATHANDLE_VALUE(_currentThread, methodHandle);
			goto returnFromSend;
		case J9_METHOD_HANDLE_KIND_CONSTANT_LONG:
			dropAllArgumentsLeavingMH(methodHandle);
			_currentThread->sp -= 1;
			*((I_64 *) _currentThread->sp) = J9VMJAVALANGINVOKECONSTANTLONGHANDLE_VALUE(_currentThread, methodHandle);
			goto returnFromSend;
		case J9_METHOD_HANDLE_KIND_CONSTANT_DOUBLE:
			dropAllArgumentsLeavingMH(methodHandle);
			_currentThread->sp -= 1;
			*((U_64 *) _currentThread->sp) = J9VMJAVALANGINVOKECONSTANTDOUBLEHANDLE_VALUE(_currentThread, methodHandle);
			goto returnFromSend;
		case J9_METHOD_HANDLE_KIND_EXPLICITCAST: /* fall through */
		case J9_METHOD_HANDLE_KIND_ASTYPE:
			methodHandle = convertArgumentsForAsType(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}
			break;
		case J9_METHOD_HANDLE_KIND_SPREAD:
			methodHandle = spreadForAsSpreader(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}
			break;
		case J9_METHOD_HANDLE_KIND_PERMUTE:
			methodHandle = permuteForPermuteHandle(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}
			break;
		case J9_METHOD_HANDLE_KIND_INSERT:
			methodHandle = insertArgumentsForInsertHandle(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}
			break;
		case J9_METHOD_HANDLE_KIND_PASSTHROUGH: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			methodHandle = J9VMJAVALANGINVOKEPASSTHROUGHHANDLE_EQUIVALENT(_currentThread, methodHandle);
			((j9object_t *)_currentThread->sp)[slotCount] = methodHandle;
			break;
		}
		case J9_METHOD_HANDLE_KIND_VARARGS_COLLECTOR: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			methodHandle = J9VMJAVALANGINVOKEVARARGSCOLLECTORHANDLE_NEXT(_currentThread, methodHandle);
			((j9object_t *)_currentThread->sp)[slotCount] = methodHandle;
			break;
		}
		case J9_METHOD_HANDLE_KIND_STATIC: {
			methodHandle = initializeClassIfNeeded(methodHandle);
			/* fall through */
		}
		case J9_METHOD_HANDLE_KIND_SPECIAL: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			shiftStackedArguments(slotCount);
			if (J9_METHOD_HANDLE_KIND_SPECIAL == kind) {
				if (NULL == ((j9object_t*)_currentThread->sp)[slotCount]) {
					nextAction = THROW_NPE;
					goto done;
				}
			}
			_currentThread->sp += 1;
			method = (J9Method *)getVMSlot(methodHandle);
			goto runMethod;
		}
		case J9_METHOD_HANDLE_KIND_VIRTUAL: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			shiftStackedArguments(slotCount);
			j9object_t receiver = ((j9object_t*)_currentThread->sp)[slotCount];
			if (NULL != receiver) {
				_currentThread->sp += 1;
				J9Class *clazz = J9OBJECT_CLAZZ(_currentThread, receiver);
				method = *(J9Method **)((UDATA)clazz + getVMSlot(methodHandle));
				goto runMethod;
			}
			nextAction = THROW_NPE;
			goto done;
		}
		case J9_METHOD_HANDLE_KIND_INTERFACE: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			shiftStackedArguments(slotCount);
			j9object_t receiver = ((j9object_t*)_currentThread->sp)[slotCount];
			if (NULL != receiver) {
				_currentThread->sp += 1;
				J9Class *receiverClazz = J9OBJECT_CLAZZ(_currentThread, receiver);
				J9Class *interfaceClazz = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9VMJAVALANGINVOKEPRIMITIVEHANDLE_REFERENCECLASS(_currentThread, methodHandle));
				method = convertITableIndexToVirtualMethod(receiverClazz, interfaceClazz, getVMSlot(methodHandle));
				if (NULL != method) {
					goto runMethod;
				}
				prepareForExceptionThrow(_currentThread);
				setCurrentExceptionUTF(_currentThread, J9VMCONSTANTPOOL_JAVALANGINCOMPATIBLECLASSCHANGEERROR, NULL);
				goto throwCurrentException;
			}
			nextAction = THROW_NPE;
			goto done;
		}

		case J9_METHOD_HANDLE_KIND_BOUND: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			j9object_t receiver = J9VMJAVALANGINVOKERECEIVERBOUNDHANDLE_RECEIVER(_currentThread, methodHandle);
			U_32 modifiers = getPrimitiveHandleModifiers(methodHandle);
			method = (J9Method*)getVMSlot(methodHandle);
			if (J9AccStatic != (modifiers & J9AccStatic)) {
				if (NULL == receiver) {
					nextAction = THROW_NPE;
					goto done;
				}
			}
			((j9object_t*)_currentThread->sp)[slotCount] = receiver;
			goto runMethod;
		}
		case J9_METHOD_HANDLE_KIND_DYNAMIC_INVOKER: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			j9object_t callsite = J9VMJAVALANGINVOKEDYNAMICINVOKERHANDLE_SITE(_currentThread, methodHandle);
			if (isVolatileCallsite(callsite)) {
				methodHandle = J9VMJAVALANGINVOKEVOLATILECALLSITE_TARGET(_currentThread, callsite);
				VM_AtomicSupport::readBarrier();
			} else {
				methodHandle = J9VMJAVALANGINVOKEMUTABLECALLSITE_TARGET(_currentThread, callsite);
				/* MutableCallSite.target is currently volatile - therefore readbarrier required */
				VM_AtomicSupport::readBarrier();
			}
			((j9object_t*)_currentThread->sp)[slotCount] = methodHandle;
			break;
		}
		case J9_METHOD_HANDLE_KIND_INVOKE_EXACT: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			shiftStackedArguments(slotCount);
			j9object_t receiver = ((j9object_t*)_currentThread->sp)[slotCount];
			_currentThread->sp += 1;
			if (NULL != receiver) {
				j9object_t nextType = J9VMJAVALANGINVOKEINVOKEEXACTHANDLE_NEXTTYPE(_currentThread, methodHandle);
				j9object_t receiverType = getMethodHandleMethodType(receiver);
				if (receiverType == nextType) {
					methodHandle = receiver;
					break;
				}
				nextAction = THROW_WRONG_METHOD_TYPE;
			} else {
				/* receiver == null */
				nextAction = THROW_NPE;
			}
			goto done;
		}
		case J9_METHOD_HANDLE_KIND_INVOKE_GENERIC: {
			methodHandle = doInvokeGeneric(methodHandle);

			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			} else if (NULL == methodHandle) {
				nextAction = THROW_NPE;
				goto done;
			}
			break;
		}
		case J9_METHOD_HANDLE_KIND_FILTER_RETURN: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			j9object_t returnFilter = J9VMJAVALANGINVOKEFILTERRETURNHANDLE_FILTER(_currentThread, methodHandle);
			insertPlaceHolderFrame(slotCount, returnFilter, J9VMJAVALANGINVOKEMETHODHANDLE_RETURNFILTERPLACEHOLDER_METHOD(_vm));
			methodHandle = J9VMJAVALANGINVOKECONVERTHANDLE_NEXT(_currentThread, methodHandle);
			((j9object_t *)_currentThread->sp)[slotCount] = methodHandle;
			break;
		}
		case J9_METHOD_HANDLE_KIND_FOLD_HANDLE: {
			methodHandle = foldForFoldArguments(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}
			break;
		}
		case J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS_WITH_COMBINER: {
			methodHandle = filterArgumentsWithCombiner(methodHandle);
			if (VM_VMHelpers::exceptionPending(_currentThread)) {
				goto throwCurrentException;
			}	
			break;
		}
		case J9_METHOD_HANDLE_KIND_GUARD_WITH_TEST: {
			/* Get GWT_Handle.type and GWT_Handle.type.argSlots (GWT - GuardWithTest) */
			/* [... GWT_Handle args] */
			j9object_t guardType = getMethodHandleMethodType(methodHandle);
			U_32 guardArgSlots = getMethodTypeArgSlots(guardType);

			/* [... GWT_Handle args descriptionBytes MethodTypeFrame] */
			UDATA *spPriorToFrameBuild = _currentThread->sp;
			(void)buildMethodTypeFrame(_currentThread, guardType);

			/* [... GWT_Handle args descriptionBytes MethodTypeFrame GWT_Handle args] */
			_currentThread->sp -= (guardArgSlots  + 1);
			memcpy(_currentThread->sp, spPriorToFrameBuild, sizeof(UDATA) * (guardArgSlots+1));

			/* [... GWT_Handle args descriptionBytes MethodTypeFrame GWT_Handle PlaceHolderFrame GWT_Handle args] */
			insertPlaceHolderFrame(guardArgSlots, methodHandle, J9VMJAVALANGINVOKEMETHODHANDLE_GUARDWITHTESTPLACEHOLDER_METHOD(_vm));

			/* Get GWT_Handle.guard (testHandle), testHandle.type and testHandle.type.argSlots */
			methodHandle = J9VMJAVALANGINVOKEGUARDWITHTESTHANDLE_GUARD(_currentThread, methodHandle);
			j9object_t testType = getMethodHandleMethodType(methodHandle);
			U_32 testArgSlots = getMethodTypeArgSlots(testType);

			/* Replace GWT_Handle with testHandle and adjust sp according to testHandle.type.argSlots */
			/* [... GWT_Handle args descriptionBytes MethodTypeFrame GWT_Handle PlaceHolderFrame testHandle args] */
			((j9object_t *)_currentThread->sp)[guardArgSlots] = methodHandle;
			_currentThread->sp += (guardArgSlots - testArgSlots);
			break;
		}
		case J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS: {
			/* Get FAH.type and FAH.type.argSlots [FAH (parent) - FilterArgumentsHandle] */
			/* [... FAH args] */
			j9object_t parentHandle = methodHandle;
			j9object_t parentType = getMethodHandleMethodType(methodHandle);

			j9object_t filters = J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_FILTERS(_currentThread, methodHandle);
			U_32 filtersLength = J9INDEXABLEOBJECT_SIZE(_currentThread, filters);
			U_32 startPosition = (U_32) J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_STARTPOS(_currentThread, methodHandle);

			methodHandle = J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_NEXT(_currentThread, methodHandle);
			j9object_t nextType = getMethodHandleMethodType(methodHandle);
			U_32 nextArgSlots = getMethodTypeArgSlots(nextType);

			/* Check if there are filters to be applied */
			Assert_VM_true(filtersLength > 0);

			/* [... FAH args MTFrame] (MTFrame - descriptionBytes + MethodTypeFrame) */
			(void)buildMethodTypeFrame(_currentThread, parentType);

			/* [... FAH args MTFrame FAH.next UpdatedArgs] */
			UDATA *nextPtr = _currentThread->sp - 1;  /* Store pointer to FAH.next */
			_currentThread->sp -= (nextArgSlots + 1);
			((j9object_t *)_currentThread->sp)[nextArgSlots] = methodHandle;
			memset(_currentThread->sp, 0, nextArgSlots * sizeof(UDATA *));

			/* [... FAH args MTFrame FAH.next UpdatedArgs MTFrame] */
			J9SFMethodTypeFrame *frameNext = buildMethodTypeFrame(_currentThread, nextType);

			/* [... FAH args MTFrame FAH.next UpdatedArgs MTFrame FAH index parentOffset nextOffset PlaceHolderFrame FAH] */
			_currentThread->sp -= 1;
			insertFourArgumentPlaceHolderFrame(0, parentHandle, 0, 0, 0, J9VMJAVALANGINVOKEMETHODHANDLE_FILTERARGUMENTSPLACEHOLDER_METHOD(_vm));

			/* Accessing FAH.type.arguments[] and its attributes for future operations */
			j9object_t parentTypeArguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, parentType);
			UDATA *parentPtr = UNTAGGED_A0(frameNext);  /* Store pointer to parent */

			/* Copy identical args between parent and next */
			J9Class *currentClass = NULL;
			U_32 offset = 0;
			for (U_32 i = 0; i < startPosition; i++) {
				currentClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, parentTypeArguments, i));
				*--nextPtr = *--parentPtr;
				offset += 1;
				if ((_vm->doubleReflectClass == currentClass) || (_vm->longReflectClass == currentClass)) {
					*--nextPtr = *--parentPtr;
					offset += 1;
				}
			}

			UDATA *offsetPtr = _currentThread->arg0EA - 2;  /* Pointer to parentOffset */
			*(U_32 *)(offsetPtr - 1) = offset;  /* Update nextOffset */

			/* [... FAH args MTFrame FAH.next UpdatedArgs MTFrame FAH index parentOffset nextOffset PlaceHolderFrame FAH.filters[0] arg] */
			J9Class *startClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, parentTypeArguments, startPosition));
			methodHandle = J9JAVAARRAYOFOBJECT_LOAD(_currentThread, filters, 0);
			*(j9object_t *)_currentThread->sp = methodHandle;
			*--(_currentThread->sp) = *--parentPtr;
			offset += 1;
			if ((_vm->doubleReflectClass == startClass) || (_vm->longReflectClass == startClass)) {
				*--(_currentThread->sp) = *--parentPtr;
				offset += 1;
			}

			*(U_32 *) offsetPtr = offset; /* Update parentOffset */
			break;
		}
		case J9_METHOD_HANDLE_KIND_CONSTRUCTOR: {
			methodHandle = initializeClassIfNeeded(methodHandle);
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			J9Class *allocateClass = J9VM_J9CLASS_FROM_HEAPCLASS(
					_currentThread,
					J9VMJAVALANGINVOKEPRIMITIVEHANDLE_REFERENCECLASS(_currentThread, methodHandle));
			method = (J9Method*)getVMSlot(methodHandle);
			j9object_t newObject = _objectAllocate->inlineAllocateObject(_currentThread, allocateClass);
			if (NULL == newObject) {
				UDATA *spPriorToFrameBuild = _currentThread->sp;
				J9SFMethodTypeFrame * frame = buildMethodTypeFrame(_currentThread, type);
				newObject = _vm->memoryManagerFunctions->J9AllocateObject(
						_currentThread,
						allocateClass,
						J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
				if (NULL == newObject) {
					goto throwHeapOOM;
				}
				restoreMethodTypeFrame(frame, spPriorToFrameBuild);
			}
			insertPlaceHolderFrame(slotCount, newObject, J9VMJAVALANGINVOKEMETHODHANDLERESOLVER_CONSTRUCTORPLACEHOLDER_METHOD(_vm));
			goto runMethod;
		}
		case J9_METHOD_HANDLE_KIND_GET_FIELD: {
			UDATA const objectHeaderSize = J9VMTHREAD_OBJECT_HEADER_SIZE(_currentThread);
			j9object_t type = getMethodHandleMethodType(methodHandle);
			UDATA fieldOffset = getVMSlot(methodHandle);
			J9Class *fieldClass = getMethodTypeReturnClass(type);
			U_32 modifiers = getPrimitiveHandleModifiers(methodHandle);
			j9object_t object = *(j9object_t *)_currentThread->sp;
			_currentThread->sp += 1;
			if (NULL == object) {
				nextAction = THROW_NPE;
				goto done;
			}
			bool isVolatile = (J9AccVolatile == (modifiers & J9AccVolatile));
			fieldOffset += objectHeaderSize;
			if (J9ROMCLASS_IS_PRIMITIVE_TYPE(fieldClass->romClass)) {
				if (8 == ((J9ROMReflectClass *)(fieldClass->romClass))->elementSize) {
					_currentThread->sp -= 1;
					*(U_64*)_currentThread->sp = 
						_objectAccessBarrier->inlineMixedObjectReadU64(_currentThread, object, fieldOffset, isVolatile);
				} else {
					*(U_32*)_currentThread->sp = 
						_objectAccessBarrier->inlineMixedObjectReadU32(_currentThread, object, fieldOffset, isVolatile);
				}
			} else {
				*(j9object_t*)_currentThread->sp = 
					_objectAccessBarrier->inlineMixedObjectReadObject(_currentThread, object, fieldOffset, isVolatile);
			}
			goto returnFromSend;
		}
		case J9_METHOD_HANDLE_KIND_PUT_FIELD: {
			UDATA const objectHeaderSize = J9VMTHREAD_OBJECT_HEADER_SIZE(_currentThread);
			j9object_t type = getMethodHandleMethodType(methodHandle);
			UDATA fieldOffset = getVMSlot(methodHandle);
			/* Loading index 1 because MethodType has form: (instanceClass, fieldClass)V */
			j9object_t fieldClassObject =
				_objectAccessBarrier->inlineIndexableObjectReadObject(_currentThread, getMethodTypeArguments(type), 1);
			J9Class *fieldClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, fieldClassObject);
			U_32 modifiers = getPrimitiveHandleModifiers(methodHandle);
			fieldOffset += objectHeaderSize;
			bool isVolatile = (J9AccVolatile == (modifiers & J9AccVolatile));
			if (J9ROMCLASS_IS_PRIMITIVE_TYPE(fieldClass->romClass)) {
				if (8 == ((J9ROMReflectClass *)(fieldClass->romClass))->elementSize) {
					j9object_t objectref = *(j9object_t*)(_currentThread->sp + 2);
					if (NULL == objectref) {
						nextAction = THROW_NPE;
						goto done;
					}
					_objectAccessBarrier->inlineMixedObjectStoreU64(_currentThread, objectref, fieldOffset, *(U_64*)_currentThread->sp, isVolatile);
					_currentThread->sp += 3;
				} else {
					j9object_t objectref = *(j9object_t*)(_currentThread->sp + 1);
					if (NULL == objectref) {
						nextAction = THROW_NPE;
						goto done;
					}
					_objectAccessBarrier->inlineMixedObjectStoreU32(_currentThread, objectref, fieldOffset, *(U_32*)_currentThread->sp, isVolatile);
					_currentThread->sp += 2;
				}
			} else {
				j9object_t objectref = *(j9object_t*)(_currentThread->sp + 1);
				if (NULL == objectref) {
					nextAction = THROW_NPE;
					goto done;
				}
				_objectAccessBarrier->inlineMixedObjectStoreObject(_currentThread, objectref, fieldOffset, *(j9object_t*)_currentThread->sp, isVolatile);
				_currentThread->sp += 2;
			}
			_currentThread->sp += 1; /* remove MethodHandle */
			goto returnFromSend;
		}
		case J9_METHOD_HANDLE_KIND_GET_STATIC_FIELD: {
			methodHandle = initializeClassIfNeeded(methodHandle);
			J9Class* defc = getPrimitiveHandleDefc(methodHandle);
			UDATA srcAddress = getVMSlot(methodHandle);
			srcAddress &= ~J9_SUN_FIELD_OFFSET_MASK;
			srcAddress += (UDATA)defc->ramStatics;
			U_32 modifiers = getPrimitiveHandleModifiers(methodHandle);
			j9object_t type = getMethodHandleMethodType(methodHandle);
			J9Class *fieldClass = getMethodTypeReturnClass(type);
			bool isVolatile = (J9StaticFieldRefVolatile == (modifiers & J9StaticFieldRefVolatile));
			{
				if (J9ROMCLASS_IS_PRIMITIVE_TYPE(fieldClass->romClass)) {
					if (8 == ((J9ROMReflectClass *)(fieldClass->romClass))->elementSize) {
						_currentThread->sp -=1;
						*(U_64*)_currentThread->sp =
							_objectAccessBarrier->inlineStaticReadU64(_currentThread, defc, (U_64 *)srcAddress, isVolatile);
					} else {
						*(U_32*)_currentThread->sp =
							_objectAccessBarrier->inlineStaticReadU32(_currentThread, defc, (U_32*)srcAddress, isVolatile);
					}
				} else {
					*(j9object_t*)_currentThread->sp =
						_objectAccessBarrier->inlineStaticReadObject(_currentThread, defc, (j9object_t*)srcAddress, isVolatile);
				}
			}
			goto returnFromSend;
		}
		case J9_METHOD_HANDLE_KIND_PUT_STATIC_FIELD: {
			methodHandle = initializeClassIfNeeded(methodHandle);
			j9object_t type = getMethodHandleMethodType(methodHandle);
			J9Class* defc = getPrimitiveHandleDefc(methodHandle);
			UDATA srcAddress = getVMSlot(methodHandle);
			srcAddress &= ~J9_SUN_FIELD_OFFSET_MASK;
			srcAddress += (UDATA)defc->ramStatics;
			j9object_t fieldClassObject =
					J9JAVAARRAYOFOBJECT_LOAD(_currentThread, getMethodTypeArguments(type), 0);
			J9Class *fieldClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, fieldClassObject);
			U_32 modifiers = getPrimitiveHandleModifiers(methodHandle);
			bool isVolatile = (J9StaticFieldRefVolatile == (modifiers & J9StaticFieldRefVolatile));
			{
				if (J9ROMCLASS_IS_PRIMITIVE_TYPE(fieldClass->romClass)) {
					if (8 == ((J9ROMReflectClass *)(fieldClass->romClass))->elementSize) {
						_objectAccessBarrier->inlineStaticStoreU64(_currentThread, defc, (U_64*)srcAddress, *(U_64*)_currentThread->sp, isVolatile);
						_currentThread->sp += 3;
					} else {
						_objectAccessBarrier->inlineStaticStoreU32(_currentThread, defc, (U_32*)srcAddress, *(U_32*)_currentThread->sp, isVolatile);
						_currentThread->sp += 2;
					}
				} else {
					_objectAccessBarrier->inlineStaticStoreObject(_currentThread, defc, (j9object_t*)srcAddress, *(j9object_t*)_currentThread->sp, isVolatile);
					_currentThread->sp += 2;
				}
			}
			goto returnFromSend;
		}
		case J9_METHOD_HANDLE_KIND_COLLECT: {
			BOOLEAN foundHeapOOM = FALSE;
			methodHandle = collectForAsCollector(methodHandle, &foundHeapOOM);
			if (foundHeapOOM) {
				goto throwHeapOOM;
			}
			break;
		}
		case J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_EXACT:
			/* fallthrough */
		case J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_GENERIC: {
			j9object_t type = getMethodHandleMethodType(methodHandle);
			U_32 slotCount = getMethodTypeArgSlots(type);
			j9object_t varHandle = ((j9object_t*)_currentThread->sp)[slotCount - 1];

			if (NULL == varHandle) {
				nextAction = THROW_NPE;
				goto done;
			}

			I_32 operation = J9VMJAVALANGINVOKEVARHANDLEINVOKEHANDLE_OPERATION(_currentThread, methodHandle);

			/* Get MethodHandle for this operation from the VarHandle's handleTable */
			j9object_t handleTable = J9VMJAVALANGINVOKEVARHANDLE_HANDLETABLE(_currentThread, varHandle);
			j9object_t methodHandleFromTable = J9JAVAARRAYOFOBJECT_LOAD(_currentThread, handleTable, operation);

			if (NULL == methodHandleFromTable) {
				/* Building a method type (MT) frame makes the stack walkable since looking up the class below may
				 * cause a GC or another exception. Also, the MT frame will make it easier to debug and service the
				 * error as the VarHandle will be on the stack and findable with DDR. The MT frame does not need to
				 * be restored since throwing the exception will handle it appropriately.
				 */
				buildMethodTypeFrame(_currentThread, type);
				prepareExceptionUsingClassName(_currentThread, "java/lang/UnsupportedOperationException");
				goto throwCurrentException;
			}

			j9object_t handleTypeFromTable = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandleFromTable);
			j9object_t accessModeType = J9VMJAVALANGINVOKEVARHANDLEINVOKEHANDLE_ACCESSMODETYPE(_currentThread, methodHandle);

			if (accessModeType == handleTypeFromTable) {
				methodHandle = methodHandleFromTable;
			} else if (J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_GENERIC == kind) {
				UDATA * spPriorToFrameBuild = _currentThread->sp;

				/* We need to do a callin to get an asType handle */
				J9SFMethodTypeFrame* currentTypeFrame = buildMethodTypeFrame(_currentThread, type);

				/* Convert absolute values to A0 relative offsets */
				IDATA spOffset = spPriorToFrameBuild - _currentThread->arg0EA;
				IDATA frameOffset = (UDATA*)currentTypeFrame - _currentThread->arg0EA;

				sendForGenericInvoke(_currentThread, methodHandleFromTable, accessModeType, FALSE /* dropFirstArg */);
				methodHandle = (j9object_t)_currentThread->returnValue;

				if (VM_VMHelpers::exceptionPending(_currentThread)) {
					goto throwCurrentException;
				}

				/* Convert A0 relative offsets to absolute values */
				spPriorToFrameBuild = _currentThread->arg0EA + spOffset;
				currentTypeFrame = (J9SFMethodTypeFrame*)(_currentThread->arg0EA + frameOffset);

				/* Pop the frame */
				_currentThread->literals = currentTypeFrame->savedCP;
				_currentThread->pc = currentTypeFrame->savedPC;
				_currentThread->arg0EA = UNTAGGED_A0(currentTypeFrame);
				_currentThread->sp = spPriorToFrameBuild;
			} else {
				nextAction = THROW_WRONG_METHOD_TYPE;
				goto done;
			}

			/* Get VarHandle again in case GC moved it */
			varHandle = ((j9object_t*)_currentThread->sp)[slotCount - 1];

			/* Remove old handle from stack */
			shiftStackedArguments(slotCount);

			/* Put VarHandle as last argument */
			*(j9object_t*)_currentThread->sp = varHandle;

			/* Interpret access mode handle */
			((j9object_t*)_currentThread->sp)[slotCount] = methodHandle;
			break;
		}
		default:
			Assert_VM_unreachable();
			break;
		}
		void * compiledEntryPoint = VM_VMHelpers::methodHandleCompiledEntryPoint(_vm, _currentThread, methodHandle);
		if (NULL != compiledEntryPoint) {
			_currentThread->tempSlot = (UDATA)methodHandle;
			_currentThread->floatTemp1 = compiledEntryPoint;
			nextAction = GOTO_RUN_METHODHANDLE_COMPILED;
#if defined(MH_TRACE)
	printf("#(%p)# dispatchLoop run MH %p compiled.  Entrypoint = %p\n", _currentThread, methodHandle, compiledEntryPoint);
#endif
			goto done;
		}
	}

runMethod: {
#if defined(MH_TRACE)
	printf("#(%p)# dispatchLoop run method=%p\n", _currentThread, method);
#endif
	_currentThread->tempSlot = (UDATA)method;
	nextAction = GOTO_RUN_METHOD_FROM_METHOD_HANDLE;
	goto done;
}

returnFromSend: {
	nextAction = returnFromSend();
	goto done;
}

throwCurrentException: {
#if defined(MH_TRACE)
	printf("#(%p)# dispatchLoop throw current exception %p\n", _currentThread, _currentThread->currentException);
#endif
	nextAction = GOTO_THROW_CURRENT_EXCEPTION;
	goto done;
}

throwHeapOOM: {
#if defined(MH_TRACE)
	printf("#(%p)# dispatchLoop throw heap OOM\n", _currentThread);
#endif
	nextAction = THROW_HEAP_OOM;
	goto done;
}

done:
#if defined(MH_TRACE)
	printf("#(%p)# dispatchLoop done - nextAction=%d\n", _currentThread, (I_32)nextAction);
#endif
	return nextAction;
}

VM_BytecodeAction
VM_MHInterpreter::impdep1()
{
	VM_BytecodeAction rc = EXECUTE_BYTECODE;
	/* NOTE: bp calculation assumes that there is only ever a single argument on the stack */
	UDATA *bp = _currentThread->arg0EA - (sizeof(J9SFStackFrame)/sizeof(UDATA*));
	J9SFStackFrame *frame = (J9SFStackFrame*)(bp);
	if (J9VMJAVALANGINVOKEMETHODHANDLERESOLVER_CONSTRUCTORPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		/* restore frame for constructor: leave objectRef on stack & increment PC */
		_currentThread->sp = _currentThread->arg0EA;
		_currentThread->literals = frame->savedCP;
		_currentThread->pc = frame->savedPC + 3;
		_currentThread->arg0EA =  UNTAGGED_A0(frame);
	} else if (J9VMJAVALANGINVOKEMETHODHANDLE_RETURNFILTERPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		rc = GOTO_RUN_METHODHANDLE;
		/* Get the filterHandle.type.arguments[0] to determine the kind of value on the stack */
		j9object_t filterHandle = *(j9object_t*)_currentThread->arg0EA;
		j9object_t filterType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, filterHandle);
		j9object_t argTypes = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, filterType);
		UDATA returnSlots = J9INDEXABLEOBJECT_SIZE(_currentThread, argTypes);
		/* Determine how many slots to pop off the stack */
		UDATA returnValue0 = 0;
		UDATA returnValue1 = 0;
		if (0 != returnSlots) {
			j9object_t returnClass = _objectAccessBarrier->inlineIndexableObjectReadObject(_currentThread, argTypes, 0);
			J9Class *argTypeClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, returnClass);
			returnSlots = 1;
			returnValue0 = _currentThread->sp[0];
			if ((_vm->doubleReflectClass == argTypeClass) || (_vm->longReflectClass == argTypeClass)) {
				returnSlots = 2;
				returnValue1 = _currentThread->sp[1];
			}
		}
		_currentThread->sp = _currentThread->arg0EA - returnSlots;
		_currentThread->literals = frame->savedCP;
		_currentThread->pc = frame->savedPC;
		_currentThread->arg0EA = UNTAGGED_A0(frame);
		if (0 != returnSlots) {
			_currentThread->sp[0] = returnValue0;
			if (2 == returnSlots) {
				_currentThread->sp[1] = returnValue1;
			}
		}
		_currentThread->tempSlot = (UDATA) filterHandle;
	} else if (J9VMJAVALANGINVOKEMETHODHANDLE_FILTERARGUMENTSWITHCOMBINERPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		rc = GOTO_RUN_METHODHANDLE;
		_currentThread->tempSlot = (UDATA) replaceReturnValueForFilterArgumentsWithCombiner();
	} else if (J9VMJAVALANGINVOKEMETHODHANDLE_FOLDHANDLEPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		rc = GOTO_RUN_METHODHANDLE;
		_currentThread->tempSlot = (UDATA) insertReturnValueForFoldArguments();
	} else if (J9VMJAVALANGINVOKEMETHODHANDLE_GUARDWITHTESTPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		rc = GOTO_RUN_METHODHANDLE;

		/* [... GWT_Handle args descriptionBytes MethodTypeFrame args GWT_Handle PlaceHolderFrame testHandleReturnValue] */
		j9object_t guardHandle = *(j9object_t*)_currentThread->arg0EA;
		j9object_t guardType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, guardHandle);
		U_32 guardArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, guardType);

		/* Locally store the boolean return value from the testHandle */
		I_32 testReturnValue = *(I_32*)_currentThread->sp;

		/* Set local variables to restore state of the _currentThread during updateVMStruct */
		UDATA *mhPtr = UNTAGGED_A0(frame);

		bp = (UDATA *)(((J9SFStackFrame*)(bp+1)) + 1);

		J9SFMethodTypeFrame *mtFrame = (J9SFMethodTypeFrame*)(bp);
		_currentThread->literals = mtFrame->savedCP;
		_currentThread->pc = mtFrame->savedPC;
		_currentThread->arg0EA = UNTAGGED_A0(mtFrame);
		_currentThread->sp = mhPtr - guardArgSlots;

		/* Overwrite initial GWT_Handle with either GWT_Handle.trueTarget or GWT_Handle.falseTarget based upon testHandleReturnValue */
		j9object_t nextHandle = NULL;

		if (0 == testReturnValue) {
			nextHandle = J9VMJAVALANGINVOKEGUARDWITHTESTHANDLE_FALSETARGET(_currentThread, guardHandle);
		} else {
			nextHandle = J9VMJAVALANGINVOKEGUARDWITHTESTHANDLE_TRUETARGET(_currentThread, guardHandle);
		}

		*(j9object_t*)(mhPtr) = nextHandle;
		_currentThread->tempSlot = (UDATA) nextHandle;
	} else if (J9VMJAVALANGINVOKEMETHODHANDLE_FILTERARGUMENTSPLACEHOLDER_METHOD(_vm) == _currentThread->literals) {
		rc = GOTO_RUN_METHODHANDLE;

		/* [... FAH[1] args MTFrame FAH.next UpdatedArgs MTFrame FAH[2] index parentOffset nextOffset PlaceHolderFrame filterReturnValue] */
		j9object_t parentHandle = *(j9object_t *)_currentThread->arg0EA;
		j9object_t parentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, parentHandle);
		j9object_t parentTypeArguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, parentType);

		j9object_t filters = J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_FILTERS(_currentThread, parentHandle);
		U_32 filtersLength = J9INDEXABLEOBJECT_SIZE(_currentThread, filters);
		U_32 startPosition = (U_32) J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_STARTPOS(_currentThread, parentHandle);

		j9object_t nextHandle = J9VMJAVALANGINVOKEFILTERARGUMENTSHANDLE_NEXT(_currentThread, parentHandle);
		j9object_t nextType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, nextHandle);
		U_32 nextArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, nextType);
		j9object_t nextTypeArguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, nextType);

		U_32 *index = (U_32 *)(_currentThread->arg0EA - 1);
		U_32 *parentOffset = (U_32 *)(_currentThread->arg0EA - 2);
		U_32 *nextOffset = (U_32 *)(_currentThread->arg0EA - 3);

		frame = ((J9SFStackFrame*)(_currentThread->arg0EA - 3)) - 1;  /* Recalculate frame location while taking into account 4 placeHolderMethod arguments */
		J9SFMethodTypeFrame *mtNextFrame = (J9SFMethodTypeFrame*)(_currentThread->arg0EA + 1);

		UDATA *parentPtr = UNTAGGED_A0(mtNextFrame);  /* Pointer to FAH[1] */
		UDATA *nextPtr = UNTAGGED_A0(frame);  /* Pointer to FAH.next */

		/* Update FAH.next arguments using filterReturnValue */
		J9Class *returnClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, nextTypeArguments, (startPosition + *index)));
		U_32 returnArgSlots = 0;
		if (_vm->voidReflectClass != returnClass) {
			returnArgSlots += 1;
			if ((_vm->doubleReflectClass == returnClass) || (_vm->longReflectClass == returnClass)) {
				returnArgSlots += 1;
			}
		}
		parentPtr -= *parentOffset;
		*nextOffset += returnArgSlots;
		nextPtr -= *nextOffset;
		memmove(nextPtr, _currentThread->sp, returnArgSlots * sizeof(UDATA *));
		_currentThread->sp += returnArgSlots;

		/* Get the next non-null filterMH while treating intermediate null filterMHs as identity functions */
		j9object_t currentFilterHandle = NULL;
		J9Class *currentParentClass = NULL;
		U_32 offset = 0;
		U_32 i = (*index + 1);
		for (; i < filtersLength; i++) {
			currentFilterHandle = J9JAVAARRAYOFOBJECT_LOAD(_currentThread, filters, i);
			currentParentClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, parentTypeArguments, (startPosition + i)));
			if (NULL == currentFilterHandle) {
				if (_vm->voidReflectClass != currentParentClass) {
					*--nextPtr = *--parentPtr;
					offset += 1;
					if ((_vm->doubleReflectClass == currentParentClass) || (_vm->longReflectClass == currentParentClass)) {
						*--nextPtr = *--parentPtr;
						offset += 1;
					}
				}
			} else {
				break;
			}
		}
		*parentOffset += offset;
		*nextOffset += offset;
		*index = i;

		if (filtersLength == *index) {  /* All filterMHs have been processed */
			/* Copy trailing parent args into next args */
			for (U_32 i = (*nextOffset + 1); i <= nextArgSlots; i++) {
				*--nextPtr = *--parentPtr;
			}

			UDATA *srcPtr = UNTAGGED_A0(frame);  /* Pointer to FAH.next */
			UDATA *destPtr = UNTAGGED_A0(mtNextFrame);  /* Pointer to FAH[1] */

			/* Restore initial thread state */
			J9SFMethodTypeFrame *mtParentFrame = (J9SFMethodTypeFrame *)(srcPtr + 1);
			_currentThread->literals = mtParentFrame->savedCP;
			_currentThread->pc = mtParentFrame->savedPC;
			_currentThread->arg0EA = UNTAGGED_A0(mtParentFrame);
			_currentThread->sp = destPtr - nextArgSlots;

			/* [... FAH.next UpdatedArgs] */
			memmove(destPtr - nextArgSlots, srcPtr - nextArgSlots, (nextArgSlots + 1) * sizeof(UDATA *));
			_currentThread->tempSlot = (UDATA) nextHandle;
		} else {  /* Prepare next non-null filterMH for processing */
			_currentThread->sp -= 1;
			*(j9object_t *)(_currentThread->sp) = currentFilterHandle;
			_currentThread->tempSlot = (UDATA) currentFilterHandle;

			U_32 nextFilterArgumentSlots = 0;
			if (_vm->voidReflectClass != currentParentClass) {
				nextFilterArgumentSlots += 1;
				if ((_vm->doubleReflectClass == currentParentClass) || (_vm->longReflectClass == currentParentClass)) {
					nextFilterArgumentSlots += 1;
				}
			}
			*parentOffset += nextFilterArgumentSlots;
			parentPtr -= nextFilterArgumentSlots;
			_currentThread->sp -= nextFilterArgumentSlots;
			memmove(_currentThread->sp, parentPtr, nextFilterArgumentSlots * sizeof(UDATA *));

			_currentThread->pc -= 3;  /* Point to impdep1 */
		}
	} else {
		Assert_VM_unreachable();
	}
	return rc;
}

j9object_t
VM_MHInterpreter::convertArgumentsForAsType(j9object_t methodHandle)
{

	j9object_t result = NULL;
	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandle);
	U_32 currentArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	j9object_t nextHandle = J9VMJAVALANGINVOKECONVERTHANDLE_NEXT(_currentThread, methodHandle);
	j9object_t nextType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, nextHandle);
	U_32 nextArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, nextType);
	UDATA explicitCast = (J9VMJAVALANGINVOKEMETHODHANDLE_KIND(_currentThread, methodHandle) == J9_METHOD_HANDLE_KIND_EXPLICITCAST);
	UDATA requiresBoxing = (UDATA)J9VMJAVALANGINVOKECONVERTHANDLE_REQUIRESBOXING(_currentThread, methodHandle);
	UDATA * currentArgs = NULL;
	UDATA * nextArgs = NULL;
	UDATA * finalSP = NULL;
	J9SFMethodTypeFrame * currentTypeFrame = NULL;
	J9SFMethodTypeFrame * nextTypeFrame = NULL;
	UDATA rc = 0;
	ClassCastExceptionData exceptionData;

	memset(&exceptionData, 0, sizeof(ClassCastExceptionData));
	currentArgs = _currentThread->sp + currentArgSlots;
	finalSP = currentArgs - nextArgSlots;

	if (0 != requiresBoxing) {
		/* Lay down a frame to describe the current args */
		currentTypeFrame = buildMethodTypeFrame(_currentThread, currentType);

		/* Reserve space for all of the new arguments, zero them, and drop another method type frame to describe them */
		*--(_currentThread->sp) = (UDATA) nextHandle;
		nextArgs = _currentThread->sp;
		_currentThread->sp -= nextArgSlots;
		memset(_currentThread->sp, 0, nextArgSlots * sizeof(UDATA *));
		nextTypeFrame = buildMethodTypeFrame(_currentThread, nextType);

		/* Convert the arguments */
		rc = convertArguments(currentArgs, &currentTypeFrame->methodType, nextArgs, &nextTypeFrame->methodType, explicitCast, &exceptionData);
		if (NO_EXCEPTION != rc) {
			/* In the case of a return value being non-zero, throw the exception */
			goto fail;
		}

		/* Remove the method type frames */
		_currentThread->literals = currentTypeFrame->savedCP;
		_currentThread->pc = currentTypeFrame->savedPC;
		_currentThread->arg0EA = UNTAGGED_A0(currentTypeFrame);
	} else {
		/* If we don't require boxing, do the operations on the java stack
		 * but don't move the sp until the operations are complete or an exception is thrown
		 */
		UDATA *tempSP = _currentThread->sp;

		/* Reserve space for all of the new arguments, and zero them */
		*--(tempSP) = (UDATA) nextHandle;
		nextArgs = tempSP;
		tempSP -= nextArgSlots;
		memset(tempSP, 0, nextArgSlots * sizeof(UDATA *));

		/* Convert the arguments */
		rc = convertArguments(currentArgs, &currentType, nextArgs, &nextType, explicitCast, &exceptionData);

		if (NO_EXCEPTION != rc) {
			/* In the case of a return value being non-zero, throw the exception.
			 * First we need to make the stack walkable by building a valid frame.
			 */
			buildMethodTypeFrame(_currentThread, currentType);
			goto fail;
		}
	}

	/* Move the new arguments (including the next MethodHandle) into their final position */
	_currentThread->sp = finalSP;
	memmove(finalSP, nextArgs - nextArgSlots, (nextArgSlots + 1) * sizeof(UDATA *));

	/* Return the next handle from its new location on the stack */

	result = (j9object_t) finalSP[nextArgSlots];
done:
	return result;
fail:
	/* In the case of a return value being non-zero, throw the exception */
	if (NULL_POINTER_EXCEPTION == rc) {
		setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else if (CLASS_CAST_EXCEPTION == rc) {
		setClassCastException(_currentThread, exceptionData.currentClass, exceptionData.nextClass);
	} else {
		setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
	}
	goto done;
}

j9object_t
VM_MHInterpreter::doInvokeGeneric(j9object_t methodHandle)
{
	j9object_t castType = J9VMJAVALANGINVOKEINVOKEGENERICHANDLE_CASTTYPE(_currentThread, methodHandle);
	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandle);
	U_32 currentArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	const U_32 slotsOffsetToTargetHandle = currentArgSlots - 1;
	j9object_t targetHandle = *(j9object_t*)(_currentThread->sp + slotsOffsetToTargetHandle);

	/* If receiver handle is null, return directly and a NPE will be thrown by the caller */
	if (targetHandle == NULL) {
		return targetHandle;
	}

	j9object_t targetType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, targetHandle);

	/* There are three cases here:
	 * - The target handle's type correctly matches the stack, we can directly invoke that handle
	 * - A previous asType result correctly matches the stack, we can overwrite the target and invoke that
	 * - We need to do a callin to create the correct as type, we can overwrite the target and invoke that
	 */
	if (targetType != castType) {
		J9SFMethodTypeFrame *currentTypeFrame = NULL;
		UDATA * spPriorToFrameBuild = _currentThread->sp;
		j9object_t cachedHandle = J9VMJAVALANGINVOKEMETHODHANDLE_PREVIOUSASTYPE(_currentThread, targetHandle);
		IDATA spOffset = 0;
		IDATA frameOffset = 0;

		if (cachedHandle != NULL) {
			j9object_t cachedHandleType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, cachedHandle);
			if (cachedHandleType == castType) {
				*(j9object_t*)(_currentThread->sp + slotsOffsetToTargetHandle) = cachedHandle;
				targetHandle = cachedHandle;
				goto done;
			}
		}

		/* We need to do a callin to get an asType handle */
		currentTypeFrame = buildMethodTypeFrame(_currentThread, currentType);

		/* Convert absolute values to A0 relative offsets */
		spOffset = spPriorToFrameBuild - _currentThread->arg0EA;
		frameOffset = (UDATA*)currentTypeFrame - _currentThread->arg0EA;

		sendForGenericInvoke(_currentThread, targetHandle, castType, FALSE);
		targetHandle = (j9object_t)_currentThread->returnValue;

		/* If there's an exception, don't do anything and return the old handle */
		if (NULL != _currentThread->currentException) {
			return *(j9object_t*)(_currentThread->sp + slotsOffsetToTargetHandle);
		}

		/* Convert A0 relative offsets to absolute values */
		spPriorToFrameBuild = _currentThread->arg0EA + spOffset;
		currentTypeFrame = (J9SFMethodTypeFrame*)(_currentThread->arg0EA + frameOffset);

		/* Pop the frame */
		_currentThread->literals = currentTypeFrame->savedCP;
		_currentThread->pc = currentTypeFrame->savedPC;
		_currentThread->arg0EA = UNTAGGED_A0(currentTypeFrame);
		_currentThread->sp = spPriorToFrameBuild;

		/* Swap in the asType */
		*(j9object_t*)(_currentThread->sp + slotsOffsetToTargetHandle) = targetHandle;
	}

done:
	/* Slide the stack down a slot */
	memmove(_currentThread->sp + 1, _currentThread->sp, currentArgSlots*sizeof(UDATA));
	_currentThread->sp += 1;
	return targetHandle;
}

j9object_t
VM_MHInterpreter::spreadForAsSpreader(j9object_t methodHandle)
{
	j9object_t currentType = getMethodHandleMethodType(methodHandle);
	U_32 currentArgSlots = getMethodTypeArgSlots(currentType);
	j9object_t nextMethodHandle = J9VMJAVALANGINVOKESPREADHANDLE_NEXT(_currentThread, methodHandle);
	U_32 spreadCount = (U_32)J9VMJAVALANGINVOKESPREADHANDLE_SPREADCOUNT(_currentThread, methodHandle);
	U_32 spreadPosition = (U_32)J9VMJAVALANGINVOKESPREADHANDLE_SPREADPOSITION(_currentThread, methodHandle);
	j9object_t arrayClass = J9VMJAVALANGINVOKESPREADHANDLE_ARRAYCLASS(_currentThread, methodHandle);
	J9Class *arrayClazz = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, arrayClass);
	j9object_t nextType = getMethodHandleMethodType(nextMethodHandle);
	j9object_t argumentTypes = getMethodTypeArguments(nextType);

	UDATA spreadArrayLength = 0;
	J9Class *argumentClazz = NULL;
	j9object_t arrayObject = NULL;
	UDATA *spOnTopStack = _currentThread->sp;
	UDATA *spSpreadSlot = spOnTopStack;
	UDATA remainingArgSlots = 0;

	/* Replace the SpreadHandle methodhandle on the stack with the next methodhandle */
	((j9object_t*)_currentThread->sp)[currentArgSlots] = nextMethodHandle;

	/* Locate the 1st slot of the spread arguments via the specified position */
	if (0 != currentArgSlots) {
		/* Count the slot number of all arguments before spreadPosition to
		 * determine the 1st slot of the spread arguments.
		 */
		U_32 argumentSlots = getArgSlotsBeforePosition(argumentTypes, spreadPosition);

		/* The first slot of the spread elements and the slot number of remaining arguments */
		remainingArgSlots = currentArgSlots - argumentSlots - 1;
		spSpreadSlot += remainingArgSlots;
	}

	/* Copy the invoke argument at the specified position into the C stack */
	arrayObject = *(j9object_t*)spSpreadSlot;

	/* We can only have a NULL array iff spreadCount is zero */
	if (NULL == arrayObject) {
		if (0 != spreadCount) {
			/* Build a frame so we can throw an exception */
			buildMethodTypeFrame(_currentThread, currentType);
#if JAVA_SPEC_VERSION >= 11
			setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
#else
			setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGILLEGALARGUMENTEXCEPTION, NULL);
#endif /* JAVA_SPEC_VERSION >= 11 */
			goto exitSpreadForAsSpreader;
		}
	} else {
		/* Get the class of the invoke argument at the specified position */
		argumentClazz = J9OBJECT_CLAZZ(_currentThread, arrayObject);
		if (!instanceOfOrCheckCast(argumentClazz, arrayClazz)) {
			buildMethodTypeFrame(_currentThread, currentType);
			setClassCastException(_currentThread, arrayClazz, argumentClazz);
			goto exitSpreadForAsSpreader;
		}

		spreadArrayLength = J9INDEXABLEOBJECT_SIZE(_currentThread, arrayObject);
	}

	/* If the array size doesn't match the number of elements in the array, throw an error */
	if (spreadCount != spreadArrayLength) {
		buildMethodTypeFrame(_currentThread, currentType);
		setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGILLEGALARGUMENTEXCEPTION, NULL);
		goto exitSpreadForAsSpreader;
	}

 	/* There are 2 cases to address the remaining arguments following the array reference:
 	 * 1) If spreadCount is zero, then the stack should be:
 	 *    Before: | next method handle | argument 1 | array reference | argument 3, 4, ...
 	 *    After:  | next method handle | argument 1 | argument 3, 4, ...
 	 *    Note: Initially the slot of array reference (NULL array) exists at the specified position.
 	 *    In such case, all remaining arguments after the array reference must move up by 1 slot
 	 *    to pop off the zero-sized array.
 	 *
 	 * 2) If spreadCount is non-zero, then the stack should be:
 	 *    Before: | next method handle | argument 1 | array reference | argument 3, 4, ...
 	 *    After:  | next method handle | argument 1 | spread argument1, 2, ... | argument 3, 4, ...
 	 *    Note: Initially the slot of array reference exists at the specified position.
 	 *    In such case, all remaining arguments after the array reference must move down by (spreadCount - 1)
 	 *    to make room for spread arguments.
 	 */
	if (0 == spreadCount) {
		/* Case 1: Move up all remaining arguments the array reference
		 * by 1 slot to pop off the zero-sized array.
		 */
		memmove(spOnTopStack + 1, spOnTopStack, remainingArgSlots * sizeof(UDATA));
		spOnTopStack += 1;
	} else {
		UDATA *spOnNewTopStack = spOnTopStack;
		U_32 spreadSlots = spreadCount;

		/* We've already verified that the object is an array and not null */
		Assert_VM_true(NULL != argumentClazz);
		J9ArrayClass *arrayClazz = (J9ArrayClass*)argumentClazz;
		J9Class *componentType = arrayClazz->componentType;
		BOOLEAN isPrimitiveType = J9ROMCLASS_IS_PRIMITIVE_TYPE(componentType->romClass);

		/* Count the total number of slots to be occupied by spread arguments
		 * to determine the new top of stack.
		 */
		if (isPrimitiveType
			&& ((argumentClazz == _vm->longArrayClass) || (argumentClazz == _vm->doubleArrayClass))
		) {
			spreadSlots += spreadCount;
		}
		spOnNewTopStack -= spreadSlots - 1;

		/* Case 2: To store the spread arguments starting from the specified position on stack,
		 * we need to move down all the remaining arguments by spreadCount - 1.
		 */
		memmove(spOnNewTopStack, spOnTopStack, remainingArgSlots * sizeof(UDATA));
		spOnTopStack = spOnNewTopStack;

		/* Check to see if it's an array of objects or primitives */
		if (!isPrimitiveType) {
			U_32 spreadIndex;
			j9object_t* current = (j9object_t*)spSpreadSlot;
			for (spreadIndex = 0; spreadIndex < spreadCount; spreadIndex++) {
				*current = J9JAVAARRAYOFOBJECT_LOAD(_currentThread, arrayObject, spreadIndex);
				current -= 1;
			}
		} else {
			_currentThread->sp = spSpreadSlot + 1;
			primitiveArraySpread(arrayObject, spreadCount, argumentClazz);
		}
	}

	/* Adjust the top of stack after filling up the spread arguments */
	_currentThread->sp = spOnTopStack;

exitSpreadForAsSpreader:
	/* Return the next MethodHandle, this is what gets executed next */
	return nextMethodHandle;
}

j9object_t
VM_MHInterpreter::collectForAsCollector(j9object_t methodHandle, BOOLEAN * foundHeapOOM)
{
	j9object_t currentType = getMethodHandleMethodType(methodHandle);
	U_32 currentArgSlots = getMethodTypeArgSlots(currentType);
	U_32 collectArraySize = (U_32)J9VMJAVALANGINVOKECOLLECTHANDLE_COLLECTARRAYSIZE(_currentThread, methodHandle);
	U_32 collectPosition = (U_32)J9VMJAVALANGINVOKECOLLECTHANDLE_COLLECTPOSITION(_currentThread, methodHandle);
	j9object_t nextMethodHandle = J9VMJAVALANGINVOKECOLLECTHANDLE_NEXT(_currentThread, methodHandle);
	((j9object_t*)_currentThread->sp)[currentArgSlots] = nextMethodHandle;

	UDATA *spPtr = _currentThread->sp;
	UDATA *spOnTopStack = _currentThread->sp;
	UDATA *spCollectSlot = NULL;
	UDATA * spLastCollectSlot = NULL;
	UDATA *spRemainingArgSlots = NULL;

	j9object_t nextType = getMethodHandleMethodType(nextMethodHandle);
	j9object_t argumentTypes = getMethodTypeArguments(nextType);

	j9object_t posArgumentType =
		_objectAccessBarrier->inlineIndexableObjectReadObject(
			_currentThread,
			argumentTypes,
			collectPosition);
	J9Class *posArgumentClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, posArgumentType);
	J9Class *arrayComponentClass = ((J9ArrayClass *)posArgumentClass)->componentType;

	j9object_t collectedArgsArrayRef = VM_VMHelpers::inlineAllocateIndexableObject(_currentThread, _objectAllocate, posArgumentClass, collectArraySize);
	if (NULL == collectedArgsArrayRef) {
		UDATA *spPriorToFrameBuild = _currentThread->sp;
		J9SFMethodTypeFrame * frame = buildMethodTypeFrame(_currentThread, currentType);
		collectedArgsArrayRef =
			_vm->memoryManagerFunctions->J9AllocateIndexableObject(
				_currentThread,
				posArgumentClass,
				collectArraySize,
				J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == collectedArgsArrayRef) {
			*foundHeapOOM = TRUE;
			goto exitCollectForAsCollector;
		}
		restoreMethodTypeFrame(frame, spPriorToFrameBuild);
	}

	/* Restore object references after allocation with GC as GC may move them to somewhere else */
	nextMethodHandle = ((j9object_t*)_currentThread->sp)[currentArgSlots];
	nextType = getMethodHandleMethodType(nextMethodHandle);
	argumentTypes = getMethodTypeArguments(nextType);
	
	/* With the specified position of collected arguments, we need to locate
	 * the 1st slot of the remaining arguments and the last slot of the
	 * array arguments before collecting.
	 * e.g.
	 * | arg1 | arg2 |... | collected arguments | arg4 (the remaining arguments) ...
	 */
	if (0 != currentArgSlots) {
		/* Count the slot number of all the arguments before collected arguments to
		 * determine the 1st slot for collected arguments.
		 */
		U_32 argumentSlots = getArgSlotsBeforePosition(argumentTypes, collectPosition);

		/* The 1st slot of arguments to be collected (used for storing the array reference) */
		spPtr += currentArgSlots - 1;
		spPtr -= argumentSlots;
		spCollectSlot = spPtr;

		/* Locate the 1st slot of remaining arguments following the collected elements */
		spPtr -= collectArraySize;
		if (J9ROMCLASS_IS_PRIMITIVE_TYPE(arrayComponentClass->romClass)
			&& ((_vm->doubleReflectClass == arrayComponentClass) || (_vm->longReflectClass == arrayComponentClass))
		) {
			spPtr -= collectArraySize;
		}
		spRemainingArgSlots = spPtr;

		/* Locate the last slot of array arguments to be collected */
		if (0 != collectArraySize) {
			spPtr += 1;
		}
		spLastCollectSlot = spPtr;
	}

	/* There are 3 cases to address the remaining arguments following the array reference:
	 * 1) If there is no argument on stack, then the stack should be adjusted to:
	 *    | next method handle | array reference |
	 *    In such case, the top of stack should move down by 1 slot to store the array reference.
	 *
	 * 2) If there exists arguments on stack but the collect size is zero, then the stack should be:
	 *    | next method handle | arguments 1 | array reference | arguments 2, 3...
	 *    Note: Initially the slot of array reference doesn't exist on stack.
	 *    In such case, all remaining arguments after the collectPosition must move down by 1 slot
	 *    to make room for a zero-sized array.
	 *
	 * 3) If there exists arguments on stack but the collect size is non-zero, then the stack should be:
	 *    | next method handle | arguments 1 | array reference | arguments 2, 3...
	 *    Note: Initially the slot of array reference is occupied by one of array elements to be collected
	 *    In such case, all remaining arguments after the collectPosition must move up by the
	 *    length of collectArraySize to match the number of argument slots of the next method handle.
	 */

	/* Case 1: The slot to store the array reference should be the position following the next method handle */
	if (0 == currentArgSlots) {
		spOnTopStack -= 1;
		spCollectSlot = spOnTopStack;
	} else if (0 == collectArraySize) {
		/* Case 2: Move down all remaining arguments after the collectPosition
		 * by 1 slot to allow for storing the zero-sized array reference.
		 */
		spPtr = spOnTopStack - 1;
		while (spRemainingArgSlots >= spOnTopStack) {
			*spPtr = *(spPtr + 1);
			spPtr += 1;
			spRemainingArgSlots -= 1;
		}
		spOnTopStack -= 1;
	} else {
		/* Case 3: Store all of array elements to the allocated array area for a non-zero sized array */
		spPtr = spLastCollectSlot;
		if (!J9ROMCLASS_IS_PRIMITIVE_TYPE(arrayComponentClass->romClass)) {
			U_32 collectCount = collectArraySize;
			while(0 != collectCount) {
				collectCount -= 1;
				_objectAccessBarrier->inlineIndexableObjectStoreObject(_currentThread, collectedArgsArrayRef, collectCount, *(j9object_t*)spPtr);
				spPtr += 1;
			}
		} else {
			_currentThread->sp = spPtr;
			primitiveArrayCollect(collectedArgsArrayRef, collectArraySize, arrayComponentClass);
		}

		/* Move up the remaining arguments after the array reference
		 * by the length of collectArraySize.
		 */
		spPtr = spCollectSlot;
		while (spRemainingArgSlots >= spOnTopStack) {
			spPtr -= 1;
			*spPtr = *spRemainingArgSlots;
			spRemainingArgSlots -= 1;
		}
		spOnTopStack = spPtr;
	}

	/* Set the new location of the top of stack after adjusting the arguments */
	_currentThread->sp = spOnTopStack;

	/* Store the array reference to the 1st slot of the original items of array */
	*(j9object_t *)spCollectSlot = collectedArgsArrayRef;

exitCollectForAsCollector:
	/* Return the next MethodHandle, this is what gets executed next */
	return nextMethodHandle;
}

U_32
VM_MHInterpreter::getArgSlotsBeforePosition(j9object_t argumentTypes, U_32 argPosition)
{
	U_32 argumentSlots = 0;
	U_32 argumentIndex = 0;

	/* Count the slot number of all arguments before the specified position of argument.
	 * Note: long/double argument occupies 2 slots while other type occupies 1 slot.
	 * e.g.
	 * case 1: arg1 (int) | arg2 (int) | arg3 (int) | arguments to be addressed | ...,
	 *         the slot number before the arguments = 1(int) + 1(int) + 1(int) = 3
	 * case 2: arg1 (object) | arg2 (long) | arg3 (int) | arguments to be addressed | ...,
	 *         the slot number before the arguments = 1(object) + 2(long) + 1(int) = 4
	 */
	for (argumentIndex = 0; argumentIndex < argPosition; argumentIndex++) {
		j9object_t argumentTypeAtIndex =
			_objectAccessBarrier->inlineIndexableObjectReadObject(
				_currentThread,
				argumentTypes,
				argumentIndex);
		J9Class *argumentClassAtIndex = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, argumentTypeAtIndex);

		if ((_vm->doubleReflectClass == argumentClassAtIndex)
			|| (_vm->longReflectClass == argumentClassAtIndex)
		) {
			argumentSlots += 2;
		} else {
			argumentSlots += 1;
		}
	}
	return argumentSlots;
}

j9object_t
VM_MHInterpreter::filterArgumentsWithCombiner(j9object_t methodHandle)
{
	/* Get filterHandle.type and filterHandle.type.argSlots */
	/* [... filterHandle args] */
	j9object_t filterHandleType = getMethodHandleMethodType(methodHandle);
	j9object_t argumentTypes = getMethodTypeArguments(filterHandleType);
	U_32 filterArgSlots = getMethodTypeArgSlots(filterHandleType);
	j9object_t argumentIndices = J9VMJAVALANGINVOKEFILTERARGUMENTSWITHCOMBINERHANDLE_ARGUMENTINDICES(_currentThread, methodHandle);
	U_32 argumentIndicesCount = J9INDEXABLEOBJECT_SIZE(_currentThread, argumentIndices);
	UDATA *spFirstFilterArgSlot = _currentThread->sp + filterArgSlots;

	/* [... filterHandle args descriptionBytes MethodTypeFrame] */
	(void)buildMethodTypeFrame(_currentThread, filterHandleType);

	/* Get combinerHandle, combinerHandle.type and combinerHandle.type.argSlots */
	j9object_t combinerHandle = getCombinerHandleForFilter(methodHandle);
	j9object_t combinerType = getMethodHandleMethodType(combinerHandle);
	U_32 combinerArgSlots = getMethodTypeArgSlots(combinerType);

	/* Adjust sp to make room for combinerHandle and combinerArgs. There is no point in actually adding the 
	 * combinerHandle to the stack here because it will later be overwritten by insertPlaceHolderFrame. */
	/* [... filterHandle args descriptionBytes MethodTypeFrame combinerHandle(empty) combinerArgs(empty)] */
	_currentThread->sp -= (combinerArgSlots + 1);
	UDATA *spCombinerSlot = _currentThread->sp + combinerArgSlots;

	/* Copy all arguments specified by argumentIndices from filterHandle to the stack slots used by combinerHandler */
	U_32 arrayIndex = 0;
	for (arrayIndex = 0; arrayIndex < argumentIndicesCount; arrayIndex++) {
		U_32 argumentTypeIndex = (U_32)J9JAVAARRAYOFINT_LOAD(_currentThread, argumentIndices, arrayIndex);

		/* Determine the slot index of each argument specified by the index in argumentIndices */
		U_32 argumentTypeSlots = getArgSlotsBeforePosition(argumentTypes, argumentTypeIndex);

		/* Determine the type of each argument specified by argumentIndices before copying.
		* Note: long/double type takes 2 slots while other types take 1 slot.
		*/
		j9object_t argumentTypeAtIndex =
			_objectAccessBarrier->inlineIndexableObjectReadObject(
				_currentThread,
				argumentTypes,
				argumentTypeIndex);
		J9Class *argumentClassAtIndex = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, argumentTypeAtIndex);

		if ((_vm->doubleReflectClass == argumentClassAtIndex)
			|| (_vm->longReflectClass == argumentClassAtIndex)
		) {
			spCombinerSlot -= 2;
			*(U_64*)spCombinerSlot = *(U_64*)(spFirstFilterArgSlot - argumentTypeSlots - 2);
		} else {
			spCombinerSlot -= 1;
			*spCombinerSlot = *(spFirstFilterArgSlot - argumentTypeSlots - 1);
		}
	}
	/* after loop spCombinerSlot is at the top of the combiner arguments */
	Assert_VM_true(spCombinerSlot == _currentThread->sp);

	/* [... filterHandle args descriptionBytes MethodTypeFrame filterHandle PlaceHolderFrame filterHandle combinerArgs] */
	insertPlaceHolderFrame(combinerArgSlots, methodHandle, J9VMJAVALANGINVOKEMETHODHANDLE_FILTERARGUMENTSWITHCOMBINERPLACEHOLDER_METHOD(_vm));

	/* insertPlaceHolderFrame adds methodHandle to the stack in the slot where combinerHandle should be. Insert combinerHandle
	 * so the stack is correctly set up to call the combiner handle */ 
	/* [... filterHandle args descriptionBytes MethodTypeFrame filterHandle PlaceHolderFrame combinerHandle combinerArgs] */
	((j9object_t *)_currentThread->sp)[combinerArgSlots] = combinerHandle;

	return combinerHandle;
}

j9object_t
VM_MHInterpreter::replaceReturnValueForFilterArgumentsWithCombiner()
{
	UDATA *bp = _currentThread->arg0EA - (sizeof(J9SFStackFrame)/sizeof(UDATA*));
	J9SFStackFrame *frame = (J9SFStackFrame*)(bp);

	/* [... filterHandle args descriptionBytes MethodTypeFrame filterHandle PlaceHolderFrame combinerReturnValue] */
	j9object_t filterHandle = *(j9object_t*)_currentThread->arg0EA;
	j9object_t filterType = getMethodHandleMethodType(filterHandle);
	j9object_t argumentTypes = getMethodTypeArguments(filterType);
	U_32 filterArgSlots = (U_32)getMethodTypeArgSlots(filterType);
	U_32 filterPosition = getMethodHandleFilterPosition(filterHandle);

	/* Count the slot number of all the arguments before the specified filter position */
	U_32 argumentSlots = getArgSlotsBeforePosition(argumentTypes, filterPosition);

	/* Locally store the return value from the combinerHandle and determine stackslots required by the return value */
	j9object_t combinerHandle = getCombinerHandleForFilter(filterHandle);
	j9object_t combinerType = getMethodHandleMethodType(combinerHandle);
	j9object_t combinerReturnType = J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(_currentThread, combinerType);
	J9Class *combinerReturnTypeClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, combinerReturnType);
	U_32 combinerReturnSlots = 1;
	UDATA combinerReturnValue0 = _currentThread->sp[0];
	UDATA combinerReturnValue1 = 0;
	if ((_vm->doubleReflectClass == combinerReturnTypeClass) || (_vm->longReflectClass == combinerReturnTypeClass)) {
		combinerReturnSlots = 2;
		combinerReturnValue1 = _currentThread->sp[1];
	}

	/* Determine the number of argument slots starting after the filterPosition */
	U_32 remainingArgSlots = filterArgSlots - argumentSlots - combinerReturnSlots;

	/* Set local variables to restore state of the _currentThread during updateVMStruct */
	UDATA *mhPtr = UNTAGGED_A0(frame);

	/* Advance past the frame and single argument (now filterHandle is at the top of the stack)) */
	bp = (UDATA *)(((J9SFStackFrame*)(bp+1)) + 1);

	J9SFMethodTypeFrame *mtFrame = (J9SFMethodTypeFrame*)(bp);
	_currentThread->literals = mtFrame->savedCP;
	_currentThread->pc = mtFrame->savedPC;
	_currentThread->arg0EA = UNTAGGED_A0(mtFrame);
	_currentThread->sp = mhPtr - filterArgSlots;

	/* Overwrite initial filterHandle with filterHandle.next */
	j9object_t nextHandle = J9VMJAVALANGINVOKEFILTERARGUMENTSWITHCOMBINERHANDLE_NEXT(_currentThread, filterHandle);
	*(j9object_t*)(mhPtr) = nextHandle;

	/* Add the combinerReturnValue at filterPosition */
	_currentThread->sp[remainingArgSlots] = combinerReturnValue0;
	if (2 == combinerReturnSlots) {
		_currentThread->sp[remainingArgSlots + 1] = combinerReturnValue1;
	}

	return nextHandle;
}

j9object_t
VM_MHInterpreter::foldForFoldArguments(j9object_t methodHandle)
{
	/* Get foldHandle.type and foldHandle.type.argSlots */
	/* [... foldHandle args] */
	j9object_t foldType = getMethodHandleMethodType(methodHandle);
	j9object_t argumentTypes = getMethodTypeArguments(foldType);
	U_32 foldArgSlots = getMethodTypeArgSlots(foldType);
	U_32 foldPosition = getMethodHandleFoldPosition(methodHandle);
	j9object_t argumentIndices = J9VMJAVALANGINVOKEFOLDHANDLE_ARGUMENTINDICES(_currentThread, methodHandle);
	U_32 argumentIndicesCount = J9INDEXABLEOBJECT_SIZE(_currentThread, argumentIndices);
	UDATA *spFirstFoldArgSlot = _currentThread->sp + foldArgSlots;

	/* Count the slot number of all the arguments before the specified fold position to
	 * determine the 1st slot of fold arguments.
	 */
	U_32 slotsBeforeFold = getArgSlotsBeforePosition(argumentTypes, foldPosition);

	/* [... foldHandle args descriptionBytes MethodTypeFrame] */
	(void)buildMethodTypeFrame(_currentThread, foldType);

	/* Get combinerHandle, combinerHandle.type and combinerHandle.type.argSlots */
	j9object_t combinerHandle = getCombinerHandleForFold(methodHandle);
	j9object_t combinerType = getMethodHandleMethodType(combinerHandle);
	U_32 combinerArgSlots = getMethodTypeArgSlots(combinerType);

	/* Adjust sp to make room for combinerHandle and combinerArgs. There is no point in actually adding the 
	 * combinerHandle to the stack here because it will later be overwritten by insertPlaceHolderFrame. */
	/* [... foldHandle args descriptionBytes MethodTypeFrame combinerHandle(empty) combinerArgs(empty)] */
	_currentThread->sp -= (combinerArgSlots  + 1);

	/* Do the normal fold operation if the argument indices of foldHandle are not specified;
	 * Otherwise, use the specified arguments of foldHandle to fold.
	 */
	if (0 == argumentIndicesCount) {
		/* Copy the subset of arguments from foldHandle that are used for combinerArgs.
		* e.g.
		* Arguments of foldHandle:  [int, long, {int, double, ....} remaining arguments ]
		*                                       |---> arguments of combinerHandler from foldPosition
		* Arguments of combinerHandler [int, double, .... ]
		*/
		UDATA *spLastSlotOfCombinerArgs = spFirstFoldArgSlot - slotsBeforeFold - combinerArgSlots;
		memcpy(_currentThread->sp, spLastSlotOfCombinerArgs, combinerArgSlots * sizeof(UDATA));
	} else {
		U_32 arrayIndex = 0;
		UDATA *spCombinerSlot = _currentThread->sp + combinerArgSlots;
		/* Copy all arguments specified by argumentIndices from foldHandle to the stack slots used by combinerHandler */
		for (arrayIndex = 0; arrayIndex < argumentIndicesCount; arrayIndex++) {
			U_32 argumentTypeIndex = (U_32)J9JAVAARRAYOFINT_LOAD(_currentThread, argumentIndices, arrayIndex);

			/* According to Oracle's behavior, it indicates the passed-in argument indices
			 * treats the fold position as placeholder which means the fold position (placeholder)
			 * is reserved for counting index but never used as an argument index.
			 * In this case, we need to adjust the argument indices as follows:
			 * 1) if the argument index < the fold position, there is no change for the argument index;
			 * 2) if the argument index == the fold position, throw out InvocationTargetException;
			 * 3) if the argument index > the fold position, the argument index decreases 1 to exclude
			 *    the fold position (placeholder) so as to fetch the required argument.
			 * e.g. if the fold position is 2 (index2), the passed-in argument indices are {index1, index3, index5}.
			 * Given that the fold position will reserved as placeholder,
			 * the argument sequence is {arg0,   arg1, [placeholder], arg2,   arg3,   arg4,  arg5}
			 *                         index0   index1    index2     index3  index4  index5  index6
			 * in which case the retrieved arguments for combiner should be {arg1, arg2, arg4}.
			 *
			 * Note: we need to modify the argument indexes because we're using the new methodType
			 * (which has the combined value added into it) rather than the original methodType.
			 */
			if (argumentTypeIndex == foldPosition) {
				setCurrentException(_currentThread, J9VMCONSTANTPOOL_JAVALANGREFLECTINVOCATIONTARGETEXCEPTION, NULL);
				goto exitFoldForFoldArguments;
			} else if (argumentTypeIndex > foldPosition) {
				argumentTypeIndex -= 1;
			}

			/* Determine the slot index of each argument specified by the index in argumentIndices */
			U_32 argumentTypeSlots = getArgSlotsBeforePosition(argumentTypes, argumentTypeIndex);

			/* Determine the type of each argument specified by argumentIndices before copying.
			 * Note: long/double type takes 2 slots while other types take 1 slot.
			 */
	 		j9object_t argumentTypeAtIndex =
	 			_objectAccessBarrier->inlineIndexableObjectReadObject(
	 				_currentThread,
	 				argumentTypes,
					argumentTypeIndex);
	 		J9Class *argumentClassAtIndex = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, argumentTypeAtIndex);

	 		if ((_vm->doubleReflectClass == argumentClassAtIndex)
	 			|| (_vm->longReflectClass == argumentClassAtIndex)
	 		) {
	 			spCombinerSlot -= 2;
	 			*(U_64*)spCombinerSlot = *(U_64*)(spFirstFoldArgSlot - argumentTypeSlots - 2);
	 		} else {
	 			spCombinerSlot -= 1;
	 			*spCombinerSlot = *(spFirstFoldArgSlot - argumentTypeSlots - 1);
	 		}
		}
		/* after loop spCombinerSlot is at the top of the combiner arguments */
		Assert_VM_true(spCombinerSlot == _currentThread->sp);
	}

	/* [... foldHandle args descriptionBytes MethodTypeFrame foldHandle PlaceHolderFrame foldHandle combinerArgs] */
	insertPlaceHolderFrame(combinerArgSlots, methodHandle, J9VMJAVALANGINVOKEMETHODHANDLE_FOLDHANDLEPLACEHOLDER_METHOD(_vm));

	/* insertPlaceHolderFrame adds methodHandle to the stack in the slot where combinerHandle should be. Insert combinerHandle
	 * so the stack is correctly set up to call the combiner handle */ 
	/* [... foldHandle args descriptionBytes MethodTypeFrame foldHandle PlaceHolderFrame combinerHandle combinerArgs] */
	((j9object_t *)_currentThread->sp)[combinerArgSlots] = combinerHandle;

exitFoldForFoldArguments:
	return combinerHandle;
}

j9object_t
VM_MHInterpreter::insertReturnValueForFoldArguments()
{
	UDATA *bp = _currentThread->arg0EA - (sizeof(J9SFStackFrame)/sizeof(UDATA*));
	J9SFStackFrame *frame = (J9SFStackFrame*)(bp);

	/* [... foldHandle args descriptionBytes MethodTypeFrame args foldHandle PlaceHolderFrame combinerReturnValue] */
	j9object_t foldHandle = *(j9object_t*)_currentThread->arg0EA;
	j9object_t foldType = getMethodHandleMethodType(foldHandle);
	j9object_t argumentTypes = getMethodTypeArguments(foldType);
	U_32 foldArgSlots = (U_32)getMethodTypeArgSlots(foldType);
	U_32 foldPosition = getMethodHandleFoldPosition(foldHandle);

	/* Count the slot number of all the arguments before the specified fold position */
	U_32 argumentSlots = getArgSlotsBeforePosition(argumentTypes, foldPosition);

	/* Determine the number of argument slots starting from the specified fold position */
	U_32 remainingArgSlots = foldArgSlots - argumentSlots;

	/* Locally store the return value from the combinerHandle and determine stackslots required by the return value */
	j9object_t combinerHandle = getCombinerHandleForFold(foldHandle);
	j9object_t combinerType = getMethodHandleMethodType(combinerHandle);
	j9object_t combinerReturnType = J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(_currentThread, combinerType);
	J9Class *argTypeClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, combinerReturnType);
	UDATA combinerReturnSlots = 0;
	UDATA combinerReturnValue0 = 0;
	UDATA combinerReturnValue1 = 0;

	if (_vm->voidReflectClass != argTypeClass) {
		combinerReturnSlots = 1;
		combinerReturnValue0 = _currentThread->sp[0];
		if ((_vm->doubleReflectClass == argTypeClass) || (_vm->longReflectClass == argTypeClass)) {
			combinerReturnSlots = 2;
			combinerReturnValue1 = _currentThread->sp[1];
		}
	}

	/* Set local variables to restore state of the _currentThread during updateVMStruct */
	UDATA *mhPtr = UNTAGGED_A0(frame);

	/* Advance past the frame and single argument */
	bp = (UDATA *)(((J9SFStackFrame*)(bp+1)) + 1);

	J9SFMethodTypeFrame *mtFrame = (J9SFMethodTypeFrame*)(bp);
	_currentThread->literals = mtFrame->savedCP;
	_currentThread->pc = mtFrame->savedPC;
	_currentThread->arg0EA = UNTAGGED_A0(mtFrame);
	_currentThread->sp = mhPtr - foldArgSlots;

	/* Overwrite initial foldHandle with foldHandle.next */
	j9object_t nextHandle = J9VMJAVALANGINVOKEFOLDHANDLE_NEXT(_currentThread, foldHandle);
	*(j9object_t*)(mhPtr) = nextHandle;

	/* Add the combinerReturnValue between foldHandle.next and the arguments */
	if (0 != combinerReturnSlots) {
		UDATA *finalArg = _currentThread->sp;
		_currentThread->sp -= combinerReturnSlots;
		/* Move down the stack data starting from the specified fold position by combinerReturnSlots
		 * so as to insert the return value of combinerHandle.
		 */
		memmove(_currentThread->sp, finalArg, remainingArgSlots * sizeof(UDATA));
		_currentThread->sp[remainingArgSlots] = combinerReturnValue0;
		if (2 == combinerReturnSlots) {
			_currentThread->sp[remainingArgSlots + 1] = combinerReturnValue1;
		}
	}

	return nextHandle;
}

j9object_t
VM_MHInterpreter::permuteForPermuteHandle(j9object_t methodHandle)
{
	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandle);
	j9object_t currentTypeArguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, currentType);
	U_32 currentTypeArgumentsLength = J9INDEXABLEOBJECT_SIZE(_currentThread, currentTypeArguments);
	U_32 currentArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	j9object_t nextHandle = J9VMJAVALANGINVOKEPERMUTEHANDLE_NEXT(_currentThread, methodHandle);
	j9object_t nextType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, nextHandle);
	U_32 nextArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, nextType);
	j9object_t permuteArray = J9VMJAVALANGINVOKEPERMUTEHANDLE_PERMUTE(_currentThread, methodHandle);
	U_32 permuteArrayLength = J9INDEXABLEOBJECT_SIZE(_currentThread, permuteArray);
	J9JavaVM *vm = _currentThread->javaVM;
	J9Class * doubleReflectClass = vm->doubleReflectClass;
	J9Class * longReflectClass = vm->longReflectClass;
	UDATA *newArgsPtr = _currentThread->sp;
	/* Points at the FIRST argument on the stack. Zero argument cases are not permutable */
	UDATA *argsPtr = _currentThread->sp + currentArgSlots - 1;
	/* Maximum number of stack slots is 255 */
	U_8 slotIndexArray[255];
	U_32 i = 0;
	U_8 slotIndex = 0;

	/* overwrite original handle with next */
	*(j9object_t*)(_currentThread->sp + currentArgSlots) = nextHandle;


	/* If we're dropping everything, do the fast case */
	if (0 == permuteArrayLength) {
		_currentThread->sp = _currentThread->sp + currentArgSlots;
		return *(j9object_t*)_currentThread->sp;
	}



	/* Generate an array similar to the permute array, except that
	 * it's contents contain the source STACK SLOT.  We grab each argument
	 * off the stack, figure out it's type and from that can tell how many stack
	 * slots that is. We then look inside the permute array and replace all references
	 * to the current argument with the stack slot index
	 */
	for (i = 0; i < currentTypeArgumentsLength; i++) {
		J9Class *sourceClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, currentTypeArguments, i));
		U_32 j = 0;

		if ((sourceClass == doubleReflectClass) || (sourceClass == longReflectClass)) {
			slotIndex += 1;
		}

		/* Replace all indexes to the current argument with the calculated stack slot index */
		for (j = 0; j < permuteArrayLength; j++) {
			U_32 permuteValue = (U_32)J9JAVAARRAYOFINT_LOAD(_currentThread, permuteArray, j);

			if (permuteValue == i) {
				slotIndexArray[j] = slotIndex;
			}
		}
		slotIndex++;
	}

	/* Actually do the copy - values are copied in the correct order below the stack */
	for (i = 0; i < permuteArrayLength; i++) {
		/* Get the slot index for this parameter */
		IDATA sourceSlotIndex = slotIndexArray[i];
		UDATA *sourceAddress = &argsPtr[-sourceSlotIndex];
		UDATA typeSize = 1;
		U_32 sourceArgument = (U_32)J9JAVAARRAYOFINT_LOAD(_currentThread, permuteArray, i);
		/* Determine the size of the argument */
		J9Class *sourceClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, currentTypeArguments, sourceArgument));

		/* Long and doubles always use two stack slots */
		if ((sourceClass == doubleReflectClass) || (sourceClass == longReflectClass)) {
			typeSize = 2;
		}

		/* Move the destination pointer to where the argument will end up */
		newArgsPtr -= typeSize;

		/* Copy the argument */
		*newArgsPtr = *sourceAddress;

		/* doubles and longs take up two slots, copy the other half */
		if (2 == typeSize) {
			*(newArgsPtr+1) = *(sourceAddress+1);
		}
	}

	/* Adjust the stack to the final position.  Bracketed in a way to avoid potential
	 * issues with overflow.
	 */
	_currentThread->sp = (_currentThread->sp + currentArgSlots) - nextArgSlots;

	/* Move new arguments down to make them live */
	memmove(_currentThread->sp, newArgsPtr, nextArgSlots*sizeof(UDATA));

	/* Be sure to return the (next) method handle */
	return *(j9object_t*)(_currentThread->sp + nextArgSlots);
}

j9object_t
VM_MHInterpreter::insertArgumentsForInsertHandle(j9object_t methodHandle)
{
	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandle);
	U_32 currentArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	j9object_t currentTypeArguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, currentType);
    U_32 currentTypeArgumentsLength = J9INDEXABLEOBJECT_SIZE(_currentThread, currentTypeArguments);
	j9object_t nextHandle = J9VMJAVALANGINVOKEINSERTHANDLE_NEXT(_currentThread, methodHandle);
	j9object_t nextType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, nextHandle);
	U_32 nextArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, nextType);
	U_32 insertionIndex = (U_32)J9VMJAVALANGINVOKEINSERTHANDLE_INSERTIONINDEX(_currentThread, methodHandle);
	j9object_t valuesArray = J9VMJAVALANGINVOKEINSERTHANDLE_VALUES(_currentThread, methodHandle);
	U_32 valuesArrayLength = J9INDEXABLEOBJECT_SIZE(_currentThread, valuesArray);
	UDATA argSlotDelta = nextArgSlots - currentArgSlots;
	UDATA * finalSP = _currentThread->sp - argSlotDelta;
	UDATA *insertPtr = NULL;
	U_32 i = 0;

	/* Insert handles always insert at least one value */
	Assert_VM_true(argSlotDelta > 0);

	/* overwrite original handle with next */
	*(j9object_t*)(_currentThread->sp + currentArgSlots) = nextHandle;

	/* Determine if there are any 2 slot entries in the MethodType of
	 * the current handle.  If there are, we need to figure out how
	 * many of them are before the 'insertionIndex' and adjust it
	 * accordingly.
	 */
	if (currentArgSlots != currentTypeArgumentsLength) {
		J9JavaVM *vm = _currentThread->javaVM;
		const J9Class * doubleReflectClass = vm->doubleReflectClass;
		const J9Class * longReflectClass = vm->longReflectClass;
		U_32 j = 0;
		U_32 extraSlots = 0;

		Assert_VM_true(insertionIndex <= currentTypeArgumentsLength);
		for (j = 0; j < insertionIndex; j++) {
			J9Class *sourceClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, currentTypeArguments, j));

			/* Long and doubles always use two stack slots */
			if ((doubleReflectClass == sourceClass) || (longReflectClass == sourceClass)) {
				extraSlots += 1;
			}
		}
		insertionIndex += extraSlots;
	}

	/*   (S,S,I)V		(S,I,S,I)V
	 * [low memory]
	 * 					I 		  <- finalSP
	 *  I	<-sp		S
	 *  S				<insert>  <- insertPtr
	 *  S				S
	 *  MH				MH.next
	 * [high memory]
	 * argSlots = 3
	 * insertIndex = 1
	 */
	insertPtr = _currentThread->sp + currentArgSlots - insertionIndex - 1;
	memmove(finalSP, _currentThread->sp, (currentArgSlots - insertionIndex) * sizeof(UDATA));

	/* Implementation assumes that this never needs to unbox the value.  If inserting a value that would
	 * need to be unboxed, the next handle will an AsTypeHandle that does the unboxing.  The following
	 * assertion validates that we never expect to convert a j.l.Long/Double to a primitive long/double.
	 */
	Assert_VM_true(argSlotDelta == valuesArrayLength);
	for (i = 0; i < valuesArrayLength; i++) {
		*(j9object_t*)insertPtr = J9JAVAARRAYOFOBJECT_LOAD(_currentThread, valuesArray, i);
		insertPtr -= 1;
	}

	_currentThread->sp = finalSP;

	/* Return the next handle from its new location on the stack */
	return (j9object_t) finalSP[nextArgSlots];
}

void
VM_MHInterpreter::primitiveArraySpread(j9object_t arrayObject, I_32 spreadCount, J9Class *arrayClass)
{
	I_32 i;
	J9JavaVM *vm = _currentThread->javaVM;
	UDATA *ptr = _currentThread->sp;

	if (arrayClass == vm->intArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			/* Load the value */
			*(I_32*)ptr = J9JAVAARRAYOFINT_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->longArrayClass) {
		/* Double slot value - need to move two slots, thus the -- here */
		for (i = 0; i < spreadCount; i++) {
			/* Move the pointer down two slots */
			ptr -= 2;
			*(U_64*)ptr = J9JAVAARRAYOFLONG_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->doubleArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 2;
			*(U_64*)ptr = J9JAVAARRAYOFDOUBLE_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->byteArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			*(I_32*)ptr = J9JAVAARRAYOFBYTE_LOAD(_currentThread, arrayObject, i);
		}
	} else if(arrayClass == vm->charArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			*(I_32*)ptr = J9JAVAARRAYOFCHAR_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->floatArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			*(U_32*)ptr = J9JAVAARRAYOFFLOAT_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->shortArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			*(I_32*)ptr = J9JAVAARRAYOFSHORT_LOAD(_currentThread, arrayObject, i);
		}
	} else if (arrayClass == vm->booleanArrayClass) {
		for (i = 0; i < spreadCount; i++) {
			ptr -= 1;
			*(I_32*)ptr = J9JAVAARRAYOFBOOLEAN_LOAD(_currentThread, arrayObject, i);
		}
	} else {
		Assert_VM_unreachable();
	}
	_currentThread->sp = ptr;
}

void
VM_MHInterpreter::primitiveArrayCollect(j9object_t collectedArgsArrayRef, U_32 collectArraySize, J9Class *arrayComponentClass)
{
	J9JavaVM *_vm = _currentThread->javaVM;
	UDATA *spPtr = _currentThread->sp;
	U_32 collectCount = collectArraySize;

	if (_vm->booleanReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreU8(_currentThread, collectedArgsArrayRef, collectCount, (U_8)*(I_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->byteReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreI8(_currentThread, collectedArgsArrayRef, collectCount, (I_8)*(I_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->charReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreU16(_currentThread, collectedArgsArrayRef, collectCount, (U_16)*(I_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->shortReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreI16(_currentThread, collectedArgsArrayRef, collectCount, (I_16)*(I_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->intReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreI32(_currentThread, collectedArgsArrayRef, collectCount, *(I_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->floatReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreU32(_currentThread, collectedArgsArrayRef, collectCount, *(U_32*)spPtr);
			spPtr += 1;
		}
	} else if (_vm->doubleReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreU64(_currentThread, collectedArgsArrayRef, collectCount, *(U_64*)spPtr);
			spPtr += 2;
		}

	} else if (_vm->longReflectClass == arrayComponentClass) {
		while(0 != collectCount) {
			collectCount -= 1;
			_objectAccessBarrier->inlineIndexableObjectStoreI64(_currentThread, collectedArgsArrayRef, collectCount, *(I_64*)spPtr);
			spPtr += 2;
		}
	} else {
		Assert_VM_unreachable();
	}

	_currentThread->sp = spPtr;
}

ExceptionType
VM_MHInterpreter::convertArguments(UDATA * currentArgs, j9object_t *currentType, UDATA * nextArgs, j9object_t *nextType, UDATA explicitCast, ClassCastExceptionData *exceptionData)
{
	U_32 argCount = (U_32)J9INDEXABLEOBJECT_SIZE(_currentThread, J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, *currentType));
	U_32 i;
	J9JavaVM * vm = _currentThread->javaVM;
	J9Class * booleanReflectClass = vm->booleanReflectClass;
	J9Class * byteReflectClass = vm->byteReflectClass;
	J9Class * shortReflectClass = vm->shortReflectClass;
	J9Class * charReflectClass = vm->charReflectClass;
	J9Class * intReflectClass = vm->intReflectClass;
	J9Class * floatReflectClass = vm->floatReflectClass;
	J9Class * doubleReflectClass = vm->doubleReflectClass;
	J9Class * longReflectClass = vm->longReflectClass;
	J9Class * booleanWrapperClass = J9VMJAVALANGBOOLEAN_OR_NULL(vm);
	J9Class * byteWrapperClass = J9VMJAVALANGBYTE_OR_NULL(vm);
	J9Class * shortWrapperClass = J9VMJAVALANGSHORT_OR_NULL(vm);
	J9Class * charWrapperClass = J9VMJAVALANGCHARACTER_OR_NULL(vm);
	J9Class * intWrapperClass = J9VMJAVALANGINTEGER_OR_NULL(vm);
	J9Class * floatWrapperClass = J9VMJAVALANGFLOAT_OR_NULL(vm);
	J9Class * doubleWrapperClass = J9VMJAVALANGDOUBLE_OR_NULL(vm);
	J9Class * longWrapperClass = J9VMJAVALANGLONG_OR_NULL(vm);
	I_32 intValue = 0;
	I_64 longValue = 0;
	float floatValue = 0.0;
	double doubleValue = 0.0;
	ExceptionType rc = NO_EXCEPTION;

	for (i = 0; i < argCount; ++i) {
		J9Class * currentClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, *currentType), i));
		J9Class * nextClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, *nextType), i));

		if (currentClass == nextClass) {
			/* Trivial case - types are identical */

			*--nextArgs = *--currentArgs;
			if ((currentClass == doubleReflectClass) || (currentClass == longReflectClass)) {
				*--nextArgs = *--currentArgs;
			}
		} else {
			if (J9ROMCLASS_IS_PRIMITIVE_TYPE(currentClass->romClass)) {
				if (J9ROMCLASS_IS_PRIMITIVE_TYPE(nextClass->romClass)) {
					/* primitive to primitive conversion */

					if (currentClass == doubleReflectClass) {
						memcpy(&doubleValue, currentArgs -= 2, sizeof(doubleValue));
convertDouble:
						if (nextClass == floatReflectClass) {
							helperConvertDoubleToFloat(&doubleValue, (jfloat *) --nextArgs);
							continue;
						}
						if (nextClass == longReflectClass) {
							helperConvertDoubleToLong(&doubleValue, (I_64 *) (nextArgs -= 2));
							continue;
						}
						/* narrowing to an int type */
						helperConvertDoubleToInteger(&doubleValue, &intValue);
					} else if (currentClass == floatReflectClass) {
						memcpy(&floatValue, --currentArgs, sizeof(floatValue));
convertFloat:
						if (nextClass == doubleReflectClass) {
							helperConvertFloatToDouble(&floatValue, (jdouble *) (nextArgs -= 2));
							continue;
						}
						if (nextClass == longReflectClass) {
							helperConvertFloatToLong(&floatValue, (I_64 *) (nextArgs -= 2));
							continue;
						}
						/* narrowing to an int type */
						helperConvertFloatToInteger(&floatValue, &intValue);
					} else if (currentClass == longReflectClass) {
						memcpy(&longValue, currentArgs -= 2, sizeof(longValue));
convertLong:
						if (nextClass == floatReflectClass) {
							helperConvertLongToFloat(&longValue, (jfloat *) --nextArgs);
							continue;
						}
						if (nextClass == doubleReflectClass) {
							helperConvertLongToDouble(&longValue, (jdouble *) (nextArgs -= 2));
							continue;
						}
						/* narrowing to an int type */
						intValue = (I_32)longValue;
					} else { /* int types */
						intValue = *(I_32*)--currentArgs;
						if (currentClass == booleanReflectClass) {
convertBoolean:
							intValue = (I_32)(((I_8)intValue) & 1);
						}
convertInt:

						if (nextClass == longReflectClass) {
							longValue = (I_64) intValue;
							memcpy(nextArgs -= 2, &longValue, sizeof(longValue));
							continue;
						}
						if (nextClass == floatReflectClass) {
							helperConvertIntegerToFloat(&intValue, (jfloat *) --nextArgs);
							continue;
						}
						if (nextClass == doubleReflectClass) {
							helperConvertIntegerToDouble(&intValue, (jdouble *) (nextArgs -= 2));
							continue;
						}
					}

					if (nextClass == booleanReflectClass) {
						intValue = (I_32)(((I_8)intValue) & 1);
					} else if (nextClass == byteReflectClass) {
						intValue = (I_32)(I_8)intValue;
					} else if (nextClass == shortReflectClass) {
						intValue = (I_32)(I_16)intValue;
					} else if (nextClass == charReflectClass) {
						intValue = (I_32)(U_16)intValue;
					}
					*(I_32 *)--nextArgs = intValue;
				} else {
					j9object_t box = NULL;

					/* Boxing conversion */

					if (currentClass == booleanReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, booleanWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							intValue = *(I_32*)--currentArgs;
							J9VMJAVALANGBOOLEAN_SET_VALUE(_currentThread, box, intValue);
						}
					} else if (currentClass == byteReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, byteWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							intValue = *(I_32*)--currentArgs;
							J9VMJAVALANGBYTE_SET_VALUE(_currentThread, box, intValue);
						}
					} else if (currentClass == shortReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, shortWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							intValue = *(I_32*)--currentArgs;
							J9VMJAVALANGSHORT_SET_VALUE(_currentThread, box, intValue);
						}
					} else if (currentClass == charReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, charWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							U_32 u32Value = *(U_32*)--currentArgs;
							J9VMJAVALANGCHARACTER_SET_VALUE(_currentThread, box, u32Value);
						}
					} else if (currentClass == intReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, intWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							intValue = *(I_32*)--currentArgs;
							J9VMJAVALANGINTEGER_SET_VALUE(_currentThread, box, intValue);
						}
					} else if (currentClass == floatReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, floatWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							U_32 u32Value = *(U_32*)--currentArgs;
							J9VMJAVALANGFLOAT_SET_VALUE(_currentThread, box, u32Value);
						}
					} else if (currentClass == doubleReflectClass) {
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, doubleWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							U_64 u64Value;
							memcpy(&u64Value, currentArgs -= 2, sizeof(u64Value));
							J9VMJAVALANGDOUBLE_SET_VALUE(_currentThread, box, u64Value);
						}
					} else { /* must be long */
						box = vm->memoryManagerFunctions->J9AllocateObject(_currentThread, longWrapperClass, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (box != NULL) {
							memcpy(&longValue, currentArgs -= 2, sizeof(longValue));
							J9VMJAVALANGLONG_SET_VALUE(_currentThread, box, longValue);
						}
					}

					if (box == NULL) {
						setHeapOutOfMemoryError(_currentThread);
						break;
					}
					*--nextArgs = (UDATA) box;
				}
			} else {
				j9object_t currentReference = (j9object_t) *--currentArgs;

				/* Source is a reference type - operate on the runtime type, not the static type */

				if (currentReference == NULL) {
					currentClass = NULL;
				} else {
					currentClass = J9OBJECT_CLAZZ(_currentThread, currentReference);
				}

				if (J9ROMCLASS_IS_PRIMITIVE_TYPE(nextClass->romClass)) {
					/* Unboxing conversion */

					if (currentClass == floatWrapperClass) {
						U_32 temp = J9VMJAVALANGFLOAT_VALUE(_currentThread, currentReference);
						if (nextClass != floatReflectClass) {
							if (!explicitCast) {
								if (nextClass != doubleReflectClass) {
									goto classCastException;
								}
							}
							memcpy(&floatValue, &temp, sizeof(float));
							goto convertFloat;
						}
						*(U_32 *)--nextArgs = temp;
					} else if (currentClass == doubleWrapperClass) {
						U_64 temp = J9VMJAVALANGDOUBLE_VALUE(_currentThread, currentReference);
						if (nextClass != doubleReflectClass) {
							if (!explicitCast) {
								goto classCastException;
							}
							memcpy(&doubleValue, &temp, sizeof(doubleValue));
							goto convertDouble;
						}
						memcpy(nextArgs -= 2, &temp, sizeof(temp));
					} else if (currentClass == booleanWrapperClass) {
						intValue = J9VMJAVALANGBOOLEAN_VALUE(_currentThread, currentReference);
						if (!explicitCast) {
							if (nextClass != booleanReflectClass) {
								goto classCastException;
							}
						}
						goto convertBoolean;
					} else if (currentClass == byteWrapperClass) {
						intValue = J9VMJAVALANGBYTE_VALUE(_currentThread, currentReference);
						if (!explicitCast) {
							if ((nextClass == booleanReflectClass) || (nextClass == charReflectClass)) {
								goto classCastException;
							}
						}
						goto convertInt;
					} else if (currentClass == shortWrapperClass) {
						intValue = J9VMJAVALANGSHORT_VALUE(_currentThread, currentReference);
						if (!explicitCast) {
							if ((nextClass == booleanReflectClass) || (nextClass == byteReflectClass) || (nextClass == charReflectClass)) {
								goto classCastException;
							}
						}
						goto convertInt;
 					} else if (currentClass == charWrapperClass) {
						intValue = J9VMJAVALANGCHARACTER_VALUE(_currentThread, currentReference);
						if (!explicitCast) {
							if ((nextClass == booleanReflectClass) || (nextClass == byteReflectClass) || (nextClass == shortReflectClass)) {
								goto classCastException;
							}
						}
						goto convertInt;
					} else if (currentClass == intWrapperClass) {
						intValue = J9VMJAVALANGINTEGER_VALUE(_currentThread, currentReference);
						if (!explicitCast) {
							if ((nextClass == booleanReflectClass) || (nextClass == byteReflectClass) || (nextClass == shortReflectClass) || (nextClass == charReflectClass)) {
								goto classCastException;
							}
						}
						goto convertInt;
					} else if (currentClass == longWrapperClass) {
						longValue = J9VMJAVALANGLONG_VALUE(_currentThread, currentReference);
						if (nextClass != longReflectClass) {
							if (!explicitCast) {
								if ((nextClass != floatReflectClass) && (nextClass != doubleReflectClass)) {
									goto classCastException;
								}
							}
							goto convertLong;
						}
						memcpy(nextArgs -= 2, &longValue, sizeof(longValue));
					} else if (currentClass == NULL) {
						if (!explicitCast) {
							rc = NULL_POINTER_EXCEPTION;
							break;
						}
						/* stack has been memset to 0 - just bump the nextArgs pointer */
						--nextArgs;
						if ((nextClass == longReflectClass) || (nextClass == doubleReflectClass)) {
							--nextArgs;
						}
					} else {
						goto classCastException;
					}
				} else {
					/* Reference to reference conversion.
					 *
					 * explicitCastArguments allows any reference type to be cast to any
					 * interface type.
					 */

					if (!explicitCast || !J9ROMCLASS_IS_INTERFACE(nextClass->romClass)) {
						if (currentClass != NULL) {
							if (!instanceOfOrCheckCast(currentClass, nextClass)) {
classCastException:
								exceptionData->currentClass = currentClass;
								exceptionData->nextClass = nextClass;
								rc = CLASS_CAST_EXCEPTION;
								break;
							}
						}
					}
					*--nextArgs = (UDATA) currentReference;
				}
			}

		}
	}
	return rc;
}

U_8
VM_MHInterpreter::doesMHandStackMHMatch(j9object_t methodhandle)
{
	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodhandle);
	U_32 currentArgSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	if(*((j9object_t *) _currentThread->sp + currentArgSlots) != methodhandle){
		return 0;
	}
	return 1;
}

void
VM_MHInterpreter::mhStackValidator(j9object_t methodhandle)
{
	J9JavaVM *vm = _currentThread->javaVM;
	J9Class * doubleReflectClass = vm->doubleReflectClass;
	J9Class * longReflectClass = vm->longReflectClass;

	j9object_t currentType = J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodhandle);
	U_32 argSlots = (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, currentType);
	j9object_t arguments = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, currentType);
	U_32 argCount = (U_32)J9INDEXABLEOBJECT_SIZE(_currentThread, arguments);

	U_32 i;
	UDATA * argEA = _currentThread->sp;

	/* Backup by the expected number of args */
	if (argSlots > 1) {
		argEA += (argSlots - 1);
	}

	Assert_VM_mustHaveVMAccess(_currentThread);
	Assert_VM_mhStackHandleMatch(doesMHandStackMHMatch(methodhandle));

	/* Stacks grow toward 0: ensure that there are at least argSlots worth of arguments
	 * between the current SP and arg0EA */
	Assert_VM_true(argEA <= _currentThread->arg0EA);

	for (i = 0; i < argCount; ++i) {
		J9Class * currentClass = J9VM_J9CLASS_FROM_HEAPCLASS(_currentThread, J9JAVAARRAYOFOBJECT_LOAD(_currentThread, arguments, i));

		if (J9ROMCLASS_IS_PRIMITIVE_TYPE(currentClass->romClass)) {
			argEA--;
			if ((doubleReflectClass == currentClass) || (longReflectClass == currentClass)) {
				argEA--;	/* long and double take 2 slots */
			}
		} else {
			/* validate the object is an instance of the correct type */
			j9object_t stackObject = (j9object_t) *argEA;

			if (NULL != stackObject) {
				J9Class * stackClass = J9OBJECT_CLAZZ(_currentThread, stackObject);

				if (J9ROMCLASS_IS_INTERFACE(currentClass->romClass)) {
					/* Due to the explicitCastArguments() allowing an object of any class to be passed as an interface,
					 * we can't validate interfaces as the "bogus" seeming interface may have been introduced by an
					 * ExplicitCastHandle in an entirely valid way.
					 */
					/* Do nothing */
				} else {
					if (0 == instanceOfOrCheckCast(stackClass, currentClass)) {
						BOOLEAN MHStackValidationError = FALSE;
						J9UTF8 *className;
						PORT_ACCESS_FROM_JAVAVM(vm);

						Trc_VM_mhStackValidator_mismatchDetected(_currentThread);
						j9tty_printf(vm->portLibrary, "[MethodHandle StackValidator]\n");
						j9tty_printf(vm->portLibrary, "\tMismatch detected on J9VMThread %p\n", _currentThread);

						Trc_VM_mhStackValidator_mh(_currentThread, methodhandle);
						j9tty_printf(vm->portLibrary, "\tMethodHandle = 0x%p\n", methodhandle);

						Trc_VM_mhStackValidator_mh_methodType(_currentThread, currentType);
						j9tty_printf(vm->portLibrary, "\tMethodType = 0x%p\n", currentType);

						Trc_VM_mhStackValidator_mh_methodType_argSlots(_currentThread, argSlots);
						j9tty_printf(vm->portLibrary, "\tMethodType.argSlots = %d\n", argSlots);

						Trc_VM_mhStackValidator_mh_methodType_arguments_length(_currentThread, argCount);
						j9tty_printf(vm->portLibrary, "\tMethodType.arguments[].length = %d\n", argCount);

						Trc_VM_mhStackValidator_mh_methodType_arguments(_currentThread, i, currentClass);
						j9tty_printf(vm->portLibrary, "\tMethodType.arguments[%d] = 0x%p\n", i, currentClass);

						className = J9ROMCLASS_CLASSNAME(currentClass->romClass);
						Trc_VM_mhStackValidator_mh_class(_currentThread, J9UTF8_LENGTH(className), J9UTF8_DATA(className));
						j9tty_printf(vm->portLibrary, "\tClass = %.*s\n", J9UTF8_LENGTH(className), J9UTF8_DATA(className));

						Trc_VM_mhStackValidator_sp(_currentThread, _currentThread->sp);
						j9tty_printf(vm->portLibrary, "\tSP = 0x%p\n", _currentThread->sp);

						Trc_VM_mhStackValidator_sp_slot(_currentThread, argEA);
						j9tty_printf(vm->portLibrary, "\tSlot = 0x%p\n", argEA);

						Trc_VM_mhStackValidator_sp_slot_value(_currentThread, stackObject);
						j9tty_printf(vm->portLibrary, "\tSlot value = 0x%p\n", stackObject);

						className = J9ROMCLASS_CLASSNAME(stackClass->romClass);
						Trc_VM_mhStackValidator_sp_class(_currentThread, J9UTF8_LENGTH(className), J9UTF8_DATA(className));
						j9tty_printf(vm->portLibrary, "\tClass = %.*s\n", J9UTF8_LENGTH(className), J9UTF8_DATA(className));

						Assert_VM_true(MHStackValidationError);
					}
				}
			}
			argEA--;
		}
	}
}

#endif /* defined(J9VM_OPT_METHOD_HANDLE) */

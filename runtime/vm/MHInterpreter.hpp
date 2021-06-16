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

#if !defined(MHINTERPRETER_HPP_)
#define MHINTERPRETER_HPP_

#if defined(OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES)
#if OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES
#define VM_MHInterpreter VM_MHInterpreterCompressed
#else /* OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES */
#define VM_MHInterpreter VM_MHInterpreterFull
#endif /* OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES */
#endif /* defined(OMR_OVERRIDE_COMPRESS_OBJECT_REFERENCES) */

#include "ffi.h"
#include "j9.h"
#include "objhelp.h"
#include "ut_j9vm.h"
#include "vm_internal.h"
#include "stackwalk.h"

#include "AtomicSupport.hpp"
#include "VMHelpers.hpp"
#include "BytecodeAction.hpp"
#include "ObjectAllocationAPI.hpp"
#include "ObjectAccessBarrierAPI.hpp"

typedef struct ClassCastExceptionData {
	J9Class * currentClass;
	J9Class * nextClass;
} ClassCastExceptionData;

typedef enum {
	NO_EXCEPTION,
	NULL_POINTER_EXCEPTION,
	CLASS_CAST_EXCEPTION
} ExceptionType;

class VM_MHInterpreter
{
/*
 * Data members
 */
private:
	J9VMThread *const _currentThread;
	J9JavaVM *const _vm;
	MM_ObjectAllocationAPI *const _objectAllocate;
	MM_ObjectAccessBarrierAPI *const _objectAccessBarrier;

protected:

public:

/*
 * Function members
 */
private:

	/**
	 * Fetch the vmSlot field from the j.l.i.PrimitiveHandle.
	 * Note, the meaning of the vmSlot field depends on the type of the MethodHandle.
	 * @param primitiveHandle[in] A PrimitiveHandle object
	 * @return The vmSlot field from the j.l.i.PrimitiveHandle.
	 */
	VMINLINE UDATA
	getVMSlot(j9object_t primitiveHandle) const
	{
		return (UDATA)J9VMJAVALANGINVOKEPRIMITIVEHANDLE_VMSLOT(_currentThread, primitiveHandle);
	}

	/**
	 * Fetch the modifiers field from the j.l.i.PrimitiveHandle
	 * @param primitiveHandle[in] A PrimitiveHandle object
	 * @return The modifiers field from the j.l.i.PrimitiveHandle.
	 */
	VMINLINE U_32
	getPrimitiveHandleModifiers(j9object_t primitiveHandle) const
	{
		return J9VMJAVALANGINVOKEPRIMITIVEHANDLE_RAWMODIFIERS(_currentThread, primitiveHandle);
	}

	/**
	 * Fetch the defc field from the j.l.i.PrimitiveHandle
	 * @param primitiveHandle[in] A PrimitiveHandle object
	 * @return The defc field from the j.l.i.PrimitiveHandle.
	 */
	VMINLINE J9Class*
	getPrimitiveHandleDefc(j9object_t primitiveHandle) const
	{
		return J9VM_J9CLASS_FROM_HEAPCLASS(
				_currentThread,
				J9VMJAVALANGINVOKEPRIMITIVEHANDLE_DEFC(_currentThread, primitiveHandle));
	}

	/**
	 * Fetch the kind field from the j.l.i.MethodHandle
	 * @param methodHandle[in] A MethodHandle object
	 * @return The kind field from the j.l.i.MethodHandle.
	 */
	VMINLINE U_32
	getMethodHandleKind(j9object_t methodHandle) const
	{
		return J9VMJAVALANGINVOKEMETHODHANDLE_KIND(_currentThread, methodHandle);
	}

	/**
	 * Fetch the methodType field from the j.l.i.MethodHandle
	 * @param methodHandle[in] A MethodHandle object
	 * @return The methodType field from the j.l.i.MethodHandle.
	 */
	VMINLINE j9object_t
	getMethodHandleMethodType(j9object_t methodHandle) const
	{
		return J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(_currentThread, methodHandle);
	}

	/**
	 * Fetch the argSlots field from the j.l.i.MethodType.
	 * Note, the argSlots does not include the MethodHandle.
	 *
	 * @param methodType[in] A MethodType object
	 * @return The argSlots field from the j.l.i.MethodType.
	 */
	VMINLINE U_32
	getMethodTypeArgSlots(j9object_t methodType) const
	{
		return (U_32)J9VMJAVALANGINVOKEMETHODTYPE_ARGSLOTS(_currentThread, methodType);
	}

	/**
	 * Fetch the arrayClass field from the j.l.i.MethodHandle.
	 * @param methodHandle[in] A MethodHandle object
	 * @return The arrayClass field from the j.l.i.MethodHandle.
	 */
	VMINLINE j9object_t
	getMethodHandleArrayClass(j9object_t methodHandle) const
	{
		return J9VMJAVALANGINVOKESPREADHANDLE_ARRAYCLASS(_currentThread, methodHandle);
	}

	/**
	 * Fetch the filterPosition field from the j.l.i.MethodHandle.
	 * @param methodHandle[in] A MethodHandle object
	 * @return The filterPosition field from the j.l.i.MethodHandle.
	 */
	VMINLINE U_32
	getMethodHandleFilterPosition(j9object_t methodHandle) const
	{
		return (U_32)J9VMJAVALANGINVOKEFILTERARGUMENTSWITHCOMBINERHANDLE_FILTERPOSITION(_currentThread, methodHandle);
	}

	/**
	 * Fetch the foldPosition field from the j.l.i.MethodHandle.
	 * @param methodHandle[in] A MethodHandle object
	 * @return The foldPosition field from the j.l.i.MethodHandle.
	 */
	VMINLINE U_32
	getMethodHandleFoldPosition(j9object_t methodHandle) const
	{
		return (U_32)J9VMJAVALANGINVOKEFOLDHANDLE_FOLDPOSITION(_currentThread, methodHandle);
	}

	/**
	 * Fetch the next combinerHandle on stack from the j.l.i.MethodHandle.
	 * @param methodHandle[in] A MethodHandle object
	 * @return The combinerHandle from the j.l.i.MethodHandle.
	 */
	VMINLINE j9object_t
	getCombinerHandleForFold(j9object_t methodHandle) const
	{
		return J9VMJAVALANGINVOKEFOLDHANDLE_COMBINER(_currentThread, methodHandle);
	}

	VMINLINE j9object_t
	getCombinerHandleForFilter(j9object_t methodHandle) const
	{
		return J9VMJAVALANGINVOKEFILTERARGUMENTSWITHCOMBINERHANDLE_COMBINER(_currentThread, methodHandle);
	}

	/**
	 * Fetch the returnType field from the j.l.i.MethodType.
	 * @param methodType[in] A MethodType object
	 * @return The returnType field from the j.l.i.MethodType.
	 */
	VMINLINE J9Class*
	getMethodTypeReturnClass(j9object_t methodType) const
	{
		return J9VM_J9CLASS_FROM_HEAPCLASS(
				_currentThread,
				J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(_currentThread, methodType));
	}

	/**
	 * Fetch the arguments array field from the j.l.i.MethodType.
	 * @param methodType[in] A MethodType object
	 * @return The arguments array field from the j.l.i.MethodType.
	 */
	VMINLINE j9object_t
	getMethodTypeArguments(j9object_t methodType) const
	{
		return J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(_currentThread, methodType);
	}

	/**
	 * Determine if this Object is a subclass of j.l.i.VolatileCallSite.
	 * @param callsite[in] A Object that is a subclass of j.l.i.CallSite subclass
	 * @return a boolean indicating if it is a volatile callsite or not
	 */
	VMINLINE bool
	isVolatileCallsite(j9object_t callsite) const
	{
		J9Class *instanceClass = J9OBJECT_CLAZZ(_currentThread, callsite);
		J9Class *castClass = J9VMJAVALANGINVOKEVOLATILECALLSITE(_vm);
		return (0 != instanceOfOrCheckCast(instanceClass, castClass));
	}

	/**
	 * Drop all the incoming arguments, leaving only the MH receiver on the stack.
	 *
	 * @param methodHandle[in] The MethodHandle describing the stack
	 */
	VMINLINE void
	dropAllArgumentsLeavingMH(j9object_t methodHandle) const
	{
		j9object_t methodType = getMethodHandleMethodType(methodHandle);
		U_32 argSlots = getMethodTypeArgSlots(methodType);

		_currentThread->sp += argSlots;
	}

	/**
	 * Move slotCount number of stack slots to overwrite the receiver on the stack.
	 * This function is usually used to remove the MethodHandle from the stack prior to invoking
	 * the target method.
	 *
	 * @param slotCount[in] Number of stackslots to move.
	 */
	VMINLINE void
	shiftStackedArguments(U_32 slotCount) const
	{
		memmove(_currentThread->sp + 1, _currentThread->sp, slotCount * sizeof(UDATA));
	}

	/**
	 * Advance the PC to return from a send.
	 * @return EXECUTE_BYTECODE as the next interpreter action
	 */
	VMINLINE VM_BytecodeAction
	returnFromSend() const
	{
		_currentThread->pc += 3;
		return EXECUTE_BYTECODE;
	}

	/**
	 * Convert a interface table index into the correct virtual method for a given receiver class
	 *
	 * @param receiverClass[in] The class that will provide the method implementation (vtable slot)
	 * @param interfaceClass[in] The interface class to search for
	 * @param iTableIndex[in] The iTable index to convert
	 * @return The J9Method implementing that interface method or NULL.
	 */
	VMINLINE J9Method*
	convertITableIndexToVirtualMethod(J9Class *receiverClass, J9Class *interfaceClass, UDATA iTableIndex) const
	{
		J9Method *sendMethod = NULL;
		J9ITable *iTable = receiverClass->lastITable;
		if (interfaceClass == iTable->interfaceClass) {
			goto foundITable;
		}
		iTable = (J9ITable*)receiverClass->iTable;
		while (NULL != iTable) {
			if (interfaceClass == iTable->interfaceClass) {
				receiverClass->lastITable = iTable;
foundITable:
				sendMethod = *(J9Method**)((UDATA)receiverClass + ((UDATA*)(iTable + 1))[iTableIndex]);
				break;
			}
			iTable = iTable->next;
		}
		return sendMethod;
	}

	/**
	 * Unwind a J9SFMethodTypeFrame from the stack.
	 *
	 * @param frame[in] A pointer to the J9SFMethodTypeFrame
	 * @param spPriorToFrameBuild[in] The stack pointer prior to building the frame.
	 */
	VMINLINE void
	restoreMethodTypeFrame(J9SFMethodTypeFrame *frame, UDATA *spPriorToFrameBuild) const
	{
		_currentThread->literals = frame->savedCP;
		_currentThread->pc = frame->savedPC;
		_currentThread->arg0EA = UNTAGGED_A0(frame);
		_currentThread->sp = spPriorToFrameBuild;
	}

	/**
	 * @brief Insert a PlaceHolder method into the Java stack to preserve a single argument.
	 * The stack is modified as follows:
	 * 		[... MH args] --> [... preservedObject MethodFrame MH args]
	 * @param slotCount[in] Number of slots.  Use MethodType.argSlots
	 * @param preservedObject[in] Object to preserve.  Must match expected object type used in impdep1.
	 * @param placeHolderMethod[in] The J9Method to become literals.
	 */
	VMINLINE void
	insertPlaceHolderFrame(U_32 slotCount, j9object_t preservedObject, J9Method *placeHolderMethod) const
	{
		const UDATA sizeof_J9SFStackFrame_inSlots = (sizeof(J9SFStackFrame)/sizeof(UDATA*));
		((j9object_t *)_currentThread->sp)[slotCount] = preservedObject;
		UDATA *originalSP = _currentThread->sp;
		_currentThread->sp -= sizeof_J9SFStackFrame_inSlots + 1;
		memmove(_currentThread->sp, originalSP, (slotCount + 1) * sizeof(UDATA*));
		J9SFStackFrame *frame = (J9SFStackFrame *)(originalSP + slotCount - sizeof_J9SFStackFrame_inSlots);
		frame->savedCP = _currentThread->literals;
		frame->savedPC = _currentThread->pc;
		frame->savedA0 = (UDATA*)((UDATA)_currentThread->arg0EA | J9SF_A0_INVISIBLE_TAG);
		_currentThread->literals = placeHolderMethod;
		_currentThread->pc = _vm->impdep1PC;
		_currentThread->arg0EA = originalSP + slotCount;
	}

	/**
	 * @brief Insert a PlaceHolder method into the Java stack to preserve a four arguments.
	 * The stack is modified as follows:
	 * 		[... MH args] --> [... preservedObject1 preservedObject2 MethodFrame MH args]
	 * @param slotCount[in] Number of slots. Use MethodType.argSlots
	 * @param preservedObject1[in] Object to preserve. Must match expected object type used in impdep1.
	 * @param preservedValue1[in] U_32 to preserve.
	 * @param preservedValue2[in] U_32 to preserve.
	 * @param preservedValue3[in] U_32 to preserve.
	 * @param placeHolderMethod[in] The J9Method to become literals.
	 */
	VMINLINE void
	insertFourArgumentPlaceHolderFrame(U_32 slotCount, j9object_t preservedObject1, U_32 preservedValue1, U_32 preservedValue2, U_32 preservedValue3, J9Method *placeHolderMethod) const
	{
		const UDATA sizeof_J9SFStackFrame_inSlots = (sizeof(J9SFStackFrame)/sizeof(UDATA*));
		((j9object_t *)_currentThread->sp)[slotCount] = preservedObject1;
		UDATA *originalSP = _currentThread->sp;
		_currentThread->sp -= sizeof_J9SFStackFrame_inSlots + 4;
		memmove(_currentThread->sp, originalSP, (slotCount + 1) * sizeof(UDATA*));
		J9SFStackFrame *frame = (J9SFStackFrame *)(_currentThread->sp + slotCount + 1);
		frame->savedCP = _currentThread->literals;
		frame->savedPC = _currentThread->pc;
		frame->savedA0 = (UDATA*)((UDATA)_currentThread->arg0EA | J9SF_A0_INVISIBLE_TAG);
		_currentThread->literals = placeHolderMethod;
		_currentThread->pc = _vm->impdep1PC;
		_currentThread->arg0EA = originalSP + slotCount;
		*(U_32 *)(_currentThread->arg0EA - 1) = preservedValue1;
		*(U_32 *)(_currentThread->arg0EA - 2) = preservedValue2;
		*(U_32 *)(_currentThread->arg0EA - 3) = preservedValue3;
	}

	/**
	 * @brief Call class initializer <clinit> if the class is not already initialized.
	 * @param methodHandle[in] The MethodHandle describing the stack
	 * @return 
	 */
	VMINLINE j9object_t
	initializeClassIfNeeded(j9object_t methodHandle)
	{
		J9Class *defc = getPrimitiveHandleDefc(methodHandle);
		if (VM_VMHelpers::classRequiresInitialization(_currentThread, defc)) {
			/* Build frame, make pointers A0-relative offsets, and save methodHandle */
			UDATA *spPriorToFrameBuild = _currentThread->sp;
			J9SFMethodTypeFrame *frame = buildMethodTypeFrame(_currentThread, getMethodHandleMethodType(methodHandle));
			IDATA spOffset = spPriorToFrameBuild - _currentThread->arg0EA;
			IDATA frameOffset = (UDATA*)frame - _currentThread->arg0EA;
			VM_VMHelpers::pushObjectInSpecialFrame(_currentThread, methodHandle);

			/* Call defc.<clinit> */
			initializeClass(_currentThread, defc);

			/* Update methodHandle, make pointers absolute, restore frame  */
			methodHandle = VM_VMHelpers::popObjectInSpecialFrame(_currentThread);
			spPriorToFrameBuild = _currentThread->arg0EA + spOffset;
			frame = (J9SFMethodTypeFrame*)(_currentThread->arg0EA + frameOffset);
			restoreMethodTypeFrame(frame, spPriorToFrameBuild);
		}
		return methodHandle;
	}

	/**
	* @brief
	* Perform argument conversion for AsTypeHandle.
	* @param methodHandle
	* @return j9object_t The target MethodHandle (the one to execute next)
	*/
	j9object_t
	convertArgumentsForAsType(j9object_t methodHandle);

	/**
	* @brief
	* Perform an invoke generic for InvokeGenericHandle
	* @param methodHandle
	* @return j9object_t The target MethodHandle (the one to execute next)
	* 			or null if target MethodHandle is null
	*/
	j9object_t
	doInvokeGeneric(j9object_t methodHandle);

	/**
	* @brief
	* Perform argument spreading for SpreadHandle
	* @param methodHandle
	* @return j9object_t The target MethodHandle (the one to execute next)
	*/
	j9object_t
	spreadForAsSpreader(j9object_t methodHandle);

	/**
	* @brief
	* Perform argument collection for CollectHandle.
	* @param methodHandle
	* @param foundHeapOOM
	* @return j9object_t The target MethodHandle  (the one to execute next)
	*/
	j9object_t
	collectForAsCollector(j9object_t methodHandle, BOOLEAN *foundHeapOOM);

	/**
	* @brief
	* A helper function to calculate the total number of slots
	* before the specified position of argument on stack.
	* @param argumentTypes
	* @param argPosition
	* @return U_32 The argument slots before the position.
	*/
	U_32
	getArgSlotsBeforePosition(j9object_t argumentTypes, U_32 argPosition);

	/**
	* @brief
	* Perform argument folding for foldArguments.
	* @param methodHandle
	* @return j9object_t The target MethodHandle  (the one to execute combinerHandle)
	*/
	j9object_t
	foldForFoldArguments(j9object_t methodHandle);

	/**
	* @brief
	* Perform argument filtering for filterArgumentsWithCombiner.
	* @param methodHandle
	* @return j9object_t The target MethodHandle with argument filtered by combinerHandle
	*/
	j9object_t
	filterArgumentsWithCombiner(j9object_t methodHandle);

	/**
	* @brief
	* Insert the return value of combinerHandle to the argument list of foldHandle.
	* @return j9object_t The target MethodHandle  (the one to execute foldHandle)
	*/
	j9object_t
	insertReturnValueForFoldArguments();

	/**
	* @brief
	* Insert the return value of combinerHandle to the argument list of the methodHandle.
	* @return j9object_t The target MethodHandle
	*/
	j9object_t
	replaceReturnValueForFilterArgumentsWithCombiner();

	/**
	* @brief
	* Perform argument permuting for PermuteHandle.
	* This assumes no argument conversions are required by the PermuteHandle.
	* @param methodHandle
	* @return j9object_t The target MethodHandle (the one to execute next)
	*/
	j9object_t
	permuteForPermuteHandle(j9object_t methodHandle);

	/**
	* @brief
	* Perform argument insertion for InsertHandle.
	* This assumes no argument conversions are required by the InsertHandle.
	* @param methodHandle
	* @return j9object_t The target MethodHandle (the one to execute next)
	*/
	j9object_t
	insertArgumentsForInsertHandle(j9object_t methodHandle);

	/**
	* @brief
	* Used for argument spreading for SpreadHandle.
	* @param arrayObject
	* @param spreadCount
	* @param arrayClass
	*/
	void
	primitiveArraySpread(j9object_t arrayObject, I_32 spreadCount, J9Class *arrayClass);

	/**
	* @brief
	* Used for argument collection for CollectHandle.
	* @param collectedArgsArrayRef
	* @param collectArraySize
	* @param arrayComponentClass
	*/
	void
	primitiveArrayCollect(j9object_t collectedArgsArrayRef, U_32 collectArraySize, J9Class *arrayComponentClass);

	/* We use a j9object_t* as an argument since we need to read the pointer value
	 * from either a stack value (in the non-boxing case) or from inside the
	 * MethodType frame.  We deference the argument to get the actual pointer to
	 * to find where the object is, since the GC may have moved it in the boxing case
	 */
	ExceptionType
	convertArguments(UDATA * currentArgs, j9object_t *currentType, UDATA * nextArgs, j9object_t *nextType, UDATA explicitCast, ClassCastExceptionData *exceptionData);

	/**
	 * Verifies that the MethodHandle on the stack matches the function argument
	 * @param methodhandle Handle to check
	 * @returns 1 if the stack and the argument match, 0 otherwise
	 */
	U_8
	doesMHandStackMHMatch(j9object_t methodhandle);

	/**
	* @brief Validate that the arguments on the stack match the methodhandle's methodtype.
	* Will assert on failure and therefore doesn't have a return value.
	* Caller must hold vmAccess.
	* @param methodHandle - the methodhandle that should match the stack.
	* @return void
	*/
	void
	mhStackValidator(j9object_t methodhandle);

#if defined(ENABLE_DEBUG_FUNCTIONS)
	void
	printStack(U_32 numSlots)
	{
		printf("sp = %p  A0=%p\n", _currentThread->sp, _currentThread->arg0EA);
		for (int i = 0; i < numSlots; i++) {
			printf("sp[%p] = %p\n", _currentThread->sp + i, _currentThread->sp[i]);
		}
	}
#endif

protected:

public:
	/**
	 * Run the MethodHandle using the interpreter dispatch target implementation.
	 *
	 * @param methodHandle[in] the j.l.i.MethodHandle to run
	 * @return the next action for the interpreter
	 */
	VM_BytecodeAction
	dispatchLoop(j9object_t methodHandle);

	/**
	 * FilterReturn:
	 * 		[ ... MH bytecodeframe returnSlot0 returnslot1] --> [ ... MH returnSlot0 returnslot1]
	 * ConstructorHandle:
	 * 		[ ... newUninitializedObject bytecodeframe] --> [ ... newInitializedObject]
	 * FoldHandle:
	 * 		Refer to the implementation in MHInterpreter.inc
	 * GuardWithTestHandle:
	 * 		Refer to the implementation in MHInterpreter.inc
	 * FilterArgumentsHandle:
	 * 		Refer to the implementation in MHInterpreter.inc
	 */
	VM_BytecodeAction
	impdep1();

	VM_MHInterpreter(J9VMThread *currentThread, MM_ObjectAllocationAPI *objectAllocate, MM_ObjectAccessBarrierAPI *objectAccessBarrier)
			: _currentThread(currentThread)
			, _vm(_currentThread->javaVM)
			, _objectAllocate(objectAllocate)
			, _objectAccessBarrier(objectAccessBarrier)
	{ };
};

#endif /* MHINTERPRETER_HPP_ */

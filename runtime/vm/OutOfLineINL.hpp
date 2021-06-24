/*******************************************************************************
 * Copyright (c) 2016, 2021 IBM Corp. and others
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

#if !defined(VM_OUTOFLINEINL_HPP_)
#define VM_OUTOFLINEINL_HPP_

#include "j9.h"
#include "rommeth.h"
#include "BytecodeAction.hpp"

#include "objectreferencesmacros_undefine.inc"

extern "C" {

typedef VM_BytecodeAction J9OutOfLineINLMethod(J9VMThread *, J9Method *);

J9OutOfLineINLMethod OutOfLineINL_jdk_internal_misc_Unsafe_fullFence;
J9OutOfLineINLMethod OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile;
J9OutOfLineINLMethod OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeIntVolatile;
J9OutOfLineINLMethod OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeLongVolatile;
J9OutOfLineINLMethod OutOfLineINL_com_ibm_jit_JITHelpers_acmplt;
}

class VM_OutOfLineINL_Helpers {
public:
	static VMINLINE void
	returnVoid(J9VMThread *currentThread, UDATA slotCount)
	{
		currentThread->pc += 3;
		currentThread->sp += slotCount;
	}

	static VMINLINE void
	returnSingle(J9VMThread *currentThread, I_32 value, UDATA slotCount)
	{
		currentThread->pc += 3;
		currentThread->sp += (slotCount - 1);
		*(I_32 *)currentThread->sp = value;
	}

	static VMINLINE void
	returnDouble(J9VMThread *currentThread, I_64 value, UDATA slotCount)
	{
		currentThread->pc += 3;
		currentThread->sp += (slotCount - 2);
		*(I_64 *)currentThread->sp = value;
	}

	static VMINLINE void
	returnObject(J9VMThread *currentThread, j9object_t value, UDATA slotCount)
	{
		currentThread->pc += 3;
		currentThread->sp += (slotCount - 1);
		*(j9object_t *)currentThread->sp = value;
	}

	static VMINLINE void
	returnThis(J9VMThread *currentThread, UDATA slotCount)
	{
		currentThread->pc += 3;
		currentThread->sp += (slotCount - 1);
	}

	static VMINLINE UDATA*
	buildSpecialStackFrame(J9VMThread *currentThread, UDATA type, UDATA flags, bool visible)
	{
 		*--currentThread->sp = (UDATA)currentThread->arg0EA | (visible ? 0 : J9SF_A0_INVISIBLE_TAG);
 		UDATA *bp = currentThread->sp;
 		*--currentThread->sp = (UDATA)currentThread->pc;
 		*--currentThread->sp = (UDATA)currentThread->literals;
 		*--currentThread->sp = (flags);
 		currentThread->pc = (U_8*)(type);
 		currentThread->literals = NULL;
 		return bp;
 	}
 	
 	static VMINLINE UDATA
 	jitStackFrameFlags(J9VMThread *currentThread, UDATA constantFlags)
 	{
 		UDATA flags = currentThread->jitStackFrameFlags;
 		currentThread->jitStackFrameFlags = 0;
 		return flags | constantFlags;
 	}
 	
 	static VMINLINE void
 	restoreSpecialStackFrameLeavingArgs(J9VMThread *currentThread, UDATA *bp)
 	{
 		currentThread->sp = bp + 1;
 		currentThread->literals = (J9Method*)(bp[-2]);
 		currentThread->pc = (U_8*)(bp[-1]);
 		currentThread->arg0EA = (UDATA*)(bp[0] & ~(UDATA)J9SF_A0_INVISIBLE_TAG);
 	}
 	
 	static VMINLINE UDATA*
 	buildInternalNativeStackFrame(J9VMThread *currentThread, J9Method *method)
 	{
 		UDATA *bp = buildSpecialStackFrame(currentThread, J9SF_FRAME_TYPE_NATIVE_METHOD, jitStackFrameFlags(currentThread, 0), true);
 		*--currentThread->sp = (UDATA)method;
 		currentThread->arg0EA = bp + J9_ROM_METHOD_FROM_RAM_METHOD(method)->argCount;
 		return bp;
 	}
 
 	static VMINLINE void
 	restoreInternalNativeStackFrame(J9VMThread *currentThread)
 	{
 		J9SFNativeMethodFrame *nativeMethodFrame = (J9SFNativeMethodFrame*)currentThread->sp;
 		currentThread->jitStackFrameFlags = nativeMethodFrame->specialFrameFlags & J9_SSF_JIT_NATIVE_TRANSITION_FRAME;
 		restoreSpecialStackFrameLeavingArgs(currentThread, ((UDATA*)(nativeMethodFrame + 1)) - 1);
 	}
};

#include "objectreferencesmacros_define.inc"

#endif /* VM_OUTOFLINEINL_HPP_ */

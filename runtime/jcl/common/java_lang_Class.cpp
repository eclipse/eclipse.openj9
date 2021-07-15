/*******************************************************************************
 * Copyright (c) 1998, 2021 IBM Corp. and others
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

#include "jni.h"
#include "jcl.h"
#include "jclglob.h"
#include "jclprots.h"
#include "jcl_internal.h"
#include "objhelp.h"
#include "rommeth.h"
#include "vmaccess.h"
#include "java_lang_Class.h"
#include "ArrayCopyHelpers.hpp"
#include "j9jclnls.h"


#include "VMHelpers.hpp"

extern "C" {

typedef enum {
	STATE_INITIAL = -2,
	STATE_ERROR = -1,
	STATE_STOP = 0,
	STATE_IMPLIED = 1
} StackWalkingStates;

typedef enum {
	OBJS_ARRAY_IDX_ACC = 0,
	OBJS_ARRAY_IDX_PDS = 1,
	OBJS_ARRAY_IDX_PERMS_OR_CACHECHECKED = 2,
	OBJS_ARRAY_SIZE = 3
} ObjsArraySizeNindex;

#define 	STACK_WALK_STATE_MAGIC 		(void *)1
#define 	STACK_WALK_STATE_LIMITED_DOPRIVILEGED		(void *)2
#define 	STACK_WALK_STATE_FULL_DOPRIVILEGED		(void *)3

static UDATA isPrivilegedFrameIterator(J9VMThread * currentThread, J9StackWalkState * walkState);
static UDATA isPrivilegedFrameIteratorGetAccSnapshot(J9VMThread * currentThread, J9StackWalkState * walkState);
static UDATA frameIteratorGetAccSnapshotHelper(J9VMThread * currentThread, J9StackWalkState * walkState, j9object_t acc, j9object_t perm);
static j9object_t storePDobjectsHelper(J9VMThread* vmThread, J9Class* arrayClass, J9StackWalkState* walkState, j9object_t contextObject, U_32 arraySize, UDATA framesWalked, I_32 startPos, BOOLEAN dupCallerPD);
static BOOLEAN checkInnerClassAHelper(J9Class* declaringClass, J9Class* declaredClass);

jobject JNICALL 
Java_java_lang_Class_getDeclaredAnnotationsData(JNIEnv *env, jobject jlClass)
{
	jobject result = NULL;
	j9object_t clazz = NULL;
	J9VMThread *vmThread = (J9VMThread *) env;

	enterVMFromJNI(vmThread);
	clazz = J9_JNI_UNWRAP_REFERENCE(jlClass);
	if (NULL != clazz) {
		j9object_t annotationsData = getClassAnnotationData(vmThread, J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, clazz));
		if (NULL != annotationsData) {
			result = vmThread->javaVM->internalVMFunctions->j9jni_createLocalRef(env, annotationsData);
		}
	}
	exitVMToJNI(vmThread);
	return result;
}

static UDATA
isPrivilegedFrameIterator(J9VMThread * currentThread, J9StackWalkState * walkState)
{
	J9JavaVM *vm = currentThread->javaVM;
	J9JNIMethodID *doPrivilegedMethodID1 = (J9JNIMethodID *) vm->doPrivilegedMethodID1;
	J9JNIMethodID *doPrivilegedMethodID2 = (J9JNIMethodID *) vm->doPrivilegedMethodID2;
	J9JNIMethodID *doPrivilegedWithContextMethodID1 = (J9JNIMethodID *) vm->doPrivilegedWithContextMethodID1;
	J9JNIMethodID *doPrivilegedWithContextMethodID2 = (J9JNIMethodID *) vm->doPrivilegedWithContextMethodID2;
	J9Method *currentMethod = walkState->method;

	if (J9_ARE_ALL_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod)->modifiers, J9AccMethodFrameIteratorSkip)) {
		/* Skip methods with java.lang.invoke.FrameIteratorSkip / jdk.internal.vm.annotation.Hidden / java.lang.invoke.LambdaForm$Hidden annotation */
		return J9_STACKWALK_KEEP_ITERATING;
	}

	if (NULL == walkState->userData2) {
		J9Class * currentClass = J9_CLASS_FROM_CP(walkState->constantPool);
		if ((walkState->method == vm->jlrMethodInvoke)
			|| (walkState->method == vm->jliMethodHandleInvokeWithArgs)
			|| (walkState->method == vm->jliMethodHandleInvokeWithArgsList)
			|| (vm->srMethodAccessor && VM_VMHelpers::isSameOrSuperclass(J9VM_J9CLASS_FROM_JCLASS(currentThread, vm->srMethodAccessor), currentClass))
		) {
			/* skip reflection/MethodHandleInvoke frames */
		} else {
			return J9_STACKWALK_STOP_ITERATING;
		}
	}

	if ( ((doPrivilegedMethodID1 != NULL) && (currentMethod == doPrivilegedMethodID1->method)) ||
		 ((doPrivilegedMethodID2 != NULL) && (currentMethod == doPrivilegedMethodID2->method))
	) {
		/* Context is NULL */
		walkState->userData1 = NULL;
		walkState->userData2 = NULL;
	}

	if ( ((doPrivilegedWithContextMethodID1 != NULL) && (currentMethod == doPrivilegedWithContextMethodID1->method)) ||
		 ((doPrivilegedWithContextMethodID2 != NULL) && (currentMethod == doPrivilegedWithContextMethodID2->method))
	) {
		/* Grab the Context from the arguments: ultra-scary: fetch arg2 from the doPrivileged() method */
		walkState->userData1 = (void *)walkState->arg0EA[-1];
		walkState->userData2 = NULL;
	}

	return J9_STACKWALK_KEEP_ITERATING;
}

jobject JNICALL
Java_java_lang_Class_getStackClasses(JNIEnv *env, jclass jlHeapClass, jint maxDepth, jboolean stopAtPrivileged)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9StackWalkState walkState = {0};
	J9Class *jlClass = NULL;
	J9Class *arrayClass = NULL;
	UDATA walkFlags = J9_STACKWALK_CACHE_METHODS | J9_STACKWALK_COUNT_SPECIFIED | J9_STACKWALK_VISIBLE_ONLY | J9_STACKWALK_INCLUDE_NATIVES;
	UDATA framesWalked = 0;
	UDATA *cacheContents = NULL;
	UDATA i = 0;
	j9object_t arrayObject = NULL;
	jobject result = NULL;

	vmFuncs->internalEnterVMFromJNI(vmThread);

	/* Fetch the Class[] class before doing the stack walk. */
	jlClass = J9VMJAVALANGCLASS(vm);

	arrayClass = fetchArrayClass(vmThread, jlClass);
	if (NULL != vmThread->currentException) {
		goto _throwException;
	}

	/* Walk the stack, caching the constant pools of the frames.  If we're stopping at
	 * privileged frames, specify a frame walk function.
	 */
	walkState.skipCount = 2; /*skip this JNI frame and the local caller */
	walkState.userData1 = STACK_WALK_STATE_MAGIC; /* value which will not be written into userData1 by the frame walk function (which writes only null or an object pointer) */
	walkState.userData2 = STACK_WALK_STATE_MAGIC; /* value which will not be written into userData2 by the frame walk function (which writes only null) */
	walkState.maxFrames = maxDepth;
	walkState.walkThread = vmThread;

	if (stopAtPrivileged) {
		walkFlags |= J9_STACKWALK_ITERATE_FRAMES;
		walkState.frameWalkFunction = isPrivilegedFrameIterator;
	}
	walkState.flags = walkFlags;

	if (vm->walkStackFrames(vmThread, &walkState) != J9_STACKWALK_RC_NONE) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		goto _throwException;
	}

	/* If a privileged frame caused us to stop walking, do not include that
	 * frame (the last one walked) in the array.
	 */
	framesWalked = walkState.framesWalked;
	if (STACK_WALK_STATE_MAGIC != walkState.userData1) {
		framesWalked -= 1;
	}

	/* Translate cached CPs into J9Class * and nil any entries that are for reflect frames. */
	cacheContents = walkState.cache;

	for (i = framesWalked; i > 0; i--) {
		J9Method *currentMethod = (J9Method *)*cacheContents;
		J9Class *currentClass = J9_CLASS_FROM_METHOD(currentMethod);

		if ( J9_ARE_ALL_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod)->modifiers, J9AccMethodFrameIteratorSkip) ||
			 (vm->jliArgumentHelper && instanceOfOrCheckCast(currentClass, J9VM_J9CLASS_FROM_JCLASS(vmThread, vm->jliArgumentHelper))) ||
			 (vm->srMethodAccessor && vmFuncs->instanceOfOrCheckCast(currentClass, J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, *((j9object_t*) vm->srMethodAccessor)))) ||
			 (vm->srConstructorAccessor && vmFuncs->instanceOfOrCheckCast(currentClass, J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, *((j9object_t*) vm->srConstructorAccessor))))
		) {
			currentClass = NULL;
			framesWalked--;
		}
		*cacheContents++ = (UDATA) currentClass;
	}

	/* Allocate the result array. */
	arrayObject = vm->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, (U_32)framesWalked, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
	if (NULL == arrayObject) {
		vmFuncs->freeStackWalkCaches(vmThread, &walkState);
		vmFuncs->setHeapOutOfMemoryError(vmThread);
		goto _throwException;
	}

	/* Fill in the array. */
	if (framesWalked != 0) {
		I_32 resultIndex = 0;

		cacheContents = walkState.cache;

		for (i = framesWalked; i > 0; i--) {
			J9Class *clazz = (J9Class *)(*cacheContents++);
			/* Ignore zero entries (removed reflect frames). */
			while (NULL == clazz) {
				clazz = (J9Class *)(*cacheContents++);
			}
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, resultIndex, J9VM_J9CLASS_TO_HEAPCLASS(clazz));
			resultIndex++;
		}
	}

	vmFuncs->freeStackWalkCaches(vmThread, &walkState);
	result = vmFuncs->j9jni_createLocalRef(env, arrayObject);

_throwException:
	vmFuncs->internalExitVMToJNI(vmThread);

	return result;
}

jboolean JNICALL
Java_java_lang_Class_isClassADeclaredClass(JNIEnv *env, jobject jlClass, jobject aClass)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9Class *declaringClass = NULL;
	J9Class *declaredClass = NULL;
	jboolean result = JNI_FALSE;

	enterVMFromJNI(vmThread);

	declaringClass = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(jlClass));
	declaredClass = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(aClass));

	if (TRUE == checkInnerClassAHelper(declaringClass, declaredClass)) {
		result = JNI_TRUE;
	}

	exitVMToJNI(vmThread);
	return result;
}

jboolean JNICALL
Java_java_lang_Class_isClassAEnclosedClass(JNIEnv *env, jobject jlClass, jobject aClass)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9Class *enclosingClass = NULL;
	U_32 enclosedInnerClassCount = 0;
	J9Class *enclosedClass = NULL;
	J9UTF8 *enclosedClassName = NULL;
	J9SRP *srpCursor = NULL;
	jboolean result = JNI_FALSE;
	U_32 i = 0;

	enterVMFromJNI(vmThread);

	enclosingClass = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(jlClass));
	enclosedInnerClassCount = enclosingClass->romClass->enclosedInnerClassCount;
	enclosedClass = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(aClass));
	enclosedClassName = J9ROMCLASS_CLASSNAME(enclosedClass->romClass);

	if (TRUE == checkInnerClassAHelper(enclosingClass, enclosedClass)) {
		result = JNI_TRUE;
	} else {
		srpCursor = J9ROMCLASS_ENCLOSEDINNERCLASSES(enclosingClass->romClass);
		for (i = 0; i < enclosedInnerClassCount; i++) {
			J9UTF8 *enclosedInnerClassName = SRP_PTR_GET(srpCursor, J9UTF8 *);
			if (0 == compareUTF8Length(J9UTF8_DATA(enclosedClassName), J9UTF8_LENGTH(enclosedClassName),
					J9UTF8_DATA(enclosedInnerClassName), J9UTF8_LENGTH(enclosedInnerClassName))) {
				/* aClass' class name matches one of the enclosed inner classes of 'this',
				 * therefore aClass is one of this' enclosed classes */
				result = JNI_TRUE;
				break;
			}
			srpCursor++;
		}
	}

	exitVMToJNI(vmThread);
	return result;
}

static BOOLEAN
checkInnerClassAHelper(J9Class* declaringClass, J9Class* declaredClass)
{
	U_32 innerClassCount = declaringClass->romClass->innerClassCount;
	J9SRP *srpCursor = J9ROMCLASS_INNERCLASSES(declaringClass->romClass);
	J9UTF8* declaredClassName = J9ROMCLASS_CLASSNAME(declaredClass->romClass);
	U_32 i = 0;

	for (i = 0; i < innerClassCount; i++) {
		J9UTF8 *innerClassName = SRP_PTR_GET(srpCursor, J9UTF8 *);
		if (0 == compareUTF8Length(J9UTF8_DATA(declaredClassName), J9UTF8_LENGTH(declaredClassName),
				J9UTF8_DATA(innerClassName), J9UTF8_LENGTH(innerClassName))) {
			/* aClass' class name matches one of the inner classes of 'this',
			 * therefore aClass is one of this' declared classes */
			return TRUE;
		}
		srpCursor++;
	}
	return FALSE;
}

jboolean JNICALL
Java_java_lang_Class_isCircularDeclaringClass(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jboolean result = JNI_FALSE;

	vmFuncs->internalEnterVMFromJNI(currentThread);

	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9Class *currentClazz = clazz;
	J9UTF8 *outerClassName = J9ROMCLASS_OUTERCLASSNAME(currentClazz->romClass);
	while (NULL != outerClassName) {
		J9Class *outerClass = vmFuncs->internalFindClassUTF8(currentThread, J9UTF8_DATA(outerClassName),
								J9UTF8_LENGTH(outerClassName), currentClazz->classLoader, 0);
		if (NULL == outerClass) {
			break;
		} else if (clazz == outerClass) {
			result = JNI_TRUE;
			break;
		}
		currentClazz = outerClass;
		outerClassName = J9ROMCLASS_OUTERCLASSNAME(currentClazz->romClass);
	}

	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_com_ibm_oti_vm_VM_getClassNameImpl(JNIEnv *env, jclass recv, jclass jlClass)
{
	J9VMThread *currentThread = (J9VMThread *) env;
	J9JavaVM *vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jobject classNameRef = NULL;
	J9Class *clazz;
	J9ROMClass *romClass;
	U_8 *utfData = NULL;
	UDATA utfLength;
	UDATA freeUTFData = FALSE;
	U_8 onStackBuffer[64];
	bool anonClassName = false;

	vmFuncs->internalEnterVMFromJNI(currentThread);

	clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(jlClass));
	romClass = clazz->romClass;
	if (J9ROMCLASS_IS_ARRAY(clazz->romClass)) {
		J9ArrayClass *arrayClazz = (J9ArrayClass*)clazz;
		UDATA arity = arrayClazz->arity;
		J9Class *leafComponentType = arrayClazz->leafComponentType;
		J9ROMClass * leafROMClass = leafComponentType->romClass;
		J9UTF8 *leafName = J9ROMCLASS_CLASSNAME(leafROMClass);
		UDATA isPrimitive = J9ROMCLASS_IS_PRIMITIVE_TYPE(leafROMClass);

		/* Compute the length of the class name.
		 * 
		 * Primitive arrays are one [ per level plus the primitive type code
		 * 		e.g. [[[B
		 * Object arrays are one [ per level plus L plus the leaf type name plus ;
		 * 		e.g. [[[[Lpackage.name.Class;
		 */
		utfLength = arity;
		if (isPrimitive) {
			utfLength += 1;
		} else {
			utfLength += (J9UTF8_LENGTH(leafName) + 2);
		}

		/* Create the name in UTF8, using an on-stack buffer if possible */

		if (utfLength <= sizeof(onStackBuffer)) {
			utfData = onStackBuffer;
		} else {
			utfData = (U_8*)j9mem_allocate_memory(utfLength, J9MEM_CATEGORY_VM_JCL);
			freeUTFData = TRUE;
		}
		if (NULL == utfData) {
			vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
		} else {
			memset(utfData, '[', arity);
			if (isPrimitive) {
				utfData[arity] = J9UTF8_DATA(J9ROMCLASS_CLASSNAME(leafComponentType->arrayClass->romClass))[1];
			} else {
				/* The / to . conversion is done later, so just copy the RAW class name here */
				utfData[arity] = 'L';
				memcpy(utfData + arity + 1, J9UTF8_DATA(leafName), J9UTF8_LENGTH(leafName));
				utfData[utfLength - 1] = ';';
			}
		}
		anonClassName = J9_ARE_ANY_BITS_SET(leafROMClass->extraModifiers, J9AccClassAnonClass | J9AccClassHidden);
	} else {
		J9UTF8 *className = J9ROMCLASS_CLASSNAME(romClass);
		utfLength = J9UTF8_LENGTH(className);
		utfData = J9UTF8_DATA(className);
		anonClassName = J9_ARE_ANY_BITS_SET(romClass->extraModifiers, J9AccClassAnonClass | J9AccClassHidden);
	}

	if (NULL != utfData) {
		UDATA flags = J9_STR_INTERN | J9_STR_XLAT;
		if (anonClassName) {
			flags |= J9_STR_ANON_CLASS_NAME;
		}
		j9object_t classNameObject = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, utfData, utfLength, flags);
		if (NULL != classNameObject) {
			classNameRef = vmFuncs->j9jni_createLocalRef(env, classNameObject);
			if (NULL == classNameRef) {
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
			}
		}
		if (freeUTFData) {
			j9mem_free_memory(utfData);
		}
	}

	vmFuncs->internalExitVMToJNI(currentThread);
	return classNameRef;
}

jarray JNICALL
Java_java_lang_Class_getDeclaredFieldsImpl(JNIEnv *env, jobject recv)
{
	return getDeclaredFieldsHelper(env, recv);
}

jobject JNICALL
Java_java_lang_Class_getGenericSignature(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jobject result = NULL;
	j9object_t classObject = NULL;
	J9Class *clazz = NULL;
	J9ROMClass *romClass = NULL;
	J9UTF8 *signature = NULL;

	vmFuncs->internalEnterVMFromJNI(currentThread);
	classObject = J9_JNI_UNWRAP_REFERENCE(recv);
	clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, classObject);
	romClass = clazz->romClass;
	signature = getGenericSignatureForROMClass(vm, clazz->classLoader, romClass);
	if (NULL != signature) {
		j9object_t stringObject = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(signature), J9UTF8_LENGTH(signature), 0);
		result = vmFuncs->j9jni_createLocalRef(env, stringObject);
		releaseOptInfoBuffer(vm, romClass);
	}
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * Determines if a method is a normal method (not <clinit> or <init>).
 *
 * @param romMethod[in] the ROM method
 *
 * @returns true if the method is <init> or <clinit>, false if not
 */
static VMINLINE bool
isSpecialMethod(J9ROMMethod *romMethod)
{
	return '<' == (char)*J9UTF8_DATA(J9ROMMETHOD_NAME(romMethod));
}

/**
 * Determines if a method is <init>.
 *
 * @param romMethod[in] the ROM method
 *
 * @returns true if this is <init>, false if not
 */
static VMINLINE bool
isConstructor(J9ROMMethod *romMethod)
{
	return (J9_ARE_NO_BITS_SET(romMethod->modifiers, J9AccStatic)) && isSpecialMethod(romMethod);
}

/**
 * Determines if a method is a static method which should be reported.
 *
 * @param romMethod[in] the ROM method
 *
 * @returns true if this is public, static and not <clinit>, false if not
 */
static VMINLINE bool
isNormalStaticMethod(J9ROMMethod *romMethod)
{
	return J9_ARE_ALL_BITS_SET(romMethod->modifiers, J9AccPublic | J9AccStatic) && !isSpecialMethod(romMethod);
}

/**
 * Get the constructors for a Class.
 *
 * @param env[in] the JNIEnv
 * @param recv[in] the receiver
 * @param mustBePublic[in] true if only public methods are to be returned, false if not
 *
 * @returns true if this is <init>, false if not
 */
static jobject
getConstructorsHelper(JNIEnv *env, jobject recv, bool mustBePublic)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *mmFuncs = vm->memoryManagerFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	J9Class *arrayClass = fetchArrayClass(currentThread, J9VMJAVALANGREFLECTCONSTRUCTOR_OR_NULL(vm));

retry:
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	U_32 size = 0;
	UDATA preCount = vm->hotSwapCount;
	
	/* primitives/arrays don't have local methods */
	if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
		J9Method *currentMethod = clazz->ramMethods;
		J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
		while (currentMethod != endOfMethods) {
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (isConstructor(romMethod) && (!mustBePublic || J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccPublic))) {
				size += 1;
			}
			currentMethod += 1;
		}
	}
	
	if (NULL != arrayClass) {
		resultObject = mmFuncs->J9AllocateIndexableObject(currentThread, arrayClass, size, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (vm->hotSwapCount != preCount) {
			goto retry;
		} else if (NULL == resultObject) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
		} else {
			J9Method *currentMethod = clazz->ramMethods;
			U_32 index = 0;
			J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
			while (currentMethod != endOfMethods) {
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
				if (isConstructor(romMethod) && (!mustBePublic || J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccPublic))) {
					PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, resultObject);
					j9object_t element = vm->reflectFunctions.createConstructorObject(currentMethod, clazz, NULL, currentThread);
					resultObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);
					if (vm->hotSwapCount != preCount) {
						goto retry;
					} else if (NULL == element) {
						break;
					}
					J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, index, element);
					index += 1;
				}
				currentMethod += 1;
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * Get a particular constructor for a Class.
 *
 * @param env[in] the JNIEnv
 * @param recv[in] the receiver
 * @param parameterTypes[in] the parameter types array
 * @param signature[in] the signature
 * @param mustBePublic[in] true if only public methods are to be returned, false if not
 *
 * @returns true if this is <init>, false if not
 */
static jobject
getConstructorHelper(JNIEnv *env, jobject recv, jobject parameterTypes, jobject signature, bool mustBePublic)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);

retry:
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	if (NULL == signature) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		J9ROMClass *romClass = clazz->romClass;
		/* primitives/arrays don't have local methods */
		if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
			j9object_t signatureObject = J9_JNI_UNWRAP_REFERENCE(signature);
			J9Method *currentMethod = clazz->ramMethods;
			J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
			UDATA preCount = vm->hotSwapCount;
			while (currentMethod != endOfMethods) {
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
				if (isConstructor(romMethod) && (!mustBePublic || J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccPublic))) {
					J9UTF8 *signatureUTF = J9ROMMETHOD_SIGNATURE(romMethod);
					if (0 != vmFuncs->compareStringToUTF8(currentThread, signatureObject, TRUE, J9UTF8_DATA(signatureUTF), J9UTF8_LENGTH(signatureUTF))) {
						j9object_t parameterTypesObject = NULL;
						if (NULL != parameterTypes) {
							parameterTypesObject = J9_JNI_UNWRAP_REFERENCE(parameterTypes);
						}
						resultObject = vm->reflectFunctions.createConstructorObject(currentMethod, clazz, (j9array_t)parameterTypesObject, currentThread);
						if (vm->hotSwapCount != preCount) {
							goto retry;
						}
						break;
					}
				}
				currentMethod += 1;
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_allocateAndFillArray(JNIEnv *env, jobject recv, jint size)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *mmFuncs = vm->memoryManagerFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9Class *arrayClass = fetchArrayClass(currentThread, clazz);
	if (NULL != arrayClass) {
		resultObject = mmFuncs->J9AllocateIndexableObject(currentThread, arrayClass, (U_32)size, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == resultObject) {
oom:
			vmFuncs->setHeapOutOfMemoryError(currentThread);
		} else {
			for (U_32 i = 0; i < (U_32)size; ++i) {
				PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, resultObject);
				j9object_t element = mmFuncs->J9AllocateObject(currentThread, clazz, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
				resultObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);
				if (NULL == element) {
					goto oom;
				}
				J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, i, element);
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getConstructorImpl(JNIEnv *env, jobject recv, jobject parameterTypes, jobject signature)
{
	return getConstructorHelper(env, recv, parameterTypes, signature, true);
}

jobject JNICALL
Java_java_lang_Class_getConstructorsImpl(JNIEnv *env, jobject recv)
{
	return getConstructorsHelper(env, recv, true);
}

jobject JNICALL
Java_java_lang_Class_getDeclaredClassesImpl(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *mmFuncs = vm->memoryManagerFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *arrayClass = fetchArrayClass(currentThread, J9VMJAVALANGCLASS_OR_NULL(vm));

retry:
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	U_32 size = romClass->innerClassCount;
	UDATA preCount = vm->hotSwapCount;
	
	if (NULL != arrayClass) {
		resultObject = mmFuncs->J9AllocateIndexableObject(currentThread, arrayClass, size, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (vm->hotSwapCount != preCount) {
			goto retry;
		} else if (NULL == resultObject) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
		} else {
			J9ClassLoader *classLoader = clazz->classLoader;
			J9SRP *innerClasses = J9ROMCLASS_INNERCLASSES(romClass);

			for (U_32 i = 0; i < size; ++i) {
				J9UTF8 *className = NNSRP_PTR_GET(innerClasses, J9UTF8*);
				PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, resultObject);
				J9Class *innerClazz = vmFuncs->internalFindClassUTF8(currentThread, J9UTF8_DATA(className), J9UTF8_LENGTH(className), classLoader, J9_FINDCLASS_FLAG_THROW_ON_FAIL);
				resultObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);
				if (vm->hotSwapCount != preCount) {
					goto retry;
				}
				if (NULL == innerClazz) {
					break;
				}
				J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, i, J9VM_J9CLASS_TO_HEAPCLASS(innerClazz));
				innerClasses += 1;
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getDeclaredConstructorImpl(JNIEnv *env, jobject recv, jobject parameterTypes, jobject signature)
{
	return getConstructorHelper(env, recv, parameterTypes, signature, false);
}

jobject JNICALL
Java_java_lang_Class_getDeclaredConstructorsImpl(JNIEnv *env, jobject recv)
{
	return getConstructorsHelper(env, recv, false);
}

jobject JNICALL
Java_java_lang_Class_getDeclaredMethodImpl(JNIEnv *env, jobject recv, jobject name, jobject parameterTypes, jobject partialSignature, jobject startingPoint)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	
retry:
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	if ((NULL == name) || (NULL == partialSignature)) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		J9ROMClass *romClass = clazz->romClass;
		UDATA preCount = vm->hotSwapCount;

		/* primitives/arrays don't have local methods */
		if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
			j9object_t nameObject = J9_JNI_UNWRAP_REFERENCE(name);
			j9object_t signatureObject = J9_JNI_UNWRAP_REFERENCE(partialSignature);
			J9Method *currentMethod = clazz->ramMethods;
			J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
			if (NULL != startingPoint) {
				j9object_t methodObject = J9_JNI_UNWRAP_REFERENCE(startingPoint);
				J9JNIMethodID *id = vm->reflectFunctions.idFromMethodObject(currentThread, methodObject);
				currentMethod = id->method + 1;
			}
			while (currentMethod != endOfMethods) {
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
				if (!isSpecialMethod(romMethod)) {
					J9UTF8 *nameUTF = J9ROMMETHOD_NAME(romMethod);
					J9UTF8 *signatureUTF = J9ROMMETHOD_SIGNATURE(romMethod);
					if (0 != vmFuncs->compareStringToUTF8(currentThread, nameObject, FALSE, J9UTF8_DATA(nameUTF), J9UTF8_LENGTH(nameUTF))) {
						if (0 != compareJavaStringToPartialUTF8(currentThread, signatureObject, J9UTF8_DATA(signatureUTF), J9UTF8_LENGTH(signatureUTF))) {
							j9object_t parameterTypesObject = NULL;
							if (NULL != parameterTypes) {
								parameterTypesObject = J9_JNI_UNWRAP_REFERENCE(parameterTypes);
							}
							resultObject = vm->reflectFunctions.createDeclaredMethodObject(currentMethod, clazz, (j9array_t)parameterTypesObject, currentThread);
							if (vm->hotSwapCount != preCount) {
								goto retry;
							}
							break;
						}
					}
				}
				currentMethod += 1;
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getDeclaredMethodsImpl(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *mmFuncs = vm->memoryManagerFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *arrayClass = fetchArrayClass(currentThread, J9VMJAVALANGREFLECTMETHOD_OR_NULL(vm));
	
retry:
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	U_32 size = 0;
	UDATA preCount = vm->hotSwapCount;
	
	/* primitives/arrays don't have local methods */
	if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
		J9Method *currentMethod = clazz->ramMethods;
		J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
		while (currentMethod != endOfMethods) {
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (!isSpecialMethod(romMethod)) {
				size += 1;
			}
			currentMethod += 1;
		}
	}
	
	if (NULL != arrayClass) {
		resultObject = mmFuncs->J9AllocateIndexableObject(currentThread, arrayClass, size, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (vm->hotSwapCount != preCount) {
			goto retry;
		} else if (NULL == resultObject) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
		} else {
			J9Method *currentMethod = clazz->ramMethods;
			U_32 index = 0;
			J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
			while (currentMethod != endOfMethods) {
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
				if (!isSpecialMethod(romMethod)) {
					PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, resultObject);
					j9object_t element = vm->reflectFunctions.createDeclaredMethodObject(currentMethod, clazz, NULL, currentThread);
					resultObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);
					if (vm->hotSwapCount != preCount) {
						goto retry;
					}
					if (NULL == element) {
						break;
					}
					J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, index, element);
					index += 1;
				}
				currentMethod += 1;
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getDeclaringClassImpl(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	J9UTF8 *outerClassName = J9ROMCLASS_OUTERCLASSNAME(romClass);
	if (NULL != outerClassName) {
		J9Class *outerClass = vmFuncs->internalFindClassUTF8(currentThread, J9UTF8_DATA(outerClassName), J9UTF8_LENGTH(outerClassName), clazz->classLoader, J9_FINDCLASS_FLAG_THROW_ON_FAIL);
		resultObject = J9VM_J9CLASS_TO_HEAPCLASS(outerClass);
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getEnclosingObject(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	/* primitives/arrays don't have enclosing objects */
	if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
		J9EnclosingObject *methodRef = getEnclosingMethodForROMClass(vm, clazz->classLoader, romClass);
		if (NULL != methodRef) {
			J9ROMNameAndSignature *nas = J9ENCLOSINGOBJECT_NAMEANDSIGNATURE(methodRef);
			if (NULL != nas) {
				J9Class *resolvedClass = vmFuncs->resolveClassRef(currentThread, J9_CP_FROM_CLASS(clazz), methodRef->classRefCPIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
				if (NULL != resolvedClass) {
					J9UTF8 *enclosingMethodNameUTF = J9ROMNAMEANDSIGNATURE_NAME(nas);
					J9UTF8 *enclosingMethodSigUTF = J9ROMNAMEANDSIGNATURE_SIGNATURE(nas);
					J9Method *method = vmFuncs->searchClassForMethod(resolvedClass, J9UTF8_DATA(enclosingMethodNameUTF), J9UTF8_LENGTH(enclosingMethodNameUTF), J9UTF8_DATA(enclosingMethodSigUTF), J9UTF8_LENGTH(enclosingMethodSigUTF));
					if (NULL != method) {
						J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
						if (!isSpecialMethod(romMethod)) {
							resultObject = vm->reflectFunctions.createDeclaredMethodObject(method, resolvedClass, NULL, currentThread);
						} else if (isConstructor(romMethod)) {
							resultObject = vm->reflectFunctions.createDeclaredConstructorObject(method, resolvedClass, NULL, currentThread);
						}
					}
				}
			}
			releaseOptInfoBuffer(vm, romClass);
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getEnclosingObjectClass(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9ROMClass *romClass = clazz->romClass;
	/* primitives/arrays don't have enclosing objects */
	if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(romClass)) {
		J9EnclosingObject *methodRef = getEnclosingMethodForROMClass(vm, clazz->classLoader, romClass);
		if (NULL != methodRef) {
			J9Class *resolvedClass = vmFuncs->resolveClassRef(currentThread, J9_CP_FROM_CLASS(clazz), methodRef->classRefCPIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
			resultObject = J9VM_J9CLASS_TO_HEAPCLASS(resolvedClass);
			releaseOptInfoBuffer(vm, romClass);
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jobject JNICALL
Java_java_lang_Class_getFieldImpl(JNIEnv *env, jobject recv, jstring name)
{
	return getFieldHelper(env, recv, name);
}

jobject JNICALL
Java_java_lang_Class_getDeclaredFieldImpl(JNIEnv *env, jobject recv, jstring name)
{
	return getDeclaredFieldHelper(env, recv, name);
}

jarray JNICALL
Java_java_lang_Class_getFieldsImpl(JNIEnv *env, jobject recv)
{
	return getFieldsHelper(env, recv);
}

jobject JNICALL
Java_java_lang_Class_getMethodImpl(JNIEnv *env, jobject recv, jobject name, jobject parameterTypes, jobject partialSignature)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	j9object_t resultObject = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	PORT_ACCESS_FROM_VMC(currentThread);
	if ((NULL == name) || (NULL == partialSignature)) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
		J9ROMClass *romClass = clazz->romClass;

		/* primitives doesn't have local methods */
		if (!J9ROMCLASS_IS_PRIMITIVE_TYPE(romClass)) {
			J9Method *currentMethod = NULL;
			j9object_t nameObject = J9_JNI_UNWRAP_REFERENCE(name);
			j9object_t signatureObject = J9_JNI_UNWRAP_REFERENCE(partialSignature);
			UDATA lookupFLags = J9_LOOK_JNI | J9_LOOK_NO_THROW | J9_LOOK_PARTIAL_SIGNATURE | (J9ROMCLASS_IS_INTERFACE(romClass) ? (J9_LOOK_INTERFACE | J9_LOOK_NO_JLOBJECT) : 0);

			J9JNINameAndSignature nameAndSig;
			char nameBuffer[J9VM_PACKAGE_NAME_BUFFER_LENGTH];
			UDATA nameBufferLength = 0;
			char signatureBuffer[J9VM_PACKAGE_NAME_BUFFER_LENGTH];
			UDATA signatureLength = 0;
			nameAndSig.name = nameBuffer;
			nameAndSig.nameLength = 0;
			nameAndSig.signature = signatureBuffer;
			nameAndSig.signatureLength = 0;

			nameAndSig.name = vmFuncs->copyStringToUTF8WithMemAlloc(
				currentThread, nameObject, J9_STR_NULL_TERMINATE_RESULT, "", 0, nameBuffer, J9VM_PACKAGE_NAME_BUFFER_LENGTH, &nameBufferLength);
			if (NULL == nameAndSig.name) {
				goto _done;
			}
			nameAndSig.nameLength = (U_32)nameBufferLength;

			nameAndSig.signature = vmFuncs->copyStringToUTF8WithMemAlloc(
				currentThread, signatureObject, J9_STR_NULL_TERMINATE_RESULT | J9_STR_XLAT, "", 0, signatureBuffer, J9VM_PACKAGE_NAME_BUFFER_LENGTH, &signatureLength);
			if (NULL == nameAndSig.signature) {
				goto _done;
			}
			nameAndSig.signatureLength = (U_32)signatureLength;

			currentMethod = (J9Method *) vmFuncs->javaLookupMethodImpl(currentThread, clazz, ((J9ROMNameAndSignature *) &nameAndSig), NULL, lookupFLags, NULL);
			if (NULL == currentMethod) { /* by default we look for virtual methods.  Try static methods. */
				lookupFLags |= J9_LOOK_STATIC;
				currentMethod = (J9Method *) vmFuncs->javaLookupMethodImpl(currentThread, clazz, ((J9ROMNameAndSignature *) &nameAndSig), NULL, lookupFLags, NULL);
			}


			Trc_JCL_getMethodImpl_result(currentThread, nameAndSig.nameLength, nameAndSig.name, nameAndSig.signatureLength, nameAndSig.signature, currentMethod);
_done:
			if (nameAndSig.name != nameBuffer) {
				j9mem_free_memory((void *)nameAndSig.name);
			}
			if (nameAndSig.signature != signatureBuffer) {
				j9mem_free_memory((void *)nameAndSig.signature);
			}

			if (NULL != currentMethod) {
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
				if (J9_ARE_ALL_BITS_SET(romMethod->modifiers, J9AccPublic) && !isSpecialMethod(romMethod)) {
					j9object_t parameterTypesObject = NULL;
					if (NULL != parameterTypes) {
						parameterTypesObject = J9_JNI_UNWRAP_REFERENCE(parameterTypes);
					}
					resultObject = vm->reflectFunctions.createMethodObject(currentMethod, clazz, (j9array_t)parameterTypesObject, currentThread);
				}
			}
		}
	}
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jint JNICALL
Java_java_lang_Class_getStaticMethodCountImpl(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jint result = 0;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	do {
		J9ROMClass *romClass = clazz->romClass;
		J9Method *currentMethod = clazz->ramMethods;
		J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
		while (currentMethod != endOfMethods) {
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (isNormalStaticMethod(romMethod)) {
				result += 1;
			}
			currentMethod += 1;
		}
		clazz = VM_VMHelpers::getSuperclass(clazz);
	} while (NULL != clazz);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jboolean JNICALL
Java_java_lang_Class_getStaticMethodsImpl(JNIEnv *env, jobject recv, jobject array, jint start, jint count)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	jboolean result = JNI_TRUE;
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	U_32 index = (U_32)start;
	jint numMethodFound = 0;
	UDATA preCount = vm->hotSwapCount;

	do {
		J9ROMClass *romClass = clazz->romClass;
		J9Method *currentMethod = clazz->ramMethods;
		J9Method *endOfMethods = currentMethod + romClass->romMethodCount;

		while ((currentMethod != endOfMethods) && (numMethodFound < count)) {
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (isNormalStaticMethod(romMethod)) {
				J9JNIMethodID *methodID = vmFuncs->getJNIMethodID(currentThread, currentMethod);
				j9object_t resultObject = J9_JNI_UNWRAP_REFERENCE(array);
				j9object_t methodObject = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultObject, index);
				vm->reflectFunctions.fillInReflectMethod(methodObject, clazz, (jmethodID)methodID, currentThread);
				if (VM_VMHelpers::exceptionPending(currentThread)) {
					goto done;
				}
				index += 1;
				numMethodFound += 1;
			}
			currentMethod += 1;
			if (vm->hotSwapCount != preCount) {
				result = JNI_FALSE;
				goto done;
			}
		}
		clazz = VM_VMHelpers::getSuperclass(clazz);
	} while (NULL != clazz);

	if (numMethodFound != count) {
		result = JNI_FALSE;
	}
done:
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jint JNICALL
Java_java_lang_Class_getVirtualMethodCountImpl(JNIEnv *env, jobject recv)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));

	J9VTableHeader *vTableHeader = J9VTABLE_HEADER_FROM_RAM_CLASS(clazz);
	UDATA count = vTableHeader->size;
	J9Method **vTableMethods = J9VTABLE_FROM_HEADER(vTableHeader);
	/* assuming constant number of public final methods in java.lang.Object */
	jint result = 6;
	for (UDATA index = 0; index < count; ++index) {
		J9Method *currentMethod = vTableMethods[index];
		J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
		if (J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccPublic)) {
			if (VM_VMHelpers::hasDefaultConflictSendTarget(currentMethod)) {
				/*
				 * PR 104809: getMethods() does not report multiple default implementations of the same method.
				 * The method pointer has a link to the actual conflicting method.  Return the method that's actually in the VTable.
				 * Class.getMethods() will pick up the alternate implementation when it scans the class's interfaces.
				 */
				currentMethod = (J9Method *) (((UDATA)currentMethod->extra) & ~J9_STARTPC_NOT_TRANSLATED);
			}
			/* found a candidate, now reverse scan for a duplicate */
			for (UDATA scan = 0; scan < index; ++scan) {
				if (currentMethod == vTableMethods[scan]) {
					goto skip;
				}
			}
			result += 1;
		}
skip: ;
	}
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jboolean JNICALL
Java_java_lang_Class_getVirtualMethodsImpl(JNIEnv *env, jobject recv, jobject array, jint start, jint count)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	U_32 index = (U_32)start;
	jint numMethodFound = 0;
	jboolean result = JNI_TRUE;
	UDATA preCount = vm->hotSwapCount;

	/* First walk the vTable */
	{
		J9VTableHeader *vTableHeader = J9VTABLE_HEADER_FROM_RAM_CLASS(clazz);
		UDATA vTableSize = vTableHeader->size;
		J9Method **vTableMethods = J9VTABLE_FROM_HEADER(vTableHeader);
		for (UDATA progress = 0; ((progress < vTableSize) && (numMethodFound < count)); ++progress) {
			J9Method *currentMethod = vTableMethods[progress];
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccPublic)) {
				if (VM_VMHelpers::hasDefaultConflictSendTarget(currentMethod)) {
					/*
					 * PR 104809: getMethods() does not report multiple default implementations of the same method.
					 * Does this affect Class.getMethods()?
					 */
					currentMethod = (J9Method *) (((UDATA)currentMethod->extra) & ~J9_STARTPC_NOT_TRANSLATED);
				}
				/* found a candidate, now reverse scan for a duplicate */
				for (UDATA scan = 0; scan < progress; ++scan) {
					if (currentMethod == vTableMethods[scan]) {
						goto skip;
					}
				}
				J9Class *declaringClass = J9_CLASS_FROM_METHOD(currentMethod);
				J9JNIMethodID *methodID = vmFuncs->getJNIMethodID(currentThread, currentMethod);
				j9object_t resultObject = J9_JNI_UNWRAP_REFERENCE(array);
				j9object_t methodObject = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultObject, index);
				vm->reflectFunctions.fillInReflectMethod(methodObject, declaringClass, (jmethodID)methodID, currentThread);
				if (VM_VMHelpers::exceptionPending(currentThread)) {
					goto done;
				}
				index += 1;
				numMethodFound += 1;
			}

			if (vm->hotSwapCount != preCount) {
				result = JNI_FALSE;
				goto done;
			}
skip: ;
		}
	}
	/* Now add the public final methods from Object */
	{
		J9Class *objectClass = J9VMJAVALANGOBJECT_OR_NULL(vm);
		J9ROMClass *romClass = objectClass->romClass;
		J9Method *currentMethod = objectClass->ramMethods;
		J9Method *endOfMethods = currentMethod + romClass->romMethodCount;
		while ((currentMethod != endOfMethods) && (numMethodFound < count)) {
			J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
			if (J9_ARE_ALL_BITS_SET(romMethod->modifiers, J9AccPublic | J9AccFinal)) {
				J9JNIMethodID *methodID = vmFuncs->getJNIMethodID(currentThread, currentMethod);
				j9object_t resultObject = J9_JNI_UNWRAP_REFERENCE(array);
				j9object_t methodObject = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultObject, index);
				vm->reflectFunctions.fillInReflectMethod(methodObject, objectClass, (jmethodID)methodID, currentThread);
				if (VM_VMHelpers::exceptionPending(currentThread)) {
					goto done;
				}
				index += 1;
				numMethodFound += 1;
			}

			if (vm->hotSwapCount != preCount) {
				result = JNI_FALSE;
				goto done;
			}

			currentMethod += 1;
		}

		if (numMethodFound != count) {
			result = JNI_FALSE;
			goto done;
		}
	}
done:
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

jarray JNICALL
Java_java_lang_Class_getRecordComponentsImpl(JNIEnv *env, jobject cls)
{
	return getRecordComponentsHelper(env, cls);
}

jarray JNICALL
Java_java_lang_Class_permittedSubclassesImpl(JNIEnv *env, jobject cls)
{
	return permittedSubclassesHelper(env, cls);
}

static UDATA
frameIteratorGetAccSnapshotHelper(J9VMThread * currentThread, J9StackWalkState * walkState, j9object_t acc, j9object_t perm)
{
	PORT_ACCESS_FROM_VMC(currentThread);

	DoPrivilegedMethodArgs*	doPrivilegedMethodsArgs = (DoPrivilegedMethodArgs*)walkState->userData2;
	DoPrivilegedMethodArgs* doPrivilegedMethodsArgsTmp = (DoPrivilegedMethodArgs*)j9mem_allocate_memory(sizeof(DoPrivilegedMethodArgs), J9MEM_CATEGORY_VM_JCL);
	if (NULL == doPrivilegedMethodsArgsTmp) {
		currentThread->javaVM->internalVMFunctions->setNativeOutOfMemoryError(currentThread, 0, 0);
		return J9_STACKWALK_STOP_ITERATING;
	}
	memset(doPrivilegedMethodsArgsTmp, 0, sizeof(DoPrivilegedMethodArgs));

	doPrivilegedMethodsArgsTmp->accControlContext = acc;
	doPrivilegedMethodsArgsTmp->permissions = perm;
	while (NULL != doPrivilegedMethodsArgs->next) {
		doPrivilegedMethodsArgs = doPrivilegedMethodsArgs->next;
	}
	doPrivilegedMethodsArgs->next = doPrivilegedMethodsArgsTmp;
	return J9_STACKWALK_KEEP_ITERATING;
}

/**
 * PrivilegedFrameIterator method to perform stack walking for doPrivileged & doPrivilegedWithCombiner methods
 * For doPrivileged methods, this finds the callers of each doPrivileged method and the AccessControlContext discovered during stack walking, 
 * 		either from a privilege frame or the contextObject from current thread
 * For doPrivilegedWithCombiner, this finds the caller of doPrivilegedWithCombiner method, and the AccessControlContext
 * 		discovered during stack walking, either from a privilege frame or the contextObject from current thread
 *
 * 	Notes:
 * 		walkState.userData1
 * 			initial value is STACK_WALK_STATE_MAGIC((void*)1), set to NULL when a limited doPrivileged frame is discovered
 * 		walkState.userData2
 * 			initial value is contextObject from current thread, set to DoPrivilegedMethodArgs* when a limited doPrivileged frame is discovered
 * 		walkState.userData3
 * 			initial value is STACK_WALK_STATE_MAGIC((void*)1),
 * 			set to STACK_WALK_STATE_LIMITED_DOPRIVILEGED((void*)2) for searching the caller of a limited doPrivileged method,
 * 			and reset to STACK_WALK_STATE_MAGIC after identified the caller of that limited doPrivileged method
 * 			set to STACK_WALK_STATE_FULL_DOPRIVILEGED ((void*)3) for searching the caller of a full privileged doPrivileged method,
 * 			there is no need for resetting cause PrivilegedFrameIterator method exits with J9_STACKWALK_STOP_ITERATING
 * 		walkState.userData4 (only for doPrivilegedWithCombiner)
 * 			initial value is NULL, set to walkState->framesWalked when found the non reflection/MethodHandleInvoke frame caller of doPrivilegedWithCombiner
 *
 * @param currentThread		the VM Thread
 * @param walkState 		the stack walk state
 *
 * @return J9_STACKWALK_STOP_ITERATING, J9_STACKWALK_KEEP_ITERATING
 */
static UDATA
isPrivilegedFrameIteratorGetAccSnapshot(J9VMThread * currentThread, J9StackWalkState * walkState)
{
	J9JavaVM *vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9JNIMethodID *doPrivilegedMethodID1 = (J9JNIMethodID *) vm->doPrivilegedMethodID1;
	J9JNIMethodID *doPrivilegedMethodID2 = (J9JNIMethodID *) vm->doPrivilegedMethodID2;
	J9JNIMethodID *doPrivilegedWithContextMethodID1 = (J9JNIMethodID *) vm->doPrivilegedWithContextMethodID1;
	J9JNIMethodID *doPrivilegedWithContextMethodID2 = (J9JNIMethodID *) vm->doPrivilegedWithContextMethodID2;
	J9JNIMethodID *doPrivilegedWithContextPermissionMethodID1 = (J9JNIMethodID *) vm->doPrivilegedWithContextPermissionMethodID1;
	J9JNIMethodID *doPrivilegedWithContextPermissionMethodID2 = (J9JNIMethodID *) vm->doPrivilegedWithContextPermissionMethodID2;
	J9Method *currentMethod = walkState->method;

	if (J9_ARE_ALL_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod)->modifiers, J9AccMethodFrameIteratorSkip)) {
		/* Skip methods with java.lang.invoke.FrameIteratorSkip / jdk.internal.vm.annotation.Hidden / java.lang.invoke.LambdaForm$Hidden annotation */
		return J9_STACKWALK_KEEP_ITERATING;
	}

	if ((NULL == walkState->userData4)
		|| (STACK_WALK_STATE_LIMITED_DOPRIVILEGED == walkState->userData3)
		|| (STACK_WALK_STATE_FULL_DOPRIVILEGED == walkState->userData3)
	) {
		/* find the callers of each doPrivileged method */
		J9Class * currentClass = J9_CLASS_FROM_CP(walkState->constantPool);
		if ((walkState->method == vm->jlrMethodInvoke)
			|| (walkState->method == vm->jliMethodHandleInvokeWithArgs)
			|| (walkState->method == vm->jliMethodHandleInvokeWithArgsList)
			|| (vm->srMethodAccessor && VM_VMHelpers::isSameOrSuperclass(J9VM_J9CLASS_FROM_JCLASS(currentThread, vm->srMethodAccessor), currentClass))
		) {
			/* skip reflection/MethodHandleInvoke frames */
			return J9_STACKWALK_KEEP_ITERATING;
		} else {
			if (NULL == walkState->userData4) {
				/* find the caller of doPrivilegedWithCombiner */
				walkState->userData4 = (void*)walkState->framesWalked;
			} else {
				if (STACK_WALK_STATE_FULL_DOPRIVILEGED == walkState->userData3) {
					/* find the caller of full doPrivileged method */
					return J9_STACKWALK_STOP_ITERATING;
				}
				/* find the caller of limited doPrivileged method */
				Assert_JCL_notNull(walkState->userData2);
				DoPrivilegedMethodArgs*	doPrivilegedMethodsArgs = (DoPrivilegedMethodArgs*)walkState->userData2;
				while (NULL != doPrivilegedMethodsArgs->next) {
					doPrivilegedMethodsArgs = doPrivilegedMethodsArgs->next;
				}
				doPrivilegedMethodsArgs->frameCounter = walkState->framesWalked;
				/* reset to magic value to finish the search for the caller of current limited doPrivileged method */
				walkState->userData3 = STACK_WALK_STATE_MAGIC;
			}
		}
	}

	if ( ((doPrivilegedMethodID1 != NULL) && (currentMethod == doPrivilegedMethodID1->method))
		|| ((doPrivilegedMethodID2 != NULL) && (currentMethod == doPrivilegedMethodID2->method))
	) {
		/* Context is NULL */
		walkState->userData3 = STACK_WALK_STATE_FULL_DOPRIVILEGED;
		if (NULL == walkState->userData1) {	/* a limited doPrivileged frame was discovered */
			return frameIteratorGetAccSnapshotHelper(currentThread, walkState, NULL, NULL);
		} else {
			walkState->userData2 = NULL;	/* set NULL context */
		}
	}

	if ( ((doPrivilegedWithContextMethodID1 != NULL) && (currentMethod == doPrivilegedWithContextMethodID1->method)) ||
		 ((doPrivilegedWithContextMethodID2 != NULL) && (currentMethod == doPrivilegedWithContextMethodID2->method))
	) {
		/* Grab the Context from the arguments: ultra-scary: fetch arg2 from the doPrivileged() method */
		walkState->userData3 = STACK_WALK_STATE_FULL_DOPRIVILEGED;
		if (NULL == walkState->userData1) {	/* a limited doPrivileged frame was discovered */
			return frameIteratorGetAccSnapshotHelper(currentThread, walkState, (j9object_t)walkState->arg0EA[-1], NULL);
		} else {
			walkState->userData2 = (void *)walkState->arg0EA[-1];	/* grab context from doPrivileged frame */
		}
	}

	if ( ((doPrivilegedWithContextPermissionMethodID1 != NULL) && (currentMethod == doPrivilegedWithContextPermissionMethodID1->method))
		|| ((doPrivilegedWithContextPermissionMethodID2 != NULL) && (currentMethod == doPrivilegedWithContextPermissionMethodID2->method))
	) {
		walkState->userData3 = STACK_WALK_STATE_LIMITED_DOPRIVILEGED;
		if (NULL != walkState->userData1) {
			DoPrivilegedMethodArgs* doPrivilegedMethodsArgsTmp = (DoPrivilegedMethodArgs*)j9mem_allocate_memory(sizeof(DoPrivilegedMethodArgs), J9MEM_CATEGORY_VM_JCL);
			walkState->userData1 = NULL;	/* set to NULL when a limited doPrivileged frame is discovered */
			if (NULL == doPrivilegedMethodsArgsTmp) {
				currentThread->javaVM->internalVMFunctions->setNativeOutOfMemoryError(currentThread, 0, 0);
				return J9_STACKWALK_STOP_ITERATING;
			}
			memset(doPrivilegedMethodsArgsTmp, 0, sizeof(DoPrivilegedMethodArgs));
			doPrivilegedMethodsArgsTmp->accControlContext = (j9object_t)walkState->arg0EA[-1];
			doPrivilegedMethodsArgsTmp->permissions = (j9object_t)walkState->arg0EA[-2];

			walkState->userData2 = doPrivilegedMethodsArgsTmp;
		} else {
			return frameIteratorGetAccSnapshotHelper(currentThread, walkState, (j9object_t)walkState->arg0EA[-1], (j9object_t)walkState->arg0EA[-2]);
		}
	}

	return J9_STACKWALK_KEEP_ITERATING;
}

/**
 * The object array returned has following format:
 *
 * Pre-JEP140 format: AccessControlContext/ProtectionDomain..., and the length of the object array is NOT divisible by OBJS_ARRAY_SIZE
 * 	First element is an AccessControlContext object which might be null, either from a full permission privileged frame or from current thread if there is no such privileged frames
 * 	ProtectionDomain elements after AccessControlContext object could be in one of following two formats:
 * 	For doPrivileged methods - flag forDoPrivilegedWithCombiner is false:
 * 		the ProtectionDomain element might be null, first ProtectionDomain element is a duplicate of the ProtectionDomain of the caller of doPrivileged
 * 		rest of ProtectionDomain elements are from the callers discovered during stack walking 
 * 		the start index of the actual ProtectionDomain element is 2 of the object array returned 		
 * 	For doPrivilegedWithCombiner methods - flag forDoPrivilegedWithCombiner is true:
 * 		there are only two ProtectionDomain elements, first one is the ProtectionDomain of the caller of doPrivileged
 * 		and the other is the ProtectionDomain of the caller of doPrivilegedWithCombiner
 * 		the fourth element of the object array is NULL for padding to ensure that the length of the object array is NOT divisible by OBJS_ARRAY_SIZE either
 *
 * JEP 140 format: AccessControlContext/ProtectionDomain[]/Permission[]
 * 	The length of the object array is always divisible by OBJS_ARRAY_SIZE.
 * 	Depends on number of limited permission privileged frames, the result are in following format:
 * 		First element is an AccessControlContext object
 * 		Second element could be in one of following two formats:
 * 			For doPrivileged methods - flag forDoPrivilegedWithCombiner is false:
 * 				an array of ProtectionDomain objects in which first ProtectionDomain element is a duplicate of the ProtectionDomain of the caller of doPrivileged
 * 				the start index of the actual ProtectionDomain element is 1 of this ProtectionDomain objects array
 * 			For doPrivilegedWithCombiner methods - flag forDoPrivilegedWithCombiner is true:
 * 				an array of ProtectionDomain objects with only two elements
 * 				first one is the ProtectionDomain of the caller of doPrivileged 
 * 				and the other is the ProtectionDomain of the caller of doPrivilegedWithCombiner
 * 		Third element is an array of Limited Permission objects
 * 		Repeating this format:
 * 			AccessControlContext object, 
 * 			ProtectionDomain objects array with same format above when flag forDoPrivilegedWithCombiner is false
 * 			 or just the ProtectionDomain of the caller of doPrivileged in case of flag forDoPrivilegedWithCombiner is true
 * 			Permission object array
 * 		 Until a full permission privileged frame or the end of the stack reached.
 *
 * Note: 1. The reason to have Pre-JEP140 and JEP 140 format is to keep similar format and processing logic
 *			when there is no limited doPrivileged method (JEP 140 implementation) involved.
 *			This helped to address performance issue raised by JAZZ 66091: Perf work for LIR 28261 (Limited doPrivileged / JEP 140)
 *		 2. The reason to duplicate the ProtectionDomain object of the caller of doPrivileged is to avoid creating a new object array
 *			without NULL and duplicate ProtectionDomain objects discovered during stack walking while still keeping same order of
 *			those objects.
 *
 * @param env					The current thread.
 * @param jsAccessController	The receiver class
 * @param startingFrame			The frame to start stack walking
 * @param forDoPrivilegedWithCombiner the flag to indicate if it is for doPrivilegedWithCombiner method
 *
 * @return an array of objects as per comment above
 */
jobject JNICALL
Java_java_security_AccessController_getAccSnapshot(JNIEnv* env, jclass jsAccessController, jint startingFrame, jboolean forDoPrivilegedWithCombiner)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9StackWalkState walkState = {0};
	J9Class *arrayClass = NULL;
	UDATA i = 0;
	j9object_t contextObject = NULL;
	j9object_t arrayObject = NULL;
	jobject result = NULL;

	/* Trc_JCL_java_security_AccessController_getAccSnapshot_Entry(vmThread, startingFrame); */
	vmFuncs->internalEnterVMFromJNI(vmThread);

	/* Fetch the Object[] class before doing the stack walk. */
	arrayClass = fetchArrayClass(vmThread, J9VMJAVALANGOBJECT(vm));
	if (NULL != vmThread->currentException) {
		goto _walkStateUninitialized;
	}
	/* AccessControlContext is allocated in the same space as the thread, so no exception can occur */
	contextObject = vmThread->threadObject;
	if (NULL != contextObject) {
		contextObject = J9VMJAVALANGTHREAD_INHERITEDACCESSCONTROLCONTEXT(vmThread, contextObject);
	}
	/* Walk the stack, caching the constant pools of the frames. */
	walkState.skipCount = startingFrame + 1; /* skip this JNI frame as well */
	walkState.userData1 = STACK_WALK_STATE_MAGIC;	/* set to NULL when a limited doPrivileged frame is discovered */
	walkState.userData2 = contextObject;	/* initially set to contextObject, set to DoPrivilegedMethodArgs* when a limited doPrivileged frame is discovered */
	walkState.userData3 = STACK_WALK_STATE_MAGIC;	/* set to NULL when to find the caller of a limited doPrivileged method */
	walkState.userData4 = NULL;	/* when forDoPrivilegedWithCombiner is true, set to walkState->framesWalked of non reflection/MethodHandleInvoke frame caller of doPrivilegedWithCombiner */
	walkState.frameWalkFunction = isPrivilegedFrameIteratorGetAccSnapshot;
	walkState.flags = J9_STACKWALK_CACHE_CPS | J9_STACKWALK_VISIBLE_ONLY | J9_STACKWALK_INCLUDE_NATIVES | J9_STACKWALK_ITERATE_FRAMES;
	walkState.walkThread = vmThread;

	if (vm->walkStackFrames(vmThread, &walkState) != J9_STACKWALK_RC_NONE) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		goto _throwException;
	}
	if (STACK_WALK_STATE_MAGIC == walkState.userData1) {
		/* No limited doPrivileged or no doPrivileged frame discovered */
		if (forDoPrivilegedWithCombiner) {
			/* for doPrivilegedWithCombiner methods
			 * first slot: contextObject
			 * second slot: the caller of the ProtectionDomain of the caller of doPrivileged
			 * third slot: the ProtectionDomain of the caller of doPrivilegedWithCombiner
			 * fourth slot: NULL to ensure the total length of object array is not divisible by OBJS_ARRAY_SIZE
			 * */
			UDATA *cachePtr = NULL;
			UDATA framesWalked = (UDATA)walkState.userData4;
			Assert_JCL_true(framesWalked > 0);
			contextObject = (j9object_t)walkState.userData2;
			PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, contextObject);
			arrayObject = vmThread->javaVM->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, 4, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
			contextObject = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
			if (NULL == arrayObject) {
				vmThread->javaVM->internalVMFunctions->setHeapOutOfMemoryError(vmThread);
				goto _throwException;
			}
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, 0, contextObject);
			cachePtr = walkState.cache + (walkState.framesWalked - 1);
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, 1, J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr))));
			cachePtr = walkState.cache + (framesWalked - 1);
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, 2, J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr))));
		} else {
			/* contextObject in first slot, duplicate of the ProtectionDomain of the caller of doPrivileged in second slot (add 2 to size before allocation). */
			U_32 arraySize = (U_32)(walkState.framesWalked + 2);
			if (0 == (arraySize % OBJS_ARRAY_SIZE)) {
				arraySize++; /* increase one more slot to make sure the size of arrayObject is NOT divisible by OBJS_ARRAY_SIZE */
			}
			arrayObject = storePDobjectsHelper(vmThread, arrayClass, &walkState, (j9object_t)walkState.userData2, arraySize, walkState.framesWalked, 2, TRUE);
			if (NULL == arrayObject) {
				goto _throwException;
			}
		}
		result = vmFuncs->j9jni_createLocalRef(env, arrayObject);
	} else {
		/* at least a limited doPrivileged frame discovered */
		IDATA	signedCounter = 0;
		UDATA	nbrPDblockToBuild = 1;	/* the number of ProtectionDomain block walked, minimum 1 when no target method walked, i.e., walked whole stack */
		UDATA	counter = 0;
		DoPrivilegedMethodArgs*	dpMethodsArgs = NULL; /* args found within target methods */
		DoPrivilegedMethodArgs*	dpMethodsArgsTmp = NULL;

		PORT_ACCESS_FROM_JAVAVM(vm);

		dpMethodsArgs = (DoPrivilegedMethodArgs*)walkState.userData2;
		if (NULL == dpMethodsArgs) {
			goto _clearAllocation;
		}
		/* Array structure: AccessControlContext (could be null), ProtectionDomain object array, Permission object array (null means a normal method) */
		dpMethodsArgsTmp = dpMethodsArgs;
		if (NULL != dpMethodsArgsTmp->next) {
			/* look for top doPrivileged frame */
			do {
				nbrPDblockToBuild++;
				dpMethodsArgsTmp = dpMethodsArgsTmp->next;
			} while (NULL != dpMethodsArgsTmp->next);
		} else {
			/* only one frame */
		}
		if (NULL != dpMethodsArgsTmp->permissions) {
			/* top frame is a limited doPrivilege frame */
			DoPrivilegedMethodArgs* doPrivilegedMethodsArgsTmp2 = (DoPrivilegedMethodArgs*)j9mem_allocate_memory(sizeof(DoPrivilegedMethodArgs), J9MEM_CATEGORY_VM_JCL);
			if (NULL == doPrivilegedMethodsArgsTmp2) {
				vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
				goto _clearAllocation;
			}
			memset(doPrivilegedMethodsArgsTmp2, 0, sizeof(DoPrivilegedMethodArgs));
			doPrivilegedMethodsArgsTmp2->accControlContext = contextObject;
			doPrivilegedMethodsArgsTmp2->frameCounter = walkState.framesWalked;
			dpMethodsArgsTmp->next = doPrivilegedMethodsArgsTmp2;

			nbrPDblockToBuild++;
		} else {
			dpMethodsArgsTmp->frameCounter = walkState.framesWalked;
		}

		dpMethodsArgsTmp = dpMethodsArgs;
		counter = 0;
		do {
			PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, (j9object_t)dpMethodsArgsTmp->accControlContext);
			PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, (j9object_t)dpMethodsArgsTmp->permissions);
			dpMethodsArgsTmp = dpMethodsArgsTmp->next;
			counter++;
		} while (NULL != dpMethodsArgsTmp);
		arrayObject = vm->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, (U_32)nbrPDblockToBuild*OBJS_ARRAY_SIZE, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == arrayObject) {
			for (signedCounter = (counter - 1); signedCounter >= 0; signedCounter--) {
				DROP_OBJECT_IN_SPECIAL_FRAME(vmThread);
				DROP_OBJECT_IN_SPECIAL_FRAME(vmThread);
			}
			vmFuncs->setHeapOutOfMemoryError(vmThread);
			goto _clearAllocation;
		}
		for (signedCounter = (counter - 1); signedCounter >= 0; signedCounter--) {
			j9object_t permissions = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
			j9object_t accControlContext = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
			/* store accControlContext at index OBJS_ARRAY_IDX_ACC(0) */
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, signedCounter*OBJS_ARRAY_SIZE, accControlContext);
			/* store permissions at index OBJS_ARRAY_IDX_PERMS_OR_CACHECHECKED(2) */
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, (signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PERMS_OR_CACHECHECKED), permissions);
		}

		if (forDoPrivilegedWithCombiner) {
			/* for doPrivilegedWithCombiner methods
			 * first arrayObject[signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS] is an object array with length 2
			 * 	in which first element is the ProtectionDomain of the caller of doPrivileged
			 * second slot: the ProtectionDomain of the caller of doPrivilegedWithCombiner
			 * 	rest of arrayObject[signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS] are a single ProtectionDomain object of the callers of each limited/full doPrivileged methods
			 * */
			signedCounter = 0;
			dpMethodsArgsTmp = dpMethodsArgs;
			do {
				UDATA *cachePtr = NULL;
				if (0 == signedCounter) {
					/* this is first frame */
					UDATA	framesWalked = (UDATA)walkState.userData4;
					j9object_t pdArrayTmp = NULL;
					Assert_JCL_true(framesWalked > 0);

					PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, (j9object_t)arrayObject);
					pdArrayTmp = vm->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, 2, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
					arrayObject = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
					if (NULL == pdArrayTmp) {
						vmFuncs->setHeapOutOfMemoryError(vmThread);
						goto _clearAllocation;
					}
					cachePtr = walkState.cache + (dpMethodsArgsTmp->frameCounter - 1);
					J9JAVAARRAYOFOBJECT_STORE(vmThread, pdArrayTmp, 0, J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr))));
					cachePtr = walkState.cache + (framesWalked - 1);
					J9JAVAARRAYOFOBJECT_STORE(vmThread, pdArrayTmp, 1, J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr))));
					J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, (signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS), pdArrayTmp);
				}  else {
					cachePtr = walkState.cache + (dpMethodsArgsTmp->frameCounter - 1);
					J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, (signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS), J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr))));
				}
				signedCounter++;
				dpMethodsArgsTmp = dpMethodsArgsTmp->next;
			} while (NULL != dpMethodsArgsTmp);
		} else {
			UDATA *cachePtr = walkState.cache;
			j9object_t lastPD = NULL;
			j9object_t pdArrayTmp = NULL;
			UDATA lastFrameCounter = 0;
			signedCounter = 0;
			i = 0;

			dpMethodsArgsTmp = dpMethodsArgs;
			do {
				j9object_t pd = NULL;
				I_32 resultIndex = 1;

				/* The layout of ProtectionDomain object array is changed to following format:
				 * first element: the ProtectionDomain of the caller of doPrivileged
				 * rest elements: the ProtectionDomains of the callers discovered during stack walking including the caller of doPrivileged
				 * The reason to duplicate the ProtectionDomain of the caller of doPrivileged is to keep current structure and performance
				 * */
				PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, (j9object_t)arrayObject);
				pdArrayTmp = vm->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, (U_32)(dpMethodsArgsTmp->frameCounter - lastFrameCounter + 1), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
				arrayObject = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
				if (NULL == pdArrayTmp) {
					vmFuncs->setHeapOutOfMemoryError(vmThread);
					goto _clearAllocation;
				}
				lastFrameCounter = dpMethodsArgsTmp->frameCounter;
				do {
					pd = J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr)));
					cachePtr += 1;
					if ((NULL != pd) && (pd != lastPD)) {
						BOOLEAN duplicate = FALSE;
						IDATA	sc = 0;

						while (sc < signedCounter) {
							/* Scanning over object array already in previous limited doPrivileged frames */
							UDATA j = 0;
							j9object_t	pdTmp = J9JAVAARRAYOFOBJECT_LOAD(vmThread, arrayObject, (sc*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS));
							for (j = 1; j < J9INDEXABLEOBJECT_SIZE(vmThread, pdTmp); j++) {
								if (pd == J9JAVAARRAYOFOBJECT_LOAD(vmThread, pdTmp, j)) {
									duplicate = TRUE;
									break;
								}
							}
							if (TRUE == duplicate) {
								break;
							}
							sc++;
						}
						if (!duplicate) {
							I_32 scanIndex = 1;
							while (scanIndex < resultIndex) {
								/* Scanning over objects just saved in this doPrivileged frame */
								if (pd == J9JAVAARRAYOFOBJECT_LOAD(vmThread, pdArrayTmp, scanIndex)) {
									duplicate = TRUE;
									break;
								}
								scanIndex++;
							}
							if (!duplicate) {
								J9JAVAARRAYOFOBJECT_STORE(vmThread, pdArrayTmp, resultIndex, pd);
								resultIndex++;
							}
						}
						lastPD = pd;
					}
					i++;
				} while (i < dpMethodsArgsTmp->frameCounter);
				/* save the PD of the caller of doPrivileged at first element */
				if (NULL != pd) {
					J9JAVAARRAYOFOBJECT_STORE(vmThread, pdArrayTmp, 0, pd);
				}
				J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, (signedCounter*OBJS_ARRAY_SIZE + OBJS_ARRAY_IDX_PDS), pdArrayTmp);
				signedCounter++;
				dpMethodsArgsTmp = dpMethodsArgsTmp->next;
			} while (i < walkState.framesWalked);
		}
		result = vmFuncs->j9jni_createLocalRef(env, arrayObject);

_clearAllocation:
		if (NULL != dpMethodsArgs) {
			do {
				dpMethodsArgsTmp = dpMethodsArgs->next;
				j9mem_free_memory(dpMethodsArgs);
				dpMethodsArgs = dpMethodsArgsTmp;
			} while (NULL != dpMethodsArgsTmp);
		}
	}

_throwException:
	vmFuncs->freeStackWalkCaches(vmThread, &walkState);
_walkStateUninitialized:
	vmFuncs->internalExitVMToJNI(vmThread);
	/* Trc_JCL_java_security_AccessController_getAccSnapshot_Exit(vmThread, result); */
	return result;
}

/**
 * PrivilegedFrameIterator method to perform stack walking and find the caller specified by startingFrame
 *
 * @param currentThread		the VM Thread
 * @param walkState 		the stack walk state
 *
 * @return J9_STACKWALK_STOP_ITERATING, J9_STACKWALK_KEEP_ITERATING
 */
static UDATA
isPrivilegedFrameIteratorGetCallerPD(J9VMThread * currentThread, J9StackWalkState * walkState)
{
	if (J9_ARE_ALL_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(walkState->method)->modifiers, J9AccMethodFrameIteratorSkip)) {
		/* Skip methods with java.lang.invoke.FrameIteratorSkip / jdk.internal.vm.annotation.Hidden / java.lang.invoke.LambdaForm$Hidden annotation */
		return J9_STACKWALK_KEEP_ITERATING;
	}

	J9JavaVM *vm = currentThread->javaVM;
	J9Class * currentClass = J9_CLASS_FROM_CP(walkState->constantPool);
	if ((walkState->method == vm->jlrMethodInvoke)
		|| (walkState->method == vm->jliMethodHandleInvokeWithArgs)
		|| (walkState->method == vm->jliMethodHandleInvokeWithArgsList)
		|| (vm->srMethodAccessor && VM_VMHelpers::isSameOrSuperclass(J9VM_J9CLASS_FROM_JCLASS(currentThread, vm->srMethodAccessor), currentClass))
		/* there is no need to check srConstructorAccessor because doPrivilegedXX are method calls and not affected by Constructor reflection */
	) {
		/* skip reflection/MethodHandleInvoke frames */
		return J9_STACKWALK_KEEP_ITERATING;
	} else {
		/* find the caller */
		return J9_STACKWALK_STOP_ITERATING;
	}
}

/**
 * This native retrieves the ProtectionDomain object of the non-reflection/MethodHandleInvoke caller as per the startingFrame specified.

 * @param env					The current thread.
 * @param jsAccessController	The receiver class
 * @param startingFrame			The frame to start stack walking
 *
 * @return a ProtectionDomain object as per description above
 */
jobject JNICALL
Java_java_security_AccessController_getCallerPD(JNIEnv* env, jclass jsAccessController, jint startingFrame)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9StackWalkState walkState = {0};
	j9object_t pd = NULL;
	jobject result = NULL;
	UDATA *cachePtr = NULL;

	vmFuncs->internalEnterVMFromJNI(vmThread);

	walkState.skipCount = startingFrame + 1; /* skip this JNI frame as well */
	walkState.frameWalkFunction = isPrivilegedFrameIteratorGetCallerPD;
	walkState.flags = J9_STACKWALK_CACHE_CPS | J9_STACKWALK_VISIBLE_ONLY | J9_STACKWALK_INCLUDE_NATIVES | J9_STACKWALK_ITERATE_FRAMES;
	walkState.walkThread = vmThread;
	if (vm->walkStackFrames(vmThread, &walkState) != J9_STACKWALK_RC_NONE) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		goto _throwException;
	}
	Assert_JCL_true(walkState.framesWalked > 0);
	cachePtr = walkState.cache + (walkState.framesWalked - 1);
	pd = J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr)));
	if (NULL != pd) {
		result = vmFuncs->j9jni_createLocalRef(env, pd);
	}

_throwException:
	vmFuncs->freeStackWalkCaches(vmThread, &walkState);
	vmFuncs->internalExitVMToJNI(vmThread);

	return result;
}

/**
 * Helper method to store PD object discovered during stack walking
 *
 * @param vmThread[in] the current J9VMThread
 * @param arrayClass[in] the Object[] class
 * @param walkState[in] the stack walk state
 * @param contextObject[in] the AccessControlContext object
 * @param arraySize[in] the size of array object to be allocated
 * @param framesWalked[in] the frames walked
 * @param startPos[in] the start index of the actual ProtectionDomain object excluding acc & dup
 * @param dupCallerPD[in] the flag to indicate if the caller ProtectionDomain object is to be duplicated at index 1
 *
 * @returns an object array with AccessControlContext object at index 0 followed by ProtectionDomain objects
 */
static j9object_t
storePDobjectsHelper(J9VMThread* vmThread, J9Class* arrayClass, J9StackWalkState* walkState, j9object_t contextObject, U_32 arraySize, UDATA framesWalked, I_32 startPos, BOOLEAN dupCallerPD)
{
	j9object_t arrayObject = NULL;

	PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, contextObject);
	arrayObject = vmThread->javaVM->memoryManagerFunctions->J9AllocateIndexableObject(vmThread, arrayClass, arraySize, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
	contextObject = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
	if (NULL == arrayObject) {
		vmThread->javaVM->internalVMFunctions->setHeapOutOfMemoryError(vmThread);
		return	NULL;
	}
	J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, 0, contextObject);
	/* Fill in the array. */
	if (0 < framesWalked) {
		UDATA i = 0;
		j9object_t lastPD = NULL;
		I_32 resultIndex = startPos;
		UDATA *cachePtr = walkState->cache;
		j9object_t pd = NULL;
		for (i = framesWalked; i > 0; i--) {
			pd = J9VMJAVALANGCLASS_PROTECTIONDOMAIN(vmThread, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_CP(*cachePtr)));
			cachePtr += 1;
			if ((NULL != pd) && (pd != lastPD)) {
				I_32 scanIndex = startPos;
				BOOLEAN duplicate = FALSE;
				while (scanIndex < resultIndex) {
					/* Scanning over objects that were already read in this function, so no exception can occur */
					if (pd == J9JAVAARRAYOFOBJECT_LOAD(vmThread, arrayObject, scanIndex)) {
						duplicate = TRUE;
						break;
					}
					scanIndex++;
				}
				if (!duplicate) {
					J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, resultIndex, pd);
					resultIndex++;
				}
				lastPD = pd;
			}
		}
		if (dupCallerPD && NULL != pd) {
			J9JAVAARRAYOFOBJECT_STORE(vmThread, arrayObject, 1, pd);
		}
	}
	return arrayObject;
}


jobject JNICALL
Java_java_lang_Class_getNestHostImpl(JNIEnv *env, jobject recv)
{
#if JAVA_SPEC_VERSION >= 11
	J9VMThread *currentThread = (J9VMThread*)env;
	J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9Class *nestHost = clazz->nestHost;

	if (NULL == nestHost) {
		if (J9_VISIBILITY_ALLOWED == vmFuncs->loadAndVerifyNestHost(currentThread, clazz, J9_LOOK_NO_THROW)) {
			nestHost = clazz->nestHost;
		} else {
			/* If there is a failure loading or accessing the nest host, or if this class or interface does
			 * not specify a nest, then it is considered to belong to its own nest and this is returned as
			 * the host */
			nestHost = clazz;
		}
	}
	j9object_t resultObject = J9VM_J9CLASS_TO_HEAPCLASS(nestHost);
	jobject result = vmFuncs->j9jni_createLocalRef(env, resultObject);

	if (NULL == result) {
		vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
	}

	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
#else /* JAVA_SPEC_VERSION >= 11 */
	Assert_JCL_unimplemented();
	return NULL;
#endif /* JAVA_SPEC_VERSION >= 11 */
}

jobject JNICALL
Java_java_lang_Class_getNestMembersImpl(JNIEnv *env, jobject recv)
{
#if JAVA_SPEC_VERSION >= 11
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *mmFuncs = vm->memoryManagerFunctions;

	j9object_t resultObject = NULL;
	jobject result = NULL;
	J9ROMClass *romHostClass = NULL;
	U_16 nestMemberCount = 0;
	J9Class *jlClass = NULL;
	J9Class *arrayClass = NULL;

	vmFuncs->internalEnterVMFromJNI(currentThread);

	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	J9Class *nestHost = clazz->nestHost;

	if (NULL == nestHost) {
		if (J9_VISIBILITY_ALLOWED != vmFuncs->loadAndVerifyNestHost(currentThread, clazz, 0)) {
			goto _done;
		}
		nestHost = clazz->nestHost;
	}
	romHostClass = nestHost->romClass;
	nestMemberCount = romHostClass->nestMemberCount;

	/*  Grab java.lang.Class class for result object size */
	jlClass = J9VMJAVALANGCLASS_OR_NULL(vm);
	Assert_JCL_notNull(jlClass);
	arrayClass = fetchArrayClass(currentThread, jlClass);
	if (NULL != currentThread->currentException) {
		goto _done;
	}

	resultObject = mmFuncs->J9AllocateIndexableObject(currentThread, arrayClass, 1 + nestMemberCount, J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
	if (NULL == resultObject) {
		vmFuncs->setHeapOutOfMemoryError(currentThread);
		goto _done;
	}

	/* Host class is always in zeroeth index */
	J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, 0, J9VM_J9CLASS_TO_HEAPCLASS(nestHost));

	/* If host class claims nest members, they should be placed in second index onwards */
	if (0 != nestMemberCount) {
		J9SRP *nestMembers = J9ROMCLASS_NESTMEMBERS(romHostClass);
		U_16 i = 0;
		/* Classes in nest are in same runtime package & therefore have same classloader */
		J9ClassLoader *classLoader = clazz->classLoader;

		for (i = 0; i < nestMemberCount; i++) {
			J9UTF8 *nestMemberName = NNSRP_GET(nestMembers[i], J9UTF8 *);

			PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, resultObject);
			J9Class *nestMember = vmFuncs->internalFindClassUTF8(currentThread, J9UTF8_DATA(nestMemberName), J9UTF8_LENGTH(nestMemberName), classLoader, J9_FINDCLASS_FLAG_THROW_ON_FAIL);
			resultObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);

			if (NULL == nestMember) {
				/* If internalFindClassUTF8 fails to find the nest member, it sets
				 * a NoClassDefFoundError
				 */
				goto _done;
			} else if (NULL == nestMember->nestHost) {
				if (J9_VISIBILITY_ALLOWED != vmFuncs->loadAndVerifyNestHost(currentThread, nestMember, 0)) {
					goto _done;
				}
			}
			if (nestMember->nestHost != nestHost) {
				vmFuncs->setNestmatesError(currentThread, nestMember, nestHost, J9_VISIBILITY_NEST_MEMBER_NOT_CLAIMED_ERROR);
				goto _done;
			}
			J9JAVAARRAYOFOBJECT_STORE(currentThread, resultObject, i + 1, J9VM_J9CLASS_TO_HEAPCLASS(nestMember));
		}
	}

	result = vmFuncs->j9jni_createLocalRef(env, resultObject);

_done:
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
#else /* JAVA_SPEC_VERSION >= 11 */
	Assert_JCL_unimplemented();
	return NULL;
#endif /* JAVA_SPEC_VERSION >= 11 */
}

jboolean JNICALL 
Java_java_lang_Class_isHiddenImpl(JNIEnv *env, jobject recv)
{
#if JAVA_SPEC_VERSION >= 15
	jboolean result = JNI_FALSE;
	J9VMThread *currentThread = (J9VMThread*)env;
	J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	J9Class *clazz = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, J9_JNI_UNWRAP_REFERENCE(recv));
	result = J9ROMCLASS_IS_HIDDEN(clazz->romClass);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
#else /* JAVA_SPEC_VERSION >= 15 */
	Assert_JCL_unimplemented();
	return JNI_FALSE;
#endif /* JAVA_SPEC_VERSION >= 15 */
}

}

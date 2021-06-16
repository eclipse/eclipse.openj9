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

#include <string.h>
#include <stdlib.h>

#include "j9.h"
#include "jcl.h"
#include "j9consts.h"
#include "jni.h"
#include "j9protos.h"
#include "jcl_internal.h"
#include "jclprots.h"
#include "ut_j9jcl.h"
#include "VM_MethodHandleKinds.h"

#include "rommeth.h"
#include "j9vmnls.h"
#include "j9vmconstantpool.h"
#include "j9jclnls.h"
#include "cfreader.h"

/*
 * Note that the following native methods are implemented in sun_reflect_ConstantPool.c because
 * they effectively extend the ConstantPool API and require functionality from that module:
 * 		Java_java_lang_invoke_MethodHandleResolver_getCPTypeAt
 * 		Java_java_lang_invoke_MethodHandleResolver_getCPMethodTypeAt
 * 		Java_java_lang_invoke_MethodHandleResolver_getCPMethodHandleAt
 * 		Java_java_lang_invoke_MethodHandleResolver_getCPConstantDynamicAt
 */

static VMINLINE UDATA lookupImpl(J9VMThread *currentThread, J9Class *lookupClass, J9UTF8 *name, J9UTF8 *signature, J9Class *senderClass, UDATA options, BOOLEAN *foundDefaultConflicts);
static void registerVMNativesIfNoJIT(JNIEnv *env, jclass nativeClass, JNINativeMethod *natives, jint numNatives);
static jlong JNICALL vmInitialInvokeExactThunk(JNIEnv *env, jclass ignored);
static jlong JNICALL vmConvertITableIndexToVTableIndex(JNIEnv *env, jclass InterfaceMethodHandle, jlong interfaceArg, jint itableIndex, jlong receiverClassArg);
static void JNICALL vmFinalizeImpl(JNIEnv *env, jclass methodHandleClass, jlong thunkAddress);
static void JNICALL vmInvalidate(JNIEnv *env, jclass mutableCallSiteClass, jlongArray cookies);
static BOOLEAN accessCheckMethodSignature(J9VMThread *currentThread, J9Method *method, j9object_t methodType, J9UTF8 *lookupSig);
static BOOLEAN accessCheckFieldSignature(J9VMThread *currentThread, J9Class* lookupClass, UDATA romField, j9object_t methodType, J9UTF8 *lookupSig);
static char * expandNLSTemplate(J9VMThread *vmThread, const char *nlsTemplate, ...);

/**
 * Lookup static method name of type signature on lookupClass class.
 *
 * @param currentThread	The thread used in the lookup.
 * @param lookupClass	The class to do the lookup on
 * @param name			The method name to lookup.  Assumed to be verified.
 * @param signature		A valid method signature (JVM spec 4.4.3).
 *
 * @return a J9Method pointer or NULL on error.
 */
static UDATA
lookupStaticMethod(J9VMThread *currentThread, J9Class *lookupClass, J9UTF8 *name, J9UTF8 *signature)
{
	return lookupImpl(currentThread, lookupClass, name, signature, NULL, J9_LOOK_STATIC, NULL);
}

static UDATA
lookupInterfaceMethod(J9VMThread *currentThread, J9Class *lookupClass, J9UTF8 *name, J9UTF8 *signature, UDATA *methodIndex)
{
	J9Method *method;

	if (methodIndex == NULL) {
		return (UDATA) NULL;
	}
	*methodIndex = -1;

	/* Lookup the Interface method find its itable slot */
	method = (J9Method *) lookupImpl(currentThread, lookupClass, name, signature, NULL, J9_LOOK_INTERFACE, NULL);
	if (method != NULL) {
		J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;

		Assert_JCL_true(!J9_ARE_ANY_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(method)->modifiers, J9AccStatic));

		/* Starting Java 11 Nestmates, invokeInterface is allowed to target private interface methods */
		if ((J2SE_VERSION(currentThread->javaVM) < J2SE_V11) && J9_ARE_ANY_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(method)->modifiers, J9AccPrivate)) {
			/* [PR 67082] private interface methods require invokespecial, not invokeinterface.*/
			vmFuncs->setCurrentExceptionNLS(currentThread, J9VMCONSTANTPOOL_JAVALANGINCOMPATIBLECLASSCHANGEERROR, J9NLS_JCL_PRIVATE_INTERFACE_REQUIRES_INVOKESPECIAL);
			method = NULL;
		} else {
			if (J9_ARE_ANY_BITS_SET(J9_CLASS_FROM_METHOD(method)->romClass->modifiers, J9AccInterface)) {
				if (J9ROMMETHOD_IN_ITABLE(J9_ROM_METHOD_FROM_RAM_METHOD(method))) {
					*methodIndex = getITableIndexForMethod(method, lookupClass);
				} else {
					PORT_ACCESS_FROM_VMC(currentThread);
					J9Class *clazz = J9_CLASS_FROM_METHOD(method);
					J9UTF8 *className = J9ROMCLASS_CLASSNAME(clazz->romClass);
					J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
					J9UTF8 *methodName = J9ROMMETHOD_NAME(romMethod);
					J9UTF8 *sig = J9ROMMETHOD_SIGNATURE(romMethod);
					size_t nameAndSigLength = J9UTF8_LENGTH(className)
						+ J9UTF8_LENGTH(methodName) + J9UTF8_LENGTH(sig)
						+ 4 /* period, parentheses, and terminating null */;
					char *msg = j9mem_allocate_memory(nameAndSigLength, OMRMEM_CATEGORY_VM);
					if (NULL != msg) {
						j9str_printf(PORTLIB, msg, nameAndSigLength, "%.*s.%.*s(%.*s)",
								J9UTF8_LENGTH(className), J9UTF8_DATA(className),
								J9UTF8_LENGTH(methodName), J9UTF8_DATA(methodName),
								J9UTF8_LENGTH(sig), J9UTF8_DATA(sig));
						vmFuncs->setCurrentExceptionUTF(currentThread,
								J9VMCONSTANTPOOL_JAVALANGNOSUCHMETHODERROR, msg);
						j9mem_free_memory(msg);
					} else {
						vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
					}
					method = NULL;
				}
			}
		}
	}

	return (UDATA) method;
}

/*
 * See resolvesupport.c: resolveSpecialMethodRefInto
 */
static UDATA
lookupSpecialMethod(J9VMThread *currentThread, J9Class *lookupClass, J9UTF8 *name, J9UTF8 *signature, J9Class *specialCaller, BOOLEAN isForVirtualLookup)
{
	J9Method *method = NULL;
	BOOLEAN foundDefaultConflicts = FALSE;

	/* REASON FOR THE J9_LOOK_HANDLE_DEFAULT_METHOD_CONFLICTS:
	 * Virtual invocation modes may still find a method in the receiver's vtable that resolves the default method conflict.
	 * If not, the method in the vtable will be a special method for throwing the exception.
	 *
	 * Special invocations (defender supersends) will not look at the receiver's vtable, but instead invoke the result of javaLookupMethod.
	 * Default method conflicts must therefore be handled by the lookup code.
	 */
	UDATA lookupOptions = J9_LOOK_VIRTUAL | J9_LOOK_HANDLE_DEFAULT_METHOD_CONFLICTS;

	method = (J9Method*)lookupImpl(currentThread, lookupClass, name, signature, specialCaller, lookupOptions, &foundDefaultConflicts);

	if (foundDefaultConflicts) {
		J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;
		/* Don't need to clear the current exception here - it will be cleared when setting the exception. */
		vmFuncs->setCurrentExceptionWithCause(currentThread, J9VMCONSTANTPOOL_JAVALANGINVOKEDEFAULTMETHODCONFLICTEXCEPTION, NULL, currentThread->currentException);
	} else if (NULL != method) {
		J9Class *resolvedClass = J9_CLASS_FROM_METHOD(method);
		if (isForVirtualLookup || isDirectSuperInterface(currentThread, resolvedClass, specialCaller)) {
			/* CMVC 197763 seq#4: Use the method's class, not the lookupClass, as the resolvedClass arg.  LookupClass may be unrelated */
			method = getMethodForSpecialSend(currentThread, specialCaller, resolvedClass, method, J9_LOOK_VIRTUAL | J9_LOOK_NO_VISIBILITY_CHECK | J9_LOOK_IGNORE_INCOMPATIBLE_METHODS);
			Assert_JCL_notNull(method);
		} else {
			setIncompatibleClassChangeErrorInvalidDefenderSupersend(currentThread, resolvedClass, specialCaller);
			method = NULL;
		}
	}

	return (UDATA)method;
}


/**
 * Wrapper around javalookupMethod - packs the name and signature into a J9ROMNameAndSignature struct and calls
 * javaLookupMethod.
 *
 * Returns a J9Method* or NULL.
 */
static UDATA
lookupImpl(J9VMThread *currentThread, J9Class *lookupClass, J9UTF8 *name, J9UTF8 *signature, J9Class *senderClass, UDATA options, BOOLEAN *foundDefaultConflicts)
{
	J9Method *method;
	J9NameAndSignature nameAndSig;
	J9JavaVM * vm = currentThread->javaVM;

	Assert_JCL_notNull(lookupClass);
	Assert_JCL_notNull(name);
	Assert_JCL_notNull(signature);

	nameAndSig.name = name;
	nameAndSig.signature = signature;
	method = (J9Method *) vm->internalVMFunctions->javaLookupMethodImpl(currentThread, lookupClass, (J9ROMNameAndSignature *)&nameAndSig, senderClass, options /* | J9_LOOK_NO_THROW | J9_LOOK_CLCONSTRAINTS */ | J9_LOOK_DIRECT_NAS, foundDefaultConflicts);

	return (UDATA) method;
}

void JNICALL
Java_java_lang_invoke_MethodHandle_requestCustomThunkFromJit(JNIEnv* env, jobject handle, jobject thunk)
{
	J9VMThread *vmThread = ((J9VMThread *) env);
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	vm->jitConfig->translateMethodHandle(
		vmThread, 
		J9_JNI_UNWRAP_REFERENCE(handle), 
		J9_JNI_UNWRAP_REFERENCE(thunk), 
		J9_METHOD_HANDLE_COMPILE_CUSTOM);
	vmFuncs->internalExitVMToJNI(vmThread);
}

jclass JNICALL
Java_java_lang_invoke_PrimitiveHandle_lookupMethod(JNIEnv *env, jobject handle, jclass lookupClass, jstring name, jstring signature, jbyte kind, jclass specialCaller)
{
	J9VMThread *vmThread = ((J9VMThread *) env);
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9UTF8 *nameUTF8 = NULL;
	char nameUTF8Buffer[256];
	J9UTF8 *signatureUTF8 = NULL;
	char signatureUTF8Buffer[256];
	UDATA method;
	J9Class *j9LookupClass;
	J9Class *j9SpecialCaller = NULL;
	UDATA vmSlotValue = (UDATA)-1;		/* Set to invalid value so easy to find.*/
	jclass result = NULL;
	BOOLEAN isForVirtualLookup = FALSE;
	PORT_ACCESS_FROM_ENV(env);

	vmFuncs->internalEnterVMFromJNI(vmThread);

	nameUTF8 = vmFuncs->copyStringToJ9UTF8WithMemAlloc(vmThread, J9_JNI_UNWRAP_REFERENCE(name), J9_STR_NONE, "", 0, nameUTF8Buffer, sizeof(nameUTF8Buffer));

	if (nameUTF8 == NULL) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		goto _cleanup;
	}

	signatureUTF8 = vmFuncs->copyStringToJ9UTF8WithMemAlloc(vmThread, J9_JNI_UNWRAP_REFERENCE(signature), J9_STR_NONE, "", 0, signatureUTF8Buffer, sizeof(signatureUTF8Buffer));

	if (signatureUTF8 == NULL) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		goto _cleanup;
	}

	j9LookupClass = J9VM_J9CLASS_FROM_JCLASS(vmThread, lookupClass);
	Assert_JCL_notNull(j9LookupClass);

	switch(kind){
	case J9_METHOD_HANDLE_KIND_STATIC:
		method = lookupStaticMethod(vmThread, j9LookupClass, nameUTF8, signatureUTF8);
		vmSlotValue = method;

		if (NULL != (J9Method*)method) {
			J9Class *methodClass = J9_CLASS_FROM_METHOD((J9Method *)method);
		
			if (methodClass != j9LookupClass) {
				if (J9AccInterface == (j9LookupClass->romClass->modifiers & J9AccInterface)) {
					/* Throws NoSuchMethodError (an IncompatibleClassChangeError subclass).
					 * This will be converted to NoSuchMethodException
					 * by the finishMethodInitialization() call in DirectHandle constructor.
					 */
					vmFuncs->setCurrentException(vmThread, J9VMCONSTANTPOOL_JAVALANGNOSUCHMETHODERROR, NULL);
					goto _cleanup;
				}
			}
		}

		break;
	case J9_METHOD_HANDLE_KIND_INTERFACE:
		method = lookupInterfaceMethod(vmThread, j9LookupClass, nameUTF8, signatureUTF8, &vmSlotValue);
		break;
	case J9_METHOD_HANDLE_KIND_VIRTUAL:
		/* Instead of looking up the VirtualHandle directly, we look up the equivalent DirectHandle(KIND_SPECIAL)
		 * and then convert it into a VirtualHandle using setVMSlotAndRawModifiersFromSpecialHandle().  This is necessary
		 * as not every method invokeable by invokevirtual is in the vtable (ie: final methods in Object).
		 * The reason why we can't go directly to the KIND_SPECIAL case is because we need to allow lookup of VirtualHandle
		 * for methods in indirect super interfaces. DirectHandle(KIND_SPECIAL) can NOT be used for methods in indirect
		 * super interfaces.
		 */
		isForVirtualLookup = TRUE;
		kind = J9_METHOD_HANDLE_KIND_SPECIAL;
		J9VMJAVALANGINVOKEMETHODHANDLE_SET_KIND(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), J9_METHOD_HANDLE_KIND_SPECIAL);
		/* In order to allow lookup of virtual methods in indirect super interfaces, we need to be able to differentiate
		 * between KIND_VIRTUAL and KIND_SPECIAL below.
		 */
		/* Fall through */
	case J9_METHOD_HANDLE_KIND_CONSTRUCTOR:
		/* Fall through - <init> gets invoked by invokespecial so this should be able to find it */
	case J9_METHOD_HANDLE_KIND_SPECIAL:
		Assert_JCL_notNull(specialCaller);
		j9SpecialCaller = J9VM_J9CLASS_FROM_JCLASS(vmThread, specialCaller);
		method = lookupSpecialMethod(vmThread, j9LookupClass, nameUTF8, signatureUTF8, j9SpecialCaller, isForVirtualLookup);
		vmSlotValue = method;
		break;
	default:
		/* Assert here - illegal should never be called with something that doesn't directly map to kind */
		Assert_JCL_unreachable();
		goto _cleanup;
	}
	
	if (method != (UDATA) NULL) {
		/* Check signature for classloader visibility */
		if (!accessCheckMethodSignature(vmThread, (J9Method*)method, J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(vmThread, J9_JNI_UNWRAP_REFERENCE(handle)), signatureUTF8)) {
			J9Class *methodClass = J9_CLASS_FROM_METHOD((J9Method*)method);
			setClassLoadingConstraintLinkageError(vmThread, methodClass, signatureUTF8);
			goto _cleanup;
		}

		J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), vmSlotValue);
		J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), J9_ROM_METHOD_FROM_RAM_METHOD(method)->modifiers);
		result = vmFuncs->j9jni_createLocalRef(env, J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_METHOD((J9Method*)method)));
	}

_cleanup:
	vmFuncs->internalExitVMToJNI(vmThread);

	if (signatureUTF8 != (J9UTF8*)signatureUTF8Buffer) {
		j9mem_free_memory(signatureUTF8);
	}

	if (nameUTF8 != (J9UTF8*)nameUTF8Buffer) {
		j9mem_free_memory(nameUTF8);
	}

	return result;	
}

static BOOLEAN
accessCheckFieldSignature(J9VMThread *currentThread, J9Class* lookupClass, UDATA romField, j9object_t methodType, J9UTF8 *lookupSig)
{

	J9JavaVM *vm = currentThread->javaVM;
	J9BytecodeVerificationData *verifyData = vm->bytecodeVerificationData;
	BOOLEAN result = TRUE;

	/* If the verifier isn't enabled, accept the access check unconditionally */
	if (NULL != verifyData) {
		U_8 *lookupSigData = J9UTF8_DATA(lookupSig);
		U_32 sigOffset = 0;

		while ('[' == lookupSigData[sigOffset]) {
			sigOffset += 1;
		}
	
		if ('L' == lookupSigData[sigOffset]) {
			BOOLEAN isVirtual = (0 == (((J9ROMFieldShape*)romField)->modifiers & J9AccStatic));
			j9object_t argsArray = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(currentThread, methodType);
			U_32 numParameters = J9INDEXABLEOBJECT_SIZE(currentThread, argsArray);
			j9object_t clazz = NULL;
			J9Class *ramClass = NULL;
			J9ClassLoader *targetClassloader = lookupClass->classLoader;

			/* Move past the L in the Lfoo; signature */
			sigOffset += 1;

			/* '()X' or '(Receiver)X' are both getters.  All others are setters */
			if (((0 == numParameters) && !isVirtual) || ((1 == numParameters) && isVirtual)) {
				clazz = J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(currentThread, methodType);
			} else {
				U_32 argumentIndex = 0;

				if (isVirtual) {
					argumentIndex = 1;
				}

				clazz = J9JAVAARRAYOFOBJECT_LOAD(currentThread, argsArray, argumentIndex);
			}

			ramClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, clazz);
			if (ramClass->classLoader != targetClassloader) {
				/* -1 on the length to remove the ; and the end of the signature */
				U_32 sigLength = J9UTF8_LENGTH(lookupSig) - sigOffset - 1;
				
				omrthread_monitor_enter(vm->classTableMutex);
				if(verifyData->checkClassLoadingConstraintForNameFunction(currentThread, targetClassloader, ramClass->classLoader, &lookupSigData[sigOffset], &lookupSigData[sigOffset], sigLength, TRUE) != 0) {
					result = FALSE;
				}
				omrthread_monitor_exit(vm->classTableMutex);
			}
		}
	}
	return result;
}

static BOOLEAN
accessCheckMethodSignature(J9VMThread *currentThread, J9Method *method, j9object_t methodType, J9UTF8 *lookupSig)
{
	J9JavaVM *vm = currentThread->javaVM;
	J9BytecodeVerificationData *verifyData = vm->bytecodeVerificationData;
	BOOLEAN result = TRUE;
	
	/* If the verifier isn't enabled, accept the check unconditionally */
	if (NULL != verifyData) {
		U_8 *lookupSigData = J9UTF8_DATA(lookupSig);
		/* Grab the args array and length from MethodType.arguments */
		j9object_t argsArray = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(currentThread, methodType);
		j9object_t clazz = NULL;
		J9Class *methodRamClass = J9_CLASS_FROM_METHOD(method);
		J9ClassLoader *targetClassloader = methodRamClass->classLoader;
		J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
		J9UTF8 *targetSig = J9ROMMETHOD_SIGNATURE(romMethod);
		U_32 numParameters = J9INDEXABLEOBJECT_SIZE(currentThread, argsArray);
		U_32 i = 0;
		U_32 index = 0;
		U_32 endIndex = 0;
		U_32 start = 0;

		/* For virtual methods we need to skip the first parameter in the MethodType parameter array */
		if (0 == (romMethod->modifiers & J9AccStatic)) {
			J9UTF8 *targetName = J9ROMMETHOD_NAME(romMethod);
			if ('<' != J9UTF8_DATA(targetName)[0]) {
				/* ensure the method is not a constructor which has a name <init> */
				start = 1;
			}
		}

		/* checkClassLoadingConstraintForNameFunction requires this mutex to be locked */
		omrthread_monitor_enter(vm->classTableMutex);
		for (i = start; i < numParameters; i++) {
			J9Class *argumentRamClass = NULL;

			/* Advance the text pointer to the next argument */
			index += 1;

			/* If this entry is an array, index forward */
			while ('[' == lookupSigData[index]) {
				index += 1;
			}

			endIndex = index;

			/* If this entry is a class type, we need to do a classloader check on it */
			if ('L' == lookupSigData[index]) {
				index += 1;

				clazz = J9JAVAARRAYOFOBJECT_LOAD(currentThread, argsArray, i);
				argumentRamClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, clazz);

	 			while (';' != lookupSigData[endIndex]) {
					endIndex += 1;
				}

				/* Check if we really need to check this classloader constraint */
				if (argumentRamClass->classLoader != targetClassloader) {
					if(verifyData->checkClassLoadingConstraintForNameFunction(currentThread, targetClassloader, argumentRamClass->classLoader, &J9UTF8_DATA(targetSig)[index], &lookupSigData[index], endIndex - index, TRUE) != 0) {
						result = FALSE;
						goto releaseMutexAndReturn;
					}
				}

				index = endIndex;
			}
		}

		/* Skip past the ) at the end of the signature, plus 1 to get to the next entry */
		index += 2;

		/* Check the return type.  If it's an array, get to the actual type */
		while ('[' == lookupSigData[index]) {
			index += 1;
		}
		if('L' == lookupSigData[index]) {
			J9Class *returnRamClass = NULL;
			/* Grab the MethodType returnType */
			clazz = J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(currentThread, methodType);
			returnRamClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, clazz);

			/* Check if we really need to check this classloader constraint */
			if (targetClassloader != returnRamClass->classLoader) {
				endIndex = index;
				index++;

				while (';' != lookupSigData[endIndex]) {
					endIndex++;
				}

				if(verifyData->checkClassLoadingConstraintForNameFunction(currentThread, targetClassloader, returnRamClass->classLoader, &J9UTF8_DATA(targetSig)[index], &lookupSigData[index], endIndex - index, TRUE) != 0) {
					result = FALSE;
					goto releaseMutexAndReturn;
				}
			}
		}

releaseMutexAndReturn:
		omrthread_monitor_exit(vm->classTableMutex);
	}

	return result;
}

/* Should already have VMAccess */
UDATA
lookupField(JNIEnv *env, jboolean isStatic, J9Class *j9LookupClass, jstring name, J9UTF8 *sigUTF, J9Class **definingClass, UDATA *romField, jclass accessClass)
{
	J9UTF8 *nameUTF8 = NULL;
	char nameUTF8Buffer[256];
	J9Class *j9AccessClass = NULL;	/* J9Class for java.lang.Class accessClass */
	UDATA field = 0;
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	PORT_ACCESS_FROM_VMC(vmThread);

	nameUTF8 = vmFuncs->copyStringToJ9UTF8WithMemAlloc(vmThread, J9_JNI_UNWRAP_REFERENCE(name), J9_STR_NONE, "", 0, nameUTF8Buffer, sizeof(nameUTF8Buffer));

	if (NULL == nameUTF8) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		Assert_JCL_notNull(vmThread->currentException);
		goto _cleanup;
	}

	if (NULL != accessClass) {
		j9AccessClass = J9VM_J9CLASS_FROM_JCLASS(vmThread, accessClass);
	}

	if (JNI_TRUE == isStatic) {
		field = (UDATA) vmFuncs->staticFieldAddress(vmThread, j9LookupClass, J9UTF8_DATA(nameUTF8), J9UTF8_LENGTH(nameUTF8), J9UTF8_DATA(sigUTF), J9UTF8_LENGTH(sigUTF), definingClass, romField, 0, j9AccessClass);
		if (0 == field) {
			/* IllegalAccessError / IncompatibleClassChangeError / NoSuchFieldError will be pending */
			Assert_JCL_notNull(vmThread->currentException);
			goto _cleanup;
		}
		/* Turn field into an offset that is usable by Unsafe to support
		 * the JIT implementation of static Field handles
		 */
		field = (UDATA) field - (UDATA) (*definingClass)->ramStatics;
		field += J9_SUN_STATIC_FIELD_OFFSET_TAG;
	} else {
		field = (UDATA) vmFuncs->instanceFieldOffset(vmThread, j9LookupClass, J9UTF8_DATA(nameUTF8), J9UTF8_LENGTH(nameUTF8), J9UTF8_DATA(sigUTF), J9UTF8_LENGTH(sigUTF), definingClass, romField, 0);
		if (-1 == field) {
			/* IllegalAccessError / IncompatibleClassChangeError / NoSuchFieldError will be pending */
			Assert_JCL_notNull(vmThread->currentException);
			goto _cleanup;
		}
	}

	/* definingClass & romField cannot be null here.  The field lookup would have failed if they were null and
	 * would have done a goto _cleanup.
	 */
	Assert_JCL_notNull(*definingClass);
	Assert_JCL_notNull((J9ROMFieldShape *)(*romField));

_cleanup:

	if (nameUTF8 != (J9UTF8*)nameUTF8Buffer) {
		j9mem_free_memory(nameUTF8);
	}

	return field;
}

jclass JNICALL
Java_java_lang_invoke_PrimitiveHandle_lookupField(JNIEnv *env, jobject handle, jclass lookupClass, jstring name, jstring signature, jboolean isStatic, jclass accessClass)
{
	J9UTF8 *signatureUTF8 = NULL;
	char signatureUTF8Buffer[256];
	J9Class *j9LookupClass = NULL;			/* J9Class for java.lang.Class lookupClass */
	J9Class *definingClass = NULL;	/* Returned by calls to find field */
	UDATA field = 0;
	jclass result = NULL;
	UDATA romField = 0;
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	PORT_ACCESS_FROM_ENV(env);

	vmFuncs->internalEnterVMFromJNI(vmThread);

	signatureUTF8 = vmFuncs->copyStringToJ9UTF8WithMemAlloc(vmThread, J9_JNI_UNWRAP_REFERENCE(signature), J9_STR_NONE, "", 0, signatureUTF8Buffer, sizeof(signatureUTF8Buffer));

	if (signatureUTF8 == NULL) {
		vmFuncs->setNativeOutOfMemoryError(vmThread, 0, 0);
		Assert_JCL_notNull(vmThread->currentException);
		goto _cleanup;
	}

	j9LookupClass = J9VM_J9CLASS_FROM_JCLASS(vmThread, lookupClass);

	field = lookupField(env, isStatic, j9LookupClass, name, signatureUTF8, &definingClass, &romField, accessClass);

	if (NULL != vmThread->currentException) {
		goto _cleanup;
	}

	/* Check signature for classloader visibility */
	if (!accessCheckFieldSignature(vmThread, j9LookupClass, romField, J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(vmThread, J9_JNI_UNWRAP_REFERENCE(handle)), signatureUTF8)) {
		setClassLoadingConstraintLinkageError(vmThread, definingClass, signatureUTF8);
		goto _cleanup;
	}
	if (NULL != accessClass) {
		J9Class *j9AccessClass = J9VM_J9CLASS_FROM_JCLASS(vmThread, accessClass);
		if ((j9AccessClass->classLoader != j9LookupClass->classLoader)
			&& !accessCheckFieldSignature(vmThread, j9AccessClass, romField, J9VMJAVALANGINVOKEMETHODHANDLE_TYPE(vmThread, J9_JNI_UNWRAP_REFERENCE(handle)), signatureUTF8)
		) {
			setClassLoadingConstraintLinkageError(vmThread, j9AccessClass, signatureUTF8);
			goto _cleanup;
		}
	}

	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), field);
	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), ((J9ROMFieldShape*) romField)->modifiers);
	result = vmFuncs->j9jni_createLocalRef(env, J9VM_J9CLASS_TO_HEAPCLASS(definingClass));

_cleanup:
	vmFuncs->internalExitVMToJNI(vmThread);

	if (signatureUTF8 != (J9UTF8*)signatureUTF8Buffer) {
		j9mem_free_memory(signatureUTF8);
	}

	return result;
}


jboolean JNICALL
Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromField(JNIEnv *env, jclass clazz, jobject handle, jobject reflectField)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ReflectFunctionTable *reflectFunctions = &(vm->reflectFunctions);
	J9JNIFieldID *fieldID;
	jboolean result = JNI_FALSE;
	j9object_t fieldObject;
	UDATA fieldOffset;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	/* Can't fail as we know the field is not null */
	fieldObject = J9_JNI_UNWRAP_REFERENCE(reflectField);
	fieldID = reflectFunctions->idFromFieldObject(vmThread, NULL, fieldObject);

	fieldOffset = fieldID->offset;
	if (J9AccStatic == (fieldID->field->modifiers & J9AccStatic)) {
		/* ensure this is correctly tagged so that the JIT targets using Unsafe will correctly detect this is static */
		fieldOffset |= J9_SUN_STATIC_FIELD_OFFSET_TAG;
	}


	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), fieldOffset);
	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), fieldID->field->modifiers);
	result = JNI_TRUE;

	vmFuncs->internalExitVMToJNI(vmThread);
	return result;
}


jboolean JNICALL
Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromMethod(JNIEnv *env, jclass clazz, jobject handle, jclass unused, jobject method, jbyte kind, jclass specialToken)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ReflectFunctionTable *reflectFunctions = &(vm->reflectFunctions);
	J9JNIMethodID *methodID;
	J9Class *j9SpecialToken = NULL;
	jboolean result = JNI_FALSE;
	UDATA vmSlotValue;
	j9object_t methodObject;
	U_32 modifiers;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	if (specialToken != NULL) {
		j9SpecialToken = J9VM_J9CLASS_FROM_JCLASS(vmThread, specialToken);
	}
	/* Can't fail as we know the method is not null */
	methodObject = J9_JNI_UNWRAP_REFERENCE(method);
	methodID = reflectFunctions->idFromMethodObject(vmThread, methodObject);
	modifiers = J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method)->modifiers;

	switch(kind){
		case J9_METHOD_HANDLE_KIND_STATIC:
			vmSlotValue = (UDATA) methodID->method;
			break;
		case J9_METHOD_HANDLE_KIND_VIRTUAL:
			vmSlotValue = methodID->vTableIndex;
			break;
		case J9_METHOD_HANDLE_KIND_INTERFACE:
			Assert_JCL_true(J9_ARE_ANY_BITS_SET(methodID->vTableIndex, J9_JNI_MID_INTERFACE));
			vmSlotValue = methodID->vTableIndex & ~J9_JNI_MID_INTERFACE;
			break;
		case J9_METHOD_HANDLE_KIND_SPECIAL:
			if (specialToken == NULL) {
				goto _cleanup;
			}
			if ((J9AccMethodVTable != (modifiers & J9AccMethodVTable))) {
				/* Handle methods which aren't in the VTable, such as private methods and Object.getClass */
				vmSlotValue = (UDATA) methodID->method;
			} else {
				vmSlotValue = (UDATA)getMethodForSpecialSend(vmThread, j9SpecialToken, J9_CLASS_FROM_METHOD(methodID->method), methodID->method, J9_LOOK_VIRTUAL | J9_LOOK_NO_VISIBILITY_CHECK | J9_LOOK_IGNORE_INCOMPATIBLE_METHODS);
				Assert_JCL_true(0 != vmSlotValue);
			}
			break;
		default:
			/* Assert here - illegal should never be called with something that doesn't directly map to kind */
			Assert_JCL_unreachable();
			goto _cleanup;
	}

	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), vmSlotValue);
	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), modifiers);

	result = JNI_TRUE;

_cleanup:
	vmFuncs->internalExitVMToJNI(vmThread);

	return result;
}

jboolean JNICALL
Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromConstructor(JNIEnv *env, jclass clazz, jobject handle, jobject ctor)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ReflectFunctionTable *reflectFunctions = &(vm->reflectFunctions);
	J9JNIMethodID *methodID;
	jboolean result = JNI_FALSE;
	j9object_t methodObject;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	/* Can't fail as we know the method is not null */
	methodObject = J9_JNI_UNWRAP_REFERENCE(ctor);
	methodID = reflectFunctions->idFromConstructorObject(vmThread, methodObject);

	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), (UDATA)methodID->method);
	J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method)->modifiers);

	result = JNI_TRUE;

	vmFuncs->internalExitVMToJNI(vmThread);
	return result;
}

/**
 * Assign the vmSlot for a VirtualHandle when given a DirectHandle that is of KIND_SPECIAL.
 * This method does no access checking as the access checks should have occurred when creating
 * the SpecialHandle.
 */
jboolean JNICALL
Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromSpecialHandle(JNIEnv *env, jclass clazz, jobject handle, jobject specialHandle)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jboolean result = JNI_FALSE;
	UDATA vtableOffset;
	j9object_t specialHandleObject;
	J9Method *method;
	j9object_t classObject;
	J9Class *j9class;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	specialHandleObject = J9_JNI_UNWRAP_REFERENCE(specialHandle);

	method = (J9Method *)(UDATA) J9VMJAVALANGINVOKEPRIMITIVEHANDLE_VMSLOT(vmThread, specialHandleObject);
	classObject = (j9object_t) J9VMJAVALANGINVOKEPRIMITIVEHANDLE_REFERENCECLASS(vmThread, specialHandleObject);
	if ((method == NULL) || (classObject == NULL)) {
		goto _done;
	}
	j9class = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, classObject);
	if (j9class == NULL) {
		goto _done;
	}
	vtableOffset = vmFuncs->getVTableOffsetForMethod(method, j9class, vmThread);
	if (vtableOffset != 0) {
		J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), vtableOffset);
		J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_RAWMODIFIERS(vmThread, J9_JNI_UNWRAP_REFERENCE(handle), J9_ROM_METHOD_FROM_RAM_METHOD(method)->modifiers);
		result = JNI_TRUE;
	}

_done:
	vmFuncs->internalExitVMToJNI(vmThread);
	return result;
}

/* Return the field offset of the 'vmRef' field in j.l.Class.
 * Needed by the JIT in MethodHandle's static initializer before Unsafe
 * can be loaded.  The 'ignored' parameter is to work around a javac bug.
 */
jint JNICALL
Java_java_lang_invoke_MethodHandle_vmRefFieldOffset(JNIEnv *env, jclass clazz, jclass ignored)
{
	return (jint) J9VMJAVALANGCLASS_VMREF_OFFSET(((J9VMThread *) env));
}

jobject JNICALL
Java_java_lang_invoke_MethodHandle_invokeExact(JNIEnv *env, jclass ignored, jobject handle, jobject args)
{
	throwNewUnsupportedOperationException(env);
	return NULL;
}

jobject JNICALL
Java_java_lang_invoke_MethodHandle_invoke(JNIEnv *env, jclass ignored, jobject handle, jobject args)
{
	throwNewUnsupportedOperationException(env);
	return NULL;
}

void JNICALL
Java_java_lang_invoke_MutableCallSite_freeGlobalRef(JNIEnv *env, jclass mutableCallSite, jlong bypassOffset)
{
	if (0 != bypassOffset) {
		J9VMThread *vmThread = (J9VMThread *) env;
		J9JavaVM *vm = vmThread->javaVM;
		J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
		jobject globalRef = NULL;

		vmFuncs->internalEnterVMFromJNI(vmThread);
		globalRef = (jobject)((UDATA)J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(mutableCallSite))->ramStatics + ((UDATA)bypassOffset & ~(UDATA)J9_SUN_FIELD_OFFSET_MASK));
		vmFuncs->j9jni_deleteGlobalRef(env, globalRef, FALSE);
		vmFuncs->internalExitVMToJNI(vmThread);
	}
	return;
}

void JNICALL
Java_java_lang_invoke_ThunkTuple_registerNatives(JNIEnv *env, jclass nativeClass)
{
	JNINativeMethod natives[] = {
			{
					"initialInvokeExactThunk",
					"()J",
					(void *)&vmInitialInvokeExactThunk
			}
	};
	JNINativeMethod finalizeNative[] = {
			{
					"finalizeImpl",
					"(J)V",
					(void *)&vmFinalizeImpl
			}
	};

	registerVMNativesIfNoJIT(env, nativeClass, natives, 1);

	/* Always register this native - JIT may provide an implementation later.
	 * This is just a hook that will allow them to override the native if
	 * the code cache leaking is an issue.
	 */
	(*env)->RegisterNatives(env, nativeClass, finalizeNative, 1);
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
	/* Jazz 99339: Given that the non-zAAP eligible bit is set in RegisterNatives(),
	 * we need to clear the bit here for JCL natives to allow them to run on zAAP.
	 */
	clearNonZAAPEligibleBit(env, nativeClass, finalizeNative, 1);
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */

	return;
}


void JNICALL
Java_java_lang_invoke_MutableCallSite_registerNatives(JNIEnv *env, jclass nativeClass)
{
	JNINativeMethod natives[] = {
			{
					"invalidate",
					"([J)V",
					(void *)&vmInvalidate
			}
	};
	registerVMNativesIfNoJIT(env, nativeClass, natives, 1);
	return;
}


void JNICALL
Java_java_lang_invoke_InterfaceHandle_registerNatives(JNIEnv *env, jclass nativeClass)
{
	JNINativeMethod natives[] = {
			{
					"convertITableIndexToVTableIndex",
					"(JIJ)I",
					(void *)&vmConvertITableIndexToVTableIndex
			}
	};
	registerVMNativesIfNoJIT(env, nativeClass, natives, 1);
	return;
}

/*
 * Helper function to register vm versions of native methods if the jitconfig is null.
 */
static void
registerVMNativesIfNoJIT(JNIEnv *env, jclass nativeClass, JNINativeMethod *natives, jint numNatives)
{
	J9VMThread *vmThread = (J9VMThread *) env;

	if (NULL == vmThread->javaVM->jitConfig) {
		(*env)->RegisterNatives(env, nativeClass, natives, numNatives);
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
		/* Jazz 99339: Given that the non-zAAP eligible bit is set in RegisterNatives(),
		 * we need to clear the bit here for JCL natives to allow them to run on zAAP.
		 */
		clearNonZAAPEligibleBit(env, nativeClass, natives, numNatives);
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */
	}

	return;
}

static void JNICALL
vmInvalidate(JNIEnv *env, jclass mutableCallSiteClass, jlongArray cookies)
{
	/* No op - this native is supplied by the JIT dll when the jit is enabled.
	 * This is just a dummy native to prevent LinkageErrors when running Xint.
	 */
	return;
}

static jlong JNICALL
vmInitialInvokeExactThunk(JNIEnv *env, jclass ignored)
{
	return 0;
}

static jlong JNICALL
vmConvertITableIndexToVTableIndex(JNIEnv *env, jclass InterfaceMethodHandle, jlong interfaceArg, jint itableIndex, jlong receiverClassArg)
{
	return 0;
}

static void JNICALL
vmFinalizeImpl(JNIEnv *env, jclass methodHandleClass, jlong thunkAddress)
{
	return;
}

/**
 * Fill in a parameterized NLS message.
 *
 * Does not require VM access.
 *
 * @param[in] vmThread The current VM thread.
 * @param[in] nlsTemplate A parameterized NLS message, as returned from j9nls_lookup_message().
 * @return An allocated buffer containing the expanded NLS message. Caller must free this
 * buffer using j9mem_free_memory().
 */
static char *
expandNLSTemplate(J9VMThread *vmThread, const char *nlsTemplate, ...)
{
	PORT_ACCESS_FROM_VMC(vmThread);
	char *msg = NULL;

	if (NULL != nlsTemplate) {
		UDATA msgLen = 0;
		va_list args;

		va_start(args, nlsTemplate);
		msgLen = j9str_vprintf(NULL, 0, nlsTemplate, args);
		va_end(args);

		msg = (char *)j9mem_allocate_memory(msgLen, OMRMEM_CATEGORY_VM);
		if (NULL != msg) {
			va_start(args, nlsTemplate);
			j9str_vprintf(msg, msgLen, nlsTemplate, args);
			va_end(args);
		}
	}
	return msg;
}

/**
 * Set a LinkageError to indicate a class loading constraint violation between
 * the MT and the looked up method or field.
 *
 * @param[in] vmThread The current J9VMThread *
 * @param[in] methodOrFieldClass The defining class of the looked up field or method
 * @param[in] signatureUTF8 The signature of the MethodType
 */
void
setClassLoadingConstraintLinkageError(J9VMThread *vmThread, J9Class *methodOrFieldClass, J9UTF8 *signatureUTF8)
{
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	PORT_ACCESS_FROM_VMC(vmThread);
	J9UTF8 *className = J9ROMCLASS_CLASSNAME(methodOrFieldClass->romClass);
	const char *nlsTemplate = j9nls_lookup_message(
			J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE,
			J9NLS_JCL_METHODTYPE_CLASS_LOADING_CONSTRAINT,
			"loading constraint violation: %.*s not visible from %.*s");
	char * msg = expandNLSTemplate(
			vmThread,
			nlsTemplate,
			J9UTF8_LENGTH(signatureUTF8), J9UTF8_DATA(signatureUTF8),
			J9UTF8_LENGTH(className), J9UTF8_DATA(className));
	vmFuncs->setCurrentExceptionUTF(vmThread, J9VMCONSTANTPOOL_JAVALANGLINKAGEERROR, msg);
	j9mem_free_memory(msg);
}

#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
/**
 * Clear the non-ZAAP eligible bit for JCL natives to allow them to run on zAAP.
 *
 * @param[in] env                 The JNI interface pointer
 * @param[in] nativeClass         The class containing the native method
 * @param[in] nativeMethods       The JNI native method pointer
 * @param[in] nativeMethodCount  The count of native methods
 */
void
clearNonZAAPEligibleBit(JNIEnv *env, jclass nativeClass, const JNINativeMethod *nativeMethods, jint nativeMethodCount)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions* vmFuncs = vm->internalVMFunctions;
	const JNINativeMethod *nativeMethod = nativeMethods;
	jint count = nativeMethodCount;
	J9Class *j9clazz = NULL;

	vmFuncs->internalEnterVMFromJNI(vmThread);
	j9clazz = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, J9_JNI_UNWRAP_REFERENCE(nativeClass));

	while (0 < count) {
		J9Method *jniMethod = vmFuncs->findJNIMethod(vmThread, j9clazz, nativeMethod->name, nativeMethod->signature);
		vmFuncs->atomicAndIntoConstantPool(vm, jniMethod, ~(UDATA)J9_STARTPC_NATIVE_REQUIRES_SWITCHING);
		count -= 1;
		nativeMethod +=1;
	}
	vmFuncs->internalExitVMToJNI(vmThread);
}
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */

#if JAVA_SPEC_VERSION >= 15
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_checkClassBytes(JNIEnv *env, jclass jlClass, jbyteArray classRep)
{
	J9VMThread *currentThread = (J9VMThread *)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jsize length = 0;
	U_8* classBytes = NULL;
	I_32 rc = 0;
	U_8* segment = NULL;
	U_32 segmentLength = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);
	
	if (NULL == classRep) {
		throwNewNullPointerException(env, NULL);
		goto done;
	}
	length = (*env)->GetArrayLength(env, classRep);
	if (0 == length) {
		goto done;
	}

	segmentLength = ESTIMATE_SIZE(length);
	segment = (U_8*)j9mem_allocate_memory(segmentLength, J9MEM_CATEGORY_VM_JCL);
	if (NULL == segment) {
		vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
		goto done;
	}
	memset(segment, 0, segmentLength);
	classBytes = (*env)->GetPrimitiveArrayCritical(env, classRep, NULL);

	rc = vmFuncs->checkClassBytes(currentThread, classBytes, length, segment, segmentLength);
	(*env)->ReleasePrimitiveArrayCritical(env, classRep, classBytes, 0);
	if (0 != rc) {
		J9CfrError *cfrError = (J9CfrError *)segment;
		const char* errorMsg = OMRPORT_FROM_J9PORT(PORTLIB)->nls_lookup_message(OMRPORT_FROM_J9PORT(PORTLIB), J9NLS_DO_NOT_APPEND_NEWLINE, cfrError->errorCatalog, cfrError->errorCode, NULL);
		vmFuncs->internalEnterVMFromJNI(currentThread);
		vmFuncs->setCurrentExceptionUTF(currentThread, cfrError->errorAction, errorMsg);
		vmFuncs->internalExitVMToJNI(currentThread);
	}
	j9mem_free_memory(segment);
done:
	return;
}
#endif /* JAVA_SPEC_VERSION >= 15 */

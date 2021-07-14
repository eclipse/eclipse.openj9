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
#include "jni.h"
#include "j9comp.h"
#include "vmaccess.h"
#include "j9cfg.h"
#include "j9consts.h"
#include "j9protos.h"
#include "rommeth.h"
#include "stackwalk.h"
#include "ut_j9vm.h"
#include "j9vmnls.h"

#include <string.h>
#include <stdlib.h>
#include "j9port.h"
#include "jnicsup.h"
#include "jnicimap.h"
#include "jnifield.h"
#include "jnireflect.h"
#include "j9cp.h"
#include "jvminit.h"
#include "vm_internal.h"
#include "j2sever.h"
#include "util_api.h"
#include "j9accessbarrier.h"

#include "VMHelpers.hpp"
#include "VMAccess.hpp"
#include "ObjectMonitor.hpp"

extern "C" {

typedef union J9GenericJNIID {
	J9JNIFieldID fieldID;
	J9JNIMethodID methodID;
} J9GenericJNIID;

#define METHODID_CLASS_REF(methodID) ((jclass) &(J9_CP_FROM_METHOD(((J9JNIMethodID *) (methodID))->method)->ramClass->classObject))

#define MAX_LOCAL_CAPACITY (64 * 1024)

#define J9JNIID_METHOD  0
#define J9JNIID_FIELD  1
#define J9JNIID_STATIC  2

static void * JNICALL getDirectBufferAddress (JNIEnv *env, jobject buf);
static jmethodID JNICALL getMethodID (JNIEnv *env, jclass clazz, const char *name, const char *signature);
static void JNICALL exceptionClear (JNIEnv *env);
static jobject JNICALL newObject (JNIEnv *env, jclass clazz, jmethodID methodID, ...);
static jfieldID JNICALL getFieldID (JNIEnv *env, jclass clazz, const char *name, const char *sig);
static jobject JNICALL newObjectA (JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args);
static jobject JNICALL newObjectV (JNIEnv *env, jclass clazz, jmethodID methodID, va_list args);
static jthrowable JNICALL exceptionOccurred (JNIEnv *env);
static jint JNICALL pushLocalFrame (JNIEnv *env, jint capacity);
static jint JNICALL ensureLocalCapacityWrapper (JNIEnv *env, jint capacity);

#if (defined(J9VM_INTERP_FLOAT_SUPPORT))
static jfloatArray JNICALL newFloatArray (JNIEnv *env, jsize length);
static jdoubleArray JNICALL newDoubleArray (JNIEnv *env, jsize length);
#endif /* J9VM_INTERP_FLOAT_SUPPORT */

#if  !defined(J9VM_INTERP_MINIMAL_JNI)
static jobject JNICALL gpCheckToReflectedField (JNIEnv * env, jclass clazz, jfieldID fieldID, jboolean isStatic);
static UDATA gpProtectedToReflected (void *entryArg);
static jobject JNICALL gpCheckToReflectedMethod (JNIEnv * env, jclass clazz, jmethodID methodID, jboolean isStatic);
#endif /* !INTERP_MINIMAL_JNI */

static UDATA gpProtectedSetNativeOutOfMemoryError(void * entryArg);
static UDATA gpProtectedSetHeapOutOfMemoryError(void * entryArg);
static UDATA gpProtectedSetCurrentExceptionNLS (void * entryArg);
static UDATA gpProtectedRunCallInMethod (void *entryArg);
static UDATA gpProtectedSetCurrentException (void * entryArg);
static UDATA gpProtectedFindClass (void * entryArg);
static jclass JNICALL gpCheckFindClass (JNIEnv * env, const char *name);
static UDATA gpProtectedInitialize (void * entryArg);
static UDATA jniPushFrame (J9VMThread * vmThread, UDATA type, UDATA capacity);
static jint JNICALL getJavaVM (JNIEnv *env, JavaVM **vm);
static jboolean isDirectBuffer(JNIEnv *env, jobject buf);
static jlong JNICALL getDirectBufferCapacity (JNIEnv *env, jobject buf);
static void JNICALL deleteGlobalRef (JNIEnv *env, jobject globalRef);
static jweak JNICALL newWeakGlobalRef (JNIEnv *env, jobject localOrGlobalRef);
static jbooleanArray JNICALL newBooleanArray (JNIEnv *env, jsize length);
static jobject allocateGlobalRef (JNIEnv *env, jobject localOrGlobalRef, jboolean isWeak);
static void ensurePendingJNIException (JNIEnv* env);
static void deallocateGlobalRef (JNIEnv *env, jobject weakOrStrongGlobalRef, jboolean isWeak);
static jobject JNICALL newLocalRef (JNIEnv *env, jobject object);
static jobject JNICALL newGlobalRef (JNIEnv *env, jobject localOrGlobalRef);
static jobject JNICALL popLocalFrame (JNIEnv *env, jobject result);
static void JNICALL deleteWeakGlobalRef (JNIEnv *env, jweak weakGlobalRef);
static jfieldID JNICALL getStaticFieldID (JNIEnv *env, jclass clazz, const char *name, const char *sig);
static jboolean JNICALL initDirectByteBufferCache (JNIEnv *env);
static void JNICALL setStaticByteField (JNIEnv *env, jclass cls, jfieldID fieldID, jbyte value);
static jshortArray JNICALL newShortArray (JNIEnv *env, jsize length);
static void JNICALL setStaticBooleanField (JNIEnv *env, jclass cls, jfieldID fieldID, jboolean value);
static void JNICALL setStaticShortField (JNIEnv *env, jclass cls, jfieldID fieldID, jshort value);
static jstring JNICALL newString (JNIEnv *env, const jchar* uchars, jsize len);
static jboolean JNICALL exceptionCheck (JNIEnv *env);
static jcharArray JNICALL newCharArray (JNIEnv *env, jsize length);
static jobject JNICALL newDirectByteBuffer (JNIEnv *env, void *address, jlong capacity);
static void JNICALL setStaticCharField (JNIEnv *env, jclass cls, jfieldID fieldID, jchar value);
static jlongArray JNICALL newLongArray (JNIEnv *env, jsize length);
static jmethodID JNICALL getStaticMethodID (JNIEnv *env, jclass clazz, const char *name, const char *signature);
static jintArray JNICALL newIntArray (JNIEnv *env, jsize length);
static jint JNICALL throwNew (JNIEnv *env, jclass clazz, const char *message);
static jbyteArray JNICALL newByteArray (JNIEnv *env, jsize length);
static jint JNICALL monitorEnter(JNIEnv* env, jobject obj);
static jint JNICALL monitorExit(JNIEnv* env, jobject obj);
static jboolean JNICALL isInstanceOf(JNIEnv *env, jobject obj, jclass clazz);
static void* getMethodOrFieldID(JNIEnv *env, jclass classReference, const char *name, const char *signature, UDATA flags);
static jobjectRefType JNICALL getObjectRefType(JNIEnv *env, jobject obj);

static void * JNICALL getPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy);
static void JNICALL releasePrimitiveArrayCritical(JNIEnv *env, jarray array, void * elems, jint mode);
static const jchar * JNICALL getStringCritical(JNIEnv *env, jstring str, jboolean *isCopy);
static void JNICALL releaseStringCritical(JNIEnv *env, jstring str, const jchar * elems);

#if JAVA_SPEC_VERSION >= 9
static jobject JNICALL getModule(JNIEnv *env, jclass clazz);
#endif /* JAVA_SPEC_VERSION >= 9 */

#define FIND_CLASS gpCheckFindClass
#define TO_REFLECTED_METHOD gpCheckToReflectedMethod
#define TO_REFLECTED_FIELD gpCheckToReflectedField
#define SET_CURRENT_EXCEPTION gpCheckSetCurrentException
#define SET_CURRENT_EXCEPTION_NLS gpCheckSetCurrentExceptionNLS
#define SET_NATIVE_OUT_OF_MEMORY_ERROR gpCheckSetNativeOutOfMemoryError

#define GET_BOOLEAN_ARRAY_ELEMENTS ((jboolean * (JNICALL *)(JNIEnv *env, jbooleanArray array, jboolean * isCopy))getArrayElements)
#define GET_BYTE_ARRAY_ELEMENTS ((jbyte * (JNICALL *)(JNIEnv *env, jbyteArray array, jboolean * isCopy))getArrayElements)
#define GET_CHAR_ARRAY_ELEMENTS ((jchar * (JNICALL *)(JNIEnv *env, jcharArray array, jboolean * isCopy))getArrayElements)
#define GET_SHORT_ARRAY_ELEMENTS ((jshort * (JNICALL *)(JNIEnv *env, jshortArray array, jboolean * isCopy))getArrayElements)
#define GET_INT_ARRAY_ELEMENTS ((jint * (JNICALL *)(JNIEnv *env, jintArray array, jboolean * isCopy))getArrayElements)
#define GET_LONG_ARRAY_ELEMENTS ((jlong * (JNICALL *)(JNIEnv *env, jlongArray array, jboolean * isCopy))getArrayElements)
#define GET_FLOAT_ARRAY_ELEMENTS ((jfloat * (JNICALL *)(JNIEnv *env, jfloatArray array, jboolean * isCopy))getArrayElements)
#define GET_DOUBLE_ARRAY_ELEMENTS ((jdouble * (JNICALL *)(JNIEnv *env, jdoubleArray array, jboolean * isCopy))getArrayElements)

#define RELEASE_BOOLEAN_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jbooleanArray array, jboolean * elems, jint mode))releaseArrayElements)
#define RELEASE_BYTE_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jbyteArray array, jbyte * elems, jint mode))releaseArrayElements)
#define RELEASE_CHAR_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jcharArray array, jchar * elems, jint mode))releaseArrayElements)
#define RELEASE_SHORT_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jshortArray array, jshort * elems, jint mode))releaseArrayElements)
#define RELEASE_INT_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jintArray array, jint * elems, jint mode))releaseArrayElements)
#define RELEASE_LONG_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jlongArray array, jlong * elems, jint mode))releaseArrayElements)
#define RELEASE_FLOAT_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jfloatArray array, jfloat * elems, jint mode))releaseArrayElements)
#define RELEASE_DOUBLE_ARRAY_ELEMENTS ((void (JNICALL *)(JNIEnv *env, jdoubleArray array, jdouble * elems, jint mode))releaseArrayElements)

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
#define GET_BOOLEAN_ARRAY_ELEMENTS31 ((jboolean * (JNICALL *)(JNIEnv *env, jbooleanArray array, jboolean * isCopy))getArrayElements31)
#define GET_BYTE_ARRAY_ELEMENTS31 ((jbyte * (JNICALL *)(JNIEnv *env, jbyteArray array, jboolean * isCopy))getArrayElements31)
#define GET_CHAR_ARRAY_ELEMENTS31 ((jchar * (JNICALL *)(JNIEnv *env, jcharArray array, jboolean * isCopy))getArrayElements31)
#define GET_SHORT_ARRAY_ELEMENTS31 ((jshort * (JNICALL *)(JNIEnv *env, jshortArray array, jboolean * isCopy))getArrayElements31)
#define GET_INT_ARRAY_ELEMENTS31 ((jint * (JNICALL *)(JNIEnv *env, jintArray array, jboolean * isCopy))getArrayElements31)
#define GET_LONG_ARRAY_ELEMENTS31 ((jlong * (JNICALL *)(JNIEnv *env, jlongArray array, jboolean * isCopy))getArrayElements31)
#define GET_FLOAT_ARRAY_ELEMENTS31 ((jfloat * (JNICALL *)(JNIEnv *env, jfloatArray array, jboolean * isCopy))getArrayElements31)
#define GET_DOUBLE_ARRAY_ELEMENTS31 ((jdouble * (JNICALL *)(JNIEnv *env, jdoubleArray array, jboolean * isCopy))getArrayElements31)
#define GET_PRIMITIVE_ARRAY_CRITICAL31 ((void * (JNICALL *)(JNIEnv *env, jarray array, jboolean * isCopy))getArrayElements31)

#define RELEASE_BOOLEAN_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jbooleanArray array, jboolean * elems, jint mode))releaseArrayElements31)
#define RELEASE_BYTE_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jbyteArray array, jbyte * elems, jint mode))releaseArrayElements31)
#define RELEASE_CHAR_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jcharArray array, jchar * elems, jint mode))releaseArrayElements31)
#define RELEASE_SHORT_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jshortArray array, jshort * elems, jint mode))releaseArrayElements31)
#define RELEASE_INT_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jintArray array, jint * elems, jint mode))releaseArrayElements31)
#define RELEASE_LONG_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jlongArray array, jlong * elems, jint mode))releaseArrayElements31)
#define RELEASE_FLOAT_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jfloatArray array, jfloat * elems, jint mode))releaseArrayElements31)
#define RELEASE_DOUBLE_ARRAY_ELEMENTS31 ((void (JNICALL *)(JNIEnv *env, jdoubleArray array, jdouble * elems, jint mode))releaseArrayElements31)
#define RELEASE_PRIMITIVE_ARRAY_CRITICAL31 ((void (JNICALL *)(JNIEnv *env, jarray carray, void * elems, jint mode))releaseArrayElements31)

#define GET_STRING_CRITICAL31 ((const jchar * (JNICALL *)(JNIEnv *env, jstring string, jboolean *isCopy))getStringChars31)
#define RELEASE_STRING_CRITICAL31 ((void (JNICALL *)(JNIEnv *env, jstring string, const jchar *carray))releaseStringChars31)
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

#define GET_BOOLEAN_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jbooleanArray array, jsize start, jsize len, jboolean *buf))getArrayRegion)
#define GET_BYTE_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf))getArrayRegion)
#define GET_CHAR_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf))getArrayRegion)
#define GET_SHORT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf))getArrayRegion)
#define GET_INT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf))getArrayRegion)
#define GET_LONG_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf))getArrayRegion)
#define GET_FLOAT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf))getArrayRegion)
#define GET_DOUBLE_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf))getArrayRegion)

#define SET_BOOLEAN_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jbooleanArray array, jsize start, jsize len, jboolean *buf))setArrayRegion)
#define SET_BYTE_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf))setArrayRegion)
#define SET_CHAR_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf))setArrayRegion)
#define SET_SHORT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf))setArrayRegion)
#define SET_INT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf))setArrayRegion)
#define SET_LONG_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf))setArrayRegion)
#define SET_FLOAT_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf))setArrayRegion)
#define SET_DOUBLE_ARRAY_REGION ((void (JNICALL *)(JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf))setArrayRegion)

static jobject JNICALL newObject(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	jobject obj;

	obj = allocObject(env, clazz);
	if (obj) {
		va_list args;

		va_start(args, methodID);
		CALL_NONVIRTUAL_VOID_METHOD_V(env, obj, METHODID_CLASS_REF(methodID), methodID, args);
		va_end(args);
		if (exceptionCheck(env)) {
			deleteLocalRef(env, obj);
			obj = (jobject) NULL;
		}
	}
	return obj;
}



static jobject JNICALL newObjectA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	jobject obj;

	obj = allocObject(env, clazz);
	if (obj) {
		CALL_NONVIRTUAL_VOID_METHOD_A(env, obj, METHODID_CLASS_REF(methodID), methodID, args);
		if (exceptionCheck(env)) {
			deleteLocalRef(env, obj);
			obj = (jobject) NULL;
		}
	}
	return obj;
}



static jobject JNICALL newObjectV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jobject obj;

	obj = allocObject(env, clazz);
	if (obj) {
		CALL_NONVIRTUAL_VOID_METHOD_V(env, obj, METHODID_CLASS_REF(methodID), methodID, args);
		if (exceptionCheck(env)) {
			deleteLocalRef(env, obj);
			obj = (jobject) NULL;
		}
	}
	return obj;
}

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
jobject JNICALL newObjectV31(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jobject obj;

	obj = allocObject(env, clazz);
	if (obj) {
		CALL_NONVIRTUAL_VOID_METHOD_V31(env, obj, METHODID_CLASS_REF(methodID), methodID, args);
		if (exceptionCheck(env)) {
			deleteLocalRef(env, obj);
			obj = (jobject) NULL;
		}
	}
	return obj;
}
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

void JNICALL OMRNORETURN fatalError(JNIEnv *env, const char *msg)
{
	PORT_ACCESS_FROM_VMC( ((J9VMThread *) env) );

	j9tty_printf( PORTLIB,  "\nFatal error: %s\n", msg);
	exitJavaVM((J9VMThread*) env, 1111);

dontreturn: goto dontreturn; /* avoid warnings */
}



static UDATA
gpProtectedRunCallInMethod(void *entryArg)
{
	J9RedirectedCallInArgs *args = (J9RedirectedCallInArgs *) entryArg;
	J9VMThread *vmThread = (J9VMThread*)args->env;

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);
	runCallInMethod(args->env, args->receiver, args->clazz, args->methodID, args->args);
	VM_VMAccess::inlineExitVMToJNI(vmThread);

	return 0;					/* return value required to clone from to port library definition */
}



static UDATA gpProtectedFindClass(void * entryArg)
{
	J9RedirectedFindClassArgs * args = (J9RedirectedFindClassArgs *) entryArg;
	return (UDATA) findClass (args->env, args->name);
}



#if  !defined(J9VM_INTERP_MINIMAL_JNI)
static UDATA gpProtectedToReflected(void *entryArg)
{
	J9RedirectedToReflectedArgs * args = (J9RedirectedToReflectedArgs *) entryArg;
	return (UDATA) (args->func) (args->env, args->clazz, args->id, args->isStatic);
}

#endif /* !INTERP_MINIMAL_JNI */


static jclass JNICALL gpCheckFindClass(JNIEnv * env, const char *name)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		return findClass(env, name);
	} else {
		J9RedirectedFindClassArgs args;
		args.env = env;
		args.name = name;
		return (jclass) gpProtectAndRun(gpProtectedFindClass, env, &args);
	}
}



#if  !defined(J9VM_INTERP_MINIMAL_JNI)
static jobject JNICALL gpCheckToReflectedField(JNIEnv * env, jclass clazz, jfieldID fieldID, jboolean isStatic)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		return (jobject) toReflectedField(env, clazz, fieldID, isStatic);
	} else {
		J9RedirectedToReflectedArgs args;
		args.func = (void *(JNICALL *) (JNIEnv *, jclass, void *, jboolean)) toReflectedField;
		args.env = env;
		args.clazz = clazz;
		args.id = fieldID;
		args.isStatic = isStatic;
		return (jobject) gpProtectAndRun(gpProtectedToReflected, env, &args);
	}
}

#endif /* !INTERP_MINIMAL_JNI */


#if  !defined(J9VM_INTERP_MINIMAL_JNI)
static jobject JNICALL gpCheckToReflectedMethod(JNIEnv * env, jclass clazz, jmethodID methodID, jboolean isStatic)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		return (jobject) toReflectedMethod(env, clazz, methodID, isStatic);
	} else {
		J9RedirectedToReflectedArgs args;
		args.func = (void *(JNICALL *) (JNIEnv *, jclass, void *, jboolean)) toReflectedMethod;
		args.env = env;
		args.clazz = clazz;
		args.id = methodID;
		args.isStatic = isStatic;
		return (jobject) gpProtectAndRun(gpProtectedToReflected, env, &args);
	}
}

#endif /* !INTERP_MINIMAL_JNI */


static jint
JNICALL throwNew(JNIEnv *env, jclass clazz, const char *message)
{
	jmethodID constructor;
	jobject throwable;
	
	if (message) {
		jstring messageObject;
		constructor = getMethodID(env, clazz, "<init>", "(Ljava/lang/String;)V");
		if (constructor == NULL) {
			return -1;
		}

		messageObject = newStringUTF(env, message);
		if (messageObject == NULL) {
			return -1;
		}

		throwable = newObject(env, clazz, constructor, messageObject);
		deleteLocalRef(env, messageObject);
	} else {
		constructor = getMethodID(env, clazz, "<init>", "()V");
		if (constructor == NULL) {
			return -1;
		}

		throwable = newObject(env, clazz, constructor);
	}

	if (throwable == NULL) {
		return -1;
	}

	jniThrow(env, (jthrowable)throwable);

	return 0;
}



static void JNICALL setStaticBooleanField(JNIEnv *env, jclass cls, jfieldID fieldID, jboolean value)
{
	setStaticIntField(env, cls, fieldID, (jint)(value & 1));
}



static void JNICALL setStaticByteField(JNIEnv *env, jclass cls, jfieldID fieldID, jbyte value)
{
	setStaticIntField(env, cls, fieldID, (jint)value);
}



static void JNICALL setStaticCharField(JNIEnv *env, jclass cls, jfieldID fieldID, jchar value)
{
	setStaticIntField(env, cls, fieldID, (jint)value);
}



static void JNICALL setStaticShortField(JNIEnv *env, jclass cls, jfieldID fieldID, jshort value)
{
	setStaticIntField(env, cls, fieldID, (jint)value);
}



static UDATA gpProtectedInitialize(void * entryArg)
{
	J9RedirectedInitializeArgs * args = (J9RedirectedInitializeArgs *) entryArg;
	initializeClass(args->env, args->clazz);
	return 0;
}



void JNICALL gpCheckInitialize(J9VMThread* env, J9Class* clazz)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		initializeClass(env, clazz);
	} else {
		J9RedirectedInitializeArgs args;
		args.env = env;
		args.clazz = clazz;
		gpProtectAndRun(gpProtectedInitialize, (JNIEnv*)env, &args);
	}
}



void   
gpCheckCallin(JNIEnv *env, jobject receiver, jclass cls, jmethodID methodID, void* args)
{
	J9RedirectedCallInArgs handlerArgs;

	handlerArgs.env = env;
	handlerArgs.receiver = receiver;
	handlerArgs.clazz = cls;
	handlerArgs.methodID = methodID;
	handlerArgs.args = args;

	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		gpProtectedRunCallInMethod(&handlerArgs);
	} else {
		gpProtectAndRun(gpProtectedRunCallInMethod, env, &handlerArgs);
	}
}


UDATA JNICALL pushArguments(J9VMThread *vmThread, J9Method* method, void *args) {
	jvalue* jvalues;
	U_8 *sigChar;
	jobject objArg;
	UDATA* sp;

	if ( (UDATA)args & 1 ) {
		/* pointer is tagged.  This means it's an array of jvalues */
		jvalues = (jvalue*)((U_8*)args - 1);
	} else {
		jvalues = NULL;
	}

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
/* 31-bit vargs support being processed from 64-bit JVM.
 * The alignment is set to be 4-bytes aligned.  Also __wsizeof() expands to 8 bytes on 64-bit, has been replaced with sizeof().
 * The alignment check setting vl[0] will naturally take care of expansion.
 */
#define va_arg_31(vl, type) (*(type *)( (vl)[0] = (char *) (((unsigned long)((vl)[1])+3)&~(3)), (vl)[1] = (vl)[0] + (sizeof ( (* (type (*)(void)) 0 )() ) ), (sizeof(type) < sizeof(U_32)) ? ((vl)[0] + ((sizeof ( (* (type (*)(void)) 0 )() ) ) - sizeof(type))): (vl)[0] ))

// Update ARG macro to also check if 31bit vargs, then call the va_arg_31 macro defined above if so.
#define ARG(type, sig) (jvalues ? (jvalues++->sig) : ((isCrossAmodeVarArgs)?va_arg_31(*(va_list*)args, type):va_arg(*(va_list*)args, type)))

	BOOLEAN isCrossAmodeVarArgs = FALSE;

	if ( J9_ARE_ANY_BITS_SET((UDATA)args, 0x2) ) {
		/* pointer being tagged with 0x2 implies it's a 31-bit cross-amode var_args */
		isCrossAmodeVarArgs = TRUE;
		args = (void *) ((U_8*)args - 2);
	}
#else /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */
#define ARG(type, sig) (jvalues ? (jvalues++->sig) : va_arg(*(va_list*)args, type))
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

	/* process the arguments */
	sigChar = &J9UTF8_DATA(J9ROMMETHOD_SIGNATURE(J9_ROM_METHOD_FROM_RAM_METHOD(method)))[1];	/* skip the opening '(' */

	sp = vmThread->sp;

	for (;;) {
		BOOLEAN skipSignature = TRUE;
		switch (*sigChar++) {
			case '[':
				/* skip the rest of the signature */
				while ('[' == *sigChar) {
					sigChar += 1;
				}
				skipSignature = ('L' == *sigChar++);
			case 'L': /* FALLTHROUGH */
				/* skip the rest of the signature */
				if (skipSignature) {
					while (';' != *sigChar) {
						sigChar += 1;
					}
				}
				sp -= 1;
				objArg = ARG(jobject, l);
				*sp = (NULL == objArg) ? 0: (UDATA) *((j9object_t*) objArg);
				break;
			case 'B':
				/* byte type */
				*(I_32*)(--sp) = (I_32)(jbyte)ARG(int, b);
				break;
			case 'Z':
				/* boolean type */
				*(I_32*)(--sp) = (I_32)(ARG(int, z) != 0);
				break;
			case 'S':
				/* short type */
				*(I_32*)(--sp) = (I_32)(jshort)ARG(int, s);
				break;
			case 'C':
				/* char type */
				*(U_32*)(--sp) = (U_32)(jchar)ARG(int, c);
				break;
			case 'I':
				/* int type */
				*(I_32*)(--sp) = (I_32)(jint)ARG(jint, i);	/* NOTE: jint may be larger than int, but jchar, jshort, etc, must be no larger than int */
				break;
#ifdef J9VM_INTERP_FLOAT_SUPPORT
			case 'F':
				/* float type */
				sp -= 1;
				/* jfloat is promoted to double when passed through '...' */
				*((jfloat*) sp) = (jfloat) ARG(jdouble, f);
			break;
			case 'D':
				/* double type */
				sp -= 2;
				*((jdouble*) sp) = ARG(jdouble, d);
				break;
#endif
			case 'J':
				/* long type */
				sp -= 2;
				*((jlong*) sp) = ARG(jlong, j);
				break;
			case ')':
				vmThread->sp = sp;
				return (*sigChar == 'L' || *sigChar == '[') ? J9_SSF_RETURNS_OBJECT : 0;
		}
	}
}

#undef ARG



void JNICALL gpCheckSetCurrentException(J9VMThread* env, UDATA exceptionNumber, UDATA* detailMessage)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		setCurrentException(env, exceptionNumber, detailMessage);
	} else {
		J9RedirectedSetCurrentExceptionArgs args;
		args.env = env;
		args.exceptionNumber = exceptionNumber;
		args.detailMessage = detailMessage;
		gpProtectAndRun(gpProtectedSetCurrentException, (JNIEnv*)env, &args);
	}
}



static UDATA gpProtectedSetCurrentException(void * entryArg)
{
	J9RedirectedSetCurrentExceptionArgs * args = (J9RedirectedSetCurrentExceptionArgs *) entryArg;
	setCurrentException(args->env, args->exceptionNumber, args->detailMessage);
	return 0;
}



static jfieldID JNICALL getFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig)
{
	return (jfieldID)getMethodOrFieldID(env, clazz, name, sig, J9JNIID_FIELD);
}


static jfieldID JNICALL getStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig)
{
	return (jfieldID)getMethodOrFieldID(env, clazz, name, sig, J9JNIID_FIELD | J9JNIID_STATIC);
}


static jmethodID JNICALL getStaticMethodID(JNIEnv *env, jclass clazz, const char *name, const char *signature)
{
	return (jmethodID)getMethodOrFieldID(env, clazz, name, signature, J9JNIID_METHOD | J9JNIID_STATIC);
}


static jmethodID JNICALL getMethodID(JNIEnv *env, jclass clazz, const char *name, const char *signature)
{
	return (jmethodID)getMethodOrFieldID(env, clazz, name, signature, J9JNIID_METHOD);
}


static jint JNICALL getJavaVM(JNIEnv *env, JavaVM **vm) {
	*vm = (JavaVM*)((J9VMThread*)env)->javaVM;
	return 0;
}


static jboolean
isDirectBuffer(JNIEnv *env, jobject buf)
{
	if (initDirectByteBufferCache(env) && (buf != NULL) && ((*(j9object_t*)buf) != NULL)) {
		J9JavaVM *javaVM = ((J9VMThread *) env)->javaVM;
		/* Must be instance of java.nio.Buffer and implement sun.nio.ch.DirectBuffer */
		if (env->IsInstanceOf(buf, javaVM->java_nio_Buffer) && env->IsInstanceOf(buf, javaVM->sun_nio_ch_DirectBuffer)) {
			return JNI_TRUE;
		}
	}

	return JNI_FALSE;
}


static jlong JNICALL getDirectBufferCapacity(JNIEnv *env, jobject buf)
{
	if (isDirectBuffer(env, buf)) {
		return env->GetIntField(buf, ((J9VMThread *) env)->javaVM->java_nio_Buffer_capacity);
	}

	return (jlong)-1;
}

static void * JNICALL getDirectBufferAddress(JNIEnv *env, jobject buf)
{
	void* result = NULL;
	Trc_VM_JNI_GetDirectBufferAddress_Entry(env, buf);

	if (isDirectBuffer(env, buf)) {
		result = (void*)(UDATA) (env->GetLongField(buf, ((J9VMThread *) env)->javaVM->java_nio_Buffer_address));
	}

	Trc_VM_JNI_GetDirectBufferAddress_Exit(env, result);
	return result;
}

/**
 * Looks up a class by name and creates a GlobalRef to maintain
 * a reference to it.
 * @param env
 * @param className The name of the class to find.
 * @return A JNI GlobalRef on success, NULL on failure.
 */
static jclass JNICALL
findClassAndCreateGlobalRef(JNIEnv *env, const char* className)
{
	jclass globalRef;

	jclass localRef = env->FindClass(className);
	if (localRef == NULL) {
		return NULL;
	}

	globalRef = (jclass)env->NewGlobalRef(localRef);
	if (globalRef == NULL) {
		return NULL;
	}

	return globalRef;
}


/**
 * Initialize JNI ID's that are specific to the Sun class library.
 * @param env
 * @param java_nio_Buffer globalref of the java_nio_Buffer class, guaranteed non-NULL.
 * @param java_nio_DirectByteBuffer globalref of the java_nio_DirectByteBuffer class, guaranteed non-NULL.
 * @return JNI_TRUE if all refs are filled in, JNI_FALSE otherwise.
 */
static jboolean JNICALL
initDirectByteBufferCacheSun(JNIEnv *env, jclass java_nio_Buffer, jclass java_nio_DirectByteBuffer)
{
	J9JavaVM *javaVM = ((J9VMThread *) env)->javaVM;
	jclass sun_nio_ch_DirectBuffer = javaVM->sun_nio_ch_DirectBuffer;
	jmethodID java_nio_DirectByteBuffer_init = javaVM->java_nio_DirectByteBuffer_init;
	jfieldID java_nio_Buffer_address = javaVM->java_nio_Buffer_address;

	/* Check to see if we are already initialized */
	if ( (NULL != sun_nio_ch_DirectBuffer) &&
			(NULL != java_nio_DirectByteBuffer_init) &&
			(NULL != java_nio_Buffer_address) ) {
		return JNI_TRUE;
	}

	sun_nio_ch_DirectBuffer = findClassAndCreateGlobalRef(env, "sun/nio/ch/DirectBuffer");
	if (sun_nio_ch_DirectBuffer == NULL) {
		goto fail;
	}

	java_nio_DirectByteBuffer_init = env->GetMethodID(java_nio_DirectByteBuffer, "<init>", "(JI)V");
	if (java_nio_DirectByteBuffer_init == NULL) {
		goto fail;
	}

	java_nio_Buffer_address = env->GetFieldID(java_nio_Buffer, "address", "J");
	if (java_nio_Buffer_address == NULL) {
		goto fail;
	}

	javaVM->sun_nio_ch_DirectBuffer = sun_nio_ch_DirectBuffer;
	javaVM->java_nio_DirectByteBuffer_init = java_nio_DirectByteBuffer_init;
	javaVM->java_nio_Buffer_address = java_nio_Buffer_address;
	return JNI_TRUE;

fail:
	env->ExceptionClear();
	env->DeleteGlobalRef(sun_nio_ch_DirectBuffer);
	return JNI_FALSE;
}


static jboolean JNICALL
initDirectByteBufferCache(JNIEnv *env)
{
	J9JavaVM *javaVM = ((J9VMThread *) env)->javaVM;
	jclass java_nio_Buffer = javaVM->java_nio_Buffer;
	jclass java_nio_DirectByteBuffer = javaVM->java_nio_DirectByteBuffer;
	jfieldID java_nio_Buffer_capacity = javaVM->java_nio_Buffer_capacity;

	if (java_nio_Buffer && java_nio_DirectByteBuffer && java_nio_Buffer_capacity) {
		return initDirectByteBufferCacheSun(env, java_nio_Buffer, java_nio_DirectByteBuffer);
	}

	java_nio_DirectByteBuffer = NULL;
	java_nio_Buffer = findClassAndCreateGlobalRef(env, "java/nio/Buffer");
	if (java_nio_Buffer == NULL) {
		goto fail;
	}

	java_nio_DirectByteBuffer = findClassAndCreateGlobalRef(env, "java/nio/DirectByteBuffer");
	if (java_nio_DirectByteBuffer == NULL) {
		goto fail;
	}

	java_nio_Buffer_capacity = env->GetFieldID(java_nio_Buffer, "capacity", "I");
	if (java_nio_Buffer_capacity == NULL) {
		goto fail;
	}

	/* Commit the common IDs */
	javaVM->java_nio_Buffer = java_nio_Buffer;
	javaVM->java_nio_DirectByteBuffer = java_nio_DirectByteBuffer;
	javaVM->java_nio_Buffer_capacity = java_nio_Buffer_capacity;

	if (JNI_TRUE != initDirectByteBufferCacheSun(env, java_nio_Buffer, java_nio_DirectByteBuffer)) {
		goto fail;
	}

	return JNI_TRUE;

fail:
	env->ExceptionClear();
	env->DeleteGlobalRef(java_nio_Buffer);
	env->DeleteGlobalRef(java_nio_DirectByteBuffer);
	return JNI_FALSE;
}


static jobject JNICALL newDirectByteBuffer(JNIEnv *env, void *address, jlong capacity)
{
	J9JavaVM *javaVM = ((J9VMThread *) env)->javaVM;
	jint actualCapacity = (jint)capacity;
	jobject result = NULL;

	Trc_VM_JNI_NewDirectByteBuffer_Entry(env, address, capacity);

	if (!initDirectByteBufferCache(env)) {
		return NULL;
	}

	/* if capacity exceeds the range of a jint, pass in a value known to cause IllegalArgumentException */
	if (actualCapacity != capacity) {
		actualCapacity = -1;
	}
	result = env->NewObject(javaVM->java_nio_DirectByteBuffer, javaVM->java_nio_DirectByteBuffer_init, (jlong)(UDATA)address, actualCapacity);
	Trc_VM_JNI_NewDirectByteBuffer_Exit(env, result);
	return result;
}


static jint JNICALL pushLocalFrame(JNIEnv *env,  jint capacity) {
	UDATA result = 0;
	J9VMThread *vmThread = (J9VMThread*)env;
	J9SFJNINativeMethodFrame* frame;

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	/* ensure that there is an internal frame allocated before any user frames. Otherwise
	 * we won't know when to stop freeing frames when we return from the method
	 */
	frame = (J9SFJNINativeMethodFrame*)((U_8*)vmThread->sp + (UDATA)vmThread->literals);
	if ((frame->specialFrameFlags & J9_SSF_CALL_OUT_FRAME_ALLOC) == 0) {
		result = jniPushFrame(vmThread, JNIFRAME_TYPE_INTERNAL, 16);
	}

	if (result == 0) {
		result = jniPushFrame(vmThread, JNIFRAME_TYPE_USER, capacity);
		if (result == 0) {
			/* ensure that the frame is marked as allocated */
			frame->specialFrameFlags |= J9_SSF_CALL_OUT_FRAME_ALLOC;
		}
	}

	VM_VMAccess::inlineExitVMToJNI(vmThread);

	if (result) {
		ensurePendingJNIException(env);
		return -1;
	}

	return 0;
}


static jobject JNICALL popLocalFrame(JNIEnv *env,  jobject  result) {
	j9object_t unwrappedResult;

	VM_VMAccess::inlineEnterVMFromJNI((J9VMThread*)env);

	unwrappedResult = result ? *(j9object_t*)result : NULL;
	jniPopFrame((J9VMThread*)env, JNIFRAME_TYPE_USER);
	result = VM_VMHelpers::createLocalRef(env, unwrappedResult);

	VM_VMAccess::inlineExitVMToJNI((J9VMThread*)env);

	return result;
}


/*		1) Private routine.  Used to create a jni local reference from an actual object pointer.
		2) We don't acquire VM access - if you have an object pointer in your hands, you had better already have it.
*/
jobject JNICALL j9jni_createLocalRef(JNIEnv *env, j9object_t object) {
	J9VMThread *vmThread = (J9VMThread*)env;
	J9SFJNINativeMethodFrame* frame;
	J9JNIReferenceFrame *referenceFrame;
	j9object_t* ref;

	if (object == NULL) return NULL;

	frame = (J9SFJNINativeMethodFrame*)((U_8*)vmThread->sp + (UDATA)vmThread->literals);
	if ( (frame->specialFrameFlags & J9_SSF_CALL_OUT_FRAME_ALLOC) == 0) {
		/* try to allocate from stack */
		if ( (UDATA)vmThread->literals < J9_SSF_CO_REF_SLOT_CNT * sizeof(UDATA) ) {
			vmThread->literals = (J9Method*)((UDATA)vmThread->literals + sizeof(UDATA));
#ifdef J9VM_INTERP_GROWABLE_STACKS
			frame->specialFrameFlags += 1;
#endif
			ref = (j9object_t*)--(vmThread->sp);	/* predecrement sp */
			*ref = object;
			return (jobject)ref;
		} else {
			/* search for a free element on the stack */
			for (ref = (j9object_t*)vmThread->sp; ref < (j9object_t*)vmThread->sp + J9_SSF_CO_REF_SLOT_CNT; ref++) {
				if (*ref == NULL) {
					*ref = object;
					return (jobject)ref;
				}
			}

			/* couldn't find a free entry. Now we need to grow a pool */
			if (jniPushFrame(vmThread, JNIFRAME_TYPE_INTERNAL, 16)) {
				fatalError(env, "Could not allocate JNI local ref");
				return NULL;
			}
			frame->specialFrameFlags |= J9_SSF_CALL_OUT_FRAME_ALLOC;
		}
	}

	/* allocate from frame */
	referenceFrame = (J9JNIReferenceFrame*)vmThread->jniLocalReferences;
	ref = (j9object_t*)pool_newElement((J9Pool*)referenceFrame->references);
	if (ref == NULL) {
		fatalError(env, "Could not allocate JNI local ref");
		return NULL;
	}
	*ref = object;
	return (jobject)ref;
}


static jint JNICALL
ensureLocalCapacityWrapper(JNIEnv *env,  jint capacity)
{
	J9VMThread *vmThread = (J9VMThread*)env;
	J9SFJNINativeMethodFrame* frame;
	J9JNIReferenceFrame *referenceFrame;
	jint rc = 0;

	Trc_VM_ensureLocalCapacity_Entry(env, capacity);

	if (capacity > MAX_LOCAL_CAPACITY && (vmThread->javaVM->runtimeFlags & J9_RUNTIME_XFUTURE) ) {
		/* The JCK test vm.jni.enlc001 tries to allocate as many local references as possible.
		 * Sun only permits about 8192 local refs, allowing the test to pass. On J9, the capacity was limited only
		 * by available memory. This means that the test would exhaust system memory. Sometimes the test
		 * passed, sometimes the OS killed the process, sometimes another thread ran out of memory.
		 * 8192 seems a bit low, so we'll allow up to 64K refs.
		 */
		rc = JNI_ERR;
	} else {
		VM_VMAccess::inlineEnterVMFromJNI(vmThread);

		frame = (J9SFJNINativeMethodFrame*)((U_8*)vmThread->sp + (UDATA)vmThread->literals);
		if ( (frame->specialFrameFlags & J9_SSF_CALL_OUT_FRAME_ALLOC) == 0) {
			/* try to allocate from stack */
			if ( capacity >= J9_SSF_CO_REF_SLOT_CNT) {
				Trc_VM_ensureLocalCapacity_allocateNewFrame(env);
				if (jniPushFrame(vmThread, JNIFRAME_TYPE_INTERNAL, capacity)) {
					Trc_VM_ensureLocalCapacity_allocateFailed(env);
					rc = -1;
				} else {
					frame->specialFrameFlags |= J9_SSF_CALL_OUT_FRAME_ALLOC;
				}
			}
		} else {
			referenceFrame = (J9JNIReferenceFrame*)vmThread->jniLocalReferences;
			Trc_VM_ensureLocalCapacity_growExistingFrame(env, referenceFrame);
			if (pool_ensureCapacity((J9Pool*)referenceFrame->references, (UDATA)capacity)) {
				Trc_VM_ensureLocalCapacity_growFailed(env);
				rc = -1;
			}
		}
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}

	if (rc) {
		ensurePendingJNIException(env);
	}

	Trc_VM_ensureLocalCapacity_Exit(env, rc);

	return rc;
}


static jbooleanArray JNICALL newBooleanArray(JNIEnv *env, jsize length)
{
	return (jbooleanArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, booleanArrayClass));
}



static jbyteArray JNICALL newByteArray(JNIEnv *env, jsize length)
{
	return (jbyteArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, byteArrayClass));
}



static jcharArray JNICALL newCharArray(JNIEnv *env, jsize length)
{
	return (jcharArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, charArrayClass));
}



#if (defined(J9VM_INTERP_FLOAT_SUPPORT))
static jdoubleArray JNICALL newDoubleArray(JNIEnv *env, jsize length)
{
	return (jdoubleArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, doubleArrayClass));
}

#endif /* J9VM_INTERP_FLOAT_SUPPORT */


#if (defined(J9VM_INTERP_FLOAT_SUPPORT))
static jfloatArray JNICALL newFloatArray(JNIEnv *env, jsize length)
{
	return (jfloatArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, floatArrayClass));
}

#endif /* J9VM_INTERP_FLOAT_SUPPORT */


static jintArray JNICALL newIntArray(JNIEnv *env, jsize length)
{
	return (jintArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, intArrayClass));
}



static jlongArray JNICALL newLongArray(JNIEnv *env, jsize length)
{
	return (jlongArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, longArrayClass));
}



static jshortArray JNICALL newShortArray(JNIEnv *env, jsize length)
{
	return (jshortArray)newBaseTypeArray(env, length, offsetof(J9JavaVM, shortArrayClass));
}



static jobject JNICALL
newGlobalRef(JNIEnv *env, jobject localOrGlobalRef)
{
	return allocateGlobalRef(env, localOrGlobalRef, JNI_FALSE);
}



static jobject
allocateGlobalRef(JNIEnv *env, jobject localOrGlobalRef, jboolean isWeak)
{
	jobject result = NULL;

	if (localOrGlobalRef != NULL) {
		J9VMThread* vmThread = (J9VMThread*)env;
		j9object_t obj;

		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		obj = *((j9object_t*) localOrGlobalRef);
		if (obj != NULL) {
			result = j9jni_createGlobalRef(env, obj, isWeak);
		}
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}

	return result;
}



static jweak JNICALL
newWeakGlobalRef(JNIEnv *env, jobject localOrGlobalRef)
{
	return (jweak)allocateGlobalRef(env, localOrGlobalRef, JNI_TRUE);
}



static jobject JNICALL
newLocalRef(JNIEnv *env, jobject object)
{
	if (object == NULL) {
		return NULL;
	} else {
		J9VMThread* vmThread = (J9VMThread*)env;
		jobject result;
		j9object_t actualObject;

		VM_VMAccess::inlineEnterVMFromJNI(vmThread);

		actualObject = *(j9object_t*)object;
		result = VM_VMHelpers::createLocalRef(env, actualObject);

		VM_VMAccess::inlineExitVMToJNI(vmThread);

		return result;
	}
}



static void JNICALL
deleteGlobalRef(JNIEnv *env, jobject globalRef)
{
	deallocateGlobalRef(env, globalRef, JNI_FALSE);
}



static void JNICALL
deleteWeakGlobalRef(JNIEnv *env, jweak weakGlobalRef)
{
	deallocateGlobalRef(env, (jobject)weakGlobalRef, JNI_TRUE);
}



static void
deallocateGlobalRef(JNIEnv *env, jobject weakOrStrongGlobalRef, jboolean isWeak)
{
	J9VMThread* vmThread = (J9VMThread*)env;

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);
	j9jni_deleteGlobalRef(env, weakOrStrongGlobalRef, isWeak);
	VM_VMAccess::inlineExitVMToJNI(vmThread);
}



static jboolean JNICALL
exceptionCheck(JNIEnv *env)
{
	return ((J9VMThread*)env)->currentException != NULL;
}


static void JNICALL
exceptionClear(JNIEnv *env)
{
	J9VMThread* vmThread = (J9VMThread*)env;

	if (vmThread->currentException != NULL) {
		/* we need VM access here, as a GC thread could currently be in the process of forwarding the currentException */
		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		vmThread->currentException = NULL;
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}
}


static jthrowable JNICALL
exceptionOccurred(JNIEnv *env)
{
	jthrowable result = NULL;
	J9VMThread* vmThread = (J9VMThread*)env;

	if (vmThread->currentException != NULL) {
		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		result = (jthrowable)VM_VMHelpers::createLocalRef(env, vmThread->currentException);
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}

	return result;
}


void JNICALL gpCheckSetCurrentExceptionNLS(J9VMThread* env, UDATA exceptionNumber, U_32 moduleName, U_32 messageNumber)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		setCurrentExceptionNLS(env, exceptionNumber, moduleName, messageNumber);
	} else {
		J9RedirectedSetCurrentExceptionNLSArgs args;
		args.env = env;
		args.exceptionNumber = exceptionNumber;
		args.moduleName = moduleName;
		args.messageNumber = messageNumber;
		gpProtectAndRun(gpProtectedSetCurrentExceptionNLS, (JNIEnv*)env, &args);
	}
}



static UDATA gpProtectedSetCurrentExceptionNLS(void * entryArg)
{
	J9RedirectedSetCurrentExceptionNLSArgs * args = (J9RedirectedSetCurrentExceptionNLSArgs *) entryArg;
	setCurrentExceptionNLS(args->env, args->exceptionNumber, args->moduleName, args->messageNumber);
	return 0;
}



static UDATA
gpProtectedSetNativeOutOfMemoryError(void * entryArg)
{
	J9RedirectedSetCurrentExceptionNLSArgs * args = (J9RedirectedSetCurrentExceptionNLSArgs *) entryArg;
	setNativeOutOfMemoryError(args->env, args->moduleName, args->messageNumber);
	return 0;
}

void JNICALL gpCheckSetNativeOutOfMemoryError(J9VMThread* env, U_32 moduleName, U_32 messageNumber)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		setNativeOutOfMemoryError(env, moduleName, messageNumber);
	} else {
		J9RedirectedSetCurrentExceptionNLSArgs args;
		args.env = env;
		args.moduleName = moduleName;
		args.messageNumber = messageNumber;
		gpProtectAndRun(gpProtectedSetNativeOutOfMemoryError, (JNIEnv*)env, &args);
	}
}


static UDATA
gpProtectedSetHeapOutOfMemoryError(void * entryArg)
{
	J9RedirectedSetCurrentExceptionNLSArgs * args = (J9RedirectedSetCurrentExceptionNLSArgs *) entryArg;
	setHeapOutOfMemoryError(args->env);
	return 0;
}

void JNICALL gpCheckSetHeapOutOfMemoryError(J9VMThread* env)
{
	/* Check if already protected or -Xrs is set and short-circuit the path through gpProtectAndRun */
	if ((((J9VMThread *) env)->gpProtected) || (J9_ARE_ALL_BITS_SET(((J9VMThread *) env)->javaVM->sigFlags, J9_SIG_XRS_SYNC))) {
		setHeapOutOfMemoryError(env);
	} else {
		J9RedirectedSetCurrentExceptionNLSArgs args;
		args.env = env;
		gpProtectAndRun(gpProtectedSetHeapOutOfMemoryError, (JNIEnv*)env, &args);
	}
}



#ifndef J9VM_INTERP_FLOAT_SUPPORT
#define getDoubleField NULL
#define getFloatField NULL
#define getStaticDoubleField NULL
#define getStaticFloatField NULL
#define newDoubleArray NULL
#define newFloatArray NULL
#define setDoubleField NULL
#define setFloatField NULL
#define setStaticDoubleField NULL
#define setStaticFloatField NULL
#define callNonvirtualDoubleMethod NULL
#define callNonvirtualDoubleMethodA NULL
#define callNonvirtualDoubleMethodV NULL
#define callNonvirtualFloatMethod NULL
#define callNonvirtualFloatMethodA NULL
#define callNonvirtualFloatMethodV NULL
#define callStaticDoubleMethod NULL
#define callStaticDoubleMethodA NULL
#define callStaticDoubleMethodV NULL
#define callStaticFloatMethod NULL
#define callStaticFloatMethodA NULL
#define callStaticFloatMethodV NULL
#define callVirtualDoubleMethod NULL
#define callVirtualDoubleMethodA NULL
#define callVirtualDoubleMethodV NULL
#define callVirtualFloatMethod NULL
#define callVirtualFloatMethodA NULL
#define callVirtualFloatMethodV NULL
#endif

#ifdef J9VM_INTERP_MINIMAL_JNI	/* On minimal platforms don't include these functions */
#define registerNatives NULL
#define unregisterNatives NULL
#define defineClass NULL
#else
static jclass JNICALL defineClass(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize bufLen);
#endif

#if !defined(J9VM_OPT_REFLECT) || defined(J9VM_INTERP_MINIMAL_JNI)
#define fromReflectedField NULL
#define fromReflectedMethod NULL
#define toReflectedField NULL
#define gpCheckToReflectedField NULL
#define toReflectedMethod NULL
#define gpCheckToReflectedMethod NULL
#endif

struct JNINativeInterface_ EsJNIFunctions = {
	NULL,
	NULL,
	NULL,
	NULL,
	getVersion,
	defineClass,
	FIND_CLASS,
	fromReflectedMethod,
	fromReflectedField,
	TO_REFLECTED_METHOD,
	getSuperclass,
	isAssignableFrom,
	TO_REFLECTED_FIELD,
	jniThrow,
	throwNew,
	exceptionOccurred,
	exceptionDescribe,
	exceptionClear,
	fatalError,
	pushLocalFrame,
	popLocalFrame,
	newGlobalRef,
	deleteGlobalRef,
	deleteLocalRef,
	isSameObject,
	newLocalRef,
	ensureLocalCapacityWrapper,
	allocObject,
	newObject,
	newObjectV,
	newObjectA,
	getObjectClass,
	isInstanceOf,
	getMethodID,

	/* Macros defined in jnicimap.h for Call<type>Method<args> */
	CALL_VIRTUAL_OBJECT_METHOD,
	CALL_VIRTUAL_OBJECT_METHOD_V,
	CALL_VIRTUAL_OBJECT_METHOD_A,
	CALL_VIRTUAL_BOOLEAN_METHOD,
	CALL_VIRTUAL_BOOLEAN_METHOD_V,
	CALL_VIRTUAL_BOOLEAN_METHOD_A,
	CALL_VIRTUAL_BYTE_METHOD,
	CALL_VIRTUAL_BYTE_METHOD_V,
	CALL_VIRTUAL_BYTE_METHOD_A,
	CALL_VIRTUAL_CHAR_METHOD,
	CALL_VIRTUAL_CHAR_METHOD_V,
	CALL_VIRTUAL_CHAR_METHOD_A,
	CALL_VIRTUAL_SHORT_METHOD,
	CALL_VIRTUAL_SHORT_METHOD_V,
	CALL_VIRTUAL_SHORT_METHOD_A,
	CALL_VIRTUAL_INT_METHOD,
	CALL_VIRTUAL_INT_METHOD_V,
	CALL_VIRTUAL_INT_METHOD_A,
	CALL_VIRTUAL_LONG_METHOD,
	CALL_VIRTUAL_LONG_METHOD_V,
	CALL_VIRTUAL_LONG_METHOD_A,
	CALL_VIRTUAL_FLOAT_METHOD,
	CALL_VIRTUAL_FLOAT_METHOD_V,
	CALL_VIRTUAL_FLOAT_METHOD_A,
	CALL_VIRTUAL_DOUBLE_METHOD,
	CALL_VIRTUAL_DOUBLE_METHOD_V,
	CALL_VIRTUAL_DOUBLE_METHOD_A,
	CALL_VIRTUAL_VOID_METHOD,
	CALL_VIRTUAL_VOID_METHOD_V,
	CALL_VIRTUAL_VOID_METHOD_A,

	/* Macros defined in jnicimap.h for CallNonvirtual<type>Method<args> */
	CALL_NONVIRTUAL_OBJECT_METHOD,
	CALL_NONVIRTUAL_OBJECT_METHOD_V,
	CALL_NONVIRTUAL_OBJECT_METHOD_A,
	CALL_NONVIRTUAL_BOOLEAN_METHOD,
	CALL_NONVIRTUAL_BOOLEAN_METHOD_V,
	CALL_NONVIRTUAL_BOOLEAN_METHOD_A,
	CALL_NONVIRTUAL_BYTE_METHOD,
	CALL_NONVIRTUAL_BYTE_METHOD_V,
	CALL_NONVIRTUAL_BYTE_METHOD_A,
	CALL_NONVIRTUAL_CHAR_METHOD,
	CALL_NONVIRTUAL_CHAR_METHOD_V,
	CALL_NONVIRTUAL_CHAR_METHOD_A,
	CALL_NONVIRTUAL_SHORT_METHOD,
	CALL_NONVIRTUAL_SHORT_METHOD_V,
	CALL_NONVIRTUAL_SHORT_METHOD_A,
	CALL_NONVIRTUAL_INT_METHOD,
	CALL_NONVIRTUAL_INT_METHOD_V,
	CALL_NONVIRTUAL_INT_METHOD_A,
	CALL_NONVIRTUAL_LONG_METHOD,
	CALL_NONVIRTUAL_LONG_METHOD_V,
	CALL_NONVIRTUAL_LONG_METHOD_A,
	CALL_NONVIRTUAL_FLOAT_METHOD,
	CALL_NONVIRTUAL_FLOAT_METHOD_V,
	CALL_NONVIRTUAL_FLOAT_METHOD_A,
	CALL_NONVIRTUAL_DOUBLE_METHOD,
	CALL_NONVIRTUAL_DOUBLE_METHOD_V,
	CALL_NONVIRTUAL_DOUBLE_METHOD_A,
	CALL_NONVIRTUAL_VOID_METHOD,
	CALL_NONVIRTUAL_VOID_METHOD_V,
	CALL_NONVIRTUAL_VOID_METHOD_A,

	getFieldID,
	getObjectField,
 	getBooleanField,
 	getByteField,
 	getCharField,
 	getShortField,
	getIntField,
	getLongField,
	getFloatField,
	getDoubleField,
	setObjectField,
	setBooleanField,
	setByteField,
	setCharField,
	setShortField,
	setIntField,
	setLongField,
	setFloatField,
	setDoubleField,
	getStaticMethodID,

	/* Macros defined in jnicimap.h for CallStatic<type>Method<args> */
	CALL_STATIC_OBJECT_METHOD,
	CALL_STATIC_OBJECT_METHOD_V,
	CALL_STATIC_OBJECT_METHOD_A,
	CALL_STATIC_BOOLEAN_METHOD,
	CALL_STATIC_BOOLEAN_METHOD_V,
	CALL_STATIC_BOOLEAN_METHOD_A,
	CALL_STATIC_BYTE_METHOD,
	CALL_STATIC_BYTE_METHOD_V,
	CALL_STATIC_BYTE_METHOD_A,
	CALL_STATIC_CHAR_METHOD,
	CALL_STATIC_CHAR_METHOD_V,
	CALL_STATIC_CHAR_METHOD_A,
	CALL_STATIC_SHORT_METHOD,
	CALL_STATIC_SHORT_METHOD_V,
	CALL_STATIC_SHORT_METHOD_A,
	CALL_STATIC_INT_METHOD,
	CALL_STATIC_INT_METHOD_V,
	CALL_STATIC_INT_METHOD_A,
	CALL_STATIC_LONG_METHOD,
	CALL_STATIC_LONG_METHOD_V,
	CALL_STATIC_LONG_METHOD_A,
	CALL_STATIC_FLOAT_METHOD,
	CALL_STATIC_FLOAT_METHOD_V,
	CALL_STATIC_FLOAT_METHOD_A,
	CALL_STATIC_DOUBLE_METHOD,
	CALL_STATIC_DOUBLE_METHOD_V,
	CALL_STATIC_DOUBLE_METHOD_A,
	CALL_STATIC_VOID_METHOD,
	CALL_STATIC_VOID_METHOD_V,
	CALL_STATIC_VOID_METHOD_A,

	getStaticFieldID,
	getStaticObjectField,
 	(jboolean (JNICALL *)(JNIEnv *env, jclass clazzj, jfieldID fieldID))getStaticIntField,
 	(jbyte (JNICALL *)(JNIEnv *env, jclass clazzj, jfieldID fieldID))getStaticIntField,
 	(jchar (JNICALL *)(JNIEnv *env, jclass clazzj, jfieldID fieldID))getStaticIntField,
 	(jshort (JNICALL *)(JNIEnv *env, jclass clazzj, jfieldID fieldID))getStaticIntField,
	getStaticIntField,
	getStaticLongField,
	getStaticFloatField,
	getStaticDoubleField,
	setStaticObjectField,
	setStaticBooleanField,
	setStaticByteField,
	setStaticCharField,
	setStaticShortField,
	setStaticIntField,
	setStaticLongField,
	setStaticFloatField,
	setStaticDoubleField,
	newString,
	getStringLength,
	getStringChars,
	releaseStringChars,
	newStringUTF,
	getStringUTFLength,
	getStringUTFChars,
	releaseStringCharsUTF,
	getArrayLength,
	newObjectArray,
	getObjectArrayElement,
	setObjectArrayElement,
	newBooleanArray,
	newByteArray,
	newCharArray,
	newShortArray,
	newIntArray,
	newLongArray,
	newFloatArray,
	newDoubleArray,
	GET_BOOLEAN_ARRAY_ELEMENTS,
	GET_BYTE_ARRAY_ELEMENTS,
	GET_CHAR_ARRAY_ELEMENTS,
	GET_SHORT_ARRAY_ELEMENTS,
	GET_INT_ARRAY_ELEMENTS,
	GET_LONG_ARRAY_ELEMENTS,
	GET_FLOAT_ARRAY_ELEMENTS,
	GET_DOUBLE_ARRAY_ELEMENTS,
	RELEASE_BOOLEAN_ARRAY_ELEMENTS,
	RELEASE_BYTE_ARRAY_ELEMENTS,
	RELEASE_CHAR_ARRAY_ELEMENTS,
	RELEASE_SHORT_ARRAY_ELEMENTS,
	RELEASE_INT_ARRAY_ELEMENTS,
	RELEASE_LONG_ARRAY_ELEMENTS,
	RELEASE_FLOAT_ARRAY_ELEMENTS,
	RELEASE_DOUBLE_ARRAY_ELEMENTS,
	GET_BOOLEAN_ARRAY_REGION,
	GET_BYTE_ARRAY_REGION,
	GET_CHAR_ARRAY_REGION,
	GET_SHORT_ARRAY_REGION,
	GET_INT_ARRAY_REGION,
	GET_LONG_ARRAY_REGION,
	GET_FLOAT_ARRAY_REGION,
	GET_DOUBLE_ARRAY_REGION,
	SET_BOOLEAN_ARRAY_REGION,
	SET_BYTE_ARRAY_REGION,
	SET_CHAR_ARRAY_REGION,
	SET_SHORT_ARRAY_REGION,
	SET_INT_ARRAY_REGION,
	SET_LONG_ARRAY_REGION,
	SET_FLOAT_ARRAY_REGION,
	SET_DOUBLE_ARRAY_REGION,
	registerNatives,
	unregisterNatives,
	monitorEnter,
	monitorExit,
	getJavaVM,
	getStringRegion,
	getStringUTFRegion,
	getPrimitiveArrayCritical,
	releasePrimitiveArrayCritical,
	getStringCritical,
	releaseStringCritical,
	newWeakGlobalRef,
	deleteWeakGlobalRef,
	exceptionCheck,
	newDirectByteBuffer,
	getDirectBufferAddress,
	getDirectBufferCapacity,
	getObjectRefType,
#if JAVA_SPEC_VERSION >= 9
	getModule,
#endif /* JAVA_SPEC_VERSION >= 9 */
};

void  initializeJNITable(J9JavaVM *vm)
{
	vm->jniFunctionTable = GLOBAL_TABLE(EsJNIFunctions);
}

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
/**
* A utility routine invoked once by libjvm31 shim library to return
* a JNINativeInterface_ function table in below-the-bar storage.
* Certain entries that require customized handling for 31-bit callers
* are updated in the table, otherwise, will reuse 64-bit functions.
*
*@param[in] env The JNIEnv* of the current thread.
*@return Below-the-bar JNINativeInterface_ function table.
*/
JNINativeInterface_* JNICALL
J9VM_initializeJNI31Table(JNIEnv *env)
{
	PORT_ACCESS_FROM_VMC((J9VMThread *) env);
	/* This 64-bit XPLINK function pointer table of JNI native functions needs to be accessible by 31-bit shim library. */
	JNINativeInterface_ *EsJNI31Functions = (JNINativeInterface_*) j9mem_allocate_memory32(sizeof(JNINativeInterface_), OMRMEM_CATEGORY_VM);

	/* Majority of 64-bit JNI functions are common for 31-bit shim library.
	 * Update the specific entries for 31-bit customized functions below.
	 */
	memcpy(EsJNI31Functions, &EsJNIFunctions, sizeof(JNINativeInterface_));

	/* Macros defined in jnicimap.h for Call<type>Method<args> */
	EsJNI31Functions->CallObjectMethodV = CALL_VIRTUAL_OBJECT_METHOD_V31;
	EsJNI31Functions->CallBooleanMethodV = CALL_VIRTUAL_BOOLEAN_METHOD_V31;
	EsJNI31Functions->CallByteMethodV = CALL_VIRTUAL_BYTE_METHOD_V31;
	EsJNI31Functions->CallCharMethodV = CALL_VIRTUAL_CHAR_METHOD_V31;
	EsJNI31Functions->CallShortMethodV = CALL_VIRTUAL_SHORT_METHOD_V31;
	EsJNI31Functions->CallIntMethodV = CALL_VIRTUAL_INT_METHOD_V31;
	EsJNI31Functions->CallLongMethodV = CALL_VIRTUAL_LONG_METHOD_V31;
	EsJNI31Functions->CallFloatMethodV = CALL_VIRTUAL_FLOAT_METHOD_V31;
	EsJNI31Functions->CallDoubleMethodV = CALL_VIRTUAL_DOUBLE_METHOD_V31;
	EsJNI31Functions->CallVoidMethodV = CALL_VIRTUAL_VOID_METHOD_V31;

	/* Macros defined in jnicimap.h for CallNonvirtual<type>Method<args> */
	EsJNI31Functions->CallNonvirtualObjectMethodV = CALL_NONVIRTUAL_OBJECT_METHOD_V31;
	EsJNI31Functions->CallNonvirtualBooleanMethodV = CALL_NONVIRTUAL_BOOLEAN_METHOD_V31;
	EsJNI31Functions->CallNonvirtualByteMethodV = CALL_NONVIRTUAL_BYTE_METHOD_V31;
	EsJNI31Functions->CallNonvirtualCharMethodV = CALL_NONVIRTUAL_CHAR_METHOD_V31;
	EsJNI31Functions->CallNonvirtualShortMethodV = CALL_NONVIRTUAL_SHORT_METHOD_V31;
	EsJNI31Functions->CallNonvirtualIntMethodV = CALL_NONVIRTUAL_INT_METHOD_V31;
	EsJNI31Functions->CallNonvirtualLongMethodV = CALL_NONVIRTUAL_LONG_METHOD_V31;
	EsJNI31Functions->CallNonvirtualFloatMethodV = CALL_NONVIRTUAL_FLOAT_METHOD_V31;
	EsJNI31Functions->CallNonvirtualDoubleMethodV = CALL_NONVIRTUAL_DOUBLE_METHOD_V31;
	EsJNI31Functions->CallNonvirtualVoidMethodV = CALL_NONVIRTUAL_VOID_METHOD_V31;

	/* Macros defined in jnicimap.h for CallStatic<type>Method<args> */
	EsJNI31Functions->CallStaticObjectMethodV = CALL_STATIC_OBJECT_METHOD_V31;
	EsJNI31Functions->CallStaticBooleanMethodV = CALL_STATIC_BOOLEAN_METHOD_V31;
	EsJNI31Functions->CallStaticByteMethodV = CALL_STATIC_BYTE_METHOD_V31;
	EsJNI31Functions->CallStaticCharMethodV = CALL_STATIC_CHAR_METHOD_V31;
	EsJNI31Functions->CallStaticShortMethodV = CALL_STATIC_SHORT_METHOD_V31;
	EsJNI31Functions->CallStaticIntMethodV = CALL_STATIC_INT_METHOD_V31;
	EsJNI31Functions->CallStaticLongMethodV = CALL_STATIC_LONG_METHOD_V31;
	EsJNI31Functions->CallStaticFloatMethodV = CALL_STATIC_FLOAT_METHOD_V31;
	EsJNI31Functions->CallStaticDoubleMethodV = CALL_STATIC_DOUBLE_METHOD_V31;
	EsJNI31Functions->CallStaticVoidMethodV = CALL_STATIC_VOID_METHOD_V31;

	EsJNI31Functions->GetBooleanArrayElements = GET_BOOLEAN_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetByteArrayElements = GET_BYTE_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetCharArrayElements = GET_CHAR_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetShortArrayElements = GET_SHORT_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetIntArrayElements = GET_INT_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetLongArrayElements = GET_LONG_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetFloatArrayElements = GET_FLOAT_ARRAY_ELEMENTS31;
	EsJNI31Functions->GetDoubleArrayElements = GET_DOUBLE_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseBooleanArrayElements = RELEASE_BOOLEAN_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseByteArrayElements = RELEASE_BYTE_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseCharArrayElements = RELEASE_CHAR_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseShortArrayElements = RELEASE_SHORT_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseIntArrayElements = RELEASE_INT_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseLongArrayElements = RELEASE_LONG_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseFloatArrayElements = RELEASE_FLOAT_ARRAY_ELEMENTS31;
	EsJNI31Functions->ReleaseDoubleArrayElements = RELEASE_DOUBLE_ARRAY_ELEMENTS31;

	EsJNI31Functions->GetStringChars = getStringChars31;
	EsJNI31Functions->ReleaseStringChars = releaseStringChars31;
	EsJNI31Functions->GetStringUTFChars = getStringUTFChars31;
	EsJNI31Functions->ReleaseStringUTFChars = releaseStringCharsUTF31;

	EsJNI31Functions->NewObjectV = newObjectV31;

	/* Critical routines are mapped to the non-critical versions, as the Java objects
	 * are not directly accessible to AMODE31 callers.
	 */
	EsJNI31Functions->GetPrimitiveArrayCritical = GET_PRIMITIVE_ARRAY_CRITICAL31;
	EsJNI31Functions->ReleasePrimitiveArrayCritical = RELEASE_PRIMITIVE_ARRAY_CRITICAL31;
	EsJNI31Functions->GetStringCritical = GET_STRING_CRITICAL31;
	EsJNI31Functions->ReleaseStringCritical = RELEASE_STRING_CRITICAL31;

	return EsJNI31Functions;
}
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

/*
 * 1) Private routine.  Used to delete a jni global reference from an actual object pointer.
 * 2) We don't acquire VM access - caller must already have it.
 * 3) globalRef may be NULL
 */
void JNICALL
j9jni_deleteGlobalRef(JNIEnv *env, jobject globalRef, jboolean isWeak)
{
	J9VMThread * vmThread = (J9VMThread *) env;
	J9JavaVM * vm = vmThread->javaVM;

	Assert_VM_mustHaveVMAccess(vmThread);

	if (globalRef != NULL) {

#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_enter(vm->jniFrameMutex);
#endif

#if defined(J9VM_GC_REALTIME)
		vm->memoryManagerFunctions->j9gc_objaccess_jniDeleteGlobalReference(vmThread, *((j9object_t*)globalRef));
#endif /* defined(J9VM_GC_REALTIME) */
		if (pool_includesElement(isWeak ? vm->jniWeakGlobalReferences : vm->jniGlobalReferences, globalRef) == TRUE) {
			pool_removeElement(isWeak ? vm->jniWeakGlobalReferences : vm->jniGlobalReferences, globalRef);
		}

#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_exit(vm->jniFrameMutex);
#endif

	}
}


/*
 * 1) Private routine.  Used to create a jni global reference from an actual object pointer.
 * 2) We don't acquire VM access - if you have an object pointer in your hands, you had better already have it.
 * 3) Does not accept NULL for object (NULL check must be done by caller)
 * 4) Returns NULL if ref creation failed.
 */
jobject JNICALL
j9jni_createGlobalRef(JNIEnv *env, j9object_t object, jboolean isWeak)
{
	J9VMThread * vmThread = (J9VMThread *) env;
	J9JavaVM * vm = vmThread->javaVM;
	j9object_t * result;

	Assert_VM_mustHaveVMAccess(vmThread);
	Assert_VM_notNull(object);

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(vm->jniFrameMutex);
#endif

	result = (j9object_t*)pool_newElement(isWeak ? vm->jniWeakGlobalReferences : vm->jniGlobalReferences);
	if (result != NULL) {
		/* Initialize the ref under mutex as a concurrent collector may read from the slot as soon as we release the mutex */
		*result = object;
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(vm->jniFrameMutex);
#endif

	if (result == NULL) {
		fatalError(env, "Could not allocate JNI global ref");
		return NULL;
	}

	return (jobject) result;
}


/*
 * 1) Private routine.  Used to delete a jni local reference.
 * 2) We don't acquire VM access - caller must already have it.
 * 3) localRef may be NULL
 */
void JNICALL
j9jni_deleteLocalRef(JNIEnv *env, jobject localRef)
{
	J9VMThread *vmThread = (J9VMThread*)env;
	J9SFJNINativeMethodFrame* frame;
	j9object_t* ref = (j9object_t*) localRef;

	Assert_VM_mustHaveVMAccess(vmThread);

	/* Spec says it's illegal to delete a ref from anything but the topmost frame.
	 * Strictly speaking, we have a bug here, in that we allow deletion from the
	 * stacked local refs at all times.  We should really be checking to see if the
	 * user has pushed a frame (as opposed to the free frame push the VM will
	 * do when the stack-based ref area overflows).
	 */

	/* If the object is in the current stack frame's local reference area, NULL it,
	 * otherwise delete from the pool frame
	 */

	frame = (J9SFJNINativeMethodFrame*)((U_8*)vmThread->sp + (UDATA)vmThread->literals);
	if (ref == NULL) {
		/* do nothing */
	} else if ((ref >= (j9object_t*) vmThread->sp) && (ref < (j9object_t*) frame)) {
		*ref = NULL;
	} else if (frame->specialFrameFlags & J9_SSF_CALL_OUT_FRAME_ALLOC) {
		J9Pool * referenceFrame = (J9Pool*)((J9JNIReferenceFrame *) vmThread->jniLocalReferences)->references;

		/* If the reference is not in the current frame, this operation is nop */
		if (pool_includesElement(referenceFrame, ref)) {
			pool_removeElement(referenceFrame, ref);
		}
	}
}

static void*
getMethodOrFieldID(JNIEnv *env, jclass classReference, const char *name, const char *signature, UDATA flags)
{
	/* TODO: this all needs to be GP protected, since it may throw an exception */
	J9VMThread* vmThread = (J9VMThread*)env;
	J9Class* clazz;
	UDATA isField = (flags & J9JNIID_FIELD) != 0;
	UDATA isStatic = (flags & J9JNIID_STATIC) != 0;
	UDATA offset;
	UDATA element;
	J9Class* declaringClass;
	void* id;

	Trc_VM_getMethodOrFieldID_Entry(vmThread, name, signature, classReference, isField, isStatic);

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	/* TODO: normalize name and signature? */

retry:
	offset = 0;
	element = 0;
	declaringClass = NULL;
	id = NULL;
	clazz = J9VM_J9CLASS_FROM_JCLASS(vmThread, classReference);

	Trc_VM_getMethodOrFieldID_dereferencedClass(vmThread, clazz,
		(UDATA)J9UTF8_LENGTH(J9ROMCLASS_CLASSNAME(clazz->romClass)),
		J9UTF8_DATA(J9ROMCLASS_CLASSNAME(clazz->romClass)));

	if (!isField) {
		J9JNINameAndSignature nas;
		UDATA lookupOptions = J9_LOOK_JNI;

		nas.name = name;
		nas.signature = signature;
		nas.nameLength = (U_32)strlen(name);
		nas.signatureLength = (U_32)strlen(signature);

		lookupOptions |= (isStatic ? J9_LOOK_STATIC : J9_LOOK_VIRTUAL);

		element = javaLookupMethod(vmThread, clazz, (J9ROMNameAndSignature*)&nas, NULL, lookupOptions);
		if (element == 0) {
			declaringClass = NULL;
		} else {
			declaringClass = UNTAGGED_METHOD_CP((J9Method*)element)->ramClass;
		}

	} else {

		if (isStatic) {
			void* fieldAddress = staticFieldAddress(vmThread,
				clazz,
				(U_8*)name, strlen(name),
				(U_8*)signature, strlen(signature),
				&declaringClass, &element,
				0,
				NULL);

			if (fieldAddress == NULL) {
				declaringClass = NULL;
			} else {
				offset = (UDATA)fieldAddress - (UDATA)declaringClass->ramStatics;
			}

		} else {
			offset = instanceFieldOffset(vmThread,
				clazz,
				(U_8*)name, strlen(name),
				(U_8*)signature, strlen(signature),
				&declaringClass, &element,
				0);

			if (offset == (UDATA)-1) {
				declaringClass = NULL;
			}
		}
	}

	if (declaringClass) {
		UDATA preCount = vmThread->javaVM->hotSwapCount;

		/* ensure that the class is initialized */
		if (declaringClass->initializeStatus != J9ClassInitSucceeded && declaringClass->initializeStatus != (UDATA) vmThread) {
			gpCheckInitialize(vmThread, declaringClass);
		}

		if (vmThread->currentException == NULL) {
			if (preCount != vmThread->javaVM->hotSwapCount) {
				goto retry;
			}
			if (isField) {
				UDATA inconsistentData = 0;
				id = getJNIFieldID(vmThread, declaringClass, (J9ROMFieldShape *) element, offset, &inconsistentData);
				if (0 != inconsistentData) {
					/* declaringClass->romClass & element are inconsistent due to 
					 * class redefinition. Retry the operation and hope a redefinition
					 * doesn't occur again 
					 */
					goto retry;
				}
			} else {
				id = getJNIMethodID(vmThread, (J9Method *) element);
			}
		}
	}

	TRIGGER_J9HOOK_VM_LOOKUP_JNI_ID(vmThread->javaVM->hookInterface,
			vmThread, classReference, name, signature, (U_8)isStatic, (U_8)isField, id);

	VM_VMAccess::inlineExitVMToJNI(vmThread);

	if (id == NULL) {
		ensurePendingJNIException(env);
	}

	Trc_VM_getMethodOrFieldID_Exit(vmThread, id);

	return id;

}


/**
 * Ensures that an exception is pending in the current thread.
 * If no exception is currently pending, then a native OutOfMemoryError is thrown
 *
 * @arg[in] env - the current JNI environment
 */
static void
ensurePendingJNIException(JNIEnv* env)
{
	if (!exceptionCheck(env)) {
		J9VMThread * currentThread = (J9VMThread*)env;

		VM_VMAccess::inlineEnterVMFromJNI(currentThread);
		SET_NATIVE_OUT_OF_MEMORY_ERROR(currentThread, 0, 0);
		VM_VMAccess::inlineExitVMToJNI(currentThread);
	}
}


/**
 * @internal
 *
 * Pushes a local frame on the JNI stack.
 *
 * @note this function does NOT set the J9_SSF_CALL_OUT_FRAME_ALLOC bit in the topmost
 * frame's specialFrameFlags. It is the caller's responsibility to do so
 */
static UDATA
jniPushFrame(J9VMThread * vmThread, UDATA type, UDATA capacity)
{
	J9JavaVM* javaVM = vmThread->javaVM;
	UDATA result = 1;
	J9JNIReferenceFrame* frame;

	Trc_VM_jniPushFrame_Entry(vmThread, type, capacity);

	/* Allocate per-thread JNI reference frame pools */
	if (vmThread->jniReferenceFrames == NULL) {
		if ((vmThread->jniReferenceFrames = pool_new(sizeof(struct J9JNIReferenceFrame), 16, 0, POOL_NO_ZERO, J9_GET_CALLSITE(), J9MEM_CATEGORY_JNI, POOL_FOR_PORT(javaVM->portLibrary))) == NULL) {
			goto exit;
		}
	}

	frame = (J9JNIReferenceFrame*)pool_newElement(vmThread->jniReferenceFrames);

	if (frame) {
		frame->type = type;
		frame->previous = (J9JNIReferenceFrame*)vmThread->jniLocalReferences;
		frame->references = pool_new( sizeof(UDATA), capacity, sizeof(UDATA), POOL_NO_ZERO, J9_GET_CALLSITE(), J9MEM_CATEGORY_JNI, POOL_FOR_PORT(javaVM->portLibrary));
		if (frame->references) {
			vmThread->jniLocalReferences = (UDATA*)frame;
			result = 0;
		} else {
			pool_removeElement(vmThread->jniReferenceFrames, frame);
		}
	}

exit:
	Trc_VM_jniPushFrame_Exit(vmThread, result);

	return result;
}


/**
 * @internal
 *
 * Pops all local frames off the stack up to and including the first frame with the specified type.
 *
 */
void
jniPopFrame(J9VMThread * vmThread, UDATA type)
{
	J9JNIReferenceFrame* frame;

	Trc_VM_jniPopFrame_Entry(vmThread, type);

	frame = (J9JNIReferenceFrame*)vmThread->jniLocalReferences;

	while (frame != NULL) {
		UDATA currentFrameType = frame->type;
		J9JNIReferenceFrame* previousFrame = frame->previous;

		pool_kill((J9Pool*)frame->references);
		pool_removeElement(vmThread->jniReferenceFrames, frame);

		frame = previousFrame;

		if (currentFrameType == type) {
			break;
		}
	}

	vmThread->jniLocalReferences = (UDATA*)frame;

	Trc_VM_jniPopFrame_Exit(vmThread);
}


static jstring JNICALL
newString(JNIEnv *env, const jchar* uchars, jsize len)
{
	J9VMThread * vmThread = (J9VMThread *) env;
	jstring result = NULL;

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	if (len < 0) {
		setCurrentExceptionUTF(vmThread, J9VMCONSTANTPOOL_JAVALANGNEGATIVEARRAYSIZEEXCEPTION, NULL);
	} else {
		j9object_t stringObject = vmThread->javaVM->memoryManagerFunctions->j9gc_createJavaLangString(vmThread, (U_8 *) uchars, ((UDATA)len) * 2, J9_STR_UNICODE);

		if (stringObject != NULL) {
			result = (jstring)VM_VMHelpers::createLocalRef(env, stringObject);
		}
	}

	VM_VMAccess::inlineExitVMToJNI(vmThread);

	return result;
}


static jint JNICALL
monitorEnter(JNIEnv* env, jobject obj)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	jint rc = 0;

	Trc_VM_JNI_monitorEnter_Entry(vmThread, obj);

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	j9object_t object = *(j9object_t*)obj;
	UDATA monstatus = objectMonitorEnter(vmThread, object);

	if (J9_OBJECT_MONITOR_ENTER_FAILED(monstatus)) {
fail:
#if JAVA_SPEC_VERSION >= 16
		if (J9_OBJECT_MONITOR_VALUE_TYPE_IMSE == monstatus) {
			J9Class* badClass = J9OBJECT_CLAZZ(vmThread, obj);
			J9UTF8 *badClassName = J9ROMCLASS_CLASSNAME(badClass->romClass);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			if (J9_IS_J9CLASS_VALUETYPE(badClass)) {
				setCurrentExceptionNLSWithArgs(vmThread, J9NLS_VM_ERROR_BYTECODE_OBJECTREF_CANNOT_BE_VALUE_TYPE, J9VMCONSTANTPOOL_JAVALANGILLEGALMONITORSTATEEXCEPTION, J9UTF8_LENGTH(badClassName), J9UTF8_DATA(badClassName));
			} else 
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */		
			{
				Assert_VM_true(J9_ARE_ALL_BITS_SET(vmThread->javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_VALUE_BASED_EXCEPTION));
				setCurrentExceptionNLSWithArgs(vmThread, J9NLS_VM_ERROR_BYTECODE_OBJECTREF_CANNOT_BE_VALUE_BASED, J9VMCONSTANTPOOL_JAVALANGVIRTUALMACHINEERROR, J9UTF8_LENGTH(badClassName), J9UTF8_DATA(badClassName));
			}
		} else
#endif /* JAVA_SPEC_VERSION >= 16 */
		if (J9_OBJECT_MONITOR_OOM == monstatus) {
			SET_NATIVE_OUT_OF_MEMORY_ERROR(vmThread, J9NLS_VM_FAILED_TO_ALLOCATE_MONITOR);
		}
		rc = -1;
	} else if (J9_UNEXPECTED(!VM_ObjectMonitor::recordJNIMonitorEnter(vmThread, (j9object_t)monstatus))) {
		objectMonitorExit(vmThread, (j9object_t)monstatus);
		goto fail;
	}

	VM_VMAccess::inlineExitVMToJNI(vmThread);

	Trc_VM_JNI_monitorEnter_Entry(vmThread, rc);

	return rc;
}

static jint JNICALL
monitorExit(JNIEnv* env, jobject obj)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	j9object_t object;
	jint rc = 0;

	Trc_VM_JNI_monitorExit_Entry(vmThread, obj);

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	object = *(j9object_t*)obj;

	if (objectMonitorExit(vmThread, object)) {
		SET_CURRENT_EXCEPTION(vmThread, J9VMCONSTANTPOOL_JAVALANGILLEGALMONITORSTATEEXCEPTION, NULL);
		rc = -1;
	}
	VM_ObjectMonitor::recordJNIMonitorExit(vmThread, object);

	VM_VMAccess::inlineExitVMToJNI(vmThread);

	Trc_VM_JNI_monitorExit_Entry(vmThread, rc);

	return rc;
}

static jboolean JNICALL
isInstanceOf(JNIEnv *env, jobject obj, jclass clazz)
{
	jboolean result = JNI_TRUE;

	if (obj != NULL) {
		J9VMThread* vmThread = (J9VMThread*)env;
		j9object_t object;

		VM_VMAccess::inlineEnterVMFromJNI(vmThread);
		object = *(j9object_t*)obj;
		if (object != NULL) {
			result = (jboolean) VM_VMHelpers::inlineCheckCast(J9OBJECT_CLAZZ(vmThread, object), J9VM_J9CLASS_FROM_JCLASS(vmThread, clazz));
		}
		VM_VMAccess::inlineExitVMToJNI(vmThread);
	}

	return result;
}


void **
ensureJNIIDTable(J9VMThread *currentThread, J9Class* clazz)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	J9ClassLoader * classLoader = clazz->classLoader;
	void ** jniIDs;

	/* First ensure that the ID pool in the class loader exists */

	if (classLoader->jniIDs == NULL) {
		J9Pool * idPool = pool_new(sizeof(J9GenericJNIID),  16, 0, 0, J9_GET_CALLSITE(), J9MEM_CATEGORY_JNI, (j9memAlloc_fptr_t)pool_portLibAlloc, (j9memFree_fptr_t)pool_portLibFree, PORTLIB);

		if (idPool == NULL) {
			return NULL;
		}
		classLoader->jniIDs = idPool;
	}

	/* Now ensure the class ID table exists */

	jniIDs = clazz->jniIDs;
	if (jniIDs == NULL) {
		J9ROMClass * romclass = clazz->romClass;
		UDATA size = (romclass->romMethodCount + romclass->romFieldCount) * sizeof(void *);

		jniIDs = (void**)j9mem_allocate_memory(size, J9MEM_CATEGORY_JNI);
		if (jniIDs != NULL) {
			memset(jniIDs, 0, size);
			issueWriteBarrier();
			clazz->jniIDs = jniIDs;
		}
	}

	return jniIDs;
}


J9JNIFieldID *
getJNIFieldID(J9VMThread *currentThread, J9Class* declaringClass, J9ROMFieldShape* field, UDATA offset, UDATA *inconsistentData)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9JNIFieldID * id = NULL;
	J9ROMClass * romClass = declaringClass->romClass;
	UDATA fieldIndex = romClass->romMethodCount;

	J9ROMFieldWalkState state;
	J9ROMFieldShape * current = NULL;

	/* Determine the field index (field IDs are stored after method IDs in the array).
	 *
	 * If a redefinition has occurred, the `field` may be from a different J9ROMClass
	 * than the `declaringClass->romClass`.  The romFieldsNextDo will return NULL
	 * when all the fields of the `declaringClass->romClass` have been iterated over.
	 * As this operation is always expected to succeed, we can use the NULL to indicate
	 * the data is inconsistent.
	 */
	current = romFieldsStartDo(romClass, &state);
	while ((field != current) && (current != NULL)) {
		fieldIndex += 1;
		current = romFieldsNextDo(&state);
	}

	if (NULL == current) {
		/* Mismatch between the `field` & the `declaringClass->romClass`
		 * due to redefinition detected!
		 */
		if (NULL != inconsistentData) {
			*inconsistentData = 1;
		}
		id = NULL;
	} else {
		void ** jniIDs = declaringClass->jniIDs;
		/* If the table exists already, check it without mutex */

		if (jniIDs != NULL) {
			id = (J9JNIFieldID*)(jniIDs[fieldIndex]);
			if (id != NULL) {
				return id;
			}
		}

#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_enter(vm->jniFrameMutex);
#endif

		jniIDs = ensureJNIIDTable(currentThread, declaringClass);
		if (jniIDs == NULL) {
			id = NULL;
		} else {
			id = (J9JNIFieldID*)(jniIDs[fieldIndex]);
			if (id == NULL) {
				id = (J9JNIFieldID*)pool_newElement(declaringClass->classLoader->jniIDs);
				if (id != NULL) {
					id->offset = offset;
					id->declaringClass = declaringClass;
					id->field = field;
					id->index = fieldIndex;
					issueWriteBarrier();
					jniIDs[fieldIndex] = id;
				}
			}
		}

#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_exit(vm->jniFrameMutex);
#endif
	}
	return id;
}


J9JNIMethodID *
getJNIMethodID(J9VMThread *currentThread, J9Method *method)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9JNIMethodID * id;
	J9Class * declaringClass = J9_CLASS_FROM_METHOD(method);
	UDATA methodIndex = getMethodIndex(method);
	void ** jniIDs;

	/* If the table exists already, check it without mutex */

	jniIDs = declaringClass->jniIDs;
	if (jniIDs != NULL) {
		id = (J9JNIMethodID*)(jniIDs[methodIndex]);
		if (id != NULL) {
			return id;
		}
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(vm->jniFrameMutex);
#endif

	jniIDs = ensureJNIIDTable(currentThread, declaringClass);
	if (jniIDs == NULL) {
		id = NULL;
	} else {
		id = (J9JNIMethodID*)(jniIDs[methodIndex]);
		if (id == NULL) {
			id = (J9JNIMethodID*)pool_newElement(declaringClass->classLoader->jniIDs);
			if (id != NULL) {
				initializeMethodID(currentThread, id, method);
				issueWriteBarrier();
				jniIDs[methodIndex] = id;
			}
		}
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(vm->jniFrameMutex);
#endif

	return id;
}


void
initializeMethodID(J9VMThread * currentThread, J9JNIMethodID * methodID, J9Method * method)
{
	UDATA vTableIndex = 0;

	/* The vTable does not contain private or static methods */
	if (J9_ARE_NO_BITS_SET(J9_ROM_METHOD_FROM_RAM_METHOD(method)->modifiers, J9AccStatic | J9AccPrivate)) {
		J9Class * declaringClass = J9_CLASS_FROM_METHOD(method);

		if (declaringClass->romClass->modifiers & J9AccInterface) {
			/* Because methodIDs do not store the original lookup class for interface methods,
			 * always use the declaring class of the interface method.  Pass NULL here to allow
			 * for methodIDs to be created on obsolete classes for HCR purposes.
			 */
			vTableIndex = getITableIndexForMethod(method, NULL);
			/* Ensure the iTableIndex isn't so large it sets J9_JNI_MID_INTERFACE */
			Assert_VM_false(J9_ARE_ANY_BITS_SET(vTableIndex, J9_JNI_MID_INTERFACE));
			vTableIndex |= J9_JNI_MID_INTERFACE;
		} else {
			vTableIndex = getVTableOffsetForMethod(method, declaringClass, currentThread);
			/* Ensure the vTableOffset isn't so large it sets J9_JNI_MID_INTERFACE */
			Assert_VM_false(J9_ARE_ANY_BITS_SET(vTableIndex, J9_JNI_MID_INTERFACE));
		}
	}

	methodID->method = method;
	methodID->vTableIndex = vTableIndex;
}


J9Method *
findJNIMethod(J9VMThread* currentThread, J9Class* clazz, char* name, char* signature)
{
	J9Method * result = NULL;
	J9Method * method = clazz->ramMethods;
	UDATA count = clazz->romClass->romMethodCount;
	UDATA nameLength = strlen(name);
	UDATA signatureLength = strlen(signature);
	J9ROMMethod * romMethod;

	while (count != 0) {
		J9UTF8 * methodSignature = NULL;
		J9UTF8 * methodName = NULL;

		romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
		methodSignature = J9ROMMETHOD_SIGNATURE(romMethod);
		methodName = J9ROMMETHOD_NAME(romMethod);
		if ((J9UTF8_LENGTH(methodSignature) == signatureLength)
		&& (J9UTF8_LENGTH(methodName) == nameLength)
		&& (memcmp(J9UTF8_DATA(methodSignature), signature, signatureLength) == 0)
		&& (memcmp(J9UTF8_DATA(methodName), name, nameLength) == 0)
		) {
			result = method;
			break;
		}
		--count;
		++method;
	}

#ifdef J9VM_OPT_JVMTI
	if (result != NULL) {
		if ((romMethod->modifiers & J9AccNative) == 0) {
			TRIGGER_J9HOOK_VM_FIND_NATIVE_TO_REGISTER(currentThread->javaVM->hookInterface, currentThread, result);
		}
	}
#endif

	return result;
}

UDATA
jniIsInternalClassRef(J9JavaVM *vm, jobject ref)
{
	UDATA rc = FALSE;
	J9ClassWalkState classWalkState;
	J9Class *clazz = allLiveClassesStartDo(&classWalkState, vm, NULL);
	while (clazz != NULL) {
		do {
			if (ref == (jobject)&clazz->classObject) {
				rc = TRUE;
				goto done;
			}
			clazz = clazz->replacedClass;
		} while (NULL != clazz);
		clazz = allLiveClassesNextDo(&classWalkState);
	}
done:
	allLiveClassesEndDo(&classWalkState);
	return rc;
}

static jobjectRefType JNICALL
getObjectRefType(JNIEnv *env, jobject obj)
{
	J9VMThread * vmThread = (J9VMThread *) env;
	J9JavaVM * vm = vmThread->javaVM;
	jobjectRefType rc = JNIInvalidRefType;
	J9JNIReferenceFrame* frame;
	J9JavaStack* stack = vmThread->stackObject;

	VM_VMAccess::inlineEnterVMFromJNI(vmThread);

	/* Quick check for NULL or tagged pointers */

	if ((obj == NULL) || ((((UDATA)obj) & ((UDATA)sizeof(UDATA) - 1)) != 0)) {
		goto done;
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(vm->jniFrameMutex);
#endif

	/* Check for global ref */

	if (pool_includesElement(vm->jniGlobalReferences, obj)) {
#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_exit(vm->jniFrameMutex);
#endif
		rc = JNIGlobalRefType;
		goto done;
	}

	/* Check for weak global ref */

	if (pool_includesElement(vm->jniWeakGlobalReferences, obj)) {
#ifdef J9VM_THR_PREEMPTIVE
		omrthread_monitor_exit(vm->jniFrameMutex);
#endif
		rc = JNIWeakGlobalRefType;
		goto done;
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(vm->jniFrameMutex);
#endif

	/* Check for stack-based local refs */

	stack = vmThread->stackObject;
	while (stack != NULL) {
		if ((U_8*)obj < (U_8*)stack->end && (U_8*)obj >= (U_8*)(stack+1)) {
			rc = JNILocalRefType;
			goto done;
		}
		stack = stack->previous;
	}

	/* Check local reference frames */

	frame = (J9JNIReferenceFrame*)vmThread->jniLocalReferences;
	while (frame != NULL) {
		if (pool_includesElement((J9Pool*)frame->references, obj)) {
			rc = JNILocalRefType;
			goto done;
		}
		frame = frame->previous;
	}

	/* Check for class refs */

	if (jniIsInternalClassRef(vm, obj)) {
		rc = JNILocalRefType;
		goto done;
	}

done:
	VM_VMAccess::inlineExitVMToJNI(vmThread);

	return rc;
}

#if JAVA_SPEC_VERSION >= 9
static jobject JNICALL
getModule(JNIEnv *env, jclass clazz)
{
	J9VMThread *vmThread = (J9VMThread *) env;
	jobject module = NULL;
	
	VM_VMAccess::inlineEnterVMFromJNI(vmThread);
	if (NULL == clazz) {
		setCurrentExceptionUTF(vmThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		J9JavaVM *vm = vmThread->javaVM;
		J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
		j9object_t clazzObject = J9_JNI_UNWRAP_REFERENCE(clazz);

		module = vmFuncs->j9jni_createLocalRef((JNIEnv *) vmThread, J9VMJAVALANGCLASS_MODULE(vmThread, clazzObject));
	}
	VM_VMAccess::inlineExitVMToJNI(vmThread);
	return module;
}
#endif /* JAVA_SPEC_VERSION >= 9 */


IDATA
jniParseArguments(J9JavaVM *vm, char *optArg)
{
	char *scan_start, *scan_limit;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* initialize defaults, first */

#ifdef J9VM_GC_JNI_ARRAY_CACHE
	vm->jniArrayCacheMaxSize = J9_GC_JNI_ARRAY_CACHE_SIZE;
#endif /* J9VM_GC_JNI_ARRAY_CACHE */

	/* parse arguments */
	if (optArg == NULL) {
		return J9VMDLLMAIN_OK;
	}

	scan_start = optArg;
	scan_limit = optArg + strlen(optArg);
	while (scan_start < scan_limit) {

		/* ignore separators */
		try_scan(&scan_start, ",");

		if (try_scan(&scan_start, "help")) {
			/*
				# help text for -Xjni:help
				J9NLS_VM_XJNI_OPTIONS_1=Usage:\n
				J9NLS_VM_XJNI_OPTIONS_2=\   -Xjni:arrayCacheMax=[<x>|unlimited] set maximum size of JNI cached array\n
			*/
			j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_VM_XJNI_OPTIONS_1 );
#if defined(J9VM_GC_JNI_ARRAY_CACHE)
			j9nls_printf(PORTLIB, J9NLS_DO_NOT_PRINT_MESSAGE_TAG, J9NLS_VM_XJNI_OPTIONS_2 );
#endif
			return J9VMDLLMAIN_SILENT_EXIT_VM;
		}

#if defined(J9VM_GC_JNI_ARRAY_CACHE)
		if (try_scan(&scan_start, "arrayCacheMax=")) {
			if(try_scan(&scan_start, "unlimited")) {
				vm->jniArrayCacheMaxSize = (UDATA)-1;
			} else {
				if(scan_udata(&scan_start, &vm->jniArrayCacheMaxSize)) {
					goto _error;
				}
			}
			continue;
		}
#endif /* J9VM_GC_JNI_ARRAY_CACHE */

		/* Couldn't find a match for arguments */
		goto _error; /* avoid warning */
_error:
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_VM_UNRECOGNIZED_XJNI_OPTION, scan_start);
		return J9VMDLLMAIN_FAILED;
	}

	return J9VMDLLMAIN_OK;
}


#if !defined(J9VM_INTERP_MINIMAL_JNI)

static jclass JNICALL
defineClass(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize bufLen)
{
	J9VMThread * currentThread = (J9VMThread *) env;
	jclass result = NULL;

	VM_VMAccess::inlineEnterVMFromJNI(currentThread);

#if defined(J9VM_OPT_DYNAMIC_LOAD_SUPPORT)
	if (bufLen < 0) {
		setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINDEXOUTOFBOUNDSEXCEPTION, NULL);
	} else {
#define JAVA_PACKAGE_NAME_LENGTH 5 /* Number of characters in "java/" */
		J9JavaVM * vm = currentThread->javaVM;
		J9ClassLoader * classLoader;
		J9TranslationBufferSet *dynamicLoadBuffers;
		UDATA classNameLength;
		U_8 * className;
		U_8 check;
		U_8 c;
		U_8 * checkPtr;
		J9Class * clazz = NULL;
		BOOLEAN errorOccurred = FALSE;

		if (loader == NULL) {
			classLoader = vm->systemClassLoader;
		} else {
			j9object_t loaderObject = *(j9object_t *) loader;

			classLoader = J9VMJAVALANGCLASSLOADER_VMREF(currentThread, loaderObject);
			if (NULL == classLoader) {
			  classLoader = internalAllocateClassLoader(vm, loaderObject);
			  if (NULL == classLoader) {
				  goto done;
			  }
			}
		}

		/* Make sure the UTF is compact */

		check = 0;
		classNameLength = 0;
		checkPtr = (U_8 *) name;
		while ((c = *checkPtr++) != 0) {
			check |= c;
			classNameLength += 1;
		}
		className = (U_8 *) name;
		if (c & 0x80) {
			className = compressUTF8(currentThread, className, classNameLength, &classNameLength);
			if (className == NULL) {
				/* Exception already set */
				errorOccurred = TRUE;
			}
		}

		/* Ensure that classes cannot be defined from JNI in package "java.*" */
		if (!errorOccurred && (JAVA_PACKAGE_NAME_LENGTH < classNameLength)) { /* className includes the package name */
			const char *javaPackageName = "java/";
			if (0 == memcmp(javaPackageName, className, JAVA_PACKAGE_NAME_LENGTH)) {
				/* className starts with "java/", throw a SecurityException */
				PORT_ACCESS_FROM_JAVAVM(vm);
				char *msgChars = NULL;
				const char *nlsMsgFormat = j9nls_lookup_message(J9NLS_DO_NOT_PRINT_MESSAGE_TAG | J9NLS_DO_NOT_APPEND_NEWLINE, J9NLS_VM_EXCEPTION_DEFINECLASS_IN_PROTECTED_SYSTEM_PACKAGE, NULL);

				if (NULL != nlsMsgFormat) {
					UDATA msgCharLength = strlen(nlsMsgFormat);
					msgCharLength += classNameLength;
					msgCharLength += JAVA_PACKAGE_NAME_LENGTH;

					msgChars = (char *)j9mem_allocate_memory(msgCharLength + 1, J9MEM_CATEGORY_JNI);

					if (NULL != msgChars) {
						j9str_printf(PORTLIB, msgChars, msgCharLength, nlsMsgFormat, classNameLength, className, JAVA_PACKAGE_NAME_LENGTH, javaPackageName);
					}
				}

				setCurrentExceptionUTF(currentThread,
										J9VMCONSTANTPOOL_JAVALANGSECURITYEXCEPTION,
										msgChars);

				if (NULL != msgChars) {
					j9mem_free_memory(msgChars);
				}

				errorOccurred = TRUE;
			}
		}

		if (!errorOccurred) {
			omrthread_monitor_enter(vm->classTableMutex);
			dynamicLoadBuffers = vm->dynamicLoadBuffers;
			if (NULL == dynamicLoadBuffers) {
				omrthread_monitor_exit(vm->classTableMutex);
				setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNOCLASSDEFFOUNDERROR, "dynamic loader is unavailable");
			} else {
				/* Defining a class which isn't loaded from the class path, set the classpath index used to -1.
				 * entryIndex only applies to the system classloader, but since the classTableMutex is acquired,
				 * there is no threading problem.
				 */
				J9TranslationLocalBuffer localBuffer = {J9_CP_INDEX_NONE, LOAD_LOCATION_UNKNOWN, NULL};
				/* this function exits the class table mutex */
				clazz = dynamicLoadBuffers->internalDefineClassFunction(currentThread, className, classNameLength,
						(U_8 *) buf, (UDATA) bufLen, NULL, classLoader, NULL, J9_FINDCLASS_FLAG_THROW_ON_FAIL, NULL, NULL, &localBuffer);
				if (currentThread->privateFlags & J9_PRIVATE_FLAGS_CLOAD_NO_MEM) {
					/*Trc_VM_internalFindClass_gcAndRetry(vmThread);*/
					currentThread->javaVM->memoryManagerFunctions->j9gc_modron_global_collect_with_overrides(currentThread, J9MMCONSTANT_EXPLICIT_GC_NATIVE_OUT_OF_MEMORY);
					omrthread_monitor_enter(vm->classTableMutex);
					clazz = dynamicLoadBuffers->internalDefineClassFunction(currentThread, className, classNameLength,
							(U_8 *) buf, (UDATA) bufLen, NULL, classLoader, NULL, J9_FINDCLASS_FLAG_THROW_ON_FAIL, NULL, NULL, &localBuffer);
					if (currentThread->privateFlags & J9_PRIVATE_FLAGS_CLOAD_NO_MEM) {
						setNativeOutOfMemoryError(currentThread, 0, 0);
					}
				}
			}
			
			result = (jclass)VM_VMHelpers::createLocalRef(env, J9VM_J9CLASS_TO_HEAPCLASS(clazz));
		}

		if (className != (U_8 *) name) {
			PORT_ACCESS_FROM_JAVAVM(vm);

			j9mem_free_memory(className);
		}
	}
done:
#else
	setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNOCLASSDEFFOUNDERROR, name);
#define result NULL
#endif
	VM_VMAccess::inlineExitVMToJNI(currentThread);

	return result;
}
#undef result


static void * JNICALL
getPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_jniGetPrimitiveArrayCritical(vmThread, array, isCopy);
}

static void JNICALL
releasePrimitiveArrayCritical(JNIEnv *env, jarray array, void * elems, jint mode)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_jniReleasePrimitiveArrayCritical(vmThread, array, elems, mode);
}

static const jchar * JNICALL
getStringCritical(JNIEnv *env, jstring str, jboolean *isCopy)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	return vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_jniGetStringCritical(vmThread, str, isCopy);
}

static void JNICALL
releaseStringCritical(JNIEnv *env, jstring str, const jchar * elems)
{
	J9VMThread* vmThread = (J9VMThread*)env;
	vmThread->javaVM->memoryManagerFunctions->j9gc_objaccess_jniReleaseStringCritical(vmThread, str, elems);
}

#endif /* !defined(J9VM_INTERP_MINIMAL_JNI) */

} /* extern "C" */

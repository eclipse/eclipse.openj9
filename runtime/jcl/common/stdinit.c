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

#include <string.h>

#include "jcl.h"
#include "j9consts.h"
#include "jni.h"
#include "j9protos.h"
#include "j9cp.h"
#include "jvminit.h"
#include "j9jclnls.h"
#include "j2sever.h"
#include "jclprots.h"
#include "util_api.h"

#define _UTE_STATIC_
#include "ut_j9jcl.h"

/* this file is owned by the VM-team.  Please do not modify it without consulting the VM team */

static IDATA computeFinalBootstrapClassPath(J9JavaVM * vm);
static IDATA computeBootstrapClassPathAppend(J9JavaVM * vm);
static UDATA isEndorsedBundle(const char *filename);
static IDATA initializeBootstrapClassPath(J9JavaVM * vm);
static char * addEndorsedBundles(J9PortLibrary *portLib, char *endorsedDir, char *path);
static jint initializeBootClassPathSystemProperty( J9JavaVM *vm);
static IDATA initializeSystemThreadGroup(J9JavaVM * vm, JNIEnv * env);
static char * addEndorsedPath(J9PortLibrary *portLib, char *endorsedPath, char *path);

/* JCL_J2SE */
#define JCL_J2SE

#define BOOT_PATH_SEPARATOR_SYS_PROP "path.separator"

#define JAVA_ENDORSED_DIRS_PROP "java.endorsed.dirs"

static J9Class jclFakeClass;

/**
 * Compute the JCL runtime flags to be used during the initialization of
 * known classes.
 * @param vm The JavaVM structure
 * @return Runtime flags which indicate what classes should be loaded.
 */
U_32
computeJCLRuntimeFlags(J9JavaVM *vm)
{
	U_32 flags = JCL_RTFLAG_DEFAULT;

#ifdef J9VM_INTERP_HOT_CODE_REPLACEMENT
	flags |= JCL_RTFLAG_INTERP_HOT_CODE_REPLACEMENT;
#endif /* J9VM_INTERP_HOT_CODE_REPLACEMENT */

#ifdef J9VM_OPT_METHOD_HANDLE
	flags |= JCL_RTFLAG_OPT_METHOD_HANDLE;
#endif /* J9VM_OPT_METHOD_HANDLE */

#ifdef J9VM_OPT_MODULE
	if (J2SE_VERSION(vm) >= J2SE_V11) {
		flags |= JCL_RTFLAG_OPT_MODULE;
	}
#endif /* J9VM_OPT_MODULE */

#ifdef J9VM_OPT_REFLECT
	flags |= JCL_RTFLAG_OPT_REFLECT;
#endif /* J9VM_OPT_REFLECT */

#ifdef J9VM_OPT_METHOD_HANDLE_COMMON
	flags |= JCL_RTFLAG_OPT_METHOD_HANDLE_COMMON;
#endif /* J9VM_OPT_METHOD_HANDLE_COMMON */

#ifdef J9VM_OPT_OPENJDK_METHODHANDLE
	flags |= JCL_RTFLAG_OPT_OPENJDK_METHODHANDLE;
#endif /* J9VM_OPT_OPENJDK_METHODHANDLE */

	return flags;
}

jint
standardInit( J9JavaVM *vm, char *dllName)
{
	jint result = 0;
	J9VMThread *vmThread = vm->mainThread;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ConstantPool *jclConstantPool = (J9ConstantPool *) vm->jclConstantPool;
	extern J9ROMClass *jclROMClass;
	jclass clazz = NULL;
	J9NativeLibrary *javaLibHandle = NULL;
	char *threadName = NULL;
	jobject threadGroup = NULL;

	/* Register this module with trace */
	UT_MODULE_LOADED(J9_UTINTERFACE_FROM_VM(vm));
	Trc_JCL_VMInitStages_Event1(vmThread);

	TOC_STORE_TOC(vm->jclTOC, standardInit);

	jclFakeClass.romClass = jclROMClass;
	jclConstantPool->ramClass = &jclFakeClass;
	jclConstantPool->romConstantPool = J9_ROM_CP_FROM_ROM_CLASS(jclROMClass);

#ifdef J9VM_OPT_DYNAMIC_LOAD_SUPPORT
	if (J2SE_VERSION(vm) < J2SE_V11) {
		/* Process the command-line bootpath additions/modifications */
		if (computeFinalBootstrapClassPath(vm)) {
			goto _fail;
		}
	} else {
		if (computeBootstrapClassPathAppend(vm)) {
			goto _fail;
		}
	}
	/* Now create the classPathEntries */
	if (initializeBootstrapClassPath(vm)) {
		goto _fail;
	}
#endif

	/* Now compute the final java.fullversion and java.vm.info properties */
	if (computeFullVersionString(vm)) {
		goto _fail;
	}

	/* Add the correct "zip" natives to the path:
	 * Configuration	Load Zip
	 *  Java_6			yes
	 *  Java_7			yes
	 * Both SHAPE_SUN (>1.6) & SHAPE_ORACLE (to boot the ext classloader) need zip.
	 */
#ifdef J9VM_OPT_SIDECAR
#if !defined(J9VM_INTERP_MINIMAL_JCL)
	{
		UDATA handle = 0;
		result = (jint)vmFuncs->registerBootstrapLibrary(vm->mainThread, "zip", (J9NativeLibrary **)&handle, FALSE);
	}
#endif /* !J9VM_INTERP_MINIMAL_JCL */
#endif /* J9VM_OPT_SIDECAR */

	if (JNI_OK == result) {
		vmFuncs->internalAcquireVMAccess(vmThread);
		
		result = (jint)initializeRequiredClasses(vmThread, dllName);
		
		if (JNI_OK == result) {
			result = vmFuncs->initializeHeapOOMMessage(vmThread);
		}
		
		if (JNI_OK == result) {
			U_32 runtimeFlags = computeJCLRuntimeFlags(vm);
			result = initializeKnownClasses(vm, runtimeFlags);
		}
		
		if (JNI_OK == result) {
			IDATA continueInitialization = TRUE;
			/* Must do this before initializeAttachedThread */
			vmFuncs->internalReleaseVMAccess(vmThread);

			TRIGGER_J9HOOK_VM_INITIALIZE_REQUIRED_CLASSES_DONE(vm->hookInterface, vmThread, continueInitialization);
			if (!continueInitialization) {
				goto _fail;
			}

			result = (jint)initializeSystemThreadGroup(vm, (JNIEnv *)vmThread);
			if (JNI_OK != result) {
				goto _fail;
			} else {
#if JAVA_SPEC_VERSION >= 15
				JNIEnv *env = (JNIEnv *)vmThread;
				jclass clz = (*env)->FindClass(env, "jdk/internal/loader/NativeLibraries");
				jmethodID mid = NULL;
				if (NULL == clz) {
					goto _fail;
				}
				mid = (*env)->GetStaticMethodID(env, clz, "load", "(Ljdk/internal/loader/NativeLibraries$NativeLibraryImpl;Ljava/lang/String;ZZ)Z");
				if (NULL == mid) {
					goto _fail;
				}
				vm->nativeLibrariesLoadMethodID = (UDATA) mid;
				Trc_JCL_init_nativeLibrariesLoadMethodID(vmThread, vm->nativeLibrariesLoadMethodID);
#endif /* JAVA_SPEC_VERSION >= 15 */
			}

	#if defined(J9VM_INTERP_ATOMIC_FREE_JNI)
			vmFuncs->internalEnterVMFromJNI(vmThread);
			vmFuncs->internalReleaseVMAccess(vmThread);
	#endif /* J9VM_INTERP_ATOMIC_FREE_JNI */
			vmFuncs->initializeAttachedThread(vmThread, threadName, (j9object_t *)threadGroup, FALSE, vmThread);

			vmFuncs->internalAcquireVMAccess(vmThread);

			if ((NULL != vmThread->currentException) || (NULL == vmThread->threadObject)) {
				result = JNI_ERR;
			} else {
				vmFuncs->internalFindKnownClass(vmThread,
					J9VMCONSTANTPOOL_JAVALANGTHREADDEATH,
					J9_FINDKNOWNCLASS_FLAG_INITIALIZE | J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
				if (vmThread->currentException) {
					result = JNI_ERR;
				}
			}
		}
		vmFuncs->internalReleaseVMAccess(vmThread);
	}
	
	if (JNI_OK != result) {
		goto _fail;
	}

	internalInitializeJavaLangClassLoader((JNIEnv*)vmThread);
	if (vmThread->currentException) goto _fail;

	if (J2SE_VERSION(vm) >= J2SE_V11) {
		result = registerJdkInternalReflectConstantPoolNatives((JNIEnv*)vmThread);
		if (JNI_OK != result) {
			fprintf(stderr, "Failed to register natives for jdk.internal.reflect.ConstantPool\n");
			goto _fail;
		}
	}

#ifdef J9VM_OPT_REFLECT
	if (vm->reflectFunctions.idToReflectMethod) {
		jmethodID invokeMethod;

		clazz = (*(JNIEnv*)vmThread)->FindClass((JNIEnv*)vmThread, "java/lang/reflect/Method");
		if (!clazz) goto _fail;
		invokeMethod = (*(JNIEnv*)vmThread)->GetMethodID((JNIEnv*)vmThread, clazz, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
		if (!invokeMethod) goto _fail;
		vm->jlrMethodInvoke = ((J9JNIMethodID *) invokeMethod)->method;
		(*(JNIEnv*)vmThread)->DeleteLocalRef((JNIEnv*)vmThread, clazz);

#ifndef J9VM_IVE_RAW_BUILD /* J9VM_IVE_RAW_BUILD is not enabled by default */
		/* JSR 292-related class */
		clazz = (*(JNIEnv*)vmThread)->FindClass((JNIEnv*)vmThread, "com/ibm/oti/lang/ArgumentHelper");
		if (!clazz) goto _fail;
		vm->jliArgumentHelper = (*(JNIEnv*)vmThread)->NewGlobalRef((JNIEnv*)vmThread, clazz);
		if (!vm->jliArgumentHelper) goto _fail;
		(*(JNIEnv*)vmThread)->DeleteLocalRef((JNIEnv*)vmThread, clazz);

		clazz = (*(JNIEnv*)vmThread)->FindClass((JNIEnv*)vmThread, "java/lang/invoke/MethodHandle");
		if (!clazz) goto _fail;
		invokeMethod = (*(JNIEnv*)vmThread)->GetMethodID((JNIEnv*)vmThread, clazz, "invokeWithArguments", "([Ljava/lang/Object;)Ljava/lang/Object;");
		if (!invokeMethod) goto _fail;
		vm->jliMethodHandleInvokeWithArgs = ((J9JNIMethodID *) invokeMethod)->method;
		invokeMethod = (*(JNIEnv*)vmThread)->GetMethodID((JNIEnv*)vmThread, clazz, "invokeWithArguments", "(Ljava/util/List;)Ljava/lang/Object;");
		if (!invokeMethod) goto _fail;
		vm->jliMethodHandleInvokeWithArgsList = ((J9JNIMethodID *) invokeMethod)->method;
		(*(JNIEnv*)vmThread)->DeleteLocalRef((JNIEnv*)vmThread, clazz);
		clazz = (*(JNIEnv*)vmThread)->FindClass((JNIEnv*)vmThread, "com/ibm/jit/JITHelpers");
		if (NULL != clazz) {
			/* Force class initialization */
			(void)(*(JNIEnv*)vmThread)->GetStaticFieldID((JNIEnv*)vmThread, clazz, "helpers", "Lcom/ibm/jit/JITHelpers;");
			(*(JNIEnv*)vmThread)->DeleteLocalRef((JNIEnv*)vmThread, clazz);
		}
#endif /* !J9VM_IVE_RAW_BUILD */
	}
#endif

#ifdef J9VM_INTERP_SIG_QUIT_THREAD
	result = J9SigQuitStartup(vm);
	if (JNI_OK != result) {
		goto _fail;
	}
#endif

	/*
	 * We need to initialize the methodID caches for String.<init> and String.getBytes(String) while we are still in single threaded cade.
	 * The JCL natives that initialize this are not thread safe and we run the risk of invoking these methods with a NULL methodID.
	 * This code is a work around that forces the methodID cache initialization code to be run.
	 */
#if defined(J9VM_INTERP_ATOMIC_FREE_JNI)
	/* Ensure VM access is released when calling registerBootstrapLibrary */
	vmFuncs->internalEnterVMFromJNI(vmThread);
	vmFuncs->internalReleaseVMAccess(vmThread);
#endif /* J9VM_INTERP_ATOMIC_FREE_JNI */
	if (0 == vmFuncs->registerBootstrapLibrary(vmThread, "java", &javaLibHandle, 0)) {
		jstring (JNICALL *nativeFuncAddr)(JNIEnv *env, const char *str) = NULL;
		PORT_ACCESS_FROM_JAVAVM(vm);
		j9sl_lookup_name(javaLibHandle->handle, "JNU_NewStringPlatform", (UDATA*)&nativeFuncAddr, "LLL");
		nativeFuncAddr((JNIEnv*)vmThread, "");
	}

	return JNI_OK;

_fail:
	return JNI_ERR;
}

/**
  * Set up any information that might be consumed/modified by other libraries before classes are loaded.
  *
  */
jint
standardPreconfigure(JavaVM *jvm)
{
	J9JavaVM* vm = (J9JavaVM*)jvm;

#ifdef J9VM_OPT_DYNAMIC_LOAD_SUPPORT
	if (J2SE_VERSION(vm) < J2SE_V11) {
		/* Now, based on java.home we can compute the default bootpath */
		if (initializeBootClassPathSystemProperty(vm)) {
			goto _fail;
		}
	}
#endif
	return JNI_OK;

_fail:
	JCL_OnUnload(vm, NULL);
	return JNI_ERR;
}

static IDATA
initializeSystemThreadGroup(J9JavaVM *vm, JNIEnv *env)
{
	IDATA result = JNI_ERR;
	jclass threadClass = NULL;
	jclass threadGroupClass = NULL;
	jobject systemThreadGroupGlobalRef;
	jfieldID nameField;
	jfieldID systemThreadGroupField = NULL;
	jmethodID constructorID;
	jobject threadGroupRef = NULL;
	jobject groupNameRef = NULL;

	threadGroupClass = (*env)->FindClass(env, "java/lang/ThreadGroup");
	if (!threadGroupClass) goto done;

	/* Force resolution of known class */
	J9VMJAVALANGTHREADGROUP(vm);

	constructorID = (*env)->GetMethodID(env, threadGroupClass, "<init>", "()V");
	if (!constructorID) goto done;
	threadGroupRef = (*env)->NewObject(env, threadGroupClass, constructorID);
	if (!threadGroupRef) goto done;
	nameField = (*env)->GetFieldID(env, threadGroupClass, "name", "Ljava/lang/String;");
	if (!nameField) goto done;
	groupNameRef = (*env)->NewStringUTF(env, "system");
	if (!groupNameRef) goto done;
	(*env)->SetObjectField(env, threadGroupRef, nameField, groupNameRef);
	if ((*env)->ExceptionCheck(env)) goto done;

	threadClass = (*env)->FindClass(env, "java/lang/Thread");
	if (!threadClass) goto done;
	systemThreadGroupField = (*env)->GetStaticFieldID(env, threadClass, "systemThreadGroup", "Ljava/lang/ThreadGroup;");
	if (!systemThreadGroupField) goto done;

	(*env)->SetStaticObjectField(env, threadClass, systemThreadGroupField, threadGroupRef);
	if ((*env)->ExceptionCheck(env)) goto done;

	systemThreadGroupGlobalRef = (*env)->NewGlobalRef(env, threadGroupRef);
	if (!systemThreadGroupGlobalRef) goto done;
	vm->systemThreadGroupRef = (j9object_t*) systemThreadGroupGlobalRef;
	result = JNI_OK;
done:
	if (threadGroupClass) (*env)->DeleteLocalRef(env, threadGroupClass);
	if (threadClass) (*env)->DeleteLocalRef(env, threadClass);
	if (threadGroupRef) (*env)->DeleteLocalRef(env, threadGroupRef);
	if (groupNameRef) (*env)->DeleteLocalRef(env, groupNameRef);
	return result;
}

void
internalInitializeJavaLangClassLoader(JNIEnv * env)
{
	J9VMThread* vmThread = (J9VMThread*) env;
	J9JavaVM* vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jfieldID appClassLoaderField;
	jobject appClassLoader = NULL;
	jclass jlClassLoader = NULL;

	jlClassLoader = (*env)->FindClass(env, "java/lang/ClassLoader");
	if ((*env)->ExceptionCheck(env)) {
		goto done;
	}

	appClassLoaderField = (*env)->GetStaticFieldID(env, jlClassLoader, "applicationClassLoader", "Ljava/lang/ClassLoader;");
	if ((*env)->ExceptionCheck(env)) {
		goto done;
	}

	appClassLoader = (*env)->GetStaticObjectField(env, jlClassLoader, appClassLoaderField);
	if ((*env)->ExceptionCheck(env)) {
		goto done;
	}

	vmFuncs->internalEnterVMFromJNI(vmThread);

	vm->applicationClassLoader = J9VMJAVALANGCLASSLOADER_VMREF(vmThread, J9_JNI_UNWRAP_REFERENCE(appClassLoader));

	if (NULL == vm->applicationClassLoader) {
		/* CMVC 201518
		 * applicationClassLoader may be null due to lazy classloader initialization. Initialize
		 * the applicationClassLoader now or vm will start throwing NoClassDefFoundException.
		 */
		vm->applicationClassLoader = (void*) (UDATA)(vmFuncs->internalAllocateClassLoader(vm, J9_JNI_UNWRAP_REFERENCE(appClassLoader)));
		if (NULL != vmThread->currentException) {
			/* while this exception check and return statement seem un-necessary, it is added to prevent
			 * oversights if anybody adds more code in the future.
			 */
			goto exitVM;
		}
	}

	/* Set up extension class loader in VM */
	if (NULL == vm->extensionClassLoader) {
		j9object_t classLoaderObject = vm->applicationClassLoader->classLoaderObject;
		j9object_t classLoaderParentObject = classLoaderObject;

		while (NULL != classLoaderParentObject) {
			classLoaderObject = classLoaderParentObject;
			classLoaderParentObject = J9VMJAVALANGCLASSLOADER_PARENT(vmThread, classLoaderObject);
		}

		vm->extensionClassLoader = J9VMJAVALANGCLASSLOADER_VMREF(vmThread, classLoaderObject);

		if (NULL == vm->extensionClassLoader) {
			vm->extensionClassLoader = (void*) (UDATA)(vmFuncs->internalAllocateClassLoader(vm, classLoaderObject));
			if (NULL != vmThread->currentException) {
				/* while this exception check and return statement seem un-necessary, it is added to prevent
				 * oversights if anybody adds more code in the future.
				 */
				goto exitVM;
			}
		}
	}
exitVM:
	vmFuncs->internalExitVMToJNI(vmThread);
done: ;
}

jint
JNICALL JVM_OnUnload(JavaVM *jvm, void *reserved)
{
	return 0;
}

jint
JCL_OnUnload(J9JavaVM *vm, void *reserved)
{
#ifdef J9VM_OPT_DYNAMIC_LOAD_SUPPORT
	PORT_ACCESS_FROM_JAVAVM(vm);
	if (vm->bootstrapClassPath) {
		j9mem_free_memory(vm->bootstrapClassPath);
		vm->bootstrapClassPath = NULL;
	}
#endif

	return 0;
}

IDATA
checkJCL(J9VMThread *vmThread, U_8 *dllValue, U_8 *jclConfig, UDATA j9Version, UDATA jclVersion)
{
	J9JavaVM * vm = vmThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	char jclName[9];
	UDATA j9V, jclV;

	/* If jclConfig is NULL or jclVersion is -1, then we didn't find the fields in java.lang.Class. Make sure the dllValue and jclConfig match. */
	if ((jclConfig == NULL) || (jclVersion == (UDATA)-1) || (memcmp(jclConfig, dllValue, 8))) {
		/* Incompatible class library */
		j9nls_printf(PORTLIB, J9NLS_ERROR | J9NLS_BEGIN_MULTI_LINE, J9NLS_JCL_INCOMPATIBLE_CL);
		if (jclConfig != NULL) {
#ifdef J9VM_ENV_LITTLE_ENDIAN
			jclName [0] = jclConfig[7];
			jclName [1] = jclConfig[6];
			jclName [2] = jclConfig[5];
			jclName [3] = jclConfig[4];
			jclName [4] = jclConfig[3];
			jclName [5] = jclConfig[2];
			jclName [6] = jclConfig[1];
			jclName [7] = jclConfig[0];
#else
			jclName [0] = jclConfig[0];
			jclName [1] = jclConfig[1];
			jclName [2] = jclConfig[2];
			jclName [3] = jclConfig[3];
			jclName [4] = jclConfig[4];
			jclName [5] = jclConfig[5];
			jclName [6] = jclConfig[6];
			jclName [7] = jclConfig[7];
#endif
			jclName [8] = '\0';
			/* Try running with -jcl:%s */
			j9nls_printf(PORTLIB, J9NLS_INFO | J9NLS_END_MULTI_LINE, J9NLS_JCL_TRY_JCL, jclName);
			return 2;
		} else {
			/* Try running with -jcl:%s */
			j9nls_printf(PORTLIB, J9NLS_INFO | J9NLS_END_MULTI_LINE, J9NLS_JCL_NOTJ9);
			return 1;
		}
	}

	/* Last, compare the versions */
	if((jclV = jclVersion & 0xffff) != (j9V = j9Version & 0xffff)) {
		/* Incompatible class library version: JCL %x, VM %x */
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JCL_INCOMPATIBLE_CL_VERSION, jclV, j9V);
		return 3;
	}
	if((jclV = jclVersion & 0xff0000) < (j9V = j9Version & 0xff0000)) {
		/* Incompatible class library version: expected JCL v%i, found v%i */
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JCL_INCOMPATIBLE_CL_VERSION_JCL, j9V >> 16, jclV >> 16);
		return 4;
	}
	if((jclV = jclVersion & 0xff000000) > (j9V = j9Version & 0xff000000)) {
		/* Incompatible class library version: requires VM v%i, found v%i */
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_JCL_INCOMPATIBLE_CL_VERSION_VM, jclV >> 24, j9V >> 24);
		return 5;
	}

	/* Good to go! */
	return 0;
}

#ifdef J9VM_OPT_DYNAMIC_LOAD_SUPPORT

static IDATA
initializeBootstrapClassPath(J9JavaVM *vm)
{
	VMI_ACCESS_FROM_JAVAVM((JavaVM*)vm);
	J9InternalVMFunctions const * const vmFuncs = vm->internalVMFunctions;
	char* path;
	char* classpathSeparator;
	J9ClassLoader *loader = vm->systemClassLoader;
	BOOLEAN initClassPathEntry = FALSE;

	/* Get the BP from the VM sysprop */
	if (J2SE_VERSION(vm) < J2SE_V11) {
		(*VMI)->GetSystemProperty(VMI, BOOT_PATH_SYS_PROP, &path);
	} else {
		(*VMI)->GetSystemProperty(VMI, BOOT_CLASS_PATH_APPEND_PROP, &path);
	}
	(*VMI)->GetSystemProperty(VMI, BOOT_PATH_SEPARATOR_SYS_PROP, &classpathSeparator);

	/* Fail if the classpath has already been set */
	if (J9_ARE_ALL_BITS_SET(loader->flags, J9CLASSLOADER_CLASSPATH_SET)) {
		return -2;
	}

#if defined(J9VM_OPT_SHARED_CLASSES)
	if (J9_ARE_ALL_BITS_SET(loader->flags, J9CLASSLOADER_SHARED_CLASSES_ENABLED)) {
		/* Warm up the classpath entry so that the Classpath stored in the cache has the correct info.
		 * This is required because when we are finding classes in the cache, initializeClassPathEntry
		 * is not called
		 */
		initClassPathEntry = TRUE;
	}
#endif
	loader->classPathEntryCount = vmFuncs->initializeClassPath(vm, path, classpathSeparator[0], CPE_FLAG_BOOTSTRAP, initClassPathEntry, &loader->classPathEntries);

	if (-1 == (IDATA)loader->classPathEntryCount) {
		return -1;
	} else {
		/* Mark the class path as having been set */
		loader->flags |= J9CLASSLOADER_CLASSPATH_SET;

		TRIGGER_J9HOOK_VM_CLASS_LOADER_CLASSPATH_ENTRIES_INITIALIZED(vm->hookInterface, vm, loader);
	}

	return 0;
}

static UDATA
isEndorsedBundle(const char *filename)
{
	size_t len = strlen(filename);

	if (len > 4) {
		char suffix[4];

		suffix[0] = j9_ascii_tolower(filename[len - 4]);
		suffix[1] = j9_ascii_tolower(filename[len - 3]);
		suffix[2] = j9_ascii_tolower(filename[len - 2]);
		suffix[3] = j9_ascii_tolower(filename[len - 1]);

		if (strncmp(suffix, ".jar", 4) == 0) {
			return 1;
		} else if (strncmp(suffix, ".zip", 4) == 0) {
			return 1;
		}
	}

	return 0;
}

static char *
addEndorsedPath(J9PortLibrary *portLib, char *endorsedPath, char *path)
{
	PORT_ACCESS_FROM_PORT(portLib);

	char separator = (char) j9sysinfo_get_classpathSeparator();
	char *dirStart = endorsedPath;
	char *dirEnd = dirStart;
	char endorsedDir[EsMaxPath];

	for (;;) {
		size_t dirLen = 0;
		/* break path into separate directories */
		dirEnd = strchr(dirEnd, separator);

		dirLen = (NULL != dirEnd) ? (dirEnd - dirStart) : strlen(dirStart);
		dirLen = OMR_MIN(dirLen, EsMaxPath - 2);

		/* ignore empty paths */
		if (dirLen > 0) {
			/* ensure there's a '/' at the end of the directory */
			strncpy(endorsedDir, dirStart, dirLen);
			if (('/' != endorsedDir[dirLen - 1]) && ('\\' != endorsedDir[dirLen - 1])) {
				endorsedDir[dirLen] = DIR_SEPARATOR;
				dirLen += 1;
			}
			endorsedDir[dirLen] = '\0';

			path = addEndorsedBundles(portLib, endorsedDir, path);
			if (NULL == path) {
				break;
			}
		}
		if (NULL == dirEnd) {
			break;
		}

		dirEnd += 1;
		dirStart = dirEnd;
	}

	return path;
}

static char *
addEndorsedBundles(J9PortLibrary *portLib, char *endorsedDir, char *path)
{
	PORT_ACCESS_FROM_PORT(portLib);
	UDATA findHandle = 0;
	char endorsedBundle[EsMaxPath];
	char *bundleName = endorsedBundle + strlen(endorsedDir);

	strcpy(endorsedBundle, endorsedDir);

	findHandle = j9file_findfirst(endorsedDir, bundleName);

	if ((UDATA)-1 != findHandle) {
		I_32 findIndex = 0;

		while ((NULL != path) && (findIndex >= 0)) {
			/* prepend any Jar or Zip bundles to the bootclasspath */
			if (isEndorsedBundle(endorsedBundle)) {
				char *oldPath = path;
				path = catPaths(PORTLIB, endorsedBundle, path);
				j9mem_free_memory(oldPath);
			}
			findIndex = j9file_findnext(findHandle, bundleName);
		}
		j9file_findclose(findHandle);
	}

	return path;
}

/**
  * Initialize the BOOT_PATH_SYS_PROP (sun.boot.class.path) system property by scanning the vm args and extracting
  * the value of any -Xbootclasspath: argument.  If no bootpath is explicitly specified then use a default
  * value appropriate for this class library.
  *
  * @return  zero on success, non-zero on failure.
  *
  * @note Assumes that the VMI functions are available, and sysprops have been allocated.
  * @note Requires that the 'java.home' system property be available.
  */
static jint
initializeBootClassPathSystemProperty(J9JavaVM *vm)
{
	VMI_ACCESS_FROM_JAVAVM((JavaVM*)vm);
	PORT_ACCESS_FROM_JAVAVM(vm);

	JavaVMInitArgs *args = (*VMI)->GetInitArgs(VMI);
	char bpOption[] = "-Xbootclasspath:";
	int bpOptionLength = sizeof(bpOption) - 1;
	char* bp = NULL;
	int i, freeBP = 0;
	jint rc = 0;
	vmiError vmiRC;

	/* Scan for the explicit bootpath override */
	for (i = 0; i < args->nOptions; i++) {
		if (strncmp(args->options[i].optionString, bpOption, bpOptionLength) == 0) {
			bp = &(args->options[i].optionString[bpOptionLength]);

			/* empty bootclasspath means use default */
			if (*bp == 0) {
				bp = NULL;
			}
		}
	}

	/* If no explicit (or empty) bootpath then ask the library for a default */
	if (bp == NULL) {
		char* javaHome;
		char* currentBootpath;
		vmiError rcGetProp;

		/* If the bootpath already exists (i.e. it was set using a -D in the startup options), use that one */
		rcGetProp = (*VMI)->GetSystemProperty(VMI, BOOT_PATH_SYS_PROP, &currentBootpath);
		if (rcGetProp != VMI_ERROR_NONE) {
			return -2;
		}
		if (currentBootpath[0] != '\0') {
			return 0;
		}

		/* Entries are relative to java.home */
		rcGetProp = (*VMI)->GetSystemProperty(VMI, "java.home", &javaHome);
		if (rcGetProp != VMI_ERROR_NONE) {
			return -2;
		}

		/* Add the 'standard' collection of jars */
		bp = getDefaultBootstrapClassPath((J9JavaVM*)vm, javaHome);
		if (!bp ) {
			return -1;
		}

		/* Remember that the bp must be freed */
		freeBP = 1;
	}

	/* Set the system property */
	vmiRC = (*VMI)->SetSystemProperty(VMI, BOOT_PATH_SYS_PROP, bp);
	if (vmiRC != VMI_ERROR_NONE) {
		rc = -3;
	}

	/* Free the memory if necessary */
	if (freeBP) {
		j9mem_free_memory(bp);
	}

	return rc;
}

/**
 * Append paths specified using -Xbootclasspath/a option to bootclasspath,
 * and sets BOOT_CLASS_PATH_APPEND_PROP ("jdk.boot.class.path.append") system property.
 *
 * @param [in] vm the JavaVM
 *
 * @return 0 On success, non-zero otherwise.
 */
static IDATA
computeBootstrapClassPathAppend(J9JavaVM *vm)
{
	I_32 i = 0;
	IDATA rc = 0;
	char *path = NULL;
	char *bpAppend = NULL;
	char *bootPath = NULL;
	JavaVMInitArgs *args = NULL;
	vmiError vmiRC = VMI_ERROR_NONE;
	VMI_ACCESS_FROM_JAVAVM((JavaVM*)vm);
	PORT_ACCESS_FROM_JAVAVM(vm);

	args = (*VMI)->GetInitArgs(VMI);

	vmiRC = (*VMI)->GetSystemProperty(VMI, BOOT_CLASS_PATH_APPEND_PROP, &bootPath);
	if (vmiRC != VMI_ERROR_NONE) {
		rc = -1;
		goto _end;
	}

	/* Copy the base bootPath so that we're always dealing with portlib allocated memory */
	path = j9mem_allocate_memory(strlen(bootPath) + 1, J9MEM_CATEGORY_VM_JCL);
	if (NULL == path) {
		rc = -2;
		goto _end;
	}
	strcpy(path, bootPath);

	/* Look for and add the append bootclasspath options */
	for (i = 0; i < args->nOptions; i++) {
		if (strncmp(args->options[i].optionString, VMOPT_XBOOTCLASSPATH_A_COLON, sizeof(VMOPT_XBOOTCLASSPATH_A_COLON) - 1) == 0) {
			char *value = &(args->options[i].optionString[sizeof(VMOPT_XBOOTCLASSPATH_A_COLON) - 1]);

			if (NULL == bpAppend) {
				bpAppend = j9mem_allocate_memory(strlen(value) + 1, J9MEM_CATEGORY_VM_JCL);
				if (NULL == bpAppend) {
					j9mem_free_memory(path);
					rc = -3;
					goto _end;
				}
				strcpy(bpAppend, value);
			} else {
				char* oldValue = bpAppend;

				bpAppend = catPaths(PORTLIB, bpAppend, value);
				j9mem_free_memory(oldValue);
				if (NULL == bpAppend) {
					j9mem_free_memory(path);
					rc = -4;
					goto _end;
				}
			}
		}
	}

	/* Update the VM sysprop */
	if (NULL != bpAppend) {
		char *oldPath = path;
		path = catPaths(PORTLIB, path, bpAppend);
		j9mem_free_memory(oldPath);
	}

	/* Cache the path in the VM */
	vm->bootstrapClassPath = (U_8 *)path;
	vmiRC = (*VMI)->SetSystemProperty(VMI, BOOT_CLASS_PATH_APPEND_PROP, path);
	if (vmiRC != VMI_ERROR_NONE) {
		rc = -5;
		goto _end;
	}

_end:
	return rc;
}

/**
  * Computes the final bootstrap class path by appending / prepending jars specified
  * on either the command-line or via JVMTI.  Also handles the endorsed directory.
  *
  * @return 0 On success, non-zero otherwise.
  *
  * @note Requires that the 'java.home' system property has been set in the VMI.
  */
static IDATA
computeFinalBootstrapClassPath(J9JavaVM *vm)
{
	VMI_ACCESS_FROM_JAVAVM((JavaVM*)vm);
	PORT_ACCESS_FROM_JAVAVM(vm);

	char* path, *javaHome = NULL, *bootPath = NULL, *endorsedPath = NULL;
	int i;
	JavaVMInitArgs	*args = (*VMI)->GetInitArgs(VMI);
	vmiError vmiRC;

#define PRIV_J9_BPA_OPTION "-Xbootclasspath/a:"
#define PRIV_J9_BPA_OPTION_LEN 18  /* strlen() */
#define PRIV_J9_BPP_OPTION "-Xbootclasspath/p:"
#define PRIV_J9_BPP_OPTION_LEN 18  /* strlen() */

#define PRIV_J9_IJB_OPTION "-Dibm.jvm.bootclasspath="
#define PRIV_J9_IJB_OPTION_LEN 24  /* strlen() */

	/* Fetch java.home and cache it in the JavaVM struct */
	vmiRC = (*VMI)->GetSystemProperty(VMI, "java.home", &javaHome);
	if (vmiRC != VMI_ERROR_NONE) {
		return -1;
	}

	/* Fetch the java.endorsed.dirs path */
	vmiRC = (*VMI)->GetSystemProperty(VMI, JAVA_ENDORSED_DIRS_PROP, &endorsedPath);
	if (vmiRC != VMI_ERROR_NONE) {
		return -13;
	}

	/* Point bootPath at the system property */
	vmiRC = (*VMI)->GetSystemProperty(VMI, BOOT_PATH_SYS_PROP, &bootPath);
	if (vmiRC != VMI_ERROR_NONE) {
		return -2;
	}

	/* Copy the base bootPath so that we're always dealing with portlib allocated memory */
	path = j9mem_allocate_memory(strlen(bootPath) + 1, J9MEM_CATEGORY_VM_JCL);
	if (path == NULL) {
		return -3;
	}
	strcpy(path, bootPath);

/* -Dibm.jvm.bootclasspath (if it exists) should be prepended to the bootclasspath, but should appear after
	-Xbootclasspath/p: if specified. So scan for -Dibm.jvm.bootclasspath first. */
#ifdef JCL_J2SE
	for (i = 0; i < args->nOptions; i++) {
		if (strncmp(args->options[i].optionString, PRIV_J9_IJB_OPTION, PRIV_J9_IJB_OPTION_LEN) == 0) {
			char* oldPath = path;
			path = catPaths(PORTLIB, &(args->options[i].optionString[PRIV_J9_IJB_OPTION_LEN]), path);
			j9mem_free_memory(oldPath);
			if (path == NULL) {
				return -4;
			}
		}
	}
#endif

	/* Look for and add the prepend and append bootclasspath options */
	for (i = 0; i < args->nOptions; i++) {
		if (strncmp(args->options[i].optionString, PRIV_J9_BPA_OPTION, PRIV_J9_BPA_OPTION_LEN) == 0) {
			char* oldPath = path;
			path = catPaths(PORTLIB, path, &(args->options[i].optionString[PRIV_J9_BPA_OPTION_LEN]));
			j9mem_free_memory(oldPath);
			if (path == NULL) {
				return -5;
			}
		} else if (strncmp(args->options[i].optionString, PRIV_J9_BPP_OPTION, PRIV_J9_BPP_OPTION_LEN) == 0) {
			char* oldPath = path;
			path = catPaths(PORTLIB, &(args->options[i].optionString[PRIV_J9_BPP_OPTION_LEN]), path);
			j9mem_free_memory(oldPath);
			if (path == NULL) {
				return -6;
			}
		}
	}

	 /* Prepend any bundles found using the most recent -Djava.endorsed.dirs setting. */
	if (NULL != endorsedPath) {
		path = addEndorsedPath(PORTLIB, endorsedPath, path);
		if (path == NULL) {
			return -8;
		}
	}

	/* Cache the bp in the VM */
	vm->bootstrapClassPath = (U_8*)path;

	/* Update the VM sysprop */
	vmiRC = (*VMI)->SetSystemProperty(VMI, BOOT_PATH_SYS_PROP, path);
	if (vmiRC != VMI_ERROR_NONE) {
		return -11;
	}

	return 0;

#undef PRIV_J9_BP_OPTION
#undef PRIV_J9_BP_OPTION_LEN
#undef PRIV_J9_BPA_OPTION
#undef PRIV_J9_BPA_OPTION_LEN
#undef PRIV_J9_BPP_OPTION
#undef PRIV_J9_BPP_OPTION_LEN

#undef PRIV_J9_IJB_OPTION
#undef PRIV_J9_IJB_OPTION_LEN

#undef PRIV_J9_ENDORSED_OPTION
#undef PRIV_J9_ENDORSED_OPTION_LEN
}

#endif /* OPT_DYNAMIC_LOAD_SUPPORT */

/* Prototype properties helper */
jobject getPropertyList(JNIEnv *env);

jint
completeInitialization(J9JavaVM * vm)
{
	jint result = JNI_OK;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9VMThread *currentThread = vm->mainThread;
	
	vmFuncs->internalEnterVMFromJNI(currentThread);
	vmFuncs->sendCompleteInitialization(currentThread);
	vmFuncs->internalReleaseVMAccess(currentThread);
	
	if (NULL == currentThread->currentException) {
		/* ensure ClassLoader.applicationClassLoader updated via system property java.system.class.loader is updated in VM as well */
		internalInitializeJavaLangClassLoader((JNIEnv*)currentThread);
		if (NULL != currentThread->currentException) {
			result = JNI_ERR;
		}
	} else {
		result = JNI_ERR;
	}
	return result;
}

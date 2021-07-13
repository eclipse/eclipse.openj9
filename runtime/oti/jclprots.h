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

#ifndef JCLPROTS_H
#define JCLPROTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jcl.h"

jint initializeJCLSystemProperties(J9JavaVM * vm);

/************************************************************
 ** COMPONENT: BBjclNativesCommon
 ************************************************************/

void JNICALL Java_com_ibm_oti_vm_VM_releasePointerToNativeResourceImpl(JNIEnv *env, jobject recv, jlong pointer);
jlong JNICALL Java_com_ibm_oti_vm_VM_getPointerToNativeResourceJarImpl(JNIEnv *env, jobject recv, jstring jarPath, jstring resName, jintArray size);

/* BBjclNativesCommonShared*/
jint JNICALL 
Java_com_ibm_oti_shared_SharedClassAbstractHelper_initializeShareableClassloaderImpl (JNIEnv* env, jobject thisObj, jobject classloader);
jobject JNICALL
Java_com_ibm_oti_shared_SharedDataHelperImpl_storeSharedDataImpl (JNIEnv* env, jobject thisObj, jobject loaderObj, jint helperID, jstring tokenObj, jobject byteBufferInput);
jlong JNICALL 
Java_com_ibm_oti_shared_SharedClassStatistics_freeSpaceBytesImpl (JNIEnv* env, jobject thisObj);
jobject JNICALL
Java_com_ibm_oti_shared_SharedDataHelperImpl_findSharedDataImpl (JNIEnv* env, jobject thisObj, jint helperID, jstring tokenObj);
jlong JNICALL 
Java_com_ibm_oti_shared_SharedClassStatistics_maxSizeBytesImpl (JNIEnv* env, jobject thisObj);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_softmxBytesImpl(JNIEnv* env, jobject thisObj);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_minAotBytesImpl(JNIEnv* env, jobject thisObj);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_maxAotBytesImpl(JNIEnv* env, jobject thisObj);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_minJitDataBytesImpl(JNIEnv* env, jobject thisObj);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_maxJitDataBytesImpl(JNIEnv* env, jobject thisObj);
jboolean JNICALL 
Java_com_ibm_oti_shared_SharedAbstractHelper_getIsVerboseImpl (JNIEnv* env, jobject thisObj);
void JNICALL 
Java_com_ibm_oti_shared_SharedClassURLClasspathHelperImpl_notifyClasspathChange2 (JNIEnv* env, jobject thisObj, jobject classLoaderObj);
void JNICALL
Java_com_ibm_oti_shared_SharedClassURLClasspathHelperImpl_notifyClasspathChange3 (JNIEnv* env, jobject thisObj, jint helperID, jobject classLoaderObj, jobjectArray urlArrayObj, jint urlIndex, jint urlCount, jboolean isOpen);
jboolean JNICALL 
Java_com_ibm_oti_shared_SharedClassTokenHelperImpl_findSharedClassImpl2(JNIEnv* env, jobject thisObj, jint helperID, jstring classNameObj, jobject loaderObj, jstring tokenObj, jboolean doFind, jboolean doStore, jbyteArray romClassCookie);
jint JNICALL 
Java_com_ibm_oti_shared_SharedClassTokenHelperImpl_storeSharedClassImpl2(JNIEnv* env, jobject thisObj, jint helperID, jobject loaderObj, jstring tokenObj, jclass clazzObj, jbyteArray nativeFlags);
jint JNICALL 
Java_com_ibm_oti_shared_SharedClassURLClasspathHelperImpl_findSharedClassImpl2(JNIEnv* env, jobject thisObj, jint helperID, jstring partitionObj, jstring classNameObj, jobject loaderObj, jobjectArray urlArrayObj, jboolean doFind, jboolean doStore, jint urlCount, jint confirmedCount, jbyteArray romClassCookie);
jint JNICALL 
Java_com_ibm_oti_shared_SharedClassURLClasspathHelperImpl_storeSharedClassImpl2(JNIEnv* env, jobject thisObj, jint helperID, jstring partitionObj, jobject loaderObj, jobjectArray urlArrayObj, jint urlCount, jint cpLoadIndex, jclass clazzObj, jbyteArray nativeFlags);
jboolean JNICALL 
Java_com_ibm_oti_shared_SharedClassURLHelperImpl_findSharedClassImpl3(JNIEnv* env, jobject thisObj, jint helperID, jstring partitionObj, jstring classNameObj, jobject loaderObj, jobject urlObj, jboolean doFind, jboolean doStore, jbyteArray romClassCookie, jboolean newJarFile, jboolean minimizeUpdateChecks);
jint JNICALL 
Java_com_ibm_oti_shared_SharedClassURLHelperImpl_storeSharedClassImpl3(JNIEnv* env, jobject thisObj, jint helperID, jstring partitionObj, jobject loaderObj, jobject urlObj, jclass clazzObj, jboolean newJarFile, jboolean minimizeUpdateChecks, jbyteArray nativeFlags);
void JNICALL
Java_com_ibm_oti_shared_SharedClassUtilities_init(JNIEnv *env, jclass clazz);
jint JNICALL
Java_com_ibm_oti_shared_SharedClassUtilities_getSharedCacheInfoImpl(JNIEnv *env, jclass clazz, jstring cacheDir, jint flag, jboolean useCommandLineValue, jobject arrayList);
jint JNICALL
Java_com_ibm_oti_shared_SharedClassUtilities_destroySharedCacheImpl(JNIEnv *env, jclass clazz, jstring cacheDir, jint cacheType, jstring cacheName, jboolean useCommandLineValue);
void JNICALL
Java_com_ibm_oti_shared_SharedClassURLHelperImpl_init(JNIEnv *env, jclass clazz);
void JNICALL
Java_com_ibm_oti_shared_SharedClassURLClasspathHelperImpl_init(JNIEnv *env, jclass clazz);
jboolean JNICALL
Java_com_ibm_oti_shared_Shared_isNonBootSharingEnabledImpl(JNIEnv* env, jclass clazz);
jlong JNICALL
Java_com_ibm_oti_shared_SharedClassStatistics_softMaxBytesImpl (JNIEnv* env, jobject thisObj);
/* J9SourceJclExtremeInit*/
extern J9_CFUNC jint JNICALL JVM_OnLoad ( JavaVM *jvm, char* options, void *reserved );
extern J9_CFUNC IDATA J9VMDllMain (J9JavaVM* vm, IDATA stage, void* reserved);

/* J9SourceManagementInit*/
extern J9_CFUNC jint
managementInit ( J9JavaVM *vm );
extern J9_CFUNC void
managementTerminate ( J9JavaVM *vm );

/* J9SourceManagementRuntime*/
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_RuntimeMXBeanImpl_getUptimeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_RuntimeMXBeanImpl_getStartTimeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_RuntimeMXBeanImpl_isBootClassPathSupportedImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_RuntimeMXBeanImpl_getNameImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedRuntimeMXBeanImpl_getProcessIDImpl(JNIEnv *env, jclass clazz);
extern J9_CFUNC jint JNICALL
Java_com_ibm_lang_management_internal_ExtendedRuntimeMXBeanImpl_getVMIdleStateImpl(JNIEnv *env, jclass clazz);

/* J9SourceManagementOperatingSystem*/
jdouble JNICALL
Java_com_ibm_java_lang_management_internal_OperatingSystemMXBeanImpl_getSystemLoadAverageImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getTotalPhysicalMemoryImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_lang_management_internal_OperatingSystemNotificationThreadShutdown_sendShutdownNotification (JNIEnv *env, jobject instance);
extern J9_CFUNC jint JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessingCapacityImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_lang_management_internal_OperatingSystemNotificationThread_processNotificationLoop (JNIEnv *env, jobject instance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_isDLPAREnabled (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessCpuTimeImpl (JNIEnv *env, jobject instance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getFreePhysicalMemorySizeImpl (JNIEnv *env, jobject instance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessVirtualMemorySizeImpl (JNIEnv *env, jobject instance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessPrivateMemorySizeImpl (JNIEnv *env, jobject instance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessPhysicalMemorySizeImpl (JNIEnv *env, jobject instance);
extern J9_CFUNC jdouble JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getSystemCpuLoadImpl (JNIEnv *env, jobject instance);

/* BBresmanNativesCommonMemorySpace*/
jboolean JNICALL Java_com_ibm_oti_vm_MemorySpace_isObjectInMemorySpace (JNIEnv * env, jobject memorySpace, jlong memorySpaceAddress, jobject anObject);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_getJ9MemorySpaceId (JNIEnv * env, jobject memorySpace, jlong memorySpaceKey, jlong memorySpaceAddress );
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_initializeVM (JNIEnv * env, jclass memorySpaceClass);
jint JNICALL Java_com_ibm_oti_vm_MemorySpace_allReferencesToObject (JNIEnv * env, jclass memorySpaceClass, jobject object, jobjectArray target);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_getJ9MemorySpaceKey (JNIEnv * env, jobject memorySpace, jlong memorySpaceAddress );
jint JNICALL Java_com_ibm_oti_vm_MemorySpace_allObjectsInJ9MemorySpace ( JNIEnv * env, jobject memorySpace, jlong j9MemorySpaceAddress, jobjectArray target );
void JNICALL Java_com_ibm_oti_vm_MemorySpace_setCurrentJ9MemorySpace (JNIEnv * env, jobject memorySpace, jlong j9MemorySpaceAddress);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_moveObjectToJ9MemorySpace (JNIEnv * env, jobject memorySpace, jlong j9MemorySpaceAddress, jobject anObject);
void JNICALL Java_com_ibm_oti_vm_MemorySpace_freeJ9MemorySpaceKey (JNIEnv * env, jobject memorySpace, jlong memorySpaceKey );
jobject JNICALL Java_com_ibm_oti_vm_MemorySpace_getMemorySpaceForClassLoaderImpl ( JNIEnv * env, jclass classClass, jobject classLoader );
jlong JNICALL Java_com_ibm_oti_vm_MemorySubspace_getSlotAt (JNIEnv * env, jobject memorySubspace, jlong j9MemoryAddress, jlong j9MemorySpaceKey, jint type, jint offset);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_getCurrentJ9MemorySpace (JNIEnv * env, jclass memorySpaceClass);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_createJ9MemorySpace (JNIEnv * env, jclass memorySpaceClass, jint newSpaceSize, jint oldSpaceSize);
void JNICALL Java_com_ibm_oti_vm_MemorySpace_removeJ9MemorySpace (JNIEnv * env, jclass memorySpaceClass, jlong j9MemorySpaceToRemove, jlong j9RemnantsMemorySpace);
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_getCurrentJ9MemorySpaceFor (JNIEnv * env, jclass memorySpaceClass, jobject javaThread);
void JNICALL Java_com_ibm_oti_vm_MemorySpace_setCurrentJ9MemorySpaceFor (JNIEnv * env, jclass memorySpaceClass, jlong j9MemorySpaceAddress, jobject javaThread);
void JNICALL Java_com_ibm_oti_vm_MemorySpace_setMemorySpaceForClassLoaderImpl (JNIEnv * env, jclass memorySpaceClass, jobject classLoader, jobject memorySpace );
jlong JNICALL Java_com_ibm_oti_vm_MemorySpace_getJ9MemorySpaceFor (JNIEnv * env, jclass memorySpaceClass, jobject anObject);

/* BBjclNativesCommonThread*/
jint JNICALL 
Java_java_lang_Thread_getStateImpl (JNIEnv *env, jobject recv, jlong threadRef);
void JNICALL
Java_java_lang_Thread_setPriorityNoVMAccessImpl(JNIEnv *env, jobject thread, jlong threadRef, jint priority);
void JNICALL
Java_java_lang_Thread_setNameImpl(JNIEnv *env, jobject thread, jlong threadRef, jstring threadName);

/* J9SourceJclThreadHelpers*/
extern J9_CFUNC jint
getJclThreadState (UDATA vmstate, jboolean started);
extern J9_CFUNC void
jclCallThreadPark (J9VMThread* vmThread, IDATA timeoutIsEpochRelative, const I_64* timeoutPtr);

/* J9SourceManagementMemoryManager*/
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryManagerMXBeanImpl_isManagedPoolImpl(JNIEnv *env, jclass beanInstance, jint id, jint poolID);

/* BBjclNativesCommonSystem*/
void JNICALL Java_java_lang_System_setFieldImpl (JNIEnv * env, jclass cls, jstring name, jobject stream);
jobject createSystemPropertyList (JNIEnv *env, const char *defaultValues[], int defaultCount);
jstring JNICALL Java_java_lang_System_getEncoding (JNIEnv *env, jclass clazz, jint encodingType);
jobject JNICALL Java_java_lang_System_getPropertyList (JNIEnv *env, jclass clazz);
jstring JNICALL Java_java_lang_System_mapLibraryName (JNIEnv * env, jclass unusedClass, jstring inName);
void JNICALL Java_java_lang_System_initLocale (JNIEnv *env, jclass clazz);

extern J9_CFUNC void JNICALL
Java_java_lang_System_startSNMPAgent(JNIEnv *env, jclass jlClass);

extern J9_CFUNC void JNICALL
Java_java_lang_System_rasInitializeVersion(JNIEnv * env, jclass unusedClass, jstring javaRuntimeVersion);

/* J9SourceManagementMemoryPool*/
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getCollectionUsageThresholdCountImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_resetPeakUsageImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getPeakUsageImpl (JNIEnv *env, jobject beanInstance, jint id, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC void JNICALL
Java_com_ibm_lang_management_internal_MemoryNotificationThreadShutdown_sendShutdownNotification ( JNIEnv *env, jobject instance, jint id );
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getUsageThresholdCountImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_isUsageThresholdSupportedImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_setUsageThresholdImpl (JNIEnv *env, jobject beanInstance, jint id, jlong newThreshold);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getCollectionUsageThresholdImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getPreCollectionUsageImpl (JNIEnv *env, jobject beanInstance, jint id, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_isUsageThresholdExceededImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_isCollectionUsageThresholdExceededImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_isCollectionUsageThresholdSupportedImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC void JNICALL
Java_com_ibm_lang_management_internal_MemoryNotificationThread_processNotificationLoop (JNIEnv *env, jobject threadInstance);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getUsageImpl (JNIEnv *env, jobject beanInstance, jint id, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getCollectionUsageImpl (JNIEnv *env, jobject beanInstance, jint id, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_setCollectionUsageThresholdImpl (JNIEnv *env, jobject beanInstance, jint id, jlong newThreshold);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryPoolMXBeanImpl_getUsageThresholdImpl (JNIEnv *env, jobject beanInstance, jint id);

/* J9SourceManagementThread*/
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_findNativeThreadIDImpl (JNIEnv *env, jclass beanClass, jlong threadId);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getNativeThreadIdsImpl (JNIEnv *env, jobject beanInstance, jlongArray tids, jlongArray nativeTIDs);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isCurrentThreadCpuTimeSupportedImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getThreadCpuTimeImpl (JNIEnv *env, jobject beanInstance, jlong threadID);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isThreadContentionMonitoringEnabledImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_resetPeakThreadCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_setThreadCpuTimeEnabledImpl (JNIEnv *env, jobject beanInstance, jboolean flag);
extern J9_CFUNC jint JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getDaemonThreadCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isThreadCpuTimeSupportedImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_setThreadContentionMonitoringEnabledImpl (JNIEnv *env, jobject beanInstance, jboolean flag);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_findMonitorDeadlockedThreadsImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isThreadCpuTimeEnabledImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jint JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getThreadCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getThreadUserTimeImpl (JNIEnv *env, jobject beanInstance, jlong threadID);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getAllThreadIdsImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jint JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getPeakThreadCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getTotalStartedThreadCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isThreadContentionMonitoringSupportedImpl (JNIEnv *env, jobject beanInstance);

extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getThreadInfoImpl(JNIEnv *env, jobject beanInstance,
	jlong id, jint maxStackDepth);
extern J9_CFUNC jboolean JNICALL 
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isObjectMonitorUsageSupportedImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_isSynchronizerUsageSupportedImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlongArray JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_findDeadlockedThreadsImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jobjectArray JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_getMultiThreadInfoImpl(JNIEnv *env, jobject beanInstance,
	jlongArray ids, jint maxStackDepth, jboolean lockedMonitors, jboolean lockedSynchronizers);
extern J9_CFUNC jobjectArray JNICALL
Java_com_ibm_java_lang_management_internal_ThreadMXBeanImpl_dumpAllThreadsImpl(JNIEnv *env, jobject beanInstance,
	jboolean lockedMonitors, jboolean lockedSynchronizers, jint maxDepth);

/* J9SourceJclReflect*/
extern J9_CFUNC UDATA
compareJavaStringToPartialUTF8 (J9VMThread * vmThread, j9object_t string, U_8 * utfData, UDATA utfLength);

/* J9SourceSigQuit*/
#if (defined(J9VM_INTERP_SIG_QUIT_THREAD)) /* priv. proto (autogen) */
extern J9_CFUNC void
J9SigQuitShutdown (J9JavaVM * vm);
#endif /* J9VM_INTERP_SIG_QUIT_THREAD (autogen) */

#if (defined(J9VM_INTERP_SIG_QUIT_THREAD)) /* priv. proto (autogen) */
extern J9_CFUNC jint
J9SigQuitStartup (J9JavaVM * vm);
#endif /* J9VM_INTERP_SIG_QUIT_THREAD (autogen) */


/* BBjclNativesCommonExceptionHelpers*/
void
throwNewNullPointerException (JNIEnv *env, char *message);
void
throwNewIllegalStateException (JNIEnv *env, char *message);
void
throwNewJavaZIOException (JNIEnv *env, char *message);
void
throwNewIndexOutOfBoundsException (JNIEnv *env, char *message);
void
throwNewInternalError (JNIEnv *env, char *message);
void
throwNewIllegalArgumentException (JNIEnv *env, char *message);
void
throwNewUnsupportedOperationException(JNIEnv *env);

/* J9SourceManagementCompilation*/
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_CompilationMXBeanImpl_isJITEnabled (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_CompilationMXBeanImpl_getTotalCompilationTimeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_CompilationMXBeanImpl_isCompilationTimeMonitoringSupportedImpl (JNIEnv *env, jobject beanInstance);

/* BBjclNativesCommonPlainMulticastSocketImpl*/
void JNICALL Java_java_net_PlainMulticastSocketImpl_createMulticastSocketImpl (
	JNIEnv* env, jclass thisClz, jobject thisObjFD, jboolean preferIPv4Stack);

/* J9SourceManagementClassLoading*/
extern J9_CFUNC jlong JNICALL
Java_openj9_internal_management_ClassLoaderInfoBaseImpl_getUnloadedClassCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_ClassLoadingMXBeanImpl_setVerboseImpl (JNIEnv *env, jobject beanInstance, jboolean value);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_ClassLoadingMXBeanImpl_getTotalLoadedClassCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_ClassLoadingMXBeanImpl_isVerboseImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_openj9_internal_management_ClassLoaderInfoBaseImpl_getLoadedClassCountImpl (JNIEnv *env, jobject beanInstance);

/* J9SourceManagementGarbageCollector*/
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getCollectionCountImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getCollectionTimeImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getLastCollectionStartTimeImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getLastCollectionEndTimeImpl (JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getTotalMemoryFreedImpl(JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getTotalCompactsImpl(JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_GarbageCollectorMXBeanImpl_getMemoryUsedImpl(JNIEnv *env, jobject beanInstance, jint id);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_lang_management_internal_ExtendedGarbageCollectorMXBeanImpl_getLastGcInfoImpl(JNIEnv *env, jobject beanInstance, jint id);

/* BBjclNativesCommonClassLoader*/
jboolean JNICALL Java_java_lang_ClassLoader_isVerboseImpl (JNIEnv *env, jclass clazz);
jclass JNICALL Java_java_lang_ClassLoader_defineClassImpl (JNIEnv *env, jobject receiver, jstring className, jbyteArray classRep, jint offset, jint length, jobject protectionDomain);
#if JAVA_SPEC_VERSION >= 15
jclass JNICALL Java_java_lang_ClassLoader_defineClassImpl1(JNIEnv *env, jobject receiver, jclass hostClass, jstring className, jbyteArray classRep, jobject protectionDomain, jboolean init, jint flags, jobject classData);
#endif /* JAVA_SPEC_VERSION >= 15 */
jboolean JNICALL Java_java_lang_ClassLoader_foundJavaAssertOption (JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_oti_vm_BootstrapClassLoader_addJar(JNIEnv *env, jobject receiver, jbyteArray jarPath);

/* BBjclNativesCommonIoHelpers*/
void ioh_convertToPlatform (char *path);

/* J9SourceManagementMemory*/
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setMaxHeapSizeImpl (JNIEnv *env, jobject beanInstance, jlong newsoftmx);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setSharedClassCacheSoftmxBytesImpl(JNIEnv *env, jobject beanInstance, jlong value);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setSharedClassCacheMinAotBytesImpl(JNIEnv *env, jobject beanInstance, jlong value);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setSharedClassCacheMaxAotBytesImpl(JNIEnv *env, jobject beanInstance, jlong value);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setSharedClassCacheMinJitDataBytesImpl(JNIEnv *env, jobject beanInstance, jlong value);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setSharedClassCacheMaxJitDataBytesImpl(JNIEnv *env, jobject beanInstance, jlong value);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getSharedClassCacheSoftmxUnstoredBytesImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getSharedClassCacheMaxAotUnstoredBytesImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getSharedClassCacheMaxJitDataUnstoredBytesImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getHeapMemoryUsageImpl (JNIEnv *env, jobject beanInstance, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC jstring JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getGCModeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_setVerboseImpl (JNIEnv *env, jobject beanInstance, jboolean flag);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_createMemoryManagers (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC void JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_createMemoryPools (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_isVerboseImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getMaxHeapSizeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jobject JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getNonHeapMemoryUsageImpl (JNIEnv *env, jobject beanInstance, jclass memoryUsage, jobject memUsageConstructor);
extern J9_CFUNC jint JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getObjectPendingFinalizationCountImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jboolean JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_isSetMaxHeapSizeSupportedImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getMaxHeapSizeLimitImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getMinHeapSizeImpl (JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getGCMainThreadCpuUsedImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jlong JNICALL
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getGCWorkerThreadsCpuUsedImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jint JNICALL 
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getMaximumGCThreadsImpl(JNIEnv *env, jobject beanInstance);
extern J9_CFUNC jint JNICALL 
Java_com_ibm_java_lang_management_internal_MemoryMXBeanImpl_getCurrentGCThreadsImpl(JNIEnv *env, jobject beanInstance);


/* J9SourceJclSidecarInit*/
extern J9_CFUNC IDATA J9VMDllMain (J9JavaVM* vm, IDATA stage, void* reserved);
extern J9_CFUNC jint JNICALL JVM_OnLoad (JavaVM * jvm, char *options, void *reserved);

/* dump.c */
jint JNICALL 
Java_com_ibm_jvm_Dump_JavaDumpImpl (JNIEnv *env, jclass clazz);
jint JNICALL 
Java_com_ibm_jvm_Dump_HeapDumpImpl (JNIEnv *env, jclass clazz);
jint JNICALL 
Java_com_ibm_jvm_Dump_SystemDumpImpl (JNIEnv *env, jclass clazz);
jint JNICALL
Java_com_ibm_jvm_Dump_SnapDumpImpl (JNIEnv *env, jclass clazz);

void JNICALL
Java_com_ibm_jvm_Dump_setDumpOptionsImpl (JNIEnv *env, jclass clazz, jstring opts);
jstring JNICALL
Java_com_ibm_jvm_Dump_queryDumpOptionsImpl (JNIEnv *env, jclass clazz);
void JNICALL
Java_com_ibm_jvm_Dump_resetDumpOptionsImpl (JNIEnv *env, jclass clazz);
jstring JNICALL
Java_com_ibm_jvm_Dump_triggerDumpsImpl (JNIEnv *env, jclass clazz, jstring opts, jstring event);
jboolean JNICALL
Java_com_ibm_jvm_Dump_isToolDump (JNIEnv *, jclass, jstring);


/* log.c */
jstring JNICALL
Java_com_ibm_jvm_Log_QueryOptionsImpl(JNIEnv *env, jclass clazz);
jint JNICALL
Java_com_ibm_jvm_Log_SetOptionsImpl(JNIEnv *env, jclass clazz, jstring options);

/* jcltrace.c */
void JNICALL Java_com_ibm_jvm_Trace_initTrace(JNIEnv *env, jobject recv, jobjectArray keys, jobjectArray values);
int JNICALL Java_com_ibm_jvm_Trace_registerApplication(JNIEnv *env, jobject recv, jobject name, jarray templates);
void JNICALL Java_com_ibm_jvm_Trace_trace__II(JNIEnv *env,
					jclass recv, jint handle, jint traceId);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2Ljava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jstring s2, jstring s3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2Ljava_lang_Object_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jobject o1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_Object_2Ljava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jobject o1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2I(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jint i1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIILjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jint i1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2J(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jlong l1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIJLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jlong l1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2B(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jbyte b1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIBLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jbyte b1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2C(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jchar c1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IICLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jchar c1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2F(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jfloat f1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIFLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jfloat f1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2D(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jdouble d1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIDLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jdouble d1, jstring s1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_Object_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jobject o1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_Object_2Ljava_lang_Object_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jobject o1, jobject o2);
void JNICALL Java_com_ibm_jvm_Trace_trace__III(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jint i1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIII(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jint i1 , jint i2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIIII(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jint i1, jint i2, jint i3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIJ(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jlong l1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIJJ(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jlong l1, jlong l2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIJJJ(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jlong l1, jlong l2, jlong l3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIB(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jbyte b1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIBB(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jbyte b1, jbyte b2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIBBB(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jbyte b1, jbyte b2, jbyte b3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIC(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jchar c1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IICC(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jchar c1, jchar c2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IICCC(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jchar c1, jchar c2, jchar c3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIF(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jfloat f1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIFF(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jfloat f1, jfloat f2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIFFF(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jfloat f1, jfloat f2, jfloat f3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IID(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jdouble d1);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIDD(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jdouble d1, jdouble d2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIDDD(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jdouble d1, jdouble d2, jdouble d3);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2Ljava_lang_Object_2Ljava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jobject o1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_Object_2Ljava_lang_String_2Ljava_lang_Object_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jobject o1, jstring s1, jobject o2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2ILjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jint i1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIILjava_lang_String_2I(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jint i1, jstring s1, jint i2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2JLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jlong l1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIJLjava_lang_String_2J(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jlong l1, jstring s1, jlong l2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2BLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jbyte b1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIBLjava_lang_String_2B(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jbyte b1, jstring s1, jbyte b2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2CLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jchar c1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IICLjava_lang_String_2C(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jchar c1, jstring s1, jchar c2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2FLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jfloat f1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIFLjava_lang_String_2F(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jfloat f1, jstring s1, jfloat f2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IILjava_lang_String_2DLjava_lang_String_2(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jstring s1, jdouble d1, jstring s2);
void JNICALL Java_com_ibm_jvm_Trace_trace__IIDLjava_lang_String_2D(JNIEnv *env,
					jclass recv, jint handle, jint traceId, jdouble d1, jstring s1, jdouble d2);
int JNICALL Java_com_ibm_jvm_Trace_set(JNIEnv *env, jobject recv, jstring jcmd);
void JNICALL Java_com_ibm_jvm_Trace_snap(JNIEnv *env, jobject recv);
void JNICALL Java_com_ibm_jvm_Trace_suspend(JNIEnv *env, jobject recv);
void JNICALL Java_com_ibm_jvm_Trace_resume(JNIEnv *env, jobject recv);
void JNICALL Java_com_ibm_jvm_Trace_suspendThis(JNIEnv *env, jobject recv);
void JNICALL Java_com_ibm_jvm_Trace_resumeThis(JNIEnv *env, jobject recv);
jlong JNICALL Java_com_ibm_jvm_Trace_getMicros(JNIEnv *env, jobject recv);

/* attach API */
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_cancelNotify(JNIEnv *env, jclass clazz, jstring ctrlDirName, jstring semaName, jint numberOfDecrements, jboolean global);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_chownFileToTargetUid(JNIEnv *env, jclass clazz, jstring path, jlong uid);
jlong JNICALL
Java_openj9_internal_tools_attach_target_CommonDirectory_getFileOwner(JNIEnv *env, jclass clazz, jstring path);
jlong JNICALL
Java_openj9_internal_tools_attach_target_IPC_getUid(JNIEnv *env, jclass clazz);
jbyteArray JNICALL
Java_openj9_internal_tools_attach_target_IPC_getTmpDirImpl(JNIEnv *env, jclass clazz);
jstring JNICALL
Java_openj9_internal_tools_attach_target_IPC_getTempDirImpl(JNIEnv *env, jclass clazz);
jboolean JNICALL
Java_openj9_internal_tools_attach_target_IPC_isUsingDefaultUid(JNIEnv *env, jclass clazz);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_mkdirWithPermissionsImpl(JNIEnv *env, jclass clazz, jstring absolutePath, jint cdPerms);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_createFileWithPermissionsImpl(JNIEnv *env, jclass clazz, jstring path, jint mode);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_setupSemaphore(JNIEnv *env, jclass clazz, jstring ctrlDirName);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_openSemaphore(JNIEnv *env, jclass clazz, jstring ctrlDirName, jstring semaName);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_notifyVm(JNIEnv *env, jclass clazz, jstring ctrlDirName, jstring semaName, jint numberOfPosts, jboolean global);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_waitSemaphore(JNIEnv *env, jclass clazz);
void JNICALL
Java_openj9_internal_tools_attach_target_IPC_closeSemaphore(JNIEnv *env, jclass clazz);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_destroySemaphore(JNIEnv *env, jclass clazz);
jlong JNICALL
Java_openj9_internal_tools_attach_target_IPC_getProcessId(JNIEnv *env, jclass clazz);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_chmod(JNIEnv *env, jclass clazz, jstring path, jint mode);
jint JNICALL
Java_openj9_internal_tools_attach_target_IPC_processExistsImpl(JNIEnv *env, jclass clazz, jlong pid);
jlong JNICALL
Java_openj9_internal_tools_attach_target_FileLock_lockFileImpl(JNIEnv *env, jclass clazz, jstring path, jint mode, jboolean blocking);
jint JNICALL
Java_openj9_internal_tools_attach_target_FileLock_unlockFileImpl(JNIEnv *env, jclass clazz, jlong fd);
void JNICALL
Java_openj9_internal_tools_attach_target_IPC_tracepoint(JNIEnv *env, jclass clazz, jint statusCode, jstring message);

/* J9SourceJclExceptionSupport*/
extern J9_CFUNC j9array_t  
getStackTrace (J9VMThread * vmThread, j9object_t* exceptionAddr, UDATA pruneConstructors);

/* J9SourceJclClearInit*/
extern J9_CFUNC jint JNICALL JVM_OnLoad (JavaVM * jvm, char *options, void *reserved);
extern J9_CFUNC IDATA J9VMDllMain (J9JavaVM* vm, IDATA stage, void* reserved);

/* BBjclNativesCommonCompiler*/
jobject JNICALL Java_java_lang_Compiler_commandImpl (JNIEnv *env, jclass clazz, jobject cmd);
void JNICALL Java_java_lang_Compiler_disable (JNIEnv *env, jclass clazz);
jboolean JNICALL Java_java_lang_Compiler_compileClassImpl (JNIEnv *env, jclass clazz, jclass compileClass);
jboolean JNICALL Java_java_lang_Compiler_compileClassesImpl (JNIEnv *env, jclass clazz, jstring nameRoot);
void JNICALL Java_java_lang_Compiler_enable (JNIEnv *env, jclass clazz);

#if (defined(J9VM_OPT_DYNAMIC_LOAD_SUPPORT)) /* priv. proto (autogen) */
extern J9_CFUNC char* getExtraOptions (J9JavaVM* vm, char* key);
#endif /* J9VM_OPT_DYNAMIC_LOAD_SUPPORT (autogen) */

#if (defined(J9VM_OPT_DYNAMIC_LOAD_SUPPORT)) /* priv. proto (autogen) */
extern J9_CFUNC char* catPaths (J9PortLibrary* portLib, char* path1, char* path2);
#endif /* J9VM_OPT_DYNAMIC_LOAD_SUPPORT (autogen) */

/* BBjclNativesCommonUnsafe*/


/**
 * Unsafe method used to create anonClasses
 *
 * This function defines a class without making it known to its classLoader (classLoader of hostClass).
 * Also, this class is not search-able.
 *
 * @param env					The JNI env
 * @param receiver				The unsafe (Unsafe)
 * @param hostClass			 	The host class creating the anonClass
 * @param bytecodes			 	The anonClass bytes
 * @param constPatches 			The constantpool patches (this is NULL for JSR335)
 *
 * @return anonClass if succeeded, NULL otherwise.
 */
jclass JNICALL
Java_sun_misc_Unsafe_defineAnonymousClass(JNIEnv *env, jobject receiver, jclass hostClass, jbyteArray bytecodes, jobjectArray constPatches);
jclass JNICALL
Java_sun_misc_Unsafe_defineClass__Ljava_lang_String_2_3BIILjava_lang_ClassLoader_2Ljava_security_ProtectionDomain_2 (
	JNIEnv *env, jobject receiver, jstring className, jbyteArray classRep, jint offset, jint length, jobject classLoader, jobject protectionDomain);
void JNICALL
Java_sun_misc_Unsafe_setMemory__Ljava_lang_Object_2JJB(JNIEnv *env, jobject receiver, jobject obj, jlong offset, jlong size, jbyte value);
void JNICALL Java_sun_misc_Unsafe_registerNatives(JNIEnv *env, jclass clazz);
void JNICALL Java_jdk_internal_misc_Unsafe_registerNatives(JNIEnv *env, jclass clazz);
jboolean JNICALL Java_sun_misc_Unsafe_shouldBeInitialized(JNIEnv *env, jobject receiver, jclass clazz);
jint JNICALL Java_sun_misc_Unsafe_pageSize(JNIEnv *env, jobject receiver);
jint JNICALL Java_sun_misc_Unsafe_getLoadAverage(JNIEnv *env, jobject receiver, jdoubleArray loadavg, jint nelems);
jboolean JNICALL Java_sun_misc_Unsafe_unalignedAccess0(JNIEnv *env, jobject receiver);
jboolean JNICALL Java_sun_misc_Unsafe_isBigEndian0(JNIEnv *env, jobject receiver);
jobject JNICALL Java_sun_misc_Unsafe_getUncompressedObject(JNIEnv *env, jobject receiver, jlong address);
jclass JNICALL Java_sun_misc_Unsafe_getJavaMirror(JNIEnv *env, jobject receiver, jlong address);
jlong JNICALL Java_sun_misc_Unsafe_getKlassPointer(JNIEnv *env, jobject receiver, jobject address);
jlong JNICALL Java_jdk_internal_misc_Unsafe_allocateDBBMemory(JNIEnv *env, jobject receiver, jlong size);
void JNICALL Java_jdk_internal_misc_Unsafe_freeDBBMemory(JNIEnv *env, jobject receiver, jlong address);
jlong JNICALL Java_jdk_internal_misc_Unsafe_reallocateDBBMemory(JNIEnv *env, jobject receiver, jlong address, jlong size);

void JNICALL Java_jdk_internal_misc_Unsafe_copySwapMemory0(JNIEnv *env, jobject receiver, jobject obj1, jlong size1, jobject obj2, jlong size2, jlong size3, jlong size4);
jobject JNICALL Java_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile(JNIEnv *env, jobject receiver, jobject obj1, jlong size, jobject obj2, jobject obj3);
jint JNICALL Java_jdk_internal_misc_Unsafe_compareAndExchangeIntVolatile(JNIEnv *env, jobject receiver, jobject obj1, jlong size1, jint size2, jint size3);
jlong JNICALL Java_jdk_internal_misc_Unsafe_compareAndExchangeLongVolatile(JNIEnv *env, jobject receiver, jobject obj1, jlong size1, jlong size2, jlong size3);

/* vector natives */
jint JNICALL
Java_jdk_internal_vm_vector_VectorSupport_registerNatives(JNIEnv *env, jclass clazz);
jint JNICALL
Java_jdk_internal_vm_vector_VectorSupport_getMaxLaneCount(JNIEnv *env, jclass clazz, jclass elementType);


/* BBjclNativesCommonVM*/
jint JNICALL Java_com_ibm_oti_vm_VM_getBootClassPathCount (JNIEnv * env, jclass clazz);
jint JNICALL 
Java_com_ibm_oti_vm_VM_allInstances (JNIEnv * env, jclass unused, jclass clazz, jobjectArray target );
jint JNICALL Java_com_ibm_oti_vm_VM_getClassPathCount (JNIEnv * env, jclass clazz);
jint JNICALL Java_com_ibm_oti_vm_VM_processorsImpl (JNIEnv * env, jclass clazz);
jobject JNICALL 
Java_com_ibm_oti_vm_VM_getHttpProxyImpl (JNIEnv *env, jobject recv);
jstring JNICALL
Java_com_ibm_oti_vm_VM_setProperty (JNIEnv *env, jobject recv, jstring prop, jstring value);
jint JNICALL 
Java_com_ibm_oti_vm_VM_setCommonData (JNIEnv * env, jclass unused, jobject string1, jobject string2 );
jboolean JNICALL
Java_com_ibm_oti_vm_VM_setDaemonThreadImpl (JNIEnv *env, jobject recv, jobject aThread);
void JNICALL Java_com_ibm_oti_vm_VM_dumpString(JNIEnv * env, jclass clazz, jstring str);
jboolean JNICALL Java_com_ibm_oti_vm_VM_appendToCPNativeImpl(JNIEnv * env, jclass clazz, jstring classPathAdditions, jstring newClassPath);
jboolean JNICALL Java_com_ibm_oti_vm_VM_isApplicationClassLoaderPresent(JNIEnv * env, jclass clazz);
jstring JNICALL Java_openj9_internal_tools_attach_target_DiagnosticUtils_getHeapClassStatisticsImpl(JNIEnv * env, jclass unused);
jobjectArray JNICALL Java_openj9_internal_tools_attach_target_DiagnosticUtils_dumpAllThreadsImpl(JNIEnv *env, jobject beanInstance,
	jboolean getLockedMonitors, jboolean getLockedSynchronizers, jint maxDepth);
jstring JNICALL Java_openj9_internal_tools_attach_target_DiagnosticUtils_triggerDumpsImpl(JNIEnv *env, jclass clazz, jstring opts, jstring event);

/* J9SourceJclCommonInit*/
jint computeFullVersionString (J9JavaVM* vm);
jint initializeKnownClasses (J9JavaVM* vm, U_32 runtimeFlags);
UDATA initializeRequiredClasses(J9VMThread *vmThread, char* libName);

/* J9SourceJclDefineClass*/
extern J9_CFUNC jclass 
defineClassCommon (JNIEnv *env, jobject classLoaderObject,
	jstring className, jbyteArray classRep, jint offset, jint length, jobject protectionDomain, UDATA *options, J9Class *hostClass, J9ClassPatchMap *patchMap, BOOLEAN validateName);

/* BBjclNativesCommonAccessController*/
jboolean JNICALL Java_java_security_AccessController_initializeInternal (JNIEnv *env, jclass thisClz);

/* BBjclNativesCommonProxy*/
jclass JNICALL Java_java_lang_reflect_Proxy_defineClassImpl (JNIEnv * env, jclass recvClass, jobject classLoader, jstring className, jbyteArray classBytes);
jclass JNICALL 
Java_java_lang_reflect_Proxy_defineClass0__Ljava_lang_ClassLoader_2Ljava_lang_String_2_3BIILjava_lang_Object_2_3Ljava_lang_Object_2Ljava_lang_Object_2 (JNIEnv * env, jclass recvClass, jobject classLoader, jstring className, jbyteArray classBytes, jint offset, jint length, jobject pd, jobject signers, jobject source);
jclass JNICALL 
Java_java_lang_reflect_Proxy_defineClass0__Ljava_lang_ClassLoader_2Ljava_lang_String_2_3BII (JNIEnv * env, jclass recvClass, jobject classLoader, jstring className, jbyteArray classBytes, jint offset, jint length);

/* BBjclNativesCommonGlobals*/
void JNICALL JNI_OnUnload (JavaVM * vm, void *reserved);
jint JNICALL JCL_OnLoad (JavaVM * vm, void *reserved);

/* J9SourceJclGetStackTraceSupport*/
extern J9_CFUNC j9object_t
createStackTraceThrowable (J9VMThread *currentThread,  const UDATA *frames, UDATA maxFrames);
extern J9_CFUNC j9object_t
getStackTraceForThread (J9VMThread * vmThread, J9VMThread *targetThread, UDATA skipCount);

/* J9SourceJclStandardInit*/
jint JCL_OnUnload (J9JavaVM* vm, void* reserved);
jint standardPreconfigure ( JavaVM *jvm);
void
internalInitializeJavaLangClassLoader (JNIEnv * env);
IDATA checkJCL (J9VMThread * vmThread, U_8* dllValue, U_8* jclConfig, UDATA j9Version, UDATA jclVersion);
jint
completeInitialization (J9JavaVM * vm);
jint standardInit ( J9JavaVM *vm, char* dllName);
jint JNICALL JVM_OnUnload (JavaVM* jvm, void* reserved);

/* J9SourceJclBPInit*/
#if (defined(J9VM_OPT_DYNAMIC_LOAD_SUPPORT)) /* priv. proto (autogen) */
extern J9_CFUNC char* getDefaultBootstrapClassPath (J9JavaVM * vm, char* javaHome);
#endif /* J9VM_OPT_DYNAMIC_LOAD_SUPPORT (autogen) */

/* vminternals */
jobject JNICALL Java_java_lang_J9VMInternals_newInstance(JNIEnv *env, jclass clazz, jobject instantiationClass, jobject constructorClass);


/************************************************************
 ** COMPONENT: BBjclNativesCommonFileSystem
 ************************************************************/

/* BBjclNativesCommonVMFileSystem*/
jbyteArray JNICALL Java_com_ibm_oti_vm_VM_getPathFromClassPath (JNIEnv * env, jclass clazz, jint cpIndex);

/************************************************************
 ** COMPONENT: BBjclNativesWin32
 ************************************************************/

/* BBjclNativesWin32CharConv*/
jint JNICALL Java_com_ibm_oti_io_NativeCharacterConverter_convertStreamBytesToCharsImpl (JNIEnv * env, jobject recv, jbyteArray srcBytes, jint srcOffset, jint srcCount, jcharArray dstChars, jint dstOffset, jint dstCount, jintArray stopPos, jstring codePageID, jlong osCodePage);
jbyteArray JNICALL Java_com_ibm_oti_io_NativeCharacterConverter_convertCharsToBytesImpl (JNIEnv * env, jobject recv, jcharArray chars, jint offset, jint count, jstring codePageID, jlong osCodePage);
jboolean JNICALL Java_com_ibm_oti_io_NativeCharacterConverter_supportsCodePage (JNIEnv * env, jobject recv, jstring javaCodePage);
jcharArray JNICALL Java_com_ibm_oti_io_NativeCharacterConverter_convertBytesToCharsImpl (JNIEnv * env, jobject recv, jbyteArray bytes, jint offset, jint count, jstring codePageID, jlong osCodePage);

/* BBjclNativesWin32SystemHelpers*/
char* getPlatformFileEncoding (JNIEnv *env, char *codepage, int size, int encodingType);
I_32
convertToUTF8 (J9PortLibrary* portLibrary, const wchar_t* unicodeString, char* utf8Buffer, UDATA size);
char * getTmpDir (JNIEnv *env, char **tempdir);
jobject getPlatformPropertyList (JNIEnv *env, const char *strings[], int propIndex);
void mapLibraryToPlatformName (const char *inPath, char *outPath);

/************************************************************
 ** COMPONENT: BBjclNativesUNIX
 ************************************************************/

/* BBjclNativesUNIXSystemHelpers*/
char *getPlatformFileEncoding (JNIEnv * env, char *codepageProp, int propSize, int encodingType);
char * getTmpDir (JNIEnv *env, char**envSpace);
jobject getPlatformPropertyList (JNIEnv * env, const char *strings[], int propIndex);
void mapLibraryToPlatformName (const char *inPath, char *outPath);

/* orbvmhelpers.c */

jboolean JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_is32Bit(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBitsInReferenceField(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInReferenceField(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBitsInDescriptionWord(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInDescriptionWord(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInJ9ObjectHeader(JNIEnv *env, jclass rcv);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass64(JNIEnv *env, jclass rcv, jclass c);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getTotalInstanceSizeFromJ9Class64(JNIEnv *env, jclass rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class64(JNIEnv *env, jclass rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr64(JNIEnv *env, jclass rcv, jlong descriptorPtr);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass32(JNIEnv *env, jclass rcv, jclass c);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getTotalInstanceSizeFromJ9Class32(JNIEnv *env, jclass rcv, jint j9clazz);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class32(JNIEnv *env, jclass rcv, jint j9clazz);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr32(JNIEnv *env, jclass rcv, jint descriptorPtr);

/* jithelpers.c */
jint JNICALL Java_com_ibm_jit_JITHelpers_javaLangClassJ9ClassOffset(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_j9ClassROMClassOffset(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_romClassModifiersOffset(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_classIsInterfaceFlag(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_classIsPrimitiveFlag(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_j9ClassSuperclassesOffset(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_j9ClassClassDepthAndFlagsOffset(JNIEnv *env, jclass ignored);
jint JNICALL Java_com_ibm_jit_JITHelpers_classDepthMask(JNIEnv *env, jclass ignored);
jboolean JNICALL Java_com_ibm_jit_JITHelpers_isBigEndian(JNIEnv *env, jclass ignored);
jboolean JNICALL Java_com_ibm_jit_JITHelpers_is32Bit(JNIEnv *env, jobject rcv);
jint JNICALL Java_com_ibm_jit_JITHelpers_getNumBitsInReferenceField(JNIEnv *env, jobject rcv);
jint JNICALL Java_com_ibm_jit_JITHelpers_getNumBytesInReferenceField(JNIEnv *env, jobject rcv);
jint JNICALL Java_com_ibm_jit_JITHelpers_getNumBitsInDescriptionWord(JNIEnv *env, jobject rcv);
jint JNICALL Java_com_ibm_jit_JITHelpers_getNumBytesInDescriptionWord(JNIEnv *env, jobject rcv);
jint JNICALL Java_com_ibm_jit_JITHelpers_getNumBytesInJ9ObjectHeader(JNIEnv *env, jobject rcv);
#if defined(J9VM_ENV_DATA64)
jlong JNICALL Java_com_ibm_jit_JITHelpers_getJ9ClassFromClass64(JNIEnv *env, jobject rcv, jclass c);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getTotalInstanceSizeFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getInstanceDescriptionFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getDescriptionWordFromPtr64(JNIEnv *env, jobject rcv, jlong descriptorPtr);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getRomClassFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getArrayShapeFromRomClass64(JNIEnv *env, jobject rcv, jlong j9romclazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getModifiersFromRomClass64(JNIEnv *env, jobject rcv, jlong j9romclazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class64(JNIEnv *env, jobject rcv, jlong j9clazz);
#else /* J9VM_ENV_DATA64 */
jint JNICALL Java_com_ibm_jit_JITHelpers_getJ9ClassFromClass32(JNIEnv *env, jobject rcv, jclass c);
jint JNICALL Java_com_ibm_jit_JITHelpers_getTotalInstanceSizeFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getInstanceDescriptionFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getDescriptionWordFromPtr32(JNIEnv *env, jobject rcv, jint descriptorPtr);
jint JNICALL Java_com_ibm_jit_JITHelpers_getRomClassFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getSuperClassesFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getClassDepthAndFlagsFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getBackfillOffsetFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getArrayShapeFromRomClass32(JNIEnv *env, jobject rcv, jint j9romclazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getModifiersFromRomClass32(JNIEnv *env, jobject rcv, jint j9romclazz);
jint JNICALL Java_com_ibm_jit_JITHelpers_getClassFlagsFromJ9Class32(JNIEnv *env, jobject rcv, jint j9clazz);
#endif /* J9VM_ENV_DATA64 */

/* crypto.c */
jboolean JNICALL Java_com_ibm_jit_Crypto_isAESSupportedByHardware(JNIEnv *env, jobject ignored);
jboolean JNICALL Java_com_ibm_jit_Crypto_expandAESKeyInHardware(JNIEnv *env, jobject ignored, jbyteArray rawKey, jintArray rkeys, jint Nr);
jboolean JNICALL Java_com_ibm_jit_Crypto_doAESInHardware(JNIEnv *env, jobject ignored, jbyteArray data, jint offset, jint length, jbyteArray to, jint pos, jintArray rkeys, jint Nr, jboolean encrypt);

extern void* unsafeAllocateMemory(J9VMThread* vmThread, UDATA size, UDATA throwExceptionOnFailure);
extern void unsafeFreeMemory(J9VMThread* vmThread, void* oldAddress);
extern void* unsafeReallocateMemory(J9VMThread* vmThread, void* oldAddress, UDATA size);
extern void* unsafeAllocateDBBMemory(J9VMThread* vmThread, UDATA size, UDATA throwExceptionOnFailure);
extern void unsafeFreeDBBMemory(J9VMThread* vmThread, void* oldAddress);
extern void* unsafeReallocateDBBMemory(J9VMThread* vmThread, void* oldAddress, UDATA size);


/* annparser.c */
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getAnnotationsData__Ljava_lang_reflect_Field_2(JNIEnv *env, jclass unusedClass, jobject jlrField);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getAnnotationsData__Ljava_lang_reflect_Constructor_2(JNIEnv *env, jclass unusedClass, jobject jlrConstructor);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getAnnotationsData__Ljava_lang_reflect_Method_2(JNIEnv *env, jclass unusedClass, jobject jlrMethod);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getParameterAnnotationsData__Ljava_lang_reflect_Constructor_2(JNIEnv *env, jclass unusedClass, jobject jlrConstructor);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getParameterAnnotationsData__Ljava_lang_reflect_Method_2(JNIEnv *env, jclass unusedClass, jobject jlrMethod);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getDefaultValueData(JNIEnv *env, jclass unusedClass, jobject jlrMethod);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getAnnotationsDataImpl__Ljava_lang_Class_2(JNIEnv *env, jclass unusedClass, jclass jlClass);
jobject JNICALL Java_com_ibm_oti_reflect_TypeAnnotationParser_getTypeAnnotationsDataImpl__Ljava_lang_reflect_Constructor_2(JNIEnv *env, jclass unusedClass, jobject jlrConstructor);
jobject JNICALL Java_com_ibm_oti_reflect_TypeAnnotationParser_getTypeAnnotationsDataImpl__Ljava_lang_reflect_Method_2(JNIEnv *env, jclass unusedClass, jobject jlrMethod);
jobject JNICALL Java_com_ibm_oti_reflect_TypeAnnotationParser_getTypeAnnotationsDataImpl__Ljava_lang_reflect_Field_2(JNIEnv *env, jclass unusedClass, jobject jlrField);
jobject JNICALL Java_com_ibm_oti_reflect_TypeAnnotationParser_getTypeAnnotationsDataImpl__Ljava_lang_Class_2(JNIEnv *env, jclass unusedClass, jclass jlClass);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getUTF8At(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jint JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getIntAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jlong JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getLongAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jfloat JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getFloatAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jdouble JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getDoubleAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_com_ibm_oti_reflect_AnnotationParser_getStringAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);

/* reflecthelp.c */
jobject JNICALL Java_java_lang_reflect_Field_getSignature(JNIEnv *env, jobject jlrField);
jobject JNICALL Java_java_lang_reflect_Method_getSignature(JNIEnv *env, jobject jlrMethod);
jobject JNICALL Java_java_lang_reflect_Constructor_getSignature(JNIEnv *env, jobject jlrConstructor);
jobject JNICALL Java_java_lang_reflect_Method_invokeImpl(JNIEnv * env, jobject jlrMethod, jobject receiver, jobjectArray args);
jobject JNICALL Java_java_lang_reflect_Constructor_newInstanceImpl(JNIEnv * env, jobject jlrConstructor, jobjectArray args);
jint JNICALL Java_java_lang_reflect_AccessibleObject_getClassAccessFlags(JNIEnv * env, jclass ignored, jclass clazz);
jobject JNICALL Java_java_lang_reflect_AccessibleObject_getCallerClass(JNIEnv * env, jclass ignored, jint depth);
void JNICALL Java_java_lang_reflect_AccessibleObject_initializeClass(JNIEnv * env, jclass ignored, jclass jlClass);
jobject JNICALL Java_java_lang_reflect_Array_multiNewArrayImpl(JNIEnv *env, jclass unusedClass, jclass componentType, jint dimensions, jintArray dimensionsArray);

/* java_lang_Class.c */
jobject JNICALL Java_java_lang_Class_getDeclaredAnnotationsData(JNIEnv *env, jobject jlClass);
jobject JNICALL Java_java_lang_Class_getStackClasses(JNIEnv *env, jclass jlHeapClass, jint maxDepth, jboolean stopAtPrivileged);
jobject JNICALL Java_java_security_AccessController_getAccSnapshot(JNIEnv* env, jclass jsAccessController, jint startingFrame, jboolean forDoPrivilegedWithCombiner);
jobject JNICALL Java_java_security_AccessController_getCallerPD(JNIEnv* env, jclass jsAccessController, jint startingFrame);
jobject JNICALL Java_com_ibm_oti_vm_VM_getClassNameImpl(JNIEnv *env, jclass recv, jclass jlClass);
jobject JNICALL Java_java_lang_Class_getDeclaredFieldImpl(JNIEnv *env, jobject recv, jstring jname);
jarray JNICALL Java_java_lang_Class_getDeclaredFieldsImpl(JNIEnv *env, jobject recv);
#if JAVA_SPEC_VERSION >= 11
jobject JNICALL Java_java_lang_Class_getNestHostImpl(JNIEnv *env, jobject recv);
jobject JNICALL Java_java_lang_Class_getNestMembersImpl(JNIEnv *env, jobject recv);
#endif /* JAVA_SPEC_VERSION >= 11 */

/* sun_misc_Perf.c */
void JNICALL Java_sun_misc_Perf_registerNatives(JNIEnv *env, jclass klass);
jobject JNICALL Java_sun_misc_Perf_attach(JNIEnv *env, jobject perf, jstring user, jint lvmid, jint mode);
jobject JNICALL Java_sun_misc_Perf_detach(JNIEnv * env, jobject perf, jobject byteBuffer);
jobject JNICALL Java_sun_misc_Perf_createLong(JNIEnv *env, jobject perf, jstring name, jint variability, jint units, jlong value);
jobject JNICALL Java_sun_misc_Perf_createByteArray(JNIEnv *env, jobject perf, jstring name, jint variability, jint units, jarray value, jint maxLength);
jlong JNICALL Java_sun_misc_Perf_highResCounter(JNIEnv *env, jobject perf);
jlong JNICALL Java_sun_misc_Perf_highResFrequency(JNIEnv *env, jobject perf);
void JNICALL Java_jdk_internal_perf_Perf_registerNatives(JNIEnv *env, jclass clazz);

/* Used by both OpenJ9 & OJDK MH impl */
#if defined (J9VM_OPT_METHOD_HANDLE) || defined(J9VM_OPT_OPENJDK_METHODHANDLE)
jobject JNICALL Java_java_lang_invoke_MethodHandle_invoke(JNIEnv *env, jclass ignored, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_MethodHandle_invokeExact(JNIEnv *env, jclass ignored, jobject handle, jobject args);
#endif

/* java_dyn_methodhandle.c */
#if defined(J9VM_OPT_METHOD_HANDLE)
void		JNICALL Java_java_lang_invoke_InterfaceHandle_registerNatives(JNIEnv *env, jclass nativeClass);
void		JNICALL Java_java_lang_invoke_MethodHandle_requestCustomThunkFromJit(JNIEnv* env, jobject handle, jobject thunk);
jint		JNICALL Java_java_lang_invoke_MethodHandle_vmRefFieldOffset(JNIEnv *env, jclass clazz, jclass ignored);
void		JNICALL Java_java_lang_invoke_MutableCallSite_freeGlobalRef(JNIEnv *env, jclass mutableCallSite, jlong bypassOffset);
void		JNICALL Java_java_lang_invoke_MutableCallSite_registerNatives(JNIEnv *env, jclass nativeClass);
jclass		JNICALL Java_java_lang_invoke_PrimitiveHandle_lookupField(JNIEnv *env, jobject handle, jclass lookupClass, jstring name, jstring signature, jboolean isStatic, jclass accessClass);
jclass		JNICALL Java_java_lang_invoke_PrimitiveHandle_lookupMethod(JNIEnv *env, jobject handle, jclass lookupClass, jstring name, jstring signature, jbyte kind, jclass specialCaller);
jboolean	JNICALL Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromConstructor(JNIEnv *env, jclass clazz, jobject handle, jobject ctor);
jboolean	JNICALL Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromField(JNIEnv *env, jclass clazz, jobject handle, jobject reflectField);
jboolean	JNICALL Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromMethod(JNIEnv *env, jclass clazz, jobject handle, jclass declaringClass, jobject method, jbyte kind, jclass specialToken);
jboolean	JNICALL Java_java_lang_invoke_PrimitiveHandle_setVMSlotAndRawModifiersFromSpecialHandle(JNIEnv *env, jclass clazz, jobject handle, jobject specialHandle);
void		JNICALL Java_java_lang_invoke_ThunkTuple_registerNatives(JNIEnv *env, jclass nativeClass);

UDATA lookupField(JNIEnv *env, jboolean isStatic, J9Class *j9LookupClass, jstring name, J9UTF8 *sigUTF, J9Class **definingClass, UDATA *romField, jclass accessClass);
void setClassLoadingConstraintLinkageError(J9VMThread *vmThread, J9Class *methodOrFieldClass, J9UTF8 *signatureUTF8);
#ifdef J9VM_OPT_PANAMA
extern J9_CFUNC jlong JNICALL
Java_java_lang_invoke_MethodHandles_findNativeAddress(JNIEnv *env, jclass jlClass, jstring methodName);
#endif
#if JAVA_SPEC_VERSION >= 15
extern J9_CFUNC void JNICALL
Java_java_lang_invoke_MethodHandleNatives_checkClassBytes(JNIEnv *env, jclass jlClass, jbyteArray classRep);
#endif /* JAVA_SPEC_VERSION >= 15 */
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */

/* java_dyn_methodtype.c */
#if defined(J9VM_OPT_METHOD_HANDLE)
jobject JNICALL Java_java_lang_invoke_MethodType_makeTenured(JNIEnv *env, jclass clazz, jobject receiverObject);
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */

/* java_lang_invoke_MethodHandleNatives.cpp */
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
void JNICALL Java_java_lang_invoke_MethodHandleNatives_init(JNIEnv *env, jclass clazz, jobject self, jobject ref);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_expand(JNIEnv *env, jclass clazz, jobject self);
#if JAVA_SPEC_VERSION >= 16
jobject JNICALL Java_java_lang_invoke_MethodHandleNatives_resolve(JNIEnv *env, jclass clazz, jobject self, jclass caller, jint lookupMode, jboolean speculativeResolve);
#else /* JAVA_SPEC_VERSION >= 16 */
jobject JNICALL Java_java_lang_invoke_MethodHandleNatives_resolve(JNIEnv *env, jclass clazz, jobject self, jclass caller, jboolean speculativeResolve);
#endif /* JAVA_SPEC_VERSION >= 16 */
jint JNICALL Java_java_lang_invoke_MethodHandleNatives_getMembers(JNIEnv *env, jclass clazz, jclass defc, jstring matchName, jstring matchSig, jint matchFlags, jclass caller, jint skip, jobjectArray results);
jlong JNICALL Java_java_lang_invoke_MethodHandleNatives_objectFieldOffset(JNIEnv *env, jclass clazz, jobject self);
jlong JNICALL Java_java_lang_invoke_MethodHandleNatives_staticFieldOffset(JNIEnv *env, jclass clazz, jobject self);
jobject JNICALL Java_java_lang_invoke_MethodHandleNatives_staticFieldBase(JNIEnv *env, jclass clazz, jobject self);
jobject JNICALL Java_java_lang_invoke_MethodHandleNatives_getMemberVMInfo(JNIEnv *env, jclass clazz, jobject self);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_setCallSiteTargetNormal(JNIEnv *env, jclass clazz, jobject callsite, jobject target);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_setCallSiteTargetVolatile(JNIEnv *env, jclass clazz, jobject callsite, jobject target);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_copyOutBootstrapArguments(JNIEnv *env, jclass clazz, jclass caller, jintArray indexInfo, jint start, jint end, jobjectArray buf, jint pos, jboolean resolve, jobject ifNotAvailable);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_clearCallSiteContext(JNIEnv *env, jclass clazz, jobject context);
jint JNICALL Java_java_lang_invoke_MethodHandleNatives_getNamedCon(JNIEnv *env, jclass clazz, jint which, jobjectArray name);
void JNICALL Java_java_lang_invoke_MethodHandleNatives_registerNatives(JNIEnv *env, jclass clazz);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

/* java_lang_invoke_VarHandle.c */
#if defined(J9VM_OPT_METHOD_HANDLE)
jlong JNICALL Java_java_lang_invoke_FieldVarHandle_lookupField(JNIEnv *env, jobject handle, jclass lookupClass, jstring name, jstring signature, jclass type, jboolean isStatic, jclass accessClass);
jlong JNICALL Java_java_lang_invoke_FieldVarHandle_unreflectField(JNIEnv *env, jobject handle, jobject reflectField, jboolean isStatic);
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */
jobject JNICALL Java_java_lang_invoke_VarHandle_get(JNIEnv *env, jobject handle, jobject args);
void JNICALL Java_java_lang_invoke_VarHandle_set(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getVolatile(JNIEnv *env, jobject handle, jobject args);
void JNICALL Java_java_lang_invoke_VarHandle_setVolatile(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getOpaque(JNIEnv *env, jobject handle, jobject args);
void JNICALL Java_java_lang_invoke_VarHandle_setOpaque(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAcquire(JNIEnv *env, jobject handle, jobject args);
void JNICALL Java_java_lang_invoke_VarHandle_setRelease(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_compareAndSet(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_compareAndExchange(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_compareAndExchangeAcquire(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_compareAndExchangeRelease(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_weakCompareAndSet(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_weakCompareAndSetAcquire(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_weakCompareAndSetRelease(JNIEnv *env, jobject handle, jobject args);
jboolean JNICALL Java_java_lang_invoke_VarHandle_weakCompareAndSetPlain(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_getAndSet(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndSetAcquire(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndSetRelease(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_getAndAdd(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndAddAcquire(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndAddRelease(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseAnd(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseAndAcquire(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseAndRelease(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseOr(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseOrAcquire(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseOrRelease(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseXor(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseXorAcquire(JNIEnv *env, jobject handle, jobject args);
jobject JNICALL Java_java_lang_invoke_VarHandle_getAndBitwiseXorRelease(JNIEnv *env, jobject handle, jobject args);

jobject JNICALL Java_java_lang_invoke_VarHandle_addAndGet(JNIEnv *env, jobject handle, jobject args);

/* java_lang_ref_Finalizer.c */
void JNICALL Java_lang_ref_Finalizer_runAllFinalizersImpl(JNIEnv *env, jclass recv);
void JNICALL Java_lang_ref_Finalizer_runFinalizationImpl(JNIEnv *env, jclass recv);

/* sun_misc_URLClassLoader.c */
jobject JNICALL Java_sun_misc_URLClassPath_getLookupCacheURLs(JNIEnv *env, jobject unusedObject, jobject classLoader);

/* sun_reflect_ConstantPool.c */
jint JNICALL Java_sun_reflect_ConstantPool_getSize0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop);
jclass JNICALL Java_sun_reflect_ConstantPool_getClassAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jclass JNICALL Java_sun_reflect_ConstantPool_getClassAtIfLoaded0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getMethodAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getMethodAtIfLoaded0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getFieldAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getFieldAtIfLoaded0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getMemberRefInfoAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jint JNICALL Java_sun_reflect_ConstantPool_getIntAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jlong JNICALL Java_sun_reflect_ConstantPool_getLongAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jfloat JNICALL Java_sun_reflect_ConstantPool_getFloatAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jdouble JNICALL Java_sun_reflect_ConstantPool_getDoubleAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getStringAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_sun_reflect_ConstantPool_getUTF8At0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jint JNICALL Java_java_lang_invoke_MethodHandleResolver_getCPTypeAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_java_lang_invoke_MethodHandleResolver_getCPMethodTypeAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_java_lang_invoke_MethodHandleResolver_getCPMethodHandleAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_java_lang_invoke_MethodHandleResolver_getCPClassNameAt(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_java_lang_invoke_MethodHandleResolver_getCPConstantDynamicAt(JNIEnv *env, jclass unusedClass, jobject constantPoolOop, jint cpIndex);
jint JNICALL Java_jdk_internal_reflect_ConstantPool_getClassRefIndexAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jint JNICALL Java_jdk_internal_reflect_ConstantPool_getNameAndTypeRefIndexAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jobject JNICALL Java_jdk_internal_reflect_ConstantPool_getNameAndTypeRefInfoAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jbyte JNICALL Java_jdk_internal_reflect_ConstantPool_getTagAt0(JNIEnv *env, jobject unusedObject, jobject constantPoolOop, jint cpIndex);
jint registerJdkInternalReflectConstantPoolNatives(JNIEnv *env);

/* java_lang_Access.c */
jobject JNICALL Java_java_lang_Access_getConstantPool(JNIEnv *env, jobject jlAccess, jobject classToIntrospect);

/* orbvmhelpers.c */
jboolean JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_is32Bit(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBitsInReferenceField(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInReferenceField(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBitsInDescriptionWord(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInDescriptionWord(JNIEnv *env, jclass rcv);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getNumBytesInJ9ObjectHeader(JNIEnv *env, jclass rcv);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass64(JNIEnv *env, jclass rcv, jclass c);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getTotalInstanceSizeFromJ9Class64(JNIEnv *env, jclass rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class64(JNIEnv *env, jclass rcv, jlong j9clazz);
jlong JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr64(JNIEnv *env, jclass rcv, jlong descriptorPtr);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getJ9ClassFromClass32(JNIEnv *env, jclass rcv, jclass c);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getTotalInstanceSizeFromJ9Class32(JNIEnv *env, jclass rcv, jint j9clazz);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getInstanceDescriptionFromJ9Class32(JNIEnv *env, jclass rcv, jint j9clazz);
jint JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_getDescriptionWordFromPtr32(JNIEnv *env, jclass rcv, jint descriptorPtr);
jlong JNICALL Java_com_ibm_rmi_io_IIOPInputStream_00024LUDCLStackWalkOptimizer_LUDCLMarkFrame(JNIEnv *env, jclass rcv);
jboolean JNICALL Java_com_ibm_rmi_io_IIOPInputStream_00024LUDCLStackWalkOptimizer_LUDCLUnmarkFrameImpl(JNIEnv *env, jclass rcv, jlong previousValue);
jobject JNICALL Java_com_ibm_oti_vm_ORBVMHelpers_LatestUserDefinedLoader(JNIEnv *env, jclass rcv);


/* com_ibm_oti_vm_VM.c */
jobjectArray JNICALL Java_com_ibm_oti_vm_VM_getVMArgsImpl(JNIEnv *env, jobject recv);

/* rcmnatives.c */
jlong JNICALL Java_javax_rcm_CPUThrottlingRunnable_requestToken(JNIEnv *env, jobject runnable, jlong tokenNumber);
jlong JNICALL Java_javax_rcm_CPUThrottlingRunnable_getTokenBucketLimit(JNIEnv *env, jclass clazz, jlong resourceHandle);
jlong JNICALL Java_javax_rcm_CPUThrottlingRunnable_getTokenBucketInterval(JNIEnv *env, jclass clazz, jlong resourceHandle);
/* thread.c */
void JNICALL Java_java_lang_Thread_yield(JNIEnv *env, jclass threadClass);

/* java_lang_Class.c */
jboolean JNICALL
Java_java_lang_Class_isClassADeclaredClass(JNIEnv *env, jobject jlClass, jobject aClass);
#if JAVA_SPEC_VERSION >= 15
jboolean JNICALL
Java_java_lang_Class_isHiddenImpl(JNIEnv *env, jobject recv);
#endif /* JAVA_SPEC_VERSION >= 15 */

/* Virtualization_management_HypervisorMXBean */
extern J9_CFUNC jint JNICALL
Java_com_ibm_virtualization_management_internal_HypervisorMXBeanImpl_isEnvironmentVirtualImpl(JNIEnv *env, jobject obj);
extern J9_CFUNC jstring JNICALL
Java_com_ibm_virtualization_management_internal_HypervisorMXBeanImpl_getVendorImpl(JNIEnv *env, jobject obj);

/* extendedosmbean.c */
jobject JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getTotalProcessorUsageImpl(JNIEnv *env, jobject instance, jobject procTotalObject);

jobjectArray JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getProcessorUsageImpl(JNIEnv *env, jobject instance, jobjectArray procUsageArray);

jobject JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getMemoryUsageImpl(JNIEnv *env, jobject instance, jobject memUsageObject);

jint JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getOnlineProcessorsImpl(JNIEnv *env, jobject instance);

jstring JNICALL
Java_com_ibm_lang_management_internal_ExtendedOperatingSystemMXBeanImpl_getHardwareModelImpl(JNIEnv *env, jobject obj);

/**
 * Returns the maximum number of file descriptors that can be opened in a process.
 * 
 * Class: com_ibm_lang_management_UnixOperatingSystemMXBean
 * Method: getMaxFileDescriptorCountImpl
 *
 * @param[in] env The JNI env.
 * @param[in] theClass The bean class.
 *
 * @return The maximum number of file descriptors that can be opened in a process;
 * -1 on failure.  If this is set to unlimited on the OS, simply return the signed 
 * integer (long long) limit.
 */
jlong JNICALL
Java_com_ibm_lang_management_internal_UnixExtendedOperatingSystem_getMaxFileDescriptorCountImpl(JNIEnv *env, jclass theClass);

/**
 * Returns the current number of file descriptors that are in opened state.
 *
 * Class: com_ibm_lang_management_UnixOperatingSystemMXBean
 * Method: getOpenFileDescriptorCountImpl
 *
 * @param[in] env The JNI env.
 * @param[in] theClass The bean class.
 *
 * @return The current number of file descriptors that are in opened state;
 * -1 on failure.
 */
jlong JNICALL
Java_com_ibm_lang_management_internal_UnixExtendedOperatingSystem_getOpenFileDescriptorCountImpl(JNIEnv *env, jclass theClass);

/* mgmthypervisor.c */
jobject JNICALL
Java_com_ibm_virtualization_management_internal_GuestOS_retrieveProcessorUsageImpl(JNIEnv *env, jobject beanInstance, jobject procUsageObject);

/* CRIUSupport.c */
jboolean JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_isCRIUSupportEnabledImpl(JNIEnv *env, jclass unused);

jboolean JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_isCheckpointAllowed(JNIEnv *env, jclass unused);

jobject JNICALL
Java_org_eclipse_openj9_criu_CRIUSupport_checkpointJVMImpl(JNIEnv *env, jobject criuSupport);

/* com_ibm_jvm_Stats.c */
void JNICALL
Java_com_ibm_jvm_Stats_getStats(JNIEnv *env, jobject obj);

/* gpu.c */
jint JNICALL Java_com_ibm_gpu_Kernel_launch(JNIEnv *env, jobject thisObject, jobject invokeObject, jobject mymethod, jint deviceId, jint gridDimX, jint gridDimY, jint gridDimZ, jint blockDimX, jint blockDimY, jint blockDimZ, jintArray argSizes, jlongArray argValues);

void JNICALL Java_java_util_stream_IntPipeline_promoteGPUCompile(JNIEnv *env, jclass className);

jobject JNICALL
Java_com_ibm_virtualization_management_internal_GuestOS_retrieveMemoryUsageImpl(JNIEnv *env, jobject beanInstance, jobject memUsageObject);


/* mgmtthread.c */
/**
 * Returns the CPU usage of all attached jvm threads split into different categories.
 *
 * @param env						The JNI env.
 * @param beanInstance				beanInstance.
 * @param jvmCpuMonitorInfoObject 	The jvmCpuMonitorInfo object that needs to be filled in.
 * @return 							The jvmCpuMonitorInfo object that is filled in on success or NULL on failure.
 */
jobject JNICALL
Java_com_ibm_lang_management_internal_JvmCpuMonitor_getThreadsCpuUsageImpl(JNIEnv *env, jobject beanInstance, jobject jvmCpuMonitorInfoObject);
/**
 * Sets the thread category of the given threadID to the one that is passed.
 *
 * @param env			The JNI env.
 * @param beanInstance	beanInstance.
 * @param threadID		The thread ID of the thread whose category needs to be set.
 * @param category		The category to be set to.
 * @return 				0 on success -1 on failure.
 */
jint JNICALL
Java_com_ibm_lang_management_internal_JvmCpuMonitor_setThreadCategoryImpl(JNIEnv *env, jobject beanInstance, jlong threadID, jint category);
/**
 * Returns the category of the thread whose threadID is passed.
 *
 * @param env			The JNI env.
 * @param beanInstance	beanInstance.
 * @param threadID 		The thread ID of the thread whose category needs to be returned.
 * @return 				The category on success or -1 on failure.
 */
jint JNICALL
Java_com_ibm_lang_management_internal_JvmCpuMonitor_getThreadCategoryImpl(JNIEnv *env, jobject beanInstance, jlong threadID);
/**
 * Sets the category of the current thread as J9THREAD_CATEGORY_SYSTEM_THREAD.
 *
 * @param env	The JNI env.
 * @return		0 on success -1 on failure.
 */
jint JNICALL
Java_com_ibm_oti_vm_VM_markCurrentThreadAsSystemImpl(JNIEnv *env);

#if JAVA_SPEC_VERSION >= 16
jboolean JNICALL
Java_java_lang_ref_Reference_refersTo(JNIEnv *env, jobject reference, jobject target);

void JNICALL
Java_jdk_internal_misc_ScopedMemoryAccess_registerNatives(JNIEnv *env, jclass clazz);

jboolean JNICALL
Java_jdk_internal_misc_ScopedMemoryAccess_closeScope0(JNIEnv *env, jobject instance, jobject scope, jobject exception);
#endif /* JAVA_SPEC_VERSION >= 16 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* JCLPROTS_H */

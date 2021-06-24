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

#include <string.h>
#include "j9.h"
#include "j9port.h"
#include "rommeth.h"
#include "ut_j9vm.h"
#include "vm_internal.h"
#include "j9cp.h"
#include "j9consts.h"
#include "OutOfLineINL.hpp"
#include "VMHelpers.hpp"
#include "AtomicSupport.hpp"
#include "OMR/Bytes.hpp"

extern "C" {

#if defined(J9HAMMER)
#define J9CPU_AMD64
#elif defined(WIN32) || defined(J9X86) 
#define J9CPU_IA32
#endif

#ifdef J9VM_NEEDS_JNI_REDIRECTION
#if defined(J9CPU_IA32)
#define J9JNIREDIRECT_SEQUENCE_ALIGNMENT 8
#elif defined(J9CPU_ARM)
#define J9JNIREDIRECT_SEQUENCE_ALIGNMENT 8
#elif defined(J9CPU_AMD64)
#define J9JNIREDIRECT_SEQUENCE_ALIGNMENT 16
#else
#error Unknown platform
#endif

#if J9JNIREDIRECT_SEQUENCE_ALIGNMENT < J9JNIREDIRECT_REQUIRED_ALIGNMENT
#error J9JNIREDIRECT_SEQUENCE_ALIGNMENT must be >= J9JNIREDIRECT_REQUIRED_ALIGNMENT
#endif
#if (J9JNIREDIRECT_SEQUENCE_ALIGNMENT % J9JNIREDIRECT_REQUIRED_ALIGNMENT) != 0
#error J9JNIREDIRECT_SEQUENCE_ALIGNMENT must be a multiple of J9JNIREDIRECT_REQUIRED_ALIGNMENT
#endif
#endif

/**
 * Prototypes for locally defined functions.
 */
static IDATA mangledSize (U_8 * data, U_16 length, BOOLEAN legacyMangling);
static void mangledData (U_8** pBuffer, U_8 * data, U_16 length);
static void nativeSignature(J9Method* nativeMethod, char *resultBuffer);
static UDATA nativeMethodHash(void *key, void *userData);
static UDATA nativeMethodEqual(void *leftKey, void *rightKey, void *userData);
static UDATA bindNative(J9VMThread *currentThread, J9Method *nativeMethod, char * longJNI, char * shortJNI, UDATA bindJNINative);
static UDATA lookupNativeAddress(J9VMThread *currentThread, J9Method *nativeMethod, J9NativeLibrary *handle, char *longJNI, char *shortJNI, UDATA functionArgCount, UDATA bindJNINative);

#if JAVA_SPEC_VERSION >= 15
J9_DECLARE_CONSTANT_UTF8(j9_findnative_sig, "(Ljava/lang/ClassLoader;Ljava/lang/String;)J");
J9_DECLARE_CONSTANT_UTF8(j9_findnative_name, "findNative");
#endif /* JAVA_SPEC_VERSION >= 15 */

typedef struct {
	const char *nativeName;
	UDATA sendTargetNumber;
} inlMapping;

static inlMapping mappings[] = {
	{ "Java_java_lang_Thread_currentThread__", J9_BCLOOP_SEND_TARGET_INL_THREAD_CURRENT_THREAD },
	{ "Java_java_lang_Object_getClass__", J9_BCLOOP_SEND_TARGET_INL_OBJECT_GET_CLASS },
	{ "Java_java_lang_Class_isAssignableFrom__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_CLASS_IS_ASSIGNABLE_FROM },
	{ "Java_java_lang_Class_isArray__", J9_BCLOOP_SEND_TARGET_INL_CLASS_IS_ARRAY },
	{ "Java_java_lang_Class_isPrimitive__", J9_BCLOOP_SEND_TARGET_INL_CLASS_IS_PRIMITIVE },
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	{ "Java_java_lang_Class_isPrimitiveClass__", J9_BCLOOP_SEND_TARGET_INL_CLASS_IS_PRIMITIVE_CLASS },
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	{ "Java_java_lang_Class_getModifiersImpl__", J9_BCLOOP_SEND_TARGET_INL_CLASS_GET_MODIFIERS_IMPL },
	{ "Java_java_lang_Class_getComponentType__", J9_BCLOOP_SEND_TARGET_INL_CLASS_GET_COMPONENT_TYPE },
	{ "Java_java_lang_Class_arrayTypeImpl__", J9_BCLOOP_SEND_TARGET_CLASS_ARRAY_TYPE_IMPL },
	{ "Java_java_lang_Class_isRecordImpl__", J9_BCLOOP_SEND_TARGET_CLASS_IS_RECORD_IMPL },
	{ "Java_java_lang_Class_isSealed__", J9_BCLOOP_SEND_TARGET_CLASS_IS_SEALED },
	{ "Java_java_lang_System_arraycopy__Ljava_lang_Object_2ILjava_lang_Object_2II", J9_BCLOOP_SEND_TARGET_INL_SYSTEM_ARRAYCOPY },
	{ "Java_java_lang_System_currentTimeMillis__", J9_BCLOOP_SEND_TARGET_INL_SYSTEM_CURRENT_TIME_MILLIS },
	{ "Java_java_lang_System_nanoTime__", J9_BCLOOP_SEND_TARGET_INL_SYSTEM_NANO_TIME },
	{ "Java_java_lang_J9VMInternals_getSuperclass__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_GET_SUPERCLASS },
	{ "Java_java_lang_J9VMInternals_identityHashCode__Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_IDENTITY_HASH_CODE },
	{ "Java_java_lang_String_intern__", J9_BCLOOP_SEND_TARGET_INL_STRING_INTERN },
	{ "Java_java_lang_Throwable_fillInStackTrace__", J9_BCLOOP_SEND_TARGET_INL_THROWABLE_FILL_IN_STACK_TRACE },
	{ "Java_java_lang_J9VMInternals_newInstanceImpl__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_NEWINSTANCEIMPL },
	{ "Java_java_lang_J9VMInternals_primitiveClone__Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_PRIMITIVE_CLONE },
	{ "Java_java_lang_ref_Reference_getImpl__", J9_BCLOOP_SEND_TARGET_INL_REFERENCE_GETIMPL },
	{ "Java_sun_misc_Unsafe_getByte__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE_NATIVE },
	{ "Java_sun_misc_Unsafe_getByte__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE },
	{ "Java_sun_misc_Unsafe_getByteVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE_VOLATILE },
	{ "Java_sun_misc_Unsafe_putByte__JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE_NATIVE },
	{ "Java_sun_misc_Unsafe_putByte__Ljava_lang_Object_2JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE },
	{ "Java_sun_misc_Unsafe_putByteVolatile__Ljava_lang_Object_2JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE_VOLATILE },
	{ "Java_sun_misc_Unsafe_getBoolean__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBOOLEAN },
	{ "Java_sun_misc_Unsafe_getBooleanVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBOOLEAN_VOLATILE },
	{ "Java_sun_misc_Unsafe_putBoolean__Ljava_lang_Object_2JZ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBOOLEAN },
	{ "Java_sun_misc_Unsafe_putBooleanVolatile__Ljava_lang_Object_2JZ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBOOLEAN_VOLATILE },
	{ "Java_sun_misc_Unsafe_getShort__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT_NATIVE },
	{ "Java_sun_misc_Unsafe_getShort__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT },
	{ "Java_sun_misc_Unsafe_getShortVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT_VOLATILE },
	{ "Java_sun_misc_Unsafe_putShort__JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT_NATIVE },
	{ "Java_sun_misc_Unsafe_putShort__Ljava_lang_Object_2JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT },
	{ "Java_sun_misc_Unsafe_putShortVolatile__Ljava_lang_Object_2JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT_VOLATILE },
	{ "Java_sun_misc_Unsafe_getChar__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR_NATIVE },
	{ "Java_sun_misc_Unsafe_getChar__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR },
	{ "Java_sun_misc_Unsafe_getCharVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR_VOLATILE },
	{ "Java_sun_misc_Unsafe_putChar__JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR_NATIVE },
	{ "Java_sun_misc_Unsafe_putChar__Ljava_lang_Object_2JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR },
	{ "Java_sun_misc_Unsafe_putCharVolatile__Ljava_lang_Object_2JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR_VOLATILE },
	{ "Java_sun_misc_Unsafe_getInt__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT_NATIVE },
	{ "Java_sun_misc_Unsafe_getInt__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT },
	{ "Java_sun_misc_Unsafe_getIntVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT_VOLATILE },
	{ "Java_sun_misc_Unsafe_putInt__JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_NATIVE },
	{ "Java_sun_misc_Unsafe_putInt__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT },
	{ "Java_sun_misc_Unsafe_putIntVolatile__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_VOLATILE },
	/* Implement putOrderedInt as putIntVolatile - parameters are identical */
	{ "Java_sun_misc_Unsafe_putOrderedInt__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_VOLATILE },
	{ "Java_sun_misc_Unsafe_getFloat__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT_NATIVE },
	{ "Java_sun_misc_Unsafe_getFloat__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT },
	{ "Java_sun_misc_Unsafe_getFloatVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT_VOLATILE },
	{ "Java_sun_misc_Unsafe_putFloat__JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT_NATIVE },
	{ "Java_sun_misc_Unsafe_putFloat__Ljava_lang_Object_2JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT },
	{ "Java_sun_misc_Unsafe_putFloatVolatile__Ljava_lang_Object_2JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT_VOLATILE },
	{ "Java_sun_misc_Unsafe_getLong__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG_NATIVE },
	{ "Java_sun_misc_Unsafe_getLong__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG },
	{ "Java_sun_misc_Unsafe_getLongVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG_VOLATILE },
	{ "Java_sun_misc_Unsafe_putLong__JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_NATIVE },
	{ "Java_sun_misc_Unsafe_putLong__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG },
	{ "Java_sun_misc_Unsafe_putLongVolatile__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_VOLATILE },
	/* Implement putOrderedLong as putLongVolatile - parameters are identical */
	{ "Java_sun_misc_Unsafe_putOrderedLong__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_VOLATILE },
	{ "Java_sun_misc_Unsafe_getDouble__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE_NATIVE },
	{ "Java_sun_misc_Unsafe_getDouble__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE },
	{ "Java_sun_misc_Unsafe_getDoubleVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE_VOLATILE },
	{ "Java_sun_misc_Unsafe_putDouble__JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE_NATIVE },
	{ "Java_sun_misc_Unsafe_putDouble__Ljava_lang_Object_2JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE },
	{ "Java_sun_misc_Unsafe_putDoubleVolatile__Ljava_lang_Object_2JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE_VOLATILE },
	{ "Java_sun_misc_Unsafe_getObject__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT },
	{ "Java_sun_misc_Unsafe_getObjectVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT_VOLATILE },
	{ "Java_sun_misc_Unsafe_putObject__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT },
	{ "Java_sun_misc_Unsafe_putObjectVolatile__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT_VOLATILE },
	/* Implement putOrderedObject as putObjectVolatile - parameters are identical */
	{ "Java_sun_misc_Unsafe_putOrderedObject__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT_VOLATILE },
	{ "Java_sun_misc_Unsafe_getAddress__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETADDRESS },
	{ "Java_sun_misc_Unsafe_putAddress__JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTADDRESS },
	{ "Java_sun_misc_Unsafe_addressSize__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ADDRESS_SIZE },
	{ "Java_sun_misc_Unsafe_arrayBaseOffset__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_BASE_OFFSET },
	{ "Java_sun_misc_Unsafe_arrayIndexScale__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_INDEX_SCALE },
	{ "Java_sun_misc_Unsafe_loadFence__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_LOAD_FENCE },
	{ "Java_sun_misc_Unsafe_storeFence__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_STORE_FENCE },
	{ "Java_sun_misc_Unsafe_compareAndSwapObject__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPOBJECT },
	{ "Java_sun_misc_Unsafe_compareAndSwapLong__Ljava_lang_Object_2JJJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPLONG },
	{ "Java_sun_misc_Unsafe_compareAndSwapInt__Ljava_lang_Object_2JII", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPINT },
	{ "Java_java_lang_J9VMInternals_prepareClassImpl__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_PREPARE_CLASS_IMPL },
	{ "Java_java_lang_J9VMInternals_getInterfaces__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_INTERNALS_GET_INTERFACES },
	{ "Java_java_lang_reflect_Array_newArrayImpl__Ljava_lang_Class_2I", J9_BCLOOP_SEND_TARGET_INL_ARRAY_NEW_ARRAY_IMPL },
	{ "Java_java_lang_ClassLoader_findLoadedClassImpl__Ljava_lang_String_2", J9_BCLOOP_SEND_TARGET_INL_CLASSLOADER_FIND_LOADED_CLASS_IMPL },
	{ "Java_java_lang_VMAccess_findClassOrNull__Ljava_lang_String_2Ljava_lang_ClassLoader_2", J9_BCLOOP_SEND_TARGET_INL_VM_FIND_CLASS_OR_NULL },
	{ "Java_java_lang_Class_forNameImpl__Ljava_lang_String_2ZLjava_lang_ClassLoader_2", J9_BCLOOP_SEND_TARGET_CLASS_FORNAMEIMPL },
	{ "Java_java_lang_Thread_interrupted__", J9_BCLOOP_SEND_TARGET_INL_THREAD_INTERRUPTED },
	{ "Java_com_ibm_oti_vm_VM_getCPIndexImpl__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_VM_GET_CP_INDEX_IMPL},
	{ "Java_com_ibm_oti_vm_VM_getStackClassLoader__I", J9_BCLOOP_SEND_TARGET_INL_VM_GET_STACK_CLASS_LOADER },
	/* Forward duplicated getStackClassLoader natives to the same target */
	{ "Java_java_lang_ClassLoader_getStackClassLoader__I", J9_BCLOOP_SEND_TARGET_INL_VM_GET_STACK_CLASS_LOADER },
	{ "Java_org_apache_harmony_kernel_vm_VM_getStackClassLoader__I", J9_BCLOOP_SEND_TARGET_INL_VM_GET_STACK_CLASS_LOADER },
	{ "Java_java_lang_Object_notifyAll__", J9_BCLOOP_SEND_TARGET_INL_OBJECT_NOTIFY_ALL },
	{ "Java_java_lang_Object_notify__", J9_BCLOOP_SEND_TARGET_INL_OBJECT_NOTIFY },
	{ "Java_java_lang_Class_isInstance__Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_CLASS_IS_INSTANCE },
	{ "Java_java_lang_Class_getSimpleNameImpl__", J9_BCLOOP_SEND_TARGET_INL_CLASS_GET_SIMPLE_NAME_IMPL },
	{ "Java_com_ibm_oti_vm_VM_initializeClassLoader__Ljava_lang_ClassLoader_2IZ", J9_BCLOOP_SEND_TARGET_INL_VM_INITIALIZE_CLASS_LOADER },
	{ "Java_com_ibm_oti_vm_VM_getClassPathEntryType__Ljava_lang_Object_2I", J9_BCLOOP_SEND_TARGET_INL_VM_GET_CLASS_PATH_ENTRY_TYPE },
	{ "Java_com_ibm_oti_vm_VM_isBootstrapClassLoader__Ljava_lang_ClassLoader_2", J9_BCLOOP_SEND_TARGET_INL_VM_IS_BOOTSTRAP_CLASS_LOADER },
	{ "Java_sun_misc_Unsafe_allocateInstance__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ALLOCATE_INSTANCE },
	{ "Java_openj9_internal_tools_attach_target_Attachment_loadAgentLibraryImpl__Ljava_lang_ClassLoader_2Ljava_lang_String_2Ljava_lang_String_2Z", J9_BCLOOP_SEND_TARGET_INL_ATTACHMENT_LOADAGENTLIBRARYIMPL },
	{ "Java_com_ibm_oti_vm_VM_getStackClass__I", J9_BCLOOP_SEND_TARGET_INL_VM_GETSTACKCLASS },
	/* Forward duplicated getStackClass natives to the same target */
	{ "Java_java_lang_invoke_MethodHandles_getStackClass__I", J9_BCLOOP_SEND_TARGET_INL_VM_GETSTACKCLASS },
	{ "Java_java_lang_Class_getStackClass__I", J9_BCLOOP_SEND_TARGET_INL_VM_GETSTACKCLASS },
	{ "Java_java_lang_Thread_sleep__JI", J9_BCLOOP_SEND_TARGET_INL_THREAD_SLEEP },
	{ "Java_java_lang_Object_wait__JI", J9_BCLOOP_SEND_TARGET_INL_OBJECT_WAIT },
	{ "Java_java_lang_ClassLoader_loadLibraryWithPath___3BLjava_lang_ClassLoader_2_3B", J9_BCLOOP_SEND_TARGET_INL_CLASSLOADER_LOADLIBRARYWITHPATH },
	{ "Java_java_lang_Thread_isInterruptedImpl__", J9_BCLOOP_SEND_TARGET_INL_THREAD_ISINTERRUPTEDIMPL },
	{ "Java_java_lang_ClassLoader_initAnonClassLoader__Ljava_lang_InternalAnonymousClassLoader_2", J9_BCLOOP_SEND_TARGET_INL_CLASSLOADER_INITIALIZEANONCLASSLOADER },
	{ "Java_jdk_internal_misc_Unsafe_getByte__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getByte__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE },
	{ "Java_jdk_internal_misc_Unsafe_getByteVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBYTE_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putByte__JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putByte__Ljava_lang_Object_2JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE },
	{ "Java_jdk_internal_misc_Unsafe_putByteVolatile__Ljava_lang_Object_2JB", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBYTE_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getBoolean__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBOOLEAN },
	{ "Java_jdk_internal_misc_Unsafe_getBooleanVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETBOOLEAN_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putBoolean__Ljava_lang_Object_2JZ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBOOLEAN },
	{ "Java_jdk_internal_misc_Unsafe_putBooleanVolatile__Ljava_lang_Object_2JZ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTBOOLEAN_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getShort__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getShort__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT },
	{ "Java_jdk_internal_misc_Unsafe_getShortVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETSHORT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putShort__JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putShort__Ljava_lang_Object_2JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT },
	{ "Java_jdk_internal_misc_Unsafe_putShortVolatile__Ljava_lang_Object_2JS", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTSHORT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getChar__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getChar__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR },
	{ "Java_jdk_internal_misc_Unsafe_getCharVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETCHAR_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putChar__JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putChar__Ljava_lang_Object_2JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR },
	{ "Java_jdk_internal_misc_Unsafe_putCharVolatile__Ljava_lang_Object_2JC", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTCHAR_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getInt__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getInt__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT },
	{ "Java_jdk_internal_misc_Unsafe_getIntVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETINT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putInt__JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putInt__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT },
	{ "Java_jdk_internal_misc_Unsafe_putIntVolatile__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putOrderedInt__Ljava_lang_Object_2JI", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTINT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getFloat__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getFloat__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT },
	{ "Java_jdk_internal_misc_Unsafe_getFloatVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETFLOAT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putFloat__JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putFloat__Ljava_lang_Object_2JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT },
	{ "Java_jdk_internal_misc_Unsafe_putFloatVolatile__Ljava_lang_Object_2JF", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTFLOAT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getLong__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getLong__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG },
	{ "Java_jdk_internal_misc_Unsafe_getLongVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETLONG_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putLong__JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putLong__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG },
	{ "Java_jdk_internal_misc_Unsafe_putLongVolatile__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putOrderedLong__Ljava_lang_Object_2JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTLONG_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getDouble__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_getDouble__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE },
	{ "Java_jdk_internal_misc_Unsafe_getDoubleVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETDOUBLE_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putDouble__JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE_NATIVE },
	{ "Java_jdk_internal_misc_Unsafe_putDouble__Ljava_lang_Object_2JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE },
	{ "Java_jdk_internal_misc_Unsafe_putDoubleVolatile__Ljava_lang_Object_2JD", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTDOUBLE_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getObject__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_getObjectVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putObject__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_putObjectVolatile__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getReference__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_getReferenceVolatile__Ljava_lang_Object_2J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETOBJECT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putReference__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_putReferenceVolatile__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_putOrderedObject__Ljava_lang_Object_2JLjava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTOBJECT_VOLATILE },
	{ "Java_jdk_internal_misc_Unsafe_getAddress__J", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_GETADDRESS },
	{ "Java_jdk_internal_misc_Unsafe_putAddress__JJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_PUTADDRESS },
	{ "Java_jdk_internal_misc_Unsafe_addressSize__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ADDRESS_SIZE },
	{ "Java_jdk_internal_misc_Unsafe_addressSize0__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ADDRESS_SIZE },
	{ "Java_jdk_internal_misc_Unsafe_arrayBaseOffset__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_BASE_OFFSET },
	{ "Java_jdk_internal_misc_Unsafe_arrayBaseOffset0__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_BASE_OFFSET },
	{ "Java_jdk_internal_misc_Unsafe_arrayIndexScale__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_INDEX_SCALE },
	{ "Java_jdk_internal_misc_Unsafe_arrayIndexScale0__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ARRAY_INDEX_SCALE },
	{ "Java_jdk_internal_misc_Unsafe_loadFence__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_LOAD_FENCE },
	{ "Java_jdk_internal_misc_Unsafe_storeFence__", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_STORE_FENCE },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSwapObject__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSwapLong__Ljava_lang_Object_2JJJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPLONG },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSwapInt__Ljava_lang_Object_2JII", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPINT },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSetObject__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSetReference__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPOBJECT },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSetLong__Ljava_lang_Object_2JJJ", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPLONG },
	{ "Java_jdk_internal_misc_Unsafe_compareAndSetInt__Ljava_lang_Object_2JII", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_COMPAREANDSWAPINT },
	{ "Java_jdk_internal_misc_Unsafe_allocateInstance__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_UNSAFE_ALLOCATE_INSTANCE },
	{ "Java_java_lang_Thread_onSpinWait__", J9_BCLOOP_SEND_TARGET_INL_THREAD_ON_SPIN_WAIT },
#if JAVA_SPEC_VERSION >= 11
	{ "Java_jdk_internal_reflect_Reflection_getClassAccessFlags__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_REFLECTION_GETCLASSACCESSFLAGS },
#else /* JAVA_SPEC_VERSION >= 11 */
	{ "Java_sun_reflect_Reflection_getClassAccessFlags__Ljava_lang_Class_2", J9_BCLOOP_SEND_TARGET_INL_REFLECTION_GETCLASSACCESSFLAGS },
#endif /* JAVA_SPEC_VERSION >= 11 */
};

typedef struct J9OutOfLineINLMapping {
	const char *nativeName;
	J9OutOfLineINLMethod *targetMethod;
} J9OutOfLineINLMapping;

static J9OutOfLineINLMapping outOfLineINLmappings[] = {
	{ "Java_jdk_internal_misc_Unsafe_fullFence__", OutOfLineINL_jdk_internal_misc_Unsafe_fullFence },
	{ "Java_sun_misc_Unsafe_fullFence__", OutOfLineINL_jdk_internal_misc_Unsafe_fullFence },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeObject__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeReference__Ljava_lang_Object_2JLjava_lang_Object_2Ljava_lang_Object_2", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeObjectVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeIntVolatile__Ljava_lang_Object_2JII", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeIntVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeInt__Ljava_lang_Object_2JII", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeIntVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeLongVolatile__Ljava_lang_Object_2JJJ", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeLongVolatile },
	{ "Java_jdk_internal_misc_Unsafe_compareAndExchangeLong__Ljava_lang_Object_2JJJ", OutOfLineINL_jdk_internal_misc_Unsafe_compareAndExchangeLongVolatile },
	{ "Java_com_ibm_jit_JITHelpers_acmplt__Ljava_lang_Object_2Ljava_lang_Object_2", OutOfLineINL_com_ibm_jit_JITHelpers_acmplt },
};

static UDATA
inlIntercept(J9VMThread *currentThread, J9Method *nativeMethod, const char *symbolName)
{
	UDATA rc = J9_NATIVE_METHOD_BIND_FAIL;

	/* INL method with unique send target */
	for (UDATA i = 0; i < sizeof(mappings) / sizeof(inlMapping); ++i) {
		if (0 == strcmp(symbolName, mappings[i].nativeName)) {
			Trc_VM_INLIntercepted(currentThread, symbolName);
			nativeMethod->methodRunAddress = J9_BCLOOP_ENCODE_SEND_TARGET(mappings[i].sendTargetNumber);
			nativeMethod->extra = reinterpret_cast<void*>(J9_JIT_NEVER_TRANSLATE);
			rc = J9_NATIVE_METHOD_BIND_SUCCESS;
			break;
		}
	}

	/* INL method with generic send target */
	for (UDATA i = 0; i < sizeof(outOfLineINLmappings) / sizeof(J9OutOfLineINLMapping); ++i) {
		if (0 == strcmp(symbolName, outOfLineINLmappings[i].nativeName)) {
			Trc_VM_OutOfLineINLIntercepted(currentThread, symbolName);
			nativeMethod->methodRunAddress = J9_BCLOOP_ENCODE_SEND_TARGET(J9_BCLOOP_SEND_TARGET_OUT_OF_LINE_INL);
			void *functionAddress = (void *)outOfLineINLmappings[i].targetMethod;
#if defined(J9VM_NEEDS_JNI_REDIRECTION)
			if (((UDATA)functionAddress) & (J9JNIREDIRECT_REQUIRED_ALIGNMENT - 1)) {
				functionAddress = alignJNIAddress(currentThread->javaVM, functionAddress, J9_CLASS_FROM_METHOD(nativeMethod)->classLoader);
				if (NULL == functionAddress) {
					/* Need to propagate the OOM back out */
					rc = J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY;
					break;
				}
			}
#endif
			nativeMethod->extra = (void *)((UDATA)functionAddress | J9_STARTPC_NOT_TRANSLATED);
			rc = J9_NATIVE_METHOD_BIND_SUCCESS;
			break;
		}
	}
	return rc;
}

/**
 * Constructs multiple mangled JNI names for the \c ramMethod to a single buffer
 * where each name is separated by null characters.  This function dynamically
 * allocates the buffer, which the caller is responsible to free.
 *
 * \param javaVM
 * \param ramMethod The method for signature computation.
 * \param ramClass The class which contains the method.
 * \param nameOffset Buffer into which the mangled signature will be written.
 * \return A buffer containing the mangled JNI names, separated by nulls
 *         Failure is indicated by:
 *            NULL (failure to allocate native memory)
 *            UDATA_MAX (identifiers are invalid)
 *
 * Sample Input:
 * 			boolean pkg.X.foo(String,int);
 *
 * Sample Output:
 * 			Java_pkg_X_foo__Ljava_lang_String_2_3IZ '\0'
 * 			Java_pkg_X_foo'\0'
 */
U_8*
buildNativeFunctionNames(J9JavaVM * javaVM, J9Method* ramMethod, J9Class* ramClass, UDATA nameOffset)
{
	J9ROMMethod* romMethod;
	J9UTF8 *methodName, *methodSig, *className;
	U_8 *buffer, *current;
	UDATA size, shortSize;
	U_8 *classNameData, *methodNameData, *methodSigData;
	U_16 classNameLength, methodNameLength, methodSigLength;
	IDATA tempSize = 0;
	BOOLEAN legacyMangling = J9_ARE_ANY_BITS_SET(javaVM->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_LEGACY_MANGLING);

	PORT_ACCESS_FROM_JAVAVM(javaVM);

	className = J9ROMCLASS_CLASSNAME(ramClass->romClass);
	classNameData = J9UTF8_DATA(className);
	classNameLength = J9UTF8_LENGTH(className);
	romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(ramMethod);
	methodName = J9ROMMETHOD_NAME(romMethod);
	methodNameData = J9UTF8_DATA(methodName) + nameOffset;
	methodNameLength = J9UTF8_LENGTH(methodName) - (U_16) nameOffset;
	methodSig = J9ROMMETHOD_SIGNATURE(romMethod);
	methodSigData = J9UTF8_DATA(methodSig);
	methodSigLength = J9UTF8_LENGTH(methodSig);

	/* First add the common part: Java_<cls>_<name> */
	size = 6;
	tempSize = mangledSize(classNameData, classNameLength, legacyMangling);
	if (tempSize < 0) {
error:
		buffer = (U_8*)UDATA_MAX;
		goto done;
	}
	size += tempSize;
	tempSize = mangledSize(methodNameData, methodNameLength, legacyMangling);
	if (tempSize < 0) {
		goto error;
	}
	size += tempSize;
	shortSize = size;
	size <<= 1;

	/* Now add for the signature part: __<parm1><parm2> */
	size += 2;
	tempSize = mangledSize(methodSigData, methodSigLength, legacyMangling);
	if (tempSize < 0) {
		goto error;
	}
	size += tempSize;

	/* Null terminate both strings */
	size += 2;

	/* Allocate enough memory */
	buffer = (U_8*)j9mem_allocate_memory(size, OMRMEM_CATEGORY_VM);
	if (NULL == buffer) {
		goto done;
	}

	/* Dump the long name */
	current = buffer;
	memcpy(current, "Java_", 5);
	current += 5;
	mangledData(&current, classNameData, classNameLength);
	*current++ = '_';
	mangledData(&current, methodNameData, methodNameLength);
	*current++ = '_';
	*current++ = '_';
	mangledData(&current, methodSigData, methodSigLength);
	*current++ = '\0';

	/* Memcopy the short name from the long name */
	memcpy(current, buffer, shortSize);
	current[shortSize] = '\0';
done:
	return buffer;
}


/**
 * Constructs the mangled JNI name from a Java method signature.
 * \param pBuffer Buffer into which the mangled signature will be written.
 * \param data  Method signature in canonical UTF8 form.
 * \param length The number of bytes in data.
 */
static void
mangledData(U_8** pBuffer, U_8 * data, U_16 length)
{
	const char* hexDigits = "0123456789abcdef";
	U_8* buffer = *pBuffer;
	U_16 i, ch, temp;

	i = 0;
	while(i < length) {
		ch = data[i++];
		switch(ch) {
			case '(':										/* Start of signature -- ignore */
				break;

			case ')':										/* End of signature -- done */
				*pBuffer = buffer;
				return;

			case '/':										/* / -> _ */
				*buffer++ = '_';
				break;

			case '_':										/* _ -> _1 */
				*buffer++ = '_';
				*buffer++ = '1';
				break;

			case ';':										/* ; -> _2 */
				*buffer++ = '_';
				*buffer++ = '2';
				break;

			case '[':										/* [ -> _3 */
				*buffer++ = '_';
				*buffer++ = '3';
				break;

			case '$':										/* $ -> _00024 */
				*buffer++ = '_';
				*buffer++ = '0';
				*buffer++ = '0';
				*buffer++ = '0';
				*buffer++ = '2';
				*buffer++ = '4';
				break;

			default:
				if(ch > 0x7F) {
					/* Multibyte encoding: two or three bytes? */
					temp = data[i++];
					if((ch & 0xE0) == 0xE0) {
						ch = (((ch & 0x1F) << 6) + (temp & 0x3F)) << 6;
						temp = data[i++];
						ch += temp & 0x3F;
					} else {
						ch = ((ch & 0x1F) << 6) + (temp & 0x3F);
					}

					/* _0xxxx */
					*buffer++ = '_';
					*buffer++ = '0';
					*buffer++ = hexDigits[(ch & 0xF000) >> 12];
					*buffer++ = hexDigits[(ch & 0x0F00) >> 8];
					*buffer++ = hexDigits[(ch & 0x00F0) >> 4];
					*buffer++ = hexDigits[ch & 0x000F];
				} else {
					*buffer++ = ch & 0x7F;
				}
		}
	}

	*pBuffer = buffer;
	return;
}


/**
 * Return the number of ASCII characters required to construct the
 * mangled JNI name for a method.
 * \param data  Method signature in canonical UTF8 form.
 * \param length The number of bytes in data.
 * \return The number of ASCII characters required for the mangled JNI name, or a negative value for illegal identifiers
 */
static IDATA
mangledSize(U_8 * data, U_16 length, BOOLEAN legacyMangling)
{
	/* Return the size in ASCII characters of the JNI mangled version of utf
		Assumes verified canonical UTF8 strings */
	IDATA size;
	U_16 i, ch;
	BOOLEAN disallow0to3 = !legacyMangling;

	size = 0;
	i = 0;
	while(i < length) {
		ch = data[i++];
		switch(ch) {
			case '(':										/* Start of signature -- ignore */
				break;

			case ')':										/* End of signature -- done */
				goto done;

			case '/':										/* / -> _ */
				size += 1;
				disallow0to3 = !legacyMangling;
				continue;

			case '_':										/* _ -> _1 */
			case ';':										/* ; -> _2 */
			case '[':										/* [ -> _3 */
				size += 2;
				break;

			case '$':										/* $ -> _u0024 */
				size += 6;
				break;

			default:
				if(ch > 0x7F) {
					size += 6;			/* _0xxxx */

					/* Multibyte encoding: two or three bytes? */
					i += 1;
					if((ch & 0xE0) == 0xE0) i += 1;
				} else {
					if (disallow0to3) {
						if ((ch >= (U_16)'0') && (ch <= (U_16)'3')) {
							size = -1;
							goto done;
						}
					}
					size += 1;
				}
		}
		disallow0to3 = FALSE;
	}
done:
	return size;
}


/**
 * Computes the appropriate signature for j9sl_lookup_name() from the supplied
 * \c nativeMethod, writing the signature into \c resultBuffer.
 *
 * \param nativeMethod The native method to register.
 * \param resultBuffer The buffer into which the native signature is written,
 * which must be at least (args + 2) bytes long [args + retval + trailing null]
 */
static void
nativeSignature(J9Method* nativeMethod, char *resultBuffer)
{
	J9UTF8 *methodSig;
	UDATA arg;
	U_16 i, ch;
	BOOLEAN parsingReturnType = FALSE, processingBracket = FALSE;
	char nextType = '\0';

	methodSig = J9ROMMETHOD_SIGNATURE(J9_ROM_METHOD_FROM_RAM_METHOD(nativeMethod));

	i = 0;
	arg = 3; /* skip the return type slot and JNI standard slots, they will be filled in later. */

	while(i < J9UTF8_LENGTH(methodSig)) {
		ch = J9UTF8_DATA(methodSig)[i++];
		switch(ch) {
			case '(':										/* start of signature -- skip */
				continue;
			case ')':										/* End of signature -- done args, find return type */
				parsingReturnType = TRUE;
				continue;
			case 'L':
				nextType = 'L';
				while(J9UTF8_DATA(methodSig)[i++] != ';') {}		/* a type string - loop scanning for ';' to end it - i points past ';' when done loop */
				break;
			case 'Z':
				nextType = 'Z';
				break;
			case 'B':
				nextType = 'B';
				break;
			case 'C':
				nextType = 'c';
				break;
			case 'S':
				nextType = 'C';
				break;
			case 'I':
				nextType = 'I';
				break;
			case 'J':
				nextType = 'J';
				break;
			case 'F':
				nextType = 'F';
				break;
			case 'D':
				nextType = 'D';
				break;
			case '[':
				processingBracket = TRUE;
				continue;				/* go back to top of loop for next char */
			case 'V':
				if(!parsingReturnType) {
				Trc_VM_NativeSignature_BadSig(NULL, i-1);
				}
				nextType = 'V';
				break;

			default:
				nextType = '\0';
				Trc_VM_NativeSignature_BadChar(NULL, ch, i-1);
				break;
		}
		if(processingBracket) {
			if(parsingReturnType) {
				resultBuffer[0] = 'L';
				break;			/* from the while loop */
			} else {
				resultBuffer[arg] = 'L';
				arg++;
				processingBracket = FALSE;
			}
		} else if(parsingReturnType) {
			resultBuffer[0] = nextType;
			break;			/* from the while loop */
		} else {
			resultBuffer[arg] = nextType;
			arg++;
		}
	}

	resultBuffer[1] = 'L'; 	/* the JNIEnv */
	resultBuffer[2] = 'L';	/* the jobject or jclass */
	resultBuffer[arg] = '\0';
}


#if (defined(J9VM_NEEDS_JNI_REDIRECTION))
/**
 * Creates an aligned trampoline which dispatches to the JNI function located
 * at \c address.
 * \param vm
 * \param address The address of the JNI function.
 * \param classLoader The classLoader in which the trampoline will be allocated.
 * \return The start address of the trampoline, or NULL on failure.
 */
void *
alignJNIAddress(J9JavaVM * vm, void * address, J9ClassLoader * classLoader)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9JNIRedirectionBlock * block;

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(vm->bindNativeMutex);
#endif

	block = classLoader->jniRedirectionBlocks;
	if ((block == NULL) || ((block->end - block->alloc) < J9JNIREDIRECT_SEQUENCE_ALIGNMENT)) {
		J9PortVmemIdentifier identifier;

		block = (J9JNIRedirectionBlock*)j9vmem_reserve_memory(
			NULL,
			J9JNIREDIRECT_BLOCK_SIZE,
			&identifier,
			J9PORT_VMEM_MEMORY_MODE_READ | J9PORT_VMEM_MEMORY_MODE_WRITE | J9PORT_VMEM_MEMORY_MODE_EXECUTE | J9PORT_VMEM_MEMORY_MODE_COMMIT,
			j9vmem_supported_page_sizes()[0], OMRMEM_CATEGORY_VM);
		if (block == NULL) {
			return NULL;
		}
		block->next = classLoader->jniRedirectionBlocks;
		block->vmemID = identifier;
		block->alloc = (U_8 *)OMR::align((UDATA)(block + 1), J9JNIREDIRECT_SEQUENCE_ALIGNMENT);
		block->end = ((U_8 *) block) + J9JNIREDIRECT_BLOCK_SIZE;
		classLoader->jniRedirectionBlocks = block;
		TRIGGER_J9HOOK_VM_DYNAMIC_CODE_LOAD(vm->hookInterface, currentVMThread(vm), NULL, block, J9JNIREDIRECT_BLOCK_SIZE, "JNI trampoline area", NULL);
	}

#if defined(J9CPU_IA32)
	/* E9 (4 byte displacement)		jmp address */

	block->alloc[0] = 0xE9;
	*((UDATA *) (block->alloc + 1)) = (((U_8 *) address) - (block->alloc + 5));
#elif defined(J9CPU_AMD64)
	/* 49 BB (8 bytes address)		mov r11,address
	 * 41 FF E3								jmp r11
	 */

	block->alloc[0] = 0x49;
	block->alloc[1] = 0xBB;
	*((UDATA *) (block->alloc + 2)) = (UDATA)address;
	block->alloc[10] = 0x41;
	block->alloc[11] = 0xFF;
	block->alloc[12] = 0xE3;
#elif defined(J9CPU_ARM)
	/* ldr R15, [R15,  #-4]
	 * <address>
	 */
	((U_32*)block->alloc)[0] = 0xE51FF004;
	((U_32*)block->alloc)[1] = (U_32)address;
#else
#error Unknown CPU
#endif

	address = block->alloc;
	j9cpu_flush_icache(address, J9JNIREDIRECT_SEQUENCE_ALIGNMENT);
	block->alloc += J9JNIREDIRECT_SEQUENCE_ALIGNMENT;

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(vm->bindNativeMutex);
#endif

	return address;
}

#endif /* J9VM_NEEDS_JNI_REDIRECTION */


#define NATIVE_METHOD_UNBOUND	0
#define NATIVE_METHOD_BINDING	1
#define NATIVE_METHOD_BOUND		2

typedef struct J9NativeMethodBindEntry {
	J9Method * nativeMethod;	/* The method being bound */
	J9VMThread * bindThread;	/* The thread doing the bind */
	UDATA count;				/* Number of threads referring to this table entry */
	UDATA state;				/* One of UNBOUND, BINDING or BOUND */
	char * longJNIName;			/* Fully-decorated JNI function name */
	char * shortJNIName;		/* Undecorated JNI function name */
} J9NativeMethodBindEntry ;

/**
 * Initialize the native method bind table.
 * \param vm
 * \return Zero on success, non-zero on failure.
 */
UDATA
initializeNativeMethodBindTable(J9JavaVM *vm)
{
	vm->nativeMethodBindTable = hashTableNew(OMRPORT_FROM_J9PORT(vm->portLibrary), "Native method bind table", 0, sizeof(J9NativeMethodBindEntry), 0, 0, OMRMEM_CATEGORY_VM, nativeMethodHash, nativeMethodEqual, NULL, NULL);
	return vm->nativeMethodBindTable == NULL;
}

/**
 * Free the native method bind table.
 * \param vm
 */
void
freeNativeMethodBindTable(J9JavaVM *vm)
{
	if (vm->nativeMethodBindTable != NULL) {
		hashTableFree(vm->nativeMethodBindTable);
	}
}

/**
 * Compute the hash code for the supplied \c J9NativeMethodBindEntry.
 * \param key
 * \param userData
 * \return A hash value for the J9NativeMethodBindEntry.
 */
static UDATA
nativeMethodHash(void *key, void *userData)
{
	return (UDATA)((J9NativeMethodBindEntry*)key)->nativeMethod;
}


/**
 * Determines if \c leftKey and \c rightKey refer to the same hashtable entry.
 * \param leftKey The first key to compare.
 * \param rightKey The second key to compare.
 * \param userData
 * \return Non-zero if the entries are equal, zero otherwise.
 */
static UDATA
nativeMethodEqual(void *leftKey, void *rightKey, void *userData)
{
	return ((J9NativeMethodBindEntry*)leftKey)->nativeMethod == ((J9NativeMethodBindEntry*)rightKey)->nativeMethod;
}

/**
 * Resolves the native entrypoint for the Java \c nativeMethod.  Compile-time
 * resolves will not generate VM hook events.
 *
 * \param currentThread
 * \param nativeMethod The native method to resolve.
 * \param runtimeBind Non-zero for a regular bind, zero for compile-time resolve.
 * \warning This function may release VM access.
 * \return
 * 	J9_NATIVE_METHOD_BIND_SUCCESS on success.
 * 	J9_NATIVE_METHOD_BIND_FAIL if native cannot be bound, or mangling is illegal.
 *  J9_NATIVE_METHOD_BIND_RECURSIVE if another thread is binding the method.
 * 	J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY if scratch space cannot be allocated.
 */
UDATA   
resolveNativeAddress(J9VMThread *currentThread, J9Method *nativeMethod, UDATA runtimeBind)
{
	J9JavaVM * vm = currentThread->javaVM;
	UDATA rc = J9_NATIVE_METHOD_BIND_SUCCESS;
	UDATA bindJNINative = TRUE;

#if defined(J9VM_OPT_JVMTI)
	/* Binds from within a JIT compile-time resolve must not run java code.
	 * The only way that could happen is from inside the JNI native method bind event
	 * in JVMTI.  If that event is not enabled, allow JNI natives to be bound at compile time.
	 */
	if (!runtimeBind) {
		J9HookInterface ** vmHook = getVMHookInterface(vm);

		if ((*vmHook)->J9HookDisable(vmHook, J9HOOK_VM_JNI_NATIVE_BIND)) {
			bindJNINative = FALSE;
		}
	}
#endif

	/* Release VM access before acquiring the monitor to prevent deadlock situations */
	internalReleaseVMAccess(currentThread);
	omrthread_monitor_enter(vm->bindNativeMutex);

	/* If the method is already bound, we're done */

	if (!J9_NATIVE_METHOD_IS_BOUND(nativeMethod)) {
		J9NativeMethodBindEntry exemplar;
		J9NativeMethodBindEntry * entry;

		/* See if a table entry exists for this method */
		exemplar.nativeMethod = nativeMethod;
		entry = (J9NativeMethodBindEntry*)hashTableFind(vm->nativeMethodBindTable, &exemplar);
		if (entry != NULL) {
			/* Entry exists, see if a bind is in progress */

			if (entry->state == NATIVE_METHOD_BINDING) {
				/* If the current thread is the one binding, fail out, otherwise wait for the bind to complete */
				if (currentThread == entry->bindThread) {
					rc = J9_NATIVE_METHOD_BIND_RECURSIVE;
				} else {
					++entry->count;
					do {
						omrthread_monitor_wait(vm->bindNativeMutex);
					} while (entry->state == NATIVE_METHOD_BINDING);
					--entry->count;
				}
			}
		} else {
			char * namesBuffer;

			/* Create the symbol names for lookup */
			namesBuffer = (char *) buildNativeFunctionNames(vm, nativeMethod, J9_CLASS_FROM_METHOD(nativeMethod), 0);
			if (namesBuffer == NULL) {
				rc = J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY;
			} else if (namesBuffer == (char*)UDATA_MAX) {
				rc = J9_NATIVE_METHOD_BIND_FAIL;
			} else {
				/* exemplar.nativeMethod already set above */
				/* exemplar.bindThread is set below */
				exemplar.state = NATIVE_METHOD_UNBOUND;
				exemplar.count = 0;
				exemplar.longJNIName = namesBuffer;
				exemplar.shortJNIName = namesBuffer + strlen(namesBuffer) + 1;
				entry = (J9NativeMethodBindEntry*)hashTableAdd(vm->nativeMethodBindTable, &exemplar);
				if (entry == NULL) {
					rc = J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY;
				}
			}
		}

		/* Bail if recursion or out of memory was detected */

		if (rc == J9_NATIVE_METHOD_BIND_SUCCESS) {
			/* If the bind has not already succeeded, perform the bind (state cannot be binding at this point) */

			if (entry->state == NATIVE_METHOD_UNBOUND) {
				/* Set the entry to be binding on the current thread and drop the monitor */

				++entry->count;
				entry->bindThread = currentThread;
				entry->state = NATIVE_METHOD_BINDING;
				omrthread_monitor_exit(vm->bindNativeMutex);

				/* Perform the bind */

				rc = bindNative(currentThread, nativeMethod, entry->longJNIName, entry->shortJNIName, bindJNINative);

				/* Reacquire the mutex and update the state based on the success/fail of the bind */

				omrthread_monitor_enter(vm->bindNativeMutex);
				entry->state = (rc == J9_NATIVE_METHOD_BIND_SUCCESS) ? NATIVE_METHOD_BOUND : NATIVE_METHOD_UNBOUND;

				/* Remove the current thread from the entry and notify anyone waiting */

				--entry->count;
				omrthread_monitor_notify_all(vm->bindNativeMutex);
			}

			/* If this entry is now unused, delete it */

			if (entry->count == 0) {
				PORT_ACCESS_FROM_JAVAVM(vm);

				j9mem_free_memory(entry->longJNIName);
				hashTableRemove(vm->nativeMethodBindTable, entry);
			}
		}
	}

	/* Drop the monitor and reacquire VM access */
	omrthread_monitor_exit(vm->bindNativeMutex);
	internalAcquireVMAccess(currentThread);

	return rc;
}

/**
 *Attempt to bind the \c nativeMethod by looking through native libraries in
 *classLoader, and JVMTI agent libraries.
 * \param currentThread
 * \param nativeMethod The JNI native method to bind.
 * \param longJNI The long mangled JNI name.
 * \param shortJNI The short mangled JNI name.
 * \param bindJNINative Non-zero if JNI natives are allowed, zero for INL-only.
 * \return
 *   J9_NATIVE_METHOD_BIND_SUCCESS on success.
 *   J9_NATIVE_METHOD_BIND_FAIL on failure.
 */
static UDATA
bindNative(J9VMThread *currentThread, J9Method *nativeMethod, char *longJNI, char *shortJNI, UDATA bindJNINative)
{
	J9JavaVM *vm = currentThread->javaVM;
	J9ClassLoader *classLoader = J9_CLASS_FROM_METHOD(nativeMethod)->classLoader;
	J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(nativeMethod);
	U_8 argCount = romMethod->argCount;
	J9NativeLibrary *nativeLibrary = NULL;

	Trc_VM_bindNative_Entry(currentThread, nativeMethod, longJNI, shortJNI, bindJNINative);
#if defined(J9VM_INTERP_MINIMAL_JNI)
	/* Minimal JNI does not support all 255 arguments */
	if (argCount > J9_INLINE_JNI_MAX_ARG_COUNT) {
		return J9_NATIVE_METHOD_BIND_FAIL;
	}
#endif

	/* Adjust the argument count to account for the env and clazz/receiver.
	 * The receiver for virtual methods is already included in the method argument count.
	 */
	++argCount;
	if (romMethod->modifiers & J9AccStatic) {
		++argCount;
	}

#if JAVA_SPEC_VERSION >= 15
	if (classLoader == vm->systemClassLoader)
#endif /* JAVA_SPEC_VERSION >= 15 */
	{
		/* Search each shared library in the class loader for a matching native */
		nativeLibrary = classLoader->librariesHead;
		while (nativeLibrary != NULL) {
			UDATA rc = lookupNativeAddress(currentThread, nativeMethod, nativeLibrary, longJNI, shortJNI, argCount, bindJNINative);
			if (J9_NATIVE_METHOD_IS_BOUND(nativeMethod)) {
				Trc_VM_bindNative_NativeLibrary_Success(currentThread, nativeMethod, nativeLibrary, longJNI, shortJNI, bindJNINative);
				return J9_NATIVE_METHOD_BIND_SUCCESS;
			} else if (J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY == rc) {
				Trc_VM_bindNative_NativeLibrary_OOM(currentThread, nativeMethod, nativeLibrary, longJNI, shortJNI, bindJNINative);
				return rc;
			}
			nativeLibrary = nativeLibrary->next;
		}
	}
#if JAVA_SPEC_VERSION >= 15
	UDATA rc = lookupNativeAddress(currentThread, nativeMethod, NULL, longJNI, shortJNI, argCount, bindJNINative);
	if (J9_NATIVE_METHOD_IS_BOUND(nativeMethod)) {
		Trc_VM_bindNative_NullNativeLibrary_Success(currentThread, nativeMethod, longJNI, shortJNI, bindJNINative);
		return J9_NATIVE_METHOD_BIND_SUCCESS;
	} else if (J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY == rc) {
		Trc_VM_bindNative_NullNativeLibrary_OOM(currentThread, nativeMethod, longJNI, shortJNI, bindJNINative);
		return rc;
	}
#endif /* JAVA_SPEC_VERSION >= 15 */

#if defined(J9VM_OPT_JVMTI)
	/* If the native is not found in any registered library, search JVMTI agent libraries.
	 * The lookup hook calls lookupNativeAddress with bindJNINative = TRUE, so it must be TRUE
	 * here in order to call the hook.
	 */
	if (bindJNINative) {
		TRIGGER_J9HOOK_VM_LOOKUP_NATIVE_ADDRESS(vm->hookInterface, currentThread, nativeMethod, longJNI, shortJNI, argCount, lookupNativeAddress);
		if (J9_NATIVE_METHOD_IS_BOUND(nativeMethod)) {
			Trc_VM_bindNative_JVMTIAgent_Success(currentThread, nativeMethod, longJNI, shortJNI);
			return J9_NATIVE_METHOD_BIND_SUCCESS;
		}
	}
#endif

	Trc_VM_bindNative_Fail(currentThread, nativeMethod, longJNI, shortJNI, bindJNINative);
	return J9_NATIVE_METHOD_BIND_FAIL;
}

/**
 * Look up a JNI native in the specified \c nativeLibrary, if found then
 * fill in the \c nativeMethod->extra and \c nativeMethod->sendTarget fields.
 *
 * \param currentThread
 * \param nativeLibrary The library to scan for the C entrypoint.
 * \param nativeMethod The Java native method.
 * \param symbolName The name of the C entrypoint.
 * \param signature The signature as required by j9sl_lookup_name().
 * \return 0 on success, any other value on failure.
 */
UDATA
lookupJNINative(J9VMThread *currentThread, J9NativeLibrary *nativeLibrary, J9Method *nativeMethod, char *symbolName, char *signature)
{
	UDATA lookupResult = 0;
	void *functionAddress = NULL;
	J9JavaVM *vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_VM_lookupJNINative_Entry(currentThread, nativeLibrary, nativeMethod, symbolName, signature);
#if JAVA_SPEC_VERSION >= 15
	if (NULL == nativeLibrary) {
		J9MemoryManagerFunctions const * const mmFuncs = vm->memoryManagerFunctions;
		J9NameAndSignature nas = {0};
		nas.name = (J9UTF8 *)&j9_findnative_name;
		nas.signature = (J9UTF8 *)&j9_findnative_sig;
		internalAcquireVMAccess(currentThread);
		j9object_t entryName = mmFuncs->j9gc_createJavaLangString(currentThread, (U_8*)symbolName, strlen(symbolName), 0);
		j9object_t classLoaderObject = J9_CLASS_FROM_METHOD(nativeMethod)->classLoader->classLoaderObject;
		UDATA args[] = { (UDATA) classLoaderObject, (UDATA) entryName };
		runStaticMethod(currentThread, (U_8 *)"java/lang/ClassLoader", &nas, 2, (UDATA *)args);
		internalReleaseVMAccess(currentThread);
		functionAddress = (UDATA *) currentThread->returnValue;
		if ((NULL != currentThread->currentException) || (NULL == functionAddress) ) {
			lookupResult = 1;
		}
		Trc_VM_lookupJNINative_NullNativeLibrary(currentThread, nativeMethod, symbolName, signature, functionAddress);
	} else
#endif /* JAVA_SPEC_VERSION >= 15 */
	{
		lookupResult = j9sl_lookup_name(nativeLibrary->handle, symbolName, (UDATA*)&functionAddress, signature);
	}
	if (lookupResult == 0) {
		UDATA cpFlags = J9_STARTPC_JNI_NATIVE;

#if defined(J9VM_OPT_JVMTI)
		internalAcquireVMAccess(currentThread);
		TRIGGER_J9HOOK_VM_JNI_NATIVE_BIND(vm->hookInterface, currentThread, nativeMethod, functionAddress);
		internalReleaseVMAccess(currentThread);
#endif
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
		if ((NULL != nativeLibrary) && (0 != nativeLibrary->doSwitching)) {
			cpFlags |= J9_STARTPC_NATIVE_REQUIRES_SWITCHING;
			if (nativeLibrary->doSwitching & J9_NATIVE_LIBRARY_SWITCH_JDBC) {
				J9Class* ramClass;
				
				ramClass = J9_CLASS_FROM_METHOD(nativeMethod);
				ramClass->classDepthAndFlags |= J9AccClassHasJDBCNatives;
			}
		}
#endif
#if defined(J9VM_NEEDS_JNI_REDIRECTION)
		if (((UDATA)functionAddress) & (J9JNIREDIRECT_REQUIRED_ALIGNMENT - 1)) {
			functionAddress = alignJNIAddress(vm, functionAddress, J9_CLASS_FROM_METHOD(nativeMethod)->classLoader);
			if (functionAddress == NULL) {
				/* Need to propagate the OOM back out */
				return 1;
			}
		}
#endif
		nativeMethod->extra = (void *) ((UDATA)functionAddress | J9_STARTPC_NOT_TRANSLATED);
		atomicOrIntoConstantPool(vm, nativeMethod, cpFlags);
		nativeMethod->methodRunAddress = vm->jniSendTarget;
	}
	Trc_VM_lookupJNINative_Exit(currentThread, nativeLibrary, nativeMethod, nativeMethod->extra, symbolName, signature, lookupResult);

	return lookupResult;
}

/**
 * Probes for the various JNI function names in the order specified by the JNI
 * specification (short then long).  JNI natives supersede INL equivalents.
 * \param currentThread
 * \param nativeMethod The JNI native method to bind.
 * \param nativeLibrary The library to scan for the matching entrypoint.
 * \param longJNI The long mangled JNI name.
 * \param shortJNI The short mangled JNI name.
 * \param functionArgCount The number of arguments to the C entrypoint.
 * \param bindJNINative Non-zero if JNI natives are allowed, zero for INL-only.
 */
static UDATA   
lookupNativeAddress(J9VMThread *currentThread, J9Method *nativeMethod, J9NativeLibrary *nativeLibrary, char *longJNI, char *shortJNI, UDATA functionArgCount, UDATA bindJNINative)
{
	UDATA lookupResult = 0;
	char argSignature[260];  /* max args is 256 + JNIEnv + jobject/jclass + return type + '\0' */

	Trc_VM_lookupNativeAddress_Entry(currentThread, nativeLibrary, nativeMethod, longJNI, shortJNI, functionArgCount, bindJNINative);
	if (NULL != nativeLibrary) {
		bool inlAllowed = J9_ARE_ANY_BITS_SET(nativeLibrary->flags, J9NATIVELIB_FLAG_ALLOW_INL);
		/* If INL lookup is allowed for this library, check for C INLs */
		if (inlAllowed) {
			UDATA rc = inlIntercept(currentThread, nativeMethod, longJNI);
			if ((J9_NATIVE_METHOD_BIND_SUCCESS == rc) || (J9_NATIVE_METHOD_BIND_OUT_OF_MEMORY == rc)) {
				Trc_VM_lookupNativeAddress_inlIntercept_Exit(currentThread, nativeLibrary, nativeMethod, longJNI, rc);
				return rc;
			}
		}
	}

	/* Compute the native signature based on the method */
	nativeSignature(nativeMethod, argSignature);

	/* From the JNI spec:
	 *
	 * The VM looks first for the short name; that is, the name without the argument signature.
	 * It then looks for the long name, which is the name with the argument signature.
	 */

	if (bindJNINative) {
		UDATA  (*bind_method)(struct J9VMThread* vmThread, struct J9NativeLibrary* library, struct J9Method* method, char* functionName, char* argSignature) = NULL;
		if (NULL != nativeLibrary) {
			bind_method = nativeLibrary->bind_method;
		} else {
			bind_method = lookupJNINative;
		}
		lookupResult = bind_method(currentThread, nativeLibrary, nativeMethod, shortJNI, argSignature);
		if (0 == lookupResult) {
			Trc_VM_lookupNativeAddress_bindmethod_shortJNI_Exit(currentThread, nativeLibrary, nativeMethod, shortJNI, argSignature);
			return J9_NATIVE_METHOD_BIND_SUCCESS;
		}
		lookupResult = bind_method(currentThread, nativeLibrary, nativeMethod, longJNI, argSignature);
		if (0 == lookupResult) {
			Trc_VM_lookupNativeAddress_bindmethod_longJNI_Exit(currentThread, nativeLibrary, nativeMethod, longJNI, argSignature);
			return J9_NATIVE_METHOD_BIND_SUCCESS;
		}
	}

	Trc_VM_lookupNativeAddress_fail_Exit(currentThread, nativeLibrary, nativeMethod, argSignature);
	return J9_NATIVE_METHOD_BIND_FAIL;
}

void atomicOrIntoConstantPool(J9JavaVM *vm, J9Method *method, UDATA cpFlags)
{
	VM_AtomicSupport::bitOr((UDATA*)&method->constantPool, cpFlags);
}

void atomicAndIntoConstantPool(J9JavaVM *vm, J9Method *method, UDATA cpFlags)
{
	VM_AtomicSupport::bitAnd((UDATA*)&method->constantPool, cpFlags);
}

}

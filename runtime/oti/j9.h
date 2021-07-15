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

#ifndef J9_H
#define J9_H

/* @ddr_namespace: default */
#include <string.h>

#include "j9cfg.h"
#include "j9comp.h"
#if defined(J9ZOS390)
/* This is the contents of the old zvarmaps.h - delete this once the builder symbols are gone */
#pragma variable( native2JITExitBCTable, NORENT )
#pragma variable( returnFromJ2IBytecodes, NORENT )
#endif /* J9ZOS390 */

#include "j9port.h"
#include "j9argscan.h"
#include "omrthread.h"
#include "j9class.h"
#include "j9pool.h"
#include "j9simplepool.h"
#include "omrhashtable.h"
#include "j9lib.h"
#include "jni.h"
#include "j9trace.h"
#include "omravl.h"
#include "vmhook_internal.h" /* for definition of struct J9JavaVMHookInterface */
#include "jlm.h" /* for definition of struct J9VMJlmDump */
#include "j9cp.h"
#include "j9accessbarrier.h"
#include "j9vmconstantpool.h"
#include "shcflags.h"
#include "j9modifiers_api.h"
#include "j9memcategories.h"
#include "j9segments.h"
#include "bcutil.h"
#include "ute_module.h"
#include "j9consts.h"
#include "omr.h"
#include "temp.h"
#include "j9thread.h"
#include "j2sever.h"
#include "j9relationship.h"

typedef struct J9JNIRedirectionBlock {
	struct J9JNIRedirectionBlock* next;
	struct J9PortVmemIdentifier vmemID;
	U_8* alloc;
	U_8* end;
} J9JNIRedirectionBlock;

#define J9JNIREDIRECT_BLOCK_SIZE  0x1000

typedef struct J9ClassLoaderWalkState {
   struct J9JavaVM* vm;
   UDATA flags;
   struct J9PoolState classLoaderBlocksWalkState;
} J9ClassLoaderWalkState;

/* include ENVY-generated part of j9.h (which formerly was j9.h) */
#include "j9generated.h"
#include "j9cfg_builder.h"

#include "j9accessbarrierhelpers.h"

/*------------------------------------------------------------------
 * AOT version defines
 *------------------------------------------------------------------*/
#define AOT_MAJOR_VERSION 1
#define AOT_MINOR_VERSION 0


/* temporary hack to ensure that we get the right version of translate MethodHandle */
#define TRANSLATE_METHODHANDLE_TAKES_FLAGS

/* temporary define to allow JIT work to promote and be enabled at the same time as the vm side */
#define NEW_FANIN_INFRA

/* -------------- Add C-level global definitions below this point ------------------ */ 

/*
 * Simultaneously check if the flags field has the sign bit set and the valueOffset is non-zero.
 *
 * In a resolved field, flags will have the J9FieldFlagResolved bit set, thus having a higher
 * value than any valid valueOffset.
 *
 * This avoids the need for a barrier, as this check only succeeds if flags and valueOffset have
 * both been updated. It is crucial that we do not treat a field ref as resolved if only one of
 * the two values has been set (by another thread that is in the middle of a resolve).
 */

#define JNIENV_FROM_J9VMTHREAD(vmThread) ((JNIEnv*)(vmThread))
#define J9VMTHREAD_FROM_JNIENV(env) ((J9VMThread*)(env))

#define J9RAMFIELDREF_IS_RESOLVED(base) ((base)->flags > (base)->valueOffset)

#define J9DDR_DAT_FILE "j9ddr.dat"

/* RAM class shape flags - valid to use these when the underlying ROM class has been unloaded */

#define J9CLASS_SHAPE(ramClass) ((J9CLASS_FLAGS(ramClass) >> J9AccClassRAMShapeShift) & OBJECT_HEADER_SHAPE_MASK)
#define J9CLASS_IS_ARRAY(ramClass) ((J9CLASS_FLAGS(ramClass) & J9AccClassRAMArray) != 0)
#define J9CLASS_IS_MIXED(ramClass) (((J9CLASS_FLAGS(ramClass) >> J9AccClassRAMShapeShift) & OBJECT_HEADER_SHAPE_MASK) == OBJECT_HEADER_SHAPE_MIXED)

#define J9CLASS_IS_EXEMPT_FROM_VALIDATION(clazz) \
	((J9ROMCLASS_IS_UNSAFE((clazz)->romClass) && !J9ROMCLASS_IS_HIDDEN((clazz)->romClass)) || (J9_ARE_ANY_BITS_SET((clazz)->classFlags, J9ClassIsExemptFromValidation)))

#define IS_STRING_COMPRESSION_ENABLED(vmThread) (FALSE != ((vmThread)->javaVM)->strCompEnabled)

#define IS_STRING_COMPRESSION_ENABLED_VM(javaVM) (FALSE != (javaVM)->strCompEnabled)

#define IS_STRING_COMPRESSED(vmThread, object) \
	(IS_STRING_COMPRESSION_ENABLED(vmThread) ? \
		(J2SE_VERSION((vmThread)->javaVM) >= J2SE_V11 ? \
			(((I_32) J9VMJAVALANGSTRING_CODER(vmThread, object)) == 0) : \
			(((I_32) J9VMJAVALANGSTRING_COUNT(vmThread, object)) >= 0)) : \
		FALSE)

#define IS_STRING_COMPRESSED_VM(javaVM, object) \
	(IS_STRING_COMPRESSION_ENABLED_VM(javaVM) ? \
		(J2SE_VERSION(javaVM) >= J2SE_V11 ? \
			(((I_32) J9VMJAVALANGSTRING_CODER_VM(javaVM, object)) == 0) : \
			(((I_32) J9VMJAVALANGSTRING_COUNT_VM(javaVM, object)) >= 0)) : \
		FALSE)

#define J9VMJAVALANGSTRING_LENGTH(vmThread, object) \
	(IS_STRING_COMPRESSION_ENABLED(vmThread) ? \
		(J2SE_VERSION((vmThread)->javaVM) >= J2SE_V11 ? \
			(J9INDEXABLEOBJECT_SIZE(vmThread, J9VMJAVALANGSTRING_VALUE(vmThread, object)) >> ((I_32) J9VMJAVALANGSTRING_CODER(vmThread, object))) : \
			(J9VMJAVALANGSTRING_COUNT(vmThread, object) & 0x7FFFFFFF)) : \
		(J2SE_VERSION((vmThread)->javaVM) >= J2SE_V11 ? \
			(J9INDEXABLEOBJECT_SIZE(vmThread, J9VMJAVALANGSTRING_VALUE(vmThread, object)) >> 1) : \
			(J9VMJAVALANGSTRING_COUNT(vmThread, object))))

#define J9VMJAVALANGSTRING_LENGTH_VM(javaVM, object) \
	(IS_STRING_COMPRESSION_ENABLED_VM(javaVM) ? \
		(J2SE_VERSION(javaVM) >= J2SE_V11 ? \
			(J9INDEXABLEOBJECT_SIZE_VM(javaVM, J9VMJAVALANGSTRING_VALUE_VM(javaVM, object)) >> ((I_32) J9VMJAVALANGSTRING_CODER_VM(javaVM, object))) : \
			(J9VMJAVALANGSTRING_COUNT_VM(javaVM, object) & 0x7FFFFFFF)) : \
		(J2SE_VERSION(javaVM) >= J2SE_V11 ? \
			(J9INDEXABLEOBJECT_SIZE_VM(javaVM, J9VMJAVALANGSTRING_VALUE_VM(javaVM, object)) >> 1) : \
			(J9VMJAVALANGSTRING_COUNT_VM(javaVM, object))))

/* UTF8 access macros - all access to J9UTF8 fields should be done through these macros */

#define J9UTF8_LENGTH(j9UTF8Address) (((struct J9UTF8 *)(j9UTF8Address))->length)
#define J9UTF8_SET_LENGTH(j9UTF8Address, len) (((struct J9UTF8 *)(j9UTF8Address))->length = (len))
#define J9UTF8_DATA(j9UTF8Address) (((struct J9UTF8 *)(j9UTF8Address))->data)
#define J9UTF8_TOTAL_SIZE(j9UTF8Address) (sizeof(J9UTF8) + J9UTF8_LENGTH(j9UTF8Address))
#define J9UTF8_DATA_EQUALS(data1, length1, data2, length2) ((((length1) == (length2)) && (memcmp((data1), (data2), (length1)) == 0)))
#define J9UTF8_EQUALS(utf1, utf2) (((utf1) == (utf2)) || (J9UTF8_DATA_EQUALS(J9UTF8_DATA(utf1), J9UTF8_LENGTH(utf1), J9UTF8_DATA(utf2), J9UTF8_LENGTH(utf2))))
#define J9UTF8_LITERAL_EQUALS(data1, length1, cString) (J9UTF8_DATA_EQUALS((data1), (length1), (cString), sizeof(cString) - 1))

/*
 * Equivalent to ((int)strlen(string_literal)) when given a literal string.
 * E.g.: LITERAL_STRLEN("lib") == 3
 */
#define LITERAL_STRLEN(string_literal) ((IDATA)(sizeof(string_literal) - 1))

#define ROUND_UP_TO_POWEROF2(value, powerof2) (((value) + ((powerof2) - 1)) & (UDATA)~((powerof2) - 1))
#undef ROUND_DOWN_TO_POWEROF2
#define ROUND_DOWN_TO_POWEROF2(value, powerof2) ((value) & (UDATA)~((powerof2) - 1))

#define J9_REDIRECTED_REFERENCE 1
#define J9_JNI_UNWRAP_REDIRECTED_REFERENCE(ref)  ((*(UDATA*)(ref)) & J9_REDIRECTED_REFERENCE ? **(j9object_t**)(((UDATA)(ref)) - J9_REDIRECTED_REFERENCE) : *(j9object_t*)(ref))

#define VERBOSE_CLASS 1
#define VERBOSE_GC 2
#define VERBOSE_RESERVED 4
#define VERBOSE_DYNLOAD 8
#define VERBOSE_STACK 16
#define VERBOSE_DEBUG 32
#define VERBOSE_INIT 64
#define VERBOSE_RELOCATIONS 128
#define VERBOSE_ROMCLASS 256
#define VERBOSE_STACKTRACE 512
#define VERBOSE_SHUTDOWN 1024
#define VERBOSE_DUMPSIZES	2048

/* Maximum array dimensions, according to the spec for the array bytecodes, is 255 */
#define J9_ARRAY_DIMENSION_LIMIT 255

#define J9FLAGSANDCLASS_FROM_CLASSANDFLAGS(classAndFlags) (((classAndFlags) >> J9_REQUIRED_CLASS_SHIFT) | ((classAndFlags) << (sizeof(UDATA) * 8 - J9_REQUIRED_CLASS_SHIFT)))
#define J9CLASSANDFLAGS_FROM_FLAGSANDCLASS(flagsAndClass) (((flagsAndClass) >> (8 * sizeof(UDATA) - J9_REQUIRED_CLASS_SHIFT)) | ((flagsAndClass) << J9_REQUIRED_CLASS_SHIFT))
#define J9RAMSTATICFIELDREF_CLASS(base) ((J9Class *) ((base)->flagsAndClass << J9_REQUIRED_CLASS_SHIFT))
/* In a resolved J9RAMStaticFieldRef, the high bit of valueOffset is set, so it must be masked when adding valueOffset to the ramStatics address.
 * Note that ~IDATA_MIN has all but the high bit set. */
#define J9STATICADDRESS(flagsAndClass, valueOffset) ((void *) (((UDATA) ((J9Class *) ((flagsAndClass) << J9_REQUIRED_CLASS_SHIFT))->ramStatics) + ((valueOffset) & ~IDATA_MIN)))
#define J9RAMSTATICFIELDREF_VALUEADDRESS(base) ((void *) (((UDATA) J9RAMSTATICFIELDREF_CLASS(base)->ramStatics) + ((base)->valueOffset & ~IDATA_MIN)))
/* Important: We check both valueOffset and flagsAndClass because we do not set them atomically on resolve. */
#define J9RAMSTATICFIELDREF_IS_RESOLVED(base) ((-1 != (base)->valueOffset) && ((base)->flagsAndClass) > 0)

/* From tracehelp.c - prototyped here because the macro is here */

/**
 * Get the trace interface from the VM using GetEnv.
 *
 * @param j9vm the J9JavaVM
 *
 * @return the trace interface, or NULL if GetEnv fails
 */
extern J9_CFUNC UtInterface * getTraceInterfaceFromVM(J9JavaVM *j9vm);

#define J9_UTINTERFACE_FROM_VM(vm) getTraceInterfaceFromVM(vm)

/**
 * @name Port library access
 * @anchor PortAccess
 * Macros for accessing port library.
 * @{
 */
#define PORT_ACCESS_FROM_ENV(jniEnv) J9PortLibrary *privatePortLibrary = (J9VMTHREAD_FROM_JNIENV(jniEnv))->javaVM->portLibrary
#define PORT_ACCESS_FROM_JAVAVM(javaVM) J9PortLibrary *privatePortLibrary = (javaVM)->portLibrary
#define PORT_ACCESS_FROM_VMC(vmContext) J9PortLibrary *privatePortLibrary = (vmContext)->javaVM->portLibrary
#define PORT_ACCESS_FROM_GINFO(javaVM) J9PortLibrary *privatePortLibrary = (javaVM)->portLibrary
#define PORT_ACCESS_FROM_JITCONFIG(jitConfig) J9PortLibrary *privatePortLibrary = (jitConfig)->javaVM->portLibrary
#define PORT_ACCESS_FROM_WALKSTATE(walkState) J9PortLibrary *privatePortLibrary = (walkState)->walkThread->javaVM->portLibrary
#define OMRPORT_ACCESS_FROM_J9VMTHREAD(vmContext) OMRPORT_ACCESS_FROM_J9PORT((vmContext)->javaVM->portLibrary)
#define PORT_ACCESS_FROM_VMI(vmi) J9PortLibrary *privatePortLibrary = (*vmi)->GetPortLibrary(vmi)
/** @} */

#define J9_DECLARE_CONSTANT_UTF8(instanceName, name) \
static const struct { \
	U_16 length; \
	U_8 data[sizeof(name)]; \
} instanceName = { sizeof(name) - 1, name }

#define J9_DECLARE_CONSTANT_NAS(instanceName, name, sig) const J9NameAndSignature instanceName = { (J9UTF8*)&name, (J9UTF8*)&sig }

#if defined(J9ZOS390) || (defined(LINUXPPC64) && defined(J9VM_ENV_LITTLE_ENDIAN))
#define J9_EXTERN_BUILDER_SYMBOL(name) extern void name()
#else
#define J9_EXTERN_BUILDER_SYMBOL(name) extern void* name
#endif
#define J9_BUILDER_SYMBOL(name) TOC_UNWRAP_ADDRESS(&name)

#define J9JAVASTACK_STACKOVERFLOWMARK(stack) ((UDATA*)(((UDATA)(stack)->end) - (stack)->size))

#define J9VM_PACKAGE_NAME_BUFFER_LENGTH 128

#if defined(J9VM_ARCH_X86)
/* x86 - one slot on the end of J9VMThread used to save the machineSP */
#define J9_VMTHREAD_SEGREGATED_ALLOCATION_CACHE_OFFSET (sizeof(J9VMThread) + sizeof(UDATA))
#else /* J9VM_ARCH_X86 */
/* Not x86 - no extra space required */
#define J9_VMTHREAD_SEGREGATED_ALLOCATION_CACHE_OFFSET sizeof(J9VMThread)
#endif /* J9VM_ARCH_X86 */

#if !defined(J9VM_INTERP_ATOMIC_FREE_JNI)
#define internalEnterVMFromJNI internalAcquireVMAccess
#define internalExitVMToJNI internalReleaseVMAccess
#endif /* !J9VM_INTERP_ATOMIC_FREE_JNI */

#define J9_IS_J9MODULE_UNNAMED(vm, module) ((NULL == module) || (module == vm->unamedModuleForSystemLoader))

#define J9_IS_J9MODULE_OPEN(module) (TRUE == module->isOpen)

#define J9_ARE_MODULES_ENABLED(vm) (J2SE_VERSION(vm) >= J2SE_V11)

/* Macro for VM internalVMFunctions */
#if defined(J9_INTERNAL_TO_VM)
#define J9_VM_FUNCTION(currentThread, function) function
#define J9_VM_FUNCTION_VIA_JAVAVM(javaVM, function) function
#else /* J9_INTERNAL_TO_VM */
#define J9_VM_FUNCTION(currentThread, function) ((currentThread)->javaVM->internalVMFunctions->function)
#define J9_VM_FUNCTION_VIA_JAVAVM(javaVM, function) ((javaVM)->internalVMFunctions->function)
#endif /* J9_INTERNAL_TO_VM */

/* Macros for VTable */
#define J9VTABLE_HEADER_FROM_RAM_CLASS(clazz) ((J9VTableHeader *)(((J9Class *)(clazz)) + 1))
#define J9VTABLE_FROM_HEADER(vtableHeader) ((J9Method **)(((J9VTableHeader *)(vtableHeader)) + 1))
#define J9VTABLE_FROM_RAM_CLASS(clazz) J9VTABLE_FROM_HEADER(J9VTABLE_HEADER_FROM_RAM_CLASS(clazz))
#define J9VTABLE_OFFSET_FROM_INDEX(index) (sizeof(J9Class) + sizeof(J9VTableHeader) + ((index) * sizeof(UDATA)))

/* VTable constants offset */
#define J9VTABLE_INITIAL_VIRTUAL_OFFSET (sizeof(J9Class) + offsetof(J9VTableHeader, initialVirtualMethod))
#define J9VTABLE_INVOKE_PRIVATE_OFFSET (sizeof(J9Class) + offsetof(J9VTableHeader, invokePrivateMethod))

/* Skip Interpreter VTable header */
#define JIT_VTABLE_START_ADDRESS(clazz) ((UDATA *)(clazz) - (sizeof(J9VTableHeader) / sizeof(UDATA)))

/* Check if J9RAMInterfaceMethodRef is resolved */
#define J9RAMINTERFACEMETHODREF_RESOLVED(interfaceClass, methodIndexAndArgCount) \
	((NULL != (interfaceClass)) && ((J9_ITABLE_INDEX_UNRESOLVED != ((methodIndexAndArgCount) & ~255))))

/* Macros for ValueTypes */
#define J9_CLASS_DISALLOWS_LOCKING_FLAGS (J9ClassIsValueType | J9ClassIsValueBased)
#define J9_CLASS_ALLOWS_LOCKING(clazz) J9_ARE_NO_BITS_SET((clazz)->classFlags, J9_CLASS_DISALLOWS_LOCKING_FLAGS)
#define J9_IS_J9CLASS_VALUEBASED(clazz) J9_ARE_ALL_BITS_SET((clazz)->classFlags, J9ClassIsValueBased)
#ifdef J9VM_OPT_VALHALLA_VALUE_TYPES
#define J9CLASS_UNPADDED_INSTANCE_SIZE(clazz) J9_VALUETYPE_FLATTENED_SIZE(clazz)
#define J9_IS_J9CLASS_VALUETYPE(clazz) J9_ARE_ALL_BITS_SET((clazz)->classFlags, J9ClassIsValueType)
#define J9_IS_J9CLASS_FLATTENED(clazz) J9_ARE_ALL_BITS_SET((clazz)->classFlags, J9ClassIsFlattened)
/**
 * Disable flattening of volatile field that is > 8 bytes for now, as the current implementation of copyObjectFields() will tear this field.
 * Check J9AccVolatile for now. A new way other that "volatile" will likely to be introduced to indicate a non-tearable VT class,
 * probably a marker interface java.lang.NonTearable.
 */
#define J9_IS_FIELD_FLATTENED(fieldClazz, romFieldShape) \
	(J9_IS_J9CLASS_FLATTENED(fieldClazz) && \
	(J9_ARE_NO_BITS_SET((romFieldShape)->modifiers, J9AccVolatile) || (J9CLASS_UNPADDED_INSTANCE_SIZE(fieldClazz) <= sizeof(U_64))))
#define J9_VALUETYPE_FLATTENED_SIZE(clazz) (J9CLASS_HAS_4BYTE_PREPADDING((clazz)) ? ((clazz)->totalInstanceSize - sizeof(U_32)) : (clazz)->totalInstanceSize)
#define IS_REF_OR_VAL_SIGNATURE(firstChar) ('L' == (firstChar) || 'Q' == (firstChar))
#define IS_QTYPE(firstChar) ('Q' == (firstChar))
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
#define J9CLASS_UNPADDED_INSTANCE_SIZE(clazz) ((clazz)->totalInstanceSize)
#define J9_IS_J9CLASS_VALUETYPE(clazz) FALSE
#define J9_IS_J9CLASS_FLATTENED(clazz) FALSE
#define J9_IS_FIELD_FLATTENED(fieldClazz, romFieldShape) FALSE
#define J9_VALUETYPE_FLATTENED_SIZE(clazz)((UDATA) 0) /* It is not possible for this macro to be used since we always check J9_IS_J9CLASS_FLATTENED before ever using it. */
#define IS_REF_OR_VAL_SIGNATURE(firstChar) ('L' == (firstChar))
#define IS_QTYPE(firstChar) FALSE
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

#if defined(OPENJ9_BUILD)
#define J9_SHARED_CACHE_DEFAULT_BOOT_SHARING(vm) TRUE
#define RUNTIME_INFO "RUNTIME"
#else /* defined(OPENJ9_BUILD) */
#define J9_SHARED_CACHE_DEFAULT_BOOT_SHARING(vm) FALSE
#define RUNTIME_INFO "JRE"
#endif /* defined(OPENJ9_BUILD) */

/* Annotation name which indicates that a class is not allowed to be modified by
 * JVMTI ClassFileLoadHook or RedefineClasses.
 */
#define J9_UNMODIFIABLE_CLASS_ANNOTATION "Lcom/ibm/oti/vm/J9UnmodifiableClass;"

typedef struct {
	char tag;
	char zero;
	char size;
	char data[LITERAL_STRLEN(J9_UNMODIFIABLE_CLASS_ANNOTATION)];
} J9_UNMODIFIABLE_CLASS_ANNOTATION_DATA;

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
#define J9_IS_31BIT_INTEROP_TARGET(handle) J9_ARE_ALL_BITS_SET((UDATA)(handle), OMRPORT_SL_ZOS_31BIT_TARGET_HIGHTAG)
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

#if 0 /* Until compile error are resolved */
#if defined(__cplusplus)
/* Hide the asserts from DDR */
#if !defined(TYPESTUBS_H)
static_assert((sizeof(J9_UNMODIFIABLE_CLASS_ANNOTATION_DATA) == (3 + LITERAL_STRLEN(J9_UNMODIFIABLE_CLASS_ANNOTATION))), "Annotation structure is not packed correctly");
/* '/' is the lowest numbered character which appears in the annotation name (assume
 * that no '$' exists in there). The name length must be smaller than '/' in order
 * to ensure that there are no overlapping substrings which would mandate a more
 * complex matching algorithm.
 */
static_assert((LITERAL_STRLEN(J9_UNMODIFIABLE_CLASS_ANNOTATION) < (size_t)'/'), "Annotation contains invalid characters");
#endif /* !TYPESTUBS_H */
#endif /* __cplusplus */
#endif /* 0 */

#endif /* J9_H */

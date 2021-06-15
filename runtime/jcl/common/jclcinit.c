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
#include <ctype.h>

#include "jcl.h"
#include "j9consts.h"
#include "jvminit.h"
#include "omrgcconsts.h"
#include "jni.h"
#include "j9protos.h"
#include "j9cp.h"
#include "jclprots.h"
#include "exelib_api.h"
#include "util_api.h"
#if defined(J9VM_OPT_SIDECAR)
#include "j2sever.h"
#endif
#include "ut_j9jcl.h"
#include "jclglob.h"
#include "j9version.h"
#include "omrversionstrings.h"
#include "j9modron.h"
#include "omr.h"
#include "vendor_version.h"

/* The vm version which must match the JCL.
 * It has the format 0xAABBCCCC
 *	AA - vm version, BB - jcl version, CCCC - master version
 * CCCC must match exactly with the JCL
 * Up the vm version (AA) when adding natives
 * BB is the required level of JCL to run the vm
 */
#define JCL_VERSION 0x06040270

extern void *jclConfig;

static UDATA
doJCLCheck(J9JavaVM *vm, J9Class *j9VMInternalsClass);


/*
	Calculate the value for java.vm.info (and java.fullversion) system/vm properties.
	Currently allocates into a fixed-size buffer. This really should be fixed.
*/
jint computeFullVersionString(J9JavaVM* vm)
{
	VMI_ACCESS_FROM_JAVAVM((JavaVM*)vm);
	PORT_ACCESS_FROM_JAVAVM(vm);
	const char *osarch = NULL;
	const char *osname = NULL;
	const char *j2se_version_info = NULL;
	const char *jitEnabled = "";
	const char *aotEnabled = "";
	const char *memInfo = NULL;
#define BUFFER_SIZE 512

	/* The actual allowed BUFFER_SIZE is 512, the extra 1 char is added to check for overflow */
	char vminfo[BUFFER_SIZE + 1];

#if defined(J9VM_INTERP_NATIVE_SUPPORT)
	J9JITConfig *jitConfig = vm->jitConfig;
	jitEnabled = "dis";
	aotEnabled = "dis";

	if (NULL != jitConfig) {
		if (J9_ARE_ALL_BITS_SET(jitConfig->runtimeFlags, J9JIT_JIT_ATTACHED)) {
			jitEnabled = "en";
		}
		if (J9_ARE_ALL_BITS_SET(jitConfig->runtimeFlags, J9JIT_AOT_ATTACHED)) {
			aotEnabled = "en";
		}
	}
	#define JIT_INFO " (JIT %sabled, AOT %sabled)\nOpenJ9   - "
#else
	#define JIT_INFO "%s%s"
#endif /* J9VM_INTERP_NATIVE_SUPPORT */

#if JAVA_SPEC_VERSION == 8
	if ((J2SE_VERSION(vm) & J2SE_RELEASE_MASK) == J2SE_18) {
		j2se_version_info = "1.8.0";
	} else {
		j2se_version_info = "1.8.?";
	}
#else /* JAVA_SPEC_VERSION == 8 */
	if ((J2SE_VERSION(vm) & J2SE_RELEASE_MASK) == J2SE_CURRENT_VERSION) {
		j2se_version_info = JAVA_SPEC_VERSION_STRING;
	} else {
		j2se_version_info = JAVA_SPEC_VERSION_STRING ".?";
	}
#endif /* JAVA_SPEC_VERSION == 8 */

	osname = j9sysinfo_get_OS_type();
	osarch = j9sysinfo_get_CPU_architecture();

#ifdef J9VM_ENV_DATA64
	memInfo = J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm) ? "64-Bit Compressed References": "64-Bit";
#else
	#if defined(J9ZOS390) || defined(S390)
		memInfo = "31-Bit";
	#else
		memInfo = "32-Bit";
	#endif
#endif

#if defined(J9VM_GC_MODRON_GC)
	#define OMR_INFO "\nOMR      - " OMR_VERSION_STRING
#else
	#define OMR_INFO ""
#endif /* J9VM_GC_MODRON_GC */

#if defined(OPENJDK_TAG) && defined(OPENJDK_SHA)
	#define OPENJDK_INFO "\nJCL      - " OPENJDK_SHA " based on " OPENJDK_TAG
#else
	#define OPENJDK_INFO ""
#endif /* OPENJDK_TAG && OPENJDK_SHA */

#if defined(VENDOR_SHORT_NAME) && defined(VENDOR_SHA)
	#define VENDOR_INFO "\n" VENDOR_SHORT_NAME "      - " VENDOR_SHA
#else
	#define VENDOR_INFO ""
#endif /* VENDOR_SHORT_NAME && VENDOR_SHA */

	if (BUFFER_SIZE <= j9str_printf(PORTLIB, vminfo, BUFFER_SIZE + 1,
			"JRE %s %s %s-%s %s" JIT_INFO J9VM_VERSION_STRING OMR_INFO VENDOR_INFO OPENJDK_INFO,
			j2se_version_info,
			(NULL != osname ? osname : " "),
			osarch,
			memInfo,
			EsBuildVersionString,
			jitEnabled,
			aotEnabled)) {
		j9tty_err_printf(PORTLIB, "\n%s - %d: %s: Error: Java VM info string exceeds buffer size\n", __FILE__, __LINE__, __FUNCTION__);
		return JNI_ERR;
	}

#undef BUFFER_SIZE
#undef JIT_INFO
#undef MEM_INFO
#undef OMR_INFO
#undef VENDOR_INFO

	(*VMI)->SetSystemProperty(VMI, "java.vm.info", vminfo);
	(*VMI)->SetSystemProperty(VMI, "java.fullversion", vminfo);
	return JNI_OK;
}


static jint initializeStaticMethod(J9JavaVM* vm, UDATA offset)
{
	J9ConstantPool * jclConstantPool = (J9ConstantPool *) vm->jclConstantPool;
	J9RAMStaticMethodRef * staticMethodConstantPool = (J9RAMStaticMethodRef *) vm->jclConstantPool;
	J9ROMMethodRef * romMethodConstantPool = (J9ROMMethodRef *) jclConstantPool->romConstantPool;
	J9ROMClass * jclROMClass = jclConstantPool->ramClass->romClass;
	UDATA cpType = J9_CP_TYPE(J9ROMCLASS_CPSHAPEDESCRIPTION(jclROMClass), offset);

	if ((J9CPTYPE_STATIC_METHOD == cpType) || (J9CPTYPE_INTERFACE_STATIC_METHOD == cpType)) {
		if (NULL == vm->internalVMFunctions->resolveStaticMethodRef(vm->mainThread, jclConstantPool, offset, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_JCL_CONSTANT_POOL)) {
			if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romMethodConstantPool[offset].classRefCPIndex)->value) {
				Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForMethodRef(vm->mainThread, romMethodConstantPool[offset].classRefCPIndex, offset);
			} else {
				Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, offset);
				return JNI_ERR;
			}
		} else {
			Trc_JCL_initializeKnownClasses_ResolvedStaticMethodRef(vm->mainThread, offset, staticMethodConstantPool[offset].method);
		}
	}

	return JNI_OK;
}

#if 0
static jint
initializeInstanceField(J9JavaVM* vm, UDATA offset)
{
	J9ConstantPool * jclConstantPool = (J9ConstantPool *) vm->jclConstantPool;
	J9RAMFieldRef * instanceFieldConstantPool = (J9RAMFieldRef *) vm->jclConstantPool;
	J9ROMFieldRef * romFieldConstantPool = (J9ROMFieldRef *) jclConstantPool->romConstantPool;
	J9ROMClass * jclROMClass = jclConstantPool->ramClass->romClass;
	U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(jclROMClass);

	if (J9CPTYPE_FIELD == J9_CP_TYPE(cpShapeDescription, offset)) {
		if (-1 == vm->internalVMFunctions->resolveInstanceFieldRef(vm->mainThread, NULL, jclConstantPool, offset, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, NULL)) {
			if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romFieldConstantPool[offset].classRefCPIndex)->value) {
				Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForInstanceFieldRef(vm->mainThread, romFieldConstantPool[offset].classRefCPIndex, offset);
			}  else {
				Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, offset);
				return JNI_ERR;
			}
		} else {
			Trc_JCL_initializeKnownClasses_ResolvedInstanceFieldRef(vm->mainThread, offset, instanceFieldConstantPool[offset].valueOffset);
		}
		return JNI_OK;
	}

	return JNI_EINVAL;
}
#endif

static jint
initializeStaticField(J9JavaVM* vm, UDATA offset, UDATA resolveFlags)
{
	J9ConstantPool * jclConstantPool = (J9ConstantPool *) vm->jclConstantPool;
	J9RAMStaticFieldRef * staticFieldConstantPool = (J9RAMStaticFieldRef *) vm->jclConstantPool;
	J9ROMFieldRef * romFieldConstantPool = (J9ROMFieldRef *) jclConstantPool->romConstantPool;
	J9ROMClass * jclROMClass = jclConstantPool->ramClass->romClass;
	U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(jclROMClass);

	if (J9CPTYPE_FIELD == J9_CP_TYPE(cpShapeDescription, offset)) {
		if (NULL == vm->internalVMFunctions->resolveStaticFieldRef(vm->mainThread, NULL, jclConstantPool, offset, resolveFlags, NULL)) {
			if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romFieldConstantPool[offset].classRefCPIndex)->value) {
				Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForStaticFieldRef(vm->mainThread, romFieldConstantPool[offset].classRefCPIndex, offset);
			} else {
				Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, offset);
				return JNI_ERR;
			}
		} else {
			Trc_JCL_initializeKnownClasses_ResolvedStaticFieldRef(vm->mainThread, offset, J9RAMSTATICFIELDREF_VALUEADDRESS(staticFieldConstantPool + offset));
		}
		return JNI_OK;
	}
	return JNI_EINVAL;
}

jint initializeKnownClasses(J9JavaVM* vm, U_32 runtimeFlags)
{
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ConstantPool * jclConstantPool = (J9ConstantPool *) vm->jclConstantPool;
	J9RAMFieldRef * instanceFieldConstantPool = (J9RAMFieldRef *) vm->jclConstantPool;
	J9RAMStaticFieldRef * staticFieldConstantPool = (J9RAMStaticFieldRef *) vm->jclConstantPool;
	J9RAMStaticMethodRef * staticMethodConstantPool = (J9RAMStaticMethodRef *) vm->jclConstantPool;
	J9RAMVirtualMethodRef * virtualMethodConstantPool = (J9RAMVirtualMethodRef *) vm->jclConstantPool;
	J9RAMSpecialMethodRef * specialMethodConstantPool = (J9RAMSpecialMethodRef *) vm->jclConstantPool;
	J9RAMInterfaceMethodRef * interfaceMethodConstantPool = (J9RAMInterfaceMethodRef *) vm->jclConstantPool;
	J9ROMFieldRef * romFieldConstantPool = (J9ROMFieldRef *) jclConstantPool->romConstantPool;
	J9ROMMethodRef * romMethodConstantPool = (J9ROMMethodRef *) jclConstantPool->romConstantPool;
	J9ROMClassRef * romClassConstantPool = (J9ROMClassRef *) jclConstantPool->romConstantPool;
	J9ROMClass * jclROMClass = jclConstantPool->ramClass->romClass;
	U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(jclROMClass);
	U_32 constPoolCount = jclROMClass->romConstantPoolCount;
	U_32 i;

	Trc_JCL_initializeKnownClasses_Entry(vm->mainThread);

	for (i = 0; i < constPoolCount; i++) {
		if (J9CPTYPE_FIELD == J9_CP_TYPE(cpShapeDescription, i)) {
			J9ROMClassRef* romClassRef = &romClassConstantPool[romMethodConstantPool[i].classRefCPIndex];

			if (0 == (romClassRef->runtimeFlags & runtimeFlags)) {
				Trc_JCL_initializeKnownClasses_SkippingResolve(vm->mainThread, i, romClassRef, romClassRef->runtimeFlags, runtimeFlags);
			} else {
				/* Try resolving as a static fieldref, then as an instance fieldref. */
				if (NULL != vmFuncs->resolveStaticFieldRef(vm->mainThread, NULL, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, NULL)) {
					Trc_JCL_initializeKnownClasses_ResolvedStaticFieldRef(vm->mainThread, i, J9RAMSTATICFIELDREF_VALUEADDRESS(staticFieldConstantPool + i));
				} else if (-1 != vmFuncs->resolveInstanceFieldRef(vm->mainThread, NULL, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, NULL)) {
					Trc_JCL_initializeKnownClasses_ResolvedInstanceFieldRef(vm->mainThread, i, instanceFieldConstantPool[i].valueOffset);
				} else if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romFieldConstantPool[i].classRefCPIndex)->value) {
					/* TODO replace this tracepoint with Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForFieldRef */
					Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForInstanceFieldRef(vm->mainThread, romFieldConstantPool[i].classRefCPIndex, i);
				}  else {
					Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, i);
					return JNI_ERR;
				}
			}
		} else if ((J9CPTYPE_INSTANCE_METHOD == J9_CP_TYPE(cpShapeDescription, i))
		|| (J9CPTYPE_INTERFACE_INSTANCE_METHOD == J9_CP_TYPE(cpShapeDescription, i))
		) {
			J9ROMClassRef* romClassRef = &romClassConstantPool[romMethodConstantPool[i].classRefCPIndex];

			if (0 == (romClassRef->runtimeFlags & runtimeFlags)) {
				Trc_JCL_initializeKnownClasses_SkippingResolve(vm->mainThread, i, romClassRef, romClassRef->runtimeFlags, runtimeFlags);
			} else {
				/* Resolve as both special and virtual method. It is an error for both to fail, but not for one to fail. */
				BOOLEAN resolved = FALSE;
				J9Method *resolvedMethod;
				if (0 != vmFuncs->resolveVirtualMethodRef(vm->mainThread, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_JCL_CONSTANT_POOL, &resolvedMethod)) {
					Trc_JCL_initializeKnownClasses_ResolvedVirtualMethodRef(vm->mainThread, i, virtualMethodConstantPool[i].methodIndexAndArgCount);
					resolved = TRUE;
				}
				if (NULL != vmFuncs->resolveSpecialMethodRef(vm->mainThread, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_JCL_CONSTANT_POOL)) {
					Trc_JCL_initializeKnownClasses_ResolvedSpecialMethodRef(vm->mainThread, i, specialMethodConstantPool[i].method);
					resolved = TRUE;
				}
				if (!resolved) {
					if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romMethodConstantPool[i].classRefCPIndex)->value) {
						Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForMethodRef(vm->mainThread, romMethodConstantPool[i].classRefCPIndex, i);
					} else {
						Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, i);
						return JNI_ERR;
					}
				}
			}
		} else if (J9CPTYPE_STATIC_METHOD == J9_CP_TYPE(cpShapeDescription, i)
		|| (J9CPTYPE_INTERFACE_STATIC_METHOD == J9_CP_TYPE(cpShapeDescription, i))
		) {
			J9ROMClassRef* romClassRef = &romClassConstantPool[romMethodConstantPool[i].classRefCPIndex];
			if (0 == (romClassRef->runtimeFlags & runtimeFlags)) {
				Trc_JCL_initializeKnownClasses_SkippingResolve(vm->mainThread, i, romClassRef, romClassRef->runtimeFlags, runtimeFlags);
			} else {
				if (NULL == vmFuncs->resolveStaticMethodRef(vm->mainThread, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_JCL_CONSTANT_POOL)) {
					if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romMethodConstantPool[i].classRefCPIndex)->value) {
						Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForMethodRef(vm->mainThread, romMethodConstantPool[i].classRefCPIndex, i);
					} else {
						Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, i);
						return JNI_ERR;
					}
				} else {
					Trc_JCL_initializeKnownClasses_ResolvedStaticMethodRef(vm->mainThread, i, staticMethodConstantPool[i].method);
				}
			}
		} else if (J9CPTYPE_INTERFACE_METHOD == J9_CP_TYPE(cpShapeDescription, i)) {
			J9ROMClassRef* romClassRef = &romClassConstantPool[romMethodConstantPool[i].classRefCPIndex];
			if (0 == (romClassRef->runtimeFlags & runtimeFlags)) {
				Trc_JCL_initializeKnownClasses_SkippingResolve(vm->mainThread, i, romClassRef, romClassRef->runtimeFlags, runtimeFlags);
			} else {
				if (NULL == vmFuncs->resolveInterfaceMethodRef(vm->mainThread, jclConstantPool, i, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_JCL_CONSTANT_POOL)) {
					if (NULL == J9VMCONSTANTPOOL_CLASSREF_AT(vm, romMethodConstantPool[i].classRefCPIndex)->value) {
						Trc_JCL_initializeKnownClasses_ClassRefNotResolvedForMethodRef(vm->mainThread, romMethodConstantPool[i].classRefCPIndex, i);
					} else {
						Trc_JCL_initializeKnownClasses_ExitError(vm->mainThread, i);
						return JNI_ERR;
					}
				} else {
					Trc_JCL_initializeKnownClasses_ResolvedInterfaceMethodRef(vm->mainThread, i, interfaceMethodConstantPool[i].methodIndexAndArgCount);
				}
			}
		}
	}

	Trc_JCL_initializeKnownClasses_Exit(vm->mainThread);

	return JNI_OK;
}


static UDATA
doJCLCheck(J9JavaVM *vm, J9Class *j9VMInternalsClass)
{
	J9VMThread *vmThread = vm->mainThread;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ROMStaticFieldShape *jclField;
	U_8 *cConfigPtr;
	U_8 *jclConfigPtr = NULL;
	UDATA jclVersion = -1;

	/* get the jcl specified by the class library (i.e. java.lang.J9VMInternals) */
	vmFuncs->staticFieldAddress(vmThread, j9VMInternalsClass, (U_8*)"j9Config", sizeof("j9Config") - 1, (U_8*)"J", 1, NULL, (UDATA *)&jclField, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, NULL);
	if (jclField != NULL) {
		jclConfigPtr = (U_8 *)&jclField->initialValue;
		/* get the jcl version from the class library (i.e. java.lang.J9VMInternals) */
		vmFuncs->staticFieldAddress(vmThread, j9VMInternalsClass, (U_8*)"j9Version", sizeof("j9Version") - 1, (U_8*)"I", 1, NULL, (UDATA *)&jclField, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, NULL);
		if (jclField != NULL) {
			jclVersion = jclField->initialValue;
		}
	}

	/* get the jcl specified by the DLL */
	cConfigPtr = (U_8 *)&jclConfig;

	/* check the values and report any errors */
	return checkJCL(vmThread, cConfigPtr, jclConfigPtr, JCL_VERSION, jclVersion);
}

/**
 * Initialize a static int field in a Class.
 *
 * @param[in] *vmThread the J9VMThread
 * @param[in] *clazz the J9Class containing the static field
 * @param[in] vmCPIndex the VM constant pool index of the static field ref
 * @param[in] value the int value to store in the field
 *
 * @return a JNI result code
 */
static jint
initializeStaticIntField(J9VMThread *vmThread, J9Class *clazz, UDATA vmCPIndex, I_32 value)
{
	jint rc = initializeStaticField(vmThread->javaVM, vmCPIndex, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL);
	if (JNI_OK == rc) {
		J9STATIC_I32_STORE(vmThread, clazz, J9VMCONSTANTPOOL_STATICFIELD_ADDRESS(vmThread->javaVM, vmCPIndex), value);
	}
	return rc;
}

typedef struct {
	const UDATA vmCPIndex;
	const I_32 value;
} J9IntConstantMapping;

static const J9IntConstantMapping intVMConstants[] = {
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_NONE, J9_GC_WRITE_BARRIER_TYPE_NONE },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_ALWAYS, J9_GC_WRITE_BARRIER_TYPE_ALWAYS },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_OLDCHECK, J9_GC_WRITE_BARRIER_TYPE_OLDCHECK },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_CARDMARK, J9_GC_WRITE_BARRIER_TYPE_CARDMARK },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_CARDMARK_INCREMENTAL, J9_GC_WRITE_BARRIER_TYPE_CARDMARK_INCREMENTAL },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_CARDMARK_AND_OLDCHECK, J9_GC_WRITE_BARRIER_TYPE_CARDMARK_AND_OLDCHECK },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_SATB, J9_GC_WRITE_BARRIER_TYPE_SATB },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE_SATB_AND_OLDCHECK, J9_GC_WRITE_BARRIER_TYPE_SATB_AND_OLDCHECK },

		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_ALLOCATION_TYPE_TLH, J9_GC_ALLOCATION_TYPE_TLH },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_ALLOCATION_TYPE_SEGREGATED, J9_GC_ALLOCATION_TYPE_SEGREGATED },

		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_OPTTHRUPUT, J9_GC_POLICY_OPTTHRUPUT },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_OPTAVGPAUSE, J9_GC_POLICY_OPTAVGPAUSE },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_GENCON, J9_GC_POLICY_GENCON },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_BALANCED, J9_GC_POLICY_BALANCED },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_METRONOME, J9_GC_POLICY_METRONOME },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY_NOGC, J9_GC_POLICY_NOGC },

		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_JAVA_CLASS_RAM_SHAPE_SHIFT, J9AccClassRAMShapeShift },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_OBJECT_HEADER_SHAPE_MASK, OBJECT_HEADER_SHAPE_MASK },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_INSTANCESIZE_OFFSET, offsetof(J9Class, totalInstanceSize) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_INSTANCE_DESCRIPTION_OFFSET, offsetof(J9Class, instanceDescription) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_LOCK_OFFSET_OFFSET, offsetof(J9Class, lockOffset) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_LOCK_RESERVATION_HISTORY_RESERVED_COUNTER_OFFSET, offsetof(J9Class, reservedCounter) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_LOCK_RESERVATION_HISTORY_CANCEL_COUNTER_OFFSET, offsetof(J9Class, cancelCounter) },

		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_INITIALIZE_STATUS_OFFSET, offsetof(J9Class, initializeStatus) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_SIZE, (I_32)sizeof(J9Class) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_ADDRESS_SIZE, (I_32)sizeof(UDATA) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_CLASS_DEPTH_AND_FLAGS_OFFSET, offsetof(J9Class, classDepthAndFlags) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_INIT_SUCCEEDED, J9ClassInitSucceeded  },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_SUPERCLASSES_OFFSET, offsetof(J9Class, superclasses) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_ROMCLASS_OFFSET, offsetof(J9Class, romClass) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9ROMCLASS_MODIFIERS_OFFSET, offsetof(J9ROMClass, modifiers) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_JAVA_CLASS_DEPTH_MASK, J9AccClassDepthMask },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_JAVA_CLASS_MASK, ~(J9_REQUIRED_CLASS_ALIGNMENT - 1) },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_ACC_CLASS_INTERNAL_PRIMITIVE_TYPE, J9AccClassInternalPrimitiveType },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_ACC_CLASS_ARRAY, J9AccClassArray },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS, OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS },
#if defined(J9VM_ENV_LITTLE_ENDIAN)
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_IS_BIG_ENDIAN, JNI_FALSE},
#else
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_IS_BIG_ENDIAN, JNI_TRUE},
#endif
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_CLASSLOADER_TYPE_OTHERS, J9_CLASSLOADER_TYPE_OTHERS },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_CLASSLOADER_TYPE_BOOT, J9_CLASSLOADER_TYPE_BOOT },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_CLASSLOADER_TYPE_PLATFORM, J9_CLASSLOADER_TYPE_PLATFORM },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9CLASS_RESERVABLE_LOCK_WORD_INIT, J9ClassReservableLockWordInit },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_OBJECT_HEADER_LOCK_RESERVED, OBJECT_HEADER_LOCK_RESERVED },
		{ J9VMCONSTANTPOOL_COMIBMOTIVMVM_OBJECT_HEADER_LOCK_LEARNING, OBJECT_HEADER_LOCK_LEARNING },
};

/**
 * Initialize all of the constants in com.ibm.oti.vm.VM
 *
 * @param[in] *currentThread the current J9VMThread
 *
 * @return a JNI result code
 */
static jint
intializeVMConstants(J9VMThread *currentThread)
{
	J9JavaVM *vm = currentThread->javaVM;
	OMR_VM *omrVM = (OMR_VM *)vm->omrVM;
	jint rc = JNI_ERR;
	UDATA i = 0;
	/* Load the VM class and mark it as initialized to prevent running the <clinit> */
	J9Class *vmClass = vm->internalVMFunctions->internalFindKnownClass(currentThread, J9VMCONSTANTPOOL_COMIBMOTIVMVM, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if ((NULL == vmClass) || (NULL != currentThread->currentException)) {
		goto done;
	}
	vmClass->initializeStatus = J9ClassInitSucceeded;

	/* Initialize non-constant fields */
	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_WRITE_BARRIER_TYPE, (I_32)vm->gcWriteBarrierType);
	if (JNI_OK != rc) {
		goto done;
	}
	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_ALLOCATION_TYPE, (I_32)vm->gcAllocationType);
	if (JNI_OK != rc) {
		goto done;
	}
	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_GC_POLICY, (I_32)omrVM->gcPolicy);
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_JIT_STRING_DEDUP_POLICY, vm->memoryManagerFunctions->j9gc_get_jit_string_dedup_policy(vm));
	if (JNI_OK != rc) {
		goto done;
	}
	
	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_J9_STRING_COMPRESSION_ENABLED, (I_32)IS_STRING_COMPRESSION_ENABLED_VM(vm));
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_OBJECT_HEADER_SIZE, (I_32)J9JAVAVM_OBJECT_HEADER_SIZE(vm));
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_FJ9OBJECT_SIZE, (I_32)J9JAVAVM_REFERENCE_SIZE(vm));
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_GLR_ENABLE_GLOBAL_LOCK_RESERVATION, (I_32)vm->enableGlobalLockReservation);
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_GLR_RESERVED_ABSOLUTE_THRESHOLD, (I_32)vm->reservedAbsoluteThreshold);
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_GLR_MINIMUM_RESERVED_RATIO, (I_32)vm->minimumReservedRatio);
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_GLR_CANCEL_ABSOLUTE_THRESHOLD, (I_32)vm->cancelAbsoluteThreshold);
	if (JNI_OK != rc) {
		goto done;
	}

	rc = initializeStaticIntField(currentThread, vmClass, J9VMCONSTANTPOOL_COMIBMOTIVMVM_GLR_MINIMUM_LEARNING_RATIO, (I_32)vm->minimumLearningRatio);
	if (JNI_OK != rc) {
		goto done;
	}

	/* Initialize constant int fields */
	for (i = 0; i < sizeof(intVMConstants) / sizeof(J9IntConstantMapping); ++i) {
			UDATA vmCPIndex = intVMConstants[i].vmCPIndex;
			I_32 value = intVMConstants[i].value;
			rc = initializeStaticIntField(currentThread, vmClass, vmCPIndex, value);
			if (JNI_OK != rc) {
				goto done;
			}
	}
done:
	return rc;
}

#define ADDMODS_PROPERTY_BASE "jdk.module.addmods."
UDATA
initializeRequiredClasses(J9VMThread *vmThread, char* dllName)
{
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9MemoryManagerFunctions *gcFuncs = vm->memoryManagerFunctions;
	UDATA jniVersion, i;
	J9Class *objectClass;
	J9Class *vmInternalsClass;
	J9Class *classClass;
	J9Class *clazz;
	J9Class *stringClass;
	J9Class *lockClass;
	J9ClassWalkState state;
	J9NativeLibrary* nativeLibrary = NULL;
	j9object_t oom;
	PORT_ACCESS_FROM_JAVAVM(vm);
	static UDATA requiredClasses[] = {
			J9VMCONSTANTPOOL_JAVALANGTHREAD,
			J9VMCONSTANTPOOL_JAVALANGCLASSLOADER,
			J9VMCONSTANTPOOL_JAVALANGSTACKTRACEELEMENT,
			J9VMCONSTANTPOOL_JAVALANGTHROWABLE,
			J9VMCONSTANTPOOL_JAVALANGSTACKOVERFLOWERROR,
			J9VMCONSTANTPOOL_JAVALANGCLASSNOTFOUNDEXCEPTION,
			J9VMCONSTANTPOOL_JAVALANGLINKAGEERROR,
			J9VMCONSTANTPOOL_JAVALANGNOCLASSDEFFOUNDERROR,
			J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION,
#if JAVA_SPEC_VERSION >= 15
			J9VMCONSTANTPOOL_JDKINTERNALLOADERNATIVELIBRARIES,
			J9VMCONSTANTPOOL_JDKINTERNALLOADERNATIVELIBRARIESNATIVELIBRARYIMPL,
#endif /* JAVA_SPEC_VERSION >= 15 */
	};

	/* Determine java/lang/String.value signature before any required class is initialized */
	if (J2SE_VERSION(vm) >= J2SE_V11) {
		vm->runtimeFlags |= J9_RUNTIME_STRING_BYTE_ARRAY;
	}

	/* CANNOT hold VM Access while calling registerBootstrapLibrary */
	vmFuncs->internalReleaseVMAccess(vmThread);
	
	if (vmFuncs->registerBootstrapLibrary(vmThread, dllName, &nativeLibrary, FALSE) != J9NATIVELIB_LOAD_OK) {
		return 1;
	}

	/* If we have a JitConfig, add the JIT dll to the bootstrap loader so we can add JNI natives in the JIT */
	if (NULL != vm->jitConfig) {
		J9NativeLibrary* jitLibrary = NULL;
	
		if (vmFuncs->registerBootstrapLibrary(vmThread, J9_JIT_DLL_NAME, &jitLibrary, FALSE) != J9NATIVELIB_LOAD_OK) {
			return 1;
		}
	}
	vmFuncs->internalAcquireVMAccess(vmThread);

	/* request an extra slot in java/lang/Module which we will use to connect native data to the Module object */
	if (0 != vmFuncs->addHiddenInstanceField(vm, "java/lang/Module", "modulePointer", "J", &vm->modulePointerOffset)) {
		return 1;
	}

#ifdef J9VM_OPT_OPENJDK_METHODHANDLE
	if (0 != vmFuncs->addHiddenInstanceField(vm, "java/lang/invoke/MemberName", "vmindex", "J", &vm->vmindexOffset)) {
		return 1;
	}

	if (0 != vmFuncs->addHiddenInstanceField(vm, "java/lang/invoke/MemberName", "vmtarget", "J", &vm->vmtargetOffset)) {
		return 1;
	}

	if (0 != vmFuncs->addHiddenInstanceField(vm, "java/lang/invoke/MutableCallSite", "invalidationCookie", "J", &vm->mutableCallSiteInvalidationCookieOffset)) {
		return 1;
	}
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */


	vmThread->privateFlags |= J9_PRIVATE_FLAGS_REPORT_ERROR_LOADING_CLASS;

	objectClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGOBJECT, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if (NULL == objectClass) {
		return 1;
	}
	vmInternalsClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGJ9VMINTERNALS, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if ((NULL == vmInternalsClass) || (NULL != vmThread->currentException)) {
		return 1;
	}
	vmInternalsClass->initializeStatus = J9ClassInitSucceeded;
	if (JNI_OK != intializeVMConstants(vmThread)) {
		return 1;
	}

	if (doJCLCheck(vm, vmInternalsClass) != 0) {
		return 1;
	}

	/* Load ClassInitializationLock as early as possible */

	lockClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGJ9VMINTERNALSCLASSINITIALIZATIONLOCK, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if ((NULL == lockClass) || (NULL != vmThread->currentException)) {
		return 1;
	}
	lockClass->initializeStatus = J9ClassInitSucceeded;

	/* Load java.lang.Class.  This makes sure that the slot in the known class array is filled in.  Since
	 * the slot was previously uninitialized, set the class of all existing classes java.lang.Class.  Subsequent
	 * class creations will correctly initialize the value from the slot in the known class array.  This code
	 * runs only in JNI_CreateJavaVM, so no need to mutex the class table access.
	 */

	classClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGCLASS, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if ((NULL == classClass) || (NULL != vmThread->currentException)) {
		return 1;
	}

	clazz = vmFuncs->allClassesStartDo(&state, vm, vm->systemClassLoader);
	do {
		j9object_t classObj = gcFuncs->J9AllocateObject(vmThread, classClass, J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE | J9_GC_ALLOCATE_OBJECT_HASHED);
		j9object_t lockObject;
		UDATA allocateFlags = J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE;

		if (NULL == classObj) {
			return 1;
		}
		J9VMJAVALANGCLASS_SET_VMREF(vmThread, classObj, clazz);
		clazz->classObject = classObj;
		lockObject = gcFuncs->J9AllocateObject(vmThread, lockClass, allocateFlags);
		classObj = clazz->classObject;
		if (lockObject == NULL) {
			return 1;
		}
		J9VMJAVALANGJ9VMINTERNALSCLASSINITIALIZATIONLOCK_SET_THECLASS(vmThread, lockObject, (j9object_t)classObj);
		J9VMJAVALANGCLASS_SET_INITIALIZATIONLOCK(vmThread, (j9object_t)classObj, lockObject);
	} while ((clazz = vmFuncs->allClassesNextDo(&state)) != NULL);
	vmFuncs->allClassesEndDo(&state);

	/* This runtime flag indicates that j.l.Class has been loaded, and all classes have a valid classObject.
	 * It is not safe to invoke GC before this point.
	 */
	vm->extendedRuntimeFlags |= J9_EXTENDED_RUNTIME_CLASS_OBJECT_ASSIGNED;

	if (vmFuncs->internalCreateBaseTypePrimitiveAndArrayClasses(vmThread) != 0) {
		return 1;
	}

	/* Initialize early since sendInitialize() uses this */ 
	if (initializeStaticMethod(vm, J9VMCONSTANTPOOL_JAVALANGJ9VMINTERNALS_INITIALIZATIONALREADYFAILED)) {
		return 1;
	}
	if (initializeStaticMethod(vm, J9VMCONSTANTPOOL_JAVALANGJ9VMINTERNALS_RECORDINITIALIZATIONFAILURE)) {
		return 1;
	}
	/* Load java.lang.String. This makes sure that the slot in the known class array is filled in.
	 * String may be needed to initialize Object.
	 */
	stringClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGSTRING, J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
	if ((NULL == stringClass) || (NULL != vmThread->currentException)) {
		return 1;
	}
	
	/* Initialize the java.lang.String.compressionFlag static field early enough so that we have
	 * access to it during the resolution of other classes in which Strings may need to be created
	 * in StringTable.cpp
	 */
	if (initializeStaticField(vm, J9VMCONSTANTPOOL_JAVALANGSTRING_COMPRESSIONFLAG, J9_RESOLVE_FLAG_NO_THROW_ON_FAIL | J9_RESOLVE_FLAG_NO_CLASS_INIT)) {
		return 1;
	}

	/* Run the JCL initialization code (what used to be JNI_OnLoad) */
	jniVersion = nativeLibrary->send_lifecycle_event(vmThread, nativeLibrary, "JCL_OnLoad", JNI_VERSION_1_1);
	if (!vmFuncs->jniVersionIsValid(jniVersion)) {
		return 1;
	}

	/* Initialize java.lang.String which will also initialize java.lang.Object. */
	vmFuncs->initializeClass(vmThread, stringClass);
	if (vmThread->currentException != NULL) {
		return 1;
	}

	/* Initialize java.lang.Class. */
	vmFuncs->initializeClass(vmThread, classClass);
	if (vmThread->currentException != NULL) {
		return 1;
	}

	/* Load some other needed classes. */
	for (i=0; i < sizeof(requiredClasses) / sizeof(UDATA); i++) {
		clazz = vmFuncs->internalFindKnownClass(vmThread, requiredClasses[i], J9_FINDKNOWNCLASS_FLAG_NON_FATAL);
		if ((NULL == clazz) || (NULL != vmThread->currentException)) {
			return 1;
		}
		vmFuncs->initializeClass(vmThread, clazz);
		if (NULL != vmThread->currentException) {
			return 1;
		}
	}

	if (J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_LOAD_AGENT_MODULE)
		&& (NULL != vm->modulesPathEntry->extraInfo))
	{
		const char *packageName = "jdk/internal/agent";
		const char *expectedModuleName = "jdk.management.agent";
		const char *moduleName = vm->jimageIntf->jimagePackageToModule(
				vm->jimageIntf, (UDATA) vm->modulesPathEntry->extraInfo,
				packageName);
		if (NULL == moduleName) {
			Trc_JCL_initializeRequiredClasses_moduleForPackageNotFound(vmThread, packageName);
		} else if (0 != strcmp(expectedModuleName, moduleName)) {
			Trc_JCL_initializeRequiredClasses_unexpectedModuleForPackage(vmThread, packageName, moduleName, expectedModuleName);
		} else {
			J9VMSystemProperty *systemProperty = NULL;
			Trc_JCL_initializeRequiredClasses_addAgentModuleEntry(vmThread, moduleName);
			/* Handle the case where there are no user-specified modules. In this case, there
			 * is typically one system property but no user-set "add-modules" arguments.
			 */
			if ((0 == vm->addModulesCount)
				&& (J9SYSPROP_ERROR_NONE == vmFuncs->getSystemProperty(vm, ADDMODS_PROPERTY_BASE "0", &systemProperty)))
			{
				/* this is implicitly an unused property */
				vmFuncs->setSystemProperty(vm, systemProperty, moduleName);
			} else {
				UDATA indexLen = j9str_printf(PORTLIB, NULL, 0, "%zu", vm->addModulesCount); /* get the length of the number string */
				char *propNameBuffer = j9mem_allocate_memory(sizeof(ADDMODS_PROPERTY_BASE) + indexLen, OMRMEM_CATEGORY_VM);
				if (NULL == propNameBuffer) {
					Trc_JCL_initializeRequiredClasses_addAgentModuleOutOfMemory(vmThread);
					return 1;
				}
				j9str_printf(PORTLIB, propNameBuffer, sizeof(ADDMODS_PROPERTY_BASE) + indexLen, ADDMODS_PROPERTY_BASE "%zu", vm->addModulesCount);
				Trc_JCL_initializeRequiredClasses_addAgentModuleSetProperty(vmThread, propNameBuffer, moduleName);
				vmFuncs->addSystemProperty(vm, propNameBuffer, moduleName, J9SYSPROP_FLAG_NAME_ALLOCATED);
			}
			vm->addModulesCount += 1;
		}
	}

	if (J9_ARE_ANY_BITS_SET(vm->extendedRuntimeFlags2, J9_EXTENDED_RUNTIME2_LOAD_HEALTHCENTER_MODULE)
		&& (NULL != vm->modulesPathEntry->extraInfo))
	{
		const char *packageName = "com/ibm/java/diagnostics/healthcenter/agent";
		const char *expectedModuleName = "ibm.healthcenter";
		const char *moduleName = vm->jimageIntf->jimagePackageToModule(
				vm->jimageIntf, (UDATA) vm->modulesPathEntry->extraInfo,
				packageName);
		if (NULL == moduleName) {
			Trc_JCL_initializeRequiredClasses_moduleForPackageNotFound(vmThread, packageName);
		} else if (0 != strcmp(expectedModuleName, moduleName)) {
			// package %s found in module %s (expected %s) - not loading"
			Trc_JCL_initializeRequiredClasses_unexpectedModuleForPackage(vmThread, packageName, moduleName, expectedModuleName);
		} else {
			J9VMSystemProperty *systemProperty = NULL;
			Trc_JCL_initializeRequiredClasses_addAgentModuleEntry(vmThread, moduleName);
			/* Handle the case where there are no user-specified modules. In this case, there
			 * is typically one system property but no user-set "add-modules" arguments.
			 */
			if ((0 == vm->addModulesCount)
				&& (J9SYSPROP_ERROR_NONE == vmFuncs->getSystemProperty(vm, ADDMODS_PROPERTY_BASE "0", &systemProperty)))
			{
				/* this is implicitly an unused property */
				vmFuncs->setSystemProperty(vm, systemProperty, moduleName);
			} else {
				UDATA indexLen = j9str_printf(PORTLIB, NULL, 0, "%zu", vm->addModulesCount); /* get the length of the number string */
				char *propNameBuffer = j9mem_allocate_memory(sizeof(ADDMODS_PROPERTY_BASE) + indexLen, OMRMEM_CATEGORY_VM);
				if (NULL == propNameBuffer) {
					Trc_JCL_initializeRequiredClasses_addAgentModuleOutOfMemory(vmThread);
					return 1;
				}
				j9str_printf(PORTLIB, propNameBuffer, sizeof(ADDMODS_PROPERTY_BASE) + indexLen, ADDMODS_PROPERTY_BASE "%zu", vm->addModulesCount);
				Trc_JCL_initializeRequiredClasses_addAgentModuleSetProperty(vmThread, propNameBuffer, moduleName);
				vmFuncs->addSystemProperty(vm, propNameBuffer, moduleName, J9SYSPROP_FLAG_NAME_ALLOCATED);
			}
			vm->addModulesCount += 1;
		}
	}

	vmThread->privateFlags &= (~J9_PRIVATE_FLAGS_REPORT_ERROR_LOADING_CLASS);

	/* Create an OutOfMemoryError now in case we run out during startup
	 * (1GDH3HI: J9VM:Neutrino - GPF reading manifest file during class loading).
	 */
	oom = vmFuncs->createCachedOutOfMemoryError(vmThread, NULL);
	if (NULL == oom) {
		return 1;
	}
	vmThread->outOfMemoryError = oom;

	return 0;
}
#undef ADDMODS_PROPERTY_BASE

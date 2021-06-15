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
#include <stdlib.h>

#include "j9.h"
#include "j9protos.h"
#include "j9user.h"
#include "util_api.h"
#include "jni.h"
#include "j9comp.h"
#include "vmaccess.h"
#include "j9consts.h"
#include "rommeth.h"
#include "j9port.h"
#include "j9cp.h"
#include "bcnames.h"
#include "pcstack.h"
#include "jvmti.h"
#include "jvmtiInternal.h"
#include "HeapIteratorAPI.h"
#include "ut_j9hshelp.h"
#include "j9modron.h"
#include "VM_MethodHandleKinds.h"
#include "j2sever.h"
#include "vrfytbl.h"
#include "bytecodewalk.h"

/* Static J9ITable used as a non-NULL iTable cache value by classes that don't implement any interfaces */
const J9ITable invalidITable = { (J9Class *) (UDATA) 0xDEADBEEF, 0, (J9ITable *) NULL };

#if defined(J9VM_INTERP_HOT_CODE_REPLACEMENT)

#define GET_SUPERCLASS(clazz) \
	((J9CLASS_DEPTH(clazz) == 0) ? NULL : \
		(clazz)->superclasses[J9CLASS_DEPTH(clazz) - 1])

#define NAME_AND_SIG_IDENTICAL(o1, o2, getNameMacro, getSigMacro) \
	areUTFPairsIdentical(getNameMacro(o1), getSigMacro(o1), getNameMacro(o2), getSigMacro(o2))

#define J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(o1, o2) \
	areUTFPairsIdentical(J9ROMMETHOD_NAME(o1), J9ROMMETHOD_SIGNATURE(o1), J9ROMMETHOD_NAME(o2), J9ROMMETHOD_SIGNATURE(o2))

static UDATA equivalenceHash (void *key, void *userData);
static UDATA equivalenceEquals (void *leftKey, void *rightKey, void *userData);
static jvmtiError addMethodEquivalence(J9VMThread * currentThread, J9Method * oldMethod, J9Method * newMethod, J9HashTable ** methodEquivalences, U_32 size);
static J9Method * getMethodEquivalence (J9VMThread * currentThread, J9Method * method, J9HashTable ** methodEquivalences);
static UDATA fixJNIFieldID PROTOTYPE((J9VMThread * currentThread, J9JNIFieldID * fieldID, J9Class * replacementRAMClass));
static UDATA areMethodRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2);
static UDATA areClassRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2);
static UDATA areFieldRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2);
static UDATA areNameAndSigsIdentical(J9ROMNameAndSignature * nas1, J9ROMNameAndSignature * nas2);
static UDATA areSingleSlotConstantRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2);
static UDATA areMethodsEquivalentPropagateCallSites(J9ROMMethod * method1, J9Class * ramClass1, J9ROMMethod * method2, J9Class * ramClass2);
static UDATA areMethodsEquivalentSub(J9ROMMethod * method1, J9ROMClass * romClass1, J9Class * ramClass1, J9ROMMethod * method2, J9ROMClass * romClass2, J9Class * ramClass2);
static UDATA areCallSiteDataMethodsEquivalent(J9ROMClass* romClass1, UDATA callSiteIndex1, J9ROMClass* romClass2, UDATA callSiteIndes2);
static UDATA areDoubleSlotConstantRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2);
static void fixClassSlot(J9VMThread* currentThread, J9Class** classSlot, J9HashTable *classPairs);
static void fixJNIFieldIDs(J9VMThread * currentThread, J9Class * originalClass, J9Class * replacementClass);
static void copyStaticFields (J9VMThread * currentThread, J9Class * originalRAMClass, J9Class * replacementRAMClass);
static void fixLoadingConstraints (J9JavaVM * vm, J9Class * oldClass, J9Class * newClass);
static UDATA classPairHash(void* entry, void* userData);
static UDATA classPairEquals(void* left, void* right, void* userData);
static UDATA findMethodInVTable(J9Method *method, UDATA *vTable);
static jvmtiError addClassesRequiringNewITables(J9JavaVM *vm, J9HashTable *classHashTable, UDATA *addedMethodCountPtr, UDATA *addedClassCountPtr, BOOLEAN fastHCR);
static jvmtiError verifyFieldsAreSame (J9VMThread * currentThread, UDATA fieldType, J9ROMClass * originalROMClass, J9ROMClass * replacementROMClass,
									   UDATA extensionsEnabled, UDATA * extensionsUsed);
static jvmtiError verifyMethodsAreSame (J9VMThread * currentThread, J9JVMTIClassPair * classPair, UDATA extensionsEnabled, UDATA * extensionsUsed);
static int compareClassDepth (const void *leftPair, const void *rightPair);
static UDATA utfsAreIdentical(J9UTF8 * utf1, J9UTF8 * utf2);
static UDATA areUTFPairsIdentical(J9UTF8 * leftUtf1, J9UTF8 * leftUtf2, J9UTF8 * rightUtf1, J9UTF8 * rightUtf2);
static jvmtiError fixJNIMethodID(J9VMThread *currentThread, J9Method *oldMethod, J9Method *newMethod, BOOLEAN equivalent, UDATA extensionsUsed);
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
static jvmtiIterationControl fixMemberNamesObjectIteratorCallback(J9JavaVM *vm, J9MM_IterateObjectDescriptor *objectDesc, void *userData);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

static jvmtiIterationControl fixHeapRefsHeapIteratorCallback(J9JavaVM *vm, J9MM_IterateHeapDescriptor *heapDesc, void *userData);
static jvmtiIterationControl fixHeapRefsSpaceIteratorCallback(J9JavaVM *vm, J9MM_IterateSpaceDescriptor *spaceDesc, void *userData);
static jvmtiIterationControl fixHeapRefsRegionIteratorCallback(J9JavaVM *vm, J9MM_IterateRegionDescriptor *regionDesc, void *userData);
static jvmtiIterationControl fixHeapRefsObjectIteratorCallback(J9JavaVM *vm, J9MM_IterateObjectDescriptor *objectDesc, void *userData);
static void replaceInAllClassLoaders(J9VMThread * currentThread, J9Class * originalRAMClass, J9Class * replacementRAMClass);
#ifdef J9VM_INTERP_NATIVE_SUPPORT
static jvmtiError jitEventInitialize(J9VMThread * currentThread, jint redefinedClassCount, UDATA redefinedMethodCount, J9JVMTIHCRJitEventData * eventData);
static void jitEventAddMethod(J9VMThread * currentThread, J9JVMTIHCRJitEventData * eventData, J9Method * oldMethod, J9Method * newMethod, UDATA equivalent);
static void jitEventAddClass(J9VMThread * currentThread, J9JVMTIHCRJitEventData * eventData, J9Class * originalRAMClass, J9Class * replacementRAMClass);
#endif
static void swapClassesForFastHCR(J9Class *originalClass, J9Class *newClass);

#undef  J9HSHELP_DEBUG_SANITY_CHECK
#define J9HSHELP_DEBUG 1

#ifdef J9HSHELP_DEBUG
static char *
getClassName(J9Class * c)
{
	static char buf[512];
	J9UTF8 * className = J9ROMCLASS_CLASSNAME(c->romClass);
	memcpy(buf, J9UTF8_DATA(className), J9UTF8_LENGTH(className));
	buf[J9UTF8_LENGTH(className)] = 0;
	return buf;
}
#endif

#ifdef J9HSHELP_DEBUG
static char *
getMethodName(J9Method * m)
{
	static char buf[512];
	J9UTF8 * n = J9ROMMETHOD_NAME(J9_ROM_METHOD_FROM_RAM_METHOD(m));
	memcpy(buf, J9UTF8_DATA(n), J9UTF8_LENGTH(n));
	buf[J9UTF8_LENGTH(n)] = 0;
	return buf;
}
#endif


#ifdef J9HSHELP_DEBUG_SANITY_CHECK
void
hcrSanityCheck(J9JavaVM * vm)
{
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
    J9Class * clazz;
	J9JVMTIClassPair * result;
	J9JVMTIClassPair   exemplar;
	J9ClassWalkState state;


	clazz = vmFuncs->allClassesStartDo(&state, vm, NULL);
	while (clazz != NULL) {

		/* Make sure all the arrayClass ptrs in obsolete classes point at something (naive null check) */
		if (J9_IS_CLASS_OBSOLETE(clazz)) {
			if (clazz->arrayClass == NULL) {
				abort();
			}
		}

		clazz = vmFuncs->allClassesNextDo(&state);
	}
	vmFuncs->allClassesEndDo(&state);

}
#endif


/**
 * @brief
 * Compares two J9UTF8s for identical contents.
 * @param first J9UTF8 (must not be NULL)
 * @param second J9UTF8 (must not be NULL)
 * @return TRUE if they are identical, FALSE otherwise
 */
static UDATA
utfsAreIdentical(J9UTF8 * utf1, J9UTF8 * utf2)
{
	if (J9UTF8_LENGTH(utf1) != J9UTF8_LENGTH(utf2)) {
		return FALSE;
	}

	return (memcmp(J9UTF8_DATA(utf1), J9UTF8_DATA(utf2), J9UTF8_LENGTH(utf1)) == 0) ? TRUE : FALSE;
}


/**
 * @brief
 * Compares two pairs of J9UTF8s for identical contents.
 * @param leftUtf1 J9UTF8 (must not be NULL)
 * @param leftUtf2 J9UTF8 (must not be NULL)
 * @param rightUtf1 J9UTF8 (must not be NULL)
 * @param rightUtf2 J9UTF8 (must not be NULL)
 * @return TRUE if they are identical, FALSE otherwise
 */
static UDATA
areUTFPairsIdentical(J9UTF8 * leftUtf1, J9UTF8 * leftUtf2, J9UTF8 * rightUtf1, J9UTF8 * rightUtf2)
{
	if ((J9UTF8_LENGTH(leftUtf1) != J9UTF8_LENGTH(rightUtf1)) || (J9UTF8_LENGTH(leftUtf2) != J9UTF8_LENGTH(rightUtf2))) {
		return FALSE;
	}
	if (0 != memcmp(J9UTF8_DATA(leftUtf1), J9UTF8_DATA(rightUtf1), J9UTF8_LENGTH(leftUtf1))) {
		return FALSE;
	}
	if (0 != memcmp(J9UTF8_DATA(leftUtf2), J9UTF8_DATA(rightUtf2), J9UTF8_LENGTH(leftUtf2))) {
		return FALSE;
	}
	return TRUE;
}


static UDATA
fixJNIFieldID(J9VMThread * currentThread, J9JNIFieldID * fieldID, J9Class * replacementRAMClass)
{
	J9ROMFieldShape * field = fieldID->field;
	J9ROMFieldShape * newField = NULL;
	UDATA offset;
	J9UTF8 * fieldName = J9ROMFIELDSHAPE_NAME(field);
	J9UTF8 * fieldSignature = J9ROMFIELDSHAPE_SIGNATURE(field);
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ROMClass * replacementROMClass;
	UDATA newFieldIndex;
	J9ROMFieldWalkState state;
	J9ROMFieldShape * current;

	if (field->modifiers & J9AccStatic) {
		J9ROMFieldShape * resolvedField;
		void * newFieldAddress;
		J9Class * declaringClass;

		newFieldAddress = vmFuncs->staticFieldAddress(
			currentThread,
			replacementRAMClass,
			J9UTF8_DATA(fieldName),
			J9UTF8_LENGTH(fieldName),
			J9UTF8_DATA(fieldSignature),
			J9UTF8_LENGTH(fieldSignature),
			&declaringClass,
			(UDATA *) &resolvedField,
			J9_LOOK_NO_JAVA,
			NULL);
		if ((newFieldAddress != NULL) && (J9_CURRENT_CLASS(declaringClass) == replacementRAMClass)) {
			offset = (UDATA) newFieldAddress - (UDATA) replacementRAMClass->ramStatics;
			newField = resolvedField;
		}
	} else {
		J9ROMFieldShape * resolvedField;
		UDATA newFieldOffset;
		J9Class * declaringClass;

		newFieldOffset = vmFuncs->instanceFieldOffset(
			currentThread,
			replacementRAMClass,
			J9UTF8_DATA(fieldName),
			J9UTF8_LENGTH(fieldName),
			J9UTF8_DATA(fieldSignature),
			J9UTF8_LENGTH(fieldSignature),
			&declaringClass,
			(UDATA *) &resolvedField,
			J9_LOOK_NO_JAVA);
		if ((newFieldOffset != -1) && (declaringClass == replacementRAMClass)) {
			offset = newFieldOffset;
			newField = resolvedField;
		}
	}

	if (newField == NULL) {
		return FALSE;
	}

	/* Determine the field index (field IDs are stored after method IDs in the array) */

	replacementROMClass = replacementRAMClass->romClass;
	newFieldIndex = replacementROMClass->romMethodCount;
	current = romFieldsStartDo(replacementROMClass, &state);
	while (newField != current) {
		++newFieldIndex;
		current = romFieldsNextDo(&state);
	}

	fieldID->index = newFieldIndex;
	fieldID->field = newField;
	fieldID->offset = offset;
	fieldID->declaringClass = replacementRAMClass;
	return TRUE;
}

static void
fixJNIFieldIDs(J9VMThread * currentThread, J9Class * originalClass, J9Class * replacementClass)
{
	void ** oldJNIIDs = originalClass->jniIDs;

	if (oldJNIIDs != NULL) {
		J9JavaVM * vm = currentThread->javaVM;
		void ** newJNIIDs;

		newJNIIDs = vm->internalVMFunctions->ensureJNIIDTable(currentThread, replacementClass);
		if (newJNIIDs == NULL) {
			/* do something */
			Assert_hshelp_ShouldNeverHappen();
		} else {
			J9ROMClass * originalROMClass = originalClass->romClass;
			UDATA oldMethodCount = originalROMClass->romMethodCount;
			UDATA oldFieldCount = originalROMClass->romFieldCount;
			UDATA oldFieldIndex;

			for (oldFieldIndex = oldMethodCount; oldFieldIndex < oldMethodCount + oldFieldCount; ++oldFieldIndex) {
				J9JNIFieldID * fieldID = oldJNIIDs[oldFieldIndex];

				if (fieldID != NULL) {
					/* Always invalidate the old field ID slot (do not free IDs for deleted fields, since a reflect field might be holding onto it) */

					oldJNIIDs[oldFieldIndex] = NULL;

					/* If the old field was not deleted, move the fieldID to the new class */

					if (fixJNIFieldID(currentThread, fieldID, replacementClass)) {
						newJNIIDs[fieldID->index] = fieldID;
					}
				}
			}
		}
	}
}

/*
 * @param currentThread
 * @param oldMethod
 * @param newMethod
 * @param equivalent
 *
 * if equivalent and old exists, copy old methodID to new method
 * if equivalent and old doesn't exists, create methodID for old method copy to new method
 * if non-equivalent and old exists, copy old methodID to new method, and create a new methodID for the old chain
 * if non-equivalent and old does not exist, do nothing
 * if new doesn't exist do nothing
 */
static jvmtiError
fixJNIMethodID(J9VMThread *currentThread, J9Method *oldMethod, J9Method *newMethod, BOOLEAN equivalent, UDATA extensionsUsed)
{
	jvmtiError rc = JVMTI_ERROR_NONE;
	if (NULL != newMethod) {
		J9JavaVM *vm = currentThread->javaVM;
		J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
		J9Class *oldMethodClass = J9_CLASS_FROM_METHOD(oldMethod);
		void **oldJNIIDs = oldMethodClass->jniIDs;
		UDATA oldMethodIndex = 0;
		J9JNIMethodID *oldMethodID = NULL;
		J9Class *newMethodClass = J9_CLASS_FROM_METHOD(newMethod);
		void **newJNIIDs = NULL;

		oldMethodIndex = getMethodIndex(oldMethod);
		if (equivalent) {
			if (NULL == oldJNIIDs) {
				oldJNIIDs = vmFuncs->ensureJNIIDTable(currentThread, oldMethodClass);
				if (NULL == oldJNIIDs) {
					rc = JVMTI_ERROR_OUT_OF_MEMORY;
					goto done;
				}
			}

			oldMethodID = oldJNIIDs[oldMethodIndex];

			/* we need to have a methodID so we can keep track of equivalences */
			if (NULL == oldMethodID) {
				oldMethodID = vmFuncs->getJNIMethodID(currentThread, oldMethod);
			}
		} else {
			/* NOT Equivalent case */
			J9JNIMethodID *oldMethodIDReplacement = NULL;
			J9Class *currentClass = oldMethodClass;

			if (NULL == oldJNIIDs) {
				goto done;
			}
			oldMethodID = oldJNIIDs[oldMethodIndex];
			if (NULL == oldMethodID) {
				goto done;
			}

			/* invalidate the old methodID and create a new one */
			oldJNIIDs[oldMethodIndex] = NULL;
			oldMethodIDReplacement = vmFuncs->getJNIMethodID(currentThread, oldMethod);
			if (NULL == oldMethodIDReplacement) {
				/* Put the MethodID back so that the chain is still correct */
				oldJNIIDs[oldMethodIndex] = oldMethodID;
				rc = JVMTI_ERROR_OUT_OF_MEMORY;
				goto done;
			}
			vmFuncs->initializeMethodID(currentThread, oldMethodIDReplacement, oldMethod);
			oldJNIIDs[oldMethodIndex] = oldMethodIDReplacement;

			/* walk back the class chain and fix up old equivalence */
			while (NULL != currentClass->replacedClass) {
				void **methodIDs = NULL;
				J9Method *equivalentMethod = NULL;
				BOOLEAN found = FALSE;
				
				currentClass = currentClass->replacedClass;
				methodIDs = currentClass->jniIDs;

				if (NULL != methodIDs) {
					UDATA methodIndex = 0;
					UDATA methodCount = currentClass->romClass->romMethodCount;
					for (methodIndex = 0; methodIndex < methodCount; methodIndex++) {
						if (methodIDs[methodIndex] == oldMethodID) {
							methodIDs[methodIndex] = oldMethodIDReplacement;
							found = TRUE;
							break;
						}
					}
				}
				if (!found) {
					/* method is not found in the currentClass, stop searching now */
					break;
				}
			}
		}

		if (NULL == newJNIIDs) {
			newJNIIDs = vmFuncs->ensureJNIIDTable(currentThread, newMethodClass);
			if (newJNIIDs == NULL) {
				rc = JVMTI_ERROR_OUT_OF_MEMORY;
				goto done;
			}
		}
		newJNIIDs[getMethodIndex(newMethod)] = oldMethodID;
		vmFuncs->initializeMethodID(currentThread, oldMethodID, newMethod);
	}
done:
	return rc;
}

static UDATA
areMethodRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2)
{
	J9ROMMethodRef * ref1 = (J9ROMMethodRef *) &romCP1[index1];
	J9ROMMethodRef * ref2 = (J9ROMMethodRef *) &romCP2[index2];

	return
		areClassRefsIdentical(romCP1, ref1->classRefCPIndex, romCP2, ref2->classRefCPIndex) &&
		areNameAndSigsIdentical(J9ROMMETHODREF_NAMEANDSIGNATURE(ref1), J9ROMMETHODREF_NAMEANDSIGNATURE(ref2));
}


static UDATA
areClassRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2)
{
	J9ROMClassRef * ref1 = (J9ROMClassRef *) &romCP1[index1];
	J9ROMClassRef * ref2 = (J9ROMClassRef *) &romCP2[index2];

	return utfsAreIdentical(J9ROMCLASSREF_NAME(ref1), J9ROMCLASSREF_NAME(ref2));
}


static UDATA
areFieldRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2)
{
	J9ROMFieldRef * ref1 = (J9ROMFieldRef *) &romCP1[index1];
	J9ROMFieldRef * ref2 = (J9ROMFieldRef *) &romCP2[index2];

	return
		areClassRefsIdentical(romCP1, ref1->classRefCPIndex, romCP2, ref2->classRefCPIndex) &&
		areNameAndSigsIdentical(J9ROMFIELDREF_NAMEANDSIGNATURE(ref1), J9ROMFIELDREF_NAMEANDSIGNATURE(ref2));
}


static UDATA
areNameAndSigsIdentical(J9ROMNameAndSignature * nas1, J9ROMNameAndSignature * nas2)
{
	return NAME_AND_SIG_IDENTICAL(nas1, nas2, J9ROMNAMEANDSIGNATURE_NAME, J9ROMNAMEANDSIGNATURE_SIGNATURE);
}


static UDATA
areSingleSlotConstantRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2)
{
	J9ROMSingleSlotConstantRef * ref1 = (J9ROMSingleSlotConstantRef *) &romCP1[index1];
	J9ROMSingleSlotConstantRef * ref2 = (J9ROMSingleSlotConstantRef *) &romCP2[index2];

	if (ref1->cpType != ref2->cpType) {
		return FALSE;
	}

	if ((ref1->cpType == J9DescriptionCpTypeScalar) || (ref1->cpType == J9DescriptionCpTypeMethodHandle)) {
		return ref1->data == ref2->data;
	}

	/* all remaining cases { class, string, methodtype } are single UTF8 slots */
	return utfsAreIdentical(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) ref1), J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) ref2));
}

/*
 * Helper methods for areCallSiteDataMethodsEquivalent
 */

/* ROM call site data header size (16 bits of method handle address + additional argument count) */
#define CALL_SITE_HEADER_OFFSET 2

static UDATA
isMethodHandleAField(UDATA cpType) 
{
	return (cpType == MH_REF_GETFIELD) || (cpType == MH_REF_GETSTATIC) || (cpType == MH_REF_PUTSTATIC) || (cpType == MH_REF_PUTFIELD);
}

static U_16*
findBSMDataAtIndex(U_16* bsmData, U_16 bsmIndex) 
{
	U_16 i = 0;
	for (i = 0; i < bsmIndex; i++) {
		/* increment by size of bsm data plus header */
		bsmData += (bsmData[1] + CALL_SITE_HEADER_OFFSET);
	}
	return bsmData;
}

static U_32
iterateToNextArgument(U_32 sigIndex, U_32 sigLength, U_8* sigData) 
{
	if (sigIndex >= sigLength) return sigIndex;

	/* check for object */
	if ('L' == sigData[sigIndex]) {
		while ((sigIndex < sigLength) && (';' != sigData[sigIndex])) {
			sigIndex += 1;
		}
	}
	/* for an object this will move past the ;, for a primitive this will move past the argument */
	sigIndex += 1;
	return sigIndex;
}

/**
 * Verify callsite equivalence. 
 * 
 * Structure of CallSiteData structure in ROM class (also see romclasswalk.c):
 * 
 * romClass->callSiteData : {
 * 	SRP callSiteNAS[romClass->callSiteCount]; // structures describing the resolved callsite. Note: SRP is 32 bits
 * 	U_16 callSiteBSMIndex[romClass->callSiteCount]; // map from callSiteIndex value to bootStrapMethodData index
 * 	{
 * 		U_16 bootStrapMethodHandleRef; // (header) index of the bsm's MethodHandle in the constant pool.
 *		U_16 argumentCount; // (header) number of bsm arguments in addition to the required three (MethodHandles.Lookup, String, MethodType).
 *		U_16 argument[argumentCount]; // The additional BSM arguments, these are constant pool indices.
 * 	} bootStrapMethodData[romClass->bsmCount];
 * }
 * 
 */
static UDATA
areCallSiteDataMethodsEquivalent(J9ROMClass* romClass1, UDATA callSiteIndex1, J9ROMClass* romClass2, UDATA callSiteIndex2)
{
	J9ROMConstantPoolItem *romCP1 = J9_ROM_CP_FROM_ROM_CLASS(romClass1);
	J9ROMConstantPoolItem *romCP2 = J9_ROM_CP_FROM_ROM_CLASS(romClass2);
	J9SRP *callSiteData1 = (J9SRP *) J9ROMCLASS_CALLSITEDATA(romClass1);
	J9SRP *callSiteData2 = (J9SRP *) J9ROMCLASS_CALLSITEDATA(romClass2);
	/* get call site name and signature */
	J9ROMNameAndSignature* nas1 = SRP_PTR_GET(callSiteData1 + callSiteIndex1, J9ROMNameAndSignature*);
	J9ROMNameAndSignature* nas2 = SRP_PTR_GET(callSiteData2 + callSiteIndex2, J9ROMNameAndSignature*);
	/* get call site BSM index */
	U_16 *bsmIndices1 = (U_16 *) (callSiteData1 + romClass1->callSiteCount);
	U_16 *bsmIndices2 = (U_16 *) (callSiteData2 + romClass2->callSiteCount);
	U_16 bsmIndex1 = bsmIndices1[callSiteIndex1];
	U_16 bsmIndex2 = bsmIndices2[callSiteIndex2];
	/* get top of bsm data */
	U_16 *bsmData1 = findBSMDataAtIndex(bsmIndices1 + romClass1->callSiteCount, bsmIndex1);
	U_16 *bsmData2 = findBSMDataAtIndex(bsmIndices2 + romClass2->callSiteCount, bsmIndex2);
	/* additional arg variables */
	U_16 additionalArgCount1 = bsmData1[1];
	U_16 additionalArgCount2 = bsmData2[1];
	/* BSM MethodHandle reference */
	J9ROMMethodHandleRef *mhRef1 = (J9ROMMethodHandleRef*) &romCP1[bsmData1[0]];
	J9ROMMethodHandleRef *mhRef2 = (J9ROMMethodHandleRef*) &romCP2[bsmData2[0]];
	U_32 bsmHandleTypeAndCpType1 = mhRef1->handleTypeAndCpType;
	U_32 bsmHandleTypeAndCpType2 = mhRef2->handleTypeAndCpType;
	U_32 bsmCpTypeIndex1 = bsmHandleTypeAndCpType1 & J9DescriptionCpTypeMask;
	U_32 bsmCpTypeIndex2 = bsmHandleTypeAndCpType2 & J9DescriptionCpTypeMask;
	UDATA bsmCpType1 = J9_CP_TYPE(J9ROMCLASS_CPSHAPEDESCRIPTION(romClass1), bsmCpTypeIndex1);
	UDATA bsmCpType2 = J9_CP_TYPE(J9ROMCLASS_CPSHAPEDESCRIPTION(romClass2), bsmCpTypeIndex2);
	/* additional helpers */
	J9ROMMethodRef* bsmMethod = NULL;
	J9ROMNameAndSignature *bsmNAS = NULL;
	J9UTF8 *bsmSig = NULL;
	U_8* sigData = NULL;
	U_32 sigIndex = 0;
	U_32 sigLength = 0;
	U_16 i = 0;

	/* compare method name and signature */
	if (!areNameAndSigsIdentical(nas1, nas2)) {
		return FALSE;
	}

	/* compare number of additional arguments */
	if (additionalArgCount1 != additionalArgCount2) {
		return FALSE;
	}

	/* verify that bsm MethodHandles are the same:
	 * step 1) verify that the handle types and cp types match
	 * step 2) verify that the method references are the same (at this point we know they are
	 * both method references)
	 */
	if (bsmHandleTypeAndCpType1 != bsmHandleTypeAndCpType2) {
		return FALSE;
	}

	/* Before continuing verify that the bsm MethodHandle refers to a Method and not a Field. The method
	 * signature will be used to determine
	 * whether the additional arguments are double or single slot which is impossible with a field.
	 * The indy call will fail at a later point. It is only necessary to check one because from the previous
	 * check both bsm1 and bsm2 are of the same type.
	 */
	if (isMethodHandleAField(bsmCpType1)) {
		return FALSE;
	}

	if (!areMethodRefsIdentical(romCP1, mhRef1->methodOrFieldRefIndex, romCP2, mhRef2->methodOrFieldRefIndex)) {
		return FALSE;
	}

	/* Compare additional argument types. We know that both call site entries have the same number of additional arguments.
	 * Each argument represents an index into the constant pool to be compared. If there are no additional arguments
	 * just skip this step.
	 */
	if (0 == additionalArgCount1) {
		return TRUE;
	}

	bsmMethod = (J9ROMMethodRef *) &romCP1[mhRef1->methodOrFieldRefIndex];
	bsmNAS = J9ROMMETHODREF_NAMEANDSIGNATURE(bsmMethod);
	bsmSig = J9ROMNAMEANDSIGNATURE_SIGNATURE(bsmNAS);
	sigData = J9UTF8_DATA(bsmSig);
	sigLength = J9UTF8_LENGTH(bsmSig);

	/* Iterate past the first three arguments which are mandatory (MethodHandle.Lookup, String, MethodType) */
	sigIndex += 1; /* move past first char '(' */

	for (i = 0; i < 3; i++) {
		sigIndex = iterateToNextArgument(sigIndex, sigLength, sigData);
	}

	/* compare additional arguments */
	for (i = 0; i < additionalArgCount1; i++) {
		if (sigIndex >= sigLength) {
			return FALSE;
		}

		if (('D' == sigData[sigIndex]) || ('J' == sigData[sigIndex])) {
			if (!areDoubleSlotConstantRefsIdentical
				(romCP1, bsmData1[i + CALL_SITE_HEADER_OFFSET], romCP2, bsmData2[i + CALL_SITE_HEADER_OFFSET])
			) {
				return FALSE;
			}
		} else {
			if (!areSingleSlotConstantRefsIdentical
				(romCP1, bsmData1[i + CALL_SITE_HEADER_OFFSET], romCP2, bsmData2[i + CALL_SITE_HEADER_OFFSET])
			) {
				return FALSE;
			}
		}

		sigIndex = iterateToNextArgument(sigIndex, sigLength, sigData);
	}

	/* verify that each argument was checked */
	if (i != additionalArgCount1) {
		return FALSE;
	}

	return TRUE;
}

static UDATA
areDoubleSlotConstantRefsIdentical(J9ROMConstantPoolItem * romCP1, U_32 index1, J9ROMConstantPoolItem * romCP2, U_32 index2)
{
	J9ROMConstantRef * ref1 = (J9ROMConstantRef *) &romCP1[index1];
	J9ROMConstantRef * ref2 = (J9ROMConstantRef *) &romCP2[index2];

	return (ref1->slot1 == ref2->slot1) && (ref1->slot2 == ref2->slot2);
}

/**
 * Compares two methods bytecode by bytecode to determine equivalence.
 * 
 * @param method1 first method to compare
 * @param romClass1 ROM class associated with method1
 * @param method2 second method to compare
 * @param romClass2 ROM class associated with method2
 * @return true if equivalent, false otherwise
 */
UDATA
areMethodsEquivalent(J9ROMMethod * method1, J9ROMClass * romClass1, J9ROMMethod * method2, J9ROMClass * romClass2)
{
	return areMethodsEquivalentSub(method1, romClass1, NULL, method2, romClass2, NULL);
}

/**
 * Compares two methods bytecode by bytecode to determine equivalence.
 * For invokedynamic if the referenced callsite was resolved in method1
 * the resolved MethodHandle will be copied to the RAM class callsite table
 * in method2.
 *
 * @param method1 first method to compare
 * param ramClass1 RAM class associated with method1
 * @param method2 second method to compare
 * @param ramClass2 RAM class associated with method2
 * @return true if equivalent, false otherwise
 */
static UDATA
areMethodsEquivalentPropagateCallSites(J9ROMMethod * method1, J9Class *ramClass1, J9ROMMethod * method2, J9Class *ramClass2) 
{
	return areMethodsEquivalentSub(method1, ramClass1->romClass, ramClass1, method2, ramClass2->romClass, ramClass2);
}

/**
 * Compares two methods bytecode by bytecode to determine equivalence. If invoke
 * dynamic callsites are determined to be equivalent and RAM classes are not null, resolved
 * callsites will be propagated from ramClass1 to ramClass2. It is assumed that callsites will
 * be propagated from 1 (original) to 2 (new).
 * 
 * @param method1 first method to compare
 * @param romClass1 ROM class associated with method1
 * @param ramClass1 RAM class associated with method1, must be provided to propagate callsites
 * @param method2 second method to compare
 * @param romClass2 ROM class associated with method2
 * @param ramClass2 RAM class associated with method2, must be provided to propagate callsites
 * @return true if equivalent, false otherwise
 */
static UDATA
areMethodsEquivalentSub(J9ROMMethod * method1, J9ROMClass * romClass1, J9Class * ramClass1, J9ROMMethod * method2, J9ROMClass * romClass2, J9Class * ramClass2)
{
	U_8 * bytecodes1;
	J9ROMConstantPoolItem * romCP1;
	U_8 * bytecodes2;
	J9ROMConstantPoolItem * romCP2;
	UDATA size;
	UDATA index;

#ifdef J9VM_ENV_LITTLE_ENDIAN
#define NEXT_U16(bytecodes) (((U_16 ) ((bytecodes)[index + alreadyCompared + 0])) + (((U_16 ) ((bytecodes)[index + alreadyCompared + 1])) << 8))
#else
#define NEXT_U16(bytecodes) (((U_16 ) ((bytecodes)[index + alreadyCompared + 1])) + (((U_16 ) ((bytecodes)[index + alreadyCompared + 0])) << 8))
#endif

	/* Modifiers must match exactly */

	if (method1->modifiers != method2->modifiers) {
		return FALSE;
	}

	/* Bytecode array sizes must match exactly */

	size = J9_BYTECODE_SIZE_FROM_ROM_METHOD(method1);
	if (size != J9_BYTECODE_SIZE_FROM_ROM_METHOD(method2)) {
		return FALSE;
	}

	/* For a native or an abstract method, there are no bytecodes.
	 * A native method has a method signature in place of bytecodes.
	 * Method signature is derived from the method descriptor.
	 * As the method descriptor has already been compared by the caller fixMethodEquivalencesAndCallSites(),
	 * there is no need to compare native method signature.
	 */
	if (J9_ARE_NO_BITS_SET(J9AccNative | J9AccAbstract, method1->modifiers)) {
		/* Walk the bytecodes in parallel looking for mismatches */

		bytecodes1 = J9_BYTECODE_START_FROM_ROM_METHOD(method1);
		romCP1 = J9_ROM_CP_FROM_ROM_CLASS(romClass1);
		bytecodes2 = J9_BYTECODE_START_FROM_ROM_METHOD(method2);
		romCP2 = J9_ROM_CP_FROM_ROM_CLASS(romClass2);
		index = 0;
		while (index < size) {
			U_8 bc = bytecodes1[index];
			UDATA bytecodeSize;
			UDATA alreadyCompared;
			U_8 bc2 = bytecodes2[index];

			/* Bytecode numbers/indices in each method must be identical */

			if (bc != bc2) {
				/* Treat all return instructions to JBgenericReturn */

				if (RTV_RETURN == (J9JavaBytecodeVerificationTable[bc] >> 8)) {
					bc = JBgenericReturn;
				}
				if (RTV_RETURN == (J9JavaBytecodeVerificationTable[bc2] >> 8)) {
					bc2 = JBgenericReturn;
				}
				if (bc != bc2) {
					return FALSE;
				}
			}
			alreadyCompared = 1;
			bytecodeSize = J9JavaInstructionSizeAndBranchActionTable[bc] & 7;

			switch (bc) {
				case JBcheckcast:
				case JBinstanceof:
				case JBmultianewarray:
				case JBnew:
				case JBanewarray:
					if (!areClassRefsIdentical(romCP1, NEXT_U16(bytecodes1), romCP2, NEXT_U16(bytecodes2))) {
						return FALSE;
					}
					alreadyCompared += 2;
					break;

				case JBinvokeinterface2:
					alreadyCompared += 2;
					/* Intentional fall-through */
				case JBinvokespecial:
				case JBinvokestatic:
				case JBinvokespecialsplit:
				case JBinvokestaticsplit:
				case JBinvokevirtual: {
					U_16 cpIndex1 = NEXT_U16(bytecodes1);
					U_16 cpIndex2 = NEXT_U16(bytecodes2);

					if (JBinvokestaticsplit == bc) {
						/* use cpIndex1 as index into slit table */
						cpIndex1 = *(U_16 *)(J9ROMCLASS_STATICSPLITMETHODREFINDEXES(romClass1) + cpIndex1);
					} else if (JBinvokespecialsplit == bc) {
						/* use cpIndex1 as index into slit table */
						cpIndex1 = *(U_16 *)(J9ROMCLASS_SPECIALSPLITMETHODREFINDEXES(romClass1) + cpIndex1);
					}

					if (JBinvokestaticsplit == bc2) {
						/* use cpIndex2 as index into slit table */
						cpIndex2 = *(U_16 *)(J9ROMCLASS_STATICSPLITMETHODREFINDEXES(romClass2) + cpIndex2);
					} else if (JBinvokespecialsplit == bc) {
						/* use cpIndex2 as index into slit table */
						cpIndex2 = *(U_16 *)(J9ROMCLASS_SPECIALSPLITMETHODREFINDEXES(romClass2) + cpIndex2);
					}

					if (!areMethodRefsIdentical(romCP1, cpIndex1, romCP2, cpIndex2)) {
						return FALSE;
					}
					alreadyCompared += 2;
					break;
				}

				case JBgetfield:
				case JBputfield:
				case JBgetstatic:
				case JBputstatic:
					if (!areFieldRefsIdentical(romCP1, NEXT_U16(bytecodes1), romCP2, NEXT_U16(bytecodes2))) {
						return FALSE;
					}
					alreadyCompared += 2;
					break;

				case JBldc:
					if (!areSingleSlotConstantRefsIdentical(romCP1, bytecodes1[index + alreadyCompared], romCP2, bytecodes2[index + alreadyCompared])) {
						return FALSE;
					}
					alreadyCompared += 1;
					break;

				case JBldcw:
					if (!areSingleSlotConstantRefsIdentical(romCP1, NEXT_U16(bytecodes1), romCP2, NEXT_U16(bytecodes2))) {
						return FALSE;
					}
					alreadyCompared += 2;
					break;

				case JBldc2dw:
				case JBldc2lw:
					if (!areDoubleSlotConstantRefsIdentical(romCP1, NEXT_U16(bytecodes1), romCP2, NEXT_U16(bytecodes2))) {
						return FALSE;
					}
					alreadyCompared += 2;
					break;

				case JBlookupswitch:
				case JBtableswitch: {
					UDATA tempIndex = index + (4 - (index & 3));
					UDATA numEntries;
					I_32 low;

					tempIndex += 4;
					low = *((I_32 *) (bytecodes1 + tempIndex));
					tempIndex += 4;

					if (bc == JBtableswitch) {
						I_32 high = *((I_32 *) (bytecodes1 + tempIndex));

						tempIndex += 4;
						numEntries = (UDATA) (high - low + 1);
					} else {
						numEntries = ((UDATA) low) * 2;
					}

					bytecodeSize = (tempIndex + (4 * numEntries)) - index;
					break;
				}
				case JBinvokedynamic: {
					U_16 callSiteIndex1 = NEXT_U16(bytecodes1);
					U_16 callSiteIndex2 = NEXT_U16(bytecodes2);
					if (!areCallSiteDataMethodsEquivalent(romClass1, callSiteIndex1, romClass2, callSiteIndex2)) {
						return FALSE;
					}

					/* if RAM classes are not provided do not attempt to propagate resolved callsite */
					if ((NULL != ramClass1) && (NULL != ramClass2)) {
						/* propagate call site data */
						if (NULL != ramClass1->callSites[callSiteIndex1]) {
							/* callsite resolution exists */
							ramClass2->callSites[callSiteIndex2] = ramClass1->callSites[callSiteIndex1];
						}
					}

					alreadyCompared += 2;
					break;
				}
			}

			/* Compare any remaining bytes that have not been treated */

			if (memcmp(bytecodes1 + index + alreadyCompared, bytecodes2 + index + alreadyCompared, bytecodeSize - alreadyCompared) != 0) {
				return FALSE;
			}

			index += bytecodeSize;
		}
	}

	return TRUE;

#undef NEXT_U16
}


void
fixSubclassHierarchy(J9VMThread * currentThread, J9HashTable * classPairs)
{
	J9JavaVM * vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;
	J9JVMTIClassPair ** array;
	J9JVMTIClassPair exemplar;
	J9JVMTIClassPair * result;
	UDATA classCount;
	UDATA i;

	/*
	 * Update the subclass traversal list with the replaced classes.
	 */
	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		J9Class *replacementClass = classPair->replacementClass.ramClass;
		if (NULL != replacementClass) {
			J9Class *originalClass = classPair->originalRAMClass;
			J9Class *previous = originalClass->subclassTraversalReverseLink;
			J9Class *next = originalClass->subclassTraversalLink;

			/* Put the new class into the subclass hierarchy */
			previous->subclassTraversalLink = replacementClass;
			next->subclassTraversalReverseLink = replacementClass;
			replacementClass->subclassTraversalReverseLink = previous;
			replacementClass->subclassTraversalLink = next;
			/* link this obsolete class to itself so that it won't have dangling pointers into the subclass traversal list */
			originalClass->subclassTraversalReverseLink = originalClass;
			originalClass->subclassTraversalLink = originalClass;
		}
		classPair = hashTableNextDo(&hashTableState);
	}

	/* copy the pairs in the hashTable into an array so that they may be sorted */
	classCount = hashTableGetCount(classPairs);
	array = j9mem_allocate_memory(classCount * sizeof(*array), OMRMEM_CATEGORY_VM);
	if (array == NULL) {
		return;
	}

	classPair = hashTableStartDo(classPairs, &hashTableState);
	for (i = 0; i < classCount; ++i) {
		array[i] = classPair;
		classPair = hashTableNextDo(&hashTableState);
	}

	/* sort the array so that superclasses appear before subclasses */
	J9_SORT(array, (UDATA)classCount, sizeof(J9JVMTIClassPair*), compareClassDepth);

	/* Add all the new classes to the graph */
	for (i = 0; i < classCount; ++i) {
		J9Class * replacementClass;
		J9Class * superclass;

		if (array[i]->replacementClass.ramClass) {
			replacementClass = array[i]->replacementClass.ramClass;
		} else {
			replacementClass = array[i]->originalRAMClass;
		}

		superclass = GET_SUPERCLASS(replacementClass);
		
		/* Find the correct superclass. If the superclass of replacementClass
		 * was replaced itself, make sure we use the new superclass 
		 */
		exemplar.originalRAMClass = superclass;
		result = hashTableFind(classPairs, &exemplar);
		if (NULL != result) {
			if (NULL != result->replacementClass.ramClass) {
				/* we found the original superclass inside the old-new
				 * mapping table and have determined a new RAM class
				 * was created. we should use the new superclass's RAM class
				 * from now on to fix up any possible reference to this class.
				 */
				superclass = result->replacementClass.ramClass;
			}
		}

		if (NULL != superclass) {
			/* Update classes->superclass array with possibly updated ramClass address */
			UDATA superclassCount = J9CLASS_DEPTH(superclass);
			memcpy(replacementClass->superclasses, superclass->superclasses, superclassCount * sizeof(UDATA));
			replacementClass->superclasses[superclassCount] = superclass;
		}
	}
}


/*
 * Examine the class stored in classSlot to determine if it has
 * been replaced. If it has, store the replacement class into
 * classSlot
 */
static void
fixClassSlot(J9VMThread* currentThread, J9Class** classSlot, J9HashTable *classPairs)
{
	J9JVMTIClassPair exemplar;
	J9JVMTIClassPair* result;

	exemplar.originalRAMClass = *classSlot;
	result = hashTableFind(classPairs, &exemplar);
	if (result != NULL) {
		/* replace the class slot only if we have a replacement class */
		if (result->replacementClass.ramClass) {
			*classSlot = result->replacementClass.ramClass;
		}
	}
}

/*
 * For each replaced interface in classPairs, update the iTables of
 * all implementers to point at the new version of the class.
 */
void
fixITables(J9VMThread * currentThread, J9HashTable * classPairs)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;
	UDATA anyInterfaces = FALSE;
	J9JavaVM* vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9Class* clazz;
	J9ClassWalkState classWalkState;

	/* first determine if any interfaces have been replaced */
	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		J9Class * originalRAMClass = classPair->originalRAMClass;

		if (originalRAMClass->romClass->modifiers & J9AccInterface) {
			anyInterfaces = TRUE;
			break;
		}
		classPair = hashTableNextDo(&hashTableState);
	}

	clazz = vmFuncs->allClassesStartDo(&classWalkState, vm, NULL);
	while (clazz != NULL) {
		if (anyInterfaces) {
			J9ITable *iTable = (J9ITable *)clazz->iTable;
			while( iTable != NULL ) {
				fixClassSlot(currentThread, &iTable->interfaceClass, classPairs);
				iTable = iTable->next;
			}
		}

		if (J9_IS_CLASS_OBSOLETE(clazz)) {
			fixClassSlot(currentThread, &clazz->arrayClass, classPairs);
		}

		clazz = vmFuncs->allClassesNextDo(&classWalkState);
	}
	vmFuncs->allClassesEndDo(&classWalkState);


	/* Iterate through replaced classes and their subclasses
	 *   - if the class has an iTable defined in a replaced class
	 *     change it to the newest version of the iTable
	 */

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {

		if (classPair->replacementClass.ramClass == NULL) {
			clazz = classPair->originalRAMClass;
		} else {
			clazz = classPair->replacementClass.ramClass;
		}

		clazz->lastITable = (J9ITable *) &invalidITable;

		if (clazz->iTable) {
			J9Class * superClass = GET_SUPERCLASS(clazz);
			J9JVMTIClassPair * result;
			J9JVMTIClassPair   exemplar;

			/* Look at the superclass of clazz in the hashtable
			 *   - if the superclass was replaced
			 *       - change any iTable structures in clazz to the one from the
			 *         new superclass version
			 */


			while (superClass) {
				exemplar.originalRAMClass = superClass;
				result = hashTableFind(classPairs, &exemplar);
				if (NULL != result) {
					if (result->replacementClass.ramClass) {

						/* Get the iTable of the replaced superclass */

						J9ITable * oldSuperITable = (J9ITable *) result->originalRAMClass->iTable;

						/* See if the old iTable is used as head of the clazz iTable list and
						 * replace it with the new iTable version. Otherwise keep looking by
						 * following the iTable linked list */

						if (((J9ITable *) clazz->iTable) == oldSuperITable) {
							clazz->iTable = result->replacementClass.ramClass->iTable;
						} else {
							J9ITable * iTable = (J9ITable *) clazz->iTable;
							while (iTable != NULL) {
								if (iTable->next == oldSuperITable) {
									iTable->next = (J9ITable *) result->replacementClass.ramClass->iTable;
								}
								iTable = iTable->next;
							}
						}
					}
				}
				superClass = GET_SUPERCLASS(superClass);
			}
		}

		classPair = hashTableNextDo(&hashTableState);
	}

	/* Find all obsolete classes and make sure they have their iTable pointing at the newest
	   version. This covers a scenario where we have the following class hierarchy:

	   C1 implements I1
	   C2 extends C1
	   C3 extends C2

	   Assume C2 and C3 have been replaced at least once and we are currently replacing C1.
	   We need to walk all old versions of C2 and C3 and replace their class->iTables with the
	   one from the most current version (but only once it itself has been fixed). Failure
	   to do so will cause MM_MarkingScheme::scanClass to fall off the end of its iTables
	   walk while scanning an old class version
	   CMVC 153732: Fast HCR Doesn't Share Superclass' ITable */

	/* TODO: We need to come up with another unused field in obsolete J9Classes to maintain
	   a chain of back references through all obsolete versions of a given class that terminates
	   at the oldest version. This class walk will then change to a classPairs hashtable walk
	   of the whole redefined hierarchy fixing up itables of all obsolete classes using the reverse
	   obsolete class chain */

	clazz = vmFuncs->allClassesStartDo(&classWalkState, vm, NULL);
	while (clazz != NULL) {
		if (J9_IS_CLASS_OBSOLETE(clazz)) {
			clazz->iTable = J9_CURRENT_CLASS(clazz)->iTable;
		}
		clazz = vmFuncs->allClassesNextDo(&classWalkState);
	}
	vmFuncs->allClassesEndDo(&classWalkState);

	return;
}

static UDATA
findMethodInVTable(J9Method *method, UDATA *vTable)
{
	J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(method);
	J9VTableHeader *vTableHeader = (J9VTableHeader *)vTable;
	J9Method **vTableMethods = J9VTABLE_FROM_HEADER(vTableHeader);
	UDATA vTableSize = vTableHeader->size;
	UDATA searchIndex = 0;

	/* Search the vTable for a public method of the correct name. */
	for (searchIndex = 0; searchIndex < vTableSize; searchIndex++) {
		J9Method *vTableMethod = vTableMethods[searchIndex];
		J9ROMMethod *vTableRomMethod = J9_ROM_METHOD_FROM_RAM_METHOD(vTableMethod);

		if (vTableRomMethod->modifiers & J9AccPublic) {
			if (J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(romMethod, vTableRomMethod)) {
				return searchIndex;
			}
		}
	}

	return (UDATA)-1;
}


/**
 * \brief   Method Pair hashing function
 * \ingroup jvmtiClass
 *
 * @param[in] entry     Hashtable entry, assumes J9JVMTIMethodPair type
 * @param[in] userData  opaque user data
 * @return hash result
 *
 */
static UDATA
redefinedClassPairHash(void* entry, void* userData)
{
	J9HotswappedClassPair * pair = entry;

	return (UDATA) pair->replacementClass / sizeof(J9Class *);
}

/**
 * \brief     Method Pair hash equality function
 * \ingroup   jvmtiClass
 *
 * @param[in] left      first hash item
 * @param[in] right     second hash item
 * @param[in] userData  opaque user data
 * @return  hash item equality result
 *
 */
static UDATA
redefinedClassPairEquals(void* left, void* right, void* userData)
{
	J9HotswappedClassPair * leftPair = left;
	J9HotswappedClassPair * rightPair = right;

	return leftPair->replacementClass == rightPair->replacementClass;
}


/**
 * \brief   Method Pair hashing function
 * \ingroup jvmtiClass
 *
 * @param[in] entry     Hashtable entry, assumes J9JVMTIMethodPair type
 * @param[in] userData  opaque user data
 * @return hash result
 *
 */
static UDATA
methodPairHash(void* entry, void* userData)
{
	J9JVMTIMethodPair* pair = entry;
	return (UDATA) pair->oldMethod / sizeof(J9Method *);
}

/**
 * \brief     Method Pair hash equality function
 * \ingroup   jvmtiClass
 *
 * @param[in] left      first hash item
 * @param[in] right     second hash item
 * @param[in] userData  opaque user data
 * @return  hash item equality result
 *
 */
static UDATA
methodPairEquals(void* left, void* right, void* userData)
{
	J9JVMTIMethodPair* leftPair = left;
	J9JVMTIMethodPair* rightPair = right;

	return leftPair->oldMethod == rightPair->oldMethod;
}

/**
 * \brief   Pre-allocate the method remap hash table
 * \ingroup jvmtiClass
 *
 *
 * @param[in] currentThread
 * @param[in] methodCount
 * @param[in] methodHashTable
 * @return
 *
 *	Allocate the hashtable that will hold method pairs for all replaced classes. We do not
 *	yet save the addresses that will replace these methods (not available at this point).
 *	Instead this step serves to reserve the hashtable to avoid memory allocation failures
 *	later on (where it might not be as easy to recover)
 */
static jvmtiError
preallocMethodHashTable(J9VMThread * currentThread, UDATA methodCount, J9HashTable ** methodHashTable)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	J9HashTable * ht;

	*methodHashTable = NULL;

	ht = hashTableNew(
			OMRPORT_FROM_J9PORT(PORTLIB),
			"JVMTIMethodPairs",
			(U_32)methodCount,
			sizeof(J9JVMTIMethodPair),
			sizeof(J9Method *),
			0,
			OMRMEM_CATEGORY_VM,
			methodPairHash,
			methodPairEquals,
			NULL,
			NULL);

	if (ht == NULL) {
		return JVMTI_ERROR_OUT_OF_MEMORY;
	}

	*methodHashTable = ht;

	return JVMTI_ERROR_NONE;
}

/**
 * \brief    Fix int and jit vtables for a set of redefines that did not use any extensions
 * \ingroup  hotswaphelp
 *
 *
 * @param currentThread     Current Thread
 * @param classPairs        A hashtable of Replacement classes (includes specifiedClasses) and their subclasses
 * @param methodPairs		A hashtable mapping old and new method pairs
 * @param fastHCR			true for fastHCR, else false
 * @param methodEquivalence	A hashtable of equivalent method pairs
 * @return
 *
 */
void
fixVTables_forNormalRedefine(J9VMThread *currentThread, J9HashTable *classPairs, J9HashTable *methodPairs,
							 BOOLEAN fastHCR, J9HashTable **methodEquivalences)
{
	J9HashTableState hashTableState;

	J9JavaVM* vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9JVMTIClassPair * classPair;

	UDATA i;
	J9VTableHeader * oldVTableHeader;
	J9VTableHeader * newVTableHeader;
	J9Method ** oldVTable;
	J9Method ** newVTable;
#ifdef J9VM_INTERP_NATIVE_SUPPORT
	UDATA * newJitVTable;
	UDATA * oldJitVTable;
#endif
	UDATA vTableSize;
	J9JVMTIMethodPair methodPair;
	J9JVMTIMethodPair * result;

	Trc_hshelp_fixVTables_forNormalRedefine_Entry(currentThread);


	Trc_hshelp_fixVTables_ExtensionUse(currentThread, "NOT used");

	/* The extensions have not been used, we do not have to fully recreate the vtables in all subclasses of the
	 * redefined class. Instead we replace the old method pointers with new once. */

	/* This HT walk assumes that the classPairs hashtable contains redefined classes AND their subclasses */
	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {

		/* Walk the vtable and replace any old method pointer with a new one (redefined) */

		oldVTableHeader = J9VTABLE_HEADER_FROM_RAM_CLASS(classPair->originalRAMClass);
		if (classPair->replacementClass.ramClass) {
			newVTableHeader = J9VTABLE_HEADER_FROM_RAM_CLASS(classPair->replacementClass.ramClass);
		} else {
			newVTableHeader = oldVTableHeader;
		}
		vTableSize = oldVTableHeader->size;

#ifdef J9VM_INTERP_NATIVE_SUPPORT
		oldJitVTable = JIT_VTABLE_START_ADDRESS(classPair->originalRAMClass);
		if (classPair->replacementClass.ramClass) {
			newJitVTable = JIT_VTABLE_START_ADDRESS(classPair->replacementClass.ramClass);
		} else {
			newJitVTable = oldJitVTable;
		}
#endif

		/* Under fastHCR, fix the vTable in place for the redefined class. */
		if (fastHCR && (0 != (classPair->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED))) {
			newVTableHeader = oldVTableHeader;
			newJitVTable = oldJitVTable;
		}

		Trc_hshelp_fixVTables_Shape(currentThread, vTableSize, getClassName(classPair->originalRAMClass));

		oldVTable = J9VTABLE_FROM_HEADER(oldVTableHeader);
		newVTable = J9VTABLE_FROM_HEADER(newVTableHeader);

		for (i = 0; i < vTableSize; i++) {
			/* Given the old method, find the corresponding new method and replace
			 * the old classes vtable entry (for the old method with the new method) */

			methodPair.oldMethod = oldVTable[i];

#ifdef J9VM_INTERP_NATIVE_SUPPORT
			Trc_hshelp_fixVTables_Search(currentThread, i,
						classPair->replacementClass.ramClass ? classPair->replacementClass.ramClass : classPair->originalRAMClass,
						methodPair.oldMethod, getMethodName(methodPair.oldMethod),
						vm->jitConfig ? oldJitVTable[0 - i] : 0, vm->jitConfig ? newJitVTable[0 - i] : 0);
#endif

			result = hashTableFind(methodPairs, &methodPair);
			if (result != NULL) {
				/* Replace the old method pointer with the redefined one */
				newVTable[i] = result->newMethod;
				Trc_hshelp_fixVTables_intVTableReplace(currentThread, i);

#ifdef J9VM_JIT_FULL_SPEED_DEBUG
				/* The subclasses of a redefined class (not necessarily redefined themselves) must have their jit
				 * vtable entry changed back to the default recompilation helper. This is done to force a recompilation
				 * of such methods. Once recompiled the jit will update the relevant jit vtable slots again
				 */
				if (vm->jitConfig) {
					if (NULL != getMethodEquivalence(currentThread, methodPair.oldMethod, methodEquivalences)) {
						/* Copy the compiled method address from the JIT vTable in the old class to the new class.
						 * We do not need to remap the order here because we're making the newVTable/newJitVTable
						 * have the same order of the oldVTable/oldJitVTable.
						 */
						newJitVTable[0 - i] = oldJitVTable[0 - i];
						Trc_hshelp_fixVTables_jitVTableReplace(currentThread, i);
					} else {
						vmFuncs->fillJITVTableSlot(currentThread, &newJitVTable[0 - i], result->newMethod);
					}
				}
#endif
			}
		}

		classPair = hashTableNextDo(&hashTableState);
	}

	Trc_hshelp_fixVTables_forNormalRedefine_Exit(currentThread);
}

/**
 * \brief  Fix static references
 * \ingroup
 *
 * @param[in] currentThread
 * @param[in] classPairs
 * @return
 *
 *	A redefined class introduces a set of replacement statics. JIT might hold references to the
 *	statics in the class we have just replaced. It is exceedingly complex to patch those refs
 *	so instead we change the replaced (old version) of the class such that its ramStatics refer
 *	to the new statics.
 *
 *	The classloader contains a hashtable of replaced classes. It maps the old class version to the
 *	new version. The hashtable is lazy-initialized, hence we do not incur any penalty if redefine
 *	is not used.
 *
 *  for all replacementClasses in classPairs
 *		is replacementClass (key) in its class loaders hotswappedClasses HT?
 *			get old version of class from hotswappedClasses HT
 *			fix static refs in old version using newest version
 *			is old version of class in HT?
 *				repeat with old version and older version
 *
 *
 *  Note that fixing static references is NOT required when extended redefinition features
 *  are used.
 *
 */
void
fixStaticRefs(J9VMThread * currentThread, J9HashTable * classPairs, UDATA extensionsUsed)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;
	J9ClassLoader * classLoader;
	J9HotswappedClassPair search;
	J9HotswappedClassPair * result;
	J9JavaVM* vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;

	/* If the extensions have been used, we do not need to fix static refs since we reresolve
	 * everything AND the JIT dumps the entire code cache hence there will be no static refs
	 * inlined that require fixing */

	if (extensionsUsed) {
		return;
	}


	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {

		/* Filter out classes that have not actually been redefined. The HT contains
		 * redefined classes and their subclasses */

		if ((classPair->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED) == 0) {
			classPair = hashTableNextDo(&hashTableState);
			continue;
		}

		/* Change the ramStatics to point at the statics of the oldest class version */

		classPair->replacementClass.ramClass->ramStatics = classPair->originalRAMClass->ramStatics;
		J9CLASS_EXTENDED_FLAGS_SET(classPair->replacementClass.ramClass, J9ClassReusedStatics);

		/* Get the classloader for the replacementClass. We need the hashtable of class replacements
		 * it knows about */

		classLoader = classPair->replacementClass.ramClass->classLoader;

		/* Find the entry for the class we are replacing */

		/* See if the HT if the classPair->originalRAMClass is a class that has replaced a prior version. */

		/* printf("static O: %p N: %p\n", classPair->originalRAMClass, classPair->replacementClass.ramClass); */
		search.replacementClass = classPair->originalRAMClass;
		result = hashTableFind(classLoader->redefinedClasses, &search);

		if (NULL != result) {

			/* if we found a prior replacement, ie the class we are replacing now has replaced
			 * an even older version of this class, then adjust the originalClass of the current
			 * entry to point at the oldest original version */

			search.originalClass = result->originalClass;

		} else {

			/* There was no prior replacement of this class.. save the classPair->originalRAMClass as the replaced class */

			search.originalClass = classPair->originalRAMClass;
		}

		/* Add the mapping to the HT */

		search.replacementClass = classPair->replacementClass.ramClass;

		/* Add a mapping that will be used by <code>findFieldInClass</code> to find the
		 * oldest version of this class given another newer version.
		 * search.replacementClass serves as the key for that hash table search. */

		if (NULL == hashTableAdd(classLoader->redefinedClasses, &search)) {
			/* TODO: this should not fail if we preallocated the correct number of hashtable nodes */
			return;
		}


#if !defined (J9VM_SIZE_SMALL_CODE)
		/* Remove the original ram class from the field cache */
		vmFuncs->fieldIndexTableRemove(vm, classPair->originalRAMClass);
#endif

		/* Get the next classPair */

		classPair = hashTableNextDo(&hashTableState);

	}

	return;
}


/*
 * For each replaced class in classPairs, update all of its array
 * classes to point at the new version of the class.
 */
void
fixArrayClasses(J9VMThread * currentThread, J9HashTable * classPairs)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9Class * clazz;
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;
	J9ClassWalkState state;

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		/* Note that the arrayClass in the originalRAMClass is already invalid by this point */
		J9Class * replacementRAMClass = classPair->replacementClass.ramClass;

		if (replacementRAMClass) {
			J9ArrayClass * arrayClass = (J9ArrayClass*)(replacementRAMClass->arrayClass);
			if (arrayClass != NULL) {
				/* update the componentType of the arity-1 array of the replaced class */
				arrayClass->componentType = replacementRAMClass;

				/* update the leafComponentType of all arity arrays of the replaced class */
				while (arrayClass != NULL) {
					arrayClass->leafComponentType = replacementRAMClass;
					arrayClass = (J9ArrayClass*)(arrayClass->arrayClass);
				}
			}
		}
		classPair = hashTableNextDo(&hashTableState);
	}


	/* Fix the class forwarding of previous versions of a obsolete class to the newest version of
	 * the corresponding class. The remapping must happen prior to reresolving of the constant pool
	 * otherwise we'll fail visibility checks in <code>checkVisibility()</code> run as part of field
	 * resolution. */

	clazz = vmFuncs->allClassesStartDo(&state, vm, NULL);
	while (clazz != NULL) {
		if (J9_IS_CLASS_OBSOLETE(clazz)) {
			/* If the class->arrayClass of an obsolete class version is found in our current
			   replacement classpair ht, then replace the clazz->arrayClass with the newest replacement
			   class.. This has the effect of ALL obsolete classes having their arrayClass point at the
			   oldest version of the class. We use this oldest version to resolve the static fields in
			   resolvefield.c */
			fixClassSlot(currentThread, &clazz->arrayClass, classPairs);
		}
		clazz = vmFuncs->allClassesNextDo(&state);
	}
	vmFuncs->allClassesEndDo(&state);

}


/*
 * CMVC 198908
 * In fastHCR code path, we fix method equivalence, then fix vtable indexes in J9MethodID using method
 * equivalence information, then fix all JNI ref accordingly.
 *
 * This method is invoked by fixJNIRefs() to ensure vtable indexes are updated in replaced class.
 * For detail, see design 44016.
 */
void
fixJNIMethodIDs(J9VMThread * currentThread, J9Class *originalRAMClass, J9Class *replacementRAMClass, UDATA extensionsUsed)
{
	if (originalRAMClass->romClass == replacementRAMClass->romClass) {
		U_32 methodIndex = 0;

		for (methodIndex = 0; methodIndex < originalRAMClass->romClass->romMethodCount; ++methodIndex) {
			J9Method * oldMethod = originalRAMClass->ramMethods + methodIndex;
			J9Method * newMethod = replacementRAMClass->ramMethods + methodIndex;

			fixJNIMethodID(currentThread, oldMethod, newMethod, TRUE, extensionsUsed);
		}
	} else {
		U_32 oldMethodIndex = 0;
		J9ROMClass *originalROMClass = originalRAMClass->romClass;
		J9ROMClass *replacementROMClass = replacementRAMClass->romClass;

		for (oldMethodIndex = 0; oldMethodIndex < originalROMClass->romMethodCount; ++oldMethodIndex) {
			J9Method * oldMethod = originalRAMClass->ramMethods + oldMethodIndex;
			J9Method * newMethod = NULL;
			J9ROMMethod * oldROMMethod = J9_ROM_METHOD_FROM_RAM_METHOD(oldMethod);
			U_32 newMethodIndex;
			BOOLEAN deleted = TRUE;
			BOOLEAN equivalent = FALSE;

			for (newMethodIndex = 0; newMethodIndex < replacementROMClass->romMethodCount; ++newMethodIndex) {
				J9ROMMethod * newROMMethod = NULL;

				newMethod = replacementRAMClass->ramMethods + newMethodIndex;
				newROMMethod = J9_ROM_METHOD_FROM_RAM_METHOD(newMethod);

				if (J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(oldROMMethod, newROMMethod)) {
					deleted = FALSE;
					equivalent = areMethodsEquivalent(oldROMMethod, originalROMClass, newROMMethod, replacementROMClass);
					break;
				}
			}
			fixJNIMethodID(currentThread, oldMethod, deleted ? NULL : newMethod, equivalent, extensionsUsed);
		}
	}
}

void
fixJNIRefs(J9VMThread * currentThread, J9HashTable * classPairs, BOOLEAN fastHCR, UDATA extensionsUsed)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		J9Class *originalRAMClass;
		J9Class *replacementRAMClass;

		if (classPair->replacementClass.ramClass) {
			if (fastHCR) {
				originalRAMClass = classPair->replacementClass.ramClass;
				replacementRAMClass = classPair->originalRAMClass;
				fixJNIMethodIDs(currentThread, originalRAMClass, replacementRAMClass, extensionsUsed);
			} else {
				originalRAMClass = classPair->originalRAMClass;
				replacementRAMClass = classPair->replacementClass.ramClass;
			}
			fixJNIFieldIDs(currentThread, originalRAMClass, replacementRAMClass);
		}

		classPair = hashTableNextDo(&hashTableState);
	}
}


typedef struct J9ThreadHashPair {
	J9VMThread *currentThread;
	void *userData;
} J9ThreadHashPair;

#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
void
fixMemberNames(J9VMThread * currentThread, J9HashTable * classHashTable)
{
	if (NULL != classHashTable) {
		PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);

		J9ThreadHashPair data;
		data.currentThread = currentThread;
		data.userData = classHashTable;

		/* iterate over all objects fixing up vmtarget refs if object is a MemberName */
		currentThread->javaVM->memoryManagerFunctions->j9mm_iterate_all_objects(currentThread->javaVM, PORTLIB, 0, fixMemberNamesObjectIteratorCallback, &data);
	}
}

static jvmtiIterationControl
fixMemberNamesObjectIteratorCallback(J9JavaVM *vm, J9MM_IterateObjectDescriptor *objectDesc, void *userData)
{
	J9ThreadHashPair *data = userData;
	J9VMThread *currentThread = data->currentThread;
	J9HashTable *classHashTable = data->userData;
	j9object_t object = objectDesc->object;
	J9Class *clazz = J9OBJECT_CLAZZ_VM(vm, object);

	if (clazz == J9VMJAVALANGINVOKEMEMBERNAME_OR_NULL(vm)) {
		U_64 vmindex = J9OBJECT_U64_LOAD(currentThread, object, vm->vmindexOffset);
		if (0 != vmindex) {
			J9JVMTIClassPair exemplar;
			J9JVMTIClassPair *result = NULL;

			j9object_t membernameClazz = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, object);
			exemplar.replacementClass.ramClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, membernameClazz);
			result = hashTableFind(classHashTable, &exemplar);
			if (NULL != result) {
				jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, object);
				if (J9_ARE_ANY_BITS_SET(flags, MN_IS_FIELD)) {
					/* Update vmtarget to vmindex->offset */
					J9JNIFieldID *fieldID = (J9JNIFieldID *)(UDATA)vmindex;
					J9ROMFieldShape *romField = fieldID->field;
					UDATA offset = fieldID->offset;

					if (J9_ARE_ANY_BITS_SET(romField->modifiers, J9AccStatic)) {
						offset |= J9_SUN_STATIC_FIELD_OFFSET_TAG;
						if (J9_ARE_ANY_BITS_SET(romField->modifiers, J9AccFinal)) {
							offset |= J9_SUN_FINAL_FIELD_OFFSET_TAG;
						}
					}
					J9OBJECT_U64_STORE(currentThread, object, vm->vmtargetOffset, (U_64)offset);
				} else if (J9_ARE_ANY_BITS_SET(flags, MN_IS_METHOD | MN_IS_CONSTRUCTOR)) {
					/* Update vmtarget to vmindex->method */
					J9JNIMethodID *methodID = (J9JNIMethodID *)(UDATA)vmindex;
					J9OBJECT_U64_STORE(currentThread, object, vm->vmtargetOffset, (U_64)(UDATA)methodID->method);
				}
			}
		}
	}

	return JVMTI_ITERATION_CONTINUE;
}
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */


/**
 * \brief  Reresolve the passed in constant pool
 * \ingroup jvmtiClass
 *
 * @param[in] ramConstantPool  class or jclConstantPool constant pool items to be reresolved
 * @param[in] currentThread    current thread
 * @param[in] classHashTable   hashtable of classes being hotswapped
 * @param[in] methodHashTable  hashtable of methods being hotswapped
 * @return
 *
 *	This call reresolves constant pool items for the passed in class constant pool or jclConstantPool.
 *  For class constant pools we clear the constant pool and run the same type of code as in <code>internalRunPreInitInstructions</code>
 *  For jclConstantPool we must reresolve the items in place since they are assumed to be already resolved.
 */
static void
reresolveHotSwappedConstantPool(J9ConstantPool * ramConstantPool, J9VMThread * currentThread,
								J9HashTable * classHashTable, J9HashTable * methodHashTable)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ROMClass * romClass = ramConstantPool->ramClass->romClass;
	UDATA methodTypeIndex = 0;
	UDATA ramConstantPoolCount = romClass->ramConstantPoolCount;


	if (ramConstantPoolCount != 0) {
		J9ROMConstantPoolItem * romConstantPool = ramConstantPool->romConstantPool;
		U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(romClass);
		UDATA resolveFlagsBase = J9_RESOLVE_FLAG_REDEFINE_CLASS;
		UDATA i;

		if (ramConstantPool == (J9ConstantPool *) vm->jclConstantPool) {
			resolveFlagsBase |= J9_RESOLVE_FLAG_JCL_CONSTANT_POOL;
		}


		for (i = 0; i < ramConstantPoolCount; ++i) {

			switch (J9_CP_TYPE(cpShapeDescription, i)) {
				J9ROMMethodRef * romMethodRef;
				J9ROMNameAndSignature * nas;
				J9Method *resolvedMethod;

				case J9CPTYPE_INT:
				case J9CPTYPE_FLOAT:
				case J9CPTYPE_STRING:
				case J9CPTYPE_ANNOTATION_UTF8:
				case J9CPTYPE_FIELD:
					break;

				case J9CPTYPE_HANDLE_METHOD:
					if (ramConstantPool != (J9ConstantPool *) vm->jclConstantPool) {
						romMethodRef = ((J9ROMMethodRef *) romConstantPool) + i;
						nas = J9ROMMETHODREF_NAMEANDSIGNATURE(romMethodRef);
						((J9RAMMethodRef *) ramConstantPool)[i].method = vm->initialMethods.initialStaticMethod;
						((J9RAMMethodRef *) ramConstantPool)[i].methodIndexAndArgCount = (methodTypeIndex << 8) +
							getSendSlotsFromSignature(J9UTF8_DATA(J9ROMNAMEANDSIGNATURE_SIGNATURE(nas)));
						methodTypeIndex++;
					} else {
						vmFuncs->resolveVirtualMethodRef(currentThread, ramConstantPool, i,
																	 resolveFlagsBase | J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, &resolvedMethod);
					}

					break;

				case J9CPTYPE_INSTANCE_METHOD:
				case J9CPTYPE_INTERFACE_INSTANCE_METHOD:

					if (ramConstantPool != (J9ConstantPool *) vm->jclConstantPool) {
						romMethodRef = ((J9ROMMethodRef *) romConstantPool) + i;
						nas = J9ROMMETHODREF_NAMEANDSIGNATURE(romMethodRef);
						((J9RAMMethodRef *) ramConstantPool)[i].method = vm->initialMethods.initialSpecialMethod;
						((J9RAMMethodRef *) ramConstantPool)[i].methodIndexAndArgCount = (J9VTABLE_INITIAL_VIRTUAL_OFFSET << 8) +
							getSendSlotsFromSignature(J9UTF8_DATA(J9ROMNAMEANDSIGNATURE_SIGNATURE(nas)));
					} else {
						/* Try to resolve as virtual and as special */
						vmFuncs->resolveVirtualMethodRef(currentThread, ramConstantPool, i,
																	 	 resolveFlagsBase | J9_RESOLVE_FLAG_NO_THROW_ON_FAIL, &resolvedMethod);
						vmFuncs->resolveSpecialMethodRef(currentThread, ramConstantPool, i,
																		 resolveFlagsBase | J9_RESOLVE_FLAG_NO_THROW_ON_FAIL);
					}

					break;

				case J9CPTYPE_STATIC_METHOD:
				case J9CPTYPE_INTERFACE_STATIC_METHOD:

					if (ramConstantPool != (J9ConstantPool *) vm->jclConstantPool) {
						romMethodRef = ((J9ROMMethodRef *) romConstantPool) + i;
						nas = J9ROMMETHODREF_NAMEANDSIGNATURE(romMethodRef);
						/* Set methodIndex to initial virtual method, just as we do in internalRunPreInitInstructions() */
						((J9RAMStaticMethodRef *) ramConstantPool)[i].methodIndexAndArgCount = (J9VTABLE_INITIAL_VIRTUAL_OFFSET << 8) +
							getSendSlotsFromSignature(J9UTF8_DATA(J9ROMNAMEANDSIGNATURE_SIGNATURE(nas)));
						((J9RAMStaticMethodRef *) ramConstantPool)[i].method = vm->initialMethods.initialStaticMethod;
					} else {
						vmFuncs->resolveStaticMethodRef(currentThread, ramConstantPool, i,
																		resolveFlagsBase | J9_RESOLVE_FLAG_NO_THROW_ON_FAIL);
					}

					break;


				case J9CPTYPE_INTERFACE_METHOD:

					if (ramConstantPool != (J9ConstantPool *) vm->jclConstantPool) {
						romMethodRef = ((J9ROMMethodRef *) romConstantPool) + i;
						nas = J9ROMMETHODREF_NAMEANDSIGNATURE(romMethodRef);
						((J9RAMInterfaceMethodRef *) ramConstantPool)[i].interfaceClass = 0;
						((J9RAMInterfaceMethodRef *) ramConstantPool)[i].methodIndexAndArgCount = J9_ITABLE_INDEX_UNRESOLVED | getSendSlotsFromSignature(J9UTF8_DATA(J9ROMNAMEANDSIGNATURE_SIGNATURE(nas)));
					} else {
						vmFuncs->resolveInterfaceMethodRef(currentThread, ramConstantPool, i,
																			resolveFlagsBase | J9_RESOLVE_FLAG_NO_THROW_ON_FAIL);
					}

					break;

				case J9CPTYPE_CLASS:
					if (((J9RAMClassRef *) ramConstantPool)[i].value != NULL) {
						J9Class * klass = ((J9RAMClassRef *) ramConstantPool)[i].value;
						if (J9CLASS_FLAGS(klass) & J9AccClassHotSwappedOut) {
							J9JVMTIClassPair exemplar;
							J9JVMTIClassPair * result;
							exemplar.originalRAMClass = ((J9RAMClassRef *) ramConstantPool)[i].value;
							result = hashTableFind(classHashTable, &exemplar);
							if (NULL != result) {
								((J9RAMClassRef *) ramConstantPool)[i].value = result->replacementClass.ramClass;
								((J9RAMClassRef *) ramConstantPool)[i].modifiers = result->replacementClass.romClass->modifiers;
							}
						}
					}
					break;

			}

		}
	}
}

static void
fixRAMConstantPoolForFastHCR(J9ConstantPool *ramConstantPool, J9HashTable *classHashTable, J9HashTable *methodHashTable, J9Class *objectClass)
{
	J9ROMClass *romClass = ramConstantPool->ramClass->romClass;
	U_32 *cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(romClass);
	J9JVMTIMethodPair methodPair;
	J9JVMTIMethodPair *methodResult;
	J9JVMTIClassPair classPair;
	J9JVMTIClassPair *classResult;
	UDATA ramConstantPoolCount = romClass->ramConstantPoolCount;
	UDATA cpIndex;

	for (cpIndex = 0; cpIndex < ramConstantPoolCount; cpIndex++) {
		switch (J9_CP_TYPE(cpShapeDescription, cpIndex)) {
			case J9CPTYPE_INSTANCE_METHOD: /* Fall through */
			case J9CPTYPE_INTERFACE_INSTANCE_METHOD: /* Fall through */
			case J9CPTYPE_HANDLE_METHOD: {
				J9RAMMethodRef *methodRef = (J9RAMMethodRef *) &ramConstantPool[cpIndex];

				methodPair.oldMethod = methodRef->method;
				methodResult = hashTableFind(methodHashTable, &methodPair);
				if (NULL != methodResult) {
					methodRef->method = methodResult->newMethod;
				}
				break;
			}
			case J9CPTYPE_STATIC_METHOD: /* Fall through */
			case J9CPTYPE_INTERFACE_STATIC_METHOD: {
				J9RAMStaticMethodRef *methodRef = (J9RAMStaticMethodRef *) &ramConstantPool[cpIndex];

				methodPair.oldMethod = methodRef->method;
				methodResult = hashTableFind(methodHashTable, &methodPair);
				if (NULL != methodResult) {
					methodRef->method = methodResult->newMethod;
				}
				break;
			}
			case J9CPTYPE_INTERFACE_METHOD: {
				J9RAMInterfaceMethodRef *methodRef = (J9RAMInterfaceMethodRef *) &ramConstantPool[cpIndex];
				J9Class *resolvedClass = (J9Class *) methodRef->interfaceClass;
				/* Don't fix unresolved entries */
				if (NULL != resolvedClass) {
					UDATA methodIndexAndArgCount = methodRef->methodIndexAndArgCount;
					UDATA methodIndex = methodIndexAndArgCount >> J9_ITABLE_INDEX_SHIFT;
					UDATA newMethodIndex = methodIndex;
					UDATA tagsAndArgCount = methodIndexAndArgCount & (J9_ITABLE_INDEX_TAG_BITS | 0xFF);
					/* In fast HCR, both vTable and iTable indices are stable, so only the direct method
					 * (via RAM method index) case can possibly be affected by method reordering.
					 */
					if (J9_ARE_ANY_BITS_SET(methodIndexAndArgCount, J9_ITABLE_INDEX_METHOD_INDEX)) {
						/* Private interface method or non-vTable Object method */
						if (J9_ARE_ANY_BITS_SET(methodIndexAndArgCount, J9_ITABLE_INDEX_OBJECT)) {
							resolvedClass = objectClass;
						}
						classPair.originalRAMClass = resolvedClass;
						classResult = hashTableFind(classHashTable, &classPair);
						/* If the class was not replaced, no need to update the constant pool */
						if (NULL != classResult) {
							J9Class *obsoleteClass = classResult->replacementClass.ramClass;
							if (NULL != obsoleteClass) {
								/* If the referenced method was not reordered, no need to update the constant pool */
								methodPair.oldMethod = obsoleteClass->ramMethods + methodIndex;
								methodResult = hashTableFind(methodHashTable, &methodPair);
								if (NULL != methodResult) {
									J9Method *newMethod = methodResult->newMethod;
									newMethodIndex = newMethod - J9_CLASS_FROM_METHOD(newMethod)->ramMethods;
									/* Fix the index in the resolved CP entry, retaining the argCount and tag bits */
									methodRef->methodIndexAndArgCount = ((newMethodIndex << J9_ITABLE_INDEX_SHIFT) | tagsAndArgCount);
								}
							}
						}
					}
				}
				break;
			}
		}
	}
}

static void
fixRAMSplitTablesForFastHCR(J9Class *clazz, J9HashTable *methodHashTable)
{
	U_16 i = 0;
	J9JVMTIMethodPair methodPair;
	J9JVMTIMethodPair *methodResult = NULL;

	if (NULL != clazz->staticSplitMethodTable) {
		J9Method **splitTable = clazz->staticSplitMethodTable;

		for (i = 0; i < clazz->romClass->staticSplitMethodRefCount; i++) {
			methodPair.oldMethod = splitTable[i];
			methodResult = hashTableFind(methodHashTable, &methodPair);
			if (NULL != methodResult) {
				splitTable[i] = methodResult->newMethod;
			}
		}
	}

	if (NULL != clazz->specialSplitMethodTable) {
		J9Method **splitTable = clazz->specialSplitMethodTable;

		for (i = 0; i < clazz->romClass->specialSplitMethodRefCount; i++) {
			methodPair.oldMethod = splitTable[i];
			methodResult = hashTableFind(methodHashTable, &methodPair);
			if (NULL != methodResult) {
				splitTable[i] = methodResult->newMethod;
			}
		}
	}
}


void
fixConstantPoolsForFastHCR(J9VMThread *currentThread, J9HashTable *classPairs, J9HashTable *methodPairs)
{
	J9ClassWalkState state;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9Class *clazz = vmFuncs->allClassesStartDo(&state, vm, NULL);
	J9Class *objectClass = J9VMJAVALANGOBJECT_OR_NULL(currentThread->javaVM);

	while (NULL != clazz) {
		if (0 != clazz->romClass->ramConstantPoolCount) {
			/* NOTE: We must fix up the constant pool even if the class is obsolete, as
			 * this is necessary to invoke new methods from running old methods.
			 */
			fixRAMConstantPoolForFastHCR(J9_CP_FROM_CLASS(clazz), classPairs, methodPairs, objectClass);
		}

		fixRAMSplitTablesForFastHCR(clazz, methodPairs);

		clazz = vmFuncs->allClassesNextDo(&state);
	}
	vmFuncs->allClassesEndDo(&state);

	fixRAMConstantPoolForFastHCR((J9ConstantPool *) vm->jclConstantPool, classPairs, methodPairs, objectClass);
}


void
unresolveAllClasses(J9VMThread * currentThread, J9HashTable * classPairs, J9HashTable * methodPairs, UDATA extensionsUsed)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ClassWalkState state;
	J9Class * clazz;



	clazz = vmFuncs->allClassesStartDo(&state, vm, NULL);

	while (clazz != NULL) {
		U_16 i = 0;

		if (extensionsUsed) {

			/* Zero the constant pool and then run the pre-init instructions.
			 * Do not zero the first entry as it contains VM information, not a CP item. */
			if (clazz->romClass->ramConstantPoolCount != 0) {
				memset(((J9RAMConstantPoolItem *) J9_CP_FROM_CLASS(clazz)) + 1, 0,
					   (clazz->romClass->ramConstantPoolCount - 1) * sizeof(J9RAMConstantPoolItem));
				vmFuncs->internalRunPreInitInstructions(clazz, currentThread);
			}

		} else {

			if (clazz->romClass->ramConstantPoolCount != 0) {
				reresolveHotSwappedConstantPool(J9_CP_FROM_CLASS(clazz), currentThread, classPairs, methodPairs);
			}
		}

		/* unresolve split table entries */
		if (NULL != clazz->staticSplitMethodTable) {
			for (i = 0; i < clazz->romClass->staticSplitMethodRefCount; ++i) {
				clazz->staticSplitMethodTable[i] = (J9Method*)vm->initialMethods.initialStaticMethod;
			}
		}
		if (NULL != clazz->specialSplitMethodTable) {
			for (i = 0; i < clazz->romClass->specialSplitMethodRefCount; ++i) {
				clazz->specialSplitMethodTable[i] = (J9Method*)vm->initialMethods.initialSpecialMethod;
			}
		}

		clazz = vmFuncs->allClassesNextDo(&state);
	}

	vmFuncs->allClassesEndDo(&state);

	/* Fix the JCL Constant Pool entries containing known classes amongst other things */

	reresolveHotSwappedConstantPool((J9ConstantPool *) vm->jclConstantPool, currentThread, classPairs, methodPairs);

}


static jvmtiIterationControl
fixHeapRefsHeapIteratorCallback(J9JavaVM *vm, J9MM_IterateHeapDescriptor *heapDesc, void *userData)
{
	vm->memoryManagerFunctions->j9mm_iterate_spaces(vm, vm->portLibrary, heapDesc, 0, fixHeapRefsSpaceIteratorCallback, userData);
	return JVMTI_ITERATION_CONTINUE;
}

static jvmtiIterationControl
fixHeapRefsSpaceIteratorCallback(J9JavaVM *vm, J9MM_IterateSpaceDescriptor *spaceDesc, void *userData)
{
	vm->memoryManagerFunctions->j9mm_iterate_regions(vm, vm->portLibrary, spaceDesc, 0, fixHeapRefsRegionIteratorCallback, userData);
	return JVMTI_ITERATION_CONTINUE;
}

static jvmtiIterationControl
fixHeapRefsRegionIteratorCallback(J9JavaVM *vm, J9MM_IterateRegionDescriptor *regionDesc, void *userData)
{
	vm->memoryManagerFunctions->j9mm_iterate_region_objects(vm, vm->portLibrary, regionDesc, 0, fixHeapRefsObjectIteratorCallback, userData);
	return JVMTI_ITERATION_CONTINUE;
}

static jvmtiIterationControl
fixHeapRefsObjectIteratorCallback(J9JavaVM *vm, J9MM_IterateObjectDescriptor *objectDesc, void *userData)
{
	J9HashTable* classHashTable = userData;
	j9object_t object = objectDesc->object;
	J9Class *clazz = J9OBJECT_CLAZZ_VM(vm, object);

	if (NULL != classHashTable) {
		J9JVMTIClassPair exemplar;
		J9JVMTIClassPair* result = NULL;

		exemplar.originalRAMClass = clazz;

		result = hashTableFind(classHashTable, &exemplar);
		if (NULL != result) {
			J9Class * replacementRAMClass = result->replacementClass.ramClass;
			if (NULL != replacementRAMClass) {
				J9OBJECT_SET_CLAZZ_VM(vm, objectDesc->object, replacementRAMClass);
			}
		}
	}

	return JVMTI_ITERATION_CONTINUE;
}

void
fixHeapRefs(J9JavaVM * vm, J9HashTable * classHashTable)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* iterate over all objects fixing up their class pointers */
	vm->memoryManagerFunctions->j9mm_iterate_heaps(vm, PORTLIB, 0, fixHeapRefsHeapIteratorCallback, classHashTable);
}

#if defined(J9VM_OPT_METHOD_HANDLE)
void
fixDirectHandles(J9VMThread * currentThread, J9HashTable * classHashTable, J9HashTable * methodHashTable)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9HashTableState hashTableState = {0};
	J9JVMTIClassPair * classPair = NULL;

	if (NULL != methodHashTable) {
		classPair = hashTableStartDo(classHashTable, &hashTableState);
		while (NULL != classPair) {
			/* Get the MethodHandleCache from the j.l.Class of the original RAM class */
			j9object_t classObject = J9VM_J9CLASS_TO_HEAPCLASS(classPair->originalRAMClass);
			j9object_t mhCache = J9VMJAVALANGCLASS_METHODHANDLECACHE_VM(vm, classObject);

			if (NULL != mhCache) {
				/* Walk the linked list of WeakReferenceNodes referencing DirectHandles on this class */
				j9object_t weakRef = J9VMJAVALANGINVOKEMETHODHANDLECACHE_DIRECTHANDLESHEAD_VM(vm, mhCache);
				while (NULL != weakRef) {
					j9object_t mhReferent = vm->memoryManagerFunctions->j9gc_objaccess_referenceGet(currentThread, weakRef);

					if (NULL != mhReferent) {
						/* Found live DirectHandle - check whether vmSlot needs to be updated */
						J9JVMTIMethodPair exemplar = {0};
						J9JVMTIMethodPair * result = NULL;

						exemplar.oldMethod = (J9Method *)(UDATA)J9VMJAVALANGINVOKEPRIMITIVEHANDLE_VMSLOT_VM(vm, mhReferent);

						result = hashTableFind(methodHashTable, &exemplar);
						if (NULL != result) {
							J9Method * newMethod = result->newMethod;
							if (NULL != newMethod) {
								/* Method has been redefined, so vmSlot must be updated */
								J9VMJAVALANGINVOKEPRIMITIVEHANDLE_SET_VMSLOT_VM(vm, mhReferent, (U_64)(UDATA)newMethod);
							}
						}
					}

					weakRef = J9VMCOMIBMOTIUTILWEAKREFERENCENODE_NEXT_VM(vm, weakRef);
				}
			}
			classPair = hashTableNextDo(&hashTableState);
		}
	}
}
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */

void
copyPreservedValues(J9VMThread * currentThread, J9HashTable * classPairs, UDATA extensionsUsed)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		J9Class * originalRAMClass = classPair->originalRAMClass;
		J9Class * replacementRAMClass = classPair->replacementClass.ramClass;

		if (replacementRAMClass != NULL) {
			J9Class * arrayClass;

			/* Copy J9Class fields */

			replacementRAMClass->initializeStatus = originalRAMClass->initializeStatus;
			replacementRAMClass->classObject = originalRAMClass->classObject;
			replacementRAMClass->module = originalRAMClass->module;
			J9VMJAVALANGCLASS_SET_VMREF(currentThread, replacementRAMClass->classObject, replacementRAMClass);


			/* Copy static fields */
			if (extensionsUsed) {
				copyStaticFields(currentThread, originalRAMClass, replacementRAMClass);
			}

			/* Mark the original class as replaced */

			arrayClass = originalRAMClass->arrayClass;
			replacementRAMClass->arrayClass = arrayClass;
			originalRAMClass->arrayClass = replacementRAMClass;
			originalRAMClass->classDepthAndFlags |= J9AccClassHotSwappedOut;
			/* Set the totalInstanceSize in the replaced class to a value so large that it
			 * can never be allocated (but not so large as to overflow the arithmetic when
			 * the header size addition and rounding are done).
			 *
			 * Do not do this for abstract or interface classes, which can not be
			 * instantiated (and hence may reuse the totalInstanceSize field as something else).
			 */
			if (J9ROMCLASS_ALLOCATE_USES_TOTALINSTANCESIZE(originalRAMClass->romClass)) {
				originalRAMClass->totalInstanceSize = (UDATA)-256;
			}
		}
		classPair = hashTableNextDo(&hashTableState);
	}
}


static void
copyStaticFields(J9VMThread * currentThread, J9Class * originalRAMClass, J9Class * replacementRAMClass)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ROMClass * originalROMClass = originalRAMClass->romClass;
	J9ROMFieldShape * currentField;
	J9ROMFieldWalkState state;

	currentField = romFieldsStartDo(originalROMClass, &state);
	while (currentField != NULL) {
		J9UTF8 * fieldName = J9ROMFIELDSHAPE_NAME(currentField);
		J9UTF8 * fieldSignature = J9ROMFIELDSHAPE_SIGNATURE(currentField);
		UDATA * newFieldAddress;

		newFieldAddress = vmFuncs->staticFieldAddress(
			currentThread,
			replacementRAMClass,
			J9UTF8_DATA(fieldName),
			J9UTF8_LENGTH(fieldName),
			J9UTF8_DATA(fieldSignature),
			J9UTF8_LENGTH(fieldSignature),
			NULL,
			NULL,
			J9_LOOK_NO_JAVA,
			NULL);

		if (newFieldAddress != NULL) {
			UDATA * oldFieldAddress;

			oldFieldAddress = vmFuncs->staticFieldAddress(
				currentThread,
				originalRAMClass,
				J9UTF8_DATA(fieldName),
				J9UTF8_LENGTH(fieldName),
				J9UTF8_DATA(fieldSignature),
				J9UTF8_LENGTH(fieldSignature),
				NULL,
				NULL,
				J9_LOOK_NO_JAVA,
				NULL);

			/* printf("copyStaticFields:  [%p:0x%08x] to [%p:0x%08x]\n", oldFieldAddress, oldFieldAddress[0], newFieldAddress, newFieldAddress[0]); */

			if (newFieldAddress != oldFieldAddress) {
				newFieldAddress[0] = oldFieldAddress[0];
				if (currentField->modifiers & J9FieldSizeDouble) {
#ifndef J9VM_ENV_DATA64
					newFieldAddress[1] = oldFieldAddress[1];
#endif
				} else if (currentField->modifiers & J9FieldFlagObject) {
					vm->memoryManagerFunctions->J9WriteBarrierPostClass(currentThread, replacementRAMClass, *(j9object_t*)newFieldAddress);
				}
			}
		}

		currentField = romFieldsNextDo(&state);
	}

}


static void
replaceInAllClassLoaders(J9VMThread * currentThread, J9Class * originalRAMClass, J9Class * replacementRAMClass)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9ClassLoader * classLoader;
	J9ClassLoaderWalkState walkState;

	classLoader = vmFuncs->allClassLoadersStartDo(&walkState, vm, 0);
	while (NULL != classLoader) {

		fixLoadingConstraints(vm, originalRAMClass, replacementRAMClass);
		vmFuncs->hashClassTableReplace(currentThread, classLoader,
			originalRAMClass, replacementRAMClass);

		classLoader = vmFuncs->allClassLoadersNextDo(&walkState);
	}
	vmFuncs->allClassLoadersEndDo(&walkState);
}


static void
fixLoadingConstraints(J9JavaVM * vm, J9Class * oldClass, J9Class * newClass)
{
	if (vm->classLoadingConstraints != NULL) {
		J9HashTableState walkState;
		J9ClassLoadingConstraint* constraint;

		constraint = hashTableStartDo(vm->classLoadingConstraints, &walkState);
		while (constraint != NULL) {
			if (constraint->clazz == oldClass) {
				constraint->clazz = newClass;
			}
			constraint = hashTableNextDo(&walkState);
		}
	}
}


/**
 * \brief	Flush the annotation and reflection caches.
 * \ingroup hcr
 *
 * @param[in] currentThread		current thread
 * @param[in] classPairs		replacement class pair hashtable
 * @return	none
 *
 *	The annotation and reflection caches must be cleared for each class
 *	that has been redefined or retransformed.
 */
void
flushClassLoaderReflectCache(J9VMThread * currentThread, J9HashTable * classPairs)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair = hashTableStartDo(classPairs, &hashTableState);

	while (NULL != classPair) {
		J9Class * replacementRAMClass = classPair->replacementClass.ramClass;

		if (NULL != replacementRAMClass) {
			j9object_t classObject = J9VM_J9CLASS_TO_HEAPCLASS(replacementRAMClass);
			J9VMJAVALANGCLASS_SET_ANNOTATIONCACHE(currentThread, classObject, NULL);
			J9VMJAVALANGCLASS_SET_REFLECTCACHE(currentThread, classObject, NULL);
		}
		classPair = hashTableNextDo(&hashTableState);
	}
}

#if JAVA_SPEC_VERSION >= 11
/**
 * \brief  Fix any resolved nest members
 * \ingroup
 *
 * @param[in] currentThread
 * @param[in] classPairs
 * @return
 *
 *	Within Project Valhalla nestmates, a nest member class will resolve a link
 *	to its nest host class when necessary to verify private access to another
 *	class within its next class. In order for these access checks to be accurate
 *	post nest host resolution, any classes that point to the original class def
 *	as their nest host must be updated to point do the replaced class def instead.
 */
void
fixNestMembers(J9VMThread * currentThread, J9HashTable * classPairs)
{
	J9HashTableState hashTableState = {0};
	J9JVMTIClassPair *classPair = hashTableStartDo(classPairs, &hashTableState);
	J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;

	while (NULL != classPair) {
		J9Class *originalRAMClass = classPair->originalRAMClass;
		J9Class *replacementRAMClass = classPair->replacementClass.ramClass;

		if ((NULL != originalRAMClass) && (NULL != replacementRAMClass)) {
			J9ClassLoader *classLoader = originalRAMClass->classLoader;
			J9SRP *nestMembers = J9ROMCLASS_NESTMEMBERS(originalRAMClass->romClass);
			U_16 nestMemberCount = originalRAMClass->romClass->nestMemberCount;
			U_16 i = 0;

			for (i = 0; i < nestMemberCount; i++) {
				J9UTF8 *nestMemberName = NNSRP_GET(nestMembers[i], J9UTF8*);
				J9Class *nestMember = vmFuncs->hashClassTableAt(classLoader, J9UTF8_DATA(nestMemberName), J9UTF8_LENGTH(nestMemberName));
				if (NULL != nestMember) {
					if (nestMember->nestHost == originalRAMClass) {
						nestMember->nestHost = replacementRAMClass;
					}
				}
			}
		}
		classPair = hashTableNextDo(&hashTableState);
	}
}
#endif /* JAVA_SPEC_VERSION >= 11 */

static void
swapClassesForFastHCR(J9Class *originalClass, J9Class *obsoleteClass)
{
	/* Helper macro to swap fields between the two J9Class structs. */
	#define SWAP_MEMBER(fieldName, fieldType, class1, class2) \
		do { \
			fieldType temp = (fieldType) (class1)->fieldName; \
			(class1)->fieldName = (class2)->fieldName; \
			(class2)->fieldName = (void *) temp; \
		} while (0)

	/* Preserved values. */
	obsoleteClass->initializeStatus = originalClass->initializeStatus;

	obsoleteClass->classObject = originalClass->classObject;
	obsoleteClass->module = originalClass->module;

	/* Mark the newClass as replaced by the original class. */
	obsoleteClass->arrayClass = originalClass;
	obsoleteClass->classDepthAndFlags |= J9AccClassHotSwappedOut;

	/* Swap fields that must be swapped. */
	SWAP_MEMBER(initializerCache, J9Method *, originalClass, obsoleteClass);
	SWAP_MEMBER(jniIDs, void **, originalClass, obsoleteClass);
	SWAP_MEMBER(romClass, J9ROMClass *, originalClass, obsoleteClass);
	SWAP_MEMBER(ramMethods, J9Method *, originalClass, obsoleteClass);
	SWAP_MEMBER(ramConstantPool, J9ConstantPool *, originalClass, obsoleteClass);
	((J9ConstantPool *) originalClass->ramConstantPool)->ramClass = originalClass;
	((J9ConstantPool *) obsoleteClass->ramConstantPool)->ramClass = obsoleteClass;
	SWAP_MEMBER(replacedClass, J9Class *, originalClass, obsoleteClass);
	SWAP_MEMBER(staticSplitMethodTable, J9Method **, originalClass, obsoleteClass);
	SWAP_MEMBER(specialSplitMethodTable, J9Method **, originalClass, obsoleteClass);
	/* Force invokedynamics to be re-resolved as we can't map from the old callsite index to the new one */
	SWAP_MEMBER(callSites, j9object_t*, originalClass, obsoleteClass);
	/* Force varhanles to be re-resolved as we can't map from the old varhandle MTs index to the new one */
	SWAP_MEMBER(varHandleMethodTypes, j9object_t*, originalClass, obsoleteClass);
	/* Force methodTypes to be re-resolved as indexes are assigned in CP order, no mapping from old to new. */
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	SWAP_MEMBER(invokeCache, j9object_t*, originalClass, obsoleteClass);
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	SWAP_MEMBER(methodTypes, j9object_t*, originalClass, obsoleteClass);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

	J9CLASS_EXTENDED_FLAGS_SET(obsoleteClass, J9ClassReusedStatics);
	Assert_hshelp_true(0 == (J9CLASS_EXTENDED_FLAGS(originalClass) & J9ClassReusedStatics));

	#undef SWAP_MEMBER
}


jvmtiError
recreateRAMClasses(J9VMThread * currentThread, J9HashTable * classHashTable, J9HashTable * methodHashTable, UDATA extensionsUsed, BOOLEAN fastHCR)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	UDATA i;
	UDATA classCount;
	J9JVMTIClassPair ** classPairs;
	J9JVMTIClassPair * classPairIter;
	J9HashTableState hashTableState;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* copy the pairs in the hashTable into an array so that they may be sorted */
	classCount = hashTableGetCount(classHashTable);
	classPairs = j9mem_allocate_memory(classCount * sizeof(*classPairs), OMRMEM_CATEGORY_VM);
	if (classPairs == NULL) {
		return JVMTI_ERROR_OUT_OF_MEMORY;
	}

	classPairIter = hashTableStartDo(classHashTable, &hashTableState);
	for (i = 0; i < classCount; ++i) {
		classPairs[i] = classPairIter;
		classPairIter = hashTableNextDo(&hashTableState);
	}

	/* sort the array so that superclasses appear before subclasses */
	J9_SORT(classPairs, (UDATA) classCount, sizeof(J9JVMTIClassPair*), compareClassDepth);

	for (i = 0; i < classCount; ++i) {
		J9Class * originalRAMClass = classPairs[i]->originalRAMClass;
		J9ROMClass * replacementROMClass = classPairs[i]->replacementClass.romClass;
		J9Class * replacementRAMClass;
		J9ClassLoader * classLoader = originalRAMClass->classLoader;
		J9UTF8 * className = J9ROMCLASS_CLASSNAME(originalRAMClass->romClass);
		j9object_t heapClass = J9VM_J9CLASS_TO_HEAPCLASS(originalRAMClass);
		j9object_t protectionDomain = J9VMJAVALANGCLASS_PROTECTIONDOMAIN(currentThread, heapClass);
		UDATA options;
		J9Class *hostClass = NULL;

		/* We iterate over replaced classes and their subclasses (that might not have been explicitly
		 * replaced).  In the case the redefine extensions have been used, always recreate the
		 * replaced class and all of its subclasses. In the case extensions have NOT been used,
		 * recreate only the redefined classes and ignore the subclasses */

		if (!(classPairs[i]->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED)) {
			classPairs[i]->replacementClass.ramClass = NULL;
			continue;
		}

		/* Delete original class from defining loader's class table */
		if (!fastHCR) {
			vmFuncs->hashClassTableDelete(classLoader, J9UTF8_DATA(className), J9UTF8_LENGTH(className));
		}

		/* Create new RAM class */
		options = J9_FINDCLASS_FLAG_NO_DEBUG_EVENTS|J9_FINDCLASS_FLAG_NO_SUBCLASS_LINK;
		if (fastHCR) {
			options |= J9_FINDCLASS_FLAG_FAST_HCR;
		}
		if (J9ROMCLASS_IS_HIDDEN(originalRAMClass->romClass)) {
			options |= (J9_FINDCLASS_FLAG_HIDDEN | J9_FINDCLASS_FLAG_UNSAFE);
			if (J9ROMCLASS_IS_OPTIONNESTMATE_SET(originalRAMClass->romClass)) {
				options |= J9_FINDCLASS_FLAG_CLASS_OPTION_NESTMATE;
			}
			if (J9ROMCLASS_IS_OPTIONSTRONG_SET(originalRAMClass->romClass)) {
				options |= J9_FINDCLASS_FLAG_CLASS_OPTION_STRONG ;
			} else {
				options |= J9_FINDCLASS_FLAG_ANON;
			}
		} else if (J9_ARE_ALL_BITS_SET(originalRAMClass->classFlags, J9ClassIsAnonymous)) {
			options |= J9_FINDCLASS_FLAG_ANON;
		}

		/* originalRAMClass != originalRAMClass->hostClass : this is an anon class created using Unsafe.defineAnonymousClass.
		 * For anon classes created using Unsafe.defineAnonymousClass, we need to preserve the host class.
		 * For all other cases, clazz->hostClass can point to itself (clazz).
		 */
		if (originalRAMClass != originalRAMClass->hostClass) {
			hostClass = originalRAMClass->hostClass;
		}

		replacementRAMClass = vmFuncs->internalCreateRAMClassFromROMClass(
			currentThread,
			classLoader,
			replacementROMClass,
			options,
			NULL,
			protectionDomain,
			classPairs[i]->methodRemap,
			J9_CP_INDEX_NONE,
			LOAD_LOCATION_UNKNOWN,
			originalRAMClass,
			hostClass);

		/* If class creation fails, put all the original RAM classes back in their tables */

		if (NULL == replacementRAMClass) {
outOfMemory:
			if (!fastHCR) {
				vmFuncs->hashClassTableAtPut(currentThread, classLoader, J9UTF8_DATA(className), J9UTF8_LENGTH(className), originalRAMClass);
				while (i != 0) {
					replaceInAllClassLoaders(currentThread, classPairs[i]->replacementClass.ramClass, classPairs[i]->originalRAMClass);
					--i;
				}
			}
			j9mem_free_memory(classPairs);
			return JVMTI_ERROR_OUT_OF_MEMORY;
		}

		if ((FALSE == fastHCR) && (classLoader == vm->systemClassLoader)) {
			/* In non-fastHCR case when class is being redefined, the classLocation of original class
			 * should now point to redefined class.
			 * Get the classLocation for the original class, remove it from the hashtable
			 * and a new classLocation with redefined class as the key.
			 */
			J9ClassLocation *classLocation = NULL;

			omrthread_monitor_enter(vm->classLoaderModuleAndLocationMutex);
			classLocation = vmFuncs->findClassLocationForClass(currentThread, originalRAMClass);

			if (NULL != classLocation) {
				J9ClassLocation newLocation = {0};

				memcpy((void *)&newLocation, classLocation, sizeof(J9ClassLocation));
				newLocation.clazz = replacementRAMClass;

				hashTableRemove(classLoader->classLocationHashTable, classLocation);

				hashTableAdd(classLoader->classLocationHashTable, (void *)&newLocation);
			}
			omrthread_monitor_exit(vm->classLoaderModuleAndLocationMutex);
		}

		/* Replace ROM class with RAM class in the pair */
		classPairs[i]->replacementClass.ramClass = replacementRAMClass;

		if (fastHCR) {
			J9Class *temp = replacementRAMClass;
			swapClassesForFastHCR(originalRAMClass, replacementRAMClass);
			replacementRAMClass = originalRAMClass;
			originalRAMClass = temp;
		}

		/* Make the new class version keep track of its direct predecessor */
		replacementRAMClass->replacedClass = originalRAMClass;

		/* Preserve externally set class flag (by the jit) */
		if (J9CLASS_FLAGS(originalRAMClass) & J9AccClassHasBeenOverridden) {
			replacementRAMClass->classDepthAndFlags |= J9AccClassHasBeenOverridden;
		}

		/* Replace the class in all classloaders */
		if (!fastHCR) {
			replaceInAllClassLoaders(currentThread, originalRAMClass, replacementRAMClass);
		}

		Trc_hshelp_recreateRAMClasses_classReplace(currentThread, originalRAMClass, replacementRAMClass);

		/* If we are dealing with one of the redefined classes, save the old to new method mappings
		 * and lazy init the redefined classes hashtable */

		if (!extensionsUsed && (classPairs[i]->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED)) {
			UDATA methodCount, m;
			J9JVMTIMethodPair methodPair;

			/* Walk over all methods for this old class */

			methodCount = originalRAMClass->romClass->romMethodCount;

			/* Add method re-mappings to the hashtable */

			for (m = 0; m < methodCount; m++) {

				/* Check if we have detected any changes between original and replacement method reordering
				 * and apply the correct mapping */

				if (classPairs[i]->methodRemap == NULL) {
					methodPair.oldMethod = originalRAMClass->ramMethods + m;
					methodPair.newMethod = replacementRAMClass->ramMethods + m;
				} else {
					methodPair.oldMethod = originalRAMClass->ramMethods + m;
					methodPair.newMethod = replacementRAMClass->ramMethods + classPairs[i]->methodRemapIndices[m];
				}

				Trc_hshelp_recreateRAMClasses_addMethRemap(currentThread,
					   classPairs[i]->originalRAMClass, methodPair.oldMethod, methodPair.newMethod,
					   getMethodName(methodPair.oldMethod));

				if (NULL == hashTableAdd(methodHashTable, &methodPair)) {
					++i;
					goto outOfMemory;
				}
			}

			/* Preallocate a hash table of class redefinitions kept track of by each redefined classes
			 * classloader */

			if (NULL == replacementRAMClass->classLoader->redefinedClasses) {

				/* Lazy initialize this classLoaders redefined classes hashtable */
				replacementRAMClass->classLoader->redefinedClasses =
					hashTableNew(OMRPORT_FROM_J9PORT(PORTLIB),
								 "JVMTIRedefinedClassesMap",
								 1,
								 sizeof(J9JVMTIMethodPair),
								 sizeof(J9Method *),
								 0,
								 OMRMEM_CATEGORY_VM,
								 redefinedClassPairHash,
								 redefinedClassPairEquals,
								 NULL,
								 NULL);

				if (NULL == replacementRAMClass->classLoader->redefinedClasses) {
					++i;
					goto outOfMemory;
				}
			}
		}


#ifdef  WIP_D1452
		// TODO: check whether we really need to prune out redefined classes that are a subclass of another redefined class
		/* Prune only the classes that have been redefined AND have not already been pruned */
		/* TODO: revisit, pruning might not be necessary in the end.  */
		if (classPairs[i]->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED &&
			classPairs[i]->flags & ~J9JVMTI_CLASS_PAIR_FLAG_PRUNED) {

			/* For a redefined class (classPairs[i]) check if it is a subclass of another redefined class
			 * and mark it as pruned */
			for (j = 0; j < classCount; ++j) {
				if (!(classPairs[j]->flags & J9JVMTI_CLASS_PAIR_FLAG_PRUNED)) {
					if (isSameOrSuperClassOf(classPairs[i]->originalRAMClass, classPairs[j]->originalRAMClass)) {
						classPairs[i]->flags |= J9JVMTI_CLASS_PAIR_FLAG_PRUNED;
					}
				}
			}
		}
#endif

	}



	j9mem_free_memory(classPairs);

	return JVMTI_ERROR_NONE;
}


static UDATA
classPairHash(void* entry, void* userData)
{
	J9JVMTIClassPair* pair = entry;
	return (UDATA)pair->originalRAMClass / sizeof(J9Class*);
}

static UDATA
classPairEquals(void* left, void* right, void* userData)
{
	J9JVMTIClassPair* leftPair = left;
	J9JVMTIClassPair* rightPair = right;

	return leftPair->originalRAMClass == rightPair->originalRAMClass;
}

static jvmtiError
addClassesRequiringNewITables(J9JavaVM *vm, J9HashTable *classHashTable, UDATA *addedMethodCountPtr, UDATA *addedClassCountPtr, BOOLEAN fastHCR)
{
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	J9Class *clazz;
	J9ClassWalkState classWalkState;
	UDATA addedMethodCount = 0;
	UDATA addedClassCount = 0;

	*addedMethodCountPtr = 0;
	*addedClassCountPtr = 0;
	clazz = vmFuncs->allClassesStartDo(&classWalkState, vm, NULL);
	while (NULL != clazz) {

		if (!J9_IS_CLASS_OBSOLETE(clazz)) {
			J9ITable *iTable = (J9ITable *)clazz->iTable;

			while (NULL != iTable) {
				J9JVMTIClassPair exemplar;
				J9JVMTIClassPair *result;

				exemplar.originalRAMClass = iTable->interfaceClass;
				result = hashTableFind(classHashTable, &exemplar);

				if (NULL != result) {
					UDATA flags = 0;
					U_32 nodeCount = hashTableGetCount(classHashTable);

					if (!fastHCR) {
						BOOLEAN extensionsUsed = (0 != (result->flags & J9JVMTI_CLASS_PAIR_FLAG_USES_EXTENSIONS));
						/* If methods were added or removed (extensionsUsed) or re-ordered in the interface, the class should be redefined. */
						if (extensionsUsed || (NULL != result->methodRemap)) {
							/* An interface of this class is being updated with extensions - this class will require new iTables. */
							flags = J9JVMTI_CLASS_PAIR_FLAG_REDEFINED;
						}
					}

					memset(&exemplar, 0, sizeof(J9JVMTIClassPair));
					exemplar.originalRAMClass = clazz;
					exemplar.replacementClass.romClass = clazz->romClass;
					exemplar.flags = flags;

					if (NULL == hashTableAdd(classHashTable, &exemplar)) {
						vmFuncs->allClassesEndDo(&classWalkState);
						return JVMTI_ERROR_OUT_OF_MEMORY;
					}

					/* Increment counts if the class was really added (i.e. did not exist before). */
					if (hashTableGetCount(classHashTable) == (nodeCount + 1)) {
						J9SubclassWalkState subclassState;
						J9Class * subclass;

						addedMethodCount += clazz->romClass->romMethodCount;
						addedClassCount += 1;

						/* Now add all of the subclasses of the affected class to the table */
						subclass = allSubclassesStartDo(clazz, &subclassState, TRUE);
						while (subclass != NULL) {
							U_32 nodeCount = hashTableGetCount(classHashTable);

							memset(&exemplar, 0x00, sizeof(J9JVMTIClassPair));
							exemplar.originalRAMClass = subclass;
							exemplar.replacementClass.romClass = subclass->romClass;
							exemplar.flags = flags;

							/* If this class is already in the table this add will have no effect */
							result = hashTableAdd(classHashTable, &exemplar);
							if (NULL == result) {
								vmFuncs->allClassesEndDo(&classWalkState);
								return JVMTI_ERROR_OUT_OF_MEMORY;
							}
							/* Ensure that classes already in the table get redefined */
							result->flags |= flags;

							/* Increment counts if the class was really added (i.e. did not exist before). */
							if (hashTableGetCount(classHashTable) == (nodeCount + 1)) {
								addedMethodCount += subclass->romClass->romMethodCount;
								addedClassCount += 1;
							}

							subclass = allSubclassesNextDo(&subclassState);
						}
					}
					break;
				}
				iTable = iTable->next;
			}
		}

		clazz = vmFuncs->allClassesNextDo(&classWalkState);
	}
	vmFuncs->allClassesEndDo(&classWalkState);
	*addedMethodCountPtr = addedMethodCount;
	*addedClassCountPtr = addedClassCount;

	return JVMTI_ERROR_NONE;
}

jvmtiError
determineClassesToRecreate(J9VMThread * currentThread, jint classCount,
						   J9JVMTIClassPair * specifiedClasses, J9HashTable ** classPairsPtr,
						   J9HashTable ** methodPairs, J9JVMTIHCRJitEventData * jitEventData, BOOLEAN fastHCR)
{
	PORT_ACCESS_FROM_VMC(currentThread);
	UDATA redefinedMethodCount = 0;
	jvmtiError jvmtiResult;
	jint i;
	jint redefinedSubclassCount = 0;
	UDATA addedClassCount = 0;
	UDATA addedMethodCount = 0;

	J9HashTable* classHashTable = hashTableNew(
		OMRPORT_FROM_J9PORT(PORTLIB),
		"JVMTIClassPairs",
		classCount * 2,
		sizeof(J9JVMTIClassPair),
		sizeof(J9Class*),
		0,
		OMRMEM_CATEGORY_VM,
		classPairHash,
		classPairEquals,
		NULL,
		NULL);

	*classPairsPtr = NULL;

	if (classHashTable == NULL) {
		return JVMTI_ERROR_OUT_OF_MEMORY;
	}

	/* Add all of the specified classes to the table first. Additional attempts to add the
	 * same class will have no effect.
	 */
	for (i = 0; i < classCount; ++i) {
		specifiedClasses[i].flags |= J9JVMTI_CLASS_PAIR_FLAG_REDEFINED;
		redefinedMethodCount += specifiedClasses[i].originalRAMClass->romClass->romMethodCount;
		if (NULL == hashTableAdd(classHashTable, &specifiedClasses[i])) {
			hashTableFree(classHashTable);
			return JVMTI_ERROR_OUT_OF_MEMORY;
		}
	}

	/* Now add all of the subclasses of the classes being redefined to the table */
	for (i = 0; i < classCount; ++i) {
		J9Class * originalRAMClass = specifiedClasses[i].originalRAMClass;
		J9SubclassWalkState subclassState;
		J9Class * subclass;
		BOOLEAN mustRedefineSubclasses = FALSE;

		if (!fastHCR) {
			J9JVMTIClassPair exemplar;
			J9JVMTIClassPair *result;

			/* If methods were added, removed or re-ordered in the superclass, the subclasses must be redefined */
			exemplar.originalRAMClass = originalRAMClass;
			result = hashTableFind(classHashTable, &exemplar);
			/* assert null != result */
			if ((0 != (result->flags & J9JVMTI_CLASS_PAIR_FLAG_USES_EXTENSIONS)) || (NULL != result->methodRemap)) {
				mustRedefineSubclasses = TRUE;
			}
		}

		subclass = allSubclassesStartDo(originalRAMClass, &subclassState, TRUE);
		while (subclass != NULL) {
			J9JVMTIClassPair exemplar;
			U_32 nodeCount = hashTableGetCount(classHashTable);

			memset(&exemplar, 0x00, sizeof(J9JVMTIClassPair));
			exemplar.originalRAMClass = subclass;
			exemplar.replacementClass.romClass = subclass->romClass;
			if (mustRedefineSubclasses) {
				exemplar.flags = J9JVMTI_CLASS_PAIR_FLAG_REDEFINED;
			}

			/* If this class is already in the table this add will have no effect */
			if (NULL == hashTableAdd(classHashTable, &exemplar)) {
				hashTableFree(classHashTable);
				return JVMTI_ERROR_OUT_OF_MEMORY;
			}

			if (mustRedefineSubclasses) {
				/* Increment counts if the class was really added (i.e. did not exist before). */
				if (hashTableGetCount(classHashTable) == (nodeCount + 1)) {
					redefinedMethodCount += subclass->romClass->romMethodCount;
					redefinedSubclassCount += 1;
				}
			}

			subclass = allSubclassesNextDo(&subclassState);
		}
	}
	classCount += redefinedSubclassCount;

	/* Add any classes implementing interfaces that are being updated with extensions or re-ordered methods. */
	jvmtiResult = addClassesRequiringNewITables(currentThread->javaVM, classHashTable, &addedClassCount, &addedMethodCount, fastHCR);
	if (JVMTI_ERROR_NONE != jvmtiResult) {
		hashTableFree(classHashTable);
		return jvmtiResult;
	}

	redefinedMethodCount += addedMethodCount;
	classCount += (jint) addedClassCount;

#ifdef J9VM_INTERP_NATIVE_SUPPORT
	/* Pre-allocate memory to be used for the JIT event callback data */
	if (NULL != jitEventData) {
		jvmtiResult = jitEventInitialize(currentThread, classCount, redefinedMethodCount, jitEventData);
		if (JVMTI_ERROR_NONE != jvmtiResult) {
			return jvmtiResult;
		}
	}
#endif

	/* Pre-allocate the method remap hash table. */
	jvmtiResult = preallocMethodHashTable(currentThread, redefinedMethodCount, methodPairs);
	if (JVMTI_ERROR_NONE != jvmtiResult) {
		return jvmtiResult;
	}

	*classPairsPtr = classHashTable;

	return JVMTI_ERROR_NONE;
}

static jvmtiError
verifyFieldsAreSame(J9VMThread * currentThread, UDATA fieldType, J9ROMClass * originalROMClass, J9ROMClass * replacementROMClass,
					UDATA extensionsEnabled, UDATA * extensionsUsed)
{
    jvmtiError rc = JVMTI_ERROR_NONE;
	UDATA originalFieldCount;
	UDATA replacementFieldCount;
	J9ROMFieldWalkState originalState;
	J9ROMFieldShape * originalField;
	J9ROMFieldShape * replacementField;
	J9ROMFieldWalkState replacementState;

	/* Verify that static or instance fields are the same */

	originalFieldCount = 0;
	originalField = romFieldsStartDo(originalROMClass, &originalState);
	while (originalField != NULL) {
		if ((originalField->modifiers & J9AccStatic) == fieldType) {
			++originalFieldCount;
		}
		originalField = romFieldsNextDo(&originalState);
	}
	replacementFieldCount = 0;
	replacementField = romFieldsStartDo(replacementROMClass, &replacementState);
	while (replacementField != NULL) {
		if ((replacementField->modifiers & J9AccStatic) == fieldType) {
			++replacementFieldCount;
		}
		replacementField = romFieldsNextDo(&replacementState);
	}

	if (originalFieldCount == replacementFieldCount) {
		originalField = romFieldsStartDo(originalROMClass, &originalState);
		replacementField = romFieldsStartDo(replacementROMClass, &replacementState);
		while (originalField != NULL) {
			if ((originalField->modifiers & J9AccStatic) == fieldType) {
				while ((replacementField->modifiers & J9AccStatic) != fieldType) {
					replacementField = romFieldsNextDo(&replacementState);
				}
				if (!NAME_AND_SIG_IDENTICAL(originalField, replacementField, J9ROMFIELDSHAPE_NAME, J9ROMFIELDSHAPE_SIGNATURE) ||
					(originalField->modifiers & CFR_FIELD_ACCESS_MASK) != (replacementField->modifiers & CFR_FIELD_ACCESS_MASK)
				) {
					rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
					goto done;
				}
				replacementField = romFieldsNextDo(&replacementState);
			}
			originalField = romFieldsNextDo(&originalState);
		}
	} else {
		rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
		goto done;
	}

done:

	/* change in instance field shape is considered an error with extensions on or off
	 *
	 * change in static field shape is
	 *    - an error if extensions are disabled
	 *    - not an error if extensions are enabled
	 *
	 * if we are looking at static fields with extensions enabled, then any change in the
	 * field shape (rc != JVMTI_ERROR_NONE)  is treated as a hint that extensions are used */

    if ((fieldType == J9AccStatic) && (rc != JVMTI_ERROR_NONE)) {
		if (extensionsEnabled) {
			*extensionsUsed = 1;
			rc = JVMTI_ERROR_NONE;
		}
	}

	return rc;
}


static J9Class *
getOldestClassVersion(J9Class * clazz)
{
	J9HotswappedClassPair * result = NULL;

	if (clazz->classLoader->redefinedClasses) {
		J9HotswappedClassPair search;

		search.replacementClass = clazz;
		result = hashTableFind(clazz->classLoader->redefinedClasses, &search);
		if (result) {
			return result->originalClass;
		}
	}

	return NULL;
}



/**
 * \brief	Verify that the methods follow allowed schema change rules
 * \ingroup
 *
 * @param[in] currentThread
 * @param[in] classPair			old and replacing class
 * @param[in] extensionsEnabled boolean indicating if the extensions are enabled
 * @return error code
 *
 * If extensions are enabled then none of the schema verification errors matter as they are all allowed.
 * In that particular case we run this code to see if the extensions were actually used. The method
 * remap array is constructed and kept if extensions are NOT used. It is discarded if they are used.
 *
 * In non-extended mode, we must return an error if method modifiers have been changed or methods have been
 * added or removed. The method remap array is always needed in the non-extended mode.
 *
 * This function is "overloaded" to calculate the method remap array to avoid doing the same method walk again.
 * The method remap array is used during ram class creation to align vtable entries in the correct slot vis a vi slot
 * order. Compilers are not guaranteed to create classes containing methods in the same order and the rom class
 * loading code does not care.
 *
 */
static jvmtiError
verifyMethodsAreSame(J9VMThread * currentThread, J9JVMTIClassPair * classPair, UDATA extensionsEnabled, UDATA * extensionsUsed)
{
	jvmtiError rc = JVMTI_ERROR_NONE;
	J9Class * oldestRAMClass;
	J9ROMClass * originalROMClass;
	J9ROMClass * replacementROMClass = classPair->replacementClass.romClass;

	oldestRAMClass = getOldestClassVersion(classPair->originalRAMClass);
	if (oldestRAMClass == NULL) {
		oldestRAMClass = classPair->originalRAMClass;
	}

	originalROMClass = oldestRAMClass->romClass;


	/* Verify that the methods are the same */

	if (originalROMClass->romMethodCount == replacementROMClass->romMethodCount) {
		PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);
		J9ROMMethod * originalROMMethod = J9ROMCLASS_ROMMETHODS(originalROMClass);
		BOOLEAN identityRemap;
		U_32 j;

		if (originalROMClass->romMethodCount) {
			/* Allocate the method remapping array. Keeps track of which method slot in the replacement class
			 * maps to which slot in the original class. The relationship is not necessarily the same as it depends on the
			 * compiler */
			classPair->methodRemap = j9mem_allocate_memory(originalROMClass->romMethodCount * sizeof(J9ROMMethod *), OMRMEM_CATEGORY_VM);
			if (NULL == classPair->methodRemap) {
				rc = JVMTI_ERROR_OUT_OF_MEMORY;
				goto done;
			}
			classPair->methodRemapIndices = j9mem_allocate_memory(originalROMClass->romMethodCount * sizeof(U_32), OMRMEM_CATEGORY_VM);
			if (NULL == classPair->methodRemapIndices) {
				rc = JVMTI_ERROR_OUT_OF_MEMORY;
				goto done;
			}
		}

		for (j = 0; j < originalROMClass->romMethodCount; ++j) {
			U_32 k;
			J9ROMMethod * replacementROMMethod = J9ROMCLASS_ROMMETHODS(replacementROMClass);

			for (k = 0; k < replacementROMClass->romMethodCount; ++k) {
				if (J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(originalROMMethod, replacementROMMethod)) {
					/* Populate the method remap array using ROM methods. The mapping happens between the current
					 * and _oldest_ class version */
					classPair->methodRemap[j] = replacementROMMethod;
					break;
				}
				replacementROMMethod = nextROMMethod(replacementROMMethod);
			}

			if (k == replacementROMClass->romMethodCount) {
				rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED;
				if (extensionsEnabled == 0) {
					/* In non-extended mode, this is really an error and we should bail out */
					goto done;
				}
			}

			if ((originalROMMethod->modifiers & CFR_METHOD_ACCESS_MASK) != (replacementROMMethod->modifiers & CFR_METHOD_ACCESS_MASK)) {
				rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED;
				if (extensionsEnabled == 0) {
					/* In non-extended mode, this is really an error and we should bail out */
					goto done;
				}
			}

			originalROMMethod = nextROMMethod(originalROMMethod);
		}


		/* Create the method remap array based on the method index. We must do it again as the comparison should
		 * happen between the current and new class version */

		originalROMMethod = J9ROMCLASS_ROMMETHODS(classPair->originalRAMClass->romClass);

		identityRemap = TRUE;
		for (j = 0; j < originalROMClass->romMethodCount; ++j) {
			U_32 k;
			J9ROMMethod * replacementROMMethod = J9ROMCLASS_ROMMETHODS(replacementROMClass);

			for (k = 0; k < replacementROMClass->romMethodCount; ++k) {
				if (J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(originalROMMethod, replacementROMMethod)) {
					classPair->methodRemapIndices[j] = k;
					if (j != k) {
						identityRemap = FALSE;
					}
					break;
				}
				replacementROMMethod = nextROMMethod(replacementROMMethod);
			}

			originalROMMethod = nextROMMethod(originalROMMethod);
		}

		/*
		 * Free the remap arrays if it is an identity mapping. If J9JVMTI_CLASS_PAIR_FLAG_USES_EXTENSIONS is not
		 * set and methodRemap is NULL, then we know that the methods list is the same (and in the same order).
		 * This is used to determine whether classes implementing a redefined interface will need to be re-created.
		 */
		if (identityRemap) {
			j9mem_free_memory(classPair->methodRemap);
			j9mem_free_memory(classPair->methodRemapIndices);
			classPair->methodRemap = NULL;
			classPair->methodRemapIndices = NULL;
		}

	} else {
		if (originalROMClass->romMethodCount < replacementROMClass->romMethodCount) {
			rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
		} else {
			rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED;
		}
	}


done:

	/* Free the remap arrays if:
	 *    - we had an allocation problem, redefine cannot continue in either case
	 *    - extensions are enabled AND used, in that case the remap array is of no use since we will rebuild
	 *      the class and its subclasses and automatically get the new vtables done for us.
	 */

	if ((extensionsEnabled && (rc != JVMTI_ERROR_NONE)) || (rc == JVMTI_ERROR_OUT_OF_MEMORY)) {

		PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);

		j9mem_free_memory(classPair->methodRemap);
		j9mem_free_memory(classPair->methodRemapIndices);

		classPair->methodRemap = NULL;
		classPair->methodRemapIndices = NULL;
	}

    /* Out of memory is always fatal, no matter which mode we are in */

    if (rc == JVMTI_ERROR_OUT_OF_MEMORY) {
		return JVMTI_ERROR_OUT_OF_MEMORY;
	}

	/* If extensions are not enabled, treat any schema change as a real error */

    if (extensionsEnabled == 0) {
		return rc;
	}

    /* If extensions are enabled, any error aside of OOM (returned with earlier) is a hint that extensions have been used
	 * and should not be treated as an error */
    if (rc != JVMTI_ERROR_NONE) {
		*extensionsUsed = 1;
	}

	return JVMTI_ERROR_NONE;

}

#if JAVA_SPEC_VERSION >= 15
static jvmtiError
verifyRecordAttributesAreSame(J9ROMClass *originalROMClass, J9ROMClass *replacementROMClass)
{
	jvmtiError rc = JVMTI_ERROR_NONE;

	/* Since retranformation is not allowed to change inheritance there's no need to consider 
	 * one class being a record and one not. */
	if (J9ROMCLASS_IS_RECORD(originalROMClass) && J9ROMCLASS_IS_RECORD(replacementROMClass)) {
		U_32 originalNumberOfRecords = getNumberOfRecordComponents(originalROMClass);
		U_32 replacementNumberOfRecords = getNumberOfRecordComponents(replacementROMClass);

		if (originalNumberOfRecords == replacementNumberOfRecords) {
			if (originalNumberOfRecords > 0) {
				J9ROMRecordComponentShape* originalRecordComponent = NULL;
				J9ROMRecordComponentShape* replacementRecordComponent = NULL;
				U_32 i = 0;

				/* Compare record components in order. For two records to be the same their record components
				 * must be in the same order. According to the spec: "Each component of the record must have 
				 * exactly one corresponding entry in the 
				 * components array, in the order in which the components are declared."
				 */
				originalRecordComponent = recordComponentStartDo(originalROMClass);
				replacementRecordComponent = recordComponentStartDo(replacementROMClass);
				for (; i < originalNumberOfRecords; i++) {
					/* verify name and signature */
					if (!NAME_AND_SIG_IDENTICAL(originalRecordComponent, replacementRecordComponent, 
						J9ROMRECORDCOMPONENTSHAPE_NAME, J9ROMRECORDCOMPONENTSHAPE_SIGNATURE)
					) {
						rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
						goto done;
					}
					originalRecordComponent = recordComponentNextDo(originalRecordComponent);
					replacementRecordComponent = recordComponentNextDo(replacementRecordComponent);
				}
			}
		} else {
			rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
		}
	}

done:
	return rc;
}

static jvmtiError
verifyPermittedSubclassAttributeContentsMatch(J9ROMClass *originalROMClass, J9ROMClass *replacementROMClass)
{
	jvmtiError rc = JVMTI_ERROR_NONE;

	if (J9ROMCLASS_IS_SEALED(originalROMClass) && J9ROMCLASS_IS_SEALED(replacementROMClass)) {
		U_32* originalPermittedSubclassesCountPtr = getNumberOfPermittedSubclassesPtr(originalROMClass);
		U_32* replacementPermittedSubclassesCountPtr = getNumberOfPermittedSubclassesPtr(replacementROMClass);

		if (*originalPermittedSubclassesCountPtr == *replacementPermittedSubclassesCountPtr) {
			U_32 i = 0;

			for (i = 0; i < *originalPermittedSubclassesCountPtr; i++) {
				U_32 j = 0;
				BOOLEAN foundMatchingSubclass = FALSE;
				J9UTF8* originalSubclassNameUTF = permittedSubclassesNameAtIndex(originalPermittedSubclassesCountPtr, i);
				
				/* Find matching subclass name in replacement ROM class. The permitted subclasses are not required to be in the same order
				 * as the original class. Assume the replacement subclass is in the same slot as original to try to improve speed.
				 */
				for (j = 0; j < *originalPermittedSubclassesCountPtr; j++){
					U_32 adjustedIndex = (i + j) % *originalPermittedSubclassesCountPtr;
					J9UTF8* replacementSubclassNameUTF = permittedSubclassesNameAtIndex(replacementPermittedSubclassesCountPtr, adjustedIndex);

					if (J9UTF8_EQUALS(originalSubclassNameUTF, replacementSubclassNameUTF)) {
						foundMatchingSubclass = TRUE;
						break;
					}
				}

				/* check if matching subclass was found. */
				if (!foundMatchingSubclass) {
					rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
					break;
				}
			}

		} else {
			rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
		}
	} else if (J9ROMCLASS_IS_SEALED(originalROMClass) != J9ROMCLASS_IS_SEALED(replacementROMClass)) {
		/* If sealed status has changed the PermittedSubclass attribute has also changed which is not allowed. */
		rc = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
	}

	return rc;
}

#endif /* JAVA_SPEC_VERSION >= 15 */

jvmtiError
verifyClassesAreCompatible(J9VMThread * currentThread, jint class_count, J9JVMTIClassPair * classPairs,
						   UDATA extensionsEnabled, UDATA * extensionsUsed)
{
	jint i;

	for (i = 0; i < class_count; ++i) {
		UDATA classUsesExtensions = 0;
		J9ROMClass * originalROMClass = classPairs[i].originalRAMClass->romClass;
		J9ROMClass * replacementROMClass = classPairs[i].replacementClass.romClass;
		jvmtiError rc;
		J9UTF8 *originalSuperclassName = J9ROMCLASS_SUPERCLASSNAME(originalROMClass);
		J9UTF8 *replacementSuperclassName = J9ROMCLASS_SUPERCLASSNAME(replacementROMClass);

		/* Verify that the class names match */

		if (!utfsAreIdentical(J9ROMCLASS_CLASSNAME(originalROMClass), J9ROMCLASS_CLASSNAME(replacementROMClass))) {
			return JVMTI_ERROR_NAMES_DONT_MATCH;
		}

		/* Verify that the superclass names match */

		if (NULL == originalSuperclassName) {
			if (NULL != replacementSuperclassName) {
				return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;							
			}
		} else if (NULL == replacementSuperclassName) {
			return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;			
		} else if (!utfsAreIdentical(originalSuperclassName, replacementSuperclassName)) {
			return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
		}

		/* Verify that the modifiers match */

		if ((originalROMClass->modifiers & J9AccClassCompatibilityMask) != (replacementROMClass->modifiers & J9AccClassCompatibilityMask)) {
			return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
		}

		/* 
		 * cannot add or remove a finalizer.  If (FINALIZE_NEEDED || HAS_EMPTY_FINALIZE) has changed between
		 * the original and new ROM class then the shape of the class will have to change. This can occur
		 * if a finalizer has been added or removed.
		 */
		if ((J9ROMCLASS_FINALIZE_NEEDED(originalROMClass)||J9ROMCLASS_HAS_EMPTY_FINALIZE(originalROMClass)) !=
			(J9ROMCLASS_FINALIZE_NEEDED(replacementROMClass)||J9ROMCLASS_HAS_EMPTY_FINALIZE(replacementROMClass))){
			if (J9ROMCLASS_FINALIZE_NEEDED(originalROMClass)||J9ROMCLASS_HAS_EMPTY_FINALIZE(originalROMClass)){
				/* original class had FINALIZE_NEEDED || HAS_EMPTY_FINALIZE  set so we must have removed the finalizer */
				return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED;
			} else {
				return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
			}
		}

		/* 
		 * cannot modify java.lang.Object to have a non-empty finalizer, java.lang.Object has a null 
		 * for the superclass name 
		 */
		if (J9ROMCLASS_FINALIZE_NEEDED(originalROMClass) != J9ROMCLASS_FINALIZE_NEEDED(replacementROMClass)){
			if (NULL == J9ROMCLASS_SUPERCLASSNAME(originalROMClass)){
				return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
			}
		}

		/* Verify that the set of directly-implemented interfaces is the same */


		if (originalROMClass->interfaceCount == replacementROMClass->interfaceCount) {
			U_32 j;
			J9SRP * originalInterfaces = J9ROMCLASS_INTERFACES(originalROMClass);
			J9SRP * replacementInterfaces = J9ROMCLASS_INTERFACES(replacementROMClass);

			for (j = 0; j < originalROMClass->interfaceCount; ++j) {
				if (!utfsAreIdentical(NNSRP_PTR_GET(&(originalInterfaces[j]), J9UTF8 *), NNSRP_PTR_GET(&(replacementInterfaces[j]), J9UTF8 *))) {
					return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
				}
			}
		} else {
			return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
		}

		/* Verify that instance fields are the same */

		rc = verifyFieldsAreSame(currentThread, 0, originalROMClass, replacementROMClass, extensionsEnabled, &classUsesExtensions);
		if (rc != JVMTI_ERROR_NONE) {
			return rc;
		}


		/* Verify that static fields are the same */

		rc = verifyFieldsAreSame(currentThread, J9AccStatic, originalROMClass, replacementROMClass, extensionsEnabled, &classUsesExtensions);
		if (rc != JVMTI_ERROR_NONE) {
			return rc;
		}

		/* Verify that the methods are the same */

		rc = verifyMethodsAreSame(currentThread, &classPairs[i], extensionsEnabled, &classUsesExtensions);
		if (rc != JVMTI_ERROR_NONE) {
			return rc;
		}

#if JAVA_SPEC_VERSION >= 15
		/* Verify that records attributes are the same */
		rc = verifyRecordAttributesAreSame(originalROMClass, replacementROMClass);
		if (rc != JVMTI_ERROR_NONE) {
			return rc;
		}

		/* Verify that permitted subclass attributes contain the same classes */
		rc = verifyPermittedSubclassAttributeContentsMatch(originalROMClass, replacementROMClass);
		if (rc != JVMTI_ERROR_NONE) {
			return rc;
		}
#endif /* JAVA_SPEC_VERSION >= 15 */

#if JAVA_SPEC_VERSION >= 11
		/* Verify that the nestmates attributes - nest host & nest members - are the same */
		{
			J9UTF8 *originalNestHostName = J9ROMCLASS_NESTHOSTNAME(originalROMClass);
			J9UTF8 *replacementNestHostName = J9ROMCLASS_NESTHOSTNAME(replacementROMClass);
			U_16 nestMemberCount = originalROMClass->nestMemberCount;

			/* Nest hosts must both be null or must both contain the same string */
			if ((NULL != originalNestHostName) || (NULL != replacementNestHostName)) {
				if ((NULL == originalNestHostName) || (NULL == replacementNestHostName)) {
					return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
				} else if (!J9UTF8_EQUALS(originalNestHostName, replacementNestHostName)) {
					return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
				}
			}

			if (nestMemberCount != replacementROMClass->nestMemberCount) {
				return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
			} else if (0 != nestMemberCount) {
				J9SRP *originalNestMembers = J9ROMCLASS_NESTMEMBERS(originalROMClass);
				J9SRP *replacementNestMembers = J9ROMCLASS_NESTMEMBERS(replacementROMClass);
				U_16 i = 0;

				for (i = 0; i < nestMemberCount; i++) {
					J9UTF8 *originalNestMemberName = NNSRP_GET(originalNestMembers[i], J9UTF8*);
					J9UTF8 *replacementNestMemberName = NNSRP_GET(replacementNestMembers[i], J9UTF8*);
					if (!J9UTF8_EQUALS(originalNestMemberName, replacementNestMemberName)) {
						return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_ATTRIBUTE_CHANGED;
					}
				}
			}
		}
#endif /* JAVA_SPEC_VERSION >= 11 */

		if (0 != classUsesExtensions) {
			classPairs[i].flags |= J9JVMTI_CLASS_PAIR_FLAG_USES_EXTENSIONS;
			*extensionsUsed = 1;
		}
	}

	return JVMTI_ERROR_NONE;
}

jvmtiError
verifyClassesCanBeReplaced(J9VMThread * currentThread, jint class_count, const jvmtiClassDefinition * class_definitions)
{
	jint i;

	for (i = 0; i < class_count; ++i) {
		jclass klass = class_definitions[i].klass;
		J9Class * clazz;

		/* Verify that klass is not NULL */

		if (klass == NULL) {
			return JVMTI_ERROR_INVALID_CLASS;
		}
		clazz = J9VM_J9CLASS_FROM_JCLASS(currentThread, klass);

		if (!classIsModifiable(currentThread->javaVM, clazz)) {
			return JVMTI_ERROR_UNMODIFIABLE_CLASS;
		}

		/* Verify that class_bytes is not NULL */

		if (class_definitions[i].class_bytes == NULL) {
			return JVMTI_ERROR_NULL_POINTER;
		}
	}

	return JVMTI_ERROR_NONE;
}

/*
 * Can't replace:
 * - primitive or array classes
 * - J9VMInternals
 * - sun.misc.Unsafe Anonymous classes
 * - Object (in extended HCR mode)
 */
jboolean
classIsModifiable(J9JavaVM * vm, J9Class * clazz)
{
	return !J9ROMCLASS_IS_UNMODIFIABLE(clazz->romClass);
}

jvmtiError
reloadROMClasses(J9VMThread * currentThread, jint class_count, const jvmtiClassDefinition * class_definitions, J9JVMTIClassPair * classPairs, UDATA options)
{
	J9JavaVM * vm = currentThread->javaVM;
	jint i;

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(vm->classTableMutex);
#endif

	for (i = 0; i < class_count; ++i) {
		const jvmtiClassDefinition * currentDefinition = &(class_definitions[i]);
		J9Class * originalRAMClass = J9VM_J9CLASS_FROM_JCLASS(currentThread, currentDefinition->klass);
		J9UTF8 * className = J9ROMCLASS_CLASSNAME(originalRAMClass->romClass);
		UDATA loadRC;
		J9LoadROMClassData loadData;
		j9object_t heapClass = J9VM_J9CLASS_TO_HEAPCLASS(originalRAMClass);
		J9TranslationLocalBuffer localBuffer = {J9_CP_INDEX_NONE, LOAD_LOCATION_UNKNOWN, NULL};

		/* The original rom class might have been marked unsafe (we loaded it via
		 * sun.misc.Unsafe). The new class version must also be marked as unsafe
		 * otherwise we'll fail visibility checks while creating the ramclass later on.
		 * For example, redefined class: sun.reflect.GeneratedMethodAccessor* (unsafe)
		 * that has a superclass sun.reflect.MethodAccessorImpl */

		if (J9CLASS_IS_EXEMPT_FROM_VALIDATION(originalRAMClass)) {
			options = options | J9_FINDCLASS_FLAG_UNSAFE;
		}
		loadData.classLoader = originalRAMClass->classLoader;
		if (J9ROMCLASS_IS_HIDDEN(originalRAMClass->romClass)) {
			options |= (J9_FINDCLASS_FLAG_HIDDEN | J9_FINDCLASS_FLAG_UNSAFE);
			if (J9ROMCLASS_IS_OPTIONNESTMATE_SET(originalRAMClass->romClass)) {
				options |= J9_FINDCLASS_FLAG_CLASS_OPTION_NESTMATE;
			}
			if (J9ROMCLASS_IS_OPTIONSTRONG_SET(originalRAMClass->romClass)) {
				options |= J9_FINDCLASS_FLAG_CLASS_OPTION_STRONG ;
			} else {
				options |= J9_FINDCLASS_FLAG_ANON;
				loadData.classLoader = vm->anonClassLoader;
			}
		} else if (J9_ARE_ALL_BITS_SET(originalRAMClass->classFlags, J9ClassIsAnonymous)) {
			options = options | J9_FINDCLASS_FLAG_ANON;
			loadData.classLoader = vm->anonClassLoader;
		} 
		
		/* Create the new ROM class */

		loadData.classBeingRedefined = originalRAMClass;
		loadData.className = J9UTF8_DATA(className);
		loadData.classNameLength = J9UTF8_LENGTH(className);
		loadData.classData = (U_8 *) currentDefinition->class_bytes;
		loadData.classDataLength = (UDATA) currentDefinition->class_byte_count;
		loadData.classDataObject = NULL;
		loadData.protectionDomain = J9VMJAVALANGCLASS_PROTECTIONDOMAIN(currentThread, heapClass);
		loadData.options = options;

		loadData.hostPackageName = NULL;
		loadData.hostPackageLength = 0;
		if ((J2SE_VERSION(vm) >= J2SE_V11) && J9_ARE_ALL_BITS_SET(originalRAMClass->classFlags, J9ClassIsAnonymous)) {
			J9ROMClass *hostROMClass = originalRAMClass->hostClass->romClass;
			loadData.hostPackageName = J9UTF8_DATA(J9ROMCLASS_CLASSNAME(hostROMClass));;
			loadData.hostPackageLength = packageNameLength(hostROMClass);
		}

		loadData.freeUserData = NULL;
		loadData.freeFunction = NULL;
		loadData.romClass = NULL;
		loadRC = vm->dynamicLoadBuffers->internalLoadROMClassFunction(currentThread, &loadData, &localBuffer);

		if (loadRC == BCT_ERR_NO_ERROR) {

			classPairs[i].originalRAMClass = originalRAMClass;
			classPairs[i].replacementClass.romClass = loadData.romClass;
			classPairs[i].flags = 0;
			classPairs[i].methodRemap = NULL;
			classPairs[i].methodRemapIndices = NULL;

		} else {
			/* Grab a field before releasing the mutex to avoid potential race condition */
			UDATA errorAction = ((J9CfrError *) vm->dynamicLoadBuffers->classFileError)->errorAction;
#ifdef J9VM_THR_PREEMPTIVE
			omrthread_monitor_exit(vm->classTableMutex);
#endif
			switch (loadRC) {
				case BCT_ERR_CLASS_READ:
					switch (errorAction) {
						case CFR_ThrowOutOfMemoryError:
							return JVMTI_ERROR_OUT_OF_MEMORY;
						case CFR_ThrowVerifyError:
							return JVMTI_ERROR_FAILS_VERIFICATION;
						case CFR_ThrowUnsupportedClassVersionError:
							return JVMTI_ERROR_UNSUPPORTED_VERSION;
						case CFR_ThrowNoClassDefFoundError:
							return JVMTI_ERROR_NAMES_DONT_MATCH;
					}
					break;

				case BCT_ERR_INVALID_BYTECODE:
				case BCT_ERR_STACK_MAP_FAILED:
				case BCT_ERR_VERIFY_ERROR_INLINING:
				case BCT_ERR_BYTECODE_TRANSLATION_FAILED:
				case BCT_ERR_UNKNOWN_ANNOTATION:
				case BCT_ERR_INVALID_ANNOTATION:
					return JVMTI_ERROR_FAILS_VERIFICATION;

				case BCT_ERR_OUT_OF_ROM:
				case BCT_ERR_OUT_OF_MEMORY:
					return JVMTI_ERROR_OUT_OF_MEMORY;
			}

			return JVMTI_ERROR_INVALID_CLASS_FORMAT;
		}
	}

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(vm->classTableMutex);
#endif

	return JVMTI_ERROR_NONE;
}



jvmtiError
verifyNewClasses(J9VMThread * currentThread, jint class_count, J9JVMTIClassPair * classPairs)
{
	J9JavaVM * vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	jint i = 0;
	jvmtiError rc = JVMTI_ERROR_NONE;
	J9BytecodeVerificationData *verifyData = vm->bytecodeVerificationData;

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_enter(verifyData->verifierMutex);
#endif
	verifyData->vmStruct = currentThread;

	/* CMVC 193469: verification after class modification uses incorrect class data */
	verifyData->redefinedClassesCount = (UDATA) class_count;
	if (class_count > 0) {
	    /* hold on to classPairs array for isProtectedAccessPermitted */
	    verifyData->redefinedClasses = classPairs;
	}

	/* CMVC 192547 : bytecode verification is usually done at runtime verification phase during ram class initialisation,
	 * but since we're retransforming a class and the ram class is already initialized, manually run bytecode verification on
	 * all the newly created ROM classes.
	 */
	for (i = 0; i < class_count; ++i) {
		/* Do not run bytecode verification on unsafe classes */
		if (!J9CLASS_IS_EXEMPT_FROM_VALIDATION(classPairs[i].originalRAMClass)) {
			IDATA result = 0;

			verifyData->classLoader = classPairs[i].originalRAMClass->classLoader;
			result = verifyData->verifyBytecodesFunction(PORTLIB, classPairs[i].originalRAMClass, classPairs[i].replacementClass.romClass, verifyData);
			if (-1 == result) {
				rc = JVMTI_ERROR_FAILS_VERIFICATION;
				break;
			}

			if (-2 == result) {
				rc = JVMTI_ERROR_OUT_OF_MEMORY;
				break;
			}
		}
	}
	
	verifyData->redefinedClasses = NULL;
	verifyData->redefinedClassesCount = 0;
	verifyData->vmStruct = NULL;

#ifdef J9VM_THR_PREEMPTIVE
	omrthread_monitor_exit(verifyData->verifierMutex);
#endif
	return rc;
}


static int
compareClassDepth(const void *leftPair, const void *rightPair)
{
	J9Class *left= (*(J9JVMTIClassPair **)leftPair)->originalRAMClass;
	J9Class *right= (*(J9JVMTIClassPair **)rightPair)->originalRAMClass;
	UDATA leftDepth= J9CLASS_DEPTH(left);
	UDATA rightDepth= J9CLASS_DEPTH(right);

	/* Make sure interfaces sort by superinterface relationship */

	if ((left->romClass->modifiers & J9AccInterface) && (right->romClass->modifiers & J9AccInterface)) {
		/* do the dance to order by superinterface relationship */
		J9ITable *currentITable= (J9ITable *)left->iTable;
		while (currentITable) {
			if (currentITable->interfaceClass == right) {
				/* right is a superinterface of left */
				return 1;
			}
			currentITable = currentITable->next;
		}
		/* right is not a superinterface of left, so either left is a superinterface of right, or they are not related */
		return -1;
	}

	/* Make sure interfaces sort earlier than normal classes, other than Object */

	if (left->romClass->modifiers & J9AccInterface) {
		if (0 == rightDepth) {
			return 1;
		}
		return -1;
	} else if (right->romClass->modifiers & J9AccInterface) {
		if (0 == leftDepth) {
			return -1;
		}
		return 1;
	}

	if (leftDepth == rightDepth) {
		return 0;
	} else if (leftDepth > rightDepth) {
		return 1;
	} else {
		return -1;
	}
}

void
notifyGCOfClassReplacement(J9VMThread * currentThread, J9HashTable * classPairs, UDATA isFastHCR)
{
	J9JavaVM *vm = currentThread->javaVM;
	J9MemoryManagerFunctions *mmFunctions = vm->memoryManagerFunctions;
	J9JVMTIClassPair *classPair = NULL;
	J9HashTableState hashTableState = {0};

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
		if (J9_ARE_ALL_BITS_SET(classPair->flags, J9JVMTI_CLASS_PAIR_FLAG_REDEFINED)) {
			mmFunctions->j9gc_notifyGCOfClassReplacement(currentThread, classPair->originalRAMClass, classPair->replacementClass.ramClass, isFastHCR);
		}
		classPair = hashTableNextDo(&hashTableState);
	}
}

jvmtiError
fixMethodEquivalencesAndCallSites(J9VMThread * currentThread,
	J9HashTable * classPairs,
	J9JVMTIHCRJitEventData * eventData,
	BOOLEAN fastHCR, J9HashTable **methodEquivalences,
	UDATA extensionsUsed)
{
	J9HashTableState hashTableState;
	J9JVMTIClassPair * classPair;
	jvmtiError rc = JVMTI_ERROR_NONE;

	classPair = hashTableStartDo(classPairs, &hashTableState);
	while (classPair != NULL) {
#ifdef J9VM_INTERP_NATIVE_SUPPORT
		BOOLEAN addJitEventData;
#endif
		J9Class *originalRAMClass;
		J9Class *replacementRAMClass;

		if (NULL == classPair->replacementClass.ramClass) {
			classPair = hashTableNextDo(&hashTableState);
			continue;
		}

		if (fastHCR) {
			originalRAMClass = classPair->replacementClass.ramClass;
			replacementRAMClass = classPair->originalRAMClass;
		} else {
			originalRAMClass = classPair->originalRAMClass;
			replacementRAMClass = classPair->replacementClass.ramClass;
		}

#ifdef J9VM_INTERP_NATIVE_SUPPORT
		if ((NULL != eventData) && (eventData->initialized) && (classPair->flags & J9JVMTI_CLASS_PAIR_FLAG_REDEFINED)) {
			jitEventAddClass(currentThread, eventData, (fastHCR ? replacementRAMClass : originalRAMClass), replacementRAMClass);
			addJitEventData = TRUE;
		} else {
			addJitEventData = FALSE;
		}
#endif

		if (originalRAMClass->romClass == replacementRAMClass->romClass) {
			U_32 methodIndex;
			U_32 romMethodCount = originalRAMClass->romClass->romMethodCount;

			for (methodIndex = 0; methodIndex < romMethodCount; ++methodIndex) {
				J9Method * oldMethod = originalRAMClass->ramMethods + methodIndex;
				J9Method * newMethod = replacementRAMClass->ramMethods + methodIndex;

				rc = addMethodEquivalence(currentThread, oldMethod, newMethod, methodEquivalences, romMethodCount);
				if (rc != JVMTI_ERROR_NONE) {
					goto fail;
				}
				if (!fastHCR) {
					rc = fixJNIMethodID(currentThread, oldMethod, newMethod, TRUE, extensionsUsed);
					if (rc != JVMTI_ERROR_NONE) {
						goto fail;
					}
				}
#ifdef J9VM_INTERP_NATIVE_SUPPORT
				if (addJitEventData) {
					jitEventAddMethod(currentThread, eventData, oldMethod, newMethod, 1);
				}
#endif
			}

		} else {
			U_32 oldMethodIndex;
			J9ROMClass *originalROMClass = originalRAMClass->romClass;
			J9ROMClass *replacementROMClass = replacementRAMClass->romClass;

			for (oldMethodIndex = 0; oldMethodIndex < originalROMClass->romMethodCount; ++oldMethodIndex) {
				J9Method * oldMethod = originalRAMClass->ramMethods + oldMethodIndex;
				J9Method * newMethod;
				J9ROMMethod * oldROMMethod = J9_ROM_METHOD_FROM_RAM_METHOD(oldMethod);
				J9ROMMethod * newROMMethod;
				U_32 newMethodIndex;
				U_32 romMethodCount = replacementROMClass->romMethodCount;
				BOOLEAN equivalent = FALSE;
				BOOLEAN deleted = TRUE;

				for (newMethodIndex = 0; newMethodIndex < romMethodCount; ++newMethodIndex) {
					newMethod = replacementRAMClass->ramMethods + newMethodIndex;
					newROMMethod = J9_ROM_METHOD_FROM_RAM_METHOD(newMethod);

					if (J9ROMMETHOD_NAME_AND_SIG_IDENTICAL(oldROMMethod, newROMMethod)) {
						equivalent = areMethodsEquivalentPropagateCallSites
							(oldROMMethod, originalRAMClass, newROMMethod, replacementRAMClass);
#ifdef J9VM_INTERP_NATIVE_SUPPORT
						if (addJitEventData) {
							jitEventAddMethod(currentThread, eventData, oldMethod, newMethod, equivalent);
						}
#endif
						deleted = FALSE;
						break;
					}
				}

				if (equivalent) {
					rc = addMethodEquivalence(currentThread, oldMethod, newMethod, methodEquivalences, romMethodCount);
					if (rc != JVMTI_ERROR_NONE) {
						goto fail;
					}
				}
				if (!fastHCR) {
					rc = fixJNIMethodID(currentThread, oldMethod, deleted ? NULL : newMethod, equivalent, extensionsUsed);
					if (rc != JVMTI_ERROR_NONE) {
						goto fail;
					}
				}
			}
		}

		classPair = hashTableNextDo(&hashTableState);
	}
fail:
	return rc;
}


static jvmtiError
addMethodEquivalence(J9VMThread * currentThread, J9Method * oldMethod, J9Method * newMethod, J9HashTable ** methodEquivalences, U_32 size)
{
	J9JVMTIMethodEquivalence newEquivalence = {0};
	jvmtiError rc = JVMTI_ERROR_NONE;
	
	/* Create the equivalence hash table if it does not already exist */

	if (NULL == *methodEquivalences) {
		PORT_ACCESS_FROM_VMC(currentThread);
		*methodEquivalences = hashTableNew(OMRPORT_FROM_J9PORT(PORTLIB), J9_GET_CALLSITE(), size, sizeof(J9JVMTIMethodEquivalence), 0, 0,  J9MEM_CATEGORY_JVMTI, equivalenceHash, equivalenceEquals, NULL, NULL);
		if (NULL == *methodEquivalences) {
			rc = JVMTI_ERROR_OUT_OF_MEMORY;
			goto fail;
		}
	}

	/* Add a new equivalence for oldMethod->newMethod */

	newEquivalence.oldMethod = oldMethod;
	newEquivalence.currentMethod = newMethod;
	if (NULL == hashTableAdd(*methodEquivalences, &newEquivalence)) {
		rc = JVMTI_ERROR_OUT_OF_MEMORY;
		goto fail;
	}

	/*
	 * CMVC 188007: If the equivalent method is native, copy the fields over so
	 * that the native remains bound (or compiled).  If the FSD (not fast HCR) case,
	 * natives are not currently compiled, so we don't need to worry about the
	 * translation of the native being discarded.
	 */
	if (J9_ROM_METHOD_FROM_RAM_METHOD(oldMethod)->modifiers & J9AccNative) {
		newMethod->methodRunAddress = oldMethod->methodRunAddress;
		newMethod->extra = oldMethod->extra;
	}

fail:
	return rc;
}


static J9Method *
getMethodEquivalence(J9VMThread * currentThread, J9Method * method, J9HashTable ** methodEquivalences)
{
	J9JVMTIMethodEquivalence* equivalence;
	J9JVMTIMethodEquivalence find = {0};
	
	find.oldMethod = method;

	if (NULL != *methodEquivalences) {
		equivalence = hashTableFind(*methodEquivalences, &find);
		if (NULL != equivalence) {
			return equivalence->currentMethod;
		}
	}

	return NULL;
}

static UDATA
equivalenceEquals(void *leftKey, void *rightKey, void *userData)
{
	return ((J9JVMTIMethodEquivalence *) leftKey)->oldMethod == ((J9JVMTIMethodEquivalence *) rightKey)->oldMethod;
}


static UDATA
equivalenceHash(void *key, void *userData)
{
	return ((UDATA) ((J9JVMTIMethodEquivalence *) key)->oldMethod) / sizeof(UDATA);
}


#ifdef J9VM_INTERP_NATIVE_SUPPORT
/**
 * \brief	Preallocate JIT classes redefinition event data
 * \ingroup
 *
 * @param[in]     currentThread
 * @param[in]     redefinedClassCount
 * @param[in]     redefinedMethodCount
 * @param[in/out] eventData
 * @return jvmti error code
 *
 * Preallocate memory for the JIT event carrying the classes and methods that have been redefined.
 * We do this relatively early on to mitigate error recovery risk once the classes have been replaced.
 */
static jvmtiError
jitEventInitialize(J9VMThread * currentThread, jint redefinedClassCount, UDATA redefinedMethodCount, J9JVMTIHCRJitEventData * eventData)
{
 	UDATA size;
	PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);

	size = redefinedClassCount * sizeof(J9JITRedefinedClass) + redefinedMethodCount * sizeof(J9JITMethodEquivalence);
	eventData->data = j9mem_allocate_memory(size, J9MEM_CATEGORY_JVMTI);
	if (NULL == eventData->data) {
		eventData->initialized = 0;
		return JVMTI_ERROR_OUT_OF_MEMORY;
	}

	eventData->dataCursor = eventData->data;
	eventData->classCount = 0;
	eventData->initialized = 1;

	return JVMTI_ERROR_NONE;
}
#endif


#ifdef J9VM_INTERP_NATIVE_SUPPORT
void
jitEventFree(J9JavaVM * vm, J9JVMTIHCRJitEventData * eventData)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (eventData->data) {
		j9mem_free_memory(eventData->data);
	}
}
#endif


#ifdef J9VM_INTERP_NATIVE_SUPPORT
static void
jitEventAddMethod(J9VMThread * currentThread, J9JVMTIHCRJitEventData * eventData, J9Method * oldMethod, J9Method * newMethod, UDATA equivalent)
{
    J9JITMethodEquivalence * methodEquiv;

	methodEquiv = (J9JITMethodEquivalence *) eventData->dataCursor;

	methodEquiv->oldMethod = oldMethod;
	methodEquiv->newMethod = newMethod;
    methodEquiv->equivalent = equivalent;

	Trc_hshelp_jitEventAddMethod(currentThread, oldMethod, newMethod, equivalent);

	eventData->dataCursor = (UDATA *) (((char *) methodEquiv) + sizeof(J9JITMethodEquivalence));

	return;
}
#endif

#ifdef J9VM_INTERP_NATIVE_SUPPORT
static void
jitEventAddClass(J9VMThread * currentThread, J9JVMTIHCRJitEventData * eventData, J9Class * originalRAMClass, J9Class * replacementRAMClass)
{
	J9JITRedefinedClass * classMapping;
    J9JITMethodEquivalence * methodEquiv;

	classMapping = (J9JITRedefinedClass *) eventData->dataCursor;
	methodEquiv  = (J9JITMethodEquivalence *) ((char *) classMapping + sizeof(J9JITRedefinedClass));

	classMapping->oldClass = originalRAMClass;
	classMapping->newClass = replacementRAMClass;
	classMapping->methodCount = originalRAMClass->romClass->romMethodCount;
	classMapping->methodList = methodEquiv;

    Trc_hshelp_jitEventAddClass(currentThread, originalRAMClass, replacementRAMClass, classMapping->methodCount);

    eventData->dataCursor = (UDATA *) methodEquiv;
    eventData->classCount++;

	return;
}
#endif

void
hshelpUTRegister(J9JavaVM *vm)
{
	UT_J9HSHELP_MODULE_LOADED(J9_UTINTERFACE_FROM_VM(vm));
}

/**
 * \brief	Notify the jit about redefined classes
 * \ingroup
 *
 * @param[in]     currentThread
 * @param[in]     jitEventData       event data describing redefined classes
 * @param[in]     extensionsEnabled  specifies if the extensions are enabled, always true if FSD is on
 * @return none
 *
 * Notify the JIT of the changes. Enabled extensions mean that the JIT has
 * been initialized in FSD mode. We need to dump the whole code cache in such a case.
 * If the extensions have NOT been enabled we take the smarter code path and ask the
 * jit to patch the compiled code to account for the redefined classes.
 */
void
jitClassRedefineEvent(J9VMThread * currentThread, J9JVMTIHCRJitEventData * jitEventData, UDATA extensionsEnabled)
{
	J9JavaVM * vm = currentThread->javaVM;
	J9JITConfig *jitConfig = vm->jitConfig;

	if (NULL != jitConfig) {
		jitConfig->jitClassesRedefined(currentThread, jitEventData->classCount, (J9JITRedefinedClass*)jitEventData->data);
		if (extensionsEnabled) {
			/* Toss the whole code cache */
			jitConfig->jitHotswapOccurred(currentThread);
		}
	}
}

#endif /* J9VM_INTERP_HOT_CODE_REPLACEMENT */ /* End File Level Build Flags */

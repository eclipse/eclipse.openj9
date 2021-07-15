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

#include "j9consts.h"
#include "j9cp.h"
#include "rommeth.h"
#include "bcnames.h"
#include "cfreader.h"
#include "romclasswalk.h"
#include "util_internal.h"
#include "ut_j9util.h"

static void allSlotsInROMMethodsSectionDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInROMFieldsSectionDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static J9ROMMethod* allSlotsInROMMethodDo (J9ROMClass* romClass, J9ROMMethod* method, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInROMFieldDo(J9ROMClass* romClass, J9ROMFieldShape* field, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInConstantPoolDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInEnclosingObjectDo (J9ROMClass* romClass, J9EnclosingObject* enclosingObject, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInAnnotationDo(J9ROMClass* romClass, U_32* annotation, const char *annotationSectionName,  J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInExceptionInfoDo (J9ROMClass* romClass, J9ExceptionInfo* exceptionInfo, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInMethodDebugInfoDo (J9ROMClass* romClass, U_32* cursor, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInOptionalInfoDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInStackMapFramesDo (J9ROMClass* romClass, U_8 *cursor, U_16 frameCount, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInStackMapDo (J9ROMClass* romClass, U_8* stackMap, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInVerificationTypeInfoDo (J9ROMClass* romClass, U_8* cursor, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInBytecodesDo (J9ROMClass* romClass, J9ROMMethod* method, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInCPShapeDescriptionDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInCallSiteDataDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static UDATA allSlotsInMethodParametersDataDo(J9ROMClass* romClass, U_8* cursor, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInStaticSplitMethodRefIndexesDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInSpecialSplitMethodRefIndexesDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData);
static void allSlotsInSourceDebugExtensionDo (J9ROMClass* romClass, J9SourceDebugExtension* sde, J9ROMClassWalkCallbacks* callbacks, void* userData);

static void nullSlotCallback(J9ROMClass*, U_32, void*, const char*, void*);
static void nullSectionCallback(J9ROMClass*, void*, UDATA, const char*, void*);
static BOOLEAN defaultValidateRangeCallback(J9ROMClass*, void*, UDATA, void*);

#define SLOT_CALLBACK(romClassPtr, slotType, structure, slotName) callbacks->slotCallback(romClassPtr, slotType, &structure->slotName, #slotName, userData)
/* 
 * Walk all slots in the J9ROMClass, calling callback for each slot.
 * Identify the type of slot using one of the J9ROM_ constants defined in the header file.
 * Note that NAS and UTF8 slots are treated specially, since, as invariants, they may
 * point to memory outside of the ROMClass.
 *
 * Slots are always walked before they are used. This allows the callback function to
 * relocate or correct the endianness of the slot, for instance. However the callback
 * function must NOT change the field in a way which invalidates it (e.g. changing it
 * to non-platform endianness). Any such changes must be deferred until after the
 * walk is completed.
 *
 * Note that UTF8s and NASs are not walked -- only the pointers to them are.
 * It is the callback's responsibility to walk these and to detect duplicates (since two
 * slots may point to the same address).
 */
void allSlotsInROMClassDo(J9ROMClass* romClass,
	J9ROMClassSlotCallback callback,
	J9ROMClassSectionCallback sectionCallback,
	J9ROMClassValidateRangeCallback validateRangeCallback,
	void* userData)
{
	J9ROMArrayClass* romArrayClass;
	J9SRP* srpCursor;
	J9ROMMethod* firstMethod;
	U_32 count;
	J9ROMClassWalkCallbacks romClassWalkCallbacks;
	J9ROMClassWalkCallbacks* callbacks = &romClassWalkCallbacks;
	BOOLEAN isArray;
	BOOLEAN rangeValid;

	romClassWalkCallbacks.slotCallback = (NULL != callback ? callback : nullSlotCallback);
	romClassWalkCallbacks.sectionCallback = (NULL != sectionCallback ? sectionCallback : nullSectionCallback);
	romClassWalkCallbacks.validateRangeCallback = (NULL != validateRangeCallback ? validateRangeCallback : defaultValidateRangeCallback);

	isArray = J9ROMCLASS_IS_ARRAY(romClass);
	if (isArray) {
		romArrayClass = (J9ROMArrayClass *)romClass;
	}

	/* Validate romSize field. */
	rangeValid = callbacks->validateRangeCallback(romClass, romClass, sizeof(U_32), userData);
	if (FALSE == rangeValid) {
		return;
	}

	callbacks->sectionCallback(romClass, romClass, sizeof(J9ROMClass), "romHeader", userData);
	/* walk the immediate fields in the J9ROMClass */
	SLOT_CALLBACK(romClass, J9ROM_U32, romClass, romSize);

	/* Validate that the range [romClass, romClass + romSize) is valid. */
	rangeValid = callbacks->validateRangeCallback(romClass, romClass, romClass->romSize, userData);
	if (FALSE == rangeValid) {
		return;
	}

	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, singleScalarStaticCount);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, romClass, className);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, romClass, superclassName);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, modifiers);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, extraModifiers);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, interfaceCount);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, interfaces);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, romMethodCount);
	if (isArray) {
		SLOT_CALLBACK(romClass, J9ROM_U32, romArrayClass, arrayShape);
	} else {
		SLOT_CALLBACK(romClass, J9ROM_SRP, romClass, romMethods);
	}
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, romFieldCount);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, romFields);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, objectStaticCount);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, doubleScalarStaticCount);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, ramConstantPoolCount);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, romConstantPoolCount);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, intermediateClassDataLength);
	SLOT_CALLBACK(romClass, J9ROM_WSRP, romClass, intermediateClassData);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, instanceShape);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, cpShapeDescription);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, romClass, outerClassName);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, memberAccessFlags);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, innerClassCount);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, innerClasses);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, enclosedInnerClassCount);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, enclosedInnerClasses);
#if JAVA_SPEC_VERSION >= 11
	SLOT_CALLBACK(romClass, J9ROM_UTF8, romClass, nestHost);
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, nestMemberCount);
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, unused);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, nestMembers);
#endif /* JAVA_SPEC_VERSION >= 11 */
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, majorVersion);
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, minorVersion);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, optionalFlags);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, optionalInfo);
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, maxBranchCount);
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, invokeCacheCount);
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	SLOT_CALLBACK(romClass, J9ROM_U32,  romClass, methodTypeCount);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, staticSplitMethodRefCount);
	SLOT_CALLBACK(romClass, J9ROM_U16,  romClass, specialSplitMethodRefCount);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, staticSplitMethodRefIndexes);
	SLOT_CALLBACK(romClass, J9ROM_SRP,  romClass, specialSplitMethodRefIndexes);

	/* walk interfaces SRPs block */
	srpCursor = J9ROMCLASS_INTERFACES(romClass);
	count = romClass->interfaceCount;
	rangeValid = callbacks->validateRangeCallback(romClass, srpCursor, count * sizeof(J9SRP), userData);
	if (rangeValid) {
		callbacks->sectionCallback(romClass, srpCursor, count * sizeof(J9SRP), "interfacesSRPs", userData);
		for (; count > 0; count--) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, srpCursor++, "interfaceUTF8", userData);
		}
	}

	/* walk inner classes SRPs block */
	srpCursor = J9ROMCLASS_INNERCLASSES(romClass);
	count = romClass->innerClassCount;
	rangeValid = callbacks->validateRangeCallback(romClass, srpCursor, count * sizeof(J9SRP), userData);
	if (rangeValid) {
		callbacks->sectionCallback(romClass, srpCursor, count * sizeof(J9SRP), "innerClassesSRPs", userData);
		for (; count > 0; count--) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, srpCursor++, "innerClassNameUTF8", userData);
		}
	}

#if JAVA_SPEC_VERSION >= 11
	/* walk nest members SRPs block */
	if (0 != romClass->nestMemberCount) {
		srpCursor = J9ROMCLASS_NESTMEMBERS(romClass);
		count = romClass->nestMemberCount;
		rangeValid = callbacks->validateRangeCallback(romClass, srpCursor, count * sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->sectionCallback(romClass, srpCursor, count * sizeof(J9SRP), "nestMembersSRPs", userData);
			for (; count > 0; count--) {
				callbacks->slotCallback(romClass, J9ROM_UTF8, srpCursor++, "nestMemberUTF8", userData);
			}
		}
	}
#endif /* JAVA_SPEC_VERSION >= 11 */

	/* add CP NAS section */
	firstMethod = J9ROMCLASS_ROMMETHODS(romClass);
	count = (U_32)(((UDATA)firstMethod - (UDATA)srpCursor) / (sizeof(J9SRP) * 2));
	rangeValid = callbacks->validateRangeCallback(romClass, srpCursor, count * sizeof(J9SRP) * 2, userData);
	if (rangeValid) {
		callbacks->sectionCallback(romClass, srpCursor, count * sizeof(J9SRP) * 2, "cpNamesAndSignaturesSRPs", userData);
	}

	allSlotsInROMMethodsSectionDo(romClass, callbacks, userData);
	allSlotsInROMFieldsSectionDo(romClass, callbacks, userData);
	allSlotsInCPShapeDescriptionDo(romClass, callbacks, userData);
	allSlotsInConstantPoolDo(romClass, callbacks, userData);
	allSlotsInCallSiteDataDo(romClass, callbacks, userData);
	allSlotsInOptionalInfoDo(romClass, callbacks, userData);
	allSlotsInStaticSplitMethodRefIndexesDo(romClass, callbacks, userData);
	allSlotsInSpecialSplitMethodRefIndexesDo(romClass, callbacks, userData);
}

static void allSlotsInROMMethodsSectionDo(J9ROMClass *romClass, J9ROMClassWalkCallbacks *callbacks, void *userData)
{
	U_32 count;
	J9ROMMethod *firstMethod;
	J9ROMMethod *nextMethod;
	J9ROMMethod *methodCursor;

	methodCursor = firstMethod = J9ROMCLASS_ROMMETHODS(romClass);
	for (count = romClass->romMethodCount; (NULL != methodCursor) && (count > 0); count--) {
		nextMethod = allSlotsInROMMethodDo(romClass, methodCursor, callbacks, userData);
		callbacks->sectionCallback(romClass, methodCursor, (UDATA)nextMethod - (UDATA)methodCursor, "method", userData);
		methodCursor = nextMethod;
	}
	callbacks->sectionCallback(romClass, firstMethod, (UDATA)methodCursor - (UDATA)firstMethod, "methods", userData);
}

static void allSlotsInROMFieldsSectionDo(J9ROMClass *romClass, J9ROMClassWalkCallbacks *callbacks, void *userData)
{
	U_32 count;
	J9ROMFieldWalkState state;
	J9ROMFieldShape *firstField;
	J9ROMFieldShape *fieldCursor;

	firstField = romFieldsStartDo(romClass, &state);
	fieldCursor = firstField;
	for (count = romClass->romFieldCount; count > 0; count--) {
		UDATA fieldLength = allSlotsInROMFieldDo(romClass, fieldCursor, callbacks, userData);

		if (0 == fieldLength) {
			/* Range validation failed on the field, break early. */
			break;
		}
		fieldCursor = (J9ROMFieldShape*)((UDATA)fieldCursor + fieldLength);
	}
	callbacks->sectionCallback(romClass, firstField, (UDATA)fieldCursor - (UDATA)firstField, "fields", userData);
}

static J9ROMMethod* allSlotsInROMMethodDo(J9ROMClass* romClass, J9ROMMethod* method, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	J9ROMNameAndSignature *methodNAS;
	U_32 *cursor;
	BOOLEAN rangeValid;
	U_32 extendedModifiers = getExtendedModifiersDataFromROMMethod(method);

	rangeValid = callbacks->validateRangeCallback(romClass, method, sizeof(J9ROMMethod), userData);
	if (FALSE == rangeValid) {
		return NULL;
	}

	methodNAS = &method->nameAndSignature;
	SLOT_CALLBACK(romClass, J9ROM_UTF8, methodNAS, name);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, methodNAS, signature);
	SLOT_CALLBACK(romClass, J9ROM_U32, method, modifiers);
	SLOT_CALLBACK(romClass, J9ROM_U16, method, maxStack);
	SLOT_CALLBACK(romClass, J9ROM_U16, method, bytecodeSizeLow);
	SLOT_CALLBACK(romClass, J9ROM_U8, method, bytecodeSizeHigh);
	SLOT_CALLBACK(romClass, J9ROM_U8, method, argCount);
	SLOT_CALLBACK(romClass, J9ROM_U16, method, tempCount);

	cursor = J9_GENERIC_SIG_ADDR_FROM_ROM_METHOD(method);

	allSlotsInBytecodesDo(romClass, method, callbacks, userData);

	if (J9ROMMETHOD_HAS_GENERIC_SIGNATURE(method)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_32), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, cursor, "methodUTF8", userData);
		}
		cursor++;
	}

	if (J9ROMMETHOD_HAS_EXCEPTION_INFO(method)) {
		J9ExceptionInfo *exceptionInfo = (J9ExceptionInfo *) cursor;
		UDATA exceptionInfoSize = sizeof(J9ExceptionInfo) +
			exceptionInfo->catchCount * sizeof(J9ExceptionHandler) +
			exceptionInfo->throwCount * sizeof(J9SRP);

		rangeValid = callbacks->validateRangeCallback(romClass, cursor, exceptionInfoSize, userData);
		if (rangeValid) {
			allSlotsInExceptionInfoDo(romClass, exceptionInfo, callbacks, userData);
		}
		cursor = (U_32 *)((UDATA)cursor + exceptionInfoSize);
	}

	if (J9ROMMETHOD_HAS_ANNOTATIONS_DATA(method)) {
		cursor += allSlotsInAnnotationDo(romClass, (U_32 *)cursor, "methodAnnotation", callbacks, userData);
	}

	
	if (J9ROMMETHOD_HAS_PARAMETER_ANNOTATIONS(method)) {
		cursor += allSlotsInAnnotationDo(romClass, (U_32 *)cursor, "parameterAnnotations", callbacks, userData);
	}

	if (J9ROMMETHOD_HAS_DEFAULT_ANNOTATION(method)) {
		cursor += allSlotsInAnnotationDo(romClass, (U_32 *)cursor, "defaultAnnotation", callbacks, userData);
	}
	
	if (J9ROMMETHOD_HAS_METHOD_TYPE_ANNOTATIONS(extendedModifiers)) {
		cursor += allSlotsInAnnotationDo(romClass, (U_32 *)cursor, "methodTypeAnnotations", callbacks, userData);
	}

	if (J9ROMMETHOD_HAS_CODE_TYPE_ANNOTATIONS(extendedModifiers)) {
		cursor += allSlotsInAnnotationDo(romClass, (U_32 *)cursor, "codeTypeAnnotations", callbacks, userData);
	}

	if (J9ROMMETHOD_HAS_DEBUG_INFO(method)) {
		cursor += allSlotsInMethodDebugInfoDo(romClass, (U_32 *)cursor, callbacks, userData);
	}

	if (J9ROMMETHOD_HAS_STACK_MAP(method)) {
		U_32 stackMapSize = *cursor;
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, stackMapSize, userData);
		if (rangeValid) {
			U_8 *stackMap = (U_8*) (cursor + 1);
			callbacks->sectionCallback(romClass, cursor, stackMapSize, "stackMap", userData);
			callbacks->slotCallback(romClass, J9ROM_U32, cursor, "stackMapSize", userData);
			allSlotsInStackMapDo(romClass, stackMap, callbacks, userData);
		}
		cursor = (U_32 *)((UDATA)cursor + stackMapSize);
	}

	if (J9ROMMETHOD_HAS_METHOD_PARAMETERS(method)) {
	
		cursor += allSlotsInMethodParametersDataDo(romClass, (U_8*)cursor, callbacks, userData);
	}

	return (J9ROMMethod *) cursor;
}

static UDATA allSlotsInROMFieldDo(J9ROMClass* romClass, J9ROMFieldShape* field, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	UDATA fieldLength = 0;
	J9ROMNameAndSignature *fieldNAS;
	U_32 *initialValue;
	U_32 modifiers;
	BOOLEAN rangeValid;

	rangeValid = callbacks->validateRangeCallback(romClass, field, sizeof(J9ROMFieldShape), userData);
	if (FALSE == rangeValid) {
		return 0;
	}

	fieldNAS = &field->nameAndSignature;

	SLOT_CALLBACK(romClass, J9ROM_UTF8, fieldNAS, name);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, fieldNAS, signature);
	SLOT_CALLBACK(romClass, J9ROM_U32, field, modifiers);

	modifiers = field->modifiers;
	initialValue = (U_32 *) (field + 1);

	if (modifiers & J9FieldFlagConstant) {
		if (modifiers & J9FieldSizeDouble) {
			rangeValid = callbacks->validateRangeCallback(romClass, initialValue, sizeof(U_64), userData);
			if (rangeValid) {
				callbacks->slotCallback(romClass, J9ROM_U64, initialValue, "fieldInitialValue", userData);
			}
			initialValue += 2;
		} else {
			rangeValid = callbacks->validateRangeCallback(romClass, initialValue, sizeof(U_32), userData);
			if (rangeValid) {
				callbacks->slotCallback(romClass, J9ROM_U32, initialValue, "fieldInitialValue", userData);
			}
			initialValue += 1;
		}
	}

	if (modifiers & J9FieldFlagHasGenericSignature) {
		rangeValid = callbacks->validateRangeCallback(romClass, initialValue, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, initialValue, "fieldGenSigUTF8", userData);
		}
		initialValue += 1;
	}

	if (J9FieldFlagHasFieldAnnotations == (modifiers & J9FieldFlagHasFieldAnnotations)) {
		initialValue += allSlotsInAnnotationDo(romClass, initialValue, "fieldAnnotation", callbacks, userData);
	}

	if (J9_ARE_ANY_BITS_SET(modifiers, J9FieldFlagHasTypeAnnotations)) {
		initialValue += allSlotsInAnnotationDo(romClass, initialValue, "fieldTypeAnnotation", callbacks, userData);
	}

	fieldLength = (UDATA)initialValue - (UDATA)field;
	callbacks->sectionCallback(romClass, field, fieldLength, "field", userData);

	return fieldLength;
}


static void allSlotsInExceptionInfoDo(J9ROMClass* romClass, J9ExceptionInfo* exceptionInfo, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	J9ExceptionHandler *exceptionHandler;
	J9SRP *throwNames;
	U_16 i;

	SLOT_CALLBACK(romClass, J9ROM_U16, exceptionInfo, catchCount);
	SLOT_CALLBACK(romClass, J9ROM_U16, exceptionInfo, throwCount);

	exceptionHandler = J9EXCEPTIONINFO_HANDLERS(exceptionInfo); /* endian safe */
	for (i = 0; i < exceptionInfo->catchCount; i++) {
		SLOT_CALLBACK(romClass, J9ROM_U32, exceptionHandler, startPC);
		SLOT_CALLBACK(romClass, J9ROM_U32, exceptionHandler, endPC);
		SLOT_CALLBACK(romClass, J9ROM_U32, exceptionHandler, handlerPC);
		SLOT_CALLBACK(romClass, J9ROM_U32, exceptionHandler, exceptionClassIndex);
		exceptionHandler++;
	}

	/* not using J9EXCEPTIONINFO_THROWNAMES...we're already there */
	throwNames = (J9SRP *)exceptionHandler;
	for (i = 0; i < exceptionInfo->throwCount; i++, throwNames++) {
		callbacks->slotCallback(romClass, J9ROM_UTF8, throwNames, "throwNameUTF8", userData);
	}
}


static void allSlotsInBytecodesDo(J9ROMClass* romClass, J9ROMMethod* method, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_32 i;
	I_32 high, low;
	U_8 *pc, *bytecodes;
	UDATA length, roundedLength;
	BOOLEAN rangeValid;

	/* bytecodeSizeLow already walked */
	length = J9_BYTECODE_SIZE_FROM_ROM_METHOD(method); 

	if (length == 0) return;
	pc = bytecodes = J9_BYTECODE_START_FROM_ROM_METHOD(method); /* endian safe */

	rangeValid = callbacks->validateRangeCallback(romClass, bytecodes, length, userData);
	if (FALSE == rangeValid) {
		return;
	}

	while ((UDATA)(pc - bytecodes) < length) {
		U_8 bc = *pc;
		callbacks->slotCallback(romClass, J9ROM_U8, pc, JavaBCNames[bc], userData);
		pc++;
		switch (bc) {
			/* Single 8-bit argument, no flip. */
			case JBbipush:
			case JBldc:
			case JBiload:
			case JBlload:
			case JBfload:
			case JBdload:
			case JBaload:
			case JBistore:
			case JBlstore:
			case JBfstore:
			case JBdstore:
			case JBastore:
			case JBnewarray:
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				break;
				
			/* Single 16-bit argument */
			case JBinvokeinterface2:
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				/* Deliberate fall through */
			case JBsipush:
			case JBldcw:
			case JBldc2dw:
			case JBldc2lw:
			case JBiloadw:
			case JBlloadw:
			case JBfloadw:
			case JBdloadw:
			case JBaloadw:
			case JBistorew:
			case JBlstorew:
			case JBfstorew:
			case JBdstorew:
			case JBastorew:
			case JBifeq:
			case JBifne:
			case JBiflt:
			case JBifge:
			case JBifgt:
			case JBifle:
			case JBificmpeq:
			case JBificmpne:
			case JBificmplt:
			case JBificmpge:
			case JBificmpgt:
			case JBificmple:
			case JBifacmpeq:
			case JBifacmpne:
			case JBgoto:
			case JBifnull:
			case JBifnonnull:
			case JBgetstatic:
			case JBputstatic:
			case JBgetfield:
			case JBputfield:
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			case JBwithfield:
#endif
			case JBinvokevirtual:
			case JBinvokespecial:
			case JBinvokestatic:
			case JBinvokehandle:
			case JBinvokehandlegeneric:
			case JBinvokedynamic:
			case JBinvokeinterface:
			case JBnew:
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			case JBdefaultvalue:
#endif
			case JBnewdup:
			case JBanewarray:
			case JBcheckcast:
			case JBinstanceof:
			case JBinvokestaticsplit:
			case JBinvokespecialsplit:
				callbacks->slotCallback(romClass, J9ROM_U16, pc, "bcArg16", userData);
				pc += 2;
				break;

			/* Two 8-bit arguments. */
			case JBiinc:
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				break;

			/* Two 16-bit arguments */
			case JBiincw:
				callbacks->slotCallback(romClass, J9ROM_U16, pc, "bcArg16", userData);
				pc += 2;
				callbacks->slotCallback(romClass, J9ROM_U16, pc, "bcArg16", userData);
				pc += 2;
				break;

			/* 16-bit argument followed by 8-bit argument */
			case JBmultianewarray:
				callbacks->slotCallback(romClass, J9ROM_U16, pc, "bcArg16", userData);
				pc += 2;
				callbacks->slotCallback(romClass, J9ROM_U8, pc, "bcArg8", userData);
				pc += 1;
				break;

			/* Single 32-bit argument */
			case JBgotow:
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				pc += 4;
				break;

			case JBtableswitch:
				switch((pc - bytecodes - 1) % 4) {
					case 0:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 1:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 2:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 3:
						break;
				}
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				pc += 4;
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				low = *(I_32*)pc;
				pc += 4;
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				high = *(I_32*)pc;
				pc += 4;
				for (i = 0; i <= (U_32)(high - low); ++i) {
					callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
					pc += 4;
				}
				break;
				

			case JBlookupswitch:
				switch((pc - bytecodes - 1) % 4) {
					case 0:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 1:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 2:
						callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcPad", userData);
					case 3:
						break;
				}
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				pc += 4;
				callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
				i = *(U_32*)pc;
				pc += 4;
				while (i-- > 0) {
					callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
					pc += 4;
					callbacks->slotCallback(romClass, J9ROM_U32, pc, "bcArg32", userData);
					pc += 4;
				}
				break;

			default:
				break;
		}
	}

	/* Walk the padding bytes. */
	roundedLength = J9_ROUNDED_BYTECODE_SIZE_FROM_ROM_METHOD(method);
	while (length++ < roundedLength) {
		callbacks->slotCallback(romClass, J9ROM_U8, pc++, "bcSectionPadding", userData);
	}
	callbacks->sectionCallback(romClass, bytecodes, (UDATA)pc - (UDATA)bytecodes, "methodBytecodes", userData);
}


static void allSlotsInCPShapeDescriptionDo(J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_32 i, count;
	U_32 *cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(romClass);
	BOOLEAN rangeValid;

	count = (romClass->romConstantPoolCount + J9_CP_DESCRIPTIONS_PER_U32 - 1) / J9_CP_DESCRIPTIONS_PER_U32;

	rangeValid = callbacks->validateRangeCallback(romClass, cpShapeDescription, count * sizeof(U_32), userData);
	if (rangeValid) {
		callbacks->sectionCallback(romClass, cpShapeDescription, count * sizeof(U_32), "cpShapeDescription", userData);
		for (i = 0; i < count; i++) {
			callbacks->slotCallback(romClass, J9ROM_U32, &cpShapeDescription[i], "cpShapeDescriptionU32", userData);
		}
	}
}


static void allSlotsInConstantPoolDo(J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	J9ROMConstantPoolItem* constantPool;
	UDATA index;
	U_32 *cpShapeDescription;
	U_32 shapeDesc;
	U_32 constPoolCount;
	BOOLEAN rangeValid;

	constantPool = J9_ROM_CP_FROM_ROM_CLASS(romClass);
	cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(romClass);
	constPoolCount = romClass->romConstantPoolCount;

	rangeValid = callbacks->validateRangeCallback(romClass, constantPool, constPoolCount * sizeof(U_64), userData);
	if (FALSE == rangeValid) {
		return;
	}

	callbacks->sectionCallback(romClass, constantPool, constPoolCount * sizeof(U_64), "constantPool", userData);
	for (index = 0; index < constPoolCount; index++) {
		shapeDesc = J9_CP_TYPE(cpShapeDescription, index);
		switch(shapeDesc) {
			case J9CPTYPE_CLASS:
			case J9CPTYPE_STRING:
			case J9CPTYPE_ANNOTATION_UTF8:
				callbacks->slotCallback(romClass, J9ROM_UTF8, &((J9ROMStringRef *)&constantPool[index])->utf8Data, "cpFieldUtf8", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMStringRef *)&constantPool[index])->cpType, "cpFieldType", userData);
				break;

			case J9CPTYPE_INT:
				callbacks->slotCallback(romClass, J9ROM_U32, &constantPool[index], "cpFieldInt", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, (void *)((UDATA)&constantPool[index] + sizeof(U_32)), "cpFieldIntUnused", userData);
				break;
			case J9CPTYPE_FLOAT:
				callbacks->slotCallback(romClass, J9ROM_U32, &constantPool[index], "cpFieldFloat", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, (void *)((UDATA)&constantPool[index] + sizeof(U_32)), "cpFieldFloatUnused", userData);
				break;

			case J9CPTYPE_LONG:
			case J9CPTYPE_DOUBLE:
				callbacks->slotCallback(romClass, J9ROM_U64, &constantPool[index], "cpField8", userData);
				break;

			case J9CPTYPE_FIELD:
				callbacks->slotCallback(romClass, J9ROM_NAS, &((J9ROMFieldRef *)&constantPool[index])->nameAndSignature, "cpFieldNAS", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMFieldRef *)&constantPool[index])->classRefCPIndex, "cpFieldClassRef", userData);
				break;

			case J9CPTYPE_HANDLE_METHOD:
			case J9CPTYPE_INSTANCE_METHOD:
			case J9CPTYPE_STATIC_METHOD:
			case J9CPTYPE_INTERFACE_METHOD:
			case J9CPTYPE_INTERFACE_INSTANCE_METHOD:
			case J9CPTYPE_INTERFACE_STATIC_METHOD:
				callbacks->slotCallback(romClass, J9ROM_NAS, &((J9ROMMethodRef *)&constantPool[index])->nameAndSignature, "cpFieldNAS", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMMethodRef *)&constantPool[index])->classRefCPIndex, "cpFieldClassRef", userData);
				break;

			case J9CPTYPE_METHOD_TYPE:
				callbacks->slotCallback(romClass, J9ROM_UTF8, &((J9ROMMethodTypeRef *)&constantPool[index])->signature, "cpFieldUtf8", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMMethodTypeRef *)&constantPool[index])->cpType, "cpFieldType", userData);
				break;

			case J9CPTYPE_METHODHANDLE:
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMMethodHandleRef *)&constantPool[index])->methodOrFieldRefIndex, "cpFieldMethodOrFieldRef", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMMethodHandleRef *)&constantPool[index])->handleTypeAndCpType, "cpFieldHandleTypeAndCpType", userData);
				break;

			case J9CPTYPE_CONSTANT_DYNAMIC:
				callbacks->slotCallback(romClass, J9ROM_NAS, &((J9ROMConstantDynamicRef *)&constantPool[index])->nameAndSignature, "cpFieldNAS", userData);
				callbacks->slotCallback(romClass, J9ROM_U32, &((J9ROMConstantDynamicRef *)&constantPool[index])->bsmIndexAndCpType, "cpFieldBSMIndexAndCpType", userData);
				break;

			case J9CPTYPE_UNUSED:
			case J9CPTYPE_UNUSED8:
				callbacks->slotCallback(romClass, J9ROM_U64, &constantPool[index], "cpFieldUnused", userData);
				break;

			default:
				/* unknown cp type - bail */
				Assert_Util_unreachable();
		}
	}
}

static UDATA
allSlotsInRecordComponentDo(J9ROMClass* romClass, J9ROMRecordComponentShape* recordComponent, J9ROMClassWalkCallbacks* callbacks, void* userData) {
	BOOLEAN rangeValid = FALSE;
	U_32 attributeFlags = 0;
	J9ROMNameAndSignature *recordComponentNAS = NULL;
	UDATA increment = 0; /* this will be the size of the record component in U_32 to match results from allSlotsInAnnotationDo. */

	rangeValid = callbacks->validateRangeCallback(romClass, recordComponent, sizeof(J9ROMRecordComponentShape), userData);
	if (FALSE == rangeValid) {
		return 0;
	}

	attributeFlags = recordComponent->attributeFlags;
	recordComponentNAS = &recordComponent->nameAndSignature;

	SLOT_CALLBACK(romClass, J9ROM_UTF8, recordComponentNAS, name);
	SLOT_CALLBACK(romClass, J9ROM_UTF8, recordComponentNAS, signature);
	SLOT_CALLBACK(romClass, J9ROM_U32, recordComponent, attributeFlags);

	increment += sizeof(J9ROMRecordComponentShape) / sizeof(U_32);

	if (J9_ARE_ANY_BITS_SET(attributeFlags, J9RecordComponentFlagHasGenericSignature)) {
		rangeValid = callbacks->validateRangeCallback(romClass, (U_32*)recordComponent + increment, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, (U_32*)recordComponent + increment, "recordComponentGenSigUTF8", userData);
		}
		increment += sizeof(J9SRP) / sizeof(U_32);
	}

	if (J9_ARE_ANY_BITS_SET(attributeFlags, J9RecordComponentFlagHasAnnotations)) {
		increment += allSlotsInAnnotationDo(romClass, (U_32*)recordComponent + increment, "recordComponentAnnotation", callbacks, userData);
	}

	if (J9_ARE_ANY_BITS_SET(attributeFlags, J9RecordComponentFlagHasTypeAnnotations)) {
		increment += allSlotsInAnnotationDo(romClass, (U_32*)recordComponent + increment, "recordComponentTypeAnnotations", callbacks, userData);
	}

	callbacks->sectionCallback(romClass, recordComponent, increment * sizeof(U_32), "recordComponentInfo", userData);

	return increment;
}

static void allSlotsInRecordDo(J9ROMClass* romClass, U_32* recordPointer, J9ROMClassWalkCallbacks* callbacks, void* userData) {
	BOOLEAN rangeValid = FALSE;
	U_32 recordComponentCount = 0;
	U_32* cursor = recordPointer;

	rangeValid = callbacks->validateRangeCallback(romClass, recordPointer, sizeof(U_32), userData);
	if (FALSE == rangeValid) {
		return;
	}
	callbacks->slotCallback(romClass, J9ROM_U32, recordPointer, "recordComponentCount", userData);
	cursor += 1;
	recordComponentCount = *recordPointer;

	for (; recordComponentCount > 0; recordComponentCount--) {
		cursor += allSlotsInRecordComponentDo(romClass, (J9ROMRecordComponentShape*)cursor, callbacks, userData);
	}

	callbacks->sectionCallback(romClass, recordPointer, (UDATA)cursor - (UDATA)recordPointer, "recordInfo", userData);
}

static void 
allSlotsInPermittedSubclassesDo(J9ROMClass* romClass, U_32* permittedSubclassesPointer, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	BOOLEAN rangeValid = FALSE;
	U_32 *cursor = permittedSubclassesPointer;
	U_32 permittedSubclassesCount = 0;

	rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_32), userData);
	if (FALSE == rangeValid) {
		return;
	}

	callbacks->slotCallback(romClass, J9ROM_U32, cursor, "permittedSubclassesCount", userData);
	cursor += 1;
	permittedSubclassesCount = *permittedSubclassesPointer;
	
	for (; permittedSubclassesCount > 0; permittedSubclassesCount--) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_32), userData);
		if (FALSE == rangeValid) {
			return;
		}
		callbacks->slotCallback(romClass, J9ROM_UTF8, cursor, "className", userData);
		cursor += 1;
	}

	callbacks->sectionCallback(romClass, permittedSubclassesPointer, (UDATA)cursor - (UDATA)permittedSubclassesPointer, "permittedSubclassesInfo", userData);
}

/*
 * See ROMClassWriter::writeOptionalInfo for illustration of the layout.
 */
static void 
allSlotsInOptionalInfoDo(J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_32 *optionalInfo;
	J9SRP *cursor;
	BOOLEAN rangeValid;

	optionalInfo = J9ROMCLASS_OPTIONALINFO(romClass);
	cursor = (J9SRP *)optionalInfo;

	if (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_SOURCE_FILE_NAME) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, cursor, "optionalFileNameUTF8", userData);
		}
		cursor++;
	}
	if (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_GENERIC_SIGNATURE) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, cursor, "optionalGenSigUTF8", userData);
		}
		cursor++;
	}
	if (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_SOURCE_DEBUG_EXTENSION) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "optionalSourceDebugExtSRP", userData);
			allSlotsInSourceDebugExtensionDo(romClass, SRP_PTR_GET(cursor, J9SourceDebugExtension *), callbacks, userData);
		}
		cursor++;
	}
	if (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_ENCLOSING_METHOD) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "optionalEnclosingMethodSRP", userData);
			allSlotsInEnclosingObjectDo(romClass, SRP_PTR_GET(cursor, J9EnclosingObject *), callbacks, userData);
		}
		cursor++;
	}
	if (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_SIMPLE_NAME) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, cursor, "optionalSimpleNameUTF8", userData);
		}
		cursor++;
	}
	if (J9_ROMCLASS_OPTINFO_VERIFY_EXCLUDE == (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_VERIFY_EXCLUDE)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_U32, cursor, "optionalVerifyExclude", userData);
		}
		cursor++;
	}
	if (J9_ROMCLASS_OPTINFO_CLASS_ANNOTATION_INFO == (romClass->optionalFlags & J9_ROMCLASS_OPTINFO_CLASS_ANNOTATION_INFO)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "classAnnotationsSRP", userData);
			allSlotsInAnnotationDo(romClass, SRP_PTR_GET(cursor, U_32 *), "classAnnotations", callbacks, userData);
		}
		cursor++;
	}
	if (J9_ARE_ANY_BITS_SET(romClass->optionalFlags, J9_ROMCLASS_OPTINFO_TYPE_ANNOTATION_INFO)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "classTypeAnnotationsSRP", userData);
			allSlotsInAnnotationDo(romClass, SRP_PTR_GET(cursor, U_32 *), "classTypeAnnotations", callbacks, userData);
		}
		cursor++;
	}
	if (J9_ARE_ANY_BITS_SET(romClass->optionalFlags, J9_ROMCLASS_OPTINFO_RECORD_ATTRIBUTE)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "recordSRP", userData);
			allSlotsInRecordDo(romClass, SRP_PTR_GET(cursor, U_32*), callbacks, userData);
		}
		cursor++;
	}
	if (J9ROMCLASS_IS_SEALED(romClass)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "permittedSubclassesSRP", userData);
			allSlotsInPermittedSubclassesDo(romClass, SRP_PTR_GET(cursor, U_32*), callbacks, userData);
		}
		cursor++;
	}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	if (J9_ARE_ANY_BITS_SET(romClass->optionalFlags, J9_ROMCLASS_OPTINFO_INJECTED_INTERFACE_INFO)) {
		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(J9SRP), userData);
		if (rangeValid) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "optionalInjectedInterfaces", userData);
		}
		cursor++;
	}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	callbacks->sectionCallback(romClass, optionalInfo, (UDATA)cursor - (UDATA)optionalInfo, "optionalInfo", userData);
}

static void 
allSlotsInStaticSplitMethodRefIndexesDo(J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_16 count = romClass->staticSplitMethodRefCount;

	if (count > 0) {
		U_16 *cursor = J9ROMCLASS_STATICSPLITMETHODREFINDEXES(romClass);
		BOOLEAN rangeValid = callbacks->validateRangeCallback(romClass, cursor, count * sizeof(U_16), userData);

		if (rangeValid) {
			U_16 i = 0;

			callbacks->sectionCallback(romClass, cursor, count * sizeof(U_16), "staticSplitMethodRefIndexes", userData);
			for (i = 0; i < count; i++) {
				callbacks->slotCallback(romClass, J9ROM_U16, cursor, "cpIndex", userData);
				cursor += 1;
			}
		}
	}
}

static void 
allSlotsInSpecialSplitMethodRefIndexesDo(J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_16 count = romClass->specialSplitMethodRefCount;

	if (count > 0) {
		U_16 *cursor = J9ROMCLASS_SPECIALSPLITMETHODREFINDEXES(romClass);
		BOOLEAN rangeValid = callbacks->validateRangeCallback(romClass, cursor, count * sizeof(U_16), userData);

		if (rangeValid) {
			U_16 i = 0;

			callbacks->sectionCallback(romClass, cursor, count * sizeof(U_16), "specialSplitMethodRefIndexes", userData);
			for (i = 0; i < count; i++) {
				callbacks->slotCallback(romClass, J9ROM_U16, cursor, "cpIndex", userData);
				cursor += 1;
			}
		}
	}
}

#ifndef SWAP2BE
#ifdef J9VM_ENV_LITTLE_ENDIAN
#define SWAP2BE(v) ((((v) >> (8*1)) & 0x00FF) | (((v) << (8*1)) & 0xFF00))
#else
#define SWAP2BE(v) (v)
#endif
#endif

static UDATA
allSlotsInStackMapFramesDo(J9ROMClass* romClass, U_8 *cursor, U_16 frameCount, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_8 *cursorStart = NULL;
	U_16 count;
	BOOLEAN rangeValid;

	for (; frameCount > 0; frameCount--) {
		U_8 frameType;
		UDATA length;

		rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_8), userData);
		if (FALSE == rangeValid) {
			goto fail;
		}

		if (NULL == cursorStart) {
			cursorStart = cursor;
		}

		callbacks->slotCallback(romClass, J9ROM_U8, cursor, "stackMapFrameType", userData);
		frameType = *cursor++;

		if (CFR_STACKMAP_SAME_LOCALS_1_STACK > frameType) { /* 0..63 */
			/* SAME frame - no extra data */
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_END > frameType) { /* 64..127 */
			length = allSlotsInVerificationTypeInfoDo(romClass, cursor, callbacks, userData);
			if (0 == length) {
				goto fail;
			}
			cursor += length;
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED > frameType) { /* 128..246 */
			/* Reserved frame types - no extra data */
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED == frameType) { /* 247 */
			rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				goto fail;
			}
			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameOffset", userData);
			cursor += 2;
			length = allSlotsInVerificationTypeInfoDo(romClass, cursor, callbacks, userData);
			if (0 == length) {
				goto fail;
			}
			cursor += length;
		} else if (CFR_STACKMAP_SAME_EXTENDED >= frameType) { /* 248..251 */
			rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				goto fail;
			}
			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameOffset", userData);
			cursor += 2;
		} else if (CFR_STACKMAP_FULL > frameType) { /* 252..254 */
			rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				goto fail;
			}
			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameOffset", userData);
			cursor += 2;
			count = frameType - CFR_STACKMAP_APPEND_BASE;
			for (; count > 0; count--) {
				length = allSlotsInVerificationTypeInfoDo(romClass, cursor, callbacks, userData);
				if (0 == length) {
					goto fail;
				}
				cursor += length;
			}
		} else if (CFR_STACKMAP_FULL == frameType) { /* 255 */
			rangeValid = callbacks->validateRangeCallback(romClass, cursor, 2*sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				goto fail;
			}

			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameOffset", userData);
			cursor += 2;
			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameLocalsCount", userData);
			count = *(U_16 *)cursor;
			count = SWAP2BE(count);
			cursor += 2;

			for (; count > 0; count--) {
				length = allSlotsInVerificationTypeInfoDo(romClass, cursor, callbacks, userData);
				if (0 == length) {
					goto fail;
				}
				cursor += length;
			}

			rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				goto fail;
			}

			callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameItemsCount", userData);
			count = *(U_16 *)cursor;
			count = SWAP2BE(count);
			cursor += 2;

			for (; count > 0; count--) {
				length = allSlotsInVerificationTypeInfoDo(romClass, cursor, callbacks, userData);
				if (0 == length) {
					goto fail;
				}
				cursor += length;
			}
		}
	}

fail:
	return (UDATA)cursor - (UDATA)cursorStart;
}

static void allSlotsInStackMapDo(J9ROMClass* romClass, U_8 *stackMap, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_8 *cursor = stackMap;
	U_16 frameCount;

	if (NULL == stackMap) {
		return;
	}

	/* TODO: Questions?
	 *  - do we want to display the non-flipped value?
	 *  - do we want to flip and display?
	 *  - do we want to have a J9ROM_U16_BE type?
	 *  */
	callbacks->slotCallback(romClass, J9ROM_U16, cursor, "stackMapFrameCount", userData);
	frameCount = *(U_16 *)cursor;
	frameCount = SWAP2BE(frameCount);
	cursor += 2;

	allSlotsInStackMapFramesDo(romClass, cursor, frameCount, callbacks, userData);
}


static UDATA
allSlotsInMethodParametersDataDo(J9ROMClass* romClass, U_8 * cursor, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	J9MethodParametersData* methodParametersData = (J9MethodParametersData* )cursor;
	J9MethodParameter * parameters = &methodParametersData->parameters;
	U_32 methodParametersSize = J9_METHOD_PARAMS_SIZE_FROM_NUMBER_OF_PARAMS(methodParametersData->parameterCount);
	U_32 padding = sizeof(U_32) - (methodParametersSize % sizeof(U_32));
	BOOLEAN rangeValid = FALSE;
	UDATA i = 0;
	UDATA size = 0;

	if (sizeof(U_32) == padding) {
		padding = 0;
	}

	size = methodParametersSize + padding;

	rangeValid = callbacks->validateRangeCallback(romClass, methodParametersData, methodParametersSize, userData);
	if (rangeValid == TRUE) {
		SLOT_CALLBACK(romClass, J9ROM_U8, methodParametersData, parameterCount);

		for (; i < methodParametersData->parameterCount; i++) {
			callbacks->slotCallback(romClass, J9ROM_UTF8, &parameters[i].name, "methodParameterName", userData);
			callbacks->slotCallback(romClass, J9ROM_U16, &parameters[i].flags, "methodParameterFlag", userData);
		}
	}

	cursor += methodParametersSize;
	for (; padding > 0; padding--) {
		callbacks->slotCallback(romClass, J9ROM_U8, cursor++, "MethodParameters padding", userData);
	}

	callbacks->sectionCallback(romClass, methodParametersData, size, "Method Parameters", userData);
	return size/sizeof(U_32);
}

static UDATA
allSlotsInVerificationTypeInfoDo(J9ROMClass* romClass, U_8* cursor, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	U_8 type;
	BOOLEAN rangeValid;

	rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_8), userData);
	if (FALSE == rangeValid) {
		return 0;
	}

	callbacks->slotCallback(romClass, J9ROM_U8, cursor, "typeInfoTag", userData);
	type = *cursor++;
	if (type < CFR_STACKMAP_TYPE_OBJECT) {
		return 1;
	}

	rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_16), userData);
	if (FALSE == rangeValid) {
		return 0;
	}
	callbacks->slotCallback(romClass, J9ROM_U16, cursor, "typeInfoU16", userData);

	return 3;
}

static UDATA
allSlotsInAnnotationDo(J9ROMClass* romClass, U_32* annotation, const char *annotationSectionName,  J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	UDATA increment = 0;
	BOOLEAN rangeValid = callbacks->validateRangeCallback(romClass, annotation, sizeof(U_32), userData);
	if (rangeValid) {
		U_32 annotationLength = *annotation;
		/* determine how many U_32 sized chunks to increment initialValue by
		 * NOTE: annotation length is U_32 and does not include the length field itself
		 * annotations are padded to U_32 which is also not included in the length field*/
		U_32 padding = sizeof(U_32) - (annotationLength % sizeof(U_32));
		increment = annotationLength / sizeof(U_32);
		if (sizeof(U_32) == padding) {
			padding = 0;
		}
		if (padding > 0) {
			increment ++;
		}
		callbacks->slotCallback(romClass, J9ROM_U32, annotation, "annotation length", userData);
		rangeValid = callbacks->validateRangeCallback(romClass, annotation + 1, annotationLength, userData);
		if (rangeValid) {
			U_32 count = annotationLength;
			U_8 *cursor = (U_8*)(annotation+1);
			for (; count > 0; count--) {
				callbacks->slotCallback(romClass, J9ROM_U8, cursor++, "annotation data", userData);
			}
			count = padding;
			for (; count > 0; count--) {
				callbacks->slotCallback(romClass, J9ROM_U8, cursor++, "annotation padding", userData);
			}
		}
	}
	/* move past the annotation length */
	increment += 1;
	callbacks->sectionCallback(romClass, annotation, increment * sizeof(U_32), annotationSectionName, userData);

	return increment;
}

/* returns the size of the debug information stored inline in the ROMMethod in increments of sizeof(U_32) */
static UDATA
allSlotsInMethodDebugInfoDo(J9ROMClass* romClass, U_32* cursor, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	BOOLEAN rangeValid;
	J9MethodDebugInfo *methodDebugInfo;
	U_8 *lineNumberTable;
	U_32 lineNumberTableCompressedSize;
	U_8 *variableTable;
	/* if data is out of line, then the size of the data inline in the method is a single SRP in sizeof(U_32 increments), currently assuming J9SRP is U_32 aligned*/
	UDATA inlineSize = sizeof(J9SRP)/sizeof(U_32);

	rangeValid = callbacks->validateRangeCallback(romClass, cursor, sizeof(U_32), userData);
	if (FALSE == rangeValid) {
		return 0;
	}

	/* check for low tag to indicate inline or out of line debug information */
	BOOLEAN isInline = (1 == (*cursor & 1));
	if (isInline) {
		methodDebugInfo = (J9MethodDebugInfo *)cursor;
		/* set the inline size to stored size in terms of U_32
		 * NOTE: stored size is aligned
		 * tag bit will be dropped by the '/' operation */
		inlineSize = *cursor / sizeof(U_32);
	} else {
		methodDebugInfo = SRP_PTR_GET(cursor, J9MethodDebugInfo *);
	}

	rangeValid = callbacks->validateRangeCallback(romClass, methodDebugInfo, sizeof(J9MethodDebugInfo), userData);
	if ((FALSE == rangeValid) || (FALSE == isInline)) {
		if (1 == inlineSize) {
			callbacks->slotCallback(romClass, J9ROM_SRP, cursor, "SRP to DebugInfo", userData);
			callbacks->sectionCallback(romClass, cursor, inlineSize * sizeof(U_32), "methodDebugInfo", userData);
		}
		if (FALSE == rangeValid) {
			/* linear walker will check that the range is within the ROMClass bounds
			 * so it will skip walking the debug info if it is out of line */
			return inlineSize;
		}
	}

	callbacks->slotCallback(romClass, J9ROM_U32, &methodDebugInfo->srpToVarInfo, "SizeOfDebugInfo(low tagged)", userData);
	callbacks->slotCallback(romClass, J9ROM_U32, &methodDebugInfo->lineNumberCount, "lineNumberCount(low tagged)", userData);
	SLOT_CALLBACK(romClass, J9ROM_U32, methodDebugInfo, varInfoCount);
	if (1 == (methodDebugInfo->lineNumberCount & 0x1)) {
		callbacks->slotCallback(romClass, J9ROM_U32, (U_32*)(methodDebugInfo+1), "compressed line number size", userData);
	}

	lineNumberTable = getLineNumberTable(methodDebugInfo);
	lineNumberTableCompressedSize = getLineNumberCompressedSize(methodDebugInfo);
	if (NULL != lineNumberTable) {
		U_32 j;
		rangeValid = callbacks->validateRangeCallback(romClass, lineNumberTable, lineNumberTableCompressedSize * sizeof(U_8), userData);
		if (rangeValid) {
			for (j = 0; j < lineNumberTableCompressedSize; j++) {
				callbacks->slotCallback(romClass, J9ROM_U8, lineNumberTable++, "pc, lineNumber compressed", userData);
			}
		}
	}

	variableTable = getVariableTableForMethodDebugInfo(methodDebugInfo);

	if (NULL != variableTable) {
		UDATA entryLength = 0;
		J9VariableInfoWalkState state;
		J9VariableInfoValues *values = NULL;
		U_8 *start;
		values = variableInfoStartDo(methodDebugInfo, &state);

		start = getVariableTableForMethodDebugInfo(methodDebugInfo);

		while (NULL != values) {
			/* Need to walk the name and signature to add them to the UTF8 section */
			rangeValid = callbacks->validateRangeCallback(romClass, values->nameSrp, sizeof(J9SRP), userData);
			if (rangeValid) {
				callbacks->slotCallback(romClass, J9ROM_UTF8, values->nameSrp, "variableName", userData);
			}
			rangeValid = callbacks->validateRangeCallback(romClass, values->signatureSrp, sizeof(J9SRP), userData);
			if (rangeValid) {
				callbacks->slotCallback(romClass, J9ROM_UTF8, values->signatureSrp, "variableSignature", userData);
			}
			if (NULL != values->genericSignature) {
				rangeValid = callbacks->validateRangeCallback(romClass, values->genericSignatureSrp, sizeof(J9SRP), userData);
				if (rangeValid) {
					callbacks->slotCallback(romClass, J9ROM_UTF8, values->genericSignatureSrp, "variableGenericSignature", userData);
				}
			}

			values = variableInfoNextDo(&state);
		}
		while (start < state.variableTablePtr) {
			rangeValid = callbacks->validateRangeCallback(romClass, start, sizeof(U_8), userData);
			if (FALSE == rangeValid) {
				break;
			}
			callbacks->slotCallback(romClass, J9ROM_U8, start++, "local variable compressed", userData);
			entryLength += sizeof(U_8);
		}
		callbacks->sectionCallback(romClass, variableTable, entryLength, "variableInfo", userData);
	}
	if (isInline) { /* section callback was already called if debug info is out of line */
		callbacks->sectionCallback(romClass, cursor, inlineSize * sizeof(U_32), "methodDebugInfo", userData);
	}
	return inlineSize;
}

static void
allSlotsInEnclosingObjectDo(J9ROMClass* romClass, J9EnclosingObject* enclosingObject, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	BOOLEAN rangeValid;

	if (NULL == enclosingObject) {
		return;
	}

	rangeValid = callbacks->validateRangeCallback(romClass, enclosingObject, sizeof(U_32) + sizeof(J9SRP), userData);
	if (rangeValid) {
		SLOT_CALLBACK(romClass, J9ROM_U32, enclosingObject, classRefCPIndex);
		SLOT_CALLBACK(romClass, J9ROM_NAS, enclosingObject, nameAndSignature);
		callbacks->sectionCallback(romClass, enclosingObject, sizeof(J9EnclosingObject), "enclosingObject", userData);
	}
}

static void allSlotsInCallSiteDataDo (J9ROMClass* romClass, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	/*
	 * romClass->callSiteData : {
	 *		SRP callSiteNAS[romClass->callSiteCount];
	 *		U_16 callSiteBSMIndex[romClass->callSiteCount];
	 *		{
	 *			U_16 bootStrapMethodHandleRef;
	 *			U_16 argumentCount;
	 *			U_16 argument[argumentCount];
	 *		} bootStrapMethodData[romClass->bsmCount];
	 * }
	 * 
	 * Note: SRP is 32 bits
	 */
	BOOLEAN rangeValid;
	UDATA index, bsmArgumentCount;
	UDATA callSiteCount = romClass->callSiteCount;
	UDATA bsmCount = romClass->bsmCount;
	J9SRP *callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(romClass);
	U_16 *bsmIndices = (U_16 *) (callSiteData + callSiteCount);
	U_16 *bsmData = bsmIndices + callSiteCount;
	U_16 *bsmDataCursor = bsmData; /* bsmDataCursor initially points at the data for the first BSM (bootstrap method) */

	rangeValid = callbacks->validateRangeCallback(romClass, callSiteData, callSiteCount * sizeof(J9SRP) + callSiteCount * sizeof(U_16), userData);
	if (FALSE == rangeValid) {
		return;
	}

	for (index = 0; index < callSiteCount; index++) {
		callbacks->slotCallback(romClass, J9ROM_NAS, callSiteData + index, "callSiteNAS", userData);
		callbacks->slotCallback(romClass, J9ROM_U16, bsmIndices + index, "callSiteBSMIndex", userData);
	}

	for (index = 0; index < bsmCount; index++) {
		/* bsmDataCursor now points at the data for the index-th BSM */
		rangeValid = callbacks->validateRangeCallback(romClass, bsmDataCursor, 2 * sizeof(U_16), userData);
		if (FALSE == rangeValid) {
			return;
		}

		callbacks->slotCallback(romClass, J9ROM_U16, bsmDataCursor + 0, "bootstrapMethodHandleRef", userData);
		callbacks->slotCallback(romClass, J9ROM_U16, bsmDataCursor + 1, "bootstrapMethodArgumentCount", userData);

		bsmArgumentCount = bsmDataCursor[1];
		bsmDataCursor += 2; /* bsmDataCursor now points at the first BSM argument */
		if (0 != bsmArgumentCount) {
			rangeValid = callbacks->validateRangeCallback(romClass, bsmDataCursor, bsmArgumentCount * sizeof(U_16), userData);
			if (FALSE == rangeValid) {
				return;
			}

			for (; 0 != bsmArgumentCount; bsmArgumentCount--) {
				callbacks->slotCallback(romClass, J9ROM_U16, bsmDataCursor++, "bootstrapMethodArgument", userData);
			}
		}
		/* bsmDataCursor now points after the data for the index-th BSM */
	}

	/* bsmDataCursor now points after all the bootstrap method data */
	callbacks->sectionCallback(romClass, callSiteData, (U_8 *)bsmDataCursor - (U_8 *)callSiteData, "callSiteData", userData);
}

static void
allSlotsInSourceDebugExtensionDo(J9ROMClass* romClass, J9SourceDebugExtension* sde, J9ROMClassWalkCallbacks* callbacks, void* userData)
{
	BOOLEAN rangeValid;
	UDATA alignedSize, i;
	U_8 *data;

	if (NULL == sde) {
		return;
	}
	rangeValid = callbacks->validateRangeCallback(romClass, sde, sizeof(J9SourceDebugExtension), userData);
	if (FALSE == rangeValid) {
		return;
	}

	callbacks->slotCallback(romClass, J9ROM_U32, &sde->size, "optionalSourceDebugExtSize", userData);
	alignedSize = (sde->size + sizeof(U_32) - 1) & ~(sizeof(U_32) - 1);
	rangeValid = callbacks->validateRangeCallback(romClass, sde + 1, alignedSize, userData);
	if (FALSE == rangeValid) {
		return;
	}

	data = (U_8 *)(sde + 1);
	for (i = 0; i < sde->size; ++i) {
		callbacks->slotCallback(romClass, J9ROM_U8, data + i, "optionalSourceDebugExtData", userData);
	}
	for (i = sde->size; i < alignedSize; ++i) {
		callbacks->slotCallback(romClass, J9ROM_U8, data + i, "optionalSourceDebugExtPadding", userData);
	}

	callbacks->sectionCallback(romClass, sde, sizeof(J9SourceDebugExtension) + alignedSize, "optionalSourceDebugExt", userData);
}

static void
nullSlotCallback(J9ROMClass *romClass, U_32 type, void *slotPtr, const char *slotName, void *userData)
{
	/* does nothing */
}

static void
nullSectionCallback(J9ROMClass *romClass, void *sectionPtr, UDATA sectionLength, const char *sectionName, void *userData)
{
	/* does nothing */
}

static BOOLEAN
defaultValidateRangeCallback(J9ROMClass *romClass, void *address, UDATA length, void *userData)
{
	/* everything is valid */
	return TRUE;
}

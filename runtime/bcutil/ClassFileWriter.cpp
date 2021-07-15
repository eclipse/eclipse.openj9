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
/*
 * ClassFileWriter.cpp
 */

/* Note: j9.h has to be included before bcnames.h to avoid conflict of
 * J9InternalVMLabels->JBaload0getfield in j9generated.h with
 * the macro JBaload0getfield in bcnames.h.
 */
#include "j9.h"
#include "bcnames.h"
#include "bcutil.h"
#include "cfr.h"
#include "cfreader.h"
#include "pcstack.h"
#include "rommeth.h"
#include "util_api.h"

#include "ClassFileWriter.hpp"

#define DECLARE_UTF8_ATTRIBUTE_NAME(instanceName, name) \
static const struct { \
	U_16 length; \
	U_8 data[sizeof(name)]; \
} instanceName = { sizeof(name) - 1, name }

DECLARE_UTF8_ATTRIBUTE_NAME(CONSTANT_VALUE, "ConstantValue");
DECLARE_UTF8_ATTRIBUTE_NAME(CODE, "Code");
DECLARE_UTF8_ATTRIBUTE_NAME(STACK_MAP_TABLE, "StackMapTable");
DECLARE_UTF8_ATTRIBUTE_NAME(EXCEPTIONS, "Exceptions");
DECLARE_UTF8_ATTRIBUTE_NAME(METHODPARAMETERS, "MethodParameters");
DECLARE_UTF8_ATTRIBUTE_NAME(INNER_CLASSES, "InnerClasses");
DECLARE_UTF8_ATTRIBUTE_NAME(ENCLOSING_METHOD, "EnclosingMethod");
DECLARE_UTF8_ATTRIBUTE_NAME(SIGNATURE, "Signature");
DECLARE_UTF8_ATTRIBUTE_NAME(SOURCE_FILE, "SourceFile");
DECLARE_UTF8_ATTRIBUTE_NAME(SOURCE_DEBUG_EXTENSION, "SourceDebugExtension");
DECLARE_UTF8_ATTRIBUTE_NAME(LINE_NUMBER_TABLE, "LineNumberTable");
DECLARE_UTF8_ATTRIBUTE_NAME(LOCAL_VARIABLE_TABLE, "LocalVariableTable");
DECLARE_UTF8_ATTRIBUTE_NAME(LOCAL_VARIABLE_TYPE_TABLE, "LocalVariableTypeTable");
DECLARE_UTF8_ATTRIBUTE_NAME(RUNTIME_VISIBLE_ANNOTATIONS, "RuntimeVisibleAnnotations");
DECLARE_UTF8_ATTRIBUTE_NAME(RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS, "RuntimeVisibleParameterAnnotations");
DECLARE_UTF8_ATTRIBUTE_NAME(RUNTIME_VISIBLE_TYPE_ANNOTATIONS, "RuntimeVisibleTypeAnnotations");
DECLARE_UTF8_ATTRIBUTE_NAME(ANNOTATION_DEFAULT, "AnnotationDefault");
DECLARE_UTF8_ATTRIBUTE_NAME(BOOTSTRAP_METHODS, "BootstrapMethods");
DECLARE_UTF8_ATTRIBUTE_NAME(RECORD, "Record");
DECLARE_UTF8_ATTRIBUTE_NAME(PERMITTED_SUBCLASSES, "PermittedSubclasses");
#if JAVA_SPEC_VERSION >= 11
DECLARE_UTF8_ATTRIBUTE_NAME(NEST_MEMBERS, "NestMembers");
DECLARE_UTF8_ATTRIBUTE_NAME(NEST_HOST, "NestHost");
#endif /* JAVA_SPEC_VERSION >= 11 */

void
ClassFileWriter::analyzeROMClass()
{
	_cpHashTable = hashTableNew(OMRPORT_FROM_J9PORT(_portLibrary), "ClassFileWriter::_cpHashTable", _romClass->classFileCPCount, sizeof(HashTableEntry), 0, 0, J9MEM_CATEGORY_JVMTI, hashFunction, equalFunction, NULL, NULL);
	if (NULL == _cpHashTable) {
		_buildResult = OutOfMemory;
		return;
	}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	if (J9_ARE_ALL_BITS_SET(_romClass->optionalFlags, J9_ROMCLASS_OPTINFO_INJECTED_INTERFACE_INFO)) {
		_numOfInjectedInterfaces = getNumberOfInjectedInterfaces(_romClass);
	}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

	/* Walk ROM class adding hashtable entries for all referenced UTF8s and NASs, with index=0 */
	analyzeConstantPool();
	analyzeInterfaces();
	analyzeFields();
	analyzeMethods();
	addClassEntry(J9ROMCLASS_CLASSNAME(_romClass), 0);

	/* Super class name is NULL only for java/lang/Object */
	if (NULL != J9ROMCLASS_SUPERCLASSNAME(_romClass)) {
		addClassEntry(J9ROMCLASS_SUPERCLASSNAME(_romClass), 0);
	}

	if (J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccRecord)) {
		analyzeRecordAttribute();
	}

	if (J9ROMCLASS_IS_SEALED(_romClass)) {
		addEntry((void*) &PERMITTED_SUBCLASSES, 0, CFR_CONSTANT_Utf8);

		U_32 *permittedSubclassesCountPtr = getNumberOfPermittedSubclassesPtr(_romClass);
		for (U_32 i = 0; i < *permittedSubclassesCountPtr; i++) {
			J9UTF8* permittedSubclassNameUtf8 = permittedSubclassesNameAtIndex(permittedSubclassesCountPtr, i);
			addEntry(permittedSubclassNameUtf8, 0, CFR_CONSTANT_Utf8);
		}
	}

	J9EnclosingObject * enclosingObject = getEnclosingMethodForROMClass(_javaVM, NULL, _romClass);
	J9UTF8 * genericSignature = getGenericSignatureForROMClass(_javaVM, NULL, _romClass);
	J9UTF8 * sourceFileName = getSourceFileNameForROMClass(_javaVM, NULL, _romClass);
	J9SourceDebugExtension * sourceDebugExtension = getSourceDebugExtensionForROMClass(_javaVM, NULL, _romClass);
	U_32 * annotationsData = getClassAnnotationsDataForROMClass(_romClass);
	U_32 * typeAnnotationsData = getClassTypeAnnotationsDataForROMClass(_romClass);
	J9UTF8 * outerClassName = J9ROMCLASS_OUTERCLASSNAME(_romClass);
	J9UTF8 * simpleName = getSimpleNameForROMClass(_javaVM, NULL, _romClass);
#if JAVA_SPEC_VERSION >= 11
	J9UTF8 *nestHost = J9ROMCLASS_NESTHOSTNAME(_romClass);
#endif /* JAVA_SPEC_VERSION >= 11 */

	/* For a local class only InnerClasses.class[i].inner_name_index is preserved as simpleName in its J9ROMClass */
	if ((0 != _romClass->innerClassCount)
	|| (0 != _romClass->enclosedInnerClassCount)
	|| J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccClassInnerClass)
	) {
		addEntry((void *) &INNER_CLASSES, 0, CFR_CONSTANT_Utf8);

		if (NULL != outerClassName) {
			addClassEntry(outerClassName, 0);
		}

		if (NULL != simpleName) {
			addEntry(simpleName, 0, CFR_CONSTANT_Utf8);
		}

		J9SRP * innerClasses = (J9SRP *) J9ROMCLASS_INNERCLASSES(_romClass);
		for (UDATA i = 0; i < _romClass->innerClassCount; i++, innerClasses++) {
			J9UTF8 * className = NNSRP_PTR_GET(innerClasses, J9UTF8 *);
			addClassEntry(className, 0);
		}
		J9SRP * enclosedInnerClasses = (J9SRP *) J9ROMCLASS_ENCLOSEDINNERCLASSES(_romClass);
		for (UDATA i = 0; i < _romClass->enclosedInnerClassCount; i++, enclosedInnerClasses++) {
			J9UTF8 * className = NNSRP_PTR_GET(enclosedInnerClasses, J9UTF8 *);
			addClassEntry(className, 0);
		}
	}

#if JAVA_SPEC_VERSION >= 11
	/* Class can not have both a nest members and nest host attribute */
	if (0 != _romClass->nestMemberCount) {
		U_16 nestMemberCount = _romClass->nestMemberCount;
		J9SRP *nestMembers = (J9SRP *) J9ROMCLASS_NESTMEMBERS(_romClass);

		addEntry((void *) &NEST_MEMBERS, 0, CFR_CONSTANT_Utf8);
		for (U_16 i = 0; i < nestMemberCount; i++) {
			J9UTF8 * className = NNSRP_GET(nestMembers[i], J9UTF8 *);
			addClassEntry(className, 0);
		}
	} else if (NULL != nestHost) {
		addEntry((void *) &NEST_HOST, 0, CFR_CONSTANT_Utf8);
		addClassEntry(nestHost, 0);
	}
#endif /* JAVA_SPEC_VERSION >= 11 */

	if (NULL != enclosingObject) {
		addEntry((void *) &ENCLOSING_METHOD, 0, CFR_CONSTANT_Utf8);
		J9ROMNameAndSignature * nas = J9ENCLOSINGOBJECT_NAMEANDSIGNATURE(enclosingObject);
		if (NULL != nas) {
			addNASEntry(nas);
		}
	}

	if (NULL != genericSignature) {
		addEntry((void *) &SIGNATURE, 0, CFR_CONSTANT_Utf8);
		addEntry(genericSignature, 0, CFR_CONSTANT_Utf8);
	}

	if (NULL != sourceFileName) {
		addEntry((void *) &SOURCE_FILE, 0, CFR_CONSTANT_Utf8);
		addEntry(sourceFileName, 0, CFR_CONSTANT_Utf8);
	}

	if (NULL != sourceDebugExtension) {
		addEntry((void *) &SOURCE_DEBUG_EXTENSION, 0, CFR_CONSTANT_Utf8);
	}

	if (NULL != annotationsData) {
		addEntry((void *) &RUNTIME_VISIBLE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
	}

	if (NULL != typeAnnotationsData) {
		addEntry((void *) &RUNTIME_VISIBLE_TYPE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
	}

	if (0 != _romClass->bsmCount) {
		addEntry((void *) &BOOTSTRAP_METHODS, 0, CFR_CONSTANT_Utf8);

		U_32 bsmCount = _romClass->bsmCount;
		U_32 callSiteCount = _romClass->callSiteCount;
		J9SRP * callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(_romClass);
		U_16 * bsmIndices = (U_16 *) (callSiteData + callSiteCount);
		U_16 * bsmData = bsmIndices + callSiteCount;
		U_16 * bsmCursor = bsmData;

		for (U_32 i = 0; i < bsmCount; i++) {
			U_16 argCount = bsmCursor[1];
			bsmCursor += argCount + 2;
		}
		_bsmAttributeLength = (U_32)((bsmCursor - bsmData + 1) * sizeof(U_16));	/* +1 to account for num_bootstrap_methods */
	}

	/* Walk callSiteData and add InvokeDynamic CP entries.
	 *
	 * InvokeDynamic entries in J9ROMClass are stored as CallSite data.
	 * For every call site that refers an InvokeDynamic entry, we store its J9ROMNameAndSignature and bootstrapMethodIndex.
	 * First the SRPs to J9ROMNameAndSignature for each callsite are stored.
	 * This is followed by the bootstrapMethodIndex for each callsite.
	 * So the CallSite data consists of a block of SRPs to J9ROMNameAndSignature, followed by a block of bootstrapMethodIndices.
	 *
	 * If there are multiple call sites referring to same InvokeDynamic CP entry,
	 * then the contents get duplicated as many times.
	 *
	 * Eg: Say there are two InvokeDynamic entries in .class
	 *
	 * 		InvokeDynamic1
	 * 			bootstrap_method_attr_index1
	 * 			name_and_type_index1
	 * 		InvokeDynamic2
	 * 			bootstrap_method_attr_index2
	 * 			name_and_type_index2
	 *
	 * 	If InvokeDynamic1 is referred twice and InvokeDynamic2 is referred thrice,
	 * 	CallSite data layout in J9ROMClass will be:
	 *
	 * 	index:  0             1             2             3             4             0      1      2      3      4
	 *
	 * 	        | SRP to NAS1 | SRP to NAS1 | SRP to NAS2 | SRP to NAS2 | SRP to NAS2 | bsm1 | bsm1 | bsm2 | bsm2 | bsm2 |
	 *
	 * When adding an entry in _cpHashTable for an InvokeDynamic entry,
	 * address of its SRP to J9ROMNameAndSignature is used as key.
	 * However, we need to skip the duplicate entries.
	 * In above example, entries at index 1, 3 and 4 are duplicate and should be skipped.
	 * This is achieved by checking J9ROMNameAndSignature address of callSite being considered with the
	 * address of J9ROMNameAndSignature of the callSite last stored in _cpHashTable.
	 * If they are same, it indicates a duplicate entry and can be skipped.
	 */
	if (0 != _romClass->callSiteCount) {
		J9SRP * callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(_romClass);
		U_16 const * bsmIndices = (U_16 *) (callSiteData + _romClass->callSiteCount);
		J9ROMNameAndSignature * prevNAS = NULL;
		U_16 prevIndex = (U_16)-1; /* set to invalid value */

		for (UDATA i = 0; i < _romClass->callSiteCount; i++) {
			J9ROMNameAndSignature * nas = SRP_PTR_GET(callSiteData + i, J9ROMNameAndSignature *);
			U_16 index = bsmIndices[i];
			if ((nas != prevNAS) || (index != prevIndex)) {
				/* Both the index and the NAS need to be the same to be considered the same element */
				addEntry(callSiteData + i, 0, CFR_CONSTANT_InvokeDynamic);
				addNASEntry(nas);
				prevNAS = nas;
				prevIndex = index;
			}
		}
	}

	/* Walk the hashtable assigning indexes to all entries with index==0 */

	/* romConstantPoolCount has only one slot for Long/Double CP entries,
	 * but they occupy two slots in .class constant pool.
	 */
	U_16 doubleSlotCount = _romClass->romConstantPoolCount - _romClass->ramConstantPoolCount;
	_constantPoolCount = _romClass->romConstantPoolCount + doubleSlotCount;
	J9HashTableState hashTableState;
	HashTableEntry * entry = (HashTableEntry *) hashTableStartDo(_cpHashTable, &hashTableState);
	while (NULL != entry) {
		if (0 == entry->cpIndex) {
			entry->cpIndex = _constantPoolCount;
			_constantPoolCount += 1;
			if ((CFR_CONSTANT_Long == entry->cpType) || (CFR_CONSTANT_Double == entry->cpType)) {
				_constantPoolCount += 1;
			}
		}
		entry = (HashTableEntry *) hashTableNextDo(&hashTableState);
	}
}

void
ClassFileWriter::analyzeConstantPool()
{
	/* Walk ROM CP adding hashtable entries for all
	 * J9CPTYPE_INT, J9CPTYPE_FLOAT, J9CPTYPE_LONG, J9CPTYPE_DOUBLE CP entries, with their index,
	 * and all referenced UTF8s & NASs
	 */
	U_16 cpCount = _romClass->romConstantPoolCount;
	J9ROMConstantPoolItem * constantPool = J9_ROM_CP_FROM_ROM_CLASS(_romClass);
	U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(_romClass);

	for (U_16 i = 1; i < cpCount; i++) {
		J9ROMConstantPoolItem * cpItem = constantPool + i;
		switch (J9_CP_TYPE(cpShapeDescription, i)) {
		case J9CPTYPE_CLASS:
			addClassEntry(J9ROMCLASSREF_NAME((J9ROMClassRef *) cpItem), i);
			break;
		case J9CPTYPE_STRING:
			addEntry(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem), 0, CFR_CONSTANT_Utf8);
			break;
		case J9CPTYPE_FIELD:
			addNASEntry(J9ROMFIELDREF_NAMEANDSIGNATURE((J9ROMFieldRef *) cpItem));
			break;
		case J9CPTYPE_INSTANCE_METHOD:
		case J9CPTYPE_STATIC_METHOD:
		case J9CPTYPE_INTERFACE_METHOD:
		case J9CPTYPE_INTERFACE_STATIC_METHOD:
		case J9CPTYPE_INTERFACE_INSTANCE_METHOD:
		case J9CPTYPE_HANDLE_METHOD:
			addNASEntry(J9ROMMETHODREF_NAMEANDSIGNATURE((J9ROMMethodRef *) cpItem));
			break;
		case J9CPTYPE_METHOD_TYPE:
			addEntry(J9ROMMETHODTYPEREF_SIGNATURE((J9ROMMethodTypeRef *) cpItem), 0, CFR_CONSTANT_Utf8);
			break;
		case J9CPTYPE_METHODHANDLE:
			/* do nothing */
			break;
		case J9CPTYPE_INT:
			addEntry(&((J9ROMSingleSlotConstantRef *) cpItem)->data, i, CFR_CONSTANT_Integer);
			break;
		case J9CPTYPE_FLOAT:
			addEntry(&((J9ROMSingleSlotConstantRef *) cpItem)->data, i, CFR_CONSTANT_Float);
			break;
		case J9CPTYPE_LONG:
			addEntry(&((J9ROMConstantRef *) cpItem)->slot1, i + (i - _romClass->ramConstantPoolCount), CFR_CONSTANT_Long);
			break;
		case J9CPTYPE_DOUBLE:
			addEntry(&((J9ROMConstantRef *) cpItem)->slot1, i + (i - _romClass->ramConstantPoolCount), CFR_CONSTANT_Double);
			break;
		case J9CPTYPE_ANNOTATION_UTF8:
			addEntry(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem), i, CFR_CONSTANT_Utf8);
			break;
		case J9CPTYPE_CONSTANT_DYNAMIC:
			addNASEntry(J9ROMCONSTANTDYNAMICREF_NAMEANDSIGNATURE((J9ROMConstantDynamicRef *) cpItem));
			break;
		default:
			Trc_BCU_Assert_ShouldNeverHappen();
			break;
		}
	}
}

void
ClassFileWriter::analyzeInterfaces()
{
	J9SRP * interfaceNames = J9ROMCLASS_INTERFACES(_romClass);
	UDATA interfaceCount = _romClass->interfaceCount;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	interfaceCount -=  _numOfInjectedInterfaces;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	for (UDATA i = 0; i < interfaceCount; i++) {
		J9UTF8 * interfaceName = NNSRP_GET(interfaceNames[i], J9UTF8*);
		addClassEntry(interfaceName, 0);
	}
}

void
ClassFileWriter::analyzeFields()
{
	J9ROMFieldWalkState fieldWalkState;
	J9ROMFieldShape * fieldShape = romFieldsStartDo(_romClass, &fieldWalkState);
	while (NULL != fieldShape) {
		addEntry(J9ROMFIELDSHAPE_NAME(fieldShape), 0, CFR_CONSTANT_Utf8);
		addEntry(J9ROMFIELDSHAPE_SIGNATURE(fieldShape), 0, CFR_CONSTANT_Utf8);

		J9UTF8 * genericSignature = romFieldGenericSignature(fieldShape);
		if (NULL != genericSignature) {
			addEntry((void *) &SIGNATURE, 0, CFR_CONSTANT_Utf8);
			addEntry(genericSignature, 0, CFR_CONSTANT_Utf8);
		}
		if (J9FieldFlagHasFieldAnnotations & fieldShape->modifiers) {
			addEntry((void *) &RUNTIME_VISIBLE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}

		if (J9FieldFlagHasTypeAnnotations & fieldShape->modifiers) {
			addEntry((void *) &RUNTIME_VISIBLE_TYPE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}

		if (J9FieldFlagConstant & fieldShape->modifiers) {
			addEntry((void *) &CONSTANT_VALUE, 0, CFR_CONSTANT_Utf8);

			if (0 == (J9FieldFlagObject & fieldShape->modifiers)) {
				U_32 * value = romFieldInitialValueAddress(fieldShape);
				U_8 cpType = 0;
				switch (J9FieldTypeMask & fieldShape->modifiers) {
				case J9FieldTypeDouble:
					cpType = CFR_CONSTANT_Double;
					break;
				case J9FieldTypeLong:
					cpType = CFR_CONSTANT_Long;
					break;
				case J9FieldTypeFloat:
					cpType = CFR_CONSTANT_Float;
					break;
				default:
					cpType = CFR_CONSTANT_Integer;
					break;
				}
				addEntry(value, 0, cpType);
			}
		}

		fieldShape = romFieldsNextDo(&fieldWalkState);
	}
}

void
ClassFileWriter::analyzeMethods()
{
	J9ROMMethod * method = J9ROMCLASS_ROMMETHODS(_romClass);

	for (U_32 i = 0; i < _romClass->romMethodCount; i++) {
		addEntry(J9ROMMETHOD_NAME(method), 0, CFR_CONSTANT_Utf8);
		addEntry(J9ROMMETHOD_SIGNATURE(method), 0, CFR_CONSTANT_Utf8);

		if (J9ROMMETHOD_HAS_GENERIC_SIGNATURE(method)) {
			addEntry((void *) &SIGNATURE, 0, CFR_CONSTANT_Utf8);
			addEntry(J9_GENERIC_SIGNATURE_FROM_ROM_METHOD(method), 0, CFR_CONSTANT_Utf8);
		}
		if (0 == ((J9AccAbstract | J9AccNative) & method->modifiers)) {
			addEntry((void *) &CODE, 0, CFR_CONSTANT_Utf8);
		}
		if (J9ROMMETHOD_HAS_EXCEPTION_INFO(method)) {
			J9ExceptionInfo * exceptionInfo = J9_EXCEPTION_DATA_FROM_ROM_METHOD(method);
			if (0 != exceptionInfo->throwCount) {
				addEntry((void *) &EXCEPTIONS, 0, CFR_CONSTANT_Utf8);

				U_8 * address = (U_8 *) (exceptionInfo + 1);
				address += sizeof(J9ExceptionHandler) * exceptionInfo->catchCount;
				J9SRP * throwNames = (J9SRP *) address;

				for (U_16 i = 0; i < exceptionInfo->throwCount; i++) {
					J9UTF8 * throwName = NNSRP_GET(throwNames[i], J9UTF8 *);
					addClassEntry(throwName, 0);
				}
			}
		}
		if (J9ROMMETHOD_HAS_METHOD_PARAMETERS(method)) {
			addEntry((void *) &METHODPARAMETERS, 0, CFR_CONSTANT_Utf8);
			/*
			   J9MethodParametersData
			  __________________________
			 | U_8 methodParameterCount |
			 |__________________________|
			 |__________________________|
			 |   J9MethodParameter      |
			 |__________________________|
			 |   J9MethodParameter      |  ===========> methodParameterCount * J9MethodParameter
			 |__________________________|
			 |     .      .      .      |
			 |	   .	  .      .      |
			 |__________________________|

			 struct J9MethodParameter
			 ---------------------------------------
			 |J9SRP name (J9SRP -> J9UTF8) |
			 |U_16	flags                           |
			 ---------------------------------------
			 */
			J9MethodParametersData * methodParametersData = methodParametersFromROMMethod(method);
			J9MethodParameter * parameters = &methodParametersData->parameters;
			for (U_8 i = 0; i < methodParametersData->parameterCount; i++) {
				J9UTF8 * parameterNameUTF8 = SRP_GET(parameters[i].name, J9UTF8 *);
				if (NULL != parameterNameUTF8) {
					addEntry(parameterNameUTF8, 0, CFR_CONSTANT_Utf8);
				}
			}
		}
		if (J9ROMMETHOD_HAS_ANNOTATIONS_DATA(method)) {
			addEntry((void *) &RUNTIME_VISIBLE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}
		if (J9ROMMETHOD_HAS_PARAMETER_ANNOTATIONS(method)) {
			addEntry((void *) &RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}
		U_32 extendedModifiers = getExtendedModifiersDataFromROMMethod(method);
		if (J9ROMMETHOD_HAS_METHOD_TYPE_ANNOTATIONS(extendedModifiers) || J9ROMMETHOD_HAS_CODE_TYPE_ANNOTATIONS(extendedModifiers)) {
			addEntry((void *) &RUNTIME_VISIBLE_TYPE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}
		if (J9ROMMETHOD_HAS_DEFAULT_ANNOTATION(method)) {
			addEntry((void *) &ANNOTATION_DEFAULT, 0, CFR_CONSTANT_Utf8);
		}
		if (J9ROMMETHOD_HAS_STACK_MAP(method)) {
			addEntry((void *) &STACK_MAP_TABLE, 0, CFR_CONSTANT_Utf8);
		}

		J9MethodDebugInfo* debugInfo = getMethodDebugInfoFromROMMethod(method);
		if (NULL != debugInfo) {
			if (0 != debugInfo->lineNumberCount) {
				addEntry((void *) &LINE_NUMBER_TABLE, 0, CFR_CONSTANT_Utf8);
			}

			if (0 != debugInfo->varInfoCount) {
				addEntry((void *) &LOCAL_VARIABLE_TABLE, 0, CFR_CONSTANT_Utf8);

				J9VariableInfoWalkState state;
				J9VariableInfoValues *values = NULL;
				bool typeTable = false;

				values = variableInfoStartDo(debugInfo, &state);
				while(NULL != values) {
					addEntry(values->name, 0, CFR_CONSTANT_Utf8);
					addEntry(values->signature, 0, CFR_CONSTANT_Utf8);

					if (NULL != values->genericSignature) {
						/* If any of the values has 'genericSignature' non-null, then it has LocalVariableTypeTable */
						typeTable = true;
						addEntry(values->genericSignature, 0, CFR_CONSTANT_Utf8);
					}
					values = variableInfoNextDo(&state);
				}
				if (typeTable) {
					addEntry((void *) &LOCAL_VARIABLE_TYPE_TABLE, 0, CFR_CONSTANT_Utf8);
				}
			}
		}
		method = nextROMMethod(method);
	}
}

void
ClassFileWriter::analyzeRecordAttribute()
{
	addEntry((void *) &RECORD, 0, CFR_CONSTANT_Utf8);

	/* first 4 bytes contains number of record components */
	U_32 numberOfRecords = getNumberOfRecordComponents(_romClass);
	J9ROMRecordComponentShape* recordComponent = recordComponentStartDo(_romClass);
	for (U_32 i = 0; i < numberOfRecords; i++) {

		/* record component name and signature */
		addEntry(J9ROMRECORDCOMPONENTSHAPE_NAME(recordComponent), 0, CFR_CONSTANT_Utf8);
		addEntry(J9ROMRECORDCOMPONENTSHAPE_SIGNATURE(recordComponent), 0, CFR_CONSTANT_Utf8);

		/* analyze attributes */
		if (recordComponentHasSignature(recordComponent)) {
			J9UTF8* genericSignature = getRecordComponentGenericSignature(recordComponent);
			addEntry((void *) &SIGNATURE, 0, CFR_CONSTANT_Utf8);
			addEntry(genericSignature, 0, CFR_CONSTANT_Utf8);
		}
		if (recordComponentHasAnnotations(recordComponent)) {
			addEntry((void *) &RUNTIME_VISIBLE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}
		if (recordComponentHasTypeAnnotations(recordComponent)) {
			addEntry((void *) &RUNTIME_VISIBLE_TYPE_ANNOTATIONS, 0, CFR_CONSTANT_Utf8);
		}

		recordComponent = recordComponentNextDo(recordComponent);
	}
}

void
ClassFileWriter::writeClassFile()
{
	writeU32(CFR_MAGIC);
	writeU16(_romClass->minorVersion);
	writeU16(_romClass->majorVersion);
	writeConstantPool();
	writeU16(_romClass->modifiers & CFR_CLASS_ACCESS_MASK);
	writeU16(indexForClass(J9ROMCLASS_CLASSNAME(_romClass)));
	/* Super class name is NULL only for java/lang/Object */
	if (NULL != J9ROMCLASS_SUPERCLASSNAME(_romClass)) {
		writeU16(indexForClass(J9ROMCLASS_SUPERCLASSNAME(_romClass)));
	} else {
		writeU16(0);
	}
	writeInterfaces();
	writeFields();
	writeMethods();
	writeAttributes();
}

void
ClassFileWriter::writeConstantPool()
{
	U_16 cpCount = _romClass->romConstantPoolCount;
	J9ROMConstantPoolItem * constantPool = J9_ROM_CP_FROM_ROM_CLASS(_romClass);
	U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(_romClass);

	writeU16(_constantPoolCount);

	/* Expand all ROM CP entries into .class file CP entries */
	for (U_16 i = 1; i < cpCount; i++) {
		J9ROMConstantPoolItem * cpItem = constantPool + i;

		switch (J9_CP_TYPE(cpShapeDescription, i)) {
		case J9CPTYPE_CLASS:
			writeU8(CFR_CONSTANT_Class);
			writeU16(indexForUTF8(J9ROMCLASSREF_NAME((J9ROMClassRef *) cpItem)));
			break;
		case J9CPTYPE_STRING:
			writeU8(CFR_CONSTANT_String);
			writeU16(indexForUTF8(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem)));
			break;
		case J9CPTYPE_FIELD:
			writeU8(CFR_CONSTANT_Fieldref);
			writeU16(U_16(((J9ROMFieldRef *) cpItem)->classRefCPIndex));
			writeU16(indexForNAS(J9ROMFIELDREF_NAMEANDSIGNATURE((J9ROMFieldRef *) cpItem)));
			break;
		case J9CPTYPE_INSTANCE_METHOD:
		case J9CPTYPE_STATIC_METHOD:
		case J9CPTYPE_HANDLE_METHOD:
			writeU8(CFR_CONSTANT_Methodref);
			writeU16(U_16(((J9ROMMethodRef *) cpItem)->classRefCPIndex));
			writeU16(indexForNAS(J9ROMMETHODREF_NAMEANDSIGNATURE((J9ROMMethodRef *) cpItem)));
			break;
		case J9CPTYPE_INTERFACE_METHOD:
		case J9CPTYPE_INTERFACE_STATIC_METHOD:
		case J9CPTYPE_INTERFACE_INSTANCE_METHOD:
			writeU8(CFR_CONSTANT_InterfaceMethodref);
			writeU16(U_16(((J9ROMMethodRef *) cpItem)->classRefCPIndex));
			writeU16(indexForNAS(J9ROMMETHODREF_NAMEANDSIGNATURE((J9ROMMethodRef *) cpItem)));
			break;
		case J9CPTYPE_METHOD_TYPE:
			writeU8(CFR_CONSTANT_MethodType);
			writeU16(indexForUTF8(J9ROMMETHODTYPEREF_SIGNATURE((J9ROMMethodTypeRef *) cpItem)));
			break;
		case J9CPTYPE_METHODHANDLE:
			writeU8(CFR_CONSTANT_MethodHandle);
			writeU8(U_8(((J9ROMMethodHandleRef *) cpItem)->handleTypeAndCpType >> BCT_J9DescriptionCpTypeShift));
			writeU16(U_16(((J9ROMMethodHandleRef *) cpItem)->methodOrFieldRefIndex));
			break;
		case J9CPTYPE_INT:
			writeU8(CFR_CONSTANT_Integer);
			writeU32(((J9ROMSingleSlotConstantRef *) cpItem)->data);
			break;
		case J9CPTYPE_FLOAT:
			writeU8(CFR_CONSTANT_Float);
			writeU32(((J9ROMSingleSlotConstantRef *) cpItem)->data);
			break;
		case J9CPTYPE_LONG:
			writeU8(CFR_CONSTANT_Long);
#ifdef J9VM_ENV_LITTLE_ENDIAN
			writeU32(((J9ROMConstantRef *) cpItem)->slot2);
			writeU32(((J9ROMConstantRef *) cpItem)->slot1);
#else /* J9VM_ENV_LITTLE_ENDIAN */
			writeU32(((J9ROMConstantRef *) cpItem)->slot1);
			writeU32(((J9ROMConstantRef *) cpItem)->slot2);
#endif /* J9VM_ENV_LITTLE_ENDIAN */
			break;
		case J9CPTYPE_DOUBLE:
			writeU8(CFR_CONSTANT_Double);
#ifdef J9VM_ENV_LITTLE_ENDIAN
			writeU32(((J9ROMConstantRef *) cpItem)->slot2);
			writeU32(((J9ROMConstantRef *) cpItem)->slot1);
#else /* J9VM_ENV_LITTLE_ENDIAN */
			writeU32(((J9ROMConstantRef *) cpItem)->slot1);
			writeU32(((J9ROMConstantRef *) cpItem)->slot2);
#endif /* J9VM_ENV_LITTLE_ENDIAN */
			break;
		case J9CPTYPE_ANNOTATION_UTF8:
			writeU8(CFR_CONSTANT_Utf8);
			writeU16(J9UTF8_LENGTH(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem)));
			writeData(J9UTF8_LENGTH(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem)), J9UTF8_DATA(J9ROMSTRINGREF_UTF8DATA((J9ROMStringRef *) cpItem)));
			break;
		case J9CPTYPE_CONSTANT_DYNAMIC:
			writeU8(CFR_CONSTANT_Dynamic);
			writeU16(U_16((((J9ROMConstantDynamicRef *) cpItem)->bsmIndexAndCpType >> J9DescriptionCpTypeShift) & J9DescriptionCpBsmIndexMask));
			writeU16(indexForNAS(J9ROMCONSTANTDYNAMICREF_NAMEANDSIGNATURE((J9ROMConstantDynamicRef *) cpItem)));
			break;
		default:
			Trc_BCU_Assert_ShouldNeverHappen();
			break;
		}
	}

	U_16 doubleSlotCount = _romClass->romConstantPoolCount - _romClass->ramConstantPoolCount;
	/* Walk the hashtable and write CP entries for all hashtable entries with index >= _romClass->romConstantPoolCount */
	J9HashTableState hashTableState;
	HashTableEntry * entry = (HashTableEntry *) hashTableStartDo(_cpHashTable, &hashTableState);
	while (NULL != entry) {
		if ((_romClass->romConstantPoolCount + doubleSlotCount) <= entry->cpIndex) {
			writeU8(entry->cpType);

			switch (entry->cpType) {
			case CFR_CONSTANT_Utf8:
			{
				J9UTF8 * utf8 = (J9UTF8 *) entry->address;
				/* If this is an anonClass, we must build the classData with the original name, 
				 * not the anonClass name. There is only one copy of the className in the ROMClass,
				 * so replace the matching anonClassName reference with the originalClassName reference  
				 */
				if (_isAnon) {
					if (utf8 == _anonClassName) {
						utf8 = _originalClassName;
					}
				}
				writeU16(J9UTF8_LENGTH(utf8));
				writeData(J9UTF8_LENGTH(utf8), J9UTF8_DATA(utf8));
				break;
			}
			case CFR_CONSTANT_Class:
				writeU16(indexForUTF8((J9UTF8 *) entry->address));
				break;
			case CFR_CONSTANT_Double: /* fall through */
			case CFR_CONSTANT_Long:
#ifdef J9VM_ENV_LITTLE_ENDIAN
				writeU32(((U_32 *) entry->address)[1]);
				writeU32(((U_32 *) entry->address)[0]);
#else /* J9VM_ENV_LITTLE_ENDIAN */
				writeU32(((U_32 *) entry->address)[0]);
				writeU32(((U_32 *) entry->address)[1]);
#endif /* J9VM_ENV_LITTLE_ENDIAN */
				break;
			case CFR_CONSTANT_Float: /* fall through */
			case CFR_CONSTANT_Integer:
				writeU32(((U_32 *) entry->address)[0]);
				break;
			case CFR_CONSTANT_NameAndType: {
				J9ROMNameAndSignature * nas = (J9ROMNameAndSignature *) entry->address;
				writeU16(indexForUTF8(J9ROMNAMEANDSIGNATURE_NAME(nas)));
				writeU16(indexForUTF8(J9ROMNAMEANDSIGNATURE_SIGNATURE(nas)));
				break;
			}
			case CFR_CONSTANT_InvokeDynamic: {
				J9SRP * callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(_romClass);
				U_16 * bsmIndices = (U_16 *) (callSiteData + _romClass->callSiteCount);
				J9SRP * srp = (J9SRP *) entry->address;
				UDATA index(srp - callSiteData);
				U_16 bsmIndex = bsmIndices[index];
				J9ROMNameAndSignature * nas = SRP_PTR_GET(srp, J9ROMNameAndSignature *);

				writeU16(bsmIndex);
				writeU16(indexForNAS(nas));
				break;
			}
			default:
				Trc_BCU_Assert_ShouldNeverHappen();
				break;
			}
		}
		entry = (HashTableEntry *) hashTableNextDo(&hashTableState);
	}
}

void
ClassFileWriter::writeInterfaces()
{
	J9SRP * interfaceNames = J9ROMCLASS_INTERFACES(_romClass);
	UDATA interfaceCount = _romClass->interfaceCount;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	interfaceCount -= _numOfInjectedInterfaces;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

	writeU16(U_16(interfaceCount));
	for (UDATA i = 0; i < interfaceCount; i++) {
		J9UTF8 * interfaceName = NNSRP_GET(interfaceNames[i], J9UTF8 *);
		writeU16(indexForClass(interfaceName));
	}
}

void
ClassFileWriter::writeField(J9ROMFieldShape * fieldShape)
{
	J9UTF8 * name = J9ROMFIELDSHAPE_NAME(fieldShape);
	J9UTF8 * signature = J9ROMFIELDSHAPE_SIGNATURE(fieldShape);
	J9UTF8 * genericSignature = romFieldGenericSignature(fieldShape);
	U_32 * annotationsData = getFieldAnnotationsDataFromROMField(fieldShape);
	U_32 * typeAnnotationsData = getFieldTypeAnnotationsDataFromROMField(fieldShape);
	U_16 attributesCount = 0;

	if (J9FieldFlagConstant & fieldShape->modifiers) {
		attributesCount += 1;
	}
	if (NULL != genericSignature) {
		attributesCount += 1;
	}
	if (NULL != annotationsData) {
		attributesCount += 1;
	}
	if (NULL != typeAnnotationsData) {
		attributesCount += 1;
	}

	writeU16(U_16(fieldShape->modifiers & CFR_FIELD_ACCESS_MASK));
	writeU16(indexForUTF8(name));
	writeU16(indexForUTF8(signature));
	writeU16(attributesCount);

	if (J9FieldFlagConstant & fieldShape->modifiers) {
		U_32 * value = romFieldInitialValueAddress(fieldShape);
		U_16 index = 0;
		if (J9FieldFlagObject & fieldShape->modifiers) {
			index = *value;
		} else {
			switch (J9FieldTypeMask & fieldShape->modifiers) {
			case J9FieldTypeDouble:
				index = indexForDouble(value);
				break;
			case J9FieldTypeLong:
				index = indexForLong(value);
				break;
			case J9FieldTypeFloat:
				index = indexForFloat(value);
				break;
			default:
				index = indexForInteger(value);
				break;
			}
		}
		writeAttributeHeader((J9UTF8 *) &CONSTANT_VALUE, 2);
		writeU16(index);
	}
	if (NULL != genericSignature) {
		writeSignatureAttribute(genericSignature);
	}
	if (NULL != annotationsData) {
		writeAnnotationsAttribute(annotationsData);
	}
	if (NULL != typeAnnotationsData) {
		writeTypeAnnotationsAttribute(typeAnnotationsData);
	}
}

void
ClassFileWriter::writeFields()
{
	writeU16(U_16(_romClass->romFieldCount));

	J9ROMFieldWalkState state;
	J9ROMFieldShape * fieldShape = romFieldsStartDo(_romClass, &state);
	while (NULL != fieldShape) {
		writeField(fieldShape);
		fieldShape = romFieldsNextDo(&state);
	}
}

void
ClassFileWriter::writeMethod(J9ROMMethod * method)
{
	J9UTF8 * name = J9ROMMETHOD_NAME(method);
	J9UTF8 * signature = J9ROMMETHOD_SIGNATURE(method);
	J9UTF8 * genericSignature = J9_GENERIC_SIGNATURE_FROM_ROM_METHOD(method);
	U_32 * defaultAnnotationsData = getDefaultAnnotationDataFromROMMethod(method);
	U_32 * parameterAnnotationsData = getParameterAnnotationsDataFromROMMethod(method);
	U_32 * typeAnnotationsData = getMethodTypeAnnotationsDataFromROMMethod(method);
	U_32 * annotationsData = getMethodAnnotationsDataFromROMMethod(method);
	J9MethodParametersData * methodParametersData = getMethodParametersFromROMMethod(method);
	
	U_16 attributesCount = 0;

	/* native or abstract methods don't have Code attribute */
	if (0 == ((J9AccAbstract | J9AccNative) & method->modifiers)) {
		attributesCount += 1;
	}
	if (NULL != genericSignature) {
		attributesCount += 1;
	}
	if (J9ROMMETHOD_HAS_EXCEPTION_INFO(method)) {
		J9ExceptionInfo * exceptionInfo = J9_EXCEPTION_DATA_FROM_ROM_METHOD(method);
		if (0 != exceptionInfo->throwCount) {
			attributesCount += 1;
		}
	}
	if (NULL != annotationsData) {
		attributesCount += 1;
	}
	if (NULL != parameterAnnotationsData) {
		attributesCount += 1;
	}
	if (NULL != typeAnnotationsData) {
		attributesCount += 1;
	}
	if (NULL != defaultAnnotationsData) {
		attributesCount += 1;
	}
	if (NULL != methodParametersData) {
		attributesCount += 1;
	}

	writeU16(U_16(method->modifiers & CFR_METHOD_ACCESS_MASK));
	writeU16(indexForUTF8(name));
	writeU16(indexForUTF8(signature));
	writeU16(attributesCount);

	if (0 == ((J9AccAbstract | J9AccNative) & method->modifiers)) {
		writeCodeAttribute(method);
	}
	if (NULL != genericSignature) {
		writeSignatureAttribute(genericSignature);
	}
	if (J9ROMMETHOD_HAS_EXCEPTION_INFO(method)) {
		J9ExceptionInfo * exceptionInfo = J9_EXCEPTION_DATA_FROM_ROM_METHOD(method);
		if (0 != exceptionInfo->throwCount) {
			U_8 * address = (U_8 *) (exceptionInfo + 1);
			address += sizeof(J9ExceptionHandler) * exceptionInfo->catchCount;
			J9SRP * throwNames = (J9SRP *) address;

			writeAttributeHeader((J9UTF8 *) &EXCEPTIONS, sizeof(U_16) + (sizeof(U_16) * exceptionInfo->throwCount));
			writeU16(exceptionInfo->throwCount);
			for (U_16 i = 0; i < exceptionInfo->throwCount; i++) {
				J9UTF8 * throwName = NNSRP_GET(throwNames[i], J9UTF8 *);
				writeU16(indexForClass(throwName));
			}
		}
	}
	if (NULL != annotationsData) {
		writeAnnotationsAttribute(annotationsData);
	}
	if (NULL != parameterAnnotationsData) {
		writeParameterAnnotationsAttribute(parameterAnnotationsData);
	}
	if (NULL != defaultAnnotationsData) {
		writeAnnotationDefaultAttribute(defaultAnnotationsData);
	}
	if (NULL != typeAnnotationsData) {
		writeTypeAnnotationsAttribute(typeAnnotationsData);
	}

	if (NULL != methodParametersData) {
		U_8 parameterCount = methodParametersData->parameterCount;
		J9MethodParameter * parameters = &methodParametersData->parameters;
		writeAttributeHeader((J9UTF8 *) &METHODPARAMETERS, sizeof(U_8) + ((sizeof(U_16)+sizeof(U_16)) * parameterCount));
		
		writeU8(parameterCount);
		for (U_8 i = 0; i < parameterCount; i++) {
			U_16 utfIndex = 0;
			J9UTF8 * parameterName = SRP_GET(parameters[i].name, J9UTF8 *);
			if (NULL != parameterName) {
				utfIndex = indexForUTF8(parameterName);
			}
			writeU16(utfIndex);
			writeU16(parameters[i].flags);
		}
	}	
}

void
ClassFileWriter::writeMethods()
{
	writeU16(U_16(_romClass->romMethodCount));
	J9ROMMethod * method = J9ROMCLASS_ROMMETHODS(_romClass);
	for (U_32 i = 0; i < _romClass->romMethodCount; i++) {
		writeMethod(method);
		method = nextROMMethod(method);
	}
}

void
ClassFileWriter::writeAttributes()
{
	U_16 attributesCount = 0;
	J9UTF8 * outerClassName = J9ROMCLASS_OUTERCLASSNAME(_romClass);
	J9UTF8 * simpleName = getSimpleNameForROMClass(_javaVM, NULL, _romClass);
	J9EnclosingObject * enclosingObject = getEnclosingMethodForROMClass(_javaVM, NULL, _romClass);
	J9UTF8 * signature = getGenericSignatureForROMClass(_javaVM, NULL, _romClass);
	J9UTF8 * sourceFileName = getSourceFileNameForROMClass(_javaVM, NULL, _romClass);
	J9SourceDebugExtension * sourceDebugExtension = getSourceDebugExtensionForROMClass(_javaVM, NULL, _romClass);
	U_32 * annotationsData = getClassAnnotationsDataForROMClass(_romClass);
	U_32 * typeAnnotationsData = getClassTypeAnnotationsDataForROMClass(_romClass);
#if JAVA_SPEC_VERSION >= 11
	J9UTF8 *nestHost = J9ROMCLASS_NESTHOSTNAME(_romClass);
	U_16 nestMemberCount = _romClass->nestMemberCount;
#endif /* JAVA_SPEC_VERSION >= 11 */

	if ((0 != _romClass->innerClassCount)
	|| (0 != _romClass->enclosedInnerClassCount)
	|| J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccClassInnerClass)
	) {
		attributesCount += 1;
	}
	if (NULL != enclosingObject) {
		attributesCount += 1;
	}
	if (NULL != signature) {
		attributesCount += 1;
	}
	if (NULL != sourceFileName) {
		attributesCount += 1;
	}
	if (NULL != sourceDebugExtension) {
		attributesCount += 1;
	}
	if (NULL != annotationsData) {
		attributesCount += 1;
	}
	if (NULL != typeAnnotationsData) {
		attributesCount += 1;
	}
	if (0 != _romClass->bsmCount) {
		attributesCount += 1;
	}
	if (J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccRecord)) {
		attributesCount += 1;
	}
	if (J9ROMCLASS_IS_SEALED(_romClass)) {
		attributesCount += 1;
	}
#if JAVA_SPEC_VERSION >= 11
	/* Class can not have both a nest members and member of nest attribute */
	if ((0 != _romClass->nestMemberCount) || (NULL != nestHost)) {
		attributesCount += 1;
	}
#endif /* JAVA_SPEC_VERSION >= 11 */
	writeU16(attributesCount);

	if ((0 != _romClass->innerClassCount)
	|| (0 != _romClass->enclosedInnerClassCount)
	|| J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccClassInnerClass)
	) {
		U_16 innerClassesCount(_romClass->innerClassCount + _romClass->enclosedInnerClassCount);

		if (J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccClassInnerClass)) {
			/* This is an inner class, so we have one extra inner class attribute to allocate/write */
			innerClassesCount++;
		}

		/* Calculate and write size of inner class attributes */
		U_32 innerClassesSize = innerClassesCount * 4 * sizeof(U_16);
		innerClassesSize += sizeof(U_16); /* innerClassesCount */
		writeAttributeHeader((J9UTF8 *) &INNER_CLASSES, innerClassesSize);
		writeU16(innerClassesCount);

		U_16 thisClassCPIndex = indexForClass(J9ROMCLASS_CLASSNAME(_romClass));

		/* Write inner classes of this class */
		J9SRP * innerClasses = (J9SRP *) J9ROMCLASS_INNERCLASSES(_romClass);
		/* Write inner class entries for inner classes of this class */
		for (UDATA i = 0; i < _romClass->innerClassCount; i++, innerClasses++) {
			J9UTF8 * innerClassName = NNSRP_PTR_GET(innerClasses, J9UTF8 *);

			writeU16(indexForClass(innerClassName));
			writeU16(thisClassCPIndex);

			/* NOTE: innerClassAccessFlags and innerNameIndex are not preserved in the ROM class - technically incorrect, but this should only matter to compilers */
			writeU16(0); /* innerNameIndex */
			writeU16(0); /* innerClassAccessFlags */
		}

		/* Write enclosed inner classes of this class */
		J9SRP * enclosedInnerClasses = (J9SRP *) J9ROMCLASS_ENCLOSEDINNERCLASSES(_romClass);
		/* Write the enclosed inner class entries for inner classes of this class */
		for (UDATA i = 0; i < _romClass->enclosedInnerClassCount; i++, enclosedInnerClasses++) {
			J9UTF8 * enclosedInnerClassName = NNSRP_PTR_GET(enclosedInnerClasses, J9UTF8 *);

			writeU16(indexForClass(enclosedInnerClassName));
			/* NOTE: outerClassInfoIndex (these inner class are not the declared classes of this class),
			 * innerNameIndex and innerClassAccessFlags are not preserved in the ROM class
			 * - technically incorrect, but this should only matter to compilers.
			 */
			writeU16(0); /* outerClassInfoIndex */
			writeU16(0); /* innerNameIndex */
			writeU16(0); /* innerClassAccessFlags */
		}

		if (J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccClassInnerClass)) {
			/* This is an inner class. Write an inner class attribute for itself. */
			writeU16(thisClassCPIndex);
			writeU16((NULL == outerClassName) ? 0 : indexForClass(outerClassName));
			writeU16((NULL == simpleName) ? 0 : indexForUTF8(simpleName));
			writeU16(_romClass->memberAccessFlags);
		}
	}

#if JAVA_SPEC_VERSION >= 11
	/* A class can only have one of the nest mate attributes */
	if (0 != nestMemberCount) {
		J9SRP *nestMembers = (J9SRP *) J9ROMCLASS_NESTMEMBERS(_romClass);
		U_32 nestMembersAttributeSize = (1 + nestMemberCount) * sizeof(U_16);
		writeAttributeHeader((J9UTF8 *) &NEST_MEMBERS, nestMembersAttributeSize);
		writeU16(nestMemberCount);

		for (U_16 i = 0; i < nestMemberCount; i++) {
			J9UTF8 * nestMemberName = NNSRP_PTR_GET(nestMembers, J9UTF8 *);
			writeU16(indexForClass(nestMemberName));
			nestMembers += 1;
		}
	} else if (NULL != nestHost) {
		writeAttributeHeader((J9UTF8 *) &NEST_HOST, 2);
		writeU16(indexForClass(nestHost));
	}
#endif /* JAVA_SPEC_VERSION >= 11 */

	if (NULL != enclosingObject) {
		J9ROMNameAndSignature * nas = J9ENCLOSINGOBJECT_NAMEANDSIGNATURE(enclosingObject);

		writeAttributeHeader((J9UTF8 *) &ENCLOSING_METHOD, 4);
		writeU16(U_16(enclosingObject->classRefCPIndex));
		writeU16(NULL == nas ? 0 : indexForNAS(nas));
	}

	if (NULL != signature) {
		writeSignatureAttribute(signature);
	}

	if (NULL != sourceFileName) {
		writeAttributeHeader((J9UTF8 *) &SOURCE_FILE, 2);
		writeU16(indexForUTF8(sourceFileName));
	}

	if (NULL != sourceDebugExtension) {
		writeAttributeHeader((J9UTF8 *) &SOURCE_DEBUG_EXTENSION, sourceDebugExtension->size);
		writeData(sourceDebugExtension->size, sourceDebugExtension + 1);
	}

	if (NULL != annotationsData) {
		writeAnnotationsAttribute(annotationsData);
	}

	if (NULL != typeAnnotationsData) {
		writeTypeAnnotationsAttribute(typeAnnotationsData);
	}

	if (0 != _romClass->bsmCount) {
		U_32 bsmCount = _romClass->bsmCount;
		U_32 callSiteCount = _romClass->callSiteCount;
		J9SRP * callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(_romClass);
		U_16 * bsmIndices = (U_16 *) (callSiteData + callSiteCount);
		U_16 * bsmCursor = bsmIndices + callSiteCount;

		writeAttributeHeader((J9UTF8 *) &BOOTSTRAP_METHODS, _bsmAttributeLength);
		writeU16(U_16(bsmCount));	/* num_bootstrap_methods */
		for (U_32 i = 0; i < bsmCount; i++) {
			writeU16(*bsmCursor);	/* bootstrap_method_ref */
			bsmCursor += 1;
			U_16 argCount = *bsmCursor;
			writeU16(argCount);		/* num_bootstrap_arguments */
			bsmCursor += 1;
			for (U_16 j = 0; j < argCount; j++) {
				U_16 cpIndex = *bsmCursor;
				
				if (cpIndex >= _romClass->ramConstantPoolCount) {
					/* Adjust double slot entry index */
					cpIndex = cpIndex + (cpIndex - _romClass->ramConstantPoolCount);
				}
				writeU16(cpIndex);
				bsmCursor += 1;
			}
		}
	}

	/* record attribute */
	if (J9_ARE_ANY_BITS_SET(_romClass->extraModifiers, J9AccRecord)) {
		writeRecordAttribute();
	}

	/* write PermittedSubclasses attribute */
	if (J9ROMCLASS_IS_SEALED(_romClass)) {
		U_32 *permittedSubclassesCountPtr = getNumberOfPermittedSubclassesPtr(_romClass);
		writeAttributeHeader((J9UTF8 *) &PERMITTED_SUBCLASSES, sizeof(U_16) + (*permittedSubclassesCountPtr * sizeof(U_16)));

		writeU16(*permittedSubclassesCountPtr);

		for (U_32 i = 0; i < *permittedSubclassesCountPtr; i++) {
			J9UTF8* permittedSubclassNameUtf8 = permittedSubclassesNameAtIndex(permittedSubclassesCountPtr, i);

			/* CONSTANT_Class_info index should be written. Find class entry that references the subclass name in constant pool. */
			J9HashTableState hashTableState;
			HashTableEntry * entry = (HashTableEntry *) hashTableStartDo(_cpHashTable, &hashTableState);
			while (NULL != entry) {
				if (CFR_CONSTANT_Class == entry->cpType) {
					J9UTF8* classNameCandidate = (J9UTF8*)entry->address;
					if (J9UTF8_EQUALS(classNameCandidate, permittedSubclassNameUtf8)) {
						writeU16(entry->cpIndex);
						break;
					}
				}
				entry = (HashTableEntry *) hashTableNextDo(&hashTableState);
			}
		}
	}
}

void ClassFileWriter::writeRecordAttribute()
{
	/* Write header - size calculation will be written specially at the end.
	 * Write zero as a placeholder for length.
	 */
	writeU16(indexForUTF8((J9UTF8 *) &RECORD));
	U_8* recordAttributeLengthAddr = _classFileCursor;
	writeU32(0);
	U_8* startLengthCalculationAddr = _classFileCursor;
	
	/* write number of record components (components_count).
	 * Stored as U32 in the ROM class, U16 in class file.
	 */
	U_32 numberOfRecords = getNumberOfRecordComponents(_romClass);
	writeU16(numberOfRecords);

	/* write record components */
	J9ROMRecordComponentShape* recordComponent = recordComponentStartDo(_romClass);
	for (U_32 i = 0; i < numberOfRecords; i++) {
		J9UTF8 * name = J9ROMRECORDCOMPONENTSHAPE_NAME(recordComponent);
		J9UTF8 * signature = J9ROMRECORDCOMPONENTSHAPE_SIGNATURE(recordComponent);
		J9UTF8 * genericSignature = getRecordComponentGenericSignature(recordComponent);
		U_32 * annotationsData = getRecordComponentAnnotationData(recordComponent);
		U_32 * typeAnnotationsData = getRecordComponentTypeAnnotationData(recordComponent);
		U_16 attributesCount = 0;

		writeU16(indexForUTF8(name));
		writeU16(indexForUTF8(signature));

		if (NULL != genericSignature) {
			attributesCount += 1;
		}
		if (NULL != annotationsData) {
			attributesCount += 1;
		}
		if (NULL != typeAnnotationsData) {
			attributesCount += 1;
		}
		writeU16(attributesCount);

		if (NULL != genericSignature) {
			writeSignatureAttribute(genericSignature);
		}
		if (NULL != annotationsData) {
			writeAnnotationsAttribute(annotationsData);
		}
		if (NULL != typeAnnotationsData) {
			writeTypeAnnotationsAttribute(typeAnnotationsData);
		}

		recordComponent = recordComponentNextDo(recordComponent);
	}

	/* calculate and write record attribute length */
	U_8* endLengthCalculationAddr = _classFileCursor;
	writeU32At(U_32(endLengthCalculationAddr - startLengthCalculationAddr), recordAttributeLengthAddr);
}

U_8
ClassFileWriter::computeArgsCount(U_16 methodRefIndex)
{
	J9ROMConstantPoolItem *constantPool = (J9ROMConstantPoolItem*) (_romClass + 1);
	J9ROMMethodRef *methodRef = (J9ROMMethodRef *)(constantPool + methodRefIndex);
	J9ROMNameAndSignature *nas = J9ROMMETHODREF_NAMEANDSIGNATURE(methodRef);
	J9UTF8 *sigUTF8 = J9ROMNAMEANDSIGNATURE_SIGNATURE(nas);
	U_16 count = J9UTF8_LENGTH(sigUTF8);
	U_8 *sig = J9UTF8_DATA(sigUTF8);
	U_8 argsCount = 1;	/* interface method always has 'this' parameter */
	bool done = false;

	for (U_16 index = 1; (index < count) && (!done); index++) { /* 1 to skip the opening '(' */
		switch (sig[index]) {
		case ')':
			done = true;
			break;
		case '[':
			/* skip all '['s */
			while ((index < count) && ('[' == sig[index])) {
				index += 1;
			}
			if ('L' != sig[index]) {
				break;
			}
			/* fall through */
		case 'L':
			index += 1;
			while ((index < count) && (';' != sig[index])) {
				index += 1;
			}
			break;
		case 'D':
			/* fall through */
		case 'J':
			argsCount += 1; /* double occupies 2 slots */
			break;
		default:
			/* any other primitive type */
			break;
		}
		if (!done) {
			argsCount += 1;
		}
	}

	return argsCount;
}

void
ClassFileWriter::writeCodeAttribute(J9ROMMethod * method)
{
	U_32 codeLength(J9_BYTECODE_SIZE_FROM_ROM_METHOD(method));
	U_8 * code(J9_BYTECODE_START_FROM_ROM_METHOD(method));
	U_16 attributesCount = 0;

	writeU16(indexForUTF8((J9UTF8 *) &CODE));
	U_8 * attributeLenAddr = _classFileCursor;
	writeU32(0);

	U_8 * start = _classFileCursor;
	writeU16(method->maxStack);
	writeU16(method->tempCount + method->argCount);
	writeU32(codeLength);

	U_8 * bytecode = _classFileCursor;
	writeData(codeLength, code);
	rewriteBytecode(method, codeLength, bytecode);

	if (J9ROMMETHOD_HAS_EXCEPTION_INFO(method)) {
		J9ExceptionInfo * exceptionInfo = J9_EXCEPTION_DATA_FROM_ROM_METHOD(method);
		J9ExceptionHandler * handlers = (J9ExceptionHandler *) (exceptionInfo + 1);

		writeU16(exceptionInfo->catchCount);
		for (U_16 i = 0; i < exceptionInfo->catchCount; i++) {
			writeU16(U_16(handlers[i].startPC));
			writeU16(U_16(handlers[i].endPC));
			writeU16(U_16(handlers[i].handlerPC));
			writeU16(U_16(handlers[i].exceptionClassIndex));
		}
	} else {
		writeU16(0);
	}

	U_8 * attributesCountAddr = _classFileCursor;
	writeU16(0);

	if (J9ROMMETHOD_HAS_STACK_MAP(method)) {
		writeStackMapTableAttribute(method);
		attributesCount += 1;
	}

	if (J9ROMMETHOD_HAS_CODE_TYPE_ANNOTATIONS(getExtendedModifiersDataFromROMMethod(method))) {
		U_32 * typeAnnotationsData = getCodeTypeAnnotationsDataFromROMMethod(method);
		writeTypeAnnotationsAttribute(typeAnnotationsData);
		attributesCount += 1;
	}

	J9MethodDebugInfo* debugInfo = getMethodDebugInfoFromROMMethod(method);
	if (NULL != debugInfo) {
		U_16 lineNumberCount(getLineNumberCount(debugInfo));

		if (0 != lineNumberCount) {
			writeAttributeHeader((J9UTF8 *) &LINE_NUMBER_TABLE, sizeof(U_16) + (lineNumberCount * sizeof(U_16) * 2));
			writeU16(lineNumberCount);

			U_8 * lineNumberTable = getLineNumberTable(debugInfo);

			J9LineNumber lineNumber;
			/* getNextLineNumberFromTable() expects initial values in lineNumber to be 0 */
			memset(&lineNumber, 0, sizeof(J9LineNumber));

			for (U_16 i = 0; i < lineNumberCount; i++) {
				if (getNextLineNumberFromTable(&lineNumberTable, &lineNumber)) {
					writeU16(lineNumber.location);
					writeU16(lineNumber.lineNumber);
				} else {
					_buildResult = LineNumberTableDecompressFailed;
					return;
				}
			}
			attributesCount += 1;
		}

		U_16 varInfoCount(debugInfo->varInfoCount);

		if (0 != varInfoCount) {
			writeAttributeHeader((J9UTF8 *) &LOCAL_VARIABLE_TABLE, sizeof(U_16) + (sizeof(U_16) * 5 * varInfoCount));
			writeU16(varInfoCount);

			J9VariableInfoWalkState state;
			J9VariableInfoValues * values = NULL;

			U_16 varInfoWithSigCount = 0;

			values = variableInfoStartDo(debugInfo, &state);
			while(NULL != values) {
				writeU16(values->startVisibility);
				writeU16(values->visibilityLength);
				writeU16(indexForUTF8(values->name));
				writeU16(indexForUTF8(values->signature));
				writeU16(values->slotNumber);
				if (NULL != values->genericSignature) {
					varInfoWithSigCount += 1;
				}
				values = variableInfoNextDo(&state);
			}
			attributesCount += 1;

			if (0 != varInfoWithSigCount) {
				writeAttributeHeader((J9UTF8 *) &LOCAL_VARIABLE_TYPE_TABLE, sizeof(U_16) + (sizeof(U_16) * 5 * varInfoWithSigCount));
				writeU16(varInfoWithSigCount);

				values = variableInfoStartDo(debugInfo, &state);

				while(NULL != values) {
					if (NULL != values->genericSignature) {
						writeU16(values->startVisibility);
						writeU16(values->visibilityLength);
						writeU16(indexForUTF8(values->name));
						writeU16(indexForUTF8(values->genericSignature));
						writeU16(values->slotNumber);
					}
					values = variableInfoNextDo(&state);
				}
				attributesCount += 1;
			}
		}
	}

	writeU16At(attributesCount, attributesCountAddr);

	U_8 * end = _classFileCursor;
	writeU32At(U_32(end - start), attributeLenAddr);
}

void
ClassFileWriter::rewriteBytecode(J9ROMMethod * method, U_32 length, U_8 * code)
{
	/*
	 * This is derived from jvmtiGetBytecodes() in jvmtiMethod.c. It avoids CP index renumbering.
	 * It should likely be replaced with a call to jvmtiGetBytecodes() once that function is fixed for CP unsplitting.
	 */
	U_32 index = 0;
	while (index < length) {
		U_8 bc = code[index];
		U_32 bytecodeSize = J9JavaInstructionSizeAndBranchActionTable[bc] & 7;

		if (bytecodeSize == 0) {
			_buildResult = InvalidBytecodeSize;
			return;
		}

		switch (bc) {
		case JBldc: /* do nothing */
			break;

		case JBldc2dw: /* Fall-through */
		case JBldc2lw: {
				code[index] = CFR_BC_ldc2_w;
				U_16 cpIndex = *(U_16 *)(code + index + 1);
				U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(_romClass);

				if (J9CPTYPE_CONSTANT_DYNAMIC != J9_CP_TYPE(cpShapeDescription, cpIndex)) {
					/* Adjust index of double/long CP entry. Not necessary for Constant_Dynamic as
					 * its already in the RAM CP while double/long are sorted to the end.
					 */
					cpIndex = cpIndex + (cpIndex - _romClass->ramConstantPoolCount);
				}
				writeU16At(cpIndex, code + index + 1);
			}
			break;

		case JBnewdup:
			code[index] = JBnew;
			flip16bit(code + index + 1);
			break;

		case JBinvokehandle:
		case JBinvokehandlegeneric:
			code[index] = CFR_BC_invokevirtual;
			flip16bit(code + index + 1);
			break;

		case JBinvokedynamic: {
			UDATA i;
			U_16 callSiteIndex = *(U_16 *) (code + index + 1);
			J9SRP * callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(_romClass);
			U_16 *bsmIndices = (U_16 *)(callSiteData + _romClass->callSiteCount);
			J9ROMNameAndSignature * nas = SRP_PTR_GET(callSiteData + callSiteIndex, J9ROMNameAndSignature *);
			U_16 bsmIndex = bsmIndices[callSiteIndex];

			/* Scan the data to get the first entry which has same J9ROMNameAndSignature as the 'nas'.
			 * See comments in ClassFileWriter::analyzeROMClass() for the explanation.
			 */
			for (i = 0; i < _romClass->callSiteCount; i++) {
				J9ROMNameAndSignature * tempNAS = SRP_PTR_GET(callSiteData + i, J9ROMNameAndSignature *);
				U_16 tempIndex = bsmIndices[i];
				if ((nas == tempNAS) && (bsmIndex == tempIndex)) {
					break;
				}
			}
			writeU16At(indexForInvokeDynamic(callSiteData + i), code + index + 1);
			index += 2;	/* Advance past the extra nops */
			break;
		}

		case JBinvokestaticsplit: {
			U_16 cpIndex = *(U_16 *) (code + index + 1);
			/* treat cpIndex as index into static split table */
			cpIndex = *(U_16 *)(J9ROMCLASS_STATICSPLITMETHODREFINDEXES(_romClass) + cpIndex);
			/* reset bytecode to non-split version */
			*(code + index) = CFR_BC_invokestatic;
			writeU16At(cpIndex, code + index + 1);
			break;
		}

		case JBinvokespecialsplit: {
			U_16 cpIndex = *(U_16 *) (code + index + 1);
			/* treat cpIndex as index into special split table */
			cpIndex = *(U_16 *)(J9ROMCLASS_SPECIALSPLITMETHODREFINDEXES(_romClass) + cpIndex);
			/* reset bytecode to non-split version */
			*(code + index) = CFR_BC_invokespecial;
			writeU16At(cpIndex, code + index + 1);
			break;
		}

		case JBiloadw: /* Fall-through */
		case JBlloadw: /* Fall-through */
		case JBfloadw: /* Fall-through */
		case JBdloadw: /* Fall-through */
		case JBaloadw:
			bc = bc - JBiloadw + JBiload;
readdWide:
			code[index + 0] = CFR_BC_wide;
			{
				U_8 tmp = code[index + 1];
				code[index + 1] = bc;
#ifdef J9VM_ENV_LITTLE_ENDIAN
				/* code[index + 2] = code[index + 2]; */
				code[index + 3] = tmp;
#else /* J9VM_ENV_LITTLE_ENDIAN */
				code[index + 3] = code[index + 2];
				code[index + 2] = tmp;
#endif /* J9VM_ENV_LITTLE_ENDIAN */
			}
			break;

		case JBistorew: /* Fall-through */
		case JBlstorew: /* Fall-through */
		case JBfstorew: /* Fall-through */
		case JBdstorew: /* Fall-through */
		case JBastorew:
			bc = bc - JBistorew + JBistore;
			goto readdWide;

		case JBiincw:
#ifdef J9VM_ENV_LITTLE_ENDIAN
			/* code[index + 4] = code[index + 4]; */
			code[index + 5] = code[index + 3];
#else /* J9VM_ENV_LITTLE_ENDIAN */
			code[index + 5] = code[index + 4];
			code[index + 4] = code[index + 3];
#endif /* J9VM_ENV_LITTLE_ENDIAN */
			bc = JBiinc;
			goto readdWide;

		case JBgenericReturn: /* Fall-through */
		case JBreturnFromConstructor: /* Fall-through */
		case JBreturn0: /* Fall-through */
		case JBreturn1: /* Fall-through */
		case JBreturn2: /* Fall-through */
		case JBreturnB: /* Fall-through */
		case JBreturnC: /* Fall-through */
		case JBreturnS: /* Fall-through */
		case JBreturnZ: /* Fall-through */
		case JBsyncReturn0: /* Fall-through */
		case JBsyncReturn1: /* Fall-through */
		case JBsyncReturn2: {
			J9UTF8 * signature = J9ROMMETHOD_SIGNATURE(method);
			U_8 * sigData = J9UTF8_DATA(signature);
			U_16 sigLength = J9UTF8_LENGTH(signature);

			switch (sigData[sigLength - 2] == '[' ? ';' : sigData[sigLength - 1]) {
				case 'V':
					bc = CFR_BC_return;
					break;
				case 'J':
					bc = CFR_BC_lreturn;
					break;
				case 'D':
					bc = CFR_BC_dreturn;
					break;
				case 'F':
					bc = CFR_BC_freturn;
					break;
				case ';':
					bc = CFR_BC_areturn;
					break;
				default:
					bc = CFR_BC_ireturn;
					break;
			}
			code[index] = bc;
			break;
		}

		case JBaload0getfield:
			code[index] = JBaload0;
			break;

		case JBinvokeinterface2: {
			code[index + 0] = CFR_BC_invokeinterface;
			U_16 cpIndex = *(U_16 *)(code + index + 3);
			writeU16At(cpIndex, code + index + 1);
			code[index + 3] = computeArgsCount(cpIndex);
			code[index + 4] = 0;
			break;
		}

		case JBlookupswitch: /* Fall-through */
		case JBtableswitch: {
			U_32 tempIndex = index + (4 - (index & 3));
			UDATA numEntries;

			flip32bit(code + tempIndex);
			tempIndex += sizeof(U_32);

			I_32 low = *((I_32 *) (code + tempIndex));

			flip32bit(code + tempIndex);
			tempIndex += sizeof(U_32);

			if (bc == JBtableswitch) {
				I_32 high = *((I_32 *) (code + tempIndex));

				flip32bit(code + tempIndex);
				tempIndex += sizeof(U_32);
				numEntries = (UDATA) (high - low + 1);
			} else {
				numEntries = ((UDATA) low) * 2;
			}

			while (0 != numEntries) {
				flip32bit(code + tempIndex);
				tempIndex += 4;
				numEntries -= 1;
			}

			bytecodeSize = tempIndex - index;
			break;
		}

#ifdef J9VM_ENV_LITTLE_ENDIAN
		case JBgotow:
			flip32bit(code + index + 1);
			break;

		case JBiinc:
			/* Size is 3 - avoid the default endian flip case */
			break;

		default:
			/* Any bytecode of size 3 or more which does not have a single word parm first must be added to the switch */
			if (bytecodeSize >= 3) {
				flip16bit(code + index + 1);
			}
			break;
#endif /* J9VM_ENV_LITTLE_ENDIAN */
		}

		index += bytecodeSize;
	}
}

void
ClassFileWriter::writeVerificationTypeInfo(U_16 count, U_8 ** typeInfo)
{
	U_8 * cursor = *typeInfo;

	for (U_16 i = 0; i < count; i++) {
		U_8 tag;
		NEXT_U8(tag, cursor);

		switch(tag) {
		case CFR_STACKMAP_TYPE_BYTE_ARRAY:
		case CFR_STACKMAP_TYPE_BOOL_ARRAY:
		case CFR_STACKMAP_TYPE_CHAR_ARRAY:
		case CFR_STACKMAP_TYPE_DOUBLE_ARRAY:
		case CFR_STACKMAP_TYPE_FLOAT_ARRAY:
		case CFR_STACKMAP_TYPE_INT_ARRAY:
		case CFR_STACKMAP_TYPE_LONG_ARRAY:
		case CFR_STACKMAP_TYPE_SHORT_ARRAY: {
			/* convert primitive array tag to corresponding class index in constant pool */
			U_8 typeInfoTagToPrimitiveArrayCharMap[] = { 'I', 'F', 'D', 'J', 'S', 'B', 'C', 'Z' };
			U_8 primitiveChar = typeInfoTagToPrimitiveArrayCharMap[tag - CFR_STACKMAP_TYPE_INT_ARRAY];
			/* An array cannot have more than 255 dimensions as per VM spec */
			U_8 classUTF8[2 + 255 + 1];		/* represents J9UTF8 for primitive class (size = length + arity + primitiveChar) */
			U_16 arity;

			NEXT_U16(arity, cursor);
			arity += 1; /* actual arity is one more than stored in stack map table */
			*(U_16 *)classUTF8 = arity + 1;	/* store UTF8 length = arity + one char for primitive type */
			memset((void *)(classUTF8 + 2), '[', arity);
			*(classUTF8 + 2 + arity) = primitiveChar;

			/* There is no need to double-check the primitive type the in the constant pool in the case
			 * of boolean arrays because CFR_STACKMAP_TYPE_BYTE_ARRAY is only used for byte arrays and
			 * CFR_STACKMAP_TYPE_BOOL_ARRAY is used to represent boolean arrays.
			 */

			writeU8(CFR_STACKMAP_TYPE_OBJECT);
			writeU16(indexForClass((J9UTF8 *)classUTF8));
			break;
		}

		case CFR_STACKMAP_TYPE_OBJECT:
		case CFR_STACKMAP_TYPE_NEW_OBJECT: {
			U_16 data;
			NEXT_U16(data, cursor);

			writeU8(tag);
			writeU16(data);
			break;
		}

		default:
			writeU8(tag);
			break;
		}
	}

	*typeInfo = cursor;
}

void
ClassFileWriter::writeStackMapTableAttribute(J9ROMMethod * romMethod)
{
	U_16 numEntries = 0;
	U_8 * stackMap = (U_8 *)stackMapFromROMMethod(romMethod);

	writeU16(indexForUTF8((J9UTF8 *) &STACK_MAP_TABLE));
	U_8 * attributeLenAddr = _classFileCursor;
	writeU32(0);

	stackMap += sizeof(U_32);	/* skip length */

	U_8 * start = _classFileCursor;

	NEXT_U16(numEntries, stackMap);
	writeU16(numEntries);

	for (U_16 i = 0; i < numEntries; i++) {
		U_8 frameType;

		NEXT_U8(frameType, stackMap);
		writeU8(frameType);

		if (CFR_STACKMAP_SAME_LOCALS_1_STACK > frameType) { /* 0..63 */
			/* SAME frame - no extra data */
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_END > frameType) { /* 64..127 */
			/*
			 * SAME_LOCALS_1_STACK {
			 * 		TypeInfo stackItems[1]
			 * };
			 */
			writeVerificationTypeInfo(1, &stackMap);
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED > frameType) { /* 128..246 */
			/* Reserved frame types - no extra data */
			Trc_BCU_Assert_ShouldNeverHappen();
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED == frameType) { /* 247 */
			/*
			 * SAME_LOCALS_1_STACK_EXTENDED {
			 *		U_16 offsetDelta
			 *		TypeInfo stackItems[1]
			 * };
			 */
			U_16 offsetDelta;
			NEXT_U16(offsetDelta, stackMap);
			writeU16(offsetDelta);
			writeVerificationTypeInfo(1, &stackMap);
		} else if (CFR_STACKMAP_SAME_EXTENDED > frameType) { /* 248..250 */
			/*
			 * CHOP {
			 *		U_16 offsetDelta
			 * };
			 */
			U_16 offsetDelta;
			NEXT_U16(offsetDelta, stackMap);
			writeU16(offsetDelta);
		} else if (CFR_STACKMAP_SAME_EXTENDED == frameType) { /* 251 */
			/*
			 * SAME_EXTENDED {
			 *		U_16 offsetDelta
			 * };
			 */
			U_16 offsetDelta;
			NEXT_U16(offsetDelta, stackMap);
			writeU16(offsetDelta);
		} else if (CFR_STACKMAP_FULL > frameType) { /* 252..254 */
			/*
			 * APPEND {
			 *		U_16 offsetDelta
			 *		TypeInfo locals[frameType - CFR_STACKMAP_APPEND_BASE]
			 * };
			 */
			U_16 offsetDelta;
			NEXT_U16(offsetDelta, stackMap);
			writeU16(offsetDelta);
			writeVerificationTypeInfo(frameType - CFR_STACKMAP_APPEND_BASE, &stackMap);
		} else if (CFR_STACKMAP_FULL == frameType) { /* 255 */
			/*
			 * FULL {
			 *		U_16 offsetDelta
			 *		U_16 localsCount
			 *		TypeInfo locals[localsCount]
			 *		U_16 stackItemsCount
			 *		TypeInfo stackItems[stackItemsCount]
			 * };
			 */
			U_16 offsetDelta;
			/* u2 offset delta */
			NEXT_U16(offsetDelta, stackMap);
			writeU16(offsetDelta);
			/* handle locals */
			/* u2 number of locals  */
			U_16 localsCount;
			NEXT_U16(localsCount, stackMap);
			writeU16(localsCount);
			/* verification_type_info locals[number of locals] */
			writeVerificationTypeInfo(localsCount, &stackMap);

			/* handle stack */
			/* u2 number of stack items */
			U_16 stackItemsCount;
			NEXT_U16(stackItemsCount, stackMap);
			writeU16(stackItemsCount);
			/* verification_type_info stack[number of stack items] */
			writeVerificationTypeInfo(stackItemsCount, &stackMap);
		}
	}

	writeU32At((U_32)(_classFileCursor - start), attributeLenAddr);
}

void
ClassFileWriter::writeSignatureAttribute(J9UTF8 * genericSignature)
{
	writeAttributeHeader((J9UTF8 *) &SIGNATURE, 2);
	writeU16(indexForUTF8(genericSignature));
}

void
ClassFileWriter::writeAnnotationElement(U_8 **annotationElement)
{
	U_8 *cursor = *annotationElement;
	U_8 tag;
	
	NEXT_U8(tag, cursor);
	writeU8(tag);
	
	switch(tag) {
	case 'e': {
			U_16 typeNameIndex, constNameIndex;
			
			NEXT_U16(typeNameIndex, cursor);
			writeU16(typeNameIndex);
			NEXT_U16(constNameIndex, cursor);
			writeU16(constNameIndex);
		}
		break;
	case 'c': {
			U_16 classInfoIndex;
			
			NEXT_U16(classInfoIndex, cursor);
			writeU16(classInfoIndex);
		}
		break;
	case '@':
		writeAnnotation(&cursor);
		break;
	case '[': {
			U_16 numValues;
			
			NEXT_U16(numValues, cursor);
			writeU16(numValues);
			for (U_16 i = 0; i < numValues; i++) {
				writeAnnotationElement(&cursor);
			}
		}
		break;
	default: {
			U_16 constValueIndex;
			
			NEXT_U16(constValueIndex, cursor);
			if (('D' == tag) || ('J' == tag)) {
				/* Adjust index of double/long CP entry */
				constValueIndex = constValueIndex + (constValueIndex - _romClass->ramConstantPoolCount);
			}
			writeU16(constValueIndex);
		}
		break;
	}
	*annotationElement = cursor;
}

void
ClassFileWriter::writeAnnotation(U_8 **annotation)
{
	U_8 *cursor = *annotation;
	U_16 typeIndex;
	
	NEXT_U16(typeIndex, cursor);
	writeU16(typeIndex);
	
	U_16 numElements;
	NEXT_U16(numElements, cursor);
	writeU16(numElements);
	
	for (U_16 i = 0; i < numElements; i++) {
		U_16 elementNameIndex;
		
		NEXT_U16(elementNameIndex, cursor);
		writeU16(elementNameIndex);
		writeAnnotationElement(&cursor);
	}
	*annotation = cursor;
}

void
ClassFileWriter::writeAnnotationsAttribute(U_32 * annotationsData)
{
	writeAttributeHeader((J9UTF8 *) &RUNTIME_VISIBLE_ANNOTATIONS, *annotationsData);
	if (J9ROMCLASS_ANNOTATION_REFERS_DOUBLE_SLOT_ENTRY(_romClass)) {
		U_8 *data = (U_8 *)(annotationsData + 1);
		U_16 numAnnotations;
		
		NEXT_U16(numAnnotations, data);
		writeU16(numAnnotations);
		
		for (U_16 i = 0; i < numAnnotations; i++) {
			writeAnnotation(&data);
		}
	} else {
		writeData(*annotationsData, annotationsData + 1);
	}
}

void
ClassFileWriter::writeParameterAnnotationsAttribute(U_32 *parameterAnnotationsData)
{
	writeAttributeHeader((J9UTF8 *) &RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS, *parameterAnnotationsData);
	if (J9ROMCLASS_ANNOTATION_REFERS_DOUBLE_SLOT_ENTRY(_romClass)) {
		U_8 *data = (U_8 *)(parameterAnnotationsData + 1);
		U_8 numParameters;
		
		NEXT_U8(numParameters, data);
		writeU8(numParameters);
		
		for (U_8 i = 0; i < numParameters; i++) {
			U_16 numAnnotations;
			
			NEXT_U16(numAnnotations, data);
			writeU16(numAnnotations);
			
			for (U_16 i = 0; i < numAnnotations; i++) {
				writeAnnotation(&data);
			}
		}
	} else {
		writeData(*parameterAnnotationsData, parameterAnnotationsData + 1);
	}
}

void
ClassFileWriter::writeTypeAnnotationsAttribute(U_32 *typeAnnotationsData)
{
	writeAttributeHeader((J9UTF8 *) &RUNTIME_VISIBLE_TYPE_ANNOTATIONS, *typeAnnotationsData);
	if (J9ROMCLASS_ANNOTATION_REFERS_DOUBLE_SLOT_ENTRY(_romClass)) {
		U_8 *data = (U_8 *)(typeAnnotationsData + 1);
		U_16 numAnnotations;
		NEXT_U16(numAnnotations, data);
		writeU16(numAnnotations);
		if (CFR_TARGET_TYPE_ErrorInAttribute == *data) {
			writeData(*typeAnnotationsData, typeAnnotationsData + 1); /* dump out the raw bytes */
		} else {
			for (U_16 i = 0; i < numAnnotations; i++) {
				U_8 u8Data = 0;
				U_16 u16Data = 0;

				U_8 targetType;
				NEXT_U8(targetType, data);
				writeU8(targetType);
				switch (targetType) {
				case CFR_TARGET_TYPE_TypeParameterGenericClass:
				case CFR_TARGET_TYPE_TypeParameterGenericMethod:
					NEXT_U8(u8Data, data); /* typeParameterIndex */
					writeU8(u8Data);
					break;

				case CFR_TARGET_TYPE_TypeInExtends:
					NEXT_U16(u16Data, data);
					writeU16(u16Data); /* supertypeIndex */
					break;

				case CFR_TARGET_TYPE_TypeInBoundOfGenericClass:
				case CFR_TARGET_TYPE_TypeInBoundOfGenericMethod:
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* typeParameterIndex */
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* boundIndex */
					break;

				case CFR_TARGET_TYPE_TypeInFieldDecl:
				case CFR_TARGET_TYPE_ReturnType:
				case CFR_TARGET_TYPE_ReceiverType: /* empty_target */
					break;

				case CFR_TARGET_TYPE_TypeInFormalParam:
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* formalParameterIndex */
					break;

				case CFR_TARGET_TYPE_TypeInThrows:
					NEXT_U16(u16Data, data);
					writeU16(u16Data); /* throwsTypeIndex */
					break;

				case CFR_TARGET_TYPE_TypeInLocalVar:
				case CFR_TARGET_TYPE_TypeInResourceVar: {
					U_16 tableLength = 0;
					NEXT_U16(tableLength, data);
					writeU16(tableLength);
					for (U_32 ti=0; ti < tableLength; ++ti) {
						NEXT_U16(u16Data, data);
						writeU16(u16Data); /* startPC */
						NEXT_U16(u16Data, data);
						writeU16(u16Data); /* length */
						NEXT_U16(u16Data, data);
						writeU16(u16Data); /* index */
					}
				}
				break;
				case CFR_TARGET_TYPE_TypeInExceptionParam:
					NEXT_U16(u16Data, data);
					writeU16(u16Data); /* exceptiontableIndex */
					break;

				case CFR_TARGET_TYPE_TypeInInstanceof:
				case CFR_TARGET_TYPE_TypeInNew:
				case CFR_TARGET_TYPE_TypeInMethodrefNew:
				case CFR_TARGET_TYPE_TypeInMethodrefIdentifier:
					NEXT_U16(u16Data, data);
					writeU16(u16Data); /* offset */
					break;

				case CFR_TARGET_TYPE_TypeInCast:
				case CFR_TARGET_TYPE_TypeForGenericConstructorInNew:
				case CFR_TARGET_TYPE_TypeForGenericMethodInvocation:
				case CFR_TARGET_TYPE_TypeForGenericConstructorInMethodRef:
				case CFR_TARGET_TYPE_TypeForGenericMethodInvocationInMethodRef:
					NEXT_U16(u16Data, data);
					writeU16(u16Data); /* offset */
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* typeArgumentIndex */
					break;

				default:
					break;
				}
				U_8 pathLength;
				NEXT_U8(pathLength, data);
				writeU8(pathLength);
				for (U_8 pathIndex = 0; pathIndex < pathLength; ++pathIndex) {
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* typePathKind */
					NEXT_U8(u8Data, data);
					writeU8(u8Data); /* typeArgumentIndex */
				}
				writeAnnotation(&data);
			}
		}
	} else {
		writeData(*typeAnnotationsData, typeAnnotationsData + 1);
	}
}


void
ClassFileWriter::writeAnnotationDefaultAttribute(U_32 *annotationsDefaultData)
{
	writeAttributeHeader((J9UTF8 *) &ANNOTATION_DEFAULT, *annotationsDefaultData);
	if (J9ROMCLASS_ANNOTATION_REFERS_DOUBLE_SLOT_ENTRY(_romClass)) {
		U_8 *cursor = (U_8 *)(annotationsDefaultData+1);
		
		writeAnnotationElement(&cursor);
	} else {
		writeData(*annotationsDefaultData, annotationsDefaultData + 1);
	}
}

void
ClassFileWriter::writeAttributeHeader(J9UTF8 * name, U_32 length)
{
	writeU16(indexForUTF8(name));
	writeU32(length);
}

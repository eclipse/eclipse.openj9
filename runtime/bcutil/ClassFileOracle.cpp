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
 * ClassFileOracle.cpp
 */

#include "ClassFileOracle.hpp"
#include "BufferManager.hpp"
#include "ConstantPoolMap.hpp"
#include "ROMClassCreationContext.hpp"
#include "ROMClassVerbosePhase.hpp"


#include "j9port.h"
#include "jbcmap.h"
#include "ut_j9bcu.h"
#include "util_api.h"
#include "j9protos.h"

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
#define VALUE_TYPES_MAJOR_VERSION 55
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

/* The array entries must be in same order as the enums in ClassFileOracle.hpp */
ClassFileOracle::KnownAnnotation ClassFileOracle::_knownAnnotations[] = {
#define FRAMEITERATORSKIP_SIGNATURE "Ljava/lang/invoke/MethodHandle$FrameIteratorSkip;"
		{FRAMEITERATORSKIP_SIGNATURE, sizeof(FRAMEITERATORSKIP_SIGNATURE)},
#undef FRAMEITERATORSKIP_SIGNATURE
#define SUN_REFLECT_CALLERSENSITIVE_SIGNATURE "Lsun/reflect/CallerSensitive;"
		{SUN_REFLECT_CALLERSENSITIVE_SIGNATURE, sizeof(SUN_REFLECT_CALLERSENSITIVE_SIGNATURE)},
#undef SUN_REFLECT_CALLERSENSITIVE_SIGNATURE
#define JDK_INTERNAL_REFLECT_CALLERSENSITIVE_SIGNATURE "Ljdk/internal/reflect/CallerSensitive;"
		{JDK_INTERNAL_REFLECT_CALLERSENSITIVE_SIGNATURE, sizeof(JDK_INTERNAL_REFLECT_CALLERSENSITIVE_SIGNATURE)},
#undef JDK_INTERNAL_REFLECT_CALLERSENSITIVE_SIGNATURE
#define JAVA8_CONTENDED_SIGNATURE "Lsun/misc/Contended;" /* TODO remove this if VM does not support Java 8 */
		{JAVA8_CONTENDED_SIGNATURE, sizeof(JAVA8_CONTENDED_SIGNATURE)},
#undef JAVA8_CONTENDED_SIGNATURE
#define CONTENDED_SIGNATURE "Ljdk/internal/vm/annotation/Contended;"
		{CONTENDED_SIGNATURE, sizeof(CONTENDED_SIGNATURE)},
#undef CONTENDED_SIGNATURE
		{J9_UNMODIFIABLE_CLASS_ANNOTATION, sizeof(J9_UNMODIFIABLE_CLASS_ANNOTATION)},
#define VALUEBASED_SIGNATURE "Ljdk/internal/ValueBased;"
		{VALUEBASED_SIGNATURE, sizeof(VALUEBASED_SIGNATURE)},
#undef VALUEBASED_SIGNATURE
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
#if JAVA_SPEC_VERSION >= 16
#define HIDDEN_SIGNATURE "Ljdk/internal/vm/annotation/Hidden;"
#else /* JAVA_SPEC_VERSION >= 16 */
#define HIDDEN_SIGNATURE "Ljava/lang/invoke/LambdaForm$Hidden;"
#endif /* JAVA_SPEC_VERSION >= 16 */
		{HIDDEN_SIGNATURE, sizeof(HIDDEN_SIGNATURE)},
#undef HIDDEN_SIGNATURE
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
#if JAVA_SPEC_VERSION >= 16
#define SCOPED_SIGNATURE "Ljdk/internal/misc/ScopedMemoryAccess$Scoped;"
		{SCOPED_SIGNATURE, sizeof(SCOPED_SIGNATURE)},
#undef SCOPED_SIGNATURE
#endif /* JAVA_SPEC_VERSION >= 16 */
		{0, 0}
};

bool
ClassFileOracle::containsKnownAnnotation(UDATA knownAnnotationSet, UDATA knownAnnotation)
{
	UDATA knownAnnotationBit = (UDATA) 1 << knownAnnotation;
	return (knownAnnotationSet & knownAnnotationBit) == knownAnnotationBit;
}

UDATA
ClassFileOracle::addAnnotationBit(UDATA annotationBits, UDATA knownAnnotation)
{
	UDATA knownAnnotationBit = (UDATA) 1 << knownAnnotation;
	return annotationBits |= knownAnnotationBit;
}

U_16
ClassFileOracle::LocalVariablesIterator::getGenericSignatureIndex()
{
	Trc_BCU_Assert_NotEquals(NULL, _localVariableTable);
	Trc_BCU_Assert_NotEquals(NULL, _localVariablesInfo[_index].localVariableTypeTableAttribute);

	/* If the localVariableTable and localVariableTypeTable are in the same order, return the signatureIndex */
	J9CfrLocalVariableTypeTableEntry* localVariableTypeTable = _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTable;
	if ((_localVariableTableIndex < _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTableLength)
			&& (_localVariableTable[_localVariableTableIndex].index == localVariableTypeTable[_localVariableTableIndex].index)
			&& (_localVariableTable[_localVariableTableIndex].startPC == localVariableTypeTable[_localVariableTableIndex].startPC)
			&& (_localVariableTable[_localVariableTableIndex].length == localVariableTypeTable[_localVariableTableIndex].length)) {
		return localVariableTypeTable[_localVariableTableIndex].signatureIndex;
	}

	/* Scan for matching localVariableTypeTable entry */
	for (U_16 localVariableTypeTableIndex = 0;
			localVariableTypeTableIndex < _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTableLength;
			++localVariableTypeTableIndex) {
		if ((_localVariableTable[_localVariableTableIndex].index == localVariableTypeTable[localVariableTypeTableIndex].index)
				&& (_localVariableTable[_localVariableTableIndex].startPC == localVariableTypeTable[localVariableTypeTableIndex].startPC)
				&& (_localVariableTable[_localVariableTableIndex].length == localVariableTypeTable[localVariableTypeTableIndex].length)) {
			return localVariableTypeTable[localVariableTypeTableIndex].signatureIndex;
		}
	}

	Trc_BCU_Assert_ShouldNeverHappen();
	return 0;
}

bool
ClassFileOracle::LocalVariablesIterator::hasGenericSignature()
{
	Trc_BCU_Assert_NotEquals(NULL, _localVariableTable);

	/* Check if the current local variable isn't generic */
	if (NULL == _localVariablesInfo[_index].localVariableTypeTableAttribute) {
		return false;
	}

	/* Check if the localVariableTable and localVariableTypeTable are in the same order */
	J9CfrLocalVariableTypeTableEntry* localVariableTypeTable = _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTable;
	if ((_localVariableTableIndex < _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTableLength)
			&& (_localVariableTable[_localVariableTableIndex].index == localVariableTypeTable[_localVariableTableIndex].index)
			&& (_localVariableTable[_localVariableTableIndex].startPC == localVariableTypeTable[_localVariableTableIndex].startPC)
			&& (_localVariableTable[_localVariableTableIndex].length == localVariableTypeTable[_localVariableTableIndex].length)) {
		return true;
	}

	/* Scan for matching localVariableTypeTable entry */
	for (U_16 localVariableTypeTableIndex = 0;
			localVariableTypeTableIndex < _localVariablesInfo[_index].localVariableTypeTableAttribute->localVariableTypeTableLength;
			++localVariableTypeTableIndex) {
		if ((_localVariableTable[_localVariableTableIndex].index == localVariableTypeTable[localVariableTypeTableIndex].index)
				&& (_localVariableTable[_localVariableTableIndex].startPC == localVariableTypeTable[localVariableTypeTableIndex].startPC)
				&& (_localVariableTable[_localVariableTableIndex].length == localVariableTypeTable[localVariableTypeTableIndex].length)) {
			return true;
		}
	}
	return false;
}

ClassFileOracle::ClassFileOracle(BufferManager *bufferManager, J9CfrClassFile *classFile, ConstantPoolMap *constantPoolMap,
                                 U_8 * verifyExcludeAttribute, U_8 * romBuilderClassFileBuffer, ROMClassCreationContext *context) :
	_buildResult(OK),
	_bufferManager(bufferManager),
	_classFile(classFile),
	_constantPoolMap(constantPoolMap),
	_verifyExcludeAttribute(verifyExcludeAttribute),
	_romBuilderClassFileBuffer(romBuilderClassFileBuffer),
	_context(context),
	_singleScalarStaticCount(0),
	_objectStaticCount(0),
	_doubleScalarStaticCount(0),
	_memberAccessFlags(0),
	_innerClassCount(0),
	_enclosedInnerClassCount(0),
#if JAVA_SPEC_VERSION >= 11
	_nestMembersCount(0),
	_nestHost(0),
#endif /* JAVA_SPEC_VERSION >= 11 */
	_maxBranchCount(1), /* This is required to support buffer size calculations for stackmap support code */
	_outerClassNameIndex(0),
	_simpleNameIndex(0),
	_hasEmptyFinalizeMethod(false),
	_hasFinalFields(false),
	_hasNonStaticNonAbstractMethods(false),
	_hasFinalizeMethod(false),
	_isCloneable(false),
	_isSerializable(false),
	_isSynthetic(false),
	_hasVerifyExcludeAttribute(false),
	_hasFrameIteratorSkipAnnotation(false),
	_hasClinit(false),
	_annotationRefersDoubleSlotEntry(false),
	_fieldsInfo(NULL),
	_methodsInfo(NULL),
	_recordComponentsInfo(NULL),
	_genericSignature(NULL),
	_enclosingMethod(NULL),
	_sourceFile(NULL),
	_sourceDebugExtension(NULL),
	_annotationsAttribute(NULL),
	_typeAnnotationsAttribute(NULL),
	_innerClasses(NULL),
	_bootstrapMethodsAttribute(NULL),
#if JAVA_SPEC_VERSION >= 11
	_nestMembers(NULL),
#endif /* JAVA_SPEC_VERSION >= 11 */
	_isClassContended(false),
	_isClassUnmodifiable(context->isClassUnmodifiable()),
	_isInnerClass(false),
	_needsStaticConstantInit(false),
	_isRecord(false),
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	_hasIdentityInterface(false),
	_isIdentityInterfaceNeeded(false),
	_isValueType(false),
	_hasNonStaticSynchronizedMethod(false),
	_hasNonStaticFields(false),
	_hasNonEmptyConstructor(false),
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	_recordComponentCount(0),
	_permittedSubclassesAttribute(NULL),
	_isSealed(false),
	_isClassValueBased(false)
{
	Trc_BCU_Assert_NotEquals( classFile, NULL );

	ROMClassVerbosePhase v(_context, ClassFileAnalysis, &_buildResult);


	/* Quick check against expected class name */
	_buildResult = _context->checkClassName(getUTF8Data(getClassNameIndex()), getUTF8Length(getClassNameIndex()));
	if (OK != _buildResult) {
		return;
	}

	_fieldsInfo = (FieldInfo *) _bufferManager->alloc(getFieldsCount() * sizeof(FieldInfo));
	_methodsInfo = (MethodInfo *) _bufferManager->alloc(getMethodsCount() * sizeof(MethodInfo));
	if ( (NULL == _fieldsInfo) || (NULL == _methodsInfo) ) {
		Trc_BCU_ClassFileOracle_OutOfMemory((U_32)getUTF8Length(getClassNameIndex()), getUTF8Data(getClassNameIndex()));
		_buildResult = OutOfMemory;
		return;
	}
	memset(_fieldsInfo, 0, getFieldsCount() * sizeof(FieldInfo));
	memset(_methodsInfo, 0, getMethodsCount() * sizeof(MethodInfo));

	_constantPoolMap->setClassFileOracleAndInitialize(this);
	if ( !constantPoolMap->isOK() ) {
		_buildResult = _constantPoolMap->getBuildResult();
		return;
	}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)	
	if (J9_ARE_ALL_BITS_SET(_classFile->accessFlags, CFR_ACC_VALUE_TYPE) 
		|| J9_ARE_NO_BITS_SET(_classFile->accessFlags, CFR_ACC_ABSTRACT | CFR_ACC_INTERFACE)
	) {
		/**
		 * We care about whether there is a non-empty constructor only for non-value type abstract classes.
		 * Simply set _hasNonEmptyConstructor to true for value types, or concrete classes.
		 */ 
		_hasNonEmptyConstructor = true;
	}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

	/* analyze class file */

	if (OK == _buildResult) {
		walkHeader();
	}
	if (OK == _buildResult) {
		walkAttributes();
	}
	
	if (_context->isClassHidden()) {
		checkHiddenClass();
	}
	
	if (OK == _buildResult) {
		walkInterfaces();
	}
	
	
	if (OK == _buildResult) {
		walkMethods();
	}

	if (OK == _buildResult) {
		walkFields();
	}
	
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	if (OK == _buildResult) {
		checkAndRecordIsIdentityInterfaceNeeded();
	}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */
	if (OK == _buildResult) {
		_constantPoolMap->computeConstantPoolMapAndSizes();
		if (!constantPoolMap->isOK()) {
			_buildResult = _constantPoolMap->getBuildResult();
		} else {
			/* computeConstantPoolMapAndSizes must complete successfully before calling findVarHandleMethodRefs */
			_constantPoolMap->findVarHandleMethodRefs();
			if (!constantPoolMap->isOK()) {
				_buildResult = _constantPoolMap->getBuildResult();
			}
		}
	}

}

ClassFileOracle::~ClassFileOracle()
{
	if (NULL != _methodsInfo && OutOfMemory != _buildResult) {
		for (U_16 methodIndex = 0; methodIndex < _classFile->methodsCount; ++methodIndex) {
			_bufferManager->free(_methodsInfo[methodIndex].stackMapFramesInfo);
			_bufferManager->free(_methodsInfo[methodIndex].localVariablesInfo);
			_bufferManager->free(_methodsInfo[methodIndex].lineNumbersInfoCompressed);
		}
	}
	_bufferManager->free(_methodsInfo);
	_bufferManager->free(_fieldsInfo);

}

void
ClassFileOracle::walkHeader()
{
	ROMClassVerbosePhase v(_context, ClassFileHeaderAnalysis);

	markConstantUTF8AsReferenced(getClassNameIndex());
	U_16 superClassNameIndex = getSuperClassNameIndex();
	if (0 != superClassNameIndex) { /* java/lang/Object has no super class */
		markConstantUTF8AsReferenced(superClassNameIndex);
	}
}

void
ClassFileOracle::walkFields()
{
	ROMClassVerbosePhase v(_context, ClassFileFieldsAnalysis);


	U_16 fieldsCount = getFieldsCount();

	/* CMVC 197718 : After the first compliance offense is detected, if we do not stop, annotations on subsequent fields
	 * will not be parsed, resulting in all subsequent valid fields to be noted as not having proper @Length annotation, hence overwriting
	 * the good error message. Checking (OK == _buildResult) in for-loop condition achieves the desired error handling behavior.
	 */
	for (U_16 fieldIndex = 0; (OK == _buildResult) && (fieldIndex < fieldsCount); fieldIndex++) {
		J9CfrField *field = &_classFile->fields[fieldIndex];
		U_8 fieldChar = _classFile->constantPool[field->descriptorIndex].bytes[0];
		bool isStatic = (0 != (field->accessFlags & CFR_ACC_STATIC));


		markConstantUTF8AsReferenced(field->nameIndex);
		markConstantUTF8AsReferenced(field->descriptorIndex);

		if (isStatic) {
			if (NULL != field->constantValueAttribute) {
				_needsStaticConstantInit = true;
				U_16 constantValueIndex = field->constantValueAttribute->constantValueIndex;
				if (CFR_CONSTANT_String == _classFile->constantPool[constantValueIndex].tag) {
					markStringAsReferenced(constantValueIndex);
				}
			}
			if (('L' == fieldChar) || ('[' == fieldChar)) {
				_objectStaticCount++;
			} else if (('D' == fieldChar) || ('J' == fieldChar)) {
				_doubleScalarStaticCount++;
			} else {
				_singleScalarStaticCount++;
			}
		} else {
			if (0 != (field->accessFlags & CFR_ACC_FINAL)) {
				/* if the class has any final instance fields, mark it so that we can generate
				 * the appropriate memory barriers when its constructors run. See
				 * JBreturnFromConstructor
				 */
				_hasFinalFields = true;
			}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			_hasNonStaticFields = true;
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */
		}

		for (U_16 attributeIndex = 0; (OK == _buildResult) && (attributeIndex < field->attributesCount); ++attributeIndex) {
			J9CfrAttribute * attrib = field->attributes[attributeIndex];
			switch (attrib->tag) {
			case CFR_ATTRIBUTE_Signature: {
				J9CfrAttributeSignature *signature = (J9CfrAttributeSignature *) attrib;
				markConstantUTF8AsReferenced(signature->signatureIndex);
				_fieldsInfo[fieldIndex].hasGenericSignature = true;
				_fieldsInfo[fieldIndex].genericSignatureIndex = signature->signatureIndex;
				break;
			}
			case CFR_ATTRIBUTE_Synthetic:
				_fieldsInfo[fieldIndex].isSynthetic = true;
				break;
			case CFR_ATTRIBUTE_RuntimeVisibleAnnotations: {
				J9CfrAttributeRuntimeVisibleAnnotations *attribAnnotations = (J9CfrAttributeRuntimeVisibleAnnotations *)attrib;
				UDATA knownAnnotations = 0;
				if ((NULL != _context->javaVM()) && (J9_ARE_ALL_BITS_SET(_context->javaVM()->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_ALLOW_CONTENDED_FIELDS)) &&
						(_context->isBootstrapLoader() || (J9_ARE_ALL_BITS_SET(_context->javaVM()->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_ALLOW_APPLICATION_CONTENDED_FIELDS)))) {
					knownAnnotations = addAnnotationBit(knownAnnotations, CONTENDED_ANNOTATION);
					knownAnnotations = addAnnotationBit(knownAnnotations, JAVA8_CONTENDED_ANNOTATION);
				}
				if (0 == attribAnnotations->rawDataLength) {
					UDATA foundAnnotations = walkAnnotations(attribAnnotations->numberOfAnnotations, attribAnnotations->annotations, knownAnnotations);


					if (containsKnownAnnotation(foundAnnotations, CONTENDED_ANNOTATION) || containsKnownAnnotation(foundAnnotations, JAVA8_CONTENDED_ANNOTATION)) {
						_fieldsInfo[fieldIndex].isFieldContended = true;
					}
				}
				_fieldsInfo[fieldIndex].annotationsAttribute = attribAnnotations;
				break;
			}
			case CFR_ATTRIBUTE_RuntimeVisibleTypeAnnotations: {
				J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotations = (J9CfrAttributeRuntimeVisibleTypeAnnotations *)attrib;
				if (0 == typeAnnotations->rawDataLength) { /* rawDataLength non-zero in case of error in the attribute */
					walkTypeAnnotations(typeAnnotations->numberOfAnnotations, typeAnnotations->typeAnnotations);
				}
				_fieldsInfo[fieldIndex].typeAnnotationsAttribute = typeAnnotations;
				break;
			}
			case CFR_ATTRIBUTE_ConstantValue:
				/* Fall through */
			case CFR_ATTRIBUTE_Deprecated:
				/* Do nothing */
				break;
			default:
				Trc_BCU_ClassFileOracle_walkFields_UnknownAttribute((U_32)attrib->tag, (U_32)getUTF8Length(attrib->nameIndex), getUTF8Data(attrib->nameIndex), attrib->length);
				break;
			}
		}

	}
	
}

void
ClassFileOracle::walkAttributes()
{
	ROMClassVerbosePhase v(_context, ClassFileAttributesAnalysis);

	for (U_16 attributeIndex = 0; attributeIndex < _classFile->attributesCount; attributeIndex++) {
		J9CfrAttribute *attrib = _classFile->attributes[attributeIndex];
		switch (attrib->tag) {
		case CFR_ATTRIBUTE_InnerClasses: {
			J9CfrAttributeInnerClasses *classes = (J9CfrAttributeInnerClasses*)attrib;
			U_16 thisClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, _classFile->thisClass);
			for (U_16 classIndex = 0; classIndex < classes->numberOfClasses; classIndex++) {
				J9CfrClassesEntry *entry = &(classes->classes[classIndex]);
				U_16  outerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->outerClassInfoIndex);
				U_16  innerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->innerClassInfoIndex);

				/* In some cases, there might be two entries for the same class.
				 * But the UTF8 classname entry will be only one.
				 * Therefore comparing the UTF8 will find the matches, while comparing the class entries will not
				 */
				if (outerClassUTF8 == thisClassUTF8) {
					/* Member class - mark the class' name. */
					markClassNameAsReferenced(entry->innerClassInfoIndex);
					_innerClassCount++;
				} else if (innerClassUTF8 == thisClassUTF8) {
					_isInnerClass = true;
					_memberAccessFlags = entry->innerClassAccessFlags;
					/* We are an inner class - a member? */
					if (entry->outerClassInfoIndex != 0) {
						/* We are a member class - mark the outer class name. */
						markClassNameAsReferenced(entry->outerClassInfoIndex);
						_outerClassNameIndex = outerClassUTF8;
					}
					if (entry->innerNameIndex != 0) {
						/* mark the simple class name of member, local, and anonymous classes */
						markConstantUTF8AsReferenced(entry->innerNameIndex);
						_simpleNameIndex = entry->innerNameIndex;
					}
				/* Count all entries in the InnerClass attribute (except the inner class itself) so as
				 * to check the InnerClass attribute between the inner classes and the enclosing class.
				 * See getDeclaringClass() for details.
				 */
				} else {
					markClassNameAsReferenced(entry->innerClassInfoIndex);
					_enclosedInnerClassCount++;
				}
			}
			Trc_BCU_Assert_Equals(NULL, _innerClasses);
			_innerClasses = classes;
			break;
		}
		case CFR_ATTRIBUTE_Signature:
			_genericSignature = (J9CfrAttributeSignature *)attrib;
			markConstantUTF8AsReferenced(_genericSignature->signatureIndex);
			break;
		case CFR_ATTRIBUTE_EnclosingMethod:
			_enclosingMethod = (J9CfrAttributeEnclosingMethod *)attrib;
			markClassAsReferenced(_enclosingMethod->classIndex);
			if (0 != _enclosingMethod->methodIndex) {
				markNameAndDescriptorAsReferenced(_enclosingMethod->methodIndex);
			}
			break;
		case CFR_ATTRIBUTE_Synthetic:
			_isSynthetic = true;
			break;
		case CFR_ATTRIBUTE_SourceFile:
			if (!hasSourceFile() && _context->shouldPreserveSourceFileName()) {
				_sourceFile = (J9CfrAttributeSourceFile *)attrib;
				markConstantUTF8AsReferenced(_sourceFile->sourceFileIndex);
			}
			break;
		case CFR_ATTRIBUTE_SourceDebugExtension:
			if (!hasSourceDebugExtension()) {
				_sourceDebugExtension = (J9CfrAttributeUnknown *)attrib;
			}
			break;
		case CFR_ATTRIBUTE_RuntimeVisibleAnnotations: {
			UDATA knownAnnotations = 0;
			if ((NULL != _context->javaVM()) && J9_ARE_ALL_BITS_SET(_context->javaVM()->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_ALLOW_CONTENDED_FIELDS) &&
					(_context->isBootstrapLoader() || (J9_ARE_ALL_BITS_SET(_context->javaVM()->extendedRuntimeFlags, J9_EXTENDED_RUNTIME_ALLOW_APPLICATION_CONTENDED_FIELDS)))) {
				knownAnnotations = addAnnotationBit(knownAnnotations, CONTENDED_ANNOTATION);
				knownAnnotations = addAnnotationBit(knownAnnotations, JAVA8_CONTENDED_ANNOTATION);
			}
			knownAnnotations = addAnnotationBit(knownAnnotations, UNMODIFIABLE_ANNOTATION);
			knownAnnotations = addAnnotationBit(knownAnnotations, VALUEBASED_ANNOTATION);
			_annotationsAttribute = (J9CfrAttributeRuntimeVisibleAnnotations *)attrib;
			if (0 == _annotationsAttribute->rawDataLength) {
				UDATA foundAnnotations = walkAnnotations(_annotationsAttribute->numberOfAnnotations, _annotationsAttribute->annotations, knownAnnotations);
				if (containsKnownAnnotation(foundAnnotations, CONTENDED_ANNOTATION) || containsKnownAnnotation(foundAnnotations, JAVA8_CONTENDED_ANNOTATION)) {
					_isClassContended = true;
				}
				if (containsKnownAnnotation(foundAnnotations, UNMODIFIABLE_ANNOTATION)) {
					_isClassUnmodifiable = true;
				}
				if (containsKnownAnnotation(foundAnnotations, VALUEBASED_ANNOTATION)) {
					_isClassValueBased = true;
				}
			}
			break;
		}
		case CFR_ATTRIBUTE_RuntimeVisibleTypeAnnotations: {
			J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotations = (J9CfrAttributeRuntimeVisibleTypeAnnotations *)attrib;
			if (0 == typeAnnotations->rawDataLength) {  /* rawDataLength non-zero in case of error in the attribute */
				walkTypeAnnotations(typeAnnotations->numberOfAnnotations, typeAnnotations->typeAnnotations);
			}
			_typeAnnotationsAttribute = typeAnnotations;
			break;
		}
		case CFR_ATTRIBUTE_BootstrapMethods: {
			_bootstrapMethodsAttribute = (J9CfrAttributeBootstrapMethods *)attrib;
			for (U_16 methodIndex = 0; methodIndex < _bootstrapMethodsAttribute->numberOfBootstrapMethods; methodIndex++) {
				J9CfrBootstrapMethod *bootstrapMethod = _bootstrapMethodsAttribute->bootstrapMethods + methodIndex;
				markMethodHandleAsReferenced(bootstrapMethod->bootstrapMethodIndex);
				for (U_16 argumentIndex = 0; argumentIndex < bootstrapMethod->numberOfBootstrapArguments; argumentIndex++) {
					U_16 argCpIndex = bootstrapMethod->bootstrapArguments[argumentIndex];
					markConstantBasedOnCpType(argCpIndex, false);
				}
			}
			break;
		}
		case CFR_ATTRIBUTE_Record: {
			_isRecord = true;
			walkRecordComponents((J9CfrAttributeRecord *)attrib);
			break;
		}
		case CFR_ATTRIBUTE_PermittedSubclasses: {
			/* PermittedSubclasses verification is for Java version >= 15 */
			if ((_classFile->majorVersion > 59)
			|| ((59 == _classFile->majorVersion) && (65535 == _classFile->minorVersion))
			) {
				_isSealed = true;
				_permittedSubclassesAttribute = (J9CfrAttributePermittedSubclasses *)attrib;
				for (U_16 numberOfClasses = 0; numberOfClasses < _permittedSubclassesAttribute->numberOfClasses; numberOfClasses++) {
					U_16 classCpIndex = _permittedSubclassesAttribute->classes[numberOfClasses];
					markClassAsReferenced(classCpIndex);
				}
			}
			break;
		}
#if JAVA_SPEC_VERSION >= 11
		case CFR_ATTRIBUTE_NestMembers:
			/* ignore CFR_ATTRIBUTE_NestMembers for hidden classes, as the nest members never know the name of hidden classes */
			if (!_context->isClassHidden()) {
				_nestMembers = (J9CfrAttributeNestMembers *)attrib;
				_nestMembersCount = _nestMembers->numberOfClasses;
				/* The classRefs are never resolved & therefore do not need to
				 * be kept in the constant pool.
				 */
				for (U_16 i = 0; i < _nestMembersCount; i++) {
					U_16 classNameIndex = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, _nestMembers->classes[i]);
					markConstantUTF8AsReferenced(classNameIndex);
				}
			}
			break;

		case CFR_ATTRIBUTE_NestHost: {
			/* Ignore CFR_ATTRIBUTE_NestHost for hidden classes, as the nest host of a hidden class is not decided by CFR_ATTRIBUTE_NestHost.
			 * The nesthost of a hidden class is its host class if ClassOption.NESTMATE is used or itself if ClassOption.NESTMATE is not used. */
			if (!_context->isClassHidden()) {
				U_16 hostClassIndex = ((J9CfrAttributeNestHost *)attrib)->hostClassIndex;
				_nestHost = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, hostClassIndex);
				markConstantUTF8AsReferenced(_nestHost);
			}
			break;
		}
#endif /* JAVA_SPEC_VERSION >= 11 */
		default:
			Trc_BCU_ClassFileOracle_walkAttributes_UnknownAttribute((U_32)attrib->tag, (U_32)getUTF8Length(attrib->nameIndex), getUTF8Data(attrib->nameIndex), attrib->length);
			break;
		}
		if (!_hasVerifyExcludeAttribute && (NULL != _verifyExcludeAttribute)) {
			U_8 *found = (U_8 *) strstr((const char *)_verifyExcludeAttribute, (const char *)getUTF8Data(attrib->nameIndex));
			if ((NULL != found)
			&& ((found == _verifyExcludeAttribute) || (';' == (*(found - 1))))
			&& (('\0' == found[getUTF8Length(attrib->nameIndex)]) || (';' == found[getUTF8Length(attrib->nameIndex)]))) {
				_hasVerifyExcludeAttribute = true;
			}
		}
	}
}

void
ClassFileOracle::checkHiddenClass()
{
	ROMClassVerbosePhase v(_context, ClassFileAttributesAnalysis);
	/* Hidden Class cannot be a record or enum. */
	U_16 superClassNameIndex = getSuperClassNameIndex();
	bool isEnum = false;
	
	/**
	 * See test case jdk/java/lang/invoke/defineHiddenClass/BasicTest.emptyHiddenClass().
	 * A normal Enum cannot be defined as a hidden class. But an empty enum class that does not
	 * define constants of its type can still be defined as a hidden class. 
	 * So when setting isEnum, add a check for field count. 
	 */
	if (0 != superClassNameIndex) {
		isEnum = J9_ARE_ALL_BITS_SET(_classFile->accessFlags, CFR_ACC_ENUM) && 
				J9UTF8_DATA_EQUALS(getUTF8Data(superClassNameIndex), getUTF8Length(superClassNameIndex), "java/lang/Enum", LITERAL_STRLEN("java/lang/Enum")) &&
				(getFieldsCount() > 0);
	}
	if (_isRecord  || isEnum) {
		PORT_ACCESS_FROM_PORT(_context->portLibrary());
		char msg[] = "Hidden Class cannot be a record or enum";
		UDATA len = sizeof(msg);
		char *error = (char *) j9mem_allocate_memory(len, J9MEM_CATEGORY_CLASSES);
		if (NULL != error) {
			strcpy(error, msg);
			_context->recordCFRError((U_8*)error);
		}
		_buildResult = InvalidClassType;
	}
}

void
ClassFileOracle::walkRecordComponents(J9CfrAttributeRecord *attrib)
{
	ROMClassVerbosePhase v(_context, ClassFileAttributesRecordAnalysis);

	if (0 == attrib->numberOfRecordComponents) {
		return;
	}

	_recordComponentCount = attrib->numberOfRecordComponents;

	_recordComponentsInfo = (RecordComponentInfo *) _bufferManager->alloc(_recordComponentCount * sizeof(RecordComponentInfo));
	if (NULL == _recordComponentsInfo) {
		Trc_BCU_ClassFileOracle_OutOfMemory((U_32)getUTF8Length(getClassNameIndex()), getUTF8Data(getClassNameIndex()));
		_buildResult = OutOfMemory;
		return;
	}
	memset(_recordComponentsInfo, 0, _recordComponentCount * sizeof(RecordComponentInfo));

	for (U_16 i = 0; i < _recordComponentCount; i++) {
		J9CfrRecordComponent* recordComponent = &attrib->recordComponents[i];

		markConstantUTF8AsReferenced(recordComponent->nameIndex);
		_recordComponentsInfo[i].nameIndex = recordComponent->nameIndex;
		markConstantUTF8AsReferenced(recordComponent->descriptorIndex);
		_recordComponentsInfo[i].descriptorIndex = recordComponent->descriptorIndex;

		/* track record component attributes */
		for (U_16 j = 0; j < recordComponent->attributesCount; j++) {
			J9CfrAttribute* recordComponentAttr = recordComponent->attributes[j];
			switch(recordComponentAttr->tag) {
			case CFR_ATTRIBUTE_Signature: {
				J9CfrAttributeSignature *signature = (J9CfrAttributeSignature *) recordComponentAttr;
				markConstantUTF8AsReferenced(signature->signatureIndex);
				_recordComponentsInfo[i].hasGenericSignature = true;
				_recordComponentsInfo[i].genericSignatureIndex = signature->signatureIndex;
				break;
			}
			case CFR_ATTRIBUTE_RuntimeVisibleAnnotations: {
				J9CfrAttributeRuntimeVisibleAnnotations *recordComponentAnnotations = (J9CfrAttributeRuntimeVisibleAnnotations *)recordComponentAttr;
				if (0 == recordComponentAnnotations->rawDataLength) {
					walkAnnotations(recordComponentAnnotations->numberOfAnnotations, recordComponentAnnotations->annotations, 0);
				}
				_recordComponentsInfo[i].annotationsAttribute = recordComponentAnnotations;
				break;
			}
			case CFR_ATTRIBUTE_RuntimeVisibleTypeAnnotations: {
				J9CfrAttributeRuntimeVisibleTypeAnnotations *recordComponentTypeAnnotations = (J9CfrAttributeRuntimeVisibleTypeAnnotations *)recordComponentAttr;
				if (0 == recordComponentTypeAnnotations->rawDataLength) {
					walkTypeAnnotations(recordComponentTypeAnnotations->numberOfAnnotations, recordComponentTypeAnnotations->typeAnnotations);
				}
				_recordComponentsInfo[i].typeAnnotationsAttribute = recordComponentTypeAnnotations;
				break;
			}
			default:
				Trc_BCU_ClassFileOracle_walkRecordComponents_UnknownAttribute((U_32)attrib->tag, (U_32)getUTF8Length(attrib->nameIndex), getUTF8Data(attrib->nameIndex), attrib->length);
				break;
			}
		}
	}
}

class ClassFileOracle::InterfaceVisitor : public ClassFileOracle::ConstantPoolIndexVisitor
{
public:
	InterfaceVisitor(ClassFileOracle *classFileOracle, ConstantPoolMap *constantPoolMap) :
		_classFileOracle(classFileOracle),
		_constantPoolMap(constantPoolMap),
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		_wasIdentityInterfaceSeen(false),
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		_wasCloneableSeen(false),
		_wasSerializableSeen(false)
	{
	}

	void visitConstantPoolIndex(U_16 cpIndex)
	{
		_constantPoolMap->markConstantUTF8AsReferenced(cpIndex);
#define CLONEABLE_NAME "java/lang/Cloneable"
		if( _classFileOracle->isUTF8AtIndexEqualToString(cpIndex, CLONEABLE_NAME, sizeof(CLONEABLE_NAME)) ) {
			_wasCloneableSeen = true;
		}
#undef CLONEABLE_NAME

#define SERIALIZABLE_NAME "java/io/Serializable"
		if( _classFileOracle->isUTF8AtIndexEqualToString(cpIndex, SERIALIZABLE_NAME, sizeof(SERIALIZABLE_NAME)) ) {
			_wasSerializableSeen = true;
		}
#undef SERIALIZABLE_NAME

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		if( _classFileOracle->isUTF8AtIndexEqualToString(cpIndex, IDENTITY_OBJECT_NAME, sizeof(IDENTITY_OBJECT_NAME)) ) {
			_wasIdentityInterfaceSeen = true;
		}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}

	bool wasCloneableSeen() const { return _wasCloneableSeen; }
	bool wasSerializableSeen() const { return _wasSerializableSeen; }
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	bool wasIdentityInterfaceSeen() const { return _wasIdentityInterfaceSeen; }
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

private:
	ClassFileOracle *_classFileOracle;
	ConstantPoolMap *_constantPoolMap;
	bool _wasCloneableSeen;
	bool _wasSerializableSeen;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	bool _wasIdentityInterfaceSeen;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
};

void
ClassFileOracle::walkInterfaces()
{
	ROMClassVerbosePhase v(_context, ClassFileInterfacesAnalysis);

	InterfaceVisitor interfaceVisitor(this, _constantPoolMap);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	interfacesDo(&interfaceVisitor, 0);
#else
	interfacesDo(&interfaceVisitor);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	_isCloneable = interfaceVisitor.wasCloneableSeen();
	_isSerializable = interfaceVisitor.wasSerializableSeen();
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	if (J9_ARE_ALL_BITS_SET(_classFile->accessFlags, CFR_ACC_VALUE_TYPE)) {
		_isValueType = true;
	}
	_hasIdentityInterface = interfaceVisitor.wasIdentityInterfaceSeen();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
void
ClassFileOracle::checkAndRecordIsIdentityInterfaceNeeded()
{
	if (isValueType()) {
		if (_hasIdentityInterface
			|| J9_ARE_ANY_BITS_SET(_classFile->accessFlags, CFR_ACC_ABSTRACT | CFR_ACC_INTERFACE)
			|| _hasNonStaticSynchronizedMethod
		) {
			_buildResult = InvalidValueType;
		}
	} else {
		if (!_hasIdentityInterface
			&& (getSuperClassNameIndex() != 0) /* j.l.Object has no superClass */
		) {
			if (J9_ARE_NO_BITS_SET(_classFile->accessFlags, CFR_ACC_ABSTRACT | CFR_ACC_INTERFACE)) {
				/* For concrete classes, IdentityInterface is needed.  */
				_isIdentityInterfaceNeeded = true;
			} else {
				/**
				 * For abstract classes, IdentityInterface is needed only when it has non-static fields,
				 * non-static synchronized method or non-empty constructor.
				 */
				if ((_hasNonStaticFields)
					|| (_hasNonStaticSynchronizedMethod)
					|| (_hasNonEmptyConstructor)
				) {
					_isIdentityInterfaceNeeded = true;
				}
			}
		}
	}
}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

void
ClassFileOracle::walkMethods()
{
	ROMClassVerbosePhase v(_context, ClassFileMethodsAnalysis);

	U_16 methodsCount = getMethodsCount();
	/* We check (OK == _buildResult) because walkMethodCodeAttribute() may fail to alloc the bytecode fixup table. */
	for (U_16 methodIndex = 0; (methodIndex < methodsCount) && (OK == _buildResult); ++methodIndex) {
		U_16 nameIndex = _classFile->methods[methodIndex].nameIndex;
		U_16 descIndex = _classFile->methods[methodIndex].descriptorIndex;
		markConstantUTF8AsReferenced(nameIndex);
		markConstantUTF8AsReferenced(descIndex);


		walkMethodAttributes(methodIndex);

		_methodsInfo[methodIndex].modifiers |= _classFile->methods[methodIndex].accessFlags;

		/* Is this an empty method, a getter, a forwarder or <clinit>?
		 * Note that <clinit> is checked after empty, so we will consider
		 * classes with an empty <clinit> to not have one.
		 */
		if (methodIsEmpty(methodIndex)) {
			_methodsInfo[methodIndex].modifiers |= J9AccEmptyMethod;
		} else if (methodIsForwarder(methodIndex)) {
			_methodsInfo[methodIndex].modifiers |= J9AccForwarderMethod;
		} else if (methodIsGetter(methodIndex)) {
			_methodsInfo[methodIndex].modifiers |= J9AccGetterMethod;
		} else if (methodIsClinit(methodIndex)) {
			_hasClinit = true;
		}

		if (methodIsObjectConstructor(methodIndex)) {
			_methodsInfo[methodIndex].modifiers |= J9AccMethodObjectConstructor;
		}
		
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		if (!_hasNonEmptyConstructor) {
			if (methodIsConstructor(methodIndex)) {
				/* Do not record constructor forwarded to its superclass. */
				if (J9_ARE_NO_BITS_SET(_methodsInfo[methodIndex].modifiers, J9AccForwarderMethod)) {
					_hasNonEmptyConstructor = true;
				}
			}
		}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

		/* does the method belong in vtables? */
		if (methodIsVirtual(methodIndex)) {
			_methodsInfo[methodIndex].modifiers |= J9AccMethodVTable;
		}

		/* Does this class contain non-static, non-abstract methods? */
		if (!_hasNonStaticNonAbstractMethods) {
			_hasNonStaticNonAbstractMethods = methodIsNonStaticNonAbstract(methodIndex);
		}

		/* Look for an instance selector whose name is finalize()V */
		if (methodIsFinalize(methodIndex, 0 != (_methodsInfo[methodIndex].modifiers & J9AccForwarderMethod))) {
			_hasFinalizeMethod = true;
			/* If finalize() is empty, mark this class so it does not inherit CFR_ACC_FINALIZE_NEEDED from its superclass */
			if (0 != (_methodsInfo[methodIndex].modifiers & J9AccEmptyMethod)) {
				_hasEmptyFinalizeMethod = true;
			}
		}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		if (!_hasNonStaticSynchronizedMethod) {
			_hasNonStaticSynchronizedMethod = methodIsNonStaticSynchronized(methodIndex);
		}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

		computeSendSlotCount(methodIndex);

		walkMethodThrownExceptions(methodIndex);
		walkMethodCodeAttribute(methodIndex);
		walkMethodMethodParametersAttribute(methodIndex);
	}
	
}

void
ClassFileOracle::walkMethodAttributes(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodAttributesAnalysis);

	for (U_16 methodAttrIndex = 0; (methodAttrIndex < _classFile->methods[methodIndex].attributesCount) && (OK == _buildResult); ++methodAttrIndex){
		J9CfrAttribute *attrib = _classFile->methods[methodIndex].attributes[methodAttrIndex];
		switch (attrib->tag) {
		case CFR_ATTRIBUTE_Synthetic:
			_methodsInfo[methodIndex].modifiers |= J9AccSynthetic;
			break;
		case CFR_ATTRIBUTE_Signature: {
			J9CfrAttributeSignature *signature = (J9CfrAttributeSignature *) attrib;
			markConstantUTF8AsReferenced(signature->signatureIndex);
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasGenericSignature;
			_methodsInfo[methodIndex].genericSignatureIndex = signature->signatureIndex;
			break;
		}
		case CFR_ATTRIBUTE_RuntimeVisibleAnnotations: {
			UDATA knownAnnotations = 0;
			knownAnnotations = addAnnotationBit(knownAnnotations, FRAMEITERATORSKIP_ANNOTATION);
			knownAnnotations = addAnnotationBit(knownAnnotations, SUN_REFLECT_CALLERSENSITIVE_ANNOTATION);
			knownAnnotations = addAnnotationBit(knownAnnotations, JDK_INTERNAL_REFLECT_CALLERSENSITIVE_ANNOTATION);
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
			knownAnnotations = addAnnotationBit(knownAnnotations, HIDDEN_ANNOTATION);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
#if JAVA_SPEC_VERSION >= 16
			knownAnnotations = addAnnotationBit(knownAnnotations, SCOPED_ANNOTATION);
#endif /* JAVA_SPEC_VERSION >= 16*/

			J9CfrAttributeRuntimeVisibleAnnotations *attribAnnotations = (J9CfrAttributeRuntimeVisibleAnnotations *)attrib;
			if (0 == attribAnnotations->rawDataLength) { /* rawDataLength non-zero in case of error in the attribute */
				UDATA foundAnnotations = walkAnnotations(attribAnnotations->numberOfAnnotations, attribAnnotations->annotations, knownAnnotations);
				if (containsKnownAnnotation(foundAnnotations, SUN_REFLECT_CALLERSENSITIVE_ANNOTATION)
					|| containsKnownAnnotation(foundAnnotations, JDK_INTERNAL_REFLECT_CALLERSENSITIVE_ANNOTATION)
				) {
					_methodsInfo[methodIndex].modifiers |= J9AccMethodCallerSensitive;
				}
				if (_context->isBootstrapLoader()) {
					/* Only check for FrameIteratorSkip annotation on bootstrap classes */
					if (containsKnownAnnotation(foundAnnotations, FRAMEITERATORSKIP_ANNOTATION)) {
						_methodsInfo[methodIndex].modifiers |= J9AccMethodFrameIteratorSkip;
					}
				}
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
				if (containsKnownAnnotation(foundAnnotations, HIDDEN_ANNOTATION)) {
					/* J9AccMethodFrameIteratorSkip is reused for Hidden Annotation when OpenJDK MH is enabled
					 * Hidden annotation is used by OpenJDK to tag LambdaForm generated methods which is similar
					 * to the thunkArchetype methods as they both need to be skipped during stackwalk when
					 * verifying the caller
					 */
					_methodsInfo[methodIndex].modifiers |= J9AccMethodFrameIteratorSkip;
				}
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
#if JAVA_SPEC_VERSION >= 16
				if (containsKnownAnnotation(foundAnnotations, SCOPED_ANNOTATION)) {
					/* J9AccMethodHasExtendedModifiers in the modifiers is set when the ROM class is written */
					_methodsInfo[methodIndex].extendedModifiers |= CFR_METHOD_EXT_HAS_SCOPED_ANNOTATION;
				}
#endif /* JAVA_SPEC_VERSION >= 16*/
			}
			_methodsInfo[methodIndex].annotationsAttribute = attribAnnotations;
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasMethodAnnotations;
			break;
		}
		case CFR_ATTRIBUTE_RuntimeVisibleParameterAnnotations: {
			J9CfrAttributeRuntimeVisibleParameterAnnotations *attribParameterAnnotations = (J9CfrAttributeRuntimeVisibleParameterAnnotations *)attrib;
			for (U_8 parameterAnnotationIndex = 0;
					(parameterAnnotationIndex < attribParameterAnnotations->numberOfParameters) && (OK == _buildResult);
					++parameterAnnotationIndex) {
				walkAnnotations(
						attribParameterAnnotations->parameterAnnotations[parameterAnnotationIndex].numberOfAnnotations,
						attribParameterAnnotations->parameterAnnotations[parameterAnnotationIndex].annotations,
						0);
			}
			_methodsInfo[methodIndex].parameterAnnotationsAttribute = attribParameterAnnotations;
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasParameterAnnotations;
			break;
		}
		case CFR_ATTRIBUTE_RuntimeVisibleTypeAnnotations: {
			J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotations = (J9CfrAttributeRuntimeVisibleTypeAnnotations *)attrib;
			if (0 == typeAnnotations->rawDataLength) { /* rawDataLength non-zero in case of error in the attribute */
				walkTypeAnnotations(typeAnnotations->numberOfAnnotations, typeAnnotations->typeAnnotations);
			}
			_methodsInfo[methodIndex].methodTypeAnnotationsAttribute = typeAnnotations;
			/* J9AccMethodHasExtendedModifiers in the modifiers is set when the ROM class is written */
			_methodsInfo[methodIndex].extendedModifiers |= CFR_METHOD_EXT_HAS_METHOD_TYPE_ANNOTATIONS;
			break;
		}
		case CFR_ATTRIBUTE_AnnotationDefault:
			walkAnnotationElement(((J9CfrAttributeAnnotationDefault *)attrib)->defaultValue);
			_methodsInfo[methodIndex].defaultAnnotationAttribute = (J9CfrAttributeAnnotationDefault *)attrib;
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasDefaultAnnotation;
			break;
		case CFR_ATTRIBUTE_MethodParameters:
			/* Fall through */	
		case CFR_ATTRIBUTE_Code:
			/* Fall through */
		case CFR_ATTRIBUTE_Exceptions:
			/* Fall through */
		case CFR_ATTRIBUTE_Deprecated:
			/* Do nothing */
			break;
		default:
			Trc_BCU_ClassFileOracle_walkMethods_UnknownAttribute((U_32)attrib->tag, (U_32)getUTF8Length(attrib->nameIndex), getUTF8Data(attrib->nameIndex), attrib->length);
			break;
		}
	}
}

UDATA
ClassFileOracle::walkAnnotations(U_16 annotationsCount, J9CfrAnnotation *annotations, UDATA knownAnnotations)
{
	ROMClassVerbosePhase v(_context, ClassFileAnnotationsAnalysis);
	UDATA foundAnnotations = 0;

	if (0 == knownAnnotations) {
		for (U_16 annotationsIndex = 0; (annotationsIndex < annotationsCount) &&(OK == _buildResult); ++annotationsIndex) {
			markConstantAsUsedByAnnotation(annotations[annotationsIndex].typeIndex);
			const U_16 elementValuePairsCount = annotations[annotationsIndex].numberOfElementValuePairs;
			for (U_16 elementValuePairsIndex = 0; (elementValuePairsIndex < elementValuePairsCount) && (OK == _buildResult); ++elementValuePairsIndex) {
				markConstantAsUsedByAnnotation(annotations[annotationsIndex].elementValuePairs[elementValuePairsIndex].elementNameIndex);
				walkAnnotationElement(annotations[annotationsIndex].elementValuePairs[elementValuePairsIndex].value);
			}
		}
	} else {
		for (U_16 annotationsIndex = 0; (annotationsIndex < annotationsCount) && (OK == _buildResult); ++annotationsIndex) {
			markConstantAsUsedByAnnotation(annotations[annotationsIndex].typeIndex);
			for (UDATA knownAnnotationIndex = 0; knownAnnotationIndex < KNOWN_ANNOTATION_COUNT; ++knownAnnotationIndex) {
				if (containsKnownAnnotation(knownAnnotations, knownAnnotationIndex) && isUTF8AtIndexEqualToString(annotations[annotationsIndex].typeIndex, _knownAnnotations[knownAnnotationIndex].name, _knownAnnotations[knownAnnotationIndex].size)) {
					foundAnnotations = addAnnotationBit(foundAnnotations, knownAnnotationIndex);
				}
			}
			const U_16 elementValuePairsCount = annotations[annotationsIndex].numberOfElementValuePairs;
			for (U_16 elementValuePairsIndex = 0; (elementValuePairsIndex < elementValuePairsCount) && (OK == _buildResult); ++elementValuePairsIndex) {
				markConstantAsUsedByAnnotation(annotations[annotationsIndex].elementValuePairs[elementValuePairsIndex].elementNameIndex);
				walkAnnotationElement(annotations[annotationsIndex].elementValuePairs[elementValuePairsIndex].value);
			}
		}
	}
	return foundAnnotations;
}

void
ClassFileOracle::walkTypeAnnotations(U_16 annotationsCount, J9CfrTypeAnnotation *typeAnnotations) {
	for (U_16 typeAnnotationIndex = 0; typeAnnotationIndex < annotationsCount; ++ typeAnnotationIndex) {
		J9CfrAnnotation *annotation = &(typeAnnotations[typeAnnotationIndex].annotation);
		/* type_index in an annotation must refer to a CONSTANT_UTF8_info structure. */
		if (getCPTag(annotation->typeIndex) == CFR_CONSTANT_Utf8) {
			markConstantAsUsedByAnnotation(annotation->typeIndex);
			const U_16 elementValuePairsCount = annotation->numberOfElementValuePairs;
			for (U_16 elementValuePairsIndex = 0;
					(elementValuePairsIndex < elementValuePairsCount) && (OK == _buildResult); ++elementValuePairsIndex) {
				markConstantAsUsedByAnnotation(annotation->elementValuePairs[elementValuePairsIndex].elementNameIndex);
				walkAnnotationElement(annotation->elementValuePairs[elementValuePairsIndex].value);
			}
		} else {
			/*
			 * UTF-8 entries and method entries use the same set of marking labels when
			 * preparing to build the ROM class, but
			 * the label value meanings differ depending on the type of the entry.
			 * Thus we cannot mark a non-UTF-8 entry with a label used for UTF-8 as that label
			 * value will be (mis)interpreted according to the type of the entry.
			 *
			 * In this case, force the typeIndex to a null value.
			 * This will cause the parser to throw
			 * an error if the VM or application tries to retrieve the annotation.
			 */
			annotation->typeIndex = 0;
		}
	}
}

void
ClassFileOracle::walkAnnotationElement(J9CfrAnnotationElement * annotationElement)
{
	ROMClassVerbosePhase v(_context, ClassFileAnnotationElementAnalysis);

	switch (annotationElement->tag) {
	case 'e':
		markConstantAsUsedByAnnotation(((J9CfrAnnotationElementEnum *)annotationElement)->typeNameIndex);
		markConstantAsUsedByAnnotation(((J9CfrAnnotationElementEnum *)annotationElement)->constNameIndex);
		break;
	case 'c':
		markConstantAsUsedByAnnotation(((J9CfrAnnotationElementClass *)annotationElement)->classInfoIndex);
		break;
	case '@':
		walkAnnotations(1, &(((J9CfrAnnotationElementAnnotation *)annotationElement)->annotationValue), 0);
		break;
	case '[': {
		J9CfrAnnotationElementArray *array = (J9CfrAnnotationElementArray *)annotationElement;
		for (U_16 valuesIndex = 0; (valuesIndex < array->numberOfValues) && (OK == _buildResult); ++valuesIndex) {
			walkAnnotationElement(array->values[valuesIndex]);
		}
		break;
	}

	case 'D': /* fall thru */
	case 'J':
		_annotationRefersDoubleSlotEntry = true;
		 /* fall thru */
	case 'B': /* fall thru */
	case 'C': /* fall thru */
	case 'F': /* fall thru */
	case 'I': /* fall thru */
	case 'S': /* fall thru */
	case 'Z': /* fall thru */
		/* Fall through - in Java 7, the annotation data is preserved in class file format, including referenced constant pool entries */
	case 's':
		markConstantAsUsedByAnnotation(((J9CfrAnnotationElementPrimitive *)annotationElement)->constValueIndex);
		break;
	default:
		Trc_BCU_ClassFileOracle_walkAnnotationElement_UnknownTag((U_32)annotationElement->tag);
		_buildResult = UnknownAnnotation;
		break;
	}
}

void
ClassFileOracle::computeSendSlotCount(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ComputeSendSlotCount);

	U_16 descriptorIndex = _classFile->methods[methodIndex].descriptorIndex;
	U_16 count = getUTF8Length(descriptorIndex);
	U_8 * bytes = getUTF8Data(descriptorIndex);
	U_8 sendSlotCount = 0;

	for (U_16 index = 1; index < count; ++index) { /* 1 to skip the opening '(' */
		switch (bytes[index]) {
		case ')':
			_methodsInfo[methodIndex].sendSlotCount = sendSlotCount;
			return;
		case '[':
			/* skip all '['s */
			while ((index < count) && ('[' == bytes[index])) {
				++index;
			}
			if ((index >= count)
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
				|| (('L' != bytes[index]) && ('Q' != bytes[index]))
#else
				|| ('L' != bytes[index])
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			) {
				break;
			}
			/* fall through */
		case 'L':
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		case 'Q':
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			++index;
			while ((index < count) && (';' != bytes[index])) {
				++index;
			}
			break;
		case 'D':
			++sendSlotCount; /* double requires an extra send slot */
			break;
		case 'J':
			++sendSlotCount; /* long requires an extra send slot */
			break;
		default:
			/* any other primitive type */
			break;
		}
		++sendSlotCount;
	}
}

void
ClassFileOracle::walkMethodThrownExceptions(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodThrownExceptionsAnalysis);

	J9CfrAttributeExceptions *exceptions = _classFile->methods[methodIndex].exceptionsAttribute;
	if (NULL != exceptions) {
		U_16 exceptionsThrownCount = 0;
		U_16 exceptionsCount = exceptions->numberOfExceptions;

		for (U_16 exceptionIndex = 0; exceptionIndex < exceptionsCount; ++exceptionIndex) {
			U_16 cpIndex = exceptions->exceptionIndexTable[exceptionIndex];
			if (0 != cpIndex) {
				markClassNameAsReferenced(cpIndex);
				++exceptionsThrownCount;

			}
		}

		if (exceptionsThrownCount > 0) {
			_methodsInfo[methodIndex].exceptionsThrownCount = exceptionsThrownCount;
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasExceptionInfo;
		}
	}
}

void
ClassFileOracle::walkMethodCodeAttribute(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodCodeAttributeAnalysis);

	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;
	if (NULL != codeAttribute) {
		walkMethodCodeAttributeAttributes(methodIndex);
		walkMethodCodeAttributeCaughtExceptions(methodIndex);
		walkMethodCodeAttributeCode(methodIndex);

		if (0 != codeAttribute->exceptionTableLength) {
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasExceptionInfo;
		}
	}
}

void
ClassFileOracle::walkMethodMethodParametersAttribute(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodMethodParametersAttributeAnalysis);

	J9CfrAttributeMethodParameters *methodParametersAttribute = _classFile->methods[methodIndex].methodParametersAttribute;

	if (NULL != methodParametersAttribute) {
		for (U_8 methodParamsIndex = 0;
				(methodParamsIndex < methodParametersAttribute->numberOfMethodParameters) && (OK == _buildResult);
				++methodParamsIndex)
		{
			U_16 utfIndex = methodParametersAttribute->methodParametersIndexTable[methodParamsIndex];
			if  (J9_ARE_ANY_BITS_SET(methodParametersAttribute->flags[methodParamsIndex], ~CFR_ATTRIBUTE_METHOD_PARAMETERS_MASK)) {
				/* only CFR_ACC_FINAL | CFR_ACC_SYNTHETIC | CFR_ACC_MANDATED should be set */
				_methodsInfo[methodIndex].extendedModifiers |= CFR_METHOD_EXT_INVALID_CP_ENTRY;
			}
			if (0 != utfIndex) {
				bool cpEntryOkay = false;
				if (utfIndex <= _classFile->constantPoolCount) {
					UDATA cpTag = getCPTag(utfIndex);
					if (CFR_CONSTANT_Utf8 == cpTag) {
						markConstantUTF8AsReferenced(utfIndex);
						cpEntryOkay = true;
					}
				}
				if (!cpEntryOkay) {
					/* Mark the method as having bad methodParameters */
					methodParametersAttribute->methodParametersIndexTable[methodParamsIndex] = 0;
					_methodsInfo[methodIndex].extendedModifiers |= CFR_METHOD_EXT_INVALID_CP_ENTRY;
					Trc_BCU_MalformedMethodParameterAttribute(methodIndex);
				}
			}
		}
		/* PR 97987 keep parameters object to detect mismatch between parameters attribute and actual argument count */
		_methodsInfo[methodIndex].methodParametersAttribute = methodParametersAttribute;
		_methodsInfo[methodIndex].modifiers |= J9AccMethodHasMethodParameters;
	}

}

void
ClassFileOracle::throwGenericErrorWithCustomMsg(UDATA code, UDATA offset)
{
	_buildResult = OutOfMemory;
	PORT_ACCESS_FROM_PORT(_context->portLibrary());
	U_8* errorMsg = (U_8 *)j9mem_allocate_memory(sizeof(J9CfrError), J9MEM_CATEGORY_CLASSES);
	if (NULL != errorMsg) {
		_buildResult = GenericErrorCustomMsg;
		buildError((J9CfrError*)errorMsg, code, GenericErrorCustomMsg, offset);
		J9TranslationBufferSet* dlb = _context->javaVM()->dynamicLoadBuffers;
		/* avoid leaking memory if classFileError was not previously null. Do not free
		 * memory if _classFileBuffer from ROMClassBuilder is using the same address. */
		if ((NULL != dlb->classFileError) && (_romBuilderClassFileBuffer != dlb->classFileError)) {
			j9mem_free_memory(dlb->classFileError);
		}
		dlb->classFileError = errorMsg;
	}
}

void
ClassFileOracle::walkMethodCodeAttributeAttributes(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodCodeAttributeAttributesAnalysis);

	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;
	U_32 lineNumbersCount = 0;

	Trc_BCU_Assert_NotEquals(NULL, codeAttribute);

	U_16 attributesCount = codeAttribute->attributesCount;
	for (U_16 attributeIndex = 0; (attributeIndex < attributesCount) && (OK == _buildResult); ++attributeIndex) {
		J9CfrAttribute *attribute = codeAttribute->attributes[attributeIndex];

		switch (attribute->tag) {
		case CFR_ATTRIBUTE_StackMapTable: {
			/*
			 * Stack map table frames are variable sized and look like:
			 * 	StackMapTableFrame {
			 *		U_8 frameType
			 *		... rest
			 *	};
			 *
			 * frameType is a tag distinguishing 256 different types of frame that fall into 8 categories:
			 *	SAME
			 *	SAME_LOCALS_1_STACK
			 *	Reserved
			 *	SAME_LOCALS_1_STACK_EXTENDED
			 *	CHOP
			 *	SAME_EXTENDED
			 *	APPEND
			 *	FULL
			 *
			 * Each of these types is described in more detail below.
			 *
			 * Some frame types contain 1 or more TypeInfo entries. TypeInfo is a variable-length data type TODO add doc?
			 *
			 * stackMap->numberOfEntries is the number of StackMapFrames in stackMap->entries.
			 * stackMap->entries is a byte buffer containing the variable length StackMapFrames.
			 *
			 * framePointer is used to step through stackMap->entries.
			 * entryIndex is used to determine when we've read all the StackMapFrames.
			 */

			J9CfrAttributeStackMap * stackMap = (J9CfrAttributeStackMap *) attribute;
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasStackMap;

			Trc_BCU_Assert_Equals(NULL, _methodsInfo[methodIndex].stackMapFramesInfo);
			_methodsInfo[methodIndex].stackMapFramesInfo = (StackMapFrameInfo *) _bufferManager->alloc(stackMap->numberOfEntries * sizeof(StackMapFrameInfo));
			if (NULL == _methodsInfo[methodIndex].stackMapFramesInfo) {
				Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_OutOfMemory(stackMap->numberOfEntries * sizeof(StackMapFrameInfo));
				_buildResult = OutOfMemory;
				break;
			}
			memset(_methodsInfo[methodIndex].stackMapFramesInfo, 0, stackMap->numberOfEntries * sizeof(StackMapFrameInfo));
			_methodsInfo[methodIndex].stackMapFramesCount = U_16(stackMap->numberOfEntries);

			StackMapFrameInfo *stackMapFramesInfo = _methodsInfo[methodIndex].stackMapFramesInfo;
			U_8 *framePointer = stackMap->entries;
			U_16 entryCount = U_16(stackMap->numberOfEntries);
			for(U_16 entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
				NEXT_U8(stackMapFramesInfo[entryIndex].frameType, framePointer);

				if (CFR_STACKMAP_SAME_LOCALS_1_STACK > stackMapFramesInfo[entryIndex].frameType) { /* 0..63 */
					/* SAME frame - no extra data */
				} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_END > stackMapFramesInfo[entryIndex].frameType) { /* 64..127 */
					/*
					 * SAME_LOCALS_1_STACK {
					 * 		TypeInfo stackItems[1]
					 * };
					 */
					stackMapFramesInfo[entryIndex].stackItemsCount = 1;
					stackMapFramesInfo[entryIndex].stackItemsTypeInfo = framePointer;
					framePointer = walkStackMapSlots(framePointer, stackMapFramesInfo[entryIndex].stackItemsCount);
				} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED > stackMapFramesInfo[entryIndex].frameType) { /* 128..246 */
					/* Reserved frame types - no extra data */
					Trc_BCU_Assert_ShouldNeverHappen();
				} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_EXTENDED == stackMapFramesInfo[entryIndex].frameType) { /* 247 */
					/*
					 * SAME_LOCALS_1_STACK_EXTENDED {
					 *		U_16 offsetDelta
					 *		TypeInfo stackItems[1]
					 * };
					 */
					NEXT_U16(stackMapFramesInfo[entryIndex].offsetDelta, framePointer); /* Extract offsetDelta */
					stackMapFramesInfo[entryIndex].stackItemsCount = 1;
					stackMapFramesInfo[entryIndex].stackItemsTypeInfo = framePointer;
					framePointer = walkStackMapSlots(framePointer, stackMapFramesInfo[entryIndex].stackItemsCount);
				} else if (CFR_STACKMAP_SAME_EXTENDED > stackMapFramesInfo[entryIndex].frameType) { /* 248..250 */
					/*
					 * CHOP {
					 *		U_16 offsetDelta
					 * };
					 */
					NEXT_U16(stackMapFramesInfo[entryIndex].offsetDelta, framePointer); /* Extract offsetDelta */
				} else if (CFR_STACKMAP_SAME_EXTENDED == stackMapFramesInfo[entryIndex].frameType) { /* 251 */
					/*
					 * SAME_EXTENDED {
					 *		U_16 offsetDelta
					 * };
					 */
					NEXT_U16(stackMapFramesInfo[entryIndex].offsetDelta, framePointer); /* Extract offsetDelta */
				} else if (CFR_STACKMAP_FULL > stackMapFramesInfo[entryIndex].frameType) { /* 252..254 */
					/*
					 * APPEND {
					 *		U_16 offsetDelta
					 *		TypeInfo locals[frameType - CFR_STACKMAP_APPEND_BASE]
					 * };
					 */
					NEXT_U16(stackMapFramesInfo[entryIndex].offsetDelta, framePointer); /* Extract offsetDelta */
					stackMapFramesInfo[entryIndex].localsCount = stackMapFramesInfo[entryIndex].frameType - CFR_STACKMAP_APPEND_BASE;
					stackMapFramesInfo[entryIndex].localsTypeInfo = framePointer;
					framePointer = walkStackMapSlots(framePointer, stackMapFramesInfo[entryIndex].localsCount);
				} else if (CFR_STACKMAP_FULL == stackMapFramesInfo[entryIndex].frameType) { /* 255 */
					/*
					 * FULL {
					 *		U_16 offsetDelta
					 *		U_16 localsCount
					 *		TypeInfo locals[localsCount]
					 *		U_16 stackItemsCount
					 *		TypeInfo stackItems[stackItemsCount]
					 * };
					 */
					NEXT_U16(stackMapFramesInfo[entryIndex].offsetDelta, framePointer); /* Extract offsetDelta */
					NEXT_U16(stackMapFramesInfo[entryIndex].localsCount, framePointer); /* Extract localsCount */
					stackMapFramesInfo[entryIndex].localsTypeInfo = framePointer;
					framePointer = walkStackMapSlots(framePointer, stackMapFramesInfo[entryIndex].localsCount);
					NEXT_U16(stackMapFramesInfo[entryIndex].stackItemsCount, framePointer); /* Extract stackItemsCount */
					stackMapFramesInfo[entryIndex].stackItemsTypeInfo = framePointer;
					framePointer = walkStackMapSlots(framePointer, stackMapFramesInfo[entryIndex].stackItemsCount);
				}
			}
			break;
		}
		case CFR_ATTRIBUTE_LineNumberTable:
			if (_context->shouldPreserveLineNumbers()) {
				J9CfrAttributeLineNumberTable *lineNumberTable = (J9CfrAttributeLineNumberTable *) attribute;
				lineNumbersCount += lineNumberTable->lineNumberTableLength;
			}
			break;
		case CFR_ATTRIBUTE_LocalVariableTable:
			if (_context->shouldPreserveLocalVariablesInfo()) {
				/* There can be at most one entry per local variable, so we allocate the full table up-front and index by local variable index */
				if (NULL == _methodsInfo[methodIndex].localVariablesInfo) {
					_methodsInfo[methodIndex].localVariablesInfo = (LocalVariableInfo *) _bufferManager->alloc(codeAttribute->maxLocals * sizeof(LocalVariableInfo));
					if (NULL == _methodsInfo[methodIndex].localVariablesInfo) {
						Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_OutOfMemory(codeAttribute->maxLocals * sizeof(LocalVariableInfo));
						_buildResult = OutOfMemory;
						break;
					}
					memset(_methodsInfo[methodIndex].localVariablesInfo, 0, codeAttribute->maxLocals * sizeof(LocalVariableInfo));
				}

				J9CfrAttributeLocalVariableTable *localVariableTableAttribute = (J9CfrAttributeLocalVariableTable *) attribute;
				_methodsInfo[methodIndex].localVariablesCount += localVariableTableAttribute->localVariableTableLength;
				if (0 != localVariableTableAttribute->localVariableTableLength) {
					for (U_16 localVariableTableIndex = 0; localVariableTableIndex < localVariableTableAttribute->localVariableTableLength; ++localVariableTableIndex) {
						U_16 index = localVariableTableAttribute->localVariableTable[localVariableTableIndex].index;
						if (codeAttribute->maxLocals <= index) {
							Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_LocalVariableTableIndexOutOfBounds(
									index, codeAttribute->maxLocals, (U_32)getUTF8Length(_classFile->methods[methodIndex].nameIndex), getUTF8Data(_classFile->methods[methodIndex].nameIndex));

							throwGenericErrorWithCustomMsg(J9NLS_CFR_LVT_INDEX_OUTOFRANGE__ID, index);
							break;
						} else if (NULL == _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTableAttribute) {
							_methodsInfo[methodIndex].localVariablesInfo[index].localVariableTableAttribute = localVariableTableAttribute;
						} else if (localVariableTableAttribute != _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTableAttribute) {
							Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_DuplicateLocalVariableTable(
									(U_32)getUTF8Length(_classFile->methods[methodIndex].nameIndex), getUTF8Data(_classFile->methods[methodIndex].nameIndex), 
									localVariableTableAttribute, _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTableAttribute);
							_buildResult = GenericError;
							break;
						}
						markConstantUTF8AsReferenced(localVariableTableAttribute->localVariableTable[localVariableTableIndex].nameIndex);
						markConstantUTF8AsReferenced(localVariableTableAttribute->localVariableTable[localVariableTableIndex].descriptorIndex);
					}
				}
			}
			break;
		case CFR_ATTRIBUTE_LocalVariableTypeTable:
			if (_context->shouldPreserveLocalVariablesInfo()) {
				/* There can be at most one entry per local variable, so we allocate the full table up-front and index by local variable index */
				if (NULL == _methodsInfo[methodIndex].localVariablesInfo) {
					_methodsInfo[methodIndex].localVariablesInfo = (LocalVariableInfo *) _bufferManager->alloc(codeAttribute->maxLocals * sizeof(LocalVariableInfo));
					if (NULL == _methodsInfo[methodIndex].localVariablesInfo) {
						Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_OutOfMemory(codeAttribute->maxLocals * sizeof(LocalVariableInfo));
						_buildResult = OutOfMemory;
						break;
					}
					memset(_methodsInfo[methodIndex].localVariablesInfo, 0, codeAttribute->maxLocals * sizeof(LocalVariableInfo));
				}

				J9CfrAttributeLocalVariableTypeTable *localVariableTypeTableAttribute = (J9CfrAttributeLocalVariableTypeTable *) attribute;
				if (0 != localVariableTypeTableAttribute->localVariableTypeTableLength) {
					for (U_16 localVariableTypeTableIndex = 0; localVariableTypeTableIndex < localVariableTypeTableAttribute->localVariableTypeTableLength; ++localVariableTypeTableIndex) {
						J9CfrLocalVariableTypeTableEntry *lvttEntry = &(localVariableTypeTableAttribute->localVariableTypeTable[localVariableTypeTableIndex]);
						const U_16 index = lvttEntry->index;
						if (codeAttribute->maxLocals <= index) {
							Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_LocalVariableTypeTableIndexOutOfBounds(
									index, codeAttribute->maxLocals, (U_32)getUTF8Length(_classFile->methods[methodIndex].nameIndex), getUTF8Data(_classFile->methods[methodIndex].nameIndex));
							throwGenericErrorWithCustomMsg(J9NLS_CFR_LVTT_INDEX_OUTOFRANGE__ID, index);
							break;
						} else if (NULL == _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTypeTableAttribute) {
							_methodsInfo[methodIndex].localVariablesInfo[index].localVariableTypeTableAttribute = localVariableTypeTableAttribute;
						} else if (localVariableTypeTableAttribute != _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTypeTableAttribute) {
							Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_DuplicateLocalVariableTypeTable(
									(U_32)getUTF8Length(_classFile->methods[methodIndex].nameIndex), getUTF8Data(_classFile->methods[methodIndex].nameIndex), 
									localVariableTypeTableAttribute, _methodsInfo[methodIndex].localVariablesInfo[index].localVariableTypeTableAttribute);
							_buildResult = GenericError;
							break;
						}

						/* 4.7.14: There may be no more than one LocalVariableTypeTable attribute per local variable in the attributes table of a Code attribute. 
						 * The entry is unique with its startPC, length, and index. */
						for (U_16 localVariableTypeTableCompareIndex = 0; localVariableTypeTableCompareIndex < localVariableTypeTableIndex; ++localVariableTypeTableCompareIndex) {
							J9CfrLocalVariableTypeTableEntry *lvttCompareEntry = &(localVariableTypeTableAttribute->localVariableTypeTable[localVariableTypeTableCompareIndex]);
							if ((lvttEntry->startPC == lvttCompareEntry->startPC)
								&& (lvttEntry->length == lvttCompareEntry->length)
								&& (lvttEntry->nameIndex == lvttCompareEntry->nameIndex)
								&& (lvttEntry->signatureIndex == lvttCompareEntry->signatureIndex) 
								&& (index == lvttCompareEntry->index) 
							) {
								throwGenericErrorWithCustomMsg(J9NLS_CFR_LVTT_DUPLICATE__ID, index);
								break;
							}
						}
						markConstantUTF8AsReferenced(localVariableTypeTableAttribute->localVariableTypeTable[localVariableTypeTableIndex].signatureIndex);
					}
				}
			}
			break;
		case CFR_ATTRIBUTE_RuntimeVisibleTypeAnnotations: {
			J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotations = (J9CfrAttributeRuntimeVisibleTypeAnnotations *)attribute;
			if (0 == typeAnnotations->rawDataLength) { /* rawDataLength non-zero in case of error in the attribute */
				walkTypeAnnotations(typeAnnotations->numberOfAnnotations, typeAnnotations->typeAnnotations);
			}
			_methodsInfo[methodIndex].codeTypeAnnotationsAttribute = typeAnnotations;
			/* J9AccMethodHasExtendedModifiers in the modifiers is set when the ROM class is written */
			_methodsInfo[methodIndex].extendedModifiers |= CFR_METHOD_EXT_HAS_CODE_TYPE_ANNOTATIONS;
			break;
		}
		default:
			Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_UnknownAttribute((U_32)attribute->tag, (U_32)getUTF8Length(attribute->nameIndex), getUTF8Data(attribute->nameIndex), attribute->length);
			break;
		}
	}

	/* Verify that each LVTT entry has a matching local variable. Since there is no guaranteed order
	 * for code attributes, this check must be performed after all attributes are processed.
	 * 
	 * According to the JVM spec: "Each entry in the local_variable_type_table array ... 
	 * indicates the index into the local variable array of the current frame at which 
	 * that local variable can be found."
	 * 
	 * While multiple LocalVariableTypeTable attributes may exist according to the spec, upon observation 
	 * it is the common case for 'javac' to generate only one attribute per method. To take advantage of this 
	 * I tracked the last LVTT attribute that has been verified so the second loop search is not repeated. 
	 * Because of this for most cases the second loop will only be executed once.
	 * 
	 * It is also common to see LVT and LVTT entries in the same order though the spec makes no ordering guaruntees. 
	 * To take advantage of this each search for an LVT match starts from the index where the previous match was 
	 * found saving iterations.
	 * 
	 * With these two optimizations the common runtime for this verification step should be:
	 * maxLocals + localVariableTypeTableLength + localVariableTableLength
	 */
	if (_context->shouldPreserveLocalVariablesInfo() && (NULL != _methodsInfo[methodIndex].localVariablesInfo)) {
		J9CfrAttributeLocalVariableTypeTable *lastLVTTAttribute = NULL; /* track last processed LVTT attribute */

		for (UDATA varIndex = 0; varIndex < codeAttribute->maxLocals; varIndex++) {
			J9CfrAttributeLocalVariableTable *localVariableTableAttribute = _methodsInfo[methodIndex].localVariablesInfo[varIndex].localVariableTableAttribute;
			J9CfrAttributeLocalVariableTypeTable *localVariableTypeTableAttribute = _methodsInfo[methodIndex].localVariablesInfo[varIndex].localVariableTypeTableAttribute;

			/* This may occur if the variable type does not require signature information, or if there is no entry for this variable. */
			if ((NULL == localVariableTypeTableAttribute) || (NULL == localVariableTableAttribute)) {
				continue;
			}

			/* if LVTT has been processed previously, skip to the next variable */
			if (NULL == lastLVTTAttribute) {
				lastLVTTAttribute = localVariableTypeTableAttribute;
			} else if (localVariableTypeTableAttribute == lastLVTTAttribute) {
				continue;
			}

			/* Verify that each entry in the LocalVariableTypeTable has a match */
			U_16 lvtIndex = 0; /* start at the index of the last LVT match since LVT and LVTTs are likely in the same order. */
			for (U_16 lvttIndex = 0; lvttIndex < localVariableTypeTableAttribute->localVariableTypeTableLength; lvttIndex++) {
				J9CfrLocalVariableTypeTableEntry *lvttEntry = &(localVariableTypeTableAttribute->localVariableTypeTable[lvttIndex]);
				UDATA foundMatch = FALSE;

				/* Search for match in LVT attribute with matching index. */
				for (U_16 lvtCount = 0; lvtCount < localVariableTableAttribute->localVariableTableLength; lvtCount++) {
					J9CfrLocalVariableTableEntry *lvtEntry = &(localVariableTableAttribute->localVariableTable[lvtIndex]);

					/* Update index for next iteration, rolling over to 0 if necessary. */
					lvtIndex = (lvtIndex + 1) % localVariableTableAttribute->localVariableTableLength;

					if ((lvttEntry->startPC == lvtEntry->startPC)
						&& (lvttEntry->length == lvtEntry->length)
						&& (lvttEntry->nameIndex == lvtEntry->nameIndex)
						&& (lvttEntry->index == lvtEntry->index)
					) {
						foundMatch = TRUE;
						break;
					}
				}

				/* throw error if there is no variable match. */
				if (!foundMatch) {
					throwGenericErrorWithCustomMsg(J9NLS_CFR_LVTT_DOES_NOT_MATCH_LVT__ID, lvttIndex);
				}
			}
		}
	}
	
	if ((OK == _buildResult) && (0 != lineNumbersCount)) {
		ROMCLASS_VERBOSE_PHASE_HOT(_context, CompressLineNumbers);
		compressLineNumberTable(methodIndex, lineNumbersCount);
	}
}

int
ClassFileOracle::compareLineNumbers(const void *left, const void *right)
{
	J9CfrLineNumberTableEntry *leftNumber = (J9CfrLineNumberTableEntry *)left;
	J9CfrLineNumberTableEntry *rightNumber = (J9CfrLineNumberTableEntry *)right;

	return int(leftNumber->startPC - rightNumber->startPC);
}

void
ClassFileOracle::compressLineNumberTable(U_16 methodIndex, U_32 lineNumbersCount)
{
	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;
	U_16 attributesCount = codeAttribute->attributesCount;
	MethodInfo *method = &(_methodsInfo[methodIndex]);

    /* maximum possible size of compressed data, compressLineNumbers may use a maximum
     * of 5 bytes for compressed data, the maximum memory needed will be of 5 bytes per line number.
     */
    U_8 *lineNumbersInfoCompressed = (U_8*)(_bufferManager->alloc(lineNumbersCount * 5));
    U_8 *lineNumbersInfoCompressedInitial = lineNumbersInfoCompressed;
    if (NULL == lineNumbersInfoCompressed) {
		Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_OutOfMemory(lineNumbersCount * 5);
		_buildResult = OutOfMemory;
	} else {
		J9CfrLineNumberTableEntry * lastLineNumberTableEntry = NULL;

		for (U_16 attributeIndex = 0; attributeIndex < attributesCount; ++attributeIndex) {
			J9CfrAttribute *attribute = codeAttribute->attributes[attributeIndex];
			if (CFR_ATTRIBUTE_LineNumberTable == attribute->tag) {
				J9CfrAttributeLineNumberTable *lineNumberTable = (J9CfrAttributeLineNumberTable *) attribute;
				if (!compressLineNumbers(lineNumberTable->lineNumberTable, lineNumberTable->lineNumberTableLength, lastLineNumberTableEntry, &lineNumbersInfoCompressed)) {
					/* the line numbers are not sorted, sort them */
					sortAndCompressLineNumberTable(methodIndex, lineNumbersCount, lineNumbersInfoCompressedInitial);
					return;
				}
				lastLineNumberTableEntry = &lineNumberTable->lineNumberTable[lineNumberTable->lineNumberTableLength - 1];
			}
		}
		method->lineNumbersInfoCompressed = lineNumbersInfoCompressedInitial;
		method->lineNumbersCount = lineNumbersCount;
		method->lineNumbersInfoCompressedSize = (U_32)(lineNumbersInfoCompressed - lineNumbersInfoCompressedInitial);

		/* Reclaim the rest of the buffer lineNumbersInfoCompressed */
		_bufferManager->reclaim(lineNumbersInfoCompressedInitial, method->lineNumbersInfoCompressedSize);
	}
}

void
ClassFileOracle::sortAndCompressLineNumberTable(U_16 methodIndex, U_32 lineNumbersCount, U_8 *lineNumbersInfoCompressedInitial)
{
	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;
	U_16 attributesCount = codeAttribute->attributesCount;
	MethodInfo *method = &(_methodsInfo[methodIndex]);

	U_8 *lineNumbersInfoCompressed;
    UDATA lineNumbersInfoSize = lineNumbersCount * sizeof (J9CfrLineNumberTableEntry);
    J9CfrLineNumberTableEntry *lineNumbersInfo = (J9CfrLineNumberTableEntry*)(_bufferManager->alloc(lineNumbersInfoSize));
    lineNumbersInfoCompressed = lineNumbersInfoCompressedInitial;
    if (NULL == lineNumbersInfo) {
		Trc_BCU_ClassFileOracle_walkMethodCodeAttributeAttributes_OutOfMemory(lineNumbersInfoSize);
		_buildResult = OutOfMemory;
	} else {
		sortLineNumberTable(methodIndex, lineNumbersInfo);

		/* Compress the line numbers */
		if (!compressLineNumbers(lineNumbersInfo, lineNumbersCount, NULL, &lineNumbersInfoCompressed)) {
			/* The pcOffset should be sorted by now */
			Trc_BCU_Assert_ShouldNeverHappen();
		}

		method->lineNumbersInfoCompressed = lineNumbersInfoCompressedInitial;
		method->lineNumbersCount = lineNumbersCount;
		method->lineNumbersInfoCompressedSize = (U_32)(lineNumbersInfoCompressed - lineNumbersInfoCompressedInitial);
		_bufferManager->reclaim(lineNumbersInfo, lineNumbersInfoSize);
	}
}
void
ClassFileOracle::sortLineNumberTable(U_16 methodIndex, J9CfrLineNumberTableEntry *lineNumbersInfo) {
	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;
	U_16 attributesCount = codeAttribute->attributesCount;

	U_32 lineNumbersIndex = 0;
	U_32 lastPC = 0;
	/* Used by ROMClassBuilder to not sort the line if they were already sorted */
	BOOLEAN sorted = TRUE;
	for (U_16 attributeIndex = 0; attributeIndex < attributesCount; ++attributeIndex) {
		J9CfrAttribute *attribute = codeAttribute->attributes[attributeIndex];

		if (CFR_ATTRIBUTE_LineNumberTable == attribute->tag) {
			J9CfrAttributeLineNumberTable *lineNumberTable = (J9CfrAttributeLineNumberTable *) attribute;
			for (U_16 lineNumberTableIndex = 0; lineNumberTableIndex < lineNumberTable->lineNumberTableLength; ++lineNumberTableIndex) {
				if (lineNumberTable->lineNumberTable[lineNumberTableIndex].startPC < lastPC) {
					sorted = FALSE;
				}
				lastPC = lineNumbersInfo[lineNumbersIndex].startPC = lineNumberTable->lineNumberTable[lineNumberTableIndex].startPC;
				lineNumbersInfo[lineNumbersIndex].lineNumber = lineNumberTable->lineNumberTable[lineNumberTableIndex].lineNumber;
				++lineNumbersIndex;
			}
		}
	}

	if (!sorted) {
		/* Sort the line numbers */
		J9_SORT(lineNumbersInfo, lineNumbersIndex, sizeof(J9CfrLineNumberTableEntry), &ClassFileOracle::compareLineNumbers);
	}
}

void
ClassFileOracle::walkMethodCodeAttributeCaughtExceptions(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodCodeAttributeCaughtExceptionsAnalysis);
	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;

	Trc_BCU_Assert_NotEquals(NULL, codeAttribute);

	for (U_16 exceptionTableIndex = 0; exceptionTableIndex < codeAttribute->exceptionTableLength; ++exceptionTableIndex) {
		U_16 cpIndex = codeAttribute->exceptionTable[exceptionTableIndex].catchType;
		if (0 != cpIndex) {
			markClassAsReferenced(cpIndex);
		}
	}
}

void
ClassFileOracle::addBytecodeFixupEntry(BytecodeFixupEntry *entry, U_32 codeIndex, U_16 cpIndex, U_8 type)
{
	entry->codeIndex = codeIndex;
	entry->cpIndex = cpIndex;
	entry->type = type;
}

const U_8 PARAM_VOID	= 0;
const U_8 PARAM_BOOLEAN	= 1;
const U_8 PARAM_BYTE	= 2;
const U_8 PARAM_CHAR	= 3;
const U_8 PARAM_SHORT	= 4;
const U_8 PARAM_FLOAT	= 5;
const U_8 PARAM_INT		= 6;
const U_8 PARAM_DOUBLE	= 7;
const U_8 PARAM_LONG	= 8;
const U_8 PARAM_OBJECT	= 9;

#define PARAM_U8() code[codeIndex + 1]
#define PARAM_U16() (U_16(code[codeIndex + 1]) << 8) | U_16(code[codeIndex + 2])
#define PARAM_I16() I_16(PARAM_U16())

#if defined(J9VM_ENV_LITTLE_ENDIAN)

/*
 * Swaps the I_32 in-place, returning it.
 */
static VMINLINE I_32
swapI32(U_8 *code, U_32 codeIndex)
{
	I_32 *dest = (I_32 *)&code[codeIndex];
	I_32 value = I_32(U_32(code[codeIndex] << 24) | U_32(code[codeIndex + 1] << 16) | U_32(code[codeIndex + 2] << 8) | U_32(code[codeIndex + 3]));
	*dest = value;
	return value;
}

#define SWAP_I32_GET(index) swapI32(code, index)
#define SWAP_I32(index) SWAP_I32_GET(index)
#else
/*
 * We use separate macros here because the compiler is not guaranteed to optimize out the pointer
 * dereference (as doing so may result in a change of program behaviour in the compiler's view -
 * for example a GPF may not happen).
 */
#define SWAP_I32_GET(index) *(I_32*)&code[index]
#define SWAP_I32(index)
#endif

void
ClassFileOracle::walkMethodCodeAttributeCode(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileMethodCodeAttributeCodeAnalysis);
	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;

	static const U_8 returnArgCharConversion[] = {
			0,			PARAM_INT,		PARAM_INT,		PARAM_DOUBLE,
			0,			PARAM_FLOAT,	0,				0,
			PARAM_INT,	PARAM_LONG,		0,				PARAM_OBJECT,
			0,			0,				0,				0,
			0,			0,				PARAM_INT,		0,
			0,			PARAM_VOID,		0,				0,
			0,			PARAM_INT
	};

	static const U_8 returnBytecodeConversion[] = {
			PARAM_INT,		/* ireturn */
			PARAM_LONG,		/* lreturn */
			PARAM_FLOAT,	/* freturn */
			PARAM_DOUBLE,	/* dreturn */
			PARAM_OBJECT,	/* areturn */
			PARAM_VOID		/*  return */
	};

	Trc_BCU_Assert_NotEquals(NULL, codeAttribute);

	U_32 step = 0;
	U_16 cpIndex = 0;
	U_32 branchCount = 1;

	U_16 methodDescriptorIndex = _classFile->methods[methodIndex].descriptorIndex;
	U_8 *methodDescriptorData = getUTF8Data(methodDescriptorIndex);
	U_16 methodDescriptorLength = getUTF8Length(methodDescriptorIndex);
	U_8 returnType = methodDescriptorData[methodDescriptorLength - 1];
	if (( returnType == ';') || (methodDescriptorData[methodDescriptorLength - 2] == '[')) {
		returnType = PARAM_OBJECT;
	} else {
		returnType = returnArgCharConversion[returnType - 'A'];
	}

	/* In the worst case scenario, all bytecodes are LDCs and thus half the bytes will need fixup entries. */
	UDATA maxFixupTableSize = (codeAttribute->codeLength / 2) * sizeof(BytecodeFixupEntry);
	BytecodeFixupEntry *fixupTable = (BytecodeFixupEntry *) _bufferManager->alloc(maxFixupTableSize);
	if (NULL == fixupTable) {
		_buildResult = OutOfMemory;
		return;
	}
	BytecodeFixupEntry *entry = fixupTable;

	U_8 *code = codeAttribute->code;
	for (U_32 codeIndex = 0; codeIndex < codeAttribute->codeLength; codeIndex += step) { /* NOTE codeIndex is modified below for CFR_BC_tableswitch and CFR_BC_lookupswitch */

		U_8 sunInstruction = code[codeIndex];

		step = sunJavaInstructionSizeTable[sunInstruction];

		/* Unknown bytecodes should have been detected by the static verifier. */
		Trc_BCU_Assert_SupportedByteCode(sunInstruction);

		/* TODO: A lot of cases in this switch are very similar, can we use a lookup table to match them faster? */
		switch (sunInstruction) {
		case CFR_BC_ireturn:
		case CFR_BC_freturn:
		case CFR_BC_areturn:
		case CFR_BC_lreturn:
		case CFR_BC_dreturn:
		case CFR_BC_return:
			if (returnType == returnBytecodeConversion[sunInstruction - CFR_BC_ireturn]) {
				code[codeIndex] = JBgenericReturn;
			} else if (returnType == PARAM_VOID) {
				code[codeIndex] = JBreturn1;
			} else {
				code[codeIndex] = JBreturn0;
			}
			break;

		case CFR_BC_aload_0:
			if (CFR_BC_getfield == code[codeIndex + 1]) {
				code[codeIndex] = JBaload0getfield;
			}
			break;

		case CFR_BC_ldc:
			cpIndex = PARAM_U8();
			if (isConstantInteger0(cpIndex)) {
				/* Don't allow constant int of 0, use iconst_0 - for string/class resolution later */
				code[codeIndex] = JBnop;
				code[codeIndex + 1] = JBiconst0;
			} else if (isConstantFloat0(cpIndex)) {
				/* Don't allow constant float of 0, use fconst_0 - for string/class resolution later */
				code[codeIndex] = JBnop;
				code[codeIndex + 1] = JBfconst0;
			} else {
				markConstantAsUsedByLDC(U_8(cpIndex));
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::LDC);
				markConstantBasedOnCpType(cpIndex, true);
			}
			break;

		case CFR_BC_wide: {
			U_8 nextInstruction = code[codeIndex + 1];
			/* byteswap and move up the U_16 local variable index */
#if !defined(J9VM_ENV_LITTLE_ENDIAN)
			code[codeIndex + 1] = code[codeIndex + 2];
			code[codeIndex + 2] = code[codeIndex + 3];
#else
			code[codeIndex + 1] = code[codeIndex + 3];
			/* Don't need to do: code[codeIndex + 2] = code[codeIndex + 2]; */
#endif
			if (CFR_BC_iinc == nextInstruction) {
				code[codeIndex + 0] = JBiincw;
				/* byteswap and move up the U_16 increment */
#if !defined(J9VM_ENV_LITTLE_ENDIAN)
				code[codeIndex + 3] = code[codeIndex + 4];
				code[codeIndex + 4] = code[codeIndex + 5];
#else
				code[codeIndex + 3] = code[codeIndex + 5];
				/* Don't need to do: code[codeIndex + 4] = code[codeIndex + 4]; */
#endif
				code[codeIndex + 5] = JBnop;
			} else {
				if (CFR_BC_istore <= nextInstruction) {
					/* Stores - assumes JB?storew in same order as CFR_BC_?store */
					code[codeIndex + 0] = (nextInstruction - CFR_BC_istore) + JBistorew;
				} else {
					/* Loads - assumes JB?loadw in same order as CFR_BC_?load */
					code[codeIndex + 0] = (nextInstruction - CFR_BC_iload) + JBiloadw;
				}
				code[codeIndex + 3] = JBnop;
			}
			/* Wide instruction are twice as wide as the original instructions */
			step = sunJavaInstructionSizeTable[nextInstruction] * 2;
			break;
		}

		case CFR_BC_ldc_w:
			cpIndex = PARAM_U16();
			if (isConstantInteger0(cpIndex)) {
				/* Don't allow constant int of 0, use iconst_0 - for string/class resolution later */
				code[codeIndex + 0] = JBnop;
				code[codeIndex + 1] = JBiconst0;
				code[codeIndex + 2] = JBnop;
			} else if (isConstantFloat0(cpIndex)) {
				/* Don't allow constant float of 0, use fconst_0 - for string/class resolution later */
				code[codeIndex + 0] = JBnop;
				code[codeIndex + 1] = JBfconst0;
				code[codeIndex + 2] = JBnop;
			} else {
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::REFERENCED);
				markConstantBasedOnCpType(cpIndex, true);
			}
			break;

		case CFR_BC_ldc2_w:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::LDC2W);

			if (isConstantLong(cpIndex)) {
				code[codeIndex + 0] = JBldc2lw;
				markConstantAsUsedByLDC2W(cpIndex);
			} else if (isConstantDouble(cpIndex)) {
				code[codeIndex + 0] = JBldc2dw;
				markConstantAsUsedByLDC2W(cpIndex);
			} else if (isConstantDynamic(cpIndex)) {
				code[codeIndex + 0] = constantDynamicType(cpIndex);
				markConstantDynamicAsReferenced(cpIndex);
			} else {
				Trc_BCU_Assert_ShouldNeverHappen();
			}
			break;

		case CFR_BC_tableswitch: {
			codeIndex += 4 - (codeIndex & 3); /* step past instruction + pad */
			SWAP_I32(codeIndex);
			codeIndex += sizeof(I_32); /* default offset */
			I_32 low = SWAP_I32_GET(codeIndex);
			codeIndex += sizeof(I_32);
			I_32 high = SWAP_I32_GET(codeIndex);
			codeIndex += sizeof(I_32);
			I_32 noffsets = high - low + 1;
#if defined(J9VM_ENV_LITTLE_ENDIAN)
			for (U_32 offsetsCount = high - low + 1; 0 != offsetsCount; --offsetsCount) {
				SWAP_I32(codeIndex);
				codeIndex += sizeof(I_32);
			}
#else
			codeIndex += noffsets * sizeof(I_32);
#endif
			step = 0; /* codeIndex increment has been handled already */
			branchCount += noffsets + 1;
			break;
		}

		case CFR_BC_lookupswitch: {
			codeIndex += 4 - (codeIndex & 3); /* step past instruction + pad */
			SWAP_I32(codeIndex);
			codeIndex += sizeof(I_32); /* default offset */
			I_32 npairs = SWAP_I32_GET(codeIndex);
			codeIndex += sizeof(I_32);
#if defined(J9VM_ENV_LITTLE_ENDIAN)
			for (I_32 count = npairs; 0 != count; --count) {
				SWAP_I32(codeIndex);
				codeIndex += sizeof(I_32);
				SWAP_I32(codeIndex);
				codeIndex += sizeof(I_32);
			}
#else
			codeIndex += npairs * 2 * sizeof(I_32);
#endif
			step = 0; /* codeIndex increment has been handled already */
			branchCount += npairs + 1;
			break;
		}

		case CFR_BC_invokevirtual:
			UDATA methodHandleInvocation;
			cpIndex = PARAM_U16();

			methodHandleInvocation = shouldConvertInvokeVirtualToMethodHandleBytecodeForMethodRef(cpIndex);
			if (methodHandleInvocation == CFR_BC_invokehandlegeneric) {
				code[codeIndex + 0] = CFR_BC_invokehandlegeneric;
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_HANDLEGENERIC);
				markMethodRefAsUsedByInvokeHandleGeneric(cpIndex);
				_methodsInfo[methodIndex].modifiers |= J9AccMethodHasMethodHandleInvokes;
			} else if (methodHandleInvocation == CFR_BC_invokehandle) {
				code[codeIndex + 0] = CFR_BC_invokehandle;
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_HANDLEEXACT);
				markMethodRefAsUsedByInvokeHandle(cpIndex);
				_methodsInfo[methodIndex].modifiers |= J9AccMethodHasMethodHandleInvokes;
			} else if (shouldConvertInvokeVirtualToInvokeSpecialForMethodRef(cpIndex)) {
				code[codeIndex + 0] = CFR_BC_invokespecial;
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_SPECIAL);
				markMethodRefAsUsedByInvokeSpecial(cpIndex);
			} else {
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_VIRTUAL);
				markMethodRefAsUsedByInvokeVirtual(cpIndex);
			}
			break;

		case CFR_BC_invokeinterface:
			cpIndex = PARAM_U16();
			markMethodRefAsUsedByInvokeInterface(cpIndex);
			addBytecodeFixupEntry(entry++, codeIndex + 3, cpIndex, ConstantPoolMap::INVOKE_INTERFACE);
			code[codeIndex + 0] = JBinvokeinterface2;
			code[codeIndex + 1] = JBnop;
			code[codeIndex + 2] = JBinvokeinterface;
			break;

		case CFR_BC_invokedynamic:
			cpIndex = PARAM_U16();
			markInvokeDynamicInfoAsUsedByInvokeDynamic(cpIndex);
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_DYNAMIC);
			// TODO slam in JBnop2 to code[codeIndex + 3]?
			_methodsInfo[methodIndex].modifiers |= J9AccMethodHasMethodHandleInvokes;
			break;

		case CFR_BC_putfield:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::PUT_FIELD);
			markFieldRefAsUsedByPutField(cpIndex);
			break;
		case CFR_BC_invokespecial:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_SPECIAL);
			markMethodRefAsUsedByInvokeSpecial(cpIndex);
			break;
		case CFR_BC_putstatic:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::PUT_STATIC);
			markFieldRefAsUsedByPutStatic(cpIndex);
			break;
		case CFR_BC_getfield:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::GET_FIELD);
			markFieldRefAsUsedByGetField(cpIndex);
			break;
		case CFR_BC_invokestatic:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INVOKE_STATIC);
			markMethodRefAsUsedByInvokeStatic(cpIndex);
			break;
		case CFR_BC_getstatic:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::GET_STATIC);
			markFieldRefAsUsedByGetStatic(cpIndex);
			break;
		case CFR_BC_new:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::NEW);
			markClassAsUsedByNew(cpIndex);
			if (CFR_BC_dup == code[codeIndex + 3]) {
				code[codeIndex] = JBnewdup;
			}
			break;
		case CFR_BC_anewarray:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::ANEW_ARRAY);
			markClassAsUsedByANewArray(cpIndex);
			break;
		case CFR_BC_multianewarray:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::MULTI_ANEW_ARRAY);
			markClassAsUsedByMultiANewArray(cpIndex);
			break;
		case CFR_BC_checkcast:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::CHECK_CAST);
			markClassAsUsedByCheckCast(cpIndex);
			break;
		case CFR_BC_instanceof:
			cpIndex = PARAM_U16();
			addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::INSTANCE_OF);
			markClassAsUsedByInstanceOf(cpIndex);
			break;

#if defined(J9VM_ENV_LITTLE_ENDIAN)
		case CFR_BC_sipush: {
			/* byteswap U_16 */
			U_8 temp = code[codeIndex + 1];
			code[codeIndex + 1] = code[codeIndex + 2];
			code[codeIndex + 2] = temp;
			break;
		}
#endif

		case CFR_BC_ifeq:
			/* Fall through */
		case CFR_BC_ifne:
			/* Fall through */
		case CFR_BC_iflt:
			/* Fall through */
		case CFR_BC_ifge:
			/* Fall through */
		case CFR_BC_ifgt:
			/* Fall through */
		case CFR_BC_ifle:
			/* Fall through */
		case CFR_BC_if_icmplt:
			/* Fall through */
		case CFR_BC_if_icmpge:
			/* Fall through */
		case CFR_BC_if_icmpgt:
			/* Fall through */
		case CFR_BC_if_icmple:
			/* Fall through */
		case CFR_BC_if_icmpeq:
			/* Fall through */
		case CFR_BC_if_icmpne:
			/* Fall through */
		case CFR_BC_if_acmpeq:
			/* Fall through */
		case CFR_BC_if_acmpne:
			/* Fall through */
		case CFR_BC_ifnull:
			/* Fall through */
		case CFR_BC_ifnonnull:
			++branchCount;
			/* Fall through */

		case CFR_BC_goto: {
#if defined(J9VM_ENV_LITTLE_ENDIAN)
			U_8 temp = code[codeIndex + 1];
			code[codeIndex + 1] = code[codeIndex + 2];
			code[codeIndex + 2] = temp;
#endif
			if (0 == (_methodsInfo[methodIndex].modifiers & J9AccMethodHasBackwardBranches)) {
				I_32 offset = I_32(*(I_16*)&code[codeIndex + 1]);
				if (offset <= 0) {
					/* The branch wasn't positive, so mark the method as containing backward branches. */
					_methodsInfo[methodIndex].modifiers |= J9AccMethodHasBackwardBranches;
				}
			}
			break;
		}

		case CFR_BC_goto_w: {
			SWAP_I32(codeIndex + 1);
			if (0 == (_methodsInfo[methodIndex].modifiers & J9AccMethodHasBackwardBranches)) {
				I_32 offset = *(I_32*)&code[codeIndex + 1];
				if (offset <= 0) {
					/* The branch wasn't positive, so mark the method as containing backward branches. */
					_methodsInfo[methodIndex].modifiers |= J9AccMethodHasBackwardBranches;
				}
			}
			break;
		}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		case CFR_BC_defaultvalue:
			if (_classFile->majorVersion >= VALUE_TYPES_MAJOR_VERSION) {
				cpIndex = PARAM_U16();
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::DEFAULT_VALUE);
				markClassAsUsedByDefaultValue(cpIndex);
			}
			break;
		case CFR_BC_withfield:
			if (_classFile->majorVersion >= VALUE_TYPES_MAJOR_VERSION) {
				cpIndex = PARAM_U16();
				addBytecodeFixupEntry(entry++, codeIndex + 1, cpIndex, ConstantPoolMap::WITH_FIELD);
				markFieldRefAsUsedByWithField(cpIndex);
			}
			break;
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

		default:
			/* Do nothing */
			break;
		}
	}

	branchCount += _classFile->methods[methodIndex].codeAttribute->exceptionTableLength;
	if (_maxBranchCount < branchCount) {
		_maxBranchCount = branchCount;
	}

	/* Let the buffer manager reclaim the unused part of the fixup table. */
	UDATA actualFixupTableSize = UDATA(entry) - UDATA(fixupTable);
	_bufferManager->reclaim(fixupTable, actualFixupTableSize);
	_methodsInfo[methodIndex].byteCodeFixupTable = fixupTable;
	_methodsInfo[methodIndex].byteCodeFixupCount = U_32(actualFixupTableSize / sizeof(BytecodeFixupEntry));
	_methodsInfo[methodIndex].isByteCodeFixupDone = false;
}
#undef SWAP_I32
#undef SWAP_I32_GET
#undef PARAM_I16
#undef PARAM_U16
#undef PARAM_U8

U_8 *
ClassFileOracle::walkStackMapSlots(U_8 *framePointer, U_16 typeInfoCount)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ClassFileStackMapSlotsAnalysis);
	U_8 typeInfoType;
	U_16 cpIndex;
	U_16 offset;

	/*
	 * Type info entries are variable sized and distinguished by a 8-bit type tag.
	 * Most entry types contain only the tag. The exceptions are described below.
	 */
	for(U_16 typeInfoIndex = 0; typeInfoIndex < typeInfoCount; ++typeInfoIndex) {
		NEXT_U8(typeInfoType, framePointer); /* Extract typeInfoType */
		switch (typeInfoType) {
		case CFR_STACKMAP_TYPE_OBJECT:
			/*
			 * OBJECT {
			 *		U_8 type
			 * 		U_16 cpIndex
			 * };
			 */
			NEXT_U16(cpIndex, framePointer); /* Extract cpIndex */
			markClassAsReferenced(cpIndex);
			break;
		case CFR_STACKMAP_TYPE_NEW_OBJECT:
			/*
			 * NEW_OBJECT {
			 *		U_8 type
			 * 		U_16 offset
			 * };
			 */
			NEXT_U16(offset, framePointer); /* Extract offset */
			break;
		default:
			/* do nothing */
			break;
		}
	}

	return framePointer;
}

bool
ClassFileOracle::methodIsFinalize(U_16 methodIndex, bool isForwarder)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsFinalize);

#define FINALIZE_NAME "finalize"
#define FINALIZE_SIG "()V"
	if (
			(0 == (_classFile->methods[methodIndex].accessFlags & CFR_ACC_STATIC))
			&& isUTF8AtIndexEqualToString(_classFile->methods[methodIndex].descriptorIndex, FINALIZE_SIG, sizeof(FINALIZE_SIG))
			&& isUTF8AtIndexEqualToString(_classFile->methods[methodIndex].nameIndex, FINALIZE_NAME, sizeof(FINALIZE_NAME))
		) {
		return true;
	}
#undef FINALIZE_NAME
#undef FINALIZE_SIG

	return false;
}



bool
ClassFileOracle::methodIsEmpty(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsEmpty);
	if (0 == (_classFile->methods[methodIndex].accessFlags & (CFR_ACC_ABSTRACT | CFR_ACC_NATIVE | CFR_ACC_SYNCHRONIZED))) {
		U_8 instruction = _classFile->methods[methodIndex].codeAttribute->code[0];
		if ((CFR_BC_ireturn <= instruction) && (instruction <= CFR_BC_return)) {
			return true;
		}
	}
	return false;
}

bool
ClassFileOracle::methodIsObjectConstructor(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsObjectConstructor);

	if ((0 == _classFile->superClass) &&
		(0 == (_classFile->methods[methodIndex].accessFlags & (CFR_ACC_ABSTRACT | CFR_ACC_STATIC | CFR_ACC_PRIVATE | CFR_ACC_SYNCHRONIZED))) &&
		('<' == getUTF8Data(_classFile->methods[methodIndex].nameIndex)[0]))
	{
		return true;
	}

	return false;
}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
/**
 * Determine if a method is constructor of a non-Value Type (Value Type constructors are static).
 *
 * @param methodIndex[in] the method index
 *
 * @returns true if the method is a constructor of a non-Value Type, false if not.
 */
bool
ClassFileOracle::methodIsConstructor(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsConstructor);

	if (J9_ARE_NO_BITS_SET(_classFile->methods[methodIndex].accessFlags, (CFR_ACC_ABSTRACT | CFR_ACC_STATIC | CFR_ACC_SYNCHRONIZED))
		&& ('<' == getUTF8Data(_classFile->methods[methodIndex].nameIndex)[0])
	) {
		return true;
	}

	return false;
}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

/**
 * Determine if a method is <clinit>.
 *
 * @param methodIndex[in] the method index
 *
 * @returns true if the method is <clinit>, false if not
 */
bool
ClassFileOracle::methodIsClinit(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsClinit);

	/* Use a quick check - assume any static method starting with < is <clinit>.
	 * This can not be harmful, as the worst thing that happens is we would lose
	 * the performance optimization on questionable class files.
	 */
	if ((_classFile->methods[methodIndex].accessFlags & CFR_ACC_STATIC) &&
		('<' == getUTF8Data(_classFile->methods[methodIndex].nameIndex)[0]))
	{
		return true;
	}

	return false;
}

/* answers non-zero if this method simply forwards to its superclass.
   A forwarder method is not synchronized.
   Forwarder methods must have return type void (because right now we only care if it forwards to an empty method)
*/

bool
ClassFileOracle::methodIsForwarder(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsForwarder);
	if (_classFile->methods[methodIndex].accessFlags & (CFR_ACC_ABSTRACT | CFR_ACC_NATIVE | CFR_ACC_SYNCHRONIZED | CFR_ACC_STATIC)) {
		return false;
	}

	/* Must return void */
	U_16 signatureIndex = _classFile->methods[methodIndex].descriptorIndex;
	U_8 *signatureData = getUTF8Data(signatureIndex);
	U_16 signatureLength = getUTF8Length(signatureIndex);
	if ((signatureData[signatureLength - 1]) != 'V') {
		return false;
	}

	J9CfrAttributeCode *codeAttribute = _classFile->methods[methodIndex].codeAttribute;

	/* ensure that there are no exception handlers */
	if (codeAttribute->exceptionTableLength > 0) {
		return false;
	}

	/* check that there are no temps */
	U_8 argCount = _methodsInfo[methodIndex].sendSlotCount;
	++argCount; /* not static */
	U_16 tempCount = codeAttribute->maxLocals;
	if (tempCount >= argCount) {
		tempCount -= argCount;
	} else {
		Trc_BCU_Assert_Equals(0, tempCount);
	}

	if (0 != tempCount) {
		return false;
	}

	IDATA thisArg = 0;
	U_8 *code = codeAttribute->code;
	for (U_32 codeIndex = 0; ;codeIndex++) {
		switch (code[codeIndex]) {
		case CFR_BC_aload_0:
			if (thisArg++ != 0) {
				return false;
			}
			break;
		case CFR_BC_iload_1:
		case CFR_BC_fload_1:
		case CFR_BC_aload_1:
			if (thisArg++ != 1) {
				return false;
			}
			break;
		case CFR_BC_dload_1:
		case CFR_BC_lload_1:
			if (thisArg != 1) {
				return false;
			}
			thisArg += 2;
			break;
		case CFR_BC_iload_2:
		case CFR_BC_fload_2:
		case CFR_BC_aload_2:
			if (thisArg++ != 2) {
				return false;
			}
			break;
		case CFR_BC_dload_2:
		case CFR_BC_lload_2:
			if (thisArg != 2) {
				return false;
			}
			thisArg += 2;
			break;
		case CFR_BC_iload_3:
		case CFR_BC_fload_3:
		case CFR_BC_aload_3:
			if (thisArg++ != 3) {
				return false;
			}
			break;
		case CFR_BC_dload_3:
		case CFR_BC_lload_3:
			if (thisArg != 3) {
				return false;
			}
			thisArg += 2;
			break;
		case CFR_BC_iload:
		case CFR_BC_fload:
		case CFR_BC_aload:
			if (thisArg++ != code[++codeIndex]) {
				return false;
			}
			break;
		case CFR_BC_dload:
		case CFR_BC_lload:
			if (thisArg != code[++codeIndex]) {
				return false;
			}
			thisArg += 2;
			break;
		case CFR_BC_invokespecial: {
			if (thisArg++ != argCount) {
				/* we haven't pushed all of our arguments */
				return false;
			}
#define PARAM_U16() (U_16(code[codeIndex + 1]) << 8) | U_16(code[codeIndex + 2])
			J9CfrConstantPoolInfo *methodRef = (J9CfrConstantPoolInfo *) &_classFile->constantPool[PARAM_U16()];
#undef PARAM_U16

			/* use identity comparisons for name and sig for simplicity.  Since they came from the same
				class file they're almost certainly identical,and if not we just run a bit slower */

			/* check that this is a super send */
			if ( methodRef->slot1 != _classFile->superClass ) {
				return false;
			}
			J9CfrConstantPoolInfo *nas = (J9CfrConstantPoolInfo *) &_classFile->constantPool[methodRef->slot2];
			if ( !isUTF8AtIndexEqualToString(nas->slot1, (const char *)getUTF8Data(_classFile->methods[methodIndex].nameIndex),  getUTF8Length(_classFile->methods[methodIndex].nameIndex)+1)) {
				return false;
			}
			if ( !isUTF8AtIndexEqualToString(nas->slot2, (const char *)getUTF8Data(_classFile->methods[methodIndex].descriptorIndex),  getUTF8Length(_classFile->methods[methodIndex].descriptorIndex)+1)) {
				return false;
			}

			/* check that the next instruction is a void return */
			if (CFR_BC_return == code[codeIndex+3]) {
				return true;
			}

			/* fall through */
		}
		default:
			return false;
		}
	}

	/* can't get here, but add a return false just in case */
	return false;
}



bool
ClassFileOracle::methodIsGetter (U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsGetter);


	if (0 != (_classFile->methods[methodIndex].accessFlags & (CFR_ACC_ABSTRACT | CFR_ACC_NATIVE | CFR_ACC_SYNCHRONIZED | CFR_ACC_STATIC))) {
		return false;
	}

	/* ensure that there are no exception handlers */
	if (0 != _classFile->methods[methodIndex].codeAttribute->exceptionTableLength) {
		return false;
	}

	/* ensure that there's exactly one argument (the receiver) */
	if (')' != getUTF8Data(_classFile->methods[methodIndex].descriptorIndex)[1]) {
		return false;
	}

	/* this exact bytecode sequence */
	/* JBaload0getfield means JBgetfield follows
	 * NOTEs:
	 *  - JBaload0getfield is actually CFR_BC_aload_0 followed by CFR_BC_getfield in Sun bytecodes
	 *  - JBgenericReturn is any one of the return bytecodes CFR_BC_*return */
	U_8* sunBytecodes = _classFile->methods[methodIndex].codeAttribute->code;
	if ((CFR_BC_aload_0 == sunBytecodes[0]) && (CFR_BC_getfield == sunBytecodes[1]) && (CFR_BC_ireturn <= sunBytecodes[4]) && (sunBytecodes[4] <= CFR_BC_return)) {
		return true;
	}

	return false;
}

bool
ClassFileOracle::methodIsVirtual(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsVirtual);

	/* static and private methods are never virtual */
	if (0 != (_classFile->methods[methodIndex].accessFlags & (CFR_ACC_STATIC | CFR_ACC_PRIVATE))) {
		return false;
	}

	/* <init> (and <clinit>) are not virtual */
	if ('<' == getUTF8Data(_classFile->methods[methodIndex].nameIndex)[0]) {
		return false;
	}

	if (0 == _classFile->superClass) { // TODO is this the same as J9ROMCLASS_SUPERCLASSNAME(romClass) == NULL???
		/* check for the final methods in object */
		if (methodIsFinalInObject (
				getUTF8Length(_classFile->methods[methodIndex].nameIndex),
				getUTF8Data(_classFile->methods[methodIndex].nameIndex),
				getUTF8Length(_classFile->methods[methodIndex].descriptorIndex),
				getUTF8Data(_classFile->methods[methodIndex].descriptorIndex)
				)) {
			return false;
		}
	}
	return true;
}

bool
ClassFileOracle::methodIsNonStaticNonAbstract(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, IsMethodNonStaticNonAbstract);

	return J9_ARE_NO_BITS_SET(_classFile->methods[methodIndex].accessFlags, (CFR_ACC_STATIC | CFR_ACC_ABSTRACT));
}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
bool
ClassFileOracle::methodIsNonStaticSynchronized(U_16 methodIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, MethodIsNonStaticSynchronized);
	if (J9_ARE_NO_BITS_SET(_classFile->methods[methodIndex].accessFlags, CFR_ACC_STATIC)
		&& J9_ARE_ALL_BITS_SET(_classFile->methods[methodIndex].accessFlags, CFR_ACC_SYNCHRONIZED)
	) {
		return true;
	}
	return false;
}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

/**
 * Method to determine if an invokevirtual instruction should be re-written to be an
 * invokehandle or invokehandlegeneric bytecode.  Modifications as follows:
 * 	invokevirtual MethodHandle.invokeExact(any signature) --> invokehandle
 * 	invokevirtual MethodHandle.invoke(any signature) --> invokehandlegeneric
 *
 * @param methodRefCPIndex - the constantpool index used in the invokevirtual bytecode
 * @return the invoke* bytecode to be used if it needs to be rewritten or 0
 * 	ie: One of: CFR_BC_invokehandlegeneric, CFR_BC_invokehandle, 0
 */
UDATA
ClassFileOracle::shouldConvertInvokeVirtualToMethodHandleBytecodeForMethodRef(U_16 methodRefCPIndex)
{
	J9CfrConstantPoolInfo *methodInfo = &_classFile->constantPool[methodRefCPIndex];
	J9CfrConstantPoolInfo *targetClassName = &_classFile->constantPool[ _classFile->constantPool[methodInfo->slot1].slot1 ];
	J9CfrConstantPoolInfo *nas = &_classFile->constantPool[methodInfo->slot2];
	J9CfrConstantPoolInfo *name = &_classFile->constantPool[nas->slot1];
	UDATA result = 0;

	/* Invoking against java.lang.invoke.MethodHandle. */
	if (J9UTF8_LITERAL_EQUALS(targetClassName->bytes, targetClassName->slot1, "java/lang/invoke/MethodHandle")) {
		if (J9UTF8_LITERAL_EQUALS(name->bytes, name->slot1, "invokeExact")) {
			/* MethodHandle.invokeExact */
			result = CFR_BC_invokehandle;
		} else if (J9UTF8_LITERAL_EQUALS(name->bytes, name->slot1, "invoke")) {
			/* MethodHandle.invoke */
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
 			result = CFR_BC_invokehandle;
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
			result = CFR_BC_invokehandlegeneric;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
		}
	}

#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	/* Invoking against java.lang.invoke.VarHandle. */
	if (J9UTF8_LITERAL_EQUALS(targetClassName->bytes, targetClassName->slot1, "java/lang/invoke/VarHandle")
	&& VM_VMHelpers::isPolymorphicVarHandleMethod((const U_8 *)name->bytes, name->slot1)
	) {
		result = CFR_BC_invokehandle;
	}
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

	return result;
}

bool
ClassFileOracle::shouldConvertInvokeVirtualToInvokeSpecialForMethodRef(U_16 methodRefCPIndex)
{
	ROMCLASS_VERBOSE_PHASE_HOT(_context, ShouldConvertInvokeVirtualToInvokeSpecial);

	J9CfrConstantPoolInfo *methodInfo = &_classFile->constantPool[methodRefCPIndex];
	J9CfrConstantPoolInfo *className = &_classFile->constantPool[ _classFile->constantPool[_classFile->thisClass].slot1 ];
	J9CfrConstantPoolInfo *targetClassName = &_classFile->constantPool[ _classFile->constantPool[methodInfo->slot1].slot1 ];
	J9CfrConstantPoolInfo *nas = &_classFile->constantPool[methodInfo->slot2];
	J9CfrConstantPoolInfo *name = &_classFile->constantPool[nas->slot1];
	J9CfrConstantPoolInfo *sig = &_classFile->constantPool[nas->slot2];

	/* check for wait, notify, notifyAll and getClass first */
	if (methodIsFinalInObject(name->slot1, name->bytes, sig->slot1, sig->bytes)) {
		return true;
	}

	/* Interfaces are allowed to call methods in java.lang.Object but other invokevirtual must fail */
	if (J9_ARE_ANY_BITS_SET(_classFile->accessFlags, CFR_ACC_INTERFACE)) {
		return false;
	}

	/* check for private methods (this is unlikely to be generated by a working compiler,
	 * but it is legal, so check for the case and force it to be an invokespecial)
	 */
	// TODO this hurts performance significantly - can we get away with not doing this? E.g. does it inflate vtables? Does the JIT do "the right thing"?
	if (J9UTF8_DATA_EQUALS(className->bytes, className->slot1, targetClassName->bytes, targetClassName->slot1)) {
		for (UDATA methodIndex = 0; methodIndex < _classFile->methodsCount; methodIndex++) {
			J9CfrMethod* method = &_classFile->methods[methodIndex];
			J9CfrConstantPoolInfo *aName = &_classFile->constantPool[method->nameIndex];
			J9CfrConstantPoolInfo *aSig = &_classFile->constantPool[method->descriptorIndex];
			if ((aName->slot1 == name->slot1)
			&& (aSig->slot1 == sig->slot1)
			&& (0 == memcmp(aName->bytes, name->bytes, name->slot1))
			&& (0 == memcmp(aSig->bytes, sig->bytes, sig->slot1))
			) {
				/* we found the method -- is it private or final? */
				return (method->accessFlags & (CFR_ACC_PRIVATE | CFR_ACC_FINAL)) != 0;
			}
		}
	}
	return false;
}

void
ClassFileOracle::markClassAsReferenced(U_16 classCPIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_Class, _classFile->constantPool[classCPIndex].tag);

	markConstantAsReferenced(classCPIndex); /* Mark class */
	markClassNameAsReferenced(classCPIndex);
}

void
ClassFileOracle::markClassNameAsReferenced(U_16 classCPIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_Class, _classFile->constantPool[classCPIndex].tag);

	markConstantUTF8AsReferenced(_classFile->constantPool[classCPIndex].slot1); /* Mark class name UTF8 */
}

void
ClassFileOracle::markStringAsReferenced(U_16 cpIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_String, _classFile->constantPool[cpIndex].tag);

	markConstantAsReferenced(cpIndex); /* Mark string */
	markConstantUTF8AsReferenced(_classFile->constantPool[cpIndex].slot1); /* Mark UTF8 */
}

void
ClassFileOracle::markMethodTypeAsReferenced(U_16 cpIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_MethodType, _classFile->constantPool[cpIndex].tag);

	markConstantAsReferenced(cpIndex); /* Mark MethodType */
	markConstantUTF8AsReferenced(_classFile->constantPool[cpIndex].slot1); /* Mark UTF8 */
}

void
ClassFileOracle::markMethodHandleAsReferenced(U_16 cpIndex)
{
	
	Trc_BCU_Assert_Equals(CFR_CONSTANT_MethodHandle, _classFile->constantPool[cpIndex].tag);

	markConstantAsReferenced(cpIndex); /* Mark MethodHandle */

	switch(_classFile->constantPool[cpIndex].slot1) {
	case MH_REF_GETFIELD:
		markFieldRefAsUsedByGetField(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_GETSTATIC:
		markFieldRefAsUsedByGetStatic(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_PUTFIELD:
		markFieldRefAsUsedByPutField(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_PUTSTATIC:
		markFieldRefAsUsedByPutStatic(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_INVOKEVIRTUAL:
		markMethodRefAsUsedByInvokeVirtual(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_INVOKESTATIC:
		markMethodRefAsUsedByInvokeStatic(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_INVOKESPECIAL:
	case MH_REF_NEWINVOKESPECIAL:
		markMethodRefAsUsedByInvokeSpecial(_classFile->constantPool[cpIndex].slot2);
		break;
	case MH_REF_INVOKEINTERFACE:
		markMethodRefAsUsedByInvokeInterface(_classFile->constantPool[cpIndex].slot2);
		break;
	}
}

void
ClassFileOracle::markNameAndDescriptorAsReferenced(U_16 nasCPIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_NameAndType, _classFile->constantPool[nasCPIndex].tag);

	markConstantNameAndTypeAsReferenced(nasCPIndex); /* Mark name-and-signature */
	markConstantUTF8AsReferenced(_classFile->constantPool[nasCPIndex].slot1); /* Mark name UTF8 */
	markConstantUTF8AsReferenced(_classFile->constantPool[nasCPIndex].slot2); /* Mark descriptor UTF8 */
}

void
ClassFileOracle::markFieldRefAsReferenced(U_16 cpIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_Fieldref, _classFile->constantPool[cpIndex].tag);

	// TODO markConstantAsReferenced(cpIndex); -- this breaks the iteration strategy in ROMClassBuilder
	markClassAsReferenced(_classFile->constantPool[cpIndex].slot1);
	markNameAndDescriptorAsReferenced(_classFile->constantPool[cpIndex].slot2);
}

void
ClassFileOracle::markMethodRefAsReferenced(U_16 cpIndex)
{
	Trc_BCU_Assert_True((CFR_CONSTANT_Methodref == _classFile->constantPool[cpIndex].tag) || (CFR_CONSTANT_InterfaceMethodref == _classFile->constantPool[cpIndex].tag));

	// TODO markConstantAsReferenced(cpIndex); -- this breaks the iteration strategy in ROMClassBuilder
	markClassAsReferenced(_classFile->constantPool[cpIndex].slot1);
	markNameAndDescriptorAsReferenced(_classFile->constantPool[cpIndex].slot2);
}

/* Mark the parts of a MethodRef used for a MethodTypeRef - just the signature of the methodref's nas.
 * The invokehandle/invokehandlegeneric instruction will only ever preserve the signature.
 */
void
ClassFileOracle::markMethodRefForMHInvocationAsReferenced(U_16 cpIndex)
{
	markMethodRefAsReferenced(cpIndex);
}

void
ClassFileOracle::markConstantAsReferenced(U_16 cpIndex)
{
	if (0 != cpIndex) { /* Never, never, never mark constantPool[0] referenced */
		_constantPoolMap->markConstantAsReferenced(cpIndex);
	}
}

void
ClassFileOracle::markConstantUTF8AsReferenced(U_16 cpIndex)
{
	if (0 != cpIndex) { /* Never, never, never mark constantPool[0] referenced */
		_constantPoolMap->markConstantUTF8AsReferenced(cpIndex);
	}
}

void
ClassFileOracle::markConstantNameAndTypeAsReferenced(U_16 cpIndex)
{
	if (0 != cpIndex) { /* Never, never, never mark constantPool[0] referenced */
		_constantPoolMap->markConstantNameAndTypeAsReferenced(cpIndex);
	}
}

void
ClassFileOracle::markConstantDynamicAsReferenced(U_16 cpIndex)
{
	markNameAndDescriptorAsReferenced(_classFile->constantPool[cpIndex].slot2);
	if (0 != cpIndex) { /* Never, never, never mark constantPool[0] referenced */
		_constantPoolMap->markConstantAsReferenced(cpIndex);
	}
}

void
ClassFileOracle::markConstantAsUsedByAnnotation(U_16 cpIndex)
{
	UDATA cpTag = getCPTag(cpIndex);

	if ((CFR_CONSTANT_Double == cpTag) || (CFR_CONSTANT_Long == cpTag)) {
		_constantPoolMap->markConstantAsReferencedDoubleSlot(cpIndex);
	} else if (CFR_CONSTANT_Utf8 == cpTag) {
		markConstantUTF8AsReferenced(cpIndex); /* Mark descriptor UTF8 */
		/* Is not equivalent to markConstantAsReferenced(), as that does something else for UTF8s. */
		_constantPoolMap->markConstantAsUsedByAnnotationUTF8(cpIndex);
	} else {
		markConstantAsReferenced(cpIndex);
	}
}

void
ClassFileOracle::markConstantAsUsedByLDC(U_8 cpIndex)
{
	_constantPoolMap->markConstantAsUsedByLDC(cpIndex);
}

void
ClassFileOracle::markConstantAsUsedByLDC2W(U_16 cpIndex)
{
	_constantPoolMap->markConstantAsReferencedDoubleSlot(cpIndex);
}

void
ClassFileOracle::markClassAsUsedByInstanceOf(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByInstanceOf(classCPIndex);
}

void
ClassFileOracle::markClassAsUsedByCheckCast(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByCheckCast(classCPIndex);
}

void
ClassFileOracle::markClassAsUsedByMultiANewArray(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByMultiANewArray(classCPIndex);
}

void
ClassFileOracle::markClassAsUsedByANewArray(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByANewArray(classCPIndex);
}

void
ClassFileOracle::markClassAsUsedByNew(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByNew(classCPIndex);
}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
void
ClassFileOracle::markClassAsUsedByDefaultValue(U_16 classCPIndex)
{
	markClassAsReferenced(classCPIndex);
	_constantPoolMap->markClassAsUsedByDefaultValue(classCPIndex);
}

void
ClassFileOracle::markFieldRefAsUsedByWithField(U_16 fieldRefCPIndex)
{
	markFieldRefAsReferenced(fieldRefCPIndex);
	_constantPoolMap->markFieldRefAsUsedByWithField(fieldRefCPIndex);
}
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

void
ClassFileOracle::markFieldRefAsUsedByGetStatic(U_16 fieldRefCPIndex)
{
	markFieldRefAsReferenced(fieldRefCPIndex);
	_constantPoolMap->markFieldRefAsUsedByGetStatic(fieldRefCPIndex);
}

void
ClassFileOracle::markFieldRefAsUsedByPutStatic(U_16 fieldRefCPIndex)
{
	markFieldRefAsReferenced(fieldRefCPIndex);
	_constantPoolMap->markFieldRefAsUsedByPutStatic(fieldRefCPIndex);
}

void
ClassFileOracle::markFieldRefAsUsedByGetField(U_16 fieldRefCPIndex)
{
	markFieldRefAsReferenced(fieldRefCPIndex);
	_constantPoolMap->markFieldRefAsUsedByGetField(fieldRefCPIndex);
}

void
ClassFileOracle::markFieldRefAsUsedByPutField(U_16 fieldRefCPIndex)
{
	markFieldRefAsReferenced(fieldRefCPIndex);
	_constantPoolMap->markFieldRefAsUsedByPutField(fieldRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeVirtual(U_16 methodRefCPIndex)
{
	markMethodRefAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeVirtual(methodRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeSpecial(U_16 methodRefCPIndex)
{
	markMethodRefAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeSpecial(methodRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeStatic(U_16 methodRefCPIndex)
{
	markMethodRefAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeStatic(methodRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeInterface(U_16 methodRefCPIndex)
{
	markMethodRefAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeInterface(methodRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeHandle(U_16 methodRefCPIndex)
{
	markMethodRefForMHInvocationAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeHandle(methodRefCPIndex);
}

void
ClassFileOracle::markMethodRefAsUsedByInvokeHandleGeneric(U_16 methodRefCPIndex)
{
	markMethodRefForMHInvocationAsReferenced(methodRefCPIndex);
	_constantPoolMap->markMethodRefAsUsedByInvokeHandleGeneric(methodRefCPIndex);
}

void
ClassFileOracle::markInvokeDynamicInfoAsUsedByInvokeDynamic(U_16 cpIndex)
{
	Trc_BCU_Assert_Equals(CFR_CONSTANT_InvokeDynamic, _classFile->constantPool[cpIndex].tag);

	U_16 bsmIndex = _classFile->constantPool[cpIndex].slot1;
	if ((NULL == _bootstrapMethodsAttribute) || (bsmIndex >= _bootstrapMethodsAttribute->numberOfBootstrapMethods)) {
		_buildResult = GenericError;
		return;
	}

	markNameAndDescriptorAsReferenced(_classFile->constantPool[cpIndex].slot2);
	_constantPoolMap->markInvokeDynamicInfoAsUsedByInvokeDynamic(cpIndex);
}

void
ClassFileOracle::markConstantBasedOnCpType(U_16 cpIndex, bool assertNotDoubleOrLong)
{
	switch(getCPTag(cpIndex)) {
	case CFR_CONSTANT_String:
		markStringAsReferenced(cpIndex);
		break;
	case CFR_CONSTANT_Class:
		markClassAsReferenced(cpIndex);
		break;
	case CFR_CONSTANT_Integer:
	case CFR_CONSTANT_Float:
		markConstantAsReferenced(cpIndex);
		break;
	case CFR_CONSTANT_MethodHandle:
		markMethodHandleAsReferenced(cpIndex);
		break;
	case CFR_CONSTANT_MethodType:
		markMethodTypeAsReferenced(cpIndex);
		break;
	case CFR_CONSTANT_Double:
	case CFR_CONSTANT_Long:
		if (assertNotDoubleOrLong) {
			Trc_BCU_Assert_ShouldNeverHappen();
			break;
		}
		_constantPoolMap->markConstantAsReferencedDoubleSlot(cpIndex);
		break;
	case CFR_CONSTANT_Dynamic:
		markConstantDynamicAsReferenced(cpIndex);
		break;
	default:
		Trc_BCU_Assert_ShouldNeverHappen();
	}
}

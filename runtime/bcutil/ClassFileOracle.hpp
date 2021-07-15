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
 * ClassFileOracle.hpp
 */

#ifndef CLASSFILEORACLE_HPP_
#define CLASSFILEORACLE_HPP_

/* @ddr_namespace: default */
#include "j9comp.h"
#include "cfr.h"
#include "cfreader.h"
#include "ut_j9bcu.h"
#include "bcnames.h"

#include "BuildResult.hpp"
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
#include "VMHelpers.hpp"
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */

/*
 * It is not guaranteed that slot1 value for constantpool index=0 entry will be zero. 
 * Therefore check the index first, if it is zero, return zero instead of returning the value in slot1. 
 * 
 * */
#define UTF8_INDEX_FROM_CLASS_INDEX(cp, cpIndex)  ((U_16)((cpIndex == 0)? 0 : cp[cpIndex].slot1))

class BufferManager;
class ConstantPoolMap;
class ROMClassCreationContext;

class ClassFileOracle
{
public:
	class VerificationTypeInfo;
	class ArrayAnnotationElements;
	class NestedAnnotation;

	struct FieldInfo
	{
		bool isSynthetic;
		bool hasGenericSignature;
		U_16 genericSignatureIndex;
		J9CfrAttributeRuntimeVisibleAnnotations *annotationsAttribute;
		J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotationsAttribute;
		bool isFieldContended;
	};

	struct StackMapFrameInfo
	{
		U_8 frameType;
		U_16 offsetDelta;
		U_16 localsCount;
		U_16 stackItemsCount;
		U_8 *localsTypeInfo;
		U_8 *stackItemsTypeInfo;
	};

	struct LocalVariableInfo
	{
		J9CfrAttributeLocalVariableTable *localVariableTableAttribute;
		J9CfrAttributeLocalVariableTypeTable *localVariableTypeTableAttribute;
	};

	struct BytecodeFixupEntry
	{
		U_8 type;  /* One of ConstantPoolMap::EntryUseFlags constants. */
		U_16 cpIndex;
		U_32 codeIndex;
	};

	struct MethodInfo
	{
		bool hasFrameIteratorSkipAnnotation;
		U_32 modifiers;
		U_32 extendedModifiers;
		U_8 sendSlotCount;
		U_16 exceptionsThrownCount;
		U_16 genericSignatureIndex;
		U_16 stackMapFramesCount;
		U_32 lineNumbersCount;
		U_32 lineNumbersInfoCompressedSize;
		U_32 localVariablesCount;
		StackMapFrameInfo *stackMapFramesInfo;
		LocalVariableInfo *localVariablesInfo;
		U_8 *lineNumbersInfoCompressed;
		J9CfrAttributeRuntimeVisibleAnnotations *annotationsAttribute;
		J9CfrAttributeRuntimeVisibleParameterAnnotations *parameterAnnotationsAttribute;
		J9CfrAttributeRuntimeVisibleTypeAnnotations *methodTypeAnnotationsAttribute;
		J9CfrAttributeRuntimeVisibleTypeAnnotations *codeTypeAnnotationsAttribute;
		J9CfrAttributeAnnotationDefault *defaultAnnotationAttribute;
		U_32 byteCodeFixupCount;
		J9CfrAttributeMethodParameters *methodParametersAttribute;
		BytecodeFixupEntry *byteCodeFixupTable;
		bool isByteCodeFixupDone;
	};

	struct RecordComponentInfo
	{
		bool hasGenericSignature;
		U_16 nameIndex;
		U_16 descriptorIndex;
		U_16 genericSignatureIndex;
		J9CfrAttributeRuntimeVisibleAnnotations *annotationsAttribute;
		J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotationsAttribute;
	};

	/*
	 * Interfaces.
	 */

	struct ConstantPoolIndexVisitor
	{
		virtual void visitConstantPoolIndex(U_16 cpIndex) = 0;
	};

	struct ExceptionHandlerVisitor
	{
		virtual void visitExceptionHandler(U_32 startPC, U_32 endPC, U_32 handlerPC, U_16 exceptionClassCPIndex) = 0;
	};

	struct MethodParametersVisitor
	{
		virtual void visitMethodParameters(U_16 cpIndex, U_16 flag) = 0;
	};


	struct VerificationTypeInfoVisitor
	{
		virtual void visitStackMapObject(U_8 slotType, U_16 classCPIndex, U_16 classNameCPIndex) = 0;
		virtual void visitStackMapNewObject(U_8 slotType, U_16 offset) = 0;
		virtual void visitStackMapItem(U_8 slotType) = 0;
	};

	struct StackMapFrameVisitor
	{
		virtual void visitStackMapFrame(U_16 localsCount, U_16 stackItemsCount, U_16 offsetDelta, U_8 frameType, VerificationTypeInfo *typeInfo) = 0;
	};

	struct AnnotationElementVisitor
	{
		virtual void visitConstant(U_16 elementNameIndex, U_16 cpIndex, U_8 elementType) {}
		virtual void visitEnum(U_16 elementNameIndex, U_16 typeNameIndex, U_16 constNameIndex) {}
		virtual void visitClass(U_16 elementNameIndex, U_16 cpIndex) {}
		virtual void visitArray(U_16 elementNameIndex, U_16 elementCount, ArrayAnnotationElements *arrayAnnotationElements) {}
		virtual void visitNestedAnnotation(U_16 elementNameIndex, NestedAnnotation *nestedAnnotation) {}
	};

	struct AnnotationVisitor
	{
		virtual void visitParameter(U_16 numberOfAnnotations) = 0;
		virtual void visitAnnotation(U_16 typeIndex, U_16 elementValuePairCount) = 0;
		virtual void visitTypeAnnotation(U_8 targetType, J9CfrTypeAnnotationTargetInfo *targetInfo, J9CfrTypePath *typePath) = 0;
	};

	struct AnnotationsAttributeVisitor
	{
		virtual void visitAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_16 numberOfAnnotations) = 0;
		virtual void visitDefaultAnnotationAttribute(U_16 fieldOrMethodIndex, U_32 length) = 0;
		virtual void visitParameterAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_8 numberOfParameters) = 0;
		virtual void visitTypeAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_16 numberOfAnnotations) = 0;
		virtual void visitMalformedAnnotationsAttribute(U_32 rawDataLength, U_8 *rawAttributeData) = 0;
	};

	struct BootstrapMethodVisitor
	{
		virtual void visitBootstrapMethod(U_16 cpIndex, U_16 argumentCount) = 0;
		virtual void visitBootstrapArgument(U_16 cpIndex) = 0;
	};

	/*
	 * Iteration artifacts.
	 */

class VerificationTypeInfo
{
	public:
		VerificationTypeInfo(StackMapFrameInfo *stackMapFrameInfo, J9CfrClassFile *classFile) :
			_stackMapFrameInfo(stackMapFrameInfo),
			_classFile(classFile)
		{
		}

		void localsDo(VerificationTypeInfoVisitor *visitor) { slotsDo(_stackMapFrameInfo->localsCount, _stackMapFrameInfo->localsTypeInfo, visitor); }
		void stackItemsDo(VerificationTypeInfoVisitor *visitor) { slotsDo(_stackMapFrameInfo->stackItemsCount, _stackMapFrameInfo->stackItemsTypeInfo, visitor); }

	private:
		U_16 getParameter(U_8 *bytes) const { return (U_16(bytes[1]) << 8) | U_16(bytes[2]); }
		void slotsDo(U_16 count, U_8 *bytes, VerificationTypeInfoVisitor *visitor)
		{
			for (U_16 i = 0; i < count; ++i) {
				U_8 slotType = bytes[0];
				switch (slotType) {
				case CFR_STACKMAP_TYPE_OBJECT: {
					U_16 cpIndex = getParameter(bytes);
					U_16 nameIndex = (U_16) _classFile->constantPool[cpIndex].slot1;
					visitor->visitStackMapObject(slotType, cpIndex, nameIndex);
					bytes += 3;
					break;
				}
				case CFR_STACKMAP_TYPE_NEW_OBJECT:
					visitor->visitStackMapNewObject(slotType, getParameter(bytes));
					bytes += 3;
					break;
				default:
					visitor->visitStackMapItem(slotType);
					bytes += 1;
					break;
				}
			}
		}

		StackMapFrameInfo *_stackMapFrameInfo;
		J9CfrClassFile *_classFile;
	};

class ArrayAnnotationElements
{
	public:
		ArrayAnnotationElements(ClassFileOracle *classFileOracle, J9CfrAnnotationElementArray *annotationElementArray) :
			_classFileOracle(classFileOracle),
			_annotationElementArray(annotationElementArray)
		{
		}

		void elementsDo(AnnotationElementVisitor *annotationElementVisitor)
		{
			J9CfrAnnotationElement **endAnnotationElements = _annotationElementArray->values + _annotationElementArray->numberOfValues;
			for (J9CfrAnnotationElement **annotationElement = _annotationElementArray->values; annotationElement != endAnnotationElements; ++annotationElement) {
				_classFileOracle->annotationElementDo(annotationElementVisitor, 0, *annotationElement);
			}
		}

	private:
		ClassFileOracle *_classFileOracle;
		J9CfrAnnotationElementArray *_annotationElementArray;
	};

class NestedAnnotation
{
	public:
		NestedAnnotation(ClassFileOracle *classFileOracle, J9CfrAnnotation *annotation) :
			_classFileOracle(classFileOracle),
			_annotation(annotation)
		{
		}

		void annotationDo(AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor, AnnotationElementVisitor *annotationAnnotationVisitor)
		{
			if (NULL != annotationVisitor) {
				annotationVisitor->visitAnnotation(_annotation->typeIndex, _annotation->numberOfElementValuePairs);
			}
			if (NULL != annotationElementVisitor) {
				_classFileOracle->annotationElementsDo(annotationElementVisitor, _annotation->elementValuePairs, _annotation->numberOfElementValuePairs);
			}
			if (NULL != annotationAnnotationVisitor) {
				_classFileOracle->annotationElementsDo(annotationAnnotationVisitor, _annotation->elementValuePairs, _annotation->numberOfElementValuePairs);
			}
		}

	private:
		ClassFileOracle *_classFileOracle;
		J9CfrAnnotation *_annotation;
	};

class AnnotationAnnotationVisitor : public AnnotationElementVisitor
{
	public:
		AnnotationAnnotationVisitor(AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor) :
			_annotationVisitor(annotationVisitor),
			_annotationElementVisitor(annotationElementVisitor)
		{
		}

		void visitArray(U_16 elementNameIndex, U_16 elementCount, ArrayAnnotationElements *arrayAnnotationElements)
		{
			arrayAnnotationElements->elementsDo(this);
		}

		void visitNestedAnnotation(U_16 elementNameIndex, NestedAnnotation *nestedAnnotation)
		{
			nestedAnnotation->annotationDo(_annotationVisitor, _annotationElementVisitor, this);
		}

	private:
		AnnotationVisitor *_annotationVisitor;
		AnnotationElementVisitor *_annotationElementVisitor;
	};

	/*
	 * Iterators.
	 */

class FieldIterator
{
	public:
		FieldIterator(FieldInfo *fieldsInfo, J9CfrClassFile *classFile) :
			_fieldsInfo(fieldsInfo),
			_classFile(classFile),
			_fields(classFile->fields),
			_index(0)
		{
		}

		U_16 getNameIndex() const { return _fields[_index].nameIndex; }
		U_16 getDescriptorIndex() const { return _fields[_index].descriptorIndex; }
		U_8 getFirstByteOfDescriptor() const { return _classFile->constantPool[getDescriptorIndex()].bytes[0]; }
		U_16 getAccessFlags() const { return _fields[_index].accessFlags; }
		U_16 getFieldIndex() const {return _index; }

		bool isConstant() const { return (getAccessFlags() & CFR_ACC_STATIC) && (NULL != _fields[_index].constantValueAttribute); }
		bool isConstantInteger() const { return isConstant() && getConstantValueTag() == CFR_CONSTANT_Integer; }
		bool isConstantDouble() const { return isConstant() && getConstantValueTag() == CFR_CONSTANT_Double; }
		bool isConstantFloat() const { return isConstant() && getConstantValueTag() == CFR_CONSTANT_Float; }
		bool isConstantLong() const { return isConstant() && getConstantValueTag() == CFR_CONSTANT_Long; }
		bool isConstantString() const { return isConstant() && getConstantValueTag() == CFR_CONSTANT_String; }

		bool isSynthetic() const { return _fieldsInfo[_index].isSynthetic; }
		bool hasGenericSignature() const { return _fieldsInfo[_index].hasGenericSignature; }
		U_16 getGenericSignatureIndex() const { return _fieldsInfo[_index].genericSignatureIndex; }
		bool hasAnnotation() const { return _fieldsInfo[_index].annotationsAttribute != NULL;}
		bool hasTypeAnnotation() const { return _fieldsInfo[_index].typeAnnotationsAttribute != NULL;}
		bool isFieldContended() const { return _fieldsInfo[_index].isFieldContended; }


		U_32 getConstantValueSlot1() const { return _classFile->constantPool[getConstantValueConstantPoolIndex()].slot1; }
		U_32 getConstantValueSlot2() const { return _classFile->constantPool[getConstantValueConstantPoolIndex()].slot2; }
		U_16 getConstantValueConstantPoolIndex() const { return _fields[_index].constantValueAttribute->constantValueIndex; }

		bool isNotDone() const { return _index < _classFile->fieldsCount; }
		void next() { _index++; }

	private:
		U_8 getConstantValueTag() const { return _classFile->constantPool[getConstantValueConstantPoolIndex()].tag; }

		FieldInfo *_fieldsInfo;
		J9CfrClassFile *_classFile;
		J9CfrField *_fields;
		U_16 _index;
	};

class LocalVariablesIterator
{
	public:
		LocalVariablesIterator(U_16 count, LocalVariableInfo *localVariablesInfo) :
			_localVariableTableIndex(0),
			_index(0),
			_count(count),
			_localVariablesInfo(localVariablesInfo),
			_localVariableTable(NULL) // TODO why do we need this?
		{
			findNextValidEntry();
		}

		U_16 getNameIndex() const { return _localVariableTable[_localVariableTableIndex].nameIndex; }
		U_16 getDescriptorIndex() const { return _localVariableTable[_localVariableTableIndex].descriptorIndex; }
		U_16 getGenericSignatureIndex();
		U_16 getIndex() const { return _index; } /* Equivalent to _localVariableTable[_localVariableTableIndex].index */
		U_32 getStartPC() const { return _localVariableTable[_localVariableTableIndex].startPC; }
		U_32 getLength() const { return _localVariableTable[_localVariableTableIndex].length; }

		bool hasGenericSignature();

		bool isNotDone() const { return _index < _count; }
		void next()
		{
			++_localVariableTableIndex;
			findNextValidEntry();
		}

	private:
		void findNextValidEntry()
		{
			/*
			 * _localVariableTable is a cached pointer to _localVariablesInfo[_index].localVariableTableAttribute->localVariableTable.
			 * The LocalVariableInfo array is indexed by the "local variable index" (a.k.a. slot number).
			 * An entry is valid if:
			 * 	- there is a localVariableTable for the current slot (_index) and
			 * 	- the entry's index matches the current slot number (_index)
			 * The algorithm below checks if the current entry is valid; if not, the cached pointer is cleared and the search begins.
			 * When a valid entry is found, the pointer to the corresponding localVariableTable is cached to allow easy access from the
			 * getter methods above.
			 */
			if ((NULL == _localVariableTable) /* Is the cached pointer invalid? */
					|| (_localVariableTableIndex >= _localVariablesInfo[_index].localVariableTableAttribute->localVariableTableLength) /* Have we walked off the end of the current localVariableTable? */
					|| (_index != _localVariableTable[_localVariableTableIndex].index)) { /* Is the _index different from the current entry's index? */
				_localVariableTable = NULL;
				while ((NULL == _localVariableTable) && isNotDone()) { /* Keep looking until a valid entry is found or we run out of entries */
					if ((NULL == _localVariablesInfo[_index].localVariableTableAttribute)
							|| (_localVariableTableIndex >= _localVariablesInfo[_index].localVariableTableAttribute->localVariableTableLength)) {
						/* If there is no localVariableTable or we've exhausted the entries for the current slot, advance to the next slot */
						++_index;
						_localVariableTableIndex = 0;
					} else if (_index != _localVariablesInfo[_index].localVariableTableAttribute->localVariableTable[_localVariableTableIndex].index) {
						/* If the current entry doesn't match the slot number, advance to the next entry */
						++_localVariableTableIndex;
					} else {
						/* A valid entry has been found. Cache the localVariableTable pointer and exit the loop. */
						_localVariableTable = _localVariablesInfo[_index].localVariableTableAttribute->localVariableTable;
					}
				}
			}
		}

		U_16 _localVariableTableIndex;
		U_16 _index;
		U_16 _count;
		LocalVariableInfo *_localVariablesInfo;
		J9CfrLocalVariableTableEntry *_localVariableTable;
	};

class MethodIterator
{
	public:
		MethodIterator(MethodInfo *methodsInfo, J9CfrClassFile *classFile) :
			_methodIndex(0),
			_methodsInfo(methodsInfo),
			_classFile(classFile)
		{
		}

		bool isEmpty() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccEmptyMethod); }
		bool isForwarder() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccForwarderMethod); }
		bool isGetter() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccGetterMethod); }
		bool isVirtual() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccMethodVTable); }
		bool isSynthetic() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccSynthetic); }

		bool isStatic() const { return 0 != (_classFile->methods[_methodIndex].accessFlags & CFR_ACC_STATIC); }
		bool isAbstract() const { return 0 != (_classFile->methods[_methodIndex].accessFlags & CFR_ACC_ABSTRACT); }
		bool isNative() const { return 0 != (_classFile->methods[_methodIndex].accessFlags & CFR_ACC_NATIVE); }
		bool isPrivate() const { return 0 != (_classFile->methods[_methodIndex].accessFlags & CFR_ACC_PRIVATE); }

		bool isByteCodeFixupDone() const { return _methodsInfo[_methodIndex].isByteCodeFixupDone; }
		void setByteCodeFixupDone() { _methodsInfo[_methodIndex].isByteCodeFixupDone = true; }

		bool hasStackMap() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccMethodHasStackMap); }
		bool hasBackwardBranches() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccMethodHasBackwardBranches); }
		bool hasGenericSignature() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccMethodHasGenericSignature); }
		bool hasFrameIteratorSkipAnnotation() const { return _methodsInfo[_methodIndex].hasFrameIteratorSkipAnnotation; }
		bool hasAnnotationsData() const { return NULL != _methodsInfo[_methodIndex].annotationsAttribute; }
		bool hasParameterAnnotations() const { return NULL != _methodsInfo[_methodIndex].parameterAnnotationsAttribute; }
		bool hasMethodTypeAnnotations() const { return NULL != _methodsInfo[_methodIndex].methodTypeAnnotationsAttribute; }
		bool hasCodeTypeAnnotations() const { return NULL != _methodsInfo[_methodIndex].codeTypeAnnotationsAttribute; }
		bool hasDefaultAnnotation() const { return NULL != _methodsInfo[_methodIndex].defaultAnnotationAttribute; }
		bool hasMethodParameters() const { return 0 != (_methodsInfo[_methodIndex].modifiers & J9AccMethodHasMethodParameters); }

		U_8 getMethodParametersCount() const { return (NULL == _classFile->methods[_methodIndex].methodParametersAttribute) ? 0 : _classFile->methods[_methodIndex].methodParametersAttribute->numberOfMethodParameters; }
		U_32 getModifiers() const { return  _methodsInfo[_methodIndex].modifiers; }
		U_32 getExtendedModifiers() const { return  _methodsInfo[_methodIndex].extendedModifiers; }
		U_16 getAccessFlags() const { return _classFile->methods[_methodIndex].accessFlags; }
		U_8 getSendSlotCount() const { return _methodsInfo[_methodIndex].sendSlotCount; }
		U_16 getMaxLocals() const { return (NULL == _classFile->methods[_methodIndex].codeAttribute) ? 0 : _classFile->methods[_methodIndex].codeAttribute->maxLocals; }
		U_16 getNameIndex() const { return _classFile->methods[_methodIndex].nameIndex; }
		U_16 getDescriptorIndex() const { return _classFile->methods[_methodIndex].descriptorIndex; }
		U_16 getIndex() const { return _methodIndex; }
		U_16 getMaxStack() const { return (NULL == _classFile->methods[_methodIndex].codeAttribute) ? 0 : _classFile->methods[_methodIndex].codeAttribute->maxStack; }
		U_16 getStackMapFramesCount() const { return _methodsInfo[_methodIndex].stackMapFramesCount; }
		U_16 getExceptionHandlersCount() const { return (NULL == _classFile->methods[_methodIndex].codeAttribute) ? 0 : _classFile->methods[_methodIndex].codeAttribute->exceptionTableLength; }
		U_16 getExceptionsThrownCount() const { return _methodsInfo[_methodIndex].exceptionsThrownCount; }
		U_16 getGenericSignatureIndex() const { return _methodsInfo[_methodIndex].genericSignatureIndex; }
		U_8 *getCode() const { return (NULL == _classFile->methods[_methodIndex].codeAttribute) ? 0 : _classFile->methods[_methodIndex].codeAttribute->code; }
		U_32 getCodeLength() const { return (NULL == _classFile->methods[_methodIndex].codeAttribute) ? 0 : _classFile->methods[_methodIndex].codeAttribute->codeLength; }
		U_32 getLineNumbersCount() const { return _methodsInfo[_methodIndex].lineNumbersCount; }
		U_32 getLineNumbersInfoCompressedSize() const { return _methodsInfo[_methodIndex].lineNumbersInfoCompressedSize; }
		U_8 *getLineNumbersInfoCompressed() const { return _methodsInfo[_methodIndex].lineNumbersInfoCompressed; }
		U_32 getLocalVariablesCount() const { return _methodsInfo[_methodIndex].localVariablesCount; }

		U_32 getByteCodeFixupCount() const { return _methodsInfo[_methodIndex].byteCodeFixupCount; }
		ClassFileOracle::BytecodeFixupEntry * getByteCodeFixupTable() const { return _methodsInfo[_methodIndex].byteCodeFixupTable; }

		void exceptionsThrownDo(ConstantPoolIndexVisitor *visitor)
		{ // TODO refactor MethodInfo and MethodIterator to make this usable by ClassFileOracle::walkMethodThrownExceptions
			J9CfrAttributeExceptions *exceptions = _classFile->methods[_methodIndex].exceptionsAttribute;
			if (NULL != exceptions) {
				U_16 *end = exceptions->exceptionIndexTable + exceptions->numberOfExceptions;
				for (U_16 *exceptionIndex = exceptions->exceptionIndexTable; exceptionIndex != end; ++exceptionIndex) {
					if (0 != *exceptionIndex) {
						/* Each exception is a constantClass, use slot1 to get at the underlying UTF8 */
						visitor->visitConstantPoolIndex(U_16(_classFile->constantPool[ *exceptionIndex ].slot1));
					}
				}
			}
		}

		void exceptionHandlersDo(ExceptionHandlerVisitor *visitor)
		{
			if (NULL != _classFile->methods[_methodIndex].codeAttribute) {
				J9CfrExceptionTableEntry* end = _classFile->methods[_methodIndex].codeAttribute->exceptionTable + getExceptionHandlersCount();
				for (J9CfrExceptionTableEntry* entry = _classFile->methods[_methodIndex].codeAttribute->exceptionTable; entry != end; ++entry) {
					visitor->visitExceptionHandler(entry->startPC, entry->endPC, entry->handlerPC, entry->catchType);
				}
			}
		}

		void methodParametersDo(MethodParametersVisitor *visitor)
		{
			J9CfrAttributeMethodParameters *methodParams = _classFile->methods[_methodIndex].methodParametersAttribute;
			if (NULL != methodParams) {
				U_8 methodParamsCount = methodParams->numberOfMethodParameters;
				U_16 * methodParametersIndexTable = methodParams->methodParametersIndexTable;
				for (int i = 0; i < methodParamsCount; i++) {
					visitor->visitMethodParameters(methodParametersIndexTable[i], methodParams->flags[i]);
				}
			}
		}

		void stackMapFramesDo(StackMapFrameVisitor *visitor)
		{
			StackMapFrameInfo *end = _methodsInfo[_methodIndex].stackMapFramesInfo + _methodsInfo[_methodIndex].stackMapFramesCount;
			for (StackMapFrameInfo *info = _methodsInfo[_methodIndex].stackMapFramesInfo; info != end; ++info) {
				VerificationTypeInfo typeInfo(info, _classFile);
				visitor->visitStackMapFrame(info->localsCount, info->stackItemsCount, info->offsetDelta, info->frameType, &typeInfo);
			}
		}

		LocalVariablesIterator getLocalVariablesIterator() { return LocalVariablesIterator((NULL == _methodsInfo[_methodIndex].localVariablesInfo) ? 0 : getMaxLocals(), _methodsInfo[_methodIndex].localVariablesInfo); }

		bool isNotDone() const { return _methodIndex < _classFile->methodsCount; }
		void next() { _methodIndex++; }

	private:

		U_16 _methodIndex;
		MethodInfo *_methodsInfo;
		J9CfrClassFile *_classFile;
	};

class UTF8Iterator
{
	public:
		UTF8Iterator(J9CfrClassFile *classFile) :
			_classFile(classFile),
			_cpIndex(classFile->firstUTF8CPIndex),
			_entry(&classFile->constantPool[_cpIndex])
		{
		}

		U_16 getCPIndex() const { return _cpIndex; }
		U_16 getUTF8Length() const { return U_16(_entry->slot1); }
		U_8 *getUTF8Data() const { return _entry->bytes; }

		bool isNotDone() const { return 0 != _cpIndex; }
		void next()
		{
			_cpIndex = _entry->nextCPIndex;
			_entry = &_classFile->constantPool[_cpIndex];
		}

	private:
		J9CfrClassFile *_classFile;
		U_16 _cpIndex;
		J9CfrConstantPoolInfo *_entry;
	};

class NameAndTypeIterator
{
	public:
		NameAndTypeIterator(J9CfrClassFile *classFile) :
			_classFile(classFile),
			_cpIndex(classFile->firstNATCPIndex),
			_entry(&classFile->constantPool[_cpIndex])
		{
		}

		U_16 getCPIndex() const { return _cpIndex; }
		U_16 getNameIndex() const { return U_16(_entry->slot1); }
		U_16 getDescriptorIndex() const { return U_16(_entry->slot2); }

		bool isNotDone() const { return 0 != _cpIndex; }
		void next()
		{
			_cpIndex = _entry->nextCPIndex;
			_entry = &_classFile->constantPool[_cpIndex];
		}

	private:
		J9CfrClassFile *_classFile;
		U_16 _cpIndex;
		J9CfrConstantPoolInfo *_entry;
	};

class RecordComponentIterator
{
	public:
		RecordComponentIterator(RecordComponentInfo *recordComponentsInfo, U_16 recordComponentCount) :
			_recordComponentsInfo(recordComponentsInfo),
			_recordComponentCount(recordComponentCount),
			_index(0)
		{
		}

		U_16 getNameIndex() const { return _recordComponentsInfo[_index].nameIndex; }
		U_16 getDescriptorIndex() const { return _recordComponentsInfo[_index].descriptorIndex; }		
		U_16 getGenericSignatureIndex() const { return _recordComponentsInfo[_index].genericSignatureIndex; }
		U_16 getRecordComponentIndex() const { return _index; }

		bool hasGenericSignature() const { return _recordComponentsInfo[_index].hasGenericSignature; }
		bool hasAnnotation() const { return _recordComponentsInfo[_index].annotationsAttribute != NULL; }
		bool hasTypeAnnotation() const { return _recordComponentsInfo[_index].typeAnnotationsAttribute != NULL; }

		bool isNotDone() const { return _index < _recordComponentCount; }
		void next() { _index++; }

	private:
		RecordComponentInfo *_recordComponentsInfo;
		U_16 _recordComponentCount;
		U_16 _index;
};

	/*
	 * Iteration functions.
	 */

	void annotationElementDo(AnnotationElementVisitor *annotationElementVisitor, U_16 elementNameIndex, J9CfrAnnotationElement *annotationElement)
	{
		switch (annotationElement->tag) {
		case 'e':
			annotationElementVisitor->visitEnum(elementNameIndex,
					((J9CfrAnnotationElementEnum *)annotationElement)->typeNameIndex,
					((J9CfrAnnotationElementEnum *)annotationElement)->constNameIndex);
			break;
		case 'c':
			annotationElementVisitor->visitClass(elementNameIndex,
					((J9CfrAnnotationElementClass *)annotationElement)->classInfoIndex);
			break;
		case '@': {
			NestedAnnotation nestedAnnotation(this, &(((J9CfrAnnotationElementAnnotation *)annotationElement)->annotationValue));
			annotationElementVisitor->visitNestedAnnotation(elementNameIndex,
					&nestedAnnotation);
			break;
		}
		case '[': {
			ArrayAnnotationElements arrayAnnotationElements(this, (J9CfrAnnotationElementArray *)annotationElement);
			annotationElementVisitor->visitArray(elementNameIndex,
					((J9CfrAnnotationElementArray *)annotationElement)->numberOfValues,
					&arrayAnnotationElements);
			break;
		}
		default:
			annotationElementVisitor->visitConstant(elementNameIndex, ((J9CfrAnnotationElementPrimitive *)annotationElement)->constValueIndex, annotationElement->tag);
			break;
		}
	}

	void annotationElementsDo(AnnotationElementVisitor *annotationElementVisitor, J9CfrAnnotationElementPair *elementValuePairs, U_16 elementValuePairCount)
	{
		J9CfrAnnotationElementPair *endElementValuePairs = elementValuePairs + elementValuePairCount;
		for (J9CfrAnnotationElementPair *elementValuePair = elementValuePairs; elementValuePair != endElementValuePairs; ++elementValuePair) {
			annotationElementDo(annotationElementVisitor, elementValuePair->elementNameIndex, elementValuePair->value);
		}
	}

	void defaultAnnotationDo(U_16 methodIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		if (NULL != _methodsInfo[methodIndex].defaultAnnotationAttribute) {
			if (NULL != annotationsAttributeVisitor) {
				annotationsAttributeVisitor->visitDefaultAnnotationAttribute(methodIndex, _methodsInfo[methodIndex].defaultAnnotationAttribute->length);
			}
			if (NULL != annotationElementVisitor) {
				annotationElementDo(annotationElementVisitor, _classFile->methods[methodIndex].nameIndex, _methodsInfo[methodIndex].defaultAnnotationAttribute->defaultValue);
			}
		}
	}

	VMINLINE void
	annotationsDo(U_16 index, J9CfrAttributeRuntimeVisibleAnnotations *annotationsAttribute, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		if (NULL != annotationsAttribute) {
			if ((annotationsAttribute->rawDataLength > 0) && (NULL != annotationsAttributeVisitor)) { /* Detected bad attribute: this will fail when reparsed by Java code. */
				annotationsAttributeVisitor->visitMalformedAnnotationsAttribute(annotationsAttribute->rawDataLength, annotationsAttribute->rawAttributeData);
			} else {
				if (NULL != annotationsAttributeVisitor) {
					annotationsAttributeVisitor->visitAnnotationsAttribute(index, annotationsAttribute->length, annotationsAttribute->numberOfAnnotations);
				}
				if ((NULL != annotationVisitor) || (NULL != annotationElementVisitor)) {
					J9CfrAnnotation *endAnnotations = annotationsAttribute->annotations + annotationsAttribute->numberOfAnnotations;
					for (J9CfrAnnotation *annotation = annotationsAttribute->annotations; annotation != endAnnotations; ++annotation) {
						if (NULL != annotationVisitor) {
							annotationVisitor->visitAnnotation(annotation->typeIndex, annotation->numberOfElementValuePairs);
						}
						if (NULL != annotationElementVisitor) {
							annotationElementsDo(annotationElementVisitor, annotation->elementValuePairs, annotation->numberOfElementValuePairs);
						}
					}
				}
			}
		}
	}
	void classAnnotationsDo(AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		annotationsDo(0, _annotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void classTypeAnnotationsDo(AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		typeAnnotationsDo(0, _typeAnnotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void fieldAnnotationDo(U_16 fieldIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		annotationsDo(fieldIndex, _fieldsInfo[fieldIndex].annotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void fieldTypeAnnotationDo(U_16 fieldIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		typeAnnotationsDo(fieldIndex, _fieldsInfo[fieldIndex].typeAnnotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void methodAnnotationDo(U_16 methodIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		annotationsDo(methodIndex, _methodsInfo[methodIndex].annotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void methodTypeAnnotationDo(U_16 methodIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		typeAnnotationsDo(methodIndex, _methodsInfo[methodIndex].methodTypeAnnotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void methodCodeTypeAnnotationDo(U_16 methodIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		typeAnnotationsDo(methodIndex, _methodsInfo[methodIndex].codeTypeAnnotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void recordComponentAnnotationDo(U_16 recordComponentIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor) {
		annotationsDo(recordComponentIndex, _recordComponentsInfo[recordComponentIndex].annotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void recordComponentTypeAnnotationDo(U_16 recordComponentIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor) {
		typeAnnotationsDo(recordComponentIndex, _recordComponentsInfo[recordComponentIndex].typeAnnotationsAttribute, annotationsAttributeVisitor, annotationVisitor, annotationElementVisitor);
	}

	void parameterAnnotationsDo(U_16 methodIndex, AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		J9CfrAttributeRuntimeVisibleParameterAnnotations *parameterAnnotationsAttribute = _methodsInfo[methodIndex].parameterAnnotationsAttribute;
		if (NULL != parameterAnnotationsAttribute) {
			if (parameterAnnotationsAttribute->rawDataLength > 0) { /* Detected bad attribute: this will fail when reparsed by Java code. */
				annotationsAttributeVisitor->visitMalformedAnnotationsAttribute(parameterAnnotationsAttribute->rawDataLength,
						parameterAnnotationsAttribute->rawAttributeData);
			} else {
				if (NULL != annotationsAttributeVisitor) {
					annotationsAttributeVisitor->visitParameterAnnotationsAttribute(methodIndex, parameterAnnotationsAttribute->length, parameterAnnotationsAttribute->numberOfParameters);
				}
				U_8 parameterIndex = 0;
				J9CfrParameterAnnotations *endParameterAnnotations = parameterAnnotationsAttribute->parameterAnnotations + parameterAnnotationsAttribute->numberOfParameters;
				for (J9CfrParameterAnnotations *parameterAnnotations = parameterAnnotationsAttribute->parameterAnnotations; parameterAnnotations != endParameterAnnotations; ++parameterAnnotations, ++parameterIndex) {
					if (NULL != annotationVisitor) {
						annotationVisitor->visitParameter(parameterAnnotations->numberOfAnnotations);
					}
					if ((NULL != annotationVisitor) || (NULL != annotationElementVisitor)) {
						J9CfrAnnotation *endAnnotations = parameterAnnotations->annotations + parameterAnnotations->numberOfAnnotations;
						for (J9CfrAnnotation *annotation = parameterAnnotations->annotations; annotation != endAnnotations; ++annotation) {
							if (NULL != annotationVisitor) {
								annotationVisitor->visitAnnotation(annotation->typeIndex, annotation->numberOfElementValuePairs);
							}
							if (NULL != annotationElementVisitor) {
								annotationElementsDo(annotationElementVisitor, annotation->elementValuePairs, annotation->numberOfElementValuePairs);
							}
						}
					}
				}
			}
		}
	}

	void typeAnnotationsDo(U_16 index, J9CfrAttributeRuntimeVisibleTypeAnnotations *typeAnnotationsAttribute,
			AnnotationsAttributeVisitor *annotationsAttributeVisitor, AnnotationVisitor *annotationVisitor, AnnotationElementVisitor *annotationElementVisitor)
	{
		Trc_BCU_Assert_NotNull(typeAnnotationsAttribute);
		if (typeAnnotationsAttribute->rawDataLength > 0) { /* Detected bad attribute: this will fail when reparsed by Java code. */
			annotationsAttributeVisitor->visitMalformedAnnotationsAttribute(typeAnnotationsAttribute->rawDataLength,
					typeAnnotationsAttribute->rawAttributeData);
		} else {
			if (NULL != annotationsAttributeVisitor) {
				annotationsAttributeVisitor->visitTypeAnnotationsAttribute(index, typeAnnotationsAttribute->length, typeAnnotationsAttribute->numberOfAnnotations);
			}
			if ((NULL != annotationVisitor) || (NULL != annotationElementVisitor)) {
				for (U_16 typeAnnotationIndex = 0; typeAnnotationIndex < typeAnnotationsAttribute->numberOfAnnotations; ++typeAnnotationIndex) {
					J9CfrTypeAnnotation *typeAnnotation = &(typeAnnotationsAttribute->typeAnnotations[typeAnnotationIndex]);
					if (NULL != annotationVisitor) {
						annotationVisitor->visitTypeAnnotation(typeAnnotation->targetType, &(typeAnnotation->targetInfo), &(typeAnnotation->typePath));
						annotationVisitor->visitAnnotation(typeAnnotation->annotation.typeIndex, typeAnnotation->annotation.numberOfElementValuePairs);
					}
					if (NULL != annotationElementVisitor) {
						annotationElementsDo(annotationElementVisitor, typeAnnotation->annotation.elementValuePairs, typeAnnotation->annotation.numberOfElementValuePairs);
					}

				}
			}
		}
	}

	/*
	 * Iterate over the constant pool indices corresponding to interface names (UTF8s).
	 * Also iterates over the injected interface names (UTF8s) in the "extra" cp slots.
	 * numOfInjectedInterfaces represents the number of extra slots containing the UTF8s.
	 */
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	void interfacesDo(ConstantPoolIndexVisitor *visitor, U_16 numOfInjectedInterfaces)
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	void interfacesDo(ConstantPoolIndexVisitor *visitor)
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	{
		U_16 *end = _classFile->interfaces + getInterfacesCount();
		for (U_16 *interface = _classFile->interfaces; interface != end; ++interface) {
			/* Each interface is a constantClass, use slot1 to get at the underlying UTF8 */
			visitor->visitConstantPoolIndex(U_16(_classFile->constantPool[ *interface ].slot1));
		}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		for (int i = 0; i < numOfInjectedInterfaces; i++) {
			visitor->visitConstantPoolIndex(getConstantPoolCount() + i);
		}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}

	/*
	 * Iterate over the constant pool indices corresponding to inner class names (UTF8s).
	 */
	void innerClassesDo(ConstantPoolIndexVisitor *visitor)
	{
		if (NULL != _innerClasses) {
			J9CfrClassesEntry *end = _innerClasses->classes + _innerClasses->numberOfClasses;
			U_16 thisClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, _classFile->thisClass);
			for (J9CfrClassesEntry *entry = _innerClasses->classes; entry != end; ++entry) {
				U_16  outerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->outerClassInfoIndex);
				/* In some cases, there might be two entries for the same class.
				 * But the UTF8 classname entry will be only one.
				 * Therefore comparing the UTF8 will find the matches, while comparing the class entries will not
				 */
				if (thisClassUTF8 == outerClassUTF8) {
					/* Member class - use slot1 to get at the underlying UTF8. */
					U_16  innerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->innerClassInfoIndex);
					visitor->visitConstantPoolIndex(innerClassUTF8);
				}
			}
		}
	}
	
	/*
	 * Iterate over the constant pool indices corresponding to enclosed inner class names (UTF8s).
	 */
	void enclosedInnerClassesDo(ConstantPoolIndexVisitor *visitor)
	{
		if (NULL != _innerClasses) {
			J9CfrClassesEntry *end = _innerClasses->classes + _innerClasses->numberOfClasses;
			U_16 thisClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, _classFile->thisClass);
			for (J9CfrClassesEntry *entry = _innerClasses->classes; entry != end; ++entry) {
				U_16  outerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->outerClassInfoIndex);
				U_16  innerClassUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, entry->innerClassInfoIndex);
				/* Count all remaining entries in the InnerClass attribute (except the entries covered by innerClassesDo())
				 * so as to check the InnerClass attribute between the inner classes and the enclosing class.
				 * See getDeclaringClass() for details.
				 */
				if ((thisClassUTF8 != outerClassUTF8) && (thisClassUTF8 != innerClassUTF8)) {
					visitor->visitConstantPoolIndex(innerClassUTF8);
				}
			}
		}
	}

#if JAVA_SPEC_VERSION >= 11
	void nestMembersDo(ConstantPoolIndexVisitor *visitor)
	{
		if (NULL != _nestMembers) {
			U_16 nestMembersCount = getNestMembersCount();
			for (U_16 i = 0; i < nestMembersCount; i++) {
				U_16 nestMemberUTF8 = UTF8_INDEX_FROM_CLASS_INDEX(_classFile->constantPool, _nestMembers->classes[i]);
				visitor->visitConstantPoolIndex(nestMemberUTF8);
			}
		}
	}
#endif /* JAVA_SPEC_VERSION >= 11 */

	/*
	 * Iterate over the bootstrap methods and their arguments.
	 */
	void bootstrapMethodsDo(BootstrapMethodVisitor *visitor)
	{
		if (NULL != _bootstrapMethodsAttribute) {
			J9CfrBootstrapMethod *end = _bootstrapMethodsAttribute->bootstrapMethods + _bootstrapMethodsAttribute->numberOfBootstrapMethods;
			for (J9CfrBootstrapMethod *bsm = _bootstrapMethodsAttribute->bootstrapMethods; bsm != end; ++bsm) {
				U_16 *endArguments = bsm->bootstrapArguments + bsm->numberOfBootstrapArguments;
				visitor->visitBootstrapMethod(bsm->bootstrapMethodIndex, bsm->numberOfBootstrapArguments);
				for (U_16 *argument = bsm->bootstrapArguments; argument != endArguments; ++argument) {
					visitor->visitBootstrapArgument(*argument);
				}
			}
		}
	}

	UTF8Iterator getUTF8Iterator() const { return UTF8Iterator(_classFile); }
	NameAndTypeIterator getNameAndTypeIterator() const { return NameAndTypeIterator(_classFile); }
	FieldIterator getFieldIterator() { return FieldIterator(_fieldsInfo, _classFile); }
	MethodIterator getMethodIterator() { return MethodIterator(_methodsInfo, _classFile); }
	RecordComponentIterator getRecordComponentIterator() { return RecordComponentIterator(_recordComponentsInfo, _recordComponentCount); }

	ClassFileOracle(BufferManager *bufferManager, J9CfrClassFile *classFile, ConstantPoolMap *constantPoolMap, U_8 * verifyExcludeAttribute, U_8 * romBuilderClassFileBuffer, ROMClassCreationContext *context);
	~ClassFileOracle();

	bool isOK() const { return OK == _buildResult; }
	BuildResult getBuildResult() const { return _buildResult; }

	/*
	 * Query methods.
	 */

	U_32 getClassFileSize() const { return _classFile->classFileSize; }
	U_16 getAccessFlags() const { return _classFile->accessFlags; }
	U_16 getSingleScalarStaticCount() const { return _singleScalarStaticCount; }
	U_16 getInterfacesCount() const { return _classFile->interfacesCount; }
	U_16 getMethodsCount() const { return _classFile->methodsCount; }
	U_16 getFieldsCount() const { return _classFile->fieldsCount; }
	U_16 getConstantPoolCount() const { return _classFile->constantPoolCount; }
	U_16 getObjectStaticCount() const { return _objectStaticCount; }
	U_16 getDoubleScalarStaticCount() const { return _doubleScalarStaticCount; }
	U_16 getMemberAccessFlags() const { return _memberAccessFlags; }
	U_16 getInnerClassCount() const { return _innerClassCount; }
	U_16 getEnclosedInnerClassCount() const { return _enclosedInnerClassCount; }
#if JAVA_SPEC_VERSION >= 11
	U_16 getNestMembersCount() const { return _nestMembersCount; }
	U_16 getNestHostNameIndex() const { return _nestHost; }
#endif /* JAVA_SPEC_VERSION >= 11 */
	U_16 getMajorVersion() const { return _classFile->majorVersion; }
	U_16 getMinorVersion() const { return _classFile->minorVersion; }
	U_32 getMaxBranchCount() const { return _maxBranchCount; }
	U_16 getClassNameIndex() const { return U_16(_classFile->constantPool[_classFile->thisClass].slot1); }
	U_16 getSuperClassNameIndex() const { return U_16(_classFile->constantPool[_classFile->superClass].slot1); }
	U_16 getOuterClassNameIndex() const { return _outerClassNameIndex; }
	U_16 getSimpleNameIndex() const { return _simpleNameIndex; }
	U_16 getEnclosingMethodClassRefIndex() const { return hasEnclosingMethod() ? _enclosingMethod->classIndex : 0; }
	U_16 getEnclosingMethodNameAndSignatureIndex() const { return hasEnclosingMethod() ? _enclosingMethod->methodIndex : 0; }
	U_16 getGenericSignatureIndex() const { return hasGenericSignature() ? _genericSignature->signatureIndex : 0; }
	U_16 getUTF8Length(U_16 cpIndex) const { return U_16(_classFile->constantPool[cpIndex].slot1); }
	U_8 *getUTF8Data(U_16 cpIndex) const { return _classFile->constantPool[cpIndex].bytes; }
	U_16 getNameAndSignatureNameUTF8Length(U_16 cpIndex) const { return getUTF8Length(U_16(_classFile->constantPool[cpIndex].slot1)); }
	U_8 *getNameAndSignatureNameUTF8Data(U_16 cpIndex) const { return getUTF8Data(U_16(_classFile->constantPool[cpIndex].slot1)); }
	U_16 getNameAndSignatureSignatureUTF8Length(U_16 cpIndex) const { return getUTF8Length(U_16(_classFile->constantPool[cpIndex].slot2)); }
	U_8 *getNameAndSignatureSignatureUTF8Data(U_16 cpIndex) const { return getUTF8Data(U_16(_classFile->constantPool[cpIndex].slot2)); }
	U_16 getSourceFileIndex() const { return hasSourceFile() ? _sourceFile->sourceFileIndex : 0; }
	U_32 getSourceDebugExtensionLength() const { return hasSourceDebugExtension() ? _sourceDebugExtension->length : 0; }
	U_8 *getSourceDebugExtensionData() const { return hasSourceDebugExtension() ? _sourceDebugExtension->value : NULL; }
	U_16 getBootstrapMethodCount() const { return hasBootstrapMethods() ? _bootstrapMethodsAttribute->numberOfBootstrapMethods : 0; }

	bool hasClassAnnotations() const { return NULL != _annotationsAttribute; }
	bool hasTypeAnnotations() const { return NULL != _typeAnnotationsAttribute; }
	U_16 getFieldNameIndex(U_16 fieldIndex) const { return _classFile->fields[fieldIndex].nameIndex; }
	U_16 getFieldDescriptorIndex(U_16 fieldIndex) const { return _classFile->fields[fieldIndex].descriptorIndex; }
	U_16 getMethodNameIndex(U_16 methodIndex) const { return _classFile->methods[methodIndex].nameIndex; }
	void sortLineNumberTable(U_16 methodIndex, J9CfrLineNumberTableEntry *lineNumbersInfo);
	U_16 getMethodDescriptorIndex(U_16 methodIndex) const { return _classFile->methods[methodIndex].descriptorIndex; }
	void getPrimitiveConstant(U_16 cpIndex, U_32 *slot1, U_32 *slot2) const
	{
		*slot1 = _classFile->constantPool[cpIndex].slot1;
		*slot2 = _classFile->constantPool[cpIndex].slot2;
	}

	U_8  getCPTag(U_16 cpIndex)   const { return _classFile->constantPool[cpIndex].tag; }
	U_32 getCPSlot1(U_16 cpIndex) const { return _classFile->constantPool[cpIndex].slot1; }
	U_32 getCPSlot2(U_16 cpIndex) const { return _classFile->constantPool[cpIndex].slot2; }

	/*
	  *  Long and Double CP entries are never tagged as "referenced" because they are only reached through LDC2W instructions,
	  *  so don't check the ".referenced" flag in isConstantLong() or isConstantDouble().
	  */
	bool isConstantLong(U_16 cpIndex) const { return CFR_CONSTANT_Long == _classFile->constantPool[cpIndex].tag; }
	bool isConstantDouble(U_16 cpIndex) const { return CFR_CONSTANT_Double == _classFile->constantPool[cpIndex].tag; }
	bool isConstantDynamic(U_16 cpIndex) const { return CFR_CONSTANT_Dynamic == _classFile->constantPool[cpIndex].tag;}
	bool isConstantInteger0(U_16 cpIndex) const { return (CFR_CONSTANT_Integer == _classFile->constantPool[cpIndex].tag) && (0 == _classFile->constantPool[cpIndex].slot1); }
	bool isConstantFloat0(U_16 cpIndex) const { return (CFR_CONSTANT_Float == _classFile->constantPool[cpIndex].tag) && (0 == _classFile->constantPool[cpIndex].slot1); }
	bool isUTF8AtIndexEqualToString(U_16 cpIndex, const char *string, UDATA stringSize) { return (getUTF8Length(cpIndex) == (stringSize - 1)) && (0 == memcmp(getUTF8Data(cpIndex), string, stringSize - 1)); }
	bool hasEmptyFinalizeMethod() const { return _hasEmptyFinalizeMethod; }
	bool hasFinalFields() const { return _hasFinalFields; }
	bool isClassContended() const { return _isClassContended; }
	bool isClassUnmodifiable() const { return _isClassUnmodifiable; }
	bool hasNonStaticNonAbstractMethods() const { return _hasNonStaticNonAbstractMethods; }
	bool hasFinalizeMethod() const { return _hasFinalizeMethod; }
	bool isCloneable() const { return _isCloneable; }
	bool isSerializable() const { return _isSerializable; }
	bool isSynthetic() const { return _isSynthetic; }
	bool hasEnclosingMethod() const { return NULL != _enclosingMethod; }
	bool hasGenericSignature() const { return NULL != _genericSignature; }
	bool hasSimpleName() const { return 0 != getSimpleNameIndex(); }
	bool hasVerifyExcludeAttribute() const { return _hasVerifyExcludeAttribute; }
	bool hasSourceFile() const { return NULL != _sourceFile; }
	bool hasSourceDebugExtension() const { return NULL != _sourceDebugExtension; }
	bool hasBootstrapMethods() const { return NULL != _bootstrapMethodsAttribute; }
	bool hasClinit() const { return _hasClinit; }
	bool annotationRefersDoubleSlotEntry() const { return _annotationRefersDoubleSlotEntry; }
	bool isInnerClass() const { return _isInnerClass; }
	bool needsStaticConstantInit() const { return _needsStaticConstantInit; }
	bool isRecord() const { return _isRecord; }
	U_16 getRecordComponentCount() const { return _recordComponentCount; }
	bool isSealed() const { return _isSealed; }
	U_16 getPermittedSubclassesClassCount() const { return _isSealed ? _permittedSubclassesAttribute->numberOfClasses : 0; }
	bool isValueBased() const { return _isClassValueBased; }
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	bool needsIdentityInterface() const { return _isIdentityInterfaceNeeded; }
	bool hasIdentityInterface() const { return _hasIdentityInterface; }
	bool isValueType() const { return _isValueType; }
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

	U_16 getPermittedSubclassesClassNameAtIndex(U_16 index) const {
		U_16 result = 0;
		if (_isSealed) {
			U_16 classCpIndex = _permittedSubclassesAttribute->classes[index];
			result = _classFile->constantPool[classCpIndex].slot1;
		}
		return result;
	}


	U_8 constantDynamicType(U_16 cpIndex) const
	{
		J9CfrConstantPoolInfo* nas = &_classFile->constantPool[_classFile->constantPool[cpIndex].slot2];
		J9CfrConstantPoolInfo* signature = &_classFile->constantPool[nas->slot2];
		U_8 result = 0;

		if ('D' == signature->bytes[0]) {
			result = JBldc2dw;
		} else if ('J' == signature->bytes[0]) {
			result = JBldc2lw;
		} else {
			Trc_BCU_Assert_ShouldNeverHappen();
		}

		return result;
	}

private:
	class InterfaceVisitor;

	enum {
		FRAMEITERATORSKIP_ANNOTATION,
		SUN_REFLECT_CALLERSENSITIVE_ANNOTATION,
		JDK_INTERNAL_REFLECT_CALLERSENSITIVE_ANNOTATION,
		JAVA8_CONTENDED_ANNOTATION,
		CONTENDED_ANNOTATION,
		UNMODIFIABLE_ANNOTATION,
		VALUEBASED_ANNOTATION,
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
		HIDDEN_ANNOTATION,
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
#if JAVA_SPEC_VERSION >= 16
		SCOPED_ANNOTATION,
#endif /* JAVA_SPEC_VERSION >= 16*/
		KNOWN_ANNOTATION_COUNT
	};

	struct KnownAnnotation
	{
		const char *name;
		size_t size;
	};

	static KnownAnnotation _knownAnnotations[];
	BuildResult _buildResult;
	BufferManager *_bufferManager;
	J9CfrClassFile *_classFile;
	ConstantPoolMap *_constantPoolMap;
	U_8 *_verifyExcludeAttribute;
	U_8 *_romBuilderClassFileBuffer;
	UDATA _bctFlags;
	ROMClassCreationContext *_context;

	U_16 _singleScalarStaticCount;
	U_16 _objectStaticCount;
	U_16 _doubleScalarStaticCount;
	U_16 _memberAccessFlags;
	U_16 _innerClassCount;
	U_16 _enclosedInnerClassCount;
#if JAVA_SPEC_VERSION >= 11
	U_16 _nestMembersCount;
	U_16 _nestHost;
#endif /* JAVA_SPEC_VERSION >= 11 */
	U_32 _maxBranchCount;
	U_16 _outerClassNameIndex;
	U_16 _simpleNameIndex;
	U_16 _recordComponentCount;

	bool _hasEmptyFinalizeMethod;
	bool _hasFinalFields;
	bool _hasNonStaticNonAbstractMethods;
	bool _hasFinalizeMethod;
	bool _isCloneable;
	bool _isSerializable;
	bool _isSynthetic;
	bool _isClassContended;
	bool _isClassUnmodifiable;
	bool _hasVerifyExcludeAttribute;
	bool _hasFrameIteratorSkipAnnotation;
	bool _hasClinit;
	bool _annotationRefersDoubleSlotEntry;
	bool _isInnerClass;
	bool _needsStaticConstantInit;
	bool _isRecord;
	bool _isSealed;
	bool _isClassValueBased;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	bool _hasIdentityInterface;
	bool _isIdentityInterfaceNeeded;
	bool _isValueType;
	bool _hasNonStaticSynchronizedMethod;
	bool _hasNonStaticFields;
	bool _hasNonEmptyConstructor;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */

	FieldInfo *_fieldsInfo;
	MethodInfo *_methodsInfo;
	RecordComponentInfo *_recordComponentsInfo;

	J9CfrAttributeSignature *_genericSignature;
	J9CfrAttributeEnclosingMethod *_enclosingMethod;
	J9CfrAttributeSourceFile *_sourceFile;
	J9CfrAttributeUnknown *_sourceDebugExtension;
	J9CfrAttributeRuntimeVisibleAnnotations *_annotationsAttribute;
	J9CfrAttributeRuntimeVisibleTypeAnnotations *_typeAnnotationsAttribute;
	J9CfrAttributeInnerClasses *_innerClasses;
	J9CfrAttributeBootstrapMethods *_bootstrapMethodsAttribute;
	J9CfrAttributePermittedSubclasses *_permittedSubclassesAttribute;
#if JAVA_SPEC_VERSION >= 11
	J9CfrAttributeNestMembers *_nestMembers;
#endif /* JAVA_SPEC_VERSION >= 11 */

	void walkHeader();
	void walkFields();
	void walkAttributes();
	void checkHiddenClass();
	void walkInterfaces();
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	void checkAndRecordIsIdentityInterfaceNeeded();
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */
	void walkMethods();
	void walkRecordComponents(J9CfrAttributeRecord *attrib);

	UDATA walkAnnotations(U_16 annotationsCount, J9CfrAnnotation *annotations, UDATA knownAnnotationSet);
	void walkTypeAnnotations(U_16 annotationsCount, J9CfrTypeAnnotation *annotations);
	void walkAnnotationElement(J9CfrAnnotationElement * annotationElement);

	void computeSendSlotCount(U_16 methodIndex);

	void walkMethodAttributes(U_16 methodIndex);
	void walkMethodThrownExceptions(U_16 methodIndex);
	void walkMethodCodeAttribute(U_16 methodIndex);
	void throwGenericErrorWithCustomMsg(UDATA code, UDATA offset);
	void walkMethodCodeAttributeAttributes(U_16 methodIndex);
	void walkMethodCodeAttributeCaughtExceptions(U_16 methodIndex);
	void walkMethodCodeAttributeCode(U_16 methodIndex);
	void walkMethodMethodParametersAttribute(U_16 methodIndex);

	U_8 * walkStackMapSlots(U_8 *framePointer, U_16 typeInfoCount);

	bool methodIsFinalize(U_16 methodIndex, bool isForwarder);
	bool methodIsEmpty(U_16 methodIndex);
	bool methodIsForwarder(U_16 methodIndex);
	bool methodIsGetter(U_16 methodIndex);
	bool methodIsVirtual(U_16 methodIndex);
	bool methodIsObjectConstructor(U_16 methodIndex);
	bool methodIsClinit(U_16 methodIndex);
	bool methodIsNonStaticNonAbstract(U_16 methodIndex);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	bool methodIsConstructor(U_16 methodIndex);
	bool methodIsNonStaticSynchronized(U_16 methodIndex);
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

	bool shouldConvertInvokeVirtualToInvokeSpecialForMethodRef(U_16 methodRefCPIndex);
	UDATA shouldConvertInvokeVirtualToMethodHandleBytecodeForMethodRef(U_16 methodRefCPIndex);

	VMINLINE bool containsKnownAnnotation(UDATA knownAnnotationSet, UDATA knownAnnotation);
	VMINLINE UDATA addAnnotationBit(UDATA annotationBits, UDATA knownAnnotation);

	VMINLINE void addBytecodeFixupEntry(BytecodeFixupEntry *entry, U_32 codeIndex, U_16 cpIndex, U_8 type);

	VMINLINE void markClassAsReferenced(U_16 classCPIndex);
	VMINLINE void markClassNameAsReferenced(U_16 classCPIndex);
	VMINLINE void markStringAsReferenced(U_16 cpIndex);
	VMINLINE void markNameAndDescriptorAsReferenced(U_16 nasCPIndex);
	VMINLINE void markFieldRefAsReferenced(U_16 cpIndex);
	VMINLINE void markMethodRefAsReferenced(U_16 cpIndex);
	VMINLINE void markMethodTypeAsReferenced(U_16 cpIndex);
	VMINLINE void markMethodHandleAsReferenced(U_16 cpIndex);
	VMINLINE void markMethodRefForMHInvocationAsReferenced(U_16 cpIndex);

	VMINLINE void markConstantAsReferenced(U_16 cpIndex);
	VMINLINE void markConstantDynamicAsReferenced(U_16 cpIndex);
	VMINLINE void markConstantNameAndTypeAsReferenced(U_16 cpIndex);
	VMINLINE void markConstantUTF8AsReferenced(U_16 cpIndex);

	VMINLINE void markConstantAsUsedByAnnotation(U_16 cpIndex);
	VMINLINE void markConstantAsUsedByLDC(U_8 cpIndex);
	VMINLINE void markConstantAsUsedByLDC2W(U_16 cpIndex);

	VMINLINE void markClassAsUsedByInstanceOf(U_16 classCPIndex);
	VMINLINE void markClassAsUsedByCheckCast(U_16 classCPIndex);
	VMINLINE void markClassAsUsedByMultiANewArray(U_16 classCPIndex);
	VMINLINE void markClassAsUsedByANewArray(U_16 classCPIndex);
	VMINLINE void markClassAsUsedByNew(U_16 classCPIndex);

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	VMINLINE void markClassAsUsedByDefaultValue(U_16 classCPIndex);
	VMINLINE void markFieldRefAsUsedByWithField(U_16 fieldRefCPIndex);
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

	VMINLINE void markInvokeDynamicInfoAsUsedByInvokeDynamic(U_16 cpIndex);

	VMINLINE void markFieldRefAsUsedByGetStatic(U_16 fieldRefCPIndex);
	VMINLINE void markFieldRefAsUsedByPutStatic(U_16 fieldRefCPIndex);
	VMINLINE void markFieldRefAsUsedByGetField(U_16 fieldRefCPIndex);
	VMINLINE void markFieldRefAsUsedByPutField(U_16 fieldRefCPIndex);

	VMINLINE void markMethodRefAsUsedByInvokeVirtual(U_16 methodRefCPIndex);
	VMINLINE void markMethodRefAsUsedByInvokeSpecial(U_16 methodRefCPIndex);
	VMINLINE void markMethodRefAsUsedByInvokeStatic(U_16 methodRefCPIndex);
	VMINLINE void markMethodRefAsUsedByInvokeInterface(U_16 methodRefCPIndex);
	VMINLINE void markMethodRefAsUsedByInvokeHandle(U_16 methodRefCPIndex);
	VMINLINE void markMethodRefAsUsedByInvokeHandleGeneric(U_16 methodRefCPIndex);
	VMINLINE void markConstantBasedOnCpType(U_16 cpIndex, bool assertNotDoubleOrLong);


	static int compareLineNumbers(const void *left, const void *right);
	void compressLineNumberTable(U_16 methodIndex, U_32 lineNumbersCount);
	void sortAndCompressLineNumberTable(U_16 methodIndex, U_32 lineNumbersCount, U_8 *lineNumbersInfoCompressedInitial);
};

#endif /* CLASSFILEORACLE_HPP_ */

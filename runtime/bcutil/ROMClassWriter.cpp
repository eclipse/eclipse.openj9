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
 * ROMClassWriter.cpp
 */
#include "ROMClassWriter.hpp"

#include "BufferManager.hpp"
#include "ClassFileOracle.hpp"
#include "ConstantPoolMap.hpp"
#include "Cursor.hpp"
#include "SRPKeyProducer.hpp"
#include "ROMClassVerbosePhase.hpp"

#include "bcnames.h"
#include "cfreader.h"
#include "j9.h"
#include "j9protos.h"
#include "ut_j9bcu.h"

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

class ROMClassWriter::CheckSize
{
public:
	CheckSize(Cursor *cursor, UDATA expectedSize) :
		_cursor(cursor),
		_expectedSize(expectedSize),
		_start(cursor->getCount())
	{
	}

	~CheckSize()
	{
		Trc_BCU_Assert_Equals(_cursor->getCount() - _start, _expectedSize);
	}

private:
	Cursor *_cursor;
	UDATA _expectedSize;
	UDATA _start;
};

class ROMClassWriter::AnnotationElementWriter : public ClassFileOracle::AnnotationElementVisitor
{
public:
	AnnotationElementWriter(Cursor *cursor, ConstantPoolMap *constantPoolMap, ClassFileOracle *classFileOracle) :
		_constantPoolMap(constantPoolMap),
		_cursor(cursor),
		_classFileOracle(classFileOracle)
	{
	}

	virtual void visitConstant(U_16 elementNameIndex, U_16 cpIndex, U_8 elementType)
	{
		_cursor->writeU8(elementType, Cursor::GENERIC);
		_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForAnnotation(cpIndex), Cursor::GENERIC);
	}

	virtual void visitEnum(U_16 elementNameIndex, U_16 typeNameIndex, U_16 constNameIndex)
	{
		_cursor->writeU8('e', Cursor::GENERIC);
		_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForAnnotation(typeNameIndex), Cursor::GENERIC);
		_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForAnnotation(constNameIndex), Cursor::GENERIC);
	}

	virtual void visitClass(U_16 elementNameIndex, U_16 cpIndex)
	{
		AnnotationElementWriter::visitConstant(elementNameIndex, cpIndex, 'c');
	}

	virtual void visitArray(U_16 elementNameIndex, U_16 elementCount, ClassFileOracle::ArrayAnnotationElements *arrayAnnotationElements)
	{
		_cursor->writeU8('[', Cursor::GENERIC);
		_cursor->writeBigEndianU16(elementCount, Cursor::GENERIC);

		AnnotationElementWriter annotationElementWriter(_cursor, _constantPoolMap, _classFileOracle);
		arrayAnnotationElements->elementsDo(&annotationElementWriter);
	}

	virtual void visitNestedAnnotation(U_16 elementNameIndex, ClassFileOracle::NestedAnnotation *nestedAnnotation);

protected:
	ConstantPoolMap *_constantPoolMap;
	Cursor *_cursor;
	ClassFileOracle *_classFileOracle;
};

class ROMClassWriter::AnnotationWriter :
	public ROMClassWriter::AnnotationElementWriter,
	public ClassFileOracle::AnnotationsAttributeVisitor,
	public ClassFileOracle::AnnotationVisitor
{
public:
	AnnotationWriter(Cursor *cursor, ConstantPoolMap *constantPoolMap, ClassFileOracle *classFileOracle) :
		AnnotationElementWriter(cursor, constantPoolMap, classFileOracle)
	{
	}

	void visitConstant(U_16 elementNameIndex, U_16 cpIndex, U_8 elementType)
	{
		writeElementName(elementNameIndex);
		AnnotationElementWriter::visitConstant(elementNameIndex, cpIndex, elementType);
	}

	void visitEnum(U_16 elementNameIndex, U_16 typeNameIndex, U_16 constNameIndex)
	{
		writeElementName(elementNameIndex);
		AnnotationElementWriter::visitEnum(elementNameIndex, typeNameIndex, constNameIndex);
	}

	void visitClass(U_16 elementNameIndex, U_16 cpIndex)
	{
		writeElementName(elementNameIndex);
		AnnotationElementWriter::visitClass(elementNameIndex, cpIndex);
	}

	void visitArray(U_16 elementNameIndex, U_16 elementCount, ClassFileOracle::ArrayAnnotationElements *arrayAnnotationElements)
	{
		writeElementName(elementNameIndex);
		AnnotationElementWriter::visitArray(elementNameIndex, elementCount, arrayAnnotationElements);
	}

	void visitNestedAnnotation(U_16 elementNameIndex, ClassFileOracle::NestedAnnotation *nestedAnnotation)
	{
		writeElementName(elementNameIndex);
		AnnotationElementWriter::visitNestedAnnotation(elementNameIndex, nestedAnnotation);
	}

	void visitAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_16 numberOfAnnotations)
	{
		writeAnnotationAttribute(length);
		_cursor->writeBigEndianU16(numberOfAnnotations, Cursor::GENERIC);
	}

	void visitDefaultAnnotationAttribute(U_16 fieldOrMethodIndex, U_32 length)
	{
		writeAnnotationAttribute(length);
	}

	void visitParameterAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_8 numberOfParameters)
	{
		writeAnnotationAttribute(length);
		_cursor->writeU8(numberOfParameters, Cursor::GENERIC);
	}

	void visitTypeAnnotationsAttribute(U_16 fieldOrMethodIndex, U_32 length, U_16 numberOfAnnotations)
	{
		writeAnnotationAttribute(length);
		_cursor->writeBigEndianU16(numberOfAnnotations, Cursor::GENERIC);
	}

	void visitAnnotation(U_16 typeIndex, U_16 elementValuePairCount)
	{
		_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForReference(typeIndex), Cursor::GENERIC);
		_cursor->writeBigEndianU16(elementValuePairCount, Cursor::GENERIC);
	}

	void visitTypeAnnotation(U_8 targetType, J9CfrTypeAnnotationTargetInfo *targetInfo, J9CfrTypePath *typePath)
	{
		_cursor->writeU8(targetType, Cursor::GENERIC);
		switch (targetType) { /* use values from the JVM spec. Java SE 8 Edition, table 4.7.20-A&B */
		case CFR_TARGET_TYPE_TypeParameterGenericClass:
		case CFR_TARGET_TYPE_TypeParameterGenericMethod:
			_cursor->writeU8(targetInfo->typeParameterTarget.typeParameterIndex, Cursor::GENERIC);
		break;
		case CFR_TARGET_TYPE_TypeInExtends:
			_cursor->writeBigEndianU16(targetInfo->supertypeTarget.supertypeIndex, Cursor::GENERIC);
		break;

		case CFR_TARGET_TYPE_TypeInBoundOfGenericClass:
		case CFR_TARGET_TYPE_TypeInBoundOfGenericMethod:
			_cursor->writeU8(targetInfo->typeParameterBoundTarget.typeParameterIndex, Cursor::GENERIC);
			_cursor->writeU8(targetInfo->typeParameterBoundTarget.boundIndex, Cursor::GENERIC);
		break;
		case CFR_TARGET_TYPE_TypeInFieldDecl:
		case CFR_TARGET_TYPE_ReturnType:
		case CFR_TARGET_TYPE_ReceiverType: /* empty_target */
		break;

		case CFR_TARGET_TYPE_TypeInFormalParam:
			_cursor->writeU8(targetInfo->methodFormalParameterTarget.formalParameterIndex, Cursor::GENERIC);
		break;

		case CFR_TARGET_TYPE_TypeInThrows:
			_cursor->writeBigEndianU16(targetInfo->throwsTarget.throwsTypeIndex, Cursor::GENERIC);
		break;

		case CFR_TARGET_TYPE_TypeInLocalVar:
		case CFR_TARGET_TYPE_TypeInResourceVar: {
			J9CfrLocalvarTarget *t = &(targetInfo->localvarTarget);
			_cursor->writeBigEndianU16(t->tableLength, Cursor::GENERIC);
			for (U_32 ti=0; ti < t->tableLength; ++ti) {
				J9CfrLocalvarTargetEntry *te = &(t->table[ti]);
				_cursor->writeBigEndianU16(te->startPC, Cursor::GENERIC);
				_cursor->writeBigEndianU16(te->length, Cursor::GENERIC);
				_cursor->writeBigEndianU16(te->index, Cursor::GENERIC);
			}
		};
		break;
		case CFR_TARGET_TYPE_TypeInExceptionParam:
			_cursor->writeBigEndianU16(targetInfo->catchTarget.exceptiontableIndex, Cursor::GENERIC);
		break;

		case CFR_TARGET_TYPE_TypeInInstanceof:
		case CFR_TARGET_TYPE_TypeInNew:
		case CFR_TARGET_TYPE_TypeInMethodrefNew:
		case CFR_TARGET_TYPE_TypeInMethodrefIdentifier:
			_cursor->writeBigEndianU16(targetInfo->offsetTarget.offset, Cursor::GENERIC);
		break;

		case CFR_TARGET_TYPE_TypeInCast:
		case CFR_TARGET_TYPE_TypeForGenericConstructorInNew:
		case CFR_TARGET_TYPE_TypeForGenericMethodInvocation:
		case CFR_TARGET_TYPE_TypeForGenericConstructorInMethodRef:
		case CFR_TARGET_TYPE_TypeForGenericMethodInvocationInMethodRef:
			_cursor->writeBigEndianU16(targetInfo->typeArgumentTarget.offset, Cursor::GENERIC);
			_cursor->writeU8(targetInfo->typeArgumentTarget.typeArgumentIndex, Cursor::GENERIC);
		break;

		default:
			break;
		}
		_cursor->writeU8(typePath->pathLength, Cursor::GENERIC);
		for (U_8 pathIndex = 0; pathIndex < typePath->pathLength; ++pathIndex) {
			J9CfrTypePathEntry* pathEntry = &(typePath->path[pathIndex]);
			_cursor->writeU8(pathEntry->typePathKind, Cursor::GENERIC);
			_cursor->writeU8(pathEntry->typeArgumentIndex, Cursor::GENERIC);
		}
	}

	void visitMalformedAnnotationsAttribute(U_32 rawDataLength, U_8 *rawAttributeData)
	{
		writeAnnotationAttribute(rawDataLength);
		for (U_32 byteIndex = 0; byteIndex < rawDataLength; ++byteIndex) {
			_cursor->writeU8(rawAttributeData[byteIndex], Cursor::GENERIC);
		}

	}

	void visitParameter(U_16 numberOfAnnotations)
	{
		_cursor->writeBigEndianU16(numberOfAnnotations, Cursor::GENERIC);
	}

private:
	void writeElementName(U_16 elementNameIndex)
	{
		_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForReference(elementNameIndex), Cursor::GENERIC);
	}

	void writeAnnotationAttribute(U_32 length)
	{
		_cursor->writeU32(length, Cursor::GENERIC); /* Native Endian */
	}
};
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
ROMClassWriter::ROMClassWriter(BufferManager *bufferManager, ClassFileOracle *classFileOracle, SRPKeyProducer *srpKeyProducer, ConstantPoolMap *constantPoolMap, ROMClassCreationContext *context, InterfaceInjectionInfo *interfaceInjectionInfo) :
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
ROMClassWriter::ROMClassWriter(BufferManager *bufferManager, ClassFileOracle *classFileOracle, SRPKeyProducer *srpKeyProducer, ConstantPoolMap *constantPoolMap, ROMClassCreationContext *context) :
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	_bufferManager(bufferManager),
	_classFileOracle(classFileOracle),
	_srpKeyProducer(srpKeyProducer),
	_constantPoolMap(constantPoolMap),
	_srpOffsetTable(NULL),
	_context(context),
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	_interfaceInjectionInfo(interfaceInjectionInfo),
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	_buildResult(OK),
	_interfacesSRPKey(srpKeyProducer->generateKey()),
	_methodsSRPKey(srpKeyProducer->generateKey()),
	_fieldsSRPKey(srpKeyProducer->generateKey()),
	_cpDescriptionShapeSRPKey(srpKeyProducer->generateKey()),
	_innerClassesSRPKey(srpKeyProducer->generateKey()),
	_enclosedInnerClassesSRPKey(srpKeyProducer->generateKey()),
#if JAVA_SPEC_VERSION >= 11
	_nestMembersSRPKey(srpKeyProducer->generateKey()),
#endif /* JAVA_SPEC_VERSION >= 11 */
	_optionalInfoSRPKey(srpKeyProducer->generateKey()),
	_enclosingMethodSRPKey(srpKeyProducer->generateKey()),
	_sourceDebugExtensionSRPKey(srpKeyProducer->generateKey()),
	_intermediateClassDataSRPKey(srpKeyProducer->generateKey()),
	_annotationInfoClassSRPKey(srpKeyProducer->generateKey()),
	_typeAnnotationInfoSRPKey(srpKeyProducer->generateKey()),
	_recordInfoSRPKey(srpKeyProducer->generateKey()),
	_callSiteDataSRPKey(srpKeyProducer->generateKey()),
	_staticSplitTableSRPKey(srpKeyProducer->generateKey()),
	_specialSplitTableSRPKey(srpKeyProducer->generateKey()),
	_varHandleMethodTypeLookupTableSRPKey(srpKeyProducer->generateKey()),
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	_injectedInterfaceInfoSRPKey(srpKeyProducer->generateKey()),
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	_permittedSubclassesInfoSRPKey(srpKeyProducer->generateKey())
{
	_methodNotes = (MethodNotes *) _bufferManager->alloc(classFileOracle->getMethodsCount() * sizeof(MethodNotes));
	if (NULL == _methodNotes) {
		_buildResult = OutOfMemory;
		return;
	}
	memset(_methodNotes, 0, classFileOracle->getMethodsCount() * sizeof(MethodNotes));
}

ROMClassWriter::~ROMClassWriter()
{
	_bufferManager->free(_methodNotes);
}

void
ROMClassWriter::writeROMClass(Cursor *cursor,
		Cursor *lineNumberCursor,
		Cursor *variableInfoCursor,
		Cursor *utf8Cursor,
		Cursor *classDataCursor,
		U_32 romSize, U_32 modifiers, U_32 extraModifiers, U_32 optionalFlags,
		MarkOrWrite markOrWrite)
{
	bool markAndCountOnly = (MARK_AND_COUNT_ONLY== markOrWrite);
	/*
	 * Write the J9ROMClass structure (AKA Header).
	 */
	if (markAndCountOnly) {
		if (!_context->isIntermediateDataAClassfile()) {
			if (NULL == _context->intermediateClassData()) {
				/* In case intermediate data is not explicitly provided in the _context,
				 * ROMClass being created can itself be used as intermediateClassData.
				 */
				cursor->mark(_intermediateClassDataSRPKey);
			} else {
				_srpOffsetTable->setInternedAt(_intermediateClassDataSRPKey, _context->intermediateClassData());
			}
		}
		cursor->skip(sizeof(J9ROMClass));
	} else {
		CheckSize _(cursor, sizeof(J9ROMClass));
		cursor->writeU32(romSize, Cursor::ROM_SIZE);
		cursor->writeU32(_classFileOracle->getSingleScalarStaticCount(), Cursor::GENERIC);
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getClassNameIndex()), Cursor::SRP_TO_UTF8_CLASS_NAME);
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getSuperClassNameIndex()), Cursor::SRP_TO_UTF8);
		cursor->writeU32(modifiers, Cursor::GENERIC);
		cursor->writeU32(extraModifiers, Cursor::GENERIC);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		cursor->writeU32(_classFileOracle->getInterfacesCount() + _interfaceInjectionInfo->numOfInterfaces, Cursor::GENERIC);
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		cursor->writeU32(_classFileOracle->getInterfacesCount(), Cursor::GENERIC);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		cursor->writeSRP(_interfacesSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getMethodsCount(), Cursor::GENERIC);
		cursor->writeSRP(_methodsSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getFieldsCount(), Cursor::GENERIC);
		cursor->writeSRP(_fieldsSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getObjectStaticCount(), Cursor::GENERIC);
		cursor->writeU32(_classFileOracle->getDoubleScalarStaticCount(), Cursor::GENERIC);
		cursor->writeU32(_constantPoolMap->getRAMConstantPoolCount(), Cursor::GENERIC);
		cursor->writeU32(_constantPoolMap->getROMConstantPoolCount(), Cursor::GENERIC);
		cursor->writeWSRP(_intermediateClassDataSRPKey, Cursor::SRP_TO_INTERMEDIATE_CLASS_DATA);
		if (NULL == _context->intermediateClassData()) {
			/* In case intermediate data is not explicitly provided in the _context,
			 * ROMClass being created is itself used as intermediateClassData,
			 * therefore intermediateDataLength should be the ROMClass size.
			 */
			cursor->writeU32(romSize, Cursor::INTERMEDIATE_CLASS_DATA_LENGTH);
		} else {
			cursor->writeU32(_context->intermediateClassDataLength(), Cursor::INTERMEDIATE_CLASS_DATA_LENGTH);
		}
		cursor->writeU32(OBJECT_HEADER_SHAPE_MIXED, Cursor::GENERIC);
		cursor->writeSRP(_cpDescriptionShapeSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getOuterClassNameIndex()), Cursor::SRP_TO_UTF8);
		cursor->writeU32(_classFileOracle->getMemberAccessFlags(), Cursor::GENERIC);
		cursor->writeU32(_classFileOracle->getInnerClassCount(), Cursor::GENERIC);
		cursor->writeSRP(_innerClassesSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getEnclosedInnerClassCount(), Cursor::GENERIC);
		cursor->writeSRP(_enclosedInnerClassesSRPKey, Cursor::SRP_TO_GENERIC);
#if JAVA_SPEC_VERSION >= 11
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getNestHostNameIndex()), Cursor::SRP_TO_UTF8);
		cursor->writeU16(_classFileOracle->getNestMembersCount(), Cursor::GENERIC);
		cursor->writeU16(0, Cursor::GENERIC); /* padding */
		cursor->writeSRP(_nestMembersSRPKey, Cursor::SRP_TO_GENERIC);
#endif /* JAVA_SPEC_VERSION >= 11 */
		cursor->writeU16(_classFileOracle->getMajorVersion(), Cursor::GENERIC);
		cursor->writeU16(_classFileOracle->getMinorVersion(), Cursor::GENERIC);
		cursor->writeU32(optionalFlags, Cursor::OPTIONAL_FLAGS);
		cursor->writeSRP(_optionalInfoSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getMaxBranchCount(), Cursor::GENERIC);
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
		cursor->writeU32(_constantPoolMap->getInvokeCacheCount(), Cursor::GENERIC);
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
		cursor->writeU32(_constantPoolMap->getMethodTypeCount(), Cursor::GENERIC);
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
		cursor->writeU32(_constantPoolMap->getVarHandleMethodTypeCount(), Cursor::GENERIC);
		cursor->writeU32(_classFileOracle->getBootstrapMethodCount(), Cursor::GENERIC);
		cursor->writeU32(_constantPoolMap->getCallSiteCount(), Cursor::GENERIC);
		cursor->writeSRP(_callSiteDataSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeU32(_classFileOracle->getClassFileSize(), Cursor::CLASS_FILE_SIZE);
		cursor->writeU32((U_32)_classFileOracle->getConstantPoolCount(), Cursor::GENERIC);
		cursor->writeU16(_constantPoolMap->getStaticSplitEntryCount(), Cursor::GENERIC);
		cursor->writeU16(_constantPoolMap->getSpecialSplitEntryCount(), Cursor::GENERIC);
		cursor->writeSRP(_staticSplitTableSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeSRP(_specialSplitTableSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->writeSRP(_varHandleMethodTypeLookupTableSRPKey, Cursor::SRP_TO_GENERIC);
		cursor->padToAlignment(sizeof(U_64), Cursor::GENERIC);
	}

	/*
	 * Write the rest of the contiguous portion of the ROMClass
	 */

	cursor->setClassNameIndex((_classFileOracle->getClassNameIndex()));
	writeConstantPool(cursor, markAndCountOnly);
	writeFields(cursor, markAndCountOnly);
	writeInterfaces(cursor, markAndCountOnly);
	writeInnerClasses(cursor, markAndCountOnly);
	writeEnclosedInnerClasses(cursor, markAndCountOnly);
#if JAVA_SPEC_VERSION >= 11
	writeNestMembers(cursor, markAndCountOnly);
#endif /* JAVA_SPEC_VERSION >= 11 */
	writeNameAndSignatureBlock(cursor);
	writeMethods(cursor, lineNumberCursor, variableInfoCursor, markAndCountOnly);
	writeConstantPoolShapeDescriptions(cursor, markAndCountOnly);
	writeAnnotationInfo(cursor);
	writeSourceDebugExtension(cursor);
	writeRecordComponents(cursor, markAndCountOnly);
	writePermittedSubclasses(cursor, markAndCountOnly);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	writeInjectedInterfaces(cursor, markAndCountOnly);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	writeOptionalInfo(cursor);
	writeCallSiteData(cursor, markAndCountOnly);
	writeVarHandleMethodTypeLookupTable(cursor, markAndCountOnly);
	writeStaticSplitTable(cursor, markAndCountOnly);
	writeSpecialSplitTable(cursor, markAndCountOnly);
	/* aligned to U_64 required by the shared classes */
	cursor->padToAlignment(sizeof(U_64), Cursor::GENERIC);

	/*
	 * Write UTF8s
	 */
	if ( NULL != utf8Cursor ) {
		ROMClassVerbosePhase v(_context, markAndCountOnly ? MarkAndCountUTF8s : WriteUTF8s);
		writeUTF8s(utf8Cursor);
	}

	/*
	 * Write Intermediate Class File bytes if
	 * (-Xshareclasses:enableBCI is specified and class file has not been modified by BCI agent) or
	 * (re-transformation is enabled and -Xshareclasses:enableBCI is not present).
	 */
	if ((NULL != classDataCursor)
		&& !_srpOffsetTable->isInterned(_intermediateClassDataSRPKey)
	) {
		classDataCursor->mark(_intermediateClassDataSRPKey);
		classDataCursor->writeData(_context->intermediateClassData(), _context->intermediateClassDataLength(), Cursor::INTERMEDIATE_CLASS_DATA);
		/* aligned to U_64 required by the shared classes */
		classDataCursor->padToAlignment(sizeof(U_64), Cursor::GENERIC);
	}
}

/*
 * Global table for ROMClassWriter::ConstantPoolWriter::visitMethodHandle().
 */
static const UDATA splitTypeMap[] = {
	0, /* UNUSED */
	ConstantPoolMap::GET_FIELD, /* GETFIELD */
	ConstantPoolMap::GET_STATIC, /* GETSTATIC */
	ConstantPoolMap::PUT_FIELD, /* PUTFIELD */
	ConstantPoolMap::PUT_STATIC, /* PUTSTATIC */
	ConstantPoolMap::INVOKE_VIRTUAL, /* INVOKEVIRTUAL */
	ConstantPoolMap::INVOKE_STATIC, /* INVOKESTATIC */
	ConstantPoolMap::INVOKE_SPECIAL, /* INVOKESPECIAL */
	ConstantPoolMap::INVOKE_SPECIAL, /* NEWINVOKESPECIAL */
	ConstantPoolMap::INVOKE_INTERFACE /* INVOKEINTERFACE */
};

class ROMClassWriter::ConstantPoolWriter : public ConstantPoolMap::ConstantPoolVisitor
{
public:
	ConstantPoolWriter(Cursor *cursor, SRPKeyProducer *srpKeyProducer, ConstantPoolMap *constantPoolMap) :
		_cursor(cursor),
		_srpKeyProducer(srpKeyProducer),
		_constantPoolMap(constantPoolMap)
	{
	}

	void visitClass(U_16 cfrCPIndex)
	{
		/* if the cfrCPIndex is for the class name, the data type should be SRP_TO_UTF8_CLASS_NAME to avoid comparing lambda class names */
		U_16 classNameIndex = _cursor->getClassNameIndex();
		if (((U_16)-1 != classNameIndex) && (_srpKeyProducer->mapCfrConstantPoolIndexToKey(classNameIndex) == _srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex))) {
			_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex), Cursor::SRP_TO_UTF8_CLASS_NAME);
		} else {
			_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex), Cursor::SRP_TO_UTF8);
		}
		_cursor->writeU32(BCT_J9DescriptionCpTypeClass, Cursor::GENERIC);
	}

	void visitString(U_16 cfrCPIndex)
	{
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex), Cursor::SRP_TO_UTF8);
		_cursor->writeU32(BCT_J9DescriptionCpTypeObject, Cursor::GENERIC);
	}

	void visitMethodType(U_16 cfrCPIndex, U_16 originFlags)
	{
		/* Assumes format: { SRP to UTF8, cpType } */
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex), Cursor::SRP_TO_UTF8);
		_cursor->writeU32((originFlags << BCT_J9DescriptionCpTypeShift) | BCT_J9DescriptionCpTypeMethodType, Cursor::GENERIC);
	}

	void visitMethodHandle(U_16 cfrKind, U_16 cfrCPIndex)
	{
		U_32 cpIndex = _constantPoolMap->getROMClassCPIndex(cfrCPIndex, splitTypeMap[cfrKind]);

		Trc_BCU_Assert_NotEquals(cpIndex, 0);

		/* assumes format: { U32 cpIndex, (cfrKind << 4) || cpType } */
		_cursor->writeU32(cpIndex, Cursor::GENERIC);
		_cursor->writeU32((cfrKind << BCT_J9DescriptionCpTypeShift) | BCT_J9DescriptionCpTypeMethodHandle, Cursor::GENERIC);
	}

	void visitConstantDynamic(U_16 bsmIndex, U_16 cfrCPIndex, U_32 primitiveFlag)
	{
		/* assumes format: { SRP to NameAndSignature, primitiveFlag || (bsmIndex << 4) || cpType } */
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cfrCPIndex), Cursor::SRP_TO_NAME_AND_SIGNATURE);
		_cursor->writeU32((bsmIndex << BCT_J9DescriptionCpTypeShift) | BCT_J9DescriptionCpTypeConstantDynamic | primitiveFlag, Cursor::GENERIC);
	}

	void visitSingleSlotConstant(U_32 slot1)
	{
		_cursor->writeU32(slot1, Cursor::GENERIC);
		_cursor->writeU32(BCT_J9DescriptionCpTypeScalar, Cursor::GENERIC);
	}

	void visitDoubleSlotConstant(U_32 slot1, U_32 slot2)
	{
#ifdef J9VM_ENV_DATA64
		_cursor->writeU64(slot1, slot2, Cursor::GENERIC);
#else
		_cursor->writeU32(slot1, Cursor::GENERIC);
		_cursor->writeU32(slot2, Cursor::GENERIC);
#endif
	}

	void visitFieldOrMethod(U_16 classRefCPIndex, U_16 nameAndSignatureCfrCPIndex)
	{
		_cursor->writeU32(classRefCPIndex, Cursor::GENERIC);
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(nameAndSignatureCfrCPIndex), Cursor::SRP_TO_NAME_AND_SIGNATURE);
	}

private:
	Cursor *_cursor;
	SRPKeyProducer *_srpKeyProducer;
	ConstantPoolMap *_constantPoolMap;
};

void
ROMClassWriter::writeConstantPool(Cursor *cursor, bool markAndCountOnly)
{
	UDATA constantPoolSize = UDATA(_constantPoolMap->getROMConstantPoolCount()) * sizeof(J9ROMConstantPoolItem);
	if (markAndCountOnly) {
		cursor->skip(constantPoolSize);
	} else {
		CheckSize _(cursor, constantPoolSize);

		/* Write the zeroth entry */
		cursor->writeU32(0, Cursor::GENERIC);
		cursor->writeU32(0, Cursor::GENERIC);

		ConstantPoolWriter writer(cursor, _srpKeyProducer, _constantPoolMap);
		_constantPoolMap->constantPoolDo(&writer);
	}
}

/*
 * The ROM class field has the following layout:
 * 	J9ROMFieldShape
 * 	4 or 8 bytes unsigned constant value if this is a static const field
 * 	4 bytes SRP to generic field signature if the field has a generic signature
 * 	4 bytes length of annotation data in bytes, followed by annotation data. Omitted if there are no field annotations.
 * 	4 bytes length of type annotation data in bytes followed by type annotation data.  Omitted if there are no type annotations.
 */

void
ROMClassWriter::writeFields(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_fieldsSRPKey);
	ClassFileOracle::FieldIterator iterator = _classFileOracle->getFieldIterator();
	while ( iterator.isNotDone() ) {
		if (markAndCountOnly) {
			cursor->skip(sizeof(J9ROMFieldShape));
		} else {
			CheckSize _(cursor, sizeof(J9ROMFieldShape));

			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getNameIndex()), Cursor::SRP_TO_UTF8);
			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getDescriptorIndex()), Cursor::SRP_TO_UTF8);

			U_32 fieldSignatureFlags(fieldModifiersLookupTable[iterator.getFirstByteOfDescriptor() - 'A']);
			U_32 modifiers(iterator.getAccessFlags() | (fieldSignatureFlags << 16));
			if (iterator.isConstant()) {
				modifiers |= J9FieldFlagConstant;
			}
			if (iterator.hasGenericSignature()) {
				modifiers |= J9FieldFlagHasGenericSignature;
			}
			if (iterator.isSynthetic()) {
				/* handle the synthetic attribute. In java 1.5 synthetic may be specified in the access flags as well so do not unset bit here */
				modifiers |= J9AccSynthetic;
			}
			if (iterator.isFieldContended()) {
				modifiers |= J9FieldFlagIsContended;
			}
			if (iterator.hasAnnotation()) {
				modifiers |= J9FieldFlagHasFieldAnnotations;
			}
			if (iterator.hasTypeAnnotation()) {
				modifiers |= J9FieldFlagHasTypeAnnotations;
			}

			cursor->writeU32(modifiers, Cursor::GENERIC);
		}

		if (iterator.isConstant()) { // Check not absolutely necessary, but might provide small performance win
			if (iterator.isConstantFloat() || iterator.isConstantInteger()) {
				cursor->writeU32(iterator.getConstantValueSlot1(), Cursor::GENERIC);
			} else if (iterator.isConstantDouble() || iterator.isConstantLong()) {
				cursor->writeU32(iterator.getConstantValueSlot1(), Cursor::GENERIC);
				cursor->writeU32(iterator.getConstantValueSlot2(), Cursor::GENERIC);
			} else if (iterator.isConstantString()) {
				cursor->writeU32(U_32(_constantPoolMap->getROMClassCPIndexForReference(iterator.getConstantValueConstantPoolIndex())), Cursor::GENERIC);
			}
		}

		if (iterator.hasGenericSignature()) {
			if (markAndCountOnly) { // TODO check if this is a performance win
				cursor->skip(sizeof(J9SRP));
			} else {
				cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getGenericSignatureIndex()), Cursor::SRP_TO_UTF8);
			}
		}

		if (iterator.hasAnnotation()) {
			/*
			 * u_32 length
			 * length * u_8
			 * pad to u_32 size
			 */
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->fieldAnnotationDo(iterator.getFieldIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		if (iterator.hasTypeAnnotation()) {
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->fieldTypeAnnotationDo(iterator.getFieldIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}


		iterator.next();
	}
}

/* mapping characters A..Z,[ */
const U_8 primitiveArrayTypeCharConversion[] = {
0,                           CFR_STACKMAP_TYPE_BYTE_ARRAY,  CFR_STACKMAP_TYPE_CHAR_ARRAY,  CFR_STACKMAP_TYPE_DOUBLE_ARRAY,
0,                           CFR_STACKMAP_TYPE_FLOAT_ARRAY, 0,                             0,
CFR_STACKMAP_TYPE_INT_ARRAY, CFR_STACKMAP_TYPE_LONG_ARRAY,  0,                             0,
0,                           0,                             0,                             0,
0,                           0,                             CFR_STACKMAP_TYPE_SHORT_ARRAY, 0,
0,                           0,                             0,                             0,
0,                           CFR_STACKMAP_TYPE_BOOL_ARRAY,  0};

class ROMClassWriter::CallSiteWriter : public ConstantPoolMap::CallSiteVisitor
{
public:
	CallSiteWriter(Cursor *cursor) :
		_cursor(cursor)
	{
	}

	void visitCallSite(U_16 nameAndSignatureIndex, U_16 bootstrapMethodIndex)
	{
		_cursor->writeU16(bootstrapMethodIndex, Cursor::GENERIC);
	}

private:
	Cursor *_cursor;
};

class ROMClassWriter::Helper :
	private ClassFileOracle::ConstantPoolIndexVisitor,
	private ClassFileOracle::ExceptionHandlerVisitor,
	private ClassFileOracle::StackMapFrameVisitor,
	private ClassFileOracle::VerificationTypeInfoVisitor,
	private ClassFileOracle::BootstrapMethodVisitor,
	private ClassFileOracle::MethodParametersVisitor,
	private ConstantPoolMap::SplitEntryVisitor,
	private ConstantPoolMap::CallSiteVisitor
{
public:
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(Cursor *cursor, bool markAndCountOnly,
			ClassFileOracle *classFileOracle, SRPKeyProducer *srpKeyProducer, SRPOffsetTable *srpOffsetTable, ConstantPoolMap *constantPoolMap,
			UDATA expectedSize, InterfaceInjectionInfo *interfaceInjectionInfo) :
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(Cursor *cursor, bool markAndCountOnly,
			ClassFileOracle *classFileOracle, SRPKeyProducer *srpKeyProducer, SRPOffsetTable *srpOffsetTable, ConstantPoolMap *constantPoolMap,
			UDATA expectedSize) :
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		_cursor(cursor),
		_classFileOracle(classFileOracle),
		_srpKeyProducer(srpKeyProducer),
		_srpOffsetTable(srpOffsetTable),
		_constantPoolMap(constantPoolMap),
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		_interfaceInjectionInfo(interfaceInjectionInfo),
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		_markAndCountOnly(markAndCountOnly)
	{
		if (_markAndCountOnly) {
			_cursor->skip(expectedSize);
		}
	}

	void writeInnerClasses()
	{
		if (!_markAndCountOnly) {
			_classFileOracle->innerClassesDo(this); /* visitConstantPoolIndex */
		}
	}

	void writeEnclosedInnerClasses()
	{
		if (!_markAndCountOnly) {
			_classFileOracle->enclosedInnerClassesDo(this); /* visitConstantPoolIndex */
		}
	}

#if JAVA_SPEC_VERSION >= 11
	void writeNestMembers()
	{
		if (!_markAndCountOnly) {
			_classFileOracle->nestMembersDo(this); /* visitConstantPoolIndex */
		}
	}
#endif /* JAVA_SPEC_VERSION >= 11 */

	void writeInterfaces()
	{
		if (!_markAndCountOnly) {
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			_classFileOracle->interfacesDo(this, _interfaceInjectionInfo->numOfInterfaces); /* visitConstantPoolIndex */
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			_classFileOracle->interfacesDo(this); /* visitConstantPoolIndex */
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		}
	}

	void writeNameAndSignatureBlock()
	{
		if (!_markAndCountOnly) {
			for (ClassFileOracle::NameAndTypeIterator iterator = _classFileOracle->getNameAndTypeIterator();
					iterator.isNotDone();
					iterator.next()) {
				U_16 cpIndex = iterator.getCPIndex();

				if (_constantPoolMap->isNATConstantReferenced(cpIndex)) {
					U_16 nameIndex = iterator.getNameIndex();
					U_16 descriptorIndex = iterator.getDescriptorIndex();

					_cursor->mark(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cpIndex));
					_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(nameIndex), Cursor::SRP_TO_UTF8);
					_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(descriptorIndex), Cursor::SRP_TO_UTF8);
				}
			}
		}
	}

	void writeUTF8Block()
	{
		if (!_markAndCountOnly) {
			for (ClassFileOracle::UTF8Iterator iterator = _classFileOracle->getUTF8Iterator();
					iterator.isNotDone();
					iterator.next()) {
				U_16 cpIndex = iterator.getCPIndex();

				if (_constantPoolMap->isUTF8ConstantReferenced(cpIndex)) {
					UDATA key = _srpKeyProducer->mapCfrConstantPoolIndexToKey(cpIndex);

					if (!_srpOffsetTable->isInterned(key)) {
						U_8* utf8Data = iterator.getUTF8Data();
						U_16 utf8Length = iterator.getUTF8Length();

						_cursor->mark(key);
						_cursor->writeUTF8(utf8Data, utf8Length, Cursor::GENERIC);
					}
				}
			}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			for (int i = 0; i < _interfaceInjectionInfo->numOfInterfaces; i++) {
				/* if the class requires injected interfaces an "extra" CP slot in the key table is added for each interface */
				_cursor->mark(_classFileOracle->getConstantPoolCount() + i);
				_cursor->writeUTF8((U_8*)J9UTF8_DATA(_interfaceInjectionInfo->interfaces[i]), J9UTF8_LENGTH(_interfaceInjectionInfo->interfaces[i]), Cursor::GENERIC);
			}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		}
	}

	void writeExceptionBlock(ClassFileOracle::MethodIterator *iterator)
	{
		if (!_markAndCountOnly) {
			iterator->exceptionHandlersDo(this); /* visitExceptionHandler */
			iterator->exceptionsThrownDo(this); /* visitConstantPoolIndex */
		}
	}

	void writeMethodParameters(ClassFileOracle::MethodIterator *iterator)
	{
		if (!_markAndCountOnly) {
			iterator->methodParametersDo(this); /* visitMethodParameters */
		}
	}

	void writeStackMap(ClassFileOracle::MethodIterator *methodIterator)
	{
		if (!_markAndCountOnly) {
			methodIterator->stackMapFramesDo(this); /* visitStackMapFrame */
		}
	}

	void writeCallSiteData()
	{
		if (!_markAndCountOnly) {
			CallSiteWriter callSiteWriter(_cursor);
			_constantPoolMap->callSitesDo(this); /* visitCallSite */
			_constantPoolMap->callSitesDo(&callSiteWriter);
		}
	}

	void writeVarHandleMethodTypeLookupTable()
	{
		if (!_markAndCountOnly) {
			U_16 lookupTableCount = _constantPoolMap->getVarHandleMethodTypeCount();
			if (lookupTableCount > 0) {
				U_16 *lookupTable = _constantPoolMap->getVarHandleMethodTypeLookupTable();

				/* The lookup table is a U_16[], so we need to pad it in order to keep the J9ROMClass alignment. */
				U_16 lookupTablePaddedCount = _constantPoolMap->getVarHandleMethodTypePaddedCount();
				U_16 *lookupTablePadding = lookupTable + lookupTableCount;
				UDATA lookupTablePaddingLength = lookupTablePaddedCount - lookupTableCount;

				/* Write the data section of the array */
				_cursor->writeData((U_8 *)lookupTable, lookupTableCount * sizeof(U_16), Cursor::GENERIC);

				/* Write the padding section of the array */
				if (lookupTablePaddingLength > 0) {
					_cursor->writeData((U_8 *)lookupTablePadding, lookupTablePaddingLength * sizeof(U_16), Cursor::GENERIC);
				}
			}
		}
	}

	void writeBootstrapMethods()
	{
		if (!_markAndCountOnly) {
			_classFileOracle->bootstrapMethodsDo(this); /* visitBootstrapMethod */
		}
	}

	void writeStaticSplitTable()
	{
		if (!_markAndCountOnly) {
			_constantPoolMap->staticSplitEntriesDo(this); /* visitSplitEntry */
		}
	}
	
	void writeSpecialSplitTable()
	{
		if (!_markAndCountOnly) {
			_constantPoolMap->specialSplitEntriesDo(this); /* visitSplitEntry */
		}
	}

private:
	/*
	 * Implement interfaces
	 */
	void visitConstantPoolIndex(U_16 cpIndex)
	{
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cpIndex), Cursor::SRP_TO_UTF8);
	}

	void visitMethodParameters(U_16 cpIndex, U_16 flag) {
		if (0 == cpIndex) {
			_cursor->writeU32(0, Cursor::GENERIC);
		} else {
			_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(cpIndex), Cursor::SRP_TO_UTF8);
		}
		_cursor->writeU16(flag, Cursor::GENERIC);
	}

	void visitExceptionHandler(U_32 startPC, U_32 endPC, U_32 handlerPC, U_16 exceptionClassCPIndex)
	{
		_cursor->writeU32(startPC, Cursor::GENERIC);
		_cursor->writeU32(endPC, Cursor::GENERIC);
		_cursor->writeU32(handlerPC, Cursor::GENERIC);
		_cursor->writeU32(_constantPoolMap->getROMClassCPIndexForReference(exceptionClassCPIndex), Cursor::GENERIC);
	}

	void visitStackMapObject(U_8 slotType, U_16 classCPIndex, U_16 classNameCPIndex)
	{
		U_8 *nameData = _classFileOracle->getUTF8Data(classNameCPIndex);
		U_16 nameLength = _classFileOracle->getUTF8Length(classNameCPIndex);
		/* special conversion of primitive array objects
		 * detect whether or not this is a primitive array e.g.,
		 *  [I or [[F, etc..
		 *  vs. an array of Objects  e.g.,
		 *  [Lcom/ibm/foo/Bar;
		 */
		if ((nameData[0] == '[') && (nameData[nameLength - 1] != ';')) {
			/*
			 * This is a primitive array object.
			 *
			 *  Encode the primitive array type in the tag field.
			 *  One of:
			 *   CFR_STACKMAP_TYPE_BYTE_ARRAY
			 *   CFR_STACKMAP_TYPE_BOOL_ARRAY
			 *   CFR_STACKMAP_TYPE_CHAR_ARRAY
			 *   CFR_STACKMAP_TYPE_DOUBLE_ARRAY
			 *   CFR_STACKMAP_TYPE_FLOAT_ARRAY
			 *   CFR_STACKMAP_TYPE_INT_ARRAY
			 *   CFR_STACKMAP_TYPE_LONG_ARRAY
			 *   CFR_STACKMAP_TYPE_SHORT_ARRAY
			 */
			_cursor->writeU8(primitiveArrayTypeCharConversion[nameData[nameLength - 1] - 'A'], Cursor::GENERIC);

			/* Also, encode the arity beyond 1
			 * (i.e., number of dimensions of the array - 1)
			 * in the next 2 bytes in Big Endian (since we are maintaining Sun StackMapTable format).
			 *
			 * The reason for encoding arity - 1 in verification type info in Stack maps for primitive array special cases are:
			 * The newarray and anewarray bytecodes assume that the array has only a single dimension.
			 * To create a multidimension array, multianewarray must be used.
			 * The primitive array access bytecodes (ie: iaload) can only be used on single dimension arrays.
			 * aaload must be used to access every dimension prior to the base dimension in a multi-arity primitive array.
			 * The constants in vrfytbl.c are based off the constants for the primitive types, and can't have the arity of 1 encoded if the constant is to be used for both purposes.
			 * (See rtverify.c verifyBytecodes() - the RTV_ARRAY_FETCH_PUSH & RTV_ARRAY_STORE cases of the switch)
			 * In addition, the code all through the verifier assumes this condition.
			 * Notes:
			 * See util/vrfytbl.c for bytecode tables.
			 * See constant definitions in cfreader.h and oti/bytecodewalk.h.
			 * bcverify/bcverify.c simulateStack() is the other place that creates stack maps.
			 */
			_cursor->writeBigEndianU16(nameLength - 2, Cursor::GENERIC);
		} else {
			/*
			 * Object_variable_info
			 *   u1 tag
			 *   u2 cpIndex (ROMClass constant pool index)
			 */
			_cursor->writeU8(slotType, Cursor::GENERIC);
			_cursor->writeBigEndianU16(_constantPoolMap->getROMClassCPIndexForReference(classCPIndex), Cursor::GENERIC);
		}
	}

	void visitStackMapNewObject(U_8 slotType, U_16 offset)
	{
		/*
		 * Uninitialized_variable_info
		 *   u1 tag
		 *   u2 offset (offset of the new instruction that created the object being stored in the location)
		 */
		_cursor->writeU8(slotType, Cursor::GENERIC);
		/* output the offset delta */
		_cursor->writeBigEndianU16(offset, Cursor::GENERIC);
	}

	void visitStackMapItem(U_8 slotType)
	{
		_cursor->writeU8(slotType, Cursor::GENERIC);
	}

	void visitStackMapFrame(U_16 localsCount, U_16 stackItemsCount, U_16 offsetDelta, U_8 frameType, ClassFileOracle::VerificationTypeInfo *typeInfo)
	{
		/*
		 * Stack Map Frame:
		 * consists of a 1 byte tag followed by zero or more bytes depending on the tag
		 *
		 * union stack_map_frame {
		 *	SAME
		 *	SAME_LOCALS_1_STACK
		 *	Reserved
		 *	SAME_LOCALS_1_STACK_EXTENDED
		 *	CHOP
		 *	SAME_EXTENDED
		 *	APPEND
		 *	FULL
		 * }
		 */

		/* output the frame tag */
		_cursor->writeU8(frameType, Cursor::GENERIC);

		if (CFR_STACKMAP_SAME_LOCALS_1_STACK > frameType) { /* 0..63 */
			/* SAME frame - no extra data */
		} else if (CFR_STACKMAP_SAME_LOCALS_1_STACK_END > frameType) { /* 64..127 */
			/*
			 * SAME_LOCALS_1_STACK {
			 * 		TypeInfo stackItems[1]
			 * };
			 */
			typeInfo->stackItemsDo(this);
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
			_cursor->writeBigEndianU16(offsetDelta, Cursor::GENERIC);
			typeInfo->stackItemsDo(this);
		} else if (CFR_STACKMAP_SAME_EXTENDED > frameType) { /* 248..250 */
			/*
			 * CHOP {
			 *		U_16 offsetDelta
			 * };
			 */
			_cursor->writeBigEndianU16(offsetDelta, Cursor::GENERIC);
		} else if (CFR_STACKMAP_SAME_EXTENDED == frameType) { /* 251 */
			/*
			 * SAME_EXTENDED {
			 *		U_16 offsetDelta
			 * };
			 */
			_cursor->writeBigEndianU16(offsetDelta, Cursor::GENERIC);
		} else if (CFR_STACKMAP_FULL > frameType) { /* 252..254 */
			/*
			 * APPEND {
			 *		U_16 offsetDelta
			 *		TypeInfo locals[frameType - CFR_STACKMAP_APPEND_BASE]
			 * };
			 */
			_cursor->writeBigEndianU16(offsetDelta, Cursor::GENERIC);
			typeInfo->localsDo(this);
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

			/* u2 offset delta */
			_cursor->writeBigEndianU16(offsetDelta, Cursor::GENERIC);

			/* handle locals */
			/* u2 number of locals  */
			_cursor->writeBigEndianU16(localsCount, Cursor::GENERIC);
			/* verification_type_info locals[number of locals] */
			typeInfo->localsDo(this);

			/* handle stack */
			/* u2 number of stack items */
			_cursor->writeBigEndianU16(stackItemsCount, Cursor::GENERIC);
			/* verification_type_info stack[number of stack items] */
			typeInfo->stackItemsDo(this);
		}
	}

	void visitBootstrapMethod(U_16 cpIndex, U_16 argumentCount)
	{
		_cursor->writeU16(_constantPoolMap->getROMClassCPIndexForReference(cpIndex), Cursor::GENERIC);
		_cursor->writeU16(argumentCount, Cursor::GENERIC);
	}

	void visitBootstrapArgument(U_16 cpIndex)
	{
		_cursor->writeU16(_constantPoolMap->getROMClassCPIndexForReference(cpIndex), Cursor::GENERIC);
	}

	void visitCallSite(U_16 nameAndSignatureIndex, U_16 bootstrapMethodIndex)
	{
		_cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(nameAndSignatureIndex), Cursor::SRP_TO_NAME_AND_SIGNATURE);
	}

	void visitSplitEntry(U_16 cpIndex)
	{
		_cursor->writeU16(cpIndex, Cursor::GENERIC);
	}

	Cursor *_cursor;
	ClassFileOracle *_classFileOracle;
	SRPKeyProducer *_srpKeyProducer;
	SRPOffsetTable *_srpOffsetTable;
	ConstantPoolMap *_constantPoolMap;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	InterfaceInjectionInfo *_interfaceInjectionInfo;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	bool _markAndCountOnly;
};

void
ROMClassWriter::writeInterfaces(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_interfacesSRPKey);
	UDATA size = UDATA(_classFileOracle->getInterfacesCount()) * sizeof(J9SRP);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	size += UDATA(_interfaceInjectionInfo->numOfInterfaces) * sizeof(J9SRP);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeInterfaces();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeInterfaces();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

void
ROMClassWriter::writeInnerClasses(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_innerClassesSRPKey);
	UDATA size = UDATA(_classFileOracle->getInnerClassCount()) * sizeof(J9SRP);
	CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeInnerClasses();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeInnerClasses();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

void
ROMClassWriter::writeEnclosedInnerClasses(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_enclosedInnerClassesSRPKey);
	UDATA size = UDATA(_classFileOracle->getEnclosedInnerClassCount()) * sizeof(J9SRP);
	CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeEnclosedInnerClasses();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeEnclosedInnerClasses();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

#if JAVA_SPEC_VERSION >= 11
void
ROMClassWriter::writeNestMembers(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_nestMembersSRPKey);
	UDATA size = UDATA(_classFileOracle->getNestMembersCount()) * sizeof(J9SRP);
	CheckSize _(cursor,size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeNestMembers();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeNestMembers();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}
#endif /* JAVA_SPEC_VERSION >= 11 */

void
ROMClassWriter::writeNameAndSignatureBlock(Cursor *cursor)
{
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0, _interfaceInjectionInfo).writeNameAndSignatureBlock();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0).writeNameAndSignatureBlock();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

void 
ROMClassWriter::writeUTF8s(Cursor *cursor)
{
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0, _interfaceInjectionInfo).writeUTF8Block();
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0).writeUTF8Block();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	/* aligned to U_64 required by the shared classes */
	cursor->padToAlignment(sizeof(U_64), Cursor::GENERIC);
}

void
ROMClassWriter::writeMethods(Cursor *cursor, Cursor *lineNumberCursor, Cursor *variableInfoCursor, bool markAndCountOnly)
{
	/*
	 * ****************************** PLEASE READ ***********************************
	 *
	 * When changing the format of a ROMMethod these are places that assume ROMMethod format
	 * and would need to be updated:
	 *  - nextROMMethod() in util/mthutil.c
	 *  - allSlotsInROMMethodsSectionDo() in util/romclasswalk.c
	 *  - dbgNextROMMethod() in dbgext/j9dbgext.c
	 *  - createBreakpointedMethod() in jvmti/jvmtiHelpers.c
	 * All the above are involved in walking or walking over ROMMethods.
	 *
	 */

	cursor->mark(_methodsSRPKey);
	for (ClassFileOracle::MethodIterator iterator = _classFileOracle->getMethodIterator();
			iterator.isNotDone();
			iterator.next()) {
		U_8 argCount = iterator.getSendSlotCount();
		U_16 tempCount = iterator.getMaxLocals();
		U_32 modifiers = iterator.getModifiers();
		U_32 extendedModifiers = iterator.getExtendedModifiers();
		bool writeDebugInfo =
				((iterator.getLineNumbersCount() > 0) || (iterator.getLocalVariablesCount() > 0)) &&
				(_context->shouldPreserveLineNumbers() || _context->shouldPreserveLocalVariablesInfo());

		/* If an existing method is being compared to, then calling 
		 * romMethodCacheCurrentRomMethod will cache the existing 
		 * rom method in the context. This prevents the need to constantly 
		 * retrieve the method from the existing rom class.
		 */
		_context->romMethodCacheCurrentRomMethod(cursor->getCount());

		if (!markAndCountOnly) {
			if (! iterator.isStatic()) {
				++argCount;
			}
			if (tempCount >= argCount) {
				tempCount -= argCount;
			} else {
				Trc_BCU_Assert_Equals(0, tempCount);
			}

			/*
			 * ROMMethod->modifier   what does each bit represent?
			 *
			 *  Bottom 16 bits are defined by the specification and set by the call to
			 *  iterator.getAccessFlags() the rest are set 'by hand' below.
			 *
			 *  NOTE: some of the bottom 16 bits are used for J9 specific flags as specified by a '*'
			 *
			 * 0000 0000 0000 0000 0000 0000 0000 0000
			 *                                       + AccPublic
			 *                                      + AccPrivate
			 *                                     + AccProtected
			 *                                    + AccStatic
			 *
			 *                                  + AccFinal
			 *                                 + AccSynchronized
			 *                                + AccBridge
			 *                               + AccVarArgs
			 *
			 *                             + AccNative
			 *                            + AccGetterMethod * (Not currently used by specification)
			 *                           + AccAbstract
			 *                          + AccStrict
			 *
			 *                        + AccSynthetic
			 *                       + AccForwarderMethod * (Not currently used by specification)
			 *                      + AccEmptyMethod * (Not currently used by specification)
			 *                     + UNUSED
			 *
			 *    Top 16 bits are defined by J9.
			 *                   + AccMethodVTable
			 *                  + AccMethodHasExceptionInfo
			 *                 + AccMethodHasDebugInfo
			 *                + AccMethodFrameIteratorSkip
			 *
			 *              + AccMethodCallerSensitive (has @CallerSensitive annotation)
			 *             + AccMethodHasBackwardBranches
			 *            + AccMethodObjectConstructor
			 *           + AccMethodHasMethodParameters
			 *
			 *         + AccMethodAllowFinalFieldWrites
			 *        + AccMethodHasGenericSignature
			 *       + AccMethodHasExtendedModifiers
			 *      + AccMethodHasMethodHandleInvokes
			 *
			 *    + AccMethodHasStackMap
			 *   + AccMethodHasMethodAnnotations
			 *  + AccMethodHasParameterAnnotations
			 * + AccMethodHasDefaultAnnotation
			 */

			/* In class files prior to version 53, any method in the declaring class of a final field
			 * may write to it. For 53 and later, only initializers (<init> for instance fields, <clinit>
			 * for static fields) are allowed to write to final fields.
			 */
			if ((_classFileOracle->getMajorVersion() < 53) || ('<' == *_classFileOracle->getUTF8Data(iterator.getNameIndex()))) {
				modifiers |= J9AccMethodAllowFinalFieldWrites;
			}

			if (iterator.hasFrameIteratorSkipAnnotation()) {
				modifiers |= J9AccMethodFrameIteratorSkip;
			}

			if (writeDebugInfo) {
				modifiers |= J9AccMethodHasDebugInfo;
			}

			if (iterator.hasMethodParameters()) {
				modifiers |= J9AccMethodHasMethodParameters;
			}
		}

		U_32 byteCodeSize = 0;
		if (iterator.isAbstract()) {
			/* Do nothing */
		} else if (iterator.isNative()) {
			byteCodeSize = computeNativeSignatureSize(_classFileOracle->getUTF8Data(iterator.getDescriptorIndex()));
		} else {
			byteCodeSize = iterator.getCodeLength();
		}

		if (markAndCountOnly) {
			cursor->skip(sizeof(J9ROMMethod));
		} else {
			CheckSize _(cursor, sizeof(J9ROMMethod));
			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getNameIndex()), Cursor::SRP_TO_UTF8);
			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getDescriptorIndex()), Cursor::SRP_TO_UTF8);
			if (0 != extendedModifiers) {
				modifiers |= J9AccMethodHasExtendedModifiers;
			}
			cursor->writeU32(modifiers, Cursor::ROM_CLASS_MODIFIERS);
			cursor->writeU16(iterator.getMaxStack(), Cursor::GENERIC);
			cursor->writeU16(U_16(byteCodeSize), Cursor::GENERIC);
			cursor->writeU8(U_8(byteCodeSize >> 16), Cursor::GENERIC);
			cursor->writeU8(argCount, Cursor::GENERIC);
			cursor->writeU16(tempCount, Cursor::GENERIC);
		}

		if (iterator.isAbstract()) {
			/* Do nothing */
		} else if (markAndCountOnly) {
			cursor->skip(byteCodeSize);
		} else if (iterator.isNative()) {
			writeNativeSignature(cursor, _classFileOracle->getUTF8Data(iterator.getDescriptorIndex()), byteCodeSize - 2);
		} else {
			UDATA count = cursor->getCount();
			writeByteCodes(cursor, &iterator);
			count = cursor->getCount() - count;
			Trc_BCU_Assert_Equals(count, byteCodeSize);
		}

		cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		if (0 != extendedModifiers) {
			cursor->writeU32(extendedModifiers, Cursor::ROM_CLASS_MODIFIERS);
		}

		if (iterator.hasGenericSignature()) {
			if (markAndCountOnly) { // TODO check if this is a performance win
				cursor->skip(sizeof(J9SRP));
			} else {
				cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getGenericSignatureIndex()), Cursor::SRP_TO_UTF8);
			}
		}

		if ((0 != iterator.getExceptionHandlersCount()) || (0 != iterator.getExceptionsThrownCount())) {
			/* Write exceptions info */
			cursor->writeU16(iterator.getExceptionHandlersCount(), Cursor::GENERIC);
			cursor->writeU16(iterator.getExceptionsThrownCount(), Cursor::GENERIC);
			UDATA size =
					UDATA(iterator.getExceptionHandlersCount()) * sizeof(J9ExceptionHandler) +
					UDATA(iterator.getExceptionsThrownCount()) * sizeof(J9SRP);
			CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeExceptionBlock(&iterator);
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeExceptionBlock(&iterator);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		}

		if (iterator.hasAnnotationsData()) {
			/*
			 * u_32 length
			 * length * u_8
			 * pad to u_32 size
			 */
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->methodAnnotationDo(iterator.getIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		if (iterator.hasParameterAnnotations()) {
			/*
			 * u_32 length
			 * length * u_8
			 * pad to u_32 size
			 */
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->parameterAnnotationsDo(iterator.getIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		if (iterator.hasDefaultAnnotation()) {
			/*
			 * u_32 length
			 * length * u_8
			 * pad to u_32 size
			 */
			AnnotationWriter annotationWriter(cursor, _constantPoolMap,_classFileOracle);
			AnnotationElementWriter annotationElementWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->defaultAnnotationDo(iterator.getIndex(), &annotationWriter, &annotationElementWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		if (iterator.hasMethodTypeAnnotations()) {
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->methodTypeAnnotationDo(iterator.getIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		if (iterator.hasCodeTypeAnnotations()) {
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->methodCodeTypeAnnotationDo(iterator.getIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		bool existingDebugData = _context->romMethodHasDebugData();
		if (writeDebugInfo || existingDebugData) {
			/* writeMethodDebugInfo is called if there is debug data to write, 
			 * or if an existing method with debug data is being compared to.
			 */
			cursor->notifyDebugDataWriteStart();
			if (!_context->shouldWriteDebugDataInline()) {
				cursor->writeSRP(_srpKeyProducer->mapMethodIndexToMethodDebugInfoKey(iterator.getIndex()), Cursor::SRP_TO_DEBUG_DATA);
			}
			writeMethodDebugInfo(&iterator, lineNumberCursor, variableInfoCursor, markAndCountOnly, existingDebugData);
			cursor->notifyDebugDataWriteEnd();
		}

		if ( iterator.hasStackMap() ) {
			/*
			 * Write out Stack Map
			 *
			 *  *** SUN format ***
			 * u2 attribute_name_index
			 * u4 attribute_length
			 * u2 number_of_entries
			 * stack_map_frame entries[number_of_entries]
			 *
			 *  *** J9 format ***
			 * u4 attribute_length  (native endian)
			 * u2 number_of_entries  (big endian)
			 * stack_map_frame entries[number_of_entries]  (big endian)
			 * padding for u4 alignment
			 *
			 */

			UDATA start = cursor->getCount();
			/* output the length of the stack map */
			cursor->writeU32(_methodNotes[iterator.getIndex()].stackMapSize, Cursor::GENERIC);
			/* output the number of frames */
			cursor->writeBigEndianU16(iterator.getStackMapFramesCount(), Cursor::GENERIC); /* TODO: don't write this stuff in BigEndian??? */

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
			Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0, _interfaceInjectionInfo).writeStackMap(&iterator);
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0).writeStackMap(&iterator);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
			if (markAndCountOnly) {
				/* Following is adding PAD to stackmap size. First round is always markAndCountOnly.
				 * This logic is very difficult to catch. Cause of the padded stackmapsize, we dont use padding in nextROMMethod() in mthutil.
				 * Also I find this markAndCountOnly unnecessary and confusing for the following reasons
				 * 1. It is used partially : See above, we dont use it for the first 6 bytes and we write them down.
				 * It should be used properly, either always or never.
				 * 2. Also when it is counting, it is a counting cursor and it actually do not write (see Cursor.hpp).
				 * Therefore, markAndCountOnly flag is not needed at all and extra checks for it just makes it slower.
				 *
				 *  */
				_methodNotes[iterator.getIndex()].stackMapSize = U_32(cursor->getCount() - start);
			}
		}
		
		if (0 != iterator.hasMethodParameters()) {
			U_8 mthParamCount = iterator.getMethodParametersCount();
			UDATA size = UDATA(mthParamCount * sizeof(J9MethodParameter));
			if (true == markAndCountOnly) {
				cursor->skip(sizeof(U_8) + size);
			} else {
				cursor->writeU8(mthParamCount, Cursor::GENERIC);
				CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
				Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeMethodParameters(&iterator);
#else /* J9VM_OPT_VALHALLA_VALUE_TYPES */
				Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeMethodParameters(&iterator);
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
			}
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}
		
	}
}

class ROMClassWriter::ConstantPoolShapeDescriptionWriter : public ConstantPoolMap::ConstantPoolEntryTypeVisitor
{
public:
	ConstantPoolShapeDescriptionWriter(Cursor *cursor) :
		_cursor(cursor),
		_wordValue(0),
		_nibbleIndex(1) /* Account for zeroth entry */
	{
	}

	void visitEntryType(U_32 entryType)
	{
		_wordValue |= entryType << (_nibbleIndex * J9_CP_BITS_PER_DESCRIPTION);
		++_nibbleIndex;
		if (0 == (_nibbleIndex % J9_CP_DESCRIPTIONS_PER_U32)) {
			_cursor->writeU32(_wordValue, Cursor::GENERIC);
			_nibbleIndex = 0;
			_wordValue = 0;
		}
	}

	void flushAndPad()
	{
		/* Flush any remaining nibbles and pad */
		if (0 != (_nibbleIndex % J9_CP_DESCRIPTIONS_PER_U32)) {
			_cursor->writeU32(_wordValue, Cursor::GENERIC);
		}
	}

private:
	Cursor *_cursor;
	U_32 _wordValue;
	U_32 _nibbleIndex;
};

void
ROMClassWriter::writeConstantPoolShapeDescriptions(Cursor *cursor, bool markAndCountOnly)
{
	cursor->mark(_cpDescriptionShapeSRPKey);
	UDATA size = ((UDATA(_constantPoolMap->getROMConstantPoolCount()) + J9_CP_DESCRIPTIONS_PER_U32 - 1) / J9_CP_DESCRIPTIONS_PER_U32) * sizeof(U_32);

	if (markAndCountOnly) {
		cursor->skip(size);
	} else {
		CheckSize _(cursor, size);
		ConstantPoolShapeDescriptionWriter constantPoolShapeDescriptionWriter(cursor);
		_constantPoolMap->constantPoolEntryTypesDo(&constantPoolShapeDescriptionWriter);
		constantPoolShapeDescriptionWriter.flushAndPad();
	}
}

void
ROMClassWriter::writeAnnotationInfo(Cursor *cursor)
{
	if (_classFileOracle->hasClassAnnotations()) {
		AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
		cursor->mark(_annotationInfoClassSRPKey);
		/*
		 * u_32 length
		 * length * u_8
		 * pad to u_32 size
		 */
		_classFileOracle->classAnnotationsDo(&annotationWriter, &annotationWriter, &annotationWriter);
		cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
	}
	if (_classFileOracle->hasTypeAnnotations()) {
		AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
		cursor->mark(_typeAnnotationInfoSRPKey);
		_classFileOracle->classTypeAnnotationsDo(&annotationWriter, &annotationWriter, &annotationWriter);
	}
}
void ROMClassWriter::AnnotationElementWriter::visitNestedAnnotation(U_16 elementNameIndex, ClassFileOracle::NestedAnnotation *nestedAnnotation)
{
	_cursor->writeU8('@', Cursor::GENERIC);

	AnnotationWriter annotationWriter(_cursor, _constantPoolMap, _classFileOracle);
	nestedAnnotation->annotationDo(&annotationWriter, &annotationWriter, NULL);
}

void
ROMClassWriter::writeMethodDebugInfo(ClassFileOracle::MethodIterator *methodIterator,  Cursor *lineNumberCursor, Cursor *variableInfoCursor, bool markAndCountOnly, bool existHasDebugInformation)
{
	/*
	 * Method Debug Information is stored in one of two ways:
	 *  1) inline with the ROMMethod
	 *  2) in two separate areas of the Shared Class Cache
	 *
	 *  When the data is inline with the ROMMethod the lineNumberCursor and variableInfoCursor will be the same Cursor.
	 *
	 *  The J9MethodDebugInfo header structure and the J9LineNumber entries are written to the lineNumberCursor, while
	 *  the J9VariableInfo entries are written to the variableInfoCursor.
	 *
	 *  The data looks like:
	 *
	 *  written to lineNumberCursor:
	 *  J9MethodDebugInfo
	 *   SRP to J9VariableInfo -OR- size of all Method Debug Info include Line Numbers and Variable Info
	 *
	 *	if lineNumberCount fits in 16bit and lineNumbersInfoCompressedSize fits in 15bit
	 *		u32 lineNumberCount << 16 | lineNumbersInfoCompressedSize << 1 | 0
	 *  	u32 variableInfoCount
	 *  else
	 *  	u32 lineNumberCount | 1
	 *  	u32 variableInfoCount
	 *  	u32 lineNumbersInfoCompressedSize
	 *  end if
	 *  U_8 * compressed buffer of pcOffset and lineNumber
	 *
	 *  written to variableInfoCursor:
	 *  Block of compressed data containing the 'index', 'startPC' and length [+ gen sig flag]
	 *  Followed by
	 *    SRP to name
	 *    SRP to signature
	 *    [SRP to generic signature]
	 */
	bool preserveLineNumbers = _context->shouldPreserveLineNumbers();
	bool preserveLocalVariablesInfo = _context->shouldPreserveLocalVariablesInfo();

	if (preserveLineNumbers || preserveLocalVariablesInfo || existHasDebugInformation) {
		U_32 lineNumbersCount = (preserveLineNumbers ? methodIterator->getLineNumbersCount() : 0);
		U_32 localVariablesCount = (preserveLocalVariablesInfo ? methodIterator->getLocalVariablesCount() : 0);
		U_32 lineNumbersInfoCompressedSize = methodIterator->getLineNumbersInfoCompressedSize();
	
		lineNumberCursor->mark(_srpKeyProducer->mapMethodIndexToMethodDebugInfoKey(methodIterator->getIndex()));

		/* Write J9MethodDebugInfo */
		UDATA start = lineNumberCursor->getCount();
		if (!(_context->shouldWriteDebugDataInline())) {
			lineNumberCursor->writeSRP(_srpKeyProducer->mapMethodIndexToVariableInfoKey(methodIterator->getIndex()), Cursor::SRP_TO_LOCAL_VARIABLE_DATA);
		} else {
			if ( !markAndCountOnly && ((lineNumbersCount > 0) || (localVariablesCount > 0))) {
				/* Only check this assert if the class being created was found to have
				 * debug data during the 'mark and count phase'. This should always be the
				 * case if lineNumbersCount or localVariablesCount are non zero.
				 */
				Trc_BCU_Assert_False(_methodNotes[methodIterator->getIndex()].debugInfoSize == 0);
			}
			lineNumberCursor->writeU32(_methodNotes[methodIterator->getIndex()].debugInfoSize | 1, Cursor::METHOD_DEBUG_SIZE);
		}
		
		/* Encode the lineNumbersCount with the lineNumbersInfoCompressedSize
		 * a = number of bytes in the compressed line number table
		 * b = lineNumberCount
		 * c = escape bit (0: use a and b, 1: lineNumberCount uses 31 bit and number of bytes in the compressed line number table will be stored as the next U_32)
		 * aaaaaaaa aaaaaaaa bbbbbbbb bbbbbbbc 
		 * 
		 * Note: If the lineNumberCount is 0, then check the existing rom class in the context for compressed data. 
		 * In the case of the comparing cursor this will ensure the correct number of bytes are advanced.
		 * 
		 * Note 2: The call to getCount relies on getCount() getting an offset into the current method, and 
		 * not out of line debug data. See the implementation of ComparingCursor::getCount() for more info.
		 */
		if (((0 == lineNumbersCount) && (_context->romMethodHasLineNumberCountCompressed()))
				|| ((lineNumbersInfoCompressedSize < 0xFFFF) && (lineNumbersCount < 0x7FFF))){
			/* it fits, c = 0 */
			U_32 lineNumbersCountEncoded = (lineNumbersInfoCompressedSize << 16) | ((lineNumbersCount << 1) & 0xFFFE);
			lineNumberCursor->writeU32(lineNumbersCountEncoded, Cursor::LINE_NUMBER_DATA);
			lineNumberCursor->writeU32(localVariablesCount, Cursor::LOCAL_VARIABLE_COUNT);
		} else {
			/* it does not fit, c = 1 */
			lineNumberCursor->writeU32((lineNumbersCount << 1) | 1, Cursor::LINE_NUMBER_DATA);
			lineNumberCursor->writeU32(localVariablesCount, Cursor::LOCAL_VARIABLE_COUNT);
			lineNumberCursor->writeU32(lineNumbersInfoCompressedSize, Cursor::LINE_NUMBER_DATA);
		}

		lineNumberCursor->writeData(methodIterator->getLineNumbersInfoCompressed(), methodIterator->getLineNumbersInfoCompressedSize(), Cursor::LINE_NUMBER_DATA);

		if (0 != localVariablesCount) {
			U_8 buffer[13];
			UDATA bufferLength;
			variableInfoCursor->mark(_srpKeyProducer->mapMethodIndexToVariableInfoKey(methodIterator->getIndex()));
			U_32 count = 0;
			U_32 lastIndex = 0;
			U_32 lastStartPC = 0;
			U_32 lastLength = 0;
			for (ClassFileOracle::LocalVariablesIterator localVariablesIterator = methodIterator->getLocalVariablesIterator();
					localVariablesIterator.isNotDone();
					localVariablesIterator.next()) {
				I_32 index = localVariablesIterator.getIndex() - lastIndex;
				I_32 startPC = localVariablesIterator.getStartPC() - lastStartPC;

				I_32 length = localVariablesIterator.getLength();
				if (localVariablesIterator.hasGenericSignature()) {
					length |= J9_ROMCLASS_OPTINFO_VARIABLE_TABLE_HAS_GENERIC;
				}
				length = length - lastLength;

				bufferLength = compressLocalVariableTableEntry(index, startPC, length, buffer);

				variableInfoCursor->writeData(buffer, bufferLength, Cursor::LOCAL_VARIABLE_DATA);

				variableInfoCursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(localVariablesIterator.getNameIndex()), Cursor::LOCAL_VARIABLE_DATA_SRP_TO_UTF8);
				variableInfoCursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(localVariablesIterator.getDescriptorIndex()), Cursor::LOCAL_VARIABLE_DATA_SRP_TO_UTF8);
				if (localVariablesIterator.hasGenericSignature()) {
					variableInfoCursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(localVariablesIterator.getGenericSignatureIndex()), Cursor::LOCAL_VARIABLE_DATA_SRP_TO_UTF8);
				}

				lastIndex = localVariablesIterator.getIndex();
				lastStartPC = localVariablesIterator.getStartPC();
				lastLength = localVariablesIterator.getLength();

				++count;
			}
			Trc_BCU_Assert_Equals(count, localVariablesCount);
		}
		
		/* Once done writing the 'variable table' the cursor is notified. This is done to 
		 * enable special cursors like the comparing cursor to update their internal 
		 * information
		 */
		variableInfoCursor->notifyVariableTableWriteEnd();

		/* Both cursors have to be padded, because the low bit is used as a tag, if the
		 * addressed are unaligned, the low bit may not be free */
		if (!(_context->shouldWriteDebugDataInline())) {
			/* When out of line, a 16bit alignment is required for the tag bit */
			lineNumberCursor->padToAlignment(sizeof(U_16), Cursor::LINE_NUMBER_DATA);
			variableInfoCursor->padToAlignment(sizeof(U_16), Cursor::LOCAL_VARIABLE_DATA);
		} else {
			/* When in line, a 32bit alignment is required for the next sections
			 * after the debug info section. Both cursors lineNumberCursor and
			 * variableInfoCursor are identical, only one needs to be padded */
			lineNumberCursor->padToAlignment(sizeof(U_32), Cursor::LINE_NUMBER_DATA);
		}

		if (markAndCountOnly) {
			_methodNotes[methodIterator->getIndex()].debugInfoSize = U_32(lineNumberCursor->getCount() - start);
		}
	}
}

void
ROMClassWriter::writeSourceDebugExtension(Cursor *cursor)
{
	if ((_classFileOracle->hasSourceDebugExtension() && _context->shouldPreserveSourceDebugExtension()) || (_context->romClassHasSourceDebugExtension())) {
		cursor->mark(_sourceDebugExtensionSRPKey);
		cursor->writeU32(_classFileOracle->getSourceDebugExtensionLength(), Cursor::SOURCE_DEBUG_EXT_LENGTH);
		cursor->writeData(_classFileOracle->getSourceDebugExtensionData(), _classFileOracle->getSourceDebugExtensionLength(), Cursor::SOURCE_DEBUG_EXT_DATA);
		cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
	}
}

/* 
 * Records in the ROM class has the following layout:
 * 4 bytes for the number of record components in the class
 * for each record component:
 * 	J9ROMRecordComponentShape
 * 	4 bytes SRP to record component's generic signature if the component has a signature annotation.
 * 	4 bytes length of annotation data in bytes, followed by annotation data. Omitted if there are no annotations.
 * 	4 bytes length of type annotation data in bytes followed by type annotation data.  Omitted if there are no type annotations.
 * 
 * For example, if a record component has a generic signature annotation and no other annotations the record components shape will look like:
 *  J9ROMRecordComponentShape
 *  4 bytes SRP
 */
void
ROMClassWriter::writeRecordComponents(Cursor *cursor, bool markAndCountOnly)
{
	if (! _classFileOracle->isRecord()) {
		return;
	}

	cursor->mark(_recordInfoSRPKey);

	/* number of record components */
	if (markAndCountOnly) {
		cursor->skip(sizeof(U_32));
	} else {
		cursor->writeU32(_classFileOracle->getRecordComponentCount(), Cursor::GENERIC);
	}

	ClassFileOracle::RecordComponentIterator iterator = _classFileOracle->getRecordComponentIterator();
	while ( iterator.isNotDone() ) {
		if (markAndCountOnly) {
			cursor->skip(sizeof(J9ROMRecordComponentShape));
		} else {
			CheckSize _(cursor, sizeof(J9ROMRecordComponentShape));

			/* record component name and descriptor */
			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getNameIndex()), Cursor::SRP_TO_UTF8);
			cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getDescriptorIndex()), Cursor::SRP_TO_UTF8);

			/* attribute flags */
			U_32 attributeFlags = 0;
			if (iterator.hasGenericSignature()) {
				attributeFlags |= J9RecordComponentFlagHasGenericSignature;
			}
			if (iterator.hasAnnotation()) {
				attributeFlags |= J9RecordComponentFlagHasAnnotations;
			}
			if (iterator.hasTypeAnnotation()) {
				attributeFlags |= J9RecordComponentFlagHasTypeAnnotations;
			}
			cursor->writeU32(attributeFlags, Cursor::GENERIC);
		}

		/* write optional attributes */
		if (iterator.hasGenericSignature()) {
			if (markAndCountOnly) {
				cursor->skip(sizeof(J9SRP));
			} else {
				cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(iterator.getGenericSignatureIndex()), Cursor::SRP_TO_UTF8);
			}
		}
		if (iterator.hasAnnotation()) {
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->recordComponentAnnotationDo(iterator.getRecordComponentIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}
		if (iterator.hasTypeAnnotation()) {
			AnnotationWriter annotationWriter(cursor, _constantPoolMap, _classFileOracle);
			_classFileOracle->recordComponentTypeAnnotationDo(iterator.getRecordComponentIndex(), &annotationWriter, &annotationWriter, &annotationWriter);
			cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);
		}

		iterator.next();
	}
}

/*
 * PermittedSubclasses ROM class layout:
 * 4 bytes for number of classes (actually takes up two, but use 4 for alignment)
 * for number of classes:
 *   4 byte SRP to class name
 */
void
ROMClassWriter::writePermittedSubclasses(Cursor *cursor, bool markAndCountOnly)
{
	if (_classFileOracle->isSealed()) {
		cursor->mark(_permittedSubclassesInfoSRPKey);

		U_16 classCount = _classFileOracle->getPermittedSubclassesClassCount();
		if (markAndCountOnly) {
			cursor->skip(sizeof(U_32));
		} else {
			cursor->writeU32(classCount, Cursor::GENERIC);
		}

		for (U_16 index = 0; index < classCount; index++) {
			if (markAndCountOnly) {
				cursor->skip(sizeof(J9SRP));
			} else {
				U_16 classNameCpIndex = _classFileOracle->getPermittedSubclassesClassNameAtIndex(index);
				cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(classNameCpIndex), Cursor::SRP_TO_UTF8);
			}
		}
	}
}

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
void
ROMClassWriter::writeInjectedInterfaces(Cursor *cursor, bool markAndCountOnly)
{
	if (_interfaceInjectionInfo->numOfInterfaces > 0) {
		cursor->mark(_injectedInterfaceInfoSRPKey);

		if (markAndCountOnly) {
			cursor->skip(sizeof(U_32));
		} else {
			cursor->writeU32(_interfaceInjectionInfo->numOfInterfaces, Cursor::GENERIC);
		}
	}
}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
void
ROMClassWriter::writeOptionalInfo(Cursor *cursor)
{
	// TODO check if we care about alignment of optional info
	cursor->padToAlignment(sizeof(U_32), Cursor::GENERIC);

	/*
	 * TODO The following is written in storeOptionalData, but likely needs to be a part of something else
	 *
	 * J9EnclosingObject:
	 *    U_32  classRefCPIndex
	 *    SRP  nameAndSignature
	 */
	if (_classFileOracle->hasEnclosingMethod()) {
		cursor->mark(_enclosingMethodSRPKey);
		cursor->writeU32(_constantPoolMap->getROMClassCPIndexForReference(_classFileOracle->getEnclosingMethodClassRefIndex()), Cursor::GENERIC);
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getEnclosingMethodNameAndSignatureIndex()), Cursor::SRP_TO_NAME_AND_SIGNATURE);
	}

	/*
	 * OptionalInfo is made up of the following information:
	 *
	 * All entries are optional, present only if the appropriate data is available.
	 *
	 * SRP to source file name
	 * SRP to generic signature
	 * SRP to source debug extensions
	 * SRP to annotation info
	 * SRP to Method Debug Info
	 * SRP to enclosing method
	 * SRP to simple name
	 * SRP to 'SRP' (self) if OPTINFO_VERIFY_EXCLUDE.. pretty much a flag, leaving an empty slot.
	 * SRP to class annotations
	 * SRP to class Type Annotations
	 * SRP to record class component attributes
	 * SRP to PermittedSubclasses attribute
	 * SRP to injected interfaces info
	 */
	cursor->mark(_optionalInfoSRPKey);

	if ((_classFileOracle->hasSourceFile() && _context->shouldPreserveSourceFileName()) 
		|| (_context->romClassHasSourceFileName())) {
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getSourceFileIndex()), Cursor::OPTINFO_SOURCE_FILE_NAME);
	}

	if (_classFileOracle->hasGenericSignature()) {
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getGenericSignatureIndex()), Cursor::SRP_TO_UTF8);
	}

	if ((_classFileOracle->hasSourceDebugExtension() && (_context->shouldPreserveSourceDebugExtension()))
		|| (_context->romClassHasSourceDebugExtension())) {
		cursor->writeSRP(_sourceDebugExtensionSRPKey, Cursor::SRP_TO_SOURCE_DEBUG_EXT);
	}

	if (_classFileOracle->hasEnclosingMethod()) {
		cursor->writeSRP(_enclosingMethodSRPKey, Cursor::SRP_TO_GENERIC);
	}

	if (_classFileOracle->hasSimpleName()) {
		cursor->writeSRP(_srpKeyProducer->mapCfrConstantPoolIndexToKey(_classFileOracle->getSimpleNameIndex()), Cursor::SRP_TO_UTF8);
	}

	if (_classFileOracle->hasVerifyExcludeAttribute()) {
		cursor->writeU32(0, Cursor::GENERIC); /* This is required for getSRPPtr in util/optinfo.c to work (counts bits in romClass->optionalFlags) */
	}

	if (_classFileOracle->hasClassAnnotations()) {
		cursor->writeSRP(_annotationInfoClassSRPKey, Cursor::SRP_TO_GENERIC);
	}
	if (_classFileOracle->hasTypeAnnotations()) {
		cursor->writeSRP(_typeAnnotationInfoSRPKey, Cursor::SRP_TO_GENERIC);
	}
	if (_classFileOracle->isRecord()) {
		cursor->writeSRP(_recordInfoSRPKey, Cursor::SRP_TO_GENERIC);
	}
	if (_classFileOracle->isSealed()) {
		cursor->writeSRP(_permittedSubclassesInfoSRPKey, Cursor::SRP_TO_GENERIC);
	}
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	if (_interfaceInjectionInfo->numOfInterfaces > 0) {
		cursor->writeSRP(_injectedInterfaceInfoSRPKey, Cursor::SRP_TO_GENERIC);
	}
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
}

void
ROMClassWriter::writeCallSiteData(Cursor *cursor, bool markAndCountOnly)
{
	if (_constantPoolMap->hasCallSites() || _classFileOracle->hasBootstrapMethods()) {
		cursor->mark(_callSiteDataSRPKey);
	}
	if (_constantPoolMap->hasCallSites()) {
		UDATA size = UDATA(_constantPoolMap->getCallSiteCount()) * (sizeof(J9SRP) + sizeof(U_16));
		CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeCallSiteData();
#else  /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeCallSiteData();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}
	if (_classFileOracle->hasBootstrapMethods()) {
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0, _interfaceInjectionInfo).writeBootstrapMethods();
#else  /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		Helper(cursor, false, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, 0).writeBootstrapMethods();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}
}

void
ROMClassWriter::writeVarHandleMethodTypeLookupTable(Cursor *cursor, bool markAndCountOnly)
{
	if (_constantPoolMap->hasVarHandleMethodRefs()) {
		cursor->mark(_varHandleMethodTypeLookupTableSRPKey);
		UDATA size = _constantPoolMap->getVarHandleMethodTypePaddedCount() * sizeof(U_16);
		CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeVarHandleMethodTypeLookupTable();
#else  /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeVarHandleMethodTypeLookupTable();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}
}

void
ROMClassWriter::writeStaticSplitTable(Cursor *cursor, bool markAndCountOnly)
{
	if (_constantPoolMap->hasStaticSplitTable()) {
		cursor->mark(_staticSplitTableSRPKey);
		UDATA size = UDATA(_constantPoolMap->getStaticSplitEntryCount()) * sizeof(U_16);
		CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeStaticSplitTable();
#else  /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeStaticSplitTable();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}
}

void
ROMClassWriter::writeSpecialSplitTable(Cursor *cursor, bool markAndCountOnly)
{
	if (_constantPoolMap->hasSpecialSplitTable()) {
		cursor->mark(_specialSplitTableSRPKey);
		UDATA size = UDATA(_constantPoolMap->getSpecialSplitEntryCount()) * sizeof(U_16);
		CheckSize _(cursor, size);
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size, _interfaceInjectionInfo).writeSpecialSplitTable();
#else  /* J9VM_OPT_VALHALLA_VALUE_TYPES */
		Helper(cursor, markAndCountOnly, _classFileOracle, _srpKeyProducer, _srpOffsetTable, _constantPoolMap, size).writeSpecialSplitTable();
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
	}
}

void
ROMClassWriter::writeByteCodes(Cursor* cursor, ClassFileOracle::MethodIterator *methodIterator)
{
	U_8 *code = methodIterator->getCode();

	if (!methodIterator->isByteCodeFixupDone()) {
		ClassFileOracle::BytecodeFixupEntry *fixupTable = methodIterator->getByteCodeFixupTable();
		ClassFileOracle::BytecodeFixupEntry *end = fixupTable + methodIterator->getByteCodeFixupCount();

		for (ClassFileOracle::BytecodeFixupEntry *entry = fixupTable; entry != end; ++entry) {
			U_16 *dest = (U_16 *) &code[entry->codeIndex];

			switch(entry->type) {
			case ConstantPoolMap::LDC:
				code[entry->codeIndex] = U_8(_constantPoolMap->getROMClassCPIndex(entry->cpIndex));
				break;

			case ConstantPoolMap::INVOKE_DYNAMIC:
				/* rewrite the invokedynamic index to be the callSiteIndex */
				*dest = _constantPoolMap->getCallSiteIndex(entry->cpIndex);
				break;

			case ConstantPoolMap::INVOKE_STATIC:
				if (_constantPoolMap->isStaticSplit(entry->cpIndex)) {
					code[entry->codeIndex - 1] = JBinvokestaticsplit;
					*dest = _constantPoolMap->getStaticSplitTableIndex(entry->cpIndex);
				} else {
					*dest = _constantPoolMap->getROMClassCPIndex(entry->cpIndex, entry->type);
				}
				break;

			case ConstantPoolMap::INVOKE_SPECIAL:
				if (_constantPoolMap->isSpecialSplit(entry->cpIndex)) {
					code[entry->codeIndex - 1] = JBinvokespecialsplit;
					*dest = _constantPoolMap->getSpecialSplitTableIndex(entry->cpIndex);
				} else {
					*dest = _constantPoolMap->getROMClassCPIndex(entry->cpIndex, entry->type);
				}
				break;

			default:
				*dest = _constantPoolMap->getROMClassCPIndex(entry->cpIndex, entry->type);
				break;
			}
		}
		methodIterator->setByteCodeFixupDone();
	}

	cursor->writeData(code, methodIterator->getCodeLength(), Cursor::BYTECODE);
}

U_32
ROMClassWriter::computeNativeSignatureSize(U_8 *methodDescriptor)
{
	Cursor countingCursor(0, NULL, _context);
	writeNativeSignature(&countingCursor, methodDescriptor, 0);
	return U_32(countingCursor.getCount());
}

void
ROMClassWriter::writeNativeSignature(Cursor *cursor, U_8 *methodDescriptor, U_8 nativeArgCount)
{
	/* mapping characters A..Z */
	static const U_8 nativeArgCharConversion[] = {
			0,			PARAM_BYTE,		PARAM_CHAR,		PARAM_DOUBLE,
			0,			PARAM_FLOAT,	0,				0,
			PARAM_INT,	PARAM_LONG,		0,				PARAM_OBJECT,
			0,			0,				0,				0,
			0,			0,				PARAM_SHORT,	0,
			0,			PARAM_VOID,		0,				0,
			0,			PARAM_BOOLEAN};

	UDATA index = 1;

	cursor->writeU8(nativeArgCount, Cursor::GENERIC);

	/* Parse and write return type */
	while (')' != methodDescriptor[index]) {
		++index;
	}
	++index;
	if ('[' == methodDescriptor[index]) {
		cursor->writeU8(PARAM_OBJECT, Cursor::GENERIC);
	} else {
		cursor->writeU8(nativeArgCharConversion[methodDescriptor[index] - 'A'], Cursor::GENERIC);
	}

	index = 1;

	/* Parse the signature inside the ()'s */
	while (')' != methodDescriptor[index]) {
		if ('[' == methodDescriptor[index]) {
			cursor->writeU8(PARAM_OBJECT, Cursor::GENERIC);
			while ('[' == methodDescriptor[index]) {
				++index;
			}
		} else {
			cursor->writeU8(nativeArgCharConversion[methodDescriptor[index] - 'A'], Cursor::GENERIC);
		}
		if ('L' == methodDescriptor[index]) {
			while (';' != methodDescriptor[index]) {
				++index;
			}
		}
		++index;
	}
}

/*******************************************************************************
 * Copyright (c) 2000, 2021 IBM Corp. and others
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

#ifndef J9_SYMBOLREFERENCETABLE_INCL
#define J9_SYMBOLREFERENCETABLE_INCL

/*
 * The following #define and typedef must appear before any #includes in this file
 */
#ifndef J9_SYMBOLREFERENCETABLE_CONNECTOR
#define J9_SYMBOLREFERENCETABLE_CONNECTOR
namespace J9 { class SymbolReferenceTable; }
namespace J9 { typedef J9::SymbolReferenceTable SymbolReferenceTableConnector; }
#endif

#include "compile/OMRSymbolReferenceTable.hpp"

#include <stddef.h>
#include <stdint.h>
#include <map>
#include "env/jittypes.h"

class TR_BitVector;
class TR_PersistentClassInfo;
namespace TR { class Compilation; }


class TR_ImmutableInfo
   {
   public:
   TR_ALLOC(TR_Memory::SymbolReferenceTable)

   TR_ImmutableInfo(TR_OpaqueClassBlock *clazz, TR_BitVector *immutableSymRefs, TR_BitVector *immutableConstructorDefAliases)
       : _clazz(clazz), _immutableSymRefNumbers(immutableSymRefs), _immutableConstructorDefAliases(immutableConstructorDefAliases)
      {
      }

   TR_OpaqueClassBlock *_clazz;
   TR_BitVector        *_immutableSymRefNumbers;
   TR_BitVector        *_immutableConstructorDefAliases;
   };



namespace J9
{

class SymbolReferenceTable : public OMR::SymbolReferenceTableConnector
   {
   public:

   SymbolReferenceTable(size_t size, TR::Compilation *comp);

   TR::SymbolReference * findOrCreateCountForRecompileSymbolRef();
   TR::SymbolReference * findDiscontiguousArraySizeSymbolRef() { return element(discontiguousArraySizeSymbol); }
   TR::SymbolReference * findOrCreateOSRBufferSymbolRef();
   TR::SymbolReference * findOrCreateOSRScratchBufferSymbolRef();
   TR::SymbolReference * findOrCreateOSRFrameIndexSymbolRef();

   /** \brief
    * Find or create VMThread tempSlot symbol reference. J9VMThread.tempSlot provides a mechanism for the
    * compiler to provide information that the VM can use for various reasons - such as locating items on
    * the stack during a call to internal native methods that are signature-polymorphic.
    *
    * \return TR::SymbolReference* the VMThreadTempSlotField symbol reference
    */
   TR::SymbolReference * findOrCreateVMThreadTempSlotFieldSymbolRef();

   /** \brief
    * Find or create VMThread.floatTemp1 symbol reference. J9VMThread.floatTemp1 provides an additional
    * mechanism for the compiler to provide information that the VM can use for various reasons
    *
    * \return TR::SymbolReference* the VMThread.floatTemp1 symbol reference
    */
   TR::SymbolReference * findOrCreateVMThreadFloatTemp1SymbolRef();

   // CG linkage
   TR::SymbolReference * findOrCreateAcquireVMAccessSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol); // minor rework
   TR::SymbolReference * findOrCreateReleaseVMAccessSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol); // minor rework
   TR::SymbolReference * findOrCreateStackOverflowSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol); // minor rework
   TR::SymbolReference * findOrCreateThrowCurrentExceptionSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0); // minor rework
   TR::SymbolReference * findOrCreateThrowUnreportedExceptionSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0);

   TR::SymbolReference * findOrCreateWriteBarrierStoreSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0);
   TR::SymbolReference * findOrCreateWriteBarrierStoreGenerationalSymbolRef(TR::ResolvedMethodSymbol *owningMethodSymbol = 0);
   TR::SymbolReference * findOrCreateConstantPoolAddressSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);

   // FE
   TR::SymbolReference * findOrCreateFloatSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateDoubleSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * createSystemRuntimeHelper(TR_RuntimeHelper, bool = false, bool = false, bool preservesAllRegisters = false);
   TR::SymbolReference * findOrCreateStaticMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateSpecialMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateVirtualMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateInterfaceMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateDynamicMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t callSiteIndex, bool * unresolvedInCP = 0);
   TR::SymbolReference * findOrCreateHandleMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex, bool * unresolvedInCP = 0);
   TR::SymbolReference * findOrCreateHandleMethodSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex, char *signature);
   TR::SymbolReference * findOrCreateCallSiteTableEntrySymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t callSiteIndex);
   TR::SymbolReference * findOrCreateMethodTypeTableEntrySymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateVarHandleMethodTypeTableEntrySymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * methodSymRefWithSignature(TR::SymbolReference *original, char *effectiveSignature, int32_t effectiveSignatureLength);
   TR::SymbolReference * findOrCreateTypeCheckArrayStoreSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateArrayClassRomPtrSymbolRef();
   TR::SymbolReference * findOrCreateJavaLangReferenceReferentShadowSymbol(TR::ResolvedMethodSymbol *, bool , TR::DataType, uint32_t, bool);
   TR::SymbolReference * findOrCreateWriteBarrierStoreGenerationalAndConcurrentMarkSymbolRef(TR::ResolvedMethodSymbol *owningMethodSymbol = 0);
   TR::SymbolReference * findOrCreateWriteBarrierStoreRealTimeGCSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0);
   TR::SymbolReference * findOrCreateWriteBarrierClassStoreRealTimeGCSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0);
   TR::SymbolReference * findOrCreateWriteBarrierBatchStoreSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = 0);

   TR::SymbolReference * findOrCreateAcmpHelperSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);

   // these helpers are guaranteed to never throw if the receiving object is not null,
   // so we explicit generate NULLCHKs and assume the helpers will never throw
   TR::SymbolReference * findOrCreateGetFlattenableFieldSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);
   TR::SymbolReference * findOrCreateWithFlattenableFieldSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);
   TR::SymbolReference * findOrCreatePutFlattenableFieldSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);
   TR::SymbolReference * findOrCreateGetFlattenableStaticFieldSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);
   TR::SymbolReference * findOrCreatePutFlattenableStaticFieldSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);

   // these helpers may throw exception such as ArrayIndexOutOfBoundsException or ArrayStoreException etc.
   TR::SymbolReference * findOrCreateLoadFlattenableArrayElementSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);
   TR::SymbolReference * findOrCreateStoreFlattenableArrayElementSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol = NULL);

   TR::SymbolReference * findOrCreateShadowSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex, bool isStore);
   TR::SymbolReference * findOrFabricateShadowSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, TR::Symbol::RecognizedField recognizedField, TR::DataType type, uint32_t offset, bool isVolatile, bool isPrivate, bool isFinal, char* name = NULL);

   /** \brief
    *     Returns a symbol reference for an entity not present in the constant pool.
    *
    *     For resolved symrefs, if an appropriate one is found, it will be returned
    *     even if it originated from a call to findOrCreateShadowSymbol.
    *
    *  \param containingClass
    *     The class that contains the field.
    *  \param type
    *     The data type of the field.
    *  \param offset
    *     The offset of the field.
    *  \param isVolatile
    *     Specifies whether the field is volatile.
    *  \param isPrivate
    *     Specifies whether the field is private.
    *  \param isFinal
    *     Specifies whether the field is final.
    *  \param name
    *     The name of the field.
    *  \param signature
    *     The signature of the field.
    *  \return
    *     Returns a symbol reference fabricated for the field.
    */
   TR::SymbolReference * findOrFabricateShadowSymbol(TR_OpaqueClassBlock *containingClass, TR::DataType type, uint32_t offset, bool isVolatile, bool isPrivate, bool isFinal,  const char *name, const char *signature);

   TR::SymbolReference * findOrCreateObjectNewInstanceImplSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateDLTBlockSymbolRef();
   TR::SymbolReference * findDLTBlockSymbolRef();
   TR::SymbolReference * findOrCreateMultiANewArraySymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateInstanceShapeSymbolRef();
   TR::SymbolReference * findOrCreateInstanceDescriptionSymbolRef();
   TR::SymbolReference * findOrCreateDescriptionWordFromPtrSymbolRef();
   TR::SymbolReference * findOrCreateClassFlagsSymbolRef();
   TR::SymbolReference * findOrCreateClassAndDepthFlagsSymbolRef();
   TR::SymbolReference * findOrCreateArrayComponentTypeAsPrimitiveSymbolRef();
   TR::SymbolReference * findOrCreateMethodTypeCheckSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateIncompatibleReceiverSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateIncompatibleClassChangeErrorSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateReportStaticMethodEnterSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateReportMethodExitSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateHeaderFlagsSymbolRef();
   TR::SymbolReference * findOrCreateDiscontiguousArraySizeSymbolRef();
   TR::SymbolReference * findOrCreateClassLoaderSymbolRef(TR_ResolvedMethod *);
   TR::SymbolReference * findOrCreateCurrentThreadSymbolRef();
   TR::SymbolReference * findOrCreateJ9MethodExtraFieldSymbolRef(intptr_t offset);
   TR::SymbolReference * findOrCreateJ9MethodConstantPoolFieldSymbolRef(intptr_t offset);
   TR::SymbolReference * findOrCreateStartPCLinkageInfoSymbolRef(intptr_t offset);
   TR::SymbolReference * findOrCreatePerCodeCacheHelperSymbolRef(TR_CCPreLoadedCode helper, uintptr_t helperAddr);
   TR::SymbolReference * findOrCreateANewArraySymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateStringSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   /** \brief
    *     Finds or creates a constant dynamic static symbol reference.
    *
    *  \param owningMethodSymbol
    *     The owning resolved method symbol.
    *
    *  \param cpIndex
    *     The constant pool index of the constant dynamic.
    *
    *  \param symbolTypeSig
    *     The underlying class type signature string of the constant dynamic. For primitive this is the signature of the corresponding autobox class.
    *
    *  \param symbolTypeSigLength
    *     Length of the underlying class type signature string.
    *
    *  \param isCondyPrimitive
    *     Determines whether the constant dynamic is of primitive type.
    *
    *  \return
    *     The static symbol reference of the constant dynamic.
    */
   TR::SymbolReference * findOrCreateConstantDynamicSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex, char* symbolTypeSig, int32_t symbolTypeSigLength, bool isCondyPrimitive);
   TR::SymbolReference * findContiguousArraySizeSymbolRef() { return element(contiguousArraySizeSymbol); }
   TR::SymbolReference * findOrCreateMethodMonitorEntrySymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateMethodMonitorExitSymbolRef(TR::ResolvedMethodSymbol * owningMethodSymbol);
   TR::SymbolReference * findOrCreateCompiledMethodSymbolRef();
   TR::SymbolReference * findOrCreateComputedStaticCallSymbol();
   TR::SymbolReference * findOrCreateMethodTypeSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateMethodHandleSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateClassStaticsSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex);
   TR::SymbolReference * findOrCreateStaticSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t cpIndex, bool isStore);

   TR::SymbolReference * findOrCreateClassIsArraySymbolRef();

   TR::SymbolReference * findArrayClassRomPtrSymbolRef();
   TR::SymbolReference * findClassRomPtrSymbolRef();

   TR::SymbolReference * findClassFromJavaLangClassAsPrimitiveSymbolRef();
   TR::SymbolReference * findOrCreateClassFromJavaLangClassAsPrimitiveSymbolRef();

   TR::SymbolReference * findOrCreateJavaLangClassFromClassSymbolRef();
   TR::SymbolReference * findOrCreateClassFromJavaLangClassSymbolRef();
   TR::SymbolReference * findOrCreateInitializeStatusFromClassSymbolRef();
   TR::SymbolReference * findInitializeStatusFromClassSymbolRef();
   TR::SymbolReference * findOrCreateClassRomPtrSymbolRef();
   TR::SymbolReference * findOrCreateArrayComponentTypeSymbolRef();

   TR::SymbolReference * findOrCreateProfilingBufferCursorSymbolRef();
   TR::SymbolReference * findOrCreateProfilingBufferEndSymbolRef();
   TR::SymbolReference * findOrCreateProfilingBufferSymbolRef(intptr_t offset = 0);

   // optimizer
   TR::SymbolReference * findOrCreateRamStaticsFromClassSymbolRef();
   TR::SymbolReference * findOrCreateGlobalFragmentSymbolRef();
   TR::SymbolReference * findOrCreateThreadDebugEventData(int32_t index);

   // optimizer (loop versioner)
   TR::SymbolReference * findOrCreateThreadLowTenureAddressSymbolRef();
   TR::SymbolReference * findOrCreateThreadHighTenureAddressSymbolRef();

   TR::SymbolReference * findOrCreateFragmentParentSymbolRef();

   /** \brief
    *     Finds an unsafe symbol reference with given constraints if it exists.
    *
    *  \param type
    *     The type of the unsafe symbol.
    *
    *  \param javaObjectReference
    *     Determines whether this symbol reference is referencing a Java object field.
    *
    *  \param javaStaticReference
    *     Determines whether this symbol reference is referencing a Java object static field.
    *
    *  \param isVolatile
    *     Determines whether this symbol should be treated with volatile semantics.
    *
    *  \return
    *     The unsafe symbol reference with given constraints if it exists; <c>NULL</c> otherwise.
    */
   TR::SymbolReference* findUnsafeSymbolRef(TR::DataType type, bool javaObjectReference = false, bool javaStaticReference = false, bool isVolatile = false);

   /** \brief
    *     Finds an unsafe symbol reference with given constraints if it exists, or creates one if no such symbol
    *     reference already exists.
    *
    *  \param type
    *     The type of the unsafe symbol.
    *
    *  \param javaObjectReference
    *     Determines whether this symbol reference is referencing a Java object field.
    *
    *  \param javaStaticReference
    *     Determines whether this symbol reference is referencing a Java object static field.
    *
    *  \param isVolatile
    *     Determines whether this symbol should be treated with volatile semantics.
    *
    *  \return
    *     The unsafe symbol reference with given constraints if it exists.
    */
   TR::SymbolReference* findOrCreateUnsafeSymbolRef(TR::DataType type, bool javaObjectReference = false, bool javaStaticReference = false, bool isVolatile = false);

   TR::SymbolReference * findOrCreateImmutableGenericIntShadowSymbolReference(intptr_t offset); // "Immutable" means no aliasing issues; ie. reads from these shadows can be freely reordered wrt anything else

   TR::SymbolReference * findOrCreateCheckCastForArrayStoreSymbolRef(TR::ResolvedMethodSymbol *owningMethodSymbol);

   /** \brief
    *     Finds the <objectEqualityComparison> "nonhelper" symbol reference,
    *     creating it if necessary.
    *
    *  \return
    *     The <objectEqualityComparison> symbol reference.
    */
   TR::SymbolReference *findOrCreateObjectEqualityComparisonSymbolRef();

   /**
    * \brief
    *    Creates a new symbol for a parameter within the supplied owning method of the
    *    specified type and slot index.
    *
    * \param owningMethodSymbol
    *    Resolved method symbol for which this parameter is created.
    *
    * \param slot
    *    Slot index for this parameter.
    *
    * \param type
    *    TR::DataType of the parameter.
    *
    * \param knownObjectIndex
    *    known object index is needed if the parameter is a known object, like the receiver of a customer thunk
    *
    * \return
    *    The created TR::ParameterSymbol
    */
   TR::ParameterSymbol * createParameterSymbol(TR::ResolvedMethodSymbol * owningMethodSymbol, int32_t slot, TR::DataType type, TR::KnownObjectTable::Index knownObjectIndex = TR::KnownObjectTable::UNKNOWN);

   void initShadowSymbol(TR_ResolvedMethod *, TR::SymbolReference *, bool, TR::DataType, uint32_t, bool);

   List<TR::SymbolReference> *dynamicMethodSymrefsByCallSiteIndex(int32_t index);
   bool isFieldClassObject(TR::SymbolReference *symRef);
   bool isFieldTypeBool(TR::SymbolReference *symRef);
   bool isStaticTypeBool(TR::SymbolReference *symRef);
   bool isReturnTypeBool(TR::SymbolReference *symRef);

   /*
    * Creates symbol references for parameters in @param owningMethodSymbol
    * and add them to the current symbol reference table which is usually different
    * from the global symbol reference table. This API is used for peeking and
    * optimizations where a local symbol reference table is needed.
    */
   void addParameters(TR::ResolvedMethodSymbol * owningMethodSymbol);

   // NO LONGER NEEDED?  Disabled since inception (2009)
   void checkUserField(TR::SymbolReference *);

   // Immutability
   void checkImmutable(TR::SymbolReference *);
   TR_ImmutableInfo *findOrCreateImmutableInfo(TR_OpaqueClassBlock *);
   bool isImmutable(TR::SymbolReference *);
   int immutableConstructorId(TR::MethodSymbol *);
   bool hasImmutable() { return _hasImmutable; }
   void setHasImmutable(bool b) { _hasImmutable = b; }
   List<TR_ImmutableInfo> &immutableInfo() { return _immutableInfo; }
   TR_Array<TR_BitVector *> &immutableSymRefNumbers() { return _immutableSymRefNumbers; }

   int userFieldMethodId(TR::MethodSymbol *);
   bool hasUserField() { return _hasUserField; }
   void setHasUserField(bool b) { _hasUserField = b; }

   void performClassLookahead(TR_PersistentClassInfo *, TR_ResolvedMethod *);


   TR::SymbolReference * findShadowSymbol(TR_ResolvedMethod * owningMethod, int32_t cpIndex, TR::DataType, TR::Symbol::RecognizedField *recognizedField = NULL);

   /**
    * @brief Retrieve the textual name of the given NonHelperSymbol
    *
    * @param[in] nonHelper : the nonHelper symbol
    *
    * @return Textual name of the NonHelperSymbol
    */
   static const char *getNonHelperSymbolName(CommonNonhelperSymbol nonHelper);

   protected:

   TR::Symbol                           *_currentThreadDebugEventDataSymbol;
   List<TR::SymbolReference>            _currentThreadDebugEventDataSymbolRefs;
   List<TR::SymbolReference>            _constantPoolAddressSymbolRefs;

   private:

   struct ResolvedFieldShadowKey
      {
      ResolvedFieldShadowKey(
         TR_OpaqueClassBlock *containingClass,
         uint32_t offset,
         TR::DataType type)
         : _containingClass(containingClass)
         , _offset(offset)
         , _type(type)
         {}

      TR_OpaqueClassBlock * const _containingClass;
      const uint32_t _offset;
      const TR::DataType _type;

      bool operator<(const ResolvedFieldShadowKey &rhs) const
         {
         const ResolvedFieldShadowKey &lhs = *this;
         std::less<void*> ptrLt;

         if (lhs._containingClass != rhs._containingClass)
            return ptrLt(lhs._containingClass, rhs._containingClass);
         else if (lhs._offset != rhs._offset)
            return lhs._offset < rhs._offset;
         else
            return lhs._type.getDataType() < rhs._type.getDataType();
         }
      };

   typedef std::pair<const ResolvedFieldShadowKey, TR::SymbolReference*> ResolvedFieldShadowsEntry;
   typedef TR::typed_allocator<ResolvedFieldShadowsEntry, TR::Allocator> ResolvedFieldShadowsAlloc;
   typedef std::map<ResolvedFieldShadowKey, TR::SymbolReference*, std::less<ResolvedFieldShadowKey>, ResolvedFieldShadowsAlloc> ResolvedFieldShadows;

   /**
    * \brief Find if an existing resolved shadow exists matching the given key.
    *
    * The \p key is used to lookup the shadow symref. If a matching symref is found,
    * the properties specified by the other boolean parameters are checked to make
    * sure the properties of the symbol either match or have a more conservatively
    * correct value. In other words, the properties need not match if the values
    * set on the symbol would still be functionally correct. If they are not, an
    * assert is fired.
    *
    * \param key is the key used to search for the symref
    * \param isVolatile specifies whether the symbol found must be volatile.
    *    Expecting a non-volatile symbol but finding a volatile is functionally correct.
    *    However, expectinga a volatile symbol and finding a non-volatile one is incorrect,
    *    so an assert is fired.
    * \param isPrivate specifies whether the symbol found must be private.
    *    Expecting a private symbol but finding a non-private one is functionally correct.
    *    However, expecting a non-private symbol and finding a private is incorrect,
    *    so an assert is fired.
    * \param isFinal specifies whether the symbol found must be final.
    *    Expecting a final symbol but finding a non-final one is functionally correct.
    *    However, expecting a non-final field and finding a final one is incorrect,
    *    so an assert is fired.
    * \return TR::SymbolReference* the shadow symref if found, NULL otherwise
    */
   TR::SymbolReference * findResolvedFieldShadow(ResolvedFieldShadowKey key, bool isVolatile, bool isPrivate, bool isFinal);

   /**
    * \brief Create a shadow symbol
    *
    * \param type is the type of the shadow
    * \param isVolatile specifies whether the shadow corresponds to a vloatile field
    * \param isPrivate specifies whether the shadow corresponds to a private field
    * \param isFinal specifies whether the shadow corresponds to a final field
    * \param name is the name of the corresponding field
    * \param recognizedField sepcifies a recognized field the symbol corresponds to
    * \return TR::Symbol* the pointer to the new symbol
    */
   TR::Symbol * createShadowSymbol(TR::DataType type, bool isVolatile, bool isPrivate, bool isFinal, const char *name, TR::Symbol::RecognizedField recognizedField);

   TR::SymbolReference * createShadowSymbolWithoutCpIndex(TR::ResolvedMethodSymbol *, bool , TR::DataType, uint32_t, bool);
   TR::SymbolReference * findJavaLangReferenceReferentShadowSymbol(TR_ResolvedMethod * owningMethod, TR::DataType, uint32_t);

   TR_Array<List<TR::SymbolReference>*> _dynamicMethodSymrefsByCallSiteIndex;

   List<TR_ImmutableInfo>              _immutableInfo;
   TR_Array<TR_BitVector *>            _immutableSymRefNumbers;

   /** \brief
    *     Represents the set of symbol references to static fields of Java objects.
    */
   TR_Array<TR::SymbolReference *> * _unsafeJavaStaticSymRefs;

   /** \brief
    *     Represents the set of symbol references to static volatile fields of Java objects.
    */
   TR_Array<TR::SymbolReference *> * _unsafeJavaStaticVolatileSymRefs;

   ResolvedFieldShadows _resolvedFieldShadows;

   static const char *_commonNonHelperSymbolNames[];

   };

}
#define _numImmutableClasses (TR::java_lang_String_init - TR::java_lang_Boolean_init + 1)

#endif

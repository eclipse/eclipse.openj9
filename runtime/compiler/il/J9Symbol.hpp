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

#ifndef J9_SYMBOL_INCL
#define J9_SYMBOL_INCL

/*
 * The following #define and typedef must appear before any #includes in this file
 */
#ifndef J9_SYMBOL_CONNECTOR
#define J9_SYMBOL_CONNECTOR
namespace J9 { class Symbol; }
namespace J9 { typedef J9::Symbol SymbolConnector; }
#endif

#include "il/OMRSymbol.hpp"

#include <stddef.h>
#include <stdint.h>
#include "env/TRMemory.hpp"
#include "il/DataTypes.hpp"
#include "infra/Assert.hpp"
#include "infra/Flags.hpp"

class TR_FrontEnd;
class TR_ResolvedMethod;
namespace TR { class AutomaticSymbol; }
namespace TR { class Compilation; }
namespace TR { class LabelSymbol; }
namespace TR { class MethodSymbol; }
namespace TR { class ParameterSymbol; }
namespace TR { class RegisterMappedSymbol; }
namespace TR { class ResolvedMethodSymbol; }
namespace TR { class StaticSymbol; }
namespace TR { class Symbol; }

namespace J9 {

class OMR_EXTENSIBLE Symbol : public OMR::SymbolConnector
   {

public:
   TR_ALLOC(TR_Memory::Symbol)

protected:

   Symbol() :
      OMR::SymbolConnector() { }

   Symbol(TR::DataType d) :
      OMR::SymbolConnector(d)
      {
      TR_ASSERT(!d.isBCD(),"BCD symbols must be created with a > 0 size\n");
      }

   Symbol(TR::DataType d, uint32_t size) :
      OMR::SymbolConnector(d, size)
      {
      TR_ASSERT(!d.isBCD() || size > 0,"BCD symbols must be created with a > 0 size\n");
      }

public:

   virtual ~Symbol() {}

   inline TR::Symbol * getRecognizedShadowSymbol();

   void setDataType(TR::DataType dt);

   static uint32_t convertTypeToSize(TR::DataType dt);


   enum RecognizedField
      {
      UnknownField,
      Com_ibm_gpu_Kernel_blockIdxX,
      Com_ibm_gpu_Kernel_blockIdxY,
      Com_ibm_gpu_Kernel_blockIdxZ,
      Com_ibm_gpu_Kernel_blockDimX,
      Com_ibm_gpu_Kernel_blockDimY,
      Com_ibm_gpu_Kernel_blockDimZ,
      Com_ibm_gpu_Kernel_threadIdxX,
      Com_ibm_gpu_Kernel_threadIdxY,
      Com_ibm_gpu_Kernel_threadIdxZ,
      Com_ibm_gpu_Kernel_syncThreads,
      Com_ibm_jit_JITHelpers_IS_32_BIT,
      Com_ibm_jit_JITHelpers_J9OBJECT_J9CLASS_OFFSET,
      Com_ibm_jit_JITHelpers_OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS,
      Com_ibm_jit_JITHelpers_OBJECT_HEADER_HAS_BEEN_HASHED_IN_CLASS,
      Com_ibm_jit_JITHelpers_J9OBJECT_FLAGS_MASK32,
      Com_ibm_jit_JITHelpers_J9OBJECT_FLAGS_MASK64,
      Com_ibm_jit_JITHelpers_JLTHREAD_J9THREAD_OFFSET,
      Com_ibm_jit_JITHelpers_J9THREAD_J9VM_OFFSET,
      Com_ibm_jit_JITHelpers_J9_GC_OBJECT_ALIGNMENT_SHIFT,
      Com_ibm_jit_JITHelpers_J9ROMARRAYCLASS_ARRAYSHAPE_OFFSET,
      Com_ibm_jit_JITHelpers_J9CLASS_BACKFILL_OFFSET_OFFSET,
      Com_ibm_jit_JITHelpers_ARRAYSHAPE_ELEMENTCOUNT_MASK,
      Com_ibm_jit_JITHelpers_J9CONTIGUOUSARRAY_HEADER_SIZE,
      Com_ibm_jit_JITHelpers_J9DISCONTIGUOUSARRAY_HEADER_SIZE,
      Com_ibm_jit_JITHelpers_J9OBJECT_CONTIGUOUS_LENGTH_OFFSET,
      Com_ibm_jit_JITHelpers_J9OBJECT_DISCONTIGUOUS_LENGTH_OFFSET,
      Com_ibm_jit_JITHelpers_JLOBJECT_ARRAY_BASE_OFFSET,
      Com_ibm_jit_JITHelpers_J9CLASS_J9ROMCLASS_OFFSET,
      Com_ibm_jit_JITHelpers_J9JAVAVM_IDENTITY_HASH_DATA_OFFSET,
      Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA1_OFFSET,
      Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA2_OFFSET,
      Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA3_OFFSET,
      Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_SALT_TABLE_OFFSET,
      Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_STANDARD,
      Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_REGION,
      Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_NONE,
      Com_ibm_jit_JITHelpers_IDENTITY_HASH_SALT_POLICY,
      Com_ibm_oti_vm_VM_J9CLASS_CLASS_FLAGS_OFFSET,
      Com_ibm_oti_vm_VM_J9CLASS_INITIALIZE_STATUS_OFFSET,
      Com_ibm_oti_vm_VM_J9_JAVA_CLASS_RAM_SHAPE_SHIFT,
      Com_ibm_oti_vm_VM_OBJECT_HEADER_SHAPE_MASK,
      Com_ibm_oti_vm_VM_ADDRESS_SIZE,
      Java_io_ByteArrayOutputStream_count,
      Java_lang_J9VMInternals_jitHelpers,
      Java_lang_ref_SoftReference_age,
      Java_lang_String_count,
      Java_lang_String_enableCompression,
      Java_lang_String_hashCode,
      Java_lang_String_value,
      Java_lang_StringBuffer_count,
      Java_lang_StringBuffer_value,
      Java_lang_StringBuilder_count,
      Java_lang_StringBuilder_value,
      Java_lang_Throwable_stackTrace,
      Java_lang_invoke_BruteArgumentMoverHandle_extra,
      Java_lang_invoke_DynamicInvokerHandle_site,
      Java_lang_invoke_CallSite_target,
      Java_lang_invoke_LambdaForm_vmentry,
      Java_lang_invoke_MutableCallSite_target,
      Java_lang_invoke_MutableCallSiteDynamicInvokerHandle_mutableSite,
      Java_lang_invoke_MemberName_vmtarget,
      Java_lang_invoke_MethodHandle_form,
      Java_lang_invoke_MethodHandle_thunks,
      Java_lang_invoke_MethodHandle_type,
      Java_lang_invoke_MethodType_ptypes,
      Java_lang_invoke_PrimitiveHandle_rawModifiers,
      Java_lang_invoke_PrimitiveHandle_defc,
      Java_lang_invoke_ThunkTuple_invokeExactThunk,
      Java_util_Hashtable_elementCount,
      Java_math_BigInteger_ZERO,
      Java_math_BigInteger_useLongRepresentation,
      Java_lang_invoke_VarHandle_handleTable,
      Java_lang_invoke_MethodHandleImpl_LoopClauses_clauses,
      Java_lang_Integer_value,
      Java_lang_Long_value,
      Java_lang_Float_value,
      Java_lang_Double_value,
      Java_lang_Byte_value,
      Java_lang_Character_value,
      Java_lang_Short_value,
      Java_lang_Boolean_value,
      Java_lang_Class_enumVars,
      Java_lang_ClassEnumVars_cachedEnumConstants,
      assertionsDisabled,
      NumRecognizedFields
      };

   static RecognizedField searchRecognizedField(TR::Compilation *, TR_ResolvedMethod * owningMethodSymbol, int32_t cpIndex, bool isStatic);
   RecognizedField  getRecognizedField();
   const char *owningClassNameCharsForRecognizedField(int32_t & length);

   /**
    * TR_RecognizedShadowSymbol
    *
    * @{
    */
public:

   template <typename AllocatorType>
   static TR::Symbol * createRecognizedShadow(AllocatorType m, TR::DataType d, RecognizedField f);

   template <typename AllocatorType>
   static TR::Symbol * createRecognizedShadow(AllocatorType m, TR::DataType d, uint32_t s, RecognizedField f);

private:

   /// This recognized field is currently only used for RecognizedShadows.
   RecognizedField _recognizedField;
   /** @} */

public:

   // These two methods are primarily for direct analysis of bytecode. If
   // generating trees, use SymbolReferenceTable instead.

   template <typename AllocatorType>
   static TR::Symbol * createPossiblyRecognizedShadowWithFlags(
      AllocatorType m,
      TR::DataType type,
      bool isVolatile,
      bool isFinal,
      bool isPrivate,
      RecognizedField recognizedField);

   template <typename AllocatorType>
   static TR::Symbol * createPossiblyRecognizedShadowFromCP(
      TR::Compilation *comp,
      AllocatorType m,
      TR_ResolvedMethod *owningMethod,
      int32_t cpIndex,
      TR::DataType *type,
      uint32_t *offset,
      bool needsAOTValidation);

   };
}

#endif

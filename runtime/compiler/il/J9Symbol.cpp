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

/**************************************************************************
                       Testarossa JIT Codegen

  File name:  Symbol.cpp
  Attributes: Platform independent, IL interface
  Purpose:    Implementation of the:
                        Symbol class

**************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "env/FrontEnd.hpp"
#include "compile/Compilation.hpp"
#include "compile/SymbolReferenceTable.hpp"
#include "env/TRMemory.hpp"
#include "env/VMJ9.h"
#include "il/DataTypes.hpp"
#include "il/StaticSymbol.hpp"
#include "il/StaticSymbol_inlines.hpp"
#include "il/Symbol.hpp"
#include "il/SymbolReference.hpp"
#include "infra/Assert.hpp"
#include "infra/Flags.hpp"
#include "ras/Debug.hpp"

    struct F
      {
      TR::Symbol::RecognizedField id;
      // split "<class>.<field> <signature>"
      const char *classStr;   // "<class>"
      uint16_t classLen;
      const char *fieldStr;   // "<field>"
      uint16_t fieldLen;
      const char *sigStr;    // "<signature>"
      uint16_t sigLen;
      };
   #define r(id, c, f, s)   id, c, sizeof(c)-1, f, sizeof(f)-1, s, sizeof(s)-1

   /*
    * These recognized fields start with 'c' (like "com/...") as class name.
    */
   static F recognizedFieldName_c[] =
      {
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockIdxX, "com/ibm/gpu/Kernel", "blockIdxX", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockIdxY, "com/ibm/gpu/Kernel", "blockIdxY", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockIdxZ, "com/ibm/gpu/Kernel", "blockIdxZ", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockDimX, "com/ibm/gpu/Kernel", "blockDimX", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockDimY, "com/ibm/gpu/Kernel", "blockDimY", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_blockDimZ, "com/ibm/gpu/Kernel", "blockDimZ", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_threadIdxX, "com/ibm/gpu/Kernel", "threadIdxX", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_threadIdxY, "com/ibm/gpu/Kernel", "threadIdxY", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_threadIdxZ, "com/ibm/gpu/Kernel", "threadIdxZ", "I")},
      {r(TR::Symbol::Com_ibm_gpu_Kernel_syncThreads, "com/ibm/gpu/Kernel", "syncThreads", "I")},

      {r(TR::Symbol::Com_ibm_jit_JITHelpers_IS_32_BIT, "com/ibm/jit/JITHelpers", "IS_32_BIT", "Z")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9OBJECT_J9CLASS_OFFSET, "com/ibm/jit/JITHelpers", "J9OBJECT_J9CLASS_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS, "com/ibm/jit/JITHelpers", "OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_OBJECT_HEADER_HAS_BEEN_HASHED_IN_CLASS, "com/ibm/jit/JITHelpers", "OBJECT_HEADER_HAS_BEEN_HASHED_IN_CLASS", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9OBJECT_FLAGS_MASK32, "com/ibm/jit/JITHelpers", "J9OBJECT_FLAGS_MASK32", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9OBJECT_FLAGS_MASK64, "com/ibm/jit/JITHelpers", "J9OBJECT_FLAGS_MASK64", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_JLTHREAD_J9THREAD_OFFSET, "com/ibm/jit/JITHelpers", "JLTHREAD_J9THREAD_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9THREAD_J9VM_OFFSET, "com/ibm/jit/JITHelpers", "J9THREAD_J9VM_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9_GC_OBJECT_ALIGNMENT_SHIFT, "com/ibm/jit/JITHelpers", "J9_GC_OBJECT_ALIGNMENT_SHIFT", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9ROMARRAYCLASS_ARRAYSHAPE_OFFSET, "com/ibm/jit/JITHelpers", "J9ROMARRAYCLASS_ARRAYSHAPE_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9CLASS_BACKFILL_OFFSET_OFFSET, "com/ibm/jit/JITHelpers", "J9CLASS_BACKFILL_OFFSET_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_ARRAYSHAPE_ELEMENTCOUNT_MASK, "com/ibm/jit/JITHelpers", "ARRAYSHAPE_ELEMENTCOUNT_MASK", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9CONTIGUOUSARRAY_HEADER_SIZE, "com/ibm/jit/JITHelpers", "J9CONTIGUOUSARRAY_HEADER_SIZE", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9DISCONTIGUOUSARRAY_HEADER_SIZE, "com/ibm/jit/JITHelpers", "J9DISCONTIGUOUSARRAY_HEADER_SIZE", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9OBJECT_CONTIGUOUS_LENGTH_OFFSET, "com/ibm/jit/JITHelpers", "J9OBJECT_CONTIGUOUS_LENGTH_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9OBJECT_DISCONTIGUOUS_LENGTH_OFFSET, "com/ibm/jit/JITHelpers", "J9OBJECT_DISCONTIGUOUS_LENGTH_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_JLOBJECT_ARRAY_BASE_OFFSET, "com/ibm/jit/JITHelpers", "JLOBJECT_ARRAY_BASE_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9CLASS_J9ROMCLASS_OFFSET, "com/ibm/jit/JITHelpers", "J9CLASS_J9ROMCLASS_OFFSET", "I")}, //22
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9JAVAVM_IDENTITY_HASH_DATA_OFFSET, "com/ibm/jit/JITHelpers", "J9JAVAVM_IDENTITY_HASH_DATA_OFFSET","I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA1_OFFSET,"com/ibm/jit/JITHelpers", "J9IDENTITYHASHDATA_HASH_DATA1_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA2_OFFSET,"com/ibm/jit/JITHelpers", "J9IDENTITYHASHDATA_HASH_DATA2_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_DATA3_OFFSET,"com/ibm/jit/JITHelpers", "J9IDENTITYHASHDATA_HASH_DATA3_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9IDENTITYHASHDATA_HASH_SALT_TABLE_OFFSET,"com/ibm/jit/JITHelpers","J9IDENTITYHASHDATA_HASH_SALT_TABLE_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_STANDARD,"com/ibm/jit/JITHelpers","J9_IDENTITY_HASH_SALT_POLICY_STANDARD", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_REGION,"com/ibm/jit/JITHelpers","J9_IDENTITY_HASH_SALT_POLICY_REGION", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_J9_IDENTITY_HASH_SALT_POLICY_NONE,"com/ibm/jit/JITHelpers","J9_IDENTITY_HASH_SALT_POLICY_NONE", "I")},
      {r(TR::Symbol::Com_ibm_jit_JITHelpers_IDENTITY_HASH_SALT_POLICY,"com/ibm/jit/JITHelpers","IDENTITY_HASH_SALT_POLICY", "I")},
      {r(TR::Symbol::Com_ibm_oti_vm_VM_J9CLASS_CLASS_FLAGS_OFFSET,"com/ibm/oti/vm/VM","J9CLASS_CLASS_FLAGS_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_oti_vm_VM_J9CLASS_INITIALIZE_STATUS_OFFSET,"com/ibm/oti/vm/VM","J9CLASS_INITIALIZE_STATUS_OFFSET", "I")},
      {r(TR::Symbol::Com_ibm_oti_vm_VM_J9_JAVA_CLASS_RAM_SHAPE_SHIFT,"com/ibm/oti/vm/VM","J9AccClassRAMShapeShift", "I")},
      {r(TR::Symbol::Com_ibm_oti_vm_VM_OBJECT_HEADER_SHAPE_MASK,"com/ibm/oti/vm/VM","OBJECT_HEADER_SHAPE_MASK", "I")},
      {r(TR::Symbol::Com_ibm_oti_vm_VM_ADDRESS_SIZE,"com/ibm/oti/vm/VM","ADDRESS_SIZE", "I")},
      {TR::Symbol::UnknownField}
      };

   /*
    * These recognized fields start with 'j' (like "java/...") as class name.
    */
   static F recognizedFieldName_j[] =
      {
       /*
        * NOTE: If you add a new class here, make sure its length is within
        * minClassLength and maxClassLength of the corresponding FP struct.
        *
        * r(<enum>,                                                  "<class>"                      ,"<field>", "<sig>")
        */
       {r(TR::Symbol::Java_io_ByteArrayOutputStream_count,            "java/io/ByteArrayOutputStream", "count", "I")},
       {r(TR::Symbol::Java_lang_J9VMInternals_jitHelpers,             "java/lang/J9VMInternals", "jitHelpers", "Lcom/ibm/jit/JITHelpers;")},
       {r(TR::Symbol::Java_lang_String_count,                         "java/lang/String", "count", "I")},
       {r(TR::Symbol::Java_lang_String_enableCompression,             "java/lang/String", "COMPACT_STRINGS", "Z") },
       {r(TR::Symbol::Java_lang_String_hashCode,                      "java/lang/String", "hash", "I")},
       {r(TR::Symbol::Java_lang_String_value,                         "java/lang/String", "value", "[B")},
       {r(TR::Symbol::Java_lang_String_value,                         "java/lang/String", "value", "[C")},
       {r(TR::Symbol::Java_lang_StringBuffer_count,                   "java/lang/StringBuffer", "count", "I")},
       {r(TR::Symbol::Java_lang_StringBuffer_value,                   "java/lang/StringBuffer", "value", "[B")},
       {r(TR::Symbol::Java_lang_StringBuffer_value,                   "java/lang/StringBuffer", "value", "[C")},
       {r(TR::Symbol::Java_lang_StringBuilder_count,                  "java/lang/StringBuilder", "count", "I")},
       {r(TR::Symbol::Java_lang_StringBuilder_value,                  "java/lang/StringBuilder", "value", "[B")},
       {r(TR::Symbol::Java_lang_StringBuilder_value,                  "java/lang/StringBuilder", "value", "[C")},
       {r(TR::Symbol::Java_lang_Throwable_stackTrace,                 "java/lang/Throwable", "stackTrace", "[Ljava/lang/StackTraceElement;")},
       {r(TR::Symbol::Java_lang_invoke_BruteArgumentMoverHandle_extra,"java/lang/invoke/BruteArgumentMoverHandle", "extra", "[Ljava/lang/Object;")},
       {r(TR::Symbol::Java_lang_invoke_DynamicInvokerHandle_site,     "java/lang/invoke/DynamicInvokerHandle", "site", "Ljava/lang/invoke/CallSite;")},
       {r(TR::Symbol::Java_lang_invoke_CallSite_target,               "java/lang/invoke/CallSite", "target", "Ljava/lang/invoke/MethodHandle;")},
       {r(TR::Symbol::Java_lang_invoke_LambdaForm_vmentry,            "java/lang/invoke/LambdaForm", "vmentry", "Ljava/lang/invoke/MemberName;")},
       {r(TR::Symbol::Java_lang_invoke_MutableCallSite_target,        "java/lang/invoke/MutableCallSite", "target", "Ljava/lang/invoke/MethodHandle;")},
       {r(TR::Symbol::Java_lang_invoke_MutableCallSiteDynamicInvokerHandle_mutableSite,"java/lang/invoke/MutableCallSiteDynamicInvokerHandle", "mutableSite", "Ljava/lang/invoke/MutableCallSite;")},
       {r(TR::Symbol::Java_lang_invoke_MemberName_vmtarget,           "java/lang/invoke/MemberName", "vmtarget", "J")},
       {r(TR::Symbol::Java_lang_invoke_MethodHandle_form,             "java/lang/invoke/MethodHandle", "form", "Ljava/lang/invoke/LambdaForm;")},
       {r(TR::Symbol::Java_lang_invoke_MethodHandle_thunks,           "java/lang/invoke/MethodHandle", "thunks", "Ljava/lang/invoke/ThunkTuple;")},
       {r(TR::Symbol::Java_lang_invoke_MethodHandle_type,             "java/lang/invoke/MethodHandle", "type", "Ljava/lang/invoke/MethodType;")},
       {r(TR::Symbol::Java_lang_invoke_MethodType_ptypes,             "java/lang/invoke/MethodType", "ptypes", "[Ljava/lang/Class;")},
       {r(TR::Symbol::Java_lang_invoke_PrimitiveHandle_rawModifiers,  "java/lang/invoke/PrimitiveHandle", "rawModifiers", "I")},
       {r(TR::Symbol::Java_lang_invoke_PrimitiveHandle_defc,          "java/lang/invoke/PrimitiveHandle", "defc", "Ljava/lang/Class;")},
       {r(TR::Symbol::Java_lang_invoke_ThunkTuple_invokeExactThunk,   "java/lang/invoke/ThunkTuple", "invokeExactThunk", "J")},
       {r(TR::Symbol::Java_util_Hashtable_elementCount,               "java/util/Hashtable", "count", "I")},
       {r(TR::Symbol::Java_math_BigInteger_ZERO,                      "java/math/BigInteger", "ZERO", "Ljava/math/BigInteger;")},
       {r(TR::Symbol::Java_math_BigInteger_useLongRepresentation,     "java/math/BigInteger", "useLongRepresentation", "Z")},
       {r(TR::Symbol::Java_lang_ref_SoftReference_age,                "java/lang/ref/SoftReference", "age", "I")},
       {r(TR::Symbol::Java_lang_invoke_VarHandle_handleTable,         "java/lang/invoke/VarHandle", "handleTable", "[Ljava/lang/invoke/MethodHandle;")},
       {r(TR::Symbol::Java_lang_invoke_MethodHandleImpl_LoopClauses_clauses,         "java/lang/invoke/MethodHandleImpl$LoopClauses", "clauses", "[[Ljava/lang/invoke/MethodHandle;")},
       {r(TR::Symbol::Java_lang_Integer_value,                        "java/lang/Integer", "value", "I")},
       {r(TR::Symbol::Java_lang_Long_value,                           "java/lang/Long", "value", "J")},
       {r(TR::Symbol::Java_lang_Float_value,                          "java/lang/Float", "value", "F")},
       {r(TR::Symbol::Java_lang_Double_value,                         "java/lang/Double", "value", "D")},
       {r(TR::Symbol::Java_lang_Byte_value,                           "java/lang/Byte", "value", "B")},
       {r(TR::Symbol::Java_lang_Character_value,                      "java/lang/Character", "value", "C")},
       {r(TR::Symbol::Java_lang_Short_value,                          "java/lang/Short", "value", "S")},
       {r(TR::Symbol::Java_lang_Boolean_value,                        "java/lang/Boolean", "value", "Z")},
       {r(TR::Symbol::Java_lang_Class_enumVars,                       "java/lang/Class", "enumVars", "Ljava/lang/Class$EnumVars;")},
       {r(TR::Symbol::Java_lang_ClassEnumVars_cachedEnumConstants,    "java/lang/Class$EnumVars", "cachedEnumConstants", "[Ljava/lang/Object;")},
       {TR::Symbol::UnknownField}
      };

TR::Symbol::RecognizedField
J9::Symbol::searchRecognizedField(TR::Compilation * comp, TR_ResolvedMethod * owningMethod, int32_t cpIndex, bool isStatic)
   {
   struct FP
      {
      struct F   *fieldInfo;
      uint16_t    minClassLength;
      uint16_t    maxClassLength;
      };
   static FP fieldPrefixTable[] =
      {
       /*
        * List is sorted by class name prefix
        * The first prefix is 'c'
        */
       {recognizedFieldName_c, 17, 22},
       {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
       {recognizedFieldName_j, 14, 52}
      };

   const char minClassPrefix = 'c';
   const char maxClassPrefix = 'j';

   TR_OpaqueClassBlock *declaringClass = owningMethod->getDeclaringClassFromFieldOrStatic(comp, cpIndex);

   // $assertionDisabled fields are always foldable based on the Javadoc (setClassAssertionStatus
   // "This method has no effect if the named class has already been initialized.
   // (Once a class is initialized, its assertion status cannot change.)"
   // So check if the field is final and check if it is this special field
   if (isStatic)
      {
      int32_t  totalLen;
      char    *fieldName;
      fieldName = owningMethod->staticName(cpIndex, totalLen, comp->trMemory());  // totalLen = strlen("<class>" + "<field>" + "<sig>") + 3
      static char *assertionsDisabledStr = "$assertionsDisabled Z";
      //string will be of the form "<class>.$assertionsDisabled Z"
      if (declaringClass
          && totalLen >= 22
          && comp->fej9()->isClassInitialized(declaringClass)
          && strncmp(&(fieldName[totalLen - 22]), assertionsDisabledStr, 21) == 0)
         {
         if (comp->getOption(TR_TraceCG))
            traceMsg(comp, "Matched $assertionsDisabled Z\n");
         return assertionsDisabled;
         }
      }

   /*
    * quick check
    */
   int32_t  classLen;
   char    *className; // Note: not Null-terminated!
   if (declaringClass)
      className = comp->fej9()->getClassNameChars(declaringClass, classLen);
   else
      className = owningMethod->classNameOfFieldOrStatic(cpIndex, classLen);  // classLen = strlen("<class>");

   if (!className ||
       className[0] < minClassPrefix ||
       className[0] > maxClassPrefix)
      {
      return TR::Symbol::UnknownField;
      }

   FP * recognizedFieldPrefix = &fieldPrefixTable[className[0] - minClassPrefix];
   if (!recognizedFieldPrefix ||
       classLen < recognizedFieldPrefix->minClassLength || classLen > recognizedFieldPrefix->maxClassLength)
      {
      return TR::Symbol::UnknownField;
      }

   F       *recognizedFieldName = recognizedFieldPrefix->fieldInfo;
   int32_t i;
   F       *knownField;

   int32_t fieldLen;
   char* fieldName = isStatic ? owningMethod->staticNameChars(cpIndex, fieldLen) : owningMethod->fieldNameChars(cpIndex, fieldLen);

   int32_t sigLen;
   char* fieldSig = isStatic ? owningMethod->staticSignatureChars(cpIndex, sigLen) : owningMethod->fieldSignatureChars(cpIndex, sigLen);

   for (i=0, knownField = &recognizedFieldName[i];
        knownField->id != TR::Symbol::UnknownField;
        knownField = &recognizedFieldName[++i])
      {
      if (classLen ==knownField->classLen &&
          fieldLen == knownField->fieldLen &&
          sigLen == knownField->sigLen &&
          strncmp(knownField->fieldStr, fieldName, fieldLen) == 0 &&
          strncmp(knownField->classStr, className, classLen) == 0)
         {
         // TODO (Filip): This is a workaround for Java 829 performance as we switched to using a byte[] backing array in String*. Remove this workaround once obsolete.
         if (knownField->id != TR::Symbol::Java_lang_String_value &&
             knownField->id != TR::Symbol::Java_lang_StringBuffer_value &&
             knownField->id != TR::Symbol::Java_lang_StringBuilder_value)
            {
            TR_ASSERT(strncmp(knownField->sigStr, fieldSig, sigLen) == 0, "Signature is altered unexpectly!");
            }

         return knownField->id;
         }
      }

   return TR::Symbol::UnknownField;
   }

TR::Symbol::RecognizedField
J9::Symbol::getRecognizedField()
   {
   if (self()->isRecognizedShadow())
      return _recognizedField;
   else if (self()->isRecognizedStatic())
      return self()->getRecognizedStaticSymbol()->getRecognizedField();
   else
      return TR::Symbol::UnknownField;
   }

/**
 * Return the owning class name of this recognized field.
 * Return null if this symbol does not have a recognized field.
 */
const char *
J9::Symbol::owningClassNameCharsForRecognizedField(int32_t & length)
   {
   TR_ASSERT(isShadow(), "Must be a shadow symbol");
   TR::Symbol::RecognizedField recognizedField = self()->getRecognizedField();
   TR_ASSERT(TR::Symbol::UnknownField != recognizedField, "Symbol should have a valid recognized field");
   for (int i = 0; recognizedFieldName_c[i].id != TR::Symbol::UnknownField; ++i)
      {
      F &knownField = recognizedFieldName_c[i];
      if (knownField.id == recognizedField)
         {
         length = knownField.classLen;
         return knownField.classStr;
         }
      }
   for (int i = 0; recognizedFieldName_j[i].id != TR::Symbol::UnknownField; ++i)
      {
      F &knownField = recognizedFieldName_j[i];
      if (knownField.id == recognizedField)
         {
         length = knownField.classLen;
         return knownField.classStr;
         }
      }

   return NULL;
   }

/**
 * Sets the data type of a symbol, and the size, if the size can be inferred
 * from the data type.
 */
void
J9::Symbol::setDataType(TR::DataType dt)
   {
   if (dt.isBCD())
      {
      // do not infer a size for BCD types as their size varies
      //Set data type but not size
      _flags.setValue(DataTypeMask, dt);
      }
   else
      {
      OMR::SymbolConnector::setDataType(dt);
      }
   }

uint32_t
J9::Symbol::convertTypeToSize(TR::DataType dt)
   {
   TR_ASSERT(!dt.isBCD(),"size for BCD type is not fixed\n");
   return OMR::SymbolConnector::convertTypeToSize(dt);
   }

TR::Symbol *
J9::Symbol::getRecognizedShadowSymbol()
   {
   return self()->isRecognizedShadow() ? (TR::Symbol*)this : 0;
   }

template <typename AllocatorType>
TR::Symbol *
J9::Symbol::createRecognizedShadow(AllocatorType m, TR::DataType d, RecognizedField f)
   {
   auto * sym = createShadow(m, d);
   sym->_recognizedField = f;
   sym->_flags.set(RecognizedShadow);
   if ((f == TR::Symbol::Java_lang_Class_enumVars) || (f == TR::Symbol::Java_lang_ClassEnumVars_cachedEnumConstants))
      sym->_flags.set(RecognizedKnownObjectShadow);
   return sym;
   }

template <typename AllocatorType>
TR::Symbol *
J9::Symbol::createRecognizedShadow(AllocatorType m, TR::DataType d, uint32_t s, RecognizedField f)
   {
   auto * sym = createShadow(m,d,s);
   sym->_recognizedField = f;
   sym->_flags.set(RecognizedShadow);
   if ((f == TR::Symbol::Java_lang_Class_enumVars) || (f == TR::Symbol::Java_lang_ClassEnumVars_cachedEnumConstants))
      sym->_flags.set(RecognizedKnownObjectShadow);
   return sym;
   }

template <typename AllocatorType>
TR::Symbol *
J9::Symbol::createPossiblyRecognizedShadowWithFlags(
   AllocatorType m,
   TR::DataType type,
   bool isVolatile,
   bool isFinal,
   bool isPrivate,
   RecognizedField recognizedField)
   {
   TR::Symbol *fieldSymbol = NULL;
   if (recognizedField != TR::Symbol::UnknownField)
      fieldSymbol = TR::Symbol::createRecognizedShadow(m, type, recognizedField);
   else
      fieldSymbol = TR::Symbol::createShadow(m, type);

   if (isVolatile)
      fieldSymbol->setVolatile();

   if (isFinal)
      fieldSymbol->setFinal();

   if (isPrivate)
      fieldSymbol->setPrivate();

   return fieldSymbol;
   }

template <typename AllocatorType>
TR::Symbol *
J9::Symbol::createPossiblyRecognizedShadowFromCP(
   TR::Compilation *comp,
   AllocatorType m,
   TR_ResolvedMethod *owningMethod,
   int32_t cpIndex,
   TR::DataType *type, // can be determined accurately even for unresolved field refs
   uint32_t *offset, // typically stored for some reason on symref (not Symbol)
   bool needsAOTValidation)
   {
   *type = TR::NoType;
   *offset = 0;

   const bool isStatic = false;
   RecognizedField recognizedField =
      TR::Symbol::searchRecognizedField(comp, owningMethod, cpIndex, isStatic);

   bool isVolatile, isFinal, isPrivate, isUnresolvedInCP;

   const bool isStore = false;
   bool resolved = owningMethod->fieldAttributes(
      comp,
      cpIndex,
      offset,
      type,
      &isVolatile,
      &isFinal,
      &isPrivate,
      isStore,
      &isUnresolvedInCP,
      needsAOTValidation);

   if (!resolved)
      return NULL;

   return createPossiblyRecognizedShadowWithFlags(
      m, *type, isVolatile, isFinal, isPrivate, recognizedField);
   }

/*
 * Explicit instantiation of factories for each TR_Memory type.
 */

template TR::Symbol * J9::Symbol::createRecognizedShadow(TR_StackMemory,         TR::DataType, RecognizedField);
template TR::Symbol * J9::Symbol::createRecognizedShadow(TR_HeapMemory,          TR::DataType, RecognizedField);

template TR::Symbol * J9::Symbol::createRecognizedShadow(TR_StackMemory,         TR::DataType, uint32_t, RecognizedField);
template TR::Symbol * J9::Symbol::createRecognizedShadow(TR_HeapMemory,          TR::DataType, uint32_t, RecognizedField);

template TR::Symbol * J9::Symbol::createPossiblyRecognizedShadowWithFlags(TR_StackMemory,         TR::DataType, bool, bool, bool, RecognizedField);
template TR::Symbol * J9::Symbol::createPossiblyRecognizedShadowWithFlags(TR_HeapMemory,          TR::DataType, bool, bool, bool, RecognizedField);

template TR::Symbol * J9::Symbol::createPossiblyRecognizedShadowFromCP(TR::Compilation*, TR_StackMemory,         TR_ResolvedMethod*, int32_t, TR::DataType*, uint32_t*, bool);
template TR::Symbol * J9::Symbol::createPossiblyRecognizedShadowFromCP(TR::Compilation*, TR_HeapMemory,          TR_ResolvedMethod*, int32_t, TR::DataType*, uint32_t*, bool);

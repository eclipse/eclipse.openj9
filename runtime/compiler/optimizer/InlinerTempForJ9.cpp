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
#include <algorithm>
#include "j9cfg.h"
#include "optimizer/Inliner.hpp"
#include "optimizer/J9Inliner.hpp"
#include "optimizer/J9EstimateCodeSize.hpp"

#include "env/KnownObjectTable.hpp"
#include "compile/InlineBlock.hpp"
#include "compile/Method.hpp"
#include "compile/OSRData.hpp"
#include "compile/ResolvedMethod.hpp"
#include "env/CompilerEnv.hpp"
#include "env/CHTable.hpp"
#include "env/PersistentCHTable.hpp"
#include "env/VMJ9.h"
#include "env/jittypes.h"
#include "il/Block.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/ParameterSymbol.hpp"
#include "il/StaticSymbol.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "optimizer/CallInfo.hpp"
#include "optimizer/J9CallGraph.hpp"
#include "optimizer/PreExistence.hpp"
#include "optimizer/Structure.hpp"
#include "codegen/CodeGenerator.hpp"
#include "codegen/CodeGenerator_inlines.hpp"
#include "il/ILOpCodes.hpp"
#include "il/ILOps.hpp"
#include "ilgen/IlGenRequest.hpp"
#include "ilgen/IlGeneratorMethodDetails.hpp"
#include "ilgen/IlGeneratorMethodDetails_inlines.hpp"
#include "optimizer/Structure.hpp"                        // TR_RegionAnalysis
#include "optimizer/StructuralAnalysis.hpp"
#include "control/Recompilation.hpp"                      //TR_PersistentJittedBodyInfo
#include "control/RecompilationInfo.hpp"                  //TR_PersistentJittedBodyInfo
#include "optimizer/EstimateCodeSize.hpp"
#include "env/VMJ9.h"
#include "runtime/J9Profiler.hpp"
#include "ras/DebugCounter.hpp"
#include "j9consts.h"
#include "optimizer/TransformUtil.hpp"

namespace TR { class SimpleRegex; }

#define OPT_DETAILS "O^O INLINER: "

// == Hack markers ==

// To conserve owning method indexes, we share TR_ResolvedMethods even where
// the owning method differs.  Usually that is done only for methods that the
// inliner never sees, but if we get aggressive and share TR_ResolvedMethods
// that are exposed to the inliner, then the inliner needs to make sure it
// doesn't rely on the "owning method" accurately representing the calling method.
//
// This is a fragile design that needs some more thought.  We should either
// eliminate the limitations that motivate sharing in the first place, or else
// take the time to modify inliner (and everyone else) so it doesn't rely on
// owning method information to indicate the caller.
//
// For now, we mark such code with this macro.
//
#define OWNING_METHOD_MAY_NOT_BE_THE_CALLER (1)

#define MIN_NUM_CALLERS 20
#define MIN_FAN_IN_SIZE 50
#define SIZE_MULTIPLIER 4
#define FANIN_OTHER_BUCKET_THRESHOLD 0.5
#define DEFAULT_CONST_CLASS_WEIGHT 10

#undef TRACE_CSI_IN_INLINER

const char* TR_PrexArgument::priorKnowledgeStrings[] = { "", "(preexistent) ", "(fixed-class) ", "(known-object) " };

static bool isWarm(TR::Compilation *comp)
   {
   return comp->getMethodHotness() >= warm;
   }
static bool isHot(TR::Compilation *comp)
   {
   return comp->getMethodHotness() >= hot;
   }
static bool isScorching(TR::Compilation *comp)
   {
   return ((comp->getMethodHotness() >= scorching) || ((comp->getMethodHotness() >= veryHot) && comp->isProfilingCompilation())) ;
   }

static int32_t getJ9InitialBytecodeSize(TR_ResolvedMethod * feMethod, TR::ResolvedMethodSymbol * methodSymbol, TR::Compilation *comp)
   {
   int32_t size = feMethod->maxBytecodeIndex();

   if (methodSymbol && methodSymbol->getRecognizedMethod() == TR::java_util_ArrayList_remove)
      {
      size >>= 1;
      }

   if (feMethod->getRecognizedMethod() == TR::java_lang_String_indexOf_String_int ||
       feMethod->getRecognizedMethod() == TR::java_lang_String_init_String       ||
       feMethod->getRecognizedMethod() == TR::java_lang_String_indexOf_fast       ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_subMulSetScale ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_addAddMulSetScale ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_mulSetScale ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_noLLOverflowAdd ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_noLLOverflowMul ||
       feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_subMulAddAddMulSetScale ||
       feMethod->getRecognizedMethod() == TR::com_ibm_ws_webcontainer_channel_WCCByteBufferOutputStream_printUnencoded ||
       feMethod->getRecognizedMethod() == TR::java_lang_String_equals)
      {
      size >>= 1;
      }

    else if (feMethod->isDAAWrapperMethod())
      {
      size = 1;
      }

    else if (feMethod->isDAAIntrinsicMethod())
      {
      size >>= 3;
      }

   else if (feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_valueOf )
      {
      size >>= 2;
      }
   else if (feMethod->getRecognizedMethod() == TR::java_math_BigDecimal_add ||
            feMethod->getRecognizedMethod() == TR::java_lang_String_init_int_String_int_String_String ||
            feMethod->getRecognizedMethod() == TR::com_ibm_jit_DecimalFormatHelper_formatAsDouble ||
            feMethod->getRecognizedMethod() == TR::com_ibm_jit_DecimalFormatHelper_formatAsFloat)
      {
      size >>= 3;
      }

   else if (strncmp(feMethod->nameChars(), "toString", 8) == 0 ||
            strncmp(feMethod->nameChars(), "multiLeafArrayCopy", 18) == 0)
      {
      size >>= 1;
      }
   else if (!comp->getOption(TR_DisableAdaptiveDumbInliner))
      {
      if (methodSymbol && !methodSymbol->mayHaveInlineableCall() && size <= 5) // favor the inlining of methods that are very small
         size = 0;
      }

   TR_J9EstimateCodeSize::adjustEstimateForStringCompression(feMethod, size, TR_J9EstimateCodeSize::STRING_COMPRESSION_ADJUSTMENT_FACTOR);

   return size;
   }

static bool insideIntPipelineForEach(TR_ResolvedMethod *method, TR::Compilation *comp)
   {
   char *sig = "accept";
   bool returnValue = true; //default is true since if first method is IntPipeline.forEach true is returned

   //Searches up the owning method chain until IntPipeline.forEach is found
   //If the first method passed into this function is IntPipeline$Head.forEach or IntPipeline.forEach, true is returned
   //since IntPipeline$Head.forEach and IntPipeline.forEach needs to be inlined for JIT GPU.
   //If not the method name is checked to see if it is called accept.
   //If the method in the chain just before IntPipeline.forEach is accept, true is also returned
   //This tries to inline accept and the methods inside accept
   //IntPipelineHead.forEach must also be inlined.
   if (method && comp->getOptions()->getEnableGPU(TR_EnableGPU) && comp->hasIntStreamForEach())
      {
      if (method->getRecognizedMethod() == TR::java_util_stream_IntPipelineHead_forEach)
         return true;

      while (method)
         {
         if (method->getRecognizedMethod() == TR::java_util_stream_IntPipeline_forEach)
            return returnValue;

         //If the current method is accept, true is returned if the next method is IntPipeline.forEach
         //Otherwise, if the next method is IntPipeline.forEach, false is returned
         if (strncmp(method->nameChars(), sig, strlen(sig)) == 0)
            returnValue = true;
         else
            returnValue = false;

         method = method->owningMethod();
         }
      }

   return false;
   }

bool
TR_J9InlinerPolicy::inlineRecognizedMethod(TR::RecognizedMethod method)
   {
//    if (method ==
//        TR::java_lang_String_init_String_char)
//       return false;
   if (comp()->cg()->suppressInliningOfRecognizedMethod(method))
      return false;

   if (comp()->isConverterMethod(method) &&
            comp()->canTransformConverterMethod(method))
      return false;

   // Check for memoizing methods
   if (comp()->getOption(TR_DisableDememoization) || (comp()->getMethodHotness() < hot)) // TODO: This should actually check whether EA will run, but that's not crucial for correctness
   {
      switch (method)
         {
         case TR::java_lang_Integer_valueOf:
            comp()->getMethodSymbol()->setHasNews(true);
            return true;
         default:
            break;
         }
      }
   else if (method == TR::java_lang_Integer_valueOf)
     return false;

   if (willBeInlinedInCodeGen(method))
      return false;

   return true;

   }

int32_t
TR_J9InlinerPolicy::getInitialBytecodeSize(TR_ResolvedMethod *feMethod, TR::ResolvedMethodSymbol * methodSymbol, TR::Compilation *comp)
   {
   return getJ9InitialBytecodeSize(feMethod, methodSymbol, comp);
   }

bool
TR_J9InlinerPolicy::aggressivelyInlineInLoops()
   {
   return _aggressivelyInlineInLoops;
   }

void
TR_J9InlinerPolicy::determineAggressionInLoops(TR::ResolvedMethodSymbol *callerSymbol)
   {
   if (isHot(comp()) && OMR_InlinerPolicy::getInitialBytecodeSize(callerSymbol, comp()) < 100)
      _aggressivelyInlineInLoops = true;
   }

void
TR_J9InlinerPolicy::determineInliningHeuristic(TR::ResolvedMethodSymbol *callerSymbol)
   {
   determineAggressionInLoops(callerSymbol);
   return;
   }

void TR_MultipleCallTargetInliner::generateNodeEstimate::operator ()(TR_CallTarget *ct, TR::Compilation *comp)
   {
   int32_t size = getJ9InitialBytecodeSize(ct->_calleeMethod, 0, comp);

   // only scale the inlining size when the method is non-empty - conversion of
   // NaN/Inf to int is undefined and an empty method partially inlined instead
   // of fully inlined is still empty
   if(ct->_isPartialInliningCandidate && ct->_fullSize != 0)
      {
      size = size * ((float)(ct->_partialSize)/(float)(ct->_fullSize));
      }
   _nodeEstimate += size;
   }

bool
TR_J9InlinerPolicy::mustBeInlinedEvenInDebug(TR_ResolvedMethod * calleeMethod, TR::TreeTop *callNodeTreeTop)
   {
   if (calleeMethod)
      {
      switch (calleeMethod->convertToMethod()->getMandatoryRecognizedMethod())
         {
         // call to invokeExactTargetAddress are generated out of thin air by our JSR292
         // implementation, but we never want the VM or anyone else to know this so we must
         // always inline the implementation
         case TR::java_lang_invoke_MethodHandle_invokeExactTargetAddress:
            {
            TR::TreeTop *scanTT = callNodeTreeTop->getNextTreeTop();
            TR::Node *nextCall = NULL;

            while (scanTT &&
                   scanTT->getNode()->getByteCodeInfo().getByteCodeIndex() == callNodeTreeTop->getNode()->getByteCodeInfo().getByteCodeIndex() &&
                   scanTT->getNode()->getByteCodeInfo().getCallerIndex() == callNodeTreeTop->getNode()->getByteCodeInfo().getCallerIndex())
               {
               TR::Node *scanNode = scanTT->getNode();
               if (scanNode && (scanNode->getOpCode().isCheck() || scanNode->getOpCodeValue() == TR::treetop))
                   scanNode = scanNode->getFirstChild();

               if (scanNode->getOpCode().isCall())
                  {
                  nextCall = scanNode;
                  break;
                  }
               scanTT = scanTT->getNextTreeTop();
               }

            debugTrace(tracer(), "considering nextOperation node n%dn", nextCall->getGlobalIndex());
            if (nextCall && nextCall->getOpCode().hasSymbolReference() &&
                nextCall->getSymbolReference()->getSymbol()->getMethodSymbol()->isComputedVirtual())
               return true;
            }
         default:
          break;
         }
      }
   return false;
   }

/**  Test for methods that we wish to inline whenever possible.

   Identify methods for which the benefits of inlining them into the caller
   are particularly significant and which might not otherwise be chosen by
   the inliner.
*/
bool
TR_J9InlinerPolicy::alwaysWorthInlining(TR_ResolvedMethod * calleeMethod, TR::Node *callNode)
   {
   if (!calleeMethod)
      return false;

   if (isInlineableJNI(calleeMethod, callNode))
      return true;

   if (calleeMethod->isDAAWrapperMethod())
      return true;

   if (isJSR292AlwaysWorthInlining(calleeMethod))
      return true;

   switch (calleeMethod->getRecognizedMethod())
      {
      case TR::sun_misc_Unsafe_getAndAddLong:
      case TR::sun_misc_Unsafe_getAndSetLong:
         return comp()->target().is32Bit();
      case TR::java_lang_J9VMInternals_fastIdentityHashCode:
      case TR::java_lang_Class_getSuperclass:
      case TR::java_lang_String_regionMatchesInternal:
      case TR::java_lang_String_regionMatches:
      case TR::java_lang_Class_newInstance:
      // we rely on inlining compareAndSwap so we see the inner native call and can special case it
      case TR::com_ibm_jit_JITHelpers_compareAndSwapIntInObject:
      case TR::com_ibm_jit_JITHelpers_compareAndSwapLongInObject:
      case TR::com_ibm_jit_JITHelpers_compareAndSwapObjectInObject:
      case TR::com_ibm_jit_JITHelpers_compareAndSwapIntInArray:
      case TR::com_ibm_jit_JITHelpers_compareAndSwapLongInArray:
      case TR::com_ibm_jit_JITHelpers_compareAndSwapObjectInArray:
      case TR::com_ibm_jit_JITHelpers_jitHelpers:
      case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfLatin1:
      case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfUTF16:
      case TR::java_lang_String_charAt:
      case TR::java_lang_String_charAtInternal_I:
      case TR::java_lang_String_charAtInternal_IB:
      case TR::java_lang_String_checkIndex:
      case TR::java_lang_String_coder:
      case TR::java_lang_String_isLatin1:
      case TR::java_lang_String_length:
      case TR::java_lang_String_lengthInternal:
      case TR::java_lang_String_isCompressed:
      case TR::java_lang_StringBuffer_capacityInternal:
      case TR::java_lang_StringBuffer_lengthInternalUnsynchronized:
      case TR::java_lang_StringBuilder_capacityInternal:
      case TR::java_lang_StringBuilder_lengthInternal:
      case TR::java_lang_StringUTF16_charAt:
      case TR::java_lang_StringUTF16_checkIndex:
      case TR::java_lang_StringUTF16_length:
      case TR::java_lang_StringUTF16_newBytesFor:
      case TR::java_util_HashMap_get:
      case TR::java_util_HashMap_getNode:
      case TR::java_lang_String_getChars_charArray:
      case TR::java_lang_String_getChars_byteArray:
      case TR::java_lang_Integer_toUnsignedLong:
      case TR::java_nio_Bits_byteOrder:
      case TR::java_nio_ByteOrder_nativeOrder:
         return true;

      // In Java9 the following enum values match both sun.misc.Unsafe and
      // jdk.internal.misc.Unsafe The sun.misc.Unsafe methods are simple
      // wrappers to call jdk.internal impls, and we want to inline them. Since
      // the same code can run with Java8 classes where sun.misc.Unsafe has the
      // JNI impl, we need to differentiate by testing with isNative(). If it is
      // native, then we don't need to inline it as it will be handled
      // elsewhere.
      case TR::sun_misc_Unsafe_compareAndSwapInt_jlObjectJII_Z:
      case TR::sun_misc_Unsafe_compareAndSwapLong_jlObjectJJJ_Z:
      case TR::sun_misc_Unsafe_compareAndSwapObject_jlObjectJjlObjectjlObject_Z:
      case TR::sun_misc_Unsafe_copyMemory:
         return !calleeMethod->isNative();
      default:
         break;
      }

   if (!strncmp(calleeMethod->classNameChars(), "java/util/concurrent/atomic/", strlen("java/util/concurrent/atomic/")))
      {
      return true;
      }

   int32_t length = calleeMethod->classNameLength();
   char* className = calleeMethod->classNameChars();

   if (length == 24 && !strncmp(className, "jdk/internal/misc/Unsafe", 24))
      return true;
   else if (length == 15 && !strncmp(className, "sun/misc/Unsafe", 15))
      return true;

   return false;
   }

/*
 * Check if there is any method handle invoke left in the method body
 */
static bool checkForRemainingInlineableJSR292(TR::Compilation *comp, TR::ResolvedMethodSymbol *methodSymbol)
   {
   TR::NodeChecklist visited(comp);
   for (TR::TreeTop *tt = methodSymbol->getFirstTreeTop(); tt ; tt = tt->getNextTreeTop())
      {
      TR::Node *ttNode = tt->getNode();
      if (ttNode->getNumChildren() > 0)
         {
         TR::Node *node = ttNode->getFirstChild();
         if (node->getOpCode().isCall() && !visited.contains(node))
            {
            visited.add(node);
            if (node->getSymbolReference()->getSymbol()->getResolvedMethodSymbol())
               {
               TR_ResolvedMethod * resolvedMethod = node->getSymbolReference()->getSymbol()->getResolvedMethodSymbol()->getResolvedMethod();
               if (!node->isTheVirtualCallNodeForAGuardedInlinedCall() &&
                  (comp->fej9()->isLambdaFormGeneratedMethod(resolvedMethod) ||
                   resolvedMethod->getRecognizedMethod() == TR::java_lang_invoke_MethodHandle_invokeBasic ||
                   resolvedMethod->convertToMethod()->isArchetypeSpecimen() ||
                   resolvedMethod->getRecognizedMethod() == TR::java_lang_invoke_MethodHandle_invokeExact))
                  {
                  return true;
                  }
               }
            }
         }
      }
   return false;
   }

void
TR_J9InlinerUtil::requestAdditionalOptimizations(TR_CallTarget *calltarget)
   {
   if (calltarget->_myCallSite->getDepth() == -1 // only do this for top level callee to prevent exponential walk of inlined trees
      && checkForRemainingInlineableJSR292(comp(), calltarget->_calleeSymbol))
      {
      _inliner->getOptimizer()->setRequestOptimization(OMR::methodHandleInvokeInliningGroup);
      if (comp()->trace(OMR::inlining))
         heuristicTrace(tracer(),"Requesting one more pass of targeted inlining due to method handle invoke in %s\n", tracer()->traceSignature(calltarget->_calleeSymbol));
      }
   }

void
TR_J9InlinerUtil::adjustByteCodeSize(TR_ResolvedMethod *calleeResolvedMethod, bool isInLoop, TR::Block *block, int &bytecodeSize)
   {
   traceMsg(comp(), "Reached new code \n");
      int32_t blockNestingDepth = 1;
      if (isInLoop)
        {
         char *tmptmp=0;
         if (calleeResolvedMethod)
            tmptmp = TR::Compiler->cls.classSignature(comp(), calleeResolvedMethod->containingClass(),trMemory());

         bool doit = false;

         if (((TR_J9InlinerPolicy *)inliner()->getPolicy())->aggressivelyInlineInLoops())
            {
            doit = true;
            }

         if (doit && calleeResolvedMethod && !strcmp(tmptmp,"Ljava/math/BigDecimal;"))
            {
            traceMsg(comp(), "Reached code for block nesting depth %d\n", blockNestingDepth);
            if ((isInLoop || (blockNestingDepth > 1)) &&
               (bytecodeSize > 10))
               {
               if (comp()->trace(OMR::inlining))
                  heuristicTrace(tracer(),"Exceeds Size Threshold: Scaled down size for call block %d from %d to %d\n", block->getNumber(), bytecodeSize, 10);
               bytecodeSize = 15;
               }
            }
         else
            heuristicTrace(tracer(),"Omitting Big Decimal method from size readjustment, calleeResolvedMethod = %p, tmptmp =%s",calleeResolvedMethod, tmptmp);
        }
   }

TR::Node *
TR_J9InlinerPolicy::genCompressedRefs(TR::Node * address, bool genTT, int32_t isLoad)
   {
   static char *pEnv = feGetEnv("TR_UseTranslateInTrees");

   if (performTransformation(comp(), "O^O Inliner: Generating compressedRefs anchor for node [%p]\n", address))
      {
      TR::Node *value = address;
      if (pEnv && (isLoad < 0)) // store
         value = address->getSecondChild();
      TR::Node *newAddress = TR::Node::createCompressedRefsAnchor(value);
      //traceMsg(comp(), "compressedRefs anchor %p generated\n", newAddress);
      if (!pEnv && genTT)
         {
         if (!newAddress->getOpCode().isTreeTop())
            newAddress = TR::Node::create(TR::treetop, 1, newAddress);
         }
      else
         return newAddress;
      }
   return NULL;
   }


TR::Node *
TR_J9InlinerPolicy::createUnsafeAddressWithOffset(TR::Node * unsafeCall)
   {
   if (comp()->target().is64Bit())
      {
      TR::Node *constNode = TR::Node::lconst(unsafeCall, ~(J9_SUN_FIELD_OFFSET_MASK));
      return TR::Node::create(TR::aladd, 2, unsafeCall->getChild(1), TR::Node::create(TR::land, 2, unsafeCall->getChild(2), constNode));
      }

   return TR::Node::create(TR::aiadd, 2, unsafeCall->getChild(1), TR::Node::create(TR::iand, 2, TR::Node::create(TR::l2i, 1, unsafeCall->getChild(2)), TR::Node::iconst(unsafeCall, ~(J9_SUN_FIELD_OFFSET_MASK))));
   }

void
TR_J9InlinerPolicy::createTempsForUnsafeCall( TR::TreeTop *callNodeTreeTop, TR::Node * unsafeCallNode )
   {
   //This method is intended to replace and generalize createTempsForUnsafePutGet
   //Will go through all children of the unsafeCallNode and create a store tree that child, insert it before the call, and then replace the call's child with a load of the symref.

   for (int32_t i = 0 ; i < unsafeCallNode->getNumChildren() ; ++i)
      {
      TR::Node *child = unsafeCallNode->getChild(i);


      //create a store of the correct type and insert before call.

      TR::DataType dataType = child->getDataType();
      TR::SymbolReference *newSymbolReference = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), dataType);

      TR::Node *storeNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(dataType), 1, 1, child, newSymbolReference);
      TR::TreeTop *storeTree = TR::TreeTop::create(comp(), storeNode);

      debugTrace(tracer(),"Creating store node %p with child %p",storeNode,child);

      callNodeTreeTop->insertBefore(storeTree);

      // Replace the old child with a load of the new sym ref
      TR::Node *value = TR::Node::createWithSymRef(child, comp()->il.opCodeForDirectLoad(dataType), 0, newSymbolReference);

      debugTrace(tracer(),"Replacing callnode %p child %p with %p",unsafeCallNode,unsafeCallNode->getChild(i),value);

      unsafeCallNode->setAndIncChild(i, value);
      child->recursivelyDecReferenceCount();

      }
   }
void
TR_J9InlinerPolicy::createTempsForUnsafePutGet(TR::Node*& unsafeAddress,
                                           TR::Node* unsafeCall,
                                           TR::TreeTop* callNodeTreeTop,
                                           TR::Node*& offset,
                                           TR::SymbolReference*& newSymbolReferenceForAddress,
                                           bool isUnsafeGet)
   {
   TR::Node *oldUnsafeAddress = unsafeAddress;
   TR::DataType dataType = unsafeAddress->getDataType();
   TR::SymbolReference *newSymbolReference =
      comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), dataType);
   newSymbolReferenceForAddress = newSymbolReference;
   TR::Node *storeNode =
      TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(unsafeAddress->getDataType()),
                      1, 1, unsafeAddress, newSymbolReference);
   TR::TreeTop *storeTree = TR::TreeTop::create(comp(), storeNode);

   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\tIn createTempsForUnsafePutGet.  inserting store Tree before callNodeTT:\n");
      comp()->getDebug()->print(comp()->getOutFile(), storeTree);
      }

   callNodeTreeTop->insertTreeTopsBeforeMe(storeTree);

   // Replace the old child with a load of the new sym ref
   unsafeAddress =
      TR::Node::createWithSymRef(unsafeAddress,
                      comp()->il.opCodeForDirectLoad(unsafeAddress->getDataType()),
                      0, newSymbolReference);

   debugTrace(tracer(), "\tIn createTempsForUnsafePutGet. replacing unsafeCall ( %p) child %p with %p\n", unsafeCall, unsafeCall->getChild(1), unsafeAddress);

   unsafeCall->setAndIncChild(1, unsafeAddress);

   TR::Node *oldOffset = offset;
   dataType = offset->getDataType();
   newSymbolReference = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(),
                                                                dataType);
   storeNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(offset->getDataType()),
                               1, 1, offset, newSymbolReference);
   storeTree = TR::TreeTop::create(comp(), storeNode);

   if (tracer()->debugLevel())
      {
      traceMsg(comp(), "\tIn createTempsForUnsafePutGet.  inserting store Tree before callNodeTT 2:\n");
      comp()->getDebug()->print(comp()->getOutFile(), storeTree);
      }

   callNodeTreeTop->insertTreeTopsBeforeMe(storeTree);

   // Replace the old child with a load of the new sym ref
   offset = TR::Node::createWithSymRef(offset,
                            comp()->il.opCodeForDirectLoad(offset->getDataType()),
                            0, newSymbolReference);

   debugTrace(tracer(), "\tIn createTempsForUnsafePutGet. replacing unsafeCall ( %p) child %p with %p\n", unsafeCall, unsafeCall->getChild(2), offset);

   unsafeCall->setAndIncChild(2, offset);
   if (!isUnsafeGet)
      {
      TR::Node *value = unsafeCall->getChild(3);
      TR::Node *oldValue = value;
      dataType = value->getDataType();
      newSymbolReference =
         comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), dataType);
      storeNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(value->getDataType()), 1, 1, value, newSymbolReference);
      storeTree = TR::TreeTop::create(comp(), storeNode);
      callNodeTreeTop->insertTreeTopsBeforeMe(storeTree);

      // Replace the old child with a load of the new sym ref
      value = TR::Node::createWithSymRef(value,
                              comp()->il.opCodeForDirectLoad(value->getDataType()),
                              0, newSymbolReference);
      unsafeCall->setAndIncChild(3, value);
      oldValue->recursivelyDecReferenceCount();
      }

   oldUnsafeAddress->recursivelyDecReferenceCount();
   oldOffset->recursivelyDecReferenceCount();
   }
// Extra boolean is for AOT case when we can not simplify the Class Test so we need Array Case in that scenario, But we can put indirect in cold instead of fallthrough for the tests
TR::TreeTop*
TR_J9InlinerPolicy::genClassCheckForUnsafeGetPut(TR::Node* offset, bool isNotLowTagged)
   {
   // The low bit is tagged if the object being dereferenced is a
   // java/lang/Class object. This is because this is a special case when
   // an extra level of indirection is necessary
   bool isILoad = (offset->getOpCodeValue() == TR::iload);
   TR::Node *lowTag = NULL;

   if (isILoad)
      lowTag = TR::Node::create(TR::iand, 2, offset, TR::Node::iconst(1));
   else
      lowTag = TR::Node::create(TR::land, 2, offset, TR::Node::lconst(1));

   TR::ILOpCodes op = isNotLowTagged ? (isILoad ? TR::ificmpne : TR::iflcmpne) : (isILoad ?  TR::ificmpeq : TR::iflcmpeq);
   // Create the if to check if an extra level of indirection is needed
   TR::Node *cmp = TR::Node::createif(op, lowTag, lowTag->getSecondChild(), NULL);
   TR::TreeTop* lowTagCmpTree = TR::TreeTop::create(comp(), cmp);
   return lowTagCmpTree;
   }


TR::TreeTop*
TR_J9InlinerPolicy::genClassCheckForUnsafeGetPut(TR::Node* offset)
   {
   // The low bit is tagged if the object being dereferenced is a
   // java/lang/Class object. This is because this is a special case when
   // an extra level of indirection is necessary
   bool isILoad = (offset->getOpCodeValue() == TR::iload);
   TR::Node *lowTag =
      TR::Node::create(isILoad ? TR::iand : TR::land, 2, offset,
                      TR::Node::create(offset, isILoad ? TR::iconst : TR::lconst, 0, 0));
   if (isILoad)
      lowTag->getSecondChild()->setInt(1);
   else
      lowTag->getSecondChild()->setLongInt(1);

   // Create the if to check if an extra level of indirection is needed
   TR::Node *cmp = TR::Node::createif(isILoad ? TR::ificmpne : TR::iflcmpne,
                                    lowTag, lowTag->getSecondChild(), NULL);
   TR::TreeTop* lowTagCmpTree = TR::TreeTop::create(comp(), cmp);
   return lowTagCmpTree;
   }


TR::TreeTop*
TR_J9InlinerPolicy::genDirectAccessCodeForUnsafeGetPut(TR::Node* callNode,
                                                   bool conversionNeeded, bool isUnsafeGet)
   {
   //Generate the code for the direct access
   TR::Node *directAccessNode = callNode->duplicateTree();
   TR::TreeTop *directAccessTreeTop = TR::TreeTop::create(comp(), directAccessNode, NULL, NULL);
   TR::Node* firstChild = directAccessNode->getFirstChild();

   if (isUnsafeGet) {
      firstChild = firstChild->getFirstChild();
      //if there is a conversion node we need to go one level deeper
      if (conversionNeeded)
         firstChild = firstChild->getFirstChild();
   }
   else {
      // if there is an anchor, the store is one level below
      if (directAccessNode->getOpCodeValue() == TR::compressedRefs)
         firstChild = firstChild->getFirstChild();
   }

   TR_ASSERT(((firstChild->getOpCodeValue() == TR::aiadd) ||
           (firstChild->getOpCodeValue() == TR::aladd)), "Unexpected opcode in unsafe access\n");
   TR::Node *grandChild = firstChild->getSecondChild();
   firstChild->setAndIncChild(1, grandChild->getFirstChild());
   grandChild->recursivelyDecReferenceCount();

   // If a conversion is needed, the 'callNode' was constructed earlier on in createUnsafe(get/put)WithOffset
    // While some of the children end up in the final trees, this constructed callNode does not.
    // We need to dec the reference count otherwise a child of the callNode that ends up in the final trees will have an extra refcount and will cause
    // an assert
    if(conversionNeeded)
       {
       for(int32_t i=0 ; i< callNode->getNumChildren(); i++)
          {
          debugTrace(tracer(), "\t In genDirectAccessCodeForUnsafeGetPut, recursively dec'ing refcount of %p:\n", callNode->getChild(i));

          callNode->getChild(i)->recursivelyDecReferenceCount();
          }
       }

   return directAccessTreeTop;
   }


TR::TreeTop*
TR_J9InlinerPolicy::genIndirectAccessCodeForUnsafeGetPut(TR::Node* directAccessOrTempStoreNode, TR::Node* unsafeAddress)
   {
   // Generate the indirect access code in the java/lang/Class case. First modify unsafeAddress which is a descendant
   // of directAccessOrTempStoreNode and then make a copy of directAccessOrTempStoreNode.
   TR::Node *oldAddressFirstChild = unsafeAddress->getFirstChild();
   TR::Node *addressFirstChild =
      TR::Node::createWithSymRef(TR::aloadi, 1, 1, oldAddressFirstChild,
                      comp()->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef());
   addressFirstChild =
      TR::Node::createWithSymRef(TR::aloadi, 1, 1, addressFirstChild,
                      comp()->getSymRefTab()->findOrCreateRamStaticsFromClassSymbolRef());
   unsafeAddress->setAndIncChild(0, addressFirstChild);
   oldAddressFirstChild->recursivelyDecReferenceCount();

   TR::Node* indirectAccessOrTempStoreNode = directAccessOrTempStoreNode->duplicateTree();

   // The directAccessNode has been replaced with the corresponding get (load)/put (store) node at this point. The
   // purpose of this function is to create the indirect access (static access) for an unsafe get/put operation
   // since in general we do not know whether the target of our get/put operation is to a static or instance field.
   // As such for the indirect access (static access) case we need to reassign the symbol reference after duplicating
   // the direct access node.

   TR::Symbol* directSymbol = directAccessOrTempStoreNode->getSymbolReference()->getSymbol();

   if (!directSymbol->isUnsafeShadowSymbol())
      {
      // We may have generated a store to a temp in case of a get operation
      directSymbol = directAccessOrTempStoreNode->getFirstChild()->getSymbolReference()->getSymbol();
      }

   // Sanity check. Note this is fatal because under concurrent scavenge we could potentially miss a read barrier here.
   TR_ASSERT_FATAL(directSymbol->isUnsafeShadowSymbol(), "Expected to find an unsafe symbol for the get/put operation.");

   TR::Node* indirectAccessNode = indirectAccessOrTempStoreNode;

   TR::Symbol* indirectSymbol = indirectAccessOrTempStoreNode->getSymbolReference()->getSymbol();

   if (!indirectSymbol->isUnsafeShadowSymbol())
      {
      // We may have generated a store to a temp in case of a get operation
      indirectAccessNode = indirectAccessOrTempStoreNode->getFirstChild();
      }

   TR::SymbolReference* indirectSymRef = comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(directSymbol->getDataType(), true, true, directSymbol->isVolatile());

   indirectAccessNode->setSymbolReference(indirectSymRef);

   return TR::TreeTop::create(comp(), indirectAccessOrTempStoreNode, NULL, NULL);
   }

TR::Block *
TR_J9InlinerPolicy::addNullCheckForUnsafeGetPut(TR::Node* unsafeAddress,
                                            TR::SymbolReference* newSymbolReferenceForAddress,
                                            TR::TreeTop* callNodeTreeTop,
                                            TR::TreeTop* directAccessTreeTop,
                                            TR::TreeTop* arrayDirectAccessTreeTop,
                                            TR::TreeTop* indirectAccessTreeTop)
   {
   //Generate the treetop for the null comparison
   TR::Node *addrLoad =
      TR::Node::createWithSymRef(unsafeAddress,
                      comp()->il.opCodeForDirectLoad(unsafeAddress->getDataType()),
                      0, newSymbolReferenceForAddress);
   TR::Node *nullCmp =
      TR::Node::createif(TR::ifacmpeq, addrLoad,
                        TR::Node::create(addrLoad, TR::aconst, 0, 0), NULL);
   TR::TreeTop *nullComparisonTree = TR::TreeTop::create(comp(), nullCmp, NULL, NULL);
   TR::TreeTop* ifTree = arrayDirectAccessTreeTop ? arrayDirectAccessTreeTop : indirectAccessTreeTop;
   TR::TreeTop* elseTree = arrayDirectAccessTreeTop ? indirectAccessTreeTop : directAccessTreeTop;
   // Connect the trees/add blocks etc. properly and split the original block
   TR::Block * joinBlock =
      callNodeTreeTop->getEnclosingBlock()->
      createConditionalBlocksBeforeTree(callNodeTreeTop,
                                        nullComparisonTree,
                                        ifTree,
                                        elseTree,
                                        comp()->getFlowGraph(), false, false);
   return joinBlock;
   }

void
TR_J9InlinerPolicy::createAnchorNodesForUnsafeGetPut(TR::TreeTop* treeTop,
                                                 TR::DataType type, bool isUnsafeGet)
   {
   if (comp()->useCompressedPointers() && (type == TR::Address))
      {
      // create the anchor node only for the non-tagged case

      TR::Node* node = treeTop->getNode();
      TR::TreeTop *compRefTT =
         TR::TreeTop::create(comp(), genCompressedRefs(isUnsafeGet?node->getFirstChild():node,
                                                      false));
      if (compRefTT)
         {
         TR::TreeTop *prevTT = treeTop->getPrevTreeTop();
         prevTT->join(compRefTT);
         compRefTT->join(isUnsafeGet?treeTop:treeTop->getNextTreeTop());
         }
      }
   }

void
TR_J9InlinerPolicy::genCodeForUnsafeGetPut(TR::Node* unsafeAddress,
                                       TR::TreeTop* callNodeTreeTop,
                                       TR::TreeTop* prevTreeTop,
                                       TR::SymbolReference* newSymbolReferenceForAddress,
                                       TR::TreeTop* directAccessTreeTop,
                                       TR::TreeTop* lowTagCmpTree,
                                       bool needNullCheck, bool isUnsafeGet,
                                       bool conversionNeeded,
                                       TR::Block * joinBlock,
                                       TR_OpaqueClassBlock *javaLangClass,
                                       TR::Node* orderedCallNode = NULL)
   {
   TR::CFG *cfg = comp()->getFlowGraph();
   TR::Block *nullComparisonBlock = prevTreeTop->getEnclosingBlock();
   TR::TreeTop* nullComparisonTree = nullComparisonBlock->getLastRealTreeTop();
   TR::TreeTop *nullComparisonEntryTree = nullComparisonBlock->getEntry();
   TR::TreeTop *nullComparisonExitTree = nullComparisonBlock->getExit();
   //if conversionNeeded is true, we haven't generated and we don't need arrayDirectAccessBlock
   TR::Block *arrayDirectAccessBlock = conversionNeeded ? nullComparisonTree->getNode()->getBranchDestination()->getNode()->getBlock() : NULL;
   TR::Block *indirectAccessBlock;
   TR::Block * directAccessBlock;
   if (conversionNeeded)
      {
      //Generating block for direct access
      indirectAccessBlock = nullComparisonBlock->getNextBlock();
      directAccessBlock = TR::Block::createEmptyBlock(lowTagCmpTree->getNode(), comp(),
      indirectAccessBlock->getFrequency());
      directAccessBlock->append(directAccessTreeTop);
      directAccessBlock->append(TR::TreeTop::create(comp(),
                                TR::Node::create(directAccessTreeTop->getNode(),
                                TR::Goto, 0, joinBlock->getEntry())));
      arrayDirectAccessBlock->getExit()->insertTreeTopsAfterMe(directAccessBlock->getEntry(),
      directAccessBlock->getExit());
      cfg->addNode(directAccessBlock);
      cfg->addEdge(TR::CFGEdge::createEdge(directAccessBlock,  joinBlock, trMemory()));
      }
   else
      {
      directAccessBlock = nullComparisonBlock->getNextBlock();
      indirectAccessBlock = nullComparisonTree->getNode()->getBranchDestination()->getNode()->getBlock();
      indirectAccessBlock->setFrequency(VERSIONED_COLD_BLOCK_COUNT);
      indirectAccessBlock->setIsCold();
      nullComparisonTree->getNode()->setBranchDestination(directAccessBlock->getEntry());
      }

   debugTrace(tracer(), "\t In genCodeForUnsafeGetPut, Block %d created for direct Access\n", directAccessBlock->getNumber());

   //Generating block for lowTagCmpTree
   TR::Block *lowTagCmpBlock =
   TR::Block::createEmptyBlock(unsafeAddress, comp(), conversionNeeded ? indirectAccessBlock->getFrequency() : directAccessBlock->getFrequency());
   lowTagCmpBlock->append(lowTagCmpTree);
   cfg->addNode(lowTagCmpBlock);
   debugTrace(tracer(), "\t In genCodeForUnsafeGetPut, Block %d created for low tag comparison\n", lowTagCmpBlock->getNumber());

   TR::Node *vftLoad = TR::Node::createWithSymRef(TR::aloadi, 1, 1, TR::Node::createWithSymRef(unsafeAddress, comp()->il.opCodeForDirectLoad(unsafeAddress->getDataType()), 0, newSymbolReferenceForAddress), comp()->getSymRefTab()->findOrCreateVftSymbolRef());
   TR::TreeTop *isArrayTreeTop;
   TR::Block *isArrayBlock;
   TR::TreeTop *isClassTreeTop;
   TR::Block *isClassBlock;
   // If we need conversion or java/lang/Class is not loaded yet, we generate old sequence of tests
   if (conversionNeeded || javaLangClass == NULL)
      {
      TR::Node *isArrayField = NULL;
      if (comp()->target().is32Bit())
         {
         isArrayField = TR::Node::createWithSymRef(TR::iloadi, 1, 1, vftLoad, comp()->getSymRefTab()->findOrCreateClassAndDepthFlagsSymbolRef());
         }
      else
         {
         isArrayField = TR::Node::createWithSymRef(TR::lloadi, 1, 1, vftLoad, comp()->getSymRefTab()->findOrCreateClassAndDepthFlagsSymbolRef());
         isArrayField = TR::Node::create(TR::l2i, 1, isArrayField);
         }
      TR::Node *andConstNode = TR::Node::create(isArrayField, TR::iconst, 0, TR::Compiler->cls.flagValueForArrayCheck(comp()));
      TR::Node * andNode   = TR::Node::create(TR::iand, 2, isArrayField, andConstNode);
      TR::Node *isArrayNode = TR::Node::createif(TR::ificmpeq, andNode, andConstNode, NULL);
      isArrayTreeTop = TR::TreeTop::create(comp(), isArrayNode, NULL, NULL);
      isArrayBlock = TR::Block::createEmptyBlock(vftLoad, comp(), indirectAccessBlock->getFrequency());
      isArrayBlock->append(isArrayTreeTop);
      cfg->addNode(isArrayBlock);
      isArrayNode->setBranchDestination(conversionNeeded ? arrayDirectAccessBlock->getEntry() : directAccessBlock->getEntry());
      if (conversionNeeded)
         {
         indirectAccessBlock->getEntry()->insertTreeTopsBeforeMe(lowTagCmpBlock->getEntry(), lowTagCmpBlock->getExit());
         lowTagCmpTree->getNode()->setBranchDestination(directAccessBlock->getEntry());
         }
      else
         {
         traceMsg(comp(),"\t\t Generating an isArray test as j9class of java/lang/Class is NULL");
         directAccessBlock->getEntry()->insertTreeTopsBeforeMe(lowTagCmpBlock->getEntry(), lowTagCmpBlock->getExit());
         lowTagCmpTree->getNode()->setBranchDestination(indirectAccessBlock->getEntry());
         }
      lowTagCmpBlock->getEntry()->insertTreeTopsBeforeMe(isArrayBlock->getEntry(),
                                                         isArrayBlock->getExit());
      cfg->addEdge(TR::CFGEdge::createEdge(isArrayBlock,  lowTagCmpBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(lowTagCmpBlock, indirectAccessBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(isArrayBlock, conversionNeeded ? arrayDirectAccessBlock : directAccessBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(nullComparisonBlock,  isArrayBlock, trMemory()));

      debugTrace(tracer(), "\t In genCodeForUnsafeGetPut, Block %d created for array check\n", isArrayBlock->getNumber());
      }
   else
      {
      // Following sequence of code generate isClassTest.
      // ifacmpeq goto indirectAccess
      //    aload vft-symbol
      //    aconst J9Class of java/lang/Class

      // Note for loadJavaLangClass node:
      // AOT Relocation relies on guards to locate correct method for a call site, while inlined Unsafe calls do not have a guard.
      // Therefore J9Class of java/lang/Class cannot be relocated correctly. Inserting any guard for an inlined Unsafe call is potentially
      // expensive, especially for applications that intensively use Unsafe, such as Apache Spark.
      // As a compromise, the node is given such BCI as if it is generated from the out-most call, so that J9Class can be correctly
      // relocated without any guard.
      TR::Node *loadJavaLangClass = TR::Node::createAddressNode(vftLoad, TR::aconst,(uintptr_t) javaLangClass);
      loadJavaLangClass->getByteCodeInfo().setInvalidCallerIndex();
      loadJavaLangClass->getByteCodeInfo().setZeroByteCodeIndex();
      loadJavaLangClass->setIsClassPointerConstant(true);

      TR::Node *isClassNode = TR::Node::createif(TR::ifacmpeq, vftLoad, loadJavaLangClass, NULL);
      isClassTreeTop = TR::TreeTop::create(comp(), isClassNode, NULL, NULL);
      isClassBlock = TR::Block::createEmptyBlock(vftLoad, comp(), directAccessBlock->getFrequency());
      isClassBlock->append(isClassTreeTop);
      cfg->addNode(isClassBlock);
      directAccessBlock->getEntry()->insertTreeTopsBeforeMe(isClassBlock->getEntry(), isClassBlock->getExit());
      lowTagCmpTree->getNode()->setBranchDestination(directAccessBlock->getEntry());
      isClassNode->setBranchDestination(indirectAccessBlock->getEntry());
      isClassBlock->getEntry()->insertTreeTopsBeforeMe(lowTagCmpBlock->getEntry(), lowTagCmpBlock->getExit());
      cfg->addEdge(TR::CFGEdge::createEdge(isClassBlock,directAccessBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(isClassBlock,indirectAccessBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(nullComparisonBlock, lowTagCmpBlock, trMemory()));
      cfg->addEdge(TR::CFGEdge::createEdge(lowTagCmpBlock, isClassBlock, trMemory()));

      debugTrace(tracer(), "\t In genCodeForUnsafeGetPut, Block %d created for isClass Test\n", isClassBlock->getNumber());
      }
   cfg->addEdge(TR::CFGEdge::createEdge(lowTagCmpBlock,  directAccessBlock, trMemory()));
   //Generating treetop and block for array check
   cfg->removeEdge(nullComparisonBlock, indirectAccessBlock);
   if (needNullCheck)
      {
      TR::TreeTop *treeBeforeCmp = nullComparisonTree->getPrevTreeTop();
      TR::TreeTop *nullchkTree =
         TR::TreeTop::create(comp(), treeBeforeCmp,
                TR::Node::createWithSymRef(TR::NULLCHK, 1, 1,
                  TR::Node::create(TR::PassThrough, 1,
                     TR::Node::createWithSymRef(unsafeAddress,
                                     comp()->il.opCodeForDirectLoad(unsafeAddress->getDataType()),
                                     0, newSymbolReferenceForAddress)),
                               comp()->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp()->getMethodSymbol())
                               )
                            );
      nullchkTree->getNode()->getByteCodeInfo().setCallerIndex(comp()->getCurrentInlinedSiteIndex());
      }

   if (!isUnsafeGet && joinBlock && orderedCallNode)
      {
      TR::TreeTop *orderedCallTree = TR::TreeTop::create(comp(), orderedCallNode);
      joinBlock->prepend(orderedCallTree);
      }
   }

/*
Converting Unsafe.get/put* routines into inlined code involves two cases:
1) if the size of the element to put/get is 4 bytes or more
2) if the size of the element to put/get is less than 4 bytes (boolean, byte, char, short)

In (1), there are two alternatives on how to read from/write to the object: direct and
indirect write/read. The selection of alternatives is done by looking at three conditions:
a) whether the object is NULL
b) whether the object is array
c) whether the object is of type java.lang.Class
The pseudocode of the generated inline code for case (1) under normal compilation is :
if (object == NULL)
  use direct access
else if (offset is not low tagged)
  use direct access
else if (object is type of java/lang/Class)
  use indirect access
else
  use direct access

If we can not get the J9Class of java/lang/Class, we generate following sequence of tests
if (object == NULL)
   use direct access
else if (object it Array type)
   use direct access
else if (offset is low tagged)
   use indirect access
else
   use direct access


In (2), there are three alternatives on how to read from/write the object. direct,
direct with conversion, indirect. The same three conditions are used to decide which one
to use based on the following pseudocode:
if (object is NULL)
  use direct access with conversion
else if (object is array)
  use direct access with conversion
else if (object is of type Class)
  use indirect access
else
  use direct access

- genClassCheckForUnsafeGetPut builds the treetop for condition (c) above.
- genDirectAccessCodeForUnsafeGetPut completes the building of treetop for both "direct access" and
"direct access with conversion"
- genIndirectAccessCodeForUnsafeGetPut builds the treetop for indirect access
- addNullCheckForUnsafeGetPut builds node for NULLness check (condition (a) above) and
builds a diamond CFG based on that. The CFG will be completed in later stages.
- createAnchorNodesForUnsafeGetPut creates compressed references in case they are needed
- genCodeForUnsafeGetPut completes the CFG/code by adding the array check, Class check,
and the direct access code.

Note that in case (2), i.e., when the conversion is needed, we generate code like the
following for the "direct access with conversion" for Unsafe.getByte
    b2i
      ibload
        aiadd
while the direct access code looks like
    iiload
      aiadd
We will replace b2i and ibload by c2iu and icload for Unsafe.getChar, by
s2i and isload for Unsafe.getShort, and by bu2i and ibload for Unsafe.getBoolean

For Unsafe.putByte and Unsafe.putBoolean, we generate
   ibstore
     i2b
       <some load node>
We replace i2b and ibstore by i2c and icstore for Unsafe.getChar, and by i2s and isstore for
Unsafe.getShort.
*/

bool
TR_J9InlinerPolicy::createUnsafePutWithOffset(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * unsafeCall, TR::DataType type, bool isVolatile, bool needNullCheck, bool isOrdered)
   {
   if (isVolatile && type == TR::Int64 && comp()->target().is32Bit() && !comp()->cg()->getSupportsInlinedAtomicLongVolatiles())
      return false;
   TR_ASSERT(TR::Compiler->cls.classesOnHeap(), "Unsafe inlining code assumes classes are on heap\n");
   if (debug("traceUnsafe"))
      printf("createUnsafePutWithOffset %d in %s\n", type.getDataType(), comp()->signature());

   debugTrace(tracer(), "\tcreateUnsafePutWithOffset.  call tree %p offset(datatype) %d isvolatile %d needNullCheck %d isOrdered %d\n", callNodeTreeTop, type.getDataType(), isVolatile, needNullCheck, isOrdered);

   // Truncate the value before inlining the call
   if (TR_J9MethodBase::isUnsafeGetPutBoolean(calleeSymbol->getRecognizedMethod()))
      {
      TR::TransformUtil::truncateBooleanForUnsafeGetPut(comp(), callNodeTreeTop);
      }

   // Preserve null check on the unsafe object
   TR::TransformUtil::separateNullCheck(comp(), callNodeTreeTop, tracer()->debugLevel());

   // Since the block has to be split, we need to create temps for the arguments to the call
   for (int i = 0; i < unsafeCall->getNumChildren(); i++)
      {
      TR::Node* child = unsafeCall->getChild(i);
      TR::Node* newChild = TR::TransformUtil::saveNodeToTempSlot(comp(), child, callNodeTreeTop);
      unsafeCall->setAndIncChild(i, newChild);
      child->recursivelyDecReferenceCount();
      }

   TR::Node *offset = unsafeCall->getChild(2);
   TR::TreeTop *prevTreeTop = callNodeTreeTop->getPrevTreeTop();
   TR::SymbolReference *newSymbolReferenceForAddress = unsafeCall->getChild(1)->getSymbolReference();
   TR::SymbolReference * symRef = comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(type, true, false, isVolatile);
   TR::Node *orderedCallNode = NULL;

   if (isOrdered)
      {
      symRef->getSymbol()->setOrdered();
      orderedCallNode = callNodeTreeTop->getNode()->duplicateTree();
      orderedCallNode->getFirstChild()->setDontInlinePutOrderedCall();

      debugTrace(tracer(), "\t Duplicate Tree for ordered call, orderedCallNode = %p\n", orderedCallNode);
      }

   static char *disableIllegalWriteReport = feGetEnv("TR_DisableIllegalWriteReport");
   TR::TreeTop* reportFinalFieldModification = NULL;
   if (!disableIllegalWriteReport && !comp()->getOption(TR_DisableGuardedStaticFinalFieldFolding))
      {
      reportFinalFieldModification = TR::TransformUtil::generateReportFinalFieldModificationCallTree(comp(), unsafeCall->getArgument(1)->duplicateTree());
      }

   TR::Node * unsafeAddress = createUnsafeAddressWithOffset(unsafeCall);
   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\t After createUnsafeAddressWithOffset, unsafeAddress = %p : \n", unsafeAddress);
      TR::TreeTop *tmpUnsafeAddressTT = TR::TreeTop::create(comp(), unsafeAddress);
      comp()->getDebug()->print(comp()->getOutFile(), tmpUnsafeAddressTT);
      }

   TR::Node* valueWithoutConversion = unsafeCall->getChild(3);
   TR::Node* valueWithConversion = NULL;
   TR::Node* unsafeNodeWithConversion = NULL;

   debugTrace(tracer(), "\tvalueWithouTConversion = %p\n", valueWithoutConversion);


   bool conversionNeeded = comp()->fe()->dataTypeForLoadOrStore(type) != type;
   if (conversionNeeded)
      {
      TR::ILOpCodes conversionOpCode =
         TR::ILOpCode::getProperConversion(comp()->fe()->dataTypeForLoadOrStore(type),  type, true);
      TR::Node* conversionNode = TR::Node::create(conversionOpCode,
                                                 1, valueWithoutConversion);
      valueWithConversion = conversionNode;
      unsafeNodeWithConversion = type == TR::Address && (TR::Compiler->om.writeBarrierType() != gc_modron_wrtbar_none)
         ? TR::Node::createWithSymRef(TR::awrtbari, 3, 3, unsafeAddress, valueWithConversion, unsafeCall->getChild(1), symRef)
         : TR::Node::createWithSymRef(comp()->il.opCodeForIndirectArrayStore(type), 2, 2, unsafeAddress, valueWithConversion, symRef);

      debugTrace(tracer(), "\tConversion is Needed, conversionNode = %p unsafeNodeWithConversion = %p valueWithConversion = %p\n", conversionNode, unsafeNodeWithConversion, valueWithConversion);

      }
   TR::Node * unsafeNode = type == TR::Address && (TR::Compiler->om.writeBarrierType() != gc_modron_wrtbar_none)
      ? TR::Node::createWithSymRef(TR::awrtbari, 3, 3, unsafeAddress, valueWithoutConversion, unsafeCall->getChild(1), symRef)
      : TR::Node::createWithSymRef(comp()->il.opCodeForIndirectStore(type), 2, 2, unsafeAddress, valueWithoutConversion, symRef);


   TR::TreeTop *oldCallNodeTreeTop = 0;          // For Tracing Purposes Only
   if (tracer()->debugLevel())
         oldCallNodeTreeTop = TR::TreeTop::create(comp(),unsafeCall);

   callNodeTreeTop->setNode(unsafeNode);

   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\t After callNodeTreeTop setNode callNodeTreeTop dump:\n");
      comp()->getDebug()->print(comp()->getOutFile(), callNodeTreeTop);
      debugTrace(tracer(), "\t After callNodeTreeTop setNode oldCallNodeTreeTop dump oldCallNodeTreeTop->getNode->getChild = %p:\n", oldCallNodeTreeTop->getNode() ? oldCallNodeTreeTop->getNode()->getFirstChild() : 0);
      comp()->getDebug()->print(comp()->getOutFile(), oldCallNodeTreeTop);
      }


   TR::TreeTop* directAccessTreeTop = genDirectAccessCodeForUnsafeGetPut(unsafeNode, false, false);

   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\t After genDirectAccessCodeForUnsafeGetPut, directAccessTreeTop dump:\n");
      comp()->getDebug()->print(comp()->getOutFile(), directAccessTreeTop);
      }

   TR::TreeTop* arrayDirectAccessTreeTop = conversionNeeded
      ? genDirectAccessCodeForUnsafeGetPut(unsafeNodeWithConversion, conversionNeeded, false)
      : NULL;

   if (tracer()->debugLevel() && conversionNeeded)
      {
      debugTrace(tracer(), "\t After genDirectAccessCodeForUnsafeGetPut, arrayDirectAccessTreeTop dump:\n");
      comp()->getDebug()->print(comp()->getOutFile(), arrayDirectAccessTreeTop);
      }

   TR::TreeTop* indirectAccessTreeTop = genIndirectAccessCodeForUnsafeGetPut(callNodeTreeTop->getNode(), unsafeAddress);

   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\t After genIndirectAccessCodeForUnsafeGetPut, indirectAccessTreeTop dump:\n");
      comp()->getDebug()->print(comp()->getOutFile(), indirectAccessTreeTop);
      }

   if (indirectAccessTreeTop && indirectAccessTreeTop->getNode() && indirectAccessTreeTop->getNode()->getOpCode().isWrtBar())
      {
      debugTrace(tracer(), "Setting node %p as an unsafe static wrtbar\n", indirectAccessTreeTop->getNode());
      indirectAccessTreeTop->getNode()->setIsUnsafeStaticWrtBar(true);
      }

   TR_OpaqueClassBlock *javaLangClass = comp()->getClassClassPointer(/* isVettedForAOT = */ true);
   // If we are not able to get javaLangClass it is still inefficient to put direct Access far
   // So in that case we will generate lowTagCmpTest to branch to indirect access if true
   bool needNotLowTagged = javaLangClass != NULL  || conversionNeeded ;
   TR::TreeTop *lowTagCmpTree = genClassCheckForUnsafeGetPut(offset, needNotLowTagged);

   if (tracer()->debugLevel())
      {
      debugTrace(tracer(), "\t After genClassCheckForUnsafeGetPut, lowTagCmpTree dump:\n");
      comp()->getDebug()->print(comp()->getOutFile(), lowTagCmpTree);
      }

   TR::Block * joinBlock =
      addNullCheckForUnsafeGetPut(unsafeAddress, newSymbolReferenceForAddress,
                                  callNodeTreeTop, directAccessTreeTop,
                                  arrayDirectAccessTreeTop,
                                  indirectAccessTreeTop);

   debugTrace(tracer(), "\t After addNullCHeckForUnsafeGetPut, joinBlock is %d\n", joinBlock->getNumber());

   createAnchorNodesForUnsafeGetPut(directAccessTreeTop, type, false);
   if (arrayDirectAccessTreeTop)
      createAnchorNodesForUnsafeGetPut(arrayDirectAccessTreeTop, type, false);
   genCodeForUnsafeGetPut(unsafeAddress, callNodeTreeTop,
                          prevTreeTop, newSymbolReferenceForAddress,
                          directAccessTreeTop,
                          lowTagCmpTree, needNullCheck, false, conversionNeeded,
                          joinBlock, javaLangClass, orderedCallNode);


   // Test for static final field
   if (reportFinalFieldModification)
      {
      TR::Block* storeToStaticFieldBlock = indirectAccessTreeTop->getEnclosingBlock();
      auto isFinalStaticNode = TR::Node::createif(TR::iflcmpeq,
                                                  TR::Node::create(TR::land, 2, offset->duplicateTree(), TR::Node::lconst(J9_SUN_FINAL_FIELD_OFFSET_TAG)),
                                                  TR::Node::lconst(J9_SUN_FINAL_FIELD_OFFSET_TAG),
                                                  NULL /*branchTarget*/);
      auto isFinalStaticTreeTop = TR::TreeTop::create(comp(), isFinalStaticNode);

      TR::TransformUtil::createConditionalAlternatePath(comp(), isFinalStaticTreeTop, reportFinalFieldModification, storeToStaticFieldBlock, storeToStaticFieldBlock, comp()->getMethodSymbol()->getFlowGraph(), true /*markCold*/);

      debugTrace(tracer(), "Created isFinal test node n%dn whose branch target is Block_%d to report illegal write to static final field\n",
         isFinalStaticNode->getGlobalIndex(), reportFinalFieldModification->getEnclosingBlock()->getNumber());

      TR::DebugCounter::prependDebugCounter(comp(),
                                            TR::DebugCounter::debugCounterName(comp(),
                                                                              "illegalWriteReport/put/(%s %s)",
                                                                               comp()->signature(),
                                                                               comp()->getHotnessName(comp()->getMethodHotness())),
                                            reportFinalFieldModification->getNextTreeTop());

      }

   unsafeCall->recursivelyDecReferenceCount();
   return true;
   }


TR::Node *
TR_J9InlinerPolicy::createUnsafeMonitorOp(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * unsafeCall, bool isEnter)
   {
   bool isDirectJNI = unsafeCall->isPreparedForDirectJNI();
   // Expecting directToJNI to have loadaddr children, if not then we had better bail out
   if (isDirectJNI && unsafeCall->getChild(1)->getOpCodeValue() != TR::loadaddr)
      {
      traceMsg(comp(),"Unsafe Inlining: The Unsafe.monitorEnter/Exit() children are not loadaddr's as expected. Not inlining.\n");
      return unsafeCall;
      }

   TR::Node::recreate(callNodeTreeTop->getNode(), TR::NULLCHK);
   callNodeTreeTop->getNode()->setSymbolReference(comp()->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp()->getMethodSymbol()));

   if (isEnter)
      {
      TR::Node::recreate(unsafeCall, TR::monent);
      TR::SymbolReference * monitorEnterSymbolRef = comp()->getSymRefTab()->findOrCreateMonitorEntrySymbolRef(comp()->getMethodSymbol());
      unsafeCall->setSymbolReference(monitorEnterSymbolRef);
      }
   else
      {
      TR::Node::recreate(unsafeCall, TR::monexit);
      TR::SymbolReference * monitorExitSymbolRef = comp()->getSymRefTab()->findOrCreateMonitorExitSymbolRef(comp()->getMethodSymbol());
      unsafeCall->setSymbolReference(monitorExitSymbolRef);
      }

   TR::Node *oldChild = unsafeCall->getChild(0);
   // Anchor the unused oldChild
   callNodeTreeTop->insertBefore(TR::TreeTop::create(comp(), TR::Node::create(oldChild, TR::treetop, 1, oldChild)));
   if (isDirectJNI)
      {
      TR::Node::recreate(unsafeCall->getChild(1), TR::aload);
      }
   unsafeCall->setChild(0, unsafeCall->getChild(1));
   oldChild->recursivelyDecReferenceCount();
   unsafeCall->setChild(1, NULL);
   unsafeCall->setNumChildren(1);

   if (!comp()->getOption(TR_DisableLiveMonitorMetadata))
      {
      TR::Node *storeNode = NULL;
      if (isEnter)
         {
         TR::SymbolReference * tempSymRef = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), TR::Address);
         comp()->addAsMonitorAuto(tempSymRef, false);
         storeNode = TR::Node::createStore(tempSymRef, unsafeCall->getChild(0));
         }
      else
         {
         storeNode = TR::Node::create(unsafeCall,TR::monexitfence,0);
         }


      TR::TreeTop *storeTree = TR::TreeTop::create(comp(), storeNode);
      callNodeTreeTop->insertTreeTopsBeforeMe(storeTree);
      }

   comp()->getMethodSymbol()->setMayContainMonitors(true);
   return unsafeCall;
   }

bool
TR_J9InlinerPolicy::createUnsafeCASCallDiamond( TR::TreeTop *callNodeTreeTop, TR::Node *callNode)
   {
   // This method is used to create an if diamond around a call to any of the unsafe compare and swap methods
   // Codegens have a fast path for the compare and swaps, but cannot deal with the case where the offset value passed in to a the CAS is low tagged
   // (A low tagged offset value means the object being passed in is a java/lang/Class object, and we want a static field)

   // This method assumes the offset node is of type long, and is the second child of the unsafe call.
   TR_InlinerDelimiter delimiter(tracer(),"createUnsafeCASCallDiamond");
   debugTrace(tracer(),"Transforming unsafe callNode = %p",callNode);

   createTempsForUnsafeCall(callNodeTreeTop, callNode);

   TR::Node *offsetNode = callNode->getChild(2);

   TR::TreeTop *compareTree = genClassCheckForUnsafeGetPut(offsetNode);

   // genClassCheck generates a ifcmpne offset&mask 1, meaning if it IS lowtagged (ie offset&mask == 1), the branch will be taken
   TR::TreeTop *ifTree = TR::TreeTop::create(comp(),callNodeTreeTop->getNode()->duplicateTree());
   ifTree->getNode()->getFirstChild()->setIsSafeForCGToFastPathUnsafeCall(true);


   TR::TreeTop *elseTree = TR::TreeTop::create(comp(),callNodeTreeTop->getNode()->duplicateTree());


   ifTree->getNode()->getFirstChild()->setVisitCount(_inliner->getVisitCount());
   elseTree->getNode()->getFirstChild()->setVisitCount(_inliner->getVisitCount());


   debugTrace(tracer(),"ifTree = %p elseTree = %p",ifTree->getNode(),elseTree->getNode());



   // the call itself may be commoned, so we need to create a temp for the callnode itself
   TR::SymbolReference *newSymbolReference = 0;
   TR::DataType dataType = callNode->getDataType();
   if(callNode->getReferenceCount() > 1)
      {
      newSymbolReference = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), dataType);
      TR::Node::recreate(callNode, comp()->il.opCodeForDirectLoad(dataType));
      callNode->setSymbolReference(newSymbolReference);
      callNode->removeAllChildren();

      debugTrace(tracer(),"Unsafe call has refcount > 1.  Replacing callnode with a load of symref %d",newSymbolReference->getReferenceNumber());
      }




   TR::Block *callBlock = callNodeTreeTop->getEnclosingBlock();

   callBlock->createConditionalBlocksBeforeTree(callNodeTreeTop,compareTree, ifTree, elseTree, comp()->getFlowGraph(),false,false);

   // the original call will be deleted by createConditionalBlocksBeforeTree, but if the refcount was > 1, we need to insert stores.

   if (newSymbolReference)
      {
      TR::Node *ifStoreNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(dataType), 1, 1, ifTree->getNode()->getFirstChild(), newSymbolReference);
      TR::TreeTop *ifStoreTree = TR::TreeTop::create(comp(), ifStoreNode);

      ifTree->insertAfter(ifStoreTree);

      debugTrace(tracer(),"Inserted store tree %p for if side of the diamond",ifStoreNode);

      TR::Node *elseStoreNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(dataType), 1, 1, elseTree->getNode()->getFirstChild(), newSymbolReference);
      TR::TreeTop *elseStoreTree = TR::TreeTop::create(comp(), elseStoreNode);

      elseTree->insertAfter(elseStoreTree);

      debugTrace(tracer(),"Inserted store tree %p for else side of the diamond",elseStoreNode);

      }



   return true;
   }



bool
TR_J9InlinerPolicy::createUnsafeGetWithOffset(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * unsafeCall, TR::DataType type, bool isVolatile, bool needNullCheck)
   {
   if (isVolatile && type == TR::Int64 && comp()->target().is32Bit() && !comp()->cg()->getSupportsInlinedAtomicLongVolatiles())
      return false;
   TR_ASSERT(TR::Compiler->cls.classesOnHeap(), "Unsafe inlining code assumes classes are on heap\n");

   if (debug("traceUnsafe"))
      printf("createUnsafeGetWithOffset %s in %s\n", type.toString(), comp()->signature());

   // Truncate the return before inlining the call
   if (TR_J9MethodBase::isUnsafeGetPutBoolean(calleeSymbol->getRecognizedMethod()))
      {
      TR::TransformUtil::truncateBooleanForUnsafeGetPut(comp(), callNodeTreeTop);
      }

   // Preserve null check on the unsafe object
   TR::TransformUtil::separateNullCheck(comp(), callNodeTreeTop, tracer()->debugLevel());

   TR::Node *unsafeAddress = unsafeCall->getChild(1);
   TR::Node *offset = unsafeCall->getChild(2);

   TR::TreeTop *prevTreeTop = callNodeTreeTop->getPrevTreeTop();
   TR::SymbolReference *newSymbolReferenceForAddress = NULL;

   // Since the block has to be split, we need to create temps for the arguments to the call
   // so that the right values are picked up in the 2 blocks that are targets of the if block
   // created for the inlining of the unsafe method
   //
   createTempsForUnsafePutGet(unsafeAddress, unsafeCall, callNodeTreeTop,
                              offset, newSymbolReferenceForAddress, true);
   unsafeAddress = createUnsafeAddressWithOffset(unsafeCall);

   for (int32_t j=0; j<unsafeCall->getNumChildren(); j++)
      unsafeCall->getChild(j)->recursivelyDecReferenceCount();
   unsafeCall->setNumChildren(1);

   TR::SymbolReference* symRef = comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(type, true, false, isVolatile);
   bool conversionNeeded = comp()->fe()->dataTypeForLoadOrStore(type) != type;
   TR_ASSERT(unsafeCall == callNodeTreeTop->getNode()->getFirstChild(), "assumption not valid\n");
   TR::Node* unsafeCallWithConversion = NULL;
   TR::Node* callNodeWithConversion = NULL;
   if (conversionNeeded)
      {
      TR::Node* loadNode = TR::Node::createWithSymRef(comp()->il.opCodeForIndirectArrayLoad(type),
                                          1, 1, unsafeAddress, symRef);

      bool unsignedType;

      switch (calleeSymbol->getRecognizedMethod()) {
      //boolean and char are unsigned so we need an unsigned conversion
      case TR::sun_misc_Unsafe_getBoolean_jlObjectJ_Z:
      case TR::sun_misc_Unsafe_putBooleanVolatile_jlObjectJZ_V:
      case TR::sun_misc_Unsafe_getBooleanVolatile_jlObjectJ_Z:

      case TR::sun_misc_Unsafe_getChar_jlObjectJ_C:
      case TR::sun_misc_Unsafe_getCharVolatile_jlObjectJ_C:
      case TR::sun_misc_Unsafe_getChar_J_C:
         unsignedType = true;
         break;
      //byte and short are signed so we need a signed conversion
      case TR::sun_misc_Unsafe_getByte_jlObjectJ_B:
      case TR::sun_misc_Unsafe_getByte_J_B:
      case TR::sun_misc_Unsafe_getByteVolatile_jlObjectJ_B:

      case TR::sun_misc_Unsafe_getShort_jlObjectJ_S:
      case TR::sun_misc_Unsafe_getShortVolatile_jlObjectJ_S:
      case TR::sun_misc_Unsafe_getShort_J_S:
         unsignedType = false;
         break;
      default:
         TR_ASSERT(false, "all TR::sun_misc_Unsafe.get* methods must be handled.");
      }

      TR::ILOpCodes conversionOpCode =
         TR::ILOpCode::getProperConversion(type,
                                          comp()->fe()->dataTypeForLoadOrStore(type),
                                          unsignedType);
      unsafeCallWithConversion = TR::Node::create(conversionOpCode, 1, loadNode);
      }

   unsafeAddress->incReferenceCount();
   TR::Node::recreate(unsafeCall, comp()->il.opCodeForIndirectLoad(type));
   unsafeCall->setChild(0, unsafeAddress);
   unsafeCall->setSymbolReference(symRef);

   TR::Node::recreate(callNodeTreeTop->getNode(), TR::treetop);
      TR::DataType dataType = unsafeCall->getDataType();
      TR::SymbolReference *newTemp = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), dataType);
   TR::ILOpCodes storeOpCode = comp()->il.opCodeForDirectStore(unsafeCall->getDataType());
   TR::Node::recreate(callNodeTreeTop->getNode(), storeOpCode);
      callNodeTreeTop->getNode()->setSymbolReference(newTemp);

   if (conversionNeeded)
      {
      callNodeWithConversion =
         TR::Node::createWithSymRef(storeOpCode, 1, 1, unsafeCallWithConversion, newTemp);
      }

   TR::TreeTop* directAccessTreeTop =
      genDirectAccessCodeForUnsafeGetPut(callNodeTreeTop->getNode(), false, true);
   TR::TreeTop* arrayDirectAccessTreeTop = conversionNeeded
      ? genDirectAccessCodeForUnsafeGetPut(callNodeWithConversion, conversionNeeded, true)
      : NULL;
   TR::TreeTop* indirectAccessTreeTop =
      genIndirectAccessCodeForUnsafeGetPut(callNodeTreeTop->getNode(), unsafeAddress);
   // If we are not able to get javaLangClass it is still inefficient to put direct Access far
   // So in that case we will generate lowTagCmpTest to branch to indirect access if true
   TR_OpaqueClassBlock *javaLangClass = comp()->fe()->getClassFromSignature("Ljava/lang/Class;",17, comp()->getCurrentMethod(),true);
   bool needNotLowTagged = javaLangClass != NULL || conversionNeeded ;
   // If we can get a J9Class or we need conversion we generate test to branch to direct access if low bit is not tagged
   // Else in case we get NULL instead of j9Class we generate test to branch to indirect access if low bit is tagged
   TR::TreeTop *lowTagCmpTree = genClassCheckForUnsafeGetPut(offset, needNotLowTagged);

   TR::Block * joinBlock =
      addNullCheckForUnsafeGetPut(unsafeAddress, newSymbolReferenceForAddress,
                                  callNodeTreeTop, directAccessTreeTop,
                                  arrayDirectAccessTreeTop,
                                  indirectAccessTreeTop);

   createAnchorNodesForUnsafeGetPut(directAccessTreeTop, type, true);
   if (arrayDirectAccessTreeTop)
      createAnchorNodesForUnsafeGetPut(arrayDirectAccessTreeTop, type, true);
   genCodeForUnsafeGetPut(unsafeAddress, callNodeTreeTop, prevTreeTop,
                          newSymbolReferenceForAddress, directAccessTreeTop,
                          lowTagCmpTree, needNullCheck, true, conversionNeeded,
                          joinBlock, javaLangClass);

   for (int32_t j=0; j<unsafeCall->getNumChildren(); j++)
         unsafeCall->getChild(j)->recursivelyDecReferenceCount();
   unsafeCall->setNumChildren(0);
   TR::Node::recreate(unsafeCall, comp()->il.opCodeForDirectLoad(unsafeCall->getDataType()));
   unsafeCall->setSymbolReference(newTemp);
   return true;

   }

TR::Node *
TR_J9InlinerPolicy::createUnsafeAddress(TR::Node * unsafeCall)
   {
   if (comp()->target().is64Bit())
      return unsafeCall->getChild(1);  // should use l2a if we ever have one

   return TR::Node::create(TR::l2i, 1, unsafeCall->getChild(1)); // should use i2a if we ever have one
   }

bool
TR_J9InlinerPolicy::createUnsafePut(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * unsafeCall, TR::DataType type, bool compress)
   {
   if (debug("traceUnsafe"))
      printf("createUnsafePut %s in %s\n", type.toString(), comp()->signature());

   // Preserve null check on the unsafe object
   TR::TransformUtil::separateNullCheck(comp(), callNodeTreeTop, tracer()->debugLevel());

   TR::Node * address = createUnsafeAddress(unsafeCall);

   TR::Node * value = unsafeCall->getChild(2);

   TR::Node * unsafeNode;
   if (type == TR::Address)
      {
      if (comp()->target().is64Bit())
         {
         unsafeNode = TR::Node::createWithSymRef(TR::lstorei, 2, 2, address, value, comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(TR::Int64));
         }
      else
         {
         unsafeNode = TR::Node::create(TR::l2i, 1, value);
         unsafeNode = TR::Node::createWithSymRef(TR::istorei, 2, 2, address, unsafeNode, comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(TR::Int32));
         }
      }
   else
      {
      switch (type)
         {
         case TR::Int8:
            value = TR::Node::create(TR::i2b, 1, value);
            break;
         case TR::Int16:
            value = TR::Node::create(TR::i2s, 1, value);
            break;
         default:
          break;
         }
      unsafeNode = TR::Node::createWithSymRef(comp()->il.opCodeForIndirectArrayStore(type), 2, 2, address, value, comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(type));
      }

   if (compress && comp()->useCompressedPointers() &&
         (type == TR::Address))
      {
      unsafeNode = genCompressedRefs(unsafeNode, false, -1);
      }

   callNodeTreeTop->setNode(unsafeNode);
   unsafeCall->recursivelyDecReferenceCount();

   return true;

   }

bool
TR_J9InlinerPolicy::createUnsafeGet(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * unsafeCall, TR::DataType type, bool compress)
   {
   if (debug("traceUnsafe"))
      printf("createUnsafeGet %s in %s\n", type.toString(), comp()->signature());

   // Preserve null check on the unsafe object
   TR::TransformUtil::separateNullCheck(comp(), callNodeTreeTop, tracer()->debugLevel());

   TR::Node * unsafeAddress = createUnsafeAddress(unsafeCall);

   TR::Node * unsafeNode;
   if (type == TR::Address)
      {
      if (comp()->target().is64Bit())
         {
         unsafeAddress->incReferenceCount();

         int32_t j;
         for (j=0; j<unsafeCall->getNumChildren(); j++)
            unsafeCall->getChild(j)->recursivelyDecReferenceCount();

         unsafeCall->setNumChildren(1);
         TR::Node::recreate(unsafeCall, TR::lloadi);
         unsafeCall->setSymbolReference(comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(TR::Int64));
         unsafeCall->setChild(0, unsafeAddress);
         unsafeNode = unsafeCall;
         }
      else
         {
         unsafeNode = TR::Node::createWithSymRef(TR::iloadi, 1, 1, unsafeAddress, comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(TR::Int32));

         unsafeNode->incReferenceCount();

         int32_t j;
         for (j=0; j<unsafeCall->getNumChildren(); j++)
            unsafeCall->getChild(j)->recursivelyDecReferenceCount();

         unsafeCall->setNumChildren(1);
         TR::Node::recreate(unsafeCall, TR::iu2l);
         unsafeCall->setChild(0, unsafeNode);
         }
      }
   else
      {
      bool unsignedType = false;
      bool needConversion = false;
      switch (type)
         {
         case TR::Int8:
         case TR::Int16:
            needConversion = true;
            break;
         default:
          break;
         }

      switch (calleeSymbol->getRecognizedMethod())
         {
         case TR::sun_misc_Unsafe_getChar_J_C:
            unsignedType = true;
            break;
         default:
          break;
         }

      if (needConversion)
         unsafeNode = TR::Node::createWithSymRef(comp()->il.opCodeForIndirectArrayLoad(type), 1, 1, unsafeAddress, comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(type));
      else
         unsafeNode = unsafeAddress;

      unsafeNode->incReferenceCount();

      int32_t j;
      for (j=0; j<unsafeCall->getNumChildren(); j++)
         unsafeCall->getChild(j)->recursivelyDecReferenceCount();

      unsafeCall->setNumChildren(1);

      switch (type)
         {
         case TR::Int8:
            TR::Node::recreate(unsafeCall, TR::b2i);
            break;
         case TR::Int16:
            if(unsignedType)
               TR::Node::recreate(unsafeCall, TR::su2i);
            else
               TR::Node::recreate(unsafeCall, TR::s2i);
            break;
         default:
          break;
         }

      if (!needConversion)
         {
         TR::Node::recreate(unsafeCall, comp()->il.opCodeForIndirectArrayLoad(type));
         unsafeCall->setSymbolReference(comp()->getSymRefTab()->findOrCreateUnsafeSymbolRef(type));
         }

      unsafeCall->setChild(0, unsafeNode);
      }

   TR::TreeTop *compRefTT = NULL;
   if (compress && comp()->useCompressedPointers() &&
         (type == TR::Address))
      {
      // create the anchor node
      compRefTT = TR::TreeTop::create(comp(), genCompressedRefs(unsafeCall, false));
      }

   if (compRefTT)
      {
      TR::TreeTop *prevTT = callNodeTreeTop->getPrevTreeTop();
      prevTT->join(compRefTT);
      }

   TR::Node::recreate(callNodeTreeTop->getNode(), TR::treetop);

   return true;

   }

bool
TR_J9InlinerPolicy::createUnsafeFence(TR::TreeTop *callNodeTreeTop, TR::Node *callNode, TR::ILOpCodes opCode)
   {
   TR::Node *fenceNode = TR::Node::createWithSymRef(callNode, opCode, 0, callNode->getSymbolReference());
   TR::Node::recreate(callNode, TR::PassThrough);
   TR::TreeTop *fenceTop = TR::TreeTop::create(comp(), fenceNode);
   callNodeTreeTop->insertAfter(fenceTop);
   return true;
   }

TR::Node *
TR_J9InlinerPolicy::inlineGetClassAccessFlags(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * callNode)
   {
   if (
         comp()->getOption(TR_DisableInliningOfNatives) ||
         calleeSymbol->castToMethodSymbol()->getRecognizedMethod() != TR::sun_reflect_Reflection_getClassAccessFlags)
      return 0;

   TR::Block * block = callNodeTreeTop->getEnclosingBlock();

   TR::SymbolReference * modifiersSymRef = comp()->getSymRefTab()->createTemporary(callerSymbol, callNode->getDataType());

   // generating "modifiers = J9VM_J9CLASS_FROM_HEAPCLASS(vmThread, *(j9object_t*)clazzRef)->romClass->modifiers;"

   TR::Node *j9cNode;

   if(callNode->isPreparedForDirectJNI())
      j9cNode = callNode->getSecondChild();
   else
      j9cNode = callNode->getFirstChild();

   TR::Node::recreate(j9cNode, TR::aload);

   if (TR::Compiler->cls.classesOnHeap())
      {
      j9cNode = TR::Node::createWithSymRef(TR::aloadi, 1, 1, j9cNode, comp()->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef());
      }

   TR::Node *nullCheckNode = TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, j9cNode, comp()->getSymRefTab()->findOrCreateNullCheckSymbolRef(callerSymbol));
   TR::TreeTop *nullCheckTree = TR::TreeTop::create(comp(), nullCheckNode);
   TR::Node *romclassNode = TR::Node::createWithSymRef(TR::aloadi, 1, 1, j9cNode, comp()->getSymRefTab()->findOrCreateClassRomPtrSymbolRef());
   TR::Node *classAccessFlagsNode = TR::Node::createWithSymRef(TR::iloadi, 1, 1, romclassNode, comp()->getSymRefTab()->findOrCreateClassIsArraySymbolRef());
   TR::Node *modifiersNode = TR::Node::createStore(modifiersSymRef, classAccessFlagsNode);
   TR::TreeTop *modifiersTree = TR::TreeTop::create(comp(), modifiersNode);
   callNodeTreeTop->insertBefore(modifiersTree);
   modifiersTree->insertBefore(nullCheckTree);

   /*** need to generate this:
    *  if (modifiers & J9AccClassInternalPrimitiveType) {
    *    modifiers = J9AccAbstract | J9AccFinal | J9AccPublic;
    *  } else {
    *    modifiers &= 0xFFF;
    *  }
    *  return modifiers;
    */
   // generating "if (modifiers & J9AccClassInternalPrimitiveType)"
   TR::Node *iAndNode = TR::Node::create(TR::iand, 2,
                                       TR::Node::createLoad(callNode, modifiersSymRef),
                                       TR::Node::iconst(callNode, (int32_t)comp()->fej9()->constClassFlagsPrimitive()));
   TR::Node *compareNode = TR::Node::createif(TR::ificmpne,
                          iAndNode,
                          TR::Node::iconst(callNode, 0),
                          0);
   TR::TreeTop *compareTree = TR::TreeTop::create(comp(), compareNode);

   // generating if-then part "   modifiers = J9AccAbstract | J9AccFinal | J9AccPublic;"
   TR::Node *modifiersIfStrNode = TR::Node::createStore(modifiersSymRef,
                                 TR::Node::iconst(callNode, (int32_t)(comp()->fej9()->constClassFlagsAbstract() | comp()->fej9()->constClassFlagsFinal() | comp()->fej9()->constClassFlagsPublic()))
                                                     );
   TR::TreeTop *ifTree = TR::TreeTop::create(comp(), modifiersIfStrNode);


   // generating else part "   modifiers &= 0xFFF;"
   TR::Node *modifiersIAndNode = TR::Node::create(TR::iand, 2,
                                TR::Node::createLoad(callNode, modifiersSymRef),
                                TR::Node::iconst(callNode, 0xFFF));
   TR::Node *modifiersElseStrNode = TR::Node::createStore(modifiersSymRef, modifiersIAndNode);
   TR::TreeTop *elseTree = TR::TreeTop::create(comp(), modifiersElseStrNode);

   // generating "   return modifiers;"
   // - simply convert the original call node to an iload of the modifiers
   TR::Node *resultNode = callNode;
   TR::Node::recreate(callNode, TR::iload);
   callNode->removeAllChildren();
   callNode->setSymbolReference(modifiersSymRef);

   block->createConditionalBlocksBeforeTree(callNodeTreeTop, compareTree, ifTree, elseTree, callerSymbol->getFlowGraph(), false);

   return resultNode;

   }

bool
TR_J9InlinerPolicy::inlineUnsafeCall(TR::ResolvedMethodSymbol *calleeSymbol, TR::ResolvedMethodSymbol *callerSymbol, TR::TreeTop * callNodeTreeTop, TR::Node * callNode)
   {
   debugTrace(tracer(), "Unsafe Inlining: Trying to inline Unsafe Call at Node %p\n", callNode);

   if (comp()->getOption(TR_DisableUnsafe))
      return false;

   if (!callNode->getSymbol()->isResolvedMethod())
      return false;

   if (comp()->fej9()->isAnyMethodTracingEnabled(calleeSymbol->getResolvedMethod()->getPersistentIdentifier()) &&
       !comp()->fej9()->traceableMethodsCanBeInlined())
      return false;

   if ((TR::Compiler->vm.canAnyMethodEventsBeHooked(comp()) && !comp()->fej9()->methodsCanBeInlinedEvenIfEventHooksEnabled(comp())) ||
       (comp()->fej9()->isAnyMethodTracingEnabled(calleeSymbol->getResolvedMethod()->getPersistentIdentifier()) &&
       !comp()->fej9()->traceableMethodsCanBeInlined()))
      return false;

   // I am not sure if having the same type between C/S and B/Z matters here.. ie. if the type is being used as the only distinguishing factor
   switch (callNode->getSymbol()->castToResolvedMethodSymbol()->getRecognizedMethod())
      {
      case TR::sun_misc_Unsafe_putByte_jlObjectJB_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false);
      case TR::sun_misc_Unsafe_putBoolean_jlObjectJZ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false);
      case TR::sun_misc_Unsafe_putChar_jlObjectJC_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false);
      case TR::sun_misc_Unsafe_putShort_jlObjectJS_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false);
      case TR::sun_misc_Unsafe_putInt_jlObjectJI_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32, false);
      case TR::sun_misc_Unsafe_putLong_jlObjectJJ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64, false);
      case TR::sun_misc_Unsafe_putFloat_jlObjectJF_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float, false);
      case TR::sun_misc_Unsafe_putDouble_jlObjectJD_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double, false);
      case TR::sun_misc_Unsafe_putObject_jlObjectJjlObject_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, false, true);

      case TR::sun_misc_Unsafe_getBoolean_jlObjectJ_Z:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false);
      case TR::sun_misc_Unsafe_getByte_jlObjectJ_B:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false);
      case TR::sun_misc_Unsafe_getChar_jlObjectJ_C:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false);
      case TR::sun_misc_Unsafe_getShort_jlObjectJ_S:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false);
      case TR::sun_misc_Unsafe_getInt_jlObjectJ_I:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32, false);
      case TR::sun_misc_Unsafe_getLong_jlObjectJ_J:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64, false);
      case TR::sun_misc_Unsafe_getFloat_jlObjectJ_F:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float, false);
      case TR::sun_misc_Unsafe_getDouble_jlObjectJ_D:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double, false);
      case TR::sun_misc_Unsafe_getObject_jlObjectJ_jlObject:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, false, true);

      case TR::sun_misc_Unsafe_putByteVolatile_jlObjectJB_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, true);
      case TR::sun_misc_Unsafe_putBooleanVolatile_jlObjectJZ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, true);
      case TR::sun_misc_Unsafe_putCharVolatile_jlObjectJC_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, true);
      case TR::sun_misc_Unsafe_putShortVolatile_jlObjectJS_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, true);
      case TR::sun_misc_Unsafe_putIntVolatile_jlObjectJI_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32, true);
      case TR::sun_misc_Unsafe_putLongVolatile_jlObjectJJ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64, true);
      case TR::sun_misc_Unsafe_putFloatVolatile_jlObjectJF_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float, true);
      case TR::sun_misc_Unsafe_putDoubleVolatile_jlObjectJD_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double, true);
      case TR::sun_misc_Unsafe_putObjectVolatile_jlObjectJjlObject_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, true, true);

      case TR::sun_misc_Unsafe_monitorEnter_jlObject_V:
         return createUnsafeMonitorOp(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, true);
      case TR::sun_misc_Unsafe_monitorExit_jlObject_V:
         return createUnsafeMonitorOp(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, false);

      case TR::sun_misc_Unsafe_putByteOrdered_jlObjectJB_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false, false, true);
      case TR::sun_misc_Unsafe_putBooleanOrdered_jlObjectJZ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, false, false, true);
      case TR::sun_misc_Unsafe_putCharOrdered_jlObjectJC_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false, false, true);
      case TR::sun_misc_Unsafe_putShortOrdered_jlObjectJS_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, false, false, true);
      case TR::sun_misc_Unsafe_putIntOrdered_jlObjectJI_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32, false, false, true);
      case TR::sun_misc_Unsafe_putLongOrdered_jlObjectJJ_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64, false, false, true);
      case TR::sun_misc_Unsafe_putFloatOrdered_jlObjectJF_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float, false, false, true);
      case TR::sun_misc_Unsafe_putDoubleOrdered_jlObjectJD_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double, false, false, true);
      case TR::sun_misc_Unsafe_putObjectOrdered_jlObjectJjlObject_V:
         return createUnsafePutWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, false, true, true);

      case TR::sun_misc_Unsafe_getBooleanVolatile_jlObjectJ_Z:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, true);
      case TR::sun_misc_Unsafe_getByteVolatile_jlObjectJ_B:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8, true);
      case TR::sun_misc_Unsafe_getCharVolatile_jlObjectJ_C:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, true);
      case TR::sun_misc_Unsafe_getShortVolatile_jlObjectJ_S:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16, true);
      case TR::sun_misc_Unsafe_getIntVolatile_jlObjectJ_I:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32, true);
      case TR::sun_misc_Unsafe_getLongVolatile_jlObjectJ_J:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64, true);
      case TR::sun_misc_Unsafe_getFloatVolatile_jlObjectJ_F:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float, true);
      case TR::sun_misc_Unsafe_getDoubleVolatile_jlObjectJ_D:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double, true);
      case TR::sun_misc_Unsafe_getObjectVolatile_jlObjectJ_jlObject:
         return createUnsafeGetWithOffset(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, true, true);

      case TR::sun_misc_Unsafe_putByte_JB_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putByte_JB_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8);
      case TR::sun_misc_Unsafe_putChar_JC_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16);
      case TR::sun_misc_Unsafe_putShort_JS_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putShort_JS_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16);
      case TR::sun_misc_Unsafe_putInt_JI_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putInt_JI_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32);
      case TR::sun_misc_Unsafe_putLong_JJ_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putLong_JJ_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64);
      case TR::sun_misc_Unsafe_putFloat_JF_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putFloat_JF_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float);
      case TR::sun_misc_Unsafe_putDouble_JD_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putDouble_JD_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double);
      case TR::sun_misc_Unsafe_putAddress_JJ_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putAddress_JJ_V:
         return createUnsafePut(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, false);

      case TR::sun_misc_Unsafe_getByte_J_B:
      case TR::org_apache_harmony_luni_platform_OSMemory_getByte_J_B:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int8);
      case TR::sun_misc_Unsafe_getChar_J_C:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16);
      case TR::sun_misc_Unsafe_getShort_J_S:
      case TR::org_apache_harmony_luni_platform_OSMemory_getShort_J_S:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int16);
      case TR::sun_misc_Unsafe_getInt_J_I:
      case TR::org_apache_harmony_luni_platform_OSMemory_getInt_J_I:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int32);
      case TR::sun_misc_Unsafe_getLong_J_J:
      case TR::org_apache_harmony_luni_platform_OSMemory_getLong_J_J:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Int64);
      case TR::sun_misc_Unsafe_getFloat_J_F:
      case TR::org_apache_harmony_luni_platform_OSMemory_getFloat_J_F:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Float);
      case TR::sun_misc_Unsafe_getDouble_J_D:
      case TR::org_apache_harmony_luni_platform_OSMemory_getDouble_J_D:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Double);
      case TR::sun_misc_Unsafe_getAddress_J_J:
      case TR::org_apache_harmony_luni_platform_OSMemory_getAddress_J_J:
         return createUnsafeGet(calleeSymbol, callerSymbol, callNodeTreeTop, callNode, TR::Address, false);

      case TR::sun_misc_Unsafe_loadFence:
         return createUnsafeFence(callNodeTreeTop, callNode, TR::loadFence);
      case TR::sun_misc_Unsafe_storeFence:
         return createUnsafeFence(callNodeTreeTop, callNode, TR::storeFence);
      case TR::sun_misc_Unsafe_fullFence:
         return createUnsafeFence(callNodeTreeTop, callNode, TR::fullFence);

      case TR::sun_misc_Unsafe_staticFieldBase:
         return false; // todo
      case TR::sun_misc_Unsafe_staticFieldOffset:
         return false; // todo
      case TR::sun_misc_Unsafe_objectFieldOffset:
         return false; // todo

      case TR::sun_misc_Unsafe_compareAndSwapInt_jlObjectJII_Z:
      case TR::sun_misc_Unsafe_compareAndSwapLong_jlObjectJJJ_Z:
      case TR::sun_misc_Unsafe_compareAndSwapObject_jlObjectJjlObjectjlObject_Z:
         if (callNode->isSafeForCGToFastPathUnsafeCall())
            return false;
         switch (callerSymbol->castToMethodSymbol()->getRecognizedMethod())
            {
            case TR::java_util_concurrent_ConcurrentHashMap_addCount:
            case TR::java_util_concurrent_ConcurrentHashMap_casTabAt:
            case TR::java_util_concurrent_ConcurrentHashMap_fullAddCount:
            case TR::java_util_concurrent_ConcurrentHashMap_helpTransfer:
            case TR::java_util_concurrent_ConcurrentHashMap_initTable:
            case TR::java_util_concurrent_ConcurrentHashMap_transfer:
            case TR::java_util_concurrent_ConcurrentHashMap_tryPresize:
            case TR::java_util_concurrent_ConcurrentHashMap_TreeBin_contendedLock:
            case TR::java_util_concurrent_ConcurrentHashMap_TreeBin_find:
            case TR::java_util_concurrent_ConcurrentHashMap_TreeBin_lockRoot:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapIntInObject:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapLongInObject:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapObjectInObject:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapIntInArray:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapLongInArray:
            case TR::com_ibm_jit_JITHelpers_compareAndSwapObjectInArray:
               callNode->setIsSafeForCGToFastPathUnsafeCall(true);
               return callNode;
            default:
               return createUnsafeCASCallDiamond(callNodeTreeTop, callNode);
            }
      default:
         break;
      }

   return false;
   }


bool
TR_J9InlinerPolicy::isInlineableJNI(TR_ResolvedMethod *method,TR::Node *callNode)
   {
   TR::Compilation* comp = TR::comp();
   TR::RecognizedMethod recognizedMethod = method->getRecognizedMethod();
   // Reflection's JNI
   //
   if (!comp->getOption(TR_DisableInliningOfNatives) &&
         recognizedMethod == TR::sun_reflect_Reflection_getClassAccessFlags)
      //return false;
      return true;

   // Unsafe's JNIs
   //
   if (comp->getOption(TR_DisableUnsafe))
      return false;

   // If this put ordered call node has already been inlined, do not inline it again (JTC-JAT 71313)
   if (callNode && callNode->isUnsafePutOrderedCall() && callNode->isDontInlinePutOrderedCall())
      {
      debugTrace(tracer(), "Unsafe Inlining: Unsafe Call %p already inlined\n", callNode);

      return false;
      }

   if ((TR::Compiler->vm.canAnyMethodEventsBeHooked(comp) && !comp->fej9()->methodsCanBeInlinedEvenIfEventHooksEnabled(comp)) ||
       (comp->fej9()->isAnyMethodTracingEnabled(method->getPersistentIdentifier()) &&
        !comp->fej9()->traceableMethodsCanBeInlined()))
      return false;

   if (method->convertToMethod()->isUnsafeWithObjectArg(comp) || method->convertToMethod()->isUnsafeCAS(comp))
      {
      // In Java9 sun/misc/Unsafe methods are simple Java wrappers to JNI
      // methods in jdk.internal, and the enum values above match both. Only
      // return true for the methods that are native.
      if (!TR::Compiler->om.canGenerateArraylets() || (callNode && callNode->isUnsafeGetPutCASCallOnNonArray()))
         return method->isNative();
      else
         return false;
      }

   switch (recognizedMethod)
      {

      case TR::sun_misc_Unsafe_monitorEnter_jlObject_V:
      case TR::sun_misc_Unsafe_monitorExit_jlObject_V:

      case TR::sun_misc_Unsafe_putByte_JB_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putByte_JB_V:
      case TR::sun_misc_Unsafe_putChar_JC_V:
      case TR::sun_misc_Unsafe_putShort_JS_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putShort_JS_V:
      case TR::sun_misc_Unsafe_putInt_JI_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putInt_JI_V:
      case TR::sun_misc_Unsafe_putLong_JJ_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putLong_JJ_V:
      case TR::sun_misc_Unsafe_putFloat_JF_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putFloat_JF_V:
      case TR::sun_misc_Unsafe_putDouble_JD_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putDouble_JD_V:
      case TR::sun_misc_Unsafe_putAddress_JJ_V:
      case TR::org_apache_harmony_luni_platform_OSMemory_putAddress_JJ_V:

      case TR::sun_misc_Unsafe_getByte_J_B:
      case TR::org_apache_harmony_luni_platform_OSMemory_getByte_J_B:
      case TR::sun_misc_Unsafe_getChar_J_C:
      case TR::sun_misc_Unsafe_getShort_J_S:
      case TR::org_apache_harmony_luni_platform_OSMemory_getShort_J_S:
      case TR::sun_misc_Unsafe_getInt_J_I:
      case TR::org_apache_harmony_luni_platform_OSMemory_getInt_J_I:
      case TR::sun_misc_Unsafe_getLong_J_J:
      case TR::org_apache_harmony_luni_platform_OSMemory_getLong_J_J:
      case TR::sun_misc_Unsafe_getFloat_J_F:
      case TR::org_apache_harmony_luni_platform_OSMemory_getFloat_J_F:
      case TR::sun_misc_Unsafe_getDouble_J_D:
      case TR::org_apache_harmony_luni_platform_OSMemory_getDouble_J_D:
      case TR::sun_misc_Unsafe_getAddress_J_J:
      case TR::org_apache_harmony_luni_platform_OSMemory_getAddress_J_J:

      case TR::sun_misc_Unsafe_loadFence:
      case TR::sun_misc_Unsafe_storeFence:
      case TR::sun_misc_Unsafe_fullFence:
         return true;

      case TR::sun_misc_Unsafe_staticFieldBase:
         return false; // todo
      case TR::sun_misc_Unsafe_staticFieldOffset:
         return false; // todo
      case TR::sun_misc_Unsafe_objectFieldOffset:
         return false; // todo

      default:
         break;
      }

   return false;
   }

//first check J9 specific tryToInline methods and then general tryToInline methods
bool
TR_J9InlinerPolicy::tryToInline(TR_CallTarget * calltarget, TR_CallStack * callStack, bool toInline)
   {
   TR_ResolvedMethod *method = calltarget->_calleeMethod;

   if (toInline && insideIntPipelineForEach(method, comp()))
      {
      if (comp()->trace(OMR::inlining))
         traceMsg(comp(), "forcing inlining of IntPipelineForEach or method inside it: %s\n", method->signature(comp()->trMemory()));

      return true;
      }

   if (OMR_InlinerPolicy::tryToInlineGeneral(calltarget, callStack, toInline))
      return true;

   return false;
   }

bool
TR_J9InlinerPolicy::inlineMethodEvenForColdBlocks(TR_ResolvedMethod *method)
   {
   bool insideForEach = insideIntPipelineForEach(method, comp());
   return insideForEach;
   }

void
TR_J9InlinerPolicy::adjustFanInSizeInWeighCallSite(int32_t& weight,
                                                int32_t size,
                                                TR_ResolvedMethod* callee,
                                                TR_ResolvedMethod* caller,
                                                int32_t bcIndex)
   {
      /*
      Our goal is to use the ratio of the weight of a particular caller to the total weight to penalize the callers whose weights are relatively small.
      To reach that goal, we have to introduce two magic numbers: defaultWeight and TR::Options::INLINE_fanInCallGraphFactor.
         *defaultWeight is used when our caller belongs in the other bucket, so we don't have a meaningful weight to represent it.
         *INLINE_fanInCallGraphFactor is simply hand-tuned number by which we multiply our ratio.

      INLINE_fanInCallGraphFactor is an integer number divided by 100. This allows us to avoid using float numbers for specifying the factor.
      */



      if (comp()->getMethodHotness() > warm)
         return;

      static const char *qq = feGetEnv("TR_Min_FanIn_Size");
      static const uint32_t min_size = ( qq ) ? atoi(qq) : MIN_FAN_IN_SIZE;

      uint32_t thresholdSize = (!comp()->getOption(TR_InlinerFanInUseCalculatedSize)) ? getJ9InitialBytecodeSize(callee, 0, comp()) : size;
      if (thresholdSize <= min_size)  // if we are less than min_fan_in size, we don't want to apply fan-in heuristic
         {
         return;
         }

      static const char *qqq = feGetEnv("TR_OtherBucketThreshold");
      static const float otherBucketThreshold = (qqq) ? (float)  (atoi (qqq) /100.0) : FANIN_OTHER_BUCKET_THRESHOLD ;

      //convenience
      TR_ResolvedJ9Method *resolvedJ9Callee = (TR_ResolvedJ9Method *) callee;
      TR_ResolvedJ9Method *resolvedJ9Caller = (TR_ResolvedJ9Method *) caller;


      uint32_t numCallers = 0, totalWeight = 0, fanInWeight = 0, otherBucketWeight = 0;
      resolvedJ9Callee->getFaninInfo(&numCallers, &totalWeight, &otherBucketWeight);

      if (numCallers < MIN_NUM_CALLERS || (totalWeight > 0 && otherBucketWeight * 1.0 / totalWeight < otherBucketThreshold))
         return;

      bool hasCaller = resolvedJ9Callee->getCallerWeight(resolvedJ9Caller, &fanInWeight, bcIndex);

      if (size >= 0 && totalWeight && fanInWeight)
         {
         static const char *q4 = feGetEnv("TR_MagicNumber");
         static const int32_t magicNumber = q4 ? atoi (q4) : 1 ;

         float dynamicFanInRatio = hasCaller ? ((float)totalWeight - (float)fanInWeight) / (float) totalWeight : (float) fanInWeight / (float) totalWeight;

         int32_t oldWeight = weight;
         weight += weight*dynamicFanInRatio*magicNumber;

         heuristicTrace (tracer(), "FANIN: callee %s in caller %s @ %d oldWeight %d weight %d",
            callee->signature(comp()->trMemory()),
            caller->signature(comp()->trMemory()),
            bcIndex, oldWeight, weight
         );

         }
   }

bool TR_J9InlinerPolicy::_tryToGenerateILForMethod (TR::ResolvedMethodSymbol* calleeSymbol, TR::ResolvedMethodSymbol* callerSymbol, TR_CallTarget* calltarget)
   {
   bool success = false;
   TR::Node * callNode = calltarget->_myCallSite->_callNode;

   TR::IlGeneratorMethodDetails storage;
   TR::IlGeneratorMethodDetails & ilGenMethodDetails = TR::IlGeneratorMethodDetails::create(storage, calleeSymbol->getResolvedMethod());
   if (!comp()->getOption(TR_DisablePartialInlining) && calltarget->_partialInline)
      {
      heuristicTrace(tracer(),"Doing a partialInline for method %s\n",tracer()->traceSignature(calleeSymbol));
      TR::PartialInliningIlGenRequest ilGenRequest(ilGenMethodDetails, callerSymbol, calltarget->_partialInline);

      if (comp()->trace(OMR::inlining))
         {
         traceMsg(comp(), "ILGen of [%p] using request: ", callNode);
         ilGenRequest.print(comp()->fe(), comp()->getOutFile(), "\n");
         }
      success = calleeSymbol->genIL(comp()->fe(), comp(), comp()->getSymRefTab(), ilGenRequest);
      }
   else
      {
      TR::InliningIlGenRequest ilGenRequest(ilGenMethodDetails, callerSymbol);
      if (comp()->trace(OMR::inlining))
         {
         ilGenRequest.print(comp()->fe(), comp()->getOutFile(), "\n");
         }
      success =  calleeSymbol->genIL(comp()->fe(), comp(), comp()->getSymRefTab(), ilGenRequest);
      }

   return success;
   }

bool TR_J9InlinerPolicy::tryToInlineTrivialMethod (TR_CallStack* callStack, TR_CallTarget* calltarget)
   {
   TR::Node * callNode = calltarget->_myCallSite->_callNode;
   TR::ResolvedMethodSymbol * calleeSymbol = calltarget->_calleeSymbol;
   TR::TreeTop * callNodeTreeTop = calltarget->_myCallSite->_callNodeTreeTop;
   TR_VirtualGuardSelection *guard = calltarget->_guard;
   TR::ResolvedMethodSymbol * callerSymbol = callStack->_methodSymbol;

   if (isInlineableJNI(calleeSymbol->getResolvedMethod(),callNode))
      {
      if (performTransformation(comp(), "%sInlining jni %s into %s\n", OPT_DETAILS, calleeSymbol->signature(comp()->trMemory()), callerSymbol->signature(comp()->trMemory())))
         {
         if (calltarget->_myCallSite->isIndirectCall())
            return true;

         if (inlineGetClassAccessFlags(calleeSymbol, callerSymbol, callNodeTreeTop, callNode))
            guard->_kind = TR_NoGuard;
         else if (inlineUnsafeCall(calleeSymbol, callerSymbol, callNodeTreeTop, callNode))
            guard->_kind = TR_NoGuard;
         }
      return true;
      }

   return false;
   }

bool
TR_J9InlinerPolicy::adjustFanInSizeInExceedsSizeThreshold(int bytecodeSize,
                                                      uint32_t& calculatedSize,
                                                      TR_ResolvedMethod* callee,
                                                      TR_ResolvedMethod* caller,
                                                      int32_t bcIndex)
   {
     if (comp()->getMethodHotness() > warm)
         return false;

   static const char *q = feGetEnv("TR_SizeMultiplier");
   static const uint32_t multiplier = ( q ) ? atoi (q) : SIZE_MULTIPLIER;

   static const char *qq = feGetEnv("TR_Min_FanIn_Size");
   static const uint32_t min_size = ( qq ) ? atoi(qq) : MIN_FAN_IN_SIZE;

   static const char *qqq = feGetEnv("TR_OtherBucketThreshold");
   static const float otherBucketThreshold = (qqq) ? (float) (atoi (qqq) /100.0) : FANIN_OTHER_BUCKET_THRESHOLD;


   uint32_t thresholdSize = (!comp()->getOption(TR_InlinerFanInUseCalculatedSize)) ? getJ9InitialBytecodeSize(callee, 0, comp()) : calculatedSize;
   if (thresholdSize <= min_size)  // if we are less than min_fan_in size, we don't want to apply fan-in heuristic
      {
      return false;
      }

   TR_ResolvedJ9Method *resolvedJ9Callee = (TR_ResolvedJ9Method *) callee;
   TR_ResolvedJ9Method *resolvedJ9Caller = (TR_ResolvedJ9Method *) caller;

   uint32_t numCallers = 0, totalWeight = 0, otherBucketWeight = 0;
   float dynamicFanInRatio = 0.0;
   resolvedJ9Callee->getFaninInfo(&numCallers, &totalWeight, &otherBucketWeight);

   if (numCallers < MIN_NUM_CALLERS || (totalWeight > 0 && otherBucketWeight * 1.0 / totalWeight < otherBucketThreshold))
     return false;



   uint32_t weight = 0;
   bool hasCaller = resolvedJ9Callee->getCallerWeight(resolvedJ9Caller, &weight, bcIndex);

   /*
    * We assume that if the caller lands in the other bucket it is not worth trouble inlining
    * There seem to be an empirical evidence to that.
    * If we increase the number of callers we remember up to 40
    * Still a considerable share of calls lands in the other bucket
    * This indirectly suggests that the other bucket typically consists of
    * a lot of infrequent caller-bcIndex pairs
   */

   if (!hasCaller && weight != ~0) //the caller is in the other bucket
      {
      heuristicTrace (tracer(), "FANIN: callee %s in caller %s @ %d exceeds thresholds due to the caller being in the other bucket",
            callee->signature(comp()->trMemory()),
            caller->signature(comp()->trMemory()),
            bcIndex
         );

      return true;
      }

   if (weight != ~0) //there is an entry for this particular caller
      dynamicFanInRatio = (float)weight / (float)totalWeight ;

   int32_t oldCalculatedSize = calculatedSize;
   if (dynamicFanInRatio == 0.0)
      calculatedSize =  bytecodeSize * multiplier; //weight == ~0 we don't know anything about the caller
   else
      calculatedSize = (uint32_t) ((float)bytecodeSize/dynamicFanInRatio);

   heuristicTrace (tracer(), "FANIN: callee %s in caller %s @ %d oldCalculatedSize %d calculatedSize %d",
         callee->signature(comp()->trMemory()),
         caller->signature(comp()->trMemory()),
         bcIndex, oldCalculatedSize, calculatedSize
         );

   return false;
   }

bool
TR_J9InlinerPolicy::callMustBeInlined(TR_CallTarget *calltarget)
   {
   TR_ResolvedMethod *method = calltarget->_calleeMethod;

   if (method->convertToMethod()->isArchetypeSpecimen())
      return true;

   if (comp()->fej9()->isLambdaFormGeneratedMethod(method))
      return true;

   if (insideIntPipelineForEach(method, comp()))
      {
      if (comp()->trace(OMR::inlining))
         traceMsg(comp(), "forcing inlining of IntPipelineForEach or method inside it:  %s\n", method->signature(comp()->trMemory()));

      return true;
      }


   if (comp()->getOption(TR_EnableSIMDLibrary) &&
       strncmp(calltarget->_calleeMethod->classNameChars(), "com/ibm/dataaccess/SIMD", 23) == 0)
      return true;

#ifdef ENABLE_GPU
   if (strncmp(calltarget->_calleeMethod->classNameChars(), "com/ibm/gpu/Kernel", 18) == 0)
      return true;
#endif

   return false;
   }

void
TR_J9InlinerUtil::adjustCallerWeightLimit(TR::ResolvedMethodSymbol *callerSymbol, int &callerWeightLimit)
   {
   if (inliner()->getPolicy()->aggressiveSmallAppOpts() && (callerSymbol->getRecognizedMethod() == TR::java_util_GregorianCalendar_computeFields) && isHot(comp()))
      callerWeightLimit = 2600;
   }


void
TR_J9InlinerUtil::adjustMethodByteCodeSizeThreshold(TR::ResolvedMethodSymbol *callerSymbol, int &methodByteCodeSizeThreshold)
   {
   if (inliner()->getPolicy()->aggressiveSmallAppOpts() && (callerSymbol->getRecognizedMethod() == TR::java_util_GregorianCalendar_computeFields))
      methodByteCodeSizeThreshold = 400;
   }


bool
TR_J9InlinerPolicy::willBeInlinedInCodeGen(TR::RecognizedMethod method)
   {
#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   if (willInlineCryptoMethodInCodeGen(method))
      {
      return true;
      }
#endif

   return false;
   }

bool
TR_J9InlinerPolicy::skipHCRGuardForCallee(TR_ResolvedMethod *callee)
   {
   // TODO: This is a very hacky way of avoiding HCR guards on sensitive String Compression methods which allows idiom
   // recognition to work. It also avoids unnecessary block splitting in performance sensitive methods for String
   // operations that are quite common. Can we do something better?
   TR::RecognizedMethod rm = callee->getRecognizedMethod();
   switch (rm)
      {
      case TR::java_lang_String_charAtInternal_I:
      case TR::java_lang_String_charAtInternal_IB:
      case TR::java_lang_String_lengthInternal:
      case TR::java_lang_String_isCompressed:
      case TR::java_lang_StringUTF16_length:
      case TR::java_lang_StringBuffer_capacityInternal:
      case TR::java_lang_StringBuffer_lengthInternalUnsynchronized:
      case TR::java_lang_StringBuilder_capacityInternal:
      case TR::java_lang_StringBuilder_lengthInternal:
         return true;
      default:
         break;
      }

   // Skip HCR guard for non-public methods in java/lang/invoke package. These methods
   // are related to implementation details of MethodHandle and VarHandle
   int32_t length = callee->classNameLength();
   char* className = callee->classNameChars();
   if (length > 17
       && !strncmp("java/lang/invoke/", className, 17)
       && !callee->isPublic())
      return true;

   return false;
   }

TR_J9InlinerPolicy::TR_J9InlinerPolicy(TR::Compilation *comp)
   : _aggressivelyInlineInLoops(false), OMR_InlinerPolicy(comp)
   {

   }

TR_J9JSR292InlinerPolicy::TR_J9JSR292InlinerPolicy(TR::Compilation *comp)
   : TR_J9InlinerPolicy(comp)
   {

   }

TR_J9InlinerUtil::TR_J9InlinerUtil(TR::Compilation *comp)
   : OMR_InlinerUtil(comp)
   {

   }

TR_Inliner::TR_Inliner(TR::OptimizationManager *manager)
   : TR::Optimization(manager)
   {}

int32_t TR_Inliner::perform()
   {
   //static bool enableInliningInOSR = feGetEnv("TR_disableInliningInOSR") != NULL;

  //Disabled, but putting in here an env option to enable it for testing
  //this is not the best spot but the other didn't work out
  //and it's temporary anyways
  static const char* enableMT4Testing = feGetEnv("TR_EnableMT4Testing");

  if (!enableMT4Testing)
      comp()->setOption(TR_DisableMultiTargetInlining);


   TR::ResolvedMethodSymbol * sym = comp()->getMethodSymbol();
   if (sym->mayHaveInlineableCall() && optimizer()->isEnabled(OMR::inlining))
      {
      comp()->getFlowGraph()->setStructure(NULL);

      TR_MultipleCallTargetInliner inliner(optimizer(),this);
      if (manager()->numPassesCompleted() == 0)
         inliner.setFirstPass();
      inliner.performInlining(sym);
      manager()->incNumPassesCompleted();
      comp()->getFlowGraph()->resetFrequencies();
      comp()->getFlowGraph()->setFrequencies();
      }

   return 1; // cost??
   }

const char *
TR_Inliner::optDetailString() const throw()
   {
   return "O^O INLINER: ";
   }

template <typename FunctObj>
void TR_MultipleCallTargetInliner::recursivelyWalkCallTargetAndPerformAction(TR_CallTarget *ct, FunctObj &action)
   {

   debugTrace(tracer(),"recursivelyWalkingCallTargetAndPerformAction: Considering Target %p. node estimate before = %d maxbcindex = %d",ct,action.getNodeEstimate(),getPolicy()->getInitialBytecodeSize(ct->_calleeMethod, 0, comp()));

   action(ct,comp());

   TR_CallSite *callsite = 0;
   for(callsite = ct->_myCallees.getFirst() ; callsite ; callsite = callsite->getNext()   )
      {
      for (int32_t i = 0 ; i < callsite->numTargets() ; i++)
         {
         recursivelyWalkCallTargetAndPerformAction(callsite->getTarget(i),action);
         }
      }


   }

int32_t
TR_MultipleCallTargetInliner::applyArgumentHeuristics(TR_LinkHead<TR_ParameterMapping> &map, int32_t originalWeight, TR_CallTarget *target)
   {
   int32_t weight = originalWeight;
   TR_PrexArgInfo *argInfo = target->_ecsPrexArgInfo;

   static char *disableCCI=feGetEnv("TR_DisableConstClassInlining");
   static char *pEnvconstClassWeight=feGetEnv("TR_constClassWeight");
   static int constClassWeight = pEnvconstClassWeight ? atoi(pEnvconstClassWeight) : DEFAULT_CONST_CLASS_WEIGHT;

   int32_t fraction = comp()->getOptions()->getInlinerArgumentHeuristicFraction();
   for(TR_ParameterMapping * parm = map.getFirst(); parm ; parm = parm->getNext())
      {
      if(parm->_parameterNode->getOpCode().isLoadConst())
         {
         weight = weight * (fraction-1) / fraction;
         heuristicTrace(tracer(),"Setting weight to %d because arg is load const.",weight);
         }
      else if (parm->_parameterNode->getOpCodeValue() == TR::aload && parm->_parameterNode->getSymbolReference()->getSymbol()->isConstObjectRef())
         {
         weight = weight * (fraction-1) / fraction;
         heuristicTrace(tracer(),"Setting weight to %d because arg is const object reference.",weight);
         }
      else if (!disableCCI &&
               (parm->_parameterNode->getOpCodeValue() == TR::aloadi) &&
               (parm->_parameterNode->getSymbolReference() == comp()->getSymRefTab()->findJavaLangClassFromClassSymbolRef()))
         {
         weight = constClassWeight;
         heuristicTrace(tracer(),"Setting weight to %d because arg is const Class reference.",weight);
         }
      else if( parm->_parameterNode->getDataType() == TR::Address)
         {
         weight = comp()->fej9()->adjustedInliningWeightBasedOnArgument(weight,parm->_parameterNode, parm->_parmSymbol,comp());
         heuristicTrace(tracer(),"Setting weight to %d after frontend adjusted weight for address parm %p\n",weight,parm->_parameterNode);
         }

      if (!disableCCI && argInfo)
         {
         TR_PrexArgument *argPrexInfo = argInfo->get(parm->_parmSymbol->getOrdinal());
         if (argPrexInfo && argPrexInfo->hasKnownObjectIndex())
            {
            weight = constClassWeight;
            heuristicTrace(tracer(),"Setting weight to %d because arg is known object parm %p\n",weight,parm->_parameterNode);
            break;
            }
         }
      }

   weight -= (map.getSize() * 4);
   heuristicTrace(tracer(),"Setting weight to %d (subtracting numArgs*4)", weight);

   return weight;
   }


//---------------------------------------------------------------------
// TR_InlinerBase eliminateTailRecursion
//---------------------------------------------------------------------

bool
TR_MultipleCallTargetInliner::eliminateTailRecursion(
   TR::ResolvedMethodSymbol * calleeSymbol, TR_CallStack * callStack,
   TR::TreeTop * callNodeTreeTop, TR::Node * parent, TR::Node * callNode, TR_VirtualGuardSelection *guard)
   {
   if (comp()->getOption(TR_DisableTailRecursion))
      return false;

   if (_disableTailRecursion)
      return false;

   TR::TreeTop * nextTT = callNodeTreeTop->getNextRealTreeTop();
   for (;;)
      {
      if (nextTT->getNode()->getOpCodeValue() == TR::Goto)
         nextTT = nextTT->getNode()->getBranchDestination()->getNextRealTreeTop();
      else if (nextTT->getNode()->getOpCodeValue() == TR::BBEnd)
         nextTT = nextTT->getNextTreeTop()->getNextRealTreeTop();
      else break;
      }

   if (!nextTT->getNode()->getOpCode().isReturn())
      return false;

   TR_ResolvedMethod * calleeResolvedMethod = calleeSymbol->getResolvedMethod();
   if (comp()->isDLT() && comp()->getCurrentMethod()->isSameMethod(calleeResolvedMethod))
      return false;
   if (calleeResolvedMethod->numberOfExceptionHandlers() > 0)
      {
      // todo check that none of the parameters are referenced in a catch block...that may not be enough
      if (debug("traceETR"))
         printf("potential to eliminate an eh aware tail recursion to %s\n", tracer()->traceSignature(calleeResolvedMethod));
      return false;
      }

   if (guard->_kind != TR_NoGuard && calleeResolvedMethod->virtualMethodIsOverridden())
      return false; // we can't generate the correct virtual guard

   for (TR_CallStack * cs = callStack; cs->_methodSymbol != calleeSymbol; cs = cs->getNext())
      if (cs->_method->numberOfExceptionHandlers() > 0) // || cs->_method->isSynchronized())
         return false;

   TR::ResolvedMethodSymbol * callerSymbol = callStack->_methodSymbol;

   if (!callerSymbol->getResolvedMethod()->isSameMethod(calleeResolvedMethod))
      return false; // todo ... handle this case

   // check for any parms being marked pre-existent or fixed
   //
   bool parmIsFixedOrFinal = false;
   ListIterator<TR::ParameterSymbol> parms(&calleeSymbol->getParameterList());
   for (TR::ParameterSymbol *p = parms.getFirst(); p; p = parms.getNext())
      {
      if (p->getIsPreexistent() || p->getFixedType())
         parmIsFixedOrFinal = true;
      }

   if (parmIsFixedOrFinal)
      return false;

   TR::Block * branchDestination = calleeSymbol->getFirstTreeTop()->getNode()->getBlock();

   TR::Block * block = callNodeTreeTop->getEnclosingBlock();
   if (nextTT->getNode()->getOpCodeValue() != TR::Return && nextTT->getNode()->getFirstChild() != callNode)
      {
      if (nextTT->getNode()->getFirstChild()->getOpCodeValue() != TR::iadd)
         return false;

      if (nextTT->getNode()->getFirstChild()->getSecondChild() != callNode)
         return false;

      TR::Node * arithmeticNode = nextTT->getNode()->getFirstChild()->getFirstChild();
      if (arithmeticNode->getReferenceCount() > 1)
         return false;

      if (block->getPredecessors().empty() || (block->getPredecessors().size() > 1))
         return false;

      TR::Block * conditionBlock = toBlock(block->getPredecessors().front()->getFrom());
      if (conditionBlock->getSuccessors().size() != 2)
         return false;

      TR::Block * otherBranch = toBlock(conditionBlock->getSuccessors().front()->getTo() == block ? (*(++conditionBlock->getSuccessors().begin()))->getTo() : conditionBlock->getSuccessors().front()->getTo());
      TR::Node * returnNode = otherBranch->getFirstRealTreeTop()->getNode();
      if (returnNode->getOpCodeValue() != TR::ireturn)
         return false;

      TR::Node * returnValue = returnNode->getFirstChild();
      if (returnValue->getOpCodeValue() != TR::iconst || returnValue->getInt() != 0)
         return false;

      if (debug("arithmeticSeries"))
         {
         // idiv
         //   imul
         //     iload #101[0x1
         //     iadd
         //       iload #101[0
         //       iconst 1
         //   iconst 2
         TR::TreeTop * ifTreeTop = conditionBlock->getLastRealTreeTop();

         TR::TreeTop::create(comp(), ifTreeTop->getPrevTreeTop(),
                            TR::Node::create(TR::ireturn, 1,
                                            TR::Node::create(TR::idiv, 2,
                                                  TR::Node::create(TR::imul, 2,
                                                        arithmeticNode,
                                                        TR::Node::create(TR::iadd, 2,
                                                              arithmeticNode,
                                                              TR::Node::create(returnNode, TR::iconst, 0, 1))),
                                                  TR::Node::create(returnNode, TR::iconst, 0, 2))));


         callerSymbol->removeTree(ifTreeTop);
         TR::CFG * cfg = callerSymbol->getFlowGraph();
         cfg->removeEdge(conditionBlock->getSuccessors().front());
         cfg->removeEdge(*(++conditionBlock->getSuccessors().begin()));
         cfg->addEdge(conditionBlock, cfg->getEnd());
         return true;
         }

      TR::DataType dt = TR::Int32;
      TR::SymbolReference * temp = comp()->getSymRefTab()->createTemporary(calleeSymbol, dt);
      returnNode->setAndIncChild(0, TR::Node::createLoad(returnNode, temp));
      returnValue->decReferenceCount();
      TR::Block * generatedFirstBlock = calleeSymbol->prependEmptyFirstBlock();
      generatedFirstBlock->setFrequency(conditionBlock->getFrequency());
      generatedFirstBlock->append(TR::TreeTop::create(comp(), TR::Node::createStore(temp, returnValue)));
      arithmeticNode = TR::Node::copy(arithmeticNode);
      arithmeticNode->decReferenceCount();
      TR::TreeTop::create(comp(), callNodeTreeTop->getPrevTreeTop(),
                         TR::Node::createStore(temp, TR::Node::create(TR::iadd, 2, TR::Node::createLoad(returnNode, temp), arithmeticNode)));
      }

   if (!performTransformation(comp(), "%sEliminating tail recursion to %s\n", OPT_DETAILS, tracer()->traceSignature(calleeResolvedMethod)))
      return false;

   //please don't move this if. It needs to be done after all early exits but exactly before
   //we do any transformations
   if (!comp()->incInlineDepth(calleeSymbol, callNode, !callNode->getOpCode().isCallIndirect(), guard, calleeResolvedMethod->classOfMethod(), 0))
      {
      return false;
      }


   _disableInnerPrex = true;

   TR::CFG * callerCFG = callerSymbol->getFlowGraph();
   TR::TreeTop * prevTreeTop = callNodeTreeTop->getPrevTreeTop();

   if (parent->getOpCode().isNullCheck())
      prevTreeTop = parent->extractTheNullCheck(prevTreeTop);

   assignArgumentsToParameters(calleeSymbol, prevTreeTop, callNode);

   TR::CFGEdge * backEdge;
   if (guard->_kind != TR_NoGuard)
      {
//       TR::Block *block2 = block->split(callNodeTreeTop, callerCFG);
//       block->append(TR::TreeTop::create(comp(), createVirtualGuard(callNode, callNode->getSymbol()->castToResolvedMethodSymbol(), branchDestination->getEntry(), false, (void *)calleeResolvedMethod->classOfMethod(), false)));
//       //      branchDestination->setIsCold(); <--- branch destination is NOT cold
//       block2->setIsCold();
//       backEdge = TR::CFGEdge::createEdge(block,  branchDestination, trMemory());
//       callerCFG->addEdge(backEdge);

      TR::Block *gotoBlock    = block->split(callNodeTreeTop, callerCFG);
      TR::Block *block2       = gotoBlock->split(callNodeTreeTop, callerCFG);

      TR::Node  *gotoNode     = TR::Node::create(callNode, TR::Goto);
      gotoNode->setBranchDestination(branchDestination->getEntry());
      gotoBlock->append(TR::TreeTop::create(comp(), gotoNode));

      // calleeResolvedMethod will be inlined with a virtual guard v.
      // At this point we need to create another virtual guard v' for the
      // recursive call. v' needs a calleeIndex that is different from the one
      // for v (otherwise we cannot distinguish between the two virtual guards)
      // We achieve this by artificially incrementing the inlining depth as if
      // we inlined calleeResolvedMethod again.

      TR::Node *vguardNode = createVirtualGuard(callNode,
                            callNode->getSymbol()->castToResolvedMethodSymbol(),
                            block2->getEntry(),
                            comp()->getCurrentInlinedSiteIndex(),//branchDestination->getEntry()->getNode()->getInlinedSiteIndex(),
                            calleeResolvedMethod->classOfMethod(), false, guard);
      block->append(TR::TreeTop::create(comp(), vguardNode));
      callerCFG->addEdge(block, block2);

      TR::CFGEdge *origEdge = gotoBlock->getSuccessors().front();
      backEdge = TR::CFGEdge::createEdge(gotoBlock,  branchDestination, trMemory());
      callerCFG->addEdge(backEdge);
      callerCFG->removeEdge(origEdge);
      if (guard->_kind == TR_ProfiledGuard)
         {
         if (block->getFrequency() < 0)
            block2->setFrequency(block->getFrequency());
         else
            {
            if (guard->isHighProbablityProfiledGuard())
               block2->setFrequency(MAX_COLD_BLOCK_COUNT+1);
            else
               block2->setFrequency(TR::Block::getScaledSpecializedFrequency(block->getFrequency()));
            }
         }
      else
         {
         block2->setFrequency(VERSIONED_COLD_BLOCK_COUNT);
         block2->setIsCold();
         }
      }
   else
      {
      callNodeTreeTop->setNode(TR::Node::create(callNode, TR::Goto, 0, branchDestination->getEntry()));
      TR_ASSERT((block->getSuccessors().size() == 1), "eliminateTailRecursion, block with call does not have exactly 1 successor");
      TR::CFGEdge * existingSuccessorEdge = block->getSuccessors().front();
      backEdge = TR::CFGEdge::createEdge(block,  branchDestination, trMemory());
      callerCFG->addEdge(backEdge);
      callerCFG->removeEdge(existingSuccessorEdge);
      if (block->getLastRealTreeTop() != callNodeTreeTop)
         callerSymbol->removeTree(block->getLastRealTreeTop());
      TR_ASSERT(block->getLastRealTreeTop() == callNodeTreeTop, "eliminateTailRecursion call isn't last or second last tree in block");
      }

   if (comp()->getProfilingMode() == JitProfiling)
      {
      TR::Node *asyncNode = TR::Node::createWithSymRef(callNode, TR::asynccheck, 0, comp()->getSymRefTab()->findOrCreateAsyncCheckSymbolRef(comp()->getMethodSymbol()));
      block->prepend(TR::TreeTop::create(comp(), asyncNode));
      }

   backEdge->setCreatedByTailRecursionElimination(true);
   calleeSymbol->setMayHaveLoops(true);
   comp()->decInlineDepth(); // undo what we artificially did before
   return true;
   }

void
TR_MultipleCallTargetInliner::assignArgumentsToParameters(TR::ResolvedMethodSymbol * calleeSymbol, TR::TreeTop * prevTreeTop, TR::Node * callNode)
   {
   int32_t i = callNode->getFirstArgumentIndex();
   ListIterator<TR::ParameterSymbol> parms(&calleeSymbol->getParameterList());
   for (TR::ParameterSymbol * p = parms.getFirst(); p; ++i, p = parms.getNext())
      {
      TR::SymbolReference * sr = comp()->getSymRefTab()->findOrCreateAutoSymbol(calleeSymbol, p->getSlot(), p->getDataType(), true);
      TR::Node * arg = callNode->getChild(i);
      if (arg->getReferenceCount() != 1 || !arg->getOpCode().hasSymbolReference() || arg->getSymbolReference() != sr)
         {
         arg->decReferenceCount(); // logically remove it from the call noe

         // Consider,
         // void foo(int a, int b) { .... return foo(b, a); }
         // We're going to create 'a = b; b = a;' which will assign the modified value of 'a' to 'b'.
         // To get the original value of 'a' assigned to 'b' we
         // create a treetop before the assignments so that 'a' is evaluated before it is modified.
         //
         prevTreeTop = TR::TreeTop::create(comp(), prevTreeTop, TR::Node::create(TR::treetop, 1, arg));

         TR::Node *storeNode = TR::Node::createStore(sr, arg);

         TR::TreeTop::create(comp(), prevTreeTop, storeNode);
         TR::Node * newArg = TR::Node::createLoad(arg, sr);

         if (arg->getType().isBCD())
            {
            storeNode->setDecimalPrecision(arg->getDecimalPrecision());
            newArg->setDecimalPrecision(arg->getDecimalPrecision());
            }

         if (i == 1 && i == callNode->getFirstArgumentIndex() && callNode->getChild(0)->getChild(0) == arg)
            {
            arg->decReferenceCount();
            callNode->getChild(0)->setAndIncChild(0, newArg);
            }
         callNode->setAndIncChild(i, newArg);
         }
      }
   }

TR_MultipleCallTargetInliner::TR_MultipleCallTargetInliner(TR::Optimizer *optimizer, TR::Optimization *optimization)
   : TR_InlinerBase(optimizer, optimization)
   {

   }


void
TR_MultipleCallTargetInliner::walkCallSite(
   TR::ResolvedMethodSymbol * calleeSymbol, TR_CallStack * callStack,
   TR::TreeTop * callNodeTreeTop, TR::Node * parent, TR::Node * callNode, TR_VirtualGuardSelection *guard,
   TR_OpaqueClassBlock * thisClass, bool inlineNonRecursively, int32_t walkDepth)
   {
   TR::ResolvedMethodSymbol * callerSymbol = callStack->_methodSymbol;

   int32_t bytecodeSize = getPolicy()->getInitialBytecodeSize(calleeSymbol->getResolvedMethod(), calleeSymbol, comp());

   ///comp()->getFlowGraph()->setMaxFrequency(-1);
   ///comp()->getFlowGraph()->setMaxEdgeFrequency(-1);

   TR_J9InnerPreexistenceInfo innerPrexInfo(comp(), calleeSymbol, callStack, callNodeTreeTop, callNode, guard->_kind);

   bool genILSucceeded = false;
   vcount_t visitCount = comp()->getVisitCount();
   if (!calleeSymbol->getFirstTreeTop())
      {
      //if (comp()->trace(inlining))
      dumpOptDetails(comp(), "O^O INLINER: Peeking into the IL from walkCallSites as part of the inlining heuristic for [%p]\n", calleeSymbol);

      //comp()->setVisitCount(1);
      genILSucceeded = (NULL != calleeSymbol->getResolvedMethod()->genMethodILForPeekingEvenUnderMethodRedefinition(calleeSymbol, comp()));
      //comp()->setVisitCount(visitCount);
      }

   dumpOptDetails(comp(), "  -- %s\n", genILSucceeded? "succeeded" : "failed");

   ///if (!inlineNonRecursively && calleeSymbol->mayHaveInlineableCall())

   if (!inlineNonRecursively && genILSucceeded && calleeSymbol->mayHaveInlineableCall())
      {
      walkCallSites(calleeSymbol, callStack, &innerPrexInfo, walkDepth+1);
      }
   //calleeSymbol->setFirstTreeTop(NULL); // We can reuse the peeked trees.  If we're doing real ILGen, the trees will be re-created anyway.
   }

void
TR_MultipleCallTargetInliner::walkCallSites(TR::ResolvedMethodSymbol * callerSymbol, TR_CallStack * prevCallStack, TR_InnerPreexistenceInfo *innerPrexInfo, int32_t walkDepth)
   {
   heuristicTrace(tracer(),"**WalkCallSites: depth %d\n",walkDepth);
   if (walkDepth > MAX_ECS_RECURSION_DEPTH / 4 )
      return;

   TR_InlinerDelimiter delimiter(tracer(),"walkCallSites");

   TR_CallStack callStack(comp(), callerSymbol, callerSymbol->getResolvedMethod(), prevCallStack, 0);

   if (innerPrexInfo)
      callStack._innerPrexInfo = innerPrexInfo;

   if (prevCallStack == 0)
      callStack.initializeControlFlowInfo(callerSymbol);

   bool currentBlockHasExceptionSuccessors = false;
   bool prevDisableTailRecursion = _disableTailRecursion;
   bool prevDisableInnerPrex = _disableInnerPrex;
   _disableTailRecursion = false;
   _disableInnerPrex = false;

   bool isCold = false;
   for (TR::TreeTop * tt = callerSymbol->getFirstTreeTop(); tt && (walkDepth==0); tt = tt->getNextTreeTop())
      {
      TR::Node * parent = tt->getNode();

      if (parent->getOpCodeValue() == TR::BBStart)
         {
         isCold = false;
         TR::Block *block = parent->getBlock();

         if (prevCallStack == 0 && !block->isExtensionOfPreviousBlock())
            callStack.makeBasicBlockTempsAvailable(_availableBasicBlockTemps);

         // dont inline into cold blocks
         //
         if (block->isCold() ||
             !block->getExceptionPredecessors().empty())
            {
            isCold = true;
            }

         currentBlockHasExceptionSuccessors = !block->getExceptionSuccessors().empty();

         if (prevCallStack == 0)
            callStack.updateState(block);
         _isInLoop = callStack._inALoop;
         }
      else if (parent->getNumChildren())
         {
         TR::Node * node = parent->getChild(0);
         if (node->getOpCode().isCall() && node->getVisitCount() != _visitCount)
            {
            TR::Symbol *sym  = node->getSymbol();
            if (!isCold)
               {

               ///TR::ResolvedMethodSymbol * calleeSymbol = isInlineable(&callStack, node, guard, thisClass,tt);
               TR::SymbolReference *symRef = node->getSymbolReference();
               TR::MethodSymbol *calleeSymbol = symRef->getSymbol()->castToMethodSymbol();

               //TR_CallSite *callsite = new (trStackMemory()) TR_CallSite (symRef->getOwningMethod(comp()), tt, parent, node, calleeSymbol->getMethod(), 0, (int32_t)symRef->getOffset(), symRef->getCPIndex(), 0, calleeSymbol->getResolvedMethodSymbol(), node->getOpCode().isCallIndirect(), calleeSymbol->isInterface(), node->getByteCodeInfo(), comp());


               TR_CallSite *callsite = TR_CallSite::create(tt, parent, node,
                                                            0, symRef, (TR_ResolvedMethod*) 0,
                                                            comp(), trMemory() , stackAlloc);


               debugTrace(tracer(),"**WalkCallSites: Analysing Call at call node %p . Creating callsite %p to encapsulate call.",node,callsite);
               getSymbolAndFindInlineTargets(&callStack, callsite);

               heuristicTrace(tracer(),"**WalkCallSites:Searching for Targets returned %d targets for call at node %p. ",callsite->numTargets(),node);

               ///if (calleeSymbol)
               if (callsite->numTargets())
                  {
                  bool flag=false;
                  for(int32_t i=0 ; i < callsite->numTargets() && flag==false; i++)
                     {

               // TR::ResolvedMethodSymbol *calleeResolvedSymbol = calleeSymbol->getResolvedMethodSymbol();
               // if (!calleeResolvedSymbol)
                  // continue;

                     bool walkCall = false;

                     if (! (
                            callsite->getTarget(i)->_calleeSymbol->isVMInternalNative()      ||
                            callsite->getTarget(i)->_calleeSymbol->isHelper()                ||
                            callsite->getTarget(i)->_calleeSymbol->isNative()                ||
                            callsite->getTarget(i)->_calleeSymbol->isSystemLinkageDispatch() ||
                            callsite->getTarget(i)->_calleeSymbol->isJITInternalNative()     ||
                            callsite->getTarget(i)->_calleeSymbol->getResolvedMethod()->isAbstract()
                           ))
                        {
                        if (TR::Compiler->mtd.isCompiledMethod(callsite->getTarget(i)->_calleeSymbol->getResolvedMethod()->getPersistentIdentifier()))
                           {
                           TR_PersistentJittedBodyInfo * bodyInfo = ((TR_ResolvedJ9Method*) callsite->getTarget(i)->_calleeSymbol->getResolvedMethodSymbol()->getResolvedMethod())->getExistingJittedBodyInfo();
                           if (bodyInfo &&
                               bodyInfo->getHotness() < warm &&
                               !bodyInfo->getIsProfilingBody())
                              walkCall = true;
                           }
                        else
                           walkCall = true;
                        }
                   if (symRef->getOwningMethodSymbol(comp()) != callerSymbol)
                      {
                      walkCall = false;
                      }

                     if (walkCall)
                        {
                        //                      TR_ResolvedMethod * calleeResolvedMethod = calleeResolvedSymbol->getResolvedMethod();
                        TR_CallStack * cs = callStack.isCurrentlyOnTheStack(callsite->getTarget(i)->_calleeMethod, 1);
                        TR_PersistentMethodInfo * methodInfo = TR_PersistentMethodInfo::get(callsite->getTarget(i)->_calleeMethod);       //calleeResolvedMethod);
                        bool alreadyVisited = false;

                        if (methodInfo && methodInfo->wasScannedForInlining())
                           {
                           //printf("Already visited\n");
                           debugTrace(tracer(),"Walk call sites for scanning: methodInfo %p already visited\n", methodInfo);

                           alreadyVisited = true;
                           }

                        if (!(!alreadyVisited &&
                              cs    &&
                              callsite->getTarget(i)->_calleeSymbol == callsite->_callNode->getSymbol() &&
                              eliminateTailRecursion(cs->_methodSymbol, &callStack, callsite->_callNodeTreeTop, callsite->_parent, callsite->_callNode, callsite->getTarget(i)->_guard)))
                           {
                           //                         walkCallSite(calleeResolvedSymbol, &callStack, tt, parent, node, guard, thisClass, false, walkDepth);
                           walkCallSite(callsite->getTarget(i)->_calleeSymbol, &callStack,callsite->_callNodeTreeTop,callsite->_parent,callsite->_callNode,callsite->getTarget(i)->_guard,callsite->getTarget(i)->_receiverClass,false,walkDepth);
                           debugTrace(tracer(),"Walk call sites for scanning: at call site: %s\n", tracer()->traceSignature(callsite->getTarget(i)->_calleeSymbol));

//                         TR::SymbolReference * symRef = node->getSymbolReference();
                         // TR_CallSite *callsite = new (trStackMemory()) TR_CallSite (symRef->getOwningMethod(comp()),
                                                                                    // tt,
                                                                                    // parent,
                                                                                    // node,
                                                                                    // calleeResolvedSymbol->getMethod(),
                                                                                    // 0,
                                                                                    // (int32_t)symRef->getOffset(),
                                                                                    // symRef->getCPIndex(),
                                                                                    // 0,
                                                                                    // calleeResolvedSymbol->getResolvedMethodSymbol(),
                                                                                    // node->getOpCode().isCallIndirect(),
                                                                                    // calleeResolvedSymbol->isInterface(),
                                                                                    // node->getByteCodeInfo(),
                                                                                    // comp());

                           weighCallSite(&callStack, callsite, currentBlockHasExceptionSuccessors, true);

                           if(tracer()->debugLevel())
                              {
                              tracer()->dumpCallSite(callsite, "Dumping Call Site after Weighing");
                              }

                           if (methodInfo)
                              {
                              methodInfo->setWasScannedForInlining(true);
                              debugTrace(tracer(),"Walk call sites for scanning: set scaneed for methodInfo %p\n", methodInfo);
                              }
                           //printf("Walk %s, method info %p\n", calleeResolvedSymbol->signature(trMemory()), methodInfo);
                           }
                        }
                     }
                  } //end for loop over call targets
               }
            node->setVisitCount(_visitCount);
            }
         }
      }

   _disableTailRecursion = prevDisableTailRecursion;
   _disableInnerPrex = prevDisableInnerPrex;
   }

bool TR_MultipleCallTargetInliner::inlineCallTargets(TR::ResolvedMethodSymbol *callerSymbol, TR_CallStack *prevCallStack, TR_InnerPreexistenceInfo *innerPrexInfo)
   {
   TR_InlinerDelimiter delimiter(tracer(),"TR_MultipleCallTargetInliner::inlineCallTargets");

   TR_CallStack callStack(comp(), callerSymbol, callerSymbol->getResolvedMethod(), prevCallStack, 0, true);

   if (innerPrexInfo)
      callStack._innerPrexInfo = innerPrexInfo;

   if (prevCallStack == 0)
      callStack.initializeControlFlowInfo(callerSymbol);

   bool anySuccess = false;
   bool anySuccess2 = false;

   bool currentBlockHasExceptionSuccessors = false;

   bool prevDisableTailRecursion = _disableTailRecursion;
   bool prevDisableInnerPrex = _disableInnerPrex;
   bool prevInliningAsWeWalk = _inliningAsWeWalk;

   _disableTailRecursion = false;
   _disableInnerPrex = false;
   bool isCold = false;

   {
   TR_InlinerDelimiter delimiter(tracer(),"collectTargets");

   int32_t thisCallSite = callerSymbol->getFirstTreeTop()->getNode()->getInlinedSiteIndex();

   TR::TreeTop *nextTree = NULL;
   for (TR::TreeTop * tt = callerSymbol->getFirstTreeTop(); tt; tt = nextTree)
      {
      // Inlining can add code downstream of our traversal.  We need to skip that code.
      //
      nextTree = tt->getNextTreeTop();

      TR::Node * parent = tt->getNode();

      if (prevCallStack)
         _inliningAsWeWalk = true;

      if (parent->getOpCodeValue() == TR::BBStart)
         {
         isCold = false;
         TR::Block *block = parent->getBlock();

         if (prevCallStack == 0 && !block->isExtensionOfPreviousBlock())
            callStack.makeBasicBlockTempsAvailable(_availableBasicBlockTemps);

         // dont inline into cold blocks
         if (block->isCold() || !block->getExceptionPredecessors().empty())
            {
            isCold = true;
            }

         // FIXME: the following assumes that catch blocks are at the end of the method
         // which may not generally be true.  Correct fix is to do either dom-pdom or
         // structural analysis before this opt, and mark the cold-paths, and skipping
         // cold blocks would automagically do the trick
         //
         //if (!block->getExceptionPredecessors().empty())
         //   break; // dont inline into catch blocks

         currentBlockHasExceptionSuccessors = !block->getExceptionSuccessors().empty();

         if (prevCallStack == 0)
            callStack.updateState(block);
         }
      else if (parent->getNumChildren())
         {
         TR::Node * node = parent->getChild(0);
         if (node->getOpCode().isFunctionCall() && node->getVisitCount() != _visitCount)
            {
            TR_CallStack::SetCurrentCallNode sccn(callStack, node);

            TR::Symbol *sym  = node->getSymbol();
            if (!isCold && !node->isTheVirtualCallNodeForAGuardedInlinedCall())
               {
               TR::SymbolReference * symRef = node->getSymbolReference();
               TR::MethodSymbol * calleeSymbol = symRef->getSymbol()->castToMethodSymbol();

               TR_CallSite *callsite = TR_CallSite::create(tt, parent, node,
                                                            0, symRef, (TR_ResolvedMethod*) 0,
                                                            comp(), trMemory() , stackAlloc);

               if (prevCallStack==0)
                  {
                  heuristicTrace(tracer(),"\n");
                  heuristicTrace(tracer(),"^^^ Top Level: Analysing Call at call node %p . Creating callsite %p to encapsulate call.",node,callsite);
                  }

               getSymbolAndFindInlineTargets(&callStack, callsite);

               if (!prevCallStack && callsite->numTargets() > 0)
                  {
                  // buildPrexArgInfo and propagateArgs use the caller symbol to look up the invoke bytecode.
                  // In a pass of inliner after the first (which happens in JSR292), the invoke bytecode won't
                  // be in the top-level method's bytecode, and the caller can vary with the call node.
                  TR::ResolvedMethodSymbol *thisCallSiteCallerSymbol = node->getSymbolReference()->getOwningMethodSymbol(comp());
                  TR_PrexArgInfo* compArgInfo = TR_PrexArgInfo::buildPrexArgInfoForMethodSymbol(thisCallSiteCallerSymbol, tracer());

                  if (tracer()->heuristicLevel())
                     {
                     alwaysTrace(tracer(), "compArgInfo :");
                     compArgInfo->dumpTrace();
                     }
                  compArgInfo->clearArgInfoForNonInvariantArguments(thisCallSiteCallerSymbol, tracer());
                  TR_PrexArgInfo::propagateArgsFromCaller(thisCallSiteCallerSymbol, callsite, compArgInfo, tracer());
                  if (tracer()->heuristicLevel())
                     {
                     alwaysTrace(tracer(), "callsite->getTarget(0)->_ecsPrexArgInfo :");
                     callsite->getTarget(0)->_ecsPrexArgInfo->dumpTrace();
                     }
                  }

               heuristicTrace(tracer(),"Searching for Targets returned %d targets for call at node %p. ",callsite->numTargets(),node);

               if (callsite->numTargets())
                  {
                  bool flag = false;
                  for (int32_t i = 0; i < callsite->numTargets() && flag == false; i++)
                     {
                     TR_CallStack *cs = callStack.isCurrentlyOnTheStack(callsite->getTarget(i)->_calleeMethod,1);
                     if (cs && callsite->getTarget(i)->_calleeSymbol == node->getSymbol() &&
                           eliminateTailRecursion( cs->_methodSymbol, &callStack, callsite->_callNodeTreeTop, callsite->_parent,callsite->_callNode,callsite->getTarget(i)->_guard) )
                        {
                        anySuccess2 = true;
                        flag = true;
                        }
                     }

                  if (!flag)
                     {
                     if (prevCallStack == 0)//we only weigh base level calls.. All other calls we proceed right to inlining
                        {
                        weighCallSite(&callStack, callsite, currentBlockHasExceptionSuccessors);

                        if (tracer()->debugLevel())
                           {
                           tracer()->dumpCallSite(callsite, "Dumping Call Site after Weighing");
                           }
                        }
                     else
                        {
                        // with !comp()->getOption(TR_DisableNewInliningInfrastructure)
                        // prevCallStack == 0 will always be true?

                        for (int32_t i=0; i<callsite->numTargets(); i++)
                           {
                           heuristicTrace(tracer(),"call depth > 0 . Inlining call at node %p",node);
                           anySuccess2 |= inlineCallTarget(&callStack, callsite->getTarget(i), true);
                           }
                        }
                     }
                  else
                     {
                     // when flag is true do nothing
                     }
                  }
               else
                  {
                  heuristicTrace(tracer(),"Found No Inlineable targets for call at node %p\n",node);
                  debugTrace(tracer(),"Adding callsite %p to list of deadCallSites",callsite);
                  _deadCallSites.add(callsite);
                  }
               }
            else
               {
               TR::SymbolReference * symRef = node->getSymbolReference();
               TR::MethodSymbol * calleeSymbol = symRef->getSymbol()->castToMethodSymbol();
               heuristicTrace(tracer(),"Block containing call node %p %s. Skipping call.", node, node->isTheVirtualCallNodeForAGuardedInlinedCall() ? "is on the cold side of a guard" : "is cold");
               tracer()->insertCounter(Cold_Block,tt);
               }

            node->setVisitCount(_visitCount);
            }
         else if (node->getOpCode().isCall())
            {
            debugTrace(tracer(),"Failing for an unknown reason. TreeTop = %p, node = %p nodevisitCount = %d _visitCount = %d getInlinedSiteIndex() = %d thisCallSite = %d. ",
                         tt, node, node->getVisitCount(), _visitCount,node->getInlinedSiteIndex(),thisCallSite);
            }
         }
      }
   }

   for (TR_CallTarget *target = _callTargets.getFirst(); target; target = target->getNext())
      target->_prexArgInfo = getUtil()->computePrexInfo(target);

   if (prevCallStack == 0)
      {
      TR_InlinerDelimiter delimiter(tracer(),"refineCallgraph");

      int32_t size = getPolicy()->getInitialBytecodeSize(callerSymbol, comp());
      int32_t limit = _callerWeightLimit;
      int32_t totalWeight=0;

      TR_CallTarget * calltarget = NULL;

      if (comp()->getOption(TR_TraceAll))
         {
         traceMsg(comp(), "\n\n~~~ Call site weights for %s\n", comp()->signature());
         traceMsg(comp(), "original size: %d\n", size);
         traceMsg(comp(), "Inlining weight limit: %d\n", limit);
         totalWeight = 0;
         for (calltarget = _callTargets.getFirst(); calltarget; calltarget = calltarget->getNext())
            {
            totalWeight += calltarget->_weight;
            traceMsg(comp(), "Calltarget %p callnode %p %s\n", calltarget, &calltarget->_myCallSite->_callNode, tracer()->traceSignature(calltarget->_calleeSymbol));
            traceMsg(comp(), "Site size: %d site weight %d call-graph adjusted weight %lf, total weight %d\n", calltarget->_size, calltarget->_weight, calltarget->_callGraphAdjustedWeight, totalWeight);
            }
         }

      static const char * p = feGetEnv("TR_TrivialWeightForLimit");
      int32_t trivialWeightForLimit = 30;

      if (p)
         {
         trivialWeightForLimit = atoi(p);
         printf("Using trivial weight limit of %d\n", trivialWeightForLimit);
         }

      TR_CallTarget* callTargetToChop = NULL;
      {
      bool doneInlining = false;
      int32_t totalWeight = 0;
      TR_CallTarget * prev = 0;
      for (calltarget = _callTargets.getFirst(); calltarget; prev = calltarget, calltarget = calltarget->getNext())
         {
         totalWeight += calltarget->_weight;
         if (doneInlining)
            tracer()->insertCounter(Exceeded_Caller_Budget,calltarget->_myCallSite->_callNodeTreeTop);
         else if (totalWeight > limit && calltarget->_weight > trivialWeightForLimit)
            {
            callTargetToChop = calltarget;
            doneInlining = true;
            }
         }
      }

      TR_CallTarget * prev = 0;
      int32_t estimatedNumberOfNodes = getCurrentNumberOfNodes();
      debugTrace(tracer(), "Initially, estimatedNumberOfNodes = %d\n", estimatedNumberOfNodes);
      for (calltarget = _callTargets.getFirst(); calltarget != callTargetToChop; prev = calltarget, calltarget = calltarget->getNext())
         {
         generateNodeEstimate myEstimate;
         recursivelyWalkCallTargetAndPerformAction(calltarget, myEstimate);
         estimatedNumberOfNodes += myEstimate.getNodeEstimate();

         debugTrace(tracer(),"Estimated Number of Nodes is %d after calltarget %p",estimatedNumberOfNodes,calltarget);

         float factor = 1.1F;          // this factor was chosen based on a study of a large WAS app that showed that getMaxBytecodeindex was 92% accurate compared to nodes generated

         if ((uint32_t)(estimatedNumberOfNodes*factor) > _nodeCountThreshold)
            {
            callTargetToChop = calltarget;
            debugTrace(tracer(),"estimate nodes exceeds _nodeCountThreshold, chopped off targets staring from %p, lastTargetToInline %p\n", callTargetToChop, prev);
            break;
            }
         }

      processChoppedOffCallTargets(prev, callTargetToChop, estimatedNumberOfNodes);
      if (comp()->getOption(TR_TraceAll) || tracer()->heuristicLevel())
         {
         tracer()->dumpCallGraphs(&_callTargets);
         tracer()->dumpDeadCalls(&_deadCallSites);
         }
      }

   if (prevCallStack == 0)
      {
      for (TR_CallTarget* calltarget = _callTargets.getFirst(); calltarget; calltarget = calltarget->getNext())
         {
         debugTrace(tracer(), "marking calltarget %p of %p as MT_Marked", calltarget, calltarget->_myCallSite);
         calltarget->_failureReason = MT_Marked;
         }

      for (TR_CallTarget* calltarget = _callTargets.getFirst(); calltarget; calltarget = calltarget->getNext())
         {
         TR_CallSite* clSite = calltarget->_myCallSite;
         for (int i = 0 ; i < clSite->numTargets(); i++)
            {
            if (clSite->getTarget(i)->_failureReason != MT_Marked)
               {
               debugTrace(tracer(), "removing calltarget %p of %p as it isn't in _callTargets", clSite->getTarget(i), calltarget->_myCallSite);
               clSite->removecalltarget(i, tracer(), Not_Sane);
               i--;
               }
            }
         }

      if (tracer()->heuristicLevel())
         {
         tracer()->dumpInline(&_callTargets, "inline script");
         }
      }

   if (prevCallStack == 0)
      {
      TR_InlinerDelimiter delimiter(tracer(),"inlineTransformation");

      TR_CallTarget * calltarget = NULL;

      heuristicTrace(tracer(),"Starting Transformation Phase\n");
      //static int si, sj;
      //printf("graph exceeded %d times out of %d", (calltarget ? ++si : si) , ++sj);

      // We must inline in tree order because of the way temp sharing is done.
      // there are two types of temporaries inliner generates - availablebasicblocktemps, which are used when inlining breaks a block, and commoning must be broken and
      // availableTemps, which are generally used for when a parameter needs a temporary created for it.
      // all the methods that deal with temps (parametertoargumentmapper, handleinjectedbasicblock,transforminlinedfunction) will consult these lists
      // usually, it will search a list, and if it doesn't find a temp, search the second list.
      // the problem is when inlining out of order and with the fact that both temp lists can be consulted, it is possible that a temp will get misused.
      // an example will be a call lower down was inlined first and created a temp t1, for a parameter (the block doesn't get split).  It gets added to availableTemps after inlining.
      // After, higher up (in the same block) another call now gets inlined, and splits the block.  handleinjectedbasicblock now goes and breaks commoning around this higher up call.
      // when this happens, it can grab the temp t1 from the availableTemps list and reuse it for breaking commoning.  Now there are two stores to t1 in the same block.  If there was any
      // commoning that existed after the second store to t1 that was supposed to get broken, it will now load a bad value of t1.


      for (TR::TreeTop * tt = callerSymbol->getFirstTreeTop(); tt; tt = tt->getNextTreeTop())
         {
         TR::Node * parent = tt->getNode();
         if (tt->getNode()->getNumChildren() && tt->getNode()->getChild(0)->getOpCode().isCall())
            {
            debugTrace(tracer()," (Second Iteration)  Found a call at tt %p node %p",tt,tt->getNode());
            TR_CallStack::SetCurrentCallNode sccn(callStack, tt->getNode()->getChild(0));
            for (calltarget = _callTargets.getFirst(); calltarget; calltarget = calltarget->getNext())
               {
               if(tracer()->debugLevel())
                  debugTrace(tracer()," (Second Iteration) Considering call target %p  ByteCodeIndex = %d calltarget->_myCallSite->_callNodeTreeTop = %p"
                                       " alreadyInlined = %d signature = %s",
                                       calltarget,calltarget->_myCallSite->_callNode->getByteCodeIndex(),calltarget->_myCallSite->_callNodeTreeTop,
                                       calltarget->_alreadyInlined,tracer()->traceSignature(calltarget->_calleeSymbol));

               if (calltarget->_myCallSite->_callNodeTreeTop == tt && !calltarget->_alreadyInlined)
                  {
                  TR::TreeTop* oldTt = tt;
                  bool success = inlineCallTarget(&callStack, calltarget, true, NULL, &tt);
                  anySuccess |= success;
                  debugTrace(tracer(), "(Second Iteration) call target %p node %p.  success = %d anySuccess = %d",calltarget, oldTt->getNode(),success,anySuccess);
                  }
               }
            }
         if (parent->getOpCodeValue() == TR::BBStart &&
             !parent->getBlock()->isExtensionOfPreviousBlock())
            callStack.makeBasicBlockTempsAvailable(_availableBasicBlockTemps);
         }
      }

   _disableTailRecursion = prevDisableTailRecursion;
   _disableInnerPrex = prevDisableInnerPrex;
   _inliningAsWeWalk = prevInliningAsWeWalk;

   callStack.commit();
   return anySuccess;
   }

void TR_MultipleCallTargetInliner::weighCallSite( TR_CallStack * callStack , TR_CallSite *callsite, bool currentBlockHasExceptionSuccessors, bool dontAddCalls)
   {
   TR_J9InlinerPolicy *j9inlinerPolicy = (TR_J9InlinerPolicy *) getPolicy();
   TR_InlinerDelimiter delimiter(tracer(), "weighCallSite");

   for (int32_t k = 0; k < callsite->numTargets(); k++)
      {
      uint32_t size = 0;

      TR_EstimateCodeSize::raiiWrapper ecsWrapper(this, tracer(), _maxRecursiveCallByteCodeSizeEstimate);
      TR_EstimateCodeSize *ecs = ecsWrapper.getCodeEstimator();

      bool possiblyVeryHotLargeCallee = false;
      bool wouldBenefitFromInlining = false;

      TR_CallTarget *calltarget = callsite->getTarget(k);

      //for partial inlining:
      calltarget->_originatingBlock = callsite->_callNodeTreeTop->getEnclosingBlock();


      heuristicTrace(tracer(),"222 Weighing Call Target %p (node = %p)",calltarget,callsite->_callNode);

      if (calltarget->_calleeSymbol && calltarget->_calleeSymbol->getResolvedMethod() &&
            comp()->isGeneratedReflectionMethod(calltarget->_calleeSymbol->getResolvedMethod()) &&
            !comp()->isGeneratedReflectionMethod(comp()->getCurrentMethod()))
         return;

      if (calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_lang_Class_newInstance ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_util_Arrays_fill ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_util_Arrays_equals ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_lang_String_equals ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::sun_io_ByteToCharSingleByte_convert ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::sun_io_ByteToCharDBCS_EBCDIC_convert ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::sun_io_CharToByteSingleByte_convert ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::sun_io_ByteToCharSingleByte_JITintrinsicConvert ||
            calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_math_BigDecimal_subMulAddAddMulSetScale)
         {
         //This resetting of visit count is safe to do because all nodes and blocks in  Estimate Code Size die once ecs returns
         vcount_t origVisitCount = comp()->getVisitCount();

         ecs->calculateCodeSize(calltarget, callStack);
         //This resetting of visit count is safe to do because all nodes and blocks in  Estimate Code Size die once ecs returns
         comp()->setVisitCount(origVisitCount);

         if(calltarget->_isPartialInliningCandidate && calltarget->_partialInline)
            calltarget->_partialInline->setCallNodeTreeTop(callsite->_callNodeTreeTop);
         heuristicTrace(tracer(),"Setting size to 10 for recognized call target at node %p",calltarget->_myCallSite->_callNode);
         size = 10;
         }
      else
         {
         if (currentBlockHasExceptionSuccessors && ecs->aggressivelyInlineThrows())
            {
            _maxRecursiveCallByteCodeSizeEstimate <<= 3;
            heuristicTrace(tracer(),"Setting  _maxRecursiveCallByteCodeSizeEstimate to %d because current block has exception successors and want to aggressively inline throws 1.",_maxRecursiveCallByteCodeSizeEstimate);
            }
         if (ecs->aggressivelyInlineThrows())
            _EDODisableInlinedProfilingInfo = true;

         //This resetting of visit count is safe to do because all nodes and blocks in  Estimate Code Size die once ecs returns
         vcount_t origVisitCount = comp()->getVisitCount();

         bool inlineit = ecs->calculateCodeSize(calltarget, callStack);
         //This resetting of visit count is safe to do because all nodes and blocks in  Estimate Code Size die once ecs returns
         comp()->setVisitCount(origVisitCount);


         debugTrace(tracer()," Original ecs size = %d, _maxRecursiveCallByteCodeSizeEstimate = %d ecs _realSize = %d optimisticSize = %d inlineit = %d error = %s ecs.sizeThreshold = %d",
                              size,_maxRecursiveCallByteCodeSizeEstimate,ecs->getSize(),ecs->getOptimisticSize(),inlineit,ecs->getError(),ecs->getSizeThreshold());

         size = ecs->getSize();

         if (!inlineit && !callMustBeInlinedRegardlessOfSize(callsite))
            {
            if (isWarm(comp()))
               {
               if (comp()->isServerInlining())
                  {
                  if (callsite->_callNode->getInlinedSiteIndex() < 0) //Ensures setWarmCallGraphTooBig is only called for methods with inline index -1 (indexes >=0 can happen when inliner is called after inlining has already occured
                     comp()->getCurrentMethod()->setWarmCallGraphTooBig (callsite->_callNode->getByteCodeInfo().getByteCodeIndex(), comp());
                  else
                     heuristicTrace(tracer(),"Not calling setWarmCallGraphTooBig on callNode %p because it is not from method being compiled bc index %d inlinedsiteindex %d",callsite->_callNode,callsite->_callNode->getByteCodeInfo().getByteCodeIndex(),callsite->_callNode->getInlinedSiteIndex());
                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(),"inliner: Marked call as warm callee too big: %d > %d: %s\n", size, ecs->getSizeThreshold(), tracer()->traceSignature(calltarget->_calleeSymbol));
               //printf("inliner: Marked call as warm callee too big: %d > %d: %s\n", nonRecursiveSize, sizeThreshold, calleeSymbol->signature(trMemory()));
                  }
               }
            tracer()->insertCounter(ECS_Failed,calltarget->_myCallSite->_callNodeTreeTop);
            heuristicTrace(tracer(),"Not Adding Call Target %p to list of targets to be inlined");
            if (comp()->cg()->traceBCDCodeGen())
               {
               traceMsg(comp(), "q^q : failing to inline %s into %s (callNode %p on line_no=%d) due to code size\n",
                  tracer()->traceSignature(calltarget->_calleeSymbol),tracer()->traceSignature(callStack->_methodSymbol),
                  callsite->_callNode,comp()->getLineNumber(callsite->_callNode));
               }
            continue;
            }

         if (calltarget->_isPartialInliningCandidate && calltarget->_partialInline)
            calltarget->_partialInline->setCallNodeTreeTop(callsite->_callNodeTreeTop);

         heuristicTrace(tracer(),"WeighCallSite: For Target %p node %p signature %s, estimation returned a size of %d",
                                  calltarget,calltarget->_myCallSite->_callNode,tracer()->traceSignature(calltarget),size);

         if (currentBlockHasExceptionSuccessors && ecs->aggressivelyInlineThrows())
            {
            _maxRecursiveCallByteCodeSizeEstimate >>= 3;
            size >>= 3;
            size = std::max<uint32_t>(1, size);

            heuristicTrace(tracer(),"Setting size to %d because current block has exception successors and want to aggressively inline throws 2",size);
            }
         if (callMustBeInlinedRegardlessOfSize(calltarget->_myCallSite))
            {
            heuristicTrace(tracer(), "calltarget->_fullSize: %d size: %d", calltarget->_fullSize, size);
            size = 0;
            heuristicTrace(tracer(), "Setting size to %d because call is dominate hot based on PDF", size);
            }


         wouldBenefitFromInlining = false;
         possiblyVeryHotLargeCallee = false;
         if ((((comp()->getMethodHotness() == veryHot) &&
               comp()->isProfilingCompilation()) ||
               (comp()->getMethodHotness() == scorching)) &&
               (size > _maxRecursiveCallByteCodeSizeEstimate/2))
            possiblyVeryHotLargeCallee = true;

         if (calltarget->_calleeSymbol->isSynchronised())
            {
            size >>= 1; // could help gvp
            heuristicTrace(tracer(),"Setting size to %d because call is Synchronized",size);
            if (comp()->getMethodHotness() >= hot)
               {
               size >>= 1; // could help escape analysis as well
               heuristicTrace(tracer(),"Setting size to %d because call is Synchronized and also hot",size);
               }
            wouldBenefitFromInlining = true;
            }

         if (strstr(calltarget->_calleeSymbol->signature(trMemory()),"BigDecimal.add("))
            {
            size >>=2;
            heuristicTrace(tracer(),"Setting size to %d because call is BigDecimal.add",size);
            }

         if (isHot(comp()))
            {
            TR_ResolvedMethod *m = calltarget->_calleeSymbol->getResolvedMethod();
            char *sig = "toString";
            if (strncmp(m->nameChars(), sig, strlen(sig)) == 0)
               {
               size >>= 1;
               heuristicTrace(tracer(),"Setting size to %d because call is toString and compile is hot",size);
               }
            else
               {
               sig = "multiLeafArrayCopy";
               if (strncmp(m->nameChars(), sig, strlen(sig)) == 0)
                  {
                  size >>= 1;
                  heuristicTrace(tracer(),"Setting size to %d because call is multiLeafArrayCopy and compile is hot",size);
                  }
               }

            if (calltarget->_calleeSymbol->getRecognizedMethod() == TR::java_math_BigDecimal_valueOf)
               {
               size >>= 2;
               heuristicTrace(tracer(),"Setting size to %d because call is BigDecimal_valueOf and compile is hot",size);
               }
            }

          int32_t frequency1 = 0, frequency2 = 0;
          int32_t origSize = size;
          bool isCold;
          TR::TreeTop *callNodeTreeTop = calltarget->_myCallSite->_callNodeTreeTop;
          if (callNodeTreeTop)
            {
            // HACK: Get frequency from both sources, and use both.  You're
            // only cold if you're cold according to both.

            frequency1 = comp()->convertNonDeterministicInput(comp()->fej9()->getIProfilerCallCount(callsite->_callNode->getByteCodeInfo(), comp()), MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT, randomGenerator(), 0);
            TR::Block * block = callNodeTreeTop->getEnclosingBlock();
            frequency2 = comp()->convertNonDeterministicInput(block->getFrequency(), MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT, randomGenerator(), 0);

            TR::TreeTop *tt = callNodeTreeTop;
            while (tt && (frequency2 == -1))
               {
               while (tt->getNode()->getOpCodeValue() != TR::BBStart) tt = tt->getPrevTreeTop();

               TR::Block *block = NULL;
               if (tt) block = tt->getNode()->getBlock();
               if (block && tt->getNode()->getInlinedSiteIndex()<0)
                  {
                  frequency2 = comp()->convertNonDeterministicInput(block->getFrequency(), MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT, randomGenerator(), 0);
                  }

               tt = tt->getPrevTreeTop();
               }

            if ((frequency1 <= 0) && ((0 <= frequency2) &&  (frequency2 <= MAX_COLD_BLOCK_COUNT)))
               {
               isCold = true;
               }
            // For optServer in hot/scorching I want the old thresholds 1000 0 0 (high degree of inlining)
            // For optSever in warm I want the new thresholds 9000 5000 1500
            // For noServer I want no change for high frequency but inhibit inlining in cold blocks ==> 10000 5000 1500
            if (TR::isJ9() && !comp()->getMethodSymbol()->doJSR292PerfTweaks() && calltarget->_calleeMethod &&
                !alwaysWorthInlining(calltarget->_calleeMethod, callsite->_callNode))
               {
               int32_t maxFrequency = MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT;
               int32_t borderFrequency = 9000;
               int32_t coldBorderFrequency = 5000;
               int32_t veryColdBorderFrequency = 1500;
               if (comp()->isServerInlining())
                  {
                  if (comp()->getOption(TR_DisableConservativeInlining) ||
                      (comp()->getOptLevel() >= hot) ||
                      getJ9InitialBytecodeSize(calltarget->_calleeMethod, 0, comp()) < comp()->getOptions()->getAlwaysWorthInliningThreshold())// use old thresholds
                     {
                     borderFrequency = 1000;
                     coldBorderFrequency = 0;
                     veryColdBorderFrequency = 0;
                     }
                  }
               else
                  {
                  borderFrequency = 10000;
                  if (comp()->getOptLevel() >= hot)
                     {
                     coldBorderFrequency = 0;
                     veryColdBorderFrequency = 0;
                     }
                  }

               // Did the user specify specific values? If so, use those
               if (comp()->getOptions()->getInlinerCGBorderFrequency() >= 0)
                  borderFrequency = comp()->getOptions()->getInlinerCGBorderFrequency();
               if (comp()->getOptions()->getInlinerCGColdBorderFrequency() >= 0)
                  coldBorderFrequency = comp()->getOptions()->getInlinerCGColdBorderFrequency();
               if (comp()->getOptions()->getInlinerCGVeryColdBorderFrequency() >= 0)
                  veryColdBorderFrequency = comp()->getOptions()->getInlinerCGVeryColdBorderFrequency();

               if (comp()->trace(OMR::inlining))
                  heuristicTrace(tracer(),"WeighCallSite: Considering shrinking call %p with frequency %d\n", callsite->_callNode, frequency2);

               bool largeCompiledCallee = !comp()->getOption(TR_InlineVeryLargeCompiledMethods) &&
                                          isLargeCompiledMethod(calltarget->_calleeMethod, size, frequency2);
               if (largeCompiledCallee)
                  {
                  size = size*TR::Options::_inlinerVeryLargeCompiledMethodAdjustFactor;
                  }
               else if (frequency2 > borderFrequency)
                  {
                  float factor = (float)(maxFrequency-frequency2)/(float)maxFrequency;
                  factor = std::max(factor, 0.4f);

                  float avgMethodSize = (float)size/(float)ecs->getNumOfEstimatedCalls();
                  float numCallsFactor = (float)(avgMethodSize)/110.0f;

                  numCallsFactor = std::max(factor, 0.1f);

                  if (size > 100)
                     {
                     size = (int)((float)size * factor * numCallsFactor);
                     if (size < 100) size = 100;
                     }
                  else
                     {
                     size = (int)((float)size * factor * numCallsFactor);
                     }
                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(), "WeighCallSite: Adjusted call-graph size for call node %p, from %d to %d\n", callsite->_callNode, origSize, size);
                  }
               else if ((frequency2 > 0) && (frequency2 < veryColdBorderFrequency)) // luke-warm block
                  {
                  float factor = (float)frequency2 / (float)maxFrequency;
                  //factor = std::max(factor, 0.1f);
                  size = (int)((float)size / (factor*factor)); // make the size look bigger to inline less
                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(), "WeighCallSite: Adjusted call-graph size for call node %p, from %d to %d\n", callsite->_callNode, origSize, size);
                  }
               else if ((frequency2 >= 0) && (frequency2 < coldBorderFrequency)) // very cold block
                  {
                  //to avoid division by zero crash. Semantically  freqs of 0 and 1 should be pretty close given maxFrequency of 10K
                  int adjFrequency2 = frequency2 ? frequency2 : 1;
                  float factor = (float)adjFrequency2 / (float)maxFrequency;
                  //factor = std::max(factor, 0.1f);
                  size = (int)((float)size / factor);


                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(),"WeighCallSite: Adjusted call-graph size for call node %p, from %d to %d\n", callsite->_callNode, origSize, size);
                  }
               else
                  {
                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(),"WeighCallSite: Not adjusted call-graph size for call node %p, size %d\n", callsite->_callNode, origSize);
                  }
               }
            }

         bool toInline = getPolicy()->tryToInline(calltarget, callStack, true);
         heuristicTrace(tracer(),"WeighCallSite: For Target %p node %p, size after size mangling %d",calltarget,calltarget->_myCallSite->_callNode,size);

         if (!toInline && !forceInline(calltarget) && (size > _maxRecursiveCallByteCodeSizeEstimate || ecs->recursedTooDeep() == true))
            {
            if (isWarm(comp()))
               {
               if (comp()->isServerInlining())
                  {
                  if (callsite->_callNode->getInlinedSiteIndex() < 0) //Ensures setWarmCallGraphTooBig is only called for methods with inline index -1 (indexes >=0 can happen when inliner is called after inlining has already occured
                     comp()->getCurrentMethod()->setWarmCallGraphTooBig (callsite->_callNode->getByteCodeInfo().getByteCodeIndex(), comp());
                  else
                     heuristicTrace(tracer(),"Not calling setWarmCallGraphTooBig on callNode %p because it is not from method being compiled bc index %d inlinedsiteindex %d",callsite->_callNode,callsite->_callNode->getByteCodeInfo().getByteCodeIndex(),callsite->_callNode->getInlinedSiteIndex());
                  if (comp()->trace(OMR::inlining))
                     heuristicTrace(tracer(),"inliner: Marked call as warm callee too big: %d > %d: %s\n", size, ecs->getSizeThreshold(), tracer()->traceSignature(calltarget->_calleeSymbol));
                  }
               //printf("inliner: Marked call as warm callee too big: %d > %d: %s\n", nonRecursiveSize, sizeThreshold, calleeSymbol->signature(trMemory()));
               }
            tracer()->insertCounter(Callee_Too_Many_Bytecodes,calltarget->_myCallSite->_callNodeTreeTop);
            TR::Options::INLINE_calleeToDeep ++;
            if (comp()->trace(OMR::inlining))
               traceMsg(comp(), "inliner: size exceeds call graph size threshold: %d > %d: %s\n", size, _maxRecursiveCallByteCodeSizeEstimate, tracer()->traceSignature(calltarget->_calleeSymbol));

            callsite->removecalltarget(k,tracer(),Exceeds_Size_Threshold);
            k--;
            continue;
            }
         }

      if (comp()->getOption(TR_DisableNewInliningInfrastructure))
         calltarget->_isPartialInliningCandidate = false;

      int32_t weight = size;
      int32_t origWeight = weight;

      heuristicTrace(tracer(),"Beginning of Weight Calculation. Setting weight to %d",weight);


      if (!comp()->getOption(TR_DisableInlinerFanIn))
         {
         j9inlinerPolicy->adjustFanInSizeInWeighCallSite (weight,
                                         size,
                                         calltarget->_calleeSymbol->getResolvedMethod(),
                                         callsite->_callerResolvedMethod,
                                         callsite->_callNode->getByteCodeIndex());
         }

      if (callStack->_inALoop)
         {
         weight >>= 2;    // divide by 4
         if(getPolicy()->aggressiveSmallAppOpts())
            weight >>=3;
         heuristicTrace(tracer(),"Setting weight to %d because call is in a loop.",weight);
         }
      else if (!callStack->_alwaysCalled)
         {
         if ( getPolicy()->aggressiveSmallAppOpts() && comp()->getMethodSymbol()->getRecognizedMethod() == TR::java_util_GregorianCalendar_computeFields)
             {
             TR::TreeTop *callNodeTreeTop = calltarget->_myCallSite->_callNodeTreeTop;
             int32_t adjustment = 5;
             if (callNodeTreeTop)
                {
                TR::Block * block = callNodeTreeTop->getEnclosingBlock();
                int32_t frequency = block->getFrequency();
                if(frequency > 1000)
                   adjustment = 10;
                else
                   adjustment = (frequency*10)/10000;
                }

             adjustment = std::max(5, adjustment);
             if (adjustment == 0)
                adjustment = 5;
             weight = (weight * 10) / adjustment;
             }
         heuristicTrace(tracer(),"Setting weight to %d because call is not always called.",weight);
         }

      int32_t weightBeforeLookingForBenefits = weight;

      bool isLambdaFormGeneratedMethod = comp()->fej9()->isLambdaFormGeneratedMethod(calltarget->_calleeMethod);
      if ((calltarget->_calleeMethod->convertToMethod()->isArchetypeSpecimen() && calltarget->_calleeMethod->getMethodHandleLocation()) || isLambdaFormGeneratedMethod)
         {
         static char *methodHandleThunkWeightFactorStr = feGetEnv("TR_methodHandleThunkWeightFactor");
         static int32_t methodHandleThunkWeightFactor = methodHandleThunkWeightFactorStr? atoi(methodHandleThunkWeightFactorStr) : 10;
         // MethodHandle thunks benefit a great deal from inlining so let's encourage them.
         weight /= methodHandleThunkWeightFactor;
         heuristicTrace(tracer(),"Setting weight to %d because callee is MethodHandle thunk.",weight);
         }


      TR_LinkHead<TR_ParameterMapping> map;
      if(!((TR_J9InlinerPolicy *)getPolicy())->validateArguments(calltarget,map))    //passing map by reference
         {
         continue;      // arguments are not valid for this calltarget
         }

      weight = applyArgumentHeuristics(map,weight, calltarget);

      if (calltarget->_calleeMethod->isDAAWrapperMethod())
         {
         weight = 1;
         heuristicTrace(tracer(),"Setting DAA wrapper methods weights to minimum(%d).", weight);
         }

#ifdef ENABLE_SPMD_SIMD
         if (/*calltarget->_calleeSymbol->getRecognizedMethod() == TR::com_ibm_simt_SPMDKernel_execute ||*/
             calltarget->_calleeSymbol->getRecognizedMethod() == TR::com_ibm_simt_SPMDKernel_kernel
             )
            {
            weight = 1;
            traceMsg(comp(), "Setting SIMD kernel methods weights to minimum(%d) node %p.", weight, callsite->_callNode);
            }
#endif

      if (weightBeforeLookingForBenefits != weight)
         wouldBenefitFromInlining = true;

      if (possiblyVeryHotLargeCallee &&  !wouldBenefitFromInlining)
         {
         // Increase the possibility of callee reaching scorching with profiling done in it since
         // it is possible we collect more useful profile data if it is not inlined
         // (since the profiling budget for the caller might get exhausted on some
         //  other long running loop earlier than the place where the callee is inlined)
         // Only do this if the current method is very hot (so callee will likely be very hot if it
         // is large) and there are no obvious benefits from inlining the large callee.
         // Important in _228_jack since we do not want to inline the Move method into getNextTokenFromStream
         //

         if ( getPolicy()->aggressiveSmallAppOpts() && comp()->getMethodSymbol()->getRecognizedMethod() == TR::java_util_GregorianCalendar_computeFields)
            {

            TR::TreeTop *callNodeTreeTop = calltarget->_myCallSite->_callNodeTreeTop;
            int32_t adjustment = 5;
            if (callNodeTreeTop)
               {
               TR::Block * block = callNodeTreeTop->getEnclosingBlock();
               int32_t frequency = block->getFrequency();
               if(frequency > 1000)
                  adjustment = 10;
               else
                  adjustment = (frequency*10)/10000;
               }

            adjustment = std::max(5, adjustment);
            if (adjustment == 0)
               adjustment = 5;
            weight = (weight * 10) / adjustment;

               }
         else
            weight <<= 1;
         heuristicTrace(tracer(),"Setting weight to %d because method is possibly a very hot and large callee", weight);
         }

      if (ecs->isLeaf())
         {
         weight -= 4;
         heuristicTrace(tracer(),"Setting weight to %d because method is a leaf method", weight);
         }

      static char *xyz = feGetEnv("TR_MaxWeightReduction");
      uint32_t maxWeightReduction = xyz ? atoi(xyz) : 8;
      if (weight < (origWeight/maxWeightReduction))
         {
         weight = origWeight/8;
         heuristicTrace(tracer(),"Setting weight to %d because weight is less than originalWeight/8", weight);
         }


      float callGraphAdjustedWeight = 0.0f;
      int32_t callGraphWeight       = -1;
      TR_ValueProfileInfoManager * profileManager = TR_ValueProfileInfoManager::get(comp());
      bool callGraphEnabled = profileManager->isCallGraphProfilingEnabled(comp()) && !_EDODisableInlinedProfilingInfo;

      if (callGraphEnabled)
         callGraphAdjustedWeight = profileManager->getAdjustedInliningWeight(calltarget->_myCallSite->_callNode, weight, comp());

      //There's (almost) no way to get out of adding the call site to the list of call sites.
      //Exceptions:  1) you blow your budget
      //             2) reflection
      // We only WeighCallSite() for the original method.  So we will only have a list of call sites of top level methods.

      calltarget->_size = size;
      calltarget->_weight = (int32_t)(weight/calltarget->_frequencyAdjustment);
      calltarget->_callGraphAdjustedWeight = callGraphAdjustedWeight/calltarget->_frequencyAdjustment;

      heuristicTrace(tracer(),"WeighCallSite:  Adding call target %p node %p with calleeSymbol = %p to list of call sites with guard %p kind = %d"
                               "type = %d weight = %d (adjusted by frequencyAdjustment of %d) callGraphAdjustedWeight = %d",
                               calltarget,calltarget->_myCallSite->_callNode,calltarget->_calleeSymbol,calltarget->_guard,calltarget->_guard->_kind,
                               calltarget->_guard->_type,calltarget->_weight,calltarget->_frequencyAdjustment, calltarget->_callGraphAdjustedWeight);

      TR_CallTarget *calltargetiterator  = _callTargets.getFirst(), * prevTarget = 0;
      bool dontinsert = false;
      if (dontAddCalls == true)
         {
//         printf("Would have inserted bogus call to list from walkcallsites\n");fflush(stdout);
         dontinsert=true;
         }
      for (; calltargetiterator; prevTarget = calltargetiterator, calltargetiterator=calltargetiterator->getNext())
         if (callGraphEnabled)
            {
            if (callGraphAdjustedWeight < calltargetiterator->_callGraphAdjustedWeight)
               {
               _callTargets.insertAfter(prevTarget, calltarget);
               dontinsert=true;
               break;
               }
            else if ((callGraphAdjustedWeight == calltargetiterator->_callGraphAdjustedWeight) &&
                     (weight < calltargetiterator->_weight))
               {
               _callTargets.insertAfter(prevTarget, calltarget);
               //return;
               dontinsert=true;
               break;
               }
            }
         else if (weight < calltargetiterator->_weight)
            {
            _callTargets.insertAfter(prevTarget, calltarget);
            //return;
            dontinsert=true;
            break;
            }
      if(!dontinsert)
         _callTargets.insertAfter(prevTarget, calltarget);
      } //end for loop over call targets


   heuristicTrace(tracer(),"^^^ Done Weighing of all targets in CallSite %p callnode %p\n",callsite, callsite->_callNode);
   }

bool TR_MultipleCallTargetInliner::inlineSubCallGraph(TR_CallTarget* calltarget)
   {
   TR_J9InlinerPolicy *j9inlinerPolicy = (TR_J9InlinerPolicy *) getPolicy();
   /*
    * keep the target if it meets either of the following condition:
    * 1. It's a JSR292 related method. This condition allows inlining method handle thunk chain without inlining the leaf java method.
    * 2. It's force inline target
    */
   if (j9inlinerPolicy->isJSR292Method(calltarget->_calleeMethod)
       || forceInline(calltarget))
      {
      for (TR_CallSite* callsite = calltarget->_myCallees.getFirst(); callsite ; callsite = callsite->getNext())
         {
         for (int32_t i = 0 ; i < callsite->numTargets() ; i++)
            inlineSubCallGraph(callsite->getTarget(i));
         }
      return true;
      }

   calltarget->_myCallSite->removecalltarget(calltarget, tracer(), Trimmed_List_of_Callees);
   return false;
   }

void TR_MultipleCallTargetInliner::processChoppedOffCallTargets(TR_CallTarget *lastTargetToInline, TR_CallTarget* firstChoppedOffcalltarget, int estimatedNumberOfNodes)
   {
   if (firstChoppedOffcalltarget)
      {
      TR_CallTarget * calltarget = firstChoppedOffcalltarget;
      for (; calltarget; calltarget = calltarget->getNext())
         {
         if (inlineSubCallGraph(calltarget))
            {
            generateNodeEstimate myEstimate;
            recursivelyWalkCallTargetAndPerformAction(calltarget, myEstimate);
            estimatedNumberOfNodes += myEstimate.getNodeEstimate();
            /*
             * ForceInline targets and JSR292 methods should always be inlined regarless of budget. However, with
             * inlining methodhandle chain in normal inlining, the number of nodes can be tremendous resulting in
             * compilations, especially those with higher opt level, eating up too much CPU time. The heuristic here
             * is added to prevent compilations consuming too much CPU.
             */
            static bool dontAbortCompilationEvenWithLargeInliningNodesEstimation = feGetEnv("TR_DontAbortCompilationEvenWithLargeInliningNodesEstimation") ? true: false;
            if (!dontAbortCompilationEvenWithLargeInliningNodesEstimation && estimatedNumberOfNodes > 50000 && comp()->getMethodHotness() >= hot)
                  comp()->failCompilation<TR::ExcessiveComplexity>("too many nodes if forced inlining targets are included");

            if (lastTargetToInline)
               lastTargetToInline->setNext(calltarget);
            else
               _callTargets.setFirst(calltarget);
            lastTargetToInline = calltarget;
            }
         }
      }

   if (lastTargetToInline)
      lastTargetToInline->setNext(NULL);
   else _callTargets.setFirst(NULL);
   }

//Note, this function is shared by all FE's.  If you are changing the heuristic for your FE only, you need to push this method into the various FE's FEInliner.cpp file.
int32_t TR_MultipleCallTargetInliner::scaleSizeBasedOnBlockFrequency(int32_t bytecodeSize, int32_t frequency, int32_t borderFrequency, TR_ResolvedMethod * calleeResolvedMethod, TR::Node *callNode, int32_t coldBorderFrequency)
   {
   int32_t maxFrequency = MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT;

   bool largeCompiledCallee = !comp()->getOption(TR_InlineVeryLargeCompiledMethods) &&
                              isLargeCompiledMethod(calleeResolvedMethod, bytecodeSize, frequency);
   if (largeCompiledCallee)
      {
      bytecodeSize = bytecodeSize * TR::Options::_inlinerVeryLargeCompiledMethodAdjustFactor;
      }
   else if (frequency > borderFrequency)
      {
      int32_t oldSize = 0;
      if (comp()->trace(OMR::inlining))
      oldSize = bytecodeSize;

      float factor = (float)(maxFrequency-frequency)/(float)maxFrequency;
      factor = getScalingFactor(factor);


      bytecodeSize = (int32_t)((float)bytecodeSize * factor);
      if (bytecodeSize < 10) bytecodeSize = 10;

      heuristicTrace(tracer(),"exceedsSizeThreshold (mct): Scaled down size for call from %d to %d", oldSize, bytecodeSize);
      }
   else if (frequency < coldBorderFrequency)
      {
      int32_t oldSize = 0;
      if (comp()->trace(OMR::inlining))
      oldSize = bytecodeSize;

      //to avoid division by zero crash. Semantically  freqs of 0 and 1 should be pretty close given maxFrequency of 10K
      int adjFrequency = frequency ? frequency : 1;

      float factor = (float)adjFrequency / (float)maxFrequency;
      bytecodeSize = (int32_t)((float)bytecodeSize / (factor*factor));

      heuristicTrace(tracer(),"exceedsSizeThreshold: Scaled up size for call from %d to %d", oldSize, bytecodeSize);
      }
   return bytecodeSize;
   }


bool TR_MultipleCallTargetInliner::isLargeCompiledMethod(TR_ResolvedMethod *calleeResolvedMethod, int32_t bytecodeSize, int32_t callerBlockFrequency)
   {
   TR_OpaqueMethodBlock* methodCallee = calleeResolvedMethod->getPersistentIdentifier();
   if (!calleeResolvedMethod->isInterpreted())
      {
      TR_PersistentJittedBodyInfo * bodyInfo = ((TR_ResolvedJ9Method*) calleeResolvedMethod)->getExistingJittedBodyInfo();
      if ((comp()->getMethodHotness() <= warm))
         {
         if (bodyInfo &&
             (bodyInfo->getHotness() >= warm))
            {
            // hot and scorching bodies should never be inlined to warm or cooler bodies
            if (bodyInfo->getHotness() >= hot)
               {
               return true;
               }

            // Allow inlining of big methods into high frequency blocks
            if (callerBlockFrequency > comp()->getOptions()->getLargeCompiledMethodExemptionFreqCutoff())
               return false;

            int32_t veryLargeCompiledMethodThreshold = comp()->getOptions()->getInlinerVeryLargeCompiledMethodThreshold();
            int32_t veryLargeCompiledMethodFaninThreshold = comp()->getOptions()->getInlinerVeryLargeCompiledMethodFaninThreshold();
            // Subdue inliner in low frequency blocks
            if (callerBlockFrequency > 0)
               {
               if ((2 * callerBlockFrequency) < comp()->getOptions()->getLargeCompiledMethodExemptionFreqCutoff())
                  {
                  veryLargeCompiledMethodThreshold = 100;
                  veryLargeCompiledMethodFaninThreshold = 0;
                  }
               }

            uint32_t numCallers = 0, totalWeight = 0;
            ((TR_ResolvedJ9Method *) calleeResolvedMethod)->getFaninInfo(&numCallers, &totalWeight);
            if ((numCallers > veryLargeCompiledMethodFaninThreshold) &&
                (bytecodeSize > veryLargeCompiledMethodThreshold))
               {
               return true;
               }
            }
         }
      else if (comp()->getMethodHotness() < scorching)
         {
         // scorching compiled methods should not be inlined in compiles below scorching
         // unless we are preparing to upgrade the compile and need profiling info
         if (bodyInfo &&
             (bodyInfo->getHotness() >= scorching) &&
             !(comp()->isProfilingCompilation() && comp()->getMethodHotness() == veryHot))
            {
            return true;
            }
         }
      }
   return false;
   }


bool
TR_MultipleCallTargetInliner::exceedsSizeThreshold(TR_CallSite *callSite, int bytecodeSize, TR::Block *block, TR_ByteCodeInfo & bcInfo, int32_t numLocals, TR_ResolvedMethod * callerResolvedMethod, TR_ResolvedMethod * calleeResolvedMethod,TR::Node *callNode, bool allConsts)
   {
   if (alwaysWorthInlining(calleeResolvedMethod, callNode))
      return false;

   TR_J9InlinerPolicy *j9InlinerPolicy = (TR_J9InlinerPolicy *)getPolicy();
   static char *polymorphicCalleeSizeThresholdStr = feGetEnv("TR_InlinerPolymorphicConservatismCalleeSize");
   int polymorphicCalleeSizeThreshold = polymorphicCalleeSizeThresholdStr ? atoi(polymorphicCalleeSizeThresholdStr) : 10;
   static char *polymorphicRootSizeThresholdStr = feGetEnv("TR_InlinerPolymorphicConservatismRootSize");
   int polymorphicRootSizeThreshold = polymorphicRootSizeThresholdStr ? atoi(polymorphicRootSizeThresholdStr) : 30;
   static char *trustedInterfacePattern = feGetEnv("TR_TrustedPolymorphicInterfaces");
   static TR::SimpleRegex *trustedInterfaceRegex = trustedInterfacePattern ? TR::SimpleRegex::create(trustedInterfacePattern) : NULL;
   // we need to be conservative about inlining potentially highly polymorphic interface calls for
   // functional frameworks like scala - we limit this to hot and above
   // if the callsite is highly polymorphic but the following conditions are meet, still inline the callee
   // 1. the compiling method is scorching
   // 2. the callee is scorching OR queued for veryhot/scorching compile
   int32_t outterMethodSize = getJ9InitialBytecodeSize(callSite->_callerResolvedMethod, 0, comp());
   if (comp()->getMethodHotness() > warm && callSite->isInterface()
       && bytecodeSize > polymorphicCalleeSizeThreshold
       && outterMethodSize > polymorphicRootSizeThreshold
       && ((bytecodeSize * 100) / outterMethodSize) < 6
       && (!callSite->_ecsPrexArgInfo || !callSite->_ecsPrexArgInfo->get(0) || !callSite->_ecsPrexArgInfo->get(0)->getClass())
       && comp()->fej9()->maybeHighlyPolymorphic(comp(), callSite->_callerResolvedMethod, callSite->_cpIndex, callSite->_interfaceMethod, callSite->_receiverClass)
       && (!trustedInterfaceRegex || !TR::SimpleRegex::match(trustedInterfaceRegex, callSite->_interfaceMethod->signature(trMemory()), false)))
      {
      TR_PersistentJittedBodyInfo *bodyInfo = NULL;
      if (!calleeResolvedMethod->isInterpretedForHeuristics() && !calleeResolvedMethod->isJITInternalNative())
         {
         bodyInfo = ((TR_ResolvedJ9Method*) calleeResolvedMethod)->getExistingJittedBodyInfo();
         }
      if (((!bodyInfo && !calleeResolvedMethod->isInterpretedForHeuristics() && !calleeResolvedMethod->isJITInternalNative()) //jitted method without bodyInfo must be scorching
         || (bodyInfo && bodyInfo->getHotness() == scorching)
         || comp()->fej9()->isQueuedForVeryHotOrScorching(calleeResolvedMethod, comp()))
         && (comp()->getMethodHotness() == scorching))
         debugTrace(tracer(), "### inline this callee because the compiling method and callee are both scorching even though it's potentially highly polymorphic callsite initialCalleeSymbol = %s callerResolvedMethod = %s calleeResolvedMethod = %s\n",
            tracer()->traceSignature(callSite->_interfaceMethod), tracer()->traceSignature(callerResolvedMethod), tracer()->traceSignature(calleeResolvedMethod));
      else
         {
         debugTrace(tracer(), "### exceeding size threshold for potentially highly polymorphic callsite initialCalleeSymbol = %s callerResolvedMethod = %s calleeResolvedMethod = %s\n",
                 tracer()->traceSignature(callSite->_interfaceMethod), tracer()->traceSignature(callerResolvedMethod), tracer()->traceSignature(calleeResolvedMethod));
         TR::DebugCounter::incStaticDebugCounter(comp(), TR::DebugCounter::debugCounterName(comp(), "inliner.highlyPolymorphicFail/(%s)/%s/(%s)/sizes=%d.%d", comp()->signature(), comp()->getHotnessName(comp()->getMethodHotness()), callSite->_interfaceMethod->signature(trMemory()), bytecodeSize, outterMethodSize));
         return true;
         }
      }

   if (comp()->getMethodHotness() > warm && !comp()->isServerInlining())
      return TR_InlinerBase::exceedsSizeThreshold(callSite, bytecodeSize, block, bcInfo,numLocals,callerResolvedMethod,calleeResolvedMethod,callNode,allConsts);

   heuristicTrace(tracer(),"### Checking multiple call target inliner sizeThreshold. bytecodeSize = %d, block = %p, numLocals = %p, callerResolvedMethod = %s,"
                            " calleeResolvedMethod = %s opt server is %d methodHotness = %d warm = %d\n",
                            bytecodeSize,block,numLocals,tracer()->traceSignature(callerResolvedMethod),tracer()->traceSignature(calleeResolvedMethod), comp()->isServerInlining(),comp()->getMethodHotness(), warm);

   int32_t origbytecodeSize=bytecodeSize;
   bool isCold = false;
   int32_t frequency1 = 0, frequency2 = 0;


   // In the old days we used 2000,50 for optServer, 2500,1000 for noOptServer
   // Now we want to use 6000, 1500 for optServer

   if (block)
      {
      int32_t borderFrequency;
      int32_t veryColdBorderFrequency;

      getBorderFrequencies(borderFrequency, veryColdBorderFrequency, calleeResolvedMethod, callNode);


     // HACK: Get frequency from both sources, and use both.  You're
     // only cold if you're cold according to both.

     bool isLambdaFormGeneratedMethod = comp()->fej9()->isLambdaFormGeneratedMethod(callerResolvedMethod);
     // TODO: we should ignore frequency for thunk archetype, however, this require performance evaluation
     bool frequencyIsInaccurate = isLambdaFormGeneratedMethod;

     frequency1 = comp()->convertNonDeterministicInput(comp()->fej9()->getIProfilerCallCount(bcInfo, comp()), MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT, randomGenerator(), 0);
     frequency2 = comp()->convertNonDeterministicInput(block->getFrequency(), MAX_BLOCK_COUNT + MAX_COLD_BLOCK_COUNT, randomGenerator(), 0);
     if (frequency1 > frequency2 && callerResolvedMethod->convertToMethod()->isArchetypeSpecimen())
        frequency2 = frequency1;

     if ((frequency1 <= 0) && ((0 <= frequency2) &&  (frequency2 <= MAX_COLD_BLOCK_COUNT)) &&
        !alwaysWorthInlining(calleeResolvedMethod, callNode) &&
        !frequencyIsInaccurate)
        {
        isCold = true;
        }

     debugTrace(tracer(), "exceedsSizeThreshold: Call with block_%d has frequency1 %d frequency2 %d ", block->getNumber(), frequency1, frequency2);

     if (allowBiggerMethods() &&
         !comp()->getMethodSymbol()->doJSR292PerfTweaks() &&
         calleeResolvedMethod &&
         !frequencyIsInaccurate &&
         !j9InlinerPolicy->isInlineableJNI(calleeResolvedMethod, callNode))
        {
        bytecodeSize = scaleSizeBasedOnBlockFrequency(bytecodeSize,frequency2,borderFrequency, calleeResolvedMethod,callNode,veryColdBorderFrequency);
        }
     else if (getPolicy()->aggressiveSmallAppOpts())
         {
          traceMsg(comp(), "Reached new code 2\n");
          int32_t blockNestingDepth = 1;
          if (_isInLoop)
             {
             char *tmptmp =0;
             if (calleeResolvedMethod)
                tmptmp = TR::Compiler->cls.classSignature(comp(), calleeResolvedMethod->containingClass(), trMemory());
             bool doit = false;

             if (j9InlinerPolicy->aggressivelyInlineInLoops())
                {
                doit = true;
                }

             if (doit && calleeResolvedMethod && !strcmp(tmptmp,"Ljava/math/BigDecimal;"))
                {
                traceMsg(comp(), "Reached code for block nesting depth %d\n", blockNestingDepth);
                if ((_isInLoop || (blockNestingDepth > 1)) &&
                   (bytecodeSize > 10))
                   {
                   if (comp()->trace(OMR::inlining))
                      heuristicTrace(tracer(),"Exceeds Size Threshold: Scaled down size for call block %d from %d to %d\n", block->getNumber(), bytecodeSize, 10);
                   bytecodeSize = 10;
                   }
                }
             else
                heuristicTrace(tracer(),"Omitting Big Decimal method from size readjustment, calleeResolvedMethod = %p tmptmp = %s",calleeResolvedMethod, tmptmp);
             }
         }
       }

 // reduce size if your args are consts
    if (callNode)
       {
       heuristicTrace(tracer(),"In ExceedsSizeThreshold.  Reducing size from %d",bytecodeSize);

       int32_t originalbcSize = bytecodeSize;
       uint32_t numArgs = callNode->getNumChildren();
       bool allconstsfromCallNode = true;

       uint32_t i = callNode->getFirstArgumentIndex();

       if( callNode->getOpCode().isCall() &&
           !callNode->getSymbolReference()->isUnresolved() &&
           callNode->getSymbolReference()->getSymbol()->getMethodSymbol() &&
          !callNode->getSymbolReference()->getSymbol()->castToMethodSymbol()->isHelper() &&
          !callNode->getSymbolReference()->getSymbol()->castToMethodSymbol()->isSystemLinkageDispatch() &&
          !callNode->getSymbolReference()->getSymbol()->castToMethodSymbol()->isStatic() )
                ++i;

       for (; i < numArgs; ++i)
          {

         // printf("callNode = %p\n");fflush(stdout);
         // printf("callNode->getOpCode().isCall() = %p\n");fflush(stdout);
         // printf("callNode->getSymbolReference() = %p\n");fflush(stdout);
         // pr

          TR::Node * arg = callNode->getChild(i);
          if (arg->getOpCode().isLoadConst())
             {
             heuristicTrace(tracer(),"Node %p is load const\n",arg);
             bytecodeSize = bytecodeSize - (bytecodeSize/10);
             }
          else if (arg->getOpCodeValue() == TR::aload && arg->getSymbolReference()->getSymbol()->isConstObjectRef())
             {
             heuristicTrace(tracer(),"Node %p is aload const\n",arg);
             bytecodeSize = bytecodeSize - (bytecodeSize/10);
             }
          else
             {
             heuristicTrace(tracer(),"Node %p is not const\n",arg);
             allconstsfromCallNode = false;
             }
          }


       if (!allconstsfromCallNode)
          bytecodeSize = originalbcSize;

       else if (bytecodeSize < originalbcSize && originalbcSize > 100)
          {
          /*
          char tmpian[1024];
          comp()->fej9()->sampleSignature(calleeResolvedMethod->getPersistentIdentifier(), tmpian, 1024, trMemory());
          printf("R size of bc %d to %d meth %s comp %s\n",originalbcSize,bytecodeSize,tmpian,comp()->signature());
          fflush(stdout);
          */
          }

       heuristicTrace(tracer()," to %d because of const arguments", bytecodeSize);

 /*      if ( bytecodeSize != originalbcSize )
          {
          char tmpian[1024];
          comp()->fej9()->sampleSignature(calleeResolvedMethod->getPersistentIdentifier(), tmpian, 1024, trMemory());
          printf("R size of bc %d to %d meth %s comp %s\n",originalbcSize,bytecodeSize,tmpian,comp()->signature());
          fflush(stdout);
          }
 */
       }
    else if (allConsts)
       {

       int32_t originalbcSize=bytecodeSize;
       heuristicTrace(tracer(),"In ExceedsSizeThreshold.  Reducing size from %d",bytecodeSize);

       int32_t numArgs = calleeResolvedMethod->numberOfExplicitParameters();
       for (int32_t i=0 ; i < numArgs; ++i)
          {
          bytecodeSize = bytecodeSize - (bytecodeSize/10);
          }

       heuristicTrace(tracer()," to %d because of const arguments",bytecodeSize);

  /*
       if (bytecodeSize < originalbcSize && originalbcSize > 100)
       {
          char tmpian[1024];
          comp()->fe()->sampleSignature(calleeResolvedMethod->getPersistentIdentifier(), tmpian, 1024, trMemory());
          printf("R size of bc %d to %d meth %s comp %s\n",originalbcSize,bytecodeSize,tmpian,comp()->signature());
          fflush(stdout);
       }
 */
       }

   static const char *qq;
   static uint32_t min_size = ( qq = feGetEnv("TR_Min_FanIn_Size")) ? atoi(qq) : MIN_FAN_IN_SIZE;
   static const char *q;
   static uint32_t multiplier = ( q = feGetEnv("TR_SizeMultiplier")) ? atoi (q) : SIZE_MULTIPLIER;
   uint32_t calculatedSize = bytecodeSize; //(bytecodeSize - MIN_FAN_IN_SIZE);

   if (!comp()->getOption(TR_DisableInlinerFanIn))  // TODO: make the default for everybody
      {
      // In JIT, having low caller information is equivalent to lack of information.  We want to exclude only cases where we know we have alot of fan-in
      if (j9InlinerPolicy->adjustFanInSizeInExceedsSizeThreshold(bytecodeSize, calculatedSize, calleeResolvedMethod, callerResolvedMethod, bcInfo.getByteCodeIndex()))
         {
         return true;
         }
      }
//   else
//      {
//     calculatedSize=bytecodeSize;
//      }


   if (isCold && (bytecodeSize > _methodInColdBlockByteCodeSizeThreshold))
      {
      if (block)
         {
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/coldCallee/tooManyBytecodes", block->getFirstRealTreeTop());
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/coldCallee/tooManyBytecodes:#bytecodeSize", block->getFirstRealTreeTop(), bytecodeSize);
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/coldCallee/tooManyBytecodes:#excess", block->getFirstRealTreeTop(), bytecodeSize-_methodInColdBlockByteCodeSizeThreshold);
         }
      heuristicTrace(tracer(),"### Exceeds Size Threshold because call is cold and has a bytecodeSize %d > _methodInColdBlockByteCodeSizeThreshold %d", bytecodeSize,_methodInColdBlockByteCodeSizeThreshold);
      return true; // exceeds size threshold
      }

   if(bytecodeSize > _methodInWarmBlockByteCodeSizeThreshold || calculatedSize > _methodInWarmBlockByteCodeSizeThreshold*multiplier)
      {
      if (block)
         {
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/warmCallee/tooManyBytecodes", block->getFirstRealTreeTop());
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/warmCallee/tooManyBytecodes:#bytecodeSize", block->getFirstRealTreeTop(), bytecodeSize);
         TR::DebugCounter::prependDebugCounter(comp(), "inliner.callSites/failed/warmCallee/tooManyBytecodes:#excess", block->getFirstRealTreeTop(), bytecodeSize - _methodInWarmBlockByteCodeSizeThreshold);
         }

      if( bytecodeSize <= _methodInWarmBlockByteCodeSizeThreshold )
         {
         heuristicTrace(tracer(),"### Exceeds Size Threshold because calculatedSize %d > _methodInWarmBlockByteCodeSizeThreshold*multiplier %d (excessive Fan In)",calculatedSize,_methodInWarmBlockByteCodeSizeThreshold*multiplier);

         static const char *r = feGetEnv("TR_PrintFanIn");
         if(r)
            {
            char calleeName2[1024];


            printf("Method %p %s excluded because it has fan in ratio of %f threshold = %d size = %d original = %d.\n",
                 calleeResolvedMethod,
                 comp()->fej9()->sampleSignature(calleeResolvedMethod->getPersistentIdentifier(), calleeName2, 1024, comp()->trMemory()),
                 (float)calculatedSize/(float) bytecodeSize,_methodInWarmBlockByteCodeSizeThreshold*multiplier,calculatedSize,bytecodeSize);

            }
         }
      else
         heuristicTrace(tracer(),"### Exceeds Size Threshold because  bytecodeSize %d > _methodInWarmBlockByteCodeSizeThreshold %d",bytecodeSize,_methodInWarmBlockByteCodeSizeThreshold);

      return true; // Exceeds size threshold
      }

   if (isWarm(comp()))
      {
      if (OWNING_METHOD_MAY_NOT_BE_THE_CALLER && calleeResolvedMethod->owningMethodDoesntMatter())
         {
         // callerResolvedMethod may not correspond to the caller listed in bcInfo, so it's
         // not safe to call isWarmCallGraphTooBig.
         }
      else if (comp()->isServerInlining() &&
            !alwaysWorthInlining(calleeResolvedMethod, NULL) &&
            callerResolvedMethod->isWarmCallGraphTooBig(bcInfo.getByteCodeIndex(), comp()) &&
            !isHot(comp()))
         {
         heuristicTrace(tracer(),"### Avoiding estimation (even though size is reasonable) of call %s.(Exceeding Size Threshold)", tracer()->traceSignature(calleeResolvedMethod));
         return true;
         }
      }

   heuristicTrace(tracer(),"### Did not exceed size threshold, bytecodeSize %d <= inlineThreshold %d",bytecodeSize, _methodInWarmBlockByteCodeSizeThreshold);

   // Does not exceed size threshold
   return false;
   }

bool TR_J9InlinerPolicy::canInlineMethodWhileInstrumenting(TR_ResolvedMethod *method)
   {
   if ((TR::Compiler->vm.isSelectiveMethodEnterExitEnabled(comp()) && !comp()->fej9()->methodsCanBeInlinedEvenIfEventHooksEnabled(comp())) ||
         (comp()->fej9()->isAnyMethodTracingEnabled(method->getPersistentIdentifier()) &&
         !comp()->fej9()->traceableMethodsCanBeInlined()))
      return false;
   else return true;
   }

bool TR_J9InlinerPolicy::shouldRemoveDifferingTargets(TR::Node *callNode)
   {
   if (!OMR_InlinerPolicy::shouldRemoveDifferingTargets(callNode))
      return false;

   TR::RecognizedMethod rm =
      callNode->getSymbol()->castToMethodSymbol()->getRecognizedMethod();

   return rm != TR::java_lang_invoke_MethodHandle_invokeBasic;
   }

void
TR_J9InlinerUtil::refineInlineGuard(TR::Node *callNode, TR::Block *&block1, TR::Block *&block2,
                  bool &appendTestToBlock1, TR::ResolvedMethodSymbol * callerSymbol, TR::TreeTop *cursorTree,
                  TR::TreeTop *&virtualGuard, TR::Block *block4)
   {
   TR::CFG * callerCFG = callerSymbol->getFlowGraph();
   TR_PrexArgInfo *argInfo = comp()->getCurrentInlinedCallArgInfo();
   if (argInfo)
      {
      if (comp()->usesPreexistence()) // Mark the preexistent arguments (maybe redundant if VP has already done that and provided the info in argInfo)
         {
         int32_t firstArgIndex = callNode->getFirstArgumentIndex();
         for (int32_t c = callNode->getNumChildren() -1; c >= firstArgIndex; c--)
            {
            TR::Node *argument = callNode->getChild(c);
            TR_PrexArgument *p = argInfo->get(c - firstArgIndex);
            if (p && p->usedProfiledInfo())
               {
               TR_OpaqueClassBlock *pc = p->getFixedProfiledClass();
               if (pc)
                  {
                  //printf("Creating new guards in method %s caller %s callee %s\n", comp()->signature(), callerSymbol->getResolvedMethod()->signature(trMemory()), calleeSymbol->getResolvedMethod()->signature(trMemory()));
                  //fflush(stdout);


                  TR::Block *origBlock1 = block1;
                  TR::Block *newBlock = block1;
                  TR::Block *newBlock2 = TR::Block::createEmptyBlock(callNode, comp(), block1->getFrequency());
                  callerCFG->addNode(newBlock2);
                  if (!appendTestToBlock1)
                     {
                     newBlock = TR::Block::createEmptyBlock(callNode, comp());
                     callerCFG->addNode(newBlock);
                     callerCFG->addEdge(block1, newBlock);
                     callerCFG->addEdge(newBlock, block2);
                     //callerCFG->addEdge(newBlock, newBlock2); //block4);
                     callerCFG->copyExceptionSuccessors(block1, newBlock);
                     callerCFG->removeEdge(block1, block2);
                     }

                  TR::SymbolReferenceTable *symRefTab = comp()->getSymRefTab();

                  TR::Node * aconstNode = TR::Node::aconst(callNode, (uintptr_t)pc);
                  aconstNode->setIsClassPointerConstant(true);

                  TR::Node *guard = NULL;
                  TR::DataType dataType = argument->getDataType();
                  TR::SymbolReference *newSymbolReference = comp()->getSymRefTab()->createTemporary(callerSymbol, dataType);

                  TR::Node *storeNode = TR::Node::createWithSymRef(comp()->il.opCodeForDirectStore(argument->getDataType()), 1, 1, argument, newSymbolReference);
                  TR::TreeTop *storeTree = TR::TreeTop::create(comp(), storeNode);
                  TR::TreeTop *nextTree = cursorTree->getNextTreeTop();

                  cursorTree->join(storeTree);
                  storeTree->join(nextTree);
                  cursorTree = storeTree;

                  TR::Node * aconstNullNode = TR::Node::create(callNode, TR::aconst, 0);
                  TR::Node * nullcmp = TR::Node::createWithSymRef(argument, comp()->il.opCodeForDirectLoad(argument->getDataType()), 0, newSymbolReference);
                  guard = TR::Node::createif(TR::ifacmpeq, nullcmp, aconstNullNode, block2->getEntry());

                  TR::TreeTop *newTreeTop = newBlock->append(TR::TreeTop::create(comp(), guard));
                  if (!appendTestToBlock1)
                     {
                     newBlock->setDoNotProfile();
                     block1->getExit()->join(newBlock->getEntry());
                     newBlock->getExit()->join(block2->getEntry());
                     }
                  else
                     virtualGuard = newTreeTop;

                  block1 = newBlock;
                  block2 = block1->getNextBlock();
                  //appendTestToBlock1 = false;

                  newBlock = newBlock2;
                  callerCFG->addEdge(block1, newBlock);
                  callerCFG->addEdge(newBlock, block2);
                  callerCFG->addEdge(newBlock, block4);
                  if (appendTestToBlock1)
                     callerCFG->removeEdge(origBlock1, block4);
                  callerCFG->copyExceptionSuccessors(block1, newBlock);
                  // callerCFG->removeEdge(block1, block2);

                  TR::Node * vft = TR::Node::createWithSymRef(TR::aloadi, 1, 1, TR::Node::createWithSymRef(argument, comp()->il.opCodeForDirectLoad(argument->getDataType()), 0, newSymbolReference), symRefTab->findOrCreateVftSymbolRef());
                  guard = TR::Node::createif(TR::ifacmpne, vft, aconstNode, block4->getEntry());

                  //argument->recursivelyDecReferenceCount();
                  //callNode->setAndIncChild(c, TR::Node::create(comp()->il.opCodeForDirectLoad(argument->getDataType()), 0, newSymbolReference));

                  newTreeTop = newBlock->append(TR::TreeTop::create(comp(), guard));
                  //if (!appendTestToBlock1)
                     {
                     newBlock->setDoNotProfile();
                     block1->getExit()->join(newBlock->getEntry());
                     newBlock->getExit()->join(block2->getEntry());
                     }
                  //else
                  //   virtualGuard = newTreeTop;

                  block1 = newBlock;
                  block2 = block1->getNextBlock();
                  appendTestToBlock1 = false;
                  }
               }
            }
         }
      }
   }

void
TR_J9InlinerUtil::refineInliningThresholds(TR::Compilation *comp, int32_t &callerWeightLimit, int32_t &maxRecursiveCallByteCodeSizeEstimate, int32_t &methodByteCodeSizeThreshold, int32_t &methodInWarmBlockByteCodeSizeThreshold, int32_t &methodInColdBlockByteCodeSizeThreshold, int32_t &nodeCountThreshold, int32_t size)
   {
   comp->fej9()->setInlineThresholds(comp, callerWeightLimit, maxRecursiveCallByteCodeSizeEstimate, methodByteCodeSizeThreshold,
         methodInWarmBlockByteCodeSizeThreshold, methodInColdBlockByteCodeSizeThreshold, nodeCountThreshold, size);
   }

bool
TR_J9InlinerPolicy::doCorrectnessAndSizeChecksForInlineCallTarget(TR_CallStack *callStack, TR_CallTarget *calltarget, bool inlinefromgraph, TR_PrexArgInfo *argInfo)
   {
   // I think it would be a good idea to dump the list of Call Targets before doing the first inlineCallTarget.
   // You could get creative in your dumps:  calltargets in order, calltargets by callsite, etc.

   TR_LinkHead<TR_ParameterMapping> map;
   if (!validateArguments(calltarget, map))
      {
      TR_ASSERT(comp()->fej9()->canAllowDifferingNumberOrTypesOfArgsAndParmsInInliner(), "Error, call target has a parameter mapping issue.");

      return false;
      }


   debugTrace(tracer(),"bool inlinecallTarget: calltarget %p calltarget->mycallsite %p calltarget->alreadyInlined = %d inlinefromgraph = %d currentNumberOfNodes = %d",calltarget,calltarget->_myCallSite,calltarget->_alreadyInlined, inlinefromgraph, _inliner->getCurrentNumberOfNodes());

   TR_ASSERT(!(calltarget->_alreadyInlined && calltarget->_myCallSite->_callNode->isTheVirtualCallNodeForAGuardedInlinedCall()), "inlineCallTarget: trying to inline the virtual call node for a guarded inline call that's already been inlined!");
   int32_t nodeCount = _inliner->getCurrentNumberOfNodes();

   int32_t sitesSize = (int32_t)(comp()->getNumInlinedCallSites());

   if (sitesSize >= inliner()->getMaxInliningCallSites() && !inliner()->forceInline(calltarget))
      {
      tracer()->insertCounter(Exceeded_Caller_SiteSize,calltarget->_myCallSite->_callNodeTreeTop);
      if (comp()->trace(OMR::inlining))
         traceMsg(comp(), "inliner: failed: Caller has too many call sites %s\n", tracer()->traceSignature(calltarget->_calleeSymbol));
      return false;
      }

   bool ignoreThisSmallMethod = getInitialBytecodeSize(calltarget->_calleeMethod, calltarget->_calleeSymbol, comp()) <= 20;
   //static int si, sj; ++sj;
   if (((_inliner->getNumAsyncChecks() > HIGH_LOOP_COUNT-5) || ((uint32_t)nodeCount > _inliner->getNodeCountThreshold())) && !_inliner->forceInline(calltarget) && !ignoreThisSmallMethod)
      {
      // getCurrentNumberOfNodes may be unreliable so we must recompute
      if (((uint32_t)(nodeCount = comp()->generateAccurateNodeCount()) > _inliner->getNodeCountThreshold()) || (_inliner->getNumAsyncChecks() > HIGH_LOOP_COUNT-5))
         {
         tracer()->insertCounter(Exceeded_Caller_Node_Budget,calltarget->_myCallSite->_callNodeTreeTop);

         TR::Options::INLINE_calleeHasTooManyNodes++;
         TR::Options::INLINE_calleeHasTooManyNodesSum += nodeCount;
         if (comp()->trace(OMR::inlining))
            traceMsg(comp(), "inliner: failed: Caller has too many nodes %s while considering callee %s  nodeCount = %d nodeCountThreshold = %d\n",comp()->signature(), tracer()->traceSignature(calltarget->_calleeSymbol),nodeCount,_inliner->getNodeCountThreshold());
         return false;
         }
      }

   return true;
   }

bool
TR_J9InlinerPolicy::validateArguments(TR_CallTarget *calltarget, TR_LinkHead<TR_ParameterMapping> &map)
   {
   calltarget->_calleeSymbol->setParameterList();

   ListIterator<TR::ParameterSymbol> parms(&(calltarget->_calleeSymbol->getParameterList()));

   int32_t numParms = calltarget->_calleeSymbol->getParameterList().getSize();
   int32_t numArgs = (int32_t) (calltarget->_myCallSite->_callNode->getNumChildren());

   numArgs = numArgs - calltarget->_myCallSite->_callNode->getFirstArgumentIndex();

   if (calltarget->_calleeSymbol->getResolvedMethod()->isJNINative() && calltarget->_calleeSymbol->getResolvedMethod()->isStatic() && calltarget->_myCallSite->_callNode->isPreparedForDirectJNI())
      numArgs--;

   if (numArgs != numParms)
      {
      heuristicTrace(tracer(), "Number of Parameters %d and Arguments %d Differ.  Removing Call Target for Safety's sake.", numParms, numArgs);
      calltarget->_myCallSite->removecalltarget(calltarget, tracer(), Cant_Match_Parms_to_Args);

      TR_ASSERT(comp()->fej9()->canAllowDifferingNumberOrTypesOfArgsAndParmsInInliner() , "Can't match args to parms, arg number mismatch");

      return false;
      }

   inliner()->createParmMap(calltarget->_calleeSymbol, map);


   TR_ParameterMapping * parm = map.getFirst();
   int32_t argNodeIndex = calltarget->_myCallSite->_callNode->getFirstArgumentIndex();
   if (argNodeIndex == 0 && calltarget->_calleeSymbol->getResolvedMethod()->isJNINative() && calltarget->_calleeSymbol->getResolvedMethod()->isStatic() && calltarget->_myCallSite->_callNode->isPreparedForDirectJNI())
      argNodeIndex++;

   for ( ; parm ; parm = parm->getNext(), ++argNodeIndex )
      {
      TR::Node *arg = calltarget->_myCallSite->_callNode->getChild(argNodeIndex);

      parm->_parameterNode = arg;

      if (arg->getDataType() != parm->_parmSymbol->getDataType() &&
         (parm->_parmSymbol->getDataType() != TR::Aggregate))
         {
         heuristicTrace(tracer(), "For argNodeIndex %d, data type of node %p does not match data type of parameter. Removing Call Target for Safety's sake.", argNodeIndex, arg);
         calltarget->_myCallSite->removecalltarget(calltarget, tracer(), Cant_Match_Parms_to_Args);

         if (!comp()->fej9()->canAllowDifferingNumberOrTypesOfArgsAndParmsInInliner())
            TR_ASSERT(0, "Can't match args to parms. Data type mismatch.");

         return false;
         }
      }
   return true;
   }

bool
TR_J9InlinerPolicy::supressInliningRecognizedInitialCallee(TR_CallSite* callsite, TR::Compilation* comp)
   {
   TR_ResolvedMethod * initialCalleeMethod = callsite->_initialCalleeMethod;

   if (initialCalleeMethod)
      {
      switch (initialCalleeMethod->getRecognizedMethod())
         {
         /*
          * Inline this group of methods when the compiling method is shared method handle thunk.
          * Otherwise, they can be folded away by VP and should not be inlined here.
          */
         case TR::java_lang_invoke_DirectHandle_nullCheckIfRequired:
         case TR::java_lang_invoke_PrimitiveHandle_initializeClassIfRequired:
         case TR::java_lang_invoke_MethodHandle_invokeExactTargetAddress:
            {
            TR::IlGeneratorMethodDetails & details = comp->ilGenRequest().details();
            if (details.isMethodHandleThunk())
               {
               J9::MethodHandleThunkDetails & thunkDetails = static_cast<J9::MethodHandleThunkDetails &>(details);
                  return thunkDetails.isCustom();
               }
            return true;
            }

         default:
            break;
         }
      }
   return (callsite->_callNode && comp->fej9()->supressInliningRecognizedInitialCallee(callsite, comp));
   }

static bool
isDecimalFormatPattern(TR::Compilation *comp, TR_ResolvedMethod *method)
   {
   // look for the NumberFormat pattern
   //
   //  [   0],     0, JBaload0getfield
   //  [   1],     1, JBgetfield              Ljava/text/NumberFormat;
   //  [   4],     4, JBaload1                Ljava/math/BigDecimal;
   //  [   5],     5, JBinvokevirtual         BigDecimal.doubleValue()D or BigDecimal.floatValue()F
   //  [   8],     8, JBf2d                   if floatValue
   //  [   8],     8, JBinvokevirtual         NumberFormat.format(D)Ljava/lang/String;
   //  [   b],    11, JBreturn1
   //
   // don't inline such methods as we want to run stringpeepholes when compiled on their own
   // so that the decimalFormat optimization can be applied
   //

   TR_J9ByteCodeIterator bci(0, static_cast<TR_ResolvedJ9Method *> (method), comp->fej9(), comp);

   // maxbytecode could be 12 or 13 depending on whether
   // doubleValue or floatValue is called
   //
   if (bci.maxByteCodeIndex() > 13)
      return false;
   int32_t bcCount = 0;
   TR::DataType type = TR::NoType;
   uint32_t fieldOffset;
   bool isUnresolvedInCP;
   TR_J9ByteCode bc = bci.first();
   if (bc == J9BCunknown) return false;
   if (bc != J9BCaload0) return false;
   bc = bci.next(); // matched 1st bc

   if (bc == J9BCgetfield)
      {
      bool isVolatile, isPrivate, resolved;
      resolved = method->fieldAttributes(comp, bci.next2Bytes(), &fieldOffset, &type, &isVolatile, 0, &isPrivate, false, &isUnresolvedInCP);
      if (!resolved || isUnresolvedInCP)
         return false;
      else if (type != TR::Address)
         return false;

      // TODO: make sure the field is recognized as a NumberFormat
      //
      bc = bci.next(); //matched 2nd bc
      }
   else
      return false;

   if (bc != J9BCaload1)
      return false;
   bc = bci.next(); // matched 3rd bc

   bool isFloat = false;
   if (bc == J9BCinvokevirtual)
      {
      int32_t cpIndex = bci.next2Bytes();
      TR_ResolvedMethod *resolvedMethod = method->getResolvedVirtualMethod(comp, cpIndex, true, &isUnresolvedInCP);
      if (resolvedMethod &&
            (resolvedMethod->getRecognizedMethod() == TR::java_math_BigDecimal_doubleValue ||
             resolvedMethod->getRecognizedMethod() == TR::java_math_BigDecimal_floatValue))
         {
         if (resolvedMethod->getRecognizedMethod() == TR::java_math_BigDecimal_floatValue)
            isFloat = true;
         bc = bci.next(); // matched 4th bc
         }
      else
         return false;
      }
   else
      return false;

   if (isFloat)
      {
      if (bc != J9BCf2d)
         return false;
      bc = bci.next(); // matched 5th bc if floatValue
      }

   if (bc == J9BCinvokevirtual)
      {
      int32_t cpIndex = bci.next2Bytes();
      TR_ResolvedMethod *resolvedMethod = method->getResolvedVirtualMethod(comp, cpIndex, true, &isUnresolvedInCP);
      if (resolvedMethod &&
            resolvedMethod->getRecognizedMethod() == TR::java_text_NumberFormat_format)
         {
         bc = bci.next(); // matched 5th (or 6th) bc
         }
      else
         return false;
      }
   else
      return false;

   if (bc != J9BCgenericReturn)
      return false; // matched 6th (or 7th) bc

   ///traceMsg(comp, "pattern matched successfully\n");
   return true;
   }

TR_InlinerFailureReason
TR_J9JSR292InlinerPolicy::checkIfTargetInlineable(TR_CallTarget* target, TR_CallSite* callsite, TR::Compilation* comp)
   {
   // for GPU, skip all the heuristic for JSR292 related methods
   if (comp->hasIntStreamForEach())
      return DontInline_Callee;

   TR_ResolvedMethod * resolvedMethod = target->_calleeSymbol ? target->_calleeSymbol->getResolvedMethod():target->_calleeMethod;
   if (!isJSR292Method(resolvedMethod))
      return DontInline_Callee;

   if (isJSR292AlwaysWorthInlining(resolvedMethod))
      return InlineableTarget;
   else if (comp->getCurrentMethod()->convertToMethod()->isArchetypeSpecimen() ||
            comp->getCurrentMethod()->getRecognizedMethod() == TR::java_lang_invoke_MethodHandle_invokeExact)
      {
      //we are ourselves an archetype specimen, so we can inline other archetype speciman
      return InlineableTarget;
      }
   else if (comp->getMethodHotness() >= warm)
      {
      // We are not an archetype specimen
      // but because we're warm (or greater) we are allowed to inline JSR292 methods
      return InlineableTarget;
      }

   //we are not an archetype specimen ourselves and we are cold or below, No inlining of JSR292 methods.
   return DontInline_Callee;
   }

TR_InlinerFailureReason
 TR_J9InlinerPolicy::checkIfTargetInlineable(TR_CallTarget* target, TR_CallSite* callsite, TR::Compilation* comp)
   {
   if (comp->compileRelocatableCode() && comp->getMethodHotness() <= cold)
      {
      // If we are an AOT cold compile, don't inline
      return DontInline_Callee;
      }

   TR_ResolvedMethod * resolvedMethod = target->_calleeSymbol ? target->_calleeSymbol->getResolvedMethod():target->_calleeMethod;

   if (!isInlineableJNI(resolvedMethod,callsite->_callNode) || callsite->isIndirectCall())
      {
      if (!target->_calleeMethod->isCompilable(comp->trMemory()) || !target->_calleeMethod->isInlineable(comp))
         {
         return Not_Compilable_Callee;
         }

      if (target->_calleeMethod->isJNINative())
         {
         return JNI_Callee;
         }
      }

   TR::RecognizedMethod rm = resolvedMethod->getRecognizedMethod();

   // Don't inline methods that are going to be reduced in ilgen or UnsafeFastPath
   switch (rm)
      {
      case TR::com_ibm_jit_JITHelpers_getByteFromArray:
      case TR::com_ibm_jit_JITHelpers_getByteFromArrayByIndex:
      case TR::com_ibm_jit_JITHelpers_getByteFromArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_getCharFromArray:
      case TR::com_ibm_jit_JITHelpers_getCharFromArrayByIndex:
      case TR::com_ibm_jit_JITHelpers_getCharFromArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_getIntFromArray:
      case TR::com_ibm_jit_JITHelpers_getIntFromArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_getIntFromObject:
      case TR::com_ibm_jit_JITHelpers_getIntFromObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_getLongFromArray:
      case TR::com_ibm_jit_JITHelpers_getLongFromArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_getLongFromObject:
      case TR::com_ibm_jit_JITHelpers_getLongFromObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_getObjectFromArray:
      case TR::com_ibm_jit_JITHelpers_getObjectFromArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_getObjectFromObject:
      case TR::com_ibm_jit_JITHelpers_getObjectFromObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_putByteInArray:
      case TR::com_ibm_jit_JITHelpers_putByteInArrayByIndex:
      case TR::com_ibm_jit_JITHelpers_putByteInArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_putCharInArray:
      case TR::com_ibm_jit_JITHelpers_putCharInArrayByIndex:
      case TR::com_ibm_jit_JITHelpers_putCharInArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_putIntInArray:
      case TR::com_ibm_jit_JITHelpers_putIntInArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_putIntInObject:
      case TR::com_ibm_jit_JITHelpers_putIntInObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_putLongInArray:
      case TR::com_ibm_jit_JITHelpers_putLongInArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_putLongInObject:
      case TR::com_ibm_jit_JITHelpers_putLongInObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_putObjectInArray:
      case TR::com_ibm_jit_JITHelpers_putObjectInArrayVolatile:
      case TR::com_ibm_jit_JITHelpers_putObjectInObject:
      case TR::com_ibm_jit_JITHelpers_putObjectInObjectVolatile:
      case TR::com_ibm_jit_JITHelpers_byteToCharUnsigned:
      case TR::com_ibm_jit_JITHelpers_acmplt:
      case TR::com_ibm_jit_JITHelpers_isArray:
      case TR::com_ibm_jit_JITHelpers_getJ9ClassFromObject32:
      case TR::com_ibm_jit_JITHelpers_getJ9ClassFromObject64:
      case TR::com_ibm_jit_JITHelpers_getClassInitializeStatus:
      case TR::java_lang_StringUTF16_getChar:
      case TR::java_lang_StringUTF16_putChar:
      case TR::java_lang_StringUTF16_toBytes:
      case TR::java_lang_invoke_MethodHandle_asType:
            return DontInline_Callee;
      default:
         break;
   }

   if (comp->getOptions()->getEnableGPU(TR_EnableGPU))
      {
      switch (rm)
         {
         case TR::java_util_stream_AbstractPipeline_evaluate:
            traceMsg(comp, "Intentionally avoided inlining evaluate\n");
            return Recognized_Callee;
            break;
         default:
            break;
         }
      }

   if (comp->getOptions()->getEnableGPU(TR_EnableGPUEnableMath))
      {
      switch (rm)
         {
         case TR::java_lang_Math_abs_F:
         case TR::java_lang_Math_abs_D:
         case TR::java_lang_Math_exp:
         case TR::java_lang_Math_log:
         case TR::java_lang_Math_sqrt:
         case TR::java_lang_Math_sin:
         case TR::java_lang_Math_cos:
            traceMsg(comp, "Intentionally avoided inlining MathMethod\n");
            return Recognized_Callee;
         default:
            break;
         }
      }

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   if (comp->fej9()->inlineRecognizedCryptoMethod(target, comp))
      {
      return Recognized_Callee;
      }
#endif

   if (
       rm == TR::com_ibm_jit_JITHelpers_toUpperIntrinsicLatin1 ||
       rm == TR::com_ibm_jit_JITHelpers_toLowerIntrinsicUTF16 ||
       rm == TR::com_ibm_jit_JITHelpers_toUpperIntrinsicLatin1 ||
       rm == TR::com_ibm_jit_JITHelpers_toLowerIntrinsicUTF16 ||

       rm == TR::java_lang_String_compressedArrayCopy_BIBII ||
       rm == TR::java_lang_String_compressedArrayCopy_BICII ||
       rm == TR::java_lang_String_compressedArrayCopy_CIBII ||
       rm == TR::java_lang_String_compressedArrayCopy_CICII ||
       rm == TR::java_lang_String_decompressedArrayCopy_BIBII ||
       rm == TR::java_lang_String_decompressedArrayCopy_BICII ||
       rm == TR::java_lang_String_decompressedArrayCopy_CIBII ||
       rm == TR::java_lang_String_decompressedArrayCopy_CICII ||
       rm == TR::java_lang_Math_max_D ||
       rm == TR::java_lang_Math_min_D ||
       //DAA Intrinsic methods will get reduced if intrinsics are on, so don't consider it as a target
       (resolvedMethod->isDAAMarshallingIntrinsicMethod() && !comp->getOption(TR_DisableMarshallingIntrinsics)) ||
       (resolvedMethod->isDAAPackedDecimalIntrinsicMethod() && !comp->getOption(TR_DisablePackedDecimalIntrinsics)) ||

      // dont inline methods that contain the NumberFormat pattern
      // this is because we want to catch the opportunity with stringpeepholes
      // and stringpeepholes runs before inliner. so if the calleemethod contained
      // the pattern and it got inlined, we would never find the pattern
      isDecimalFormatPattern(comp, target->_calleeMethod))
      {
      return Recognized_Callee;
      }

   return InlineableTarget;
   }

bool TR_J9InlinerPolicy::isJSR292Method(TR_ResolvedMethod *resolvedMethod)
   {
   if (isJSR292AlwaysWorthInlining(resolvedMethod))
      return true;

   TR::RecognizedMethod method =  resolvedMethod->getRecognizedMethod();
   if (method == TR::java_lang_invoke_MethodHandle_invokeExact)
      return true;
   return false;
   }

bool TR_J9InlinerPolicy::isJSR292AlwaysWorthInlining(TR_ResolvedMethod *resolvedMethod)
   {
   TR::RecognizedMethod method =  resolvedMethod->getRecognizedMethod();
   if (method == TR::java_lang_invoke_MethodHandle_invokeExact)
      return true;

   if (TR_J9MethodBase::isVarHandleOperationMethod(method))
      return true;

   if (isJSR292SmallGetterMethod(resolvedMethod))
      return true;

   if (isJSR292SmallHelperMethod(resolvedMethod))
      return true;

   if (resolvedMethod->convertToMethod()->isArchetypeSpecimen())
      return true;

   if (TR::comp()->fej9()->isLambdaFormGeneratedMethod(resolvedMethod))
      return true;

   return false;
   }

bool TR_J9InlinerPolicy::isJSR292SmallHelperMethod(TR_ResolvedMethod *resolvedMethod)
   {
   TR::RecognizedMethod method =  resolvedMethod->getRecognizedMethod();
   switch (method)
      {
      case TR::java_lang_invoke_ConvertHandleFilterHelpers_object2J:
      case TR::java_lang_invoke_ConvertHandleFilterHelpers_number2J:
      case TR::java_lang_invoke_MethodHandle_doCustomizationLogic:
      case TR::java_lang_invoke_MethodHandle_undoCustomizationLogic:
         return true;
      
      default:
         break;
      }
   return false;
   }

bool TR_J9InlinerPolicy::isJSR292SmallGetterMethod(TR_ResolvedMethod *resolvedMethod)
   {
   TR::RecognizedMethod method =  resolvedMethod->getRecognizedMethod();
   // small getters
   switch (method)
      {
      case TR::java_lang_invoke_MutableCallSite_getTarget:
      case TR::java_lang_invoke_MethodHandle_type:
         return true;

      default:
         break;
      }
   return false;
   }

void
TR_J9InlinerUtil::estimateAndRefineBytecodeSize(TR_CallSite* callsite, TR_CallTarget* calltarget, TR_CallStack *callStack, int32_t &bytecodeSize)
   {
   if (comp()->getOptLevel() >= warm && bytecodeSize > 100)
      {
                     //We call to calculateCodeSize to simply get an estimate.
                     //We don't want the original calltarget to be modified and become inconsistent in any way
                     //Please see 196749 for more details

      calltarget->_originatingBlock = (callsite->_callerBlock != NULL) ? callsite->_callerBlock : (callsite->_callNodeTreeTop ? callsite->_callNodeTreeTop->getEnclosingBlock() : 0);
      bool estimateIsFine = false;
      if (calltarget->_originatingBlock && calltarget->_calleeSymbol)
         {
         TR_CallTarget callTargetClone (*calltarget);
         TR_EstimateCodeSize::raiiWrapper ecsWrapper(inliner(), tracer(), inliner()->getMaxRecursiveCallByteCodeSizeEstimate());
         TR_EstimateCodeSize *ecs = ecsWrapper.getCodeEstimator();
         vcount_t origVisitCount = comp()->getVisitCount();
         estimateIsFine = ecs->calculateCodeSize(&callTargetClone, callStack, false);
         comp()->setVisitCount(origVisitCount);

         if (estimateIsFine)
            {
            if (comp()->trace(OMR::inlining))
               traceMsg( comp(), "Partial estimate for this target %d, full size %d, real bytecode size %d\n", callTargetClone._partialSize, callTargetClone._fullSize, bytecodeSize);

            bytecodeSize = callTargetClone._fullSize;
            if (comp()->trace(OMR::inlining))
               traceMsg( comp(), "Reducing bytecode size to %d\n", bytecodeSize);
            }
         }
      }
   }

TR_PrexArgInfo* TR_PrexArgInfo::argInfoFromCaller(TR::Node* callNode, TR_PrexArgInfo* callerArgInfo)
   {
   TR::Compilation* compilation = TR::comp();
   bool tracePrex = compilation->trace(OMR::inlining) || compilation->trace(OMR::invariantArgumentPreexistence);

   int32_t firstArgIndex = callNode->getFirstArgumentIndex();
   int32_t numArgs = callNode->getNumArguments();
   int32_t numChildren = callNode->getNumChildren();

   TR_PrexArgInfo* argInfo = new (compilation->trHeapMemory()) TR_PrexArgInfo(numArgs, compilation->trMemory());

   for (int32_t i = firstArgIndex; i < numChildren; i++)
      {
      TR::Node* child = callNode->getChild(i);
      if (TR_PrexArgInfo::hasArgInfoForChild(child, callerArgInfo))
         {
         argInfo->set(i - firstArgIndex, TR_PrexArgInfo::getArgForChild(child, callerArgInfo));
         if (tracePrex)
            traceMsg(compilation, "Arg %d is from caller\n", i - firstArgIndex);
         }
      }
   return argInfo;
   }

TR_PrexArgInfo *
TR_J9InlinerUtil::computePrexInfo(TR_CallTarget *target, TR_PrexArgInfo *callerArgInfo)
   {
   if (comp()->getOption(TR_DisableInlinerArgsPropagation))
      return NULL;

   TR_CallSite *site = target->_myCallSite;
   if (!site || !site->_callNode)
      return NULL;

   bool tracePrex = comp()->trace(OMR::inlining) || comp()->trace(OMR::invariantArgumentPreexistence);

   auto prexArgInfoFromTarget = createPrexArgInfoForCallTarget(target->_guard, target->_calleeMethod);
   auto prexArgInfoFromCallSite = TR_J9InlinerUtil::computePrexInfo(inliner(), site, callerArgInfo);
   auto prexArgInfo = TR_PrexArgInfo::enhance(prexArgInfoFromTarget, prexArgInfoFromCallSite, comp());

   if (tracePrex && prexArgInfo)
      {
      traceMsg(comp(), "PREX.inl:    argInfo for target %p\n", target);
      prexArgInfo->dumpTrace();
      }

   // At this stage, we can improve the type of the virtual guard we are going to use
   // For a non-overridden guard or for an interface guard, if it makes sense, try to use a vft-test
   // based virtual guard
   //
   bool disablePreexForChangedGuard = false;
   TR_PersistentCHTable * chTable = comp()->getPersistentInfo()->getPersistentCHTable();
   TR_PersistentClassInfo *thisClassInfo = chTable->findClassInfoAfterLocking(target->_receiverClass, comp());
   if (target->_calleeSymbol->hasThisCalls() &&
       target->_receiverClass &&
       !TR::Compiler->cls.isAbstractClass(comp(), target->_receiverClass) &&
       !fe()->classHasBeenExtended(target->_receiverClass) &&
       thisClassInfo &&
       thisClassInfo->isInitialized() &&
       ((target->_guard->_kind == TR_NonoverriddenGuard && target->_guard->_type == TR_NonoverriddenTest) ||
        (target->_guard->_kind == TR_InterfaceGuard)) &&
       performTransformation(comp(), "O^O VIRTUAL GUARD IMPROVE: Changed guard kind %s type %s to use VFT test\n", tracer()->getGuardKindString(target->_guard), tracer()->getGuardTypeString(target->_guard)))
      {
      target->_guard->_type = TR_VftTest;
      target->_guard->_thisClass = target->_receiverClass;
      }

   return prexArgInfo;
   }

TR_PrexArgInfo *
TR_J9InlinerUtil::computePrexInfo(TR_CallTarget *target)
   {
   return computePrexInfo(target, NULL);
   }

/** \brief
 *     Find the def to an auto or parm before treetop in a extended basic block
 *
 *  \return
 *     The treetop containing the def (the store)
 */
static TR::TreeTop*
defToAutoOrParmInEBB(TR::Compilation* comp, TR::TreeTop* treetop, TR::SymbolReference* symRef, TR::Node** valueNode)
   {
   for (;treetop != NULL; treetop= treetop->getPrevTreeTop())
      {
      auto ttNode = treetop->getNode();
      if (ttNode->getOpCodeValue() == TR::BBStart)
         {
         auto block = ttNode->getBlock();
         if (!block->isExtensionOfPreviousBlock())
            return NULL;
         else
            continue;
         }

      auto storeNode = ttNode->getStoreNode();
      if (storeNode &&
          storeNode->getOpCode().isStoreDirect() &&
          storeNode->getSymbolReference() == symRef)
         {
         auto child = storeNode->getFirstChild();
         // If the child is also an auto, keep walking the trees to find the child's def
         if (child->getOpCode().hasSymbolReference() &&
             child->getSymbolReference()->getSymbol()->isAuto() &&
             !child->getSymbolReference()->hasKnownObjectIndex())
            {
            symRef = child->getSymbolReference();
            continue;
            }

         if (valueNode)
            *valueNode = child;

         return treetop;
         }
      }

   return NULL;
   }

TR_PrexArgInfo *
TR_J9InlinerUtil::computePrexInfo(TR_InlinerBase *inliner, TR_CallSite* site, TR_PrexArgInfo *callerArgInfo)
   {
   TR::Compilation* comp = inliner->comp();

   if (comp->getOption(TR_DisableInlinerArgsPropagation))
      return NULL;

   if (!site->_callNode)
      return NULL;

   auto callNode = site->_callNode;

   // We want to avoid degrading info we already have from another source like VP.
   // Unfortunately that desire mucks up this function with a lot of logic that
   // looks like constraint merging, and is redundant with what VP already does.
   //
   TR_PrexArgInfo *prexArgInfo = NULL;
   // Interface call doesn't have a resolved _intialCalleeMethod, so callee can be NULL
   auto callee = site->_initialCalleeMethod;

   bool tracePrex = comp->trace(OMR::inlining) || comp->trace(OMR::invariantArgumentPreexistence);
   if (tracePrex)
      traceMsg(comp, "PREX.inl: Populating prex argInfo for [%p] %s %s\n", callNode, callNode->getOpCode().getName(), callNode->getSymbol()->castToMethodSymbol()->getMethod()->signature(inliner->trMemory(), stackAlloc));

   int32_t firstArgIndex = callNode->getFirstArgumentIndex();
   for (int32_t c = callNode->getNumChildren() -1; c >= firstArgIndex; c--)
      {
      int32_t argOrdinal = c - firstArgIndex;

      TR::Node *argument = callNode->getChild(c);
      if (tracePrex)
         {
         traceMsg(comp, "PREX.inl:    Child %d [%p] n%dn %s %s\n",
            c, argument,
            argument->getGlobalIndex(),
            argument->getOpCode().getName(),
            argument->getOpCode().hasSymbolReference()? argument->getSymbolReference()->getName(comp->getDebug()) : "");
         }

      if (!argument->getOpCode().hasSymbolReference() || argument->getDataType() != TR::Address)
         continue;

      auto symRef = argument->getSymbolReference();
      auto symbol = symRef->getSymbol();

      TR_PrexArgument* prexArg = NULL;

      if (c == callNode->getFirstArgumentIndex() &&
          callee &&
          callee->convertToMethod()->isArchetypeSpecimen() &&
          callee->getMethodHandleLocation() &&
          comp->getOrCreateKnownObjectTable())
         {
         // Here's a situation where inliner is taking it upon itself to draw
         // conclusions about known objects.  VP won't get a chance to figure this
         // out before we go ahead and do the inlining, so we'd better populate
         // the prex info now.
         //
         // (If VP did this stuff instead of inliner, it might work a bit more naturally.)
         //
         TR::KnownObjectTable::Index methodHandleIndex = comp->getKnownObjectTable()->getOrCreateIndexAt(callee->getMethodHandleLocation());
         prexArg = new (inliner->trHeapMemory()) TR_PrexArgument(methodHandleIndex, comp);
         if (tracePrex)
            {
            TR::Node *mh = callNode->getArgument(0);
            traceMsg(comp, "PREX.inl:      %p: %p is known object obj%d in inlined call [%p]\n", prexArg, mh, methodHandleIndex, callNode);
            }
         }
      else if (symRef->hasKnownObjectIndex())
         {
         prexArg = new (inliner->trHeapMemory()) TR_PrexArgument(symRef->getKnownObjectIndex(), comp);
         if (tracePrex)
            traceMsg(comp, "PREX.inl:      %p: is known object obj%d\n", prexArg, symRef->getKnownObjectIndex());
         }
      else if (argument->getOpCodeValue() == TR::aload)
         {
         OMR::ParameterSymbol *parmSymbol = symbol->getParmSymbol();
         if (parmSymbol && !prexArg)
            {
            if (parmSymbol->getFixedType())
               {
               prexArg = new (inliner->trHeapMemory()) TR_PrexArgument(TR_PrexArgument::ClassIsFixed, (TR_OpaqueClassBlock *) parmSymbol->getFixedType());
               if (tracePrex)
                  {
                  char *sig = TR::Compiler->cls.classSignature(comp, (TR_OpaqueClassBlock*)parmSymbol->getFixedType(), inliner->trMemory());
                  traceMsg(comp, "PREX.inl:      %p: is load of parm with fixed class %p %s\n", prexArg, parmSymbol->getFixedType(), sig);
                  }
               }
            if (parmSymbol->getIsPreexistent())
               {
               int32_t len = 0;
               const char *sig = parmSymbol->getTypeSignature(len);
               TR_OpaqueClassBlock *clazz = comp->fe()->getClassFromSignature(sig, len, site->_callerResolvedMethod);

               if (clazz)
                  {
                  prexArg = new (inliner->trHeapMemory()) TR_PrexArgument(TR_PrexArgument::ClassIsPreexistent, clazz);
                  if (tracePrex)
                     traceMsg(comp, "PREX.inl:      %p: is preexistent\n", prexArg);
                  }
               }
            }
         else if (symbol->isAuto())
            {
            TR::Node* valueNode = NULL;
            defToAutoOrParmInEBB(comp, site->_callNodeTreeTop, symRef, &valueNode);
            if (valueNode &&
                valueNode->getOpCode().hasSymbolReference() &&
                valueNode->getSymbolReference()->hasKnownObjectIndex())
               {
               prexArg = new (inliner->trHeapMemory()) TR_PrexArgument(valueNode->getSymbolReference()->getKnownObjectIndex(), comp);
               if (tracePrex)
                  traceMsg(comp, "PREX.inl:      %p: is known object obj%d, argument n%dn has def from n%dn %s %s\n",
                           prexArg,
                           prexArg->getKnownObjectIndex(),
                           argument->getGlobalIndex(),
                           valueNode->getGlobalIndex(),
                           valueNode->getOpCode().getName(),
                           valueNode->getSymbolReference()->getName(comp->getDebug()));
               }
            }
         }
      else if (symRef == comp->getSymRefTab()->findJavaLangClassFromClassSymbolRef())
         {
         TR::Node *argFirstChild = argument->getFirstChild();
         if (argFirstChild->getOpCodeValue() == TR::loadaddr &&
             argFirstChild->getSymbol()->isStatic() &&
             !argFirstChild->getSymbolReference()->isUnresolved() &&
             argFirstChild->getSymbol()->isClassObject() &&
             argFirstChild->getSymbol()->castToStaticSymbol()->getStaticAddress())
            {
            uintptr_t objectReferenceLocation = (uintptr_t)argFirstChild->getSymbolReference()->getSymbol()->castToStaticSymbol()->getStaticAddress();
            TR::KnownObjectTable *knot = comp->getOrCreateKnownObjectTable();
            if (knot)
               {
               TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());
               auto knownObjectIndex = knot->getOrCreateIndexAt((uintptr_t*)(objectReferenceLocation + fej9->getOffsetOfJavaLangClassFromClassField()));
               prexArg = new (comp->trHeapMemory()) TR_PrexArgument(knownObjectIndex, comp);
               if (tracePrex)
                  traceMsg(comp, "PREX.inl is known java/lang/Class obj%d\n", prexArg, knownObjectIndex);
               }
            }
         }

      if (prexArg)
         {
         if (!prexArgInfo)
            prexArgInfo =  new (inliner->trHeapMemory()) TR_PrexArgInfo(callNode->getNumArguments(), inliner->trMemory());
         prexArgInfo->set(argOrdinal, prexArg);
         }
      }

   if (tracePrex)
      traceMsg(comp, "PREX.inl: Done populating prex argInfo for %s %p\n", callNode->getOpCode().getName(), callNode);

   if (tracePrex && prexArgInfo)
      {
      traceMsg(comp, "PREX.inl:    argInfo for callsite %p\n", site);
      prexArgInfo->dumpTrace();
      }

   if (callerArgInfo)
      {
      if (tracePrex)
         traceMsg(comp, "PREX.inl: Propagating prex argInfo from caller for [%p] %s %s\n",
                  callNode,
                  callNode->getOpCode().getName(),
                  callNode->getSymbol()->castToMethodSymbol()->getMethod()->signature(inliner->trMemory(), stackAlloc));

      TR_PrexArgInfo* argsFromCaller = TR_PrexArgInfo::argInfoFromCaller(callNode, callerArgInfo);
      prexArgInfo = TR_PrexArgInfo::enhance(prexArgInfo, argsFromCaller, comp);

      if (tracePrex)
         {
         traceMsg(comp, "PREX.inl:    argInfo for callsite %p after propagating argInfo from caller\n", site);
         prexArgInfo->dumpTrace();
         }
      }

   return prexArgInfo;
   }

bool TR_J9InlinerUtil::needTargetedInlining(TR::ResolvedMethodSymbol *callee)
   {
   // Trees from archetype specimens may not match the archetype method's bytecodes,
   // so there may be some calls things that inliner missed.
   //
   // Tactically, we also inline again based on hasMethodHandleInvokes because EstimateCodeSize
   // doesn't yet cope with invokeHandle, invokeHandleGeneric, and invokeDynamic (but it should).
   //
   if (callee->getMethod()->isArchetypeSpecimen() ||
       callee->hasMethodHandleInvokes())
      return true;
   return false;
   }

/** Find arguments which refer to constant classes
 If a parameter refers to a constant class, set the known object index in _ecsPrexArgInfo
 of the target.
*/
void TR_J9InlinerUtil::checkForConstClass(TR_CallTarget *target, TR_LogTracer *tracer)
   {
   static char *disableCCI=feGetEnv("TR_DisableConstClassInlining");

   if (disableCCI || !tracer || !target) return;

   TR_CallSite *site = target->_myCallSite;
   if (!site) return;

   TR::Node* callNode = site->_callNode;
   if (!callNode) return;

   TR_PrexArgInfo *ecsArgInfo = target->_ecsPrexArgInfo;
   if (!ecsArgInfo) return;

   TR::Compilation * comp = tracer->comp();
   bool tracePrex = comp->trace(OMR::inlining) || comp->trace(OMR::invariantArgumentPreexistence);

   if (tracePrex)
      traceMsg(comp, "checkForConstClass parm for [%p] %s %s\n", callNode, callNode->getOpCode().getName(), callNode->getSymbol()->castToMethodSymbol()->getMethod()->signature(comp->trMemory(), stackAlloc));

   // loop over args
   int32_t firstArgIndex = callNode->getFirstArgumentIndex();
   for (int32_t c = callNode->getNumChildren()-1; c >= firstArgIndex; c--)
      {
      int32_t argOrdinal = c - firstArgIndex;

      // Check that argOrdinal is a valid index for ecsArgInfo.
      if (argOrdinal >= ecsArgInfo->getNumArgs())
         {
         traceMsg(comp, "checkForConstClass skipping c=%d because argOrdinal(%d) >= numArgs(%d)\n", c, argOrdinal, ecsArgInfo->getNumArgs());
         continue;
         }

      TR_PrexArgument *prexArgument = ecsArgInfo->get(argOrdinal);

      PrexKnowledgeLevel priorKnowledge = TR_PrexArgument::knowledgeLevel(prexArgument);

      TR::Node *argument = callNode->getChild(c);
      if (tracePrex)
         {
         traceMsg(comp, "checkForConstClass: Child %d [%p] arg %p %s%s %s\n",
                  c, argument, prexArgument, TR_PrexArgument::priorKnowledgeStrings[priorKnowledge],
                  argument->getOpCode().getName(),
                  argument->getOpCode().hasSymbolReference()? argument->getSymbolReference()->getName(comp->getDebug()) : "");
         }

      TR::KnownObjectTable::Index knownObjectIndex;
      bool knownObjectClass = false;

      if (argument->getOpCode().hasSymbolReference() &&
          (argument->getSymbolReference() == comp->getSymRefTab()->findJavaLangClassFromClassSymbolRef()))
         {
         TR::Node *argFirstChild = argument->getFirstChild();
         if (argFirstChild->getOpCode().hasSymbolReference() &&
             argFirstChild->getSymbol()->isStatic() &&
             !argFirstChild->getSymbolReference()->isUnresolved() &&
             argFirstChild->getSymbol()->isClassObject())
            {
            uintptr_t objectReferenceLocation = (uintptr_t)argFirstChild->getSymbolReference()->getSymbol()->castToStaticSymbol()->getStaticAddress();
            if (objectReferenceLocation)
               {
               TR::KnownObjectTable *knot = comp->getOrCreateKnownObjectTable();
               if (knot)
                  {
                  TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());
                  knownObjectIndex = knot->getOrCreateIndexAt((uintptr_t*)(objectReferenceLocation + fej9->getOffsetOfJavaLangClassFromClassField()));
                  knownObjectClass = true;
                  }
               }
            }
         }

      if (argument->getOpCode().hasSymbolReference() && (knownObjectClass || argument->getSymbolReference()->hasKnownObjectIndex()))
         {
         if (priorKnowledge < KNOWN_OBJECT)
            {
            if (knownObjectClass)
               {
               ecsArgInfo->set(argOrdinal, new (comp->trStackMemory()) TR_PrexArgument(knownObjectIndex, comp));
               if (tracePrex)
                  traceMsg(comp, "checkForConstClass: %p: is known object obj%d (knownObjectClass)\n", ecsArgInfo->get(argOrdinal), knownObjectIndex);
               }
            else
               {
               ecsArgInfo->set(argOrdinal, new (comp->trStackMemory()) TR_PrexArgument(argument->getSymbolReference()->getKnownObjectIndex(), comp));
               if (tracePrex)
                  traceMsg(comp, "checkForConstClass: %p: is known object obj%d\n", ecsArgInfo->get(argOrdinal), argument->getSymbolReference()->getKnownObjectIndex());
               }
            }
         }

      } // for each arg

   return;

   } // checkForConstClass

//@TODO this can be re-used as we start building prexargs for every callsite
TR_PrexArgInfo* TR_PrexArgInfo::buildPrexArgInfoForMethodSymbol(TR::ResolvedMethodSymbol* methodSymbol, TR_LogTracer* tracer)
   {
   int numArgs = methodSymbol->getParameterList().getSize();
   TR_ResolvedMethod       *feMethod = methodSymbol->getResolvedMethod();
   ListIterator<TR::ParameterSymbol> parms(&methodSymbol->getParameterList());

   TR::Compilation *comp = tracer->comp();

   TR_PrexArgInfo *argInfo = new (comp->trHeapMemory()) TR_PrexArgInfo(numArgs, comp->trMemory());
   heuristicTrace(tracer, "PREX-CSI:  Populating parmInfo of current method %s\n", feMethod->signature(comp->trMemory()));
   int index = 0;
   /**
    *  For non-static method, first slot of the paramters is populated with the class owning the method. Most of the time,
    *  we should be able to get the class pointer using class signature but in case of hidden classes which according to
    *  JEP 371, cannot be used directly by bytecode instructions in other classes also is not possible to refer to them in
    *  paramters except for the method from the same hidden class. In any case, for non-static methods instead of making
    *  VM query to get the owning class, extract that information from resolvedMethod.
    */
   for (TR::ParameterSymbol *p = parms.getFirst(); p != NULL; index++, p = parms.getNext())
      {
      TR_ASSERT(index < numArgs, "out of bounds!");
      int32_t len = 0;
      const char *sig = p->getTypeSignature(len);

      if (*sig == 'L')
         {
         TR_OpaqueClassBlock *clazz = (index == 0 && !methodSymbol->isStatic()) ? feMethod->containingClass() :  comp->fe()->getClassFromSignature(sig, len, feMethod);
         if (clazz)
            {
            argInfo->set(index, new (comp->trHeapMemory()) TR_PrexArgument(TR_PrexArgument::ClassIsPreexistent, clazz));
            heuristicTrace(tracer, "PREX-CSI:  Parm %d class %p in %p is %.*s\n", index, argInfo->get(index)->getClass(), argInfo->get(index), len, sig);
            }
         }
      }
   return argInfo;
   }


static void populateClassNameSignature(TR::Method *m, TR_ResolvedMethod* caller, TR_OpaqueClassBlock* &c, char* &nc, int32_t &nl, char* &sc, int32_t &sl)
   {
   int32_t len = m->classNameLength();
   char* cs = classNameToSignature(m->classNameChars(), len, TR::comp());
   c = caller->fe()->getClassFromSignature(cs, len, caller);
   nc = m->nameChars();
   nl = m->nameLength();
   sc = m->signatureChars();
   sl = m->signatureLength();
   }

static char* classSignature (TR::Method * m, TR::Compilation* comp) //tracer helper
   {
   int32_t len = m->classNameLength();
   return classNameToSignature(m->classNameChars(), len /*don't care, cos this gives us a null terminated string*/, comp);
   }

static bool treeMatchesCallSite(TR::TreeTop* tt, TR::ResolvedMethodSymbol* callerSymbol, TR_CallSite* callsite, TR_LogTracer* tracer)
   {
   if (tt->getNode()->getNumChildren()>0 &&
       tt->getNode()->getFirstChild()->getOpCode().isCall() &&
       tt->getNode()->getFirstChild()->getByteCodeIndex() == callsite->_bcInfo.getByteCodeIndex())
      {
      TR::Node* callNode =  tt->getNode()->getFirstChild();

      TR::MethodSymbol* callNodeMS = callNode->getSymbolReference()->getSymbol()->castToMethodSymbol();
      TR_ASSERT(callNodeMS, "isCall returned true!");

      if (callNodeMS->isHelper())
         {
         return false;
         }

      TR_OpaqueClassBlock *callSiteClass, *callNodeClass;

      char *callSiteNameChars, *callNodeNameChars,
           *callSiteSignatureChars, *callNodeSignatureChars;

      int32_t callSiteNameLength, callNodeNameLength,
              callSiteSignatureLength, callNodeSignatureLength;


      populateClassNameSignature (callsite->_initialCalleeMethod ?
            callsite->_initialCalleeMethod->convertToMethod() : //TR_ResolvedMethod doesn't extend TR::Method
            callsite->_interfaceMethod,
         callerSymbol->getResolvedMethod(),
         callSiteClass,
         callSiteNameChars, callSiteNameLength,
         callSiteSignatureChars, callSiteSignatureLength
      );


      populateClassNameSignature (callNodeMS->getMethod(),
         callerSymbol->getResolvedMethod(),
         callNodeClass,
         callNodeNameChars, callNodeNameLength,
         callNodeSignatureChars, callNodeSignatureLength
      );



      //make sure classes are compatible

      if (!callNodeClass || !callSiteClass || callerSymbol->getResolvedMethod()->fe()->isInstanceOf (callNodeClass, callSiteClass, true, true, true) != TR_yes)
         {
         if (tracer->heuristicLevel())
            {
            TR::Compilation* comp = TR::comp(); //won't be evaluated unless tracing is on
            heuristicTrace(tracer, "ARGS PROPAGATION: Incompatible classes: callSiteClass %p (%s) callNodeClass %p (%s)",
               callSiteClass,
               classSignature(callsite->_initialCalleeMethod ?
                  callsite->_initialCalleeMethod->convertToMethod() :
                  callsite->_interfaceMethod,
                  comp),
               callNodeClass,
               classSignature(callNodeMS->getMethod(), comp)
            );
            }
         return false;
         }

      //compare names and signatures
      if (callSiteNameLength != callNodeNameLength ||
          strncmp(callSiteNameChars, callNodeNameChars, callSiteNameLength) ||
          callSiteSignatureLength != callNodeSignatureLength ||
          strncmp(callSiteSignatureChars, callNodeSignatureChars, callSiteSignatureLength))
          {
          heuristicTrace(tracer, "ARGS PROPAGATION: Signature mismatch: callSite class %.*s callNode class %.*s",
            callSiteNameLength, callSiteNameChars, callNodeNameLength, callNodeNameChars);
          return false;
          }

      //heuristicTrace(tracer, "ARGS PROPAGATION: matched the node!!!");
      return true;
      }

   return false;
   }

TR::TreeTop* TR_PrexArgInfo::getCallTree(TR::ResolvedMethodSymbol* methodSymbol, TR_CallSite* callsite, TR_LogTracer* tracer)
   {
   if (callsite->_callNodeTreeTop)
      return callsite->_callNodeTreeTop;

   for (TR::TreeTop* tt = methodSymbol->getFirstTreeTop(); tt; tt=tt->getNextTreeTop())
      {
      if (treeMatchesCallSite(tt, methodSymbol, callsite, tracer))
         return tt;
      }

   heuristicTrace(tracer, "ARGS PROPAGATION: Couldn't find a matching node for callsite %p bci %d", callsite, callsite->_bcInfo.getByteCodeIndex());
   return NULL;
   }

TR::Node* TR_PrexArgInfo::getCallNode(TR::ResolvedMethodSymbol* methodSymbol, TR_CallSite* callsite, TR_LogTracer* tracer)
   {
   if (callsite->_callNode)
      return callsite->_callNode;

   for (TR::TreeTop* tt = methodSymbol->getFirstTreeTop(); tt; tt=tt->getNextTreeTop())
      {
      if (treeMatchesCallSite(tt, methodSymbol, callsite, tracer))
         return tt->getNode()->getFirstChild();
      }

   heuristicTrace(tracer, "ARGS PROPAGATION: Couldn't find a matching node for callsite %p bci %d", callsite, callsite->_bcInfo.getByteCodeIndex());
   return NULL;
   }

bool TR_PrexArgInfo::hasArgInfoForChild (TR::Node *child, TR_PrexArgInfo * argInfo)
   {
   if (child->getOpCode().hasSymbolReference() &&
          child->getSymbolReference()->getSymbol()->isParm() &&
          child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal() < argInfo->getNumArgs() &&
          argInfo->get(child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal()))
      return true;


   return false;
   }

TR_PrexArgument* TR_PrexArgInfo::getArgForChild(TR::Node *child, TR_PrexArgInfo* argInfo)
   {
   TR_ASSERT(child->getOpCode().hasSymbolReference() &&
             child->getSymbolReference()->getSymbol()->isParm() &&
             child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal() < argInfo->getNumArgs() && argInfo->get(child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal())
             , "hasArgInfoForChild should have returned false");

   return argInfo->get(child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal());
   }

void TR_PrexArgInfo::propagateReceiverInfoIfAvailable (TR::ResolvedMethodSymbol* methodSymbol, TR_CallSite* callsite,
                                              TR_PrexArgInfo * argInfo, TR_LogTracer *tracer)
   {
   //this implies we have some argInfo available
   TR_ASSERT(argInfo, "otherwise we shouldn't even peek");
   TR::Node* callNode = TR_PrexArgInfo::getCallNode(methodSymbol, callsite, tracer);
   TR::Compilation *comp = tracer->comp();
   heuristicTrace(tracer, "ARGS PROPAGATION: trying to propagate receiver's info for callsite %p at %p", callsite, callNode);
   if (!callNode || comp->getOption(TR_DisableInlinerArgsPropagation))
      return;

   uint32_t numOfArgs = callNode->getNumChildren()-callNode->getFirstArgumentIndex();

   if (numOfArgs<1)
      return;
   //TR_ASSERT(numOfArgs > 0, "argsinfo index out of bounds");

   TR::Node* child = callNode->getChild(callNode->getFirstArgumentIndex());

   if (TR_PrexArgInfo::hasArgInfoForChild(child, argInfo))
      {
      heuristicTrace(tracer, "ARGS PROPAGATION: the receiver for callsite %p is also one of the caller's args", callsite);
      callsite->_ecsPrexArgInfo = new (comp->trHeapMemory()) TR_PrexArgInfo(numOfArgs, comp->trMemory());
      callsite->_ecsPrexArgInfo->set(0, TR_PrexArgInfo::getArgForChild(child, argInfo));
      }
   }

bool TR_PrexArgInfo::validateAndPropagateArgsFromCalleeSymbol(TR_PrexArgInfo* argsFromSymbol, TR_PrexArgInfo* argsFromTarget, TR_LogTracer *tracer)
   {
   if (!argsFromSymbol || !argsFromTarget || tracer->comp()->getOption(TR_DisableInlinerArgsPropagation))
      {
      heuristicTrace(tracer, "ARGS PROPAGATION: argsFromSymbol %p or argsFromTarget %p are missing\n", argsFromSymbol, argsFromTarget);
      return true;
      }

   heuristicTrace(tracer, "ARGS PROPAGATION: argsFromSymbol (from calleeSymbol)");
   if (tracer->heuristicLevel())
      argsFromSymbol->dumpTrace();

   //validation
   TR_FrontEnd* fe = tracer->comp()->fe();
   int32_t numArgsToEnhance = std::min(argsFromTarget->getNumArgs(), argsFromSymbol->getNumArgs());
   for (int32_t i = 0; i < numArgsToEnhance; i++)
      {
      if (!argsFromTarget->get(i) || !argsFromTarget->get(i)->getClass()) //no incoming class info
         continue;

      if (!argsFromSymbol->get(i) || !argsFromSymbol->get(i)->getClass())
         {
         heuristicTrace(tracer, "ARGS PROPAGATION: No class info for arg %d from symbol. ", i);
         return false; //TODO: This can be relaxed
                       //just make a copy of incoming args
                       //and clear the info for this particular slot
         }

      /*
      At this point class types from argsFromSymbol and argsFromTarget MUST be compatible
      Incompatibility might mean that we are inlining dead code
      */
      if (fe->isInstanceOf(argsFromSymbol->get(i)->getClass(), argsFromTarget->get(i)->getClass(), true, true, true) != TR_yes  &&
         fe->isInstanceOf(argsFromTarget->get(i)->getClass(), argsFromSymbol->get(i)->getClass(), true, true, true) != TR_yes)
         {
         return false;
         }
      }


   TR_PrexArgInfo::enhance(argsFromTarget, argsFromSymbol, tracer->comp()); //otherwise just pick more specific

   heuristicTrace(tracer, "ARGS PROPAGATION: final argInfo after merging argsFromTarget %p", argsFromTarget);
   if (tracer->heuristicLevel())
      argsFromTarget->dumpTrace();

   return true;
   }


   void TR_PrexArgInfo::clearArgInfoForNonInvariantArguments(TR::ResolvedMethodSymbol* methodSymbol, TR_LogTracer* tracer)
      {
      if (tracer->comp()->getOption(TR_DisableInlinerArgsPropagation))
         return;

      bool cleanedAnything = false;
      for (TR::TreeTop * tt = methodSymbol->getFirstTreeTop(); tt; tt = tt->getNextTreeTop())
         {
         TR::Node* storeNode = tt->getNode()->getStoreNode();


         if (!storeNode || !storeNode->getSymbolReference()->getSymbol()->isParm())
            continue;

         TR_ASSERT(storeNode->getSymbolReference(), "stores should have symRefs");
         TR::ParameterSymbol*  parmSymbol = storeNode->getSymbolReference()->getSymbol()->getParmSymbol();
         if (parmSymbol->getOrdinal() < getNumArgs())
            {
            debugTrace(tracer, "ARGS PROPAGATION: unsetting an arg [%i] of argInfo %p", parmSymbol->getOrdinal(), this);
            set(parmSymbol->getOrdinal(), NULL);
            cleanedAnything = true;
            }
         }

      if (cleanedAnything)
         {
         debugTrace(tracer, "ARGS PROPAGATION: argInfo %p after clear arg info for non-invariant arguments", this);
         if (tracer->heuristicLevel())
            dumpTrace();
         }
      }

void TR_PrexArgInfo::propagateArgsFromCaller(TR::ResolvedMethodSymbol* methodSymbol, TR_CallSite* callsite,
                           TR_PrexArgInfo * argInfo, TR_LogTracer *tracer)
   {
   if (tracer->comp()->getOption(TR_DisableInlinerArgsPropagation))
      return;

   TR_ASSERT(argInfo, "otherwise we shouldn't even peek");
   TR::Node* callNode = TR_PrexArgInfo::getCallNode(methodSymbol, callsite, tracer);
   heuristicTrace(tracer, "ARGS PROPAGATION: trying to propagate arg info from caller symbol to callsite %p at %p", callsite, callNode);

   if (!callNode)
      return;

   //If we are dealing with indirect calls, temporary use callsite->_ecsPrexArgInfo->get(0)
   //instead of argInfo->get(0). This is because the former might have been reseted by
   //findCallSiteTarget if it couldn't use argInfo->get(0).
   //In such case, propagating argInfo->get(0) any longer might be incorrect.

   TR_PrexArgument* receiverPrexArg = NULL;
   TR::Node *receiverChild = callNode->getChild(callNode->getFirstArgumentIndex());
   if (callsite->_ecsPrexArgInfo)
      {
      if (TR_PrexArgInfo::hasArgInfoForChild(receiverChild, argInfo))
         {
         receiverPrexArg = TR_PrexArgInfo::getArgForChild(receiverChild, argInfo);
         argInfo->set(receiverChild->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal(), callsite->_ecsPrexArgInfo->get(0));
         }
      }

   heuristicTrace(tracer, "ARGS PROPAGATION: argsFromTarget before args propagation");
   for (int i = 0; i < callsite->numTargets(); i++)
      if (tracer->heuristicLevel())
         callsite->getTarget(i)->_ecsPrexArgInfo->dumpTrace();

   for (int i = callNode->getFirstArgumentIndex(); i < callNode->getNumChildren(); i++)
      {
      TR::Node* child = callNode->getChild(i);
      if (TR_PrexArgInfo::hasArgInfoForChild(child, argInfo))
         {
         heuristicTrace(tracer, "ARGS PROPAGATION: arg %d at callsite %p matches caller's arg %d", i, callsite, child->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal());

         for (int j = 0; j < callsite->numTargets(); j++)
            {
            if (!callsite->getTarget(j)->_ecsPrexArgInfo)
               continue;

            TR_PrexArgInfo* targetArgInfo = callsite->getTarget(j)->_ecsPrexArgInfo;

            if (i - callNode->getFirstArgumentIndex() >= targetArgInfo->getNumArgs())
               continue;

            if (!targetArgInfo->get(i - callNode->getFirstArgumentIndex()))
               targetArgInfo->set(i - callNode->getFirstArgumentIndex(), TR_PrexArgInfo::getArgForChild(child, argInfo));
            }
         }
      }

   // Call checkForConstClass on each target so that uses of constant classes
   // are identified. (The information will be used by applyArgumentHeuristics)
   for (int j = 0; j < callsite->numTargets(); j++)
      {
      TR_J9InlinerUtil::checkForConstClass(callsite->getTarget(j), tracer);
      }

   //Restoring argInfo (see setting receiverPrexArg above)
   if (receiverPrexArg)
      {
      argInfo->set(receiverChild->getSymbolReference()->getSymbol()->getParmSymbol()->getOrdinal(), receiverPrexArg);
      }

   if (tracer->heuristicLevel())
      {
      heuristicTrace(tracer, "ARGS PROPAGATION: ArgInfo after propagating the args from the caller");
      for (int i = 0; i < callsite->numTargets(); i++)
         callsite->getTarget(i)->_ecsPrexArgInfo->dumpTrace();
      }
   }

void
TR_J9InlinerUtil::refineColdness(TR::Node* node, bool& isCold)
   {
   bool inlineableJNI = false;
   TR::SymbolReference * symRef = node->getSymbolReference();
   if(symRef->getSymbol()->isResolvedMethod()
         && symRef->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod())
       inlineableJNI = static_cast<TR_J9InlinerPolicy*>(inliner()->getPolicy())->isInlineableJNI(symRef->getSymbol()->castToResolvedMethodSymbol()->getResolvedMethod(),node);

   isCold = isCold && !inlineableJNI;
   }

void
TR_J9InlinerUtil::computeMethodBranchProfileInfo (TR::Block * cfgBlock, TR_CallTarget* calltarget, TR::ResolvedMethodSymbol* callerSymbol)
   {
   if (cfgBlock) //isn't this equal to genILSucceeded??
      {

      TR::ResolvedMethodSymbol * calleeSymbol = calltarget->_calleeSymbol;
      TR::TreeTop * callNodeTreeTop = calltarget->_myCallSite->_callNodeTreeTop;

      TR_MethodBranchProfileInfo *mbpInfo = TR_MethodBranchProfileInfo::getMethodBranchProfileInfo(cfgBlock->getEntry()->getNode()->getInlinedSiteIndex(), comp());
      if (!mbpInfo)
         {
         TR::Block *block = callNodeTreeTop->getEnclosingBlock();

         mbpInfo = TR_MethodBranchProfileInfo::addMethodBranchProfileInfo (cfgBlock->getEntry()->getNode()->getInlinedSiteIndex(), comp());

         calleeSymbol->getFlowGraph()->computeInitialBlockFrequencyBasedOnExternalProfiler(comp());
         uint32_t firstBlockFreq = calleeSymbol->getFlowGraph()->getInitialBlockFrequency();

         int32_t blockFreq = block->getFrequency();
         if (blockFreq < 0)
            blockFreq = 6;

         float freqScaleFactor = 0.0;
         if (callerSymbol->getFirstTreeTop()->getNode()->getBlock()->getFrequency() > 0)
            {
            freqScaleFactor = (float)(blockFreq)/callerSymbol->getFirstTreeTop()->getNode()->getBlock()->getFrequency();
            if (callerSymbol->getFlowGraph()->getInitialBlockFrequency() > 0)
               freqScaleFactor *= (float)(callerSymbol->getFlowGraph()->getInitialBlockFrequency())/(float)firstBlockFreq;
            }
         mbpInfo->setInitialBlockFrequency(firstBlockFreq);
         mbpInfo->setCallFactor(freqScaleFactor);

         calleeSymbol->getFlowGraph()->setFrequencies();

         if (comp()->getOption(TR_TraceBFGeneration))
            {
            traceMsg(comp(), "Setting initial block count for a call with index %d to be %d, call factor %f where block %d (%p) and blockFreq = %d\n", cfgBlock->getEntry()->getNode()->getInlinedSiteIndex(), firstBlockFreq, freqScaleFactor, block->getNumber(), block, blockFreq);
            traceMsg(comp(), "first block freq %d and initial block freq %d\n", callerSymbol->getFirstTreeTop()->getNode()->getBlock()->getFrequency(), callerSymbol->getFlowGraph()->getInitialBlockFrequency());
            }
         }
      }
   }

TR_TransformInlinedFunction *
TR_J9InlinerUtil::getTransformInlinedFunction(TR::ResolvedMethodSymbol *callerSymbol, TR::ResolvedMethodSymbol *calleeSymbol, TR::Block *blockContainingTheCall, TR::TreeTop *callNodeTreeTop,
                                 TR::Node *callNode, TR_ParameterToArgumentMapper & pam, TR_VirtualGuardSelection *guard, List<TR::SymbolReference> & tempList,
                                 List<TR::SymbolReference> & availableTemps, List<TR::SymbolReference> & availableBasicBlockTemps)
   {
   return new (comp()->trStackMemory()) TR_J9TransformInlinedFunction(comp(), tracer(), callerSymbol, calleeSymbol, blockContainingTheCall, callNodeTreeTop, callNode, pam, guard, tempList, availableTemps, availableBasicBlockTemps);
   }

TR_J9TransformInlinedFunction::TR_J9TransformInlinedFunction(
   TR::Compilation *c, TR_InlinerTracer *tracer,TR::ResolvedMethodSymbol * callerSymbol, TR::ResolvedMethodSymbol * calleeSymbol,
   TR::Block * callNodeBlock, TR::TreeTop * callNodeTreeTop, TR::Node * callNode,
   TR_ParameterToArgumentMapper & mapper, TR_VirtualGuardSelection *guard,
   List<TR::SymbolReference> & temps, List<TR::SymbolReference> & availableTemps,
   List<TR::SymbolReference> & availableTemps2)
   : TR_TransformInlinedFunction(c, tracer, callerSymbol, calleeSymbol, callNodeBlock, callNodeTreeTop, callNode, mapper, guard, temps, availableTemps, availableTemps2)
   {
   }

void
TR_J9TransformInlinedFunction::transform(){
   TR_ResolvedMethod * calleeResolvedMethod = _calleeSymbol->getResolvedMethod();
   if (calleeResolvedMethod->isSynchronized() && !_callNode->canDesynchronizeCall())
      {
      if (comp()->trace(OMR::inlining))
         traceMsg(comp(), "Wrapping in try region for synchronized method\n");
      transformSynchronizedMethod(calleeResolvedMethod);
      }
   TR_TransformInlinedFunction::transform();
}

void
TR_J9TransformInlinedFunction::transformSynchronizedMethod(TR_ResolvedMethod * calleeMethod)
   {
   // If an inlined synchronized method ends with a throw then we have to unlock the monitor.
   // The stack unwinder does this if the function isn't inlined, but the unwinder doesn't know
   // about the inlined version (unless or until we enhance the meta data).
   // If we change to use the meta data, care must be taken since some call
   // sites may have been desynchronized even though the method is marked as
   // synchronized.
   //
   wrapCalleeInTryRegion(true, false, calleeMethod);
   }

void
TR_J9TransformInlinedFunction::wrapCalleeInTryRegion(bool isSynchronized, bool putCatchInCaller, TR_ResolvedMethod * calleeMethod)
   {
   TR_InlinerDelimiter delimiter(tracer(),"tif.wrapCalleeInTryRegion");
   int32_t handlerIndex = calleeMethod->numberOfExceptionHandlers();
   TR::TreeTop * prevTreeTop = _calleeSymbol->getLastTreeTop(), * originalLastTreeTop = prevTreeTop;

   TR::CFG  *calleeCFG  = _calleeSymbol->getFlowGraph();
   TR::Block *catchBlock = NULL;
   TR::Block *block      = NULL;
   TR_ScratchList<TR::Block> newCatchBlocks(trMemory());

   TR_CatchBlockProfileInfo * catchInfo = TR_CatchBlockProfileInfo::get(comp());
   if (catchInfo && catchInfo->getCatchCounter() >= TR_CatchBlockProfileInfo::EDOThreshold)
      {
      // For each explicit throw in the callee add an explicit catch block so that we have a chance
      // of converting throws to gotos.
      //
      for (TR::TreeTop * tt = _calleeSymbol->getFirstTreeTop(); tt != originalLastTreeTop; tt = tt->getNextTreeTop())
         {
         TR::Node * node = tt->getNode();
         if (node->getOpCodeValue() == TR::BBStart)
            block = node->getBlock();
         else if (node->getNumChildren() > 0 &&
                  (node = node->getFirstChild())->getOpCodeValue() == TR::athrow &&
                  (node = node->getFirstChild())->getOpCodeValue() == TR::New &&
                  (node = node->getFirstChild())->getOpCodeValue() == TR::loadaddr &&
                  node->getSymbol()->isClassObject() && !node->getSymbolReference()->isUnresolved())
            {
            TR::SymbolReference * symRef = node->getSymbolReference();
            int32_t catchBlockHandler = handlerIndex++;
            prevTreeTop = createThrowCatchBlock(isSynchronized, putCatchInCaller, calleeCFG, block, prevTreeTop, symRef, catchBlockHandler, newCatchBlocks);
            }
         }
      }

   if (isSynchronized)
      catchBlock = appendCatchBlockForInlinedSyncMethod(calleeMethod, prevTreeTop, 0, handlerIndex);
   else
      catchBlock = appendCatchBlockToRethrowException(calleeMethod, prevTreeTop, putCatchInCaller, 0, handlerIndex, true);

   TR::Block * monEnterBlock = _calleeSymbol->getFirstTreeTop()->getNode()->getBlock();
   for (TR::CFGNode * n = calleeCFG->getFirstNode(); n; n = n->getNext())
      if (!catchBlock->hasSuccessor(n) &&
         (!isSynchronized || (n != monEnterBlock && !isSyncReturnBlock(comp(), toBlock(n)))) &&
          !toBlock(n)->isOSRCodeBlock() &&
          !toBlock(n)->isOSRCatchBlock())
         calleeCFG->addExceptionEdge(n, catchBlock);

   // now add the catch blocks (important to do it here so that the above iterator doesn't find these blocks)
   calleeCFG->addNode(catchBlock);

   ListIterator<TR::Block> bi(&newCatchBlocks);
   for (TR::Block * b = bi.getFirst(); b; b = bi.getNext())
      calleeCFG->addNode(b);

   if (comp()->trace(OMR::inlining))
      comp()->dumpMethodTrees("Callee Trees", _calleeSymbol);
   }

TR::TreeTop *
TR_J9TransformInlinedFunction::createThrowCatchBlock(bool isSynchronized, bool putCatchInCaller,
      TR::CFG *calleeCFG, TR::Block *block, TR::TreeTop *prevTreeTop,
      TR::SymbolReference *symRef, int32_t handlerIndex,
      TR_ScratchList<TR::Block> & newCatchBlocks)
   {
   TR_InlinerDelimiter delimiter(tracer(),"tif.createThrowCatchBlock");
   TR::Block *catchBlock;
   if (isSynchronized)
      {
      catchBlock = appendCatchBlockForInlinedSyncMethod(
                      symRef->getOwningMethod(comp()), prevTreeTop, symRef->getCPIndex(), handlerIndex, false);
      catchBlock->setSpecializedDesyncCatchBlock();
      catchBlock->setIsSynchronizedHandler();
      }
   else
      catchBlock = appendCatchBlockToRethrowException(
                      symRef->getOwningMethod(comp()), prevTreeTop, putCatchInCaller, symRef->getCPIndex(), handlerIndex, false);

   TR::TreeTop *lastRealTree = catchBlock->getLastRealTreeTop();
   if (!lastRealTree->getNode()->getOpCode().isBranch())  // if !isSynchronized, this condition will be true
      prevTreeTop = catchBlock->getExit();
   else
      {
      TR::Block *monexitBlock = catchBlock->getExit()->getNextTreeTop()->getNode()->getBlock();
      TR::Block *rethrowBlock = lastRealTree->getNode()->getBranchDestination()->getNode()->getBlock();
      prevTreeTop = rethrowBlock->getExit();
      newCatchBlocks.add(monexitBlock);
      newCatchBlocks.add(rethrowBlock);
      }
   calleeCFG->addExceptionEdge(block, catchBlock);
   newCatchBlocks.add(catchBlock);

   return prevTreeTop;
   }

TR::Block *
TR_J9TransformInlinedFunction::appendCatchBlockToRethrowException(
   TR_ResolvedMethod * calleeMethod, TR::TreeTop * prevTreeTop, bool putCatchInCaller, int32_t catchType, int32_t handlerIndex, bool addBlocks)
   {
   TR_InlinerDelimiter delimiter(tracer(),"tif.appendCatchBlockToRethrowException");
   TR::SymbolReferenceTable * symRefTab = comp()->getSymRefTab();

   TR::Node *modelNode;
   if (putCatchInCaller)
      modelNode = _callNode;
   else
      modelNode = _calleeSymbol->getFirstTreeTop()->getNode();
   //TR::Node * lastNode = prevTreeTop->getNode();

   TR::Block * catchBlock = TR::Block::createEmptyBlock(modelNode, comp());
   catchBlock->setHandlerInfo(catchType, (uint8_t)comp()->getInlineDepth(), handlerIndex, calleeMethod, comp());

   if (comp()->getOption(TR_EnableThisLiveRangeExtension))
      {
      if (!_calleeSymbol->isStatic() &&
          (!comp()->fej9()->isClassFinal(_calleeSymbol->getResolvedMethod()->containingClass()) ||
           comp()->fej9()->hasFinalizer(_calleeSymbol->getResolvedMethod()->containingClass())))
         {
         TR::Node *anchoredThis = TR::Node::createWithSymRef(modelNode, TR::aload, 0, symRefTab->findOrCreateAutoSymbol(_calleeSymbol, 0, TR::Address));
         TR::SymbolReference *tempSymRef = comp()->getSymRefTab()->findOrCreateThisRangeExtensionSymRef(_calleeSymbol);
         TR::TreeTop *storeTT = TR::TreeTop::create(comp(), TR::Node::createStore(tempSymRef, anchoredThis));
         catchBlock->append(storeTT);
         }
      }

   // rethrow the exception
   //
   TR::SymbolReference * tempSymRef = 0;
   TR::Node * loadExcpSymbol = TR::Node::createWithSymRef(modelNode, TR::aload, 0, symRefTab->findOrCreateExcpSymbolRef());
   catchBlock->append(TR::TreeTop::create(comp(), TR::Node::createWithSymRef(TR::athrow, 1, 1, loadExcpSymbol, symRefTab->findOrCreateAThrowSymbolRef(_calleeSymbol))));

   TR::CFG * calleeCFG = _calleeSymbol->getFlowGraph();
   calleeCFG->addEdge(catchBlock, calleeCFG->getEnd());

   prevTreeTop->join(catchBlock->getEntry());
   return catchBlock;
   }
// } RTSJ Support ends

TR::Block *
TR_J9TransformInlinedFunction::appendCatchBlockForInlinedSyncMethod(
   TR_ResolvedMethod * calleeResolvedMethod, TR::TreeTop * prevTreeTop, int32_t catchType, int32_t handlerIndex, bool addBlocks)
   {
   TR_InlinerDelimiter delimiter(tracer(),"tif.appendCatchBlockForInlinedSyncMethod");
   TR::SymbolReferenceTable * symRefTab = comp()->getSymRefTab();

   TR::Node * lastNode = _calleeSymbol->getFirstTreeTop()->getNode(); //prevTreeTop->getNode();
   TR::Block * catchBlock = TR::Block::createEmptyBlock(lastNode, comp());
   catchBlock->setHandlerInfo(catchType, (uint8_t)comp()->getInlineDepth(), handlerIndex, calleeResolvedMethod, comp());
   catchBlock->setIsSynchronizedHandler();
   catchBlock->setIsSyntheticHandler();

   // store the exception symbol into a temp
   //
   TR::SymbolReference * tempSymRef = 0;
   TR::Node * excpSymbol = TR::Node::createWithSymRef(lastNode, TR::aload, 0, symRefTab->findOrCreateExcpSymbolRef());
   OMR_InlinerUtil::storeValueInATemp(comp(), excpSymbol, tempSymRef, catchBlock->getEntry(), _callerSymbol, _tempList, _availableTemps, &_availableTemps2);

   // unlock the monitor
   //
   TR::Node * monitorArg, *monitorArgHandle;
   if (_calleeSymbol->isStatic())
      if (TR::Compiler->cls.classesOnHeap())
         {
         monitorArgHandle = TR::Node::createWithSymRef(lastNode, TR::loadaddr, 0,
                                            symRefTab->findOrCreateClassSymbol (_calleeSymbol, 0, _calleeSymbol->getResolvedMethod()->containingClass()));
         monitorArgHandle = TR::Node::createWithSymRef(TR::aloadi, 1, 1, monitorArgHandle, symRefTab->findOrCreateJavaLangClassFromClassSymbolRef());
         }
      else
         {
         monitorArgHandle = TR::Node::createWithSymRef(lastNode, TR::loadaddr, 0,
                                            symRefTab->findOrCreateClassSymbol (_calleeSymbol, 0, _calleeSymbol->getResolvedMethod()->containingClass()));
         }
   else
      monitorArgHandle = TR::Node::createWithSymRef(lastNode, TR::aload, 0, symRefTab->findOrCreateAutoSymbol(_calleeSymbol, 0, TR::Address));

   TR::CFG * calleeCFG = _calleeSymbol->getFlowGraph();
   TR::Block *monexitBlock = catchBlock;
   TR::Block *rethrowBlock = catchBlock;
   bool createdStoreForMonitorExit = false;
   if (!_calleeSymbol->isStatic())
      {
      monexitBlock = TR::Block::createEmptyBlock(lastNode, comp());
      rethrowBlock = TR::Block::createEmptyBlock(lastNode, comp());
      if (addBlocks)
         {
         calleeCFG->addNode(monexitBlock);
         calleeCFG->addNode(rethrowBlock);
         }

         monitorArg = monitorArgHandle;

      if (!comp()->getOption(TR_DisableLiveMonitorMetadata) &&
            _calleeSymbol->isSynchronised() &&
            _calleeSymbol->getSyncObjectTemp())
         {
         TR::TreeTop *storeTT = TR::TreeTop::create(comp(), (TR::Node::create(lastNode,TR::monexitfence,0)));
         catchBlock->append(storeTT);
         createdStoreForMonitorExit = true;
         }

      TR::Node *ifNode = TR::Node::createif(TR::ifacmpeq, monitorArg->duplicateTree(), TR::Node::aconst(monitorArg, 0),rethrowBlock->getEntry());
      catchBlock->append(TR::TreeTop::create(comp(), ifNode));
      ifNode->getByteCodeInfo().setDoNotProfile(1);

      catchBlock->getExit()->join(monexitBlock->getEntry());
      monexitBlock->getExit()->join(rethrowBlock->getEntry());
      calleeCFG->addEdge(monexitBlock, rethrowBlock);
      calleeCFG->addEdge(catchBlock, rethrowBlock);
      calleeCFG->addEdge(catchBlock, monexitBlock);
      }
   else
      monitorArg = monitorArgHandle;


   // add the store to track liveMonitors
   //
   if (!comp()->getOption(TR_DisableLiveMonitorMetadata) &&
         !createdStoreForMonitorExit &&
         _calleeSymbol->isSynchronised() &&
         _calleeSymbol->getSyncObjectTemp())
      {
      TR::Node *addrNode = TR::Node::create(monitorArg, TR::iconst, 0, 0);
      TR::TreeTop *storeTT = TR::TreeTop::create(comp(), (TR::Node::create(lastNode,TR::monexitfence,0)));
      monexitBlock->append(storeTT);
      }

   TR::Node *monexitNode = TR::Node::createWithSymRef(TR::monexit, 1, 1, monitorArg, symRefTab->findOrCreateMonitorExitSymbolRef(_calleeSymbol));
   monexitNode->setSyncMethodMonitor(true);
   monexitBlock->append(TR::TreeTop::create(comp(), monexitNode));

   if (comp()->getOption(TR_EnableThisLiveRangeExtension))
      {
      if (!_calleeSymbol->isStatic() &&
          (!comp()->fej9()->isClassFinal(_calleeSymbol->getResolvedMethod()->containingClass()) ||
           comp()->fej9()->hasFinalizer(_calleeSymbol->getResolvedMethod()->containingClass())))
         {
         TR::Node *anchoredThis = TR::Node::createWithSymRef(lastNode, TR::aload, 0, symRefTab->findOrCreateAutoSymbol(_calleeSymbol, 0, TR::Address));
         TR::SymbolReference *tempSymRef = comp()->getSymRefTab()->findOrCreateThisRangeExtensionSymRef(_calleeSymbol);
         TR::TreeTop *storeTT = TR::TreeTop::create(comp(), TR::Node::createStore(tempSymRef, anchoredThis));
         monexitBlock->append(storeTT);
         }
      }


   // rethrow the exception
   //
   TR::Node * temp = TR::Node::createWithSymRef(lastNode, TR::aload, 0, tempSymRef);
   rethrowBlock->append(TR::TreeTop::create(comp(), TR::Node::createWithSymRef(TR::athrow, 1, 1, temp, symRefTab->findOrCreateThrowUnreportedExceptionSymbolRef(_calleeSymbol))));

   calleeCFG->addEdge(rethrowBlock, calleeCFG->getEnd());

   prevTreeTop->join(catchBlock->getEntry());
   return catchBlock;
   }

bool
TR_J9TransformInlinedFunction::isSyncReturnBlock(TR::Compilation *comp, TR::Block * b)
   {
   TR::TreeTop * tt = b->getEntry();
   if (!tt) return false;

   tt = tt->getNextTreeTop();
   TR::Node * node = tt->getNode();

   if (node->getOpCode().getOpCodeValue() == TR::monexitfence)
      tt = tt->getNextTreeTop();

   if (node->getOpCode().isStore() && (node->getSymbolReference() == comp->getSymRefTab()->findThisRangeExtensionSymRef()))
      tt = tt->getNextTreeTop();

   node = tt->getNode();
   if (node->getOpCodeValue() == TR::treetop || node->getOpCode().isNullCheck())
      node = node->getFirstChild();

   if (node->getOpCodeValue() != TR::monexit)
      return false;

   tt = tt->getNextTreeTop();
   if (!tt || !tt->getNode()->getOpCode().isReturn())
      return false;

   return true;
   }

/*
 * if the initialCalleeMethod of this callsite is not overridden, add this method as the target of the callsite
 */
bool
TR_J9InlinerUtil::addTargetIfMethodIsNotOverridenInReceiversHierarchy(TR_IndirectCallSite *callsite)
   {
   TR_PersistentCHTable *chTable = comp()->getPersistentInfo()->getPersistentCHTable();

   if( !chTable->isOverriddenInThisHierarchy(callsite->_initialCalleeMethod, callsite->_receiverClass, callsite->_vftSlot, comp()) &&
       !comp()->getOption(TR_DisableHierarchyInlining))
      {
      if(comp()->trace(OMR::inlining))
         {
         int32_t len;
         bool isClassObsolete = comp()->getPersistentInfo()->isObsoleteClass((void*)callsite->_receiverClass, comp()->fe());
         if(!isClassObsolete)
            {
            char *s = TR::Compiler->cls.classNameChars(comp(), callsite->_receiverClass, len);
            heuristicTrace(tracer(),"Virtual call to %s is not overridden in the hierarchy of thisClass %*s\n",tracer()->traceSignature(callsite->_initialCalleeMethod), len, s);
            }
         else
            {
            heuristicTrace(tracer(),"Virtual call to %s is not overridden in the hierarchy of thisClass <obsolete class>\n",tracer()->traceSignature(callsite->_initialCalleeMethod));
            }
         }

      TR_VirtualGuardSelection *guard = (fe()->classHasBeenExtended(callsite->_receiverClass)) ?
         new (comp()->trHeapMemory()) TR_VirtualGuardSelection(TR_HierarchyGuard, TR_MethodTest) :
         new (comp()->trHeapMemory()) TR_VirtualGuardSelection(TR_HierarchyGuard, TR_VftTest, callsite->_receiverClass);
      callsite->addTarget(comp()->trMemory(),inliner(),guard,callsite->_initialCalleeMethod,callsite->_receiverClass,heapAlloc);
      return true;
      }
   return false;
   }

int32_t
TR_J9InlinerUtil::getCallCount(TR::Node *callNode)
   {
   return comp()->fej9()->getIProfilerCallCount(callNode->getByteCodeInfo(), comp());
   }

TR_ResolvedMethod*
TR_J9InlinerUtil::findSingleJittedImplementer(TR_IndirectCallSite *callsite)
   {
   return comp()->getPersistentInfo()->getPersistentCHTable()->findSingleJittedImplementer(callsite->_receiverClass, callsite->_vftSlot, callsite->_callerResolvedMethod, comp(), callsite->_initialCalleeSymbol);
   }

bool
TR_J9InlinerUtil::addTargetIfThereIsSingleImplementer (TR_IndirectCallSite *callsite)
   {
   static bool disableSingleJittedImplementerInlining = feGetEnv("TR_DisableSingleJittedImplementerInlining")  ? true : false;
   TR_ResolvedMethod *implementer;        // A temp to be used to find an implementer in abstract implementer analysis
   //findSingleJittedImplementer J9Virtual also knows about interfaces needs to be virtual
   if (!disableSingleJittedImplementerInlining && comp()->getMethodHotness() >= hot &&
            (implementer = callsite->findSingleJittedImplementer(inliner())))
      {
      if (comp()->trace(OMR::inlining))
         traceMsg(comp(), "inliner: Abstract method %s currently has a single jitted implementation %s\n",
                 inliner()->tracer()->traceSignature(callsite->_initialCalleeMethod), implementer->signature(comp()->trMemory()));

      if (!comp()->cg()->getSupportsProfiledInlining())
         {
         return false;
         }

      TR_VirtualGuardSelection *guard;
      if (callsite->_receiverClass && !fe()->classHasBeenExtended(callsite->_receiverClass))
         guard = new (comp()->trHeapMemory()) TR_VirtualGuardSelection(TR_ProfiledGuard, TR_VftTest, implementer->classOfMethod());
      else
         guard = new (comp()->trHeapMemory()) TR_VirtualGuardSelection(TR_ProfiledGuard, TR_MethodTest);
      callsite->addTarget(comp()->trMemory(),inliner(),guard,implementer,implementer->classOfMethod(),heapAlloc);
      return true;
      }
   return false;
   }

TR_PrexArgInfo*
TR_J9InlinerUtil::createPrexArgInfoForCallTarget(TR_VirtualGuardSelection *guard, TR_ResolvedMethod *implementer)
   {
   TR_PrexArgInfo *myPrexArgInfo = NULL;
   //if CSI (context sensitive inlining + args propagation) enabled we still want to create an argInfo for args propagation
   if (!comp()->getOption(TR_DisableInlinerArgsPropagation) && comp()->fej9()->supportsContextSensitiveInlining())
      {
      //rather than sticking in a not-null check in TR_J9EstimateCodeSize::realEstimateCodeSize and duplicating the line below
      //we might as well put a supportsContextSensitiveInlining check in here
      myPrexArgInfo = new (comp()->trHeapMemory()) TR_PrexArgInfo(implementer->numberOfParameters(), comp()->trMemory());
      if( guard->_type == TR_VftTest)
         {

         TR_ASSERT(implementer, "no implementer!\n");
         TR_ASSERT(!implementer->isStatic(), "method is static\n");

         myPrexArgInfo->set(0, new (comp()->trHeapMemory()) TR_PrexArgument(TR_PrexArgument::ClassIsFixed, guard->_thisClass));

         if (tracer()->heuristicLevel())
            {
            int32_t len;
            char *s = TR::Compiler->cls.classNameChars(comp(), guard->_thisClass, len);
            heuristicTrace(tracer(),"Created an argInfo to fix receiver to class %s",s);
            }
         }

      bool isArchetypeSpecimen =
         implementer->convertToMethod()->isArchetypeSpecimen()
         && implementer->getMethodHandleLocation() != NULL;

      bool isMCS = guard->_kind == TR_MutableCallSiteTargetGuard;

      bool isLambdaFormMCS =
         isMCS && comp()->fej9()->isLambdaFormGeneratedMethod(implementer);

      if ((isArchetypeSpecimen || isLambdaFormMCS) && comp()->getOrCreateKnownObjectTable())
         {
         TR::KnownObjectTable::Index mhIndex = TR::KnownObjectTable::UNKNOWN;
         if (isLambdaFormMCS)
            {
            mhIndex = guard->_mutableCallSiteEpoch;
            }
         else
            {
            uintptr_t *mhLocation = implementer->getMethodHandleLocation();
            mhIndex = comp()->getKnownObjectTable()->getOrCreateIndexAt(mhLocation);
            }

         auto prexArg = new (comp()->trHeapMemory()) TR_PrexArgument(mhIndex, comp());
         if (isMCS)
            prexArg->setTypeInfoForInlinedBody();
         myPrexArgInfo->set(0, prexArg);
         }
      }
   return myPrexArgInfo;
   }

TR_InnerPreexistenceInfo *
TR_J9InlinerUtil::createInnerPrexInfo(TR::Compilation * c, TR::ResolvedMethodSymbol *methodSymbol, TR_CallStack *callStack,
                                    TR::TreeTop *callTree, TR::Node *callNode,
                                    TR_VirtualGuardKind guardKind)
   {
   return new(comp()->trStackMemory())TR_J9InnerPreexistenceInfo(c, methodSymbol, callStack, callTree, callNode, guardKind);
   }

//---------------------------------------------------------------------
// TR_J9InnerPreexistenceInfo::ParmInfo
//---------------------------------------------------------------------
TR_J9InnerPreexistenceInfo::ParmInfo::ParmInfo(TR::ParameterSymbol *innerParm, TR::ParameterSymbol *outerParm)
   : _innerParm(innerParm), _outerParm(outerParm), _isInvariant(true)
   {}

bool
TR_J9InnerPreexistenceInfo::perform(TR::Compilation *comp, TR::Node *guardNode, bool & disableTailRecursion)
   {
   static char *disable = feGetEnv("TR_DisableIPREX");
   if (disable ||
       !comp->getOptimizer()->isEnabled(OMR::innerPreexistence) ||
       comp->getOption(TR_FullSpeedDebug) ||
       comp->getHCRMode() != TR::none ||
       guardNode->isHCRGuard() ||
       guardNode->isBreakpointGuard() ||
       comp->compileRelocatableCode())
      return false;

   // perform() is a misnomer -- most of the work is already done by the constructor
   // at this stage - we just find what is the best way to utilize the information
   //
   if (!comp->performVirtualGuardNOPing())
      return false;

   // If we have inner assumptions - then we must register the assumptions on the
   // virtual guard
   //
   if (hasInnerAssumptions())
      {
      TR_VirtualGuard *virtualGuard = comp->findVirtualGuardInfo(guardNode);
      TR_ASSERT(virtualGuard, "Must have an outer guard to have inner assumptions");

      disableTailRecursion = true;
      ListIterator<TR_InnerAssumption> it(&getInnerAssumptions());
      for (TR_InnerAssumption *a = it.getFirst(); a; a = it.getNext())
         virtualGuard->addInnerAssumption(a);
      }
   else
      {
      // Else, see if we can directly devirtualize this call by using inner preexistence
      //
      TR_VirtualGuard *virtualGuard = comp->findVirtualGuardInfo(guardNode);
      PreexistencePoint *point = getPreexistencePoint(0); // ie. see if the 'this' for the call preexists
      if (point &&
            performTransformation(comp, "%sIPREX: remove virtual guard for inlined call %p to %s because it inner preexists parm ordinal %d of %s\n",
                                   OPT_DETAILS, _callNode, _methodSymbol->getResolvedMethod()->signature(trMemory()),
                                   point->_ordinal, point->_callStack->_methodSymbol->getResolvedMethod()->signature(trMemory())))
         {
         TR_ASSERT(virtualGuard, "we cannot directly devirtualize anything thats not guarded");

         //_callNode->devirtualizeCall(_callTree);

         // Add an inner assumption on the outer guard
         //
         TR_InnerAssumption *a  = new (comp->trHeapMemory()) TR_InnerAssumption(point->_ordinal, virtualGuard);
         ((TR_J9InnerPreexistenceInfo *)point->_callStack->_innerPrexInfo)->addInnerAssumption(a);
         disableTailRecursion = true;

         // Tell compilation that this guard is to be removed
         //
         comp->removeVirtualGuard(virtualGuard);

         // "Remove" the guard node
         //
         TR_ASSERT(guardNode->getOpCodeValue() == TR::ificmpne ||
                guardNode->getOpCodeValue() == TR::iflcmpne ||
                guardNode->getOpCodeValue() == TR::ifacmpne,
                "Wrong kind of if discovered for a virtual guard");
         guardNode->getFirstChild()->recursivelyDecReferenceCount();
         guardNode->setAndIncChild(0, guardNode->getSecondChild());
         guardNode->resetIsTheVirtualGuardForAGuardedInlinedCall();

         // FIXME:
         //printf("---$$$--- inner prex in %s\n", comp->signature());

         ((TR::Optimizer*)comp->getOptimizer())->setRequestOptimization(OMR::treeSimplification, true);

         return true;
         }
      }
   return false;
   }

//---------------------------------------------------------------------
// TR_J9InnerPreexistenceInfo
//---------------------------------------------------------------------

TR_J9InnerPreexistenceInfo::TR_J9InnerPreexistenceInfo(TR::Compilation * c, TR::ResolvedMethodSymbol *methodSymbol,
      TR_CallStack *callStack, TR::TreeTop *treeTop,
      TR::Node *callNode, TR_VirtualGuardKind guardKind)
   :TR_InnerPreexistenceInfo(c, methodSymbol, callStack, treeTop, callNode, guardKind)
   {
   static char *disable = feGetEnv("TR_DisableIPREX");
   if (!c->getOptimizer()->isEnabled(OMR::innerPreexistence) ||
       c->compileRelocatableCode() ||
       disable ||
       !_methodSymbol ||
       c->getHCRMode() == TR::traditional)
      return;

   _numArgs = methodSymbol->getParameterList().getSize();
   _parameters = (ParmInfo **) trMemory()->allocateStackMemory(_numArgs * sizeof(ParmInfo*));
   memset(_parameters, 0, _numArgs * sizeof(ParmInfo*));

   // Initialize the Parameter Info Array
   //
   ListIterator<TR::ParameterSymbol> parmIt(&methodSymbol->getParameterList());
   int32_t ordinal = 0;
   for (TR::ParameterSymbol *p = parmIt.getFirst(); p; p = parmIt.getNext(), ordinal++)
      {
      if (p->getDataType() == TR::Address)
         {
         _parameters[ordinal] = new (trStackMemory()) ParmInfo(p);
         }
      }

   // Walk the IL of the method to find out which parms are invariant
   //
   for (TR::TreeTop *tt = methodSymbol->getFirstTreeTop();
         tt; tt = tt->getNextRealTreeTop())
      {
      TR::Node *node = tt->getNode();
      if (node->getOpCodeValue() == TR::treetop)
         node = node->getFirstChild();

      if (node->getOpCode().isStoreDirect() && node->getDataType() == TR::Address)
         {
         TR::Symbol *symbol = node->getSymbolReference()->getSymbol();
         if (symbol->isParm())
            {
            getParmInfo(symbol->getParmSymbol()->getOrdinal())->setNotInvariant();
            }
         }
      }

   // Figure out how the parms of the caller method tie together with the parms
   // of this method
   //
   if (_callNode) // we are being inlined
      {
      TR::Node *node = _callNode;
      TR_ASSERT(callStack, "must have a call stack if we are being inlined from somewhere\n");

      int32_t firstArgIndex = node->getFirstArgumentIndex();
      for (int32_t c = node->getNumChildren() - 1; c >= firstArgIndex; --c)
         {
         TR::Node *argument = node->getChild(c);
         if (argument->getOpCodeValue() == TR::aload)
            {
            TR::ParameterSymbol *parmSymbol = argument->getSymbolReference()->getSymbol()->getParmSymbol();
            if (parmSymbol && c - firstArgIndex<ordinal)
               {
               ParmInfo *parmInfo = getParmInfo(c - firstArgIndex);
               if (parmInfo) parmInfo->setOuterSymbol(parmSymbol);
               }
            }
         }
      }

   }

TR_J9InnerPreexistenceInfo::PreexistencePoint *
TR_J9InnerPreexistenceInfo::getPreexistencePoint(int32_t ordinal)
   {
   if (hasInnerAssumptions()) return 0;
   ParmInfo *parmInfo = getParmInfo(ordinal);
   if (!parmInfo->_outerParm) return 0;
   if (!_callStack) return 0;

   return ((TR_J9InnerPreexistenceInfo *)_callStack->_innerPrexInfo)->getPreexistencePointImpl(parmInfo->_outerParm->getOrdinal(), _callStack);
   }

TR_J9InnerPreexistenceInfo::PreexistencePoint *
TR_J9InnerPreexistenceInfo::getPreexistencePointImpl(int32_t ordinal, TR_CallStack *prevCallStack)
   {
   ParmInfo *parmInfo = getParmInfo(ordinal);
   if (!parmInfo->isInvariant()) return 0;
   if (!_callStack) return 0;

   PreexistencePoint *point = 0;
   if (parmInfo->_outerParm)
      point = ((TR_J9InnerPreexistenceInfo *)_callStack->_innerPrexInfo)->getPreexistencePointImpl(parmInfo->_outerParm->getOrdinal(), _callStack);

   if (!point)
      {

      if (_guardKind != TR_ProfiledGuard && (_guardKind != TR_NoGuard || !comp()->hasIntStreamForEach())) // FIXME: this limitation can be removed by doing the tree transformation
         point = new (trStackMemory()) PreexistencePoint(prevCallStack, ordinal);
      }

   return point;
   }

bool TR_J9InlinerPolicy::dontPrivatizeArgumentsForRecognizedMethod(TR::RecognizedMethod recognizedMethod)
   {
   static char *aggressiveJSR292Opts = feGetEnv("TR_aggressiveJSR292Opts");
   if (aggressiveJSR292Opts && strchr(aggressiveJSR292Opts, '2'))
      {
      switch (recognizedMethod)
         {
         case TR::java_lang_invoke_MethodHandle_invokeExactTargetAddress:
            return true;

         default:
            break;
         }
      }
   return false;
   }

bool
TR_J9InlinerPolicy::replaceSoftwareCheckWithHardwareCheck(TR_ResolvedMethod *calleeMethod)
   {
   if (calleeMethod && comp()->cg()->getSupportsBDLLHardwareOverflowCheck() &&
    ((strncmp(calleeMethod->signature(comp()->trMemory()), "java/math/BigDecimal.noLLOverflowAdd(JJJ)Z", 42) == 0) ||
     (strncmp(calleeMethod->signature(comp()->trMemory()), "java/math/BigDecimal.noLLOverflowMul(JJJ)Z", 42) == 0)))
      return true;
   else return false;
   }

bool
TR_J9InlinerPolicy::suitableForRemat(TR::Compilation *comp, TR::Node *callNode, TR_VirtualGuardSelection *guard)
   {
   float profiledGuardProbabilityThreshold = 0.6f;
   static char *profiledGuardProbabilityThresholdStr = feGetEnv("TR_ProfiledGuardRematProbabilityThreshold");
   if (profiledGuardProbabilityThresholdStr)
      {
      profiledGuardProbabilityThreshold = ((float)atof(profiledGuardProbabilityThresholdStr));
      }

   bool suitableForRemat = true;
   TR_AddressInfo *valueInfo = static_cast<TR_AddressInfo*>(TR_ValueProfileInfoManager::getProfiledValueInfo(callNode, comp, AddressInfo));
   if (guard->isHighProbablityProfiledGuard())
      {
      if (comp->getMethodHotness() <= warm && comp->getPersistentInfo()->getJitState() == STARTUP_STATE)
         {
         suitableForRemat = false;
         TR::DebugCounter::incStaticDebugCounter(comp, TR::DebugCounter::debugCounterName(comp, "profiledPrivArgRemat/unsuitableForRemat/warmHighProb"));
         }
      else
         {
         TR::DebugCounter::incStaticDebugCounter(comp, TR::DebugCounter::debugCounterName(comp, "profiledPrivArgRemat/suitableForRemat/highProb"));
         }
      }
   else if (valueInfo)
      {
      if (valueInfo->getTopProbability() >= profiledGuardProbabilityThreshold)
         {
         TR::DebugCounter::incStaticDebugCounter(comp, TR::DebugCounter::debugCounterName(comp, "profiledPrivArgRemat/suitableForRemat/probability=%d", ((int32_t)(valueInfo->getTopProbability() * 100))));
         }
      else
         {
         TR::DebugCounter::incStaticDebugCounter(comp, TR::DebugCounter::debugCounterName(comp, "profiledPrivArgRemat/unsuitableForRemat/probability=%d", ((int32_t)(valueInfo->getTopProbability() * 100))));
         suitableForRemat = false;
         }
      }
   else
      {
      TR::DebugCounter::incStaticDebugCounter(comp, TR::DebugCounter::debugCounterName(comp, "profiledPrivArgRemat/unsuitableForRemat/noinfo"));
      suitableForRemat = false;
      }
   return suitableForRemat;
   }

TR_J9InlinerTracer::TR_J9InlinerTracer(TR::Compilation *comp, TR_FrontEnd *fe, TR::Optimization *opt)
   : TR_InlinerTracer(comp, fe, opt)
   {}

TR_InlinerTracer *
TR_J9InlinerUtil::getInlinerTracer(TR::Optimization *optimization)
   {
   return new (comp()->trHeapMemory()) TR_J9InlinerTracer(comp(),fe(),optimization);
   }

void TR_J9InlinerTracer::dumpProfiledClasses (ListIterator<TR_ExtraAddressInfo>& sortedValuesIt, uint32_t totalFrequency)
   {
   if(heuristicLevel())
      {
      TR_ExtraAddressInfo *profiledInfo;
      for (profiledInfo = sortedValuesIt.getFirst(); profiledInfo != NULL; profiledInfo = sortedValuesIt.getNext())
         {
         int32_t freq = profiledInfo->_frequency;
         TR_OpaqueClassBlock* tempreceiverClass = (TR_OpaqueClassBlock *) profiledInfo->_value;
         float val = (float)freq/(float)totalFrequency;
         int32_t len = 1;
         bool isClassObsolete = comp()->getPersistentInfo()->isObsoleteClass((void*)tempreceiverClass, comp()->fe());

         if(!isClassObsolete)
           {
           const char *className = TR::Compiler->cls.classNameChars(comp(), tempreceiverClass, len);
           heuristicTrace(this , "receiverClass %s has a profiled frequency of %f", className,val);
           }
         else
           {
           heuristicTrace(this, "receiverClass %p is obsolete and has profiled frequency of %f",tempreceiverClass,val);
           }
         }
      }

   }

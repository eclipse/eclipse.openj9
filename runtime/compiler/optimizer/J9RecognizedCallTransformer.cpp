/*******************************************************************************
* Copyright (c) 2017, 2021 IBM Corp. and others
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

#include "optimizer/RecognizedCallTransformer.hpp"

#include "compile/ResolvedMethod.hpp"
#include "env/CompilerEnv.hpp"
#include "env/VMJ9.h"
#include "env/jittypes.h"
#include "il/Block.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/StaticSymbol.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "il/ILOpCodes.hpp"
#include "il/ILOps.hpp"
#include "ilgen/IlGenRequest.hpp"
#include "ilgen/IlGeneratorMethodDetails.hpp"
#include "ilgen/IlGeneratorMethodDetails_inlines.hpp"
#include "optimizer/CallInfo.hpp"
#include "optimizer/Structure.hpp"
#include "codegen/CodeGenerator.hpp"
#include "optimizer/TransformUtil.hpp"
#include "env/j9method.h"
#include "optimizer/Optimization_inlines.hpp"

void J9::RecognizedCallTransformer::processIntrinsicFunction(TR::TreeTop* treetop, TR::Node* node, TR::ILOpCodes opcode)
   {
   TR::Node::recreate(node, opcode);
   }

void J9::RecognizedCallTransformer::processConvertingUnaryIntrinsicFunction(TR::TreeTop* treetop, TR::Node* node, TR::ILOpCodes argConvertOpcode, TR::ILOpCodes opcode, TR::ILOpCodes resultConvertOpcode)
   {
   TR::Node* actualArg = TR::Node::create(argConvertOpcode, 1, node->getFirstChild());
   TR::Node* actualResult = TR::Node::create(opcode, 1, actualArg);

   TR::Node::recreate(node, resultConvertOpcode);
   node->getFirstChild()->decReferenceCount();
   node->setAndIncChild(0, actualResult);
   }

void J9::RecognizedCallTransformer::process_java_lang_Class_IsAssignableFrom(TR::TreeTop* treetop, TR::Node* node)
   {
   auto toClass = node->getChild(0);
   auto fromClass = node->getChild(1);
   auto nullchk = comp()->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp()->getMethodSymbol());
   treetop->insertBefore(TR::TreeTop::create(comp(), TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, TR::Node::create(node, TR::PassThrough, 1, toClass), nullchk)));
   treetop->insertBefore(TR::TreeTop::create(comp(), TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, TR::Node::create(node, TR::PassThrough, 1, fromClass), nullchk)));

   TR::Node::recreate(treetop->getNode(), TR::treetop);
   node->setSymbolReference(comp()->getSymRefTab()->findOrCreateRuntimeHelper(TR_checkAssignable));
   node->setAndIncChild(0, TR::Node::createWithSymRef(TR::aloadi, 1, 1, toClass, comp()->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef()));
   node->setAndIncChild(1, TR::Node::createWithSymRef(TR::aloadi, 1, 1, fromClass, comp()->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef()));
   node->swapChildren();

   toClass->recursivelyDecReferenceCount();
   fromClass->recursivelyDecReferenceCount();
   }

void J9::RecognizedCallTransformer::process_java_lang_StringUTF16_toBytes(TR::TreeTop* treetop, TR::Node* node)
   {
   TR_J9VMBase* fej9 = static_cast<TR_J9VMBase*>(comp()->fe());

   TR::Node* valueNode = node->getChild(0);
   TR::Node* offNode = node->getChild(1);
   TR::Node* lenNode = node->getChild(2);

   anchorAllChildren(node, treetop);
   prepareToReplaceNode(node);

   int32_t byteArrayType = fej9->getNewArrayTypeFromClass(fej9->getByteArrayClass());

   TR::Node::recreateWithoutProperties(node, TR::newarray, 2,
      TR::Node::create(TR::ishl, 2,
         lenNode,
         TR::Node::iconst(1)),
      TR::Node::iconst(byteArrayType),

      getSymRefTab()->findOrCreateNewArraySymbolRef(node->getSymbolReference()->getOwningMethodSymbol(comp())));

   TR::Node* newByteArrayNode = node;
   newByteArrayNode->setCanSkipZeroInitialization(true);
   newByteArrayNode->setIsNonNull(true);

   TR::Node* newCallNode = TR::Node::createWithSymRef(node, TR::call, 5,
      getSymRefTab()->methodSymRefFromName(comp()->getMethodSymbol(), "java/lang/String", "decompressedArrayCopy", "([CI[BII)V", TR::MethodSymbol::Static));
   newCallNode->setAndIncChild(0, valueNode);
   newCallNode->setAndIncChild(1, offNode);
   newCallNode->setAndIncChild(2, newByteArrayNode);
   newCallNode->setAndIncChild(3, TR::Node::iconst(0));
   newCallNode->setAndIncChild(4, lenNode);

   treetop->insertAfter(TR::TreeTop::create(comp(), TR::Node::create(node, TR::treetop, 1, newCallNode)));
   }

void J9::RecognizedCallTransformer::process_java_lang_StrictMath_and_Math_sqrt(TR::TreeTop* treetop, TR::Node* node)
   {
   TR::Node* valueNode = node->getLastChild();

   anchorAllChildren(node, treetop);
   prepareToReplaceNode(node);

   TR::Node::recreate(node, TR::dsqrt);
   node->setNumChildren(1);
   node->setAndIncChild(0, valueNode);

   TR::TransformUtil::removeTree(comp(), treetop);
   }
/*
Transform an Unsafe atomic call to diamonds with equivalent semantics

                          yes
isObjectNull ------------------------------------------>
    |                                                  |
    | no                                               |
    |                     yes                          |
isNotLowTagged ---------------------------------------->
    |                                                  |
    |  no                                              |
    |           no                                     |
isFinal ---------------------->                        |
    |                         |                        |
    | yes                     |                        |
    |                         |                        |
call the                calculate address      calculate address
original method         for static field       for instance field
    |                         |                and absolute address
    |                         |                        |
    |                         |________________________|
    |                                     |
    |                         xcall atomic method helper
    |                                     |
    |_____________________________________|
                    |
      program after the original call

Block before the transformation: ===>

start Block_A
  ...
xcall Unsafe.xxx
  ...
end Block_A

Blocks after the transformation: ===>

start Block_A
...
ifacmpeq -> <Block_E>
  object
  aconst null
end Block_A

start Block_B
iflcmpne -> <Block_E>
  land
    lload <offset>
    lconst J9_SUN_STATIC_FIELD_OFFSET_TAG
  lconst J9_SUN_STATIC_FIELD_OFFSET_TAG
end Block_B

start Block_C
iflcmpeq -> <Block_F>
  land
    lload <offset>
    lconst J9_SUN_FINAL_FIELD_OFFSET_TAG
  lconst J9_SUN_FINAL_FIELD_OFFSET_TAG
end Block_C

start Block_D
astore <object>
  aloadi ramStaticsFromClass
   ...
lstore <offset>
  land
    lload <offset>
    lconst ~J9_SUN_FIELD_OFFSET_MASK
end Block_D

start Block_E
xcall atomic method helper
  aladd
    aload <object>
    lload <offset>
  xload value
end Block_E

...

start Block_F
call jitReportFinalFieldModified
go to <Block_E>
end Block_F
*/
void J9::RecognizedCallTransformer::processUnsafeAtomicCall(TR::TreeTop* treetop, TR::SymbolReferenceTable::CommonNonhelperSymbol helper, bool needsNullCheck)
   {
   bool enableTrace = trace();
   bool isNotStaticField = !strncmp(comp()->getCurrentMethod()->classNameChars(), "java/util/concurrent/atomic/", strlen("java/util/concurrent/atomic/"));
   bool fixupCommoning = true;
   TR::Node* unsafeCall = treetop->getNode()->getFirstChild();
   TR::Node* objectNode = unsafeCall->getChild(1);
   TR::Node* offsetNode = unsafeCall->getChild(2);
   TR::Node* address = NULL;

   // Preserve null check on the unsafe object
   if (treetop->getNode()->getOpCode().isNullCheck())
      {
      TR::Node *passthrough = TR::Node::create(unsafeCall, TR::PassThrough, 1);
      passthrough->setAndIncChild(0, unsafeCall->getFirstChild());
      TR::Node * checkNode = TR::Node::createWithSymRef(treetop->getNode(), TR::NULLCHK, 1, passthrough, treetop->getNode()->getSymbolReference());
      treetop->insertBefore(TR::TreeTop::create(comp(), checkNode));
      TR::Node::recreate(treetop->getNode(), TR::treetop);
      if (enableTrace)
         traceMsg(comp(), "Created node %p to preserve NULLCHK on unsafe call %p\n", checkNode, unsafeCall);
      }

   if (isNotStaticField)
      {
      // It is safe to skip diamond, the address can be calculated directly via [object+offset]
      address = comp()->target().is32Bit() ? TR::Node::create(TR::aiadd, 2, objectNode, TR::Node::create(TR::l2i, 1, offsetNode)) :
                                              TR::Node::create(TR::aladd, 2, objectNode, offsetNode);
      if (enableTrace)
         traceMsg(comp(), "Field is not static, use the object and offset directly\n");
      }
   else
      {
      // Otherwise, the address is [object+offset] for non-static field,
      //            or [object's ramStaticsFromClass + (offset & ~mask)] for static field

      // Save all the children to temps before splitting the block
      TR::TransformUtil::createTempsForCall(this, treetop);

      auto cfg = comp()->getMethodSymbol()->getFlowGraph();
      objectNode = unsafeCall->getChild(1);
      offsetNode = unsafeCall->getChild(2);

      // Null Check
      if (needsNullCheck)
         {
         auto NULLCHKNode = TR::Node::createWithSymRef(unsafeCall, TR::NULLCHK, 1,
                                                       TR::Node::create(TR::PassThrough, 1, objectNode->duplicateTree()),
                                                       comp()->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp()->getMethodSymbol()));
         treetop->insertBefore(TR::TreeTop::create(comp(), NULLCHKNode));
         if (enableTrace)
            traceMsg(comp(), "Created NULLCHK tree %p on the first argument of Unsafe call\n", treetop->getPrevTreeTop());
         }

      // Test if object is null
      auto isObjectNullNode = TR::Node::createif(TR::ifacmpeq, objectNode->duplicateTree(), TR::Node::aconst(0), NULL);
      auto isObjectNullTreeTop = TR::TreeTop::create(comp(), isObjectNullNode);
      treetop->insertBefore(isObjectNullTreeTop);
      treetop->getEnclosingBlock()->split(treetop, cfg, fixupCommoning);

      if (enableTrace)
         traceMsg(comp(), "Created isObjectNull test node n%dn, non-null object will fall through to Block_%d\n", isObjectNullNode->getGlobalIndex(), treetop->getEnclosingBlock()->getNumber());

      // Test if low tag is set
      auto isNotLowTaggedNode = TR::Node::createif(TR::iflcmpne,
                                               TR::Node::create(TR::land, 2, offsetNode->duplicateTree(), TR::Node::lconst(J9_SUN_STATIC_FIELD_OFFSET_TAG)),
                                               TR::Node::lconst(J9_SUN_STATIC_FIELD_OFFSET_TAG),
                                               NULL);
      auto isNotLowTaggedTreeTop = TR::TreeTop::create(comp(), isNotLowTaggedNode);
      treetop->insertBefore(isNotLowTaggedTreeTop);
      treetop->getEnclosingBlock()->split(treetop, cfg, fixupCommoning);

      if (enableTrace)
         traceMsg(comp(), "Created isNotLowTagged test node n%dn, static field will fall through to Block_%d\n", isNotLowTaggedNode->getGlobalIndex(), treetop->getEnclosingBlock()->getNumber());

      static char *disableIllegalWriteReport = feGetEnv("TR_DisableIllegalWriteReport");
      // Test if the call is a write to a static final field
      if (!disableIllegalWriteReport
          && !comp()->getOption(TR_DisableGuardedStaticFinalFieldFolding)
          && TR_J9MethodBase::isUnsafePut(unsafeCall->getSymbol()->castToMethodSymbol()->getMandatoryRecognizedMethod()))
         {
         // If the field is static final
         auto isFinalNode = TR::Node::createif(TR::iflcmpeq,
                                                  TR::Node::create(TR::land, 2, offsetNode->duplicateTree(), TR::Node::lconst(J9_SUN_FINAL_FIELD_OFFSET_TAG)),
                                                  TR::Node::lconst(J9_SUN_FINAL_FIELD_OFFSET_TAG),
                                                  NULL /*branchTarget*/);
         auto isFinalTreeTop = TR::TreeTop::create(comp(), isFinalNode);
         auto reportFinalFieldModification = TR::TransformUtil::generateReportFinalFieldModificationCallTree(comp(), objectNode->duplicateTree());
         auto elseBlock = treetop->getEnclosingBlock();
         TR::TransformUtil::createConditionalAlternatePath(comp(), isFinalTreeTop, reportFinalFieldModification, elseBlock, elseBlock, comp()->getMethodSymbol()->getFlowGraph(), true /*markCold*/);
         if (enableTrace)
            {
            traceMsg(comp(), "Created isFinal test node n%dn, non-final-static field will fall through to Block_%d, final field goes to Block_%d\n",
                     isFinalNode->getGlobalIndex(), treetop->getEnclosingBlock()->getNumber(), reportFinalFieldModification->getEnclosingBlock()->getNumber());
            }
         TR::DebugCounter::prependDebugCounter(comp(),
                                               TR::DebugCounter::debugCounterName(comp(),
                                                                                  "illegalWriteReport/atomic/(%s %s)",
                                                                                  comp()->signature(),
                                                                                  comp()->getHotnessName(comp()->getMethodHotness())),
                                                                                  reportFinalFieldModification->getNextTreeTop());

         }

      // Calculate static address
      auto objectAdjustmentNode = TR::Node::createWithSymRef(TR::astore, 1, 1,
                                                             TR::Node::createWithSymRef(TR::aloadi, 1, 1,
                                                                                        TR::Node::createWithSymRef(TR::aloadi, 1, 1,
                                                                                                                   objectNode->duplicateTree(),
                                                                                                                   comp()->getSymRefTab()->findOrCreateClassFromJavaLangClassSymbolRef()),
                                                                                        comp()->getSymRefTab()->findOrCreateRamStaticsFromClassSymbolRef()),
                                                             objectNode->getSymbolReference());
      auto offsetAdjustmentNode = TR::Node::createWithSymRef(TR::lstore, 1, 1,
                                                             TR::Node::create(TR::land, 2,
                                                                              offsetNode->duplicateTree(),
                                                                              TR::Node::lconst(~J9_SUN_FIELD_OFFSET_MASK)),
                                                             offsetNode->getSymbolReference());

      if (enableTrace)
         traceMsg(comp(), "Created node n%dn and n%dn to adjust object and offset for static field\n", objectAdjustmentNode->getGlobalIndex(), offsetAdjustmentNode->getGlobalIndex());

      treetop->insertBefore(TR::TreeTop::create(comp(), objectAdjustmentNode));
      treetop->insertBefore(TR::TreeTop::create(comp(), offsetAdjustmentNode));
      treetop->getEnclosingBlock()->split(treetop, cfg, fixupCommoning);

      if (enableTrace)
         traceMsg(comp(), "Block_%d contains call to atomic method helper, and is the target of isObjectNull and isNotLowTagged tests\n", treetop->getEnclosingBlock()->getNumber());

      // Setup CFG edges
      isObjectNullNode->setBranchDestination(treetop->getEnclosingBlock()->getEntry());
      cfg->addEdge(TR::CFGEdge::createEdge(isObjectNullTreeTop->getEnclosingBlock(), treetop->getEnclosingBlock(), comp()->trMemory()));
      isNotLowTaggedNode->setBranchDestination(treetop->getEnclosingBlock()->getEntry());
      cfg->addEdge(TR::CFGEdge::createEdge(isNotLowTaggedTreeTop->getEnclosingBlock(), treetop->getEnclosingBlock(), comp()->trMemory()));
      address = comp()->target().is32Bit() ? TR::Node::create(TR::aiadd, 2, objectNode->duplicateTree(), TR::Node::create(TR::l2i, 1, offsetNode->duplicateTree())) :
                                              TR::Node::create(TR::aladd, 2, objectNode->duplicateTree(), offsetNode->duplicateTree());
      }

   // Transmute the unsafe call to a call to atomic method helper
   unsafeCall->getChild(0)->recursivelyDecReferenceCount(); // replace unsafe object with the address
   unsafeCall->setAndIncChild(0, address);
   unsafeCall->removeChild(2); // remove offset node
   unsafeCall->removeChild(1); // remove object node
   unsafeCall->setSymbolReference(comp()->getSymRefTab()->findOrCreateCodeGenInlinedHelper(helper));
   }
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
void J9::RecognizedCallTransformer::process_java_lang_invoke_MethodHandle_invokeBasic(TR::TreeTop * treetop, TR::Node* node)
   {
   TR_J9VMBase* fej9 = static_cast<TR_J9VMBase*>(comp()->fe());
   TR::TransformUtil::separateNullCheck(comp(), treetop, true);
   TR::Node * inlCallNode = node->duplicateTree(false);
   TR::list<TR::SymbolReference *>* argsList = new (comp()->trStackMemory()) TR::list<TR::SymbolReference*>(getTypedAllocator<TR::SymbolReference*>(comp()->allocator()));
   for (int i = 0; i < node->getNumChildren(); i++)
      {
      TR::Node * currentChild = node->getChild(i);
      TR::SymbolReference *newSymbolReference = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), currentChild->getDataType());
      argsList->push_back(newSymbolReference);
      TR::Node * storeNode = TR::Node::createStore(node, newSymbolReference, currentChild);
      treetop->insertBefore(TR::TreeTop::create(comp(),storeNode));
      inlCallNode->setAndIncChild(i, TR::Node::createLoad(node, newSymbolReference));
      currentChild->recursivelyDecReferenceCount();
      }

   TR::Node* mhNode = TR::Node::createLoad(node, argsList->front()); // the first arg of invokeBasic call is the receiver MethodHandle object
   // load MethodHandle.form, which is the LambdaForm object
   uint32_t offset = fej9->getInstanceFieldOffsetIncludingHeader("Ljava/lang/invoke/MethodHandle;", "form", "Ljava/lang/invoke/LambdaForm;", comp()->getCurrentMethod());
   TR::SymbolReference * lambdaFormSymRef = comp()->getSymRefTab()->findOrFabricateShadowSymbol(comp()->getMethodSymbol(),
                                                                                                TR::Symbol::Java_lang_invoke_MethodHandle_form,
                                                                                                TR::Address,
                                                                                                offset,
                                                                                                false,
                                                                                                false,
                                                                                                true,
                                                                                                "java/lang/invoke/MethodHandle.form Ljava/lang/invoke/LambdaForm;");
   TR::Node * lambdaFormNode = TR::Node::createWithSymRef(node, comp()->il.opCodeForIndirectLoad(TR::Address), 1 , mhNode, lambdaFormSymRef);
   lambdaFormNode->setIsNonNull(true);
   // load from lambdaForm.vmEntry, which is the MemberName object
   offset = fej9->getInstanceFieldOffsetIncludingHeader("Ljava/lang/invoke/LambdaForm;", "vmentry", "Ljava/lang/invoke/MemberName;", comp()->getCurrentMethod());
   TR::SymbolReference * memberNameSymRef = comp()->getSymRefTab()->findOrFabricateShadowSymbol(comp()->getMethodSymbol(),
                                                                                                TR::Symbol::Java_lang_invoke_LambdaForm_vmentry,
                                                                                                TR::Address,
                                                                                                offset,
                                                                                                false,
                                                                                                false,
                                                                                                true,
                                                                                                "java/lang/invoke/LambdaForm.vmentry Ljava/lang/invoke/MemberName;");
   TR::Node * memberNameNode = TR::Node::createWithSymRef(node, comp()->il.opCodeForIndirectLoad(TR::Address), 1, lambdaFormNode, memberNameSymRef);
   // load from membername.vmTarget, which is the J9Method
   TR::SymbolReference * vmTargetSymRef = comp()->getSymRefTab()->findOrFabricateShadowSymbol(comp()->getMethodSymbol(),
                                                                                                TR::Symbol::Java_lang_invoke_MemberName_vmtarget,
                                                                                                TR::Address,
                                                                                                fej9->getCurrentVMThread()->javaVM->vmtargetOffset,
                                                                                                false,
                                                                                                false,
                                                                                                true,
                                                                                                "java/lang/invoke/MemberName.vmtarget J");
   vmTargetSymRef->getSymbol()->setNotCollected();
   TR::Node * vmTargetNode = TR::Node::createWithSymRef(node, TR::aloadi, 1, memberNameNode, vmTargetSymRef);
   processVMInternalNativeFunction(treetop, node, vmTargetNode, argsList, inlCallNode, /* hasMethodHandleReceiver*/ true);
   }

void J9::RecognizedCallTransformer::process_java_lang_invoke_MethodHandle_linkToStaticSpecial(TR::TreeTop* treetop, TR::Node* node)
   {
   TR_J9VMBase* fej9 = static_cast<TR_J9VMBase*>(comp()->fe());
   TR::TransformUtil::separateNullCheck(comp(), treetop, true);
   TR::Node * inlCallNode = node->duplicateTree(false);
   TR::list<TR::SymbolReference *>* argsList = new (comp()->trStackMemory()) TR::list<TR::SymbolReference*>(getTypedAllocator<TR::SymbolReference*>(comp()->allocator()));
   for (int i = 0; i < node->getNumChildren(); i++)
      {
      TR::Node * currentChild = node->getChild(i);
      TR::SymbolReference *newSymbolReference = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(), currentChild->getDataType());
      argsList->push_back(newSymbolReference);
      TR::Node * storeNode = TR::Node::createStore(node, newSymbolReference, currentChild);
      treetop->insertBefore(TR::TreeTop::create(comp(),storeNode));
      inlCallNode->setAndIncChild(i, TR::Node::createLoad(node, newSymbolReference));
      currentChild->recursivelyDecReferenceCount();
      }
   // load from membername.vmTarget, which is the J9Method
   TR::Node* mnNode = TR::Node::createLoad(node, argsList->back());
   TR::SymbolReference * vmTargetSymRef = comp()->getSymRefTab()->findOrFabricateShadowSymbol(comp()->getMethodSymbol(),
                                                                                                TR::Symbol::Java_lang_invoke_MemberName_vmtarget,
                                                                                                TR::Address,
                                                                                                fej9->getCurrentVMThread()->javaVM->vmtargetOffset,
                                                                                                false,
                                                                                                false,
                                                                                                true,
                                                                                                "java/lang/invoke/MemberName.vmtarget J");
   vmTargetSymRef->getSymbol()->setNotCollected();
   TR::Node * vmTargetNode = TR::Node::createWithSymRef(node, TR::aloadi, 1, mnNode, vmTargetSymRef);
   argsList->pop_back(); // MemberName is not required when dispatching directly to the jitted method address
   processVMInternalNativeFunction(treetop, node, vmTargetNode, argsList, inlCallNode, /* hasMethodHandleReceiver*/ false);
   }

void J9::RecognizedCallTransformer::processVMInternalNativeFunction(TR::TreeTop* treetop, TR::Node* node, TR::Node* vmTargetNode, TR::list<TR::SymbolReference *>* argsList, TR::Node* inlCallNode, bool hasMethodHandleReceiver)
   {
   TR::SymbolReference *extraField = comp()->getSymRefTab()->findOrCreateJ9MethodExtraFieldSymbolRef(offsetof(struct J9Method, extra));
   TR::Node *extra = TR::Node::createWithSymRef(node, comp()->il.opCodeForIndirectLoad(extraField->getSymbol()->getDataType()), 1, vmTargetNode, extraField);
   TR::SymbolReference *extraTempSlotSymRef = comp()->getSymRefTab()->createTemporary(comp()->getMethodSymbol(),extraField->getSymbol()->getDataType());
   treetop->insertBefore(TR::TreeTop::create(comp(), TR::Node::createStore(node, extraTempSlotSymRef, extra)));

   TR::ILOpCodes xcmpne = comp()->target().is64Bit()? TR::iflcmpne : TR::ificmpne;
   TR::ILOpCodes xand   = comp()->target().is64Bit()? TR::land   : TR::iand;
   TR::Node *zero = comp()->target().is64Bit()? TR::Node::lconst(node, 0) : TR::Node::iconst(node, 0);
   TR::Node *mask = comp()->target().is64Bit()? TR::Node::lconst(node, J9_STARTPC_NOT_TRANSLATED) : TR::Node::iconst(node, J9_STARTPC_NOT_TRANSLATED);

   TR::Node* isCompiledNode = TR::Node::createif(xcmpne,
         TR::Node::create(xand, 2, TR::Node::createLoad(node, extraTempSlotSymRef), mask),
         zero,
         NULL);
   isCompiledNode->copyByteCodeInfo(node);
   TR::TreeTop * cmpCheckTreeTop = TR::TreeTop::create(self()->comp(), isCompiledNode);

   TR::Node *jitAddress;
   if (comp()->target().cpu.isI386())
      {
      jitAddress = TR::Node::create(TR::i2l, 1, TR::Node::createLoad(node, extraTempSlotSymRef));
      }
   else
      {
      TR::SymbolReference *linkageInfoSymRef = comp()->getSymRefTab()->findOrCreateStartPCLinkageInfoSymbolRef(-4);
      TR::ILOpCodes x2a = comp()->target().is64Bit()? TR::l2a : TR::i2a;
      TR::Node *linkageInfo    = TR::Node::createWithSymRef(TR::iloadi, 1, 1, TR::Node::create(x2a, 1, TR::Node::createLoad(node, extraTempSlotSymRef)), linkageInfoSymRef);
      TR::Node *jitEntryOffset = TR::Node::create(TR::ishr,   2, linkageInfo, TR::Node::iconst(node, 16));
      if (comp()->target().is64Bit())
         jitAddress = TR::Node::create(TR::ladd, 2, TR::Node::createLoad(node, extraTempSlotSymRef), TR::Node::create(TR::i2l, 1, jitEntryOffset));
      else
         jitAddress = TR::Node::create(TR::iadd, 2, TR::Node::createLoad(node, extraTempSlotSymRef), jitEntryOffset);
      }

   TR::SymbolReference * computedCallSymbol = comp()->getSymRefTab()->findOrCreateComputedStaticCallSymbol();
   uint32_t numComputedCallArgs = hasMethodHandleReceiver ? node->getNumChildren()+1 : node->getNumChildren();
   TR::Node * computedCallNode = TR::Node::createWithSymRef(node, node->getSymbol()->castToMethodSymbol()->getMethod()->indirectCallOpCode(),numComputedCallArgs, computedCallSymbol);
   computedCallNode->setAndIncChild(0,jitAddress); // first arguments of computed calls is the method address to jump to
   int32_t child_i = 1;
   for (auto symRefIt = argsList->begin(); symRefIt != argsList->end(); ++symRefIt)
      {
      TR::SymbolReference * currentArg = *symRefIt;
      computedCallNode->setAndIncChild(child_i, TR::Node::createLoad(currentArg));
      child_i++;
      }
   TR::TreeTop * computedCallTreeTop = TR::TreeTop::create(comp(), TR::Node::create(node, TR::treetop, 1, computedCallNode));
   TR::TreeTop * inlCallTreeTop = TR::TreeTop::create(comp(), TR::Node::create(node, TR::treetop, 1, inlCallNode)); // original treetop gets deleted by createDiamondForCall
   TR::TransformUtil::createDiamondForCall(this, treetop, cmpCheckTreeTop, inlCallTreeTop, computedCallTreeTop, false, false);
   _processedINLCalls->set(inlCallNode->getGlobalIndex());
   }
#endif // J9VM_OPT_OPENJDK_METHODHANDLE

bool J9::RecognizedCallTransformer::isInlineable(TR::TreeTop* treetop)
   {
   auto node = treetop->getNode()->getFirstChild();
   switch(node->getSymbol()->castToMethodSymbol()->getMandatoryRecognizedMethod())
      {
      case TR::sun_misc_Unsafe_getAndAddInt:
         return !comp()->getOption(TR_DisableUnsafe) && !comp()->compileRelocatableCode() && !TR::Compiler->om.canGenerateArraylets() &&
            cg()->supportsNonHelper(TR::SymbolReferenceTable::atomicFetchAndAddSymbol);
      case TR::sun_misc_Unsafe_getAndSetInt:
         return !comp()->getOption(TR_DisableUnsafe) && !comp()->compileRelocatableCode() && !TR::Compiler->om.canGenerateArraylets() &&
            cg()->supportsNonHelper(TR::SymbolReferenceTable::atomicSwapSymbol);
      case TR::sun_misc_Unsafe_getAndAddLong:
         return !comp()->getOption(TR_DisableUnsafe) && !comp()->compileRelocatableCode() && !TR::Compiler->om.canGenerateArraylets() && comp()->target().is64Bit() &&
            cg()->supportsNonHelper(TR::SymbolReferenceTable::atomicFetchAndAddSymbol);
      case TR::sun_misc_Unsafe_getAndSetLong:
         return !comp()->getOption(TR_DisableUnsafe) && !comp()->compileRelocatableCode() && !TR::Compiler->om.canGenerateArraylets() && comp()->target().is64Bit() &&
            cg()->supportsNonHelper(TR::SymbolReferenceTable::atomicSwapSymbol);
      case TR::java_lang_Class_isAssignableFrom:
         return cg()->supportsInliningOfIsAssignableFrom();
      case TR::java_lang_Integer_rotateLeft:
      case TR::java_lang_Integer_rotateRight:
         return comp()->target().cpu.getSupportsHardware32bitRotate();
      case TR::java_lang_Long_rotateLeft:
      case TR::java_lang_Long_rotateRight:
         return comp()->target().cpu.getSupportsHardware64bitRotate();
      case TR::java_lang_Math_abs_I:
      case TR::java_lang_Math_abs_L:
         return cg()->supportsIntAbs();
      case TR::java_lang_Math_abs_F:
      case TR::java_lang_Math_abs_D:
         return cg()->supportsFPAbs();
      case TR::java_lang_Math_max_I:
      case TR::java_lang_Math_min_I:
      case TR::java_lang_Math_max_L:
      case TR::java_lang_Math_min_L:
         return !comp()->getOption(TR_DisableMaxMinOptimization);
      case TR::java_lang_StringUTF16_toBytes:
         return !comp()->compileRelocatableCode();
      case TR::java_lang_StrictMath_sqrt:
      case TR::java_lang_Math_sqrt:
         return comp()->target().cpu.getSupportsHardwareSQRT();
      case TR::java_lang_Short_reverseBytes:
      case TR::java_lang_Integer_reverseBytes:
      case TR::java_lang_Long_reverseBytes:
         return comp()->cg()->supportsByteswap();
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
      case TR::java_lang_invoke_MethodHandle_invokeBasic:
         if (_processedINLCalls->get(node->getGlobalIndex()))
            return false;
         else
            return true;
      case TR::java_lang_invoke_MethodHandle_linkToStatic:
      case TR::java_lang_invoke_MethodHandle_linkToSpecial:
         // linkToStatic calls are also used for unresolved invokedynamic/invokehandle, which we can not
         // bypass as we may push null appendix object that we can not check at compile time
         if (_processedINLCalls->get(node->getGlobalIndex()) || node->getSymbolReference()->getSymbol()->isDummyResolvedMethod())
            return false;
         else
            return true;
#endif
      default:
         return false;
      }
   }

void J9::RecognizedCallTransformer::transform(TR::TreeTop* treetop)
   {
   auto node = treetop->getNode()->getFirstChild();
   switch(node->getSymbol()->castToMethodSymbol()->getMandatoryRecognizedMethod())
      {
      case TR::sun_misc_Unsafe_getAndAddInt:
      case TR::sun_misc_Unsafe_getAndAddLong:
         processUnsafeAtomicCall(treetop, TR::SymbolReferenceTable::atomicFetchAndAddSymbol);
         break;
      case TR::sun_misc_Unsafe_getAndSetInt:
      case TR::sun_misc_Unsafe_getAndSetLong:
         processUnsafeAtomicCall(treetop, TR::SymbolReferenceTable::atomicSwapSymbol);
         break;
      case TR::java_lang_Class_isAssignableFrom:
         process_java_lang_Class_IsAssignableFrom(treetop, node);
         break;
      case TR::java_lang_Integer_rotateLeft:
         processIntrinsicFunction(treetop, node, TR::irol);
         break;
      case TR::java_lang_Integer_rotateRight:
         {
         // rotateRight(x, distance) = rotateLeft(x, -distance)
         TR::Node *distance = TR::Node::create(node, TR::ineg, 1);
         distance->setChild(0, node->getSecondChild());
         node->setAndIncChild(1, distance);

         processIntrinsicFunction(treetop, node, TR::irol);

         break;
         }
      case TR::java_lang_Long_rotateLeft:
         processIntrinsicFunction(treetop, node, TR::lrol);
         break;
      case TR::java_lang_Long_rotateRight:
         {
         // rotateRight(x, distance) = rotateLeft(x, -distance)
         TR::Node *distance = TR::Node::create(node, TR::ineg, 1);
         distance->setChild(0, node->getSecondChild());
         node->setAndIncChild(1, distance);

         processIntrinsicFunction(treetop, node, TR::lrol);

         break;
         }
      case TR::java_lang_Math_abs_I:
         processIntrinsicFunction(treetop, node, TR::iabs);
         break;
      case TR::java_lang_Math_abs_L:
         processIntrinsicFunction(treetop, node, TR::labs);
         break;
      case TR::java_lang_Math_abs_D:
         processIntrinsicFunction(treetop, node, TR::dabs);
         break;
      case TR::java_lang_Math_abs_F:
         processIntrinsicFunction(treetop, node, TR::fabs);
         break;
      case TR::java_lang_Math_max_I:
         processIntrinsicFunction(treetop, node, TR::imax);
         break;
      case TR::java_lang_Math_min_I:
         processIntrinsicFunction(treetop, node, TR::imin);
         break;
      case TR::java_lang_Math_max_L:
         processIntrinsicFunction(treetop, node, TR::lmax);
         break;
      case TR::java_lang_Math_min_L:
         processIntrinsicFunction(treetop, node, TR::lmin);
         break;
      case TR::java_lang_StringUTF16_toBytes:
         process_java_lang_StringUTF16_toBytes(treetop, node);
         break;
      case TR::java_lang_StrictMath_sqrt:
      case TR::java_lang_Math_sqrt:
         process_java_lang_StrictMath_and_Math_sqrt(treetop, node);
         break;
      case TR::java_lang_Short_reverseBytes:
         processConvertingUnaryIntrinsicFunction(treetop, node, TR::i2s, TR::sbyteswap, TR::s2i);
         break;
      case TR::java_lang_Integer_reverseBytes:
         processIntrinsicFunction(treetop, node, TR::ibyteswap);
         break;
      case TR::java_lang_Long_reverseBytes:
         processIntrinsicFunction(treetop, node, TR::lbyteswap);
         break;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
      case TR::java_lang_invoke_MethodHandle_invokeBasic:
         process_java_lang_invoke_MethodHandle_invokeBasic(treetop, node);
         break;
      case TR::java_lang_invoke_MethodHandle_linkToStatic:
      case TR::java_lang_invoke_MethodHandle_linkToSpecial:
         process_java_lang_invoke_MethodHandle_linkToStaticSpecial(treetop, node);
         break;
#endif
      default:
         break;
      }
   }

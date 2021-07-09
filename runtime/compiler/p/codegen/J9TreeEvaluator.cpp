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

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include "j9.h"
#include "j9cfg.h"
#include "j9consts.h"
#include "omrmodroncore.h"
#include "thrdsup.h"
#include "thrtypes.h"
#include "codegen/AheadOfTimeCompile.hpp"
#include "codegen/BackingStore.hpp"
#include "codegen/CodeGenerator.hpp"
#include "codegen/CodeGeneratorUtils.hpp"
#include "codegen/CodeGenerator_inlines.hpp"
#include "codegen/Machine.hpp"
#include "codegen/Linkage.hpp"
#include "codegen/Linkage_inlines.hpp"
#include "codegen/LiveRegister.hpp"
#include "codegen/PPCJNILinkage.hpp"
#include "codegen/PPCPrivateLinkage.hpp"
#include "codegen/Relocation.hpp"
#include "codegen/Snippet.hpp"
#include "codegen/TreeEvaluator.hpp"
#include "compile/ResolvedMethod.hpp"
#include "control/Recompilation.hpp"
#include "control/RecompilationInfo.hpp"
#include "codegen/J9PPCWatchedInstanceFieldSnippet.hpp"
#include "codegen/J9PPCWatchedStaticFieldSnippet.hpp"
#include "env/CHTable.hpp"
#include "env/CompilerEnv.hpp"
#include "env/IO.hpp"
#include "env/jittypes.h"
#include "env/VMJ9.h"
#include "il/DataTypes.hpp"
#include "il/LabelSymbol.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "infra/Annotations.hpp"
#include "infra/Bit.hpp"
#include "p/codegen/ForceRecompilationSnippet.hpp"
#include "p/codegen/GenerateInstructions.hpp"
#include "p/codegen/InterfaceCastSnippet.hpp"
#include "p/codegen/J9PPCSnippet.hpp"
#include "p/codegen/LoadStoreHandler.hpp"
//#include "p/codegen/PPCAheadOfTimeCompile.hpp"
#include "p/codegen/PPCEvaluator.hpp"
#include "p/codegen/PPCInstruction.hpp"
#include "p/codegen/PPCTableOfConstants.hpp"
#include "runtime/CodeCache.hpp"
#include "runtime/Runtime.hpp"
#include "env/VMJ9.h"

#define MAX_PPC_ARRAYCOPY_INLINE 256
#define AIX_NULL_ZERO_AREA_SIZE 256
#define NUM_PICS 3

#define LOCK_INC_DEC_VALUE                                OBJECT_HEADER_LOCK_FIRST_RECURSION_BIT
#define LOCK_RESERVATION_BIT                              OBJECT_HEADER_LOCK_RESERVED

#define LOCK_RES_PRIMITIVE_ENTER_MASK                     (OBJECT_HEADER_LOCK_RECURSION_MASK | OBJECT_HEADER_LOCK_FLC)
#define LOCK_RES_PRIMITIVE_EXIT_MASK                      (OBJECT_HEADER_LOCK_BITS_MASK & ~OBJECT_HEADER_LOCK_RECURSION_MASK)
#define LOCK_RES_NON_PRIMITIVE_ENTER_MASK                 (OBJECT_HEADER_LOCK_RECURSION_MASK & ~OBJECT_HEADER_LOCK_LAST_RECURSION_BIT)
#define LOCK_RES_NON_PRIMITIVE_EXIT_MASK                  (OBJECT_HEADER_LOCK_RECURSION_MASK & ~OBJECT_HEADER_LOCK_FIRST_RECURSION_BIT)

#define LOCK_RES_OWNING_COMPLEMENT                        (OBJECT_HEADER_LOCK_RECURSION_MASK | OBJECT_HEADER_LOCK_FLC)

#define LOCK_NON_PRIMITIVE_ENTER_IGNORE_MASK              ((OBJECT_HEADER_LOCK_RECURSION_MASK & ~OBJECT_HEADER_LOCK_LAST_RECURSION_BIT) | OBJECT_HEADER_LOCK_RESERVED | OBJECT_HEADER_LOCK_FLC)
#define LOCK_NON_PRIMITIVE_EXIT_IGNORE_MASK               (OBJECT_HEADER_LOCK_RECURSION_MASK | OBJECT_HEADER_LOCK_RESERVED | OBJECT_HEADER_LOCK_FLC)

extern TR::Register *addConstantToLong(TR::Node * node, TR::Register *srcReg, int64_t value, TR::Register *trgReg, TR::CodeGenerator *cg);
extern TR::Register *addConstantToInteger(TR::Node * node, TR::Register *trgReg, TR::Register *srcReg, int32_t value, TR::CodeGenerator *cg);

static TR::InstOpCode::Mnemonic getLoadOrStoreFromDataType(TR::CodeGenerator *cg, TR::DataType dt, int32_t elementSize, bool isUnsigned, bool returnLoad);
void loadFieldWatchSnippet(TR::CodeGenerator *cg, TR::Node *node, TR::Snippet *dataSnippet, TR::Register *snippetReg, TR::Register *scratchReg, bool isInstance);
static const char *ppcSupportsReadMonitors = feGetEnv("TR_ppcSupportReadMonitors");

extern uint32_t getPPCCacheLineSize();

static TR::RegisterDependencyConditions *createConditionsAndPopulateVSXDeps(TR::CodeGenerator *cg, int32_t nonVSXDepsCount)
   {
   TR::RegisterDependencyConditions *conditions;
   TR_LiveRegisters *lrVector = cg->getLiveRegisters(TR_VSX_VECTOR);
   bool liveVSXVectorReg = (!lrVector || (lrVector->getNumberOfLiveRegisters() > 0));
   const TR::PPCLinkageProperties& properties = cg->getLinkage()->getProperties();
   if (liveVSXVectorReg)
      {
      int32_t depsCount = 64 + nonVSXDepsCount;
      conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(depsCount, depsCount, cg->trMemory());
      for (int32_t i = TR::RealRegister::FirstFPR; i <= TR::RealRegister::LastFPR; i++)
         {
         if (!properties.getPreserved((TR::RealRegister::RegNum) i))
            {
            TR::addDependency(conditions, NULL, (TR::RealRegister::RegNum) i, TR_FPR, cg);
            }
         }
      for (int32_t i = TR::RealRegister::FirstVRF; i <= TR::RealRegister::LastVRF; i++)
         {
         if (!properties.getPreserved((TR::RealRegister::RegNum) i))
            {
            TR::addDependency(conditions, NULL, (TR::RealRegister::RegNum) i, TR_VRF, cg);
            }
         }
      }
   else
      {
      TR_LiveRegisters *lrScalar = cg->getLiveRegisters(TR_VSX_SCALAR);
      if (!lrScalar || (lrScalar->getNumberOfLiveRegisters() > 0))
         {
         int32_t depsCount = 32 + nonVSXDepsCount;
         conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(depsCount, depsCount, cg->trMemory());
         for (int32_t i = TR::RealRegister::FirstVRF; i <= TR::RealRegister::LastVRF; i++)
            {
            if (!properties.getPreserved((TR::RealRegister::RegNum) i))
               {
               TR::addDependency(conditions, NULL, (TR::RealRegister::RegNum) i, TR_VRF, cg);
               }
            }
         }
      else
         {
         conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(nonVSXDepsCount, nonVSXDepsCount, cg->trMemory());
         }
      }
   return conditions;
   }

static TR::Instruction *genNullTest(TR::Node *node, TR::Register *objectReg, TR::Register *crReg, TR::CodeGenerator *cg, TR::Instruction *cursor = NULL)
   {
   cursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, crReg, objectReg, NULLVALUE, cursor);
   return cursor;
   }

static inline TR::Instruction *genNullTest(TR::Node *node, TR::Register *objectReg, TR::Register *crReg, TR::Register *scratchReg, TR::Instruction *cursor, TR::CodeGenerator *cg)
   {
   TR::Instruction *iRet;
   iRet = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, crReg, objectReg, NULLVALUE, cursor);
   return (iRet);
   }

static void genInlineTest(TR::Node * node, TR_OpaqueClassBlock* castClassAddr, TR_OpaqueClassBlock** guessClassArray, uint8_t num_PICS, TR::Register * objClassReg,
      TR::Register * resultReg, TR::Register * cndReg, TR::Register * scratch1Reg, TR::Register * scratch2Reg, bool checkCast, bool needsResult, TR::LabelSymbol * falseLabel,
      TR::LabelSymbol * trueLabel, TR::LabelSymbol * doneLabel, TR::LabelSymbol * callLabel, bool testCastClassIsSuper, TR::Instruction *iCursor, TR::CodeGenerator * cg)
   {
   TR::Compilation * comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   dumpOptDetails(comp, "inline test with %d guess classes\n", num_PICS);
   TR_ASSERT(num_PICS != 0, "genInlineTest is called with at least 1 guess class\n");

   TR::LabelSymbol *snippetLabel = generateLabelSymbol(cg);

   // TODO: we pass callLabel twice in TR::PPCInterfaceCastSnippet. However, this is because the first one is used as
   // the restart point on TR::PPCInterfaceCastSnippet, and the second as the reference to the call to the helper function
   // this lets us have a more generic Snippet. This can be simplified later on, should it be needed.
   //
   TR::PPCInterfaceCastSnippet * guessCacheSnippet = new (cg->trHeapMemory()) TR::PPCInterfaceCastSnippet(cg, node, callLabel, snippetLabel, trueLabel, falseLabel, doneLabel,
         callLabel, testCastClassIsSuper, checkCast, TR::Compiler->om.offsetOfObjectVftField(), offsetof(J9Class, castClassCache), needsResult);
   cg->addSnippet(guessCacheSnippet);

   uint8_t i;
   bool result_bool;
   int32_t result_value;
   TR::LabelSymbol *result_label;
   for (i = 0; i < num_PICS - 1; i++)
      {
      // Load the cached value in scratch1Reg
      // HCR in genInlineTest for checkcast and instanceof
      if (cg->wantToPatchClassPointer(guessClassArray[i], node))
         {
         iCursor = loadAddressConstantInSnippet(cg, node, (intptr_t) (guessClassArray[i]), scratch1Reg, scratch2Reg,TR::InstOpCode::Op_load, true, iCursor);
         }
      else if (comp->compileRelocatableCode() && comp->getOption(TR_UseSymbolValidationManager))
         {
         TR::StaticSymbol *sym = TR::StaticSymbol::create(comp->trHeapMemory(), TR::Address);
         sym->setStaticAddress(guessClassArray[i]);
         sym->setClassObject();

         iCursor = loadAddressConstant(cg, true, node, (intptr_t) new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), sym), scratch1Reg, iCursor, false, TR_ClassAddress);
         }
      else
         {
         iCursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t) (guessClassArray[i]), scratch1Reg, iCursor);
         }
      result_bool = fej9->instanceOfOrCheckCast((J9Class*) guessClassArray[i], (J9Class*) castClassAddr);
      int32_t result_value = result_bool ? 1 : 0;
      result_label = (falseLabel != trueLabel) ? (result_bool ? trueLabel : falseLabel) : doneLabel;
      iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, objClassReg, scratch1Reg, iCursor);
      if (needsResult)
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, result_value, iCursor);
      iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, result_label, cndReg, iCursor);
      }

   // Load the cached value in scratch1Reg
   if (cg->wantToPatchClassPointer(guessClassArray[num_PICS - 1], node))
      {
      iCursor = loadAddressConstantInSnippet(cg, node, (intptr_t) (guessClassArray[num_PICS - 1]), scratch1Reg, scratch2Reg,TR::InstOpCode::Op_load, true, iCursor);
      }
   else if (comp->compileRelocatableCode() && comp->getOption(TR_UseSymbolValidationManager))
      {
      TR::StaticSymbol *sym = TR::StaticSymbol::create(comp->trHeapMemory(), TR::Address);
      sym->setStaticAddress(guessClassArray[num_PICS - 1]);
      sym->setClassObject();

      iCursor = loadAddressConstant(cg, true, node, (intptr_t) new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), sym), scratch1Reg, iCursor, false, TR_ClassAddress);
      }
   else
      {
      iCursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t) (guessClassArray[num_PICS - 1]), scratch1Reg, iCursor);
      }
   iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, objClassReg, scratch1Reg, iCursor);
   iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg, iCursor);
   result_bool = fej9->instanceOfOrCheckCast((J9Class*) guessClassArray[num_PICS - 1], (J9Class*) castClassAddr);
   result_value = result_bool ? 1 : 0;
   result_label = (falseLabel != trueLabel) ? (result_bool ? trueLabel : falseLabel) : doneLabel;
   if (needsResult)
      {
      iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, result_value, iCursor);
      }
   iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, result_label, iCursor);
   return;
   }

static TR::Register *nonFixedDependency(TR::RegisterDependencyConditions *conditions, TR::Register *nonFixedReg, int32_t *depIndex, TR_RegisterKinds kind, bool excludeGR0,
      TR::CodeGenerator *cg)
   {
   int32_t index = *depIndex;

   if (nonFixedReg == NULL)
      nonFixedReg = cg->allocateRegister(kind);

   TR::addDependency(conditions, nonFixedReg, TR::RealRegister::NoReg, kind, cg);

   if (excludeGR0)
      {
      conditions->getPreConditions()->getRegisterDependency(index)->setExcludeGPR0();
      conditions->getPostConditions()->getRegisterDependency(index)->setExcludeGPR0();
      }

   *depIndex += 1;
   return nonFixedReg;
   }

static void reviveResultRegister(TR::Register *realResult, TR::Register *deadRes, TR::CodeGenerator *cg)
   {
   TR_RegisterKinds kind = realResult->getKind();
   TR_LiveRegisters *liveRegs = cg->getLiveRegisters(kind);

   if (deadRes->isLive())
      deadRes->getLiveRegisterInfo()->decNodeCount();
   cg->stopUsingRegister(deadRes);

   if (liveRegs != NULL)
      {
      liveRegs->addRegister(realResult);
      }
   }

static int32_t numberOfRegisterCandidate(TR::CodeGenerator *cg, TR::Node *depNode, TR_RegisterKinds kind)
   {
   int32_t idx, result = 0;

   for (idx = 0; idx < depNode->getNumChildren(); idx++)
      {
      TR::Node *child = depNode->getChild(idx);
      TR::Register *reg;

      if (child->getOpCodeValue() == TR::PassThrough)
         child = child->getFirstChild();
      reg = child->getRegister();
      if (reg != NULL && reg->getKind() == kind)
         {
         result += 1;
         if (kind == TR_GPR && cg->comp()->target().is32Bit() && child->getType().isInt64())
            result += 1;
         }
      }
   return (result);
   }

// ----------------------------------------------------------------------------



/*
 * J9 PPC specific tree evaluator table overrides
 */
extern void TEMPORARY_initJ9PPCTreeEvaluatorTable(TR::CodeGenerator *cg)
   {
   TR_TreeEvaluatorFunctionPointer *tet = cg->getTreeEvaluatorTable();

   tet[TR::awrtbar] = TR::TreeEvaluator::awrtbarEvaluator;
   tet[TR::awrtbari] = TR::TreeEvaluator::awrtbariEvaluator;
   tet[TR::monent] = TR::TreeEvaluator::monentEvaluator;
   tet[TR::monexit] = TR::TreeEvaluator::monexitEvaluator;
   tet[TR::monexitfence] = TR::TreeEvaluator::monexitfenceEvaluator;
   tet[TR::asynccheck] = TR::TreeEvaluator::asynccheckEvaluator;
   tet[TR::instanceof] = TR::TreeEvaluator::instanceofEvaluator;
   tet[TR::checkcast] = TR::TreeEvaluator::checkcastEvaluator;
   tet[TR::checkcastAndNULLCHK] = TR::TreeEvaluator::checkcastAndNULLCHKEvaluator;
   tet[TR::New] = TR::TreeEvaluator::newObjectEvaluator;
   tet[TR::variableNew] = TR::TreeEvaluator::newObjectEvaluator;
   tet[TR::newarray] = TR::TreeEvaluator::newArrayEvaluator;
   tet[TR::anewarray] = TR::TreeEvaluator::anewArrayEvaluator;
   tet[TR::variableNewArray] = TR::TreeEvaluator::anewArrayEvaluator;
   tet[TR::multianewarray] = TR::TreeEvaluator::multianewArrayEvaluator;
   tet[TR::arraylength] = TR::TreeEvaluator::arraylengthEvaluator;
   tet[TR::ResolveCHK] = TR::TreeEvaluator::resolveCHKEvaluator;
   tet[TR::DIVCHK] = TR::TreeEvaluator::DIVCHKEvaluator;
   tet[TR::BNDCHK] = TR::TreeEvaluator::BNDCHKEvaluator;
   tet[TR::ArrayCopyBNDCHK] = TR::TreeEvaluator::ArrayCopyBNDCHKEvaluator;
   tet[TR::BNDCHKwithSpineCHK] = TR::TreeEvaluator::BNDCHKwithSpineCHKEvaluator;
   tet[TR::SpineCHK] = TR::TreeEvaluator::BNDCHKwithSpineCHKEvaluator;
   tet[TR::ArrayStoreCHK] = TR::TreeEvaluator::ArrayStoreCHKEvaluator;
   tet[TR::ArrayCHK] = TR::TreeEvaluator::ArrayCHKEvaluator;
   tet[TR::MethodEnterHook] = TR::TreeEvaluator::conditionalHelperEvaluator;
   tet[TR::MethodExitHook] = TR::TreeEvaluator::conditionalHelperEvaluator;
   tet[TR::allocationFence] = TR::TreeEvaluator::flushEvaluator;
   tet[TR::loadFence] = TR::TreeEvaluator::flushEvaluator;
   tet[TR::storeFence] = TR::TreeEvaluator::flushEvaluator;
   tet[TR::fullFence] = TR::TreeEvaluator::flushEvaluator;

   tet[TR::icall]  = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::lcall]  = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::fcall]  = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::dcall]  = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::acall]  = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::call]   = TR::TreeEvaluator::directCallEvaluator;
   tet[TR::vcall]  = TR::TreeEvaluator::directCallEvaluator;

   tet[TR::tstart] = TR::TreeEvaluator::tstartEvaluator;
   tet[TR::tfinish] = TR::TreeEvaluator::tfinishEvaluator;
   tet[TR::tabort] = TR::TreeEvaluator::tabortEvaluator;

   tet[TR::NULLCHK] = TR::TreeEvaluator::NULLCHKEvaluator;
   tet[TR::ResolveAndNULLCHK] = TR::TreeEvaluator::resolveAndNULLCHKEvaluator;
   }


static void
VMoutlinedHelperWrtbarEvaluator(
      TR::Node *node,
      TR::Register *srcObjectReg,
      TR::Register *dstObjectReg,
      bool srcIsNonNull,
      TR::CodeGenerator *cg)
   {
   TR::Compilation * comp = cg->comp();
   const auto gcMode = TR::Compiler->om.writeBarrierType();

   if (gcMode == gc_modron_wrtbar_none)
      return;

   const bool doWrtbar = gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC();

   const bool doCardMark = !node->isNonHeapObjectWrtBar() && (gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental);

   TR::CodeCache *codeCache = cg->getCodeCache();
   TR::LabelSymbol *doneWrtbarLabel = generateLabelSymbol(cg);
   TR::Register *srcNullCondReg;

   TR_PPCScratchRegisterDependencyConditions deps;

   // Clobbered by the helper
   deps.addDependency(cg, NULL, TR::RealRegister::cr0);
   deps.addDependency(cg, NULL, TR::RealRegister::cr1);

   if (!srcIsNonNull)
      {
      srcNullCondReg = cg->allocateRegister(TR_CCR);
      generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, srcNullCondReg, srcObjectReg, NULLVALUE);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneWrtbarLabel, srcNullCondReg);
      }

   if (doWrtbar)
      {
      deps.addDependency(cg, dstObjectReg, TR::RealRegister::gr3);
      deps.addDependency(cg, srcObjectReg, TR::RealRegister::gr4);
      // Clobbered by the helper
      deps.addDependency(cg, NULL, TR::RealRegister::gr5);
      deps.addDependency(cg, NULL, TR::RealRegister::gr6);
      deps.addDependency(cg, NULL, TR::RealRegister::gr11);

      TR_CCPreLoadedCode helper = doCardMark ? TR_writeBarrierAndCardMark : TR_writeBarrier;
      uintptr_t helperAddr = (uintptr_t) codeCache->getCCPreLoadedCodeAddress(helper, cg);
      TR::SymbolReference *symRef = comp->getSymRefTab()->findOrCreatePerCodeCacheHelperSymbolRef(helper, helperAddr);

      TR::Instruction *gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, helperAddr, new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 0, cg->trMemory()),
            symRef);
      gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
      }
   else
      {
      TR_ASSERT(doCardMark, "Expecting at least one write barrier to be performed");

      deps.addDependency(cg, dstObjectReg, TR::RealRegister::gr3);
      // Clobbered by the helper
      deps.addDependency(cg, NULL, TR::RealRegister::gr4);
      deps.addDependency(cg, NULL, TR::RealRegister::gr5);
      deps.addDependency(cg, NULL, TR::RealRegister::gr11);

      uintptr_t helperAddr = (uintptr_t) codeCache->getCCPreLoadedCodeAddress(TR_cardMark, cg);
      TR::SymbolReference *symRef = comp->getSymRefTab()->findOrCreatePerCodeCacheHelperSymbolRef(TR_cardMark, helperAddr);
      generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, helperAddr, new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 0, cg->trMemory()), symRef);
      }

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneWrtbarLabel, TR_PPCScratchRegisterDependencyConditions::createDependencyConditions(cg, NULL, &deps));

   if (!srcIsNonNull)
      cg->stopUsingRegister(srcNullCondReg);

   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();
   }

TR::Register *outlinedHelperWrtbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Register *srcObjectReg = cg->gprClobberEvaluate(node->getFirstChild());
   TR::Register *dstObjectReg = cg->gprClobberEvaluate(node->getSecondChild());
   TR::Compilation* comp = cg->comp();

   TR::Symbol *storeSym = node->getSymbolReference()->getSymbol();
   const bool isOrderedShadowStore = storeSym->isShadow() && storeSym->isOrdered();
   const bool needSync = comp->target().isSMP() && (storeSym->isSyncVolatile() || isOrderedShadowStore);
   const bool lazyVolatile = comp->target().isSMP() && isOrderedShadowStore;

   // Under real-time, store happens after the wrtbar
   // For other GC modes, store happens before the wrtbar
   if (comp->getOptions()->realTimeGC())
      {
      }
   else
      {
      TR::LoadStoreHandler::generateStoreNodeSequence(cg, srcObjectReg, node, TR::InstOpCode::Op_st, TR::Compiler->om.sizeofReferenceAddress());

      if (!node->getFirstChild()->isNull())
         VMoutlinedHelperWrtbarEvaluator(node, srcObjectReg, dstObjectReg, node->getFirstChild()->isNonNull(), cg);
      }

   cg->decReferenceCount(node->getFirstChild());
   cg->decReferenceCount(node->getSecondChild());

   if (srcObjectReg != node->getFirstChild()->getRegister())
      cg->stopUsingRegister(srcObjectReg);
   if (dstObjectReg != node->getSecondChild()->getRegister())
      cg->stopUsingRegister(dstObjectReg);

   return NULL;
   }

static int32_t getOffsetOfJ9ObjectFlags()
   {
#if defined(J9VM_INTERP_FLAGS_IN_CLASS_SLOT)
#if defined(TR_TARGET_64BIT)
   if (!TR::Compiler->om.compressObjectReferences())
#if defined(__LITTLE_ENDIAN__)
      return TMP_OFFSETOF_J9OBJECT_CLAZZ;
#else
      return TMP_OFFSETOF_J9OBJECT_CLAZZ + 4;
#endif
#endif
   return TMP_OFFSETOF_J9OBJECT_CLAZZ;
#else
#if defined(TMP_OFFSETOF_J9OBJECT_FLAGS)
   return TMP_OFFSETOF_J9OBJECT_FLAGS;
#else
   return 0;
#endif
#endif
   }

static void VMnonNullSrcWrtBarCardCheckEvaluator(TR::Node *node, TR::Register *srcReg, TR::Register *dstReg, TR::Register *condReg, TR::Register *temp1Reg, TR::Register *temp2Reg,
      TR::Register *temp3Reg, TR::Register *temp4Reg, TR::LabelSymbol *doneLabel, TR::RegisterDependencyConditions *deps,
      bool isCompressedRef, TR::CodeGenerator *cg, bool flagsInTemp1 = false)
   {
   // non-heap objects cannot be marked
   // Make sure we really should be here
   TR::Compilation *comp = cg->comp();
   TR::Options *options = comp->getOptions();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   auto gcMode = TR::Compiler->om.writeBarrierType();
   TR::LabelSymbol *callLabel = generateLabelSymbol(cg);
   TR::Node *wrtbarNode = NULL;
   TR::SymbolReference *wbRef;
   bool definitelyHeapObject = false, definitelyNonHeapObject = false;
   bool doCrdMrk = (gcMode == gc_modron_wrtbar_cardmark_and_oldcheck);
   bool doWrtBar = (gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC());
   bool temp3RegIsNull = (temp3Reg == NULL);

   TR_ASSERT(doWrtBar == true, "VMnonNullSrcWrtBarCardCheckEvaluator: Invalid call to VMnonNullSrcWrtBarCardCheckEvaluator\n");

   if (temp3RegIsNull)
      {
      temp3Reg = cg->allocateRegister();
      TR::addDependency(deps, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      }

   if (node->getOpCodeValue() == TR::awrtbari || node->getOpCodeValue() == TR::awrtbar)
      wrtbarNode = node;
   else if (node->getOpCodeValue() == TR::ArrayStoreCHK)
      wrtbarNode = node->getFirstChild();

   if (wrtbarNode != NULL)
      {
      definitelyHeapObject = wrtbarNode->isHeapObjectWrtBar();
      definitelyNonHeapObject = wrtbarNode->isNonHeapObjectWrtBar();
      }

   if (gcMode == gc_modron_wrtbar_cardmark_and_oldcheck)
      wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierStoreGenerationalAndConcurrentMarkSymbolRef(comp->getMethodSymbol());
   else if (gcMode == gc_modron_wrtbar_oldcheck)
      wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierStoreGenerationalSymbolRef(comp->getMethodSymbol());
   else if (comp->getOptions()->realTimeGC())
      {
      if (wrtbarNode->getOpCodeValue() == TR::awrtbar || wrtbarNode->isUnsafeStaticWrtBar())
         wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierClassStoreRealTimeGCSymbolRef(comp->getMethodSymbol());
      else
         wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierStoreRealTimeGCSymbolRef(comp->getMethodSymbol());
      }
   else
      wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierStoreSymbolRef(comp->getMethodSymbol());

   if (gcMode != gc_modron_wrtbar_always && !comp->getOptions()->realTimeGC())
      {
      bool inlineCrdmrk = (doCrdMrk && !definitelyNonHeapObject);
      // object header flags now occupy 4bytes (instead of 8) on 64-bit. Keep it in temp1Reg for following checks.

      TR::Register *metaReg = cg->getMethodMetaDataRegister();

      // dstReg - heapBaseForBarrierRange0
      TR::Register *tempRegister = (temp4Reg != NULL) ? temp4Reg : temp3Reg;
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, tempRegister,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapBaseForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
      generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp3Reg, tempRegister, dstReg);

      // if (temp3Reg >= heapSizeForBarrierRage0), object not in the tenured area
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapSizeForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
      generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp3Reg, temp2Reg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, doneLabel, condReg);

      if (!flagsInTemp1)
         generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, temp1Reg, TR::MemoryReference::createWithDisplacement(cg, dstReg, getOffsetOfJ9ObjectFlags(), 4));

      if (inlineCrdmrk)
         {
         TR::LabelSymbol *noChkLabel = NULL;

         // Balanced policy must always dirty the card table.
         //
         if (gcMode != gc_modron_wrtbar_cardmark_incremental)
            {
            noChkLabel = generateLabelSymbol(cg);

            generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, privateFlags), TR::Compiler->om.sizeofReferenceAddress()));

            // The value for J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE is a generated value when VM code is created
            // At the moment we are safe here, but it is better to be careful and avoid any unexpected behaviour
            // Make sure this falls within the scope of andis
            //
            TR_ASSERT(J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE >= 0x00010000 && J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE <= 0x80000000,
                  "Concurrent mark active Value assumption broken.");
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andis_r, node, temp2Reg, temp2Reg, condReg, J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE >> 16);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, noChkLabel, condReg);
            }

#if defined(TR_TARGET_64BIT)
         uintptr_t card_size_shift = trailingZeroes((uint64_t)options->getGcCardSize());
#else
         uintptr_t card_size_shift = trailingZeroes((uint32_t) options->getGcCardSize());
#endif

         // dirty(activeCardTableBase + temp3Reg >> card_size_shift)
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, activeCardTableBase), TR::Compiler->om.sizeofReferenceAddress()));
#if defined(TR_TARGET_64BIT)
         generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicl, node, temp3Reg, temp3Reg, 64-card_size_shift, (CONSTANT64(1)<<(64-card_size_shift)) - CONSTANT64(1));
#else
         generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, temp3Reg, temp3Reg, 32 - card_size_shift, ((uint32_t) 0xFFFFFFFF) >> card_size_shift);
#endif
         generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, temp4Reg, CARD_DIRTY);
         generateMemSrc1Instruction(cg, TR::InstOpCode::stb, node, TR::MemoryReference::createWithIndexReg(cg, temp2Reg, temp3Reg, 1), temp4Reg);

         if (noChkLabel)
            generateLabelInstruction(cg, TR::InstOpCode::label, node, noChkLabel);

         if (gcMode == gc_modron_wrtbar_cardmark_and_oldcheck)
            {
            //check for src in new space
            generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapBaseForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
            generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp2Reg, temp2Reg, srcReg);

            generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp3Reg,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapSizeForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
            generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp2Reg, temp3Reg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneLabel, condReg);
            }
         }
      else
         {
         if (gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_oldcheck)
            {
            //check for src in new space
            if (temp4Reg != NULL)
               {
               generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp3Reg, temp4Reg, srcReg);
               }
            else
               {
               generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp3Reg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapBaseForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
               generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp3Reg, temp3Reg, srcReg);
               }
            generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp3Reg, temp2Reg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneLabel, condReg);
            }
         }

      TR::Instruction * i = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, temp2Reg, temp1Reg, condReg, J9_OBJECT_HEADER_REMEMBERED_MASK_FOR_TEST);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, doneLabel, condReg);
      }
   else if (comp->getOptions()->realTimeGC())
      {
      if (!comp->getOption(TR_DisableInlineWriteBarriersRT))
         {
         // check if barrier is enabled: if disabled then branch around the rest

         TR::MemoryReference *fragmentParentMR = TR::MemoryReference::createWithDisplacement(cg, cg->getMethodMetaDataRegister(),
               fej9->thisThreadRememberedSetFragmentOffset() + fej9->getFragmentParentOffset(), TR::Compiler->om.sizeofReferenceAddress());
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg, fragmentParentMR);

         TR::MemoryReference *globalFragmentIDMR = TR::MemoryReference::createWithDisplacement(cg, temp2Reg, fej9->getRememberedSetGlobalFragmentOffset(), TR::Compiler->om.sizeofReferenceAddress());
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg, globalFragmentIDMR);

         // if barrier not enabled, nothing to do
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, condReg, temp1Reg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);

         // now check if barrier absolutely necessary, according to GC: if it is, then make sure we go out-of-line
         // if the global fragment index and local fragment index don't match, go to the snippet
         TR::MemoryReference *localFragmentIndexMR = TR::MemoryReference::createWithDisplacement(cg, cg->getMethodMetaDataRegister(),
               fej9->thisThreadRememberedSetFragmentOffset() + fej9->getLocalFragmentOffset(), TR::Compiler->om.sizeofReferenceAddress());
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg, localFragmentIndexMR);
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, condReg, temp2Reg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg);

         // null test on the reference we're about to store over: if it is null goto doneLabel
         TR::InstOpCode::Mnemonic opCode = TR::InstOpCode::lwz;
         int32_t size = 4;
         if (comp->target().is64Bit() && !isCompressedRef)
            {
            opCode = TR::InstOpCode::ld;
            size = 8;
            }
         generateTrg1MemInstruction(cg, opCode, node, temp1Reg, TR::MemoryReference::createWithDisplacement(cg, temp3Reg, 0, size));
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, condReg, temp1Reg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);

         generateLabelInstruction(cg, TR::InstOpCode::label, node, callLabel);
         }
      }

   generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t) wbRef->getSymbol()->castToMethodSymbol()->getMethodAddress(),
         new (cg->trHeapMemory()) TR::RegisterDependencyConditions((uint8_t) 0, 0, cg->trMemory()), wbRef, NULL);
   cg->machine()->setLinkRegisterKilled(true);

   if (temp3RegIsNull && temp3Reg)
      cg->stopUsingRegister(temp3Reg);
   }

static void VMCardCheckEvaluator(TR::Node *node, TR::Register *dstReg, TR::Register *condReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::Register *temp3Reg,
      TR::RegisterDependencyConditions *deps, TR::CodeGenerator *cg)
   {
   TR::Compilation * comp = cg->comp();
   // non-heap objects cannot be marked
   // Make sure we really should be here
   TR::Options *options = comp->getOptions();
   TR::Node *wrtbarNode = NULL;
   bool definitelyHeapObject = false, definitelyNonHeapObject = false;
   auto gcMode = TR::Compiler->om.writeBarrierType();

   if (node->getOpCodeValue() == TR::awrtbari || node->getOpCodeValue() == TR::awrtbar)
      wrtbarNode = node;
   else if (node->getOpCodeValue() == TR::ArrayStoreCHK)
      wrtbarNode = node->getFirstChild();

   if (wrtbarNode != NULL)
      {
      definitelyHeapObject = wrtbarNode->isHeapObjectWrtBar();
      definitelyNonHeapObject = wrtbarNode->isNonHeapObjectWrtBar();
      }

   TR_ASSERT((gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental) && !definitelyNonHeapObject,
         "VMCardCheckEvaluator: Invalid call to cardCheckEvaluator\n");

   if (!definitelyNonHeapObject)
      {
      TR::Register *metaReg = cg->getMethodMetaDataRegister();
      TR::LabelSymbol *noChkLabel = generateLabelSymbol(cg);

      // Balanced policy must always dirty the card table.
      //
      if (gcMode != gc_modron_wrtbar_cardmark_incremental)
         {
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, privateFlags), TR::Compiler->om.sizeofReferenceAddress()));

         // The value for J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE is a generated value when VM code is created
         // At the moment we are safe here, but it is better to be careful and avoid any unexpected behaviour
         // Make sure this falls within the scope of andis
         //
         TR_ASSERT(J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE >= 0x00010000 && J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE <= 0x80000000,
               "Concurrent mark active Value assumption broken.");
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andis_r, node, temp1Reg, temp1Reg, condReg, J9_PRIVATE_FLAGS_CONCURRENT_MARK_ACTIVE >> 16);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, noChkLabel, condReg);
         }

#if defined(TR_TARGET_64BIT)
      uintptr_t card_size_shift = trailingZeroes((uint64_t)options->getGcCardSize());
#else
      uintptr_t card_size_shift = trailingZeroes((uint32_t) options->getGcCardSize());
#endif

      // temp3Reg = dstReg - heapBaseForBarrierRange0
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp3Reg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapBaseForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
      generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp3Reg, temp3Reg, dstReg);

      if (!definitelyHeapObject)
         {
         // if (temp3Reg >= heapSizeForBarrierRage0), object not in the heap
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapSizeForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
         generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp3Reg, temp1Reg);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, noChkLabel, condReg);
         }

      // dirty(activeCardTableBase + temp3Reg >> card_size_shift)
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, activeCardTableBase), TR::Compiler->om.sizeofReferenceAddress()));
#if defined(TR_TARGET_64BIT)
      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicl, node, temp3Reg, temp3Reg, 64-card_size_shift, (CONSTANT64(1)<<(64-card_size_shift)) - CONSTANT64(1));
#else
      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, temp3Reg, temp3Reg, 32 - card_size_shift, ((uint32_t) 0xFFFFFFFF) >> card_size_shift);
#endif
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, temp2Reg, CARD_DIRTY);
      generateMemSrc1Instruction(cg, TR::InstOpCode::stb, node, TR::MemoryReference::createWithIndexReg(cg, temp1Reg, temp3Reg, 1), temp2Reg);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, noChkLabel);
      }

   }

static void VMwrtbarEvaluator(TR::Node *node, TR::Register *srcReg, TR::Register *dstReg, TR::Register *dstAddrReg,
      TR::RegisterDependencyConditions *conditions, bool srcNonNull, bool needDeps, bool isCompressedRef, TR::CodeGenerator *cg, TR::Register *flagsReg = NULL)
   {
   TR::Compilation *comp = cg->comp();
   auto gcMode = TR::Compiler->om.writeBarrierType();
   bool doWrtBar = (gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC());
   bool doCrdMrk = ((gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental) && !node->isNonHeapObjectWrtBar());
   TR::LabelSymbol *label;
   TR::Register *cr0, *temp1Reg, *temp2Reg, *temp3Reg = NULL, *temp4Reg = NULL;
   uint8_t numRegs = (doWrtBar && doCrdMrk) ? 7 : 5;

   //Also need to pass in destinationAddress to jitWriteBarrierStoreMetronome
   if (comp->getOptions()->realTimeGC())
      numRegs += 3;

   if (doWrtBar || doCrdMrk)
      numRegs += 2;

   if ((!doWrtBar && !doCrdMrk) || (node->getOpCode().isWrtBar() && node->skipWrtBar()))
      {
      if (flagsReg)
         cg->stopUsingRegister(flagsReg);
      return;
      }
   if (flagsReg)
      temp1Reg = flagsReg;
   else
      temp1Reg = cg->allocateRegister();

   temp2Reg = cg->allocateRegister();
   label = generateLabelSymbol(cg);

   if (doWrtBar || doCrdMrk)
      needDeps = true;

   if (needDeps)
      {
      cr0 = cg->allocateRegister(TR_CCR);
      conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(numRegs, numRegs, cg->trMemory());
      TR::addDependency(conditions, cr0, TR::RealRegister::cr0, TR_CCR, cg);
      TR::addDependency(conditions, dstReg, TR::RealRegister::gr3, TR_GPR, cg);
      TR::addDependency(conditions, temp1Reg, TR::RealRegister::gr11, TR_GPR, cg);
      if (comp->getOptions()->realTimeGC())
         {
         TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         conditions->getPostConditions()->getRegisterDependency(3)->setExcludeGPR0(); //3=temp2Reg
         TR::addDependency(conditions, dstAddrReg, TR::RealRegister::gr4, TR_GPR, cg);
         temp3Reg = dstAddrReg;
         }
      else
         TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      }
   else
      cr0 = conditions->getPostConditions()->getRegisterDependency(2)->getRegister();

   if (doWrtBar)
      {
      if (doCrdMrk)
         {
         temp3Reg = cg->allocateRegister();
         temp4Reg = cg->allocateRegister();
         if (needDeps)
            {
            TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
            TR::addDependency(conditions, temp4Reg, TR::RealRegister::NoReg, TR_GPR, cg);
            conditions->getPostConditions()->getRegisterDependency(3)->setExcludeGPR0(); //3=temp2Reg
            }
         }
      if (needDeps)
         {
         if (comp->getOptions()->realTimeGC())
            TR::addDependency(conditions, srcReg, TR::RealRegister::gr5, TR_GPR, cg);
         else
            TR::addDependency(conditions, srcReg, TR::RealRegister::gr4, TR_GPR, cg);
         }
      if (!srcNonNull && !comp->getOptions()->realTimeGC())
         {
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cr0, srcReg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, label, cr0);
         }

      VMnonNullSrcWrtBarCardCheckEvaluator(node, srcReg, dstReg, cr0, temp1Reg, temp2Reg, temp3Reg, temp4Reg, label, conditions, isCompressedRef, cg,
            flagsReg != NULL);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, label, conditions);
      if (doCrdMrk)
         {
         cg->stopUsingRegister(temp3Reg);
         cg->stopUsingRegister(temp4Reg);
         }
      else if (comp->getOptions()->realTimeGC())
         cg->stopUsingRegister(temp3Reg);
      }
   else if (doCrdMrk)
      {
      temp3Reg = cg->allocateRegister();
      if (needDeps)
         {
         TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         conditions->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0(); //1=dstReg
         conditions->getPostConditions()->getRegisterDependency(2)->setExcludeGPR0(); //2=temp1Reg
         }

      VMCardCheckEvaluator(node, dstReg, cr0, temp1Reg, temp2Reg, temp3Reg, conditions, cg);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, label, conditions);
      cg->stopUsingRegister(temp3Reg);
      }
   if (needDeps)
      {
      cg->stopUsingRegister(cr0);
      }
   cg->stopUsingRegister(temp1Reg);
   cg->stopUsingRegister(temp2Reg);
   }

inline void generateLoadJ9Class(TR::Node* node, TR::Register* j9classReg, TR::Register *objReg, TR::CodeGenerator* cg)
   {
   if (TR::Compiler->om.compressObjectReferences())
      generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, j9classReg,
         TR::MemoryReference::createWithDisplacement(cg, objReg, static_cast<int32_t>(TR::Compiler->om.offsetOfObjectVftField()), 4));
   else
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, j9classReg,
         TR::MemoryReference::createWithDisplacement(cg, objReg, static_cast<int32_t>(TR::Compiler->om.offsetOfObjectVftField()), TR::Compiler->om.sizeofReferenceAddress()));
   TR::TreeEvaluator::generateVFTMaskInstruction(cg, node, j9classReg);
   }

void
J9::Power::TreeEvaluator::generateFillInDataBlockSequenceForUnresolvedField(TR::CodeGenerator *cg, TR::Node *node, TR::Snippet *dataSnippet, bool isWrite, TR::Register *sideEffectRegister, TR::Register *dataSnippetRegister)
   {
   TR::Compilation *comp = cg->comp();
   TR::SymbolReference *symRef = node->getSymbolReference();
   bool is64Bit = comp->target().is64Bit();
   bool isStatic = symRef->getSymbol()->getKind() == TR::Symbol::IsStatic;

   TR_RuntimeHelper helperIndex = isWrite? (isStatic ?  TR_jitResolveStaticFieldSetterDirect: TR_jitResolveFieldSetterDirect):
                                           (isStatic ?  TR_jitResolveStaticFieldDirect: TR_jitResolveFieldDirect);

   TR::Linkage *linkage = cg->getLinkage(runtimeHelperLinkage(helperIndex));
   auto linkageProperties = linkage->getProperties();
   intptr_t offsetInDataBlock = isStatic ? offsetof(J9JITWatchedStaticFieldData, fieldAddress): offsetof(J9JITWatchedInstanceFieldData, offset);


   TR::LabelSymbol* startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol* endLabel = generateLabelSymbol(cg);
   TR::LabelSymbol* unresolvedLabel = generateLabelSymbol(cg);
   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   TR::Register *cpIndexReg = cg->allocateRegister();
   TR::Register *resultReg = cg->allocateRegister();
   TR::Register *scratchReg = cg->allocateRegister();
   TR::Register *jumpReg = cg->allocateRegister();

   // Check if snippet has been loaded
   if (!isStatic && !static_cast<TR::J9PPCWatchedInstanceFieldSnippet *>(dataSnippet)->isSnippetLoaded() ||
      (isStatic && !static_cast<TR::J9PPCWatchedStaticFieldSnippet *>(dataSnippet)->isSnippetLoaded()))
         loadFieldWatchSnippet(cg, node, dataSnippet, dataSnippetRegister, scratchReg, !isStatic);


   // Setup Dependencies
   // dataSnippetRegister is always used during OOL sequence.
   // Requires two argument registers: resultReg and cpIndexReg.
   // Static requires an extra arugment fieldClassReg
   // jumpReg used by trampoline
   uint8_t numOfConditions = isStatic? 5 : 4;
   TR::RegisterDependencyConditions  *deps =  new (cg->trHeapMemory()) TR::RegisterDependencyConditions(numOfConditions, numOfConditions, cg->trMemory());

   deps->addPreCondition(dataSnippetRegister, TR::RealRegister::NoReg);
   deps->addPostCondition(dataSnippetRegister, TR::RealRegister::NoReg);

   TR_PPCOutOfLineCodeSection *generateReportOOL = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(unresolvedLabel, endLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(generateReportOOL);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

   // Compare J9JITWatchedInstanceFieldData.offset or J9JITWatchedStaticFieldData.fieldAddress (Depending on Instance of Static)
   // Load value from dataSnippetRegister + offsetInDataBlock then compare and branch
   TR::Register *cndReg = cg->allocateRegister(TR_CCR);
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, scratchReg,
                     TR::MemoryReference::createWithDisplacement(cg, dataSnippetRegister, offsetInDataBlock, TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::Op_cmpi, node, cndReg, scratchReg, -1);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, unresolvedLabel, cndReg);

   generateReportOOL->swapInstructionListsWithCompilation();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, unresolvedLabel);

   bool isSideEffectReg = false;
   if (isStatic)
      {
      // Fill in J9JITWatchedStaticFieldData.fieldClass
      TR::Register *fieldClassReg = NULL;

      if (isWrite)
         {
         fieldClassReg = cg->allocateRegister();
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, fieldClassReg,
                        TR::MemoryReference::createWithDisplacement(cg, sideEffectRegister, comp->fej9()->getOffsetOfClassFromJavaLangClassField(), TR::Compiler->om.sizeofReferenceAddress()));
         }
      else
         {
         isSideEffectReg = true;
         fieldClassReg = sideEffectRegister;
         }
      TR::MemoryReference *memRef = TR::MemoryReference::createWithDisplacement(cg, dataSnippetRegister, offsetof(J9JITWatchedStaticFieldData, fieldClass), TR::Compiler->om.sizeofReferenceAddress());

      // Store value to dataSnippetRegister + offset of fieldClass
      generateMemSrc1Instruction(cg, TR::InstOpCode::Op_st, node, memRef, fieldClassReg);
      deps->addPreCondition(fieldClassReg, TR::RealRegister::NoReg);
      deps->addPostCondition(fieldClassReg, TR::RealRegister::NoReg);
      if (!isSideEffectReg)
         cg->stopUsingRegister(fieldClassReg);
      }

   TR::ResolvedMethodSymbol *methodSymbol = node->getByteCodeInfo().getCallerIndex() == -1 ? comp->getMethodSymbol(): comp->getInlinedResolvedMethodSymbol(node->getByteCodeInfo().getCallerIndex());

   // Store cpAddressReg
   // TODO: Replace when AOT TR_ConstantPool Discontiguous support is enabled
   //loadAddressConstant(cg, node, (uintptr_t)methodSymbol->getResolvedMethod()->constantPool(), resultReg, NULL, false, TR_ConstantPool);
   loadAddressConstant(cg, comp->compileRelocatableCode(), node, reinterpret_cast<uintptr_t>(methodSymbol->getResolvedMethod()->constantPool()), resultReg);
   loadConstant(cg, node, symRef->getCPIndex(), cpIndexReg);

   // cpAddress is the first argument of VMHelper
   deps->addPreCondition(resultReg, TR::RealRegister::gr3);
   deps->addPostCondition(resultReg, TR::RealRegister::gr3);
   // cpIndexReg is the second argument
   deps->addPreCondition(cpIndexReg, TR::RealRegister::gr4);
   deps->addPostCondition(cpIndexReg, TR::RealRegister::gr4);
   // jumpReg is used in trampoline to store callee address
   deps->addPreCondition(jumpReg, TR::RealRegister::gr11);
   deps->addPostCondition(jumpReg, TR::RealRegister::gr11);

   // Generate helper address and branch
   TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(helperIndex);
   TR::Instruction *call = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, reinterpret_cast<uintptr_t>(helperSym->getMethodAddress()), deps, helperSym);
   call->PPCNeedsGCMap(linkageProperties.getPreservedRegisterMapForGC());

   /*
      * For instance field offset, the result returned by the vmhelper includes header size.
      * subtract the header size to get the offset needed by field watch helpers
   */
   if (!isStatic)
      addConstantToInteger(node, resultReg, resultReg , -TR::Compiler->om.objectHeaderSizeInBytes(), cg);

   // store result into J9JITWatchedStaticFieldData.fieldAddress / J9JITWatchedInstanceFieldData.offset
   TR::MemoryReference* dataRef = TR::MemoryReference::createWithDisplacement(cg, dataSnippetRegister, offsetInDataBlock, TR::Compiler->om.sizeofReferenceAddress());
   generateMemSrc1Instruction(cg, TR::InstOpCode::Op_st, node, dataRef, resultReg);

   generateLabelInstruction(cg, TR::InstOpCode::b, node, endLabel);

   generateReportOOL->swapInstructionListsWithCompilation();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, endLabel);

   cg->stopUsingRegister(scratchReg);
   cg->stopUsingRegister(cndReg);
   cg->stopUsingRegister(cpIndexReg);
   cg->stopUsingRegister(resultReg);
   cg->stopUsingRegister(jumpReg);

   }

/*
 * Generate the reporting field access helper call with required arguments
 *
 * jitReportInstanceFieldRead
 * arg1 pointer to static data block
 * arg2 object being read
 *
 * jitReportInstanceFieldWrite
 * arg1 pointer to static data block
 * arg2 object being written to
 * arg3 pointer to value being written
 *
 * jitReportStaticFieldRead
 * arg1 pointer to static data block
 *
 * jitReportStaticFieldWrite
 * arg1 pointer to static data block
 * arg2 pointer to value being written
 *
 */
void generateReportFieldAccessOutlinedInstructions(TR::Node *node, TR::LabelSymbol *endLabel, TR::Register *dataBlockReg, bool isWrite, TR::CodeGenerator *cg, TR::Register *sideEffectRegister, TR::Register *valueReg)
   {
   TR::Compilation *comp = cg->comp();
   bool isInstanceField = node->getOpCode().isIndirect();

   TR_RuntimeHelper helperIndex = isWrite ? (isInstanceField ? TR_jitReportInstanceFieldWrite: TR_jitReportStaticFieldWrite):
                                            (isInstanceField ? TR_jitReportInstanceFieldRead: TR_jitReportStaticFieldRead);

   TR::Linkage *linkage = cg->getLinkage(runtimeHelperLinkage(helperIndex));
   auto linkageProperties = linkage->getProperties();
   TR::Register *valueReferenceReg = NULL;
   TR::Register *jumpReg = cg->allocateRegister();

   // First argument is always the data block.
   // One register used by trampoline
   uint8_t numOfConditions = 2;
   // Instance field report needs the base object
   if (isInstanceField)
      numOfConditions++;
   // Field write report needs:
   // value being written
   // the reference to the value being written
   if (isWrite)
      numOfConditions += 2;
   // Register Pair may be needed
   if(comp->target().is32Bit())
      numOfConditions++;

   TR::RegisterDependencyConditions  *deps =  new (cg->trHeapMemory())TR::RegisterDependencyConditions(numOfConditions, numOfConditions, cg->trMemory());

   /*
    * For reporting field write, reference to the valueNode is needed so we need to store
    * the value on to a stack location first and pass the stack location address as an arguement
    * to the VM helper
    */
   if (isWrite)
      {
      TR::DataType dt = node->getDataType();
      int32_t elementSize = dt == TR::Address ? TR::Compiler->om.sizeofReferenceField() : TR::Symbol::convertTypeToSize(dt);
      TR::InstOpCode::Mnemonic storeOp = getLoadOrStoreFromDataType(cg, dt, elementSize, node->getOpCode().isUnsigned(), false);
      TR::SymbolReference *location = cg->allocateLocalTemp(dt);
      TR::MemoryReference *valueMR = TR::MemoryReference::createWithSymRef(cg, node, location, node->getSize());
      if (!valueReg->getRegisterPair())
         {
         generateMemSrc1Instruction(cg, storeOp, node, valueMR, valueReg);
         deps->addPreCondition(valueReg, TR::RealRegister::NoReg);
         deps->addPostCondition(valueReg, TR::RealRegister::NoReg);
         valueReferenceReg = cg->allocateRegister();
         }
      else
         {
         TR::MemoryReference *tempMR2 =  TR::MemoryReference::createWithMemRef(cg, node, *valueMR, 4, 4);
         generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, valueMR, valueReg->getHighOrder());
         generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, tempMR2, valueReg->getLowOrder());
         deps->addPreCondition(valueReg->getHighOrder(), TR::RealRegister::NoReg);
         deps->addPostCondition(valueReg->getHighOrder(), TR::RealRegister::NoReg);
         valueReferenceReg = valueReg->getLowOrder();
         }

      // store the stack location into a register
      generateTrg1MemInstruction(cg, TR::InstOpCode::addi2, node, valueReferenceReg, valueMR);
      }

   // First Argument - DataBlock
   deps->addPreCondition(dataBlockReg, TR::RealRegister::gr3);
   deps->addPostCondition(dataBlockReg, TR::RealRegister::gr3);

   // Second Argument
   if (isInstanceField)
      {
      deps->addPreCondition(sideEffectRegister, TR::RealRegister::gr4);
      deps->addPostCondition(sideEffectRegister, TR::RealRegister::gr4);
      }
   else if (isWrite)
      {
      deps->addPreCondition(valueReferenceReg, TR::RealRegister::gr4);
      deps->addPostCondition(valueReferenceReg, TR::RealRegister::gr4);
      }

   // Third Argument
   if (isInstanceField && isWrite)
      {
      deps->addPreCondition(valueReferenceReg, TR::RealRegister::gr5);
      deps->addPostCondition(valueReferenceReg, TR::RealRegister::gr5);
      }

   // Register ued by trampoline to store callee address
   deps->addPreCondition(jumpReg, TR::RealRegister::gr11);
   deps->addPostCondition(jumpReg, TR::RealRegister::gr11);

   // Generate branch instruction to jump into helper
   TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(helperIndex);
   TR::Instruction *call = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, reinterpret_cast<uintptr_t>(helperSym->getMethodAddress()), deps, helperSym);
   call->PPCNeedsGCMap(linkageProperties.getPreservedRegisterMapForGC());

   generateLabelInstruction(cg, TR::InstOpCode::b, node, endLabel);

   cg->stopUsingRegister(valueReferenceReg);
   cg->stopUsingRegister(jumpReg);

   }

void loadFieldWatchSnippet(TR::CodeGenerator *cg, TR::Node *node, TR::Snippet *dataSnippet, TR::Register *snippetReg, TR::Register *scratchReg, bool isInstanceField)
   {
   TR::Compilation *comp = cg->comp();
   int32_t beginIndex = PTOC_FULL_INDEX;

   cg->fixedLoadLabelAddressIntoReg(node, snippetReg, dataSnippet->getSnippetLabel());

   // Loaded Snippet
   if (!isInstanceField)
      {
      static_cast<TR::J9PPCWatchedStaticFieldSnippet *>(dataSnippet)->setLoadSnippet();
      }
   else
      {
      static_cast<TR::J9PPCWatchedInstanceFieldSnippet *>(dataSnippet)->setLoadSnippet();
      }
   }

TR::Snippet *
J9::Power::TreeEvaluator::getFieldWatchInstanceSnippet(TR::CodeGenerator *cg, TR::Node *node, J9Method *m, UDATA loc, UDATA os)
   {
   return new (cg->trHeapMemory()) TR::J9PPCWatchedInstanceFieldSnippet(cg, node, m, loc, os);
   }

TR::Snippet *
J9::Power::TreeEvaluator::getFieldWatchStaticSnippet(TR::CodeGenerator *cg, TR::Node *node, J9Method *m, UDATA loc, void *fieldAddress, J9Class *fieldClass)
   {
   return new (cg->trHeapMemory()) TR::J9PPCWatchedStaticFieldSnippet(cg, node, m, loc, fieldAddress, fieldClass);
   }

void
J9::Power::TreeEvaluator::generateTestAndReportFieldWatchInstructions(TR::CodeGenerator *cg, TR::Node *node, TR::Snippet *dataSnippet, bool isWrite, TR::Register *sideEffectRegister, TR::Register *valueReg, TR::Register *dataSnippetRegister)
   {
   bool isInstanceField = node->getOpCode().isIndirect();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(cg->fe());

   TR::Register *scratchReg = cg->allocateRegister();

   // Check if snippet has been loaded
   if (isInstanceField && !static_cast<TR::J9PPCWatchedInstanceFieldSnippet *>(dataSnippet)->isSnippetLoaded() ||
      (!isInstanceField && !static_cast<TR::J9PPCWatchedStaticFieldSnippet *>(dataSnippet)->isSnippetLoaded()))
      loadFieldWatchSnippet(cg, node, dataSnippet, dataSnippetRegister, scratchReg, isInstanceField);

   TR::LabelSymbol* startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol* endLabel = generateLabelSymbol(cg);
   TR::LabelSymbol* fieldReportLabel = generateLabelSymbol(cg);
   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

   TR_PPCOutOfLineCodeSection *generateReportOOL = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(fieldReportLabel, endLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(generateReportOOL);

   TR::Register *fieldClassReg = NULL;
   bool isSideEffectReg = false;
   // Load fieldClass
   if (isInstanceField)
      {
      fieldClassReg = cg->allocateRegister();
      generateLoadJ9Class(node, fieldClassReg, sideEffectRegister, cg);
      }
   else if (!(node->getSymbolReference()->isUnresolved()))
      {
      fieldClassReg = cg->allocateRegister();
      // During Non-AOT (JIT and JITServer) compilation the fieldClass has been populated inside the dataSnippet during compilation.
      // During AOT compilation the fieldClass must be loaded from the snippet. The fieldClass in an AOT body is invalid.
      if (cg->needClassAndMethodPointerRelocations())
         {
         // Load FieldClass from snippet
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, fieldClassReg,
                           TR::MemoryReference::createWithDisplacement(cg, dataSnippetRegister, offsetof(J9JITWatchedStaticFieldData, fieldClass),
                           TR::Compiler->om.sizeofReferenceAddress()));
         }
      else
         {
         J9Class * fieldClass = static_cast<TR::J9PPCWatchedStaticFieldSnippet *>(dataSnippet)->getFieldClass();
         loadAddressConstant(cg, false, node, reinterpret_cast<uintptr_t>(fieldClass), fieldClassReg);
         }
      }
   else
      {
      // Unresolved
      if (isWrite)
         {
         fieldClassReg = cg->allocateRegister();
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, fieldClassReg,
               TR::MemoryReference::createWithDisplacement(cg, sideEffectRegister, fej9->getOffsetOfClassFromJavaLangClassField(), TR::Compiler->om.sizeofReferenceAddress()));
         }
      else
         {
         isSideEffectReg = true;
         fieldClassReg = sideEffectRegister;
         }
      }

   TR::MemoryReference *classFlagsMemRef = TR::MemoryReference::createWithDisplacement(cg, fieldClassReg, static_cast<uintptr_t>(fej9->getOffsetOfClassFlags()), 4);

   TR::Register *cndReg = cg->allocateRegister(TR_CCR);
   generateTrg1MemInstruction(cg,TR::InstOpCode::lwz, node, scratchReg, classFlagsMemRef);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, scratchReg, scratchReg, cndReg, J9ClassHasWatchedFields);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, fieldReportLabel, cndReg);

	generateReportOOL->swapInstructionListsWithCompilation();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, fieldReportLabel);
   generateReportFieldAccessOutlinedInstructions(node, endLabel, dataSnippetRegister, isWrite, cg, sideEffectRegister, valueReg);

   generateReportOOL->swapInstructionListsWithCompilation();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, endLabel);

   cg->stopUsingRegister(cndReg);
   cg->stopUsingRegister(scratchReg);
   if (!isSideEffectReg)
      cg->stopUsingRegister(fieldClassReg);
   cg->stopUsingRegister(valueReg);

   cg->machine()->setLinkRegisterKilled(true);

   }

TR::Register *J9::Power::TreeEvaluator::awrtbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   TR::Register *valueReg = cg->evaluate(node->getFirstChild());
   TR::Register *sideEffectRegister = cg->evaluate(node->getSecondChild());
   if (comp->getOption(TR_EnableFieldWatch) && !node->getSymbolReference()->getSymbol()->isShadow())
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }

   if (comp->isOptServer() && !comp->compileRelocatableCode() && !comp->getOptions()->realTimeGC())
      {
      if (!comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8))
         {
         static bool disableOutlinedWrtbar = feGetEnv("TR_ppcDisableOutlinedWriteBarrier") != NULL;
         if (!disableOutlinedWrtbar)
            {
            return outlinedHelperWrtbarEvaluator(node, cg);
            }
         }
      else
         {
         static bool enableOutlinedWrtbar = feGetEnv("TR_ppcEnableOutlinedWriteBarrier") != NULL;
         if (enableOutlinedWrtbar)
            {
            return outlinedHelperWrtbarEvaluator(node, cg);
            }
         }
      }

   TR::Register *destinationRegister = cg->evaluate(node->getSecondChild());
   TR::Register *flagsReg = NULL;
   TR::Node *firstChild = node->getFirstChild();
   TR::Register *sourceRegister;
   bool killSource = false;

   if (firstChild->getReferenceCount() > 1 && firstChild->getRegister() != NULL)
      {
      if (!firstChild->getRegister()->containsInternalPointer())
         sourceRegister = cg->allocateCollectedReferenceRegister();
      else
         {
         sourceRegister = cg->allocateRegister();
         sourceRegister->setPinningArrayPointer(firstChild->getRegister()->getPinningArrayPointer());
         sourceRegister->setContainsInternalPointer();
         }
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, sourceRegister, firstChild->getRegister());
      killSource = true;
      }
   else
      sourceRegister = cg->evaluate(firstChild);

   if (!node->skipWrtBar() && !node->hasUnresolvedSymbolReference()
         && (TR::Compiler->om.writeBarrierType() == gc_modron_wrtbar_oldcheck || TR::Compiler->om.writeBarrierType() == gc_modron_wrtbar_cardmark_and_oldcheck))
      {
      flagsReg = cg->allocateRegister();
      generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, flagsReg, TR::MemoryReference::createWithDisplacement(cg, destinationRegister, getOffsetOfJ9ObjectFlags(), 4));
      }

   // RealTimeGC write barriers occur BEFORE the store
   if (comp->getOptions()->realTimeGC())
      {
      TR::Register *destinationAddressRegister = cg->allocateRegister();

      TR::LoadStoreHandler::generateComputeAddressSequence(cg, destinationAddressRegister, node);
      VMwrtbarEvaluator(node, sourceRegister, destinationRegister, destinationAddressRegister, NULL, firstChild->isNonNull(), true, false, cg);
      TR::LoadStoreHandler::generateStoreAddressSequence(cg, sourceRegister, node, destinationAddressRegister, TR::InstOpCode::Op_st, TR::Compiler->om.sizeofReferenceAddress());

      cg->stopUsingRegister(destinationAddressRegister);
      }
   else
      {
      TR::LoadStoreHandler::generateStoreNodeSequence(cg, sourceRegister, node, TR::InstOpCode::Op_st, TR::Compiler->om.sizeofReferenceAddress());
      VMwrtbarEvaluator(node, sourceRegister, destinationRegister, NULL, NULL, firstChild->isNonNull(), true, false, cg, flagsReg);
      }

   if (killSource)
      cg->stopUsingRegister(sourceRegister);

   cg->decReferenceCount(node->getFirstChild());
   cg->decReferenceCount(node->getSecondChild());

   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::awrtbariEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());

   TR::Node *valueNode = NULL;
   TR::TreeEvaluator::getIndirectWrtbarValueNode(cg, node, valueNode, false);
   TR::Register *valueReg = cg->evaluate(valueNode);
   TR::Register *sideEffectRegister = cg->evaluate(node->getThirdChild());

   TR::Register *destinationRegister = cg->evaluate(node->getChild(2));
   TR::Node *secondChild = node->getSecondChild();
   TR::Register *sourceRegister;
   TR::Register *flagsReg = NULL;
   bool killSource = false;

   bool usingCompressedPointers = false;
   bool bumpedRefCount = false;

   if (comp->getOption(TR_EnableFieldWatch) && !node->getSymbolReference()->getSymbol()->isArrayShadowSymbol())
      {
      // The Third child (sideEffectNode) and valueReg's node is also used by the store evaluator below.
      // The store evaluator will also evaluate+decrement it. In order to avoid double
      // decrementing the node we skip doing it here and let the store evaluator do it.
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }

   if (comp->useCompressedPointers() && (node->getSymbolReference()->getSymbol()->getDataType() == TR::Address) && (node->getSecondChild()->getDataType() != TR::Address))
      {
      usingCompressedPointers = true;
      while (secondChild->getNumChildren() && secondChild->getOpCodeValue() != TR::a2l)
         secondChild = secondChild->getFirstChild();
      if (secondChild->getNumChildren())
         secondChild = secondChild->getFirstChild();
      }

   int32_t sizeofMR = TR::Compiler->om.sizeofReferenceAddress();
   if (usingCompressedPointers)
      sizeofMR = TR::Compiler->om.sizeofReferenceField();

   TR::Register *compressedReg;
   if (secondChild->getReferenceCount() > 1 && secondChild->getRegister() != NULL)
      {
      if (!secondChild->getRegister()->containsInternalPointer())
         sourceRegister = cg->allocateCollectedReferenceRegister();
      else
         {
         sourceRegister = cg->allocateRegister();
         sourceRegister->setPinningArrayPointer(secondChild->getRegister()->getPinningArrayPointer());
         sourceRegister->setContainsInternalPointer();
         }
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, sourceRegister, secondChild->getRegister());
      killSource = true;
      compressedReg = sourceRegister;
      if (usingCompressedPointers)
         compressedReg = cg->evaluate(node->getSecondChild());
      }
   else
      {
      sourceRegister = cg->evaluate(secondChild);
      compressedReg = sourceRegister;
      if (usingCompressedPointers)
         compressedReg = cg->evaluate(node->getSecondChild());
      }

   if (!node->skipWrtBar() && !node->hasUnresolvedSymbolReference()
         && (TR::Compiler->om.writeBarrierType() == gc_modron_wrtbar_oldcheck || TR::Compiler->om.writeBarrierType() == gc_modron_wrtbar_cardmark_and_oldcheck))
      {
      flagsReg = cg->allocateRegister();
      generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, flagsReg, TR::MemoryReference::createWithDisplacement(cg, destinationRegister, getOffsetOfJ9ObjectFlags(), 4));
      }

   TR::InstOpCode::Mnemonic storeOp = usingCompressedPointers ? TR::InstOpCode::stw : TR::InstOpCode::Op_st;
   TR::Register *storeRegister = usingCompressedPointers ? compressedReg : sourceRegister;

   // RealTimeGC write barriers occur BEFORE the store
   if (comp->getOptions()->realTimeGC())
      {
      TR::Register *destinationAddressRegister = cg->allocateRegister();

      TR::LoadStoreHandler::generateComputeAddressSequence(cg, destinationAddressRegister, node);
      VMwrtbarEvaluator(node, sourceRegister, destinationRegister, destinationAddressRegister, NULL, secondChild->isNonNull(), true, usingCompressedPointers, cg);
      TR::LoadStoreHandler::generateStoreAddressSequence(cg, storeRegister, node, destinationAddressRegister, storeOp, sizeofMR);

      cg->stopUsingRegister(destinationAddressRegister);
      }
   else
      {
      TR::LoadStoreHandler::generateStoreNodeSequence(cg, storeRegister, node, storeOp, sizeofMR);
      VMwrtbarEvaluator(node, sourceRegister, destinationRegister, NULL, NULL, secondChild->isNonNull(), true, usingCompressedPointers, cg, flagsReg);
      }

   if (killSource)
      cg->stopUsingRegister(sourceRegister);

   cg->decReferenceCount(node->getSecondChild());
   cg->decReferenceCount(node->getChild(2));

   if (comp->useCompressedPointers())
      node->setStoreAlreadyEvaluated(true);

   return NULL;
   }

TR::Register *iGenerateSoftwareReadBarrier(TR::Node *node, TR::CodeGenerator *cg)
   {
#ifndef OMR_GC_CONCURRENT_SCAVENGER
   TR_ASSERT_FATAL(false, "Concurrent Scavenger not supported.");
#else
   TR::Compilation *comp = cg->comp();

   TR::Register *objReg = cg->allocateRegister();
   TR::Register *locationReg = cg->allocateRegister();
   TR::Register *evacuateReg = cg->allocateRegister();
   TR::Register *r3Reg = cg->allocateRegister();
   TR::Register *r11Reg = cg->allocateRegister();
   TR::Register *metaReg = cg->getMethodMetaDataRegister();
   TR::Register *condReg = cg->allocateRegister(TR_CCR);

   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *endLabel = generateLabelSymbol(cg);
   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 7, cg->trMemory());
   deps->addPostCondition(objReg, TR::RealRegister::NoReg);
   deps->addPostCondition(locationReg, TR::RealRegister::gr4); //TR_softwareReadBarrier helper needs this in gr4.
   deps->addPostCondition(evacuateReg, TR::RealRegister::NoReg);
   deps->addPostCondition(r3Reg, TR::RealRegister::gr3);
   deps->addPostCondition(r11Reg, TR::RealRegister::gr11);
   deps->addPostCondition(metaReg, TR::RealRegister::NoReg);
   deps->addPostCondition(condReg, TR::RealRegister::NoReg);

   if (node->getSymbolReference()->getSymbol()->isInternalPointer())
      {
      objReg->setPinningArrayPointer(node->getSymbolReference()->getSymbol()->castToInternalPointerAutoSymbol()->getPinningArrayPointer());
      objReg->setContainsInternalPointer();
      }

   node->setRegister(objReg);

   TR::LoadStoreHandler::generateComputeAddressSequence(cg, locationReg, node);

   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, objReg, TR::MemoryReference::createWithDisplacement(cg, locationReg, 0, 4));

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);
   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, evacuateReg,
         TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateBaseAddressOffset(), 4));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, condReg, objReg, evacuateReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, endLabel, condReg);

   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, evacuateReg,
         TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateTopAddressOffset(), 4));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, condReg, objReg, evacuateReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, endLabel, condReg);

   // TR_softwareReadBarrier helper expects the vmThread in r3.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r3Reg, metaReg);

   TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_softwareReadBarrier);
   generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), deps, helperSym);

   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, objReg, TR::MemoryReference::createWithDisplacement(cg, locationReg, 0, 4));

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, deps);

   // TODO: Allow this to be patched or skipped at runtime for unresolved symrefs
   if (node->getSymbol()->isSyncVolatile() && comp->target().isSMP())
      {
      generateInstruction(cg, comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P7) ? TR::InstOpCode::lwsync : TR::InstOpCode::isync, node);
      }

   cg->insertPrefetchIfNecessary(node, objReg);

   cg->stopUsingRegister(evacuateReg);
   cg->stopUsingRegister(locationReg);
   cg->stopUsingRegister(r3Reg);
   cg->stopUsingRegister(r11Reg);
   cg->stopUsingRegister(condReg);

   cg->machine()->setLinkRegisterKilled(true);

   return objReg;
#endif
   }

TR::Register *aGenerateSoftwareReadBarrier(TR::Node *node, TR::CodeGenerator *cg)
   {
#ifndef OMR_GC_CONCURRENT_SCAVENGER
   TR_ASSERT_FATAL(false, "Concurrent Scavenger not supported.");
#else
   TR::Compilation *comp = cg->comp();

   TR::Register *tempReg;
   TR::Register *locationReg = cg->allocateRegister();
   TR::Register *evacuateReg = cg->allocateRegister();
   TR::Register *r3Reg = cg->allocateRegister();
   TR::Register *r11Reg = cg->allocateRegister();
   TR::Register *metaReg = cg->getMethodMetaDataRegister();
   TR::Register *condReg = cg->allocateRegister(TR_CCR);

   if (!node->getSymbolReference()->getSymbol()->isInternalPointer())
      {
      if (node->getSymbolReference()->getSymbol()->isNotCollected())
         tempReg = cg->allocateRegister();
      else
         tempReg = cg->allocateCollectedReferenceRegister();
      }
   else
      {
      tempReg = cg->allocateRegister();
      tempReg->setPinningArrayPointer(node->getSymbolReference()->getSymbol()->castToInternalPointerAutoSymbol()->getPinningArrayPointer());
      tempReg->setContainsInternalPointer();
      }

   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *endLabel = generateLabelSymbol(cg);
   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 7, cg->trMemory());
   deps->addPostCondition(tempReg, TR::RealRegister::NoReg);
   deps->addPostCondition(locationReg, TR::RealRegister::gr4); //TR_softwareReadBarrier helper needs this in gr4.
   deps->addPostCondition(evacuateReg, TR::RealRegister::NoReg);
   deps->addPostCondition(r3Reg, TR::RealRegister::gr3);
   deps->addPostCondition(r11Reg, TR::RealRegister::gr11);
   deps->addPostCondition(metaReg, TR::RealRegister::NoReg);
   deps->addPostCondition(condReg, TR::RealRegister::NoReg);

   node->setRegister(tempReg);

   TR::LoadStoreHandler::generateComputeAddressSequence(cg, locationReg, node);

   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, tempReg, TR::MemoryReference::createWithDisplacement(cg, locationReg, 0, TR::Compiler->om.sizeofReferenceAddress()));

   if (node->getSymbolReference() == comp->getSymRefTab()->findVftSymbolRef())
      TR::TreeEvaluator::generateVFTMaskInstruction(cg, node, tempReg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, evacuateReg,
         TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateBaseAddressOffset(), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, condReg, tempReg, evacuateReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, endLabel, condReg);

   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, evacuateReg,
         TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateTopAddressOffset(), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, condReg, tempReg, evacuateReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, endLabel, condReg);

   // TR_softwareReadBarrier helper expects the vmThread in r3.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r3Reg, metaReg);

   TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_softwareReadBarrier);
   generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), deps, helperSym);

   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, tempReg, TR::MemoryReference::createWithDisplacement(cg, locationReg, 0, TR::Compiler->om.sizeofReferenceAddress()));

   if (node->getSymbolReference() == comp->getSymRefTab()->findVftSymbolRef())
      TR::TreeEvaluator::generateVFTMaskInstruction(cg, node, tempReg);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, deps);

   // TODO: Allow this to be patched or skipped at runtime for unresolved symrefs
   if (node->getSymbol()->isSyncVolatile() && comp->target().isSMP())
      {
      generateInstruction(cg, comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P7) ? TR::InstOpCode::lwsync : TR::InstOpCode::isync, node);
      }

   cg->stopUsingRegister(evacuateReg);
   cg->stopUsingRegister(locationReg);
   cg->stopUsingRegister(r3Reg);
   cg->stopUsingRegister(r11Reg);
   cg->stopUsingRegister(condReg);

   cg->machine()->setLinkRegisterKilled(true);

   return tempReg;
#endif
   }

TR::Register *J9::Power::TreeEvaluator::fwrtbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getSecondChild();
   TR::Register *valueReg = cg->evaluate(node->getFirstChild());
   TR::Register *sideEffectRegister = cg->evaluate(sideEffectNode);
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }
   // The Value Node, or the second child is not decremented here. The store evaluator also uses it, and decrements it.
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::fstoreEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::fwrtbariEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getThirdChild();
   TR::Register *valueReg = cg->evaluate(node->getSecondChild());
   TR::Register *sideEffectRegister = cg->evaluate(sideEffectNode);
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }
   // The Value Node, or the second child is not decremented here. The store evaluator also uses it, and decrements it.
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::fstoreEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::dwrtbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getSecondChild();
   TR::Register *valueReg = cg->evaluate(node->getFirstChild());
   TR::Register *sideEffectRegister = cg->evaluate(node->getSecondChild());
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }
   // The Value Node, or the second child is not decremented here. The store evaluator also uses it, and decrements it.
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::dstoreEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::dwrtbariEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getThirdChild();
   TR::Register *valueReg = cg->evaluate(node->getSecondChild());
   TR::Register *sideEffectRegister = cg->evaluate(node->getThirdChild());
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, valueReg);
      }
   // The Value Node, or the second child is not decremented here. The store evaluator also uses it, and decrements it.
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::dstoreEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::irdbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getFirstChild();
   TR::Register * sideEffectRegister = cg->evaluate(sideEffectNode);
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, NULL);
      }
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::iloadEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::irdbariEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getFirstChild();
   TR::Register *sideEffectRegister = cg->evaluate(sideEffectNode);
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, NULL);
      }

   // Note: For indirect rdbar nodes, the first child (sideEffectNode) is also used by the
   // load evaluator. The load evaluator will also evaluate+decrement it. In order to avoid double
   // decrementing the node we skip doing it here and let the load evaluator do it.
   if (TR::Compiler->om.readBarrierType() != gc_modron_readbar_none &&
       cg->comp()->useCompressedPointers() &&
       (node->getOpCode().hasSymbolReference() &&
        node->getSymbolReference()->getSymbol()->getDataType() == TR::Address))
      {
      return iGenerateSoftwareReadBarrier(node, cg);
      }
   else
      return TR::TreeEvaluator::iloadEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::ardbarEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Node *sideEffectNode = node->getFirstChild();
   TR::Register *sideEffectRegister = cg->evaluate(sideEffectNode);
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, NULL);
      }
   cg->decReferenceCount(sideEffectNode);
   return TR::TreeEvaluator::aloadEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::ardbariEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // For rdbar and wrtbar nodes we first evaluate the children we need to
   // handle the side effects. Then we delegate the evaluation of the remaining
   // children and the load/store operation to the appropriate load/store evaluator.
   TR::Register *sideEffectRegister = cg->evaluate(node->getFirstChild());
   if (cg->comp()->getOption(TR_EnableFieldWatch))
      {
      TR::TreeEvaluator::rdWrtbarHelperForFieldWatch(node, cg, sideEffectRegister, NULL);
      }
   // Note: For indirect rdbar nodes, the first child (sideEffectNode) is also used by the
   // load evaluator. The load evaluator will also evaluate+decrement it. In order to avoid double
   // decrementing the node we skip doing it here and let the load evaluator do it.
   if (TR::Compiler->om.readBarrierType() == gc_modron_readbar_none)
      return TR::TreeEvaluator::aloadEvaluator(node, cg);
   else
      return aGenerateSoftwareReadBarrier(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::monentEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::VMmonentEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::monexitEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::VMmonexitEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::asynccheckEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // The child contains an inline test. If it succeeds, the helper is called
   // or (with TR_ASYNC_CHECK_TRAPS) trap handler is invoked.
   // The address of the helper is contained as an int in this node.
   //
   TR::Compilation *comp = cg->comp();
   TR::Node *testNode = node->getFirstChild();
   TR::Node *firstChild = testNode->getFirstChild();
   TR::Register *src1Reg = cg->evaluate(firstChild);
   TR::Node *secondChild = testNode->getSecondChild();

   TR_ASSERT(testNode->getOpCodeValue() == (comp->target().is64Bit() ? TR::lcmpeq : TR::icmpeq), "asynccheck bad format");
   TR_ASSERT(secondChild->getOpCode().isLoadConst() && secondChild->getRegister() == NULL, "asynccheck bad format");

   TR::Instruction *gcPoint;

#if defined(TR_ASYNC_CHECK_TRAPS)
   if (cg->getHasResumableTrapHandler())
      {
      if (comp->target().is64Bit())
         {
         TR_ASSERT(secondChild->getLongInt() == -1L, "asynccheck bad format");
         gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::tdeqi, node, src1Reg, secondChild->getLongInt());
         }
      else
         {
         TR_ASSERT(secondChild->getInt() == -1, "asynccheck bad format");
         gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::tweqi, node, src1Reg, secondChild->getInt());
         }
      cg->setCanExceptByTrap();
      }
   else
#endif
      {
      TR::Register *condReg = cg->allocateRegister(TR_CCR);
      if (comp->target().is64Bit())
         {
         TR_ASSERT(secondChild->getLongInt() == -1L, "asynccheck bad format");
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi8, node, condReg, src1Reg, secondChild->getLongInt());
         }
      else
         {
         TR_ASSERT(secondChild->getInt() == -1, "asynccheck bad format");
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, src1Reg, secondChild->getInt());
         }

      TR::LabelSymbol *snippetLabel;
      TR::Register *jumpRegister = cg->allocateRegister();
      TR::RegisterDependencyConditions *dependencies = createConditionsAndPopulateVSXDeps(cg, 2);

      snippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
      if (snippetLabel == NULL)
         {
         snippetLabel = generateLabelSymbol(cg);
         cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference()));
         }
      TR::addDependency(dependencies, jumpRegister, TR::RealRegister::gr11, TR_GPR, cg);
      TR::addDependency(dependencies, condReg, TR::RealRegister::cr7, TR_CCR, cg);
      gcPoint = generateDepConditionalBranchInstruction(cg, TR::InstOpCode::beql, PPCOpProp_BranchUnlikely, node, snippetLabel, condReg, dependencies);
      gcPoint->setAsyncBranch();
      cg->machine()->setLinkRegisterKilled(true);

      dependencies->stopUsingDepRegs(cg);
      }

   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   cg->decReferenceCount(testNode);
   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::instanceofEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return VMinstanceOfEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::checkcastEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::VMcheckcastEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::checkcastAndNULLCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return checkcastEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::newObjectEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // If the helper symbol set on the node is TR_newValue, we are (expecting to be)
   // dealing with a value type. Since we do not fully support value types yet, always
   // call the JIT helper to do the allocation.
   //
   TR::Compilation* comp = cg->comp();
   if (comp->suppressAllocationInlining() ||
       (TR::Compiler->om.areValueTypesEnabled() && node->getSymbolReference() == comp->getSymRefTab()->findOrCreateNewValueSymbolRef(comp->getMethodSymbol())))
      {
      TR::ILOpCodes opCode = node->getOpCodeValue();
      TR::Node::recreate(node, TR::acall);
      TR::Register *targetRegister = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return targetRegister;
      }
   else
      return TR::TreeEvaluator::VMnewEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::newArrayEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   if (cg->comp()->suppressAllocationInlining())
      {
      TR::ILOpCodes opCode = node->getOpCodeValue();
      TR::Node::recreate(node, TR::acall);
      TR::Register *targetRegister = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return targetRegister;
      }
   else
      return TR::TreeEvaluator::VMnewEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::anewArrayEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   if (cg->comp()->suppressAllocationInlining())
      {
      TR::ILOpCodes opCode = node->getOpCodeValue();
      TR::Node::recreate(node, TR::acall);
      TR::Register *targetRegister = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return targetRegister;
      }
   else
      return TR::TreeEvaluator::VMnewEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::multianewArrayEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::ILOpCodes opCode = node->getOpCodeValue();
   TR::Node::recreate(node, TR::acall);
   TR::Register *targetRegister = directCallEvaluator(node, cg);
   TR::Node::recreate(node, opCode);
   return targetRegister;
   }

TR::Register *J9::Power::TreeEvaluator::arraylengthEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_ASSERT(cg->comp()->requiresSpineChecks(), "TR::arraylength should be lowered when hybrid arraylets are not in use");
   TR_ASSERT(node->getOpCodeValue() == TR::arraylength, "arraylengthEvaluator expecting TR::arraylength");

   TR::Register *objectReg = cg->evaluate(node->getFirstChild());
   TR::Register *lengthReg = cg->allocateRegister();
   TR::Register *condReg = cg->allocateRegister(TR_CCR);
   TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());

   TR::MemoryReference *contiguousArraySizeMR = TR::MemoryReference::createWithDisplacement(cg, objectReg, fej9->getOffsetOfContiguousArraySizeField(), 4);
   TR::MemoryReference *discontiguousArraySizeMR = TR::MemoryReference::createWithDisplacement(cg, objectReg, fej9->getOffsetOfDiscontiguousArraySizeField(), 4);

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 3, cg->trMemory());
   deps->addPostCondition(objectReg, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
   deps->addPostCondition(lengthReg, TR::RealRegister::NoReg);
   deps->addPostCondition(condReg, TR::RealRegister::NoReg);

   // lwz R, [B + contiguousSize]       ; Load contiguous array length
   // cmp R, 0                          ; If 0, must be a discontiguous array
   // beq out-of-line
   // done:
   //
   // out-of-line:
   // lwz R, [B + discontiguousSize]    ; Load discontiguous array length
   // b done

   TR::LabelSymbol *discontiguousArrayLabel = generateLabelSymbol(cg);
   TR_PPCOutOfLineCodeSection *discontiguousArrayOOL = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(discontiguousArrayLabel, doneLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(discontiguousArrayOOL);

   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, lengthReg, contiguousArraySizeMR);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, lengthReg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, PPCOpProp_BranchUnlikely, node, discontiguousArrayLabel, condReg);
   generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel);

   // OOL begin.
   //
   discontiguousArrayOOL->swapInstructionListsWithCompilation();
      {
      generateLabelInstruction(cg, TR::InstOpCode::label, node, discontiguousArrayLabel);
      generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, lengthReg, discontiguousArraySizeMR);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
      }
   discontiguousArrayOOL->swapInstructionListsWithCompilation();
   //
   // OOL end.

   TR::LabelSymbol *depLabel = generateLabelSymbol(cg);
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, depLabel, deps);

   cg->stopUsingRegister(condReg);
   cg->decReferenceCount(node->getFirstChild());
   node->setRegister(lengthReg);

   return lengthReg;
   }

TR::Register *J9::Power::TreeEvaluator::DIVCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR::Node *secondChild = node->getFirstChild()->getSecondChild();
   TR::DataType type = secondChild->getType();
   TR::Register *srcReg;
   TR::Instruction *gcPoint;
   bool constDivisor = secondChild->getOpCode().isLoadConst();
   bool killSrc = false;

   if (!constDivisor || (type.isInt32() && secondChild->getInt() == 0) || (type.isInt64() && secondChild->getLongInt() == 0))
      {
      if (!constDivisor || cg->getHasResumableTrapHandler())
         {
         srcReg = cg->evaluate(secondChild);
         if (type.isInt64() && comp->target().is32Bit())
            {
            TR::Register *trgReg = cg->allocateRegister();
            generateTrg1Src2Instruction(cg, TR::InstOpCode::OR, node, trgReg, srcReg->getHighOrder(), srcReg->getLowOrder());
            srcReg = trgReg;
            killSrc = true;
            }
         }

      if (cg->getHasResumableTrapHandler())
         {
         if (type.isInt64() && comp->target().is64Bit())
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::tdllti, node, srcReg, 1);
         else
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twllti, node, srcReg, 1);
         cg->setCanExceptByTrap();
         }
      else
         {
         TR::LabelSymbol *snippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
         TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(1, 1, cg->trMemory());
         TR::Register *jumpReg = cg->allocateRegister();

         if (snippetLabel == NULL)
            {
            snippetLabel = generateLabelSymbol(cg);
            cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference()));
            }

         // trampoline kills gr11
         TR::addDependency(conditions, jumpReg, TR::RealRegister::gr11, TR_GPR, cg);
         if (constDivisor)
            {
            // Can be improved to: call the helper directly.
            gcPoint = generateDepLabelInstruction(cg, TR::InstOpCode::bl, node, snippetLabel, conditions);
            }
         else
            {
            TR::Register *condReg = cg->allocateRegister(TR_CCR);
            if (type.isInt64() && comp->target().is64Bit())
               generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli8, node, condReg, srcReg, 0);
            else
               generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, srcReg, 0);
            gcPoint = generateDepConditionalBranchInstruction(cg, TR::InstOpCode::beql, PPCOpProp_BranchUnlikely, node, snippetLabel, condReg, conditions);
            cg->stopUsingRegister(condReg);
            }
         cg->stopUsingRegister(jumpReg);
         }

      if (killSrc)
         cg->stopUsingRegister(srcReg);

      gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
      }

   cg->evaluate(node->getFirstChild());
   cg->decReferenceCount(node->getFirstChild());
   return NULL;
   }

static void genBoundCheck(TR::CodeGenerator *cg, TR::Node *node, TR::Register *indexReg, int32_t indexVal, TR::Register *arrayLengthReg, int32_t arrayLengthVal,
      TR::Register *condReg, bool noTrap)
   {
   TR::Instruction *gcPoint;
   if (noTrap)
      {
      TR::LabelSymbol *boundCheckFailSnippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
      if (!boundCheckFailSnippetLabel)
         {
         boundCheckFailSnippetLabel = generateLabelSymbol(cg);
         cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, boundCheckFailSnippetLabel, node->getSymbolReference()));
         }

      if (indexReg)
         generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, condReg, arrayLengthReg, indexReg);
      else
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, arrayLengthReg, indexVal);

      // NOTE: Trampoline kills gr11
      gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::blel, PPCOpProp_BranchUnlikely, node, boundCheckFailSnippetLabel, condReg);
      }
   else
      {
      if (indexReg)
         {
         if (arrayLengthReg)
            gcPoint = generateSrc2Instruction(cg, TR::InstOpCode::twlle, node, arrayLengthReg, indexReg);
         else
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twlgei, node, indexReg, arrayLengthVal);
         }
      else
         {
         if (arrayLengthReg)
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twllei, node, arrayLengthReg, indexVal);
         else if (arrayLengthVal <= indexVal)
            gcPoint = generateInstruction(cg, TR::InstOpCode::trap, node);
         }

      cg->setCanExceptByTrap();
      }

   // Exception edges don't have any live regs
   gcPoint->PPCNeedsGCMap(0);
   }

TR::Register *J9::Power::TreeEvaluator::BNDCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   static bool noReversedTrap = (feGetEnv("TR_noReversedTrap") != NULL);
   TR::Node *secondChild = node->getSecondChild();
   TR::Node *firstChild = node->getFirstChild();
   TR::Register *src1Reg;
   TR::Register *src2Reg = NULL;
   TR::Register *cndReg, *tmpReg = NULL;
   int32_t value;
   TR::RegisterDependencyConditions *conditions;
   TR::LabelSymbol *snippetLabel;
   TR::Instruction *gcPoint;
   bool noTrap = !cg->getHasResumableTrapHandler();
   bool reversed = false;

   if (noTrap)
      {
      cndReg = cg->allocateRegister(TR_CCR);
      conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(4, 4, cg->trMemory());
      }

   if (firstChild->getOpCode().isLoadConst() && firstChild->getInt() >= 0 && firstChild->getInt() <= UPPER_IMMED && firstChild->getRegister() == NULL && !noReversedTrap)
      {
      src2Reg = cg->evaluate(secondChild);
      reversed = true;
      }
   else
      {
      src1Reg = cg->evaluate(firstChild);

      if (secondChild->getOpCode().isLoadConst() && secondChild->getRegister() == NULL)
         {
         value = secondChild->getInt();
         if (value < 0 || value > UPPER_IMMED)
            {
            src2Reg = cg->evaluate(secondChild);
            }
         }
      else
         src2Reg = cg->evaluate(secondChild);
      }

   if (!noTrap)
      {
      if (reversed)
         {
         gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twlgei, node, src2Reg, firstChild->getInt());
         }
      else
         {
         if (src2Reg == NULL)
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twllei, node, src1Reg, value);
         else
            gcPoint = generateSrc2Instruction(cg, TR::InstOpCode::twlle, node, src1Reg, src2Reg);
         }
      }
   else
      {
      if (reversed)
         {
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, cndReg, src2Reg, firstChild->getInt());
         }
      else
         {
         if (src2Reg == NULL)
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, cndReg, src1Reg, value);
         else
            generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, cndReg, src1Reg, src2Reg);
         }

      snippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
      if (snippetLabel == NULL)
         {
         snippetLabel = generateLabelSymbol(cg);
         cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference()));
         }

      tmpReg = cg->allocateRegister();
      // trampoline kills gr11
      TR::addDependency(conditions, tmpReg, TR::RealRegister::gr11, TR_GPR, cg);
      gcPoint = generateDepConditionalBranchInstruction(cg, reversed ? TR::InstOpCode::bgel : TR::InstOpCode::blel, PPCOpProp_BranchUnlikely, node, snippetLabel, cndReg, conditions);
      cg->stopUsingRegister(tmpReg);
      }

   if (noTrap)
      cg->stopUsingRegister(cndReg);

   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
   if (!noTrap)
      cg->setCanExceptByTrap();
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   secondChild->setIsNonNegative(true);
   return (NULL);
   }

TR::Register *J9::Power::TreeEvaluator::ArrayCopyBNDCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Node *firstChild = node->getFirstChild();
   TR::Node *secondChild = node->getSecondChild();
   TR::LabelSymbol *snippetLabel = NULL;
   TR::Instruction *gcPoint;
   bool noTrap = !cg->getHasResumableTrapHandler();
   bool directCase = firstChild->getOpCode().isLoadConst() && secondChild->getOpCode().isLoadConst() && firstChild->getInt() < secondChild->getInt();

   if (directCase || noTrap)
      {
      snippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
      if (snippetLabel == NULL)
         {
         snippetLabel = generateLabelSymbol(cg);
         cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference()));
         }
      }

   // We are checking for firstChild>=secondChild. We don't need to copy registers.

   if (directCase)
      {
      gcPoint = generateLabelInstruction(cg, TR::InstOpCode::bl, node, snippetLabel);
      }
   else if (firstChild->getOpCode().isLoadConst() && firstChild->getInt() >= LOWER_IMMED && firstChild->getInt() <= UPPER_IMMED && firstChild->getRegister() == NULL)
      {
      TR::Register *copyIndexReg = cg->evaluate(secondChild);
      if (noTrap)
         {
         TR::Register *cndReg = cg->allocateRegister(TR_CCR);
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, cndReg, copyIndexReg, firstChild->getInt());
         gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgtl, PPCOpProp_BranchUnlikely, node, snippetLabel, cndReg);
         cg->stopUsingRegister(cndReg);
         }
      else
         {
         gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twgti, node, copyIndexReg, firstChild->getInt());
         cg->setCanExceptByTrap();
         }
      }
   else
      {
      TR::Register *boundReg = cg->evaluate(firstChild);
      if (secondChild->getOpCode().isLoadConst() && secondChild->getInt() >= LOWER_IMMED && secondChild->getInt() <= UPPER_IMMED)
         {
         if (noTrap)
            {
            TR::Register *cndReg = cg->allocateRegister(TR_CCR);
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, cndReg, boundReg, secondChild->getInt());
            gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bltl, PPCOpProp_BranchUnlikely, node, snippetLabel, cndReg);
            cg->stopUsingRegister(cndReg);
            }
         else
            {
            gcPoint = generateSrc1Instruction(cg, TR::InstOpCode::twlti, node, boundReg, secondChild->getInt());
            cg->setCanExceptByTrap();
            }
         }
      else
         {
         TR::Register *copyIndexReg = cg->evaluate(secondChild);
         if (noTrap)
            {
            TR::Register *cndReg = cg->allocateRegister(TR_CCR);
            generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, cndReg, boundReg, copyIndexReg);
            gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bltl, PPCOpProp_BranchUnlikely, node, snippetLabel, cndReg);
            cg->stopUsingRegister(cndReg);
            }
         else
            {
            gcPoint = generateSrc2Instruction(cg, TR::InstOpCode::twlt, node, boundReg, copyIndexReg);
            cg->setCanExceptByTrap();
            }
         }
      }

   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   if (secondChild->getOpCode().isLoadConst() && secondChild->getInt() >= 0)
      firstChild->setIsNonNegative(true);
   return (NULL);
   }

static TR::Instruction* genSpineCheck(TR::CodeGenerator *cg, TR::Node *node, TR::Register *arrayLengthReg, TR::Register *condReg, TR::LabelSymbol *discontiguousArrayLabel)
   {
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, arrayLengthReg, 0);
   return generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, discontiguousArrayLabel, condReg);
   }

static TR::Instruction* genSpineCheck(TR::CodeGenerator *cg, TR::Node *node, TR::Register *baseArrayReg, TR::Register *arrayLengthReg, TR::Register *condReg,
      TR::LabelSymbol *discontiguousArrayLabel)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::MemoryReference *contiguousArraySizeMR = TR::MemoryReference::createWithDisplacement(cg, baseArrayReg, fej9->getOffsetOfContiguousArraySizeField(), 4);
   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, arrayLengthReg, contiguousArraySizeMR);
   return genSpineCheck(cg, node, arrayLengthReg, condReg, discontiguousArrayLabel);
   }

static void genArrayletAccessAddr(TR::CodeGenerator *cg, TR::Node *node, int32_t elementSize,
// Inputs:
      TR::Register *baseArrayReg, TR::Register *indexReg, int32_t indexVal,
      // Outputs:
      TR::Register *arrayletReg, TR::Register *offsetReg, int32_t& offsetVal)
   {
   TR::Compilation* comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR_ASSERT(offsetReg || !indexReg, "Expecting valid offset reg when index reg is passed");

   uintptr_t arrayHeaderSize = TR::Compiler->om.discontiguousArrayHeaderSizeInBytes();
   int32_t spinePointerSize = TR::Compiler->om.sizeofReferenceField();
   int32_t spinePointerSizeShift = spinePointerSize == 8 ? 3 : 2;

   TR::MemoryReference *spineMR;

   // Calculate the spine offset.
   //
   if (indexReg)
      {
      int32_t spineShift = fej9->getArraySpineShift(elementSize);

      // spineOffset = (index >> spineShift) * spinePtrSize
      //             = (index >> spineShift) << spinePtrSizeShift
      //             = (index >> (spineShift - spinePtrSizeShift)) & ~(spinePtrSize - 1)
      // spineOffset += arrayHeaderSize
      //
      TR_ASSERT(spineShift >= spinePointerSizeShift, "Unexpected spine shift value");
      generateShiftRightLogicalImmediate(cg, node, arrayletReg, indexReg, spineShift - spinePointerSizeShift);
      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, arrayletReg, arrayletReg, 0, ~(spinePointerSize - 1));
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, arrayletReg, arrayletReg, arrayHeaderSize);

      spineMR = TR::MemoryReference::createWithIndexReg(cg, baseArrayReg, arrayletReg, spinePointerSize);
      }
   else
      {
      int32_t spineIndex = fej9->getArrayletLeafIndex(indexVal, elementSize);
      int32_t spineDisp32 = spineIndex * spinePointerSize + arrayHeaderSize;

      spineMR = TR::MemoryReference::createWithDisplacement(cg, baseArrayReg, spineDisp32, spinePointerSize);
      }

   // Load the arraylet from the spine.
   //
   generateTrg1MemInstruction(cg, spinePointerSize == 8 ? TR::InstOpCode::ld : TR::InstOpCode::lwz, node, arrayletReg, spineMR);

   // Calculate the arraylet offset.
   //
   if (indexReg)
      {
      int32_t arrayletMask = fej9->getArrayletMask(elementSize);
      int32_t elementSizeShift = CHAR_BIT * sizeof(int32_t) - leadingZeroes(elementSize - 1);

      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, offsetReg, indexReg, elementSizeShift, arrayletMask << elementSizeShift);
      }
   else
      offsetVal = (fej9->getLeafElementIndex(indexVal, elementSize) * elementSize);
   }

static TR::InstOpCode::Mnemonic getLoadOrStoreFromDataType(TR::CodeGenerator *cg, TR::DataType dt, int32_t elementSize, bool isUnsigned, bool returnLoad)
   {
   switch (dt)
      {
   case TR::Int8:
      return returnLoad ? TR::InstOpCode::lbz : TR::InstOpCode::stb;
   case TR::Int16:
      if (returnLoad)
         return isUnsigned ? TR::InstOpCode::lhz : TR::InstOpCode::lha;
      else
         return TR::InstOpCode::sth;
   case TR::Int32:
      if (returnLoad)
         return TR::InstOpCode::lwz;
      else
         return TR::InstOpCode::stw;
   case TR::Int64:
      return returnLoad ? TR::InstOpCode::ld : TR::InstOpCode::std;
   case TR::Float:
      return returnLoad ? TR::InstOpCode::lfs : TR::InstOpCode::stfs;
   case TR::Double:
      return returnLoad ? TR::InstOpCode::lfd : TR::InstOpCode::stfd;
   case TR::Address:
      if (returnLoad)
         return elementSize == 8 ? TR::InstOpCode::ld : TR::InstOpCode::lwz;
      else
         return elementSize == 8 ? TR::InstOpCode::std : TR::InstOpCode::stw;
   default:
      TR_ASSERT(false, "Unexpected array data type");
      return TR::InstOpCode::bad;
      }
   }

static void genDecompressPointer(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *condReg = NULL, bool nullCheck = true)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      generateShiftLeftImmediateLong(cg, node, ptrReg, ptrReg, shiftAmount);
   }

static TR::Register *addConstantToLongWithTempReg(TR::Node * node, TR::Register *srcReg, int64_t value, TR::Register *trgReg, TR::Register *tempReg, TR::CodeGenerator *cg)
   {
   if (!trgReg)
      trgReg = cg->allocateRegister();

   if ((int16_t) value == value)
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, trgReg, srcReg, value);
      }
   // NOTE: the following only works if the second add's immediate is not sign extended
   else if (((int32_t) value == value) && ((value & 0x8000) == 0))
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, trgReg, srcReg, value >> 16);
      if (value & 0xffff)
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, trgReg, trgReg, LO_VALUE(value));
      }
   else
      {
      loadConstant(cg, node, value, tempReg);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, trgReg, srcReg, tempReg);
      }

   return trgReg;
   }

static void genDecompressPointerWithTempReg(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *tempReg, TR::Register *condReg = NULL, bool nullCheck = true)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      generateShiftLeftImmediateLong(cg, node, ptrReg, ptrReg, shiftAmount);
   }

static TR::Register *genDecompressPointerNonNull2Regs(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *targetReg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftLeftImmediateLong(cg, node, targetReg, ptrReg, shiftAmount);
      return targetReg;
      }
   else
      {
      return ptrReg;
      }
   }

static TR::Register *genDecompressPointerNonNull2RegsWithTempReg(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *targetReg, TR::Register *tempReg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftLeftImmediateLong(cg, node, targetReg, ptrReg, shiftAmount);
      return targetReg;
      }
   else
      {
      return ptrReg;
      }
   }

static void genCompressPointerWithTempReg(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *tempReg, TR::Register *condReg = NULL, bool nullCheck = true)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftRightLogicalImmediateLong(cg, node, ptrReg, ptrReg, shiftAmount);
      }
   }

static void genCompressPointer(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *condReg = NULL, bool nullCheck = true)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftRightLogicalImmediateLong(cg, node, ptrReg, ptrReg, shiftAmount);
      }
   }

static TR::Register *genCompressPointerNonNull2RegsWithTempReg(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *targetReg, TR::Register *tempReg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftRightLogicalImmediateLong(cg, node, targetReg, ptrReg, shiftAmount);
      return targetReg;
      }
   else
      return ptrReg;
   }

static TR::Register *genCompressPointerNonNull2Regs(TR::CodeGenerator *cg, TR::Node *node, TR::Register *ptrReg, TR::Register *targetReg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   int32_t shiftAmount = TR::Compiler->om.compressedReferenceShift();

   if (shiftAmount != 0)
      {
      generateShiftRightLogicalImmediateLong(cg, node, targetReg, ptrReg, shiftAmount);
      return targetReg;
      }
   else
      return ptrReg;
   }

// Handles both BNDCHKwithSpineCHK and SpineCHK nodes.
//
TR::Register *J9::Power::TreeEvaluator::BNDCHKwithSpineCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   bool noTrap = !cg->getHasResumableTrapHandler();
   bool needsBoundCheck = node->getOpCodeValue() == TR::BNDCHKwithSpineCHK;
   bool needsBoundCheckOOL;

   TR_PPCScratchRegisterManager *srm = cg->generateScratchRegisterManager();

   TR::Node *loadOrStoreChild = node->getFirstChild();
   TR::Node *baseArrayChild = node->getSecondChild();
   TR::Node *arrayLengthChild;
   TR::Node *indexChild;

   if (needsBoundCheck)
      {
      arrayLengthChild = node->getChild(2);
      indexChild = node->getChild(3);
      }
   else
      indexChild = node->getChild(2);

   TR::Register *baseArrayReg = cg->evaluate(baseArrayChild);
   TR::Register *indexReg;
   TR::Register *loadOrStoreReg;
   TR::Register *arrayLengthReg;

   // If the index is too large to be an immediate load it in a register
   if (!indexChild->getOpCode().isLoadConst() || (indexChild->getInt() > UPPER_IMMED || indexChild->getInt() < 0))
      indexReg = cg->evaluate(indexChild);
   else
      indexReg = NULL;

   // For primitive stores anchored under the check node, we must evaluate the source node
   // before the bound check branch so that its available to the snippet.
   //
   if (loadOrStoreChild->getOpCode().isStore() && !loadOrStoreChild->getRegister())
      {
      TR::Node *valueChild = loadOrStoreChild->getSecondChild();
      cg->evaluate(valueChild);
      }

   // Evaluate any escaping nodes before the OOL branch since they won't be evaluated in the OOL path.
   preEvaluateEscapingNodesForSpineCheck(node, cg);

   // Label to the OOL code that will perform the load/store/agen for discontiguous arrays (and the bound check if needed).
   TR::LabelSymbol *discontiguousArrayLabel = generateLabelSymbol(cg);

   // Label back to main-line that the OOL code will branch to when done.
   TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
   doneLabel->setEndInternalControlFlow();

   TR_PPCOutOfLineCodeSection *discontiguousArrayOOL = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(discontiguousArrayLabel, doneLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(discontiguousArrayOOL);

   TR::Instruction *OOLBranchInstr;

   if (needsBoundCheck)
      {
      TR_ASSERT(arrayLengthChild, "Expecting to have an array length child for BNDCHKwithSpineCHK node");
      TR_ASSERT(
            arrayLengthChild->getOpCode().isConversion() || arrayLengthChild->getOpCodeValue() == TR::iloadi || arrayLengthChild->getOpCodeValue() == TR::iload
                  || arrayLengthChild->getOpCodeValue() == TR::iRegLoad || arrayLengthChild->getOpCode().isLoadConst(),
            "Expecting array length child under BNDCHKwithSpineCHK to be a conversion, iiload, iload, iRegLoad or iconst");

      TR::Register *condReg = srm->findOrCreateScratchRegister(TR_CCR);

      arrayLengthReg = arrayLengthChild->getRegister();

      if (arrayLengthReg)
         {
         OOLBranchInstr = genSpineCheck(cg, node, baseArrayReg, arrayLengthReg, condReg, discontiguousArrayLabel);
         needsBoundCheckOOL = true;
         genBoundCheck(cg, node, indexReg, indexChild->getInt(), arrayLengthReg, arrayLengthChild->getInt(), condReg, noTrap);
         }
      else if (arrayLengthChild->getOpCode().isLoadConst())
         {
         // If the constant arraylength is non-zero then it will pass the spine check (hence its
         // a contiguous array) and the BNDCHK can be done inline with no OOL path.
         //
         // If the constant arraylength is zero then we will always go OOL.
         //
         // TODO: in the future there shouldn't be an OOL path because any valid access must be
         //       on a discontiguous array.
         //
         if (arrayLengthChild->getInt() != 0)
            {
            // The array must be contiguous.
            //

            // If the array length is too large to be an immediate load it in a register for the bound check
            if (arrayLengthChild->getInt() > UPPER_IMMED || arrayLengthChild->getInt() < 0)
               arrayLengthReg = cg->evaluate(arrayLengthChild);

            // Do the bound check first.
            genBoundCheck(cg, node, indexReg, indexChild->getInt(), arrayLengthReg, arrayLengthChild->getInt(), condReg, noTrap);
            needsBoundCheckOOL = false;
            TR::Register *scratchArrayLengthReg = srm->findOrCreateScratchRegister();
            OOLBranchInstr = genSpineCheck(cg, node, baseArrayReg, scratchArrayLengthReg, condReg, discontiguousArrayLabel);
            srm->reclaimScratchRegister(scratchArrayLengthReg);
            }
         else
            {
            // Zero length array or discontiguous array.  Unconditionally branch to the OOL path
            // to find out which.
            //
            OOLBranchInstr = generateLabelInstruction(cg, TR::InstOpCode::b, node, discontiguousArrayLabel);
            needsBoundCheckOOL = true;
            }
         }
      else
         {
         // Load the contiguous array length.
         arrayLengthReg = cg->evaluate(arrayLengthChild);
         // If the array length is 0, this is a discontiguous array and the bound check will be handled OOL.
         OOLBranchInstr = genSpineCheck(cg, node, arrayLengthReg, condReg, discontiguousArrayLabel);
         needsBoundCheckOOL = true;
         // Do the bound check using the contiguous array length.
         genBoundCheck(cg, node, indexReg, indexChild->getInt(), arrayLengthReg, arrayLengthChild->getInt(), condReg, noTrap);
         }

      srm->reclaimScratchRegister(condReg);
      cg->decReferenceCount(arrayLengthChild);
      }
   else
      {
      // Spine check only; load the contiguous length, check for 0, branch OOL if discontiguous.
      needsBoundCheckOOL = false;

      arrayLengthReg = srm->findOrCreateScratchRegister();

      TR::Register *condReg = srm->findOrCreateScratchRegister(TR_CCR);

      OOLBranchInstr = genSpineCheck(cg, node, baseArrayReg, arrayLengthReg, condReg, discontiguousArrayLabel);

      srm->reclaimScratchRegister(arrayLengthReg);
      srm->reclaimScratchRegister(condReg);
      }

   // For reference stores, only evaluate the array element address because the store cannot
   // happen here (it must be done via the array store check).
   //
   // For primitive stores, evaluate them now.
   // For loads, evaluate them now.
   // For address calculations (aladd/aiadd), evaluate them now.
   //
   bool doLoadOrStore;
   bool doLoadDecompress = false;
   bool doAddressComputation;

   if (loadOrStoreChild->getOpCode().isStore() && loadOrStoreChild->getReferenceCount() > 1)
      {
      TR_ASSERT(loadOrStoreChild->getOpCode().isWrtBar(), "Opcode must be wrtbar");
      loadOrStoreReg = cg->evaluate(loadOrStoreChild->getFirstChild());
      cg->decReferenceCount(loadOrStoreChild->getFirstChild());
      doLoadOrStore = false;
      doAddressComputation = true;
      }
   else
      {
      // If it's a store and not commoned then it must be a primitive store.
      // If it's an address load it may need decompression in the OOL path.

      // Top-level check whether a decompression sequence is necessary, because the first child
      // may have been created by a PRE temp.
      //
      if ((loadOrStoreChild->getOpCodeValue() == TR::aload || loadOrStoreChild->getOpCodeValue() == TR::aRegLoad) && node->isSpineCheckWithArrayElementChild() && comp->target().is64Bit()
            && comp->useCompressedPointers())
         {
         doLoadDecompress = true;
         }

      TR::Node *actualLoadOrStoreChild = loadOrStoreChild;
      while (actualLoadOrStoreChild->getOpCode().isConversion() || actualLoadOrStoreChild->containsCompressionSequence())
         {
         if (actualLoadOrStoreChild->containsCompressionSequence())
            doLoadDecompress = true;
         actualLoadOrStoreChild = actualLoadOrStoreChild->getFirstChild();
         }

      doLoadOrStore = actualLoadOrStoreChild->getOpCode().hasSymbolReference()
            && (actualLoadOrStoreChild->getSymbolReference()->getSymbol()->isArrayShadowSymbol()
                  || actualLoadOrStoreChild->getSymbolReference()->getSymbol()->isArrayletShadowSymbol()) && node->isSpineCheckWithArrayElementChild();

      // If the 1st child is not a load/store/aladd/aiadd it's probably a nop (e.g. const) at this point due to commoning
      //
      doAddressComputation = !doLoadOrStore && actualLoadOrStoreChild->getOpCode().isArrayRef() && !node->isSpineCheckWithArrayElementChild();

      if (doLoadOrStore || doAddressComputation || !loadOrStoreChild->getOpCode().isLoadConst())
         loadOrStoreReg = cg->evaluate(loadOrStoreChild);
      else
         loadOrStoreReg = NULL;
      }

   if (noTrap)
      {
      TR::RegisterDependencyConditions *mainlineDeps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 1, cg->trMemory());
      // Trampoline to bound check fail kills gr11
      // TODO: Try to use this as the arraylet reg
      TR::Register *gr11 = cg->allocateRegister();
      mainlineDeps->addPostCondition(gr11, TR::RealRegister::gr11);
      cg->stopUsingRegister(gr11);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel);
      TR::LabelSymbol *doneMainlineLabel = generateLabelSymbol(cg);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneMainlineLabel, mainlineDeps);
      }
   else
      generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel);

   // OOL begin.
   //
   discontiguousArrayOOL->swapInstructionListsWithCompilation();
      {
      TR::Instruction *OOLLabelInstr = generateLabelInstruction(cg, TR::InstOpCode::label, node, discontiguousArrayLabel);
      // XXX: Temporary fix, OOL instruction stream does not pick up live locals or monitors correctly.
      TR_ASSERT(!OOLLabelInstr->getLiveLocals() && !OOLLabelInstr->getLiveMonitors(), "Expecting first OOL instruction to not have live locals/monitors info");
      OOLLabelInstr->setLiveLocals(OOLBranchInstr->getLiveLocals());
      OOLLabelInstr->setLiveMonitors(OOLBranchInstr->getLiveMonitors());

      if (needsBoundCheckOOL)
         {
         TR_ASSERT(needsBoundCheck, "Inconsistent state, needs bound check OOL but doesn't need bound check");

         TR::MemoryReference *discontiguousArraySizeMR = TR::MemoryReference::createWithDisplacement(cg, baseArrayReg, fej9->getOffsetOfDiscontiguousArraySizeField(), 4);
         TR::Register *arrayLengthScratchReg = srm->findOrCreateScratchRegister();

         generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, arrayLengthScratchReg, discontiguousArraySizeMR);

         TR::Register *condReg = srm->findOrCreateScratchRegister(TR_CCR);

         // Do the bound check using the discontiguous array length.
         genBoundCheck(cg, node, indexReg, indexChild->getInt(), arrayLengthScratchReg, arrayLengthChild->getInt(), condReg, noTrap);

         srm->reclaimScratchRegister(condReg);
         srm->reclaimScratchRegister(arrayLengthScratchReg);
         }

      TR_ASSERT(!(doLoadOrStore && doAddressComputation), "Unexpected condition");

      TR::Register *arrayletReg;
      TR::DataType dt = loadOrStoreChild->getDataType();

      if (doLoadOrStore || doAddressComputation)
         {
         arrayletReg = doAddressComputation ? loadOrStoreReg : /* Needs to be in !gr0 in this case */cg->allocateRegister();

         // Generate the base+offset address pair into the arraylet.
         //
         int32_t elementSize = dt == TR::Address ? TR::Compiler->om.sizeofReferenceField() : TR::Symbol::convertTypeToSize(dt);
         TR::Register *arrayletOffsetReg;
         int32_t arrayletOffsetVal;

         if (indexReg)
            arrayletOffsetReg = srm->findOrCreateScratchRegister();

         genArrayletAccessAddr(cg, node, elementSize, baseArrayReg, indexReg, indexChild->getInt(), arrayletReg, arrayletOffsetReg, arrayletOffsetVal);

         // Decompress the arraylet pointer if necessary.
         genDecompressPointer(cg, node, arrayletReg);

         if (doLoadOrStore)
            {
            // Generate the load or store.
            //
            if (loadOrStoreChild->getOpCode().isStore())
               {
               bool isUnsigned = loadOrStoreChild->getOpCode().isUnsigned();
               TR::InstOpCode::Mnemonic storeOp = getLoadOrStoreFromDataType(cg, dt, elementSize, isUnsigned, false);

               if (storeOp == TR::InstOpCode::std && comp->target().is32Bit())
                  {
                  // Store using consecutive stws.
                  TR_ASSERT(elementSize == 8 && loadOrStoreChild->getSecondChild()->getRegister()->getRegisterPair(), "Expecting 64-bit store value to be in a register pair");
                  if (indexReg)
                     {
                     TR::MemoryReference *arrayletMR = TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, 4);
                     generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, arrayletMR, loadOrStoreChild->getSecondChild()->getRegister()->getHighOrder());

                     generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, arrayletOffsetReg, arrayletOffsetReg, 4);

                     TR::MemoryReference *arrayletMR2 = TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, 4);
                     generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, arrayletMR2, loadOrStoreChild->getSecondChild()->getRegister()->getLowOrder());
                     }
                  else
                     {
                     TR::MemoryReference *arrayletMR = TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal, 4);
                     TR::MemoryReference *arrayletMR2 = TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal + 4, 4);
                     generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, arrayletMR, loadOrStoreChild->getSecondChild()->getRegister()->getHighOrder());
                     generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, arrayletMR2, loadOrStoreChild->getSecondChild()->getRegister()->getLowOrder());
                     }
                  }
               else
                  {
                  TR::MemoryReference *arrayletMR =
                        indexReg ? TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, elementSize) :
                                   TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal, elementSize);
                  generateMemSrc1Instruction(cg, storeOp, node, arrayletMR, loadOrStoreChild->getSecondChild()->getRegister());
                  }
               }
            else
               {
               TR_ASSERT(loadOrStoreChild->getOpCode().isConversion() || loadOrStoreChild->getOpCode().isLoad(), "Unexpected op");

               bool isUnsigned = loadOrStoreChild->getOpCode().isUnsigned();
               TR::InstOpCode::Mnemonic loadOp = getLoadOrStoreFromDataType(cg, dt, elementSize, isUnsigned, true);

               if (loadOp == TR::InstOpCode::ld && comp->target().is32Bit())
                  {
                  // Load using consecutive lwzs.
                  TR_ASSERT(elementSize == 8 && loadOrStoreReg->getRegisterPair(), "Expecting 64-bit load value to be in a register pair");
                  if (indexReg)
                     {
                     TR::MemoryReference *arrayletMR = TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, 4);
                     generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, loadOrStoreReg->getHighOrder(), arrayletMR);

                     generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, arrayletOffsetReg, arrayletOffsetReg, 4);

                     TR::MemoryReference *arrayletMR2 = TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, 4);
                     generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, loadOrStoreReg->getLowOrder(), arrayletMR2);
                     }
                  else
                     {
                     TR::MemoryReference *arrayletMR = TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal, 4);
                     TR::MemoryReference *arrayletMR2 = TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal + 4, 4);
                     generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, loadOrStoreReg->getHighOrder(), arrayletMR);
                     generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, loadOrStoreReg->getLowOrder(), arrayletMR2);
                     }
                  }
               else
                  {
                  TR::MemoryReference *arrayletMR =
                        indexReg ? TR::MemoryReference::createWithIndexReg(cg, arrayletReg, arrayletOffsetReg, elementSize) :
                                   TR::MemoryReference::createWithDisplacement(cg, arrayletReg, arrayletOffsetVal, elementSize);
                  generateTrg1MemInstruction(cg, loadOp, node, loadOrStoreReg, arrayletMR);
                  }

               if (doLoadDecompress)
                  {
                  TR_ASSERT(dt == TR::Address, "Expecting loads with decompression trees to have data type TR::Address");
                  genDecompressPointer(cg, node, loadOrStoreReg);
                  }
               }

            cg->stopUsingRegister(arrayletReg);
            }
         else
            {
            if (indexReg)
               generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, loadOrStoreReg, loadOrStoreReg, arrayletOffsetReg);
            else
               addConstantToInteger(node, loadOrStoreReg, loadOrStoreReg, arrayletOffsetVal, cg);
            }

         if (indexReg)
            srm->reclaimScratchRegister(arrayletOffsetReg);
         }

      const uint32_t numOOLDeps = 1 + (doLoadOrStore ? 1 : 0) + (needsBoundCheck && arrayLengthReg ? 1 : 0) + (loadOrStoreReg ? (loadOrStoreReg->getRegisterPair() ? 2 : 1) : 0)
            + (indexReg ? 1 : 0) + srm->numAvailableRegisters();
      TR::RegisterDependencyConditions *OOLDeps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, numOOLDeps, cg->trMemory());
      OOLDeps->addPostCondition(baseArrayReg, TR::RealRegister::NoReg);
      TR_ASSERT(OOLDeps->getPostConditions()->getRegisterDependency(0)->getRegister() == baseArrayReg, "Unexpected register");
      OOLDeps->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
      if (doLoadOrStore)
         {
         OOLDeps->addPostCondition(arrayletReg, TR::RealRegister::NoReg);
         TR_ASSERT(OOLDeps->getPostConditions()->getRegisterDependency(1)->getRegister() == arrayletReg, "Unexpected register");
         OOLDeps->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0();
         }
      if (indexReg)
         OOLDeps->addPostCondition(indexReg, TR::RealRegister::NoReg);
      if (loadOrStoreReg)
         {
         if (loadOrStoreReg->getRegisterPair())
            {
            TR_ASSERT(dt == TR::Int64 && comp->target().is32Bit(), "Unexpected register pair");
            OOLDeps->addPostCondition(loadOrStoreReg->getHighOrder(), TR::RealRegister::NoReg);
            OOLDeps->addPostCondition(loadOrStoreReg->getLowOrder(), TR::RealRegister::NoReg);
            }
         else
            OOLDeps->addPostCondition(loadOrStoreReg, TR::RealRegister::NoReg);
         }
      if (needsBoundCheck && arrayLengthReg)
         OOLDeps->addPostCondition(arrayLengthReg, TR::RealRegister::NoReg);
      srm->addScratchRegistersToDependencyList(OOLDeps);

      srm->stopUsingRegisters();

      TR::LabelSymbol *doneOOLLabel = generateLabelSymbol(cg);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneOOLLabel, OOLDeps);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
      }
   discontiguousArrayOOL->swapInstructionListsWithCompilation();
   //
   // OOL end.

   cg->decReferenceCount(loadOrStoreChild);
   cg->decReferenceCount(baseArrayChild);
   cg->decReferenceCount(indexChild);

   return NULL;
   }

static void VMoutlinedHelperArrayStoreCHKEvaluator(TR::Node *node, TR::Register *srcReg, TR::Register *dstReg, bool srcIsNonNull, TR::CodeGenerator *cg)
   {
   TR::Compilation * comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::CodeCache *codeCache = cg->getCodeCache();
   TR::LabelSymbol *doneArrayStoreCHKLabel = generateLabelSymbol(cg);
   TR::Register *rootClassReg = cg->allocateRegister();
   TR::Register *scratchReg = cg->allocateRegister();
   TR::Register *srcNullCondReg;

   TR_PPCScratchRegisterDependencyConditions deps;

   if (!srcIsNonNull)
      {
      srcNullCondReg = cg->allocateRegister(TR_CCR);
      generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, srcNullCondReg, srcReg, NULLVALUE);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneArrayStoreCHKLabel, srcNullCondReg);
      }

   deps.addDependency(cg, dstReg, TR::RealRegister::gr3);
   deps.addDependency(cg, srcReg, TR::RealRegister::gr4);
   // Clobbered by the helper
   deps.addDependency(cg, NULL, TR::RealRegister::gr5);
   deps.addDependency(cg, NULL, TR::RealRegister::gr6);
   deps.addDependency(cg, scratchReg, TR::RealRegister::gr7);
   deps.addDependency(cg, rootClassReg, TR::RealRegister::gr11);

   TR_OpaqueClassBlock *rootClass = fej9->getSystemClassFromClassName("java/lang/Object", 16);
   if (cg->wantToPatchClassPointer(rootClass, node))
      loadAddressConstantInSnippet(cg, node, (intptr_t) rootClass, rootClassReg, scratchReg,TR::InstOpCode::Op_load, false, NULL);
   else
      loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t) rootClass, rootClassReg);

   TR_CCPreLoadedCode helper = TR_arrayStoreCHK;
   uintptr_t helperAddr = (uintptr_t) codeCache->getCCPreLoadedCodeAddress(helper, cg);
   TR::SymbolReference *symRef = comp->getSymRefTab()->findOrCreatePerCodeCacheHelperSymbolRef(helper, helperAddr);

   TR::Instruction *gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, helperAddr, new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 0, cg->trMemory()),
         symRef);
   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneArrayStoreCHKLabel, TR_PPCScratchRegisterDependencyConditions::createDependencyConditions(cg, NULL, &deps));

   cg->stopUsingRegister(rootClassReg);
   cg->stopUsingRegister(scratchReg);

   if (!srcIsNonNull)
      cg->stopUsingRegister(srcNullCondReg);
   }

TR::Register *outlinedHelperArrayStoreCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Node *srcNode = node->getFirstChild()->getSecondChild();
   TR::Node *dstNode = node->getFirstChild()->getThirdChild();
   TR::Register *srcReg = cg->evaluate(srcNode);
   TR::Register *dstReg = cg->evaluate(dstNode);
   TR::Compilation* comp = cg->comp();

   if (!srcNode->isNull())
      {
      VMoutlinedHelperArrayStoreCHKEvaluator(node, srcReg, dstReg, srcNode->isNonNull(), cg);
      }

   cg->evaluate(node->getFirstChild());
   cg->decReferenceCount(node->getFirstChild());

   return NULL;
   }

TR::Instruction *J9::Power::TreeEvaluator::generateVFTMaskInstruction(TR::CodeGenerator *cg, TR::Node *node, TR::Register *dstReg, TR::Register *srcReg, TR::Instruction *preced)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   uintptr_t mask = TR::Compiler->om.maskOfObjectVftField();
   bool isCompressed = TR::Compiler->om.compressObjectReferences();
   if (~mask == 0)
      {
      // no mask instruction required
      return preced;
      }
   else
      {
      TR::InstOpCode::Mnemonic op = (cg->comp()->target().is64Bit() && !isCompressed) ? TR::InstOpCode::rldicr : TR::InstOpCode::rlwinm;
      return generateTrg1Src1Imm2Instruction(cg, op, node, dstReg, srcReg, 0, mask, preced);
      }
   }

TR::Instruction *J9::Power::TreeEvaluator::generateVFTMaskInstruction(TR::CodeGenerator *cg, TR::Node *node, TR::Register *reg, TR::Instruction *preced)
   {
   return TR::TreeEvaluator::generateVFTMaskInstruction(cg, node, reg, reg, preced);
   }

static TR::Instruction *genTestIsSuper(TR::Node *node, TR::Register *objClassReg, TR::Register *castClassReg, TR::Register *crReg, TR::Register *scratch1Reg,
      TR::Register *scratch2Reg, int32_t castClassDepth, TR::LabelSymbol *failLabel, TR::Instruction *cursor, TR::CodeGenerator *cg, bool depthInReg2 = false)
   {
   TR::Compilation *comp = cg->comp();
   int32_t superClassOffset = castClassDepth * TR::Compiler->om.sizeofReferenceAddress();
   bool outOfBound = (!depthInReg2 && (superClassOffset > UPPER_IMMED || superClassOffset < LOWER_IMMED)) ? true : false;
#ifdef OMR_GC_COMPRESSED_POINTERS
   // objClassReg contains the class offset so we may need to generate code
   // to convert from class offset to real J9Class pointer
#endif
   cursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, scratch1Reg,
         TR::MemoryReference::createWithDisplacement(cg, objClassReg, offsetof(J9Class, classDepthAndFlags), TR::Compiler->om.sizeofReferenceAddress()), cursor);
   if (outOfBound)
      {
      cursor = loadConstant(cg, node, castClassDepth, scratch2Reg, cursor);
      }
   cursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, scratch1Reg, scratch1Reg, 0, J9AccClassDepthMask, cursor);

   if (outOfBound || depthInReg2)
      {
      cursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, crReg, scratch1Reg, scratch2Reg, cursor);
      }
   else
      {
      cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, crReg, scratch1Reg, castClassDepth, cursor);
      }
   cursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, failLabel, crReg, cursor);

   if (outOfBound || depthInReg2)
      {
      if (comp->target().is64Bit())
         cursor = generateShiftLeftImmediateLong(cg, node, scratch2Reg, scratch2Reg, 3, cursor);
      else
         cursor = generateShiftLeftImmediate(cg, node, scratch2Reg, scratch2Reg, 2, cursor);
      }
#ifdef OMR_GC_COMPRESSED_POINTERS
   // objClassReg contains the class offset so we may need to generate code
   // to convert from class offset to real J9Class pointer
#endif
   cursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, scratch1Reg,
         TR::MemoryReference::createWithDisplacement(cg, objClassReg, offsetof(J9Class, superclasses), TR::Compiler->om.sizeofReferenceAddress()), cursor);

   if (outOfBound || depthInReg2)
      {
      cursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_loadx, node, scratch1Reg, TR::MemoryReference::createWithIndexReg(cg, scratch1Reg, scratch2Reg, TR::Compiler->om.sizeofReferenceAddress()),
            cursor);
      }
   else
      {
      cursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, scratch1Reg,
            TR::MemoryReference::createWithDisplacement(cg, scratch1Reg, superClassOffset, TR::Compiler->om.sizeofReferenceAddress()), cursor);
      }
#ifdef OMR_GC_COMPRESSED_POINTERS
   // castClassReg has a class offset and scratch1Reg contains a J9Class pointer
   // May need to convert the J9Class pointer to a class offset
#endif
   cursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmp, node, crReg, scratch1Reg, castClassReg, cursor);
   return (cursor);
   }

static void VMarrayStoreCHKEvaluator(TR::Node *node, TR::Register *src, TR::Register *dst, TR::Register *t1Reg, TR::Register *t2Reg, TR::Register *t3Reg, TR::Register *t4Reg,
      TR::Register *cndReg, TR::LabelSymbol *toWB, TR::LabelSymbol *fLabel, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());

   generateLoadJ9Class(node, t1Reg, dst, cg);
   generateLoadJ9Class(node, t2Reg, src, cg);

   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, t1Reg,
         TR::MemoryReference::createWithDisplacement(cg, t1Reg, (int32_t)offsetof(J9ArrayClass, componentType), TR::Compiler->om.sizeofReferenceAddress()));

   if (!comp->compileRelocatableCode() || comp->getOption(TR_UseSymbolValidationManager))
      {
      TR_OpaqueClassBlock *rootClass = fej9->getSystemClassFromClassName("java/lang/Object", 16);

      if (cg->wantToPatchClassPointer(rootClass, node))
         {
         loadAddressConstantInSnippet(cg, node, (intptr_t) rootClass, t3Reg, t4Reg, TR::InstOpCode::Op_load, false, NULL);
         }
      else if (comp->compileRelocatableCode())
         {
         TR::StaticSymbol *sym = TR::StaticSymbol::create(comp->trHeapMemory(), TR::Address);
         sym->setStaticAddress(rootClass);
         sym->setClassObject();

         loadAddressConstant(cg, true, node, (intptr_t) new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), sym), t3Reg, NULL, false, TR_ClassAddress);
         }
      else
         {
         loadAddressConstant(cg, false, node, (intptr_t) rootClass, t3Reg);
         }

      generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, cndReg, t1Reg, t3Reg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, toWB, cndReg);
      }

   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, t4Reg,
         TR::MemoryReference::createWithDisplacement(cg, t2Reg, (int32_t) offsetof(J9Class, castClassCache), TR::Compiler->om.sizeofReferenceAddress()));

   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, t1Reg, t2Reg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, toWB, cndReg);

   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, t1Reg, t4Reg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, toWB, cndReg);

   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, t1Reg, t3Reg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, toWB, cndReg);

   if ((!comp->getOption(TR_DisableArrayStoreCheckOpts)) && node->getArrayComponentClassInNode())
      {
      TR_OpaqueClassBlock *castClass = (TR_OpaqueClassBlock *) node->getArrayComponentClassInNode();

      if (cg->wantToPatchClassPointer(castClass, node))
         loadAddressConstantInSnippet(cg, node, (intptr_t) castClass, t3Reg, t4Reg,TR::InstOpCode::Op_load, false, NULL);
      else
         loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t) castClass, t3Reg);

      generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, t1Reg, t3Reg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, toWB, cndReg);
      }

#ifdef OMR_GC_COMPRESSED_POINTERS
   // For the following two instructions
   // we may need to convert the class offset from t1Reg into J9Class
#endif
   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, t3Reg, TR::MemoryReference::createWithDisplacement(cg, t1Reg, (int32_t) offsetof(J9Class, romClass), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, t4Reg,
         TR::MemoryReference::createWithDisplacement(cg, t1Reg, (int32_t) offsetof(J9Class, classDepthAndFlags), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, t3Reg, TR::MemoryReference::createWithDisplacement(cg, t3Reg, (int32_t) offsetof(J9ROMClass, modifiers), 4));
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, t4Reg, t4Reg, 0, J9AccClassDepthMask);
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, t3Reg, t3Reg, 31, 0xFFFFFFFF);
   TR_ASSERT(J9AccClassArray == (1 << 16) && J9AccInterface == (1 << 9), "Verify code sequence is still right ...");
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, t3Reg, t3Reg, cndReg, (J9AccClassArray | J9AccInterface) >> 1);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, fLabel, cndReg);
   genTestIsSuper(node, t2Reg, t1Reg, cndReg, t3Reg, t4Reg, 0, fLabel, cg->getAppendInstruction(), cg, true);

   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, fLabel, cndReg);
   }

TR::Register *J9::Power::TreeEvaluator::ArrayStoreCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   static bool oldArrayStoreCHK = feGetEnv("TR_newArrayStoreCheck") == NULL;
   if (!comp->compileRelocatableCode() && !comp->getOptions()->realTimeGC() && !comp->useCompressedPointers() && !oldArrayStoreCHK)
      return outlinedHelperArrayStoreCHKEvaluator(node, cg);

   // Note: we take advantage of the register conventions of the helpers by limiting register usages on
   //       the fast-path (most likely 4 registers; at most, 6 registers)

   TR::Node * firstChild = node->getFirstChild();

   TR::Node *sourceChild = firstChild->getSecondChild();
   TR::Node *destinationChild = firstChild->getChild(2);

   if (comp->useCompressedPointers() && firstChild->getOpCode().isIndirect())
      {
      while ((sourceChild->getNumChildren() > 0) && (sourceChild->getOpCodeValue() != TR::a2l))
         sourceChild = sourceChild->getFirstChild();
      if (sourceChild->getOpCodeValue() == TR::a2l)
         sourceChild = sourceChild->getFirstChild();
      }

   // Since ArrayStoreCHK doesn't have the shape of the corresponding helper call we have to create this tree
   // so we can have it evaluated out of line
   TR::Node *helperCallNode = TR::Node::createWithSymRef(node, TR::call, 2, node->getSymbolReference());
   helperCallNode->setAndIncChild(0, sourceChild);
   helperCallNode->setAndIncChild(1, destinationChild);
   if (comp->getOption(TR_TraceCG))
      {
      traceMsg(comp, "%s: Creating and evaluating the following tree to generate the necessary helper call for this node\n", node->getOpCode().getName());
      cg->getDebug()->print(comp->getOutFile(), helperCallNode);
      }

   TR::Register *srcReg, *dstReg;
   TR::Register *condReg, *temp1Reg, *temp2Reg, *temp3Reg, *temp4Reg;
   TR::LabelSymbol *wbLabel, *startLabel;
   TR::RegisterDependencyConditions *conditions;

   wbLabel = generateLabelSymbol(cg);

   dstReg = cg->evaluate(destinationChild);
   srcReg = cg->evaluate(sourceChild);

   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(7, 7, cg->trMemory());
   temp1Reg = cg->allocateRegister();
   temp2Reg = cg->allocateRegister();
   temp3Reg = cg->allocateRegister();
   temp4Reg = cg->allocateRegister();
   condReg = cg->allocateRegister(TR_CCR);

   TR::addDependency(conditions, dstReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, srcReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, temp1Reg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, temp4Reg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
   TR::addDependency(conditions, condReg, TR::RealRegister::cr0, TR_CCR, cg);

   //--Generate Test to see if 'sourceRegister' is NULL
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, srcReg, NULLVALUE);
   //--Generate Jump past ArrayStoreCHKEvaluator
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, wbLabel, condReg);

   TR::LabelSymbol *helperCall = generateLabelSymbol(cg);
   VMarrayStoreCHKEvaluator(node, srcReg, dstReg, temp1Reg, temp2Reg, temp3Reg, temp4Reg, condReg, wbLabel, helperCall, cg);

   TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(helperCallNode, TR::call, NULL, helperCall, wbLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);
   cg->decReferenceCount(helperCallNode->getFirstChild());
   cg->decReferenceCount(helperCallNode->getSecondChild());

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, wbLabel, conditions);
   cg->evaluate(firstChild);

   cg->decReferenceCount(firstChild);

   cg->stopUsingRegister(temp1Reg);
   cg->stopUsingRegister(temp2Reg);
   cg->stopUsingRegister(temp3Reg);
   cg->stopUsingRegister(temp4Reg);
   cg->stopUsingRegister(condReg);

   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::ArrayCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::VMarrayCheckEvaluator(node, cg);
   }

TR::Register *J9::Power::TreeEvaluator::conditionalHelperEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // There are two points which have to be considered for this evaluator:
   //  1) This conditional tree is of a special form which is lowered just
   //     before instruction selection. This means the conditional is invisible
   //     to the control flow analysis. As such, we have to treat it just as
   //     part of a basic block;
   //  2) The two opCodes using this evaluator are intended as quick JVMPI
   //     probes. They should not impact the normal code (in terms of performance).
   //     As such, their call evaluation will be specially treated not to go
   //     through the normal linkage. OTI will preserve all the registers as
   //     needed.

   TR::RegisterDependencyConditions *conditions;
   TR::Register *cndReg, *jumpReg, *valReg;
   TR::Node *testNode = node->getFirstChild(), *callNode = node->getSecondChild();
   TR::Node *firstChild = testNode->getFirstChild(), *secondChild = testNode->getSecondChild();
   int32_t value, i, numArgs = callNode->getNumChildren();

   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(4, 4, cg->trMemory());

   TR_ASSERT(numArgs <= 2, "Unexpected number of arguments for helper.");

   // Helper arguments are in reversed order of the private linkage
   // Argument registers are not needed to be split since the helper will
   // preserve all of them.
   int32_t iArgIndex = 0, fArgIndex = 0;
   TR::Linkage *linkage = cg->createLinkage(TR_Private);
   for (i = numArgs - 1; i >= 0; i--)
      {
      TR::Register *argReg = cg->evaluate(callNode->getChild(i));
      TR::addDependency(conditions, argReg, (argReg->getKind() == TR_GPR) ? // Didn't consider Long here
            linkage->getProperties().getIntegerArgumentRegister(iArgIndex++) : linkage->getProperties().getFloatArgumentRegister(fArgIndex++), argReg->getKind(), cg);
      }

   cndReg = cg->allocateRegister(TR_CCR);
   jumpReg = cg->evaluate(firstChild);
   TR::addDependency(conditions, cndReg, TR::RealRegister::NoReg, TR_CCR, cg);
   TR::addDependency(conditions, jumpReg, TR::RealRegister::gr11, TR_GPR, cg);

   if (secondChild->getOpCode().isLoadConst() && (value = secondChild->getInt()) <= UPPER_IMMED && value >= LOWER_IMMED)
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, testNode, cndReg, jumpReg, value);
      }
   else
      {
      valReg = cg->evaluate(secondChild);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, testNode, cndReg, jumpReg, valReg);
      }

   TR::Instruction *gcPoint;
   TR::LabelSymbol *snippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, node->getSymbolReference());
   if (snippetLabel == NULL)
      {
      snippetLabel = generateLabelSymbol(cg);
      cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference()));
      }
   gcPoint = generateDepConditionalBranchInstruction(cg, testNode->getOpCodeValue() == TR::icmpeq ? TR::InstOpCode::beql : TR::InstOpCode::bnel, node, snippetLabel, cndReg, conditions);
   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);

   cg->stopUsingRegister(cndReg);
   for (i = numArgs - 1; i >= 0; i--)
      cg->decReferenceCount(callNode->getChild(i));
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   cg->decReferenceCount(testNode);
   cg->decReferenceCount(callNode);
   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::flushEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::ILOpCodes opCode = node->getOpCodeValue();

   if (opCode == TR::allocationFence)
      {
      if (!node->canOmitSync())
         generateInstruction(cg, TR::InstOpCode::lwsync, node);
      }
   else
      {
      if (opCode == TR::loadFence)
         {
         if (cg->comp()->target().cpu.is(OMR_PROCESSOR_PPC_P7))
            generateInstruction(cg, TR::InstOpCode::lwsync, node);
         else
            generateInstruction(cg, TR::InstOpCode::isync, node);
         }
      else if (opCode == TR::storeFence)
         generateInstruction(cg, TR::InstOpCode::lwsync, node);
      else if (opCode == TR::fullFence)
         {
         if (node->canOmitSync())
            generateLabelInstruction(cg, TR::InstOpCode::label, node, generateLabelSymbol(cg));
         else
            generateInstruction(cg, TR::InstOpCode::sync, node);
         }
      }

   return NULL;
   }

static
void genCheckCastTransitionToNextSequence(TR::Node *node, J9::TreeEvaluator::InstanceOfOrCheckCastSequences *iter, TR::LabelSymbol *nextSequenceLabel, TR::LabelSymbol *trueLabel, TR::Register *condReg, TR::CodeGenerator *cg)
   {
   J9::TreeEvaluator::InstanceOfOrCheckCastSequences current = *iter++;

   if (current == J9::TreeEvaluator::EvaluateCastClass || current == J9::TreeEvaluator::LoadObjectClass)
      return;
   if (current == J9::TreeEvaluator::NullTest && node->getOpCodeValue() == TR::checkcastAndNULLCHK)
      return;

   J9::TreeEvaluator::InstanceOfOrCheckCastSequences next = *iter;

   while (next == J9::TreeEvaluator::EvaluateCastClass || next == J9::TreeEvaluator::LoadObjectClass)
      next = *++iter;

   bool nextSequenceIsOutOfLine = next == J9::TreeEvaluator::HelperCall || next == J9::TreeEvaluator::GoToFalse;

   if (nextSequenceIsOutOfLine)
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, nextSequenceLabel, condReg);
   else
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, trueLabel, condReg);
   }

static
void genInstanceOfTransitionToNextSequence(TR::Node *node, J9::TreeEvaluator::InstanceOfOrCheckCastSequences *iter, TR::LabelSymbol *nextSequenceLabel, TR::LabelSymbol *doneLabel, TR::Register *condReg, TR::Register *resultReg, bool resultRegInitialValue, TR::LabelSymbol *oppositeResultLabel, bool profiledClassIsInstanceOf, TR::CodeGenerator *cg)
   {
   J9::TreeEvaluator::InstanceOfOrCheckCastSequences current = *iter++;

   if (current == J9::TreeEvaluator::EvaluateCastClass || current == J9::TreeEvaluator::LoadObjectClass)
      return;
   if (current == J9::TreeEvaluator::NullTest && node->getOpCodeValue() == TR::checkcastAndNULLCHK)
      return;
   if (current == J9::TreeEvaluator::GoToFalse)
      return;

   J9::TreeEvaluator::InstanceOfOrCheckCastSequences next = *iter;

   while (next == J9::TreeEvaluator::EvaluateCastClass || next == J9::TreeEvaluator::LoadObjectClass)
      next = *++iter;

   if (next == J9::TreeEvaluator::GoToFalse && resultRegInitialValue == false)
      nextSequenceLabel = doneLabel;

   bool nextSequenceIsOutOfLine = next == J9::TreeEvaluator::HelperCall;

   if (current == J9::TreeEvaluator::NullTest)
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, resultRegInitialValue == false ? doneLabel : oppositeResultLabel, condReg);
      }
   else if (current == J9::TreeEvaluator::CastClassCacheTest)
      {
      if (resultRegInitialValue == true)
         {
         if (nextSequenceIsOutOfLine)
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneLabel, condReg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, nextSequenceLabel, condReg);
            // Fall through to oppositeResultLabel
            }
         else
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneLabel, condReg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, oppositeResultLabel, condReg);
            // Fall through to nextSequenceLabel
            }
         }
      else
         {
         if (nextSequenceIsOutOfLine)
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, nextSequenceLabel, condReg);
            // Fall through to oppositeResultLabel
            }
         else
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, oppositeResultLabel, condReg);
            // Fall through to nextSequenceLabel
            }
         }
      }
   else if (current == J9::TreeEvaluator::ProfiledClassTest)
      {
      if (resultRegInitialValue == true)
         {
         if (profiledClassIsInstanceOf)
            {
            if (nextSequenceIsOutOfLine)
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
               generateLabelInstruction(cg, TR::InstOpCode::b, node, nextSequenceLabel);
               }
            else
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
               }
            }
         else
            {
            if (nextSequenceIsOutOfLine)
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, nextSequenceLabel, condReg);
               // Fall through to oppositeResultLabel
               }
            else
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, oppositeResultLabel, condReg);
               }
            }
         }
      else
         {
         if (profiledClassIsInstanceOf)
            {
            if (nextSequenceIsOutOfLine)
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, nextSequenceLabel, condReg);
               // Fall through to oppositeResultLabel
               }
            else
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, oppositeResultLabel, condReg);
               }
            }
         else
            {
            if (nextSequenceIsOutOfLine)
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
               generateLabelInstruction(cg, TR::InstOpCode::b, node, nextSequenceLabel);
               }
            else
               {
               generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg);
               }
            }
         }
      }
   else
      {
      if (nextSequenceIsOutOfLine)
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, nextSequenceLabel, condReg);
      else
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, resultRegInitialValue == true ? doneLabel : oppositeResultLabel, condReg);
      }
   }

static
void genInstanceOfOrCheckCastClassEqualityTest(TR::Node *node, TR::Register *condReg, TR::Register *instanceClassReg, TR::Register *castClassReg, TR::CodeGenerator *cg)
   {
   generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, condReg, instanceClassReg, castClassReg);

   // At this point condReg[eq] will be set if the cast class matches the instance class. Caller is responsible for acting on the result.
   }

static
void genInstanceOfOrCheckCastSuperClassTest(TR::Node *node, TR::Register *condReg, TR::Register *instanceClassReg, TR::Register *castClassReg, int32_t castClassDepth, TR::LabelSymbol *falseLabel, TR_PPCScratchRegisterManager *srm, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();

   // Compare the instance class depth to the cast class depth. If the instance class depth is less than or equal to
   // to the cast class depth then the cast class cannot be a superclass of the instance class.
   //
   TR::Register *instanceClassDepthReg = srm->findOrCreateScratchRegister();
   TR::Register *castClassDepthReg = NULL;
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, instanceClassDepthReg,
                              TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9Class, classDepthAndFlags), TR::Compiler->om.sizeofReferenceAddress()));
   static_assert(J9AccClassDepthMask < UINT_MAX, "Class depth is not containable in 32 bits");
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, instanceClassDepthReg, instanceClassDepthReg, 0, J9AccClassDepthMask);
   if (castClassDepth <= UPPER_IMMED && castClassDepth >= LOWER_IMMED)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, instanceClassDepthReg, castClassDepth);
   else
      {
      castClassDepthReg = srm->findOrCreateScratchRegister();
      loadConstant(cg, node, castClassDepth, castClassDepthReg);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, condReg, instanceClassDepthReg, castClassDepthReg);
      }
   srm->reclaimScratchRegister(instanceClassDepthReg);

   // At this point condReg[gt] will be set if the instance class depth is greater than the cast class depth.
   //
   generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, falseLabel, condReg);

   // Load the superclasses array of the instance class and check if the superclass that appears at the depth of the cast class is in fact the cast class.
   // If not, the instance class and cast class are not in the same hierarchy.
   //
   TR::Register *instanceClassSuperClassesArrayReg = srm->findOrCreateScratchRegister();
   TR::Register *instanceClassSuperClassReg = srm->findOrCreateScratchRegister();

   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, instanceClassSuperClassesArrayReg,
                              TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9Class, superclasses), TR::Compiler->om.sizeofReferenceAddress()));

   int32_t castClassDepthOffset = castClassDepth * TR::Compiler->om.sizeofReferenceAddress();
   if (castClassDepthOffset <= UPPER_IMMED && castClassDepthOffset >= LOWER_IMMED)
      {
      generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, instanceClassSuperClassReg,
                                 TR::MemoryReference::createWithDisplacement(cg, instanceClassSuperClassesArrayReg, castClassDepthOffset, TR::Compiler->om.sizeofReferenceAddress()));
      }
   else
      {
      if (!castClassDepthReg)
         {
         castClassDepthReg = srm->findOrCreateScratchRegister();
         if (0x00008000 == HI_VALUE(castClassDepthOffset))
            {
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, castClassDepthReg, instanceClassSuperClassesArrayReg, 0x7FFF);
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, castClassDepthReg, castClassDepthReg, 0x1);
            }
         else
            {
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, castClassDepthReg, instanceClassSuperClassesArrayReg, HI_VALUE(castClassDepthOffset));
            }
         generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, instanceClassSuperClassReg,
                                    TR::MemoryReference::createWithDisplacement(cg, castClassDepthReg, LO_VALUE(castClassDepthOffset), TR::Compiler->om.sizeofReferenceAddress()));
         }
      else
         {
         if (comp->target().is64Bit())
            generateShiftLeftImmediateLong(cg, node, castClassDepthReg, castClassDepthReg, 3);
         else
            generateShiftLeftImmediate(cg, node, castClassDepthReg, castClassDepthReg, 2);
         generateTrg1MemInstruction(cg, TR::InstOpCode::Op_loadx, node, instanceClassSuperClassReg, TR::MemoryReference::createWithIndexReg(cg, instanceClassSuperClassesArrayReg, castClassDepthReg, TR::Compiler->om.sizeofReferenceAddress()));
         }
      }
   generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, condReg, instanceClassSuperClassReg, castClassReg);
   if (castClassDepthReg)
      srm->reclaimScratchRegister(castClassDepthReg);
   srm->reclaimScratchRegister(instanceClassSuperClassesArrayReg);
   srm->reclaimScratchRegister(instanceClassSuperClassReg);

   // At this point condReg[eq] will be set if the cast class is a superclass of the instance class. Caller is responsible for acting on the result.
   }

static
void genInstanceOfOrCheckCastArbitraryClassTest(TR::Node *node, TR::Register *condReg, TR::Register *instanceClassReg, TR_OpaqueClassBlock *arbitraryClass, TR_PPCScratchRegisterManager *srm, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR::Register *arbitraryClassReg = srm->findOrCreateScratchRegister();
   if (comp->compileRelocatableCode() && comp->getOption(TR_UseSymbolValidationManager))
      {
      TR::StaticSymbol *sym = TR::StaticSymbol::create(comp->trHeapMemory(), TR::Address);
      sym->setStaticAddress(arbitraryClass);
      sym->setClassObject();

      loadAddressConstant(cg, true, node, (intptr_t) new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), sym), arbitraryClassReg, NULL, false, TR_ClassAddress);
      }
   else
      {
      loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t)arbitraryClass, arbitraryClassReg);
      }
   genInstanceOfOrCheckCastClassEqualityTest(node, condReg, instanceClassReg, arbitraryClassReg, cg);
   srm->reclaimScratchRegister(arbitraryClassReg);

   // At this point condReg[eq] will be set if the cast class matches the arbitrary class. Caller is responsible for acting on the result.
   }

static
void genInstanceOfCastClassCacheTest(TR::Node *node, TR::Register *condReg, TR::Register *instanceClassReg, TR::Register *castClassReg, TR_PPCScratchRegisterManager *srm, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();

   // Compare the cast class against the cache on the instance class.
   // If they are the same the cast is successful.
   // If not it's either because the cache class does not match the cast class, or it does match except the cache class has the low bit set, which means the cast is not successful.
   //
   TR::Register *castClassCacheReg = srm->findOrCreateScratchRegister();
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, castClassCacheReg,
                              TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9Class, castClassCache), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::XOR, node, castClassCacheReg, castClassCacheReg, castClassReg);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::Op_cmpli, node, condReg, castClassCacheReg, 1);

   // At this point condReg[lt] will be set if the cast is successful, condReg[eq] will be set if the cast is unsuccessful, and condReg[gt] will be set if the cache class did not match the cast class.
   // Caller is responsible for acting on the result.
   }

static
void genCheckCastCastClassCacheTest(TR::Node *node, TR::Register *condReg, TR::Register *instanceClassReg, TR::Register *castClassReg, TR_PPCScratchRegisterManager *srm, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();

   // Compare the cast class against the cache on the instance class.
   // If they are the same the cast is successful.
   // If not it's either because the cache class does not match the cast class, or it does match except the cache class has the low bit set, which means the cast is invalid.
   //
   TR::Register *castClassCacheReg = srm->findOrCreateScratchRegister();
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, castClassCacheReg,
                              TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9Class, castClassCache), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::Op_cmpl, node, condReg, castClassReg, castClassCacheReg);
   srm->reclaimScratchRegister(castClassCacheReg);

   // At this point condReg[eq] will be set if the cast is successful. Caller is responsible for acting on the result.
   }

static
void genInstanceOfOrCheckCastObjectArrayTest(TR::Node *node, TR::Register *cr0Reg, TR::Register *instanceClassReg, TR::LabelSymbol *falseLabel, TR_PPCScratchRegisterManager *srm, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();

   // Load the object ROM class and test the modifiers to see if this is an array.
   //
   TR::Register *scratchReg = srm->findOrCreateScratchRegister();
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9Class, romClass), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, scratchReg, offsetof(J9ROMClass, modifiers), 4));
   if (J9AccClassArray <= UPPER_IMMED && J9AccClassArray >= LOWER_IMMED)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, scratchReg, scratchReg, cr0Reg, J9AccClassArray);
   else
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andis_r, node, scratchReg, scratchReg, cr0Reg, J9AccClassArray >> 16);

   // At this point cr0[eq] will be set if this is not an array.
   //
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, falseLabel, cr0Reg);

   // If it's an array, load the component ROM class and test the modifiers to see if this is a primitive array.
   //
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, instanceClassReg, offsetof(J9ArrayClass, componentType), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, scratchReg, offsetof(J9Class, romClass), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, scratchReg, offsetof(J9ROMClass, modifiers), 4));
   if (J9AccClassInternalPrimitiveType <= UPPER_IMMED && J9AccClassInternalPrimitiveType >= LOWER_IMMED)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, scratchReg, scratchReg, cr0Reg, J9AccClassInternalPrimitiveType);
   else
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andis_r, node, scratchReg, scratchReg, cr0Reg, J9AccClassInternalPrimitiveType >> 16);
   srm->reclaimScratchRegister(scratchReg);

   // At this point cr0[eq] will be set if this is not a primitive array. Caller is responsible acting on the result.
   }

static
void genInstanceOfOrCheckCastHelperCall(TR::Node *node, TR::Register *objectReg, TR::Register *castClassReg, TR::Register *resultReg, TR::CodeGenerator *cg)
   {
   TR_ASSERT((resultReg != NULL) == (node->getOpCodeValue() == TR::instanceof),
             "Expecting result reg for instanceof but not checkcast");

   TR::Register *helperReturnReg = resultReg ? cg->allocateRegister() : NULL;

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(1, 4, cg->trMemory());

   deps->addPreCondition(castClassReg, TR::RealRegister::gr3);
   deps->addPostCondition(resultReg ? helperReturnReg : castClassReg, TR::RealRegister::gr3);
   deps->addPostCondition(objectReg, TR::RealRegister::gr4);
   TR::Register *dummyReg, *dummyRegCCR;
   // gr11 is killed by the trampoline.
   deps->addPostCondition(dummyReg = cg->allocateRegister(), TR::RealRegister::gr11);
   dummyReg->setPlaceholderReg();
   // cr0 is not preserved by the helper.
   deps->addPostCondition(dummyRegCCR = cg->allocateRegister(TR_CCR), TR::RealRegister::cr0);
   dummyRegCCR->setPlaceholderReg();

   TR::Instruction *gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)node->getSymbolReference()->getMethodAddress(), deps, node->getSymbolReference());
   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);

   if (resultReg)
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, resultReg, helperReturnReg);

   cg->stopUsingRegister(dummyReg);
   cg->stopUsingRegister(dummyRegCCR);

   cg->machine()->setLinkRegisterKilled(true);

   TR::Register *nodeRegs[3] = {objectReg, castClassReg, resultReg};
   deps->stopUsingDepRegs(cg, 3, nodeRegs);
   }

TR::Register *J9::Power::TreeEvaluator::VMcheckcastEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation                      *comp = cg->comp();
   TR_J9VMBase                          *fej9 = reinterpret_cast<TR_J9VMBase *>(comp->fe());
   TR_OpaqueClassBlock                  *compileTimeGuessClass;
   InstanceOfOrCheckCastProfiledClasses  profiledClassesList[1];
   InstanceOfOrCheckCastSequences        sequences[InstanceOfOrCheckCastMaxSequences];
   uint32_t                              numberOfProfiledClass = 0;
   float                                 topClassProbability = 0.0;
   bool                                  topClassWasCastClass = false;
   uint32_t                              numSequencesRemaining = calculateInstanceOfOrCheckCastSequences(node, sequences, &compileTimeGuessClass, cg, profiledClassesList, &numberOfProfiledClass, 1, &topClassProbability, &topClassWasCastClass);

   TR::Node        *objectNode = node->getFirstChild();
   TR::Node        *castClassNode = node->getSecondChild();
   TR::Register    *objectReg = cg->evaluate(objectNode);
   TR::Register    *castClassReg = NULL;

   TR::LabelSymbol *depLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *helperReturnLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *nextSequenceLabel = generateLabelSymbol(cg);

   TR::Instruction *gcPoint;

   TR_PPCScratchRegisterManager *srm = cg->generateScratchRegisterManager();
   TR::Register                 *objectClassReg = NULL;
   TR::Register                 *cr0Reg = cg->allocateRegister(TR_CCR);

   InstanceOfOrCheckCastSequences *iter = &sequences[0];
   while (numSequencesRemaining > 1)
      {
      switch (*iter)
         {
         case EvaluateCastClass:
            TR_ASSERT(!castClassReg, "Cast class already evaluated");
            castClassReg = cg->evaluate(castClassNode);
            break;
         case LoadObjectClass:
            TR_ASSERT(!objectClassReg, "Object class already loaded");
            objectClassReg = srm->findOrCreateScratchRegister();
            generateLoadJ9Class(node, objectClassReg, objectReg, cg);
            break;
         case NullTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting NullTest\n", node->getOpCode().getName());
            TR_ASSERT(!objectNode->isNonNull(), "Object is known to be non-null, no need for a null test");
            if (node->getOpCodeValue() == TR::checkcastAndNULLCHK)
               {
               TR::Node *nullChkInfo = comp->findNullChkInfo(node);
               gcPoint = generateNullTestInstructions(cg, objectReg, nullChkInfo, !cg->getHasResumableTrapHandler());
               gcPoint->PPCNeedsGCMap(0x0);
               }
            else
               {
               genNullTest(node, objectReg, cr0Reg, cg);
               }
            break;
         case GoToTrue:
            TR_ASSERT(false, "Doesn't make sense, GoToTrue should not be part of multiple sequences");
            break;
         case GoToFalse:
            TR_ASSERT(false, "Doesn't make sense, GoToFalse should be the terminal sequence");
            break;
         case ClassEqualityTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ClassEqualityTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastClassEqualityTest(node, cr0Reg, objectClassReg, castClassReg, cg);
            break;
         case SuperClassTest:
            {
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting SuperClassTest\n", node->getOpCode().getName());
            int32_t castClassDepth = castClassNode->getSymbolReference()->classDepth(comp);
            genInstanceOfOrCheckCastSuperClassTest(node, cr0Reg, objectClassReg, castClassReg, castClassDepth, nextSequenceLabel, srm, cg);
            break;
            }
         case ProfiledClassTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ProfiledClassTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastArbitraryClassTest(node, cr0Reg, objectClassReg, profiledClassesList[0].profiledClass, srm, cg);
            break;
         case CompileTimeGuessClassTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting CompileTimeGuessClassTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastArbitraryClassTest(node, cr0Reg, objectClassReg, compileTimeGuessClass, srm, cg);
            break;
         case CastClassCacheTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting CastClassCacheTest\n", node->getOpCode().getName());
            genCheckCastCastClassCacheTest(node, cr0Reg, objectClassReg, castClassReg, srm, cg);
            break;
         case ArrayOfJavaLangObjectTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ArrayOfJavaLangObjectTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastObjectArrayTest(node, cr0Reg, objectClassReg, nextSequenceLabel, srm, cg);
            break;
         case HelperCall:
            TR_ASSERT(false, "Doesn't make sense, HelperCall should be the terminal sequence");
            break;
         }

      genCheckCastTransitionToNextSequence(node, iter, nextSequenceLabel, doneLabel, cr0Reg, cg);

      --numSequencesRemaining;
      ++iter;

      if (*iter != HelperCall && *iter != GoToFalse)
         {
         generateLabelInstruction(cg, TR::InstOpCode::label, node, nextSequenceLabel);
         nextSequenceLabel = generateLabelSymbol(cg);
         }
      }

   if (objectClassReg)
      srm->reclaimScratchRegister(objectClassReg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel);

   cg->generateDebugCounter(TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast/%s/fastPath",
                                                   node->getOpCode().getName()),
                            *srm);
   cg->generateDebugCounter(TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast.perMethod/%s/(%s)/%d/%d/fastPath",
                                                   node->getOpCode().getName(), comp->signature(), node->getByteCodeInfo().getCallerIndex(), node->getByteCodeInfo().getByteCodeIndex()),
                            *srm);

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 3 + srm->numAvailableRegisters(), cg->trMemory());
   srm->addScratchRegistersToDependencyList(deps, true);
   deps->addPostCondition(cr0Reg, TR::RealRegister::cr0);
   deps->addPostCondition(objectReg, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
   if (castClassReg)
      {
      deps->addPostCondition(castClassReg, TR::RealRegister::NoReg);
      deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
      }

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, depLabel, deps);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, helperReturnLabel);

   if (numSequencesRemaining > 0 && *iter != GoToTrue)
      {
      TR_ASSERT(*iter == HelperCall || *iter == GoToFalse, "Expecting helper call or fail here");
      bool helperCallForFailure = *iter != HelperCall;
      if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting HelperCall%s\n", node->getOpCode().getName(), helperCallForFailure ? " for failure" : "");
      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, nextSequenceLabel, helperReturnLabel, cg);
      cg->generateDebugCounter(nextSequenceLabel->getInstruction(),
                               TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast/%s/%s",
                                                                  node->getOpCode().getName(),
                                                                  helperCallForFailure ? "fail" : "helperCall"));
      cg->generateDebugCounter(nextSequenceLabel->getInstruction(),
                               TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast.perMethod/%s/(%s)/%d/%d/%s",
                                                                  node->getOpCode().getName(), comp->signature(), node->getByteCodeInfo().getCallerIndex(), node->getByteCodeInfo().getByteCodeIndex(),
                                                                  helperCallForFailure ? "fail" : "helperCall"));

      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);
      }

   deps->stopUsingDepRegs(cg, objectReg, castClassReg);
   cg->decReferenceCount(objectNode);
   cg->decReferenceCount(castClassNode);

   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::VMinstanceOfEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation                      *comp = cg->comp();
   TR_OpaqueClassBlock                  *compileTimeGuessClass;
   bool                                  profiledClassIsInstanceOf;
   InstanceOfOrCheckCastProfiledClasses  profiledClassesList[1];
   InstanceOfOrCheckCastSequences        sequences[InstanceOfOrCheckCastMaxSequences];
   bool                                  topClassWasCastClass = false;
   float                                 topClassProbability = 0.0;
   uint32_t                              numberOfProfiledClass;
   uint32_t                              numSequencesRemaining = calculateInstanceOfOrCheckCastSequences(node, sequences, &compileTimeGuessClass, cg, profiledClassesList, &numberOfProfiledClass, 1, &topClassProbability, &topClassWasCastClass);

   TR::Node                       *objectNode = node->getFirstChild();
   TR::Node                       *castClassNode = node->getSecondChild();
   TR::Register                   *objectReg = cg->evaluate(objectNode);
   TR::Register                   *castClassReg = NULL;
   TR::Register                   *resultReg = cg->allocateRegister();

   TR::LabelSymbol *depLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *helperReturnLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *nextSequenceLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *oppositeResultLabel = generateLabelSymbol(cg);

   TR::Instruction *gcPoint;

   TR_PPCScratchRegisterManager *srm = cg->generateScratchRegisterManager();
   TR::Register                 *objectClassReg = NULL;
   TR::Register                 *cr0Reg = cg->allocateRegister(TR_CCR);

   static bool initialResult = feGetEnv("TR_instanceOfInitialValue") != NULL;//true;
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, initialResult);

   InstanceOfOrCheckCastSequences *iter = &sequences[0];
   while (numSequencesRemaining > 1 || (numSequencesRemaining == 1 && *iter != HelperCall))
      {
      switch (*iter)
         {
         case EvaluateCastClass:
            TR_ASSERT(!castClassReg, "Cast class already evaluated");
            castClassReg = cg->evaluate(castClassNode);
            break;
         case LoadObjectClass:
            TR_ASSERT(!objectClassReg, "Object class already loaded");
            objectClassReg = srm->findOrCreateScratchRegister();
            generateLoadJ9Class(node, objectClassReg, objectReg, cg);
            break;
         case NullTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting NullTest\n", node->getOpCode().getName());
            TR_ASSERT(!objectNode->isNonNull(), "Object is known to be non-null, no need for a null test");
            if (node->getOpCodeValue() == TR::checkcastAndNULLCHK)
               {
               TR::Node *nullChkInfo = comp->findNullChkInfo(node);
               gcPoint = generateNullTestInstructions(cg, objectReg, nullChkInfo, !cg->getHasResumableTrapHandler());
               gcPoint->PPCNeedsGCMap(0x0);
               }
            else
               {
               genNullTest(node, objectReg, cr0Reg, cg);
               }
            break;
         case GoToTrue:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting GoToTrue\n", node->getOpCode().getName());
            if (initialResult == true)
               generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
            break;
         case GoToFalse:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting GoToFalse\n", node->getOpCode().getName());
            if (initialResult == false)
               generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
            break;
         case ClassEqualityTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ClassEqualityTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastClassEqualityTest(node, cr0Reg, objectClassReg, castClassReg, cg);
            break;
         case SuperClassTest:
            {
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting SuperClassTest\n", node->getOpCode().getName());
            int32_t castClassDepth = castClassNode->getSymbolReference()->classDepth(comp);
            genInstanceOfOrCheckCastSuperClassTest(node, cr0Reg, objectClassReg, castClassReg, castClassDepth, nextSequenceLabel, srm, cg);
            break;
            }
         case ProfiledClassTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ProfiledClassTest\n", node->getOpCode().getName());
            profiledClassIsInstanceOf = profiledClassesList[0].isProfiledClassInstanceOfCastClass;
            genInstanceOfOrCheckCastArbitraryClassTest(node, cr0Reg, objectClassReg, profiledClassesList[0].profiledClass, srm, cg);
            break;
         case CompileTimeGuessClassTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting CompileTimeGuessClassTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastArbitraryClassTest(node, cr0Reg, objectClassReg, compileTimeGuessClass, srm, cg);
            break;
         case CastClassCacheTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting CastClassCacheTest\n", node->getOpCode().getName());
            genInstanceOfCastClassCacheTest(node, cr0Reg, objectClassReg, castClassReg, srm, cg);
            break;
         case ArrayOfJavaLangObjectTest:
            if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting ArrayOfJavaLangObjectTest\n", node->getOpCode().getName());
            genInstanceOfOrCheckCastObjectArrayTest(node, cr0Reg, objectClassReg, nextSequenceLabel, srm, cg);
            break;
         case DynamicCacheObjectClassTest:
            TR_ASSERT_FATAL(false, "%s: DynamicCacheObjectClassTest is not implemented on P\n", node->getOpCode().getName());
            break;
         case DynamicCacheDynamicCastClassTest:
            TR_ASSERT_FATAL(false, "%s: DynamicCacheDynamicCastClassTest is not implemented on P\n", node->getOpCode().getName());
            break;
         case HelperCall:
            TR_ASSERT(false, "Doesn't make sense, HelperCall should be the terminal sequence");
            break;
         }

      genInstanceOfTransitionToNextSequence(node, iter, nextSequenceLabel, doneLabel, cr0Reg, resultReg, initialResult, oppositeResultLabel, profiledClassIsInstanceOf, cg);

      --numSequencesRemaining;
      ++iter;

      if (*iter != HelperCall)
         {
         generateLabelInstruction(cg, TR::InstOpCode::label, node, nextSequenceLabel);
         nextSequenceLabel = generateLabelSymbol(cg);
         }
      }

   if (objectClassReg)
      srm->reclaimScratchRegister(objectClassReg);

   if (true)
      {
      generateLabelInstruction(cg, TR::InstOpCode::label, node, oppositeResultLabel);
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, !initialResult);
      }

   generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel);

   cg->generateDebugCounter(TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast/%s/fastPath",
                                                               node->getOpCode().getName()),
                            *srm);
   cg->generateDebugCounter(TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast.perMethod/%s/(%s)/%d/%d/fastPath",
                                                               node->getOpCode().getName(), comp->signature(), node->getByteCodeInfo().getCallerIndex(), node->getByteCodeInfo().getByteCodeIndex()),
                            *srm);

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 4 + srm->numAvailableRegisters(), cg->trMemory());
   srm->addScratchRegistersToDependencyList(deps, true);
   deps->addPostCondition(cr0Reg, TR::RealRegister::cr0);
   deps->addPostCondition(resultReg, TR::RealRegister::NoReg);
   deps->addPostCondition(objectReg, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
   if (castClassReg)
      {
      deps->addPostCondition(castClassReg, TR::RealRegister::NoReg);
      deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
      }

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, depLabel, deps);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, helperReturnLabel);

   if (numSequencesRemaining > 0)
      {
      if (*iter == HelperCall)
         {
         if (comp->getOption(TR_TraceCG)) traceMsg(comp, "%s: Emitting HelperCall\n", node->getOpCode().getName());
         TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::icall, resultReg, nextSequenceLabel, helperReturnLabel, cg);
         cg->generateDebugCounter(nextSequenceLabel->getInstruction(),
                                  TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast/%s/helperCall",
                                                                     node->getOpCode().getName()));
         cg->generateDebugCounter(nextSequenceLabel->getInstruction(),
                                  TR::DebugCounter::debugCounterName(comp, "instanceOfOrCheckCast.perMethod/%s/(%s)/%d/%d/helperCall",
                                                                     node->getOpCode().getName(), comp->signature(), node->getByteCodeInfo().getCallerIndex(), node->getByteCodeInfo().getByteCodeIndex()));

         cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);
         }
      }

   // Stop using every reg in the deps except these ones.
   //
   TR::Register *nodeRegs[3] = {objectReg, castClassReg, resultReg};
   deps->stopUsingDepRegs(cg, 3, nodeRegs);

   cg->decReferenceCount(objectNode);
   cg->decReferenceCount(castClassNode);

   node->setRegister(resultReg);

   return resultReg;
   }

// ----------------------------------------------------------------------------

// only need a helper call if the class is not super and not final, otherwise
// it can be determined without a call-out
static bool needHelperCall(bool testCastClassIsSuper, bool isFinalClass)
   {
   return !testCastClassIsSuper && !isFinalClass;
   }

static bool needTestCache(bool cachingEnabled, bool needsHelperCall, bool superClassTest, TR_OpaqueClassBlock* castClassAddr, uint8_t num_PICS)
   {
   return cachingEnabled && needsHelperCall && !superClassTest && castClassAddr && num_PICS;
   }

TR::Register *J9::Power::TreeEvaluator::ifInstanceOfEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::VMifInstanceOfEvaluator(node, cg);
   }


TR::Register *J9::Power::TreeEvaluator::VMifInstanceOfEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Node *depNode;
   int32_t depIndex;
   int32_t numDep;
   TR::LabelSymbol *branchLabel;
   TR::Node *instanceOfNode;
   TR::Node *valueNode;
   int32_t value;
   TR::LabelSymbol *doneLabel, *res0Label, *res1Label;
   bool branchOn1;
   TR::Register *resultReg;
   TR::ILOpCodes opCode;
   TR::Compilation * comp = cg->comp();
   TR::SymbolReference *castClassSymRef;
   TR::Node *castClassNode;
   TR_OpaqueClassBlock *castClassAddr;
   TR_OpaqueClassBlock *guessClassArray[NUM_PICS];

   depIndex = 0;
   numDep = 0;
   depNode = NULL;
   branchLabel = node->getBranchDestination()->getNode()->getLabel();
   instanceOfNode = node->getFirstChild();
   valueNode = node->getSecondChild();
   value = valueNode->getInt();
   opCode = node->getOpCodeValue();

   castClassNode = instanceOfNode->getSecondChild();
   castClassSymRef = castClassNode->getSymbolReference();
   castClassAddr = TR::TreeEvaluator::getCastClassAddress(castClassNode);
   uint8_t num_PICS = TR::TreeEvaluator::interpreterProfilingInstanceOfOrCheckCastInfo(cg, instanceOfNode, guessClassArray);

   bool testEqualClass = instanceOfOrCheckCastNeedEqualityTest(instanceOfNode, cg);
   // Testing castClassAddr to set testCastClassIsSuper only if cast class is known at compile time
   // When class is unknown, testCastClassIsSuper is false, and needsHelperCall is true to handle non-typical class types (e.g. interfaces)
   bool testCastClassIsSuper = castClassAddr && instanceOfOrCheckCastNeedSuperTest(instanceOfNode, cg);
   bool isFinalClass = (castClassSymRef == NULL) ? false : castClassSymRef->isNonArrayFinal(comp);
   bool needsHelperCall = needHelperCall(testCastClassIsSuper, isFinalClass);
   bool testCache = needTestCache(!comp->getOption(TR_DisableInlineCheckCast), needsHelperCall, testCastClassIsSuper, castClassAddr, num_PICS);

   if (node->getNumChildren() == 3)
      {
      depNode = node->getChild(2);
      numDep = depNode->getNumChildren();
      }

   // If the result itself is assigned to a global register, we still have to evaluate it
   int32_t needResult = (instanceOfNode->getReferenceCount() > 1);

   if (depNode != NULL)
      {
      int32_t i1;

      if (!needsHelperCall && numberOfRegisterCandidate(cg, depNode, TR_GPR) + 7 > cg->getMaximumNumberOfGPRsAllowedAcrossEdge(node))
         return ((TR::Register *) 1);

      const TR::PPCLinkageProperties& properties = cg->getLinkage()->getProperties();
      TR::Node *objNode = instanceOfNode->getFirstChild();
      for (i1 = 0; i1 < depNode->getNumChildren(); i1++)
         {
         TR::Node *childNode = depNode->getChild(i1);
         int32_t regIndex = cg->getGlobalRegister(childNode->getGlobalRegisterNumber());

         int32_t validHighRegNum = TR::TreeEvaluator::getHighGlobalRegisterNumberIfAny(childNode, cg);

         if (needsHelperCall)
            {
            int32_t highIndex;

            if (validHighRegNum != -1)
               highIndex = cg->getGlobalRegister(validHighRegNum);

            if (!properties.getPreserved((TR::RealRegister::RegNum) regIndex)
                  || (validHighRegNum != -1 && !properties.getPreserved((TR::RealRegister::RegNum) highIndex)))
               return ((TR::Register *) 1);
            }
         else
            {
            if (childNode->getOpCodeValue() == TR::PassThrough)
               childNode = childNode->getFirstChild();
            if ((childNode == objNode || childNode == castClassNode) && regIndex == TR::RealRegister::gr0)
               return ((TR::Register *) 1);
            }
         }
      depIndex = numberOfRegisterCandidate(cg, depNode, TR_GPR) + numberOfRegisterCandidate(cg, depNode, TR_FPR) +
                 numberOfRegisterCandidate(cg, depNode, TR_CCR) + numberOfRegisterCandidate(cg, depNode, TR_VRF) +
	         numberOfRegisterCandidate(cg, depNode, TR_VSX_SCALAR) + numberOfRegisterCandidate(cg, depNode, TR_VSX_VECTOR);
      }

   doneLabel = generateLabelSymbol(cg);

   if ((opCode == TR::ificmpeq && value == 1) || (opCode != TR::ificmpeq && value == 0))
      {
      res0Label = doneLabel;
      res1Label = branchLabel;
      branchOn1 = true;
      }
   else
      {
      res0Label = branchLabel;
      res1Label = doneLabel;
      branchOn1 = false;
      }

   resultReg = TR::TreeEvaluator::VMgenCoreInstanceofEvaluator(instanceOfNode, cg, true, depIndex, numDep, depNode, needResult, needsHelperCall, testEqualClass, testCache, testCastClassIsSuper,
         doneLabel, res0Label, res1Label, branchOn1);

   if (resultReg != instanceOfNode->getRegister())
      instanceOfNode->setRegister(resultReg);
   cg->decReferenceCount(instanceOfNode);
   cg->decReferenceCount(valueNode);

   node->setRegister(NULL);
   return NULL;
   }

// genCoreInstanceofEvaluator is used by if instanceof and instanceof routines.
// The routine generates the 'core' code for instanceof evaluation. It requires a true and false label
// (which are the same and are just fall-through labels if no branching is required) as well as
// a boolean to indicate if the result should be calculated and returned in a register.
// The code also needs to indicate if the fall-through case if for 'true' or 'false'.
TR::Register * J9::Power::TreeEvaluator::VMgenCoreInstanceofEvaluator(TR::Node * node, TR::CodeGenerator * cg, bool isVMifInstanceOf, int32_t depIndex, int32_t numDep,
      TR::Node *depNode, bool needResult, bool needHelperCall, bool testEqualClass, bool testCache, bool testCastClassIsSuper, TR::LabelSymbol *doneLabel,
      TR::LabelSymbol *res0Label, TR::LabelSymbol *res1Label, bool branchOn1)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::Register *objectReg, *castClassReg, *objClassReg;
   TR::Register *resultReg, *crReg, *scratch1Reg, *scratch2Reg, *scratch3Reg;
   TR::Instruction *iCursor;
   TR::SymbolReference *castClassSymRef;
   TR::RegisterDependencyConditions *conditions;
   TR::LabelSymbol *trueLabel, *callLabel, *nextTestLabel;
   TR::Node *objectNode, *castClassNode;
   int32_t castClassDepth;
   TR::ILOpCodes opCode;
   TR::RegisterDependencyConditions *deps;
   TR_OpaqueClassBlock *castClassAddr;
   TR_OpaqueClassBlock *guessClassArray[NUM_PICS];
   TR::Compilation * comp = cg->comp();
   castClassDepth = -1;
   trueLabel = NULL;
   resultReg = NULL;
   deps = NULL;
   iCursor = NULL;

   castClassNode = node->getSecondChild();
   castClassSymRef = castClassNode->getSymbolReference();
   castClassAddr = TR::TreeEvaluator::getCastClassAddress(castClassNode);
   uint8_t num_PICS = TR::TreeEvaluator::interpreterProfilingInstanceOfOrCheckCastInfo(cg, node, guessClassArray);
   objectNode = node->getFirstChild();

   bool stopUsingHelperCallCR = false;

   if (needHelperCall)
      {
      callLabel = generateLabelSymbol(cg);

      TR::ILOpCodes opCode = node->getOpCodeValue();

      TR::Node::recreate(node, TR::icall);
      directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);

      iCursor = cg->getAppendInstruction();
      while (iCursor->getOpCodeValue() != TR::InstOpCode::bl)
         iCursor = iCursor->getPrev();
      conditions = ((TR::PPCDepImmSymInstruction *) iCursor)->getDependencyConditions();
      iCursor = iCursor->getPrev();
      iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, callLabel, iCursor);
      iCursor = iCursor->getPrev();

      auto firstArgReg = node->getSymbol()->castToMethodSymbol()->getLinkageConvention() == TR_CHelper ? TR::RealRegister::gr4 : TR::RealRegister::gr3;
      objectReg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 1));
      castClassReg = conditions->searchPreConditionRegister(firstArgReg);
      scratch1Reg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 2));
      scratch2Reg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 4));
      scratch3Reg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 6));
      objClassReg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 3));
      crReg = conditions->searchPreConditionRegister(TR::RealRegister::cr0);
      if (!crReg)
         {
         // This is another hack to compensate for skipping the preserving of CCRs for helper calls in linkage code.
         // This hack wouldn't be necessary if this code wasn't being lazy and re-using the dependencies
         // set up by the direct call evaluator, but alas...
         // Since we are not preserving CCRs and this code previously expected to find a CCR in the call instruction's
         // dependency conditions we allocate a new virtual here and stick it in the dependencies, making sure to
         // repeat everything that is normally done for virtual regs when they are referenced by a generated instruction,
         // which just means we record two uses to correspond to the fact that we added them to the pre/post deps of the
         // call.
         crReg = nonFixedDependency(conditions, NULL, &depIndex, TR_CCR, false, cg);
         stopUsingHelperCallCR = true;
         TR::Instruction *callInstruction = scratch1Reg->getStartOfRange();
         callInstruction->useRegister(crReg);
         callInstruction->useRegister(crReg);
         }

      if (depNode != NULL)
         {
         cg->evaluate(depNode);
         deps = generateRegisterDependencyConditions(cg, depNode, 0, &iCursor);
         }

      if (testEqualClass || testCache || testCastClassIsSuper)
         {
         if (needResult)
            {
            resultReg = conditions->searchPreConditionRegister(static_cast<TR::RealRegister::RegNum>(firstArgReg + 5));
            iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0, iCursor);
            }
         if (!objectNode->isNonNull())
            {
            iCursor = genNullTest(objectNode, objectReg, crReg, scratch1Reg, iCursor, cg);
            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res0Label, crReg, iCursor);
            }
         }
      }
   else
      {
      bool earlyEval = (castClassNode->getOpCodeValue() != TR::loadaddr || castClassNode->getReferenceCount() > 1) ? true : false;

      bool objInDep = false, castClassInDep = false;
      if (depNode != NULL)
         {
         int32_t i1;

         TR::Node *grNode;
         cg->evaluate(depNode);
         deps = generateRegisterDependencyConditions(cg, depNode, 7);

         for (i1 = 0; i1 < numDep; i1++)
            {
            grNode = depNode->getChild(i1);
            if (grNode->getOpCodeValue() == TR::PassThrough)
               grNode = grNode->getFirstChild();
            if (grNode == objectNode)
               objInDep = true;
            if (grNode == castClassNode)
               castClassInDep = true;
            }
         }

      if (deps == NULL)
         {
         conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(7, 7, cg->trMemory());
         }
      else
         {
         conditions = deps;
         }

      crReg = nonFixedDependency(conditions, NULL, &depIndex, TR_CCR, false, cg);

      if (needResult)
         resultReg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, false, cg);

      objClassReg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, true, cg);

      objectReg = cg->evaluate(objectNode);

      if (!objInDep)
         objectReg = nonFixedDependency(conditions, objectReg, &depIndex, TR_GPR, true, cg);

      if (earlyEval)
         {
         castClassReg = cg->evaluate(castClassNode);
         castClassReg = nonFixedDependency(conditions, castClassReg, &depIndex, TR_GPR, true, cg);
         }

      if (needResult)
         generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0);

      if (!objectNode->isNonNull())
         {
         genNullTest(objectNode, objectReg, crReg, objClassReg, NULL, cg);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res0Label, crReg);
         }

      if (!earlyEval)
         {
         castClassReg = cg->evaluate(castClassNode);
         castClassReg = nonFixedDependency(conditions, castClassReg, &depIndex, TR_GPR, true, cg);
         }

      if (testCastClassIsSuper)
         {
         scratch1Reg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, true, cg);
         scratch2Reg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, true, cg);
         cg->stopUsingRegister(scratch1Reg);
         cg->stopUsingRegister(scratch2Reg);
         }

      iCursor = cg->getAppendInstruction();

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      cg->decReferenceCount(objectNode);
      cg->decReferenceCount(castClassNode);
      cg->stopUsingRegister(crReg);
      cg->stopUsingRegister(objClassReg);
      }

   if (testEqualClass || testCache || testCastClassIsSuper)
      {
      if (TR::Compiler->om.compressObjectReferences())
        // read only 32 bits
         iCursor = generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, objClassReg,
               TR::MemoryReference::createWithDisplacement(cg, objectReg,
                     (int32_t) TR::Compiler->om.offsetOfObjectVftField(), 4),
               iCursor);
      else
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, objClassReg,
               TR::MemoryReference::createWithDisplacement(cg, objectReg, (int32_t) TR::Compiler->om.offsetOfObjectVftField(), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
      iCursor = TR::TreeEvaluator::generateVFTMaskInstruction(cg, node, objClassReg, iCursor);
      }

   if (testEqualClass)
      {
      iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, crReg, objClassReg, castClassReg, iCursor);

      if (testCache || testCastClassIsSuper)
         {
         if (needResult)
            {
            trueLabel = generateLabelSymbol(cg);
            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, trueLabel, crReg, iCursor);
            }
         else
            {
            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res1Label, crReg, iCursor);
            }
         }
      else if (needHelperCall)
         {
         if (needResult)
            {
            iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
            }
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res1Label, crReg, iCursor);

         bool castClassIsReferenceArray = (comp->isOptServer()) && (!castClassNode->getSymbolReference()->isUnresolved())
               && (castClassNode->getSymbolReference() == comp->getSymRefTab()->findVftSymbolRef()) && (castClassNode->getNumChildren() == 1)
               && (castClassNode->getFirstChild()->getOpCodeValue() == TR::anewarray);
         if (castClassIsReferenceArray)
            {
            TR_OpaqueClassBlock * jlobjectclassBlock = fej9->getSystemClassFromClassName("java/lang/Object", 16);
            J9Class* jlobjectarrayclassBlock = jlobjectclassBlock ? (J9Class*)fej9->getArrayClassFromComponentClass((TR_OpaqueClassBlock*)jlobjectclassBlock) : NULL;
            if (jlobjectarrayclassBlock != NULL)
               {
               iCursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, (intptr_t) (jlobjectarrayclassBlock), scratch1Reg, iCursor);
               iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, crReg, objClassReg, scratch1Reg, iCursor);
               if (needResult)
                  {
                  iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0, iCursor);
                  }
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res0Label, crReg, iCursor);
               }
            }
         }
      else
         {
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, res0Label, crReg, iCursor);
         if (needResult)
            {
            iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
            }

         if (branchOn1)
            {
            iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, res1Label, iCursor);
            }
         }
      }

   if (testCache)
      {
      // The cached value could have been from a previously successful checkcast or instanceof.
      // An answer of 0 in the low order bit indicates 'success' (the cast or instanceof was successful).
      // An answer of 1 in the lower order bit indicates 'failure' (the cast would have thrown an exception, instanceof would have been unsuccessful)
      // Because of this, we can just do a simple load and compare of the 2 class pointers. If it succeeds, the low order bit
      // must be off (success) from a previous checkcast or instanceof. If the low order bit is on, it is guaranteed not to
      // compare and we will take the slow path.
      // Generate inline test for instanceOf
      genInlineTest(node, castClassAddr, guessClassArray, num_PICS, objClassReg, resultReg, crReg, scratch1Reg, scratch2Reg, false, needResult, res0Label, res1Label, doneLabel,
            callLabel, testCastClassIsSuper, iCursor, cg);
      }

   if (testCastClassIsSuper)
      {
      bool depthInReg2 = false;

      TR::StaticSymbol    *castClassSym = NULL;
      if (castClassSymRef && !castClassSymRef->isUnresolved())
         castClassSym = castClassSymRef->getSymbol()->getStaticSymbol();

      TR_OpaqueClassBlock * clazz = NULL;
      if (castClassSym)
         clazz = (TR_OpaqueClassBlock *) castClassSym->getStaticAddress();

      if(clazz)
         castClassDepth = (int32_t)TR::Compiler->cls.classDepthOf(clazz);

      if (castClassDepth == -1)
         {
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, scratch2Reg,
            TR::MemoryReference::createWithDisplacement(cg, castClassReg, offsetof(J9Class, classDepthAndFlags), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, scratch2Reg, scratch2Reg, 0, J9AccClassDepthMask, iCursor);
         depthInReg2 = true;
         }

      iCursor = genTestIsSuper(node, objClassReg, castClassReg, crReg, scratch1Reg, scratch2Reg, castClassDepth, res0Label, iCursor, cg, depthInReg2);
      iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, res0Label, crReg, iCursor);

      if (trueLabel == NULL)
         {
         if (needResult)
            {
            iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
            }
         }
      else
         {
         iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, trueLabel, iCursor);
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
         }

      if (branchOn1)
         {
         iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, res1Label, iCursor);
         }
      }
   else if (testCache)
      {
      if (trueLabel == NULL)
         {
         if (needResult)
            {
            iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
            }
         }
      else
         {
         iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, trueLabel, iCursor);
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1, iCursor);
         }
      }

   if (needHelperCall)
      {
      TR::Register *callResult = node->getRegister();
      TR::RegisterDependencyConditions *newDeps;

      if (resultReg == NULL)
         {
         resultReg = callResult;
         }
      else
         {
         generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, resultReg, callResult);
         }

      if (isVMifInstanceOf)
         {
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, crReg, resultReg, 0);
         }

      if (depNode != NULL)
         {
         newDeps = conditions->cloneAndFix(cg, deps);
         }
      else
         {
         newDeps = conditions->cloneAndFix(cg);
         }

      if (isVMifInstanceOf)
         {
         if (branchOn1)
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, res1Label, crReg);
            }
         else
            {
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, res0Label, crReg);
            }
         }

      // Just fall through
      // generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);

      // Done
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, newDeps);

      if (stopUsingHelperCallCR)
         cg->stopUsingRegister(crReg);

      // We have to revive the resultReg here. Should revisit to see how to use registers better
      // (e.g., reducing the chance of moving around).
      if (resultReg != callResult)
         reviveResultRegister(resultReg, callResult, cg);
      }

   if (depNode != NULL)
      cg->decReferenceCount(depNode);

   return resultReg;
   }

static TR::Register *
reservationLockEnter(TR::Node *node, int32_t lwOffset, TR::CodeGenerator *cg, TR::RegisterDependencyConditions *conditions, TR::Register *objReg, TR::Register *monitorReg, TR::Register *valReg, TR::Register *tempReg, TR::Register *cndReg, TR::LabelSymbol *callLabel)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::LabelSymbol *resLabel, *doneLabel, *doneOOLLabel, *loopLabel, *reserved_checkLabel;
   int32_t lockSize;
   TR::Compilation * comp = cg->comp();

   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   resLabel = generateLabelSymbol(cg);
   loopLabel = generateLabelSymbol(cg);
   reserved_checkLabel = generateLabelSymbol(cg);
   doneLabel = generateLabelSymbol(cg);
   doneOOLLabel = generateLabelSymbol(cg);

   TR::TreeEvaluator::isPrimitiveMonitor(node, cg);
   bool isPrimitive = node->isPrimitiveLockedRegion();
   lockSize = comp->target().is64Bit() && !fej9->generateCompressedLockWord() ? 8 : 4;

   /*
    * PRIMITIVE lockReservation enter sequence:            Non-Primitive lockReservation enter sequence:
    *    ld/lwz monitorReg, lwOffset(objReg)                  ld/lwz monitorReg, lwOffset(objReg)
    *    ori    valReg, metaReg, LOCK_RESERVATION_BIT         ori    valReg, metaReg, LOCK_RESERVATION_BIT
    *    cmpl   cr0, monitorReg, valReg                       cmpl   cr0, monitorReg, valReg
    *    beq    doneLabel                                     bne    resLabel                          <--- Diff
    *                                                         addi   tempReg, monitorReg, LOCK_INC_DEC_VALUE <--- Diff
    *                                                         st     tempReg, lwOffset(objReg)         <--- Diff
    *                                                         b      doneLabel
    * resLabel(startOOLLabel):
    *    cmpli  cr0, monitorReg, 0                            cmpli  cr0, monitorReg, 0
    *    bne    reserved_checkLabel                           bne    reserved_checkLabel
    *    li     tempReg, lwOffset                             li     tempReg, lwOffset
    *                                                         addi   valReg, metaReg, RES+INC          <--- Diff
    * loopLabel:
    *    larx   monitorReg, [objReg, tempReg]                 larx   monitorReg, [objReg, tempReg]
    *    cmpli  cr0, monitorReg, 0                            cmpli  cr0, monitorReg, 0
    *    bne    callLabel                                     bne    callLabel
    *    stcx.  valReg, [objReg, tempReg]                     stcx.  valReg, [objReg, tempReg]
    *    bne    loopLabel                                     bne    loopLabel
    *    isync                                                isync
    *    b      doneLabel                                     b      doneLabel
    * reserved_checkLabel:
    *    li     tempReg, PRIMITIVE_ENTER_MASK                 li     tempReg, NON_PRIMITIVE_ENTER_MASK <--- Diff
    *    andc   tempReg, monitorReg, tempReg                  andc   tempReg, monitorReg, tempReg
    *                                                         addi   valReg, metaReg, RES              <--- Diff
    *    cmpl   cr0, tempReg, valReg                          cmpl   cr0, tempReg, valReg
    *    bne    callLabel                                     bne    callLabel
    *                                                         addi   monitorReg, monitorReg, INC       <--- Diff
    *                                                         st     monitorReg, [objReg, lwOffset]    <--- Diff
    * doneLabel:
    * doneOOLLabel:
    * === OUT OF LINE ===
    * callLabel:
    *    bl     jitMonitorEntry
    *    b      doneOOLLabel
    */

   generateTrg1MemInstruction(cg, lockSize == 8 ? TR::InstOpCode::ld : TR::InstOpCode::lwz, node, monitorReg, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, lockSize));
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, valReg, metaReg, LOCK_RESERVATION_BIT);
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, monitorReg, valReg);

   if (!isPrimitive)
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, resLabel, cndReg);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tempReg, monitorReg, LOCK_INC_DEC_VALUE);
      generateMemSrc1Instruction(cg, lockSize == 8 ? TR::InstOpCode::std : TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, lockSize), tempReg);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
      }
   else
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, cndReg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, resLabel);
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, monitorReg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, reserved_checkLabel, cndReg);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, lwOffset);
   if (!isPrimitive)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valReg, metaReg, LOCK_RESERVATION_BIT | LOCK_INC_DEC_VALUE);
   generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
   generateTrg1MemInstruction(cg, lockSize == 8 ? TR::InstOpCode::ldarx : TR::InstOpCode::lwarx,
                              PPCOpProp_LoadReserveExclusiveAccess, node, monitorReg, TR::MemoryReference::createWithIndexReg(cg, objReg, tempReg, lockSize));
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, monitorReg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, cndReg);
   generateMemSrc1Instruction(cg, lockSize == 8 ? TR::InstOpCode::stdcx_r : TR::InstOpCode::stwcx_r,
                              node, TR::MemoryReference::createWithIndexReg(cg, objReg, tempReg, lockSize), valReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, loopLabel, cndReg);
   generateInstruction(cg, TR::InstOpCode::isync, node);
   generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
   generateLabelInstruction(cg, TR::InstOpCode::label, node, reserved_checkLabel);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, isPrimitive ? LOCK_RES_PRIMITIVE_ENTER_MASK : LOCK_RES_NON_PRIMITIVE_ENTER_MASK);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, tempReg, monitorReg, tempReg);
   if (!isPrimitive)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valReg, metaReg, LOCK_RESERVATION_BIT);
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, tempReg, valReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, cndReg);
   if (!isPrimitive)
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, monitorReg, monitorReg, LOCK_INC_DEC_VALUE);
      generateMemSrc1Instruction(cg, lockSize == 8 ? TR::InstOpCode::std : TR::InstOpCode::stw,
                                 node, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset & 0x0000FFFF, lockSize), monitorReg);
      }

   doneLabel->setEndInternalControlFlow();
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, doneOOLLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, doneOOLLabel);

   conditions->stopUsingDepRegs(cg, objReg);
   cg->decReferenceCount(node->getFirstChild());
   return NULL;
   }

static TR::Register *
reservationLockExit(TR::Node *node, int32_t lwOffset, TR::CodeGenerator *cg, TR::RegisterDependencyConditions *conditions, TR::Register *objReg, TR::Register *monitorReg, TR::Register *valReg, TR::Register *tempReg, TR::Register *cndReg, TR::LabelSymbol *callLabel)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::LabelSymbol *resLabel, *doneLabel, *doneOOLLabel;
   int32_t lockSize;
   TR::Compilation *comp = cg->comp();

   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   resLabel = generateLabelSymbol(cg);
   doneLabel = generateLabelSymbol(cg);
   doneOOLLabel = generateLabelSymbol(cg);

   bool isPrimitive = node->isPrimitiveLockedRegion();
   lockSize = comp->target().is64Bit() && !fej9->generateCompressedLockWord() ? 8 : 4;

   /*
    * PRIMITIVE reservationLock exit sequence              Non-PRIMITIVE reservationLock exit sequence
    *    ld/lwz monitorReg, [objReg, lwOffset]             ld/lwz monitorReg, [objReg, lwOffset]
    *    andi_r tempReg, monitorReg, PRIMITIVE_EXIT_MASK   ori    tempReg, metaReg, RES+INC              <-- Diff
    *    cmpli  cndReg, tempReg, LOCK_RESERVATION_BIT      cmpl   cndReg, monitorReg, tempReg            <-- Diff
    *    bne    resLabel                                   bne    resLabel                               <-- Diff
    *                                                      addi   valReg, metaReg, LOCK_RESERVATION_BIT  <-- Diff
    *                                                      st     valReg, [objReg, lwOffset]             <-- Diff
    *                                                      b      doneLabel                              <-- Diff
    * resLabel(startOOLLabel):
    *    li     tempReg, RES_OWNING_COMPLEMENT             li     tempReg, RES_OWNING_COMPLEMENT
    *    andc   tempReg, monitorReg, tempReg               andc   tempReg, monitorReg, tempReg
    *    addi   valReg, metaReg, RES_BIT                   addi   valReg, metaReg, RES_BIT
    *    cmpl   cndReg, tempReg, valReg                    cmpl   cndReg, tempReg, valReg
    *    bne    callLabel                                  bne    callLabel
    *    andi_r tempReg, monitorReg, RECURSION_MASK        andi_r tempReg, monitorReg, NON_PRIMITIVE_EXIT_MASK <-- Diff
    *    bne    doneLabel                                  beq    callLabel                                    <-- Diff
    *    addi   monitorReg, monitorReg, INC                addi   monitorReg, monitorReg, -INC                 <-- Diff
    *    st     monitorReg, [objReg, lwOffset]             st     monitorReg, [objReg, lwOffset]
    *                                                      b      doneLabel                                    <-- Diff
    * doneLabel:
    * doneOOLLabel:
    * === OUT OF LINE ===
    * callLabel:
    *    bl     jitMonitorExit
    *    b      doneOOLLabel
    */

   generateTrg1MemInstruction(cg, lockSize == 8 ? TR::InstOpCode::ld : TR::InstOpCode::lwz, node, monitorReg, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, lockSize));
   if (!node->isPrimitiveLockedRegion())
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, tempReg, metaReg, LOCK_RESERVATION_BIT + LOCK_INC_DEC_VALUE);
      generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, monitorReg, tempReg);
      }
   else
      {
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, tempReg, monitorReg, LOCK_RES_PRIMITIVE_EXIT_MASK);
      generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, tempReg, LOCK_RESERVATION_BIT);
      }

   if (!isPrimitive)
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, resLabel, cndReg);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valReg, metaReg, LOCK_RESERVATION_BIT);
      generateMemSrc1Instruction(cg, lockSize == 8 ? TR::InstOpCode::std : TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, lockSize), valReg);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
      }
   else
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, resLabel, cndReg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, resLabel);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, LOCK_RES_OWNING_COMPLEMENT);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, tempReg, monitorReg, tempReg);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valReg, metaReg, LOCK_RESERVATION_BIT);
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, tempReg, valReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, cndReg);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, tempReg, monitorReg, isPrimitive ? OBJECT_HEADER_LOCK_RECURSION_MASK : LOCK_RES_NON_PRIMITIVE_EXIT_MASK);
   if (isPrimitive)
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, doneLabel, cndReg);
   else
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, cndReg);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, monitorReg, monitorReg, isPrimitive ? LOCK_INC_DEC_VALUE : -LOCK_INC_DEC_VALUE);
   generateMemSrc1Instruction(cg, lockSize == 8 ? TR::InstOpCode::std : TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, lockSize), monitorReg);
   if (!isPrimitive)
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);

   doneLabel->setEndInternalControlFlow();
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, doneOOLLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, doneOOLLabel);

   conditions->stopUsingDepRegs(cg, objReg);
   cg->decReferenceCount(node->getFirstChild());
   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::VMmonexitEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   int32_t lwOffset = fej9->getByteOffsetToLockword((TR_OpaqueClassBlock *) cg->getMonClass(node));
   TR::Compilation *comp = cg->comp();
   TR_YesNoMaybe isMonitorValueBasedOrValueType = cg->isMonitorValueBasedOrValueType(node);

   if (comp->getOption(TR_FullSpeedDebug) ||
         (isMonitorValueBasedOrValueType == TR_yes) ||
         comp->getOption(TR_DisableInlineMonExit))
      {
      TR::ILOpCodes opCode = node->getOpCodeValue();
      TR::Node::recreate(node, TR::call);
      TR::Register *targetRegister = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return targetRegister;
      }

   int32_t numDeps = 6;

   TR::Node *objNode = node->getFirstChild();
   TR::Register *objReg = cg->evaluate(objNode);

   TR::Register *monitorReg = cg->allocateRegister();
   TR::Register *tempReg = cg->allocateRegister();
   TR::Register *threadReg = cg->allocateRegister();

   TR::Register *objectClassReg = cg->allocateRegister();
   TR::Register *lookupOffsetReg = NULL;

   TR::Register *condReg = cg->allocateRegister(TR_CCR);
   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   TR::Register *baseReg = objReg;

   if (lwOffset <= 0)
      {
      if (comp->getOption(TR_EnableMonitorCacheLookup))
         {
         lookupOffsetReg = cg->allocateRegister();
         numDeps++;
         }
      }

   TR::RegisterDependencyConditions *conditions;
   conditions = createConditionsAndPopulateVSXDeps(cg, numDeps);

   TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   TR::addDependency(conditions, monitorReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   TR::addDependency(conditions, tempReg, TR::RealRegister::NoReg, TR_GPR, cg);

   TR::addDependency(conditions, threadReg, TR::RealRegister::NoReg, TR_GPR, cg);

   TR::addDependency(conditions, objectClassReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   if (lookupOffsetReg)
      {
      TR::addDependency(conditions, lookupOffsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
      conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
      }

   TR::addDependency(conditions, condReg, TR::RealRegister::cr0, TR_CCR, cg);

   TR::LabelSymbol *callLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *monitorLookupCacheLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *fallThruFromMonitorLookupCacheLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel, NULL);
   startLabel->setStartInternalControlFlow();

   //If object is not known to be value type or value based class at compile time, check at run time
   if (isMonitorValueBasedOrValueType == TR_maybe)
      {
      generateCheckForValueMonitorEnterOrExit(node, callLabel, objReg, objectClassReg, tempReg, threadReg, condReg, cg, J9_CLASS_DISALLOWS_LOCKING_FLAGS);
      }

   bool simpleLocking = false;

   if (lwOffset <= 0)
      {
      generateLoadJ9Class(node, objectClassReg, objReg, cg);

      int32_t offsetOfLockOffset = offsetof(J9Class, lockOffset);
      TR::MemoryReference *tempMR = TR::MemoryReference::createWithDisplacement(cg, objectClassReg, offsetOfLockOffset, TR::Compiler->om.sizeofReferenceAddress());
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, objectClassReg, tempMR);

      generateTrg1Src1ImmInstruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, condReg, objectClassReg, 0);

      if (comp->getOption(TR_EnableMonitorCacheLookup))
         {
         lwOffset = 0;
         generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, monitorLookupCacheLabel, condReg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, objectClassReg, objReg, objectClassReg);
         generateLabelInstruction(cg, TR::InstOpCode::b, node, fallThruFromMonitorLookupCacheLabel);

         generateLabelInstruction(cg, TR::InstOpCode::label, node, monitorLookupCacheLabel);

         int32_t offsetOfMonitorLookupCache = offsetof(J9VMThread, objectMonitorLookupCache);

         generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, lookupOffsetReg, objReg);

         int32_t t = trailingZeroes(TR::Compiler->om.objectAlignmentInBytes());

         if (comp->target().is64Bit())
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::sradi, node, lookupOffsetReg, lookupOffsetReg, t);
         else
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::srawi, node, lookupOffsetReg, lookupOffsetReg, t);

         J9JavaVM * jvm = fej9->getJ9JITConfig()->javaVM;

         if (comp->target().is64Bit())
         simplifyANDRegImm(node, lookupOffsetReg, lookupOffsetReg, (int64_t) J9VMTHREAD_OBJECT_MONITOR_CACHE_SIZE - 1, cg, objNode);
         else
         simplifyANDRegImm(node, lookupOffsetReg, lookupOffsetReg, J9VMTHREAD_OBJECT_MONITOR_CACHE_SIZE - 1, cg, objNode);

         if (comp->target().is64Bit())
         generateShiftLeftImmediateLong(cg, node, lookupOffsetReg, lookupOffsetReg, trailingZeroes((int32_t) TR::Compiler->om.sizeofReferenceField()));
         else
         generateShiftLeftImmediate(cg, node, lookupOffsetReg, lookupOffsetReg, trailingZeroes((int32_t) TR::Compiler->om.sizeofReferenceField()));

         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, lookupOffsetReg, lookupOffsetReg, offsetOfMonitorLookupCache);

         generateTrg1MemInstruction(cg, (comp->target().is64Bit() && fej9->generateCompressedLockWord()) ? TR::InstOpCode::lwz :TR::InstOpCode::Op_load, node, objectClassReg,
               TR::MemoryReference::createWithIndexReg(cg, metaReg, lookupOffsetReg, TR::Compiler->om.sizeofReferenceField()));

         generateTrg1Src1ImmInstruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, condReg, objectClassReg, 0);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg);

         int32_t offsetOfMonitor = offsetof(J9ObjectMonitor, monitor);
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, lookupOffsetReg,
               TR::MemoryReference::createWithDisplacement(cg, objectClassReg, offsetOfMonitor, TR::Compiler->om.sizeofReferenceAddress()));

         int32_t offsetOfUserData = offsetof(J9ThreadAbstractMonitor, userData);
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, lookupOffsetReg,
               TR::MemoryReference::createWithDisplacement(cg, lookupOffsetReg, offsetOfUserData, TR::Compiler->om.sizeofReferenceAddress()));

         generateTrg1Src2Instruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmp8 : TR::InstOpCode::cmp4, node, condReg, lookupOffsetReg, objReg);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);

         int32_t offsetOfAlternateLockWord = offsetof(J9ObjectMonitor, alternateLockword);

         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, objectClassReg, objectClassReg, offsetOfAlternateLockWord);

         generateLabelInstruction(cg, TR::InstOpCode::label, node, fallThruFromMonitorLookupCacheLabel);
         }
      else
         {
         generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, callLabel, condReg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, objectClassReg, objReg, objectClassReg);
         }

      simpleLocking = true;
      lwOffset = 0;
      baseReg = objectClassReg;
      }

   /* If the -XX:-GlobalLockReservation option is set, try to use the original aggressive reserved locking code path. */
   if (!fej9->isEnableGlobalLockReservationSet())
      {
      bool reserveLocking = false, normalLockWithReservationPreserving = false;

      if (!simpleLocking && comp->getOption(TR_ReservingLocks))
         TR::TreeEvaluator::evaluateLockForReservation(node, &reserveLocking, &normalLockWithReservationPreserving, cg);

      if (reserveLocking)
         return reservationLockExit(node, lwOffset, cg, conditions, baseReg, monitorReg, threadReg, tempReg, condReg, callLabel);
      }

   int32_t lockSize;
   TR::InstOpCode::Mnemonic loadOpCode, storeOpCode, reservedLoadOpCode, conditionalStoreOpCode, compareLogicalOpCode, compareLogicalImmOpCode;

   if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
      {
      lockSize = 8;
      loadOpCode = TR::InstOpCode::ld;
      storeOpCode = TR::InstOpCode::std;
      reservedLoadOpCode = TR::InstOpCode::ldarx;
      conditionalStoreOpCode = TR::InstOpCode::stdcx_r;
      compareLogicalOpCode = TR::InstOpCode::cmpl8;
      compareLogicalImmOpCode = TR::InstOpCode::cmpli8;
      }
   else
      {
      lockSize = 4;
      loadOpCode = TR::InstOpCode::lwz;
      storeOpCode = TR::InstOpCode::stw;
      reservedLoadOpCode = TR::InstOpCode::lwarx;
      conditionalStoreOpCode = TR::InstOpCode::stwcx_r;
      compareLogicalOpCode = TR::InstOpCode::cmpl4;
      compareLogicalImmOpCode = TR::InstOpCode::cmpli4;
      }

   /*
    * Read only locks do not support either the Reserved bit nor the Learning bit.
    * They are disabled until the code path is updated.
    */
   if (true || !ppcSupportsReadMonitors || !node->isReadMonitor())
      {
      /*
       * JIT unlocking fast path checks:
       * 1. Check if lockword matches TID and the lower 8 bits are clear. If so, clear lockword to unlock the object.
       * 2. Check if TID matches, Learning bit is clear, and Inflated bit is clear. If not, call VM.
       * 3. Check if RC field is at least 1. If so, decrement RC.
       * 4. All fast path checks failed so go to VM.
       *
       * Check 1 catches Flat-Locked (non-nested, FLC bit clear).
       * Check 2 catches New-PreLearning, New-AutoReserve, Learning-Unlocked, Learning-Locked, Flat-Unlocked and Inflated and sends them to the VM.
       * Check 3 catches Flat-Locked (nested case) and Reserved-Locked.
       * Check 4 catches Reserved-Unlocked and Flat-Locked (non-nested case, FLC bit set) and sends it to the VM.
       *
       *
       *    ld/lwz monitorReg, lwOffset(baseReg)
       *    li     tempReg, 0
       *    cmpl   cr0, monitorReg, metaReg
       *    bne    decrementCheckLabel
       *    lwsync
       *    st     tempReg, lwOffset(baseReg)
       *    b      doneLabel
       *
       * decrementCheckLabel:
       *    li     tempReg, LOCK_NON_PRIMITIVE_EXIT_IGNORE_MASK
       *    andc   tempReg, monitorReg, tempReg
       *    cmpl   cr0, tempReg, metaReg
       *    bne    callLabel
       *    andi.  tempReg, monitorReg, OBJECT_HEADER_LOCK_RECURSION_MASK
       *    beq    callLabel
       *    addi   monitorReg, monitorReg, -LOCK_INC_DEC_VALUE
       *    st     monitorReg, lwOffset(baseReg)
       *
       * doneLabel:
       * callReturnLabel:
       * === OUT OF LINE ===
       * callLabel:
       *    bl    jitMonitorExit
       *    b     callReturnLabel
       */

      TR::LabelSymbol *decrementCheckLabel, *doneLabel;

      decrementCheckLabel = generateLabelSymbol(cg);
      doneLabel = generateLabelSymbol(cg);

      generateTrg1MemInstruction(cg, loadOpCode, node, monitorReg, TR::MemoryReference::createWithDisplacement(cg, baseReg, lwOffset, lockSize));
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, 0);
      generateTrg1Src2Instruction(cg, compareLogicalOpCode, node, condReg, monitorReg, metaReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, decrementCheckLabel, condReg);

      // exiting from read monitors still needs lwsync
      // (ensures loads have completed before releasing lock)
      if (comp->target().isSMP())
         generateInstruction(cg, TR::InstOpCode::lwsync, node);

      generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, baseReg, lwOffset, lockSize), tempReg);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, decrementCheckLabel);
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, LOCK_NON_PRIMITIVE_EXIT_IGNORE_MASK);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, tempReg, monitorReg, tempReg);
      generateTrg1Src2Instruction(cg, compareLogicalOpCode, node, condReg, tempReg, metaReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);

      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, tempReg, monitorReg, OBJECT_HEADER_LOCK_RECURSION_MASK);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg);

      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, monitorReg, monitorReg, -LOCK_INC_DEC_VALUE);
      generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, baseReg, lwOffset, lockSize), monitorReg);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      doneLabel->setEndInternalControlFlow();

      TR::LabelSymbol *callReturnLabel = generateLabelSymbol(cg);

      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, callReturnLabel, cg);
      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, callReturnLabel);

      conditions->stopUsingDepRegs(cg, objReg);
      cg->decReferenceCount(objNode);
      }
   else
      {
      // read-only locks, ReaderReg = 0x0000 0000
      //    li     tempReg, offset
      //    lwsync
      // loopLabel:
      //    lwarx  monitorReg, [baseReg, tempReg]
      //    cmpi   condReg, monitorReg, 0x6 // count == 1 (flc bit is set)
      //    addi   threadReg, monitorReg, -4
      //    beq    decLabel
      //    stwcx  [baseReg, tempReg], threadReg
      //    bne    loopLabel
      //    b      doneLabel
      // decLabel: (a misleading name, really a RestoreAndCallLabel)
      //    andi_r threadReg, monitorReg, 0x3
      //    or     threadReg, threadReg, metaReg
      //    stwcx  [baseReg, tempReg], threadReg
      //    beq    callLabel
      //    b      loopLabel
      // doneLabel:
      // callReturnLabel:
      // === OUT OF LINE ===
      // callLabel:
      //    bl   jitMonitorExit
      //    b    callReturnLabel;

      TR::LabelSymbol *loopLabel, *decLabel, *doneLabel;
      loopLabel = generateLabelSymbol(cg);
      decLabel = generateLabelSymbol(cg);
      doneLabel = generateLabelSymbol(cg);

      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, lwOffset);

      // exiting from read monitors still needs lwsync
      // (ensures loads have completed before releasing lock)
      if (comp->target().isSMP())
         generateInstruction(cg, TR::InstOpCode::lwsync, node);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel, conditions);
      generateTrg1MemInstruction(cg, reservedLoadOpCode, node, monitorReg, TR::MemoryReference::createWithIndexReg(cg, baseReg, tempReg, lockSize));

      generateTrg1Src1ImmInstruction(cg, compareLogicalImmOpCode, node, condReg, monitorReg, 0x6);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, threadReg, monitorReg, -4);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, decLabel, condReg);

      generateMemSrc1Instruction(cg, conditionalStoreOpCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, tempReg, lockSize), threadReg);

      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, condReg);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, decLabel);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, threadReg, monitorReg, condReg, 0x3);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::OR, node, threadReg, threadReg, metaReg);
      generateMemSrc1Instruction(cg, conditionalStoreOpCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, tempReg, lockSize), threadReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, loopLabel);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      doneLabel->setEndInternalControlFlow();

      TR::LabelSymbol *callReturnLabel = generateLabelSymbol(cg);

      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, callReturnLabel, cg);
      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, callReturnLabel);

      conditions->stopUsingDepRegs(cg, objReg);
      cg->decReferenceCount(objNode);
      }

   return (NULL);
   }

static void genInitObjectHeader(TR::Node *node, TR::Instruction *&iCursor, TR_OpaqueClassBlock *clazz, TR::Register *classReg, TR::Register *resReg, TR::Register *zeroReg,
      TR::Register *condReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::Register * packedClobberedOffset, TR::RegisterDependencyConditions *conditions, bool needZeroInit,
      TR::CodeGenerator *cg);

static void genHeapAlloc(TR::Node *node, TR::Instruction *&iCursor, TR_OpaqueClassBlock *clazz, bool isVariableLen, TR::Register *enumReg, TR::Register *classReg, TR::Register *resReg, TR::Register *zeroReg, TR::Register *condReg,
      TR::Register *dataSizeReg, TR::Register *heapTopReg, TR::Register *sizeReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::Register *temp3Reg, TR::LabelSymbol *callLabel, TR::LabelSymbol *doneLabel,
      int32_t allocSize, int32_t elementSize, bool usingTLH, bool needZeroInit,  TR::RegisterDependencyConditions *dependencies, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::Register *metaReg = cg->getMethodMetaDataRegister();
   TR::ILOpCodes opCode = node->getOpCodeValue();

   if (comp->getOptions()->realTimeGC())
      {
#if defined(J9VM_GC_REALTIME)
      // Use temp3Reg to hold the javaVM.
      iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp3Reg,
                                           TR::MemoryReference::createWithDisplacement(cg, metaReg, fej9->thisThreadJavaVMOffset(), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

      // Realtime size classes are now a pointer in J9JavaVM rather than an inlined struct of arrays, so we can't use J9JavaVM as a base pointer anymore
      // Use temp3Reg to hold J9JavaVM->realTimeSizeClasses
      iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp3Reg,
                                           TR::MemoryReference::createWithDisplacement(cg, temp3Reg, fej9->getRealtimeSizeClassesOffset(), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

      // heap allocation, so proceed
      if (isVariableLen)
         {
         // make sure size isn't too big
         // convert max object size to num elements because computing an object size from num elements may overflow

         uintptr_t maxSize = fej9->getMaxObjectSizeForSizeClass();
         TR_ASSERT(((int32_t)(maxSize)-allocSize)/elementSize <= USHRT_MAX, "MaxObjectSizeForSizeClass won't fit into 16-bits.");

         iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, enumReg, ((int32_t)(maxSize)-allocSize)/elementSize, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, callLabel, condReg, iCursor);

         if (TR::Compiler->om.useHybridArraylets())
            {
            // Zero length arrays are discontiguous (i.e. they also need the discontiguous length field to be 0) because
            // they are indistinguishable from non-zero length discontiguous arrays
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, enumReg, 0, iCursor);
            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg, iCursor);
            }

         // now compute size of object in bytes
         TR_ASSERT(elementSize == 1 || elementSize == 2 || elementSize == 4 || elementSize == 8 || elementSize == 16, "Illegal element size for genHeapAlloc()");

         TR::Register *scaledSizeReg = enumReg;

         if (elementSize > 1)
            {
            iCursor = generateShiftLeftImmediate(cg, node, dataSizeReg, enumReg, trailingZeroes(elementSize), iCursor);
            scaledSizeReg = dataSizeReg;
            }

         // need to round up to sizeof(UDATA) so we can use it to index into size class index array
         // conservatively just add sizeof(UDATA) bytes and round
         if (elementSize < sizeof(UDATA))
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, scaledSizeReg, allocSize + sizeof(UDATA) - 1, iCursor);
         else
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, scaledSizeReg, allocSize, iCursor);

         if (elementSize < sizeof(UDATA))
            {
            if (comp->target().is64Bit())
               iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, dataSizeReg, dataSizeReg, 0, int64_t(-sizeof(UDATA)), iCursor);
            else
               iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, dataSizeReg, dataSizeReg, 0, -sizeof(UDATA), iCursor);
            }

#ifdef J9VM_INTERP_FLAGS_IN_CLASS_SLOT
         iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, dataSizeReg, J9_GC_MINIMUM_OBJECT_SIZE, iCursor);
         TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, PPCOpProp_BranchLikely, node, doneLabel, condReg, iCursor);
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, dataSizeReg, J9_GC_MINIMUM_OBJECT_SIZE, iCursor);
         iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, iCursor);
#endif

         // J9JavaVM (or realTimeSizeClasses when J9_CHANGES_PR95193 is defined) + rounded data size + SizeClassesIndexOffset is pointer to the size class
         iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, dataSizeReg, temp3Reg, iCursor);

         //Now adjust dataSizeReg to only include the size of the array data in bytes, since this will be used in genInitArrayHeader();
         iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, dataSizeReg, -allocSize, iCursor);
         iCursor = generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, heapTopReg,
                                              TR::MemoryReference::createWithDisplacement(cg, temp1Reg, fej9->getSizeClassesIndexOffset(), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         // heapTopReg now holds size class

         // get next cell for this size class at vmThread + thisThreadAllocationCacheCurrentOffset(0) + (size class)*sizeof(J9VMGCSegregatedAllocationCacheEntry)
         TR_ASSERT(sizeof(J9VMGCSegregatedAllocationCacheEntry) == sizeof(UDATA*) * 2,
                   "J9VMGCSegregatedAllocationCacheEntry may need to be padded to avoid multiply in array access.");

         if (comp->target().is64Bit())
            iCursor = generateShiftLeftImmediateLong(cg, node, temp1Reg, heapTopReg, trailingZeroes((int32_t)sizeof(J9VMGCSegregatedAllocationCacheEntry)), iCursor);
         else
            iCursor = generateShiftLeftImmediate(cg, node, temp1Reg, heapTopReg, trailingZeroes((int32_t)sizeof(J9VMGCSegregatedAllocationCacheEntry)), iCursor);

         iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, sizeReg, temp1Reg, metaReg, iCursor);
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg,
                                              TR::MemoryReference::createWithDisplacement(cg, sizeReg, fej9->thisThreadAllocationCacheCurrentOffset(0), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
                                              TR::MemoryReference::createWithDisplacement(cg, sizeReg, fej9->thisThreadAllocationCacheTopOffset(0), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

         // if we've reached the top, then no cell available, use slow path
         iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, resReg, temp1Reg, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, callLabel, condReg, iCursor);

         // have a valid cell, need to update current cell pointer
         if (comp->target().is64Bit())
            iCursor = generateShiftLeftImmediateLong(cg, node, temp1Reg, heapTopReg, trailingZeroes((int32_t)sizeof(UDATA)), iCursor);
         else
            iCursor = generateShiftLeftImmediate(cg, node, temp1Reg, heapTopReg, trailingZeroes((int32_t)sizeof(UDATA)), iCursor);

         iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, temp1Reg, temp3Reg, iCursor);

         iCursor = generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, temp1Reg,
                                              TR::MemoryReference::createWithDisplacement(cg, temp1Reg, fej9->getSmallCellSizesOffset(), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

         // temp1Reg now holds cell size
         iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, temp1Reg, resReg, iCursor);

         //temp1Reg now holds new current cell pointer. Update it in memory:
         iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                                              TR::MemoryReference::createWithDisplacement(cg, sizeReg, fej9->thisThreadAllocationCacheCurrentOffset(0), TR::Compiler->om.sizeofReferenceAddress()), temp1Reg, iCursor);
         }
      else
         {
         // sizeClass will be bogus for variable length allocations because it only includes the header size (+ arraylet ptr for arrays)
         UDATA sizeClass = fej9->getObjectSizeClass(allocSize);

         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg, TR::MemoryReference::createWithDisplacement(cg, metaReg,
                     fej9->thisThreadAllocationCacheCurrentOffset(sizeClass), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg, TR::MemoryReference::createWithDisplacement(cg, metaReg,
                     fej9->thisThreadAllocationCacheTopOffset(sizeClass), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, resReg, temp2Reg, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, callLabel, condReg, iCursor);

         //now bump the current updatepointer
         if (fej9->getCellSizeForSizeClass(sizeClass) <= UPPER_IMMED)
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp1Reg, resReg, fej9->getCellSizeForSizeClass(sizeClass), iCursor);
         else
            {
            iCursor = loadConstant(cg, node, (int32_t)fej9->getCellSizeForSizeClass(sizeClass), temp1Reg, iCursor);
            iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, temp1Reg, resReg, iCursor);
            }
         iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                                              TR::MemoryReference::createWithDisplacement(cg, metaReg, fej9->thisThreadAllocationCacheCurrentOffset(sizeClass), TR::Compiler->om.sizeofReferenceAddress()), temp1Reg, iCursor);
         }

      iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroReg, 0, iCursor);

      //we're done
      return;
#endif
      } // if (comp->getOptions()->realTimeGC())
   else
      {
      uint32_t maxSafeSize = cg->getMaxObjectSizeGuaranteedNotToOverflow();

      bool generateArraylets = comp->generateArraylets();
      bool isArrayAlloc = (opCode == TR::newarray || opCode == TR::anewarray);

      if (isArrayAlloc)
         {
         if (TR::Compiler->om.useHybridArraylets())
            {
            int32_t maxContiguousArraySize = TR::Compiler->om.maxContiguousArraySizeInBytes() / elementSize;
            TR_ASSERT(maxContiguousArraySize >= 0, "Unexpected negative size for max contiguous array size");
            if (maxContiguousArraySize > UPPER_IMMED)
               {
               iCursor = loadConstant(cg, node, maxContiguousArraySize, temp2Reg, iCursor);
               iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, condReg, enumReg, temp2Reg, iCursor);
               }
            else
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, enumReg, maxContiguousArraySize, iCursor);

            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, callLabel, condReg, iCursor);
            }

         if (isVariableLen)
            {  // Inline runtime zero length non-packed array allocation
               // Zero length arrays are discontiguous (i.e. they also need the discontiguous length field to be 0) because
               // they are indistinguishable from non-zero length discontiguous arrays
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpli4, node, condReg, enumReg, 0, iCursor);
               TR::LabelSymbol *nonZeroLengthLabel = generateLabelSymbol(cg);
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, nonZeroLengthLabel, condReg, iCursor);

               int32_t zeroLenArraySize = (TR::Compiler->om.discontiguousArrayHeaderSizeInBytes() + TR::Compiler->om.objectAlignmentInBytes() - 1) & (-TR::Compiler->om.objectAlignmentInBytes());
               TR_ASSERT(zeroLenArraySize >= J9_GC_MINIMUM_OBJECT_SIZE, "Zero-length array size must be bigger than MIN_OBJECT_SIZE");
               TR_ASSERT(zeroLenArraySize <= maxSafeSize, "Zero-length array size must be smaller than maxSafeSize");
               // Load TLH heapAlloc and heapTop values.
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, heapTopReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapTop), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp2Reg, resReg, zeroLenArraySize, iCursor);
               iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp2Reg, heapTopReg, iCursor);
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, callLabel, condReg, iCursor);

               if (comp->compileRelocatableCode())
                  genInitObjectHeader(node, iCursor, clazz, classReg, resReg, zeroReg, condReg, heapTopReg, sizeReg, NULL, dependencies, needZeroInit, cg);
               else
                  genInitObjectHeader(node, iCursor, clazz, NULL, resReg, zeroReg, condReg, heapTopReg, sizeReg, NULL, dependencies, needZeroInit, cg);

               UDATA offsetSizeFields = fej9->getOffsetOfDiscontiguousArraySizeField();
               iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                                         TR::MemoryReference::createWithDisplacement(cg, resReg, offsetSizeFields-4, 4), enumReg, iCursor);
               iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                                         TR::MemoryReference::createWithDisplacement(cg, resReg, offsetSizeFields, 4), enumReg, iCursor);

               iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                            TR::MemoryReference::createWithDisplacement(cg, metaReg,
                            offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), temp2Reg, iCursor);
               iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
               iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, nonZeroLengthLabel);
            }
         }

      if (usingTLH)
         {
         bool sizeInReg = (isVariableLen || (allocSize > UPPER_IMMED));
         bool shouldAlignToCacheBoundary = false;
         int32_t instanceBoundaryForAlignment = 64;

         static bool verboseDualTLH = feGetEnv("TR_verboseDualTLH") != NULL;

         if (isVariableLen)
            {
            // Detect large or negative number of elements in case addr wrap-around

            if (maxSafeSize < 0x00100000)
               {
               // NOTE: TR::InstOpCode::bge and the special shifts are used to cover every
               //       possible corner cases, with 32byte maximum object header.
               iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, enumReg, (maxSafeSize >> 6) << 2, iCursor);
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, callLabel, condReg, iCursor);
               }
            else
               {
               uintptr_t mask;
               if (comp->target().is64Bit())
                  {
                  mask = 0xFFFFFFFF << (27 - leadingZeroes(maxSafeSize));
                  iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm_r, node, temp2Reg, enumReg, condReg, 0, mask, iCursor);
                  }
               else
                  {
                  mask = (0xFFFFFFFF << (11 - leadingZeroes(maxSafeSize))) & 0x0000FFFF;
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andis_r, node, temp2Reg, enumReg, condReg, mask, iCursor);
                  }
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg, iCursor);
               }
            }

         //TODO: This code is never executed, check if this can be deleted now.
         if (!cg->isDualTLH())
            {
            //All of this code never gets executed because of the 0 && in
            //the inside if statement. Candidate for deletion

            if (!isVariableLen)
               {
               static char *disableAlign = feGetEnv("TR_DisableAlignAlloc");

               if (0 && !disableAlign && (node->getOpCodeValue() == TR::New) && (comp->getMethodHotness() >= hot || node->shouldAlignTLHAlloc()))
                  {
                  TR_OpaqueMethodBlock *ownMethod = node->getOwningMethod();

                  TR::Node *classChild = node->getFirstChild();
                  char * className = NULL;
                  TR_OpaqueClassBlock *clazz = NULL;

                  if (classChild && classChild->getSymbolReference() && !classChild->getSymbolReference()->isUnresolved())
                     {
                     TR::SymbolReference *symRef = classChild->getSymbolReference();
                     TR::Symbol *sym = symRef->getSymbol();

                     if (sym && sym->getKind() == TR::Symbol::IsStatic && sym->isClassObject())
                        {
                        TR::StaticSymbol * staticSym = symRef->getSymbol()->castToStaticSymbol();
                        void * staticAddress = staticSym->getStaticAddress();
                        if (symRef->getCPIndex() >= 0)
                           {
                           if (!staticSym->addressIsCPIndexOfStatic() && staticAddress)
                              {
                              int32_t len;
                              className = TR::Compiler->cls.classNameChars(comp, symRef, len);
                              clazz = (TR_OpaqueClassBlock *) staticAddress;
                              }
                           }
                        }
                     }

                  int32_t instanceSizeForAlignment = 56;
                  static char *alignSize = feGetEnv("TR_AlignInstanceSize");
                  static char *alignBoundary = feGetEnv("TR_AlignInstanceBoundary");

                  if (alignSize)
                     instanceSizeForAlignment = atoi(alignSize);
                  if (alignBoundary)
                     instanceBoundaryForAlignment = atoi(alignBoundary);

                  if (clazz && !cg->getCurrentEvaluationBlock()->isCold() && TR::Compiler->cls.classInstanceSize(clazz) >= instanceSizeForAlignment)
                     {
                     shouldAlignToCacheBoundary = true;

                     iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
                           TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, resReg, temp1Reg, instanceBoundaryForAlignment - 1, iCursor);
                     if (comp->target().is64Bit())
                        iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, resReg, resReg, 0, int64_t(-instanceBoundaryForAlignment), iCursor);
                     else
                        iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, resReg, resReg, 0, -instanceBoundaryForAlignment, iCursor);
                     }
                  }
               }

            if (!shouldAlignToCacheBoundary)
               {
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }
            iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, heapTopReg,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapTop), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

            if (needZeroInit)
               iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroReg, 0, iCursor);

            }
         else
            {
            //DualTLH support, use nonZeroTLH if optimizer says we can.

            if (node->canSkipZeroInitialization())
               {

               if (verboseDualTLH)
                  fprintf(stderr, "DELETEME genHeapAlloc useNonZeroTLH [%d] isVariableLen [%d] node: [%p]  %s [@%s]\n", 1, isVariableLen, node, comp->signature(),
                        comp->getHotnessName(comp->getMethodHotness()));

               //Load non-zeroed TLH heapAlloc and heapTop values.
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, nonZeroHeapAlloc), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, heapTopReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, nonZeroHeapTop), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

               }
            else
               {
               if (verboseDualTLH)
                  fprintf(stderr, "DELETEME genHeapAlloc useNonZeroTLH [%d] isVariableLen [%d] node: [%p]  %s [@%s]\n", 0, isVariableLen, node, comp->signature(),
                        comp->getHotnessName(comp->getMethodHotness()));

               //Load zeroed TLH heapAlloc and heapTop values.
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, heapTopReg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapTop), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }

            } //if(!cg->isDualTLH()) == false

         if (sizeInReg)
            {
            //Size will put into sizeReg
            //Get the size of the object
            //See if we can put into a single arraylet, if not call the helper.
            //Add enough padding to make it a multiple of OBJECT_ALIGNMENT
            if (isVariableLen)
               {
               int32_t elementSize;
               if (comp->useCompressedPointers() && node->getOpCodeValue() == TR::anewarray)
                  elementSize = TR::Compiler->om.sizeofReferenceField();
               else
                  elementSize = TR::Compiler->om.getSizeOfArrayElement(node);

               // Check to see if the size of the array will fit in a single arraylet leaf.
               //
               if (generateArraylets && (node->getOpCodeValue() == TR::anewarray || node->getOpCodeValue() == TR::newarray))
                  {
                  int32_t arrayletLeafSize = TR::Compiler->om.arrayletLeafSize();
                  int32_t maxContiguousArrayletLeafSizeInBytes = arrayletLeafSize - TR::Compiler->om.sizeofReferenceAddress(); //need to add definition
                  int32_t maxArrayletSizeInElements = maxContiguousArrayletLeafSizeInBytes / elementSize;
                  if (maxArrayletSizeInElements <= UPPER_IMMED)
                     {
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, enumReg, maxArrayletSizeInElements, iCursor);
                     }
                  else
                     {
                     iCursor = loadConstant(cg, node, maxArrayletSizeInElements, temp1Reg, iCursor);
                     iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, condReg, enumReg, temp1Reg, iCursor);
                     }
                  static const char *p = feGetEnv("TR_TarokPreLeafSizeCheckVarBreak");
                  if (p)
                     generateInstruction(cg, TR::InstOpCode::bad, node);

                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, callLabel, condReg, iCursor);
                  }

               int32_t round; // zero indicates no rounding is necessary
               round = (elementSize >= TR::Compiler->om.objectAlignmentInBytes()) ? 0 : TR::Compiler->om.objectAlignmentInBytes();
               bool headerAligned = allocSize % TR::Compiler->om.objectAlignmentInBytes() ? 0 : 1;

               //TODO: The code below pads up the object allocation size so that zero init code later
               //will have multiples of wordsize to work with. For now leaving this code as is, but
               //check if its worthwhile to remove these extra instructions added here for padding as
               //zero init will be removed now.
               if (elementSize >= 2)
                  {
                  if (comp->target().is64Bit())
                     iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldic, node, dataSizeReg, enumReg, trailingZeroes(elementSize), CONSTANT64(0x00000000FFFFFFFF) << trailingZeroes(elementSize));
                  else
                     iCursor = generateShiftLeftImmediate(cg, node, dataSizeReg, enumReg, trailingZeroes(elementSize), iCursor);

                  }
               if (needZeroInit && elementSize <= 2)
                  {
                  // the zero initialization code uses a loop of stwu's, and
                  // so dataSizeReg must be rounded up to a multiple of 4
                  if (elementSize == 2)
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, dataSizeReg, 3, iCursor);
                  else  //elementSize == 1
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, enumReg, 3, iCursor);

                  if (comp->target().is64Bit() && elementSize != 1)
                     iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, dataSizeReg, dataSizeReg, 0, int64_t(-4), iCursor);
                  else
                     iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, dataSizeReg, dataSizeReg, 0, -4, iCursor);
                  }

               if ((round != 0 && (!needZeroInit || round != 4)) || !headerAligned)
                  {
                  // Round the total size to a multiple of OBJECT_ALIGNMENT
                  if (elementSize == 1 && !needZeroInit)
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, enumReg, allocSize + round - 1, iCursor);
                  else
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, dataSizeReg, allocSize + round - 1, iCursor);
                  if (comp->target().is64Bit() && elementSize != 1)
                     iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, sizeReg, sizeReg, 0, int64_t(-round), iCursor);
                  else
                     iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, sizeReg, sizeReg, 0, -round, iCursor);
                  }
               else
                  {
                  if (elementSize == 1 && !needZeroInit)
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, enumReg, allocSize, iCursor);
                  else
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, dataSizeReg, allocSize, iCursor);
                  }

#ifdef J9VM_INTERP_FLAGS_IN_CLASS_SLOT
               if (!((node->getOpCodeValue() == TR::New) || (node->getOpCodeValue() == TR::anewarray) || (node->getOpCodeValue() == TR::newarray)))
                  {
                  iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, sizeReg, J9_GC_MINIMUM_OBJECT_SIZE, iCursor);
                  TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, PPCOpProp_BranchLikely, node, doneLabel, condReg, iCursor);

                  iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, sizeReg, J9_GC_MINIMUM_OBJECT_SIZE, iCursor);
                  iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, iCursor);
                  }
#endif
               } //if (isVariableLen)
            else
               {
               // Check to see if the size of the array will fit in a single arraylet leaf.
               //
               if (generateArraylets && (node->getOpCodeValue() == TR::anewarray || node->getOpCodeValue() == TR::newarray))
                  {
                  int32_t arrayletLeafSize = TR::Compiler->om.arrayletLeafSize();

                  if (allocSize >= arrayletLeafSize + TR::Compiler->om.contiguousArrayHeaderSizeInBytes())
                     {
                     static const char *p = feGetEnv("TR_TarokPreLeafSizeCheckConstBreak");
                     if (p)
                        generateInstruction(cg, TR::InstOpCode::bad, node);

                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, callLabel, iCursor);
                     }
                  }

               iCursor = loadConstant(cg, node, allocSize, sizeReg, iCursor);
               }
            } //if (sizeInReg)

         // Calculate the after-allocation heapAlloc: if the size is huge,
         //   we need to check address wrap-around also. This is unsigned
         //   integer arithmetic, checking carry bit is enough to detect it.
         //   For variable length array, we did an up-front check already.
         if (sizeInReg)
            iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp2Reg, resReg, sizeReg, iCursor);
         else
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp2Reg, resReg, allocSize, iCursor);

         //TODO: shouldAlignToCacheBoundary is never true, check its effects here.
         int32_t padding = shouldAlignToCacheBoundary ? instanceBoundaryForAlignment : 0;

         if (!isVariableLen && ((uint32_t) allocSize + padding) > maxSafeSize)
            {
            if (!shouldAlignToCacheBoundary)
               iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp2Reg, resReg, iCursor);
            else
               iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp2Reg, temp1Reg, iCursor);
            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, callLabel, condReg, iCursor);
            }

         // We need to make sure that the TLH pointer is bumped correctly to support the Arraylet header.
         //
         if (generateArraylets && (node->getOpCodeValue() == TR::anewarray || node->getOpCodeValue() == TR::newarray))
            {
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp2Reg, temp2Reg, TR::Compiler->om.objectAlignmentInBytes() - 1, iCursor);
            iCursor = generateTrg1Src1Imm2Instruction(cg, comp->target().is64Bit() ? TR::InstOpCode::rldicr : TR::InstOpCode::rlwinm, node, temp2Reg, temp2Reg, 0, int64_t(-TR::Compiler->om.objectAlignmentInBytes()), iCursor);
            static const char *p = feGetEnv("TR_TarokAlignHeapTopBreak");
            if (p)
               iCursor = generateInstruction(cg, TR::InstOpCode::bad, node, iCursor);
            }

         // Ok, temp2Reg now points to where the object will end on the TLH.
         //    resReg will contain the start of the object where we'll write out our
         //    J9Class*. Should look like this in memory:
         //    [heapAlloc == resReg] ... temp2Reg ...//... heapTopReg.

         //Here we check if we overflow the TLH Heap Top (heapTop, or nonZeroHeapTop)
         //branch to regular heapAlloc Snippet if we overflow (ie callLabel).
         iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp2Reg, heapTopReg, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, callLabel, condReg, iCursor);

         //TODO: this code is never executed, check if we can remove this now.
         if (!cg->isDualTLH())
            {
            //shouldAlignToCacheBoundary is false at definition at the top, and
            //the only codepoint where its set to true is never executed
            //so this looks like a candidate for deletion.
            if (shouldAlignToCacheBoundary)
               {
               TR::LabelSymbol *doneAlignLabel = generateLabelSymbol(cg);
               TR::LabelSymbol *multiSlotGapLabel = generateLabelSymbol(cg);
               ;
               iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, dataSizeReg, temp1Reg, resReg, iCursor);

               if (sizeInReg)
                  iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, sizeReg, dataSizeReg, sizeReg, iCursor);
               else
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, dataSizeReg, allocSize, iCursor);

               sizeInReg = true;

               iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, dataSizeReg, sizeof(uintptr_t), iCursor);
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneAlignLabel, condReg, iCursor);
               iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, multiSlotGapLabel, condReg, iCursor);
               iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, dataSizeReg, J9_GC_SINGLE_SLOT_HOLE, iCursor);

               if (comp->target().is64Bit() && fej9->generateCompressedLockWord())
                  {
                  iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, 0, 4), dataSizeReg, iCursor);
                  iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, 4, 4), dataSizeReg, iCursor);
                  }
               else
                  {
                  iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, 0, TR::Compiler->om.sizeofReferenceAddress()), dataSizeReg,
                        iCursor);
                  }

               iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, doneAlignLabel, iCursor);
               iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, multiSlotGapLabel, iCursor);
               iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, TR::Compiler->om.sizeofReferenceAddress(), TR::Compiler->om.sizeofReferenceAddress()),
                     dataSizeReg, iCursor);
               iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, dataSizeReg, J9_GC_MULTI_SLOT_HOLE, iCursor);
               iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, 0, TR::Compiler->om.sizeofReferenceAddress()), dataSizeReg,
                     iCursor);
               iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, doneAlignLabel, iCursor);
               }
            }

         if (cg->enableTLHPrefetching())
            {
            //Decide between zeroed and non-zero TLH'es

            if (cg->isDualTLH() && node->canSkipZeroInitialization())
               {

               if (verboseDualTLH)
                  fprintf(stderr, "DELETEME genHeapAlloc PREFETCH useNonZeroTLH [%d] isVariableLen [%d] node: [%p]  %s [@%s]\n", 1, isVariableLen, node, comp->signature(),
                        comp->getHotnessName(comp->getMethodHotness()));

               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, nonZeroTlhPrefetchFTA), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }
            else
               {
               if (verboseDualTLH)
                  fprintf(stderr, "DELETEME genHeapAlloc PREFETCH useNonZeroTLH [%d] isVariableLen [%d] node: [%p]  %s [@%s]\n", 0, isVariableLen, node, comp->signature(),
                        comp->getHotnessName(comp->getMethodHotness()));

               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, tlhPrefetchFTA), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }

            if (sizeInReg)
               iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::subf_r, node, temp1Reg, sizeReg, temp1Reg, condReg, iCursor);
            else
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addic_r, node, temp1Reg, temp1Reg, condReg, -allocSize, iCursor);

            //Write back and allocate Snippet if needed.
            TR::LabelSymbol * callLabel = NULL;
            if (cg->isDualTLH() && node->canSkipZeroInitialization())
               {
               iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, nonZeroTlhPrefetchFTA), TR::Compiler->om.sizeofReferenceAddress()), temp1Reg, iCursor);

               callLabel = cg->lookUpSnippet(TR::Snippet::IsNonZeroAllocPrefetch, NULL);
               if (callLabel == NULL)
                  {
                  callLabel = generateLabelSymbol(cg);
                  TR::Snippet *snippet = new (cg->trHeapMemory()) TR::PPCNonZeroAllocPrefetchSnippet(cg, node, callLabel);
                  cg->addSnippet(snippet);
                  }
               }
            else
               {
               iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                     TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, tlhPrefetchFTA), TR::Compiler->om.sizeofReferenceAddress()), temp1Reg, iCursor);

               callLabel = cg->lookUpSnippet(TR::Snippet::IsAllocPrefetch, NULL);
               if (callLabel == NULL)
                  {
                  callLabel = generateLabelSymbol(cg);
                  TR::Snippet *snippet = new (cg->trHeapMemory()) TR::PPCAllocPrefetchSnippet(cg, node, callLabel);
                  cg->addSnippet(snippet);
                  }
               }

            // kills temp1Reg and sizeReg
            iCursor = generateDepConditionalBranchInstruction(cg, TR::InstOpCode::blel, PPCOpProp_BranchUnlikely, node, callLabel, condReg, dependencies, iCursor);

            cg->machine()->setLinkRegisterKilled(true);
            }

         //Done, write back to heapAlloc (zero or nonZero TLH) here.
         if (cg->isDualTLH() && node->canSkipZeroInitialization())
            {
            iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, nonZeroHeapAlloc), TR::Compiler->om.sizeofReferenceAddress()), temp2Reg, iCursor);
            }
         else
            {
            iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                  TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapAlloc), TR::Compiler->om.sizeofReferenceAddress()), temp2Reg, iCursor);
            }
         }
      else
         {
         bool sizeInReg = (allocSize > UPPER_IMMED);
         TR::LabelSymbol *tryLabel = generateLabelSymbol(cg);

         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, heapTopReg,
               TR::MemoryReference::createWithDisplacement(cg, temp1Reg, offsetof(J9MemorySegment, heapTop), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dataSizeReg, temp1Reg, offsetof(J9MemorySegment, heapAlloc), iCursor);
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroReg, 0, iCursor);
         if (sizeInReg)
            {
            if (0x00008000 == HI_VALUE(allocSize))
               {
               iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, sizeReg, 0x7FFF, iCursor);
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, sizeReg, sizeReg, 0x1, iCursor);
               }
            else
               {
               iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, sizeReg, HI_VALUE(allocSize), iCursor);
               }
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, sizeReg, sizeReg, LO_VALUE(allocSize), iCursor);
            }

         // Try to allocate
         iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, tryLabel, iCursor);
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, resReg, TR::MemoryReference::createWithDisplacement(cg, dataSizeReg, 0, TR::Compiler->om.sizeofReferenceAddress()), iCursor);

         // Calculate the after-allocation heapAlloc: if the size is huge,
         //   we need to check address wrap-around also. This is unsigned
         //   integer arithmetic, checking carry bit is enough to detect it.
         if ((uint32_t) allocSize > maxSafeSize)
            {
            if (sizeInReg)
               {
               iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, resReg, sizeReg, iCursor);
               }
            else
               {
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp1Reg, resReg, allocSize, iCursor);
               }

            iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp1Reg, resReg, iCursor);

            iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, callLabel, condReg, iCursor);
            }
         else
            {
            if (sizeInReg)
               {
               iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, resReg, sizeReg, iCursor);
               }
            else
               {
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, temp1Reg, resReg, allocSize, iCursor);
               }
            }

         iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp1Reg, heapTopReg, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, callLabel, condReg, iCursor);

         // todo64: I think these need to be ldarx/stdcx. in 64-bit mode
         iCursor = generateTrg1MemInstruction(cg, TR::InstOpCode::lwarx, node, temp2Reg, TR::MemoryReference::createWithIndexReg(cg, dataSizeReg, zeroReg, 4), iCursor);
         iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::cmpl4, node, condReg, temp2Reg, resReg, iCursor);
         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, tryLabel, condReg, iCursor);
         iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stwcx_r, node, TR::MemoryReference::createWithIndexReg(cg, dataSizeReg, zeroReg, 4), temp1Reg, iCursor);

         iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, tryLabel, condReg, iCursor);
         }
      }
   }


static void genInitObjectHeader(TR::Node *node, TR::Instruction *&iCursor, TR_OpaqueClassBlock *clazz, TR::Register *classReg, TR::Register *resReg, TR::Register *zeroReg,
      TR::Register *condReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::Register * packedClobberedOffset, TR::RegisterDependencyConditions *conditions, bool needZeroInit,
      TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   J9ROMClass *romClass = 0;
   uint32_t staticFlag = 0;
   uint32_t orFlag = 0;
   TR::Compilation *comp = cg->comp();

   TR_ASSERT(clazz, "Cannot have a null OpaqueClassBlock\n");
   romClass = TR::Compiler->cls.romClassOf(clazz);
   staticFlag = romClass->instanceShape;
   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   TR::Register * clzReg = classReg;

   if (comp->compileRelocatableCode() && !comp->getOption(TR_UseSymbolValidationManager))
      {
      if (node->getOpCodeValue() == TR::newarray)
         {
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, javaVM), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, temp1Reg, ((TR_J9VM *) fej9)->getPrimitiveArrayOffsetInJavaVM(node->getSecondChild()->getInt()),
                     TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         clzReg = temp1Reg;
         }
      else if (node->getOpCodeValue() == TR::anewarray)
         {
         TR_ASSERT(classReg, "must have a classReg for TR::anewarray in AOT mode");
         iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, classReg, offsetof(J9Class, arrayClass), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
         clzReg = temp1Reg;
         }
      else
         {
         TR_ASSERT(node->getOpCodeValue() == TR::New && classReg, "must have a classReg for TR::New in AOT mode");
         clzReg = classReg;
         }
      }

   // Store the class
   if (clzReg == NULL)
      {
      // HCR in genInitObjectHeader
      if (cg->wantToPatchClassPointer(clazz, node))
         {
         iCursor = loadAddressConstantInSnippet(cg, node, (int64_t) clazz, temp1Reg, temp2Reg,TR::InstOpCode::Op_load, false, iCursor);
         iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, temp1Reg, temp1Reg, orFlag, iCursor);
         }
      else
         {
#ifdef TR_TARGET_64BIT
         int32_t offset;
         intptr_t classPtr = (intptr_t)clazz;

         offset = TR_PPCTableOfConstants::lookUp((int8_t *)&classPtr, sizeof(intptr_t), true, 0, cg);

         if (offset != PTOC_FULL_INDEX)
            {
            offset *= TR::Compiler->om.sizeofReferenceAddress();
            if (TR_PPCTableOfConstants::getTOCSlot(offset) == 0)
            TR_PPCTableOfConstants::setTOCSlot(offset, (int64_t)clazz);
            if (offset<LOWER_IMMED||offset>UPPER_IMMED)
               {
               TR_ASSERT_FATAL_WITH_NODE(node, 0x00008000 != HI_VALUE(offset), "TOC offset (0x%x) is unexpectedly high. Can not encode upper 16 bits into an addis instruction.", offset);
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, temp1Reg, cg->getTOCBaseRegister(), HI_VALUE(offset), iCursor);
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, LO_VALUE(offset), TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }
            else
               {
               iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg, TR::MemoryReference::createWithDisplacement(cg, cg->getTOCBaseRegister(), offset, TR::Compiler->om.sizeofReferenceAddress()), iCursor);
               }
            if (orFlag != 0)
               {
               iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, temp1Reg, temp1Reg, orFlag, iCursor);
               }
            }
         else
            {
            iCursor = loadConstant(cg, node, (int64_t)clazz|(int64_t)orFlag, temp1Reg, iCursor);
            }
#else
         iCursor = loadConstant(cg, node, (int32_t) clazz | (int32_t) orFlag, temp1Reg, iCursor);
#endif /* TR_TARGET_64BIT */
         }
      if (TR::Compiler->om.compressObjectReferences())
         // must store only 32 bits
         iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
              TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t)TR::Compiler->om.offsetOfObjectVftField(), 4),
               temp1Reg, iCursor);
      else
         iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
               TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t) TR::Compiler->om.offsetOfObjectVftField(), TR::Compiler->om.sizeofReferenceAddress()), temp1Reg, iCursor);
      }
   else
      {
      if (orFlag != 0)
         {
         iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, clzReg, clzReg, orFlag, iCursor);
         }
      if (TR::Compiler->om.compressObjectReferences())
         // must store only 32 bits
         iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
               TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t)TR::Compiler->om.offsetOfObjectVftField(), 4),
               clzReg, iCursor);
      else
         iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
               TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t) TR::Compiler->om.offsetOfObjectVftField(), TR::Compiler->om.sizeofReferenceAddress()), clzReg, iCursor);
      }


#ifndef J9VM_INTERP_FLAGS_IN_CLASS_SLOT

   bool isStaticFlag = fej9->isStaticObjectFlags();
   if (isStaticFlag)
      {
      // The object flags can be determined at compile time.
      staticFlag |= fej9->getStaticObjectFlags();
      if (staticFlag != 0 || needZeroInit)
         {
         iCursor = loadConstant(cg, node, (int32_t) staticFlag, temp2Reg, iCursor);
         // Store the flags
         iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, new (cg->trHeapMemory()) TR::MemoryReference(resReg, (int32_t) TMP_OFFSETOF_J9OBJECT_FLAGS, 4, cg), temp2Reg,
               iCursor);
         }
      }
   else if (!comp->getOptions()->realTimeGC())
      {
      // If the object flags cannot be determined at compile time, we add a load for it.
      //
      iCursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, allocateThreadLocalHeap.objectFlags), TR::Compiler->om.sizeofReferenceAddress()), iCursor);

      // OR staticFlag with temp2Reg.
      // For now, only the lower 16 bits are set in staticFlag. But check the higher 16 bits just in case.
      if (staticFlag != 0)
         {
         if ((staticFlag & 0xFFFF0000) != 0)
            {
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::oris, node, temp2Reg, temp2Reg, (staticFlag >> 16) & 0x0000FFFF, iCursor);
            }
         if ((staticFlag & 0x0000FFFF) != 0)
            {
            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, temp2Reg, temp2Reg, staticFlag & 0x0000FFFF, iCursor);
            }
         }
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t) TMP_OFFSETOF_J9OBJECT_FLAGS, 4), temp2Reg,
            iCursor);
      }

#endif /*J9VM_INTERP_FLAGS_IN_CLASS_SLOT*/

   TR::InstOpCode::Mnemonic storeOpCode;
   int32_t lockSize;

   if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
      {
      storeOpCode = TR::InstOpCode::std;
      lockSize = 8;
      }
   else
      {
      storeOpCode = TR::InstOpCode::stw;
      lockSize = 4;
      }

   int32_t lwOffset = fej9->getByteOffsetToLockword(clazz);
   if (clazz && (lwOffset > 0))
      {
      int32_t lwInitialValue = fej9->getInitialLockword(clazz);

      if (0 != lwInitialValue)
         {
         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, temp1Reg, lwInitialValue, iCursor);
         iCursor = generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, resReg, lwOffset, lockSize), temp1Reg, iCursor);
         }
      }
   }

static void genAlignArray(TR::Node *node, TR::Instruction *&iCursor, bool isVariableLen, TR::Register *resReg, int32_t objectSize, int32_t dataBegin, TR::Register *dataSizeReg,
      TR::Register *condReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::CodeGenerator *cg)
   {
   TR::LabelSymbol *slotAtStart = generateLabelSymbol(cg);
   TR::LabelSymbol *doneAlign = generateLabelSymbol(cg);

   iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::andi_r, node, temp1Reg, resReg, condReg, 7, iCursor);
   iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, temp2Reg, 3, iCursor);
   iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, slotAtStart, condReg, iCursor);

   // The slop bytes are at the end of the allocated object.
   if (isVariableLen)
      {
      iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, temp1Reg, resReg, dataSizeReg, iCursor);
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, temp1Reg, dataBegin, 4), temp2Reg, iCursor);
      }
   else if (objectSize > UPPER_IMMED)
      {
      iCursor = loadConstant(cg, node, objectSize, temp1Reg, iCursor);
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithIndexReg(cg, resReg, temp1Reg, 4), temp2Reg, iCursor);
      }
   else
      {
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, objectSize, 4), temp2Reg, iCursor);
      }
   iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, doneAlign, iCursor);

   // the slop bytes are at the start of the allocation
   iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, slotAtStart, iCursor);
   iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, (int32_t) 0, 4), temp2Reg, iCursor);
   iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, resReg, resReg, 4, iCursor);
   iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, doneAlign, iCursor);
   }

static void genInitArrayHeader(TR::Node *node, TR::Instruction *&iCursor, bool isVariableLen, TR_OpaqueClassBlock *clazz, TR::Register *classReg, TR::Register *resReg,
      TR::Register *zeroReg, TR::Register *condReg, TR::Register *eNumReg, TR::Register *dataSizeReg, TR::Register *temp1Reg, TR::Register *temp2Reg,
      TR::RegisterDependencyConditions *conditions, bool needZeroInit, TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::Register *instanceSizeReg;

   genInitObjectHeader(node, iCursor, clazz, classReg, resReg, zeroReg, condReg, temp1Reg, temp2Reg, NULL, conditions, needZeroInit, cg);

   instanceSizeReg = eNumReg;

   if ((node->getOpCodeValue() == TR::newarray || node->getOpCodeValue() == TR::anewarray) && node->getFirstChild()->getOpCode().isLoadConst() && (node->getFirstChild()->getInt() == 0))
      {// constant zero length non-packed array
       // Zero length arrays are discontiguous (i.e. they also need the discontiguous length field to be 0) because
       // they are indistinguishable from non-zero length discontiguous arrays
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                                            TR::MemoryReference::createWithDisplacement(cg, resReg, fej9->getOffsetOfDiscontiguousArraySizeField()-4, 4),
                                            instanceSizeReg, iCursor);
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                                            TR::MemoryReference::createWithDisplacement(cg, resReg, fej9->getOffsetOfDiscontiguousArraySizeField(), 4),
                                            instanceSizeReg, iCursor);
      }
   else
      {
      // Store the array size
      iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                                           TR::MemoryReference::createWithDisplacement(cg, resReg, fej9->getOffsetOfContiguousArraySizeField(), 4),
                                           instanceSizeReg, iCursor);
      }
   }

static void genZeroInit(TR::CodeGenerator *cg, TR::Node *node, TR::Register *objectReg, int32_t headerSize, int32_t totalSize, bool useInitInfo)
   {
   TR_ASSERT((totalSize - headerSize > 0) && ((totalSize - headerSize) % 4 == 0), "Expecting non-zero word-aligned data size");

   TR::Register *zeroReg = cg->allocateRegister();
   TR::Compilation* comp = cg->comp();

   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroReg, 0);

   // Perform initialization if it is needed:
   //   1) Initialize certain array elements individually. This depends on the optimizer
   //      providing a "short" list of individual indices;
   //   2) Initialize the whole array:
   //      a) If the object size is constant and small use straight line code;
   //      b) For large objects and variable length arrays, do nothing, it was handled in the allocation helper.
   if (useInitInfo)
      {
      TR_ExtraInfoForNew *initInfo = node->getSymbolReference()->getExtraInfo();

      TR_ASSERT(initInfo && initInfo->zeroInitSlots && initInfo->numZeroInitSlots > 0, "Expecting valid init info");

      TR_BitVectorIterator bvi(*initInfo->zeroInitSlots);
      if (comp->target().is64Bit())
         {
         int32_t lastNotInit = -1;
         int32_t currElem = -1;

         // Try to group set of words into smallest groupings using double-word stores (where possible)
         while (bvi.hasMoreElements())
            {
            currElem = bvi.getNextElement();

            if (lastNotInit == -1)
               {
               // currently looking at only one slot (either just starting, or just emitted double-word store in last iter)
               lastNotInit = currElem;
               }
            else if (currElem == lastNotInit + 1)
               {
               // consecutive slots, can use std
               generateMemSrc1Instruction(cg, TR::InstOpCode::std, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, headerSize + lastNotInit * 4, 8), zeroReg);
               lastNotInit = -1;
               }
            else
               {
               // Two non-consecutive slots, use stw for earlier slot and keep looking for slot
               // consecutive with the second slot currently held
               generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, headerSize + lastNotInit * 4, 4), zeroReg);
               lastNotInit = currElem;
               }
            }

         if (lastNotInit != -1)
            {
            // Last slot is not consecutive with other slots, hasn't been initialized yet
            generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, headerSize + lastNotInit * 4, 4), zeroReg);
            }
         }
      else
         {
         while (bvi.hasMoreElements())
            {
            generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                  TR::MemoryReference::createWithDisplacement(cg, objectReg, headerSize + bvi.getNextElement() * TR::Compiler->om.sizeofReferenceAddress(), TR::Compiler->om.sizeofReferenceAddress()), zeroReg);
            }
         }
      }
   else
      {
      int32_t ofs;
      for (ofs = headerSize; ofs < totalSize - TR::Compiler->om.sizeofReferenceAddress(); ofs += TR::Compiler->om.sizeofReferenceAddress())
         {
         generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, ofs, TR::Compiler->om.sizeofReferenceAddress()), zeroReg);
         }
      if (ofs + 4 == totalSize)
         generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, ofs, 4), zeroReg);
      else if (ofs + 8 == totalSize)
         generateMemSrc1Instruction(cg, TR::InstOpCode::std, node, TR::MemoryReference::createWithDisplacement(cg, objectReg, ofs, 8), zeroReg);
      }

   // Scheduling barrier
   generateLabelInstruction(cg, TR::InstOpCode::label, node, generateLabelSymbol(cg));

   cg->stopUsingRegister(zeroReg);
   }

TR::Register *J9::Power::TreeEvaluator::VMnewEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   int32_t allocateSize, objectSize, dataBegin, idx;
   TR::ILOpCodes opCode;
   TR_OpaqueClassBlock *clazz;
   TR::Register *classReg, *resReg, *zeroReg = NULL;
   TR::RealRegister::RegNum resultRealReg = TR::RealRegister::gr8;
   TR::Register *condReg, *callResult, *enumReg, *dataSizeReg;
   TR::Register *tmp3Reg = NULL, *tmp4Reg, *tmp5Reg, *tmp6Reg, *tmp7Reg = NULL;
   TR::Register *packedRegAddr, *packedRegOffs, *packedRegLength;
   TR::LabelSymbol *callLabel, *callReturnLabel, *doneLabel;
   TR::RegisterDependencyConditions *conditions;
   TR::Instruction *iCursor = NULL;
   bool doInline = true, isArray = false, doublewordAlign = false;
   bool isVariableLen;
   bool insertType = false;
   static bool TR_noThreadLocalHeap = feGetEnv("TR_noThreadLocalHeap") ? 1 : 0;
   TR::Compilation * comp = cg->comp();
   bool generateArraylets = comp->generateArraylets();

   if (comp->getOption(TR_DisableTarokInlineArrayletAllocation) && (node->getOpCodeValue() == TR::anewarray || node->getOpCodeValue() == TR::newarray))
      doInline = false;

   bool usingTLH = !TR_noThreadLocalHeap;
   bool needZeroInit = (!usingTLH || !fej9->tlhHasBeenCleared()) || comp->getOptions()->realTimeGC();
   bool isDualTLH = cg->isDualTLH();

   int32_t elementSize = 0;

   opCode = node->getOpCodeValue();
   objectSize = comp->canAllocateInline(node, clazz);

   bool isArrayAlloc = (opCode == TR::newarray || opCode == TR::anewarray);
   bool isConstantLenArrayAlloc = isArrayAlloc && node->getFirstChild()->getOpCode().isLoadConst();
   bool isConstantZeroLenArrayAlloc = isConstantLenArrayAlloc && (node->getFirstChild()->getInt() == 0);

   TR_ASSERT(objectSize <= 0 || (objectSize & (TR::Compiler->om.sizeofReferenceAddress() - 1)) == 0, "object size causes an alignment problem");
   if (objectSize < 0 || (objectSize == 0 && !usingTLH))
      doInline = false;
   isVariableLen = (objectSize == 0);

   allocateSize = objectSize;

   if (opCode == TR::New)
      {
      if (comp->getOptions()->realTimeGC())
         {
         if (objectSize > fej9->getMaxObjectSizeForSizeClass())
            {
            doInline = false;
            }
         }
      }

   if (!isVariableLen)
      {
      allocateSize = (allocateSize + TR::Compiler->om.objectAlignmentInBytes() - 1) & (-TR::Compiler->om.objectAlignmentInBytes());
      }

   if (comp->compileRelocatableCode())
      {
      switch (opCode)
         {
         case TR::New: break;
         case TR::anewarray: break;
         case TR::newarray: break;
         default: doInline = false; break;
         }
      }

   static int count = 0;
   doInline = doInline && performTransformation(comp, "O^O <%3d> Inlining Allocation of %s [0x%p].\n", count++, node->getOpCode().getName(), node);

   if (doInline)
      {
      generateLabelInstruction(cg, TR::InstOpCode::label, node, generateLabelSymbol(cg));

      callLabel = generateLabelSymbol(cg);
      callReturnLabel = generateLabelSymbol(cg);
      doneLabel = generateLabelSymbol(cg);
      conditions = createConditionsAndPopulateVSXDeps(cg, 14);

      TR::Node *firstChild = node->getFirstChild();
      TR::Node *secondChild = NULL;

      if (opCode == TR::New)
         {
         // classReg is passed to the VM helper on the slow path and subsequently clobbered; copy it for later nodes if necessary
         classReg = cg->gprClobberEvaluate(firstChild);
         dataBegin = TR::Compiler->om.objectHeaderSizeInBytes();
         }
      else
         {
         TR_ASSERT(opCode == TR::newarray || opCode == TR::anewarray, "Bad opCode for VMnewEvaluator");
         isArray = true;
         if (generateArraylets || TR::Compiler->om.useHybridArraylets())
            {
            if (node->getOpCodeValue() == TR::newarray)
               elementSize = TR::Compiler->om.getSizeOfArrayElement(node);
            else if (comp->useCompressedPointers())
               elementSize = TR::Compiler->om.sizeofReferenceField();
            else
               elementSize = TR::Compiler->om.sizeofReferenceAddress();

            if (generateArraylets)
               dataBegin = fej9->getArrayletFirstElementOffset(elementSize, comp);
            else
               dataBegin = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
            static const char *p = feGetEnv("TR_TarokDataBeginBreak");
            if (p)
               TR_ASSERT(false, "Hitting Arraylet Data Begin Break");
            }
         else
            {
            dataBegin = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
            }
         secondChild = node->getSecondChild();
         enumReg = cg->evaluate(firstChild);

         insertType = (opCode == TR::newarray && secondChild->getDataType() == TR::Int32 && secondChild->getReferenceCount() == 1 && secondChild->getOpCode().isLoadConst());

         if (comp->compileRelocatableCode() && comp->getOption(TR_UseSymbolValidationManager))
            {
            // IMPORTANT: secondChild actually references the J9Class of the array *elements* rather
            // than the J9Class of the array itself; the new AOT infrastructure requires that
            // classReg contain the J9Class of the array, so we have to actually construct a new
            // loadaddr for that and then evaluate it instead of evaluating secondChild directly.
            // Note that the original secondChild *must still be evaluated* as it will be used in
            // the out-of-line code section.
            TR::StaticSymbol *classSymbol = TR::StaticSymbol::create(comp->trHeapMemory(), TR::Address);
            classSymbol->setStaticAddress(clazz);
            classSymbol->setClassObject();

            cg->evaluate(secondChild);
            secondChild = TR::Node::createWithSymRef(TR::loadaddr, 0,
               new (comp->trHeapMemory()) TR::SymbolReference(comp->getSymRefTab(), classSymbol));
            secondChild->incReferenceCount();

            classReg = cg->evaluate(secondChild);
            }
         else if (insertType)
            {
            classReg = cg->allocateRegister();
            }
         else
            {
            // classReg is passed to the VM helper on the slow path and subsequently clobbered; copy it for later nodes if necessary
            classReg = cg->gprClobberEvaluate(secondChild);
            }
         }
      TR::addDependency(conditions, classReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();

      if (secondChild == NULL)
         enumReg = cg->allocateRegister();
      TR::addDependency(conditions, enumReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0();

      condReg = cg->allocateRegister(TR_CCR);
      TR::addDependency(conditions, condReg, TR::RealRegister::cr0, TR_CCR, cg);
      tmp6Reg = cg->allocateRegister();
      TR::addDependency(conditions, tmp6Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      tmp4Reg = cg->allocateRegister();
      TR::addDependency(conditions, tmp4Reg, TR::RealRegister::gr11, TR_GPR, cg); // used by TR::PPCAllocPrefetchSnippet
      tmp5Reg = cg->allocateRegister();
      TR::addDependency(conditions, tmp5Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(5)->setExcludeGPR0();
      resReg = cg->allocateRegister();
      TR::addDependency(conditions, resReg, resultRealReg, TR_GPR, cg); // used by TR::PPCAllocPrefetchSnippet
      conditions->getPostConditions()->getRegisterDependency(6)->setExcludeGPR0();
      dataSizeReg = cg->allocateRegister();
      TR::addDependency(conditions, dataSizeReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(7)->setExcludeGPR0();
      tmp7Reg = cg->allocateRegister();
      TR::addDependency(conditions, tmp7Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(8)->setExcludeGPR0();
      tmp3Reg = cg->allocateRegister();
      TR::addDependency(conditions, tmp3Reg, TR::RealRegister::gr10, TR_GPR, cg); // used by TR::PPCAllocPrefetchSnippet

      TR::Instruction *firstInstruction = cg->getAppendInstruction();

      if (!(isDualTLH && fej9->tlhHasBeenCleared()) && needZeroInit)
         {
         zeroReg = cg->allocateRegister();
         TR::addDependency(conditions, zeroReg, TR::RealRegister::NoReg, TR_GPR, cg);
         }

      if (isVariableLen)
         allocateSize += dataBegin;

      //Here we set up backout paths if we overflow nonZeroTLH in genHeapAlloc.
      //If we overflow the nonZeroTLH, set the destination to the right VM runtime helper (eg jitNewObjectNoZeroInit, etc...)
      //The zeroed-TLH versions have their correct destinations already setup in TR_ByteCodeIlGenerator::genNew, TR_ByteCodeIlGenerator::genNewArray, TR_ByteCodeIlGenerator::genANewArray
      //To retrieve the destination node->getSymbolReference() is used below after genHeapAlloc.
      if (isDualTLH && node->canSkipZeroInitialization())
         {
         if (node->getOpCodeValue() == TR::New)
            node->setSymbolReference(comp->getSymRefTab()->findOrCreateNewObjectNoZeroInitSymbolRef(comp->getMethodSymbol()));
         else if (node->getOpCodeValue() == TR::newarray)
            node->setSymbolReference(comp->getSymRefTab()->findOrCreateNewArrayNoZeroInitSymbolRef(comp->getMethodSymbol()));
         else if (node->getOpCodeValue() == TR::anewarray)
            node->setSymbolReference(comp->getSymRefTab()->findOrCreateANewArrayNoZeroInitSymbolRef(comp->getMethodSymbol()));
         }

      // On return, zeroReg is set to 0 if needZeroInit is true, and
      // dataSizeReg is set to the size of data area if isVariableLen
      // is true, and either elementSize != 1 or needZeroInit is true
      // (and we only ever use dataSizeReg below in those cases).
      genHeapAlloc(node, iCursor, clazz, isVariableLen, enumReg, classReg, resReg, zeroReg, condReg, dataSizeReg, tmp5Reg, tmp4Reg, tmp3Reg, tmp6Reg, tmp7Reg, callLabel, doneLabel, allocateSize, elementSize,
            usingTLH, needZeroInit, conditions, cg);

      // Call out to VM runtime helper if needed.
      TR::Register *objReg = cg->allocateCollectedReferenceRegister();
      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::acall, objReg, callLabel, callReturnLabel, cg);
      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

      if (isArray)
         {
         // Align the array if necessary.
         if (doublewordAlign && !comp->getOptions()->realTimeGC())
            genAlignArray(node, iCursor, isVariableLen, resReg, objectSize, dataBegin, dataSizeReg, condReg, tmp5Reg, tmp4Reg, cg);
         if (comp->compileRelocatableCode() && (opCode == TR::anewarray || comp->getOption(TR_UseSymbolValidationManager)))
            genInitArrayHeader(node, iCursor, isVariableLen, clazz, classReg, resReg, zeroReg, condReg, enumReg, dataSizeReg, tmp5Reg, tmp4Reg, conditions, needZeroInit, cg);
         else
            genInitArrayHeader(node, iCursor, isVariableLen, clazz, NULL, resReg, zeroReg, condReg, enumReg, dataSizeReg,
                  tmp5Reg, tmp4Reg, conditions, needZeroInit, cg);

         if (generateArraylets)
            {
            //write arraylet pointer to object header
            static const char *p = feGetEnv("TR_TarokPreWritePointerBreak");
            if (p)
               generateInstruction(cg, TR::InstOpCode::bad, node);

            iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, resReg, dataBegin, iCursor);
            if (TR::Compiler->om.compressedReferenceShiftOffset() > 0)
               iCursor = generateShiftRightLogicalImmediateLong(cg, node, tmp4Reg, tmp4Reg, TR::Compiler->om.compressedReferenceShiftOffset(), iCursor);
            iCursor = generateMemSrc1Instruction(cg, (comp->target().is64Bit() && !comp->useCompressedPointers()) ? TR::InstOpCode::std : TR::InstOpCode::stw, node,
                  TR::MemoryReference::createWithDisplacement(cg, resReg, fej9->getFirstArrayletPointerOffset(comp), comp->useCompressedPointers() ? 4 : TR::Compiler->om.sizeofReferenceAddress()), tmp4Reg, iCursor);
            static const char *p1 = feGetEnv("TR_TarokPostWritePointerBreak");
            if (p1)
               generateInstruction(cg, TR::InstOpCode::bad, node);
            }
         }
      else
         {
         genInitObjectHeader(node, iCursor, clazz, classReg, resReg, zeroReg, condReg, tmp5Reg, tmp4Reg, packedRegOffs, conditions, needZeroInit, cg);
         }

      //Do not need zero init if using dualTLH now, unless explicit zeroInit is required.
      if (!(isDualTLH && fej9->tlhHasBeenCleared()) && needZeroInit && (!isConstantZeroLenArrayAlloc))
         {
         // Perform initialization if it is needed:
         //   1) Initialize certain array elements individually. This depends on the optimizer
         //      providing a "short" list of individual indices;
         //   2) Initialize the whole array:
         //      a) If the object size is constant, we can choose strategy depending on the
         //         size of the array. Using straight line of code, or unrolled loop;
         //      b) For variable length of array, do a counted loop;

         TR_ExtraInfoForNew *initInfo = node->getSymbolReference()->getExtraInfo();

         static bool disableFastArrayZeroInit = (feGetEnv("TR_DisableFastArrayZeroInit") != NULL);

         iCursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroReg, 0, iCursor);

         if (!node->canSkipZeroInitialization() && (initInfo == NULL || initInfo->numZeroInitSlots > 0))
            {
            if (!isVariableLen)
               {
               if (initInfo != NULL && initInfo->zeroInitSlots != NULL && initInfo->numZeroInitSlots <= 9 && objectSize <= UPPER_IMMED)
                  {
                  TR_BitVectorIterator bvi(*initInfo->zeroInitSlots);

                  if (comp->target().is64Bit())
                     {
                     int32_t lastNotInit = -1;
                     int32_t currElem = -1;

                     // Try to group set of words into smallest groupings using double-word stores (where possible)
                     while (bvi.hasMoreElements())
                        {
                        currElem = bvi.getNextElement();

                        if (lastNotInit == -1)
                           {
                           // currently looking at only one slot (either just starting, or just emitted double-word store in last iter)
                           lastNotInit = currElem;
                           }
                        else if (currElem == lastNotInit + 1)
                           {
                           // consecutive slots, can use std
                           iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::std, node, TR::MemoryReference::createWithDisplacement(cg, resReg, lastNotInit * 4 + dataBegin, 8),
                                 zeroReg, iCursor);

                           lastNotInit = -1;
                           }
                        else
                           {
                           // Two non-consecutive slots, use stw for earlier slot and keep looking for slot
                           // consecutive with the second slot currently held
                           iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, lastNotInit * 4 + dataBegin, 4),
                                 zeroReg, iCursor);

                           lastNotInit = currElem;
                           }
                        }

                     if (lastNotInit != -1)
                        {
                        // Last slot is not consecutive with other slots, hasn't been initialized yet
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, lastNotInit * 4 + dataBegin, 4),
                              zeroReg, iCursor);
                        }
                     }
                  else
                     {
                     while (bvi.hasMoreElements())
                        {
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node,
                              TR::MemoryReference::createWithDisplacement(cg, resReg, bvi.getNextElement() * 4 + dataBegin, 4), zeroReg, iCursor);
                        }
                     }
                  }
               else if (objectSize <= (TR::Compiler->om.sizeofReferenceAddress() * 12))
                  {
                  for (idx = dataBegin; idx < (objectSize - TR::Compiler->om.sizeofReferenceAddress()); idx += TR::Compiler->om.sizeofReferenceAddress())
                     {
                     iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, resReg, idx, TR::Compiler->om.sizeofReferenceAddress()), zeroReg,
                           iCursor);
                     }
                  if (idx + 4 == objectSize)
                     iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, resReg, idx, 4), zeroReg, iCursor);
                  else if (idx + 8 == objectSize)
                     iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::std, node, TR::MemoryReference::createWithDisplacement(cg, resReg, idx, 8), zeroReg, iCursor);
                  }
               else
                  {
                  static bool disableUnrollNewInitLoop1 = (feGetEnv("TR_DisableUnrollNewInitLoop1") != NULL);
                  if (!disableUnrollNewInitLoop1)
                     {
                     int32_t unrollFactor = 8;
                     int32_t width = comp->target().is64Bit() ? 8 : 4;
                     int32_t loopCount = (objectSize - dataBegin) / (unrollFactor * width);
                     int32_t res1 = (objectSize - dataBegin) % (unrollFactor * width);
                     int32_t residueCount = res1 / width;
                     int32_t res2 = res1 % width;
                     TR::LabelSymbol *loopStart = generateLabelSymbol(cg);

                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, resReg, dataBegin, iCursor);
                     if (loopCount > 0)
                        {
                        if (loopCount > 1)
                           {
                           iCursor = loadConstant(cg, node, loopCount, tmp5Reg, iCursor);
                           iCursor = generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, tmp5Reg, 0, iCursor);
                           iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, loopStart, iCursor);
                           }

                        for (int32_t i = 0; i < unrollFactor; i++)
                           iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, i * width, width), zeroReg, iCursor);

                        iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, tmp4Reg, tmp4Reg, (unrollFactor * width), iCursor);
                        if (loopCount > 1)
                           iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bdnz, node, loopStart, condReg, iCursor);
                        }

                     for (int32_t i = 0; i < residueCount; i++)
                        iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, i * width, width), zeroReg, iCursor);

                     if (res2 && residueCount != 0)
                        iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, tmp4Reg, tmp4Reg, residueCount * width, iCursor);

                     if (comp->target().is64Bit() && res2 >= 4) // Should only be 0 or 4 here
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 0, 4), zeroReg, iCursor);
                     }
                  else
                     {
                     int32_t loopCnt = (objectSize - dataBegin) >> 4;
                     int32_t residue = ((objectSize - dataBegin) >> 2) & 0x03;
                     TR::LabelSymbol *initLoop = generateLabelSymbol(cg);

                     iCursor = loadConstant(cg, node, loopCnt, tmp5Reg, iCursor);
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, resReg, dataBegin, iCursor);
                     iCursor = generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, tmp5Reg, 0, iCursor);
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, initLoop, iCursor);
                     iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 0, TR::Compiler->om.sizeofReferenceAddress()), zeroReg,
                           iCursor);
                     iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
                           TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, TR::Compiler->om.sizeofReferenceAddress(), TR::Compiler->om.sizeofReferenceAddress()), zeroReg, iCursor);
                     if (comp->target().is32Bit())
                        {
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 8, 4), zeroReg, iCursor);
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 12, 4), zeroReg, iCursor);
                        }
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, tmp4Reg, 16, iCursor);
                     iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bdnz, node, initLoop, condReg, iCursor);

                     idx = 0;
                     if (residue & 0x2)
                        {
                        iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 0, TR::Compiler->om.sizeofReferenceAddress()), zeroReg,
                              iCursor);

                        if (comp->target().is32Bit())
                           iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 4, 4), zeroReg, iCursor);

                        idx = 8;
                        }
                     if (residue & 0x1)
                        {
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, idx, 4), zeroReg, iCursor);
                        }
                     }
                  }
               }
            else // Init variable length array
               {
               static bool disableUnrollNewInitLoop2 = (feGetEnv("TR_DisableUnrollNewInitLoop2") != NULL);
               static bool disableNewZeroInit = (feGetEnv("TR_DisableNewZeroInit") != NULL);
               if (!disableUnrollNewInitLoop2)
                  {
                  int32_t unrollFactor = 8, i = 1;
                  int32_t width = comp->target().is64Bit() ? 8 : 4;
                  TR::LabelSymbol *loopLabel = generateLabelSymbol(cg);
                  TR::LabelSymbol *resLabel = generateLabelSymbol(cg);
                  TR::LabelSymbol *left4Label = generateLabelSymbol(cg);
                  TR::LabelSymbol *resLoopLabel = NULL;

                  if (!disableNewZeroInit)
                     resLoopLabel = generateLabelSymbol(cg);

                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp5Reg, resReg, (dataBegin - width), iCursor);
                  iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, tmp4Reg, tmp5Reg, dataSizeReg, iCursor);
                  iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp4Reg, tmp5Reg, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg, iCursor);

                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp6Reg, tmp4Reg, (int32_t)(-unrollFactor * width), iCursor);

                  // Switch order of compare to avoid FXU reject in P6 for later store instruction
                  iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp6Reg, tmp5Reg, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, resLabel, condReg, iCursor);

                  iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel, iCursor);
                  for (; i <= unrollFactor; i++)
                     iCursor = generateMemSrc1Instruction(cg, (width == 8) ? TR::InstOpCode::std : TR::InstOpCode::stw, node,
                           TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, i * width, width), zeroReg, iCursor);
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp5Reg, tmp5Reg, (int32_t)(unrollFactor * width), iCursor);

                  iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp6Reg, tmp5Reg, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, loopLabel, condReg, iCursor);

                  iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp4Reg, tmp5Reg, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg, iCursor);

                  iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, resLabel, iCursor);

                  if (!disableNewZeroInit)
                     {
                     // End pointer-4
                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp6Reg, tmp4Reg, -4, iCursor);

                     // Residue loop start
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, resLoopLabel, iCursor);

                     // If current pointer == end pointer - 4, there is 4 bytes left to write
                     // If current pointer > end pointer - 4, data is 8-byte divisible and we are done
                     iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp6Reg, tmp5Reg, iCursor);
                     iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, left4Label, condReg, iCursor);
                     iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, doneLabel, condReg, iCursor);

                     // Do 8-byte zero init and restart loop
                     iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, width, width), zeroReg, iCursor);

                     if (comp->target().is32Bit())
                        {
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, 8, 4), zeroReg, iCursor);
                        }

                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp5Reg, tmp5Reg, 8, iCursor);
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, resLoopLabel);

                     // Finish last 4 bytes if necessary
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, left4Label, iCursor);
                     iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, width, 4), zeroReg, iCursor);
                     }
                  else
                     {
                     /* Need this in 64-bit as well */
                     iCursor = generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, tmp6Reg, tmp5Reg, tmp4Reg, iCursor);
                     iCursor = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, condReg, tmp6Reg, 4, iCursor);
                     iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, left4Label, condReg, iCursor);

                     iCursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, 8, width), zeroReg, iCursor);

                     if (comp->target().is32Bit())
                        {
                        iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, 12, 4), zeroReg, iCursor);
                        }

                     iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp5Reg, tmp5Reg, 8, iCursor);
                     iCursor = generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, tmp4Reg, tmp5Reg, iCursor);
                     iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg, iCursor);
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::b, node, resLabel);

                     /* Need this in 64-bit as well */
                     iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, left4Label, iCursor);
                     iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp5Reg, 8, 4), zeroReg, iCursor);
                     }
                  }
               else
                  {
                  // Test for zero length array: the following two instructions will be combined
                  // to the record form by later pass.
                  iCursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, tmp5Reg, dataSizeReg, 30, 0x3FFFFFFF, iCursor);
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, tmp5Reg, 0, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, condReg, iCursor);
                  iCursor = generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, tmp5Reg, 0, iCursor);
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, resReg, dataBegin - 4, iCursor);

                  TR::LabelSymbol *initLoop = generateLabelSymbol(cg);
                  iCursor = generateLabelInstruction(cg, TR::InstOpCode::label, node, initLoop, iCursor);
                  iCursor = generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, tmp4Reg, 4, 4), zeroReg, iCursor);
                  iCursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, tmp4Reg, tmp4Reg, 4, iCursor);
                  iCursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::bdnz, node, initLoop, condReg, iCursor);
                  }
               }
            }
         }

      if (comp->compileRelocatableCode() && (opCode == TR::New || opCode == TR::anewarray))
         {
         firstInstruction = firstInstruction->getNext();
         TR_OpaqueClassBlock *classToValidate = clazz;

         TR_RelocationRecordInformation *recordInfo = (TR_RelocationRecordInformation *) comp->trMemory()->allocateMemory(sizeof(TR_RelocationRecordInformation), heapAlloc);
         recordInfo->data1 = allocateSize;
         recordInfo->data2 = node->getInlinedSiteIndex();
         recordInfo->data3 = (uintptr_t) callLabel;
         recordInfo->data4 = (uintptr_t) firstInstruction;

         TR::SymbolReference * classSymRef;
         TR_ExternalRelocationTargetKind reloKind;

         if (opCode == TR::New)
            {
            classSymRef = node->getFirstChild()->getSymbolReference();
            reloKind = TR_VerifyClassObjectForAlloc;
            }
         else
            {
            classSymRef = node->getSecondChild()->getSymbolReference();
            reloKind = TR_VerifyRefArrayForAlloc;

            if (comp->getOption(TR_UseSymbolValidationManager))
               classToValidate = comp->fej9()->getComponentClassFromArrayClass(classToValidate);
            }

         if (comp->getOption(TR_UseSymbolValidationManager))
            {
            TR_ASSERT_FATAL(classToValidate, "classToValidate should not be NULL, clazz=%p\n", clazz);
            recordInfo->data5 = (uintptr_t)classToValidate;
            }

         cg->addExternalRelocation(new (cg->trHeapMemory()) TR::BeforeBinaryEncodingExternalRelocation(firstInstruction, (uint8_t *) classSymRef, (uint8_t *) recordInfo, reloKind, cg),
                  __FILE__, __LINE__, node);
         }

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      // At this point the object is initialized and we can move it to a collected register.
      // The out of line path will do the same. Since the live ranges of resReg and objReg
      // do not overlap we want them to occupy the same real register so that this instruction
      // becomes a nop and can be optimized away.
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, objReg, resReg);
      conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(1, 1, cg->trMemory());
      TR::addDependency(conditions, objReg, resultRealReg, TR_GPR, cg);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, callReturnLabel, conditions);

      cg->stopUsingRegister(condReg);
      cg->stopUsingRegister(tmp3Reg);
      cg->stopUsingRegister(tmp4Reg);
      cg->stopUsingRegister(tmp5Reg);
      cg->stopUsingRegister(tmp6Reg);
      cg->stopUsingRegister(tmp7Reg);
      cg->stopUsingRegister(resReg);
      cg->stopUsingRegister(dataSizeReg);
      if (!(isDualTLH && fej9->tlhHasBeenCleared()) && needZeroInit)
         cg->stopUsingRegister(zeroReg);
      cg->decReferenceCount(firstChild);
      if (opCode == TR::New)
         {
         if (classReg != firstChild->getRegister())
            cg->stopUsingRegister(classReg);
         cg->stopUsingRegister(enumReg);
         }
      else
         {
         cg->decReferenceCount(secondChild);
         if (node->getSecondChild() != secondChild)
            cg->decReferenceCount(node->getSecondChild());
         if (classReg != secondChild->getRegister())
            cg->stopUsingRegister(classReg);
         }

      node->setRegister(objReg);
      return objReg;
      }
   else
      {
      TR::Node::recreate(node, TR::acall);
      callResult = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return (callResult);
      }
   }

// Special case evaluator for simple read monitors.
static bool simpleReadMonitor(TR::Node *node, TR::CodeGenerator *cg, TR::Node *objNode, TR::Register *objReg, TR::Register *objectClassReg, TR::Register *condReg,
      TR::Register *lookupOffsetReg)
   {
#if defined(J9VM_JIT_NEW_DUAL_HELPERS)
   // This needs to be updated to work with dual-mode helpers by not using snippets
   return false;
#endif
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   static char *disableSRM = feGetEnv("TR_disableSimpleReadMonitors");
   static char *nolwsync = feGetEnv("TR_noSRMlwsync");

   if (disableSRM)
      return false;

   // look for the special case of a read monitor sequence that protects
   // a single fixed-point load, ie:
   //   monenter (object)
   //   a simple form of iaload or iiload
   //   monexit (object)

   // note: before we make the following checks it is important that the
   // object on the monenter has been evaluated (currently done before the
   // call to this routine, and the result passed in as objReg)

   if (!node->isReadMonitor())
      return false;

   TR::TreeTop *nextTreeTop = cg->getCurrentEvaluationTreeTop()->getNextTreeTop();
   if (!nextTreeTop)
      return false;

   TR::TreeTop *liveMonitorExitStore = NULL;
   TR::TreeTop *liveMonitorEnterStore = NULL;

   if (nextTreeTop->getNode()->getOpCode().isStore() && nextTreeTop->getNode()->getSymbol()->holdsMonitoredObject())
      {
      liveMonitorEnterStore = nextTreeTop;
      nextTreeTop = nextTreeTop->getNextTreeTop();
      }

   if (nextTreeTop->getNode()->getOpCode().getOpCodeValue() == TR::monexitfence)
      {
      liveMonitorExitStore = nextTreeTop;
      nextTreeTop = nextTreeTop->getNextTreeTop();
      }

   TR::TreeTop *secondNextTreeTop = nextTreeTop->getNextTreeTop();
   if (!secondNextTreeTop)
      return false;

   if (secondNextTreeTop->getNode()->getOpCode().getOpCodeValue() == TR::monexitfence)
      {
      liveMonitorExitStore = secondNextTreeTop;
      secondNextTreeTop = secondNextTreeTop->getNextTreeTop();
      }

   // check that the second node after the monent is the matching monexit
   TR::Node *secondNextTopNode = secondNextTreeTop->getNode();
   if (!secondNextTopNode)
      return false;
   if (secondNextTopNode->getOpCodeValue() == TR::treetop)
      secondNextTopNode = secondNextTopNode->getFirstChild();
   if (secondNextTopNode->getOpCodeValue() != TR::monexit || secondNextTopNode->getFirstChild() != objNode)
      return false;

   // check that the first node after the monent is a simple load
   TR::Node *nextTopNode = nextTreeTop->getNode();
   if (!nextTopNode)
      return false;
   if (nextTopNode->getOpCodeValue() == TR::treetop || nextTopNode->getOpCodeValue() == TR::iRegStore)
      nextTopNode = nextTopNode->getFirstChild();
   if (!TR::LoadStoreHandler::isSimpleLoad(cg, nextTopNode))
      return false;
   // possible TODO: expand the set of load types we can handle
   if (nextTopNode->getOpCodeValue() != TR::aloadi && nextTopNode->getOpCodeValue() != TR::iloadi)
      {
      // printf("Rejecting read-only monitor on load type in %s\n", comp->getCurrentMethod()->signature());
      return false;
      }
   // possible TODO: expand the complexity of loads we can handle
   // iaload and iiload are indirect and have a child
   // if we don't need to evaluate that child then the iaload or iiload
   // consists of a single hardware instruction and satisfies our current
   // constraint of simple
   if (!nextTopNode->getFirstChild()->getRegister())
      {
      // printf("Rejecting read-only monitor on complex load in %s\n", comp->getCurrentMethod()->signature());
      return false;
      }

   // end of checks, we can handle this case
   // printf("Success on read-only monitor optimization in %s\n", comp->getCurrentMethod()->signature());

   // RAS: dump out the load and monexit trees, since we are effectively
   // evaluating them here
   if (comp->getOption(TR_TraceCG) || debug("traceGRA"))
      {
      trfprintf(comp->getOutFile(), "\n");
      comp->getDebug()->dumpSingleTreeWithInstrs(nextTreeTop, NULL, true, false, true, comp->getOption(TR_TraceRegisterPressureDetails));
      trfprintf(comp->getOutFile(), "\n");
      comp->getDebug()->dumpSingleTreeWithInstrs(secondNextTreeTop, NULL, true, false, true, comp->getOption(TR_TraceRegisterPressureDetails));
      trfflush(comp->getOutFile());
      }

   if (liveMonitorEnterStore)
      {
      TR::TreeTop *oldCur = cg->getCurrentEvaluationTreeTop();
      cg->setCurrentEvaluationTreeTop(liveMonitorEnterStore);
      cg->evaluate(liveMonitorEnterStore->getNode());
      cg->setCurrentEvaluationTreeTop(oldCur);
      }
   // check the TR::monexit to see if an lwsync is required
   // or whether to suppress the lwsync for performance analysis purposes
   bool needlwsync = comp->target().isSMP() && !secondNextTopNode->canSkipSync() && nolwsync == NULL;

   // generate code for the combined monenter, load and monexit:
   //    <monitor object evaluation code>
   //    <TR_MemoryReference evaluation code for load>
   // (see design 908 for more information on the code sequences used)
   // if running on a POWER4/5 and the load is from the monitor object:
   //    (altsequence)
   //   if an lwsync is needed:
   //          li     offsetReg, offset_of_monitor_word
   //       loopLabel:
   //          lwarx  monitorReg, [objReg, offsetReg]
   //          cmpli  cndReg, monitorReg, 0
   //          bne    cndReg, recurCheckLabel
   //          stwcx  [objReg, offsetReg], metaReg
   //          bne-   loopLabel
   //          or     loadBaseReg,loadBaseReg,monitorReg
   //          <load>
   //          lwsync
   //          stw    offsetReg(objReg), monitorReg
   //       restartLabel:
   //   else (no lwsync):
   //          li     offsetReg, offset_of_monitor_word
   //       loopLabel:
   //          lwarx  monitorReg, [objReg, offsetReg]
   //          cmpli  cndReg, monitorReg, 0
   //          bne    cndReg, recurCheckLabel
   //          stwcx  [objReg, offsetReg], metaReg
   //          bne-   loopLabel
   //          or     loadBaseReg,loadBaseReg,monitorReg
   //          <load>
   //          xor    monitorReg,loadResultReg,loadResultReg
   //          stw    offsetReg(objReg), monitorReg
   //       restartLabel:
   // else:
   //   if an lwsync is needed:
   //          li     offsetReg, offset_of_monitor_word
   //       loopLabel:
   //          lwarx  monitorReg, [objReg, offsetReg]
   //          cmpli  cndReg, monitorReg, 0
   //          bne    cndReg, recurCheckLabel
   //          or     loadBaseReg,loadBaseReg,monitorReg
   //          <load>
   //          lwsync
   //          stwcx  [objReg, offsetReg], monitorReg
   //          bne-   loopLabel
   //       restartLabel:
   //   else (no lwsync):
   //          li     offsetReg, offset_of_monitor_word
   //       loopLabel:
   //          lwarx  monitorReg, [objReg, offsetReg]
   //          cmpli  cndReg, monitorReg, 0
   //          bne    cndReg, recurCheckLabel
   //          or     loadBaseReg,loadBaseReg,monitorReg
   //          <load>
   //          xor    monitorReg,loadResultReg,loadResultReg
   //          stwcx  [objReg, offsetReg], monitorReg
   //          bne-   loopLabel
   //       restartLabel:
   // === SNIPPET === (generated later)
   //   recurCheckLabel:
   //      rlwinm monitorReg, monitorReg, 0, THREAD_MASK_CONST
   //      cmp    cndReg, metaReg, monitorReg
   //      bne    cndReg, slowPath
   //      <load>
   //      b      restartLabel
   //   slowPath:
   //      bl     monitorEnterHelper
   //      <load>
   //      bl     monitorExitHelper
   //      b      restartLabel

   TR::RegisterDependencyConditions *conditions;
   TR::Register *metaReg, *monitorReg, *cndReg, *offsetReg;
   TR::InstOpCode::Mnemonic opCode;
   int32_t lockSize;

   int32_t numDeps = 6;
   if (objectClassReg)
      numDeps = numDeps + 2;

   if (lookupOffsetReg)
      numDeps = numDeps + 1;

   conditions = createConditionsAndPopulateVSXDeps(cg, numDeps);

   monitorReg = cg->allocateRegister();
   offsetReg = cg->allocateRegister();
   cndReg = cg->allocateRegister(TR_CCR);
   TR::addDependency(conditions, monitorReg, TR::RealRegister::gr11, TR_GPR, cg);
   TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);

   // the following code is derived from the iaload and iiload evaluators
   TR::Register *loadResultReg;
   OMR::Power::NodeMemoryReference tempMR;
   if (nextTopNode->getOpCodeValue() == TR::aloadi)
      loadResultReg = cg->allocateCollectedReferenceRegister();
   else
      loadResultReg = cg->allocateRegister();
   if (nextTopNode->getSymbolReference()->getSymbol()->isInternalPointer())
      {
      loadResultReg->setPinningArrayPointer(nextTopNode->getSymbolReference()->getSymbol()->castToInternalPointerAutoSymbol()->getPinningArrayPointer());
      loadResultReg->setContainsInternalPointer();
      }
   nextTopNode->setRegister(loadResultReg);
   TR::InstOpCode::Mnemonic loadOpCode;
   if (nextTopNode->getOpCodeValue() == TR::aloadi && comp->target().is64Bit())
      {
      if (TR::Compiler->om.compressObjectReferences() && nextTopNode->getSymbol()->isClassObject())
         {
         tempMR = TR::LoadStoreHandler::generateSimpleLoadMemoryReference(cg, nextTopNode, 4);
         loadOpCode = TR::InstOpCode::lwz;
         }
      else
         {
         tempMR = TR::LoadStoreHandler::generateSimpleLoadMemoryReference(cg, nextTopNode, 8);
         loadOpCode = TR::InstOpCode::ld;
         }
      }
   else
      {
      tempMR = TR::LoadStoreHandler::generateSimpleLoadMemoryReference(cg, nextTopNode, 4);
      loadOpCode = TR::InstOpCode::lwz;
      }
   // end of code derived from the iaload and iiload evaluators

   TR::addDependency(conditions, loadResultReg, TR::RealRegister::NoReg, TR_GPR, cg);
   if (tempMR.getMemoryReference()->getBaseRegister() != objReg)
      {
      TR::addDependency(conditions, tempMR.getMemoryReference()->getBaseRegister(), TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPreConditions()->getRegisterDependency(4)->setExcludeGPR0();
      conditions->getPostConditions()->getRegisterDependency(4)->setExcludeGPR0();
      }
   TR::addDependency(conditions, objReg, TR::RealRegister::gr3, TR_GPR, cg);

   TR::Register *baseReg = objReg;
   if (objectClassReg)
      baseReg = objectClassReg;

   bool altsequence = (comp->target().cpu.is(OMR_PROCESSOR_PPC_GP) || comp->target().cpu.is(OMR_PROCESSOR_PPC_GR)) && tempMR.getMemoryReference()->getBaseRegister() == objReg;

   TR::LabelSymbol *loopLabel, *restartLabel, *recurCheckLabel, *monExitCallLabel;
   loopLabel = generateLabelSymbol(cg);
   restartLabel = generateLabelSymbol(cg);
   recurCheckLabel = generateLabelSymbol(cg);
   monExitCallLabel = generateLabelSymbol(cg);

   metaReg = cg->getMethodMetaDataRegister();

   if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
      {
      opCode = TR::InstOpCode::ldarx;
      lockSize = 8;
      }
   else
      {
      opCode = TR::InstOpCode::lwarx;
      lockSize = 4;
      }

   if (objectClassReg)
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, offsetReg, 0);
   else
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, offsetReg, fej9->getByteOffsetToLockword((TR_OpaqueClassBlock *) cg->getMonClass(node)));
   generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
   generateTrg1MemInstruction(cg, opCode, node, monitorReg, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize));
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, monitorReg, 0);
   TR::Instruction *gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, recurCheckLabel, cndReg);
   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);

   if (altsequence)
      {
      if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
         opCode = TR::InstOpCode::stdcx_r;
      else
         opCode = TR::InstOpCode::stwcx_r;
      generateMemSrc1Instruction(cg, opCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize), metaReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, cndReg);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::OR, node, tempMR.getMemoryReference()->getBaseRegister(), tempMR.getMemoryReference()->getBaseRegister(), monitorReg);
      generateTrg1MemInstruction(cg, loadOpCode, node, loadResultReg, tempMR.getMemoryReference());
      if (needlwsync)
         generateInstruction(cg, TR::InstOpCode::lwsync, node);
      else
         generateTrg1Src2Instruction(cg, TR::InstOpCode::XOR, node, monitorReg, loadResultReg, loadResultReg);
      if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
         opCode = TR::InstOpCode::std;
      else
         opCode = TR::InstOpCode::stw;
      generateMemSrc1Instruction(cg, opCode, node,
            TR::MemoryReference::createWithDisplacement(cg, baseReg, objectClassReg ? 0 : fej9->getByteOffsetToLockword((TR_OpaqueClassBlock *) cg->getMonClass(node)), lockSize), monitorReg);
      }
   else
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::OR, node, tempMR.getMemoryReference()->getBaseRegister(), tempMR.getMemoryReference()->getBaseRegister(), monitorReg);
      generateTrg1MemInstruction(cg, loadOpCode, node, loadResultReg, tempMR.getMemoryReference());
      if (needlwsync)
         generateInstruction(cg, TR::InstOpCode::lwsync, node);
      else
         generateTrg1Src2Instruction(cg, TR::InstOpCode::XOR, node, monitorReg, loadResultReg, loadResultReg);
      if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
         opCode = TR::InstOpCode::stdcx_r;
      else
         opCode = TR::InstOpCode::stwcx_r;
      generateMemSrc1Instruction(cg, opCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize), monitorReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, cndReg);
      }

   if (objectClassReg)
      TR::addDependency(conditions, objectClassReg, TR::RealRegister::NoReg, TR_GPR, cg);

   if (lookupOffsetReg)
      TR::addDependency(conditions, lookupOffsetReg, TR::RealRegister::NoReg, TR_GPR, cg);

   if (condReg)
      TR::addDependency(conditions, condReg, TR::RealRegister::cr1, TR_CCR, cg);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, restartLabel, conditions);

   conditions->stopUsingDepRegs(cg, objReg, loadResultReg);
   cg->decReferenceCount(objNode);

   TR::Snippet *snippet = new (cg->trHeapMemory()) TR::PPCReadMonitorSnippet(cg, node, secondNextTopNode, recurCheckLabel, monExitCallLabel, restartLabel, loadOpCode,
         tempMR.getMemoryReference()->getOffset(), objectClassReg);
   cg->addSnippet(snippet);

   // the load and monexit trees need reference count adjustments and
   // must not be evaluated
   tempMR.decReferenceCounts(cg);
   cg->decReferenceCount(nextTopNode);
   if (secondNextTopNode == secondNextTreeTop->getNode())
      cg->recursivelyDecReferenceCount(secondNextTopNode->getFirstChild());
   else
      cg->recursivelyDecReferenceCount(secondNextTopNode);

   if (liveMonitorExitStore)
      {
      TR::TreeTop *prev = liveMonitorExitStore->getPrevTreeTop();
      TR::TreeTop *next = liveMonitorExitStore->getNextTreeTop();
      prev->join(next);

      next = secondNextTreeTop->getNextTreeTop();
      secondNextTreeTop->join(liveMonitorExitStore);
      liveMonitorExitStore->join(next);
      }

   cg->setCurrentEvaluationTreeTop(secondNextTreeTop);

   return true;
   }

void J9::Power::TreeEvaluator::generateCheckForValueMonitorEnterOrExit(TR::Node *node, TR::LabelSymbol *helperCallLabel, TR::Register *objReg, TR::Register *objectClassReg, TR::Register *temp1Reg, TR::Register *temp2Reg, TR::Register *condReg, TR::CodeGenerator *cg, int32_t classFlag)
{
   //get class of object
   generateLoadJ9Class(node, objectClassReg, objReg, cg);

   //get memory reference to class flags
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(cg->fe());
   TR::MemoryReference *classFlagsMemRef = TR::MemoryReference::createWithDisplacement(cg, objectClassReg, static_cast<uintptr_t>(fej9->getOffsetOfClassFlags()), 4);

   generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, temp1Reg, classFlagsMemRef);

   loadConstant(cg, node, classFlag, temp2Reg);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::and_r, node, temp1Reg, temp1Reg, temp2Reg);

   //If obj is value type or value based class instance, call VM helper and throw IllegalMonitorState exception, else continue as usual
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, helperCallLabel, condReg);
}

TR::Register *J9::Power::TreeEvaluator::VMmonentEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   int32_t lwOffset = fej9->getByteOffsetToLockword((TR_OpaqueClassBlock *) cg->getMonClass(node));
   TR_ASSERT(lwOffset>=LOWER_IMMED && lwOffset<=UPPER_IMMED, "Need re-work on using lwOffset.");
   TR_YesNoMaybe isMonitorValueBasedOrValueType = cg->isMonitorValueBasedOrValueType(node);

   if (comp->getOption(TR_MimicInterpreterFrameShape) ||
         (comp->getOption(TR_FullSpeedDebug) && node->isSyncMethodMonitor()) ||
         (isMonitorValueBasedOrValueType == TR_yes) ||
         comp->getOption(TR_DisableInlineMonEnt))
      {
      TR::ILOpCodes opCode = node->getOpCodeValue();
      TR::Node::recreate(node, TR::call);
      TR::Register *targetRegister = directCallEvaluator(node, cg);
      TR::Node::recreate(node, opCode);
      return targetRegister;
      }

   int32_t numDeps = 6;

   TR::Node *objNode = node->getFirstChild();
   TR::Register *objReg = cg->evaluate(objNode);

   TR::Register *monitorReg = cg->allocateRegister();
   TR::Register *offsetReg = cg->allocateRegister();
   TR::Register *tempReg = cg->allocateRegister();

   TR::Register *objectClassReg = cg->allocateRegister();
   TR::Register *lookupOffsetReg = NULL;

   TR::Register *condReg = cg->allocateRegister(TR_CCR);
   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   TR::Register *baseReg = objReg;

   if (lwOffset <= 0)
      {
      if (comp->getOption(TR_EnableMonitorCacheLookup))
         {
         lookupOffsetReg = cg->allocateRegister();
         numDeps++;
         }
      }

   TR::RegisterDependencyConditions *conditions;
   conditions = createConditionsAndPopulateVSXDeps(cg, numDeps);

   TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   TR::addDependency(conditions, monitorReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);

   TR::addDependency(conditions, tempReg, TR::RealRegister::NoReg, TR_GPR, cg);

   TR::addDependency(conditions, objectClassReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();

   if (lookupOffsetReg)
      {
      TR::addDependency(conditions, lookupOffsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPreConditions()->getRegisterDependency(conditions->getAddCursorForPre() - 1)->setExcludeGPR0();
      conditions->getPostConditions()->getRegisterDependency(conditions->getAddCursorForPost() - 1)->setExcludeGPR0();
      }

   TR::addDependency(conditions, condReg, TR::RealRegister::cr0, TR_CCR, cg);

   TR::LabelSymbol *callLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *monitorLookupCacheLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *fallThruFromMonitorLookupCacheLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel, NULL);
   startLabel->setStartInternalControlFlow();

   //If object is not known to be value type or value based class at compile time, check at run time
   if (isMonitorValueBasedOrValueType == TR_maybe)
      {
      generateCheckForValueMonitorEnterOrExit(node, callLabel, objReg, objectClassReg, tempReg, offsetReg, condReg, cg, J9_CLASS_DISALLOWS_LOCKING_FLAGS);
      }

   bool simpleLocking = false;

   if (lwOffset <= 0)
      {
      generateLoadJ9Class(node, objectClassReg, objReg, cg);

      int32_t offsetOfLockOffset = offsetof(J9Class, lockOffset);
      TR::MemoryReference *tempMR = TR::MemoryReference::createWithDisplacement(cg, objectClassReg, offsetOfLockOffset, TR::Compiler->om.sizeofReferenceAddress());
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, objectClassReg, tempMR);

      generateTrg1Src1ImmInstruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, condReg, objectClassReg, 0);

      if (comp->getOption(TR_EnableMonitorCacheLookup))
         {
         lwOffset = 0;
         generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, monitorLookupCacheLabel, condReg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, objectClassReg, objReg, objectClassReg);
         generateLabelInstruction(cg, TR::InstOpCode::b, node, fallThruFromMonitorLookupCacheLabel);

         generateLabelInstruction(cg, TR::InstOpCode::label, node, monitorLookupCacheLabel);

         int32_t offsetOfMonitorLookupCache = offsetof(J9VMThread, objectMonitorLookupCache);

         generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, lookupOffsetReg, objReg);

         int32_t t = trailingZeroes(TR::Compiler->om.objectAlignmentInBytes());

         if (comp->target().is64Bit())
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::sradi, node, lookupOffsetReg, lookupOffsetReg, t);
         else
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::srawi, node, lookupOffsetReg, lookupOffsetReg, t);

         J9JavaVM * jvm = fej9->getJ9JITConfig()->javaVM;
         if (comp->target().is64Bit())
            simplifyANDRegImm(node, lookupOffsetReg, lookupOffsetReg, (int64_t) J9VMTHREAD_OBJECT_MONITOR_CACHE_SIZE - 1, cg, objNode);
         else
            simplifyANDRegImm(node, lookupOffsetReg, lookupOffsetReg, J9VMTHREAD_OBJECT_MONITOR_CACHE_SIZE - 1, cg, objNode);

         if (comp->target().is64Bit())
            generateShiftLeftImmediateLong(cg, node, lookupOffsetReg, lookupOffsetReg, trailingZeroes((int32_t) TR::Compiler->om.sizeofReferenceField()));
         else
            generateShiftLeftImmediate(cg, node, lookupOffsetReg, lookupOffsetReg, trailingZeroes((int32_t) TR::Compiler->om.sizeofReferenceField()));

         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, lookupOffsetReg, lookupOffsetReg, offsetOfMonitorLookupCache);

         generateTrg1MemInstruction(cg, (comp->target().is64Bit() && fej9->generateCompressedLockWord()) ? TR::InstOpCode::lwz :TR::InstOpCode::Op_load, node, objectClassReg,
               TR::MemoryReference::createWithIndexReg(cg, metaReg, lookupOffsetReg, TR::Compiler->om.sizeofReferenceField()));

         generateTrg1Src1ImmInstruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, condReg, objectClassReg, 0);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, callLabel, condReg);

         int32_t offsetOfMonitor = offsetof(J9ObjectMonitor, monitor);
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, lookupOffsetReg,
               TR::MemoryReference::createWithDisplacement(cg, objectClassReg, offsetOfMonitor, TR::Compiler->om.sizeofReferenceAddress()));

         int32_t offsetOfUserData = offsetof(J9ThreadAbstractMonitor, userData);
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, lookupOffsetReg,
               TR::MemoryReference::createWithDisplacement(cg, lookupOffsetReg, offsetOfUserData, TR::Compiler->om.sizeofReferenceAddress()));

         generateTrg1Src2Instruction(cg, comp->target().is64Bit() ? TR::InstOpCode::cmp8 : TR::InstOpCode::cmp4, node, condReg, lookupOffsetReg, objReg);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);

         int32_t offsetOfAlternateLockWord = offsetof(J9ObjectMonitor, alternateLockword);

         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, objectClassReg, objectClassReg, offsetOfAlternateLockWord);

         generateLabelInstruction(cg, TR::InstOpCode::label, node, fallThruFromMonitorLookupCacheLabel);
         }
      else
         {
         generateConditionalBranchInstruction(cg, TR::InstOpCode::ble, node, callLabel, condReg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, objectClassReg, objReg, objectClassReg);
         }

      simpleLocking = true;
      lwOffset = 0;
      baseReg = objectClassReg;
      }

   /* If the -XX:-GlobalLockReservation option is set, try to use the original aggressive reserved locking code path. */
   if (!fej9->isEnableGlobalLockReservationSet())
      {
      bool reserveLocking = false, normalLockWithReservationPreserving = false;

      if (!simpleLocking && comp->getOption(TR_ReservingLocks))
         TR::TreeEvaluator::evaluateLockForReservation(node, &reserveLocking, &normalLockWithReservationPreserving, cg);

      if (reserveLocking)
         return reservationLockEnter(node, lwOffset, cg, conditions, baseReg, monitorReg, offsetReg, tempReg, condReg, callLabel);

      if (!simpleLocking && !reserveLocking && !normalLockWithReservationPreserving && simpleReadMonitor(node, cg, objNode, objReg, objectClassReg, condReg, lookupOffsetReg))
         return NULL;
      }

   int32_t lockSize;
   TR::InstOpCode::Mnemonic loadOpCode, storeOpCode, reservedLoadOpCode, conditionalStoreOpCode, compareLogicalOpCode, compareLogicalImmOpCode;

   if (comp->target().is64Bit() && !fej9->generateCompressedLockWord())
      {
      lockSize = 8;
      loadOpCode = TR::InstOpCode::ld;
      storeOpCode = TR::InstOpCode::std;
      reservedLoadOpCode = TR::InstOpCode::ldarx;
      conditionalStoreOpCode = TR::InstOpCode::stdcx_r;
      compareLogicalOpCode = TR::InstOpCode::cmpl8;
      compareLogicalImmOpCode = TR::InstOpCode::cmpli8;
      }
   else
      {
      lockSize = 4;
      loadOpCode = TR::InstOpCode::lwz;
      storeOpCode = TR::InstOpCode::stw;
      reservedLoadOpCode = TR::InstOpCode::lwarx;
      conditionalStoreOpCode = TR::InstOpCode::stwcx_r;
      compareLogicalOpCode = TR::InstOpCode::cmpl4;
      compareLogicalImmOpCode = TR::InstOpCode::cmpli4;
      }

   // full codegen support for read monitors is not enabled, pending performance tuning
   /*
    * Read only locks do not support either the Reserved bit nor the Learning bit.
    * They are disabled until the code path is updated.
    */
   if (true || !ppcSupportsReadMonitors || !node->isReadMonitor())
      {
      /*
       * JIT locking fast path checks:
       * 1. Compare lockword to 0. If equal, use CAS to set TID in lockword.
       * 2. Check if TID matches, highest RC bit is 0, Learning bit is clear and Inflated bit is clear. If so, increment RC.
       * 3. All fast path checks failed so go to VM.
       *
       * Check 1 catches Flat-Unlocked.
       * Check 2 catches Reserved-Unlocked, Reserved-Locked and Flat-Locked.
       * Check 3 catches New-PreLearning, New-AutoReserve, Learning-Unlocked, Learning-Locked and Inflated and sends them to the VM.
       *
       *
       *    ld/lwz monitorReg, lwOffset(baseReg)
       *    cmpli  cr0, monitorReg, 0
       *    bne    incrementCheckLabel
       *    li     offsetReg, lwOffset
       *
       * loopLabel:
       *    larx   monitorReg, [baseReg, offsetReg]
       *    cmpli  cr0, monitorReg, 0
       *    bne    callLabel
       *    stcx.  metaReg, [baseReg, offsetReg]
       *    bne    loopLabel
       *    isync
       *    b      doneLabel
       *
       * incrementCheckLabel:
       *    li     tempReg, LOCK_NON_PRIMITIVE_ENTER_IGNORE_MASK
       *    andc   tempReg, monitorReg, tempReg
       *    cmpl   cr0, tempReg, metaReg
       *    bne    callLabel
       *    addi   monitorReg, monitorReg, LOCK_INC_DEC_VALUE
       *    st     monitorReg, lwOffset(baseReg)
       *
       * doneLabel:
       * callReturnLabel:
       * === OUT OF LINE ===
       * callLabel:
       *    bl    jitMonitorEntry
       *    b     callReturnLabel
       */

      TR::LabelSymbol *loopLabel, *incrementCheckLabel, *doneLabel;
      loopLabel = generateLabelSymbol(cg);
      incrementCheckLabel = generateLabelSymbol(cg);
      doneLabel = generateLabelSymbol(cg);

      generateTrg1MemInstruction(cg, loadOpCode, node, monitorReg, TR::MemoryReference::createWithDisplacement(cg, baseReg, lwOffset, lockSize));

      generateTrg1Src1ImmInstruction(cg, compareLogicalImmOpCode, node, condReg, monitorReg, 0);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, incrementCheckLabel, condReg);
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, offsetReg, lwOffset);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
      generateTrg1MemInstruction(cg, reservedLoadOpCode, PPCOpProp_LoadReserveExclusiveAccess, node, monitorReg, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize));
      generateTrg1Src1ImmInstruction(cg, compareLogicalImmOpCode, node, condReg, monitorReg, 0);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);
      generateMemSrc1Instruction(cg, conditionalStoreOpCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize), metaReg);

      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, condReg);

      if (comp->target().isSMP())
         {
         // either an isync or a sync can be used here to prevent any
         // following loads from executing out-of-order
         // on nstar and pulsar a sync is cheaper, while an isync is
         // cheaper on other processors
         if (comp->target().cpu.is(OMR_PROCESSOR_PPC_NSTAR) || comp->target().cpu.is(OMR_PROCESSOR_PPC_PULSAR))
            generateInstruction(cg, TR::InstOpCode::sync, node);
         else
            generateInstruction(cg, TR::InstOpCode::isync, node);
         }
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, incrementCheckLabel);
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, LOCK_NON_PRIMITIVE_ENTER_IGNORE_MASK);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, tempReg, monitorReg, tempReg);
      generateTrg1Src2Instruction(cg, compareLogicalOpCode, node, condReg, tempReg, metaReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, monitorReg, monitorReg, LOCK_INC_DEC_VALUE);
      generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, baseReg, lwOffset, lockSize), monitorReg);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      doneLabel->setEndInternalControlFlow();

      TR::LabelSymbol *callReturnLabel = generateLabelSymbol(cg);

      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, callReturnLabel, cg);
      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, callReturnLabel);

      conditions->stopUsingDepRegs(cg, objReg);
      cg->decReferenceCount(objNode);
      }
   else
      {
      // read-only locks, readerReg = 0x0000 0000
      //    li     offsetReg, offset_of_monitor_word
      // loopLabel:
      //    larx   monitorReg, [baseReg, offsetReg]
      //    rlwinm tempReg, monitorReg, 0, 0xFFFFFF83
      //    cmpi   cr0, tempReg, 0x0
      //    bne    callLabel
      //    addi   monitorReg, monitorReg, 4
      //    stcx.  [baseReg, offsetReg], monitorReg
      //    bne    loopLabel
      //    isync
      // doneLabel:
      // callReturnLabel:
      // === OUT OF LINE ===
      // callLabel:
      //    bl     jitMonitorEntry
      //    b      callReturnLabel

      TR::LabelSymbol *doneLabel, *loopLabel;
      doneLabel = generateLabelSymbol(cg);
      loopLabel = generateLabelSymbol(cg);

      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, offsetReg, lwOffset);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel, conditions);
      generateTrg1MemInstruction(cg, reservedLoadOpCode, PPCOpProp_LoadReserveExclusiveAccess, node, monitorReg,
            TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize));
      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, tempReg, monitorReg, 0, 0xFFFFFF80);
      generateTrg1Src1ImmInstruction(cg, compareLogicalImmOpCode, node, condReg, tempReg, 0);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, callLabel, condReg);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, monitorReg, monitorReg, 4);
      generateMemSrc1Instruction(cg, conditionalStoreOpCode, node, TR::MemoryReference::createWithIndexReg(cg, baseReg, offsetReg, lockSize), monitorReg);

      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, condReg);

      if (comp->target().isSMP())
         {
         // either an isync or a sync can be used here to prevent any
         // following loads from executing out-of-order
         // on nstar and pulsar a sync is cheaper, while an isync is
         // cheaper on other processors
         if (comp->target().cpu.is(OMR_PROCESSOR_PPC_NSTAR) || comp->target().cpu.is(OMR_PROCESSOR_PPC_PULSAR))
            generateInstruction(cg, TR::InstOpCode::sync, node);
         else
            generateInstruction(cg, TR::InstOpCode::isync, node);
         }
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      doneLabel->setEndInternalControlFlow();

      TR::LabelSymbol *callReturnLabel = generateLabelSymbol(cg);

      TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::call, NULL, callLabel, callReturnLabel, cg);
      cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, callReturnLabel);

      conditions->stopUsingDepRegs(cg, objReg);
      cg->decReferenceCount(objNode);
      }

   return (NULL);
   }

TR::Register *J9::Power::TreeEvaluator::VMarrayCheckEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::Register *obj1Reg, *obj2Reg, *tmp1Reg, *tmp2Reg, *cndReg;
   TR::LabelSymbol *doneLabel, *snippetLabel;
   TR::Instruction *gcPoint;
   TR::Snippet *snippet;
   TR::RegisterDependencyConditions *conditions;
   int32_t depIndex;
   TR::Compilation* comp = cg->comp();

   obj1Reg = cg->evaluate(node->getFirstChild());
   obj2Reg = cg->evaluate(node->getSecondChild());
   doneLabel = generateLabelSymbol(cg);
   conditions = createConditionsAndPopulateVSXDeps(cg, 5);
   depIndex = 0;
   nonFixedDependency(conditions, obj1Reg, &depIndex, TR_GPR, true, cg);
   nonFixedDependency(conditions, obj2Reg, &depIndex, TR_GPR, true, cg);
   tmp1Reg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, true, cg);
   tmp2Reg = nonFixedDependency(conditions, NULL, &depIndex, TR_GPR, false, cg);
   cndReg = cg->allocateRegister(TR_CCR);
   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);

   // We have a unique snippet sharing arrangement in this code sequence.
   // It is not generally applicable for other situations.
   snippetLabel = NULL;

   // Same array, we are done.
   //
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, obj1Reg, obj2Reg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, cndReg);

   // If we know nothing about either object, test object1 first. It has to be an array.
   //
   if (!node->isArrayChkPrimitiveArray1() && !node->isArrayChkReferenceArray1() && !node->isArrayChkPrimitiveArray2() && !node->isArrayChkReferenceArray2())
      {

      generateLoadJ9Class(node, tmp1Reg, obj1Reg, cg);

      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, tmp1Reg, (int32_t) offsetof(J9Class, classDepthAndFlags), 4));

      loadConstant(cg, node, (int32_t) J9AccClassRAMArray, tmp2Reg);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, tmp2Reg, tmp1Reg, tmp2Reg);

      generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cndReg, tmp2Reg, NULLVALUE);

      snippetLabel = generateLabelSymbol(cg);
      gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, snippetLabel, cndReg);
      snippet = new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference(), doneLabel);
      cg->addSnippet(snippet);
      }

   // One of the object is array. Test equality of two objects' classes.
   //
   generateLoadJ9Class(node, tmp2Reg, obj2Reg, cg);
   generateLoadJ9Class(node, tmp1Reg, obj1Reg, cg);

   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, cndReg, tmp1Reg, tmp2Reg);

   // If either object is known to be of primitive component type,
   // we are done: since both of them have to be of equal class.
   if (node->isArrayChkPrimitiveArray1() || node->isArrayChkPrimitiveArray2())
      {
      if (snippetLabel == NULL)
         {
         snippetLabel = generateLabelSymbol(cg);
         gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg);
         snippet = new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference(), doneLabel);
         cg->addSnippet(snippet);
         }
      else
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg);
      }
   // We have to take care of the un-equal class situation: both of them must be of reference array
   else
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, cndReg);

      // Object1 must be of reference component type, otherwise throw exception
      if (!node->isArrayChkReferenceArray1())
         {

         // Loading the Class Pointer -> classDepthAndFlags
         generateLoadJ9Class(node, tmp1Reg, obj1Reg, cg);

         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, tmp1Reg, (int32_t) offsetof(J9Class, classDepthAndFlags), 4));

         // We already have classDepth&Flags in tmp1Reg.  X = (ramclass->ClassDepthAndFlags)>>J9AccClassRAMShapeShift
         generateShiftRightLogicalImmediate(cg, node, tmp1Reg, tmp1Reg, J9AccClassRAMShapeShift);

         // We need to perform a X & OBJECT_HEADER_SHAPE_MASK

         generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, tmp2Reg, tmp1Reg, 0, OBJECT_HEADER_SHAPE_MASK);
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, cndReg, tmp2Reg, OBJECT_HEADER_SHAPE_POINTERS);

         if (snippetLabel == NULL)
            {
            snippetLabel = generateLabelSymbol(cg);
            gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg);
            snippet = new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference(), doneLabel);
            cg->addSnippet(snippet);
            }
         else
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg);
         }

      // Object2 must be of reference component type array, otherwise throw exception
      if (!node->isArrayChkReferenceArray2())
         {

         generateLoadJ9Class(node, tmp1Reg, obj2Reg, cg);
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, tmp1Reg, (int32_t) offsetof(J9Class, classDepthAndFlags), 4));

         loadConstant(cg, node, (int32_t) J9AccClassRAMArray, tmp2Reg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, tmp2Reg, tmp1Reg, tmp2Reg);

         TR::Instruction * i = generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cndReg, tmp2Reg, NULLVALUE);

         if (snippetLabel == NULL)
            {
            snippetLabel = generateLabelSymbol(cg);
            gcPoint = generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, snippetLabel, cndReg);
            snippet = new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, snippetLabel, node->getSymbolReference(), doneLabel);
            cg->addSnippet(snippet);
            }
         else
            generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, snippetLabel, cndReg);

         // We already have classDepth&Flags in tmp1Reg.  X = (ramclass->ClassDepthAndFlags)>>J9AccClassRAMShapeShift
         generateShiftRightLogicalImmediate(cg, node, tmp1Reg, tmp1Reg, J9AccClassRAMShapeShift);

         // We need to perform a X & OBJECT_HEADER_SHAPE_MASK

         generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, tmp2Reg, tmp1Reg, 0, OBJECT_HEADER_SHAPE_MASK);
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, cndReg, tmp2Reg, OBJECT_HEADER_SHAPE_POINTERS);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, snippetLabel, cndReg);
         }
      }

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);
   if (snippetLabel != NULL)
      {
      gcPoint->PPCNeedsGCMap(0x0);
      snippet->gcMap().setGCRegisterMask(0x0);
      }

   conditions->stopUsingDepRegs(cg, obj1Reg, obj2Reg);

   cg->decReferenceCount(node->getFirstChild());
   cg->decReferenceCount(node->getSecondChild());
   return (NULL);
   }

void J9::Power::TreeEvaluator::genArrayCopyWithArrayStoreCHK(TR::Node* node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());
   TR::Register *metaReg = cg->getMethodMetaDataRegister();
   TR::Register *gr3Reg, *gr2Reg, *lengthReg, *temp1Reg;

   //      child 0 ------  Source array object;
   //      child 1 ------  Destination array object;
   //      child 2 ------  Source byte address;
   //      child 3 ------  Destination byte address;
   //      child 4 ------  Copy length in byte;

   bool aix_style_linkage = (comp->target().isAIX() || (comp->target().is64Bit() && comp->target().isLinux()));
   J9::Power::JNILinkage *jniLinkage = (J9::Power::JNILinkage*) cg->getLinkage(TR_J9JNILinkage);
   const TR::PPCLinkageProperties &pp = jniLinkage->getProperties();
   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(pp.getNumberOfDependencyGPRegisters(),
         pp.getNumberOfDependencyGPRegisters(), cg->trMemory());

   // I_32 referenceArrayCopy(
   //                   J9VMThread *vmThread,
   //                   J9IndexableObjectContiguous *srcObject,
   //                   J9IndexableObjectContiguous *destObject,
   //                   U_8 *srcAddress,
   //                   U_8 *destAddress,
   //                   I_32 lengthInSlots)
   intptr_t *funcdescrptr = (intptr_t*) fej9->getReferenceArrayCopyHelperAddress();

   TR::Instruction *iCursor;
   if (aix_style_linkage)
      {
      gr2Reg = cg->allocateRegister();
      TR::addDependency(conditions, gr2Reg, TR::RealRegister::gr2, TR_GPR, cg);
      }
   // set up the arguments and dependencies
   int32_t argSize = jniLinkage->buildJNIArgs(node, conditions, pp, true);
   temp1Reg = conditions->searchPreConditionRegister(TR::RealRegister::gr12);
   if (aix_style_linkage &&
      !(comp->target().is64Bit() && comp->target().isLinux() && comp->target().cpu.isLittleEndian()))
      {
      intptr_t target_ip = funcdescrptr[0];
      intptr_t target_toc = funcdescrptr[1];
      iCursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, target_ip, temp1Reg, NULL, false, TR_ArrayCopyHelper);
      iCursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, target_toc, gr2Reg, iCursor, false, TR_ArrayCopyToc);
      }
   else
      {
      bool doRelocation = comp->compileRelocatableCode();
#ifdef J9VM_OPT_JITSERVER
      doRelocation = doRelocation || comp->isOutOfProcessCompilation();
#endif
      iCursor = loadAddressConstant(cg, doRelocation, node, (intptr_t) funcdescrptr, temp1Reg, NULL, false, TR_ArrayCopyHelper);
      }

   iCursor = generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, temp1Reg, NULL, iCursor);
   // the C routine expects length measured by slots
   lengthReg = conditions->searchPreConditionRegister(TR::RealRegister::gr8);
   int32_t elementSize;
   if (comp->useCompressedPointers())
      elementSize = TR::Compiler->om.sizeofReferenceField();
   else
      elementSize = (int32_t) TR::Compiler->om.sizeofReferenceAddress();
   generateShiftRightLogicalImmediate(cg, node, lengthReg, lengthReg, trailingZeroes(elementSize));
   // pass vmThread as the first parameter
   gr3Reg = cg->allocateRegister();
   TR::addDependency(conditions, gr3Reg, TR::RealRegister::gr3, TR_GPR, cg);
   iCursor = generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, gr3Reg, metaReg, iCursor);
   // call the C routine
   TR::Instruction *gcPoint = generateDepInstruction(cg, TR::InstOpCode::bctrl, node, conditions);
   gcPoint->PPCNeedsGCMap(pp.getPreservedRegisterMapForGC());
   // check return value
   TR::Register *cr0Reg = conditions->searchPreConditionRegister(TR::RealRegister::cr0);
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cr0Reg, gr3Reg, -1);
   // throw exception if needed
   TR::SymbolReference *throwSymRef = comp->getSymRefTab()->findOrCreateArrayStoreExceptionSymbolRef(comp->getJittedMethodSymbol());
   TR::LabelSymbol *exceptionSnippetLabel = cg->lookUpSnippet(TR::Snippet::IsHelperCall, throwSymRef);
   if (exceptionSnippetLabel == NULL)
      {
      exceptionSnippetLabel = generateLabelSymbol(cg);
      cg->addSnippet(new (cg->trHeapMemory()) TR::PPCHelperCallSnippet(cg, node, exceptionSnippetLabel, throwSymRef));
      }

   gcPoint = generateDepConditionalBranchInstruction(cg, TR::InstOpCode::bnel, node, exceptionSnippetLabel, cr0Reg, conditions->cloneAndFix(cg));
   // somewhere to hang the dependencies
   TR::LabelSymbol *depLabel = generateLabelSymbol(cg);
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, depLabel, conditions);
   gcPoint->PPCNeedsGCMap(pp.getPreservedRegisterMapForGC());
   cg->machine()->setLinkRegisterKilled(true);
   conditions->stopUsingDepRegs(cg);
   cg->setHasCall();
   return;
   }

void J9::Power::TreeEvaluator::genWrtbarForArrayCopy(TR::Node *node, TR::Register *srcObjReg, TR::Register *dstObjReg, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   bool ageCheckIsNeeded = false;
   bool cardMarkIsNeeded = false;
   auto gcMode = TR::Compiler->om.writeBarrierType();
   TR::RegisterDependencyConditions *conditions = NULL;

   ageCheckIsNeeded = (gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always);
   cardMarkIsNeeded = (gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_incremental);

   if (!ageCheckIsNeeded && !cardMarkIsNeeded)
      return;

   if (ageCheckIsNeeded)
      {
      TR::Register *temp1Reg;
      TR::Register *temp2Reg;
      TR::Register *condReg = cg->allocateRegister(TR_CCR);
      TR::Instruction *gcPoint;

      if (gcMode != gc_modron_wrtbar_always)
         {
         temp1Reg = cg->allocateRegister();
         temp2Reg = cg->allocateRegister();
         conditions = createConditionsAndPopulateVSXDeps(cg, 4);
         TR::addDependency(conditions, temp1Reg, TR::RealRegister::gr11, TR_GPR, cg);
         TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         }
      else
         {
         conditions = createConditionsAndPopulateVSXDeps(cg, 2);
         }

      TR::addDependency(conditions, dstObjReg, TR::RealRegister::gr3, TR_GPR, cg);
      TR::addDependency(conditions, condReg, TR::RealRegister::cr0, TR_CCR, cg);

      TR::LabelSymbol *doneLabel;
      TR::SymbolReference *wbRef = comp->getSymRefTab()->findOrCreateWriteBarrierBatchStoreSymbolRef(comp->getMethodSymbol());

      if (gcMode != gc_modron_wrtbar_always)
         {
         doneLabel = generateLabelSymbol(cg);

         TR::Register *metaReg = cg->getMethodMetaDataRegister();

         // temp1Reg = dstObjReg - heapBaseForBarrierRange0
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapBaseForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
         generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, temp1Reg, temp1Reg, dstObjReg);

         // if (temp1Reg >= heapSizeForBarrierRage0), object not in the tenured area
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, temp2Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, heapSizeForBarrierRange0), TR::Compiler->om.sizeofReferenceAddress()));
         generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, temp1Reg, temp2Reg);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, doneLabel, condReg);
         }

      gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t) wbRef->getSymbol()->castToMethodSymbol()->getMethodAddress(),
            new (cg->trHeapMemory()) TR::RegisterDependencyConditions((uint8_t) 0, 0, cg->trMemory()), wbRef, NULL);

      if (gcMode != gc_modron_wrtbar_always)
         generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      cg->machine()->setLinkRegisterKilled(true);
      cg->setHasCall();

      // This GC point can only happen when there is an exception. As a result, we can ditch
      // all registers.
      gcPoint->PPCNeedsGCMap(0xFFFFFFFF);
      }

   if (!ageCheckIsNeeded && cardMarkIsNeeded)
      {
      if (!comp->getOptions()->realTimeGC())

         {
         TR::Register *cndReg = cg->allocateRegister(TR_CCR);
         TR::Register *temp1Reg = cg->allocateRegister();
         TR::Register *temp2Reg = cg->allocateRegister();
         TR::Register *temp3Reg = cg->allocateRegister();
         conditions = createConditionsAndPopulateVSXDeps(cg, 7);

         TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);
         TR::addDependency(conditions, dstObjReg, TR::RealRegister::NoReg, TR_GPR, cg);
         TR::addDependency(conditions, temp1Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         conditions->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0();
         conditions->getPostConditions()->getRegisterDependency(2)->setExcludeGPR0();
         TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);

         VMCardCheckEvaluator(node, dstObjReg, cndReg, temp1Reg, temp2Reg, temp3Reg, conditions, cg);
         generateDepLabelInstruction(cg, TR::InstOpCode::label, node, generateLabelSymbol(cg), conditions);

         }
      else
         TR_ASSERT(0, "genWrtbarForArrayCopy card marking not supported for RT");
      }
      if (conditions)
         {
         conditions->stopUsingDepRegs(cg, dstObjReg);
         }
   }

static TR::Register *genCAS(TR::Node *node, TR::CodeGenerator *cg, TR::Register *objReg, TR::Register *offsetReg, TR::Register *oldVReg, TR::Register *newVReg, TR::Register *cndReg,
      TR::LabelSymbol *doneLabel, TR::Node *objNode, int32_t oldValue, bool oldValueInReg, bool isLong, bool casWithoutSync = false)
   {
   TR::Register *resultReg = cg->allocateRegister();
   TR::Instruction *gcPoint;

   // Memory barrier --- NOTE: we should be able to do a test upfront to save this barrier,
   //                          but Hursley advised to be conservative due to lack of specification.
   generateInstruction(cg, TR::InstOpCode::lwsync, node);

   TR::LabelSymbol *loopLabel = generateLabelSymbol(cg);
   generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);

   generateTrg1MemInstruction(cg, isLong ? TR::InstOpCode::ldarx : TR::InstOpCode::lwarx, node, resultReg, TR::MemoryReference::createWithIndexReg(cg, objReg, offsetReg, isLong ? 8 : 4));
   if (oldValueInReg)
      generateTrg1Src2Instruction(cg, isLong ? TR::InstOpCode::cmp8 : TR::InstOpCode::cmp4, node, cndReg, resultReg, oldVReg);
   else
      generateTrg1Src1ImmInstruction(cg, isLong ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, cndReg, resultReg, oldValue);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0);

   // We don't know how the compare will fare such that we don't dictate the prediction
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, doneLabel, cndReg);

   generateMemSrc1Instruction(cg, isLong ? TR::InstOpCode::stdcx_r : TR::InstOpCode::stwcx_r, node, TR::MemoryReference::createWithIndexReg(cg, objReg, offsetReg, isLong ? 8 : 4), newVReg);
   // We expect this store is usually successful, i.e., the following branch will not be taken
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, cndReg);

   // We deviate from the VM helper here: no-store-no-barrier instead of always-barrier
   if (!casWithoutSync)
      generateInstruction(cg, TR::InstOpCode::sync, node);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1);

   node->setRegister(resultReg);
   return resultReg;
   }

static TR::Register *VMinlineCompareAndSwap(TR::Node *node, TR::CodeGenerator *cg, bool isLong)
   {
   TR::Compilation * comp = cg->comp();
   TR::Register *objReg, *offsetReg, *oldVReg, *newVReg, *resultReg, *cndReg;
   TR::Node *firstChild, *secondChild, *thirdChild, *fourthChild, *fifthChild;
   TR::RegisterDependencyConditions *conditions;
   TR::LabelSymbol *doneLabel;
   intptr_t offsetValue, oldValue;
   bool oldValueInReg = true, freeOffsetReg = false;
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->fe());

   firstChild = node->getFirstChild();
   secondChild = node->getSecondChild();
   thirdChild = node->getChild(2);
   fourthChild = node->getChild(3);
   fifthChild = node->getChild(4);
   objReg = cg->evaluate(secondChild);

   // VM helper chops off the value in 32bit, and we don't want the whole long value either
   if (thirdChild->getOpCode().isLoadConst() && thirdChild->getRegister() == NULL && comp->target().is32Bit())
      {
      offsetValue = thirdChild->getLongInt();
      offsetReg = cg->allocateRegister();
      loadConstant(cg, node, (int32_t) offsetValue, offsetReg);
      freeOffsetReg = true;
      }
   else
      {
      offsetReg = cg->evaluate(thirdChild);
      if (comp->target().is32Bit())
         offsetReg = offsetReg->getLowOrder();
      }

   if (fourthChild->getOpCode().isLoadConst() && fourthChild->getRegister() == NULL)
      {
      if (isLong)
         oldValue = fourthChild->getLongInt();
      else
         oldValue = fourthChild->getInt();
      if (oldValue >= LOWER_IMMED && oldValue <= UPPER_IMMED)
         oldValueInReg = false;
      }
   if (oldValueInReg)
      oldVReg = cg->evaluate(fourthChild);
   newVReg = cg->evaluate(fifthChild);
   cndReg = cg->allocateRegister(TR_CCR);
   doneLabel = generateLabelSymbol(cg);

   bool casWithoutSync = false;
   TR_OpaqueMethodBlock *caller = node->getOwningMethod();
   if (caller)
      {
      TR_ResolvedMethod *m = fej9->createResolvedMethod(cg->trMemory(), caller, node->getSymbolReference()->getOwningMethod(comp));
      if ((m->getRecognizedMethod() == TR::java_util_concurrent_atomic_AtomicInteger_weakCompareAndSet)
            || (m->getRecognizedMethod() == TR::java_util_concurrent_atomic_AtomicLong_weakCompareAndSet)
            || (m->getRecognizedMethod() == TR::java_util_concurrent_atomic_AtomicReference_weakCompareAndSet))
         {
         casWithoutSync = true;
         }
      }

   resultReg = genCAS(node, cg, objReg, offsetReg, oldVReg, newVReg, cndReg, doneLabel, secondChild, oldValue, oldValueInReg, isLong, casWithoutSync);

   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(6, 6, cg->trMemory());
   TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
   TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, resultReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, newVReg, TR::RealRegister::NoReg, TR_GPR, cg);
   if (oldValueInReg)
      TR::addDependency(conditions, oldVReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   cg->stopUsingRegister(cndReg);
   cg->recursivelyDecReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   if (freeOffsetReg)
      {
      cg->stopUsingRegister(offsetReg);
      cg->recursivelyDecReferenceCount(thirdChild);
      }
   else
      cg->decReferenceCount(thirdChild);
   if (oldValueInReg)
      cg->decReferenceCount(fourthChild);
   else
      cg->recursivelyDecReferenceCount(fourthChild);
   cg->decReferenceCount(fifthChild);
   return resultReg;
   }

static TR::Register *VMinlineCompareAndSwapObject(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   TR::Register *objReg, *offsetReg, *oldVReg, *newVReg, *resultReg, *cndReg;
   TR::Node *firstChild, *secondChild, *thirdChild, *fourthChild, *fifthChild;
   TR::RegisterDependencyConditions *conditions;
   TR::LabelSymbol *doneLabel, *storeLabel, *wrtBarEndLabel;
   intptr_t offsetValue;
   bool freeOffsetReg = false;
   bool needDup = false;

   auto gcMode = TR::Compiler->om.writeBarrierType();
   bool doWrtBar = (gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC());
   bool doCrdMrk = (gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental);

   firstChild = node->getFirstChild();
   secondChild = node->getSecondChild();
   thirdChild = node->getChild(2);
   fourthChild = node->getChild(3);
   fifthChild = node->getChild(4);
   objReg = cg->evaluate(secondChild);

   // VM helper chops off the value in 32bit, and we don't want the whole long value either
   if (thirdChild->getOpCode().isLoadConst() && thirdChild->getRegister() == NULL && comp->target().is32Bit())
      {
      offsetValue = thirdChild->getLongInt();
      offsetReg = cg->allocateRegister();
      loadConstant(cg, node, (int32_t) offsetValue, offsetReg);
      freeOffsetReg = true;
      }
   else
      {
      offsetReg = cg->evaluate(thirdChild);
      if (comp->target().is32Bit())
         offsetReg = offsetReg->getLowOrder();
      }

   oldVReg = cg->evaluate(fourthChild);

   TR::Node *translatedNode = fifthChild;
   bool bumpedRefCount = false;
   if (comp->useCompressedPointers() && (fifthChild->getDataType() != TR::Address))
      {
      bool useShiftedOffsets = (TR::Compiler->om.compressedReferenceShiftOffset() != 0);

      translatedNode = fifthChild;
      if (translatedNode->getOpCode().isConversion())
         translatedNode = translatedNode->getFirstChild();
      if (translatedNode->getOpCode().isRightShift()) // optional
         translatedNode = translatedNode->getFirstChild();

      translatedNode = fifthChild;
      if (useShiftedOffsets)
         {
         while ((translatedNode->getNumChildren() > 0) && (translatedNode->getOpCodeValue() != TR::a2l))
            translatedNode = translatedNode->getFirstChild();

         if (translatedNode->getOpCodeValue() == TR::a2l)
            translatedNode = translatedNode->getFirstChild();

         // this is required so that different registers are
         // allocated for the actual store and translated values
         translatedNode->incReferenceCount();
         bumpedRefCount = true;
         }
      }

   newVReg = cg->evaluate(fifthChild);
   if (objReg == newVReg)
      {
      newVReg = cg->allocateCollectedReferenceRegister();
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, newVReg, objReg);
      needDup = true;
      }
   cndReg = cg->allocateRegister(TR_CCR);
   doneLabel = generateLabelSymbol(cg);
   storeLabel = generateLabelSymbol(cg);

   if (comp->getOptions()->realTimeGC())
      wrtBarEndLabel = storeLabel;
   else
      wrtBarEndLabel = doneLabel;

#ifdef OMR_GC_CONCURRENT_SCAVENGER
   if (TR::Compiler->om.readBarrierType() != gc_modron_readbar_none)
      {
      TR::Register *tmpReg = cg->allocateRegister();
      TR::Register *locationReg = cg->allocateRegister();
      TR::Register *evacuateReg = cg->allocateRegister();
      TR::Register *r3Reg = cg->allocateRegister();
      TR::Register *r11Reg = cg->allocateRegister();
      TR::Register *metaReg = cg->getMethodMetaDataRegister();

      TR::LabelSymbol *startReadBarrierLabel = generateLabelSymbol(cg);
      TR::LabelSymbol *endReadBarrierLabel = generateLabelSymbol(cg);

      TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 9, cg->trMemory());
      deps->addPostCondition(objReg, TR::RealRegister::NoReg);
      deps->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
      deps->addPostCondition(offsetReg, TR::RealRegister::NoReg);
      deps->addPostCondition(tmpReg, TR::RealRegister::NoReg);
      deps->addPostCondition(locationReg, TR::RealRegister::gr4); //TR_softwareReadBarrier helper needs this in gr4.
      deps->addPostCondition(evacuateReg, TR::RealRegister::NoReg);
      deps->addPostCondition(r3Reg, TR::RealRegister::gr3);
      deps->addPostCondition(r11Reg, TR::RealRegister::gr11);
      deps->addPostCondition(metaReg, TR::RealRegister::NoReg);
      deps->addPostCondition(cndReg, TR::RealRegister::NoReg);

      startReadBarrierLabel->setStartInternalControlFlow();
      endReadBarrierLabel->setEndInternalControlFlow();

      TR::InstOpCode::Mnemonic loadOpCode = TR::InstOpCode::lwz;
      TR::InstOpCode::Mnemonic cmpOpCode = TR::InstOpCode::cmpl4;
      int32_t loadWidth = 4;

      if (comp->target().is64Bit() && !comp->useCompressedPointers())
         {
         loadOpCode = TR::InstOpCode::ld;
         cmpOpCode = TR::InstOpCode::cmpl8;
         loadWidth = 8;
         }

      generateTrg1MemInstruction(cg, loadOpCode, node, tmpReg, TR::MemoryReference::createWithIndexReg(cg, objReg, offsetReg, loadWidth));

      generateLabelInstruction(cg, TR::InstOpCode::label, node, startReadBarrierLabel);

      generateTrg1MemInstruction(cg, loadOpCode, node, evacuateReg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateBaseAddressOffset(), loadWidth));
      generateTrg1Src2Instruction(cg, cmpOpCode, node, cndReg, tmpReg, evacuateReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, endReadBarrierLabel, cndReg);

      generateTrg1MemInstruction(cg, loadOpCode, node, evacuateReg,
            TR::MemoryReference::createWithDisplacement(cg, metaReg, comp->fej9()->thisThreadGetEvacuateTopAddressOffset(), loadWidth));
      generateTrg1Src2Instruction(cg, cmpOpCode, node, cndReg, tmpReg, evacuateReg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bgt, node, endReadBarrierLabel, cndReg);

      generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, locationReg, objReg, offsetReg);

      // TR_softwareReadBarrier helper expects the vmThread in r3.
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r3Reg, metaReg);

      TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_softwareReadBarrier);
      generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), deps, helperSym);

      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endReadBarrierLabel, deps);

      cg->stopUsingRegister(tmpReg);
      cg->stopUsingRegister(locationReg);
      cg->stopUsingRegister(evacuateReg);
      cg->stopUsingRegister(r11Reg);
      cg->stopUsingRegister(r3Reg);

      cg->machine()->setLinkRegisterKilled(true);
      }
#endif //OMR_GC_CONCURRENT_SCAVENGER

   if (!comp->getOptions()->realTimeGC())
      resultReg = genCAS(node, cg, objReg, offsetReg, oldVReg, newVReg, cndReg, doneLabel, secondChild, 0, true, (comp->target().is64Bit() && !comp->useCompressedPointers()));

   uint32_t numDeps = (doWrtBar || doCrdMrk) ? 13 : 11;
   conditions = createConditionsAndPopulateVSXDeps(cg, numDeps);

   if (doWrtBar && doCrdMrk)
      {
      TR::Register *temp1Reg = cg->allocateRegister(), *temp2Reg = cg->allocateRegister(), *temp3Reg, *temp4Reg = cg->allocateRegister();
      TR::addDependency(conditions, objReg, TR::RealRegister::gr3, TR_GPR, cg);
      TR::Register *wrtbarSrcReg;
      if (translatedNode != fifthChild)
         {
         TR::addDependency(conditions, newVReg, TR::RealRegister::NoReg, TR_GPR, cg);
         TR::addDependency(conditions, translatedNode->getRegister(), TR::RealRegister::gr4, TR_GPR, cg);
         wrtbarSrcReg = translatedNode->getRegister();
         }
      else
         {
         TR::addDependency(conditions, newVReg, TR::RealRegister::gr4, TR_GPR, cg);
         wrtbarSrcReg = newVReg;
         }

      TR::addDependency(conditions, temp1Reg, TR::RealRegister::gr11, TR_GPR, cg);
      TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
      TR::addDependency(conditions, temp4Reg, TR::RealRegister::NoReg, TR_GPR, cg);

      if (freeOffsetReg)
         {
         temp3Reg = offsetReg;
         }
      else
         {
         temp3Reg = cg->allocateRegister();
         TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         }

      if (!fifthChild->isNonNull())
         {
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cndReg, newVReg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, doneLabel, cndReg);
         }

      VMnonNullSrcWrtBarCardCheckEvaluator(node, comp->useCompressedPointers() ? wrtbarSrcReg : newVReg, objReg, cndReg, temp1Reg, temp2Reg, temp3Reg, temp4Reg, doneLabel,
            conditions, comp->useCompressedPointers(), cg);

      cg->stopUsingRegister(temp1Reg);
      cg->stopUsingRegister(temp2Reg);
      if (!freeOffsetReg)
         cg->stopUsingRegister(temp3Reg);
      cg->stopUsingRegister(temp4Reg);
      }
   else if (doWrtBar && !doCrdMrk)
      {
      TR::Register *temp1Reg = cg->allocateRegister(), *temp2Reg;
      TR::addDependency(conditions, objReg, TR::RealRegister::gr3, TR_GPR, cg);

      if (newVReg != translatedNode->getRegister())
         {
         TR::addDependency(conditions, newVReg, TR::RealRegister::NoReg, TR_GPR, cg);
         if (comp->getOptions()->realTimeGC())
            TR::addDependency(conditions, translatedNode->getRegister(), TR::RealRegister::gr5, TR_GPR, cg);
         else
            TR::addDependency(conditions, translatedNode->getRegister(), TR::RealRegister::gr4, TR_GPR, cg);
         }
      else
         {
         if (comp->getOptions()->realTimeGC())
            TR::addDependency(conditions, newVReg, TR::RealRegister::gr5, TR_GPR, cg);
         else
            TR::addDependency(conditions, newVReg, TR::RealRegister::gr4, TR_GPR, cg);
         }

      TR::addDependency(conditions, temp1Reg, TR::RealRegister::NoReg, TR_GPR, cg);

      //Realtime needs the offsetReg to be preserved after the wrtbar to do the store in genCAS()
      if (freeOffsetReg && !comp->getOptions()->realTimeGC())
         {
         TR::addDependency(conditions, offsetReg, TR::RealRegister::gr11, TR_GPR, cg);
         temp2Reg = offsetReg;
         }
      else
         {
         temp2Reg = cg->allocateRegister();
         TR::addDependency(conditions, temp2Reg, TR::RealRegister::gr11, TR_GPR, cg);
         TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
         }

      if (!fifthChild->isNonNull())
         {
         generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpi, node, cndReg, newVReg, NULLVALUE);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, wrtBarEndLabel, cndReg);
         }

      TR::Register *dstAddrReg = NULL;

      if (comp->getOptions()->realTimeGC())
         {
         dstAddrReg = cg->allocateRegister();
         TR::addDependency(conditions, dstAddrReg, TR::RealRegister::gr4, TR_GPR, cg);
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, dstAddrReg, objReg, offsetReg);
         }

      VMnonNullSrcWrtBarCardCheckEvaluator(node, comp->useCompressedPointers() ? translatedNode->getRegister() : newVReg, objReg, cndReg, temp1Reg, temp2Reg, dstAddrReg, NULL,
            wrtBarEndLabel, conditions, comp->useCompressedPointers(), cg);

      if (comp->getOptions()->realTimeGC())
         cg->stopUsingRegister(dstAddrReg);

      cg->stopUsingRegister(temp1Reg);
      if (!freeOffsetReg || comp->getOptions()->realTimeGC())
         cg->stopUsingRegister(temp2Reg);
      }
   else if (!doWrtBar && doCrdMrk)
      {
      TR::Register *temp1Reg = cg->allocateRegister(), *temp2Reg = cg->allocateRegister(), *temp3Reg;
      TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
      TR::addDependency(conditions, temp1Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0();
      if (newVReg != translatedNode->getRegister())
         TR::addDependency(conditions, newVReg, TR::RealRegister::NoReg, TR_GPR, cg);
      TR::addDependency(conditions, translatedNode->getRegister(), TR::RealRegister::NoReg, TR_GPR, cg);

      TR::addDependency(conditions, temp2Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
      if (freeOffsetReg)
         {
         temp3Reg = offsetReg;
         }
      else
         {
         temp3Reg = cg->allocateRegister();
         TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         }

      VMCardCheckEvaluator(node, objReg, cndReg, temp1Reg, temp2Reg, temp3Reg, conditions, cg);

      cg->stopUsingRegister(temp1Reg);
      cg->stopUsingRegister(temp2Reg);
      if (!freeOffsetReg)
         cg->stopUsingRegister(temp3Reg);
      }
   else
      {
      TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
      TR::addDependency(conditions, offsetReg, TR::RealRegister::NoReg, TR_GPR, cg);
      if (newVReg != translatedNode->getRegister())
         TR::addDependency(conditions, newVReg, TR::RealRegister::NoReg, TR_GPR, cg);
      TR::addDependency(conditions, translatedNode->getRegister(), TR::RealRegister::NoReg, TR_GPR, cg);
      }

   generateLabelInstruction(cg, TR::InstOpCode::label, node, storeLabel);

   if (comp->getOptions()->realTimeGC())
      resultReg = genCAS(node, cg, objReg, offsetReg, oldVReg, newVReg, cndReg, doneLabel, secondChild, 0, true, (comp->target().is64Bit() && !comp->useCompressedPointers()));

   TR::addDependency(conditions, resultReg, TR::RealRegister::NoReg, TR_GPR, cg);
   if (oldVReg != newVReg && oldVReg != objReg)
      TR::addDependency(conditions, oldVReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   if (needDup)
      cg->stopUsingRegister(newVReg);
   cg->stopUsingRegister(cndReg);
   cg->recursivelyDecReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   if (freeOffsetReg)
      {
      cg->stopUsingRegister(offsetReg);
      cg->recursivelyDecReferenceCount(thirdChild);
      }
   else
      cg->decReferenceCount(thirdChild);
   cg->decReferenceCount(fourthChild);
   cg->decReferenceCount(fifthChild);
   if (bumpedRefCount)
      cg->decReferenceCount(translatedNode);

   return resultReg;
   }


void J9::Power::TreeEvaluator::restoreTOCRegister(TR::Node *node, TR::CodeGenerator *cg, TR::RegisterDependencyConditions *dependencies)
   {
#if defined(TR_HOST_POWER)
   TR::Compilation *comp = cg->comp();
   TR::Register *tocReg = dependencies->searchPreConditionRegister(TR::RealRegister::gr2);
   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, tocReg,
         TR::MemoryReference::createWithDisplacement(cg, cg->getMethodMetaDataRegister(), offsetof(J9VMThread, jitTOC), TR::Compiler->om.sizeofReferenceAddress()));
#else
   TR_ASSERT(0, "direct C calls directly are not supported on this platform.\n");
#endif
   }

void J9::Power::TreeEvaluator::buildArgsProcessFEDependencies(TR::Node *node, TR::CodeGenerator *cg, TR::RegisterDependencyConditions *dependencies)
   {
   TR::Compilation *comp = cg->comp();
   //Java uses gr2 as a volatile for targets except for 32-bit Linux.
   bool aix_style_linkage = (comp->target().isAIX() || (comp->target().is64Bit() && comp->target().isLinux()));

   if(aix_style_linkage)
      TR::addDependency(dependencies, NULL, TR::RealRegister::gr2, TR_GPR, cg);
   }

TR::Register *J9::Power::TreeEvaluator::retrieveTOCRegister(TR::Node *node, TR::CodeGenerator *cg, TR::RegisterDependencyConditions *dependencies)
   {
#if defined(DEBUG)
   TR::Compilation *comp = cg->comp();
   //We should not land here if we're compiling for 32-bit Linux.
   bool aix_style_linkage = (comp->target().isAIX() || (comp->target().is64Bit() && comp->target().isLinux()));
   TR_ASSERT(aix_style_linkage, "Landed to restore gr2 for TOC with 32bit Linux as target\n");
#endif

   TR::Register *grTOCReg=dependencies->searchPreConditionRegister(TR::RealRegister::gr2);
   TR_ASSERT(grTOCReg != NULL, "Dependency not set in J9 Java on gr2 for TOC.\n");
   return grTOCReg;
   }


static TR::Register *inlineAtomicOps(TR::Node *node, TR::CodeGenerator *cg, int8_t size, TR::MethodSymbol *method, bool isArray)
   {
   TR::Node *valueChild = node->getFirstChild();
   TR::Node *deltaChild = NULL;
   TR::Register *valueReg = cg->evaluate(valueChild);
   TR::Register *deltaReg = NULL;
   TR::Register *resultReg = cg->allocateRegister();
   TR::Register *cndReg = cg->allocateRegister(TR_CCR);
   TR::Register *fieldOffsetReg = cg->allocateRegister();
   TR::Register *tempReg = NULL;
   int32_t delta = 0;
   int32_t numDeps = 5;
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());

   bool isAddOp = true;
   bool isGetAndOp = true;
   bool isLong = false;
   bool isArgConstant = false;
   bool isArgImmediate = false;
   bool isArgImmediateShifted = false;
   TR::RecognizedMethod currentMethod = method->getRecognizedMethod();

   switch (currentMethod)
      {
   case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicLong_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicReference_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicLongArray_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicReferenceArray_getAndSet:
      {
      isAddOp = false;
      break;
      }
   case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
      {
      isGetAndOp = false;
      }
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
      {
      break;
      }

   case TR::java_util_concurrent_atomic_AtomicInteger_incrementAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_incrementAndGet:
      {
      isGetAndOp = false;
      }
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
      {
      delta = (int32_t) 1;
      isArgConstant = true;
      isArgImmediate = true;
      break;
      }
   case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
      {
      isGetAndOp = false;
      }
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
      {
      delta = (int32_t) - 1;
      isArgConstant = true;
      isArgImmediate = true;
      break;
      }
      }

   if (node->getNumChildren() > 1)
      deltaChild = node->getSecondChild();

   //determine if the delta is a constant.
   if (deltaChild && deltaChild->getOpCode().isLoadConst() && !deltaChild->getRegister() && deltaChild->getDataType() == TR::Int32)
      {
      delta = (int32_t)(deltaChild->getInt());
      isArgConstant = true;

      //determine if the constant can be represented as an immediate
      if (delta <= UPPER_IMMED && delta >= LOWER_IMMED)
         {
         // avoid evaluating immediates for add operations
         isArgImmediate = true;
         }
      else if (delta & 0xFFFF == 0 && (delta & 0xFFFF0000) >> 16 <= UPPER_IMMED && (delta & 0xFFFF0000) >> 16 >= LOWER_IMMED)
         {
         // avoid evaluating shifted immediates for add operations
         isArgImmediate = true;
         isArgImmediateShifted = true;
         }
      else
         {
         // evaluate non-immediate constants since there may be reuse
         // and they have to go into a reg anyway
         tempReg = cg->evaluate(deltaChild);
         }
      }
   else if (deltaChild)
      tempReg = cg->evaluate(deltaChild);

   //determine the offset of the value field
   int32_t fieldOffset = 0;
   int32_t shiftAmount = 0;
   TR::Node *indexChild = NULL;
   TR::Register *indexRegister = NULL;

   TR::Register *scratchRegister = NULL;

   if (!isArray)
      {
      TR_OpaqueClassBlock * bdClass;
      char *className, *fieldSig;
      int32_t classNameLen, fieldSigLen;

      fieldSigLen = 1;

      switch (currentMethod)
         {
      case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
         className = "Ljava/util/concurrent/atomic/AtomicBoolean;";
         classNameLen = 43;
         fieldSig = "I"; // not a typo, the field is int
         break;
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicInteger_incrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
         className = "Ljava/util/concurrent/atomic/AtomicInteger;";
         classNameLen = 43;
         fieldSig = "I";
         break;
      case TR::java_util_concurrent_atomic_AtomicLong_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicLong_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicLong_incrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicLong_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndDecrement:
         className = "Ljava/util/concurrent/atomic/AtomicLong;";
         classNameLen = 40;
         fieldSig = "J";
         break;
      case TR::java_util_concurrent_atomic_AtomicReference_getAndSet:
         className = "Ljava/util/concurrent/atomic/AtomicReference;";
         classNameLen = 45;
         fieldSig = "Ljava/lang/Object;";
         fieldSigLen = 18;
         break;
      default:
         TR_ASSERT(0, "Unknown atomic operation method\n");
         return NULL;
         }

      TR_ResolvedMethod *owningMethod = node->getSymbolReference()->getOwningMethod(comp);
      TR_OpaqueClassBlock *containingClass = fej9->getClassFromSignature(className, classNameLen, owningMethod, true);
      fieldOffset = fej9->getInstanceFieldOffset(containingClass, "value", 5, fieldSig, fieldSigLen, true) + fej9->getObjectHeaderSizeInBytes(); // size of a J9 object header
      }
   else
      {
      if (isArray)
         {
         indexChild = node->getChild(1);
         indexRegister = cg->evaluate(indexChild);
         fieldOffset = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
         if (size == 4)
            shiftAmount = 2;
         else if (size == 8)
            shiftAmount = 3;

         TR_OpaqueClassBlock * bdClass;
         char *className, *fieldSig;
         int32_t classNameLen, fieldSigLen;

         fieldSigLen = 1;

         switch (currentMethod)
            {
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_incrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
         case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
            className = "Ljava/util/concurrent/atomic/AtomicIntegerArray;";
            classNameLen = 48;
            fieldSig = "[I";
            fieldSigLen = 2;
            break;

         case TR::java_util_concurrent_atomic_AtomicLongArray_getAndSet:
         case TR::java_util_concurrent_atomic_AtomicLongArray_incrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicLongArray_getAndIncrement:
         case TR::java_util_concurrent_atomic_AtomicLongArray_decrementAndGet:
         case TR::java_util_concurrent_atomic_AtomicLongArray_getAndDecrement:
         case TR::java_util_concurrent_atomic_AtomicLongArray_addAndGet:
         case TR::java_util_concurrent_atomic_AtomicLongArray_getAndAdd:
            className = "Ljava/util/concurrent/atomic/AtomicLongArray;";
            classNameLen = 45;
            fieldSig = "[J";
            fieldSigLen = 2;
            break;

         case TR::java_util_concurrent_atomic_AtomicReferenceArray_getAndSet:
            className = "Ljava/util/concurrent/atomic/AtomicReferenceArray;";
            classNameLen = 50;
            fieldSig = "Ljava/lang/Object;";
            fieldSigLen = 18;
            break;
            }

         TR_ResolvedMethod *owningMethod = node->getSymbolReference()->getOwningMethod(comp);
         TR_OpaqueClassBlock *containingClass = fej9->getClassFromSignature(className, classNameLen, owningMethod, true);
         int32_t arrayFieldOffset = fej9->getInstanceFieldOffset(containingClass, "array", 5, fieldSig, fieldSigLen) + fej9->getObjectHeaderSizeInBytes(); // size of a J9 object header

         TR::MemoryReference *tempMR = TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, isLong ? 8 : 4);

         numDeps++;
         scratchRegister = cg->allocateCollectedReferenceRegister();
         TR::Register *memRefRegister = scratchRegister;

         if (TR::Compiler->om.compressObjectReferences())
            // read only 32 bits
            generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, memRefRegister,
                  TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, 4));
         else
            generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, memRefRegister, TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, TR::Compiler->om.sizeofReferenceAddress()));

         valueReg = memRefRegister;

         generateShiftLeftImmediate(cg, node, fieldOffsetReg, indexRegister, shiftAmount);
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, fieldOffsetReg, fieldOffsetReg, fieldOffset);
         }
      }

   // Memory barrier --- NOTE: we should be able to do a test upfront to save this barrier,
   //                          but Hursley advised to be conservative due to lack of specification.
   generateInstruction(cg, TR::InstOpCode::lwsync, node);

   TR::LabelSymbol *doneLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol *loopLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);

   loopLabel->setStartInternalControlFlow();
   if (!isArray)
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, fieldOffsetReg, fieldOffset);

   deltaReg = cg->allocateRegister();
   if (isArgImmediate && isAddOp)
      {
      // If argument is an immediate value and operation is an add,
      // it will be used as an immediate operand in an add immediate instruction
      generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
      }
   else if (isArgImmediate)
      {
      // If argument is immediate, but the operation is not an add,
      // the value must still be loaded into a register
      generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
      loadConstant(cg, node, delta, deltaReg);
      }
   else
      {
      // For non-constant arguments, use evaluated register
      // For non-immediate constants, evaluate since they may be re-used
      numDeps++;
      generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, deltaReg, tempReg);
      }

   generateTrg1MemInstruction(cg, isLong ? TR::InstOpCode::ldarx : TR::InstOpCode::lwarx, node, resultReg,
         TR::MemoryReference::createWithIndexReg(cg, valueReg, fieldOffsetReg, isLong ? 8 : 4));

   if (isAddOp)
      {
      if (isArgImmediateShifted)
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addis, node, deltaReg, resultReg, ((delta & 0xFFFF0000) >> 16));
      else if (isArgImmediate)
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, deltaReg, resultReg, delta);
      else
         generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, deltaReg, resultReg, deltaReg);
      }

   generateMemSrc1Instruction(cg, isLong ? TR::InstOpCode::stdcx_r : TR::InstOpCode::stwcx_r, node, TR::MemoryReference::createWithIndexReg(cg, valueReg, fieldOffsetReg, isLong ? 8 : 4),
         deltaReg);

   // We expect this store is usually successful, i.e., the following branch will not be taken
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, loopLabel, cndReg);

   // We deviate from the VM helper here: no-store-no-barrier instead of always-barrier
   generateInstruction(cg, TR::InstOpCode::sync, node);

   TR::RegisterDependencyConditions *conditions;

   //Set the conditions and dependencies
   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, (uint16_t) numDeps, cg->trMemory());
   conditions->addPostCondition(valueReg, TR::RealRegister::NoReg);
   conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
   conditions->addPostCondition(resultReg, TR::RealRegister::NoReg);
   conditions->getPostConditions()->getRegisterDependency(1)->setExcludeGPR0();
   conditions->addPostCondition(deltaReg, TR::RealRegister::NoReg);
   conditions->addPostCondition(cndReg, TR::RealRegister::cr0);
   conditions->addPostCondition(fieldOffsetReg, TR::RealRegister::NoReg);
   if (tempReg)
      conditions->addPostCondition(tempReg, TR::RealRegister::NoReg);
   if (scratchRegister)
      conditions->addPostCondition(scratchRegister, TR::RealRegister::NoReg);

   doneLabel->setEndInternalControlFlow();
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   cg->decReferenceCount(valueChild);
   cg->stopUsingRegister(cndReg);
   cg->stopUsingRegister(fieldOffsetReg);

   if (tempReg)
      cg->stopUsingRegister(tempReg);

   if (scratchRegister)
      cg->stopUsingRegister(scratchRegister);

   if (deltaChild)
      cg->decReferenceCount(deltaChild);

   if (isGetAndOp)
      {
      //for Get And Op, we will store the result in the result register
      cg->stopUsingRegister(deltaReg);
      node->setRegister(resultReg);
      return resultReg;
      }
   else
      {
      //for Op And Get, we will store the return value in the delta register
      //we no longer need the result register
      cg->stopUsingRegister(resultReg);
      node->setRegister(deltaReg);
      return deltaReg;
      }
   }

static TR::Register *inlineSinglePrecisionFP(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 1, "Wrong number of children in inlineSinglePrecisionFP");

   TR::Node *firstChild = node->getFirstChild();
   TR::Register *srcRegister = cg->evaluate(firstChild);
   TR::Register *targetRegister = cg->allocateSinglePrecisionRegister();

   generateTrg1Src1Instruction(cg, op, node, targetRegister, srcRegister);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);

   return targetRegister;
   }

static TR::Register *inlineSinglePrecisionFPTrg1Src2(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 2, "Wrong number of children in inlineSinglePrecisionFPTrg1Src2");

   TR::Node *firstChild = node->getFirstChild();
   TR::Node *secondChild = node->getSecondChild();
   TR::Register *src1Register = cg->evaluate(firstChild);
   TR::Register *src2Register = cg->evaluate(secondChild);
   TR::Register *targetRegister = cg->allocateSinglePrecisionRegister();

   if (op == TR::InstOpCode::fcpsgn) // fcpsgn orders operands opposite of Math.copySign
      generateTrg1Src2Instruction(cg, op, node, targetRegister, src2Register, src1Register);
   else
      generateTrg1Src2Instruction(cg, op, node, targetRegister, src1Register, src2Register);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   return targetRegister;
   }

static TR::Register *inlineFPTrg1Src3(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT_FATAL(node->getNumChildren() == 3, "In function inlineFPTrg1Src3, the node at address %p should have exactly 3 children, but got %u instead", node, node->getNumChildren());

   TR::DataType type = node->getDataType();
   TR_ASSERT_FATAL(type == TR::Float || type == TR::Double, "In function inlineFPTrg1Src3, the node at address %p should be either TR::Float or TR::Double", node);

   TR::Node *firstChild = node->getFirstChild();
   TR::Node *secondChild = node->getSecondChild();
   TR::Node *thirdChild = node->getThirdChild();
   TR::Register *src1Register = cg->evaluate(firstChild);
   TR::Register *src2Register = cg->evaluate(secondChild);
   TR::Register *src3Register = cg->evaluate(thirdChild);
   TR::Register *targetRegister;

   if(type == TR::Float)
      targetRegister = cg->allocateSinglePrecisionRegister();
   else
      targetRegister = cg->allocateRegister(TR_FPR);

   generateTrg1Src3Instruction(cg, op, node, targetRegister, src1Register, src2Register, src3Register);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   cg->decReferenceCount(thirdChild);
   return targetRegister;
   }

static TR::Register *inlineDoublePrecisionFP(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 1, "Wrong number of children in inlineDoublePrecisionFP");

   TR::Node *firstChild = node->getFirstChild();
   TR::Register *srcRegister = cg->evaluate(firstChild);
   TR::Register *targetRegister = cg->allocateRegister(TR_FPR);

   generateTrg1Src1Instruction(cg, op, node, targetRegister, srcRegister);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);

   return targetRegister;
   }

static TR::Register *inlineDoublePrecisionFPTrg1Src2(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 2, "Wrong number of children in inlineDoublePrecisionFPTrg1Src2");

   TR::Node *firstChild = node->getFirstChild();
   TR::Node *secondChild = node->getSecondChild();
   TR::Register *src1Register = cg->evaluate(firstChild);
   TR::Register *src2Register = cg->evaluate(secondChild);
   TR::Register *targetRegister = cg->allocateRegister(TR_FPR);

   if (op == TR::InstOpCode::fcpsgn) // fcpsgn orders operands opposite of Math.copySign
      generateTrg1Src2Instruction(cg, op, node, targetRegister, src2Register, src1Register);
   else
      generateTrg1Src2Instruction(cg, op, node, targetRegister, src1Register, src2Register);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);
   cg->decReferenceCount(secondChild);
   return targetRegister;
   }

static TR::Register *inlineAtomicOperation(TR::Node *node, TR::CodeGenerator *cg, TR::MethodSymbol *method)
   {
   TR::Node *firstChild = NULL;
   TR::Node *valueChild = NULL;
   TR::Node *indexChild = NULL;
   TR::Node *deltaChild = NULL;
   TR::Node *newValChild = NULL;
   TR::Node *expValChild = NULL;
   TR::Node *newRefChild = NULL;
   TR::Node *expRefChild = NULL;
   TR::Node *overNewNode = NULL;
   TR::Node *overExpNode = NULL;

   TR::LabelSymbol *startLabel = NULL;
   TR::LabelSymbol *failLabel = NULL;
   TR::LabelSymbol *doneLabel = NULL;

   TR::Register *valueReg = NULL;
   TR::Register *scratchReg = NULL;
   TR::Register *fieldOffsetReg = NULL;
   TR::Register *cndReg = NULL;
   TR::Register *currentReg = NULL;
   TR::Register *newComposedReg = NULL;
   TR::Register *expComposedReg = NULL;
   TR::Register *expRefReg = NULL;
   TR::Register *newRefReg = NULL;
   TR::Register *expValReg = NULL;
   TR::Register *newValReg = NULL;
   TR::Register *resultReg = NULL;
   TR::Register *objReg = NULL;
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());

   bool isArray = false;
   bool isAddOp = true;
   bool isDeltaImplied = false;
   bool isDelta = false;
   bool isGetAndOp = false;
   bool isCAS = false;
   bool isSetOnly = false;
   bool isWeak = false;
   bool isRefFirst = false;
   bool isUnsafe = false;

   bool isArgImm = false;
   bool isArgImmShifted = false;

   bool fieldOffsetRegIsEval = false;
   bool isRefWrite = false;

   bool newPair = false;
   bool expPair = false;

   bool stopNewComposedCopy = false;

   int64_t expValImm;
   int32_t delta;
   uint8_t size = 4;
   int32_t fieldOffset = 0;
   int32_t idx;
   uint8_t numDeps = 1;

   TR::RecognizedMethod currentMethod = method->getRecognizedMethod();

   switch (currentMethod)
      {
   case TR::sun_misc_Unsafe_compareAndSwapLong_jlObjectJJJ_Z:
      size = 8;
      isUnsafe = true;
      isCAS = true;
      isAddOp = false;
      break;
   case TR::sun_misc_Unsafe_compareAndSwapObject_jlObjectJjlObjectjlObject_Z:
      size = TR::Compiler->om.sizeofReferenceAddress();
      isRefWrite = true;
   case TR::sun_misc_Unsafe_compareAndSwapInt_jlObjectJII_Z:
      isUnsafe = true;
      isCAS = true;
      isAddOp = false;
      break;
   case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicInteger_incrementAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_incrementAndGet:
      size = 4;
      break;
   case TR::java_util_concurrent_atomic_AtomicReference_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicReferenceArray_getAndSet:
      size = TR::Compiler->om.sizeofReferenceAddress();
      break;
   default:
      size = 8;
      }

   switch (currentMethod)
      {
   case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicLong_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicLongArray_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicReference_getAndSet:
   case TR::java_util_concurrent_atomic_AtomicReferenceArray_getAndSet:
      isAddOp = false;
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicLong_getAndAdd:
   case TR::java_util_concurrent_atomic_AtomicLongArray_getAndAdd:
      isDelta = true;
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicLong_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicLong_getAndIncrement:
   case TR::java_util_concurrent_atomic_AtomicLongArray_getAndDecrement:
   case TR::java_util_concurrent_atomic_AtomicLongArray_getAndIncrement:
      isGetAndOp = true;
      break;
   case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicLong_addAndGet:
   case TR::java_util_concurrent_atomic_AtomicLongArray_addAndGet:
      isDelta = true;
      break;
      }

   isDeltaImplied = isAddOp && !isDelta;

   if (isDeltaImplied)
      {
      switch (currentMethod)
         {
      case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicLong_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicLongArray_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLongArray_getAndDecrement:
         delta = (int32_t) - 1;
      default:
         delta = (int32_t) 1;
         }
      }

   if (isUnsafe && isCAS)
      {
      firstChild = node->getChild(0);
      valueChild = node->getChild(1);
      indexChild = node->getChild(2);
      if (isRefWrite)
         {
         expRefChild = node->getChild(3);
         newRefChild = node->getChild(4);
         }
      else
         {
         expValChild = node->getChild(3);
         newValChild = node->getChild(4);
         }
      }
   else
      {
      valueChild = node->getChild(0);
      idx = 1;
      if (isArray)
         {
         indexChild = node->getChild(idx++);
         }
      if (isDelta)
         {
         deltaChild = node->getChild(idx++);
         }
      else if (isCAS)
         {
         expValChild = node->getChild(idx);
         newValChild = node->getChild(idx + 1);
         idx += 2;
         }
      if (idx != node->getNumChildren())
         {
         TR_ASSERT(0, "Wrong number of children for JUC Atomic node\n");
         return NULL;
         }
      }

   valueReg = cg->evaluate(valueChild);
   objReg = valueReg;

   if (isUnsafe)
      {
      if (indexChild->getOpCode().isLoadConst() && indexChild->getRegister() == NULL && comp->target().is32Bit())
         {
         fieldOffset = (int32_t) indexChild->getLongInt();
         //traceMsg(comp,"Allocate fieldOffsetReg\n");
         fieldOffsetReg = cg->allocateRegister();
         numDeps++;
         loadConstant(cg, node, fieldOffset, fieldOffsetReg);
         }
      else
         {
         fieldOffsetReg = cg->evaluate(indexChild);
         resultReg = cg->allocateRegister();
         numDeps += 2;
         if (comp->target().is32Bit())
            fieldOffsetReg = fieldOffsetReg->getLowOrder();
         }
      }
   else if (!isArray)
      {
      TR_OpaqueClassBlock *classBlock;
      char *className, *fieldSig;
      int32_t classNameLen, fieldSigLen;
      fieldSigLen = 1;

      switch (currentMethod)
         {
      case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
         className = "Ljava/util/concurrent/atomic/AtomicBoolean;";
         classNameLen = 43;
         fieldSig = "I"; // not a type, the field is int
      case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicInteger_incrementAndGet:
         className = "Ljava/util/concurrent/atomic/AtomicInteger;";
         classNameLen = 43;
         fieldSig = "I";
         break;
      case TR::java_util_concurrent_atomic_AtomicLong_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicLong_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicLong_incrementAndGet:
         className = "Ljava/util/concurrent/atomic/AtomicLong;";
         classNameLen = 40;
         fieldSig = "J";
         break;
      case TR::java_util_concurrent_atomic_AtomicReference_getAndSet:
         className = "Ljava/util/concurrent/atomic/AtomicReference;";
         classNameLen = 45;
         fieldSig = "Ljava/lang/Object;";
         fieldSigLen = 18;
         break;
      default:
         TR_ASSERT(0, "Unknown atomic operation method\n");
         return NULL;
         }
      classBlock = fej9->getClassFromSignature(className, classNameLen, comp->getCurrentMethod(), true);
      fieldOffset = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "value", 5, fieldSig, fieldSigLen);
      }
   else // isArray
      {
      TR::Register *indexReg = cg->evaluate(indexChild);
      int32_t shiftAmount = size == 8 ? 3 : 2;
      fieldOffset = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();

      TR_OpaqueClassBlock *classBlock;
      char *className, *fieldSig;
      int32_t classNameLen, fieldSigLen;
      fieldSigLen = 1;

      switch (currentMethod)
         {
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_incrementAndGet:
         className = "Ljava/util/concurrent/atomic/AtomicIntegerArray;";
         classNameLen = 48;
         fieldSig = "[I";
         fieldSigLen = 2;
         break;
      case TR::java_util_concurrent_atomic_AtomicLongArray_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicLongArray_decrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicLongArray_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicLongArray_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicLongArray_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicLongArray_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicLongArray_incrementAndGet:
         className = "Ljava/util/concurrent/atomic/AtomicLongArray;";
         classNameLen = 45;
         fieldSig = "[J";
         fieldSigLen = 2;
         break;
      case TR::java_util_concurrent_atomic_AtomicReferenceArray_getAndSet:
         className = "Ljava/util/concurrent/atomic/AtomicReferenceArray;";
         classNameLen = 50;
         fieldSig = "[Ljava/lang/Object;";
         fieldSigLen = 19;
         break;
      default:
         TR_ASSERT(0, "Unknown atomic operation method\n");
         return NULL;
         }
      classBlock = fej9->getClassFromSignature(className, classNameLen, comp->getCurrentMethod(), true);
      int32_t arrayFieldOffset = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "array", 5, fieldSig, fieldSigLen);

      TR::MemoryReference *tempMR = TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, size);
      scratchReg = cg->allocateCollectedReferenceRegister();

      if (TR::Compiler->om.compressObjectReferences())
         // read only 32 bits
         generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, scratchReg,
               TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, 4));
      else
         generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, scratchReg, TR::MemoryReference::createWithDisplacement(cg, valueReg, arrayFieldOffset, TR::Compiler->om.sizeofReferenceAddress()));

      valueReg = scratchReg;
      fieldOffsetReg = cg->allocateRegister();
      numDeps += 2;
      generateShiftLeftImmediate(cg, node, fieldOffsetReg, indexReg, shiftAmount);
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, fieldOffsetReg, fieldOffsetReg, fieldOffset);
      }

   if (isUnsafe)
      {
      //right now not checking for ref, just doing long!
      if (isRefWrite)
         {
         }
      else
         {
         if (expValChild->getOpCode().isLoadConst() && expValChild->getRegister() == NULL)
            {
            if (size == 8)
               expValImm = expValChild->getLongInt();
            else
               expValImm = expValChild->getInt();
            if (expValImm >= LOWER_IMMED && expValImm <= UPPER_IMMED)
               isArgImm = true;
            }

         if (!isArgImm)
            {
            expValReg = cg->evaluate(expValChild);
            numDeps++;
            }

         newValReg = cg->evaluate(newValChild);
         numDeps++;
         }
      }

   if (isCAS)
      {
      TR::LabelSymbol *rsvFailLabel;
      cndReg = cg->allocateRegister(TR_CCR);
      currentReg = cg->allocateRegister();
      numDeps += 2;
      startLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
      failLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
      doneLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);

      //allow spurious failures
      rsvFailLabel = isWeak ? failLabel : startLabel;

      startLabel->setStartInternalControlFlow();

      if (fieldOffsetReg == NULL)
         {
         numDeps++;
         fieldOffsetReg = cg->allocateRegister();
         loadConstant(cg, node, fieldOffset, fieldOffsetReg);
         }
      if (resultReg == NULL)
         {
         resultReg = fieldOffsetReg;
         }

      if (!isWeak)
         generateInstruction(cg, TR::InstOpCode::lwsync, node);

      generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

      generateTrg1MemInstruction(cg, size == 8 ? TR::InstOpCode::ldarx : TR::InstOpCode::lwarx, node, currentReg,
            TR::MemoryReference::createWithIndexReg(cg, valueReg, (fieldOffsetReg->getRegisterPair() ? fieldOffsetReg->getLowOrder() : fieldOffsetReg), size));

      if (size == 8 && comp->target().is32Bit())
         {
         if (!isArgImm && expValReg->getRegisterPair())
            {
            generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldimi, node, expValReg->getLowOrder(), expValReg->getHighOrder(), 32, CONSTANT64(0xFFFFFFFF00000000));
            expPair = true;
            numDeps++;
            }
         if (newValReg->getRegisterPair())
            {
            if (newValReg != expValReg)
               {
               generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldimi, node, newValReg->getLowOrder(), newValReg->getHighOrder(), 32, CONSTANT64(0xFFFFFFFF00000000));
               }
            numDeps++;
            newPair = true;
            }
         }

      if (!isArgImm && expComposedReg == NULL)
         {
         if (expPair)
            expComposedReg = expValReg->getLowOrder();
         else
            expComposedReg = expValReg != NULL ? expValReg : expRefReg;
         }
      if (newComposedReg == NULL)
         {
         if (newPair)
            newComposedReg = newValReg->getLowOrder();
         else
            newComposedReg = newValReg != NULL ? newValReg : newRefReg;
         }

      if (isArgImm)
         {
         generateTrg1Src1ImmInstruction(cg, size == 8 ? TR::InstOpCode::cmpi8 : TR::InstOpCode::cmpi4, node, cndReg, currentReg, expValImm);
         }
      else
         {
         generateTrg1Src2Instruction(cg, size == 8 ? TR::InstOpCode::cmp8 : TR::InstOpCode::cmp4, node, cndReg, currentReg, expComposedReg);
         }

      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, failLabel, cndReg);
      generateMemSrc1Instruction(cg, size == 8 ? TR::InstOpCode::stdcx_r : TR::InstOpCode::stwcx_r, node,
            TR::MemoryReference::createWithIndexReg(cg, valueReg, (fieldOffsetReg->getRegisterPair() ? fieldOffsetReg->getLowOrder() : fieldOffsetReg), size),
            newComposedReg);

      /* Expect store to be successful, so this branch is usually not taken */
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, rsvFailLabel, cndReg);

      if (!isWeak)
         generateInstruction(cg, TR::InstOpCode::sync, node);

      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, doneLabel);
      generateLabelInstruction(cg, TR::InstOpCode::label, node, failLabel);

      if (!isWeak && size == 8 && comp->target().is32Bit())
         {
         /* store original current value conditionally
          * if it succeeds, then this was a true failure, if it
          * does not, it could have been corruption messing up the compare
          */
         generateMemSrc1Instruction(cg, size == 8 ? TR::InstOpCode::stdcx_r : TR::InstOpCode::stwcx_r, node,
               TR::MemoryReference::createWithIndexReg(cg, valueReg, (fieldOffsetReg->getRegisterPair() ? fieldOffsetReg->getLowOrder() : fieldOffsetReg), size),
               currentReg);
         /* Expect store to be successful, so this branch is usually not taken */
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, PPCOpProp_BranchUnlikely, node, startLabel, cndReg);
         }

      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0);

      }

   TR::RegisterDependencyConditions *conditions;
   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, numDeps, cg->trMemory());
   conditions->addPostCondition(valueReg, TR::RealRegister::NoReg);
   conditions->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0();
   conditions->addPostCondition(currentReg, TR::RealRegister::NoReg);

   numDeps -= 2;
   if (fieldOffsetReg != NULL)
      {
      conditions->addPostCondition(fieldOffsetReg, TR::RealRegister::NoReg);
      numDeps--;
      }
   if (resultReg != fieldOffsetReg)
      {
      conditions->addPostCondition(resultReg, TR::RealRegister::NoReg);
      numDeps--;
      }
   if (objReg != valueReg)
      {
      conditions->addPostCondition(objReg, TR::RealRegister::NoReg);
      numDeps--;
      }
   if (isUnsafe)
      {
      if (isRefWrite)
         {
         conditions->addPostCondition(newRefReg, TR::RealRegister::NoReg);
         conditions->addPostCondition(expRefReg, TR::RealRegister::NoReg);
         numDeps -= 2;
         }
      else
         {
         if (newPair)
            {
            conditions->addPostCondition(newValReg->getHighOrder(), TR::RealRegister::NoReg);
            conditions->addPostCondition(newValReg->getLowOrder(), TR::RealRegister::NoReg);
            numDeps--;
            }
         else
            {
            conditions->addPostCondition(newValReg, TR::RealRegister::NoReg);
            }
         numDeps--;
         if (!isArgImm)
            {
            if (expPair)
               {
               conditions->addPostCondition(expValReg->getHighOrder(), TR::RealRegister::NoReg);
               conditions->addPostCondition(expValReg->getLowOrder(), TR::RealRegister::NoReg);
               numDeps--;
               }
            else
               {
               conditions->addPostCondition(expValReg, TR::RealRegister::NoReg);
               }
            numDeps--;
            }
         }
      }

   conditions->addPostCondition(cndReg, TR::RealRegister::cr0);
   numDeps--;

   doneLabel->setEndInternalControlFlow();
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

   if (firstChild)
      cg->decReferenceCount(firstChild);
   cg->decReferenceCount(valueChild);
   cg->decReferenceCount(indexChild);
   cg->decReferenceCount(expValChild);
   cg->decReferenceCount(newValChild);
   cg->stopUsingRegister(cndReg);
   cg->stopUsingRegister(currentReg);
   node->setRegister(resultReg);

   return resultReg;
   }

static TR::Register *compressStringEvaluator(TR::Node *node, TR::CodeGenerator *cg, bool japaneseMethod)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   TR::Node *srcObjNode, *dstObjNode, *startNode, *lengthNode;
   TR::Register *srcObjReg = NULL, *dstObjReg = NULL, *lengthReg = NULL, *startReg = NULL;

   srcObjNode = node->getChild(0);
   dstObjNode = node->getChild(1);
   startNode = node->getChild(2);
   lengthNode = node->getChild(3);

   bool stopUsingCopyReg1, stopUsingCopyReg2, stopUsingCopyReg3, stopUsingCopyReg4;

   stopUsingCopyReg1 = TR::TreeEvaluator::stopUsingCopyReg(srcObjNode, srcObjReg, cg);
   stopUsingCopyReg2 = TR::TreeEvaluator::stopUsingCopyReg(dstObjNode, dstObjReg, cg);
   stopUsingCopyReg3 = TR::TreeEvaluator::stopUsingCopyReg(startNode, startReg, cg);
   stopUsingCopyReg4 = TR::TreeEvaluator::stopUsingCopyReg(lengthNode, lengthReg, cg);

   uintptr_t hdrSize = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, srcObjReg, srcObjReg, hdrSize);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dstObjReg, dstObjReg, hdrSize);

   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(12, 12, cg->trMemory());
   TR::Register *cndRegister = cg->allocateRegister(TR_CCR);
   TR::Register *resultReg = cg->allocateRegister(TR_GPR);
   TR::addDependency(conditions, cndRegister, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, lengthReg, TR::RealRegister::gr8, TR_GPR, cg);
   TR::addDependency(conditions, startReg, TR::RealRegister::gr7, TR_GPR, cg);
   TR::addDependency(conditions, srcObjReg, TR::RealRegister::gr9, TR_GPR, cg);
   TR::addDependency(conditions, dstObjReg, TR::RealRegister::gr10, TR_GPR, cg);

   TR::addDependency(conditions, NULL, TR::RealRegister::gr0, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr11, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr6, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr4, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr5, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr12, TR_GPR, cg);
   TR::addDependency(conditions, resultReg, TR::RealRegister::gr3, TR_GPR, cg);

   if (japaneseMethod)
      TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(TR_PPCcompressStringJ, node, conditions, cg);
   else
      TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(TR_PPCcompressString, node, conditions, cg);

   TR::Register* regs[5] =
      {
      lengthReg, startReg, srcObjReg, dstObjReg, resultReg
      };
   conditions->stopUsingDepRegs(cg, 5, regs);
   for (uint16_t i = 0; i < node->getNumChildren(); i++)
      cg->decReferenceCount(node->getChild(i));
   if (stopUsingCopyReg1)
      cg->stopUsingRegister(srcObjReg);
   if (stopUsingCopyReg2)
      cg->stopUsingRegister(dstObjReg);
   if (stopUsingCopyReg3)
      cg->stopUsingRegister(startReg);
   if (stopUsingCopyReg4)
      cg->stopUsingRegister(lengthReg);

   node->setRegister(resultReg);

   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();
   return (resultReg);
   }

static TR::Register *compressStringNoCheckEvaluator(TR::Node *node, TR::CodeGenerator *cg, bool japaneseMethod)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   TR::Node *srcObjNode, *dstObjNode, *startNode, *lengthNode;
   TR::Register *srcObjReg = NULL, *dstObjReg = NULL, *lengthReg = NULL, *startReg = NULL;

   srcObjNode = node->getChild(0);
   dstObjNode = node->getChild(1);
   startNode = node->getChild(2);
   lengthNode = node->getChild(3);

   bool stopUsingCopyReg1, stopUsingCopyReg2, stopUsingCopyReg3, stopUsingCopyReg4;

   stopUsingCopyReg1 = TR::TreeEvaluator::stopUsingCopyReg(srcObjNode, srcObjReg, cg);
   stopUsingCopyReg2 = TR::TreeEvaluator::stopUsingCopyReg(dstObjNode, dstObjReg, cg);
   stopUsingCopyReg3 = TR::TreeEvaluator::stopUsingCopyReg(startNode, startReg, cg);
   stopUsingCopyReg4 = TR::TreeEvaluator::stopUsingCopyReg(lengthNode, lengthReg, cg);

   uintptr_t hdrSize = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, srcObjReg, srcObjReg, hdrSize);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, dstObjReg, dstObjReg, hdrSize);

   int numOfRegs = japaneseMethod ? 11 : 12;
   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(numOfRegs, numOfRegs, cg->trMemory());
   TR::Register *cndRegister = cg->allocateRegister(TR_CCR);
   TR::addDependency(conditions, cndRegister, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, lengthReg, TR::RealRegister::gr8, TR_GPR, cg);
   TR::addDependency(conditions, startReg, TR::RealRegister::gr7, TR_GPR, cg);
   TR::addDependency(conditions, srcObjReg, TR::RealRegister::gr9, TR_GPR, cg);
   TR::addDependency(conditions, dstObjReg, TR::RealRegister::gr10, TR_GPR, cg);

   TR::addDependency(conditions, NULL, TR::RealRegister::gr0, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr11, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr6, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr4, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr5, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr3, TR_GPR, cg);
   if (!japaneseMethod)
      TR::addDependency(conditions, NULL, TR::RealRegister::gr12, TR_GPR, cg);

   if (japaneseMethod)
      TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(TR_PPCcompressStringNoCheckJ, node, conditions, cg);
   else
      TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(TR_PPCcompressStringNoCheck, node, conditions, cg);

   TR::Register* regs[4] =
      {
      lengthReg, startReg, srcObjReg, dstObjReg
      };
   conditions->stopUsingDepRegs(cg, 4, regs);
   for (uint16_t i = 0; i < node->getNumChildren(); i++)
      cg->decReferenceCount(node->getChild(i));
   if (stopUsingCopyReg1)
      cg->stopUsingRegister(srcObjReg);
   if (stopUsingCopyReg2)
      cg->stopUsingRegister(dstObjReg);
   if (stopUsingCopyReg3)
      cg->stopUsingRegister(startReg);
   if (stopUsingCopyReg4)
      cg->stopUsingRegister(lengthReg);

   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();
   return NULL;
   }

static TR::Register *andORStringEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (cg->comp()->fe());
   TR::Node *srcObjNode, *startNode, *lengthNode;
   TR::Register *srcObjReg = NULL, *lengthReg = NULL, *startReg = NULL;

   srcObjNode = node->getChild(0);
   startNode = node->getChild(1);
   lengthNode = node->getChild(2);

   bool stopUsingCopyReg1, stopUsingCopyReg2, stopUsingCopyReg3;

   stopUsingCopyReg1 = TR::TreeEvaluator::stopUsingCopyReg(srcObjNode, srcObjReg, cg);
   stopUsingCopyReg2 = TR::TreeEvaluator::stopUsingCopyReg(startNode, startReg, cg);
   stopUsingCopyReg3 = TR::TreeEvaluator::stopUsingCopyReg(lengthNode, lengthReg, cg);

   uintptr_t hdrSize = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, srcObjReg, srcObjReg, hdrSize);

   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(8, 8, cg->trMemory());
   TR::Register *cndRegister = cg->allocateRegister(TR_CCR);
   TR::Register *resultReg = cg->allocateRegister(TR_GPR);
   TR::addDependency(conditions, cndRegister, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, lengthReg, TR::RealRegister::gr8, TR_GPR, cg);
   TR::addDependency(conditions, startReg, TR::RealRegister::gr7, TR_GPR, cg);
   TR::addDependency(conditions, srcObjReg, TR::RealRegister::gr9, TR_GPR, cg);

   TR::addDependency(conditions, NULL, TR::RealRegister::gr0, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr4, TR_GPR, cg);
   TR::addDependency(conditions, NULL, TR::RealRegister::gr5, TR_GPR, cg);
   TR::addDependency(conditions, resultReg, TR::RealRegister::gr3, TR_GPR, cg);

   TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(TR_PPCandORString, node, conditions, cg);

   TR::Register* regs[4] =
      {
      lengthReg, startReg, srcObjReg, resultReg
      };
   conditions->stopUsingDepRegs(cg, 4, regs);

   for (uint16_t i = 0; i < node->getNumChildren(); i++)
      cg->decReferenceCount(node->getChild(i));
   if (stopUsingCopyReg1)
      cg->stopUsingRegister(srcObjReg);
   if (stopUsingCopyReg2)
      cg->stopUsingRegister(startReg);
   if (stopUsingCopyReg3)
      cg->stopUsingRegister(lengthReg);

   node->setRegister(resultReg);

   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();
   return (resultReg);
   }

static TR::Register *inlineConcurrentLinkedQueueTMOffer(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   int32_t addressFieldSize = TR::Compiler->om.sizeofReferenceField();
   TR::InstOpCode::Mnemonic loadOpCode = (addressFieldSize == 8) ? TR::InstOpCode::ld : TR::InstOpCode::lwz;
   TR::InstOpCode::Mnemonic storeOpCode = (addressFieldSize == 8) ? TR::InstOpCode::std : TR::InstOpCode::stw;
   bool usesCompressedrefs = comp->useCompressedPointers();

   TR_OpaqueClassBlock * classBlock = NULL;
   classBlock = fej9->getClassFromSignature("Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49, comp->getCurrentMethod(), true);
   int32_t offsetNext = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "next", 4, "Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49);
   classBlock = fej9->getClassFromSignature("Ljava/util/concurrent/ConcurrentLinkedQueue;", 44, comp->getCurrentMethod(), true);
   int32_t offsetTail = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "tail", 4, "Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49);

   TR::Register * cndReg = cg->allocateRegister(TR_CCR);
   TR::Register * resultReg = cg->allocateRegister();
   TR::Register * objReg = cg->evaluate(node->getFirstChild());
   TR::Register * nReg = cg->evaluate(node->getSecondChild());
   TR::Register * pReg = cg->allocateRegister();
   TR::Register * qReg = cg->allocateRegister();
   TR::Register * retryCountReg = cg->allocateRegister();
   TR::Register * temp3Reg = NULL;
   TR::Register * temp4Reg = NULL;

   TR::LabelSymbol * loopLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * insertLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * failureLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * returnLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * wrtbar1Donelabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);

   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(9, 9, cg->trMemory());

   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, resultReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, pReg, TR::RealRegister::gr3, TR_GPR, cg); // dstReg for wrtbar
   TR::addDependency(conditions, qReg, TR::RealRegister::gr11, TR_GPR, cg); // temp1Reg for wrtbar
   TR::addDependency(conditions, retryCountReg, TR::RealRegister::NoReg, TR_GPR, cg); // temp2Reg for wrtbar
   TR::addDependency(conditions, nReg, TR::RealRegister::gr4, TR_GPR, cg); // srcReg for wrtbar

   static char * disableTMOffer = feGetEnv("TR_DisableTMOffer");
   static char * debugTM = feGetEnv("TR_DebugTM");

   /*
    * TM is not compatible with read barriers. If read barriers are required, TM is disabled.
    */
   if (disableTMOffer || TR::Compiler->om.readBarrierType() != gc_modron_readbar_none)
      {
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 3); // TM offer disabled
      generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);
      }

   if (debugTM)
      {
      printf("\nTM: use TM CLQ.Offer in %s (%s)", comp->signature(), comp->getHotnessName(comp->getMethodHotness()));
      fflush (stdout);
      }

   static const char *s = feGetEnv("TR_TMOfferRetry");
   static uint16_t TMOfferRetry = s ? atoi(s) : 7;

   //#define CLQ_OFFER_RETRY 7
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, retryCountReg, TMOfferRetry);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, loopLabel);

   generateInstruction(cg, TR::InstOpCode::tbegin_r, node);

   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, failureLabel, cndReg);

   // ALG: p := this.tail
   generateTrg1MemInstruction(cg, loadOpCode, node, pReg, TR::MemoryReference::createWithDisplacement(cg, objReg, offsetTail, addressFieldSize));
   if (usesCompressedrefs)
      {
      genDecompressPointerWithTempReg(cg, node, pReg, resultReg, NULL, false); // p is not null
      }

   // ALG: q := p.next
   generateTrg1MemInstruction(cg, loadOpCode, node, qReg, TR::MemoryReference::createWithDisplacement(cg, pReg, offsetNext, addressFieldSize));

   // ALG: if q == null goto insertLabel
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, qReg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, insertLabel, cndReg);

   if (usesCompressedrefs)
      {
      genDecompressPointerWithTempReg(cg, node, qReg, resultReg, NULL, false); // q has been checked not null
      }

   // ALG: p := q
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, pReg, qReg);

   // ALG: q := p.next
   generateTrg1MemInstruction(cg, loadOpCode, node, qReg, TR::MemoryReference::createWithDisplacement(cg, pReg, offsetNext, addressFieldSize));

   // ALG: if q == null goto insertLabel
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, qReg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, PPCOpProp_BranchLikely, node, insertLabel, cndReg);

   // ALG: tend.
   generateInstruction(cg, TR::InstOpCode::tend_r, node);

   // ALG: res := 1
   // cannot find tail
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1);

   // ALG: goto returnLabel
   generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);

   // ALG: *** insert:
   generateLabelInstruction(cg, TR::InstOpCode::label, node, insertLabel);

   // need uncompressed nReg for the wrtbar
   TR::Register *compressedReg = nReg;
   if (usesCompressedrefs)
      {
      compressedReg = genCompressPointerNonNull2RegsWithTempReg(cg, node, nReg, qReg, resultReg); //In Java code checkNotNull() already ensures n != null
      }
   // ALG: p.next := n
   generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, pReg, offsetNext, addressFieldSize), compressedReg);

   // ALG: this.tail = n
   generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, objReg, offsetTail, addressFieldSize), compressedReg);

   // ALG: tend
   generateInstruction(cg, TR::InstOpCode::tend_r, node);

   // ALG: res := 0
   // TM success
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0);

   // wrt bar
   auto gcMode = TR::Compiler->om.writeBarrierType();
   bool doWrtBar = (gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC());
   bool doCrdMrk = ((gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental) && (!node->getOpCode().isWrtBar() || !node->isNonHeapObjectWrtBar()));
   if (doWrtBar)
      {
      if (doCrdMrk)
         {
         temp3Reg = cg->allocateRegister();
         temp4Reg = cg->allocateRegister();
         TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         TR::addDependency(conditions, temp4Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         conditions->getPostConditions()->getRegisterDependency(5)->setExcludeGPR0(); //5=temp2Reg
         }

      VMnonNullSrcWrtBarCardCheckEvaluator(node, nReg, pReg, cndReg, qReg, retryCountReg, temp3Reg, temp4Reg, wrtbar1Donelabel, conditions, false, cg, false);
      // ALG: *** wrtbar1Donelabel
      generateLabelInstruction(cg, TR::InstOpCode::label, node, wrtbar1Donelabel);

      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, pReg, objReg);

      VMnonNullSrcWrtBarCardCheckEvaluator(node, nReg, pReg, cndReg, qReg, retryCountReg, temp3Reg, temp4Reg, returnLabel, conditions, false, cg, false);

      }
   else if (doCrdMrk)
      {
      temp3Reg = cg->allocateRegister();
      TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);
      conditions->getPostConditions()->getRegisterDependency(3)->setExcludeGPR0(); //3=dstReg
      conditions->getPostConditions()->getRegisterDependency(4)->setExcludeGPR0(); //4=temp1Reg

      VMCardCheckEvaluator(node, pReg, cndReg, qReg, retryCountReg, temp3Reg, conditions, cg);

      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, pReg, objReg);

      VMCardCheckEvaluator(node, pReg, cndReg, qReg, retryCountReg, temp3Reg, conditions, cg);
      }

   // ALG: goto returnLabel
   generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);

   // ALG: *** failureLabel
   generateLabelInstruction(cg, TR::InstOpCode::label, node, failureLabel);
   //generateDepLabelInstruction(cg, TR::InstOpCode::label, node, failureLabel, conditions);

   // retryCount-=1
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addic_r, node, retryCountReg, retryCountReg, -1);

   // ALG: goto loopLabel
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, loopLabel, cndReg);

   // ALG: res := 2
   // exceeds retry count
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 2);

   // ALG: *** return
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, returnLabel, conditions);

   // wrtbar or after TM success earlier

   cg->decReferenceCount(node->getFirstChild());
   cg->decReferenceCount(node->getSecondChild());
   cg->stopUsingRegister(pReg);
   cg->stopUsingRegister(qReg);
   cg->stopUsingRegister(retryCountReg);
   cg->stopUsingRegister(cndReg);
   if (temp3Reg != NULL)
      cg->stopUsingRegister(temp3Reg);
   if (temp4Reg != NULL)
      cg->stopUsingRegister(temp4Reg);

   node->setRegister(resultReg);
   resultReg->setContainsCollectedReference();
   return resultReg;
   }

static TR::Register *inlineConcurrentLinkedQueueTMPoll(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   int32_t addressFieldSize = TR::Compiler->om.sizeofReferenceField();
   TR::InstOpCode::Mnemonic loadOpCode = (addressFieldSize == 8) ? TR::InstOpCode::ld : TR::InstOpCode::lwz;
   TR::InstOpCode::Mnemonic storeOpCode = (addressFieldSize == 8) ? TR::InstOpCode::std : TR::InstOpCode::stw;
   bool usesCompressedrefs = comp->useCompressedPointers();

   TR_OpaqueClassBlock *classBlock = fej9->getClassFromSignature("Ljava/util/concurrent/ConcurrentLinkedQueue;", 44, comp->getCurrentMethod(), true);
   int32_t offsetHead = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "head", 4, "Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49);
   classBlock = fej9->getClassFromSignature("Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49, comp->getCurrentMethod(), true);
   int32_t offsetNext = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "next", 4, "Ljava/util/concurrent/ConcurrentLinkedQueue$Node;", 49);
   int32_t offsetItem = fej9->getObjectHeaderSizeInBytes() + fej9->getInstanceFieldOffset(classBlock, "item", 4, "Ljava/lang/Object;", 18);

   TR::Register * objReg = cg->evaluate(node->getFirstChild());
   TR::Register * cndReg = cg->allocateRegister(TR_CCR);
   TR::Register * resultReg = cg->allocateRegister();
   TR::Register * nullReg = cg->allocateRegister();
   TR::Register * pReg = cg->allocateRegister();
   TR::Register * qReg = cg->allocateRegister();
   TR::Register * temp3Reg = cg->allocateRegister();
   TR::Register * temp4Reg = NULL;

   TR::LabelSymbol * tendLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * returnLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
   TR::LabelSymbol * failureLabel = TR::LabelSymbol::create(cg->trHeapMemory(),cg);

   TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(8, 8, cg->trMemory());

   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, resultReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, objReg, TR::RealRegister::gr3, TR_GPR, cg); // dstReg for wrtbar
   TR::addDependency(conditions, nullReg, TR::RealRegister::gr11, TR_GPR, cg); // temp1Reg for wrtbar
   TR::addDependency(conditions, pReg, TR::RealRegister::NoReg, TR_GPR, cg); // temp2Reg for wrtbar
   TR::addDependency(conditions, qReg, TR::RealRegister::gr4, TR_GPR, cg); // srcReg for wrtbar
   TR::addDependency(conditions, temp3Reg, TR::RealRegister::NoReg, TR_GPR, cg);

   static char * disableTMPoll = feGetEnv("TR_DisableTMPoll");
   static char * debugTM = feGetEnv("TR_DebugTM");

   /*
    * TM is not compatible with read barriers. If read barriers are required, TM is disabled.
    */
   if (disableTMPoll || TR::Compiler->om.readBarrierType() != gc_modron_readbar_none)
      {
      generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0); // BEFORE WAS 0
      generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);
      }

   if (debugTM)
      {
      printf("\nTM: use TM CLQ.Poll in %s (%s)", comp->signature(), comp->getHotnessName(comp->getMethodHotness()));
      fflush (stdout);
      }

   generateInstruction(cg, TR::InstOpCode::tbegin_r, node);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, failureLabel, cndReg);

   // ALG: p := this.head
   generateTrg1MemInstruction(cg, loadOpCode, node, temp3Reg, TR::MemoryReference::createWithDisplacement(cg, objReg, offsetHead, addressFieldSize));
   TR::Register *decompressedP = temp3Reg;
   if (usesCompressedrefs)
      {
      decompressedP = genDecompressPointerNonNull2RegsWithTempReg(cg, node, temp3Reg, pReg, qReg);
      }

   // ALG: res := p.item
   generateTrg1MemInstruction(cg, loadOpCode, node, resultReg, TR::MemoryReference::createWithDisplacement(cg, decompressedP, offsetItem, addressFieldSize));
   if (usesCompressedrefs)
      {
      genDecompressPointerWithTempReg(cg, node, resultReg, qReg, cndReg); // NOTE: res could be null
      }

   // ALG: q := p.next
   generateTrg1MemInstruction(cg, loadOpCode, node, qReg, TR::MemoryReference::createWithDisplacement(cg, decompressedP, offsetNext, addressFieldSize));

   // ALG: p.item := null
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, nullReg, 0);

   generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, decompressedP, offsetItem, addressFieldSize), nullReg);

   // ALG: if q == null goto tendLabel
   generateTrg1Src1ImmInstruction(cg,TR::InstOpCode::Op_cmpli, node, cndReg, qReg, 0);

   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, tendLabel, cndReg);

   // ALG: this.head = q
   // qReg is still compressed, use it to store
   generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, objReg, offsetHead, addressFieldSize), qReg);

   if (usesCompressedrefs)
      {
      genDecompressPointerWithTempReg(cg, node, qReg, nullReg, NULL, false); // qReg is not null
      }

   // ALG: p.next := p
   generateMemSrc1Instruction(cg, storeOpCode, node, TR::MemoryReference::createWithDisplacement(cg, decompressedP, offsetNext, addressFieldSize), temp3Reg);

   // ALG: tend
   generateInstruction(cg, TR::InstOpCode::tend_r, node);

   // WrtBar for this.head = q
   auto gcMode = TR::Compiler->om.writeBarrierType();
   bool doWrtBar = (gcMode == gc_modron_wrtbar_satb || gcMode == gc_modron_wrtbar_oldcheck || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_always
         || comp->getOptions()->realTimeGC());
   bool doCrdMrk = ((gcMode == gc_modron_wrtbar_cardmark || gcMode == gc_modron_wrtbar_cardmark_and_oldcheck || gcMode == gc_modron_wrtbar_cardmark_incremental) && (!node->getOpCode().isWrtBar() || !node->isNonHeapObjectWrtBar()));
   if (doWrtBar)
      {
      if (doCrdMrk)
         {
         temp4Reg = cg->allocateRegister();
         TR::addDependency(conditions, temp4Reg, TR::RealRegister::NoReg, TR_GPR, cg);
         conditions->getPostConditions()->getRegisterDependency(4)->setExcludeGPR0(); //4=temp2Reg
         }

      VMnonNullSrcWrtBarCardCheckEvaluator(node, qReg, objReg, cndReg, nullReg, pReg, temp3Reg, temp4Reg, returnLabel, conditions, false, cg, false);
      }
   else if (doCrdMrk)
      {
      conditions->getPostConditions()->getRegisterDependency(2)->setExcludeGPR0(); //2=dstReg
      conditions->getPostConditions()->getRegisterDependency(3)->setExcludeGPR0(); //3=temp1Reg

      VMCardCheckEvaluator(node, objReg, cndReg, nullReg, pReg, temp3Reg, conditions, cg);
      }

   // ALG: goto returnLabel
   generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);

   // ALG:**** tendLabel:
   generateLabelInstruction(cg, TR::InstOpCode::label, node, tendLabel);

   // ALG: tend
   generateInstruction(cg, TR::InstOpCode::tend_r, node);

   // ALG: goto returnLabel
   generateLabelInstruction(cg, TR::InstOpCode::b, node, returnLabel);

   // ALG: *** failureLabel
   generateLabelInstruction(cg, TR::InstOpCode::label, node, failureLabel);
   // resultReg := 0
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 0);

   // ALG: *** returnLabel
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, returnLabel, conditions);

   // wrtbar if result is non-null

   cg->decReferenceCount(node->getFirstChild());
   cg->stopUsingRegister(nullReg);
   cg->stopUsingRegister(pReg);
   cg->stopUsingRegister(qReg);
   cg->stopUsingRegister(cndReg);
   if (temp3Reg != NULL)
      cg->stopUsingRegister(temp3Reg);
   if (temp4Reg != NULL)
      cg->stopUsingRegister(temp4Reg);

   node->setRegister(resultReg);
   resultReg->setContainsCollectedReference();
   return resultReg;
   }

#if defined(AIXPPC)
static TR::Register *dangerousGetCPU(
      TR::Node *node,
      TR::CodeGenerator *cg)
   {
   // This is a bad idea that works most of the time.
   // We don't bother with ANY of the usual native method protocol(!)
   // But we do adjust r1 to be way out of the way and then call
   // "mycpu" which is buried deep in the kernel

   TR::Register *retReg = cg->allocateRegister();
   TR::Register *gr1 = cg->allocateRegister();
   TR::Register *gr2 = cg->allocateRegister();
   TR::Register *tmpReg = cg->allocateRegister();
   TR::RegisterDependencyConditions *dependencies = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(4, 4, cg->trMemory());
   TR::Compilation *comp = cg->comp();
   TR::addDependency(dependencies, retReg, TR::RealRegister::gr3, TR_GPR, cg);
   TR::addDependency(dependencies, gr1, TR::RealRegister::gr1, TR_GPR, cg);
   TR::addDependency(dependencies, gr2, TR::RealRegister::gr2, TR_GPR, cg);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, gr1, gr1, -256);
   generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node,
         TR::MemoryReference::createWithDisplacement(cg, gr1, 0x14, TR::Compiler->om.sizeofReferenceAddress()),
         gr2);
   intptr_t xx[3];
   xx[0] = ((intptr_t *)(void *)&mycpu)[0];
   xx[1] = ((intptr_t *)(void *)&mycpu)[1];
   xx[2] = ((intptr_t *)(void *)&mycpu)[2];
   loadAddressConstant(cg, comp->compileRelocatableCode(), node, xx[0], tmpReg);
   loadAddressConstant(cg, comp->compileRelocatableCode(), node, xx[1], gr2);
   generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, tmpReg);
   generateInstruction(cg, TR::InstOpCode::bctrl, node);

   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, gr2,
         TR::MemoryReference::createWithDisplacement(cg, gr1, 0x14, TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, gr1, gr1, 256);
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, TR::LabelSymbol::create(cg->trHeapMemory(),cg), dependencies);
   cg->stopUsingRegister(gr1);
   cg->stopUsingRegister(gr2);
   cg->stopUsingRegister(tmpReg);
   node->setRegister(retReg);
   cg->machine()->setLinkRegisterKilled(true);
   return retReg;
   }
#else
static TR::Register *dangerousGetCPU(TR::Node *node, TR::CodeGenerator *cg)
   {
   return NULL;
   }
#endif

static TR::Register *inlineFixedTrg1Src1(TR::Node *node, TR::InstOpCode::Mnemonic op, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 1, "Wrong number of children in inlineFixedTrg1Src1");

   TR::Node *firstChild = node->getFirstChild();
   TR::Register *srcRegister = cg->evaluate(firstChild);
   TR::Register *targetRegister = cg->allocateRegister();

   generateTrg1Src1Instruction(cg, op, node, targetRegister, srcRegister);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);

   return targetRegister;
   }

static TR::Register *inlineLongNumberOfTrailingZeros(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR_ASSERT(node->getNumChildren() == 1, "Wrong number of children in inlineLongNumberOfTrailingZeros");

   TR::Node *firstChild = node->getFirstChild();
   TR::Register *srcRegister = cg->evaluate(firstChild);
   TR::Register *targetRegister = cg->allocateRegister();
   TR::Register *tempRegister = cg->allocateRegister();
   TR::Register *maskRegister = cg->allocateRegister();

   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addic, node, tempRegister, srcRegister->getLowOrder(), -1);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::addme, node, targetRegister, srcRegister->getHighOrder());
   generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, tempRegister, tempRegister, srcRegister->getLowOrder());
   generateTrg1Src2Instruction(cg, TR::InstOpCode::andc, node, targetRegister, targetRegister, srcRegister->getHighOrder());
   generateTrg1Src1Instruction(cg, TR::InstOpCode::cntlzw, node, targetRegister, targetRegister);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::cntlzw, node, tempRegister, tempRegister);
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, maskRegister, targetRegister, 27, 0x1);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::neg, node, maskRegister, maskRegister);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, tempRegister, tempRegister, maskRegister);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, targetRegister, targetRegister, tempRegister);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::subfic, node, targetRegister, targetRegister, 64);

   cg->stopUsingRegister(tempRegister);
   cg->stopUsingRegister(maskRegister);

   node->setRegister(targetRegister);
   cg->decReferenceCount(firstChild);

   return targetRegister;
   }

static TR::Register *inlineIsAssignableFrom(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());
   TR::Node        *receiverNode = node->getFirstChild();
   TR::Node        *parmNode = node->getSecondChild();

   // If the receiver class is known at compile-time and the receiver and parm classes aren't the same,
   // we can check if the receiver is a super class of the parm class
   // Look for:
   // icall java/lang/Class.isAssignableFrom(Ljava/lang/Class;)Z
   //   aloadi <javaLangClassFromClass>
   //      loadaddr receiverj9class
   int32_t receiverClassDepth = -1;
   if (receiverNode->getOpCodeValue() == TR::aloadi &&
       receiverNode->getSymbolReference() == comp->getSymRefTab()->findJavaLangClassFromClassSymbolRef() &&
       receiverNode->getFirstChild()->getOpCodeValue() == TR::loadaddr)
      {
      TR::Node            *receiverJ9ClassNode = receiverNode->getFirstChild();
      TR::SymbolReference *receiverJ9ClassSymRef = receiverJ9ClassNode->getSymbolReference();
      if (receiverJ9ClassSymRef && !receiverJ9ClassSymRef->isUnresolved())
         {
         TR::StaticSymbol *receiverJ9ClassSym = receiverJ9ClassSymRef->getSymbol()->getStaticSymbol();
         if (receiverJ9ClassSym)
            {
            TR_OpaqueClassBlock *receiverJ9ClassPtr = (TR_OpaqueClassBlock *)receiverJ9ClassSym->getStaticAddress();
            if (receiverJ9ClassPtr)
               {
               receiverClassDepth = (int32_t)TR::Compiler->cls.classDepthOf(receiverJ9ClassPtr);
               if (receiverClassDepth >= 0)
                  {
                  static bool disable = feGetEnv("TR_disableInlineIsAssignableFromSuperTest") != NULL;
                  if (disable)
                     receiverClassDepth = -1;
                  }
               }
            }
         }
      }

   TR_PPCScratchRegisterManager *srm = cg->generateScratchRegisterManager();
   TR::Register                *receiverReg = cg->evaluate(node->getFirstChild());
   TR::Register                *parmReg = cg->evaluate(node->getSecondChild());
   TR::Register                *resultReg = cg->allocateRegister();
   TR::LabelSymbol                *outlinedCallLabel = generateLabelSymbol(cg);
   TR::LabelSymbol                *returnTrueLabel = generateLabelSymbol(cg);
   TR::LabelSymbol                *doneLabel = generateLabelSymbol(cg);

   doneLabel->setEndInternalControlFlow();

   // We don't want this guy to be live across the call to isAssignableFrom in the outlined section
   // because the CR reg will have to be spilled/restored.
   // Instead, we allocate it outside the srm and don't bother putting it in the dependencies on doneLabel.
   // For correctness we could free all CRs in the post conditions of doneLabel, but currently CRs are
   // never live outside of the evaluator they were created in so it's not a concern.
   TR::Register                *condReg = cg->allocateRegister(TR_CCR);

   // This is fine since we don't use any scratch regs after writing to the result reg
   srm->donateScratchRegister(resultReg);

   // If either the receiver or the parm is null, bail out
   TR::Register *nullTestReg = srm->findOrCreateScratchRegister();
   if (!receiverNode->isNonNull())
      {
      genNullTest(node, receiverReg, condReg, nullTestReg, NULL, cg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, outlinedCallLabel, condReg);
      }
   if (!parmNode->isNonNull())
      {
      genNullTest(node, parmReg, condReg, nullTestReg, NULL, cg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, outlinedCallLabel, condReg);
      }
   srm->reclaimScratchRegister(nullTestReg);

   // Compare receiver and parm
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, receiverReg, parmReg);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, returnTrueLabel, condReg);

   // Compare receiver and parm classes
   TR::Register *receiverJ9ClassReg = srm->findOrCreateScratchRegister();
   TR::Register *parmJ9ClassReg = srm->findOrCreateScratchRegister();
   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, receiverJ9ClassReg,
                              TR::MemoryReference::createWithDisplacement(cg, receiverReg, fej9->getOffsetOfClassFromJavaLangClassField(), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, parmJ9ClassReg,
                              TR::MemoryReference::createWithDisplacement(cg, parmReg, fej9->getOffsetOfClassFromJavaLangClassField(), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src2Instruction(cg,TR::InstOpCode::Op_cmpl, node, condReg, receiverJ9ClassReg, parmJ9ClassReg);

   TR::Register *scratch1Reg = NULL;
   if (receiverClassDepth >= 0)
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, returnTrueLabel, condReg);

      scratch1Reg = srm->findOrCreateScratchRegister();
      TR::Register *scratch2Reg = srm->findOrCreateScratchRegister();

      genTestIsSuper(node, parmJ9ClassReg, receiverJ9ClassReg, condReg, scratch1Reg, scratch2Reg, receiverClassDepth, outlinedCallLabel, NULL, cg);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, outlinedCallLabel, condReg);

      srm->reclaimScratchRegister(scratch1Reg);
      srm->reclaimScratchRegister(scratch2Reg);
      }
   else
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, outlinedCallLabel, condReg);
      }

   srm->reclaimScratchRegister(receiverJ9ClassReg);
   srm->reclaimScratchRegister(parmJ9ClassReg);
   generateLabelInstruction(cg, TR::InstOpCode::label, node, returnTrueLabel);
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, resultReg, 1);

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 2 + srm->numAvailableRegisters(), cg->trMemory());
   deps->addPostCondition(receiverReg, TR::RealRegister::NoReg, UsesDependentRegister | ExcludeGPR0InAssigner);
   deps->addPostCondition(parmReg, TR::RealRegister::NoReg, UsesDependentRegister | ExcludeGPR0InAssigner);
   srm->addScratchRegistersToDependencyList(deps);
   // Make sure these two (added to the deps by the srm) have !gr0, since we use them as base regs
   deps->setPostDependencyExcludeGPR0(receiverJ9ClassReg);
   if (scratch1Reg)
      deps->setPostDependencyExcludeGPR0(scratch1Reg);
   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, deps);

   TR_PPCOutOfLineCodeSection *outlinedHelperCall = new (cg->trHeapMemory()) TR_PPCOutOfLineCodeSection(node, TR::icall, resultReg, outlinedCallLabel, doneLabel, cg);
   cg->getPPCOutOfLineCodeSectionList().push_front(outlinedHelperCall);

   node->setRegister(resultReg);
   cg->decReferenceCount(node->getFirstChild());
   cg->decReferenceCount(node->getSecondChild());
   srm->stopUsingRegisters();
   cg->stopUsingRegister(condReg);

   return resultReg;
   }

static TR::Register *inlineStringHashcode(TR::Node *node, TR::CodeGenerator *cg)
{
    TR::Compilation *comp = cg->comp();
    TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());
    bool isLE = comp->target().cpu.isLittleEndian();

    TR::Node *valueNode = node->getFirstChild();
    TR::Node *offsetNode = node->getSecondChild();
    TR::Node *countNode = node->getThirdChild();

    TR::Register *valueReg = cg->gprClobberEvaluate(valueNode);
    TR::Register *endReg = cg->gprClobberEvaluate(offsetNode);
    TR::Register *vendReg = cg->gprClobberEvaluate(countNode);
    TR::Register *hashReg = cg->allocateRegister();
    TR::Register *tempReg = cg->allocateRegister();
    TR::Register *constant0Reg = cg->allocateRegister();
    TR::Register *multiplierAddrReg = cg->allocateRegister();
    TR::Register *condReg = cg->allocateRegister(TR_CCR);

    TR::Register *multiplierReg = cg->allocateRegister(TR_VRF);
    TR::Register *high4Reg = cg->allocateRegister(TR_VRF);
    TR::Register *low4Reg = cg->allocateRegister(TR_VRF);
    TR::Register *vtmp1Reg = cg->allocateRegister(TR_VRF);
    TR::Register *vtmp2Reg = cg->allocateRegister(TR_VRF);
    TR::Register *vconstant0Reg = cg->allocateRegister(TR_VRF);
    TR::Register *vconstantNegReg = cg->allocateRegister(TR_VRF);
    TR::Register *vunpackMaskReg = cg->allocateRegister(TR_VRF);

    TR::LabelSymbol *serialLabel = generateLabelSymbol(cg);
    TR::LabelSymbol *VSXLabel = generateLabelSymbol(cg);
    TR::LabelSymbol *POSTVSXLabel = generateLabelSymbol(cg);
    TR::LabelSymbol *endLabel = generateLabelSymbol(cg);

    // Skip header of the array
    // v = v + offset<<1
    // end = v + count<<1
    // hash = 0
    // temp = 0
    intptr_t hdrSize = TR::Compiler->om.contiguousArrayHeaderSizeInBytes();
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valueReg, valueReg, hdrSize);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, endReg, endReg, endReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, valueReg, valueReg, endReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, vendReg, vendReg, vendReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, endReg, valueReg, vendReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::XOR, node, hashReg, hashReg, hashReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::XOR, node, tempReg, tempReg, tempReg);
    loadConstant(cg, node, 0x0, constant0Reg);

    // if count<<1 < 16 goto serial
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, vendReg, 0x10);
    generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, serialLabel, condReg);

    // load multiplier, clear high4 low4
    static uint32_t multiplierVectors_be[12] = {0x94446F01, 0x94446F01, 0x94446F01, 0x94446F01, 0x67E12CDF, 887503681, 28629151, 923521, 29791, 961, 31, 1};
    static uint32_t multiplierVectors_le[12] = {0x94446F01, 0x94446F01, 0x94446F01, 0x94446F01, 1, 31, 961, 29791, 923521, 28629151, 887503681, 0x67E12CDF};

    if (isLE){
        loadAddressConstant(cg, false, node, (intptr_t)multiplierVectors_le, multiplierAddrReg);
    }else{
        loadAddressConstant(cg, false, node, (intptr_t)multiplierVectors_be, multiplierAddrReg);
    }
    generateTrg1MemInstruction(cg, TR::InstOpCode::lxvw4x, node, multiplierReg, TR::MemoryReference::createWithIndexReg(cg, multiplierAddrReg, constant0Reg, 16));
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vxor, node, high4Reg, high4Reg, high4Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vxor, node, low4Reg, low4Reg, low4Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vxor, node, vconstant0Reg, vconstant0Reg, vconstant0Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vnor, node, vconstantNegReg, vconstant0Reg, vconstant0Reg);
    generateTrg1Src2ImmInstruction(cg, TR::InstOpCode::vsldoi, node, vunpackMaskReg, vconstant0Reg, vconstantNegReg, 2);
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::vspltw, node, vunpackMaskReg, vunpackMaskReg, 3);

    // vend = end & (~0xf)
    // if v is 16byte aligned goto VSX_LOOP
    generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, 0xFFFFFFFFFFFFFFF0);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, vendReg, endReg, tempReg);
    loadConstant(cg, node, 0xF, tempReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, tempReg, valueReg, tempReg);
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::cmpi4, node, condReg, tempReg, 0x0);
    generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, VSXLabel, condReg);

    // The reason we don't do VSX loop directly is we want to avoid loading unaligned data and deal it with vperm.
    // Instead, we load the first unaligned part, let VSX handle the rest aligned part.
    // load unaligned v, mask out unwanted part
    // for example, if value = 0x12345, we mask out 0x12340~0x12344, keep 0x12345~0x1234F
    generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, vtmp1Reg, TR::MemoryReference::createWithIndexReg(cg, valueReg, constant0Reg, 16));
    loadConstant(cg, node, 0xF, tempReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, tempReg, valueReg, tempReg);
    generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, tempReg, tempReg, 3, 0xFFFFFFFFFFFFFFF8);
    generateTrg1Src1Instruction(cg, TR::InstOpCode::mtvsrd, node, vtmp2Reg, tempReg);
    generateTrg1Src2ImmInstruction(cg, TR::InstOpCode::vsldoi, node, vtmp2Reg, vconstant0Reg, vtmp2Reg, 8);
    if (isLE) {
        generateTrg1Src2Instruction(cg, TR::InstOpCode::vslo, node, vtmp2Reg, vconstantNegReg, vtmp2Reg);
    } else {
        generateTrg1Src2Instruction(cg, TR::InstOpCode::vsro, node, vtmp2Reg, vconstantNegReg, vtmp2Reg);
    }
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vand, node, vtmp1Reg, vtmp1Reg, vtmp2Reg);

    // unpack masked v to high4 low4
    generateTrg1Src1Instruction(cg, TR::InstOpCode::vupkhsh, node, high4Reg, vtmp1Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vand, node, high4Reg, high4Reg, vunpackMaskReg);
    generateTrg1Src1Instruction(cg, TR::InstOpCode::vupklsh, node, low4Reg, vtmp1Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vand, node, low4Reg, low4Reg, vunpackMaskReg);

    // advance v to next aligned pointer
    // if v >= vend goto POST_VSX
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valueReg, valueReg, 0xF);
    generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tempReg, 0xFFFFFFFFFFFFFFF0);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::AND, node, valueReg, valueReg, tempReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, condReg, valueReg, vendReg);
    generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, POSTVSXLabel, condReg);

    //VSX_LOOP:
    //load v (here v must be aligned)
    //unpack v to temphigh4 templow4
    //high4 = high4 * multiplier
    //low4 = low4 * multiplier
    //high4 = high4 + temphigh4
    //low4 = low4 + templow4
    //v = v + 0x10
    //if v < vend goto VSX_LOOP
    generateLabelInstruction(cg, TR::InstOpCode::label, node, VSXLabel);
    generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, vtmp1Reg, TR::MemoryReference::createWithIndexReg(cg, valueReg, constant0Reg, 16));
    generateTrg1Src1Instruction(cg, TR::InstOpCode::vupkhsh, node, vtmp2Reg, vtmp1Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vand, node, vtmp2Reg, vtmp2Reg, vunpackMaskReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vmuluwm, node, high4Reg, high4Reg, multiplierReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vadduwm, node, high4Reg, high4Reg, vtmp2Reg);
    generateTrg1Src1Instruction(cg, TR::InstOpCode::vupklsh, node, vtmp2Reg, vtmp1Reg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vand, node, vtmp2Reg, vtmp2Reg, vunpackMaskReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vmuluwm, node, low4Reg, low4Reg, multiplierReg);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vadduwm, node, low4Reg, low4Reg, vtmp2Reg);
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valueReg, valueReg, 0x10);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, condReg, valueReg, vendReg);
    generateConditionalBranchInstruction(cg, TR::InstOpCode::blt, node, VSXLabel, condReg);

    // POST_VSX:
    // shift and sum low4 and high4
    // move result to hashReg
    generateLabelInstruction(cg, TR::InstOpCode::label, node, POSTVSXLabel);

    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, multiplierAddrReg, multiplierAddrReg, 0x10);
    generateTrg1MemInstruction(cg, TR::InstOpCode::lxvw4x, node, multiplierReg, TR::MemoryReference::createWithIndexReg(cg, multiplierAddrReg, constant0Reg, 16));
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vmuluwm, node, high4Reg, high4Reg, multiplierReg);
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, multiplierAddrReg, multiplierAddrReg, 0x10);
    generateTrg1MemInstruction(cg, TR::InstOpCode::lxvw4x, node, multiplierReg, TR::MemoryReference::createWithIndexReg(cg, multiplierAddrReg, constant0Reg, 16));
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vmuluwm, node, low4Reg, low4Reg, multiplierReg);

    generateTrg1Src2Instruction(cg, TR::InstOpCode::vadduwm, node, high4Reg, high4Reg, low4Reg);
    generateTrg1Src2ImmInstruction(cg, TR::InstOpCode::vsldoi, node, vtmp1Reg, vconstant0Reg, high4Reg, 8);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vadduwm, node, high4Reg, high4Reg, vtmp1Reg);
    generateTrg1Src2ImmInstruction(cg, TR::InstOpCode::vsldoi, node, vtmp1Reg, vconstant0Reg, high4Reg, 12);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::vadduwm, node, high4Reg, high4Reg, vtmp1Reg);

    generateTrg1Src2ImmInstruction(cg, TR::InstOpCode::vsldoi, node, vtmp1Reg, high4Reg, vconstant0Reg, 8);
    generateTrg1Src1Instruction(cg, TR::InstOpCode::mfvsrwz, node, hashReg, vtmp1Reg);

    // Head of the serial loop
    generateLabelInstruction(cg, TR::InstOpCode::label, node, serialLabel);

    // temp = hash
    // hash = hash << 5
    // hash = hash - temp
    // temp = v[i]
    // hash = hash + temp

    generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, condReg, valueReg, endReg);
    generateConditionalBranchInstruction(cg, TR::InstOpCode::bge, node, endLabel, condReg);
    generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, tempReg, hashReg);
    generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, hashReg, hashReg, 5, 0xFFFFFFFFFFFFFFE0);
    generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, hashReg, tempReg, hashReg);
    generateTrg1MemInstruction(cg, TR::InstOpCode::lhzx, node, tempReg, TR::MemoryReference::createWithIndexReg(cg, valueReg, constant0Reg, 2));
    generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, hashReg, hashReg, tempReg);
    generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, valueReg, valueReg, 0x2);
    generateLabelInstruction(cg, TR::InstOpCode::b, node, serialLabel);

    // End of this method
    TR::RegisterDependencyConditions *dependencies = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 16, cg->trMemory());
    dependencies->addPostCondition(valueReg, TR::RealRegister::NoReg);
    dependencies->getPostConditions()->getRegisterDependency(0)->setExcludeGPR0(); // valueReg

    dependencies->addPostCondition(endReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vendReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(hashReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(tempReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(constant0Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(condReg, TR::RealRegister::NoReg);

    dependencies->addPostCondition(multiplierAddrReg, TR::RealRegister::NoReg);
    dependencies->getPostConditions()->getRegisterDependency(7)->setExcludeGPR0(); // multiplierAddrReg

    dependencies->addPostCondition(multiplierReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(high4Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(low4Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vtmp1Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vtmp2Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vconstant0Reg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vconstantNegReg, TR::RealRegister::NoReg);
    dependencies->addPostCondition(vunpackMaskReg, TR::RealRegister::NoReg);

    generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, dependencies);

    node->setRegister(hashReg);
    cg->decReferenceCount(valueNode);
    cg->decReferenceCount(offsetNode);
    cg->decReferenceCount(countNode);

    cg->stopUsingRegister(valueReg);
    cg->stopUsingRegister(endReg);
    cg->stopUsingRegister(vendReg);
    cg->stopUsingRegister(tempReg);
    cg->stopUsingRegister(constant0Reg);
    cg->stopUsingRegister(condReg);
    cg->stopUsingRegister(multiplierAddrReg);

    cg->stopUsingRegister(multiplierReg);
    cg->stopUsingRegister(high4Reg);
    cg->stopUsingRegister(low4Reg);
    cg->stopUsingRegister(vtmp1Reg);
    cg->stopUsingRegister(vtmp2Reg);
    cg->stopUsingRegister(vconstant0Reg);
    cg->stopUsingRegister(vconstantNegReg);
    cg->stopUsingRegister(vunpackMaskReg);
    return hashReg;
}

static TR::Register *inlineEncodeUTF16(TR::Node *node, TR::CodeGenerator *cg)
   {
   // tree looks like:
   // icall com.ibm.jit.JITHelpers.encodeUtf16{Big,Little}()
   // input ptr
   // output ptr
   // input length (in elements)
   // Number of elements converted returned

   TR::MethodSymbol *symbol = node->getSymbol()->castToMethodSymbol();
   bool bigEndian = symbol->getRecognizedMethod() == TR::com_ibm_jit_JITHelpers_transformedEncodeUTF16Big;

   // Set up register dependencies
   const int gprClobberCount = 5;
   const int fprClobberCount = 4;
   const int vrClobberCount = 6;
   const int crClobberCount = 2;
   const int totalDeps = crClobberCount + gprClobberCount + fprClobberCount + vrClobberCount + 3;
   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(1, totalDeps, cg->trMemory());

   TR::Register *inputReg = cg->gprClobberEvaluate(node->getChild(0));
   TR::Register *outputReg = cg->gprClobberEvaluate(node->getChild(1));
   TR::Register *inputLenReg = cg->gprClobberEvaluate(node->getChild(2));
   TR::Register *outputLenReg = cg->allocateRegister();

   // Allocate clobbered registers
   TR::Register *gprClobbers[gprClobberCount], *fprClobbers[fprClobberCount], *vrClobbers[vrClobberCount], *crClobbers[crClobberCount];
   for (int i = 0; i < gprClobberCount; ++i) gprClobbers[i] = cg->allocateRegister(TR_GPR);
   for (int i = 0; i < fprClobberCount; ++i) fprClobbers[i] = cg->allocateRegister(TR_FPR);
   for (int i = 0; i < vrClobberCount; ++i) vrClobbers[i] = cg->allocateRegister(TR_VRF);
   for (int i = 0; i < crClobberCount; ++i) crClobbers[i] = cg->allocateRegister(TR_CCR);

   // Add the pre and post conditions
   // Input and output registers
   deps->addPreCondition(inputReg, TR::RealRegister::gr3);

   deps->addPostCondition(outputLenReg, TR::RealRegister::gr3);
   deps->addPostCondition(outputReg, TR::RealRegister::gr4);
   deps->addPostCondition(inputLenReg, TR::RealRegister::gr5);

   //CCR.
   deps->addPostCondition(crClobbers[0], TR::RealRegister::cr0);
   deps->addPostCondition(crClobbers[1], TR::RealRegister::cr6);

   //GPRs + Trampoline
   deps->addPostCondition(gprClobbers[0], TR::RealRegister::gr6);
   deps->addPostCondition(gprClobbers[1], TR::RealRegister::gr7);
   deps->addPostCondition(gprClobbers[2], TR::RealRegister::gr8);
   deps->addPostCondition(gprClobbers[3], TR::RealRegister::gr9);
   deps->addPostCondition(gprClobbers[4], TR::RealRegister::gr11);

   //VR's
   deps->addPostCondition(vrClobbers[0], TR::RealRegister::vr0);
   deps->addPostCondition(vrClobbers[1], TR::RealRegister::vr1);
   deps->addPostCondition(vrClobbers[2], TR::RealRegister::vr2);
   deps->addPostCondition(vrClobbers[3], TR::RealRegister::vr3);
   deps->addPostCondition(vrClobbers[4], TR::RealRegister::vr4);
   deps->addPostCondition(vrClobbers[5], TR::RealRegister::vr5);

   //FP/VSR
   deps->addPostCondition(fprClobbers[0], TR::RealRegister::fp0);
   deps->addPostCondition(fprClobbers[1], TR::RealRegister::fp1);
   deps->addPostCondition(fprClobbers[2], TR::RealRegister::fp2);
   deps->addPostCondition(fprClobbers[3], TR::RealRegister::fp3);

   // Generate helper call
   TR_RuntimeHelper helper;
   helper = bigEndian ? TR_PPCencodeUTF16Big : TR_PPCencodeUTF16Little;
   TR::SymbolReference *helperSym = cg->comp()->getSymRefTab()->findOrCreateRuntimeHelper(helper);
   generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), deps, helperSym);

   for (uint32_t i = 0; i < node->getNumChildren(); ++i) cg->decReferenceCount(node->getChild(i));

   // Spill the clobbered registers
   if (inputReg != node->getChild(0)->getRegister()) cg->stopUsingRegister(inputReg);
   if (outputReg != node->getChild(1)->getRegister()) cg->stopUsingRegister(outputReg);
   if (inputLenReg != node->getChild(2)->getRegister()) cg->stopUsingRegister(inputLenReg);
   for (int i = 0; i < gprClobberCount; ++i) cg->stopUsingRegister(gprClobbers[i]);
   for (int i = 0; i < vrClobberCount; ++i) cg->stopUsingRegister(vrClobbers[i]);
   for (int i = 0; i < fprClobberCount; ++i) cg->stopUsingRegister(fprClobbers[i]);
   for (int i = 0; i < crClobberCount; ++i) cg->stopUsingRegister(crClobbers[i]);

   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();
   node->setRegister(outputLenReg);

   return outputLenReg;
   }

static TR::Register *inlineIntrinsicIndexOf(TR::Node *node, TR::CodeGenerator *cg, bool isLatin1)
   {
   TR::Compilation *comp = cg->comp();
   auto vectorCompareOp = isLatin1 ? TR::InstOpCode::vcmpequb_r : TR::InstOpCode::vcmpequh_r;
   auto scalarLoadOp = isLatin1 ? TR::InstOpCode::lbzx : TR::InstOpCode::lhzx;

   TR::Register *array = cg->evaluate(node->getChild(1));
   TR::Register *ch = cg->evaluate(node->getChild(2));
   TR::Register *offset = cg->evaluate(node->getChild(3));
   TR::Register *length = cg->evaluate(node->getChild(4));

   TR::Register *cr0 = cg->allocateRegister(TR_CCR);
   TR::Register *cr6 = cg->allocateRegister(TR_CCR);

   TR::Register *zeroRegister = cg->allocateRegister();
   TR::Register *result = cg->allocateRegister();
   TR::Register *arrAddress = cg->allocateRegister();
   TR::Register *currentAddress = cg->allocateRegister();
   TR::Register *endAddress = cg->allocateRegister();

   TR::Register *targetVector = cg->allocateRegister(TR_VRF);
   TR::Register *targetVectorNot = cg->allocateRegister(TR_VRF);
   TR::Register *searchVector = cg->allocateRegister(TR_VRF);
   TR::Register *permuteVector = cg->allocateRegister(TR_VRF);

   TR_PPCScratchRegisterManager *srm = cg->generateScratchRegisterManager();

   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *notSmallLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *vectorLoopLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *residueLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *notFoundLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *foundLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *foundExactLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *endLabel = generateLabelSymbol(cg);

   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

   // Special case for empty strings, which always return -1
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, cr0, offset, length);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, notFoundLabel, cr0);

   // IMPORTANT: The upper 32 bits of a 64-bit register containing an int are undefined. Since the
   // indices are being passed in as ints, we must ensure that their upper 32 bits are not garbage.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::extsw, node, result, offset);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::extsw, node, endAddress, length);

   if (!isLatin1)
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, result, result, result);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, endAddress, endAddress, endAddress);
      }

   if (node->getChild(3)->getReferenceCount() == 1)
      srm->donateScratchRegister(offset);
   if (node->getChild(4)->getReferenceCount() == 1)
      srm->donateScratchRegister(length);

   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, arrAddress, array, TR::Compiler->om.contiguousArrayHeaderSizeInBytes());
   if (node->getChild(1)->getReferenceCount() == 1)
      srm->donateScratchRegister(array);

   // Handle the first character using a simple scalar compare. Otherwise, first character matches
   // would be slower than the old scalar comparison loop. This is a problem since first character
   // matches are common in certain contexts, e.g. StringTokenizer where the default first character
   // to each String.indexOf call is ' ', which is the most common whitespace character.
      {
      TR::Register *value = srm->findOrCreateScratchRegister();
      TR::Register *zxTargetScalar = srm->findOrCreateScratchRegister();

      // Since we're going to do a load followed immediately by a comparison, we need to ensure that
      // the target scalar is zero-extended and *not* sign-extended.
      generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rlwinm, node, zxTargetScalar, ch, 0, isLatin1 ? 0xff : 0xffff);

      generateTrg1MemInstruction(cg, scalarLoadOp, node, value, TR::MemoryReference::createWithIndexReg(cg, result, arrAddress, isLatin1 ? 1 : 2));
      generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp4, node, cr0, value, zxTargetScalar);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, foundExactLabel, cr0);

      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, result, result, isLatin1 ? 1 : 2);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, result, endAddress);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, notFoundLabel, cr0);

      srm->reclaimScratchRegister(zxTargetScalar);
      srm->reclaimScratchRegister(value);
      }

   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, zeroRegister, 0);

   // Calculate the actual addresses of the start and end points
   generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, endAddress, arrAddress, endAddress);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, currentAddress, arrAddress, result);

   // Splat the value to be compared against and its bitwise complement into two vector registers
   // for later use
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mtvsrwz, node, targetVector, ch);
   if (node->getChild(2)->getReferenceCount() == 1)
      srm->donateScratchRegister(ch);
   if (isLatin1)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::vspltb, node, targetVector, targetVector, 7);
   else
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::vsplth, node, targetVector, targetVector, 3);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::vnor, node, targetVectorNot, targetVector, targetVector);

   TR::Register *endVectorAddress = srm->findOrCreateScratchRegister();
   TR::Register *startVectorAddress = srm->findOrCreateScratchRegister();

   // Calculate the end address for what can be compared using full vector compares. After reaching
   // this address, the remaining comparisons (if required) will need special handling.
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, endVectorAddress, endAddress, 0, CONSTANT64(0xfffffffffffffff0));

   // Check if the entire string is contained within a single 16-byte aligned section. If this
   // happens, we need to handle that case specially.
   generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, startVectorAddress, currentAddress, 0, CONSTANT64(0xfffffffffffffff0));
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, startVectorAddress, endVectorAddress);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, notSmallLabel, cr0);

   // If we got here, then the entire string to search is contained within a single 16-byte aligned
   // vector and the end of the string is not aligned to the end of the vector. We don't know
   // whether the start of the string is aligned, but we'll assume it isn't since that just results
   // in a few unnecessary operations producing the correct answer regardless.

   // First, we read in an entire 16-byte aligned vector containing the entire string. Since this
   // load is done with 16-byte natural alignment, this load can't cross a page boundary and cause
   // an unexpected page fault. However, this will read some garbage at the start and end of the
   // vector. Assume that we read n bytes of garbage before the string and m bytes of garbage after
   // the string.
   generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, searchVector, TR::MemoryReference::createWithIndexReg(cg, zeroRegister, currentAddress, 16));

      {
      TR::Register *scratchRegister = srm->findOrCreateScratchRegister();

      // We need to ensure that the garbage we read can't compare as equal to the target value for
      // obvious reasons. In order to accomplish this, we first rotate forwards by m bytes. This
      // places all garbage at the beginning of the vector.
      generateTrg1Src1Instruction(cg, TR::InstOpCode::neg, node, scratchRegister, endAddress);
      generateTrg1Src2Instruction(cg, comp->target().cpu.isLittleEndian() ? TR::InstOpCode::lvsl : TR::InstOpCode::lvsr, node, permuteVector, zeroRegister, scratchRegister);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, searchVector, permuteVector);

      // Next, we shift the vector backwards by (n + m) bytes shifting in the bitwise complement of
      // the target value. This causes the garbage to end up at the vector register, having been
      // replaced with a bit pattern that can never compare as equal to the target value.
      generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, scratchRegister, scratchRegister, currentAddress);
      if (comp->target().cpu.isLittleEndian())
         {
         generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsr, node, permuteVector, zeroRegister, scratchRegister);
         generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, targetVectorNot, searchVector, permuteVector);
         }
      else
         {
         generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsl, node, permuteVector, zeroRegister, scratchRegister);
         generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, targetVectorNot, permuteVector);
         }

      srm->reclaimScratchRegister(scratchRegister);

      // Now the search vector is ready for comparison: none of the garbage can compare as equal to
      // our target value and the start of the vector is now aligned to the start of the string. So
      // we can now perform the comparison as normal.
      generateTrg1Src2Instruction(cg, vectorCompareOp, node, searchVector, searchVector, targetVector);
      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, foundLabel, cr6);
      generateLabelInstruction(cg, TR::InstOpCode::b, node, notFoundLabel);
      }

   generateLabelInstruction(cg, TR::InstOpCode::label, node, notSmallLabel);

   // Check if we already have 16-byte alignment. Vector loads require 16-byte alignment, so if we
   // aren't properly aligned, we'll need to handle comparisons specially until we achieve 16-byte
   // alignment.
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, currentAddress, startVectorAddress);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, vectorLoopLabel, cr0);

   // We are not on a 16-byte boundary, so we cannot directly load the first 16 bytes of the string
   // for comparison. Instead, we load the 16 byte vector starting from the 16-byte aligned section
   // containing the start of the string. Since we have 16-byte natural alignment, this can't cause
   // an unexpected page fault.
   generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, searchVector, TR::MemoryReference::createWithIndexReg(cg, zeroRegister, currentAddress, 16));

   // However, before we can run any comparisons on the loaded vector, we must ensure that the extra
   // garbage read before the start of the string can't match the target character. To do this, we
   // shift the loaded vector backwards by n bytes shifting in the bitwise complement of the target
   // character.
   if (comp->target().cpu.isLittleEndian())
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsr, node, permuteVector, zeroRegister, currentAddress);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, targetVectorNot, searchVector, permuteVector);
      }
   else
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsl, node, permuteVector, zeroRegister, currentAddress);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, targetVectorNot, permuteVector);
      }

   // Now our vector is ready for comparison: no garbage can match the target value and the start of
   // the vector is now aligned to the start of the string.
   generateTrg1Src2Instruction(cg, vectorCompareOp, node, searchVector, searchVector, targetVector);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, foundLabel, cr6);

   // If the first vector didn't match, then we can slide right into the standard vectorized loop.
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, currentAddress, startVectorAddress, 0x10);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, currentAddress, endVectorAddress);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, residueLabel, cr0);

   srm->reclaimScratchRegister(startVectorAddress);

   // This is the heart of the vectorized loop, working just like any standard vectorized loop.
   generateLabelInstruction(cg, TR::InstOpCode::label, node, vectorLoopLabel);
   generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, searchVector, TR::MemoryReference::createWithIndexReg(cg, zeroRegister, currentAddress, 16));
   generateTrg1Src2Instruction(cg, vectorCompareOp, node, searchVector, searchVector, targetVector);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, foundLabel, cr6);

   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, currentAddress, currentAddress, 0x10);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, currentAddress, endVectorAddress);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, vectorLoopLabel, cr0);

   srm->reclaimScratchRegister(endVectorAddress);

   // Now we're done with the part of the loop which can be handled as a normal vectorized loop. If
   // there are no more elements to compare, we're done. Otherwise, we need to handle the residue.
   generateLabelInstruction(cg, TR::InstOpCode::label, node, residueLabel);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::cmp8, node, cr0, currentAddress, endAddress);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, notFoundLabel, cr0);

   // Usually, we would need a residue loop here, but it's safe to read beyond the end of the string
   // here. Since our load will have 16-byte natural alignment, it can't cross a page boundary and
   // cause an unexpected page fault.
   generateTrg1MemInstruction(cg, TR::InstOpCode::lvx, node, searchVector, TR::MemoryReference::createWithIndexReg(cg, zeroRegister, currentAddress, 16));

   TR::Register *shiftAmount = srm->findOrCreateScratchRegister();

   // Before we can run our comparison, we need to ensure that the garbage from beyond the end of
   // the string cannot compare as equal to our target value. To do this, we first rotate the vector
   // forwards by n bytes then shift back by n bytes, shifting in the bitwise complement of the
   // target value.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::neg, node, shiftAmount, endAddress);
   if (comp->target().cpu.isLittleEndian())
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsl, node, permuteVector, zeroRegister, shiftAmount);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, searchVector, permuteVector);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsr, node, permuteVector, zeroRegister, shiftAmount);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, targetVectorNot, searchVector, permuteVector);
      }
   else
      {
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsr, node, permuteVector, zeroRegister, shiftAmount);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, searchVector, permuteVector);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsl, node, permuteVector, zeroRegister, shiftAmount);
      generateTrg1Src3Instruction(cg, TR::InstOpCode::vperm, node, searchVector, searchVector, targetVectorNot, permuteVector);
      }

   srm->reclaimScratchRegister(shiftAmount);

   // Now we run our comparison as normal
   generateTrg1Src2Instruction(cg, vectorCompareOp, node, searchVector, searchVector, targetVector);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, foundLabel, cr6);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, notFoundLabel);

   // We've looked through the entire string and didn't find our target character, so return the
   // sentinel value -1
   generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, result, -1);
   generateLabelInstruction(cg, TR::InstOpCode::b, node, endLabel);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, foundLabel);

   // We've managed to find a match for the target value in the loaded vector, but we don't yet know
   // which element of the loaded vector is the first match. The comparison will have set matching
   // elements in the vector to -1 and non-matching elements to 0. We can find the first matching
   // element by gathering the first bit of every byte in the vector register...

   // Set permuteVector = 0x000102030405060708090a0b0c0d0e0f
   generateTrg1Src2Instruction(cg, TR::InstOpCode::lvsl, node, permuteVector, zeroRegister, zeroRegister);

   // For little-endian, reverse permuteVector so that we can find the first set bit using a count
   // leading zeroes test instead of a count trailing zeroes test. This is necessary since cnttzw
   // wasn't introduced until Power 9.
   if (comp->target().cpu.isLittleEndian())
      {
      generateTrg1ImmInstruction(cg, TR::InstOpCode::vspltisb, node, targetVector, 0x0f);
      generateTrg1Src2Instruction(cg, TR::InstOpCode::vsububm, node, permuteVector, targetVector, permuteVector);
      }

   // Set permuteVector = 0x00081018202830384048505860687078 (reversed for LE)
   generateTrg1ImmInstruction(cg, TR::InstOpCode::vspltisb, node, targetVector, 3);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::vslb, node, permuteVector, permuteVector, targetVector);

   generateTrg1Src2Instruction(cg, TR::InstOpCode::vbpermq, node, searchVector, searchVector, permuteVector);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mfvsrwz, node, result, searchVector);

   // Then count the number of leading zeroes from the obtained result. This tells us the index (in
   // bytes) of the first matching element in the vector. Note that there is no way to count leading
   // zeroes of a half-word, so we count leading zeroes of a word and subtract 16 since the value
   // we're interested in is in the least significant half-word.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::cntlzw, node, result, result);
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, result, result, -16);

   // Finally, combine this with the address of the last vector load to find the address of the
   // first matching element in the string. Finally, use this to calculate the corresponding index.
   generateTrg1Src2Instruction(cg, TR::InstOpCode::add, node, result, result, currentAddress);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::subf, node, result, arrAddress, result);

   generateLabelInstruction(cg, TR::InstOpCode::label, node, foundExactLabel);
   if (!isLatin1)
      generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::srawi, node, result, result, 1);

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 15 + srm->numAvailableRegisters(), cg->trMemory());

   if (node->getChild(1)->getReferenceCount() != 1)
      {
      deps->addPostCondition(array, TR::RealRegister::NoReg);
      deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
      }
   if (node->getChild(2)->getReferenceCount() != 1)
      deps->addPostCondition(ch, TR::RealRegister::NoReg);
   if (node->getChild(3)->getReferenceCount() != 1)
      deps->addPostCondition(offset, TR::RealRegister::NoReg);
   if (node->getChild(4)->getReferenceCount() != 1)
      deps->addPostCondition(length, TR::RealRegister::NoReg);

   deps->addPostCondition(cr0, TR::RealRegister::cr0);
   deps->addPostCondition(cr6, TR::RealRegister::cr6);

   deps->addPostCondition(zeroRegister, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
   deps->addPostCondition(result, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
   deps->addPostCondition(arrAddress, TR::RealRegister::NoReg);
   deps->addPostCondition(currentAddress, TR::RealRegister::NoReg);
   deps->getPostConditions()->getRegisterDependency(deps->getAddCursorForPost() - 1)->setExcludeGPR0();
   deps->addPostCondition(endAddress, TR::RealRegister::NoReg);

   deps->addPostCondition(targetVector, TR::RealRegister::NoReg);
   deps->addPostCondition(targetVectorNot, TR::RealRegister::NoReg);
   deps->addPostCondition(searchVector, TR::RealRegister::NoReg);
   deps->addPostCondition(permuteVector, TR::RealRegister::NoReg);

   srm->addScratchRegistersToDependencyList(deps, true);

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, deps);

   deps->stopUsingDepRegs(cg, result);

   node->setRegister(result);

   cg->decReferenceCount(node->getChild(0));
   cg->decReferenceCount(node->getChild(1));
   cg->decReferenceCount(node->getChild(2));
   cg->decReferenceCount(node->getChild(3));
   cg->decReferenceCount(node->getChild(4));

   return result;
   }

/*
 * Arraycopy evaluator needs a version of inlineArrayCopy that can be used inside internal control flow. For this version of inlineArrayCopy, registers must
 * be allocated outside of this function so the dependency at the end of the control flow knows about them.
 */
static void inlineArrayCopy_ICF(TR::Node *node, int64_t byteLen, TR::Register *src, TR::Register *dst, TR::CodeGenerator *cg, TR::Register *cndReg,
                                TR::Register *tmp1Reg, TR::Register *tmp2Reg, TR::Register *tmp3Reg, TR::Register *tmp4Reg,
                                TR::Register *fp1Reg, TR::Register *fp2Reg, TR::Register *fp3Reg, TR::Register *fp4Reg)
   {
   if (byteLen == 0)
      return;

   bool postP10CopyInline = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P10) &&
                            cg->comp()->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX);

   if (postP10CopyInline)
      {
      // tmp1Reg and fp1Reg can be used: fp1Reg is a VSX_VECTOR

      int32_t iteration64 = byteLen >> 6, residue64 = byteLen & 0x3F, standingOffset = 0;

      if (iteration64 > 0)
         {
         TR::LabelSymbol *loopStart;

         if (iteration64 > 1)
            {
            generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tmp1Reg, iteration64);
            generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, tmp1Reg);

            loopStart = generateLabelSymbol(cg);
            generateLabelInstruction(cg, TR::InstOpCode::label, node, loopStart);
            }

         generateTrg1MemInstruction(cg, TR::InstOpCode::lxv, node, fp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, 0, 16));
         generateMemSrc1Instruction(cg, TR::InstOpCode::stxv, node, TR::MemoryReference::createWithDisplacement(cg, dst, 0, 16), fp1Reg);
         generateTrg1MemInstruction(cg, TR::InstOpCode::lxv, node, fp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, 16, 16));
         generateMemSrc1Instruction(cg, TR::InstOpCode::stxv, node, TR::MemoryReference::createWithDisplacement(cg, dst, 16, 16), fp1Reg);
         generateTrg1MemInstruction(cg, TR::InstOpCode::lxv, node, fp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, 32, 16));
         generateMemSrc1Instruction(cg, TR::InstOpCode::stxv, node, TR::MemoryReference::createWithDisplacement(cg, dst, 32, 16), fp1Reg);
         generateTrg1MemInstruction(cg, TR::InstOpCode::lxv, node, fp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, 48, 16));
         generateMemSrc1Instruction(cg, TR::InstOpCode::stxv, node, TR::MemoryReference::createWithDisplacement(cg, dst, 48, 16), fp1Reg);

         if (iteration64 > 1)
            {
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, src, src, 64);
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, dst, dst, 64);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bdnz, node, loopStart, cndReg);
            }
         else
            standingOffset = 64;
         }

      for (int32_t i = 0; i < (residue64>>4); i++)
         {
         generateTrg1MemInstruction(cg, TR::InstOpCode::lxv, node, fp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, standingOffset+i*16, 16));
         generateMemSrc1Instruction(cg, TR::InstOpCode::stxv, node, TR::MemoryReference::createWithDisplacement(cg, dst, standingOffset+i*16, 16), fp1Reg);
         }

      if ((residue64 & 0xF) != 0)
         {
         standingOffset += residue64 & 0x30;
         switch (residue64 & 0xF)
            {
            case 1:
               generateTrg1MemInstruction(cg, TR::InstOpCode::lbz, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, standingOffset, 1));
               generateMemSrc1Instruction(cg, TR::InstOpCode::stb, node, TR::MemoryReference::createWithDisplacement(cg, dst, standingOffset, 1), tmp1Reg);
               break;
            case 2:
               generateTrg1MemInstruction(cg, TR::InstOpCode::lhz, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, standingOffset, 2));
               generateMemSrc1Instruction(cg, TR::InstOpCode::sth, node, TR::MemoryReference::createWithDisplacement(cg, dst, standingOffset, 2), tmp1Reg);
               break;
            case 4:
               generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, standingOffset, 4));
               generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, dst, standingOffset, 4), tmp1Reg);
               break;
            case 8:
               generateTrg1MemInstruction(cg, TR::InstOpCode::ld, node, tmp1Reg, TR::MemoryReference::createWithDisplacement(cg, src, standingOffset, 8));
               generateMemSrc1Instruction(cg, TR::InstOpCode::std, node, TR::MemoryReference::createWithDisplacement(cg, dst, standingOffset, 8), tmp1Reg);
               break;
            default:
               generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, tmp1Reg, residue64 & 0xF);
               generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, tmp1Reg, tmp1Reg,
                                               56, CONSTANT64(0xff00000000000000));
               if (standingOffset != 0)
                  {
                  generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, src, src, standingOffset);
                  generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, dst, dst, standingOffset);
                  }
               generateTrg1Src2Instruction(cg, TR::InstOpCode::lxvl, node, fp1Reg, src, tmp1Reg);
               generateSrc3Instruction(cg, TR::InstOpCode::stxvl, node, fp1Reg, dst, tmp1Reg);
               break;
            }
         }
      }
   else
      {
      TR::Register *regs[4] = {tmp1Reg, tmp2Reg, tmp3Reg, tmp4Reg};
      TR::Register *fpRegs[4] = {fp1Reg, fp2Reg, fp3Reg, fp4Reg};
      int32_t groups, residual;

      static bool disableLEArrayCopyInline = (feGetEnv("TR_disableLEArrayCopyInline") != NULL);
      bool supportsLEArrayCopyInline = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) && !disableLEArrayCopyInline && cg->comp()->target().cpu.isLittleEndian() && cg->comp()->target().cpu.hasFPU() && cg->comp()->target().is64Bit();

      if (cg->comp()->target().is64Bit())
         {
         groups = byteLen >> 5;
         residual = byteLen & 0x0000001F;
         }
      else
         {
         groups = byteLen >> 4;
         residual = byteLen & 0x0000000F;
         }

      int32_t regIx = 0, ix = 0, fpRegIx = 0;
      int32_t memRefSize;
      TR::InstOpCode::Mnemonic load, store;
      TR::Compilation* comp = cg->comp();

      /* Some machines have issues with 64bit loads/stores in 32bit mode, ie. sicily, do not check for is64BitProcessor() */
      memRefSize = TR::Compiler->om.sizeofReferenceAddress();
      load = TR::InstOpCode::Op_load;
      store = TR::InstOpCode::Op_st;

      if (groups != 0)
         {
         TR::LabelSymbol *loopStart;

         if (groups != 1)
            {
            if (groups <= UPPER_IMMED)
               generateTrg1ImmInstruction(cg, TR::InstOpCode::li, node, regs[0], groups);
            else
               loadConstant(cg, node, groups, regs[0]);
            generateSrc1Instruction(cg, TR::InstOpCode::mtctr, node, regs[0]);

            loopStart = generateLabelSymbol(cg);
            generateLabelInstruction(cg, TR::InstOpCode::label, node, loopStart);
            }

         if (supportsLEArrayCopyInline)
            {
            generateTrg1MemInstruction(cg, TR::InstOpCode::lfd, node, fpRegs[3], TR::MemoryReference::createWithDisplacement(cg, src,            0, memRefSize));
            generateTrg1MemInstruction(cg, TR::InstOpCode::lfd, node, fpRegs[2], TR::MemoryReference::createWithDisplacement(cg, src,   memRefSize, memRefSize));
            generateTrg1MemInstruction(cg, TR::InstOpCode::lfd, node, fpRegs[1], TR::MemoryReference::createWithDisplacement(cg, src, 2*memRefSize, memRefSize));
            generateTrg1MemInstruction(cg, TR::InstOpCode::lfd, node, fpRegs[0], TR::MemoryReference::createWithDisplacement(cg, src, 3*memRefSize, memRefSize));
            }
         else
            {
            generateTrg1MemInstruction(cg, load, node, regs[3], TR::MemoryReference::createWithDisplacement(cg, src,            0, memRefSize));
            generateTrg1MemInstruction(cg, load, node, regs[2], TR::MemoryReference::createWithDisplacement(cg, src,   memRefSize, memRefSize));
            generateTrg1MemInstruction(cg, load, node, regs[1], TR::MemoryReference::createWithDisplacement(cg, src, 2*memRefSize, memRefSize));
            generateTrg1MemInstruction(cg, load, node, regs[0], TR::MemoryReference::createWithDisplacement(cg, src, 3*memRefSize, memRefSize));
            }

         if (groups != 1)
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi, node, src, src, 4*memRefSize);

         if (supportsLEArrayCopyInline)
            {
            generateMemSrc1Instruction(cg, TR::InstOpCode::stfd, node, TR::MemoryReference::createWithDisplacement(cg, dst,            0, memRefSize), fpRegs[3]);
            generateMemSrc1Instruction(cg, TR::InstOpCode::stfd, node, TR::MemoryReference::createWithDisplacement(cg, dst,   memRefSize, memRefSize), fpRegs[2]);
            generateMemSrc1Instruction(cg, TR::InstOpCode::stfd, node, TR::MemoryReference::createWithDisplacement(cg, dst, 2*memRefSize, memRefSize), fpRegs[1]);
            generateMemSrc1Instruction(cg, TR::InstOpCode::stfd, node, TR::MemoryReference::createWithDisplacement(cg, dst, 3*memRefSize, memRefSize), fpRegs[0]);
            }
         else
            {
            generateMemSrc1Instruction(cg, store, node, TR::MemoryReference::createWithDisplacement(cg, dst,            0, memRefSize), regs[3]);
            generateMemSrc1Instruction(cg, store, node, TR::MemoryReference::createWithDisplacement(cg, dst,   memRefSize, memRefSize), regs[2]);
            generateMemSrc1Instruction(cg, store, node, TR::MemoryReference::createWithDisplacement(cg, dst, 2*memRefSize, memRefSize), regs[1]);
            generateMemSrc1Instruction(cg, store, node, TR::MemoryReference::createWithDisplacement(cg, dst, 3*memRefSize, memRefSize), regs[0]);
            }

         if (groups != 1)
            {
            generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2, node, dst, dst, 4*memRefSize);
            generateConditionalBranchInstruction(cg, TR::InstOpCode::bdnz, node, loopStart, cndReg);
            }
         else
            {
            ix = 4*memRefSize;
            }
         }

      for (; residual>=memRefSize; residual-=memRefSize, ix+=memRefSize)
         {
         if (supportsLEArrayCopyInline)
            {
            TR::Register *oneReg = fpRegs[fpRegIx++];
            if (fpRegIx>3 || fpRegs[fpRegIx]==NULL)
               fpRegIx = 0;
            generateTrg1MemInstruction(cg, TR::InstOpCode::lfd, node, oneReg, TR::MemoryReference::createWithDisplacement(cg, src, ix, memRefSize));
            generateMemSrc1Instruction(cg, TR::InstOpCode::stfd, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, memRefSize), oneReg);
            }
         else
            {
            TR::Register *oneReg = regs[regIx++];
            if (regIx>3 || regs[regIx]==NULL)
               regIx = 0;
            generateTrg1MemInstruction(cg, load, node, oneReg, TR::MemoryReference::createWithDisplacement(cg, src, ix, memRefSize));
            generateMemSrc1Instruction(cg, store, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, memRefSize), oneReg);
            }
         }

      if (residual != 0)
         {
         if (residual & 4)
            {
            if (supportsLEArrayCopyInline)
               {
               TR::Register *oneReg = fpRegs[fpRegIx++];
               if (fpRegIx>3 || fpRegs[fpRegIx]==NULL)
                  fpRegIx = 0;
               generateTrg1MemInstruction(cg, TR::InstOpCode::lfs, node, oneReg, TR::MemoryReference::createWithDisplacement(cg, src, ix, 4));
               generateMemSrc1Instruction(cg, TR::InstOpCode::stfs, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, 4), oneReg);
               }
            else
               {
               TR::Register *oneReg = regs[regIx++];
               if (regIx>3 || regs[regIx]==NULL)
                  regIx = 0;
               generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, oneReg, TR::MemoryReference::createWithDisplacement(cg, src, ix, 4));
               generateMemSrc1Instruction(cg, TR::InstOpCode::stw, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, 4), oneReg);
               }
            ix += 4;
            }
         if (residual & 2)
            {
            TR::Register *oneReg = regs[regIx++];
            if (regIx>3 || regs[regIx]==NULL)
               regIx = 0;
            generateTrg1MemInstruction(cg, TR::InstOpCode::lhz, node, oneReg, TR::MemoryReference::createWithDisplacement(cg, src, ix, 2));
            generateMemSrc1Instruction(cg, TR::InstOpCode::sth, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, 2), oneReg);
            ix += 2;
            }
         if (residual & 1)
            {
            generateTrg1MemInstruction(cg, TR::InstOpCode::lbz, node, regs[regIx], TR::MemoryReference::createWithDisplacement(cg, src, ix, 1));
            generateMemSrc1Instruction(cg, TR::InstOpCode::stb, node, TR::MemoryReference::createWithDisplacement(cg, dst, ix, 1), regs[regIx]);
            }
         }
      }

   return;
   }

bool
J9::Power::CodeGenerator::inlineDirectCall(TR::Node *node, TR::Register *&resultReg)
   {
   TR::CodeGenerator *cg = self();
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());
   TR::MethodSymbol * methodSymbol = node->getSymbol()->getMethodSymbol();

   static bool disableDCAS = (feGetEnv("TR_DisablePPCDCAS") != NULL);
   static bool useJapaneseCompression = (feGetEnv("TR_JapaneseComp") != NULL);

   if (comp->getSymRefTab()->isNonHelper(node->getSymbolReference(), TR::SymbolReferenceTable::singlePrecisionSQRTSymbol))
      {
      resultReg = inlineSinglePrecisionFP(node, TR::InstOpCode::fsqrts, cg);
      return true;
      }
   else if (OMR::CodeGeneratorConnector::inlineDirectCall(node, resultReg))
      {
      return true;
      }
   else if (methodSymbol)
      {
      switch (methodSymbol->getRecognizedMethod())
         {
      case TR::java_util_concurrent_ConcurrentLinkedQueue_tmOffer:
         {
         if (cg->getSupportsInlineConcurrentLinkedQueue())
            {
            resultReg = inlineConcurrentLinkedQueueTMOffer(node, cg);
            return true;
            }
         break;
         }

      case TR::java_util_concurrent_ConcurrentLinkedQueue_tmPoll:
         {
         if (cg->getSupportsInlineConcurrentLinkedQueue())
            {
            resultReg = inlineConcurrentLinkedQueueTMPoll(node, cg);
            return true;
            }
         break;
         }
      case TR::jdk_internal_misc_Unsafe_copyMemory0:
      case TR::sun_misc_Unsafe_copyMemory:
         if (comp->canTransformUnsafeCopyToArrayCopy()
            && methodSymbol->isNative()
            && performTransformation(comp,
                  "O^O Call arraycopy instead of Unsafe.copyMemory: %s\n", cg->getDebug()->getName(node)))
            {
            TR::Node *src = node->getChild(1);
            TR::Node *srcOffset = node->getChild(2);
            TR::Node *dest = node->getChild(3);
            TR::Node *destOffset = node->getChild(4);
            TR::Node *len = node->getChild(5);

            if (comp->target().is32Bit())
               {
               srcOffset = TR::Node::create(TR::l2i, 1, srcOffset);
               destOffset = TR::Node::create(TR::l2i, 1, destOffset);
               len = TR::Node::create(TR::l2i, 1, len);
               src = TR::Node::create(TR::aiadd, 2, src, srcOffset);
               dest = TR::Node::create(TR::aiadd, 2, dest, destOffset);
               }
            else
               {
               src = TR::Node::create(TR::aladd, 2, src, srcOffset);
               dest = TR::Node::create(TR::aladd, 2, dest, destOffset);
               }

            TR::Node *arraycopyNode = TR::Node::createArraycopy(src, dest, len);
            TR::TreeEvaluator::arraycopyEvaluator(arraycopyNode,cg);

            if (node->getChild(0)->getRegister())
               cg->decReferenceCount(node->getChild(0));
            else
               node->getChild(0)->recursivelyDecReferenceCount();

            cg->decReferenceCount(node->getChild(1));
            cg->decReferenceCount(node->getChild(2));
            cg->decReferenceCount(node->getChild(3));
            cg->decReferenceCount(node->getChild(4));
            cg->decReferenceCount(node->getChild(5));

            return true;
            }
         break;

      case TR::sun_misc_Unsafe_setMemory:
         if (comp->canTransformUnsafeSetMemory())
            {
            TR::Node *dest = node->getChild(1);
            TR::Node *destOffset = node->getChild(2);
            TR::Node *len = node->getChild(3);
            TR::Node *byteValue = node->getChild(4);
            dest = TR::Node::create(TR::aladd, 2, dest, destOffset);

            TR::Node * copyMemNode = TR::Node::createWithSymRef(TR::arrayset, 3, 3, dest, len, byteValue, node->getSymbolReference());
            copyMemNode->setByteCodeInfo(node->getByteCodeInfo());

            TR::TreeEvaluator::setmemoryEvaluator(copyMemNode,cg);

            if (node->getChild(0)->getRegister())
               cg->decReferenceCount(node->getChild(0));
            else
               node->getChild(0)->recursivelyDecReferenceCount();

            cg->decReferenceCount(node->getChild(1));
            cg->decReferenceCount(node->getChild(2));
            cg->decReferenceCount(node->getChild(3));
            cg->decReferenceCount(node->getChild(4));
            return true;
            }
         break;

      case TR::java_lang_String_compress:
         resultReg = compressStringEvaluator(node, cg, useJapaneseCompression);
         return true;
         break;

      case TR::java_lang_String_compressNoCheck:
         resultReg = compressStringNoCheckEvaluator(node, cg, useJapaneseCompression);
         return true;

      case TR::java_lang_String_andOR:
         resultReg = andORStringEvaluator(node, cg);
         return true;
         break;

      case TR::java_util_concurrent_atomic_AtomicBoolean_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicInteger_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicInteger_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_incrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicInteger_decrementAndGet:
         {
         resultReg = inlineAtomicOps(node, cg, 4, methodSymbol, false);
         return true;
         break;
         }

      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndAdd:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndIncrement:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndDecrement:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_getAndSet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_addAndGet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_incrementAndGet:
      case TR::java_util_concurrent_atomic_AtomicIntegerArray_decrementAndGet:
         {
         resultReg = inlineAtomicOps(node, cg, 4, methodSymbol, true);
         return true;
         break;
         }

      case TR::java_lang_Math_ceil:
      case TR::java_lang_StrictMath_ceil:
         if (methodSymbol->getResolvedMethodSymbol()->canReplaceWithHWInstr())
            {
            resultReg = inlineDoublePrecisionFP(node, TR::InstOpCode::frip, cg);
            return true;
            }
         break;

      case TR::java_lang_Math_floor:
      case TR::java_lang_StrictMath_floor:
         if (methodSymbol->getResolvedMethodSymbol()->canReplaceWithHWInstr())
            {
            resultReg = inlineDoublePrecisionFP(node, TR::InstOpCode::frim, cg);
            return true;
            }
         break;

      case TR::java_lang_Math_copySign_F:
         // StrictMath copySign spec not guaranteed by fcpsgn instruction
         if (methodSymbol->getResolvedMethodSymbol()->canReplaceWithHWInstr())
            {
            resultReg = inlineSinglePrecisionFPTrg1Src2(node, TR::InstOpCode::fcpsgn, cg);
            return true;
            }
         break;

      case TR::java_lang_Math_copySign_D:
         // StrictMath copySign spec not guaranteed by fcpsgn instruction
         if (methodSymbol->getResolvedMethodSymbol()->canReplaceWithHWInstr())
            {
            resultReg = inlineDoublePrecisionFPTrg1Src2(node, TR::InstOpCode::fcpsgn, cg);
            return true;
            }
         break;

      case TR::java_lang_Math_fma_D:
      case TR::java_lang_StrictMath_fma_D:
         resultReg = inlineFPTrg1Src3(node, TR::InstOpCode::fmadd, cg);
         return true;

      case TR::java_lang_Math_fma_F:
      case TR::java_lang_StrictMath_fma_F:
         resultReg = inlineFPTrg1Src3(node, TR::InstOpCode::fmadds, cg);
         return true;

      case TR::java_lang_String_hashCodeImplDecompressed:
         if (!TR::Compiler->om.canGenerateArraylets() && comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) && comp->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX) && !comp->compileRelocatableCode()
#ifdef J9VM_OPT_JITSERVER
               && !comp->isOutOfProcessCompilation()
#endif
            )
            {
            resultReg = inlineStringHashcode(node, cg);
            return true;
            }
         break;

      case TR::com_ibm_jit_JITHelpers_transformedEncodeUTF16Big:
      case TR::com_ibm_jit_JITHelpers_transformedEncodeUTF16Little:
         if (comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P7) && comp->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX))
            {
            resultReg = inlineEncodeUTF16(node, cg);
            return true;
            }
         break;

      case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfLatin1:
         if (cg->getSupportsInlineStringIndexOf())
            {
            resultReg = inlineIntrinsicIndexOf(node, cg, true);
            return true;
            }
         break;

      case TR::com_ibm_jit_JITHelpers_intrinsicIndexOfUTF16:
         if (cg->getSupportsInlineStringIndexOf())
            {
            resultReg = inlineIntrinsicIndexOf(node, cg, false);
            return true;
            }
         break;

      case TR::sun_misc_Unsafe_compareAndSwapInt_jlObjectJII_Z:
        // In Java9 this can be either the jdk.internal JNI method or the sun.misc Java wrapper.
        // In Java8 it will be sun.misc which will contain the JNI directly.
        // We only want to inline the JNI methods, so add an explicit test for isNative().
        if (!methodSymbol->isNative())
           break;

        if ((node->isUnsafeGetPutCASCallOnNonArray() || !TR::Compiler->om.canGenerateArraylets()) && node->isSafeForCGToFastPathUnsafeCall())
            {
            resultReg = VMinlineCompareAndSwap(node, cg, false);
            return true;
            }
         break;

      case TR::sun_misc_Unsafe_compareAndSwapLong_jlObjectJJJ_Z:
         if (comp->getOption(TR_TraceCG))
            traceMsg(comp, "In evaluator for compareAndSwapLong. node = %p node->isSafeForCGToFastPathUnsafeCall = %p\n", node, node->isSafeForCGToFastPathUnsafeCall());
         // As above, we only want to inline the JNI methods, so add an explicit test for isNative()
         if (!methodSymbol->isNative())
            break;

         if (comp->target().is64Bit() && (node->isUnsafeGetPutCASCallOnNonArray() || !TR::Compiler->om.canGenerateArraylets()) && node->isSafeForCGToFastPathUnsafeCall())
            {
            resultReg = VMinlineCompareAndSwap(node, cg, true);
            return true;
            }
         else if ((node->isUnsafeGetPutCASCallOnNonArray() || !TR::Compiler->om.canGenerateArraylets()) && node->isSafeForCGToFastPathUnsafeCall())
            {
            resultReg = inlineAtomicOperation(node, cg, methodSymbol);
            return true;
            }
         break;

      case TR::sun_misc_Unsafe_compareAndSwapObject_jlObjectJjlObjectjlObject_Z:
         // As above, we only want to inline the JNI methods, so add an explicit test for isNative()
         if (!methodSymbol->isNative())
            break;

         if ((node->isUnsafeGetPutCASCallOnNonArray() || !TR::Compiler->om.canGenerateArraylets()) && node->isSafeForCGToFastPathUnsafeCall())
            {
            resultReg = VMinlineCompareAndSwapObject(node, cg);
            return true;
            }
         break;

      case TR::java_nio_Bits_keepAlive:
      case TR::java_lang_ref_Reference_reachabilityFence:
         {
         TR::Node *paramNode = node->getFirstChild();
         TR::Register *paramReg = cg->evaluate(paramNode);
         TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(1, 1, cg->trMemory());
         TR::addDependency(conditions, paramReg, TR::RealRegister::NoReg, TR_GPR, cg);
         TR::LabelSymbol *label = TR::LabelSymbol::create(cg->trHeapMemory(),cg);
         generateDepLabelInstruction(cg, TR::InstOpCode::label, node, label, conditions);
         cg->decReferenceCount(paramNode);
         resultReg = NULL;
         return true;
         }

      case TR::x10JITHelpers_getCPU:
         break;

      case TR::java_util_concurrent_atomic_Fences_reachabilityFence:
         {
         cg->decReferenceCount(node->getChild(0));
         resultReg = NULL;
         return true;
         }

      case TR::java_util_concurrent_atomic_Fences_orderReads:
         if (performTransformation(comp, "O^O PPC Evaluator: Replacing read/read Fence with an lwsync [%p].\n", node))
            {
            // mark as seen and then just don't bother generating instructions
            TR::Node *callNode = node;
            int32_t numArgs = callNode->getNumChildren();
            for (int32_t i = numArgs - 1; i >= 0; i--)
               cg->decReferenceCount(callNode->getChild(i));
            cg->decReferenceCount(callNode);
            generateInstruction(cg, TR::InstOpCode::isync, node);
            resultReg = NULL;
            return true;
            }
         break;

         // for now all preStores simply emit lwsync  See JIT design 1598
      case TR::java_util_concurrent_atomic_Fences_preStoreFence:
      case TR::java_util_concurrent_atomic_Fences_preStoreFence_jlObject:
      case TR::java_util_concurrent_atomic_Fences_preStoreFence_jlObjectI:
      case TR::java_util_concurrent_atomic_Fences_preStoreFence_jlObjectjlrField:
      case TR::java_util_concurrent_atomic_Fences_postLoadFence:
      case TR::java_util_concurrent_atomic_Fences_postLoadFence_jlObjectjlrField:
      case TR::java_util_concurrent_atomic_Fences_postLoadFence_jlObject:
      case TR::java_util_concurrent_atomic_Fences_postLoadFence_jlObjectI:
      case TR::java_util_concurrent_atomic_Fences_orderWrites:
         if (performTransformation(comp, "O^O PPC Evaluator: Replacing store/store Fence with an lwsync [%p].\n", node))
            {
            // mark as seen and then just don't bother generating instructions
            TR::Node *callNode = node;
            int32_t numArgs = callNode->getNumChildren();
            for (int32_t i = numArgs - 1; i >= 0; i--)
               cg->decReferenceCount(callNode->getChild(i));
            cg->decReferenceCount(callNode);
            generateInstruction(cg, TR::InstOpCode::lwsync, node);
            resultReg = NULL;
            return true;
            }
         break;

         // for now just emit a sync.  See design 1598
      case TR::java_util_concurrent_atomic_Fences_postStorePreLoadFence_jlObject:
      case TR::java_util_concurrent_atomic_Fences_postStorePreLoadFence_jlObjectjlrField:
      case TR::java_util_concurrent_atomic_Fences_postStorePreLoadFence:
      case TR::java_util_concurrent_atomic_Fences_postStorePreLoadFence_jlObjectI:
      case TR::java_util_concurrent_atomic_Fences_orderAccesses:
         if (performTransformation(comp, "O^O PPC Evaluator: Replacing store/load Fence with a sync [%p].\n", node))
            {
            TR::Node *callNode = node;
            int32_t numArgs = callNode->getNumChildren();
            for (int32_t i = numArgs - 1; i >= 0; i--)
               cg->decReferenceCount(callNode->getChild(i));
            cg->decReferenceCount(callNode);
            generateInstruction(cg, TR::InstOpCode::sync, node);
            resultReg = NULL;
            return true;
            }
         break;

      case TR::java_lang_Class_isAssignableFrom:
         {
         // Do not use an inline class check if the 'this' Class object is known to be an
         // abstract class or an interface at compile-time (the check will always fail
         // and go out of line).
         //
         TR::Node *receiverNode = node->getFirstChild();
         if (receiverNode->getOpCodeValue() == TR::aloadi &&
             receiverNode->getSymbolReference() == comp->getSymRefTab()->findJavaLangClassFromClassSymbolRef() &&
             receiverNode->getFirstChild()->getOpCodeValue() == TR::loadaddr)
            {
            TR::SymbolReference *receiverClassSymRef = receiverNode->getFirstChild()->getSymbolReference();
            if (receiverClassSymRef->isClassInterface(comp) ||
                receiverClassSymRef->isClassAbstract(comp))
               break;
            }

         static bool disable = feGetEnv("TR_disableInlineIsAssignableFrom") != NULL;
         if (!disable &&
             performTransformation(comp, "O^O PPC Evaluator: Specialize call to java/lang/Class.isAssignableFrom [%p].\n", node))
            {
            resultReg = inlineIsAssignableFrom(node, cg);
            return true;
            }

         break;
         }

      default:
         break;
         }
      }

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   if (self()->inlineCryptoMethod(node, resultReg))
      {
      return true;
      }
#endif

   // No method specialization was done.
   //
   resultReg = NULL;
   return false;
   }

void VMgenerateCatchBlockBBStartPrologue(TR::Node *node, TR::Instruction *fenceInstruction, TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());

   TR::Block *block = node->getBlock();

   if (fej9->shouldPerformEDO(block, comp))
      {
      TR::Register *biAddrReg = cg->allocateRegister();
      TR::Register *recompCounterReg = cg->allocateRegister();
      intptr_t addr = (intptr_t) (comp->getRecompilationInfo()->getCounterAddress());
      TR::Instruction *cursor = loadAddressConstant(cg, comp->compileRelocatableCode(), node, addr, biAddrReg);
      TR::MemoryReference *loadbiMR = TR::MemoryReference::createWithDisplacement(cg, biAddrReg, 0, TR::Compiler->om.sizeofReferenceAddress());
      TR::MemoryReference *storebiMR = TR::MemoryReference::createWithDisplacement(cg, biAddrReg, 0, TR::Compiler->om.sizeofReferenceAddress());
      cursor = generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, recompCounterReg, loadbiMR);
      TR::Register *cndRegister = cg->allocateRegister(TR_CCR);
      cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::addi2_r, node, recompCounterReg, recompCounterReg, cndRegister, -1, cursor);
      cursor = generateMemSrc1Instruction(cg,TR::InstOpCode::Op_st, node, storebiMR, recompCounterReg, cursor);
      cg->stopUsingRegister(biAddrReg);
      cg->stopUsingRegister(recompCounterReg);

      TR::LabelSymbol *snippetLabel = NULL;
      TR::LabelSymbol *doneLabel = generateLabelSymbol(cg);
      // snippet call chain through jitCallCFunction kills 3 parameter
      // registers that we will reserve here
      TR::Register *arg1Reg, *arg2Reg, *arg3Reg;
      arg1Reg = cg->allocateRegister();
      arg2Reg = cg->allocateRegister();
      arg3Reg = cg->allocateRegister();

      snippetLabel = cg->lookUpSnippet(TR::Snippet::IsForceRecompilation, NULL);
      if (snippetLabel == NULL)
         {
         TR::Snippet *snippet;
         snippetLabel = generateLabelSymbol(cg);
         snippet = new (cg->trHeapMemory()) TR::PPCForceRecompilationSnippet(snippetLabel, doneLabel, cursor, cg);
         cg->addSnippet(snippet);
         }

      TR::RegisterDependencyConditions *conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(4, 4, cg->trMemory());
      TR::addDependency(conditions, arg1Reg, TR::RealRegister::gr3, TR_GPR, cg);
      TR::addDependency(conditions, arg2Reg, TR::RealRegister::gr4, TR_GPR, cg);
      TR::addDependency(conditions, arg3Reg, TR::RealRegister::gr5, TR_GPR, cg);
      TR::addDependency(conditions, cndRegister, TR::RealRegister::cr0, TR_CCR, cg);

      // used to be blel
      // only need to call snippet once, so using beql instead
      cursor = generateConditionalBranchInstruction(cg, TR::InstOpCode::beql, node, snippetLabel, cndRegister);
      generateDepLabelInstruction(cg, TR::InstOpCode::label, node, doneLabel, conditions);

      cg->machine()->setLinkRegisterKilled(true);
      conditions->stopUsingDepRegs(cg);
      }
   }

TR::Instruction *loadAddressRAM32(TR::CodeGenerator *cg, TR::Node * node, int32_t value, TR::Register *trgReg)
   {
   // load a 32-bit constant into a register with a fixed 2 instruction sequence
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());

   bool isAOT = comp->compileRelocatableCode();

   TR::Instruction *cursor = cg->getAppendInstruction();

   cursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, trgReg, isAOT ? 0 : value>>16, cursor);

   if (value != 0x0)
      {
      TR_ExternalRelocationTargetKind reloType;
      if (node->getSymbol()->castToResolvedMethodSymbol()->isSpecial())
         reloType = TR_SpecialRamMethodConst;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isStatic())
         reloType = TR_StaticRamMethodConst;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isVirtual())
         reloType = TR_VirtualRamMethodConst;
      else
         {
         reloType = TR_NoRelocation;
         TR_ASSERT(0,"JNI relocation not supported.");
         }
      if(isAOT)
         cg->addExternalRelocation(new (cg->trHeapMemory()) TR::ExternalRelocation((uint8_t *)cursor, (uint8_t *) node->getSymbolReference(),
               node ? (uint8_t *)(intptr_t)node->getInlinedSiteIndex() : (uint8_t *)-1,
                  reloType, cg),
                  __FILE__, __LINE__, node);
      }

   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : value&0x0000ffff, cursor);

   cg->setAppendInstruction(cursor);
   return(cursor);
   }

TR::Instruction *loadAddressRAM(TR::CodeGenerator *cg, TR::Node * node, intptr_t value, TR::Register *trgReg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   bool isAOT = comp->compileRelocatableCode();
   if (comp->target().is32Bit())
      {
      return loadAddressRAM32(cg, node, (int32_t)value, trgReg);
      }

   // load a 64-bit constant into a register with a fixed 5 instruction sequence
   TR::Instruction *cursor = cg->getAppendInstruction();

   // lis trgReg, upper 16-bits
   cursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, trgReg, isAOT? 0: (value>>48) , cursor);
   if (value != 0x0)
      {
      TR_ExternalRelocationTargetKind reloType;
      if (node->getSymbol()->castToResolvedMethodSymbol()->isSpecial())
         reloType = TR_SpecialRamMethodConst;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isStatic())
         reloType = TR_StaticRamMethodConst;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isVirtual())
         reloType = TR_VirtualRamMethodConst;
      else
         {
         reloType = TR_NoRelocation;
         TR_ASSERT(0,"JNI relocation not supported.");
         }
      if(isAOT)
         cg->addExternalRelocation(new (cg->trHeapMemory()) TR::ExternalRelocation((uint8_t *)cursor, (uint8_t *) node->getSymbolReference(),
               node ? (uint8_t *)(intptr_t)node->getInlinedSiteIndex() : (uint8_t *)-1,
                  reloType, cg),
                  __FILE__,__LINE__, node);
      }
   // ori trgReg, trgReg, next 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : ((value>>32) & 0x0000ffff), cursor);
   // shiftli trgReg, trgReg, 32
   cursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, trgReg, trgReg, 32, CONSTANT64(0xFFFFFFFF00000000), cursor);
   // oris trgReg, trgReg, next 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::oris, node, trgReg, trgReg, isAOT ? 0 : ((value>>16) & 0x0000ffff), cursor);
   // ori trgReg, trgReg, last 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : (value & 0x0000ffff), cursor);

   cg->setAppendInstruction(cursor);

   return(cursor);
   }

TR::Instruction *loadAddressJNI32(TR::CodeGenerator *cg, TR::Node * node, int32_t value, TR::Register *trgReg)
   {
   // load a 32-bit constant into a register with a fixed 2 instruction sequence
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());

   bool isAOT = comp->compileRelocatableCode();

   TR::Instruction *cursor = cg->getAppendInstruction();

   cursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, trgReg, isAOT ? 0 : value>>16, cursor);

   if (value != 0x0)
      {
      TR_ExternalRelocationTargetKind reloType;
      if (node->getSymbol()->castToResolvedMethodSymbol()->isSpecial())
         reloType = TR_JNISpecialTargetAddress;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isStatic())
         reloType = TR_JNIStaticTargetAddress;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isVirtual())
         reloType = TR_JNIVirtualTargetAddress;
      else
         {
         reloType = TR_NoRelocation;
         TR_ASSERT(0,"JNI relocation not supported.");
         }
      if(isAOT)
         cg->addExternalRelocation(new (cg->trHeapMemory()) TR::ExternalRelocation((uint8_t *)cursor, (uint8_t *) node->getSymbolReference(),
               node ? (uint8_t *)(intptr_t)node->getInlinedSiteIndex() : (uint8_t *)-1,
                  reloType, cg),
                  __FILE__, __LINE__, node);
      }

   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : value&0x0000ffff, cursor);

   cg->setAppendInstruction(cursor);
   return(cursor);
   }

TR::Instruction *loadAddressJNI(TR::CodeGenerator *cg, TR::Node * node, intptr_t value, TR::Register *trgReg)
   {
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *) (comp->fe());
   bool isAOT = comp->compileRelocatableCode();
   if (comp->target().is32Bit())
      {
      return loadAddressJNI32(cg, node, (int32_t)value, trgReg);
      }

   // load a 64-bit constant into a register with a fixed 5 instruction sequence
   TR::Instruction *cursor = cg->getAppendInstruction();

   // lis trgReg, upper 16-bits
   cursor = generateTrg1ImmInstruction(cg, TR::InstOpCode::lis, node, trgReg, isAOT? 0: (value>>48) , cursor);
   if (value != 0x0)
      {
      TR_ExternalRelocationTargetKind reloType;
      if (node->getSymbol()->castToResolvedMethodSymbol()->isSpecial())
         reloType = TR_JNISpecialTargetAddress;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isStatic())
         reloType = TR_JNIStaticTargetAddress;
      else if (node->getSymbol()->castToResolvedMethodSymbol()->isVirtual())
         reloType = TR_JNIVirtualTargetAddress;
      else
         {
         reloType = TR_NoRelocation;
         TR_ASSERT(0,"JNI relocation not supported.");
         }
      if(isAOT)
         cg->addExternalRelocation(new (cg->trHeapMemory()) TR::ExternalRelocation((uint8_t *)cursor, (uint8_t *) node->getSymbolReference(),
               node ? (uint8_t *)(intptr_t)node->getInlinedSiteIndex() : (uint8_t *)-1,
                  reloType, cg),
                  __FILE__,__LINE__, node);
      }
   // ori trgReg, trgReg, next 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : ((value>>32) & 0x0000ffff), cursor);
   // shiftli trgReg, trgReg, 32
   cursor = generateTrg1Src1Imm2Instruction(cg, TR::InstOpCode::rldicr, node, trgReg, trgReg, 32, CONSTANT64(0xFFFFFFFF00000000), cursor);
   // oris trgReg, trgReg, next 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::oris, node, trgReg, trgReg, isAOT ? 0 : ((value>>16) & 0x0000ffff), cursor);
   // ori trgReg, trgReg, last 16-bits
   cursor = generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::ori, node, trgReg, trgReg, isAOT ? 0 : (value & 0x0000ffff), cursor);

   cg->setAppendInstruction(cursor);

   return(cursor);
   }

TR::Register *J9::Power::TreeEvaluator::directCallEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   TR::SymbolReference *symRef    = node->getSymbolReference();
   TR::MethodSymbol    *callee    = symRef->getSymbol()->castToMethodSymbol();
   TR::Linkage      *linkage;
   TR::Register        *returnRegister;

   if (!cg->inlineDirectCall(node, returnRegister))
      {
      TR::SymbolReferenceTable *symRefTab = cg->comp()->getSymRefTab();

      // Non-helpers supported by code gen. are expected to be inlined
      if (symRefTab->isNonHelper(symRef))
         {
         TR_ASSERT(!cg->supportsNonHelper(symRefTab->getNonHelperSymbol(symRef)),
                   "Non-helper %d was not inlined, but was expected to be.\n",
                   symRefTab->getNonHelperSymbol(symRef));
         }

      linkage = cg->deriveCallingLinkage(node, false);
      returnRegister = linkage->buildDirectDispatch(node);
      }

   return returnRegister;
   }


TR::Register *J9::Power::TreeEvaluator::tstartEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   // tstart
   //  - persistentFailNode
   //  - transientFailNode
   //  - fallThroughNode (next block)
   //  - monitorObject
   //fprintf(stderr,"tstart Start\n");
   TR::Node *persistentFailureNode = node->getFirstChild();
   TR::Node *transientFailureNode  = node->getSecondChild();
   TR::Node *fallThrough = node->getThirdChild();
   TR::Node *objNode = node->getChild(3);
   TR::Node *GRANode = NULL;
   TR::Compilation *comp = cg->comp();

   TR::LabelSymbol *labelPersistentFailure = persistentFailureNode->getBranchDestination()->getNode()->getLabel();
   TR::LabelSymbol *labelTransientFailure  = transientFailureNode->getBranchDestination()->getNode()->getLabel();
   TR::LabelSymbol *labelfallThrough  = fallThrough->getBranchDestination()->getNode()->getLabel();
   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *lockwordLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *noTexStatsLabel = generateLabelSymbol(cg);

   TR::Register *objReg = cg->evaluate(objNode);
   TR::Register *monReg = cg->allocateRegister();
   TR::Register *cndReg = cg->allocateRegister(TR_CCR);
   TR::Register *tempReg = cg->allocateRegister();
   //TR::Register *temp2Reg = cg->allocateRegister();
   //TR::Register *temp3Reg = cg->allocateRegister();

   // Dependency conditions for this evaluator's internal control flow
   TR::RegisterDependencyConditions *conditions = NULL;
   // Dependency conditions for GRA
   TR::RegisterDependencyConditions *fallThroughConditions = NULL;
   TR::RegisterDependencyConditions *persistentConditions = NULL;
   TR::RegisterDependencyConditions *transientConditions  = NULL;

   conditions = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(4, 4, cg->trMemory());

   if (fallThrough->getNumChildren() != 0)
      {
      GRANode = fallThrough->getFirstChild();
      cg->evaluate(GRANode);
      fallThroughConditions = generateRegisterDependencyConditions(cg, GRANode, 0);
      cg->decReferenceCount(GRANode);
      }

   if (persistentFailureNode->getNumChildren() != 0)
      {
      GRANode = persistentFailureNode->getFirstChild();
      cg->evaluate(GRANode);
      persistentConditions = generateRegisterDependencyConditions(cg, GRANode, 0);
      cg->decReferenceCount(GRANode);
      }

   if (transientFailureNode->getNumChildren() != 0)
      {
      GRANode = transientFailureNode->getFirstChild();
      cg->evaluate(GRANode);
      transientConditions = generateRegisterDependencyConditions(cg, GRANode, 0);
      cg->decReferenceCount(GRANode);
      }

   uint32_t conditionCursor = conditions->getAddCursorForPre();
   TR::addDependency(conditions, objReg, TR::RealRegister::NoReg, TR_GPR, cg);
   conditions->getPreConditions()->getRegisterDependency(conditionCursor)->setExcludeGPR0();
   conditions->getPostConditions()->getRegisterDependency(conditionCursor++)->setExcludeGPR0();
   TR::addDependency(conditions, cndReg, TR::RealRegister::cr0, TR_CCR, cg);
   TR::addDependency(conditions, monReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(conditions, tempReg, TR::RealRegister::NoReg, TR_GPR, cg);

   static char * debugTMTLE = feGetEnv("debugTMTLE");

   if (debugTMTLE)
      printf ("\nTM: use TM TLE in %s (%s) %p", comp->signature(), comp->getHotnessName(comp->getMethodHotness()), node);

   if (debugTMTLE )
      {
      if (persistentConditions)
        {
        generateDepLabelInstruction(cg, TR::InstOpCode::b, node, labelPersistentFailure, persistentConditions);
        }
     else
        {
        generateLabelInstruction(cg, TR::InstOpCode::b, node, labelPersistentFailure);
         }
      }
   else
      {
      generateInstruction(cg, TR::InstOpCode::tbegin_r, node);
      }

      generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node,
                                           lockwordLabel, cndReg);

   generateTrg1Instruction(cg, TR::InstOpCode::mftexasru, node, monReg);

   // This mask *should* correspond to the TEXASR failure persistent bit (8)
   // NOT the abort bit (31, indicates an explicit abort)
   //loadConstant(cg, node, 0x01000001, tempReg);
   loadConstant(cg, node, 0x01000000, tempReg);
   generateTrg1Src2Instruction(cg, TR::InstOpCode::and_r, node, tempReg, tempReg, monReg);
   if (transientConditions)
      {
      generateDepConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, labelTransientFailure, cndReg, transientConditions);
      }
   else
      {
      generateConditionalBranchInstruction(cg, TR::InstOpCode::beq, node, labelTransientFailure, cndReg);
      }
   if (persistentConditions)
      {
      generateDepLabelInstruction(cg, TR::InstOpCode::b, node, labelPersistentFailure, persistentConditions);
      }
   else
      {
      generateLabelInstruction(cg, TR::InstOpCode::b, node, labelPersistentFailure);
      }

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, lockwordLabel, conditions);

   int32_t lwOffset = cg->fej9()->getByteOffsetToLockword((TR_OpaqueClassBlock *) cg->getMonClass(node));

   if (comp->target().is64Bit() && cg->fej9()->generateCompressedLockWord())
      {
      generateTrg1MemInstruction(cg, TR::InstOpCode::lwz, node, monReg,
                                 TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, 4));
      }
   else
      {
      generateTrg1MemInstruction(cg,TR::InstOpCode::Op_load, node, monReg,
                                 TR::MemoryReference::createWithDisplacement(cg, objReg, lwOffset, TR::Compiler->om.sizeofReferenceAddress()));
      }

   // abort if lock is held
   if (comp->target().is32Bit() || cg->fej9()->generateCompressedLockWord())
      {
      generateSrc1Instruction(cg, TR::InstOpCode::tabortwneqi_r, node, monReg, 0);
      }
   else
      {
      generateSrc1Instruction(cg, TR::InstOpCode::tabortdneqi_r, node, monReg, 0);
      }

   TR::TreeTop *BBendTreeTop = cg->getCurrentEvaluationTreeTop()->getNextTreeTop();
   TR::TreeTop *BBstartTreeTop = NULL;
   if (BBendTreeTop)
      BBstartTreeTop = BBendTreeTop->getNextTreeTop();
   TR::TreeTop *fallThruTarget = fallThrough->getBranchDestination();

   if (BBstartTreeTop && (fallThruTarget == BBstartTreeTop))
      {
      if (fallThroughConditions)
         generateDepLabelInstruction(cg, TR::InstOpCode::label, node, startLabel, fallThroughConditions);
      else
         generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);
      }
   else
      {
      if (fallThroughConditions)
         generateDepLabelInstruction(cg, TR::InstOpCode::b, node, labelfallThrough, fallThroughConditions);
      else
         generateLabelInstruction(cg, TR::InstOpCode::b, node, labelfallThrough);
      }

   cg->stopUsingRegister(monReg);
   cg->stopUsingRegister(cndReg);
   cg->stopUsingRegister(tempReg);

   cg->decReferenceCount(objNode);
   cg->decReferenceCount(persistentFailureNode);
   cg->decReferenceCount(transientFailureNode);
   cg->decReferenceCount(fallThrough);

   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::tfinishEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   generateInstruction(cg, TR::InstOpCode::tend_r, node);
   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::tabortEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   generateInstruction(cg, TR::InstOpCode::tabort_r, node);
   return NULL;
   }

TR::Register *J9::Power::TreeEvaluator::arraycopyEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
#ifdef OMR_GC_CONCURRENT_SCAVENGER
   /*
    * This version of arraycopyEvaluator is designed to handle the special case where read barriers are
    * needed for field loads. At the time of writing, read barriers are used for Concurrent Scavenge GC.
    * If there are no read barriers then the original implementation of arraycopyEvaluator can be used.
    */
   if (TR::Compiler->om.readBarrierType() == gc_modron_readbar_none ||
          !node->chkNoArrayStoreCheckArrayCopy() ||
          !node->isReferenceArrayCopy() ||
          debug("noArrayCopy")
      )
      {
      return OMR::TreeEvaluatorConnector::arraycopyEvaluator(node, cg);
      }

   TR::Compilation *comp = cg->comp();
   TR::Instruction      *gcPoint;
   TR::Node             *srcObjNode, *dstObjNode, *srcAddrNode, *dstAddrNode, *lengthNode;
   TR::Register         *srcObjReg, *dstObjReg, *srcAddrReg, *dstAddrReg, *lengthReg;
   bool stopUsingCopyReg1, stopUsingCopyReg2, stopUsingCopyReg3, stopUsingCopyReg4, stopUsingCopyReg5 = false;

   srcObjNode = node->getChild(0);
   dstObjNode = node->getChild(1);
   srcAddrNode = node->getChild(2);
   dstAddrNode = node->getChild(3);
   lengthNode = node->getChild(4);

   // These calls evaluate the nodes and give back registers that can be clobbered if needed.
   stopUsingCopyReg1 = TR::TreeEvaluator::stopUsingCopyReg(srcObjNode, srcObjReg, cg);
   stopUsingCopyReg2 = TR::TreeEvaluator::stopUsingCopyReg(dstObjNode, dstObjReg, cg);
   stopUsingCopyReg3 = TR::TreeEvaluator::stopUsingCopyReg(srcAddrNode, srcAddrReg, cg);
   stopUsingCopyReg4 = TR::TreeEvaluator::stopUsingCopyReg(dstAddrNode, dstAddrReg, cg);

   lengthReg = cg->evaluate(lengthNode);
   if (!cg->canClobberNodesRegister(lengthNode))
      {
      TR::Register *lenCopyReg = cg->allocateRegister();
      generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, lengthNode, lenCopyReg, lengthReg);
      lengthReg = lenCopyReg;
      stopUsingCopyReg5 = true;
      }

   // Inline forward arrayCopy with constant length.
   int64_t len = -1;
   if (node->isForwardArrayCopy() && lengthNode->getOpCode().isLoadConst())
      {
      len = (lengthNode->getType().isInt32() ? lengthNode->getInt() : lengthNode->getLongInt());

      // inlineArrayCopy_ICF is not currently capable of handling very long lengths correctly. Under some circumstances,
      // it will generate an li instruction with an out-of-bounds immediate, which triggers an assert in the binary
      // encoder.
      if (len >= 0 && len < MAX_PPC_ARRAYCOPY_INLINE)
         {
         /*
          * This path generates code to perform a runtime check on whether concurrent GC is done moving objects or not.
          * If it isn't, a call to referenceArrayCopy helper should be made.
          * If it is, using the inlined array copy code path is okay.
          */
         int32_t groups;

         // Our hands are tied somewhat due to the potential ReferenceArrayCopy call
         // We can really work with less registers in P10 case. For readability and
         // maintainability, we are simplifying the logic but using more registers.

         bool postP10CopyInline = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P10) &&
                                  cg->comp()->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX);

         static bool disableLEArrayCopyInline = (feGetEnv("TR_disableLEArrayCopyInline") != NULL);
         bool supportsLEArrayCopyInline = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) &&
                                          !disableLEArrayCopyInline &&
                                          comp->target().cpu.isLittleEndian() &&
                                          comp->target().cpu.hasFPU() &&
                                          comp->target().is64Bit();

         // There are a minimum of 9 dependencies.
         uint8_t numDeps = 9;

         if (comp->target().is64Bit())
            {
            groups = len >> 5;
            }
         else
            {
            groups = len >> 4;
            }

         TR::Register *condReg = cg->allocateRegister(TR_CCR);

         // These registers are used when taking the inlineArrayCopy_ICF path.
         TR::Register *tmp1Reg = cg->allocateRegister(TR_GPR);
         TR::Register *tmp2Reg = NULL;
         TR::Register *tmp3Reg = NULL;
         TR::Register *tmp4Reg = NULL;
         TR::Register *fp1Reg = NULL;
         TR::Register *fp2Reg = NULL;
         TR::Register *fp3Reg = NULL;
         TR::Register *fp4Reg = NULL;

         // These registers are used when taking the referenceArrayCopy helper call path.
         TR::Register *r3Reg = cg->allocateRegister();
         TR::Register *metaReg = cg->getMethodMetaDataRegister();

         if (postP10CopyInline)
            {
            numDeps += 1;
            fp1Reg = cg->allocateRegister(TR_VSX_VECTOR);
            }
         else
            {
            if (groups != 0)
               {
               numDeps += 3;
               tmp2Reg = cg->allocateRegister(TR_GPR);
               tmp3Reg = cg->allocateRegister(TR_GPR);
               tmp4Reg = cg->allocateRegister(TR_GPR);
               }

            if (supportsLEArrayCopyInline)
               {
               numDeps += 4;
               fp1Reg = cg->allocateRegister(TR_FPR);
               fp2Reg = cg->allocateRegister(TR_FPR);
               fp3Reg = cg->allocateRegister(TR_FPR);
               fp4Reg = cg->allocateRegister(TR_FPR);
               }
            }

         /*
          * r3-r8 are used to pass parameters to the referenceArrayCopy helper.
          * r11 is used for the tmp1Reg since r11 gets killed by the trampoline and values put into tmp1Reg are not needed after the trampoline.
          */
         TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, numDeps, cg->trMemory());
         deps->addPostCondition(condReg, TR::RealRegister::cr0);
         deps->addPostCondition(metaReg, TR::RealRegister::NoReg);

         deps->addPostCondition(r3Reg, TR::RealRegister::gr3);
         deps->addPostCondition(srcObjReg, TR::RealRegister::gr4);
         deps->addPostCondition(dstObjReg, TR::RealRegister::gr5);
         deps->addPostCondition(srcAddrReg, TR::RealRegister::gr6);
         deps->addPostCondition(dstAddrReg, TR::RealRegister::gr7);
         deps->addPostCondition(lengthReg, TR::RealRegister::gr8);

         deps->addPostCondition( tmp1Reg, TR::RealRegister::gr11);

         if (postP10CopyInline)
            {
            deps->addPostCondition(fp1Reg, TR::RealRegister::NoReg);
            }
         else
            {
            if (groups != 0)
               {
               deps->addPostCondition(tmp2Reg, TR::RealRegister::NoReg);
               deps->addPostCondition(tmp3Reg, TR::RealRegister::NoReg);
               deps->addPostCondition(tmp4Reg, TR::RealRegister::NoReg);
               }

            if (supportsLEArrayCopyInline)
               {
               deps->addPostCondition(fp1Reg, TR::RealRegister::NoReg);
               deps->addPostCondition(fp2Reg, TR::RealRegister::NoReg);
               deps->addPostCondition(fp3Reg, TR::RealRegister::NoReg);
               deps->addPostCondition(fp4Reg, TR::RealRegister::NoReg);
               }
            }

         TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
         TR::LabelSymbol *helperLabel = generateLabelSymbol(cg);
         TR::LabelSymbol *endLabel = generateLabelSymbol(cg);
         startLabel->setStartInternalControlFlow();
         endLabel->setEndInternalControlFlow();

         generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

         // Runtime check for concurrent scavenger.
         generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, tmp1Reg,
               TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, readBarrierRangeCheckTop), TR::Compiler->om.sizeofReferenceAddress()));
         generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::Op_cmpli, node, condReg, tmp1Reg, 0);
         generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, helperLabel, condReg);

         // Generate assembly for inlined version of array copy.
         inlineArrayCopy_ICF(node, len, srcAddrReg, dstAddrReg, cg, condReg,
                             tmp1Reg, tmp2Reg, tmp3Reg, tmp4Reg,
                             fp1Reg, fp2Reg, fp3Reg, fp4Reg);

         generateLabelInstruction(cg, TR::InstOpCode::b, node, endLabel);

         // Start of referenceArrayCopy helper path.
         generateLabelInstruction(cg, TR::InstOpCode::label, node, helperLabel);

         J9::Power::JNILinkage *jniLinkage = (J9::Power::JNILinkage*) cg->getLinkage(TR_J9JNILinkage);
         const TR::PPCLinkageProperties &pp = jniLinkage->getProperties();

         int32_t elementSize;
         if (comp->useCompressedPointers())
            elementSize = TR::Compiler->om.sizeofReferenceField();
         else
            elementSize = (int32_t) TR::Compiler->om.sizeofReferenceAddress();

         // Sign extend non-64bit Integers on LinuxPPC64 as required by the ABI
         if (comp->target().isLinux() && comp->target().is64Bit())
            {
            generateTrg1Src1Instruction(cg, TR::InstOpCode::extsw, node, lengthReg, lengthReg);
            }

         // The C routine expects length measured by slots.
         generateShiftRightLogicalImmediate(cg, node, lengthReg, lengthReg, trailingZeroes(elementSize));

         generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r3Reg, metaReg);

         TR::RegisterDependencyConditions *helperDeps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 0, cg->trMemory());
         TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_referenceArrayCopy);

         gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), helperDeps, helperSym);
         gcPoint->PPCNeedsGCMap(pp.getPreservedRegisterMapForGC());

         generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, deps);

         TR::TreeEvaluator::genWrtbarForArrayCopy(node, srcObjReg, dstObjReg, cg);

         TR::Register *retRegisters[5];
         int retRegCount = 0;
         cg->decReferenceCount(srcObjNode);
         cg->decReferenceCount(dstObjNode);
         cg->decReferenceCount(srcAddrNode);
         cg->decReferenceCount(dstAddrNode);
         cg->decReferenceCount(lengthNode);

         // Don't kill the registers that should not be clobbered
         if (!stopUsingCopyReg1)
            retRegisters[retRegCount++] = srcObjReg;
         if (!stopUsingCopyReg2)
            retRegisters[retRegCount++] = dstObjReg;
         if (!stopUsingCopyReg3)
            retRegisters[retRegCount++] = srcAddrReg;
         if (!stopUsingCopyReg4)
            retRegisters[retRegCount++] = dstAddrReg;
         if (!stopUsingCopyReg5)
            retRegisters[retRegCount++] = lengthReg;

         deps->stopUsingDepRegs(cg, retRegCount, retRegisters);
         cg->machine()->setLinkRegisterKilled(true);
         cg->setHasCall();

         return NULL;
         }
      }

   /*
    * This path also generates code to perform a runtime check on whether concurrent GC is done moving objects or not.
    * If it isn't done, once again a call to referenceArrayCopy helper should be made.
    * If it is done, using the assembly helpers code path is okay.
    */

   TR::Register *condReg = cg->allocateRegister(TR_CCR);

   // These registers are used when taking the assembly helpers path
   TR::Register *tmp1Reg = cg->allocateRegister(TR_GPR);
   TR::Register *tmp2Reg = cg->allocateRegister(TR_GPR);
   TR::Register *tmp3Reg = cg->allocateRegister(TR_GPR);
   TR::Register *tmp4Reg = cg->allocateRegister(TR_GPR);
   TR::Register *fp1Reg = NULL;
   TR::Register *fp2Reg = NULL;
   TR::Register *vec0Reg = NULL;
   TR::Register *vec1Reg = NULL;

   // These registers are used when taking the referenceArrayCopy helper call path.
   TR::Register *r3Reg = cg->allocateRegister();
   TR::Register *r4Reg = cg->allocateRegister();
   TR::Register *metaReg = cg->getMethodMetaDataRegister();

   // This section calculates the number of dependencies needed by the assembly helpers path.
   bool postP10Copy = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P10) &&
                      cg->comp()->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX);

   static bool disableVSXArrayCopy = (feGetEnv("TR_disableVSXArrayCopy") != NULL);
   bool useVSXForCopy  = cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P8) &&
                         !disableVSXArrayCopy && cg->comp()->target().cpu.supportsFeature(OMR_FEATURE_PPC_HAS_VSX);

   // VSX supercedes FPU.  No reason to offering disable option on this.
   //    POWER8 potentially micro-coded unaligned integer accesses in LE mode,
   //    breaking the guarantee of data atomicity. So, we use floating point
   //    accesses instead.
   bool extraLERequirement  = cg->comp()->target().cpu.isLittleEndian();

#if defined(DEBUG) || defined(PROD_WITH_ASSUMES)
   static bool verboseArrayCopy = (feGetEnv("TR_verboseArrayCopy") != NULL);       //Check which helper is getting used.
   if (verboseArrayCopy)
      fprintf(stderr, "arraycopy [0x%p] isReferenceArrayCopy:[%d] isForwardArrayCopy:[%d] isHalfWordElementArrayCopy:[%d] isWordElementArrayCopy:[%d] %s @ %s\n",
               node,
               0,
               node->isForwardArrayCopy(),
               node->isHalfWordElementArrayCopy(),
               node->isWordElementArrayCopy(),
               comp->signature(),
               comp->getHotnessName(comp->getMethodHotness())
               );
#endif

   /*
    * The minimum number of dependencies used by the assembly helpers path is 8.
    * The number of dependencies added by the referenceArrayCopy helper call path is 5.
    */
   int32_t numDeps = 8 + 5;

   if (postP10Copy)
      {
      // Due to the potential ReferenceArrayCopy call, our hands are tied somewhat.
      // Strictly for arrayCopy call, we don't need that many registers at all.

      numDeps += 4;
      }
   else if (useVSXForCopy)
      {
      vec0Reg = cg->allocateRegister(TR_VRF);
      vec1Reg = cg->allocateRegister(TR_VRF);
      numDeps += 2;
      if (comp->target().is32Bit())
         {
         numDeps += 1;
         }
      if (extraLERequirement)
         {
         fp1Reg = cg->allocateSinglePrecisionRegister();
         fp2Reg = cg->allocateSinglePrecisionRegister();
         numDeps += 4;
         }
      }
   else if (comp->target().is32Bit())
      {
      numDeps += 1;
      if (comp->target().cpu.hasFPU())
         {
         numDeps += 4;
         }
      }
   else if (comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
      {
      numDeps += 4;
      }

   TR::RegisterDependencyConditions *deps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(numDeps, numDeps, cg->trMemory());

   /*
    * Build up the dependency conditions for assembly helper path.
    * Unfortunately, the two different paths have a conflict regarding which real register they want srcAddrReg, dstAddrReg and lengthReg in.
    * Dependencies are set up to favour the fast assembly path. Register moves are used in the slow helper path to move the values to the
    * real registers they are expected to be in.
    */
   TR::addDependency(deps, condReg, TR::RealRegister::cr0, TR_CCR, cg);

   TR::addDependency(deps, lengthReg, TR::RealRegister::gr7, TR_GPR, cg);
   TR::addDependency(deps, srcAddrReg, TR::RealRegister::gr8, TR_GPR, cg);
   TR::addDependency(deps, dstAddrReg, TR::RealRegister::gr9, TR_GPR, cg);

   TR::addDependency(deps, tmp1Reg, TR::RealRegister::gr5, TR_GPR, cg);
   TR::addDependency(deps, tmp2Reg, TR::RealRegister::gr6, TR_GPR, cg);
   TR::addDependency(deps, tmp3Reg, TR::RealRegister::gr0, TR_GPR, cg);
   TR::addDependency(deps, tmp4Reg, TR::RealRegister::gr11, TR_GPR, cg); // Trampoline kills gr11.

   if (postP10Copy)
      {
      TR::addDependency(deps, NULL, TR::RealRegister::vsr8, TR_VSX_VECTOR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::vsr9, TR_VSX_VECTOR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::vsr32, TR_VSX_VECTOR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::vsr33, TR_VSX_VECTOR, cg);
      }
   else if (useVSXForCopy)
      {
      TR::addDependency(deps, vec0Reg, TR::RealRegister::vr0, TR_VRF, cg);
      TR::addDependency(deps, vec1Reg, TR::RealRegister::vr1, TR_VRF, cg);
      if (comp->target().is32Bit())
         {
         TR::addDependency(deps, NULL, TR::RealRegister::gr10, TR_GPR, cg);
         }
      if (extraLERequirement)
         {
         TR::addDependency(deps, fp1Reg, TR::RealRegister::fp8, TR_FPR, cg);
         TR::addDependency(deps, fp2Reg, TR::RealRegister::fp9, TR_FPR, cg);
         TR::addDependency(deps, NULL, TR::RealRegister::fp10, TR_FPR, cg);
         TR::addDependency(deps, NULL, TR::RealRegister::fp11, TR_FPR, cg);
         }
      }
   else if (comp->target().is32Bit())
      {
      TR::addDependency(deps, NULL, TR::RealRegister::gr10, TR_GPR, cg);
      if (comp->target().cpu.hasFPU())
         {
         TR::addDependency(deps, NULL, TR::RealRegister::fp8, TR_FPR, cg);
         TR::addDependency(deps, NULL, TR::RealRegister::fp9, TR_FPR, cg);
         TR::addDependency(deps, NULL, TR::RealRegister::fp10, TR_FPR, cg);
         TR::addDependency(deps, NULL, TR::RealRegister::fp11, TR_FPR, cg);
         }
      }
   else if (comp->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
      {
      // stfdp arrayCopy used
      TR::addDependency(deps, NULL, TR::RealRegister::fp8, TR_FPR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::fp9, TR_FPR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::fp10, TR_FPR, cg);
      TR::addDependency(deps, NULL, TR::RealRegister::fp11, TR_FPR, cg);
      }

   // Add dependencies for the referenceArrayCopy helper call path.
   TR::addDependency(deps, r3Reg, TR::RealRegister::gr3, TR_GPR, cg);
   TR::addDependency(deps, r4Reg, TR::RealRegister::gr4, TR_GPR, cg);

   TR::addDependency(deps, srcObjReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(deps, dstObjReg, TR::RealRegister::NoReg, TR_GPR, cg);
   TR::addDependency(deps, metaReg, TR::RealRegister::NoReg, TR_GPR, cg);

   TR::LabelSymbol *startLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *helperLabel = generateLabelSymbol(cg);
   TR::LabelSymbol *endLabel = generateLabelSymbol(cg);
   startLabel->setStartInternalControlFlow();
   endLabel->setEndInternalControlFlow();

   generateLabelInstruction(cg, TR::InstOpCode::label, node, startLabel);

   // Runtime check for concurrent scavenger.
   generateTrg1MemInstruction(cg, TR::InstOpCode::Op_load, node, tmp1Reg,
         TR::MemoryReference::createWithDisplacement(cg, metaReg, offsetof(J9VMThread, readBarrierRangeCheckTop), TR::Compiler->om.sizeofReferenceAddress()));
   generateTrg1Src1ImmInstruction(cg, TR::InstOpCode::Op_cmpli, node, condReg, tmp1Reg, 0);
   generateConditionalBranchInstruction(cg, TR::InstOpCode::bne, node, helperLabel, condReg);

   // Start of assembly helper path.
   TR_RuntimeHelper helper;

   if (node->isForwardArrayCopy())
      {
      if (postP10Copy)
         {
         helper = TR_PPCpostP10ForwardCopy;
         }
      else if (useVSXForCopy)
         {
         helper = TR_PPCforwardQuadWordArrayCopy_vsx;
         }
      else if (node->isWordElementArrayCopy())
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPCforwardWordArrayCopy_dp;
         else
            helper = TR_PPCforwardWordArrayCopy;
         }
      else if (node->isHalfWordElementArrayCopy())
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPCforwardHalfWordArrayCopy_dp;
         else
            helper = TR_PPCforwardHalfWordArrayCopy;
         }
      else
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPCforwardArrayCopy_dp;
         else
            helper = TR_PPCforwardArrayCopy;
         }
      }
   else // We are not sure it is forward or we have to do backward.
      {
      if (postP10Copy)
         {
         helper = TR_PPCpostP10GenericCopy;
         }
      else if (useVSXForCopy)
         {
         helper = TR_PPCquadWordArrayCopy_vsx;
         }
      else if (node->isWordElementArrayCopy())
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPCwordArrayCopy_dp;
         else
            helper = TR_PPCwordArrayCopy;
         }
      else if (node->isHalfWordElementArrayCopy())
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPChalfWordArrayCopy_dp;
         else
            helper = TR_PPChalfWordArrayCopy;
         }
      else
         {
         if (cg->comp()->target().cpu.isAtLeast(OMR_PROCESSOR_PPC_P6))
            helper = TR_PPCarrayCopy_dp;
         else
            helper = TR_PPCarrayCopy;
         }
      }
   TR::TreeEvaluator::generateHelperBranchAndLinkInstruction(helper, node, deps, cg);

   generateLabelInstruction(cg, TR::InstOpCode::b, node, endLabel);

   // Start of referenceArrayCopy helper path.
   generateLabelInstruction(cg, TR::InstOpCode::label, node, helperLabel);

   J9::Power::JNILinkage *jniLinkage = (J9::Power::JNILinkage*) cg->getLinkage(TR_J9JNILinkage);
   const TR::PPCLinkageProperties &pp = jniLinkage->getProperties();

   int32_t elementSize;
   if (comp->useCompressedPointers())
      elementSize = TR::Compiler->om.sizeofReferenceField();
   else
      elementSize = (int32_t) TR::Compiler->om.sizeofReferenceAddress();

   // Sign extend non-64bit Integers on LinuxPPC64 as required by the ABI
   if (comp->target().isLinux() && comp->target().is64Bit())
      {
      generateTrg1Src1Instruction(cg, TR::InstOpCode::extsw, node, lengthReg, lengthReg);
      }

   // The C routine expects length measured by slots.
   generateShiftRightLogicalImmediate(cg, node, lengthReg, lengthReg, trailingZeroes(elementSize));

   /*
    * Parameters are set up here
    * r3 = vmThread
    * r4 = srcObj
    * r5 = dstObj
    * r6 = srcAddr
    * r7 = dstAddr
    * r8 = length
    *
    * CAUTION: Virtual register names are based on their use during the non-helper path so they are misleading after this point.
    * Due to register reuse, pay attention to copying order so that a register is not clobbered too early.
    */
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r3Reg, metaReg);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, r4Reg, srcObjReg);
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, tmp1Reg, dstObjReg);    //tmp1Reg is tied to r5.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, tmp2Reg, srcAddrReg);   //tmp2Reg is tied to r6.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, srcAddrReg, lengthReg); //srcAddrReg is tied to r8. Need to copy srcAddrReg first.
   generateTrg1Src1Instruction(cg, TR::InstOpCode::mr, node, lengthReg, dstAddrReg); //lengthReg is tied to r7.  Need to copy lengthReg first.

   TR::RegisterDependencyConditions *helperDeps = new (cg->trHeapMemory()) TR::RegisterDependencyConditions(0, 0, cg->trMemory());
   TR::SymbolReference *helperSym = comp->getSymRefTab()->findOrCreateRuntimeHelper(TR_referenceArrayCopy);

   gcPoint = generateDepImmSymInstruction(cg, TR::InstOpCode::bl, node, (uintptr_t)helperSym->getMethodAddress(), helperDeps, helperSym);
   gcPoint->PPCNeedsGCMap(pp.getPreservedRegisterMapForGC());

   generateDepLabelInstruction(cg, TR::InstOpCode::label, node, endLabel, deps);

   TR::TreeEvaluator::genWrtbarForArrayCopy(node, srcObjReg, dstObjReg, cg);

   cg->decReferenceCount(srcObjNode);
   cg->decReferenceCount(dstObjNode);
   cg->decReferenceCount(srcAddrNode);
   cg->decReferenceCount(dstAddrNode);
   cg->decReferenceCount(lengthNode);

   TR::Register *retRegisters[5];
   int retRegCount = 0;

   // Don't kill the registers that should not be clobbered
   if (!stopUsingCopyReg1)
      retRegisters[retRegCount++] = srcObjReg;
   if (!stopUsingCopyReg2)
      retRegisters[retRegCount++] = dstObjReg;
   if (!stopUsingCopyReg3)
      retRegisters[retRegCount++] = srcAddrReg;
   if (!stopUsingCopyReg4)
      retRegisters[retRegCount++] = dstAddrReg;
   if (!stopUsingCopyReg5)
      retRegisters[retRegCount++] = lengthReg;

   deps->stopUsingDepRegs(cg, retRegCount, retRegisters);
   cg->machine()->setLinkRegisterKilled(true);
   cg->setHasCall();

   return NULL;
#else /* OMR_GC_CONCURRENT_SCAVENGER */
   return OMR::TreeEvaluatorConnector::arraycopyEvaluator(node, cg);
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
   }

TR::Register *
J9::Power::TreeEvaluator::NULLCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::evaluateNULLCHKWithPossibleResolve(node, false, cg);
   }

TR::Register *
J9::Power::TreeEvaluator::resolveAndNULLCHKEvaluator(TR::Node *node, TR::CodeGenerator *cg)
   {
   return TR::TreeEvaluator::evaluateNULLCHKWithPossibleResolve(node, true, cg);
   }

TR::Register *
J9::Power::TreeEvaluator::evaluateNULLCHKWithPossibleResolve(TR::Node *node, bool needsResolve, TR::CodeGenerator *cg)
   {
   // NOTE:
   // If in the future no code is generated for the null check, just evaluate the
   // child and decrement its use count UNLESS the child is a pass-through node
   // in which case some kind of explicit test or indirect load must be generated
   // to force the null check at this point.

   TR::Node     *firstChild     = node->getFirstChild();
   TR::ILOpCode &opCode = firstChild->getOpCode();
   TR::Node *reference = NULL;
   TR::Compilation *comp = cg->comp();

   bool hasCompressedPointers = false;
   if (comp->useCompressedPointers()
         && firstChild->getOpCodeValue() == TR::l2a)
      {
      hasCompressedPointers = true;
      TR::ILOpCodes loadOp = comp->il.opCodeForIndirectLoad(TR::Int32);
      TR::ILOpCodes rdbarOp = comp->il.opCodeForIndirectReadBarrier(TR::Int32);
      TR::Node *n = firstChild;
      while ((n->getOpCodeValue() != loadOp) && (n->getOpCodeValue() != rdbarOp))
         n = n->getFirstChild();
      reference = n->getFirstChild();
      }
   else
      reference = node->getNullCheckReference();

   // TODO - If a resolve check is needed as well, the resolve must be done
   // before the null check, so that exceptions are handled in the correct
   // order.
   //
   ///// if (needsResolve)
   /////    {
   /////    ...
   /////    }

   TR::Register *trgReg = cg->evaluate(reference);
   TR::Instruction *gcPoint;

   gcPoint = TR::TreeEvaluator::generateNullTestInstructions(cg, trgReg, node);

   gcPoint->PPCNeedsGCMap(0xFFFFFFFF);

   TR::Node *n = NULL;
   if (comp->useCompressedPointers()
         && reference->getOpCodeValue() == TR::l2a)
      {
      reference->setIsNonNull(true);
      n = reference->getFirstChild();
      TR::ILOpCodes loadOp = comp->il.opCodeForIndirectLoad(TR::Int32);
      TR::ILOpCodes rdbarOp = comp->il.opCodeForIndirectReadBarrier(TR::Int32);
      while ((n->getOpCodeValue() != loadOp) && (n->getOpCodeValue() != rdbarOp))
         {
         n->setIsNonZero(true);
         n = n->getFirstChild();
         }
      n->setIsNonZero(true);
      }

   reference->setIsNonNull(true);

   cg->evaluate(firstChild);
   cg->decReferenceCount(firstChild);

   return NULL;
   }

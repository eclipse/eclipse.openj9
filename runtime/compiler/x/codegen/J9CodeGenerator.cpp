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

#include "codegen/AheadOfTimeCompile.hpp"
#include "codegen/CodeGenerator.hpp"
#include "codegen/CodeGenerator_inlines.hpp"
#include "codegen/Machine.hpp"
#include "codegen/Linkage.hpp"
#include "codegen/Linkage_inlines.hpp"
#include "compile/Compilation.hpp"
#include "control/Recompilation.hpp"
#include "control/RecompilationInfo.hpp"
#include "env/CompilerEnv.hpp"
#include "env/VMJ9.h"
#include "env/jittypes.h"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "runtime/CodeCache.hpp"
#include "runtime/CodeCacheConfig.hpp"
#include "runtime/CodeCacheExceptions.hpp"
#include "runtime/CodeCacheManager.hpp"
#include "runtime/CodeCacheTypes.hpp"
#include "runtime/J9Runtime.hpp"
#include "x/codegen/CallSnippet.hpp"
#include "x/codegen/X86Recompilation.hpp"
#include "x/codegen/FPTreeEvaluator.hpp"
#include "x/codegen/X86Instruction.hpp"

extern void TEMPORARY_initJ9X86TreeEvaluatorTable(TR::CodeGenerator *cg);

J9::X86::CodeGenerator::CodeGenerator(TR::Compilation *comp) :
      J9::CodeGenerator(comp),
   _stackFramePaddingSizeInBytes(0)
   {
   /**
    * Do not add CodeGenerator initialization logic here.
    * Use the \c initialize() method instead.
    */
   }

void
J9::X86::CodeGenerator::initialize()
   {
   self()->J9::CodeGenerator::initialize();

   TR::CodeGenerator *cg = self();
   TR::Compilation *comp = cg->comp();
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(cg->fe());
   TR::ResolvedMethodSymbol *methodSymbol  = comp->getJittedMethodSymbol();
   TR_ResolvedMethod * jittedMethod = methodSymbol->getResolvedMethod();

   cg->setAheadOfTimeCompile(new (cg->trHeapMemory()) TR::AheadOfTimeCompile(cg));

   if (!TR::Compiler->om.canGenerateArraylets())
      {
      cg->setSupportsReferenceArrayCopy();
      }

   if (comp->requiresSpineChecks())
      {
      // Spine check code doesn't officially support codegen register rematerialization
      // yet.  Better spill placement interferes with tracking live spills.
      //
      cg->setUseNonLinearRegisterAssigner();
      cg->resetEnableRematerialisation();
      cg->resetEnableBetterSpillPlacements();
      }

   static char *disableMonitorCacheLookup = feGetEnv("TR_disableMonitorCacheLookup");
   if (!disableMonitorCacheLookup)
      {
      comp->setOption(TR_EnableMonitorCacheLookup);
      }

   cg->setSupportsPartialInlineOfMethodHooks();
   cg->setSupportsInliningOfTypeCoersionMethods();
   cg->setSupportsNewInstanceImplOpt();

   TR_ASSERT_FATAL(comp->compileRelocatableCode() || comp->isOutOfProcessCompilation() || comp->compilePortableCode() || comp->target().cpu.supportsFeature(OMR_FEATURE_X86_SSE4_1) == cg->getX86ProcessorInfo().supportsSSE4_1(), "supportsSSE4_1() failed\n");
   TR_ASSERT_FATAL(comp->compileRelocatableCode() || comp->isOutOfProcessCompilation() || comp->compilePortableCode() || comp->target().cpu.supportsFeature(OMR_FEATURE_X86_SSSE3) == cg->getX86ProcessorInfo().supportsSSSE3(), "supportsSSSE3() failed\n");

   if (comp->target().cpu.supportsFeature(OMR_FEATURE_X86_SSE4_1) &&
       !comp->getOption(TR_DisableSIMDStringCaseConv) &&
       !TR::Compiler->om.canGenerateArraylets())
      cg->setSupportsInlineStringCaseConversion();

   if (comp->target().cpu.supportsFeature(OMR_FEATURE_X86_SSSE3) &&
       !comp->getOption(TR_DisableFastStringIndexOf) &&
       !TR::Compiler->om.canGenerateArraylets())
      {
      cg->setSupportsInlineStringIndexOf();
      }

   if (comp->target().cpu.supportsFeature(OMR_FEATURE_X86_SSE4_1) &&
       !comp->getOption(TR_DisableSIMDStringHashCode) &&
       !TR::Compiler->om.canGenerateArraylets())
      {
      cg->setSupportsInlineStringHashCode();
      }

   if (comp->generateArraylets() && !comp->getOptions()->realTimeGC())
      {
      cg->setSupportsStackAllocationOfArraylets();
      }

   if (!comp->getOption(TR_FullSpeedDebug))
      cg->setSupportsDirectJNICalls();

   if (!comp->getOption(TR_DisableBDLLVersioning))
      {
      cg->setSupportsBigDecimalLongLookasideVersioning();
      cg->setSupportsBDLLHardwareOverflowCheck();
      }

   // Disable fast gencon barriers for AOT compiles because relocations on
   // the inlined heap addresses are not available (yet).
   //
   if (!fej9->supportsEmbeddedHeapBounds())
      {
      comp->setOption(TR_DisableWriteBarriersRangeCheck);
      }

   // Enable copy propagation of floats.
   //
   cg->setSupportsJavaFloatSemantics();

   /*
    * "Statically" initialize the FE-specific tree evaluator functions.
    * This code only needs to execute once per JIT lifetime.
    */
   static bool initTreeEvaluatorTable = false;
   if (!initTreeEvaluatorTable)
      {
      TEMPORARY_initJ9X86TreeEvaluatorTable(cg);
      initTreeEvaluatorTable = true;
      }

   // Set return type info here so that we always set it in case the return is optimized out
   TR_ReturnInfo returnInfo;
   switch (jittedMethod->returnType())
      {
      case TR::NoType:
         returnInfo = TR_VoidReturn;
         break;
      case TR::Int8:
      case TR::Int16:
      case TR::Int32:
         returnInfo = TR_IntReturn;
         break;
      case TR::Int64:
         returnInfo = TR_LongReturn;
         break;
      case TR::Address:
         returnInfo = comp->target().is64Bit() ? TR_ObjectReturn : TR_IntReturn;
         break;
      case TR::Float:
         returnInfo = TR_FloatXMMReturn;
         break;
      case TR::Double:
         returnInfo = TR_DoubleXMMReturn;
         break;
      }

   comp->setReturnInfo(returnInfo);
   }

TR::Recompilation *
J9::X86::CodeGenerator::allocateRecompilationInfo()
   {
   return TR_X86Recompilation::allocate(self()->comp());
   }

void
J9::X86::CodeGenerator::beginInstructionSelection()
   {
   TR::Compilation *comp = self()->comp();
   _returnTypeInfoInstruction = NULL;
   TR::ResolvedMethodSymbol *methodSymbol  = comp->getJittedMethodSymbol();
   TR::Recompilation *recompilation = comp->getRecompilationInfo();
   TR::Node *startNode = comp->getStartTree()->getNode();

   if (recompilation && recompilation->generatePrePrologue() != NULL)
      {
      // Return type info will have been generated by recompilation info
      //
      if (methodSymbol->getLinkageConvention() == TR_Private)
         _returnTypeInfoInstruction = (TR::X86ImmInstruction*)self()->getAppendInstruction();

      if (methodSymbol->getLinkageConvention() == TR_System)
         _returnTypeInfoInstruction = (TR::X86ImmInstruction*)self()->getAppendInstruction();
      }
   else if (comp->getOption(TR_FullSpeedDebug) || comp->getOption(TR_SupportSwitchToInterpreter))
      {
      int32_t alignmentMargin = comp->target().is64Bit()? 2 : 0; // # bytes between the alignment instruction and the address that needs to be aligned
      if (methodSymbol->getLinkageConvention() == TR_Private)
         alignmentMargin += 4; // The linkageInfo word

      // Make sure the startPC is at least 8-byte aligned.  The VM needs to be
      // able to low-tag the pointer, and also for code patching on IA32, this
      // is how we make sure the first instruction doesn't cross a patching boundary (see 175746).
      //
      int32_t alignmentBoundary = 8;

      TR::Instruction *cursor = self()->generateSwitchToInterpreterPrePrologue(NULL, alignmentBoundary, alignmentMargin);
      if (comp->target().is64Bit())
         {
         // A copy of the first two bytes of the method, in case we need to un-patch them
         //
         new (self()->trHeapMemory()) TR::X86ImmInstruction(cursor, TR::InstOpCode::DWImm2, 0xcccc, self());
         }
      }
   else if (methodSymbol->isJNI())
      {

      intptr_t methodAddress = (intptr_t)methodSymbol->getResolvedMethod()->startAddressForJNIMethod(comp);

      if (comp->target().is64Bit())
         new (self()->trHeapMemory()) TR::AMD64Imm64Instruction ((TR::Instruction *)NULL, TR::InstOpCode::DQImm64, methodAddress, self());
      else
         new (self()->trHeapMemory()) TR::X86ImmInstruction    ((TR::Instruction *)NULL, TR::InstOpCode::DDImm4, methodAddress, self());
      }

   if (methodSymbol->getLinkageConvention() == TR_Private && !_returnTypeInfoInstruction)
      {
      // linkageInfo word
      if (self()->getAppendInstruction())
         _returnTypeInfoInstruction = generateImmInstruction(TR::InstOpCode::DDImm4, startNode, 0, self());
      else
         _returnTypeInfoInstruction = new (self()->trHeapMemory()) TR::X86ImmInstruction((TR::Instruction *)NULL, TR::InstOpCode::DDImm4, 0, self());
      }

   if (methodSymbol->getLinkageConvention() == TR_System && !_returnTypeInfoInstruction)
      {
      // linkageInfo word
      if (self()->getAppendInstruction())
         _returnTypeInfoInstruction = generateImmInstruction(TR::InstOpCode::DDImm4, startNode, 0, self());
      else
         _returnTypeInfoInstruction = new (self()->trHeapMemory()) TR::X86ImmInstruction((TR::Instruction *)NULL, TR::InstOpCode::DDImm4, 0, self());
      }

   TR::RegisterDependencyConditions  *deps = generateRegisterDependencyConditions((uint8_t)0, (uint8_t)1, self());
   if (_linkageProperties->getMethodMetaDataRegister() != TR::RealRegister::NoReg)
      {
      deps->addPostCondition(self()->getVMThreadRegister(),
                             (TR::RealRegister::RegNum)self()->getVMThreadRegister()->getAssociation(), self());
      }
   deps->stopAddingPostConditions();

   if (self()->getAppendInstruction())
      generateInstruction(TR::InstOpCode::proc, startNode, deps, self());
   else
      new (self()->trHeapMemory()) TR::Instruction(deps, TR::InstOpCode::proc, (TR::Instruction *)NULL, self());

   // Set the default FPCW to single precision mode if we are allowed to.
   //
   if (self()->enableSinglePrecisionMethods() && comp->getJittedMethodSymbol()->usesSinglePrecisionMode())
      {
      auto cds = self()->findOrCreate2ByteConstant(startNode, SINGLE_PRECISION_ROUND_TO_NEAREST);
      generateMemInstruction(TR::InstOpCode::LDCWMem, startNode, generateX86MemoryReference(cds, self()), self());
      }
   }

void
J9::X86::CodeGenerator::endInstructionSelection()
   {
   TR::Compilation *comp = self()->comp();
   if (_returnTypeInfoInstruction != NULL)
      {
      TR_ReturnInfo returnInfo = comp->getReturnInfo();

      // Note: this will get clobbered again in code generation on AMD64
      _returnTypeInfoInstruction->setSourceImmediate(returnInfo);
      }

   // Reset the FPCW in the dummy finally block.
   //
   if (self()->enableSinglePrecisionMethods() &&
       comp->getJittedMethodSymbol()->usesSinglePrecisionMode())
      {
      TR_ASSERT(self()->getLastCatchAppendInstruction(),
             "endInstructionSelection() ==> Could not find the dummy finally block!\n");

      auto cds = self()->findOrCreate2ByteConstant(self()->getLastCatchAppendInstruction()->getNode(), DOUBLE_PRECISION_ROUND_TO_NEAREST);
      generateMemInstruction(self()->getLastCatchAppendInstruction(), TR::InstOpCode::LDCWMem, generateX86MemoryReference(cds, self()), self());
      }
   }

TR::Instruction *
J9::X86::CodeGenerator::generateSwitchToInterpreterPrePrologue(
      TR::Instruction *prev,
      uint8_t alignment,
      uint8_t alignmentMargin)
   {
   TR::Compilation *comp = self()->comp();
   TR::Register *ediRegister = self()->allocateRegister();
   TR::ResolvedMethodSymbol *methodSymbol = comp->getJittedMethodSymbol();
   intptr_t feMethod = (intptr_t)methodSymbol->getResolvedMethod()->resolvedMethodAddress();
   TR::LabelSymbol *startLabel = NULL;

   if (comp->target().is32Bit())
      {
      // Put the alignment before the interpreter jump so the jump's offset is fixed
      //
      alignmentMargin += 6; // TR::InstOpCode::MOV4RegImm4 below
      prev = generateAlignmentInstruction(prev, alignment, alignmentMargin, self());
      }

   startLabel = generateLabelSymbol(self());
   prev = generateLabelInstruction(prev, TR::InstOpCode::label, startLabel, self());
   self()->setSwitchToInterpreterLabel(startLabel);

   TR::RegisterDependencyConditions  *deps =
      generateRegisterDependencyConditions((uint8_t)1, (uint8_t)0, self());
   deps->addPreCondition(ediRegister, TR::RealRegister::edi, self());

   TR::SymbolReference *helperSymRef =
      self()->symRefTab()->findOrCreateRuntimeHelper(TR_j2iTransition);

   if (comp->target().is64Bit())
      {
      prev = generateRegImm64Instruction(prev, TR::InstOpCode::MOV8RegImm64, ediRegister, feMethod, self(), TR_RamMethod);
      if (comp->getOption(TR_EnableHCR))
         comp->getStaticHCRPICSites()->push_front(prev);
      prev = self()->getLinkage(methodSymbol->getLinkageConvention())->storeArguments(prev, methodSymbol);
      }
   else
      {
      prev = generateRegImmInstruction(prev, TR::InstOpCode::MOV4RegImm4, ediRegister, feMethod, self(), TR_RamMethod);
      if (comp->getOption(TR_EnableHCR))
         comp->getStaticHCRPICSites()->push_front(prev);
      }

   prev = new (self()->trHeapMemory()) TR::X86ImmSymInstruction(prev, TR::InstOpCode::JMP4, (uintptr_t)helperSymRef->getMethodAddress(), helperSymRef, deps, self());
   self()->stopUsingRegister(ediRegister);

   if (comp->target().is64Bit())
      {
      // Generate a mini-trampoline jump to the start of the
      // SwitchToInterpreterPrePrologue.  This comes after the alignment
      // instruction so we know where it will be relative to startPC.  Note
      // that it ought to be a TR::InstOpCode::JMP1 despite the fact that we're using a TR::InstOpCode::JMP4
      // opCode; otherwise, this instruction is not 2 bytes long, so it will
      // mess up alignment.
      //
      alignmentMargin += 2; // Size of the mini-trampoline
      prev = generateAlignmentInstruction(prev, alignment, alignmentMargin, self());
      prev = new (self()->trHeapMemory()) TR::X86LabelInstruction(prev, TR::InstOpCode::JMP4, startLabel, self());
      }

   return prev;
   }

bool
J9::X86::CodeGenerator::nopsAlsoProcessedByRelocations()
   {
   return self()->fej9()->nopsAlsoProcessedByRelocations();
   }


bool
J9::X86::CodeGenerator::enableAESInHardwareTransformations()
   {
   if (self()->comp()->target().cpu.supportsFeature(OMR_FEATURE_X86_AESNI) && !self()->comp()->getOption(TR_DisableAESInHardware) && !self()->comp()->getCurrentMethod()->isJNINative())
      return true;
   else
      return false;
   }

bool
J9::X86::CodeGenerator::suppressInliningOfRecognizedMethod(TR::RecognizedMethod method)
   {
   switch (method)
      {
      case TR::java_lang_Object_clone:
         return true;
      default:
         return false;
      }
   }

bool
J9::X86::CodeGenerator::supportsInliningOfIsAssignableFrom()
   {
   static const bool disableInliningOfIsAssignableFrom = feGetEnv("TR_disableInlineIsAssignableFrom") != NULL;
   return !disableInliningOfIsAssignableFrom;
   }

bool
J9::X86::CodeGenerator::canEmitBreakOnDFSet()
   {
   static const bool enableBreakOnDFSet = feGetEnv("TR_enableBreakOnDFSet") != NULL;
   return enableBreakOnDFSet;
   }

void
J9::X86::CodeGenerator::reserveNTrampolines(int32_t numTrampolines)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(self()->fe());
   TR::Compilation *comp = self()->comp();

   if (!TR::CodeCacheManager::instance()->codeCacheConfig().needsMethodTrampolines())
      return;

   bool hadClassUnloadMonitor;
   bool hadVMAccess = fej9->releaseClassUnloadMonitorAndAcquireVMaccessIfNeeded(comp, &hadClassUnloadMonitor);

   TR::CodeCache *curCache = self()->getCodeCache();
   TR::CodeCache *newCache = curCache;
   OMR::CodeCacheErrorCode::ErrorCode status = OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS;

   TR_ASSERT(curCache->isReserved(), "Current CodeCache is not reserved");

   if (!fej9->isAOT_DEPRECATED_DO_NOT_USE())
      {
      status = curCache->reserveSpaceForTrampoline_bridge(numTrampolines);
      if (status != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
         {
         // Current code cache is no good. Must unreserve
         curCache->unreserve();
         newCache = 0;
         if (self()->getCodeGeneratorPhase() != TR::CodeGenPhase::BinaryEncodingPhase)
            {
            newCache = TR::CodeCacheManager::instance()->getNewCodeCache(comp->getCompThreadID());
            if (newCache)
               {
               status = newCache->reserveSpaceForTrampoline_bridge(numTrampolines);

               if (status != OMR::CodeCacheErrorCode::ERRORCODE_SUCCESS)
                  {
                  TR_ASSERT(0, "Failed to reserve trampolines in fresh code cache.");
                  newCache->unreserve();
                  }
               }
            }
         }
      }

   fej9->acquireClassUnloadMonitorAndReleaseVMAccessIfNeeded(comp, hadVMAccess, hadClassUnloadMonitor);

   if (!newCache)
      {
      comp->failCompilation<TR::TrampolineError>("Failed to allocate code cache in reserveNTrampolines");
      }

   if (newCache != curCache)
      {
      // We keep track of number of IPIC trampolines that are present in the current code cache
      // If the code caches have been switched we have to reset this number, the setCodeCacheSwitched helper called
      // in switchCodeCacheTo resets the count
      // If we are in binaryEncoding we will kill this compilation anyway
      //
      self()->switchCodeCacheTo(newCache);
      }
   else
      {
      self()->setNumReservedIPICTrampolines(self()->getNumReservedIPICTrampolines() + numTrampolines);
      }

   TR_ASSERT(newCache->isReserved(), "New CodeCache is not reserved");
   }

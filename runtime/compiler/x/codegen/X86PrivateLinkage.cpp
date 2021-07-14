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

#include "codegen/X86PrivateLinkage.hpp"

#include "codegen/Linkage_inlines.hpp"
#include "codegen/LiveRegister.hpp"
#include "codegen/Machine.hpp"
#include "codegen/MemoryReference.hpp"
#include "codegen/Register.hpp"
#include "codegen/RegisterDependency.hpp"
#include "codegen/RegisterPair.hpp"
#include "codegen/Snippet.hpp"
#include "codegen/UnresolvedDataSnippet.hpp"
#include "compile/Method.hpp"
#include "compile/ResolvedMethod.hpp"
#include "compile/VirtualGuard.hpp"
#if defined(J9VM_OPT_JITSERVER)
#include "control/CompilationThread.hpp"
#endif /* defined(J9VM_OPT_JITSERVER) */
#include "env/CHTable.hpp"
#include "env/CompilerEnv.hpp"
#include "env/J2IThunk.hpp"
#include "env/PersistentCHTable.hpp"
#include "env/StackMemoryRegion.hpp"
#include "env/jittypes.h"
#include "env/VMJ9.h"
#include "il/DataTypes.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/ParameterSymbol.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "infra/SimpleRegex.hpp"
#include "env/VMJ9.h"
#include "x/codegen/X86Instruction.hpp"
#include "x/codegen/CallSnippet.hpp"
#include "x/codegen/FPTreeEvaluator.hpp"
#include "runtime/J9Profiler.hpp"
#include "runtime/J9ValueProfiler.hpp"
#include "OMR/Bytes.hpp"

#ifdef TR_TARGET_64BIT
#include "x/amd64/codegen/AMD64GuardedDevirtualSnippet.hpp"
#else
#include "x/codegen/GuardedDevirtualSnippet.hpp"
#endif

inline uint32_t gcd(uint32_t a, uint32_t b)
   {
   while (b != 0)
      {
      uint32_t t = b;
      b = a % b;
      a = t;
      }
   return a;
   }

inline uint32_t lcm(uint32_t a, uint32_t b)
   {
   return a * b / gcd(a, b);
   }

J9::X86::PrivateLinkage::PrivateLinkage(TR::CodeGenerator *cg) : J9::PrivateLinkage(cg)
   {
   // Stack alignment basic requirement:
   //    X86-32:  4 bytes, per hardware requirement
   //    X86-64: 16 bytes, required by both Linux and Windows
   // Stack alignment additional requirement:
   //    Stack alignment has to match the alignment requirement for local object address
   _properties.setOutgoingArgAlignment(lcm(cg->comp()->target().is32Bit() ? 4 : 16,
                                           cg->fej9()->getLocalObjectAlignmentInBytes()));
   }

const TR::X86LinkageProperties& J9::X86::PrivateLinkage::getProperties()
   {
   return _properties;
   }

////////////////////////////////////////////////
//
// Argument manipulation
//

static const TR::RealRegister::RegNum NOT_ASSIGNED = (TR::RealRegister::RegNum)-1;


void J9::X86::PrivateLinkage::copyLinkageInfoToParameterSymbols()
   {
   TR::ResolvedMethodSymbol              *bodySymbol = comp()->getJittedMethodSymbol();
   ListIterator<TR::ParameterSymbol>   paramIterator(&(bodySymbol->getParameterList()));
   TR::ParameterSymbol                *paramCursor;
   const TR::X86LinkageProperties     &properties = getProperties();
   int32_t                            maxIntArgs, maxFloatArgs;
   int32_t                            numIntArgs = 0, numFloatArgs = 0;

   maxIntArgs   = properties.getNumIntegerArgumentRegisters();
   maxFloatArgs = properties.getNumFloatArgumentRegisters();
   for (paramCursor = paramIterator.getFirst(); paramCursor != NULL; paramCursor = paramIterator.getNext())
      {
      // If we're out of registers, just stop now instead of looping doing nothing
      //
      if (numIntArgs >= maxIntArgs && numFloatArgs >= maxFloatArgs)
         break;

      // Assign linkage registers of each type until we run out
      //
      switch(paramCursor->getDataType())
         {
         case TR::Float:
         case TR::Double:
            if (numFloatArgs < maxFloatArgs)
               paramCursor->setLinkageRegisterIndex(numFloatArgs++);
            break;
         default:
            if (numIntArgs < maxIntArgs)
               paramCursor->setLinkageRegisterIndex(numIntArgs++);
            break;
         }
      }
   }

void J9::X86::PrivateLinkage::copyGlRegDepsToParameterSymbols(TR::Node *bbStart, TR::CodeGenerator *cg)
   {
   TR_ASSERT(bbStart->getOpCodeValue() == TR::BBStart, "assertion failure");
   if (bbStart->getNumChildren() > 0)
      {
      TR::Node *glRegDeps = bbStart->getFirstChild();
      if (!glRegDeps) // No global register info, so nothing to do
         return;

      TR_ASSERT(glRegDeps->getOpCodeValue() == TR::GlRegDeps, "First child of first Node must be a GlRegDeps");

      uint16_t childNum;
      for (childNum=0; childNum < glRegDeps->getNumChildren(); childNum++)
         {
         TR::Node *child = glRegDeps->getChild(childNum);
         TR::ParameterSymbol *sym = child->getSymbol()->getParmSymbol();
         sym->setAssignedGlobalRegisterIndex(cg->getGlobalRegister(child->getGlobalRegisterNumber()));
         }
      }
   }

TR::Instruction *J9::X86::PrivateLinkage::copyStackParametersToLinkageRegisters(TR::Instruction *procEntryInstruction)
   {
   TR_ASSERT(procEntryInstruction && procEntryInstruction->getOpCodeValue() == TR::InstOpCode::proc, "assertion failure");
   TR::Instruction *intrpPrev = procEntryInstruction->getPrev(); // The instruction before the interpreter entry point
   movLinkageRegisters(intrpPrev, false);
   return intrpPrev->getNext();
   }

TR::Instruction *J9::X86::PrivateLinkage::movLinkageRegisters(TR::Instruction *cursor, bool isStore)
   {
   TR_ASSERT(cursor, "assertion failure");

   TR::Machine *machine = cg()->machine();
   TR::RealRegister  *rspReal = machine->getRealRegister(TR::RealRegister::esp);

   TR::ResolvedMethodSymbol             *bodySymbol = comp()->getJittedMethodSymbol();
   ListIterator<TR::ParameterSymbol>  paramIterator(&(bodySymbol->getParameterList()));
   TR::ParameterSymbol               *paramCursor;

   // Copy from stack all parameters that belong in linkage regs
   //
   for (paramCursor = paramIterator.getFirst();
        paramCursor != NULL;
        paramCursor = paramIterator.getNext())
      {
      int8_t lri = paramCursor->getLinkageRegisterIndex();

      if (lri != NOT_LINKAGE) // This param should be in a linkage reg
         {
         TR_MovDataTypes movDataType = paramMovType(paramCursor);
         TR::RealRegister     *reg = machine->getRealRegister(getProperties().getArgumentRegister(lri, isFloat(movDataType)));
         TR::MemoryReference  *memRef = generateX86MemoryReference(rspReal, paramCursor->getParameterOffset(), cg());

         if (isStore)
            {
            // stack := lri
            cursor = generateMemRegInstruction(cursor, TR::Linkage::movOpcodes(MemReg, movDataType), memRef, reg, cg());
            }
         else
            {
            // lri := stack
            cursor = generateRegMemInstruction(cursor, TR::Linkage::movOpcodes(RegMem, movDataType), reg, memRef, cg());
            }
         }
      }

   return cursor;
   }


// Copies parameters from where they enter the method (either on stack or in a
// linkage register) to their "home location" where the method body will expect
// to find them (either on stack or in a global register).
//
TR::Instruction *J9::X86::PrivateLinkage::copyParametersToHomeLocation(TR::Instruction *cursor, bool parmsHaveBeenStored)
   {
   TR::Machine *machine = cg()->machine();
   TR::RealRegister  *framePointer = machine->getRealRegister(TR::RealRegister::vfp);

   TR::ResolvedMethodSymbol             *bodySymbol = comp()->getJittedMethodSymbol();
   ListIterator<TR::ParameterSymbol>  paramIterator(&(bodySymbol->getParameterList()));
   TR::ParameterSymbol               *paramCursor;

   const TR::RealRegister::RegNum noReg = TR::RealRegister::NoReg;
   TR_ASSERT(noReg == 0, "noReg must be zero so zero-initializing movStatus will work");

   TR::MovStatus movStatus[TR::RealRegister::NumRegisters] = {{(TR::RealRegister::RegNum)0,(TR::RealRegister::RegNum)0,(TR_MovDataTypes)0}};

   // We must always do the stores first, then the reg-reg copies, then the
   // loads, so that we never clobber a register we will need later.  However,
   // the logic is simpler if we do the loads and stores in the same loop.
   // Therefore, we maintain a separate instruction cursor for the loads.
   //
   // We defer the initialization of loadCursor until we generate the first
   // load.  Otherwise, if we happen to generate some stores first, then the
   // store cursor would get ahead of the loadCursor, and the instructions
   // would end up in the wrong order despite our efforts.
   //
   TR::Instruction *loadCursor = NULL;

   // Phase 1: generate RegMem and MemReg movs, and collect information about
   // the required RegReg movs.
   //
   for (paramCursor = paramIterator.getFirst();
       paramCursor != NULL;
       paramCursor = paramIterator.getNext())
      {
      int8_t lri = paramCursor->getLinkageRegisterIndex();     // How the parameter enters the method
      TR::RealRegister::RegNum ai                               // Where method body expects to find it
         = (TR::RealRegister::RegNum)paramCursor->getAssignedGlobalRegisterIndex();
      int32_t offset = paramCursor->getParameterOffset();      // Location of the parameter's stack slot
      TR_MovDataTypes movDataType = paramMovType(paramCursor); // What sort of MOV instruction does it need?

      // Copy the parameter to wherever it should be
      //
      if (lri == NOT_LINKAGE) // It's on the stack
         {
         if (ai == NOT_ASSIGNED) // It only needs to be on the stack
            {
            // Nothing to do
            }
         else // Method body expects it to be in the ai register
            {
            if (loadCursor == NULL)
               loadCursor = cursor;

            if (debug("traceCopyParametersToHomeLocation"))
               diagnostic("copyParametersToHomeLocation: Loading %d\n", ai);
            // ai := stack
            loadCursor = generateRegMemInstruction(
               loadCursor,
               TR::Linkage::movOpcodes(RegMem, movDataType),
               machine->getRealRegister(ai),
               generateX86MemoryReference(framePointer, offset, cg()),
               cg()
               );
            }
         }
      else // It's in a linkage register
         {
         TR::RealRegister::RegNum sourceIndex = getProperties().getArgumentRegister(lri, isFloat(movDataType));

         // Copy to the stack if necessary
         //
         if (ai == NOT_ASSIGNED || hasToBeOnStack(paramCursor))
            {
            if (parmsHaveBeenStored)
               {
               if (debug("traceCopyParametersToHomeLocation"))
                  diagnostic("copyParametersToHomeLocation: Skipping store of %d because parmsHaveBeenStored already\n", sourceIndex);
               }
            else
               {
               if (debug("traceCopyParametersToHomeLocation"))
                  diagnostic("copyParametersToHomeLocation: Storing %d\n", sourceIndex);
               // stack := lri
               cursor = generateMemRegInstruction(
                  cursor,
                  TR::Linkage::movOpcodes(MemReg, movDataType),
                  generateX86MemoryReference(framePointer, offset, cg()),
                  machine->getRealRegister(sourceIndex),
                  cg()
                  );
               }
            }

         // Copy to the ai register if necessary
         //
         if (ai != NOT_ASSIGNED && ai != sourceIndex)
            {
            // This parameter needs a RegReg move.  We don't know yet whether
            // we need the value in the target register, so for now we just
            // remember that we need to do this and keep going.
            //
            TR_ASSERT(movStatus[ai         ].sourceReg == noReg, "Each target reg must have only one source");
            TR_ASSERT(movStatus[sourceIndex].targetReg == noReg, "Each source reg must have only one target");
            if (debug("traceCopyParametersToHomeLocation"))
               diagnostic("copyParametersToHomeLocation: Planning to move %d to %d\n", sourceIndex, ai);
            movStatus[ai].sourceReg                  = sourceIndex;
            movStatus[sourceIndex].targetReg         = ai;
            movStatus[sourceIndex].outgoingDataType  = movDataType;
            }

         if (debug("traceCopyParametersToHomeLocation") && ai == sourceIndex)
            {
            diagnostic("copyParametersToHomeLocation: Parameter #%d already in register %d\n", lri, ai);
            }
         }
      }

   // Phase 2: Iterate through the parameters again to insert the RegReg moves.
   //
   for (paramCursor = paramIterator.getFirst();
       paramCursor != NULL;
       paramCursor = paramIterator.getNext())
      {
      if (paramCursor->getLinkageRegisterIndex() == NOT_LINKAGE)
         continue;

      const TR::RealRegister::RegNum paramReg =
         getProperties().getArgumentRegister(paramCursor->getLinkageRegisterIndex(), isFloat(paramMovType(paramCursor)));

      if (movStatus[paramReg].targetReg == 0)
         {
         // This parameter does not need to be copied anywhere
         if (debug("traceCopyParametersToHomeLocation"))
            diagnostic("copyParametersToHomeLocation: Not moving %d\n", paramReg);
         }
      else
         {
         if (debug("traceCopyParametersToHomeLocation"))
            diagnostic("copyParametersToHomeLocation: Preparing to move %d\n", paramReg);

         // If a mov's target register is the source for another mov, we need
         // to do that other mov first.  The idea is to find the end point of
         // the chain of movs starting with paramReg and ending with a
         // register whose current value is not needed; then do that chain of
         // movs in reverse order.
         //
         TR_ASSERT(noReg == 0, "noReg must be zero (not %d) for zero-filled initialization to work", noReg);

         TR::RealRegister::RegNum regCursor;

         // Find the last target in the chain
         //
         regCursor = movStatus[paramReg].targetReg;
         while(movStatus[regCursor].targetReg != noReg)
            {
            // Haven't found the end yet
            regCursor = movStatus[regCursor].targetReg;
            TR_ASSERT(regCursor != paramReg, "Can't yet handle cyclic dependencies");

            // TODO:AMD64 Use scratch register to break cycles
            //
            // A properly-written pickRegister should never
            // cause cycles to occur in the first place.  However, we may want
            // to consider adding cycle-breaking logic so that (1) pickRegister
            // has more flexibility, and (2) we're more robust against
            // otherwise harmless bugs in pickRegister.
            }

         // Work our way backward along the chain, generating all the necessary movs
         //
         while(movStatus[regCursor].sourceReg != noReg)
            {
            TR::RealRegister::RegNum source = movStatus[regCursor].sourceReg;
            if (debug("traceCopyParametersToHomeLocation"))
               diagnostic("copyParametersToHomeLocation: Moving %d to %d\n", source, regCursor);
            // regCursor := regCursor.sourceReg
            cursor = generateRegRegInstruction(
               cursor,
               TR::Linkage::movOpcodes(RegReg, movStatus[source].outgoingDataType),
               machine->getRealRegister(regCursor),
               machine->getRealRegister(source),
               cg()
               );
            // Update movStatus as we go so we don't generate redundant movs
            movStatus[regCursor].sourceReg = noReg;
            movStatus[source   ].targetReg = noReg;
            // Continue with the next register in the chain
            regCursor = source;
            }
         }
      }

   // Return the last instruction we inserted, whether or not it was a load.
   //
   return loadCursor? loadCursor : cursor;
   }

static TR::Instruction *initializeLocals(TR::Instruction      *cursor,
                                        int32_t              lowOffset,
                                        uint32_t             count,
                                        int32_t              pointerSize,
                                        TR::RealRegister  *framePointer,
                                        TR::RealRegister  *sourceReg,
                                        TR::RealRegister  *loopReg,
                                        TR::CodeGenerator *cg)
   {
   TR::Compilation *comp = cg->comp();
   int32_t offset = lowOffset;

   if (count <= 4)
      {
      // For a small number, just generate a sequence of stores.
      //
      for (int32_t i=0; i < count; i++, offset += pointerSize)
         {
         cursor = new (cg->trHeapMemory()) TR::X86MemRegInstruction(
            cursor,
            TR::InstOpCode::SMemReg(),
            generateX86MemoryReference(framePointer, offset, cg),
            sourceReg,
            cg);
         }
      }
   else
      {
      // For a large number, generate a loop.
      //
      // for (loopReg = count-1; loopReg >= 0; loopReg--)
      //    framePointer[offset + loopReg * pointerSize] = sourceReg;
      //
      TR_ASSERT(count > 0, "positive count required for dword RegImm instruction");

      cursor = new (cg->trHeapMemory()) TR::X86RegMemInstruction(
                  cursor,
                  TR::InstOpCode::LEARegMem(),
                  loopReg,
                  generateX86MemoryReference(sourceReg, count-1, cg),
                  cg);

      TR::LabelSymbol *loopLabel = generateLabelSymbol(cg);
      cursor = new (cg->trHeapMemory()) TR::X86LabelInstruction(cursor, TR::InstOpCode::label, loopLabel, cg);

      cursor = new (cg->trHeapMemory()) TR::X86MemRegInstruction(
         cursor,
         TR::InstOpCode::SMemReg(),
         generateX86MemoryReference(
            framePointer,
            loopReg,
            TR::MemoryReference::convertMultiplierToStride(pointerSize),
            offset,
            cg),
         sourceReg,
         cg);

      cursor = new (cg->trHeapMemory()) TR::X86RegImmInstruction(cursor, TR::InstOpCode::SUB4RegImms, loopReg, 1, cg);
      cursor = new (cg->trHeapMemory()) TR::X86LabelInstruction(cursor, TR::InstOpCode::JAE4, loopLabel, cg);
      }

   return cursor;
   }


#define STACKCHECKBUFFER 512

void J9::X86::PrivateLinkage::createPrologue(TR::Instruction *cursor)
   {
#if defined(DEBUG)
   // TODO:AMD64: Get this into the debug DLL

   class TR_DebugFrameSegmentInfo
      {
      private:

      TR_DebugFrameSegmentInfo *_next;
      const char *_description;
      TR::RealRegister *_register;
      int32_t _lowOffset;
      uint8_t _size;
      TR::Compilation * _comp;

      public:

      TR_ALLOC(TR_Memory::CodeGenerator)

      TR_DebugFrameSegmentInfo(
         TR::Compilation * c,
         int32_t lowOffset,
         uint8_t size,
         const char *description,
         TR_DebugFrameSegmentInfo *next,
         TR::RealRegister *reg=NULL
         ):
         _comp(c),
         _next(next),
         _description(description),
         _register(reg),
         _lowOffset(lowOffset),
         _size(size)
         {}

      TR::Compilation * comp() { return _comp; }

      TR_DebugFrameSegmentInfo *getNext(){ return _next; }

      TR_DebugFrameSegmentInfo *sort()
         {
         TR_DebugFrameSegmentInfo *result;
         TR_DebugFrameSegmentInfo *tail = _next? _next->sort() : NULL;
         TR_DebugFrameSegmentInfo *before=NULL, *after;
         for (after = tail; after; before=after, after=after->_next)
            {
            if (after->_lowOffset > _lowOffset)
               break;
            }
         _next = after;
         if (before)
            {
            before->_next = this;
            result = tail;
            }
         else
            {
            result = this;
            }
         return result;
         }

      void print(TR_Debug *debug)
         {
         if (_next)
            _next->print(debug);
         if (_size > 0)
            {
            diagnostic("        % 4d: % 4d -> % 4d (% 4d) %5.5s %s\n",
               _lowOffset, _lowOffset, _lowOffset + _size - 1, _size,
               _register? debug->getName(_register, TR_DoubleWordReg) : "",
               _description
               );
            }
         else
            {
            diagnostic("        % 4d: % 4d -> ---- (% 4d) %5.5s %s\n",
               _lowOffset, _lowOffset, _size,
               _register? debug->getName(_register, TR_DoubleWordReg) : "",
               _description
               );
            }
         }

      };

   TR_DebugFrameSegmentInfo *debugFrameSlotInfo=NULL;
#endif
   bool trace = comp()->getOption(TR_TraceCG);

   TR::RealRegister  *espReal      = machine()->getRealRegister(TR::RealRegister::esp);
   TR::RealRegister  *scratchReg   = machine()->getRealRegister(getProperties().getIntegerScratchRegister(0));
   TR::RealRegister  *metaDataReg  = machine()->getRealRegister(getProperties().getMethodMetaDataRegister());

   TR::ResolvedMethodSymbol             *bodySymbol = comp()->getJittedMethodSymbol();
   ListIterator<TR::ParameterSymbol>  paramIterator(&(bodySymbol->getParameterList()));
   TR::ParameterSymbol               *paramCursor;

   const TR::RealRegister::RegNum noReg = TR::RealRegister::NoReg;
   const TR::X86LinkageProperties &properties = getProperties();

   const uint32_t outgoingArgSize = cg()->getLargestOutgoingArgSize();

   // We will set this to zero after generating the first instruction (and thus
   // satisfying the size constraint).
   uint8_t minInstructionSize = getMinimumFirstInstructionSize();

   // Entry breakpoint
   //
   if (comp()->getOption(TR_EntryBreakPoints))
      {
      if (minInstructionSize > 0)
         {
         // We don't want the breakpoint to get patched, so generate a sacrificial no-op
         //
         cursor = new (trHeapMemory()) TR::X86PaddingInstruction(cursor, minInstructionSize, TR_AtomicNoOpPadding, cg());
         }
      cursor = new (trHeapMemory()) TR::Instruction(TR::InstOpCode::bad, cursor, cg());
      }

   // Compute the nature of the preserved regs
   //
   uint32_t preservedRegsSize = 0;
   uint32_t registerSaveDescription = 0; // bit N corresponds to real reg N, with 1=preserved

   // Preserved register index
   for (int32_t pindex = 0; pindex < properties.getMaxRegistersPreservedInPrologue(); pindex++)
      {
      TR::RealRegister *reg = machine()->getRealRegister(properties.getPreservedRegister((uint32_t)pindex));
      if (reg->getHasBeenAssignedInMethod() && reg->getState() != TR::RealRegister::Locked)
         {
         preservedRegsSize += properties.getPointerSize();
         registerSaveDescription |= reg->getRealRegisterMask();
         }
      }

   cg()->setRegisterSaveDescription(registerSaveDescription);

   // Compute frame size
   //
   // allocSize: bytes to be subtracted from the stack pointer when allocating the frame
   // peakSize:  maximum bytes of stack this method might consume before encountering another stack check
   //
   const int32_t localSize = _properties.getOffsetToFirstLocal() - bodySymbol->getLocalMappingCursor();
   TR_ASSERT(localSize >= 0, "assertion failure");

   // Note that the return address doesn't appear here because it is allocated by the call instruction
   //
      {
      int32_t frameSize = localSize + preservedRegsSize + ( _properties.getReservesOutgoingArgsInPrologue()? outgoingArgSize : 0 );
      uint32_t stackSize = frameSize + _properties.getRetAddressWidth();
      uint32_t adjust = OMR::align(stackSize, _properties.getOutgoingArgAlignment()) - stackSize;
      cg()->setStackFramePaddingSizeInBytes(adjust);
      cg()->setFrameSizeInBytes(frameSize + adjust);
      if (trace)
         traceMsg(comp(),
                  "Stack size was %d, and is adjusted by +%d (alignment %d, return address width %d)\n",
                  stackSize,
                  cg()->getStackFramePaddingSizeInBytes(),
                  _properties.getOutgoingArgAlignment(),
                  _properties.getRetAddressWidth());
      }
   auto allocSize = cg()->getFrameSizeInBytes();

   // Here we conservatively assume there is a call in this method that will require space for its return address
   const int32_t peakSize = allocSize + _properties.getPointerSize();

   bool doOverflowCheck = !comp()->isDLT();

   // Small: entire stack usage fits in STACKCHECKBUFFER, so if sp is within
   // the soft limit before buying the frame, then the whole frame will fit
   // within the hard limit.
   //
   // Medium: the additional stack required after bumping the sp fits in
   // STACKCHECKBUFFER, so if sp after the bump is within the soft limit, the
   // whole frame will fit within the hard limit.
   //
   // Large: No shortcuts.  Calculate the maximum extent of stack needed and
   // compare that against the soft limit.  (We have to use the soft limit here
   // if for no other reason than that's the one used for asyncchecks.)
   //
   const bool frameIsSmall  = peakSize < STACKCHECKBUFFER;
   const bool frameIsMedium = !frameIsSmall;

   if (trace)
      {
      traceMsg(comp(), "\nFrame size: %c%c locals=%d frame=%d peak=%d\n",
         frameIsSmall? 'S':'-', frameIsMedium? 'M':'-',
         localSize, cg()->getFrameSizeInBytes(), peakSize);
      }

#if defined(DEBUG)
   for (
      paramCursor = paramIterator.getFirst();
      paramCursor != NULL;
      paramCursor = paramIterator.getNext()
      ){
      TR::RealRegister::RegNum ai = (TR::RealRegister::RegNum)paramCursor->getAssignedGlobalRegisterIndex();
      debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
         paramCursor->getOffset(), paramCursor->getSize(), "Parameter",
         debugFrameSlotInfo,
         (ai==NOT_ASSIGNED)? NULL : machine()->getRealRegister(ai)
         );
      }

   ListIterator<TR::AutomaticSymbol>  autoIterator(&bodySymbol->getAutomaticList());
   TR::AutomaticSymbol               *autoCursor;
   for (
      autoCursor = autoIterator.getFirst();
      autoCursor != NULL;
      autoCursor = autoIterator.getNext()
      ){
      debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
         autoCursor->getOffset(), autoCursor->getSize(), "Local",
         debugFrameSlotInfo
         );
      }

   debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
      0, getProperties().getPointerSize(), "Return address",
      debugFrameSlotInfo
      );
#endif

   // Set the VFP state for the TR::InstOpCode::proc instruction
   //
   if (_properties.getAlwaysDedicateFramePointerRegister())
      {
      cg()->initializeVFPState(getProperties().getFramePointerRegister(), 0);
      }
   else
      {
      cg()->initializeVFPState(TR::RealRegister::esp, 0);
      }

   // In FSD, we must save linkage regs to the incoming argument area because
   // the stack overflow check doesn't preserve them.
   //
   bool parmsHaveBeenBackSpilled = false;
   if (comp()->getOption(TR_FullSpeedDebug))
      {
      cursor = movLinkageRegisters(cursor, true);
      parmsHaveBeenBackSpilled = true;
      }

   // Allocating the frame "speculatively" means bumping the stack pointer before checking for overflow
   //
   TR::GCStackAtlas *atlas = cg()->getStackAtlas();
   bool doAllocateFrameSpeculatively = false;
   if (metaDataReg)
      {
      // Generate stack overflow check
      doAllocateFrameSpeculatively = frameIsMedium;

      if (doAllocateFrameSpeculatively)
         {
         // Subtract allocSize from esp before stack overflow check

         TR_ASSERT(minInstructionSize <= 5, "Can't guarantee SUB instruction will be at least %d bytes", minInstructionSize);
         TR_ASSERT(allocSize >= 1, "When allocSize >= 1, the frame should be small or large, but never medium");

         const TR::InstOpCode::Mnemonic subOp = (allocSize <= 127 && getMinimumFirstInstructionSize() <= 3)? TR::InstOpCode::SUBRegImms() : TR::InstOpCode::SUBRegImm4();
         cursor = new (trHeapMemory()) TR::X86RegImmInstruction(cursor, subOp, espReal, allocSize, cg());

         minInstructionSize = 0; // The SUB satisfies the constraint
         }

      TR::Instruction* jitOverflowCheck = NULL;
      if (doOverflowCheck)
         {
         TR::X86VFPSaveInstruction* vfp = generateVFPSaveInstruction(cursor, cg());
         cursor = generateStackOverflowCheckInstruction(vfp, TR::InstOpCode::CMPRegMem(), espReal, generateX86MemoryReference(metaDataReg, cg()->getStackLimitOffset(), cg()), cg());

         TR::LabelSymbol* begLabel = generateLabelSymbol(cg());
         TR::LabelSymbol* endLabel = generateLabelSymbol(cg());
         TR::LabelSymbol* checkLabel = generateLabelSymbol(cg());
         begLabel->setStartInternalControlFlow();
         endLabel->setEndInternalControlFlow();
         checkLabel->setStartOfColdInstructionStream();

         cursor = generateLabelInstruction(cursor, TR::InstOpCode::label, begLabel, cg());
         cursor = generateLabelInstruction(cursor, TR::InstOpCode::JBE4, checkLabel, cg());
         cursor = generateLabelInstruction(cursor, TR::InstOpCode::label, endLabel, cg());

         // At this point, cg()->getAppendInstruction() is already in the cold code section.
         generateVFPRestoreInstruction(vfp, cursor->getNode(), cg());
         generateLabelInstruction(TR::InstOpCode::label, cursor->getNode(), checkLabel, cg());
         generateRegImmInstruction(TR::InstOpCode::MOV4RegImm4, cursor->getNode(), machine()->getRealRegister(TR::RealRegister::edi), allocSize, cg());
         if (doAllocateFrameSpeculatively)
            {
            generateRegImmInstruction(TR::InstOpCode::ADDRegImm4(), cursor->getNode(), espReal, allocSize, cg());
            }
         TR::SymbolReference* helper = comp()->getSymRefTab()->findOrCreateStackOverflowSymbolRef(NULL);
         jitOverflowCheck = generateImmSymInstruction(TR::InstOpCode::CALLImm4, cursor->getNode(), (uintptr_t)helper->getMethodAddress(), helper, cg());
         jitOverflowCheck->setNeedsGCMap(0xFF00FFFF);
         if (doAllocateFrameSpeculatively)
            {
            generateRegImmInstruction(TR::InstOpCode::SUBRegImm4(), cursor->getNode(), espReal, allocSize, cg());
            }
         generateLabelInstruction(TR::InstOpCode::JMP4, cursor->getNode(), endLabel, cg());
         }

      if (cg()->canEmitBreakOnDFSet())
         cursor = generateBreakOnDFSet(cg(), cursor);

      if (atlas)
         {
         uint32_t  numberOfParmSlots = atlas->getNumberOfParmSlotsMapped();
         TR_GCStackMap *map;
         if (_properties.getNumIntegerArgumentRegisters() == 0)
            {
            map = atlas->getParameterMap();
            }
         else
            {
            map = new (trHeapMemory(), numberOfParmSlots) TR_GCStackMap(numberOfParmSlots);
            map->copy(atlas->getParameterMap());

            // Before this point, the parameter stack considers all parms to be on the stack.
            // Fix it to have register parameters in registers.
            //
            TR::ParameterSymbol *paramCursor = paramIterator.getFirst();

            for (
               paramCursor = paramIterator.getFirst();
               paramCursor != NULL;
               paramCursor = paramIterator.getNext()
               ){
               int32_t  intRegArgIndex = paramCursor->getLinkageRegisterIndex();
               if (intRegArgIndex >= 0                   &&
                   paramCursor->isReferencedParameter()  &&
                   paramCursor->isCollectedReference())
                  {
                  // In FSD, the register parameters have already been backspilled.
                  // They exist in both registers and on the stack.
                  //
                  if (!parmsHaveBeenBackSpilled)
                     map->resetBit(paramCursor->getGCMapIndex());

                  map->setRegisterBits(TR::RealRegister::gprMask((getProperties().getIntegerArgumentRegister(intRegArgIndex))));
                  }
               }
            }

         if (jitOverflowCheck)
            jitOverflowCheck->setGCMap(map);

         atlas->setParameterMap(map);
         }
      }

   bodySymbol->setProloguePushSlots(preservedRegsSize / properties.getPointerSize());

   // Allocate the stack frame
   //
   if (allocSize == 0)
      {
      // No need to do anything
      }
   else if (!doAllocateFrameSpeculatively)
      {
      TR_ASSERT(minInstructionSize <= 5, "Can't guarantee SUB instruction will be at least %d bytes", minInstructionSize);
      const TR::InstOpCode::Mnemonic subOp = (allocSize <= 127 && getMinimumFirstInstructionSize() <= 3)? TR::InstOpCode::SUBRegImms() : TR::InstOpCode::SUBRegImm4();
      cursor = new (trHeapMemory()) TR::X86RegImmInstruction(cursor, subOp, espReal, allocSize, cg());
      }

   //Support to paint allocated frame slots.
   //
   if (( comp()->getOption(TR_PaintAllocatedFrameSlotsDead) || comp()->getOption(TR_PaintAllocatedFrameSlotsFauxObject) )  && allocSize!=0)
      {
      uint32_t paintValue32 = 0;
      uint64_t paintValue64 = 0;

      TR::RealRegister *paintReg = NULL;
      TR::RealRegister *frameSlotIndexReg = machine()->getRealRegister(TR::RealRegister::edi);
      uint32_t paintBound = 0;
      uint32_t paintSlotsOffset = 0;
      uint32_t paintSize = allocSize-sizeof(uintptr_t);

      //Paint the slots with deadf00d
      //
      if (comp()->getOption(TR_PaintAllocatedFrameSlotsDead))
         {
         if (comp()->target().is64Bit())
            paintValue64 = (uint64_t)CONSTANT64(0xdeadf00ddeadf00d);
         else
            paintValue32 = 0xdeadf00d;
         }
      //Paint stack slots with a arbitrary object aligned address.
      //
      else
         {
         if (comp()->target().is64Bit())
            {
            paintValue64 = ((uintptr_t) ((uintptr_t)comp()->getOptions()->getHeapBase() + (uintptr_t) 4096));
            }
         else
            {
            paintValue32 = ((uintptr_t) ((uintptr_t)comp()->getOptions()->getHeapBase() + (uintptr_t) 4096));
            }
         }

      TR::LabelSymbol   *startLabel = generateLabelSymbol(cg());

      //Load the 64 bit paint value into a paint reg.
#ifdef TR_TARGET_64BIT
       paintReg = machine()->getRealRegister(TR::RealRegister::r8);
       cursor = new (trHeapMemory()) TR::AMD64RegImm64Instruction(cursor, TR::InstOpCode::MOV8RegImm64, paintReg, paintValue64, cg());
#endif

      //Perform the paint.
      //
      cursor = new (trHeapMemory()) TR::X86RegImmInstruction(cursor, TR::InstOpCode::MOVRegImm4(), frameSlotIndexReg, paintSize, cg());
      cursor = new (trHeapMemory()) TR::X86LabelInstruction(cursor, TR::InstOpCode::label, startLabel, cg());
      if (comp()->target().is64Bit())
         cursor = new (trHeapMemory()) TR::X86MemRegInstruction(cursor, TR::InstOpCode::S8MemReg, generateX86MemoryReference(espReal, frameSlotIndexReg, 0,(uint8_t) paintSlotsOffset, cg()), paintReg, cg());
      else
         cursor = new (trHeapMemory()) TR::X86MemImmInstruction(cursor, TR::InstOpCode::SMemImm4(), generateX86MemoryReference(espReal, frameSlotIndexReg, 0,(uint8_t) paintSlotsOffset, cg()), paintValue32, cg());
      cursor = new (trHeapMemory()) TR::X86RegImmInstruction(cursor, TR::InstOpCode::SUBRegImms(), frameSlotIndexReg, sizeof(intptr_t),cg());
      cursor = new (trHeapMemory()) TR::X86RegImmInstruction(cursor, TR::InstOpCode::CMPRegImm4(), frameSlotIndexReg, paintBound, cg());
      cursor = new (trHeapMemory()) TR::X86LabelInstruction(cursor, TR::InstOpCode::JGE4, startLabel,cg());
      }

   // Save preserved regs
   //
   cursor = savePreservedRegisters(cursor);

   // Insert some counters
   //
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:#preserved", preservedRegsSize >> getProperties().getParmSlotShift(), TR::DebugCounter::Expensive);
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:inline", 1, TR::DebugCounter::Expensive);

   // Initialize any local pointers that could otherwise confuse the GC.
   //
   TR::RealRegister *framePointer = machine()->getRealRegister(TR::RealRegister::vfp);
   if (atlas)
      {
      TR_ASSERT(_properties.getNumScratchRegisters() >= 2, "Need two scratch registers to initialize reference locals");
      TR::RealRegister *loopReg = machine()->getRealRegister(properties.getIntegerScratchRegister(1));

      int32_t numReferenceLocalSlotsToInitialize = atlas->getNumberOfSlotsToBeInitialized();
      int32_t numInternalPointerSlotsToInitialize = 0;

      if (atlas->getInternalPointerMap())
         {
         numInternalPointerSlotsToInitialize = atlas->getNumberOfDistinctPinningArrays() +
                                               atlas->getInternalPointerMap()->getNumInternalPointers();
         }

      if (numReferenceLocalSlotsToInitialize > 0 || numInternalPointerSlotsToInitialize > 0)
         {
         cursor = new (trHeapMemory()) TR::X86RegRegInstruction(cursor, TR::InstOpCode::XORRegReg(), scratchReg, scratchReg, cg());

         // Initialize locals that are live on entry
         //
         if (numReferenceLocalSlotsToInitialize > 0)
            {
            cursor = initializeLocals(
               cursor,
               atlas->getLocalBaseOffset(),
               numReferenceLocalSlotsToInitialize,
               properties.getPointerSize(),
               framePointer, scratchReg, loopReg,
               cg());

#if defined(DEBUG)
            debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
               atlas->getLocalBaseOffset(),
               numReferenceLocalSlotsToInitialize * properties.getPointerSize(), "Initialized live vars",
               debugFrameSlotInfo);
#endif
            }

         // Initialize internal pointers and their pinning arrays
         //
         if (numInternalPointerSlotsToInitialize > 0)
            {
            cursor = initializeLocals(
               cursor,
               atlas->getOffsetOfFirstInternalPointer(),
               numInternalPointerSlotsToInitialize,
               properties.getPointerSize(),
               framePointer, scratchReg, loopReg,
               cg());

#if defined(DEBUG)
            debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
               atlas->getOffsetOfFirstInternalPointer(),
               numInternalPointerSlotsToInitialize * properties.getPointerSize(),
               "Initialized internal pointers",
               debugFrameSlotInfo);
#endif
            }
         }
      }

#if defined(DEBUG)
   debugFrameSlotInfo = new (trHeapMemory()) TR_DebugFrameSegmentInfo(comp(),
      -localSize - preservedRegsSize - outgoingArgSize,
      outgoingArgSize, "Outgoing args",
      debugFrameSlotInfo
      );
#endif

   // Move parameters to where the method body will expect to find them
   // TODO: If we separate the stores from the reg moves, we could do the stores
   // before buying the stack frame, thereby using tiny offsets and thus smaller
   // instructions.
   //
   cursor = copyParametersToHomeLocation(cursor, parmsHaveBeenBackSpilled);

   cursor = cg()->generateDebugCounter(cursor, "cg.prologues",             1,         TR::DebugCounter::Expensive);
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:#allocBytes", allocSize, TR::DebugCounter::Expensive);
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:#localBytes", localSize, TR::DebugCounter::Expensive);
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:#frameBytes", cg()->getFrameSizeInBytes(), TR::DebugCounter::Expensive);
   cursor = cg()->generateDebugCounter(cursor, "cg.prologues:#peakBytes",  peakSize,  TR::DebugCounter::Expensive);

#if defined(DEBUG)
   if (comp()->getOption(TR_TraceCG))
      {
      diagnostic("\nFrame layout:\n");
      diagnostic("        +rsp  +vfp     end  size        what\n");
      debugFrameSlotInfo->sort()->print(cg()->getDebug());
      diagnostic("\n");
      }
#endif
   }

bool J9::X86::PrivateLinkage::needsFrameDeallocation()
   {
   // frame needs a deallocation if FrameSize == 0
   //
   return !_properties.getAlwaysDedicateFramePointerRegister() && cg()->getFrameSizeInBytes() == 0;
   }

TR::Instruction *J9::X86::PrivateLinkage::deallocateFrameIfNeeded(TR::Instruction *cursor, int32_t size)
   {
   return cursor;
   }


void J9::X86::PrivateLinkage::createEpilogue(TR::Instruction *cursor)
   {
   if (cg()->canEmitBreakOnDFSet())
      cursor = generateBreakOnDFSet(cg(), cursor);

   TR::RealRegister* espReal = machine()->getRealRegister(TR::RealRegister::esp);

   cursor = cg()->generateDebugCounter(cursor, "cg.epilogues", 1, TR::DebugCounter::Expensive);

   // Restore preserved regs
   //
   cursor = restorePreservedRegisters(cursor);

   // Deallocate the stack frame
   //
   if (_properties.getAlwaysDedicateFramePointerRegister())
      {
      // Restore stack pointer from frame pointer
      //
      cursor = generateRegRegInstruction(cursor, TR::InstOpCode::MOVRegReg(), espReal, machine()->getRealRegister(_properties.getFramePointerRegister()), cg());
      cursor = generateRegInstruction(cursor, TR::InstOpCode::POPReg, machine()->getRealRegister(_properties.getFramePointerRegister()), cg());
      }
   else
      {
      auto frameSize = cg()->getFrameSizeInBytes();
      if (frameSize != 0)
         {
         cursor = generateRegImmInstruction(cursor, (frameSize <= 127) ? TR::InstOpCode::ADDRegImms() : TR::InstOpCode::ADDRegImm4(), espReal, frameSize, cg());
         }
      }

   if (cursor->getNext()->getOpCodeValue() == TR::InstOpCode::RETImm2)
      {
      toIA32ImmInstruction(cursor->getNext())->setSourceImmediate(comp()->getJittedMethodSymbol()->getNumParameterSlots() << getProperties().getParmSlotShift());
      }
   }

TR::Register *
J9::X86::PrivateLinkage::buildDirectDispatch(
      TR::Node *callNode,
      bool spillFPRegs)
   {
   TR::StackMemoryRegion stackMemoryRegion(*comp()->trMemory());

   TR::SymbolReference *methodSymRef = callNode->getSymbolReference();
   TR::MethodSymbol *methodSymbol = methodSymRef->getSymbol()->castToMethodSymbol();

   TR::X86CallSite site(callNode, this);

   // Add the int3 instruction if breakOnThrow is set on this user defined exception
   //
   TR::SimpleRegex *r = comp()->getOptions()->getBreakOnThrow();
   if (r && callNode && callNode->getOpCode().hasSymbolReference() &&
       comp()->getSymRefTab()->findOrCreateAThrowSymbolRef(comp()->getMethodSymbol())==callNode->getSymbolReference() &&
       callNode->getNumChildren()>=1 && callNode->getFirstChild()->getNumChildren()>=1 &&
       callNode->getFirstChild()->getFirstChild()->getOpCode().hasSymbolReference() &&
       callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getSymbol()->isStatic() &&
       callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getCPIndex() >= 0 &&
       callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getSymbol()->castToStaticSymbol()->isClassObject() &&
       !callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getSymbol()->castToStaticSymbol()->addressIsCPIndexOfStatic())
      {
      uint32_t len;
      TR_ResolvedMethod * method =
      callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getOwningMethod(comp());
      int32_t cpIndex = callNode->getFirstChild()->getFirstChild()->getSymbolReference()->getCPIndex();
      char * name = method->getClassNameFromConstantPool(cpIndex, len);
      if (name)
         {
         if (TR::SimpleRegex::matchIgnoringLocale(r, name))
            {
            generateInstruction(TR::InstOpCode::bad, callNode, cg());
            }
         }
      }

   // Build arguments and initially populate regdeps
   //
   buildCallArguments(site);

   // Remember where internal control flow region should start,
   // and create labels
   //
   TR::Instruction *startBookmark = cg()->getAppendInstruction();
   TR::LabelSymbol *startLabel = generateLabelSymbol(cg());
   TR::LabelSymbol *doneLabel = generateLabelSymbol(cg());
   startLabel->setStartInternalControlFlow();
   doneLabel->setEndInternalControlFlow();

   buildDirectCall(callNode->getSymbolReference(), site);

   // Construct postconditions
   //
   TR::Register *returnRegister = buildCallPostconditions(site);
   site.stopAddingConditions();

   // Create the internal control flow region and VFP adjustment
   //
   generateLabelInstruction(startBookmark, TR::InstOpCode::label, startLabel, site.getPreConditionsUnderConstruction(), cg());
   if (getProperties().getCallerCleanup())
      {
      // TODO: Caller must clean up
      }
   else if (callNode->getSymbol()->castToMethodSymbol()->isHelper() && getProperties().getUsesRegsForHelperArgs())
      {
      // No cleanup needed for helpers if args are passed in registers
      }
   else
      {
      generateVFPCallCleanupInstruction(-site.getArgSize(), callNode, cg());
      }
   generateLabelInstruction(TR::InstOpCode::label, callNode, doneLabel, site.getPostConditionsUnderConstruction(), cg());

   // Stop using the killed registers that are not going to persist
   //
   stopUsingKilledRegisters(site.getPostConditionsUnderConstruction(), returnRegister);

   if (callNode->getType().isFloatingPoint())
      {
      static char *forceX87LinkageForSSE = feGetEnv("TR_ForceX87LinkageForSSE");
      if (callNode->getReferenceCount() == 1 && returnRegister->getKind() == TR_X87)
         {
         // If the method returns a floating-point value that is not used, insert a
         // dummy store to eventually pop the value from the floating-point stack.
         //
         generateFPSTiST0RegRegInstruction(TR::InstOpCode::FSTRegReg, callNode, returnRegister, returnRegister, cg());
         }
      else if (forceX87LinkageForSSE && returnRegister->getKind() == TR_FPR)
         {
         // If the caller expects the return value in an XMMR, insert a
         // transfer from the floating-point stack to the XMMR via memory.
         //
         coerceFPReturnValueToXMMR(callNode, site.getPostConditionsUnderConstruction(), site.getMethodSymbol(), returnRegister);
         }
      }

   if (cg()->enableRegisterAssociations() && !callNode->getSymbol()->castToMethodSymbol()->preservesAllRegisters())
      associatePreservedRegisters(site.getPostConditionsUnderConstruction(), returnRegister);

   return returnRegister;
   }


TR::X86CallSite::X86CallSite(TR::Node *callNode, TR::Linkage *calleeLinkage)
   :_callNode(callNode)
   ,_linkage(calleeLinkage)
   ,_vftImplicitExceptionPoint(NULL)
   ,_firstPICSlotInstruction(NULL)
   ,_profiledTargets(NULL)
   ,_interfaceClassOfMethod(NULL)
   ,_argSize(-1)
   ,_preservedRegisterMask(0)
   ,_thunkAddress(NULL)
   ,_useLastITableCache(false)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   if (getMethodSymbol()->isInterface())
      {
      // Find the class pointer to the interface class if it is already loaded.
      // This is needed by both static PICs
      //
      TR::Method *interfaceMethod = getMethodSymbol()->getMethod();
      int32_t len = interfaceMethod->classNameLength();
      char * s = classNameToSignature(interfaceMethod->classNameChars(), len, comp());
      _interfaceClassOfMethod = fej9->getClassFromSignature(s, len, getSymbolReference()->getOwningMethod(comp()));
      }

   setupVirtualGuardInfo();
   computeProfiledTargets();

   // Initialize the register dependencies with conservative estimates of the
   // number of conditions
   //
   uint32_t numPreconditions =
        calleeLinkage->getProperties().getNumIntegerArgumentRegisters()
      + calleeLinkage->getProperties().getNumFloatArgumentRegisters()
      + 3; // VM Thread + eax + possible vtableIndex/J9Method arg on IA32

   uint32_t numPostconditions =
        calleeLinkage->getProperties().getNumberOfVolatileGPRegisters()
      + calleeLinkage->getProperties().getNumberOfVolatileXMMRegisters()
      + 3; // return reg + VM Thread + scratch

   _preConditionsUnderConstruction  = generateRegisterDependencyConditions(numPreconditions, 0, cg());
   _postConditionsUnderConstruction = generateRegisterDependencyConditions((COPY_PRECONDITIONS_TO_POSTCONDITIONS? numPreconditions : 0), numPostconditions + (COPY_PRECONDITIONS_TO_POSTCONDITIONS? numPreconditions : 0), cg());


   _preservedRegisterMask = getLinkage()->getProperties().getPreservedRegisterMapForGC();
   if (getMethodSymbol()->preservesAllRegisters())
      {
      _preservedRegisterMask |= TR::RealRegister::getAvailableRegistersMask(TR_GPR);
      if (callNode->getDataType() != TR::NoType)
         {
         // Cross our fingers and hope things that preserve all regs only return ints
         _preservedRegisterMask &= ~TR::RealRegister::gprMask(getLinkage()->getProperties().getIntegerReturnRegister());
         }
      }

   }

void TR::X86CallSite::setupVirtualGuardInfo()
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   _virtualGuardKind    = TR_NoGuard;
   _devirtualizedMethod = NULL;
   _devirtualizedMethodSymRef = NULL;

   if (getMethodSymbol()->isVirtual() && _callNode->getOpCode().isIndirect())
      {
      TR_ResolvedMethod *resolvedMethod = getResolvedMethod();
      if (resolvedMethod &&
          (!getMethodSymbol()->isVMInternalNative() || !comp()->getOption(TR_FullSpeedDebug)) &&
          !_callNode->isTheVirtualCallNodeForAGuardedInlinedCall())
         {
         if (!resolvedMethod->virtualMethodIsOverridden() &&
             !resolvedMethod->isAbstract())
            {
            _virtualGuardKind = TR_NonoverriddenGuard;
            _devirtualizedMethod = resolvedMethod;
            _devirtualizedMethodSymRef = getSymbolReference();
            }
          else
            {
            TR_OpaqueClassBlock *thisClass = resolvedMethod->containingClass();
            TR_DevirtualizedCallInfo *devirtualizedCallInfo = comp()->findDevirtualizedCall(_callNode);
            TR_OpaqueClassBlock *refinedThisClass = NULL;

            if (devirtualizedCallInfo)
               {
               refinedThisClass = devirtualizedCallInfo->_thisType;

               if (refinedThisClass)
                  thisClass = refinedThisClass;
               }

            TR::SymbolReference *methodSymRef = getSymbolReference();
            TR_PersistentCHTable * chTable = comp()->getPersistentInfo()->getPersistentCHTable();
            /* Devirtualization is not currently supported for AOT compilations */
            if (thisClass && TR::Compiler->cls.isAbstractClass(comp(), thisClass) && !comp()->compileRelocatableCode())
               {
               TR_ResolvedMethod * method = chTable->findSingleAbstractImplementer(thisClass, methodSymRef->getOffset(), methodSymRef->getOwningMethod(comp()), comp());
               if (method &&
                   (comp()->isRecursiveMethodTarget(method) ||
                    !method->isInterpreted() ||
                    method->isJITInternalNative()))
                  {
                  _virtualGuardKind = TR_AbstractGuard;
                  _devirtualizedMethod = method;
                  }
               }
            else if (refinedThisClass &&
                     !chTable->isOverriddenInThisHierarchy(resolvedMethod, refinedThisClass, methodSymRef->getOffset(), comp()))
               {
               if (resolvedMethod->virtualMethodIsOverridden())
                  {
                  TR_ResolvedMethod *calleeMethod = methodSymRef->getOwningMethod(comp())->getResolvedVirtualMethod(comp(), refinedThisClass, methodSymRef->getOffset());
                  if (calleeMethod &&
                      (comp()->isRecursiveMethodTarget(calleeMethod) ||
                       !calleeMethod->isInterpreted() ||
                       calleeMethod->isJITInternalNative()))
                     {
                     _virtualGuardKind = TR_HierarchyGuard;
                     _devirtualizedMethod = calleeMethod;
                     }
                  }
               }
            }

         if (_devirtualizedMethod != NULL && _devirtualizedMethodSymRef == NULL)
            _devirtualizedMethodSymRef = comp()->getSymRefTab()->findOrCreateMethodSymbol(
               getSymbolReference()->getOwningMethodIndex(), -1, _devirtualizedMethod, TR::MethodSymbol::Virtual);
         }
      }

   // Some self-consistency conditions
   TR_ASSERT((_virtualGuardKind == TR_NoGuard) == (_devirtualizedMethod == NULL), "Virtual guard requires _devirtualizedMethod");
   TR_ASSERT((_devirtualizedMethod == NULL) == (_devirtualizedMethodSymRef == NULL), "_devirtualizedMethod requires _devirtualizedMethodSymRef");
   }

void TR::X86CallSite::computeProfiledTargets()
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());

   if (cg()->profiledPointersRequireRelocation())
      // bail until create appropriate relocations to validate profiled targets
      return;

   // Use static PICs for guarded calls as well
   //

   _profiledTargets = new(comp()->trStackMemory()) TR_ScratchList<TR::X86PICSlot>(comp()->trMemory());

   TR::SymbolReference *methodSymRef = getSymbolReference();
   TR::Node            *callNode     = getCallNode();

   // TODO: Note the different logic for virtual and interface calls.  Is this necessary?
   //

   if (getMethodSymbol()->isVirtual() && !callNode->getSymbolReference()->isUnresolved() &&
       (callNode->getSymbolReference() != comp()->getSymRefTab()->findObjectNewInstanceImplSymbol()) &&
       callNode->getOpCode().isIndirect())
      {
      if (!comp()->getOption(TR_DisableInterpreterProfiling) &&
          TR_ValueProfileInfoManager::get(comp()))
         {
         TR::Node *callNode = getCallNode();
         TR_AddressInfo *valueInfo = static_cast<TR_AddressInfo*>(TR_ValueProfileInfoManager::getProfiledValueInfo(callNode, comp(), AddressInfo));

         // PMR 05447,379,000 getTopValue may return array length profile data instead of a class pointer
         // (when the virtual call feeds an arraycopy method length parameter). We need to defend this case to
         // avoid attempting to use the length as a pointer, so use asAddressInfo() to gate assignment of topValue.
         uintptr_t topValue = (valueInfo) ? valueInfo->getTopValue() : 0;

         // if the call to hashcode is a virtual call node, the top value was already inlined.
         if (callNode->isTheVirtualCallNodeForAGuardedInlinedCall())
            topValue = 0;

         // Is the topValue valid?
         if (topValue)
            {
            if (valueInfo->getTopProbability() < getMinProfiledCallFrequency() ||
                comp()->getPersistentInfo()->isObsoleteClass((void*)topValue, fej9))
               {
               topValue = 0;
               }
            // We can't do this guarded devirtualization if the profile data is not correct for this context. See defect 98813
            else
               {
               //printf("Checking is instanceof for top %p for %s\n", topValue, methodSymRef->getSymbol()->getResolvedMethodSymbol()->getResolvedMethod()->signature(comp()->trMemory())); fflush(stdout);
               TR_OpaqueClassBlock *callSiteMethod = methodSymRef->getSymbol()->getResolvedMethodSymbol()->getResolvedMethod()->classOfMethod();
               if (fej9->isInstanceOf((TR_OpaqueClassBlock *)topValue, callSiteMethod, true, true) != TR_yes)
                  {
                  topValue = 0;
                  }
               }
            }

         if (!topValue && !callNode->getSymbolReference()->isUnresolved() &&
             (callNode->getSymbol()->castToMethodSymbol()->getRecognizedMethod() == TR::java_lang_Object_clone))
            topValue = (uintptr_t) comp()->getObjectClassPointer();

         if (topValue)
            {

            TR_ResolvedMethod *profiledVirtualMethod = callNode->getSymbolReference()->getOwningMethod(comp())->getResolvedVirtualMethod(comp(),
               (TR_OpaqueClassBlock *)topValue, methodSymRef->getOffset());
            if (profiledVirtualMethod &&
                (!profiledVirtualMethod->isInterpreted() ||
                profiledVirtualMethod->isJITInternalNative()))
               {
               //if (!getMethodSymbol()->isInterface() && profiledVirtualMethod->isJITInternalNative())
               //printf("New opportunity in %s to callee %s\n", comp()->signature(), profiledVirtualMethod->signature(comp()->trMemory(), stackAlloc));
               //TR_ASSERT(profiledVirtualMethod->classOfMethod() == (TR_OpaqueClassBlock *)topValue, "assertion failure");

               TR_OpaqueMethodBlock *methodToBeCompared = NULL;
               int32_t slot = -1;
               if (profiledVirtualMethod->isJITInternalNative())
                  {
                  int32_t offset = callNode->getSymbolReference()->getOffset();
                  slot = fej9->virtualCallOffsetToVTableSlot(offset);
                  methodToBeCompared = profiledVirtualMethod->getPersistentIdentifier();
                  }

               _profiledTargets->add(new(comp()->trStackMemory()) TR::X86PICSlot((uintptr_t)topValue, profiledVirtualMethod, true, methodToBeCompared, slot));
               }
            }
         }
      }
   else if (getMethodSymbol()->isInterface())
      {
      bool staticPICsExist = false;
      int32_t numStaticPICSlots = 0;


      TR_AddressInfo *addressInfo = static_cast<TR_AddressInfo*>(TR_ValueProfileInfoManager::getProfiledValueInfo(callNode, comp(), AddressInfo));
#if defined(OSX)
      uint64_t topValue;
#else
      uintptr_t topValue;
#endif /* OSX */
      float missRatio = 0.0;
      if (addressInfo && addressInfo->getTopValue(topValue) > 0 && topValue && !comp()->getPersistentInfo()->isObsoleteClass((void*)topValue, fej9) &&
          addressInfo->getTopProbability() >= getMinProfiledCallFrequency())
         {
         uint32_t totalFrequency = addressInfo->getTotalFrequency();
         TR_ScratchList<TR_ExtraAddressInfo> valuesSortedByFrequency(comp()->trMemory());
         addressInfo->getSortedList(comp(), &valuesSortedByFrequency);

         static const char *p = feGetEnv("TR_TracePIC");
         if (p)
            {
            traceMsg(comp(), "Value profile info for callNode %p in %s\n", callNode, comp()->signature());
            addressInfo->getProfiler()->dumpInfo(comp()->getOutFile());
            traceMsg(comp(), "\n");
            }

         uintptr_t totalPICHitFrequency = 0;
         uintptr_t totalPICMissFrequency = 0;
         ListIterator<TR_ExtraAddressInfo> sortedValuesIt(&valuesSortedByFrequency);
         for (TR_ExtraAddressInfo *profiledInfo = sortedValuesIt.getFirst(); profiledInfo != NULL; profiledInfo = sortedValuesIt.getNext())
            {
            float frequency = ((float)profiledInfo->_frequency) / totalFrequency;
            if (comp()->getOption(TR_TraceCG))
               traceMsg(comp(), "  Profiled target frequency %f", frequency);

            TR_OpaqueClassBlock *thisType = (TR_OpaqueClassBlock *) profiledInfo->_value;
            TR_ResolvedMethod *profiledInterfaceMethod = NULL;
            TR::SymbolReference *methodSymRef = getSymbolReference();
            if (!comp()->getPersistentInfo()->isObsoleteClass((void *)thisType, fej9))
               {
               profiledInterfaceMethod = methodSymRef->getOwningMethod(comp())->getResolvedInterfaceMethod(comp(),
                  thisType, methodSymRef->getCPIndex());
               }
            if (profiledInterfaceMethod &&
                (!profiledInterfaceMethod->isInterpreted() ||
                 profiledInterfaceMethod->isJITInternalNative()))
               {
               if (frequency < getMinProfiledCallFrequency())
                  {
                  if (comp()->getOption(TR_TraceCG))
                     traceMsg(comp(), " - Too infrequent");
                  totalPICMissFrequency += profiledInfo->_frequency;
                  }
               else if (numStaticPICSlots >= comp()->getOptions()->getMaxStaticPICSlots(comp()->getMethodHotness()))
                  {
                  if (comp()->getOption(TR_TraceCG))
                     traceMsg(comp(), " - Already reached limit of %d static PIC slots", numStaticPICSlots);
                  totalPICMissFrequency += profiledInfo->_frequency;
                  }
               else
                  {
                  _profiledTargets->add(new(comp()->trStackMemory()) TR::X86PICSlot((uintptr_t)thisType, profiledInterfaceMethod));
                  if (comp()->getOption(TR_TraceCG))
                     traceMsg(comp(), " + Added static PIC slot");
                  numStaticPICSlots++;
                  totalPICHitFrequency += profiledInfo->_frequency;
                  }
               if (comp()->getOption(TR_TraceCG))
                  traceMsg(comp(), " for %s\n", profiledInterfaceMethod->signature(comp()->trMemory(), stackAlloc));
               }
            else
               {
               if (comp()->getOption(TR_TraceCG))
                  traceMsg(comp(), " * Can't find suitable method from profile info\n");
               }

            }
         missRatio = 1.0 * totalPICMissFrequency / totalFrequency;
         }

      _useLastITableCache = !comp()->getOption(TR_DisableLastITableCache) ? true : false;
      // Disable lastITable logic if all the implementers can fit into the pic slots during non-startup state
      if (_useLastITableCache && comp()->target().is64Bit() && _interfaceClassOfMethod && comp()->getPersistentInfo()->getJitState() != STARTUP_STATE)
         {
         J9::X86::PrivateLinkage *privateLinkage = static_cast<J9::X86::PrivateLinkage *>(getLinkage());
         int32_t numPICSlots = numStaticPICSlots + privateLinkage->IPicParameters.defaultNumberOfSlots;
         TR_ResolvedMethod **implArray = new (comp()->trStackMemory()) TR_ResolvedMethod*[numPICSlots+1];
         TR_PersistentCHTable * chTable = comp()->getPersistentInfo()->getPersistentCHTable();
         int32_t cpIndex = getSymbolReference()->getCPIndex();
         int32_t numImplementers = chTable->findnInterfaceImplementers(_interfaceClassOfMethod, numPICSlots+1, implArray, cpIndex, getSymbolReference()->getOwningMethod(comp()), comp());
         if (numImplementers <= numPICSlots)
            {
            _useLastITableCache = false;
            if (comp()->getOption(TR_TraceCG))
               traceMsg(comp(),"Found %d implementers for call to %s, can be fit into %d pic slots, disabling lastITable cache\n", numImplementers, getMethodSymbol()->getMethod()->signature(comp()->trMemory()), numPICSlots);
            }
         }
      else if (_useLastITableCache && comp()->target().is32Bit())  // Use the original heuristic for ia32 due to defect 111651
         {
         _useLastITableCache = false; // Default on ia32 is not to use the last itable cache
         static char *lastITableCacheThresholdStr = feGetEnv("TR_lastITableCacheThreshold");

         // With 4 static and 2 dynamic PIC slots, the cache starts to be used
         // for 7 equally-likely targets.  We want to catch that case, so the
         // threshold must be comfortably below 3/7 = 28%.
         //
         float lastITableCacheThreshold = lastITableCacheThresholdStr? atof(lastITableCacheThresholdStr) : 0.2;
         if (  missRatio >= lastITableCacheThreshold
            && performTransformation(comp(), "O^O PIC miss ratio is %f >= %f -- adding lastITable cache\n", missRatio, lastITableCacheThreshold))
            {
            _useLastITableCache = true;
            }
         }
      }

   if (_profiledTargets->isEmpty())
      _profiledTargets = NULL;
   }

bool TR::X86CallSite::shouldUseInterpreterLinkage()
   {
   if (getMethodSymbol()->isVirtual() &&
      !getSymbolReference()->isUnresolved() &&
      getMethodSymbol()->isVMInternalNative() &&
      !getResolvedMethod()->virtualMethodIsOverridden() &&
      !getResolvedMethod()->isAbstract())
      return true;
   else
      return false;
   }


TR::Register *TR::X86CallSite::evaluateVFT()
   {
   TR::Node *vftNode = getCallNode()->getFirstChild();
   if (vftNode->getRegister())
      return vftNode->getRegister();
   else
      {
      TR::Register *result = cg()->evaluate(vftNode);
      _vftImplicitExceptionPoint = cg()->getImplicitExceptionPoint();
      return result;
      }
   }

bool TR::X86CallSite::resolvedVirtualShouldUseVFTCall()
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   TR_ASSERT(getMethodSymbol()->isVirtual() && !getSymbolReference()->isUnresolved(), "assertion failure");

   return
      !fej9->forceUnresolvedDispatch() &&
      (!comp()->getOption(TR_EnableVPICForResolvedVirtualCalls)    ||
       getProfiledTargets()                                        ||
       getCallNode()->isTheVirtualCallNodeForAGuardedInlinedCall() ||
       ( comp()->getSymRefTab()->findObjectNewInstanceImplSymbol() &&
         comp()->getSymRefTab()->findObjectNewInstanceImplSymbol()->getSymbol() == getResolvedMethodSymbol()));
   }

void TR::X86CallSite::stopAddingConditions()
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   if (COPY_PRECONDITIONS_TO_POSTCONDITIONS)
      {
      TR::RegisterDependencyGroup *preconditions  = getPreConditionsUnderConstruction()->getPreConditions();
      TR::RegisterDependencyGroup *postconditions = getPostConditionsUnderConstruction()->getPostConditions();
      for (uint8_t i = 0; i < getPreConditionsUnderConstruction()->getAddCursorForPre(); i++)
         {
         TR::RegisterDependency *pre  = preconditions->getRegisterDependency(i);
         getPostConditionsUnderConstruction()->unionPreCondition(pre->getRegister(), pre->getRealRegister(), cg(), pre->getFlags());
         TR::RegisterDependency *post = postconditions->findDependency(pre->getRealRegister(), getPostConditionsUnderConstruction()->getAddCursorForPost());
         if (!post)
            getPostConditionsUnderConstruction()->addPostCondition(pre->getRegister(), pre->getRealRegister(), cg(), pre->getFlags());
         }
      }

   _preConditionsUnderConstruction->stopAddingPreConditions();
   _preConditionsUnderConstruction->stopAddingPostConditions();
   _postConditionsUnderConstruction->stopAddingPreConditions();
   _postConditionsUnderConstruction->stopAddingPostConditions();
   }

static void evaluateCommonedNodes(TR::Node *node, TR::CodeGenerator *cg)
   {
   // There is a rule that if a node with a symref is evaluated, it must be
   // evaluated in the first treetop under which it appears.  (The so-called
   // "prompt evaluation" rule).  Since we don't know what future trees will
   // do, this effectively means that any symref-bearing node that is commoned
   // with another treetop must be evaluated now.
   // We approximate this by saying that any node with a refcount >= 2 must be
   // evaluated now.  The "refcount >= 2" is a conservative approximation of
   // "commoned with another treetop" because the latter is not cheap to figure out.
   // "Any node" is an approximation of "any node with a symref"; we do that
   // because it allows us to use a simple linear-time tree walk without
   // resorting to visit counts.
   //
   TR::Compilation * comp= cg->comp();
   if (node->getRegister() == NULL)
      {
      if (node->getReferenceCount() >= 2)
         {
         if (comp->getOption(TR_TraceCG))
            traceMsg(comp, "Promptly evaluating commoned node %s\n", cg->getDebug()->getName(node));
         cg->evaluate(node);
         }
      else
         {
         for (int32_t i = 0; i < node->getNumChildren(); i++)
            evaluateCommonedNodes(node->getChild(i), cg);
         }
      }
   }


static bool indirectDispatchWillBuildVirtualGuard(TR::Compilation *comp, TR::X86CallSite *site)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp->fe());

   // This method is used in vft mask instruction removal in buildIndirectDispatch
   // if method will generate virtual call guard and build direct call, then skip vft mask instruction.
   if (site->getVirtualGuardKind() != TR_NoGuard && fej9->canDevirtualizeDispatch() )
      {
      if (comp->performVirtualGuardNOPing())
         {
         return true;
         }
      else if (site->getVirtualGuardKind() == TR_NonoverriddenGuard
         && !comp->getOption(TR_EnableHCR)
         && !comp->getOption(TR_MimicInterpreterFrameShape))
         {
         return true;
         }
      }
   return false;
   }

TR::Register *J9::X86::PrivateLinkage::buildIndirectDispatch(TR::Node *callNode)
   {
   TR::StackMemoryRegion stackMemoryRegion(*comp()->trMemory());

   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp()->fe());

   TR::X86CallSite site(callNode, this);

   // Build arguments and initially populate regdeps
   //
   buildCallArguments(site);

   // If receiver could be NULL, must evaluate it before the call
   // so any exception occurs before the call.
   // Might as well do it outside the internal control flow.
   //
   // Also evaluate the VFT if it survives the call.
   // The optimizer expects things to be evaluated in
   // the first tree in which they appear.
   //
   bool skipVFTmaskInstruction = false;
   if (callNode->getSymbol()->castToMethodSymbol()->firstArgumentIsReceiver())
      {
      TR::Node *rcvrChild = callNode->getChild(callNode->getFirstArgumentIndex());
      TR::Node  *vftChild = callNode->getFirstChild();
      bool loadVFTForNullCheck = false;

      if (cg()->getCurrentEvaluationTreeTop()->getNode()->getOpCodeValue() == TR::NULLCHK
         && vftChild->getOpCode().isLoadIndirect()
         && vftChild->getFirstChild() == cg()->getCurrentEvaluationTreeTop()->getNode()->getNullCheckReference()
         && vftChild->getFirstChild()->isNonNull() == false)
         loadVFTForNullCheck = true;

      bool willGenerateDirectCall = indirectDispatchWillBuildVirtualGuard(comp(), &site);
      static char *enableX86VFTLoadOpt = feGetEnv("TR_EnableX86VFTLoadOpt");

      if (enableX86VFTLoadOpt &&
          loadVFTForNullCheck &&
          willGenerateDirectCall &&
          vftChild->getReferenceCount() == 1 &&
          vftChild->getRegister() == NULL)
         {
         /*cg()->generateDebugCounter(
            TR::DebugCounter::debugCounterName(comp(), "cg.vftload/%s/(%s)/%d/%d", "skipmask",
                                    comp()->signature(),
                                    callNode->getByteCodeInfo().getCallerIndex(),
                                    callNode->getByteCodeInfo().getByteCodeIndex()));
         */
         TR::MemoryReference  *sourceMR = generateX86MemoryReference(vftChild, cg());
         TR::Register *reg = cg()->allocateRegister();
         // as vftChild->getOpCode().isLoadIndirect is true here, need set exception point
         TR::Instruction * instr = TR::TreeEvaluator::insertLoadMemory(vftChild, reg, sourceMR, TR_RematerializableAddress, cg());
         reg->setMemRef(sourceMR);
         cg()->setImplicitExceptionPoint(instr);
         site.setImplicitExceptionPoint(instr);
         cg()->stopUsingRegister(reg);
         skipVFTmaskInstruction = true;
         }
      else if (enableX86VFTLoadOpt &&
         loadVFTForNullCheck == false &&
         willGenerateDirectCall &&
         //vftChild->getReferenceCount() == 1 &&
         vftChild->getRegister() == NULL)
         {
         // skip evaluate vft mask load instruction
         // as it is not used in direct call
         //fprintf(stderr, "Skip load in %s\n", comp()->getMethodSymbol()->signature(comp()->trMemory()));
         skipVFTmaskInstruction = true;
         /*
         cg()->generateDebugCounter(
            TR::DebugCounter::debugCounterName(comp(), "cg.vftload/%s/(%s)/%d/%d", "skipvft",
                                    comp()->signature(),
                                    callNode->getByteCodeInfo().getCallerIndex(),
                                    callNode->getByteCodeInfo().getByteCodeIndex()));
                                    */
         }
      else if (rcvrChild->isNonNull() == false || callNode->getFirstChild()->getReferenceCount() > 1)
         {
         /*
         if (vftChild->getRegister() == NULL)
            {
            cg()->generateDebugCounter(
               TR::DebugCounter::debugCounterName(comp(), "cg.vftload/%s/(%s)/%d/%d", "loadvft",
                                    comp()->signature(),
                                    callNode->getByteCodeInfo().getCallerIndex(),
                                    callNode->getByteCodeInfo().getByteCodeIndex()));
            }*/
         site.evaluateVFT();
         }
      }

   // Children of the VFT expression may also survive the call.
   // (Note that the following is not sufficient for the VFT node
   // itself, which should use site.evaluateVFT instead.)
   //
   if (skipVFTmaskInstruction == false)
      evaluateCommonedNodes(callNode->getFirstChild(), cg());

   // Remember where internal control flow region should start,
   // and create labels
   //
   TR::Instruction *startBookmark = cg()->getAppendInstruction();
   TR::LabelSymbol *startLabel    = generateLabelSymbol(cg());
   TR::LabelSymbol *doneLabel     = generateLabelSymbol(cg());
   startLabel->setStartInternalControlFlow();
   doneLabel->setEndInternalControlFlow();

   // Allocate thunk if necessary
   //
   void *virtualThunk = NULL;
   if (getProperties().getNeedsThunksForIndirectCalls())
      {
      TR::MethodSymbol *methodSymbol = callNode->getSymbol()->castToMethodSymbol();
      TR::Method       *method       = methodSymbol->getMethod();
      if (methodSymbol->isComputed())
         {
         if (methodSymbol->isComputedStatic() && methodSymbol->getMandatoryRecognizedMethod() != TR::unknownMethod)
         switch (method->getMandatoryRecognizedMethod())
            {
            case TR::java_lang_invoke_ComputedCalls_dispatchVirtual:
               {
               // Need a j2i thunk for the method that will ultimately be dispatched by this handle call
               char *j2iSignature = fej9->getJ2IThunkSignatureForDispatchVirtual(methodSymbol->getMethod()->signatureChars(), methodSymbol->getMethod()->signatureLength(), comp());
               int32_t signatureLen = strlen(j2iSignature);
               virtualThunk = fej9->getJ2IThunk(j2iSignature, signatureLen, comp());
               if (!virtualThunk)
                  {
                  virtualThunk = fej9->setJ2IThunk(j2iSignature, signatureLen,
                     generateVirtualIndirectThunk(
                        fej9->getEquivalentVirtualCallNodeForDispatchVirtual(callNode, comp())), comp());
                  }
               }
               break;
            default:
               if (fej9->needsInvokeExactJ2IThunk(callNode, comp()))
                  {
                  TR_J2IThunk *thunk = generateInvokeExactJ2IThunk(callNode, methodSymbol->getMethod()->signatureChars());
                  fej9->setInvokeExactJ2IThunk(thunk, comp());
                  }
               break;
            }
         }
      else
         {
         virtualThunk = fej9->getJ2IThunk(methodSymbol->getMethod(), comp());
         if (!virtualThunk)
            virtualThunk = fej9->setJ2IThunk(methodSymbol->getMethod(), generateVirtualIndirectThunk(callNode), comp());
         }

      site.setThunkAddress((uint8_t *)virtualThunk);
      }

   TR::LabelSymbol *revirtualizeLabel  = generateLabelSymbol(cg());
   if (site.getVirtualGuardKind() != TR_NoGuard && fej9->canDevirtualizeDispatch() && buildVirtualGuard(site, revirtualizeLabel) )
      {
      buildDirectCall(site.getDevirtualizedMethodSymRef(), site);
      buildRevirtualizedCall(site, revirtualizeLabel, doneLabel);
      }
   else
      {
      // Build static PIC if profiling targets available.
      //
      TR_ASSERT(skipVFTmaskInstruction == false, "VFT mask instruction is skipped in early evaluation");

      TR::LabelSymbol *picMismatchLabel = NULL;
      TR_ScratchList<TR::X86PICSlot> *profiledTargets = site.getProfiledTargets();
      if (profiledTargets)
         {
         ListIterator<TR::X86PICSlot> i(profiledTargets);
         TR::X86PICSlot *picSlot = i.getFirst();
         while (picSlot)
            {
            picMismatchLabel = generateLabelSymbol(cg());

            if (comp()->target().is32Bit())
               picSlot->setNeedsPicCallAlignment();

            TR::Instruction *instr = buildPICSlot(*picSlot, picMismatchLabel, doneLabel, site);

            if (fej9->isUnloadAssumptionRequired((TR_OpaqueClassBlock *)picSlot->getClassAddress(), comp()->getCurrentMethod()) ||
                cg()->profiledPointersRequireRelocation())
               {
               if (picSlot->getMethodAddress())
                  comp()->getStaticMethodPICSites()->push_front(instr);
               else
                  comp()->getStaticPICSites()->push_front(instr);
               }

            picSlot = i.getNext();
            if (picSlot)
               generateLabelInstruction(TR::InstOpCode::label, site.getCallNode(), picMismatchLabel, cg());
            }

         site.setFirstPICSlotInstruction(NULL);
         }

      // Build the call
      //
      if (site.getMethodSymbol()->isVirtual() || site.getMethodSymbol()->isComputed())
         buildVirtualOrComputedCall(site, picMismatchLabel, doneLabel, (uint8_t *)virtualThunk);
      else
         buildInterfaceCall(site, picMismatchLabel, doneLabel, (uint8_t *)virtualThunk);
      }

   // Construct postconditions
   //
   TR::Node *vftChild = callNode->getFirstChild();
   TR::Register *vftRegister = vftChild->getRegister();
   TR::Register *returnRegister;
   if (vftChild->getRegister() && (vftChild->getReferenceCount() > 1))
      {
      // VFT child survives the call, so we must include it in the postconditions.
      returnRegister = buildCallPostconditions(site);
      if (vftChild->getRegister() && vftChild->getRegister()->getRegisterPair())
         {
         site.addPostCondition(vftChild->getRegister()->getRegisterPair()->getHighOrder(), TR::RealRegister::NoReg);
         site.addPostCondition(vftChild->getRegister()->getRegisterPair()->getLowOrder(), TR::RealRegister::NoReg);
         }
      else
         site.addPostCondition(vftChild->getRegister(), TR::RealRegister::NoReg);
      cg()->recursivelyDecReferenceCount(vftChild);
      }
   else
      {
      // VFT child dies here; decrement it early so it doesn't interfere with dummy regs.
      cg()->recursivelyDecReferenceCount(vftChild);
      returnRegister = buildCallPostconditions(site);
      }

   site.stopAddingConditions();

   // Create the internal control flow region and VFP adjustment
   //
   generateLabelInstruction(startBookmark, TR::InstOpCode::label, startLabel, site.getPreConditionsUnderConstruction(), cg());
   if (!getProperties().getCallerCleanup())
      generateVFPCallCleanupInstruction(-site.getArgSize(), callNode, cg());
   generateLabelInstruction(TR::InstOpCode::label, callNode, doneLabel, site.getPostConditionsUnderConstruction(), cg());

   // Stop using the killed registers that are not going to persist
   //
   stopUsingKilledRegisters(site.getPostConditionsUnderConstruction(), returnRegister);

   if (callNode->getType().isFloatingPoint())
      {
      static char *forceX87LinkageForSSE = feGetEnv("TR_ForceX87LinkageForSSE");
      if (callNode->getReferenceCount() == 1 && returnRegister->getKind() == TR_X87)
         {
         // If the method returns a floating-point value that is not used, insert a
         // dummy store to eventually pop the value from the floating-point stack.
         //
         generateFPSTiST0RegRegInstruction(TR::InstOpCode::FSTRegReg, callNode, returnRegister, returnRegister, cg());
         }
      else if (forceX87LinkageForSSE && returnRegister->getKind() == TR_FPR)
         {
         // If the caller expects the return value in an XMMR, insert a
         // transfer from the floating-point stack to the XMMR via memory.
         //
         coerceFPReturnValueToXMMR(callNode, site.getPostConditionsUnderConstruction(), site.getMethodSymbol(), returnRegister);
         }
      }

   if (cg()->enableRegisterAssociations())
      associatePreservedRegisters(site.getPostConditionsUnderConstruction(), returnRegister);

   cg()->setImplicitExceptionPoint(site.getImplicitExceptionPoint());

   return returnRegister;
   }

void J9::X86::PrivateLinkage::buildDirectCall(TR::SymbolReference *methodSymRef, TR::X86CallSite &site)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(comp()->fe());
   TR::MethodSymbol   *methodSymbol = methodSymRef->getSymbol()->castToMethodSymbol();
   TR::Instruction *callInstr    = NULL;
   TR::Node           *callNode     = site.getCallNode();
   TR_AtomicRegion   *callSiteAtomicRegions = TR::X86PatchableCodeAlignmentInstruction::CALLImm4AtomicRegions;

   if (comp()->target().is64Bit() && methodSymRef->getReferenceNumber()>=TR_AMD64numRuntimeHelpers)
      fej9->reserveTrampolineIfNecessary(comp(), methodSymRef, false);

#if defined(J9VM_OPT_JITSERVER)
   // JITServer Workaround: Further transmute dispatchJ9Method symbols to appear as a runtime helper, this will cause OMR to
   // generate a TR_HelperAddress relocation instead of a TR_RelativeMethodAddress Relocation.
   if (!comp()->getOption(TR_DisableInliningOfNatives) &&
       methodSymbol->getMandatoryRecognizedMethod() == TR::java_lang_invoke_ComputedCalls_dispatchJ9Method &&
       comp()->isOutOfProcessCompilation())
      {
      methodSymbol->setHelper();
      }
#endif /* defined(J9VM_OPT_JITSERVER) */

   if (cg()->supportVMInternalNatives() && methodSymbol->isVMInternalNative())
      {
      // Find the virtual register for edi
      // TODO: The register used should come from the linkage properties, rather than being hardcoded
      //
      TR::RealRegister::RegNum ramMethodRegisterIndex = TR::RealRegister::edi;
      TR::Register *ramMethodReg = cg()->allocateRegister();
      site.addPostCondition(ramMethodReg, TR::RealRegister::edi);

      // Load the RAM method into rdi and call the helper
      if (comp()->target().is64Bit())
         {
         generateRegImm64Instruction(TR::InstOpCode::MOV8RegImm64, callNode, ramMethodReg, (uint64_t)(uintptr_t)methodSymbol->getMethodAddress(), cg());
         }
      else
         {
         generateRegImmInstruction(TR::InstOpCode::MOV4RegImm4, callNode, ramMethodReg, (uint32_t)(uintptr_t)methodSymbol->getMethodAddress(), cg());
         }

      callInstr = generateHelperCallInstruction(callNode, TR_j2iTransition, NULL, cg());
      cg()->stopUsingRegister(ramMethodReg);
      }
   else if (comp()->target().is64Bit() && methodSymbol->isJITInternalNative())
      {
      // JIT callable natives on 64-bit may not be directly reachable.  In lieu of trampolines and since this
      // is before binary encoding call through a register instead.
      //
      TR::RealRegister::RegNum nativeRegisterIndex = TR::RealRegister::edi;
      TR::Register *nativeMethodReg = cg()->allocateRegister();
      site.addPostCondition(nativeMethodReg, TR::RealRegister::edi);

      generateRegImm64Instruction(TR::InstOpCode::MOV8RegImm64, callNode, nativeMethodReg, (uint64_t)(uintptr_t)methodSymbol->getMethodAddress(), cg());
      callInstr = generateRegInstruction(TR::InstOpCode::CALLReg, callNode, nativeMethodReg, cg());
      cg()->stopUsingRegister(nativeMethodReg);
      }
   else if (methodSymRef->isUnresolved() || methodSymbol->isInterpreted()
            || (comp()->compileRelocatableCode() && !methodSymbol->isHelper()) )
      {
      TR::LabelSymbol *label   = generateLabelSymbol(cg());

      TR::Snippet *snippet = (TR::Snippet*)new (trHeapMemory()) TR::X86CallSnippet(cg(), callNode, label, false);
      cg()->addSnippet(snippet);
      snippet->gcMap().setGCRegisterMask(site.getPreservedRegisterMask());

      callInstr = generateImmSymInstruction(TR::InstOpCode::CALLImm4, callNode, 0, new (trHeapMemory()) TR::SymbolReference(comp()->getSymRefTab(), label), cg());
      generateBoundaryAvoidanceInstruction(TR::X86BoundaryAvoidanceInstruction::unresolvedAtomicRegions, 8, 8, callInstr, cg());

      // Nop is necessary due to confusion when resolving shared slots at a transition
      if (methodSymRef->isOSRInductionHelper())
         generatePaddingInstruction(1, callNode, cg());
      }
   else
      {
      callInstr = generateImmSymInstruction(TR::InstOpCode::CALLImm4, callNode, (uintptr_t)methodSymbol->getMethodAddress(), methodSymRef, cg());

      if (comp()->target().isSMP() && !methodSymbol->isHelper())
         {
         // Make sure it's patchable in case it gets (re)compiled
         generatePatchableCodeAlignmentInstruction(callSiteAtomicRegions, callInstr, cg());
         }
      }

   callInstr->setNeedsGCMap(site.getPreservedRegisterMask());

   }

void
J9::X86::PrivateLinkage::buildInterfaceCall(
      TR::X86CallSite &site,
      TR::LabelSymbol *entryLabel,
      TR::LabelSymbol *doneLabel,
      uint8_t *thunk)
   {
   TR::Register *vftRegister = site.evaluateVFT();

   // Dynamic PICs populated by the PIC builder.
   // Might be able to simplify this in the presence of value profiling information.
   //
   buildIPIC(site, entryLabel, doneLabel, thunk);
   }

void J9::X86::PrivateLinkage::buildRevirtualizedCall(TR::X86CallSite &site, TR::LabelSymbol *revirtualizeLabel, TR::LabelSymbol *doneLabel)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   TR::Register *vftRegister = site.getCallNode()->getFirstChild()->getRegister(); // may be NULL; we don't need to evaluate it here
   int32_t      vftOffset   = site.getSymbolReference()->getOffset();

   TR::Snippet *snippet;
   if (comp()->target().is64Bit())
      {
#ifdef TR_TARGET_64BIT
      snippet = new (trHeapMemory()) TR::AMD64GuardedDevirtualSnippet(
         cg(),
         site.getCallNode(),
         site.getDevirtualizedMethodSymRef(),
         doneLabel,
         revirtualizeLabel,
         vftOffset,
         cg()->getCurrentEvaluationBlock(),
         vftRegister,
         site.getArgSize()
         );
#endif
      }
   else
      {
      snippet = new (trHeapMemory()) TR::X86GuardedDevirtualSnippet(
         cg(),
         site.getCallNode(),
         doneLabel,
         revirtualizeLabel,
         vftOffset,
         cg()->getCurrentEvaluationBlock(),
         vftRegister
         );
      }
   snippet->gcMap().setGCRegisterMask(site.getLinkage()->getProperties().getPreservedRegisterMapForGC());
   cg()->addSnippet(snippet);
   }

void J9::X86::PrivateLinkage::buildCallArguments(TR::X86CallSite &site)
   {
   site.setArgSize(buildArgs(site.getCallNode(), site.getPreConditionsUnderConstruction()));
   }

bool J9::X86::PrivateLinkage::buildVirtualGuard(TR::X86CallSite &site, TR::LabelSymbol *revirtualizeLabel)
   {
   TR_ASSERT(site.getVirtualGuardKind() != TR_NoGuard, "site must require a virtual guard");

   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());

   static TR_AtomicRegion vgnopAtomicRegions[] =
      {
      // Don't yet know whether we're patching using a self-loop or a 2-byte
      // jmp, but it doesn't matter because they are both 2 bytes.
      //
      { 0x0, 5 },
      { 0,0 }
      };

   TR::Node *callNode = site.getCallNode();

   // Modify following logic, also need update indirectDispatchWillBuildVirtualGuard
   // it is a none side effect version of this method that detect if virtual guard will be created.

   if (comp()->performVirtualGuardNOPing())
      {
      TR_VirtualGuard *virtualGuard =
         TR_VirtualGuard::createGuardedDevirtualizationGuard(site.getVirtualGuardKind(), comp(), callNode);

      TR::Instruction *patchable =
         generateVirtualGuardNOPInstruction(callNode, virtualGuard->addNOPSite(), NULL, revirtualizeLabel, cg());

      if (comp()->target().isSMP())
         generatePatchableCodeAlignmentInstruction(vgnopAtomicRegions, patchable, cg());
      // HCR in J9::X86::PrivateLinkage::buildRevirtualizedCall
      if (comp()->getOption(TR_EnableHCR))
         {
         TR_VirtualGuard* HCRGuard = TR_VirtualGuard::createGuardedDevirtualizationGuard(TR_HCRGuard, comp(), callNode);
         TR::Instruction *HCRpatchable = generateVirtualGuardNOPInstruction(callNode, HCRGuard->addNOPSite(), NULL, revirtualizeLabel, cg());
         if (comp()->target().isSMP())
            generatePatchableCodeAlignmentInstruction(vgnopAtomicRegions, HCRpatchable, cg());
         }
      return true;
      }
   else if (site.getVirtualGuardKind() == TR_NonoverriddenGuard
         && !comp()->getOption(TR_EnableHCR)       // If patching is off, devirtualization is not safe in HCR mode
         && !comp()->getOption(TR_MimicInterpreterFrameShape)) // Explicitly-guarded devirtualization is pretty pointless without inlining
      {
      // We can do an explicit guard
      //
      uint32_t overRiddenBit = fej9->offsetOfIsOverriddenBit();
      TR::InstOpCode::Mnemonic opCode;

      if (overRiddenBit <= 0xff)
         opCode = TR::InstOpCode::TEST1MemImm1;
      else
         opCode = TR::InstOpCode::TEST4MemImm4;

      generateMemImmInstruction(
         opCode,
         callNode,
         generateX86MemoryReference((intptr_t)site.getResolvedMethod()->addressContainingIsOverriddenBit(), cg()),
         overRiddenBit,
         cg()
         );

      generateLabelInstruction(TR::InstOpCode::JNE4, callNode, revirtualizeLabel, cg());

      return true;
      }
   else
      {
      // Can't do guarded devirtualization
      //
      return false;
      }
   }

TR::Instruction *J9::X86::PrivateLinkage::buildVFTCall(TR::X86CallSite &site, TR::InstOpCode dispatchOp, TR::Register *targetAddressReg, TR::MemoryReference *targetAddressMemref)
   {
   TR::Node *callNode = site.getCallNode();
   if (cg()->enableSinglePrecisionMethods() &&
       comp()->getJittedMethodSymbol()->usesSinglePrecisionMode())
      {
      auto cds = cg()->findOrCreate2ByteConstant(callNode, DOUBLE_PRECISION_ROUND_TO_NEAREST);
      generateMemInstruction(TR::InstOpCode::LDCWMem, callNode, generateX86MemoryReference(cds, cg()), cg());
      }

   TR::Instruction *callInstr;
   if (dispatchOp.sourceIsMemRef())
      {
      TR_ASSERT(targetAddressMemref, "Call via memory requires memref");
      // Fix the displacement at 4 bytes so j2iVirtual can decode it if necessary
      if (targetAddressMemref)
         targetAddressMemref->setForceWideDisplacement();
      callInstr = generateCallMemInstruction(dispatchOp.getOpCodeValue(), callNode, targetAddressMemref, cg());
      }
   else
      {
      TR_ASSERT(targetAddressReg, "Call via register requires register");
      TR::Node *callNode = site.getCallNode();
      TR::ResolvedMethodSymbol *resolvedMethodSymbol = callNode->getSymbol()->getResolvedMethodSymbol();
      bool mayReachJ2IThunk = true;
      if (resolvedMethodSymbol && resolvedMethodSymbol->getRecognizedMethod() == TR::java_lang_invoke_ComputedCalls_dispatchDirect)
         mayReachJ2IThunk = false;
      if (mayReachJ2IThunk && dispatchOp.isCallOp())
         {
         // Bad news.
         //
         // icallVMprJavaSendPatchupVirtual requires that a virtual call site
         // either (1) uses a TR::InstOpCode::CALLMem with a fixed VFT offset, or (2) puts the
         // VFT index into r8 and uses a TR::InstOpCode::CALLImm4 with a fixed call target.
         // We have neither a fixed VFT offset nor a fixed call target!
         // Adding support for TR::InstOpCode::CALLReg is difficult because the instruction is
         // a different length, making it hard to back up and disassemble it.
         //
         // Therefore, we cannot have the return address pointing after a
         // TR::InstOpCode::CALLReg instruction.  Instead, we use a TR::InstOpCode::CALLImm4 with a fixed
         // displacement to get to out-of-line instructions that do a TR::InstOpCode::JMPReg.

         // Mainline call
         //
         TR::LabelSymbol *jmpLabel   = TR::LabelSymbol::create(cg()->trHeapMemory(),cg());
         callInstr = generateLabelInstruction(TR::InstOpCode::CALLImm4, callNode, jmpLabel, cg());

         // Jump outlined
         //
         {
         TR_OutlinedInstructionsGenerator og(jmpLabel, callNode, cg());
         generateRegInstruction(TR::InstOpCode::JMPReg, callNode, targetAddressReg, cg());
         og.endOutlinedInstructionSequence();
         }

         // The targetAddressReg doesn't appear to be used in mainline code, so
         // register assignment may do weird things like spill it.  We'd prefer it
         // to stay in a register, though we don't care which.
         //
         TR::RegisterDependencyConditions *dependencies = site.getPostConditionsUnderConstruction();
         if (targetAddressReg && targetAddressReg->getRegisterPair())
            {
            dependencies->unionPreCondition(targetAddressReg->getRegisterPair()->getHighOrder(), TR::RealRegister::NoReg, cg());
            dependencies->unionPreCondition(targetAddressReg->getRegisterPair()->getLowOrder(), TR::RealRegister::NoReg, cg());
            }
         else
            dependencies->unionPreCondition(targetAddressReg, TR::RealRegister::NoReg, cg());
         }
      else
         {
         callInstr = generateRegInstruction(dispatchOp.getOpCodeValue(), callNode, targetAddressReg, cg());
         }
      }

   callInstr->setNeedsGCMap(site.getPreservedRegisterMask());

   TR_ASSERT_FATAL(
      !site.getSymbolReference()->isUnresolved() || site.getMethodSymbol()->isInterface(),
      "buildVFTCall: unresolved virtual site");

   if (cg()->enableSinglePrecisionMethods() &&
       comp()->getJittedMethodSymbol()->usesSinglePrecisionMode())
      {
      auto cds = cg()->findOrCreate2ByteConstant(callNode, SINGLE_PRECISION_ROUND_TO_NEAREST);
      generateMemInstruction(TR::InstOpCode::LDCWMem, callNode, generateX86MemoryReference(cds, cg()), cg());
      }

   return callInstr;
   }

TR::Register *J9::X86::PrivateLinkage::buildCallPostconditions(TR::X86CallSite &site)
   {
   TR::RegisterDependencyConditions *dependencies = site.getPostConditionsUnderConstruction();
   TR_ASSERT(dependencies != NULL, "assertion failure");

   const TR::X86LinkageProperties &properties   = getProperties();
   const TR::RealRegister::RegNum  noReg        = TR::RealRegister::NoReg;
   TR::Node                       *callNode     = site.getCallNode();
   TR::MethodSymbol               *methodSymbol = callNode->getSymbolReference()->getSymbol()->castToMethodSymbol();
   bool               calleePreservesRegisters = methodSymbol->preservesAllRegisters();

#ifdef J9VM_OPT_JAVA_CRYPTO_ACCELERATION
   // AES helpers actually use Java private linkage and do not preserve all
   // registers.  This should really be handled by the linkage.
   //
   if (cg()->enableAESInHardwareTransformations() && methodSymbol && methodSymbol->isHelper())
      {
      TR::SymbolReference *methodSymRef = callNode->getSymbolReference();
      switch (methodSymRef->getReferenceNumber())
         {
         case TR_doAESInHardwareInner:
         case TR_expandAESKeyInHardwareInner:
            calleePreservesRegisters = false;
            break;

         default:
            break;
         }
      }
#endif

   // We have to be careful to allocate the return register after the
   // dependency conditions for the other killed registers have been set up,
   // otherwise it will be marked as interfering with them.

   // Figure out which is the return register
   //
   TR::RealRegister::RegNum  returnRegIndex, highReturnRegIndex=noReg;
   TR_RegisterKinds         returnKind;
   switch(callNode->getDataType())
      {
      default:
         TR_ASSERT(0, "Unrecognized call node data type: #%d", (int)callNode->getDataType());
         // fall through
      case TR::NoType:
         returnRegIndex  = noReg;
         returnKind      = TR_NoRegister;
         break;
      case TR::Int64:
         if (cg()->usesRegisterPairsForLongs())
            {
            returnRegIndex     = getProperties().getLongLowReturnRegister();
            highReturnRegIndex = getProperties().getLongHighReturnRegister();
            returnKind         = TR_GPR;
            break;
            }
         // else fall through
      case TR::Int8:
      case TR::Int16:
      case TR::Int32:
      case TR::Address:
         returnRegIndex  = getProperties().getIntegerReturnRegister();
         returnKind      = TR_GPR;
         break;
      case TR::Float:
      case TR::Double:
         returnRegIndex  = getProperties().getFloatReturnRegister();
         returnKind      = TR_FPR;
         break;
      }

   // Find the registers that are already in the postconditions so we don't add them again.
   // (The typical example is the ramMethod.)
   //
   int32_t gprsAlreadyPresent = TR::RealRegister::noRegMask;
   TR::RegisterDependencyGroup *group = dependencies->getPostConditions();
   for (int i = 0; i < dependencies->getAddCursorForPost(); i++)
      {
      TR::RegisterDependency *dep = group->getRegisterDependency(i);
      TR_ASSERT(dep->getRealRegister() <= TR::RealRegister::LastAssignableGPR, "Currently, only GPRs can be added to call postcondition before buildCallPostconditions; found %s", cg()->getDebug()->getRealRegisterName(dep->getRealRegister()-1));
      gprsAlreadyPresent |= TR::RealRegister::gprMask((TR::RealRegister::RegNum)dep->getRealRegister());
      }

   // Add postconditions indicating the state of arg regs (other than the return reg)
   //
   if (calleePreservesRegisters)
      {
      // For all argument-register preconditions, add an identical
      // postcondition, thus indicating that the arguments are preserved.
      // Note: this assumes the postcondition regdeps have preconditions too; see COPY_PRECONDITIONS_TO_POSTCONDITIONS.
      //
      TR::RegisterDependencyGroup *preConditions = dependencies->getPreConditions();
      for (int i = 0; i < dependencies->getAddCursorForPre(); i++)
         {
         TR::RegisterDependency *preCondition = preConditions->getRegisterDependency(i);
         TR::RealRegister::RegNum   regIndex     = preCondition->getRealRegister();

         if (regIndex <= TR::RealRegister::LastAssignableGPR && (gprsAlreadyPresent & TR::RealRegister::gprMask(regIndex)))
            continue;

         if (
            regIndex != returnRegIndex && regIndex != highReturnRegIndex
            && (properties.isIntegerArgumentRegister(regIndex) || properties.isFloatArgumentRegister(regIndex))
            ){
            dependencies->addPostCondition(preCondition->getRegister(), regIndex, cg());
            }
         }
      }
   else
      {
      // Kill all non-preserved int and float regs besides the return register,
      // by assigning them to unused virtual registers
      //
      TR::RealRegister::RegNum regIndex;

      for (regIndex = TR::RealRegister::FirstGPR; regIndex <= TR::RealRegister::LastAssignableGPR; regIndex = (TR::RealRegister::RegNum)(regIndex + 1))
         {
         // Skip non-assignable registers
         //
         if (machine()->getRealRegister(regIndex)->getState() == TR::RealRegister::Locked)
            continue;

         // Skip registers already present
         if (gprsAlreadyPresent & TR::RealRegister::gprMask(regIndex))
            continue;

         if ((regIndex != returnRegIndex) && (regIndex != highReturnRegIndex) && !properties.isPreservedRegister(regIndex))
            {
            TR::Register *dummy = cg()->allocateRegister(TR_GPR);
            dummy->setPlaceholderReg();
            dependencies->addPostCondition(dummy, regIndex, cg());
            cg()->stopUsingRegister(dummy);
            }
         }

      TR_LiveRegisters *lr = cg()->getLiveRegisters(TR_FPR);
      if(!lr || lr->getNumberOfLiveRegisters() > 0)
         {
         for (regIndex = TR::RealRegister::FirstXMMR; regIndex <= TR::RealRegister::LastXMMR; regIndex = (TR::RealRegister::RegNum)(regIndex + 1))
            {
            TR_ASSERT(regIndex != highReturnRegIndex, "highReturnRegIndex should not be an XMM register.");
            if ((regIndex != returnRegIndex) && !properties.isPreservedRegister(regIndex))
               {
               TR::Register *dummy = cg()->allocateRegister(TR_FPR);
               dummy->setPlaceholderReg();
               dependencies->addPostCondition(dummy, regIndex, cg());
               cg()->stopUsingRegister(dummy);
               }
            }
         }
      }

   // Preserve the VM thread register
   //
   dependencies->addPostCondition(cg()->getMethodMetaDataRegister(), getProperties().getMethodMetaDataRegister(), cg());

   // Now that everything is dead, we can allocate the return register without
   // interference
   //
   TR::Register *returnRegister;
   if (highReturnRegIndex)
      {
      TR::Register *lo = cg()->allocateRegister(returnKind);
      TR::Register *hi = cg()->allocateRegister(returnKind);
      returnRegister = cg()->allocateRegisterPair(lo, hi);
      dependencies->addPostCondition(lo, returnRegIndex,     cg());
      dependencies->addPostCondition(hi, highReturnRegIndex, cg());
      }
   else if (returnRegIndex)
      {
      TR_ASSERT(returnKind != TR_NoRegister, "assertion failure");
      if (callNode->getDataType() == TR::Address)
         {
         returnRegister = cg()->allocateCollectedReferenceRegister();
         }
      else
         {
         returnRegister = cg()->allocateRegister(returnKind);
         if (callNode->getDataType() == TR::Float)
            returnRegister->setIsSinglePrecision();
         }
      dependencies->addPostCondition(returnRegister, returnRegIndex, cg());
      }
   else
      {
      returnRegister = NULL;
      }

   return returnRegister;
   }


void J9::X86::PrivateLinkage::buildVPIC(TR::X86CallSite &site, TR::LabelSymbol *entryLabel, TR::LabelSymbol *doneLabel)
   {
   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());
   TR_ASSERT(doneLabel, "a doneLabel is required for VPIC dispatches");

   if (entryLabel)
      generateLabelInstruction(TR::InstOpCode::label, site.getCallNode(), entryLabel, cg());

   int32_t numVPicSlots = VPicParameters.defaultNumberOfSlots;

   TR::SymbolReference *callHelperSymRef =
      cg()->symRefTab()->findOrCreateRuntimeHelper(TR_X86populateVPicSlotCall, true, true, false);

   if (numVPicSlots > 1)
      {
      TR::X86PICSlot emptyPicSlot = TR::X86PICSlot(VPicParameters.defaultSlotAddress, NULL);
      emptyPicSlot.setNeedsShortConditionalBranch();
      emptyPicSlot.setJumpOnNotEqual();
      emptyPicSlot.setNeedsPicSlotAlignment();
      emptyPicSlot.setHelperMethodSymbolRef(callHelperSymRef);
      emptyPicSlot.setGenerateNextSlotLabelInstruction();

      // Generate all slots except the last
      // (short branch to next slot, jump to doneLabel)
      //
      while (--numVPicSlots)
         {
         TR::LabelSymbol *nextSlotLabel = generateLabelSymbol(cg());
         buildPICSlot(emptyPicSlot, nextSlotLabel, doneLabel, site);
         }
      }

   // Generate the last slot
   // (long branch to lookup snippet, fall through to doneLabel)
   //
   TR::X86PICSlot lastPicSlot = TR::X86PICSlot(VPicParameters.defaultSlotAddress, NULL, false);
   lastPicSlot.setJumpOnNotEqual();
   lastPicSlot.setNeedsPicSlotAlignment();
   lastPicSlot.setNeedsLongConditionalBranch();

   if (comp()->target().is32Bit())
      {
      lastPicSlot.setNeedsPicCallAlignment();
      }

   lastPicSlot.setHelperMethodSymbolRef(callHelperSymRef);

   TR::LabelSymbol *snippetLabel = generateLabelSymbol(cg());

   TR::Instruction *slotPatchInstruction = buildPICSlot(lastPicSlot, snippetLabel, NULL, site);

   TR::Instruction *startOfPicInstruction = site.getFirstPICSlotInstruction();

   TR::X86PicDataSnippet *snippet = new (trHeapMemory()) TR::X86PicDataSnippet(
      VPicParameters.defaultNumberOfSlots,
      startOfPicInstruction,
      snippetLabel,
      doneLabel,
      site.getSymbolReference(),
      slotPatchInstruction,
      site.getThunkAddress(),
      false,
      cg());

   snippet->gcMap().setGCRegisterMask(site.getPreservedRegisterMask());
   cg()->addSnippet(snippet);

   cg()->incPicSlotCountBy(VPicParameters.defaultNumberOfSlots);
   cg()->reserveNTrampolines(VPicParameters.defaultNumberOfSlots);
   }

void J9::X86::PrivateLinkage::buildInterfaceDispatchUsingLastITable (TR::X86CallSite &site, int32_t numIPicSlots, TR::X86PICSlot &lastPicSlot, TR::Instruction *&slotPatchInstruction, TR::LabelSymbol *doneLabel, TR::LabelSymbol *lookupDispatchSnippetLabel, TR_OpaqueClassBlock *declaringClass, uintptr_t itableIndex )
   {
   static char *breakBeforeInterfaceDispatchUsingLastITable = feGetEnv("TR_breakBeforeInterfaceDispatchUsingLastITable");

   TR_J9VMBase *fej9 = (TR_J9VMBase *)(fe());

   TR::Node *callNode = site.getCallNode();

   TR::LabelSymbol *lastITableTestLabel     = generateLabelSymbol(cg());
   TR::LabelSymbol *lastITableDispatchLabel = generateLabelSymbol(cg());

   if (numIPicSlots >= 1)
      {
      // The last PIC slot looks much like the others
      //
      lastPicSlot.setNeedsShortConditionalBranch();
      lastPicSlot.setNeedsJumpToDone();
      slotPatchInstruction = buildPICSlot(lastPicSlot, lastITableTestLabel, doneLabel, site);
      }
   else
      {
      // The sequence below requires control to flow straight to lastITableTestLabel
      // TODO: This is lame.  Without IPIC slots, generating this sequence
      // upside-down is sub-optimal.
      //
         generateLabelInstruction(TR::InstOpCode::JMP4, callNode, lastITableTestLabel, cg());
      }

   TR::Register *vftReg          = site.evaluateVFT();
   TR::Register *scratchReg      = cg()->allocateRegister();
   TR::Register *vtableIndexReg  = cg()->allocateRegister();
   TR::RegisterDependencyConditions* vtableIndexRegDeps = generateRegisterDependencyConditions(1, 0, cg());
   vtableIndexRegDeps->addPreCondition(vtableIndexReg, getProperties().getVTableIndexArgumentRegister(), cg());
   // Now things get weird.
   //
   // We're going to generate the lastITable sequence upside-down.
   // We'll generate the dispatch sequence first, and THEN we'll generate
   // the test that guards that dispatch.
   //
   // Why?
   //
   // 1) You can't call a j2i thunk with your return address pointing at a
   //    TR::InstOpCode::CALLMem unless that TR::InstOpCode::CALLMem has a displacement which equals the jit
   //    vtable offset.  We don't know the vtable offset statically, so we
   //    must pass it in r8 and leave the return address pointing at a CALLImm.
   //
   // 2) PICBuilder needs to work with or without this lastITable dispatch.
   //    To avoid extreme complexity in PICBuilder, that means the return
   //    address should point at a sequence that looks enough like a PIC
   //    slot that PICBuilder can act the same for both.
   //
   // 3) Given 1&2 above, the natural thing to do would be to put the
   //    dispatch sequence out of line.  However, we expect this to be
   //    performance-critical, so we want it nearby.  It just so happens
   //    that the previous PIC slot ends with an unconditional jump, so we
   //    can just stuff the dispatch sequence right between the last PIC
   //    slot and the lastITable test.
   //
   // The final layout looks like this:
   //
   //          jne lastITableTest                                 ; PREVIOUS PIC SLOT
   //          call xxx                                           ; PREVIOUS PIC SLOT
   //          jmp done                                           ; PREVIOUS PIC SLOT
   //    lastITableDispatch:
   //          mov r8, sizeof(J9Class)
   //          sub r8, [rdi + ITableSlotOffset]                   ; r8 = jit vtable offset
   //          jmp [vft + r8]                                     ; vtable dispatch
   //    lastITableTest:
   //          mov rdi, [vft + lastITableOffset]                  ; cached ITable
   //          cmp [rdi + interfaceClassOffset], interfaceClass   ; check if it's our interface class
   //          jne lookupDispatchSnippet                          ; if not, jump to the slow path
   //          call lastITableDispatch                            ; if so, call the dispatch sequence with return address pointing here
   //    done:
   //          ...

   // The dispatch sequence
   //

   TR::Instruction *lastITableDispatchStart = generateLabelInstruction(  TR::InstOpCode::label, callNode, lastITableDispatchLabel, cg());
   generateRegImmInstruction( TR::InstOpCode::MOV4RegImm4, callNode, vtableIndexReg, fej9->getITableEntryJitVTableOffset(), cg());
   generateRegMemInstruction( TR::InstOpCode::SUBRegMem(), callNode, vtableIndexReg, generateX86MemoryReference(scratchReg, fej9->convertITableIndexToOffset(itableIndex), cg()), cg());
   buildVFTCall(site,         TR::InstOpCode::JMPMem, NULL, generateX86MemoryReference(vftReg, vtableIndexReg, 0, cg()));

   // Without PIC slots, lastITableDispatchStart takes the place of various "first instruction" pointers
   //
   if (!site.getFirstPICSlotInstruction())
      site.setFirstPICSlotInstruction(lastITableDispatchStart);
   if (!slotPatchInstruction)
      slotPatchInstruction = lastITableDispatchStart;

   // The test sequence
   //
   generateLabelInstruction(TR::InstOpCode::label, callNode, lastITableTestLabel, cg());
   if (breakBeforeInterfaceDispatchUsingLastITable)
      generateInstruction(TR::InstOpCode::bad, callNode, cg());
   generateRegMemInstruction(TR::InstOpCode::LRegMem(), callNode, scratchReg, generateX86MemoryReference(vftReg, (int32_t)fej9->getOffsetOfLastITableFromClassField(), cg()), cg());
   bool use32BitInterfacePointers = comp()->target().is32Bit();
   if (comp()->useCompressedPointers() /* actually compressed object headers */)
      {
      // The field is 8 bytes, but only 4 matter
      use32BitInterfacePointers = true;
      }
   if (use32BitInterfacePointers)
      {
      // The field is 8 bytes, but only 4 matter
      generateMemImmInstruction(TR::InstOpCode::CMP4MemImm4,
                                callNode,
                                generateX86MemoryReference(scratchReg, fej9->getOffsetOfInterfaceClassFromITableField(), cg()),
                                (int32_t)(intptr_t)declaringClass,
                                cg());
      }
   else
      {
      TR_ASSERT(comp()->target().is64Bit(), "Only 64-bit path should reach here.");
      TR::Register *interfaceClassReg = vtableIndexReg;
      auto cds = cg()->findOrCreate8ByteConstant(site.getCallNode(), (intptr_t)declaringClass);
      TR::MemoryReference *interfaceClassAddr = generateX86MemoryReference(cds, cg());
      generateRegMemInstruction(TR::InstOpCode::LRegMem(), callNode, interfaceClassReg, interfaceClassAddr, cg());
      generateMemRegInstruction(TR::InstOpCode::CMPMemReg(),
                                callNode,
                                generateX86MemoryReference(scratchReg, fej9->getOffsetOfInterfaceClassFromITableField(), cg()),
                                interfaceClassReg, cg());
      }

   generateLongLabelInstruction(TR::InstOpCode::JNE4, callNode, lookupDispatchSnippetLabel, cg()); // PICBuilder needs this to have a 4-byte offset
   if (comp()->target().is32Bit())
      generatePaddingInstruction(3, callNode, cg());
   generateLabelInstruction(TR::InstOpCode::CALLImm4, callNode, lastITableDispatchLabel, vtableIndexRegDeps, cg());

   cg()->stopUsingRegister(vtableIndexReg);
   TR::RealRegister::RegNum otherScratchRegister = getProperties().getJ9MethodArgumentRegister(); // scratch reg other than the vtable index reg
   site.addPostCondition(scratchReg, otherScratchRegister);
   site.addPostCondition(vftReg, TR::RealRegister::NoReg);
   }

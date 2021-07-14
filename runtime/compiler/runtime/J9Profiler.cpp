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

#include "runtime/J9Profiler.hpp"
#include <string.h>
#include "AtomicSupport.hpp"
#include "codegen/CodeGenerator.hpp"
#include "codegen/LinkageConventionsEnum.hpp"
#include "codegen/RecognizedMethods.hpp"
#include "compile/Compilation.hpp"
#include "compile/Method.hpp"
#include "compile/ResolvedMethod.hpp"
#include "compile/SymbolReferenceTable.hpp"
#include "control/Options.hpp"
#include "control/OptionsUtil.hpp"
#include "control/Options_inlines.hpp"
#include "control/Recompilation.hpp"
#include "control/RecompilationInfo.hpp"
#include "env/IO.hpp"
#include "env/PersistentCHTable.hpp"
#include "env/PersistentInfo.hpp"
#include "env/StackMemoryRegion.hpp"
#include "env/jittypes.h"
#include "env/VMJ9.h"
#include "env/VerboseLog.hpp"
#include "il/Block.hpp"
#include "il/DataTypes.hpp"
#include "il/ILOpCodes.hpp"
#include "il/ILOps.hpp"
#include "il/MethodSymbol.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/ResolvedMethodSymbol.hpp"
#include "il/Symbol.hpp"
#include "il/SymbolReference.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "infra/Assert.hpp"
#include "infra/BitVector.hpp"
#include "infra/Cfg.hpp"
#include "infra/List.hpp"
#include "infra/TRCfgNode.hpp"
#include "optimizer/Optimizations.hpp"
#include "optimizer/Structure.hpp"
#include "optimizer/TransformUtil.hpp"
#include "optimizer/JProfilingValue.hpp"
#include "runtime/ExternalProfiler.hpp"
#include "runtime/J9Runtime.hpp"
#include "runtime/J9ValueProfiler.hpp"

//*************************************************************************
//
// Optimization passes to insert recompilation counters, etc
//
//*************************************************************************

#define OPT_DETAILS "O^O RECOMPILATION COUNTERS: "

TR_RecompilationModifier::TR_RecompilationModifier(TR::OptimizationManager *manager)
   : TR::Optimization(manager),
     _recompilation(manager->comp()->getRecompilationInfo())
   {
   if (_recompilation)
      {
      if (!comp()->fej9()->isAOT_DEPRECATED_DO_NOT_USE())
         requestOpt(OMR::recompilationModifier);
      }
   }

int32_t TR_RecompilationModifier::perform()
   {
   for (TR_RecompilationProfiler * rp = _recompilation->getFirstProfiler(); rp; rp = rp->getNext())
      rp->modifyTrees();
   return 1;
   }

const char *
TR_RecompilationModifier::optDetailString() const throw()
   {
   return "O^O RECOMPILATION COUNTERS: ";
   }

void TR_LocalRecompilationCounters::removeTrees()
   {
   TR::SymbolReference *symRef = _recompilation->getCounterSymRef();
   for (TR::TreeTop *treeTop = comp()->getStartTree();
        treeTop;
        treeTop = treeTop->getNextTreeTop())
      {
      TR::Node *node = treeTop->getNode();
      if (node->getOpCodeValue() == TR::istore &&
          node->getSymbolReference() == symRef)
         {
         TR::TreeTop *prevTree = treeTop->getPrevTreeTop();
         TR::TransformUtil::removeTree(comp(), treeTop);
         treeTop = prevTree;
         }
      }
   }

void TR_LocalRecompilationCounters::modifyTrees()
   {
   if (!comp()->mayHaveLoops())
      return;

   if (debug("traceCounters"))
      {
      diagnostic("Starting Local Recompilation Counters for %s\n", comp()->signature());
      comp()->dumpMethodTrees("Trees before Local Recompilation Counters");
      }

   // Wherever there is an asynccheck tree, insert a counter decrement
   //
   for (TR::TreeTop *treeTop = comp()->getStartTree();
        treeTop;
        treeTop = treeTop->getNextTreeTop())
      {
      TR::Node *node = treeTop->getNode();
      if (node->getOpCodeValue() == TR::asynccheck)
         {
         if (performTransformation(comp(), "%s LOCAL RECOMPILATION COUNTERS: Add recomp counter decrement at async check %p\n", OPT_DETAILS, node))
            {
            treeTop = TR::TreeTop::createIncTree(comp(), node, _recompilation->getCounterSymRef(), -1);
            setHasModifiedTrees(true);
            }
         }
      }

   if (debug("traceCounters"))
      {
      comp()->dumpMethodTrees("Trees after Local Recompilation Counters");
      }
   }

void TR_GlobalRecompilationCounters::modifyTrees()
   {
   if (!comp()->mayHaveLoops())
      return;

   if (debug("traceCounters"))
      {
      diagnostic("Starting Global Recompilation Counters for %s\n", comp()->signature());
      comp()->dumpMethodTrees("Trees before Global Recompilation Counters");
      }

   {
   TR::StackMemoryRegion stackMemoryRegion(*trMemory());

   // Find all natural loops and insert a decrement of the counter to their
   // header blocks
   //
   TR::CFG *cfg = comp()->getFlowGraph();
   int32_t numBlocks = cfg->getNextNodeNumber();
   TR_BitVector headerBlocks(numBlocks, trMemory(), stackAlloc);

   examineStructure(cfg->getStructure(), headerBlocks);
   } // Scope of the stack memory region

   if (debug("traceCounters"))
      {
      comp()->dumpMethodTrees("Trees after Global Recompilation Counters");
      }
   }

void
TR_GlobalRecompilationCounters::examineStructure(
      TR_Structure *s,
      TR_BitVector &headerBlocks)
   {
   // If this is a block structure, insert a decrement of the recompilation
   // counter if the block has been marked as a header block
   //
   TR_BlockStructure *blockStructure = s->asBlock();
   if (blockStructure)
      {
      TR::Block *block = blockStructure->getBlock();
      if (headerBlocks.get(block->getNumber()))
         {
         if (performTransformation(comp(), "%s GLOBAL RECOMPILATION COUNTERS: Add recomp counter decrement at loop header block_%d\n", OPT_DETAILS, block->getNumber()))
            {
            TR::TreeTop::createIncTree(comp(), block->getEntry()->getNode(), _recompilation->getCounterSymRef(), -1, block->getEntry());
            setHasModifiedTrees(true);
            }
         }
      return;
      }

   // This is a region. If it is a natural loop, mark the header block
   //
   TR_RegionStructure *region = s->asRegion();
   if (region->isNaturalLoop())
      headerBlocks.set(region->getNumber());

   // Now look at the children
   //
   TR_RegionStructure::Cursor subNodes(*region);
   for (TR_StructureSubGraphNode *subNode = subNodes.getCurrent();
        subNode;
        subNode = subNodes.getNext())
      {
      examineStructure(subNode->getStructure(), headerBlocks);
      }
   }

TR_CatchBlockProfiler::TR_CatchBlockProfiler(
      TR::Compilation  * c,
      TR::Recompilation * r,
      bool forInitialCompilation)
   : TR_RecompilationProfiler(c, r, forInitialCompilation ? initialCompilation : 0),
      _profileInfo(0),
      _throwCounterSymRef(0),
      _catchCounterSymRef(0)
   {
   }

void TR_CatchBlockProfiler::removeTrees()
   {
   if (debug("dontRemoveCatchBlockProfileTrees"))
      return;

   for (TR::TreeTop *tt = comp()->getStartTree(); tt; tt = tt->getNextTreeTop())
      {
      TR::Node *node = tt->getNode();
      if (node->getOpCodeValue() == TR::istore)
         {
         TR::SymbolReference * symRef = node->getSymbolReference();
         if (symRef == _throwCounterSymRef || symRef == _catchCounterSymRef)
            {
            TR::TreeTop *prevTree = tt->getPrevTreeTop();
            TR::TransformUtil::removeTree(comp(), tt);
            tt = prevTree;
            }
         }
      }
   }

void TR_CatchBlockProfiler::modifyTrees()
   {
   // neither the throw or catch symbol has been created then no need to walk the trees
   //
   TR::SymbolReferenceTable * symRefTab = comp()->getSymRefTab();
   if (!symRefTab->getSymRef(TR_aThrow) && !symRefTab->getSymRef(TR::SymbolReferenceTable::excpSymbol))
      return;

   TR_CatchBlockProfileInfo *profileInfo = _recompilation->findOrCreateProfileInfo()->findOrCreateCatchBlockProfileInfo(comp());

   TR::TreeTop * firstTree = comp()->getStartTree();
   for (TR::TreeTop * tt = firstTree; tt; tt = tt->getNextTreeTop())
      {
      TR::Node * node = tt->getNode();
      if (((node->getOpCodeValue() == TR::athrow && !node->throwInsertedByOSR()) ||
          (node->getNumChildren() > 0 &&
           (node->getFirstChild()->getOpCodeValue() == TR::athrow) &&
           (!node->getFirstChild()->throwInsertedByOSR()))))
         {
         if (performTransformation(comp(), "%s CATCH BLOCK PROFILER: Add profiling trees to track the execution frequency of throw %p\n", OPT_DETAILS, node))
            {
            if (!_throwCounterSymRef)
               _throwCounterSymRef = symRefTab->createKnownStaticDataSymbolRef(&profileInfo->getThrowCounter(), TR::Int32);
            TR::TreeTop *profilingTree = TR::TreeTop::createIncTree(comp(), node, _throwCounterSymRef, 1, tt->getPrevTreeTop());
            profilingTree->getNode()->setIsProfilingCode();
            setHasModifiedTrees(true);
            }
         }
      }

   for (TR::Block * b = firstTree->getNode()->getBlock(); b; b = b->getNextBlock())
      if (!b->getExceptionPredecessors().empty() && !b->isOSRCatchBlock())
         {
         if (performTransformation(comp(), "%s CATCH BLOCK PROFILER: Add profiling trees to track the execution frequency of catch block_%d\n", OPT_DETAILS, b->getNumber()))
            {
            if (!_catchCounterSymRef)
               _catchCounterSymRef = symRefTab->createKnownStaticDataSymbolRef(&profileInfo->getCatchCounter(), TR::Int32);
            TR::TreeTop *profilingTree = TR::TreeTop::createIncTree(comp(), b->getEntry()->getNode(), _catchCounterSymRef, 1, b->getEntry());
            profilingTree->getNode()->setIsProfilingCode();
            setHasModifiedTrees(true);
            }
         }
   }

TR_BlockFrequencyProfiler::TR_BlockFrequencyProfiler(TR::Compilation  * c, TR::Recompilation * r)
   : TR_RecompilationProfiler(c, r)
   {
   }

void TR_BlockFrequencyProfiler::modifyTrees()
   {
   TR_BlockFrequencyInfo *blockFrequencyInfo = _recompilation->findOrCreateProfileInfo()->findOrCreateBlockFrequencyInfo(comp());

   TR_ByteCodeInfo invalidByteCodeInfo;
   invalidByteCodeInfo.setInvalidByteCodeIndex();
   invalidByteCodeInfo.setInvalidCallerIndex();
   invalidByteCodeInfo.setIsSameReceiver(0);
   invalidByteCodeInfo.setDoNotProfile(0);
   TR_ByteCodeInfo prevByteCodeInfo = invalidByteCodeInfo;
   for (TR::TreeTop *treeTop = comp()->getStartTree(); treeTop; treeTop = treeTop->getNextTreeTop())
      {
      TR::Node *node = treeTop->getNode();
      if (node->getOpCodeValue() == TR::BBStart)
         {
         // Start of a block

         // If the BBStart node has the same bytecode info as its predecessor
         // and is reachable from the predecessor, this indicates a block that
         // has been split and only the first of the two blocks needs to be
         // counted.
         //
         if ((node->getByteCodeInfo().getCallerIndex() == prevByteCodeInfo.getCallerIndex() && node->getByteCodeInfo().getByteCodeIndex() == prevByteCodeInfo.getByteCodeIndex()))
            {
            TR::Node *prevNode = treeTop->getPrevTreeTop()->getPrevRealTreeTop()->getNode();
            if (!prevNode->getOpCode().isBranch() || prevNode->getOpCode().isIf())
               {
               // However, insert the counters if the current block is reachable from multiple blocks
               //
               if (!(node->getBlock()->getPredecessors().size() > 1))
                  continue;
               }
            }

         // If the block is marked as not to be profiled, don't add the
         // frequency counting tree.
         //
         if (node->getBlock()->doNotProfile())
            {
            prevByteCodeInfo = invalidByteCodeInfo;
            continue;
            }

        if (!performTransformation(comp(), "%s BLOCK FREQUENCY PROFILER: Add profiling trees to track the execution frequency of block_%d\n", OPT_DETAILS, node->getBlock()->getNumber()))
           continue;

         // Create the block frequency counting tree and put it after the
         // BBStart node.
         //
         TR::Node *storeNode;
         TR::SymbolReference *symRef = comp()->getSymRefTab()->createKnownStaticDataSymbolRef(blockFrequencyInfo->getFrequencyForBlock(node->getBlock()->getNumber()), TR::Int32);
         symRef->getSymbol()->setIsBlockFrequency();
         symRef->getSymbol()->setNotDataAddress();
         treeTop = TR::TreeTop::createIncTree(comp(), node, symRef, 1, treeTop);
         storeNode = treeTop->getNode();

         storeNode->setIsProfilingCode();
         prevByteCodeInfo = node->getByteCodeInfo();
         }

      else if (node->getOpCodeValue() == TR::asynccheck)
         {
         // Count the next block anyway, even if it is split from the previous
         // block.
         //
         prevByteCodeInfo = invalidByteCodeInfo;
         }
      }
   }



void TR_ValueProfiler::modifyTrees()
   {
   // For now only the only value profiling done for an initial compilation is
   // overridden virtual call profiling for method with catch blocks.  This is a small hack
   // to allow overridden virtual functions to be inlined when recompiling methods
   // that have lots of catch blocks.  This allows the throw to goto transformation
   // to speed up javac by 10%.
   //
   //if (getInitialCompilation() && !comp()->getSymRefTab()->getSymRef(TR::SymbolReferenceTable::excpSymbol))
   //   return;

   vcount_t visitCount = comp()->incVisitCount();
   TR::Block *block = NULL;
   for (TR::TreeTop * tt = comp()->getStartTree(); tt; tt = tt->getNextTreeTop())
      {
      TR::Node * node = tt->getNode();
      if (node->getOpCodeValue() == TR::BBStart)
         block = node->getBlock();

      TR::Node * firstChild = node->getNumChildren() > 0 ? node->getFirstChild() : 0;
      if (firstChild && firstChild->getOpCodeValue() == TR::arraycopy &&
          !getInitialCompilation() && !debug("disableProfileArrayCopy"))
         {
         TR::Node *arrayCopyLen = firstChild->getChild(firstChild->getNumChildren()-1);
         if (!arrayCopyLen->getOpCode().isLoadConst() &&
             !arrayCopyLen->getByteCodeInfo().doNotProfile() &&
             !(arrayCopyLen->getOpCode().isCallIndirect() &&
               !arrayCopyLen->isTheVirtualCallNodeForAGuardedInlinedCall()))
             {
             addProfilingTrees(arrayCopyLen, tt, firstChild->getByteCodeInfo(), 0, LastValueInfo, LastProfiler, true);
             }
         }
      else if (firstChild && firstChild->getOpCode().isCall() &&
               firstChild->getVisitCount() != visitCount &&
               !debug("disableProfilingOfVirtualCalls"))
         {
         firstChild->setVisitCount(visitCount);

         TR::Node *callNode = firstChild;
         TR::SymbolReference *methodSymRef = callNode->getSymbolReference();
         TR::MethodSymbol *methodSymbol = methodSymRef->getSymbol()->castToMethodSymbol();

         bool doit=true;

         if (firstChild->isTheVirtualCallNodeForAGuardedInlinedCall())
             {
             doit=false;
             TR::Block * callBlock = tt->getEnclosingBlock();
             TR::Block * guard = callBlock->findVirtualGuardBlock(comp()->getFlowGraph());

             if (!guard || (guard->getLastRealTreeTop()->getNode()->isProfiledGuard() || !guard->getLastRealTreeTop()->getNode()->isNopableInlineGuard()))
                doit=true;
             }



         if (doit && firstChild->getOpCode().isCallIndirect()
         //&& !firstChild->getFirstChild()->getByteCodeInfo().doNotProfile() // We should profile this situation even if the bytecode has doNotProfile on it since we could have a node
                                                                         // that was cloned by loop versioner (or any other opt that clones node) that we still want to profile. The usual
                                                                         // risk with profiling a value that is different than the intended one because of arbitrary sharing of bytecode info between a node created by IL gen originally and newly created
                                                                        //  nodes that have nothing to do with any value seen in the original problem is not an issue here I feel
                                                                         // mostly because I do not see how we can have such a value appear as the vft child of a virtual call anyway
                                                                         // The vft child should share the bc info with the virtual call and no opt should be able to stick in a different expression at that precise spot in the IL trees

            && !firstChild->getSymbol()->castToMethodSymbol()->isComputed()) // 182550: We currently don't support profiling computed call targets because we have no way to map a target address to a MethodHandle object or to inline a thunk body
            {
            // todo: may have to implement an atoi opcode
            //
            bool picMiss = false;

            if (methodSymbol->isInterface())
               {
               TR::Method *interfaceMethod = methodSymbol->getMethod();
               int32_t         cpIndex      = methodSymRef->getCPIndex();
               int32_t len = interfaceMethod->classNameLength();
               char * s = classNameToSignature(interfaceMethod->classNameChars(), len, comp());
               TR_OpaqueClassBlock *thisClass = comp()->fe()->getClassFromSignature(s, len, methodSymRef->getOwningMethod(comp()));
               if (thisClass)
                  {
                  TR_PersistentCHTable * chTable = comp()->getPersistentInfo()->getPersistentCHTable();
                  picMiss = chTable->isKnownToHaveMoreThanTwoInterfaceImplementers(thisClass, cpIndex, methodSymRef->getOwningMethod(comp()), comp());
                  }
               }

            bool doit = true;

            if (firstChild->isTheVirtualCallNodeForAGuardedInlinedCall())
               {
               TR::Block * callBlock = tt->getEnclosingBlock();
               TR::Block * guard = callBlock->findVirtualGuardBlock(comp()->getFlowGraph());

               if (guard &&
                   (guard->getSuccessors().size() == 2) &&
                   guard->getExit() &&
                  (guard->getLastRealTreeTop()->getNode()->getOpCode().isBranch()) &&
                  (guard->getLastRealTreeTop()->getNode()->getBranchDestination() == callBlock->getEntry()))
                  {
                  TR::Node *callChild = callNode->getFirstChild();

                  if ((callChild->getOpCode().isLoadVarDirect() && callChild->getSymbolReference()->getSymbol()->isAutoOrParm()) ||
                      (callChild->getOpCode().hasSymbolReference() && (callChild->getSymbolReference() == comp()->getSymRefTab()->findVftSymbolRef())))
                     {
                     TR::Block *fallThroughBlock = guard->getNextBlock();
                     if (fallThroughBlock->getPredecessors().size() > 1)
                        fallThroughBlock = fallThroughBlock->splitEdge(guard, fallThroughBlock, comp());

                     TR::SymbolReference *callChildSymRef = callChild->getSymbolReference();
                     if ((callChild->getSymbolReference() == comp()->getSymRefTab()->findVftSymbolRef()) &&
                         callChild->getFirstChild()->getOpCode().hasSymbolReference())
                        callChildSymRef = callChild->getFirstChild()->getSymbolReference();

                     bool correctToClone = true;
                     TR::TreeTop *cursorTree = callBlock->getEntry();
                     while (cursorTree != tt)
                        {
                        TR::Node *cursorStoreNode = cursorTree->getNode()->getStoreNode();
                        if (cursorStoreNode &&
                            (cursorStoreNode->getSymbolReference() == callChildSymRef))
                           {
                           correctToClone = false;
                           break;
                           }

                       cursorTree = cursorTree->getNextTreeTop();
                       }

                     int32_t visitBudget = 20;
                     if (  correctToClone && !callChild->isUnsafeToDuplicateAndExecuteAgain(&visitBudget)
                        && performTransformation(comp(), "%s CALL PROFILE VERIFICATION: Duplicating node %p so it can be profiled near %p\n", callChild, fallThroughBlock->getEntry()))
                        {
                        TR::Node *dupChild = callChild->duplicateTree();

                        if (picMiss)
                           {
                           addProfilingTrees(dupChild, fallThroughBlock->getEntry(), 20);
                           }
                        else
                           {
                           addProfilingTrees(dupChild, fallThroughBlock->getEntry());
                           }
                        }
                     else
                        {
                        // We can't safely profile the fall-through path, so
                        // don't profile either path, or else we could get
                        // skewed profiling info.
                        //
                        doit = false;
                        }
                     }
                  }
               }

            if (doit)
               {
               if (picMiss)
                  {
                  addProfilingTrees(firstChild->getFirstChild(), tt, 20);
                  }
               else
                  addProfilingTrees(firstChild->getFirstChild(), tt);
               }
            }

         if (comp()->cg()->getSupportsBigDecimalLongLookasideVersioning() &&
             !methodSymRef->isUnresolved() && !methodSymbol->isHelper() /* && !firstChild->getByteCodeInfo().doNotProfile() */)
            {
            TR::ResolvedMethodSymbol *method = firstChild->getSymbol()->getResolvedMethodSymbol();
            if (method && ((method->getRecognizedMethod() == TR::java_math_BigDecimal_add) ||
                (method->getRecognizedMethod() == TR::java_math_BigDecimal_subtract) ||
                (method->getRecognizedMethod() == TR::java_math_BigDecimal_multiply)))
               {
               if (!firstChild->getByteCodeInfo().doNotProfile())
                  addProfilingTrees(firstChild, tt, 0, BigDecimalInfo);

               TR::Node *child = firstChild->getChild(firstChild->getNumChildren()-2);
               TR_ByteCodeInfo bcInfo = child->getByteCodeInfo();
               TR_ByteCodeInfo childBcInfo = firstChild->getByteCodeInfo();
               childBcInfo.setByteCodeIndex(childBcInfo.getByteCodeIndex()+1);

               child->setByteCodeInfo(childBcInfo);
               if (!child->getByteCodeInfo().doNotProfile())
                  addProfilingTrees(child, tt, 0, BigDecimalInfo);
               child->setByteCodeInfo(bcInfo);

               child = firstChild->getChild(firstChild->getNumChildren()-1);
               bcInfo = child->getByteCodeInfo();
               childBcInfo.setByteCodeIndex(childBcInfo.getByteCodeIndex()+1);

               child->setByteCodeInfo(childBcInfo);
               if (!child->getByteCodeInfo().doNotProfile())
                  addProfilingTrees(child, tt, 0, BigDecimalInfo);
               child->setByteCodeInfo(bcInfo);
               }
            }
         }
      else if ((node->getOpCodeValue() == TR::ificmpne) ||
               (node->getOpCodeValue() == TR::ificmpeq))
         {
         // Profile elementCount field in Hashtable so that
         // method Hashtable.elements can be optimized. If elementCount
         // was almost never zero then the 'if' that checks if elementCount is
         // zero and returns an empty enumerator can be eliminated
         //
         TR::Node *firstChild = node->getFirstChild();
         TR::Node *secondChild = node->getSecondChild();
         if ((firstChild->getOpCodeValue() == TR::iloadi) &&
             (secondChild->getOpCodeValue() == TR::iconst) &&
             (secondChild->getInt() == 0))
            {
            TR::SymbolReference *symRef = firstChild->getSymbolReference();
            if (symRef->getSymbol()->getRecognizedField() == TR::Symbol::Java_util_Hashtable_elementCount)
               addProfilingTrees(firstChild, tt->getPrevTreeTop(), 10);
            }
         }


      if (!getInitialCompilation())
         visitNode(node, tt, visitCount);
      }
   }

void
TR_ValueProfiler::visitNode(
      TR::Node *node,
      TR::TreeTop *tt,
      vcount_t visitCount)
   {
   if (node->getVisitCount() == visitCount)
      return;

   node->setVisitCount(visitCount);

   static char *profileLongParms = feGetEnv("TR_ProfileLongParms");
   if (profileLongParms &&
       (node->getType().isInt64()) &&
       node->getOpCode().isLoadVar() &&
       !node->getByteCodeInfo().doNotProfile())
      {
      TR::Node *intNode = TR::Node::create(TR::l2i, 1, TR::Node::create(TR::lushr, 2, node, TR::Node::create(node, TR::iconst, 0, 32)));
      TR::ILOpCode &placeHolderOpCode = tt->getNode()->getOpCode();
      if (placeHolderOpCode.isBranch() ||
          placeHolderOpCode.isJumpWithMultipleTargets() ||
          placeHolderOpCode.isReturn() ||
          placeHolderOpCode.getOpCodeValue() == TR::athrow)
        addProfilingTrees(intNode, tt->getPrevTreeTop());
      else
        addProfilingTrees(intNode, tt);
      }

   int32_t i;
   for (i = 0; i < node->getNumChildren(); i++)
     visitNode(node->getChild(i), tt, visitCount);
   }

bool
TR_ValueProfiler::validConfiguration(
      TR::DataType dataType,
      TR_ValueInfoKind kind)
   {
   TR::DataType rounded = dataType == TR::Int8 || dataType == TR::Int16 ? TR::Int32 : dataType;

   if ((kind == BigDecimalInfo || kind == AddressInfo || kind == StringInfo) && dataType != TR::Address)
      return false;
   if (kind == ValueInfo && rounded != TR::Int32)
      return false;
   if (kind == LongValueInfo && rounded != TR::Int64)
      return false;

   return true;
   }

void
TR_ValueProfiler::addProfilingTrees(
      TR::Node *node,
      TR::TreeTop *cursorTree,
      size_t numExpandedValues,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      bool commonNode,
      bool decrementRecompilationCounter)
   {
   addProfilingTrees(node, cursorTree, node->getByteCodeInfo(), numExpandedValues, kind, source,
      commonNode, decrementRecompilationCounter);
   }

/**
 * Insert value profiling trees
 *
 * \param node The value to profile.
 * \param cursorTree The tree to insert the profiling trees after.
 * \param bci ByteCodeInfo to stash the profiling information against.
 * \param numExpandedValues Optionally specify the number of additional values to track. Defaults to 0.
 * \param kind Optional kind to profile, will be checked against node's kind. Defaults to extracting the kind from node.
 * \param source Optional implementation to use, should be List, Array or HashTable. Defaults to ValueProfiler's configured default.
 * \param commonNode Optional argument to prevent the profiling trees from referencing the value node directly.
 * \param decrementRecompilationCounter Decrement the recompilation counter when a value is profiled. Not supported by all implementations.
 */
void
TR_ValueProfiler::addProfilingTrees(
      TR::Node *node,
      TR::TreeTop *cursorTree,
      TR_ByteCodeInfo &bci,
      size_t numExpandedValues,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      bool commonNode,
      bool decrementRecompilationCounter)
   {
   // Apply defaults or verify the node's value and kind match
   if (kind == LastValueInfo)
      {
      if (node->getDataType() == TR::Address)
         kind = AddressInfo;
      else if (node->getDataType() == TR::Int64)
         kind = LongValueInfo;
      else
         kind = ValueInfo;
      }
   else if (!validConfiguration(node->getDataType(), kind))
      {
      TR_ASSERT(0, "Invalid value profiling configuration");
      return;
      }

   if (source == LastProfiler)
      {
      source = _defaultProfiler;
      // Fallback on LinkedListProfiler if HashTableProfiler does not support the type
      if (kind == StringInfo || kind == BigDecimalInfo)
         source = LinkedListProfiler;
      }

   if (source == LinkedListProfiler || source == ArrayProfiler)
      addListOrArrayProfilingTrees(node, cursorTree, bci, numExpandedValues, kind, source, commonNode, decrementRecompilationCounter);
   else if (source == HashTableProfiler)
      addHashTableProfilingTrees(node, cursorTree, bci, kind, source, commonNode);
   }

void
TR_ValueProfiler::addHashTableProfilingTrees(
      TR::Node *node,
      TR::TreeTop *cursorTree,
      TR_ByteCodeInfo &bci,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      bool commonNode)
   {
   if (comp()->getOption(TR_DisableValueProfiling) ||
       !performTransformation(comp(), "%s VALUE PROFILER: Add JProfiling trees to track the value of node %p near tree %p, commonNode %d\n", OPT_DETAILS, node, cursorTree->getNode(), commonNode))
      return;

   TR_ValueProfileInfo *valueProfileInfo = _recompilation->findOrCreateProfileInfo()->findOrCreateValueProfileInfo(comp());
   TR_AbstractProfilerInfo *valueInfo = valueProfileInfo->getOrCreateProfilerInfo(bci, comp(), kind, source);

   if (_postLowering)
      {
      TR_JProfilingValue::addProfilingTrees(comp(), cursorTree, node, (TR_AbstractHashTableProfilerInfo *) valueInfo);
      }
   else
      {
      // Create a placeholder, which cannot be left in the jitted body
      TR::SymbolReference* profiler = comp()->getSymRefTab()->findOrCreateJProfileValuePlaceHolderSymbolRef();
      TR::Node *call = TR::Node::createWithSymRef(node, TR::call, 2, profiler);
      call->setAndIncChild(0, (commonNode ? node : node->duplicateTree()));
      call->setAndIncChild(1, TR::Node::aconst(node, (uintptr_t) valueInfo));
      TR::TreeTop *callTree = TR::TreeTop::create(comp(), cursorTree, TR::Node::create(TR::treetop, 1, call));
      callTree->getNode()->setIsProfilingCode();
      }
   }

void
TR_ValueProfiler::addListOrArrayProfilingTrees(
      TR::Node *node,
      TR::TreeTop *cursorTree,
      TR_ByteCodeInfo &bci,
      size_t numExpandedValues,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      bool commonNode,
      bool decrementRecompilationCounter)
   {
   bool validBigDecimalFieldOffset = true;
   int32_t scaleOffset = 0, flagOffset = 0;
   if (kind == BigDecimalInfo)
      {
      if (!_bdClass)
         {
         TR_ResolvedMethod *owningMethod = comp()->getCurrentMethod();
         _bdClass = comp()->fe()->getClassFromSignature("Ljava/math/BigDecimal;", 22, owningMethod);
         }
      TR_OpaqueClassBlock * bdClass = _bdClass;
      char *fieldName = "scale";
      int32_t fieldNameLen = 5;
      char *fieldSig = "I";
      int32_t fieldSigLen = 1;

      scaleOffset = comp()->fej9()->getInstanceFieldOffset(bdClass, fieldName, fieldNameLen, fieldSig, fieldSigLen);
      fieldName = "flags";
      flagOffset = comp()->fej9()->getInstanceFieldOffset(bdClass, fieldName, fieldNameLen, fieldSig, fieldSigLen);

      if (scaleOffset == -1)
         {
         fieldName = "cachedScale";
         fieldNameLen = 11;
         scaleOffset = comp()->fej9()->getInstanceFieldOffset(bdClass, fieldName, fieldNameLen, fieldSig, fieldSigLen);
         }

      if(scaleOffset == -1 || flagOffset == -1)
         {
         validBigDecimalFieldOffset = false;
         TR_ASSERT(false, "scale or flags offset not found from getInstanceFieldOffset(...) in addProfilingTrees\n");
         }

      flagOffset += (int32_t) comp()->fe()->getObjectHeaderSizeInBytes();  // size of a J9 object header to move past it
      scaleOffset += (int32_t) comp()->fe()->getObjectHeaderSizeInBytes();  // size of a J9 object header to move past it
      }

   bool validStringFieldOffset = true;
   int32_t charsOffset = 0, lengthOffset = 0;
   if (kind == StringInfo)
      {
      if (!_stringClass)
         {
         TR_ResolvedMethod *owningMethod = comp()->getCurrentMethod();
         _stringClass = comp()->fe()->getClassFromSignature("Ljava/lang/String;", 18, owningMethod);
         }

      TR_OpaqueClassBlock * stringClass = _stringClass;
      char *fieldName = "count";
      int32_t fieldNameLen = 5;
      char *fieldSig = "I";
      int32_t fieldSigLen = 1;

      lengthOffset = comp()->fej9()->getInstanceFieldOffset(stringClass, fieldName, fieldNameLen, fieldSig, fieldSigLen);
      fieldName = "value";
      fieldSig = "[C";
      fieldSigLen = 2;
      charsOffset = comp()->fej9()->getInstanceFieldOffset(stringClass, fieldName, fieldNameLen, fieldSig, fieldSigLen);

      if(charsOffset == -1 || lengthOffset == -1)
         {
         validStringFieldOffset = false;
         TR_ASSERT(false, "value or count offset not found from getInstanceFieldOffset(...) in addProfilingTrees\n");
         }

      lengthOffset += (int32_t) comp()->fe()->getObjectHeaderSizeInBytes();  // size of a J9 object header to move past it
      charsOffset += (int32_t) comp()->fe()->getObjectHeaderSizeInBytes();  // size of a J9 object header to move past it
      }

   if (!validBigDecimalFieldOffset || !validStringFieldOffset || comp()->getOption(TR_DisableValueProfiling) ||
       !performTransformation(comp(), "%s VALUE PROFILER: Add profiling trees to track the value of node %p near tree %p, commonNode %d, decrementRecompilationCounter %d, up to %d distinct values will be tracked \n", OPT_DETAILS, node, cursorTree->getNode(), commonNode, decrementRecompilationCounter, numExpandedValues))
      return;

   TR_ValueProfileInfo *valueProfileInfo = _recompilation->findOrCreateProfileInfo()->findOrCreateValueProfileInfo(comp());
   TR_AbstractProfilerInfo *valueInfo = valueProfileInfo->getOrCreateProfilerInfo(bci, comp(), kind, source);

   TR_RuntimeHelper helper;
   switch (kind)
      {
      case BigDecimalInfo:
         helper = TR_jitProfileBigDecimalValue;
         break;
      case StringInfo:
         helper = TR_jitProfileStringValue;
         break;
      case AddressInfo:
         helper = source == ArrayProfiler ? TR_jitProfileWarmCompilePICAddress : TR_jitProfileAddress;
         break;
      case LongValueInfo:
         helper = TR_jitProfileLongValue;
         break;
      case ValueInfo:
         helper = TR_jitProfileValue;
         break;

      default:
         break;
      }
   TR::SymbolReference *profiler = comp()->getSymRefTab()->findOrCreateRuntimeHelper(helper, false, false, true);
#if defined(TR_HOST_POWER) || defined(TR_HOST_ARM)
   profiler->getSymbol()->castToMethodSymbol()->setLinkage(TR_System);
#else
#ifndef TR_HOST_X86
   profiler->getSymbol()->castToMethodSymbol()->setPreservesAllRegisters();
#endif
   profiler->getSymbol()->castToMethodSymbol()->setSystemLinkageDispatch();
#endif

   TR::Node *recompilationCounter = TR::Node::aconst(node, 0);
   if (decrementRecompilationCounter)
      {
      TR::Recompilation *recompilationInfo   = comp()->getRecompilationInfo();
      TR::SymbolReference *recompilationCounterSymRef = recompilationInfo->getCounterSymRef();
      recompilationCounter = TR::Node::createWithSymRef(node, TR::loadaddr, 0, recompilationCounterSymRef);
      }

   int32_t numChildren;
   if (kind == BigDecimalInfo)
      numChildren = 7;
   else if (kind == StringInfo)
      numChildren = 6;
   else
      numChildren = 4;

   TR::Node *call = TR::Node::createWithSymRef(node, TR::call, numChildren, profiler);
   call->setAndIncChild(0, (commonNode ? node : node->duplicateTree()));

   int32_t childNum = 1;

   if (kind == BigDecimalInfo)
      {
      TR::Node *bdarg = TR::Node::aconst(node, (uintptr_t)_bdClass) ;
      bdarg->setIsClassPointerConstant(true);

      call->setAndIncChild(childNum++, bdarg);
      call->setAndIncChild(childNum++, TR::Node::create(node, TR::iconst, 0, scaleOffset));
      call->setAndIncChild(childNum++, TR::Node::create(node, TR::iconst, 0, flagOffset));
      }
   else if (kind == StringInfo)
      {
      call->setAndIncChild(childNum++, TR::Node::create(node, TR::iconst, 0, charsOffset));
      call->setAndIncChild(childNum++, TR::Node::create(node, TR::iconst, 0, lengthOffset));
      }

   TR::Node *arg = TR::Node::aconst(node, (uintptr_t)valueInfo) ;

   call->setAndIncChild(childNum++, arg);
   call->setAndIncChild(childNum++, TR::Node::create(node, TR::iconst, 0, numExpandedValues));
   call->setAndIncChild(childNum++, recompilationCounter);

   TR::TreeTop *callTree = TR::TreeTop::create(comp(), cursorTree, TR::Node::create(TR::treetop, 1, call));
   callTree->getNode()->setIsProfilingCode();
   }

TR_ValueProfileInfoManager::TR_ValueProfileInfoManager(TR::Compilation *comp)
   {
   _jitValueProfileInfo = NULL;
   _jitBlockFrequencyInfo = NULL;
   _cachedJ9Method = NULL;
   _isCountZero = false;

   // Load persistent profiling information from a prior compilation
   TR_PersistentProfileInfo *profileInfo = TR_PersistentProfileInfo::get(comp);

   if (profileInfo &&
       profileInfo->getValueProfileInfo())
      _jitValueProfileInfo = profileInfo->getValueProfileInfo();

   if (profileInfo &&
       profileInfo->getBlockFrequencyInfo())
      _jitBlockFrequencyInfo = profileInfo->getBlockFrequencyInfo();
   }

TR_AbstractInfo *
TR_ValueProfileInfoManager::getProfiledValueInfo(TR::Node *node, TR::Compilation *comp, TR_ValueInfoKind kind,
      uint32_t source)
   {
   TR_ValueProfileInfoManager *manager = TR_ValueProfileInfoManager::get(comp);
   return manager ? manager->getValueInfo(node, comp, kind, source) : NULL;
   }

TR_AbstractInfo *
TR_ValueProfileInfoManager::getProfiledValueInfo(TR_ByteCodeInfo &bci, TR::Compilation *comp, TR_ValueInfoKind kind,
      uint32_t source)
   {
   TR_ValueProfileInfoManager *manager = TR_ValueProfileInfoManager::get(comp);
   return manager ? manager->getValueInfo(bci, comp, kind, source) : NULL;
   }

TR_AbstractInfo *
TR_ValueProfileInfoManager::getValueInfo(TR::Node *node, TR::Compilation *comp, TR_ValueInfoKind kind,
      uint32_t source)
   {
   return getValueInfo(node->getByteCodeInfo(), comp, kind, source);
   }

TR_AbstractInfo *
TR_ValueProfileInfoManager::getValueInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind,
      uint32_t source)
   {
   TR_AbstractInfo *info = NULL;
   bool internal = _jitValueProfileInfo && (source == allProfileInfo || source == justJITProfileInfo);
   bool external = source == allProfileInfo || source == justInterpreterProfileInfo;

   if (internal)
      {
      if (!info || info->getTotalFrequency() == 0)
         info = _jitValueProfileInfo->getValueInfo(bcInfo, comp, kind, HashTableProfiler, true);

      if (!info || info->getTotalFrequency() == 0)
         info = _jitValueProfileInfo->getValueInfo(bcInfo, comp, kind, LinkedListProfiler, true);

      if (!info || info->getTotalFrequency() == 0)
         info = _jitValueProfileInfo->getValueInfo(bcInfo, comp, kind, ArrayProfiler, true);
      }

   if (external && (!info || info->getTotalFrequency() == 0))
      {
      TR_ExternalValueProfileInfo *iVPInfo = comp->fej9()->getValueProfileInfoFromIProfiler(bcInfo, comp);
      if (iVPInfo)
         info = iVPInfo->getValueInfo(bcInfo, comp);
      }
   return info;
   }

int32_t
TR_ValueProfileInfoManager::getCallGraphProfilingCount(TR_OpaqueMethodBlock * method, int32_t byteCodeIndex, TR::Compilation * comp)
   {
   // the call count information is stored in the interpreter profiler
   // persistent database
   return comp->fej9()->getIProfilerCallCount(method, byteCodeIndex, comp);
   }

int32_t
TR_ValueProfileInfoManager::getCallGraphProfilingCount(TR_OpaqueMethodBlock *calleeMethod, TR_OpaqueMethodBlock * method, int32_t byteCodeIndex, TR::Compilation * comp)
   {
   // the call count information is stored in the interpreter profiler
   // persistent database
   return comp->fej9()->getIProfilerCallCount(calleeMethod, method, byteCodeIndex, comp);
   }

int32_t
TR_ValueProfileInfoManager::getCallGraphProfilingCount(TR::Node *node, TR_OpaqueMethodBlock *method, TR::Compilation *comp)
   {
   return comp->fej9()->getCGEdgeWeight(node, method, comp);
   }

int32_t
TR_ValueProfileInfoManager::getCallGraphProfilingCount(TR::Node *node, TR::Compilation *comp)
   {
   return comp->fej9()->getIProfilerCallCount(node->getByteCodeInfo(), comp);
   }

bool
TR_ValueProfileInfoManager::isColdCall(TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp)
   {
   return (getCallGraphProfilingCount(method, byteCodeIndex, comp) < (comp->getFlowGraph()->getLowFrequency()));
   }

bool
TR_ValueProfileInfoManager::isColdCall(TR_OpaqueMethodBlock *calleeMethod, TR_OpaqueMethodBlock *method, int32_t byteCodeIndex, TR::Compilation *comp)
   {
   return (getCallGraphProfilingCount(calleeMethod, method, byteCodeIndex, comp) < (comp->getFlowGraph()->getLowFrequency()));
   }

bool
TR_ValueProfileInfoManager::isColdCall(TR::Node* node, TR::Compilation *comp)
   {
   return (comp->fej9()->getIProfilerCallCount(node->getByteCodeInfo(), comp) < (comp->getFlowGraph()->getLowFrequency()));
   }

bool
TR_ValueProfileInfoManager::isWarmCall(TR::Node* node, TR::Compilation *comp)
   {
   return (comp->fej9()->getIProfilerCallCount(node->getByteCodeInfo(), comp) < (comp->getFlowGraph()->getLowFrequency()<<1));
   }

bool
TR_ValueProfileInfoManager::isHotCall(TR::Node* node, TR::Compilation *comp)
   {
   int32_t maxCallCount = comp->fej9()->getMaxCallGraphCallCount();

   // if the maximum call count is very low then don't bother
   if (maxCallCount < (comp->getFlowGraph()->getLowFrequency()<<1))
      return false;

   int32_t currentCallCount = comp->fej9()->getIProfilerCallCount(node->getByteCodeInfo(), comp);
   float ratio = (float)currentCallCount/(float)maxCallCount;

   // if the current call count is within 80% of the maximum call count in the program
   // it's hot and help out inliner to boost its inlining priority
   return (ratio >= 0.8f);
   }

float
TR_ValueProfileInfoManager::getAdjustedInliningWeight(TR::Node *callNode, int32_t weight, TR::Compilation *comp)
   {
   if (!isCallGraphProfilingEnabled(comp))
      return (float)weight;

   float callGraphAdjustedWeight = (float)weight;
   int32_t callGraphWeight = getCallGraphProfilingCount(callNode, comp);

   if (isWarmCall(callNode, comp))
      callGraphAdjustedWeight = 5000.0f;
   else if (isHotCall(callNode, comp))
      {
      if (weight < 0)
         callGraphAdjustedWeight = ((float)weight)*1.5f;
      else
         callGraphAdjustedWeight = ((float)weight)/1.5f;
      }

   return callGraphAdjustedWeight;
   }

bool
TR_ValueProfileInfoManager::isWarmCallGraphCall(TR::Node *node, TR_OpaqueMethodBlock *method, TR::Compilation *comp)
   {
   if (node->getSymbolReference() && node->getSymbolReference()->getSymbol() &&
       ((node->getSymbolReference()->getSymbol()->castToMethodSymbol()->getMethodKind()==TR::MethodSymbol::Special) ||
        (node->getSymbolReference()->getSymbol()->castToMethodSymbol()->getMethodKind()==TR::MethodSymbol::Static)))
      return false;

   return (getCallGraphProfilingCount(node, method, comp) < comp->getFlowGraph()->getLowFrequency());
   }

bool
TR_ValueProfileInfoManager::isCallGraphProfilingEnabled(TR::Compilation *comp)
   {
   if (comp->getCurrentMethod()->getPersistentIdentifier() == _cachedJ9Method)
      {
      if (_isCountZero)
         return false;
      }
   else
      {
      _cachedJ9Method = comp->getCurrentMethod()->getPersistentIdentifier();

      if (TR::Options::getCmdLineOptions()->getInitialCount() == 0 ||
          TR::Options::getCmdLineOptions()->getInitialBCount())
         {
         _isCountZero = true;
         return false;
         }

      TR::OptionSet * optionSet = TR::Options::findOptionSet(comp->trMemory(), comp->getCurrentMethod(), false);

      if (optionSet &&
          (optionSet->getOptions()->getInitialCount() == 0 ||
          (optionSet->getOptions()->getInitialBCount() == 0)))
         {
         _isCountZero = true;
         return false;
         }
      }

   return comp->fej9()->isCallGraphProfilingEnabled();
   }

void
TR_ValueProfileInfoManager::updateCallGraphProfilingCount(
      TR::Block *block,
      TR_OpaqueMethodBlock *method,
      int32_t byteCodeIndex,
      TR::Compilation *comp)
   {

   if(comp->getMethodHotness() > hot)  // don't update using jit profiling info
      return;

   // Cannot use JIT profiling info on dummy blocks from ECS.
   int32_t currentBlockFreq = 0;

   // if we don't have proper JIT profiling info get it using interpreter profiler
   // set frequency
   //if (currentBlockFreq <= 0)
   currentBlockFreq = block->getFrequency();

   if (currentBlockFreq > 0)
      {
      comp->fej9()->setIProfilerCallCount(method, byteCodeIndex, currentBlockFreq, comp);
      }
   }

float
TR_BranchProfileInfoManager::getCallFactor(int32_t callSiteIndex, TR::Compilation *comp)
   {
   float callFactor = 1.0;

   if (_iProfiler == NULL)
      return callFactor;

   // Speed-up te search by leveraging the fact that we are searching
   // all elements from one list into another list and both are sorted
   TR::list<TR_MethodBranchProfileInfo*> &infos = comp->getMethodBranchInfos();
   auto info = infos.begin(); // first element in my list of TR_MethodBranchProfileInfo

   int32_t calleeSiteIndex = callSiteIndex;
   while (calleeSiteIndex >= 0)
      {
      // Search for mbpInfo from where we left off
      for (; (info != infos.end()) && (*info)->getCallSiteIndex() > calleeSiteIndex; ++info)
         {}
      if (info == infos.end())
         break; // stop searching because we reached the end of the list with infos
      if ((*info)->getCallSiteIndex() == calleeSiteIndex)
         callFactor *= (*info)->getCallFactor();

      // Prepare for next iteration by getting to the parent of this caller
      TR_InlinedCallSite & site = comp->getInlinedCallSite(calleeSiteIndex);
      int32_t parentSiteIndex = site._byteCodeInfo.getCallerIndex();
      TR_ASSERT(parentSiteIndex < calleeSiteIndex, "Caller site index should be smaller than the parentSiteIndex");
      calleeSiteIndex = parentSiteIndex;
      }

   return callFactor;
   }

void
TR_BranchProfileInfoManager::getBranchCounters(TR::Node *node, TR::TreeTop *treeTop,
                             int32_t *branchToCount, int32_t *fallThroughCount, TR::Compilation *comp)
   {
   if (_iProfiler == NULL)
      {
      *branchToCount = 0;
      *fallThroughCount = 0;

      return;
      }

   TR_MethodBranchProfileInfo *mbpInfo = TR_MethodBranchProfileInfo::getMethodBranchProfileInfo(node->getInlinedSiteIndex(), comp);

   if (comp->getOption(TR_TraceBFGeneration))
      traceMsg(comp, "mbpInfo %p\n", mbpInfo);

   if (mbpInfo && node->getInlinedSiteIndex()>=0)
      {
      _iProfiler->getBranchCounters(node, treeTop, branchToCount, fallThroughCount, comp);

      float callFactor = getCallFactor(node->getInlinedSiteIndex(), comp);

      if (comp->getOption(TR_TraceBFGeneration))
         {
         traceMsg(comp, "Using call factor %f for callSiteIndex %d\n", callFactor, node->getInlinedSiteIndex());
         traceMsg(comp, "Orig branch to count %d and fall through count %d\n", *branchToCount, *fallThroughCount);
         }

      if ((*branchToCount <=0) && (*fallThroughCount<=0))
         {
         if (node->getBranchDestination()->getNode()->getBlock()->isCold())
            {
            *branchToCount = 0;

            return;
            }
         else
            *branchToCount = LOW_FREQ;

         if (treeTop->getEnclosingBlock()->getNextBlock() &&
             treeTop->getEnclosingBlock()->getNextBlock()->isCold())
            {
            *fallThroughCount = 0;

            return;
            }
         else
            *fallThroughCount = LOW_FREQ;

         }
      else
         {
         if (*branchToCount <=0) *branchToCount=1;
         if (*fallThroughCount<=0) *fallThroughCount=1;
         }

      if (comp->getOption(TR_TraceBFGeneration))
         traceMsg(comp, "Later branch to count %d and fall through count %d\n", *branchToCount, *fallThroughCount);

      int32_t breakEven = (*branchToCount > *fallThroughCount) ? 1 : -1;
      if (*branchToCount == *fallThroughCount) breakEven = 0;

      float edgeRatio = ((float)(*branchToCount) / (float)(*fallThroughCount));

      *branchToCount = (int32_t) ((float)(*branchToCount) * callFactor);
      *fallThroughCount = (int32_t) ((float)(*fallThroughCount) * callFactor);

      if (*branchToCount >= comp->getFlowGraph()->_max_edge_freq || *fallThroughCount >= comp->getFlowGraph()->_max_edge_freq)
         {
         if (breakEven > 0)
            {
            *branchToCount = comp->getFlowGraph()->_max_edge_freq;
            *fallThroughCount = (int32_t) ((float)comp->getFlowGraph()->_max_edge_freq / edgeRatio);
            }
         else
            {
            *fallThroughCount = comp->getFlowGraph()->_max_edge_freq;
            *branchToCount = (int32_t) ((float)comp->getFlowGraph()->_max_edge_freq * edgeRatio);
            }
         }

      if (((*branchToCount + breakEven) >= 0) && (*branchToCount == *fallThroughCount)) *branchToCount += breakEven;
      }
   else
      {
      _iProfiler->getBranchCounters(node, treeTop, branchToCount, fallThroughCount, comp);
      }
   }

TR_MethodBranchProfileInfo *
TR_MethodBranchProfileInfo::addMethodBranchProfileInfo (uint32_t callSiteIndex, TR::Compilation *comp)
   {
   // There is an embedded assumption that the new elements added to the list are in
   // increasing order. This helps keep the list of methodBranchProfileInfo sorted
   // which in turn speeds up searches.
   TR_ASSERT(comp->getMethodBranchInfos().empty() ||
             (int32_t)callSiteIndex > comp->getMethodBranchInfos().front()->getCallSiteIndex(),
             "Adding to MethodBranchProfileInfo must be done in order");
   TR_MethodBranchProfileInfo *methodBPInfo = new (comp->trHeapMemory()) TR_MethodBranchProfileInfo(callSiteIndex, comp);
   comp->getMethodBranchInfos().push_front(methodBPInfo);

   return methodBPInfo;
   }

TR_MethodBranchProfileInfo *
TR_MethodBranchProfileInfo::getMethodBranchProfileInfo(uint32_t callSiteIndex, TR::Compilation *comp)
   {
   TR::list<TR_MethodBranchProfileInfo*> &infos = comp->getMethodBranchInfos();

   // list is sorted in descending order; let's jump over elements that are bigger
   auto info = infos.begin();
   for (;  (info != infos.end()) && (*info)->getCallSiteIndex() > callSiteIndex; ++info)
      {}
   if ((info != infos.end()) && (*info)->getCallSiteIndex() == callSiteIndex)
      {
      return *info;
      }
   return NULL;
   }

void
TR_MethodBranchProfileInfo::resetMethodBranchProfileInfos(int32_t oldMaxFrequency, int32_t oldMaxEdgeFrequency, TR::Compilation *comp)
   {
   TR::list<TR_MethodBranchProfileInfo*> &infos = comp->getMethodBranchInfos();
   for (auto info = infos.begin(); info != infos.end(); ++info)
      {
      (*info)->_oldMaxFrequency = oldMaxFrequency;
      (*info)->_oldMaxEdgeFrequency = oldMaxEdgeFrequency;
      (*info)->setCallFactor(1.0f);
      }
   }

/**
 * Create a new TR_ExternalValueProfileInfo to represent profiling results from an external profiler.
 * The result is added to the list of TR_ExternalValueProfileInfo on the current compilation.
 *
 * \param method The profiled method.
 * \param profiler The external profiler.
 * \param comp The current compilation.
 * \return The created TR_ExternalValueProfileInfo.
 */
TR_ExternalValueProfileInfo*
TR_ExternalValueProfileInfo::addInfo(TR_OpaqueMethodBlock *method, TR_ExternalProfiler *profiler, TR::Compilation *comp)
   {
   TR_ExternalValueProfileInfo *methodVPInfo = new (comp->trHeapMemory()) TR_ExternalValueProfileInfo(method, profiler);
   comp->getExternalVPInfos().push_front(methodVPInfo);
   return methodVPInfo;
   }

/**
 * Search the current compilation for a TR_ExternalValueProfileInfo with the specified method.
 *
 * \param method The method to search for.
 * \param comp The current compilation.
 * \return The existing TR_ExternalValueProfileInfo or NULL if nothing was found.
 */
TR_ExternalValueProfileInfo*
TR_ExternalValueProfileInfo::getInfo(TR_OpaqueMethodBlock *method, TR::Compilation *comp)
   {
   TR::list<TR_ExternalValueProfileInfo*> &infos = comp->getExternalVPInfos();
   for (auto info = infos.begin(); info != infos.end(); ++info)
      {
      if ((*info)->getPersistentIdentifier() == method)
         return *info;
      }

   return NULL;
   }

/**
 * Get value profiling information from the external profiler.
 * Currently, this is limited to returning TR_AddressInfo.
 *
 * \param bcInfo Bytecode info to specify the desired value.
 * \param comp The current compilation.
 * \return Abstract information for the desired BCI. Will always be TR_AddressInfo.
 */
TR_AbstractInfo *
TR_ExternalValueProfileInfo::getValueInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp)
   {
   for (TR_AbstractInfo *valueInfo = _info; valueInfo; valueInfo = valueInfo->getNext())
      if (_profiler->hasSameBytecodeInfo(valueInfo->getByteCodeInfo(), bcInfo, comp))
         return valueInfo;

   return comp->fej9()->createIProfilingValueInfo(bcInfo, comp);
   }

/**
 * Create address info to be filled with information from an external profiler.
 *
 * \param bcInfo Bytecode info to specify the desired value.
 * \param comp The current compilation.
 * \param initialValue The initial value to add to the list.
 * \param initialFreq The initial frequency.
 * \param list Optional argument to retrieve the linked list profiler.
 * \return The created abstract information.
 */
TR_AbstractInfo *
TR_ExternalValueProfileInfo::createAddressInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, uintptr_t initialValue,
    uint32_t initialFreq, TR_LinkedListProfilerInfo<ProfileAddressType> **list)
   {
   TR::Region &region = comp->trMemory()->heapMemoryRegion();

   // Create the linked list profiler
   auto profiler = new (region) TR_LinkedListProfilerInfo<ProfileAddressType>(bcInfo, AddressInfo, initialValue, initialFreq, true);
   if (list)
      *list = profiler;

   // Create the abstract info and add it to the list
   TR_AbstractInfo *info = profiler->getAbstractInfo(region);
   info->setNext(_info);
   _info = info;
   return info;
   }

/**
 * Create TR_ValueProfileInfo, initializing the list of profiling sources.
 *
 * \param info Persistent call site info, so that information can be information matched in future compiles.
 *             The lifetime of this object is managed by TR_PersistentProfileInfo.
 */
TR_ValueProfileInfo::TR_ValueProfileInfo(TR_CallSiteInfo *info) : _callSiteInfo(info)
   {
   for (size_t i = 0; i < LastProfiler; ++i)
      _values[i] = NULL;
   }

/**
 * Deallocate all persistent profiling information.
 * The call site information is not deallocated as it is managed by TR_PersistentProfileInfo.
 */
TR_ValueProfileInfo::~TR_ValueProfileInfo()
   {
   TR_AbstractProfilerInfo *tmpInfo;
   _callSiteInfo = NULL;
   for (size_t i = 0; i < LastProfiler; ++i)
      {
      while (_values[i])
         {
         tmpInfo = _values[i];
         _values[i] = _values[i]->getNext();
         tmpInfo->~TR_AbstractProfilerInfo();
         jitPersistentFree(tmpInfo);
         }
      }
   }

/**
 * Extract persistent value profiling information and convert it to the common profiling information API. A region can be specified,
 * otherwise, it will be allocated in the current stack region.
 *
 * \param bcInfo Bytecode info to find profiling information for.
 * \param comp Current compilation.
 * \param kind Kind of profiling information to find.
 * \param source Source of the profiling information.
 * \param fuzz Flag to enable fuzzy matching. Defaults to false.
 * \param region Region to allocate in.
 * \return The matched profiling information or NULL if nothing was present.
 */
TR_AbstractInfo *
TR_ValueProfileInfo::getValueInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind, TR_ValueInfoSource source, bool fuzz, TR::Region *optRegion)
   {
   TR_AbstractProfilerInfo *info = getProfilerInfo(bcInfo, comp, kind, source, fuzz);

   if (!info)
      return NULL;

   TR_ASSERT(kind == info->getKind() && source == info->getSource(), "Profiler with invalid kind or source returned");
   TR_AbstractInfo *valueInfo = info->getAbstractInfo(optRegion ? *optRegion : comp->trMemory()->currentStackRegion());

   return valueInfo;
   }

/**
 * Extract persistent value profiling information. It will attempt to exactly match kind and source, falling back on a fuzzy match for
 * BCI if an exact cannot be found.
 *
 * \param bcInfo Bytecode info to find profiling information for.
 * \param comp Current compilation.
 * \param kind Kind of profiling information to find.
 * \param source Source of the profiling information.
 * \param fuzz Flag to enable fuzzy matching. Defaults to false.
 * \return The matched profiling information or NULL if nothing was present.
 */
TR_AbstractProfilerInfo *
TR_ValueProfileInfo::getProfilerInfo(TR_ByteCodeInfo &bcInfo, TR::Compilation *comp, TR_ValueInfoKind kind, TR_ValueInfoSource source, bool fuzz)
   {
   TR_ASSERT(kind < LastValueInfo && source < LastProfiler,  "Invalid value info request, kind %d source %d\n", kind, source);
   TR_ASSERT(_callSiteInfo, "No call site information specified");

   // All other sources compare the current compilation call sites with the stashed one
   for (TR_AbstractProfilerInfo *valueInfo = _values[source]; valueInfo; valueInfo = valueInfo->getNext())
      {
      if (valueInfo->getKind() == kind && _callSiteInfo->hasSameBytecodeInfo(valueInfo->getByteCodeInfo(), bcInfo, comp))
         return valueInfo;
      }

   // If this TR_ValueProfileInfo is from a prior compile, attempt to fuzzy match the BCI
   if (fuzz)
      {
      TR_AbstractProfilerInfo *bestMatchedValueInfo = NULL;
      int32_t maxMatchedCount = 0;
      int32_t matchCount = 0;
      for (TR_AbstractProfilerInfo *valueInfo = _values[source]; valueInfo; valueInfo = valueInfo->getNext())
         {
         if (valueInfo->getKind() == kind &&
             (matchCount = _callSiteInfo->hasSamePartialBytecodeInfo(valueInfo->getByteCodeInfo(), bcInfo, comp)) > maxMatchedCount)
            {
            bestMatchedValueInfo = valueInfo;
            maxMatchedCount = matchCount;
            }
         }

      if (maxMatchedCount > 0)
         return bestMatchedValueInfo;
      }

   return NULL;
   }

/**
 * Generate new persistent value profiling information for the provided arguments. Will be allocated in persistent memory
 * and added to the corresponding list.
 *
 * \param bcInfo Bytecode information for the new value.
 * \param comp Current compilation.
 * \param kind Kind of information to profile.
 * \param source Profiling implementation.
 * \param initialValue Optional initial value. Not supported by all profilers or all kinds.
 * \return New persistent profiling information for use with JIT instrumentation, or NULL if an error was encountered.
 */
TR_AbstractProfilerInfo *
TR_ValueProfileInfo::createAndInitializeProfilerInfo(
      TR_ByteCodeInfo &bcInfo,
      TR::Compilation *comp,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      uint64_t initialValue)
   {
   uint32_t initialFreq = 0;
   if (initialValue != CONSTANT64(0xdeadf00ddeadf00d))
      initialFreq = 10;

   // Does not support an initial value
   TR_ByteInfo bytes;

   // If index is supported, prefer it
   TR_HashFunctionType hash = BitShiftHash;
   if (comp->cg()->getSupportsBitPermute())
      hash = BitIndexHash;

   TR_AbstractProfilerInfo *valueInfo = NULL;
   switch (kind)
      {
      case ValueInfo:
         if (source == HashTableProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_EmbeddedHashTable<uint32_t, 2>(bcInfo, hash, kind);
         else if (source == LinkedListProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_LinkedListProfilerInfo<uint32_t>(bcInfo, kind, initialValue, initialFreq);
         break;

      case AddressInfo:
         if (source == HashTableProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_EmbeddedHashTable<ProfileAddressType, 2>(bcInfo, hash, kind);
         else if (source == LinkedListProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_LinkedListProfilerInfo<ProfileAddressType>(bcInfo, kind, initialValue, initialFreq);
         else if (source == ArrayProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_ArrayProfilerInfo<ProfileAddressType>(bcInfo, kind);
         break;

      case LongValueInfo:
      case BigDecimalInfo:
         if (source == HashTableProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_EmbeddedHashTable<uint64_t, 2>(bcInfo, hash, kind);
         else if (source == LinkedListProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_LinkedListProfilerInfo<uint64_t>(bcInfo, kind, initialValue, initialFreq);
         break;

      case StringInfo:
         if (source == LinkedListProfiler)
            valueInfo = new (comp->trPersistentMemory()) TR_LinkedListProfilerInfo<TR_ByteInfo>(bcInfo, kind, bytes, initialFreq);
         break;

      default:
         break;
      }

   if (!valueInfo)
      {
      TR_ASSERT(0, "Unsupported value info request, kind %d source %d\n", kind, source);
      return NULL;
      }

   valueInfo->setNext(_values[source]);
   _values[source] = valueInfo;
   return valueInfo;
   }

/**
 * Find or create persistent value profiling information with the specified properties.
 *
 * \param bcInfo Bytecode information for the new value.
 * \param comp Current compilation.
 * \param kind Kind of information to profile.
 * \param source Profiling implementation.
 * \param initialValue Optional initial value. Not supported by all profilers or all kinds.
 * \return Either old or new value information. NULL if an error was encountered.
 */
TR_AbstractProfilerInfo *
TR_ValueProfileInfo::getOrCreateProfilerInfo(
      TR_ByteCodeInfo &bcInfo,
      TR::Compilation *comp,
      TR_ValueInfoKind kind,
      TR_ValueInfoSource source,
      uint64_t initialValue)
   {
   TR_AbstractProfilerInfo *profilerInfo = getProfilerInfo(bcInfo, comp, kind, source);

   if (!profilerInfo)
      profilerInfo = createAndInitializeProfilerInfo(bcInfo, comp, kind, source, initialValue);

   return profilerInfo;
   }

/**
 * Run through all hash tables and determine if their contents should be cleared out.
 * Only supports resetting on hash tables.
 *
 * \param profileLog Dump all the tables that have been inspected, as well as a log of any that were cleared. NULL will disable tracing.
 */
void
TR_ValueProfileInfo::resetLowFreqValues(TR::FILE *profileLog)
   {
   for (TR_AbstractProfilerInfo *valueInfo = _values[HashTableProfiler]; valueInfo; valueInfo = valueInfo->getNext())
      {
      TR_AbstractHashTableProfilerInfo *hashTable = static_cast<TR_AbstractHashTableProfilerInfo*>(valueInfo);
      if (profileLog)
         hashTable->dumpInfo(profileLog);
      if (!hashTable->isFull())
         continue;
      if (hashTable->resetLowFreqKeys())
         {
         if (profileLog)
            J9::IO::fprintf(profileLog, "Resetting info 0x%p\n", hashTable);
         if (TR::Options::isAnyVerboseOptionSet(TR_VerboseProfiling))
            TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Resetting info 0x%p.", hashTable);
         }
      }
   }

int32_t TR_BlockFrequencyInfo::_enableJProfilingRecompilation = -1;

TR_BlockFrequencyInfo::TR_BlockFrequencyInfo(TR_CallSiteInfo *callSiteInfo, int32_t numBlocks, TR_ByteCodeInfo *blocks, int32_t *frequencies) :
   _callSiteInfo(callSiteInfo),
   _numBlocks(numBlocks),
   _blocks(blocks),
   _frequencies(frequencies),
   _counterDerivationInfo(NULL),
   _entryBlockNumber(-1),
   _isQueuedForRecompilation(0)
   {
   }

TR_BlockFrequencyInfo::TR_BlockFrequencyInfo(
   TR::Compilation *comp,
   TR_AllocationKind allocKind) :
   _callSiteInfo(TR_CallSiteInfo::getCurrent(comp)),
   _numBlocks(comp->getFlowGraph()->getNextNodeNumber()),
   _blocks(
      _numBlocks ?
      new (comp->trMemory(), allocKind, TR_Memory::BlockFrequencyInfo) TR_ByteCodeInfo[_numBlocks] :
      0
      ),
   _frequencies(
      _numBlocks ?
      /*
       * The explicit parens value initializes the array,
       * which in turn value initializes each array member,
       * which for ints is zero initialization.
       */
      new (comp->trMemory(), allocKind, TR_Memory::BlockFrequencyInfo) int32_t[_numBlocks]() :
      NULL
      ),
   _counterDerivationInfo(NULL),
   _entryBlockNumber(-1),
   _isQueuedForRecompilation(0)
   {
   for (size_t i = 0; i < _numBlocks; ++i)
      {
      _blocks[i].setDoNotProfile(true);
      _blocks[i].setIsSameReceiver(false);
      _blocks[i].setInvalidCallerIndex();
      _blocks[i].setInvalidByteCodeIndex();
      }

   for (TR::CFGNode *node = comp->getFlowGraph()->getFirstNode(); node; node = node->getNext())
      {
      TR::TreeTop *startTree = node->asBlock()->getEntry();
      if (startTree)
         {
         TR_ByteCodeInfo &byteCodeInfo = startTree->getNode()->getByteCodeInfo();
         int32_t callSite = byteCodeInfo.getCallerIndex();
         TR_ASSERT(callSite < static_cast<int32_t>(TR_CallSiteInfo::getCurrent(comp)->getNumCallSites() & 0x7FFFFFFF), "Block call site number is unexpected");

         _blocks[node->getNumber()] = byteCodeInfo;
         }
      else
         {
         TR_ASSERT(
            node->getNumber() == 0 || node->getNumber() == 1,
            "Only the entry and exit nodes are expected not to have an entry tree."
            );
         }
      }
   }

TR_BlockFrequencyInfo::~TR_BlockFrequencyInfo()
   {
   _callSiteInfo = NULL;
   if (_blocks)
      {
      jitPersistentFree(_blocks);
      }
   if (_frequencies)
      {
      jitPersistentFree(_frequencies);
      }

   if (_counterDerivationInfo)
      {
      for (int i = 0; i < 2 * _numBlocks; ++i)
         {
         if (_counterDerivationInfo[i] &&
            ((int64_t)_counterDerivationInfo[i] & 0x1) != 1)
            {
            // Free the chunks held by each bitvector using setSize(0)
            _counterDerivationInfo[i]->setSize(0);
            // Free each bitvector
            jitPersistentFree(_counterDerivationInfo[i]);
            _counterDerivationInfo[i] = NULL;
            }
         }
      jitPersistentFree(_counterDerivationInfo);
      _counterDerivationInfo = NULL;
      }
   }

int32_t
TR_BlockFrequencyInfo::getFrequencyInfo(
      TR::Block *block,
      TR::Compilation *comp)
   {

   if (!block->getEntry())
      return -1;

   TR::Node *startNode = block->getEntry()->getNode();
   TR_ByteCodeInfo bci = startNode->getByteCodeInfo();
   bool normalizeForCallers = true;
   if (bci.getCallerIndex() == -10)
      {
      bci.setCallerIndex(comp->getCurrentInlinedSiteIndex());
      normalizeForCallers = false;
      }
   int32_t frequency = getFrequencyInfo(bci, comp, normalizeForCallers, comp->getOption(TR_TraceBFGeneration));
   if (comp->getOption(TR_TraceBFGeneration))
      traceMsg(comp, "@@ block_%d [%d,%d] has raw count %d\n", block->getNumber(), bci.getCallerIndex(), bci.getByteCodeIndex(), frequency);
   return frequency;
   }

int32_t
TR_BlockFrequencyInfo::getFrequencyInfo(
      TR_ByteCodeInfo &bci,
      TR::Compilation *comp,
      bool normalizeForCallers,
      bool trace)
   {
   int64_t maxCount = normalizeForCallers ? getMaxRawCount() : getMaxRawCount(bci.getCallerIndex());
   int32_t callerIndex = bci.getCallerIndex();
   int32_t frequency = getRawCount(callerIndex < 0 ? comp->getMethodSymbol() : comp->getInlinedResolvedMethodSymbol(callerIndex), bci, _callSiteInfo, maxCount, comp);
   if (trace)
      traceMsg(comp,"raw frequency on outter level was %d for bci %d:%d\n", frequency, bci.getCallerIndex(), bci.getByteCodeIndex());
   if (frequency > -1 || _counterDerivationInfo == NULL)
      return frequency;

   // if we still don't have any data we didn't inline this method previously
   // so now we need to look at profiling data from other JProfiled bodies in the
   // hope that we have data there
   if (callerIndex > -1)
      {
      if (trace)
         traceMsg(comp, "Previous inlining was different - looking for a grafting point\n");

      // step 1 - build the callstack in reverse order for searching
      TR_ByteCodeInfo bciToCheck = bci;
      TR::list<std::pair<TR_OpaqueMethodBlock*, TR_ByteCodeInfo> > callStack(comp->allocator());
      while (bciToCheck.getCallerIndex() > -1)
         {
         TR_InlinedCallSite *callSite = &comp->getInlinedCallSite(bciToCheck.getCallerIndex());
         callStack.push_back(std::make_pair(comp->fe()->getInlinedCallSiteMethod(callSite), bciToCheck));
         bciToCheck = callSite->_byteCodeInfo;
         }

      // step 2 - find the level at which the inlining has begun to differ for the previous compile
      // eg find the point where the current profiling info has no profiling data for the given bci
      TR_ByteCodeInfo lastProfiledBCI = bciToCheck;
      int64_t outterProfiledFrequency = getRawCount(comp->getMethodSymbol(), bciToCheck, _callSiteInfo, maxCount, comp);
      if (outterProfiledFrequency == 0)
         return 0;

      while (!callStack.empty())
         {
         bciToCheck = callStack.back().second;
         callStack.pop_back();
         int32_t callerIndex = bciToCheck.getCallerIndex();
         TR_ResolvedMethod *resolvedMethod = callerIndex > -1 ? comp->getInlinedResolvedMethod(callerIndex) : comp->getCurrentMethod();
         TR::ResolvedMethodSymbol *resolvedMethodSymbol = callerIndex > -1 ? comp->getInlinedResolvedMethodSymbol(callerIndex) : comp->getMethodSymbol();
         int32_t callerFrequency = getRawCount(resolvedMethodSymbol, bciToCheck, _callSiteInfo, maxCount, comp);
         double innerFrequencyScale = 1;
         // we have found a frame where we don't have profiling info
         if (callerFrequency < 0)
            {
            if (trace)
               traceMsg(comp, "  found frame for %s with no outter profiling info\n", resolvedMethodSymbol->signature(comp->trMemory()));
            // has this method been compiled so we might have had a chance to profile it?
            if (!resolvedMethod->isInterpretedForHeuristics()
                && !resolvedMethod->isNative()
                && !resolvedMethod->isJNINative())
               {
               TR_PersistentProfileInfo *info = TR_PersistentProfileInfo::get(comp, resolvedMethod);
               // do we have JProfiling data for this frame
               if (info
                   && info->getBlockFrequencyInfo()
                   && info->getBlockFrequencyInfo()->_counterDerivationInfo)
                  {
                  if (trace)
                     traceMsg(comp, "  method has profiling\n");
                  int32_t effectiveCallerIndex = -1;
                  TR_BlockFrequencyInfo *bfi = info->getBlockFrequencyInfo();
                  if (callStack.empty() || info->getCallSiteInfo()->computeEffectiveCallerIndex(comp, callStack, effectiveCallerIndex))
                     {
                     TR_ByteCodeInfo callee(bci);
                     callee.setCallerIndex(effectiveCallerIndex);
                     if (trace && effectiveCallerIndex > -1)
                        traceMsg(comp, "  checking bci %d:%d\n", callee.getCallerIndex(), callee.getByteCodeIndex());
                     int32_t computedFrequency = bfi->getRawCount(resolvedMethodSymbol, callee, info->getCallSiteInfo(), normalizeForCallers ? bfi->getMaxRawCount() : bfi->getMaxRawCount(callee.getCallerIndex()), comp);
                     if (normalizeForCallers)
                        {
                        callee.setCallerIndex(-1);
                        callee.setByteCodeIndex(0);
                        int32_t entry = bfi->getRawCount(resolvedMethodSymbol, callee, info->getCallSiteInfo(), bfi->getMaxRawCount(), comp);
                        if (entry == 0)
                           {
                           frequency = 0;
                           break;
                           }
                        else if (entry > -1)
                           {
                           innerFrequencyScale *= entry;
                           }

                        if (computedFrequency > -1)
                           {
                           traceMsg(comp, " effective caller %s gave frequency %d\n", resolvedMethodSymbol->signature(comp->trMemory()), computedFrequency);
                           frequency = (int32_t)((outterProfiledFrequency * computedFrequency) / innerFrequencyScale);
                           break;
                           }
                        }
                     else
                        {
                        frequency = computedFrequency;
                        break;
                        }
                     }
                  else if (normalizeForCallers)
                     {
                     TR_ByteCodeInfo entry;
                     entry.setCallerIndex(-1);
                     entry.setByteCodeIndex(0);
                     int32_t entryCount = bfi->getRawCount(resolvedMethodSymbol, entry, info->getCallSiteInfo(), bfi->getMaxRawCount(), comp);
                     TR_ByteCodeInfo call = callStack.back().second;
                     call.setCallerIndex(-1);
                     int32_t rawCount = bfi->getRawCount(resolvedMethodSymbol, call, info->getCallSiteInfo(), bfi->getMaxRawCount(), comp);
                     if (rawCount == 0)
                        {
                        frequency = 0;
                        break;
                        }
                     else if (rawCount > -1)
                        {
                        innerFrequencyScale = (innerFrequencyScale * rawCount) / entryCount;
                        continue;
                        }
                     }
                  }
               }
            // if we get here we have not been able to compute a frequency based on JProfiling data so
            // we will resort to IProfiler data stashed during ilgen (if available)
            // we need to get two frequencies 1) the entry frequency and 2) the frequency of the block we are interested in
               {
               int32_t computedFrequency = resolvedMethodSymbol->getProfilerFrequency(bci.getByteCodeIndex());
               int32_t entryFrequency = resolvedMethodSymbol->getProfilerFrequency(0);
               if (computedFrequency < 0 || entryFrequency < 0)
                  continue;
               //printf("here %s\n", comp->signature());
               if (normalizeForCallers)
                  {
                  if (callStack.size() > 0)
                     {
                     innerFrequencyScale = (innerFrequencyScale * computedFrequency) / entryFrequency;
                     }
                  else
                     {
                     //if (TR::Options::isAnyVerboseOptionSet())
                     //   TR_VerboseLog::writeLineLocked(TR_Vlog_INFO, " BFINFO: %s [%s:%d] - from IProfiler", comp->signature(), (bci.getCallerIndex() < 0 ? comp->getMethodSymbol() : comp->getInlinedResolvedMethodSymbol(bci.getCallerIndex()))->signature(comp->trMemory()), bci.getByteCodeIndex());
                     frequency = (int32_t)((outterProfiledFrequency * computedFrequency) / (innerFrequencyScale * entryFrequency));
                     break;
                     }
                  }
               else if (callStack.size() == 0)
                  frequency = computedFrequency;
               }
            }
         else
            {
            lastProfiledBCI = bciToCheck;
            outterProfiledFrequency = callerFrequency;
            }
         }
      }

   return frequency;
   }

int32_t
TR_BlockFrequencyInfo::getRawCount(TR::ResolvedMethodSymbol *resolvedMethod, TR_ByteCodeInfo &bci, TR_CallSiteInfo *callSiteInfo, int64_t maxCount, TR::Compilation *comp)
   {
   int32_t frequency = getRawCount(bci, callSiteInfo, maxCount, comp);
   if (frequency > -1 || _counterDerivationInfo == NULL)
      return frequency;

   int32_t byteCodeToSearch = resolvedMethod->getProfilingByteCodeIndex(bci.getByteCodeIndex());
   if (byteCodeToSearch > -1)
      {
      TR_ByteCodeInfo searchBCI = bci;
      searchBCI.setByteCodeIndex(byteCodeToSearch);
      frequency = getRawCount(searchBCI, callSiteInfo, maxCount, comp);
      }
   return frequency;
   }

/**   \brief Returns block number of the original block in which current byteCodeInfo was created.
 *    \details
 *          It finds the byte code info of the start of the block in which passed bci was originally
 *          created and returns the block number of that block from the stored information.
 *          It is useful when we are trying to associate the profiling information with the node which might
 *          have moved to different blocks with different bci
 *    \param bci TR_ByteCodeInfo for which original block number is searched for
 *    \param comp Current compilation object
 *    \return block number of the original block bci belongs to.
 *            WARNING: If consumer of this API uses this to get the profiled data in later compilation and
 *            requested BCI was not inlined before, it returns -1.
 */
int32_t
TR_BlockFrequencyInfo::getOriginalBlockNumberToGetRawCount(TR_ByteCodeInfo &bci, TR::Compilation *comp, bool trace)
   {
   int32_t callerIndex = bci.getCallerIndex();
   TR::ResolvedMethodSymbol *resolvedMethod = callerIndex < 0 ? comp->getMethodSymbol() : comp->getInlinedResolvedMethodSymbol(callerIndex);
   int32_t byteCodeToSearch = resolvedMethod->getProfilingByteCodeIndex(bci.getByteCodeIndex());
   TR_ByteCodeInfo searchBCI = bci;
   searchBCI.setByteCodeIndex(byteCodeToSearch);
   bool currentCallSiteInfo = TR_CallSiteInfo::getCurrent(comp) == _callSiteInfo;
   for (auto i=0; i < _numBlocks; ++i)
      {
      if ((currentCallSiteInfo && _callSiteInfo->hasSameBytecodeInfo(_blocks[i], searchBCI, comp)) ||
            (!currentCallSiteInfo && _blocks[i].getCallerIndex() == searchBCI.getCallerIndex() && _blocks[i].getByteCodeIndex() == searchBCI.getByteCodeIndex()))
         {
         if (trace)
            traceMsg(comp, "Get frequency from original block_%d\n", i);
         return i;
         }
      }
   return -1;
   }
/**   \brief Using stored static blocl frequency counters creates a node that calculates the raw count of block in which passed node belongs to
 *    \param comp Current compilation object
 *    \return root A node that loads/adds/subtracts the static block counter to calculate raw frequency of corresponding block
 */
TR::Node*
TR_BlockFrequencyInfo::generateBlockRawCountCalculationSubTree(TR::Compilation *comp, TR::Node *node, bool trace)
   {
   return generateBlockRawCountCalculationSubTree(comp, getOriginalBlockNumberToGetRawCount(node->getByteCodeInfo(), comp, trace), node);
   }
/**   \brief Creates a node that calculates the raw count of passed block from the stored static block frequency counters
 *    \param comp Current compilation object
 *    \param blockNumber Number of a block for which we need to generate frequency calculation node
 *    \return root A node that loads/adds/subtracts the static block counter to calculate raw frequency of corresponding block
 */
TR::Node*
TR_BlockFrequencyInfo::generateBlockRawCountCalculationSubTree(TR::Compilation *comp, int32_t blockNumber, TR::Node *node)
   {
   TR::Node *root = NULL;
   if (blockNumber > -1 && (_counterDerivationInfo[blockNumber * 2]
      && (((((uintptr_t)_counterDerivationInfo[blockNumber * 2]) & 0x1) == 1)
      || !_counterDerivationInfo[blockNumber * 2]->isEmpty())))
      {
      TR::Node *addRoot = NULL;
      if ((((uintptr_t)_counterDerivationInfo[blockNumber * 2]) & 0x1) == 1)
         {
         TR::SymbolReference *symRef = comp->getSymRefTab()->createKnownStaticDataSymbolRef(getFrequencyForBlock(((uintptr_t)_counterDerivationInfo[blockNumber * 2]) >> 1), TR::Int32);
         symRef->getSymbol()->setIsBlockFrequency();
         symRef->getSymbol()->setNotDataAddress();
         addRoot = TR::Node::createWithSymRef(node, TR::iload, 0, symRef);
         }
      else
         {
         TR_BitVectorIterator addBVI(*(_counterDerivationInfo[blockNumber * 2]));
         while (addBVI.hasMoreElements())
            {
            TR::SymbolReference *symRef = comp->getSymRefTab()->createKnownStaticDataSymbolRef(getFrequencyForBlock(addBVI.getNextElement()), TR::Int32);
            symRef->getSymbol()->setIsBlockFrequency();
            symRef->getSymbol()->setNotDataAddress();
            TR::Node *counterLoad = TR::Node::createWithSymRef(node, TR::iload, 0, symRef);
            if (addRoot)
               addRoot = TR::Node::create(node, TR::iadd, 2, addRoot, counterLoad);
            else
               addRoot = counterLoad;
            }
         }
      TR::Node *subRoot = NULL;
      if (_counterDerivationInfo[blockNumber * 2 +1] != NULL)
         {
         if ((((uintptr_t)_counterDerivationInfo[blockNumber *2 + 1]) & 0x1) == 1)
            {
            TR::SymbolReference *symRef = comp->getSymRefTab()->createKnownStaticDataSymbolRef(getFrequencyForBlock(((uintptr_t)_counterDerivationInfo[blockNumber * 2 + 1]) >> 1), TR::Int32);
            symRef->getSymbol()->setIsBlockFrequency();
            symRef->getSymbol()->setNotDataAddress();
            subRoot = TR::Node::createWithSymRef(node, TR::iload, 0, symRef);
            }
         else
            {
            TR_BitVectorIterator subBVI(*(_counterDerivationInfo[blockNumber * 2 + 1]));
            while (subBVI.hasMoreElements())
               {
               TR::SymbolReference *symRef = comp->getSymRefTab()->createKnownStaticDataSymbolRef(getFrequencyForBlock(subBVI.getNextElement()), TR::Int32);
               symRef->getSymbol()->setIsBlockFrequency();
               symRef->getSymbol()->setNotDataAddress();
               TR::Node *counterLoad = TR::Node::createWithSymRef(node, TR::iload, 0, symRef);
               if (subRoot)
                  {
                  subRoot = TR::Node::create(node, TR::isub, 2, subRoot, counterLoad);
                  }
               else
                  {
                  subRoot = counterLoad;
                  }
               }
            }
         }
      root = addRoot;
      if (subRoot)
         {
         root = TR::Node::create(node, TR::isub, 2, root, subRoot);
         }
      }
   return root;
   }

int32_t
TR_BlockFrequencyInfo::getRawCount(TR_ByteCodeInfo &bci, TR_CallSiteInfo *callSiteInfo, int64_t maxCount, TR::Compilation *comp)
   {
   // Try to find a block profiling slot that matches the requested block
   //
   int64_t frequency = 0;
   int32_t blocksMatched = 0;
   bool currentCallSiteInfo = TR_CallSiteInfo::getCurrent(comp) == callSiteInfo;

   for (uint32_t i = 0; i < _numBlocks; ++i)
      {
      if ((currentCallSiteInfo && callSiteInfo->hasSameBytecodeInfo(_blocks[i], bci, comp))
          || (!currentCallSiteInfo && _blocks[i].getCallerIndex() == bci.getCallerIndex() && _blocks[i].getByteCodeIndex() == bci.getByteCodeIndex()))
         {
         int64_t rawCount = 0;
         if (_counterDerivationInfo == NULL)
            {
            rawCount += _frequencies[i];
            }
         else
            {
            if (bci.getCallerIndex() == -1 && bci.getByteCodeIndex() == 0 && i != 2)
               continue;

            TR_BitVector *toAdd = _counterDerivationInfo[i * 2];
            if (toAdd == NULL)
               {
               continue;
               }
            else if (((uintptr_t)toAdd & 0x1) == 1)
               {
               rawCount = _frequencies[(uintptr_t)toAdd >> 1];
               }
            else
               {
               TR_BitVectorIterator addBVI(*toAdd);
               while (addBVI.hasMoreElements())
                  rawCount += _frequencies[addBVI.getNextElement()];
               }

            TR_BitVector *toSub = _counterDerivationInfo[i * 2 + 1];
            if (toSub != NULL)
               {
               if (((uintptr_t)toSub & 0x1) == 1)
                  {
                  rawCount -= _frequencies[(uintptr_t)toSub >> 1];
                  }
               else
                  {
                  TR_BitVectorIterator subBVI(*toSub);
                  while (subBVI.hasMoreElements())
                     rawCount -= _frequencies[subBVI.getNextElement()];
                  }
               }
            if (comp->getOption(TR_TraceBFGeneration))
               traceMsg(comp, "   Slot %d has raw frequency %d\n", i, rawCount);

            if (maxCount > 0 && rawCount > 0)
               rawCount = ((10000 * rawCount) / maxCount);
            else
               rawCount = 0;
            }

         if (comp->getOption(TR_TraceBFGeneration))
            traceMsg(comp, "   Slot %d has frequency %d\n", i, rawCount);

         frequency += rawCount;
         blocksMatched++;
         }
      }

   if (blocksMatched == 0)
      return -1;
   else if (_counterDerivationInfo == NULL)
      return frequency;
   else
      return frequency / blocksMatched;
   }

int32_t TR_BlockFrequencyInfo::getCallCount()
   {
   if (_counterDerivationInfo == NULL || _entryBlockNumber < 0)
      return -1;

   int32_t count = 0;
   TR_BitVector *toAdd = _counterDerivationInfo[_entryBlockNumber * 2];
   if (toAdd == NULL)
      return -1;

   if (((uintptr_t)toAdd & 0x1) == 1)
      {
      count += _frequencies[(uintptr_t)toAdd >> 1];
      }
   else
      {
      TR_BitVectorIterator addBVI(*toAdd);
      while (addBVI.hasMoreElements())
         count += _frequencies[addBVI.getNextElement()];
      }

   TR_BitVector *toSub = _counterDerivationInfo[_entryBlockNumber * 2 + 1];
   if (toSub != NULL)
      {
      if (((uintptr_t)toSub & 0x1) == 1)
         {
         count -= _frequencies[(uintptr_t)toSub >> 1];
         }
      else
         {
         TR_BitVectorIterator subBVI(*toSub);
         while (subBVI.hasMoreElements())
            count -= _frequencies[subBVI.getNextElement()];
         }
      }

   return count;
   }

int32_t TR_BlockFrequencyInfo::getMaxRawCount(int32_t callerIndex)
   {
   int32_t maxCount = 0;
   if (_counterDerivationInfo == NULL)
      {
      for (int32_t i = 0; i < _numBlocks; ++i)
         {
         if (_blocks[i].getCallerIndex() != callerIndex)
            continue;

         if (_frequencies[i] > maxCount)
            maxCount = _frequencies[i];
         }
      }
   else
      {
      for (int32_t i = 0; i < _numBlocks; ++i)
         {
         if (_blocks[i].getCallerIndex() != callerIndex)
            continue;

         int32_t count = 0;
         TR_BitVector *toAdd = _counterDerivationInfo[i * 2];
         if (toAdd == NULL)
            {
            continue;
            }
         else if (((uintptr_t)toAdd & 0x1) == 1)
            {
            count += _frequencies[(uintptr_t)toAdd >> 1];
            }
         else
            {
            TR_BitVectorIterator addBVI(*toAdd);
            while (addBVI.hasMoreElements())
               count += _frequencies[addBVI.getNextElement()];
            }

         TR_BitVector *toSub = _counterDerivationInfo[i * 2 + 1];
         if (toSub != NULL)
            {
            if (((uintptr_t)toSub & 0x1) == 1)
               {
               count -= _frequencies[(uintptr_t)toSub >> 1];
               }
            else
               {
               TR_BitVectorIterator subBVI(*toSub);
               while (subBVI.hasMoreElements())
                  count -= _frequencies[subBVI.getNextElement()];
               }
            }
         if (count > maxCount)
            maxCount = count;
         }
      }
   return maxCount;
   }

int32_t TR_BlockFrequencyInfo::getMaxRawCount()
   {
   int32_t maxCount = 0;
   if (_counterDerivationInfo == NULL)
      {
      for (int32_t i = 0; i < _numBlocks; ++i)
         {
         if (_frequencies[i] > maxCount)
            maxCount = _frequencies[i];
         }
      }
   else
      {
      for (int32_t i = 0; i < _numBlocks; ++i)
         {
         int32_t count = 0;
         TR_BitVector *toAdd = _counterDerivationInfo[i * 2];
         if (toAdd == NULL)
            {
            continue;
            }
         else if (((uintptr_t)toAdd & 0x1) == 1)
            {
            count += _frequencies[(uintptr_t)toAdd >> 1];
            }
         else
            {
            TR_BitVectorIterator addBVI(*toAdd);
            while (addBVI.hasMoreElements())
               count += _frequencies[addBVI.getNextElement()];
            }

         TR_BitVector *toSub = _counterDerivationInfo[i * 2 + 1];
         if (toSub != NULL)
            {
            if (((uintptr_t)toSub & 0x1) == 1)
               {
               count -= _frequencies[(uintptr_t)toSub >> 1];
               }
            else
               {
               TR_BitVectorIterator subBVI(*toSub);
               while (subBVI.hasMoreElements())
                  count -= _frequencies[subBVI.getNextElement()];
               }
            }
         if (count > maxCount)
            maxCount = count;
         }
      }
   return maxCount;
   }

uint32_t TR_BlockFrequencyInfo::getSizeForSerialization() const
   {
   uint32_t size = sizeof(SerializedBFI);
   if (_numBlocks > 0)
      {
      size += (_numBlocks * sizeof(*_blocks));
      size += (_numBlocks * sizeof(*_frequencies));
      size += (_numBlocks * 2 * sizeof(*_counterDerivationInfo));
      for (int32_t i = 0; i < (_numBlocks * 2); i++)
         {
         if (TR_BlockFrequencyInfo::isCounterDerivationInfoValidBitVector(_counterDerivationInfo[i]))
            {
            size += _counterDerivationInfo[i]->getSizeForSerialization();
            }
         }
      }
   return size;
   }

void TR_BlockFrequencyInfo::serialize(uint8_t * &buffer) const
   {
   SerializedBFI *serializedData = reinterpret_cast<SerializedBFI *>(buffer);
   serializedData->numBlocks = _numBlocks;
   buffer += sizeof(SerializedBFI);
   if (_numBlocks > 0)
      {
      size_t blocksSize = _numBlocks * sizeof(*_blocks);
      memcpy(buffer, _blocks, blocksSize);
      buffer += blocksSize;

      size_t frequenciesSize = _numBlocks * sizeof(*_frequencies);
      memcpy(buffer, _frequencies, frequenciesSize);
      buffer += frequenciesSize;

      size_t counterSize = _numBlocks * 2 * sizeof(*_counterDerivationInfo);
      memcpy(buffer, _counterDerivationInfo, counterSize);
      buffer += counterSize;
      for (int32_t i = 0; i < (_numBlocks * 2); i++)
         {
         if (TR_BlockFrequencyInfo::isCounterDerivationInfoValidBitVector(_counterDerivationInfo[i]))
            {
            // write the bit vector
            _counterDerivationInfo[i]->serialize(buffer);
            }
         }
      }
   }

TR_BlockFrequencyInfo::TR_BlockFrequencyInfo(const SerializedBFI *serializedData, uint8_t * &buffer, TR_PersistentProfileInfo *currentProfile) :
   _callSiteInfo(currentProfile->getCallSiteInfo()),
   _numBlocks(serializedData->numBlocks),
   _blocks(
      _numBlocks ?
      new (PERSISTENT_NEW) TR_ByteCodeInfo[_numBlocks] :
      0
      ),
   _frequencies(
      _numBlocks ?
      /*
       * The explicit parens value initializes the array,
       * which in turn value initializes each array member,
       * which for ints is zero initialization.
       */
      new (PERSISTENT_NEW) int32_t[_numBlocks]() :
      NULL
      ),
   _counterDerivationInfo(
      _numBlocks ?
      (TR_BitVector**) new (PERSISTENT_NEW) void**[_numBlocks*2]() :
      NULL),
   _entryBlockNumber(-1),
   _isQueuedForRecompilation(0)
   {
   if (_numBlocks > 0)
      {
      size_t blocksSize = _numBlocks * sizeof(*_blocks);
      memcpy(_blocks, buffer, blocksSize);
      buffer += blocksSize;

      size_t frequenciesSize = _numBlocks * sizeof(*_frequencies);
      memcpy(_frequencies, buffer, frequenciesSize);
      buffer += frequenciesSize;

      size_t counterSize = _numBlocks * 2 * sizeof(*_counterDerivationInfo);
      memcpy(_counterDerivationInfo, buffer, counterSize);
      buffer += counterSize;

      // Now read the bit vectors if there is any
      for (int32_t i = 0; i < (_numBlocks * 2); i++)
         {
         if (TR_BlockFrequencyInfo::isCounterDerivationInfoValidBitVector(_counterDerivationInfo[i]))
            {
            _counterDerivationInfo[i] = new (PERSISTENT_NEW) TR_BitVector(buffer);
            }
         }
      }
   }

TR_BlockFrequencyInfo * TR_BlockFrequencyInfo::deserialize(uint8_t * &buffer, TR_PersistentProfileInfo *currentProfileInfo)
   {
   SerializedBFI *serializedData = reinterpret_cast<SerializedBFI *>(buffer);
   buffer += sizeof(SerializedBFI);
   return new (PERSISTENT_NEW) TR_BlockFrequencyInfo(serializedData, buffer, currentProfileInfo);
   }

const uint32_t TR_CatchBlockProfileInfo::EDOThreshold = 50;

TR_CallSiteInfo::TR_CallSiteInfo(TR::Compilation * comp, TR_AllocationKind allocKind) :
   _numCallSites(comp->getNumInlinedCallSites()),
   _callSites(
      _numCallSites ?
      new (comp->trMemory(), allocKind, TR_Memory::CallSiteInfo) TR_InlinedCallSite[_numCallSites] :
      NULL
      ),
   _allocKind(allocKind)
   {
   for (uint32_t i = 0; i < _numCallSites; ++i)
      _callSites[i] = comp->getInlinedCallSite(i);
   }

TR_CallSiteInfo::~TR_CallSiteInfo()
   {
   if (_callSites)
      if (_allocKind == persistentAlloc)
      {
      jitPersistentFree(_callSites);
      }
   }

bool
TR_CallSiteInfo::computeEffectiveCallerIndex(TR::Compilation *comp, TR::list<std::pair<TR_OpaqueMethodBlock *, TR_ByteCodeInfo> > &callStack, int32_t &effectiveCallerIndex)
   {
   for (uint32_t i = 0; i < _numCallSites; ++i)
      {
      if (comp->fe()->getInlinedCallSiteMethod(&_callSites[i]) != callStack.front().first)
         continue;

      TR_InlinedCallSite *cursor = &_callSites[i];
      auto itr = callStack.begin(), end = callStack.end();
      while (cursor && itr != end)
         {
         if (comp->fe()->getInlinedCallSiteMethod(cursor) != itr->first)
            break;

         if (cursor->_byteCodeInfo.getCallerIndex() > -1)
            cursor = &_callSites[cursor->_byteCodeInfo.getCallerIndex()];
         else
            cursor = NULL;
         itr++;
         }

      // both have terminated at the same time means we have a match for our callstack fragment
      // so return it
      if (itr == end && cursor == NULL)
         {
         effectiveCallerIndex = i;
         return true;
         }
      }
   return false;
   }

bool
TR_CallSiteInfo::hasSameBytecodeInfo(
      TR_ByteCodeInfo & persistentBytecodeInfo,
      TR_ByteCodeInfo & currentBytecodeInfo,
      TR::Compilation *comp)
   {
   TR_ASSERT(
      currentBytecodeInfo.getByteCodeIndex() != TR_ByteCodeInfo::invalidByteCodeIndex,
      "Current byte code info is invalid."
      );

   if (persistentBytecodeInfo.getByteCodeIndex() != currentBytecodeInfo.getByteCodeIndex())
      return false;

   // Match call site information
   //
   int32_t persistentCallSite = persistentBytecodeInfo.getCallerIndex();
   int32_t currentCallSite = currentBytecodeInfo.getCallerIndex();

   TR_ASSERT(
      persistentCallSite < static_cast<int32_t>(_numCallSites & 0x7FFFFFFF),
      "Unexpected call site will overflow. persistentCallSite = %d, _numCallSites = %d",
      persistentCallSite,
      _numCallSites
      );

   while (persistentCallSite >= 0 && currentCallSite >= 0)
      {
      TR_InlinedCallSite &persistentCallSiteInfo = _callSites[persistentCallSite];
      TR_InlinedCallSite &currentCallSiteInfo = comp->getInlinedCallSite(currentCallSite);

      int32_t const persistentCallSiteByteCodeIndex = persistentCallSiteInfo._byteCodeInfo.getByteCodeIndex();
      int32_t const currentCallSiteByteCodeIndex = currentCallSiteInfo._byteCodeInfo.getByteCodeIndex();

      if (persistentCallSiteByteCodeIndex != currentCallSiteByteCodeIndex)
         break;

      TR_OpaqueMethodBlock *persitentCallSiteMethod = comp->fe()->getInlinedCallSiteMethod(&persistentCallSiteInfo);
      TR_OpaqueMethodBlock *currentCallSiteMethod = comp->fe()->getInlinedCallSiteMethod(&currentCallSiteInfo);

      if (persitentCallSiteMethod != currentCallSiteMethod)
         break;

      persistentCallSite = persistentCallSiteInfo._byteCodeInfo.getCallerIndex();
      currentCallSite = currentCallSiteInfo._byteCodeInfo.getCallerIndex();
      }

   return persistentCallSite < 0 && currentCallSite < 0;
   }


int32_t
TR_CallSiteInfo::hasSamePartialBytecodeInfo(
      TR_ByteCodeInfo & persistentBytecodeInfo,
      TR_ByteCodeInfo & currentBytecodeInfo,
      TR::Compilation *comp)
   {
   if (persistentBytecodeInfo.getByteCodeIndex() != currentBytecodeInfo.getByteCodeIndex())
      return 0;

   // Match call site information
   //
   int32_t callSite1 = currentBytecodeInfo.getCallerIndex();
   int32_t callSite2 = persistentBytecodeInfo.getCallerIndex();
   int matchLevelCount = 0;
   while (callSite1 >= 0 && callSite2 >= 0)
      {
      TR_InlinedCallSite &callSiteInfo1 = comp->getInlinedCallSite(callSite1);
      TR_InlinedCallSite &callSiteInfo2 = _callSites[callSite2];

      if (callSiteInfo1._byteCodeInfo.getByteCodeIndex() != callSiteInfo2._byteCodeInfo.getByteCodeIndex())
         break;
      TR_OpaqueMethodBlock *method1 = callSiteInfo1._methodInfo;
      TR_OpaqueMethodBlock *method2 = callSiteInfo2._methodInfo;
      if (method1 != method2)
         break;
      callSite1 = callSiteInfo1._byteCodeInfo.getCallerIndex();
      callSite2 = callSiteInfo2._byteCodeInfo.getCallerIndex();
      //traceMsg(comp, "\t\tMatched profiling info at level %d, method %p, bcIndex %d\n", matchLevelCount, method1, callSiteInfo1._byteCodeInfo._byteCodeIndex);
      matchLevelCount ++;
      }

   return matchLevelCount;
   }

TR_PersistentProfileInfo::~TR_PersistentProfileInfo()
   {
   if (_catchBlockProfileInfo)
      {
      jitPersistentFree(_catchBlockProfileInfo);
      _catchBlockProfileInfo = NULL;
      }
   if (_valueProfileInfo)
      {
      _valueProfileInfo->~TR_ValueProfileInfo();
      jitPersistentFree(_valueProfileInfo);
      _valueProfileInfo = NULL;
      }
   if (_blockFrequencyInfo)
      {
      _blockFrequencyInfo->~TR_BlockFrequencyInfo();
      jitPersistentFree(_blockFrequencyInfo);
      _blockFrequencyInfo = NULL;
      }
   if (_callSiteInfo)
      {
      _callSiteInfo->~TR_CallSiteInfo();
      jitPersistentFree(_callSiteInfo);
      _callSiteInfo = NULL;
      }
   }

/**
 * Setup the persistent information for profiling.
 *
 * This must be called before any profiling instrumentation is added to the current compilation.
 * It will create and maintain necessary bookkeeping in persistent memory.
 *
 * \param comp Current compilation.
 */
void
TR_PersistentProfileInfo::prepareForProfiling(TR::Compilation *comp)
   {
   if (!comp->haveCommittedCallSiteInfo())
      {
      TR_CallSiteInfo * const originalCallSiteInfo = getCallSiteInfo();
      TR_ASSERT_FATAL(originalCallSiteInfo == NULL, "Reusing persistent profile info %p", this);

      // Create a new persistent call site info
      _callSiteInfo = new (PERSISTENT_NEW) TR_CallSiteInfo(comp, persistentAlloc);
      comp->setCommittedCallSiteInfo(true);
      }
   else if (getCallSiteInfo()->getNumCallSites() != comp->getNumInlinedCallSites())
      {
      TR_CallSiteInfo * const originalCallSiteInfo = getCallSiteInfo();
      TR_ASSERT_FATAL(originalCallSiteInfo != NULL, "Existing CallSiteInfo should not be NULL for persistent profile info %p.", this);

      // Destroy the prior call site info and placement new the updated version
      originalCallSiteInfo->~TR_CallSiteInfo();
      TR_CallSiteInfo * const updatedCallSiteInfo = new ((void*)originalCallSiteInfo) TR_CallSiteInfo(comp, persistentAlloc);
      }
   }

/**
 * Returns the best available profiling information for the current
 * method. Should be used to inform optimizations.
 *
 * \param comp The current compilation.
 */
TR_PersistentProfileInfo *
TR_PersistentProfileInfo::get(TR::Compilation *comp)
   {
   return comp->getProfileInfo()->get(comp);
   }

/**
 * Returns the best available profiling information for the requested method.
 * Should be used to inform optimizations.
 *
 * \param comp The current compilation.
 * \param feMethod Method to collect profiling information from.
 */
TR_PersistentProfileInfo *
TR_PersistentProfileInfo::get(TR::Compilation *comp, TR_ResolvedMethod * feMethod)
   {
   return comp->getProfileInfo()->get(feMethod);
   }

/**
 * Returns the profiling information for the current compilation.
 * Used for adding instrumentation.
 *
 * \param comp The current compilation.
 */
TR_PersistentProfileInfo *
TR_PersistentProfileInfo::getCurrent(TR::Compilation *comp)
   {
   TR::Recompilation *recompilation = comp->getRecompilationInfo();
   if (recompilation)
      {
      TR_PersistentJittedBodyInfo *body = recompilation->getJittedBodyInfo();
      if (body)
         return body->getProfileInfo();
      }
   return NULL;
   }

/**
 * Increment reference count for the persistent profile info.
 * Will perform an atomic increment.
 *
 * \param info Persistent profile info to manipulate and potentially free.
 */
void
TR_PersistentProfileInfo::incRefCount(TR_PersistentProfileInfo *info)
   {
   TR_ASSERT_FATAL(info->_refCount > 0, "Increment called on profile info with no references");
   VM_AtomicSupport::add(reinterpret_cast<volatile uintptr_t*>(&(info->_refCount)), 1);
   TR_ASSERT_FATAL(info->_refCount >= 0, "Increment resulted in negative reference count");
   }

/**
 * Decrement reference count for persistent profile info.
 * Will perform an atomic decrement.
 *
 * \param info Persistent profile info to manipulate and potentially free.
 */
void
TR_PersistentProfileInfo::decRefCount(TR_PersistentProfileInfo *info)
   {
   VM_AtomicSupport::subtract(reinterpret_cast<volatile uintptr_t*>(&(info->_refCount)), 1);
   TR_ASSERT_FATAL(info->_refCount >= 0, "Decrement resulted in negative reference count");
   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableJProfilerThread))
      {
      if (info->_refCount == 0 && TR::Options::isAnyVerboseOptionSet(TR_VerboseProfiling, TR_VerboseReclamation))
         TR_VerboseLog::writeLineLocked(TR_Vlog_RECLAMATION, "PersistentProfileInfo 0x%p queued for reclamation.", info);
      }
   else if (info->_refCount == 0 && !TR::Options::getCmdLineOptions()->getOption(TR_DisableProfilingDataReclamation))
      {
      if (TR::Options::getVerboseOption(TR_VerboseReclamation))
         TR_VerboseLog::writeLineLocked(TR_Vlog_RECLAMATION, "Reclaiming PersistentProfileInfo immediately 0x%p.", info);
      info->~TR_PersistentProfileInfo();
      TR_Memory::jitPersistentFree(info);
      }
   }

/**
 * findOrCreate methods for the different kinds of profiling information
 *
 * Requesting the information through these ensures all persistent bookkeeping
 * information is correctly set up, eg. the call site information.
 */
TR_CatchBlockProfileInfo *
TR_PersistentProfileInfo::findOrCreateCatchBlockProfileInfo(TR::Compilation *comp)
   {
   prepareForProfiling(comp);
   if (!_catchBlockProfileInfo)
      _catchBlockProfileInfo = new (PERSISTENT_NEW) TR_CatchBlockProfileInfo;

   return _catchBlockProfileInfo;
   }

TR_BlockFrequencyInfo *
TR_PersistentProfileInfo::findOrCreateBlockFrequencyInfo(TR::Compilation *comp)
   {
   prepareForProfiling(comp);
   if (!_blockFrequencyInfo)
      _blockFrequencyInfo = new (PERSISTENT_NEW) TR_BlockFrequencyInfo(comp, persistentAlloc);

   return _blockFrequencyInfo;
   }

TR_ValueProfileInfo *
TR_PersistentProfileInfo::findOrCreateValueProfileInfo(TR::Compilation *comp)
   {
   prepareForProfiling(comp);
   if (!_valueProfileInfo)
      _valueProfileInfo = new (PERSISTENT_NEW) TR_ValueProfileInfo(_callSiteInfo);

   return _valueProfileInfo;
   }

void TR_ValueProfileInfo::dumpInfo(TR::FILE *logFile)
   {
   trfprintf(logFile, "\nDumping value profile info\n");
   for (size_t i = 0; i < LastProfiler; ++i)
      {
      for (TR_AbstractProfilerInfo * valueInfo = _values[i]; valueInfo; valueInfo = valueInfo->getNext())
         valueInfo->dumpInfo(logFile);
      }
   }

void TR_BlockFrequencyInfo::dumpInfo(TR::FILE *logFile)
   {
   trfprintf(logFile, "\nDumping block frequency info\n");
   for (int32_t i = 0; i < _numBlocks; i++)
      trfprintf(logFile, "   Block index = %d, caller = %d, frequency = %d\n", _blocks[i].getByteCodeIndex(), _blocks[i].getCallerIndex(), _frequencies[i]);
   }


void TR_CatchBlockProfileInfo::dumpInfo(TR::FILE *logFile)
   {
   if (_catchCounter || _throwCounter)
      trfprintf(logFile, "\nDumping catch block info\n   catch %7d throw %7d\n", _catchCounter, _throwCounter);
   }


void TR_CallSiteInfo::dumpInfo(TR::FILE *logFile)
   {
   trfprintf(logFile, "\nDumping call site info\n");
   for (int32_t i = 0; i < _numCallSites; i++)
      trfprintf(logFile, "   Call site index = %d, method = %p, parent = %d\n", _callSites[i]._byteCodeInfo.getByteCodeIndex(), _callSites[i]._methodInfo, _callSites[i]._byteCodeInfo.getCallerIndex());
   }

uint32_t TR_CallSiteInfo::getSizeForSerialization() const
   {
   uint32_t size = sizeof(SerializedCSI);
   if (_numCallSites > 0)
      {
      size += (_numCallSites * sizeof(TR_InlinedCallSite));
      }
   return size;
   }

void TR_CallSiteInfo::serialize(uint8_t * &buffer) const
   {
   SerializedCSI *serializedData = reinterpret_cast<SerializedCSI *>(buffer);
   serializedData->numCallSites = _numCallSites;
   buffer += sizeof(SerializedCSI);
   if (_numCallSites > 0)
      {
      size_t callSitesSize = _numCallSites * sizeof(TR_InlinedCallSite);
      memcpy(buffer, _callSites, callSitesSize);
      buffer += callSitesSize;
      }
   }

TR_CallSiteInfo::TR_CallSiteInfo(const SerializedCSI *data, uint8_t * &buffer) :
   _numCallSites(data->numCallSites),
   _callSites(
      _numCallSites ?
      new (PERSISTENT_NEW) TR_InlinedCallSite[_numCallSites] :
      NULL
      ),
   _allocKind(persistentAlloc)
   {
   if (_numCallSites > 0)
      {
      memcpy(buffer, _callSites, _numCallSites * sizeof(TR_InlinedCallSite));
      buffer += (_numCallSites * sizeof(TR_InlinedCallSite));
      }
   }

TR_CallSiteInfo * TR_CallSiteInfo::deserialize(uint8_t * &buffer)
   {
   SerializedCSI *serializedData = reinterpret_cast<SerializedCSI *>(buffer);
   buffer += sizeof(SerializedCSI);
   return new (PERSISTENT_NEW) TR_CallSiteInfo(serializedData, buffer);
   }

void TR_PersistentProfileInfo::dumpInfo(TR::FILE *logFile)
   {
   if (_callSiteInfo)
      _callSiteInfo->dumpInfo(logFile);

   if (_blockFrequencyInfo)
      _blockFrequencyInfo->dumpInfo(logFile);

   if (_catchBlockProfileInfo)
      _catchBlockProfileInfo->dumpInfo(logFile);

   if (_valueProfileInfo)
      _valueProfileInfo->dumpInfo(logFile);
   }

uint32_t TR_PersistentProfileInfo::getSizeForSerialization() const
   {
   uint32_t size = sizeof(SerializedPPI);
   if (_callSiteInfo)
      {
      size += _callSiteInfo->getSizeForSerialization();
      }
   if (_blockFrequencyInfo)
      {
      size += _blockFrequencyInfo->getSizeForSerialization();
      }
   return size;
   }

void TR_PersistentProfileInfo::serialize(uint8_t * &buffer) const
   {
   SerializedPPI *serializedData = reinterpret_cast<SerializedPPI *>(buffer);
   serializedData->hasCallSiteInfo = (_callSiteInfo != NULL);
   serializedData->hasBlockFrequencyInfo = (_blockFrequencyInfo != NULL);
   serializedData->hasValueProfileInfo = false;
   buffer += sizeof(SerializedPPI);
   if (_callSiteInfo)
      {
      _callSiteInfo->serialize(buffer);
      }
   if (_blockFrequencyInfo)
      {
      _blockFrequencyInfo->serialize(buffer);
      }
   }

TR_PersistentProfileInfo::TR_PersistentProfileInfo(uint8_t * &buffer) :
   _next(NULL),
   _active(true),
   _refCount(1)
   {
   SerializedPPI *serializedData = reinterpret_cast<SerializedPPI *>(buffer);
   buffer += sizeof(SerializedPPI);
   _callSiteInfo = serializedData->hasCallSiteInfo ? TR_CallSiteInfo::deserialize(buffer) : NULL;
   _blockFrequencyInfo = serializedData->hasBlockFrequencyInfo ? TR_BlockFrequencyInfo::deserialize(buffer, this) : NULL;
   TR_ASSERT_FATAL(!serializedData->hasValueProfileInfo, "hasValueProfileInfo should be false\n");
   _valueProfileInfo = NULL;

   // these two are not required
   memset(_profilingFrequency, 0, sizeof(_profilingFrequency));
   memset(_profilingCount, 0, sizeof(_profilingCount));
   }


TR_AccessedProfileInfo::TR_AccessedProfileInfo(TR::Region &region) :
    _usedInfo((InfoMapComparator()), (InfoMapAllocator(region))),
    _current(NULL),
    _searched(false)
   {
   }

TR_AccessedProfileInfo::~TR_AccessedProfileInfo()
   {
   // Decrement reference counts for all accessed information
   for (auto it = _usedInfo.begin(); it != _usedInfo.end(); ++it)
      {
      if (it->second)
        TR_PersistentProfileInfo::decRefCount(it->second);
      }
   if (_current)
      TR_PersistentProfileInfo::decRefCount(_current);
   }

/**
 * Compare two sources of profiling information, returning the preferable
 * one and updating the vmMethod->best if necessary.
 */
TR_PersistentProfileInfo *TR_AccessedProfileInfo::compare(TR_PersistentMethodInfo *methodInfo)
   {
   if (!methodInfo)
      return NULL;

   TR_PersistentProfileInfo *recent = methodInfo->getRecentProfileInfo();
   TR_PersistentProfileInfo *best = methodInfo->getBestProfileInfo();

   // Currently heuristic will replace if recent is non-null
   // Future implementation should use the method entry block counter to determine
   // whether enough data has been collected
   bool replace = recent != NULL && recent != best;

   if (replace)
      {
      if (TR::Options::getVerboseOption(TR_VerboseProfiling))
         TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "For MethodInfo 0x%p, updating best from 0x%p to 0x%p", methodInfo, best, recent);
      methodInfo->setBestProfileInfo(recent);
      if (best)
         TR_PersistentProfileInfo::decRefCount(best);
      return recent;
      }
   else
      {
      if (recent)
         TR_PersistentProfileInfo::decRefCount(recent);
      return best;
      }
   }

/**
 * For the current method.
 * More frequent request, so avoid the overhead of the map.
 */
TR_PersistentProfileInfo *TR_AccessedProfileInfo::get(TR::Compilation *comp)
   {
   if (_searched)
      return _current;

   TR::Recompilation *recompilation = comp->getRecompilationInfo();
   if (recompilation)
      {
      TR_PersistentMethodInfo *methodInfo = recompilation->getMethodInfo();
      _current = compare(methodInfo);

      // Don't return profile info if its for the current compilation, as
      // it may mislead and confuse
      if (_current && _current == TR_PersistentProfileInfo::getCurrent(comp))
         {
         TR_PersistentProfileInfo::decRefCount(_current);
         _current = NULL;
         }
      }
   _searched = true;
   return _current;
   }

/**
 * For another method.
 */
TR_PersistentProfileInfo *TR_AccessedProfileInfo::get(TR_ResolvedMethod *vmMethod)
   {
   auto lookup = _usedInfo.find(vmMethod);
   if (lookup != _usedInfo.end())
      return lookup->second;

   // Lookup the info and stash it
   TR_PersistentMethodInfo *methodInfo = TR_PersistentMethodInfo::get(vmMethod);
   _usedInfo[vmMethod] = compare(methodInfo);
   return _usedInfo[vmMethod];
   }

/**
 * JProfiler thread function.
 * Real work is carried out by processWorkingQueue
 */
static int32_t
J9THREAD_PROC jProfilerThreadProc(void * entryarg)
   {
   J9JITConfig *jitConfig  = (J9JITConfig *) entryarg;
   J9JavaVM *vm            = jitConfig->javaVM;
   TR_JProfilerThread *jProfiler = ((TR_JitPrivateConfig*)(jitConfig->privateConfig))->jProfiler;
   J9VMThread *jProfilerThread = NULL;
   PORT_ACCESS_FROM_JITCONFIG(jitConfig);

   // Starting
   int rc = vm->internalVMFunctions->internalAttachCurrentThread(vm, &jProfilerThread, NULL,
                                  J9_PRIVATE_FLAGS_DAEMON_THREAD | J9_PRIVATE_FLAGS_NO_OBJECT |
                                  J9_PRIVATE_FLAGS_SYSTEM_THREAD | J9_PRIVATE_FLAGS_ATTACHED_THREAD,
                                  jProfiler->getJProfilerOSThread());

   jProfiler->getJProfilerMonitor()->enter();
   jProfiler->setAttachAttempted();
   if (rc == JNI_OK)
      jProfiler->setJProfilerThread(jProfilerThread);
   jProfiler->getJProfilerMonitor()->notifyAll();
   jProfiler->getJProfilerMonitor()->exit();
   if (rc != JNI_OK)
      return JNI_ERR;

   j9thread_set_name(j9thread_self(), "JIT JProfiler");

   // Process JProfiling data
   jProfiler->processWorkingQueue();

   // Stopping
   vm->internalVMFunctions->DetachCurrentThread((JavaVM *) vm);
   jProfiler->getJProfilerMonitor()->enter();
   jProfiler->setJProfilerThread(NULL);
   jProfiler->getJProfilerMonitor()->notifyAll();
   j9thread_exit((J9ThreadMonitor*)jProfiler->getJProfilerMonitor()->getVMMonitor());
   return 0;
   }

TR_JProfilerThread *
TR_JProfilerThread::allocate()
   {
   TR_JProfilerThread * profiler = new (PERSISTENT_NEW) TR_JProfilerThread();
   return profiler;
   }

/**
 * Start the profiler thread.
 */
void
TR_JProfilerThread::start(J9JavaVM *javaVM)
   {
   PORT_ACCESS_FROM_JAVAVM(javaVM);
   UDATA priority;

   priority = J9THREAD_PRIORITY_NORMAL;

   if (TR::Options::getVerboseOption(TR_VerboseProfiling))
      TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Starting jProfiler thread");

   _jProfilerMonitor = TR::Monitor::create("JIT-jProfilerMonitor");
   _state = Initial;
   if (_jProfilerMonitor)
      {
      // Try to create thread
      if (javaVM->internalVMFunctions->createThreadWithCategory(&_jProfilerOSThread,
                                                                TR::Options::_profilerStackSize << 10,
                                                                priority, 0,
                                                                &jProfilerThreadProc,
                                                                javaVM->jitConfig,
                                                                J9THREAD_CATEGORY_SYSTEM_JIT_THREAD))
         {
         if (TR::Options::getVerboseOption(TR_VerboseProfiling))
            TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Failed to start jProfiler thread");

         // Thread creation failed, don't try again
         TR::Options::getCmdLineOptions()->setOption(TR_DisableJProfilerThread);
         _jProfilerMonitor = NULL;
         }
      else
         {
         // Wait until creation completes
         _jProfilerMonitor->enter();
         while (_state == Initial)
            _jProfilerMonitor->wait();
         _jProfilerMonitor->exit();

         if (TR::Options::getVerboseOption(TR_VerboseProfiling))
            TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Started jProfiler thread");
         }
      }
   else
      {
      if (TR::Options::getVerboseOption(TR_VerboseProfiling))
         TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Failed to create JIT-jProfilerMonitor");
      }
   }

/**
 * Stop the profiler thread.
 * This will set the state to Stop and wait.
 */
void
TR_JProfilerThread::stop(J9JavaVM *javaVM)
   {
   PORT_ACCESS_FROM_JAVAVM(javaVM);
   if (!_jProfilerMonitor)
      return;

   // Check the thread actually started
   _jProfilerMonitor->enter();
   if (!getJProfilerThread())
      {
      _jProfilerMonitor->exit();
      return;
      }

   if (TR::Options::getVerboseOption(TR_VerboseProfiling))
      TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Stopping jProfiler thread");

   // Set state and wait for it to stop
   _state = Stop;
   while (getJProfilerThread())
      {
      _jProfilerMonitor->notifyAll();
      _jProfilerMonitor->wait();
      }

   if (TR::Options::getVerboseOption(TR_VerboseProfiling))
      TR_VerboseLog::writeLineLocked(TR_Vlog_PROFILING, "Stopped jProfiler thread");

   _jProfilerMonitor->exit();
   }

/**
 * Process the persistent profile info.
 * Should only be called in the profiler thread.
 */
void
TR_JProfilerThread::processWorkingQueue()
   {
   while (_state == Run)
      {
      _jProfilerMonitor->enter();

      _jProfilerMonitor->wait_timed(_waitMillis, 0);
      if (_state == Stop)
         {
         _jProfilerMonitor->exit();
         break;
         }

      _jProfilerMonitor->exit();

      /*
       * prevPtr has a type of "TR_PersistentProfileInfo * volatile *"
       * This mix of pointers and volatile can be hard to make sense of. To break it down:
       *
       * _listHead has the type "TR_PersistentProfileInfo * volatile" which is a volatile pointer to a non-volatile TR_PersistentProfileInfo.
       * It is the pointer value itself that is volatile not the data it is pointing to.
       *
       * prevPtr is a pointer to _listHead. prevPtr itself is non-volatile.
       * When put together, prevPtr is a non-volatile pointer to a volatile pointer to a non-volatile TR_PersistentProfileInfo.
       */
      TR_PersistentProfileInfo * volatile *prevPtr = &_listHead;
      TR_PersistentProfileInfo *cur = _listHead;

      size_t index = 0;
      while (cur && _state == Run)
         {
         if (cur->_refCount == 0)
            {
            // Clear out entries that are no longer referenced
            cur = deleteProfileInfo(prevPtr, cur);
            continue;
            }
         else if (cur->isActive() && cur->getValueProfileInfo())
            {
            // Inspect value profile info
            cur->getValueProfileInfo()->resetLowFreqValues(NULL);
            }

         prevPtr = &cur->_next;
         cur = *prevPtr;
         }
      }
   }

/**
 * Add a new entry at the start of the linked list.
 * Can be called by compilation threads concurrently.
 *
 * \param newHead Info to add to the list.
 */
void
TR_JProfilerThread::addProfileInfo(TR_PersistentProfileInfo *newHead)
   {
   TR_PersistentProfileInfo *oldHead;
   uintptr_t oldPtr;

   // Atomic update for list head
   do {
      oldHead = _listHead;
      oldPtr = reinterpret_cast<uintptr_t>(oldHead);
      newHead->_next = oldHead;
      }
   while (oldPtr != VM_AtomicSupport::lockCompareExchange(reinterpret_cast<volatile uintptr_t*>(&_listHead), oldPtr, reinterpret_cast<uintptr_t>(newHead)));

   VM_AtomicSupport::add(&_footprint, 1);
   }

/**
 * Remove an arbitrary entry from the linked list.
 * Should only be called by the thread, as it does not support concurrent removals.
 *
 * \param prevNext Pointer to previous info's next pointer. This is a non-volatile pointer to a volatile pointer to a non-volatile TR_PersistentProfileInfo.
 * \param info Info to remove.
 * \return The next TR_PersistentProfileInfo after the removed info.
 */
TR_PersistentProfileInfo *
TR_JProfilerThread::deleteProfileInfo(TR_PersistentProfileInfo * volatile *prevNext, TR_PersistentProfileInfo *info)
   {
   TR_PersistentProfileInfo *next = info->_next;
   uintptr_t oldPtr = reinterpret_cast<uintptr_t>(info);

   if (oldPtr != VM_AtomicSupport::lockCompareExchange(reinterpret_cast<volatile uintptr_t*>(prevNext), oldPtr, reinterpret_cast<uintptr_t>(next)))
      return next;

   if (!TR::Options::getCmdLineOptions()->getOption(TR_DisableProfilingDataReclamation))
      {
      VM_AtomicSupport::subtract(&_footprint, 1);
      if (TR::Options::isAnyVerboseOptionSet(TR_VerboseProfiling, TR_VerboseReclamation))
         TR_VerboseLog::writeLineLocked(TR_Vlog_RECLAMATION, "Reclaiming PersistentProfileInfo 0x%p.", info);
      // Deallocate info
      info->~TR_PersistentProfileInfo();
      TR_Memory::jitPersistentFree(info);
      }

   return next;
   }

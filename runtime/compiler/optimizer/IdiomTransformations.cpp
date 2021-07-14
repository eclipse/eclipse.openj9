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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen/CodeGenerator.hpp"
#include "env/FrontEnd.hpp"
#include "compile/Compilation.hpp"
#include "compile/SymbolReferenceTable.hpp"
#include "control/Options.hpp"
#include "control/Options_inlines.hpp"
#include "cs2/bitvectr.h"
#include "env/CompilerEnv.hpp"
#include "env/TRMemory.hpp"
#include "env/jittypes.h"
#include "il/AutomaticSymbol.hpp"
#include "il/Block.hpp"
#include "il/DataTypes.hpp"
#include "il/ILOpCodes.hpp"
#include "il/ILOps.hpp"
#include "il/ILProps.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/Symbol.hpp"
#include "il/SymbolReference.hpp"
#include "il/TreeTop.hpp"
#include "il/TreeTop_inlines.hpp"
#include "infra/Assert.hpp"
#include "infra/BitVector.hpp"
#include "infra/Cfg.hpp"
#include "infra/List.hpp"
#include "optimizer/IdiomRecognition.hpp"
#include "optimizer/IdiomRecognitionUtils.hpp"
#include "optimizer/Optimization_inlines.hpp"
#include "optimizer/Optimizer.hpp"
#include "optimizer/UseDefInfo.hpp"
#include "ras/Debug.hpp"

#define OPT_DETAILS "O^O NEWLOOPREDUCER: "
#define DISPTRACE(OBJ) ((OBJ)->trace())
#define VERBOSE(OBJ) ((OBJ)->showMesssagesStdout())
#define PNEW new (PERSISTENT_NEW)

/** \brief
 *     Determines whether we should avoid transforming loops in java/lang/String due to functional issues when String
 *     compression is enabled.
 *
 *  \param comp
 *     The compilation object.
 *
 *  \return
 *     <c>true</c> if the transformation should be avoided, <c>false</c> otherwise.
 */
static bool avoidTransformingStringLoops(TR::Compilation* comp)
   {
   static bool cacheInitialized = false;
   static bool cacheValue = false;

   if (!cacheInitialized)
      {
      // TODO: This is a workaround for Java 829 functionality as we switched to using a byte[] backing array in String*.
      // Remove this workaround once obsolete. Idiom recognition currently does not handle idioms involving char[] in a
      // compressed string. String compression is technically a Java 9 feature, but for the sake of evaluating performance
      // we need to be able to run our standard set of benchmarks, most of which do not work under Java 9 at the moment.
      // This leaves us with the only option to run the respective benchmarks on Java 8 SR5, however in Java 8 SR5 the
      // java/lang/String.value is of type char[] which will cause functional problems. To avoid these issues we will
      // disable idiom recognition on Java 8 SR5 if String compression is enabled.
      TR_OpaqueClassBlock* stringClass = comp->cg()->fej9()->getSystemClassFromClassName("java/lang/String", strlen("java/lang/String"), true);

      if (stringClass != NULL)
         {
         // Only initialize the cache after we are certain java/lang/String has been resolved
         cacheInitialized = true;

         if (comp->cg()->fej9()->getInstanceFieldOffset(stringClass, "value", "[C") != ~0)
            {
            cacheValue = IS_STRING_COMPRESSION_ENABLED_VM(static_cast<TR_J9VMBase*>(comp->fe())->getJ9JITConfig()->javaVM);
            }
         }
      }

   return cacheValue;
   }

//*****************************************************************************************
// It partially peels the loop body to align the top of the region
//*****************************************************************************************
bool
ChangeAlignmentOfRegion(TR_CISCTransformer *trans)
   {
   const bool disptrace = DISPTRACE(trans);
   TR_CISCGraph *P = trans->getP();
   TR_CISCGraph *T = trans->getT();
   TR_CISCNode *pTop = P->getEntryNode()->getSucc(0);
   TR_CISCNode *t;
   TR_CISCNode *beforeLoop = NULL;
   bool changed = false;

   TR::Compilation * comp = trans->comp();

   // Find actual pTop. Skip an optional node if there is no corresponding target node.
   while (trans->getP2TRep(pTop) == NULL)
      {
      if (!pTop->isOptionalNode()) return changed;
      pTop = pTop->getSucc(0);
      }

   // Try to find pTop in the predecessors of the loop body
   for (t = T->getEntryNode(); t->isOutsideOfLoop();)
      {
      if (trans->analyzeT2P(t, pTop) & _T2P_MatchMask)
         {
         TR_CISCNode *chk;
         for (chk = t->getSucc(0); chk->isOutsideOfLoop(); chk=chk->getSucc(0))
            {
            if (!chk->isNegligible() && trans->analyzeT2P(chk) == _T2P_NULL) break; // t is still invalid.
            }
         if (!chk->isOutsideOfLoop())
            {
            if (disptrace) traceMsg(comp, "ChangeAlignmentOfRegion : (t:%d p:%d) no need to change alignment\n",t->getID(),pTop->getID());
            return changed;        // Find pTop! Already aligned correctly
            }
         }
      if (t->getNumSuccs() < 1)
         {
         if (disptrace) traceMsg(comp,"ChangeAlignmentOfRegion : #succs of tID:%d is 0\n", t->getID());
         return changed;    // cannot find either a loop body or pTop in the fallthrough path
         }
      beforeLoop = t;
      switch(t->getOpcode())
         {
         case TR::lookup:
         case TR::table:
            int i;
            for (i = t->getNumSuccs(); --i >= 0; )
               {
               TR_CISCNode *next_t = t->getSucc(i);
               if (next_t->getOpcode() == TR::Case &&
                   next_t->getSucc(0) != T->getExitNode())
                  {
                  t = next_t->getSucc(0);
                  goto exit_switch;
                  }
               }
            // fall through
         default:
            t = t->getSucc(0);
            break;
         }
      exit_switch:;
      }
   TR_ASSERT(beforeLoop, "error");
   if (t->getOpcode() != TR::BBStart) return changed;    // already aligned by this transformation before
   t = t->getSucc(0);   // Skip BBStart

   int condT2P = trans->analyzeT2P(t, pTop);
   if (condT2P & _T2P_MatchMask) return changed;    // no need to change alignment

   if (disptrace) traceMsg(comp,"ChangeAlignmentOfRegion : tTop %d, pTop %d\n",t->getID(),pTop->getID());
   TR_CISCNodeRegion r(T->getNumNodes(), trans->trMemory()->heapMemoryRegion());
   TR_CISCNode *firstNode = t;
   TR_CISCNode *lastNode = NULL;
   // Find the target node corresponding to pTop
   int branchCount = 0;
   for (;;)
      {
      if (condT2P != _T2P_NULL || !t->isNegligible()) lastNode = t;
      t = t->getSucc(0);
      if (t->getOpcode() == TR::BBEnd || t->getOpcode() == TR_exitnode || t == firstNode) return changed; // current limitation. peeling can be performed within the first BB of the body
      if (t->getIlOpCode().isBranch())
         if (++branchCount >= 2) return changed;        // allow a single branch
      condT2P = trans->analyzeT2P(t, pTop);
      if (condT2P & _T2P_MatchMask)
         break; // the target node corresponding to pTop is found
      }
   if (!lastNode) return changed;   // the last node of the peeling region
   TR_CISCNode *foundNode = lastNode->getSucc(0);

   // Find the last non-negligible node
   if (lastNode->isNegligible())
      {
      TR_CISCNode *lastNonNegligble = NULL;
      for (t = firstNode; ;t = t->getSucc(0))
         {
         if (!t->isNegligible()) lastNonNegligble = t;
         if (t == lastNode) break;
         }
      if (!lastNonNegligble) return changed;
      lastNode = lastNonNegligble;
      }

   // Add nodes from firstNode to lastNode into the region r
   if (disptrace) traceMsg(comp, "ChangeAlignmentOfRegion : foundNode %d, lastNode %d\n",foundNode->getID(),lastNode->getID());
   if (branchCount > 0 &&
       !lastNode->getIlOpCode().isBranch())
      {
      if (disptrace) traceMsg(comp, "Fail: there is a branch in the region. lastNode must be a branch node.\n");
      return changed;
      }
   for (t = firstNode; ;t = t->getSucc(0))
      {
      r.append(t);
      if (t == lastNode) break;
      }

   // analyze that all parents of every node in the region r are included in the region r.
   ListIterator<TR_CISCNode> ri(&r);
   for (t = ri.getFirst(); t; t = ri.getNext()) // each node in the region r
      {
      if (t->getOpcode() == TR::aload || t->getOpcode() == TR::iload)
         {
         bool noDefInR = true;
         ListIterator<TR_CISCNode> chain(t->getChains());
         TR_CISCNode *def;
         for (def = chain.getFirst(); def; def = chain.getNext())
            {
            if (r.isIncluded(def))
               {
               noDefInR = false;
               break;
               }
            }
         if (noDefInR) continue;        // If there is no def in r, it ignores this load node.
         }
      ListIterator<TR_CISCNode> pi(t->getParents());
      TR_CISCNode *pn;
      for (pn = pi.getFirst(); pn; pn = pi.getNext())   // each parent of t
         {
         if (!r.isIncluded(pn))
            {
            if (disptrace) traceMsg(comp,"ChangeAlignmentOfRegion : There is a parent(%d) of %d in the outside of the region\n", pn->getID(), t->getID());
            return changed; // fail
            }
         }
      }

   /////////////////////////////
   // From here, success path //
   /////////////////////////////
   T->duplicateListsDuplicator();
   changed = true;
   TR_CISCNode *from = r.getListHead()->getData();
   TR_CISCNode *to = r.getListTail()->getData();
   if (disptrace)
      {
      traceMsg(comp,"ChangeAlignmentOfRegion: Succ[0] of %d will be changed from %d to %d.\n",
             beforeLoop->getID(),
             beforeLoop->getSucc(0)->getID(),
             foundNode->getID());
      traceMsg(comp,"\tNodes from %d to %d will be added to BeforeInsertionList.\n",
             from->getID(),to->getID());
      }
   TR_ASSERT(r.getListTail()->getData()->getIlOpCode().isTreeTop(), "error");
   beforeLoop->replaceSucc(0, foundNode);       // replace the loop entry with foundNode
   TR_NodeDuplicator duplicator(comp);
   for (t = ri.getFirst(); t; t = ri.getNext())
      {
      if (t->getIlOpCode().isTreeTop())
         {
         TR::Node *rep = t->getHeadOfTrNodeInfo()->_node;
         if (disptrace)
            {
            traceMsg(comp,"add TR::Node 0x%p (tid:%d) to BeforeInsertionList.\n", rep, t->getID());
            }
         rep = duplicator.duplicateTree(rep);
         if (t->getIlOpCode().isIf())
            {
            if (t->getOpcode() != rep->getOpCodeValue())
               {
               TR::TreeTop *ret;
               for (ret = t->getHeadOfTreeTop()->getNextTreeTop();
                    ret->getNode()->getOpCodeValue() != TR::BBStart;
                    ret = ret->getNextTreeTop());
               TR::Node::recreate(rep, (TR::ILOpCodes)t->getOpcode());
               rep->setBranchDestination(ret);
               }
            }
         trans->getBeforeInsertionList()->append(rep);
         }
      }
   // Move the region ("from" - "to") to the last
   trans->moveCISCNodes(from, to, NULL);

   if (disptrace && changed)
      {
      traceMsg(comp,"After ChangeAlignmentOfRegion\n");
      T->dump(comp->getOutFile(), comp);
      }
   return changed;
   }


//*****************************************************************************************
// Analyze whether we can move the node n to immediately before the nodes in tgt.
// Both the node n and a node in tgt must be included in the list l.
// If the analysis fails, it will return NULL.
// Otherwise, it will return the target node, which must be included in the list tgt.
//*****************************************************************************************
TR_CISCNode *
analyzeMoveNodeForward(TR_CISCTransformer *trans, List<TR_CISCNode> *l, TR_CISCNode *n, List<TR_CISCNode> *tgt)
   {
   const bool disptrace = DISPTRACE(trans);
   ListIterator<TR_CISCNode> ti(l);
   TR_CISCNode *t;
   TR_CISCNode *ret = NULL;

   TR::Compilation * comp = trans->comp();

   for (t = ti.getFirst(); t; t = ti.getNext())
      {
      if (t == n) break;
      }
   TR_ASSERT(t != NULL, "cannot find the node n in the list l!");

   t = ti.getNext();
   TR_ASSERT(t != NULL, "cannot find any node in tgt in the list l!");
   if (tgt->find(t)) return NULL;       // already moved

   bool go = false;
   if (n->isStoreDirect())
      {
      go = true;
      }
   else if (n->getNumChildren() == 2)
      {
      if (n->getIlOpCode().isAdd() ||
          n->getIlOpCode().isSub() ||
          n->getIlOpCode().isMul() ||
          n->getIlOpCode().isLeftShift() ||
          n->getIlOpCode().isRightShift() ||
          n->getIlOpCode().isShiftLogical() ||
          n->getIlOpCode().isAnd() ||
          n->getIlOpCode().isOr() ||
          n->getIlOpCode().isXor())   // Safe expressions
         {
         go = true;
         if (n->getChild(0)->getOpcode() == TR_variable ||
             n->getChild(1)->getOpcode() == TR_variable)
            go = false;    // not implemented yet.
         }
      }
   else if (n->getNumChildren() == 1)
      {
      if (n->getIlOpCode().isConversion() ||
          n->getIlOpCode().isNeg())   // Safe expressions
         {
         go = true;
         if (n->getChild(0)->getOpcode() == TR_variable)
            go = false;    // not implemented yet.
         }
      }
   else
      {
      if (n->getIlOpCode().isLoadConst())
         {
         go = true;
         }
      }

   if (go)
      {
      List<TR_CISCNode> *chains = n->getChains();
      List<TR_CISCNode> *parents = n->getParents();
      TR_CISCNode *specialCareIf = trans->getP()->getSpecialCareNode(0);
      bool generateCompensation0 = false;
      while(true)
         {
         if (chains->find(t)) break; // it cannot be moved beyond its use/def.
         if (parents->find(t)) break; // it cannot be moved beyond its parent.

         if (t->getOpcode() == TR::BBStart)
            {
            TR::Block *block = t->getHeadOfTrNode()->getBlock();
            if (block->getPredecessors().size() > 1) return NULL; // It currently analyzes within this BB.
            }
         if (t->getNumSuccs() >= 2 && specialCareIf)
            {
            bool fail = true;
            TR_CISCNode *p = trans->getT2Phead(t);
            if (p &&
                p == specialCareIf &&
                t->getSucc(1) == trans->getT()->getExitNode())
               {
               // add compensation code into AfterInsertionIdiomList and go ahead
               TR::Node *trNode = n->getHeadOfTrNode();
               if (trNode->getOpCode().isTreeTop())
                  {
                  if (trNode->getOpCode().isStoreDirect())
                     {
                     if (!generateCompensation0)
                        {
                        trans->getT()->duplicateListsDuplicator();
                        if (disptrace) traceMsg(comp,"analyzeMoveNodeForward: append the tree of 0x%p into AfterInsertionIdiomList\n", trNode);
                        trans->getAfterInsertionIdiomList(0)->append(trNode->duplicateTree());
                        }
                     fail = false;
                     generateCompensation0 = true;
                     }
                  // else, fail to move
                  }
               else
                  {
                  fail = false;
                  }
               }
            if (fail) break; // It currently analyzes within this BB.
            }
         t = ti.getNext();
         if (t == NULL) break; // cannot find any node in tgt in the list l.
         ret = t;
         if (tgt->find(t)) break;  // find goal!
         }
      }
   return ret;
   }


//*****************************************************************************************
// It tries to reorder target nodes to match idiom nodes within each BB.
//*****************************************************************************************
bool
reorderTargetNodesInBB(TR_CISCTransformer *trans)
   {
   TR_CISCGraph *P = trans->getP();
   TR_CISCGraph *T = trans->getT();
   List<TR_CISCNode> *T2P = trans->getT2P(), *P2T = trans->getP2T(), *l;
   TR_CISCNode *t, *p;
   bool changed = false;
   const bool disptrace = DISPTRACE(trans);

   TR::Compilation * comp = trans->comp();

   static int enable = -1;
   if (enable < 0)
      {
      char *p = feGetEnv("DISABLE_REORDER");
      enable = p ? 0 : 1;
      }
   if (!enable) return false;

   TR_BitVector visited(T->getNumNodes(), comp->trMemory());
   while(true)
      {
      ListIterator<TR_CISCNode> ti(T->getNodes());
      int currentPID = 0x10000;
      bool anyChanged = false;

      for (t = ti.getFirst(); t; t = ti.getNext())
         {
         int tID = t->getID();
         if (visited.isSet(tID)) continue;
         visited.set(tID);
         l = T2P + tID;
         if (l->isEmpty()) // There is no idiom nodes corresponding to the node t
            {
            if (t->isNegligible())
               {
               continue;   // skip the node t
               }
            else
               {
               break;      // finish this analysis
               }
            }
         int maxPid = -1;
         ListIterator<TR_CISCNode> pi(l);
         for (p = pi.getFirst(); p; p = pi.getNext())
            {
            if (p->getID() > maxPid) maxPid = p->getID();
            }
         if (maxPid >= 0)
            {
            if (maxPid <= currentPID)
               {
               currentPID = maxPid;        // no problem
               }
            else
               {
               if (t->isOutsideOfLoop()) break;    // reordering is currently supported only inside of the loop

               // Try moving the node t forward
               List<TR_CISCNode> *nextPlist = P2T+maxPid+1;
               if (disptrace)
                  {
                  ListIterator<TR_CISCNode> nextTi(nextPlist);
                  TR_CISCNode *nextT;
                  traceMsg(comp,"reorderTargetNodesInBB: Try moving the tgt node %d forward until",tID);
                  for (nextT = nextTi.getFirst(); nextT; nextT = nextTi.getNext())
                     {
                     traceMsg(comp," %p(%d)",nextT,nextT->getID());
                     }
                  traceMsg(comp,"\n");
                  }

               // Analyze whether we can move the node t to immediately before the nodes in nextPlist
               List<TR_CISCNode> *dagList = T->getDagId2Nodes()+t->getDagID();
               TR_CISCNode *tgt = analyzeMoveNodeForward(trans, dagList, t, nextPlist);
               if (tgt)
                  {
                  T->duplicateListsDuplicator();
                  // OK, we can move the node t!
                  if (disptrace) traceMsg(comp,"We can move the node %d to %p(%d)\n",tID,tgt,tgt->getID());
                  anyChanged = changed = true;

                  trans->moveCISCNodes(t, t, tgt, "reorderTargetNodesInBB");
                  break;
                  }
               }
            }
         }
      if (!anyChanged) break;
      }
   if (disptrace && changed)
      {
      traceMsg(comp,"After reorderTargetNodesInBB\n");
      T->dump(comp->getOutFile(), comp);
      }
   return changed;
   }


//*****************************************************************************************
// It replicates a store instruction outside of the loop.
// It is specialized to those idioms that include TR_booltable
// Input: SpecialCareNode(0) - the TR_booltable in the idiom
//        ImportantNode(1) - ificmpge for exiting the loop (optional)
//*****************************************************************************************
bool
moveStoreOutOfLoopForward(TR_CISCTransformer *trans)
   {
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR_CISCNode *ixload, *aload, *iload;
   TR::Compilation *comp = trans->comp();

   TR_CISCNode *boolTable = P->getSpecialCareNode(0);  // Note: The opcode isn't always TR_booltable.
   TR_CISCNode *p = boolTable->getChild(0); // just before TR_booltable, such as b2i

   TR_BitVector findBV(P->getNumNodes(), trans->trMemory(), stackAlloc);
   findBV.set(boolTable->getID());

   TR_CISCNode *optionalCmp = P->getImportantNode(1); // ificmpge
   if (optionalCmp && (optionalCmp->getOpcode() == TR::ificmpge || optionalCmp->getOpcode() == TR_ifcmpall))
      findBV.set(optionalCmp->getID());

   ListIterator<TR_CISCNode> ti(P2T + p->getID());
   TR_CISCNode *t;
   TR_CISCNode *storedVariable = NULL;
   bool success0 = false;
   TR_ScratchList<TR_CISCNode> targetList(comp->trMemory());
   for (t = ti.getFirst(); t; t = ti.getNext()) // for each target node corresponding to p
      {
      // t is a target node corresponding to p (just before TR_booltable)
      ListIterator<TR_CISCNode> tParentIter(t->getParents());
      TR_CISCNode *tParent;
      for (tParent = tParentIter.getFirst(); tParent; tParent = tParentIter.getNext())
         {
         // checking whether tParent is a store instruction
         if (tParent->isStoreDirect() &&
             !tParent->isNegligible())
            {
            // checking whether all variables of stores are same.
            if (!storedVariable) storedVariable = tParent->getChild(1);
            else if (storedVariable != tParent->getChild(1))
               {
               if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward failed because all variables of stores are not same.\n");
               success0 = false;
               goto endSpecial0;        // FAIL!
               }

            // checking whether tParent will reach either boolTable or optionalCmp
            if (checkSuccsSet(trans, tParent, &findBV))
               {
               success0 = true;         // success for this t
               break;
               }
            else
               {
               if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward failed because tParent will not reach either boolTable or optionalCmp.\n");
               success0 = false;
               goto endSpecial0;        // FAIL!
               }
            }
         }
      if (tParent) targetList.add(tParent);    // add a store instruction
      }
   endSpecial0:

   if (targetList.isEmpty())
      {
      if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward failed because targetList is empty.\n");
      success0 = false;
      }
   // check if descendants of p include an array load
   if (!getThreeNodesForArray(p, &ixload, &aload, &iload, true))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward failed because decendents of pid:%d don't include an array load.\n", p->getID());
      success0 = false;
      }

   if (success0)
      {
      ixload = trans->getP2TRep(ixload);
      aload = trans->getP2TRep(aload);
      iload = trans->getP2TRep(iload);
      if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward: Target nodes ixload=%d, aload=%d, iload=%d\n",
                                    ixload ? ixload->getID() : -1, aload ? aload->getID() : -1, iload ? iload->getID() : -1);
      trans->getT()->duplicateListsDuplicator();
      if (ixload && aload && iload && (iload->isLoadVarDirect() || iload->getOpcode() == TR_variable))
         {
         TR::Node *store;
         TR::Node *conv;
         TR::Node *storeDup0;
         TR::Node *storeDup1;
         TR::Node *convDup;
         TR::Node *ixloadNode = ixload->getHeadOfTrNodeInfo()->_node;
         TR::Node *iloadNode = iload->getHeadOfTrNodeInfo()->_node;      // index
         TR::Node *iloadm1Node = createOP2(comp, TR::isub,
                                          TR::Node::createLoad(iloadNode, iloadNode->getSymbolReference()),
                                          TR::Node::create(iloadNode, TR::iconst, 0, 1));

         // prepare base[index]
         TR::Node *arrayLoad0 = createArrayLoad(comp, trans->isGenerateI2L(),
                                               ixloadNode,
                                               aload->getHeadOfTrNodeInfo()->_node,
                                               iloadNode,
                                               ixloadNode->getSize());

         // prepare base[index-1]  (it may not be used.)
         TR::Node *arrayLoad1 = createArrayLoad(comp, trans->isGenerateI2L(),
                                               ixloadNode,
                                               aload->getHeadOfTrNodeInfo()->_node,
                                               iloadm1Node,
                                               ixloadNode->getSize());
         ti.set(&targetList);
         t = ti.getFirst();
         store = t->getHeadOfTrNodeInfo()->_node;
         conv = store->getChild(0);
         if (conv->getOpCode().isConversion())
            {
            convDup = TR::Node::create(conv->getOpCodeValue(), 1, arrayLoad0);
            storeDup0 = TR::Node::createStore(store->getSymbolReference(), convDup);
            convDup = TR::Node::create(conv->getOpCodeValue(), 1, arrayLoad1);
            storeDup1 = TR::Node::createStore(store->getSymbolReference(), convDup);
            }
         else
            {
            storeDup0 = TR::Node::createStore(store->getSymbolReference(), arrayLoad0);
            storeDup1 = TR::Node::createStore(store->getSymbolReference(), arrayLoad1);
            }
         trans->getAfterInsertionIdiomList(0)->append(storeDup0);       // base[index]
         trans->getAfterInsertionIdiomList(1)->append(storeDup1);       // base[index-1] (it may not be used.)
         if (VERBOSE(trans)) printf("%s moveStoreOutOfLoopForward\n", trans->getT()->getTitle());
         if (DISPTRACE(trans)) traceMsg(comp, "moveStoreOutOfLoopForward adds %d into compensation code [0] and [1]\n", t->getID());
         for (; t; t = ti.getNext()) t->setIsNegligible();      // set negligible to all stores
         }
      else
         success0 = false;
      }

   return success0;
   }


//*****************************************************************************************
// It analyzes redundant IAND. It is specialized to MEMCPYxxx2Byte, such as MEMCPYChar2Byte.
// Input: SpecialCareNode(*) - a set of conversions, such as i2b
//*****************************************************************************************
bool
IANDSpecialNodeTransformer(TR_CISCTransformer *trans)
   {
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   int idx;
   bool ret = false;

   for (idx = 0; idx < MAX_SPECIALCARE_NODES; idx++)
      {
      TR_CISCNode *p = P->getSpecialCareNode(idx);
      if (!p) break;
      ListIterator<TR_CISCNode> ti(P2T + p->getID());
      TR_CISCNode *t;
      for (t = ti.getFirst(); t; t = ti.getNext())
         {
         if (t->getOpcode() != TR::i2b) continue; // not implemented yet for other OPs
         TR_CISCNode *ch = t->getChild(0);
         if (ch->isNegligible()) continue;

         // example: the following two IANDs are redundant.
         //          dst = (byte)(((ch & 0xFF00) >> 8) & 0xFF)
         //                            ^^^^^^^^        ^^^^^^
         switch(ch->getOpcode())
            {
            case TR::iand:
               if (!ch->getParents()->isSingleton() ||
                   !testIConst(ch, 1, 0xFF)) return false;      // child(1) is "iconst 0xff"
               ch->setIsNegligible();   // this IAND can be negligible!
               ret = true;

               ch = ch->getChild(0);
               if (ch->getOpcode() != TR::ishr && ch->getOpcode() != TR::iushr) break;
               // fall through if TR::ishr
            case TR::ishr:
            case TR::iushr:
               if (!testIConst(ch, 1, 0x8)) break;      // child(1) is "iconst 0x8"

               ch = ch->getChild(0);
               if (ch->getOpcode() != TR::iand) break;
               if (!ch->getParents()->isSingleton() ||
                   !testIConst(ch, 1, 0xFF00)) return false;      // child(1) is "iconst 0xFF00"
               ch->setIsNegligible();   // this SHR can be negligible!
               ret = true;
               break;
            }
         }
      }
   return ret;
   }

//////////////////////////////////////////////////////////////////////////
// utility routines

static void
findIndexLoad(TR::Node *aiaddNode, TR::Node *&index1, TR::Node *&index2, TR::Node *&topLevelIndex)
   {
   // iiload
   //      aiadd              <-- aiaddNode
   //           aload
   //           isub
   //             imul
   //                iload    <-- looking for the index
   //                iconst 4
   //              iconst -16
   //
   // -or-
   // iiload
   //      aiadd
   //           aload
   //           isub
   //              iload
   //              iconst
   //
   // -or-
   // iiload
   //      aiadd              <-- aiaddNode
   //           aload
   //           isub
   //             imul
   //                iadd
   //                    iload    <-- looking for the index
   //                    iload    <-- looking for the index
   //                iconst 4
   //              iconst -16
   //
   // -or-
   // iiload
   //      aiadd
   //           aload
   //           isub
   //              iadd
   //                  iload    <-- looking for the index
   //                  iload    <-- looking for the index
   //              iconst
   //
   index1 = NULL;
   index2 = NULL;
   topLevelIndex = NULL;
   TR::Node *addOrSubNode = aiaddNode->getSecondChild();
   if (addOrSubNode->getOpCode().isAdd() || addOrSubNode->getOpCode().isSub())
      {
      TR::Node *grandChild = NULL;
      if (addOrSubNode->getFirstChild()->getOpCode().isMul())
         grandChild = addOrSubNode->getFirstChild()->getFirstChild();
      else
         grandChild = addOrSubNode->getFirstChild();

      if (grandChild->getOpCodeValue() == TR::i2l)
         grandChild = grandChild->getFirstChild();

      topLevelIndex = grandChild;

      if (grandChild->getOpCode().hasSymbolReference())
         {
         index1 = grandChild;
         }
      else if (grandChild->getOpCode().isAdd() || grandChild->getOpCode().isSub())
         {
         TR::Node *grandGrandChild1 = grandChild->getFirstChild();
         TR::Node *grandGrandChild2 = grandChild->getSecondChild();
         while(grandGrandChild1->getOpCode().isAdd() || grandGrandChild1->getOpCode().isSub())
            {
            grandGrandChild2 = grandGrandChild1->getSecondChild();
            grandGrandChild1 = grandGrandChild1->getFirstChild();
            }
         if (grandGrandChild1->getOpCode().hasSymbolReference())
            {
            index1 = grandGrandChild1;
            }
         if (grandGrandChild2->getOpCode().hasSymbolReference())
            {
            index2 = grandGrandChild2;
            }
         }
      }
   }


// get the iv thats involved in the looptest
//
static bool
usedInLoopTest(TR::Compilation *comp, TR::Node *loopTestNode, TR::SymbolReference *srcSymRef)
   {
   TR::Node *ivNode = loopTestNode->getFirstChild();
   if (ivNode->getOpCode().isAdd() || ivNode->getOpCode().isSub())
      ivNode = ivNode->getFirstChild();

   if (ivNode->getOpCode().hasSymbolReference())
      {
      if (ivNode->getSymbolReference()->getReferenceNumber() == srcSymRef->getReferenceNumber())
         return true;
      }
   else dumpOptDetails(comp, "iv %p in the loop test %p has no symRef?\n", ivNode, loopTestNode);
   return false;
   }

static bool
indexContainsArray(TR::Compilation *comp, TR::Node *index, vcount_t visitCount)
   {
   if (index->getVisitCount() == visitCount)
      return false;

   index->setVisitCount(visitCount);

   if (comp->trace(OMR::idiomRecognition))
      traceMsg(comp, "analyzing node %p\n", index);

   if (index->getOpCode().hasSymbolReference() &&
         index->getSymbolReference()->getSymbol()->isArrayShadowSymbol())
      {
      if (comp->trace(OMR::idiomRecognition))
         traceMsg(comp, "found array node %p\n", index);
      return true;
      }

   for (int32_t i = 0; i < index->getNumChildren(); i++)
      if (indexContainsArray(comp, index->getChild(i), visitCount))
         return true;

   return false;
   }


static bool
indexContainsArrayAccess(TR::Compilation *comp, TR::Node *aXaddNode)
   {
   if (comp->trace(OMR::idiomRecognition))
      traceMsg(comp, "axaddnode %p\n", aXaddNode);

   TR::Node *loadNode1, *loadNode2, *topLevelIndex;
   findIndexLoad(aXaddNode, loadNode1, loadNode2, topLevelIndex);
   // topLevelIndex now contains the actual expression q in a[q]
   // if q contains another array access, then we cannot reduce
   // this loop into an arraycopy
   // ie. a[b[i]] do not represent linear array accesses
   //
   if (comp->trace(OMR::idiomRecognition))
      traceMsg(comp, "aXaddNode %p topLevelIndex %p\n", aXaddNode, topLevelIndex);
   vcount_t visitCount = comp->incOrResetVisitCount();
   if (topLevelIndex)
      return indexContainsArray(comp, topLevelIndex, visitCount);
   return false;
   }

// isIndexVariableInList checks whether the induction (index) variable symbol(s)
// from the given 'node' subtree is found inside 'nodeList'.
//
// Returns true if
//    1. one induction variable symbol is found in the list.
// Returns false if
//    1. no induction variables are found.
//    2. two induction variables found in 'node' tree are both in the list.
//            i.e.   a[i+j]
//                   i++;
//                   j++;
//                In this case, the access pattern of the array would skip every
//                other element.
static bool
isIndexVariableInList(TR::Node *node, List<TR::Node> *nodeList)
   {
   TR::Symbol *indexSymbol1 = NULL, *indexSymbol2 = NULL;
   TR::Node *loadNode1, *loadNode2, *topLevelIndex;

   findIndexLoad(node->getOpCode().isAdd() ? node : node->getFirstChild(),
                 loadNode1, loadNode2, topLevelIndex);
   if (loadNode1)
      indexSymbol1 = loadNode1->getSymbolReference()->getSymbol();
   if (loadNode2)
      indexSymbol2 = loadNode2->getSymbolReference()->getSymbol();

   bool foundSymbol1 = false, foundSymbol2 = false;

   if (indexSymbol1 || indexSymbol2)
      {
      // Search the node list for the index symbol(s).
      ListIterator<TR::Node> li(nodeList);
      TR::Node *store;
      for (store = li.getFirst(); store; store = li.getNext())
         {
         TR::Symbol *storeSymbol = store->getSymbolReference()->getSymbol();
         if (indexSymbol1 == storeSymbol)
            foundSymbol1 = true;
         if (indexSymbol2 && indexSymbol2 == storeSymbol)
            foundSymbol2 = true;
         }
      }

   // Return true only if either one symbol is found, but not both.
   return foundSymbol1 ^ foundSymbol2;
   }


// for the memCmp transformer
//
static bool
indicesAndStoresAreConsistent(TR::Compilation *comp, TR::Node *lhsSrcNode, TR::Node *rhsSrcNode, TR_CISCNode *lhsNode, TR_CISCNode *rhsNode)
   {
   // lhs and rhs indicate the two arrays involved in the comparison test
   //
   //
   TR_ScratchList<TR::Node> variableList(comp->trMemory());
   if (lhsNode)
      variableList.add(lhsNode->getHeadOfTrNode());
   if (rhsNode && rhsNode != lhsNode)
      variableList.add(rhsNode->getHeadOfTrNode());
   return (isIndexVariableInList(lhsSrcNode, &variableList) &&
           isIndexVariableInList(rhsSrcNode, &variableList));
   }

static TR::Node* getArrayBase(TR::Node *node)
   {
   if (node->getOpCode().hasSymbolReference() &&
         node->getSymbolReference()->getSymbol()->isArrayShadowSymbol())
      {
      node = node->getFirstChild();
      if (node->getOpCode().isArrayRef()) node = node->getFirstChild();
      if (node->getOpCode().isIndirect()) node = node->getFirstChild();
      return node;
      }
   return NULL;
   }

static bool
areArraysInvariant(TR::Compilation *comp, TR::Node *inputNode, TR::Node *outputNode, TR_CISCGraph *T)
   {
   if (T)
      {
      TR::Node *aNode = getArrayBase(inputNode);
      TR::Node *bNode = getArrayBase(outputNode);

      if (comp->trace(OMR::idiomRecognition))
         traceMsg(comp, "aNode = %p bNode = %p\n", aNode, bNode);
      if (aNode && aNode->getOpCode().isLoadDirect() &&
            bNode && bNode->getOpCode().isLoadDirect())
         {
         TR_CISCNode *aCNode = T->getCISCNode(aNode);
         TR_CISCNode *bCNode = T->getCISCNode(bNode);

         if (comp->trace(OMR::idiomRecognition))
            traceMsg(comp, "aC = %p %d bC = %p %d\n", aCNode, aCNode->getID(), bCNode, bCNode->getID());
         if (aCNode && bCNode)
            {
            ListIterator<TR_CISCNode> aDefI(aCNode->getChains());
            ListIterator<TR_CISCNode> bDefI(bCNode->getChains());
            TR_CISCNode *ch;
            for (ch = aDefI.getFirst(); ch; ch = aDefI.getNext())
               {
               if (ch->getDagID() == aCNode->getDagID())
                  {
                  traceMsg(comp, "def %d found inside loop for %d\n", ch->getID(), aCNode->getID());
                  return false;
                  }
               }
            for (ch = bDefI.getFirst(); ch; ch = bDefI.getNext())
               {
               if (ch->getDagID() == bCNode->getDagID())
                  {
                  traceMsg(comp, "def %d found inside loop for %d\n", ch->getID(), bCNode->getID());
                  return false;
                  }
               }
            }
         }
      }
   return true;
   }


// used for a TRTO reduction in java/io/DataOutputStream.writeUTF(String)
//
static TR::Node *
areDefsOnlyInsideLoop(TR::Compilation *comp, TR_CISCTransformer *trans, TR::Node *outputNode)
   {
   bool extraTrace = DISPTRACE(trans);

   if (extraTrace)
      traceMsg(trans->comp(), "finding defs for index used in tree %p\n", outputNode);

   TR_UseDefInfo *info = trans->optimizer()->getUseDefInfo();
   if (info)
      {
      TR::Node *loadNode = NULL, *loadNode1, *loadNode2, *topLevelIndex;
      findIndexLoad(outputNode, loadNode1, loadNode2, topLevelIndex);

      if (loadNode1 && loadNode2) return NULL; // Try to keep the original semantics, but it may be too strict.
      loadNode = loadNode1 ? loadNode1 : loadNode2;

      if (loadNode)
         {
         uint16_t useDefIndex = loadNode->getUseDefIndex();
         TR_UseDefInfo::BitVector defs(comp->allocator());
         info->getUseDef(defs, useDefIndex);
         if (!defs.IsZero())
            {
            TR_UseDefInfo::BitVector::Cursor cursor(defs);
            int32_t numDefs = 0;
            TR::TreeTop *defTT = NULL;
            for (cursor.SetToFirstOne(); cursor.Valid(); cursor.SetToNextOne())
               {
               int32_t defIndex = cursor;
               if (defIndex < info->getFirstRealDefIndex())
                  continue; // method entry is def
               defTT = info->getTreeTop(defIndex);
               numDefs++;
               }
            // if the only def is one inside the loop, then
            // insert the def before the translation node
            //
            if (numDefs == 1)
               {
               TR::Block *defBlock = defTT->getEnclosingBlock();
               if (extraTrace)
                  traceMsg(trans->comp(), "found single def %p for load %p\n", defTT->getNode(), loadNode);
               if (trans->isBlockInLoopBody(defBlock))
                  return (defTT->getNode()->duplicateTree(trans->comp()));
               }
            }
         }
      }
   return NULL;
   }


static void
findIndVarLoads(TR::Node *node, TR::Node *indVarStoreNode, bool &storeFound,
                  List<TR::Node> *ivLoads, TR::Symbol *ivSym, vcount_t visitCount)
   {
   if (node->getVisitCount() == visitCount)
      return;
   node->setVisitCount(visitCount);

   if (node == indVarStoreNode)
      storeFound = true;

   if (node->getOpCodeValue() == TR::iload &&
        node->getSymbolReference()->getSymbol() == ivSym)
      {
      if (!ivLoads->find(node))
         ivLoads->add(node);
      }

   for (int32_t i = 0; i < node->getNumChildren(); i++)
      findIndVarLoads(node->getChild(i), indVarStoreNode, storeFound, ivLoads, ivSym, visitCount);
   }

static int32_t
checkForPostIncrement(TR::Compilation *comp, TR::Block *loopHeader, TR::Node *loopCmpNode, TR::Symbol *ivSym)
   {
   TR::TreeTop *startTree = loopHeader->getFirstRealTreeTop();
   TR::Node *indVarStoreNode = NULL;
   TR::TreeTop *tt;
   for (tt = startTree; tt != loopHeader->getExit(); tt = tt->getNextTreeTop())
      {
      TR::Node *n = tt->getNode();
      if (n->getOpCode().isStoreDirect() &&
            (n->getSymbolReference()->getSymbol() == ivSym) /*&&
            n->getFirstChild()->getSecondChild()->getOpCode().isLoadConst()*/)
         {
         indVarStoreNode = n;
         break;
         }
      }
   if (!indVarStoreNode)
      return 0;

   bool storeFound = false;
   vcount_t visitCount = comp->incOrResetVisitCount();
   TR_ScratchList<TR::Node> ivLoads(comp->trMemory());
   for (tt = startTree; !storeFound && tt != loopHeader->getExit(); tt = tt->getNextTreeTop())
      findIndVarLoads(tt->getNode(), indVarStoreNode, storeFound, &ivLoads, ivSym, visitCount);

   TR::Node *cmpFirstChild = loopCmpNode->getFirstChild();

   TR::Node *storeIvLoad = indVarStoreNode->getFirstChild();
   if (storeIvLoad->getOpCode().isAdd() || storeIvLoad->getOpCode().isSub())
      storeIvLoad = storeIvLoad->getFirstChild();

   if(comp->trace(OMR::idiomRecognition))
      traceMsg(comp, "found storeIvload %p cmpFirstChild %p\n", storeIvLoad, cmpFirstChild);
   // simple case
   // the loopCmp uses the un-incremented value
   // of the iv
   //
   if (storeIvLoad == cmpFirstChild)
      return 1;

   // the loopCmp uses some load of the iv that
   // was commoned
   //
   if (ivLoads.find(cmpFirstChild))
      return 1;

   // uses a brand new load of the iv
   return 0;
   }

static bool
checkByteToChar(TR::Compilation *comp, TR::Node *iorNode, TR::Node *&inputNode, bool bigEndian)
   {
   // this is the pattern thats being reduced
   //
   //   ior
   //     imul
   //        bu2i
   //          ibload #261  Shadow[<array-shadow>]
   //            aiadd   <flags:"0x8000" (internalPtr )/>
   //              aload #523  Auto[<temp slot 10>]
   //              isub
   //                ==>iload i
   //                iconst -17
   //        iconst 256
   //      bu2i
   //        ibload #261  Shadow[<array-shadow>]
   //          aiadd   <flags:"0x8000" (internalPtr )/>
   //            ==>aload at #523
   //            isub
   //              ==>iload i
   //              iconst -16
   //
   // for little-endian platforms,
   // char = byte[i+1] << 8 | byte[i] (ie. lower index is in the lsb)
   //
   // for big-endian platforms,
   // char = byte[i] << 8 | byte[i+1] (ie. lower index is in the msb)
   //
   // in either case, if the incoming user code is swapped, then the transformation
   // is illegal.
   //
   if (!iorNode) return false;

   TR::Node *imulNode = iorNode->getFirstChild();
   if ((imulNode->getOpCodeValue() != TR::imul) &&
         (imulNode->getOpCodeValue() != TR::ishl))
     imulNode = iorNode->getSecondChild();

   if ((imulNode->getOpCodeValue() == TR::imul) ||
         (imulNode->getOpCodeValue() == TR::ishl))
      {
      // find the index to be either i, i+1
      // if (le)
      //       if index is i+1 then inputNode = other ibload of the ior
      //       else fail
      // if (be)
      //       if index is i then inputNode = ibload child of imul
      //       else fail
      //
      TR::Node *ibloadNode = imulNode->getFirstChild()->skipConversions();
      bool plusOne = false;
      bool matchPattern = false;
      if (ibloadNode->getOpCodeValue() == TR::bloadi)
         {
         TR::Node *subNode = ibloadNode->getFirstChild()->getSecondChild();
         int32_t hdrSize = TR::Compiler->om.contiguousArrayHeaderSizeInBytes() + 1;
         if (subNode->getOpCode().isSub() &&
               subNode->getSecondChild()->getOpCode().isLoadConst())
            {
            int32_t constVal;
            if (subNode->getSecondChild()->getOpCodeValue() == TR::lconst)
               constVal = (int32_t) subNode->getSecondChild()->getLongInt();
            else
               constVal = subNode->getSecondChild()->getInt();

            if (constVal < 0) constVal = -constVal;

            if (constVal == hdrSize)
               {
               matchPattern = true;
               plusOne = true;
               }
            else if (constVal == hdrSize-1)
               {
               matchPattern = true;
               plusOne = false;
               }

            if (matchPattern)
               {
               if (bigEndian)
                  {
                  if (!plusOne)
                     {
                     inputNode = ibloadNode->getFirstChild();
                     return true;
                     }
                  else
                     return false;
                  }
               else
                  {
                  if (plusOne)
                     {
                     inputNode = iorNode->getSecondChild()->skipConversions();
                     if (inputNode->getOpCodeValue() == TR::bloadi)
                        {
                        inputNode = inputNode->getFirstChild();
                        return true;
                        }
                     else
                        return false;
                     }
                  else
                     return false;
                  }
               }
            }
         }
      }

   return false;
   }

static bool
ivIncrementedBeforeBoolTableExit(TR::Compilation *comp, TR::Node *boolTableExit,
                                 TR::Block *entryBlock,
                                 TR::SymbolReference *ivSymRef)
   {
   TR::TreeTop *startTree = entryBlock->getFirstRealTreeTop();
   TR::Node *ivStore = NULL;
   bool foundBoolTable = false;
   for (TR::TreeTop *tt = startTree; tt != entryBlock->getExit(); tt = tt->getNextTreeTop())
      {
      TR::Node *n = tt->getNode();
      if (n == boolTableExit)
         {
         foundBoolTable = true;
         break;
         }
      if (n->getOpCode().isStoreDirect() &&
            (n->getSymbolReference()->getSymbol() == ivSymRef->getSymbol()))
         ivStore = n;
      }

   if (foundBoolTable && ivStore)
      return true;
   return false;
   }




//*****************************************************************************************
// default graph transformer
// currently, it has:
//  (1) partial peeling of the loop body
//*****************************************************************************************
bool
defaultSpecialNodeTransformer(TR_CISCTransformer *trans)
   {
   bool success = ChangeAlignmentOfRegion(trans);
   success |= reorderTargetNodesInBB(trans);
   return success;
   }


//*****************************************************************************************
// graph transformer for MEMCPY
// default + IANDSpecialNodeTransformer
//*****************************************************************************************
bool
MEMCPYSpecialNodeTransformer(TR_CISCTransformer *trans)
   {
   bool success = defaultSpecialNodeTransformer(trans);
   success |= IANDSpecialNodeTransformer(trans);
   return success;
   }


//*****************************************************************************************
// graph transformer for TRT
// default + moveStoreOutOfLoopForward
//*****************************************************************************************
bool
TRTSpecialNodeTransformer(TR_CISCTransformer *trans)
   {
   bool success = moveStoreOutOfLoopForward(trans);
   success |= defaultSpecialNodeTransformer(trans);
   return success;
   }


//*****************************************************************************************
// IL code generation for exploiting the TRT (or SRST) instruction
// Input: ImportantNode(0) - booltable
//        ImportantNode(1) - ificmpge
//        ImportantNode(2) - NULLCHK
//        ImportantNode(3) - array load
//*****************************************************************************************
// Possible parameters of TR::arraytranslateAndTest
// retIndex = findbytes(uint8_t *arrayBase, int arrayIndex, uint8_t *table, int arrayLen)
// retIndex = findbytes(uint8_t *arrayBase, int arrayIndex, uint8_t *table, int arrayLen, int endLen)
// retIndex = findbytes(uint8_t *arrayBase, int arrayIndex, int findByte,   int arrayLen)
// retIndex = findbytes(uint8_t *arrayBase, int arrayIndex, int findByte,   int arrayLen, int endLen)

// If the flag charArrayTRT is set, the type of the array is "char".

bool
CISCTransform2FindBytes(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   // the arraytranslateAndTest opcode is overloaded
   // with a flag
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   TR_CISCGraph *T = trans->getT();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation * comp = trans->comp();
   bool isTRT2Char = false;
   TR::CFG *cfg = comp->getFlowGraph();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   // find the first node of the region _candidateRegion
   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2FindBytes due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   List<TR_CISCNode> *listT = P2T + P->getImportantNode(1)->getID(); // ificmpge
   TR_CISCNode *exitIfRep = trans->getP2TRepInLoop(P->getImportantNode(1));
   int32_t modLength = 0;
   if (exitIfRep)
      {
      if (exitIfRep != trans->getP2TInLoopIfSingle(P->getImportantNode(1)))
         {
         if (disptrace) traceMsg(comp, "Give up because of multiple candidates of ificmpge.\n");
         return false;
         }
      bool isDecrement;
      if (!testExitIF(exitIfRep->getOpcode(), &isDecrement, &modLength)) return false;
      if (isDecrement) return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   if (!target) // multiple successors
      {
      // current restrictions. allow only the case where there is an ificmpge node and successor is 2.
      if (listT->isEmpty() ||
          trans->getNumOfBBlistSucc() != 2)
         {
         if (disptrace) traceMsg(comp, "Currently, CISCTransform2FindBytes allows only the case where there is an ificmpge node and successor is 2.\n");
         return false;
         }
      }

   // Check if there is idiom specific node insertion.
   // Currently, it is inserted by moveStoreOutOfLoopForward() or reorderTargetNodesInBB()
   bool isCompensateCode = !trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1);

   // There is an ificmpge node and (multiple successors or need to generate idiom specific node insertion)
   bool isNeedGenIcmpge = !listT->isEmpty() && (!target || isCompensateCode);

   TR::Node *baseRepNode, *indexRepNode, *ahConstNode = NULL;
   // get each target node corresponding to p0 and p1
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode);
   // get the node corresponding to
   // aiadd
   //    aload
   //    isub   <---
   //      index
   //      headerConst
   //
   TR_CISCNode *ahConstCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3)->getChild(0)->getChild(1));

   if (ahConstCISCNode)
      {
      ahConstNode = ahConstCISCNode->getHeadOfTrNodeInfo()->_node;
      if (ahConstNode->getOpCode().isAdd() || ahConstNode->getOpCode().isSub())
         ahConstNode = ahConstNode->getSecondChild();
      else
         ahConstNode = NULL;
      }
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();

   // Prepare the function table
   TR::Node *tableNode;
   uint8_t *tmpTable = (uint8_t *)comp->trMemory()->allocateStackMemory(65536 * sizeof(uint8_t));
   int32_t count;
   TR::TreeTop *retSameExit = NULL;
   TR_CISCNode *pBoolTable = P->getImportantNode(0);
   TR_CISCNode *tBoolTable = NULL;

   TR_ASSERT(trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isByte() ||
             trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isShort() && trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isUnsigned(), "Error");
   isTRT2Char = trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isShort() && trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isUnsigned();

   if (!isTRT2Char)
      {
      if ((count = trans->analyzeByteBoolTable(pBoolTable, tmpTable, P->getImportantNode(1), &retSameExit)) <= 0)
         {
         bool go = false;
         if ((tBoolTable = trans->getP2TInLoopIfSingle(pBoolTable)) != 0 &&
             (tBoolTable->getOpcode() == TR::ificmpeq))
            {
            retSameExit = tBoolTable->getDestination();
            go = true;
            }
         if (!go)
            {
            if (disptrace) traceMsg(comp, "analyzeByteBoolTable failed.\n");
            return false;  // fail to analyze
            }
         }
      }
   else
      {
      bool supportsSRSTU = comp->cg()->getSupportsSearchCharString();
      if ((count = trans->analyzeCharBoolTable(pBoolTable, tmpTable, P->getImportantNode(1), &retSameExit)) <= 0)
         {
         // Case where we have a single, non-constant delimiter.  With SRSTU, we can handle this situation.
         if (supportsSRSTU &&               // Confirm that the processor has the SRSTU instruction.
             (tBoolTable = trans->getP2TInLoopIfSingle(pBoolTable)) != NULL &&
             (tBoolTable->getOpcode() == TR::ificmpeq))
            {
            retSameExit = tBoolTable->getDestination();
            }
         else
            {
            if (disptrace) traceMsg(comp, "analyzeCharBoolTable failed.\n");
            return false;  // fail to analyze
            }
         }
      else
         {
         if (!supportsSRSTU ||  // If we don't have SRSTU support, we can use SRST/TRT for single byte searches if delimiters are within 1-255.
             count != 1)        // If we only have 1 constant delimiter and have SRSTU support, we can search for any 2-byte delimiter.
            {
            if (disptrace && count > 1)
               traceMsg(comp, "Multiple exit conditions for a char array. We can implement this case using the TRTE instruction on z6.\n");

            if (tmpTable[0])
               {
               traceMsg(comp, "Char array has '0' as an exit condition, loop will not be reduced TRT/SRST (single-byte) instruction.\n");
               return false;    // if zero is a delimiter, give up.
               }
            for (int32_t i = 256; i < 65536; i++)
               {
               if (tmpTable[i])
                  {
                  traceMsg(comp, "Char array has one of 256 through 65535 (%d) as an exit condition, loop cannot be reduced to TRT/SRST (single-byte) instruction.\n", i);
                  return false;    // if any value between 256 and 65535 is a delimiter, give up.
                  }
               }
            }
         }
      }

   if (count != 0 && !retSameExit)    // there is a booltable check and all destinations of booltable are not same
      {
      traceMsg(comp, "Multiple targets for different delimiter checks detected.  Abandoning reduction.\n");
      return false;
      }

   // Check to ensure that the delimiter checks 'break' to the target successor blocks if single successor.
   if (retSameExit != NULL && !isNeedGenIcmpge && retSameExit->getEnclosingBlock() != target)
      {
      traceMsg(comp, "Target for delimiter check (Treetop: %p / Block %d: %p) is different than loop exit block_%d: %p.  Abandoning reduction.\n",
              retSameExit, retSameExit->getEnclosingBlock()->getNumber(), retSameExit->getEnclosingBlock(),
              target->getNumber(), target);
      return false;
      }

   // FIXME: this test is needed because in the TRT2Byte
      // and TRT2 idioms, the aHeader const is not the 4th node
      //
      bool indexRequiresAdjustment = false;
      int32_t ahValue = 0;
      if (ahConstNode && ahConstNode->getOpCode().isLoadConst())
         {
         ahValue = (ahConstNode->getType().isInt64() ?
                              (int32_t)ahConstNode->getLongInt() : ahConstNode->getInt());
         if (ahValue < 0)
            ahValue = -ahValue;

         if (ahValue != TR::Compiler->om.contiguousArrayHeaderSizeInBytes())
            indexRequiresAdjustment = true;
         }
      // We currently don't distinguish between case when starting index is in form of index = index + offset
      // aiadd
      //    aload
      //    lsub       <--- ahConstCISCNode->getHeadOfTrNode()
      //      lmul    <--- indexLoadNode
      //       iload
      //       lconst 2
      //      lconst -10 <--- headerConst
      //
      // vs  index' = index; index++; (See: PR: 82148)
      //
      // istore <--- index++;
      //  isub
      //   iload
      //   iconst -1
      //..
      // aiadd
      //    aload
      //    lsub       <--- ahConstCISCNode->getHeadOfTrNode()
      //      lmul    <--- indexLoadNode
      //       ==>iload <--- index'
      //       lconst 2
      //      lconst -10 <--- headerConst
      //
      // for now disable cases when ahConstNode doesn't equal contiguousArrayHeaderSizeInBytes
      if (indexRequiresAdjustment)
         {
         traceMsg(comp, "headerConst node value doesn't equal contiguous array header size %p. Abandoning reduction.\n", ahConstNode);
         return false;
         }

   if (avoidTransformingStringLoops(comp))
      {
      traceMsg(comp, "Abandoning reduction because of functional problems when String compression is enabled in Java 8 SR5\n");
      return false;
      }

   if (count == -1)           // single delimiter which is not constant value
      {
      TR_CISCNode *tableCISCNode = tBoolTable->getChild(1);
      tableNode = createLoad(tableCISCNode->getHeadOfTrNodeInfo()->_node);
      if (disptrace) traceMsg(comp, "Single non-constant delimiter found.  Setting %p as tableNode.\n", comp->getDebug()->getName(tableCISCNode->getHeadOfTrNodeInfo()->_node));
      }
   else if (count == 1)      // single delimiter
      {
      tableNode = NULL;
      int32_t i = 0;
      for (i = 0; i < 65536; i++)
         {
         if (tmpTable[i])
            {
            tableNode = TR::Node::create( baseRepNode, TR::iconst, 0, i);    // prepare for SRST / SRSTU
            break;
            }
         }
      TR_ASSERT(tableNode, "error!!!");
      if (disptrace) traceMsg(comp, "Single delimiter found.  Setting 'iconst %d' [%p] as tableNode.\n", i, comp->getDebug()->getName(tableNode));
      }
   else
      {
      tableNode = createTableLoad(comp, baseRepNode, 8, 8, tmpTable, disptrace);    // function table for TRT
      }

   // prepare the TR::arraytranslateAndTest node
   TR::Node *findBytesNode = TR::Node::create(trNode, TR::arraytranslateAndTest, 5);
   findBytesNode->setArrayTRT(true);
   TR::Node *baseNode  = createLoad(baseRepNode);

   TR::Node *indexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef);
   if (indexRequiresAdjustment)
      {
      // if refCount > 1, then this means that an old
      // value of the iv is being used in the array index
      //
      if (ahConstCISCNode)
         {
         // aiadd
         //    aload
         //    isub       <--- ahConstCISCNode->getHeadOfTrNode()
         //      index    <--- indexLoadNode
         //      headerConst
         //
         TR::Node *indexParentNode=0;
         int32_t childNum=0;
         if (trans->searchNodeInTrees(ahConstCISCNode->getHeadOfTrNode(), indexNode, &indexParentNode, &childNum))
            {
            TR::Node *indexLoadNode = indexParentNode->getChild(childNum);
            if (indexLoadNode->getOpCode().isLoadVar() &&
                indexLoadNode->getReferenceCount() > 1)
               indexNode = indexLoadNode;
            }
         }

      int32_t width = isTRT2Char ? 2 : 1;
      indexNode = TR::Node::create(TR::isub, 2,
                                  indexNode,
                                  TR::Node::create(indexRepNode, TR::iconst, 0,
                                                  ((TR::Compiler->om.contiguousArrayHeaderSizeInBytes() - ahValue)/width))
                                 );
      }

   TR::Node *alenNode   = TR::Node::create( baseRepNode, TR::arraylength, 1);
   alenNode->setAndIncChild(0, baseNode);
   ////findBytesNode->setSymbolReference(comp->getSymRefTab()->findOrCreateFindBytesSymbol());
   findBytesNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateAndTestSymbol());
   findBytesNode->setAndIncChild(0, baseNode);
   findBytesNode->setAndIncChild(1, createI2LIfNecessary(comp, trans->isGenerateI2L(), indexNode));
   findBytesNode->setAndIncChild(2, tableNode);
   findBytesNode->setAndIncChild(3, createI2LIfNecessary(comp, trans->isGenerateI2L(), alenNode));
   ////findBytesNode->setElementChar(isTRT2Byte);
   findBytesNode->setCharArrayTRT(isTRT2Char);

   TR_CISCNode *icmpgeCISCnode = NULL;
   TrNodeInfo *icmpgeRepInfo = NULL;
   TR::Node *lenRepNode = NULL;

   // There is no ificmpge node.
   if (listT->isEmpty())
      {
      findBytesNode->setNumChildren(4); // we don't need to prepare the fifth parameter "endLen"
      }
   else
      {
      if (disptrace) traceMsg(comp,"Loop has TR::ificmpge for comparing the index.\n");
      TR_CISCNode *lenNode;
      if (listT->isSingleton())
         {
         icmpgeCISCnode = listT->getListHead()->getData();
         lenNode = icmpgeCISCnode->getChild(1);
         }
      else
         {
         ListIterator<TR_CISCNode> li(listT);
         TR_CISCNode *n;
         lenNode = NULL;
         // find icmpge in the candidate region
         for (n = li.getFirst(); n; n = li.getNext())
            {
            if (trans->getCandidateRegion()->isIncluded(n))
               {
               icmpgeCISCnode = n;
               lenNode = n->getChild(1);
               break;
               }
            }
         TR_ASSERT(lenNode != NULL, "error!");
         }
      // set the fifth parameter "endLen"
      icmpgeRepInfo = icmpgeCISCnode->getHeadOfTrNodeInfo();
      lenRepNode = createLoad(lenNode->getHeadOfTrNodeInfo()->_node);
      if (modLength) lenRepNode = createOP2(comp, TR::isub, lenRepNode,
                                            TR::Node::create( baseRepNode, TR::iconst, 0, -modLength));
      findBytesNode->setAndIncChild(4, createI2LIfNecessary(comp, trans->isGenerateI2L(), lenRepNode));
      }
   TR::Node * top = TR::Node::create(TR::treetop, 1, findBytesNode);
   TR::Node * storeToIndVar = TR::Node::createStore(indexVarSymRef, findBytesNode);

   // create Nodes if there are multiple exit points.
   TR::Node *icmpgeNode = NULL;
   TR::TreeTop *failDest = NULL;
   TR::TreeTop *okDest = NULL;
   TR::Block *compensateBlock0 = NULL;
   TR::Block *compensateBlock1 = NULL;
   if (isNeedGenIcmpge)
      {
      if (disptrace) traceMsg(comp,"Now assuming that all exits of booltable are identical and the exit of icmpge points different.\n");
      TR_ASSERT(icmpgeRepInfo, "Not implemented yet"); // current restriction
      okDest = retSameExit;
      failDest = icmpgeCISCnode->getDestination();
      // create two empty blocks for inserting compensation code (base[index] and base[index-1]) prepared by moveStoreOutOfLoopForward()
      if (isCompensateCode)
         {
         compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
         compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest)));
         okDest = compensateBlock0->getEntry();
         failDest = compensateBlock1->getEntry();
         }
      TR_ASSERT(okDest != NULL, "error! okDest == NULL");
      TR_ASSERT(failDest != NULL, "error! failDest == NULL");
      if (disptrace) traceMsg(comp, "Block: okDest=%d failDest=%d\n", okDest->getEnclosingBlock()->getNumber(),
                             failDest->getEnclosingBlock()->getNumber());
      TR_ASSERT(okDest != failDest, "error! okDest == failDest");

      // It actually generates "ificmplt" (NOT ificmpge!) in order to suppress a redundant goto block.
      icmpgeNode = TR::Node::createif(TR::ificmplt,
                                     TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef),
                                     lenRepNode,
                                     okDest);
      }

   // Check existence of nullchk
   // Insert (nullchk), findbytes, and result store instructions
   listT = P2T + P->getImportantNode(2)->getID();
   TR::TreeTop *last;
   TR::TreeTop *nextTreeTop1 = trTreeTop->getNextTreeTop();
   if (nextTreeTop1 == block->getExit())
      {
      nextTreeTop1 = TR::TreeTop::create(comp); // need to create
      }
   if (listT->isEmpty())        // no NULLCHK
      {
      last = trans->removeAllNodes(trTreeTop, block->getExit());
      last->join(block->getExit());
      block = trans->insertBeforeNodes(block);
      last = block->getLastRealTreeTop();
      last->join(trTreeTop);
      trTreeTop->setNode(top);
      trTreeTop->join(nextTreeTop1);
      nextTreeTop1->setNode(storeToIndVar);
      nextTreeTop1->join(block->getExit());
      }
   else
      {
      if (disptrace) traceMsg(comp,"NULLCHK is found!\n");
      // a NULLCHK was found, so just create a NULLCHK on
      // the arraybase
      // NULLCHK
      //   PassThrough
      //      baseNode
      //
      ///TR_CISCNode *nullNode = listT->getListHead()->getData();
      ///TR::Node *nullRepNode = nullNode->getHeadOfTrNodeInfo()->_node;
      TR::Node *dupNullRepNode = baseNode->duplicateTree();
      dupNullRepNode = TR::Node::create(TR::PassThrough, 1, dupNullRepNode);
      dupNullRepNode = TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, dupNullRepNode, comp->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp->getMethodSymbol()));
      TR::TreeTop *nextTreeTop2 = TR::TreeTop::create(comp);
      last = trans->removeAllNodes(trTreeTop, block->getExit());
      last->join(block->getExit());
      block = trans->insertBeforeNodes(block);
      last = block->getLastRealTreeTop();
      last->join(trTreeTop);
      trTreeTop->setNode(dupNullRepNode);
      trTreeTop->join(nextTreeTop1);
      nextTreeTop1->setNode(top);
      nextTreeTop1->join(nextTreeTop2);
      nextTreeTop2->setNode(storeToIndVar);
      nextTreeTop2->join(block->getExit());
      }

   // insert compensation code generated by non-idiom-specific transformation
   block = trans->insertAfterNodes(block);

   if (isNeedGenIcmpge)
      {
      block->append(TR::TreeTop::create(comp, icmpgeNode));
      if (isCompensateCode)
         {
         cfg->setStructure(NULL);
         TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
         TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
         compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);       // ch = base[index]
         compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);       // ch = base[index-1]
         cfg->insertBefore(compensateBlock0, orgNextBlock);
         cfg->insertBefore(compensateBlock1, compensateBlock0);
         cfg->join(block, compensateBlock1);
         }
      }
   else if (isCompensateCode)
      {
      block = trans->insertAfterNodesIdiom(block, 0);       // ch = base[index]
      }

   // set successor edge(s) to the original block
   if (!isNeedGenIcmpge)
      {
      trans->setSuccessorEdge(block, target);
      }
   else
      {
      trans->setSuccessorEdges(block,
                               failDest->getEnclosingBlock(),
                               okDest->getEnclosingBlock());
      }

   return true;
   }


/*************************************************************************************
Corresponding Java-like pseudocode
int i, end;
byte byteArray[ ];
while(true){
   if (booltable(byteArray[i])) break;
   i++;
   if (i >= end) break;	// optional
}

Note 1: The wildcard node "booltable" matches if-statements or switch-case statements
        whose operands consist of the argument of booltable and any constants.
Note 2: "optional" can be excluded in an input program.
*************************************************************************************/
TR_PCISCGraph *
makeTRTGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRT", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *byteArray = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType,  tgt->incNumNodes(), 8, 0, 0, 0);  tgt->addNode(byteArray); // array base
   TR_PCISCNode *iv =        new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 7, 0, 0, 0);  tgt->addNode(iv); // array index
   TR_PCISCNode *end =       new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType,  tgt->incNumNodes(), 6, 0, 0);  tgt->addNode(end); // length (optional)
   TR_PCISCNode *aHeader =   new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,  tgt->incNumNodes(), 5, 0, 0, 0);  tgt->addNode(aHeader);     // array header
   TR_PCISCNode *increment = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,  tgt->incNumNodes(), 4, 0, 0, -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  3,   0,   0);        tgt->addNode(mulFactor);        // Multiply Factor
   TR_PCISCNode *entry =     new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(), 2, 1, 0);        tgt->addNode(entry);
   TR_PCISCNode *nullChk =   new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, entry, byteArray);
   tgt->addNode(nullChk); // optional
   TR_PCISCNode *arrayLen =  new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::arraylength, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, nullChk, byteArray); tgt->addNode(arrayLen);
   TR_PCISCNode *bndChk =    new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 2, arrayLen, arrayLen, iv); tgt->addNode(bndChk);
   TR_PCISCNode *arrayLoad  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, bndChk, TR_ibcload, TR::NoType,  byteArray, iv, aHeader, mulFactor);
   TR_PCISCNode *b2iNode =  new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, arrayLoad, arrayLoad);  tgt->addNode(b2iNode);
   TR_PCISCNode *boolTable = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType,  tgt->incNumNodes(), 1, 2, 1, b2iNode, b2iNode);  tgt->addNode(boolTable);
   TR_PCISCNode *ivStore = createIdiomDecVarInLoop(tgt, ctrl, 1, boolTable, iv, increment);
   TR_PCISCNode *loopTest =  new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(), 1, 2, 2, ivStore, iv, end);
   tgt->addNode(loopTest); // optional
   TR_PCISCNode *exit  =     new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(), 0,  0, 0);        tgt->addNode(exit);

   boolTable->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   end->setIsOptionalNode();
   loopTest->setIsOptionalNode();
   nullChk->setIsOptionalNode();

   b2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, boolTable); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(boolTable, loopTest, nullChk, arrayLoad);
   tgt->setNumDagIds(9);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2FindBytes);
   tgt->setInhibitAfterVersioning();
   tgt->setAspects(isub|bndchk, existAccess, 0);
   tgt->setNoAspects(call|bitop1, 0, existAccess);
   tgt->setMinCounts(1, 1, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, true);
   return tgt;
   }


TR_PCISCGraph *
makeTRTGraph2(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRT2", 0, 16);
   /*******************************************************************     opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(v0); // array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(v1); // array index
   TR_PCISCNode *corv= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 7,   0,   0);  tgt->addNode(corv); // length (optional)
   TR_PCISCNode *alen= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 6,   0,   0);  tgt->addNode(alen); // arraylength (optional)
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  3,   0,   0);        tgt->addNode(mulFactor);        // Multiply Factor
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *nchk= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,   ent, v0); tgt->addNode(nchk); // optional
   TR_PCISCNode *bck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   nchk, alen, v1); tgt->addNode(bck); // optional
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, bck, TR_ibcload, TR::NoType,  v0, v1, cmah, mulFactor);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   n3, n3);  tgt->addNode(n4);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, n4, v1, cm1);
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n6, v1, corv);  tgt->addNode(n7); // optional
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   corv->setIsOptionalNode();
   n7->setIsOptionalNode();
   alen->setIsOptionalNode();
   nchk->setIsOptionalNode();
   bck->setIsOptionalNode();

   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, nchk, n2);
   tgt->setNumDagIds(10);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2FindBytes);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub, existAccess, 0);
   tgt->setNoAspects(call|bitop1, 0, existAccess);
   tgt->setMinCounts(1, 1, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, true);
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like pseudocode
int i, end;
char charArray[ ];	// char array
while(true){
   if (booltable(charArray[i])) break;
   i++;
   if (i >= end) break;	// optional
}

Note 1: There is one limitation. Only when the booltable matches if-statements comparing
        to the constants 1 through 255, the transformation will succeed.
Note 2: Currently, the generated code checks whether the character found by TRT (or SRST)
        is a delimiter.
Note 3: New instructions that directly support a 2-byte array will improve current
        drawbacks described in Notes 1 and 2.
****************************************************************************************/
TR_PCISCGraph *
makeTRT2ByteGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRT2Byte", 0, 16);
   /****************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 9, 0, 0, 0);  tgt->addNode(charArray); // array base
   TR_PCISCNode *iv  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 8, 0, 0, 1);  tgt->addNode(iv); // array index
   TR_PCISCNode *end = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType,  tgt->incNumNodes(), 7, 0, 0);  tgt->addNode(end); // length (optional)
   TR_PCISCNode *arrayLen = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType,  tgt->incNumNodes(), 6, 0, 0);
   tgt->addNode(arrayLen); // arraylength (optional)
   TR_PCISCNode *aHeader = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,  tgt->incNumNodes(), 5, 0, 0, 0);  tgt->addNode(aHeader);     // array header
   TR_PCISCNode *increment = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,  tgt->incNumNodes(), 4, 0, 0, -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);                    // element size
   TR_PCISCNode *entry = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(), 2, 1, 0);        tgt->addNode(entry);
   TR_PCISCNode *nullChk = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, entry, charArray); tgt->addNode(nullChk); // optional
   TR_PCISCNode *bndChk = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 2, nullChk, arrayLen, iv);
   tgt->addNode(bndChk); // optional
   TR_PCISCNode *arrayLoad  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, bndChk, TR::sloadi, TR::Int16,  charArray, iv, aHeader, mulFactor);
   TR_PCISCNode *c2iNode  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,  tgt->incNumNodes(), 1, 1, 1, arrayLoad, arrayLoad);  tgt->addNode(c2iNode);
   TR_PCISCNode *boolTable  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(), 1, 2, 1, c2iNode, c2iNode);  tgt->addNode(boolTable);
   TR_PCISCNode *ivStore  = createIdiomDecVarInLoop(tgt, ctrl, 1, boolTable, iv, increment);
   TR_PCISCNode *loopTest  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(), 1, 2, 2, ivStore, iv, end);
   tgt->addNode(loopTest); // optional
   TR_PCISCNode *exit  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(), 0, 0, 0); tgt->addNode(exit);

   boolTable->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   end->setIsOptionalNode();
   loopTest->setIsOptionalNode();
   arrayLen->setIsOptionalNode();
   nullChk->setIsOptionalNode();
   bndChk->setIsOptionalNode();

   c2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, boolTable); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(boolTable, loopTest, nullChk, arrayLoad);
   tgt->setNumDagIds(10);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2FindBytes);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_2, 0);
   tgt->setNoAspects(call|bitop1, 0, existAccess);
   tgt->setMinCounts(1, 1, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, true);
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//*****************************************************************************************
// IL code generation for exploiting the TRT (or SRST) instruction
// This is the case where the function table is prepared by the user program.
// Input: ImportantNodes(0) - booltable
//        ImportantNodes(1) - ificmpge
//        ImportantNodes(2) - NULLCHK
//*****************************************************************************************
bool
CISCTransform2NestedArrayFindBytes(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   // arraytranslateAndTest is overloaded with a flag
   //
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   int lenForDynamic = trans->isInitializeNegative128By1() ? 128 : 256;

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2NestedArrayFindBytes due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   uint8_t tmpTable[256];
   int count;
   if ((count = trans->analyzeByteBoolTable(P->getImportantNode(0), tmpTable, P->getImportantNode(1))) <= 0)
      return false;
   if (disptrace) dump256Bytes(tmpTable, comp);

   bool isMapDirectlyUsed = isFitTRTFunctionTable(tmpTable);
   bool isGenerateTROO = !isMapDirectlyUsed;

   // Currently, we support only if the map table can be directly used as the function table.
   // Thus, the following code is tentative.
   //
   if (!isMapDirectlyUsed) return false;
   //

   if (avoidTransformingStringLoops(comp))
      {
      traceMsg(comp, "Abandoning reduction because of functional problems when String compression is enabled in Java 8 SR5\n");
      return false;
      }

   TR::Node *baseRepNode, *indexRepNode, *outerBaseRepNode;
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode, &outerBaseRepNode);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * outerBaseVarSymRef = outerBaseRepNode->getSymbolReference();

   uint8_t *tableOuterResult = NULL;
   if (!isMapDirectlyUsed)
      {
      // TODO: To make this work on non-Java environments, the table should be in the code cache, not persistent memory
      tableOuterResult= (uint8_t *)comp->jitPersistentAlloc(256);
      if (trans->isInitializeNegative128By1())
         memset(tableOuterResult+128, 1, 128);
      }

   // Currently, TROO is never generated here. In this case, it returned with the failure above.
   TR::Node * tableNode;
   TR::Node * topOfTranslateNode = NULL;
   if (isGenerateTROO)
      {
      tableNode = createTableLoad(comp, baseRepNode, 8, 8, tmpTable, disptrace);
      //
      // Prepare TR::arraytranslate
      //
      TR::Node * inputNode = createArrayTopAddressTree(comp, trans->isGenerateI2L(), outerBaseRepNode);
      TR::Node * outputNode = TR::Node::aconst(baseRepNode, (uintptr_t)tableOuterResult);
      TR::Node * termCharNode = TR::Node::create( baseRepNode, TR::iconst, 0, 0xff);
      TR::Node * lengthNode = TR::Node::create( baseRepNode, TR::iconst, 0, lenForDynamic);
      TR::Node * stoppingNode = TR::Node::create( baseRepNode, TR::iconst, 0, 0xffffffff);

      TR::Node * translateNode = TR::Node::create(trNode, TR::arraytranslate, 6);
      translateNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateSymbol());
      translateNode->setAndIncChild(0, inputNode);
      translateNode->setAndIncChild(1, outputNode);
      translateNode->setAndIncChild(2, tableNode);
      translateNode->setAndIncChild(3, termCharNode);
      translateNode->setAndIncChild(4, lengthNode);
      translateNode->setAndIncChild(5, stoppingNode);

      translateNode->setSourceIsByteArrayTranslate(true);
      translateNode->setTargetIsByteArrayTranslate(true);
      translateNode->setTermCharNodeIsHint(false);
      translateNode->setSourceCellIsTermChar(false);
      translateNode->setTableBackedByRawStorage(true);
      topOfTranslateNode = TR::Node::create(TR::treetop, 1, translateNode);
      }

   //
   // Prepare TR::arraytranslateAndTest
   //
   TR::Node *findBytesNode = TR::Node::create(trNode, TR::arraytranslateAndTest, 5);
   findBytesNode->setArrayTRT(true);
   TR::Node *baseNode  = createLoad(baseRepNode);
   TR::Node *indexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef);
   TR::Node *alenNode   = TR::Node::create( baseRepNode, TR::arraylength, 1);
   alenNode->setAndIncChild(0, baseNode);
   // Currently, it always uses "isMapDirectlyUsed" version.
   if (isMapDirectlyUsed)
      {
      tableNode = createArrayTopAddressTree(comp, trans->isGenerateI2L(), outerBaseRepNode);
      }
   else
      {
      tableNode = TR::Node::create( baseRepNode, TR::aconst, (uintptr_t)tableOuterResult);
      }
   ////findBytesNode->setSymbolReference(comp->getSymRefTab()->findOrCreateFindBytesSymbol());
   findBytesNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateAndTestSymbol());
   findBytesNode->setAndIncChild(0, baseNode);
   findBytesNode->setAndIncChild(1, createI2LIfNecessary(comp, trans->isGenerateI2L(), indexNode));
   findBytesNode->setAndIncChild(2, tableNode);
   findBytesNode->setAndIncChild(3, createI2LIfNecessary(comp, trans->isGenerateI2L(), alenNode));
   findBytesNode->setCharArrayTRT(false);

   List<TR_CISCNode> *listT = P2T + P->getImportantNode(1)->getID();
   if (listT->isEmpty())
      {
      findBytesNode->setNumChildren(4);
      }
   else
      {
      if (disptrace) traceMsg(comp,"TR::ificmpge for comaring the index is found!\n");
      TR_CISCNode *lenNode;
      TR::Node *lenRepNode;
      if (listT->isSingleton())
         {
         lenNode = listT->getListHead()->getData()->getChild(1);
         }
      else
         {
         ListIterator<TR_CISCNode> li(listT);
         TR_CISCNode *n;
         lenNode = NULL;
         for (n = li.getFirst(); n; n = li.getNext())
            {
            if (trans->getCandidateRegion()->isIncluded(n))
               {
               if (!lenNode)
                  {
                  lenNode = n->getChild(1);
                  }
               }
            }
         TR_ASSERT(lenNode != NULL, "error!");
         }
      lenRepNode = createLoad(lenNode->getHeadOfTrNodeInfo()->_node);
      findBytesNode->setAndIncChild(4, createI2LIfNecessary(comp, trans->isGenerateI2L(), lenRepNode));
      }
   TR::Node * top = TR::Node::create(TR::treetop, 1, findBytesNode);
   TR::Node * storeToIndVar = TR::Node::createStore(indexVarSymRef, findBytesNode);

   // Check existence of nullchk
   // Insert (nullchk), findbytes, and result store instructions
   listT = P2T + P->getImportantNode(2)->getID();
   TR::TreeTop *last;

   if (listT->isEmpty())        // no NULLCHK
      {
      TR::TreeTop *nextTreeTop1 = TR::TreeTop::create(comp);
      last = trans->removeAllNodes(trTreeTop, block->getExit());
      last->join(block->getExit());
      block = trans->insertBeforeNodes(block);
      last = block->getLastRealTreeTop();
      last->join(trTreeTop);
      if (topOfTranslateNode)
         {
         TR::TreeTop *nextTreeTop2 = TR::TreeTop::create(comp);
         trTreeTop->setNode(topOfTranslateNode);
         trTreeTop->join(nextTreeTop1);
         nextTreeTop1->setNode(top);
         nextTreeTop1->join(nextTreeTop2);
         nextTreeTop2->setNode(storeToIndVar);
         nextTreeTop2->join(block->getExit());
         }
      else
         {
         trTreeTop->setNode(top);
         trTreeTop->join(nextTreeTop1);
         nextTreeTop1->setNode(storeToIndVar);
         nextTreeTop1->join(block->getExit());
         }
      }
   else
      {
      if (disptrace) traceMsg(comp,"NULLCHK is found!\n");
      TR::TreeTop *nextTreeTop1 = TR::TreeTop::create(comp);
      TR::TreeTop *nextTreeTop2 = TR::TreeTop::create(comp);
      // a NULLCHK was found, so just create a NULLCHK on
      // the arraybase
      // NULLCHK
      //   PassThrough
      //      baseNode
      //
      ///TR_CISCNode *nullNode = listT->getListHead()->getData();
      ///TR::Node *nullRepNode = nullNode->getHeadOfTrNodeInfo()->_node;
      TR::Node *dupNullRepNode = baseNode->duplicateTree();
      dupNullRepNode = TR::Node::create(TR::PassThrough, 1, dupNullRepNode);
      dupNullRepNode = TR::Node::createWithSymRef(TR::NULLCHK, 1, 1, dupNullRepNode, comp->getSymRefTab()->findOrCreateNullCheckSymbolRef(comp->getMethodSymbol()));

      last = trans->removeAllNodes(trTreeTop, block->getExit());
      last->join(block->getExit());
      block = trans->insertBeforeNodes(block);
      last = block->getLastRealTreeTop();
      last->join(trTreeTop);
      trTreeTop->setNode(dupNullRepNode);
      trTreeTop->join(nextTreeTop1);
      if (topOfTranslateNode)
         {
         TR::TreeTop *nextTreeTop3 = TR::TreeTop::create(comp);
         nextTreeTop1->setNode(topOfTranslateNode);
         nextTreeTop1->join(nextTreeTop2);
         nextTreeTop2->setNode(top);
         nextTreeTop2->join(nextTreeTop3);
         nextTreeTop3->setNode(storeToIndVar);
         nextTreeTop3->join(block->getExit());
         }
      else
         {
         nextTreeTop1->setNode(top);
         nextTreeTop1->join(nextTreeTop2);
         nextTreeTop2->setNode(storeToIndVar);
         nextTreeTop2->join(block->getExit());
         }
      }
   block = trans->insertAfterNodes(block); // insert compensation code generated by non-idiom-specific transformation
   block = trans->insertAfterNodesIdiom(block, 0);       // ch = base[index]

   trans->setSuccessorEdge(block, target);
   return true;
   }



/****************************************************************************************
Corresponding Java-like pseudocode
int i, end;
byte byteArray[ ], map[ ];
while(true){
   if (map[byteArray[i] & 0xff] != 0)) break;
   i++;
   if (i >= end) break;	// optional
}
****************************************************************************************/
TR_PCISCGraph *
makeTRT4NestedArrayGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRT4NestedArray", 0, 16);
   /****************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType,  tgt->incNumNodes(), 9, 0, 0, 0);  tgt->addNode(byteArray); // array base
   TR_PCISCNode *iv  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 8, 0, 0, 0);  tgt->addNode(iv); // array index
   TR_PCISCNode *mapArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 7, 0, 0, 1);  tgt->addNode(mapArray); // outer array base
   TR_PCISCNode *end = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType,  tgt->incNumNodes(), 6, 0, 0);  tgt->addNode(end); // length (optional)
   TR_PCISCNode *aHeader = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,  tgt->incNumNodes(), 5, 0, 0, 0);  tgt->addNode(aHeader);     // array header
   TR_PCISCNode *increment = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR::iconst, TR::Int32,  tgt->incNumNodes(), 4, 0, 0, -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_allconst, TR::NoType, tgt->incNumNodes(),3, 0, 0);  tgt->addNode(mulFactor); // Multiply Factor
   TR_PCISCNode *entry = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(), 2, 1, 0);  tgt->addNode(entry);
   TR_PCISCNode *nullChk = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, entry, byteArray);
   tgt->addNode(nullChk); // optional
   TR_PCISCNode *arrayLen = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::arraylength, TR::NoType,  tgt->incNumNodes(),1,   1,   1,   nullChk, byteArray); tgt->addNode(arrayLen);
   TR_PCISCNode *bndChk = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,  tgt->incNumNodes(), 1, 1, 2, arrayLen, arrayLen, iv); tgt->addNode(bndChk);
   TR_PCISCNode *bALoad  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, bndChk, TR_ibcload, TR::NoType,  byteArray, iv, aHeader, mulFactor);
   TR_PCISCNode *bu2iNode  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, bALoad, bALoad);  tgt->addNode(bu2iNode);
   TR_PCISCNode *mapAload = createIdiomArrayLoadInLoop(tgt, ctrl, 1, bu2iNode, TR_ibcload, TR::NoType,  mapArray, bu2iNode, aHeader, mulFactor);
   TR_PCISCNode *b2iNode = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(), 1, 1, 1, mapAload, mapAload); tgt->addNode(b2iNode);
   TR_PCISCNode *boolTable  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType,  tgt->incNumNodes(), 1, 2, 1, b2iNode, b2iNode); tgt->addNode(boolTable);
   TR_PCISCNode *ivStore  = createIdiomDecVarInLoop(tgt, ctrl, 1, boolTable, iv, increment);
   TR_PCISCNode *loopTest  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(), 1, 2, 2, ivStore, iv, end);
   tgt->addNode(loopTest); // optional
   TR_PCISCNode *exit  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(), 0, 0, 0); tgt->addNode(exit);

   boolTable->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   end->setIsOptionalNode();
   loopTest->setIsOptionalNode();
   nullChk->setIsOptionalNode();
   b2iNode->setIsOptionalNode();

   bu2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, boolTable); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(boolTable, loopTest, nullChk);
   tgt->setNumDagIds(10);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2NestedArrayFindBytes);
   tgt->setInhibitAfterVersioning();
   tgt->setAspects(isub|bndchk, ILTypeProp::Size_1, 0);
   tgt->setNoAspects(call|bitop1, 0, existAccess);
   tgt->setMinCounts(1, 2, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(veryHot, true);
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

bool
CISCTransform2NestedArrayIfFindBytes(TR_CISCTransformer *trans)
   {
   trans->setIsInitializeNegative128By1();
   return CISCTransform2NestedArrayFindBytes(trans);
   }



/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, end;
byte v0[ ], map[ ];
while(true){
   T = v0[v1];
   if (T < 0 || map[T] != 0)) break;
   v1++;
   if (v1 >= end) break;	// optional
}
****************************************************************************************/
TR_PCISCGraph *
makeTRT4NestedArrayIfGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRT4NestedArrayIf", 0, 16);
   /*********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    0);  tgt->addNode(v0); // array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(v1); // array index
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  8,   0,   0,    1);  tgt->addNode(v2); // outer array base
   TR_PCISCNode *corv= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),7,   0,   0);  tgt->addNode(corv); // length (optional)
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm0 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   0);   tgt->addNode(cm0);
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_allconst, TR::NoType, tgt->incNumNodes(),3, 0, 0);  tgt->addNode(mulFactor); // Multiply Factor
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *nchk= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,   ent , v0); tgt->addNode(nchk); // optional
   TR_PCISCNode *alen= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::arraylength, TR::NoType, tgt->incNumNodes(),1,   1,   1,   nchk, v0); tgt->addNode(alen);
   TR_PCISCNode *bck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   alen, alen, v1); tgt->addNode(bck);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1,    bck, TR_ibcload, TR::NoType,  v0, v1, cmah, mulFactor);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::b2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *nif0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmplt, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n3, n3, cm0);  tgt->addNode(nif0);
   TR_PCISCNode *nn2 = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   nif0, TR_ibcload, TR::NoType,  v2, n3, cmah, mulFactor);
   TR_PCISCNode *nn3 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::b2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   nn2, nn2); tgt->addNode(nn3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   nn3, nn3); tgt->addNode(n4);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, n4, v1, cm1);
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n6, v1, corv); tgt->addNode(n7); // optional
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   nif0->setSucc(1, n8);
   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   corv->setIsOptionalNode();
   n7->setIsOptionalNode();
   nchk->setIsOptionalNode();

   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, nchk);
   tgt->setNumDagIds(11);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2NestedArrayIfFindBytes);
   tgt->setInhibitAfterVersioning();
   tgt->setAspects(isub|bndchk, ILTypeProp::Size_1, 0);
   tgt->setNoAspects(call|bitop1, 0, existAccess);
   tgt->setMinCounts(2, 2, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(veryHot, true);
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//*****************************************************************************************
// IL code generation for exploiting the TROT or TROO instruction
// This is the case where the compiler will create the function table by analyzing booltable.
// Input: ImportantNode(0) - booltable
//        ImportantNode(1) - ificmpge
//        ImportantNode(2) - load of the source array
//        ImportantNode(3) - store of the destination array
//        ImportantNode(4) - optional node for optimizing java/lang/String.<init>([BIII)V
//                           We will version the loop by "if (high == 0)".
//*****************************************************************************************
static TR_YesNoMaybe isSignExtendingCopyingTROx(TR_CISCTransformer *trans);

#define TERMCHAR (0xF0FF) // not the sign- or zero-extension of any byte
bool
CISCTransform2CopyingTROx(TR_CISCTransformer *trans)
   {
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool isOutputChar = trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isShort() && trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isUnsigned();
   const char *title = P->getTitle();
   int32_t pattern = P->getPatternType();

   bool genTRxx = comp->cg()->getSupportsArrayTranslateTRxx();
   bool genSIMD = comp->cg()->getSupportsVectorRegisters() && !comp->getOption(TR_DisableSIMDArrayTranslate);

   if (!isOutputChar  && genSIMD && !genTRxx){
	   traceMsg(comp, "Bailing CISCTransform2CopyingTROx : b2b - no proper evaluator available\n");
	   return false;
   }

   bool isSignExtending = false;
   if (isOutputChar)
      {
      TR_YesNoMaybe sx = isSignExtendingCopyingTROx(trans);
      if (sx == TR_maybe)
         {
         traceMsg(comp,
            "Bailing CISCTransform2CopyingTROx : unknown integer conversion\n");
         return false;
         }
      isSignExtending = sx == TR_yes;
      }

   TR_CISCNode *additionHigh = NULL;
   if (P->getImportantNode(4))
      additionHigh = trans->getP2TRepInLoop(P->getImportantNode(4));

   if (additionHigh)
      {
      TR_CISCNode *loadResult = trans->getP2TRepInLoop(P->getImportantNode(0)->getChild(0));
      // Below we need to be able to tell which of the children of iadd is the
      // loaded value, and which is the loop-invariant offset. We do that by
      // requiring that one is obviously the loaded value. If not, give up now.
      if (additionHigh->getChild(0) != loadResult && additionHigh->getChild(1) != loadResult)
         {
         traceMsg(comp,
            "Bailing CISCTransform2CopyingTROx : inscrutable iadd\n");
         return false;
         }
      }

   /*
   while (*title != '\0')
      {
      if (*title == '(')
         {
         pattern = *(++title) - '0';
         break;
         }
      ++title;
      }
   */
   if (disptrace)
      traceMsg(comp, "Found graph pattern as %d\n", pattern);

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2CopyingTROx due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode * inputCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2)->getChild(0));
   TR_CISCNode * outputCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3)->getChild(0));
   TR::Node * inputNode = inputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = outputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();

   TR::Node *baseRepNode, *indexRepNode, *dstBaseRepNode, *dstIndexRepNode, *indexDiffRepNode;
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode, &dstBaseRepNode, &dstIndexRepNode, &indexDiffRepNode);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode ? dstIndexRepNode->getSymbolReference() : NULL;
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0)
      {
      if (disptrace) traceMsg(comp, "countGoodArrayIndex failed for %p\n",indexRepNode);
      return false;
      }
   if (indexVarSymRef == dstIndexVarSymRef)
      {
      dstIndexRepNode = NULL;
      dstIndexVarSymRef = NULL;
      }
   if (dstIndexVarSymRef)
      {
      if (trans->countGoodArrayIndex(dstIndexVarSymRef) == 0)
         {
         if (disptrace) traceMsg(comp, "countGoodArrayIndex failed for %p\n",dstIndexRepNode);
         return false;
         }
      }
   TR_ScratchList<TR::Node> variableList(comp->trMemory());
   variableList.add(indexRepNode);
   if (dstIndexRepNode) variableList.add(dstIndexRepNode);
   if (!isIndexVariableInList(inputNode, &variableList) ||
       !isIndexVariableInList(outputNode, &variableList))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction varaible updates\n", inputNode, outputNode);
      return false;
      }
   TR::SymbolReference * indexDiffVarSymRef = (indexDiffRepNode->getOpCode().isLoadVarOrStore() &&
                                              !indexDiffRepNode->getOpCode().isIndirect()) ?
                                                 indexDiffRepNode->getSymbolReference() : NULL;
   TR::Node *ignoreTree = dstIndexVarSymRef && indexDiffVarSymRef && indexVarSymRef ?
      createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, indexVarSymRef, indexDiffVarSymRef, trNode) : NULL;
   TR::Block *target = trans->analyzeSuccessorBlock(ignoreTree);
   if (!target) // multiple successors
      {
      // current restrictions. allow only the case where the number of successors is 2.
      if (trans->getNumOfBBlistSucc() != 2)
         {
         if (disptrace) traceMsg(comp, "current restrictions. The number of successors is %d\n", trans->getNumOfBBlistSucc());
         return false;
         }
      }

   // Check if there is idiom specific node insertion.
   // Currently, it is inserted by moveStoreOutOfLoopForward() or reorderTargetNodesInBB()
   bool isCompensateCode = !trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1);

   // There is an ificmpge node and (multiple successors or need to generate idiom specific node insertion)
   bool isNeedGenIcmpge = (!target || isCompensateCode);

   // Prepare the function table
   TR::Node *tableNode;
   uint8_t tmpTable[256];

   TR::TreeTop *retSameExit = NULL;

   // Number of Bool Table Test characters:
   //   -1 -> analyzeByteBoolTable error.
   //   0  -> no bool table tests.
   //   >0 -> # of constant test characters.
   int32_t numBoolTableTestChars = trans->analyzeByteBoolTable(P->getImportantNode(0), tmpTable, P->getImportantNode(1), &retSameExit);
   if (numBoolTableTestChars < 0)
      {
      if (disptrace) traceMsg(comp, "analyzeByteBoolTable failed.\n");
      return false;
      }

   if (numBoolTableTestChars != 0 && !retSameExit)    // Destinations of booltable checks are not same
      {
      traceMsg(comp, "Multiple targets for different delimiter checks detected.  Abandoning reduction.\n");
      return false;
      }

   // Check to ensure that the delimiter checks 'break' to the target successor blocks if single successor.
   if (retSameExit != NULL && !isNeedGenIcmpge && retSameExit->getEnclosingBlock() != target)
      {
      traceMsg(comp, "Target for delimiter check (Treetop: %p / Block %d: %p) is different than loop exit block_%d: %p.  Abandoning reduction.\n",
              retSameExit, retSameExit->getEnclosingBlock()->getNumber(), retSameExit->getEnclosingBlock(),
              target->getNumber(), target);
      return false;
      }

   // check if the induction variable needs to be updated by 1
   // this depends on whether the induction variable is incremented
   // before the boolTable exit or after (ie. before the loop driving test)
   //
   TR_CISCNode *boolTableExit = P->getImportantNode(0) ? trans->getP2TRepInLoop(P->getImportantNode(0)) : NULL;
   bool ivNeedsUpdate = false;
   bool dstIvNeedsUpdate = false;
   if (0 && boolTableExit)
      {
      TR::Node *boolTableNode = boolTableExit->getHeadOfTrNodeInfo()->_node;
      traceMsg(comp, "boolTableNode : %p of loop %d\n", boolTableNode, block->getNumber());
      ivNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, indexVarSymRef);
      if (dstIndexVarSymRef)
         dstIvNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, dstIndexVarSymRef);
      }

   int termchar;
   int stopchar = -1;
   if (comp->cg()->getSupportsArrayTranslateTROTNoBreak()||comp->cg()->getSupportsArrayTranslateTROT())
      {
      //for b2s on X (ISO, ASCII) and P(ISO)
      bool foundLoopToReduce = false;
      termchar = 0;  //value of 0, needed by arraytranslateEvaluator to decide between TROT and TROTNoBreak versions.
      if (!isOutputChar)
         {
         traceMsg(comp, "failed because of reason 1 %\n");
         return false;
         }
      if (comp->cg()->getSupportsArrayTranslateTROTNoBreak())
         {
         foundLoopToReduce = true;
         for (int i = 0; i < 256; i++)
            {
            if (tmpTable[i] != 0)
               foundLoopToReduce = false;
            }

         if (foundLoopToReduce)
            termchar = TERMCHAR; //It needs to be greater than zero, dummy termination char otherwise, i.e., it's not gonna be used,
        }
      if (!foundLoopToReduce && comp->cg()->getSupportsArrayTranslateTROT())      //try ascii
         {
         foundLoopToReduce = true;
         for (int i = 0; i < 256; i++)
            {
               bool excluded = tmpTable[i] != 0;
               bool nonASCII = i >= 128;
               if (excluded != nonASCII)
                  foundLoopToReduce = false;
            }

         if (foundLoopToReduce)
            termchar = 0; //to distinguish between ISO and ASCII when evaluating the node.
         }
      //
      if (!foundLoopToReduce)
         {
         traceMsg(comp, "failed because of reason 2\n");
         return false;
         }
      tableNode = TR::Node::create(baseRepNode, TR::iconst, 0, 0); //dummy table node, it's not gonna be used

      if (termchar != 0)
         {
         // This is ISO 8859-1. The decode helpers accept all input bytes, and
         // they zero-extend each byte into a char. While that's the right way
         // to decode ISO 8859-1, it may not be what the loop asks us to do.
         if (isSignExtending)
            {
            traceMsg(comp,
               "Bailing CISCTransform2CopyingTROx due to sign-extension\n");
            return false;
            }
         }
      }
   else
      {
	   //SIMD or TRxx
      if (isOutputChar)
         {
    	  //b2c
         termchar = TERMCHAR;
         uint16_t table[256];

         bool isSIMDPossible = genSIMD && !isSignExtending;
         if (isSIMDPossible) {
            //SIMD possible only if we have consecutive chars, and no ranges
       	    for (int i = 0; i < 256; i++) {
       		   if (tmpTable[i] == 0) {
                  if (stopchar != (i-1)) {
                     isSIMDPossible = false;
       				 break;
       			  }
       			  stopchar++;
       		    }
       	    }

       	    //case all are non-valid chars
       	    if (stopchar == -1 )
       		   isSIMDPossible = false;
         }

       	 if (isSIMDPossible) {
       		tableNode = TR::Node::create(baseRepNode, TR::aconst, 0, 0); //dummy table node, it's not gonna be used
       	 } else if (!genTRxx){
             traceMsg(comp, "Bailing CISCTransform2CopyingTROx: b2c - no proper evaluator available\n");
             return false;
       	 } else {
       		 for (int i = 0; i < 256; i++)
       		 	 {
       			 uint8_t excluded = tmpTable[i];
       			 uint16_t *entry = &table[i];
       			 if (excluded)
       			    *entry = TERMCHAR;
       			 else if (isSignExtending)
       			    *entry = (int8_t)i; // sign-extends up from 8-bit
       			 else
       			    *entry = i;
       		 	 }
       		 tableNode = createTableLoad(comp, baseRepNode, 8, 16, table, disptrace);
         	 }
         }
      else
         {
    	  //b2b
         termchar = -1;
         for (int i = 0; i < 256; i++)
            {
            uint8_t u8 = tmpTable[i];
            if (u8)
               {
                  if (termchar < 0) termchar = i;
                  tmpTable[i] = termchar;
               }
            else
               {
               tmpTable[i] = i;
               }
            }
         if (termchar < 0)
            {
            traceMsg(comp, "No terminating character found. Abandoning reduction.\n");
            return false;
            }
         tableNode = createTableLoad(comp, baseRepNode, 8, 8, tmpTable, disptrace);
         }
      }

   // find the target node of icmpge
   TR_CISCNode *icmpgeCISCnode = NULL;
   TrNodeInfo *icmpgeRepInfo = NULL;
   TR::Node *lenRepNode = NULL;
   List<TR_CISCNode> *listT = P2T + P->getImportantNode(1)->getID(); // ificmpge
   TR_CISCNode *lenNode;
   if (listT->isSingleton())
      {
      icmpgeCISCnode = listT->getListHead()->getData();
      lenNode = icmpgeCISCnode->getChild(1);
      }
   else
      {
      ListIterator<TR_CISCNode> li(listT);
      TR_CISCNode *n;
      lenNode = NULL;
      // find icmpge in the candidate region
      for (n = li.getFirst(); n; n = li.getNext())
         {
         if (trans->getCandidateRegion()->isIncluded(n))
            {
            if (icmpgeCISCnode != NULL)
               {
               if (disptrace)
                  traceMsg(comp, "Bailing CISCTransform2CopyingTROx: multiple loop tests: %d and %d\n", icmpgeCISCnode->getID(), n->getID());
               return false;
               }
            icmpgeCISCnode = n;
            lenNode = n->getChild(1);
            }
         }
      TR_ASSERT(lenNode != NULL, "error!");
      }
   bool isDecrement;
   int32_t modLength;
   if (!testExitIF(icmpgeCISCnode->getOpcode(), &isDecrement, &modLength)) return false;
   if (isDecrement) return false;
   TR_ASSERT(modLength == 0 || modLength == 1, "error");
   icmpgeRepInfo = icmpgeCISCnode->getHeadOfTrNodeInfo();
   lenRepNode = createLoad(lenNode->getHeadOfTrNodeInfo()->_node);

   // Modify array header constant if necessary
   TR::Node *constLoad;
   if (trans->getOffsetOperand1())
      {
      constLoad = modifyArrayHeaderConst(comp, inputNode, trans->getOffsetOperand1());
      TR_ASSERT(constLoad, "Not implemented yet");
      if (disptrace) traceMsg(comp,"The array header const of inputNode %p is modified. (offset=%d)\n", inputNode, trans->getOffsetOperand1());
      }
   if (trans->getOffsetOperand2())
      {
      int32_t offset = trans->getOffsetOperand2() * (isOutputChar ? 2 : 1);
      constLoad = modifyArrayHeaderConst(comp, outputNode, offset);
      TR_ASSERT(constLoad, "Not implemented yet");
      if (disptrace) traceMsg(comp,"The array header const of outputNode %p is modified. (offset=%d)\n", outputNode, offset);
      }

   // Prepare the arraytranslate node
   TR::Node * indexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef);
   TR::Node * lenTmpNode = createOP2(comp, TR::isub, lenRepNode, indexNode);
   if (modLength) lenTmpNode = createOP2(comp, TR::isub, lenTmpNode, TR::Node::create(indexRepNode, TR::iconst, 0, -modLength));
   TR::Node * lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lenTmpNode);
   TR::Node * termCharNode = TR::Node::create( baseRepNode, TR::iconst, 0, termchar);
   TR::Node * stopCharNode = TR::Node::create( baseRepNode, TR::iconst, 0, stopchar);

   TR::Node * translateNode = TR::Node::create(trNode, TR::arraytranslate, 6);
   translateNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateSymbol());
   translateNode->setAndIncChild(0, inputNode);
   translateNode->setAndIncChild(1, outputNode);
   translateNode->setAndIncChild(2, tableNode);
   translateNode->setAndIncChild(3, termCharNode);
   translateNode->setAndIncChild(4, lengthNode);
   translateNode->setAndIncChild(5, stopCharNode);

   translateNode->setSourceIsByteArrayTranslate(true);
   translateNode->setTargetIsByteArrayTranslate(!isOutputChar);
   translateNode->setTermCharNodeIsHint(false);
   translateNode->setSourceCellIsTermChar(false);
   translateNode->setTableBackedByRawStorage(true);
   TR::SymbolReference * translateTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
   TR::Node * topOfTranslateNode = TR::Node::createStore(translateTemp, translateNode);

   // prepare nodes that add the number of elements (which was translated) into the induction variables

   TR::Node * addCountNode = createOP2(comp, TR::iadd, indexNode->duplicateTree(), translateNode);
   if (ivNeedsUpdate)
      addCountNode = TR::Node::create(TR::iadd, 2, addCountNode, TR::Node::iconst(indexNode, 1));

   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, addCountNode);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);

   TR::TreeTop * dstIndVarUpdateTreeTop = NULL;
   // update the derived induction variable accordingly as well
   //
   if (dstIndexRepNode)
      {
      // find the store corresponding to the derived induction variable
      //
      TR_CISCNode *loopTest = P->getImportantNode(1);
      ListIterator<TR_CISCNode> ni(P->getNodes());
      TR_CISCNode *jstore = NULL;
      TR::Node *dstIVStore = NULL;
      for (TR_CISCNode *n = ni.getFirst(); n; n = ni.getNext())
         {
         if (n->getNumSuccs() >= 1 &&
               n->getSucc(0) &&
               (n->getSucc(0)->getID() == loopTest->getID()))
            {
            jstore = n;
            break;
            }

         }
      if (jstore)
         {
         ///traceMsg(comp, "found jstore %p to be %d\n", jstore, jstore->getID());
         TR_CISCNode *matchJ = trans->getP2TRepInLoop(jstore);
         if (matchJ)
            {
            ///traceMsg(comp, "found matching jstore %p to be %d\n", matchJ, matchJ->getID());
            ///traceMsg(comp, "actual store node is %p\n", matchJ->getHeadOfTrNodeInfo()->_node);
            dstIVStore = matchJ->getHeadOfTrNodeInfo()->_node;
            }
         }

      if (dstIVStore &&
            dstIVStore->getOpCode().hasSymbolReference() &&
            dstIVStore->getSymbolReference() == dstIndexVarSymRef)
         {
         // j = j + 1 (pattern=1)
         // final value j_final = j_start + arraytranslate + needsUpdate ? 1 : 0
         // or
         // j = i + offset (pattern=0)
         // final value j_final = i_final + offset (i_final has already been emitted in the previous TT)
         //
         dstIVStore = dstIVStore->duplicateTree();
         TR::Node * dstIndVarUpdateNode = NULL;
         if (pattern == 1)
            {
            TR::Node *dstAddCountNode = createOP2(comp, TR::iadd,
                                                TR::Node::createLoad(dstIndexRepNode, dstIndexVarSymRef),
                                                translateNode);
            if (dstIvNeedsUpdate)
               dstAddCountNode = TR::Node::create(TR::iadd, 2,
                                                   dstAddCountNode,
                                                   TR::Node::iconst(dstAddCountNode, 1));


            dstIndVarUpdateNode = TR::Node::createStore(dstIndexVarSymRef, dstAddCountNode);
            }
         else if (pattern == 0)
            {
            TR::Node *firstChild = dstIVStore->getFirstChild();
            if (firstChild->getOpCode().isAdd() || firstChild->getOpCode().isSub())
               {
               TR::Node *ivLoad = firstChild->getFirstChild();
               if (!ivLoad->getOpCode().hasSymbolReference() ||
                     (ivLoad->getSymbolReference() != indexVarSymRef))
                  {
                  ivLoad->recursivelyDecReferenceCount();
                  firstChild->setAndIncChild(0, TR::Node::createLoad(indexRepNode, indexVarSymRef));
                  }
               }
            dstIndVarUpdateNode = dstIVStore;
            }
         if (dstIndVarUpdateNode)
            dstIndVarUpdateTreeTop = TR::TreeTop::create(comp, dstIndVarUpdateNode);
         }
      }

   // create Nodes if there are multiple exit points.
   TR::Node *icmpgeNode = NULL;
   TR::TreeTop *failDest = NULL;
   TR::TreeTop *okDest = NULL;
   TR::Block *compensateBlock0 = NULL;
   TR::Block *compensateBlock1 = NULL;
   if (isNeedGenIcmpge)
      {
      if (disptrace) traceMsg(comp, "Now assuming that all exits of booltable are identical and the exit of icmpge points different.\n");

      TR_ASSERT(icmpgeRepInfo, "Not implemented yet"); // current restriction
      okDest = retSameExit;
      failDest = icmpgeCISCnode->getDestination();
      // create two empty blocks for inserting compensation code (base[index] and base[index-1]) prepared by moveStoreOutOfLoopForward()
      if (isCompensateCode)
         {
         compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
         compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest)));
         okDest = compensateBlock0->getEntry();
         failDest = compensateBlock1->getEntry();
         }
      TR_ASSERT(okDest != NULL && failDest != NULL && okDest != failDest, "error!");

      // It actually generates "ificmplt" (NOT ificmpge!) in order to suppress a redundant goto block.
      icmpgeNode = TR::Node::createif(TR::ificmplt,
                                     TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef),
                                     lenRepNode,
                                     okDest);
      }

   // Insert nodes and maintain the CFG
   if (additionHigh)
      {
      TR_CISCNode *highCISCNode;
      TR_CISCNode *loadResult = trans->getP2TRepInLoop(P->getImportantNode(0)->getChild(0));
      // Guaranteed above
      TR_ASSERT(additionHigh->getChild(0) == loadResult || additionHigh->getChild(1) == loadResult, "error!");
      highCISCNode = (additionHigh->getChild(0) == loadResult) ? additionHigh->getChild(1) :
                                                                 additionHigh->getChild(0);
      List<TR::Node> guardList(comp->trMemory());
      guardList.add(TR::Node::createif(TR::ificmpne, convertStoreToLoad(comp, highCISCNode->getHeadOfTrNodeInfo()->_node),
                                      TR::Node::create(lengthNode, TR::iconst, 0, 0)));
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lenTmpNode->duplicateTree(), &guardList);
      }
   else
      {
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lenTmpNode->duplicateTree());
      }

   // Create the fast path code
   block = trans->insertBeforeNodes(block);
   block->append(TR::TreeTop::create(comp, topOfTranslateNode));

   block->append(indVarUpdateTreeTop);
   //block->append(indVarIncTreeTop);
   if (dstIndVarUpdateTreeTop) block->append(dstIndVarUpdateTreeTop);
   block = trans->insertAfterNodes(block);

   if (isNeedGenIcmpge)
      {
      block->append(TR::TreeTop::create(comp, icmpgeNode));
      if (isCompensateCode)
         {
         TR::CFG *cfg = comp->getFlowGraph();
         cfg->setStructure(NULL);
         TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
         TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
         compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);       // ch = base[index]
         compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);       // ch = base[index-1]
         cfg->insertBefore(compensateBlock0, orgNextBlock);
         cfg->insertBefore(compensateBlock1, compensateBlock0);
         cfg->join(block, compensateBlock1);
         }
      }
   else if (isCompensateCode)
      {
      block = trans->insertAfterNodesIdiom(block, 0);       // ch = base[index]
      }

   // set successor edge(s) to the original block
   if (!isNeedGenIcmpge)
      {
      trans->setSuccessorEdge(block, target);
      }
   else
      {
      trans->setSuccessorEdges(block,
                               failDest->getEnclosingBlock(),
                               okDest->getEnclosingBlock());
      }

   return true;
   }

/**
 * Determine whether the 16-bit output values are sign- or zero-extended.
 *
 * Loops transformed by CISCTransform2CopyingTROx(TR_CISCTransformer*) are
 * loops that copy values from an input <tt>byte[]</tt> to an output
 * <tt>byte[]</tt> or <tt>char[]</tt>, When the output goes into a
 * <tt>char[]</tt>, the output values are never identical to the input values,
 * because they are wider. So each is the result of some integer conversion,
 * effectively like this:
 *
   \verbatim
      dest[j] = convert(src[i])
   \endverbatim
 *
 * In order to correctly transform the loop, it's important to know the
 * conversion operation. This function analyzes the loop to determine whether
 * the conversion is known to be a sign-extension (\c TR_yes), known to be a
 * zero-extension (\c TR_no), or neither (\c TR_maybe).
 *
 * Note that a result of \c TR_maybe necessarily prevents the transformation
 * from succeeding. A result of \c TR_no allows the transformation to proceed,
 * since zero-extension was previously the tacit assumption. For contrast, an
 * effort is made to transform sign-extending loops (\c TR_yes), but doing so
 * is not always possible, even in cases where the corresponding zero-extending
 * loop can be transformed.
 *
 * \param[in] trans The optimization pass object.
 * \return \c TR_yes for sign-extension, \c TR_no for zero-extension, or \c
 * TR_maybe for unknown/neither.
 */
static TR_YesNoMaybe
isSignExtendingCopyingTROx(TR_CISCTransformer *trans)
   {
   TR_CISCGraph *P = trans->getP();
   TR::Compilation *comp = trans->comp();

   TR_CISCNode *patArrStore = P->getImportantNode(3);
   TR_CISCNode *patStoreConv = patArrStore->getChild(1);
   TR_ASSERT(
      patStoreConv->getOpcode() == TR_conversion
      || patStoreConv->getIlOpCode().isConversion(),
      "isSignExtendingCopyingTROx: pattern store conversion not found\n");

   TR_CISCNode *patLoadConv = patStoreConv->getChild(0);
   // In CopyingTROx(*), the child is an optional iadd, but not in
   // CopyingTROTInduction1 or CopyingTROOSpecial.
   if (patLoadConv->getOpcode() == TR::iadd)
      patLoadConv = patLoadConv->getChild(0);

   TR_ASSERT(
      patLoadConv->getOpcode() == TR_conversion
      || patLoadConv->getIlOpCode().isConversion(),
      "isSignExtendingCopyingTROx: pattern load conversion not found\n");

   TR_CISCNode *tgtStoreConv = trans->getP2TRepInLoop(patStoreConv);
   TR_CISCNode *tgtLoadConv = trans->getP2TRepInLoop(patLoadConv);
   TR_ASSERT(
      tgtStoreConv != NULL || tgtLoadConv != NULL,
      "isSignExtendingCopyingTROx: converted from byte to char without "
      "any conversions\n");

   TR::Node *storeConv = NULL;
   if (tgtStoreConv != NULL)
      storeConv = tgtStoreConv->getHeadOfTrNodeInfo()->_node;

   TR::Node *loadConv = NULL;
   if (tgtLoadConv != NULL)
      loadConv = tgtLoadConv->getHeadOfTrNodeInfo()->_node;

   if (storeConv == NULL || loadConv == NULL) // only one conversion
      {
      TR::Node *loneConv = loadConv != NULL ? loadConv : storeConv;
      TR::ILOpCode op = loneConv->getOpCode();
      TR_ASSERT(
         op.isZeroExtension() || op.isSignExtension(),
         "isSignExtendingCopyingTROx: lone conversion not an extension\n");
      return op.isSignExtension() ? TR_yes : TR_no;
      }

   // Two conversions.
   TR::ILOpCode firstOp = loadConv->getOpCode();
   if (!firstOp.isInteger() && !firstOp.isUnsigned())
      {
      traceMsg(comp,
         "isSignExtendingCopyingTROx: conversion through non-integer type\n");
      return TR_maybe;
      }

   // The first conversion has to be a (zero- or sign-) extension, because Int8
   // is the smallest available integer type.
   TR_ASSERT(
      firstOp.isZeroExtension() || firstOp.isSignExtension(),
      "isSignExtendingCopyingTROx: first conversion not an extension\n");

   // If it produces a 16-bit integer directly, the second would have to be a
   // "conversion" from short to short.
   TR_ASSERT(
      !firstOp.isShort(),
      "isSignExtendingCopyingTROx: first conversion directly to short\n");

   // So the intermediate type is an integer type longer than 16-bit, and the
   // second conversion has to be a truncation to 16 bits. The net effect is
   // either a zero- or sign-extension depending only on the first conversion.
   return firstOp.isSignExtension() ? TR_yes : TR_no;
   }

bool
CISCTransform2CopyingTROxAddDest1(TR_CISCTransformer *trans)
   {
   trans->setOffsetOperand2(1);        // add offset of destination with 1
   return CISCTransform2CopyingTROx(trans);
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v3, end;
byte v0[ ];
byte v2[ ];
while(true){
   if (booltable(v0[v1])) break;
   v2[v3] = v0[v1];
   v1++;
   v3++;
   if (v1 >= end) break;
}

Note 1: It allows that variables v1 and v3 are identical.
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTROOSpecialGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CopyingTROOSpecial", 0, 16);
   /**********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 9,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 8,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),7,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *lc1 = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 1);                    // element size for input
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_allconst, TR::NoType, tgt->incNumNodes(),3, 0, 0);  tgt->addNode(mulFactor); // Multiply Factor
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   ent, TR::bloadi, TR::Int8,  v0, idx0, cmah, lc1);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   n3, n3);  tgt->addNode(n4);  // optional
   TR_PCISCNode *n5  = createIdiomArrayLoadInLoop(tgt, ctrl, 1,  n4, TR::bloadi, TR::Int8,  v0, idx0, cmah, mulFactor);
   TR_PCISCNode *nn0 = createIdiomArrayStoreInLoop(tgt, ctrl|CISCUtilCtl_NoConversion, 1, n5, TR::bstorei, TR::Int8,  v2, idx1, cmah, mulFactor, n5);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, nn0, v1, cm1);
   TR_PCISCNode *nn1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n6, v3, cm1); tgt->addNode(nn1);
   TR_PCISCNode *nn2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2s, TR::Int16,       tgt->incNumNodes(),  1,   1,   1,  nn1, nn1); tgt->addNode(nn2);  // optional
   TR_PCISCNode *nn3 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,  nn2, nn2); tgt->addNode(nn3);  // optional
   TR_PCISCNode *nn6 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore, TR::Int32,    tgt->incNumNodes(),  1,   1,   2,  nn3, nn3, v3); tgt->addNode(nn6);
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   nn6, v1, vorc);  tgt->addNode(n7);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   n4->setIsOptionalNode();
   nn2->setIsOptionalNode();
   nn3->setIsOptionalNode();

   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, n2, nn0, NULL);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTROx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|sameTypeLoadStore, ILTypeProp::Size_1, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 2, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTROOSpecialGraph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 19);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   tgt->setPatternType(1); // dest. induction variable is updated by incrementing
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like pseudocode

int i, j, end;
byte byteArray[ ];
char charArray[ ];
while(true){
   char T = (char)byteArray[i];
   if (booltable(T)) break;
   (T = T + high;) // optional
   charArray[j] = T;
   i++;
   j++;
   if (i >= end) break;
}

Note 1: Idiom allows variables i and j to be identical.
Note 2: The optional addition "T = T + high" is to optimize java/lang/String.<init>([BIII)V.
        We will version the loop by "if (high == 0)".
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTROxGraph(TR::Compilation *c, int32_t ctrl, int pattern)
   {
   TR_ASSERT(pattern == 0 || pattern == 1, "not implemented");
   char *name = (char *)TR_MemoryBase::jitPersistentAlloc(16);
   sprintf(name, "CopyingTROx(%d)",pattern);
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), name, 0, 16);
   /****************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType,  tgt->incNumNodes(),16,   0,   0,    0);
   tgt->addNode(byteArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(i); // src array index
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType,  tgt->incNumNodes(),14,   0,   0,    1);  tgt->addNode(charArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *idx0       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),12,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_quasiConst2, TR::NoType, tgt->incNumNodes(),10,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *high       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),9,   0,   0);  tgt->addNode(high);      // optional
   TR_PCISCNode *aHeader0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(aHeader0);     // array header
   TR_PCISCNode *aHeader1   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(aHeader1);     // array header
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *lc1        = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 1);                    // element size for input
   TR_PCISCNode *elemSize   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_allconst, TR::NoType, tgt->incNumNodes(),    4, 0, 0);  tgt->addNode(elemSize); // Multiply Factor
   TR_PCISCNode *offset = NULL;
   if (pattern == 0)
      {
      offset = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),3,   0,   0);  tgt->addNode(offset);      // optional
      }
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(entry);
   TR_PCISCNode *byteAddr   = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   entry, TR::bloadi, TR::Int8,  byteArray, idx0, aHeader0, lc1);
   TR_PCISCNode *b2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   byteAddr, byteAddr);
   tgt->addNode(b2iNode);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   b2iNode, b2iNode);
   tgt->addNode(exitTest);  // optional
   TR_PCISCNode *add        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iadd, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   exitTest, b2iNode, high);  tgt->addNode(add);  // optional
   TR_PCISCNode *charAddr   = createIdiomArrayStoreInLoop(tgt, ctrl, 1, add, TR_ibcstore, TR::NoType,  charArray, idx1, aHeader1, elemSize, add);
   TR_PCISCNode *iStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, charAddr, i, increment);
   TR_PCISCNode *jStore = NULL;
   switch(pattern)
      {
      case 0:
         jStore     = createIdiomIncVarInLoop(tgt, ctrl, 1, iStore, j, i, offset);      // j = i + offset; (optional)
         break;
      case 1:
         jStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, iStore, j, increment);      // j = j + 1;      (optional)
         break;
      default:
         TR_ASSERT(0, "not implemented!");
         return NULL;
      }
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   jStore, i, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   jStore->getChild(0)->setIsOptionalNode();
   jStore->setIsOptionalNode();
   j->setIsOptionalNode();

   exitTest->setIsOptionalNode();
   add->setIsOptionalNode();
   high->setIsOptionalNode();
   if (offset) offset->setIsOptionalNode();

   b2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, exitTest); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, byteAddr, charAddr, add);
   tgt->setNumDagIds(17);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTROx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_1, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTROxGraph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 8);
   tgt->setVersionLength(versionLength);   // depending on each architecture

   tgt->setPatternType(pattern);

   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, end;
int v3;			// optional
int v4;	// v4 usually has the value of "v3 - v1".
byte v0[ ];
char v2[ ];
while(true){
   char T = (char)v0[v1];
   if (booltable(T)) break;
   v2[v1+v4] = T;
   v1++;
   v3 = v1+v4;		// optional
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTROTInduction1Graph(TR::Compilation *c, int32_t ctrl, int32_t pattern)
   {
   TR_ASSERT(pattern == 0 || pattern == 1, "not implemented");
   char *name = (char *)TR_MemoryBase::jitPersistentAlloc(26);
   sprintf(name, "CopyingTROTInduction1(%d)",pattern);
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), name, 0, 16);
   /*********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v3); // actual dst array index (optional)
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    2);  tgt->addNode(v4); // difference of dst array index from src array index
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 1);                    // element size
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   ent, TR::bloadi, TR::Int8,  v0, v1, cmah0, c1);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   n3, n3);  tgt->addNode(n4);  // optional
   TR_PCISCNode *n45 = (pattern == 1) ? createIdiomDecVarInLoop(tgt, ctrl, 1, n4, v1, cm1) : n4;
   TR_PCISCNode *n5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iadd, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n45, v1, v4);  tgt->addNode(n5);
   TR_PCISCNode *nn0 = createIdiomCharArrayStoreInLoop(tgt, ctrl, 1, n5, v2, n5, cmah1, c2, n3);
   TR_PCISCNode *n6  = (pattern == 0) ? createIdiomDecVarInLoop(tgt, ctrl, 1, nn0, v1, cm1) : nn0;
   TR_PCISCNode *op0 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n6, n5, cm1);  tgt->addNode(op0);  // (optional)
   TR_PCISCNode *op1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   op0,op0, v3); tgt->addNode(op1);  // (optional)
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   op1, v1, vorc);  tgt->addNode(n7);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   n4->setIsOptionalNode();
   v3->setIsOptionalNode();
   op0->setIsOptionalNode();
   op1->setIsOptionalNode();

   op1->setIsChildDirectlyConnected();
   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, n2, nn0, NULL);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(pattern == 0 ? CISCTransform2CopyingTROx : CISCTransform2CopyingTROxAddDest1);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_1, ILTypeProp::Size_2);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTROTInduction1Graph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 8);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//*****************************************************************************************
// IL code generation for exploiting the TROT instruction
// This is the case where the function table is prepared by the user program.
// Input: ImportantNodes(0) - booltable
//        ImportantNodes(1) - ificmpge
//        ImportantNodes(2) - address of the source array
//        ImportantNodes(3) - address of the destination array
//*****************************************************************************************
#define TERMBYTE (0x0B) // Vertical Tab is rarely used, I guess...
bool
CISCTransform2TROTArray(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2TROTArray due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode * inputCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(2));
   TR_CISCNode * outputCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
   if (!inputCISCNode || !outputCISCNode) return false;
   TR::Node * inputNode = inputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = outputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();

   TR::Node *baseRepNode, *indexRepNode, *dstBaseRepNode, *dstIndexRepNode, *mapBaseRepNode;
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode, &dstBaseRepNode, &dstIndexRepNode, &mapBaseRepNode);
   TR::Node *cmpRepNode = trans->getP2TRep(P->getImportantNode(1))->getHeadOfTrNodeInfo()->_node;
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode ? dstIndexRepNode->getSymbolReference() : NULL;
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0) return false;
   if (dstIndexVarSymRef == indexVarSymRef)
      {
      dstIndexRepNode = NULL;
      dstIndexVarSymRef = NULL;
      }
   if (dstIndexVarSymRef)
      {
      if (trans->countGoodArrayIndex(dstIndexVarSymRef) == 0) return false;
      }
   TR_ScratchList<TR::Node> variableList(comp->trMemory());
   variableList.add(indexRepNode);
   if (dstIndexRepNode) variableList.add(dstIndexRepNode);
   if (!isIndexVariableInList(inputNode, &variableList) ||
       !isIndexVariableInList(outputNode, &variableList))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction varaible updates\n", inputNode, outputNode);
      return false;
      }
   TR::Block *target = trans->analyzeSuccessorBlock();

   // Prepare arraytranslate node
   TR::Node * tableNode = createLoad(mapBaseRepNode);
   TR::Node * indexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef);
   TR::Node * lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(),
                                               createOP2(comp, TR::isub, cmpRepNode->getChild(1)->duplicateTree(),
                                                         indexNode));
   TR_CISCNode *ifeqCiscNode = trans->getP2TRep(P->getImportantNode(0));
   TR::Node * termCharNode;
   if (ifeqCiscNode)
      termCharNode = createLoad(ifeqCiscNode->getHeadOfTrNode()->getChild(1));
   else
      termCharNode = TR::Node::create(inputNode, TR::iconst, 0, TERMBYTE);
   TR::Node * stoppingNode = TR::Node::create( baseRepNode, TR::iconst, 0, 0xffffffff);


   TR::Node * translateNode = TR::Node::create(trNode, TR::arraytranslate, 6);
   translateNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateSymbol());
   translateNode->setAndIncChild(0, inputNode);
   translateNode->setAndIncChild(1, outputNode);
   translateNode->setAndIncChild(2, tableNode);
   translateNode->setAndIncChild(3, termCharNode);
   translateNode->setAndIncChild(4, lengthNode);
   translateNode->setAndIncChild(5, stoppingNode);

   translateNode->setSourceIsByteArrayTranslate(true);
   translateNode->setTargetIsByteArrayTranslate(false);
   translateNode->setTermCharNodeIsHint(ifeqCiscNode ? false : true);
   translateNode->setSourceCellIsTermChar(false);
   translateNode->setTableBackedByRawStorage(false);
   TR::Node * topOfTranslateNode = TR::Node::create(TR::treetop, 1, translateNode);
   TR::Node * lengthTRxx = translateNode;

   if (target)
      {
      // prepare nodes that add the number of elements (which was translated) into the induction variables

      /*lengthTRxx = createOP2(comp, TR::isub,
                             translateNode,
                             TR::Node::create(translateNode, TR::iconst, 0, -1)); */
      }
   else
      {
      // For Multiple Successor Blocks, we have a test character condition in the
      // loop, which may lead to a different successor block than the fallthrough.
      // We need to be able to distinguish the following two scenarios, which both
      // would load the last character in the source array:
      //   1.  no test character found (translateNode == lengthNode).
      //   2.  test character found in the last element(translateNode < lengthNode).
      // The final IV value is always (IV + translateNode).
      // However, under case 1,  the element loaded is at index (IV + translateNode - 1).
      // Under case 2, the element loaded is at index (IV + translateNode).
      // As such, we will subtract 1 in the existing final IV calculation for case 1,
      // so that any array accesses will be correctly indexed.  The final IV value will
      // be increased by 1 again before we hit the exit test.
      lengthTRxx = TR::Node::create(TR::isub, 2, translateNode,
                                   TR::Node::create(TR::icmpeq, 2, translateNode,
                                                   lengthNode->getOpCodeValue() == TR::i2l ? lengthNode->getChild(0)
                                                                                          : lengthNode));
      }

   TR::Node * addCountNode = createOP2(comp, TR::iadd, indexNode->duplicateTree(), lengthTRxx);
   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, addCountNode);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);

   TR::TreeTop * dstIndVarUpdateTreeTop = NULL;
   if (dstIndexRepNode)
      {
      dstIndVarUpdateTreeTop = TR::TreeTop::create(comp, createStoreOP2(comp, dstIndexVarSymRef, TR::iadd,
                                                                              dstIndexVarSymRef, lengthTRxx, dstIndexRepNode));
      }

   // Insert nodes and maintain the CFG
   block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree());

   // Create the fast path code
   block = trans->insertBeforeNodes(block);
   block->append(TR::TreeTop::create(comp, topOfTranslateNode));
   block->append(indVarUpdateTreeTop);
   if (dstIndVarUpdateTreeTop) block->append(dstIndVarUpdateTreeTop);
   block = trans->insertAfterNodes(block);

   if (target)
      {
      // A single successor
      trans->setSuccessorEdge(block, target);
      }
   else
      {
      // Multiple successors
      TR::SymbolReference * translateTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
      TR_ASSERT(ifeqCiscNode, "Expecting equal CISC node.");
      TR::Node *ifeqNode = ifeqCiscNode->getHeadOfTrNode()->duplicateTree();
      if (ifeqCiscNode->getOpcode() != ifeqNode->getOpCodeValue())
         {
         TR::Node::recreate(ifeqNode, (TR::ILOpCodes)ifeqCiscNode->getOpcode());
         ifeqNode->setBranchDestination(ifeqCiscNode->getDestination());
         }
      TR::Node *tempStore = TR::Node::createStore(translateTemp, ifeqNode->getAndDecChild(0));
      ifeqNode->setAndIncChild(0, TR::Node::createLoad(ifeqNode, translateTemp));
      TR::TreeTop *tempStoreTTop = TR::TreeTop::create(comp, tempStore);
      TR::TreeTop *ifeqTTop = TR::TreeTop::create(comp, ifeqNode);
      // Fix up the IV value by adding 1 if translateNode == lengthNode (where no test char was found).  See comment above.
      TR::Node *incIndex = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthTRxx->getChild(1), indexRepNode);
      TR::TreeTop *incIndexTTop = TR::TreeTop::create(comp, incIndex);

      TR::TreeTop *last = block->getLastRealTreeTop();
      last->join(tempStoreTTop);
      tempStoreTTop->join(incIndexTTop);
      if (dstIndVarUpdateTreeTop)
         {
         TR::Node * incDstIndex = createStoreOP2(comp, dstIndexVarSymRef, TR::isub, dstIndexVarSymRef, -1, dstIndexRepNode);
         TR::TreeTop *incDstIndexTTop = TR::TreeTop::create(comp, incDstIndex);
         incIndexTTop->join(incDstIndexTTop);
         last = incDstIndexTTop;
         }
      else
         {
         last = incIndexTTop;
         }
      last->join(ifeqTTop);
      ifeqTTop->join(block->getExit());
      trans->setSuccessorEdges(block,
                               NULL,    // rely on automatic detection
                               ifeqNode->getBranchDestination()->getEnclosingBlock());
      }

   return true;
   }


/****************************************************************************************
Corresponding Java-like pseudocode
int i, j, end, exitValue;
byte byteArray[ ];
char charArray[ ], map[ ];
while(true){
   char c = map[byteArray[i]];
   if (c == exitValue) break;
   charArray[j] = c;
   i++;
   j;
   if (i >= end) break;
}


Note 1: Idiom allows that variables i and j are identical.
****************************************************************************************/
TR_PCISCGraph *
makeTROTArrayGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TROTArray", 0, 16);
   /**************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 16,   0,   0,    0);
   tgt->addNode(byteArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);
   tgt->addNode(i); // src array index
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 14,   0,   0,    1);
   tgt->addNode(charArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *map        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    2);  tgt->addNode(map); // map array base
   TR_PCISCNode *idx0       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),9,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *exitValue  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(exitValue);// exitvalue (optional)
   TR_PCISCNode *aHeader    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);
   tgt->addNode(aHeader);    // array header constant
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *c1         = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 1); // element size
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 2); // element size
   TR_PCISCNode *offset     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),3,   0,   0);  tgt->addNode(offset);      // optional
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(entry);
   TR_PCISCNode *byteAddr   = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   entry, TR::bloadi, TR::Int8,  byteArray, idx0, aHeader, c1);
   TR_PCISCNode *convNode, *mapAddr;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      convNode = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2l, TR::Int64,      tgt->incNumNodes(),  1,   1,   1,   byteAddr, byteAddr);  tgt->addNode(convNode);
      mapAddr  = createIdiomCharArrayLoadInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1,   convNode,  map, convNode, aHeader, elemSize);
      }
   else
      {
      convNode = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   byteAddr, byteAddr);  tgt->addNode(convNode);
      mapAddr  = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,   convNode,  map, convNode, aHeader, elemSize);
      }
   TR_PCISCNode *c2iNode   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,  tgt->incNumNodes(),  1,   1,   1,   mapAddr, mapAddr);  tgt->addNode(c2iNode);  // optional
   TR_PCISCNode *exitTest  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpeq, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   c2iNode, c2iNode, exitValue);  // optional
   tgt->addNode(exitTest);
   TR_PCISCNode *charAddr  = createIdiomCharArrayStoreInLoop(tgt, ctrl, 1, exitTest, charArray, idx1, aHeader, elemSize, c2iNode);
   TR_PCISCNode *iStore    = createIdiomDecVarInLoop(tgt, ctrl, 1, charAddr, i, increment);
   TR_PCISCNode *jStore    = createIdiomIncVarInLoop(tgt, ctrl, 1, iStore, j, i, offset);      // optional
   TR_PCISCNode *loopTest  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   jStore, i, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   jStore->getChild(0)->setIsOptionalNode();
   jStore->setIsOptionalNode();
   j->setIsOptionalNode();
   offset->setIsOptionalNode();

   convNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();
   charAddr->setIsChildDirectlyConnected(false);

   exitTest->setIsOptionalNode();
   exitValue->setIsOptionalNode();
   c2iNode->setIsOptionalNode();
   c2iNode->getHeadOfParents()->setIsOptionalNode();

   tgt->setSpecialCareNode(0, convNode); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, byteAddr->getChild(0), charAddr->getChild(0));
   tgt->setNumDagIds(17);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2TROTArray);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_1|ILTypeProp::Size_2, ILTypeProp::Size_2);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTOInduction1Graph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 8);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for exploiting the TRTx instruction
// This is the case where the compiler will create the function table by analyzing booltable.
// Input: ImportantNode(0) - booltable
//        ImportantNode(1) - ificmpge
//        ImportantNode(2) - load of the source array
//        ImportantNode(3) - store of the destination array
//        ImportantNode(4) - another ificmpxx if exists (optional)
//*****************************************************************************************
bool
CISCTransform2CopyingTRTx(TR_CISCTransformer *trans)
   {
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool isOutputChar = trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isShort() && trans->getP2TRepInLoop(P->getImportantNode(3))->getIlOpCode().isUnsigned();
   bool genTRxx = comp->cg()->getSupportsArrayTranslateTRxx();
   bool genSIMD = comp->cg()->getSupportsVectorRegisters() && !comp->getOption(TR_DisableSIMDArrayTranslate);

   if (isOutputChar  && genSIMD && !genTRxx){
	   traceMsg(comp, "Bailing CISCTransform2CopyingTRTx : c2c - no proper evaluator available\n");
	   return false;
   }


   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block)
      return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2CopyingTRTx due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode * inputCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2)->getChild(0));
   TR_CISCNode * outputCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3)->getChild(0));
   TR::Node * inputNode = inputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = outputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();

   TR::Node *baseRepNode, *indexRepNode, *dstBaseRepNode, *dstIndexRepNode;
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode, &dstBaseRepNode, &dstIndexRepNode);
   if (indexRepNode == 0) indexRepNode = dstIndexRepNode;
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode ? dstIndexRepNode->getSymbolReference() : NULL;
   if (indexVarSymRef == dstIndexVarSymRef)
      {
      dstIndexRepNode = NULL;
      dstIndexVarSymRef = NULL;
      }
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0 &&
       (!dstIndexVarSymRef || trans->countGoodArrayIndex(dstIndexVarSymRef) == 0))
      {
      if (disptrace) traceMsg(comp, "countGoodArrayIndex failed for %p, %p\n",indexRepNode,dstIndexRepNode);
      return false;
      }
   TR_ScratchList<TR::Node> variableList(comp->trMemory());
   variableList.add(indexRepNode);
   if (dstIndexRepNode) variableList.add(dstIndexRepNode);
   if (!isIndexVariableInList(inputNode, &variableList) ||
       !isIndexVariableInList(outputNode, &variableList))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction variable updates\n", inputNode, outputNode);
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   if (!target) // multiple successors
      {
      // current restrictions. allow only the case where the number of successors is greater than 3.
      if (trans->getNumOfBBlistSucc() > 3)
         {
         if (disptrace) traceMsg(comp, "trans->getNumOfBBlistSucc() is %d.",trans->getNumOfBBlistSucc());
         return false;
         }
      }

   // Check if there is idiom specific node insertion.
   // Currently, it is inserted by moveStoreOutOfLoopForward() or reorderTargetNodesInBB()
   bool isCompensateCode = !trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1);

   // There is an ificmpge node and (multiple successors or need to generate idiom specific node insertion)
   bool isNeedGenIcmpge = (!target || isCompensateCode);

   TR::Node *tableNode;
   uint8_t *tmpTable = (uint8_t*)comp->trMemory()->allocateMemory(65536, stackAlloc);
   bool isAllowSourceCellTermChar = false;

   int count;
   TR::TreeTop *retSameExit = NULL;
   if ((count = trans->analyzeCharBoolTable(P->getImportantNode(0), tmpTable, P->getImportantNode(1), &retSameExit)) <= 0)
      {
      if (disptrace) traceMsg(comp, "trans->analyzeCharBoolTable failed\n");
      return false;
      }

   if (!retSameExit)    // all destinations of booltable are not same
      {
      traceMsg(comp, "Multiple targets for different delimiter checks detected.  Abandoning reduction.\n");
      return false;
      }

   // Check to ensure that the delimiter checks 'break' to the target successor blocks if single successor.
   if (retSameExit != NULL && !isNeedGenIcmpge && retSameExit->getEnclosingBlock() != target)
      {
      traceMsg(comp, "Target for delimiter check (Treetop: %p / Block %d: %p) is different than loop exit block_%d: %p.  Abandoning reduction.\n",
              retSameExit, retSameExit->getEnclosingBlock()->getNumber(), retSameExit->getEnclosingBlock(),
              target->getNumber(), target);
      return false;
      }

   // check if the induction variable needs to be updated by 1
   // this depends on whether the induction variable is incremented
   // before the boolTable exit or after (ie. before the loop driving test)
   //
   TR_CISCNode *boolTableExit = P->getImportantNode(0) ? trans->getP2TRepInLoop(P->getImportantNode(0)) : NULL;
   bool ivNeedsUpdate = false;
   bool dstIvNeedsUpdate = false;
   if (0 && boolTableExit)
      {
      TR::Node *boolTableNode = boolTableExit->getHeadOfTrNodeInfo()->_node;
      traceMsg(comp, "boolTableNode : %p of loop %d\n", boolTableNode, block->getNumber());
      ivNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, indexVarSymRef);
      if (dstIndexVarSymRef)
         dstIvNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, dstIndexVarSymRef);
      }

   // Try to find a terminal byte (but we might not find it in many cases...)
   int termchar = -1;
   int stopchar = -1;
   if (comp->cg()->getSupportsArrayTranslateTRTO255() || comp->cg()->getSupportsArrayTranslateTRTO()  )
      {
      if (isOutputChar)
         return false;

      for (int i = 256; i < 65536; i++)
         if (tmpTable[i] != 1)
            return false;
      if (comp->cg()->getSupportsArrayTranslateTRTO255())
         {
    	 for (int i = 0; i < 256; i++)
    	    if (tmpTable[i] != 0)
    	       return false;
    	 termchar = 0x0ff00ff00;
         }
      else
         {
         bool allOnes = true;
         bool allZeros = true;

         for (int i = 0; i < 128; i++)
            if (tmpTable[i] != 0)
               return false;

         for (int i = 128; i < 256; i++)
            {
            uint8_t u8 = tmpTable[i];
            if (u8 == 0)
               allOnes = false;
            else if (u8 == 1)
               allZeros = false;
            else
               {
               allOnes = false;
               allZeros = false;
               }
            }

            if (allZeros && !allOnes) //this is 255 (ISO_8859_1)
               termchar = 0x0ff00ff00;
            else if (allOnes && !allZeros) //this is 127 (ASCII)
               termchar = 0x0ff80ff80;
            else
               return false;
         }
         //termchar = TERMBYTE; //It needs to be greater than zero, dummy termination char otherwise, i.e., it's not gonna be used,
         tableNode = TR::Node::create(baseRepNode, TR::iconst, 0, 0); //dummy table node, it's not gonna be used
      }
   else         //Z
      {
      if (!isOutputChar)
         {
         uint8_t termByteTable[256];
         memset(termByteTable, 0, 256);
         int i;
         for (i = 0; i < 65536; i++)
            {
            if (tmpTable[i] == 0) {
            	if ( i >= 256)
            		return false;
               termByteTable[i] = 1;
               }
            }


         bool isSIMDPossible = genSIMD;
         if (isSIMDPossible) {
        	//SIMD possible only if we have consecutive chars, and no ranges
        	for (int i = 0; i < 256; i++) {
       	       if (tmpTable[i] == 0) {
       		      if (stopchar != (i-1)) {
                     isSIMDPossible = false;
       			     break;
       		      }
       		      stopchar++;
       		   }
       	    }

       	    //case all non valid chars
       	    if (stopchar == -1 )
       		   isSIMDPossible = false;
         }

        if (isSIMDPossible) {
            tableNode = TR::Node::create(baseRepNode, TR::aconst, 0, 0); //dummy table node, it's not gonna be used
        } else if(!genTRxx){
        	traceMsg(comp, "Bailing CISCTransform2CopyingTRTx : c2b - no proper evaluator available\n");
        	return false;
        } else {
        	//TRxx
        	 for (i = 256; --i >= 0; )
        	 {
        		 if (termByteTable[i] == 0)
        		 {
        			 termchar = i;  // find termchar;
        			 break;
        		 }
        	 }

        	 // Create the function table for TRTO
        	 if (termchar < 0) // no room of termchar
        	 {
        		 isAllowSourceCellTermChar = true; // Generated code will check whether the character is a delimiter.
        		 termchar = TERMBYTE;
        		 if (disptrace)
        			 traceMsg(comp, "setAllowSourceCellIsTermChar: ");
        	 }
        	 if (disptrace)
        		 traceMsg(comp, "termchar is 0x%02x\n", termchar);


        	 uint8_t *table = (uint8_t*)comp->trMemory()->allocateMemory(65536, stackAlloc);
        	 //Only check up to 256 because we already
        	 for (i = 0; i < 65536; i++)
        	 {
        		 uint8_t u8 = tmpTable[i];
        	 	 //Not sure I understand the reasning behind discarding those: chars larger than 256 which map to byte ... possible
        	 	 //we have the table to hold all chars. Value needs to represent i & ff
        	 	 //for now I moved the check up - so bail out earlier.
        	 	 //Reach here only if chars that need mapping are <256.
        	 	 //if 	(!u8 && i >= 256)
        	 	 // return false;
        	 	 table[i] = (uint8_t)(u8 ? termchar : i);
         	 }
        	 tableNode = createTableLoad(comp, baseRepNode, 16, 8, table, disptrace);
         }

         }
      else
         {
    	  //c2c case - currently no SIMD support
    	 uint16_t *table = (uint16_t*)comp->trMemory()->allocateMemory(65536*2, stackAlloc);
         int i;
         for (i = 0; i < 65536; i++)
            {
            uint8_t u8 = tmpTable[i];
            if (u8)
               {
               if (termchar < 0)
                  termchar = i;
               table[i] = termchar;
               }
            else
               {
               table[i] = i;
               }
            }
         tableNode = createTableLoad(comp, baseRepNode, 16, 16, table, disptrace);
         }
      }



      // find the target node of icmpge
   TR_ScratchList<TR_CISCNode> necessaryCmp(comp->trMemory());

   // find icmpge in the candidate region
   sortList(P2T + P->getImportantNode(1)->getID(),
            &necessaryCmp, trans->getCandidateRegion());

   bool isDecrement;
   int32_t modLength;
   TR::Node * cmpIndexNode;
   TR::Node * lenTmpNode;
   TR::Node * lengthNode;

   TR_CISCNode *icmpgeCISCnode1 = NULL;
   TR::Node *lenRepNode1 = NULL;
   TR_CISCNode *icmpgeCISCnode2 = NULL;
   TR::Node *lenRepNode2 = NULL;
   TR::SymbolReference * icmpgeSymRef2 = NULL;

   // We cannot handle too many loop exit tests.
   if (necessaryCmp.getSize() >= 3)
      {
      if (disptrace) traceMsg(comp, "Too many (%d) loop exit tests to transform correctly.  Transformation only supports up to 2.  Abandoning reduction.\n", necessaryCmp.getSize());
      return false;
      }

   icmpgeCISCnode1 = necessaryCmp.getListHead()->getData();

   if (!testExitIF(icmpgeCISCnode1->getOpcode(), &isDecrement, &modLength))
      {
      if (disptrace) traceMsg(comp, "testExitIF for icmpgeCISCnode1 failed\n");
      return false;
      }
   if (isDecrement)
      {
      if (disptrace) traceMsg(comp, "Not support a decrement loop. (icmpgeCISCnode1)\n");
      return false;
      }
   TR_ASSERT(modLength == 0 || modLength == 1, "error");

   // The length calculation requires the initial value of the induction variable
   // used in the loop iteration comparison.
   TR::Node *cmpChild = icmpgeCISCnode1->getHeadOfTrNode()->getChild(0);

   TR::SymbolReference * cmpVarSymRef = NULL;
   while (cmpChild && (cmpChild->getOpCode().isAdd() || cmpChild->getOpCode().isSub()))
      {
      cmpChild = cmpChild->getChild(0);
      }
   if (cmpChild && cmpChild->getOpCode().isLoadVar())
      cmpVarSymRef = cmpChild->getSymbolReference();
   if (cmpVarSymRef == NULL)
      {
      if (disptrace)  traceMsg(comp, "Unable to determine the sym ref of induction variable in loop termination node.\n");
      return false;
      }

   lenRepNode1 = createLoad(icmpgeCISCnode1->getChild(1)->getHeadOfTrNode());
   if (modLength) lenRepNode1 = createOP2(comp, TR::isub, lenRepNode1, TR::Node::create(baseRepNode, TR::iconst, 0, -modLength));
   cmpIndexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, cmpVarSymRef);
   lenTmpNode = createOP2(comp, TR::isub, lenRepNode1, cmpIndexNode);
   if (necessaryCmp.isDoubleton())
      {
      icmpgeCISCnode2 = necessaryCmp.getListHead()->getNextElement()->getData();
      }

   // analyze ImportantNode(4) - another ificmpxx
   if (P->getImportantNode(4))
      {
      if (icmpgeCISCnode2)
         {
         if (disptrace) traceMsg(comp, "Not support yet more than three if-statements. (1)\n");
         return false;
         }
      icmpgeCISCnode2 = trans->getP2TInLoopIfSingle(P->getImportantNode(4));
      if (!icmpgeCISCnode2)
         {
         if (disptrace) traceMsg(comp, "Not support yet more than three if-statements. (2)\n");
         return false;
         }
      }

   if (icmpgeCISCnode2)
      {
      if (!testExitIF(icmpgeCISCnode2->getOpcode(), &isDecrement, &modLength))
         {
         if (disptrace) traceMsg(comp, "testExitIF for icmpgeCISCnode2 failed\n");
         return false;
         }
      if (isDecrement)
         {
         if (disptrace) traceMsg(comp, "Not support a decrement loop. (icmpgeCISCnode2)\n");
         return false;
         }
      TR_ASSERT(modLength == 0 || modLength == 1, "error");
      lenRepNode2 = createLoad(icmpgeCISCnode2->getChild(1)->getHeadOfTrNode());
      if (modLength) lenRepNode2 = createOP2(comp, TR::isub, lenRepNode2, TR::Node::create(baseRepNode, TR::iconst, 0, -modLength));

      TR::Node *icmpgeNode2 = icmpgeCISCnode2->getHeadOfTrNode();
      TR_ASSERT(icmpgeNode2->getChild(0)->getOpCode().isLoadVarDirect(), "Please remove this assertion");
      if (!icmpgeNode2->getChild(0)->getOpCode().isLoadVarDirect()) return false;
      icmpgeSymRef2 = icmpgeNode2->getChild(0)->getSymbolReference();

      TR::Node *lenTmpNode2 = createOP2(comp, TR::isub, lenRepNode2, TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, icmpgeSymRef2));

      lenTmpNode = createMin(comp, lenTmpNode, lenTmpNode2);
      }
   lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lenTmpNode);

   // Modify array header constant if necessary
   TR::Node *constLoad;
   if (trans->getOffsetOperand1())
      {
      int32_t offset = trans->getOffsetOperand1() * 2;
      constLoad = modifyArrayHeaderConst(comp, inputNode, offset);
      TR_ASSERT(constLoad, "Not implemented yet");
      if (disptrace) traceMsg(comp,"The array header const of inputNode %p is modified. (offset=%d)\n", inputNode, offset);
      }
   if (trans->getOffsetOperand2())
      {
      int32_t offset = trans->getOffsetOperand2() * (isOutputChar ? 2 : 1);
      constLoad = modifyArrayHeaderConst(comp, outputNode, offset);
      TR_ASSERT(constLoad, "Not implemented yet");
      if (disptrace) traceMsg(comp,"The array header const of outputNode %p is modified. (offset=%d)\n", outputNode, offset);
      }

   // Prepare arraytranslate
   TR::Node * termCharNode = TR::Node::create( baseRepNode, TR::iconst, 0, termchar);
   TR::Node * stopCharNode = TR::Node::create( baseRepNode, TR::iconst, 0, stopchar);



   TR::Node * translateNode = TR::Node::create(trNode, TR::arraytranslate, 6);
   translateNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateSymbol());
   translateNode->setAndIncChild(0, inputNode);
   translateNode->setAndIncChild(1, outputNode);
   translateNode->setAndIncChild(2, tableNode);
   translateNode->setAndIncChild(3, termCharNode);
   translateNode->setAndIncChild(4, lengthNode);
   translateNode->setAndIncChild(5, stopCharNode);

   translateNode->setSourceIsByteArrayTranslate(false);
   translateNode->setTargetIsByteArrayTranslate(!isOutputChar);
   translateNode->setTableBackedByRawStorage(true);
   if (isAllowSourceCellTermChar)
      {
      translateNode->setTermCharNodeIsHint(true);
      //translateNode->setAllowSourceCellIsTermChar(true); // Generated code will check whether the character is a delimiter.
      // determine the use of this flag on the node
      translateNode->setSourceCellIsTermChar(true); // Generated code will check whether the character is a delimiter.
      }
   else
      {
      translateNode->setTermCharNodeIsHint(false);
      translateNode->setSourceCellIsTermChar(false);
      }
   TR::SymbolReference * translateTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
   TR::Node * topOfTranslateNode = TR::Node::createStore(translateTemp, translateNode);

   // prepare nodes that add the number of elements (which was translated) into the induction variables
   TR::Node *addCountNode = createOP2(comp, TR::iadd,
                                    TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef),
                                    translateNode);
   if (ivNeedsUpdate)
      addCountNode = TR::Node::create(TR::iadd, 2, addCountNode, TR::Node::iconst(indexRepNode, 1));

   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, addCountNode);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);

   TR::TreeTop * dstIndVarUpdateTreeTop = NULL;
   TR::Node *dstIndVarInitializer = NULL;
   if (dstIndexRepNode)
      {
      dstIndVarInitializer = areDefsOnlyInsideLoop(comp, trans, outputCISCNode->getHeadOfTrNodeInfo()->_node);

      TR::Node *dstAddCountNode = NULL;
      if (dstIndexVarSymRef->getSymbol()->getDataType() == TR::Int32)
         {
         dstAddCountNode = createOP2(comp, TR::iadd,
                                            TR::Node::createWithSymRef(dstIndexRepNode, TR::iload, 0, dstIndexVarSymRef),
                                            translateNode);
         if (dstIvNeedsUpdate)
            dstAddCountNode = TR::Node::create(TR::iadd, 2, dstAddCountNode, TR::Node::iconst(dstAddCountNode, 1));
         }
      else
         {
         dstAddCountNode = createOP2(comp, TR::ladd,
                                           TR::Node::createWithSymRef(dstIndexRepNode, TR::lload, 0, dstIndexVarSymRef),
                                           TR::Node::create(TR::i2l, 1, translateNode));
         if (dstIvNeedsUpdate)
            dstAddCountNode = TR::Node::create(TR::ladd, 2, dstAddCountNode, TR::Node::lconst(dstAddCountNode, 1));
         }
      TR::Node * dstIndVarUpdateNode = TR::Node::createStore(dstIndexVarSymRef, dstAddCountNode);
      dstIndVarUpdateTreeTop = TR::TreeTop::create(comp, dstIndVarUpdateNode);
      }

   // create Nodes if there are multiple exit points.
   TR::Node *icmpgeNode = NULL;
   TR::TreeTop *failDest = NULL;
   TR::TreeTop *okDest = NULL;
   TR::Block *compensateBlock0 = NULL;
   TR::Block *compensateBlock1 = NULL;
   if (icmpgeCISCnode2)
      {
      TR_ASSERT(isNeedGenIcmpge, "assumption error?");
      TR::Node *icmpgeNode2 = NULL;
      TR::TreeTop *failDest2 = NULL;
      TR::Block *compensateBlock2 = NULL;
      TR::Block *newBlockForIf2 = NULL;

      if (disptrace) traceMsg(comp, "Now assuming that all exits of booltable are identical.\n");

      icmpgeNode = icmpgeCISCnode1->getHeadOfTrNode();
      okDest = retSameExit;
      failDest = icmpgeCISCnode1->getDestination();

      icmpgeNode2 = icmpgeCISCnode2->getHeadOfTrNode();
      failDest2 = icmpgeCISCnode2->getDestination();
      newBlockForIf2 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      // create two empty blocks for inserting compensation code (base[index] and base[index-1]) prepared by moveStoreOutOfLoopForward()
      if (isCompensateCode)
         {
         compensateBlock2 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock2->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest2)));
         failDest2 = compensateBlock2->getEntry();

         compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         compensateBlock0->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
         compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest)));
         okDest = compensateBlock0->getEntry();
         failDest = compensateBlock1->getEntry();
         }
      if (disptrace)
         {
         if (okDest == NULL) traceMsg(comp,"error, okDest == NULL!\n");
         if (failDest == NULL) traceMsg(comp,"error, failDest == NULL!\n");
         if (failDest2 == NULL) traceMsg(comp,"error, failDest2 == NULL!\n");
         }
      TR_ASSERT(okDest != NULL && failDest != NULL && failDest2 != NULL, "error!");

      // It generates "ificmpge".
      icmpgeNode = TR::Node::createif(TR::ificmpge,
                                     cmpIndexNode->duplicateTree(),
                                     lenRepNode1,
                                     failDest);
      icmpgeNode2 = TR::Node::createif(TR::ificmpge,
                                     TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, icmpgeSymRef2),
                                     lenRepNode2->duplicateTree(),
                                     failDest2);

      // Insert nodes and maintain the CFG
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lenTmpNode->duplicateTree());

      if (P->needsInductionVariableInit())
         {
         TR::TreeTop *storeTree = TR::TreeTop::create(comp, dstIndexRepNode->duplicateTree());
         block->prepend(storeTree);
         }

      // Create the fast path code
      block = trans->insertBeforeNodes(block);
      TR::TreeTop *translateTT = TR::TreeTop::create(comp, topOfTranslateNode);
      block->append(translateTT);
      if (dstIndVarInitializer)
         {
         translateTT->insertBefore(TR::TreeTop::create(comp, dstIndVarInitializer));
         }
      block->append(indVarUpdateTreeTop);
      if (dstIndVarUpdateTreeTop) block->append(dstIndVarUpdateTreeTop);
      block = trans->insertAfterNodes(block);

      block->append(TR::TreeTop::create(comp, icmpgeNode));
      newBlockForIf2->append(TR::TreeTop::create(comp, icmpgeNode2));
      TR::CFG *cfg = comp->getFlowGraph();
      cfg->setStructure(NULL);
      TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
      TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
      if (isCompensateCode)
         {
         compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);       // ch = base[index]
         compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);       // ch = base[index-1]
         // Duplicate all insertion nodes in getAfterInsertionIdiomList(1)
         ListElement<TR::Node> *le;
         for (le = trans->getAfterInsertionIdiomList(1)->getListHead(); le; le = le->getNextElement())
            {
            le->setData(le->getData()->duplicateTree());
            }
         compensateBlock2 = trans->insertAfterNodesIdiom(compensateBlock2, 1, true);       // ch = base[index-1]
         cfg->insertBefore(compensateBlock0, orgNextBlock);
         cfg->insertBefore(compensateBlock1, compensateBlock0);
         cfg->insertBefore(compensateBlock2, compensateBlock1);
         cfg->insertBefore(newBlockForIf2, compensateBlock2);
         cfg->join(block, newBlockForIf2);
         }
      else
         {
         cfg->insertBefore(newBlockForIf2, orgNextBlock);
         cfg->join(block, newBlockForIf2);
         }
      trans->setSuccessorEdges(block,
                               newBlockForIf2,
                               failDest->getEnclosingBlock());
      trans->setSuccessorEdges(newBlockForIf2,
                               okDest->getEnclosingBlock(),
                               failDest2->getEnclosingBlock());
      }
   else
      {
      if (isNeedGenIcmpge)
         {
         if (disptrace) traceMsg(comp, "Now assuming that all exits of booltable are identical and the exit of icmpge points different.\n");

         icmpgeNode = icmpgeCISCnode1->getHeadOfTrNode();
         okDest = retSameExit;
         failDest = icmpgeCISCnode1->getDestination();
         // create two empty blocks for inserting compensation code (base[index] and base[index-1]) prepared by moveStoreOutOfLoopForward()
         if (isCompensateCode)
            {
            compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
            compensateBlock0 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
            compensateBlock0->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
            compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest)));
            okDest = compensateBlock0->getEntry();
            failDest = compensateBlock1->getEntry();
            }
         TR_ASSERT(okDest != NULL && failDest != NULL && okDest != failDest, "error!");

         // It actually generates "ificmplt" (NOT ificmpge!) in order to suppress a redundant goto block.
         icmpgeNode = TR::Node::createif(TR::ificmplt,
                                        cmpIndexNode->duplicateTree(), // TR::Node::create(indexRepNode, TR::iload, 0, indexVarSymRef),
                                        lenRepNode1,
                                        okDest);
         }

      // Insert nodes and maintain the CFG
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lenTmpNode->duplicateTree());

      if (P->needsInductionVariableInit())
         {
         TR::TreeTop *storeTree = TR::TreeTop::create(comp, dstIndexRepNode->duplicateTree());
         block->prepend(storeTree);
         }

      // Create the fast path code
      block = trans->insertBeforeNodes(block);
      block->append(TR::TreeTop::create(comp, topOfTranslateNode));
      block->append(indVarUpdateTreeTop);
      if (dstIndVarUpdateTreeTop) block->append(dstIndVarUpdateTreeTop);
      block = trans->insertAfterNodes(block);

      if (isNeedGenIcmpge)
         {
         block->append(TR::TreeTop::create(comp, icmpgeNode));
         if (isCompensateCode)
            {
            TR::CFG *cfg = comp->getFlowGraph();
            cfg->setStructure(NULL);
            TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
            TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
            compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);       // ch = base[index]
            compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);       // ch = base[index-1]
            cfg->insertBefore(compensateBlock0, orgNextBlock);
            cfg->insertBefore(compensateBlock1, compensateBlock0);
            cfg->join(block, compensateBlock1);
            }
         }
      else if (isCompensateCode)
         {
         block = trans->insertAfterNodesIdiom(block, 0);       // ch = base[index]
         }

      // set successor edge(s) to the original block
      if (!isNeedGenIcmpge)
         {
         trans->setSuccessorEdge(block, target);
         }
      else
         {
         trans->setSuccessorEdges(block,
                                  failDest->getEnclosingBlock(),
                                  okDest->getEnclosingBlock());
         }
      }

   return true;
   }

bool
CISCTransform2CopyingTRTxAddDest1(TR_CISCTransformer *trans)
   {
   trans->setOffsetOperand2(1);        // add offset of destination with 1
   return CISCTransform2CopyingTRTx(trans);
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int i, j, end;
char charArray[ ];
byte byteArray[ ];
while(true){
   char c = charArray[i];
   if (booltable(c)) break;
   byteArray[j] = (byte)c;
   i++;
   j++;
   if (j >= end) break;
}

Note 1: It allows that variables v1 and v3 are identical.
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTRTxGraph(TR::Compilation *c, int32_t ctrl, int pattern)
   {
   TR_ASSERT(pattern == 0 || pattern == 1 || pattern == 2, "not implemented");
   char *name = (char *)TR_MemoryBase::jitPersistentAlloc(16);
   sprintf(name, "CopyingTRTx(%d)",pattern);
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), name, 0, 16);
   /***************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 15,   0,   0,    0);
   tgt->addNode(charArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(i); // src array index
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    1);
   tgt->addNode(byteArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *idx0       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),  9,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *aHeader0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(aHeader0);     // array header
   TR_PCISCNode *aHeader1   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(aHeader1);     // array header
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  5,   0,   0);        tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 2); // element size
   TR_PCISCNode *offset     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  3,   0,   0,    2);  tgt->addNode(offset); // optional
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(entry);
   TR_PCISCNode *charAddr   = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,  entry, charArray, idx0, aHeader0, elemSize);
   TR_PCISCNode *c2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   charAddr, charAddr);
   tgt->addNode(c2iNode);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   c2iNode, c2iNode);
   tgt->addNode(exitTest);
   TR_PCISCNode *byteAddr   = createIdiomArrayStoreInLoop(tgt, ctrl, 1, exitTest, TR_ibcstore, TR::NoType,  byteArray, idx1, aHeader1, mulFactor, c2iNode);
   TR_PCISCNode *store1, *store2;
   switch(pattern)
      {
      case 0:
         store1     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);     // optional (i = i + 1)
         store2     = createIdiomDecVarInLoop(tgt, ctrl, 1, store1, j, idx1, increment); // j = idx1 + 1
         store1->getChild(0)->setIsOptionalNode();
         store1->setIsOptionalNode();
         break;
      case 1:
         store1     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, j, idx1, increment);  // j = idx1 + 1
         store2     = createIdiomIncVarInLoop(tgt, ctrl, 1, store1, i, j, offset);      // optional (i = j + offset)
         store2->getChild(0)->setIsOptionalNode();
         store2->setIsOptionalNode();
         break;
      case 2:
         store1     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);     // optional (i = i + 1)
         store2     = createIdiomDecVarInLoop(tgt, ctrl, 1, store1, j, j, increment);   // j = j + 1
         store1->getChild(0)->setIsOptionalNode();
         store1->setIsOptionalNode();
         break;
      }
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   store2, j, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   i->setIsOptionalNode();
   offset->setIsOptionalNode();

   c2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, exitTest); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, charAddr, byteAddr, NULL);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTRTx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_2, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(2, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTxGraph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 15);
   tgt->setVersionLength(versionLength);   // depending on each architecture

   tgt->setPatternType(pattern);

   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int i, j, end;
char charArray[ ];
byte byteArray[ ];
while(true){
   char c = charArray[i];
   if (booltable(c)) break;
   if (j > end) break;
   byteArray[j] = (byte)c;
   i++;
   j++;
   if (i >= end) break;
}

Note 1: It allows that variables i and j are identical.
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTRTxThreeIfsGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CopyingTRTxThreeIfs", 0, 16);
   /***************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);
   tgt->addNode(charArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(i); // src array index
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    1);
   tgt->addNode(byteArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),  9,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *end2       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),  8,   0,   0);  tgt->addNode(end2);     // length2
   TR_PCISCNode *aHeader0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(aHeader0);     // array header
   TR_PCISCNode *aHeader1   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(aHeader1);     // array header
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  4,   0,   0);        tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2); // element size
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(entry);
   TR_PCISCNode *charAddr   = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,  entry, charArray, i, aHeader0, elemSize);
   TR_PCISCNode *c2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   charAddr, charAddr);
   tgt->addNode(c2iNode);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   c2iNode, c2iNode);
   tgt->addNode(exitTest);
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   exitTest, j, end);  tgt->addNode(loopTest);
   TR_PCISCNode *byteAddr   = createIdiomArrayStoreInLoop(tgt, ctrl, 1, loopTest, TR_ibcstore, TR::NoType,  byteArray, j, aHeader1, mulFactor, c2iNode);
   TR_PCISCNode *store1     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);     // i = i + 1
   TR_PCISCNode *store2     = createIdiomDecVarInLoop(tgt, ctrl, 1, store1, j, j, increment);   // j = j + 1
   TR_PCISCNode *loopTest2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   store2, i, end2);  tgt->addNode(loopTest2);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSucc(1, exit);
   loopTest2->setSuccs(entry->getSucc(0), exit);

   c2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();
   loopTest2->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, exitTest); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest2, charAddr, byteAddr, loopTest);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTRTx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_2, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(3, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTxThreeIfsGraph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 15);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   return tgt;
   }


/****************************************************************************************
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTRTOGraphSpecial(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CopyingTRTOSpecial", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 12,   0,   0,    0);
   tgt->addNode(charArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(i); // src array index
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);
   tgt->addNode(byteArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *aHeader0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(aHeader0);     // array header
   TR_PCISCNode *aHeader1   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(aHeader1);     // array header
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  4,   0,   0);        tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);                    // element size
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(entry);
   TR_PCISCNode *charAddr   = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,  entry, charArray, i, aHeader0, elemSize);
   TR_ASSERT((ctrl & CISCUtilCtl_64Bit) && i->getParents()->isSingleton(), "assumption error");
   TR_PCISCNode *i2lNode    = (TR_PCISCNode *)i->getHeadOfParents();
   TR_PCISCNode *c2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   charAddr, charAddr);
   tgt->addNode(c2iNode);
   TR_PCISCNode *lStore     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lstore , TR::Int64,   tgt->incNumNodes(),  1,   1,   2,   c2iNode, i2lNode, j);
   tgt->addNode(lStore);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   lStore, c2iNode);  tgt->addNode(exitTest);
   TR_PCISCNode *byteAddr   = createIdiomArrayStoreInLoop(tgt, ctrl|CISCUtilCtl_NoI2L, 1, exitTest, TR_ibcstore, TR::NoType,  byteArray, j, aHeader1, mulFactor, c2iNode);
   TR_PCISCNode *iStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   iStore, i, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   c2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, c2iNode); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, charAddr, byteAddr, NULL);
   tgt->setNumDagIds(13);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTRTx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(2, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTOGraphSpecial_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 15);
   tgt->setVersionLength(versionLength);   // depending on each architecture

   // needs induction variable init
   tgt->setNeedsInductionVariableInit(true);

   return tgt;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, end;
int v3;			// optional
int v4;	// v4 usually has the value of "v3 - v1".
byte v0[ ];
char v2[ ];
while(true){
   char T = (char)v0[v1];
   if (booltable(T)) break;
   v2[v1+v4] = T;
   v3 = (v1+v4)+1;		// optional
   v1++;
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTRTOInduction1Graph(TR::Compilation *c, int32_t ctrl, int32_t pattern)
   {
   TR_ASSERT(pattern == 0 || pattern == 1 || pattern == 2, "not implemented");
   char *name = (char *)TR_MemoryBase::jitPersistentAlloc(26);
   sprintf(name, "CopyingTRTOInduction1(%d)",pattern);
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), name, 0, 16);
   /*********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v3); // actual dst array index (optional)
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    2);  tgt->addNode(v4); // difference of dst array index from src array index
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 1);                    // element size
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1,   ent, TR::sloadi, TR::Int16,  v0, v1, cmah0, c2);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   n3, n3);  tgt->addNode(n4);  // optional
   TR_PCISCNode *n5, *nn0, *op1, *n6;
   switch(pattern)
      {
      case 0: {
         // v2[v1+v4] = T;
         // v3 = (v1+v4)+1      (optional)
         // v1++;
         TR_PCISCNode *op0;
         n5 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iadd, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n4, v1, v4);  tgt->addNode(n5);
         nn0 = createIdiomArrayStoreInLoop(tgt, ctrl, 1, n5, TR::bstorei, TR::Int8,  v2, n5, cmah1, c1, n3);
         op0 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nn0, n5, cm1);  tgt->addNode(op0);  // (optional)
         op1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   op0,op0, v3); tgt->addNode(op1);  // (optional)
         n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, op1, v1, cm1);
         op0->setIsOptionalNode();
         op1->setIsOptionalNode();
         op1->setIsChildDirectlyConnected();
         break; }

      case 1: {
         // v2[v3] = T;
         // v1++;
         // v3 = v1+v4;
         op1 = NULL;
         nn0 = createIdiomArrayStoreInLoop(tgt, ctrl, 1, n4, TR::bstorei, TR::Int8,  v2, v3, cmah1, c1, n3);
         n5  = createIdiomDecVarInLoop(tgt, ctrl, 1, nn0, v1, cm1);
         n6  = createIdiomIncVarInLoop(tgt, ctrl, 1, n5,  v3, v1, v4);
         break; }

      case 2: {
         // v1++;
         // v2[v1+v4] = T;      In this case, we need to add 1 to the destination index, because v1 was incremented.
         // v3 = v1+v4;
         TR_PCISCNode *n45;
         n45 = createIdiomDecVarInLoop(tgt, ctrl, 1, n4, v1, cm1);
         n5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iadd, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n45, v1, v4);  tgt->addNode(n5);
         nn0 = createIdiomArrayStoreInLoop(tgt, ctrl, 1, n5, TR::bstorei, TR::Int8,  v2, n5, cmah1, c1, n3);
         op1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   nn0, n5, v3); tgt->addNode(op1);
         n6  = op1;
         op1->setIsChildDirectlyConnected();
         break; }
      }
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n6, v1, vorc);  tgt->addNode(n7);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   n4->setIsOptionalNode();
   v3->setIsOptionalNode();

   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, n2, nn0, NULL);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(pattern != 2 ? CISCTransform2CopyingTRTx : CISCTransform2CopyingTRTxAddDest1);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTOInduction1Graph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 8);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v3, end;
char v0[ ];
char v2[ ];
while(true){
   if (booltable(v0[v1])) break;
   v2[v3] = v0[v1];
   v1++;
   v3++;
   if (v1 >= end) break;
}

Note 1: It allows that variables v1 and v3 are identical.
****************************************************************************************/
TR_PCISCGraph *
makeCopyingTRTTSpecialGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CopyingTRTTSpecial", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 8,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 7,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),6,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n2  = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,  ent, v0, idx0, cmah, c2);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_booltable, TR::NoType, tgt->incNumNodes(),  1,   2,   1,   n3, n3);  tgt->addNode(n4);
   TR_PCISCNode *n5  = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,  n4, v0, idx0, cmah, c2);
   TR_PCISCNode *nn0 = createIdiomCharArrayStoreInLoop(tgt, ctrl|CISCUtilCtl_NoConversion, 1, n5, v2, idx1, cmah, c2, n5);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, nn0, v1, cm1);
   TR_PCISCNode *nn6 = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v3, cm1);
   TR_PCISCNode *n7  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   nn6, v3, vorc);  tgt->addNode(n7);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n8);

   n4->setSucc(1, n8);
   n7->setSuccs(ent->getSucc(0), n8);

   n3->setIsChildDirectlyConnected();
   n7->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, n4); // TR_booltable
   tgt->setEntryNode(ent);
   tgt->setExitNode(n8);
   tgt->setImportantNodes(n4, n7, n2, nn0, NULL);
   tgt->setNumDagIds(13);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2CopyingTRTx);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul|sameTypeLoadStore, ILTypeProp::Size_2, ILTypeProp::Size_2);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(2, 2, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   static char *versionLengthStr = feGetEnv("TR_CopyingTRTTSpecialGraph_versionLength");
   static int   versionLength = versionLengthStr ? atoi(versionLengthStr) : (c->target().cpu.isPower() ? 0 : 20);
   tgt->setVersionLength(versionLength);   // depending on each architecture
   return tgt;
   }

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//*****************************************************************************************
// IL code generation for exploiting the TRTO instruction
// This is the case where the function table is prepared by the user program.
// Input: ImportantNode(0) - ificmpeq (booltable)
//        ImportantNode(1) - ificmpge
//        ImportantNode(2) - address of the source array
//        ImportantNode(3) - address of the destination array
//        ImportantNode(4) - optional ificmpge for limit checking
//*****************************************************************************************
bool
CISCTransform2TRTOArray(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2TRTOArray due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode * inputCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(2));
   TR_CISCNode * outputCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
   if (!inputCISCNode || !outputCISCNode) return false;
   TR::Node * inputNode = inputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = outputCISCNode->getHeadOfTrNodeInfo()->_node->duplicateTree();

   TR::Node *baseRepNode, *indexRepNode, *dstBaseRepNode, *dstIndexRepNode, *mapBaseRepNode;
   getP2TTrRepNodes(trans, &baseRepNode, &indexRepNode, &dstBaseRepNode, &dstIndexRepNode, &mapBaseRepNode);
   TR::Node *cmpRepNode = trans->getP2TRep(P->getImportantNode(1))->getHeadOfTrNodeInfo()->_node;
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode ? dstIndexRepNode->getSymbolReference() : NULL;
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0) return false;
   if (dstIndexVarSymRef == indexVarSymRef)
      {
      dstIndexRepNode = NULL;
      dstIndexVarSymRef = NULL;
      }
   if (dstIndexVarSymRef)
      {
      if (trans->countGoodArrayIndex(dstIndexVarSymRef) == 0) return false;
      }
   TR_ScratchList<TR::Node> variableList(comp->trMemory());
   variableList.add(indexRepNode);
   if (dstIndexRepNode) variableList.add(dstIndexRepNode);
   if (!isIndexVariableInList(inputNode, &variableList) ||
       !isIndexVariableInList(outputNode, &variableList))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction varaible updates\n", inputNode, outputNode);
      return false;
      }

   // check if the induction variable needs to be updated by 1
   // this depends on whether the induction variable is incremented
   // before the boolTable exit or after (ie. before the loop driving test)
   //
   TR_CISCNode *boolTableExit = P->getImportantNode(0) ? trans->getP2TRepInLoop(P->getImportantNode(0)) : NULL;
   bool ivNeedsUpdate = false;
   bool dstIvNeedsUpdate = false;
   if (0 && boolTableExit)
      {
      TR::Node *boolTableNode = boolTableExit->getHeadOfTrNodeInfo()->_node;
      ///traceMsg(comp, "boolTableNode : %p of loop %d\n", boolTableNode, block->getNumber());
      ivNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, indexVarSymRef);
      if (dstIndexVarSymRef)
         dstIvNeedsUpdate = ivIncrementedBeforeBoolTableExit(comp, boolTableNode, block, dstIndexVarSymRef);
      }


   TR::Block *target = trans->analyzeSuccessorBlock();

   // Prepare arraytranslate node
   TR::Node * tableNode = createLoad(mapBaseRepNode);
   if (tableNode->getOpCode().isLong() && comp->target().is32Bit())
      tableNode = TR::Node::create(TR::l2i, 1, tableNode);
   TR::Node * indexNode = TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, indexVarSymRef);
   TR::Node * lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(),
                                               createOP2(comp, TR::isub, cmpRepNode->getChild(1)->duplicateTree(), indexNode));
   TR::Node * termCharNode = createLoad(trans->getP2TRep(P->getImportantNode(0)->getChild(1))->getHeadOfTrNodeInfo()->_node);
   TR::Node * stoppingNode = TR::Node::create( baseRepNode, TR::iconst, 0, 0xffffffff);

   TR::Node * translateNode = TR::Node::create(trNode, TR::arraytranslate, 6);
   translateNode->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayTranslateSymbol());
   translateNode->setAndIncChild(0, inputNode);
   translateNode->setAndIncChild(1, outputNode);
   translateNode->setAndIncChild(2, tableNode);
   translateNode->setAndIncChild(3, termCharNode);
   translateNode->setAndIncChild(4, lengthNode);
   translateNode->setAndIncChild(5, stoppingNode);

   translateNode->setSourceIsByteArrayTranslate(false);
   translateNode->setTargetIsByteArrayTranslate(true);
   translateNode->setTermCharNodeIsHint(false);
   translateNode->setSourceCellIsTermChar(false);
   translateNode->setTableBackedByRawStorage(trans->isTableBackedByRawStorage());
   TR::Node * topOfTranslateNode = TR::Node::create(TR::treetop, 1, translateNode);
   TR::Node * lengthTRxx = translateNode;

   TR_CISCNode *ifeqCiscNode = NULL;
   TR::Node *ifeqNode = NULL;
   if (target)  // single successor block
      {
      // prepare nodes that add the number of elements (which was translated) into the induction variables

      /*lengthTRxx = createOP2(comp, TR::isub,
                                   translateNode,
                                   TR::Node::create(translateNode, TR::iconst, 0, -1)); */
      }
   else
      {         // multiple successor blocks
      // A loop may have multiple successor blocks (i.e. break from a test character match)
      // First, we need to identify the node that we will try to match.  We have one of two
      // scenarios:
      //     1.  b2i node (commoned with the b2i load of the translation table character)
      //     2.  iload of an auto - the same auto should have a preceding store with the
      //                            translation table character.
      //  In case 2, we'll try to replace the load with the RHS expression of the corresponding
      //  store (expect b2i node).
      //
      //  Once we have the b2i node in hand, we attempt to break the commoning of that node between
      //  the store and test comparison node.
      TR_CISCNode *b2iCiscNode = NULL;
      TR::Node *b2iNode = NULL;

      ifeqCiscNode = trans->getP2TRep(P->getImportantNode(0));
      b2iCiscNode = ifeqCiscNode->getChild(0);
      TR_CISCNode *store;
      ifeqNode = ifeqCiscNode->getHeadOfTrNodeInfo()->_node;
      // try to find a tree including the array load
      switch(b2iCiscNode->getOpcode())
         {
         case TR::b2i:
            break;
         case TR::iload:
            TR_ASSERT(b2iCiscNode->getChains()->isSingleton(), "Not implemented yet");
            store = b2iCiscNode->getChains()->getListHead()->getData();
            b2iCiscNode = store->getChild(0);
            TR_ASSERT(b2iCiscNode->getOpcode() == TR::b2i, "Not implemented yet");
            b2iNode = b2iCiscNode->getHeadOfTrNodeInfo()->_node;
            break;
         case TR_variable:
            if (ifeqCiscNode->isEmptyHint()) return false;
            b2iCiscNode = ifeqCiscNode->getHintChildren()->getListHead()->getData();
            TR_ASSERT(b2iCiscNode->getOpcode() == TR::b2i, "Not implemented yet");
            store = b2iCiscNode->getHeadOfParents();
            TR_ASSERT(store->getOpcode() == TR::istore, "Not implemented yet");
            TR_ASSERT(store->getChild(1) == ifeqCiscNode->getChild(0), "Not implemented yet");
            b2iNode = b2iCiscNode->getHeadOfTrNodeInfo()->_node;
            break;
         default:
            TR_ASSERT(0, "Not implemented yet");
            break;
         }
      // Expect b2iCiscNode has the tree.
      TR_CISCNode *ixload, *aload, *iload;
      if (getThreeNodesForArray(b2iCiscNode, &ixload, &aload, &iload))
         {
         // Try to replace "iload" with a RHS expression of the single store.
         if (iload->getOpcode() == TR::iload &&
             iload->getChains()->isSingleton() &&
             iload->getParents()->isSingleton())
            {   // simple copy propagation
            TR_ASSERT(iload->getChains()->isSingleton(), "Not implemented yet");
            store = iload->getChains()->getListHead()->getData();
            TR::Node *storeTR = store->getHeadOfTrNode();
            TR::Node *iloadTR = iload->getHeadOfTrNode();

            TR_ASSERT(iload->getParents()->isSingleton(), "Not implemented yet");
            TR_CISCNode *iloadParent = iload->getHeadOfParents();
            TR::Node *iloadParentTR = iloadParent->getHeadOfTrNodeInfo()->_node;

            if (iloadParentTR->getChild(0) == iloadTR)
               {
               iloadParentTR->setAndIncChild(0, storeTR->getChild(0)->duplicateTree());
               }
            else if (iloadParentTR->getChild(1) == iloadTR)
               {
               iloadParentTR->setAndIncChild(1, storeTR->getChild(0)->duplicateTree());
               }
            else
               {
               TR_ASSERT(false, "Not implemented yet");
               }
            }
         }
      if (b2iNode)
         {
         ifeqNode->getAndDecChild(0);
         ifeqNode->setAndIncChild(0, b2iNode->duplicateTree());
         }

      // For Multiple Successor Blocks, we have a test character condition in the
      // loop, which may lead to a different successor block than the fallthrough.
      // We need to be able to distinguish the following two scenarios, which both
      // would load the last character in the source array:
      //   1.  no test character found (translateNode == lengthNode).
      //   2.  test character found in the last element(translateNode < lengthNode).
      // The final IV value is always (IV + translateNode).
      // However, under case 1,  the element loaded is at index (IV + translateNode - 1).
      // Under case 2, the element loaded is at index (IV + translateNode).
      // As such, we will subtract 1 in the existing final IV calculation for case 1,
      // so that any array accesses will be correctly indexed.  The final IV value will
      // be increased by 1 again before we hit the exit test.
      lengthTRxx = TR::Node::create(TR::isub, 2, translateNode,
                                   TR::Node::create(TR::icmpeq, 2, translateNode,
                                                   lengthNode->getOpCodeValue() == TR::i2l ? lengthNode->getChild(0)
                                                                                          : lengthNode));
      }

   // prepare nodes that add the number of elements (which was translated) into the induction variables
   TR::Node * addCountNode = createOP2(comp, TR::iadd, indexNode->duplicateTree(), lengthTRxx);
   if (ivNeedsUpdate)
      addCountNode = TR::Node::create(TR::iadd, 2, addCountNode, TR::Node::iconst(indexNode, 1));
   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, addCountNode);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);

   TR::TreeTop * dstIndVarUpdateTreeTop = NULL;
   if (dstIndexRepNode)
      {
      TR::Node *dstAddCountNode = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd,
                                                      dstIndexVarSymRef, lengthTRxx, dstIndexRepNode);
      if (dstIvNeedsUpdate)
         dstAddCountNode = TR::Node::create(TR::iadd, 2, dstAddCountNode, TR::Node::iconst(dstAddCountNode, 1));

      dstIndVarUpdateTreeTop = TR::TreeTop::create(comp, dstAddCountNode);
      }

   // Insert nodes and maintain the CFG
   TR_CISCNode *optionalIficmpge = NULL;
   if (P->getImportantNode(4)) optionalIficmpge = trans->getP2TRepInLoop(P->getImportantNode(4));
   TR_ScratchList<TR::Node> guardList(comp->trMemory());
   if (optionalIficmpge)
      {
      TR_CISCNode *limitCISCNode = optionalIficmpge->getChild(1);
      guardList.add(TR::Node::createif(TR::ificmple, convertStoreToLoad(comp, limitCISCNode->getHeadOfTrNode()),
                                      TR::Node::create(lengthNode, TR::iconst, 0, 65535)));
      }
   TR::Node* alignmentCheck = createTableAlignmentCheck(comp, tableNode, false, true, trans->isTableBackedByRawStorage());
   if (alignmentCheck)
      guardList.add(alignmentCheck);
   block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree(), &guardList);

   // Create the fast path code
   block = trans->insertBeforeNodes(block);
   block->append(TR::TreeTop::create(comp, topOfTranslateNode));
   block->append(indVarUpdateTreeTop);
   if (dstIndVarUpdateTreeTop) block->append(dstIndVarUpdateTreeTop);

   // Insert java/nio/Bits.keepAlive() calls into fastpath, if any.
   trans->insertBitsKeepAliveCalls(block);

   block = trans->insertAfterNodes(block);

   if (target)
      {
      // A single successor block
      trans->setSuccessorEdge(block, target);
      }
   else
      {
      // Multiple successor blocks
      // Generate the if-statement to jump to the correct destinations.
      TR::SymbolReference * translateTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
      TR::Node *tempStore;
      ifeqNode = ifeqNode->duplicateTree();
      tempStore = TR::Node::createStore(translateTemp, ifeqNode->getAndDecChild(0));
      ifeqNode->setAndIncChild(0, TR::Node::createLoad(ifeqNode, translateTemp));
      TR::TreeTop *tempStoreTTop = TR::TreeTop::create(comp, tempStore);
      TR::TreeTop *ifeqTTop = TR::TreeTop::create(comp, ifeqNode);
      // Fix up the IV value by adding 1 if translateNode == lengthNode (where no test char was found).  See comment above.
      TR::Node *incIndex = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthTRxx->getChild(1), indexRepNode);
      ///TR::Node *icmpeqNode = TR::Node::create(TR::icmpeq, 2, TR::Node::createLoad(indexNode, statusCheckTemp), TR::Node::iconst(indexNode, 0));
      ///TR::Node *incNode = TR::Node::create(TR::iadd, 2, TR::Node::createLoad(indexNode, indexVarSymRef), icmpeqNode);
      ///TR::Node *incIndex = TR::Node::createStore(indexVarSymRef, incNode);
      TR::TreeTop *incIndexTTop = TR::TreeTop::create(comp, incIndex);

      TR::TreeTop *last = block->getLastRealTreeTop();
      last->join(tempStoreTTop);
      tempStoreTTop->join(incIndexTTop);
      if (dstIndVarUpdateTreeTop)
         {
         TR::Node * incDstIndex = createStoreOP2(comp, dstIndexVarSymRef, TR::isub, dstIndexVarSymRef, -1, dstIndexRepNode);
         TR::TreeTop *incDstIndexTTop = TR::TreeTop::create(comp, incDstIndex);
         incIndexTTop->join(incDstIndexTTop);
         last = incDstIndexTTop;
         }
      else
         {
         last = incIndexTTop;
         }
      last->join(ifeqTTop);
      ifeqTTop->join(block->getExit());
      if (ifeqCiscNode->getOpcode() != ifeqNode->getOpCodeValue())
         {
         ifeqNode->setBranchDestination(ifeqCiscNode->getDestination());
         TR::Node::recreate(ifeqNode, (TR::ILOpCodes)ifeqCiscNode->getOpcode());
         }
      TR::Block *okDest = ifeqNode->getBranchDestination()->getEnclosingBlock();
      TR::Block *failDest = NULL;
      TR::Block *optionalDest = NULL;
      if (optionalIficmpge) optionalDest = optionalIficmpge->getDestination()->getEnclosingBlock();
      failDest = trans->searchOtherBlockInSuccBlocks(okDest, optionalDest);
      TR_ASSERT(failDest, "error");
      trans->setSuccessorEdges(block, failDest, okDest);
      }

   return true;
   }

bool
CISCTransform2TRTOArrayTableRaw(TR_CISCTransformer *trans)
   {
   trans->setTableBackedByRawStorage();
   return CISCTransform2TRTOArray(trans);
   }

/****************************************************************************************
Corresponding Java-like pseudocode

int i, j, end, exitValue;
char charArray[ ];
byte byteArray[ ], map[ ];
while(true){
   byte b = map[charArray[i]];
   if (b == exitValue) break;
   byteArray[j] = b;
   i++;
   j++;
   if (i >= end) break;
}

Note 1: Idiom allows variables i and j to be identical.
****************************************************************************************/
TR_PCISCGraph *
makeTRTOArrayGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRTOArray", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 16,   0,   0,    0);
   tgt->addNode(charArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(i); // src array index
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 14,   0,   0,    1);
   tgt->addNode(byteArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(j); // dst array index
   TR_PCISCNode *map        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    2);  tgt->addNode(map); // map array base
   TR_PCISCNode *idx0       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),9,   0,   0);  tgt->addNode(end);  // length
   TR_PCISCNode *exitValue  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(exitValue);  // exitvalue
   TR_PCISCNode *aHeader    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  7,   0,   0,    0);
   tgt->addNode(aHeader); // array header const
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -1);  tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  5,   0,   0);        tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 4, 2); // element size
   TR_PCISCNode *offset     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),3,   0,   0);  tgt->addNode(offset);      // optional
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0); tgt->addNode(entry);
   TR_PCISCNode *charAddr   = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,   entry, charArray, idx0, aHeader, elemSize);
   TR_PCISCNode *convNode   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), (ctrl & CISCUtilCtl_64Bit) ? TR::su2l : TR::su2i,
								(ctrl & CISCUtilCtl_64Bit) ? TR::Int64 : TR::Int32,
                                                tgt->incNumNodes(),  1,   1,   1,   charAddr, charAddr);  tgt->addNode(convNode);
   TR_PCISCNode *mapAddr    = createIdiomArrayLoadInLoop(tgt, ctrl|CISCUtilCtl_NoI2L, 1,   convNode, TR::bloadi, TR::Int8,  map, convNode, aHeader, mulFactor);
   TR_PCISCNode *b          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::b2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   mapAddr, mapAddr);  tgt->addNode(b);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpeq, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   b, b, exitValue);  tgt->addNode(exitTest);
   TR_PCISCNode *byteAddr   = createIdiomArrayStoreInLoop(tgt, ctrl, 1, exitTest, TR::bstorei, TR::Int8,  byteArray, idx1, aHeader, mulFactor, b);
   TR_PCISCNode *iStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);
   TR_PCISCNode *jStore     = createIdiomIncVarInLoop(tgt, ctrl, 1, iStore, j, i, offset);      // optional
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   jStore, i, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   jStore->getChild(0)->setIsOptionalNode();
   jStore->setIsOptionalNode();
   j->setIsOptionalNode();
   offset->setIsOptionalNode();

   convNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   tgt->setSpecialCareNode(0, exitTest); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, charAddr->getChild(0), byteAddr->getChild(0), NULL);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2TRTOArray);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_1|ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(2, 2, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setVersionLength(c->target().cpu.isPower() ? 0 : 11);   // depending on each architecture
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like pseudocode
int i, j, end, exitValue;
char charArray[ ];
byte byteArray[ ], *map;
while(true){
   int T = charArray[i];
   if (T >= limit) break;       // optional
   byte b = *(map + T);		- (1)
   if (b == exitValue) break;
   byteArray[j] = b;
   i++;
   j++;
   if (i >= end) break;
}

Note 1: Idiom allows variables i and j to be identical.
Note 2: This pattern is found in "sun/io/CharToByteSingleByte.JITintrinsicConvert".
        I don't know how we can write (1) in a Java program. From a log file, it seems
        that the map table is in java.nio.DirectByteBuffer and is treated as a pointer
        of C; the address (1) can be computed without adding the array header size.
****************************************************************************************/
TR_PCISCGraph *
makeTRTOArrayGraphSpecial(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "TRTOArraySpecial", 0, 16);
   /***************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *charArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 16,   0,   0,    0);
   tgt->addNode(charArray); // src array base
   TR_PCISCNode *i          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0); tgt->addNode(i); // src array index
   TR_PCISCNode *byteArray  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 14,   0,   0,    1);
   tgt->addNode(byteArray); // dst array base
   TR_PCISCNode *j          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1); tgt->addNode(j); // dst array index
   TR_PCISCNode *map        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    2); tgt->addNode(map); // map array base
   TR_PCISCNode *idx0       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *end        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),9,   0,   0);  tgt->addNode(end);      // length
   TR_PCISCNode *exitValue  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8,   0,   0);  tgt->addNode(exitValue); // exitvalue
   TR_PCISCNode *limit      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),7,   0,   0);  tgt->addNode(limit); // optional
   TR_PCISCNode *aHeader    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);
   tgt->addNode(aHeader);     // array header const
   TR_PCISCNode *increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   -1); tgt->addNode(increment);
   TR_PCISCNode *mulFactor  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  4,   0,   0);       tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *elemSize   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 2);   // element size
   TR_PCISCNode *entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);  tgt->addNode(entry);
   TR_PCISCNode *charAddr   = createIdiomCharArrayLoadInLoop(tgt, ctrl, 1,   entry, charArray, idx0, aHeader, elemSize);
   TR_PCISCNode *c2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,  tgt->incNumNodes(),  1,   1,   1,   charAddr, charAddr);  tgt->addNode(c2iNode);
   TR_PCISCNode *limitChk   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   c2iNode, c2iNode, limit);
   tgt->addNode(limitChk);        // optional
   TR_PCISCNode *mapAddr    = createIdiomByteDirectArrayLoadInLoop(tgt, ctrl, 1,   limitChk,  map, c2iNode);
   TR_PCISCNode *b2iNode    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::b2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   mapAddr, mapAddr);  tgt->addNode(b2iNode);
   TR_PCISCNode *exitTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpeq, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   b2iNode, b2iNode, exitValue);
   tgt->addNode(exitTest);
   TR_PCISCNode *byteAddr   = createIdiomArrayStoreInLoop(tgt, ctrl, 1, exitTest, TR::bstorei, TR::Int8,  byteArray, idx1, aHeader, mulFactor, b2iNode);
   TR_PCISCNode *iStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, byteAddr, i, increment);
   TR_PCISCNode *jStore     = createIdiomDecVarInLoop(tgt, ctrl, 1, iStore, j, increment);
   TR_PCISCNode *loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   jStore, i, end);  tgt->addNode(loopTest);
   TR_PCISCNode *exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(exit);

   exitTest->setSucc(1, exit);
   limitChk->setSucc(1, exit);
   loopTest->setSuccs(entry->getSucc(0), exit);

   c2iNode->setIsChildDirectlyConnected();
   loopTest->setIsChildDirectlyConnected();

   limit->setIsOptionalNode();
   limitChk->setIsOptionalNode();

   tgt->setSpecialCareNode(0, exitTest); // TR_booltable
   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(exitTest, loopTest, charAddr->getChild(0), byteAddr->getChild(0), limitChk);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(TRTSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2TRTOArrayTableRaw);
   tgt->setInhibitBeforeVersioning();
   tgt->setAspects(isub|mul, ILTypeProp::Size_1|ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(2, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setVersionLength(c->target().cpu.isPower() ? 0 : 11);   // depending on each architecture
   return tgt;
   }


enum StatusArrayStore
   {
   NO_NEED_TO_CHECK = 0,
   ABANDONING_REDUCTION = 1,
   GENERATE_ARRAY_ALIAS_TEST = 2,
   GENERATE_SUBRANGE_OVERLAP_TEST = 3,
   };

static StatusArrayStore checkArrayStore(TR::Compilation *comp, TR::Node *inputNode, TR::Node *outputNode, int elementSize, bool isForward)
   {
   // if the src and dest objects are the same, return
   // a) x86 arraycopy helpers dont handle byte copies well
   // (they use SSE so at least 4 bytes are copied at a time for perf)
   // b) this idiom cannot distinguish between loops of the type
   //    for (i=0; i<N; i++)
   //       a[l+i] = a[l-1]
   // and a typical arraycopy loop. loops like above cannot be reduced to an arraycopy
   // (look at the java semantics for arraycopy)
   //
   if (comp->target().cpu.isZ())
      return NO_NEED_TO_CHECK;       // On 390, MVC (which performs byte copies) is generated.

   if (inputNode->getFirstChild()->getSymbol()->getRegisterMappedSymbol() == outputNode->getFirstChild()->getSymbol()->getRegisterMappedSymbol())
      {
      static bool disableArraycopyOverlapTest = feGetEnv("TR_disableArraycopyOverlapTest") != NULL;
      if (!disableArraycopyOverlapTest &&
          comp->getOptLevel() >= hot &&
          performTransformation(comp, "%sNot abandoning reduction due to src == dest, generating element range overlap test\n", OPT_DETAILS))
         {
         dumpOptDetails(comp, "src and dest are the same, generating guard code 'if (src/dest subranges nonoverlapping)'\n");
         return GENERATE_SUBRANGE_OVERLAP_TEST;
         }
      dumpOptDetails(comp, "src and dest are the same, abandoning reduction\n");
      return ABANDONING_REDUCTION;
      }
   if (!inputNode->getFirstChild()->getOpCode().hasSymbolReference() ||
       !outputNode->getFirstChild()->getOpCode().hasSymbolReference())
      {
      dumpOptDetails(comp, "src and dest may be the same, generating guard code 'if (src != dst)'\n");
      return GENERATE_ARRAY_ALIAS_TEST;
      }

   //the only safe thing to do, in general, is to dynamically check the arrays
   return GENERATE_ARRAY_ALIAS_TEST;
   }

namespace
   {
   // Subrange overlap test: Arraycopy is incorrect for aliased arrays iff
   //    |d - s| < n,
   // where d is the destination offset, s is the source offset,
   // and n is the length (all in bytes).
   // (Note that it would be safe to relax this condition, and run the loop
   // instead of arraycopy more often.)
   class SubrangeOverlapTestGenerator
      {
      public:
      SubrangeOverlapTestGenerator(TR::Compilation *comp, TR::Node *arraycopy, TR::Node *byteLength, bool is64Bit, int elementSize);
      TR::Node *generate();

      private:
      TR::ILOpCodes ifxcmplt() { return _is64Bit ? TR::iflcmplt : TR::ificmplt; }
      TR::ILOpCodes xabs()     { return _is64Bit ? TR::labs     : TR::iabs;     }
      TR::ILOpCodes xconst()   { return _is64Bit ? TR::lconst   : TR::iconst;   }
      TR::ILOpCodes xmul()     { return _is64Bit ? TR::lmul     : TR::imul;     }
      TR::ILOpCodes xsub()     { return _is64Bit ? TR::lsub     : TR::isub;     }

      // probably better to teach simplifier how to do these
      void simplifyConstSub();
      void simplifyConstMul();
      void simplifyI2L();

      TR::Node *byteOffset(TR::Node *addr);
      void checkTypes();
      void checkType(const char *desc, TR::Node *node);

      TR::Compilation *_comp;
      bool _is64Bit;
      TR::Node *_dst;
      TR::Node *_src;
      TR::Node *_len;
      int _elementSize;
      };

   SubrangeOverlapTestGenerator::SubrangeOverlapTestGenerator(
      TR::Compilation *comp,
      TR::Node *arraycopy,
      TR::Node *byteLength,
      bool is64Bit,
      int elementSize)
      : _comp(comp)
      , _is64Bit(is64Bit)
      , _dst(byteOffset(arraycopy->getChild(1)))
      , _src(byteOffset(arraycopy->getChild(0)))
      , _len(byteLength)
      , _elementSize(elementSize)
      {
      checkTypes();
      simplifyConstSub();
      checkTypes();
      simplifyConstMul();
      checkTypes();
      simplifyI2L();
      checkTypes();
      }

   // Generate the test: if |d - s| < n
   TR::Node *SubrangeOverlapTestGenerator::generate()
      {
      _dst = _dst->duplicateTree();
      _src = _src->duplicateTree();
      _len = _len->duplicateTree();

      TR::Node *diff = TR::Node::create(xsub(), 2, _dst, _src);
      TR::Node *separation = TR::Node::create(xabs(), 1, diff);

      return TR::Node::createif(ifxcmplt(), separation, _len);
      }

   // For all k, TFAE:
   // 1.  |(d - k) - (s - k)| < n
   // 2.  |d - s| < n
   void SubrangeOverlapTestGenerator::simplifyConstSub()
      {
      static bool disableArraycopyOverlapTestSubSimplification =
         feGetEnv("TR_disableArraycopyOverlapTestSubSimplification") != NULL;
      if (disableArraycopyOverlapTestSubSimplification)
         return;

      // Check that both are sub.
      if (_dst->getOpCodeValue() != xsub())
         return;
      if (_src->getOpCodeValue() != xsub())
         return;

      // Check that both subtrahends are constant.
      TR::Node *dstSubtrahend = _dst->getChild(1);
      TR::Node *srcSubtrahend = _src->getChild(1);
      if (dstSubtrahend->getOpCodeValue() != xconst())
         return;
      if (srcSubtrahend->getOpCodeValue() != xconst())
         return;

      // Check that the constants are equal.
      if (dstSubtrahend->getConstValue() != srcSubtrahend->getConstValue())
         return;

      // Ask permission to transform.
      if (!performTransformation(_comp, "%sSimplifying arraycopy element range overlap test by looking through sub const\n", OPT_DETAILS))
         return;

      // Transform.
      _dst = _dst->getChild(0);
      _src = _src->getChild(0);
      }

   // For all k > 0,
   // if dk, sk, nk don't overflow,
   // and if dk, sk >= 0, then TFAE:
   // 1.  |dk - sk| < nk
   // 2.  |d - s| < n.
   void SubrangeOverlapTestGenerator::simplifyConstMul()
      {
      static bool disableArraycopyOverlapTestMulSimplification =
         feGetEnv("TR_disableArraycopyOverlapTestMulSimplification") != NULL;
      if (disableArraycopyOverlapTestMulSimplification)
         return;

      // Check that all three are mul.
      if (_dst->getOpCodeValue() != xmul())
         return;
      if (_src->getOpCodeValue() != xmul())
         return;
      if (_len->getOpCodeValue() != xmul())
         return;

      // Check that all three multiplicands are constant.
      TR::Node *dstMultiplicand = _dst->getChild(1);
      TR::Node *srcMultiplicand = _src->getChild(1);
      TR::Node *lenMultiplicand = _len->getChild(1);
      if (dstMultiplicand->getOpCodeValue() != xconst())
         return;
      if (srcMultiplicand->getOpCodeValue() != xconst())
         return;
      if (lenMultiplicand->getOpCodeValue() != xconst())
         return;

      // Check that all constants are equal, and positive.
      int64_t k = dstMultiplicand->getConstValue();
      if (k <= 0)
         return;
      if (srcMultiplicand->getConstValue() != k)
         return;
      if (lenMultiplicand->getConstValue() != k)
         return;

      // Check that the multiplications don't overflow.
      if (!_dst->cannotOverflow() || !_src->cannotOverflow())
         return;
      // NB. when _elementSize > 1, _len is expected to start as a
      // newly-created multiply by _elementSize. It won't yet be marked
      // as cannotOverflow, but overflow would mean serious trouble.
      if (!_len->cannotOverflow() && !(k == _elementSize && k > 1))
         return;

      // Check that src, dst >= 0.
      if (!_dst->isNonNegative() || !_src->isNonNegative())
         return;

      // Ask permission to transform.
      if (!performTransformation(_comp, "%sSimplifying arraycopy element range overlap test by looking through mul const\n", OPT_DETAILS))
         return;

      // Transform.
      _dst = _dst->getChild(0);
      _src = _src->getChild(0);
      _len = _len->getChild(0);
      _elementSize = 1;
      }

   // When d, s >= 0, TFAE:
   // 1.  |i2l(d) - i2l(s)| < i2l(n)
   // 2.  |d - s| < n
   void SubrangeOverlapTestGenerator::simplifyI2L()
      {
      static bool disableArraycopyOverlapTestI2LSimplification =
         feGetEnv("TR_disableArraycopyOverlapTestI2LSimplification") != NULL;
      if (disableArraycopyOverlapTestI2LSimplification)
         return;

      // Check that we are operating on 64-bit numbers,
      // and that all three are i2l.
      if (!_is64Bit)
         return;
      if (_dst->getOpCodeValue() != TR::i2l)
         return;
      if (_src->getOpCodeValue() != TR::i2l)
         return;
      if (_len->getOpCodeValue() != TR::i2l)
         return;

      // Check that dst, src >= 0.
      // Note that x and i2l(x) have identical signs,
      // so it's good enough if the child is >= 0.
      if (!_dst->isNonNegative() && !_dst->getFirstChild()->isNonNegative())
         return;
      if (!_src->isNonNegative() && !_src->getFirstChild()->isNonNegative())
         return;

      // Ask permission to transform.
      if (!performTransformation(_comp, "%sSimplifying arraycopy element range overlap test by looking through i2l\n", OPT_DETAILS))
         return;

      // Transform
      _dst = _dst->getFirstChild();
      _src = _src->getFirstChild();
      _len = _len->getFirstChild();
      _is64Bit = false;
      }

   // Get the byte offset from an array element address calculation.
   TR::Node *SubrangeOverlapTestGenerator::byteOffset(TR::Node *addr)
      {
      TR::ILOpCodes op = addr->getOpCodeValue();
      TR_ASSERT(op == TR::aladd || op == TR::aiadd,
         "unexpected arraycopy child opcode %s",
         addr->getOpCode().getName());
      return addr->getChild(1);
      }

   // Assert that all nodes involved in the test have the expected data type.
   void SubrangeOverlapTestGenerator::checkTypes()
      {
      checkType("destination index", _dst);
      checkType("source index", _src);
      checkType("length", _len);
      }

   // Assert that a single node has the expected data type.
   void SubrangeOverlapTestGenerator::checkType(const char *desc, TR::Node *node)
      {
      TR::DataType expectedType = _is64Bit ? TR::Int64 : TR::Int32;
      TR::DataType actualType = node->getDataType();
      TR_ASSERT(
         actualType == expectedType,
         "expected %s node to have type %s, but found %s (%d)",
         desc,
         TR::DataType::getName(expectedType),
         TR::DataType::getName(actualType),
         (int)actualType);
      }
   }

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory
// Input: ImportantNode(0) - array load
//        ImportantNode(1) - array store
//        ImportantNode(2) - the size of elements (NULL for the byte array)
//        ImportantNode(3) - exit if node
//        ImportantNode(4) - optional iistore
//*****************************************************************************************
static bool
CISCTransform2ArrayCopySub(TR_CISCTransformer *trans, TR::Node *indexRepNode, TR::Node *dstIndexRepNode,
                           TR::Node *exitVarRepNode, TR::Node *variableORconstRepNode)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool isDecrement = trans->isMEMCPYDec();
   const bool disptrace = DISPTRACE(trans);

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ARrayCopySub due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR_CISCNode * inLoadCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(0));
   TR_CISCNode * inStoreCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(1));
   if (!inLoadCISCNode || !inStoreCISCNode)
      {
      if (DISPTRACE(trans)) traceMsg(comp, "CISCTransform2ArrayCopy failed. inLoadCISCNode %x inStoreCISCNode %x\n",inLoadCISCNode,inStoreCISCNode);
      return false;
      }

   // The transformation can support one exit if-stmt
   TR_CISCGraph *T = trans->getT();
   if (T && T->getAspects()->getIfCount() > 1)
      {
      traceMsg(comp,"CISCTransform2ArrayCopySub detected %d if-stmts in loop (> 1).  Not transforming.\n", T->getAspects()->getIfCount());
      return false;
      }

   TR::Node * inLoadNode = inLoadCISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inStoreNode = inStoreCISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * mulFactorNode;
   int elementSize;

   TR::Node * inputNode = inLoadNode->getChild(0)->duplicateTree();
   TR::Node * outputNode = inStoreNode->getChild(0)->duplicateTree();

   // Get the size of elements
   if (!getMultiplier(trans, P->getImportantNode(2), &mulFactorNode, &elementSize, inLoadNode->getType()))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "CISCTransform2ArrayCopy getMultiplier failed.\n");
      return false;
      }
   if (elementSize != inLoadNode->getSize() || elementSize != inStoreNode->getSize())
      {
      traceMsg(comp, "CISCTransform2ArrayCopy failed - Size Mismatch.  Element Size: %d InLoadSize: %d inStoreSize: %d\n", elementSize, inLoadNode->getSize(), inStoreNode->getSize());
      return false;     // Size is mismatch!
      }

   // if the src and dest objects are the same, return
   //
   StatusArrayStore statusArrayStore;
   if ((statusArrayStore = checkArrayStore(comp, inputNode, outputNode, elementSize, !isDecrement)) == ABANDONING_REDUCTION)
      return false;

   if (indexContainsArrayAccess(comp, inLoadNode->getFirstChild()) ||
         indexContainsArrayAccess(comp, inStoreNode->getFirstChild()))
      {
      traceMsg(comp, "inputNode %p or outputnode %p contains another arrayaccess, no reduction\n", inLoadNode, inStoreNode);
      return false;
      }

   TR_CISCNode *cmpIfAllCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3));
   int modStartIdx = 0;
   int modLength = 0;
   bool isDecrementRet;
   if (!testExitIF(cmpIfAllCISCNode->getOpcode(), &isDecrementRet, &modLength, &modStartIdx))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "CISCTransform2ArrayCopy testExitIF failed.\n");
      return false;
      }
   if (isDecrement != isDecrementRet) return false;


   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   if (!trans->analyzeArrayIndex(indexVarSymRef))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "analyzeArrayIndex failed. %x\n",indexRepNode);
      return false;
      }
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode ? dstIndexRepNode->getSymbolReference() : NULL;
   if (indexVarSymRef == dstIndexVarSymRef) dstIndexVarSymRef = NULL;
   indexRepNode = convertStoreToLoad(comp, indexRepNode);
   if (!trans->searchNodeInTrees(inputNode, indexRepNode))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "searchNodeInTrees for inputNode failed.\n");
      return false;
      }
   if (!trans->searchNodeInTrees(outputNode, dstIndexVarSymRef ? convertStoreToLoad(comp, dstIndexRepNode) : indexRepNode))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "searchNodeInTrees for outputNode failed.\n");
      return false;
      }
   TR::SymbolReference * exitVarSymRef = exitVarRepNode->getSymbolReference();
   if (indexVarSymRef != exitVarSymRef && dstIndexVarSymRef != exitVarSymRef)
      {
      if (DISPTRACE(trans)) traceMsg(comp, "CISCTransform2ArrayCopy cannot find exitVarSymRef correctly %x.\n", exitVarRepNode);
      return false;
      }

   TR::Node *optionalIistore = NULL;
   if (P->getImportantNode(4))
      {
      TR_CISCNode *optionalCISCIistore = trans->getP2TInLoopIfSingle(P->getImportantNode(4));
      if (!optionalCISCIistore)
         return false;
      optionalIistore = optionalCISCIistore->getHeadOfTrNode()->duplicateTree();
      }

   TR::Node * exitVarNode = createLoad(exitVarRepNode);
   variableORconstRepNode = convertStoreToLoad(comp, variableORconstRepNode);


   int32_t postIncrement = checkForPostIncrement(comp, block, cmpIfAllCISCNode->getHeadOfTrNodeInfo()->_node, exitVarSymRef->getSymbol());

   if (disptrace)
      traceMsg(comp, "detected postIncrement %d modLength %d modStartIdx %d\n", postIncrement, modLength, modStartIdx);

   TR::Node * lengthNode;
   if (isDecrement)
      {
      TR_ASSERT(dstIndexVarSymRef == NULL, "not implemented yet");
      TR_CISCNode *ixloadORstore, *aload, *iload;
      if (postIncrement &&
            (modStartIdx > 0))
         modStartIdx = 0;
      TR::Node *startIdx = modStartIdx ? createOP2(comp, TR::isub, variableORconstRepNode,
                                                  TR::Node::create(trNode, TR::iconst, 0, -modStartIdx)) :
                                        variableORconstRepNode;
      if (!getThreeNodesForArray(inLoadCISCNode, &ixloadORstore, &aload, &iload)) return false;
      if ((inputNode = replaceIndexInAddressTree(comp, ixloadORstore->getChild(0)->getHeadOfTrNodeInfo()->_node->duplicateTree(),
                                                 indexVarSymRef, startIdx)) == NULL) return false;
      if (!getThreeNodesForArray(inStoreCISCNode, &ixloadORstore, &aload, &iload)) return false;
      if ((outputNode = replaceIndexInAddressTree(comp, ixloadORstore->getChild(0)->getHeadOfTrNodeInfo()->_node->duplicateTree(),
                                                  dstIndexVarSymRef ? dstIndexVarSymRef : indexVarSymRef, startIdx)) == NULL) return false;
      lengthNode = createOP2(comp, TR::isub, exitVarNode, variableORconstRepNode);
      }
   else
      {
      TR_ASSERT(modStartIdx == 0, "error");
      inputNode = inputNode->duplicateTree();
      outputNode = outputNode->duplicateTree();
      lengthNode = createOP2(comp, TR::isub, variableORconstRepNode, exitVarNode);
      }

   if (postIncrement != 0)
      lengthNode = createOP2(comp, TR::iadd, lengthNode, TR::Node::create(lengthNode, TR::iconst, 0, postIncrement));

   if (modLength) lengthNode = createOP2(comp, TR::isub, lengthNode, TR::Node::create(trNode, TR::iconst, 0, -modLength));
   TR::Node * diff = lengthNode;

   lengthNode = createBytesFromElement(comp, trans->isGenerateI2L(), lengthNode, elementSize);

   // Prepare the arraycopy node.
   bool needWriteBarrier = false;
   if (inStoreNode->getOpCodeValue() == TR::awrtbari)
      {
      switch (TR::Compiler->om.writeBarrierType())
         {
         case gc_modron_wrtbar_oldcheck:
         case gc_modron_wrtbar_cardmark:
         case gc_modron_wrtbar_cardmark_and_oldcheck:
         case gc_modron_wrtbar_cardmark_incremental:
            needWriteBarrier = true;
            break;
         default:
            break;
         }
      }

   if (!comp->cg()->getSupportsReferenceArrayCopy() && needWriteBarrier)
      {
      if (DISPTRACE(trans)) traceMsg(comp, "CISCTransform2ArrayCopy: needWriteBarrier but not getSupportsReferenceArrayCopy().\n");
      return false;
      }

   TR::Node * arraycopy;
   if (!needWriteBarrier)
      {
      arraycopy = TR::Node::createArraycopy(inputNode, outputNode, lengthNode);
      }
   else
      {
      arraycopy = TR::Node::createArraycopy(inputNode->getFirstChild(), outputNode->getFirstChild(), inputNode, outputNode, lengthNode);
      }
   arraycopy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   if (isDecrement)
      {
      arraycopy->setBackwardArrayCopy(true); /* bit available only to primitive arraycopy */
      }
   else
      {
      arraycopy->setForwardArrayCopy(true);
      }
   arraycopy->setArrayCopyElementType(inStoreNode->getDataType());

   switch(elementSize)
      {
      case 2:
         arraycopy->setHalfWordElementArrayCopy(true);
         break;

      case 4:
      case 8:
         arraycopy->setWordElementArrayCopy(true);
         break;
      }

   TR::Node * topArraycopy = TR::Node::create(TR::treetop, 1, arraycopy);

   // Insert nodes and maintain the CFG
   if (statusArrayStore == GENERATE_ARRAY_ALIAS_TEST)
      {
      // devinmp: Should this also check the index ranges?
      List<TR::Node> guardList(comp->trMemory());
      guardList.add(TR::Node::createif(TR::ifacmpeq, inputNode->getFirstChild()->duplicateTree(),
                                      outputNode->getFirstChild()->duplicateTree()));
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree(), &guardList);
      }
   else if (statusArrayStore == GENERATE_SUBRANGE_OVERLAP_TEST)
      {
      // We know that the arrays alias, so only test the index ranges.
      bool is64Bit = trans->isGenerateI2L();
      SubrangeOverlapTestGenerator overlapTestGen(comp, arraycopy, lengthNode, is64Bit, elementSize);
      List<TR::Node> guardList(comp->trMemory());
      guardList.add(overlapTestGen.generate());
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree(), &guardList);
      }
   else
      {
      TR_ASSERT(statusArrayStore == NO_NEED_TO_CHECK, "unexpected statusArrayStore value %d", (int)statusArrayStore);
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree());
      }
   block = trans->insertBeforeNodes(block);
   block->append(TR::TreeTop::create(comp, topArraycopy));
   TR::Node * finalValue = variableORconstRepNode;
   if (modLength || (postIncrement != 0))
      {
      int32_t incr = 0;
      if (modLength)
         incr = modLength;
      else if (postIncrement != 0)
         incr = postIncrement;

      finalValue =  createOP2(comp, TR::isub, finalValue,
                                    TR::Node::create(trNode, TR::iconst, 0, isDecrement ? incr : -incr));
      }
   TR::TreeTop * exitVarUpdateTreeTop = TR::TreeTop::create(comp,
          TR::Node::createStore(exitVarSymRef, finalValue));

   block->append(exitVarUpdateTreeTop);
   TR_ASSERT(indexVarSymRef == exitVarSymRef || dstIndexVarSymRef == exitVarSymRef, "error!");
   TR::Node * theOtherVarUpdateNode = NULL;
   if (dstIndexVarSymRef != NULL)
      {
      // If there are two induction variables, we need to maintain the other one.
      TR::SymbolReference * theOtherSymRef = (indexVarSymRef == exitVarSymRef ? dstIndexVarSymRef : indexVarSymRef);
      TR::Node * result = createOP2(comp, isDecrement ? TR::isub : TR::iadd,
                                   TR::Node::createLoad(trNode, theOtherSymRef),
                                   diff);
      theOtherVarUpdateNode = TR::Node::createStore(theOtherSymRef, result);
      TR::TreeTop * theOtherVarUpdateTreeTop = TR::TreeTop::create(comp, theOtherVarUpdateNode);
      block->append(theOtherVarUpdateTreeTop);
      }

   if (optionalIistore)
      {
      TR_ASSERT(theOtherVarUpdateNode != NULL, "error!");
      optionalIistore->setAndIncChild(1, theOtherVarUpdateNode->getChild(0));
      block->append(TR::TreeTop::create(comp, optionalIistore));
      }

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

bool
CISCTransform2ArrayCopy(TR_CISCTransformer *trans)
   {
   TR::Node *indexRepNode, *dstIndexRepNode, *exitVarRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &indexRepNode, &dstIndexRepNode, &exitVarRepNode, &variableORconstRepNode);
   return CISCTransform2ArrayCopySub(trans, indexRepNode, dstIndexRepNode, exitVarRepNode, variableORconstRepNode);
   }

bool
CISCTransform2ArrayCopySpecial(TR_CISCTransformer *trans)
   {
   TR::Node *indexRepNode, *dstIndexRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &indexRepNode, &dstIndexRepNode, &variableORconstRepNode);
   return CISCTransform2ArrayCopySub(trans, indexRepNode, dstIndexRepNode, indexRepNode, variableORconstRepNode);
   }

bool
CISCTransform2ArrayCopyDec(TR_CISCTransformer *trans)
   {
   trans->setMEMCPYDec();
   return CISCTransform2ArrayCopy(trans);
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v3, end;
byte v0[ ], v2[ ];
while(true){
   v2[v3] = v0[v1];
   v1++;
   v3++;
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeMemCpySpecialGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpySpecial", 0, 16);
   /********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),12,   0,   0);  tgt->addNode(vorc);      // length

   TR_PCISCNode *v5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 11,   0,   0,    2);  tgt->addNode(v5); // dst array index
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 9,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *mulFactor = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  3,   0,   0);  tgt->addNode(mulFactor);    // Multiply Factor
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ent, v3, cm1);
   n6->getChild(0)->setIsSuccDirectlyConnected(false);
   TR_PCISCNode *iis = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istorei, TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   n6, v5, n6->getChild(0)); tgt->addNode(iis);
   TR_PCISCNode *n0  = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, iis, idx1, cmah1, mulFactor);
   TR_PCISCNode *n1  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, n0, v2, n0);
   TR_PCISCNode *n2  = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, n1, idx0, cmah0, mulFactor);
   TR_PCISCNode *n3  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, n2, v0, n2);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indload, TR::NoType,    tgt->incNumNodes(),  1,   1,   1,   n3, n3); tgt->addNode(n4);
   TR_PCISCNode *nn1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore, TR::NoType,   tgt->incNumNodes(),  1,   1,   2,   n4, n1, n4); tgt->addNode(nn1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, nn1, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v1, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);

   n4->setIsChildDirectlyConnected();
   nn1->setIsChildDirectlyConnected();
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(n4, nn1, NULL, n8, iis);
   tgt->setNumDagIds(15);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopySpecial);
   tgt->setAspects(isub|sameTypeLoadStore, existAccess, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v3, end;
 v0[ ], v2[ ];	// char, int, float, long, and so on
while(true){
   v2[v3] = v0[v1];
   v1++;
   v3++;
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeMemCpyGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpy", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    2);  tgt->addNode(v4); // exit checking
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),11,   0,   0);  tgt->addNode(vorc);      // length

   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 9,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(), 6,   0,   0);        tgt->addNode(iall);        // Multiply Factor
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  4,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n1  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ent, v2, idx1, cmah1, iall);
   TR_PCISCNode *n3  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, n1, v0, idx0, cmah0, iall);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indload, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,   n3, n3); tgt->addNode(n4);
   TR_PCISCNode *nn1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore, TR::NoType,  tgt->incNumNodes(),  1,   1,   2,   n4, n1, n4); tgt->addNode(nn1);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, nn1, v3, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v4, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);

   n4->setIsChildDirectlyConnected();
   nn1->setIsChildDirectlyConnected();
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(n4, nn1, iall, n8, NULL);
   tgt->setNumDagIds(15);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopy);
   tgt->setAspects(isub|mul | sameTypeLoadStore, existAccess, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, true);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v3, end;
 v0[ ], v2[ ];	// char, int, float, long, and so on
while(true){
   v2[v1] = v0[v1];
   v1--;
   if (v1 <= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeMemCpyDecGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpyDec", 0, 16);
   /*********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    2);  tgt->addNode(v4); // exit checking
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),11,   0,   0);  tgt->addNode(vorc);      // length

   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 9,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(), 6,   0,   0);        tgt->addNode(iall);        // Multiply Factor
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  4,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n1  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ent, v2, idx1, cmah1, iall);
   TR_PCISCNode *n3  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, n1, v0, idx0, cmah0, iall);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indload, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,   n3, n3); tgt->addNode(n4);
   TR_PCISCNode *nn1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore, TR::NoType,  tgt->incNumNodes(),  1,   1,   2,   n4, n1, n4); tgt->addNode(nn1);
   TR_PCISCNode *n6  = createIdiomIncVarInLoop(tgt, ctrl, 1, nn1, v3, cm1);
   TR_PCISCNode *n7  = createIdiomIncVarInLoop(tgt, ctrl, 1, n6, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v4, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);

   n4->setIsChildDirectlyConnected();
   nn1->setIsChildDirectlyConnected();
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(n4, nn1, iall, n8, NULL);
   tgt->setNumDagIds(15);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopyDec);
   tgt->setAspects(iadd|mul | sameTypeLoadStore, existAccess, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory (ByteToChar or CharToByte version)
// Input: ImportantNodes(0) - array load
//        ImportantNodes(1) - array store
//*****************************************************************************************
bool
CISCTransform2ArrayCopyB2CorC2B(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   bool bigEndian = comp->target().cpu.isBigEndian();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCopyB2CorC2B due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR::Node *indexRepNode, *dstIndexRepNode, *exitVarRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &indexRepNode, &dstIndexRepNode, &exitVarRepNode, &variableORconstRepNode);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode->getSymbolReference();
   TR::SymbolReference * exitVarSymRef = exitVarRepNode->getSymbolReference();
   TR_ASSERT(indexVarSymRef == exitVarSymRef || dstIndexVarSymRef == exitVarSymRef, "error!");
   TR_ASSERT(indexVarSymRef != dstIndexVarSymRef, "error!");

   TR::Node * inputMemNode = trans->getP2TRepInLoop(P->getImportantNode(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputMemNode = trans->getP2TRepInLoop(P->getImportantNode(1))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * inputNode = trans->getP2TRepInLoop(P->getImportantNode(0)->getChild(0))->getHeadOfTrNodeInfo()->_node;
   TR::Node * outputNode = trans->getP2TRepInLoop(P->getImportantNode(1)->getChild(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();

   // check whether the transformation is valid
   //
   if (outputMemNode->getOpCode().isShort() && outputMemNode->getOpCode().isUnsigned())
      {
      TR::Node * iorNode = trans->getP2TRepInLoop(P->getImportantNode(2))->getHeadOfTrNodeInfo()->_node;
      if (!checkByteToChar(comp, iorNode, inputNode, bigEndian))
         {
         dumpOptDetails(comp, "byte loads in [%p] are not compatible with endian-ness %d\n", iorNode, bigEndian);
         return false;
         }
      }
   inputNode = inputNode->duplicateTree();

   TR::Node * exitVarNode = createLoad(exitVarRepNode);
   variableORconstRepNode = convertStoreToLoad(comp, variableORconstRepNode);
   TR::Node * lengthNode = createOP2(comp, TR::isub,
                                    variableORconstRepNode,
                                    exitVarNode);
   TR::Node * updateTree1, *updateTree2;
   TR::Node * c2 = TR::Node::create(exitVarRepNode, TR::iconst, 0, 2);
   bool isExitVarChar;
   isExitVarChar = (dstIndexVarSymRef == exitVarSymRef) ? outputMemNode->getSize() == 2 :
                                                          inputMemNode->getSize() == 2;
   //there are 2 scenarios
   // dstIndexVarSymRef is a char (ie. outputMemNode size == 2, consequently inputMemNode == 1 and indexVarSymRef is a byte)
   // or
   // indexVarSymRef is a char (ie. inputMemNode size == 2, consequently outputMemNode == 1 and dstIndexVarSymRef is a byte)
   // in each of these cases, its possible that each induction variable could be the loop controlling variable (ie. exitVarSymRef) ; thereby creating 4 possible conditions
   //
   if (outputMemNode->getSize() == 2) // type is a byteToChar copy
      {
      // for a byteToChar copy, the length needs to be expressed in number of bytes
      if (dstIndexVarSymRef == exitVarSymRef)
         {
         // dstIndex is the char array's index and length should be multiplied by 2 because the
         // arraycopy length should be expressed in bytes
         //
         lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c2);
         updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthNode, trNode);
         updateTree2 = TR::Node::createStore(dstIndexVarSymRef, variableORconstRepNode);
         }
      else
         {
         // byte array's index is the loop controlling variable. this means length is already in bytes
         // nothing to do for length
         updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthNode, trNode);
         updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef,
                                      TR::Node::create(TR::idiv, 2, lengthNode, c2), trNode);
         }
      }
   else // type is a charToByte copy
      {
      // For a charToByte copy, the length needs to be expressed in number of bytes
      if (dstIndexVarSymRef == exitVarSymRef)
         {
         // dstIndex is the byte array's index and length is already in bytes.
         // index is the char array's index and needs to be adjusted by # of chars (byte / 2).
         updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, TR::Node::create(TR::idiv, 2, lengthNode, c2), trNode);
         updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, lengthNode, trNode);
         }
      else
         {
         // char array's index is the loop controlling variable, so length needs to be adjusted by 2.
         // index is the char array's index and should be added to original length value.
         // dstIndex is the byte array's index and needs to be added to 2 * length (or updated lengthNode).
         updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthNode, trNode);
         lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c2);
         updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, lengthNode, trNode);
         }
      }

#if 0
   // Prepare nodes for byte length and induction variable updates
   if (isExitVarChar)   // The variable that checks the exit condition is for a 2-byte array.
      {
      TR::Node * diff = lengthNode;
      lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c2);
      // lengthNode has the byte size, and diff has the char-based size (that is, lengthNode = diff * 2)
      updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthNode, trNode);
      updateTree2 = TR::Node::createStore(dstIndexVarSymRef, variableORconstRepNode);
      }
   else
      {
      ///TR::Node * div2 = TR::Node::create(TR::idiv, 2, lengthNode, c2);
      lengthNode = TR::Node::create(TR::idiv, 2, lengthNode, c2);
      ///lengthNode = TR::Node::create(TR::imul, 2, div2, c2); // to make the length even
      // lengthNode has the byte size, and div2 has the char-based size (that is, lengthNode = div2 * 2)
      updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthNode, trNode);
      updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef,
                                   TR::Node::create(TR::imul, 2, lengthNode, c2), trNode);
      }
#endif

   lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode);

   // Prepare the arraycopy node
   TR::Node * arraycopy = TR::Node::createArraycopy(inputNode, outputNode, lengthNode);
   arraycopy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   arraycopy->setForwardArrayCopy(true);
   arraycopy->setArrayCopyElementType(TR::Int8);

   TR::Node * topArraycopy = TR::Node::create(TR::treetop, 1, arraycopy);
   TR::TreeTop * updateTreeTop1 = TR::TreeTop::create(comp, updateTree1);
   TR::TreeTop * updateTreeTop2 = TR::TreeTop::create(comp, updateTree2);

   // Insert nodes and maintain the CFG
   TR::TreeTop *last;
   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);
   last = block->getLastRealTreeTop();
   last->join(trTreeTop);
   trTreeTop->setNode(topArraycopy);
   trTreeTop->join(updateTreeTop1);
   updateTreeTop1->join(updateTreeTop2);
   updateTreeTop2->join(block->getExit());

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program (for big endian)
int v1, v3, end;
byte v0[ ];
char v2[ ];
while(true){
   v2[v3] = ((v0[v1] & 0xFF) << 8) | (v0[v1+1] & 0xFF))
   v1+=2;
   v3++;
   if (v3 >= end) break;
}

Note 1: This idiom also supports little endian.
****************************************************************************************/
TR_PCISCGraph *
makeMemCpyByteToCharGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpyByteToChar", 0, 16);
   bool isBigEndian = (ctrl & CISCUtilCtl_BigEndian);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    2);  tgt->addNode(v4); // exit checking
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),12,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 8, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *ah1 = isBigEndian ? cmah  : cmah1;
   TR_PCISCNode *ah2 = isBigEndian ? cmah1 : cmah;
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -2);  tgt->addNode(cm2);
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 2);                    // element size
   TR_PCISCNode *c256= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  256);  tgt->addNode(c256);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *ns0 = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, ent, v3, cmah, c2);
   TR_PCISCNode *ns1 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns0, v2, ns0);
   TR_PCISCNode *nl00;
   TR_PCISCNode *nl10;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      nl00= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2l, TR::Int64,       tgt->incNumNodes(),  1,   1,   1,   ns1, v1); tgt->addNode(nl00);
      nl10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, nl00, nl00, ah1, c1);
      }
   else
      {
      nl00= v1;
      nl10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, ns1, nl00, ah1, c1);
      }
   TR_PCISCNode *nl11= createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl10, v0, nl10);
   TR_PCISCNode *nl12= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bloadi, TR::Int8,    tgt->incNumNodes(),  1,   1,   1,   nl11, nl11); tgt->addNode(nl12);
   TR_PCISCNode *nl13= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl12, nl12); tgt->addNode(nl13);
   TR_PCISCNode *nl14= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl13, nl13, c256); tgt->addNode(nl14);
   TR_PCISCNode *nl20= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, nl14, nl00, ah2, c1);
   TR_PCISCNode *nl21= createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl20, v0, nl20);
   TR_PCISCNode *nl22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bloadi, TR::Int8,    tgt->incNumNodes(),  1,   1,   1,   nl21, nl21); tgt->addNode(nl22);
   TR_PCISCNode *nl23= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl22, nl22); tgt->addNode(nl23);
   TR_PCISCNode *ns2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ior, TR::Int32,       tgt->incNumNodes(),  1,   1,   2,   nl23, nl14, nl23); tgt->addNode(ns2);
   TR_PCISCNode *ns3 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2s, TR::Int16,       tgt->incNumNodes(),  1,   1,   1,   ns2, ns2); tgt->addNode(ns3);
   TR_PCISCNode *ns4 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::sstorei, TR::Int16,   tgt->incNumNodes(),  1,   1,   2,   ns3, ns1, ns3); tgt->addNode(ns4);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns4, v1, cm2);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v3, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v4, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(isBigEndian ? nl12 : nl22, ns4, ns2);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopyB2CorC2B);
   tgt->setAspects(isub|mul|bitop1, ILTypeProp::Size_1, ILTypeProp::Size_2);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 2, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


/****************************************************************************************
Corresponding Java-like Pseudo Program (for big endian)
int v1, v3, end;
char v0[ ];
byte v2[ ];
while(true){
   v2[v3]   = (byte)(v0[v1] >> 8);
   v2[v3+1] = (byte)(v0[v1] & 0xff);
   v1++;
   v3+=2;
   if (v1 >= end) break;
}

Note 1: This idiom also supports little endian.
****************************************************************************************/
TR_PCISCGraph *
makeMemCpyCharToByteGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpyCharToByte", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    2);  tgt->addNode(v4); // exit checking
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),12,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 8, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -2);  tgt->addNode(cm2);
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 2);                    // element size
   TR_PCISCNode *c8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  8);  tgt->addNode(c8);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *ns10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, ent, v3, cmah, c1);
   TR_PCISCNode *ns11= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns10, v2, ns10);
   TR_PCISCNode *nl0 = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, ns11, v1, cmah, c2);
   TR_PCISCNode *nl1 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl0, v0, nl0);
   TR_PCISCNode *nl2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::sloadi, TR::Int16,    tgt->incNumNodes(),  1,   1,   1,   nl1, nl1); tgt->addNode(nl2);
   TR_PCISCNode *cvt0, *cvt1;
   if ((ctrl & CISCUtilCtl_BigEndian))
      {
      TR_PCISCNode *nc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   nl2, nl2); tgt->addNode(nc2i);
      TR_PCISCNode *ns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType, tgt->incNumNodes(), 1,   1,   2,   nc2i, nc2i, c8); tgt->addNode(ns22);
      cvt0=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns22, ns22); tgt->addNode(cvt0);
      }
   else
      {
      cvt0=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   nl2, nl2); tgt->addNode(cvt0);
      }
   TR_PCISCNode *ns14= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   cvt0, ns11, cvt0); tgt->addNode(ns14);
   TR_PCISCNode *ns20= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl|CISCUtilCtl_NoI2L, 1, ns14, ns10->getChild(0)->getChild(0), cmah1, c1);
   TR_PCISCNode *ns21= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns20, v2, ns20);
   if ((ctrl & CISCUtilCtl_BigEndian))
      {
      cvt1=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns21, nl2); tgt->addNode(cvt1);
      }
   else
      {
      TR_PCISCNode *nc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   ns21, nl2); tgt->addNode(nc2i);
      TR_PCISCNode *ns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType, tgt->incNumNodes(), 1,   1,   2,   nc2i, nc2i, c8); tgt->addNode(ns22);
      cvt1=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns22, ns22); tgt->addNode(cvt1);
      }
   TR_PCISCNode *ns24= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   cvt1, ns21, cvt1); tgt->addNode(ns24);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns24, v3, cm2);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v4, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);

   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(nl2, ns14);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialCareNode(0, cvt0); // conversion (possibly i2b)
   tgt->setSpecialCareNode(1, cvt1); // conversion (possibly i2b)
   tgt->setSpecialNodeTransformer(MEMCPYSpecialNodeTransformer);

   tgt->setTransformer(CISCTransform2ArrayCopyB2CorC2B);
   tgt->setAspects(isub|mul|shr, ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 1, 2);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory (ByteToChar or CharToByte version)
// Input: ImportantNode(0) - array load
//        ImportantNode(1) - array store
//        ImportantNode(2) - indirect load of the array index for the array load
//*****************************************************************************************
bool
CISCTransform2ArrayCopyB2CBndchk(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCopyB2CBndchk due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR::Node *dstIndexRepNode, *exitVarRepNode, *variableORconstRepNode, *arrayLenRepNode;
   getP2TTrRepNodes(trans, &dstIndexRepNode, &exitVarRepNode, &variableORconstRepNode, &arrayLenRepNode);
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode->getSymbolReference();
   TR::SymbolReference * exitVarSymRef = exitVarRepNode->getSymbolReference();
   if (!trans->analyzeArrayIndex(dstIndexVarSymRef))
      {
      if (DISPTRACE(trans)) traceMsg(comp, "analyzeArrayIndex failed. %x\n",dstIndexRepNode);
      return false;
      }

   TR::Node * inputMemNode  = trans->getP2TRepInLoop(P->getImportantNode(0))->getHeadOfTrNodeInfo()->_node;
   TR::Node * outputMemNode = trans->getP2TRepInLoop(P->getImportantNode(1))->getHeadOfTrNodeInfo()->_node;
   TR::Node * indexLoadNode = trans->getP2TRepInLoop(P->getImportantNode(2))->getHeadOfTrNodeInfo()->_node;
   TR_ASSERT(inputMemNode && outputMemNode && indexLoadNode, "error");
   TR::Node * inputNode  = inputMemNode->getChild(0)->duplicateTree();
   TR::Node * outputNode = outputMemNode->getChild(0)->duplicateTree();

   TR::Node * exitVarNode = createLoad(exitVarRepNode);
   variableORconstRepNode = convertStoreToLoad(comp, variableORconstRepNode);
   TR::Node * lengthNode = createOP2(comp, TR::isub,
                                    variableORconstRepNode,
                                    exitVarNode);
   TR::Node * updateTree1, *updateTree2, *updateTree3;
   TR::Node * c2 = TR::Node::create(exitVarRepNode, TR::iconst, 0, 2);
   bool isExitVarChar = (outputMemNode->getSize() == 2);
   // Prepare nodes for byte length and induction variable updates
   indexLoadNode = indexLoadNode->duplicateTree();
   TR::Node * endIndex;
   if (isExitVarChar)   // The variable that checks the exit condition is for a 2-byte array.
      {
      TR::Node * diff = lengthNode;
      lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c2);
      // lengthNode has the byte size, and diff has the char-based size (that is, lengthNode = diff * 2)
      endIndex = createOP2(comp, TR::iadd, indexLoadNode, lengthNode);
      updateTree1 = TR::Node::createWithSymRef(TR::istorei, 2, 2, indexLoadNode->getChild(0), endIndex, indexLoadNode->getSymbolReference());
      updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, diff, trNode);
      }
   else
      {
      TR::Node * div2 = TR::Node::create(TR::idiv, 2, lengthNode, c2);
      lengthNode = TR::Node::create(TR::imul, 2, div2, c2); // to make the length even
      // lengthNode has the byte size, and div2 has the char-based size (that is, lengthNode = div2 * 2)
      endIndex = createOP2(comp, TR::iadd, indexLoadNode, lengthNode);
      updateTree1 = TR::Node::createWithSymRef(TR::istorei, 2, 2, indexLoadNode->getChild(0), endIndex, indexLoadNode->getSymbolReference());
      updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, div2, trNode);
      }
   updateTree3 = TR::Node::createStore(exitVarSymRef, variableORconstRepNode);

   lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode);
   // Prepare the arraycopy node
   TR::Node * arraycopy = TR::Node::createArraycopy(inputNode, outputNode, lengthNode);
   arraycopy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   arraycopy->setForwardArrayCopy(true);
   arraycopy->setArrayCopyElementType(TR::Int8);

   TR::Node * topArraycopy = TR::Node::create(TR::treetop, 1, arraycopy);
   TR::TreeTop * updateTreeTop1 = TR::TreeTop::create(comp, updateTree1);
   TR::TreeTop * updateTreeTop2 = TR::TreeTop::create(comp, updateTree2);
   TR::TreeTop * updateTreeTop3 = TR::TreeTop::create(comp, updateTree3);

   // Insert nodes and maintain the CFG
   List<TR::Node> guardList(comp->trMemory());
   guardList.add(TR::Node::createif(TR::ifiucmpgt, endIndex->duplicateTree(), createLoad(arrayLenRepNode)));
   guardList.add(TR::Node::createif(TR::ifiucmpge, indexLoadNode->duplicateTree(), createLoad(arrayLenRepNode)));
   block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree(), &guardList);

   block = trans->insertBeforeNodes(block);

   block->append(TR::TreeTop::create(comp, topArraycopy));
   block->append(updateTreeTop1);
   block->append(updateTreeTop2);
   block->append(updateTreeTop3);

   block = trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int indIndex2, end;
byte v0[ ];
char v2[ ];
while(true){
   v2[v1++] = ((v0[this.indeIndex1++] & 0xFF) << 8) + (v0[this.indIndex1++] & 0xFF))
   v3++;
   if (v3 >= end) break;
}

Note 1: One of target methods is com/ibm/rmi/iiop/CDRInputStream.read_wstring().
****************************************************************************************/
TR_PCISCGraph *
makeMemCpyByteToCharBndchkGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCpyByteToCharBndchk", 0, 16);
   bool isBigEndian = (ctrl & CISCUtilCtl_BigEndian);
   /*******************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 17,   0,   0,    0);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 16,   0,   0,    1);  tgt->addNode(v4); // exit checking
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_quasiConst2, TR::NoType, tgt->incNumNodes(),15,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *alen= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_quasiConst2, TR::NoType, tgt->incNumNodes(),14,   0,   0);  tgt->addNode(alen); // arraylength
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *ths = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    2);  tgt->addNode(ths); // this object
   TR_PCISCNode *aidx= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),10,   0,   0,    0);  tgt->addNode(aidx);
   TR_PCISCNode *cmah= createIdiomArrayHeaderConst (tgt, ctrl, tgt->incNumNodes(), 9, c);// array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 8, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *ah1 = isBigEndian ? cmah  : cmah1;
   TR_PCISCNode *ah2 = isBigEndian ? cmah1 : cmah;
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -2);  tgt->addNode(cm2);
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 2);                    // element size
   TR_PCISCNode *c256= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  256);  tgt->addNode(c256);

   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iloadi, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   ent, ths); tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,  idx0, idx0, cm1); tgt->addNode(idx1);
   TR_PCISCNode *idx2= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istorei, TR::Int32,   tgt->incNumNodes(),  1,   1,   2,  idx1, ths, idx1); tgt->addNode(idx2);
   TR_PCISCNode *idx3= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,  idx2, alen,idx0); tgt->addNode(idx3);
   TR_PCISCNode *idx4= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,  idx3, idx0, cm2); tgt->addNode(idx4);
   TR_PCISCNode *idx5= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istorei, TR::Int32,   tgt->incNumNodes(),  1,   1,   2,  idx4, ths, idx4); tgt->addNode(idx5);
   TR_PCISCNode *idx6= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,  idx5, alen,idx1); tgt->addNode(idx6);
   TR_PCISCNode *ns0 = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, idx6, aidx, cmah, c2);
   TR_PCISCNode *ns1 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns0, v2, ns0);
   TR_PCISCNode *nl00;
   TR_PCISCNode *nl10;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      nl00= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2l, TR::Int64,       tgt->incNumNodes(),  1,   1,   1,   ns1, idx0); tgt->addNode(nl00);
      nl10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, nl00, nl00, ah1, c1);
      }
   else
      {
      nl00= idx0;
      nl10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, ns1, nl00, ah1, c1);
      }
   TR_PCISCNode *nl11= createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl10, v0, nl10);
   TR_PCISCNode *nl12= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bloadi, TR::Int8,    tgt->incNumNodes(),  1,   1,   1,   nl11, nl11); tgt->addNode(nl12);
   TR_PCISCNode *nl13= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl12, nl12); tgt->addNode(nl13);
   TR_PCISCNode *nl14= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl13, nl13, c256); tgt->addNode(nl14);
   TR_PCISCNode *nl20= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl | CISCUtilCtl_NoI2L, 1, nl14, nl00, ah2, c1);
   TR_PCISCNode *nl21= createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl20, v0, nl20);
   TR_PCISCNode *nl22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bloadi, TR::Int8,    tgt->incNumNodes(),  1,   1,   1,   nl21, nl21); tgt->addNode(nl22);
   TR_PCISCNode *nl23= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl22, nl22); tgt->addNode(nl23);
   TR_PCISCNode *ns2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iadd, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl23, nl14, nl23); tgt->addNode(ns2);
   TR_PCISCNode *ns3 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2s, TR::Int16,       tgt->incNumNodes(),  1,   1,   1,   ns2, ns2); tgt->addNode(ns3);
   TR_PCISCNode *ns4 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::sstorei, TR::Int16,   tgt->incNumNodes(),  1,   1,   2,   ns3, ns1, ns3); tgt->addNode(ns4);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns4, v3, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v4, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v4, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);
   n8->setIsChildDirectlyConnected();
   idx3->setIsChildDirectlyConnected();
   idx6->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(isBigEndian ? nl12 : nl22, ns4, idx0);
   tgt->setNumDagIds(18);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopyB2CBndchk);
   tgt->setAspects(isub|iadd|mul|bndchk|sameTypeLoadStore, ILTypeProp::Size_1|ILTypeProp::Size_4, ILTypeProp::Size_2|ILTypeProp::Size_4);
   tgt->setNoAspects(call, 0, 0);
   tgt->setMinCounts(1, 3, 3);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory (ByteToChar or CharToByte version)
// Input: ImportantNode(0) - array load in the little endian path
//        ImportantNode(1) - array store in the little endian path
//        ImportantNode(2) - array load in the big endian path
//        ImportantNode(3) - array store in the big endian path
//        ImportantNode(4) - if statement of the flag checking
//        ImportantNode(5) - if statement of back edge
//*****************************************************************************************
bool
CISCTransform2ArrayCopyC2BMixed(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCopyC2BMixed due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR::Node *indexRepNode, *dstIndexRepNode, *arrayLenRepNode;
   getP2TTrRepNodes(trans, &indexRepNode, &dstIndexRepNode, &arrayLenRepNode);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode->getSymbolReference();
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0)
      {
      if (DISPTRACE(trans)) traceMsg(comp, "analyzeArrayIndex failed. %x\n",indexRepNode);
      return false;
      }
   TR_ASSERT(indexVarSymRef != dstIndexVarSymRef, "error");
   if (trans->countGoodArrayIndex(dstIndexVarSymRef) == 0)
      {
      if (DISPTRACE(trans)) traceMsg(comp, "analyzeArrayIndex failed. %x\n",dstIndexRepNode);
      return false;
      }

   TR_CISCNode * BEloadMem  = trans->getP2TInLoopIfSingle(P->getImportantNode(2));
   TR_CISCNode * BEstoreMem = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
   TR_CISCNode * LEloadMem  = trans->getP2TRepInLoop(P->getImportantNode(0), BEloadMem);
   TR_CISCNode * LEstoreMem = trans->getP2TInLoopIfSingle(P->getImportantNode(1));
   TR_CISCNode * flagIf = trans->getP2TInLoopIfSingle(P->getImportantNode(4));
   TR_CISCNode * backIf = trans->getP2TInLoopIfSingle(P->getImportantNode(5));

   if (DISPTRACE(trans)) traceMsg(comp, "All parameters: %x %x %x %x %x %x\n",
                                 LEloadMem, LEstoreMem, BEloadMem, BEstoreMem, flagIf, backIf);
   if (!LEloadMem || !LEstoreMem || !BEloadMem || !BEstoreMem || !flagIf || !backIf) return false;
   if (flagIf->getOpcode() != TR::ificmpeq && flagIf->getOpcode() != TR::ificmpne) return false;

   TR_ASSERT(searchNodeInBlock(flagIf->getSucc(1), LEloadMem) ||
          searchNodeInBlock(flagIf->getSucc(1), BEloadMem), "error");
   TR_ASSERT(!searchNodeInBlock(flagIf->getSucc(1), LEloadMem) ||
          !searchNodeInBlock(flagIf->getSucc(1), BEloadMem), "error");
   bool LEalongJumpPath = searchNodeInBlock(flagIf->getSucc(1), LEloadMem);
   bool isBig = comp->target().cpu.isBigEndian();
   if (!isBig) LEalongJumpPath = !LEalongJumpPath;
   if (DISPTRACE(trans)) traceMsg(comp, "LEalongJumpPath = %d\n",LEalongJumpPath);

   TR::Block *blockBE = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency()/2, block);
   TR::Block *blockLE = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency()/2, block);
   TR::Block *blockAfter = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);

   TR::Node * LEloadMemNode  = LEloadMem->getHeadOfTrNode();
   TR::Node * LEstoreMemNode = LEstoreMem->getHeadOfTrNode();
   TR::Node * BEloadMemNode  = BEloadMem->getHeadOfTrNode();
   TR::Node * BEstoreMemNode = BEstoreMem->getHeadOfTrNode();
   TR::Node * flagIfNode = flagIf->getHeadOfTrNode()->duplicateTree();
   TR::Node * backIfNode = backIf->getHeadOfTrNode();

   TR::Node * variableORconstRepNode = backIfNode->getChild(1)->duplicateTree();
   indexRepNode = createLoad(indexRepNode);
   TR::Node * lengthNode = createOP2(comp, TR::isub, variableORconstRepNode, indexRepNode);
   TR::Node * c2 = TR::Node::create(indexRepNode, TR::iconst, 0, 2);
   TR::Node * diff = lengthNode;
   lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c2);
   // lengthNode has the byte size, and diff has the char-based size (that is, lengthNode = diff * 2)
   TR::Node * indexLoadNode = backIfNode->getChild(0)->duplicateTree();

   //
   // Big Endian Path
   //
   TR::Node * BELoadAddrTree  = BEloadMemNode->getChild(0)->duplicateTree();
   TR::Node * BEStoreAddrTree = BEstoreMemNode->getChild(0)->duplicateTree();
   TR::Node * BEMemCpy = TR::Node::createArraycopy(BELoadAddrTree, BEStoreAddrTree, createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode));
   BEMemCpy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   BEMemCpy->setForwardArrayCopy(true);
   BEMemCpy->setArrayCopyElementType(TR::Int8);
   blockBE->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, BEMemCpy)));
   TR::Node * updateTree1 = TR::Node::createStore(indexVarSymRef, variableORconstRepNode->duplicateTree());
   TR::Node * updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, lengthNode, trNode);
   blockBE->append(TR::TreeTop::create(comp, updateTree2));
   blockBE->append(TR::TreeTop::create(comp, updateTree1));
   blockBE->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, blockAfter->getEntry())));

   //
   // Little Endian Path
   //
   TR::Node * LELoadTree  = LEloadMemNode->duplicateTree();
   TR::Node * LEStoreAddrTree = LEstoreMemNode->getChild(0)->duplicateTree();
   if (comp->cg()->supportsByteswap())
      {
      TR::Node * LEReverseStore = TR::Node::createWithSymRef(TR::sstorei, 2, 2,
         LEStoreAddrTree,
         TR::Node::create(TR::sbyteswap, 1, LELoadTree),
         comp->getSymRefTab()->findOrCreateGenericIntShadowSymbolReference(0));
      blockLE->append(TR::TreeTop::create(comp, LEReverseStore));
      }
   else
      {
      TR::Node *replaceParent = NULL;
      int childNum = -1;
      bool ret;
      TR::Node * LEStoreAddrTree2 = LEStoreAddrTree->duplicateTree();
      TR::Node *arrayHeaderConst = createArrayHeaderConst(comp, comp->target().is64Bit(), trNode);
      ret = trans->searchNodeInTrees(isBig ? LEStoreAddrTree2 : LEStoreAddrTree,
                                     arrayHeaderConst, &replaceParent, &childNum);
      TR_ASSERT(ret, "error");
      if (comp->target().is64Bit())
         {
         arrayHeaderConst->setLongInt(arrayHeaderConst->getLongInt()-1);
         }
      else
         {
         arrayHeaderConst->setInt(arrayHeaderConst->getInt()-1);
         }
      replaceParent->setAndIncChild(childNum, arrayHeaderConst);

      TR::Node * LEc2b0 = TR::Node::create(TR::s2b, 1, LELoadTree);
      TR::Node * LEstore0 = TR::Node::createWithSymRef(TR::bstorei, 2, 2, LEStoreAddrTree, LEc2b0,
                                                 comp->getSymRefTab()->findOrCreateGenericIntShadowSymbolReference(0));
      blockLE->append(TR::TreeTop::create(comp, LEstore0));

      TR::Node * LEand1 = createOP2(comp, TR::iushr, LELoadTree, TR::Node::create(indexRepNode, TR::iconst, 0, 0x8));
      TR::Node * LEi2b1 = TR::Node::create(TR::i2b, 1, LEand1);
      TR::Node * LEstore1 = TR::Node::createWithSymRef(TR::bstorei, 2, 2, LEStoreAddrTree2, LEi2b1,
                                                 comp->getSymRefTab()->findOrCreateGenericIntShadowSymbolReference(0));
      blockLE->append(TR::TreeTop::create(comp, LEstore1));
      }
   TR::Node * c1 = TR::Node::create(indexRepNode, TR::iconst, 0, 1);
   TR::Node * indexUpdateLE = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, c1, trNode);
   blockLE->append(TR::TreeTop::create(comp, indexUpdateLE));
   blockLE->append(TR::TreeTop::create(comp, createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, c2->duplicateTree(), trNode)));
   TR::Node *backIfLE = TR::Node::createif(TR::ificmplt, indexUpdateLE->getChild(0), variableORconstRepNode->duplicateTree(),
                                         blockLE->getEntry());
   blockLE->append(TR::TreeTop::create(comp, backIfLE));

   // after these two paths
   //
   // Currently, blockAfter has no nodes.
   //

   //
   // Insert nodes and maintain the CFG
   List<TR::Node> guardList(comp->trMemory());
   guardList.add(TR::Node::createif(TR::ifiucmpgt, updateTree2->getChild(0)->duplicateTree(), createLoad(arrayLenRepNode)));
   guardList.add(TR::Node::createif(TR::ifiucmpge, createLoad(dstIndexRepNode), createLoad(arrayLenRepNode)));
   block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree(), &guardList);
   block = trans->insertBeforeNodes(block);
   flagIfNode->setBranchDestination(blockLE->getEntry());
   if (!LEalongJumpPath) TR::Node::recreate(flagIfNode, flagIfNode->getOpCode().getOpCodeForReverseBranch());
   block->append(TR::TreeTop::create(comp, flagIfNode));

   TR::CFG *cfg = comp->getFlowGraph();
   cfg->setStructure(NULL);
   TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
   if (orgNextTreeTop)
      {
      cfg->insertBefore(blockAfter, orgNextTreeTop->getNode()->getBlock());
      }
   else
      {
      cfg->addNode(blockAfter);
      }
   cfg->insertBefore(blockLE, blockAfter);
   cfg->insertBefore(blockBE, blockLE);
   cfg->join(block, blockBE);

   blockAfter = trans->insertAfterNodes(blockAfter);

   trans->setSuccessorEdges(block, blockBE, blockLE);
   trans->setSuccessorEdge(blockAfter, target);

   return true;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program (for big endian)
char v0[ ];
byte v2[ ];
while (true)
   {
   if(flag)
      {
      v2[i++] = (byte)(v0[j] & 0xff);
      v2[i++] = (byte)(v0[j] >>> 8 & 0xff);
      }
   else
      {
      v2[i++] = (byte)(v0[j] >>> 8 & 0xff);
      v2[i++] = (byte)(v0[j] & 0xff);
      }
   j++;
   if (j >= len) break;
   }

Note 1: One of target methods is com/ibm/rmi/iiop/CDROutputStream.read_wstring().
****************************************************************************************/
TR_PCISCGraph *
makeMEMCPYChar2ByteMixedGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MEMCPYChar2ByteMixed", 0, 16);
   /********************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 18,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 17,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *alen = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_quasiConst2, TR::NoType, tgt->incNumNodes(),16,   0,   0);  tgt->addNode(alen); // arraylength
   TR_PCISCNode *vorc = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(),TR_quasiConst2, TR::NoType, tgt->incNumNodes(),15,   0,   0);  tgt->addNode(vorc);     // length
   TR_PCISCNode *flag = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    2);  tgt->addNode(flag); // flag
   TR_PCISCNode *v0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *aidx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),11,   0,   0,    0);  tgt->addNode(aidx0);
   TR_PCISCNode *cmah = createIdiomArrayHeaderConst (tgt, ctrl, tgt->incNumNodes(), 10, c);// array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 9, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *cm1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  8,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,   -2);  tgt->addNode(cm2);
   TR_PCISCNode *c0   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,  0);  tgt->addNode(c0);
   TR_PCISCNode *c2   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 2);                    // element size
   TR_PCISCNode *c8   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  8);  tgt->addNode(c8);
   TR_PCISCNode *c1   = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *fchk = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   ent, flag, c0);  tgt->addNode(fchk);

   // big endian path
   TR_PCISCNode *bbck0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   fchk, alen, v3); tgt->addNode(bbck0);
   TR_PCISCNode *bld0 = createIdiomCharArrayLoadInLoop(tgt, ctrl | CISCUtilCtl_ChildDirectConnected, 1, bbck0, v0, aidx0, cmah, c2);
   TR_PCISCNode *bnc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   bld0, bld0); tgt->addNode(bnc2i);
   TR_PCISCNode *bns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iushr, TR::Int32,     tgt->incNumNodes(),  1,   1,   2,   bnc2i, bnc2i, c8); tgt->addNode(bns22);
   //TR_PCISCNode *bcvt0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   bns22, bns22); tgt->addNode(bcvt0);
   TR_PCISCNode *bns0 = createIdiomArrayStoreInLoop(tgt, ctrl|CISCUtilCtl_ChildDirectConnected, 1, bns22, TR::bstorei, TR::Int8,  v2, v3, cmah, c1, bns22);
   TR_PCISCNode *ba1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   bns0, v3, cm1); tgt->addNode(ba1);
   TR_PCISCNode *bbck1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   ba1, alen, ba1); tgt->addNode(bbck1);
   TR_PCISCNode *bns10= createIdiomArrayAddressInLoop(tgt, ctrl | CISCUtilCtl_ChildDirectConnected, 1, bbck1, v2, v3, cmah1, c1);
   TR_PCISCNode *bcvt1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   bns10, bld0); tgt->addNode(bcvt1);
   TR_PCISCNode *bns11= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   bcvt1, bns10, bcvt1); tgt->addNode(bns11);
   TR_PCISCNode *bn6  = createIdiomDecVarInLoop(tgt, ctrl, 1, bns11, v3, cm2);

   // little endian path
   TR_PCISCNode *lbck0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   fchk, alen, v3); tgt->addNode(lbck0);
   TR_PCISCNode *lld0 = createIdiomCharArrayLoadInLoop(tgt, ctrl | CISCUtilCtl_ChildDirectConnected, 1, lbck0, v0, aidx0, cmah, c2);
   TR_PCISCNode *lns10= createIdiomArrayAddressInLoop(tgt, ctrl | CISCUtilCtl_ChildDirectConnected, 1, lld0, v2, v3, cmah, c1);
   TR_PCISCNode *lcvt1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   lns10, lld0); tgt->addNode(lcvt1);
   TR_PCISCNode *lns11= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   lcvt1, lns10, lcvt1); tgt->addNode(lns11);
   TR_PCISCNode *la1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   lns11, v3, cm1); tgt->addNode(la1);
   TR_PCISCNode *lbck1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   la1, alen, la1); tgt->addNode(lbck1);
   TR_PCISCNode *lnc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,       tgt->incNumNodes(),  1,   1,   1,   lbck1, lld0); tgt->addNode(lnc2i);
   TR_PCISCNode *lns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iushr, TR::Int32,     tgt->incNumNodes(),  1,   1,   2,   lnc2i, lnc2i, c8); tgt->addNode(lns22);
   //TR_PCISCNode *lcvt0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   lns22, lns22); tgt->addNode(lcvt0);
   TR_PCISCNode *lns0 = createIdiomArrayStoreInLoop(tgt, ctrl|CISCUtilCtl_ChildDirectConnected, 1, lns22, TR::bstorei, TR::Int8,  v2, v3, cmah1, c1, lns22);
   TR_PCISCNode *ln6  = createIdiomDecVarInLoop(tgt, ctrl, 1, lns0, v3, cm2);

   // merge two paths
   TR_PCISCNode *addv1= createIdiomDecVarInLoop(tgt, ctrl, 1, ln6, v1, cm1);
   TR_PCISCNode *topAddV1 = addv1->getChild(0);
   TR_PCISCNode *back = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   addv1, v1, vorc);  tgt->addNode(back);
   TR_PCISCNode *ext  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(ext);

   fchk->setSuccs(lbck0, bbck0);
   bn6->setSucc(0, topAddV1);
   back->setSuccs(ent->getSucc(0), ext);

   bbck0->setIsChildDirectlyConnected();
   bbck1->setIsChildDirectlyConnected();
   bnc2i->setIsChildDirectlyConnected();
   bns22->setIsChildDirectlyConnected();
   //bcvt0->setIsChildDirectlyConnected();
   bcvt1->setIsChildDirectlyConnected();
   bns10->setIsChildDirectlyConnected();
   bns11->setIsChildDirectlyConnected();

   lbck0->setIsChildDirectlyConnected();
   lbck1->setIsChildDirectlyConnected();
   lnc2i->setIsChildDirectlyConnected();
   lns22->setIsChildDirectlyConnected();
   //lcvt0->setIsChildDirectlyConnected();
   lcvt1->setIsChildDirectlyConnected();
   lns10->setIsChildDirectlyConnected();
   lns11->setIsChildDirectlyConnected();

   fchk->setIsChildDirectlyConnected();
   back->setIsChildDirectlyConnected();

   bld0->setIsSuccDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(ext);
   tgt->setImportantNodes(lld0, lns11, bld0, bns0, fchk, back);
   tgt->setNumDagIds(18);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);

   tgt->setTransformer(CISCTransform2ArrayCopyC2BMixed);
   tgt->setAspects(isub|mul|shr|bndchk, ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call, 0, 0);
   tgt->setMinCounts(2, 2, 4);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }




//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory for CharToByte with two if-statements version
// Input: ImportantNodes(0) - array load
//        ImportantNodes(1) - array store
//        ImportantNodes(2) - the first if
//        ImportantNodes(3) - the second if
//*****************************************************************************************
bool
CISCTransform2ArrayCopyC2BIf2(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCopyC2BIf2 due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();

   TR::Node *indexRepNode, *dstIndexRepNode, *variableORconstRepNode, *variableORconstRepNode2;
   getP2TTrRepNodes(trans, &indexRepNode, &dstIndexRepNode, &variableORconstRepNode, &variableORconstRepNode2);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * dstIndexVarSymRef = dstIndexRepNode->getSymbolReference();
   TR_ASSERT(indexVarSymRef != dstIndexVarSymRef, "error!");

   TR::Node * inputNode = trans->getP2TRepInLoop(P->getImportantNode(0)->getChild(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = trans->getP2TRepInLoop(P->getImportantNode(1)->getChild(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();

   //**********************************************************************
   // For this idiom, because there are two if-statements, we need to check
   // which if-statement will trigger the loop exit.
   // Based on this, it will compute the length of copy, which will be
   // stored into the variable "lengthByteTemp".
   //**********************************************************************
   //
   TR::CFG *cfg = comp->getFlowGraph();
   TR::Node * c2 = TR::Node::create(indexRepNode, TR::iconst, 0, 2);
   indexRepNode = convertStoreToLoad(comp, indexRepNode)->duplicateTree();
   dstIndexRepNode = convertStoreToLoad(comp, dstIndexRepNode)->duplicateTree();
   variableORconstRepNode = convertStoreToLoad(comp, variableORconstRepNode)->duplicateTree();
   variableORconstRepNode2 = convertStoreToLoad(comp, variableORconstRepNode2)->duplicateTree();

   // Compute length
   TR::Block *chkLen1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
   TR::Block *chkLen2 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
   TR::Block *bodyBlock = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
   TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();

   TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
   // chkLen1
   TR::SymbolReference * lengthCharTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
   TR::SymbolReference * lengthByteTemp = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);
   TR::SymbolReference * selectLen2 = comp->getSymRefTab()->
         createTemporary(comp->getMethodSymbol(), TR::Int32);

   // use the formula to compute the number of iterations
   // the number of times the loop is executed
   // n1 => C1 = ceiling[(N1 - i)/incr(i)] = (N1 - i) // entry valueof i ; increment is 1 & lt condition
   //       C2 = floor[(N2 - j)/incr(j)] = floor[(N2 - j)/2]  // entry valueof j ; increment is 2 & le condition
   // n2 => C2 + 1                                                // which necessitates adding an extra iteration
   //
   // so the lesser(C1, C2) will decide which test exits the loop.
   //
   //
   TR::Node * lengthSrcNode = createOP2(comp, TR::isub,
                                       variableORconstRepNode,
                                       indexRepNode);
   TR::Node * storeSrcCharLen = TR::Node::createStore(lengthCharTemp, lengthSrcNode);
   TR::Node * storeSrcByteLen = TR::Node::createStore(lengthByteTemp,
                                  TR::Node::create(TR::imul, 2, lengthSrcNode, c2));
   TR::Node *zeroConst = TR::Node::create(indexRepNode, TR::iconst, 0, 0);
   TR::Node * storeSelectLen = TR::Node::createStore(selectLen2,
                                                    zeroConst);
   TR::Node * lengthDstNode = createOP2(comp, TR::isub,
                                       variableORconstRepNode2,
                                       dstIndexRepNode);

   TR::Node * c1 = TR::Node::create(indexRepNode, TR::iconst, 0, 1);

   TR::Node *incr = c1->duplicateTree();
   lengthDstNode = TR::Node::create(TR::ishr, 2, lengthDstNode, incr);
   TR::Node * lengthDstDiv2Node = TR::Node::create(TR::isub, 2, lengthDstNode, TR::Node::create(indexRepNode, TR::iconst, 0, -1));

   TR::Node *cmpMin = TR::Node::createif(TR::ificmpge, lengthDstDiv2Node, lengthSrcNode, bodyBlock->getEntry());
   chkLen1->append(TR::TreeTop::create(comp, storeSrcCharLen));
   chkLen1->append(TR::TreeTop::create(comp, storeSrcByteLen));
   chkLen1->append(TR::TreeTop::create(comp, storeSelectLen));
   chkLen1->append(TR::TreeTop::create(comp, cmpMin));

   // chkLen2
   c1 = c1->duplicateTree();
   lengthDstDiv2Node = lengthDstDiv2Node->duplicateTree();
   TR::Node * storeSrcCharLen2 = TR::Node::createStore(lengthCharTemp, lengthDstDiv2Node);
   TR::Node * storeSrcByteLen2 = TR::Node::createStore(lengthByteTemp,
                                                      TR::Node::create(TR::ishl, 2, lengthDstDiv2Node, c1->duplicateTree()));
   TR::Node * storeSelectLen2 = TR::Node::createStore(selectLen2, c1);
   chkLen2->append(TR::TreeTop::create(comp, storeSrcCharLen2));
   chkLen2->append(TR::TreeTop::create(comp, storeSrcByteLen2));
   chkLen2->append(TR::TreeTop::create(comp, storeSelectLen2));

   // body
   c2 = c2->duplicateTree();
   TR::Node * updateTree1, *updateTree2;
   updateTree1 = createStoreOP2(comp, indexVarSymRef, TR::iadd, indexVarSymRef, lengthCharTemp, trNode);
   updateTree2 = createStoreOP2(comp, dstIndexVarSymRef, TR::iadd, dstIndexVarSymRef, lengthByteTemp, trNode);

   // Prepare the node arraycopy
   TR::Node *lenNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, lengthByteTemp));
   TR::Node * arraycopy = TR::Node::createArraycopy(inputNode, outputNode, lenNode);
   arraycopy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   arraycopy->setForwardArrayCopy(true);
   arraycopy->setArrayCopyElementType(TR::Int8);

   TR::Node * topArraycopy = TR::Node::create(TR::treetop, 1, arraycopy);
   TR::TreeTop * updateTreeTop1 = TR::TreeTop::create(comp, updateTree1);
   TR::TreeTop * updateTreeTop2 = TR::TreeTop::create(comp, updateTree2);
   TR::Node * cmpExit = NULL;
   TR::TreeTop *failDest = NULL;
   TR::TreeTop *okDest = NULL;
   if (!target)         // multiple successor blocks
      {
      TR_CISCNode *cmpgeCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2));
      TR_CISCNode *cmpgtCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3));
      failDest = cmpgtCISCNode->getDestination();
      okDest = cmpgeCISCNode->getDestination();

      cmpExit = TR::Node::createif(TR::ificmpeq,
                                  TR::Node::createWithSymRef(indexRepNode, TR::iload, 0, selectLen2),
                                  TR::Node::create(indexRepNode, TR::iconst, 0, 0),
                                  okDest);
      }

   //
   // Insert nodes and maintain the CFG
   //
   TR::TreeTop *last;
   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);

   cfg->setStructure(NULL);

   trTreeTop->setNode(topArraycopy);
   bodyBlock->append(trTreeTop);
   bodyBlock->append(updateTreeTop1);
   bodyBlock->append(updateTreeTop2);
   trans->insertAfterNodes(bodyBlock);
   cfg->insertBefore(bodyBlock, orgNextBlock);
   cfg->insertBefore(chkLen2, bodyBlock);
   cfg->insertBefore(chkLen1, chkLen2);
   cfg->join(block, chkLen1);
   if (target)  // single successor block
      {
      trans->setSuccessorEdge(bodyBlock, target);
      }
   else
      {         // multiple successor blocks
      bodyBlock->append(TR::TreeTop::create(comp, cmpExit));
      trans->setSuccessorEdges(bodyBlock,
                               failDest->getEnclosingBlock(),
                               okDest->getEnclosingBlock());
      }
   trans->setSuccessorEdge(block, chkLen1);
   return true;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program (for big endian)
int v1, v3, end, end2;
char v0[ ];
byte v2[ ];
while(true){
   if (v1 >= end) break;
   if (v3 > end2) break;
   char T = v0[v1++];
   v2[v3++] = (byte)(T >> 8);
   v2[v3++] = (byte)(T & 0xff);
}

Note 1: This idiom also supports little endian.
****************************************************************************************/
TR_PCISCGraph *
makeMEMCPYChar2ByteGraph2(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MEMCPYChar2Byte2", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v1); // src array index
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    1);  tgt->addNode(v3); // dst array index
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),13,   0,   0);  tgt->addNode(vorc);     // length
   TR_PCISCNode *vorc2=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),12,   0,   0);  tgt->addNode(vorc2);    // length2
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 8, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cm2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,   -2);  tgt->addNode(cm2);
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 5, 2);                    // element size
   TR_PCISCNode *c8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  8);  tgt->addNode(c8);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *lv1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iload, TR::Int32,     tgt->incNumNodes(),  1,   1,   1,   ent, v1); tgt->addNode(lv1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, lv1, lv1, cm1);
   TR_PCISCNode *ns10= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, n7, v3, cmah, c1);
   TR_PCISCNode *ns11= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns10, v2, ns10);
   TR_PCISCNode *nl0 = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, ns11, lv1, cmah, c2);
   TR_PCISCNode *nl1 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, nl0, v0, nl0);
   TR_PCISCNode *nl2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::sloadi, TR::Int16,    tgt->incNumNodes(),  1,   1,   1,   nl1, nl1); tgt->addNode(nl2);
   TR_PCISCNode *cvt0, *cvt1;
   if ((ctrl & CISCUtilCtl_BigEndian))
      {
      TR_PCISCNode *nc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   nl2, nl2); tgt->addNode(nc2i);
      TR_PCISCNode *ns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType, tgt->incNumNodes(), 1,   1,   2,   nc2i, nc2i, c8); tgt->addNode(ns22);
      cvt0=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns22, ns22); tgt->addNode(cvt0);
      }
   else
      {
      cvt0=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   nl2, nl2); tgt->addNode(cvt0);
      }
   TR_PCISCNode *ns14= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   cvt0, ns11, cvt0); tgt->addNode(ns14);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns14, v3, cm2);
   TR_PCISCNode *ns20= createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl|CISCUtilCtl_NoI2L, 1, n6, ns10->getChild(0)->getChild(0), cmah1, c1);
   TR_PCISCNode *ns21= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns20, v2, ns20);
   if ((ctrl & CISCUtilCtl_BigEndian))
      {
      cvt1=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::s2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns21, nl2); tgt->addNode(cvt1);
      }
   else
      {
      TR_PCISCNode *nc2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::su2i, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   ns21, nl2); tgt->addNode(nc2i);
      TR_PCISCNode *ns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType, tgt->incNumNodes(), 1,   1,   2,   nc2i, nc2i, c8); tgt->addNode(ns22);
      cvt1=            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns22, ns22); tgt->addNode(cvt1);
      }
   TR_PCISCNode *ns24= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   cvt1, ns21, cvt1); tgt->addNode(ns24);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   ns24, v1, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpgt, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n8, v3, vorc2);  tgt->addNode(n9);
   TR_PCISCNode *n10 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n10);

   n8->setSucc(1, n10);
   n9->setSuccs(ent->getSucc(0), n10);

   n8->setIsChildDirectlyConnected();
   n9->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n10);
   tgt->setImportantNodes(nl2, ns14, n8, n9);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialCareNode(0, cvt0); // conversion (possibly i2b)
   tgt->setSpecialCareNode(1, cvt1); // conversion (possibly i2b)
   tgt->setSpecialNodeTransformer(MEMCPYSpecialNodeTransformer);

   tgt->setTransformer(CISCTransform2ArrayCopyC2BIf2);
   tgt->setAspects(isub|mul|shr, ILTypeProp::Size_2, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 1, 2);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }




//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for copying memory (ByteToInt or IntToByte version)
// Input: ImportantNodes(0) - array load
//        ImportantNodes(1) - array store
//*****************************************************************************************
bool
CISCTransform2ArrayCopyB2I(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCopyB2I due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR::Node *indexRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &indexRepNode, &variableORconstRepNode);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();

   TR::Node * inputMemNode = trans->getP2TRepInLoop(P->getImportantNode(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputMemNode = trans->getP2TRepInLoop(P->getImportantNode(1))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * inputNode = trans->getP2TRepInLoop(P->getImportantNode(0)->getChild(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();
   TR::Node * outputNode = trans->getP2TRepInLoop(P->getImportantNode(1)->getChild(0))->getHeadOfTrNodeInfo()->_node->duplicateTree();

   TR::Node * exitVarNode = createLoad(indexRepNode);
   variableORconstRepNode = convertStoreToLoad(comp, variableORconstRepNode);
   TR::Node * lengthNode = createOP2(comp, TR::isub,
                                    variableORconstRepNode,
                                    exitVarNode);
   TR::Node * updateTree1;
   TR::Node * c4 = TR::Node::create(indexRepNode, TR::iconst, 0, 4);
   TR::Node * diff = lengthNode;
   lengthNode = TR::Node::create(TR::imul, 2, lengthNode, c4);
   // lengthNode has the byte size, and diff has the int-based size (that is, lengthNode = diff * 4)
   updateTree1 = TR::Node::createStore(indexVarSymRef, variableORconstRepNode);

   lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode);

   // Prepare the arraycopy node
   TR::Node * arraycopy = TR::Node::createArraycopy(inputNode, outputNode, lengthNode);
   arraycopy->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCopySymbol());
   arraycopy->setForwardArrayCopy(true);
   arraycopy->setArrayCopyElementType(TR::Int8);

   TR::Node * topArraycopy = TR::Node::create(TR::treetop, 1, arraycopy);
   TR::TreeTop * updateTreeTop1 = TR::TreeTop::create(comp, updateTree1);

   // Insert nodes and maintain the CFG
   TR::TreeTop *last;
   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);
   last = block->getLastRealTreeTop();
   last->join(trTreeTop);
   trTreeTop->setNode(topArraycopy);
   trTreeTop->join(updateTreeTop1);
   updateTreeTop1->join(block->getExit());

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, end;
byte v0[ ];
int v2[ ];
while(true){
   v2[v1] = ((v0[v1*4] & 0xFF) << 24) | (v0[v1*4+1] & 0xFF) << 16) |
             (v0[v1*4+2] & 0xFF) << 8) | (v0[v1*4+3] & 0xFF));
   v1++;
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeMEMCPYByte2IntGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MEMCPYByte2Int", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 16,   0,   0,    0);  tgt->addNode(v1); // array index of src and dst
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 15,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),12, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *cmah2=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),11, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+2));// array header+2
   TR_PCISCNode *cmah3=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),10, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+3));// array header+3
   TR_PCISCNode *ah1 = (ctrl & CISCUtilCtl_BigEndian) ? cmah  : cmah3;
   TR_PCISCNode *ah2 = (ctrl & CISCUtilCtl_BigEndian) ? cmah1 : cmah2;
   TR_PCISCNode *ah3 = (ctrl & CISCUtilCtl_BigEndian) ? cmah2 : cmah1;
   TR_PCISCNode *ah4 = (ctrl & CISCUtilCtl_BigEndian) ? cmah3 : cmah;
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  9,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *c4  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 8, 4);                    // element size
   TR_PCISCNode *ci4 = c4;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      ci4 =            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  7,   0,   0,  4);  tgt->addNode(ci4);
      }
   TR_PCISCNode *cs8 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,  0x100);  tgt->addNode(cs8);
   TR_PCISCNode *cs16= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,  0x10000);  tgt->addNode(cs16);
   TR_PCISCNode *cs24= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,  0x1000000);  tgt->addNode(cs24);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *ns0 = createIdiomArrayAddressIndexTreeInLoop(tgt, ctrl, 1, ent, v1, cmah, c4);
   TR_PCISCNode *ns1 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns0, v2, ns0);
   TR_PCISCNode *nmul= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   ns1, v1, ci4); tgt->addNode(nmul);
   TR_PCISCNode *nl12= createIdiomArrayLoadInLoop(tgt, ctrl, 1, nmul, TR::bloadi, TR::Int8,  v0, nmul, ah1, c1);
   TR_PCISCNode *nl13= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl12, nl12); tgt->addNode(nl13);
   TR_PCISCNode *nl14= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl13, nl13, cs24); tgt->addNode(nl14);
   TR_PCISCNode *nl22= createIdiomArrayLoadInLoop(tgt, ctrl, 1, nl14, TR::bloadi, TR::Int8,  v0, nmul, ah2, c1);
   TR_PCISCNode *nl23= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl22, nl22); tgt->addNode(nl23);
   TR_PCISCNode *nl24= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl23, nl23, cs16); tgt->addNode(nl24);
   TR_PCISCNode *nl25= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ior, TR::Int32,       tgt->incNumNodes(),  1,   1,   2,   nl24, nl14, nl24); tgt->addNode(nl25);
   TR_PCISCNode *nl32= createIdiomArrayLoadInLoop(tgt, ctrl, 1, nl25, TR::bloadi, TR::Int8,  v0, nmul, ah3, c1);
   TR_PCISCNode *nl33= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl32, nl32); tgt->addNode(nl33);
   TR_PCISCNode *nl34= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   nl33, nl33, cs8); tgt->addNode(nl34);
   TR_PCISCNode *nl35= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ior, TR::Int32,       tgt->incNumNodes(),  1,   1,   2,   nl34, nl25, nl34); tgt->addNode(nl35);
   TR_PCISCNode *nl42= createIdiomArrayLoadInLoop(tgt, ctrl, 1, nl35, TR::bloadi, TR::Int8,  v0, nmul, ah4, c1);
   TR_PCISCNode *nl43= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bu2i, TR::Int32,      tgt->incNumNodes(),  1,   1,   1,   nl42, nl42); tgt->addNode(nl43);
   TR_PCISCNode *nl45= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ior, TR::Int32,       tgt->incNumNodes(),  1,   1,   2,   nl43, nl35, nl43); tgt->addNode(nl45);
   TR_PCISCNode *ns4 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istorei, TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   nl45, ns1, nl45); tgt->addNode(ns4);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns4, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n6, v1, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes((ctrl & CISCUtilCtl_BigEndian) ? nl12 : nl42, ns4);
   tgt->setNumDagIds(17);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCopyB2I);
   tgt->setAspects(isub|mul|bitop1, ILTypeProp::Size_1, ILTypeProp::Size_4);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 4, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(hot, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, end, end2;
int v0[ ];
byte v2[ ];
while(true){
   v2[v1*4]   = (byte)(v0[v1] >>> 24) & 0xFF;
   v2[v1*4+1] = (byte)(v0[v1] >>> 16) & 0xFF;
   v2[v1*4+2] = (byte)(v0[v1] >>> 8) & 0xFF;
   v2[v1*4+3] = (byte)(v0[v1] & 0xff);
   v1++;
   if (v1 >= end) break;
}
****************************************************************************************/
TR_PCISCGraph *
makeMEMCPYInt2ByteGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MEMCPYInt2Byte", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 17,   0,   0,    0);  tgt->addNode(v1); // array index of src and dst
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),16,   0,   0);  tgt->addNode(vorc);      // length
   TR_PCISCNode *v0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v0); // src array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 14,   0,   0,    1);  tgt->addNode(v2); // dst array base
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cmah1=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),12, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+1));// array header+1
   TR_PCISCNode *cmah2=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),11, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+2));// array header+2
   TR_PCISCNode *cmah3=createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),10, -(int32_t)(TR::Compiler->om.contiguousArrayHeaderSizeInBytes()+3));// array header+3
   TR_PCISCNode *ah1 = (ctrl & CISCUtilCtl_BigEndian) ? cmah  : cmah3;
   TR_PCISCNode *ah2 = (ctrl & CISCUtilCtl_BigEndian) ? cmah1 : cmah2;
   TR_PCISCNode *ah3 = (ctrl & CISCUtilCtl_BigEndian) ? cmah2 : cmah1;
   TR_PCISCNode *ah4 = (ctrl & CISCUtilCtl_BigEndian) ? cmah3 : cmah;
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  9,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *cs4 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  8,   0,   0,    4);  tgt->addNode(cs4);      // element size
   TR_PCISCNode *cl4 = cs4;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      cl4 =            new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst, TR::Int64,    tgt->incNumNodes(),  7,   0,   0,    4);  tgt->addNode(cl4);      // element size for 64-bit
      }
   TR_PCISCNode *cs8 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  6,   0,   0,    8);  tgt->addNode(cs8);
   TR_PCISCNode *cs16= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  5,   0,   0,   16);  tgt->addNode(cs16);
   TR_PCISCNode *cs24= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   24);  tgt->addNode(cs24);
   TR_PCISCNode *c1  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *nmul= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   ent, v1, cs4); tgt->addNode(nmul);
   TR_PCISCNode *ns00= createIdiomArrayAddressInLoop(tgt, ctrl, 1, nmul, v2, nmul, ah1, c1);
   TR_PCISCNode *nl00;
   if (ctrl & CISCUtilCtl_64Bit)
      {
      nl00           = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns00, v0, v1,   cmah, cl4);
      }
   else
      {
      nl00           = createIdiomArrayAddressInLoop   (tgt, ctrl, 1, ns00, v0, nmul, cmah, c1);
      }
   TR_PCISCNode *nl01= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iloadi, TR::Int32,    tgt->incNumNodes(),  1,   1,   1,   nl00, nl00); tgt->addNode(nl01);
   TR_PCISCNode *ns01= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType,   tgt->incNumNodes(),  1,   1,   2,   nl01, nl01, cs24); tgt->addNode(ns01);
   TR_PCISCNode *ns02= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns01, ns01);       tgt->addNode(ns02);
   TR_PCISCNode *ns03= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   ns02, ns00, ns02); tgt->addNode(ns03);
   TR_PCISCNode *ns10= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns03, v2, nmul, ah2, c1);
   TR_PCISCNode *ns11= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType,   tgt->incNumNodes(),  1,   1,   2,   ns10, nl01, cs16); tgt->addNode(ns11);
   TR_PCISCNode *ns12= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns11, ns11);       tgt->addNode(ns12);
   TR_PCISCNode *ns13= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   ns12, ns10, ns12); tgt->addNode(ns13);
   TR_PCISCNode *ns20= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns13, v2, nmul, ah3, c1);
   TR_PCISCNode *ns21= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ishrall, TR::NoType,   tgt->incNumNodes(),  1,   1,   2,   ns20, nl01, cs8); tgt->addNode(ns21);
   TR_PCISCNode *ns22= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns21, ns21);       tgt->addNode(ns22);
   TR_PCISCNode *ns23= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   ns22, ns20, ns22); tgt->addNode(ns23);
   TR_PCISCNode *ns30= createIdiomArrayAddressInLoop(tgt, ctrl, 1, ns23, v2, nmul, ah4, c1);
   TR_PCISCNode *ns32= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(),  1,   1,   1,   ns30, nl01);       tgt->addNode(ns32);
   TR_PCISCNode *ns33= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8,   tgt->incNumNodes(),  1,   1,   2,   ns32, ns30, ns32); tgt->addNode(ns33);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ns33, v1, cm1);
   TR_PCISCNode *n8  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n6, v1, vorc);  tgt->addNode(n8);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   n8->setSuccs(ent->getSucc(0), n9);
   n8->setIsChildDirectlyConnected();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(nl01, (ctrl & CISCUtilCtl_BigEndian) ? ns03 : ns33);
   tgt->setNumDagIds(18);
   tgt->createInternalData(1);

   tgt->setSpecialCareNode(0, ns02); // i2b
   tgt->setSpecialCareNode(1, ns12); // i2b
   tgt->setSpecialCareNode(2, ns22); // i2b
   tgt->setSpecialCareNode(3, ns32); // i2b
   tgt->setSpecialNodeTransformer(MEMCPYSpecialNodeTransformer);

   tgt->setTransformer(CISCTransform2ArrayCopyB2I);
   tgt->setAspects(isub|mul|shr, ILTypeProp::Size_4, ILTypeProp::Size_1);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 1, 4);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(hot, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for filling memory
// Input: ImportantNode(0) - astore of aiadd or aladd for address induction variable
//        ImportantNode(1) - array element store
//        ImportantNode(2) - exit if
//*****************************************************************************************
static int32_t getAbs(int32_t val)
   {
   return val < 0 ? -val : val;
   }
bool
CISCTransform2PtrArraySet(TR_CISCTransformer *trans)
   {
   bool trace = trans->trace();
   TR::Node *trNode = NULL;
   TR::TreeTop *trTreeTop = NULL;
   TR::Block *block = NULL;
   TR_CISCGraph *p = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block)
      return false;    // cannot find

   // Currently, it allows only a single successor.
   TR::Block *target = trans->analyzeSuccessorBlock();
   if (!target)
      return false;

   // Only handle very simple loops.
   if (trans->getNumOfBBlistBody() > 1)
      {
      if (trace) traceMsg(comp, "Need exactly 1 basic block\n");
      return false;
      }

   // Should have 3 treetops in body. See makePtrArraySetGraph
   if (block->getNumberOfRealTreeTops() != 3)
      {
      if (trace) traceMsg(comp, "Need exactly 3 real treetops\n");
      return false;
      }

   auto astore = trans->getP2TRepInLoop(p->getImportantNode(0));
   auto Store = trans->getP2TRepInLoop(p->getImportantNode(1));
   auto ifcmp = trans->getP2TRepInLoop(p->getImportantNode(2));

   if (!astore)
      {
      if (trace) traceMsg(comp, "astore missing\n");
      return false;
      }
   if (!Store)
      {
      if (trace) traceMsg(comp, "array element store missing\n");
      return false;
      }
   if (!ifcmp)
      {
      if (trace) traceMsg(comp, "if compare missing\n");
      return false;
      }

   auto astoreNode = astore->getHeadOfTrNode();
   auto StoreNode = Store->getHeadOfTrNode();
   auto ifcmpNode = ifcmp->getHeadOfTrNode();

   if (!(astoreNode->getChild(0)->getChild(0) == StoreNode->getChild(0) &&
         astoreNode->getChild(0) == ifcmpNode->getChild(0)))
      {
      if (trace) traceMsg(comp, "node trees not in required form\n");
      return false;
      }

   if (!ifcmpNode->getChild(0)->getOpCode().isLoadVar() &&
       !ifcmpNode->getChild(1)->getOpCode().isLoadVar())
      {
      if (trace) traceMsg(comp, "neither comparands are loadvar\n");
      return false;
      }

   if (ifcmpNode->getChild(0)->getOpCode().isLoadVar() ^
       ifcmpNode->getChild(1)->getOpCode().isLoadVar())
      {
      auto nonLoadChild = (ifcmpNode->getChild(0)->getOpCode().isLoadVar()) ?
            ifcmpNode->getChild(1) : ifcmpNode->getChild(0);
      if (astoreNode->getChild(0) != nonLoadChild)
         {
         if (trace) traceMsg(comp, "iv is not a commoned child in if comparand\n");
         return false;
         }
      }

   // Only ordered compare {lt,le,ge,gt} and ne allowed
   if (!ifcmpNode->getOpCode().isCompareForOrder() &&
       !(!ifcmpNode->getOpCode().isCompareTrueIfEqual() && ifcmpNode->getOpCode().isCompareForEquality()))
      {
      if (trace) traceMsg(comp, "invalid compare condition\n");
      return false;
      }

   if (!StoreNode->getOpCode().isStoreIndirect() ||
       (StoreNode->getChild(0)->getOpCode().isLoadVar() &&
        StoreNode->getChild(0)->getSymbolReference() != astoreNode->getSymbolReference()))
      {
      if (trace) traceMsg(comp, "array element store node is neither indirect store "
            "nor matched with addr iv\n");
      return false;
      }

   switch(StoreNode->getSize())
      {
      case 1:
      case 2:
      case 4:
      case 8: break;
      default:
         if (trace)
            traceMsg(comp, "element size is not power-of-2 <= 8\n");
         return false;
      }

   if (StoreNode->getDataType() == TR::Aggregate)
      {
      if (trace)
         traceMsg(comp, "arrayset can't handle aggregate elem type\n");
      return false;
      }

   auto increment = astoreNode->getChild(0)->getChild(1)->getConst<int32_t>();
   if (StoreNode->getSize() != getAbs(increment))
      {
      if (trace) traceMsg(comp, "increment size does not match element size\n");
      return false;
      }

   TR::Node *endPtr = NULL;
   if (ifcmpNode->getChild(0)->getOpCode().isLoadVar() &&
       ifcmpNode->getChild(0)->getSymbolReference() != astoreNode->getSymbolReference())
      endPtr = ifcmpNode->getChild(0);
   else if (ifcmpNode->getChild(1)->getOpCode().isLoadVar() &&
            ifcmpNode->getChild(1)->getSymbolReference() != astoreNode->getSymbolReference())
      endPtr = ifcmpNode->getChild(1);

   if (!endPtr)
      {
      if (trace) traceMsg(comp, "Could not get end pointer\n");
      return false;
      }

   // all good..  now actual transformations
   auto startPtr = TR::Node::createWithSymRef(TR::aload, 0, astoreNode->getSymbolReference());
   TR::Node *length, *arrayset;
   bool use64bit = comp->target().is64Bit();
   bool equal = ifcmpNode->getOpCode().isCompareTrueIfEqual();  // fix off by one.
   if (increment < 0)
      {
      length = TR::Node::create(use64bit ? TR::a2l : TR::a2i, 1, TR::Node::create(TR::asub, 2, startPtr, endPtr));
      if (equal)
         {
         length = TR::Node::create(use64bit ? TR::ladd : TR::iadd, 2, length,
               use64bit ? TR::Node::lconst(1) : TR::Node::iconst(1));
         }
      arrayset = TR::Node::create(TR::arrayset, 3, endPtr, StoreNode->getChild(1), length);
      }
   else
      {
      length = TR::Node::create(use64bit ? TR::a2l : TR::a2i, 1, TR::Node::create(TR::asub, 2, endPtr, startPtr));
      if (equal)
         {
         length = TR::Node::create(use64bit ? TR::ladd : TR::iadd, 2, length,
               use64bit ? TR::Node::lconst(1) : TR::Node::iconst(1));
         }
      arrayset = TR::Node::create(TR::arrayset, 3, startPtr, StoreNode->getChild(1), length);
      }
   arrayset->setSymbolReference(comp->getSymRefTab()->findOrCreateArraySetSymbol());

   //reset block
   block->getEntry()->join(block->getExit());
   block->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, arrayset)));

   ifcmpNode->recursivelyDecReferenceCount();
   StoreNode->recursivelyDecReferenceCount();
   auto tmpastoreChild = astoreNode->getChild(0);

   // set startPtr as if it got to the end of the loop
   if (equal)
      {
      int offsetAtEnd = (increment < 0) ? -1 : 1;
      auto newEnd = TR::Node::create(use64bit ? TR::aladd : TR::aiadd, 2, endPtr,
            use64bit ? TR::Node::lconst(offsetAtEnd) : TR::Node::iconst(offsetAtEnd));
      astoreNode->setAndIncChild(0, newEnd);
      }
   else
      {
      astoreNode->setAndIncChild(0, endPtr);
      }
   tmpastoreChild->recursivelyDecReferenceCount();

   block->append(TR::TreeTop::create(comp, astoreNode));
   trans->setSuccessorEdge(block, target);
   return true;
   }

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for filling memory
// Input: ImportantNode(0) - array store
//        ImportantNode(1) - Store of iadd or isub for induction variable
//        ImportantNode(2) - Store of iadd or isub for induction variable 1
//        ImportantNode(3) - exit if
//*****************************************************************************************
bool
CISCTransform2ArraySet(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode = NULL;
   TR::TreeTop *trTreeTop = NULL;
   TR::Block *block = NULL;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArraySet due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR_CISCNode *ivStoreCISCNode = trans->getP2TRepInLoop(P->getImportantNode(1));
   TR_CISCNode *ivStore1CISCNode = trans->getP2TRepInLoop(P->getImportantNode(2));
   TR_CISCNode *addORsubCISCNode = trans->getP2TRepInLoop(P->getImportantNode(1)->getChild(0));
   TR_CISCNode *addORsub1CISCNode = trans->getP2TRepInLoop(P->getImportantNode(2)->getChild(0));
   TR_CISCNode *cmpIfAllCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(3));

   TR_ScratchList<TR::Node> storeList(comp->trMemory());
   TR_ASSERT(ivStoreCISCNode, "Expected induction variable store node in Transform2ArraySet");
   storeList.add(ivStoreCISCNode->getHeadOfTrNode());

   if (ivStore1CISCNode && ivStore1CISCNode != ivStoreCISCNode)
      storeList.add(ivStore1CISCNode->getHeadOfTrNode());

   if (!cmpIfAllCISCNode)
      {
      if (disptrace) traceMsg(comp, "Not implemented yet for multiple-if\n");
      return false;
      }
   TR_ASSERT(addORsubCISCNode->getOpcode() == TR::isub || addORsubCISCNode->getOpcode() == TR::iadd, "error");
   TR_ASSERT(addORsub1CISCNode->getOpcode() == TR::isub || addORsub1CISCNode->getOpcode() == TR::iadd, "error");

   // Check which count-up or count-down loop
   bool isIncrement0 = (addORsubCISCNode->getOpcode() == TR::isub);
   bool isIncrement1 = (addORsub1CISCNode->getOpcode() == TR::isub);

   bool isIncrement = isIncrement0;

   // Depending on the loop exit comparison, we may need to adjust the length of the arrayset.
   int32_t lengthMod = 0;
   TR_CISCNode *retStore = trans->getT()->searchStore(cmpIfAllCISCNode->getChild(0), cmpIfAllCISCNode);
   switch(cmpIfAllCISCNode->getOpcode())
      {
      case TR::ificmpgt:
         lengthMod = 1;
         // fallthrough
      case TR::ificmpge:
         if (!isIncrement) return false;
         if (retStore == ivStoreCISCNode) lengthMod++;
         break;
      case TR::ificmplt:
         lengthMod = 1;
         // fallthrough
      case TR::ificmple:
         if (isIncrement) return false;
         if (retStore == ivStoreCISCNode) lengthMod++;
         break;
      default:
         traceMsg(comp, "Bailing CISCTransform2ArraySet due to unrecognized loop exit comparison.\n");
         return false;
      }

   if (disptrace)
      traceMsg(comp,"Examining exit comparison CICS node %d, and determined required length modifier to be: %d\n", cmpIfAllCISCNode->getID(), lengthMod);

   TR_ScratchList<TR::Node> listStores(comp->trMemory());
   ListAppender<TR::Node> appenderListStores(&listStores);
   ListIterator<TR_CISCNode> ni(trans->getP2T() + P->getImportantNode(0)->getID());
   TR_CISCNode *inStoreCISCNode;
   TR::Node *inStoreNode;
   for (inStoreCISCNode = ni.getFirst(); inStoreCISCNode; inStoreCISCNode = ni.getNext())
      {
      if (!inStoreCISCNode->isOutsideOfLoop())
         {
         inStoreNode = inStoreCISCNode->getHeadOfTrNodeInfo()->_node;
         if (!isIndexVariableInList(inStoreNode, &storeList))
            {
            dumpOptDetails(comp, "an index used in an array store %p is not consistent with the induction varaible updates\n", inStoreNode);
            return false;
            }
         // this idiom operates in two modes - arrayset for all values or arrayset only for setting to zero
         // if the codegen does not support generic arrayset - make sure we are storing a constant 0
         // note the stored value is constrained to a constant by the node matcher
         if (!trans->comp()->cg()->getSupportsArraySet()
             && !(inStoreNode->getType().isIntegral() && inStoreNode->getSecondChild()->get64bitIntegralValueAsUnsigned() == 0)
             && !(inStoreNode->getType().isAddress() && inStoreNode->getSecondChild()->getAddress() == 0))
            {
            dumpOptDetails(comp, "the cg only supports arrayset to zero, but found a non-zero or non-constant value\n");
            return false;
            }
         appenderListStores.add(inStoreNode);
         }
      }
   if (listStores.isEmpty()) return false;

   TR::Node *indexRepNode, *index1RepNode, *dstBaseRepNode, *variableORconstRepNode1;
   getP2TTrRepNodes(trans, &indexRepNode, &index1RepNode, &dstBaseRepNode, &variableORconstRepNode1);

   if (disptrace)
      {
      traceMsg(comp,"Identified target nodes\n\tindexRepNode: %p\n\tindex1RepNode: %p\n\tdstBaseRepNode: %p\n\tvariableOrconstRepNode1: %p\n",
            indexRepNode, index1RepNode, dstBaseRepNode, variableORconstRepNode1);
      }
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   TR::SymbolReference * indexVar1SymRef = index1RepNode->getSymbolReference();
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0 &&
       trans->countGoodArrayIndex(indexVar1SymRef) == 0) return false;
   if (indexVarSymRef != indexVar1SymRef)
      {
      // there are two induction variables
      if (!listStores.isSingleton())
         {
         dumpOptDetails(comp, "Multiple induction variables with multiple stores not supported for arrayset transformation.\n");
         return false;
         }
      if (!isIncrement1)
         {
         // We do not correctly handle the second induction variable being a decrement.
         // TODO: Things to fix include:
         //     Proper Last Value calculation for count-down loop that uses ind var 1.
         //     Proper length calculation for count-down loop that uses ind var 1.
         dumpOptDetails(comp, "A decrementing second induction variable is not supported. \n");
         return false;
         }
      }

   //
   // analyze each store
   //
   ListIterator<TR::Node> iteratorStores(&listStores);
   TR::Node * indexNode = createLoad(indexRepNode);

   // check if the induction variable
   // is being stored into the array
   for (inStoreNode = iteratorStores.getFirst(); inStoreNode; inStoreNode = iteratorStores.getNext())
      {
      TR::Node * valueNode = inStoreNode->getChild(1);
      if (valueNode->getOpCode().isLoadDirect() && valueNode->getOpCode().hasSymbolReference())
         {
         if (valueNode->getSymbolReference()->getReferenceNumber() == indexNode->getSymbolReference()->getReferenceNumber() ||
             valueNode->getSymbolReference()->getReferenceNumber() == index1RepNode->getSymbolReference()->getReferenceNumber())
            {
            traceMsg(comp, "arraystore tree has induction variable on rhs\n");
            return false;
            }
         }
      }

   List<TR::Node> listArraySet(comp->trMemory());
   TR::Node * computeIndex = NULL;
   TR::Node * lengthNode = NULL;
   TR::Node * lengthByteNode = NULL;

   for (inStoreNode = iteratorStores.getFirst(); inStoreNode; inStoreNode = iteratorStores.getNext())
      {
      TR::Node * outputNode = inStoreNode->getChild(0)->duplicateTree();
      TR::Node * valueNode = convertStoreToLoad(comp, inStoreNode->getChild(1));

      uint32_t elementSize = 0;
      if (inStoreNode->getType().isAddress())
         elementSize = TR::Compiler->om.sizeofReferenceField();
      else
         elementSize = inStoreNode->getSize();

      // Depending on the induction variable used in the loop, determine if it's count up or count down.
      bool loopIsIncrement = false;
      if (findAndOrReplaceNodesWithMatchingSymRefNumber(outputNode->getSecondChild(), NULL, indexVarSymRef->getReferenceNumber()))
         {
         loopIsIncrement = isIncrement0;
         }
      else
         {
         TR_ASSERT(findAndOrReplaceNodesWithMatchingSymRefNumber(outputNode->getSecondChild(), NULL, indexVar1SymRef->getReferenceNumber()), "Unable to find matching array access induction variable.\n");
         loopIsIncrement = isIncrement1;
         }

      if (!loopIsIncrement)    // count-down loop
         {
         // This case covers a backwards counting loops of the following general forms:

         //  A)  Induction variable update BEFORE the array store.
         //    i = i_init;
         //    do {
         //        i--;
         //        a [i + c] = d;
         //    } while ( i >= i_last );
         //
         //  B)  Induction variable update AFTER the array store.
         //    i = i_init;
         //    do {
         //        a [i + c] = d;
         //        i--;
         //    } while ( i >= i_last );
         //
         // The loops can be transformed into an equivalent forward counting loop:
         //    i = i_last';
         //    do {
         //       a [i + c] = d;
         //       i++;
         //    } while (i <= i_init')
         //
         // Where:
         //    A)  Induction variable update BEFORE the array store.
         //      i_init' = i_init - 1
         //      i_last' = i_last - 1
         //    B)  Induction variable update AFTER the array store.
         //      i_init' = i_init
         //      i_last' = i_last
         //
         // This forward version can be reduced to an arrayset
         //     arrayset
         //          a[i_last' + c]         // Address of first element to set (forward sense)
         //          bconst d              // Element to set.
         //          i_init - i_last (+1)  // Length
         // Calculate the last value of the induction variable in the original count-down loop.
         // This value becomes the index of the first element in the count-up version, and hence
         // the first element of the arrayset.

       	 TR::Node * lastValueNode = convertStoreToLoad(comp, variableORconstRepNode1);

         // Determine if the induction variable update is before the arrayset
         bool isIndexVarUpdateBeforeArrayset = (trans->findStoreToSymRefInInsertBeforeNodes(indexVarSymRef->getReferenceNumber()) != NULL);

         // Adjust for the index based on exit condition (i.e. > vs >= ) and whether the induction
         // variable update is before/after the array stores.
         //   i_last':             > (lengthMod=0)     >= (lengthMod=1)
         //                        ---------------     ----------------
         //            Before         i_last               i_last - 1
         //            After          i_last + 1           i_last
         int32_t lastLegalValueAdjustment = -lengthMod;
         if (!isIndexVarUpdateBeforeArrayset)
            lastLegalValueAdjustment++;

         // If the induction variable update is before the arrayset, we need to validate whether the array access
         // commoned the node with the iadd/isub of the induction variable.  i.e.
         //
         // istore #indvar
         //    iadd (A)
         //        iload #indvar (B)
         //        iconst -1
         // istore
         //    aiadd
         //        aload arraybase
         //        aiadd
         //            index
         //            iconst array_header_size
         //
         // where index could be:
         //   (A) commoned to iadd, effectively using new value of #indvar
         //   (B) commoned to iload, effectively using old value of #indvar
         //   (C) a new iload using new value of #indvar
         //
         //  Case (A) is problematic, as the induction variable store is still before the arrayset, but
         //  the array access pattern is using the original value of #indvar.
         //  Case (B) is okay, in that topological embedding will recognize that to be equivalent to
         //  updating induction variable after the arraystore.
         //  Case (C) is handled correctly.
         int32_t arrayStoreCommoningAdjustment = 0;
         if (isIndexVarUpdateBeforeArrayset)
            {
            TR::Node *origIndVarStore = ivStoreCISCNode->getHeadOfTrNodeInfo()->_node;
            TR::Node *origIndVarLoad = origIndVarStore->getChild(0)->getChild(0);

            TR::Node *origArrayIndVarLoad = findLoadWithMatchingSymRefNumber(inStoreNode->getChild(0)->getSecondChild(), indexVarSymRef->getReferenceNumber());

            // If they match, we have case (B), so we need to readjust by +1.
            if (origIndVarLoad == origArrayIndVarLoad)
               {
               traceMsg(comp, "Identified array index to have been referencing original induction variable value: %p\n",origIndVarLoad);
               arrayStoreCommoningAdjustment = 1;
               }
            }

         TR::Node *lastLegalValue = createOP2(comp, TR::iadd, lastValueNode,
                                   TR::Node::create(indexNode, TR::iconst, 0, lastLegalValueAdjustment + arrayStoreCommoningAdjustment));

         // Search for the induction variable in the array access sub-tree and replace that node
         // with the last value index we just calculated.
         bool isFound = findAndOrReplaceNodesWithMatchingSymRefNumber(outputNode->getSecondChild(), lastLegalValue, indexVarSymRef->getReferenceNumber());
         if (!isFound && (indexVarSymRef != indexVar1SymRef))
             isFound = findAndOrReplaceNodesWithMatchingSymRefNumber(outputNode->getSecondChild(), lastLegalValue, indexVar1SymRef->getReferenceNumber());

         TR_ASSERT(isFound, "Count down arrayset was unable to find and replace array access induction variable.\n");

         // Determine the length of the arrayset (# of elements to set) and adjusting it based on exit condition.
         // In the case of the induction variable update is before the array store, the indexNode value has already been
         // decremented by 1 once already (since i--; is inserted before the final arrayset.  We need to readjust that.
         //   length:             > (lengthMod=0)          >= (lengthMod=1)
         //                        ---------------           ----------------
         //            Before     i_init - i_last + 1         i_init - i_last +2
         //            After      i_init - i_last             i_init - i_last +1
         int32_t lengthAdjustment = lengthMod + ((isIndexVarUpdateBeforeArrayset)?1:0);

         lengthNode = createOP2(comp, TR::isub, indexNode, lastValueNode);
         lengthNode = createOP2(comp, TR::iadd, lengthNode, TR::Node::create(indexNode, TR::iconst, 0, lengthAdjustment));

         // Determine the final induction variable value on loop exit.
         // If the induction variable update is before the arrayset,
         //    it will be the last value we access.
         // If the induction variable update is after the arrayset,
         //    it will always be one less than the last index (count-down sense) that we access.
         computeIndex = createOP2(comp, TR::iadd, lastLegalValue, TR::Node::create(indexRepNode, TR::iconst, 0, ((isIndexVarUpdateBeforeArrayset)?0:-1) - arrayStoreCommoningAdjustment));

         }
      else      // count-up loop
         {
         TR::Node * lastValue = convertStoreToLoad(comp, variableORconstRepNode1);
         lastValue = createOP2(comp, isIncrement0 ? TR::iadd : TR::isub, lastValue,
                                  TR::Node::create(indexNode, TR::iconst, 0, lengthMod));

         // Induction variable 0 is always part of the loop exit condition based on idiom graph.
         if (isIncrement0)
            {
            lengthNode = createOP2(comp, TR::isub, lastValue, indexNode);
            }
         else
            {
            lengthNode = createOP2(comp, TR::isub, indexNode, lastValue);
            }
         computeIndex = lastValue;
         }

      lengthByteNode = lengthNode;
      const bool longOffsets = trans->isGenerateI2L();
      lengthByteNode = createI2LIfNecessary(comp, longOffsets, lengthByteNode);
      if (elementSize > 1)
         {
         TR::Node *elementSizeNode = NULL;
         if (longOffsets)
            elementSizeNode = TR::Node::lconst(inStoreNode, elementSize);
         else
            elementSizeNode = TR::Node::iconst(inStoreNode, elementSize);

         lengthByteNode = TR::Node::create(
            longOffsets ? TR::lmul : TR::imul,
            2,
            lengthByteNode,
            elementSizeNode);
         }

      TR::Node * arrayset = TR::Node::create(TR::arrayset, 3, outputNode, valueNode, lengthByteNode);
      arrayset->setSymbolReference(comp->getSymRefTab()->findOrCreateArraySetSymbol());

      listArraySet.add(TR::Node::create(TR::treetop, 1, arrayset));
      }

   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, computeIndex);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);
   TR::Node * indVar1UpdateNode = NULL;
   TR::TreeTop * indVar1UpdateTreeTop = NULL;
   if (indexVarSymRef != indexVar1SymRef)
      {
      indVar1UpdateNode = createStoreOP2(comp, indexVar1SymRef, TR::iadd, indexVar1SymRef, lengthNode, trNode);
      indVar1UpdateTreeTop = TR::TreeTop::create(comp, indVar1UpdateNode);
      }

   // Insert nodes and maintain the CFG
   TR::TreeTop *last;
   ListIterator<TR::Node> iteratorArraySet(&listArraySet);
   TR::Node *arrayset = NULL;
   TR_ASSERT(lengthByteNode, "Expected at least one set of arrayset.");
   block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthByteNode->duplicateTree());
   block = trans->insertBeforeNodes(block);
   last = block->getLastRealTreeTop();
   for (arrayset = iteratorArraySet.getFirst(); arrayset; arrayset = iteratorArraySet.getNext())
      {
      TR::TreeTop *newTop = TR::TreeTop::create(comp, arrayset);
      last->join(newTop);
      last = newTop;
      }
   last->join(indVarUpdateTreeTop);
   indVarUpdateTreeTop->join(block->getExit());
   if (indVar1UpdateTreeTop)
      {
      block->append(indVar1UpdateTreeTop);
      }

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

bool CISCTransform2Strlen16(TR_CISCTransformer *trans)
   {
   bool trace = trans->trace();
   TR::Node *trNode = NULL;
   TR::TreeTop *trTreeTop = NULL;
   TR::Block *block = NULL;
   TR_CISCGraph *p = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block)
      return false;    // cannot find

   // Currently, it allows only a single successor.
   TR::Block *target = trans->analyzeSuccessorBlock();
   if (!target)
      return false;

   // Only handle very simple loops.
   if (trans->getNumOfBBlistBody() > 1)
      {
      if (trace) traceMsg(comp, "Need exactly 1 basic block\n");
      return false;
      }

   // Should have 2 treetops in body. See makeStrlen16Graph
   if (block->getNumberOfRealTreeTops() != 2)
      {
      if (trace) traceMsg(comp, "Need exactly 2 real treetops\n");
      return false;
      }

   auto astore = trans->getP2TRepInLoop(p->getImportantNode(0));
   auto loopTest = trans->getP2TRepInLoop(p->getImportantNode(1));
   auto astoreNode = astore->getHeadOfTrNode();
   auto ificmpne = loopTest->getHeadOfTrNode();

   if (!astore || !loopTest || !astoreNode || !ificmpne)
      return false;

   auto ptr = astoreNode->getChild(0)->getChild(0);
   auto increment = astoreNode->getChild(0)->getChild(1)->getConst<int32_t>();

   TR::Node *iconst=NULL, *conv=NULL;
   if (ificmpne->getChild(0)->getOpCodeValue() == TR::iconst)
      {
      iconst = ificmpne->getChild(0);
      conv = ificmpne->getChild(1);
      }
   else if (ificmpne->getChild(1)->getOpCodeValue() == TR::iconst)
      {
      iconst = ificmpne->getChild(1);
      conv = ificmpne->getChild(0);
      }

   if (trace) traceMsg(comp, "Failed one of the requirements\n");
   return false;
   }

/*********************************************************************************************
 * Catch very simple case of strlen16
n170n     BBStart <block_30> (freq 1682) (in loop 30)
n177n     astore  <auto slot 14>[id=384:"pszTmp"] [#65  Auto] [flags 0x7 0x0 ]
n176n       aladd (X>=0 internalPtr sharedMemory )
n172n         aload  <auto slot 14>[id=384:"pszTmp"] [#65  Auto] [flags 0x7 0x0 ]
n175n         lconst 2 (highWordZero X!=0 X>=0 )
n185n     ificmpne --> block_30 BBStart at n170n ()
n184n       su2i (X>=0 )
n181n         sloadi  <refined-array-shadow>[id=185:"(unsigned short)"] [#61  Shadow]
n176n           ==>aladd
n183n       iconst 0 (X==0 X>=0 X<=0 )
n179n     BBEnd </block_30> =====
 */

TR_PCISCGraph *
makeStrlen16Graph(TR::Compilation *c, int32_t ctrl)
   {
   auto tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "Strlen16", 0, 10);
   auto entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(), 9, 1, 0);
   tgt->addNode(entry);

   auto ptr        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable,  TR::Address, tgt->incNumNodes(), 8, 0, 0, 0);
   tgt->addNode(ptr);
   auto increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst,   TR::Int64,   tgt->incNumNodes(), 7, 0, 0, 2);
   tgt->addNode(increment);
   auto addrAdd    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::aladd,    TR::Address, tgt->incNumNodes(), 6, 1, 2, entry, ptr, increment);
   tgt->addNode(addrAdd);
   auto addrStore  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::astore,   TR::Address, tgt->incNumNodes(), 5, 1, 2, addrAdd, addrAdd, ptr);
   tgt->addNode(addrStore);

   auto Load       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indload,   TR::Int16,   tgt->incNumNodes(), 4, 1, 1, addrStore, ptr);
   Load->addHint(addrAdd);
   tgt->addNode(Load);
   auto conversion = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::Int32,  tgt->incNumNodes(), 3, 1, 1, Load, Load);
   tgt->addNode(conversion);
   auto nullChar   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst,   TR::Int32,   tgt->incNumNodes(), 2, 0, 0, 0);
   tgt->addNode(nullChar);
   auto loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall,  TR::Int32,   tgt->incNumNodes(), 1, 2, 2, conversion, conversion, nullChar);
   tgt->addNode(loopTest);

   auto exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode,  TR::NoType,  tgt->incNumNodes(), 0, 0, 0);
   tgt->addNode(exit);

   loopTest->setSuccs(entry->getSucc(0), exit);
   loopTest->setIsChildDirectlyConnected();

   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(addrStore, loopTest);
   tgt->setNumDagIds(10);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2Strlen16);
   //tgt->setAspects(storeMasks);  // not sure which to set, but do want astore aload for ptr incr and any size ptr deref store
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 1, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }


/****************************************************************************************
 * Corresponding C-like pseudocode with loop with pointer increment
 * T* start = arr;      // char, short, int long ptr. Array can be any type container
 * T* end = arr + size   // arr
 * while(start < end)
 *   *start++ = 0;
 *
n16n      BBStart <block_5> (freq 10000) (in loop 5)                                          [0x00000000823bf380]
n22n      astore  <auto slot 2>[id=3:"start"] [#49  Auto] [flags 0x7 0x0 ]                    [0x00000000823bf590]
n21n        aladd (internalPtr sharedMemory )                                                 [0x00000000823bf538]
n18n          aload  <auto slot 2>[id=3:"start"] [#49  Auto] [flags 0x7 0x0 ] (X>=0 sharedMemory )  [0x00000000823b
n20n          lconst 1 (highWordZero X!=0 X>=0 )                                              [0x00000000823bf4e0]
n26n      bstorei  <refined-array-shadow>[id=7:"(char)"] [#51  Shadow] [flags 0x80000601 0x0 ]  [0x00000000823bf6f0
n18n        ==>aload
n25n        bconst   0 (Unsigned X==0 X>=0 X<=0 )                                             [0x00000000823bf698]
n31n      ifacmpne --> block_5 BBStart at n16n ()                                             [0x00000000823bf8a8]
n21n        ==>aladd
n30n        aload  <auto slot 0>[id=5:"end"] [#50  Auto] [flags 0x7 0x0 ]                     [0x00000000823bf850]
n28n      BBEnd </block_5> =====                                                              [0x00000000823bf7a0]
 *
 */
TR_PCISCGraph *
makePtrArraySetGraph(TR::Compilation *c, int32_t ctrl)
   {
   bool is64bit = c->target().is64Bit();
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "PtrArraySet", 0, 10);
   /******************************************************************************    opc          id         dagId #cfg #child other/pred/children */
   auto entry      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(), 9, 1, 0);
   tgt->addNode(entry);
   auto ptr      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable,  TR::Address, tgt->incNumNodes(), 8, 0, 0, 0);
   tgt->addNode(ptr);
   auto increment  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst,   TR::Int64,   tgt->incNumNodes(), 7, 0, 0);
   tgt->addNode(increment);
   auto addrAdd    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), is64bit ? TR::aladd : TR::aiadd, TR::Address, tgt->incNumNodes(), 6, 1, 2, entry, ptr, increment);
   tgt->addNode(addrAdd);
   auto addrStore  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::astore,   TR::Address, tgt->incNumNodes(), 5, 1, 2, addrAdd, addrAdd, ptr);
   tgt->addNode(addrStore);
   auto value      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variableORconst, TR::NoType, tgt->incNumNodes(), 4, 0, 0);
   tgt->addNode(value);   // set value
   auto Store      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore,  TR::NoType,  tgt->incNumNodes(), 3, 1, 2, addrStore, ptr, value);
   tgt->addNode(Store);
   auto endPtr     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variableORconst, TR::Address, tgt->incNumNodes(), 2, 0, 0, 0);
   tgt->addNode(endPtr);
   auto loopTest   = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall,  TR::Address, tgt->incNumNodes(), 1, 2, 2, Store, ptr, endPtr);
   tgt->addNode(loopTest);
   auto exit       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode,  TR::NoType,  tgt->incNumNodes(), 0, 0, 0);
   tgt->addNode(exit);

   loopTest->setSuccs(entry->getSucc(0), exit);
   loopTest->setIsChildDirectlyConnected();

   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(addrStore, Store, loopTest);
   tgt->setNumDagIds(10);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2PtrArraySet);
   //tgt->setAspects(storeMasks);  // not sure which to set, but do want astore aload for ptr incr and any size ptr deref store
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 0, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }

/****************************************************************************************
Corresponding Java-like pseudocode
int i, end, value;
 Array[ ];        // char, int, float, long, and so on
while(true){
   Array[i] = value;
   iaddORisub(i, -1)
   ifcmpall(i, end) break;
}

Note 1: This idiom matches both count up and down loops.
Note 2: The wildcard node iaddORisub matches iadd or isub.
Note 3: The wildcard node ifcmpall matches all types of if-instructions.
****************************************************************************************/
TR_PCISCGraph *
makeMemSetGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemSet", 0, 16);
   /******************************************************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *iv           = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),11, 0, 0, 0);  tgt->addNode(iv); // array index
   TR_PCISCNode *iv1          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),10, 0, 0, 1);  tgt->addNode(iv1); // array index
   TR_PCISCNode *Array        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType,  tgt->incNumNodes(),9, 0, 0, 0);  // array base
   tgt->addNode(Array);
   TR_PCISCNode *end          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),8, 0, 0);  tgt->addNode(end);    // length
   // if cg only supports arrayset to zero only match constant nodes
   TR_PCISCNode *value        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), c->cg()->getSupportsArraySet() ? TR_variableORconst : TR_allconst, TR::NoType, tgt->incNumNodes(), 7, 0, 0);  tgt->addNode(value);   // set value
   TR_PCISCNode *mulConst     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(), 6, 0, 0);
   tgt->addNode(mulConst);  // Multiplicative factor for index into non-byte arrays
   TR_PCISCNode *idx0         = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),5,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *aHeader      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,  tgt->incNumNodes(),  4, 0, 0, 0);  tgt->addNode(aHeader); // array header
   TR_PCISCNode *increment    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,  tgt->incNumNodes(),   3, 0, 0, -1);  tgt->addNode(increment);
   TR_PCISCNode *entry        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(),2, 1, 0);        tgt->addNode(entry);
   TR_PCISCNode *Addr         = createIdiomArrayAddressInLoop(tgt, ctrl, 1, entry, Array, idx0, aHeader, mulConst);
   TR_PCISCNode *i2x          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(), 1,   1,   1,   Addr, value); tgt->addNode(i2x);
   TR_PCISCNode *Store        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore, TR::NoType,  tgt->incNumNodes(), 1, 1, 2, i2x, Addr, i2x);
   tgt->addNode(Store);
   TR_PCISCNode *ivStore      = createIdiomIOP2VarInLoop(tgt, ctrl, 1, Store, TR_iaddORisub, iv, increment);
   TR_PCISCNode *iv1Store     = createIdiomIOP2VarInLoop(tgt, ctrl, 1, ivStore, TR_iaddORisub, iv1, increment);
   TR_PCISCNode *loopTest     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(), 1, 2, 2, iv1Store, iv, end);
   tgt->addNode(loopTest);
   TR_PCISCNode *exit         = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(), 0, 0, 0);        tgt->addNode(exit);

   loopTest->setSuccs(entry->getSucc(0), exit);
   loopTest->setIsChildDirectlyConnected();

   i2x->setIsOptionalNode();
   i2x->setIsChildDirectlyConnected();

   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(Store, ivStore, iv1Store, loopTest);
   tgt->setNumDagIds(12);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArraySet);
   tgt->setAspects(mul, 0, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, 0, 0);
   tgt->setMinCounts(1, 0, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//*****************************************************************************************
// IL code generation for filling memory
// Input: ImportantNode(0) - non-byte array store
//        ImportantNode(1) - byte array store
//        ImportantNode(2) - iadd or isub for induction variable
//        ImportantNode(3) - exit if
//        ImportantNode(4) - the size of elements
//*****************************************************************************************
bool
CISCTransform2MixedArraySet(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2MixedArraySet due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   TR_CISCNode *addORsubCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2));
   TR_CISCNode *cmpIfAllCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
   if (!cmpIfAllCISCNode) return false;
   TR_ASSERT(addORsubCISCNode->getOpcode() == TR::isub || addORsubCISCNode->getOpcode() == TR::iadd, "error");

   // Check which count-up or count-down loop
   bool isIncrement = (addORsubCISCNode->getOpcode() == TR::isub);
   int lengthMod = 0;
   switch(cmpIfAllCISCNode->getOpcode())
      {
      case TR::ificmpgt:
         lengthMod = 1;
         // fallthrough
      case TR::ificmpge:
         if (!isIncrement) return false;
         break;
      case TR::ificmple:
         lengthMod = -1;
         // fallthrough
      case TR::ificmplt:
         if (isIncrement) return false;
         break;
      default:
         return false;
      }

   List<TR::Node> listStores(comp->trMemory());
   ListAppender<TR::Node> appenderListStores(&listStores);
   ListIterator<TR_CISCNode> ni(trans->getP2T() + P->getImportantNode(0)->getID());
   TR_CISCNode *inStoreCISCNode;
   for (inStoreCISCNode = ni.getFirst(); inStoreCISCNode; inStoreCISCNode = ni.getNext())
      {
      if (!inStoreCISCNode->isOutsideOfLoop())
         appenderListStores.add(inStoreCISCNode->getHeadOfTrNodeInfo()->_node);
      }
   ni.set(trans->getP2T() + P->getImportantNode(1)->getID());
   for (inStoreCISCNode = ni.getFirst(); inStoreCISCNode; inStoreCISCNode = ni.getNext())
      {
      if (!inStoreCISCNode->isOutsideOfLoop())
         appenderListStores.add(inStoreCISCNode->getHeadOfTrNodeInfo()->_node);
      }
   if (listStores.isEmpty()) return false;

   TR::Node *indexRepNode, *variableORconstRepNode1;
   getP2TTrRepNodes(trans, &indexRepNode, &variableORconstRepNode1);
   TR::SymbolReference * indexVarSymRef = indexRepNode->getSymbolReference();
   if (trans->countGoodArrayIndex(indexVarSymRef) == 0) return false;

   //
   // analyze each store
   //
   ListIterator<TR::Node> iteratorStores(&listStores);
   TR::Node *inStoreNode;
   TR::Node * indexNode = createLoad(indexRepNode);

   // check if the induction variable
   // is being stored into the array
   for (inStoreNode = iteratorStores.getFirst(); inStoreNode; inStoreNode = iteratorStores.getNext())
      {
      TR::Node * valueNode = inStoreNode->getChild(1);
      if (valueNode->getOpCode().isLoadDirect() && valueNode->getOpCode().hasSymbolReference())
         {
         if (valueNode->getSymbolReference()->getReferenceNumber() == indexNode->getSymbolReference()->getReferenceNumber())
            {
            dumpOptDetails(comp, "arraystore tree has induction variable on rhs\n");
            return false;
            }
         }
      }

   List<TR::Node> listArraySet(comp->trMemory());
   TR::Node * computeIndex = NULL;
   for (inStoreNode = iteratorStores.getFirst(); inStoreNode; inStoreNode = iteratorStores.getNext())
      {
      TR::Node * outputNode = inStoreNode->getChild(0)->duplicateTree();
      TR::Node * valueNode = convertStoreToLoad(comp, inStoreNode->getChild(1));
      int elementSize = inStoreNode->getSize();

      TR::Node * lengthNode;
      if (!isIncrement)    // count-down loop
         {
         // exit variable is zero or not
         bool isInitOffset0 = (variableORconstRepNode1->getOpCodeValue() == TR::iconst && (variableORconstRepNode1->getInt()-lengthMod) == 0);
         bool done = false;
         TR::Node * constm1 = TR::Node::create(indexRepNode, TR::iconst, 0, -1);
         TR::Node * lastValue = NULL;
         if (isInitOffset0)
            {
            // When the array index is zero, it will modify the address computation to "base + size of header".
            TR::Node *arrayheader = outputNode->getSecondChild()->getSecondChild();
            switch (outputNode->getSecondChild()->getOpCodeValue())
               {
               case TR::iadd:
               case TR::ladd:
               outputNode->setSecond(arrayheader);
               done = true;
               break;
               case TR::isub:
               if (arrayheader->getOpCodeValue() == TR::iconst)
                  {
                  arrayheader->setInt(-arrayheader->getInt());
                  outputNode->setSecond(arrayheader);
                  done = true;
                  }
               break;
               case TR::lsub:
               if (arrayheader->getOpCodeValue() == TR::lconst)
                  {
                  arrayheader->setLongInt(-arrayheader->getLongInt());
                  outputNode->setSecond(arrayheader);
                  done = true;
                  }
               break;
               default:
               break;
               }
            lengthNode = indexNode;
            computeIndex = constm1;
            }
         else
            {
            lastValue = convertStoreToLoad(comp, variableORconstRepNode1);
            if (lengthMod)
               {
               lastValue = createOP2(comp, TR::isub,
                                     lastValue,
                                     TR::Node::create(indexNode, TR::iconst, 0, lengthMod));
               }
            lengthNode = createOP2(comp, TR::isub, indexNode, lastValue);
            computeIndex = createOP2(comp, TR::iadd, lastValue, constm1);
            }
         lengthNode = createOP2(comp, TR::isub, lengthNode, TR::Node::create(indexNode, TR::iconst, 0, -(lengthMod+1)));

         if (!done)
            {
            if (!lastValue) lastValue = convertStoreToLoad(comp, variableORconstRepNode1);
            TR::Node *termNode = createI2LIfNecessary(comp, ctrl, lastValue);
            TR::Node *mulNode = outputNode->getSecondChild()->getFirstChild();
            mulNode = mulNode->skipConversions();
            if (elementSize > 1)
               mulNode->setAndIncChild(0, termNode);
            else
               outputNode->getSecondChild()->setAndIncChild(0, termNode);
            }
         }
      else      // count-up loop
         {
         TR::Node * lastValue = convertStoreToLoad(comp, variableORconstRepNode1);
         if (lengthMod)
            {
            lastValue = createOP2(comp, TR::iadd,
                                  lastValue,
                                  TR::Node::create(indexNode, TR::iconst, 0, lengthMod));
            }
         lengthNode = createOP2(comp, TR::isub, lastValue, indexNode);
         computeIndex = lastValue;
         }

      if (elementSize > 1)
         lengthNode = TR::Node::create(TR::imul, 2,
                                      lengthNode,
                                      TR::Node::create(inStoreNode, TR::iconst, 0, elementSize));

      lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode);

      TR::Node * arrayset = TR::Node::create(TR::arrayset, 3, outputNode, valueNode, lengthNode);
      arrayset->setSymbolReference(comp->getSymRefTab()->findOrCreateArraySetSymbol());

      listArraySet.add(TR::Node::create(TR::treetop, 1, arrayset));
      }

   TR::Node * indVarUpdateNode = TR::Node::createStore(indexVarSymRef, computeIndex);
   TR::TreeTop * indVarUpdateTreeTop = TR::TreeTop::create(comp, indVarUpdateNode);

   // Insert nodes and maintain the CFG
   TR::TreeTop *last;
   ListIterator<TR::Node> iteratorArraySet(&listArraySet);
   TR::Node *arrayset;
   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);
   last = block->getLastRealTreeTop();
   for (arrayset = iteratorArraySet.getFirst(); arrayset; arrayset = iteratorArraySet.getNext())
      {
      TR::TreeTop *newTop = TR::TreeTop::create(comp, arrayset);
      last->join(newTop);
      last = newTop;
      }
   last->join(indVarUpdateTreeTop);
   indVarUpdateTreeTop->join(block->getExit());

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

/****************************************************************************************
Corresponding Java-like pseudocode
int i, end, value;
byte byteArray[ ];
 Array[ ];        // char, int, float, long, and so on
while(true){
   Array[i] = value1;
   byteArray[i] = value2;
   iaddORisub(i, -1)
   ifcmpall(i, end) break;
}

Note 1: This idiom matches both count up and down loops.
Note 2: The wildcard node iaddORisub matches iadd or isub.
Note 3: The wildcard node ifcmpall matches all types of if-instructions.
****************************************************************************************/
TR_PCISCGraph *
makeMixedMemSetGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MixedMemSet", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *iv           = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),12, 0, 0, 0);  tgt->addNode(iv); // array index
   TR_PCISCNode *end          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),11, 0, 0);  tgt->addNode(end);    // length
   TR_PCISCNode *Array = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),10, 0, 0, 0);  // array base
   tgt->addNode(Array);
   TR_PCISCNode *byteArray    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 9, 0, 0, 1);  // array base
   tgt->addNode(byteArray);
   TR_PCISCNode *value1       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variableORconst, TR::NoType, tgt->incNumNodes(), 8, 0, 0);  tgt->addNode(value1);   // set value
   TR_PCISCNode *value2       = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variableORconst, TR::NoType, tgt->incNumNodes(), 7, 0, 0);  tgt->addNode(value2);   // set value
   TR_PCISCNode *mulConst     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(), 6, 0, 0);
   tgt->addNode(mulConst);  // Multiplicative factor for index into non-byte arrays
   TR_PCISCNode *idx0         = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(),5,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *aHeader      = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,  tgt->incNumNodes(),  4, 0, 0, 0);  tgt->addNode(aHeader); // array header
   TR_PCISCNode *increment    = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,  tgt->incNumNodes(),   3, 0, 0, -1);  tgt->addNode(increment);
   TR_PCISCNode *c1           = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 3, 1);                    // element size
   TR_PCISCNode *entry        = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType,  tgt->incNumNodes(),2, 1, 0);        tgt->addNode(entry);
   TR_PCISCNode *Addr  = createIdiomArrayAddressInLoop(tgt, ctrl, 1, entry, Array, idx0, aHeader, mulConst);
   TR_PCISCNode *i2x          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(), 1,   1,   1,   Addr, value1); tgt->addNode(i2x);
   TR_PCISCNode *Store = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_inbstore, TR::NoType,  tgt->incNumNodes(), 1, 1, 2, i2x, Addr, i2x);
   tgt->addNode(Store);
   TR_PCISCNode *byteAddr     = createIdiomArrayAddressInLoop(tgt, ctrl, 1, Store, byteArray, idx0, aHeader, c1);
   TR_PCISCNode *i2b          = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::i2b, TR::Int8,       tgt->incNumNodes(), 1,   1,   1,   byteAddr, value2); tgt->addNode(i2b);
   TR_PCISCNode *byteStore    =new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::bstorei, TR::Int8, tgt->incNumNodes(), 1, 1, 2, i2b, byteAddr, i2b);
   tgt->addNode(byteStore);
   TR_PCISCNode *ivStore      = createIdiomIOP2VarInLoop(tgt, ctrl, 1, byteStore, TR_iaddORisub, iv, increment);
   TR_PCISCNode *loopTest     = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(), 1, 2, 2, ivStore, iv, end);
   tgt->addNode(loopTest);
   TR_PCISCNode *exit         = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(), 0, 0, 0);        tgt->addNode(exit);

   loopTest->setSuccs(entry->getSucc(0), exit);
   loopTest->setIsChildDirectlyConnected();

   i2x->setIsOptionalNode();
   i2x->setIsChildDirectlyConnected();
   i2b->setIsOptionalNode();
   i2b->setIsChildDirectlyConnected();

   tgt->setEntryNode(entry);
   tgt->setExitNode(exit);
   tgt->setImportantNodes(Store, byteStore, ivStore->getChild(0), loopTest, mulConst);
   tgt->setNumDagIds(13);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2MixedArraySet);
   tgt->setAspects(mul, 0, existAccess);
   tgt->setNoAspects(call|bndchk|bitop1, ILTypeProp::Size_2, 0);
   tgt->setMinCounts(1, 0, 2);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitBeforeVersioning();
   return tgt;
   }




//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//*****************************************************************************************
// IL code generation for 2 if-statement version of comparing memory (using CLCL)
// Input: ImportantNode(0) - array load for src1
//        ImportantNode(1) - array load for src2
//        ImportantNode(2) - exit-if for checking the length
//        ImportantNode(3) - exit-if for comparing two arrays
//        ImportantNode(4) - increment the array index for src1
//        ImportantNode(5) - increment the array index for src2
//        ImportantNode(6) - the size of elements (NULL for byte arrays)
//
// Note: If we need to know the position where characters are different (flag generateArraycmplen),
//       we generate the CLCL instruction. Otherwise, we generate the CLC instruction.
//*****************************************************************************************
bool
CISCTransform2ArrayCmp2Ifs(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCmp2Ifs due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode *cmpIfAllCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2));
   TR::TreeTop *okDest = NULL;                // Target treetop of the array length check.
   TR_CISCNode *cmpneIfAllCISCNode[2];       // CISCNodes for the arraycmp checks.
   TR::TreeTop *topCmpIfNonEqual[2];          // Treetops of the arraycmp checks.
   TR::Node *cmpIfNonEqual[2];                // Nodes of the arraycmp checks.
   TR::TreeTop *failDest[2];                  // Target treetops of the arraycmp checks.

   int32_t count = 0;
   // Extract all the CISCNodes corresponding to the two exit if-stmts
   // for comparing the two arrays.
   ListIterator <TR_CISCNode> ci;
   ci.set(trans->getP2T() + P->getImportantNode(3)->getID());
   for (TR_CISCNode *c = ci.getFirst(); c; c = ci.getNext())
      {
      if (!c->isOutsideOfLoop())
         {
         // Checks exit-if for comparing two arrays
         switch(c->getOpcode())
            {
            case TR::ificmpgt:
            case TR::ificmplt:
            case TR::iflcmpgt:
            case TR::iflcmplt:
               if (count >= 2) return false;
               cmpneIfAllCISCNode[count] = c;
               topCmpIfNonEqual[count] = c->getHeadOfTrNodeInfo()->_treeTop;
               cmpIfNonEqual[count] = c->getHeadOfTrNodeInfo()->_node;
               failDest[count] = c->getDestination();
               count++;
               break;
            default:
               return false;
            }
         }
      }
   if (count != 2) return false;

   // Checks exit-if for checking the length
   switch(cmpIfAllCISCNode->getOpcode())
      {
      case TR::ificmpge:
         break;
      default:
         return false;
      }
   okDest = cmpIfAllCISCNode->getDestination();

   //
   // obtain a CISCNode of each store for incrementing induction variables
   TR_CISCNode *storeSrc1 = trans->getP2TRepInLoop(P->getImportantNode(4));
   TR_CISCNode *storeSrc2 = trans->getP2TRepInLoop(P->getImportantNode(5));
   if (!storeSrc2) storeSrc2 = storeSrc1;
   TR_ASSERT(storeSrc1 != NULL && storeSrc2 != NULL, "error");

   //
   // checking a set of all uses for each index
   TR_ASSERT(storeSrc1->getDagID() == storeSrc2->getDagID(), "error");

   TR_CISCNode *src1CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(0));
   TR_CISCNode *src2CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(1));
   if (!src1CISCNode || !src2CISCNode) return false;
   TR::Node * inSrc1Node = src1CISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inSrc2Node = src2CISCNode->getHeadOfTrNodeInfo()->_node;
   // check the indices used in the array loads and
   // the store nodes
   //
   if (!indicesAndStoresAreConsistent(comp, inSrc1Node, inSrc2Node, storeSrc1, storeSrc2))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction varaible updates\n", inSrc1Node, inSrc2Node);
      return false;
      }
   TR::Node * mulFactorNode;
   int32_t elementSize;

   // Get the size of elements
   if (!getMultiplier(trans, P->getImportantNode(6), &mulFactorNode, &elementSize, inSrc1Node->getType())) return false;
   if (elementSize != inSrc1Node->getSize() || elementSize != inSrc2Node->getSize())
      {
      traceMsg(comp, "CISCTransform2ArrayCmp2Ifs failed - Size Mismatch.  Element Size: %d InSrc1Size: %d inSrc2Size: %d\n", elementSize, inSrc1Node->getSize(), inSrc2Node->getSize());
      return false;     // Size is mismatch!
      }

   TR::Node *src1IdxRepNode, *src2IdxRepNode, *src1BaseRepNode, *src2BaseRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &src1IdxRepNode, &src2IdxRepNode, &src1BaseRepNode, &src2BaseRepNode, &variableORconstRepNode);
   if (!src2IdxRepNode) src2IdxRepNode = src1IdxRepNode;
   TR::SymbolReference * src1IdxSymRef = src1IdxRepNode->getSymbolReference();
   if (!trans->analyzeArrayIndex(src1IdxSymRef)) return false;
   TR::SymbolReference * src2IdxSymRef = src2IdxRepNode->getSymbolReference();
   TR::Node *start1Idx, *start2Idx, *end1Idx, *end2Idx, *diff2;
   TR_CISCNode *arrayindex0, *arrayindex1;
   arrayindex0 = trans->getP()->getCISCNode(TR_arrayindex, true, 0);
   bool indexOf = trans->isIndexOf();
   if (indexOf && arrayindex0)
      {
      // more analysis for String.indexOf(Ljava/lang/String;I)I
      TR_CISCNode *a0;
      ListIterator<TR_CISCNode> pi(arrayindex0->getParents());
      for (a0 = pi.getFirst(); a0; a0 = pi.getNext())
         {
         if (a0->getOpcode() == TR::isub)
            {
            if (trans->getP2TRepInLoop(a0)) arrayindex0 = a0;
            break;
            }
         }
      }
   arrayindex1 = trans->getP()->getCISCNode(TR_arrayindex, true, 1);

   bool useSrc1 = usedInLoopTest(comp, cmpIfAllCISCNode->getHeadOfTrNodeInfo()->_node, src1IdxSymRef);
   end2Idx = convertStoreToLoad(comp, variableORconstRepNode);
   start2Idx = convertStoreToLoad(comp, useSrc1 ? src1IdxRepNode : src2IdxRepNode);
   diff2 = createOP2(comp, TR::isub, end2Idx, start2Idx);
   start1Idx = convertStoreToLoad(comp, src1IdxRepNode);
   end1Idx = NULL;

   if (arrayindex0) start1Idx = trans->getP2TRep(arrayindex0)->getHeadOfTrNodeInfo()->_node;
   if (arrayindex1) start2Idx = trans->getP2TRep(arrayindex1)->getHeadOfTrNodeInfo()->_node;

   // Prepare effective addresses for arraycmplen
   TR::Node * input1Node = inSrc1Node->getChild(0)->duplicateTree();
   TR::Node * input2Node = inSrc2Node->getChild(0)->duplicateTree();
   TR::Node * lengthNode;
   lengthNode = diff2;

   // an extra compare is going to be generated after the arrayCmp
   // to determine where to branch. if the arrayCmp found a mismatch
   // between the two array elements, the induction variable will be
   // updated correctly and the extra compare will test the element
   // at the index where the mismatch occurred.
   // however, if the two arrays are the same, the arrayCmp will terminate
   // after searching lengthNode bytes causing the extra compare to test
   // the index at lengthNode+1 which is incorrect.
   //
   lengthNode = TR::Node::create(TR::isub, 2, lengthNode, TR::Node::create(mulFactorNode, TR::iconst, 0, 1));

   int shrCount = 0;
   TR::Node * elementSizeNode = NULL;
   if (elementSize > 1)
      {
      //FIXME: enable this code for 64-bit
      // currently disabled until all uses of lengthNode are
      // sign-extended correctly
      //
      TR::ILOpCodes mulOp = TR::imul;
#if 0
      if (comp->target().is64Bit())
         {
         elementSizeNode = TR::Node::create(mulFactorNode, TR::lconst);
         elementSizeNode->setLongInt(elementSize);
         lengthNode = TR::Node::create(TR::i2l, 1, lengthNode);
         mulOp = TR::lmul;
         }
      else
#endif
         elementSizeNode = TR::Node::create(mulFactorNode, TR::iconst, 0, elementSize);
      lengthNode = TR::Node::create(mulOp, 2,
                                   lengthNode,
                                   elementSizeNode);
      switch(elementSize)
         {
         case 2: shrCount = 1; break;
         case 4: shrCount = 2; break;
         case 8: shrCount = 3; break;
         default: TR_ASSERT(false, "error");
         }
      }

   // Currently, it is inserted by reorderTargetNodesInBB()
   bool isCompensateCode = !trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1);
   TR::Block *compensateBlock0[2];
   compensateBlock0[0] = compensateBlock0[1] = NULL;
   TR::Block *compensateBlock1 = NULL;

   // create two empty blocks for inserting compensation code prepared by reorderTargetNodesInBB()
   if (isCompensateCode)
      {
      compensateBlock0[0] = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      compensateBlock0[1] = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      compensateBlock0[0]->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest[0])));
      compensateBlock0[1]->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest[1])));
      compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
      failDest[0] = compensateBlock0[0]->getEntry();
      failDest[1] = compensateBlock0[1]->getEntry();
      okDest = compensateBlock1->getEntry();
      }
   TR_ASSERT(okDest != NULL && failDest[0] != NULL && failDest[1] != NULL, "error!");

   TR::Node * topArraycmp;
   TR::TreeTop * newFirstTreeTop[2];
   TR::TreeTop * newLastTreeTop[2];

   // Using the CLCL instruction
   lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode);
   TR::Node * arraycmplen = TR::Node::create(TR::arraycmp, 3, input1Node, input2Node, lengthNode);
   arraycmplen->setArrayCmpLen(true);
   arraycmplen->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCmpSymbol());

   TR::SymbolReference * resultSymRef = comp->getSymRefTab()->
      createTemporary(comp->getMethodSymbol(), TR::Int32);
   topArraycmp = TR::Node::createStore(resultSymRef, arraycmplen);

   TR::Node * resultLoad = TR::Node::createLoad(topArraycmp, resultSymRef);
   TR::Node * equalLen = resultLoad;
   if (shrCount != 0)
      {
      equalLen = TR::Node::create(TR::ishr, 2,
                                 equalLen,
                                 TR::Node::create(equalLen, TR::iconst, 0, shrCount));
      }

   TR::Node *tmpNode = createStoreOP2(comp, src1IdxSymRef, TR::iadd, src1IdxSymRef, equalLen, trNode);
   newFirstTreeTop[0] = TR::TreeTop::create(comp, tmpNode);
   newLastTreeTop[0] = newFirstTreeTop[0];
   TR::TreeTop * tmpTreeTop = NULL;

   if (src1IdxSymRef != src2IdxSymRef)
      {
      tmpNode = createStoreOP2(comp, src2IdxSymRef, TR::iadd, src2IdxSymRef, equalLen, trNode);
      tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
      newLastTreeTop[0]->join(tmpTreeTop);
      newLastTreeTop[0] = tmpTreeTop;
      }

   //
   // Generate 2 if-statements

   //  First One
   TR_CISCNode *ifChild[2];
   ifChild[0] = trans->getP2TInLoopAllowOptionalIfSingle(P->getImportantNode(3)->getChild(0));
   ifChild[1] = trans->getP2TInLoopAllowOptionalIfSingle(P->getImportantNode(3)->getChild(1));
   TR::DataType dataType = cmpIfNonEqual[0]->getChild(0)->getDataType();
   TR_ASSERT(dataType == TR::Int32 || dataType == TR::Int64, "error!");
   TR::SymbolReference * diffSymRef = comp->getSymRefTab()->
      createTemporary(comp->getMethodSymbol(), dataType);
   tmpNode = TR::Node::createStore(diffSymRef,
                                  TR::Node::create(dataType == TR::Int32 ? TR::isub : TR::lsub, 2,
                                                  ifChild[0]->getHeadOfTrNodeInfo()->_node,
                                                  ifChild[1]->getHeadOfTrNodeInfo()->_node));
   tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
   newLastTreeTop[0]->join(tmpTreeTop);
   newLastTreeTop[0] = tmpTreeTop;
   TR::Node * loadNode = convertStoreToLoad(comp, tmpNode);
   TR::Node * constNode;
   if (dataType == TR::Int32)
      {
      constNode = TR::Node::create(loadNode, TR::iconst, 0, 0);
      }
   else
      {
      constNode = TR::Node::create(loadNode, TR::lconst, 0, 0);
      constNode->setLongInt(0);
      }

   tmpNode = TR::Node::createif((TR::ILOpCodes)cmpneIfAllCISCNode[0]->getOpcode(),
                               loadNode,
                               constNode,
                               failDest[0]);
   tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
   newLastTreeTop[0]->join(tmpTreeTop);
   newLastTreeTop[0] = tmpTreeTop;

   //  Second One
   tmpNode = TR::Node::createif((TR::ILOpCodes)cmpneIfAllCISCNode[1]->getOpcode(),
                               loadNode->duplicateTree(),
                               constNode->duplicateTree(),
                               failDest[1]);
   tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
   newFirstTreeTop[1] = tmpTreeTop;
   newLastTreeTop[1] = newFirstTreeTop[1];

   // Transform CFG
   TR::CFG *cfg = comp->getFlowGraph();
   TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
   cfg->setStructure(NULL);
   TR::TreeTop *last;

   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);
   last = block->getLastRealTreeTop();
   last->join(trTreeTop);
   trTreeTop->setNode(topArraycmp);
   trTreeTop->join(newFirstTreeTop[0]);
   newLastTreeTop[0]->join(block->getExit());

   block = trans->insertAfterNodes(block);

   TR::Block *if1Block = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
   if1Block->getEntry()->join(newFirstTreeTop[1]);
   newLastTreeTop[1]->join(if1Block->getExit());
   if (orgNextTreeTop != NULL) {
      cfg->insertBefore(if1Block, orgNextTreeTop->getNode()->getBlock());
   } else {
      // Block returned by findFirstNode is the last BB of the method.
      cfg->addNode(if1Block);
   }
   cfg->join(block, if1Block);

   trans->setSuccessorEdges(if1Block,
                            okDest->getEnclosingBlock(),
                            failDest[1]->getEnclosingBlock());

   trans->setSuccessorEdges(block,
                            if1Block,
                            failDest[0]->getEnclosingBlock());

   block = if1Block;
   if (isCompensateCode)
      {
      TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
      TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
      compensateBlock0[0] = trans->insertAfterNodesIdiom(compensateBlock0[0], 0, true);
      compensateBlock0[1] = trans->insertAfterNodesIdiom(compensateBlock0[1], 0, true);
      compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);
      cfg->insertBefore(compensateBlock1, orgNextBlock);
      cfg->insertBefore(compensateBlock0[1], compensateBlock1);
      cfg->insertBefore(compensateBlock0[0], compensateBlock0[1]);
      cfg->join(block, compensateBlock0[0]);
      }

   return true;
   }



//*****************************************************************************************
// IL code generation for comparing memory (using CLC or CLCL)
// Input: ImportantNode(0) - array load for src1
//        ImportantNode(1) - array load for src2
//        ImportantNode(2) - exit-if for checking the length
//        ImportantNode(3) - exit-if for comparing two arrays
//        ImportantNode(4) - increment the array index for src1
//        ImportantNode(5) - increment the array index for src2
//        ImportantNode(6) - the size of elements (NULL for byte arrays)
//        ImportantNode(7) - additional node for analyzing MEMCMPCompareTo. Not used for the others.
//
// Note: If we need to know the position where characters are different (flag generateArraycmplen),
//       we generate the CLCL instruction. Otherwise, we generate the CLC instruction.
//*****************************************************************************************
bool
CISCTransform2ArrayCmp(TR_CISCTransformer *trans)
   {

   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   TR::Block *preHeader = NULL;
   if (isLoopPreheaderLastBlockInMethod(comp, block, &preHeader))
      {
      traceMsg(comp, "Bailing CISCTransform2ArrayCmp due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR_CISCNode *storeSrc1 = trans->getP2TRepInLoop(P->getImportantNode(4));
   TR_CISCNode *storeSrc2 = trans->getP2TRepInLoop(P->getImportantNode(5));
   if (!storeSrc2) storeSrc2 = storeSrc1;
   TR_ASSERT(storeSrc1 != NULL && storeSrc2 != NULL, "error");


   if (preHeader)
      {
      if (disptrace)
         traceMsg(comp, "found preheader to be %d\n", preHeader->getNumber());
      //
      // obtain a CISCNode of each store for incrementing induction variables

      //check if any of the loop indices are defined between the preheader first tree and the first node found to match idiom
      TR::Node * inStoreSrc1= storeSrc1->getHeadOfTrNodeInfo()->_node;
      TR::Node * inStoreSrc2= storeSrc2->getHeadOfTrNodeInfo()->_node;

      int32_t index1SymRefNum = inStoreSrc1->getSymbolReference()->getReferenceNumber();
      int32_t index2SymRefNum = inStoreSrc2->getSymbolReference()->getReferenceNumber();

      if (disptrace)
         traceMsg(comp, "searching for stores to loop indices between preheader first tree %p and first matching tree %p, looking for symrefnum %d %d\n", preHeader->getFirstRealTreeTop()->getNode(),trTreeTop->getNode(),index1SymRefNum,index2SymRefNum);


      TR::Node * tempNode;
      for (TR::TreeTop * tt = preHeader->getFirstRealTreeTop();tt && tt != trTreeTop; tt = tt->getNextRealTreeTop())
         {
         tempNode = tt->getNode();
         if (tempNode->getOpCode().isStore() && tempNode->getOpCode().hasSymbolReference() &&
            ((tempNode->getSymbolReference()->getReferenceNumber() == index1SymRefNum) ||
            (tempNode->getSymbolReference()->getReferenceNumber() == index2SymRefNum)))
            {
            traceMsg(comp, "Bailing CISCTransform2ArrayCmp due to unexpected store (%p) of one of the indices prior to the idiom\n",tempNode);
            return false;
            }
         }
      }

   TR_CISCNode *cmpIfAllCISCNode = trans->getP2TRepInLoop(P->getImportantNode(2));
   TR_CISCNode *cmpneIfAllCISCNode = trans->getP2TRepInLoop(P->getImportantNode(3));
   ListIterator <TR_CISCNode> ci;
   TR_CISCNode *c;
   bool isDecrement = false;
   bool needVersioned = false;
   bool addLength1 = true;
   TR::TreeTop *failDest = NULL, *okDest = NULL;
   bool generateArraycmplen;
   bool generateArraycmpsign;
   bool compareTo;
   bool indexOf = trans->isIndexOf();

   // The transformation can support two if-stmts: array comparison exit and one loop ending condition.
   TR_CISCGraph *T = trans->getT();
   if (T && T->getAspects()->getIfCount() > 2)
      {
      traceMsg(comp,"CISCTransform2ArrayCmp detected %d if-stmts in loop (> 2).  Not transforming.\n", T->getAspects()->getIfCount());
      return false;
      }

   // Checks exit-if for comparing two arrays
   switch(cmpneIfAllCISCNode->getOpcode())
      {
      case TR::ificmpne:
      case TR::ifbcmpne:
      case TR::ifscmpne:
      case TR::iflcmpne:
      case TR::ifacmpne:
      case TR::iffcmpne:
      case TR::ifdcmpne:
      case TR::iffcmpneu:
      case TR::ifdcmpneu:
         break;
      case TR::ificmpgt:
      case TR::ificmplt:
      case TR::iflcmpgt:
      case TR::iflcmplt:
         return CISCTransform2ArrayCmp2Ifs(trans);      // Use 2 if-statements version
      default:
         return false;
      }

   failDest = cmpneIfAllCISCNode->getDestination();

   // We will fail this pattern if the comparison 'exit' is in fact not an exit out of the loop
   if (trans->isBlockInLoopBody(failDest->getNode()->getBlock()))
      {
      if (disptrace)
         traceMsg(comp, "CISCTransform2ArrayCmp failing transformer, ifcmpall test branch does not exit the loop.\n");
      return false;
      }

   // Checks exit-if for checking the length
   switch(cmpIfAllCISCNode->getOpcode())
      {
      case TR::ificmplt:
         if (cmpIfAllCISCNode->isEmptyHint()) return false;
         c = cmpIfAllCISCNode->getHintChildren()->getListHead()->getData();
         if (c->getOpcode() != TR::iadd) return false;
         isDecrement = true;
         needVersioned = true;
         addLength1 = true;
         break;
      case TR::ificmple:{
         TR_CISCNode *child = cmpIfAllCISCNode->getChild(0);
         ci.set(child->getParents());
         for (c = ci.getFirst(); c; c = ci.getNext())
            if (c->getOpcode() == TR::iadd) break;
         if (!c) return false;
         isDecrement = true;
         needVersioned = true;
         addLength1 = true;
         break;}
      case TR::ificmpgt:
         isDecrement = false;
         needVersioned = false;
         addLength1 = true;
         break;
      case TR::ificmpge:
         isDecrement = false;
         needVersioned = false;
         addLength1 = false;
         break;
      default:
         return false;
      }

   okDest = cmpIfAllCISCNode->getDestination();

   //
   // checking a set of all uses for each index
   TR_ASSERT(storeSrc1->getDagID() == storeSrc2->getDagID(), "error");
   generateArraycmplen = false;
   generateArraycmpsign = false;
   if (storeSrc1 == storeSrc2)
      {
      if (!storeSrc1->checkDagIdInChains())
         {
         // there is an use outside of the loop.
         if (isDecrement)
            return false;
         else
            generateArraycmplen = true;
         }
      }
   else
      {
      if (!storeSrc1->checkDagIdInChains() || !storeSrc2->checkDagIdInChains())
         {
         // there is an use outside of the loop.
         if (isDecrement)
            return false;
         else
            generateArraycmplen = true;
         }
      }
   List<TR::TreeTop> compareIfs(comp->trMemory());
   if (true == (compareTo = trans->isCompareTo()))
      {
      if (!generateArraycmplen)
         {
         bool canConvertToArrayCmp = false;
         if (trans->canConvertArrayCmpSign(trans->getP2TRep(P->getImportantNode(7))->getHeadOfTrNode(),
                                           &compareIfs, &canConvertToArrayCmp))
            {
            if (!canConvertToArrayCmp)
               generateArraycmpsign = true;
            }
         else
            {
            generateArraycmplen = true;
            }
         }
      }

   TR_CISCNode *src1CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(0));
   TR_CISCNode *src2CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(1));
   if (!src1CISCNode || !src2CISCNode || src1CISCNode == src2CISCNode) return false;
   TR::Node * inSrc1Node = src1CISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inSrc2Node = src2CISCNode->getHeadOfTrNodeInfo()->_node;

   if (generateArraycmpsign)
      {
      if (!comp->cg()->getSupportsArrayCmpSign() ||
            !((inSrc1Node->getType().isIntegral() && src1CISCNode->getIlOpCode().isUnsigned()) || inSrc1Node->getType().isAddress()))
         {
         // arrayCmpLen can be reduced to arrayCmpSign, but either codegen does not support it
         // or we can't guarantee it works with byte-by-byte comparisons (only allow addresses and unsigned integrals)
         generateArraycmpsign = false;
         generateArraycmplen = true;
         }
      }

   // check the indices used in the array loads and
   // the store nodes
   //
   if (!indicesAndStoresAreConsistent(comp, inSrc1Node, inSrc2Node, storeSrc1, storeSrc2))
      {
      dumpOptDetails(comp, "indices used in array loads %p and %p are not consistent with the induction varaible updates\n", inSrc1Node, inSrc2Node);
      return false;
      }

   if (!areArraysInvariant(comp, inSrc1Node, inSrc2Node, T))
      {
      traceMsg(comp, "input array bases %p and %p are not invariant, no reduction\n", inSrc1Node, inSrc2Node);
      return false;
      }

   TR::Node * mulFactorNode;
   int elementSize;

   // Get the size of elements
   if (!getMultiplier(trans, P->getImportantNode(6), &mulFactorNode, &elementSize, inSrc1Node->getType())) return false;

   if (inSrc1Node->getType() != inSrc2Node->getType())
      {
      traceMsg(comp,
         "CISCTransform2ArrayCmp failed - Array access types differ. inSrc1: %s, inSrc2: %s\n",
         TR::DataType::getName(inSrc1Node->getType()),
         TR::DataType::getName(inSrc2Node->getType()));
      return false;     // Size is mismatch!
      }

   const uint32_t expectedSize = inSrc1Node->getType().isAddress()
      ? TR::Compiler->om.sizeofReferenceField()
      : inSrc1Node->getSize();

   if (elementSize != expectedSize)
      {
      traceMsg(comp,
         "CISCTransform2ArrayCmp failed - Size Mismatch.  Element Size: %d, Expected Size: %d\n",
         elementSize,
         expectedSize);
      return false;     // Size is mismatch!
      }

   TR::Node *src1IdxRepNode, *src2IdxRepNode, *src1BaseRepNode, *src2BaseRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &src1IdxRepNode, &src2IdxRepNode, &src1BaseRepNode, &src2BaseRepNode, &variableORconstRepNode);
   if (!src2IdxRepNode) src2IdxRepNode = src1IdxRepNode;
   TR::SymbolReference * src1IdxSymRef = src1IdxRepNode->getSymbolReference();
   if (!trans->analyzeArrayIndex(src1IdxSymRef)) return false;
   TR::SymbolReference * src2IdxSymRef = src2IdxRepNode->getSymbolReference();
   TR::Node *start1Idx, *start2Idx, *end1Idx, *end2Idx, *diff2;
   TR_CISCNode *arrayindex0, *arrayindex1;
   arrayindex0 = trans->getP()->getCISCNode(TR_arrayindex, true, 0);
   if (indexOf && arrayindex0)
      {
      // more analysis for String.indexOf(Ljava/lang/String;I)I
      TR_CISCNode *a0;
      ListIterator<TR_CISCNode> pi(arrayindex0->getParents());
      for (a0 = pi.getFirst(); a0; a0 = pi.getNext())
         {
         if (a0->getOpcode() == TR::isub)
            {
            if (trans->getP2TRepInLoop(a0)) arrayindex0 = a0;
            break;
            }
         }
      }
   arrayindex1 = trans->getP()->getCISCNode(TR_arrayindex, true, 1);

   bool useSrc1 = usedInLoopTest(comp, cmpIfAllCISCNode->getHeadOfTrNodeInfo()->_node, src1IdxSymRef);

   TR::Node * input1Node;
   TR::Node * input2Node;
   TR::Node *startNode = NULL;
   TR::Node *endNode = NULL;
   if (isDecrement)     // count-down loop
      {
      start2Idx = convertStoreToLoad(comp, variableORconstRepNode);
      end2Idx = convertStoreToLoad(comp, useSrc1 ? src1IdxRepNode : src2IdxRepNode);
      diff2 = createOP2(comp, TR::isub, end2Idx, start2Idx);
      end1Idx = convertStoreToLoad(comp, src1IdxRepNode);
      start1Idx = createOP2(comp, TR::isub, end1Idx, diff2);

      if (disptrace)
         traceMsg(comp, "isDecrement start1Idx %p start2Idx %p end1Idx %p end2Idx %p\n", start1Idx, start2Idx, end1Idx, end2Idx);
      startNode = start2Idx->duplicateTree();
      endNode = useSrc1 ? end1Idx->duplicateTree() : end2Idx->duplicateTree();

      if (arrayindex0) end1Idx = trans->getP2TRep(arrayindex0)->getHeadOfTrNodeInfo()->_node;
      if (arrayindex1) end2Idx = trans->getP2TRep(arrayindex1)->getHeadOfTrNodeInfo()->_node;
      input1Node = createArrayAddressTree(comp, ctrl, src1BaseRepNode, start1Idx, elementSize);
      input2Node = createArrayAddressTree(comp, ctrl, src2BaseRepNode, start2Idx, elementSize);
      }
   else
      {     // count-up loop
      end2Idx = convertStoreToLoad(comp, variableORconstRepNode);
      start2Idx = convertStoreToLoad(comp, useSrc1 ? src1IdxRepNode : src2IdxRepNode);
      diff2 = createOP2(comp, TR::isub, end2Idx, start2Idx);
      start1Idx = convertStoreToLoad(comp, src1IdxRepNode);
      end1Idx = needVersioned ? createOP2(comp, TR::iadd, start1Idx, diff2) : NULL;

      if (disptrace)
         traceMsg(comp, "start1Idx %p start2Idx %p end1Idx %p end2Idx %p\n", start1Idx, start2Idx, end1Idx, end2Idx);
      startNode = useSrc1 ? start1Idx->duplicateTree() : start2Idx->duplicateTree();
      endNode = end2Idx->duplicateTree();

      if (arrayindex0) start1Idx = trans->getP2TRep(arrayindex0)->getHeadOfTrNodeInfo()->_node;
      if (arrayindex1) start2Idx = trans->getP2TRep(arrayindex1)->getHeadOfTrNodeInfo()->_node;
      input1Node = inSrc1Node->getChild(0)->duplicateTree();
      input2Node = inSrc2Node->getChild(0)->duplicateTree();
      }

   // Prepare effective addresses for arraycmp(len)
   TR::Node * lengthNode;
   if (addLength1)
      {
      lengthNode = createOP2(comp, TR::isub,
                             diff2,
                             TR::Node::create(src1BaseRepNode, TR::iconst, 0, -1));
      }
   else
      {
      lengthNode = diff2;
      }

   int shrCount = 0;
   TR::Node * elementSizeNode = NULL;
   if (elementSize > 1)
      {
      //FIXME: enable this code for 64-bit
      // currently disabled until all uses of lengthNode are
      // sign-extended correctly
      //
      TR::ILOpCodes mulOp = TR::imul;
#if 0
      if (comp->target().is64Bit())
         {
         elementSizeNode = TR::Node::create(mulFactorNode, TR::lconst);
         elementSizeNode->setLongInt(elementSize);
         mulOp = TR::lmul;
         lengthNode = TR::Node::create(TR::i2l, 1, lengthNode);
         }
      else
#endif
         elementSizeNode = TR::Node::create(mulFactorNode, TR::iconst, 0, elementSize);
      lengthNode = TR::Node::create(mulOp, 2,
                                   lengthNode,
                                   elementSizeNode);
      switch(elementSize)
         {
         case 2: shrCount = 1; break;
         case 4: shrCount = 2; break;
         case 8: shrCount = 3; break;
         default: TR_ASSERT(false, "error");
         }
      }

   TR_ASSERT(!generateArraycmplen || !generateArraycmpsign, "error");

   // Prepare compensation code
   if (compareTo)
      {
      if (generateArraycmplen)
         {
         TR::Node *tmpNode;
         TR_CISCNode *storeResult = P->getImportantNode(7);
         tmpNode = trans->getP2TRep(storeResult)->getHeadOfTrNodeInfo()->_node->duplicateTree();
         trans->getAfterInsertionIdiomList(0)->add(tmpNode);

         tmpNode = TR::Node::createStore(tmpNode->getSymbolReference(),
                                        TR::Node::create(tmpNode, TR::iconst, 0, 0));
         trans->getAfterInsertionIdiomList(1)->add(tmpNode);
         }
      }

   bool isCompensateCode = !trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1);
   TR::Block *compensateBlock0 = NULL;
   TR::Block *compensateBlock1 = NULL;

   // create two empty blocks for inserting compensation code prepared by reorderTargetNodesInBB()
   if (isCompensateCode)
      {
      compensateBlock0 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      compensateBlock1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      compensateBlock0->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest)));
      compensateBlock1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, okDest)));
      failDest = compensateBlock0->getEntry();
      okDest = compensateBlock1->getEntry();
      }
   TR_ASSERT(okDest != NULL && failDest != NULL, "error!");

   TR::Node * topArraycmp;
   TR::TreeTop * newFirstTreeTop;
   TR::TreeTop * newLastTreeTop;

   TR::Node *storeCompareToResult = NULL;

   if (generateArraycmplen)
      {
      // Using the CLCL instruction

      TR::Node * arraycmplen = TR::Node::create(TR::arraycmp, 3, input1Node, input2Node, createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode));
      arraycmplen->setArrayCmpLen(true);
      arraycmplen->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCmpSymbol());

      TR::SymbolReference * resultSymRef = comp->getSymRefTab()->createTemporary(comp->getMethodSymbol(), TR::Int32);
      topArraycmp = TR::Node::createStore(resultSymRef, arraycmplen);

      TR::Node * resultLoad = TR::Node::createLoad(topArraycmp, resultSymRef);
      TR::Node * equalLen = resultLoad;
      if (shrCount != 0)
         {
         equalLen = TR::Node::create(TR::ishr, 2,
                                    equalLen,
                                    TR::Node::create(equalLen, TR::iconst, 0, shrCount));
         }

      TR::Node *tmpNode = createStoreOP2(comp, src1IdxSymRef, TR::iadd, src1IdxSymRef, equalLen, trNode);
      newFirstTreeTop = TR::TreeTop::create(comp, tmpNode);
      newLastTreeTop = newFirstTreeTop;
      TR::TreeTop * tmpTreeTop = NULL;

      if (src1IdxSymRef != src2IdxSymRef)
         {
         tmpNode = createStoreOP2(comp, src2IdxSymRef, TR::iadd, src2IdxSymRef, equalLen, trNode);
         tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
         newLastTreeTop->join(tmpTreeTop);
         newLastTreeTop = tmpTreeTop;
         }

      tmpNode = TR::Node::createif(TR::ificmpeq,
                                  lengthNode,
                                  resultLoad,
                                  okDest);
      tmpTreeTop = TR::TreeTop::create(comp, tmpNode);
      newLastTreeTop->join(tmpTreeTop);
      newLastTreeTop = tmpTreeTop;
      }
   else
      {
      // Using the CLC instruction
      TR::Node * arraycmp = TR::Node::create(TR::arraycmp, 3, input1Node, input2Node, createI2LIfNecessary(comp, trans->isGenerateI2L(), lengthNode));
      arraycmp->setSymbolReference(comp->getSymRefTab()->findOrCreateArrayCmpSymbol());

      TR::Node * cmpIfNode;
      if (compareTo)
         {
         storeCompareToResult = trans->getP2TRep(P->getImportantNode(7))->getHeadOfTrNode();
         if (generateArraycmpsign)
            {
            TR_ASSERT(comp->cg()->getSupportsArrayCmpSign(), "error");
            arraycmp->setArrayCmpSign(true);

            topArraycmp = TR::Node::createStore(storeCompareToResult->getSymbolReference(), arraycmp);
            cmpIfNode = TR::Node::createif(TR::ificmpeq, arraycmp,
                                          TR::Node::create( src1BaseRepNode, TR::iconst, 0, 0),
                                          okDest);
            }
         else
            {
            if (disptrace) traceMsg(comp, "ArrayCmp: Convert compareTo into equals!\n");
            topArraycmp = TR::Node::createStore(storeCompareToResult->getSymbolReference(),
                                               createOP2(comp, TR::isub,
                                                         TR::Node::create(src1BaseRepNode, TR::iconst, 0, 1),
                                                         TR::Node::create(TR::iand, 2, TR::Node::create(TR::ixor, 2, arraycmp, TR::Node::iconst(arraycmp, 1)), TR::Node::create(TR::ishr, 2, TR::Node::create(TR::ixor, 2, arraycmp, TR::Node::iconst(arraycmp, 2)), TR::Node::iconst(arraycmp, 1)))));

            cmpIfNode = TR::Node::createif(TR::ificmpeq, arraycmp,
                                          TR::Node::create( src1BaseRepNode, TR::iconst, 0, 0),
                                          okDest);

            }
         }
      else
         {
         topArraycmp = TR::Node::createStore(src1IdxSymRef, TR::Node::create(TR::iand, 2, TR::Node::create(TR::ixor, 2, arraycmp, TR::Node::iconst(arraycmp, 1)), TR::Node::create(TR::ishr, 2, TR::Node::create(TR::ixor, 2, arraycmp, TR::Node::iconst(arraycmp, 2)), TR::Node::iconst(arraycmp, 1))));
         cmpIfNode = TR::Node::createif(TR::ificmpeq, arraycmp,
                                       TR::Node::create( src1BaseRepNode, TR::iconst, 0, 0),
                                       okDest);
         }
      newFirstTreeTop = TR::TreeTop::create(comp, cmpIfNode);
      newLastTreeTop = newFirstTreeTop;
      }

   TR::TreeTop *last;

   if (needVersioned)   // Need to version the loop to eliminate array bounds checking
      {
      TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

      // making two versions (safe and non-safe).
      TR::CFG *cfg = comp->getFlowGraph();
      TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
      TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
      TR::Block *chkSrc1a = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *chkSrc1b = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *chkSrc2a = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *chkSrc2b = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *fastpath = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *slowpad  = block->split(trTreeTop, cfg, true);
      TR::Block *gotoBlock = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);

      TR::Node *cmp, *len1, *len2;
      len1 = TR::Node::create(TR::arraylength, 1,
                             convertStoreToLoad(comp, src1BaseRepNode));
      cmp = TR::Node::createif(TR::ifiucmpge, start1Idx->duplicateTree(), len1, slowpad->getEntry());
      chkSrc1a->getEntry()->insertAfter(TR::TreeTop::create(comp, cmp));
      cmp = TR::Node::createif(TR::ifiucmpge, end1Idx->duplicateTree(), len1->duplicateTree(), slowpad->getEntry());
      chkSrc1b->getEntry()->insertAfter(TR::TreeTop::create(comp, cmp));
      len2 = TR::Node::create(TR::arraylength, 1,
                             convertStoreToLoad(comp, src2BaseRepNode));
      cmp = TR::Node::createif(TR::ifiucmpge, start2Idx->duplicateTree(), len2, slowpad->getEntry());
      chkSrc2a->getEntry()->insertAfter(TR::TreeTop::create(comp, cmp));
      cmp = TR::Node::createif(TR::ifiucmpge, end2Idx->duplicateTree(), len2->duplicateTree(), slowpad->getEntry());
      chkSrc2b->getEntry()->insertAfter(TR::TreeTop::create(comp, cmp));

      TR::TreeTop * branchTreeTop = TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, failDest));
      gotoBlock->append(branchTreeTop);

      cfg->setStructure(NULL);
      cfg->insertBefore(gotoBlock, slowpad);
      cfg->insertBefore(fastpath, gotoBlock);
      cfg->insertBefore(chkSrc2b, fastpath);
      cfg->insertBefore(chkSrc2a, chkSrc2b);
      cfg->insertBefore(chkSrc1b, chkSrc2a);
      cfg->insertBefore(chkSrc1a, chkSrc1b);

      fastpath = trans->insertBeforeNodes(fastpath);
      last = fastpath->getLastRealTreeTop();
      TR::TreeTop *arrayCmpTreeTop = TR::TreeTop::create(comp, topArraycmp);
      last->join(arrayCmpTreeTop);
      arrayCmpTreeTop->join(newFirstTreeTop);
      newLastTreeTop->join(fastpath->getExit());
      fastpath = trans->insertAfterNodes(fastpath);

      if (isCompensateCode)
         {
         cfg->setStructure(NULL);
         TR::TreeTop * orgNextTreeTop = fastpath->getExit()->getNextTreeTop();
         TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
         compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);
         compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);
         cfg->insertBefore(compensateBlock1, orgNextBlock);
         cfg->insertBefore(compensateBlock0, compensateBlock1);
         cfg->join(fastpath, compensateBlock0);
         }

      fastpath->getExit()->join(gotoBlock->getEntry());
      trans->setSuccessorEdges(fastpath,
                               gotoBlock,
                               okDest->getEnclosingBlock());

      block->getExit()->join(chkSrc1a->getEntry());
      cfg->addEdge(block, chkSrc1a);
      cfg->removeEdge(block, slowpad);
      trans->setColdLoopBody();
      }
   else
      {
      // making only the bound-check free version
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree());
      block = trans->insertBeforeNodes(block);
      block->append(TR::TreeTop::create(comp, topArraycmp));
      last = block->getLastRealTreeTop();
      last->join(newFirstTreeTop);
      newLastTreeTop->join(block->getExit());

      block = trans->insertAfterNodes(block);

      if (isCompensateCode)
         {
         TR::CFG *cfg = comp->getFlowGraph();
         cfg->setStructure(NULL);
         TR::TreeTop * orgNextTreeTop = block->getExit()->getNextTreeTop();
         TR::Block *orgNextBlock = orgNextTreeTop->getNode()->getBlock();
         compensateBlock0 = trans->insertAfterNodesIdiom(compensateBlock0, 0, true);
         compensateBlock1 = trans->insertAfterNodesIdiom(compensateBlock1, 1, true);
         cfg->insertBefore(compensateBlock1, orgNextBlock);
         cfg->insertBefore(compensateBlock0, compensateBlock1);
         cfg->join(block, compensateBlock0);
         }

      trans->setSuccessorEdges(block,
                               failDest->getEnclosingBlock(),
                               okDest->getEnclosingBlock());
      }

   if (0 && isCompensateCode)
      {
      // create control flow as below
      // --start preheader--
      //   if (i reverseopcode N)
      //      goto compensateblock0
      // --end preheader--
      //   else
      //      arraycmp
      //      ...
      traceMsg(comp, "cmpifallciscnode %d ifcmpge %d\n", cmpIfAllCISCNode->getOpcode(), TR::ificmpge);
      TR::Node *compareNode = TR::Node::createif((TR::ILOpCodes)cmpIfAllCISCNode->getOpcode(), startNode, endNode, compensateBlock0->getEntry());
      TR::TreeTop *compareTree = TR::TreeTop::create(comp, compareNode);
      if (!preHeader)
         preHeader = trans->addPreHeaderIfNeeded(trans->getCurrentLoop());
      preHeader->append(compareTree);
      comp->getFlowGraph()->addEdge(preHeader, compensateBlock0);
      }

   return true;
   }

bool
CISCTransform2ArrayCmpCompareTo(TR_CISCTransformer *trans)
   {
   trans->setCompareTo();
   return CISCTransform2ArrayCmp(trans);
   }


bool
CISCTransform2ArrayCmpIndexOf(TR_CISCTransformer *trans)
   {
   trans->setIndexOf();
   return CISCTransform2ArrayCmp(trans);
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v2, end;
 v3[ ], v4[ ]; 	// char, int, float, long, and so on
while(true){
   ifcmpall (v3[v1], v4[v2] ) break;
   v1++;
   v2++;
   ifcmpall(v1, end) break;
}

Note 1: It allows that variables v1 and v2 are identical.
Note 2: The wildcard node ifcmpall matches all types of if-instructions.
****************************************************************************************/
TR_PCISCGraph *
makeMemCmpGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCmp", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v1); // array index for src1
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(v2); // array index for src2
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v3); // src1 array base
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v4); // src2 array base
   TR_PCISCNode *vorc1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 9,   0,   0);  tgt->addNode(vorc1);     // length
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 8,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 7,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  4,   0,   0);        tgt->addNode(iall);        // Multiply Factor
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n0  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, ent, TR_indload, TR::NoType,  v3, idx0, cmah0, iall);
   TR_PCISCNode *n1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n0, n0);  tgt->addNode(n1);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, n1, TR_indload, TR::NoType,  v4, idx1, cmah1, iall);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *ncmp= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n3, n1, n3);  tgt->addNode(ncmp);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ncmp, v1, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v2, cm1);
   TR_PCISCNode *ncmpge = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v1, vorc1);  tgt->addNode(ncmpge);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmpge->setSuccs(ent->getSucc(0), n9);
   ncmp->setSucc(1, n9);

   n1->setIsOptionalNode();
   n3->setIsOptionalNode();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setSpecialCareNode(0, ncmp); // exit-if due to a different character
   tgt->setImportantNodes(n0, n2, ncmpge, ncmp, n6, n7, iall);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCmp);
   tgt->setAspects(isub|mul, existAccess, 0);
   tgt->setNoAspects(call|bndchk|bitop1, 0, existAccess);
   tgt->setMinCounts(2, 2, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setInhibitBeforeVersioning();
   tgt->setHotness(warm, false);
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v2, end;
 v3[ ], v4[ ]; 	// char, int, float, long, and so on
while(true){
   ifcmpall (v3[v1], v4[v2] ) break;
   v1++;
   v2++;
   ifcmpall(v1, end) break;
}

Note 1: It allows that variables v1 and v2 are identical.
Note 2: The wildcard node ifcmpall matches all types of if-instructions.
****************************************************************************************/
TR_PCISCGraph *
makeMemCmpIndexOfGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCmpIndexOf", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    0);  tgt->addNode(v1); // array index for src1
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 11,   0,   0,    1);  tgt->addNode(v2); // array index for src2
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    0);  tgt->addNode(v3); // src1 array base
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(),  9,   0,   0,    1);  tgt->addNode(v4); // src2 array base
   TR_PCISCNode *vorc1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 8,   0,   0);  tgt->addNode(vorc1);     // length
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 7,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 6,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(), 4,   0,   0);        tgt->addNode(iall);        // Multiply Factor
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *a1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2, ent, idx0, cm1);  tgt->addNode(a1);
   TR_PCISCNode *n0  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, a1, TR_inbload, TR::NoType,  v3, a1, cmah, iall);
   a1->getHeadOfParents()->setIsChildDirectlyConnected();
   TR_PCISCNode *n1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n0, n0);  tgt->addNode(n1);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, n1, TR_inbload, TR::NoType,  v4, idx1, cmah, iall);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *ncmp= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n3, n1, n3);  tgt->addNode(ncmp);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ncmp, v1, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v2, cm1);
   TR_PCISCNode *ncmpge = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   n7, v1, vorc1);  tgt->addNode(ncmpge);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmpge->setSuccs(ent->getSucc(0), n9);
   ncmp->setSucc(1, n9);

   n1->setIsOptionalNode();
   n3->setIsOptionalNode();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setSpecialCareNode(0, ncmp); // exit-if due to a different character
   tgt->setImportantNodes(n0, n2, ncmpge, ncmp, n6, n7, iall);
   tgt->setNumDagIds(13);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCmpIndexOf);
   tgt->setAspects(isub|mul, existAccess, 0);
   tgt->setNoAspects(call|bndchk|bitop1, ILTypeProp::Size_1, existAccess);
   tgt->setMinCounts(2, 2, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setInhibitBeforeVersioning();
   tgt->setHotness(warm, false);
   return tgt;
   }


/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v2, end, v5;
 v3[ ], v4[ ]; 	// char, int, float, long, and so on
while(true){
   v5 = v3[v1++] - v4[v2++];
   if (v5 != 0) break;
   if (v1 >= end) break;
}

Note 1: It allows that variables v1 and v2 are identical.
****************************************************************************************/
TR_PCISCGraph *
makeMemCmpSpecialGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "MemCmpSpecial", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v1); // array index for src1
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(v2); // array index for src2
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(v3); // src1 array base
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 10,   0,   0,    1);  tgt->addNode(v4); // src2 array base
   TR_PCISCNode *vorc1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 9,   0,   0);  tgt->addNode(vorc1);     // length
   TR_PCISCNode *v5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  8,   0,   0,    2);  tgt->addNode(v5); // result
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 7,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  5,   0,   0);        tgt->addNode(iall);        // Multiply Factor
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  4,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *c0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,    0);  tgt->addNode(c0);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *n0  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, ent, TR_inbload, TR::NoType,  v3, v1, cmah, iall);
   TR_PCISCNode *n1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n0, n0);  tgt->addNode(n1);
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, n1, TR_inbload, TR::NoType,  v4, idx0, cmah, iall);
   TR_PCISCNode *n3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType,  tgt->incNumNodes(),  1,   1,   1,   n2, n2);  tgt->addNode(n3);
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,        tgt->incNumNodes(),  1,   1,   2,   n3, n1, n3);  tgt->addNode(n4);
   TR_PCISCNode *n5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,   n4, n4, v5);  tgt->addNode(n5);
   TR_PCISCNode *ncmp= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpne, TR::NoType,    tgt->incNumNodes(),  1,   2,   2,   n5, v5, c0);  tgt->addNode(ncmp);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, ncmp, v1, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v2, cm1);
   TR_PCISCNode *ncmpge= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,  n7, v1, vorc1);  tgt->addNode(ncmpge);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmpge->setSuccs(ent->getSucc(0), n9);
   ncmp->setSucc(1, n9);

   n1->setIsOptionalNode();
   n3->setIsOptionalNode();

   tgt->setSpecialCareNode(0, ncmp); // exit-if due to a different character
   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(n0, n2, ncmpge, ncmp, n6, n7, iall, n5);
   tgt->setNumDagIds(14);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2ArrayCmpCompareTo);
   tgt->setAspects(isub|mul, existAccess, 0);
   tgt->setNoAspects(call|bndchk|bitop1, ILTypeProp::Size_1, existAccess);
   tgt->setMinCounts(2, 2, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setInhibitBeforeVersioning();
   tgt->setHotness(warm, false);
   return tgt;
   }


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Utilities for BitOpMem

static void
setSubopBitOpMem(TR::Compilation *comp, TR::Node *bitOpMem, TR_CISCNode *opCISCNode)
   {
   if (opCISCNode->getIlOpCode().isAnd())
      {
      bitOpMem->setAndBitOpMem(true);
      }
   else if (opCISCNode->getIlOpCode().isXor())
      {
      bitOpMem->setXorBitOpMem(true);
      }
   else
      {
      TR_ASSERT(opCISCNode->getIlOpCode().isOr(), "error");
      bitOpMem->setOrBitOpMem(true);
      }
   }

static TR::AutomaticSymbol *
setPinningArray(TR::Compilation *comp, TR::Node *internalPtrStore, TR::Node *base, TR::Block *appendBlock)
   {
   TR::AutomaticSymbol *pinningArray = NULL;
   if (base->getOpCode().isLoadVarDirect() &&
       base->getSymbolReference()->getSymbol()->isAuto())
      {
      pinningArray = (base->getSymbolReference()->getSymbol()->castToAutoSymbol()->isInternalPointer()) ?
             base->getSymbolReference()->getSymbol()->castToInternalPointerAutoSymbol()->getPinningArrayPointer() :
             base->getSymbolReference()->getSymbol()->castToAutoSymbol();
      }
   else
      {
      TR::SymbolReference *newRef = comp->getSymRefTab()->createTemporary(comp->getMethodSymbol(), TR::Address);
      appendBlock->append(TR::TreeTop::create(comp, TR::Node::createStore(newRef, createLoad(base))));
      pinningArray = newRef->getSymbol()->castToAutoSymbol();
      }
   pinningArray->setPinningArrayPointer();
   internalPtrStore->getSymbolReference()->getSymbol()->castToInternalPointerAutoSymbol()->setPinningArrayPointer(pinningArray);
   if (internalPtrStore->isInternalPointer()) internalPtrStore->setPinningArrayPointer(pinningArray);
   return pinningArray;
   }

//*****************************************************************************************
// IL code generation for bit operations for memory to memory (dest = src1 op src2)
// Input: ImportantNode(0) - array load for src1
//        ImportantNode(1) - array load for src2
//        ImportantNode(2) - array store for dest
//        ImportantNode(3) - a bit operation (XOR, AND, or OR)
//        ImportantNode(4) - increment the array index for src1
//        ImportantNode(5) - increment the array index for src2
//        ImportantNode(6) - increment the array index for dest
//        ImportantNode(7) - the size of elements (NULL for byte arrays)
//*****************************************************************************************
// This transformer will generate the following code.
// if (dest.addr == src1.addr)
//    {
//    // dest and src1 are identical
//    bitOpMem(dest.addr, src2.addr, len); // three children (dest op= src2)
//    }
// else if (dest.addr == src2.addr)
//    {
//    // dest and src2 are identical
//    bitOpMem(dest.addr, src1.addr, len); // three children (dest op= src1)
//    }
// else if (dest.obj == src1.obj || dest.obj == src2.obj)
//    {
//    // the destination may overlap to src1 or src2.
//    <go to the original loop>
//    }
// else
//    {
//    // We can guarantee the destination NEVER overlaps to src1 or src2.
//    bitOpMem(dest.addr, src1.addr, src2.addr, len); // four children (dest = src1 op src2)
//    }
bool
CISCTransform2BitOpMem(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2BitOpMem due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;

   //
   // obtain a CISCNode of each store for incrementing induction variables
   TR_CISCNode *src1CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(0));
   TR_CISCNode *src2CISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(1));
   if (!src1CISCNode || !src2CISCNode || src1CISCNode == src2CISCNode) return false;
   TR_CISCNode *destCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(2));
   if (!destCISCNode) return false;
   TR_CISCNode *opCISCNode = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
   TR_ASSERT(opCISCNode, "error");
   TR::Node * inSrc1Node = src1CISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inSrc2Node = src2CISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inDestNode = destCISCNode->getHeadOfTrNodeInfo()->_node;
   TR::Node * inputNode1 = inSrc1Node->getChild(0);
   TR::Node * inputNode2 = inSrc2Node->getChild(0);
   TR::Node * outputNode = inDestNode->getChild(0);

   TR::Node * mulFactorNode;
   int elementSize;

   // Get the size of elements
   if (!getMultiplier(trans, P->getImportantNode(7), &mulFactorNode, &elementSize, inSrc1Node->getType())) return false;
   if (elementSize != inSrc1Node->getSize() || elementSize != inSrc2Node->getSize())
      {
      traceMsg(comp, "CISCTransform2BitOpMem failed - Size Mismatch.  Element Size: %d InSrc1Size: %d inSrc2Size: %d\n", elementSize, inSrc1Node->getSize(), inSrc2Node->getSize());
      return false;     // Size is mismatch!
      }

   TR_CISCNode *storeSrc1 = trans->getP2TRepInLoop(P->getImportantNode(4));
   TR_CISCNode *storeSrc2 = trans->getP2TRepInLoop(P->getImportantNode(5));
   TR_CISCNode *storeDest = trans->getP2TRepInLoop(P->getImportantNode(6));

   // check the indices used in the array loads and
   // the store nodes
   //
   List<TR::Node> storeList(comp->trMemory());
   TR_ASSERT(storeSrc1, "error");
   storeList.add(storeSrc1->getHeadOfTrNode());
   if (storeSrc2 && storeSrc2 != storeSrc1) storeList.add(storeSrc2->getHeadOfTrNode());
   if (storeDest && storeDest != storeSrc1) storeList.add(storeDest->getHeadOfTrNode());
   if (!isIndexVariableInList(inSrc1Node, &storeList) ||
       !isIndexVariableInList(inSrc2Node, &storeList) ||
       !isIndexVariableInList(inDestNode, &storeList))
      {
      dumpOptDetails(comp, "indices used in array loads %p, %p, and %p are not consistent with the induction varaible updates\n", inSrc1Node, inSrc2Node, inDestNode);
      return false;
      }

   TR::Node *src1IdxRepNode, *src2IdxRepNode, *destIdxRepNode, *src1BaseRepNode, *src2BaseRepNode, *destBaseRepNode, *variableORconstRepNode;
   getP2TTrRepNodes(trans, &src1IdxRepNode, &src2IdxRepNode, &destIdxRepNode, &src1BaseRepNode, &src2BaseRepNode, &destBaseRepNode, &variableORconstRepNode);
   TR_ASSERT(src1IdxRepNode != 0, "error");
   TR::SymbolReference * src1IdxSymRef = src1IdxRepNode->getSymbolReference();
   TR::SymbolReference * src2IdxSymRef = 0;
   TR::SymbolReference * destIdxSymRef = 0;
   if (src2IdxRepNode)
      src2IdxSymRef = src2IdxRepNode->getSymbolReference();
   if (destIdxRepNode)
      destIdxSymRef = destIdxRepNode->getSymbolReference();
   if (src1IdxSymRef == destIdxSymRef) destIdxSymRef = 0;
   if (src1IdxSymRef == src2IdxSymRef) src2IdxSymRef = 0;
   if (trans->countGoodArrayIndex(src1IdxSymRef) == 0) return false;
   if (src2IdxSymRef && (trans->countGoodArrayIndex(src2IdxSymRef) == 0)) return false;
   if (destIdxSymRef && (trans->countGoodArrayIndex(destIdxSymRef) == 0)) return false;
   TR::Node *startSrc1Idx, *endSrc1Idx, *diff2;
   endSrc1Idx = convertStoreToLoad(comp, variableORconstRepNode);
   startSrc1Idx = convertStoreToLoad(comp, src1IdxRepNode);
   diff2 = createOP2(comp, TR::isub, endSrc1Idx, startSrc1Idx);
   TR::Node * elementSizeNode = NULL;

   TR::Node * lengthNode = createI2LIfNecessary(comp, trans->isGenerateI2L(), diff2);
   if (elementSize > 1)
      {
      TR::ILOpCodes mulOp = TR::imul;
      if (comp->target().is64Bit())
         {
         elementSizeNode = TR::Node::create(mulFactorNode, TR::lconst);
         elementSizeNode->setLongInt(elementSize);
         mulOp = TR::lmul;
         }
      else
         elementSizeNode = TR::Node::create(mulFactorNode, TR::iconst, 0, elementSize);
      lengthNode = TR::Node::create(mulOp, 2,
                                   lengthNode,
                                   elementSizeNode);
      }

   TR::Node * src1Update = TR::Node::createStore(src1IdxSymRef, endSrc1Idx->duplicateTree());
   TR::Node * destUpdate = NULL;
   if (destIdxSymRef != NULL && src1IdxSymRef != destIdxSymRef)
      {
      // If there are two induction variables, we need to maintain the other one.
      TR::Node * result = createOP2(comp, TR::iadd,
                                   TR::Node::createLoad(trNode, destIdxSymRef),
                                   diff2->duplicateTree());
      destUpdate = TR::Node::createStore(destIdxSymRef, result);
      }
   TR::Node * src2Update = NULL;
   if (src2IdxSymRef != NULL && src2IdxSymRef != destIdxSymRef && src2IdxSymRef != src1IdxSymRef)
      {
      // If there are three induction variables, we need to maintain the other one.
      TR::Node * result = createOP2(comp, TR::iadd,
                                   TR::Node::createLoad(trNode, src2IdxSymRef),
                                   diff2->duplicateTree());
      src2Update = TR::Node::createStore(src2IdxSymRef, result);
      }

   TR::Node * bitOpMem = NULL;
   if (outputNode == inputNode1 || outputNode == inputNode2)
      {
      bitOpMem = TR::Node::create(TR::bitOpMem, 3,
                                 outputNode->duplicateTree(),
                                 (outputNode == inputNode1 ? inputNode2 : inputNode1)->duplicateTree(),
                                 lengthNode);
      bitOpMem->setSymbolReference(comp->getSymRefTab()->findOrCreatebitOpMemSymbol());
      setSubopBitOpMem(comp, bitOpMem, opCISCNode);
      }

   //********************
   // Modify actual code
   //********************
   if (bitOpMem)
      { // src1 or src2 is equal to dest
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, lengthNode->duplicateTree());
      block = trans->insertBeforeNodes(block);
      block->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, bitOpMem)));
      }
   else
      {
      TR::CFG *cfg = comp->getFlowGraph();
      cfg->setStructure(0);
      TR::Block *slowpad;
      TR::Block *orgPrevBlock = 0;
      TR::Block *checkSrc1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *fastpath1 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *checkSrc2 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *fastpath2 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *checkSrc3 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *checkSrc4 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *fastpath3 = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
      TR::Block *lastpath  = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);

      // find orgPrevBlock and slowpad
      if (block->getFirstRealTreeTop() == trTreeTop)
         {
         // search the entry pad
         orgPrevBlock = trans->searchPredecessorOfBlock(block);
         }

      slowpad = block;
      if (!orgPrevBlock)
         {
         orgPrevBlock = block;
         slowpad = block->split(trTreeTop, cfg, true);
         }

      // checkSrc1: if (dest.addr != src1.addr) goto checkSrc2
      TR::SymbolReference *destAddrSymRef = comp->getSymRefTab()->createTemporary(comp->getMethodSymbol(), TR::Address, true);
      TR::SymbolReference *src1AddrSymRef = comp->getSymRefTab()->createTemporary(comp->getMethodSymbol(), TR::Address, true);
      TR::SymbolReference *src2AddrSymRef = comp->getSymRefTab()->createTemporary(comp->getMethodSymbol(), TR::Address, true);
      TR::Node *destStore = TR::Node::createStore(destAddrSymRef, outputNode->duplicateTree());
      TR::Node *src1Store = TR::Node::createStore(src1AddrSymRef, inputNode1->duplicateTree());
      TR::Node *src2Store = TR::Node::createStore(src2AddrSymRef, inputNode2->duplicateTree());

      setPinningArray(comp, destStore, destBaseRepNode, checkSrc1);
      setPinningArray(comp, src1Store, src1BaseRepNode, checkSrc1);
      setPinningArray(comp, src2Store, src2BaseRepNode, checkSrc1);

      checkSrc1->append(TR::TreeTop::create(comp, destStore));
      checkSrc1->append(TR::TreeTop::create(comp, src1Store));
      checkSrc1->append(TR::TreeTop::create(comp, src2Store));
      checkSrc1->append(TR::TreeTop::create(comp, TR::Node::createif(TR::ifacmpne,
                                           TR::Node::createLoad(trNode, destAddrSymRef),
                                           TR::Node::createLoad(trNode, src1AddrSymRef),
                                           checkSrc2->getEntry())));

      // fastpath1: bitOpMem(dest, src2, length); goto lastpath;
      TR::Node *bitOpMem1 = TR::Node::create(TR::bitOpMem, 3,
                                           TR::Node::createLoad(trNode, destAddrSymRef),
                                           TR::Node::createLoad(trNode, src2AddrSymRef),
                                           lengthNode->duplicateTree());
      bitOpMem1->setSymbolReference(comp->getSymRefTab()->findOrCreatebitOpMemSymbol());
      setSubopBitOpMem(comp, bitOpMem1, opCISCNode);
      ///fastpath1 = trans->insertBeforeNodes(fastpath1);
      fastpath1->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, bitOpMem1)));
      fastpath1->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, lastpath->getEntry())));

      // checkSrc2: if (dest.addr != src2.addr) goto checkSrc3
      checkSrc2->append(TR::TreeTop::create(comp, TR::Node::createif(TR::ifacmpne,
                                           TR::Node::createLoad(trNode, destAddrSymRef),
                                           TR::Node::createLoad(trNode, src2AddrSymRef),
                                           checkSrc3->getEntry())));

      // fastpath2: bitOpMem(dest, src1, length); goto lastpath;
      TR::Node *bitOpMem2 = TR::Node::create(TR::bitOpMem, 3,
                                           TR::Node::createLoad(trNode, destAddrSymRef),
                                           TR::Node::createLoad(trNode, src1AddrSymRef),
                                           lengthNode->duplicateTree());
      bitOpMem2->setSymbolReference(comp->getSymRefTab()->findOrCreatebitOpMemSymbol());
      setSubopBitOpMem(comp, bitOpMem2, opCISCNode);
      ///fastpath2 = trans->insertBeforeNodes(fastpath2);
      fastpath2->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, bitOpMem2)));
      fastpath2->append(TR::TreeTop::create(comp, TR::Node::create(trNode, TR::Goto, 0, lastpath->getEntry())));

      // checkSrc3: if (dest.obj == src1.obj) goto slowpad
      checkSrc3->append(TR::TreeTop::create(comp, TR::Node::createif(TR::ifacmpeq,
                                           createLoad(destBaseRepNode),
                                           createLoad(src1BaseRepNode),
                                           slowpad->getEntry())));

      // checkSrc4: if (dest.obj == src2.obj) goto slowpad
      checkSrc4->append(TR::TreeTop::create(comp, TR::Node::createif(TR::ifacmpeq,
                                           createLoad(destBaseRepNode),
                                           createLoad(src2BaseRepNode),
                                           slowpad->getEntry())));

      // fastpath3: bitOpMem(dest, src1, src2, length);
      // We can guarantee the destination NEVER overlaps to src1 or src2.
      bitOpMem = TR::Node::create(TR::bitOpMem, 4,
                                 TR::Node::createLoad(trNode, destAddrSymRef),
                                 TR::Node::createLoad(trNode, src1AddrSymRef),
                                 TR::Node::createLoad(trNode, src2AddrSymRef),
                                 lengthNode->duplicateTree());
      bitOpMem->setSymbolReference(comp->getSymRefTab()->findOrCreatebitOpMemSymbol());
      setSubopBitOpMem(comp, bitOpMem, opCISCNode);
      ///fastpath3 = trans->insertBeforeNodes(fastpath3);
      fastpath3->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, bitOpMem)));

      // Insert new blocks
      TR::TreeTop * orgPrevTreeTop = orgPrevBlock->getExit();
      TR::Node *lastOrgPrevRealNode = orgPrevBlock->getLastRealTreeTop()->getNode();
      TR::TreeTop * orgNextTreeTop = orgPrevTreeTop->getNextTreeTop();
      if (orgNextTreeTop)
         {
         TR::Block * orgNextBlock = orgNextTreeTop->getNode()->getBlock();
         cfg->insertBefore(lastpath, orgNextBlock);
         }
      else
         {
         cfg->addNode(lastpath);
         }
      cfg->insertBefore(fastpath3, lastpath);
      cfg->insertBefore(checkSrc4, fastpath3);
      cfg->insertBefore(checkSrc3, checkSrc4);
      cfg->insertBefore(fastpath2, checkSrc3);
      cfg->insertBefore(checkSrc2, fastpath2);
      cfg->insertBefore(fastpath1, checkSrc2);
      cfg->insertBefore(checkSrc1, fastpath1);

      TR::Block *extraBlock = NULL;
      if (!trans->isEmptyBeforeInsertionList())
         {
         extraBlock = TR::Block::createEmptyBlock(trNode, comp, block->getFrequency(), block);
         cfg->insertBefore(extraBlock, checkSrc1);
         orgPrevTreeTop->join(extraBlock->getEntry());
         cfg->addEdge(orgPrevBlock, extraBlock);
         TR::Block *newBlock = trans->insertBeforeNodes(extraBlock);
         }
      else
         {
         orgPrevTreeTop->join(checkSrc1->getEntry());
         cfg->addEdge(orgPrevBlock, checkSrc1);
         }
      cfg->removeEdge(orgPrevBlock, slowpad);
      block = lastpath;

      if (disptrace) traceMsg(comp, "CISCTransform2BitOpMem: orgPrevBlock=%d checkSrc1=%d lastpath=%d slowpad=%d orgNextTreeTop=%x\n",
                          orgPrevBlock->getNumber(), checkSrc1->getNumber(), lastpath->getNumber(), slowpad->getNumber(), orgNextTreeTop);

      if (lastOrgPrevRealNode->getOpCode().getOpCodeValue() == TR::Goto)
         {
         TR_ASSERT(lastOrgPrevRealNode->getBranchDestination() == slowpad->getEntry(), "Error");
         if (!extraBlock)
            lastOrgPrevRealNode->setBranchDestination(checkSrc1->getEntry());
         else
            lastOrgPrevRealNode->setBranchDestination(extraBlock->getEntry());
         }
      }

   if (src2Update) block->append(TR::TreeTop::create(comp, src2Update));
   if (destUpdate) block->append(TR::TreeTop::create(comp, destUpdate));
   // Original value of first induction variable used in the updates of the two induction variables above
   // Update this one last
   block->append(TR::TreeTop::create(comp, src1Update));

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1, v2, end;
v3[ ], v4[ ], v5[ ];
while(true){
   v5[v2] = v3[v1] op v4[v1];   // op will match one of AND, OR, and XOR operations.
   v1++;
   v2++;
   if (v1 >= end) break;
}

Note 1: It allows that variables v1 and v2 are identical.
****************************************************************************************/
TR_PCISCGraph *
makeBitOpMemGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "BitOpMem", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 16,   0,   0,    0);  tgt->addNode(v1); // array index for src1
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    1);  tgt->addNode(v2); // array index for src2
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    2);  tgt->addNode(v3); // array index for dest
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 13,   0,   0,    0);  tgt->addNode(v4); // src1 array base
   TR_PCISCNode *v5  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 12,   0,   0,    1);  tgt->addNode(v5); // src2 array base
   TR_PCISCNode *v6  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 11,   0,   0,    2);  tgt->addNode(v6); // dest array base
   TR_PCISCNode *vorc1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),10,  0,   0);  tgt->addNode(vorc1);    // length
   TR_PCISCNode *iall= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  9,   0,   0);        tgt->addNode(iall);     // Multiply Factor
   TR_PCISCNode *idx0= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 8,   0,   0,    0);  tgt->addNode(idx0);
   TR_PCISCNode *idx1= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 7,   0,   0,    1);  tgt->addNode(idx1);
   TR_PCISCNode *idx2= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arrayindex, TR::NoType, tgt->incNumNodes(), 6,   0,   0,    2);  tgt->addNode(idx2);
   TR_PCISCNode *cmah0=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(cmah0);     // array header
   TR_PCISCNode *cmah1=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  4,   0,   0,    1);  tgt->addNode(cmah1);     // array header
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *sn0 = createIdiomArrayAddressInLoop(tgt, ctrl, 1, ent, v6, idx0, cmah1, iall);
   TR_PCISCNode *n0  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, sn0, TR_indload, TR::NoType,  v4, idx1, cmah0, iall);
   TR_PCISCNode *cv0 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n0, n0);  tgt->addNode(cv0);  // optional
   TR_PCISCNode *n2  = createIdiomArrayLoadInLoop(tgt, ctrl, 1, cv0, TR_indload, TR::NoType,  v5, idx2, cmah0, iall);
   TR_PCISCNode *cv1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n2, n2);  tgt->addNode(cv1);  // optional
   TR_PCISCNode *n4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_bitop1, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,   cv1, cv0, cv1);  tgt->addNode(n4);
   TR_PCISCNode *cv2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_conversion, TR::NoType, tgt->incNumNodes(), 1,   1,   1,   n4, n4);  tgt->addNode(cv2);  // optional
   TR_PCISCNode *sn1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_indstore, TR::NoType,  tgt->incNumNodes(),  1,   1,   2,   cv2, sn0, cv2); tgt->addNode(sn1);
   TR_PCISCNode *n6  = createIdiomDecVarInLoop(tgt, ctrl, 1, sn1, v1, cm1);
   TR_PCISCNode *n7  = createIdiomDecVarInLoop(tgt, ctrl, 1, n6, v2, cm1);
   TR_PCISCNode *n8  = createIdiomDecVarInLoop(tgt, ctrl, 1, n7, v3, cm1);
   TR_PCISCNode *ncmpge= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpge, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,  n8, v1, vorc1);  tgt->addNode(ncmpge);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmpge->setSuccs(ent->getSucc(0), n9);

   cv0->setIsOptionalNode();
   cv1->setIsOptionalNode();
   cv2->setIsOptionalNode();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(n0, n2, sn1, n4, n6, n7, n8, iall);
   tgt->setNumDagIds(17);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2BitOpMem);
   tgt->setAspects(isub|mul|sameTypeLoadStore|bitop1, existAccess, existAccess);
   tgt->setNoAspects(call|bndchk, 0, 0);
   tgt->setMinCounts(1, 2, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setInhibitBeforeVersioning();
   tgt->setHotness(warm, false);
   return tgt;
   }



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Counts number of digits (Not count the character '-' (minus))
//
//       e.g.   do { count ++; } while((l /= 10) != 0);
//
// numDigit = countdigit10(int i, void *work)
// numDigit = countdigit10(long i, void *work)
//
// Use of work area depends on each platform. (e.g. 16 bytes for zSeries)
// The work area for some platforms may be NULL.
static const int64_t digit10Table[] =
   {
   -10L,                // 0
   -100L,               // 1
   -1000L,              // 2
   -10000L,             // 3
   -100000L,            // 4
   -1000000L,           // 5
   -10000000L,          // 6
   -100000000L,         // 7
   -1000000000L         // 8 (32-bit)
#ifdef TR_TARGET_64BIT
   ,-10000000000L,       // 9
   -100000000000L,      // 10
   -1000000000000L,     // 11
   -10000000000000L,    // 12
   -100000000000000L,   // 13
   -1000000000000000L,  // 14
   -10000000000000000L, // 15
   -100000000000000000L,// 16
   -1000000000000000000L// 17 (64-bit)
#endif
   };

#if 0
struct ppcDigit10TableEnt
   {
   int32_t	digits;
   uint32_t	limit;		// 10^digits-1
   uint64_t	limitLong;	// 10^digits-1
   };

// For CountDecimalDigitInt, use ppcDigit10Table[32..64]
static const struct ppcDigit10TableEnt ppcDigit10Table[64 + 1] =
   {
   //digits   limit               limitLong  zeros32 zeros64           min                max       limit         limitLong incr
   //---------------------------------------------------------------------------------------------------------------------------
   {19,          0u, 9999999999999999999llu}, //  0  0 [0x8000000000000000,0xffffffffffffffff] 0x00000000 0x8ac7230489e7ffff *
   {19,          0u, 9999999999999999999llu}, //  0  1 [0x4000000000000000,0x7fffffffffffffff] 0x00000000 0x8ac7230489e7ffff
   {19,          0u, 9999999999999999999llu}, //  0  2 [0x2000000000000000,0x3fffffffffffffff] 0x00000000 0x8ac7230489e7ffff
   {19,          0u, 9999999999999999999llu}, //  0  3 [0x1000000000000000,0x1fffffffffffffff] 0x00000000 0x8ac7230489e7ffff
   {18,          0u,  999999999999999999llu}, //  0  4 [0x0800000000000000,0x0fffffffffffffff] 0x00000000 0x0de0b6b3a763ffff *
   {18,          0u,  999999999999999999llu}, //  0  5 [0x0400000000000000,0x07ffffffffffffff] 0x00000000 0x0de0b6b3a763ffff
   {18,          0u,  999999999999999999llu}, //  0  6 [0x0200000000000000,0x03ffffffffffffff] 0x00000000 0x0de0b6b3a763ffff
   {17,          0u,   99999999999999999llu}, //  0  7 [0x0100000000000000,0x01ffffffffffffff] 0x00000000 0x016345785d89ffff *
   {17,          0u,   99999999999999999llu}, //  0  8 [0x0080000000000000,0x00ffffffffffffff] 0x00000000 0x016345785d89ffff
   {17,          0u,   99999999999999999llu}, //  0  9 [0x0040000000000000,0x007fffffffffffff] 0x00000000 0x016345785d89ffff
   {16,          0u,    9999999999999999llu}, //  0 10 [0x0020000000000000,0x003fffffffffffff] 0x00000000 0x002386f26fc0ffff *
   {16,          0u,    9999999999999999llu}, //  0 11 [0x0010000000000000,0x001fffffffffffff] 0x00000000 0x002386f26fc0ffff
   {16,          0u,    9999999999999999llu}, //  0 12 [0x0008000000000000,0x000fffffffffffff] 0x00000000 0x002386f26fc0ffff
   {16,          0u,    9999999999999999llu}, //  0 13 [0x0004000000000000,0x0007ffffffffffff] 0x00000000 0x002386f26fc0ffff
   {15,          0u,     999999999999999llu}, //  0 14 [0x0002000000000000,0x0003ffffffffffff] 0x00000000 0x00038d7ea4c67fff *
   {15,          0u,     999999999999999llu}, //  0 15 [0x0001000000000000,0x0001ffffffffffff] 0x00000000 0x00038d7ea4c67fff
   {15,          0u,     999999999999999llu}, //  0 16 [0x0000800000000000,0x0000ffffffffffff] 0x00000000 0x00038d7ea4c67fff
   {14,          0u,      99999999999999llu}, //  0 17 [0x0000400000000000,0x00007fffffffffff] 0x00000000 0x00005af3107a3fff *
   {14,          0u,      99999999999999llu}, //  0 18 [0x0000200000000000,0x00003fffffffffff] 0x00000000 0x00005af3107a3fff
   {14,          0u,      99999999999999llu}, //  0 19 [0x0000100000000000,0x00001fffffffffff] 0x00000000 0x00005af3107a3fff
   {13,          0u,       9999999999999llu}, //  0 20 [0x0000080000000000,0x00000fffffffffff] 0x00000000 0x000009184e729fff *
   {13,          0u,       9999999999999llu}, //  0 21 [0x0000040000000000,0x000007ffffffffff] 0x00000000 0x000009184e729fff
   {13,          0u,       9999999999999llu}, //  0 22 [0x0000020000000000,0x000003ffffffffff] 0x00000000 0x000009184e729fff
   {13,          0u,       9999999999999llu}, //  0 23 [0x0000010000000000,0x000001ffffffffff] 0x00000000 0x000009184e729fff
   {12,          0u,        999999999999llu}, //  0 24 [0x0000008000000000,0x000000ffffffffff] 0x00000000 0x000000e8d4a50fff *
   {12,          0u,        999999999999llu}, //  0 25 [0x0000004000000000,0x0000007fffffffff] 0x00000000 0x000000e8d4a50fff
   {12,          0u,        999999999999llu}, //  0 26 [0x0000002000000000,0x0000003fffffffff] 0x00000000 0x000000e8d4a50fff
   {11,          0u,         99999999999llu}, //  0 27 [0x0000001000000000,0x0000001fffffffff] 0x00000000 0x000000174876e7ff *
   {11,          0u,         99999999999llu}, //  0 28 [0x0000000800000000,0x0000000fffffffff] 0x00000000 0x000000174876e7ff
   {11,          0u,         99999999999llu}, //  0 29 [0x0000000400000000,0x00000007ffffffff] 0x00000000 0x000000174876e7ff
   {10,          0u,          9999999999llu}, //  0 30 [0x0000000200000000,0x00000003ffffffff] 0x00000000 0x00000002540be3ff *
   {10,          0u,          9999999999llu}, //  0 31 [0x0000000100000000,0x00000001ffffffff] 0x00000000 0x00000002540be3ff
   {10, 4294967295u,          9999999999llu}, //  0 32 [0x0000000080000000,0x00000000ffffffff] 0xffffffff 0x00000002540be3ff
   {10, 4294967295u,          9999999999llu}, //  1 33 [0x0000000040000000,0x000000007fffffff] 0xffffffff 0x00000002540be3ff
   { 9,  999999999u,           999999999llu}, //  2 34 [0x0000000020000000,0x000000003fffffff] 0x3b9ac9ff 0x000000003b9ac9ff *
   { 9,  999999999u,           999999999llu}, //  3 35 [0x0000000010000000,0x000000001fffffff] 0x3b9ac9ff 0x000000003b9ac9ff
   { 9,  999999999u,           999999999llu}, //  4 36 [0x0000000008000000,0x000000000fffffff] 0x3b9ac9ff 0x000000003b9ac9ff
   { 8,   99999999u,            99999999llu}, //  5 37 [0x0000000004000000,0x0000000007ffffff] 0x05f5e0ff 0x0000000005f5e0ff *
   { 8,   99999999u,            99999999llu}, //  6 38 [0x0000000002000000,0x0000000003ffffff] 0x05f5e0ff 0x0000000005f5e0ff
   { 8,   99999999u,            99999999llu}, //  7 39 [0x0000000001000000,0x0000000001ffffff] 0x05f5e0ff 0x0000000005f5e0ff
   { 7,    9999999u,             9999999llu}, //  8 40 [0x0000000000800000,0x0000000000ffffff] 0x0098967f 0x000000000098967f *
   { 7,    9999999u,             9999999llu}, //  9 41 [0x0000000000400000,0x00000000007fffff] 0x0098967f 0x000000000098967f
   { 7,    9999999u,             9999999llu}, // 10 42 [0x0000000000200000,0x00000000003fffff] 0x0098967f 0x000000000098967f
   { 7,    9999999u,             9999999llu}, // 11 43 [0x0000000000100000,0x00000000001fffff] 0x0098967f 0x000000000098967f
   { 6,     999999u,              999999llu}, // 12 44 [0x0000000000080000,0x00000000000fffff] 0x000f423f 0x00000000000f423f *
   { 6,     999999u,              999999llu}, // 13 45 [0x0000000000040000,0x000000000007ffff] 0x000f423f 0x00000000000f423f
   { 6,     999999u,              999999llu}, // 14 46 [0x0000000000020000,0x000000000003ffff] 0x000f423f 0x00000000000f423f
   { 5,      99999u,               99999llu}, // 15 47 [0x0000000000010000,0x000000000001ffff] 0x0001869f 0x000000000001869f *
   { 5,      99999u,               99999llu}, // 16 48 [0x0000000000008000,0x000000000000ffff] 0x0001869f 0x000000000001869f
   { 5,      99999u,               99999llu}, // 17 49 [0x0000000000004000,0x0000000000007fff] 0x0001869f 0x000000000001869f
   { 4,       9999u,                9999llu}, // 18 50 [0x0000000000002000,0x0000000000003fff] 0x0000270f 0x000000000000270f *
   { 4,       9999u,                9999llu}, // 19 51 [0x0000000000001000,0x0000000000001fff] 0x0000270f 0x000000000000270f
   { 4,       9999u,                9999llu}, // 20 52 [0x0000000000000800,0x0000000000000fff] 0x0000270f 0x000000000000270f
   { 4,       9999u,                9999llu}, // 21 53 [0x0000000000000400,0x00000000000007ff] 0x0000270f 0x000000000000270f
   { 3,        999u,                 999llu}, // 22 54 [0x0000000000000200,0x00000000000003ff] 0x000003e7 0x00000000000003e7 *
   { 3,        999u,                 999llu}, // 23 55 [0x0000000000000100,0x00000000000001ff] 0x000003e7 0x00000000000003e7
   { 3,        999u,                 999llu}, // 24 56 [0x0000000000000080,0x00000000000000ff] 0x000003e7 0x00000000000003e7
   { 2,         99u,                  99llu}, // 25 57 [0x0000000000000040,0x000000000000007f] 0x00000063 0x0000000000000063 *
   { 2,         99u,                  99llu}, // 26 58 [0x0000000000000020,0x000000000000003f] 0x00000063 0x0000000000000063
   { 2,         99u,                  99llu}, // 27 59 [0x0000000000000010,0x000000000000001f] 0x00000063 0x0000000000000063
   { 1,          9u,                   9llu}, // 28 60 [0x0000000000000008,0x000000000000000f] 0x00000009 0x0000000000000009 *
   { 1,          9u,                   9llu}, // 29 61 [0x0000000000000004,0x0000000000000007] 0x00000009 0x0000000000000009
   { 1,          9u,                   9llu}, // 30 62 [0x0000000000000002,0x0000000000000003] 0x00000009 0x0000000000000009
   { 1,          9u,                   9llu}, // 31 63 [0x0000000000000001,0x0000000000000001] 0x00000009 0x0000000000000009
   { 1,          9u,                   9llu}, // 32 64 [0x0000000000000000,0xffffffffffffffff] 0x00000009 0x0000000000000009 *
};
#endif

static TR::SymbolReference *
getSymrefDigit10(TR::Compilation *comp, TR::Node *trNode)
   {
   if (comp->target().cpu.isZ())
      {
      return comp->getSymRefTab()->createKnownStaticDataSymbolRef((void *)digit10Table, TR::Address);
      }

   return NULL;
   }

static TR::Node *
createNodeLoadDigit10Table(TR::Compilation *comp, TR::Node *trNode)
   {
   TR_ASSERT(trNode->getDataType() == TR::Int32 || trNode->getDataType() == TR::Int64, "Unexpected datatype for trNode for CountDigits10.");
   TR::SymbolReference *symRef = getSymrefDigit10(comp, trNode);
   return symRef ? TR::Node::createWithSymRef(trNode, TR::loadaddr, 0, symRef) :
                   TR::Node::create(trNode, TR::aconst, 0, 0);
   }

//*****************************************************************************************
// IL code generation for counting digits
// The IL node TR_countdigit10 will find the number of digits by using a binary search, which
// uses the above table "digit10Table".
//
// Input: ImportantNode(0) - if node
//*****************************************************************************************
bool
CISCTransform2CountDecimalDigit(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   const bool disptrace = DISPTRACE(trans);
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2CountDecimalDigit due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;
   TR_CISCNode *ifcmp = trans->getP2TInLoopIfSingle(P->getImportantNode(0));
   TR_ASSERT(ifcmp, "error!");
   TR_CISCNode *constNode = ifcmp->getChild(1);
   if (!constNode->getIlOpCode().isLoadConst())
      {
      if (disptrace) traceMsg(comp, "%p is not isLoadConst().\n",constNode);
      return false;
      }

   TR::Node *countVarRepNode, *inputVarRepNode;
   getP2TTrRepNodes(trans, &countVarRepNode, &inputVarRepNode);
   TR::SymbolReference * countVarSymRef = countVarRepNode->getSymbolReference();
   TR::SymbolReference * inputVarSymRef = inputVarRepNode->getSymbolReference();
   TR::Node *countVar, *inputVar;
   TR::Node *workNode, *digitNode;
   countVar = createLoad(countVarRepNode);
   inputVar = createLoad(inputVarRepNode);

   TR_ASSERT(inputVar->getDataType() == TR::Int32 || inputVar->getDataType() == TR::Int64, "error");


   // The countDigitsEvaluator does not handle long (register pairs) on 31-bit.
   if (inputVar->getDataType() == TR::Int64 && (!comp->target().cpu.isPower() && comp->target().is32Bit()))
      {
      return false;
      }

   TR::Node *versionNode = 0;
   int modificationResult = 0;
   switch(ifcmp->getOpcode())
      {
      case TR::ificmpeq:
      case TR::iflcmpeq:
         if (constNode->getOtherInfo() != 0)
            {
            if (disptrace) traceMsg(comp, "The exit-if is TR::if*cmpeq but the constant value is %d.\n",constNode->getOtherInfo());
            return false;
            }
         break;
      case TR::ificmplt:
      case TR::iflcmplt:
         if (constNode->getOtherInfo() != 10)
            {
            if (disptrace) traceMsg(comp, "The exit-if is TR::if*cmplt but the constant value is %d.\n",constNode->getOtherInfo());
            return false;
            }
         versionNode = TR::Node::createif((TR::ILOpCodes)ifcmp->getOpcode(), inputVar->duplicateTree(),
                                         constNode->getHeadOfTrNode()->duplicateTree());
         modificationResult = -1;
         break;
      default:
         if (disptrace) traceMsg(comp, "The exit-if %p is not as expected. We may be able to implement this case.\n",ifcmp);
         return false;
      }

   //workNode = createNodeLoadDigit10Table(comp, trNode);
   workNode = createNodeLoadDigit10Table(comp, inputVarRepNode);

   digitNode = TR::Node::create(trNode, TR::countDigits, 2);
   digitNode->setAndIncChild(0, inputVar);
   digitNode->setAndIncChild(1, workNode);
   if (modificationResult != 0)
      {
      digitNode = createOP2(comp, TR::isub, digitNode,
                            TR::Node::create(digitNode, TR::iconst, 0, -modificationResult));
      }

   TR::Node *top = TR::Node::createStore(countVarSymRef,
                                       createOP2(comp, TR::iadd, countVar, digitNode));

   // Insert nodes and maintain the CFG
   if (versionNode)
      {
      List<TR::Node> guardList(comp->trMemory());
      guardList.add(versionNode);
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, &guardList);
      }
   else
      {
      block = trans->modifyBlockByVersioningCheck(block, trTreeTop, (List<TR::Node>*)0);
      }

   block = trans->insertBeforeNodes(block);
   block->append(TR::TreeTop::create(comp, top));
   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program
int v1;
long v2;
while(true){
   v1++;
   v2 = v2 / 10;
   if (v2 == 0) break;
}

Note 1: This idiom already supported both division and multiplication versions.
****************************************************************************************/
TR_PCISCGraph *
makeCountDecimalDigitLongGraph(TR::Compilation *c, int32_t ctrl, bool isDiv2Mul)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CountDecimalDigitLong", 0, 16);
   TR_PCISCNode *ent, *ncmp, *v2, *cexit, *n9, *ndiv;
   if (isDiv2Mul)
      {
      /************************************    opc               id        dagId #cfg #child other/pred/children */
      TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(v1); // count
                    v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  7,   0,   0,    1);  tgt->addNode(v2); // long var
                    cexit=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  6,   0,   0);  tgt->addNode(cexit);    // all constant
      TR_PCISCNode *c2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  5,   0,   0,    2);  tgt->addNode(c2); // iconst 2
      TR_PCISCNode *c63 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  4,   0,   0,   63);  tgt->addNode(c63);// iconst 63 (optional)
      TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
                    ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
      TR_PCISCNode *n1  = createIdiomDecVarInLoop(tgt, ctrl, 1, ent, v1, cm1);
      TR_PCISCNode *mag = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst  , TR::Int64,  tgt->incNumNodes(),  1,   1,   0,   n1);  tgt->addNode(mag);      // lconst 7378697629483820647
      TR_PCISCNode *nmul= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lmulh    , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   mag, v2, mag);   tgt->addNode(nmul);
      TR_PCISCNode *nshr= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lshr     , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   nmul, nmul, c2);   tgt->addNode(nshr);
      TR_PCISCNode *ushr= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lushr    , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   nshr, v2, c63);   tgt->addNode(ushr);       // optional
                    ndiv= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ladd     , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   ushr, nshr, ushr);   tgt->addNode(ndiv);    // optional
      c63->setIsOptionalNode();
      ushr->setIsOptionalNode();
      ushr->setSkipParentsCheck();
      ndiv->setIsOptionalNode();
      tgt->setNumDagIds(9);
      tgt->setAspects(isub|mul|shr);
      }
   else
      {
      /************************************    opc               id        dagId #cfg #child other/pred/children */
      TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(v1); // count
                    v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(v2); // long var
                    cexit=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  5,   0,   0);  tgt->addNode(cexit);    // all constant
      TR_PCISCNode *c10 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst  , TR::Int64,  tgt->incNumNodes(),  4,   0,   0,   10);  tgt->addNode(c10);// lconst 10
      TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
                    ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
      TR_PCISCNode *n1  = createIdiomDecVarInLoop(tgt, ctrl, 1, ent, v1, cm1);
                    ndiv= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ldiv     , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   n1, v2, c10);   tgt->addNode(ndiv);
      tgt->setNumDagIds(8);
      tgt->setAspects(isub|division);
      }
   TR_PCISCNode *nst = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lstore , TR::Int64,   tgt->incNumNodes(),  1,   1,   2,   ndiv, ndiv, v2); tgt->addNode(nst);
                 ncmp= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   nst, v2, cexit);  tgt->addNode(ncmp);
                 n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmp->setSuccs(ent->getSucc(0), n9);

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setImportantNode(0, ncmp);
   tgt->setTransformer(CISCTransform2CountDecimalDigit);
   tgt->setInhibitAfterVersioning();
   tgt->setNoAspects(call|bndchk, existAccess, existAccess);
   tgt->setMinCounts(1, 0, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   return tgt;
   }

/****************************************************************************************
Corresponding Java-like Pseudo Program	(Division version)
int v1, v2;
while(true){
   v1++;
   v2 = v2 / 10;
   if (v2 == 0) break;
}

Note 1: This idiom already supported both division and multiplication versions.
****************************************************************************************/
// Division is converted to multiply
TR_PCISCGraph *
makeCountDecimalDigitIntGraph(TR::Compilation *c, int32_t ctrl, bool isDiv2Mul)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "CountDecimalDigitInt", 0, 16);
   TR_PCISCNode *ent, *ncmp, *v2, *cexit, *n9, *ndiv;
   if (isDiv2Mul)
      {
      /************************************    opc               id        dagId #cfg #child other/pred/children */
      TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  9,   0,   0,    0);  tgt->addNode(v1);      // count
                    v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  8,   0,   0,    1);  tgt->addNode(v2);      // int var
                    cexit=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  7,   0,   0);        tgt->addNode(cexit);   // all constant
      TR_PCISCNode *c2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  6,   0,   0,    2);  tgt->addNode(c2);      // iconst 2
      TR_PCISCNode *c31 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  5,   0,   0,   31);  tgt->addNode(c31);     // iconst 31 (optional)
      TR_PCISCNode *mag = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  4,   0,   0,   1717986919);  tgt->addNode(mag);// iconst 1717986919
      TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
                    ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
      TR_PCISCNode *n1  = createIdiomDecVarInLoop(tgt, ctrl, 1, ent, v1, cm1);
                    ndiv= createIdiomIDiv10InLoop(tgt, ctrl, true, 1, n1, v2, mag, c2, c31);
      tgt->setAspects(isub|mul|shr);
      tgt->setNumDagIds(10);
      }
   else
      {
      /************************************    opc               id        dagId #cfg #child other/pred/children */
      TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  7,   0,   0,    0);  tgt->addNode(v1);      // count
                    v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(),  6,   0,   0,    1);  tgt->addNode(v2);      // int var
                    cexit=new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_allconst, TR::NoType,  tgt->incNumNodes(),  5,   0,   0);        tgt->addNode(cexit);   // iconst 0
      TR_PCISCNode *mag = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  4,   0,   0,   10);  tgt->addNode(mag);     // iconst 10
      TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
                    ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
      TR_PCISCNode *n1  = createIdiomDecVarInLoop(tgt, ctrl, 1, ent, v1, cm1);
                    ndiv= createIdiomIDiv10InLoop(tgt, ctrl, false, 1, n1, v2, mag, NULL, NULL);
      tgt->setAspects(isub|division);
      tgt->setNumDagIds(8);
      }
   TR_PCISCNode *   nst = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,   ndiv, ndiv, v2); tgt->addNode(nst);
                    ncmp= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ifcmpall, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   nst, v2, cexit);  tgt->addNode(ncmp);
                    n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ncmp->setSuccs(ent->getSucc(0), n9);
   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setImportantNode(0, ncmp);
   tgt->setTransformer(CISCTransform2CountDecimalDigit);
   tgt->setInhibitAfterVersioning();
   tgt->setNoAspects(call|bndchk, existAccess, existAccess);
   tgt->setMinCounts(1, 0, 0);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   return tgt;
   }



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Convert long to string
/* Example
int v2, v3;
while(true) {
   int num = v3 / 10;
   int ch = v3 - num * 10;
   v1[v2] = (char) ('0' - ch);
   v2--;
   v3 = num;
} while (v3 != 0);
*/

static TR::SymbolReference *
getSymrefLocalArray(TR::Compilation *comp, int size)
   {
   if (comp->target().cpu.isZ())
      {
      TR::SymbolReference *workSymRef = comp->getSymRefTab()->createLocalPrimArray(size, comp->getMethodSymbol(), 8);      // work area for CVD(G)
      workSymRef->setStackAllocatedArrayAccess();
      return workSymRef;
      }
   return NULL;
   }

static TR::Node *
createNodeLoadLocalArray(TR::Compilation *comp, TR::Node *trNode, int size)
   {
   TR::SymbolReference *symRef = getSymrefLocalArray(comp, size);
   return symRef ? TR::Node::createWithSymRef(trNode, TR::loadaddr, 0, symRef) :
                   TR::Node::create(trNode, TR::aconst, 0, 0);
   }

//*****************************************************************************************
// IL code generation for converting integer to string (using CVD and UNPKU)
// Input: ImportantNode(0) - istore node for index (V2)
//        ImportantNode(1) - (i/l)store node for input value (V3)
//        ImportantNode(2) - array store node
//        ImportantNode(3) - null check (optional)
//*****************************************************************************************
bool
CISCTransform2LongToStringDigit(TR_CISCTransformer *trans)
   {
   TR_ASSERT(trans->getOffsetOperand1() == 0 && trans->getOffsetOperand2() == 0, "Not implemented yet");
   TR::Node *trNode;
   TR::TreeTop *trTreeTop;
   TR::Block *block;
   TR_CISCGraph *P = trans->getP();
   List<TR_CISCNode> *P2T = trans->getP2T();
   TR::Compilation *comp = trans->comp();
   bool ctrl = trans->isGenerateI2L();

   TR_ASSERT(trans->getP()->getVersionLength() == 0, "Versioning code is not implemented yet");

   TR_ASSERT(trans->isEmptyAfterInsertionIdiomList(0) && trans->isEmptyAfterInsertionIdiomList(1), "Not implemented yet!");
   if (!trans->isEmptyAfterInsertionIdiomList(0) || !trans->isEmptyAfterInsertionIdiomList(1)) return false;

   trans->findFirstNode(&trTreeTop, &trNode, &block);
   if (!block) return false;    // cannot find

   if (isLoopPreheaderLastBlockInMethod(comp, block))
      {
      traceMsg(comp, "Bailing CISCTransform2LongToStringDigit due to null TT - might be a preheader in last block of method\n");
      return false;
      }

   TR::Block *target = trans->analyzeSuccessorBlock();
   // Currently, it allows only a single successor.
   if (!target) return false;
   TR_CISCNode *arrayStoreCISC = trans->getP2TInLoopIfSingle(P->getImportantNode(2));
   if (!arrayStoreCISC) return false;
   TR::Node *arrayStoreAddress = arrayStoreCISC->getHeadOfTrNode()->getChild(0)->duplicateTree();

   TR::Node *baseVarRepNode, *countVarRepNode, *inputVarRepNode;
   getP2TTrRepNodes(trans, &baseVarRepNode, &countVarRepNode, &inputVarRepNode);
   TR::SymbolReference * countVarSymRef = countVarRepNode->getSymbolReference();
   TR::SymbolReference * inputVarSymRef = inputVarRepNode->getSymbolReference();
   TR::Node *countVar, *inputVar;
   countVar = createLoad(countVarRepNode);
   inputVar = createLoad(inputVarRepNode);
   TR::Node *replaceParent = NULL;
   int childNum = -1;
   if (!trans->searchNodeInTrees(arrayStoreAddress, countVar, &replaceParent, &childNum))
      return false;

   TR_ASSERT(inputVar->getDataType() == TR::Int32 || inputVar->getDataType() == TR::Int64, "error");

   //
   // obtain a CISCNode of each store
   TR_CISCNode *storeV2 = trans->getP2TRepInLoop(P->getImportantNode(0));
   TR_CISCNode *storeV3 = trans->getP2TRepInLoop(P->getImportantNode(1));
   TR_ASSERT(storeV2 != NULL && storeV3 != NULL, "error");
   TR::Node *nullchk = 0;
   if (P->getImportantNode(3))
      {
      TR_CISCNode *nullchkCISC = trans->getP2TInLoopIfSingle(P->getImportantNode(3));
      if (nullchkCISC) nullchk = nullchkCISC->getHeadOfTrNode()->duplicateTree();
      }

   //
   // checking a set of all uses for each index
   TR_ASSERT(storeV2->getDagID() == storeV3->getDagID(), "error");
#if 1
   TR::Node *digit = TR::Node::create(TR::countDigits, 2,
                                    inputVar,
                                    createNodeLoadDigit10Table(comp, inputVarRepNode));
#else
   TR::Node *digit = TR::Node::create(TR::countDigits, 2,
                                    inputVar,
                                    createNodeLoadDigit10Table(comp, trNode));
#endif
   TR::Node *resultV2 = createOP2(comp, TR::isub, countVar, digit);
   replaceParent->setAndIncChild(childNum, createOP2(comp, TR::isub, resultV2,
                                                     TR::Node::create(trNode, TR::iconst, 0, -1)));
   TR::Node *storeResultV3 = 0;
   if (!storeV3->checkDagIdInChains())
      {
      TR::DataType dataType = storeV3->getDataType();
      TR::Node * constNode;
      if (dataType == TR::Int32)
         {
         constNode = TR::Node::create(trNode, TR::iconst, 0, 0);
         }
      else
         {
         constNode = TR::Node::create(trNode, TR::lconst, 0, 0);
         constNode->setLongInt(0);
         }
      storeResultV3 = TR::Node::createStore(inputVarSymRef, constNode);
      }

   TR::Node *l2s = TR::Node::create(trNode, TR::long2String, 4);
   l2s->setSymbolReference(comp->getSymRefTab()->findOrCreatelong2StringSymbol());
   l2s->setAndIncChild(0, inputVar);
   l2s->setAndIncChild(1, arrayStoreAddress);
   l2s->setAndIncChild(2, digit);
   l2s->setAndIncChild(3, createNodeLoadLocalArray(comp, trNode, 16));
   TR::Node *storeResultV2 = TR::Node::createStore(countVarSymRef, resultV2);

   // Insert nodes and maintain the CFG
   TR::TreeTop *last;
   last = trans->removeAllNodes(trTreeTop, block->getExit());
   last->join(block->getExit());
   block = trans->insertBeforeNodes(block);
   if (nullchk) block->append(TR::TreeTop::create(comp, nullchk));
   block->append(TR::TreeTop::create(comp, TR::Node::create(TR::treetop, 1, l2s)));
   block->append(TR::TreeTop::create(comp, storeResultV2));
   if (storeResultV3) block->append(TR::TreeTop::create(comp, storeResultV3));

   trans->insertAfterNodes(block);

   trans->setSuccessorEdge(block, target);
   return true;
   }


TR_PCISCGraph *
makeLongToStringGraph(TR::Compilation *c, int32_t ctrl)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "LongToString", 0, 16);
   /************************************    opc               id        dagId #cfg #child other/pred/children */
   TR_PCISCNode *v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 16,   0,   0,    0);  tgt->addNode(v1); // array base
   TR_PCISCNode *v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v2); // count
   TR_PCISCNode *v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    1);  tgt->addNode(v3); // long var
   TR_PCISCNode *v4  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    2);  tgt->addNode(v4); // stored value
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(), 12,   0,   0); tgt->addNode(vorc);       // length
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(), 11,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *cl0 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst  , TR::Int64,  tgt->incNumNodes(), 10,   0,   0,    0);  tgt->addNode(cl0);// lconst 0
   TR_PCISCNode *cl10= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lconst  , TR::Int64,  tgt->incNumNodes(),  9,   0,   0,   10);  tgt->addNode(cl10);//lconst 10
   TR_PCISCNode *c0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  8,   0,   0,    0);  tgt->addNode(c0); // iconst 0
   TR_PCISCNode *c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(), 7, 2);                    // element size
   TR_PCISCNode *c9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  6,   0,   0,    9);  tgt->addNode(c9); // iconst 9
   TR_PCISCNode *cm87= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  5,   0,   0,  -87);  tgt->addNode(cm87);//iconst -87
   TR_PCISCNode *cm48= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  4,   0,   0,  -48);  tgt->addNode(cm48);//iconst -48
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *nrem= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lrem     , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   ent, v3, cl10);   tgt->addNode(nrem);
   TR_PCISCNode *nl2i= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::l2i      , TR::Int32, tgt->incNumNodes(),  1,   1,   1,  nrem, nrem);   tgt->addNode(nl2i);
   TR_PCISCNode *nneg= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub     , TR::Int32, tgt->incNumNodes(),  1,   1,   2,  nl2i, c0, nl2i);   tgt->addNode(nneg);
   TR_PCISCNode *nst4= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,  nneg, nneg, v4); tgt->addNode(nst4);
   TR_PCISCNode *ifge= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpgt, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,  nst4, v4, c9); tgt->addNode(ifge);
   TR_PCISCNode *ad48= createIdiomDecVarInLoop(tgt, ctrl, 1, ifge, v4, cm48);
   TR_PCISCNode *ad87= createIdiomDecVarInLoop(tgt, ctrl, 1, ad48, v4, cm87);
   TR_PCISCNode *adm1= createIdiomIncVarInLoop(tgt, ctrl, 1, ad87, v2, cm1);
   TR_PCISCNode *nck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,  adm1, v1); tgt->addNode(nck); // optional
   TR_PCISCNode *bck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,  nck, vorc, v2); tgt->addNode(bck);
   TR_PCISCNode *ncst= createIdiomCharArrayStoreInLoop(tgt, ctrl, 1, bck, v1, v2, cmah, c2, v4);
   TR_PCISCNode *ndiv= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ldiv     , TR::Int64, tgt->incNumNodes(),  1,   1,   2,   ncst, v3, cl10);   tgt->addNode(ndiv);
   TR_PCISCNode *nst3= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::lstore , TR::Int64,   tgt->incNumNodes(),  1,   1,   2,   ndiv, ndiv, v3); tgt->addNode(nst3);
   TR_PCISCNode *ifeq= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iflcmpeq, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,   nst3, v3, cl0);  tgt->addNode(ifeq);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ifge->setSucc(1, ad87);
   ad48->setSucc(0, adm1);
   ifeq->setSuccs(ent->getSucc(0), n9);
   nck->setIsOptionalNode();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(adm1, nst3, ncst, nck);
   tgt->setNumDagIds(17);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2LongToStringDigit);
   tgt->setAspects(isub|iadd|bndchk|division|reminder, 0, ILTypeProp::Size_2);
   tgt->setNoAspects(call, 0, 0);
   tgt->setMinCounts(2, 0, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitAfterVersioning();
   return tgt;
   }


TR_PCISCGraph *
makeIntToStringGraph(TR::Compilation *c, int32_t ctrl, bool isDiv2Mul)
   {
   TR_PCISCGraph *tgt = new (PERSISTENT_NEW) TR_PCISCGraph(c->trMemory(), "IntToString", 0, 16);
   TR_PCISCNode *ci2, *c2, *c10, *c31, *mag, *v1, *v2, *v3;
   uint32_t otherMask;
   /********************************************************    opc               id        dagId #cfg #child other/pred/children */
   v1  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_arraybase, TR::NoType, tgt->incNumNodes(), 15,   0,   0,    0);  tgt->addNode(v1);       // array base
   v2  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 14,   0,   0,    0);  tgt->addNode(v2);       // count
   v3  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_variable, TR::NoType,  tgt->incNumNodes(), 13,   0,   0,    1);  tgt->addNode(v3);       // long var
   c2  = createIdiomArrayRelatedConst(tgt, ctrl, tgt->incNumNodes(),12, 2);                  // element size
   c10 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(), 11,   0,   0,   10);  tgt->addNode(c10);// iconst 10
   if (isDiv2Mul)
      {
      c31 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(), 10,   0,   0,   31);  tgt->addNode(c31);// iconst 31
      if (ctrl & CISCUtilCtl_64Bit)
         {
         ci2 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  9,   0,   0,  2);  tgt->addNode(ci2);// iconst 2
         }
      else
         ci2 = c2;
      mag = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  8,   0,   0,   1717986919);  tgt->addNode(mag);// iconst 1717986919
      otherMask = shr;
      }
   else
      {
      ci2 = c31 = NULL;
      mag = c10;
      otherMask = division;
      }
   TR_PCISCNode *vorc= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_quasiConst2, TR::NoType, tgt->incNumNodes(),7,   0,   0); tgt->addNode(vorc);       // length
   TR_PCISCNode *cmah= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_ahconst, TR::NoType,   tgt->incNumNodes(),  6,   0,   0,    0);  tgt->addNode(cmah);     // array header
   TR_PCISCNode *c0  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  5,   0,   0,    0);  tgt->addNode(c0); // iconst 0
   TR_PCISCNode *c48 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst  , TR::Int32,  tgt->incNumNodes(),  4,   0,   0,  48);  tgt->addNode(c48);//iconst 48
   TR_PCISCNode *cm1 = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::iconst, TR::Int32,    tgt->incNumNodes(),  3,   0,   0,   -1);  tgt->addNode(cm1);
   TR_PCISCNode *ent = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_entrynode, TR::NoType, tgt->incNumNodes(),  2,   1,   0);        tgt->addNode(ent);
   TR_PCISCNode *nck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::NULLCHK, TR::NoType,   tgt->incNumNodes(),  1,   1,   1,  ent, v1); tgt->addNode(nck); // optional
   TR_PCISCNode *bck = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::BNDCHK, TR::NoType,    tgt->incNumNodes(),  1,   1,   2,  nck,  vorc, v2); tgt->addNode(bck);
   TR_PCISCNode *addr= createIdiomArrayAddressInLoop(tgt, ctrl, 1, bck, v1, v2, cmah, c2);
   TR_PCISCNode *ndiv= createIdiomIDiv10InLoop(tgt, ctrl, isDiv2Mul, 1, addr, v3, mag, ci2, c31);
   TR_PCISCNode *nmul= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::imul, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,  ndiv, ndiv, c10); tgt->addNode(nmul);
   TR_PCISCNode *nrem= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,  nmul, v3, nmul); tgt->addNode(nrem);
   TR_PCISCNode *nch = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::isub, TR::Int32,      tgt->incNumNodes(),  1,   1,   2,  nrem, c48, nrem); tgt->addNode(nch);
   TR_PCISCNode *ncst= createIdiomCharArrayStoreBodyInLoop(tgt, ctrl, 1, nch, addr, nch);
   TR_PCISCNode *nst3= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::istore , TR::Int32,   tgt->incNumNodes(),  1,   1,   2,  ncst, ndiv, v3); tgt->addNode(nst3);
   TR_PCISCNode *adm1= createIdiomIncVarInLoop(tgt, ctrl, 1, nst3, v2, cm1);
   TR_PCISCNode *ifeq= new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR::ificmpeq, TR::NoType,  tgt->incNumNodes(),  1,   2,   2,  adm1, v3, c0);  tgt->addNode(ifeq);
   TR_PCISCNode *n9  = new (PERSISTENT_NEW) TR_PCISCNode(c->trMemory(), TR_exitnode, TR::NoType,  tgt->incNumNodes(),  0,   0,   0);        tgt->addNode(n9);

   ifeq->setSuccs(ent->getSucc(0), n9);
   nck->setIsOptionalNode();

   tgt->setEntryNode(ent);
   tgt->setExitNode(n9);
   tgt->setImportantNodes(adm1, nst3, ncst, nck);
   tgt->setNumDagIds(16);
   tgt->createInternalData(1);

   tgt->setSpecialNodeTransformer(defaultSpecialNodeTransformer);
   tgt->setTransformer(CISCTransform2LongToStringDigit);
   tgt->setAspects(isub|iadd|bndchk|mul|otherMask, 0, ILTypeProp::Size_2);
   tgt->setNoAspects(call, 0, 0);
   tgt->setMinCounts(1, 0, 1);  // minimum ifCount, indirectLoadCount, indirectStoreCount
   tgt->setHotness(warm, false);
   tgt->setInhibitAfterVersioning();
   return tgt;
   }

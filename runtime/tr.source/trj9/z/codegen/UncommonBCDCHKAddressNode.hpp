/*******************************************************************************
 * Copyright (c) 2000, 2016 IBM Corp. and others
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
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
 *******************************************************************************/

#ifndef UNCOMMON_BCDCHK_ADDRESS_NODE
#define UNCOMMON_BCDCHK_ADDRESS_NODE

#include "codegen/CodeGenerator.hpp"  // for CodeGenerator
#include "compile/Compilation.hpp"    // for Compilation
#include "env/TRMemory.hpp"           // for TR_Memory, etc
#include "il/Node.hpp"                // for vcount_t
#include "infra/List.hpp"             // for List

/** \brief
 *     Uncommons BCDCHK's address node to prevent possible missed evaluation of the respective node.
 *
 *  \details
 *     The DAA APIs were designed such that the inputs to the APIs are never modified. Now the result of any packed
 *     decimal (or BCD) operation in the compiler is an intermediate value, thus for DAA APIs which do have a result
 *     we need to copy the intermediate value generated by the operation (which is usually on the stack) back out to
 *     the destination array specified by the user and extracted from the DAA API. One will find that for all such
 *     operations there is a BCD store following the BCDCHK operation which carries out this store back into the result
 *     array.
 *
 *     The BCDCHK node guards against the operation in its first child. This operation may raise a hardware trap which
 *     we handle in the compiler signal handler and return control to an OOL section which reconstructs the original
 *     Java call for the respective operation. Now because BCDCHK's first child generates an intermediate value and
 *     because the optimizer does not know about the subtleties of internal control flow introduced by the codegen for
 *     the BCDCHK node, we must ensure that no matter which path we take during runtime the values of all nodes
 *     following the evaluation of the BCDCHK are valid.
 *
 *     This means that if we take the hardware trap, the value of the intermediate result of the first child of the
 *     BCDCHK must be valid in the subsequent trees (local CSE could have commoned the node). To ensure this is the
 *     case we must copy the result of the Java call back into the intermediate value following the Java call in the
 *     OOL path.
 *
 *     Now an interesting problem can arise in this situation. Recall that there may be a BCD store following the
 *     BCDCHK operation and the source of the store (the intermediate result) is the same location as the target of the
 *     store we must carry out in the OOL path. Modeled in trees, the following is an example of how things may look:
 *
 *     \code
 *     n1n  BCDCHK
 *     n2n    pdOpNode
 *     n3n    addressNode
 *     n4n      arrayNode
 *     n5n      offset
 *     n6n    callParam-1
 *     ...
 *     n8n    callParam-n
 *     n9n  pdstorei
 *     n3n    ==>addressNode
 *     n2n    ==>pdOpNode
 *     \endcode
 *
 *     Note that addressNode is commoned between the BCDCHK and the subsequent pdstorei. The addressNode represents
 *     the result array from the DAA API. It is attached as the second child of the BCDCHK so that we may materialize
 *     the address in the OOL path to store back into the intermediate result.
 *
 *     And this is where the issue can arise. Because addressNode is only needed in the OOL path, and the only way to
 *     reach the OOL path is for a hardware trap to happen at runtime we can get into a scenario where the BCDCHK at
 *     n1n does not take a hardware trap. In this scenario the instructions generated from the evaluation of addressNode
 *     (which is commoned in the pdstorei) is never executed at runtime, and hence the register containing the value at
 *     runtime is garbage at the point of the pdstorei.
 *
 *     To ensure this does not happen this codegen pass uncommons BCDCHK's second child if it has a reference count > 1.
 */
class UncommonBCDCHKAddressNode
   {
   public:
   TR_ALLOC(TR_Memory::CodeGenerator)
   UncommonBCDCHKAddressNode(TR::CodeGenerator* cg): cg(cg)
   {
   _comp = cg->comp();
   }

   void perform();

   private:

   TR::CodeGenerator * cg;
   TR::Compilation *_comp;
   TR::Compilation *comp() { return _comp; }
   };

#endif

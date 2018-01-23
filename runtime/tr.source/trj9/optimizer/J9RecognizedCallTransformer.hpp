/*******************************************************************************
* Copyright (c) 2017, 2017 IBM Corp. and others
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

#ifndef J9RECOGNIZEDCALLTRANSFORMER_INCL
#define J9RECOGNIZEDCALLTRANSFORMER_INCL

#include "optimizer/OMRRecognizedCallTransformer.hpp"

namespace J9
{

class RecognizedCallTransformer : public OMR::RecognizedCallTransformer
   {
   public:
   RecognizedCallTransformer(TR::OptimizationManager* manager)
      : OMR::RecognizedCallTransformer(manager)
      {}

   protected:
   virtual bool isInlineable(TR::TreeTop* treetop);
   virtual void transform(TR::TreeTop* treetop);

   private:
   void processSimpleMath(TR::Node* node, TR::ILOpCodes opcode);
   void processCRC32(TR::Node* node);
   };

}
#endif

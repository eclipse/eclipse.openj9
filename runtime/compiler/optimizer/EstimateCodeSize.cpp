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

#include "env/StackMemoryRegion.hpp"
#include "optimizer/EstimateCodeSize.hpp"

#include "env/FrontEnd.hpp"
#include "compile/Compilation.hpp"
#include "il/ResolvedMethodSymbol.hpp"
#include "infra/Assert.hpp"
#include "optimizer/CallInfo.hpp"
#include "ras/LogTracer.hpp"
#include "env/VMJ9.h"
#include "runtime/J9Profiler.hpp"

TR_EstimateCodeSize *
TR_EstimateCodeSize::get(TR_InlinerBase * inliner, TR_InlinerTracer *tracer, int32_t sizeThreshold)
   {
   TR::Compilation *comp = inliner->comp();
   TR_EstimateCodeSize *estimator = comp->fej9()->getCodeEstimator(comp);

   estimator->_inliner = inliner;
   estimator->_tracer = tracer;

   estimator->_isLeaf = false;
   estimator->_foundThrow = false;
   estimator->_hasExceptionHandlers = false;
   estimator->_throwCount = 0;
   estimator->_mayHaveVirtualCallProfileInfo = false;

   TR_CatchBlockProfileInfo * catchInfo = TR_CatchBlockProfileInfo::get(comp);
   estimator->_aggressivelyInlineThrows = catchInfo && catchInfo->getCatchCounter()
         >= TR_CatchBlockProfileInfo::EDOThreshold;

   estimator->_recursionDepth = 0;
   estimator->_recursedTooDeep = false;

   estimator->_sizeThreshold = sizeThreshold;
   estimator->_realSize = 0;
   estimator->_error = ECS_NORMAL;

   estimator->_numOfEstimatedCalls = 0;
   estimator->_hasNonColdCalls = true;

   estimator->_totalBCSize = 0;

   return estimator;
   }

void
TR_EstimateCodeSize::release(TR_EstimateCodeSize *estimator)
   {
   TR::Compilation *comp = estimator->_inliner->comp();
   comp->fej9()->releaseCodeEstimator(comp, estimator);
   }

bool
TR_EstimateCodeSize::calculateCodeSize(TR_CallTarget *calltarget, TR_CallStack *callStack, bool recurseDown)
   {
   TR_InlinerDelimiter delimiter(tracer(), "calculateCodeSize");

   _isLeaf = true;
   _foundThrow = false;
   _hasExceptionHandlers = false;
   _throwCount = 0;

   _mayHaveVirtualCallProfileInfo = (TR_ValueProfileInfoManager::get(comp()) != NULL);

   bool retval = false;

   {
   TR::StackMemoryRegion stackMemoryRegion(*comp()->trMemory());
   retval = estimateCodeSize(calltarget, callStack, recurseDown);
   } // Stack memory region scope

   if (_inliner->getPolicy()->tryToInline(calltarget, callStack, true))
      {
      heuristicTrace(tracer(),"tryToInline pattern matched.  Assuming zero size for %s\n", tracer()->traceSignature(calltarget->_calleeSymbol));
      _realSize = 0;
      retval = true;
      }

   if (!retval && _inliner->forceInline(calltarget))
      retval = true;

   return retval;
   }

const char *
TR_EstimateCodeSize::getError()
   {
      switch(_error)
         {
            case ECS_NORMAL: return "ECS_NORMAL";
            case ECS_RECURSION_DEPTH_THRESHOLD_EXCEEDED: return "ECS_RECURSION_DEPTH_THRESHOLD_EXCEEDED";
            case ECS_OPTIMISTIC_SIZE_THRESHOLD_EXCEEDED: return "ECS_OPTIMISTIC_SIZE_THRESHOLD_EXCEEDED";
            case ECS_VISITED_COUNT_THRESHOLD_EXCEEDED: return "ECS_VISITED_COUNT_THRESHOLD_EXCEEDED";
            case ECS_REAL_SIZE_THRESHOLD_EXCEEDED: return "ECS_REAL_SIZE_THRESHOLD_EXCEEDED";
            case ECS_ARGUMENTS_INCOMPATIBLE: return "ECS_ARGUMENTS_INCOMPATIBLE";
            case ECS_CALLSITES_CREATION_FAILED: return "ECS_CALLSITES_CREATION_FAILED";
            default: return "ECS_UNKNOWN";
         }
   }

bool
TR_EstimateCodeSize::isInlineable(TR_CallStack * prevCallStack, TR_CallSite *callsite)
   {
   TR_ASSERT(callsite, "Estimate Code Size: callsite is null!");

   heuristicTrace(tracer(),"Depth %d: Created Call Site %p for call found at bc index %d. Signature %s  Looking for call targets.",
                             _recursionDepth, callsite, callsite->_byteCodeIndex, tracer()->traceSignature(callsite));

   if (_inliner->getPolicy()->supressInliningRecognizedInitialCallee(callsite, _inliner->comp()))
      {
      heuristicTrace(tracer(),"Skip looking for call targets because supressInliningRecognizedInitialCallee is true for this call site %p\n", callsite);
      return false;
      }

   callsite->findCallSiteTarget(prevCallStack, _inliner);
   _inliner->applyPolicyToTargets(prevCallStack, callsite);


   if (callsite->numTargets() <= 0)
      {
      if (tracer()->debugLevel())
         tracer()->dumpCallSite(
               callsite,
               "Call About to be Dumped returned false from findInlineTargets in partialCodeSize estimation");

      heuristicTrace(tracer(),"Depth %d: Did not find any targets to be inlined in callsite %p bc index %d. Signature %s",
                                _recursionDepth, callsite, callsite->_byteCodeIndex, tracer()->traceSignature(callsite));

      _isLeaf = false;
      return false;
      }

   if (tracer()->debugLevel())
      tracer()->dumpCallSite(
            callsite,
            "Call About to be Dumped returns true from findInlineTargets in partialCodeSize estimation");

   heuristicTrace(tracer(),"Depth %d: Found %d targets to inline for callsite %p bc index %d. Signature %s",
                            _recursionDepth, callsite->numTargets(),callsite, callsite->_byteCodeIndex, tracer()->traceSignature(callsite));

   return true;
   }

bool
TR_EstimateCodeSize::returnCleanup(EcsCleanupErrorStates errorState)
   {
   _error = errorState;
   if (_mayHaveVirtualCallProfileInfo)
      _inliner->comp()->decInlineDepth(true);
   if (errorState > 0)
      return false;
   else
      return true;
   }

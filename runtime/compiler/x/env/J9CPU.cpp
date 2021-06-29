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

#include "compile/Compilation.hpp"
#include "env/CompilerEnv.hpp"
#include "env/CPU.hpp"
#include "env/VMJ9.h"
#include "x/runtime/X86Runtime.hpp"
#include "env/JitConfig.hpp"
#include "codegen/CodeGenerator.hpp"
#if defined(J9VM_OPT_JITSERVER)
#include "control/CompilationRuntime.hpp"
#include "control/CompilationThread.hpp"
#include "runtime/JITClientSession.hpp"
#endif /* defined(J9VM_OPT_JITSERVER) */

// This is a workaround to avoid J9_PROJECT_SPECIFIC macros in x/env/OMRCPU.cpp
// Without this definition, we get an undefined symbol of JITConfig::instance() at runtime
TR::JitConfig * TR::JitConfig::instance() { return NULL; }

TR::CPU
J9::X86::CPU::detectRelocatable(OMRPortLibrary * const omrPortLib)
   {
   // Sandybridge Architecture is selected to be our default portable processor description
   const uint32_t customFeatures [] = {OMR_FEATURE_X86_FPU, OMR_FEATURE_X86_CX8, OMR_FEATURE_X86_CMOV,
                                       OMR_FEATURE_X86_MMX, OMR_FEATURE_X86_SSE, OMR_FEATURE_X86_SSE2,
                                       OMR_FEATURE_X86_SSSE3, OMR_FEATURE_X86_SSE4_1, OMR_FEATURE_X86_POPCNT,
                                       OMR_FEATURE_X86_SSE3, OMR_FEATURE_X86_AESNI, OMR_FEATURE_X86_AVX};

   OMRPORT_ACCESS_FROM_OMRPORT(omrPortLib);
   OMRProcessorDesc customProcessorDescription;
   memset(customProcessorDescription.features, 0, OMRPORT_SYSINFO_FEATURES_SIZE*sizeof(uint32_t));
   for (size_t i = 0; i < sizeof(customFeatures)/sizeof(uint32_t); i++)
      {
      omrsysinfo_processor_set_feature(&customProcessorDescription, customFeatures[i], TRUE);
      }

   OMRProcessorDesc hostProcessorDescription;
   omrsysinfo_get_processor_description(&hostProcessorDescription);

   // Pick the older processor between our hand-picked processor and host processor to be the actual portable processor
   OMRProcessorDesc portableProcessorDescription;
   portableProcessorDescription.processor = OMR_PROCESSOR_X86_FIRST;
   portableProcessorDescription.physicalProcessor = portableProcessorDescription.processor;
   memset(portableProcessorDescription.features, 0, OMRPORT_SYSINFO_FEATURES_SIZE*sizeof(uint32_t));

   for (size_t i = 0; i < OMRPORT_SYSINFO_FEATURES_SIZE; i++)
      {
      portableProcessorDescription.features[i] = hostProcessorDescription.features[i] & customProcessorDescription.features[i];
      }

   return TR::CPU::customize(portableProcessorDescription);
   }

void
J9::X86::CPU::enableFeatureMasks()
   {
   // Only enable the features that compiler currently uses
   const uint32_t utilizedFeatures [] = {OMR_FEATURE_X86_FPU, OMR_FEATURE_X86_CX8, OMR_FEATURE_X86_CMOV,
                                        OMR_FEATURE_X86_MMX, OMR_FEATURE_X86_SSE, OMR_FEATURE_X86_SSE2,
                                        OMR_FEATURE_X86_SSSE3, OMR_FEATURE_X86_SSE4_1, OMR_FEATURE_X86_POPCNT,
                                        OMR_FEATURE_X86_AESNI, OMR_FEATURE_X86_OSXSAVE, OMR_FEATURE_X86_AVX,
                                        OMR_FEATURE_X86_FMA, OMR_FEATURE_X86_HLE, OMR_FEATURE_X86_RTM,
                                        OMR_FEATURE_X86_SSE3};

   memset(_supportedFeatureMasks.features, 0, OMRPORT_SYSINFO_FEATURES_SIZE*sizeof(uint32_t));
   OMRPORT_ACCESS_FROM_OMRPORT(TR::Compiler->omrPortLib);
   for (size_t i = 0; i < sizeof(utilizedFeatures)/sizeof(uint32_t); i++)
      {
      omrsysinfo_processor_set_feature(&_supportedFeatureMasks, utilizedFeatures[i], TRUE);
      }
   _isSupportedFeatureMasksEnabled = true;
   }


TR_X86CPUIDBuffer *
J9::X86::CPU::queryX86TargetCPUID()
   {
   static TR_X86CPUIDBuffer buf = { {'U','n','k','n','o','w','n','B','r','a','n','d'} };
   jitGetCPUID(&buf);
   return &buf;
   }

const char *
J9::X86::CPU::getProcessorVendorId()
   {
   return self()->getX86ProcessorVendorId();
   }

uint32_t
J9::X86::CPU::getProcessorSignature()
   {
   return self()->getX86ProcessorSignature();
   }

bool
J9::X86::CPU::hasPopulationCountInstruction()
   {
   if ((self()->getX86ProcessorFeatureFlags2() & TR_POPCNT) != 0x00000000)
      return true;
   else
      return false;
   }

bool
J9::X86::CPU::isCompatible(const OMRProcessorDesc& processorDescription)
   {
   for (int i = 0; i < OMRPORT_SYSINFO_FEATURES_SIZE; i++)
      {
      // Check to see if the current processor contains all the features that code cache's processor has
      if ((processorDescription.features[i] & _processorDescription.features[i]) != processorDescription.features[i])
         return false;
      }
   return true;
   }

bool
J9::X86::CPU::is(OMRProcessorArchitecture p)
   {
   static bool disableCPUDetectionTest = feGetEnv("TR_DisableCPUDetectionTest");
   if (!disableCPUDetectionTest)
      {
      TR_ASSERT_FATAL(self()->is_test(p), "Old API and new API did not match: processor type %d\n", p);
      }

   return _processorDescription.processor == p;
   }

bool
J9::X86::CPU::supportsFeature(uint32_t feature)
   {
   OMRPORT_ACCESS_FROM_OMRPORT(TR::Compiler->omrPortLib);

   static bool disableCPUDetectionTest = feGetEnv("TR_DisableCPUDetectionTest");
   if (!disableCPUDetectionTest)
      {
      TR_ASSERT_FATAL(self()->supports_feature_test(feature), "Old API and new API did not match: processor feature %d\n", feature);
      TR_ASSERT_FATAL(TRUE == omrsysinfo_processor_has_feature(&_supportedFeatureMasks, feature), "New processor feature usage detected, please add feature %d to _supportedFeatureMasks via TR::CPU::enableFeatureMasks()\n", feature);
      }
      
   return TRUE == omrsysinfo_processor_has_feature(&_processorDescription, feature);
   }

uint32_t
J9::X86::CPU::getX86ProcessorFeatureFlags()
   {
#if defined(J9VM_OPT_JITSERVER)
   if (auto stream = TR::CompilationInfo::getStream())
      {
      auto *vmInfo = TR::compInfoPT->getClientData()->getOrCacheVMInfo(stream);
      return vmInfo->_processorDescription.features[0];
      }
#endif /* defined(J9VM_OPT_JITSERVER) */
   return self()->queryX86TargetCPUID()->_featureFlags;
   }

uint32_t
J9::X86::CPU::getX86ProcessorFeatureFlags2()
   {
#if defined(J9VM_OPT_JITSERVER)
   if (auto stream = TR::CompilationInfo::getStream())
      {
      auto *vmInfo = TR::compInfoPT->getClientData()->getOrCacheVMInfo(stream);
      return vmInfo->_processorDescription.features[1];
      }
#endif /* defined(J9VM_OPT_JITSERVER) */
   return self()->queryX86TargetCPUID()->_featureFlags2;
   }

uint32_t
J9::X86::CPU::getX86ProcessorFeatureFlags8()
   {
#if defined(J9VM_OPT_JITSERVER)
   if (auto stream = TR::CompilationInfo::getStream())
      {
      auto *vmInfo = TR::compInfoPT->getClientData()->getOrCacheVMInfo(stream);
      return vmInfo->_processorDescription.features[3];
      }
#endif /* defined(J9VM_OPT_JITSERVER) */
   return self()->queryX86TargetCPUID()->_featureFlags8;
   }

bool
J9::X86::CPU::is_test(OMRProcessorArchitecture p)
   {
#if defined(J9VM_OPT_JITSERVER)
   if (TR::CompilationInfo::getStream())
      return true;
#endif /* defined(J9VM_OPT_JITSERVER) */
   if (TR::comp()->compileRelocatableCode() || TR::comp()->compilePortableCode())
      return true;

   switch(p)
      {
      case OMR_PROCESSOR_X86_INTELWESTMERE:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelWestmere() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELNEHALEM:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelNehalem() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELPENTIUM:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelPentium() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELP6:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelP6() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELPENTIUM4:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelPentium4() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELCORE2:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelCore2() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELTULSA:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelTulsa() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELSANDYBRIDGE:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelSandyBridge() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELIVYBRIDGE:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelIvyBridge() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELHASWELL:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelHaswell() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELBROADWELL:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelBroadwell() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_INTELSKYLAKE:
         return TR::CodeGenerator::getX86ProcessorInfo().isIntelSkylake() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_AMDATHLONDURON:
         return TR::CodeGenerator::getX86ProcessorInfo().isAMDAthlonDuron() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_AMDOPTERON:
         return TR::CodeGenerator::getX86ProcessorInfo().isAMDOpteron() == (_processorDescription.processor == p);
      case OMR_PROCESSOR_X86_AMDFAMILY15H:
         return TR::CodeGenerator::getX86ProcessorInfo().isAMD15h() == (_processorDescription.processor == p);
      default:
         return false;
      }
   return false;
   }

bool
J9::X86::CPU::supports_feature_test(uint32_t feature)
   {
#if defined(J9VM_OPT_JITSERVER)
   if (TR::CompilationInfo::getStream())
      return true;
#endif /* defined(J9VM_OPT_JITSERVER) */
   if (TR::comp()->compileRelocatableCode() || TR::comp()->compilePortableCode())
      return true;

   OMRPORT_ACCESS_FROM_OMRPORT(TR::Compiler->omrPortLib);
   bool ans = (TRUE == omrsysinfo_processor_has_feature(&_processorDescription, feature));

   switch(feature)
      {
      case OMR_FEATURE_X86_OSXSAVE:
         return TR::CodeGenerator::getX86ProcessorInfo().enabledXSAVE() == ans;
      case OMR_FEATURE_X86_FPU:
         return TR::CodeGenerator::getX86ProcessorInfo().hasBuiltInFPU() == ans;
      case OMR_FEATURE_X86_VME:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsVirtualModeExtension() == ans;
      case OMR_FEATURE_X86_DE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsDebuggingExtension() == ans;
      case OMR_FEATURE_X86_PSE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsPageSizeExtension() == ans;
      case OMR_FEATURE_X86_TSC:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsRDTSCInstruction() == ans;
      case OMR_FEATURE_X86_MSR:
         return TR::CodeGenerator::getX86ProcessorInfo().hasModelSpecificRegisters() == ans;
      case OMR_FEATURE_X86_PAE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsPhysicalAddressExtension() == ans;
      case OMR_FEATURE_X86_MCE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsMachineCheckException() == ans;
      case OMR_FEATURE_X86_CX8:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsCMPXCHG8BInstruction() == ans;
      case OMR_FEATURE_X86_CMPXCHG16B:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsCMPXCHG16BInstruction() == ans;
      case OMR_FEATURE_X86_APIC:
         return TR::CodeGenerator::getX86ProcessorInfo().hasAPICHardware() == ans;
      case OMR_FEATURE_X86_MTRR:
         return TR::CodeGenerator::getX86ProcessorInfo().hasMemoryTypeRangeRegisters() == ans;
      case OMR_FEATURE_X86_PGE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsPageGlobalFlag() == ans;
      case OMR_FEATURE_X86_MCA:
         return TR::CodeGenerator::getX86ProcessorInfo().hasMachineCheckArchitecture() == ans;
      case OMR_FEATURE_X86_CMOV:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsCMOVInstructions() == ans;
      case OMR_FEATURE_X86_PAT:
         return TR::CodeGenerator::getX86ProcessorInfo().hasPageAttributeTable() == ans;
      case OMR_FEATURE_X86_PSE_36:
         return TR::CodeGenerator::getX86ProcessorInfo().has36BitPageSizeExtension() == ans;
      case OMR_FEATURE_X86_PSN:
         return TR::CodeGenerator::getX86ProcessorInfo().hasProcessorSerialNumber() == ans;
      case OMR_FEATURE_X86_CLFSH:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsCLFLUSHInstruction() == ans;
      case OMR_FEATURE_X86_DS:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsDebugTraceStore() == ans;
      case OMR_FEATURE_X86_ACPI:
         return TR::CodeGenerator::getX86ProcessorInfo().hasACPIRegisters() == ans;
      case OMR_FEATURE_X86_MMX:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsMMXInstructions() == ans;
      case OMR_FEATURE_X86_FXSR:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsFastFPSavesRestores() == ans;
      case OMR_FEATURE_X86_SSE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSE() == ans;
      case OMR_FEATURE_X86_SSE2:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSE2() == ans;
      case OMR_FEATURE_X86_SSE3:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSE3() == ans;
      case OMR_FEATURE_X86_SSSE3:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSSE3() == ans;
      case OMR_FEATURE_X86_SSE4_1:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSE4_1() == ans;
      case OMR_FEATURE_X86_SSE4_2:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSSE4_2() == ans;
      case OMR_FEATURE_X86_PCLMULQDQ:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsCLMUL() == ans;
      case OMR_FEATURE_X86_AESNI:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsAESNI() == ans;
      case OMR_FEATURE_X86_POPCNT:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsPOPCNT() == ans;
      case OMR_FEATURE_X86_SS:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsSelfSnoop() == ans;
      case OMR_FEATURE_X86_RTM:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsTM() == ans;
      case OMR_FEATURE_X86_HTT:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsHyperThreading() == ans;
      case OMR_FEATURE_X86_HLE:
         return TR::CodeGenerator::getX86ProcessorInfo().supportsHLE() == ans;
      case OMR_FEATURE_X86_TM:
         return TR::CodeGenerator::getX86ProcessorInfo().hasThermalMonitor() == ans;
      case OMR_FEATURE_X86_AVX:
         return true;
      default:
         return false;
      }
   return false;
   }


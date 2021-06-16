<?xml version="1.0" encoding="UTF-8"?>
<!--
Copyright (c) 2006, 2021 IBM Corp. and others

This program and the accompanying materials are made available under
the terms of the Eclipse Public License 2.0 which accompanies this
distribution and is available at https://www.eclipse.org/legal/epl-2.0/
or the Apache License, Version 2.0 which accompanies this distribution and
is available at https://www.apache.org/licenses/LICENSE-2.0.

This Source Code may also be made available under the following
Secondary Licenses when the conditions for such availability set
forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
General Public License, version 2 with the GNU Classpath
Exception [1] and GNU General Public License, version 2 with the
OpenJDK Assembly Exception [2].

[1] https://www.gnu.org/software/classpath/license.html
[2] http://openjdk.java.net/legal/assembly-exception.html

SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
-->
<spec xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://www.ibm.com/j9/builder/spec" xsi:schemaLocation="http://www.ibm.com/j9/builder/spec spec-v1.xsd" id="win_x86">
	<name>Windows IA32</name>
	<asmBuilderName>WIN31</asmBuilderName>
	<cpuArchitecture>x86</cpuArchitecture>
	<os>win</os>
	<defaultJCL>Sidecar</defaultJCL>
	<defaultSizes>desktop (256M + big OS stack + growable JIT caches)</defaultSizes>
	<priority>90</priority>
	<owners>
		<owner>graham_chapman@ca.ibm.com</owner>
	</owners>
	<properties>
		<property name="SE6_extension" value="zip"/>
		<property name="SE6_package" value="wi32"/>
		<property name="aotTarget" value="ia32-win32"/>
		<property name="complianceTestingSupported" value="true"/>
		<property name="complianceTestingSupportedModes" value="proxytck_cldc11,proxytck_cdc10,proxytck_cdc11,proxytck_foun10,proxytck_foun11"/>
		<property name="directoryDelimiter" value="\\"/>
		<property name="graph_arch.cpu" value="{$spec.arch.cpuISA$}"/>
		<property name="graph_commands.chroot" value=""/>
		<property name="graph_commands.win.postamble" value="&quot;MACHINE_TYPE=win&quot;"/>
		<property name="graph_datamines" value="commands.win.datamine,site-ottawa-win.datamine,use.local.datamine"/>
		<property name="graph_enable_compiler_cmd" value=""/>
		<property name="graph_label.classlib" value="150"/>
		<property name="graph_label.java5" value="j9vmwi3224"/>
		<property name="graph_label.java6" value="pwi3260"/>
		<property name="graph_label.java60_26" value="pwi3260_26"/>
		<property name="graph_label.java6_rebuilt_extension" value="zip"/>
		<property name="graph_label.java7" value="pwi3270"/>
		<property name="graph_label.java70_27" value="pwi3270_27"/>
		<property name="graph_label.java7_raw" value="jdk7-windows-i586"/>
		<property name="graph_label.java8" value="pwi3280"/>
		<property name="graph_label.java8_raw" value="jdk8-windows-i586"/>
		<property name="graph_label.java9" value="pwi3290"/>
		<property name="graph_label.osid" value="win"/>
		<property name="graph_label.profile" value=""/>
		<property name="graph_make_parallel_arg" value="WINDOWS_MAKE_PARALLEL=1"/>
		<property name="graph_req.arch0" value="arch:x86"/>
		<property name="graph_req.arch1" value="arch:32bit"/>
		<property name="graph_req.aux0" value=""/>
		<property name="graph_req.aux1" value=""/>
		<property name="graph_req.build" value="{$common.req.build.java9$}"/>
		<property name="graph_req.build2" value="{$common.req.build.java8$}"/>
		<property name="graph_req.machine" value=""/>
		<property name="graph_req.machine.test" value="{$spec.property.graph_req.machine$}"/>
		<property name="graph_req.os" value="{$machine_mapping.win$}"/>
		<property name="graph_req.os.build" value="os:win"/>
		<property name="graph_req.os.perf" value="{$spec.property.graph_req.os$}"/>
		<property name="graph_se_classlib.java5" value="jcl_se.zip"/>
		<property name="graph_se_classlib.java6" value="jcl_se.zip"/>
		<property name="graph_tool_script.build" value="msvc140"/>
		<property name="graph_tool_script.test" value="msvc100"/>
		<property name="graph_variant.testing_suffix" value=""/>
		<property name="graph_variant.trailingID" value=""/>
		<property name="j2seRuntimeDir" value="jre/bin"/>
		<property name="j2seTags" value="pwi3260,j9vmwi3224"/>
		<property name="j9BuildName" value="win_x86"/>
		<property name="j9dt.compileTarget" value="makefile"/>
		<property name="j9dt.environmentScript" value="%DEV_TOOLS%\config.r26\msvc100.bat"/>
		<property name="j9dt.make" value="gmake"/>
		<property name="j9dt.shellPrefix" value="cmd.exe /C"/>
		<property name="j9dt.toolsTarget" value="buildtools.mk"/>
		<property name="javatestPlatform" value="win_x86-32"/>
		<property name="jclMemoryMax" value="-Xmx32m"/>
		<property name="jclOSStackSizeMax" value=""/>
		<property name="jdiTestsSupported" value="true"/>
		<property name="jgrinderTestingSupported" value="true"/>
		<property name="jitTestingOptLevel" value="optlevel=warm"/>
		<property name="localRootPath" value="$(J9_WIN_ROOT)"/>
		<property name="longLimitCmd" value=""/>
		<property name="main_shortname" value="wi32"/>
		<property name="os.lineDelimiter" value="dos"/>
		<property name="platform_arch" value="i386"/>
		<property name="sun.jdk7.platform_id" value="windows-i586"/>
		<property name="sun.jdk8.platform_id" value="windows-i586"/>
		<property name="svn_stream" value=""/>
		<property name="testsOnDosFilesystem" value="true"/>
		<property name="uma.includeIcon" value="true"/>
		<property name="uma_include_template" value="win"/>
		<property name="uma_make_cmd_as" value="ml"/>
		<property name="uma_make_cmd_cc" value="cl"/>
		<property name="uma_make_cmd_cpp" value="$(CC) -EP"/>
		<property name="uma_make_cmd_cxx" value="$(CC)"/>
		<property name="uma_make_cmd_implib" value="lib"/>
		<property name="uma_make_cmd_link" value="link"/>
		<property name="uma_make_cmd_clang_cxx" value="clang"/>
		<property name="uma_make_cmd_mt" value="mt"/>
		<property name="uma_make_cmd_rc" value="rc"/>
		<property name="uma_processor" value="x86"/>
		<property name="uma_socketLibraries" value="ws2_32.lib,Iphlpapi.lib"/>
		<property name="uma_type" value="windows"/>
	</properties>
	<features>
		<feature id="combogc"/>
		<feature id="core"/>
		<feature id="dbgext"/>
		<feature id="se"/>
		<feature id="se60_26"/>
		<feature id="se7"/>
		<feature id="se70_27"/>
	</features>
	<source>
		<project id="com.ibm.jvmti.tests"/>
		<project id="compiler"/>
	</source>
	<flags>
		<flag id="interp_atomicFreeJni" value="true"/>
		<flag id="interp_atomicFreeJniUsesFlush" value="true"/>
		<flag id="interp_twoPassExclusive" value="true"/>
		<flag id="arch_x86" value="true"/>
		<flag id="build_SE6_package" value="true"/>
		<flag id="build_autobuild" value="true"/>
		<flag id="build_dropToHursley" value="true"/>
		<flag id="build_dropToToronto" value="true"/>
		<flag id="build_j2se" value="true"/>
		<flag id="build_java6proxy" value="true"/>
		<flag id="build_java8" value="true"/>
		<flag id="build_java9" value="false"/>
		<flag id="build_ouncemake" value="true"/>
		<flag id="build_product" value="true"/>
		<flag id="build_vmContinuous" value="true"/>
		<flag id="env_hasFPU" value="true"/>
		<flag id="env_littleEndian" value="true"/>
		<flag id="env_sse2SupportDetection" value="true"/>
		<flag id="gc_alignObjects" value="true"/>
		<flag id="gc_batchClearTLH" value="true"/>
		<flag id="gc_debugAsserts" value="true"/>
		<flag id="gc_inlinedAllocFields" value="true"/>
		<flag id="gc_minimumObjectSize" value="true"/>
		<flag id="gc_tlhPrefetchFTA" value="true"/>
		<flag id="graph_cmdLineTester" value="true"/>
		<flag id="graph_compile" value="true"/>
		<flag id="graph_copyJ2SEWinFS" value="true"/>
		<flag id="graph_enableTesting" value="false"/>
		<flag id="graph_enableTesting_Java8" value="true"/>
		<flag id="graph_includeThrstatetest" value="true"/>
		<flag id="graph_j2seSanity" value="true"/>
		<flag id="graph_jgrinder" value="true"/>
		<flag id="graph_nfsUploadForRebuild" value="true"/>
		<flag id="graph_plumhall" value="true"/>
		<flag id="graph_tck" value="true"/>
		<flag id="graph_useJTCTestingPlaylist" value="true"/>
		<flag id="graph_verification" value="true"/>
		<flag id="interp_aotCompileSupport" value="true"/>
		<flag id="interp_aotRuntimeSupport" value="true"/>
		<flag id="interp_debugSupport" value="true"/>
		<flag id="interp_enableJitOnDesktop" value="true"/>
		<flag id="interp_flagsInClassSlot" value="true"/>
		<flag id="interp_gpHandler" value="true"/>
		<flag id="interp_growableStacks" value="true"/>
		<flag id="interp_hotCodeReplacement" value="true"/>
		<flag id="interp_nativeSupport" value="true"/>
		<flag id="interp_profilingBytecodes" value="true"/>
		<flag id="interp_sigQuitThread" value="true"/>
		<flag id="ive_jxeFileRelocator" value="true"/>
		<flag id="ive_jxeInPlaceRelocator" value="true"/>
		<flag id="ive_jxeNatives" value="true"/>
		<flag id="ive_jxeOERelocator" value="true"/>
		<flag id="ive_jxeStreamingRelocator" value="true"/>
		<flag id="ive_romImageHelpers" value="true"/>
		<flag id="jit_classUnloadRwmonitor" value="true"/>
		<flag id="jit_dynamicLoopTransfer" value="true"/>
		<flag id="jit_fullSpeedDebug" value="true"/>
		<flag id="jit_gcOnResolveSupport" value="true"/>
		<flag id="jit_newDualHelpers" value="true"/>
		<flag id="jit_newInstancePrototype" value="true"/>
		<flag id="jit_requiresTrapHandler" value="true"/>
		<flag id="jit_supportsDirectJNI" value="true"/>
		<flag id="module_algorithm_test" value="true"/>
		<flag id="module_bcproftest" value="true"/>
		<flag id="module_bcutil" value="true"/>
		<flag id="module_bcverify" value="true"/>
		<flag id="module_cassume" value="true"/>
		<flag id="module_cfdumper" value="true"/>
		<flag id="module_codegen_common" value="true"/>
		<flag id="module_codegen_ia32" value="true"/>
		<flag id="module_codegen_ilgen" value="true"/>
		<flag id="module_codegen_opt" value="true"/>
		<flag id="module_codert_common" value="true"/>
		<flag id="module_codert_ia32" value="true"/>
		<flag id="module_codert_vm" value="true"/>
		<flag id="module_ddr" value="true"/>
		<flag id="module_gptest" value="true"/>
		<flag id="module_j9vm" value="true"/>
		<flag id="module_j9vmtest" value="true"/>
		<flag id="module_jextractnatives" value="true"/>
		<flag id="module_jit_common" value="true"/>
		<flag id="module_jit_ia32" value="true"/>
		<flag id="module_jit_vm" value="true"/>
		<flag id="module_jitrt_common" value="true"/>
		<flag id="module_jitrt_ia32" value="true"/>
		<flag id="module_jniargtests" value="true"/>
		<flag id="module_jnichk" value="true"/>
		<flag id="module_jniinv" value="true"/>
		<flag id="module_jnitest" value="true"/>
		<flag id="module_jvmti" value="true"/>
		<flag id="module_jvmtitst" value="true"/>
		<flag id="module_lifecycle_tests" value="true"/>
		<flag id="module_porttest" value="true"/>
		<flag id="module_rasdump" value="true"/>
		<flag id="module_rastrace" value="true"/>
		<flag id="module_shared" value="true"/>
		<flag id="module_shared_common" value="true"/>
		<flag id="module_shared_servicetest" value="true"/>
		<flag id="module_shared_test" value="true"/>
		<flag id="module_shared_util" value="true"/>
		<flag id="module_thrtrace" value="true"/>
		<flag id="module_verbose" value="true"/>
		<flag id="module_zip" value="true"/>
		<flag id="module_zlib" value="true"/>
		<flag id="opt_annotations" value="true"/>
		<flag id="opt_bigInteger" value="true"/>
		<flag id="opt_debugInfoServer" value="true"/>
		<flag id="opt_debugJsr45Support" value="true"/>
		<flag id="opt_deprecatedMethods" value="true"/>
		<flag id="opt_dynamicLoadSupport" value="true"/>
		<flag id="opt_invariantInterning" value="true"/>
		<flag id="opt_jvmti" value="true"/>
		<flag id="opt_jxeLoadSupport" value="true"/>
		<flag id="opt_memoryCheckSupport" value="true"/>
		<flag id="opt_multiVm" value="true"/>
		<flag id="opt_nativeCharacterConverter" value="true"/>
		<flag id="opt_reflect" value="true"/>
		<flag id="opt_sharedClasses" value="true"/>
		<flag id="opt_sidecar" value="true"/>
		<flag id="opt_srpAvlTreeSupport" value="true"/>
		<flag id="opt_stringCompression" value="true"/>
		<flag id="opt_switchStacksForSignalHandler" value="true"/>
		<flag id="opt_useFfi" value="true"/>
		<flag id="opt_useFfiOnly" value="true"/>
		<flag id="opt_valhallaValueTypes" value="false"/>
		<flag id="opt_zero" value="true"/>
		<flag id="opt_zipSupport" value="true"/>
		<flag id="opt_zlibCompression" value="true"/>
		<flag id="opt_zlibSupport" value="true"/>
		<flag id="port_omrsigSupport" value="true"/>
		<flag id="port_signalSupport" value="true"/>
		<flag id="prof_eventReporting" value="true"/>
		<flag id="ras_dumpAgents" value="true"/>
		<flag id="ras_eyecatchers" value="true"/>
		<flag id="size_optimizeSendTargets" value="true"/>
		<flag id="test_cunit" value="true"/>
		<flag id="test_jvmti" value="true"/>
		<flag id="thr_lockNursery" value="true"/>
		<flag id="thr_lockReservation" value="true"/>
		<flag id="thr_smartDeflation" value="true"/>
		<flag id="uma_windowsRebase" value="true"/>
	</flags>
</spec>

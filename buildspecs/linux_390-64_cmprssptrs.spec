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
<spec xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://www.ibm.com/j9/builder/spec" xsi:schemaLocation="http://www.ibm.com/j9/builder/spec spec-v1.xsd" id="linux_390-64_cmprssptrs">
	<name>Linux S390 64bit Compressed Pointers</name>
	<asmBuilderName>LinuxS390</asmBuilderName>
	<cpuArchitecture>390</cpuArchitecture>
	<os>linux</os>
	<defaultJCL>Sidecar</defaultJCL>
	<defaultSizes>desktop (256M + big OS stack)</defaultSizes>
	<priority>150</priority>
	<owners>
		<owner>peter_burka@ca.ibm.com</owner>
	</owners>
	<properties>
		<property name="SE6_extension" value="tar.gz"/>
		<property name="SE6_package" value="xz64"/>
		<property name="canTestLocally" value="true"/>
		<property name="complianceTestingJavaLibraryPath" value="linux_390-64"/>
		<property name="directoryDelimiter" value="/"/>
		<property name="doesUseSPECjbbLite" value="true"/>
		<property name="graph_arch.cpu" value="{$spec.arch.cpuISA$}"/>
		<property name="graph_commands.chroot" value=""/>
		<property name="graph_commands.unix.remote_host" value=""/>
		<property name="graph_datamines" value="commands.unix.datamine,site-zLinux.datamine,use.local.datamine"/>
		<property name="graph_enable_compiler_cmd" value="source {$buildinfo.fsroot.linux390Bin$}/set_gcc7.5_env &amp;&amp;"/>
		<property name="graph_label.classlib" value="150"/>
		<property name="graph_label.java5" value="j9vmxz64cmprssptrs24"/>
		<property name="graph_label.java6" value="pxz64cmprssptrs60"/>
		<property name="graph_label.java60_26" value="pxz64cmprssptrs60_26"/>
		<property name="graph_label.java6_rebuilt_extension" value="zip"/>
		<property name="graph_label.java7" value="pxz64cmprssptrs70"/>
		<property name="graph_label.java70_27" value="pxz64cmprssptrs70_27"/>
		<property name="graph_label.java8" value="pxz64cmprssptrs80"/>
		<property name="graph_label.java9" value="pxz64cmprssptrs90"/>
		<property name="graph_label.osid" value="lnx"/>
		<property name="graph_label.profile" value="_cmprssptrs"/>
		<property name="graph_make_parallel_arg" value="-j `numberOfCPUs`"/>
		<property name="graph_req.arch0" value="arch:390"/>
		<property name="graph_req.arch1" value="arch:64bit"/>
		<property name="graph_req.aux0" value=""/>
		<property name="graph_req.aux1" value=""/>
		<property name="graph_req.build" value="{$common.req.build.java9$}"/>
		<property name="graph_req.build2" value="{$common.req.build.java8$}"/>
		<property name="graph_req.machine" value="{$machine_mapping.s390$}"/>
		<property name="graph_req.machine.test" value="{$spec.property.graph_req.machine$}"/>
		<property name="graph_req.os" value="{$machine_mapping.linux$}"/>
		<property name="graph_req.os.build" value="{$spec.property.graph_req.os$}"/>
		<property name="graph_req.os.perf" value="{$spec.property.graph_req.os$}"/>
		<property name="graph_se_classlib.java5" value="jcl_se.zip"/>
		<property name="graph_se_classlib.java6" value="jcl_se.zip"/>
		<property name="graph_variant.testing_suffix" value=""/>
		<property name="graph_variant.trailingID" value=""/>
		<property name="isReallyUnix" value="true"/>
		<property name="j2seRuntimeDir" value="jre/lib/s390x"/>
		<property name="j2seTags" value="pxz64cmprssptrs60,j9vmxz64cmprssptrs24"/>
		<property name="j9dt.compileTarget" value="makefile"/>
		<property name="j9dt.make" value="gmake"/>
		<property name="j9dt.toolsTarget" value="buildtools.mk"/>
		<property name="javatestPlatform" value="linux_390-64_cr"/>
		<property name="jclMemoryMax" value="-Xmx64m"/>
		<property name="jclOSStackSizeMax" value=""/>
		<property name="jgrinderTestingSupported" value="true"/>
		<property name="jitTestingOptLevel" value="optlevel=warm"/>
		<property name="localRootPath" value="$(J9_L390_ROOT)"/>
		<property name="longLimitCmd" value=""/>
		<property name="main_shortname" value="xz64"/>
		<property name="os.lineDelimiter" value="unix"/>
		<property name="platform_arch" value="s390x"/>
		<property name="svn_stream" value=""/>
		<property name="uma_make_cmd_ar" value="ar"/>
		<property name="uma_make_cmd_as" value="as"/>
		<property name="uma_make_cmd_cc" value="gcc"/>
		<property name="uma_make_cmd_cpp" value="cpp -E -P"/>
		<property name="uma_make_cmd_cxx" value="g++"/>
		<property name="uma_make_cmd_cxx_dll_ld" value="$(CC)"/>
		<property name="uma_make_cmd_cxx_exe_ld" value="$(CC)"/>
		<property name="uma_make_cmd_dll_ld" value="$(CC)"/>
		<property name="uma_make_cmd_exe_ld" value="$(CC)"/>
		<property name="uma_make_cmd_ranlib" value="ranlib"/>
		<property name="uma_processor" value="s390"/>
		<property name="uma_type" value="unix,linux"/>
	</properties>
	<features>
		<feature id="cmprssptrs"/>
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
		<flag id="arch_s390" value="true"/>
		<flag id="build_SE6_package" value="true"/>
		<flag id="build_autobuild" value="true"/>
		<flag id="build_dropToHursley" value="true"/>
		<flag id="build_dropToToronto" value="true"/>
		<flag id="build_j2se" value="true"/>
		<flag id="build_java6proxy" value="true"/>
		<flag id="build_java8" value="true"/>
		<flag id="build_java9" value="false"/>
		<flag id="build_product" value="true"/>
		<flag id="build_openj9" value="true"/>
		<flag id="build_openj9JDK8" value="true"/>
		<flag id="env_hasFPU" value="true"/>
		<flag id="env_sharedLibsCalleeGlobalTableSetup" value="true"/>
		<flag id="env_sharedLibsUseGlobalTable" value="true"/>
		<flag id="gc_batchClearTLH" value="true"/>
		<flag id="gc_debugAsserts" value="true"/>
		<flag id="gc_inlinedAllocFields" value="true"/>
		<flag id="gc_enableDoubleMap" value="true"/>
		<flag id="gc_minimumObjectSize" value="true"/>
		<flag id="gc_subpoolsAlias" value="true"/>
		<flag id="graph_cmdLineTester" value="true"/>
		<flag id="graph_compile" value="true"/>
		<flag id="graph_copyJ2SEToToronto" value="true"/>
		<flag id="graph_copyJ2SEViaNovell" value="true"/>
		<flag id="graph_enableModularityTesting" value="false"/>
		<flag id="graph_enableTesting" value="false"/>
		<flag id="graph_enableTesting_Java8" value="true"/>
		<flag id="graph_excludeUnzipTestSuite390" value="true"/>
		<flag id="graph_j2seSanity" value="true"/>
		<flag id="graph_nfsUploadForRebuild" value="true"/>
		<flag id="graph_plumhall" value="true"/>
		<flag id="graph_toronto" value="true"/>
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
		<flag id="interp_useUnsafeHelper" value="true"/>
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
		<flag id="jit_runtimeInstrumentation" value="true"/>
		<flag id="jit_supportsDirectJNI" value="true"/>
		<flag id="jit_transactionDiagnosticThreadBlock" value="true"/>
		<flag id="module_algorithm_test" value="true"/>
		<flag id="module_bcutil" value="true"/>
		<flag id="module_bcverify" value="true"/>
		<flag id="module_cassume" value="true"/>
		<flag id="module_cfdumper" value="true"/>
		<flag id="module_codegen_common" value="true"/>
		<flag id="module_codegen_comsched" value="true"/>
		<flag id="module_codegen_ilgen" value="true"/>
		<flag id="module_codegen_opt" value="true"/>
		<flag id="module_codegen_s390" value="true"/>
		<flag id="module_codegen_sched" value="true"/>
		<flag id="module_codert_common" value="true"/>
		<flag id="module_codert_s390" value="true"/>
		<flag id="module_codert_vm" value="true"/>
		<flag id="module_ddr" value="true"/>
		<flag id="module_gptest" value="true"/>
		<flag id="module_j9vm" value="true"/>
		<flag id="module_j9vmtest" value="true"/>
		<flag id="module_jextractnatives" value="true"/>
		<flag id="module_jit_common" value="true"/>
		<flag id="module_jit_s390" value="true"/>
		<flag id="module_jit_vm" value="true"/>
		<flag id="module_jitrt_common" value="true"/>
		<flag id="module_jitrt_s390" value="true"/>
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
		<flag id="module_shared_test" value="true"/>
		<flag id="module_shared_util" value="true"/>
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
		<flag id="opt_reflect" value="true"/>
		<flag id="opt_sharedClasses" value="true"/>
		<flag id="opt_sidecar" value="true"/>
		<flag id="opt_srpAvlTreeSupport" value="true"/>
		<flag id="opt_stringCompression" value="true"/>
		<flag id="opt_useFfi" value="true"/>
		<flag id="opt_useFfiOnly" value="true"/>
		<flag id="opt_valhallaValueTypes" value="false"/>
		<flag id="opt_zipSupport" value="true"/>
		<flag id="opt_zlibCompression" value="true"/>
		<flag id="opt_zlibSupport" value="true"/>
		<flag id="port_omrsigSupport" value="true"/>
		<flag id="port_runtimeInstrumentation" value="true"/>
		<flag id="port_signalSupport" value="true"/>
		<flag id="prof_eventReporting" value="true"/>
		<flag id="ras_dumpAgents" value="true"/>
		<flag id="ras_eyecatchers" value="true"/>
		<flag id="size_optimizeSendTargets" value="true"/>
		<flag id="test_cunit" value="true"/>
		<flag id="test_jvmti" value="true"/>
		<flag id="thr_asyncNameUpdate" value="true"/>
		<flag id="thr_lockNursery" value="true"/>
		<flag id="thr_lockReservation" value="true"/>
		<flag id="thr_smartDeflation" value="true"/>
		<flag id="uma_gnuDebugSymbols" value="true"/>
		<flag id="uma_supportsIpv6" value="true"/>
	</flags>
</spec>

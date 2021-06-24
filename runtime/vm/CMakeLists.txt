################################################################################
# Copyright (c) 2017, 2021 IBM Corp. and others
#
# This program and the accompanying materials are made available under
# the terms of the Eclipse Public License 2.0 which accompanies this
# distribution and is available at https://www.eclipse.org/legal/epl-2.0/
# or the Apache License, Version 2.0 which accompanies this distribution and
# is available at https://www.apache.org/licenses/LICENSE-2.0.
#
# This Source Code may also be made available under the following
# Secondary Licenses when the conditions for such availability set
# forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
# General Public License, version 2 with the GNU Classpath
# Exception [1] and GNU General Public License, version 2 with the
# OpenJDK Assembly Exception [2].
#
# [1] https://www.gnu.org/software/classpath/license.html
# [2] http://openjdk.java.net/legal/assembly-exception.html
#
# SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
################################################################################

omr_add_tracegen(j9vm.tdf)

if(OMR_TOOLCONFIG STREQUAL "msvc")
	# bypass omr provided warning flag handling, because we don't want
	# the warning flags to get passed to clang.
	# Note: the updated variables are scoped to this directory
	set(OMR_ENHANCED_WARNINGS OFF)
	set(OMR_WARNINGS_AS_ERRORS OFF)
	set(OMR_BASE_WARNING_FLAGS)
	omr_append_flags(CMAKE_C_FLAGS ${OMR_ENHANCED_WARNING_FLAG} ${OMR_WARNING_AS_ERROR_FLAG})
	omr_append_flags(CMAKE_CXX_FLAGS ${OMR_ENHANCED_WARNING_FLAG} ${OMR_WARNING_AS_ERROR_FLAG})
endif()

set(interp_flags_to_remove) # list of flags removed when compiling the interpreter
set(interp_new_flags) # flags to be added when compiling the interpreter

# Note: the uma makefiles also specify this for pLinux, however it appears we no longer
# support building the interpreter with xlc on linux
if(OMR_ARCH_POWER AND OMR_OS_AIX)
	if(OMR_TOOLCONFIG STREQUAL "xlc")
		list(APPEND interp_flags_to_remove "-O3" "-g")
		list(APPEND interp_new_flags "-O2" "-qdebug=lincomm:ptranl:tfbagg")
		# Note: the following code is specified in the UMA builds, however,
		# it isn't currently reachable, so it is block commneted out ( #[[ ]] )
		#[[
		if(OMR_OS_LINUX)
			# TODO -qpic=large -> -qpic
			# however at the moment all plinux builds with xlc use -qpic anyway
			list(APPEND interp_new_flags "-qmaxmem=-1")
		endif()
		]]
	endif()
elseif(OMR_OS_ZOS)
	#TODO need to handle optimized debug info
	list(APPEND interp_flags_to_remove "-O3" "-g" "-qdebug=nohook")

	list(APPEND interp_new_flags
		-O2
		"\"-Wc,TBYDBG(-qdebug=MRABIG)\""
		"\"-Wc,TBYDBG(-qdebug=lincomm:ptranl:tfbagg)\""
		"\"-Wc,FEDBG(-qxflag=InlineDespiteVolatileInArgs)\""
	)
endif()

if(interp_flags_to_remove)
	# Keep track of which flags we have actually removed from CMAKE_CXX_FLAGS,
	# so that later we don't try re-adding flags that never existed
	set(removed_flags)
	foreach(flag IN LISTS interp_flags_to_remove)
		set(old_cxx_flags ${CMAKE_CXX_FLAGS})
		omr_remove_flags(CMAKE_CXX_FLAGS "${flag}")
		if(NOT old_cxx_flags STREQUAL CMAKE_CXX_FLAGS)
			list(APPEND removed_flags "${flag}")
		endif()
	endforeach()

	set(interp_flags_to_remove ${removed_flags})
endif()

set(main_sources
	annsup.c
	AsyncMessageHandler.cpp
	bchelper.c
	bindnatv.cpp
	callin.cpp
	classallocation.c
	ClassInitialization.cpp
	classloadersearch.c
	classseg.c
	classsupport.c
	createramclass.cpp
	criuhelpers.cpp
	description.c
	dllsup.c
	drophelp.c
	exceptiondescribe.c
	exceptionsupport.c
	extendedMessageNPE.cpp
	FastJNI_com_ibm_oti_vm_VM.cpp
	FastJNI.cpp
	FastJNI_java_lang_Class.cpp
	FastJNI_java_lang_ClassLoader.cpp
	FastJNI_java_lang_invoke_MethodHandle.cpp
	FastJNI_java_lang_J9VMInternals.cpp
	FastJNI_java_lang_Object.cpp
	FastJNI_java_lang_reflect_Array.cpp
	FastJNI_java_lang_ref_Reference.cpp
	FastJNI_java_lang_String.cpp
	FastJNI_java_lang_System.cpp
	FastJNI_java_lang_Thread.cpp
	FastJNI_java_lang_Throwable.cpp
	FastJNI_sun_misc_Unsafe.cpp
	findmethod.c
	FlushProcessWriteBuffers.cpp
	gphandle.c
	growstack.cpp
	guardedstorage.c
	hookableAsync.c
	initsendtarget.cpp
	intfunc.c
	J9OMRHelpers.cpp
	javaPriority.c
	jnicgen.c
	jnicsup.cpp
	jnifield.cpp
	jniinv.c
	jnimem.c
	jnimisc.cpp
	jnireflect.cpp
	jvmfree.c
	jvminit.c
	jvminitcommon.c
	jvmrisup.c
	KeyHashTable.c
	leconditionexceptionsup.c
	linearswalk.c
	lockwordconfig.c
	logsupport.c
	lookuphelper.c
	lookupmethod.c
	ModularityHashTables.c
	monhelpers.c
	montable.c
	NativeHelpers.cpp
	ObjectFieldInfo.cpp
	ObjectMonitor.cpp
	OutOfLineINL_com_ibm_jit_JITHelpers.cpp
	OutOfLineINL_jdk_internal_misc_Unsafe.cpp
	ownedmonitors.c
	profilingbc.c
	rasdump.c
	rastrace.c
	resolvefield.cpp
	resolvesupport.cpp
	romclasses.c
	romutil.c
	segment.c
	StackDumper.c
	statistics.c
	stringhelpers.cpp
	swalk.c
	threadhelp.cpp
	threadpark.c
	throwexception.c
	ValueTypeHelpers.cpp
	visible.c
	VMAccess.cpp
	vmbootlib.c
	vmhook.c
	vmifunc.c
	vmizip.c
	vmphases.c
	vmprops.c
	vmruntimestate.c
	vmthinit.c
	vmthread.c
	xcheck.c

	${CMAKE_CURRENT_BINARY_DIR}/ut_j9vm.c
)
set(interpreter_sources
	BytecodeInterpreterCompressed.cpp
	BytecodeInterpreterFull.cpp
	DebugBytecodeInterpreterCompressed.cpp
	DebugBytecodeInterpreterFull.cpp
	MHInterpreterCompressed.cpp
	MHInterpreterFull.cpp
)

j9vm_add_library(j9vm SHARED
	OUTPUT_NAME j9vm${J9VM_VERSION_SUFFIX}
	${main_sources}
	${interpreter_sources}
)

if(interp_flags_to_remove)
	# Since we removed the flags from CMAKE_CXX_FLAGS, we need to add them back
	# to everything except the compiler
	set(cpp_sources)
	foreach(srcfile IN LISTS main_sources)
		get_filename_component(extension "${srcfile}" EXT)
		if(extension STREQUAL ".cpp")
			list(APPEND cpp_sources "${srcfile}")
		endif()
	endforeach()

	# COMPILE_FLAGS property expects a string, not a list
	omr_stringify(flags_str ${interp_flags_to_remove})
	set_property(SOURCE ${cpp_sources} APPEND_STRING PROPERTY COMPILE_FLAGS " ${flags_str}")
endif()

if(interp_new_flags)
	# COMPILE_FLAGS property expects a string, not a list
	omr_stringify(flags_str ${interp_new_flags})
	set_property(SOURCE ${interpreter_sources} APPEND_STRING PROPERTY COMPILE_FLAGS " ${flags_str}")
endif()


if(OMR_OS_WINDOWS AND (OMR_TOOLCONFIG STREQUAL "msvc"))
	# JIT helper methods require us to disable buffer security checks on Windows
	# See https://github.com/eclipse-openj9/openj9/pull/1494 for rationale
	set_property(
		SOURCE
			cnathelp.cpp
			SharedService.c
		APPEND PROPERTY COMPILE_FLAGS "/GS-"
	)

	# Use J9VM_CXX compiler (aka clang) to build the interpreter
	enable_language(J9VM_CXX)
	set_source_files_properties(
		${interpreter_sources}
		PROPERTIES
		LANGUAGE J9VM_CXX
	)
endif()

if(OMR_ARCH_X86)
	j9vm_gen_asm(xcinterp.m4)

	if((OMR_OS_LINUX OR OMR_OS_OSX) AND OMR_ENV_DATA64)
		j9vm_gen_asm(
			xa64/stackswap.m4
			xa64/unsafeHelper.m4
		)
		target_sources(j9vm PRIVATE unsafeHelper.s)
	elseif(OMR_OS_WINDOWS)
		if(OMR_ENV_DATA64)
			j9vm_gen_asm(wa64/stackswap.m4)
		else()
			j9vm_gen_asm(wi32/stackswap.m4)
			target_sources(j9vm PRIVATE
				wi32/inlineseh.c
				safeseh.asm
			)

			set_source_files_properties(safeseh.asm PROPERTIES LANGUAGE ASM_MASM)
		endif()
	else()
		message(SEND_ERROR "Unsupported platform")
	endif()

	target_sources(j9vm
		PRIVATE
			xcinterp.s
			stackswap.s
	)
elseif(OMR_ARCH_POWER)
	j9vm_gen_asm(pcinterp.m4)
	target_sources(j9vm PRIVATE pcinterp.s)
	if(OMR_OS_LINUX)
		if(NOT (OMR_ENV_DATA64 AND OMR_ENV_LITTLE_ENDIAN))
			message(SEND_ERROR "Only PPC64 LE is currently supported")
		endif()
		target_sources(j9vm PRIVATE xl64/unsafeHelper.s)
	elseif(OMR_OS_AIX)
		if(OMR_ENV_DATA64)
			target_sources(j9vm PRIVATE ap64/unsafeHelper.s)
		else()
			target_sources(j9vm PRIVATE ap32/unsafeHelper.s)
		endif()
		if(OMR_TOOLCONFIG STREQUAL "xlc")
			# Suppress warning "Infinite loop. Program may not stop"
			target_compile_options(j9vm PRIVATE "-qsuppress=1500-010")
		endif()
	else()
		message(SEND_ERROR "Unsupported platfrom")
	endif()
elseif(OMR_ARCH_S390)
	j9vm_gen_asm(zcinterp.m4)
	target_sources(j9vm PRIVATE zcinterp.s)
	if(OMR_OS_LINUX)
		if(OMR_ENV_DATA64)
			target_sources(j9vm PRIVATE xz64/unsafeHelper.s)
		else()
			target_sources(j9vm PRIVATE xz31/unsafeHelper.s)
		endif()
	elseif(OMR_OS_ZOS)
		if(OMR_ENV_DATA64)
			target_sources(j9vm PRIVATE mz64/unsafeHelper.s)
		else()
			target_sources(j9vm PRIVATE
				mz31/unsafeHelper.s
				inlineleconditionhandler.c
				leconditionexceptionsup.c
			)
		endif()
	else()
		message(SEND_ERROR "Unsupported platform")
	endif()
elseif(OMR_ARCH_ARM)
	j9vm_gen_asm(armcinterp.m4)
	target_sources(j9vm PRIVATE armcinterp.s)
	if(OMR_OS_LINUX)
		target_sources(j9vm PRIVATE xr32/unsafeHelper.s)
	else()
		message(SEND_ERROR "Unsupported OS")
	endif()
elseif(OMR_ARCH_AARCH64)
	j9vm_gen_asm(arm64cinterp.m4)
	target_sources(j9vm PRIVATE arm64cinterp.s)
	if(OMR_OS_LINUX)
		target_sources(j9vm PRIVATE xr64/unsafeHelper.s)
	else()
		message(SEND_ERROR "Unsupported OS")
	endif()
else()
	message(SEND_ERROR "Unsupported platform")
endif()

target_include_directories(j9vm
	PRIVATE
		${j9vm_SOURCE_DIR}/shared_common/include
		${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(j9vm
	PRIVATE
		j9vm_interface
		j9vm_gc_includes

		omrcore
		j9verutil
		j9thr
		j9util
		j9utilcore
		j9avl
		j9hashtable
		j9pool
		j9stackmap
		j9hookable
		j9prt
		j9bcv
		j9dyn
		j9simplepool
		j9zip
		ffi

		# link hacks
		# TODO: figure out why CMake needs these, but UMA does not
		j9util
		j9simplepool
		j9verutil
		j9stackmap
		j9bcv
		j9dyn
		j9util
		j9thr
)

omr_add_exports(j9vm
	J9_CreateJavaVM
	J9_GetCreatedJavaVMs
	J9_GetInterface
)

if(J9VM_INTERP_TRACING)
	omr_add_exports(j9vm trace)
endif()

target_enable_ddr(j9vm GLOB_HEADERS)
ddr_set_add_targets(j9ddr j9vm)

install(
	TARGETS j9vm
	LIBRARY DESTINATION ${j9vm_SOURCE_DIR}
	RUNTIME DESTINATION ${j9vm_SOURCE_DIR}
)

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

include(CheckCXXCompilerFlag)
# This CMakeLists is included by the VM CMake lists, and works after composition
# has occurred.
#
# Relies on a few pieces of OMR, and a few pieces of the VM CMakeLists.
#
# Longer term, this will of course have to collapse into the VM builds.

if(OMR_ARCH_X86)
	# On windows, the proper binary format is not auto detected
	if(OMR_OS_WINDOWS)
		if(OMR_ENV_DATA64)
			set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
		else()
			set(CMAKE_ASM_NASM_OBJECT_FORMAT win32)
		endif()
	endif()
	enable_language(ASM_NASM)
	# We have to manually append "/" to the paths as NASM versions older than v2.14 requires trailing / in the directory paths
	set(asm_inc_dirs
		"-I${j9vm_SOURCE_DIR}/oti/"
		"-I${j9vm_BINARY_DIR}/oti/"
		"-I${CMAKE_CURRENT_SOURCE_DIR}/"
		"-I${CMAKE_CURRENT_SOURCE_DIR}/x/runtime/"
		"-I${CMAKE_CURRENT_SOURCE_DIR}/x/amd64/runtime/"
		"-I${CMAKE_CURRENT_SOURCE_DIR}/x/i386/runtime/"
	)
	omr_append_flags(CMAKE_ASM_NASM_FLAGS ${asm_inc_dirs})
	# For whatever reason cmake does not apply compile definitions when building nasm objects.
	# The if guard is here in case they ever start doing so.
	if(NOT CMAKE_ASM_NASM_COMPILE_OBJECT MATCHES "<DEFINES>")
		# Tack on defines immediately following the compiler name.
		string(REPLACE
			"<CMAKE_ASM_NASM_COMPILER>"
			"<CMAKE_ASM_NASM_COMPILER> <DEFINES>"
			CMAKE_ASM_NASM_COMPILE_OBJECT
			"${CMAKE_ASM_NASM_COMPILE_OBJECT}"
		)

	endif()
endif()

# Using the linker option -static-libgcc results in an error on OSX. The option -static-libstdc++ is unused. Therefore
# these options have been excluded from OSX
if((OMR_TOOLCONFIG STREQUAL "gnu") AND (NOT OMR_OS_OSX))
	check_cxx_compiler_flag("-static-libstdc++" ALLOWS_STATIC_LIBCPP)
endif()

# This target is created so that various subdirectories can add dependencies to it.
# The j9jit target will then depend on it, propagating the dependencies.
# This is required because at the time we include the subdirectories, the j9jit target
# does not yet exist (and as such can't have dependencies addded to it).
add_custom_target(j9jit_generate)

# On windows exceptions are controlled via a the preprocessor define _HAS_EXCEPTIONS
# We need to enable it again, after omr platform config removed it.
get_directory_property(compile_defs COMPILE_DEFINITIONS)
list(REMOVE_ITEM compile_defs "_HAS_EXCEPTIONS=0")
set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS ${compile_defs})

omr_add_tracegen(env/j9jit.tdf)

# this is a workaround because the jit code includes the tracegen header as <env/ut_j9jit.h>
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/env/ut_j9jit.h
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ut_j9jit.h
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/ut_j9jit.h" "${CMAKE_CURRENT_BINARY_DIR}/env/ut_j9jit.h"
	VERBATIM
)
add_custom_target(j9jit_tracegen DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/env/ut_j9jit.h)

# J9VM_OPT_JITSERVER
if(J9VM_OPT_JITSERVER)
	message(STATUS "JITServer is enabled")

	if(OPENSSL_BUNDLE_LIB_PATH) # --enable-openssl-bundling
		set(OPENSSL_ROOT_DIR ${OPENSSL_BUNDLE_LIB_PATH})
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath,$ORIGIN/..")
	elseif(OPENSSL_DIR) # --with-openssl=fetched only
		set(OPENSSL_ROOT_DIR ${OPENSSL_DIR})
	endif()

	find_package(OpenSSL REQUIRED)
	include_directories(${OPENSSL_INCLUDE_DIR})
endif()

#TODO We should get rid of this, but its still required by the compiler_support module in omr
set(J9SRC  ${j9vm_SOURCE_DIR})


#include compiler support module from OMR
include(OmrCompilerSupport)

# Override masm2gas with J9JIT version until we've
# resolved the divergence.
set(masm2gas_path  build/scripts/masm2gas.pl )
get_filename_component(masm2gas_path ${masm2gas_path} ABSOLUTE)
set(MASM2GAS_PATH  ${masm2gas_path} CACHE INTERNAL "MASM2GAS PATH")

# The list of files that are added to the compiler in addition
# To the defaults provided by create_omr_compiler_library
set(J9JIT_FILES "" CACHE INTERNAL "The computed list of j9jit files")

# Used inside added subdirectories to help
# fill in the object list
macro(j9jit_files)
	set(J9JIT_FILES ${J9JIT_FILES} ${ARGN} CACHE INTERNAL "The computed list of j9jit files")
endmacro(j9jit_files)

j9jit_files(${CMAKE_CURRENT_BINARY_DIR}/ut_j9jit.c)
# Add our subdirs.
add_subdirectory(codegen)
add_subdirectory(compile)
add_subdirectory(control)
add_subdirectory(env)
add_subdirectory(il)
add_subdirectory(ilgen)
add_subdirectory(infra)
add_subdirectory(optimizer)
add_subdirectory(ras)
add_subdirectory(runtime)

if(J9VM_OPT_JITSERVER)
	add_subdirectory(net)
endif()

add_subdirectory(${TR_HOST_ARCH})
if(NOT TR_TARGET_ARCH STREQUAL TR_HOST_ARCH)
	add_subdirectory(${TR_TARGET_ARCH})
endif()

# Some files in OMR provide duplicate or extra functionality not needed
# in J9, so we need to remove them
set(REMOVED_OMR_FILES
	${omr_SOURCE_DIR}/compiler/codegen/CCData.cpp
	${omr_SOURCE_DIR}/compiler/control/CompileMethod.cpp
	${omr_SOURCE_DIR}/compiler/control/CompilationController.cpp
	${omr_SOURCE_DIR}/compiler/env/FEBase.cpp
	${omr_SOURCE_DIR}/compiler/optimizer/FEInliner.cpp
	${omr_SOURCE_DIR}/compiler/env/JitConfig.cpp
	${omr_SOURCE_DIR}/compiler/env/PersistentAllocator.cpp
	${omr_SOURCE_DIR}/compiler/env/SystemSegmentProvider.cpp
	${omr_SOURCE_DIR}/compiler/infra/OMRMonitor.cpp
	${omr_SOURCE_DIR}/compiler/runtime/Trampoline.cpp
	${omr_SOURCE_DIR}/compiler/runtime/Runtime.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/IlInjector.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRBytecodeBuilder.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRIlBuilder.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRIlType.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRIlValue.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRJitBuilderRecorder.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRJitBuilderRecorderBinaryBuffer.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRJitBuilderRecorderBinaryFile.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRJitBuilderRecorderTextFile.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRMethodBuilder.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRThunkBuilder.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRTypeDictionary.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRVirtualMachineOperandArray.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRVirtualMachineOperandStack.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRVirtualMachineRegister.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRVirtualMachineRegisterInStruct.cpp
	${omr_SOURCE_DIR}/compiler/ilgen/OMRVirtualMachineState.cpp
)


get_target_property(compiler_defines j9vm_compiler_defines INTERFACE_COMPILE_DEFINITIONS)
# Extra defines not provided by the create_omr_compiler_library call
set(TARGET_DEFINES
	J9_PROJECT_SPECIFIC
	${compiler_defines}
)

# When VM is built with CMake, we should just use the
# INTERFACE specification of target_link_libraries,
# though that would also involve teaching MASM2GAS and
# PASM2ASM about target includes.
#
# In the mean time, like the makefiles, this is using
# the includes from the SDK
set(J9_INCLUDES
	# From jitinclude.mk
	${omr_SOURCE_DIR}/thread
	${omr_SOURCE_DIR}/gc/include
	# endjitinclude.mk
	../ # Frustratingly there are some #include "frob/baz.hpp" refs in the compiler which require this.
	${j9vm_SOURCE_DIR}/codert_vm
	${j9vm_SOURCE_DIR}/gc_include
	${j9vm_SOURCE_DIR}/gc_glue_java
	${j9vm_SOURCE_DIR}/jit_vm
	${j9vm_SOURCE_DIR}/nls
	${j9vm_SOURCE_DIR}/oti
	${j9vm_SOURCE_DIR}/include
	${j9vm_SOURCE_DIR}/util
	${j9vm_BINARY_DIR}
	${j9vm_BINARY_DIR}/oti
	${omr_BINARY_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

# Platform specific list of flags, derived directly from the
# Makefiles
set(J9_SHAREDFLAGS
	${OMR_PLATFORM_COMPILE_OPTIONS}
)
set(J9_CFLAGS
	${OMR_PLATFORM_C_COMPILE_OPTIONS}
)
set(J9_CXXFLAGS
	${OMR_PLATFORM_CXX_COMPILE_OPTIONS}
)
if(OMR_OS_LINUX OR OMR_OS_OSX)
	list(APPEND J9_SHAREDFLAGS
		-fno-strict-aliasing
		-Wno-deprecated
		-Wno-enum-compare
		-Wno-write-strings
		-fomit-frame-pointer
		-fasynchronous-unwind-tables
		-Wreturn-type
		-pthread
	)

	# Platform specific CXX flags, also derived from the
	# Makefiles
	list(APPEND J9_CXXFLAGS
		-fno-threadsafe-statics
		-Wno-invalid-offsetof
	)
	if(NOT J9VM_OPT_JITSERVER)
		list(APPEND J9_CXXFLAGS -fno-rtti)
	endif()

	list(APPEND J9_CFLAGS -std=gnu89)
elseif(OMR_OS_WINDOWS)
	list(APPEND J9_SHAREDFLAGS
		-D_WIN32
		-DWIN32
		-DSUPPORTS_THREAD_LOCAL
		-DWINDOWS
	)
elseif(OMR_OS_AIX)
	list(APPEND J9_SHAREDFLAGS
		-DAIXPPC
		-DRS6000
		-D_XOPEN_SOURCE_EXTENDED=1
		-D_ALL_SOURCE
		-DSUPPORTS_THREAD_LOCAL
	)
	if(OMR_ENV_DATA64)
		list(APPEND J9_SHAREDFLAGS -q64)
		if(OMR_TOOLCONFIG STREQUAL "xlc")
			# Modify the arch tuning value we inherit from omr
			list(REMOVE_ITEM TR_COMPILE_OPTIONS "-qarch=pwr7")
			list(APPEND TR_COMPILE_OPTIONS "-qarch=ppc64grsq")

			if(CMAKE_C_COMPILER_IS_XLCLANG)
				# xlclang/xlclang++ options
				list(APPEND SPP_FLAGS -qlanglvl=extc99)
				set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -lc++")
			endif()
		endif()
	else()
		list(APPEND J9_SHAREDFLAGS -q32)
	endif()
elseif(OMR_OS_ZOS)
	list(APPEND TARGET_DEFINES
		J9VM_TIERED_CODE_CACHE
		MAXMOVE
	)
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L/usr/lpp/hzc/lib")
else()
	message(SEND_ERROR "unsupported platform")
endif()

if(OMR_ARCH_S390)
	list(APPEND TARGET_DEFINES COMPRESS_AOT_DATA)
endif()

# Add optional user-specified compiler flags that only apply to the JIT
list(APPEND J9_CFLAGS ${J9JIT_EXTRA_CFLAGS})
list(APPEND J9_CXXFLAGS ${J9JIT_EXTRA_CXXFLAGS})

# Note: J9_CFLAGS and J9_CXXFLAGS are appended after J9_SHAREDFLAGS so that
#       OMR_PLATFORM_C_COMPILE_OPTIONS and OMR_PLATFORM_CXX_COMPILE_OPTIONS
#       end up after OMR_PLATFORM_COMPILE_OPTIONS, which matches the rest
#       of the VM build (see cmake/modules/OmrPlatform.cmake in OMR).
#       This allows the user to pass extra compiler options via
#       OMR_PLATFORM_C_COMPILE_OPTIONS and OMR_PLATFORM_CXX_COMPILE_OPTIONS
#       to override the defaults specified by OMR_PLATFORM_COMPILE_OPTIONS,
#       e.g. in order to produce a debug build.
omr_stringify(J9_CFLAGS_STR ${J9_SHAREDFLAGS} ${J9_CFLAGS})
omr_stringify(J9_CXXFLAGS_STR ${J9_SHAREDFLAGS} ${J9_CXXFLAGS})

# Note: This is explicitly overriding what's provided by
#       the VM CMakeLists, as they pass -fno-exceptions
#       right now, and the JIT needs exceptions
set(CMAKE_CXX_FLAGS "${J9_CXXFLAGS_STR}")
set(CMAKE_C_FLAGS "${J9_CFLAGS_STR}")

if(OMR_ARCH_X86 OR OMR_ARCH_S390)
	set(OMR_WARNINGS_AS_ERRORS ON)
	set(OMR_ENHANCED_WARNINGS OFF)
else()
	set(OMR_WARNINGS_AS_ERRORS OFF)
	set(OMR_ENHANCED_WARNINGS OFF)
endif()

create_omr_compiler_library(NAME j9jit
	SHARED
	OUTPUT_NAME j9jit${J9VM_VERSION_SUFFIX}
	OBJECTS ${J9_FILES} ${J9JIT_FILES} ${NOT_EXFRRED}
	DEFINES J9_PROJECT_SPECIFIC ${TARGET_DEFINES}
	INCLUDES ${J9_INCLUDES}
	FILTER ${REMOVED_OMR_FILES}
)

add_dependencies(j9jit
	omrgc_hookgen
	j9jit_tracegen
	j9jit_generate
)

target_link_libraries(j9jit
	PRIVATE
		j9vm_interface
		j9avl
		j9codert_vm
		j9hashtable
		j9jit_vm
		j9pool
		j9stackmap
		j9util
		j9utilcore
		j9hookable
		j9thr
		${CMAKE_DL_LIBS}
)

# This is a bit hokey, but cmake can't track the fact that files are generated across directories.
# Note: while these are only needed on z, setting the properties unconditionally has no ill-effect.
set_source_files_properties(
	${CMAKE_CURRENT_BINARY_DIR}/z/runtime/Math.s
	${CMAKE_CURRENT_BINARY_DIR}/z/runtime/PicBuilder.s
	${CMAKE_CURRENT_BINARY_DIR}/z/runtime/Recompilation.s
	${CMAKE_CURRENT_BINARY_DIR}/z/runtime/ValueProf.s
	PROPERTIES
	GENERATED TRUE
)

if(OMR_OS_LINUX)
	set_property(TARGET j9jit APPEND_STRING PROPERTY
		LINK_FLAGS "  -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/build/scripts/j9jit.linux.exp")
	target_link_libraries(j9jit PRIVATE m)
elseif(OMR_OS_WINDOWS)
	target_sources(j9jit PRIVATE build/scripts/j9jit.def)
elseif(OMR_OS_AIX)
	set_property(TARGET j9jit APPEND_STRING PROPERTY
		LINK_FLAGS " -Wl,-bE:${CMAKE_CURRENT_SOURCE_DIR}/build/scripts/j9jit.aix.exp")
endif()
if((OMR_TOOLCONFIG STREQUAL "gnu") AND ALLOWS_STATIC_LIBCPP)
	# We assume that if the compiler allows -static-libstdc++
	# it will also allow -static-libgcc
	set_property(TARGET j9jit APPEND_STRING PROPERTY
		LINK_FLAGS " -static-libgcc -static-libstdc++")
endif()

if(OMR_ARCH_S390)
	if(OMR_OS_ZOS)
		target_include_directories(j9jit BEFORE PRIVATE /usr/lpp/hzc/include)
		target_link_libraries(j9jit PRIVATE zz)
	else()
		target_link_libraries(j9jit PRIVATE j9zlib)
	endif()
endif()

set_property(TARGET j9jit PROPERTY LINKER_LANGUAGE CXX)

# Note ddrgen can't handle the templated symbols used in the jit
target_enable_ddr(j9jit NO_DEBUG_INFO)
file(GLOB_RECURSE headers
	${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h
)
ddr_add_headers(j9jit
	${headers}
)
ddr_set_add_targets(omrddr j9jit)


if(OMR_OS_ZOS)
	# Make sure that -Wc,EXPORTALL is applied
	omr_process_exports(j9jit)
endif()

install(
	TARGETS j9jit
	LIBRARY DESTINATION ${j9vm_SOURCE_DIR}
	RUNTIME DESTINATION ${j9vm_SOURCE_DIR}
)

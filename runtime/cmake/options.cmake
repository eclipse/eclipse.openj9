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

include(OmrAssert)

# creates an option which shadows an omr option of the same/similar name
# j9vm_shadowed_option(<j9_name> [<omr_name>] <help_text>)
# create a new option named <j9_name>. This option will override the value
# of <omr_name>. If <omr_name> is not supplied it defaults to <j9_name>
# with the leading 'J9VM_' replaced with 'OMR_'
function(j9vm_shadowed_option j9_opt_name)
    if(ARGC EQUAL 1)
        message(FATAL_ERROR "Too few arguments passed to j9vm_shadowed_option")
    elseif(ARGC EQUAL 2)
        omr_assert(FATAL_ERROR TEST j9_opt_name MATCHES "^J9VM_" MESSAGE "expected name starting with J9VM_")
        string(REGEX REPLACE "^J9VM_" "OMR_" omr_opt_name "${j9_opt_name}")
        set(opt_comment "${ARGV1}")
    elseif(ARGC EQUAL 3)
        set(omr_opt_name "${ARGV1}")
        set(opt_comment "${ARGV2}")
    else()
        message(FATAL_ERROR "Too many arguments passed to j9vm_shadowed_option")
    endif()
    option("${j9_opt_name}" "${opt_comment}")
    set("${omr_opt_name}" "${${j9_opt_name}}" CACHE BOOL "" FORCE)
endfunction()

# This replicates the options from j9.flags that I felt were important. Blank lines
# mark where I skipped options which seemed unimportant.
# TODO: some options still need to be ported over
# TODO: at some point we should go through and clean out options which no longer work / make sense

j9vm_shadowed_option(J9VM_GC_ALLOCATION_TAX "Enable allocation tax capabilities")
j9vm_shadowed_option(J9VM_GC_BATCH_CLEAR_TLH "Zero any TLH allocated")
j9vm_shadowed_option(J9VM_GC_COMBINATION_SPEC "Set on specs which implement LIR 16325:  reduce memory footprint by combining multiple sidecars into the same set of libraries.")
j9vm_shadowed_option(J9VM_GC_CONCURRENT_SWEEP "Enable concurrent sweep in Modron")
j9vm_shadowed_option(J9VM_GC_DEBUG_ASSERTS "Specialized GC assertions are used instead of standard trace asserts for GC assertions")

# When enabled, a contiguous block of memory is created for each array in which data surpasses the size of a region. This contiguous block represents the array as
# if the data was stored in a contiguous region of memory. All of the array data is stored in a unique region (not with spine); hence, all arraylets
# become discontiguous whenever this flag is enabled. Since there won’t be any empty arraylet leaves, then arrayoid NULL pointers are no longer required since
# all data is stored in a unique region. It additionaly reduces footprint, mainly for JNI primitive array critical.
j9vm_shadowed_option(J9VM_GC_ENABLE_DOUBLE_MAP OMR_GC_DOUBLE_MAP_ARRAYLETS "Allows LINUX and OSX systems to double map arrays that are stored as arraylets.")
j9vm_shadowed_option(J9VM_GC_LARGE_OBJECT_AREA "Enable large object area (LOA) support")
j9vm_shadowed_option(J9VM_GC_LEAF_BITS "Add leaf bit instance descriptions to classes")
j9vm_shadowed_option(J9VM_GC_MINIMUM_OBJECT_SIZE "Guarantee a minimum size to all objects allocated")
j9vm_shadowed_option(J9VM_GC_MODRON_COMPACTION "Enable compaction in Modron")
j9vm_shadowed_option(J9VM_GC_MODRON_CONCURRENT_MARK "Enable concurrent mark in Modron")
j9vm_shadowed_option(J9VM_GC_MODRON_SCAVENGER "Enable scavenger in Modron")
j9vm_shadowed_option(J9VM_GC_MODRON_STANDARD "Enable Modron standard configuration")
j9vm_shadowed_option(J9VM_GC_NON_ZERO_TLH "Allocator might use special non-zeroed thread local heap for objects")
j9vm_shadowed_option(J9VM_GC_REALTIME  "Realtime Garbage Collection is supported")
j9vm_shadowed_option(J9VM_GC_SEGREGATED_HEAP "Enable Segregated Heap model")
j9vm_shadowed_option(J9VM_GC_THREAD_LOCAL_HEAP "Allocator uses a thread local heap for objects")
j9vm_shadowed_option(J9VM_GC_TLH_PREFETCH_FTA  "Enable tlhPrefetchFTA")
j9vm_shadowed_option(J9VM_GC_VLHGC  "Enables the Very Large Heap Garbage Collector")

option(J9VM_INTERP_AOT_COMPILE_SUPPORT "Controls if the AOT compilation support is included in the VM")
option(J9VM_INTERP_AOT_RUNTIME_SUPPORT "Controls if the AOT runtime support is included in the VM")
option(J9VM_INTERP_ATOMIC_FREE_JNI "Use the new atomic-free JNI support")
option(J9VM_INTERP_ATOMIC_FREE_JNI_USES_FLUSH "Atomic free uses FlushProcessWriteBuffers instead of barriers")
option(J9VM_INTERP_BYTECODE_PREVERIFICATION "Does this VM support 1st pass bytecode verification (able to dynamically generate pre-verify data)")
option(J9VM_INTERP_BYTECODE_VERIFICATION "Does this VM support 2nd pass bytecode verification (pre-verify data in .jxe only)")

option(J9VM_INTERP_DEBUG_SUPPORT "Controls if a debugging support is included in the VM.")
option(J9VM_INTERP_ENABLE_JIT_ON_DESKTOP "TEMPORARY FLAG for arm test environment")

option(J9VM_INTERP_FLOAT_SUPPORT "Determines if float and double data types are supported by the VM.")

option(J9VM_INTERP_GP_HANDLER "Determines if protection faults are caught by the VM and handled cleanly.")
option(J9VM_INTERP_GP_GROWABLE_STACKS "Enables dynamic java stack growing")

option(J9VM_INTERP_JIT_ON_BY_DEFAULT "Turns JIT on by default")
option(J9VM_INTERP_JNI_SUPPORT "Determines if JNI native support is available.")

option(J9VM_INTERP_NATIVE_SUPPORT "Controls if the native translator is included in the VM.")
option(J9VM_INTERP_NEW_HEADER_SHAPE "Temporary flag for experimentation with the object header shape.")
option(J9VM_INTERP_PROFILING_BYTECODES "If enabled, profiling versions of branch and call bytecodes are available. These gather additional info for the JIT for optimal code generation")
option(J9VM_INTERP_ROMABLE_AOT_SUPPORT "ROMable AOT Support for TJ Watson")

option(J9VM_INTERP_TRACING "DEBUGGING FEATURE.  Determines if the interpreter produces debug information at every bytecode.")
option(J9VM_INTERP_TWO_PASS_EXCLUSIVE "Exclusive VM access - Set halt bit in one pass, count responders in another pass")

option(J9VM_INTERP_USE_SPLIT_SIDE_TABLES "Use split side tables for handling constant pool entry shared between multiple invoke bytecodes")
option(J9VM_INTERP_USE_UNSAFE_HELPER "If set, use helper functions in UnsafeAPI to access native memory")
option(J9VM_INTERP_VERBOSE "Determines if verbose reporting is available. (ie. -verbose)")

option(J9VM_MODULE_A2E "Enables compilation of the a2e module.")
option(J9VM_MODULE_ALGORITHM_TEST "Enables compilation of the algorithm_test module.")
option(J9VM_MODULE_AOTRT_COMMON "Enables compilation of the aotrt_common module.")

option(J9VM_MODULE_BCUTIL "Enables compilation of the bcutil module.")
option(J9VM_MODULE_BCVERIFY "Enables compilation of the bcverify module.")

option(J9VM_MODULE_CASSUME "Enables compilation of the cassume module.")
option(J9VM_MODULE_CFDUMPER "Enables compilation of the cfdumper module.")

option(J9VM_MODULE_DBGEXT "Enables compilation of the dbgext module.")

option(J9VM_MODULE_EXELIB "Enables compilation of the exelib module.")

option(J9VM_MODULE_VM "Enables compilation of the vm module.")
option(J9VM_MODULE_WINDBG "Enables compilation of the windbg module.")
option(J9VM_MODULE_ZIP "Enables compilation of the zip module.")
option(J9VM_MODULE_ZLIB "Enables compilation of the zlib module.")

option(J9VM_OPT_CRIU_SUPPORT "Enables support for CRIU Java API's")

j9vm_shadowed_option(J9VM_OPT_CUDA "Add support for CUDA")

option(J9VM_OPT_DYNAMIC_LOAD_SUPPORT "Determines if the dynamic loader is included.")
option(J9VM_OPT_FIPS "Add supports for FIPs")
option(J9VM_OPT_FRAGMENT_RAM_CLASSES "Transitional flag for the GC during the switch to fragmented RAM class allocation")

option(J9VM_OPT_JVMTI "Support for the JVMTI interface")
option(J9VM_OPT_JXE_LOAD_SUPPORT "Controls if main will allow -jxe: and relocate the disk image for you.")

option(J9VM_OPT_METHOD_HANDLE "Enables support for OpenJ9 MethodHandles. J9VM_OPT_OPENJDK_METHODHANDLE should be disabled.")
option(J9VM_OPT_METHOD_HANDLE_COMMON "Enables common dependencies between OpenJ9 and OpenJDK MethodHandles.")
option(J9VM_OPT_MODULE "Turns on module support")
option(J9VM_OPT_MULTI_VM "Decides if multiple VMs can be created in the same address space")
option(J9VM_OPT_OPENJDK_METHODHANDLE "Enables support for OpenJDK MethodHandles. J9VM_OPT_METHOD_HANDLE should be disabled.")

option(J9VM_OPT_VALHALLA_VALUE_TYPES "Enables support for Project Valhalla Value Types")

option(J9VM_OPT_ROM_IMAGE_SUPPORT "Controls if the VM includes basic support for linked rom images")
option(J9VM_OPT_SHARED_CLASSES "Support for class sharing")

option(J9VM_OPT_ZIP_SUPPORT "Controls if the VM includes zip reading and caching support. (implies dynamic loading)")
option(J9VM_OPT_ZLIB_COMPRESSION "Controls if the compression routines in zlib are included.")
option(J9VM_OPT_ZLIB_SUPPORT "Controls if the VM includes the zlib compression library.")

option(J9VM_PORT_RUNTIME_INSTRUMENTATION "Controls whether runtime instrumentation support exists on this platform.")

option(J9VM_RAS_DUMP_AGENTS "Support multiple dump agents, instead of just console dump.")
option(J9VM_RAS_EYE_CATCHERS "Add eyecatcher blocks to key structures")

option(J9VM_USE_RDYNAMIC "Link using the -rdynamic option (Linux only)" ON)

option(J9VM_ZOS_3164_INTEROPERABILITY "Enables support for 64-bit zOS to interoperate with 31-bit JNI native targets.")

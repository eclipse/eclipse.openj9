/*******************************************************************************
 * Copyright (c) 1991, 2021 IBM Corp. and others
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

#ifndef VM_METHODHANDLEKINDS_H_
#define VM_METHODHANDLEKINDS_H_

#define J9_METHOD_HANDLE_KIND_BOUND 0x0
#define J9_METHOD_HANDLE_KIND_GET_FIELD 0x1
#define J9_METHOD_HANDLE_KIND_GET_STATIC_FIELD 0x2
#define J9_METHOD_HANDLE_KIND_PUT_FIELD 0x3
#define J9_METHOD_HANDLE_KIND_PUT_STATIC_FIELD 0x4
#define J9_METHOD_HANDLE_KIND_VIRTUAL 0x5
#define J9_METHOD_HANDLE_KIND_STATIC 0x6
#define J9_METHOD_HANDLE_KIND_SPECIAL 0x7
#define J9_METHOD_HANDLE_KIND_CONSTRUCTOR 0x8
#define J9_METHOD_HANDLE_KIND_INTERFACE 0x9
#define J9_METHOD_HANDLE_KIND_COLLECT 0xA
#define J9_METHOD_HANDLE_KIND_INVOKE_EXACT 0xB
#define J9_METHOD_HANDLE_KIND_INVOKE_GENERIC 0xC
#define J9_METHOD_HANDLE_KIND_ASTYPE 0xD
#define J9_METHOD_HANDLE_KIND_DYNAMIC_INVOKER 0xE
#define J9_METHOD_HANDLE_KIND_FILTER_RETURN 0xF
#define J9_METHOD_HANDLE_KIND_EXPLICITCAST 0x10
#define J9_METHOD_HANDLE_KIND_VARARGS_COLLECTOR 0x11
#define J9_METHOD_HANDLE_KIND_PASSTHROUGH 0x12
#define J9_METHOD_HANDLE_KIND_SPREAD 0x13
#define J9_METHOD_HANDLE_KIND_INSERT 0x14
#define J9_METHOD_HANDLE_KIND_PERMUTE 0x15
#define J9_METHOD_HANDLE_KIND_CONSTANT_OBJECT 0x16
#define J9_METHOD_HANDLE_KIND_CONSTANT_INT 0x17
#define J9_METHOD_HANDLE_KIND_CONSTANT_FLOAT 0x18
#define J9_METHOD_HANDLE_KIND_CONSTANT_LONG 0x19
#define J9_METHOD_HANDLE_KIND_CONSTANT_DOUBLE 0x1A
#define J9_METHOD_HANDLE_KIND_FOLD_HANDLE 0x1B
#define J9_METHOD_HANDLE_KIND_GUARD_WITH_TEST 0x1C
#define J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS 0x1D
#define J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_EXACT 0x1E
#define J9_METHOD_HANDLE_KIND_VARHANDLE_INVOKE_GENERIC 0x1F
#define J9_METHOD_HANDLE_KIND_FILTER_ARGUMENTS_WITH_COMBINER 0x21

#endif /* VM_METHODHANDLEKINDS_H_ */

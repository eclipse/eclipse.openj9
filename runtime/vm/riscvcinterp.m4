dnl Copyright (c) 2021, 2021 IBM Corp. and others
dnl
dnl This program and the accompanying materials are made available under
dnl the terms of the Eclipse Public License 2.0 which accompanies this
dnl distribution and is available at https://www.eclipse.org/legal/epl-2.0/
dnl or the Apache License, Version 2.0 which accompanies this distribution and
dnl is available at https://www.apache.org/licenses/LICENSE-2.0.
dnl
dnl This Source Code may also be made available under the following
dnl Secondary Licenses when the conditions for such availability set
dnl forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
dnl General Public License, version 2 with the GNU Classpath
dnl Exception M(1) and GNU General Public License, version 2 with the
dnl OpenJDK Assembly Exception M(2).
dnl
dnl M(1) https://www.gnu.org/software/classpath/license.html
dnl M(2) http://openjdk.java.net/legal/assembly-exception.html
dnl
dnl SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception

include(riscvhelpers.m4)

    .file "riscvcinterp.s"

DECLARE_PUBLIC(cInterpreter)

START_PROC(c_cInterpreter)
    addi sp, sp, - CINTERP_STACK_SIZE

    dnl Following is esentially
    dnl
    dnl    SAVE_FPLR
    dnl    SAVE_C_NONVOLATILE_REGS
    dnl
    dnl !!! EXCEPT !!! that it is using GPR_SAVE_SLOT()
    dnl and *not* JIT_GPR_SAVE_SLOT()
    dnl
    sd fp,  GPR_SAVE_SLOT(8)
    sd ra,  GPR_SAVE_SLOT(1)

    sd s0,  GPR_SAVE_SLOT(8)
    sd s1,  GPR_SAVE_SLOT(9)

    sd s2,  GPR_SAVE_SLOT(18)
    sd s3,  GPR_SAVE_SLOT(19)
    sd s4,  GPR_SAVE_SLOT(20)
    sd s5,  GPR_SAVE_SLOT(21)
    sd s6,  GPR_SAVE_SLOT(22)
    sd s7,  GPR_SAVE_SLOT(23)
    sd s8,  GPR_SAVE_SLOT(24)
    sd s9,  GPR_SAVE_SLOT(25)
    sd s10, GPR_SAVE_SLOT(26)
    sd s11, GPR_SAVE_SLOT(27)

    mv J9VMTHREAD, a0
    ld  s8, M(J9VMTHREAD,J9TR_VMThread_entryLocalStorage)
    add s9, sp, JIT_GPR_SAVE_OFFSET(0)
    sd  s9, M(s8,J9TR_ELS_jitGlobalStorageBase)
    add s9, sp, JIT_FPR_SAVE_OFFSET(0)
    sd  s9, M(s8,J9TR_ELS_jitFPRegisterStorageBase)
cInterpreter:
    mv  a0, J9VMTHREAD
    ld  s8, M(J9VMTHREAD,J9TR_VMThread_javaVM)
    ld  s8, M(s8,J9TR_JavaVM_bytecodeLoop)
    jalr ra, s8, 0
    li s7, J9TR_bcloop_exit_interpreter
    beq a0, s7, .L_cInterpExit
    RESTORE_PRESERVED_REGS
    RESTORE_FPLR
    SWITCH_TO_JAVA_STACK
    BRANCH_VIA_VMTHREAD(J9TR_VMThread_tempSlot)
.L_cInterpExit:
    dnl Following is esentially
    dnl
    dnl    RESTORE_C_NONVOLATILE_REGS
    dnl    RESTORE_FPLR
    dnl
    dnl !!! EXCEPT !!! that it is using GPR_SAVE_SLOT()
    dnl and *not* JIT_GPR_SAVE_SLOT()
    dnl
    ld s0,  GPR_SAVE_SLOT(8)
    ld s1,  GPR_SAVE_SLOT(9)

    ld s2,  GPR_SAVE_SLOT(18)
    ld s3,  GPR_SAVE_SLOT(19)
    ld s4,  GPR_SAVE_SLOT(20)
    ld s5,  GPR_SAVE_SLOT(21)
    ld s6,  GPR_SAVE_SLOT(22)
    ld s7,  GPR_SAVE_SLOT(23)
    ld s8,  GPR_SAVE_SLOT(24)
    ld s9,  GPR_SAVE_SLOT(25)
    ld s10, GPR_SAVE_SLOT(26)
    ld s11, GPR_SAVE_SLOT(27)

    ld fp, GPR_SAVE_SLOT(8)
    ld ra, GPR_SAVE_SLOT(1)

    addi sp, sp, CINTERP_STACK_SIZE
    ret
END_PROC(c_cInterpreter)

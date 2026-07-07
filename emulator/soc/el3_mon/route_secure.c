#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool el3_mon_route_svc(uc_engine *uc, uint64_t return_address_value,
                       uint16_t immediate)
{
    uc_arm64_cp_reg spsr_el1 = {
        .crn = 4,
        .crm = 0,
        .op0 = 3,
        .op1 = 0,
        .op2 = 0,
    };
    uint64_t pstate = 0;
    uint64_t exception_pstate;
    uint64_t sp_el0 = 0;
    uint64_t sp_el1 = 0;
    uint64_t vbar_el1 = 0;
    uint64_t vector;
    uint64_t esr_el1 = (UINT64_C(0x15) << 26) | immediate;
    uint64_t x0 = 0;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        !secure_os_active)
        return false;

    if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
        (pstate & UINT64_C(0xf)) != 0 ||
        /* At EL0t, the ordinary SP is the live architectural SP_EL0. */
        uc_reg_read(uc, UC_ARM64_REG_SP, &sp_el0) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP_EL1, &sp_el1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_VBAR_EL1, &vbar_el1) != UC_ERR_OK ||
        sp_el1 == 0 || vbar_el1 == 0)
        return false;

    spsr_el1.val = pstate;
    vector = vbar_el1 + UINT64_C(0x400);
    exception_pstate = (pstate & UINT64_C(0xf0000000)) |
                       UINT64_C(0x3c5);
    if (uc_reg_read(uc, UC_ARM64_REG_X0, &x0) != UC_ERR_OK) {
        fprintf(stderr, "[EL3] failed to capture SecureOS SVC x0\n");
        bootchain_fail(uc);
        return true;
    }

    /*
     * Teegris's lower-AArch64 synchronous vector starts with
     *
     *     mrs x0, tpidrro_el0
     *     msr tpidrro_el0, xzr
     *
     * before saving the exception frame.  The native exception-entry path
     * therefore transfers the caller's x0 through TPIDRRO_EL0.  Seed the
     * same handoff for the synthetic SVC; otherwise the vector deliberately
     * replaces the syscall's x0 with zero.
     */
    if (!bootchain_cpu_set_system_register(MRS_ESR_EL1, esr_el1) ||
        !bootchain_cpu_set_system_register(MRS_ELR_EL1,
                                           return_address_value) ||
        !bootchain_cpu_set_system_register(MRS_SPSR_EL1, pstate) ||
        !bootchain_cpu_set_system_register(MRS_SP_EL0, sp_el0) ||
        !bootchain_cpu_set_system_register(MRS_TPIDRRO_EL0,
                                           x0) ||
        uc_reg_write(uc, UC_ARM64_REG_TPIDRRO_EL0,
                     &x0) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_ELR_EL1,
                     &return_address_value) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_ESR_EL1,
                     &esr_el1) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_CP_REG,
                     &spsr_el1) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP_EL0,
                     &sp_el0) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &exception_pstate) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP,
                     &sp_el1) != UC_ERR_OK) {
        fprintf(stderr, "[EL3] failed to enter SecureOS SVC vector\n");
        bootchain_fail(uc);
        return true;
    }

    bootchain_request_resume(vector);
    uc_emu_stop(uc);
    return true;
}

bool el3_mon_route_undefined_instruction(uc_engine *uc,
                                         uint64_t fault_address)
{
    uc_arm64_cp_reg spsr_el1 = {
        .crn = 4,
        .crm = 0,
        .op0 = 3,
        .op1 = 0,
        .op2 = 0,
    };
    uint64_t pstate = 0;
    uint64_t exception_pstate;
    uint64_t sp_el0 = 0;
    uint64_t sp_el1 = 0;
    uint64_t vbar_el1 = 0;
    uint64_t vector;
    uint64_t x0 = 0;
    uint64_t x1 = 0;
    uint64_t regs[31] = {0};
    uint32_t instruction = UINT32_C(0xffffffff);
    /* Unknown reason, 32-bit trapped instruction (EC=0, IL=1). */
    const uint64_t esr_el1 = UINT64_C(1) << 25;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        !secure_os_active)
        return false;

    if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
        (pstate & UINT64_C(0xf)) != 0 ||
        /* At EL0t, the ordinary SP is the live architectural SP_EL0. */
        uc_reg_read(uc, UC_ARM64_REG_SP, &sp_el0) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP_EL1, &sp_el1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_VBAR_EL1, &vbar_el1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X0, &x0) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X1, &x1) != UC_ERR_OK ||
        sp_el1 == 0 || vbar_el1 == 0)
        return false;

    (void)el3_mon_read_secure_os_instruction(
        uc, fault_address, &instruction);
    spsr_el1.val = pstate;
    vector = vbar_el1 + UINT64_C(0x400);
    exception_pstate = (pstate & UINT64_C(0xf0000000)) |
                       UINT64_C(0x3c5);

    /*
     * Mirror the same Teegris lower-EL x0 handoff used for SVC entry.
     * Its vector reloads x0 from TPIDRRO_EL0 before saving the frame.
     */
    if (!bootchain_cpu_set_system_register(MRS_ESR_EL1, esr_el1) ||
        !bootchain_cpu_set_system_register(MRS_ELR_EL1, fault_address) ||
        !bootchain_cpu_set_system_register(MRS_SPSR_EL1, pstate) ||
        !bootchain_cpu_set_system_register(MRS_SP_EL0, sp_el0) ||
        !bootchain_cpu_set_system_register(MRS_TPIDRRO_EL0, x0) ||
        uc_reg_write(uc, UC_ARM64_REG_TPIDRRO_EL0, &x0) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_ELR_EL1,
                     &fault_address) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_ESR_EL1,
                     &esr_el1) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_CP_REG,
                     &spsr_el1) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP_EL0,
                     &sp_el0) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &exception_pstate) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP,
                     &sp_el1) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to enter SecureOS UDEF vector\n");
        bootchain_fail(uc);
        return true;
    }

    printf("[SecureOS UDEF] fault=0x%016" PRIx64
           " insn=0x%08" PRIx32
           " ESR_EL1=0x%016" PRIx64
           " vector=0x%016" PRIx64
           " sp_el0=0x%016" PRIx64
           " sp_el1=0x%016" PRIx64
           " x0=0x%016" PRIx64
           " x1=0x%016" PRIx64 "\n",
           fault_address, instruction, esr_el1, vector,
           sp_el0, sp_el1, x0, x1);
    for (size_t reg = 0; reg < 31; reg++)
        (void)read_secure_os_x_register(uc, (unsigned int)reg,
                                        &regs[reg]);
    printf("[SecureOS UDEF registers]"
           " x2=0x%016" PRIx64 " x3=0x%016" PRIx64
           " x4=0x%016" PRIx64 " x5=0x%016" PRIx64
           " x19=0x%016" PRIx64 " x20=0x%016" PRIx64
           " x21=0x%016" PRIx64 " x22=0x%016" PRIx64
           " x23=0x%016" PRIx64 " x24=0x%016" PRIx64
           " x25=0x%016" PRIx64 " x26=0x%016" PRIx64
           " x27=0x%016" PRIx64 " x28=0x%016" PRIx64
           " x29=0x%016" PRIx64 " x30=0x%016" PRIx64 "\n",
           regs[2], regs[3], regs[4], regs[5],
           regs[19], regs[20], regs[21], regs[22],
           regs[23], regs[24], regs[25], regs[26],
           regs[27], regs[28], regs[29], regs[30]);
    fflush(stdout);

    bootchain_request_resume(vector);
    uc_emu_stop(uc);
    return true;
}



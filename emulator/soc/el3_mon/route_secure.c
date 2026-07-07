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


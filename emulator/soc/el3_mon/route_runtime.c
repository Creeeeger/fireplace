#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool el3_mon_route_runtime_smc(uc_engine *uc,
                               uint64_t return_address_value)
{
    const uint32_t mrs_esr_el3 = UINT32_C(0xd53e521e);
    const uint32_t mrs_elr_el3 = UINT32_C(0xd53e4021);
    const uint32_t mrs_spsr_el3 = UINT32_C(0xd53e4000);
    const uint32_t mrs_scr_el3 = UINT32_C(0xd53e1102);
    const uint64_t vector = EL3_VECTOR_ENTRY;
    uint64_t function_id = 0;
    uint64_t x1 = 0;
    uint64_t x2 = 0;
    uint64_t x3 = 0;
    uint64_t x4 = 0;
    uint64_t pstate = 0;
    uint64_t runtime_sp = 0;
    uint64_t runtime_sp_el0 = 0;
    uint64_t el3_pstate = EL3_EXCEPTION_PSTATE;
    uint64_t scr = 0;
    uint64_t el3_sp;

    /*
     * Existing LDFW runtime-SMC path.
     */
    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        !servicing_lk ||
        !ldfw_shadow_contains(return_address_value) ||
        ldfw_context_base == 0)
        return false;

    el3_sp = ldfw_context_base + EL3_LDFW_CONTEXT_SP_OFFSET;
    if (uc_reg_read(uc, UC_ARM64_REG_X0, &function_id) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X1, &x1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X2, &x2) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X3, &x3) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X4, &x4) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
        !read_u64(uc,
                  ldfw_context_base + EL3_LDFW_CONTEXT_SCR_OFFSET,
                  &scr)) {
        fprintf(stderr,
                "[EL3] failed to read LDFW runtime SMC context\n");
        bootchain_fail(uc);
        return true;
    }

    if (!sync_ldfw_va_shadow(uc)) {
        fprintf(stderr,
                "[EL3] failed to synchronize LDFW VA shadow\n");
        bootchain_fail(uc);
        return true;
    }

    if (!bootchain_cpu_set_system_register(
            mrs_esr_el3, UINT64_C(0x17) << 26) ||
        !bootchain_cpu_set_system_register(
            mrs_elr_el3, return_address_value) ||
        !bootchain_cpu_set_system_register(mrs_spsr_el3, pstate) ||
        !bootchain_cpu_set_system_register(mrs_scr_el3, scr) ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &el3_pstate) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP, &el3_sp) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PC, &vector) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to route LDFW runtime SMC\n");
        bootchain_fail(uc);
        return true;
    }

    bootchain_request_resume(vector);
    uc_emu_stop(uc);
    return true;
}



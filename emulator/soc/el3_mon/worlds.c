#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool read_u64(uc_engine *uc, uint64_t address, uint64_t *value)
{
    return uc_mem_read(uc, address, value, sizeof(*value)) == UC_ERR_OK;
}

int lk_gpr_id(size_t index)
{
    if (index < 29)
        return UC_ARM64_REG_X0 + (int)index;
    if (index == 29)
        return UC_ARM64_REG_X29;
    return UC_ARM64_REG_X30;
}

bool capture_lk_smc_frame(uc_engine *uc,
                                 uint64_t return_address_value)
{
    struct lk_smc_frame frame = {
        .return_address = return_address_value,
    };

    if (!is_lk_return_target(return_address_value) ||
        uc_reg_read(uc, UC_ARM64_REG_SP, &frame.sp) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_PSTATE, &frame.pstate) != UC_ERR_OK)
        return false;

    for (size_t i = 0; i < ARRAY_SIZE(frame.regs); i++) {
        if (uc_reg_read(uc, lk_gpr_id(i), &frame.regs[i]) != UC_ERR_OK)
            return false;
    }

    frame.valid = true;
    lk_saved_smc_frame = frame;
    return true;
}

bool restore_lk_smc_frame(uc_engine *uc, uint64_t x0)
{
    uint64_t regs[sizeof(lk_saved_smc_frame.regs) /
                  sizeof(lk_saved_smc_frame.regs[0])];

    if (!lk_saved_smc_frame.valid ||
        !is_lk_return_target(lk_saved_smc_frame.return_address))
        return false;

    memcpy(regs, lk_saved_smc_frame.regs, sizeof(regs));
    regs[0] = x0;

    for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
        if (uc_reg_write(uc, lk_gpr_id(i), &regs[i]) != UC_ERR_OK)
            return false;
    }

    return uc_reg_write(uc, UC_ARM64_REG_SP, &lk_saved_smc_frame.sp) ==
               UC_ERR_OK &&
           uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                        &lk_saved_smc_frame.pstate) == UC_ERR_OK;
}

void reset_lk_smc_frame(void)
{
    memset(&lk_saved_smc_frame, 0, sizeof(lk_saved_smc_frame));
}

bool capture_normal_world_el1_context(void)
{
    if (normal_world_el1_context.valid)
        return true;

    if (!bootchain_cpu_get_system_register(
            MRS_SCTLR_EL1, &normal_world_el1_context.sctlr) ||
        !bootchain_cpu_get_system_register(
            MRS_TTBR0_EL1, &normal_world_el1_context.ttbr0) ||
        !bootchain_cpu_get_system_register(
            MRS_TTBR1_EL1, &normal_world_el1_context.ttbr1) ||
        !bootchain_cpu_get_system_register(
            MRS_TCR_EL1, &normal_world_el1_context.tcr))
        return false;

    normal_world_el1_context.valid = true;
    return true;
}

bool restore_normal_world_el1_context(void)
{
    if (!normal_world_el1_context.valid)
        return false;

    return bootchain_cpu_set_system_register(
               MRS_SCTLR_EL1, normal_world_el1_context.sctlr) &&
           bootchain_cpu_set_system_register(
               MRS_TTBR0_EL1, normal_world_el1_context.ttbr0) &&
           bootchain_cpu_set_system_register(
               MRS_TTBR1_EL1, normal_world_el1_context.ttbr1) &&
           bootchain_cpu_set_system_register(
               MRS_TCR_EL1, normal_world_el1_context.tcr);
}

/*
 * Finish a SecureOS -> normal-world switch using the LK frame captured
 * when LK originally entered EL3.  The real monitor saves the SecureOS
 * context before ERET, but its normal-world slot is not initialized in
 * this emulator, so executing that ERET would branch to address zero.
 */
bool complete_secure_os_return_to_lk(uc_engine *uc)
{
    uint64_t target;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        !servicing_lk ||
        !secure_os_runtime_smc_pending ||
        !secure_os_runtime_returns_to_lk ||
        !lk_saved_smc_frame.valid ||
        !is_lk_return_target(lk_saved_smc_frame.return_address))
        return false;

    target = lk_saved_smc_frame.return_address;

    if (ldfw_shadow_active && !deactivate_ldfw_va_shadow(uc)) {
        fprintf(stderr,
                "[EL3] failed to deactivate LDFW VA shadow while "
                "returning SecureOS to LK\n");
        return false;
    }

    if (!restore_lk_smc_frame(uc, secure_os_runtime_lk_x0) ||
        !restore_normal_world_el1_context()) {
        fprintf(stderr,
                "[EL3] failed to restore LK frame/context after SecureOS "
                "normal-world switch\n");
        return false;
    }

    ldfw_runtime_setup_active = false;
    reset_ldfw_monitor_frame();
    secure_os_active = false;
    secure_os_monitor_sp = 0;
    secure_os_runtime_sp = 0;
    secure_os_runtime_smc_pending = false;
    secure_os_runtime_returns_to_lk = false;
    secure_os_runtime_lk_x0 = 0;

    if (!bootchain_transition(uc,
                              BOOTCHAIN_STAGE_EL3,
                              BOOTCHAIN_STAGE_LK))
        return false;

    servicing_lk = false;
    reset_lk_smc_frame();
    bootchain_request_resume(target);
    uc_emu_stop(uc);
    return true;
}

bool complete_harx_hvc_return_to_lk(uc_engine *uc,
                                           uint64_t eret_address)
{
    uint64_t target = 0;
    uint64_t target_pstate = 0;
    uint64_t lk_sp;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        !harx_hvc_pending || harx_runtime_smc_pending ||
        !is_harx_return_target(eret_address) ||
        !lk_saved_smc_frame.valid)
        return false;

    lk_sp = lk_saved_smc_frame.sp;
    if (!bootchain_cpu_get_system_register(MRS_ELR_EL2, &target) ||
        !bootchain_cpu_get_system_register(MRS_SPSR_EL2,
                                           &target_pstate) ||
        !is_lk_return_target(target) ||
        target != lk_saved_smc_frame.return_address ||
        /*
         * SP_EL1 belongs to the suspended SecureOS context in this
         * emulator model.  LK's active stack is carried in ordinary SP;
         * replacing the banked value with lk_sp corrupts the next
         * SecureOS entry.
         */
        uc_reg_write(uc, UC_ARM64_REG_SP_EL1,
                     &harx_preserved_sp_el1) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP, &lk_sp) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &target_pstate) != UC_ERR_OK ||
        !bootchain_transition(uc, BOOTCHAIN_STAGE_EL3,
                              BOOTCHAIN_STAGE_LK)) {
        fprintf(stderr,
                "[H-Arx] failed to complete EL2 HVC return "
                "at 0x%08" PRIx64
                " target=0x%08" PRIx64
                " spsr=0x%" PRIx64 "\n",
                eret_address, target, target_pstate);
        return false;
    }

    harx_active = false;
    harx_hvc_pending = false;
    harx_runtime_smc_pending = false;
    harx_runtime_sp = 0;
    harx_preserved_sp_el1 = 0;
    reset_lk_smc_frame();

    bootchain_request_resume(target);
    uc_emu_stop(uc);
    return true;
}



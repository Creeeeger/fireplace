#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool el3_mon_bootrom_service_active(void)
{
    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3)
        return false;

    /*
     * BootROM service entry points live in the same low virtual-address
     * range later owned by SecureOS EL0 tasks and the H-Arx payload.  Their
     * permanent Unicorn hooks must not claim an address merely because an
     * LDFW shadow is inactive.  In particular, BootROM's ECDSA entry at
     * 0x376c overlaps handler code and returns directly to LR.
     */
    if (secure_os_active || harx_active)
        return false;

    return !ldfw_shadow_active ||
           (servicing_lk && active_smc == EL3_SMC_SECURE_OS);
}

bool el3_mon_route_smc(uc_engine *uc, uint64_t return_address_value)
{
    secure_os_active = false;
    secure_os_monitor_sp = 0;
    secure_os_runtime_sp = 0;
    secure_os_runtime_smc_pending = false;
    secure_os_runtime_returns_to_lk = false;
    secure_os_runtime_lk_x0 = 0;
    harx_active = false;
    harx_runtime_smc_pending = false;
    harx_runtime_sp = 0;
    
    const uint32_t mrs_esr_el3 = UINT32_C(0xd53e521e);
    const uint32_t mrs_elr_el3 = UINT32_C(0xd53e4021);
    const uint32_t mrs_spsr_el3 = UINT32_C(0xd53e4000);
    const uint32_t mrs_scr_el3 = UINT32_C(0xd53e1102);
    uint64_t exception_return = return_address_value;
    uint64_t x1 = 0;
    uint64_t x2 = 0;
    uint64_t el3_pstate = EL3_EXCEPTION_PSTATE;

    if (bootchain_stage() != BOOTCHAIN_STAGE_LK || monitor_sp == 0)
        return false;

    if (uc_reg_read(uc, UC_ARM64_REG_X0, &active_smc) != UC_ERR_OK) {
        fprintf(stderr, "[EL3] failed to read LK SMC context\n");
        bootchain_fail(uc);
        return true;
    }

    uc_reg_read(uc, UC_ARM64_REG_X1, &x1);
    uc_reg_read(uc, UC_ARM64_REG_X2, &x2);

    if (active_smc == EL3_SMC_SECURE_OS &&
        !capture_normal_world_el1_context()) {
        fprintf(stderr,
                "[EL3] failed to capture normal-world EL1 context "
                "before SecureOS\n");
        bootchain_fail(uc);
        return true;
    }


    if (active_smc == EL3_SMC_CUSTOM_REGISTER) {
        secure_os_low_image_ready = false;
    } else if (active_smc == EL3_SMC_HARX_INIT) {
        harx_initialized = false;
        harx_hvc_pending = false;
        harx_monitor_sp = 0;
        harx_plugin_base = 0;
        harx_plugin_size = 0;
        harx_image_base = x1;
        harx_image_size = x2;
    }

    if (active_smc == EL3_SMC_LDFW) {
        ldfw_context_count = 0;
        memset(ldfw_contexts, 0, sizeof(ldfw_contexts));
        ldfw_context_base = 0;
        reset_ldfw_va_shadow();
    } else if (ldfw_shadow_active && !sync_ldfw_va_shadow(uc)) {
        fprintf(stderr, "[EL3] failed to synchronize LDFW VA shadow\n");
        bootchain_fail(uc);
        return true;
    }

    if (!capture_lk_smc_frame(uc, return_address_value)) {
        fprintf(stderr, "[EL3] failed to capture LK SMC frame\n");
        bootchain_fail(uc);
        return true;
    }
    lower_sp = lk_saved_smc_frame.sp;

    if (!bootchain_cpu_set_system_register(mrs_esr_el3,
                           UINT64_C(0x17) << 26) ||
        !bootchain_cpu_set_system_register(mrs_elr_el3,
                           exception_return) ||
        !bootchain_cpu_set_system_register(mrs_spsr_el3,
                           lk_saved_smc_frame.pstate) ||
        !bootchain_cpu_set_system_register(mrs_scr_el3,
                           UINT64_C(0x431)) ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE, &el3_pstate) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP, &monitor_sp) != UC_ERR_OK ||
        !bootchain_transition(uc, BOOTCHAIN_STAGE_LK,
                  BOOTCHAIN_STAGE_EL3)) {
        fprintf(stderr, "[EL3] failed to enter LK SMC vector\n");
        bootchain_fail(uc);
        return true;
    }

    harx_invalid_memory_reported = false;
    ldfw_runtime_setup_active = false;
    reset_ldfw_monitor_frame();
    secure_os_active = false;
    secure_os_monitor_sp = 0;
    servicing_lk = true;

    bootchain_request_resume(EL3_VECTOR_ENTRY);
    uc_emu_stop(uc);
    return true;
}

bool el3_mon_route_hvc(uc_engine *uc, uint64_t return_address_value,
                       uint16_t immediate)
{
    uint64_t command = 0;
    uint64_t x1 = 0;
    uint64_t x2 = 0;
    uint64_t pstate = 0;
    uint64_t sp_el2 = 0;
    uint64_t vector;
    uint64_t exception_pstate;
    uint64_t esr_el2 = (UINT64_C(0x16) << 26) | immediate;

    if (bootchain_stage() != BOOTCHAIN_STAGE_LK ||
        !harx_initialized || harx_hvc_pending ||
        harx_image_base == 0 || harx_monitor_sp == 0 ||
        !is_lk_return_target(return_address_value))
        return false;

    if (uc_reg_read(uc, UC_ARM64_REG_X0, &command) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X1, &x1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_X2, &x2) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP_EL2, &sp_el2) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP_EL1,
                    &harx_preserved_sp_el1) != UC_ERR_OK ||
        sp_el2 == 0 ||
        harx_preserved_sp_el1 == 0 ||
        !capture_lk_smc_frame(uc, return_address_value)) {
        fprintf(stderr, "[H-Arx] failed to capture LK HVC context\n");
        bootchain_fail(uc);
        return true;
    }

    if (command == HARX_HVC_REGISTER_PLUGIN) {
        harx_plugin_base = x1;
        harx_plugin_size = x2;
    }

    vector = harx_image_base + HARX_VECTOR_OFFSET +
             HARX_LOWER_AARCH64_SYNC_OFFSET;
    exception_pstate = (pstate & UINT64_C(0xf0000000)) |
                       HARX_EL2_EXCEPTION_PSTATE;

    if (!bootchain_cpu_set_system_register(MRS_ESR_EL2, esr_el2) ||
        !bootchain_cpu_set_system_register(MRS_ELR_EL2,
                                           return_address_value) ||
        !bootchain_cpu_set_system_register(MRS_SPSR_EL2, pstate) ||
        uc_reg_write(uc, UC_ARM64_REG_ESR_EL2,
                     &esr_el2) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_ELR_EL2,
                     &return_address_value) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &exception_pstate) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP, &sp_el2) != UC_ERR_OK ||
        !bootchain_transition(uc, BOOTCHAIN_STAGE_LK,
                              BOOTCHAIN_STAGE_EL3)) {
        fprintf(stderr, "[H-Arx] failed to enter EL2 HVC vector\n");
        bootchain_fail(uc);
        return true;
    }

    harx_hvc_pending = true;
    harx_active = true;
    harx_runtime_smc_pending = false;
    harx_runtime_sp = 0;

    bootchain_request_resume(vector);
    uc_emu_stop(uc);
    return true;
}

bool el3_mon_secure_os_active(void)
{
    return bootchain_stage() == BOOTCHAIN_STAGE_EL3 &&
           secure_os_active;
}



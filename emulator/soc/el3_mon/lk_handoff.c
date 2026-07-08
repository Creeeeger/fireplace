#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

void lk_handoff_cb(uc_engine *uc,
                          uint64_t address,
                          uint32_t size,
                          void *user_data)
{
    uint64_t stack_pointer;

    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        uc_reg_read(uc, UC_ARM64_REG_SP,
                    &stack_pointer) != UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                     &return_spsr) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] invalid exception-return context\n");
        bootchain_fail(uc);
        return;
    }

    /*
     * Initial SecureOS entry selected by firmware.
     *
     * This handles both the authentication/load SMC and a later
     * START_USERSPACE request.
     */
    if (servicing_lk && is_secure_os_return_target(return_address)) {
        uint64_t secure_payload_sp = 0;

        if (active_smc == EL3_SMC_SECURE_OS) {
            secure_os_shadow_page_count = 0;
            secure_os_shadow_write_sequence = 0;
            saved_secure_os_sp_el0 = 0;
            saved_secure_os_sp_el0_valid = false;
            memset(secure_os_shadow_pages, 0,
                   sizeof(secure_os_shadow_pages));
        }

        if (active_smc != EL3_SMC_SECURE_OS) {
            uc_err sp_err = uc_reg_read(uc, UC_ARM64_REG_SP_EL1,
                                        &secure_payload_sp);

            if (sp_err != UC_ERR_OK || secure_payload_sp == 0)
                secure_payload_sp = restored_secure_os_sp_el1;
            if (secure_payload_sp == 0)
                secure_payload_sp = saved_secure_os_sp;

            if (secure_payload_sp == 0 ||
                uc_reg_write(uc, UC_ARM64_REG_SP,
                             &secure_payload_sp) != UC_ERR_OK ||
                !restore_secure_os_sp_el0(uc)) {
                fprintf(stderr,
                        "[EL3] failed to restore SecureOS SP_EL1 for "
                        "LK SMC 0x%" PRIx64 "\n",
                        active_smc);
                bootchain_fail(uc);
                return;
            }
            /*
             * SecureOS recycles high virtual addresses between handler
             * loads.  Reconcile each shadow with the PTE selected by the
             * context EL3 just restored before SecureOS can read it.
             */
            if (!refresh_secure_os_va_shadow(uc)) {
                bootchain_fail(uc);
                return;
            }
        }

        secure_os_active = true;
        secure_os_monitor_sp = stack_pointer;
        secure_os_runtime_sp = 0;
        secure_os_runtime_smc_pending = false;
        secure_os_runtime_returns_to_lk = false;
        secure_os_runtime_lk_x0 = 0;

        if (active_smc == EL3_SMC_SECURE_OS)
            saved_secure_os_sp = 0;

        bootchain_request_resume(return_address);
        uc_emu_stop(uc);
        return;
    }
    /*
     * Any remaining return must be to LK.
     */
    if (!is_lk_return_target(return_address)) {
        fprintf(stderr,
                "[EL3] invalid exception-return target "
                "0x%" PRIx64
                " spsr=0x%" PRIx64
                " active_smc=0x%" PRIx64
                " secure_pending=%u\n",
                return_address,
                return_spsr,
                active_smc,
                secure_os_runtime_smc_pending ? 1U : 0U);
        bootchain_fail(uc);
        return;
    }

    if (servicing_lk) {
        uint64_t x0 = 0;

        if (uc_reg_read(uc, UC_ARM64_REG_X0,
                        &x0) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to read LK SMC return value\n");
            bootchain_fail(uc);
            return;
        }

        if (active_smc == EL3_SMC_HARX_INIT) {
            harx_initialized = x0 == 0;
            if (!harx_initialized)
                harx_monitor_sp = 0;
        }

        if (ldfw_shadow_active &&
            !deactivate_ldfw_va_shadow(uc)) {
            fprintf(stderr,
                    "[EL3] failed to deactivate LDFW VA shadow\n");
            bootchain_fail(uc);
            return;
        }
        ldfw_runtime_setup_active = false;
        reset_ldfw_monitor_frame();
        secure_os_active = false;
        secure_os_monitor_sp = 0;
        secure_os_runtime_sp = 0;
        secure_os_runtime_smc_pending = false;
        secure_os_runtime_returns_to_lk = false;
        secure_os_runtime_lk_x0 = 0;
        harx_active = false;
        harx_hvc_pending = false;
        harx_runtime_smc_pending = false;
        harx_runtime_sp = 0;

        if (uc_reg_write(uc, UC_ARM64_REG_SP,
                         &lower_sp) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to restore LK stack\n");
            bootchain_fail(uc);
            return;
        }
    } else {
        monitor_sp = stack_pointer;
        printf("[EL3] LK handoff reached at 0x%" PRIx64 "\n",
               return_address);
    }

    if (!bootchain_transition(uc,
                              BOOTCHAIN_STAGE_EL3,
                              BOOTCHAIN_STAGE_LK))
        return;

    servicing_lk = false;
    reset_lk_smc_frame();
    bootchain_request_resume(return_address);
    uc_emu_stop(uc);
}



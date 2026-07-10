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
     * SecureOS runtime SMC.
     *
     * SecureOS is still represented as BOOTCHAIN_STAGE_EL3 because the
     * emulator does not model a separate secure-payload stage.
     */
    if (bootchain_stage() == BOOTCHAIN_STAGE_EL3 &&
        secure_os_active &&
        is_secure_os_return_target(return_address_value) &&
        secure_os_monitor_sp != 0) {
        if (uc_reg_read(uc, UC_ARM64_REG_X0, &function_id) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to inspect SecureOS runtime SMC\n");
            bootchain_fail(uc);
            return true;
        }

        if (!sync_secure_os_va_shadow(uc)) {
            bootchain_fail(uc);
            return true;
        }

        if (uc_reg_read(uc, UC_ARM64_REG_X1, &x1) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to inspect SecureOS runtime result\n");
            bootchain_fail(uc);
            return true;
        }

        if (uc_reg_read(uc, UC_ARM64_REG_X0, &function_id) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_X1, &x1) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_X2, &x2) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_X3, &x3) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_X4, &x4) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
            uc_reg_read(uc, UC_ARM64_REG_SP, &runtime_sp) != UC_ERR_OK ||
            !bootchain_cpu_get_system_register(MRS_SP_EL0,
                                               &runtime_sp_el0)) {
            fprintf(stderr,
                    "[EL3] failed to capture SecureOS runtime SMC\n");
            bootchain_fail(uc);
            return true;
        }

        /*
         * 0xb2000301 is the SecureOS -> normal-world switch.  Its call
         * site is deliberately non-returning; x1 is the value delivered
         * to the suspended normal-world caller.  Keep routing the SMC
         * through the real monitor so that it can save SecureOS state,
         * but substitute the LK frame at the final ERET because the
         * emulator does not seed the monitor's normal-world context.
         */
        secure_os_runtime_returns_to_lk =
            function_id == EL3_SMC_SECURE_OS_RETURN_NORMAL;
        secure_os_runtime_lk_x0 = x1;

        /*
         * Preserve the SecureOS stack before replacing SP with EL3's
         * monitor stack. lk_handoff_cb() restores this for ordinary
         * SecureOS runtime SMC returns.
         */
        secure_os_runtime_sp = runtime_sp;
        saved_secure_os_sp_el0 = runtime_sp_el0;
        saved_secure_os_sp_el0_valid = true;
        secure_os_runtime_smc_pending = true;
        if (function_id == EL3_SMC_SECURE_OS_RETURN_NORMAL)
            saved_secure_os_sp = runtime_sp;
        else if (function_id == EL3_SMC_SECURE_OS_REGISTER_SECOND)
            saved_secure_os_sp = runtime_sp;

        /*
         * An architectural exception taken from EL1h leaves the active
         * stack in the SP_EL1 bank.  Unicorn does not perform that bank
         * transfer when the emulator changes PSTATE with uc_reg_write().
         * Without this explicit commit, the next LK -> SecureOS entry can
         * reuse the last EL0 task's kernel stack and overwrite its
         * suspended frames.
         */
        if ((pstate & UINT64_C(0xf)) == UINT64_C(5) &&
            uc_reg_write(uc, UC_ARM64_REG_SP_EL1,
                         &runtime_sp) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to bank SecureOS runtime SP_EL1\n");
            secure_os_runtime_sp = 0;
            secure_os_runtime_smc_pending = false;
            secure_os_runtime_returns_to_lk = false;
            secure_os_runtime_lk_x0 = 0;
            bootchain_fail(uc);
            return true;
        }

        if (!bootchain_cpu_set_system_register(
                mrs_esr_el3, UINT64_C(0x17) << 26) ||
            !bootchain_cpu_set_system_register(
                mrs_elr_el3, return_address_value) ||
            !bootchain_cpu_set_system_register(mrs_spsr_el3, pstate) ||
            !bootchain_cpu_set_system_register(
                mrs_scr_el3, EL3_SECURE_PAYLOAD_SCR) ||
            uc_reg_write(uc, UC_ARM64_REG_PSTATE,
                         &el3_pstate) != UC_ERR_OK ||
            uc_reg_write(uc, UC_ARM64_REG_SP,
                         &secure_os_monitor_sp) != UC_ERR_OK ||
            uc_reg_write(uc, UC_ARM64_REG_PC, &vector) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to route SecureOS runtime SMC\n");

            secure_os_runtime_sp = 0;
            secure_os_runtime_smc_pending = false;
            secure_os_runtime_returns_to_lk = false;
            secure_os_runtime_lk_x0 = 0;
            bootchain_fail(uc);
            return true;
        }

        /*
         * EL3 is executing now. This prevents the SecureOS instruction
         * callback from treating EL3 instructions as SecureOS code.
         */
        secure_os_active = false;

        bootchain_request_resume(vector);
        uc_emu_stop(uc);
        return true;
    }
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



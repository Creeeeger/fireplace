#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool restore_secure_os_sp_el0(uc_engine *uc)
{
    if (!saved_secure_os_sp_el0_valid)
        return true;

    if (!bootchain_cpu_set_system_register(MRS_SP_EL0,
                                           saved_secure_os_sp_el0) ||
        uc_reg_write(uc, UC_ARM64_REG_SP_EL0,
                     &saved_secure_os_sp_el0) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to restore banked SecureOS SP_EL0=0x%016"
                PRIx64 "\n",
                saved_secure_os_sp_el0);
        return false;
    }

    return true;
}


void ldfw_decrypt_success_cb(uc_engine *uc, uint64_t address,
                    uint32_t size, void *user_data)
{
    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || !servicing_lk ||
        active_smc != EL3_SMC_LDFW)
        return;

    ldfw_runtime_setup_active = true;
    ldfw_context_base = 0;
    reset_ldfw_va_shadow();
    reset_ldfw_monitor_frame();
}

void ldfw_entry_cb(uc_engine *uc, uint64_t address, uint32_t size,
                          void *user_data)
{
    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || !servicing_lk ||
        active_smc != EL3_SMC_LDFW || !ldfw_runtime_setup_active)
        return;

    if (uc_reg_read(uc, UC_ARM64_REG_X0, &ldfw_context_base) == UC_ERR_OK)
        reset_ldfw_monitor_frame();
}

void ldfw_context_switch_cb(uc_engine *uc, uint64_t address,
                                   uint32_t size, void *user_data)
{
    uint64_t context_base = 0;
    uint64_t context_size = 0;
    bool known_context;

    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || !servicing_lk)
        return;

    if (uc_reg_read(uc, UC_ARM64_REG_X0, &context_base) != UC_ERR_OK)
        return;

    known_context = find_ldfw_context(context_base, &context_size);
    if ((active_smc != EL3_SMC_LDFW || !ldfw_runtime_setup_active) &&
        !known_context)
        return;

    capture_ldfw_monitor_frame(uc, context_base);
    for (size_t i = 0; i < ldfw_context_count; i++) {
        if (ldfw_contexts[i].base == context_base) {
            ldfw_contexts[i].monitor_frame = ldfw_saved_monitor_frame;
            break;
        }
    }

    if (known_context) {
        if (!prepare_ldfw_va_shadow(uc, context_base, context_size)) {
            fprintf(stderr,
                    "[EL3] failed to switch LDFW context 0x%08" PRIx64
                    "\n", context_base);
            bootchain_fail(uc);
            return;
        }
        ldfw_context_base = context_base;
    }
}


void el3_return_cb(uc_engine *uc, uint64_t address, uint32_t size,
                          void *user_data)
{
    uint32_t insn = UINT32_C(0xffffffff);

    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || secure_os_active)
        return;
    if (!harx_hvc_pending &&
        !(secure_os_runtime_smc_pending &&
          secure_os_runtime_returns_to_lk))
        return;

    if (!el3_mon_read_secure_os_instruction(uc, address, &insn) &&
        uc_mem_read(uc, address, &insn, sizeof(insn)) != UC_ERR_OK)
        return;

    if (address == EL3_RESTORE_SP_EL1)
        (void)uc_reg_read(uc, UC_ARM64_REG_X10,
                          &restored_secure_os_sp_el1);

    if (insn == AARCH64_ERET &&
        harx_hvc_pending &&
        !harx_runtime_smc_pending &&
        is_harx_return_target(address)) {
        if (!complete_harx_hvc_return_to_lk(uc, address))
            bootchain_fail(uc);
        return;
    }

    if (insn == AARCH64_ERET &&
        secure_os_runtime_smc_pending &&
        secure_os_runtime_returns_to_lk &&
        !complete_secure_os_return_to_lk(uc))
        bootchain_fail(uc);
}


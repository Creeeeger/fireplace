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

bool read_secure_os_x_register(uc_engine *uc, unsigned int index,
                                      uint64_t *value)
{
    uc_arm64_reg reg;

    if (index < 29)
        reg = (uc_arm64_reg)(UC_ARM64_REG_X0 + index);
    else if (index == 29)
        reg = UC_ARM64_REG_X29;
    else if (index == 30)
        reg = UC_ARM64_REG_X30;
    else {
        *value = 0;
        return true;
    }

    return uc_reg_read(uc, reg, value) == UC_ERR_OK;
}

bool complete_secure_os_el1_eret(uc_engine *uc, uint64_t address)
{
    uc_arm64_cp_reg spsr_el1 = {
        .crn = 4,
        .crm = 0,
        .op0 = 3,
        .op1 = 0,
        .op2 = 0,
    };
    uint64_t current_pstate = 0;
    uint64_t current_sp = 0;
    uint64_t target_pstate;
    uint64_t target_pc = 0;
    uint64_t sp_el0 = 0;
    uint64_t sp_el1 = 0;
    uint64_t target_sp;
    uint64_t target_mode;

    if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &current_pstate) !=
            UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP, &current_sp) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_CP_REG, &spsr_el1) != UC_ERR_OK ||
        uc_reg_read(uc, UC_ARM64_REG_SP_EL1, &sp_el1) != UC_ERR_OK) {
        fprintf(stderr,
                "[SecureOS ERET] failed to read EL1 return state at "
                "0x%016" PRIx64 "\n",
                address);
        return false;
    }

    if (!bootchain_cpu_get_system_register(MRS_SP_EL0, &sp_el0) ||
        !bootchain_cpu_get_system_register(MRS_ELR_EL1, &target_pc) ||
        !bootchain_cpu_get_system_register(MRS_SPSR_EL1,
                                           &spsr_el1.val)) {
        fprintf(stderr,
                "[SecureOS ERET] failed to read virtual EL1 return "
                "registers at 0x%016" PRIx64 "\n",
                address);
        return false;
    }

    if (((current_pstate >> 2) & UINT64_C(3)) != UINT64_C(1)) {
        fprintf(stderr,
                "[SecureOS ERET] invalid source EL at 0x%016" PRIx64
                " pstate=0x%016" PRIx64 "\n",
                address, current_pstate);
        return false;
    }

    target_pstate = spsr_el1.val;
    target_mode = target_pstate & UINT64_C(0xf);

    /*
     * Unicorn does not commit the currently selected SP into SP_EL1 when
     * PSTATE is changed through uc_reg_write().  Save the outgoing EL1h
     * bank explicitly so that the next EL0 SVC has a valid exception
     * stack.  At EL1h the ordinary SP is the architectural SP_EL1 value.
     */
    if ((current_pstate & UINT64_C(0xf)) == UINT64_C(5)) {
        sp_el1 = current_sp;
        if (uc_reg_write(uc, UC_ARM64_REG_SP_EL1, &sp_el1) != UC_ERR_OK) {
            fprintf(stderr,
                    "[SecureOS ERET] failed to preserve SP_EL1 at "
                    "0x%016" PRIx64 "\n",
                    address);
            return false;
        }
    }

    switch (target_mode) {
    case 0: /* EL0t */
    case 4: /* EL1t */
        target_sp = sp_el0;
        break;
    case 5: /* EL1h */
        target_sp = sp_el1;
        break;
    default:
        fprintf(stderr,
                "[SecureOS ERET] invalid target mode at 0x%016" PRIx64
                " ELR_EL1=0x%016" PRIx64
                " SPSR_EL1=0x%016" PRIx64 "\n",
                address, target_pc, target_pstate);
        return false;
    }

    if (target_pc == 0 || target_sp == 0 ||
        uc_reg_write(uc, UC_ARM64_REG_PSTATE, &target_pstate) !=
            UC_ERR_OK ||
        uc_reg_write(uc, UC_ARM64_REG_SP, &target_sp) != UC_ERR_OK) {
        fprintf(stderr,
                "[SecureOS ERET] failed to install EL1 return state at "
                "0x%016" PRIx64
                " ELR_EL1=0x%016" PRIx64
                " SPSR_EL1=0x%016" PRIx64
                " target_SP=0x%016" PRIx64 "\n",
                address, target_pc, target_pstate, target_sp);
        return false;
    }

    bootchain_request_resume(target_pc);
    uc_emu_stop(uc);
    return true;
}



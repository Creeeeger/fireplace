#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool el3_mon_handle_invalid_memory(uc_engine *uc, uint64_t address)
{
    uint8_t page[EL3_LDFW_PAGE_SIZE];
    uint64_t context_base;
    uint64_t context_size;
    uint64_t va;
    uint64_t pa;
    uint64_t pc = 0;
    uint64_t lr = 0;
    uint64_t x0 = 0;
    uint64_t x2 = 0;
    uc_err err;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3)
        return false;

    if (active_smc == EL3_SMC_HARX_INIT && servicing_lk &&
        !harx_invalid_memory_reported) {
        uint64_t regs[16] = {0};
        uint64_t sp = 0;
        uint32_t config_words[8] = {0};
        uint32_t insn = UINT32_C(0xffffffff);

        for (size_t reg = 0; reg < 16; reg++)
            (void)uc_reg_read(uc, UC_ARM64_REG_X0 + (int)reg,
                              &regs[reg]);
        (void)uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
        (void)uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
        (void)uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
        (void)uc_mem_read(uc, pc, &insn, sizeof(insn));
        (void)uc_mem_read(uc, UINT64_C(0x2c858), config_words,
                          sizeof(config_words));
        printf("[EL3 H-Arx invalid memory]"
               " address=0x%016" PRIx64
               " pc=0x%016" PRIx64
               " insn=0x%08" PRIx32
               " lr=0x%016" PRIx64
               " sp=0x%016" PRIx64 "\n",
               address, pc, insn, lr, sp);
        printf("[EL3 H-Arx invalid memory] PKA config"
               " [0x2c858]=0x%08" PRIx32
               ",0x%08" PRIx32 ",0x%08" PRIx32 ",0x%08" PRIx32
               ",0x%08" PRIx32 ",0x%08" PRIx32 ",0x%08" PRIx32
               ",0x%08" PRIx32 "\n",
               config_words[0], config_words[1], config_words[2],
               config_words[3], config_words[4], config_words[5],
               config_words[6], config_words[7]);
        for (size_t reg = 0; reg < 16; reg++)
            printf("[EL3 H-Arx invalid memory] x%zu=0x%016" PRIx64
                   "%c", reg, regs[reg], reg % 4 == 3 ? '\n' : ' ');
        fflush(stdout);
        harx_invalid_memory_reported = true;
    }

    /*
     * SecureOS EL0/user mappings live under TTBR0_EL1.  Populate the
     * faulting lower VA from its real PA instead of letting the generic
     * SoC handler create a zero-filled page.
     */
    if (secure_os_active &&
        address < UINT64_C(0xffff000000000000) &&
        page_in_secure_os_low_va(uc, address, NULL))
        return true;

    /*
     * SecureOS has enabled its EL1 MMU and entered a TTBR1 mapping.
     *
     * This check must occur before the LDFW low-VA filter because
     * SecureOS virtual addresses are in the high canonical range.
     */
    if (secure_os_active &&
        address >= UINT64_C(0xffff000000000000)) {
        if (!page_in_secure_os_va(uc, address)) {
            uint64_t sp = 0;

            uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
            uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
            uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
            uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
            uc_reg_read(uc, UC_ARM64_REG_X2, &x2);
            fprintf(stderr,
                    "[EL3] unresolved SecureOS virtual address "
                    "0x%016" PRIx64
                    " PC=0x%016" PRIx64
                    " LR=0x%016" PRIx64
                    " SP=0x%016" PRIx64
                    " x0=0x%016" PRIx64
                    " x2=0x%016" PRIx64 "\n",
                    address, pc, lr, sp, x0, x2);
            bootchain_fail(uc);
        }

        return true;
    }

    /*
     * Everything below this point is exclusively for the LDFW
     * low-virtual-address shadow.
     */
    if (!servicing_lk || address >= EL3_LDFW_LOW_VA_LIMIT)
        return false;

    if (!ldfw_shadow_active) {
        context_base = ldfw_context_base != 0 ?
                       ldfw_context_base : ldfw_shadow_context;

        if (!find_ldfw_context(context_base, &context_size)) {
            if (ldfw_context_count != 0) {
                fprintf(stderr,
                        "[EL3] failed to identify LDFW context\n");
                bootchain_fail(uc);
                return true;
            }

            return false;
        }

        if (!prepare_ldfw_va_shadow(uc, context_base, context_size)) {
            fprintf(stderr,
                    "[EL3] failed to reactivate LDFW context 0x%08"
                    PRIx64 "\n",
                    context_base);
            bootchain_fail(uc);
            return true;
        }
    }

    va = address & ~(EL3_LDFW_PAGE_SIZE - 1);

    if (!translate_ldfw_context_va_internal(uc,
                                            ldfw_shadow_context,
                                            va,
                                            &pa,
                                            false)) {
        uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
        uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
        uc_reg_read(uc, UC_ARM64_REG_X2, &x2);

        fprintf(stderr,
                "[EL3] unmapped LDFW VA 0x%08" PRIx64
                " in context 0x%08" PRIx64
                " pc=0x%08" PRIx64
                " lr=0x%08" PRIx64
                " x0=0x%08" PRIx64
                " x2=0x%" PRIx64 "\n",
                address,
                ldfw_shadow_context,
                pc,
                lr,
                x0,
                x2);

        bootchain_fail(uc);
        return true;
    }

    err = uc_mem_read(uc, pa, page, sizeof(page));

    if (err == UC_ERR_OK)
        err = uc_mem_map(uc, va, EL3_LDFW_PAGE_SIZE, UC_PROT_ALL);

    if (err == UC_ERR_OK)
        err = uc_mem_write(uc, va, page, sizeof(page));

    if (err != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to page in LDFW VA 0x%08" PRIx64
                ": %s\n",
                va,
                uc_strerror(err));
        bootchain_fail(uc);
        return true;
    }

    ldfw_shadow_mapped[va / EL3_LDFW_PAGE_SIZE] = 1;

    if (ldfw_shadow_size < va + EL3_LDFW_PAGE_SIZE)
        ldfw_shadow_size = va + EL3_LDFW_PAGE_SIZE;

    for (size_t i = 0; i < ldfw_context_count; i++) {
        if (ldfw_contexts[i].base == ldfw_shadow_context &&
            ldfw_contexts[i].size < ldfw_shadow_size) {
            ldfw_contexts[i].size = ldfw_shadow_size;
            break;
        }
    }

    if (uc_ctl_flush_tb(uc) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to flush LDFW translation cache\n");
        bootchain_fail(uc);
    }

    return true;
}



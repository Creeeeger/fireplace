#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool translate_secure_os_va(uc_engine *uc, uint64_t va,
                                   uint64_t *pa)
{
    uint64_t sctlr;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t low_mask;
    unsigned int t1sz;
    unsigned int va_bits;
    unsigned int level_count;
    unsigned int start_level;
    unsigned int tg1;

    if (!bootchain_cpu_get_system_register(MRS_SCTLR_EL1, &sctlr) ||
        !bootchain_cpu_get_system_register(MRS_TTBR1_EL1, &ttbr1) ||
        !bootchain_cpu_get_system_register(MRS_TCR_EL1, &tcr))
        return false;

    /* SCTLR_EL1.M */
    if ((sctlr & UINT64_C(1)) == 0)
        return false;

    /*
     * TCR_EL1.TG1:
     *   10 = 4 KiB
     *   01 = 16 KiB
     *   11 = 64 KiB
     */
    tg1 = (unsigned int)((tcr >> 30) & UINT64_C(3));
    if (tg1 != 2)
        return false;

    t1sz = (unsigned int)((tcr >> 16) & UINT64_C(0x3f));
    va_bits = 64U - t1sz;

    if (va_bits <= 12 || va_bits > 48)
        return false;

    /*
     * Verify that this is a canonical upper address belonging to
     * TTBR1_EL1.
     */
    low_mask = (UINT64_C(1) << va_bits) - 1;
    if ((va & ~low_mask) != ~low_mask)
        return false;

    level_count = (va_bits - 12U + 8U) / 9U;
    if (level_count == 0 || level_count > 4)
        return false;

    start_level = 4U - level_count;

    return walk_aarch64_4k_table(uc, ttbr1, va, start_level, pa);
}

bool translate_secure_os_low_va(uc_engine *uc, uint64_t va,
                                       uint64_t *pa)
{
    uint64_t sctlr;
    uint64_t ttbr0;
    uint64_t tcr;
    uint64_t low_mask;
    unsigned int t0sz;
    unsigned int va_bits;
    unsigned int level_count;
    unsigned int start_level;
    unsigned int tg0;

    if (!bootchain_cpu_get_system_register(MRS_SCTLR_EL1, &sctlr) ||
        !bootchain_cpu_get_system_register(MRS_TTBR0_EL1, &ttbr0) ||
        !bootchain_cpu_get_system_register(MRS_TCR_EL1, &tcr))
        return false;

    if ((sctlr & UINT64_C(1)) == 0)
        return false;

    /* TCR_EL1.TG0 == 00 selects 4 KiB pages. */
    tg0 = (unsigned int)((tcr >> 14) & UINT64_C(3));
    if (tg0 != 0)
        return false;

    t0sz = (unsigned int)(tcr & UINT64_C(0x3f));
    va_bits = 64U - t0sz;
    if (va_bits <= 12 || va_bits > 48)
        return false;

    low_mask = (UINT64_C(1) << va_bits) - 1;
    if ((va & ~low_mask) != 0)
        return false;

    level_count = (va_bits - 12U + 8U) / 9U;
    if (level_count == 0 || level_count > 4)
        return false;

    start_level = 4U - level_count;
    return walk_aarch64_4k_table(uc, ttbr0, va, start_level, pa);
}

bool el3_mon_read_secure_os_instruction(uc_engine *uc, uint64_t address,
                                        uint32_t *instruction)
{
    uint64_t pa = 0;

    if (!instruction || !secure_os_active ||
        bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        address >= UINT64_C(0xffff000000000000))
        return false;

    if (!translate_secure_os_low_va(uc, address, &pa))
        return false;

    if (uc_mem_read(uc, pa, instruction,
                    sizeof(*instruction)) != UC_ERR_OK)
        return false;

    return true;
}


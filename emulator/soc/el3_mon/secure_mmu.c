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

bool remember_secure_os_shadow_page(uint64_t va, uint64_t pa)
{
    for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
        if (secure_os_shadow_pages[i].va == va) {
            secure_os_shadow_pages[i].pa = pa;
            secure_os_shadow_pages[i].write_sequence = 0;
            return true;
        }
    }

    if (secure_os_shadow_page_count ==
        SECURE_OS_SHADOW_PAGE_CAPACITY)
        return false;

    secure_os_shadow_pages[secure_os_shadow_page_count++] =
        (struct secure_os_shadow_page) {
            .va = va,
            .pa = pa,
            .write_sequence = 0,
        };

    return true;
}

bool translate_secure_os_va_coherent(uc_engine *uc,
                                             uint64_t va,
                                             uint64_t *pa)
{
    /*
     * Fast path: the physical page tables are already current.
     */
    if (translate_secure_os_va(uc, va, pa))
        return true;

    /*
     * SecureOS may have edited its page tables through one of the
     * high-VA shadow aliases. Copy those writes back to the physical
     * pages before walking the tables again.

    fprintf(stderr,
            "[EL3] SecureOS translation miss for VA=0x%016" PRIx64
            "; synchronizing VA shadows and retrying\n",
            va);
     */
    if (!sync_secure_os_va_shadow(uc))
        return false;

    return translate_secure_os_va(uc, va, pa);
}

bool page_in_secure_os_low_va(uc_engine *uc, uint64_t address,
                                     bool *populated)
{
    uint8_t page[SECURE_OS_VA_PAGE_SIZE];
    uint64_t va = address & ~(SECURE_OS_VA_PAGE_SIZE - 1);
    uint64_t pa = 0;
    uint64_t pa_page;
    uint64_t registered_pa = 0;
    uc_err err;

    if (populated)
        *populated = false;

    if (!translate_secure_os_low_va(uc, va, &pa))
        return false;

    pa_page = pa & ~(SECURE_OS_VA_PAGE_SIZE - 1);
    /*
     * A handler context switch can reuse the same low VA with another
     * TTBR0 physical page.  The write hooks keep aliases of the same
     * physical page coherent, so a matching VA-to-PA registration is a
     * sufficient fast path and avoids comparing two 4 KiB pages for every
     * executed handler instruction.
     */
    if (find_secure_os_shadow_page(va, &registered_pa) &&
        registered_pa == pa_page)
        return true;

    if (uc_mem_read(uc, pa_page, page, sizeof(page)) != UC_ERR_OK) {
        fprintf(stderr,
                "[EL3] failed to read SecureOS TTBR0 page "
                "VA=0x%016" PRIx64 " PA=0x%016" PRIx64 "\n",
                va, pa_page);
        return false;
    }

    /* Existing identity-mapped boot regions can already own this VA. */
    err = uc_mem_map(uc, va, SECURE_OS_VA_PAGE_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK && err != UC_ERR_MAP) {
        fprintf(stderr,
                "[EL3] failed to map SecureOS TTBR0 page "
                "VA=0x%016" PRIx64 ": %s\n",
                va, uc_strerror(err));
        return false;
    }

    secure_os_alias_sync_active = true;
    err = uc_mem_write(uc, va, page, sizeof(page));
    secure_os_alias_sync_active = false;
    if (err != UC_ERR_OK ||
        !remember_secure_os_shadow_page(va, pa_page)) {
        fprintf(stderr,
                "[EL3] failed to populate SecureOS TTBR0 page "
                "VA=0x%016" PRIx64 " PA=0x%016" PRIx64 "\n",
                va, pa_page);
        return false;
    }

    if (populated)
        *populated = true;
    return true;
}


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool walk_aarch64_4k_table(uc_engine *uc, uint64_t table,
                  uint64_t va, unsigned int start_level,
                  uint64_t *pa)
{
    uint64_t base = table & EL3_AARCH64_DESC_ADDR_MASK;

    for (unsigned int level = start_level; level <= 3; level++) {
        unsigned int shift = 12 + 9 * (3 - level);
        uint64_t index = (va >> shift) & UINT64_C(0x1ff);
        uint64_t desc;

        if (!read_u64(uc, base + index * sizeof(desc), &desc) ||
            (desc & UINT64_C(1)) == 0)
            return false;

        if (level == 3) {
            if ((desc & UINT64_C(3)) != UINT64_C(3))
                return false;
            *pa = (desc & EL3_AARCH64_DESC_ADDR_MASK) |
                  (va & UINT64_C(0xfff));
            return true;
        }

        if ((desc & UINT64_C(3)) == UINT64_C(3)) {
            base = desc & EL3_AARCH64_DESC_ADDR_MASK;
            continue;
        }

        if ((desc & UINT64_C(3)) == UINT64_C(1)) {
            uint64_t block_mask = (UINT64_C(1) << shift) - 1;
            *pa = (desc & EL3_AARCH64_DESC_ADDR_MASK &
                   ~block_mask) | (va & block_mask);
            return true;
        }

        return false;
    }

    return false;
}

bool translate_ldfw_context_va_internal(uc_engine *uc,
                                               uint64_t context_base,
                                               uint64_t va, uint64_t *pa,
                                               bool report_failure)
{
    uint64_t ttbr0;
    uint64_t tcr;
    unsigned int va_bits;
    unsigned int level_count;
    unsigned int start_level;

    if (va >= EL3_LDFW_LOW_VA_LIMIT || context_base == 0)
        return false;

    if (!read_u64(uc, context_base + EL3_LDFW_CONTEXT_TTBR0_OFFSET,
              &ttbr0) ||
        !read_u64(uc, context_base + EL3_LDFW_CONTEXT_TCR_OFFSET, &tcr)) {
        if (report_failure) {
            fprintf(stderr,
                "[EL3] failed to read LDFW translation context 0x%08"
                PRIx64 "\n",
                context_base);
        }
        return false;
    }

    va_bits = 64U - (unsigned int)(tcr & UINT64_C(0x3f));
    if (va_bits <= 12 || va_bits > 48)
        return false;

    level_count = (va_bits - 12U + 8U) / 9U;
    if (level_count == 0 || level_count > 4)
        return false;
    start_level = 4U - level_count;

    if (walk_aarch64_4k_table(uc, ttbr0, va, start_level, pa) &&
        is_ldfw_return_target(*pa))
        return true;

    if (report_failure) {
        fprintf(stderr,
            "[EL3] failed to translate LDFW VA 0x%" PRIx64
            " via TTBR0=0x%08" PRIx64 " TCR=0x%" PRIx64
            " L%u context=0x%08" PRIx64 "\n",
            va, ttbr0, tcr, start_level, context_base);
    }
    return false;
}

bool prepare_ldfw_va_shadow(uc_engine *uc, uint64_t context_base,
                                   uint64_t size)
{
    uint8_t page[EL3_LDFW_PAGE_SIZE];
    uint64_t aligned_size;

    if (context_base == 0 || size == 0 ||
        size > EL3_LDFW_SHADOW_MAX_SIZE)
        return false;

    /*
     * A lower-firmware SMC synchronizes the active VA shadow back to its
     * physical pages before the monitor handles the request.  The monitor
     * may then update shared buffers through their physical aliases before
     * re-entering the same lower-firmware context.  Do not copy the stale
     * VA shadow over those updates here; the page-population loop below
     * refreshes that context from physical memory.  A different context
     * still needs the old shadow committed before it is replaced.
     */
    if (ldfw_shadow_active && ldfw_shadow_context != context_base &&
        !sync_ldfw_va_shadow(uc))
        return false;

    aligned_size = (size + EL3_LDFW_PAGE_SIZE - 1) &
                   ~(EL3_LDFW_PAGE_SIZE - 1);
    if (aligned_size > ldfw_normal_memory_size) {
        uint8_t *expanded = realloc(ldfw_normal_memory,
                                    (size_t)aligned_size);

        if (!expanded) {
            fprintf(stderr,
                    "[EL3] failed to allocate LDFW normal-memory backup "
                    "size=0x%" PRIx64 "\n", aligned_size);
            return false;
        }
        memset(expanded + ldfw_normal_memory_size, 0,
               (size_t)(aligned_size - ldfw_normal_memory_size));
        ldfw_normal_memory = expanded;
        ldfw_normal_memory_size = aligned_size;
    }

    memset(ldfw_shadow_mapped, 0, sizeof(ldfw_shadow_mapped));
    for (uint64_t va = 0; va < aligned_size;
         va += EL3_LDFW_PAGE_SIZE) {
        uint64_t pa;
        uc_err err;

        if (!translate_ldfw_context_va_internal(uc, context_base, va, &pa,
                                                false))
            continue;

        if (!ldfw_normal_memory_saved[va / EL3_LDFW_PAGE_SIZE]) {
            err = uc_mem_read(uc, va, ldfw_normal_memory + va,
                              EL3_LDFW_PAGE_SIZE);
            if (err == UC_ERR_OK) {
                ldfw_normal_memory_mapped[
                    va / EL3_LDFW_PAGE_SIZE] = 1;
            } else if (err != UC_ERR_READ_UNMAPPED) {
                fprintf(stderr,
                        "[EL3] failed to preserve normal memory below "
                        "LDFW VA=0x%08" PRIx64 ": %s\n",
                        va, uc_strerror(err));
                return false;
            }
            ldfw_normal_memory_saved[va / EL3_LDFW_PAGE_SIZE] = 1;
        }

        err = uc_mem_read(uc, pa, page, sizeof(page));
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to read LDFW shadow page "
                    "va=0x%08" PRIx64 " pa=0x%08" PRIx64 ": %s\n",
                    va, pa, uc_strerror(err));
            return false;
        }
        ldfw_shadow_mapped[va / EL3_LDFW_PAGE_SIZE] = 1;

        err = uc_mem_write(uc, va, page, sizeof(page));
        if (err == UC_ERR_WRITE_UNMAPPED) {
            err = uc_mem_map(uc, va, EL3_LDFW_PAGE_SIZE, UC_PROT_ALL);
            if (err == UC_ERR_OK)
                err = uc_mem_write(uc, va, page, sizeof(page));
        }
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to map LDFW shadow page "
                    "va=0x%08" PRIx64 ": %s\n",
                    va, uc_strerror(err));
            return false;
        }
    }

    if (uc_ctl_flush_tb(uc) != UC_ERR_OK) {
        fprintf(stderr, "[EL3] failed to flush LDFW translation cache\n");
        return false;
    }

    if (!remember_ldfw_context(context_base, aligned_size)) {
        fprintf(stderr, "[EL3] too many LDFW translation contexts\n");
        return false;
    }

    ldfw_shadow_active = true;
    ldfw_shadow_context = context_base;
    ldfw_shadow_size = aligned_size;
    return true;
}

bool sync_ldfw_va_shadow(uc_engine *uc)
{
    uint8_t page[EL3_LDFW_PAGE_SIZE];

    if (!ldfw_shadow_active)
        return true;

    for (uint64_t va = 0; va < ldfw_shadow_size;
         va += EL3_LDFW_PAGE_SIZE) {
        uint64_t pa;

        if (!ldfw_shadow_mapped[va / EL3_LDFW_PAGE_SIZE])
            continue;

        if (uc_mem_read(uc, va, page, sizeof(page)) != UC_ERR_OK ||
            !translate_ldfw_context_va_internal(uc, ldfw_shadow_context,
                                                va, &pa, false) ||
            uc_mem_write(uc, pa, page, sizeof(page)) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to sync LDFW shadow page "
                    "va=0x%08" PRIx64 " context=0x%08" PRIx64 "\n",
                    va, ldfw_shadow_context);
            return false;
        }
    }

    return true;
}


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


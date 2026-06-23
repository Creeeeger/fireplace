
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bootchain/cpu_internal.h"

#define PAR_EL1_TRANSLATION_FAULT_BASE 4U
#define PAR_EL1_ACCESS_FLAG_FAULT_BASE 8U
#define PAR_EL1_PERMISSION_FAULT_BASE 12U
#define KERNEL_ALIAS_PAGE_CAPACITY 16384U
#define KERNEL_PENDING_WRITE_CAPACITY 16U

struct kernel_alias_page {
    uint64_t va;
    uint64_t pa;
};

struct kernel_pending_write {
    uint64_t address;
    uint32_t size;
};

static struct kernel_alias_page kernel_alias_pages[KERNEL_ALIAS_PAGE_CAPACITY];
static size_t kernel_alias_page_count;
static struct kernel_pending_write
    kernel_pending_writes[KERNEL_PENDING_WRITE_CAPACITY];
static size_t kernel_pending_write_count;
static bool kernel_alias_sync_active;

static bool walk_el1_4k_table(uc_engine *uc, uint64_t table, uint64_t va,
                              unsigned int start_level, bool write,
                              uint64_t *pa, unsigned int *fault_status)
{
    uint64_t base = table & AARCH64_DESC_ADDR_MASK;

    for (unsigned int level = start_level; level <= 3; level++) {
        unsigned int shift = 12 + 9 * (3 - level);
        uint64_t index = (va >> shift) & UINT64_C(0x1ff);
        uint64_t descriptor;

        if (uc_mem_read(uc, base + index * sizeof(descriptor),
                        &descriptor, sizeof(descriptor)) != UC_ERR_OK ||
            (descriptor & UINT64_C(1)) == 0) {
            *fault_status = PAR_EL1_TRANSLATION_FAULT_BASE + level;
            return false;
        }

        if (level < 3 && (descriptor & UINT64_C(3)) == UINT64_C(3)) {
            base = descriptor & AARCH64_DESC_ADDR_MASK;
            continue;
        }

        if ((level == 3 &&
             (descriptor & UINT64_C(3)) != UINT64_C(3)) ||
            (level < 3 &&
             (descriptor & UINT64_C(3)) != UINT64_C(1))) {
            *fault_status = PAR_EL1_TRANSLATION_FAULT_BASE + level;
            return false;
        }

        /* AF is descriptor bit 10 for both block and page mappings. */
        if ((descriptor & (UINT64_C(1) << 10)) == 0) {
            *fault_status = PAR_EL1_ACCESS_FLAG_FAULT_BASE + level;
            return false;
        }

        /* AP[2] makes the EL1 mapping read-only. */
        if (write && (descriptor & (UINT64_C(1) << 7)) != 0) {
            *fault_status = PAR_EL1_PERMISSION_FAULT_BASE + level;
            return false;
        }

        if (level == 3) {
            *pa = (descriptor & AARCH64_DESC_ADDR_MASK) |
                  (va & UINT64_C(0xfff));
        } else {
            uint64_t block_mask = (UINT64_C(1) << shift) - 1;

            *pa = (descriptor & AARCH64_DESC_ADDR_MASK & ~block_mask) |
                  (va & block_mask);
        }
        return true;
    }

    *fault_status = PAR_EL1_TRANSLATION_FAULT_BASE + 3;
    return false;
}

bool cpu_mmu_translate_el1(uc_engine *uc, uint64_t va, bool write,
                                 uint64_t *pa, unsigned int *fault_status)
{
    uint64_t sctlr;
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t low_mask;
    unsigned int va_bits;
    unsigned int level_count;
    unsigned int start_level;
    unsigned int tg;
    bool upper;

    if (!bootchain_cpu_get_system_register(MRS_SCTLR_EL1, &sctlr) ||
        !bootchain_cpu_get_system_register(MRS_TTBR0_EL1, &ttbr0) ||
        !bootchain_cpu_get_system_register(MRS_TTBR1_EL1, &ttbr1) ||
        !bootchain_cpu_get_system_register(MRS_TCR_EL1, &tcr))
        return false;

    /* With stage 1 disabled, EL1 virtual addresses are flat IPAs. */
    if ((sctlr & UINT64_C(1)) == 0) {
        *pa = (va & AARCH64_DESC_ADDR_MASK) | (va & UINT64_C(0xfff));
        return true;
    }

    upper = (va & (UINT64_C(1) << 63)) != 0;
    if (upper) {
        unsigned int t1sz = (unsigned int)((tcr >> 16) & UINT64_C(0x3f));

        va_bits = 64U - t1sz;
        tg = (unsigned int)((tcr >> 30) & UINT64_C(3));
        /* TG1=10 selects 4 KiB; EPD1 disables table walks. */
        if (tg != 2 || (tcr & (UINT64_C(1) << 23)) != 0)
            goto address_size_fault;
        if (va_bits <= 12 || va_bits > 48)
            goto address_size_fault;
        low_mask = (UINT64_C(1) << va_bits) - 1;
        if ((va & ~low_mask) != ~low_mask)
            goto address_size_fault;
    } else {
        unsigned int t0sz = (unsigned int)(tcr & UINT64_C(0x3f));

        va_bits = 64U - t0sz;
        tg = (unsigned int)((tcr >> 14) & UINT64_C(3));
        /* TG0=00 selects 4 KiB; EPD0 disables table walks. */
        if (tg != 0 || (tcr & (UINT64_C(1) << 7)) != 0)
            goto address_size_fault;
        if (va_bits <= 12 || va_bits > 48)
            goto address_size_fault;
        low_mask = (UINT64_C(1) << va_bits) - 1;
        if ((va & ~low_mask) != 0)
            goto address_size_fault;
    }

    level_count = (va_bits - 12U + 8U) / 9U;
    if (level_count == 0 || level_count > 4)
        goto address_size_fault;
    start_level = 4U - level_count;

    return walk_el1_4k_table(uc, upper ? ttbr1 : ttbr0, va, start_level, write,
                             pa, fault_status);

address_size_fault:
    *fault_status = 0;
    return false;
}


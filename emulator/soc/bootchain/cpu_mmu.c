
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

static struct kernel_alias_page *find_kernel_alias(uint64_t va)
{
    uint64_t page = va & ~UINT64_C(0xfff);

    for (size_t i = 0; i < kernel_alias_page_count; i++)
        if (kernel_alias_pages[i].va == page)
            return &kernel_alias_pages[i];
    return NULL;
}

static bool remember_kernel_alias(uint64_t va, uint64_t pa)
{
    struct kernel_alias_page *alias = find_kernel_alias(va);

    va &= ~UINT64_C(0xfff);
    pa &= ~UINT64_C(0xfff);
    if (alias) {
        alias->pa = pa;
        return true;
    }
    if (kernel_alias_page_count == KERNEL_ALIAS_PAGE_CAPACITY)
        return false;
    kernel_alias_pages[kernel_alias_page_count++] =
        (struct kernel_alias_page) { .va = va, .pa = pa };
    return true;
}

bool cpu_mmu_invalidate_aliases(uc_engine *uc)
{
    for (size_t i = 0; i < kernel_alias_page_count; i++) {
        uint64_t va = kernel_alias_pages[i].va;
        uc_err err = uc_mem_unmap(uc, va, 0x1000);

        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[Kernel MMU] failed to invalidate shadow page"
                    " VA=0x%016" PRIx64 ": %s\n",
                    va, uc_strerror(err));
            return false;
        }
    }

    kernel_alias_page_count = 0;
    return uc_ctl_flush_tb(uc) == UC_ERR_OK;
}

static bool publish_kernel_alias_bytes(uc_engine *uc, uint64_t address,
                                       const uint8_t *bytes, size_t size)
{
    kernel_alias_sync_active = true;
    while (size != 0) {
        struct kernel_alias_page *source = find_kernel_alias(address);
        uint64_t offset = address & UINT64_C(0xfff);
        uint64_t pa;
        uint64_t pa_page;
        size_t chunk = 0x1000 - (size_t)offset;
        uc_err err;

        if (source) {
            pa = source->pa + offset;
        } else if ((address & (UINT64_C(1) << 63)) != 0) {
            unsigned int fault_status;

            /*
             * Unicorn invokes the ordinary write hook before its invalid-
             * memory hook.  The first store to a valid high kernel VA can
             * therefore arrive before that VA page has been shadow-mapped
             * and recorded in kernel_alias_pages.  Resolve the live EL1
             * mapping here and publish only to its physical backing.  The
             * following invalid-memory callback will create the VA shadow
             * and remember the alias before Unicorn retries the store.
             */
            if (!cpu_mmu_translate_el1(uc, address, true, &pa,
                                      &fault_status)) {
                fprintf(stderr,
                        "[Kernel MMU] failed to translate first write"
                        " VA=0x%016" PRIx64 " FST=0x%x\n",
                        address, fault_status);
                goto fail;
            }
        } else {
            pa = address;
        }
        pa_page = pa & ~UINT64_C(0xfff);

        if (chunk > size)
            chunk = size;
        err = uc_mem_write(uc, pa, bytes, chunk);
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[Kernel MMU] backing write failed"
                    " VA=0x%016" PRIx64
                    " PA=0x%016" PRIx64
                    " size=%zu: %s\n",
                    address, pa, chunk, uc_strerror(err));
            goto fail;
        }
        for (size_t i = 0; i < kernel_alias_page_count; i++) {
            const struct kernel_alias_page *alias = &kernel_alias_pages[i];

            if (alias->pa != pa_page)
                continue;
            err = uc_mem_write(uc, alias->va + offset, bytes, chunk);
            if (err != UC_ERR_OK) {
                fprintf(stderr,
                        "[Kernel MMU] alias write failed"
                        " VA=0x%016" PRIx64
                        " PA=0x%016" PRIx64
                        " size=%zu: %s\n",
                        alias->va + offset, pa, chunk,
                        uc_strerror(err));
                goto fail;
            }
        }
        address += chunk;
        bytes += chunk;
        size -= chunk;
    }
    kernel_alias_sync_active = false;
    return true;

fail:
    kernel_alias_sync_active = false;
    return false;
}

bool cpu_mmu_flush_pending_writes(uc_engine *uc)
{
    uint8_t bytes[64];

    for (size_t i = 0; i < kernel_pending_write_count; i++) {
        const struct kernel_pending_write *pending =
            &kernel_pending_writes[i];

        if (pending->size > sizeof(bytes) ||
            uc_mem_read(uc, pending->address, bytes, pending->size) !=
                UC_ERR_OK ||
            !publish_kernel_alias_bytes(uc, pending->address, bytes,
                                        pending->size))
            return false;
    }
    kernel_pending_write_count = 0;
    return true;
}

void cpu_mmu_write_cb(uc_engine *uc, uc_mem_type type,
                            uint64_t address, int size, int64_t value,
                            void *user_data)
{
    uint8_t bytes[sizeof(value)];

    (void)type;
    (void)user_data;
    if (bootchain_stage() != BOOTCHAIN_STAGE_KERNEL ||
        kernel_alias_sync_active || size <= 0)
        return;

    if ((size_t)size <= sizeof(value)) {
        memcpy(bytes, &value, (size_t)size);
        if (!publish_kernel_alias_bytes(uc, address, bytes, (size_t)size))
            bootchain_fail(uc);
        return;
    }

    if ((size_t)size > 64 ||
        kernel_pending_write_count == KERNEL_PENDING_WRITE_CAPACITY) {
        fprintf(stderr, "[Kernel MMU] unsupported wide store size=%d\n",
                size);
        bootchain_fail(uc);
        return;
    }
    kernel_pending_writes[kernel_pending_write_count++] =
        (struct kernel_pending_write) {
            .address = address,
            .size = (uint32_t)size,
        };
}

bool bootchain_cpu_handle_invalid_memory(uc_engine *uc, uint64_t address)
{
    uint8_t page[0x1000];
    uint64_t virtual_page = address & ~UINT64_C(0xfff);
    uint64_t physical_address;
    uint64_t physical_page;
    unsigned int fault_status;
    uc_err err;

    if (bootchain_stage() != BOOTCHAIN_STAGE_KERNEL)
        return false;
    if (!cpu_mmu_translate_el1(uc, virtual_page, false,
                              &physical_address, &fault_status))
        return false;

    physical_page = physical_address & ~UINT64_C(0xfff);
    if (physical_page == virtual_page ||
        uc_mem_read(uc, physical_page, page, sizeof(page)) != UC_ERR_OK)
        return false;

    err = uc_mem_map(uc, virtual_page, sizeof(page), UC_PROT_ALL);
    if (err != UC_ERR_OK)
        return false;
    err = uc_mem_write(uc, virtual_page, page, sizeof(page));
    if (err != UC_ERR_OK ||
        !remember_kernel_alias(virtual_page, physical_page)) {
        (void)uc_mem_unmap(uc, virtual_page, sizeof(page));
        return false;
    }

    return true;
}

void cpu_mmu_reset(void)
{
	kernel_alias_page_count = 0;
	kernel_pending_write_count = 0;
	kernel_alias_sync_active = false;
}

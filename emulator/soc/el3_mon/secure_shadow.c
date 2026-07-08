#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

bool find_secure_os_shadow_page(uint64_t va, uint64_t *pa)
{
    for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
        if (secure_os_shadow_pages[i].va == va) {
            if (pa)
                *pa = secure_os_shadow_pages[i].pa;
            return true;
        }
    }

    return false;
}

/*
 * Unicorn cannot map multiple guest VAs onto one backing page, so the
 * SecureOS high-VA mappings are represented by copied shadow pages.  Keep
 * those copies coherent at every emulated store, as the real MMU would.
 */
bool publish_secure_os_shadow_bytes(uc_engine *uc, uint64_t address,
                                           const uint8_t *bytes,
                                           size_t size, uint64_t writer_pc)
{
    size_t copied = 0;

    (void)writer_pc;
    secure_os_alias_sync_active = true;

    while (copied < size) {
        uint64_t current = address + copied;
        uint64_t va_page = current & ~(SECURE_OS_VA_PAGE_SIZE - 1);
        uint64_t pa_page = 0;
        size_t page_offset =
            (size_t)(current & (SECURE_OS_VA_PAGE_SIZE - 1));
        size_t chunk = size - copied;

        if (chunk > SECURE_OS_VA_PAGE_SIZE - page_offset)
            chunk = (size_t)SECURE_OS_VA_PAGE_SIZE - page_offset;

        if (!find_secure_os_shadow_page(va_page, &pa_page)) {
            copied += chunk;
            continue;
        }

        if (uc_mem_write(uc, pa_page + page_offset,
                         bytes + copied, chunk) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to publish SecureOS shadow write "
                    "VA=0x%016" PRIx64 " PA=0x%016" PRIx64 "\n",
                    current, pa_page + page_offset);
            secure_os_alias_sync_active = false;
            return false;
        }

        for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
            const struct secure_os_shadow_page *entry =
                &secure_os_shadow_pages[i];

            if (entry->pa == pa_page && entry->va != va_page &&
                uc_mem_write(uc, entry->va + page_offset,
                             bytes + copied, chunk) != UC_ERR_OK) {
                fprintf(stderr,
                        "[EL3] failed to update SecureOS alias "
                        "VA=0x%016" PRIx64 " from writer "
                        "VA=0x%016" PRIx64 "\n",
                        entry->va + page_offset, current);
                secure_os_alias_sync_active = false;
                return false;
            }
        }

        copied += chunk;
    }

    secure_os_alias_sync_active = false;
    return true;
}

bool publish_secure_os_pending_writes(uc_engine *uc)
{
    uint8_t bytes[64];

    for (size_t i = 0; i < secure_os_pending_write_count; i++) {
        uint64_t address = secure_os_pending_writes[i].address;
        size_t remaining = secure_os_pending_writes[i].size;

        while (remaining != 0) {
            size_t chunk = remaining > sizeof(bytes) ?
                           sizeof(bytes) : remaining;

            if (uc_mem_read(uc, address, bytes, chunk) != UC_ERR_OK ||
                !publish_secure_os_shadow_bytes(uc, address,
                                                bytes, chunk, 0)) {
                secure_os_pending_write_count = 0;
                return false;
            }

            address += chunk;
            remaining -= chunk;
        }
    }

    secure_os_pending_write_count = 0;
    return true;
}

bool sync_secure_os_va_shadow(uc_engine *uc)
{
    struct secure_os_shadow_owner {
        uint64_t pa;
        size_t index;
        bool occupied;
    };
    struct secure_os_shadow_owner *owners;
    uint8_t page[SECURE_OS_VA_PAGE_SIZE];
    size_t owner_capacity = 1;

    if (secure_os_pending_write_count != 0 &&
        !publish_secure_os_pending_writes(uc))
        return false;

    while (owner_capacity < secure_os_shadow_page_count * 2)
        owner_capacity <<= 1;
    owners = calloc(owner_capacity, sizeof(*owners));
    if (!owners)
        return false;

    /* Select the newest writer for every physical page in linear time. */
    for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
        const struct secure_os_shadow_page *entry =
            &secure_os_shadow_pages[i];
        size_t slot;

        if (entry->write_sequence == 0)
            continue;
        slot = (size_t)(((entry->pa >> 12) *
                         UINT64_C(0x9e3779b97f4a7c15)) &
                        (owner_capacity - 1));
        while (owners[slot].occupied && owners[slot].pa != entry->pa)
            slot = (slot + 1) & (owner_capacity - 1);
        if (!owners[slot].occupied ||
            secure_os_shadow_pages[owners[slot].index].write_sequence <
                entry->write_sequence) {
            owners[slot].occupied = true;
            owners[slot].pa = entry->pa;
            owners[slot].index = i;
        }
    }

    for (size_t slot = 0; slot < owner_capacity; slot++) {
        const struct secure_os_shadow_page *owner;

        if (!owners[slot].occupied)
            continue;
        owner = &secure_os_shadow_pages[owners[slot].index];
        if (uc_mem_read(uc, owner->va, page, sizeof(page)) != UC_ERR_OK ||
            uc_mem_write(uc, owner->pa, page, sizeof(page)) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to synchronize SecureOS page "
                    "VA=0x%016" PRIx64 " PA=0x%016" PRIx64 "\n",
                    owner->va, owner->pa);
            free(owners);
            return false;
        }
    }

    free(owners);
    return refresh_secure_os_va_shadow(uc);
}

bool refresh_secure_os_va_shadow(uc_engine *uc)
{
    uint8_t page[SECURE_OS_VA_PAGE_SIZE];

    for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
        struct secure_os_shadow_page *entry =
            &secure_os_shadow_pages[i];
        uint64_t current_pa = 0;

        if (entry->va >= UINT64_C(0xffff000000000000)) {
            if (translate_secure_os_va(uc, entry->va, &current_pa))
                current_pa &= ~(SECURE_OS_VA_PAGE_SIZE - 1);
        } else if (translate_secure_os_low_va(
                       uc, entry->va, &current_pa)) {
            current_pa &= ~(SECURE_OS_VA_PAGE_SIZE - 1);
        }

        if (current_pa != 0 && current_pa != entry->pa) {
            entry->pa = current_pa;
        }

        if (uc_mem_read(uc, entry->pa, page,
                        sizeof(page)) != UC_ERR_OK ||
            uc_mem_write(uc, entry->va, page,
                         sizeof(page)) != UC_ERR_OK) {
            fprintf(stderr,
                    "[EL3] failed to refresh SecureOS shadow page "
                    "VA=0x%016" PRIx64
                    " PA=0x%016" PRIx64 "\n",
                    entry->va, entry->pa);
            return false;
        }

        entry->write_sequence = 0;
    }

    return true;
}



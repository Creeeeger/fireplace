#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

void secure_os_instruction_cb(uc_engine *uc, uint64_t address,
                                     uint32_t size, void *user_data)
{
    uint32_t insn = UINT32_C(0xffffffff);

    (void)size;
    (void)user_data;

    if (!secure_os_active || bootchain_stage() != BOOTCHAIN_STAGE_EL3)
        return;

    /*
     * Wide stores are published at the next instruction boundary so every
     * SecureOS VA alias observes the completed value.
     */
    if (secure_os_pending_write_count != 0 &&
        !publish_secure_os_pending_writes(uc)) {
        bootchain_fail(uc);
        return;
    }

    if (!el3_mon_read_secure_os_instruction(uc, address, &insn) &&
        uc_mem_read(uc, address, &insn, sizeof(insn)) != UC_ERR_OK)
        return;

    /*
     * Unicorn does not apply the SecureOS EL1 banked return state itself.
     * Complete the architectural ERET using the virtual ELR/SPSR registers.
     */
    if (insn == AARCH64_ERET &&
        !complete_secure_os_el1_eret(uc, address))
        bootchain_fail(uc);
}

bool refresh_secure_os_shadow_for_access(uc_engine *uc,
                                                 uint64_t address)
{
    uint8_t page[SECURE_OS_VA_PAGE_SIZE];
    uint64_t va = address & ~(SECURE_OS_VA_PAGE_SIZE - 1);
    uint64_t registered_pa = 0;
    uint64_t current_pa = 0;

    if (va < UINT64_C(0xffff000000000000) ||
        !find_secure_os_shadow_page(va, &registered_pa) ||
        !translate_secure_os_va(uc, va, &current_pa))
        return true;

    current_pa &= ~(SECURE_OS_VA_PAGE_SIZE - 1);
    if (current_pa == registered_pa)
        return true;

    if (uc_mem_read(uc, current_pa, page, sizeof(page)) != UC_ERR_OK)
        return false;

    secure_os_alias_sync_active = true;
    if (uc_mem_write(uc, va, page, sizeof(page)) != UC_ERR_OK) {
        secure_os_alias_sync_active = false;
        return false;
    }
    secure_os_alias_sync_active = false;

    if (!remember_secure_os_shadow_page(va, current_pa))
        return false;

    return true;
}

void secure_os_read_cb(uc_engine *uc, uc_mem_type type,
                              uint64_t address, int size, int64_t value,
                              void *user_data)
{
    (void)type;
    (void)size;
    (void)value;
    (void)user_data;

    if (!secure_os_active || bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        secure_os_alias_sync_active ||
        active_smc != EL3_SMC_CUSTOM_REGISTER)
        return;

    if (!refresh_secure_os_shadow_for_access(uc, address)) {
        fprintf(stderr,
                "[EL3] failed to refresh SecureOS read mapping "
                "VA=0x%016" PRIx64 "\n",
                address);
        bootchain_fail(uc);
    }
}

void secure_os_write_cb(uc_engine *uc, uc_mem_type type,
                               uint64_t address, int size, int64_t value,
                               void *user_data)
{
    uint64_t pages[2];
    size_t page_count = 1;
    uint8_t bytes[sizeof(value)];

    (void)type;
    (void)user_data;

    if (!secure_os_active || bootchain_stage() != BOOTCHAIN_STAGE_EL3 ||
        secure_os_alias_sync_active || size <= 0)
        return;

    if (active_smc == EL3_SMC_CUSTOM_REGISTER &&
        !refresh_secure_os_shadow_for_access(uc, address)) {
        fprintf(stderr,
                "[EL3] failed to refresh SecureOS write mapping "
                "VA=0x%016" PRIx64 "\n",
                address);
        bootchain_fail(uc);
        return;
    }

    pages[0] = address & ~(SECURE_OS_VA_PAGE_SIZE - 1);
    pages[1] = (address + (uint64_t)size - 1) &
               ~(SECURE_OS_VA_PAGE_SIZE - 1);
    if (pages[1] != pages[0])
        page_count = 2;

    for (size_t page = 0; page < page_count; page++) {
        for (size_t i = 0; i < secure_os_shadow_page_count; i++) {
            if (secure_os_shadow_pages[i].va != pages[page])
                continue;
            secure_os_shadow_pages[i].write_sequence =
                ++secure_os_shadow_write_sequence;
            break;
        }
    }

    /* Scalar stores provide their complete value directly in the hook. */
    if ((size_t)size <= sizeof(value)) {
        memcpy(bytes, &value, (size_t)size);
        if (!publish_secure_os_shadow_bytes(uc, address, bytes,
                                            (size_t)size, 0))
            bootchain_fail(uc);
        return;
    }

    /*
     * Unicorn truncates the hook value for wide/vector stores.  Publish
     * those bytes from the completed writer alias at the next instruction.
     */
    if (secure_os_pending_write_count ==
        SECURE_OS_PENDING_WRITE_CAPACITY) {
        fprintf(stderr, "[EL3] SecureOS pending-write table full\n");
        bootchain_fail(uc);
        return;
    }

    secure_os_pending_writes[secure_os_pending_write_count++] =
        (struct secure_os_pending_write) {
            .address = address,
            .size = (uint32_t)size,
        };
}



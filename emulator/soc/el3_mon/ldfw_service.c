#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

const struct ldfw_monitor_frame *find_ldfw_monitor_frame(uint64_t base)
{
    for (size_t i = 0; i < ldfw_context_count; i++) {
        if (ldfw_contexts[i].base == base &&
            ldfw_contexts[i].monitor_frame.valid) {
            return &ldfw_contexts[i].monitor_frame;
        }
    }

    return NULL;
}

void ldfw_low_va_cb(uc_engine *uc, uint64_t address, uint32_t size,
                           void *user_data)
{
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3)
        return;

    if (secure_os_active) {
        bool populated = false;
        bool current_populated = false;

        if (!publish_secure_os_pending_writes(uc)) {
            bootchain_fail(uc);
            return;
        }

        if (!secure_os_low_image_ready) {
            if (!populate_secure_os_low_image(uc, &populated)) {
                fprintf(stderr,
                        "[EL3] failed to prepare SecureOS TTBR0 image\n");
                bootchain_fail(uc);
                return;
            }
            secure_os_low_image_ready = true;
        }

        if (!page_in_secure_os_low_va(uc, address,
                                      &current_populated)) {
            fprintf(stderr,
                    "[EL3] failed to prepare SecureOS low VA "
                    "0x%016" PRIx64 "\n",
                    address);
            bootchain_fail(uc);
            return;
        }
        populated |= current_populated;

        if (populated) {
            if (uc_ctl_flush_tb(uc) != UC_ERR_OK) {
                fprintf(stderr,
                        "[EL3] failed to flush SecureOS TTBR0 code "
                        "translation at 0x%016" PRIx64 "\n",
                        address);
                bootchain_fail(uc);
                return;
            }
            bootchain_request_resume(address);
            uc_emu_stop(uc);
            return;
        }

        return;
    }

    /*
     * Fallback for Unicorn/firmware paths that execute the monitor ERET
     * before the generic ERET hook can redirect it.  Address zero is the
     * uninitialized normal-world ELR, not an LDFW virtual address.
     */
    if (address == 0 &&
        secure_os_runtime_smc_pending &&
        secure_os_runtime_returns_to_lk) {
        if (!complete_secure_os_return_to_lk(uc)) {
            fprintf(stderr,
                    "[EL3] failed to recover SecureOS normal-world "
                    "return at VA 0\n");
            bootchain_fail(uc);
        }
        return;
    }

    if (!servicing_lk || !ldfw_shadow_active)
        return;
    if (ldfw_shadow_context == ldfw_context_base &&
        ldfw_shadow_contains(address)) {
        /* Each LDFW module has the same printf entry at a different VA. */
        if (secure_os_internal_logs &&
            (address == UINT64_C(0x147a0) || address == UINT64_C(0x3148) ||
             address == UINT64_C(0x6a4c) || address == UINT64_C(0x3ec8) ||
             address == UINT64_C(0x7ddc))) {
            const uint32_t prologue[] = {
                0xa9ba6ffc, 0xa90167fa, 0xa9025ff8, 0xa90357f6,
                0xa9044ff4, 0xa9057bfd, 0x910143fd, 0xd10103ff,
                0xaa0003f4, 0xa9031fe6, 0xa90217e4, 0xa9010fe2,
            };
            uint32_t code[ARRAY_SIZE(prologue)];

            /* Low VAs are reused: verify that this context owns printf. */
            if (uc_mem_read(uc, address, code, sizeof(code)) == UC_ERR_OK &&
                memcmp(code, prologue, sizeof(code)) == 0)
                bootchain_log_console(uc, true);
        }
        return;
    }

    fprintf(stderr, "[EL3] LDFW executed unmapped VA 0x%08" PRIx64 "\n",
            address);
    bootchain_fail(uc);
}



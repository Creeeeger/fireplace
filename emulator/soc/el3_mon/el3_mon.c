#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

struct secure_os_shadow_page secure_os_shadow_pages[SECURE_OS_SHADOW_PAGE_CAPACITY] = {0};
size_t secure_os_shadow_page_count;
uint64_t secure_os_shadow_write_sequence;
struct secure_os_pending_write secure_os_pending_writes[SECURE_OS_PENDING_WRITE_CAPACITY] = {0};
size_t secure_os_pending_write_count;
bool secure_os_alias_sync_active;
bool secure_os_low_image_ready;
uint64_t return_spsr;
uint64_t return_address;
bool servicing_lk;
uint64_t active_smc;
uint64_t monitor_sp;
uint64_t lower_sp;
bool ldfw_runtime_setup_active;
uint64_t ldfw_context_base;
bool ldfw_shadow_active;
uint64_t ldfw_shadow_context;
uint64_t ldfw_shadow_size;
bool secure_os_active;
uint64_t secure_os_monitor_sp;
uint64_t secure_os_runtime_sp;
bool secure_os_runtime_smc_pending;
bool secure_os_runtime_returns_to_lk;
uint64_t secure_os_runtime_lk_x0;
uint64_t restored_secure_os_sp_el1;
uint64_t saved_secure_os_sp;
uint64_t saved_secure_os_sp_el0;
bool saved_secure_os_sp_el0_valid;
bool harx_active;
bool harx_runtime_smc_pending;
uint64_t harx_monitor_sp;
uint64_t harx_runtime_sp;
uint64_t harx_image_base;
uint64_t harx_image_size;
bool harx_initialized;
bool harx_hvc_pending;
uint64_t harx_plugin_base;
uint64_t harx_plugin_size;
uint64_t harx_preserved_sp_el1;
struct lk_smc_frame lk_saved_smc_frame;
struct el1_register_context normal_world_el1_context;
struct ldfw_monitor_frame ldfw_saved_monitor_frame;
struct ldfw_context ldfw_contexts[8] = {0};
size_t ldfw_context_count;
bool harx_invalid_memory_reported;
uint8_t ldfw_shadow_mapped[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE] = {0};
uint8_t ldfw_normal_memory_saved[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE] = {0};
uint8_t ldfw_normal_memory_mapped[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE] = {0};
uint8_t *ldfw_normal_memory;
uint64_t ldfw_normal_memory_size;

uc_err seed_el3_cpu_guard(uc_engine *uc)
{
    const uint64_t guard[2] = {
        EL3_CPU_GUARD_MAGIC,
        ~EL3_CPU_GUARD_MAGIC,
    };

    return uc_mem_write(uc, EL3_CPU_GUARD_BASE, guard, sizeof(guard));
}

void reset_el3_state(void)
{
    memset(&normal_world_el1_context, 0, sizeof(normal_world_el1_context));
    return_spsr = 0;
    return_address = 0;
    servicing_lk = false;
    active_smc = 0;
    reset_lk_smc_frame();
    monitor_sp = 0;
    lower_sp = 0;

    ldfw_runtime_setup_active = false;
    ldfw_context_base = 0;
    ldfw_context_count = 0;
    memset(ldfw_contexts, 0, sizeof(ldfw_contexts));
    reset_ldfw_va_shadow();
    reset_ldfw_monitor_frame();

    secure_os_active = false;
    secure_os_monitor_sp = 0;
    secure_os_runtime_sp = 0;
    secure_os_runtime_smc_pending = false;
    secure_os_runtime_returns_to_lk = false;
    secure_os_runtime_lk_x0 = 0;
    restored_secure_os_sp_el1 = 0;
    saved_secure_os_sp = 0;
    saved_secure_os_sp_el0 = 0;
    saved_secure_os_sp_el0_valid = false;
    secure_os_shadow_page_count = 0;
    secure_os_shadow_write_sequence = 0;
    memset(secure_os_shadow_pages, 0, sizeof(secure_os_shadow_pages));
    secure_os_pending_write_count = 0;
    secure_os_alias_sync_active = false;
    secure_os_low_image_ready = false;
    memset(secure_os_pending_writes, 0, sizeof(secure_os_pending_writes));

    harx_active = false;
    harx_runtime_smc_pending = false;
    harx_monitor_sp = 0;
    harx_runtime_sp = 0;
    harx_image_base = 0;
    harx_image_size = 0;
    harx_initialized = false;
    harx_hvc_pending = false;
    harx_plugin_base = 0;
    harx_plugin_size = 0;
    harx_preserved_sp_el1 = 0;
    harx_invalid_memory_reported = false;
}

uc_err el3_mon_init(uc_engine *uc)
{
    const struct bootchain_hook hooks[] = {
        BOOTCHAIN_CODE_HOOK(return_context_cb, UINT64_C(0xbfe91954)),
        BOOTCHAIN_CODE_HOOK(return_context_cb, UINT64_C(0xbfe91958)),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, el3_return_cb,
                             EL3_LOW_ADDRESS_BASE, EL3_LOW_ADDRESS_END),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, secure_os_instruction_cb,
                             EL3_SECURE_OS_BASE, EL3_SECURE_OS_END - 1),
        /* Handler tasks reuse TTBR0-backed low VAs, while the kernel-facing
         * runtime uses canonical high VAs. Both must pass through the same
         * coherence callback even though the address spaces are disjoint. */
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, secure_os_instruction_cb, 0,
                             SECURE_OS_LOW_IMAGE_END - 1),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, secure_os_instruction_cb,
                             UINT64_C(0xffff000000000000), UINT64_MAX),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_MEM_READ, secure_os_read_cb,
                             UINT64_C(0xffff000000000000), UINT64_MAX),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_MEM_WRITE, secure_os_write_cb,
                             UINT64_C(0xffff000000000000), UINT64_MAX),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_MEM_WRITE, secure_os_write_cb, 0,
                             UINT64_C(0x0000ffffffffffff)),
        BOOTCHAIN_CODE_HOOK(ldfw_registered_service_resume_cb,
                            EL3_REGISTERED_SERVICE_RESUME_CALL),
        BOOTCHAIN_CODE_HOOK(ldfw_registered_service_enter_cb,
                            EL3_REGISTERED_SERVICE_ENTER_CALL),
        BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, ldfw_low_va_cb, 0,
                             EL3_LDFW_LOW_VA_LIMIT - 1),
        BOOTCHAIN_CODE_HOOK(ldfw_decrypt_success_cb,
                            EL3_LDFW_DECRYPT_SUCCESS_RETURN),
        BOOTCHAIN_CODE_HOOK(ldfw_entry_cb, EL3_LDFW_ENTRY_CALL),
        BOOTCHAIN_CODE_HOOK(ldfw_context_switch_cb,
                            EL3_CONTEXT_SWITCH_ENTRY),
        BOOTCHAIN_CODE_HOOK(lk_handoff_cb, EL3_CONTEXT_SWITCH_ERET),
    };
    uc_err err;

    reset_el3_state();
    err = seed_el3_cpu_guard(uc);
    if (err != UC_ERR_OK)
        return err;

    return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

#ifndef FIREPLACE_SOC_EL3_MON_INTERNAL_H
#define FIREPLACE_SOC_EL3_MON_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bootchain/bootchain_internal.h"

#define MRS_SCTLR_EL1 UINT32_C(0xd5381000)
#define MRS_TTBR0_EL1 UINT32_C(0xd5382000)
#define MRS_TTBR1_EL1 UINT32_C(0xd5382020)
#define MRS_TCR_EL1   UINT32_C(0xd5382040)
#define MRS_ESR_EL1   UINT32_C(0xd5385200)
#define MRS_SPSR_EL1  UINT32_C(0xd5384000)
#define MRS_ELR_EL1   UINT32_C(0xd5384020)
#define MRS_SP_EL0    UINT32_C(0xd5384100)
#define MRS_TPIDRRO_EL0 UINT32_C(0xd53bd060)

#define SECURE_OS_VA_PAGE_SIZE UINT64_C(0x1000)
#define SECURE_OS_SHADOW_PAGE_CAPACITY 16384
#define SECURE_OS_PENDING_WRITE_CAPACITY 64
#define SECURE_OS_LOW_IMAGE_END UINT64_C(0x100000)

#define EL3_SECURE_PAYLOAD_READY_ADDR UINT64_C(0xbfea0c38)
#define EL3_SECURE_PAYLOAD_READY_VALUE UINT32_C(1)
struct secure_os_shadow_page {
    uint64_t va;
    uint64_t pa;
    uint64_t write_sequence;
};
struct secure_os_pending_write {
    uint64_t address;
    uint64_t pc;
    uint32_t size;
    uint64_t regs[12]; /* x19-x30 at the wide-store instruction. */
};

struct lk_smc_frame {
    bool valid;
    uint64_t return_address;
    uint64_t sp;
    uint64_t pstate;
    uint64_t regs[31];
};

struct el1_register_context {
    bool valid;
    uint64_t sctlr;
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
};

struct ldfw_monitor_frame {
    bool valid;
    uint64_t context_base;
    uint64_t sp;
    uint64_t regs[12]; /* x19-x30, matching FUN_bfe914fc's frame. */
};

struct ldfw_context {
    uint64_t base;
    uint64_t size;
    struct ldfw_monitor_frame monitor_frame;
};

#define EL3_LOW_ADDRESS_BASE UINT64_C(0x0)
#define EL3_LOW_ADDRESS_END  UINT64_C(0xFFFFFFFF)
#define EL3_VECTOR_ENTRY UINT64_C(0xbfe93c00)
#define EL3_EXCEPTION_PSTATE UINT64_C(0x3cd)
#define EL3_SMC_LDFW UINT64_C(0xfffffffffffffb00)
#define EL3_SMC_SECURE_OS UINT64_C(0xfffffffffffffaef)
#define EL3_SMC_START_USERSPACE UINT64_C(0xb2000014)
#define EL3_SMC_CUSTOM_REGISTER UINT64_C(0xb2000102)
#define EL3_SMC_CUSTOM_SERVICE UINT64_C(0xb2000202)
#define EL3_SMC_OTP_CONTROL UINT64_C(0xc2001014)
#define EL3_SMC_HARX_INIT UINT64_C(0x82000480)
#define HARX_HVC_REGISTER_PLUGIN UINT64_C(0xc6000010)
#define HARX_VECTOR_OFFSET UINT64_C(0x7800)
#define HARX_LOWER_AARCH64_SYNC_OFFSET UINT64_C(0x400)
#define HARX_EL2_EXCEPTION_PSTATE UINT64_C(0x3c9)
#define MRS_ESR_EL2 UINT32_C(0xd53c5200)
#define MRS_ELR_EL2 UINT32_C(0xd53c4020)
#define MRS_SPSR_EL2 UINT32_C(0xd53c4000)
#define EL3_SMC_SECURE_OS_REGISTER UINT64_C(0xb2000300)
#define EL3_SMC_SECURE_OS_RETURN_NORMAL UINT64_C(0xb2000301)
#define EL3_SMC_SECURE_OS_REGISTER_SECOND UINT64_C(0xb2000302)
#define AARCH64_ERET UINT32_C(0xd69f03e0)
#define EL3_SECURE_PAYLOAD_SCR UINT64_C(0x430)
#define EL3_LDFW_DECRYPT_SUCCESS_RETURN UINT64_C(0xbfe89814)
#define EL3_LDFW_ENTRY_CALL UINT64_C(0xbfe89ab4)
#define EL3_CONTEXT_SWITCH_ENTRY UINT64_C(0xbfe914fc)
#define EL3_CONTEXT_SWITCH_ERET UINT64_C(0xbfe91930)
#define EL3_RESTORE_SP_EL1 UINT64_C(0xbfe91838)
#define EL3_REGISTERED_SERVICE_ENTER_CALL UINT64_C(0xbfe8a420)
#define EL3_REGISTERED_SERVICE_RESUME_CALL UINT64_C(0xbfe8a394)
#define EL3_LDFW_RUNTIME_BASE UINT64_C(0xbf700000)
#define EL3_LDFW_RUNTIME_END EL3_LOAD_ADDR
#define EL3_SECURE_OS_BASE UINT64_C(0xbab00000)
#define EL3_SECURE_OS_END  UINT64_C(0xbf600000)
#define EL3_LDFW_CONTEXT_SP_OFFSET UINT64_C(0x40)
#define EL3_LDFW_CONTEXT_SPSR_OFFSET UINT64_C(0x150)
#define EL3_LDFW_CONTEXT_ELR_OFFSET UINT64_C(0x158)
#define EL3_LDFW_CONTEXT_SCR_OFFSET UINT64_C(0x140)
#define EL3_LDFW_CONTEXT_TTBR0_OFFSET UINT64_C(0x1a0)
#define EL3_LDFW_CONTEXT_TCR_OFFSET UINT64_C(0x1c0)
#define EL3_LDFW_LOW_VA_LIMIT UINT64_C(0x10000000)
#define EL3_LDFW_SHADOW_MAX_SIZE EL3_LDFW_LOW_VA_LIMIT
#define EL3_LDFW_PAGE_SIZE UINT64_C(0x1000)
#define EL3_LDFW_RUNTIME_PADDING UINT64_C(0x11000)
#define EL3_AARCH64_DESC_ADDR_MASK UINT64_C(0x0000fffffffff000)
#define EL3_CPU_GUARD_BASE UINT64_C(0xbfe96640)
#define EL3_CPU_GUARD_MAGIC UINT64_C(0xabcd01234567cdef)
extern struct secure_os_shadow_page secure_os_shadow_pages[SECURE_OS_SHADOW_PAGE_CAPACITY];
extern size_t secure_os_shadow_page_count;
extern uint64_t secure_os_shadow_write_sequence;
extern struct secure_os_pending_write secure_os_pending_writes[SECURE_OS_PENDING_WRITE_CAPACITY];
extern size_t secure_os_pending_write_count;
extern bool secure_os_alias_sync_active;
extern bool secure_os_low_image_ready;
extern uint64_t return_spsr;
extern uint64_t return_address;
extern bool servicing_lk;
extern uint64_t active_smc;
extern uint64_t monitor_sp;
extern uint64_t lower_sp;
extern bool ldfw_runtime_setup_active;
extern uint64_t ldfw_context_base;
extern bool ldfw_shadow_active;
extern uint64_t ldfw_shadow_context;
extern uint64_t ldfw_shadow_size;
extern bool secure_os_active;
extern uint64_t secure_os_monitor_sp;
extern uint64_t secure_os_runtime_sp;
extern bool secure_os_runtime_smc_pending;
extern bool secure_os_runtime_returns_to_lk;
extern uint64_t secure_os_runtime_lk_x0;
extern uint64_t restored_secure_os_sp_el1;
extern uint64_t saved_secure_os_sp;
extern uint64_t saved_secure_os_sp_el0;
extern bool saved_secure_os_sp_el0_valid;
extern bool harx_active;
extern bool harx_runtime_smc_pending;
extern uint64_t harx_monitor_sp;
extern uint64_t harx_runtime_sp;
extern uint64_t harx_image_base;
extern uint64_t harx_image_size;
extern bool harx_initialized;
extern bool harx_hvc_pending;
extern uint64_t harx_plugin_base;
extern uint64_t harx_plugin_size;
extern uint64_t harx_preserved_sp_el1;
extern struct lk_smc_frame lk_saved_smc_frame;
extern struct el1_register_context normal_world_el1_context;
extern struct ldfw_monitor_frame ldfw_saved_monitor_frame;
extern struct ldfw_context ldfw_contexts[8];
extern size_t ldfw_context_count;
extern bool harx_invalid_memory_reported;
extern uint8_t ldfw_shadow_mapped[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE];
extern uint8_t ldfw_normal_memory_saved[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE];
extern uint8_t ldfw_normal_memory_mapped[EL3_LDFW_SHADOW_MAX_SIZE / EL3_LDFW_PAGE_SIZE];
extern uint8_t *ldfw_normal_memory;
extern uint64_t ldfw_normal_memory_size;
bool walk_aarch64_4k_table(uc_engine *uc, uint64_t table,
                                  uint64_t va, unsigned int start_level,
                                  uint64_t *pa);

bool sync_secure_os_va_shadow(uc_engine *uc);
bool refresh_secure_os_va_shadow(uc_engine *uc);
bool capture_normal_world_el1_context(void);
bool restore_normal_world_el1_context(void);

bool complete_secure_os_el1_eret(uc_engine *uc, uint64_t address);
bool read_secure_os_x_register(uc_engine *uc, unsigned int index,
                                      uint64_t *value);
void secure_os_instruction_cb(uc_engine *uc, uint64_t address,
                                     uint32_t size, void *user_data);
void secure_os_read_cb(uc_engine *uc, uc_mem_type type,
                              uint64_t address, int size, int64_t value,
                              void *user_data);
void secure_os_write_cb(uc_engine *uc, uc_mem_type type,
                               uint64_t address, int size, int64_t value,
                               void *user_data);
uc_err seed_el3_cpu_guard(uc_engine *uc);
bool is_lk_return_target(uint64_t address);
bool is_ldfw_return_target(uint64_t address);
bool is_secure_os_return_target(uint64_t address);
bool is_harx_return_target(uint64_t address);
bool read_u64(uc_engine *uc, uint64_t address, uint64_t *value);
bool capture_lk_smc_frame(uc_engine *uc, uint64_t return_address);
bool complete_secure_os_return_to_lk(uc_engine *uc);
bool complete_harx_hvc_return_to_lk(uc_engine *uc,
                                           uint64_t eret_address);
bool restore_secure_os_sp_el0(uc_engine *uc);
void reset_lk_smc_frame(void);
void reset_ldfw_monitor_frame(void);
void capture_ldfw_monitor_frame(uc_engine *uc, uint64_t context_base);
bool restore_ldfw_monitor_frame(
    uc_engine *uc, const struct ldfw_monitor_frame *frame);
bool prepare_ldfw_va_shadow(uc_engine *uc, uint64_t context_base,
                                   uint64_t size);
bool translate_ldfw_context_va_internal(uc_engine *uc,
                                               uint64_t context_base,
                                               uint64_t va, uint64_t *pa,
                                               bool report_failure);
bool sync_ldfw_va_shadow(uc_engine *uc);
bool deactivate_ldfw_va_shadow(uc_engine *uc);
void reset_ldfw_va_shadow(void);
bool ldfw_shadow_contains(uint64_t va);
bool remember_ldfw_context(uint64_t base, uint64_t size);
bool find_ldfw_context(uint64_t base, uint64_t *size);
const struct ldfw_monitor_frame *find_ldfw_monitor_frame(uint64_t base);
bool translate_secure_os_va(uc_engine *uc, uint64_t va,
                                   uint64_t *pa);
bool translate_secure_os_low_va(uc_engine *uc, uint64_t va,
                                       uint64_t *pa);
bool translate_secure_os_va_coherent(uc_engine *uc, uint64_t va,
                                            uint64_t *pa);
bool find_secure_os_shadow_page(uint64_t va, uint64_t *pa);
bool remember_secure_os_shadow_page(uint64_t va, uint64_t pa);
bool sync_secure_os_va_shadow(uc_engine *uc);
bool publish_secure_os_pending_writes(uc_engine *uc);
int lk_gpr_id(size_t index);
bool page_in_secure_os_low_va(uc_engine *uc, uint64_t address,
                                     bool *populated);
bool populate_secure_os_low_image(uc_engine *uc, bool *populated);
bool page_in_secure_os_va(uc_engine *uc, uint64_t address);
void return_context_cb(uc_engine *uc, uint64_t address, uint32_t size,
		       void *user_data);
void ldfw_registered_service_enter_cb(uc_engine *uc, uint64_t address,
				      uint32_t size, void *user_data);
void ldfw_registered_service_resume_cb(uc_engine *uc, uint64_t address,
				       uint32_t size, void *user_data);
void ldfw_low_va_cb(uc_engine *uc, uint64_t address, uint32_t size,
		    void *user_data);
void lk_handoff_cb(uc_engine *uc, uint64_t address, uint32_t size,
		   void *user_data);
void ldfw_decrypt_success_cb(uc_engine *uc, uint64_t address, uint32_t size,
			     void *user_data);
void ldfw_entry_cb(uc_engine *uc, uint64_t address, uint32_t size,
		   void *user_data);
void ldfw_context_switch_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data);
void el3_return_cb(uc_engine *uc, uint64_t address, uint32_t size,
		   void *user_data);
bool publish_secure_os_shadow_bytes(uc_engine *uc, uint64_t address,
				    const uint8_t *bytes, size_t size,
				    uint64_t writer_pc);

#endif


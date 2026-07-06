#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el3_mon/el3_mon_internal.h"

void return_context_cb(uc_engine *uc, uint64_t address, uint32_t size,
                  void *user_data)
{
    uint64_t value;
    int register_id;

    (void)size;
    (void)user_data;

    if (address == UINT64_C(0xbfe91954))
        register_id = UC_ARM64_REG_X16;
    else
        register_id = UC_ARM64_REG_X17;

    if (uc_reg_read(uc, register_id, &value) != UC_ERR_OK) {
        fprintf(stderr, "[EL3] failed to capture return context\n");
        bootchain_fail(uc);
        return;
    }

    if (address == UINT64_C(0xbfe91954)) {
        return_spsr = value;
    } else {
        return_address = value;
    }
}

void reset_ldfw_monitor_frame(void)
{
    memset(&ldfw_saved_monitor_frame, 0, sizeof(ldfw_saved_monitor_frame));
}

void capture_ldfw_monitor_frame(uc_engine *uc, uint64_t context_base)
{
    static const int reg_ids[] = {
        UC_ARM64_REG_X19, UC_ARM64_REG_X20, UC_ARM64_REG_X21,
        UC_ARM64_REG_X22, UC_ARM64_REG_X23, UC_ARM64_REG_X24,
        UC_ARM64_REG_X25, UC_ARM64_REG_X26, UC_ARM64_REG_X27,
        UC_ARM64_REG_X28, UC_ARM64_REG_X29, UC_ARM64_REG_X30,
    };
    uint64_t sp = 0;

    if (context_base == 0)
        return;

    if (uc_reg_read(uc, UC_ARM64_REG_SP, &sp) != UC_ERR_OK)
        return;

    ldfw_saved_monitor_frame.valid = true;
    ldfw_saved_monitor_frame.context_base = context_base;
    ldfw_saved_monitor_frame.sp = sp;

    for (size_t i = 0; i < ARRAY_SIZE(reg_ids); i++)
        uc_reg_read(uc, reg_ids[i], &ldfw_saved_monitor_frame.regs[i]);
}

bool restore_ldfw_monitor_frame(
    uc_engine *uc, const struct ldfw_monitor_frame *frame)
{
    uint64_t frame_addr;

    if (!frame || !frame->valid || frame->sp < UINT64_C(0x60))
        return false;

    frame_addr = frame->sp - UINT64_C(0x60);
    if (uc_mem_write(uc, frame_addr, frame->regs,
             sizeof(frame->regs)) != UC_ERR_OK)
        return false;

    if (uc_mem_write(uc, frame->context_base, &frame->sp,
             sizeof(frame->sp)) != UC_ERR_OK)
        return false;

    return true;
}

void ldfw_registered_service_enter_cb(uc_engine *uc, uint64_t address,
                                              uint32_t size, void *user_data)
{
    uint64_t context_base = 0;
    uint64_t context_size = 0;
    bool known_context;

    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || !servicing_lk ||
        uc_reg_read(uc, UC_ARM64_REG_X0, &context_base) != UC_ERR_OK)
        return;

    known_context = find_ldfw_context(context_base, &context_size);

    if (!known_context)
        return;

    if (!prepare_ldfw_va_shadow(uc, context_base, context_size)) {
        fprintf(stderr,
                "[EL3] failed to enter LDFW context 0x%08" PRIx64 "\n",
                context_base);
        bootchain_fail(uc);
        return;
    }
    ldfw_context_base = context_base;
}

void ldfw_registered_service_resume_cb(uc_engine *uc, uint64_t address,
                          uint32_t size, void *user_data)
{
    uint64_t svc_ctx = 0;
    uint64_t saved_sp = 0;
    uint64_t saved_fp = 0;
    uint64_t saved_lr = 0;
    const struct ldfw_monitor_frame *frame;

    (void)address;
    (void)size;
    (void)user_data;

    if (bootchain_stage() != BOOTCHAIN_STAGE_EL3 || !servicing_lk)
        return;

    uc_reg_read(uc, UC_ARM64_REG_X0, &svc_ctx);
    read_u64(uc, svc_ctx, &saved_sp);
    if (saved_sp >= UINT64_C(0x10)) {
        read_u64(uc, saved_sp - UINT64_C(0x10), &saved_fp);
        read_u64(uc, saved_sp - UINT64_C(0x8), &saved_lr);
    }
    frame = find_ldfw_monitor_frame(svc_ctx);
    if (frame &&
        (saved_sp != frame->sp || saved_fp != frame->regs[10] ||
         saved_lr != frame->regs[11])) {
        (void)restore_ldfw_monitor_frame(uc, frame);
    }
}

bool is_lk_return_target(uint64_t address)
{
    return address >= LK_LOAD_ADDR && address < LK_LOAD_ADDR + LK_IMAGE_SIZE;
}

bool is_ldfw_return_target(uint64_t address)
{
    return address >= EL3_LDFW_RUNTIME_BASE &&
           address < EL3_LDFW_RUNTIME_END;
}

bool is_secure_os_return_target(uint64_t address)
{
    if (address >= EL3_SECURE_OS_BASE &&
        address < EL3_SECURE_OS_END)
        return true;

    return address >= UINT64_C(0xffff000000000000);
}

bool is_harx_return_target(uint64_t address)
{
    if (harx_image_base != 0 && harx_image_size != 0 &&
        address >= harx_image_base &&
        address - harx_image_base < harx_image_size)
        return true;

    return harx_plugin_base != 0 && harx_plugin_size != 0 &&
           address >= harx_plugin_base &&
           address - harx_plugin_base < harx_plugin_size;
}


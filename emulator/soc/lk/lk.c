
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_debug.h"
#include "lk/lk_devices.h"
#include "lk/lk_internal.h"

#define LK_REVISION_PROBE_ADDR UINT64_C(0xe8001948)
#define LK_KERNEL_HANDOFF_ADDR UINT64_C(0xe8018634)
#define LK_MDELAY_ADDR UINT64_C(0xe8002de0)
#define LK_REVISION_OUT_ADDR UINT64_C(0xe8154008)
#define LK_BOOT_DEVICE_UFS_WORD UINT64_C(0xcb000011)

static bool revision_logged;

static bool read_link_register(uc_engine *uc, uint64_t *link)
{
	return uc_reg_read(uc, UC_ARM64_REG_LR, link) == UC_ERR_OK;
}

static void return_to_link(uc_engine *uc)
{
	(void)bootchain_return_to_link(uc);
}

static void lk_return_cb(uc_engine *uc, uint64_t address, uint32_t size,
			 void *user_data)
{
	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() == BOOTCHAIN_STAGE_LK) {
		uint64_t link;

		if (read_link_register(uc, &link) && link != address)
			uc_reg_write(uc, UC_ARM64_REG_PC, &link);
	}
}

static void lk_return_value_cb(uc_engine *uc, uint64_t address, uint32_t size,
			       void *user_data)
{
	uint64_t value = (uint64_t)(uintptr_t)user_data;

	(void)address;
	(void)size;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;
	uc_reg_write(uc, UC_ARM64_REG_X0, &value);
	return_to_link(uc);
}

static void lk_revision_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	const uint32_t revision = 22;
	uint64_t result = revision;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;
	if (uc_mem_write(uc, LK_REVISION_OUT_ADDR, &revision,
			 sizeof(revision)) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK) {
		bootchain_fail(uc);
		return;
	}
	if (!revision_logged) {
		printf("[LK hook] board revision probe -> %" PRIu32 "\n",
		       revision);
		revision_logged = true;
	}
	return_to_link(uc);
}

static void lk_kernel_handoff_cb(uc_engine *uc, uint64_t address,
				 uint32_t size, void *user_data)
{
	uint64_t entry = 0;
	uint64_t dtb = 0;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;

	if (uc_reg_read(uc, UC_ARM64_REG_X4, &entry) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X0, &dtb) != UC_ERR_OK) {
		bootchain_fail(uc);
		return;
	}

	printf("[LK hook] kernel handoff entry=0x%016" PRIx64
	       " dtb=0x%016" PRIx64 "\n",
	       entry, dtb);
	fflush(stdout);
	if (!bootchain_transition(uc, BOOTCHAIN_STAGE_LK,
				  BOOTCHAIN_STAGE_KERNEL))
		bootchain_fail(uc);
}

static void lk_boot_device_cb(uc_engine *uc, uint64_t address, uint32_t size,
			      void *user_data)
{
	uint64_t boot_device = LK_BOOT_DEVICE_UFS_WORD;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;
	uc_reg_write(uc, UC_ARM64_REG_X0, &boot_device);
	return_to_link(uc);
}

uc_err lk_init(uc_engine *uc, bool headless,
	       enum fireplace_boot_mode boot_mode)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(lk_kernel_handoff_cb, LK_KERNEL_HANDOFF_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_return_value_cb, UINT64_C(0xe801cf28)),
		BOOTCHAIN_CODE_HOOK(lk_printf_cb, UINT64_C(0xe80dee98)),
		BOOTCHAIN_CODE_HOOK(lk_revision_cb, LK_REVISION_PROBE_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_boot_device_cb, UINT64_C(0xe8012ee0)),
		BOOTCHAIN_CODE_HOOK(lk_return_cb, LK_MDELAY_ADDR),
	};
	uc_err err;

	revision_logged = false;
	lk_patches_configure(headless);
	err = bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
	if (err == UC_ERR_OK)
		err = lk_boot_mode_init(uc, boot_mode);
	if (err == UC_ERR_OK)
		err = lk_display_init(uc);
	if (err == UC_ERR_OK)
		err = lk_devices_init(uc);
	return err;
}

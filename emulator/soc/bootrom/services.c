
#include <inttypes.h>
#include <stdio.h>

#include "bootrom/bootrom_internal.h"

#define BOOTROM_BL1_RECEIVE_ADDR UINT64_C(0x00004f24)
#define BOOTROM_BL1_JUMP_ADDR UINT64_C(0x00001120)
#define BOOTROM_AUTH_READY_ADDR UINT64_C(0x00002178)
#define BOOTROM_ECDSA_VERIFY_ADDR UINT64_C(0x0000376c)
#define BOOTROM_ECDSA_DISPATCH_ADDR UINT64_C(0x000167d8)
#define BOOTROM_PANIC_ADDR UINT64_C(0x00004860)
#define BOOTROM_PANIC_HALT_ADDR UINT64_C(0x000048e8)
#define BOOTROM_MATH_STATUS_ADDR UINT64_C(0x0000c150)
#define BOOTROM_CONSOLE_SEND_ADDR UINT64_C(0x000114bc)
#define BOOT_DEVICE_WORD_ADDR UINT64_C(0x02020064)
#define BOOT_DEVICE_UFS_WORD UINT32_C(0xcb000011)

static bool bootrom_image_hook_active(void)
{
	return bootchain_stage() == BOOTCHAIN_STAGE_BOOTROM;
}

static bool bootrom_service_hook_active(void)
{
	enum bootchain_stage stage = bootchain_stage();

	return stage == BOOTCHAIN_STAGE_BOOTROM ||
	       stage == BOOTCHAIN_STAGE_FWBL1 ||
	       stage == BOOTCHAIN_STAGE_EPBL ||
	       stage == BOOTCHAIN_STAGE_BL2 ||
	       el3_mon_bootrom_service_active();
}

static uint32_t bootrom_verify_ecdsa(uc_engine *uc)
{
	(void)uc;

	/* The vendor service depends on SSS behavior outside this profile. Keep
	 * the real call boundary hooked, but make the compatibility policy explicit
	 * instead of compiling an unreachable software verifier behind a constant. */
	return 0;
}

static void ecdsa_verify_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data)
{
	uint64_t link;
	uint64_t result;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	result = bootrom_verify_ecdsa(uc);
	if (uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr, "[BootROM] failed to return from ECDSA service\n");
		bootchain_fail(uc);
	}
}

static void ecdsa_dispatch_cb(uc_engine *uc, uint64_t address,
			      uint32_t size, void *user_data)
{
	uint64_t link = 0;
	uint64_t result;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	result = bootrom_verify_ecdsa(uc);
	if (uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr,
			"[BootROM] failed to return from ECDSA dispatch\n");
		bootchain_fail(uc);
	}
}

static void receive_fwbl1_cb(uc_engine *uc, uint64_t address, uint32_t size,
			     void *user_data)
{
	uint64_t link;
	uint64_t result = 1;
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_image_hook_active())
		return;
	err = bootchain_load_image(uc, FWBL1_IMAGE, FWBL1_LOAD_ADDR,
				   FWBL1_IMAGE_SIZE);
	if (err == UC_ERR_OK)
		err = uc_reg_read(uc, UC_ARM64_REG_LR, &link);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_X0, &result);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_PC, &link);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "[BootROM] failed to receive FWBL1: %s\n",
			uc_strerror(err));
		bootchain_fail(uc);
		return;
	}
	printf("[BootROM] loaded FWBL1 at 0x%" PRIx64 "\n", FWBL1_LOAD_ADDR);
}


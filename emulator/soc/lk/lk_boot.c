
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_internal.h"

#define LK_INIT_PARAM_PART_RETURN UINT64_C(0xe800240c)
#define LK_DOWNLOAD_CHECK_RETURN UINT64_C(0xe8002504)
#define LK_PARAM_REBOOT_MODE_ADDR UINT64_C(0xe8b82178)
#define LK_REBOOT_MODE_NONE UINT32_C(0)
#define LK_REBOOT_MODE_RECOVERY UINT32_C(4)
#define LK_DOWNLOAD_MODE_DEVICE_INFO UINT64_C(4)

static enum fireplace_boot_mode selected_boot_mode;

static const char *lk_boot_mode_name(enum fireplace_boot_mode mode)
{
	switch (mode) {
	case FIREPLACE_BOOT_RECOVERY:
		return "recovery";
	case FIREPLACE_BOOT_DOWNLOAD:
		return "download";
	case FIREPLACE_BOOT_ANDROID:
	default:
		return "android";
	}
}

static void lk_select_boot_mode_cb(uc_engine *uc, uint64_t address,
				   uint32_t size, void *user_data)
{
	uint32_t reboot_mode;
	uint32_t requested_mode;
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;

	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;

	err = uc_mem_read(uc, LK_PARAM_REBOOT_MODE_ADDR,
			  &reboot_mode, sizeof(reboot_mode));
	if (err != UC_ERR_OK) {
		fprintf(stderr,
			"[LK] failed to read reboot mode at 0x%016" PRIx64
			": %s\n",
			LK_PARAM_REBOOT_MODE_ADDR, uc_strerror(err));
		bootchain_fail(uc);
		return;
	}

	switch (selected_boot_mode) {
	case FIREPLACE_BOOT_RECOVERY:
		requested_mode = LK_REBOOT_MODE_RECOVERY;
		break;
	case FIREPLACE_BOOT_DOWNLOAD:
		/*
		 * PARAM download enters GD_DN_MODE_PARAM_DOWNLOAD, which only
		 * displays the reboot cause.  Leave PARAM in normal mode here;
		 * lk_select_download_info_cb() selects the full device-info path
		 * after sbl_check_download() returns.
		 */
		requested_mode = LK_REBOOT_MODE_NONE;
		break;
	case FIREPLACE_BOOT_ANDROID:
	default:
		requested_mode = LK_REBOOT_MODE_NONE;
		break;
	}

	if (reboot_mode == requested_mode) {
		printf("[LK] selected %s boot: reboot mode %" PRIu32
		       " (unchanged)\n",
		       lk_boot_mode_name(selected_boot_mode), requested_mode);
		return;
	}
	err = uc_mem_write(uc, LK_PARAM_REBOOT_MODE_ADDR,
			   &requested_mode, sizeof(requested_mode));
	if (err != UC_ERR_OK) {
		fprintf(stderr,
			"[LK] failed to select %s boot at 0x%016" PRIx64
			": %s\n",
			lk_boot_mode_name(selected_boot_mode),
			LK_PARAM_REBOOT_MODE_ADDR, uc_strerror(err));
		bootchain_fail(uc);
		return;
	}

	printf("[LK] selected %s boot: reboot mode %" PRIu32
	       " -> %" PRIu32 "\n",
	       lk_boot_mode_name(selected_boot_mode), reboot_mode,
	       requested_mode);
}

static void lk_select_download_info_cb(uc_engine *uc, uint64_t address,
				       uint32_t size, void *user_data)
{
	uint64_t mode = LK_DOWNLOAD_MODE_DEVICE_INFO;

	(void)address;
	(void)size;
	(void)user_data;

	if (bootchain_stage() != BOOTCHAIN_STAGE_LK ||
	    selected_boot_mode != FIREPLACE_BOOT_DOWNLOAD)
		return;

	/*
	 * Samsung's mode 4 is the non-key download path that calls
	 * display_ddi_data() and the registered download-log callback.  Mode 2,
	 * selected through PARAM_REBOOT_MODE=1, only shows "by PARAM Download".
	 */
	uc_reg_write(uc, UC_ARM64_REG_X0, &mode);
	printf("[LK] selected download boot: full device-information mode\n");
}

uc_err lk_boot_mode_init(uc_engine *uc, enum fireplace_boot_mode mode)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(lk_select_boot_mode_cb,
				    LK_INIT_PARAM_PART_RETURN),
		BOOTCHAIN_CODE_HOOK(lk_select_download_info_cb,
				    LK_DOWNLOAD_CHECK_RETURN),
	};

	selected_boot_mode = mode;
	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

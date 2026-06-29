
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootrom/bootrom_internal.h"

#define BOOTROM_FUSE_STATE_IMAGE "bootrom-fuses.hex"
#define BOOTROM_KEY_SLOT0_ADDR UINT64_C(0x10003000)
#define OTP_RUNTIME_ROW_1C00_ADDR UINT64_C(0x10001c00)
#define OTP_SECURITY_ROW_1C80_ADDR UINT64_C(0x10001c80)
#define OTP_ENABLE_ROW_1CA0_ADDR UINT64_C(0x10001ca0)
#define OTP_ANTIRBK_NS_AP0_0_ADDR UINT64_C(0x10002400)
#define OTP_ANTIRBK_NS_AP0_1_ADDR UINT64_C(0x10002420)
#define OTP_ANTIRBK_NS_AP0_2_ADDR UINT64_C(0x10002440)
#define OTP_ANTIRBK_NS_AP0_3_ADDR UINT64_C(0x10002460)
#define OTP_POLICY_ROW_3040_ADDR UINT64_C(0x10003040)
#define CHIPID_REVISION_ADDR UINT64_C(0x10000010)
#define SOC_STATUS_ADDR UINT64_C(0x1000b014)
#define SCRATCH_FUSE_ADDR UINT64_C(0x1000b018)
#define SEND_BULK_STATE_ADDR UINT64_C(0x10001000)
#define UID_WORDS_ADDR UINT64_C(0x10008000)
#define FUSE_STATUS_BLOCK_ADDR UINT64_C(0x1000c000)
#define PMU_CFG_ADDR UINT64_C(0x15860404)
#define BOOT_SELECT_STATE_ADDR UINT64_C(0x1586098c)
#define RESET_STATE_ADDR UINT64_C(0x15860000)

struct bootrom_fuse_region {
	const char *name;
	uint64_t address;
	size_t size;
	bool reject_empty;
	bool loaded;
};

static bool auth_context_logged;
static bool secure_mode_logged;
static uint32_t active_auth_context_addr;
static uint64_t secure_boot_mode;

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

static struct bootrom_fuse_region fuse_regions[] = {
	{"chip revision", CHIPID_REVISION_ADDR, 0x04, true, false},
	{"OTP runtime row 0x1c00", OTP_RUNTIME_ROW_1C00_ADDR, 0x04,
	 false, false},
	{"OTP SSS security row 0x1c80", OTP_SECURITY_ROW_1C80_ADDR, 0x04,
	 false, false},
	{"OTP enable row 0x1ca0", OTP_ENABLE_ROW_1CA0_ADDR, 0x04, false,
	 false},
	{"OTP ANTIRBK_NS_AP0 word 0", OTP_ANTIRBK_NS_AP0_0_ADDR, 0x04,
	 false, false},
	{"OTP ANTIRBK_NS_AP0 word 1", OTP_ANTIRBK_NS_AP0_1_ADDR, 0x04,
	 false, false},
	{"OTP ANTIRBK_NS_AP0 word 2", OTP_ANTIRBK_NS_AP0_2_ADDR, 0x04,
	 false, false},
	{"OTP ANTIRBK_NS_AP0 word 3", OTP_ANTIRBK_NS_AP0_3_ADDR, 0x04,
	 false, false},
	{"keyslot contexts", BOOTROM_KEY_SLOT0_ADDR, 0x40, true, false},
	{"OTP policy row 0x3040", OTP_POLICY_ROW_3040_ADDR, 0x04, false,
	 false},
	{"SoC status", SOC_STATUS_ADDR, 0x04, false, false},
	{"scratch/fuse status", SCRATCH_FUSE_ADDR, 0x04, false, false},
	{"BootROM boot state", SEND_BULK_STATE_ADDR, 0x10, false, false},
	{"UID words", UID_WORDS_ADDR, 0x10, true, false},
	{"fuse status block", FUSE_STATUS_BLOCK_ADDR, 0x100, false, false},
	{"PMU config", PMU_CFG_ADDR, 0x04, false, false},
	{"boot select state", BOOT_SELECT_STATE_ADDR, 0x08, false, false},
	{"reset state", RESET_STATE_ADDR, 0x04, false, false},
};

bool bootrom_bytes_are_empty(const uint8_t *data, size_t size)
{
	bool all_zero = true;
	bool all_ff = true;

	for (size_t i = 0; i < size; i++) {
		all_zero = all_zero && data[i] == 0;
		all_ff = all_ff && data[i] == 0xff;
	}
	return all_zero || all_ff;
}

void bootrom_print_missing_fuse_state(const char *reason)
{
	fprintf(stderr, "[BootROM] invalid %s: %s\n",
		BOOTROM_FUSE_STATE_IMAGE, reason);
	fprintf(stderr, "[BootROM] expected local fuse/register regions:\n");
	for (size_t i = 0; i < ARRAY_SIZE(fuse_regions); i++)
		fprintf(stderr, "  0x%08" PRIx64 " 0x%zx  %s\n",
			fuse_regions[i].address, fuse_regions[i].size,
			fuse_regions[i].name);
}

static int hex_digit_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


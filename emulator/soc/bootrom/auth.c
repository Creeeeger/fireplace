
#include <inttypes.h>
#include <stdio.h>

#include "bootrom/bootrom_internal.h"

#define ACTIVE_AUTH_CTX_PTR_ADDR UINT64_C(0x0202006c)
#define SECURE_AUTH_FN_ADDR UINT64_C(0x020200a8)
#define FW_SERVICE_SEND_FN_ADDR UINT64_C(0x020200d0)
#define FW_SERVICE_RECEIVE_FN_ADDR UINT64_C(0x020200dc)
#define HASH_FN_ADDR UINT64_C(0x020200e4)
#define SET_REGISTERS_FN_ADDR UINT64_C(0x020200e8)
#define HASH_MODE_FN_ADDR UINT64_C(0x0202010c)
#define BOOTROM_AUTH_CONTEXT_SIZE 32
#define BOOTROM_KEY_SLOT0_ADDR UINT64_C(0x10003000)
#define BOOTROM_KEY_SLOT1_ADDR UINT64_C(0x10003020)
#define SOC_STATUS_ADDR UINT64_C(0x1000b014)
#define SEND_BULK_STATE_ADDR UINT64_C(0x10001000)

static bool auth_context_logged;
static bool secure_mode_logged;
static uint32_t active_auth_context_addr;
static uint64_t secure_boot_mode;

void bootrom_auth_reset(void)
{
	auth_context_logged = false;
	secure_mode_logged = false;
	active_auth_context_addr = 0;
	secure_boot_mode = 0;
}

static uc_err read_u32(uc_engine *uc, uint64_t address, uint32_t *value)
{
	return uc_mem_read(uc, address, value, sizeof(*value));
}

static uc_err validate_auth_context(uc_engine *uc, uint32_t address)
{
	uint8_t context[BOOTROM_AUTH_CONTEXT_SIZE];
	uc_err err;

	err = uc_mem_read(uc, address, context, sizeof(context));
	if (err != UC_ERR_OK) {
		fprintf(stderr,
			"[BootROM] failed to read auth context at 0x%08"
			PRIx32 ": %s\n",
			address, uc_strerror(err));
		return err;
	}
	if (bootrom_bytes_are_empty(context, sizeof(context))) {
		fprintf(stderr,
			"[BootROM] selected auth context at 0x%08" PRIx32
			" is all-zero/all-0xff\n",
			address);
		return UC_ERR_ARG;
	}
	return UC_ERR_OK;
}

static uc_err select_auth_context(uc_engine *uc, uint32_t *address,
				  uint64_t *mode)
{
	uint32_t send_bulk_state;
	uint32_t soc_status;
	uc_err err;

	err = read_u32(uc, SEND_BULK_STATE_ADDR, &send_bulk_state);
	if (err == UC_ERR_OK)
		err = read_u32(uc, SOC_STATUS_ADDR, &soc_status);
	if (err != UC_ERR_OK)
		return err;
	if (((send_bulk_state >> 8) & 1) == 0 &&
	    ((soc_status >> 31) & 1) == 1) {
		bootrom_print_missing_fuse_state(
			"BootROM selected fallback auth seed; dump "
			"k_rom_fallback_auth_ctx_seed");
		return UC_ERR_ARG;
	}
	if (((send_bulk_state >> 1) & 1) != 0 &&
	    ((send_bulk_state >> 3) & 1) == 0) {
		*address = (uint32_t)BOOTROM_KEY_SLOT1_ADDR;
		*mode = 1;
	} else if ((send_bulk_state & 1) != 0) {
		*address = (uint32_t)BOOTROM_KEY_SLOT0_ADDR;
		*mode = 1;
	} else if ((soc_status & 1) != 0 && ((soc_status >> 3) & 1) == 0) {
		bootrom_print_missing_fuse_state(
			"BootROM selected fallback secure auth seed; dump "
			"k_rom_fallback_auth_ctx_seed");
		return UC_ERR_ARG;
	} else {
		bootrom_print_missing_fuse_state(
			"BootROM selected fallback non-secure auth seed; dump "
			"k_rom_fallback_auth_ctx_seed");
		return UC_ERR_ARG;
	}
	err = validate_auth_context(uc, *address);
	if (err != UC_ERR_OK)
		return err;
	printf("[BootROM] selected keyslot auth context at 0x%08" PRIx32
	       " (send_bulk=0x%08" PRIx32 ", soc_status=0x%08" PRIx32 ")\n",
	       *address, send_bulk_state, soc_status);
	return UC_ERR_OK;
}

uc_err bootrom_auth_install(uc_engine *uc)
{
	uc_err err;

	err = select_auth_context(uc, &active_auth_context_addr,
				  &secure_boot_mode);
	if (err == UC_ERR_OK)
		err = bootchain_write_u32(uc, ACTIVE_AUTH_CTX_PTR_ADDR,
					  active_auth_context_addr);
	if (err == UC_ERR_OK && !auth_context_logged) {
		printf("[BootROM] installed active auth context pointer at 0x%"
		       PRIx64 "\n", ACTIVE_AUTH_CTX_PTR_ADDR);
		auth_context_logged = true;
	}
	return err;
}

uc_err bootrom_auth_install_fwbl1_services(uc_engine *uc)
{
	const uint32_t ret_instruction = UINT32_C(0xd65f03c0);
	const struct {
		uint64_t slot;
		uint32_t stub;
	} callbacks[] = {
		{SECURE_AUTH_FN_ADDR, BOOTROM_STUB_SECURE_AUTH},
		{FW_SERVICE_SEND_FN_ADDR, BOOTROM_STUB_SEND},
		{FW_SERVICE_RECEIVE_FN_ADDR, BOOTROM_STUB_RECEIVE},
		{HASH_FN_ADDR, BOOTROM_STUB_HASH},
		{SET_REGISTERS_FN_ADDR, BOOTROM_STUB_SET_REGS},
		{HASH_MODE_FN_ADDR, BOOTROM_STUB_HASH_MODE},
	};

	for (size_t i = 0; i < ARRAY_SIZE(callbacks); i++) {
		uc_err err = bootchain_write_u32(uc, callbacks[i].slot,
						 callbacks[i].stub);

		if (err == UC_ERR_OK)
			err = bootchain_write_u32(uc, callbacks[i].stub,
						  ret_instruction);
		if (err != UC_ERR_OK)
			return err;
	}
	return UC_ERR_OK;
}

void bootrom_auth_report_mode(uc_engine *uc)
{
	uint32_t auth_context_ptr;
	uc_err err;

	err = uc_mem_read(uc, ACTIVE_AUTH_CTX_PTR_ADDR, &auth_context_ptr,
			  sizeof(auth_context_ptr));
	if (err != UC_ERR_OK) {
		fprintf(stderr,
			"[BootROM] failed to read active auth context pointer: "
			"%s\n",
			uc_strerror(err));
		bootchain_fail(uc);
		return;
	}
	if (auth_context_ptr != active_auth_context_addr) {
		fprintf(stderr,
			"[BootROM] active auth context pointer 0x%08" PRIx32
			" does not match selected context 0x%08" PRIx32
			"\n",
			auth_context_ptr, active_auth_context_addr);
		bootrom_print_missing_fuse_state(
			"BootROM did not publish a valid active auth context");
		bootchain_fail(uc);
		return;
	}
	err = validate_auth_context(uc, auth_context_ptr);
	if (err != UC_ERR_OK) {
		bootchain_fail(uc);
		return;
	}
	if (!secure_mode_logged) {
		printf("[BootROM] FWBL1 secure boot mode uses eFuse auth "
		       "context at 0x%08" PRIx32 "\n",
		       auth_context_ptr);
		secure_mode_logged = true;
	}
	if (uc_reg_write(uc, UC_ARM64_REG_X0, &secure_boot_mode) != UC_ERR_OK)
		bootchain_fail(uc);
}


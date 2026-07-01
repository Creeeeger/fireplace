
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

static void jump_fwbl1_cb(uc_engine *uc, uint64_t address, uint32_t size,
			  void *user_data)
{
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_image_hook_active())
		return;
	err = bootrom_auth_install_fwbl1_services(uc);
	if (err == UC_ERR_OK)
		err = bootchain_write_u32(uc, BOOT_DEVICE_WORD_ADDR,
					  BOOT_DEVICE_UFS_WORD);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "[BootROM] failed to publish FWBL1 services: %s\n",
			uc_strerror(err));
		bootchain_fail(uc);
		return;
	}
	if (!bootchain_transition(uc, BOOTCHAIN_STAGE_BOOTROM,
				  BOOTCHAIN_STAGE_FWBL1))
		return;
	bootchain_request_resume(FWBL1_ENTRY_ADDR);
	printf("[BootROM] FWBL1 handoff reached\n");
	uc_emu_stop(uc);
}

static void auth_ready_cb(uc_engine *uc, uint64_t address, uint32_t size,
			  void *user_data)
{
	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_image_hook_active())
		return;
	if (bootrom_auth_install(uc) != UC_ERR_OK) {
		fprintf(stderr, "[BootROM] failed to install eFuse context\n");
		bootchain_fail(uc);
	}
}

static void print_firmware_console_data(uc_engine *uc)
{
	uint64_t source;
	uint64_t length;
	char buffer[513];

	if (uc_reg_read(uc, UC_ARM64_REG_X0, &source) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X1, &length) != UC_ERR_OK ||
	    length > sizeof(buffer) - 1 ||
	    uc_mem_read(uc, source, buffer, (size_t)length) != UC_ERR_OK) {
		fprintf(stderr, "[%s service] invalid transmit request\n",
			bootchain_active_stage());
		bootchain_fail(uc);
		return;
	}
	if (length == 0)
		return;
	buffer[length] = '\0';
	for (size_t i = 0; i < length; i++) {
		unsigned char character = (unsigned char)buffer[i];

		if (character != '\r' && character != '\n' && character != '\t' &&
		    (character < 0x20 || character > 0x7e))
			buffer[i] = '.';
	}
	printf("[%s service] ", bootchain_active_stage());
	for (size_t i = 0; i < length; i++)
		if (buffer[i] != '\r')
			putchar(buffer[i]);
	if (buffer[length - 1] != '\n' && buffer[length - 1] != '\r')
		putchar('\n');
}

static void console_send_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data)
{
	uint64_t link;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	print_firmware_console_data(uc);
	if (bootchain_failed())
		return;
	if (uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr,
			"[BootROM service] failed to return from transmit\n");
		bootchain_fail(uc);
	}
}

static void math_status_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	uint64_t descriptors;
	uint64_t channel;
	uint32_t registers;
	const uint32_t ready = 7;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	if (uc_reg_read(uc, UC_ARM64_REG_X0, &descriptors) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X1, &channel) != UC_ERR_OK ||
	    uc_mem_read(uc, descriptors + channel * 0x20, &registers,
			sizeof(registers)) != UC_ERR_OK ||
	    uc_mem_write(uc, (uint64_t)registers + 0x10, &ready,
			 sizeof(ready)) != UC_ERR_OK) {
		fprintf(stderr, "[BootROM] failed to complete math status wait\n");
		bootchain_fail(uc);
	}
}

static void panic_cb(uc_engine *uc, uint64_t address, uint32_t size,
		     void *user_data)
{
	uint64_t code = 0;
	uint64_t caller = 0;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	uc_reg_read(uc, UC_ARM64_REG_X1, &code);
	uc_reg_read(uc, UC_ARM64_REG_LR, &caller);
	fprintf(stderr, "[BootROM] panic code=0x%" PRIx64
		" caller=0x%" PRIx64 "\n", code, caller);
}

static void panic_halt_cb(uc_engine *uc, uint64_t address, uint32_t size,
			  void *user_data)
{
	(void)address;
	(void)size;
	(void)user_data;
	if (!bootrom_service_hook_active())
		return;
	bootchain_fail(uc);
}

static void firmware_receive(uc_engine *uc)
{
	uint64_t destination;
	uint64_t length;
	uint64_t result = 1;
	bool handled = false;

	if (uc_reg_read(uc, UC_ARM64_REG_X0, &destination) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X1, &length) != UC_ERR_OK) {
		fprintf(stderr, "[%s] failed to read storage request\n",
			bootchain_active_stage());
		bootchain_fail(uc);
		return;
	}
	if (bootchain_stage() == BOOTCHAIN_STAGE_FWBL1)
		handled = fwbl1_receive_epbl(uc, destination, length);
	else if (bootchain_stage() == BOOTCHAIN_STAGE_EPBL ||
		 bootchain_stage() == BOOTCHAIN_STAGE_BL2)
		handled = epbl_receive_image(uc, destination, length);
	if (!handled || uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK) {
		if (!bootchain_failed())
			fprintf(stderr, "[%s] unsupported storage request\n",
				bootchain_active_stage());
		bootchain_fail(uc);
	}
}

static void firmware_service_cb(uc_engine *uc, uint64_t address, uint32_t size,
				void *user_data)
{
	uint64_t result = 0;

	(void)size;
	(void)user_data;
	switch (address) {
	case BOOTROM_STUB_SECURE_AUTH:
		bootrom_auth_report_mode(uc);
		break;
	case BOOTROM_STUB_SET_REGS:
		break;
	case BOOTROM_STUB_SEND:
		print_firmware_console_data(uc);
		break;
	case BOOTROM_STUB_RECEIVE:
		firmware_receive(uc);
		break;
	case BOOTROM_STUB_HASH:
		fwbl1_hash_sha512(uc);
		break;
	case BOOTROM_STUB_HASH_MODE:
	{
		uint64_t link = 0;

		uc_reg_read(uc, UC_ARM64_REG_LR, &link);
		result = link == UINT64_C(0x02022234) ? 1 : 0;
		if (uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK)
			bootchain_fail(uc);
		break;
	}
	}
}

uc_err bootrom_services_install(uc_engine *uc)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(receive_fwbl1_cb, BOOTROM_BL1_RECEIVE_ADDR),
		BOOTCHAIN_CODE_HOOK(jump_fwbl1_cb, BOOTROM_BL1_JUMP_ADDR),
		BOOTCHAIN_CODE_HOOK(auth_ready_cb, BOOTROM_AUTH_READY_ADDR),
		BOOTCHAIN_CODE_HOOK(console_send_cb, BOOTROM_CONSOLE_SEND_ADDR),
		BOOTCHAIN_CODE_HOOK(math_status_cb, BOOTROM_MATH_STATUS_ADDR),
		BOOTCHAIN_CODE_HOOK(ecdsa_verify_cb, BOOTROM_ECDSA_VERIFY_ADDR),
		BOOTCHAIN_CODE_HOOK(ecdsa_dispatch_cb, BOOTROM_ECDSA_DISPATCH_ADDR),
		BOOTCHAIN_CODE_HOOK(panic_cb, BOOTROM_PANIC_ADDR),
		BOOTCHAIN_CODE_HOOK(panic_halt_cb, BOOTROM_PANIC_HALT_ADDR),
		BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, firmware_service_cb,
				     BOOTROM_STUB_BASE, BOOTROM_STUB_HASH_MODE),
	};

	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

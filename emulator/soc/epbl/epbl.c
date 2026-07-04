
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/bootchain_internal.h"

#define EPBL_BOOT_FLAG_ADDR UINT64_C(0x02031030)
#define EPBL_RELOCATION_ADDR UINT64_C(0x02030000)
#define EPBL_RELOCATION_SIZE 0x2fc0
#define EPBL_VERIFY_STAGE_SIGNATURE_ADDR UINT64_C(0x02028a4c)
#define EPBL_EL3_DECRYPT_ADDR UINT64_C(0x0202a324)

static unsigned char runtime_window[EPBL_RELOCATION_SIZE];
static bool runtime_window_saved;
static uint64_t lower_sp;
static bool smc_in_progress;
static bool bl2_loaded;
static bool lk_loaded;
static bool el3_loaded;
static bool el3_decrypted;
static bool ufs_loaded_later_images_adopted;
static bool smc_logged;
static bool auth_bypass_logged;
static uint64_t last_smc;

bool epbl_route_smc(uc_engine *uc, uint64_t return_address)
{
	const uint32_t mrs_esr_el3 = UINT32_C(0xd53e521e);
	const uint32_t mrs_elr_el3 = UINT32_C(0xd53e4021);
	const uint64_t exception_stack = UINT64_C(0x02032240);
	const uint64_t vector = UINT64_C(0x02030400);
	uint64_t function_id;

	if (bootchain_stage() != BOOTCHAIN_STAGE_BL2)
		return false;
	if (uc_reg_read(uc, UC_ARM64_REG_X0, &function_id) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_SP, &lower_sp) != UC_ERR_OK ||
	    !bootchain_cpu_set_system_register(mrs_esr_el3,
					       UINT64_C(0x17) << 26) ||
	    !bootchain_cpu_set_system_register(mrs_elr_el3, return_address) ||
	    uc_reg_write(uc, UC_ARM64_REG_SP, &exception_stack) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &vector) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] failed to enter BL2 SMC vector\n");
		bootchain_fail(uc);
		return true;
	}
	if (!smc_logged || function_id != last_smc) {
		printf("[SMC] BL2 -> EPBL fid=0x%" PRIx64 "\n", function_id);
		smc_logged = true;
		last_smc = function_id;
	}
	smc_in_progress = true;
	return true;
}

static bool receive_bl2(uc_engine *uc, uint64_t destination, uint64_t length)
{
	uc_err err;

	if (bl2_loaded || destination != BL2_LOAD_ADDR ||
	    length != BL2_IMAGE_SIZE) {
		fprintf(stderr, "[EPBL] unsupported BL2 read destination=0x%" PRIx64
			" size=0x%" PRIx64 "\n", destination, length);
		bootchain_fail(uc);
		return false;
	}
	err = bootchain_load_image(uc, BL2_IMAGE, destination, length);
	if (err != UC_ERR_OK) {
		bootchain_fail(uc);
		return false;
	}
	bl2_loaded = true;
	printf("[EPBL] loaded BL2 at 0x%" PRIx64 "\n", destination);
	return true;
}

static bool receive_later_image(uc_engine *uc, uint64_t destination,
				uint64_t length)
{
	const char *filename;
	const char *name;

	if (destination == LK_LOAD_ADDR && length == LK_IMAGE_SIZE &&
	    !lk_loaded) {
		filename = LK_IMAGE;
		name = "LK";
	} else if (destination == EL3_LOAD_ADDR && length == EL3_IMAGE_SIZE &&
		   !el3_loaded) {
		filename = EL3_IMAGE;
		name = "EL3 monitor";
	} else {
		fprintf(stderr, "[EPBL] unsupported image read destination=0x%"
			PRIx64 " size=0x%" PRIx64 "\n", destination, length);
		bootchain_fail(uc);
		return false;
	}
	if (bootchain_load_image(uc, filename, destination, length) != UC_ERR_OK) {
		bootchain_fail(uc);
		return false;
	}
	if (destination == LK_LOAD_ADDR && lk_apply_runtime_patches(uc) !=
					     UC_ERR_OK) {
		bootchain_fail(uc);
		return false;
	}
	if (destination == LK_LOAD_ADDR)
		lk_loaded = true;
	else
		el3_loaded = true;
	printf("[EPBL] loaded %s at 0x%" PRIx64 "\n", name, destination);
	return true;
}

bool epbl_receive_image(uc_engine *uc, uint64_t destination, uint64_t length)
{
	if (bootchain_stage() == BOOTCHAIN_STAGE_EPBL)
		return receive_bl2(uc, destination, length);
	if (bootchain_stage() == BOOTCHAIN_STAGE_BL2)
		return receive_later_image(uc, destination, length);
	return false;
}

static void wait_status_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	uint64_t status_address;
	uint32_t status;
	int address_register;

	(void)size;
	(void)user_data;
	if (address == UINT64_C(0x0202e2dc)) {
		address_register = UC_ARM64_REG_X9;
		status = 1;
	} else if (address == UINT64_C(0x0202e308) ||
		   address == UINT64_C(0x0202e328)) {
		address_register = UC_ARM64_REG_X6;
		status = 3;
	} else {
		address_register = UC_ARM64_REG_X6;
		status = UINT32_C(0x45);
	}
	if (uc_reg_read(uc, address_register, &status_address) != UC_ERR_OK ||
	    uc_mem_write(uc, status_address, &status, sizeof(status)) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] failed to complete PLL status wait\n");
		bootchain_fail(uc);
	}
}

static void context_relocation_cb(uc_engine *uc, uint64_t address,
				  uint32_t size, void *user_data)
{
	const uint32_t boot_bl2 = 1;

	(void)size;
	(void)user_data;
	if (address == UINT64_C(0x020276d0)) {
		if (uc_mem_read(uc, EPBL_RELOCATION_ADDR, runtime_window,
				sizeof(runtime_window)) != UC_ERR_OK) {
			fprintf(stderr, "[EPBL] failed to save runtime window\n");
			bootchain_fail(uc);
			return;
		}
		runtime_window_saved = true;
		return;
	}
	if (!runtime_window_saved ||
	    uc_mem_write(uc, EPBL_RELOCATION_ADDR, runtime_window,
			 sizeof(runtime_window)) != UC_ERR_OK ||
	    uc_mem_write(uc, EPBL_BOOT_FLAG_ADDR, &boot_bl2,
			 sizeof(boot_bl2)) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] failed to restore runtime window\n");
		bootchain_fail(uc);
	}
}

static void eret_cb(uc_engine *uc, uint64_t address, uint32_t size,
		    void *user_data)
{
	const uint32_t msr_elr_el3 = UINT32_C(0xd51e4031);
	uint64_t return_address;

	(void)address;
	(void)size;
	(void)user_data;
	if (!bootchain_cpu_get_system_register(msr_elr_el3, &return_address) ||
	    return_address < BL2_LOAD_ADDR ||
	    return_address >= BL2_LOAD_ADDR + BL2_IMAGE_SIZE) {
		fprintf(stderr, "[EPBL] invalid exception-return target\n");
		bootchain_fail(uc);
		return;
	}
	if (smc_in_progress &&
	    uc_reg_write(uc, UC_ARM64_REG_SP, &lower_sp) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] failed to restore BL2 stack\n");
		bootchain_fail(uc);
		return;
	}
	smc_in_progress = false;
	bootchain_request_resume(return_address);
	uc_emu_stop(uc);
}

static void auth_bypass_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	uint64_t link;
	uint64_t result = 0;

	(void)address;
	(void)size;
	(void)user_data;
	if (!auth_bypass_logged) {
		fprintf(stderr,
			"[EPBL] stage signature verification forced success "
			"temporarily\n");
		auth_bypass_logged = true;
	}
	if (uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] failed to bypass stage authentication\n");
		bootchain_fail(uc);
	}
}

static void el3_decrypt_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	uint64_t link;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() == BOOTCHAIN_STAGE_BL2 &&
	    !ufs_loaded_later_images_adopted) {
		if (!lk_loaded && lk_apply_runtime_patches(uc) != UC_ERR_OK) {
			fprintf(stderr,
				"[EPBL] failed to patch UFS-loaded LK image\n");
			bootchain_fail(uc);
			return;
		}
		lk_loaded = true;
		el3_loaded = true;
		ufs_loaded_later_images_adopted = true;
		printf("[EPBL] adopted LK/EL3 loaded through UFS\n");
	}
	if (bootchain_stage() != BOOTCHAIN_STAGE_BL2 || !el3_loaded ||
	    !lk_loaded || el3_decrypted ||
	    bootchain_decrypt_el3_image(uc) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr, "[EPBL] invalid EL3 decryption request\n");
		bootchain_fail(uc);
		return;
	}
	el3_decrypted = true;
	printf("[EPBL] EL3 monitor decrypted in-place\n");
}

static void el3_handoff_cb(uc_engine *uc, uint64_t address, uint32_t size,
			   void *user_data)
{
	(void)size;
	(void)user_data;
	if (!lk_loaded || !el3_loaded || !el3_decrypted ||
	    !bootchain_transition(uc, BOOTCHAIN_STAGE_BL2,
				  BOOTCHAIN_STAGE_EL3)) {
		if (!bootchain_failed()) {
			fprintf(stderr, "[BL2] invalid EL3 handoff state\n");
			bootchain_fail(uc);
		}
		return;
	}
	printf("[BL2] EL3 monitor handoff reached at 0x%" PRIx64 "\n", address);
}

uc_err epbl_init(uc_engine *uc)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e2dc)),
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e308)),
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e328)),
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e38c)),
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e3a8)),
		BOOTCHAIN_CODE_HOOK(wait_status_cb, UINT64_C(0x0202e3c4)),
		BOOTCHAIN_CODE_HOOK(context_relocation_cb,
				    UINT64_C(0x020276d0)),
		BOOTCHAIN_CODE_HOOK(context_relocation_cb,
				    UINT64_C(0x020260e8)),
		BOOTCHAIN_CODE_HOOK(eret_cb, UINT64_C(0x0202e090)),
		BOOTCHAIN_CODE_HOOK(eret_cb, UINT64_C(0x02027484)),
		BOOTCHAIN_CODE_HOOK(auth_bypass_cb,
				    EPBL_VERIFY_STAGE_SIGNATURE_ADDR),
		BOOTCHAIN_CODE_HOOK(el3_decrypt_cb, EPBL_EL3_DECRYPT_ADDR),
		BOOTCHAIN_CODE_HOOK(el3_handoff_cb, EL3_ENTRY_ADDR),
	};

	runtime_window_saved = false;
	smc_in_progress = false;
	bl2_loaded = false;
	lk_loaded = false;
	el3_loaded = false;
	el3_decrypted = false;
	ufs_loaded_later_images_adopted = false;
	smc_logged = false;
	auth_bypass_logged = false;
	last_smc = 0;
	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

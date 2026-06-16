
#include <stdio.h>

#include "bootchain/bootchain_internal.h"

static enum bootchain_stage current_stage;
static bool completed;
static bool failed;
static bool resume_requested;
static uint64_t resume_address;

enum bootchain_stage bootchain_stage(void)
{
	return current_stage;
}

bool bootchain_transition(uc_engine *uc, enum bootchain_stage expected,
			  enum bootchain_stage next)
{
	if (current_stage == expected) {
		uc_err err = bootchain_cpu_prepare_stage(uc, next);

		if (err != UC_ERR_OK) {
			fprintf(stderr,
				"Failed to prepare CPU hooks for boot stage: %s\n",
				uc_strerror(err));
			bootchain_fail(uc);
			return false;
		}
		current_stage = next;
		return true;
	}
	fprintf(stderr, "Invalid bootchain transition from %s\n",
		bootchain_active_stage());
	bootchain_fail(uc);
	return false;
}

void bootchain_fail(uc_engine *uc)
{
	failed = true;
	uc_emu_stop(uc);
}

void bootchain_mark_complete(uc_engine *uc)
{
	completed = true;
	uc_emu_stop(uc);
}

bool bootchain_complete(void)
{
	return completed;
}

bool bootchain_failed(void)
{
	return failed;
}

const char *bootchain_active_stage(void)
{
	switch (current_stage) {
	case BOOTCHAIN_STAGE_BOOTROM:
		return "BootROM";
	case BOOTCHAIN_STAGE_FWBL1:
		return "FWBL1";
	case BOOTCHAIN_STAGE_EPBL:
		return "EPBL";
	case BOOTCHAIN_STAGE_BL2:
		return "BL2";
	case BOOTCHAIN_STAGE_EL3:
		return "EL3";
	case BOOTCHAIN_STAGE_LK:
		return "LK";
	case BOOTCHAIN_STAGE_KERNEL:
		return "Kernel";
	}
	return "unknown";
}

void bootchain_request_resume(uint64_t address)
{
	resume_address = address;
	resume_requested = true;
}

bool bootchain_take_resume_request(uint64_t *address)
{
	bool requested = resume_requested;

	if (requested)
		*address = resume_address;
	resume_requested = false;
	return requested;
}

bool bootchain_handle_system_instruction(uc_engine *uc)
{
	return bootchain_cpu_handle_system_instruction(uc);
}

bool bootchain_handle_invalid_memory(uc_engine *uc, uint64_t address)
{
	if (bootchain_cpu_handle_invalid_memory(uc, address))
		return true;
	return el3_mon_handle_invalid_memory(uc, address);
}

bool bootchain_route_smc(uc_engine *uc, uint64_t return_address)
{
	if (current_stage == BOOTCHAIN_STAGE_BL2)
		return epbl_route_smc(uc, return_address);
	if (current_stage == BOOTCHAIN_STAGE_EL3)
		return el3_mon_route_runtime_smc(uc, return_address);
	if (current_stage == BOOTCHAIN_STAGE_LK)
		return el3_mon_route_smc(uc, return_address);
	return false;
}

bool bootchain_route_hvc(uc_engine *uc, uint64_t return_address,
			 uint16_t immediate)
{
	if (current_stage == BOOTCHAIN_STAGE_LK)
		return el3_mon_route_hvc(uc, return_address, immediate);
	return false;
}

bool bootchain_route_svc(uc_engine *uc, uint64_t return_address,
                         uint16_t immediate)
{
	if (current_stage == BOOTCHAIN_STAGE_EL3)
		return el3_mon_route_svc(uc, return_address, immediate);
	return false;
}

bool bootchain_route_undefined_instruction(uc_engine *uc,
					   uint64_t fault_address)
{
	if (current_stage == BOOTCHAIN_STAGE_EL3)
		return el3_mon_route_undefined_instruction(uc, fault_address);
	return false;
}

uc_err bootchain_write_u32(uc_engine *uc, uint64_t address, uint32_t value)
{
	return uc_mem_write(uc, address, &value, sizeof(value));
}

uc_err bootchain_init(uc_engine *uc, const struct bootchain_config *config)
{
	uc_err err;

	if (!config || !config->image_directory ||
	    config->image_directory[0] == '\0')
		return UC_ERR_ARG;
	if (!config->lun_directory || config->lun_directory[0] == '\0')
		return UC_ERR_ARG;
	bootchain_images_configure(config->image_directory,
				   config->lun_directory);
	current_stage = BOOTCHAIN_STAGE_BOOTROM;
	completed = false;
	failed = false;
	resume_requested = false;
	bootchain_cpu_reset();
	err = bootchain_images_validate();
	if (err == UC_ERR_OK)
		err = bootchain_load_image(uc, BOOTROM_IMAGE, 0,
					   BOOTROM_IMAGE_SIZE);
	if (err == UC_ERR_OK)
		err = bootrom_init(uc);
	if (err == UC_ERR_OK)
		err = fwbl1_init(uc);
	if (err == UC_ERR_OK)
		err = epbl_init(uc);
	if (err == UC_ERR_OK)
		err = bl2_init(uc);
	if (err == UC_ERR_OK)
		err = el3_mon_init(uc);
	if (err == UC_ERR_OK)
		err = lk_init(uc, config->headless, config->boot_mode);
	return err;
}

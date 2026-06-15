
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


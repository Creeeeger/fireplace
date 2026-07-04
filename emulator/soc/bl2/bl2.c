
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/bootchain_internal.h"

#define BL2_FATAL_ADDR UINT64_C(0x15607770)
#define BL2_BUSY_DELAY_ADDR UINT64_C(0x15605928)
#define BL2_EYE_VALIDATION_ADDR UINT64_C(0x1560abb0)
#define BL2_DCA_TRAINING_FATAL_ADDR UINT64_C(0x1560c734)
#define BL2_DCA_TRAINING_CONTINUE_ADDR UINT64_C(0x1560c73c)

static bool eye_compatibility_logged;
static bool dca_compatibility_logged;
static bool busy_delay_logged;

static void entry_cb(uc_engine *uc, uint64_t address, uint32_t size,
		     void *user_data)
{
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_EPBL)
		return;
	if (!bootchain_transition(uc, BOOTCHAIN_STAGE_EPBL,
				  BOOTCHAIN_STAGE_BL2))
		return;
	printf("[EPBL] BL2 handoff reached at 0x%" PRIx64 "\n", address);
}

static void eye_validation_cb(uc_engine *uc, uint64_t address, uint32_t size,
			      void *user_data)
{
	uint64_t link;
	uint32_t success = 0;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_BL2)
		return;
	if (uc_reg_read(uc, UC_ARM64_REG_LR, &link) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_W0, &success) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_PC, &link) != UC_ERR_OK) {
		fprintf(stderr, "[BL2] failed to return idealized DRAM eye\n");
		bootchain_fail(uc);
		return;
	}
	if (!eye_compatibility_logged) {
		printf("[BL2] using idealized DRAM eye results\n");
		eye_compatibility_logged = true;
	}
}

static void dca_training_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data)
{
	uint64_t pc = BL2_DCA_TRAINING_CONTINUE_ADDR;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_BL2)
		return;
	if (uc_reg_write(uc, UC_ARM64_REG_PC, &pc) != UC_ERR_OK) {
		fprintf(stderr, "[BL2] failed to bypass DCA training failure\n");
		bootchain_fail(uc);
		return;
	}
	if (!dca_compatibility_logged) {
		printf("[BL2] bypassing DCA training fatal path\n");
		dca_compatibility_logged = true;
	}
}



#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/dmc/dmc.h>
#include <fireplace/soc/peripherals.h>

static uint8_t mode_registers[4][4][128];
static bool mode_registers_initialized;

static unsigned int dmc_index(uint64_t base)
{
	return (unsigned int)((base - DMC0_BASE) / 0x100000);
}

int dmc_init(struct uc_struct *uc)
{
	unsigned int controller;

	if (!mode_registers_initialized) {
		/* G986B profile: Samsung LPDDR4X, two 12-Gbit ranks per channel. */
		for (controller = 0; controller < 4; controller++) {
			unsigned int rank;

			for (rank = 1; rank <= 2; rank++) {
				mode_registers[controller][rank][5] = 0x01;
				mode_registers[controller][rank][6] = 0x07;
				mode_registers[controller][rank][7] = 0x00;
				mode_registers[controller][rank][8] = 0x14;
			}
		}
		mode_registers_initialized = true;
	}
	return 0;
}

void dmc_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	struct peripheral *dmc = user_data;
	uint32_t status;
	bool is_phy = (dmc->base & 0xfffff) == 0x50000;

	if (type == UC_MEM_WRITE && !is_phy &&
	    address == dmc->base + DMC_COMMAND) {
		uint32_t selector = 0;
		uint32_t data = 0;
		unsigned int controller = dmc_index(dmc->base);
		unsigned int rank;
		unsigned int mr;

		uc_mem_read(uc, dmc->base + DMC_MODE_REGISTER,
			    &selector, sizeof(selector));
		rank = (selector >> 28) & 3;
		mr = (selector >> 20) & 0x7f;
		if ((uint32_t)value == DMC_COMMAND_MRW) {
			uc_mem_read(uc, dmc->base + DMC_MODE_DATA,
				    &data, sizeof(data));
			mode_registers[controller][rank][mr] = (uint8_t)data;
		} else if ((uint32_t)value == DMC_COMMAND_MRR) {
			status = mode_registers[controller][rank][mr];
			uc_mem_write(uc, dmc->base + DMC_MODE_RESULT,
				     &status, sizeof(status));
		}
		return;
	}

	if (type != UC_MEM_READ)
		return;

	if (!is_phy && address == dmc->base + DMC_COMMAND) {
		status = 0;
		uc_mem_write(uc, address, &status, sizeof(status));
	} else if (!is_phy && address == dmc->base + DMC_DFI_INIT) {
		if (uc_mem_read(uc, address, &status, sizeof(status)) != UC_ERR_OK)
			return;

		status |= DMC_DFI_INIT_COMPLETE;
		uc_mem_write(uc, address, &status, sizeof(status));
		printf("%s: DFI initialization complete\n", dmc->name);
	} else if (is_phy &&
		   address == dmc->base + DMC_PHY_PRESET_STATUS) {
		status = DMC_PHY_PRESET_COMPLETE;
		uc_mem_write(uc, address, &status, sizeof(status));
		printf("%s: preset complete\n", dmc->name);
	} else if (is_phy &&
		   address == dmc->base + DMC_PHY_DLL_STATUS) {
		if (uc_mem_read(uc, address, &status, sizeof(status)) != UC_ERR_OK)
			return;
		status |= DMC_PHY_DLL_LOCKED;
		uc_mem_write(uc, address, &status, sizeof(status));
	} else if (is_phy &&
		   address == dmc->base + DMC_PHY_TRAINING_CONTROL) {
		if (uc_mem_read(uc, address, &status, sizeof(status)) != UC_ERR_OK)
			return;
		status |= DMC_PHY_TRAINING_COMPLETE;
		uc_mem_write(uc, address, &status, sizeof(status));
	} else if (is_phy &&
		   address == dmc->base + DMC_PHY_PRBS_STATUS) {
		if (uc_mem_read(uc, address, &status, sizeof(status)) != UC_ERR_OK)
			return;
		status |= DMC_PHY_PRBS_COMPLETE;
		uc_mem_write(uc, address, &status, sizeof(status));
	}
}

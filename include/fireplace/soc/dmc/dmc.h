
#ifndef FIREPLACE_DMC_H
#define FIREPLACE_DMC_H

#include <stdint.h>

#include <unicorn/unicorn.h>

#define DMC0_BASE 0x1bc36000
#define DMC1_BASE 0x1bd36000
#define DMC2_BASE 0x1be36000
#define DMC3_BASE 0x1bf36000
#define DMC_PHY0_BASE 0x1bc50000
#define DMC_PHY1_BASE 0x1bd50000
#define DMC_PHY2_BASE 0x1be50000
#define DMC_PHY3_BASE 0x1bf50000
#define DMC_SIZE 0x1000
#define DMC_COMMAND 0x04
#define DMC_MODE_REGISTER 0x00
#define DMC_MODE_DATA 0x08
#define DMC_MODE_RESULT 0x0c
#define DMC_COMMAND_MRR 0x01
#define DMC_COMMAND_MRW 0x10
#define DMC_DFI_INIT 0x28
#define DMC_DFI_INIT_START (1U << 0)
#define DMC_DFI_INIT_COMPLETE (1U << 1)
#define DMC_PHY_PRESET_STATUS 0x3cc
#define DMC_PHY_PRESET_COMPLETE (1U << 0)
#define DMC_PHY_DLL_STATUS 0xb4
#define DMC_PHY_DLL_LOCKED (1U << 18)
#define DMC_PHY_TRAINING_CONTROL 0xa24
#define DMC_PHY_TRAINING_COMPLETE (1U << 1)
#define DMC_PHY_PRBS_STATUS 0x684
#define DMC_PHY_PRBS_COMPLETE (1U << 0)

int dmc_init(struct uc_struct *uc);
void dmc_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);

#endif

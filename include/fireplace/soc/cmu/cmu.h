
#ifndef FIREPLACE_CMU_H
#define FIREPLACE_CMU_H

#include <unicorn/unicorn.h>

#define CMU_MIF_BASE 0x1a330000ULL
#define CMU_MCSC_BASE 0x18c00000ULL
#define CMU_MIF_SIZE 0x2000
#define CMU_MIF_PLL_CON0 0x14c
#define CMU_MIF_BOOTROM_PLL_STATUS 0x18c
#define CMU_MIF_BOOTROM_GATE_STATUS 0x180
#define CMU_PLL_LOCKED (1U << 29)

int cmu_init(struct uc_struct *uc);
void cmu_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);

#endif

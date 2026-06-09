
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/cmu/cmu.h>
#include <fireplace/soc/peripherals.h>

int cmu_init(struct uc_struct *uc)
{
	return 0;
}

void cmu_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	struct peripheral *cmu = user_data;
	uint32_t status;

	if (type != UC_MEM_READ)
		return;
	if (uc_mem_read(uc, address, &status, sizeof(status)) != UC_ERR_OK)
		return;

	if (address == cmu->base + CMU_MIF_PLL_CON0) {
		status |= CMU_PLL_LOCKED;
	} else if (cmu->base == CMU_MIF_BASE &&
		   address == CMU_MIF_BASE + CMU_MIF_BOOTROM_PLL_STATUS) {
		status |= CMU_PLL_LOCKED;
	} else if (cmu->base == CMU_MIF_BASE &&
		   address == CMU_MIF_BASE + CMU_MIF_BOOTROM_GATE_STATUS) {
		status &= ~UINT32_C(0x10000);
	} else {
		return;
	}
	uc_mem_write(uc, address, &status, sizeof(status));
}

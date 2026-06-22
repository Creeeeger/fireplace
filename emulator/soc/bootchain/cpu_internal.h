
#ifndef FIREPLACE_SOC_BOOTCHAIN_CPU_INTERNAL_H
#define FIREPLACE_SOC_BOOTCHAIN_CPU_INTERNAL_H

#include "bootchain/bootchain_internal.h"

#define MRS_SCTLR_EL1 UINT32_C(0xd5381000)
#define MRS_TTBR0_EL1 UINT32_C(0xd5382000)
#define MRS_TTBR1_EL1 UINT32_C(0xd5382020)
#define MRS_TCR_EL1 UINT32_C(0xd5382040)
#define MRS_PAR_EL1 UINT32_C(0xd5387400)
#define MRS_CURRENTEL UINT32_C(0xd5384240)
#define MRS_SP_EL0 UINT32_C(0xd5384100)

#define AT_S1E1R UINT32_C(0xd5087800)
#define AT_S1E1W UINT32_C(0xd5087820)
#define AT_S12E1R UINT32_C(0xd50c7880)
#define AT_S12E1W UINT32_C(0xd50c78a0)

#define AARCH64_DESC_ADDR_MASK UINT64_C(0x0000fffffffff000)
#define PAR_EL1_F UINT64_C(1)
#define PAR_EL1_FST_SHIFT 1

uc_arm64_reg cpu_x_register(unsigned int index);
void cpu_system_reset(void);
void cpu_mmu_reset(void);
void cpu_runtime_reset(void);
bool cpu_mmu_translate_el1(uc_engine *uc, uint64_t va, bool write,
			   uint64_t *pa, unsigned int *fault_status);
bool cpu_mmu_invalidate_aliases(uc_engine *uc);
bool cpu_mmu_flush_pending_writes(uc_engine *uc);
void cpu_mmu_write_cb(uc_engine *uc, uc_mem_type type, uint64_t address,
		      int size, int64_t value, void *user_data);
bool cpu_is_lse_ldadd_instruction(uint32_t instruction);
bool cpu_emulate_lse_ldadd(uc_engine *uc, uint64_t address,
			   uint32_t instruction);
bool el3_mon_secure_os_active(void);

#endif

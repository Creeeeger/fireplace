
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/cpu_internal.h"

static bool runtime_hooks_installed;
static bool kernel_write_hook_installed;

static bool is_ic_ivau_instruction(uint32_t instruction)
{
	return (instruction & UINT32_C(0xffffffe0)) ==
	       UINT32_C(0xd50b7520);
}

static const char *address_translation_name(uint32_t instruction)
{
    switch (instruction & UINT32_C(0xffffffe0)) {
    case AT_S1E1R:
        return "S1E1R";
    case AT_S1E1W:
        return "S1E1W";
    case AT_S12E1R:
        return "S12E1R";
    case AT_S12E1W:
        return "S12E1W";
    default:
        return NULL;
    }
}

static bool emulate_address_translation(uc_engine *uc, uint64_t address,
                                        uint32_t instruction)
{
    const char *name = address_translation_name(instruction);
    uint32_t operation = instruction & UINT32_C(0xffffffe0);
    unsigned int source_index = instruction & 31;
    uint64_t va = 0;
    uint64_t pa = 0;
    uint64_t par;
    unsigned int fault_status = 0;
    bool write;
    bool success;

    if (!name)
        return false;
    if (source_index != 31 &&
        uc_reg_read(uc, cpu_x_register(source_index), &va) != UC_ERR_OK)
        return false;

    write = operation == AT_S1E1W || operation == AT_S12E1W;
    success = cpu_mmu_translate_el1(uc, va, write, &pa, &fault_status);

    /*
     * Fireplace does not model an EL2 stage-2 translation regime yet.
     * H-Arx leaves it disabled during this boot path, so S12 operations
     * have the same final PA as the completed EL1 stage-1 walk.
     */
    par = success ? (pa & AARCH64_DESC_ADDR_MASK) :
          PAR_EL1_F | ((uint64_t)fault_status << PAR_EL1_FST_SHIFT);
    if (!bootchain_cpu_set_system_register(MRS_PAR_EL1, par))
        return false;

    return true;
}

static bool is_virtualized_system_register(enum bootchain_stage stage,
					   uint32_t instruction)
{
	/* Bit 21 selects MRS versus MSR. The remaining encoding identifies the
	 * register, so one entry covers both directions without a giant boolean
	 * expression in the per-instruction callback. */
	static const uint32_t read_write_registers[] = {
		UINT32_C(0xd5181000), /* SCTLR_EL1 */
		UINT32_C(0xd5182000), /* TTBR0_EL1 */
		UINT32_C(0xd5182020), /* TTBR1_EL1 */
		UINT32_C(0xd5182040), /* TCR_EL1 */
		UINT32_C(0xd5187400), /* PAR_EL1 */
		UINT32_C(0xd5185200), /* ESR_EL1 */
		UINT32_C(0xd5184000), /* SPSR_EL1 */
		UINT32_C(0xd5184020), /* ELR_EL1 */
		UINT32_C(0xd51bd060), /* TPIDRRO_EL0 */
		UINT32_C(0xd51c4000), /* SPSR_EL2 */
		UINT32_C(0xd51c4020), /* ELR_EL2 */
		UINT32_C(0xd51c5200), /* ESR_EL2 */
		UINT32_C(0xd518d080), /* implementation-defined register */
	};
	uint32_t operation = instruction & UINT32_C(0xffffffe0);
	uint32_t normalized = operation & ~UINT32_C(0x00200000);

	if (operation == MRS_CURRENTEL || operation == UINT32_C(0xd53800a0))
		return true;
	if (stage == BOOTCHAIN_STAGE_EL3 && el3_mon_secure_os_active() &&
	    normalized == UINT32_C(0xd5184100))
		return true;
	for (size_t i = 0; i < ARRAY_SIZE(read_write_registers); i++)
		if (normalized == read_write_registers[i])
			return true;
	return false;
}


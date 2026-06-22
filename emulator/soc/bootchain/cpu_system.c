
#include <stdio.h>

#include "bootchain/cpu_internal.h"

struct system_register {
	uint32_t crn;
	uint32_t crm;
	uint32_t op0;
	uint32_t op1;
	uint32_t op2;
	uint64_t value;
};

#define SYSTEM_REGISTER_CAPACITY 256
static struct system_register system_registers[SYSTEM_REGISTER_CAPACITY];
static size_t system_register_count;

static struct system_register *find_system_register(const uc_arm64_cp_reg *reg)
{
	struct system_register *entry;

	for (size_t i = 0; i < system_register_count; i++) {
		entry = &system_registers[i];
		if (entry->crn == reg->crn && entry->crm == reg->crm &&
		    entry->op0 == reg->op0 && entry->op1 == reg->op1 &&
		    entry->op2 == reg->op2)
			return entry;
	}
	if (system_register_count == ARRAY_SIZE(system_registers)) {
		fprintf(stderr,
			"[CPU] system register table full while adding "
			"op0=%u op1=%u crn=%u crm=%u op2=%u\n",
			reg->op0, reg->op1, reg->crn, reg->crm, reg->op2);
		return NULL;
	}
	entry = &system_registers[system_register_count++];
	entry->crn = reg->crn;
	entry->crm = reg->crm;
	entry->op0 = reg->op0;
	entry->op1 = reg->op1;
	entry->op2 = reg->op2;
	entry->value = 0;

    /* MPIDR_EL1: boot CPU, Aff0-Aff3 zero, no SMT. */
	if (reg->op0 == 3 && reg->op1 == 0 && reg->crn == 0 &&
	    reg->crm == 0 && reg->op2 == 5) {
	    entry->value = UINT64_C(0x81000000);
	}

	/* FWBL1 subtracts one before iterating this implementation-defined count. */
	if (reg->op0 == 3 && reg->op1 == 0 && reg->crn == 5 &&
	    reg->crm == 3 && reg->op2 == 0)
		entry->value = 1;
	return entry;
}

static uc_arm64_cp_reg decode_system_register(uint32_t instruction)
{
	uc_arm64_cp_reg reg = {
		.crn = instruction >> 12 & 15,
		.crm = instruction >> 8 & 15,
		.op0 = instruction >> 19 & 3,
		.op1 = instruction >> 16 & 7,
		.op2 = instruction >> 5 & 7,
	};

	return reg;
}

uc_arm64_reg cpu_x_register(unsigned int index)
{
	if (index < 29)
		return (uc_arm64_reg)(UC_ARM64_REG_X0 + index);
	if (index == 29)
		return UC_ARM64_REG_X29;
	if (index == 30)
		return UC_ARM64_REG_X30;
	return UC_ARM64_REG_XZR;
}

static bool is_system_maintenance_instruction(uint32_t instruction)
{
    uc_arm64_cp_reg reg;

    /*
     * Preserve the explicitly handled implementation-specific
     * maintenance instructions.
     */
    if (instruction == UINT32_C(0xd50344ff) ||
        instruction == UINT32_C(0xd50e871f) ||
        instruction == UINT32_C(0xd508751f)) /* IC IALLU */
        return true;

    /*
     * Register-form cache maintenance.
     *
     * Match the operation while ignoring Rt in bits [4:0].
     *
     * Do not accept all CRn=7 instructions here. CRn=7 also contains
     * AT address-translation operations, which must produce PAR_EL1 and
     * cannot be treated as no-ops.
     */
    switch (instruction & UINT32_C(0xffffffe0)) {
    case UINT32_C(0xd50b7520): /* IC IVAU, Xt */
    case UINT32_C(0xd5087620): /* DC IVAC, Xt */
    case UINT32_C(0xd50b7a20): /* DC CVAC, Xt */
    case UINT32_C(0xd50b7b20): /* DC CVAU, Xt */
    case UINT32_C(0xd50b7e20): /* DC CIVAC, Xt */
        return true;
    default:
        break;
    }

    /*
     * CRn=8 SYS instructions are TLB invalidation operations.
     * The emulator's shadow/identity mapping model handles translation
     * explicitly, so architectural TLB invalidations can be no-ops.
     */
    if ((instruction & UINT32_C(0xfff00000)) !=
        UINT32_C(0xd5000000))
        return false;

    reg = decode_system_register(instruction);
    return reg.crn == 8;
}

static bool is_ic_ivau_instruction(uint32_t instruction)
{
    return (instruction & UINT32_C(0xffffffe0)) ==
           UINT32_C(0xd50b7520);
}

static bool is_tlb_invalidation_instruction(uint32_t instruction)
{
    uc_arm64_cp_reg reg;

    if ((instruction & UINT32_C(0xfff00000)) !=
        UINT32_C(0xd5000000))
        return false;

    reg = decode_system_register(instruction);
    return reg.crn == 8;
}

static bool is_stage1_control_register(uint32_t instruction)
{
    switch (instruction & UINT32_C(0xffffffe0)) {
    case UINT32_C(0xd5181000): /* SCTLR_EL1 */
    case UINT32_C(0xd5182000): /* TTBR0_EL1 */
    case UINT32_C(0xd5182020): /* TTBR1_EL1 */
    case UINT32_C(0xd5182040): /* TCR_EL1 */
        return true;
    default:
        return false;
    }
}

void cpu_system_reset(void)
{
	system_register_count = 0;
}

bool bootchain_cpu_set_system_register(uint32_t instruction, uint64_t value)
{
	uc_arm64_cp_reg reg = decode_system_register(instruction);
	struct system_register *entry = find_system_register(&reg);

	if (!entry)
		return false;
	entry->value = value;
	return true;
}

bool bootchain_cpu_get_system_register(uint32_t instruction, uint64_t *value)
{
	uc_arm64_cp_reg reg = decode_system_register(instruction);
	struct system_register *entry = find_system_register(&reg);

	if (!entry)
		return false;
	*value = entry->value;
	return true;
}

static bool bootchain_cpu_execute_system_instruction(uc_engine *uc,
						    uint32_t instruction,
						    uint64_t next_pc)
{
	uc_arm64_cp_reg reg;
	struct system_register *entry;
	unsigned int general_register;
	bool read;

	/*
	 * Unicorn's hidden exception level does not follow PSTATE values written
	 * by the software EL3/HVC dispatcher.  MRS CurrentEL would consequently
	 * report EL0 after returning to LK even though PSTATE.M is EL1h, causing
	 * LK's final mmu_disable() assertion to panic.  CurrentEL is architecturally
	 * the current PSTATE exception level in bits [3:2], so derive it from the
	 * same visible PSTATE that the dispatcher saves and restores.
	 */
	if ((instruction & UINT32_C(0xffffffe0)) == MRS_CURRENTEL) {
		uint64_t pstate = 0;
		uint64_t current_el;
		unsigned int destination = instruction & 31;

		if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK)
			return false;
		current_el = pstate & UINT64_C(0xc);
		if (destination != 31 &&
		    uc_reg_write(uc, cpu_x_register(destination), &current_el) !=
			UC_ERR_OK)
			return false;
		return uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) == UC_ERR_OK;
	}

	/*
	 * Unicorn does not keep SPSel and the ordinary SP bank coherent.
	 * Preserve the architectural bank selection in PSTATE.M[0] and the
	 * selected SP_EL0/SP_ELx value through the ordinary SP register.
	 */
	if (instruction == UINT32_C(0xd50040bf) ||
	    instruction == UINT32_C(0xd50041bf) ||
	    (instruction & UINT32_C(0xffffffe0)) ==
		UINT32_C(0xd5184200)) {
		uint64_t pstate = 0;
		uint64_t current_sp = 0;
		uint64_t target_sp = 0;
		uint64_t select = 0;
		unsigned int current_el;
		unsigned int general_register = instruction & 31;
		uc_arm64_reg current_bank;
		uc_arm64_reg target_bank;

		if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK ||
		    uc_reg_read(uc, UC_ARM64_REG_SP, &current_sp) != UC_ERR_OK)
			return false;
		if (instruction == UINT32_C(0xd50041bf)) {
			select = 1;
		} else if ((instruction & UINT32_C(0xffffffe0)) ==
			   UINT32_C(0xd5184200) &&
			   general_register != 31 &&
			   uc_reg_read(uc, cpu_x_register(general_register), &select) !=
				UC_ERR_OK) {
			return false;
		}
		select &= UINT64_C(1);
		if ((pstate & UINT64_C(1)) == select)
			return uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) ==
			       UC_ERR_OK;

		current_el = (unsigned int)((pstate >> 2) & UINT64_C(3));
		if (current_el == 0)
			return false;
		current_bank = (pstate & UINT64_C(1)) == 0 ?
			UC_ARM64_REG_SP_EL0 :
			(uc_arm64_reg)(UC_ARM64_REG_SP_EL0 + current_el);
		target_bank = select == 0 ?
			UC_ARM64_REG_SP_EL0 :
			(uc_arm64_reg)(UC_ARM64_REG_SP_EL0 + current_el);

		if (uc_reg_write(uc, current_bank, &current_sp) != UC_ERR_OK ||
		    uc_reg_read(uc, target_bank, &target_sp) != UC_ERR_OK)
			return false;
		pstate = (pstate & ~UINT64_C(1)) | select;
		return uc_reg_write(uc, UC_ARM64_REG_PSTATE, &pstate) == UC_ERR_OK &&
		       uc_reg_write(uc, UC_ARM64_REG_SP, &target_sp) == UC_ERR_OK &&
		       uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) == UC_ERR_OK;
	}

	if ((instruction & UINT32_C(0xffffffe0)) ==
	    UINT32_C(0xd5384200)) {
		uint64_t pstate = 0;
		uint64_t select;
		unsigned int general_register = instruction & 31;

		if (uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate) != UC_ERR_OK)
			return false;
		select = pstate & UINT64_C(1);
		if (general_register != 31 &&
		    uc_reg_write(uc, cpu_x_register(general_register), &select) !=
			UC_ERR_OK)
			return false;
		return uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) == UC_ERR_OK;
	}

    if (is_system_maintenance_instruction(instruction)) {
		if (bootchain_stage() == BOOTCHAIN_STAGE_KERNEL &&
		    is_tlb_invalidation_instruction(instruction) &&
		    !cpu_mmu_invalidate_aliases(uc))
			return false;
		return uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) ==
		       UC_ERR_OK;
	}
	if ((instruction & UINT32_C(0xfff00000)) == UINT32_C(0xd5300000))
		read = true;
	else if ((instruction & UINT32_C(0xfff00000)) == UINT32_C(0xd5100000))
		read = false;
	else
		return false;

	reg = decode_system_register(instruction);
	entry = find_system_register(&reg);
	general_register = instruction & 31;
	if (!entry)
		return false;
	if (read) {
		uint64_t value = entry->value;

		if (general_register != 31 &&
		    uc_reg_write(uc, cpu_x_register(general_register), &value) !=
			UC_ERR_OK)
			return false;
	} else {
		uint64_t value = 0;

		/* MSR ..., XZR architecturally writes zero. */
		if (general_register != 31 &&
		    uc_reg_read(uc, cpu_x_register(general_register), &value) !=
			UC_ERR_OK) {
			return false;
		}
		if (bootchain_stage() == BOOTCHAIN_STAGE_KERNEL &&
		    value != entry->value &&
		    is_stage1_control_register(instruction) &&
		    !cpu_mmu_invalidate_aliases(uc))
			return false;
		entry->value = value;
	}

	return uc_reg_write(uc, UC_ARM64_REG_PC, &next_pc) == UC_ERR_OK;
}

bool bootchain_cpu_handle_system_instruction(uc_engine *uc)
{
	enum bootchain_stage stage = bootchain_stage();
	uint64_t pc;
	uint32_t instruction;

	if (uc_reg_read(uc, UC_ARM64_REG_PC, &pc) != UC_ERR_OK)
		return false;
	if (!(stage == BOOTCHAIN_STAGE_BOOTROM && pc < UINT64_C(0x20000)) &&
	    stage != BOOTCHAIN_STAGE_EPBL &&
	    !(stage == BOOTCHAIN_STAGE_BL2 && pc >= EPBL_LOAD_ADDR &&
	      pc < UINT64_C(0x02033000)) &&
	    stage != BOOTCHAIN_STAGE_EL3 && stage != BOOTCHAIN_STAGE_LK &&
	    stage != BOOTCHAIN_STAGE_KERNEL)
		return false;
	if (uc_mem_read(uc, pc, &instruction, sizeof(instruction)) ==
		UC_ERR_OK &&
	    bootchain_cpu_execute_system_instruction(uc, instruction, pc + 4))
		return true;
	if (pc < 4 ||
	    uc_mem_read(uc, pc - 4, &instruction, sizeof(instruction)) !=
		UC_ERR_OK)
		return false;
	return bootchain_cpu_execute_system_instruction(uc, instruction, pc);
}

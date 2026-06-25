
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

static void instruction_cb(uc_engine *uc, uint64_t address, uint32_t size,
                           void *user_data)
{
    enum bootchain_stage stage = bootchain_stage();
    uint32_t instruction;
    bool translated_instruction = false;

    (void)size;
    (void)user_data;

    /* The compatibility decoder is needed only once EL3 starts. Keeping the
     * hook dormant during BootROM, FWBL1, EPBL, and BL2 avoids a guest-memory
     * read for every instruction in the early boot chain. */
    if (stage != BOOTCHAIN_STAGE_EL3 &&
        stage != BOOTCHAIN_STAGE_LK &&
        stage != BOOTCHAIN_STAGE_KERNEL)
        return;

    if (stage == BOOTCHAIN_STAGE_KERNEL &&
        !cpu_mmu_flush_pending_writes(uc)) {
        fprintf(stderr, "[Kernel MMU] failed to publish wide store\n");
        bootchain_fail(uc);
        return;
    }

    /*
     * uc_mem_read() addresses Unicorn's flat backing store; it does not
     * follow the SecureOS EL0 task's current TTBR0 mapping.  The same low
     * virtual address is reused by multiple handler tasks, so its identity
     * shadow can be stale after a context switch.  Decode through the live
     * page tables before looking for SVC and other trapped instructions.
     */
    if (stage == BOOTCHAIN_STAGE_EL3 &&
        el3_mon_secure_os_active()) {
        translated_instruction =
            el3_mon_read_secure_os_instruction(
                uc, address, &instruction);
    }

    if (!translated_instruction &&
        uc_mem_read(uc, address, &instruction,
                    sizeof(instruction)) != UC_ERR_OK)
        return;

    /*
     * Route SMC instructions from LK or the SecureOS runtime represented
     * by BOOTCHAIN_STAGE_EL3.
     */
    if ((stage == BOOTCHAIN_STAGE_LK ||
         stage == BOOTCHAIN_STAGE_EL3) &&
        instruction == UINT32_C(0xd4000003) &&
        bootchain_route_smc(uc, address + 4)) {
        return;
    }

    if (stage == BOOTCHAIN_STAGE_LK &&
        (instruction & UINT32_C(0xffe0001f)) ==
            UINT32_C(0xd4000002) &&
        bootchain_route_hvc(
            uc, address + 4,
            (uint16_t)((instruction >> 5) & UINT32_C(0xffff)))) {
        return;
    }

    if (stage == BOOTCHAIN_STAGE_EL3 &&
        (instruction & UINT32_C(0xffe0001f)) ==
            UINT32_C(0xd4000001) &&
        bootchain_route_svc(
            uc, address + 4,
            (uint16_t)((instruction >> 5) & UINT32_C(0xffff)))) {
        return;
    }

    if (stage != BOOTCHAIN_STAGE_LK &&
        stage != BOOTCHAIN_STAGE_KERNEL &&
        stage != BOOTCHAIN_STAGE_EL3)
        return;

    /*
     * Unicorn raises an undefined-instruction exception for the ARMv8.1
     * LSE atomics used by the H-Arx plug-in.  Execute the architectural
     * read-modify-write before Unicorn enters that exception path.
     */
    if (stage == BOOTCHAIN_STAGE_EL3 &&
        cpu_is_lse_ldadd_instruction(instruction)) {
        if (!cpu_emulate_lse_ldadd(uc, address, instruction)) {
            fprintf(stderr,
                    "[CPU] failed to emulate LSE LDADD at 0x%" PRIx64
                    " instruction=0x%08" PRIx32 "\n",
                    address, instruction);
            bootchain_fail(uc);
            return;
        }

        bootchain_request_resume(address + 4);
        uc_emu_stop(uc);
        return;
    }

    /*
     * Unicorn does not implement the AT instructions used by H-Arx/UH.
     * Walk the live EL1 translation tables and publish the architectural
     * result through PAR_EL1 before the following MRS executes.
     */
    if (address_translation_name(instruction) != NULL) {
        if (!emulate_address_translation(uc, address, instruction)) {
            fprintf(stderr,
                    "[CPU hook] failed to emulate AT at 0x%" PRIx64
                    " instruction=0x%08" PRIx32 "\n",
                    address, instruction);
            bootchain_fail(uc);
            return;
        }

        bootchain_request_resume(address + 4);
        uc_emu_stop(uc);
        return;
    }

    /*
     * Unicorn executes SPSel without keeping the ordinary SP and the
     * selected SP_EL0/SP_ELx bank coherent.  Intercept both immediate
     * and register forms before Unicorn executes them so nested EL3
     * frames survive a temporary stack-bank switch.
     */
    if (stage == BOOTCHAIN_STAGE_EL3 &&
        (instruction == UINT32_C(0xd50040bf) ||
         instruction == UINT32_C(0xd50041bf) ||
         (instruction & UINT32_C(0xffffffe0)) ==
             UINT32_C(0xd5184200) ||
         (instruction & UINT32_C(0xffffffe0)) ==
             UINT32_C(0xd5384200))) {
        if (!bootchain_cpu_handle_system_instruction(uc)) {
            fprintf(stderr,
                    "[CPU] failed to virtualize SPSel at 0x%" PRIx64
                    "\n",
                    address);
            bootchain_fail(uc);
            return;
        }

        bootchain_request_resume(address + 4);
        uc_emu_stop(uc);
        return;
    }

    if (stage == BOOTCHAIN_STAGE_EL3 &&
        el3_mon_secure_os_active() &&
        is_ic_ivau_instruction(instruction)) {
        const uint32_t nop = UINT32_C(0xd503201f);
        uc_err err;

        err = uc_mem_write(uc, address, &nop, sizeof(nop));
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[CPU] failed to patch SecureOS IC IVAU "
                    "at 0x%" PRIx64 ": %s\n",
                    address, uc_strerror(err));
            bootchain_fail(uc);
            return;
        }

        err = uc_ctl_flush_tb(uc);
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "[CPU] failed to flush TB after patching "
                    "SecureOS IC IVAU at 0x%" PRIx64 ": %s\n",
                    address, uc_strerror(err));
            bootchain_fail(uc);
            return;
        }

        /*
         * Restart at the same address. The instruction there is now NOP.
         */
        bootchain_request_resume(address);
        uc_emu_stop(uc);
        return;
    }

    if (is_virtualized_system_register(stage, instruction)) {
        if (!bootchain_cpu_handle_system_instruction(uc)) {
            bootchain_fail(uc);
            return;
        }

        bootchain_request_resume(address + 4);
        uc_emu_stop(uc);
        return;
    }
}


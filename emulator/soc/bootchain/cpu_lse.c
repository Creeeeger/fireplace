
#include <inttypes.h>
#include <stdio.h>

#include "bootchain/cpu_internal.h"

bool cpu_is_lse_ldadd_instruction(uint32_t instruction)
{
    /*
     * LSE atomic LDADD{A,L,AL}, all operand widths.  Bits [31:30]
     * select the width, bits [23:22] select acquire/release ordering,
     * and Rs/Rn/Rt are deliberately excluded from the mask.
     */
    return (instruction & UINT32_C(0x3f20fc00)) ==
           UINT32_C(0x38200000);
}

bool cpu_emulate_lse_ldadd(uc_engine *uc, uint64_t address,
                              uint32_t instruction)
{
    unsigned int source_index = (instruction >> 16) & 31;
    unsigned int base_index = (instruction >> 5) & 31;
    unsigned int result_index = instruction & 31;
    unsigned int width = 1U << ((instruction >> 30) & 3);
    uint64_t source = 0;
    uint64_t base = 0;
    uint64_t old_value = 0;
    uint64_t new_value;
    uint64_t mask;
    uc_err err;

    if (source_index != 31 &&
        uc_reg_read(uc, cpu_x_register(source_index), &source) != UC_ERR_OK)
        return false;
    if (base_index == 31) {
        if (uc_reg_read(uc, UC_ARM64_REG_SP, &base) != UC_ERR_OK)
            return false;
    } else if (uc_reg_read(uc, cpu_x_register(base_index), &base) != UC_ERR_OK) {
        return false;
    }

    err = uc_mem_read(uc, base, &old_value, width);
    if (err != UC_ERR_OK) {
        fprintf(stderr,
                "[CPU] LDADD read failed at 0x%" PRIx64
                " address=0x%" PRIx64 " width=%u: %s\n",
                address, base, width, uc_strerror(err));
        return false;
    }

    mask = width == sizeof(uint64_t) ? UINT64_MAX :
           (UINT64_C(1) << (width * 8)) - 1;
    source &= mask;
    old_value &= mask;
    new_value = (old_value + source) & mask;
    err = uc_mem_write(uc, base, &new_value, width);
    if (err != UC_ERR_OK) {
        fprintf(stderr,
                "[CPU] LDADD write failed at 0x%" PRIx64
                " address=0x%" PRIx64 " width=%u: %s\n",
                address, base, width, uc_strerror(err));
        return false;
    }

    /* Sub-64-bit LSE operations write a W register and zero-extend. */
    if (result_index != 31 &&
        uc_reg_write(uc, cpu_x_register(result_index), &old_value) != UC_ERR_OK)
        return false;

    return true;
}


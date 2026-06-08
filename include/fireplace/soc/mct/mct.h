
#ifndef FIREPLACE_MCT_H
#define FIREPLACE_MCT_H

#include <stdint.h>

#include <unicorn/unicorn.h>

#define MCT_BASE 0x10040000
#define MCT_SIZE 0x1000
#define MCT_FRC_LOW 0x100
#define MCT_FRC_HIGH 0x104
#define MCT_FREQUENCY_HZ 26000000ULL
#define MCT_LOCAL_BASE 0x106f0000
#define MCT_LOCAL_SIZE 0x1000
#define MCT_LOCAL_TIMER0_CUR_RAW 0x14

int mct_init(struct uc_struct *uc);
void mct_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);

#endif

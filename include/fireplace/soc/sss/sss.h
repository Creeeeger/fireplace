
#ifndef FIREPLACE_SSS_H
#define FIREPLACE_SSS_H

#include <stdbool.h>
#include <stddef.h>

#include <unicorn/unicorn.h>

#define SSS_BASE 0x1a520000ULL
#define SSS_SIZE 0x5000

int sss_init(struct uc_struct *uc);
void sss_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);
bool sss_calculate_rpmb_hmac(const void *data, size_t length,
			     unsigned char output[32]);

#endif

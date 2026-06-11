
#ifndef FIREPLACE_OTP_H
#define FIREPLACE_OTP_H

#include <stdint.h>

#include <unicorn/unicorn.h>

#define OTP_CON_TOP_BASE UINT64_C(0x10000000)
#define OTP_CON_TOP_SIZE UINT64_C(0x1000)

int otp_init(struct uc_struct *uc);
void otp_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);

#endif

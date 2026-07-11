
#ifndef FIREPLACE_SOC_LK_DEBUG_H
#define FIREPLACE_SOC_LK_DEBUG_H

#include <stdint.h>

#include <unicorn/unicorn.h>

void lk_printf_cb(uc_engine *uc, uint64_t address, uint32_t size,
		  void *user_data);

#endif

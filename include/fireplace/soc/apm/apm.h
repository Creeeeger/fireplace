
#ifndef FIREPLACE_APM_H
#define FIREPLACE_APM_H

#include <stdint.h>

#include <unicorn/unicorn.h>

#define APM_BASE 0x15900000
#define APM_SIZE 0x1000
#define APM_MAILBOX_RESPONSE (APM_BASE + 0x88)
#define APM_READY_STATUS (APM_BASE + 0x8c)
#define APM_DOORBELL (APM_BASE + 0x08)
#define APM_INTERRUPT_STATUS (APM_BASE + 0x28)
#define APM_SHARED_SRAM_OFFSET 0x3000
#define APM_READY_MAGIC 0x1234abcd

int apm_init(struct uc_struct *uc);
void apm_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data);

#endif

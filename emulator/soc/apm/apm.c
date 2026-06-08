
#include <stdint.h>
#include <stdio.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/apm/apm.h>

int apm_init(struct uc_struct *uc)
{
	printf("= apm_init\n");
	return 0;
}

void apm_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	uint32_t response;

	if (type == UC_MEM_WRITE && address == APM_DOORBELL) {
		uint32_t request_base = 0;
		uint32_t response_base = 0;
		uint32_t ring_count = 0;
		uint32_t stride = 0;
		uint32_t request_producer = 0;
		uint32_t response_consumer = 0;
		uint32_t message[4] = {0};
		uint32_t request_index;

		uc_mem_read(uc, 0x15615098, &request_base,
			    sizeof(request_base));
		uc_mem_read(uc, 0x1561509c, &response_base,
			    sizeof(response_base));
		uc_mem_read(uc, 0x156150a0, &ring_count, sizeof(ring_count));
		uc_mem_read(uc, 0x156150a4, &stride, sizeof(stride));
		if (request_base && response_base && ring_count && stride) {
			uc_mem_read(uc, request_base - 4, &request_producer,
				    sizeof(request_producer));
			uc_mem_read(uc, response_base - 8, &response_consumer,
				    sizeof(response_consumer));
			request_index = (request_producer + ring_count - 1) %
				ring_count;
			uc_mem_read(uc, request_base + request_index * stride,
				    message, sizeof(message));
			uc_mem_write(uc,
				     response_base + response_consumer * stride,
				     message, sizeof(message));
		}
		response = 1;
		uc_mem_write(uc, APM_INTERRUPT_STATUS, &response,
			     sizeof(response));
		return;
	}

	if (type != UC_MEM_READ)
		return;

	if (address == APM_MAILBOX_RESPONSE) {
		/*
		 * BL2 copied the APM payload to 0x0203c000. The mailbox protocol
		 * returns its offset from the shared SRAM base at 0x02039000.
		 */
		response = APM_SHARED_SRAM_OFFSET;
		uc_mem_write(uc, address, &response, sizeof(response));
		printf("APM mailbox response: shared SRAM offset 0x%x\n",
		       response);
	} else if (address == APM_READY_STATUS) {
		response = APM_READY_MAGIC;
		uc_mem_write(uc, address, &response, sizeof(response));
		printf("APM ready status: 0x%x\n", response);
	} else if (address == APM_INTERRUPT_STATUS) {
		response = 1;
		uc_mem_write(uc, address, &response, sizeof(response));
	}
}

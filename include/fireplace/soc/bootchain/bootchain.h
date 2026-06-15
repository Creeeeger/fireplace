
#ifndef FIREPLACE_SOC_BOOTCHAIN_H
#define FIREPLACE_SOC_BOOTCHAIN_H

#include <stdbool.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <fireplace/core/emulator.h>

struct bootchain_config {
	const char *image_directory;
	const char *lun_directory;
	enum fireplace_boot_mode boot_mode;
	bool headless;
};

uc_err bootchain_init(uc_engine *uc, const struct bootchain_config *config);
bool bootchain_complete(void);
bool bootchain_failed(void);
const char *bootchain_active_stage(void);
bool bootchain_take_resume_request(uint64_t *address);
bool bootchain_handle_system_instruction(uc_engine *uc);
bool bootchain_route_smc(uc_engine *uc, uint64_t return_address);
bool bootchain_route_hvc(uc_engine *uc, uint64_t return_address,
                         uint16_t immediate);
bool bootchain_route_svc(uc_engine *uc, uint64_t return_address,
                         uint16_t immediate);
bool bootchain_route_undefined_instruction(uc_engine *uc,
                                           uint64_t fault_address);
bool bootchain_handle_invalid_memory(uc_engine *uc, uint64_t address);

#endif


#ifndef FIREPLACE_SOC_BOOTCHAIN_INTERNAL_H
#define FIREPLACE_SOC_BOOTCHAIN_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/bootchain/bootchain.h>

enum bootchain_stage {
	BOOTCHAIN_STAGE_BOOTROM,
	BOOTCHAIN_STAGE_FWBL1,
	BOOTCHAIN_STAGE_EPBL,
	BOOTCHAIN_STAGE_BL2,
	BOOTCHAIN_STAGE_EL3,
	BOOTCHAIN_STAGE_LK,
	BOOTCHAIN_STAGE_KERNEL,
};

struct bootchain_hook {
	const char *name;
	int type;
	void *callback;
	void *user_data;
	uint64_t begin;
	uint64_t end;
};

#define BOOTCHAIN_HOOK_RANGE(hook_type, hook_callback, first, last) \
	{ \
		.name = #hook_callback, \
		.type = (hook_type), \
		.callback = (void *)(hook_callback), \
		.begin = (first), \
		.end = (last), \
	}

#define BOOTCHAIN_CODE_HOOK(hook_callback, address) \
	BOOTCHAIN_HOOK_RANGE(UC_HOOK_CODE, hook_callback, address, address)

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define BOOTROM_IMAGE "bootrom.bin"
#define EFUSE_IMAGE "crecker.efuse"
#define FWBL1_IMAGE "fwbl1"
#define EPBL_IMAGE "epbl"
#define BL2_IMAGE "bl2"
#define EL3_IMAGE "el3-monitor"
#define LK_IMAGE "lk"

#define BOOTROM_IMAGE_SIZE UINT64_C(131072)

#define FWBL1_LOAD_ADDR UINT64_C(0x02022000)
#define FWBL1_ENTRY_ADDR (FWBL1_LOAD_ADDR + 0x10)
#define FWBL1_IMAGE_SIZE UINT64_C(12288)

#define EPBL_LOAD_ADDR UINT64_C(0x02026000)
#define EPBL_ENTRY_ADDR (EPBL_LOAD_ADDR + 0x10)
#define EPBL_IMAGE_SIZE UINT64_C(77824)

#define BL2_LOAD_ADDR UINT64_C(0x15600000)
#define BL2_IMAGE_SIZE UINT64_C(442368)

#define EL3_LOAD_ADDR UINT64_C(0xbfe80000)
#define EL3_ENTRY_ADDR (EL3_LOAD_ADDR + 0x20)
#define EL3_IMAGE_SIZE UINT64_C(262144)

#define LK_LOAD_ADDR UINT64_C(0xe8000000)
#define LK_IMAGE_SIZE UINT64_C(2621440)

enum bootchain_stage bootchain_stage(void);
bool bootchain_transition(uc_engine *uc, enum bootchain_stage expected,
			  enum bootchain_stage next);
uc_err bootchain_install_hooks(uc_engine *uc,
			       const struct bootchain_hook *hooks, size_t count);
bool bootchain_return_to_link(uc_engine *uc);
void bootchain_fail(uc_engine *uc);
void bootchain_mark_complete(uc_engine *uc);
void bootchain_request_resume(uint64_t address);
uc_err bootchain_load_image(uc_engine *uc, const char *filename,
			    uint64_t address, uint64_t expected_size);
void bootchain_images_configure(const char *profile_directory,
				const char *lun_directory);
uc_err bootchain_images_validate(void);
uc_err bootchain_read_profile_file(const char *filename, char **data,
				   size_t *size);
uc_err bootchain_decrypt_epbl_image(uc_engine *uc);
uc_err bootchain_decrypt_el3_image(uc_engine *uc);
uc_err bootchain_write_u32(uc_engine *uc, uint64_t address, uint32_t value);
void bootchain_cpu_reset(void);
uc_err bootchain_cpu_prepare_stage(uc_engine *uc,
				   enum bootchain_stage stage);
bool bootchain_cpu_handle_system_instruction(uc_engine *uc);
bool bootchain_cpu_handle_invalid_memory(uc_engine *uc, uint64_t address);
bool bootchain_cpu_set_system_register(uint32_t instruction, uint64_t value);
bool bootchain_cpu_get_system_register(uint32_t instruction, uint64_t *value);

uc_err bootrom_init(uc_engine *uc);
uc_err fwbl1_init(uc_engine *uc);
bool fwbl1_receive_epbl(uc_engine *uc, uint64_t destination,
			uint64_t capacity);
void fwbl1_hash_sha512(uc_engine *uc);
bool epbl_receive_image(uc_engine *uc, uint64_t destination, uint64_t length);
uc_err epbl_init(uc_engine *uc);
bool epbl_route_smc(uc_engine *uc, uint64_t return_address);
uc_err bl2_init(uc_engine *uc);
uc_err el3_mon_init(uc_engine *uc);
bool el3_mon_route_smc(uc_engine *uc, uint64_t return_address);
bool el3_mon_route_runtime_smc(uc_engine *uc, uint64_t return_address);
bool el3_mon_route_hvc(uc_engine *uc, uint64_t return_address,
                       uint16_t immediate);
bool el3_mon_route_svc(uc_engine *uc, uint64_t return_address,
                       uint16_t immediate);
bool el3_mon_route_undefined_instruction(uc_engine *uc,
                                         uint64_t fault_address);
bool el3_mon_read_secure_os_instruction(uc_engine *uc, uint64_t address,
                                        uint32_t *instruction);
bool el3_mon_handle_invalid_memory(uc_engine *uc, uint64_t address);
bool el3_mon_bootrom_service_active(void);
uc_err lk_init(uc_engine *uc, bool headless,
	       enum fireplace_boot_mode boot_mode);
uc_err lk_apply_runtime_patches(uc_engine *uc);

#endif

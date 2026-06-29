
#include "bootrom/bootrom_internal.h"

#define BL1_SIZE_ADDR UINT64_C(0x02020030)
#define BOOT_DEVICE_WORD_ADDR UINT64_C(0x02020064)
#define BOOT_DEVICE_UFS_WORD UINT32_C(0xcb000011)

uc_err bootrom_init(uc_engine *uc)
{
	const uint32_t bl1_size = UINT32_C(0x3000);
	uc_err err;

	bootrom_auth_reset();
	err = bootrom_fuses_load(uc);
	if (err == UC_ERR_OK)
		err = bootrom_auth_install(uc);
	if (err == UC_ERR_OK)
		err = bootchain_write_u32(uc, BL1_SIZE_ADDR, bl1_size);
	if (err == UC_ERR_OK)
		err = bootchain_write_u32(uc, BOOT_DEVICE_WORD_ADDR,
					  BOOT_DEVICE_UFS_WORD);
	if (err == UC_ERR_OK)
		err = bootrom_services_install(uc);
	return err;
}

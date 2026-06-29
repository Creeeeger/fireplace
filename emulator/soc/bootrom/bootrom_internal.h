
#ifndef FIREPLACE_SOC_BOOTROM_INTERNAL_H
#define FIREPLACE_SOC_BOOTROM_INTERNAL_H

#include "bootchain/bootchain_internal.h"

#define BOOTROM_STUB_BASE UINT64_C(0x0201f000)
#define BOOTROM_STUB_SECURE_AUTH (BOOTROM_STUB_BASE + 0x00)
#define BOOTROM_STUB_SEND (BOOTROM_STUB_BASE + 0x04)
#define BOOTROM_STUB_RECEIVE (BOOTROM_STUB_BASE + 0x08)
#define BOOTROM_STUB_HASH (BOOTROM_STUB_BASE + 0x0c)
#define BOOTROM_STUB_SET_REGS (BOOTROM_STUB_BASE + 0x10)
#define BOOTROM_STUB_HASH_MODE (BOOTROM_STUB_BASE + 0x14)

bool bootrom_bytes_are_empty(const uint8_t *data, size_t size);
void bootrom_print_missing_fuse_state(const char *reason);
uc_err bootrom_fuses_load(uc_engine *uc);
void bootrom_auth_reset(void);
uc_err bootrom_auth_install(uc_engine *uc);
uc_err bootrom_auth_install_fwbl1_services(uc_engine *uc);
void bootrom_auth_report_mode(uc_engine *uc);
uc_err bootrom_services_install(uc_engine *uc);

#endif


#ifndef FIREPLACE_SOC_LK_INTERNAL_H
#define FIREPLACE_SOC_LK_INTERNAL_H

#include "bootchain/bootchain_internal.h"

uc_err lk_boot_mode_init(uc_engine *uc, enum fireplace_boot_mode mode);
uc_err lk_display_init(uc_engine *uc);
uc_err lk_odin_init(uc_engine *uc, const char *sboot_path);
void lk_patches_configure(bool headless);

#endif

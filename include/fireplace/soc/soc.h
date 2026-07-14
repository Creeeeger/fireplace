/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FIREPLACE_SOC_H
#define FIREPLACE_SOC_H

#include <stdbool.h>
#include <stdint.h>
#include <unicorn/unicorn.h>

#include <fireplace/core/emulator.h>

struct soc_boot_config {
	const char *bootchain_directory;
	const char *lun_directory;
	enum fireplace_boot_mode boot_mode;
	bool headless;
};

int soc_peripherals_init(uc_engine *uc, const struct soc_boot_config *config);
bool soc_bootchain_complete(void);
bool soc_bootchain_failed(void);
const char *soc_active_firmware_stage(void);
bool soc_take_resume_request(uint64_t *address);

#endif

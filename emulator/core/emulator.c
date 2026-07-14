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

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include <unicorn/unicorn.h>

#include <fireplace/core/emulator.h>
#include <fireplace/soc/memmap.h>
#include <fireplace/soc/soc.h>

#define BOOTCHAIN_DIRECTORY FIREPLACE_SOURCE_DIR "/bootchain/G986B"
#define BOOTROM_END UINT64_C(0x20000)

atomic_int sharedState = 0;

int emulator_run(const struct fireplace_emulator_options *options)
{
	struct fireplace_emulator_options defaults = {0};
	struct soc_boot_config boot_config = {0};
	uc_engine *uc = NULL;
	uint64_t start = 0;
	uint64_t sp = UINT64_C(0x02021400);
	uint64_t pstate = UINT64_C(0x3cd);
	uc_err err;
	int result = EXIT_FAILURE;

	if (!options)
		options = &defaults;
	if (!options->lun_directory || options->lun_directory[0] == '\0') {
		fprintf(stderr, "--lun-dir PATH is required for UFS boot\n");
		return EXIT_FAILURE;
	}
	boot_config.bootchain_directory = BOOTCHAIN_DIRECTORY;
	boot_config.lun_directory = options->lun_directory;
	boot_config.boot_mode = options->boot_mode;
	boot_config.headless = options->headless;
	printf("== Emulator starting ==\n");
	printf("Boot media: ufs\n");
	printf("LUN dumps: %s\n", options->lun_directory);
	printf("Boot mode: %s\n",
	       options->boot_mode == FIREPLACE_BOOT_RECOVERY ? "recovery" :
	       options->boot_mode == FIREPLACE_BOOT_DOWNLOAD ? "download" :
	       "android");
	printf("Bootchain support files: %s\n", BOOTCHAIN_DIRECTORY);

	err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to open Unicorn: %s\n", uc_strerror(err));
		return EXIT_FAILURE;
	}
	if (memmap_soc(uc, MEMORY_12GB) != UC_ERR_OK) {
		fprintf(stderr, "Failed to map SoC memory\n");
		goto out;
	}
	err = uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_PSTATE, &pstate);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to initialize CPU state: %s\n",
			uc_strerror(err));
		goto out;
	}
	err = (uc_err)soc_peripherals_init(uc, &boot_config);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to initialize SoC: %s\n", uc_strerror(err));
		goto out;
	}

	atomic_store(&sharedState, STATE_RUNNING);
	for (;;) {
		err = uc_emu_start(uc, start, BOOTROM_END, 0, 0);
		if (err != UC_ERR_OK) {
			fprintf(stderr, "Emulator failure: %s\n", uc_strerror(err));
			break;
		}
		if (soc_bootchain_failed())
			break;
		if (soc_bootchain_complete()) {
			result = EXIT_SUCCESS;
			break;
		}
		if (soc_take_resume_request(&start))
			continue;
		fprintf(stderr, "Emulation stopped before the LK milestone\n");
		break;
	}

out:
	atomic_store(&sharedState,
		     result == EXIT_SUCCESS ? STATE_OFF : STATE_CRASHED);
	err = uc_close(uc);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to close Unicorn: %s\n", uc_strerror(err));
		result = EXIT_FAILURE;
	}
	return result;
}

void *emulator_thread_main(void *arg)
{
	return (void *)(intptr_t)emulator_run(arg);
}

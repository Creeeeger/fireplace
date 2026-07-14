/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FIREPLACE_EMULATOR_H
#define FIREPLACE_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

enum fireplace_boot_mode {
	FIREPLACE_BOOT_ANDROID = 0,
	FIREPLACE_BOOT_RECOVERY,
	FIREPLACE_BOOT_DOWNLOAD,
};

struct fireplace_emulator_options {
	const char *lun_directory;
	enum fireplace_boot_mode boot_mode;
	bool headless;
};

int emulator_run(const struct fireplace_emulator_options *options);
void *emulator_thread_main(void *arg);

typedef enum {
	STATE_OFF,
	STATE_RUNNING,
	STATE_CRASHED
} state;

#endif
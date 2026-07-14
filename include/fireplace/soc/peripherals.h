/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FIREPLACE_PERIPHERALS_H
#define FIREPLACE_PERIPHERALS_H

#include <stdint.h>

#include <unicorn/unicorn.h>

struct peripheral {
	const char *name;
	uint64_t base;
	uint64_t size;
	int (*init)(uc_engine *uc);
	uc_cb_hookmem_t access;
};

#endif
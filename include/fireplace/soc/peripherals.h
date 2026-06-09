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

#include <stdbool.h>

#include <unicorn/unicorn.h>

struct peripheral
{
	/* Let's be pretty. */
	char* name;

	/* Do we need a callback? */
	bool hook;

	union { uint64_t addressBase; uint64_t base; };
	union { uint64_t addressSize; uint64_t size; };

	/* This one is required. Called on soc init. Never again. */
	int (*peri_init)(struct uc_struct*);

	/* This one is not. Called on every write to the address range */
	uc_cb_hookmem_t peri_hook;

	// TODO: Need to add a hook for when it's trying to read

	uc_hook hh;
};

#endif
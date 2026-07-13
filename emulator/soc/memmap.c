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

#include <inttypes.h>
#include <stdio.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/fb/fb.h>
#include <fireplace/soc/memmap.h>

/* This sparse host map mirrors the firmware's early identity map. Device
 * behavior is supplied by MMIO hooks; the broad regions merely give Unicorn
 * backing storage for registers and RAM the firmware expects to touch. */
static const struct memory_mapping exynos990_12gb_memory[] = {
	{UINT64_C(0x00000000), UINT64_C(0x00100000), UC_PROT_ALL},
	{UINT64_C(0x02000000), UINT64_C(0x00200000), UC_PROT_ALL},
	{UINT64_C(0x03000000), UINT64_C(0x00200000), UC_PROT_READ},
	{UINT64_C(0x04000000), UINT64_C(0x00200000), UC_PROT_READ},
	{UINT64_C(0x06000000), UINT64_C(0x0a000000), UC_PROT_READ},
	{UINT64_C(0x10000000), UINT64_C(0x10000000), UC_PROT_ALL},
	{UINT64_C(0x80000000), UINT64_C(0x79800000), UC_PROT_ALL},
	{UINT64_C(0xf9800000), UINT64_C(0x03c00000), UC_PROT_ALL},
	{UINT64_C(0xfd400000), UINT64_C(0x00500000), UC_PROT_ALL},
	{UINT64_C(0xfd900000), UINT64_C(0x00200000), UC_PROT_ALL},
	{UINT64_C(0xfdb00000), UINT64_C(0x02500000), UC_PROT_ALL},
	{UINT64_C(0x880000000), UINT64_C(0x280000000), UC_PROT_ALL},
	{0, 0, UC_PROT_NONE},
};

static uc_err map_regular_memory(uc_engine *uc, uint64_t base, uint64_t size,
				 uint32_t perms)
{
	if (size == 0)
		return UC_ERR_OK;
	printf("Mapping memory: A: 0x%" PRIx64 " L: 0x%" PRIx64 "\n",
	       base, size);
	return uc_mem_map(uc, base, size, perms);
}

static uc_err map_memory_region(uc_engine *uc,
				const struct memory_mapping *map)
{
	const uint64_t map_end = map->base + map->size;
	const uint64_t fb_end = FB_ADDRESS + FB_SIZE;
	uc_err err;

	if (map->base > FB_ADDRESS || map_end < fb_end)
		return map_regular_memory(uc, map->base, map->size, map->perms);

	err = map_regular_memory(uc, map->base, FB_ADDRESS - map->base,
				 map->perms);
	if (err != UC_ERR_OK)
		return err;

	printf("Mapping shared framebuffer: A: 0x%x L: 0x%x\n",
	       FB_ADDRESS, FB_SIZE);
	err = uc_mem_map_ptr(uc, FB_ADDRESS, FB_SIZE, map->perms, framebuffer);
	if (err != UC_ERR_OK)
		return err;

	return map_regular_memory(uc, fb_end, map_end - fb_end, map->perms);
}

int memmap_soc(uc_engine *uc, enum board_memory_type board)
{
	uc_err err;

	if (board == MEMORY_8GB) {
		printf("8GB boards are not supported yet!\n");
		return UC_ERR_ARG;
	}

	for (size_t i = 0;
	     exynos990_12gb_memory[i].perms != UC_PROT_NONE; i++) {
		err = map_memory_region(uc, &exynos990_12gb_memory[i]);
		if (err != UC_ERR_OK) {
			fprintf(stderr, "Failed to map memory: %s\n",
				uc_strerror(err));
			return err;
		}
	}
	return UC_ERR_OK;
}

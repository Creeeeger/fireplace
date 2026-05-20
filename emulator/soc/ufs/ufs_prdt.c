/*
 *   Copyright (c) 2026 Umer Uddin <umer.uddin@mentallysanemainliners.org>
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

#include "ufs/ufs_internal.h"

static uint64_t prdt_address(const struct ufs_prdt *prdt)
{
	return ((uint64_t)prdt->upper_addr << 32) | prdt->base_addr;
}

uint64_t ufs_utrd_offset_words(uint16_t offset)
{
	return (uint64_t)offset << 2;
}

static uint32_t prdt_size(const struct ufs_prdt *prdt)
{
	return (prdt->size & UINT32_C(0x3ffff)) + 1;
}

static uint64_t prdt_table_addr(uc_engine *uc, uint64_t desc_addr,
				const struct ufs_utrd *utrd, uint32_t length)
{
	uint64_t word_addr = desc_addr + ufs_utrd_offset_words(utrd->prdt_off);
	uint64_t byte_addr = desc_addr + utrd->prdt_off;
	struct ufs_prdt prdt;

	if (length <= 1 || utrd->prdt_off == 0 || word_addr == byte_addr)
		return word_addr;
	if (uc_mem_read(uc, word_addr, &prdt, sizeof(prdt)) != UC_ERR_OK)
		return word_addr;
	if (prdt_address(&prdt) != 0 || prdt.size != 0)
		return word_addr;
	if (uc_mem_read(uc, byte_addr, &prdt, sizeof(prdt)) != UC_ERR_OK)
		return word_addr;
	if (prdt_address(&prdt) != 0 || prdt.size != 0)
		return byte_addr;
	return word_addr;
}

uc_err ufs_prdt_write(uc_engine *uc, uint64_t desc_addr,
			 const struct ufs_utrd *utrd, const unsigned char *buffer,
			 uint32_t length)
{
	uint64_t prdt_addr = prdt_table_addr(uc, desc_addr, utrd, length);
	uint32_t done = 0;

	for (uint16_t i = 0; done < length && i < utrd->prdt_len; i++) {
		struct ufs_prdt prdt;
		uint32_t chunk;
		uc_err err;

		err = uc_mem_read(uc, prdt_addr + i * sizeof(prdt), &prdt,
				  sizeof(prdt));
		if (err != UC_ERR_OK)
			return err;
		chunk = prdt_size(&prdt);
		if (chunk > length - done)
			chunk = length - done;
		err = uc_mem_write(uc, prdt_address(&prdt), buffer + done,
				   chunk);
		if (err != UC_ERR_OK)
			return err;
		done += chunk;
	}
	return done == length ? UC_ERR_OK : UC_ERR_ARG;
}

uc_err ufs_prdt_read(uc_engine *uc, uint64_t desc_addr,
			const struct ufs_utrd *utrd, unsigned char *buffer,
			uint32_t length)
{
	uint64_t prdt_addr = prdt_table_addr(uc, desc_addr, utrd, length);
	uint32_t done = 0;

	for (uint16_t i = 0; done < length && i < utrd->prdt_len; i++) {
		struct ufs_prdt prdt;
		uint32_t chunk;
		uc_err err;

		err = uc_mem_read(uc, prdt_addr + i * sizeof(prdt), &prdt,
				  sizeof(prdt));
		if (err != UC_ERR_OK)
			return err;
		chunk = prdt_size(&prdt);
		if (chunk > length - done)
			chunk = length - done;
		err = uc_mem_read(uc, prdt_address(&prdt), buffer + done,
				  chunk);
		if (err != UC_ERR_OK)
			return err;
		done += chunk;
	}
	return done == length ? UC_ERR_OK : UC_ERR_ARG;
}


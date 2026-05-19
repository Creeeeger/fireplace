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

uint16_t ufs_get_be16(const uint8_t *value)
{
	return ((uint16_t)value[0] << 8) | value[1];
}

void ufs_put_be16(uint8_t *value, uint16_t number)
{
	value[0] = (uint8_t)(number >> 8);
	value[1] = (uint8_t)number;
}

void ufs_put_be32(uint8_t *value, uint32_t number)
{
	value[0] = (uint8_t)(number >> 24);
	value[1] = (uint8_t)(number >> 16);
	value[2] = (uint8_t)(number >> 8);
	value[3] = (uint8_t)number;
}

uint32_t ufs_get_be32(const uint8_t *value)
{
	return ((uint32_t)value[0] << 24) |
	       ((uint32_t)value[1] << 16) |
	       ((uint32_t)value[2] << 8) | value[3];
}

uint64_t ufs_get_be64(const uint8_t *value)
{
	return ((uint64_t)ufs_get_be32(value) << 32) |
	       ufs_get_be32(value + 4);
}


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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ufs/ufs_internal.h"

static uint8_t storage_lun_from_upiu_lun(uint8_t lun)
{
	switch (lun) {
	case 0xb0:
		return 1;
	case 0xb1:
		return 2;
	default:
		return lun;
	}
}

uc_err ufs_complete_scsi_data_in(uc_engine *uc, uint64_t desc_addr,
				    const struct ufs_utrd *utrd,
				    struct ufs_cmd_desc *desc,
				    const unsigned char *data,
				    uint32_t data_length)
{
	uc_err err;

	if (data_length != 0) {
		err = ufs_prdt_write(uc, desc_addr, utrd, data, data_length);
		if (err != UC_ERR_OK)
			return err;
	}
	ufs_init_response(desc, 0x21, data_length);
	return UC_ERR_OK;
}

static uc_err handle_read_capacity(uc_engine *uc, uint64_t desc_addr,
				   const struct ufs_utrd *utrd,
				   struct ufs_cmd_desc *desc, uint8_t lun)
{
	unsigned char response[8] = {0};
	uint64_t blocks = ufs_lun_block_count(lun);
	uint32_t last_lba = 0;

	if (blocks > 0)
		last_lba = blocks > UINT32_MAX ? UINT32_MAX : (uint32_t)blocks - 1;
	ufs_put_be32(response, last_lba);
	ufs_put_be32(response + 4, UFS_BLOCK_SIZE);
	return ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, response,
				     sizeof(response));
}

static uc_err handle_mode_sense(uc_engine *uc, uint64_t desc_addr,
				const struct ufs_utrd *utrd,
				struct ufs_cmd_desc *desc, bool ten_byte)
{
	unsigned char response[8] = {0};
	uint16_t alloc_len;
	uint32_t length;

	if (ten_byte) {
		alloc_len = ((uint16_t)desc->command_upiu.tsf[11] << 8) |
			    desc->command_upiu.tsf[12];
		ufs_put_be16(response, sizeof(response) - 2);
		length = sizeof(response);
	} else {
		alloc_len = desc->command_upiu.tsf[8];
		response[0] = 3;
		length = 4;
	}
	if (alloc_len < length)
		length = alloc_len;
	printf("[UFS] MODE_SENSE_%u command received\n", ten_byte ? 10 : 6);
	return ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, response,
				     length);
}

static uc_err handle_read_10(uc_engine *uc, uint64_t desc_addr,
			     const struct ufs_utrd *utrd,
			     struct ufs_cmd_desc *desc, uint8_t lun)
{
	uint32_t lba = ((uint32_t)desc->command_upiu.tsf[6] << 24) |
		       ((uint32_t)desc->command_upiu.tsf[7] << 16) |
		       ((uint32_t)desc->command_upiu.tsf[8] << 8) |
		       desc->command_upiu.tsf[9];
	uint16_t transfer_blocks = ((uint16_t)desc->command_upiu.tsf[11] << 8) |
				   desc->command_upiu.tsf[12];
	uint32_t total_bytes = (uint32_t)transfer_blocks * UFS_BLOCK_SIZE;
	uint64_t file_offset = (uint64_t)lba * UFS_BLOCK_SIZE;
	unsigned char *buffer;
	uc_err err;

	buffer = malloc(total_bytes ? total_bytes : 1);
	if (!buffer)
		return UC_ERR_NOMEM;
	if (!ufs_read_lun(lun, file_offset, buffer, total_bytes)) {
		free(buffer);
		return UC_ERR_HANDLE;
	}
	err = ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, buffer,
				    total_bytes);
	free(buffer);
	return err;
}


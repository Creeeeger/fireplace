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

static uc_err handle_write_10(uc_engine *uc, uint64_t desc_addr,
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
	err = ufs_prdt_read(uc, desc_addr, utrd, buffer, total_bytes);
	if (err == UC_ERR_OK &&
	    !ufs_write_lun_overlay(lun, file_offset, buffer, total_bytes))
		err = UC_ERR_NOMEM;
	free(buffer);
	if (err != UC_ERR_OK)
		return err;
	ufs_init_response(desc, 0x21, 0);
	return UC_ERR_OK;
}

static uc_err handle_unmap(uc_engine *uc, uint64_t desc_addr,
			   const struct ufs_utrd *utrd,
			   struct ufs_cmd_desc *desc, uint8_t lun)
{
	uint8_t *cdb = &desc->command_upiu.tsf[4];
	uint16_t parameter_length = ufs_get_be16(&cdb[7]);
	uint16_t descriptor_length;
	uint64_t capacity = ufs_lun_block_count(lun);
	uint64_t discarded_blocks = 0;
	unsigned int descriptor_count = 0;
	unsigned char *parameters;
	uc_err err;

	if (parameter_length == 0) {
		ufs_init_response(desc, 0x21, 0);
		return UC_ERR_OK;
	}
	parameters = malloc(parameter_length);
	if (!parameters)
		return UC_ERR_NOMEM;
	err = ufs_prdt_read(uc, desc_addr, utrd, parameters, parameter_length);
	if (err != UC_ERR_OK) {
		free(parameters);
		return err;
	}
	if (parameter_length < 8) {
		printf("[UFS] UNMAP parameter list is too short: %u\n",
		       parameter_length);
		free(parameters);
		ufs_init_response(desc, 0x21, 0);
		return UC_ERR_OK;
	}

	descriptor_length = ufs_get_be16(parameters + 2);
	if (descriptor_length > parameter_length - 8) {
		printf("[UFS] UNMAP descriptor list truncated from %u to %u bytes\n",
		       descriptor_length, parameter_length - 8);
		descriptor_length = parameter_length - 8;
	}
	descriptor_length -= descriptor_length % 16;
	for (uint16_t offset = 8; offset < 8 + descriptor_length;
	     offset += 16) {
		uint64_t lba = ufs_get_be64(parameters + offset);
		uint32_t blocks = ufs_get_be32(parameters + offset + 8);

		if (blocks == 0)
			continue;
		if (lba >= capacity) {
			printf("[UFS] UNMAP LU%u LBA 0x%" PRIx64
			       " is outside 0x%" PRIx64 " blocks\n",
			       lun, lba, capacity);
			continue;
		}
		if ((uint64_t)blocks > capacity - lba)
			blocks = (uint32_t)(capacity - lba);
		if (!ufs_discard_lun_overlay(lun, lba * UFS_BLOCK_SIZE,
					 (uint64_t)blocks * UFS_BLOCK_SIZE)) {
			free(parameters);
			return UC_ERR_NOMEM;
		}
		discarded_blocks += blocks;
		descriptor_count++;
	}
	free(parameters);
	printf("[UFS] UNMAP LU%u: %u descriptors, 0x%" PRIx64
	       " blocks\n", lun, descriptor_count, discarded_blocks);
	ufs_init_response(desc, 0x21, 0);
	return UC_ERR_OK;
}

uc_err ufs_handle_scsi(uc_engine *uc, uint64_t desc_addr,
			  const struct ufs_utrd *utrd, struct ufs_cmd_desc *desc)
{
	uint8_t *cdb = &desc->command_upiu.tsf[4];
	uint8_t opcode = cdb[0];
	uint8_t lun = desc->command_upiu.header.lun;
	uint8_t storage_lun = storage_lun_from_upiu_lun(lun);
	unsigned char data[36] = {0};

	switch (opcode) {
	case SCSI_TEST_UNIT_READY:
	case SCSI_START_STOP_UNIT:
	case SCSI_SYNCHRONIZE_CACHE_10:
		ufs_init_response(desc, 0x21, 0);
		return UC_ERR_OK;
	case SCSI_REQUEST_SENSE:
		data[0] = 0x70;
		data[7] = 0x0a;
		return ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, data, 18);
	case SCSI_INQUIRY:
		data[2] = 0x06;
		data[3] = 0x02;
		data[4] = 0x1f;
		memcpy(&data[8], "SAMSUNG ", 8);
		memcpy(&data[16], "UFS LUN         ", 16);
		memcpy(&data[32], "1.00", 4);
		return ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, data,
					     sizeof(data));
	case SCSI_MODE_SENSE_6:
		return handle_mode_sense(uc, desc_addr, utrd, desc, false);
	case SCSI_READ_CAPACITY_10:
		return handle_read_capacity(uc, desc_addr, utrd, desc,
					    storage_lun);
	case SCSI_READ_10:
		return handle_read_10(uc, desc_addr, utrd, desc, storage_lun);
	case SCSI_WRITE_10:
		return handle_write_10(uc, desc_addr, utrd, desc, storage_lun);
	case SCSI_UNMAP:
		return handle_unmap(uc, desc_addr, utrd, desc, storage_lun);
	case SCSI_MODE_SENSE_10:
		return handle_mode_sense(uc, desc_addr, utrd, desc, true);
	case SCSI_SECURITY_PROTOCOL_IN:
		return ufs_rpmb_security_in(uc, desc_addr, utrd, desc);
	case SCSI_SECURITY_PROTOCOL_OUT:
		return ufs_rpmb_security_out(uc, desc_addr, utrd, desc);
	default:
		printf("[UFS] Unsupported SCSI opcode 0x%x\n", opcode);
		ufs_init_response(desc, 0x21, 0);
		return UC_ERR_OK;
	}
}


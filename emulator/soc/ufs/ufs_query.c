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

#include <stdio.h>
#include <string.h>

#include "ufs/ufs_internal.h"

static uint32_t query_attrs[256];
static bool query_flags[256];
static uint8_t device_init_reads_remaining;

void ufs_query_reset(void)
{
	memset(query_attrs, 0, sizeof(query_attrs));
	memset(query_flags, 0, sizeof(query_flags));
	device_init_reads_remaining = 0;
	query_attrs[0x00] = 1;
	query_attrs[0x02] = 1;
	query_attrs[0x07] = 0x40;
	query_attrs[0x08] = 0x40;
}

void ufs_set_query_data_length(struct ufs_upiu *resp, uint32_t length)
{
	resp->tsf[8] = (uint8_t)(length >> 24);
	resp->tsf[9] = (uint8_t)(length >> 16);
	resp->tsf[10] = (uint8_t)(length >> 8);
	resp->tsf[11] = (uint8_t)length;
}

static uint32_t get_query_value(const struct ufs_cmd_desc *desc)
{
	return ((uint32_t)desc->command_upiu.tsf[8] << 24) |
	       ((uint32_t)desc->command_upiu.tsf[9] << 16) |
	       ((uint32_t)desc->command_upiu.tsf[10] << 8) |
	       desc->command_upiu.tsf[11];
}

struct ufs_upiu *ufs_init_response(struct ufs_cmd_desc *desc, uint8_t type,
				      uint32_t data_length)
{
	struct ufs_upiu *resp = &desc->response_upiu;

	memset(resp, 0, sizeof(*resp));
	resp->header.type = type;
	resp->header.lun = desc->command_upiu.header.lun;
	resp->header.tag = desc->command_upiu.header.tag;
	resp->header.function = desc->command_upiu.header.function;
	resp->header.response = 0;
	resp->header.status = 0;
	resp->header.datalength = (uint16_t)data_length;
	return resp;
}

void ufs_complete_query_response(struct ufs_cmd_desc *desc, uint8_t status)
{
	struct ufs_upiu *resp = ufs_init_response(desc, 0x36, 0);

	resp->header.response = status;
	resp->tsf[0] = desc->command_upiu.tsf[0];
	resp->tsf[1] = desc->command_upiu.tsf[1];
	resp->tsf[2] = desc->command_upiu.tsf[2];
	resp->tsf[3] = desc->command_upiu.tsf[3];
}

static void complete_query_attr(struct ufs_cmd_desc *desc)
{
	struct ufs_upiu *resp;
	uint8_t idn = desc->command_upiu.tsf[1];
	uint32_t value = query_attrs[idn];

	ufs_complete_query_response(desc, 0);
	resp = &desc->response_upiu;
	resp->tsf[8] = (uint8_t)(value >> 24);
	resp->tsf[9] = (uint8_t)(value >> 16);
	resp->tsf[10] = (uint8_t)(value >> 8);
	resp->tsf[11] = (uint8_t)value;
}

static void complete_query_flag(struct ufs_cmd_desc *desc)
{
	struct ufs_upiu *resp;
	uint8_t idn = desc->command_upiu.tsf[1];
	uint32_t value = query_flags[idn] ? 1 : 0;

	ufs_complete_query_response(desc, 0);
	resp = &desc->response_upiu;
	resp->tsf[8] = 0;
	resp->tsf[9] = 0;
	resp->tsf[10] = 0;
	resp->tsf[11] = (uint8_t)value;

	/*
	 * fDeviceInit is cleared by the device when initialization completes.
	 * Keep it asserted for one read so the query has an observable pending
	 * state, then complete it before the host's next poll.
	 */
	if (idn == QUERY_FLAG_ID_DEVICE_INIT && value &&
	    device_init_reads_remaining != 0) {
		device_init_reads_remaining--;
		if (device_init_reads_remaining == 0)
			query_flags[idn] = false;
	}
}

uc_err ufs_handle_query(struct ufs_cmd_desc *desc)
{
	uint8_t opcode = desc->command_upiu.tsf[0];

	switch (opcode) {
	case UPIU_QUERY_OPCODE_READ_ATTR:
		complete_query_attr(desc);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_WRITE_ATTR:
		query_attrs[desc->command_upiu.tsf[1]] = get_query_value(desc);
		ufs_complete_query_response(desc, 0);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_READ_FLAG:
		complete_query_flag(desc);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_SET_FLAG:
		query_flags[desc->command_upiu.tsf[1]] = true;
		if (desc->command_upiu.tsf[1] == QUERY_FLAG_ID_DEVICE_INIT)
			device_init_reads_remaining = 1;
		ufs_complete_query_response(desc, 0);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_CLEAR_FLAG:
		query_flags[desc->command_upiu.tsf[1]] = false;
		if (desc->command_upiu.tsf[1] == QUERY_FLAG_ID_DEVICE_INIT)
			device_init_reads_remaining = 0;
		ufs_complete_query_response(desc, 0);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
		query_flags[desc->command_upiu.tsf[1]] =
			!query_flags[desc->command_upiu.tsf[1]];
		if (desc->command_upiu.tsf[1] == QUERY_FLAG_ID_DEVICE_INIT)
			device_init_reads_remaining =
				query_flags[QUERY_FLAG_ID_DEVICE_INIT] ? 1 : 0;
		ufs_complete_query_response(desc, 0);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_READ_DESC:
		ufs_complete_query_descriptor(desc);
		return UC_ERR_OK;
	case UPIU_QUERY_OPCODE_WRITE_DESC:
		ufs_complete_query_write_descriptor(desc);
		return UC_ERR_OK;
	default:
		ufs_complete_query_response(desc, 1);
		printf("[UFS] Unsupported UFS Query opcode: 0x%x\n", opcode);
		return UC_ERR_OK;
	}
}

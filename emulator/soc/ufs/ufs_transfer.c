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

#include "ufs/ufs_internal.h"

uc_err ufs_handle_transfer_request(uc_engine *uc, uint64_t utrd_addr)
{
	struct ufs_cmd_desc desc;
	struct ufs_utrd utrd;
	uint64_t desc_addr;
	uint64_t response_addr;
	size_t response_size;
	uc_err err;

	err = uc_mem_read(uc, utrd_addr, &utrd, sizeof(utrd));
	if (err != UC_ERR_OK)
		return err;
	utrd.dw[2] = 0;
	err = uc_mem_write(uc, utrd_addr, &utrd, sizeof(utrd));
	if (err != UC_ERR_OK)
		return err;
	desc_addr = ((uint64_t)utrd.cmd_desc_addr_h << 32) |
		    utrd.cmd_desc_addr_l;
	err = uc_mem_read(uc, desc_addr, &desc, sizeof(desc));
	if (err != UC_ERR_OK)
		return err;

	switch (desc.command_upiu.header.type) {
	case UPIU_TRANSACTION_NOP_OUT:
		ufs_init_response(&desc, UPIU_TRANSACTION_NOP_IN, 0);
		err = UC_ERR_OK;
		break;
	case UPIU_TRANSACTION_COMMAND:
		err = ufs_handle_scsi(uc, desc_addr, &utrd, &desc);
		break;
	case UPIU_TRANSACTION_QUERY_REQ:
		if (desc.command_upiu.header.function != UFS_STD_READ_REQ &&
		    desc.command_upiu.header.function != UFS_STD_WRITE_REQ) {
			printf("[UFS] Unsupported Query Function 0x%x\n",
			       desc.command_upiu.header.function);
			ufs_complete_query_response(&desc, 1);
			err = UC_ERR_OK;
			break;
		}
		err = ufs_handle_query(&desc);
		break;
	default:
		printf("[UFS] Unsupported UPIU transaction 0x%x\n",
		       desc.command_upiu.header.type);
		ufs_init_response(&desc, 0x21, 0);
		err = UC_ERR_OK;
		break;
	}
	if (err != UC_ERR_OK)
		return err;
	response_addr = desc_addr + utrd.rsp_upiu_off;
	response_size = utrd.rsp_upiu_len ? utrd.rsp_upiu_len :
					    sizeof(desc.response_upiu);
	if (response_size > sizeof(desc.response_upiu))
		response_size = sizeof(desc.response_upiu);
	err = uc_mem_write(uc, response_addr, &desc.response_upiu,
			   response_size);
	if (err != UC_ERR_OK)
		return err;
	if (ufs_utrd_offset_words(utrd.rsp_upiu_off) != utrd.rsp_upiu_off) {
		response_addr = desc_addr + ufs_utrd_offset_words(utrd.rsp_upiu_off);
		err = uc_mem_write(uc, response_addr, &desc.response_upiu,
				   response_size);
	}
	return err;
}



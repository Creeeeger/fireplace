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

static uint32_t utrdlba;
static uint32_t utrdlbau;
static uint32_t arg1;
static uint32_t arg2;
static uint32_t arg3;
static uint32_t active_rx = 1;
static uint32_t active_tx = 1;
static bool uic_command_pending_completion;
static bool utp_command_pending_completion;
static bool uic_command_needs_pms;
struct dme_attr {
	uint32_t key;
	uint32_t value;
	bool valid;
};

static struct dme_attr dme_attrs[DME_ATTR_CAPACITY];

static void dme_attrs_reset(void)
{
	memset(dme_attrs, 0, sizeof(dme_attrs));
}

static bool dme_attr_get(uint32_t key, uint32_t *value)
{
	for (size_t i = 0; i < DME_ATTR_CAPACITY; i++) {
		if (dme_attrs[i].valid && dme_attrs[i].key == key) {
			*value = dme_attrs[i].value;
			return true;
		}
	}
	return false;
}

static void dme_attr_set(uint32_t key, uint32_t value)
{
	size_t free_slot = DME_ATTR_CAPACITY;

	for (size_t i = 0; i < DME_ATTR_CAPACITY; i++) {
		if (dme_attrs[i].valid && dme_attrs[i].key == key) {
			dme_attrs[i].value = value;
			return;
		}
		if (!dme_attrs[i].valid && free_slot == DME_ATTR_CAPACITY)
			free_slot = i;
	}
	if (free_slot == DME_ATTR_CAPACITY)
		return;
	dme_attrs[free_slot].key = key;
	dme_attrs[free_slot].value = value;
	dme_attrs[free_slot].valid = true;
}

int ufs_init(struct uc_struct *uc_s)
{
	uint32_t hcs = 0x7 | (UINT32_C(1) << 8);

	active_rx = 1;
	active_tx = 1;
	arg1 = 0;
	arg2 = 0;
	arg3 = 0;
	uic_command_pending_completion = false;
	utp_command_pending_completion = false;
	uic_command_needs_pms = false;
	ufs_rpmb_reset();
	ufs_query_reset();
	ufs_descriptors_init();
	dme_attrs_reset();
	dme_attr_set(0x1520 << 16, 2);
	dme_attr_set(0x1540 << 16, 2);
	dme_attr_set(0x1543 << 16, 1);
	dme_attr_set(0x1560 << 16, active_tx);
	dme_attr_set(0x1561 << 16, active_tx);
	dme_attr_set(0x1571 << 16, 0);
	dme_attr_set(0x1580 << 16, active_rx);
	dme_attr_set(0x1581 << 16, active_rx);
	dme_attr_set(0x1587 << 16, 4);
	uc_mem_write(uc_s, REG_HOST_CONTROLLER_STATUS, &hcs, sizeof(hcs));
	printf("[UFS] initialized");
	if (ufs_storage_directory()[0] != '\0')
		printf(" with LUN dump directory: %s", ufs_storage_directory());
	printf("\n");
	return 0;
}

static void complete_uic_read(uc_engine *uc)
{
	uint32_t status;

	if (!uic_command_pending_completion)
		return;
	if (uic_command_needs_pms) {
		uc_mem_read(uc, REG_HOST_CONTROLLER_STATUS, &status,
			    sizeof(status));
		status &= ~(UINT32_C(0x7) << 8);
		status |= UINT32_C(0x1) << 8;
		uc_mem_write(uc, REG_HOST_CONTROLLER_STATUS, &status,
			     sizeof(status));
		status = 0x410;
		uic_command_needs_pms = false;
	} else {
		status = 0x400;
	}
	uc_mem_write(uc, REG_INTERRUPT_STATUS, &status, sizeof(status));
	uic_command_pending_completion = false;
}

static void handle_uic_command(uc_engine *uc, uint32_t command)
{
	uint32_t status;

	switch (command) {
	case UIC_CMD_DME_PEER_SET:
		uic_command_pending_completion = true;
		break;
	case UIC_CMD_DME_LINK_STARTUP:
		status = 0x7 | (UINT32_C(1) << 8);
		uc_mem_write(uc, REG_HOST_CONTROLLER_STATUS, &status,
			     sizeof(status));
		uic_command_pending_completion = true;
		break;
	case UIC_CMD_DME_GET:
		switch (arg1) {
		case 0x1520 << 16:
			arg3 = 2;
			break;
		case 0x1540 << 16:
			arg3 = 2;
			break;
		case 0x1587 << 16:
			arg3 = 4;
			break;
		case 0x1560 << 16:
			arg3 = active_tx;
			break;
		case 0x1561 << 16:
			arg3 = active_tx;
			active_tx = 2;
			break;
		case 0x1580 << 16:
			arg3 = active_rx;
			break;
		case 0x1581 << 16:
			arg3 = active_rx;
			active_rx = 2;
			break;
		case 0x1543 << 16:
			arg3 = 1;
			break;
		default:
			if (!dme_attr_get(arg1, &arg3)) {
				arg3 = 0;
			}
			break;
		}
		uc_mem_write(uc, REG_UIC_ARG3, &arg3, sizeof(arg3));
		uic_command_pending_completion = true;
		break;
	case UIC_CMD_DME_SET:
		dme_attr_set(arg1, arg3);
		if (arg1 == (0x1560 << 16))
			active_tx = arg3 ? arg3 : active_tx;
		else if (arg1 == (0x1580 << 16))
			active_rx = arg3 ? arg3 : active_rx;
		if (arg1 == (0x1571 << 16))
			uic_command_needs_pms = true;
		uic_command_pending_completion = true;
		break;
	default:
		printf("[UFS] Unknown UIC_COMMAND: 0x%x\n", command);
		uic_command_pending_completion = true;
		break;
	}
}
void ufs_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	(void)size;
	(void)user_data;

	if (!uic_command_pending_completion && !utp_command_pending_completion) {
		uint32_t clear = 0;
		uc_mem_write(uc, REG_INTERRUPT_STATUS, &clear, sizeof(clear));
	}
	switch (address) {
	case REG_INTERRUPT_STATUS:
		if (type == UC_MEM_READ) {
			uint32_t status = 1;

			complete_uic_read(uc);
			if (utp_command_pending_completion) {
				uc_mem_write(uc, REG_INTERRUPT_STATUS, &status,
					     sizeof(status));
				utp_command_pending_completion = false;
			}
		}
		break;
	case REG_HOST_CONTROLLER_STATUS:
		if (type == UC_MEM_READ) {
			uint32_t hcs = 0x7 | (UINT32_C(1) << 8);

			uc_mem_write(uc, REG_HOST_CONTROLLER_STATUS, &hcs,
				     sizeof(hcs));
		}
		break;
	case REG_UIC_COMMAND:
		if (type == UC_MEM_WRITE)
			handle_uic_command(uc, (uint32_t)value);
		break;
	case REG_UIC_ARG1:
		if (type == UC_MEM_WRITE)
			arg1 = (uint32_t)value;
		break;
	case REG_UIC_ARG2:
		if (type == UC_MEM_WRITE)
			arg2 = (uint32_t)value;
		break;
	case REG_UIC_ARG3:
		if (type == UC_MEM_WRITE)
			arg3 = (uint32_t)value;
		break;
	case UFS_BASE + 0x1154:
	case UFS_BASE + 0x1150:
	{
		uint32_t zero = 0;

		uc_mem_write(uc, UFS_BASE + 0x1150, &zero, sizeof(zero));
		uc_mem_write(uc, UFS_BASE + 0x1154, &zero, sizeof(zero));
		break;
	}
	case REG_UTP_TRANSFER_REQ_LIST_BASE_L:
		if (type == UC_MEM_WRITE)
			utrdlba = (uint32_t)value;
		break;
	case REG_UTP_TRANSFER_REQ_LIST_BASE_H:
		if (type == UC_MEM_WRITE)
			utrdlbau = (uint32_t)value;
		break;
	case UFS_BASE + 0x4000 + (0xce4 + 0x800 * 0):
	case UFS_BASE + 0x4000 + (0xce4 + 0x800 * 1):
	case UFS_BASE + 0x4000 + (0xce4 + 0x800 * 2):
		if (type == UC_MEM_READ) {
			uint32_t locked = 0x08;

			uc_mem_write(uc, address, &locked, sizeof(locked));
		}
		break;
	case REG_UTP_TRANSFER_REQ_DOOR_BELL:
		if (type == UC_MEM_WRITE && value) {
			uc_err err;

			err = ufs_handle_transfer_request(
					uc, ((uint64_t)utrdlbau << 32) | utrdlba);
			if (err != UC_ERR_OK)
				printf("[UFS] Transfer request failed: %s\n",
				       uc_strerror(err));
			utp_command_pending_completion = true;
		} else if (type == UC_MEM_READ && !utp_command_pending_completion) {
			uint32_t clear = 0;

			uc_mem_write(uc, REG_UTP_TRANSFER_REQ_DOOR_BELL, &clear,
				     sizeof(clear));
		}
		break;
	}
}

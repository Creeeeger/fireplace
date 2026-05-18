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


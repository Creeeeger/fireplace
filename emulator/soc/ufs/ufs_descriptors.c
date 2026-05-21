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

static uint8_t configuration_descriptor[UFS_CONFIG_DESC_LENGTH];
static uint32_t total_raw_device_capacity;

static struct ufs_unit_desc unit_descriptor[8] = {
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x00,
        .bLUEnable = 0x02,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x00,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x0C,
        .qLogicalBlockCount_h = 0x01000000,
        .qLogicalBlockCount_l = 0x000098DC,
        .dEraseBlockSize = 0x02010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x0098DC01,
        .qPhyMemResourceCount_l = 0x10000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x01,
        .bLUEnable = 0x01,
        .bBootLunID = 0x01,
        .bLUWriteProtect = 0x01,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x03,
        .bDataReliability = 0x01,
        .bLogicalBlockSize = 0x0C,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000400,
        .dEraseBlockSize = 0x02010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00040000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x02,
        .bLUEnable = 0x01,
        .bBootLunID = 0x02,
        .bLUWriteProtect = 0x01,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x03,
        .bDataReliability = 0x01,
        .bLogicalBlockSize = 0x0C,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000400,
        .dEraseBlockSize = 0x02010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00040000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x03,
        .bLUEnable = 0x01,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x01,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x0C,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000800,
        .dEraseBlockSize = 0x02010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00080000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x04,
        .bLUEnable = 0x01,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x01,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x0C,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 4096,
        .dEraseBlockSize = 0x02010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00100000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x05,
        .bLUEnable = 0x00,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x00,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x00,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000000,
        .dEraseBlockSize = 0x00010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00000000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x06,
        .bLUEnable = 0x00,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x00,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x00,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000000,
        .dEraseBlockSize = 0x00010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00000000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
    {
        .bLength = 0x2D,
        .bDescriptorType = 0x02,
        .bUnitIndex = 0x07,
        .bLUEnable = 0x00,
        .bBootLunID = 0x00,
        .bLUWriteProtect = 0x00,
        .bLUQueueDepth = 0x00,
        .Reserved = 0x00,
        .bMemoryType = 0x00,
        .bDataReliability = 0x00,
        .bLogicalBlockSize = 0x00,
        .qLogicalBlockCount_h = 0x00000000,
        .qLogicalBlockCount_l = 0x00000000,
        .dEraseBlockSize = 0x00010000,
        .bProvisioningType = 0x00,
        .qPhyMemResourceCount_h = 0x00000000,
        .qPhyMemResourceCount_l = 0x00000000,
        .wContextCapabilities = 0x0000,
        .bLargeUnitSize_M1 = 0x00,
    },
};

static void init_config_unit(uint8_t lun, uint8_t enable, uint8_t boot_lun,
			     uint8_t write_protect, uint8_t memory_type,
			     uint32_t alloc_units, uint8_t data_reliability,
			     uint8_t logical_block_size)
{
	uint8_t *unit = configuration_descriptor + UFS_CONFIG_HEADER_LENGTH +
			UFS_CONFIG_UNIT_LENGTH * lun;

	unit[0] = enable;
	unit[1] = boot_lun;
	unit[2] = write_protect;
	unit[3] = memory_type;
	ufs_put_be32(unit + 4, alloc_units);
	unit[8] = data_reliability;
	unit[9] = logical_block_size;
	unit[10] = enable ? 0x02 : 0;
	memset(unit + 13, 0xff, 3);
	memset(unit + 18, 0xff, 4);
}

void ufs_descriptors_init(void)
{
	uint32_t blocks_per_alloc_unit =
		UFS_SEGMENT_SIZE * 512U / UFS_BLOCK_SIZE;
	uint32_t total_alloc_units = 0;
	uint64_t user_blocks = ufs_lun_block_count(0);
	uint32_t user_alloc_units;

	memset(configuration_descriptor, 0, sizeof(configuration_descriptor));
	configuration_descriptor[0] = UFS_CONFIG_DESC_LENGTH;
	configuration_descriptor[1] = QUERY_IDN_CONFIG_DESC;
	configuration_descriptor[2] = 0;
	configuration_descriptor[3] = 1;
	configuration_descriptor[4] = 1;
	configuration_descriptor[5] = 1;
	configuration_descriptor[6] = 0x7f;
	configuration_descriptor[7] = 0;
	configuration_descriptor[8] = 0x0f;
	memset(configuration_descriptor + 11, 0xff, 5);
	configuration_descriptor[16] = 1;

	/*
	 * Match the LU sizes already exposed by READ CAPACITY.  One allocation
	 * unit is one 0x2000-sector segment (4 MiB).
	 */
	user_alloc_units = (uint32_t)((user_blocks + blocks_per_alloc_unit - 1) /
				      blocks_per_alloc_unit);
	if (user_alloc_units == 0)
		user_alloc_units = 1;
	init_config_unit(0, 1, 0, 0, 0, user_alloc_units, 0, 0x0c);
	init_config_unit(1, 1, 1, 1, 3, 1, 1, 0x0c);
	init_config_unit(2, 1, 2, 1, 3, 1, 1, 0x0c);
	init_config_unit(3, 1, 0, 1, 0, 2, 0, 0x0c);
	init_config_unit(4, 1, 0, 1, 0, 4, 0, 0x0c);
	init_config_unit(5, 0, 0, 0, 0, 0, 0, 0);
	init_config_unit(6, 0, 0, 0, 0, 0, 0, 0);
	init_config_unit(7, 0, 0, 0, 0, 0, 0, 0);

	for (uint8_t lun = 0; lun < UFS_LUN_COUNT; lun++) {
		const uint8_t *unit = configuration_descriptor +
			UFS_CONFIG_HEADER_LENGTH + UFS_CONFIG_UNIT_LENGTH * lun;

		total_alloc_units += ufs_get_be32(unit + 4);
	}
	total_raw_device_capacity = total_alloc_units * UFS_SEGMENT_SIZE;
}

static uint32_t copy_descriptor(struct ufs_upiu *resp, const void *data,
				uint32_t data_length)
{
	if (data_length > UPIU_DATA_SIZE)
		data_length = UPIU_DATA_SIZE;
	memcpy(resp->data, data, data_length);
	ufs_set_query_data_length(resp, data_length);
	resp->header.datalength = (uint16_t)data_length;
	return data_length;
}

void ufs_complete_query_descriptor(struct ufs_cmd_desc *desc)
{
	uint8_t descriptor[UFS_CONFIG_DESC_LENGTH] = {0};
	struct ufs_upiu *resp;
	uint8_t idn = desc->command_upiu.tsf[1];
	uint8_t index = desc->command_upiu.tsf[2];
	uint32_t length = 0;

	ufs_complete_query_response(desc, 0);
	resp = &desc->response_upiu;
	switch (idn) {
	case QUERY_IDN_DEVICE_DESC:
		descriptor[0] = UFS_DEVICE_DESC_LENGTH;
		descriptor[1] = QUERY_IDN_DEVICE_DESC;
		descriptor[4] = 0x02;
		descriptor[5] = 0x1f;
		descriptor[6] = 5;
		descriptor[8] = 1;
		descriptor[9] = 1;
		descriptor[10] = 1;
		descriptor[11] = 0x7f;
		descriptor[13] = 1;
		descriptor[15] = 0x0f;
		ufs_put_be16(descriptor + 0x10, 0x0300);
		descriptor[0x14] = 1;
		descriptor[0x15] = 2;
		descriptor[0x16] = 3;
		descriptor[0x17] = 4;
		ufs_put_be16(descriptor + 0x18, 0x01ce);
		descriptor[0x1a] = UFS_CONFIG_HEADER_LENGTH;
		descriptor[0x1b] = UFS_CONFIG_UNIT_LENGTH;
		descriptor[0x24] = 5;
		length = copy_descriptor(resp, descriptor,
					 UFS_DEVICE_DESC_LENGTH);
		break;
	case QUERY_IDN_CONFIG_DESC:
		length = copy_descriptor(resp, configuration_descriptor,
					 UFS_CONFIG_DESC_LENGTH);
		break;
	case QUERY_IDN_UNIT_DESC:
		if (index >= UFS_LUN_COUNT) {
			resp->header.response = 1;
			printf("[UFS] Unit descriptor index %u out of range\n",
			       index);
			break;
		}
		length = copy_descriptor(resp, &unit_descriptor[index],
					 sizeof(unit_descriptor[index]));
		break;
	case QUERY_IDN_INTERCONNECT_DESC:
		descriptor[0] = 0x06;
		descriptor[1] = QUERY_IDN_INTERCONNECT_DESC;
		length = copy_descriptor(resp, descriptor, 0x06);
		break;
	case QUERY_IDN_STRING_DESC:
		descriptor[0] = 0x12;
		descriptor[1] = QUERY_IDN_STRING_DESC;
		memcpy(&descriptor[2], "SAMSUNG UFS", 11);
		length = copy_descriptor(resp, descriptor, 0x12);
		break;
	case QUERY_IDN_GEOMETRY_DESC:
		descriptor[0] = UFS_GEOMETRY_DESC_LENGTH;
		descriptor[1] = QUERY_IDN_GEOMETRY_DESC;
		/* qTotalRawDeviceCapacity is expressed in 512-byte sectors. */
		ufs_put_be32(descriptor + 8, total_raw_device_capacity);
		ufs_put_be32(descriptor + 0x0d, UFS_SEGMENT_SIZE);
		descriptor[0x11] = 1;
		descriptor[0x12] = 8;
		ufs_put_be16(descriptor + 0x1e, 0x000f);
		ufs_put_be16(descriptor + 0x30, 0x0100);
		length = copy_descriptor(resp, descriptor,
					 UFS_GEOMETRY_DESC_LENGTH);
		break;
	case QUERY_IDN_POWER_DESC:
		descriptor[0] = 0x62;
		descriptor[1] = QUERY_IDN_POWER_DESC;
		length = copy_descriptor(resp, descriptor, 0x62);
		break;
	default:
		resp->header.response = 1;
		printf("[UFS] Unknown UFS Query Request Descriptor: 0x%x\n",
		       idn);
		break;
	}
	(void)length;
}

void ufs_complete_query_write_descriptor(struct ufs_cmd_desc *desc)
{
	uint8_t idn = desc->command_upiu.tsf[1];
	uint16_t length = ((uint16_t)desc->command_upiu.tsf[6] << 8) |
			  desc->command_upiu.tsf[7];

	if (idn == QUERY_IDN_CONFIG_DESC) {
		if (length != UFS_CONFIG_DESC_LENGTH ||
		    desc->command_upiu.data[0] != UFS_CONFIG_DESC_LENGTH ||
		    desc->command_upiu.data[1] != QUERY_IDN_CONFIG_DESC) {
			ufs_complete_query_response(desc, 1);
			return;
		}
		memcpy(configuration_descriptor, desc->command_upiu.data,
		       sizeof(configuration_descriptor));
	}
	ufs_complete_query_response(desc, 0);
}


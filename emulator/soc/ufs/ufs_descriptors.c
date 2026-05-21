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


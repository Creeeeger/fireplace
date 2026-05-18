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

#ifndef FIREPLACE_SOC_UFS_INTERNAL_H
#define FIREPLACE_SOC_UFS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/ufs/ufs.h>

#define UFS_BASE UINT64_C(0x13100000)
#define UFS_BLOCK_SIZE 4096U
#define UFS_LUN_COUNT 8
#define UFS_DEVICE_DESC_LENGTH 0x59U
#define UFS_CONFIG_HEADER_LENGTH 0x12U
#define UFS_CONFIG_UNIT_LENGTH 0x1aU
#define UFS_CONFIG_DESC_LENGTH \
	(UFS_CONFIG_HEADER_LENGTH + UFS_LUN_COUNT * UFS_CONFIG_UNIT_LENGTH)
#define UFS_GEOMETRY_DESC_LENGTH 0x58U
#define UFS_SEGMENT_SIZE 0x2000U
#define RPMB_FRAME_SIZE 512U
#define RPMB_DATA_OFFSET 228U
#define RPMB_DATA_SIZE 256U
#define RPMB_NONCE_OFFSET 484U
#define RPMB_HMAC_OFFSET 196U
#define RPMB_HMAC_INPUT_OFFSET 228U
#define RPMB_HMAC_INPUT_SIZE 284U
#define RPMB_MAX_PARTITIONS 32U
#define RPMB_BLOCKS_PER_PARTITION 512U
#define RPMB_BLOCK_COUNT \
	(RPMB_MAX_PARTITIONS * RPMB_BLOCKS_PER_PARTITION)

#define REG_INTERRUPT_STATUS (UFS_BASE + 0x20)
#define REG_HOST_CONTROLLER_STATUS (UFS_BASE + 0x30)
#define REG_UTP_TRANSFER_REQ_LIST_BASE_L (UFS_BASE + 0x50)
#define REG_UTP_TRANSFER_REQ_LIST_BASE_H (UFS_BASE + 0x54)
#define REG_UTP_TRANSFER_REQ_DOOR_BELL (UFS_BASE + 0x58)
#define REG_UIC_COMMAND (UFS_BASE + 0x90)
#define REG_UIC_ARG1 (UFS_BASE + 0x94)
#define REG_UIC_ARG2 (UFS_BASE + 0x98)
#define REG_UIC_ARG3 (UFS_BASE + 0x9c)

#define UIC_CMD_DME_GET 1
#define UIC_CMD_DME_SET 2
#define UIC_CMD_DME_PEER_SET 4
#define UIC_CMD_DME_LINK_STARTUP 0x16

#define DME_ATTR_CAPACITY 128

#define UPIU_QUERY_OPCODE_READ_DESC 0x01
#define UPIU_QUERY_OPCODE_WRITE_DESC 0x02
#define UPIU_QUERY_OPCODE_READ_ATTR 0x03
#define UPIU_QUERY_OPCODE_WRITE_ATTR 0x04
#define UPIU_QUERY_OPCODE_READ_FLAG 0x05
#define UPIU_QUERY_OPCODE_SET_FLAG 0x06
#define UPIU_QUERY_OPCODE_CLEAR_FLAG 0x07
#define UPIU_QUERY_OPCODE_TOGGLE_FLAG 0x08

#define UPIU_TRANSACTION_NOP_OUT 0x00
#define UPIU_TRANSACTION_COMMAND 0x01
#define UPIU_TRANSACTION_QUERY_REQ 0x16
#define UPIU_TRANSACTION_NOP_IN 0x20

#define UFS_STD_READ_REQ 0x01
#define UFS_STD_WRITE_REQ 0x81

#define QUERY_IDN_DEVICE_DESC 0x00
#define QUERY_IDN_CONFIG_DESC 0x01
#define QUERY_IDN_UNIT_DESC 0x02
#define QUERY_IDN_INTERCONNECT_DESC 0x04
#define QUERY_IDN_STRING_DESC 0x05
#define QUERY_IDN_GEOMETRY_DESC 0x07
#define QUERY_IDN_POWER_DESC 0x08

#define QUERY_FLAG_ID_DEVICE_INIT 0x01

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_INQUIRY 0x12
#define SCSI_MODE_SENSE_6 0x1a
#define SCSI_START_STOP_UNIT 0x1b
#define SCSI_READ_CAPACITY_10 0x25
#define SCSI_READ_10 0x28
#define SCSI_WRITE_10 0x2a
#define SCSI_SYNCHRONIZE_CACHE_10 0x35
#define SCSI_UNMAP 0x42
#define SCSI_MODE_SENSE_10 0x5a
#define SCSI_SECURITY_PROTOCOL_IN 0xa2
#define SCSI_SECURITY_PROTOCOL_OUT 0xb5

uint16_t ufs_get_be16(const uint8_t *value);
uint32_t ufs_get_be32(const uint8_t *value);
uint64_t ufs_get_be64(const uint8_t *value);
void ufs_put_be16(uint8_t *value, uint16_t number);
void ufs_put_be32(uint8_t *value, uint32_t number);
uint64_t ufs_utrd_offset_words(uint16_t offset);
uc_err ufs_prdt_write(uc_engine *uc, uint64_t desc_addr,
		      const struct ufs_utrd *utrd,
		      const unsigned char *buffer, uint32_t length);
uc_err ufs_prdt_read(uc_engine *uc, uint64_t desc_addr,
		     const struct ufs_utrd *utrd, unsigned char *buffer,
		     uint32_t length);
uint64_t ufs_lun_block_count(uint8_t lun);
bool ufs_read_lun(uint8_t lun, uint64_t offset, unsigned char *buffer,
		  uint32_t length);
bool ufs_write_lun_overlay(uint8_t lun, uint64_t offset,
			   const unsigned char *buffer, uint32_t length);
bool ufs_discard_lun_overlay(uint8_t lun, uint64_t offset, uint64_t length);
const char *ufs_storage_directory(void);
struct ufs_upiu *ufs_init_response(struct ufs_cmd_desc *desc, uint8_t type,
				   uint32_t data_length);
void ufs_complete_query_response(struct ufs_cmd_desc *desc, uint8_t status);
void ufs_set_query_data_length(struct ufs_upiu *resp, uint32_t length);
void ufs_query_reset(void);
void ufs_descriptors_init(void);
void ufs_complete_query_descriptor(struct ufs_cmd_desc *desc);
void ufs_complete_query_write_descriptor(struct ufs_cmd_desc *desc);
uc_err ufs_handle_query(struct ufs_cmd_desc *desc);
uc_err ufs_complete_scsi_data_in(uc_engine *uc, uint64_t desc_addr,
				 const struct ufs_utrd *utrd,
				 struct ufs_cmd_desc *desc,
				 const unsigned char *data,
				 uint32_t data_length);
uc_err ufs_handle_scsi(uc_engine *uc, uint64_t desc_addr,
			const struct ufs_utrd *utrd,
			struct ufs_cmd_desc *desc);
void ufs_rpmb_reset(void);
uc_err ufs_rpmb_security_in(uc_engine *uc, uint64_t desc_addr,
			    const struct ufs_utrd *utrd,
			    struct ufs_cmd_desc *desc);
uc_err ufs_rpmb_security_out(uc_engine *uc, uint64_t desc_addr,
			     const struct ufs_utrd *utrd,
			     struct ufs_cmd_desc *desc);
uc_err ufs_handle_transfer_request(uc_engine *uc, uint64_t utrd_addr);

#endif

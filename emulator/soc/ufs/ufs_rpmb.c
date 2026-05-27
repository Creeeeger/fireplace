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

#include <stdlib.h>
#include <string.h>

#include <fireplace/soc/sss/sss.h>

#include "ufs/ufs_internal.h"

static unsigned char rpmb_request[RPMB_FRAME_SIZE];
static unsigned char rpmb_blocks[RPMB_BLOCK_COUNT][RPMB_DATA_SIZE];
static uint32_t rpmb_write_counter;
static uint16_t rpmb_result_response;
static uint16_t rpmb_result_code;

void ufs_rpmb_reset(void)
{
	memset(rpmb_request, 0, sizeof(rpmb_request));
	memset(rpmb_blocks, 0, sizeof(rpmb_blocks));
	rpmb_write_counter = 0;
	rpmb_result_response = 0x0300;
	rpmb_result_code = 0;
}

static bool rpmb_calculate_frames_hmac(const unsigned char *frames,
				       size_t frame_count,
				       unsigned char output[32])
{
	unsigned char *input;
	bool result;

	if (frame_count == 0 || frame_count > SIZE_MAX / RPMB_HMAC_INPUT_SIZE)
		return false;
	input = malloc(frame_count * RPMB_HMAC_INPUT_SIZE);
	if (!input)
		return false;
	for (size_t i = 0; i < frame_count; i++)
		memcpy(input + i * RPMB_HMAC_INPUT_SIZE,
		       frames + i * RPMB_FRAME_SIZE + RPMB_HMAC_INPUT_OFFSET,
		       RPMB_HMAC_INPUT_SIZE);
	result = sss_calculate_rpmb_hmac(input,
					 frame_count * RPMB_HMAC_INPUT_SIZE,
					 output);
	free(input);
	return result;
}

static void rpmb_sign_frames(unsigned char *frames, size_t frame_count)
{
	unsigned char hmac[32];

	if (rpmb_calculate_frames_hmac(frames, frame_count, hmac))
		memcpy(frames + (frame_count - 1) * RPMB_FRAME_SIZE +
		       RPMB_HMAC_OFFSET, hmac, sizeof(hmac));
}

static bool rpmb_verify_frames(const unsigned char *frames, size_t frame_count)
{
	unsigned char hmac[32];

	return rpmb_calculate_frames_hmac(frames, frame_count, hmac) &&
	       memcmp(hmac, frames + (frame_count - 1) * RPMB_FRAME_SIZE +
			    RPMB_HMAC_OFFSET, sizeof(hmac)) == 0;
}

static void rpmb_build_response(unsigned char *frames, size_t frame_count)
{
	uint16_t request = ufs_get_be16(rpmb_request + 510);
	uint16_t address = ufs_get_be16(rpmb_request + 504);
	uint16_t requested_count = ufs_get_be16(rpmb_request + 506);

	memset(frames, 0, frame_count * RPMB_FRAME_SIZE);
	switch (request) {
	case 2:
		memcpy(frames + RPMB_NONCE_OFFSET,
		       rpmb_request + RPMB_NONCE_OFFSET, 16);
		ufs_put_be32(frames + 500, rpmb_write_counter);
		ufs_put_be16(frames + 510, 0x0200);
		break;
	case 4:
		for (size_t i = 0; i < frame_count; i++) {
			unsigned char *frame = frames + i * RPMB_FRAME_SIZE;

			if ((uint32_t)address + i < RPMB_BLOCK_COUNT)
				memcpy(frame + RPMB_DATA_OFFSET,
				       rpmb_blocks[address + i], RPMB_DATA_SIZE);
			else
				ufs_put_be16(frame + 508, 4);
			memcpy(frame + RPMB_NONCE_OFFSET,
			       rpmb_request + RPMB_NONCE_OFFSET, 16);
			ufs_put_be16(frame + 504, address);
			ufs_put_be16(frame + 506,
				 requested_count ? requested_count :
				 (uint16_t)frame_count);
			ufs_put_be16(frame + 510, 0x0400);
		}
		break;
	case 5:
		ufs_put_be32(frames + 500, rpmb_write_counter);
		ufs_put_be16(frames + 508, rpmb_result_code);
		ufs_put_be16(frames + 510, rpmb_result_response);
		break;
	default:
		ufs_put_be16(frames + 508, 1);
		break;
	}
	rpmb_sign_frames(frames, frame_count);
}

uc_err ufs_rpmb_security_in(uc_engine *uc, uint64_t desc_addr,
					  const struct ufs_utrd *utrd,
					  struct ufs_cmd_desc *desc)
{
	uint8_t *cdb = &desc->command_upiu.tsf[4];
	uint32_t allocation_length = ufs_get_be32(&cdb[6]);
	unsigned char *data;
	uc_err err;

	data = calloc(allocation_length ? allocation_length : 1, 1);
	if (!data)
		return UC_ERR_NOMEM;
	if (cdb[1] == 0xec && allocation_length >= RPMB_FRAME_SIZE)
		rpmb_build_response(data, allocation_length / RPMB_FRAME_SIZE);
	err = ufs_complete_scsi_data_in(uc, desc_addr, utrd, desc, data,
				    allocation_length);
	free(data);
	return err;
}

uc_err ufs_rpmb_security_out(uc_engine *uc, uint64_t desc_addr,
					   const struct ufs_utrd *utrd,
					   struct ufs_cmd_desc *desc)
{
	uint8_t *cdb = &desc->command_upiu.tsf[4];
	uint32_t transfer_length = ufs_get_be32(&cdb[6]);
	unsigned char *data;
	uc_err err = UC_ERR_OK;

	data = malloc(transfer_length ? transfer_length : 1);
	if (!data)
		return UC_ERR_NOMEM;
	if (transfer_length != 0)
		err = ufs_prdt_read(uc, desc_addr, utrd, data, transfer_length);
	if (err == UC_ERR_OK && cdb[1] == 0xec &&
	    transfer_length >= RPMB_FRAME_SIZE) {
		uint16_t request;
		uint16_t address;
		uint16_t block_count;
		uint32_t counter;
		size_t frame_count = transfer_length / RPMB_FRAME_SIZE;

		memcpy(rpmb_request, data, RPMB_FRAME_SIZE);
		request = ufs_get_be16(rpmb_request + 510);
		address = ufs_get_be16(rpmb_request + 504);
		block_count = ufs_get_be16(rpmb_request + 506);
		counter = ufs_get_be32(rpmb_request + 500);
		if (request == 1) {
			rpmb_result_response = 0x0100;
			rpmb_result_code = 0;
		} else if (request == 3) {
			rpmb_result_response = 0x0300;
			rpmb_result_code = 0;
			if (block_count == 0 || block_count != frame_count ||
			    (uint32_t)address + block_count > RPMB_BLOCK_COUNT)
				rpmb_result_code = 4;
			else if (counter != rpmb_write_counter)
				rpmb_result_code = 3;
			else if (!rpmb_verify_frames(data, frame_count))
				rpmb_result_code = 2;
			else {
				for (size_t i = 0; i < frame_count; i++)
					memcpy(rpmb_blocks[address + i],
					       data + i * RPMB_FRAME_SIZE +
					       RPMB_DATA_OFFSET, RPMB_DATA_SIZE);
				rpmb_write_counter++;
			}
		}
	}
	free(data);
	if (err != UC_ERR_OK)
		return err;
	ufs_init_response(desc, 0x21, 0);
	return UC_ERR_OK;
}



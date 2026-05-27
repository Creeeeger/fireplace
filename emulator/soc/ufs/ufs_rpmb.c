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


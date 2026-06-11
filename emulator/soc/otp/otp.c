
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/otp/otp.h>

#include "bootchain/bootchain_internal.h"

#define OTP_READ_DATA0 (OTP_CON_TOP_BASE + UINT64_C(0x108))
#define OTP_READ_DATA1 (OTP_CON_TOP_BASE + UINT64_C(0x10c))
#define OTP_CONTROL (OTP_CON_TOP_BASE + UINT64_C(0x110))
#define OTP_READ_ADDRESS (OTP_CON_TOP_BASE + UINT64_C(0x118))
#define OTP_STATUS (OTP_CON_TOP_BASE + UINT64_C(0x11c))
#define OTP_INIT_REQUEST UINT32_C(0x1)
#define OTP_READ_REQUEST UINT32_C(0x2)
#define OTP_PROGRAM_REQUEST UINT32_C(0x4)
#define OTP_FINISH_REQUEST UINT32_C(0x8)
#define OTP_INIT_COMPLETE UINT32_C(0x1)
#define OTP_READ_COMPLETE UINT32_C(0x2)
#define OTP_PROGRAM_COMPLETE UINT32_C(0x4)
#define OTP_FINISH_COMPLETE UINT32_C(0x8)
#define OTP_ERROR UINT32_C(0x80)
#define OTP_COMPLETION_MASK \
	(OTP_INIT_COMPLETE | OTP_READ_COMPLETE | OTP_PROGRAM_COMPLETE | \
	 OTP_FINISH_COMPLETE | OTP_ERROR)
#define OTP_FUSE_ROW_MASK UINT32_C(0x7fe0)
#define OTP_PROGRAM_COMMAND_MASK UINT32_C(0x80000000)
#define OTP_FUSE_ROW_SHIFT 5
#define OTP_FUSE_ROW_COUNT ((OTP_FUSE_ROW_MASK >> OTP_FUSE_ROW_SHIFT) + 1)
#define OTP_FUSE_PROFILE "otp-fuses.txt"

static bool otp_init_logged;
static bool otp_fuses_loaded;
static bool otp_fuses_load_failed;
static uint32_t otp_fuse_rows[OTP_FUSE_ROW_COUNT];
static bool otp_fuse_rows_present[OTP_FUSE_ROW_COUNT];
static uint32_t otp_program_latches[OTP_FUSE_ROW_COUNT];

int otp_init(struct uc_struct *uc)
{
	(void)uc;
	otp_init_logged = false;
	otp_fuses_loaded = false;
	otp_fuses_load_failed = false;
	memset(otp_fuse_rows, 0, sizeof(otp_fuse_rows));
	memset(otp_fuse_rows_present, 0, sizeof(otp_fuse_rows_present));
	memset(otp_program_latches, 0, sizeof(otp_program_latches));
	return UC_ERR_OK;
}

static bool otp_parse_fuse_line(char *line, unsigned int line_no)
{
	unsigned long row;
	unsigned long value;
	char *cursor = line;
	char *end;
	size_t index;

	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
		cursor++;
	if (*cursor == '\0' || *cursor == '#')
		return true;

	errno = 0;
	row = strtoul(cursor, &end, 0);
	if (errno != 0 || end == cursor || row > OTP_FUSE_ROW_MASK ||
	    (row & ((UINT32_C(1) << OTP_FUSE_ROW_SHIFT) - 1)) != 0) {
		fprintf(stderr, "[OTP-CON] invalid row in %s:%u\n",
			OTP_FUSE_PROFILE, line_no);
		return false;
	}
	cursor = end;
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	errno = 0;
	value = strtoul(cursor, &end, 0);
	if (errno != 0 || end == cursor || value > UINT32_MAX) {
		fprintf(stderr, "[OTP-CON] invalid value in %s:%u\n",
			OTP_FUSE_PROFILE, line_no);
		return false;
	}
	while (*end == ' ' || *end == '\t' || *end == '\r')
		end++;
	if (*end != '\0' && *end != '#') {
		fprintf(stderr, "[OTP-CON] trailing data in %s:%u\n",
			OTP_FUSE_PROFILE, line_no);
		return false;
	}

	index = row >> OTP_FUSE_ROW_SHIFT;
	if (otp_fuse_rows_present[index]) {
		fprintf(stderr, "[OTP-CON] duplicate row 0x%04lx in %s:%u\n",
			row, OTP_FUSE_PROFILE, line_no);
		return false;
	}
	otp_fuse_rows[index] = (uint32_t)value;
	otp_fuse_rows_present[index] = true;
	return true;
}

static bool otp_load_fuses(void)
{
	unsigned int line_no = 1;
	char *contents;
	size_t size;
	uc_err err;
	char *line;

	if (otp_fuses_loaded)
		return true;
	if (otp_fuses_load_failed)
		return false;

	err = bootchain_read_profile_file(OTP_FUSE_PROFILE, &contents, &size);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "[OTP-CON] failed to load %s\n",
			OTP_FUSE_PROFILE);
		otp_fuses_load_failed = true;
		return false;
	}
	(void)size;
	line = contents;
	while (*line != '\0') {
		char *next = strchr(line, '\n');

		if (next != NULL)
			*next = '\0';
		if (!otp_parse_fuse_line(line, line_no)) {
			free(contents);
			otp_fuses_load_failed = true;
			return false;
		}
		if (next == NULL)
			break;
		line = next + 1;
		line_no++;
	}
	free(contents);
	otp_fuses_loaded = true;
	printf("[OTP-CON] loaded fuse rows from %s\n", OTP_FUSE_PROFILE);
	return true;
}

static void otp_update_status(uc_engine *uc, uint32_t clear, uint32_t set)
{
	uint32_t status = 0;

	if (uc_mem_read(uc, OTP_STATUS, &status, sizeof(status)) !=
	    UC_ERR_OK)
		return;
	status = (status & ~clear) | set;
	(void)uc_mem_write(uc, OTP_STATUS, &status, sizeof(status));
}

static bool otp_complete_read(uc_engine *uc)
{
	uint32_t address = 0;
	uint32_t data = 0;
	size_t index;

	if (uc_mem_read(uc, OTP_READ_ADDRESS, &address, sizeof(address)) !=
	    UC_ERR_OK)
		return false;
	address &= OTP_FUSE_ROW_MASK;
	if (!otp_load_fuses())
		return false;
	index = address >> OTP_FUSE_ROW_SHIFT;
	/* A sparse profile records programmed rows; all other rows are blank. */
	if (otp_fuse_rows_present[index])
		data = otp_fuse_rows[index];
	if (uc_mem_write(uc, OTP_READ_DATA0, &data, sizeof(data)) != UC_ERR_OK ||
	    uc_mem_write(uc, OTP_READ_DATA1, &data, sizeof(data)) != UC_ERR_OK)
		return false;

	printf("[OTP-CON] read fuse row 0x%04" PRIx32 "\n", address);
	return true;
}


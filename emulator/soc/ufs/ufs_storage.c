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

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ufs/ufs_internal.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char lun_dump_dir[PATH_MAX];

static const lu_capacity_t lu_capacities[8] = {
    { .last_lba = 31234048, .block_size = 4096 },
    { .last_lba = 1024, .block_size = 4096 },
    { .last_lba = 1024, .block_size = 4096 },
	{ .last_lba = 2048, .block_size = 4096 },
	{ .last_lba = 4096, .block_size = 4096 }
};

struct lun_overlay {
	uint8_t lun;
	uint64_t offset;
	uint64_t size;
	bool discarded;
	unsigned char *data;
	struct lun_overlay *next;
};

static struct lun_overlay *overlay_head;
static struct lun_overlay *overlay_tail;

static void clear_overlays(void)
{
	struct lun_overlay *overlay = overlay_head;

	while (overlay) {
		struct lun_overlay *next = overlay->next;

		free(overlay->data);
		free(overlay);
		overlay = next;
	}
	overlay_head = NULL;
	overlay_tail = NULL;
}

void ufs_set_lun_dump_dir(const char *lun_dir)
{
	clear_overlays();
	if (!lun_dir || lun_dir[0] == '\0') {
		lun_dump_dir[0] = '\0';
		return;
	}
	snprintf(lun_dump_dir, sizeof(lun_dump_dir), "%s", lun_dir);
}

static int make_lun_path(char *path, size_t path_size, uint8_t lun)
{
	int length;

	length = snprintf(path, path_size, "%s/lun%u.img", lun_dump_dir, lun);
	if (length < 0 || (size_t)length >= path_size)
		return -1;
	return 0;
}

static bool lun_file_size(uint8_t lun, uint64_t *size)
{
	char path[PATH_MAX];
	long file_size;
	FILE *file;

	*size = 0;
	if (lun_dump_dir[0] == '\0' ||
	    make_lun_path(path, sizeof(path), lun) != 0)
		return false;
	file = fopen(path, "rb");
	if (!file)
		return false;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}
	file_size = ftell(file);
	fclose(file);
	if (file_size < 0)
		return false;
	*size = (uint64_t)file_size;
	return true;
}

uint64_t ufs_lun_block_count(uint8_t lun)
{
	uint64_t size;

	if (lun_file_size(lun, &size))
		return (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
	if (lun < UFS_LUN_COUNT)
		return lu_capacities[lun].last_lba;
	return 0;
}

static void apply_overlay(uint8_t lun, uint64_t offset, unsigned char *buffer,
			  uint32_t length)
{
	uint64_t end = offset + length;

	for (struct lun_overlay *overlay = overlay_head; overlay;
	     overlay = overlay->next) {
		uint64_t overlay_end;
		uint64_t copy_start;
		uint64_t copy_end;

		if (overlay->lun != lun)
			continue;
		overlay_end = overlay->offset + overlay->size;
		if (overlay_end <= offset || overlay->offset >= end)
			continue;
		copy_start = overlay->offset > offset ? overlay->offset : offset;
		copy_end = overlay_end < end ? overlay_end : end;
		if (overlay->discarded) {
			memset(buffer + (copy_start - offset), 0,
			       (size_t)(copy_end - copy_start));
		} else {
			memcpy(buffer + (copy_start - offset),
			       overlay->data + (copy_start - overlay->offset),
			       (size_t)(copy_end - copy_start));
		}
	}
}

bool ufs_read_lun(uint8_t lun, uint64_t offset, unsigned char *buffer,
		     uint32_t length)
{
	char path[PATH_MAX];
	size_t count = 0;
	FILE *file;

	memset(buffer, 0, length);
	if (lun_dump_dir[0] == '\0') {
		printf("[UFS] READ_10 requested without a LUN dump directory\n");
		return false;
	}
	if (make_lun_path(path, sizeof(path), lun) != 0) {
		printf("[UFS] LUN path is too long for LU%u\n", lun);
		return false;
	}
	file = fopen(path, "rb");
	if (!file) {
		printf("[UFS] Could not open %s: %s\n", path, strerror(errno));
		return false;
	}
	if (offset > LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0) {
		printf("[UFS] Failed to seek %s to 0x%" PRIx64 ": %s\n", path,
		       offset, strerror(errno));
		fclose(file);
		return false;
	}
	count = fread(buffer, 1, length, file);
	fclose(file);
	if (count != length)
		printf("[UFS] Short read from LU%u: got %zu, expected %u; "
		       "zero-filling the rest\n", lun, count, length);
	apply_overlay(lun, offset, buffer, length);
	return true;
}

bool ufs_write_lun_overlay(uint8_t lun, uint64_t offset,
			      const unsigned char *buffer, uint32_t length)
{
	struct lun_overlay *overlay;

	overlay = calloc(1, sizeof(*overlay));
	if (!overlay)
		return false;
	overlay->data = malloc(length);
	if (!overlay->data) {
		free(overlay);
		return false;
	}
	memcpy(overlay->data, buffer, length);
	overlay->lun = lun;
	overlay->offset = offset;
	overlay->size = length;
	if (overlay_tail)
		overlay_tail->next = overlay;
	else
		overlay_head = overlay;
	overlay_tail = overlay;
	return true;
}

bool ufs_discard_lun_overlay(uint8_t lun, uint64_t offset, uint64_t length)
{
	struct lun_overlay *overlay;

	if (length == 0)
		return true;
	overlay = calloc(1, sizeof(*overlay));
	if (!overlay)
		return false;
	overlay->lun = lun;
	overlay->offset = offset;
	overlay->size = length;
	overlay->discarded = true;
	if (overlay_tail)
		overlay_tail->next = overlay;
	else
		overlay_head = overlay;
	overlay_tail = overlay;
	return true;
}

const char *ufs_storage_directory(void)
{
	return lun_dump_dir;
}


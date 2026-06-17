
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootchain/bootchain_internal.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *profile_directory;
static const char *lun_directory;

struct ufs_boot_image {
	const char *filename;
	const char *name;
	unsigned int lun;
	uint64_t offset;
	uint64_t size;
};

static const struct ufs_boot_image ufs_boot_images[] = {
	{FWBL1_IMAGE, "FWBL1", 1, UINT64_C(0x000000), FWBL1_IMAGE_SIZE},
	{EPBL_IMAGE, "EPBL", 1, UINT64_C(0x003000), EPBL_IMAGE_SIZE},
	{BL2_IMAGE, "BL2", 1, UINT64_C(0x016000), BL2_IMAGE_SIZE},
	{LK_IMAGE, "LK", 1, UINT64_C(0x0db000), LK_IMAGE_SIZE},
	{EL3_IMAGE, "EL3 monitor", 1, UINT64_C(0x35b000), EL3_IMAGE_SIZE},
};

void bootchain_images_configure(const char *profile, const char *lun)
{
	profile_directory = profile;
	lun_directory = lun;
}

static uc_err bootchain_make_profile_path(char *path, size_t path_size,
					  const char *filename)
{
	int length;

	length = snprintf(path, path_size, "%s/%s", profile_directory, filename);
	if (length < 0 || (size_t)length >= path_size) {
		fprintf(stderr, "Bootchain image path is too long: %s\n", filename);
		return UC_ERR_ARG;
	}
	return UC_ERR_OK;
}

static uc_err bootchain_make_lun_path(char *path, size_t path_size,
				      unsigned int lun)
{
	int length;

	length = snprintf(path, path_size, "%s/lun%u.img", lun_directory, lun);
	if (length < 0 || (size_t)length >= path_size) {
		fprintf(stderr, "UFS LUN path is too long: lun%u.img\n", lun);
		return UC_ERR_ARG;
	}
	return UC_ERR_OK;
}

static const struct ufs_boot_image *bootchain_find_ufs_image(
	const char *filename)
{
	for (size_t i = 0; i < ARRAY_SIZE(ufs_boot_images); i++)
		if (strcmp(ufs_boot_images[i].filename, filename) == 0)
			return &ufs_boot_images[i];
	return NULL;
}

static uc_err bootchain_load_file_range(uc_engine *uc, FILE *file,
					const char *label,
					uint64_t file_offset,
					uint64_t address,
					uint64_t expected_size)
{
	unsigned char buffer[4096];
	uint64_t offset = 0;
	uc_err err = UC_ERR_OK;

	if (file_offset > LONG_MAX) {
		fprintf(stderr, "Bootchain image offset is too large: 0x%" PRIx64
			"\n", file_offset);
		return UC_ERR_ARG;
	}
	if (fseek(file, (long)file_offset, SEEK_SET) != 0) {
		fprintf(stderr, "Failed to seek %s to 0x%" PRIx64 ": %s\n",
			label, file_offset, strerror(errno));
		return UC_ERR_HANDLE;
	}
	while (offset < expected_size) {
		size_t requested = sizeof(buffer);
		size_t count;

		if (expected_size - offset < requested)
			requested = (size_t)(expected_size - offset);
		count = fread(buffer, 1, requested, file);
		if (count != requested) {
			fprintf(stderr,
				"Bootchain image %s is truncated at 0x%" PRIx64
				" bytes (expected 0x%" PRIx64 ")\n",
				label, offset + count, expected_size);
			err = UC_ERR_ARG;
			break;
		}
		err = uc_mem_write(uc, address + offset, buffer, count);
		if (err != UC_ERR_OK) {
			fprintf(stderr, "Failed to load %s at 0x%" PRIx64 ": %s\n",
				label, address + offset, uc_strerror(err));
			break;
		}
		offset += count;
	}
	return err;
}


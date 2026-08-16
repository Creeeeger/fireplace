#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_internal.h"
#include "ufs/ufs_internal.h"

/* Entering Odin mode, immediately before LK initializes the USB transport. */
#define LK_ODIN_ENTRY_ADDR UINT64_C(0xe806b378)

/* The UDF after LK's non-returning Odin entry is a convenient return trap. */
#define LK_ODIN_INJECT_RETURN_ADDR UINT64_C(0xe806b394)

/* LK's pit_flash_binary wrapper used by the completed download worker. */
#define LK_ODIN_FLASH_BINARY_ADDR UINT64_C(0xe80810b8)

/* These worker pools only exist to move received USB chunks to the flasher. */
#define LK_PARALLEL_DOWNLOAD_INIT_ADDR UINT64_C(0xe806c620)
#define LK_COMPRESSED_DOWNLOAD_INIT_ADDR UINT64_C(0xe806c838)

/* The download arena selected by LK's V3 SMP path. */
#define LK_ODIN_BUFFER_ADDR UINT64_C(0xa2200000)
#define LK_ODIN_SBOOT_SIZE (4U * 1024U * 1024U)
#define LK_ODIN_NAME_ADDR (LK_ODIN_BUFFER_ADDR + LK_ODIN_SBOOT_SIZE)

enum odin_inject_state {
	ODIN_INJECT_DISABLED,
	ODIN_INJECT_READY,
	ODIN_INJECT_RUNNING,
	ODIN_INJECT_DONE,
};

struct odin_inject {
	unsigned char *image;
	size_t image_size;
	uint64_t initial_lun_bytes;
	enum odin_inject_state state;
};

static struct odin_inject odin;

static void odin_fail(uc_engine *uc, const char *message, uc_err err);

static void odin_fail(uc_engine *uc, const char *message, uc_err err)
{
	if (err == UC_ERR_OK)
		fprintf(stderr, "[Odin] %s\n", message);
	else
		fprintf(stderr, "[Odin] %s: %s\n", message,
			uc_strerror(err));
	bootchain_fail(uc);
}

static uc_err odin_load_sboot(const char *path)
{
	long size;
	FILE *file;

	file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "[Odin] failed to open %s: %s\n", path,
			strerror(errno));
		return UC_ERR_HANDLE;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
	    fseek(file, 0, SEEK_SET) != 0) {
		fprintf(stderr, "[Odin] failed to size %s: %s\n", path,
			strerror(errno));
		fclose(file);
		return UC_ERR_HANDLE;
	}
	if ((uint64_t)size != LK_ODIN_SBOOT_SIZE) {
		fprintf(stderr,
			"[Odin] BOOTLOADER must be exactly %u bytes; %s is %ld bytes\n",
			LK_ODIN_SBOOT_SIZE, path, size);
		fclose(file);
		return UC_ERR_ARG;
	}
	odin.image = malloc((size_t)size);
	if (!odin.image) {
		fclose(file);
		return UC_ERR_NOMEM;
	}
	if (fread(odin.image, 1, (size_t)size, file) != (size_t)size) {
		fprintf(stderr, "[Odin] failed to read %s: %s\n", path,
			ferror(file) ? strerror(errno) : "short read");
		free(odin.image);
		odin.image = NULL;
		fclose(file);
		return UC_ERR_HANDLE;
	}
	if (fclose(file) != 0) {
		fprintf(stderr, "[Odin] failed to close %s: %s\n", path,
			strerror(errno));
		free(odin.image);
		odin.image = NULL;
		return UC_ERR_HANDLE;
	}
	odin.image_size = (size_t)size;
	odin.state = ODIN_INJECT_READY;
	printf("[Odin] staged %s as a completed BOOTLOADER download (%zu bytes)\n",
	       path, odin.image_size);
	return UC_ERR_OK;
}

static void lk_odin_inject_cb(uc_engine *uc, uint64_t address, uint32_t size,
			      void *user_data)
{
	static const char partition_name[] = "BOOTLOADER";
	uint64_t image_address = LK_ODIN_BUFFER_ADDR;
	uint64_t image_size = LK_ODIN_SBOOT_SIZE;
	uint64_t name_address = LK_ODIN_NAME_ADDR;
	uint64_t final_chunk = 1;
	uint64_t return_address = LK_ODIN_INJECT_RETURN_ADDR;
	uint64_t flash_address = LK_ODIN_FLASH_BINARY_ADDR;
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK ||
	    odin.state != ODIN_INJECT_READY)
		return;

	err = uc_mem_write(uc, image_address, odin.image, odin.image_size);
	if (err != UC_ERR_OK) {
		odin_fail(uc, "could not fill LK's download buffer", err);
		return;
	}
	err = uc_mem_write(uc, name_address, partition_name,
			   sizeof(partition_name));
	if (err != UC_ERR_OK) {
		odin_fail(uc, "could not write the BOOTLOADER name", err);
		return;
	}

	odin.initial_lun_bytes = ufs_lun_overlay_write_bytes(1);
	odin.state = ODIN_INJECT_RUNNING;
	printf("[Odin] injected %zu bytes at 0x%" PRIx64
	       "; invoking LK's completed-file flash path\n",
	       odin.image_size, image_address);

	err = uc_reg_write(uc, UC_ARM64_REG_X0, &name_address);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_X1, &image_address);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_X2, &image_size);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_X3, &final_chunk);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_X30, &return_address);
	if (err == UC_ERR_OK)
		err = uc_reg_write(uc, UC_ARM64_REG_PC, &flash_address);
	if (err != UC_ERR_OK)
		odin_fail(uc, "could not enter LK's completed-file handler", err);
}

static void lk_odin_complete_cb(uc_engine *uc, uint64_t address, uint32_t size,
				void *user_data)
{
	uint64_t raw_result = 0;
	uint64_t total_lun_bytes;
	uint64_t written;
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK ||
	    odin.state != ODIN_INJECT_RUNNING)
		return;
	err = uc_reg_read(uc, UC_ARM64_REG_X0, &raw_result);
	if (err != UC_ERR_OK) {
		odin_fail(uc, "could not read LK's flash result", err);
		return;
	}
	if ((int32_t)raw_result != 0) {
		fprintf(stderr,
			"[Odin] LK rejected the BOOTLOADER image with result %" PRId32
			" (0x%08" PRIx32 ")\n",
			(int32_t)raw_result, (uint32_t)raw_result);
		bootchain_fail(uc);
		return;
	}

	total_lun_bytes = ufs_lun_overlay_write_bytes(1);
	written = total_lun_bytes >= odin.initial_lun_bytes ?
		total_lun_bytes - odin.initial_lun_bytes : 0;
	if (written < odin.image_size) {
		fprintf(stderr,
			"[Odin] LK returned success, but LU1 only received 0x%" PRIx64
			" overlay bytes\n",
			written);
		bootchain_fail(uc);
		return;
	}

	odin.state = ODIN_INJECT_DONE;
	printf("[Odin] LK accepted and flashed BOOTLOADER; LU1 received "
	       "0x%" PRIx64 " overlay bytes\n",
	       written);
	bootchain_mark_complete(uc);
}

static void lk_odin_skip_worker_init_cb(uc_engine *uc, uint64_t address,
					uint32_t size, void *user_data)
{
	uint64_t result = 0;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK ||
	    odin.state != ODIN_INJECT_READY)
		return;
	if (uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK ||
	    !bootchain_return_to_link(uc))
		odin_fail(uc, "could not skip the USB download worker", UC_ERR_OK);
}

uc_err lk_odin_init(uc_engine *uc, const char *sboot_path)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(lk_odin_skip_worker_init_cb,
				    LK_PARALLEL_DOWNLOAD_INIT_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_odin_skip_worker_init_cb,
				    LK_COMPRESSED_DOWNLOAD_INIT_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_odin_inject_cb, LK_ODIN_ENTRY_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_odin_complete_cb,
				    LK_ODIN_INJECT_RETURN_ADDR),
	};
	uc_err err;

	free(odin.image);
	memset(&odin, 0, sizeof(odin));
	if (!sboot_path)
		return UC_ERR_OK;
	err = odin_load_sboot(sboot_path);
	if (err != UC_ERR_OK)
		return err;
	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

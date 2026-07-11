
#include <stdio.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_internal.h"

struct lk_runtime_patch {
	const char *name;
	uint64_t address;
	const unsigned char *bytes;
	size_t size;
	bool headless_only;
};

#define PATCH_BYTES(...) ((const unsigned char []){__VA_ARGS__})

static bool headless_mode;

void lk_patches_configure(bool headless)
{
	headless_mode = headless;
}

uc_err lk_apply_runtime_patches(uc_engine *uc)
{
	const struct lk_runtime_patch patches[] = {
		{
			"return from PMU reset helper",
			UINT64_C(0xe8012d18),
			PATCH_BYTES(0xc0, 0x03, 0x5f, 0xd6),
			4,
		},
		{
			"return from PMU reset loop",
			UINT64_C(0xe8012d28),
			PATCH_BYTES(0xc0, 0x03, 0x5f, 0xd6),
			4,
		},
		{
			"resume bootstrap after charger-logo path",
			UINT64_C(0xe800288c),
			PATCH_BYTES(0x83, 0x52, 0x00, 0x14),
			4,
		},
		{
			"exit bootstrap thread through scheduler",
			UINT64_C(0xe80172d8),
			PATCH_BYTES(0x76, 0x60, 0x00, 0x14),
			4,
		},
		{
			"battery voltage wrapper",
			UINT64_C(0xe80b9188),
			PATCH_BYTES(0x80, 0xe2, 0x81, 0x52,
				    0xc0, 0x03, 0x5f, 0xd6),
			8,
		},
		{
			"battery percentage wrapper",
			UINT64_C(0xe80b9168),
			PATCH_BYTES(0x80, 0x0c, 0x80, 0x52,
				    0xc0, 0x03, 0x5f, 0xd6),
			8,
		},
		{
			"skip display probe in headless mode",
			UINT64_C(0xe80b4a78),
			PATCH_BYTES(0x00, 0x00, 0x80, 0x52,
				    0xc0, 0x03, 0x5f, 0xd6),
			8,
			true,
		},
		{
			"skip boot-logo rendering in headless mode",
			UINT64_C(0xe8031ee4),
			PATCH_BYTES(0x1f, 0x20, 0x03, 0xd5),
			4,
			true,
		},
	};
	uc_err err;

	for (size_t i = 0; i < ARRAY_SIZE(patches); i++) {
		const struct lk_runtime_patch *patch = &patches[i];

		if (patch->headless_only && !headless_mode)
			continue;

		err = uc_mem_write(uc, patch->address, patch->bytes, patch->size);
		if (err != UC_ERR_OK) {
			fprintf(stderr, "[LK] failed to apply patch '%s': %s\n",
				patch->name, uc_strerror(err));
			return err;
		}
	}

	printf("[LK] applied UFS boot runtime patches\n");
	return UC_ERR_OK;
}

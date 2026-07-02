
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/sha.h>

#include "bootchain/bootchain_internal.h"

#define EPBL_CRYPTO_STATUS_ADDR UINT64_C(0x020276c0)

static bool receive_logged;
static bool hash_logged;

bool fwbl1_receive_epbl(uc_engine *uc, uint64_t destination,
			uint64_t capacity)
{
	uc_err err;

	if (destination != EPBL_LOAD_ADDR || capacity < EPBL_IMAGE_SIZE) {
		fprintf(stderr, "[FWBL1] invalid EPBL storage request"
			" destination=0x%" PRIx64 " capacity=0x%" PRIx64 "\n",
			destination, capacity);
		bootchain_fail(uc);
		return false;
	}
	err = bootchain_load_image(uc, EPBL_IMAGE, destination, EPBL_IMAGE_SIZE);
	if (err != UC_ERR_OK) {
		bootchain_fail(uc);
		return false;
	}
	if (!receive_logged) {
		printf("[FWBL1] loaded EPBL from emulated boot media\n");
		receive_logged = true;
	}
	return true;
}

void fwbl1_hash_sha512(uc_engine *uc)
{
	uint64_t output;
	uint64_t input;
	uint64_t length;
	uint64_t result = 1;
	unsigned char digest[SHA512_DIGEST_LENGTH];
	unsigned char *buffer;

	if (uc_reg_read(uc, UC_ARM64_REG_X0, &output) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X1, &input) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X2, &length) != UC_ERR_OK ||
	    length == 0 || length > 1024 * 1024) {
		fprintf(stderr, "[FWBL1] invalid SHA-512 request\n");
		bootchain_fail(uc);
		return;
	}
	buffer = malloc((size_t)length);
	if (!buffer) {
		fprintf(stderr, "[FWBL1] failed to allocate SHA-512 input\n");
		bootchain_fail(uc);
		return;
	}
	if (uc_mem_read(uc, input, buffer, (size_t)length) != UC_ERR_OK ||
	    !SHA512(buffer, (size_t)length, digest) ||
	    uc_mem_write(uc, output, digest, sizeof(digest)) != UC_ERR_OK ||
	    uc_reg_write(uc, UC_ARM64_REG_X0, &result) != UC_ERR_OK) {
		fprintf(stderr, "[FWBL1] failed to service SHA-512 request\n");
		free(buffer);
		bootchain_fail(uc);
		return;
	}
	free(buffer);
	if (!hash_logged) {
		printf("[FWBL1] serviced firmware SHA-512 request\n");
		hash_logged = true;
	}
}

static void epbl_handoff_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data)
{
	const uint32_t crypto_success = UINT32_C(0x43435553);
	uc_err err;

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_FWBL1)
		return;
	err = bootchain_decrypt_epbl_image(uc);
	if (err == UC_ERR_OK)
		err = uc_mem_write(uc, EPBL_CRYPTO_STATUS_ADDR, &crypto_success,
				   sizeof(crypto_success));
	if (err != UC_ERR_OK) {
		fprintf(stderr, "[FWBL1] failed to prepare EPBL: %s\n",
			uc_strerror(err));
		bootchain_fail(uc);
		return;
	}
	if (!bootchain_transition(uc, BOOTCHAIN_STAGE_FWBL1,
				  BOOTCHAIN_STAGE_EPBL))
		return;
	printf("[FWBL1] secure authentication bypassed; EPBL decrypted "
	       "in-place\n");
}

uc_err fwbl1_init(uc_engine *uc)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(epbl_handoff_cb, EPBL_ENTRY_ADDR),
	};

	receive_logged = false;
	hash_logged = false;
	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}


#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/evp.h>

#include "bootchain/bootchain_internal.h"

static const unsigned char epbl_key[32] = {
	0x45, 0xf5, 0xa2, 0xf3, 0xf2, 0xe8, 0xc5, 0xc2,
	0x34, 0xdf, 0x48, 0x1a, 0x3d, 0x66, 0x97, 0xfe,
	0x30, 0x24, 0x4c, 0x2f, 0x17, 0x3d, 0xc7, 0x73,
	0xac, 0x6b, 0xdd, 0x24, 0x08, 0x7b, 0x63, 0x8e,
};

static const unsigned char epbl_iv[16] = {
	0x06, 0x9f, 0x3c, 0x80, 0xdf, 0xba, 0xc1, 0xaf,
	0x5d, 0xf0, 0xc5, 0x57, 0x71, 0x2d, 0xfe, 0x38,
};

static const unsigned char el3_key[32] = {
	0x9d, 0x2d, 0xdc, 0x54, 0x10, 0xe6, 0x93, 0x75,
	0x7a, 0x74, 0xea, 0x02, 0x5d, 0x07, 0x17, 0x37,
	0x03, 0xcc, 0xc6, 0x5a, 0x7b, 0x5c, 0x7b, 0x76,
	0x8b, 0x09, 0x9f, 0x5f, 0xaf, 0x06, 0x50, 0x82,
};

static const unsigned char el3_iv[16] = {
	0xc4, 0x21, 0xfa, 0x6f, 0xc8, 0xdb, 0x9c, 0xb2,
	0x2a, 0xe2, 0xb4, 0x3b, 0xb5, 0x67, 0x1d, 0x7f,
};

static uc_err bootchain_read_u32(uc_engine *uc, uint64_t address,
				 uint32_t *value)
{
	return uc_mem_read(uc, address, value, sizeof(*value));
}

static uc_err bootchain_read_u64(uc_engine *uc, uint64_t address,
				 uint64_t *value)
{
	return uc_mem_read(uc, address, value, sizeof(*value));
}

static bool bootchain_range_inside(uint64_t start, uint64_t end,
				   uint64_t image_start, uint64_t image_size)
{
	return start >= image_start && end >= start &&
	       end <= image_start + image_size && ((end - start) & 15) == 0;
}

static uc_err bootchain_aes256_cbc_decrypt_memory(uc_engine *uc,
						  const char *name,
						  uint64_t start,
						  uint64_t end,
						  const unsigned char key[32],
						  const unsigned char iv[16])
{
	EVP_CIPHER_CTX *ctx;
	unsigned char *input;
	unsigned char *output;
	uint64_t size64 = end - start;
	int out_len = 0;
	int final_len = 0;
	uc_err err = UC_ERR_OK;

	if (size64 == 0 || size64 > SIZE_MAX || size64 > INT_MAX) {
		fprintf(stderr, "[Bootchain crypto] invalid %s decrypt size 0x%"
			PRIx64 "\n", name, size64);
		return UC_ERR_ARG;
	}

	input = malloc((size_t)size64);
	output = malloc((size_t)size64 + EVP_MAX_BLOCK_LENGTH);
	if (!input || !output) {
		fprintf(stderr, "[Bootchain crypto] failed to allocate %s "
			"decrypt buffer\n", name);
		err = UC_ERR_NOMEM;
		goto out;
	}

	err = uc_mem_read(uc, start, input, (size_t)size64);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "[Bootchain crypto] failed to read encrypted %s "
			"range: %s\n", name, uc_strerror(err));
		goto out;
	}

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx) {
		err = UC_ERR_NOMEM;
		goto out;
	}
	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1 ||
	    EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
	    EVP_DecryptUpdate(ctx, output, &out_len, input, (int)size64) != 1 ||
	    EVP_DecryptFinal_ex(ctx, output + out_len, &final_len) != 1 ||
	    out_len + final_len != (int)size64) {
		fprintf(stderr, "[Bootchain crypto] failed to decrypt %s range\n",
			name);
		err = UC_ERR_EXCEPTION;
		EVP_CIPHER_CTX_free(ctx);
		goto out;
	}
	EVP_CIPHER_CTX_free(ctx);

	err = uc_mem_write(uc, start, output, (size_t)size64);
	if (err != UC_ERR_OK)
		fprintf(stderr, "[Bootchain crypto] failed to write decrypted %s "
			"range: %s\n", name, uc_strerror(err));
	else
		printf("[Bootchain crypto] decrypted %s range in-place "
		       "0x%" PRIx64 "..0x%" PRIx64 "\n", name, start, end);

out:
	free(input);
	free(output);
	return err;
}

uc_err bootchain_decrypt_epbl_image(uc_engine *uc)
{
	uint64_t start;
	uint64_t end;
	uc_err err;

	err = bootchain_read_u64(uc, UINT64_C(0x02031d28), &start);
	if (err == UC_ERR_OK)
		err = bootchain_read_u64(uc, UINT64_C(0x02031d50), &end);
	if (err != UC_ERR_OK)
		return err;
	if (!bootchain_range_inside(start, end, EPBL_LOAD_ADDR,
				    EPBL_IMAGE_SIZE)) {
		fprintf(stderr, "[Bootchain crypto] invalid EPBL encrypted "
			"range 0x%" PRIx64 "..0x%" PRIx64 "\n", start, end);
		return UC_ERR_ARG;
	}
	return bootchain_aes256_cbc_decrypt_memory(uc, "EPBL", start, end,
						   epbl_key, epbl_iv);
}

uc_err bootchain_decrypt_el3_image(uc_engine *uc)
{
	uint64_t info_offset;
	uint32_t start32;
	uint32_t end32;
	uint64_t start;
	uint64_t end;
	uc_err err;

	err = bootchain_read_u64(uc, EL3_LOAD_ADDR + 4, &info_offset);
	if (err == UC_ERR_OK)
		err = bootchain_read_u32(uc, EL3_LOAD_ADDR + info_offset + 0x0c,
					 &start32);
	if (err == UC_ERR_OK)
		err = bootchain_read_u32(uc, EL3_LOAD_ADDR + info_offset + 0x10,
					 &end32);
	if (err != UC_ERR_OK)
		return err;
	start = start32;
	end = end32;
	if (!bootchain_range_inside(start, end, EL3_LOAD_ADDR,
				    EL3_IMAGE_SIZE)) {
		fprintf(stderr, "[Bootchain crypto] invalid EL3 encrypted range "
			"0x%" PRIx64 "..0x%" PRIx64 "\n", start, end);
		return UC_ERR_ARG;
	}
	return bootchain_aes256_cbc_decrypt_memory(uc, "EL3 monitor", start,
						   end, el3_key, el3_iv);
}


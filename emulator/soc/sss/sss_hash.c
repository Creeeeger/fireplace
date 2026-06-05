#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sss/sss_internal.h"

static const unsigned char sss_rpmb_key[32] = {
	0xfc, 0xf9, 0x1c, 0x3d, 0xfe, 0x0b, 0x60, 0x6c,
	0xd1, 0x25, 0xe4, 0xc4, 0x9a, 0x16, 0xdb, 0x02,
	0xfc, 0xf9, 0x1c, 0x3d, 0xfe, 0x0b, 0x60, 0x6c,
	0xd1, 0x25, 0xe4, 0xc4, 0x9a, 0x16, 0xdb, 0x02,
};

static bool sss_hash_mode(uint32_t mode, const EVP_MD **md, bool *hmac)
{
	switch (mode) {
	case 0x10:
		*md = EVP_sha1();
		*hmac = false;
		return true;
	case 0x11:
		*md = EVP_sha1();
		*hmac = true;
		return true;
	case 0x14:
		*md = EVP_sha256();
		*hmac = false;
		return true;
	case 0x15:
		*md = EVP_sha256();
		*hmac = true;
		return true;
	case 0x1a:
		*md = EVP_sha512();
		*hmac = false;
		return true;
	case 0x1b:
		*md = EVP_sha512();
		*hmac = true;
		return true;
	default:
		return false;
	}
}

static bool sss_digest_plain(const EVP_MD *md, const unsigned char *input,
			     uint32_t length, unsigned char *digest,
			     unsigned int *digest_len)
{
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	bool ok = false;

	if (!ctx)
		return false;
	if (EVP_DigestInit_ex(ctx, md, NULL) == 1 &&
	    EVP_DigestUpdate(ctx, input, length) == 1 &&
	    EVP_DigestFinal_ex(ctx, digest, digest_len) == 1)
		ok = true;
	EVP_MD_CTX_free(ctx);
	return ok;
}

static bool sss_digest_hmac(uc_engine *uc, const EVP_MD *md,
			    const unsigned char *input, uint32_t length,
			    unsigned char *digest, unsigned int *digest_len)
{
	unsigned char key[256];
	int key_len = EVP_MD_get_block_size(md);

	(void)uc;
	if (key_len <= 0 || (size_t)key_len > sizeof(key))
		return false;
	memset(key, 0, sizeof(key));
	memcpy(key, (unsigned char *)sss_regs + (SSS_HASH_KEY - SSS_BASE),
	       (size_t)key_len);
	return HMAC(md, key, key_len, input, length, digest, digest_len) != NULL;
}

bool sss_calculate_rpmb_hmac(const void *data, size_t length,
			     unsigned char output[32])
{
	unsigned int output_len = 0;

	return HMAC(EVP_sha256(), sss_rpmb_key, sizeof(sss_rpmb_key),
		    data, length, output,
		    &output_len) != NULL && output_len == 32;
}

static bool sss_write_hash_digest(uc_engine *uc, const unsigned char *digest,
				  unsigned int digest_len)
{
	unsigned char digest_regs[64];

	if (digest_len > sizeof(digest_regs))
		return false;

	memset(digest_regs, 0, sizeof(digest_regs));
	memcpy(digest_regs, digest, digest_len);
	memcpy((unsigned char *)sss_regs + (SSS_HASH_DIGEST - SSS_BASE),
	       digest_regs, sizeof(digest_regs));
	return uc_mem_write(uc, SSS_HASH_DIGEST, digest_regs,
			    sizeof(digest_regs)) == UC_ERR_OK;
}


#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "sss/sss_internal.h"

static const EVP_CIPHER *sss_select_cipher(uint32_t key_len, bool cbc)
{
	switch (key_len) {
	case 16:
		return cbc ? EVP_aes_128_cbc() : EVP_aes_128_ecb();
	case 24:
		return cbc ? EVP_aes_192_cbc() : EVP_aes_192_ecb();
	case 32:
		return cbc ? EVP_aes_256_cbc() : EVP_aes_256_ecb();
	default:
		return NULL;
	}
}

static const EVP_CIPHER *sss_select_ctr_cipher(uint32_t key_len)
{
	switch (key_len) {
	case 16:
		return EVP_aes_128_ctr();
	case 24:
		return EVP_aes_192_ctr();
	case 32:
		return EVP_aes_256_ctr();
	default:
		return NULL;
	}
}

static bool sss_cipher_key(uint32_t control, const unsigned char **key,
			   uint32_t *key_len)
{
	uint64_t key_reg;

	if (sss_cipher_internal_key_bound) {
		if (!sss_key_manager_key_valid)
			return false;
		*key = sss_key_manager_key;
		*key_len = sss_key_manager_key_len;
		return *key_len == 16 || *key_len == 24 || *key_len == 32;
	}

	if ((control & 0x20) != 0) {
		key_reg = SSS_CIPHER_KEY_256;
		*key_len = 32;
	} else if ((control & 0x10) != 0) {
		key_reg = SSS_CIPHER_KEY_192;
		*key_len = 24;
	} else {
		key_reg = SSS_CIPHER_KEY_128;
		*key_len = 16;
	}

	*key = (const unsigned char *)sss_regs + (key_reg - SSS_BASE);
	return true;
}

bool sss_complete_cipher_pio(uc_engine *uc)
{
	EVP_CIPHER_CTX *ctx;
	const EVP_CIPHER *cipher;
	const unsigned char *key;
	const unsigned char *iv;
	unsigned char output[16];
	const unsigned char *input;
	uint32_t control;
	uint32_t key_len;
	uint32_t word;
	int out_len = 0;
	int final_len = 0;
	bool decrypt;
	bool cbc;
	bool ok = false;

	control = sss_reg(SSS_CIPHER_CONTROL);
	decrypt = (control & 1) != 0;
	cbc = (control & 0x2) != 0;
	if (!sss_cipher_key(control, &key, &key_len))
		return false;
	cipher = sss_select_cipher(key_len, cbc);
	if (!cipher)
		return false;

	input = (const unsigned char *)sss_regs +
		(SSS_CIPHER_PIO_INPUT - SSS_BASE);
	iv = (const unsigned char *)sss_regs +
		(((control & 0x4) != 0 ? SSS_CIPHER_IV_ALT :
		  SSS_CIPHER_IV_BASE) - SSS_BASE);
	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return false;
	if (EVP_CipherInit_ex(ctx, cipher, NULL, key, cbc ? iv : NULL,
			      decrypt ? 0 : 1) == 1 &&
	    EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
	    EVP_CipherUpdate(ctx, output, &out_len, input, sizeof(output)) == 1 &&
	    EVP_CipherFinal_ex(ctx, output + out_len, &final_len) == 1 &&
	    out_len + final_len == (int)sizeof(output))
		ok = true;
	EVP_CIPHER_CTX_free(ctx);
	if (!ok)
		return false;

	for (size_t i = 0; i < sizeof(output) / sizeof(word); i++) {
		memcpy(&word, output + i * sizeof(word), sizeof(word));
		sss_set_reg(uc, SSS_CIPHER_PIO_OUTPUT + i * sizeof(word), word);
	}
	sss_set_reg(uc, SSS_CIPHER_STATUS,
		    sss_reg(SSS_CIPHER_STATUS) |
		    SSS_CIPHER_INPUT_READY | SSS_CIPHER_OUTPUT_READY);
	return true;
}


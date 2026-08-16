#include <stdio.h>
#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include "bootrom/bootrom_internal.h"

/*
 * Exynos9830 BootROM ECDSA ABI
 *
 * The public-key blob contains two 0x44-byte, big-endian fields (X then Y).
 * The signature is supplied as separate 0x44-byte R and S fields.  Each
 * curve consumes the naturally-sized scalar at the end of its field.  The
 * remaining bytes in the 0x20c-byte public-key and 0x200-byte signature
 * containers are metadata/padding handled by the caller.
 */
#define BOOTROM_ECDSA_FIELD_SIZE 0x44
#define BOOTROM_ECDSA_PUBLIC_KEY_SIZE 0x20c
#define BOOTROM_ECDSA_SHA512_SIZE 0x40
#define BOOTROM_ECDSA_MAX_DIGEST_SIZE 0x1000

#define BOOTROM_ECDSA_BAD_CURVE UINT32_C(0x3610)
#define BOOTROM_ECDSA_INVALID_SIGNATURE UINT32_C(0x3611)

#define BOOTROM_ECDSA_DISPATCH_DISABLED UINT32_C(0x1028)
#define BOOTROM_ECDSA_DISPATCH_BAD_KEY UINT32_C(0x1020)
#define BOOTROM_ECDSA_DISPATCH_BAD_DIGEST UINT32_C(0x1021)
#define BOOTROM_ECDSA_DISPATCH_BAD_R UINT32_C(0x1023)
#define BOOTROM_ECDSA_DISPATCH_BAD_S UINT32_C(0x1024)
#define BOOTROM_ECDSA_DISPATCH_BAD_ALGORITHM UINT32_C(0x1026)

struct bootrom_ecdsa_curve {
	const char *openssl_group;
	size_t coordinate_size;
	size_t scalar_offset;
	size_t scalar_size;
};

/* Indexed by the value accepted by ecdsa_verify_digest_core(). */
static const struct bootrom_ecdsa_curve bootrom_ecdsa_curves[] = {
	{"prime256v1", 32, 36, 32},
	{"secp384r1", 48, 20, 48},
	{"secp521r1", 66, 0, 68},
	{"brainpoolP256t1", 32, 36, 32},
	{"brainpoolP384t1", 48, 20, 48},
	{"brainpoolP512t1", 64, 4, 64},
};

static bool read_register(uc_engine *uc, int register_id, uint64_t *value)
{
	return uc_reg_read(uc, register_id, value) == UC_ERR_OK;
}

static EVP_PKEY *public_key_from_fields(
	const struct bootrom_ecdsa_curve *curve,
	const uint8_t fields[BOOTROM_ECDSA_FIELD_SIZE * 2])
{
	uint8_t encoded_point[1 + 2 * 66];
	BIGNUM *x = NULL;
	BIGNUM *y = NULL;
	EVP_PKEY_CTX *context = NULL;
	EVP_PKEY *key = NULL;
	OSSL_PARAM parameters[3];
	size_t point_size = 1 + 2 * curve->coordinate_size;

	x = BN_bin2bn(fields, BOOTROM_ECDSA_FIELD_SIZE, NULL);
	y = BN_bin2bn(fields + BOOTROM_ECDSA_FIELD_SIZE,
		      BOOTROM_ECDSA_FIELD_SIZE, NULL);
	if (x == NULL || y == NULL || BN_is_zero(x) || BN_is_zero(y) ||
	    BN_num_bytes(x) > (int)curve->coordinate_size ||
	    BN_num_bytes(y) > (int)curve->coordinate_size)
		goto out;

	encoded_point[0] = POINT_CONVERSION_UNCOMPRESSED;
	if (BN_bn2binpad(x, encoded_point + 1,
			 curve->coordinate_size) !=
			 (int)curve->coordinate_size ||
	    BN_bn2binpad(y, encoded_point + 1 + curve->coordinate_size,
			 curve->coordinate_size) !=
			 (int)curve->coordinate_size)
		goto out;

	context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
	if (context == NULL || EVP_PKEY_fromdata_init(context) <= 0)
		goto out;
	parameters[0] = OSSL_PARAM_construct_utf8_string(
		OSSL_PKEY_PARAM_GROUP_NAME, (char *)curve->openssl_group, 0);
	parameters[1] = OSSL_PARAM_construct_octet_string(
		OSSL_PKEY_PARAM_PUB_KEY, encoded_point, point_size);
	parameters[2] = OSSL_PARAM_construct_end();
	if (EVP_PKEY_fromdata(context, &key, EVP_PKEY_PUBLIC_KEY,
			      parameters) <= 0) {
		EVP_PKEY_free(key);
		key = NULL;
	}

out:
	EVP_PKEY_CTX_free(context);
	BN_free(y);
	BN_free(x);
	return key;
}

static bool signature_to_der(const uint8_t *r_bytes, size_t r_size,
			     const uint8_t *s_bytes, size_t s_size,
			     uint8_t **der, size_t *der_size)
{
	ECDSA_SIG *signature = NULL;
	BIGNUM *r = NULL;
	BIGNUM *s = NULL;
	unsigned char *cursor;
	int encoded_size;
	bool success = false;

	*der = NULL;
	*der_size = 0;
	r = BN_bin2bn(r_bytes, (int)r_size, NULL);
	s = BN_bin2bn(s_bytes, (int)s_size, NULL);
	signature = ECDSA_SIG_new();
	if (r == NULL || s == NULL || signature == NULL ||
	    BN_is_zero(r) || BN_is_zero(s) ||
	    ECDSA_SIG_set0(signature, r, s) != 1)
		goto out;
	r = NULL;
	s = NULL;

	encoded_size = i2d_ECDSA_SIG(signature, NULL);
	if (encoded_size <= 0)
		goto out;
	*der = malloc((size_t)encoded_size);
	if (*der == NULL)
		goto out;
	cursor = *der;
	if (i2d_ECDSA_SIG(signature, &cursor) != encoded_size)
		goto out;
	*der_size = (size_t)encoded_size;
	success = true;

out:
	if (!success) {
		free(*der);
		*der = NULL;
		*der_size = 0;
	}
	BN_free(s);
	BN_free(r);
	ECDSA_SIG_free(signature);
	return success;
}

static uint32_t verify_digest(uc_engine *uc, uint32_t curve_index,
			      uint64_t public_key_address,
			      uint64_t digest_address, uint32_t digest_size,
			      uint64_t r_address, uint64_t s_address)
{
	const struct bootrom_ecdsa_curve *curve;
	uint8_t public_key_fields[BOOTROM_ECDSA_FIELD_SIZE * 2];
	uint8_t r_bytes[BOOTROM_ECDSA_FIELD_SIZE];
	uint8_t s_bytes[BOOTROM_ECDSA_FIELD_SIZE];
	uint8_t *digest = NULL;
	uint8_t *der_signature = NULL;
	size_t der_signature_size = 0;
	EVP_PKEY *public_key = NULL;
	EVP_PKEY_CTX *verify_context = NULL;
	uint32_t result = BOOTROM_ECDSA_INVALID_SIGNATURE;

	if (curve_index >= ARRAY_SIZE(bootrom_ecdsa_curves))
		return BOOTROM_ECDSA_BAD_CURVE;
	curve = &bootrom_ecdsa_curves[curve_index];
	if (digest_size == 0 || digest_size > BOOTROM_ECDSA_MAX_DIGEST_SIZE)
		return BOOTROM_ECDSA_INVALID_SIGNATURE;

	digest = malloc(digest_size);
	if (digest == NULL ||
	    uc_mem_read(uc, public_key_address, public_key_fields,
			sizeof(public_key_fields)) != UC_ERR_OK ||
	    uc_mem_read(uc, digest_address, digest, digest_size) != UC_ERR_OK ||
	    uc_mem_read(uc, r_address + curve->scalar_offset, r_bytes,
			curve->scalar_size) != UC_ERR_OK ||
	    uc_mem_read(uc, s_address + curve->scalar_offset, s_bytes,
			curve->scalar_size) != UC_ERR_OK)
		goto out;

	public_key = public_key_from_fields(curve, public_key_fields);
	if (public_key == NULL ||
	    !signature_to_der(r_bytes, curve->scalar_size,
			      s_bytes, curve->scalar_size,
			      &der_signature, &der_signature_size))
		goto out;

	verify_context = EVP_PKEY_CTX_new(public_key, NULL);
	if (verify_context == NULL ||
	    EVP_PKEY_verify_init(verify_context) <= 0 ||
	    EVP_PKEY_CTX_set_signature_md(verify_context, EVP_sha512()) <= 0)
		goto out;
	if (EVP_PKEY_verify(verify_context, der_signature,
			    der_signature_size, digest, digest_size) == 1)
		result = 0;

out:
	EVP_PKEY_CTX_free(verify_context);
	EVP_PKEY_free(public_key);
	free(der_signature);
	free(digest);
	return result;
}

uint32_t bootrom_ecdsa_verify_core(uc_engine *uc)
{
	uint64_t curve_index;
	uint64_t public_key_address;
	uint64_t digest_address;
	uint64_t digest_size;
	uint64_t r_address;
	uint64_t stack_pointer;
	uint64_t stack_arguments[2];

	if (!read_register(uc, UC_ARM64_REG_X0, &curve_index) ||
	    !read_register(uc, UC_ARM64_REG_X2, &public_key_address) ||
	    !read_register(uc, UC_ARM64_REG_X4, &digest_address) ||
	    !read_register(uc, UC_ARM64_REG_X5, &digest_size) ||
	    !read_register(uc, UC_ARM64_REG_X6, &r_address) ||
	    !read_register(uc, UC_ARM64_REG_SP, &stack_pointer) ||
	    uc_mem_read(uc, stack_pointer, stack_arguments,
			sizeof(stack_arguments)) != UC_ERR_OK) {
		fprintf(stderr, "[BootROM ECDSA] failed to read core arguments\n");
		return BOOTROM_ECDSA_INVALID_SIGNATURE;
	}

	return verify_digest(uc, (uint32_t)curve_index, public_key_address,
			     digest_address, (uint32_t)digest_size, r_address,
			     stack_arguments[0]);
}

uint32_t bootrom_ecdsa_verify_dispatch(uc_engine *uc)
{
	uint64_t sign_type;
	uint64_t enabled;
	uint64_t public_key_address;
	uint64_t public_key_size;
	uint64_t digest_address;
	uint64_t digest_size;
	uint64_t r_address;
	uint64_t r_size;
	uint64_t stack_pointer;
	uint64_t stack_arguments[2];
	uint32_t curve_index;

	if (!read_register(uc, UC_ARM64_REG_X0, &sign_type) ||
	    !read_register(uc, UC_ARM64_REG_X1, &enabled) ||
	    !read_register(uc, UC_ARM64_REG_X2, &public_key_address) ||
	    !read_register(uc, UC_ARM64_REG_X3, &public_key_size) ||
	    !read_register(uc, UC_ARM64_REG_X4, &digest_address) ||
	    !read_register(uc, UC_ARM64_REG_X5, &digest_size) ||
	    !read_register(uc, UC_ARM64_REG_X6, &r_address) ||
	    !read_register(uc, UC_ARM64_REG_X7, &r_size) ||
	    !read_register(uc, UC_ARM64_REG_SP, &stack_pointer)) {
		fprintf(stderr,
			"[BootROM ECDSA] failed to read dispatch arguments\n");
		return BOOTROM_ECDSA_DISPATCH_BAD_S;
	}

	if ((uint32_t)enabled == 0)
		return BOOTROM_ECDSA_DISPATCH_DISABLED;
	if (public_key_address == 0 ||
	    (uint32_t)public_key_size != BOOTROM_ECDSA_PUBLIC_KEY_SIZE)
		return BOOTROM_ECDSA_DISPATCH_BAD_KEY;
	if (digest_address == 0 ||
	    (uint32_t)digest_size != BOOTROM_ECDSA_SHA512_SIZE)
		return BOOTROM_ECDSA_DISPATCH_BAD_DIGEST;
	if (r_address == 0 ||
	    (uint32_t)r_size > BOOTROM_ECDSA_FIELD_SIZE)
		return BOOTROM_ECDSA_DISPATCH_BAD_R;
	if (uc_mem_read(uc, stack_pointer, stack_arguments,
			sizeof(stack_arguments)) != UC_ERR_OK)
		return BOOTROM_ECDSA_DISPATCH_BAD_S;
	if (stack_arguments[0] == 0 ||
	    (uint32_t)stack_arguments[1] > BOOTROM_ECDSA_FIELD_SIZE)
		return BOOTROM_ECDSA_DISPATCH_BAD_S;

	curve_index = (uint32_t)sign_type - 3;
	if (curve_index >= ARRAY_SIZE(bootrom_ecdsa_curves))
		return BOOTROM_ECDSA_DISPATCH_BAD_ALGORITHM;
	return verify_digest(uc, curve_index, public_key_address,
			     digest_address, (uint32_t)digest_size, r_address,
			     stack_arguments[0]);
}

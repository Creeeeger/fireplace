#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include "sss/sss_internal.h"

static BIGNUM *sss_math_read_slot(uc_engine *uc, uint32_t slot,
				  uint32_t stride)
{
	unsigned char *bytes;
	BIGNUM *value = NULL;
	uint64_t address;

	if (stride == 0 || stride > 0x1000 || slot > 0xff)
		return NULL;
	bytes = calloc(1, stride);
	if (!bytes)
		return NULL;
	address = SSS_MATH_SLOT_BASE + (uint64_t)slot * stride;
	if (uc_mem_read(uc, address, bytes, stride) == UC_ERR_OK)
		value = BN_lebin2bn(bytes, (int)stride, NULL);
	free(bytes);
	return value;
}

static bool sss_math_write_slot(uc_engine *uc, uint32_t slot,
				uint32_t stride, const BIGNUM *value)
{
	unsigned char *bytes;
	uint64_t address;
	bool ok = false;

	if (!value || BN_is_negative(value) || stride == 0 || stride > 0x1000 ||
	    slot > 0xff)
		return false;
	bytes = calloc(1, stride);
	if (!bytes)
		return false;
	if (BN_bn2lebinpad(value, bytes, (int)stride) == (int)stride) {
		address = SSS_MATH_SLOT_BASE + (uint64_t)slot * stride;
		ok = uc_mem_write(uc, address, bytes, stride) == UC_ERR_OK;
	}
	free(bytes);
	return ok;
}

static void sss_math_clear_sign(uc_engine *uc, uint32_t slot)
{
	uint64_t address;
	uint32_t mask;
	uint32_t status;

	if (slot >= 64)
		return;
	address = slot < 32 ? SSS_MATH_SIGN_LO : SSS_MATH_SIGN_HI;
	mask = UINT32_C(1) << (slot & 31);
	status = sss_reg(address) & ~mask;
	sss_set_reg(uc, address, status);
}

/*
 * The lower firmware keeps the active modulus in the command's modulus slot
 * and its Montgomery radix (R mod modulus) in the following slot.  Reading R
 * from the firmware-owned slot is more accurate than reconstructing the
 * implementation-specific 480-bit P-384 radix from the PKA configuration.
 */
static BIGNUM *sss_math_read_radix(uc_engine *uc, uint32_t modulus_slot,
				   uint32_t stride)
{
	if (modulus_slot == UINT32_C(0xff))
		return NULL;
	return sss_math_read_slot(uc, modulus_slot + 1, stride);
}

static bool sss_math_montgomery_reduce(BIGNUM *result,
				       const BIGNUM *value,
				       const BIGNUM *radix,
				       const BIGNUM *modulus,
				       BN_CTX *context)
{
	BIGNUM *inverse = NULL;
	bool ok = false;

	inverse = BN_mod_inverse(NULL, radix, modulus, context);
	if (inverse)
		ok = BN_mod_mul(result, value, inverse, modulus, context) == 1;
	BN_free(inverse);
	return ok;
}

static bool sss_math_read_affine_point(uc_engine *uc, EC_GROUP *group,
				       EC_POINT *point, uint32_t coordinate_slot,
				       uint32_t stride, const BIGNUM *radix,
				       const BIGNUM *modulus, BN_CTX *context)
{
	BIGNUM *stored_x = NULL;
	BIGNUM *stored_y = NULL;
	BIGNUM *stored_z = NULL;
	BIGNUM *x = NULL;
	BIGNUM *y = NULL;
	BIGNUM *z = NULL;
	BIGNUM *z_inverse = NULL;
	BIGNUM *z_inverse_squared = NULL;
	BIGNUM *z_inverse_cubed = NULL;
	bool ok = false;

	stored_x = sss_math_read_slot(uc, coordinate_slot, stride);
	stored_y = sss_math_read_slot(uc, coordinate_slot + 1, stride);
	stored_z = sss_math_read_slot(uc, coordinate_slot + 2, stride);
	x = BN_new();
	y = BN_new();
	z = BN_new();
	z_inverse_squared = BN_new();
	z_inverse_cubed = BN_new();
	if (!stored_x || !stored_y || !stored_z || !x || !y || !z ||
	    !z_inverse_squared || !z_inverse_cubed)
		goto out;
	if (!sss_math_montgomery_reduce(x, stored_x, radix, modulus, context) ||
	    !sss_math_montgomery_reduce(y, stored_y, radix, modulus, context) ||
	    !sss_math_montgomery_reduce(z, stored_z, radix, modulus, context))
		goto out;

	if (BN_is_zero(z)) {
		ok = EC_POINT_set_to_infinity(group, point) == 1;
		goto out;
	}

	/* Convert the firmware's Jacobian X:Y:Z representation to affine. */
	z_inverse = BN_mod_inverse(NULL, z, modulus, context);
	if (!z_inverse ||
	    BN_mod_sqr(z_inverse_squared, z_inverse, modulus, context) != 1 ||
	    BN_mod_mul(z_inverse_cubed, z_inverse_squared, z_inverse, modulus,
		       context) != 1 ||
	    BN_mod_mul(x, x, z_inverse_squared, modulus, context) != 1 ||
	    BN_mod_mul(y, y, z_inverse_cubed, modulus, context) != 1 ||
	    EC_POINT_set_affine_coordinates(group, point, x, y, context) != 1 ||
	    EC_POINT_is_on_curve(group, point, context) != 1)
		goto out;
	ok = true;

out:
	BN_free(stored_x);
	BN_free(stored_y);
	BN_free(stored_z);
	BN_free(x);
	BN_free(y);
	BN_free(z);
	BN_free(z_inverse);
	BN_free(z_inverse_squared);
	BN_free(z_inverse_cubed);
	return ok;
}

static bool sss_math_write_affine_point(uc_engine *uc, EC_GROUP *group,
					const EC_POINT *point,
					uint32_t destination,
					uint32_t stride,
					const BIGNUM *radix,
					const BIGNUM *modulus,
					BN_CTX *context)
{
	BIGNUM *x = NULL;
	BIGNUM *y = NULL;
	BIGNUM *encoded_x = NULL;
	BIGNUM *encoded_y = NULL;
	bool ok = false;

	if (EC_POINT_is_at_infinity(group, point) == 1) {
		x = BN_new();
		y = BN_new();
		if (!x || !y)
			goto out;
		BN_zero(x);
		BN_zero(y);
		ok = sss_math_write_slot(uc, destination, stride, x) &&
		     sss_math_write_slot(uc, destination + 1, stride, y) &&
		     sss_math_write_slot(uc, destination + 2, stride, x);
		goto out;
	}

	x = BN_new();
	y = BN_new();
	encoded_x = BN_new();
	encoded_y = BN_new();
	if (!x || !y || !encoded_x || !encoded_y ||
	    EC_POINT_get_affine_coordinates(group, point, x, y, context) != 1 ||
	    BN_mod_mul(encoded_x, x, radix, modulus, context) != 1 ||
	    BN_mod_mul(encoded_y, y, radix, modulus, context) != 1)
		goto out;

	/* An affine result is a valid Jacobian result with Z = 1 (stored as R). */
	ok = sss_math_write_slot(uc, destination, stride, encoded_x) &&
	     sss_math_write_slot(uc, destination + 1, stride, encoded_y) &&
	     sss_math_write_slot(uc, destination + 2, stride, radix);

out:
	BN_free(x);
	BN_free(y);
	BN_free(encoded_x);
	BN_free(encoded_y);
	if (ok) {
		sss_math_clear_sign(uc, destination);
		sss_math_clear_sign(uc, destination + 1);
		sss_math_clear_sign(uc, destination + 2);
	}
	return ok;
}


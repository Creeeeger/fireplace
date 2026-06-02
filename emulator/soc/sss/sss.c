#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/rand.h>

#include "sss/sss_internal.h"

bool sss_hash_logged;
bool sss_error_logged;
bool sss_feed_ready;
bool sss_hash_pending;
bool sss_cipher_hash_pending;
uint32_t sss_regs[SSS_REG_COUNT];
unsigned char sss_key_manager_key[32];
uint32_t sss_key_manager_key_len;
bool sss_key_manager_key_valid;
bool sss_key_manager_key_device_bound;
bool sss_cipher_internal_key_bound;
bool sss_hwkek_profile_checked;
bool sss_hwkek_profile_available;
uint32_t sss_reg(uint64_t address);
void sss_set_reg(uc_engine *uc, uint64_t address, uint32_t value);
int sss_init(struct uc_struct *uc)
{
	(void)uc;
	sss_hash_logged = false;
	sss_error_logged = false;
	sss_feed_ready = false;
	sss_hash_pending = false;
	sss_cipher_hash_pending = false;
	sss_key_manager_key_len = 0;
	sss_key_manager_key_valid = false;
	sss_key_manager_key_device_bound = false;
	sss_cipher_internal_key_bound = false;
	sss_hwkek_profile_checked = false;
	sss_hwkek_profile_available = false;
	memset(sss_regs, 0, sizeof(sss_regs));
	memset(sss_key_manager_key, 0, sizeof(sss_key_manager_key));
	return 0;
}

void sss_log_error(const char *reason, uint32_t mode, uint32_t control,
			  uint64_t source, uint32_t length)
{
	if (sss_error_logged)
		return;
	fprintf(stderr,
		"[SSS] %s mode=0x%08x control=0x%08x src=0x%016llx "
		"len=0x%x\n",
		reason, mode, control, (unsigned long long)source, length);
	sss_error_logged = true;
}

static bool sss_reg_index(uint64_t address, size_t *index)
{
	if (address < SSS_BASE || address >= SSS_BASE + SSS_SIZE ||
	    ((address - SSS_BASE) & 3) != 0)
		return false;
	*index = (size_t)((address - SSS_BASE) / sizeof(uint32_t));
	return true;
}

uint32_t sss_reg(uint64_t address)
{
	size_t index;

	if (!sss_reg_index(address, &index))
		return 0;
	return sss_regs[index];
}

void sss_set_reg(uc_engine *uc, uint64_t address, uint32_t value)
{
	size_t index;

	if (!sss_reg_index(address, &index))
		return;
	sss_regs[index] = value;
	uc_mem_write(uc, address, &value, sizeof(value));
}

static void sss_start_rng(uc_engine *uc)
{
	uint32_t random_words[8];
	uint32_t control;

	control = sss_reg(SSS_RNG_CONTROL) & ~SSS_RNG_ERROR;
	if (RAND_bytes((unsigned char *)random_words,
		       sizeof(random_words)) != 1) {
		sss_set_reg(uc, SSS_RNG_CONTROL, control | SSS_RNG_ERROR);
		sss_set_reg(uc, SSS_RNG_COMMAND, 0);
		return;
	}

	sss_set_reg(uc, SSS_RNG_CONTROL, control);
	for (size_t i = 0; i < sizeof(random_words) / sizeof(random_words[0]); i++)
		sss_set_reg(uc, SSS_RNG_DATA + i * sizeof(uint32_t),
			    random_words[i]);
	sss_set_reg(uc, SSS_RNG_COMMAND, 0);
}

void sss_set_status_bits(uc_engine *uc, uint32_t bits)
{
	uint32_t status;

	status = sss_reg(SSS_FEED_STATUS) | bits;
	sss_set_reg(uc, SSS_FEED_STATUS, status);

	status = sss_reg(SSS_FEED_STATUS_CLEAR) | bits;
	sss_set_reg(uc, SSS_FEED_STATUS_CLEAR, status);

	if ((bits & SSS_FEED_READY) != 0)
		sss_feed_ready = true;
}

static void sss_clear_status_bits(uc_engine *uc, uint32_t bits)
{
	uint32_t status;

	status = sss_reg(SSS_FEED_STATUS) & ~bits;
	sss_set_reg(uc, SSS_FEED_STATUS, status);

	status = sss_reg(SSS_FEED_STATUS_CLEAR) & ~bits;
	sss_set_reg(uc, SSS_FEED_STATUS_CLEAR, status);

	if ((bits & SSS_FEED_READY) != 0)
		sss_feed_ready = false;
}

static void sss_set_feed_ready(uc_engine *uc, bool ready)
{
	if (ready)
		sss_set_status_bits(uc, SSS_FEED_READY);
	else
		sss_clear_status_bits(uc, SSS_FEED_READY);
}

void sss_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
	      int64_t value, void *user_data)
{
	uint32_t status;
	size_t index;

	(void)user_data;
	if (type == UC_MEM_WRITE && size == 4 &&
	    sss_reg_index(address, &index)) {
		uint32_t written = (uint32_t)value;

		if (address == SSS_FEED_STATUS_CLEAR) {
			sss_clear_status_bits(uc, written);
		} else if (address == SSS_HASH_STATUS) {
			sss_set_reg(uc, address, sss_regs[index] & ~written);
			if ((written & (SSS_HASH_DONE | SSS_HASH_ERROR)) != 0) {
				sss_hash_pending = false;
				sss_cipher_hash_pending = false;
			}
		} else if (address == SSS_MATH_STATUS) {
			sss_set_reg(uc, address, sss_regs[index] & ~written);
		} else if (address == SSS_CIPHER_STATUS) {
			sss_set_reg(uc, address, sss_regs[index] & ~written);
		} else if (address == SSS_RNG_COMMAND) {
			if (written != 0)
				sss_start_rng(uc);
			else
				sss_set_reg(uc, address, 0);
		} else {
			sss_regs[index] = written;
			if (address == SSS_HASH_CONTROL) {
				sss_cipher_hash_pending = (written & 1) != 0;
				if (written == 0)
					sss_hash_pending = false;
			}
		}
		if (address >= SSS_CIPHER_KEY_256 &&
		    address < SSS_CIPHER_KEY_128 + 16)
			sss_cipher_internal_key_bound = false;
		if (address == SSS_KEY_MANAGER_COMMAND)
			sss_complete_key_manager_command(uc, written);
		if (address == SSS_SRC_LEN && written != 0)
			sss_set_feed_ready(uc, true);
		if (address == SSS_SRC_LEN && written != 0)
			sss_hash_pending = true;
		if (address == SSS_CIPHER_DST_LEN && written != 0 &&
		    sss_reg(SSS_CIPHER_SRC_LEN) != 0) {
			if (!sss_complete_cipher_dma(uc)) {
				if (sss_cipher_internal_key_bound &&
				    !sss_key_manager_key_valid)
					sss_cipher_internal_key_bound = false;
				sss_set_status_bits(uc, SSS_CIPHER_ERROR);
				sss_set_reg(uc, SSS_CIPHER_STATUS,
					    sss_reg(SSS_CIPHER_STATUS) |
					    SSS_CIPHER_ERROR);
			}
		}
		if (address == SSS_CIPHER_PIO_INPUT + 3 * sizeof(uint32_t) &&
		    !sss_complete_cipher_pio(uc)) {
			if (sss_cipher_internal_key_bound &&
			    !sss_key_manager_key_valid)
				sss_cipher_internal_key_bound = false;
			sss_set_reg(uc, SSS_CIPHER_STATUS,
				    sss_reg(SSS_CIPHER_STATUS) |
				    SSS_CIPHER_PIO_ERROR);
		}
		if (address == SSS_MATH_COMMAND) {
			(void)sss_complete_math(uc, written);
			sss_set_reg(uc, SSS_MATH_STATUS,
				    sss_reg(SSS_MATH_STATUS) | SSS_MATH_DONE);
		}
		return;
	}
	if (type != UC_MEM_READ)
		return;
	if (address == SSS_SECURE_SERVICE_STATUS) {
		status = sss_reg(address) | SSS_SECURE_SERVICE_READY;
		sss_set_reg(uc, address, status);
		return;
	}
	if (address == SSS_MATH_STATUS) {
		/*
		 * MATH_STATUS is a latched, write-one-to-clear completion
		 * register.  Completion is raised when MATH_COMMAND is written
		 * above; a plain status read must not raise it again.  The lower
		 * firmware clears the bit and rereads it to verify that the PKA
		 * interrupt was acknowledged.
		 */
		sss_set_reg(uc, address, sss_reg(address));
		return;
	}
	if (address == SSS_CIPHER_STATUS) {
		status = sss_reg(address) | SSS_CIPHER_DONE |
			 SSS_CIPHER_INPUT_READY;
		sss_set_reg(uc, address, status);
		return;
	}
	if (address == SSS_RNG_STATUS) {
		status = sss_reg(address) | SSS_RNG_READY;
		sss_set_reg(uc, address, status);
		return;
	}
	if (address == SSS_RNG_COMMAND) {
		sss_set_reg(uc, address, 0);
		return;
	}
	if (address == SSS_KEY_MANAGER_STATUS) {
		status = sss_reg(address) | SSS_KEY_MANAGER_READY |
			 SSS_KEY_MANAGER_DONE;
		sss_set_reg(uc, address, status);
		return;
	}
	if (address == SSS_FEED_STATUS || address == SSS_FEED_STATUS_CLEAR) {
		status = sss_reg(address);
		if (sss_feed_ready)
			status |= SSS_FEED_READY;
		else
			status &= ~SSS_FEED_READY;
		sss_set_reg(uc, address, status);
		return;
	}
	if (address != SSS_HASH_STATUS)
		return;

	status = sss_reg(address);
	if ((status & SSS_HASH_DONE) == 0 && sss_hash_pending) {
		if (!sss_compute_hash(uc))
			return;
		status = sss_reg(address) | SSS_HASH_DONE;
	}
	sss_set_reg(uc, address, status);
}

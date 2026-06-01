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


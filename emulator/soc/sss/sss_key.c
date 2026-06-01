#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sss/sss_internal.h"

static int sss_hex_nibble(unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

static bool sss_parse_hwkek_profile(const unsigned char *data, size_t size,
				    unsigned char key[32])
{
	size_t digits = 0;
	int high = -1;

	if (size == 32) {
		memcpy(key, data, 32);
		return true;
	}
	for (size_t i = 0; i < size; i++) {
		int nibble = sss_hex_nibble(data[i]);

		if (nibble < 0) {
			if (data[i] == ' ' || data[i] == '\t' ||
			    data[i] == '\r' || data[i] == '\n')
				continue;
			return false;
		}
		if (digits >= 64)
			return false;
		if ((digits & 1) == 0)
			high = nibble;
		else
			key[digits / 2] = (unsigned char)((high << 4) | nibble);
		digits++;
	}
	return digits == 64;
}

static bool sss_load_hwkek_profile(void)
{
	char *contents = NULL;
	size_t size = 0;
	uc_err err;

	if (sss_hwkek_profile_checked)
		return sss_hwkek_profile_available;
	sss_hwkek_profile_checked = true;
	err = bootchain_read_profile_file("hwkek.key", &contents, &size);
	if (err == UC_ERR_OK) {
		sss_hwkek_profile_available = sss_parse_hwkek_profile(
			(const unsigned char *)contents, size, sss_key_manager_key);
		free(contents);
	}
	if (!sss_hwkek_profile_available) {
		fprintf(stderr,
			"[SSS] no valid 256-bit hwkek.key profile; "
			"using a non-authentic zero placeholder\n");
		memset(sss_key_manager_key, 0, sizeof(sss_key_manager_key));
	}
	return sss_hwkek_profile_available;
}

void sss_complete_key_manager_command(uc_engine *uc,
					      uint32_t command)
{
	const unsigned char *registers = (const unsigned char *)sss_regs;

	switch (command) {
	case UINT32_C(0x1410):
		memcpy(sss_key_manager_key,
		       registers + (SSS_KEY_MANAGER_INPUT - SSS_BASE), 32);
		sss_key_manager_key_len = 32;
		sss_key_manager_key_valid = true;
		sss_key_manager_key_device_bound = false;
		/* Import alone does not select the key-manager slot for AES. */
		sss_cipher_internal_key_bound = false;
		break;
	case UINT32_C(0x1800):
		(void)sss_load_hwkek_profile();
		sss_key_manager_key_valid = true;
		sss_key_manager_key_device_bound = true;
		sss_key_manager_key_len = 32;
		sss_cipher_internal_key_bound = false;
		break;
	case UINT32_C(0x4400):
	case UINT32_C(0x4408):
	case UINT32_C(0x4410):
		sss_key_manager_key_len = 16 + (command - UINT32_C(0x4400));
		sss_cipher_internal_key_bound = true;
		break;
	default:
		break;
	}
}




#include <stdint.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_devices.h"
#include "lk/lk_devices_internal.h"

#define LK_EXT_I2C_READ_ADDR UINT64_C(0xe801c368)
#define LK_EXT_I2C_WRITE_ADDR UINT64_C(0xe801c558)
#define LK_EXT_I2C_WORD_WRITE_ADDR UINT64_C(0xe801c660)
#define LK_EXT_I2C_WORD_READ_ADDR UINT64_C(0xe801c7b0)
#define LK_EXT_I2C_BYTE_READ_ADDR UINT64_C(0xe801c0d0)
#define LK_EXT_I2C_BYTE_WRITE_ADDR UINT64_C(0xe801c288)

static bool read_i2c_args(uc_engine *uc, uint64_t *bus, uint64_t *slave,
			  uint64_t *reg, uint64_t *value)
{
	return uc_reg_read(uc, UC_ARM64_REG_X0, bus) == UC_ERR_OK &&
	       uc_reg_read(uc, UC_ARM64_REG_X1, slave) == UC_ERR_OK &&
	       uc_reg_read(uc, UC_ARM64_REG_X2, reg) == UC_ERR_OK &&
	       uc_reg_read(uc, UC_ARM64_REG_X3, value) == UC_ERR_OK;
}

static void finish_i2c(uc_engine *uc)
{
	uint64_t result = 0;

	uc_reg_write(uc, UC_ARM64_REG_X0, &result);
	(void)bootchain_return_to_link(uc);
}

static bool lk_ext_i2c_byte(uc_engine *uc, bool write)
{
	uint64_t bus, slave, reg, argument;
	uint8_t value;

	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return false;
	if (!read_i2c_args(uc, &bus, &slave, &reg, &argument)) {
		bootchain_fail(uc);
		return true;
	}
	if (write) {
		if (!lk_device_byte_write((uint8_t)bus, (uint8_t)slave,
					  (uint8_t)reg, (uint8_t)argument))
			return false;
	} else {
		if (!lk_device_byte_read((uint8_t)bus, (uint8_t)slave,
					 (uint8_t)reg, &value))
			return false;
		if (uc_mem_write(uc, argument, &value, sizeof(value)) != UC_ERR_OK) {
			bootchain_fail(uc);
			return true;
		}
	}
	finish_i2c(uc);
	return true;
}

static bool lk_ext_i2c_block(uc_engine *uc, bool write)
{
	uint64_t bus, slave, reg, buffer_addr, length_arg;
	uint8_t buffer[256];
	size_t length;
	bool handled;

	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return false;
	if (!read_i2c_args(uc, &bus, &slave, &reg, &buffer_addr) ||
	    uc_reg_read(uc, UC_ARM64_REG_X4, &length_arg) != UC_ERR_OK) {
		bootchain_fail(uc);
		return true;
	}
	length = (size_t)(length_arg & UINT64_C(0xff));
	if (length == 0)
		length = 1;
	if (write && uc_mem_read(uc, buffer_addr, buffer, length) != UC_ERR_OK) {
		bootchain_fail(uc);
		return true;
	}
	handled = write ?
		lk_device_block_write((uint8_t)bus, (uint8_t)slave,
				      (uint8_t)reg, buffer, length) :
		lk_device_block_read((uint8_t)bus, (uint8_t)slave,
				     (uint8_t)reg, buffer, length);
	if (!handled)
		return false;
	if (!write && uc_mem_write(uc, buffer_addr, buffer, length) != UC_ERR_OK) {
		bootchain_fail(uc);
		return true;
	}
	finish_i2c(uc);
	return true;
}

static bool lk_ext_i2c_word(uc_engine *uc, bool write)
{
	uint64_t bus, slave, reg, argument;
	uint32_t value;
	bool handled;

	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return false;
	if (!read_i2c_args(uc, &bus, &slave, &reg, &argument)) {
		bootchain_fail(uc);
		return true;
	}
	handled = write ?
		lk_device_word_write((uint8_t)bus, (uint8_t)slave,
				     (uint32_t)reg, (uint32_t)argument) :
		lk_device_word_read((uint8_t)bus, (uint8_t)slave,
				    (uint32_t)reg, &value);
	if (!handled)
		return false;
	if (!write && uc_mem_write(uc, argument, &value, sizeof(value)) != UC_ERR_OK) {
		bootchain_fail(uc);
		return true;
	}
	finish_i2c(uc);
	return true;
}

#define I2C_CALLBACK(name, operation) \
static void name(uc_engine *uc, uint64_t address, uint32_t size, void *data) \
{ \
	(void)address; (void)size; (void)data; (void)(operation); \
}

I2C_CALLBACK(lk_ext_i2c_read_cb, lk_ext_i2c_block(uc, false))
I2C_CALLBACK(lk_ext_i2c_write_cb, lk_ext_i2c_block(uc, true))
I2C_CALLBACK(lk_ext_i2c_word_write_cb, lk_ext_i2c_word(uc, true))
I2C_CALLBACK(lk_ext_i2c_word_read_cb, lk_ext_i2c_word(uc, false))
I2C_CALLBACK(lk_ext_i2c_byte_read_cb, lk_ext_i2c_byte(uc, false))
I2C_CALLBACK(lk_ext_i2c_byte_write_cb, lk_ext_i2c_byte(uc, true))

uc_err lk_devices_init(uc_engine *uc)
{
	const struct bootchain_hook hooks[] = {
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_read_cb, LK_EXT_I2C_READ_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_write_cb, LK_EXT_I2C_WRITE_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_word_write_cb,
				    LK_EXT_I2C_WORD_WRITE_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_word_read_cb,
				    LK_EXT_I2C_WORD_READ_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_byte_read_cb,
				    LK_EXT_I2C_BYTE_READ_ADDR),
		BOOTCHAIN_CODE_HOOK(lk_ext_i2c_byte_write_cb,
				    LK_EXT_I2C_BYTE_WRITE_ADDR),
	};

	lk_devices_reset();
	return bootchain_install_hooks(uc, hooks, ARRAY_SIZE(hooks));
}

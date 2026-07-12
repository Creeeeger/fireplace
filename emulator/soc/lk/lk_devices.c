
#include <string.h>

#include "lk/lk_devices_internal.h"

#define LK_FG_I2C_BUS UINT8_C(2)
#define LK_FG_I2C_READ_SLAVE UINT8_C(0x6d)
#define LK_FG_I2C_WRITE_SLAVE UINT8_C(0x6c)
#define LK_MUIC_I2C_BUS UINT8_C(0)
#define LK_MUIC_I2C_READ_SLAVE UINT8_C(0x4a)
#define LK_MUIC_I2C_WRITE_SLAVE UINT8_C(0x4b)
#define LK_TOPSYS_I2C_READ_SLAVE UINT8_C(0xcc)
#define LK_TOPSYS_I2C_WRITE_SLAVE UINT8_C(0xcd)
#define LK_MAX77705_I2C_BUS UINT8_C(3)
#define LK_MAX77705_RID_OPEN UINT8_C(0x07)
#define LK_CHARGER_I2C_READ_SLAVE UINT8_C(0xd3)
#define LK_CHARGER_I2C_WRITE_SLAVE UINT8_C(0xd2)
#define LK_USBC_I2C_SLAVE UINT8_C(0x4a)
#define LK_S2DOS05_I2C_BUS UINT8_C(6)
#define LK_S2DOS05_I2C_SLAVE UINT8_C(0xc0)
#define LK_S2MPB02_I2C_BUS UINT8_C(5)
#define LK_S2MPB02_I2C_SLAVE UINT8_C(0xb2)
#define LK_S2MPB03_I2C_BUS UINT8_C(8)
#define LK_S2MPB03_I2C_SLAVE UINT8_C(0xac)
#define LK_CS40L2X_I2C_BUS UINT8_C(7)
#define LK_CS40L2X_I2C_SLAVE UINT8_C(0x80)
#define LK_CS40L2X_REG_COUNT 16

static bool fuel_gauge_i2c_seeded;
static bool max77705_i2c_seeded;
static uint8_t fuel_gauge_regs[256];
static uint8_t muic_regs[256];
static uint8_t topsys_regs[256];
static uint8_t charger_regs[256];
static uint8_t usbc_regs[256];
static uint8_t s2dos05_regs[256];
static uint8_t s2mpb02_regs[256];
static uint8_t s2mpb03_regs[256];

struct lk_word_reg {
	uint32_t reg;
	uint32_t value;
};

static struct lk_word_reg cs40l2x_regs[LK_CS40L2X_REG_COUNT];
static size_t cs40l2x_reg_count;

static void fuel_gauge_write16(uint8_t reg, uint16_t value)
{
	fuel_gauge_regs[reg] = (uint8_t)value;
	fuel_gauge_regs[(uint8_t)(reg + 1)] = (uint8_t)(value >> 8);
}

static void fuel_gauge_seed(void)
{
	if (fuel_gauge_i2c_seeded)
		return;

	memset(fuel_gauge_regs, 0, sizeof(fuel_gauge_regs));

	fuel_gauge_write16(0x00, 0x0000);
	fuel_gauge_write16(0x06, 0x6400);
	fuel_gauge_write16(0x09, 0xc000);
	fuel_gauge_write16(0x0a, 0x0000);
	fuel_gauge_write16(0x18, 0x1f40);
	fuel_gauge_write16(0x19, 0xc000);
	fuel_gauge_write16(0xb2, 0x0006);
	fuel_gauge_write16(0xbb, 0x0000);
	fuel_gauge_write16(0xfb, 0xc000);

	fuel_gauge_i2c_seeded = true;
}

static void fuel_gauge_read(uint8_t reg, uint8_t *buffer, size_t length)
{
	for (size_t i = 0; i < length; i++)
		buffer[i] = fuel_gauge_regs[(uint8_t)(reg + i)];

	if (reg == 0xbb && length >= 2) {
		uint16_t value = (uint16_t)buffer[0] |
				 (uint16_t)buffer[1] << 8;

		value &= (uint16_t)~UINT16_C(0x20);
		buffer[0] = (uint8_t)value;
		buffer[1] = (uint8_t)(value >> 8);
		fuel_gauge_write16(reg, value);
	}
}

static void fuel_gauge_write(uint8_t reg, const uint8_t *buffer, size_t length)
{
	for (size_t i = 0; i < length; i++)
		fuel_gauge_regs[(uint8_t)(reg + i)] = buffer[i];
}

static void max77705_seed(void)
{
	if (max77705_i2c_seeded)
		return;

	memset(charger_regs, 0, sizeof(charger_regs));
	memset(usbc_regs, 0, sizeof(usbc_regs));
	memset(s2dos05_regs, 0, sizeof(s2dos05_regs));
	memset(s2mpb02_regs, 0, sizeof(s2mpb02_regs));
	memset(s2mpb03_regs, 0, sizeof(s2mpb03_regs));
	memset(muic_regs, 0, sizeof(muic_regs));
	memset(topsys_regs, 0, sizeof(topsys_regs));

	muic_regs[0x00] = 0x10;
	muic_regs[0x01] = 0x01;
	muic_regs[0x02] = 0x80;
	muic_regs[0x06] = LK_MAX77705_RID_OPEN;
	muic_regs[0x08] = 0x00;
	muic_regs[0x0d] = LK_MAX77705_RID_OPEN;

	topsys_regs[0x00] = 0x05;
	topsys_regs[0x01] = 0x05;

	charger_regs[0xb2] = 0x04;
	charger_regs[0xb3] = 0x00;
	charger_regs[0xb4] = 0x03;
	charger_regs[0xb7] = 0x04;
	charger_regs[0xb8] = 0x00;
	charger_regs[0xb9] = 0x00;
	charger_regs[0xbb] = 0x38;
	charger_regs[0xbd] = 0x0c;
	charger_regs[0xbe] = 0x00;
	charger_regs[0xc0] = 0x0f;
	charger_regs[0xc1] = 0x10;

	s2dos05_regs[0x03] = 0x1f;
	s2dos05_regs[0x61] = 0x05;

	s2mpb02_regs[0x1e] = 0x80;
	s2mpb02_regs[0x1f] = 0x80;
	s2mpb02_regs[0x20] = 0x80;
	s2mpb02_regs[0x21] = 0x80;
	for (uint8_t reg = 0x03; reg < 0x0a; reg++)
		s2mpb03_regs[reg] = 0x80;

	max77705_i2c_seeded = true;
}

static void muic_write_reg(uint8_t reg, uint8_t value)
{
	muic_regs[reg] = value;

	if (reg == 0x21) {
		muic_regs[0x51] = value;
		muic_regs[0x01] |= 0x80;
		muic_regs[0x02] |= 0x80;
	} else if (reg == 0x22) {
		muic_regs[0x52] = value;
	} else if (reg == 0x41) {
		muic_regs[0x01] |= 0x80;
		muic_regs[0x02] |= 0x80;
	}
}

static uint8_t *max77705_regs_for_byte_i2c(uint8_t bus, uint8_t slave,
					   bool write)
{
	if (bus == LK_MUIC_I2C_BUS) {
		if (slave == LK_MUIC_I2C_READ_SLAVE ||
		    slave == LK_MUIC_I2C_WRITE_SLAVE)
			return muic_regs;
		if (slave == LK_TOPSYS_I2C_READ_SLAVE ||
		    slave == LK_TOPSYS_I2C_WRITE_SLAVE)
			return topsys_regs;
	}
	if (bus == LK_S2MPB02_I2C_BUS && slave == LK_S2MPB02_I2C_SLAVE)
		return s2mpb02_regs;
	if (bus == LK_S2DOS05_I2C_BUS && slave == LK_S2DOS05_I2C_SLAVE)
		return s2dos05_regs;
	if (bus == LK_S2MPB03_I2C_BUS && slave == LK_S2MPB03_I2C_SLAVE)
		return s2mpb03_regs;
	if (bus != LK_MAX77705_I2C_BUS)
		return NULL;
	if (!write && slave == LK_CHARGER_I2C_READ_SLAVE)
		return charger_regs;
	if (write && slave == LK_CHARGER_I2C_WRITE_SLAVE)
		return charger_regs;
	if (slave == LK_USBC_I2C_SLAVE)
		return usbc_regs;
	return NULL;
}

static uint8_t *max77705_regs_for_block_i2c(uint8_t bus, uint8_t slave)
{
	if (bus != LK_MUIC_I2C_BUS)
		return NULL;
	if (slave == LK_MUIC_I2C_READ_SLAVE ||
	    slave == LK_MUIC_I2C_WRITE_SLAVE)
		return muic_regs;
	if (slave == LK_TOPSYS_I2C_READ_SLAVE ||
	    slave == LK_TOPSYS_I2C_WRITE_SLAVE)
		return topsys_regs;
	return NULL;
}

static void max77705_block_read(uint8_t *regs, uint8_t reg, uint8_t *buffer,
				size_t length)
{
	for (size_t i = 0; i < length; i++)
		buffer[i] = regs[(uint8_t)(reg + i)];
}

static void max77705_block_write(uint8_t bus, uint8_t slave, uint8_t reg,
				 const uint8_t *buffer, size_t length)
{
	uint8_t *regs = max77705_regs_for_block_i2c(bus, slave);

	for (size_t i = 0; i < length; i++) {
		uint8_t offset = (uint8_t)(reg + i);

		if (regs == muic_regs)
			muic_write_reg(offset, buffer[i]);
		else
			regs[offset] = buffer[i];
	}
}


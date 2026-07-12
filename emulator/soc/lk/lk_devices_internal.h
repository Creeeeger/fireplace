
#ifndef FIREPLACE_SOC_LK_DEVICES_INTERNAL_H
#define FIREPLACE_SOC_LK_DEVICES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool lk_device_byte_read(uint8_t bus, uint8_t slave, uint8_t reg,
			 uint8_t *value);
bool lk_device_byte_write(uint8_t bus, uint8_t slave, uint8_t reg,
			  uint8_t value);
bool lk_device_block_read(uint8_t bus, uint8_t slave, uint8_t reg,
			  uint8_t *buffer, size_t length);
bool lk_device_block_write(uint8_t bus, uint8_t slave, uint8_t reg,
			   const uint8_t *buffer, size_t length);
bool lk_device_word_read(uint8_t bus, uint8_t slave, uint32_t reg,
			 uint32_t *value);
bool lk_device_word_write(uint8_t bus, uint8_t slave, uint32_t reg,
			  uint32_t value);
void lk_devices_reset(void);

#endif

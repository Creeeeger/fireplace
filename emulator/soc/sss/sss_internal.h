#ifndef FIREPLACE_SOC_SSS_INTERNAL_H
#define FIREPLACE_SOC_SSS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/sss/sss.h>

#include "bootchain/bootchain_internal.h"

#define SSS_MATH_STATUS (SSS_BASE + 0x00c)
#define SSS_CIPHER_SRC_ADDR_HI (SSS_BASE + 0x020)
#define SSS_CIPHER_SRC_ADDR_LO (SSS_BASE + 0x024)
#define SSS_CIPHER_SRC_LEN (SSS_BASE + 0x028)
#define SSS_CIPHER_DMA_CONTROL (SSS_BASE + 0x02c)
#define SSS_SRC_ADDR_HI (SSS_BASE + 0x040)
#define SSS_SRC_ADDR_LO (SSS_BASE + 0x044)
#define SSS_SRC_LEN (SSS_BASE + 0x048)
#define SSS_FEED_STATUS (SSS_BASE + 0x180)
#define SSS_FEED_STATUS_CLEAR (SSS_BASE + 0x18c)
#define SSS_CIPHER_CONTROL (SSS_BASE + 0x400)
#define SSS_CIPHER_STATUS (SSS_BASE + 0x404)
#define SSS_CIPHER_CONFIG (SSS_BASE + 0x408)
#define SSS_CIPHER_PIO_INPUT (SSS_BASE + 0x410)
#define SSS_CIPHER_PIO_OUTPUT (SSS_BASE + 0x420)
#define SSS_CIPHER_IV_BASE (SSS_BASE + 0x430)
#define SSS_CIPHER_IV_ALT (SSS_BASE + 0x440)
#define SSS_CIPHER_KEY_256 (SSS_BASE + 0x480)
#define SSS_CIPHER_KEY_192 (SSS_BASE + 0x488)
#define SSS_CIPHER_KEY_128 (SSS_BASE + 0x490)
#define SSS_CIPHER_DST_ADDR_HI (SSS_BASE + 0x150)
#define SSS_CIPHER_DST_ADDR_LO (SSS_BASE + 0x154)
#define SSS_CIPHER_DST_LEN (SSS_BASE + 0x158)
#define SSS_SECURE_SERVICE_STATUS (SSS_BASE + 0x010)
#define SSS_HASH_MODE (SSS_BASE + 0x1000)
#define SSS_HASH_CONTROL (SSS_BASE + 0x1008)
#define SSS_HASH_STATUS (SSS_BASE + 0x1010)
#define SSS_HASH_LEN (SSS_BASE + 0x1020)
#define SSS_HASH_KEY (SSS_BASE + 0x1100)
#define SSS_HASH_DIGEST (SSS_BASE + 0x11c0)
#define SSS_RNG_CONTROL (SSS_BASE + 0x1444)
#define SSS_RNG_COMMAND (SSS_BASE + 0x1450)
#define SSS_RNG_STATUS (SSS_BASE + 0x1460)
#define SSS_RNG_DATA (SSS_BASE + 0x1480)
#define SSS_KEY_MANAGER_COMMAND (SSS_BASE + 0x2000)
#define SSS_KEY_MANAGER_STATUS (SSS_BASE + 0x2004)
#define SSS_KEY_MANAGER_INPUT (SSS_BASE + 0x20a0)
#define SSS_KEY_MANAGER_DERIVATION (SSS_BASE + 0x2100)
#define SSS_KEY_MANAGER_DERIVATION_SIZE 0x50U
#define SSS_KEY_MANAGER_CONFIG (SSS_BASE + 0x2200)
#define SSS_MATH_COMMAND (SSS_BASE + 0x4004)
#define SSS_MATH_OPERANDS (SSS_BASE + 0x4008)
#define SSS_MATH_SIGN_LO (SSS_BASE + 0x4010)
#define SSS_MATH_SIGN_HI (SSS_BASE + 0x4014)
#define SSS_MATH_SLOT_BASE (SSS_BASE + 0x5000)

#define SSS_MATH_OP_MOD_MUL UINT32_C(0x001)
#define SSS_MATH_OP_MONT_REDUCE UINT32_C(0x101)
#define SSS_MATH_OP_MODEXP UINT32_C(0x401)
#define SSS_MATH_OP_ECC_MUL UINT32_C(0xc01)
#define SSS_MATH_OP_ECC_MUL_ADD UINT32_C(0xd01)

#define SSS_MATH_DONE UINT32_C(0x1)
#define SSS_CIPHER_DONE UINT32_C(0x1)
#define SSS_CIPHER_INPUT_READY UINT32_C(0x2)
#define SSS_CIPHER_PIO_ERROR UINT32_C(0x4)
#define SSS_CIPHER_OUTPUT_READY UINT32_C(0x8)
#define SSS_CIPHER_ERROR UINT32_C(0x40)
#define SSS_SECURE_SERVICE_READY UINT32_C(0x1)
#define SSS_FEED_READY UINT32_C(0x400)
#define SSS_HASH_ERROR UINT32_C(0x10)
#define SSS_HASH_DONE UINT32_C(0x40)
#define SSS_KEY_MANAGER_READY UINT32_C(0x1)
#define SSS_KEY_MANAGER_DONE UINT32_C(0x20)
#define SSS_RNG_ERROR UINT32_C(0x2)
#define SSS_RNG_READY UINT32_C(0x2)
#define SSS_MAX_INPUT_SIZE (64U * 1024U * 1024U)
#define SSS_REG_COUNT (SSS_SIZE / sizeof(uint32_t))

extern bool sss_hash_logged;
extern bool sss_error_logged;
extern bool sss_feed_ready;
extern bool sss_hash_pending;
extern bool sss_cipher_hash_pending;
extern uint32_t sss_regs[SSS_REG_COUNT];
extern unsigned char sss_key_manager_key[32];
extern uint32_t sss_key_manager_key_len;
extern bool sss_key_manager_key_valid;
extern bool sss_key_manager_key_device_bound;
extern bool sss_cipher_internal_key_bound;
extern bool sss_hwkek_profile_checked;
extern bool sss_hwkek_profile_available;

uint32_t sss_reg(uint64_t address);
void sss_set_reg(uc_engine *uc, uint64_t address, uint32_t value);
void sss_log_error(const char *reason, uint32_t mode, uint32_t control,
		   uint64_t source, uint32_t length);
void sss_set_status_bits(uc_engine *uc, uint32_t bits);
void sss_complete_key_manager_command(uc_engine *uc, uint32_t command);
bool sss_complete_math(uc_engine *uc, uint32_t command);
bool sss_compute_buffer_hash(uc_engine *uc, const unsigned char *input,
			     uint32_t length, uint64_t source);
bool sss_compute_hash(uc_engine *uc);
bool sss_complete_cipher_pio(uc_engine *uc);
bool sss_complete_cipher_dma(uc_engine *uc);

#endif


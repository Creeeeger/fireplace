/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/uart/uart.h>

char uart_buf[UART_BUF_SIZE] = "\x0";
pthread_mutex_t uart_lock = PTHREAD_MUTEX_INITIALIZER;
int count = 0;

atomic_int line = 0;

#define BOOTROM_CRASH_CHUNK_SIZE 8
#define BOOTROM_CRASH_RECORD_MAX 640
#define BOOTROM_CRASH_FIELD_COUNT 29

static char bootrom_crash_record[BOOTROM_CRASH_RECORD_MAX + 1];
static size_t bootrom_crash_record_len;
static bool bootrom_crash_record_reported;

void incr_line()
{
	int atLine = atomic_load(&line);
	atLine++;
	atomic_store(&line, atLine);
}

static void bootrom_crash_record_reset(void)
{
	bootrom_crash_record_len = 0;
	bootrom_crash_record[0] = '\0';
	bootrom_crash_record_reported = false;
}

static bool bootrom_crash_char_valid(char character)
{
	return (character >= '0' && character <= '9') ||
	       (character >= 'a' && character <= 'f') ||
	       (character >= 'A' && character <= 'F') ||
	       character == 'U' || character == 'V';
}

static void bootrom_crash_chunk(size_t index,
				char chunk[BOOTROM_CRASH_CHUNK_SIZE + 1])
{
	memcpy(chunk, bootrom_crash_record + index * BOOTROM_CRASH_CHUNK_SIZE,
	       BOOTROM_CRASH_CHUNK_SIZE);
	chunk[BOOTROM_CRASH_CHUNK_SIZE] = '\0';
}

static void bootrom_crash_print_field(const char *name, size_t index)
{
	char chunk[BOOTROM_CRASH_CHUNK_SIZE + 1];

	bootrom_crash_chunk(index, chunk);
	fprintf(stderr, "[BootROM crash] %s: %s\n", name, chunk);
}

static void bootrom_crash_try_print(void)
{
	if (bootrom_crash_record_reported ||
	    bootrom_crash_record_len <
		    BOOTROM_CRASH_FIELD_COUNT * BOOTROM_CRASH_CHUNK_SIZE)
		return;

	bootrom_crash_record_reported = true;
	fputc('\n', stderr);
	fprintf(stderr, "[BootROM crash] Crash information:\n");
	bootrom_crash_print_field("Version", 1);
	bootrom_crash_print_field("SoC ID", 2);
	bootrom_crash_print_field("Chip ID0", 3);
	bootrom_crash_print_field("Chip ID1", 4);
	bootrom_crash_print_field("Reset Status", 9);
	bootrom_crash_print_field("OM Status", 11);
	bootrom_crash_print_field("Key Bank Address", 13);
	bootrom_crash_print_field("Boot Device Info", 26);
	bootrom_crash_print_field("Status Register0", 27);
	bootrom_crash_print_field("Status Register1", 28);
}

static void bootrom_crash_record_append(char character)
{
	if (character == '\n' || character == '\r') {
		bootrom_crash_record_reset();
		return;
	}
	if (!bootrom_crash_char_valid(character)) {
		bootrom_crash_record_reset();
		return;
	}
	if (bootrom_crash_record_len < BOOTROM_CRASH_CHUNK_SIZE &&
	    character != 'U') {
		bootrom_crash_record_reset();
		return;
	}
	if (bootrom_crash_record_len >= BOOTROM_CRASH_RECORD_MAX) {
		bootrom_crash_record_reset();
		return;
	}

	bootrom_crash_record[bootrom_crash_record_len++] = character;
	bootrom_crash_record[bootrom_crash_record_len] = '\0';
	bootrom_crash_try_print();
}

static void uart_append_char_locked(char character)
{
	if (count >= UART_BUF_SIZE - 1) {
		memset(uart_buf, '\0', sizeof(uart_buf));
		count = 0;
		atomic_store(&line, 0);
		bootrom_crash_record_reset();
	}
	uart_buf[count++] = character;
	uart_buf[count] = '\0';
	if (character == '\n')
		incr_line();
	bootrom_crash_record_append(character);
	fputc(character, stdout);
}

void uart_append_text(const char *text)
{
	if (!text)
		return;
	pthread_mutex_lock(&uart_lock);
	for (const char *p = text; *p; p++)
		uart_append_char_locked(*p);
	pthread_mutex_unlock(&uart_lock);
}

int uart_init(struct uc_struct *uc_s)
{
	uint32_t tx_ready = 2;
	uint32_t tx = 0;

	printf("= uart_init\n");
	uc_mem_write(uc_s, UART_BASE + 0x10, &tx_ready, sizeof(tx_ready));
	uc_mem_write(uc_s, UART_BASE + UART_TX_OFFSET, &tx, sizeof(tx));
	return 0;
}

void uart_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data)
{
	char character;

	if (type != UC_MEM_WRITE || address != UART_BASE + UART_TX_OFFSET)
		return;

	character = (char)value;
	pthread_mutex_lock(&uart_lock);
	uart_append_char_locked(character);
	pthread_mutex_unlock(&uart_lock);
}

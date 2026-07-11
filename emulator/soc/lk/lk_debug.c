
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fireplace/soc/uart/uart.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_debug.h"

static void read_lk_cstring(uc_engine *uc, uint64_t address, char *buffer,
			    size_t buffer_size)
{
	size_t i = 0;

	if (buffer_size == 0)
		return;
	if (address == 0) {
		snprintf(buffer, buffer_size, "(null)");
		return;
	}
	while (i < buffer_size - 1) {
		uint8_t c = 0;

		if (uc_mem_read(uc, address + i, &c, sizeof(c)) != UC_ERR_OK) {
			snprintf(buffer + i, buffer_size - i,
				 "<unmapped @0x%" PRIx64 ">", address + i);
			return;
		}
		if (c == 0)
			break;
		buffer[i++] = (char)c;
	}
	buffer[i] = '\0';
}

static uint64_t lk_next_printf_arg(uc_engine *uc, const uint64_t *registers,
				   int *argument_index, uint64_t stack,
				   int *stack_index)
{
	uint64_t value = 0;

	if (*argument_index <= 7)
		return registers[(*argument_index)++];
	uc_mem_read(uc, stack + 8 * (*stack_index), &value, sizeof(value));
	(*stack_index)++;
	return value;
}

static void format_lk_printf(uc_engine *uc, const char *format,
			     const uint64_t *registers, uint64_t stack,
			     char *output, size_t output_size)
{
	size_t output_position = 0;
	int argument_index = 1;
	int stack_index = 0;

	for (const char *p = format; *p && output_position < output_size - 1;
	     p++) {
		char piece[1024];
		char specifier[32];
		const char *start;
		size_t specifier_length;
		size_t piece_length;
		uint64_t value;
		int length = 0;

		if (*p != '%') {
			output[output_position++] = *p;
			continue;
		}
		start = p++;
		if (*p == '%') {
			output[output_position++] = '%';
			continue;
		}
		while (*p && strchr("-+ #0", *p))
			p++;
		while (*p && isdigit((unsigned char)*p))
			p++;
		if (*p == '.') {
			p++;
			while (*p && isdigit((unsigned char)*p))
				p++;
		}
		while (*p == 'l' || *p == 'h' || *p == 'z' || *p == 'j' ||
		       *p == 't') {
			if (*p == 'l')
				length++;
			p++;
		}
		specifier_length = (size_t)(p - start) + 1;
		if (specifier_length >= sizeof(specifier))
			specifier_length = sizeof(specifier) - 1;
		memcpy(specifier, start, specifier_length);
		specifier[specifier_length] = '\0';

		switch (*p) {
		case 'd':
		case 'i':
			value = lk_next_printf_arg(uc, registers, &argument_index,
						   stack, &stack_index);
			if (length >= 2)
				snprintf(piece, sizeof(piece), specifier,
					 (long long)(int64_t)value);
			else if (length == 1)
				snprintf(piece, sizeof(piece), specifier,
					 (long)(int64_t)value);
			else
				snprintf(piece, sizeof(piece), specifier,
					 (int)(int32_t)value);
			break;
		case 'u':
		case 'x':
		case 'X':
		case 'o':
			value = lk_next_printf_arg(uc, registers, &argument_index,
						   stack, &stack_index);
			if (length >= 2)
				snprintf(piece, sizeof(piece), specifier,
					 (unsigned long long)value);
			else if (length == 1)
				snprintf(piece, sizeof(piece), specifier,
					 (unsigned long)value);
			else
				snprintf(piece, sizeof(piece), specifier,
					 (unsigned int)value);
			break;
		case 'p':
			value = lk_next_printf_arg(uc, registers, &argument_index,
						   stack, &stack_index);
			snprintf(piece, sizeof(piece), "0x%" PRIx64, value);
			break;
		case 'c':
			value = lk_next_printf_arg(uc, registers, &argument_index,
						   stack, &stack_index);
			snprintf(piece, sizeof(piece), "%c", (int)value);
			break;
		case 's':
			value = lk_next_printf_arg(uc, registers, &argument_index,
						   stack, &stack_index);
			read_lk_cstring(uc, value, output + output_position,
					output_size - output_position);
			output_position += strlen(output + output_position);
			continue;
		case 'f':
		case 'e':
		case 'g':
		case 'F':
		case 'E':
		case 'G':
			(void)lk_next_printf_arg(uc, registers, &argument_index,
						 stack, &stack_index);
			snprintf(piece, sizeof(piece), "<float:not-supported>");
			break;
		default:
			snprintf(piece, sizeof(piece), "%s", specifier);
			break;
		}
		piece_length = strlen(piece);
		if (output_position + piece_length >= output_size - 1)
			piece_length = output_size - 1 - output_position;
		memcpy(output + output_position, piece, piece_length);
		output_position += piece_length;
		if (*p == '\0')
			break;
	}
	output[output_position] = '\0';
}

void lk_printf_cb(uc_engine *uc, uint64_t address, uint32_t size,
		  void *user_data)
{
	uint64_t registers[8] = {0};
	uint64_t stack = 0;
	char format[1024];
	char formatted[4096];

	(void)address;
	(void)size;
	(void)user_data;
	if (bootchain_stage() != BOOTCHAIN_STAGE_LK)
		return;
	for (int i = 0; i < 8; i++)
		uc_reg_read(uc, UC_ARM64_REG_X0 + i, &registers[i]);
	uc_reg_read(uc, UC_ARM64_REG_SP, &stack);
	read_lk_cstring(uc, registers[0], format, sizeof(format));
	format_lk_printf(uc, format, registers, stack, formatted,
			 sizeof(formatted));
	uart_append_text(formatted);
	(void)bootchain_return_to_link(uc);
}

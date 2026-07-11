/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FIREPLACE_UART_H
#define FIREPLACE_UART_H

#include <unicorn/unicorn.h>

#define UART_BUF_SIZE 80000
#define UART_BASE 0x10540000ULL
#define UART_TX_OFFSET 0x20

int uart_init(struct uc_struct*);
void uart_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data);
void uart_append_text(const char *text);

#endif
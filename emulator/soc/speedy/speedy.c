/*
 *   Copyright (c) 2025 Umer Uddin <umer.uddin@mentallysanemainliners.org>
 *
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
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/speedy/speedy.h>

#define S2MPS19_STATUS1_REG 0x0a
#define S2MPS19_STATUS1_JIGONB (1U << 1)

static uint8_t speedy_0_slave_reg[0x100];
static uint8_t speedy_1_slave_reg[0x100];
static uint8_t speedy_0_reg;
static uint8_t speedy_1_reg;

bool speedy_0_en = true;
bool speedy_1_en = true;

int cnt = 0;

uint32_t base = 0x15940000;

pthread_mutex_t speedy_lock = PTHREAD_MUTEX_INITIALIZER;

int speedy_init(struct uc_struct *uc_s)
{
	uint32_t speedy_initial_state = 1;
	uc_err err;

	/* JIGONB is active-low; leave the emulated JIG input deasserted. */
	speedy_0_slave_reg[S2MPS19_STATUS1_REG] =
		S2MPS19_STATUS1_JIGONB;

	err = uc_mem_write(uc_s, 0x15940000 + SPEEDY_CTRL,
			   &speedy_initial_state, sizeof(speedy_initial_state));
	if (err == UC_ERR_OK)
		err = uc_mem_write(uc_s, 0x15950000 + SPEEDY_CTRL,
				   &speedy_initial_state,
				   sizeof(speedy_initial_state));
	return err;
}

void speedy_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data)
{
	uint32_t val;
	uint8_t *slave_regs;
	uint8_t *selected_reg;
	uc_err err = UC_ERR_OK;

	pthread_mutex_lock(&speedy_lock);

	if(address >= 0x15950000)
		base = 0x15950000;
	else
		base = 0x15940000;

	if (base == 0x15940000) {
		slave_regs = speedy_0_slave_reg;
		selected_reg = &speedy_0_reg;
	} else {
		slave_regs = speedy_1_slave_reg;
		selected_reg = &speedy_1_reg;
	}

	if(type == UC_MEM_WRITE)
	{
		switch(address - base)
		{
			case SPEEDY_CTRL:
				if(value & 1)
				{
					if(base == 0x15940000)
						speedy_0_en = true;
					else
						speedy_1_en = true;

				}
				break;

			case SPEEDY_FIFO_CTRL:
				if((value >> 31) & 1)
				{
					uint32_t val = 0x0;

					err = uc_mem_write(uc, address, &val, sizeof(val));
					if (err == UC_ERR_OK)
						err = uc_mem_write(uc,
							   base + SPEEDY_FIFO_STATUS,
							   &val, sizeof(val));
				}
				break;

			case SPEEDY_CMD:
				*selected_reg = (uint8_t)((value >> 7) & 0xff);
				if (!(value & SPEEDY_DIRECTION_WRITE)) {
					val = slave_regs[*selected_reg];
					err = uc_mem_write(uc, base + SPEEDY_RX_DATA,
							   &val, sizeof(val));
				}
				break;

			case SPEEDY_TX_DATA:
				slave_regs[*selected_reg] = (uint8_t)value;
				break;
		}
	}
	else
	{
		switch(address - base)
		{
			case SPEEDY_CTRL:
				val = 0x1;

				err = uc_mem_write(uc, address, &val, sizeof(val));
				break;

			case SPEEDY_FIFO_STATUS:
				val = 1 << 6;

				err = uc_mem_write(uc, address, &val, sizeof(val));
				break;
		}
	}
	pthread_mutex_unlock(&speedy_lock);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to update SPEEDY state: %s\n",
			uc_strerror(err));
		uc_emu_stop(uc);
	}
}

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

char speedy_0_slave_reg[0x100] = "\x0";
char speedy_1_slave_reg[0x100] = "\x0";

bool speedy_0_en = true;
bool speedy_1_en = true;

int cnt = 0;

uint32_t base = 0x15940000;

pthread_mutex_t speedy_lock = PTHREAD_MUTEX_INITIALIZER;

int speedy_init(struct uc_struct *uc_s)
{
	uint32_t speedy_initial_state = 1;

	printf("= speedy_init\n");

	uc_mem_write(uc_s, 0x15940000 + SPEEDY_CTRL, &speedy_initial_state, 0x4);
        uc_mem_write(uc_s, 0x15950000 + SPEEDY_CTRL, &speedy_initial_state, 0x4);

	return 0;
}

void speedy_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data)
{
	pthread_mutex_lock(&speedy_lock);

	if(address >= 0x15950000)
		base = 0x15950000;
	else
		base = 0x15940000;

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

                                        printf("SPEEDY %i now enabled!\n",
					       address >= 0x15950000 ? 1 : 0);
				}

				if ((value >> 31) & 1)
				{
					printf("SWRESET Requested on SPEEDY %i\n",
					       address >= 0x15950000 ? 1 : 0);
				}
				break;

			case SPEEDY_FIFO_CTRL:
				if((value >> 31) & 1)
				{
					uint32_t val = 0x0;

					printf("FIFO RESET Requested on SPEEDY %i\n",
					       address >= 0x15950000 ? 1 : 0);

					uc_mem_write(uc, address, &val, 0x4);
					uc_mem_write(uc, base + SPEEDY_FIFO_STATUS, &val, 0x4);
				}
				break;
		}
	}
	else
	{
		switch(address - base)
		{
			case SPEEDY_CTRL:
				uint32_t val = 0x1;

				uc_mem_write(uc, address, &val, 0x4);
				break;

			case SPEEDY_FIFO_STATUS:
				val = 1 << 6;

				printf("Probably wants FIFO empty, just writing it.\n");
				uc_mem_write(uc, address, &val, 0x4);
				break;
		}
	}
	pthread_mutex_unlock(&speedy_lock);
}

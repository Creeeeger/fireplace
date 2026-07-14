/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <inttypes.h>
#include <stdio.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/soc.h>
#include <fireplace/soc/peripherals.h>
#include <fireplace/soc/apm/apm.h>
#include <fireplace/soc/bootchain/bootchain.h>
#include <fireplace/soc/cmu/cmu.h>
#include <fireplace/soc/dmc/dmc.h>
#include <fireplace/soc/fb/fb.h>
#include <fireplace/soc/gpio/gpio_alive.h>
#include <fireplace/soc/mct/mct.h>
#include <fireplace/soc/otp/otp.h>
#include <fireplace/soc/speedy/speedy.h>
#include <fireplace/soc/sss/sss.h>
#include <fireplace/soc/uart/uart.h>
#include <fireplace/soc/usb/usb.h>
#include <fireplace/soc/ufs/ufs.h>

static struct peripheral exynos990_peripherals[] = {
	{"apm", APM_BASE, APM_SIZE, apm_init, apm_hook},
	{"cmu_mif", CMU_MIF_BASE, CMU_MIF_SIZE, cmu_init, cmu_hook},
	{"cmu_mcsc", CMU_MCSC_BASE, CMU_MIF_SIZE, cmu_init, cmu_hook},
	{"dmc_0", DMC0_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_1", DMC1_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_2", DMC2_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_3", DMC3_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_phy_0", DMC_PHY0_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_phy_1", DMC_PHY1_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_phy_2", DMC_PHY2_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"dmc_phy_3", DMC_PHY3_BASE, DMC_SIZE, dmc_init, dmc_hook},
	{"mct", MCT_BASE, MCT_SIZE, mct_init, mct_hook},
	{"mct_local", MCT_LOCAL_BASE, MCT_LOCAL_SIZE, mct_init, mct_hook},
	{"otp_con_top", OTP_CON_TOP_BASE, OTP_CON_TOP_SIZE, otp_init, otp_hook},
	{"uart", UINT64_C(0x10540000), 0x1000, uart_init, uart_hook},
	{"gpio_alive", UINT64_C(0x15850000), 0x1000, gpio_alive_init,
	 gpio_alive_hook},
	{"speedy_0", UINT64_C(0x15940000), 0x1000, speedy_init, speedy_hook},
	{"speedy_1", UINT64_C(0x15950000), 0x1000, speedy_init, speedy_hook},
	{"sss", SSS_BASE, SSS_SIZE, sss_init, sss_hook},
	{"ufs", UINT64_C(0x13100000), 0x80000, ufs_init, ufs_hook},
	/* The framebuffer is backed by host memory and therefore needs no MMIO
	 * callback; its dimensions are part of the current G986B profile. */
	{"framebuffer", FB_ADDRESS, FB_SIZE, fb_init, NULL},
};

bool soc_bootchain_complete(void)
{
	return bootchain_complete();
}

bool soc_bootchain_failed(void)
{
	return bootchain_failed();
}

const char *soc_active_firmware_stage(void)
{
	return bootchain_active_stage();
}

bool soc_take_resume_request(uint64_t *address)
{
	return bootchain_take_resume_request(address);
}

static uc_err soc_peripheral_init_one(uc_engine *uc,
				      struct peripheral *peri)
{
	uc_hook handle;
	uc_err err;
	int init_result;

	init_result = peri->init(uc);
	if (init_result != UC_ERR_OK) {
		fprintf(stderr, "Failed to initialize %s: %s\n", peri->name,
			uc_strerror((uc_err)init_result));
		return (uc_err)init_result;
	}

	if (peri->access) {
		err = uc_hook_add(uc, &handle,
				  UC_HOOK_MEM_WRITE | UC_HOOK_MEM_READ,
				  (void *)peri->access, peri, peri->base,
				  peri->base + peri->size - 1);
		if (err != UC_ERR_OK) {
			fprintf(stderr, "Failed to hook %s: %s\n", peri->name,
				uc_strerror(err));
			return err;
		}
	}

	return UC_ERR_OK;
}

static bool mem_invalid_cb(uc_engine *uc, uc_mem_type type,
                           uint64_t address, int size, int64_t value, void *user_data) {
	uint64_t pc = 0;
	uint64_t x0 = 0;
	uint64_t lr = 0;
	uint64_t aligned_address;
	uc_err err;

	(void)value;
	(void)user_data;

	if (bootchain_handle_invalid_memory(uc, address))
		return true;

	if (uc_reg_read(uc, UC_ARM64_REG_PC, &pc) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_X0, &x0) != UC_ERR_OK ||
	    uc_reg_read(uc, UC_ARM64_REG_LR, &lr) != UC_ERR_OK)
		fprintf(stderr, "Failed to read invalid-access context\n");
	fprintf(stderr, "Invalid memory access type %u in %s at 0x%" PRIx64
		" (size=%d PC=0x%" PRIx64 " LR=0x%" PRIx64
		" x0=0x%" PRIx64 ")\n",
		type, soc_active_firmware_stage(), address, size, pc, lr, x0);

	aligned_address = address & ~UINT64_C(0xfff);
	err = uc_mem_map(uc, aligned_address, 0x1000, UC_PROT_ALL);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to map invalid-access page 0x%" PRIx64
			": %s\n", aligned_address, uc_strerror(err));
		return false;
	}
	printf("[soc] mapped invalid-access page 0x%" PRIx64 "\n",
	       aligned_address);
	return true;
}

static void interrupt_cb(uc_engine *uc, uint32_t intno, void *user_data)
{
	(void)user_data;
	if ((intno == 1 || intno == 3) &&
	    bootchain_handle_system_instruction(uc))
		return;
	if (intno == 1) {
		uint64_t pc = 0;

		if (uc_reg_read(uc, UC_ARM64_REG_PC, &pc) == UC_ERR_OK &&
		    bootchain_route_undefined_instruction(uc, pc))
			return;
	}
	if (intno == 13) {
		uint64_t pc = 0;

		if (uc_reg_read(uc, UC_ARM64_REG_PC, &pc) == UC_ERR_OK &&
		    bootchain_route_smc(uc, pc))
			return;
		fprintf(stderr, "Unsupported SMC in %s at 0x%" PRIx64 "\n",
			soc_active_firmware_stage(), pc);
		uc_emu_stop(uc);
		return;
	}

	{
		uint64_t pc = 0;
		uint64_t pstate = 0;
		uint64_t sp = 0;
		uint64_t lr = 0;
		uint64_t x0 = 0;
		uint64_t x1 = 0;
		uint32_t current = UINT32_C(0xffffffff);
		uint32_t previous = UINT32_C(0xffffffff);

		(void)uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
		(void)uc_reg_read(uc, UC_ARM64_REG_PSTATE, &pstate);
		(void)uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
		(void)uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
		(void)uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
		(void)uc_reg_read(uc, UC_ARM64_REG_X1, &x1);
		(void)uc_mem_read(uc, pc, &current, sizeof(current));
		if (pc >= sizeof(previous))
			(void)uc_mem_read(uc, pc - sizeof(previous), &previous,
					  sizeof(previous));

		fprintf(stderr,
			"Unsupported CPU exception in %s: %u "
			"PC=0x%016" PRIx64 " current=0x%08" PRIx32
			" previous=0x%08" PRIx32 " pstate=0x%016" PRIx64
			" sp=0x%016" PRIx64 " lr=0x%016" PRIx64
			" x0=0x%016" PRIx64 " x1=0x%016" PRIx64 "\n",
			soc_active_firmware_stage(), intno, pc, current,
			previous, pstate, sp, lr, x0, x1);
	}
	uc_emu_stop(uc);
}

int soc_peripherals_init_configured(uc_engine *uc, const struct soc_boot_config *config)
{
	struct bootchain_config bootchain_config = {0};
	uc_hook interrupts;
	uc_hook invalid_memory;
	uc_err err;

	if (!config)
		return UC_ERR_ARG;
	ufs_set_lun_dump_dir(config->lun_directory);
	for (size_t i = 0; i < sizeof(exynos990_peripherals) /
				     sizeof(exynos990_peripherals[0]); i++) {
		printf("Initializing peripheral %s\n",
			exynos990_peripherals[i].name);

		err = soc_peripheral_init_one(uc, &exynos990_peripherals[i]);
		if (err != UC_ERR_OK)
			return err;
	}

	err = uc_hook_add(uc, &invalid_memory, UC_HOOK_MEM_INVALID,
			  (void *)mem_invalid_cb, NULL, 1, 0);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to hook invalid memory accesses: %s\n",
			uc_strerror(err));
		return err;
	}
	err = uc_hook_add(uc, &interrupts, UC_HOOK_INTR,
			  (void *)interrupt_cb, NULL, 1, 0);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Failed to hook CPU interrupts: %s\n",
			uc_strerror(err));
		return err;
	}
	bootchain_config.image_directory = config->bootchain_directory;
	bootchain_config.lun_directory = config->lun_directory;
	bootchain_config.boot_mode = config->boot_mode;
	bootchain_config.headless = config->headless;
	err = bootchain_init(uc, &bootchain_config);
	if (err != UC_ERR_OK)
		fprintf(stderr, "Failed to initialize bootchain: %s\n",
			uc_strerror(err));
	return err;
}

int soc_peripherals_init(uc_engine *uc)
{
	(void)uc;
	return UC_ERR_ARG;
}

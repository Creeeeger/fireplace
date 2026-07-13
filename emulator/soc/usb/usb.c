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

#include <inttypes.h>
#include <stdio.h>

#include <unicorn/unicorn.h>

#include <fireplace/soc/usb/usb.h>

#define USB_ADDRESS(off) (USB_DWC_BASE + off)
#define USB_PHY_ADDRESS(off) (USB_PHY_BASE + off)

int usb_phy_init(struct uc_struct *uc_s)
{
	(void)uc_s;
	printf("?>>> usb_phy_init\n");
	return UC_ERR_OK;
}

void usb_phy_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data)
{
	switch(address)
	{
		case USB_PHY_ADDRESS(EXYNOS_USBCON_UTMI):
			printf("UTMI setup! R: 0x%" PRIx64 "\n", (uint64_t)value);
			break;
		case USB_PHY_ADDRESS(EXYNOS_USBCON_HSP_TUNE):
			printf("Tuning setup! R: 0x%" PRIx64 "\n",
			       (uint64_t)value);
			break;
		case USB_PHY_ADDRESS(EXYNOS_USBCON_LINK_CTRL):
			printf("Link setup! R: 0x%" PRIx64 "\n", (uint64_t)value);
			break;
		case USB_PHY_ADDRESS(EXYNOS_USBCON_HSP):
			printf("HSP setup! R: 0x%" PRIx64 "\n", (uint64_t)value);
			break;
		case USB_PHY_ADDRESS(EXYNOS_USBCON_HSP_TEST):
			printf("HSP test? R: 0x%" PRIx64 "\n", (uint64_t)value);
			break;
		default:
			printf("PHY> Unhandled read. ");
			printf("> A: 0x%" PRIx64 " R: 0x%" PRIx64 "\n",
			       address, (uint64_t)value);
			break;
	}
}

int usb_init(struct uc_struct *uc_s)
{
	printf("= usb_init\n");
	return uc_mem_write(uc_s, USB_ADDRESS(rGSNPSID), "\x00\x30", 2);
}

uc_err usb_power_switch(uc_engine *uc, uint32_t dctl_buf, bool pwr)
{
	uc_err err;

	if (pwr == true)
		dctl_buf &= ~(1 << 30);
	else
		dctl_buf &= ~(0 << 30);

	err = uc_mem_write(uc, USB_ADDRESS(rDCTL), &dctl_buf,
			   sizeof(dctl_buf));

	printf("---------USB controller power state changed: %i ---------\n", pwr);

	if(err != UC_ERR_OK)
		printf("Couldn't do dctl command. Sorry.\n");

	return err;
}

uc_err handle_dctl(uc_engine *uc)
{
	uc_err err;
	uint32_t dctl_buf;

	err = uc_mem_read(uc, USB_ADDRESS(rDCTL), &dctl_buf, 4);

	if (err != UC_ERR_OK)
	{
		printf("Couldn't handle DCTL event! (failed to read memory)\n");
		return err;
	}

	uint32_t core_soft_reset_value = (dctl_buf >> 30) & 1;

	/*
	 * 0 - power off USB controller
	 * 1 - power on USB controller
	*/
	if (core_soft_reset_value == 1)
		usb_power_switch(uc, dctl_buf, true);
	else
		usb_power_switch(uc, dctl_buf, false);

	return err;
}

uc_err handle_epcmd(uc_engine *uc, uint64_t addr)
{
	uc_err err;
	uint32_t epcmd_buf;

	err = uc_mem_read(uc, addr, &epcmd_buf, 4);

	if (err != UC_ERR_OK)
	{
		printf("Couldn't handle EPCMD event! (failed to read memory)\n");
		return err;
	}

	epcmd_buf &= ~(1 << 10);

	err = uc_mem_write(uc, addr, &epcmd_buf, sizeof(epcmd_buf));

	return err;
}

void usb_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data)
{
	switch(address)
	{
		case USB_ADDRESS(rDCTL):
			handle_dctl(uc);
			break;
		case USB_ADDRESS(rGSNPSID):
			printf("USB: Reading IP version (3.0)\n");
			break;
		case USB_ADDRESS(rGFLADJ):
			printf("USB: reading Global Frame Length Adjustment Register\n");
			break;

			/* G986B endpoint-command registers outside the named DWC map. */
		case 0x10e0c80c:
			handle_epcmd(uc, 0x10e0c80c);
			break;

		case 0x10e0c81c:
			handle_epcmd(uc, 0x10e0c81c);
			break;

		default:
			if(type == UC_MEM_READ)
				printf("USB> Unhandled read. ");
			else
				printf("USB> Unhandled write. ");
			break;

	}
	printf("> A: 0x%" PRIx64 " R: 0x%" PRIx64 "\n", address,
	       (uint64_t)value);
}

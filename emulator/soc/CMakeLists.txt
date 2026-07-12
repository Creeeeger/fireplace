find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)
pkg_check_modules(CRYPTO REQUIRED IMPORTED_TARGET libcrypto)

add_library(fireplace_soc STATIC
	apm/apm.c
	cmu/cmu.c
	dmc/dmc.c
	fb/fb.c
	gpio/exynos_gpio.c
	gpio/gpio_alive.c
	hardware_buttons/hardware_buttons.c
	mct/mct.c
	memmap.c
	otp/otp.c
	soc.c
	speedy/speedy.c
	sss/sss.c
	sss/sss_cipher.c
	sss/sss_hash.c
	sss/sss_key.c
	sss/sss_math.c
	uart/uart.c
	usb/usb.c
	ufs/ufs.c
	ufs/ufs_descriptors.c
	ufs/ufs_endian.c
	ufs/ufs_prdt.c
	ufs/ufs_query.c
	ufs/ufs_rpmb.c
	ufs/ufs_scsi.c
	ufs/ufs_storage.c
	ufs/ufs_transfer.c
)

target_sources(fireplace_soc PRIVATE
	bl1/bl1.c
	bl2/bl2.c
	bootchain/bootchain.c
	bootchain/crypto.c
	bootchain/cpu.c
	bootchain/cpu_lse.c
	bootchain/cpu_mmu.c
	bootchain/cpu_runtime.c
	bootchain/cpu_system.c
	bootchain/hooks.c
	bootchain/images.c
	bootrom/bootrom.c
	bootrom/auth.c
	bootrom/fuses.c
	bootrom/services.c
	el3_mon/el3_mon.c
	el3_mon/context.c
	el3_mon/invalid_memory.c
	el3_mon/ldfw_mmu.c
	el3_mon/ldfw_service.c
	el3_mon/lk_handoff.c
	el3_mon/returns.c
	el3_mon/route_lk.c
	el3_mon/route_runtime.c
	el3_mon/route_secure.c
	el3_mon/secure_hooks.c
	el3_mon/secure_mmu.c
	el3_mon/secure_shadow.c
	el3_mon/worlds.c
	epbl/epbl.c
	lk/lk.c
	lk/lk_boot.c
	lk/lk_debug.c
	lk/lk_devices.c
	lk/lk_patches.c
)

target_include_directories(fireplace_soc PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fireplace_soc PRIVATE
	fireplace_include
	PkgConfig::UNICORN
	PkgConfig::CRYPTO
)

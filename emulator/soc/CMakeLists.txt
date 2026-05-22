find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)
pkg_check_modules(CRYPTO REQUIRED IMPORTED_TARGET libcrypto)

add_library(fireplace_soc STATIC
	fb/fb.c
	gpio/exynos_gpio.c
	gpio/gpio_alive.c
	hardware_buttons/hardware_buttons.c
	memmap.c
	soc.c
	speedy/speedy.c
	uart/uart.c
	usb/usb.c
	ufs/ufs.c
	ufs/ufs_descriptors.c
	ufs/ufs_endian.c
	ufs/ufs_prdt.c
	ufs/ufs_query.c
	ufs/ufs_transfer.c
)

target_include_directories(fireplace_soc PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fireplace_soc PRIVATE
	fireplace_include
	PkgConfig::UNICORN
	PkgConfig::CRYPTO
)

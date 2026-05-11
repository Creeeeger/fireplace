# Core emulator components

find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)

add_executable(fireplace main.c emulator.c)
target_compile_definitions(fireplace PRIVATE FIREPLACE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(fireplace PUBLIC fireplace_gui fireplace_soc PkgConfig::UNICORN)

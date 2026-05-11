cmake_minimum_required(VERSION 3.10)
project(fireplace C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(include)
add_subdirectory(emulator)
add_subdirectory(gui)

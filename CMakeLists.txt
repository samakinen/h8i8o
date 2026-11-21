# SPDX-License-Identifier: GPL-3.0-or-later
# H8i8o firmware: KNstaX on the GD32F103CB with the TP-UART2.
#
# Inside the toolchain image (README, "Build the firmware"):
#
#   cmake -S . -B build/fw -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
#         -DCMAKE_TOOLCHAIN_FILE=external/KNstaX/cmake/toolchains/arm-none-eabi.cmake
#   cmake --build build/fw
#
# KNSTAX_DIR may point at another KNstaX checkout; the default is the
# submodule.

cmake_minimum_required(VERSION 3.21)
project(h8i8o LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "" FORCE)
endif()

set(KNSTAX_DIR "${CMAKE_CURRENT_LIST_DIR}/external/KNstaX" CACHE PATH "KNstaX source tree")
option(H8I8O_LTO "Link with -flto" ON)
# Every octet between the MCU and the TP-UART2, over RTT. For diagnosing the
# host link (an ETS download, say); it costs about 3.6 KiB of RAM.
option(H8I8O_TRACE "Trace the host link over RTT" OFF)

include(${KNSTAX_DIR}/cmake/knx_config.cmake)
knx_configure(PROFILE tp1-small OVERRIDES ${CMAKE_CURRENT_LIST_DIR}/knx.defconfig)
add_subdirectory(${KNSTAX_DIR} knstax EXCLUDE_FROM_ALL)

add_executable(h8i8o
    startup/startup.c
    startup/syscalls.c
    bsp/board.c
    bsp/usart1.c
    bsp/gd32_fmc.c
    bsp/rtt.c
    app/main.cpp
)
target_include_directories(h8i8o PRIVATE bsp app)
target_compile_definitions(h8i8o PRIVATE H8I8O_TRACE=$<IF:$<BOOL:${H8I8O_TRACE}>,1,0>)
target_compile_features(h8i8o PRIVATE cxx_std_23)
target_compile_options(h8i8o PRIVATE -g -Wall -Wextra)
target_link_libraries(h8i8o PRIVATE knx_core)
target_link_options(h8i8o PRIVATE
    -T${CMAKE_CURRENT_LIST_DIR}/startup/link.ld
    -nostartfiles
    -Wl,-Map=$<TARGET_FILE:h8i8o>.map
    -Wl,--print-memory-usage
)
set_target_properties(h8i8o PROPERTIES SUFFIX ".elf")

if(H8I8O_LTO)
    target_compile_options(knx_core PRIVATE -flto)
    target_compile_options(h8i8o PRIVATE -flto)
    target_link_options(h8i8o PRIVATE -flto)
endif()

add_custom_command(TARGET h8i8o POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:h8i8o> $<TARGET_FILE_DIR:h8i8o>/h8i8o.bin
    COMMAND arm-none-eabi-size $<TARGET_FILE:h8i8o>
)

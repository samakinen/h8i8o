# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the channel logic (app/logic): no KNstaX, no hardware.
#
#   cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test
#
# On Windows with MinGW, add -DCMAKE_EXE_LINKER_FLAGS=-static so the test
# programs do not depend on the libstdc++ DLL found first on PATH.

cmake_minimum_required(VERSION 3.21)
project(h8i8o_logic_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../app)

add_library(h8i8o_logic STATIC
    ${APP_DIR}/logic/input_channel.cpp
    ${APP_DIR}/logic/output_channel.cpp
)
target_include_directories(h8i8o_logic PUBLIC ${APP_DIR})
target_compile_options(h8i8o_logic PUBLIC -Wall -Wextra -Werror)

foreach(name test_input_channel test_output_channel test_debounce)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE h8i8o_logic)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

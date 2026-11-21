# SPDX-License-Identifier: GPL-3.0-or-later
# ETS product file for the H8i8o, from app/product.hpp (the header the
# firmware compiles), through KNstaX's exporter. Host build:
#
#   MSYS_NO_PATHCONV=1 docker run --rm -v <h8i8o>:/work -w /work knstax-host \
#       sh -c "cmake -S tools/knxprod -B build/knxprod -G Ninja && cmake --build build/knxprod"
#
# Output: build/knxprod/h8i8o.knxprod.xml. It is not a signed .knxprod; see
# tools/knxprod/README.md for the import into ETS6.

cmake_minimum_required(VERSION 3.21)
project(h8i8o_knxprod LANGUAGES C CXX)

set(KNSTAX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external/KNstaX" CACHE PATH "KNstaX source tree")
include(${KNSTAX_DIR}/cmake/knx_config.cmake)
# host-minimal has the tp1-small capacities (128 addresses, 160 associations,
# APDU 15). The exporter writes the table sizes from the build's
# configuration, so they must be the device's: check both defconfigs when
# either changes.
knx_configure(PROFILE host-minimal OVERRIDES ${CMAKE_CURRENT_LIST_DIR}/../../knx.defconfig)
add_subdirectory(${KNSTAX_DIR} knstax EXCLUDE_FROM_ALL)
include(${KNSTAX_DIR}/cmake/knx_product.cmake)

knx_commissioned_product(h8i8o
    DEFINITION ${CMAKE_CURRENT_LIST_DIR}/../../app/product.hpp
    SYMBOL h8i8o::kProduct
)

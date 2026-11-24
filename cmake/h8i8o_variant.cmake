# SPDX-License-Identifier: GPL-3.0-or-later
# Build variants (docs/design/channel-functions.md §3.5).
#
#   include(cmake/h8i8o_variant.cmake)
#   h8i8o_variant(${H8I8O_VARIANT})            # before knx_configure()
#   knx_configure(PROFILE tp1-small OVERRIDES knx.defconfig ${H8I8O_KNX_FRAGMENT})
#   target_include_directories(<target> PRIVATE ${H8I8O_CONFIG_INCLUDE_DIR})
#
# A variant is one file, variants/<name>.defconfig (or a path), with KNstaX
# options (CONFIG_KNX_*) and H8i8o options (CONFIG_H8I8O_*). This splits it by
# prefix: the KNstaX half becomes a fragment for knx_configure(), the H8i8o
# half is resolved against the H8i8o Kconfig with KNstaX's own generator
# (tools/kconfig/knx_genconfig.py) into h8i8o_config.h. Both halves are
# checked by Kconfig: an unknown symbol fails the configure step.
#
# Results in the caller's scope: H8I8O_KNX_FRAGMENT, H8I8O_CONFIG_INCLUDE_DIR,
# H8I8O_VARIANT_FILE, and every CONFIG_H8I8O_* as a CMake variable.

set(_H8I8O_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(_H8I8O_ROOT "${_H8I8O_ROOT}" ABSOLUTE)

function(h8i8o_variant variant)
    if(EXISTS "${variant}" AND NOT IS_DIRECTORY "${variant}")
        get_filename_component(_file "${variant}" ABSOLUTE)
    else()
        set(_file "${_H8I8O_ROOT}/variants/${variant}.defconfig")
    endif()
    if(NOT EXISTS "${_file}")
        file(GLOB _known "${_H8I8O_ROOT}/variants/*.defconfig")
        set(_names)
        foreach(_f IN LISTS _known)
            get_filename_component(_n "${_f}" NAME_WE)
            list(APPEND _names "${_n}")
        endforeach()
        message(FATAL_ERROR "h8i8o_variant: no variant '${variant}' (known: ${_names})")
    endif()

    set(_out "${CMAKE_BINARY_DIR}/h8i8o-config")
    file(STRINGS "${_file}" _lines)
    set(_knx "# From ${_file}. Generated, do not edit.\n")
    set(_app "# From ${_file}. Generated, do not edit.\n")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^(# )?CONFIG_KNX_")
            string(APPEND _knx "${_line}\n")
        elseif(_line MATCHES "^(# )?CONFIG_H8I8O_")
            string(APPEND _app "${_line}\n")
        elseif(_line MATCHES "^(# )?CONFIG_")
            message(FATAL_ERROR "h8i8o_variant: ${_file}: '${_line}' is neither a KNstaX (CONFIG_KNX_) "
                                "nor an H8i8o (CONFIG_H8I8O_) option")
        endif()
    endforeach()
    file(WRITE "${_out}/knx.fragment" "${_knx}")
    file(WRITE "${_out}/h8i8o.fragment" "${_app}")

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    find_program(H8I8O_PYTHON NAMES python3 python REQUIRED)
    execute_process(
        COMMAND "${H8I8O_PYTHON}" -I "${KNSTAX_DIR}/tools/kconfig/knx_genconfig.py"
                --kconfig "${_H8I8O_ROOT}/Kconfig" --srctree "${_H8I8O_ROOT}"
                --fragment "${_out}/h8i8o.fragment" --out-dir "${_out}/gen"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "h8i8o_variant: ${_file}:\n${_stderr}${_stdout}")
    endif()
    # The generator writes knx/autoconf.h; KNstaX has a header of that name
    # too, so the H8i8o one is renamed rather than put on the include path.
    configure_file("${_out}/gen/knx/autoconf.h" "${_out}/include/h8i8o_config.h" COPYONLY)
    include("${_out}/gen/knx_config.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}" "${_H8I8O_ROOT}/Kconfig")

    get_filename_component(_name "${_file}" NAME_WE)
    message(STATUS "H8i8o variant: ${_name} (${CONFIG_H8I8O_PRODUCT_NAME}, application ${CONFIG_H8I8O_APPLICATION_NUMBER})")

    set(H8I8O_KNX_FRAGMENT "${_out}/knx.fragment" PARENT_SCOPE)
    set(H8I8O_CONFIG_INCLUDE_DIR "${_out}/include" PARENT_SCOPE)
    set(H8I8O_VARIANT_FILE "${_file}" PARENT_SCOPE)
    set(H8I8O_VARIANT_NAME "${_name}" PARENT_SCOPE)
    get_cmake_property(_vars VARIABLES)
    foreach(_v IN LISTS _vars)
        if(_v MATCHES "^CONFIG_H8I8O_")
            set(${_v} "${${_v}}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

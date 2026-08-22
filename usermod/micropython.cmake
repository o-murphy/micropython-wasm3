# usermod/micropython.cmake
# USER_C_MODULES integration for MicroPython ports that use CMake.
#
# RP2040 / Pico SDK:
#   cmake -B build -DUSER_C_MODULES=/abs/path/to/usermod/micropython.cmake
#
# ESP32 (IDF CMake):
#   idf.py build -DUSER_C_MODULES=/abs/path/to/usermod/micropython.cmake
#
# Unix port (make):
#   make -C ports/unix VARIANT=standard \
#       USER_C_MODULES=/abs/path/to/usermod/micropython.cmake \
#       FROZEN_MANIFEST=/abs/path/to/usermod/manifest.py

# NOTE: wasm3 allocates through the port's own libc (calloc/free/realloc), not
# the MicroPython GC heap — routing it at the GC heap would need the slot table
# registered with MP_REGISTER_ROOT_POINTER, since a usermod's globals live in
# firmware .bss that gc_collect() does not scan. So the port must actually have
# a C heap. rp2 defaults MICROPY_C_HEAP_SIZE to 0, which makes every wasm3
# allocation fail and faults the CPU inside wasm3.Module(); configure with
# -DMICROPY_C_HEAP_SIZE=131072 (or more, sized to the modules you load).
cmake_minimum_required(VERSION 3.13)

get_filename_component(_USERMOD_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_MOD_DIR     "${_USERMOD_DIR}/.."         ABSOLUTE)

# ── Version header ────────────────────────────────────────────────────────────
set(_VERSION_H  "${_USERMOD_DIR}/generated/wasm3_mp/version.h")
set(_VERSION_IN "${_MOD_DIR}/version.h.in")

if(NOT EXISTS "${_VERSION_H}")
    file(MAKE_DIRECTORY "${_USERMOD_DIR}/generated/wasm3_mp")
    execute_process(
        COMMAND git describe --tags --always
        WORKING_DIRECTORY "${_MOD_DIR}"
        OUTPUT_VARIABLE _GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _GIT_TAG)
        set(_GIT_TAG "v0.0.0")
    endif()
    string(REGEX REPLACE "^v?([0-9]+).*"                   "\\1" _MAJ "${_GIT_TAG}")
    string(REGEX REPLACE "^v?[0-9]+\\.([0-9]+).*"          "\\1" _MIN "${_GIT_TAG}")
    string(REGEX REPLACE "^v?[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" _PAT "${_GIT_TAG}")
    foreach(_V _MAJ _MIN _PAT)
        if(NOT "${${_V}}" MATCHES "^[0-9]+$")
            set(${_V} 0)
        endif()
    endforeach()
    set(MP_WASM3_VERSION_MAJOR ${_MAJ})
    set(MP_WASM3_VERSION_MINOR ${_MIN})
    set(MP_WASM3_VERSION_PATCH ${_PAT})
    set(MP_WASM3_VERSION "${_MAJ}.${_MIN}.${_PAT}")
    configure_file("${_VERSION_IN}" "${_VERSION_H}" @ONLY)
endif()

# ── Module library ────────────────────────────────────────────────────────────
add_library(usermod_wasm3 INTERFACE)

target_sources(usermod_wasm3 INTERFACE
    "${_MOD_DIR}/src/wasm3_mp.c"
    "${_MOD_DIR}/wasm3/source/m3_bind.c"
    "${_MOD_DIR}/wasm3/source/m3_code.c"
    "${_MOD_DIR}/wasm3/source/m3_compile.c"
    "${_MOD_DIR}/wasm3/source/m3_core.c"
    "${_MOD_DIR}/wasm3/source/m3_env.c"
    "${_MOD_DIR}/wasm3/source/m3_exec.c"
    "${_MOD_DIR}/wasm3/source/m3_function.c"
    "${_MOD_DIR}/wasm3/source/m3_info.c"
    "${_MOD_DIR}/wasm3/source/m3_module.c"
    "${_MOD_DIR}/wasm3/source/m3_parse.c"
    "${_MOD_DIR}/wasm3/source/m3_validate.c"
)

target_include_directories(usermod_wasm3 INTERFACE
    "${_MOD_DIR}/wasm3/source"
    "${_MOD_DIR}/src"
    "${_USERMOD_DIR}"
)

# Same shared knob set as the natmod build — see src/wasm3_mp_config.h.
target_compile_options(usermod_wasm3 INTERFACE
    "-include${_MOD_DIR}/src/wasm3_mp_config.h"
    -Wno-sign-compare
    -Wno-float-conversion
    -fno-fast-math
)

target_link_libraries(usermod INTERFACE usermod_wasm3)

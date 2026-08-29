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
# allocation fail and faults the CPU inside wasm3.Module(). This file now
# defaults it to 131072 itself (below) rather than requiring every caller to
# pass -DMICROPY_C_HEAP_SIZE= by hand -- an external -D still wins when given
# (CMake CACHE variables from the command line resolve before this file's own
# `include()` runs), so a caller that wants a different size still can.
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

# ── The port must have a C heap ───────────────────────────────────────────────
# wasm3 allocates through the port's own calloc/free/realloc, not the
# MicroPython GC heap (routing it there would need the slot table registered
# with MP_REGISTER_ROOT_POINTER, since a usermod's globals live in firmware
# .bss that gc_collect() does not scan). rp2 defines MICROPY_C_HEAP_SIZE and
# defaults it to 0, which makes every wasm3 allocation fail and faults the CPU
# inside wasm3.Module() with no diagnostic at all.
#
# This used to be a FATAL_ERROR pointing the caller at a -D flag to pass by
# hand -- moved from cibuildmp's own generic per-port `extra-make-args`
# passthrough. Investigated live: ports/rp2/CMakeLists.txt checks
# MICROPY_C_HEAP_SIZE (defaulting it to 0) *before* py/usermod.cmake includes
# this file, but py/usermod.cmake's own `include(${USER_C_MODULE_PATH})` is a
# same-scope include (not add_subdirectory), and the linker flag that actually
# consumes the value is emitted later still -- so a plain `set()` here runs
# after the 0 default and before the value is read, and does take effect
# (verified with a standalone CMakeLists.txt reproducing that exact
# before-default / include / after-read order). 131072 is what every build of
# this module -- CI included -- has always used; sized to the modules you
# intend to load if that ever needs to grow.
#
# Guarded on DEFINED: only rp2 has this variable. Ports with a real malloc
# (esp32's IDF heap, for one) never define it and must not get one here.
if(DEFINED MICROPY_C_HEAP_SIZE AND MICROPY_C_HEAP_SIZE STREQUAL "0")
    set(MICROPY_C_HEAP_SIZE 131072)
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
#
# -Wno-error=maybe-uninitialized is for ESP-IDF, which compiles this port with
# -Werror and a warning set gcc's interprocedural analysis does not survive on
# wasm3's own sources:
#
#   In function 'v_validate_body',
#       inlined from 'ValidateFunction' at wasm3/source/m3_validate.c:1165:9:
#   m3_validate.c:628:17: error: 't2' may be used uninitialized
#                                 [-Werror=maybe-uninitialized]
#     628 |             r = v_pop_expect(v, t2, &t1);
#   m3_validate.c:624:16: note: 't2' was declared here
#
# t2 is written by `v_pop(v, &t2)` two lines earlier and the code returns
# immediately if that fails, so the warning is a false positive gcc only
# reaches after inlining the whole validator body. It is upstream wasm3 code in
# a submodule, not this repo's, and not something to patch downstream —
# -Wno-error rather than -Wno-, so it still prints. Same reasoning as the two
# -Wno- flags below, which are there for the same class of upstream-vs-strict-
# warning-set collision.
target_compile_options(usermod_wasm3 INTERFACE
    "-include${_MOD_DIR}/src/wasm3_mp_config.h"
    -Wno-sign-compare
    -Wno-float-conversion
    -Wno-error=maybe-uninitialized
    -fno-fast-math
)

target_link_libraries(usermod INTERFACE usermod_wasm3)

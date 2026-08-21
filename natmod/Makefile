# ── Arch selection ─────────────────────────────────────────────────────────────
#
#   make                       → x64 (host / dev)
#   make ARCH=x86              → x86 32-bit
#   make ARCH=armv6m           → RP2040 / Cortex-M0+
#   make ARCH=armv7m           → Cortex-M3
#   make ARCH=armv7emsp        → Cortex-M4F / M7 single-precision FPU
#   make ARCH=armv7emdp        → Cortex-M7 double-precision FPU
#   make ARCH=xtensawin        → ESP32 / ESP32-S3 (Xtensa windowed)
#   make ARCH=xtensa           → ESP8266 (Xtensa no-window)
#   make ARCH=rv32imc          → ESP32-C3 / ESP32-C6 (RISC-V 32)
#   make ARCH=rv64imc          → RISC-V 64
#
# No aarch64: dynruntime.mk (as of MicroPython v1.28) has no ARCH=aarch64
# branch, so natmod cannot target it at all. Use ../usermod there.
#
# ── Size warning ──────────────────────────────────────────────────────────────
# A natmod's text, rodata and bss are all loaded into the MicroPython GC heap
# at import time -- unlike a usermod, none of it executes from flash. The
# wasm3 interpreter is ~14k lines of C, and its two opcode tables
# (c_operations / c_operationsFC in wasm3/source/m3_compile.c) are arrays of
# function pointers, so they must be relocated into RAM rather than staying
# in flash. Check the size line mpy_ld prints before assuming a target fits;
# on small-RAM parts (armv6m in particular) prefer ../usermod.
#
ARCH    ?= x64
MPY_DIR ?= ../micropython

MOD         = wasm3_$(ARCH)
WASM3_ROOT  = ../wasm3
SRC_DIR     = ../src
BUILD       = build/$(ARCH)
VERSION_H   = generated/wasm3_mp/version.h
VERSION_IN  = ../version.h.in

# Find our own sources by filename only, so dynruntime.mk computes
# $(BUILD)/wasm3_mp.o (arch-specific) rather than a path shared between
# arches -- x64 and x86 objects must not collide.
vpath %.c $(SRC_DIR)
vpath %.py $(SRC_DIR)

# ── wasm3 core ────────────────────────────────────────────────────────────────
# The full interpreter minus the host-API layers: m3_api_wasi.c,
# m3_api_uvwasi.c, m3_api_meta_wasi.c and m3_api_tracer.c all want a real
# libc and a filesystem, and m3_api_libc.c only provides printf-style imports
# we do not want to hardwire -- host imports come from Python via wasm3.link().
# m3_info.c stays in: without DEBUG it compiles to a couple of empty stubs
# that m3_env.c still references.
WASM3_SRC = \
    $(WASM3_ROOT)/source/m3_bind.c     \
    $(WASM3_ROOT)/source/m3_code.c     \
    $(WASM3_ROOT)/source/m3_compile.c  \
    $(WASM3_ROOT)/source/m3_core.c     \
    $(WASM3_ROOT)/source/m3_env.c      \
    $(WASM3_ROOT)/source/m3_exec.c     \
    $(WASM3_ROOT)/source/m3_function.c \
    $(WASM3_ROOT)/source/m3_info.c     \
    $(WASM3_ROOT)/source/m3_module.c   \
    $(WASM3_ROOT)/source/m3_parse.c    \
    $(WASM3_ROOT)/source/m3_validate.c

# ── Math ──────────────────────────────────────────────────────────────────────
# math_shim.c covers every libm symbol wasm3's float opcodes need except the
# two that must be correctly rounded to satisfy the wasm spec. Those come
# from MicroPython's own libm rather than the toolchain's: on several targets
# a natmod cannot link the toolchain's libm.a at all (the Xtensa newlib
# libm.a pulls in an object with a non-empty .data section, which mpy_ld
# rejects outright).
_SQRT_SRCS  := $(MPY_DIR)/lib/libm/ef_sqrt.c $(MPY_DIR)/lib/libm_dbl/sqrt.c
_SQRT_FLAGS := -I$(MPY_DIR)/lib -I$(MPY_DIR)/lib/libm_dbl \
               -include stdint.h     \
               -D__int32_t=int32_t   \
               -D__uint32_t=uint32_t \
               -D__uint8_t=uint8_t

# Host builds see glibc's <math.h>, whose hidden __sin/__cos/__tan aliases
# collide with the musl kernel prototypes in lib/libm_dbl/libm.h. The shadow
# header intercepts <math.h> and renames them out of the way; cross
# toolchains have no such aliases and do not need it.
ifeq ($(ARCH),$(filter $(ARCH),x64 x86))
_SQRT_FLAGS += -I$(SRC_DIR)/math_shadow
endif

SRC = wasm3_mp.c libc_shim.c math_shim.c wasm3.py $(WASM3_SRC) $(_SQRT_SRCS)

# ── x86: force SSE2 ───────────────────────────────────────────────────────────
# Two reasons, and the second one is not optional for a wasm runtime.
#
# 1. With the default x87 math, GCC reports __FLT_EVAL_METHOD__ == 2, so
#    glibc's <math.h> typedefs float_t/double_t as long double. MicroPython's
#    lib/libm_dbl/libm.h assumes method 0 and typedefs them float/double,
#    and the two collide with a conflicting-types error.
# 2. x87 evaluates single- and double-precision arithmetic in 80-bit extended
#    precision and rounds on store. wasm requires IEEE-754 binary32/binary64
#    semantics for every f32/f64 op, so excess precision is a conformance bug,
#    not just a rounding curiosity.
ifeq ($(ARCH),x86)
CFLAGS_EXTRA += -msse2 -mfpmath=sse
endif

# No CROSS override here on purpose. dynruntime.mk assigns CROSS with a plain
# `=` (py/dynruntime.mk:94,101), so anything set before the include below is
# overwritten and anything set after it would have to be repeated for every
# arch. Its choices are the canonical ones and CI installs to match:
#   xtensa    -> xtensa-lx106-elf-   (ESP8266)
#   xtensawin -> xtensa-esp32-elf-   (ESP32, from esp-idf)
#   armv*     -> arm-none-eabi-
#   rv*imc    -> riscv64-unknown-elf-

# ── Common flags ──────────────────────────────────────────────────────────────
# -include pulls the shared wasm3 knob set into every translation unit,
# including wasm3's own -- see ../src/wasm3_mp_config.h for what each
# override is for and why.
CFLAGS_EXTRA += \
    -I$(WASM3_ROOT)/source \
    -I$(SRC_DIR) \
    -Igenerated \
    -include $(SRC_DIR)/wasm3_mp_config.h \
    -DWASM3_BUILD_NATMOD \
    $(_SQRT_FLAGS)

# wasm3 is written against C99 with some sign-compare sloppiness that the
# upstream CMake build also silences (wasm3/source/CMakeLists.txt).
CFLAGS_EXTRA += -std=gnu99 -Wno-sign-compare -Wno-unused-function -Wno-unused-variable

# -Wmaybe-uninitialized is a false positive here, and only on the older
# xtensa-lx106 GCC (ESP8266): Compile_Ref_Null passes `type` to PushConst
# after ReadRefType, and the `_` macro jumps to _catch on any error, so the
# use is unreachable unless ReadRefType returned m3Err_none — in which case
# both of its branches have assigned *o_type (m3_compile.c:2235, and the
# ReadRefType body above it). Newer GCCs prove that; this one does not, and
# dynruntime.mk compiles with -Werror.
CFLAGS_EXTRA += -Wno-maybe-uninitialized

# math_shim.c's rint()/rintf() rely on the default FP rounding mode and on
# (x + 2^52) - 2^52 not being algebraically cancelled. Never build this with
# -ffast-math; the guard is here so an -Ofast in the environment can't reach it.
CFLAGS_EXTRA += -fno-fast-math

# Guard the include so 'make clean' works without MPY_DIR.
ifeq ($(wildcard $(MPY_DIR)/py/dynruntime.mk),)
ifeq ($(filter clean,$(MAKECMDGOALS)),clean)
# clean only — dynruntime.mk not needed
else
$(error MPY_DIR="$(MPY_DIR)" — set MPY_DIR=/path/to/micropython)
endif
else
include $(MPY_DIR)/py/dynruntime.mk
endif

# ── libgcc ────────────────────────────────────────────────────────────────────
# Must come after the include so CROSS and CFLAGS_ARCH are set.
# m3_compile.c lowers i64.popcnt to __builtin_popcountll, which becomes a
# libgcc call (__popcountdi2) on every target that has no popcount
# instruction -- x86-64 without -mpopcnt included, so this is not just an
# MCU concern. Cross targets additionally need libgcc's soft-float builtins.
MPY_LD_FLAGS += -l$(realpath $(shell $(CROSS)gcc $(CFLAGS_ARCH) --print-libgcc-file-name))

# ── Header dependency tracking ────────────────────────────────────────────────
# dynruntime.mk compiles without -MMD, so objects only depend on their .c.
# Force a full rebuild whenever a wasm3 header or our config header changes.
_WASM3_HEADERS := $(wildcard $(WASM3_ROOT)/source/*.h) $(SRC_DIR)/wasm3_mp_config.h
$(SRC_O): $(_WASM3_HEADERS)

# ── Version header ────────────────────────────────────────────────────────────
$(VERSION_H): $(VERSION_IN)
	$(ECHO) "  GEN   $@"
	$(Q)mkdir -p $(dir $@)
	$(eval _TAG := $(shell git describe --tags --always 2>/dev/null || echo v0.0.0))
	$(eval _MAJ := $(shell printf '%s' '$(_TAG)' | sed -E 's/^v?([0-9]+)\..*/\1/;t;s/.*/0/'))
	$(eval _MIN := $(shell printf '%s' '$(_TAG)' | sed -E 's/^v?[0-9]+\.([0-9]+)\..*/\1/;t;s/.*/0/'))
	$(eval _PAT := $(shell printf '%s' '$(_TAG)' | sed -E 's/^v?[0-9]+\.[0-9]+\.([0-9]+).*/\1/;t;s/.*/0/'))
	$(eval _VER := $(shell printf '%s' '$(_TAG)' | sed 's/^v//'))
	$(Q)sed -e 's/@MP_WASM3_VERSION_MAJOR@/$(_MAJ)/g' \
	        -e 's/@MP_WASM3_VERSION_MINOR@/$(_MIN)/g' \
	        -e 's/@MP_WASM3_VERSION_PATCH@/$(_PAT)/g' \
	        -e 's/@MP_WASM3_VERSION@/$(_VER)/g' \
	        $< > $@

$(BUILD)/wasm3_mp.o: $(VERSION_H)

.PHONY: submodules dist clean

submodules:
	git submodule update --init --recursive

dist: all
	$(Q)mv $(MOD).mpy $(BUILD)/wasm3.mpy
	$(Q)rm -f $(BUILD)/$(MOD).mpy $(SRC_O) $(CONFIG_H)
	$(ECHO) "  →  $(BUILD)/wasm3.mpy"

clean:
	rm -rf build generated

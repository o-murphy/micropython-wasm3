# usermod/micropython.mk
# Included by MicroPython's py.mk when USER_C_MODULES=<dir> points at this
# file's parent directory (the repo root). USERMOD_DIR is set by py.mk to the
# directory containing this file (= usermod/).
#
#   make -C ports/unix USER_C_MODULES=/path/to/micropython-wasm3 \
#        FROZEN_MANIFEST=/path/to/micropython-wasm3/usermod/manifest.py
#
# There is no wrapper Makefile of our own here on purpose: build against the
# port's own Makefile, the same way ../natmod is the whole story for .mpy
# builds.
#
# Why you may want this over ../natmod: a usermod's code is linked into the
# firmware and executes from flash. A natmod's text is copied into the
# MicroPython GC heap at import time — and wasm3 is not small (~84 KiB of
# text for x86-64). On a part where RAM is the binding constraint, that
# difference decides it.

# ── Version header ────────────────────────────────────────────────────────────
# wasm3_mp.c includes "generated/wasm3_mp/version.h" — generate it once if
# missing. Plain $(shell) at parse time guarded by $(wildcard), rather than a
# real rule: this file is *included* into another port's Makefile, and a rule
# would have to predict that port's own object-path scheme to wire up the
# dependency. Cost is a stale version string until `generated/` is removed by
# hand, which is an acceptable trade for a version string.
ifeq ($(wildcard $(USERMOD_DIR)/generated/wasm3_mp/version.h),)
$(shell mkdir -p $(USERMOD_DIR)/generated/wasm3_mp)
_WASM3_GIT_TAG := $(shell git -C $(USERMOD_DIR)/.. describe --tags --always 2>/dev/null || echo v0.0.0)
_WASM3_MAJ := $(shell printf '%s' '$(_WASM3_GIT_TAG)' | sed -E 's/^v?([0-9]+)\..*/\1/;t;s/.*/0/')
_WASM3_MIN := $(shell printf '%s' '$(_WASM3_GIT_TAG)' | sed -E 's/^v?[0-9]+\.([0-9]+)\..*/\1/;t;s/.*/0/')
_WASM3_PAT := $(shell printf '%s' '$(_WASM3_GIT_TAG)' | sed -E 's/^v?[0-9]+\.[0-9]+\.([0-9]+).*/\1/;t;s/.*/0/')
_WASM3_VER := $(shell printf '%s' '$(_WASM3_GIT_TAG)' | sed 's/^v//')
$(shell sed -e 's/@MP_WASM3_VERSION_MAJOR@/$(_WASM3_MAJ)/g' \
            -e 's/@MP_WASM3_VERSION_MINOR@/$(_WASM3_MIN)/g' \
            -e 's/@MP_WASM3_VERSION_PATCH@/$(_WASM3_PAT)/g' \
            -e 's/@MP_WASM3_VERSION@/$(_WASM3_VER)/g' \
            $(USERMOD_DIR)/../version.h.in > $(USERMOD_DIR)/generated/wasm3_mp/version.h)
endif

# Ports build with a stricter warning set than wasm3 expects. -Wsign-compare
# is silenced by wasm3's own CMake build too; -Wfloat-conversion fires on the
# interpreter's register model, where the f64 register legitimately holds f32
# values (m3_exec.h:1705).

# ── Sources ───────────────────────────────────────────────────────────────────
# No libc_shim.c / math_shim.c here: a usermod links the port's own libc and
# libm. Only the natmod build, which links against neither, needs those.
_WASM3_SRC_DIR := $(USERMOD_DIR)/../src
_WASM3_ROOT    := $(USERMOD_DIR)/../wasm3

SRC_USERMOD_C += \
    $(_WASM3_SRC_DIR)/wasm3_mp.c        \
    $(_WASM3_ROOT)/source/m3_bind.c     \
    $(_WASM3_ROOT)/source/m3_code.c     \
    $(_WASM3_ROOT)/source/m3_compile.c  \
    $(_WASM3_ROOT)/source/m3_core.c     \
    $(_WASM3_ROOT)/source/m3_env.c      \
    $(_WASM3_ROOT)/source/m3_exec.c     \
    $(_WASM3_ROOT)/source/m3_function.c \
    $(_WASM3_ROOT)/source/m3_info.c     \
    $(_WASM3_ROOT)/source/m3_module.c   \
    $(_WASM3_ROOT)/source/m3_parse.c    \
    $(_WASM3_ROOT)/source/m3_validate.c

CFLAGS_USERMOD += \
    -I$(_WASM3_ROOT)/source \
    -I$(_WASM3_SRC_DIR) \
    -I$(USERMOD_DIR) \
    -include $(_WASM3_SRC_DIR)/wasm3_mp_config.h \
    -std=gnu99 -Wno-sign-compare -Wno-float-conversion

# math_shim.c is not built here, but wasm3's own f32/f64 nearest ops still go
# through the port's rint()/rintf(); nothing in this tree relies on
# -ffast-math being off for a usermod. Left explicit anyway, since a port that
# turns it on globally would break wasm's rounding semantics.
CFLAGS_USERMOD += -fno-fast-math

# ── Warning flags that must beat the port's own ───────────────────────────────
# py.mk folds CFLAGS_USERMOD into CFLAGS *before* a port appends its CWARN
# set, so a -Wno- there loses to the port's -Werror (ports/unix/Makefile:55
# turns on -Wfloat-conversion). A pattern-specific variable is evaluated per
# object and wins regardless of ordering.
#
# The path shape comes from py.mk's PATHFIX: a source given as
# $(USERMOD_DIR)/../wasm3/source/x.c builds to
# $(BUILD)/<usermod-dirname>/../wasm3/source/x.o.
#
# -Wfloat-conversion fires on the interpreter's register model, where the f64
# register legitimately holds f32 values (m3_exec.h:1705, m3_env.c:1033).
$(BUILD)/$(notdir $(USERMOD_DIR))/../wasm3/source/%.o: CFLAGS += \
    -Wno-float-conversion -Wno-sign-compare -Wno-double-promotion

# On a usermod there is no PIC/GOT indirection in the way, so the interpreter
# can use guaranteed tail calls if the toolchain offers them. Uncomment to
# trade a compile-time risk (musttail is a hard error where the ABI forbids a
# sibling call) for a faster dispatch loop and iterative return_call.
#CFLAGS_USERMOD += -DM3_HAS_TAIL_CALL=1

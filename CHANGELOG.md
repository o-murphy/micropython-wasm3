# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

Initial project scaffold: Wasm3 as a MicroPython module, in the same layout
as `micropython-bclibc` (shared `src/`, a `natmod/` build, a `usermod/`
build, `tests/`, `tools/`, `benchmarks/`).

- `wasm3/` submodule pinned to [o-murphy/wasm3](https://github.com/o-murphy/wasm3).
- `src/wasm3_mp.c` — the glue: `load`/`unload`/`find`/`call`/`link`/`memory`/
  `mem_size`, one shared `M3Environment` and a fixed table of runtime slots.
  Host imports are routed to Python callables through a raw-call trampoline.
- `src/wasm3.py` — `Module` / `Function` wrapper with context-manager
  lifetime and attribute sugar for exports.
- `src/wasm3_mp_config.h` — the wasm3 knobs both build modes share, each
  override annotated with why the stock value is wrong for MicroPython.
- `src/libc_shim.c`, `src/math_shim.c` — the libc/libm surface a natmod has
  to bring itself. The surface was measured rather than guessed: with
  `DEBUG` off and `d_m3VerboseErrorMessages=0`, wasm3's core needs only
  `calloc/free/realloc`, `memcpy/memset/memmove/memcmp`, `strlen/strcmp`,
  `abort`, and seven libm pairs.
- `tools/make_test_wasm.py` — hand-assembles the three `.wasm` fixtures, so
  the test suite needs no WABT.
- `natmod/examples/run_wiring_app.py` — runs the `wiring`-style demo blobs
  from wasm3/embedded-wasm-apps by supplying that host interface in Python.
- `natmod/ci/run_rp2040py.py` — flashes the `.mpy` and every blob into a
  littlefs image and runs the core suite on an emulated RP2040 (rp2040py
  0.3.1), against a firmware built from the pinned MicroPython rather than
  the emulator's own default download.
- `natmod/ci/run_qemu.py` — raw-REPL bridge that runs both suites on the
  MicroPython QEMU port: the `.mpy` is served from a RAM-backed VFS and every
  blob is injected into the target's globals, since that port has no
  filesystem.
- `wasm2mpy/` submodule (208 KB) purely for its `test/*.wasm` blobs, and
  `tests/test_wiring_apps.py` — 29 checks over real toolchain output from
  seven languages, asserting each app's exact stdout, its elapsed-time
  accounting and its pin toggles, plus CoreMark behind `--slow`. Also
  cross-checks that `tools/make_test_wasm.py` reproduces `simple.wasm`
  byte-for-byte.

### Fixed

Found by loading a real-world module — a C++ build of `bclibc` compiled to
wasm by Emscripten at `-O3`, 249 KB with 51 imports — which is far outside
what the hand-written fixtures exercise:

- `src/libc_shim.c`'s `calloc`/`realloc` went through dynruntime's
  `m_malloc`/`m_realloc`, which call `m_malloc_fail()` on exhaustion and
  raise `MemoryError` through nlr (`py/dynruntime.h:83-93`) — longjmping out
  of whichever wasm3 frame was allocating, past its own cleanup. wasm3 checks
  every allocation for NULL and reports `m3Err_mallocFailed`, so both now use
  `m_realloc_maybe`. `calloc` also gained the `num * size` overflow check it
  never had.
- `WASM3_MP_MAX_ARGS` was 8. Emscripten output routinely exceeds that (one of
  bclibc's exception trampolines takes 23 parameters), so it is 16 now, sized
  against a Cortex-M0+'s 8 KB stack rather than taste.
- `d_m3MaxLinearMemoryPages` stays at 16 for devices but is raised to 1024 for
  x64/x86 in `natmod/Makefile`: a module compiled for the web declares
  whatever its toolchain chose, and the bclibc build wants 258 pages (16.1 MB)
  before it runs at all.
- `usermod.yml` — the uploaded wasm build was missing `asyncio` and 24 stdlib
  modules. `usermod-wasm` passed `FROZEN_MANIFEST=usermod/manifest.py` alone.
  `usermod/manifest.py`'s own `try`/`except` around
  `include("$(PORT_DIR)/boards/manifest.py")` only ever probes that one path,
  which doesn't exist for `ports/webassembly` (it has `variants/`, not
  `boards/`) — so the `except` silently swallowed it, and the port's real
  default, `variants/pyscript/manifest.py`, never got included. That default
  provides `asyncio` (backed by a custom JS-runtime scheduler) plus a
  `require()` list of 24 stdlib/utility modules (`base64`, `collections`,
  `gzip`, `os`, `pathlib`, `unittest`, `zlib`, and others). Neither
  `test_wasm3.py` nor `test_wiring_apps.py` imports any of them, so the gap
  never showed up as a test failure — but the `.mjs`/`.wasm` this job
  uploads is a real build artifact, not just a test fixture, and anyone
  importing `asyncio`/`os`/etc. against it hit a plain `ImportError`. The
  job now writes a combined manifest (`variants/pyscript/manifest.py` +
  this project's own `usermod/manifest.py`) and passes that instead, the
  same pattern `o-murphy/a7p`'s own webassembly job already uses for this
  exact port.

### Added

- CI builds and tests the usermod on four targets, not one: `unix-x64`,
  `unix-x86`, `unix-aarch64` and `rp2-RPI_PICO`. Structured as a single
  matrix job with `runs-on: ${{ matrix.runs_on }}`, following a7p's
  `mp-usermod.yml` — aarch64 builds *natively* on `ubuntu-24.04-arm` rather
  than through a cross-toolchain and qemu-user.
- `natmod/ci/run_rp2040py.py --no-mpy`, for a usermod firmware, where the
  module is built in and a `.mpy` on the filesystem would shadow the frozen
  `wasm3.py`.

`aarch64` is the row that earns the job: `dynruntime.mk` has no aarch64 ARCH,
so a natmod cannot reach that architecture at all, and usermod is the only
way this module runs on ARM64.

### Added

CI coverage grew from four usermod targets to eleven, and from four executed
natmod ARCHes to six.

- `usermod.yml` is its own workflow, split out of `natmod.yml`: the two
  halves share no artifacts, have different trigger paths and fail in
  different ways (a usermod links against the port's own libc, a natmod
  against `src/libc_shim.c`), so one red X standing for both was hiding
  which.
- `usermod` on **Windows** `x64`/`x86`/`arm64`, built *and run* natively via
  MSYS2 (MINGW64/MINGW32/CLANGARM64). natmod is not merely absent there but
  impossible: `ports/windows/mpconfigport.h` sets `MICROPY_EMIT_X64 (0)` and
  `py/persistentcode.c` gates `.mpy` native-code loading on
  `MICROPY_EMIT_MACHINE_CODE`, so no `wasm3.mpy` can load whatever its arch.
  The `arm64` row carries `-Wno-error` for MicroPython's *own* sources, which
  do not survive `ports/windows`' gcc-tuned `-Werror` under clang; the
  x64/x86 rows keep it, so nothing of this module's goes unchecked.
- `usermod` on **webassembly**, under node — needed no module changes at all.
  A genuinely distinct portability datapoint: 32-bit target, a libc that is
  neither glibc nor newlib, and no filesystem whatsoever. `natmod/ci/run_wasm.py`
  and a `_ARGV` hook in `tests/test_wiring_apps.py` carry the blobs and the
  `--slow` flag in, since the port gives the interpreter neither files nor argv.
- `usermod` on **`unix` `armhf`/`mipsel`**, statically linked. `mipsel` runs
  under `qemu-user`; `armhf` runs on real hardware (below).
- `usermod` on **esp32** (`BOARD=ESP32_GENERIC`, ESP-IDF v5.5.1),
  **build-only** — there is no esp32 emulator to hand a firmware image to the
  way rp2040py takes a `.uf2`. README used to call this target unbuildable;
  that was a claim about this project's development environment, where
  `dl.espressif.com` is refused by egress policy, not about the target.
- `natmod` **executes** `armv7emsp` and `armv7emdp` now, not just builds
  them, on a statically linked 32-bit armhf `ports/unix` host on
  `ubuntu-24.04-arm` — real ARM silicon, no emulator anywhere.
  `py/persistentcode.h`'s `MPY_FEATURE_ARCH_TEST` is a range
  (`ARMV6M <= x <= ARMV7EMDP`), not an equality, which is what lets a
  Cortex-M `.mpy` load on such a host.

### Changed

- CI: `actions/checkout`, `actions/upload-artifact` and
  `actions/download-artifact` bumped from `v4` to `v7`/`v7`/`v8` across
  `natmod.yml` and `usermod.yml`, matching the versions
  `ballistics-lab/micropython-bclibc` and `o-murphy/a7p` already pin (and
  already run green in CI) — this repo was the one left behind. Pure
  version bump: every call here only ever used the stable
  `submodules`/`name`/`path`/`if-no-files-found` inputs, none of which
  changed shape across these major versions.
- CI: `usermod.yml`'s unix rows (`x64`/`x86`/`aarch64`), `usermod-cross`
  (`armhf`/`mipsel`), and `usermod-windows` (`x64`/`x86`/`arm64`) no longer
  carry their own apt/cross-compile/deplibs/MSYS2 recipe inline — all three
  now call `build-usermod-unix`/`build-usermod-windows` from
  [`ballistics-lab/micropython-native-ci`](https://github.com/ballistics-lab/micropython-native-ci),
  the same repo `natmod.yml` already used for `build-natmod`. No
  behavior change: `BUILD=build-wasm3`, `PROG=micropython-wasm3(.exe)`, and
  every other build path stay exactly what they were.
- The `usermod` `armhf` row runs on `ubuntu-24.04-arm` instead of under
  `qemu-user`. A GitHub arm64 runner executes 32-bit ARM on its own CPU —
  measured on the runner with a freestanding AArch32 binary, not assumed from
  a datasheet — so the arm64 runner cross-builds and then runs it natively.
  That is also why the row switched from upstream's soft-float `gnueabi` to
  `gnueabihf`: `armel` baselines at ARMv5TE, whose SWP atomics ARMv8 removed
  outright. `mipsel` stays emulated; GitHub has no mips runner.
- `usermod/micropython.cmake` gained `-Wno-error=maybe-uninitialized`.
  ESP-IDF is the only port here that compiles wasm3's own sources with
  `-Werror` and that warning on, and gcc reaches a false positive on
  `m3_validate.c:628` only after inlining the whole validator body. Upstream
  submodule code, so `-Wno-error` rather than a downstream patch — and
  `-Wno-error=` rather than `-Wno-`, so it still prints.
- CI: `usermod-wasm` no longer carries its own inline emsdk-install/
  mpy-cross/port-build recipe — it now calls `build-usermod-webassembly`
  from
  [`ballistics-lab/micropython-native-ci`](https://github.com/ballistics-lab/micropython-native-ci),
  same as the unix/Windows jobs above. The "Write combined FROZEN_MANIFEST"
  step (see Fixed, above) stays caller-side, as the action's own contract
  requires. No behavior change: `VARIANT=pyscript`, `emsdk latest`, and every
  build path stay exactly what they were.
- CI: the `usermod` `rp2` row's toolchain-install/mpy-cross/port-build
  steps are now `build-usermod-rp2040` from the same
  `ballistics-lab/micropython-native-ci` repo. `extra_cmake_args:
  -DMICROPY_C_HEAP_SIZE=131072` replaces the row's own two-step
  configure-then-reconfigure dance — that input was added to the action
  specifically for this case (`ports/rp2/Makefile` builds its `CMAKE_ARGS`
  with `+=`, so this define can't ride the plain `make` command line
  without replacing the whole accumulated set). Only the `pip install
  rp2040py littlefs-python` step stays here; the arm-none-eabi + CMake
  toolchain now lives inside the action.

### Fixed

- `src/wasm3_mp.c`'s `f32`/`f64` returns went through an implicit conversion
  that clang rejects under `-Wdouble-promotion -Werror` (a promotion for
  `f32`, a *narrowing* for `f64` wherever `MICROPY_FLOAT_IMPL_FLOAT` is
  selected). Both now go through one `WASM3_FLOAT` macro defined per build
  mode — identity under natmod, whose `mp_obj_new_float` already bakes in a
  `(double)`, and `(mp_float_t)` under usermod. The natmod branch matters:
  `py/dynruntime.mk` selects `MICROPY_FLOAT_IMPL=none` for `xtensa`,
  `rv32imc` and `rv64imc`, where `mp_float_t` does not exist at all.

### Verified

Against MicroPython v1.28.0 (the version CI pins) and, with identical
results, against current `master` (v1.29.0-preview):

- natmod builds and links for `x64` (text 85736 B, bss 428 B, 63 GOT entries,
  `wasm3.mpy` 87685 B) and for `x86`/i386 (text 103348 B, bss 216 B, 69 GOT
  entries, 105575 B). This settles the open question about
  wasm3-as-natmod: the opcode tables in `m3_compile.c`, arrays of function
  pointers, relocate without trouble, and wasm3 needs no executable memory
  because it interprets rather than JITs.
- usermod builds into `ports/unix`, 64- and 32-bit (~109 KB / ~140 KB of
  added text).
- 20/20 tests pass under all four combinations, covering calls and i32 wrapping, linear memory
  shared by reference in both directions, out-of-bounds traps, host imports
  called back into Python, and slot lifetime.
- `src/math_shim.c` checked bit-exact against glibc over 60M values,
  including signed zeros, subnormals, infinities and NaN.

### Not yet done

- All ten arches build in CI; six are executed (x64, x86, armv7m under QEMU,
  armv6m on rp2040py, and armv7emsp/armv7emdp on a real 32-bit ARM Linux
  host). For the remaining four — `rv32imc`, `rv64imc`, `xtensa`, `xtensawin`
  — "it links" is the entire claim. `armv6m` and `armv7m` cannot join the ARM
  Linux host: `py/dynruntime.mk` builds them soft-float, so their floats
  reach the runtime in core registers while an armhf host reads them from VFP
  registers per AAPCS-VFP, and the `.mpy` loads and then returns wrong values
  rather than failing.
- On the usermod side, `mipsel` is the only remaining emulated target, and
  `esp32` the only one with no execution step at all. `ports/qemu` is still
  out: it links `-nostdlib` with libgcc alone, and this module's shims would
  link and then corrupt, since they allocate on the GC heap while a usermod's
  globals sit in firmware `.bss` that `gc_collect()` does not scan.
- On armv6m only the core suite passes. The blob suite is blocked by native
  stack depth, not by RAM — each blob loads with 40–65 KB of heap to spare
  and then fails on call depth. Relocating the stack into the heap's region
  (as MicroPython's RP2350 script already does) plus a matching
  `d_m3MaxNativeStack` takes it from 17/20 to 25/26, but that needs a patched
  firmware and is recorded as a measurement, not a recommendation. From
  inside a `.mpy` there is no fix: `-O2`, `-foptimize-sibling-calls` and
  `M3_HAS_TAIL_CALL=1` all change nothing, and a heap-allocated interpreter
  stack is unreachable because natmods cannot retarget MicroPython's stack
  extents or GC root scanning. Accepted as a limit of the part. See README.
- No benchmarks, no release packaging.
- `i64` marshalling goes through `mp_int_t` and will not round-trip large
  values on a 32-bit port.
- Globals, WASI, table/reference-type APIs are not exposed.

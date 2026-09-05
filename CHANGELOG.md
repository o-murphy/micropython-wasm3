# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **cibuildmp bumped `v0.6.1` → `v0.7.1`**, all six `ballistics-lab/cibuildmp@`
  uses in `usermod.yml`/`natmod.yml`. Nothing here needed a config change:
  `0.6.2`'s fixes are cross-arch (`riscv64`/`s390x`/`mipsel` unix builds, none
  of which this project's matrix touches) and `0.7.0`'s are either the same
  (pre-`v1.20.0` tags, `mimxrt`) or a real fix for a bug this project's own
  `usermod-armhf` job was exposed to without ever tripping it: `Container`'s
  `linux32` wrap could disagree between its create-time probe and the
  `docker exec` every real build command uses, on an arm64 CI runner — exactly
  what `ubuntu-24.04-arm` is — risking a silently wrong-arch `libffi` link.
  `0.7.1` adds `no-user-c-modules` (a stock-upstream build path this project
  has no use for, since every port here always links `usermod/`) and drops
  the already-unused `CIBMP_SCRATCH_PATH`.
- **README documents `cibuildmp` as the recommended way to build this
  module**, not just the tool CI happens to use. It resolves every target's
  cross-toolchain itself and drives the same `natmod/Makefile`/`usermod/`
  build the manual `make`/`cmake` recipes do — recipes that no longer track
  CI and can drift from what actually ships. New "Build with cibuildmp"
  section, ahead of `Prerequisites`, gives the install/build command and the
  full table of identifiers `cibuildmp.toml`'s `build =` glob matches (every
  target `usermod.yml`/`natmod.yml` build and test today).

- **README's RP2040 tail-call verdict, corrected and measured.** The
  "what was tried" table said `M3_HAS_TAIL_CALL=1` has "no effect: `musttail`
  needs GCC 15, arm-none-eabi is 13". Both halves are stale: the toolchain
  cibuildmp builds this with pins arm-none-eabi **GCC 15.2.1** and
  `__has_attribute(musttail)` is true there. It still fails, for a reason that
  is final rather than temporary — GCC's ARMv6-M backend has no
  `sibcall_epilogue` pattern at all (`cortex-m0plus` fails, `cortex-m33` and
  `cortex-m55` compile). The old wording pointed at "wait for a newer
  toolchain", which is a dead end; the new one points at a core that has the
  pattern.

- **`RPI_PICO2` (RP2350) added to `cibuildmp.toml` and the `usermod` matrix**,
  alongside `RPI_PICO` and not instead of it. It is the one build-only row in
  that matrix, carried on a `build_only: true` matrix flag that gates both the
  emulator install and the test step — `rp2040py` emulates RP2040 only, and no
  runner has an RP2350, so there is nothing to execute the image on. What the
  row buys is a real ARMv8-M compile of this module, a different core and a
  different pico-sdk platform layer from `RPI_PICO`.

- **`ports/qemu` as a usermod: tried, and blocked outright.** README carried
  this as "likely blocked outright (untried)". Now tried, on `MPS3_AN547`
  (Cortex-M55) with arm-none-eabi 15.2.1: it fails at link exactly where the
  README's own C-heap bullet predicts — `undefined reference to 'free'` and
  `'realloc'` from `m3_core.o`, plus `dangerous relocation: unsupported
  relocation`. The core is irrelevant; the missing C heap is the wall. This
  also closes off the idea of using QEMU's ARMv8-M board to test a tail-call
  build without hardware.

- **First RP2350 build results, in README.** `RPI_PICO2` builds (860160 B), and
  builds with `M3_HAS_TAIL_CALL=1` too (861184 B, +1024 B), against `v1.28.0`
  through cibuildmp. Recorded as **build** results, not test results: nothing
  has run the suites on RP2350, because `rp2040py` emulates RP2040 only. The
  README names `MPS3_AN547` — QEMU's Cortex-M55 board, already a cibuildmp
  `qemu` target — as the way to test a tail-call build without hardware.

  Two traps documented alongside, both hit while measuring: `-DCMAKE_C_FLAGS=`
  pre-seeds the cache and voids pico-sdk's own `CMAKE_C_FLAGS_INIT` (the build
  then dies inside the SDK on spin locks — reproduced with a harmless probe
  value, so it is the variable and not the contents), and `ports/rp2/Makefile`
  re-runs `cmake` only when the build directory is absent, so a changed cmake
  argument silently does nothing on an existing one.

### Removed

- **`mipsel` support (usermod, `unix-mipsel`).** Debian 13 "Trixie" dropped the
  mipsel port outright — no `gcc-mipsel-linux-gnu`/`libc6-dev-mipsel-cross` in
  its archive — so cibuildmp's own `manylinux_2_39_mipsel` image (which this
  project's `usermod-mipsel` job below built against) has no upstream
  cross-toolchain left to rebuild from (cibuildmp record 0068). A dead
  platform, not a maintenance choice: `usermod.yml`'s `usermod-mipsel` job,
  `cibuildmp.toml`'s `v1.28.0-manylinux_2_39_mipsel` entry, and every mipsel
  row/mention in `README.md`'s status tables are gone. The `### Added`/
  `### Changed` entries below that describe how that job came to be built
  stay as-is — historical record of a real target this project supported,
  not a claim about what it supports today.

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

### Changed

- **CI is off cibuildmp's legacy composite actions entirely.** That layer never
  invokes the `cibuildmp` CLI — each action installs a toolchain by hand and
  runs the port's own `make` — and is a permanent legacy fallback rather than a
  second supported integration path (cibuildmp record 0073).
  - `usermod.yml`'s `usermod-mipsel` job builds through the CLI like every
    other row now, with `build: v1.28.0-manylinux_2_39_mipsel` (already listed
    in this repo's own `cibuildmp.toml`). What used to justify holding it back
    — record 0043 keeping mipsel on the vendored `MICROPY_STANDALONE=1`/
    `deplibs` static-libffi path — is an argument the other way: cibuildmp's
    own mipsel driver applies that path itself, gated per-arch in that same
    source. Verified on the real artifact of `o-murphy/a7p`'s already-migrated
    equivalent job, not inferred: `ELF 32-bit LSB executable, MIPS, MIPS32 rel2
    ..., statically linked`. `qemu-user-static` is now an explicit apt step
    (the tests still invoke `qemu-mipsel-static` by name rather than relying on
    the binfmt registration), and the job reads cibuildmp's collected
    `mpyhouse/<identifier>/` copy instead of `usermod/build/mipsel/`.
  - `natmod.yml`'s four `fetch-micropython` uses are now the five-line
    release-tarball fetch that action performed. None of those jobs runs a
    cibuildmp step (all four consume the build job's artifact), so there is no
    cibuildmp-resolved checkout to point at, and the tarball rather than a clone
    is still what lets them skip `make submodules`.

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

- CI: every `ballistics-lab/micropython-native-ci` action reference (both
  `natmod.yml` and `usermod.yml`) is now pinned to the `v0.2.0` tag
  instead of a mix of the `v0.1.0` tag (`fetch-micropython`,
  `build-natmod-arch`) and the `claude/usermod-shared-action-kwulzv`
  development branch (everything added this cycle). `build-natmod-arch`
  also drops its `-arch` suffix (`build-natmod`) now that a tag past that
  rename exists. No behavior change: `v0.2.0` is exactly what that branch
  contained, squash-merged.
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
- CI: `usermod-esp32`'s ESP-IDF-install/mpy-cross/port-build steps are now
  `build-usermod-esp32` from the same
  `ballistics-lab/micropython-native-ci` repo. This job's own "Dump the
  IDF build logs on failure" diagnostic (added after a real failure here
  with no compiler diagnostic anywhere in the Actions log) is now folded
  into the action itself, so `ballistics-lab/micropython-bclibc` and
  `o-murphy/a7p` get it too. No `build_dir` input: a real CI failure on
  bclibc's own first run of this action showed that an explicit `BUILD=`
  override, even set to the port's own default value, made esp32's
  internal CMake-driven `mpy-cross` sub-build pick up `FROZEN_MANIFEST`
  through `MAKEFLAGS` and fail with `undefined reference to
  mp_qstr_frozen_const_pool` — the action always uses `build-$(BOARD)`
  now, same path Upload artifact already expects.

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

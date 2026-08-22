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

- All ten arches build in CI; four are executed (x64, x86, armv7m under QEMU,
  armv6m on rp2040py). Nothing has run on physical hardware, and for the
  remaining five cross targets "it links" is the entire claim.
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

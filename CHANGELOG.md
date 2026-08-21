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

- All ten arches build in CI; three are executed (x64, x86, armv7m under
  QEMU). Nothing has run on real hardware, and for the remaining six cross
  targets "it links" is the entire claim.
- No armv6m runtime leg yet (bclibc uses the rp2040py emulator for that).
- No benchmarks, no release packaging.
- `i64` marshalling goes through `mp_int_t` and will not round-trip large
  values on a 32-bit port.
- Globals, WASI, table/reference-type APIs are not exposed.

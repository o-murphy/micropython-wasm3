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

### Verified

- natmod builds and links for `x64`: text 85736 B, bss 428 B, 63 GOT
  entries, `wasm3_x64.mpy` 87667 B. This settles the open question about
  wasm3-as-natmod: the opcode tables in `m3_compile.c`, arrays of function
  pointers, relocate without trouble, and wasm3 needs no executable memory
  because it interprets rather than JITs.
- usermod builds into `ports/unix` (~108 KB of added text).
- 20/20 tests pass under both, covering calls and i32 wrapping, linear memory
  shared by reference in both directions, out-of-bounds traps, host imports
  called back into Python, and slot lifetime.
- `src/math_shim.c` checked bit-exact against glibc over 60M values,
  including signed zeros, subnormals, infinities and NaN.

### Not yet done

- No target other than `x64` has been built — no cross-toolchain was
  available. `armv6m`, `armv7m*`, `xtensa`, `xtensawin`, `rv32imc`, `rv64imc`
  have Makefile branches but are untried.
- No CI, no benchmarks, no release packaging.
- `i64` marshalling goes through `mp_int_t` and will not round-trip large
  values on a 32-bit port.
- Globals, WASI, table/reference-type APIs are not exposed.

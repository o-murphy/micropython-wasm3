# WebAssembly for MicroPython, via [Wasm3](https://github.com/wasm3/wasm3)

> [!WARNING]
> **Experimental.** APIs, module layout and build flags may change without
> notice. Only the `x64` target has been built and tested so far — see
> [Status](#status).

`wasm3` is an interpreter, so the `.wasm` blob is **not** baked into the
build: read it off the filesystem, pull it over the network, or generate it
at runtime, and run it without rebuilding anything. That is the whole point
of this repository, and the trade against the AOT alternative
([wasm2mpy](https://github.com/vshymanskyy/wasm2mpy), which compiles one
fixed blob into one `.mpy` and runs it at near-native speed).

Two integration modes, same Python API:

| Approach                | Location   | Architectures                                  | Deployment                            |
| ----------------------- | ---------- | ---------------------------------------------- | ------------------------------------- |
| **natmod** (`.mpy`)     | `natmod/`  | x64, x86, armv6m–armv7emdp, xtensa, rv32/64imc | Copy `.mpy` to the device filesystem  |
| **usermod** (baked in)  | `usermod/` | any port with `USER_C_MODULES` support         | Built into firmware — no file to copy |

Unlike `micropython-bclibc`, whose layout this repository follows, there is
no FFI mode: an FFI wrapper would need a `libwasm3.so` and the `ffi` module,
which only exists on the unix port, where CPython's own wasm runtimes are a
better answer anyway.

---

## Project structure

```
.
├── src/                        # Shared C + Python source
│   ├── wasm3_mp.c              # MicroPython C extension (natmod + usermod)
│   ├── wasm3_mp_config.h       # wasm3 build knobs, shared by both modes
│   ├── wasm3.py                # Python API wrapper (frozen / merged)
│   ├── libc_shim.c             # libc surface for natmod (GC heap, no libc)
│   ├── math_shim.c             # libm surface for natmod
│   └── math_shadow/math.h      # host-build <math.h> interception
│
├── wasm3/                      # git submodule -> o-murphy/wasm3
│
├── natmod/                     # Native module (.mpy) build
│   ├── Makefile                # make ARCH=<x64|armv6m|xtensawin|…> dist
│   ├── examples/
│   └── patches/micropython/    # mpy_ld patches, if any turn out to be needed
│
├── usermod/                    # Usermod (baked-into-firmware) build
│   ├── micropython.mk          # Make ports, via USER_C_MODULES
│   ├── micropython.cmake       # CMake ports (rp2 / esp32 / pico-sdk)
│   └── manifest.py             # Freezes wasm3.py into the firmware
│
├── tests/
│   ├── test_wasm3.py           # Test suite (natmod and usermod)
│   └── wasm/                   # Generated .wasm fixtures
│
├── tools/
│   └── make_test_wasm.py       # Hand-assembles tests/wasm/*.wasm
│
└── benchmarks/
```

---

## Status

Everything below was built and run on Linux x86-64 against **MicroPython
v1.28.0** — the version CI pins, and the latest release tag. Both build modes
were also checked against current `master` (v1.29.0-preview) with identical
results.

| Target      | natmod | usermod | tests    |
| ----------- | ------ | ------- | -------- |
| x64         | ✅     | ✅      | 20/20 ✅ |
| x86 (i386)  | ✅     | ✅      | 20/20 ✅ |
| armv6m      | ✅     | —       | not run  |
| armv7m      | ✅     | —       | not run  |
| armv7emsp   | ✅     | —       | not run  |
| armv7emdp   | ✅     | —       | not run  |
| rv32imc     | ✅     | —       | not run  |
| rv64imc     | ✅     | —       | not run  |
| xtensawin   | ✅     | —       | not run  |
| xtensa      | ✅     | —       | not run  |

Every arch builds in CI (`.github/workflows/natmod.yml`) and is uploaded as
an artifact. **Only x64 and x86 have ever been executed** — those are the
only two a GitHub runner can run natively. Nothing has been on real hardware,
so "builds" is the whole claim for the eight cross targets: an `.mpy` that
links is not an `.mpy` that runs.

An on-target leg (armv7m under QEMU, armv6m under an RP2040 emulator, as
`micropython-bclibc` does) needs a raw-REPL bridge this repo does not have
yet, and is the obvious next step.

The one arch that needed a fix beyond the toolchain was `xtensa` (ESP8266):
its older GCC raises a false `-Wmaybe-uninitialized` inside wasm3's
`Compile_Ref_Null`, which `-Werror` turns fatal — see the note in
`natmod/Makefile`.

What the x64 run establishes, which was the open question:

- **wasm3 links as a natmod at all.** Its two opcode tables
  (`c_operations` / `c_operationsFC`, `wasm3/source/m3_compile.c:3017`) are
  ~220 entries of five function pointers each. `mpy_ld` resolves them without
  complaint — 63 GOT entries in total.
- **No executable memory is needed.** wasm3 compiles wasm to arrays of C
  function pointers, allocated with plain `m3_Malloc`
  (`wasm3/source/m3_code.c:36`). A `.mpy` cannot allocate RWX pages, so a
  real JIT could not be a natmod; wasm3 can.
- **Size**, `-Os`: x64 — text 85736 B, bss 428 B, `wasm3.mpy` 87685 B;
  x86 — text 103348 B, bss 216 B, `wasm3.mpy` 105575 B. As a usermod the
  firmware grew by ~109 KB (x64) / ~140 KB (x86) of text. **A natmod's text is
  copied into the GC heap at import time** — it does not execute from flash —
  so on a RAM-constrained part (armv6m especially) prefer `usermod/`.

Known limitations:

- `i64` values travel through `mp_int_t`, because dynruntime exposes no
  64-bit integer helper. Lossless on a 64-bit port; on a 32-bit port, values
  outside the small-int range will not round-trip. `f64` is unaffected.
- `M3_HAS_TAIL_CALL=0` by default (see `src/wasm3_mp_config.h` for why —
  `musttail` is a hard compile error under natmod PIC on some ABIs). The
  interpreter is correct but slower, and the native stack grows per wasm
  call, bounded by `d_m3MaxNativeStack`.
- A Python exception raised inside a linked host import unwinds through
  wasm3's C frames. Treat that runtime as spent and `close()` it.
- `m3_CallArgv` is unavailable: it parses string arguments with
  `strtoul`/`strtod`, which `src/libc_shim.c` deliberately stubs out. Use the
  typed `wasm3.call()` path instead.
- Globals, WASI and the reference-types/table APIs are not exposed yet.

---

## Prerequisites

```sh
pip install -U pyelftools ar          # mpy_ld needs these
git submodule update --init --recursive
```

Plus a MicroPython checkout, and a matching `mpy-cross`:

```sh
# Match the version CI pins -- a natmod is only loadable by an interpreter
# with the same .mpy ABI.
git clone --depth 1 --branch v1.28.0 https://github.com/micropython/micropython
make -C micropython/mpy-cross
export MPY_DIR=$PWD/micropython
```

Cross-compilers, per target — `gcc-arm-none-eabi` (armv6m/armv7m*),
`xtensa-esp32-elf` / `xtensa-esp32s3-elf`, `gcc-riscv64-unknown-elf`.

---

## Build (natmod)

```sh
cd natmod
make ARCH=x64 MPY_DIR=/path/to/micropython dist
#  →  build/x64/wasm3.mpy
```

`ARCH` accepts `x64 x86 armv6m armv7m armv7emsp armv7emdp xtensa xtensawin
rv32imc rv64imc`. `dist` renames the arch-tagged module to plain `wasm3.mpy`
and drops the intermediates.

```sh
mpremote cp build/armv6m/wasm3.mpy :lib/
mpremote cp ../tests/wasm/add.wasm :
mpremote exec "import wasm3; m = wasm3.Module(open('add.wasm','rb').read()); print(m.add(3,4))"
7
```

## Build (usermod)

Point `USER_C_MODULES` at the **repository root** for Make ports (py.mk scans
`*/micropython.mk` beneath it), or at `usermod/micropython.cmake` for CMake
ports.

```sh
# unix port
make -C micropython/ports/unix \
    USER_C_MODULES=/path/to/micropython-wasm3 \
    FROZEN_MANIFEST=/path/to/micropython-wasm3/usermod/manifest.py

# RP2040 / pico-sdk
cmake -B build -DUSER_C_MODULES=/abs/path/to/usermod/micropython.cmake

# ESP32
idf.py build -DUSER_C_MODULES=/abs/path/to/usermod/micropython.cmake
```

---

## Test

```sh
python3 tools/make_test_wasm.py            # regenerate tests/wasm/*.wasm
cd natmod && make ARCH=x64 MPY_DIR=... && cd ..
ln -sf ../natmod/wasm3_x64.mpy tests/wasm3.mpy
cd tests && /path/to/micropython test_wasm3.py
```

The same file runs against a usermod firmware — remove `tests/wasm3.mpy`
first so the frozen module is not shadowed by the `.mpy` in the cwd.

The three fixtures under `tests/wasm/` are hand-assembled by
`tools/make_test_wasm.py` rather than produced by `wat2wasm`, so the suite
needs no WABT install. (`add.wasm` comes out byte-identical to
`wasm2mpy/test/simple.wasm`.)

---

## Module API

```py
import wasm3

wasm3.version()                      # -> str

m = wasm3.Module(blob, stack_bytes=2048)
m.function("add")(3, 4)              # -> 7
m.add(3, 4)                          # same thing, attribute sugar
m.link("env", "log", "v(ii)", fn)    # wire a wasm import to a Python callable
m.memory                             # bytearray *by reference* onto linear memory
m.mem_size                           # int
m.close()                            # or use `with wasm3.Module(...) as m:`
```

`link()` signatures use wasm3's own notation — result type first, argument
types in parentheses: `v` void, `i` i32, `I` i64, `f` f32, `F` f64. So
`"i(ii)"` is `(i32, i32) -> i32`.

`m.memory` is a live window, not a copy: writes from Python are visible to
wasm and vice versa. It is invalidated by `memory.grow`, so re-read it after
anything that might have grown.

The flat native functions underneath (`load`, `unload`, `find`, `call`,
`link`, `memory`, `mem_size`) are also exposed, in case you want to skip the
wrapper.

---

## License

MIT — see [LICENSE](LICENSE). Wasm3 itself is MIT, Copyright (c) 2019 Steven
Massey, Volodymyr Shymanskyy; it lives in the `wasm3/` submodule under its
own LICENSE.

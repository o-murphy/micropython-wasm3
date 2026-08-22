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
│   ├── ci/run_qemu.py          # raw-REPL bridge for the QEMU test leg
│   ├── examples/
│   └── patches/micropython/    # mpy_ld patches, if any turn out to be needed
│
├── usermod/                    # Usermod (baked-into-firmware) build
│   ├── micropython.mk          # Make ports, via USER_C_MODULES
│   ├── micropython.cmake       # CMake ports (rp2 / esp32 / pico-sdk)
│   └── manifest.py             # Freezes wasm3.py into the firmware
│
├── wasm2mpy/                   # git submodule -> o-murphy/wasm2mpy
│                               # (only for its test/*.wasm demo blobs)
│
├── tests/
│   ├── test_wasm3.py           # Core suite, hand-assembled fixtures
│   ├── test_wiring_apps.py     # Real blobs from the wasm2mpy submodule
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

Each cell is that build mode's own test result on that target — the two are
built and run independently, so a ✅ under `usermod` is a suite that actually
executed against a firmware with the module compiled in, not an inference
from the `natmod` column.

| Target             | natmod                          | usermod            |
| ------------------ | ------------------------------- | ------------------ |
| x64                | 51/51 ✅                        | 51/51 ✅           |
| x86 (i386)         | 51/51 ✅                        | 51/51 ✅           |
| aarch64            | impossible — no such `dynruntime.mk` ARCH | 51/51 ✅ |
| armhf (Linux)      | impossible — arm ARCHes are bare-metal EABI | 51/51 ✅ (qemu-user) |
| mipsel (Linux)     | impossible — no mips ARCH       | 51/51 ✅ (qemu-user) |
| Windows x64        | impossible — port has native emit off | 51/51 ✅ (native) |
| Windows x86        | impossible — port has native emit off | 51/51 ✅ (native, WOW64) |
| Windows arm64      | impossible — port has native emit off | 51/51 ✅ (native) |
| webassembly        | impossible — no WASM ARCH       | 51/51 ✅ (node) |
| armv6m (RP2040)    | 20/20 ✅ (rp2040py), blobs ⚠️   | 20/20 ✅ (rp2040py) |
| armv7m             | 49/49 ✅ (QEMU)                 | not built          |
| armv7emsp          | links, not run                  | not built          |
| armv7emdp          | links, not run                  | not built          |
| rv32imc            | links, not run                  | not built          |
| rv64imc            | links, not run                  | not built          |
| xtensawin (ESP32)  | links, not run                  | not built          |
| xtensa (ESP8266)   | links, not run                  | not built          |

51 = `test_wasm3.py` (20) plus `test_wiring_apps.py --slow` (31, CoreMark
included). 49 on armv7m is the same pair without CoreMark, which is left out
there because interpreted wasm inside an emulated Cortex-M3 would dominate
the job. 20 on RP2040 is the core suite only — see below for why the blob
suite does not pass there.

Every arch builds in CI (`.github/workflows/natmod.yml`) and is uploaded as
an artifact. Four are also executed: x64 and x86 natively on the runner,
**armv7m under QEMU** (`natmod/ci/run_qemu.py`, which pushes the `.mpy` and
every blob over the raw REPL — that port has no filesystem) and **armv6m on
an emulated RP2040** (`natmod/ci/run_rp2040py.py`, which flashes them into a
littlefs image, as on a real board). Nothing has run on physical hardware
yet, and for the remaining five cross targets "it links" is still the whole
claim.

The usermod half has its own workflow (`.github/workflows/usermod.yml`) —
separate because it shares no artifacts with the natmod jobs and fails for
different reasons: it links against the port's own libc rather than
`src/libc_shim.c`. It covers `ports/unix` on x64, x86 and **aarch64**
(natively, on an `ubuntu-24.04-arm` runner — `dynruntime.mk` has no aarch64
ARCH, so a natmod cannot reach that target at all), **armhf** and **mipsel**
(cross-built, statically linked, executed under qemu-user), `ports/rp2`
(`RPI_PICO`, run on the emulator), and `ports/windows` (via MSYS2 — x64 and
x86 on `windows-latest`, arm64 on `windows-11-arm`, each built and run
natively on the box that produced it), and `ports/webassembly` (wasm3
compiled to wasm, run under node).

"Impossible" for Windows is not a guess: `ports/windows/mpconfigport.h` sets
`MICROPY_EMIT_X64 (0)`, and `py/persistentcode.c` gates native `.mpy`
loading on `MICROPY_EMIT_MACHINE_CODE`. No `wasm3.mpy` loads on that port
whatever its arch.

The arm64 row builds with `-Wno-error`. MSYS2 ships no gcc for ARM64
Windows, so it is clang, and `ports/windows/Makefile` sets `-Werror` with a
gcc-tuned warning set that MicroPython's own sources do not survive under
clang — `py/binary.c` promotes a `_Float16` to `float`, and
`shared/runtime/gchelper_generic.c` declares deliberately uninitialized
`register const long x19 asm("x19")` and friends to capture callee-saved
registers for GC root scanning. Neither is this module's code, and neither
is a bug. The other eight usermod jobs keep `-Werror`, so nothing of ours
goes unchecked; this row is a build-and-run smoke test for a platform
nothing else in the matrix reaches.

The ports still uncovered, and why — the common thread is that wasm3
allocates through the port's `calloc()`, and few ports have a C heap:

- **`ports/qemu` (armv7m) as a usermod** — blocked, not skipped. wasm3
  allocates through the port's `calloc()`, and that port has neither a
  malloc nor a `MICROPY_C_HEAP_SIZE` knob to give it one. natmod already
  runs armv7m under QEMU functionally, so the missing coverage is narrow.
- **`ports/esp32`** — cannot be built in the environment this was developed
  in: the ESP-IDF component registry (`components-file.espressif.com`) is
  unreachable there, and `espressif/mdns` and `espressif/lan867x` are real
  dependencies of the port for the esp32 target, not vendored in the release
  tarball. Nothing about the module suggests a problem — the xtensa natmod
  already builds, and the IDF supplies a real heap — but it stays out rather
  than going in unverified.
- **`ports/esp8266`** — worse than uncovered: it would build and then be
  silently wrong. `ports/esp8266/posix_helpers.c:35` implements `malloc` as
  `gc_alloc`, and a usermod's globals live in firmware `.bss`, which
  `gc_collect()` does not scan — the same defect that made the GC-heap
  experiment segfault on unix. Needs `MP_REGISTER_ROOT_POINTER` first.
- **`ports/stm32`, `samd`, `nrf`, `alif`, `zephyr`, `cc3200`** — no C heap at
  all, so `calloc()` has nothing to allocate from. Same prerequisite as
  esp8266: routing wasm3's allocations at the GC heap, done properly with a
  registered root pointer.

### armv6m runs out of native stack, not RAM

The core suite passes on RP2040; the blob suite does not. Running each
failing blob on its own, on a freshly collected heap, separates the two
causes — and the binding one is not memory:

| blob             | free before | free after load | result                            |
| ---------------- | ----------- | --------------- | --------------------------------- |
| `zig`            | 140080      | 64560           | ✅ runs                            |
| `cpp`            | 140416      | 65552           | ⛔ maximum recursion depth exceeded |
| `assemblyscript` | 137248      | 56624           | ⛔ `[trap] stack overflow`          |
| `rust`           | 129024      | 40944           | ⛔ `[trap] stack overflow`          |

Every one of them loads, allocates its 64 KB linear memory, and still leaves
40–65 KB of heap. What kills three of the four is **call depth**: with
`M3_HAS_TAIL_CALL=0` the native stack grows once per wasm call (see
`src/wasm3_mp_config.h` for why it is off), and Cortex-M0+ frames on
RP2040's few-KB stack cannot absorb the deeper call graphs.
`d_m3MaxNativeStack` turns that into a trap instead of a crash; `cpp.wasm`
trips MicroPython's own C-stack guard first, which is independent
confirmation that the stack really is being consumed.

RAM is genuinely tight — the ~94 KB `.mpy` is copied into the ~192 KB heap at
import, which is half the budget before any wasm runs — and it does show up
*second-order*: run the seven blobs back-to-back and fragmentation turns
`rust`'s and `zig`'s failures into `MemoryError` instead. A `gc.collect()`
between modules fixes that part. But on a fresh heap the same blobs fail on
stack, so more RAM alone would not buy much here.

**This is accepted, not a work in progress.** On stock RP2040 firmware there
is no fix available from inside a `.mpy`, and shipping a patched MicroPython
would defeat the point of a natmod — a natmod is supposed to drop onto the
firmware people already have. What was tried:

| attempt                                      | result                          |
| -------------------------------------------- | ------------------------------- |
| `-O2` instead of `-Os`                        | 17/20, +17 KB of text            |
| explicit `-foptimize-sibling-calls`           | 17/20, byte-identical output — already on |
| `M3_HAS_TAIL_CALL=1`                          | no effect: `musttail` needs GCC 15, arm-none-eabi is 13 |
| running the interpreter on a heap stack       | not possible from a `.mpy` (below) |
| `PICO_STACK_SIZE` past `SCRATCH_Y`            | fails to link, 4 KB bank         |

GCC does not sibling-call wasm3's indirect dispatch under the natmod PIC
model on Thumb, and no optimisation flag changes that — the identical text
size on the third row is the proof that the flag was already in effect.

Switching the interpreter onto a heap-allocated stack is the one idea that
would work in principle and is out of reach in practice: `dynruntime.h` and
`mp_fun_table` expose nothing for it. `gc_collect()` derives its scan length
from `MP_STATE_THREAD(stack_top)` (`gchelper_generic.c:219`) and
`mp_cstack_check()` its depth from the same extents; a natmod cannot retarget
either, so moving the stack would corrupt root scanning and break the
recursion guard for any Python callback.

For the record, a patched firmware *does* fix most of it — MicroPython's
RP2350 script already relocates the stack into the GC heap's region, and the
two lines port to `memmap_mp_rp2040.ld` — taking the blob suite to 25/26 with
`d_m3MaxNativeStack` raised to match. Only `rust.wasm` still fails, ~15 KB
short of heap. That is documented here as a measurement, not a recommendation.

### What that means in practice

RP2040 runs this module; it runs it with a ceiling on how deep a wasm call
graph and how large a module can be. Four of the seven demo blobs — WAT,
Virgil, Zig, TinyGo — work fine, as does the whole core suite. Anything with
the call depth of AssemblyScript's or C++'s output, or the footprint of
Rust's, needs a part with a bigger stack: armv7m clears all of it (49/49
under QEMU).

What would actually help, in order of effort: build it as a `usermod`, so the
text executes from flash instead of occupying half the heap; or a toolchain
with working `musttail` — GCC gained it in 15, and arm-none-eabi 13 has no
such attribute, so `M3_HAS_TAIL_CALL=1` currently changes nothing there.
Either way the deep-call-graph blobs need the dispatch chain flattened, not
more memory.

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
- **No access to the indirect function table.** `m3_GetTableFunction()` is not
  bound, so a host import cannot dispatch through the table. That is what
  blocks Emscripten C++ modules built with `-fexceptions`: their `invoke_*`
  imports exist precisely to call a table entry and catch what it throws, and
  a stub returning 0 traps with `[trap] uninitialized element` the moment
  `__wasm_call_ctors` runs. Modules whose entry points do not go through the
  table work regardless — a C++ build of bclibc loads here, all 51 imports
  link, and `_BCLIBCFFI_get_version()` returns its string out of linear
  memory; anything needing constructors does not.
- **A usermod needs the port to have a C heap.** wasm3 allocates through the
  port's own `calloc`, and some ports default that to nothing: on rp2,
  `MICROPY_C_HEAP_SIZE` is `0`. `usermod/micropython.cmake` now fails the
  configure with an explanatory error rather than letting it become a CPU
  fault inside `wasm3.Module()`; build with `-DMICROPY_C_HEAP_SIZE=131072`.
  A natmod is unaffected — `src/libc_shim.c` routes it at the GC heap.
  `ports/qemu` looks worse than rp2 here: it has neither a C heap nor a knob
  to give it one, so a usermod there is likely blocked outright (untried).
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
ln -sf ../natmod/build/x64/wasm3.mpy tests/wasm3.mpy
cd tests
/path/to/micropython test_wasm3.py              # 20 checks, own fixtures
/path/to/micropython test_wiring_apps.py        # 29 checks, real blobs
/path/to/micropython test_wiring_apps.py --slow # + CoreMark (~20s)
```

`test_wiring_apps.py` runs the demo blobs out of the `wasm2mpy/` submodule —
the same program compiled from seven languages, plus CoreMark. It supplies
`wiring` in Python and gives `delay`/`millis` a *virtual* clock, so the apps
that print elapsed milliseconds emit a fixed string and every run is
byte-for-byte reproducible. CoreMark is the exception: it reads `millis()` to
decide how long to iterate, so a clock that never advances leaves it spinning
forever — it gets a real one.

The same file runs against a usermod firmware — remove `tests/wasm3.mpy`
first so the frozen module is not shadowed by the `.mpy` in the cwd.

The three fixtures under `tests/wasm/` are hand-assembled by
`tools/make_test_wasm.py` rather than produced by `wat2wasm`, so the suite
needs no WABT install. (`add.wasm` comes out byte-identical to
`wasm2mpy/test/simple.wasm`.)

---

## Running the wasm3 / wasm2mpy demo apps

`natmod/examples/run_wiring_app.py` runs the blobs from
[embedded-wasm-apps](https://github.com/wasm3/embedded-wasm-apps) — the same
ones in [wasm2mpy](https://github.com/vshymanskyy/wasm2mpy)'s `test/`
directory. They export `setup()`/`loop()` and import a small host interface
called `wiring`; wasm2mpy supplies that in C, compiled into each `.mpy`, and
this example supplies it in ordinary Python via `wasm3.link()`.

```sh
cp natmod/build/x64/wasm3.mpy natmod/examples/run_wiring_app.py .
cp /path/to/wasm2mpy/test/*.wasm .
micropython run_wiring_app.py zig.wasm
```

```
== zig.wasm (1208 bytes) ==
⚡ Zig is running!
0 Blink
1000 Blink
-- ok, 2000 ms --
```

All seven blink apps run unmodified — WAT, Zig, C++, Virgil, AssemblyScript,
Rust, TinyGo — with no rebuild between them, which is the whole difference
from the AOT approach. CoreMark runs too:

```sh
micropython run_wiring_app.py coremark.wasm --stack 16384 --loops 1
# Result: 1908.640
```

Two things that example had to get right, both worth copying:

- **`memory.grow` invalidates `m.memory`.** `rust.wasm` grows linear memory
  during `setup()` to seed its allocator, so a `bytearray` bound once at
  startup goes short mid-run. The example re-fetches whenever `m.mem_size`
  moves.
- **TinyGo needs `_initialize()`** called before `setup()`.

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

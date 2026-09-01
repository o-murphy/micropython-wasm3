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

| Approach               | Location   | Selected by | Covered in CI                                        | Deployment                           |
| ---------------------- | ---------- | ----------- | ---------------------------------------------------- | ------------------------------------ |
| **natmod** (`.mpy`)    | `natmod/`  | `ARCH`      | all 10 `dynruntime.mk` ARCHes                        | Copy `.mpy` to the device filesystem |
| **usermod** (baked in) | `usermod/` | port        | `unix`, `windows`, `webassembly`, `rp2` — 9 targets | Built into firmware — no file to copy |

A natmod reaches only the architectures `py/dynruntime.mk` knows about; a
usermod reaches any port with `USER_C_MODULES`, which is how aarch64, armhf,
Windows and wasm are covered at all. See [Status](#status) for the
per-target results and for the ports still out of reach.

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

The two halves are built and run independently, so a ✅ below is always a
suite that actually executed on that target — never an inference from the
other mode.

They are also keyed on different things, so they get a table each. A
natmod is selected by `ARCH`, and the ten values below are every one
`py/dynruntime.mk` defines — there is no eleventh to add. A usermod is
selected by *port*, and any port with `USER_C_MODULES` is a candidate.

### natmod — by `ARCH`

| ARCH               | built | executed                        |
| ------------------ | :---: | ------------------------------- |
| x64                | ✅    | 51/51 ✅ on the runner          |
| x86                | ✅    | 51/51 ✅ on the runner          |
| armv7m             | ✅    | 49/49 ✅ under QEMU (MPS2_AN385) |
| armv6m             | ✅    | 20/20 ✅ on rp2040py, blobs ⚠️  |
| armv7emsp          | ✅    | 51/51 ✅ on real 32-bit ARM Linux |
| armv7emdp          | ✅    | 51/51 ✅ on real 32-bit ARM Linux |
| rv32imc            | ✅    | —                               |
| rv64imc            | ✅    | —                               |
| xtensa (ESP8266)   | ✅    | —                               |
| xtensawin (ESP32)  | ✅    | —                               |

Six of ten are executed; for the other four "it links" is the whole claim.

The armv7emsp/armv7emdp rows are the only ones that run on physical ARM
hardware: `ubuntu-24.04-arm` executes AArch32 on its own CPU, so a 32-bit
armhf build of `ports/unix` is a real ARM host that loads a Cortex-M `.mpy`
directly. `py/persistentcode.h` allows it because `MPY_FEATURE_ARCH_TEST` is a
*range* (`ARMV6M <= x <= ARMV7EMDP`), not an equality.

armv6m and armv7m cannot join them, and the reason is worth stating because
the failure is silent. `py/dynruntime.mk` gives those two no
`-mfloat-abi=hard`, so their floats cross into the runtime in core registers
while an armhf host reads them from VFP registers per AAPCS-VFP. The `.mpy`
loads and then returns wrong numbers rather than failing — measured on
micropython-bclibc's module, where `find_zero_angle` came back `984.252` rad
(its range in feet) instead of `0.002502`. They keep their emulator legs,
which run them on the target they are actually built for.

None of this replaces those emulator legs. Running Cortex-M code inside a
Linux process proves the module and its relocations are right on real ARM
silicon; the QEMU leg proves it survives a `-nostdlib` firmware environment,
which is a different claim entirely.

### usermod — by port

| Port              | targets in CI                     | result             |
| ----------------- | --------------------------------- | ------------------ |
| `unix`            | x64, x86                          | 51/51 ✅ each      |
| `unix`            | aarch64 (native runner)           | 51/51 ✅           |
| `unix`            | armhf (static, real AArch32)      | 51/51 ✅           |
| `windows`         | x64, x86 (WOW64), arm64           | 51/51 ✅ each      |
| `webassembly`     | wasm, under node                  | 51/51 ✅           |
| `rp2`             | `RPI_PICO`, on rp2040py           | 20/20 ✅           |
| `rp2`             | `RPI_PICO2`/`RPI_PICO2_W` (RP2350), build-only | builds ✅, see below |
| `qemu`            | armv7m                            | blocked, see below |
| `esp32`           | `ESP32_GENERIC`, build-only       | builds ✅, see below |
| `esp8266`         | —                                 | unsafe, see below  |
| `stm32`, `samd`, `nrf`, `alif`, `zephyr`, `cc3200` | — | no C heap |
| `mimxrt`, `renesas-ra` | —                            | plausible, untried |

Eleven targets across six ports. Nine of them run the suites; `RPI_PICO2` and
`RPI_PICO2_W` are the two build-only rows, and that is for a reason no work
here can fix — `rp2040py` emulates RP2040 only and no runner has an RP2350
(QEMU has no RP2350 machine either, checked directly:
gitlab.com/qemu-project/qemu/-/work_items/3125 is an open feature request,
not something shipped), so there is nothing to execute an RP2350 image on.
See "Measured on RP2350". All nine executable targets run on real
hardware — mipsel was the only emulated one (qemu-user, GitHub has no mips
runner) and it is gone: Debian 13 "Trixie" dropped the mipsel port outright,
so the cross-toolchain cibuildmp's own `manylinux_2_39_mipsel` image needs
has no upstream to rebuild from any more (cibuildmp record 0068).

armhf moved off qemu in usermod run #19 and stayed green: `ubuntu-24.04-arm`
executes 32-bit ARM directly — measured on the runner itself, not assumed
from a datasheet — so the arm64 runner cross-builds the binary and then runs
it on its own CPU. The move is also why that row switched from upstream's
`gnueabi` to `gnueabihf`: soft-float armel baselines at ARMv5TE, whose SWP
atomics ARMv8 removed outright.

The last four rows come from reading the v1.28.0 tree rather than from
trying each: wasm3 allocates through the port's `calloc()`, `mimxrt` and
`renesas-ra` provide `_sbrk`, and the six listed as having no C heap
provide neither that nor a malloc of their own.

51 = `test_wasm3.py` (20) plus `test_wiring_apps.py --slow` (31, CoreMark
included). 49 on armv7m is the same pair without CoreMark, which is left out
there because interpreted wasm inside an emulated Cortex-M3 would dominate
the job. 20 on RP2040 is the core suite only — see below for why the blob
suite does not pass there.

Every arch builds in CI (`.github/workflows/natmod.yml`) and is uploaded as
an artifact. The six executed ones are x64 and x86 natively on the runner,
**armv7m under QEMU** (`natmod/ci/run_qemu.py`, which pushes the `.mpy` and
every blob over the raw REPL — that port has no filesystem), **armv6m on an
emulated RP2040** (`natmod/ci/run_rp2040py.py`, which flashes them into a
littlefs image, as on a real board), and **armv7emsp/armv7emdp on real ARM
silicon** — a 32-bit armhf `ports/unix` host on `ubuntu-24.04-arm`, no
emulator involved at all.

### Not done: musl for the static unix builds

The `armhf` row links `-static` against glibc, and glibc emits two
warnings on every such link:

```
Using 'dlopen' in statically linked applications requires at runtime
  the shared libraries from the glibc version used for linking
Using 'getaddrinfo' in statically linked applications requires at runtime
  the shared libraries from the glibc version used for linking
```

Both are real: glibc resolves NSS and `dlopen` through shared objects it still
expects to find at run time, so a "static" glibc binary is not fully
self-contained on the minimal target it was built for. musl has no NSS and a
stub `dlopen`, so the same build against musl has neither caveat.

Measured, not assumed — a musl static build came back with **zero** link
warnings against glibc's two, `ldd` reporting `not a dynamic executable`, and
`getaddrinfo` working. The cost is two config knobs: `MICROPY_PY_BTREE=0` and
`MICROPY_PY_FFI=0`.

Deliberately not implemented for now. Recorded here so the measurement is not
lost and so the next person does not have to re-derive it.


The usermod half has its own workflow (`.github/workflows/usermod.yml`) —
separate because it shares no artifacts with the natmod jobs and fails for
different reasons: it links against the port's own libc rather than
`src/libc_shim.c`.

Most of that table is targets a natmod cannot reach at all, which is what
the usermod half is for: `dynruntime.mk` has no aarch64 ARCH and no mips
ARCH, its arm ARCHes are bare-metal EABI rather than the Linux ABI, and
there is no WASM ARCH. The x64, x86 and RP2040 rows do overlap the natmod
table, and stay anyway — linking against the port's libc instead of
`src/libc_shim.c` is a genuinely different build, and that difference has
already produced one real defect (rp2's zero-byte C heap faulting the CPU
inside `wasm3.Module()`) and one real portability bug (the `f64` narrowing
below, found by `ports/qemu`).

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
is a bug. The other nine usermod jobs keep `-Werror`, so nothing of ours
goes unchecked; this row is a build-and-run smoke test for a platform
nothing else in the matrix reaches.

The ports still uncovered, and why — the common thread is that wasm3
allocates through the port's `calloc()`, and few ports have a C heap:

- **`ports/qemu` (armv7m) as a usermod** — attempted, and the wall is
  further in than this file used to claim. `ports/qemu/Makefile:74` links
  `-nostdlib` with only libgcc, so the build reaches the linker and fails
  on both halves at once: libm (`sqrt`, `rint`, `trunc`, `floor`, `ceil`,
  `rintf`) and libc (`calloc`, `free`, `realloc`).

  `src/libc_shim.c` and `src/math_shim.c` already supply exactly those —
  it is what they exist for — but they allocate through `m_realloc_maybe`,
  i.e. the GC heap, and a usermod's globals live in firmware `.bss`, which
  `gc_collect()` does not scan. Compiling them in without more would link
  cleanly and then corrupt: the same failure the GC-heap experiment
  produced on unix.

  So this needs `MP_REGISTER_ROOT_POINTER`, and the bookkeeping is the easy
  half. MicroPython's GC only traces a candidate pointer that lands on a
  block boundary, so interior pointers are not followed, and wasm3 holds
  plenty of them. Establishing that every live wasm3 allocation stays
  reachable through a block-aligned pointer is the real work. Genuinely
  open, not a CI knob.

  natmod already runs armv7m under QEMU at 49/49, so the missing coverage
  is narrow.
- **`ports/esp32`** — now in CI as a **build-only** job
  (`BOARD=ESP32_GENERIC`, ESP-IDF v5.5.1), the one target here with no
  execution step: there is no esp32 emulator to hand a firmware image to the
  way rp2040py takes an RP2040 `.uf2`.

  It used to say "cannot be built" here, and that was a claim about one
  machine rather than about the target: `dl.espressif.com` and
  `components-file.espressif.com` were both refused by this project's
  development environment, and `espressif/mdns` and `espressif/lan867x` are
  real dependencies of the port for target esp32, vendored in neither the
  release tarball nor esp-idf itself. So this job went in CI-first, below this
  repo's usual bar of reproducing a build locally before pushing. Both hosts
  have since been allowed, so that caveat no longer applies to further work
  here.

  I expected the port's flash and IRAM budget to be what stopped this, since
  wasm3 is an interpreter rather than a leaf module and a usermod goes into
  the firmware image. It was not — the build never got that far. What stopped
  it was `-Werror=maybe-uninitialized` firing on upstream wasm3's own
  `m3_validate.c:628`, a false positive gcc only reaches after inlining the
  whole validator body, and one that only ESP-IDF's warning set turns fatal
  (the `xtensawin` natmod compiles the same sources green). See
  `usermod/micropython.cmake` for the one `-Wno-error=` that settles it.
- **`ports/esp8266`** — worse than uncovered: it would build and then be
  silently wrong. `ports/esp8266/posix_helpers.c:35` implements `malloc` as
  `gc_alloc`, and a usermod's globals live in firmware `.bss`, which
  `gc_collect()` does not scan — the same defect that made the GC-heap
  experiment segfault on unix. Needs `MP_REGISTER_ROOT_POINTER` first.
- **`ports/stm32`, `samd`, `nrf`, `alif`, `zephyr`, `cc3200`** — no C heap at
  all, so `calloc()` has nothing to allocate from. Same prerequisite as
  esp8266: routing wasm3's allocations at the GC heap, done properly with a
  registered root pointer.

### f64 narrows on single-precision ports

`mp_float_t` is `float` wherever `MICROPY_FLOAT_IMPL_FLOAT` is selected —
`ports/qemu` among them, which rejects the implicit conversion outright
under `-Werror=float-conversion`. A wasm `f64` returned to Python therefore
loses precision on those ports. The conversion is explicit in
`src/wasm3_mp.c` so it is visible rather than accidental, but it is a real
limitation and not merely a compiler complaint: use `f32` exports, or a
double-precision port, if the extra bits matter.

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
| `M3_HAS_TAIL_CALL=1`                          | impossible on this core — see below |
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

**`M3_HAS_TAIL_CALL=1` will never work on RP2040, and the reason is the core,
not the toolchain.** This table used to say "`musttail` needs GCC 15,
arm-none-eabi is 13". Both halves are now wrong: the toolchain cibuildmp
builds this with (`ghcr.io/ballistics-lab/arm_embedded`) pins arm-none-eabi
**GCC 15.2.1**, `__has_attribute(musttail)` is true there — and it still fails,
with a different and final reason:

```
cortex-m0plus: cannot tail-call: machine description does not have
               a sibcall_epilogue instruction pattern
cortex-m33:    OK
cortex-m55:    OK
```

GCC's ARMv6-M backend has no sibling-call epilogue at all, so no compiler
version will ever unblock this. That matters because the old wording pointed at
"wait for a newer toolchain", which is a dead end — what is *not* a dead end is
a core that has the pattern.

What would actually help, in order of effort: build it as a `usermod`, so the
text executes from flash instead of occupying half the heap; or move to a
Cortex-M33/M55 part, where `musttail` compiles and the dispatch chain stops
growing the native stack per wasm call. Either way the deep-call-graph blobs
need the chain flattened, not more memory.

### Measured on RP2350

`RPI_PICO2` builds, and with tail calls it builds too; `RPI_PICO2_W` builds
too — all three through `cibuildmp` on the toolchain above, `v1.28.0`:

| build                                     | firmware  | note |
| ----------------------------------------- | --------- | ---- |
| `RPI_PICO` (RP2040, Cortex-M0+)           | 887808 B  | the baseline this README describes |
| `RPI_PICO2` (RP2350, Cortex-M33), stock   | 860160 B  | first build of this module for RP2350 |
| `RPI_PICO2` + `M3_HAS_TAIL_CALL=1`        | 861184 B  | +1024 B; compiles and links |
| `RPI_PICO2_W` (RP2350, cyw43 wifi), stock | 1861120 B | roughly 2x `RPI_PICO2` -- the wifi/BT stack (cyw43, lwip, mbedtls) this board bakes in by default, not this module |

**These are build results, not test results.** Nothing here has run the suites
on RP2350: `rp2040py` emulates RP2040 only (`--board {pico,pico_w}` on every
subcommand, and it pins Kaluma 1.2.1 precisely because 1.3.0+ ships `pico2`
images it cannot run), so the blob suite on RP2350 needs real hardware.

QEMU's `MPS3_AN547` (Cortex-M55) looked like a way around that — an ARMv8-M
core where `musttail` compiles, already a cibuildmp `qemu` target, no hardware
needed. It is not: a `usermod` on `ports/qemu` does not link at all, for the
reason this README already gives two sections down (no C heap, no knob to add
one). Tried, so it is no longer a guess — `undefined reference to 'free'` and
`'realloc'`, same as predicted. Whether flattening the dispatch chain clears
the four failing blobs is therefore still open, and RP2350 hardware is the
shortest way to close it.

Two traps found while measuring this, both worth knowing before repeating it:

- **Do not pass `-DCMAKE_C_FLAGS=…`** (via `extra-cmake-args` or otherwise) to
  add a define. Setting that variable on the command line pre-seeds the cache,
  so pico-sdk's own toolchain file never applies its `CMAKE_C_FLAGS_INIT`
  (`-mcpu=cortex-m33 -mthumb`), and the build dies in the SDK with
  `no SW_SPIN_LOCK_LOCK available for PICO_USE_SW_SPIN_LOCK on this platform`.
  Confirmed to be the variable and not its contents: a harmless
  `-DHARMLESS_PROBE=1` fails identically. Put the define in the module's own
  `target_compile_options` instead, which is where the measurement above got it.
- **`ports/rp2/Makefile` runs `cmake` only when the build directory is absent**
  (`[ -e $(BUILD)/Makefile ] || cmake …`). Change a cmake argument without
  deleting `build-<BOARD>/` and it silently does not apply: the build "succeeds"
  in about a second and produces a byte-identical firmware.

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
  to give it one, so a usermod there is blocked outright — **tried now**, on
  `MPS3_AN547` (Cortex-M55) with arm-none-eabi 15.2.1, and it fails exactly
  where the bullet above predicts: `undefined reference to 'free'` and
  `'realloc'` out of `m3_core.o`, plus `dangerous relocation: unsupported
  relocation` on the same symbols. The core makes no difference; the missing
  C heap does.
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

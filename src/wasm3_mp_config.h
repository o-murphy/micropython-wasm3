/**
 * wasm3_mp_config.h — Wasm3 build configuration for MicroPython targets.
 *
 * Pulled into every wasm3 translation unit via `-include` (see
 * natmod/Makefile and usermod/micropython.{mk,cmake}) so that natmod and
 * usermod builds share one set of knobs. Every macro here is a documented
 * wasm3 knob from wasm3/source/m3_config.h; this file only overrides the
 * defaults that are wrong for a MicroPython target.
 *
 * Override any of these from the build (-D...) — each is #ifndef-guarded.
 */

#ifndef WASM3_MP_CONFIG_H
#define WASM3_MP_CONFIG_H

/* ── Code pages ───────────────────────────────────────────────────────────
 * wasm3 does not JIT: it compiles wasm into arrays of C function pointers
 * ("metacode") allocated with plain m3_Malloc (wasm3/source/m3_code.c:36),
 * so no executable/RWX memory is ever needed. That is what makes wasm3
 * usable as a natmod at all — a .mpy cannot allocate executable pages.
 *
 * The stock 32 KiB page is sized for desktops. On an MCU it means a single
 * 32 KiB allocation from the MicroPython GC heap before any user code runs.
 */
#ifndef d_m3CodePageAlignSize
#define d_m3CodePageAlignSize           1024
#endif

/* ── Stacks and limits ────────────────────────────────────────────────────
 * d_m3MaxFunctionStackHeight is a compile-time bound on the operand stack of
 * a single wasm function; with d_m3Use32BitSlots it costs 4 bytes per slot.
 * 2000 slots is generous for a desktop, wasteful on a device.
 */
#ifndef d_m3MaxFunctionStackHeight
#define d_m3MaxFunctionStackHeight      256
#endif

/* Cap linear memory growth. Stock default is the wasm spec maximum (65536
 * pages = 4 GiB), which is meaningless here: memory comes from the GC heap.
 * 16 pages = 1 MiB is already more than most MCU targets can give.
 *
 * This is the wrong number for a host build, and natmod/Makefile raises it
 * for x64/x86 accordingly: a module compiled for the web asks for whatever
 * its toolchain chose, with no regard for this project's budget — an
 * Emscripten C++ build measured here declares an initial 258 pages (16.1 MB)
 * before a line of it runs. On a host that is unremarkable; on an MCU it is
 * simply out of reach, and the cap is what turns it into a refusal at load
 * rather than a failed allocation somewhere later.
 */
#ifndef d_m3MaxLinearMemoryPages
#define d_m3MaxLinearMemoryPages        16
#endif

/* Bound on native (C) stack recursion, probed via __builtin_frame_address.
 * The stock 8 MiB assumes a desktop thread stack. On a device the whole
 * MicroPython stack is typically 16-64 KiB, and with M3_HAS_TAIL_CALL=0
 * (below) the native stack grows on every wasm call, so this is the guard
 * that turns a runaway wasm recursion into a trap instead of a hard crash.
 * Keep it comfortably under the port's actual stack size.
 */
#ifndef d_m3MaxNativeStack
#define d_m3MaxNativeStack              (8 * 1024)
#endif

/* ── Error reporting ──────────────────────────────────────────────────────
 * d_m3VerboseErrorMessages=1 makes m3_env.c:1301 call vsnprintf(), which
 * drags a full printf formatter into the link. A natmod has no libc to
 * borrow one from, and MicroPython's own mp_printf is not a vsnprintf.
 * Turning it off leaves the static M3Result error strings, which is what
 * wasm3_mp.c surfaces as the exception message anyway.
 */
#ifndef d_m3VerboseErrorMessages
#define d_m3VerboseErrorMessages        0
#endif

/* Backtraces keep op_Entry frames alive (see m3_config.h:80) and allocate
 * per-call bookkeeping. Neither is affordable here. */
#ifndef d_m3RecordBacktraces
#define d_m3RecordBacktraces            0
#endif

/* ── Tail calls ───────────────────────────────────────────────────────────
 * This is the one that bites on natmod.
 *
 * wasm3 dispatches every opcode with `M3_MUSTTAIL return nextOpImpl()`
 * (wasm3/source/m3_exec_defs.h:62). M3_MUSTTAIL expands to
 * __attribute__((musttail)) whenever the compiler advertises the attribute
 * (m3_config_platforms.h:93). Under the natmod PIC model calls go through
 * GOT indirection, and on targets whose ABI forbids indirect sibling calls
 * -- xtensawin (the ESP32 windowed ABI) most notably -- musttail is a hard
 * compile error, not a silent fallback to a normal call.
 *
 * Setting M3_HAS_TAIL_CALL=0 makes M3_MUSTTAIL empty and, via
 * M3_GUARANTEED_TAIL_CALL=0, also clears d_m3CanTailCall (m3_config.h:93).
 * The interpreter stays correct; it is slower, `return_call` stops being
 * iterative, and the native stack grows per wasm call -- which is exactly
 * what d_m3MaxNativeStack above is there to bound.
 *
 * On a usermod build (no PIC restrictions) you can try flipping this back
 * to 1 for the speed, if your toolchain's musttail works on the target.
 */
#ifndef M3_HAS_TAIL_CALL
#define M3_HAS_TAIL_CALL                0
#endif

/* ── Host layer ───────────────────────────────────────────────────────────
 * wasm3 picks an m3_host_*.h implementation from the OS it is compiled on
 * (m3_config_platforms.h): anything that defines __linux__ gets
 * m3_host_posix.h -- mmap/mprotect guard pages around each linear memory, a
 * SIGSEGV handler, and pthread_getattr_np for the stack bounds. A natmod
 * built with the host gcc (x64, x86) sees __linux__ and would pull all of
 * that in with no libc to link it against. A usermod build could link it,
 * but should not want it either: linear memory would be mmap'd outside the
 * MicroPython GC heap, and wasm3 would own the process's SIGSEGV handler.
 *
 * m3_host_none.h answers "cannot say" to every question, which also leaves
 * d_m3GuardedMemory off, so bounds checks stay explicit in the interpreter.
 */
#ifndef d_m3HasPosixHost
#define d_m3HasPosixHost                0
#endif
#ifndef d_m3HasWin32Host
#define d_m3HasWin32Host                0
#endif

/* M3_THREAD_LOCAL is __thread on any GCC, and its one user outside the
 * POSIX host is m3_NativeStackLimit caching m3_HostStackBase() -- which
 * m3_host_none.h answers with NULL, so there is nothing to cache. A natmod
 * cannot have it anyway: TLS under PIC is a call to __tls_get_addr. */
#ifndef M3_THREAD_LOCAL
#define M3_THREAD_LOCAL
#define M3_HAS_THREAD_LOCAL             0
#endif

/* ── Features ─────────────────────────────────────────────────────────────
 * Validation is a pre-pass over the bytecode. Keep it: this module is meant
 * to load .wasm blobs that did not come from the firmware image, and the
 * validator is what stands between a malformed blob and the interpreter. */
#ifndef d_m3EnableValidation
#define d_m3EnableValidation            1
#endif

#endif /* WASM3_MP_CONFIG_H */

/**
 * libc_shim.c — the libc surface wasm3 needs, backed by MicroPython.
 *
 * A natmod (.mpy) links against no C library at all: it may only call
 * MicroPython's own exported runtime through mp_fun_table. Everything the
 * wasm3 core references has to be provided here.
 *
 * The surface is small because it was measured, not guessed. In a release
 * build (no DEBUG, d_m3VerboseErrorMessages=0 — see wasm3_mp_config.h) the
 * only libc symbols wasm3's core translation units reference are:
 *
 *   calloc free realloc            m3_core.c:129,134,141
 *   memcpy memset memmove memcmp   m3_core.c, m3_env.c, m3_compile.c
 *   strlen strcmp                  name lookups in m3_env.c / m3_module.c
 *   abort                          m3_core.c:19 (m3_Abort)
 *
 * Every printf/snprintf/sprintf/puts site in the wasm3 sources is inside
 * `#ifdef DEBUG`, `#if d_m3Log*`, or a comment — m3_info.c compiles to
 * nothing without DEBUG — and the one vsnprintf (m3_env.c:1301) is behind
 * d_m3VerboseErrorMessages. So no printf formatter is pulled into the link.
 *
 * Only compiled for natmod builds; a usermod links the port's own libc.
 */

#if !defined(__riscv)
/* Define errno before any include so it lands in .bss, not .data — mpy_ld
 * rejects a non-empty .data section in a linked native module. */
int errno;
#endif

#include "py/dynruntime.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ── Allocation ───────────────────────────────────────────────────────────
 * wasm3 allocates its code pages, runtime stack and wasm linear memory
 * through these. Routing them at the MicroPython GC heap is what makes the
 * memory show up in gc.mem_free() and get reclaimed on soft reset.
 */
/* m_realloc_maybe rather than m_malloc/m_realloc throughout: dynruntime's
 * m_malloc() funnels through m_realloc_checked_dyn(), which calls
 * m_malloc_fail() on exhaustion and raises MemoryError through nlr
 * (py/dynruntime.h:83-93). That longjmps out of whatever wasm3 frame happened
 * to be allocating, past its own cleanup. wasm3 is written to handle a NULL
 * return instead — every caller checks, and it surfaces as m3Err_mallocFailed
 * — so returning NULL is both safer and more informative.
 *
 * No size header here, unlike the usermod path in m3_core_mp.c: dynruntime's
 * m_free() takes a pointer alone, so there is nothing to remember. */
void *calloc(size_t num, size_t size) {
    size_t bytes = num * size;
    if (size != 0 && bytes / size != num) {
        return NULL;   /* overflow */
    }
    /* GC memory comes back cleared, which is what calloc promises. */
    return m_realloc_maybe(NULL, bytes, false);
}

void free(void *ptr) {
    m_free(ptr);
}

void *realloc(void *ptr, size_t new_size) {
    /* Returns NULL and leaves the old block intact on failure, which is the
     * contract m3_Realloc_Impl() is written against. */
    return m_realloc_maybe(ptr, new_size, true);
}

/* ── Memory / string ──────────────────────────────────────────────────────
 * memcpy and memset go through mp_fun_table so they resolve to the port's
 * own (often assembly) implementations instead of dragging in a copy.
 */
void *memcpy(void *dst, const void *src, size_t n) {
    return mp_fun_table.memmove_(dst, src, n);
}

void *memmove(void *dst, const void *src, size_t n) {
    return mp_fun_table.memmove_(dst, src, n);
}

void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}

int memcmp(const void *vl, const void *vr, size_t n) {
    const unsigned char *l = vl, *r = vr;
    for (; n && *l == *r; n--, l++, r++);
    return n ? *l - *r : 0;
}

size_t strlen(const char *str) {
    const char *s;
    for (s = str; *s; ++s);
    return (size_t)(s - str);
}

int strcmp(const char *l, const char *r) {
    for (; *l == *r && *l; l++, r++);
    return *(const unsigned char *)l - *(const unsigned char *)r;
}

/* ── Termination ──────────────────────────────────────────────────────────
 * m3_Abort() calls abort() on unrecoverable internal errors. Turning that
 * into a Python exception keeps the REPL alive instead of resetting the
 * board; the trailing loop is unreachable but satisfies noreturn.
 */
#if !defined(__riscv)
int *__errno(void) {
    return &errno;
}

int *__errno_location(void) {
    return &errno;
}
#endif

__attribute__((noreturn))
void abort(void) {
    mp_raise_msg(&mp_type_RuntimeError, "wasm3: aborted");
    for (;;) {}
}

/* ── Deliberately unimplemented ───────────────────────────────────────────
 * m3_CallArgv() (wasm3/source/m3_env.c:1096) converts string arguments with
 * strtoul/strtoull/strtod. It is a convenience entry point for wasm3's own
 * CLI and WASI hosts; this module never calls it — wasm3.call() marshals
 * typed values through m3_Call() instead. But mpy_ld links whole objects, so
 * m3_env.o still carries the references and the link fails without them.
 *
 * Rather than drag a correctly-rounded strtod into every .mpy for a function
 * that cannot be reached, these resolve the link and fail loudly if that
 * assumption is ever wrong. These three symbols appear nowhere else in the
 * wasm3 sources — verified by grep, and worth re-checking on a submodule
 * bump.
 */
unsigned long strtoul(const char *s, char **end, int base) {
    (void)s; (void)end; (void)base;
    mp_raise_msg(&mp_type_NotImplementedError, "wasm3: m3_CallArgv unsupported");
    return 0;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    (void)s; (void)end; (void)base;
    mp_raise_msg(&mp_type_NotImplementedError, "wasm3: m3_CallArgv unsupported");
    return 0;
}

double strtod(const char *s, char **end) {
    (void)s; (void)end;
    mp_raise_msg(&mp_type_NotImplementedError, "wasm3: m3_CallArgv unsupported");
    return 0.0;
}

__attribute__((noreturn))
void __stack_chk_fail(void) {
    abort();
}

__attribute__((noreturn))
void __stack_chk_fail_local(void) {
    abort();
}

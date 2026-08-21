/**
 * math_shim.c — the libm surface wasm3 needs, self-contained.
 *
 * wasm3's float opcodes map straight onto libm (wasm3/source/m3_exec.h:272-302):
 *
 *   f32/f64.abs      -> fabsf     / fabs
 *   f32/f64.ceil     -> ceilf     / ceil
 *   f32/f64.floor    -> floorf    / floor
 *   f32/f64.trunc    -> truncf    / trunc
 *   f32/f64.sqrt     -> sqrtf     / sqrt
 *   f32/f64.nearest  -> rintf     / rint
 *   f32/f64.copysign -> copysignf / copysign
 *
 * (wasm3 carries its own rint/rintf fallback, but only for __AVR__ —
 * m3_math_utils.h:87 — so every target here needs a real one.)
 *
 * All of these except sqrt are exact bit manipulations with no lookup
 * tables, so they are implemented here rather than pulled from a libm: a
 * natmod cannot link the toolchain's libm.a on several targets anyway (see
 * the notes in natmod/Makefile), and a whole fdlibm object would be dragged
 * in for a handful of one-liners. sqrt/sqrtf must be *correctly rounded* to
 * satisfy the wasm spec, so those two come from MicroPython's own libm
 * sources instead — the Makefile adds lib/libm/ef_sqrt.c and
 * lib/libm_dbl/sqrt.c.
 *
 * Note on rint: the "add and subtract 2^52" identity below only works under
 * the default round-to-nearest-even mode, and only if the compiler does not
 * algebraically cancel the two operations — hence the volatile. Do not build
 * this file with -ffast-math / -Ofast.
 *
 * Only compiled for natmod builds; a usermod links the port's own libm.
 */

#include <stdint.h>

/* ── Sign manipulation ────────────────────────────────────────────────── */

double fabs(double x) {
    union { double f; uint64_t i; } u = { x };
    u.i &= ~(uint64_t)0 >> 1;
    return u.f;
}

float fabsf(float x) {
    union { float f; uint32_t i; } u = { x };
    u.i &= 0x7fffffffu;
    return u.f;
}

double copysign(double x, double y) {
    union { double f; uint64_t i; } ux = { x }, uy = { y };
    ux.i &= ~(uint64_t)0 >> 1;
    ux.i |= uy.i & ((uint64_t)1 << 63);
    return ux.f;
}

float copysignf(float x, float y) {
    union { float f; uint32_t i; } ux = { x }, uy = { y };
    ux.i &= 0x7fffffffu;
    ux.i |= uy.i & 0x80000000u;
    return ux.f;
}

/* ── Truncation family ────────────────────────────────────────────────────
 * Shared shape: decode the unbiased exponent, and if the value has any
 * fraction bits at all, clear them — adding the fraction mask first when the
 * result must round away from zero. NaN/Inf fall out through the `e >= 52`
 * (`e >= 23`) early return untouched, and signed zero is preserved.
 */

double trunc(double x) {
    union { double f; uint64_t i; } u = { x };
    int e = (int)(u.i >> 52 & 0x7ff) - 0x3ff;
    uint64_t m;
    if (e >= 52) return x;
    if (e < 0) {
        u.i &= (uint64_t)1 << 63;   /* |x| < 1 -> +-0.0 */
        return u.f;
    }
    m = (~(uint64_t)0 >> 12) >> e;
    if ((u.i & m) == 0) return x;
    u.i &= ~m;
    return u.f;
}

float truncf(float x) {
    union { float f; uint32_t i; } u = { x };
    int e = (int)(u.i >> 23 & 0xff) - 0x7f;
    uint32_t m;
    if (e >= 23) return x;
    if (e < 0) {
        u.i &= 0x80000000u;
        return u.f;
    }
    m = 0x007fffffu >> e;
    if ((u.i & m) == 0) return x;
    u.i &= ~m;
    return u.f;
}

double floor(double x) {
    union { double f; uint64_t i; } u = { x };
    int e = (int)(u.i >> 52 & 0x7ff) - 0x3ff;
    uint64_t m;
    if (e >= 52) return x;
    if (e < 0) {
        /* |x| < 1: -0.0 and +0.0 stay, other negatives go to -1.0 */
        if ((u.i << 1) == 0) return x;
        return (u.i >> 63) ? -1.0 : 0.0;
    }
    m = (~(uint64_t)0 >> 12) >> e;
    if ((u.i & m) == 0) return x;
    if (u.i >> 63) u.i += m;        /* negative: round away from zero */
    u.i &= ~m;
    return u.f;
}

float floorf(float x) {
    union { float f; uint32_t i; } u = { x };
    int e = (int)(u.i >> 23 & 0xff) - 0x7f;
    uint32_t m;
    if (e >= 23) return x;
    if (e < 0) {
        if ((u.i << 1) == 0) return x;
        return (u.i >> 31) ? -1.0f : 0.0f;
    }
    m = 0x007fffffu >> e;
    if ((u.i & m) == 0) return x;
    if (u.i >> 31) u.i += m;
    u.i &= ~m;
    return u.f;
}

double ceil(double x) {
    union { double f; uint64_t i; } u = { x };
    int e = (int)(u.i >> 52 & 0x7ff) - 0x3ff;
    uint64_t m;
    if (e >= 52) return x;
    if (e < 0) {
        if ((u.i << 1) == 0) return x;
        return (u.i >> 63) ? -0.0 : 1.0;
    }
    m = (~(uint64_t)0 >> 12) >> e;
    if ((u.i & m) == 0) return x;
    if (!(u.i >> 63)) u.i += m;     /* positive: round away from zero */
    u.i &= ~m;
    return u.f;
}

float ceilf(float x) {
    union { float f; uint32_t i; } u = { x };
    int e = (int)(u.i >> 23 & 0xff) - 0x7f;
    uint32_t m;
    if (e >= 23) return x;
    if (e < 0) {
        if ((u.i << 1) == 0) return x;
        return (u.i >> 31) ? -0.0f : 1.0f;
    }
    m = 0x007fffffu >> e;
    if ((u.i & m) == 0) return x;
    if (!(u.i >> 31)) u.i += m;
    u.i &= ~m;
    return u.f;
}

/* ── Round half to even ───────────────────────────────────────────────────
 * f32.nearest / f64.nearest are round-half-to-even, which is precisely what
 * the default FP rounding mode does when a value is forced to lose its
 * fraction bits by adding and then subtracting 2^52 (2^23 for float).
 */

double rint(double x) {
    static const double toint = 4503599627370496.0;  /* 2^52 */
    union { double f; uint64_t i; } u = { x };
    int e = (int)(u.i >> 52 & 0x7ff) - 0x3ff;
    volatile double y;
    if (e >= 52) return x;                 /* integral already, or NaN/Inf */
    if (u.i >> 63) y = (x - toint) + toint;
    else           y = (x + toint) - toint;
    if (y == 0.0) return (u.i >> 63) ? -0.0 : 0.0;   /* keep the sign */
    return y;
}

float rintf(float x) {
    static const float toint = 8388608.0f;           /* 2^23 */
    union { float f; uint32_t i; } u = { x };
    int e = (int)(u.i >> 23 & 0xff) - 0x7f;
    volatile float y;
    if (e >= 23) return x;
    if (u.i >> 31) y = (x - toint) + toint;
    else           y = (x + toint) - toint;
    if (y == 0.0f) return (u.i >> 31) ? -0.0f : 0.0f;
    return y;
}

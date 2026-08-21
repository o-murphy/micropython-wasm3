#ifndef _MPY_WASM3_MATH_SHADOW_H
#define _MPY_WASM3_MATH_SHADOW_H

/* Host builds only (x64/x86) — see natmod/Makefile.
 *
 * The double-precision sqrt() we borrow from MicroPython lives in
 * lib/libm_dbl, whose libm.h declares musl's internal kernel helpers
 * __sin(double,double,int), __cos(double,double), __tan(double,double,int).
 * glibc's <math.h> declares __sin/__cos/__tan as one-argument hidden
 * aliases, so the two collide with a conflicting-types error.
 *
 * Intercept <math.h> by include-path priority: rename glibc's aliases out of
 * the way while the real header is processed, then undo the renames so
 * libm.h can declare the musl prototypes unmolested. Cross toolchains
 * (newlib, picolibc) have no such aliases and never see this file.
 */

#define __sin _glibc_sin1arg
#define __cos _glibc_cos1arg
#define __tan _glibc_tan1arg

#include_next <math.h>

#undef __sin
#undef __cos
#undef __tan

#endif /* _MPY_WASM3_MATH_SHADOW_H */

#ifndef _MPY_WASM3_CTYPE_SHADOW_H
#define _MPY_WASM3_CTYPE_SHADOW_H

/* Natmod builds only — see natmod/Makefile.
 *
 * m3_env.c's argument parser for m3_CallArgv calls isspace(). Every libc
 * makes that a macro over its own classification table, and the table is a
 * different symbol in each: glibc's __ctype_b_loc(), newlib's _ctype_. A
 * natmod links no libc, so rather than shim each table per toolchain,
 * intercept <ctype.h> by include-path priority and drop the macro. The call
 * then goes to the plain isspace() the header already declares, which
 * libc_shim.c provides.
 */

#include_next <ctype.h>

#undef isspace

#endif /* _MPY_WASM3_CTYPE_SHADOW_H */

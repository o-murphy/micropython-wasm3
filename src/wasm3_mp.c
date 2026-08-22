/**
 * wasm3_mp.c — MicroPython native module backed by Wasm3.
 *
 * Unlike an AOT approach (wasm2mpy and friends), the .wasm blob is *not*
 * baked into this module: wasm3 is a full interpreter, so a blob can be read
 * off the filesystem, pulled over the network, or generated at runtime and
 * executed without rebuilding anything.
 *
 * Python API (flat functions; the ergonomic wrapper lives in wasm3.py)
 * -------------------------------------------------------------------
 *   import wasm3
 *   wasm3.version()                       -> str
 *   wasm3.load(blob, stack_bytes=2048)    -> int handle
 *   wasm3.unload(handle)                  -> None
 *   wasm3.find(handle, name)              -> int fn slot
 *   wasm3.call(handle, fn, *args)         -> None | value | tuple
 *   wasm3.link(handle, mod, fn, sig, fn)  -> None    (host import)
 *   wasm3.memory(handle)                  -> bytearray (by reference)
 *   wasm3.mem_size(handle)                -> int
 *
 * Lifetime note: m3_ParseModule does not copy the bytes it is handed, and
 * the parsed module keeps pointing into them for as long as it lives. Rather
 * than rooting the caller's object, load() takes its own m_malloc'd copy;
 * that copy is referenced from this module's .bss, which the MicroPython GC
 * scans, so it stays alive exactly as long as the slot does.
 */

#ifdef WASM3_BUILD_NATMOD
#include "py/dynruntime.h"
/* dynruntime maps the float API onto explicit double-width helpers. */
#undef mp_obj_new_float
#define mp_obj_new_float(d) mp_obj_new_float_from_d((double)(d))
#undef mp_obj_get_float
#define mp_obj_get_float(o) mp_obj_get_float_to_d(o)
#define WASM3_RAISE(msg) mp_raise_msg(&mp_type_RuntimeError, (msg))
#define WASM3_RAISE_VALUE(msg) mp_raise_msg(&mp_type_ValueError, (msg))
/* dynruntime's m_free takes only a pointer; the core's takes a size too when
 * MICROPY_MALLOC_USES_ALLOCATED_SIZE is on. Spell the difference out here
 * rather than relying on m_del, which dynruntime.h redefines m_free out from
 * under. */
#define WASM3_FREE(ptr, nbytes) m_free(ptr)
#else
#include "py/runtime.h"
#include "py/objarray.h"
#define WASM3_RAISE(msg) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(msg))
#define WASM3_RAISE_VALUE(msg) mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT(msg))
#define WASM3_FREE(ptr, nbytes) m_del(uint8_t, (ptr), (nbytes))
#endif

#include "wasm3.h"
#include "generated/wasm3_mp/version.h"

/* ── Module state ─────────────────────────────────────────────────────────
 * One environment shared by every slot (that is what an M3Environment is
 * for: it owns the interned function types), and a small fixed table of
 * loaded modules. Fixed rather than growable on purpose — a handle is then
 * a plain small int, with no allocation to track and nothing for a stale
 * handle to dangle into.
 */

#ifndef WASM3_MP_MAX_SLOTS
#define WASM3_MP_MAX_SLOTS 4
#endif

#ifndef WASM3_MP_DEFAULT_STACK
#define WASM3_MP_DEFAULT_STACK 2048   /* bytes of wasm operand stack */
#endif

typedef struct _wasm3_slot_t {
    IM3Runtime  runtime;
    IM3Module   module;
    uint8_t *   blob;       /* our copy of the .wasm bytes (m_malloc'd) */
    uint32_t    blob_len;
    IM3Function fns[8];     /* functions resolved via find() */
    uint8_t     n_fns;
    bool        in_use;
} wasm3_slot_t;

/* Not static, deliberately: mpy_ld gives .bss variables GOT entries and a
 * file-static one gets a fixed relocation instead, which it rejects with
 * "bss variables can't be static". The wasm3_mp_ prefix stands in for the
 * file scope we cannot have. */
IM3Environment wasm3_mp_env;
wasm3_slot_t   wasm3_mp_slots[WASM3_MP_MAX_SLOTS];

/* wasm3 reports failure as a `const char *` (M3Result); m3Err_none is NULL.
 * With d_m3VerboseErrorMessages=0 these are the static strings from
 * m3_core.c, which is exactly what we want to surface to Python. */
static void check(M3Result r) {
    if (r != m3Err_none) {
        WASM3_RAISE(r);
    }
}

/* The environment is created on first use rather than at module init: on a
 * usermod build MP_REGISTER_MODULE has no init hook to hang it off, and
 * m3_NewEnvironment allocates from the GC heap either way. */
static void ensure_env(void) {
    if (wasm3_mp_env == NULL) {
        wasm3_mp_env = m3_NewEnvironment();
        if (wasm3_mp_env == NULL) {
            WASM3_RAISE("wasm3: out of memory");
        }
    }
}

static wasm3_slot_t *slot_of(mp_obj_t handle_obj) {
    mp_int_t h = mp_obj_get_int(handle_obj);
    if (h < 0 || h >= WASM3_MP_MAX_SLOTS || !wasm3_mp_slots[h].in_use) {
        WASM3_RAISE_VALUE("bad handle");
    }
    return &wasm3_mp_slots[h];
}

/* ── Value marshalling ────────────────────────────────────────────────────
 * wasm3's m3_Call/m3_GetResults take arrays of pointers to raw slots, so
 * every value is staged in a local union and the pointer array points at it.
 *
 * i64 caveat: dynruntime exposes no 64-bit integer helper, so an i64 travels
 * through mp_int_t. On a 64-bit port that is lossless; on a 32-bit port
 * values outside the small-int range will not round-trip. f64 is unaffected.
 */

typedef union _wasm3_val_t {
    uint32_t i32;
    uint64_t i64;
    float    f32;
    double   f64;
} wasm3_val_t;

static void py_to_wasm(mp_obj_t obj, M3ValueType type, wasm3_val_t *out) {
    switch (type) {
        case c_m3Type_i32: out->i32 = (uint32_t)mp_obj_get_int(obj); break;
        case c_m3Type_i64: out->i64 = (uint64_t)(int64_t)mp_obj_get_int(obj); break;
        case c_m3Type_f32: out->f32 = (float)mp_obj_get_float(obj); break;
        case c_m3Type_f64: out->f64 = mp_obj_get_float(obj); break;
        default: WASM3_RAISE_VALUE("unsupported wasm value type");
    }
}

static mp_obj_t wasm_to_py(M3ValueType type, const wasm3_val_t *v) {
    switch (type) {
        case c_m3Type_i32: return mp_obj_new_int((mp_int_t)(int32_t)v->i32);
        case c_m3Type_i64: return mp_obj_new_int((mp_int_t)(int64_t)v->i64);
        /* Explicit: clang warns on the implicit float -> double promotion
         * under -Wdouble-promotion, which ports/windows compiles with
         * -Werror on CLANGARM64. mp_float_t rather than double so this
         * stays a no-op under MICROPY_FLOAT_IMPL_FLOAT. */
        case c_m3Type_f32: return mp_obj_new_float((mp_float_t)v->f32);
        case c_m3Type_f64: return mp_obj_new_float(v->f64);
        default: WASM3_RAISE_VALUE("unsupported wasm value type");
    }
    return mp_const_none;
}

/* ── version() ────────────────────────────────────────────────────────── */

static mp_obj_t mp_wasm3_version(void) {
    return mp_obj_new_str(MP_WASM3_VERSION, strlen(MP_WASM3_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_wasm3_version_obj, mp_wasm3_version);

/* ── load(blob, stack_bytes=WASM3_MP_DEFAULT_STACK) ───────────────────── */

static mp_obj_t mp_wasm3_load(size_t n_args, const mp_obj_t *args) {
    ensure_env();

    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[0], &buf, MP_BUFFER_READ);
    if (buf.len == 0) {
        WASM3_RAISE_VALUE("empty wasm blob");
    }

    uint32_t stack = (n_args > 1) ? (uint32_t)mp_obj_get_int(args[1])
                                  : WASM3_MP_DEFAULT_STACK;

    int idx = -1;
    for (int i = 0; i < WASM3_MP_MAX_SLOTS; i++) {
        if (!wasm3_mp_slots[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        WASM3_RAISE("no free wasm3 slot");
    }
    wasm3_slot_t *s = &wasm3_mp_slots[idx];

    /* Own copy: the module keeps pointing at these bytes (see file header). */
    s->blob = m_malloc(buf.len);
    memcpy(s->blob, buf.buf, buf.len);
    s->blob_len = (uint32_t)buf.len;

    s->runtime = m3_NewRuntime(wasm3_mp_env, stack, NULL);
    if (s->runtime == NULL) {
        WASM3_FREE(s->blob, s->blob_len);
        s->blob = NULL;
        WASM3_RAISE("out of memory: runtime");
    }

    M3Result r = m3_ParseModule(wasm3_mp_env, &s->module, s->blob, s->blob_len);
    if (r == m3Err_none) {
        r = m3_LoadModule(s->runtime, s->module);
    }
    if (r != m3Err_none) {
        /* A module that failed to load is not owned by the runtime yet. */
        if (s->module) { m3_FreeModule(s->module); s->module = NULL; }
        m3_FreeRuntime(s->runtime);
        s->runtime = NULL;
        WASM3_FREE(s->blob, s->blob_len);
        s->blob = NULL;
        WASM3_RAISE(r);
    }

    s->n_fns = 0;
    s->in_use = true;
    return mp_obj_new_int(idx);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_wasm3_load_obj, 1, 2, mp_wasm3_load);

/* ── unload(handle) ───────────────────────────────────────────────────── */

static mp_obj_t mp_wasm3_unload(mp_obj_t handle_obj) {
    wasm3_slot_t *s = slot_of(handle_obj);
    /* m3_FreeRuntime also frees the modules loaded into it. */
    m3_FreeRuntime(s->runtime);
    WASM3_FREE(s->blob, s->blob_len);
    s->runtime = NULL;
    s->module = NULL;
    s->blob = NULL;
    s->blob_len = 0;
    s->n_fns = 0;
    s->in_use = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_wasm3_unload_obj, mp_wasm3_unload);

/* ── find(handle, name) -> fn slot ────────────────────────────────────── */

static mp_obj_t mp_wasm3_find(mp_obj_t handle_obj, mp_obj_t name_obj) {
    wasm3_slot_t *s = slot_of(handle_obj);
    if (s->n_fns >= MP_ARRAY_SIZE(s->fns)) {
        WASM3_RAISE("too many resolved functions");
    }
    const char *name = mp_obj_str_get_str(name_obj);
    IM3Function f;
    check(m3_FindFunction(&f, s->runtime, name));
    s->fns[s->n_fns] = f;
    return mp_obj_new_int(s->n_fns++);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_wasm3_find_obj, mp_wasm3_find);

/* ── call(handle, fn, *args) ──────────────────────────────────────────── */

/* Bound on a single call's arguments and results, in both directions. Sized
 * from real modules rather than taste: Emscripten output routinely has
 * imports past 8 parameters (a C++ build of bclibc reaches 23 on one of its
 * exception trampolines), while the arrays below are C-stack locals, and on
 * a Cortex-M0+ the whole stack is 8 KB. 16 covers ordinary signatures at
 * ~512 bytes of worst-case frame; anything beyond it fails loudly rather
 * than silently truncating. */
#ifndef WASM3_MP_MAX_ARGS
#define WASM3_MP_MAX_ARGS 16
#endif

static mp_obj_t mp_wasm3_call(size_t n_args, const mp_obj_t *args) {
    wasm3_slot_t *s = slot_of(args[0]);
    mp_int_t fi = mp_obj_get_int(args[1]);
    if (fi < 0 || fi >= s->n_fns) {
        WASM3_RAISE_VALUE("bad function slot");
    }
    IM3Function f = s->fns[fi];

    uint32_t argc = m3_GetArgCount(f);
    uint32_t retc = m3_GetRetCount(f);
    if (argc != n_args - 2) {
        WASM3_RAISE_VALUE("wrong number of arguments");
    }
    if (argc > WASM3_MP_MAX_ARGS || retc > WASM3_MP_MAX_ARGS) {
        WASM3_RAISE_VALUE("too many arguments or results");
    }

    wasm3_val_t vals[WASM3_MP_MAX_ARGS];
    const void *ptrs[WASM3_MP_MAX_ARGS];
    for (uint32_t i = 0; i < argc; i++) {
        py_to_wasm(args[2 + i], m3_GetArgType(f, i), &vals[i]);
        ptrs[i] = &vals[i];
    }

    check(m3_Call(f, argc, argc ? ptrs : NULL));

    if (retc == 0) {
        return mp_const_none;
    }

    wasm3_val_t rets[WASM3_MP_MAX_ARGS];
    const void *rptrs[WASM3_MP_MAX_ARGS];
    for (uint32_t i = 0; i < retc; i++) {
        rptrs[i] = &rets[i];
    }
    check(m3_GetResults(f, retc, rptrs));

    if (retc == 1) {
        return wasm_to_py(m3_GetRetType(f, 0), &rets[0]);
    }
    mp_obj_t items[WASM3_MP_MAX_ARGS];
    for (uint32_t i = 0; i < retc; i++) {
        items[i] = wasm_to_py(m3_GetRetType(f, i), &rets[i]);
    }
    return mp_obj_new_tuple(retc, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_wasm3_call_obj, 2, 2 + WASM3_MP_MAX_ARGS, mp_wasm3_call);

/* ── link(handle, module, field, signature, callable) ─────────────────────
 * Wires a wasm import to a Python callable. wasm3 hands raw calls a slot
 * array with the results first and the arguments after them (that is what
 * m3ApiReturnType/m3ApiGetArg walk, wasm3.h:386), and lets us stash a
 * userdata pointer per import — which is where the callable goes.
 *
 * The callable is reachable from the M3Function that wasm3 allocated with
 * m3_Malloc -> m_malloc, so the GC keeps it alive with the module.
 *
 * Caveat: a Python exception raised inside the callback unwinds via nlr
 * straight through wasm3's C frames. The runtime is left untouched but the
 * in-flight wasm call is abandoned; treat the runtime as spent and unload it.
 */
static const void *wasm3_trampoline(IM3Runtime runtime, IM3ImportContext ctx,
                                    uint64_t *sp, void *mem) {
    (void)runtime;
    (void)mem;
    mp_obj_t callable = MP_OBJ_FROM_PTR(ctx->userdata);
    IM3Function f = ctx->function;
    uint32_t retc = m3_GetRetCount(f);
    uint32_t argc = m3_GetArgCount(f);

    if (argc > WASM3_MP_MAX_ARGS) {
        return "too many arguments in host import";
    }

    uint64_t *ret_slots = sp;
    uint64_t *arg_slots = sp + retc;

    mp_obj_t py_args[WASM3_MP_MAX_ARGS];
    for (uint32_t i = 0; i < argc; i++) {
        wasm3_val_t v;
        M3ValueType t = m3_GetArgType(f, i);
        switch (t) {
            case c_m3Type_i32: v.i32 = *(uint32_t *)&arg_slots[i]; break;
            case c_m3Type_f32: v.f32 = *(float *)&arg_slots[i];    break;
            default:           v.i64 = arg_slots[i];               break;
        }
        py_args[i] = wasm_to_py(t, &v);
    }

    mp_obj_t res = mp_call_function_n_kw(callable, argc, 0, py_args);

    if (retc > 0) {
        wasm3_val_t v;
        M3ValueType t = m3_GetRetType(f, 0);
        py_to_wasm(res, t, &v);
        switch (t) {
            case c_m3Type_i32: *(uint32_t *)&ret_slots[0] = v.i32; break;
            case c_m3Type_f32: *(float *)&ret_slots[0]    = v.f32; break;
            default:            ret_slots[0]              = v.i64; break;
        }
    }
    return m3Err_none;
}

static mp_obj_t mp_wasm3_link(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    wasm3_slot_t *s = slot_of(args[0]);
    const char *mod_name = mp_obj_str_get_str(args[1]);
    const char *fn_name  = mp_obj_str_get_str(args[2]);
    const char *sig      = mp_obj_str_get_str(args[3]);
    check(m3_LinkRawFunctionEx(s->module, mod_name, fn_name, sig,
                               wasm3_trampoline, MP_OBJ_TO_PTR(args[4])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_wasm3_link_obj, 5, 5, mp_wasm3_link);

/* ── memory(handle) / mem_size(handle) ────────────────────────────────────
 * Handed out by reference, the same trick wasm2mpy uses: the bytearray is a
 * live window onto the module's linear memory, so writes from Python are
 * visible to wasm and vice versa. It is invalidated by a memory.grow — call
 * memory() again after anything that might have grown it.
 */

static mp_obj_t mp_wasm3_memory(mp_obj_t handle_obj) {
    wasm3_slot_t *s = slot_of(handle_obj);
    uint32_t size = 0;
    uint8_t *mem = m3_GetMemory(s->runtime, &size, 0);
    if (mem == NULL || size == 0) {
        WASM3_RAISE("module has no linear memory");
    }
    return mp_obj_new_bytearray_by_ref((size_t)size, mem);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_wasm3_memory_obj, mp_wasm3_memory);

static mp_obj_t mp_wasm3_mem_size(mp_obj_t handle_obj) {
    wasm3_slot_t *s = slot_of(handle_obj);
    return mp_obj_new_int((mp_int_t)m3_GetMemorySize(s->runtime));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_wasm3_mem_size_obj, mp_wasm3_mem_size);

/* ── Module registration ──────────────────────────────────────────────── */

#ifdef WASM3_BUILD_NATMOD

mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY

    mp_store_global(MP_QSTR_version,  MP_OBJ_FROM_PTR(&mp_wasm3_version_obj));
    mp_store_global(MP_QSTR_load,     MP_OBJ_FROM_PTR(&mp_wasm3_load_obj));
    mp_store_global(MP_QSTR_unload,   MP_OBJ_FROM_PTR(&mp_wasm3_unload_obj));
    mp_store_global(MP_QSTR_find,     MP_OBJ_FROM_PTR(&mp_wasm3_find_obj));
    mp_store_global(MP_QSTR_call,     MP_OBJ_FROM_PTR(&mp_wasm3_call_obj));
    mp_store_global(MP_QSTR_link,     MP_OBJ_FROM_PTR(&mp_wasm3_link_obj));
    mp_store_global(MP_QSTR_memory,   MP_OBJ_FROM_PTR(&mp_wasm3_memory_obj));
    mp_store_global(MP_QSTR_mem_size, MP_OBJ_FROM_PTR(&mp_wasm3_mem_size_obj));

    MP_DYNRUNTIME_INIT_EXIT
}

#else  /* usermod */

/* Registered as _wasm3, not wasm3: the frozen wasm3.py is the public module,
 * and it imports the native half from here. Registering both under the same
 * name would have the frozen script shadow the C module. (The natmod build
 * has no such split — dynruntime merges the two into one wasm3.mpy sharing a
 * single globals namespace.) */
static const mp_rom_map_elem_t wasm3_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__wasm3) },
    { MP_ROM_QSTR(MP_QSTR_version),  MP_ROM_PTR(&mp_wasm3_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_load),     MP_ROM_PTR(&mp_wasm3_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload),   MP_ROM_PTR(&mp_wasm3_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_find),     MP_ROM_PTR(&mp_wasm3_find_obj) },
    { MP_ROM_QSTR(MP_QSTR_call),     MP_ROM_PTR(&mp_wasm3_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_link),     MP_ROM_PTR(&mp_wasm3_link_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory),   MP_ROM_PTR(&mp_wasm3_memory_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_size), MP_ROM_PTR(&mp_wasm3_mem_size_obj) },
};
static MP_DEFINE_CONST_DICT(wasm3_module_globals, wasm3_module_globals_table);

const mp_obj_module_t wasm3_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wasm3_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__wasm3, wasm3_user_cmodule);

#endif

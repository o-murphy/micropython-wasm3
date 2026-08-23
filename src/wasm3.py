"""wasm3 — WebAssembly interpreter for MicroPython.

Thin, allocation-light wrapper over the native part of this same module.

dynruntime.mk merges this file's bytecode and the native code into a single
wasm3.mpy, and the merged module shares one globals namespace: by the time
this runs, mpy_init() has already stored `load`, `call`, `find` and friends
as globals here. There is no separate module to import them from -- hence
the fallback below, which just captures whatever mpy_init() left behind.
"""

try:  # usermod / firmware build: the native part is a real module
    from _wasm3 import (  # noqa: F401
        version,
        load,
        unload,
        find,
        call,
        link,
        memory,
        mem_size,
    )
except ImportError:  # natmod: native part already populated these globals
    pass


class Wasm3Error(RuntimeError):
    """Raised for wasm3 runtime failures (trap, link error, bad module)."""


class Function:
    """A resolved wasm export, callable like a Python function."""

    __slots__ = ("_h", "_i", "name")

    def __init__(self, handle, index, name):
        self._h = handle
        self._i = index
        self.name = name

    def __call__(self, *args):
        return call(self._h, self._i, *args)

    def __repr__(self):
        return "<wasm3.Function %s>" % self.name


class Module:
    """A loaded wasm module.

    >>> import wasm3
    >>> m = wasm3.Module(open('add.wasm', 'rb').read())
    >>> m.function('add')(3, 4)
    7

    Use it as a context manager, or call close(), to release the runtime and
    the module's linear memory back to the GC heap. There are only a handful
    of native slots (WASM3_MP_MAX_SLOTS, 4 by default), so leaking them
    matters on a device.
    """

    __slots__ = ("_h", "_fns")

    def __init__(self, blob, stack_bytes=2048):
        self._h = load(blob, stack_bytes)
        self._fns = {}

    def function(self, name):
        """Resolve an exported function. Cached — resolving costs a native slot."""
        fn = self._fns.get(name)
        if fn is None:
            fn = Function(self._h, find(self._h, name), name)
            self._fns[name] = fn
        return fn

    def __getattr__(self, name):
        # Sugar: m.add(3, 4) instead of m.function('add')(3, 4).
        if name.startswith("_"):
            raise AttributeError(name)
        return self.function(name)

    def link(self, module_name, field_name, signature, fn):
        """Wire a wasm import to a Python callable.

        `signature` is wasm3's own notation: return type, then argument types
        in parentheses -- 'i(ii)' for (i32, i32) -> i32, 'v(i)' for
        (i32) -> (), where v=void i=i32 I=i64 f=f32 F=f64.
        """
        link(self._h, module_name, field_name, signature, fn)

    @property
    def memory(self):
        """Linear memory as a bytearray *by reference* — writes go straight
        into the module's memory. Invalidated by memory.grow; re-read it
        after calling anything that might have grown."""
        return memory(self._h)

    @property
    def mem_size(self):
        return mem_size(self._h)

    def close(self):
        if self._h is not None:
            unload(self._h)
            self._h = None
            self._fns = {}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

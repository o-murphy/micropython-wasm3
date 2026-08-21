"""Smallest useful wasm3 example: load a blob, call an export, share memory.

    mpremote cp build/armv6m/wasm3.mpy :lib/
    mpremote cp ../tests/wasm/add.wasm :
    mpremote cp examples/hello_wasm3.py :
    mpremote exec "import hello_wasm3"
"""

import gc

import wasm3

print("wasm3", wasm3.version())


def load(path):
    with open(path, "rb") as f:
        return f.read()


# ── Call an export ────────────────────────────────────────────────────────
with wasm3.Module(load("add.wasm")) as m:
    print("add(3, 4)     =", m.add(3, 4))
    print("add(1000, 24) =", m.add(1000, 24))

# ── Host imports: wasm calling back into Python ───────────────────────────
# The signature is wasm3's own: result type first, argument types in
# parentheses. v=void i=i32 I=i64 f=f32 F=f64.
def add1(x):
    print("  ...wasm called add1(%d)" % x)
    return x + 1


with wasm3.Module(load("host.wasm")) as m:
    m.link("env", "add1", "i(i)", add1)
    print("twice(40)     =", m.function("twice")(40))

# ── Linear memory, shared by reference ────────────────────────────────────
with wasm3.Module(load("mem.wasm")) as m:
    mem = m.memory                      # bytearray onto the module's memory
    m.store(0, 0xCAFE)                  # wasm writes...
    print("mem[0:4]      =", bytes(mem[0:4]))   # ...Python sees it
    mem[8:12] = b"\x2a\x00\x00\x00"     # Python writes...
    print("load(8)       =", m.load(8))         # ...wasm sees it

# Runtimes are not tiny — close them (or use `with`) rather than waiting for
# the GC, and there are only WASM3_MP_MAX_SLOTS of them.
gc.collect()
print("free heap     =", gc.mem_free())

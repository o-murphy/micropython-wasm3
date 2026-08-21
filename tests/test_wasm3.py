"""Test suite for the wasm3 MicroPython module (natmod or usermod).

Runs on MicroPython, not CPython — plain asserts and a tiny harness rather
than unittest, which the unix port does not ship by default.

    # build the natmod first, then:
    ln -sf ../natmod/wasm3_x64.mpy tests/wasm3.mpy
    cd tests && micropython test_wasm3.py
"""

import gc
import wasm3

_passed = 0
_failed = 0


def check(name, cond, detail=""):
    global _passed, _failed
    if cond:
        _passed += 1
        print("  ok   %s" % name)
    else:
        _failed += 1
        print("  FAIL %s %s" % (name, detail))


def expect_raises(name, exc, fn, *args):
    try:
        fn(*args)
    except exc:
        check(name, True)
        return
    except Exception as e:  # noqa: BLE001 — reporting the wrong type is the point
        check(name, False, "raised %r, expected %s" % (e, exc.__name__))
        return
    check(name, False, "did not raise")


def read(path):
    with open(path, "rb") as f:
        return f.read()


print("wasm3 version:", wasm3.version())

# ── plain calls ───────────────────────────────────────────────────────────
print("call:")
with wasm3.Module(read("wasm/add.wasm")) as m:
    add = m.function("add")
    check("add(3, 4) == 7", add(3, 4) == 7, add(3, 4))
    check("add(10, 6) == 16", add(10, 6) == 16)
    check("add(-1, 1) == 0", add(-1, 1) == 0)
    # i32 wraps, it does not saturate or promote
    check("add wraps at 2**31", add(2147483647, 1) == -2147483648, add(2147483647, 1))
    check("attribute sugar", m.add(2, 5) == 7)
    check("function() is cached", m.function("add") is add)
    expect_raises("unknown export raises", Exception, m.function, "nope")
    expect_raises("wrong arg count raises", ValueError, add, 1)

# ── linear memory ─────────────────────────────────────────────────────────
print("memory:")
with wasm3.Module(read("wasm/mem.wasm")) as m:
    check("mem_size is one page", m.mem_size == 65536, m.mem_size)
    mem = m.memory
    check("memory is a bytearray", isinstance(mem, bytearray))
    check("memory length matches", len(mem) == 65536, len(mem))

    # wasm writes, Python reads
    m.store(0, 0x11223344)
    check("wasm store visible to Python",
          bytes(mem[0:4]) == b"\x44\x33\x22\x11", bytes(mem[0:4]))

    # Python writes, wasm reads — the bytearray is by reference, not a copy
    mem[16:20] = b"\x01\x00\x00\x00"
    check("Python write visible to wasm", m.load(16) == 1, m.load(16))

    # out-of-bounds access must trap, not corrupt the heap
    expect_raises("OOB load traps", Exception, m.function("load"), 0x7FFFFFF0)

# ── host imports ──────────────────────────────────────────────────────────
print("imports:")
calls = []


def add1(x):
    calls.append(x)
    return x + 1


with wasm3.Module(read("wasm/host.wasm")) as m:
    m.link("env", "add1", "i(i)", add1)
    check("twice(0) == 2", m.function("twice")(0) == 2)
    check("host fn called twice", calls == [0, 1], calls)

# ── lifetime ──────────────────────────────────────────────────────────────
print("lifetime:")
m = wasm3.Module(read("wasm/add.wasm"))
h = m._h
m.close()
check("close() is idempotent", m.close() is None)
expect_raises("stale handle rejected", ValueError, wasm3.call, h, 0, 1, 2)

# Slots are a fixed, small resource: loading and closing in a loop must not
# exhaust them, and the blob copy must be reclaimable.
blob = read("wasm/add.wasm")
for _ in range(32):
    with wasm3.Module(blob) as m:
        assert m.add(1, 1) == 2
gc.collect()
check("slots are released on close", True)

expect_raises("garbage blob rejected", Exception, wasm3.Module, b"not wasm at all")

print("\n%d passed, %d failed" % (_passed, _failed))
raise SystemExit(1 if _failed else 0)

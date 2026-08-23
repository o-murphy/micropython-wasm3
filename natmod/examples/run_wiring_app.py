"""Run the `wiring`-style wasm apps from wasm3/embedded-wasm-apps (and from
wasm2mpy's test/ directory) on top of this module.

Those blobs — Zig, Rust, C++, TinyGo, AssemblyScript, Virgil, WAT, CoreMark —
all export setup()/loop() and import a small host interface called `wiring`.
wasm2mpy provides that interface in C, compiled into each .mpy. Here it is
ordinary Python, linked at runtime, so the same blob runs unmodified with
nothing rebuilt.

    micropython run_wiring_app.py zig.wasm
    micropython run_wiring_app.py coremark.wasm --stack 8192
"""

import sys
import time

import wasm3


def make_wiring(mod, trace_pins=False):
    """Return the six host functions the `wiring` interface expects.

    Signatures are wasm3's: result first, then args in parens.
    v=void i=i32.
    """
    t0 = time.ticks_ms()

    # mod.memory is a window onto linear memory that memory.grow invalidates,
    # and these apps do grow it — rust.wasm grows during setup() to seed its
    # allocator, so a bytearray bound once here goes short mid-run and
    # print() starts reading past its end. Re-fetch whenever the size moved;
    # mem_size is a cheap int, the bytearray is not.
    cache = [mod.memory]

    def memory():
        mem = cache[0]
        if len(mem) != mod.mem_size:
            mem = mod.memory
            cache[0] = mem
        return mem

    def wiring_print(offset, length):
        mem = memory()
        if offset < 0 or length < 0 or offset + length > len(mem):
            raise ValueError("wiring.print out of bounds")
        sys.stdout.write(bytes(mem[offset:offset + length]).decode())

    def wiring_millis():
        return time.ticks_diff(time.ticks_ms(), t0)

    def wiring_delay(ms):
        time.sleep_ms(ms)

    def wiring_pin_mode(pin, mode):
        if trace_pins:
            print("  [pinMode %d -> %d]" % (pin, mode))

    def wiring_digital_write(pin, value):
        if trace_pins:
            print("  [digitalWrite %d <- %d]" % (pin, value))

    def wiring_stop_wdt():
        pass

    return (
        ("print",        "v(ii)", wiring_print),
        ("millis",       "i()",   wiring_millis),
        ("delay",        "v(i)",  wiring_delay),
        ("pinMode",      "v(ii)", wiring_pin_mode),
        ("digitalWrite", "v(ii)", wiring_digital_write),
        ("stopWdt",      "v()",   wiring_stop_wdt),
    )


def run(path, stack=4096, loops=2, trace_pins=False):
    with open(path, "rb") as f:
        blob = f.read()

    print("== %s (%d bytes) ==" % (path, len(blob)))
    m = wasm3.Module(blob, stack)
    try:
        for field, sig, fn in make_wiring(m, trace_pins):
            # Link every name in the interface: a module that does not import
            # one gets a "function lookup failed", which is not an error here.
            try:
                m.link("wiring", field, sig, fn)
            except Exception:
                pass

        # TinyGo emits a WASI-style _initialize that must run before setup().
        try:
            m.function("_initialize")()
        except Exception:
            pass

        t = time.ticks_ms()
        m.function("setup")()
        for _ in range(loops):
            m.function("loop")()
        print("-- ok, %d ms --" % time.ticks_diff(time.ticks_ms(), t))
    finally:
        m.close()


if __name__ == "__main__":
    args = sys.argv[1:]
    stack, loops, trace = 4096, 2, False
    paths = []
    i = 0
    while i < len(args):
        if args[i] == "--stack":
            i += 1
            stack = int(args[i])
        elif args[i] == "--loops":
            i += 1
            loops = int(args[i])
        elif args[i] == "--pins":
            trace = True
        else:
            paths.append(args[i])
        i += 1
    if not paths:
        print(__doc__)
        raise SystemExit(2)
    for p in paths:
        run(p, stack, loops, trace)

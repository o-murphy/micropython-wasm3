"""Run the real-world wasm blobs from the wasm2mpy submodule.

These are the embedded-wasm-apps demos — the same blob compiled from seven
different source languages — plus CoreMark. They are far better coverage than
the hand-assembled fixtures in tests/wasm/: real toolchain output, real
memory layouts, real host imports, and in rust.wasm's case a memory.grow
mid-run.

The `wiring` host interface is supplied here in Python, so nothing is
rebuilt per blob. delay()/millis() share a *virtual* clock: delay advances a
counter instead of sleeping and millis reads it back, which makes the runs
both instant and exactly reproducible — the apps that print elapsed
milliseconds then emit a fixed string.

    cd tests && micropython test_wiring_apps.py            # skips CoreMark
    cd tests && micropython test_wiring_apps.py --slow     # includes it

CoreMark is the one app that needs a real clock — see run_app().
"""

import gc
import sys
import time

import wasm3

try:
    _INJECTED  # noqa: B018 — set by natmod/ci/run_qemu.py; absent on a host run
except NameError:
    _INJECTED = None


def _wasm_dir():
    # Host run: the submodule sits beside tests/. On a device the blobs are
    # flashed to /wasm2mpy/test/ and there is no cwd to be relative to.
    # Under _INJECTED the dict is keyed by the host spelling, so keep it.
    if _INJECTED is not None:
        return "../wasm2mpy/test/"
    for cand in ("../wasm2mpy/test/", "/wasm2mpy/test/"):
        try:
            with open(cand + "simple.wasm", "rb"):
                return cand
        except OSError:
            pass
    return "../wasm2mpy/test/"


WASM_DIR = _wasm_dir()

# Exact stdout each app produces for setup() + two loop() calls under the
# virtual clock. wat and virgil do not import millis, so they print no
# elapsed-time prefix.
EXPECTED = {
    "wat":            "⚙️ WAT is running!\nBlink\nBlink\n",
    "virgil":         "✨ Virgil is running\nBlink\nBlink\n",
    "cpp":            "\U0001f929 C++ is running!\n0 Blink\n1000 Blink\n",
    "zig":            "⚡ Zig is running!\n0 Blink\n1000 Blink\n",
    "tinygo":         "\U0001f916 TinyGo is running!\n0 Blink\n1000 Blink\n",
    "assemblyscript": "\U0001f680 AssemblyScript is running!\n0 Blink\n1000 Blink\n",
    "rust":           "\U0001f980 Rust is running!\n0 Blink\n1000 Blink\n",
}

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


def read(path):
    # On QEMU there is no filesystem: the runner pushes every blob into
    # _INJECTED over the raw REPL and this reads from there instead.
    if _INJECTED is not None:
        return _INJECTED[path]
    with open(path, "rb") as f:
        return f.read()


def run_app(name, stack=4096, loops=2, real_clock=False):
    """Run one wiring app; return (stdout, clock_ms, pin_events).

    The virtual clock suits the blink apps, which only ever *report* elapsed
    time. It does not suit CoreMark, which reads millis() to decide how long
    to keep iterating: with a clock that never advances on its own, its timing
    loop never reaches the minimum run duration and the benchmark spins
    forever. Pass real_clock=True there.
    """
    m = wasm3.Module(read(WASM_DIR + name + ".wasm"), stack)
    clock = [0]
    t0 = time.ticks_ms()
    out = []
    pins = []

    def wiring_print(offset, length):
        # Re-fetched every call: rust.wasm grows linear memory during setup(),
        # which invalidates any bytearray bound before that.
        mem = m.memory
        if offset < 0 or length < 0 or offset + length > len(mem):
            raise ValueError("wiring.print out of bounds")
        out.append(bytes(mem[offset:offset + length]).decode())

    def wiring_millis():
        if real_clock:
            return time.ticks_diff(time.ticks_ms(), t0)
        return clock[0]

    def wiring_delay(ms):
        clock[0] += ms
        if real_clock:
            time.sleep_ms(ms)

    def wiring_pin_mode(pin, mode):
        pins.append(("mode", pin, mode))

    def wiring_digital_write(pin, value):
        pins.append(("write", pin, value))

    def wiring_stop_wdt():
        pass

    try:
        for field, sig, fn in (
            ("print",        "v(ii)", wiring_print),
            ("millis",       "i()",   wiring_millis),
            ("delay",        "v(i)",  wiring_delay),
            ("pinMode",      "v(ii)", wiring_pin_mode),
            ("digitalWrite", "v(ii)", wiring_digital_write),
            ("stopWdt",      "v()",   wiring_stop_wdt),
        ):
            # A blob that does not import a given name just has no such
            # function to link; that is not a failure.
            try:
                m.link("wiring", field, sig, fn)
            except Exception:
                pass

        # TinyGo emits a WASI-style _initialize that must precede setup().
        try:
            m.function("_initialize")()
        except Exception:
            pass

        m.function("setup")()
        for _ in range(loops):
            m.function("loop")()
        return "".join(out), clock[0], pins
    finally:
        m.close()


print("wasm3 version:", wasm3.version())
print("blink apps (setup + 2 loops, virtual clock):")

for name in sorted(EXPECTED):
    # Each app allocates a 64 KB linear memory. On a small-RAM target the
    # freed pages have to be reclaimed before the next one, or the run dies
    # partway through with MemoryError.
    gc.collect()
    try:
        out, clock, pins = run_app(name)
    except Exception as e:  # noqa: BLE001 — which app broke is the useful part
        check(name, False, "raised %r" % (e,))
        continue

    check("%s output" % name, out == EXPECTED[name], repr(out))
    # Two loops, 1000 ms of delay each.
    check("%s clock" % name, clock == 2000, clock)
    # Every app configures pin 2 as an output, then toggles it once per loop.
    writes = [p for p in pins if p[0] == "write"]
    check("%s pinMode" % name, pins[:1] == [("mode", 2, 1)], pins[:1])
    check("%s toggles pin 2" % name,
          len(writes) == 4
          and all(p[1] == 2 for p in writes)
          and all(writes[i][2] != writes[i + 1][2] for i in range(3)),
          writes)

# simple.wasm is the same module tools/make_test_wasm.py assembles by hand —
# a real-toolchain cross-check on the generator.
print("fixture cross-check:")
check("simple.wasm == generated add.wasm",
      read(WASM_DIR + "simple.wasm") == read("wasm/add.wasm"))

if "--slow" in sys.argv:
    print("coremark:")
    out, _, _ = run_app("coremark", stack=16384, loops=1, real_clock=True)
    check("coremark ran", "Running CoreMark" in out, repr(out[:60]))
    check("coremark reported a score", "Result: " in out, repr(out[-40:]))
    print("   " + out.strip().replace("\n", "\n   "))
else:
    print("coremark: skipped (pass --slow to include)")

print("\n%d passed, %d failed" % (_passed, _failed))
raise SystemExit(1 if _failed else 0)

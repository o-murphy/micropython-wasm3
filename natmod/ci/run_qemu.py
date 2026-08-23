#!/usr/bin/env python3
"""Run this repo's test suites on MicroPython under QEMU.

    python3 natmod/ci/run_qemu.py <firmware.elf> <natmod-build-dir>

The QEMU port has no filesystem, so everything the suites touch is pushed
over the raw REPL:

  * wasm3.mpy is served from a RAM-backed VFS mounted on sys.path, which is
    what makes `import wasm3` work at all;
  * every .wasm blob is injected as a dict into the target's globals, which
    the suites' read() helper consults instead of open() (see the _INJECTED
    hook in tests/test_wasm3.py).

The firmware needs a heap well above the port default: the armv7m wasm3.mpy
is ~82 KB of text and a natmod's text is copied into the GC heap at import,
before a single 64 KB wasm page is allocated on top. Build it with
MICROPY_HEAP_SIZE=1048576 (the port's linker gives RAM 2 MB to work with).
"""

import argparse
import os
import sys

# pyboard.py lives in MicroPython's tools/; MPY_DIR wins over the local guess.
_HERE = os.path.dirname(os.path.abspath(__file__))
_MPY_ROOT = os.environ.get("MPY_DIR") or os.path.join(_HERE, "..", "..", "micropython")
sys.path.insert(0, os.path.join(_MPY_ROOT, "tools"))

from pyboard import Pyboard  # noqa: E402

# Blob paths exactly as the suites spell them, so the injected dict keys match
# what read() is handed.
FIXTURES = ["wasm/add.wasm", "wasm/mem.wasm", "wasm/host.wasm"]
APPS = ["wat", "virgil", "cpp", "zig", "tinygo", "assemblyscript", "rust", "simple"]


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def mount_module(module_mpy):
    """Serve one .mpy from RAM so `import wasm3` finds it."""
    return (
        b"import sys, io, vfs\n"
        b"__mpy = " + repr(module_mpy).encode() + b"\n"
        b"class _F(io.IOBase):\n"
        b"  def __init__(self,d): self.d=d; self.off=0\n"
        b"  def ioctl(self,r,a): return 0 if r==4 else -1\n"
        b"  def readinto(self,b):\n"
        b"    b[:]=memoryview(self.d)[self.off:self.off+len(b)]\n"
        b"    self.off+=len(b); return len(b)\n"
        b"class _FS:\n"
        b"  def mount(self,r,m): pass\n"
        b"  def chdir(self,p): pass\n"
        b"  def stat(self,p):\n"
        b"    if p == '/wasm3.mpy': return (0,)*10\n"
        b"    raise OSError(-2)\n"
        b"  def open(self,p,m): return _F(__mpy)\n"
        b"vfs.mount(_FS(),'/__remote')\n"
        b"sys.path.insert(0,'/__remote')\n"
        b"import wasm3\n"
        b"del __mpy\n"
        b"import gc; gc.collect()\n"
    )


def inject_blobs(blobs):
    """Define _INJECTED in the target's globals, one chunk per blob.

    Sent as separate statements rather than one dict literal: the raw REPL
    has to buffer whatever it is handed, and a single ~30 KB expression is
    both slower to parse and needs the whole thing resident at once.
    """
    out = [b"_INJECTED = {}\n"]
    for path, data in blobs.items():
        out.append(b"_INJECTED[" + repr(path).encode() + b"] = " + repr(data).encode() + b"\n")
    out.append(b"import gc; gc.collect()\n")
    return b"".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", help="path to firmware.elf")
    ap.add_argument("build_dir", help="natmod build dir holding wasm3.mpy")
    ap.add_argument("--machine", default="mps2-an385")
    ap.add_argument("--qemu-extra", default="")
    ap.add_argument("--repo", default=os.path.join(_HERE, "..", ".."),
                    help="repo root (for tests/ and wasm2mpy/)")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    tests = os.path.join(repo, "tests")

    blobs = {}
    for rel in FIXTURES:
        blobs[rel] = read_file(os.path.join(tests, rel))
    for name in APPS:
        blobs["../wasm2mpy/test/%s.wasm" % name] = read_file(
            os.path.join(repo, "wasm2mpy", "test", "%s.wasm" % name))

    module = read_file(os.path.join(args.build_dir, "wasm3.mpy"))

    extra = " " + args.qemu_extra if args.qemu_extra else ""
    qemu_cmd = (
        "qemu-system-arm -machine %s -nographic -monitor null -semihosting%s "
        "-serial pty -kernel %s" % (args.machine, extra, args.firmware)
    )

    print("[QEMU] %s" % qemu_cmd, flush=True)
    pyb = Pyboard("execpty:" + qemu_cmd)
    pyb.enter_raw_repl()

    print("[QEMU] mounting wasm3.mpy (%d bytes) ..." % len(module), flush=True)
    pyb.exec_(mount_module(module), timeout=60)

    print("[QEMU] injecting %d blobs (%d bytes) ..."
          % (len(blobs), sum(len(b) for b in blobs.values())), flush=True)
    pyb.exec_(inject_blobs(blobs), timeout=120)

    failed = False
    # CoreMark is deliberately not run here: interpreted wasm inside an
    # emulated Cortex-M3 is slow enough that it would dominate the job.
    for i, suite in enumerate(("test_wasm3.py", "test_wiring_apps.py")):
        if i:
            # Each suite ends with `raise SystemExit`, which soft-resets the
            # target: the mounted VFS and _INJECTED both go with it, and the
            # next suite fails on `import wasm3`. Re-establish them.
            print("[QEMU] re-mounting after soft reset ...", flush=True)
            pyb.exec_(mount_module(module), timeout=60)
            pyb.exec_(inject_blobs(blobs), timeout=120)
        print("[QEMU] running %s ..." % suite, flush=True)
        try:
            out = pyb.exec_(read_file(os.path.join(tests, suite)), timeout=600)
            text = out.decode("utf-8", errors="replace")
        except Exception as e:  # a SystemExit(1) from the suite lands here
            text = str(e)
        print(text)
        # Each suite ends by printing "<n> passed, <m> failed"; anything else
        # (a traceback, a truncated run) is a failure too.
        if "FAIL" in text or ", 0 failed" not in text:
            failed = True

    pyb.exit_raw_repl()
    pyb.close()

    if failed:
        print("[QEMU] RESULT: FAILED", file=sys.stderr)
        sys.exit(1)
    print("[QEMU] RESULT: ALL PASSED")


if __name__ == "__main__":
    main()

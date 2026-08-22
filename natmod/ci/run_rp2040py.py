#!/usr/bin/env python3
"""Run a test suite on an emulated RP2040 (armv6m) via rp2040py.

    python3 natmod/ci/run_rp2040py.py <firmware.uf2> <natmod-build-dir> [suite...]

Unlike the QEMU leg, this target has a real filesystem, so the .mpy and the
.wasm blobs are flashed into a littlefs image exactly as they would be on a
board; only the test source goes over the wire. Requires `rp2040py` and
`littlefs-python`.

Defaults to tests/test_wasm3.py. tests/test_wiring_apps.py does NOT pass here
and is not run by default — see the note in README under Status: on a 264 KB
part the ~94 KB armv6m .mpy plus a 64 KB linear memory leaves too little for
the larger blobs, and with M3_HAS_TAIL_CALL=0 the deeper ones exhaust the
native stack. Both are properties of the target, not of the harness.
"""

import argparse
import os
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.abspath(os.path.join(_HERE, "..", ".."))

# RPI_PICO's littlefs partition: 1408 KB of 4096-byte blocks at the end of flash.
BLOCK_SIZE = 4096
BLOCK_COUNT = 352

FIXTURES = ["add", "mem", "host"]
APPS = ["wat", "virgil", "cpp", "zig", "tinygo", "assemblyscript", "rust", "simple"]


def build_image(mpy_path, out_path):
    """mpy_path may be None for a usermod firmware — see --no-mpy."""
    from littlefs import LittleFS

    fs = LittleFS(block_size=BLOCK_SIZE, block_count=BLOCK_COUNT)

    def put(dev_path, host_path):
        parts = dev_path.strip("/").split("/")
        for i in range(1, len(parts)):
            try:
                fs.mkdir("/" + "/".join(parts[:i]))
            except Exception:
                pass  # already there
        with open(host_path, "rb") as src, fs.open(dev_path, "wb") as dst:
            dst.write(src.read())

    if mpy_path is not None:
        put("/wasm3.mpy", mpy_path)
    for f in FIXTURES:
        put("/wasm/%s.wasm" % f, os.path.join(_REPO, "tests", "wasm", "%s.wasm" % f))
    for f in APPS:
        put("/wasm2mpy/test/%s.wasm" % f,
            os.path.join(_REPO, "wasm2mpy", "test", "%s.wasm" % f))

    with open(out_path, "wb") as f:
        f.write(fs.context.buffer)
    return out_path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", help="RPI_PICO firmware.uf2 matching MPY_TAG")
    ap.add_argument("build_dir",
                    help="natmod build dir holding wasm3.mpy; ignored with --no-mpy")
    ap.add_argument("suites", nargs="*", default=None,
                    help="suite filenames under tests/ (default: test_wasm3.py)")
    ap.add_argument("--image", default="/tmp/rp2040-littlefs.img")
    ap.add_argument("--no-mpy", action="store_true",
                    help="omit wasm3.mpy from the image — for a usermod "
                         "firmware, where the module is built in and a .mpy "
                         "on the filesystem would shadow the frozen wasm3.py")
    ap.add_argument("--rp2040py", default="rp2040py")
    args = ap.parse_args()

    suites = args.suites or ["test_wasm3.py"]

    mpy = None if args.no_mpy else os.path.join(args.build_dir, "wasm3.mpy")
    img = build_image(mpy, args.image)
    print("[rp2040py] littlefs image: %s (%d bytes)" % (img, os.path.getsize(img)),
          flush=True)

    failed = False
    for suite in suites:
        print("[rp2040py] running %s ..." % suite, flush=True)
        # The firmware must be the one built from the pinned MicroPython:
        # rp2040py's own default download is a different release, and a natmod
        # is only loadable by a matching .mpy ABI.
        proc = subprocess.run(
            [args.rp2040py, "micropython",
             "--image", args.firmware, "--littlefs", img, suite],
            cwd=os.path.join(_REPO, "tests"),
            capture_output=True, text=True)
        out = proc.stdout + proc.stderr
        print(out)
        if proc.returncode != 0 or "FAIL" in out or ", 0 failed" not in out:
            failed = True

    if failed:
        print("[rp2040py] RESULT: FAILED", file=sys.stderr)
        sys.exit(1)
    print("[rp2040py] RESULT: ALL PASSED")


if __name__ == "__main__":
    main()

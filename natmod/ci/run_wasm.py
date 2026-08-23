#!/usr/bin/env python3
"""Run a test suite against the ports/webassembly build, under node.

That port has no filesystem the interpreter can reach: MicroPython's open()
raises OSError 44 (ENOENT) for anything, even though node itself read the
script off the host. Both suites already carry an _INJECTED hook for exactly
this situation (added for the QEMU runner, which has the same problem for a
different reason), so this prepends a dict of every blob the suite asks for
and hands node one combined script.

Usage: run_wasm.py <micropython.mjs> <suite.py> [suite args...]
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TESTS = os.path.join(ROOT, "tests")


def collect():
    """Every .wasm the suites can ask for, keyed by the path they use."""
    blobs = {}
    wasm_dir = os.path.join(TESTS, "wasm")
    for name in sorted(os.listdir(wasm_dir)):
        if name.endswith(".wasm"):
            with open(os.path.join(wasm_dir, name), "rb") as f:
                blobs["wasm/" + name] = f.read()
    demo = os.path.join(ROOT, "wasm2mpy", "test")
    if os.path.isdir(demo):
        for name in sorted(os.listdir(demo)):
            if name.endswith(".wasm"):
                with open(os.path.join(demo, name), "rb") as f:
                    data = f.read()
                # Both spellings the suite probes for.
                blobs["../wasm2mpy/test/" + name] = data
                blobs["/wasm2mpy/test/" + name] = data
    return blobs


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    # Absolute: node is invoked with cwd=tests/ so the suite's own
    # relative paths behave, which would otherwise lose a relative mjs.
    mjs, suite = os.path.abspath(sys.argv[1]), sys.argv[2]
    args = sys.argv[3:]

    with open(os.path.join(TESTS, suite), "r") as f:
        source = f.read()

    parts = ["_INJECTED = {"]
    for path, data in collect().items():
        parts.append("    %r: %r," % (path, data))
    parts.append("}")
    # The suites read --slow through an _ARGV hook: node forwards nothing,
    # and this port's sys has no argv to fall back on either.
    parts.append("_ARGV = %r" % ([suite] + args))
    parts.append("")
    preamble = "\n".join(parts)

    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as tf:
        tf.write(preamble + source)
        combined = tf.name
    try:
        rc = subprocess.call(["node", mjs, combined], cwd=TESTS)
    finally:
        os.unlink(combined)
    sys.exit(rc)


if __name__ == "__main__":
    main()

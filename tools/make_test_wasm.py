#!/usr/bin/env python3
"""Emit the .wasm fixtures used by tests/test_wasm3.py.

Hand-assembled rather than produced by wat2wasm so the test suite has no
toolchain prerequisite: three modules, a few dozen bytes each, covering the
three things the native module has to get right — plain calls, linear memory
handed out by reference, and a host import routed back into Python.

    python3 tools/make_test_wasm.py [outdir]     # default: tests/wasm
"""

import os
import sys

MAGIC = bytes([0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00])


def section(sid, payload):
    assert len(payload) < 128, "fixtures stay under the 1-byte LEB limit"
    return bytes([sid, len(payload)]) + payload


def vec(items):
    return bytes([len(items)]) + b"".join(items)


def name(s):
    b = s.encode()
    return bytes([len(b)]) + b


# (module
#   (func (export "add") (param i32 i32) (result i32)
#     (i32.add (local.get 0) (local.get 1))))
ADD = MAGIC + b"".join([
    section(1, vec([bytes([0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F])])),   # type
    section(3, vec([bytes([0x00])])),                                 # func
    section(7, vec([name("add") + bytes([0x00, 0x00])])),             # export
    section(10, vec([bytes([0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6A, 0x0B])])),
])

# (module
#   (memory (export "memory") 1)
#   (func (export "load")  (param i32)     (result i32) (i32.load  (local.get 0)))
#   (func (export "store") (param i32 i32)              (i32.store (local.get 0) (local.get 1))))
MEM = MAGIC + b"".join([
    section(1, vec([
        bytes([0x60, 0x01, 0x7F, 0x01, 0x7F]),
        bytes([0x60, 0x02, 0x7F, 0x7F, 0x00]),
    ])),
    section(3, vec([bytes([0x00]), bytes([0x01])])),
    section(5, vec([bytes([0x00, 0x01])])),                           # memory, min 1 page
    section(7, vec([
        name("memory") + bytes([0x02, 0x00]),
        name("load") + bytes([0x00, 0x00]),
        name("store") + bytes([0x00, 0x01]),
    ])),
    section(10, vec([
        bytes([0x07, 0x00, 0x20, 0x00, 0x28, 0x02, 0x00, 0x0B]),
        bytes([0x09, 0x00, 0x20, 0x00, 0x20, 0x01, 0x36, 0x02, 0x00, 0x0B]),
    ])),
])

# (module
#   (import "env" "add1" (func $add1 (param i32) (result i32)))
#   (func (export "twice") (param i32) (result i32)
#     (call $add1 (call $add1 (local.get 0)))))
HOST = MAGIC + b"".join([
    section(1, vec([bytes([0x60, 0x01, 0x7F, 0x01, 0x7F])])),
    section(2, vec([name("env") + name("add1") + bytes([0x00, 0x00])])),
    section(3, vec([bytes([0x00])])),
    section(7, vec([name("twice") + bytes([0x00, 0x01])])),
    section(10, vec([bytes([0x08, 0x00, 0x20, 0x00, 0x10, 0x00, 0x10, 0x00, 0x0B])])),
])

MODULES = {"add.wasm": ADD, "mem.wasm": MEM, "host.wasm": HOST}


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests", "wasm")
    os.makedirs(outdir, exist_ok=True)
    for fname, blob in MODULES.items():
        path = os.path.join(outdir, fname)
        with open(path, "wb") as f:
            f.write(blob)
        print("%-10s %4d bytes" % (fname, len(blob)))


if __name__ == "__main__":
    main()

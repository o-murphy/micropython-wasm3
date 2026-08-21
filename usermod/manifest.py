# ruff: noqa
# Freeze wasm3.py (the Python API wrapper) into the firmware image.
# Pass to the port build via FROZEN_MANIFEST= (make) or --manifest (cmake).
#
# For port-specific builds (rp2, esp32, qemu) include the board's default
# manifest so _boot.py and the other standard frozen scripts survive. For
# unix port builds the include is a no-op, because
# $(PORT_DIR)/boards/manifest.py does not exist there.
try:
    include("$(PORT_DIR)/boards/manifest.py")
except Exception:
    pass

freeze("../src", "wasm3.py")

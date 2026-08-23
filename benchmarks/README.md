# Benchmarks

Nothing here yet.

The number worth measuring first is the interpretation overhead against the
AOT alternative on identical hardware: run the same CoreMark `.wasm` through
this module and through [wasm2mpy](https://github.com/vshymanskyy/wasm2mpy),
which compiles the blob to native code ahead of time. wasm2mpy publishes
per-board CoreMark scores, so the comparison is a like-for-like ratio rather
than an absolute number.

Also worth recording per target, since they decide whether a board is usable
at all:

- `.mpy` text size, and heap free before/after `import wasm3`
- heap cost of `wasm3.Module(blob)` for a representative blob
- compile time (`m3_LoadModule`) vs. steady-state call time — wasm3 compiles
  the whole module to metacode at load, so a short-lived module pays for it
- native stack high-water mark, which is what `d_m3MaxNativeStack` bounds and
  which grows per wasm call while `M3_HAS_TAIL_CALL=0` (see
  `src/wasm3_mp_config.h`)

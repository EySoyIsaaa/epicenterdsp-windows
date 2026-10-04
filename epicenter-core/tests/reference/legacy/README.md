# Legacy reference core (FROZEN — do not edit)

Byte-identical copy of the prototype DSP core, taken on 2026-10-04 from
`epicenter-lab/` **before** the Professional 1.0 refactor started.

| File | SHA-256 |
|---|---|
| `EpicenterDSPCore.cpp` | `394E8ABE0D9C7FF438CF8A582E3B0807404DA03ED5CD340665F591331F1F035A` |
| `EpicenterDSPCore.hpp` | `0E7550387B71B1B841D9240DDCE6F9E8E5A9AF58BBBD3A10D99C7FA559B2126C` |

Purpose:

* Oracle for the **OLD CORE vs NEW CORE** regression tests (`tests/test_regression.cpp`).
  Both cores are compiled into the same test binary with identical compiler flags and are
  fed identical input, so any difference in output is a real behavioural change.
* Source of the golden reference files in `tests/audio/golden/` (`tools/generate_golden.cpp`).
* "BEFORE" side of the benchmark (`benchmarks/core_benchmark.cpp`).

This code is **never** linked into the shipping `epicenter_core` library. It lives in the
`epicenter::` namespace; the new implementation lives in `epicenter::core_internal::`, so
both can coexist in one binary without symbol clashes.

The legacy core is invoked with the factory voicing used by every existing host
(`epicenter-windows`, `epicenter-lab` defaults): `sweep=45, width=50, balance=100, volume=100`.

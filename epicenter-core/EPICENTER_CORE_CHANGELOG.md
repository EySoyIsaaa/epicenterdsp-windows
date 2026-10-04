# Epicenter DSP Core — Changelog

All notable changes to the `EpicenterDSPCore` engine during the transition to **Professional 1.0 (Step 1)** are documented in this file.

---

## [1.0.0-pro] - 2026-10-04

### Added
- **Public Stable C API (`include/EpicenterDSP.h`)**:
  - `epicenter_create()`: Opaque handle allocation.
  - `epicenter_destroy()`: Safe teardown.
  - `epicenter_prepare()`: Audio parameter initialization and memory reservation.
  - `epicenter_reset()`: Deterministic filter/envelope zeroing.
  - `epicenter_set_enabled()`, `epicenter_is_enabled()`: Thread-safe atomic activation.
  - `epicenter_set_intensity()`, `epicenter_get_intensity()`: Clamped, sanitized parameter control.
  - `epicenter_process_planar()`: Planar $N$-channel 32-bit floating-point audio processing.
  - `epicenter_process_interleaved()`: In-place interleaved processing using pre-allocated scratch buffers.
  - `epicenter_get_latency()`: Query for algorithmic latency (guaranteed 0 frames).
- **Modern C++ RAII Wrapper (`epicenter::EpicenterDSP`)**:
  - Idiomatic class with move semantics, zero-overhead call delegation, and complete encapsulation of internals.
- **Strict Real-Time Safety Guarantees**:
  - Guaranteed **0 dynamic allocations** (`malloc`, `new`, `std::vector::resize`) inside `process()` or `reset()`.
  - Guaranteed **0 locks, 0 blocking I/O, 0 system calls** on the real-time audio thread.
- **Interleaved Audio Support**:
  - Pre-allocated scratch buffers in `prepare()` allow direct processing of interleaved streams (standard WASAPI, DirectSound, CoreAudio, ALSA) without heap allocations during processing.
- **Immunity & Sanitization for Non-Finite Values**:
  - Inputs with `NaN`, `+Inf`, or `-Inf` are sanitized to clean `0.0f` via `denormalFloor()` and soft clipping, preventing denormal performance stalls and amplifier damage.
  - `setIntensity()` automatically coerces `NaN`, `+Inf`, and `-Inf` to `0.0f`, clamping out-of-range floats to $[0.0, 100.0]$.
- **Deterministic Test & Benchmark Suite (`tests/`)**:
  - 17 automated tests covering initialization, invalid inputs, silence, sub-harmonic generation, variable block sizes, interleaved consistency, bypass bit-accuracy, intensity monotonicity, reset determinism, extreme level bounds, allocation hooks, 3-minute stress test (8.6M frames), and regression comparison against frozen legacy golden files.
  - Allocation tracking harness (`AllocCounter`) verifying zero allocations.

### Changed
- **Algorithm Encapsulation (`src/internal/`)**:
  - Moved internal building blocks (`BiquadFilter`, `LowShelfFilter`, `EnvelopeFollower`, `EpicenterProcessor`) into internal private namespace `epicenter::core_internal`.
  - Replaced dynamic vector expansion `channels_.push_back(...)` in the audio path with fixed-size channel array `channels_[kMaxChannels]`, eliminating the only dynamic allocation hazard in the legacy code.
- **Internal Parameter Consolidation**:
  - Consolidated runtime tuning parameters (`sweepFreq`, `width`, `balance`, `volume`) to factory voicing constants (`sweep=45.0f, width=50.0f, balance=100.0f, volume=100.0f`), matching the exact parameters used across all production hosts (`epicenter-windows` and `epicenter-lab`).
  - The public interface now strictly exposes `intensity` (0.0 to 100.0) and `enabled`, protecting proprietary tuning constants.

### Removed
- **Windows / OS Dependencies in Core**:
  - Removed all Windows-specific types, Win32 headers, and COM definitions from Core.
  - The DSP core is now 100% portable C++17 compliant and compiles on Windows, macOS, Linux, iOS, and Android.
- **Run-time Voicing Exposure**:
  - Removed public exposure of internal filter frequencies, detector Qs, and gain stages.

### Verified
- **Audio Quality Regression**:
  - Automated tests confirm that output from the new core matches the frozen legacy golden reference files with an absolute error difference $< 10^{-6}$ (bit-identical within IEEE float32 precision).

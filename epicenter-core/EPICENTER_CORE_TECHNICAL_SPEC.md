# Epicenter DSP Core — Technical Specification

## 1. Overview & Architecture

**Epicenter DSP Core** is a proprietary, real-time, low-frequency psychoacoustic reconstruction engine written in platform-agnostic C++17. Its primary function is restoring sub-harmonic bass fundamentals lost during recording, mixing, or audio compression (MP3, streaming).

The engine operates on 32-bit floating-point PCM audio (both planar and interleaved) and is designed to integrate into high-performance audio graphs, including **Windows Audio Processing Objects (APO)**, **macOS/iOS AudioUnits**, **Android Oboe/AAudio**, **VST3/CLAP/AU plugins**, and embedded platforms.

```
                    ┌────────────────────────────────────────────────────────┐
                    │                   Audio Input Buffer                   │
                    │               (Planar or Interleaved)                  │
                    └───────────────────────────┬────────────────────────────┘
                                                │
                     ┌──────────────────────────┴───────────────────────────┐
                     │              Bypass / Intensity Check                │
                     │  (If !enabled || intensity <= 0.01: direct bypass)   │
                     └──────────────────────────┬───────────────────────────┘
                                                │ Active Path
                     ┌──────────────────────────┴───────────────────────────┐
                     │               Mono / Side Summation                  │
                     │          mono = (L+R)/2, side = (L-R)/2              │
                     └──────────────────────────┬───────────────────────────┘
                                                │
       ┌────────────────────────────────────────┴────────────────────────────────────────┐
       │                                                                                 │
       ▼                                                                                 ▼
┌──────────────────────────────┐                                           ┌──────────────────────────────┐
│  Multi-band Detector Network │                                           │   Program Audio Separation   │
│  - 60 Hz Bandpass            │                                           │  - Voice Highpass (~105 Hz)  │
│  - 80 Hz Bandpass            │                                           │  - Bass Lowpass (~120 Hz)    │
│  - 110 Hz Bandpass           │                                           │  - Low-Mid Body (~95 Hz)     │
│  - Lowpass & Side HF         │                                           │  - Low-Mid Dip Notch         │
└──────────────┬───────────────┘                                           └──────────────┬───────────────┘
               │                                                                          │
               ▼                                                                          │
┌──────────────────────────────┐                                                          │
│ Sub-Harmonic Octave Divider  │                                                          │
│ - Zero-crossing flip-flop    │                                                          │
│ - Envelope follower (attack/ │                                                          │
│   release shaping)           │                                                          │
│ - Dynamic musical gate       │                                                          │
└──────────────┬───────────────┘                                                          │
               │                                                                          │
               ▼                                                                          │
┌──────────────────────────────┐                                                          │
│ Deep Subsonic Extension Path │                                                          │
│ - Subsonic highpass (23 Hz)  │                                                          │
│ - Lowpass + sustain envelope │                                                          │
└──────────────┬───────────────┘                                                          │
               │                                                                          │
               └────────────────────────┬─────────────────────────────────────────────────┘
                                        │
                                        ▼
                      ┌────────────────────────────────────┐
                      │    Voice-Protected Recombination   │
                      │  Clean Voice + Shaped Bass + Subs  │
                      └─────────────────┬──────────────────┘
                                        │
                                        ▼
                      ┌────────────────────────────────────┐
                      │     Progressive Low-Shelf Boost    │
                      │  (Intensity-scaled shelf at 52 Hz) │
                      └─────────────────┬──────────────────┘
                                        │
                                        ▼
                      ┌────────────────────────────────────┐
                      │    Soft-Clipper & DC Highpass      │
                      │  - Cubic saturation curve          │
                      │  - 26 Hz DC blocking highpass      │
                      │  - Hard bound clamp [-1.0, 1.0]    │
                      └─────────────────┬──────────────────┘
                                        │
                                        ▼
                    ┌──────────────────────────────────────┐
                    │          Audio Output Buffer         │
                    └──────────────────────────────────────┘
```

---

## 2. Public API Specification

The public API is defined in `include/EpicenterDSP.h`. It consists of:
1. **Pure C ABI Interface** for multi-language FFI (C#, Swift, Kotlin, Rust) and binary stability.
2. **Idiomatic C++ RAII Class** (`epicenter::EpicenterDSP`) for modern C++ projects.

### 2.1 C API Functions

```c
EpicenterHandle epicenter_create(void);
void epicenter_destroy(EpicenterHandle handle);
EpicenterResult epicenter_prepare(EpicenterHandle handle, double sampleRate, int channels, size_t maxBlockSize);
void epicenter_reset(EpicenterHandle handle);
void epicenter_set_enabled(EpicenterHandle handle, int enabled);
int epicenter_is_enabled(EpicenterHandle handle);
void epicenter_set_intensity(EpicenterHandle handle, float intensity);
float epicenter_get_intensity(EpicenterHandle handle);
EpicenterResult epicenter_process_planar(EpicenterHandle handle, float* const* channelPointers, int channelCount, size_t numFrames);
EpicenterResult epicenter_process_interleaved(EpicenterHandle handle, float* interleavedBuffer, int channelCount, size_t numFrames);
size_t epicenter_get_latency(EpicenterHandle handle);
```

### 2.2 C++ Interface

```cpp
namespace epicenter {
class EpicenterDSP {
public:
    EpicenterDSP();
    ~EpicenterDSP();
    bool prepare(double sampleRate, int channels, size_t maxBlockSize = 512);
    void reset();
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setIntensity(float intensity);
    float getIntensity() const;
    bool processPlanar(float* const* channels, int channelCount, size_t numFrames);
    bool processInterleaved(float* buffer, int channelCount, size_t numFrames);
    size_t getLatency() const;
};
}
```

---

## 3. Lifecycle & State Machine

```
               ┌────────────────────────┐
               │    epicenter_create    │
               └───────────┬────────────┘
                           │
                           ▼
               ┌────────────────────────┐
        ┌─────►│      UNPREPARED        │
        │      │ (process calls fail)   │
        │      └───────────┬────────────┘
        │                  │
        │                  │ epicenter_prepare()
        │                  ▼
        │      ┌────────────────────────┐
        │      │        PREPARED        │◄────────┐
        │      │  (buffers preallocated)│         │ epicenter_reset()
        │      └───────────┬────────────┘         │
        │                  │                      │
        │                  ▼                      │
        │      ┌────────────────────────┐         │
        │      │       PROCESSING       │─────────┘
        │      │  process_planar /      │
        │      │  process_interleaved   │
        │      └───────────┬────────────┘
        │                  │
        │                  │ epicenter_destroy()
        └──────────────────┼──────────────────────┐
                           ▼                      ▼
               ┌────────────────────────┐   ┌───────────┐
               │   Memory Deallocated   │   │ Destroyed │
               └────────────────────────┘   └───────────┘
```

1. **Instantiation**: `epicenter_create()` initializes atomic parameter flags (`enabled=false`, `intensity=0.0f`).
2. **Configuration**: `epicenter_prepare()` calculates sample-rate-dependent filter coefficients and allocates working buffers (`subBuffer_`, `deepExtensionBuffer_`, `interleaveScratch_`).
3. **Execution**: Audio thread repeatedly invokes `epicenter_process_planar()` or `epicenter_process_interleaved()`.
4. **State Reset**: Calling `epicenter_reset()` zeroes out state history in biquad delay lines and envelope followers without deallocating or reallocating memory.
5. **Teardown**: `epicenter_destroy()` cleans up instance memory.

---

## 4. Threading Model & Real-Time Safety Guarantees

The core enforces a strict lock-free control architecture:

```
[ UI / Control Thread ]                    [ Real-Time Audio Thread ]
          │                                            │
epicenter_set_intensity(val)                           │
epicenter_set_enabled(flag)                            │
          │                                            │
          ▼                                            ▼
┌─────────────────────────┐                ┌─────────────────────────┐
│ std::atomic<float/bool> │ ──────────────►│ Reads atomic params via │
│ (memory_order_release)  │                │ memory_order_acquire    │
└─────────────────────────┘                └───────────┬─────────────┘
                                                       │
                                                       ▼
                                           ┌─────────────────────────┐
                                           │ Zero Allocations (Heap) │
                                           │ Zero Locks / Mutexes    │
                                           │ Zero File / System I/O  │
                                           │ Fixed Maximum Execution │
                                           └─────────────────────────┘
```

### Guarantees
- **No Dynamic Memory Allocation**: All vector buffers are sized during `prepare()`. Neither `process()` nor `reset()` calls `malloc`, `free`, `new`, or `delete`.
- **Non-blocking Execution**: Parameter updates use standard C++11 atomics (`std::memory_order_acquire` / `release`). There are no `std::mutex`, spinlocks, or OS synchronization primitives in the audio path.
- **Priority Inversion Free**: Because no locks are acquired, the real-time thread cannot be preempted by low-priority UI or background tasks.

---

## 5. Parameter Specifications

| Parameter | Type | Valid Range | Default | Out-of-bounds Handling | Description |
|---|---|---|---|---|---|
| `enabled` | `int` / `bool` | `0` (false), `1` (true) | `0` (Bypass) | Non-zero treated as true | Master bypass switch |
| `intensity` | `float` | `0.0` .. `100.0` | `0.0` | `<0 -> 0`, `>100 -> 100`, `NaN/Inf -> 0` | Master sub-harmonic reconstruction drive |

---

## 6. Sample Rate & Channel Support

- **Sample Rates**: Formally tested and validated for **44.1 kHz**, **48 kHz**, and **96 kHz** (supports any rate from 1 kHz to 384 kHz). All biquads, shelving filters, and envelope followers are normalized using standard bilinear transform and continuous-time pole-zero mapping:
  $$\omega_0 = 2\pi \frac{f_c}{f_s}$$
- **Channels**:
  - `Mono (1 channel)`: Directly processed through mono detector and channel 0 filter bank.
  - `Stereo (2 channels)`: Sum and difference matrixing drives mono sub synthesis; left and right channels maintain independent spatial voice and low-mid imaging.
  - *Future Expansion*: Multichannel (5.1, 7.1.4 Dolby Atmos) planned for Version 2.0 via multi-instance routing.

---

## 7. Latency Characteristics

- **Algorithmic Latency**: **0 samples (0.0 ms)**.
- **Filter Topology**: All filters (Butterworth biquads, peaking/notch, low-shelf) and envelope followers are purely causal, recursive, minimum-phase IIR structures.
- **Lookahead**: The engine uses instantaneous zero-crossing tracking and causal envelope release, requiring zero lookahead buffer delay.

---

## 8. Memory Footprint

| Component | Size (bytes) | Allocation Timing |
|---|---|---|
| `EpicenterDSPState` handle | ~128 bytes | At `epicenter_create()` |
| `EpicenterProcessor` state | ~3,200 bytes | Inside handle |
| Working buffers (512-frame block) | ~8,192 bytes | At `epicenter_prepare()` |
| **Total Memory per Instance** | **~12 KB** | **Static after prepare()** |

---

## 9. Verification & Quality Assurance

The Core test suite (`epicenter_test_suite`) enforces 17 automated tests:
1. `init_valid_configurations`: Validates 44.1k, 48k, 96k, mono, stereo, 32..1024 block sizes.
2. `init_invalid_configurations`: Confirms graceful rejection of negative rates, 0 block size, unsupported channels.
3. `init_unprepared_call_safety`: Ensures calling `process` without `prepare` returns error without memory corruption.
4. `process_silence`: Validates zero DC drift or denormal generation on digital silence.
5. `process_subharmonic_generation`: Validates sub-harmonic generation on 80 Hz sine tones.
6. `process_variable_block_sizes`: Validates robustness across variable, prime, and irregular block lengths.
7. `process_interleaved_vs_planar`: Verifies bit-exact mathematical consistency between planar and interleaved processing.
8. `bypass_when_disabled`: Verifies 100% bit-accurate pass-through when `enabled=false`.
9. `bypass_when_intensity_zero`: Verifies transparent bypass when `intensity=0.0`.
10. `intensity_bounds_and_sanitization`: Verifies clamping and protection against `NaN`, `+Inf`, and `-Inf`.
11. `intensity_monotonic_boost`: Confirms sub-bass energy increases monotonically with intensity.
12. `reset_deterministic_identity`: Confirms `reset()` deterministically restores state to match a newly initialized instance.
13. `safety_nan_and_inf_inputs`: Verifies that corrupted audio buffers (NaN, Inf) are sanitized without outputting non-finite values.
14. `safety_extreme_input_levels`: Verifies hard output bounds $[-1.0, 1.0]$ when driven with $+34\text{ dBFS}$ inputs.
15. `allocations_zero_in_realtime_path`: Proves per-thread dynamic memory allocations are strictly 0 in `processPlanar()`, `processInterleaved()`, `reset()`, and `setIntensity()`.
16. `stability_long_running_stress`: Simulates 3 continuous minutes of audio (8.64 million frames) with dynamic parameter sweeps.
17. `regression_compare_against_legacy_golden`: Proves bit-level output equivalence ($< 10^{-6}$ max error) against the frozen legacy prototype across all reference fixtures.

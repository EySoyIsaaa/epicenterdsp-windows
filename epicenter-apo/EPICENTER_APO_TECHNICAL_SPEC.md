# Epicenter DSP — Windows Audio Processing Object (APO) Technical Specification
**Document Version:** 1.0.0  
**Target:** Windows 10 & Windows 11 (64-bit x64 / ARM64)  
**Classification:** Core System Architecture

---

## 1. Executive Summary & Objective

The objective of **Paso 2** is eliminating the need for virtual audio cables (VB-CABLE) by implementing a native **Windows Audio Processing Object (APO)**. This enables **Epicenter DSP Professional** to insert its sub-harmonic reconstruction engine directly into the Windows audio graph, applying the effect to all shared audio playback with zero added device latency and zero external dependencies.

---

## 2. Windows Audio Pipeline & APO Placement

Windows audio architecture divides stream processing into three stages:
1. **SFX (Stream Effects)**: Pre-mix effects applied to individual application streams.
2. **MFX (Mode Effects)**: Applied to stream categories (e.g., Media, Communications).
3. **EFX (Endpoint Effects)**: Post-mix effects applied to the master mix immediately before reaching the hardware audio endpoint (DAC / sound card / speakers / headphones).

```text
[ Application Audio Streams ] (Spotify, YouTube, DAW, Media Player)
              │
              ▼
   [ Windows Audio Engine Mixer ] (Audio Session Graph)
              │
              ▼ (Master Mixed 32-bit Float PCM Stream)
┌─────────────────────────────────────────────────────────────┐
│  audiodg.exe                                                │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │           Epicenter DSP APO (Endpoint Effect - EFX) │   │
│   │                                                     │   │
│   │  Shared Memory IPC ──► [ Clickless A/B Crossfader ] │   │
│   │                                 │                   │   │
│   │                                 ▼                   │   │
│   │                 [ EpicenterDSPCore (C ABI) ]        │   │
│   │                                                     │   │
│   └─────────────────────────┬───────────────────────────┘   │
└─────────────────────────────┼───────────────────────────────┘
                              ▼
                [ Hardware Audio Endpoint / DAC ]
```

### Exact Insertion Point:
**Endpoint Effect (EFX)**, registered on the active playback device via `PKEY_FX_EndpointEffectClsid` (`{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D}, 7`).  
For older OS builds (Windows 7/8/8.1), fallback registration uses `PKEY_FX_PostMixEffectClsid` (`{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D}, 2`).

---

## 3. Required COM Interfaces & Inheritance

The APO is packaged as a standard in-process COM Dynamic Link Library (`EpicenterAPO.dll`). The primary class `CEpicenterApo` implements the following standard Windows Audio SDK interfaces:

```
                          ┌────────────────────────┐
                          │        IUnknown        │
                          └───────────┬────────────┘
                                      │
       ┌──────────────────────────────┼──────────────────────────────┐
       │                              │                              │
       ▼                              ▼                              ▼
┌─────────────────────────┐ ┌─────────────────────────┐ ┌─────────────────────────┐
│  IAudioProcessingObject │ │ IAudioProcessingObjectRT│ │  IAudioProcessingObject │
│                         │ │                         │ │      Configuration      │
└─────────────────────────┘ └─────────────────────────┘ └─────────────────────────┘
       │                              │                              │
       └──────────────────────────────┼──────────────────────────────┘
                                      │
                                      ▼
                        ┌───────────────────────────┐
                        │   IAudioSystemEffects2    │
                        └─────────────┬─────────────┘
                                      │
                                      ▼
                        ┌───────────────────────────┐
                        │       CEpicenterApo       │
                        └───────────────────────────┘
```

### 3.1 Interface Responsibilities:
1. **`IAudioProcessingObject`**:
   - `Initialize()`: Initializes the APO with endpoint property store data.
   - `GetRegistrationProperties()`: Returns APO metadata, flags (`APO_FLAG_INPLACE`, `APO_FLAG_SAMPLECOUNT_FLOAT32`), and supported formats.
   - `IsInputFormatSupported()` / `IsOutputFormatSupported()`: Format negotiation.
   - `GetLatency()`: Reports algorithmic latency (0 samples / 0 hnstimes).
   - `Reset()`: Clears DSP state without deallocation.
2. **`IAudioProcessingObjectConfiguration`**:
   - `LockForProcess()`: Validates input/output format descriptors, prepares `EpicenterDSP` core buffers, connects to IPC shared memory. Non-realtime; dynamic memory allocation is allowed here.
   - `UnlockForProcess()`: Releases processing buffers and closes temporary resources when playback stops.
3. **`IAudioProcessingObjectRT`**:
   - `APOProcess()`: Real-time audio callback invoked by `audiodg.exe`. **ZERO allocations, ZERO locks, ZERO blocking I/O.**
   - `CalcInputFrames()` / `CalcOutputFrames()`: Returns $1:1$ frame relationship.
4. **`IAudioSystemEffects2`**:
   - `GetEffectsList()`: Queries active system effects for endpoint management.

---

## 4. APO Lifecycle & State Machine

```text
                  ┌──────────────────────────────┐
                  │ DllGetClassObject / CoCreate │
                  └──────────────┬───────────────┘
                                 │
                                 ▼
                  ┌──────────────────────────────┐
                  │          INITIALIZED         │
                  │   IAudioProcessingObject::   │
                  │         Initialize()         │
                  └──────────────┬───────────────┘
                                 │
                                 ▼
                  ┌──────────────────────────────┐
                  │       FORMAT NEGOTIATION     │
                  │   IsInput/OutputFormat...    │
                  └──────────────┬───────────────┘
                                 │
                                 ▼
                  ┌──────────────────────────────┐
                  │        LOCKED / ARMED        │
                  │   LockForProcess() called:   │
                  │   - epicenter_prepare()      │
                  │   - Open Shared Memory IPC   │
                  │   - Pre-allocate buffers     │
                  └──────────────┬───────────────┘
                                 │
                                 ▼
┌─────────────────────────► PROCESSING ◄─────────────────────────┐
│                     (Real-Time Thread)                         │
│  APOProcess():                                                 │
│  - Non-blocking SeqLock read of IPC shared memory              │
│  - If bypassed/disabled: pass through input to output          │
│  - If active: epicenter_process_interleaved()                  │
│  - Clickless A/B crossfade ramp                                │
│  - Non-blocking write of peak, RMS, CPU execution time         │
└────────────────────────────────┬───────────────────────────────┘
                                 │
                 Format Change / │ Endpoint Stop
                 Device Switch   │
                                 ▼
                  ┌──────────────────────────────┐
                  │      UnlockForProcess()      │
                  │      epicenter_reset()       │
                  └──────────────┬───────────────┘
                                 │
                                 ▼
                  ┌──────────────────────────────┐
                  │          DESTROYED           │
                  │    Release() -> destructor   │
                  │    epicenter_destroy()       │
                  └──────────────────────────────┘
```

---

## 5. Audio Format Negotiation & Fail-Safe Behavior

Windows audio graphs operate natively in IEEE 32-bit floating point (`WAVE_FORMAT_IEEE_FLOAT` / `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`).

### 5.1 Format Support Matrix
* **Sample Rates**: 44.1 kHz, 48.0 kHz, 96.0 kHz (native); any sample rate 1000 Hz to 192000 Hz supported via bilinear filter coefficient re-calculation in `prepare()`.
* **Bit Depth**: 32-bit floating point (standard for Windows APO pipelines).
* **Channel Configurations**:
  - `Mono (1 channel)`: Supported.
  - `Stereo (2 channels)`: Supported (Standard endpoint render format).
  - `Multichannel (5.1 / 7.1)`: When an unsupported channel count is connected, the APO does **NOT** fail the audio graph. Instead, it enters **transparent pass-through bypass mode**, copying input to output unharmed.

---

## 6. Clickless Real-Time A/B Crossfade Architecture

To fulfill Section 8 of the requirements without touching the DSP Core, the APO incorporates an internal clickless crossfader:

```text
Input Audio ────────┬────────────────────────────────────────── (Dry Path)
                    │
                    ▼
          [ EpicenterDSP Core ] ────────────────────────────── (Wet Path)
                    │                                                  │
                    ▼                                                  ▼
          ┌──────────────────────────────────────────────────────────────┐
          │                  Clickless Crossfader                        │
          │  y[n] = (1.0 - alpha[n]) * dry[n] + alpha[n] * wet[n]        │
          └──────────────────────────────┬───────────────────────────────┘
                                         │
                                         ▼
                                   Output Audio
```

* **Transition Duration**: $8.0\text{ ms}$ ($\approx 384\text{ frames}$ at $48\text{ kHz}$).
* **Ramp Shape**: Linear or equal-power per-sample ramp:
  $$\Delta\alpha = \frac{1.0}{\text{rampFrames}}$$
* **Real-time Safety**: 0 allocations, 0 branches in inner loop, completely pop-free and click-free.

---

## 7. Registration & Windows Endpoint Binding

### 7.1 COM Registration Keys
`EpicenterAPO.dll` registers under `HKCR\CLSID\{68C32E69-906A-4A0E-9214-41A832A88877}` with threading model `Both`.

### 7.2 Audio Endpoint Binding Keys
Under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{Endpoint-GUID}\FxProperties`:
* `PKEY_FX_EndpointEffectClsid`: `{68C32E69-906A-4A0E-9214-41A832A88877}`
* `PKEY_FX_FriendlyName`: `"Epicenter DSP Professional APO"`
* `PKEY_FX_Association`: `KSAUDIO_EFFECT_ENDPOINT_EFFECT`

---

## 8. Fail-Safe & Crash-Resilience Summary

1. **Host Isolation**: Because the APO executes inside `audiodg.exe`, any memory fault would terminate the Windows audio service. Therefore, all code paths in `APOProcess()` are wrapped in structured SEH exception handling (`__try / __except`) or defensive pointer bounds checking.
2. **Safe Bypass on Error**: If any exception or format mismatch occurs, the APO copies input to output (`memcpy` or zero-cost in-place pass-through) and marks the output buffer valid. **Windows audio never drops or halts.**
3. **Absence of UI**: If the UI is closed, the APO defaults to `enabled=0, intensity=0` (bypass).

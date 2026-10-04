# Epicenter DSP — APO Inter-Process Communication (IPC) Specification
**Document Version:** 1.0.0  
**Target:** Windows Audio Device Graph (`audiodg.exe`) <-> Professional UI & Host Services  
**Classification:** Core System Architecture

---

## 1. Overview & Threat Model

In Windows 10 and 11, the Windows Audio Processing Object (APO) executes inside `audiodg.exe` (Windows Audio Device Graph Isolation). This process runs in **Session 0** under the `NT AUTHORITY\LOCAL SERVICE` account in a restricted security token.

In contrast, the user interface (Epicenter Professional) executes in an interactive user session (**Session 1+**) under standard user credentials (Medium Integrity Level).

```
┌───────────────────────────────────────────────┐
│     Interactive User Session (Session 1)      │
│  Epicenter Professional UI (Medium Integrity) │
└───────────────────────┬───────────────────────┘
                        │
                        │ Reads / Writes Shared State
                        ▼
┌───────────────────────────────────────────────┐
│  Named Shared Memory:                         │
│  Global\EpicenterDspIpcSharedMemory           │
│  Security: Custom DACL (LocalService + Users) │
└───────────────────────▲───────────────────────┘
                        │
                        │ Reads Controls / Writes Telemetry
┌───────────────────────┴───────────────────────┐
│          Session 0 (Isolated Audio)           │
│   audiodg.exe (NT AUTHORITY\LOCAL SERVICE)    │
│   EpicenterAPO.dll (Real-Time Audio Thread)   │
└───────────────────────────────────────────────┘
```

### Critical Requirements:
1. **Real-time Non-blocking Guarantee**: The audio thread inside `audiodg.exe` must **NEVER** wait for a lock, sleep, perform I/O, or block on the UI process.
2. **Crash & Absence Resilience**: If the UI crashes, terminates, or has never been launched, the APO must automatically default to a safe, transparent bypass state with zero audio glitching.
3. **Session & Permission Boundary**: The shared memory segment must reside in the `Global\` namespace with a strict Discretionary Access Control List (DACL) permitting `LOCAL SERVICE`, `ALL APPLICATION PACKAGES`, and `INTERACTIVE` users.
4. **Data Integrity & Consistency**: Multi-field updates (such as simultaneously switching intensity and bypass mode) must be tear-free without using blocking mutexes in the audio path.

---

## 2. IPC Transport Architecture

The transport mechanism is **Win32 Named Shared Memory (Memory-Mapped File backed by the system paging file)**.

* **Kernel Object Name**: `Global\EpicenterDspIpcSharedMemory`
* **Allocation Size**: 4096 bytes (1 memory page)
* **Access Mode**: Read/Write for both ends

### 2.1 Security Descriptor & DACL
To allow cross-session access between `audiodg.exe` (`LOCAL SERVICE`) and the user session, the creator creates the object with an explicit Security Descriptor String (SDDL):

```text
D:(A;;GA;;;LS)(A;;GA;;;BA)(A;;GRGW;;;IU)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)
```

Where:
* `(A;;GA;;;LS)`: Allow Generic All to **Local Service** (`audiodg.exe`).
* `(A;;GA;;;BA)`: Allow Generic All to **Built-in Administrators**.
* `(A;;GRGW;;;IU)`: Allow Generic Read/Write to **Interactive Users**.
* `(A;;GRGW;;;AC)`: Allow Generic Read/Write to **All Application Packages** (AppContainer).
* `S:(ML;;NW;;;LW)`: Mandatory Label: No-Write-Up policy with **Low Integrity** (`LW`), allowing processes running at Low Integrity or AppContainer to write to the state if needed.

---

## 3. Memory Layout & Protocol Definition

The shared memory buffer is formatted as a single C-compatible, cache-line-aligned structure:

```c
#pragma pack(push, 8)

#define EPICENTER_IPC_MAGIC        0x45504943  /* 'EPIC' */
#define EPICENTER_IPC_VERSION      1           /* Protocol version 1 */

typedef struct EpicenterControlBlock {
    /* Sequence counter for lock-free multi-field atomic updates (SeqLock).
     * Odd value = UI is writing; Even value = state is stable and consistent. */
    volatile uint32_t sequence;

    /* Control parameters */
    volatile uint32_t enabled;         /* 1 = Active, 0 = Bypass */
    volatile float    intensity;       /* 0.0f to 100.0f */
    volatile uint32_t abState;         /* 0 = A (Original Bypass), 1 = B (Epicenter Active) */
    volatile uint32_t targetPreset;    /* 0=Default, 1=Deep, 2=Pro, 3=Custom */

    /* Reserved for future control parameters without breaking layout */
    uint32_t          reserved[8];
} EpicenterControlBlock;

typedef struct EpicenterTelemetryBlock {
    volatile uint32_t apoHeartbeat;      /* Incremented by APO on every process quantum */
    volatile uint32_t sampleRate;        /* e.g., 44100, 48000, 96000 */
    volatile uint32_t channelCount;      /* 1 or 2 */
    volatile uint32_t bufferFrameCount;  /* Quantum size (e.g., 480, 512, 1024) */
    volatile float    dspTimeMicroseconds; /* Processing execution time in µs */
    volatile float    peakDbfs;          /* Output peak level in dBFS */
    volatile float    rmsDbfs;           /* Output RMS level in dBFS */
    volatile uint32_t underruns;         /* Counter of dropped/corrupted frames */
    volatile uint32_t apoStatus;         /* 0=Inactive, 1=Active, 2=ErrorBypass */

    /* Reserved for future telemetry metrics */
    uint32_t          reserved[7];
} EpicenterTelemetryBlock;

typedef struct EpicenterIpcSharedState {
    /* Header & Versioning */
    uint32_t magic;                      /* EPICENTER_IPC_MAGIC */
    uint32_t protocolVersion;            /* EPICENTER_IPC_VERSION */
    uint32_t structSize;                 /* sizeof(EpicenterIpcSharedState) */
    uint32_t initFlags;                  /* Initialization status flags */

    /* Heartbeat of UI */
    volatile uint64_t uiLastHeartbeatQpc;/* QueryPerformanceCounter timestamp from UI */
    volatile uint32_t uiProcessId;       /* PID of running UI */

    /* Sub-blocks */
    EpicenterControlBlock   control;
    EpicenterTelemetryBlock telemetry;

    /* Padding to 4096-byte page boundary */
    uint8_t  padding[4096 - 40 - sizeof(EpicenterControlBlock) - sizeof(EpicenterTelemetryBlock)];
} EpicenterIpcSharedState;

#pragma pack(pop)
```

---

## 4. Concurrency & Atomicity Analysis

### 4.1 Memory Model & Aligned Atomics
On Windows x86-64:
- Naturally aligned 32-bit and 64-bit memory reads and writes are guaranteed atomic by the hardware CPU architecture (bus locking / L1 cache coherency protocol).
- Single parameter reads like `intensity` or `enabled` do not tear.

### 4.2 Multi-field Consistency via Lock-Free Sequence Lock (SeqLock)
When the user toggles an A/B switch or selects a preset, multiple fields change simultaneously (`enabled`, `intensity`, `abState`). To prevent the real-time audio thread from observing a half-written intermediate state, a **SeqLock** is employed:

#### UI (Writer) Protocol:
```c
void writeControlState(EpicenterIpcSharedState* ipc, uint32_t enabled, float intensity, uint32_t abState) {
    // 1. Increment sequence to ODD (indicates write in progress)
    uint32_t seq = ipc->control.sequence;
    ipc->control.sequence = seq + 1;
    _WriteBarrier(); // Full compiler/CPU write barrier

    // 2. Write control fields
    ipc->control.enabled = enabled;
    ipc->control.intensity = intensity;
    ipc->control.abState = abState;

    _WriteBarrier();
    // 3. Increment sequence to EVEN (indicates write complete)
    ipc->control.sequence = seq + 2;
}
```

#### APO (Reader) Protocol on Real-Time Thread:
```c
bool readControlState(const EpicenterIpcSharedState* ipc, uint32_t* outEnabled, float* outIntensity, uint32_t* outAbState) {
    uint32_t seq1 = ipc->control.sequence;
    if (seq1 & 1) {
        // Writer is currently updating; do not spin! Return false to keep previous valid state.
        return false;
    }
    _ReadBarrier();

    uint32_t en = ipc->control.enabled;
    float inten = ipc->control.intensity;
    uint32_t ab = ipc->control.abState;

    _ReadBarrier();
    uint32_t seq2 = ipc->control.sequence;
    if (seq1 != seq2) {
        // State was modified during read; return false to keep previous valid state.
        return false;
    }

    // Sanitize values
    if (std::isnan(inten) || std::isinf(inten)) inten = 0.0f;
    *outIntensity = std::max(0.0f, std::min(inten, 100.0f));
    *outEnabled = (en != 0);
    *outAbState = ab;
    return true;
}
```

**Key Real-time Invariant**: The reader **NEVER SPINS**. If a sequence collision occurs, it simply keeps its current cached parameters for that 10ms audio quantum and re-reads on the next quantum. This eliminates priority inversion and unbounded latency.

---

## 5. Fail-Safe Lifecycle Matrix

| Scenario | IPC Shared Memory State | APO Real-Time Audio Behaviour |
|---|---|---|
| **UI Closed / Never Launched** | Mapping not opened or magic != 'EPIC' | Defaults to `enabled=0, intensity=0`. Bit-accurate pass-through. Zero DSP load. |
| **UI Crashed Unexpectedly** | Stale mapping, heartbeat stops incrementing | APO continues running smoothly with last valid parameters or smoothly fades to bypass. |
| **UI Spams Invalid Data (NaN/Inf)** | Memory contains NaN or >100.0f | Sanitizer clamps intensity to safe range [0.0, 100.0]. Output never becomes unstable. |
| **`audiodg.exe` Restarted by OS** | Shared memory is re-opened by newly spawned APO | Reads current UI parameters immediately upon `LockForProcess()`. Audio resumes seamlessly. |
| **Multiple User Sessions** | Object created in `Global\` namespace | Accessible across Fast User Switching and Session 0. |

---

## 6. Verification Plan

1. **Permissions Test**: Verify non-elevated user process and `LOCAL SERVICE` test process can concurrently open the mapping.
2. **Tear-Free Concurrency Test**: High-frequency writer updating `enabled` and `intensity` at 100 kHz while real-time reader checks that parameters are never read out-of-sync.
3. **Absence Test**: Verify APO executes with zero errors when shared memory is non-existent.
4. **Fuzzing & Sanitization Test**: Inject random bytes, NaN, -Inf, huge floats into shared memory; verify APO output remains bounded and finite.

#pragma once

/**
 * @file EpicenterApoIpc.h
 * @brief Inter-Process Communication protocol structures and lock-free SeqLock helpers
 *        for communication between audiodg.exe and the user application.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#endif

#define EPICENTER_IPC_SHARED_MEMORY_NAME    L"Global\\EpicenterDspIpcSharedMemory"
#define EPICENTER_IPC_LOCAL_FALLBACK_NAME   L"Local\\EpicenterDspIpcSharedMemory"
#define EPICENTER_IPC_MAGIC                 0x45504943u  /* 'EPIC' in ASCII */
#define EPICENTER_IPC_VERSION               1u
#define EPICENTER_IPC_PAGE_SIZE             4096u

#pragma pack(push, 8)

typedef struct EpicenterControlBlock {
    volatile uint32_t sequence;        /* Sequence counter for lock-free updates (odd=writing, even=stable) */
    volatile uint32_t enabled;         /* 1 = Active, 0 = Bypass */
    volatile float    intensity;       /* 0.0f to 100.0f */
    volatile uint32_t abState;         /* 0 = A (Original Bypass), 1 = B (Epicenter Active) */
    volatile uint32_t targetPreset;    /* 0=Default, 1=Deep, 2=Pro, 3=Custom */
    uint32_t          reserved[8];     /* Future expansion */
} EpicenterControlBlock;

typedef struct EpicenterTelemetryBlock {
    volatile uint32_t apoHeartbeat;      /* Incremented by APO on every process block */
    volatile uint32_t sampleRate;        /* Active sample rate */
    volatile uint32_t channelCount;      /* Active channel count (1 or 2) */
    volatile uint32_t bufferFrameCount;  /* Active quantum buffer frame count */
    volatile float    dspTimeMicroseconds; /* Execution time of DSP in microseconds */
    volatile float    peakDbfs;          /* Output block peak level in dBFS */
    volatile float    rmsDbfs;           /* Output block RMS level in dBFS */
    volatile uint32_t underruns;         /* Buffer underruns or discontinuities detected */
    volatile uint32_t apoStatus;         /* 0=Inactive, 1=Active, 2=ErrorBypass */
    uint32_t          reserved[7];       /* Future telemetry */
} EpicenterTelemetryBlock;

typedef struct EpicenterIpcSharedState {
    uint32_t magic;                      /* EPICENTER_IPC_MAGIC */
    uint32_t protocolVersion;            /* EPICENTER_IPC_VERSION */
    uint32_t structSize;                 /* sizeof(EpicenterIpcSharedState) */
    uint32_t initFlags;                  /* 1 if initialized */

    volatile uint64_t uiLastHeartbeatQpc;/* QPC timestamp from UI */
    volatile uint32_t uiProcessId;       /* Process ID of UI host */
    uint32_t          headerReserved;

    EpicenterControlBlock   control;
    EpicenterTelemetryBlock telemetry;

    uint8_t  padding[EPICENTER_IPC_PAGE_SIZE - 40 - sizeof(EpicenterControlBlock) - sizeof(EpicenterTelemetryBlock)];
} EpicenterIpcSharedState;

#pragma pack(pop)

#ifdef __cplusplus
namespace epicenter {
namespace ipc {

inline void initSharedState(EpicenterIpcSharedState* s) {
    if (!s) return;
    std::memset(s, 0, sizeof(EpicenterIpcSharedState));
    s->magic = EPICENTER_IPC_MAGIC;
    s->protocolVersion = EPICENTER_IPC_VERSION;
    s->structSize = sizeof(EpicenterIpcSharedState);
    s->initFlags = 1;
    s->control.sequence = 0;
    s->control.enabled = 0;   // Default safe state: bypass
    s->control.intensity = 0.0f;
    s->control.abState = 1;   // Default B (when enabled, plays Epicenter)
    s->control.targetPreset = 0;
    s->telemetry.apoStatus = 1; // Active / Armed
}

/**
 * Writer function used by the UI thread to update control parameters.
 * Uses memory barriers to maintain SeqLock consistency.
 */
inline void writeControlState(EpicenterIpcSharedState* ipc, uint32_t enabled, float intensity, uint32_t abState, uint32_t preset = 0) {
    if (!ipc || ipc->magic != EPICENTER_IPC_MAGIC) return;

    // 1. Increment sequence to odd (writing in progress)
    uint32_t seq = ipc->control.sequence;
    ipc->control.sequence = seq + 1;
#if defined(_WIN32)
    MemoryBarrier();
#endif

    // 2. Sanitize and update fields
    float safeIntensity = intensity;
    if (!std::isfinite(safeIntensity) || safeIntensity < 0.0f) {
        safeIntensity = 0.0f;
    } else if (safeIntensity > 100.0f) {
        safeIntensity = 100.0f;
    }

    ipc->control.enabled = enabled ? 1 : 0;
    ipc->control.intensity = safeIntensity;
    ipc->control.abState = abState;
    ipc->control.targetPreset = preset;

#if defined(_WIN32)
    MemoryBarrier();
#endif

    // 3. Increment sequence to even (stable)
    ipc->control.sequence = seq + 2;
}

/**
 * Real-time reader function used inside APOProcess() on the audio thread.
 * Guaranteed NON-BLOCKING: never spins, never waits.
 * Returns true if state was read consistently, false if writer was in the middle of an update.
 */
inline bool readControlState(const EpicenterIpcSharedState* ipc, uint32_t* outEnabled, float* outIntensity, uint32_t* outAbState) {
    if (!ipc || ipc->magic != EPICENTER_IPC_MAGIC) {
        if (outEnabled) *outEnabled = 0;
        if (outIntensity) *outIntensity = 0.0f;
        if (outAbState) *outAbState = 0;
        return false;
    }

    uint32_t seq1 = ipc->control.sequence;
    if (seq1 & 1u) {
        // Writer currently modifying state. Do NOT spin on real-time thread!
        return false;
    }
#if defined(_WIN32)
    MemoryBarrier();
#endif

    uint32_t en = ipc->control.enabled;
    float inten = ipc->control.intensity;
    uint32_t ab = ipc->control.abState;

#if defined(_WIN32)
    MemoryBarrier();
#endif
    uint32_t seq2 = ipc->control.sequence;
    if (seq1 != seq2) {
        // State was modified during read; reject read and keep previous cached values.
        return false;
    }

    // Sanitize values against corruption or bad input
    if (!std::isfinite(inten) || inten < 0.0f) {
        inten = 0.0f;
    } else if (inten > 100.0f) {
        inten = 100.0f;
    }

    if (outEnabled) *outEnabled = (en != 0);
    if (outIntensity) *outIntensity = inten;
    if (outAbState) *outAbState = ab;
    return true;
}

/**
 * Real-time telemetry write function called by APOProcess().
 * Lock-free atomic writes without blocking.
 */
inline void writeTelemetry(EpicenterIpcSharedState* ipc, uint32_t sampleRate, uint32_t channels,
                           uint32_t frames, float dspTimeUs, float peakDbfs, float rmsDbfs, uint32_t status) {
    if (!ipc || ipc->magic != EPICENTER_IPC_MAGIC) return;

    ipc->telemetry.sampleRate = sampleRate;
    ipc->telemetry.channelCount = channels;
    ipc->telemetry.bufferFrameCount = frames;
    ipc->telemetry.dspTimeMicroseconds = dspTimeUs;
    ipc->telemetry.peakDbfs = peakDbfs;
    ipc->telemetry.rmsDbfs = rmsDbfs;
    ipc->telemetry.apoStatus = status;
    ipc->telemetry.apoHeartbeat++;
}

} // namespace ipc
} // namespace epicenter
#endif // __cplusplus

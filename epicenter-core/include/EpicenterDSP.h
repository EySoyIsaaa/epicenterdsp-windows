#pragma once

/**
 * @file EpicenterDSP.h
 * @brief Epicenter DSP Professional 1.0 — Public C and C++ API
 *
 * Platform-agnostic, deterministic, real-time safe audio DSP core for low-frequency
 * reconstruction and sub-harmonic synthesis.
 *
 * Real-time safety guarantees:
 * - After prepare(), process() NEVER performs dynamic memory allocation (no malloc/new/delete).
 * - process() NEVER blocks on mutexes, file I/O, network, or OS calls.
 * - Thread-safe control: parameters (enabled, intensity) can be modified concurrently from
 *   a control/UI thread via lock-free atomics without interrupting the audio thread.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(EPICENTER_CORE_EXPORTS)
    #define EPICENTER_API __declspec(dllexport)
  #elif defined(EPICENTER_CORE_STATIC)
    #define EPICENTER_API
  #else
    #define EPICENTER_API
  #endif
#else
  #if defined(EPICENTER_CORE_EXPORTS)
    #define EPICENTER_API __attribute__((visibility("default")))
  #else
    #define EPICENTER_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle to an Epicenter DSP instance */
typedef struct EpicenterDSPState* EpicenterHandle;

/** Status codes */
typedef enum EpicenterResult {
    EPICENTER_OK = 0,
    EPICENTER_ERROR_INVALID_ARGUMENT = -1,
    EPICENTER_ERROR_NOT_INITIALIZED = -2,
    EPICENTER_ERROR_OUT_OF_MEMORY = -3,
    EPICENTER_ERROR_UNSUPPORTED_CONFIG = -4
} EpicenterResult;

/**
 * Creates a new Epicenter DSP instance.
 * @return Handle to instance, or NULL if memory allocation fails.
 */
EPICENTER_API EpicenterHandle epicenter_create(void);

/**
 * Destroys an Epicenter DSP instance and frees all associated memory.
 * @param handle DSP handle to destroy. Safe to pass NULL.
 */
EPICENTER_API void epicenter_destroy(EpicenterHandle handle);

/**
 * Prepares the DSP for audio playback.
 * All working memory is allocated inside this function.
 * Must NOT be called from the real-time audio thread.
 *
 * @param handle Valid DSP handle.
 * @param sampleRate Audio sample rate in Hz (e.g. 44100, 48000, 96000).
 * @param channels Number of channels (1 for Mono, 2 for Stereo).
 * @param maxBlockSize Maximum frames expected in a single process() call (e.g. 32 to 8192).
 * @return EPICENTER_OK on success, error code otherwise.
 */
EPICENTER_API EpicenterResult epicenter_prepare(EpicenterHandle handle, double sampleRate, int channels, size_t maxBlockSize);

/**
 * Clears all internal filter states, delay lines, and envelopes to silence.
 * Real-time safe (zero allocation, zero blocking).
 * @param handle Valid DSP handle.
 */
EPICENTER_API void epicenter_reset(EpicenterHandle handle);

/**
 * Enables or bypasses the DSP effect.
 * Thread-safe and lock-free; safe to call from any thread at any time.
 * When disabled, process() passes input through cleanly with zero DSP latency.
 *
 * @param handle Valid DSP handle.
 * @param enabled 1 to enable DSP processing, 0 for bypass.
 */
EPICENTER_API void epicenter_set_enabled(EpicenterHandle handle, int enabled);

/**
 * Queries whether the DSP effect is currently enabled.
 * @param handle Valid DSP handle.
 * @return 1 if enabled, 0 if bypassed.
 */
EPICENTER_API int epicenter_is_enabled(EpicenterHandle handle);

/**
 * Sets the effect intensity.
 * Thread-safe and lock-free; safe to call from any thread at any time.
 * Value is automatically clamped to [0.0, 100.0].
 * Non-finite values (NaN, Inf) are sanitized to default safe value (0.0).
 *
 * @param handle Valid DSP handle.
 * @param intensity Effect intensity from 0.0 (bypass threshold) to 100.0 (maximum).
 */
EPICENTER_API void epicenter_set_intensity(EpicenterHandle handle, float intensity);

/**
 * Queries the current intensity value.
 * @param handle Valid DSP handle.
 * @return Intensity value in range [0.0, 100.0].
 */
EPICENTER_API float epicenter_get_intensity(EpicenterHandle handle);

/**
 * Processes planar (de-interleaved) 32-bit floating point audio in place.
 * Real-time safe: zero dynamic allocations, zero locks, zero I/O.
 *
 * @param handle Valid DSP handle.
 * @param channelPointers Array of pointers to channel buffers: channels[ch][frame].
 * @param channelCount Number of channel pointers provided (must match prepared channels).
 * @param numFrames Number of audio frames to process.
 * @return EPICENTER_OK on success.
 */
EPICENTER_API EpicenterResult epicenter_process_planar(EpicenterHandle handle, float* const* channelPointers, int channelCount, size_t numFrames);

/**
 * Processes interleaved 32-bit floating point audio in place.
 * Uses pre-allocated scratch buffers reserved during prepare().
 * Real-time safe: zero dynamic allocations, zero locks, zero I/O.
 *
 * @param handle Valid DSP handle.
 * @param interleavedBuffer Pointer to interleaved samples (L, R, L, R...).
 * @param channelCount Number of channels (1 or 2).
 * @param numFrames Number of audio frames to process.
 * @return EPICENTER_OK on success.
 */
EPICENTER_API EpicenterResult epicenter_process_interleaved(EpicenterHandle handle, float* interleavedBuffer, int channelCount, size_t numFrames);

/**
 * Queries the algorithmic latency of the DSP core in frames.
 * Note: The Epicenter minimum-phase IIR architecture operates with zero algorithmic frame delay.
 *
 * @param handle Valid DSP handle.
 * @return Algorithmic latency in sample frames (0).
 */
EPICENTER_API size_t epicenter_get_latency(EpicenterHandle handle);

#ifdef __cplusplus
} // extern "C"

#include <memory>

namespace epicenter {

/**
 * Idiomatic C++ RAII wrapper for Epicenter DSP.
 * Completely encapsulates algorithm internals; headers require no Windows or OS dependencies.
 */
class EpicenterDSP {
public:
    EpicenterDSP() : handle_(epicenter_create()) {}
    ~EpicenterDSP() {
        if (handle_) {
            epicenter_destroy(handle_);
            handle_ = nullptr;
        }
    }

    // Non-copyable, movable
    EpicenterDSP(const EpicenterDSP&) = delete;
    EpicenterDSP& operator=(const EpicenterDSP&) = delete;

    EpicenterDSP(EpicenterDSP&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    EpicenterDSP& operator=(EpicenterDSP&& other) noexcept {
        if (this != &other) {
            if (handle_) epicenter_destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    bool isValid() const { return handle_ != nullptr; }

    bool prepare(double sampleRate, int channels, size_t maxBlockSize = 512) {
        return handle_ && epicenter_prepare(handle_, sampleRate, channels, maxBlockSize) == EPICENTER_OK;
    }

    void reset() {
        if (handle_) epicenter_reset(handle_);
    }

    void setEnabled(bool enabled) {
        if (handle_) epicenter_set_enabled(handle_, enabled ? 1 : 0);
    }

    bool isEnabled() const {
        return handle_ ? (epicenter_is_enabled(handle_) != 0) : false;
    }

    void setIntensity(float intensity) {
        if (handle_) epicenter_set_intensity(handle_, intensity);
    }

    float getIntensity() const {
        return handle_ ? epicenter_get_intensity(handle_) : 0.0f;
    }

    bool process(float* const* channels, size_t numFrames) {
        return handle_ && epicenter_process_planar(handle_, channels, 2, numFrames) == EPICENTER_OK;
    }

    bool processPlanar(float* const* channels, int channelCount, size_t numFrames) {
        return handle_ && epicenter_process_planar(handle_, channels, channelCount, numFrames) == EPICENTER_OK;
    }

    bool processInterleaved(float* buffer, int channelCount, size_t numFrames) {
        return handle_ && epicenter_process_interleaved(handle_, buffer, channelCount, numFrames) == EPICENTER_OK;
    }

    size_t getLatency() const {
        return handle_ ? epicenter_get_latency(handle_) : 0;
    }

    EpicenterHandle rawHandle() const { return handle_; }

private:
    EpicenterHandle handle_ = nullptr;
};

} // namespace epicenter

#endif // __cplusplus

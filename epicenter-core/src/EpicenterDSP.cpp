#include "EpicenterDSP.h"
#include "internal/EpicenterProcessor.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>

struct EpicenterDSPState {
    std::atomic<bool> enabled{false};
    std::atomic<float> intensity{0.0f};
    epicenter::core_internal::EpicenterProcessor processor;
    bool prepared = false;
};

namespace {
inline float sanitizeIntensity(float v) {
    if (!std::isfinite(v)) {
        return 0.0f; // safe default on NaN or Inf
    }
    return std::max(0.0f, std::min(v, 100.0f));
}
} // namespace

extern "C" {

EpicenterHandle epicenter_create(void) {
    return new (std::nothrow) EpicenterDSPState();
}

void epicenter_destroy(EpicenterHandle handle) {
    delete handle;
}

EpicenterResult epicenter_prepare(EpicenterHandle handle, double sampleRate, int channels, size_t maxBlockSize) {
    if (!handle) return EPICENTER_ERROR_INVALID_ARGUMENT;
    if (sampleRate < 1000.0 || sampleRate > 384000.0) return EPICENTER_ERROR_UNSUPPORTED_CONFIG;
    if (channels < 1 || channels > epicenter::core_internal::EpicenterProcessor::kMaxChannels) return EPICENTER_ERROR_UNSUPPORTED_CONFIG;
    if (maxBlockSize == 0) return EPICENTER_ERROR_INVALID_ARGUMENT;

    try {
        handle->processor.prepare(sampleRate, channels, maxBlockSize);
        handle->prepared = true;
        return EPICENTER_OK;
    } catch (const std::bad_alloc&) {
        return EPICENTER_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return EPICENTER_ERROR_UNSUPPORTED_CONFIG;
    }
}

void epicenter_reset(EpicenterHandle handle) {
    if (!handle || !handle->prepared) return;
    handle->processor.reset();
}

void epicenter_set_enabled(EpicenterHandle handle, int enabled) {
    if (!handle) return;
    handle->enabled.store(enabled != 0, std::memory_order_release);
}

int epicenter_is_enabled(EpicenterHandle handle) {
    if (!handle) return 0;
    return handle->enabled.load(std::memory_order_acquire) ? 1 : 0;
}

void epicenter_set_intensity(EpicenterHandle handle, float intensity) {
    if (!handle) return;
    const float safe = sanitizeIntensity(intensity);
    handle->intensity.store(safe, std::memory_order_release);
}

float epicenter_get_intensity(EpicenterHandle handle) {
    if (!handle) return 0.0f;
    return handle->intensity.load(std::memory_order_acquire);
}

EpicenterResult epicenter_process_planar(EpicenterHandle handle, float* const* channelPointers, int channelCount, size_t numFrames) {
    if (!handle) return EPICENTER_ERROR_INVALID_ARGUMENT;
    if (!handle->prepared) return EPICENTER_ERROR_NOT_INITIALIZED;
    if (!channelPointers || channelCount <= 0 || numFrames == 0) return EPICENTER_OK;

    const bool enabled = handle->enabled.load(std::memory_order_acquire);
    const float intensity = handle->intensity.load(std::memory_order_acquire);

    handle->processor.process(channelPointers, numFrames, enabled, intensity);
    return EPICENTER_OK;
}

EpicenterResult epicenter_process_interleaved(EpicenterHandle handle, float* interleavedBuffer, int channelCount, size_t numFrames) {
    if (!handle) return EPICENTER_ERROR_INVALID_ARGUMENT;
    if (!handle->prepared) return EPICENTER_ERROR_NOT_INITIALIZED;
    if (!interleavedBuffer || channelCount <= 0 || numFrames == 0) return EPICENTER_OK;

    const int activeChannels = std::min(channelCount, handle->processor.channelCount());
    const bool enabled = handle->enabled.load(std::memory_order_acquire);
    const float intensity = handle->intensity.load(std::memory_order_acquire);
    const size_t maxChunk = handle->processor.maxFrames();

    size_t framesRemaining = numFrames;
    size_t frameOffset = 0;

    while (framesRemaining > 0) {
        const size_t chunk = std::min(framesRemaining, maxChunk);
        float* scratchPlanes[epicenter::core_internal::EpicenterProcessor::kMaxChannels];
        for (int c = 0; c < activeChannels; ++c) {
            scratchPlanes[c] = handle->processor.scratch(c);
        }

        // De-interleave chunk
        for (size_t i = 0; i < chunk; ++i) {
            const size_t srcIdx = (frameOffset + i) * static_cast<size_t>(channelCount);
            for (int c = 0; c < activeChannels; ++c) {
                scratchPlanes[c][i] = interleavedBuffer[srcIdx + static_cast<size_t>(c)];
            }
        }

        // Process in planar format
        handle->processor.process(scratchPlanes, chunk, enabled, intensity);

        // Re-interleave
        for (size_t i = 0; i < chunk; ++i) {
            const size_t dstIdx = (frameOffset + i) * static_cast<size_t>(channelCount);
            for (int c = 0; c < activeChannels; ++c) {
                interleavedBuffer[dstIdx + static_cast<size_t>(c)] = scratchPlanes[c][i];
            }
        }

        frameOffset += chunk;
        framesRemaining -= chunk;
    }

    return EPICENTER_OK;
}

size_t epicenter_get_latency(EpicenterHandle handle) {
    (void)handle;
    // Epicenter minimum-phase IIR architecture has 0 samples of algorithmic delay.
    return 0;
}

} // extern "C"

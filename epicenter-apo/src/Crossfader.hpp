#pragma once

/**
 * @file Crossfader.hpp
 * @brief Clickless real-time A/B crossfader for Windows APO.
 *
 * Implements smooth 8 ms linear/equal-power crossfade between original dry signal (A)
 * and processed Epicenter signal (B). Guaranteed zero allocations in realtime.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace epicenter {
namespace apo {

class ClicklessCrossfader {
public:
    void prepare(double sampleRate, float transitionMs = 8.0f) {
        sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        const double samples = (transitionMs * sampleRate_) / 1000.0;
        rampFrames_ = std::max<size_t>(32, static_cast<size_t>(samples + 0.5));
        step_ = 1.0f / static_cast<float>(rampFrames_);
        reset();
    }

    void reset() {
        currentAlpha_ = targetAlpha_;
    }

    void setTarget(float target) {
        targetAlpha_ = std::max(0.0f, std::min(target, 1.0f));
    }

    float currentAlpha() const { return currentAlpha_; }
    float targetAlpha() const { return targetAlpha_; }
    bool isTransitioning() const { return std::fabs(currentAlpha_ - targetAlpha_) > 1.0e-5f; }

    /**
     * Crossfades between dry and wet interleaved streams into output.
     * Can operate in-place if output == dry or output == wet.
     */
    void process(const float* dry, const float* wet, float* out, int channels, size_t frames) {
        if (!dry || !wet || !out || frames == 0 || channels <= 0) return;

        // Fast path 1: Fully wet (Epicenter active, no transition)
        if (currentAlpha_ >= 0.99999f && targetAlpha_ >= 0.99999f) {
            if (out != wet) {
                std::copy(wet, wet + frames * static_cast<size_t>(channels), out);
            }
            return;
        }

        // Fast path 2: Fully dry (Bypass, no transition)
        if (currentAlpha_ <= 0.00001f && targetAlpha_ <= 0.00001f) {
            if (out != dry) {
                std::copy(dry, dry + frames * static_cast<size_t>(channels), out);
            }
            return;
        }

        // Transitioning path: per-frame ramp to prevent audible clicks/pops
        for (size_t f = 0; f < frames; ++f) {
            if (currentAlpha_ < targetAlpha_) {
                currentAlpha_ = std::min(targetAlpha_, currentAlpha_ + step_);
            } else if (currentAlpha_ > targetAlpha_) {
                currentAlpha_ = std::max(targetAlpha_, currentAlpha_ - step_);
            }

            const float a = currentAlpha_;
            const float dryGain = 1.0f - a;
            const float wetGain = a;

            const size_t baseIdx = f * static_cast<size_t>(channels);
            for (int c = 0; c < channels; ++c) {
                out[baseIdx + c] = dry[baseIdx + c] * dryGain + wet[baseIdx + c] * wetGain;
            }
        }
    }

private:
    double sampleRate_ = 48000.0;
    size_t rampFrames_ = 384;
    float step_ = 1.0f / 384.0f;
    float currentAlpha_ = 0.0f; // Start in bypass (dry)
    float targetAlpha_ = 0.0f;
};

} // namespace apo
} // namespace epicenter

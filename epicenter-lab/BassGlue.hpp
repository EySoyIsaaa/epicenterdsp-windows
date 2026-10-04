#pragma once
//
// BassGlue — prototype "Soundgoodizer / Maximus-style" bass stage.
//
// This is the NEW thing we're testing. It runs AFTER the Epicenter core and does what the
// FL Studio tutorial's Soundgoodizer(D) does on an isolated low band:
//   1. Split off the low band with a smooth crossover (low = LP(x), high = x - low).
//   2. Soft saturation on the low band  -> adds harmonics = body + "translates" to headphones.
//   3. Compression on the low band      -> sustain / density = perceived DEPTH.
//   4. Parallel blend of dry-low + processed-low -> smoothness, keeps punch.
//
// Everything is deterministic and ports 1:1 to EpicenterDSPCore later if we like it.
//
#include "EpicenterDSPCore.hpp"
#include <algorithm>
#include <cmath>

namespace lab {

struct BassGlueParams {
    float splitHz   = 160.0f;  // crossover between "bass" and "rest"
    float drive     = 2.2f;    // saturation amount (harmonics)
    float depth     = 0.6f;    // 0..1 compression amount (sustain / density)
    float mix       = 0.75f;   // 0..1 parallel blend of processed low
    float outGainDb = 0.0f;    // final trim
};

class BassGlue {
public:
    void prepare(double sampleRate, int channelCount) {
        sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        ch_ = std::max(1, std::min(channelCount, 2));
        for (int c = 0; c < 2; ++c) {
            lowpass_[c].prepare(sr_, epicenter::BiquadFilter::Type::Lowpass, params_.splitHz, 0.5f);
            dcBlock_[c].prepare(sr_, epicenter::BiquadFilter::Type::Highpass, 18.0f, 0.707f);
        }
        // Linked stereo detector so L/R compress together (bass stays centered).
        detector_.prepare(sr_, 12.0f, 160.0f);
        lastSplitHz_ = params_.splitHz;
    }

    void setParameters(const BassGlueParams& p) {
        params_ = p;
        if (std::fabs(p.splitHz - lastSplitHz_) > 0.5f) {
            for (int c = 0; c < 2; ++c)
                lowpass_[c].updateCoeffs(epicenter::BiquadFilter::Type::Lowpass, p.splitHz, 0.5f);
            lastSplitHz_ = p.splitHz;
        }
    }

    void process(float* const* channels, int channelCount, std::size_t frameCount) {
        if (!channels || channelCount <= 0) return;
        const int ch = std::min(channelCount, 2);
        const float drive = std::max(1.0f, params_.drive);
        const float normSat = std::tanh(drive);
        // Compression settings derived from "depth".
        const float d = std::max(0.0f, std::min(params_.depth, 1.0f));
        const float thresholdDb = -6.0f - d * 18.0f;   // more depth -> lower threshold
        const float ratio       = 1.0f + d * 5.0f;      // 1:1 .. 6:1
        const float makeupDb    = d * 6.0f;
        const float mix = std::max(0.0f, std::min(params_.mix, 1.0f));
        const float outGain = std::pow(10.0f, params_.outGainDb / 20.0f);

        for (std::size_t i = 0; i < frameCount; ++i) {
            float dryLow[2], sat[2];
            float detInput = 0.0f;
            for (int c = 0; c < ch; ++c) {
                const float x = channels[c][i];
                const float low = lowpass_[c].process(x);
                dryLow[c] = low;
                // Soft saturation with a touch of asymmetry (2nd harmonic warmth), DC-blocked.
                float s = std::tanh(drive * (low + 0.12f * low * std::fabs(low))) / normSat;
                s = dcBlock_[c].process(s);
                sat[c] = s;
                detInput += std::fabs(s);
            }
            detInput /= static_cast<float>(ch);

            // Feed-forward downward compressor in dB domain (linked).
            const float env = detector_.process(detInput);
            const float envDb = 20.0f * std::log10(env + 1.0e-9f);
            const float overDb = envDb - thresholdDb;
            const float grDb = overDb > 0.0f ? -overDb * (1.0f - 1.0f / ratio) : 0.0f;
            const float compGain = std::pow(10.0f, (grDb + makeupDb) / 20.0f);

            for (int c = 0; c < ch; ++c) {
                const float x = channels[c][i];
                const float high = x - dryLow[c];
                const float processedLow = sat[c] * compGain;
                const float blendedLow = dryLow[c] * (1.0f - mix) + processedLow * mix;
                float out = (high + blendedLow) * outGain;
                // Gentle safety soft-clip.
                out = std::tanh(out);
                channels[c][i] = std::max(-1.0f, std::min(1.0f, out));
            }
        }
    }

private:
    double sr_ = 44100.0;
    int ch_ = 2;
    BassGlueParams params_;
    float lastSplitHz_ = -1.0f;
    epicenter::BiquadFilter lowpass_[2];
    epicenter::BiquadFilter dcBlock_[2];
    epicenter::EnvelopeFollower detector_;
};

} // namespace lab

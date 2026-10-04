#pragma once
//
// BassGlueV2 — targets the measured "YouTube Epicenter" fingerprint:
//   * huge deep-sub 20-50 Hz (clean synthesized subharmonic + boost)
//   * scoop 80-250 Hz (remove mud/boom) via real peaking cuts
//   * hard sub compression (dense, sustained = depth)
//   * bass summed to MONO (tight, powerful, translates)
//
#include "EpicenterDSPCore.hpp"
#include <algorithm>
#include <cmath>

namespace lab {

// RBJ peaking EQ (the copied core only has LP/HP/BP, so we add a peaking type here).
class Peak {
public:
    void set(double sr, float freq, float gainDb, float q) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 6.2831853f * freq / float(sr);
        const float c = std::cos(w0), s = std::sin(w0), alpha = s / (2.0f * q);
        const float a0 = 1 + alpha / A;
        b0_ = (1 + alpha * A) / a0; b1_ = (-2 * c) / a0; b2_ = (1 - alpha * A) / a0;
        a1_ = (-2 * c) / a0; a2_ = (1 - alpha / A) / a0;
    }
    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x; y2_ = y1_; y1_ = y; return y;
    }
private:
    float b0_=1,b1_=0,b2_=0,a1_=0,a2_=0,x1_=0,x2_=0,y1_=0,y2_=0;
};

struct BassGlueV2Params {
    float subGenHz   = 78.0f;
    float deepHz     = 46.0f;
    float subGen     = 1.8f;
    float subBoostDb = 10.0f;
    float scoopDb    = 10.0f;
    float subDepth   = 0.9f;
    float monoHz     = 95.0f;
    // Only synthesize the octave-down for genuinely DEEP bass. Above these the fundamental
    // isn't missing and zero-crossing tracking gets erratic -> weird warble on upper bass.
    float fundFadeStartHz = 90.0f;   // full synthesis below this bass fundamental
    float fundFadeEndHz   = 125.0f;  // no synthesis above this
};

class BassGlueV2 {
public:
    void prepare(double sr, int channelCount) {
        sr_ = sr > 1.0 ? sr : 44100.0;
        ch_ = std::max(1, std::min(channelCount, 2));
        apply();
        bassLP_.prepare(sr_, epicenter::BiquadFilter::Type::Lowpass, p_.subGenHz, 0.707f);
        deepLP_.prepare(sr_, epicenter::BiquadFilter::Type::Lowpass, p_.deepHz, 0.707f);
        rawSubLP_.prepare(sr_, epicenter::BiquadFilter::Type::Lowpass, 62.0f, 0.707f);
        subDC_.prepare(sr_, epicenter::BiquadFilter::Type::Highpass, 18.0f, 0.707f);
        subEnv_.prepare(sr_, 8.0f, 140.0f);
        bassAmpEnv_.prepare(sr_, 15.0f, 120.0f);
        period_ = float(sr_ / 55.0);
        smoothFreq_ = 0.5f * float(sr_) / period_;
    }

    void setParameters(const BassGlueV2Params& p) { p_ = p; apply();
        bassLP_.updateCoeffs(epicenter::BiquadFilter::Type::Lowpass, p.subGenHz, 0.707f);
        deepLP_.updateCoeffs(epicenter::BiquadFilter::Type::Lowpass, p.deepHz, 0.707f);
    }

    void process(float* const* channels, int channelCount, std::size_t frameCount) {
        if (!channels || channelCount <= 0) return;
        const int ch = std::min(channelCount, 2);
        const float subBoost = std::pow(10.0f, p_.subBoostDb / 20.0f);
        const float d = std::max(0.0f, std::min(p_.subDepth, 1.0f));
        const float thrDb = -10.0f - d * 22.0f, ratio = 1.0f + d * 16.0f, makeupDb = d * 7.0f;

        for (std::size_t i = 0; i < frameCount; ++i) {
            const float L = channels[0][i];
            const float R = ch > 1 ? channels[1][i] : L;
            const float mono = 0.5f * (L + R);

            // Clean subsonic: a pure SINE locked to half the bass fundamental, amplitude-
            // modulated by a smooth envelope. No rectification / no hard sign flips, so it
            // sounds smooth and subsonic instead of synthetic/buzzy.
            const float bass = bassLP_.process(mono);
            bassCount_ += 1.0f;
            if (lastBass_ <= 0.0f && bass > 0.0f) {                 // rising zero-crossing = one period
                const float lo = float(sr_) / 180.0f, hi = float(sr_) / 28.0f;
                if (bassCount_ > lo && bassCount_ < hi) period_ = period_ * 0.82f + bassCount_ * 0.18f;
                bassCount_ = 0.0f;
            }
            lastBass_ = bass;
            const float targetFreq = 0.5f * float(sr_) / std::max(period_, 1.0f);  // octave below
            smoothFreq_ += (targetFreq - smoothFreq_) * 0.001f;  // glide out per-cycle jitter (tiny warble)
            phase_ += 6.2831853f * smoothFreq_ / float(sr_);
            if (phase_ > 6.2831853f) phase_ -= 6.2831853f;
            const float amp = bassAmpEnv_.process(std::fabs(bass));             // smooth envelope
            // Fade the synthesized sub out on higher bass notes (see fundFade* params).
            const float fund = 2.0f * smoothFreq_;
            float wf = 1.0f;
            if (fund > p_.fundFadeStartHz) {
                const float span = std::max(1.0f, p_.fundFadeEndHz - p_.fundFadeStartHz);
                wf = std::max(0.0f, std::min(1.0f, (p_.fundFadeEndHz - fund) / span));
            }
            const float genSub = std::sin(phase_) * amp * 2.2f * wf;
            const float rawSub = rawSubLP_.process(mono);
            float sub = subDC_.process(rawSub + genSub * p_.subGen);

            const float env = subEnv_.process(std::fabs(sub));
            const float envDb = 20.0f * std::log10(env + 1e-9f);
            const float over = envDb - thrDb;
            const float grDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
            sub = std::tanh(sub * std::pow(10.0f, (grDb + makeupDb) / 20.0f) * subBoost);

            for (int c = 0; c < ch; ++c) {
                float s = channels[c][i];
                s = scoopA_[c].process(s);            // cut ~110 Hz
                s = scoopB_[c].process(s);            // cut ~200 Hz
                s = hp_[c].process(s);                // remove <monoHz -> bass becomes mono
                float out = s + sub;                  // mono sub added to both channels
                channels[c][i] = std::max(-1.0f, std::min(1.0f, out));
            }
        }
    }

private:
    void apply() {
        for (int c = 0; c < 2; ++c) {
            scoopA_[c].set(sr_, 110.0f, -p_.scoopDb, 0.9f);
            scoopB_[c].set(sr_, 200.0f, -p_.scoopDb * 0.45f, 1.0f);
            hp_[c].prepare(sr_, epicenter::BiquadFilter::Type::Highpass, p_.monoHz, 0.707f);
        }
    }
    double sr_ = 44100.0; int ch_ = 2;
    BassGlueV2Params p_;
    Peak scoopA_[2], scoopB_[2];
    epicenter::BiquadFilter hp_[2];
    epicenter::BiquadFilter bassLP_, deepLP_, rawSubLP_, subDC_;
    epicenter::EnvelopeFollower subEnv_, bassAmpEnv_;
    float lastBass_ = 0.0f, period_ = 800.0f, phase_ = 0.0f, bassCount_ = 0.0f, smoothFreq_ = 27.0f;
};

} // namespace lab

#pragma once
// PRIVATE — not installed, not part of the public API.
//
// DSP building blocks of the Epicenter algorithm. The arithmetic below is a verbatim move of
// the legacy prototype (epicenter-lab/EpicenterDSPCore.cpp, SHA-256 394E8ABE...F035A):
// same expressions, same operation order, same float types. Only the namespace changed and the
// functions became `inline` (they lived in the same translation unit as the processing loop in
// the legacy file, so the compiler could already inline them there).
//
// Real-time properties: no allocation, no locking, no I/O. updateCoeffs()/update() call
// sin/cos/pow/exp but never allocate.

#include <algorithm>
#include <cmath>

namespace epicenter {
namespace core_internal {

constexpr float DENORMAL_FLOOR = 1.0e-24f;
constexpr float TWO_PI = 6.28318530717958647692f;

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
// Flushes denormal-range values AND non-finite values (NaN/Inf) to zero. This is what makes
// the core immune to NaN/Inf input. Requires IEEE semantics: never compile with -ffast-math
// or /fp:fast (std::isfinite may be optimised away).
inline float denormalFloor(float v) { return (std::fabs(v) < DENORMAL_FLOOR || !std::isfinite(v)) ? 0.0f : v; }
inline float softClip(float value) {
    const float x2 = value * value;
    return (value * (27.0f + x2)) / (27.0f + 9.0f * x2);
}
inline float coeffFromMs(double sr, float ms) {
    const double samples = std::max(1.0, static_cast<double>(ms) * sr / 1000.0);
    return static_cast<float>(std::exp(-1.0 / samples));
}

class BiquadFilter {
public:
    enum class Type { Lowpass, Highpass, Bandpass };

    void prepare(double sampleRate, Type type, float freq, float q = 0.707f) {
        sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        updateCoeffs(type, freq, q);
        reset();
    }

    void updateCoeffs(Type type, float freq, float q = 0.707f) {
        type_ = type;
        const float clampedFreq = clampf(freq, 10.0f, static_cast<float>(sr_ * 0.45));
        const float clampedQ = clampf(q, 0.2f, 12.0f);
        const float omega = TWO_PI * clampedFreq / static_cast<float>(sr_);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * clampedQ);
        float b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type) {
            case Type::Lowpass:
                b0 = (1 - cosOmega) * 0.5f; b1 = 1 - cosOmega; b2 = (1 - cosOmega) * 0.5f;
                a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
                break;
            case Type::Highpass:
                b0 = (1 + cosOmega) * 0.5f; b1 = -(1 + cosOmega); b2 = (1 + cosOmega) * 0.5f;
                a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
                break;
            case Type::Bandpass:
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
                break;
        }
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }

    float process(float sample) {
        const float clean = denormalFloor(sample);
        const float y0 = denormalFloor(b0_ * clean + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_);
        x2_ = denormalFloor(x1_); x1_ = clean; y2_ = denormalFloor(y1_); y1_ = y0;
        return y0;
    }

    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

private:
    double sr_ = 44100.0;
    Type type_ = Type::Lowpass;
    float b0_ = 0, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

class LowShelfFilter {
public:
    void prepare(double sampleRate) { sr_ = sampleRate > 1.0 ? sampleRate : 44100.0; reset(); }

    void update(float freqHz, float gainDb) {
        const float clampedFreq = clampf(freqHz, 20.0f, static_cast<float>(sr_ * 0.45));
        const float clampedGain = clampf(gainDb, 0.0f, 10.5f);
        if (std::fabs(clampedFreq - freqHz_) < 1.0e-3f && std::fabs(clampedGain - gainDb_) < 1.0e-3f) return;
        freqHz_ = clampedFreq; gainDb_ = clampedGain;
        const float A = std::pow(10.0f, clampedGain / 40.0f);
        const float w0 = TWO_PI * clampedFreq / static_cast<float>(sr_);
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * 0.707f);
        const float sqrtA = std::sqrt(A);
        float b0 = A * ((A + 1) - (A - 1) * cosW0 + 2 * sqrtA * alpha);
        float b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
        float b2 = A * ((A + 1) - (A - 1) * cosW0 - 2 * sqrtA * alpha);
        const float a0 = (A + 1) + (A - 1) * cosW0 + 2 * sqrtA * alpha;
        float a1 = -2 * ((A - 1) + (A + 1) * cosW0);
        float a2 = (A + 1) + (A - 1) * cosW0 - 2 * sqrtA * alpha;
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }

    float process(float sample) {
        const float input = denormalFloor(sample);
        const float out = denormalFloor(b0_ * input + z1_);
        z1_ = denormalFloor(b1_ * input - a1_ * out + z2_);
        z2_ = denormalFloor(b2_ * input - a2_ * out);
        return out;
    }

    void reset() { z1_ = z2_ = 0.0f; freqHz_ = -1.0f; gainDb_ = -999.0f; }

private:
    double sr_ = 44100.0;
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float z1_ = 0, z2_ = 0;
    float freqHz_ = -1, gainDb_ = -999;
};

class EnvelopeFollower {
public:
    void prepare(double sampleRate, float attackMs, float releaseMs) {
        attackCoeff_ = coeffFromMs(sampleRate, attackMs);
        releaseCoeff_ = coeffFromMs(sampleRate, releaseMs);
        reset();
    }
    float process(float input) {
        const float x = std::fabs(denormalFloor(input));
        const float coeff = x > value_ ? attackCoeff_ : releaseCoeff_;
        value_ = denormalFloor(x + coeff * (value_ - x));
        return value_;
    }
    void reset() { value_ = 0.0f; }

private:
    float value_ = 0;
    float attackCoeff_ = 0;
    float releaseCoeff_ = 0;
};

} // namespace core_internal
} // namespace epicenter

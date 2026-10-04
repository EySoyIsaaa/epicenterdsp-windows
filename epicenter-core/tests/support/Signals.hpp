#pragma once
// Deterministic test-signal generators shared by tests, tools and benchmarks.
//
// All synthesis is done in double precision and rounded to float once. Noise uses a fixed
// xorshift32 PRNG, so a given (generator, sampleRate, seconds, seed) always yields the same
// samples on the same platform. Because libm sin()/exp() may differ by a few ULPs between
// toolchains, the *reference fixtures* are additionally stored as float32 WAV files in
// tests/audio/inputs/ and the golden tests read those files instead of re-synthesising.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace epicenter_test {

struct Signal {
    int sampleRate = 48000;
    std::vector<std::vector<float>> ch; // ch[channel][frame]

    int channels() const { return static_cast<int>(ch.size()); }
    std::size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

constexpr double kTwoPi = 6.283185307179586476925286766559;

inline Signal makeEmpty(int sampleRate, double seconds, int channels) {
    Signal s;
    s.sampleRate = sampleRate;
    const auto n = static_cast<std::size_t>(std::llround(seconds * sampleRate));
    s.ch.assign(static_cast<std::size_t>(channels), std::vector<float>(n, 0.0f));
    return s;
}

class XorShift32 {
public:
    explicit XorShift32(std::uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t next() {
        std::uint32_t x = state_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state_ = x;
        return x;
    }
    // Uniform in [-1, 1).
    double bipolar() { return (static_cast<double>(next()) / 4294967296.0) * 2.0 - 1.0; }
    // Uniform integer in [lo, hi].
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
private:
    std::uint32_t state_;
};

inline Signal silence(int sr, double seconds, int channels = 2) { return makeEmpty(sr, seconds, channels); }

inline Signal sine(int sr, double seconds, double freq, double amp, int channels = 2) {
    Signal s = makeEmpty(sr, seconds, channels);
    for (std::size_t i = 0; i < s.frames(); ++i) {
        const float v = static_cast<float>(amp * std::sin(kTwoPi * freq * static_cast<double>(i) / sr));
        for (auto& c : s.ch) c[i] = v;
    }
    return s;
}

// Exponential (log) sweep f0 -> f1.
inline Signal logSweep(int sr, double seconds, double f0, double f1, double amp, int channels = 2) {
    Signal s = makeEmpty(sr, seconds, channels);
    const double k = std::log(f1 / f0);
    for (std::size_t i = 0; i < s.frames(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double phase = kTwoPi * f0 * seconds / k * (std::exp(t / seconds * k) - 1.0);
        const float v = static_cast<float>(amp * std::sin(phase));
        for (auto& c : s.ch) c[i] = v;
    }
    return s;
}

// Different content per channel: exercises the stereo-difference (side) detector path.
inline Signal stereoTest(int sr, double seconds) {
    Signal s = makeEmpty(sr, seconds, 2);
    for (std::size_t i = 0; i < s.frames(); ++i) {
        const double t = static_cast<double>(i) / sr;
        s.ch[0][i] = static_cast<float>(0.40 * std::sin(kTwoPi * 50.0 * t) + 0.10 * std::sin(kTwoPi * 1000.0 * t));
        s.ch[1][i] = static_cast<float>(0.40 * std::sin(kTwoPi * 70.0 * t + 0.7) + 0.10 * std::sin(kTwoPi * 2000.0 * t + 0.3));
    }
    return s;
}

inline Signal whiteNoise(int sr, double seconds, double amp, std::uint32_t seed = 0xE91C3A7Bu, int channels = 2) {
    Signal s = makeEmpty(sr, seconds, channels);
    XorShift32 rng(seed);
    for (std::size_t i = 0; i < s.frames(); ++i)
        for (auto& c : s.ch) c[i] = static_cast<float>(amp * rng.bipolar());
    return s;
}

// Club-style synthetic track: pitch-dropping kick, harmonic bass line, off-beat hats, pad.
// Normalised to `peak` (default -1 dBFS, typical of mastered music).
inline Signal syntheticMusic(int sr, double seconds, double peak = 0.891, std::uint32_t seed = 0x51A7E5EDu) {
    Signal s = makeEmpty(sr, seconds, 2);
    const std::size_t n = s.frames();
    std::vector<double> L(n, 0.0), R(n, 0.0);
    const double beat = 60.0 / 124.0;
    const double bassNotes[4] = {41.20, 55.00, 49.00, 36.71}; // E1 A1 G1 D1, one per bar
    XorShift32 rng(seed);
    double hatPrev = 0.0;
    double kickPhase = 0.0;
    double bassPhase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        // Kick: on every beat, 45 Hz body with a fast pitch drop.
        const double tb = std::fmod(t, beat);
        const double kf = 45.0 + 70.0 * std::exp(-tb / 0.030);
        kickPhase += kTwoPi * kf / sr;
        if (tb < 1.0 / sr) kickPhase = 0.0;
        const double kick = 0.90 * std::exp(-tb / 0.25) * std::sin(kickPhase);
        // Bass: 8th notes, 6-harmonic saw, note per bar.
        const int bar = static_cast<int>(t / (4.0 * beat)) % 4;
        const double te = std::fmod(t, beat * 0.5);
        const double bf = bassNotes[bar];
        bassPhase += kTwoPi * bf / sr;
        double saw = 0.0;
        for (int h = 1; h <= 6; ++h) saw += std::sin(bassPhase * h) / h;
        const double bassEnv = std::min(1.0, te / 0.005) * std::exp(-te / 0.18);
        const double bass = 0.35 * bassEnv * saw;
        // Hats: noise bursts on the off-beat, crude high-pass via first difference.
        const double th = std::fmod(t + beat * 0.5, beat);
        const double noise = rng.bipolar();
        const double hat = 0.15 * std::exp(-th / 0.020) * (noise - hatPrev);
        hatPrev = noise;
        // Pad: A-major triad, slightly detuned between channels.
        const double padL = 0.06 * (std::sin(kTwoPi * 220.0 * t) + std::sin(kTwoPi * 277.18 * t) + std::sin(kTwoPi * 329.63 * t));
        const double padR = 0.06 * (std::sin(kTwoPi * 220.6 * t) + std::sin(kTwoPi * 277.9 * t) + std::sin(kTwoPi * 330.4 * t));
        L[i] = kick + bass + hat * 0.8 + padL;
        R[i] = kick + bass + hat * 1.2 + padR;
    }
    double maxAbs = 1e-12;
    for (std::size_t i = 0; i < n; ++i) maxAbs = std::max({maxAbs, std::fabs(L[i]), std::fabs(R[i])});
    const double g = peak / maxAbs;
    for (std::size_t i = 0; i < n; ++i) {
        s.ch[0][i] = static_cast<float>(L[i] * g);
        s.ch[1][i] = static_cast<float>(R[i] * g);
    }
    return s;
}

inline Signal toMono(const Signal& in) {
    Signal s;
    s.sampleRate = in.sampleRate;
    s.ch.assign(1, std::vector<float>(in.frames()));
    for (std::size_t i = 0; i < in.frames(); ++i)
        s.ch[0][i] = in.channels() > 1 ? 0.5f * (in.ch[0][i] + in.ch[1][i]) : in.ch[0][i];
    return s;
}

struct NamedSignal {
    std::string name;
    Signal signal;
};

// The canonical fixture set (also written to tests/audio/inputs/ at 48 kHz).
inline std::vector<NamedSignal> fixtureSet(int sr, double seconds) {
    return {
        {"silence", silence(sr, seconds)},
        {"sine_40hz", sine(sr, seconds, 40.0, 0.5)},
        {"sine_60hz", sine(sr, seconds, 60.0, 0.5)},
        {"sine_80hz", sine(sr, seconds, 80.0, 0.5)},
        {"sweep", logSweep(sr, seconds, 20.0, 20000.0, 0.5)},
        {"stereo_test", stereoTest(sr, seconds)},
        {"noise_white", whiteNoise(sr, seconds, 0.25)},
        {"synthetic_music", syntheticMusic(sr, seconds)},
    };
}

} // namespace epicenter_test

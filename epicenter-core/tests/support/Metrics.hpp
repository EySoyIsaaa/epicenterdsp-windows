#pragma once
// Signal metrics for regression / quality reports (tests only).

#include "Signals.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace epicenter_test {

struct LevelStats {
    double peak = 0.0;        // max |x|
    double rms = 0.0;
    std::size_t nonFinite = 0;
    std::size_t atFullScale = 0; // samples with |x| >= 0.99999 (hard-clamp engaged)
};

inline double toDb(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

inline LevelStats levelStats(const Signal& s, std::size_t skipFrames = 0) {
    LevelStats st;
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& c : s.ch) {
        for (std::size_t i = skipFrames; i < c.size(); ++i) {
            const float v = c[i];
            if (!std::isfinite(v)) { ++st.nonFinite; continue; }
            const double a = std::fabs(static_cast<double>(v));
            st.peak = std::max(st.peak, a);
            if (a >= 0.99999) ++st.atFullScale;
            sum += a * a;
            ++count;
        }
    }
    st.rms = count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    return st;
}

struct DiffStats {
    double maxAbs = 0.0;
    double rms = 0.0;
    std::size_t differing = 0; // samples that are not bit-identical
};

inline DiffStats diffStats(const Signal& a, const Signal& b) {
    DiffStats d;
    double sum = 0.0;
    std::size_t count = 0;
    const std::size_t chs = std::min(a.ch.size(), b.ch.size());
    for (std::size_t c = 0; c < chs; ++c) {
        const std::size_t n = std::min(a.ch[c].size(), b.ch[c].size());
        for (std::size_t i = 0; i < n; ++i) {
            const double e = static_cast<double>(a.ch[c][i]) - static_cast<double>(b.ch[c][i]);
            if (a.ch[c][i] != b.ch[c][i]) ++d.differing;
            d.maxAbs = std::max(d.maxAbs, std::fabs(e));
            sum += e * e;
            ++count;
        }
    }
    d.rms = count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    return d;
}

inline void fftInPlace(std::vector<std::complex<double>>& x) {
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -kTwoPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// 1/3-octave band levels (dB, Welch-averaged, Hann, N=8192, 50% overlap) of channel `c`.
struct BandLevels {
    std::vector<double> centerHz;
    std::vector<double> levelDb;
};

inline BandLevels thirdOctaveBands(const Signal& s, std::size_t c = 0) {
    constexpr std::size_t N = 8192;
    BandLevels out;
    const auto& x = s.ch.at(c);
    std::vector<double> power(N / 2 + 1, 0.0);
    std::vector<double> win(N);
    for (std::size_t i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / N);
    std::size_t frames = 0;
    std::vector<std::complex<double>> buf(N);
    for (std::size_t start = 0; start + N <= x.size(); start += N / 2) {
        for (std::size_t i = 0; i < N; ++i) buf[i] = {x[start + i] * win[i], 0.0};
        fftInPlace(buf);
        for (std::size_t k = 0; k <= N / 2; ++k) power[k] += std::norm(buf[k]);
        ++frames;
    }
    if (!frames) return out;
    const double binHz = static_cast<double>(s.sampleRate) / N;
    for (int b = -17; b <= 13; ++b) { // 20 Hz .. 20 kHz around 1 kHz
        const double fc = 1000.0 * std::pow(2.0, b / 3.0);
        const double lo = fc / std::pow(2.0, 1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
        if (hi >= s.sampleRate * 0.5) break;
        double e = 0.0;
        for (std::size_t k = 1; k <= N / 2; ++k) {
            const double f = k * binHz;
            if (f >= lo && f < hi) e += power[k];
        }
        out.centerHz.push_back(fc);
        out.levelDb.push_back(10.0 * std::log10(std::max(e / frames, 1e-30)));
    }
    return out;
}

// Max |dB difference| between two band analyses, ignoring bands below `floorDb` in both.
inline double maxBandDeltaDb(const BandLevels& a, const BandLevels& b, double floorDb = -200.0) {
    double m = 0.0;
    const std::size_t n = std::min(a.levelDb.size(), b.levelDb.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (a.levelDb[i] < floorDb && b.levelDb[i] < floorDb) continue;
        m = std::max(m, std::fabs(a.levelDb[i] - b.levelDb[i]));
    }
    return m;
}

} // namespace epicenter_test

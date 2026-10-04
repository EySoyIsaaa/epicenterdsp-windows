#include "EpicenterDSPCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
bool nearlyEqual(float a, float b) {
    return std::fabs(a - b) <= 1.0e-7f;
}

std::vector<float> makeSignal(std::size_t frames, float sampleRate, float phase) {
    std::vector<float> result(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        result[i] = 0.22f * std::sin(6.283185307f * 55.0f * t + phase)
                  + 0.08f * std::sin(6.283185307f * 180.0f * t + phase * 0.5f);
    }
    return result;
}
}

int main() {
    constexpr float sampleRate = 48000.0f;
    constexpr std::size_t frames = 4096;
    auto left = makeSignal(frames, sampleRate, 0.0f);
    auto right = makeSignal(frames, sampleRate, 0.3f);
    const auto originalLeft = left;
    const auto originalRight = right;
    float* channels[] = {left.data(), right.data()};

    epicenter::EpicenterDSPCore dsp;
    dsp.prepare(sampleRate, 2, 512);

    dsp.setEnabled(true);
    dsp.setParameters(0.0f, 45.0f, 50.0f, 100.0f, 100.0f);
    dsp.process(channels, 2, frames);
    for (std::size_t i = 0; i < frames; ++i) {
        if (!nearlyEqual(left[i], originalLeft[i]) || !nearlyEqual(right[i], originalRight[i])) {
            std::fprintf(stderr, "FAIL: intensity=0 is not a direct bypass at frame %zu\n", i);
            return 1;
        }
    }

    dsp.reset();
    dsp.setParameters(75.0f, 45.0f, 50.0f, 100.0f, 100.0f);
    dsp.process(channels, 2, frames);
    bool changed = false;
    for (std::size_t i = 0; i < frames; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]) ||
            std::fabs(left[i]) > 1.0001f || std::fabs(right[i]) > 1.0001f) {
            std::fprintf(stderr, "FAIL: invalid output at frame %zu\n", i);
            return 2;
        }
        changed = changed || std::fabs(left[i] - originalLeft[i]) > 1.0e-4f;
    }
    if (!changed) {
        std::fprintf(stderr, "FAIL: enabled DSP did not modify the test signal\n");
        return 3;
    }

    std::puts("PASS: bypass, processing and output bounds");
    return 0;
}


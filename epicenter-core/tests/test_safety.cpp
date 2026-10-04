#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

#include <cmath>
#include <limits>

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("safety_nan_and_inf_inputs") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(100.0f);

    const size_t frames = 2048;
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);

    // Populate with invalid numbers
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    for (size_t i = 0; i < frames; i += 7) left[i] = nanVal;
    for (size_t i = 1; i < frames; i += 11) right[i] = posInf;
    for (size_t i = 2; i < frames; i += 13) left[i] = negInf;

    float* ptrs[2] = { left.data(), right.data() };
    for (size_t off = 0; off < frames; off += 512) {
        float* chunk[2] = { ptrs[0] + off, ptrs[1] + off };
        dsp.processPlanar(chunk, 2, 512);
    }

    // Verify all outputs are finite and bounded
    for (size_t i = 0; i < frames; ++i) {
        CHECK(std::isfinite(left[i]));
        CHECK(std::isfinite(right[i]));
        CHECK(left[i] >= -1.0001f && left[i] <= 1.0001f);
        CHECK(right[i] >= -1.0001f && right[i] <= 1.0001f);
    }
}

TEST_CASE("safety_extreme_input_levels") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(100.0f);

    const size_t frames = 2048;
    std::vector<float> left(frames, 50.0f); // +50.0 amplitude (+34 dBFS)
    std::vector<float> right(frames, -50.0f); // -50.0 amplitude

    float* ptrs[2] = { left.data(), right.data() };
    for (size_t off = 0; off < frames; off += 512) {
        float* chunk[2] = { ptrs[0] + off, ptrs[1] + off };
        dsp.processPlanar(chunk, 2, 512);
    }

    for (size_t i = 0; i < frames; ++i) {
        CHECK(std::isfinite(left[i]));
        CHECK(std::isfinite(right[i]));
        CHECK(left[i] >= -1.0001f && left[i] <= 1.0001f);
        CHECK(right[i] >= -1.0001f && right[i] <= 1.0001f);
    }
}

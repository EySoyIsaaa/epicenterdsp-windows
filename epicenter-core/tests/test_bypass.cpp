#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("bypass_when_disabled") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));

    dsp.setEnabled(false);
    dsp.setIntensity(80.0f); // even with high intensity, disabled must bypass

    Signal original = syntheticMusic(48000, 1.0);
    Signal processed = original;

    for (size_t off = 0; off < processed.frames(); off += 256) {
        const size_t n = std::min<size_t>(256, processed.frames() - off);
        float* ptrs[2] = { processed.ch[0].data() + off, processed.ch[1].data() + off };
        dsp.processPlanar(ptrs, 2, n);
    }

    DiffStats diff = diffStats(original, processed);
    CHECK(diff.differing == 0);
    CHECK_NEAR(diff.maxAbs, 0.0, 1e-7); // Bit-accurate or clean pass-through
}

TEST_CASE("bypass_when_intensity_zero") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));

    dsp.setEnabled(true);
    dsp.setIntensity(0.0f); // 0% intensity is direct bypass

    Signal original = syntheticMusic(48000, 1.0);
    Signal processed = original;

    for (size_t off = 0; off < processed.frames(); off += 512) {
        const size_t n = std::min<size_t>(512, processed.frames() - off);
        float* ptrs[2] = { processed.ch[0].data() + off, processed.ch[1].data() + off };
        dsp.processPlanar(ptrs, 2, n);
    }

    DiffStats diff = diffStats(original, processed);
    CHECK_NEAR(diff.maxAbs, 0.0, 1e-7);
}

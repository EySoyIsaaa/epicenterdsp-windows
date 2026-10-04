#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("reset_deterministic_identity") {
    const double sr = 48000.0;
    const Signal music = syntheticMusic(static_cast<int>(sr), 1.5);

    // Instance 1: Dirty state then reset
    EpicenterDSP dsp1;
    REQUIRE(dsp1.prepare(sr, 2, 512));
    dsp1.setEnabled(true);
    dsp1.setIntensity(75.0f);

    // Process some random noise first to dirty the internal filters & envelopes
    Signal dirt = whiteNoise(static_cast<int>(sr), 1.0, 0.5);
    for (size_t off = 0; off < dirt.frames(); off += 512) {
        const size_t n = std::min<size_t>(512, dirt.frames() - off);
        float* ptrs[2] = { dirt.ch[0].data() + off, dirt.ch[1].data() + off };
        dsp1.processPlanar(ptrs, 2, n);
    }

    // Now RESET instance 1
    dsp1.reset();

    // Instance 2: Freshly prepared instance with same parameters
    EpicenterDSP dsp2;
    REQUIRE(dsp2.prepare(sr, 2, 512));
    dsp2.setEnabled(true);
    dsp2.setIntensity(75.0f);

    // Feed both instances identical test signal
    Signal out1 = music;
    Signal out2 = music;

    for (size_t off = 0; off < music.frames(); off += 512) {
        const size_t n = std::min<size_t>(512, music.frames() - off);
        float* ptrs1[2] = { out1.ch[0].data() + off, out1.ch[1].data() + off };
        dsp1.processPlanar(ptrs1, 2, n);

        float* ptrs2[2] = { out2.ch[0].data() + off, out2.ch[1].data() + off };
        dsp2.processPlanar(ptrs2, 2, n);
    }

    // Compare out1 vs out2: Must be completely identical
    DiffStats diff = diffStats(out1, out2);
    CHECK(diff.differing == 0);
    CHECK_NEAR(diff.maxAbs, 0.0, 1e-7);
}

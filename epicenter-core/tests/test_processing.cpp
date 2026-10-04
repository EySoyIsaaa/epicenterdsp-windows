#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("process_silence") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(75.0f);

    Signal s = silence(48000, 1.0, 2);
    float* ptrs[2] = { s.ch[0].data(), s.ch[1].data() };

    for (size_t offset = 0; offset < s.frames(); offset += 512) {
        const size_t n = std::min<size_t>(512, s.frames() - offset);
        float* chunk[2] = { ptrs[0] + offset, ptrs[1] + offset };
        dsp.processPlanar(chunk, 2, n);
    }

    LevelStats st = levelStats(s);
    CHECK(st.nonFinite == 0);
    CHECK_NEAR(st.peak, 0.0, 1e-12);
    CHECK_NEAR(st.rms, 0.0, 1e-12);
}

TEST_CASE("process_subharmonic_generation") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(80.0f);

    // 80 Hz sine wave: Epicenter detector should trigger and synthesize sub-harmonics (~40 Hz)
    Signal in = sine(48000, 1.5, 80.0, 0.5, 2);
    Signal out = in;

    for (size_t offset = 0; offset < out.frames(); offset += 512) {
        const size_t n = std::min<size_t>(512, out.frames() - offset);
        float* chunk[2] = { out.ch[0].data() + offset, out.ch[1].data() + offset };
        dsp.processPlanar(chunk, 2, n);
    }

    LevelStats outStats = levelStats(out, 4800); // skip initial transient
    CHECK(outStats.nonFinite == 0);
    CHECK(outStats.peak > 0.05);

    // Verify signal was modified significantly (energy added)
    DiffStats diff = diffStats(in, out);
    CHECK(diff.differing > 0);
    CHECK(diff.rms > 0.01);
}

TEST_CASE("process_variable_block_sizes") {
    EpicenterDSP dsp;
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(60.0f);

    Signal music = syntheticMusic(48000, 2.0);

    // Process with varying, non-power-of-two chunk sizes (e.g. 17, 73, 256, 512, 13)
    const size_t chunkSizes[] = { 17, 73, 256, 512, 13, 99, 128 };
    size_t chunkIdx = 0;
    size_t offset = 0;

    while (offset < music.frames()) {
        const size_t blockSize = chunkSizes[chunkIdx % (sizeof(chunkSizes)/sizeof(chunkSizes[0]))];
        const size_t n = std::min(blockSize, music.frames() - offset);
        float* ptrs[2] = { music.ch[0].data() + offset, music.ch[1].data() + offset };
        dsp.processPlanar(ptrs, 2, n);

        offset += n;
        ++chunkIdx;
    }

    LevelStats st = levelStats(music);
    CHECK(st.nonFinite == 0);
    CHECK(st.peak > 0.1);
    CHECK(st.peak <= 1.0001); // bounded
}

TEST_CASE("process_interleaved_vs_planar") {
    const double sr = 48000.0;
    const size_t frames = 2048;

    Signal planarSig = syntheticMusic(static_cast<int>(sr), 1.0);
    Signal interSig = planarSig;

    // Build interleaved buffer
    std::vector<float> interleaved(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        interleaved[i * 2] = interSig.ch[0][i];
        interleaved[i * 2 + 1] = interSig.ch[1][i];
    }

    EpicenterDSP dspPlanar;
    dspPlanar.prepare(sr, 2, 512);
    dspPlanar.setEnabled(true);
    dspPlanar.setIntensity(70.0f);

    EpicenterDSP dspInter;
    dspInter.prepare(sr, 2, 512);
    dspInter.setEnabled(true);
    dspInter.setIntensity(70.0f);

    // Process planar
    for (size_t off = 0; off < frames; off += 512) {
        float* ptrs[2] = { planarSig.ch[0].data() + off, planarSig.ch[1].data() + off };
        dspPlanar.processPlanar(ptrs, 2, 512);
    }

    // Process interleaved
    for (size_t off = 0; off < frames; off += 512) {
        dspInter.processInterleaved(interleaved.data() + off * 2, 2, 512);
    }

    // Compare results
    for (size_t i = 0; i < frames; ++i) {
        CHECK_NEAR(planarSig.ch[0][i], interleaved[i * 2], 1e-6f);
        CHECK_NEAR(planarSig.ch[1][i], interleaved[i * 2 + 1], 1e-6f);
    }
}

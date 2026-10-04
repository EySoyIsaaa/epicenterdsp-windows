#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "WavIO.hpp"
#include "Metrics.hpp"

#include <cstdio>
#include <string>

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("regression_compare_against_legacy_golden") {
    const std::string inDir = "tests/audio/inputs";
    const std::string goldenDir = "tests/audio/golden";
    const int intensities[] = {60, 100};

    const auto fixtures = fixtureSet(48000, 2.0);

    for (const auto& f : fixtures) {
        Signal input;
        const std::string inPath = inDir + "/" + f.name + ".wav";
        REQUIRE(readWav(inPath, input));

        for (int intensity : intensities) {
            // 1. Load frozen legacy golden reference
            Signal golden;
            const std::string goldenPath = goldenDir + "/" + f.name + "_i" + std::to_string(intensity) + ".wav";
            REQUIRE(readWav(goldenPath, golden));

            // 2. Process with NEW CORE
            Signal newOut = input;
            EpicenterDSP dsp;
            REQUIRE(dsp.prepare(input.sampleRate, input.channels(), 512));
            dsp.setEnabled(true);
            dsp.setIntensity(static_cast<float>(intensity));

            for (size_t off = 0; off < newOut.frames(); off += 512) {
                const size_t n = std::min<size_t>(512, newOut.frames() - off);
                float* ptrs[2] = { newOut.ch[0].data() + off, newOut.ch[newOut.ch.size() > 1 ? 1 : 0].data() + off };
                dsp.processPlanar(ptrs, input.channels(), n);
            }

            // 3. Compare NEW CORE output vs GOLDEN output
            DiffStats diff = diffStats(newOut, golden);
            LevelStats goldenStats = levelStats(golden);
            LevelStats newStats = levelStats(newOut);

            // Ensure identical peak & RMS
            CHECK_NEAR(goldenStats.peak, newStats.peak, 1e-5);
            CHECK_NEAR(goldenStats.rms, newStats.rms, 1e-5);

            // Difference between legacy and new core should be virtually zero (bit-identical or numerical noise < 1e-6)
            CHECK_MSG(diff.maxAbs < 1e-6, "Regression error: max difference between legacy and new core exceeds 1e-6");
            CHECK_MSG(diff.rms < 1e-7, "Regression error: RMS difference exceeds 1e-7");

            if (diff.maxAbs >= 1e-6) {
                std::fprintf(stderr, "  REGRESSION in %s i=%d: maxAbs=%.9g, rms=%.9g\n",
                             f.name.c_str(), intensity, diff.maxAbs, diff.rms);
            }
        }
    }
}

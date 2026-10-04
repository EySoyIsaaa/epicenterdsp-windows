#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

#include <cmath>
#include <limits>

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("intensity_bounds_and_sanitization") {
    EpicenterDSP dsp;
    REQUIRE(dsp.isValid());

    // Normal values
    dsp.setIntensity(0.0f);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);

    dsp.setIntensity(50.0f);
    CHECK_NEAR(dsp.getIntensity(), 50.0f, 1e-6);

    dsp.setIntensity(100.0f);
    CHECK_NEAR(dsp.getIntensity(), 100.0f, 1e-6);

    // Negative values -> clamp to 0.0f
    dsp.setIntensity(-1.0f);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);

    dsp.setIntensity(-9999.0f);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);

    // Values > 100 -> clamp to 100.0f
    dsp.setIntensity(100.1f);
    CHECK_NEAR(dsp.getIntensity(), 100.0f, 1e-6);

    dsp.setIntensity(9999.0f);
    CHECK_NEAR(dsp.getIntensity(), 100.0f, 1e-6);

    // Non-finite values (NaN, Inf) -> sanitize to safe 0.0f
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    dsp.setIntensity(nanVal);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);

    const float posInf = std::numeric_limits<float>::infinity();
    dsp.setIntensity(posInf);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);

    const float negInf = -std::numeric_limits<float>::infinity();
    dsp.setIntensity(negInf);
    CHECK_NEAR(dsp.getIntensity(), 0.0f, 1e-6);
}

TEST_CASE("intensity_monotonic_boost") {
    // Tests that higher intensity produces greater or equal RMS sub-bass energy
    const double sr = 48000.0;
    const Signal source = sine(static_cast<int>(sr), 1.0, 70.0, 0.4);

    const float intensities[] = { 10.0f, 30.0f, 60.0f, 90.0f };
    double prevRms = 0.0;

    for (float inten : intensities) {
        EpicenterDSP dsp;
        dsp.prepare(sr, 2, 512);
        dsp.setEnabled(true);
        dsp.setIntensity(inten);

        Signal sig = source;
        for (size_t off = 0; off < sig.frames(); off += 512) {
            const size_t n = std::min<size_t>(512, sig.frames() - off);
            float* ptrs[2] = { sig.ch[0].data() + off, sig.ch[1].data() + off };
            dsp.processPlanar(ptrs, 2, n);
        }

        LevelStats st = levelStats(sig, 4800);
        CHECK(st.rms >= prevRms * 0.95); // general monotonic increase with headroom tolerance
        prevRms = st.rms;
    }
}

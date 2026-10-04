#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"

using namespace epicenter;

TEST_CASE("init_valid_configurations") {
    EpicenterDSP dsp;
    REQUIRE(dsp.isValid());

    // Supported sample rates
    CHECK(dsp.prepare(44100.0, 2, 512));
    CHECK(dsp.prepare(48000.0, 2, 512));
    CHECK(dsp.prepare(96000.0, 2, 512));

    // Supported channels: 1 (mono), 2 (stereo)
    CHECK(dsp.prepare(48000.0, 1, 256));
    CHECK(dsp.prepare(48000.0, 2, 256));

    // Various block sizes
    const size_t testBlocks[] = { 32, 64, 128, 256, 512, 1024 };
    for (size_t b : testBlocks) {
        CHECK(dsp.prepare(48000.0, 2, b));
    }
}

TEST_CASE("init_invalid_configurations") {
    EpicenterDSP dsp;
    REQUIRE(dsp.isValid());

    // Invalid sample rates
    CHECK(!dsp.prepare(0.0, 2, 512));
    CHECK(!dsp.prepare(-48000.0, 2, 512));
    CHECK(!dsp.prepare(500.0, 2, 512)); // < 1000 Hz unsupported
    CHECK(!dsp.prepare(500000.0, 2, 512)); // > 384000 Hz unsupported

    // Invalid channel counts
    CHECK(!dsp.prepare(48000.0, 0, 512));
    CHECK(!dsp.prepare(48000.0, 3, 512)); // > 2 unsupported in Core 1.0

    // Invalid block size
    CHECK(!dsp.prepare(48000.0, 2, 0));
}

TEST_CASE("init_unprepared_call_safety") {
    // Calling process on unprepared handle returns error, doesn't crash
    EpicenterHandle raw = epicenter_create();
    REQUIRE(raw != nullptr);

    float left[64] = {0};
    float right[64] = {0};
    float* ptrs[2] = {left, right};

    EpicenterResult res = epicenter_process_planar(raw, ptrs, 2, 64);
    CHECK(res == EPICENTER_ERROR_NOT_INITIALIZED);

    res = epicenter_process_interleaved(raw, left, 2, 32);
    CHECK(res == EPICENTER_ERROR_NOT_INITIALIZED);

    epicenter_destroy(raw);
}

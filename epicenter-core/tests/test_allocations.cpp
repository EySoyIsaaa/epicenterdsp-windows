#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "AllocCounter.hpp"

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("allocations_zero_in_realtime_path") {
    if (!alloc::available()) {
        std::printf("  [SKIPPED] AllocCounter not available in current configuration\n");
        return;
    }

    EpicenterDSP dsp;
    // prepare() is allowed to allocate working buffers
    REQUIRE(dsp.prepare(48000.0, 2, 512));
    dsp.setEnabled(true);
    dsp.setIntensity(75.0f);

    const size_t frames = 512;
    std::vector<float> left(frames, 0.2f);
    std::vector<float> right(frames, -0.2f);
    float* ptrs[2] = { left.data(), right.data() };

    std::vector<float> interleaved(frames * 2, 0.15f);

    // Warm-up run
    dsp.processPlanar(ptrs, 2, frames);

    // 1. Test processPlanar has zero allocations
    alloc::beginTracking();
    for (int k = 0; k < 100; ++k) {
        dsp.processPlanar(ptrs, 2, frames);
    }
    alloc::Snapshot snapPlanar = alloc::endTracking();

    CHECK_MSG(snapPlanar.allocations == 0, "processPlanar() must perform 0 allocations");
    CHECK_MSG(snapPlanar.deallocations == 0, "processPlanar() must perform 0 deallocations");
    CHECK_MSG(snapPlanar.bytes == 0, "processPlanar() must allocate 0 bytes");

    // 2. Test processInterleaved has zero allocations (uses pre-allocated scratch)
    alloc::beginTracking();
    for (int k = 0; k < 100; ++k) {
        dsp.processInterleaved(interleaved.data(), 2, frames);
    }
    alloc::Snapshot snapInter = alloc::endTracking();

    CHECK_MSG(snapInter.allocations == 0, "processInterleaved() must perform 0 allocations");
    CHECK_MSG(snapInter.deallocations == 0, "processInterleaved() must perform 0 deallocations");
    CHECK_MSG(snapInter.bytes == 0, "processInterleaved() must allocate 0 bytes");

    // 3. Test reset() has zero allocations
    alloc::beginTracking();
    dsp.reset();
    alloc::Snapshot snapReset = alloc::endTracking();

    CHECK_MSG(snapReset.allocations == 0, "reset() must perform 0 allocations");

    // 4. Test parameter changes have zero allocations
    alloc::beginTracking();
    dsp.setIntensity(50.0f);
    dsp.setEnabled(false);
    dsp.setEnabled(true);
    alloc::Snapshot snapParams = alloc::endTracking();

    CHECK_MSG(snapParams.allocations == 0, "setIntensity/setEnabled must perform 0 allocations");
}

#include "EpicenterDSP.h"
#include "TestHarness.hpp"
#include "Signals.hpp"
#include "Metrics.hpp"

using namespace epicenter;
using namespace epicenter_test;

TEST_CASE("stability_long_running_stress") {
    EpicenterDSP dsp;
    const double sr = 48000.0;
    REQUIRE(dsp.prepare(sr, 2, 1024));
    dsp.setEnabled(true);

    // Simulate 3 minutes of audio = 180 seconds * 48000 = 8,640,000 frames
    const double testSeconds = 180.0;
    const size_t totalFrames = static_cast<size_t>(testSeconds * sr);

    // Use a looping 5-second synthetic music track
    Signal music = syntheticMusic(static_cast<int>(sr), 5.0);
    const size_t loopLen = music.frames();

    std::vector<float> leftBuf(1024);
    std::vector<float> rightBuf(1024);
    float* ptrs[2] = { leftBuf.data(), rightBuf.data() };

    size_t processedFrames = 0;
    size_t blockIdx = 0;
    const size_t testBlockSizes[] = { 64, 128, 256, 512, 1024, 83, 177, 349 };

    float currentIntensity = 20.0f;
    float intensityDir = 1.0f;

    while (processedFrames < totalFrames) {
        const size_t blockSize = testBlockSizes[blockIdx % (sizeof(testBlockSizes)/sizeof(testBlockSizes[0]))];
        const size_t n = std::min(blockSize, totalFrames - processedFrames);

        // Fill buffer from looping track
        for (size_t i = 0; i < n; ++i) {
            const size_t srcIdx = (processedFrames + i) % loopLen;
            leftBuf[i] = music.ch[0][srcIdx];
            rightBuf[i] = music.ch[1][srcIdx];
        }

        // Continually modulate parameter
        currentIntensity += intensityDir * 0.15f;
        if (currentIntensity >= 95.0f) { currentIntensity = 95.0f; intensityDir = -1.0f; }
        else if (currentIntensity <= 10.0f) { currentIntensity = 10.0f; intensityDir = 1.0f; }
        dsp.setIntensity(currentIntensity);

        // Process audio block
        dsp.processPlanar(ptrs, 2, n);

        // Check bounds on output
        for (size_t i = 0; i < n; ++i) {
            REQUIRE(std::isfinite(leftBuf[i]));
            REQUIRE(std::isfinite(rightBuf[i]));
            REQUIRE(leftBuf[i] >= -1.0001f && leftBuf[i] <= 1.0001f);
            REQUIRE(rightBuf[i] >= -1.0001f && rightBuf[i] <= 1.0001f);
        }

        processedFrames += n;
        ++blockIdx;
    }

    CHECK(processedFrames == totalFrames);
}

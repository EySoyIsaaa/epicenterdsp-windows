#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_stress_high_load_and_concurrent_ipc") {
    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(48000, 2, 32);

    const UINT32 maxFrames = 512;
    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = maxFrames;
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

    std::vector<float> inBuffer(maxFrames * 2);
    std::vector<float> outBuffer(maxFrames * 2);

    // Initialize with varied audio signal (sine + noise)
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < inBuffer.size(); ++i) {
        inBuffer[i] = dist(rng);
    }

    std::atomic<bool> stopWriter{ false };
    std::atomic<uint64_t> writeCount{ 0 };

    // Launch background thread simulating UI rapidly writing parameter updates
    std::thread writerThread([&]() {
        std::mt19937 writerRng(1337);
        std::uniform_real_distribution<float> intensityDist(0.0f, 100.0f);
        std::uniform_int_distribution<int> boolDist(0, 1);

        while (!stopWriter.load(std::memory_order_relaxed)) {
            if (apo->ipcChannel().isOpen()) {
                epicenter::ipc::writeControlState(
                    apo->ipcChannel().state(),
                    boolDist(writerRng),
                    intensityDist(writerRng),
                    1
                );
                writeCount.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    // Audio thread: process 10,000 blocks with randomized quantum sizes
    const UINT32 testBlocks[] = { 32, 64, 128, 256, 480, 512 };
    const size_t numIterations = 10000;

    for (size_t iter = 0; iter < numIterations; ++iter) {
        UINT32 frames = testBlocks[iter % (sizeof(testBlocks) / sizeof(testBlocks[0]))];

        APO_CONNECTION_PROPERTY inProp{};
        inProp.pBuffer = reinterpret_cast<UINT_PTR>(inBuffer.data());
        inProp.u32ValidFrameCount = frames;
        inProp.u32BufferFlags = BUFFER_VALID;

        APO_CONNECTION_PROPERTY outProp{};
        outProp.pBuffer = reinterpret_cast<UINT_PTR>(outBuffer.data());
        outProp.u32ValidFrameCount = 0;
        outProp.u32BufferFlags = BUFFER_INVALID;

        APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
        APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

        apo->APOProcess(1, inProps, 1, outProps);

        CHECK(outProp.u32ValidFrameCount == frames);
        CHECK(outProp.u32BufferFlags == BUFFER_VALID);

        // Periodically verify outputs remain finite and bounded
        if (iter % 1000 == 0) {
            for (UINT32 s = 0; s < frames * 2; ++s) {
                float sample = outBuffer[s];
                CHECK(std::isfinite(sample));
                CHECK(sample >= -2.0f && sample <= 2.0f);
            }
        }
    }

    // Stop writer thread
    stopWriter.store(true, std::memory_order_relaxed);
    writerThread.join();

    CHECK(writeCount.load() > 100);

    // Verify telemetry is updated
    if (apo->ipcChannel().isOpen()) {
        const auto* state = apo->ipcChannel().state();
        CHECK(state->telemetry.bufferFrameCount > 0);
        CHECK(state->telemetry.apoHeartbeat > 0);
    }

    apo->UnlockForProcess();
    apo->Release();
}

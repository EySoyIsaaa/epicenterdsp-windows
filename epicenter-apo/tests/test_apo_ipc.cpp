#include "EpicenterApoIpc.h"
#include "ApoIpcClient.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"

#include <cmath>
#include <limits>
#include <thread>
#include <atomic>

using namespace epicenter::apo;
using namespace epicenter::ipc;

TEST_CASE("ipc_creation_and_initial_defaults") {
    SharedMemoryChannel chan;
    REQUIRE(chan.open(true));
    REQUIRE(chan.isOpen());

    const auto* state = chan.state();
    CHECK(state->magic == EPICENTER_IPC_MAGIC);
    CHECK(state->protocolVersion == EPICENTER_IPC_VERSION);
    CHECK(state->initFlags == 1);

    // Initial default must be bypass
    CHECK(state->control.enabled == 0);
    CHECK(state->control.intensity == 0.0f);
}

TEST_CASE("ipc_seqlock_read_write_consistency") {
    SharedMemoryChannel chan;
    REQUIRE(chan.open(true));
    auto* s = chan.state();

    writeControlState(s, 1, 65.5f, 1, 2);

    uint32_t en = 0, ab = 0;
    float inten = 0.0f;
    bool ok = readControlState(s, &en, &inten, &ab);

    CHECK(ok);
    CHECK(en == 1);
    CHECK_NEAR(inten, 65.5f, 1e-5);
    CHECK(ab == 1);

    // Test when writer is in progress (odd sequence)
    s->control.sequence = 3; // odd
    ok = readControlState(s, &en, &inten, &ab);
    CHECK(!ok); // reader must not spin and must reject read

    s->control.sequence = 4; // even
    ok = readControlState(s, &en, &inten, &ab);
    CHECK(ok);
}

TEST_CASE("ipc_sanitization_nan_and_inf") {
    SharedMemoryChannel chan;
    REQUIRE(chan.open(true));
    auto* s = chan.state();

    // Inject NaN into intensity
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    writeControlState(s, 1, nanVal, 1);

    uint32_t en = 0, ab = 0;
    float inten = 999.0f;
    bool ok = readControlState(s, &en, &inten, &ab);

    CHECK(ok);
    CHECK(std::isfinite(inten));
    CHECK_NEAR(inten, 0.0f, 1e-6); // Sanitized to 0.0f

    // Inject Infinity
    const float infVal = std::numeric_limits<float>::infinity();
    writeControlState(s, 1, infVal, 1);

    ok = readControlState(s, &en, &inten, &ab);
    CHECK(ok);
    CHECK(std::isfinite(inten));
    CHECK_NEAR(inten, 0.0f, 1e-6);

    // Inject Out-of-bounds float
    writeControlState(s, 1, 250.0f, 1);
    ok = readControlState(s, &en, &inten, &ab);
    CHECK(ok);
    CHECK_NEAR(inten, 100.0f, 1e-6); // Clamped to 100.0f
}

TEST_CASE("ipc_concurrent_writer_and_reader") {
    SharedMemoryChannel chan;
    REQUIRE(chan.open(true));
    auto* s = chan.state();

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> writeCount{0};
    std::atomic<uint64_t> readCount{0};

    // Writer thread (simulating high-frequency UI updates)
    std::thread writer([&]() {
        float val = 0.0f;
        while (!stop.load(std::memory_order_relaxed)) {
            val += 0.5f;
            if (val > 100.0f) val = 0.0f;
            writeControlState(s, 1, val, 1);
            writeCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Reader thread (simulating real-time audio thread)
    std::thread reader([&]() {
        uint32_t en = 0, ab = 0;
        float inten = 0.0f;
        for (int k = 0; k < 50000; ++k) {
            if (readControlState(s, &en, &inten, &ab)) {
                CHECK_MSG(inten >= 0.0f && inten <= 100.0f && std::isfinite(inten),
                          "Corrupt intensity read during concurrent IPC update");
            }
            readCount.fetch_add(1, std::memory_order_relaxed);
        }
        stop.store(true);
    });

    writer.join();
    reader.join();

    CHECK(readCount.load() >= 50000);
    CHECK(writeCount.load() > 0);
}

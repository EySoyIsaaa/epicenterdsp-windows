#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "../../epicenter-core/tests/support/AllocCounter.hpp"

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_realtime_zero_allocations_in_process") {
    if (!alloc::available()) {
        std::printf("  [SKIPPED] AllocCounter not available in current configuration\n");
        return;
    }

    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(48000, 2, 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = 512;
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

    const UINT32 frameCount = 256;
    std::vector<float> inBuffer(frameCount * 2, 0.25f);
    std::vector<float> outBuffer(frameCount * 2, 0.0f);

    APO_CONNECTION_PROPERTY inProp{};
    inProp.pBuffer = reinterpret_cast<UINT_PTR>(inBuffer.data());
    inProp.u32ValidFrameCount = frameCount;
    inProp.u32BufferFlags = BUFFER_VALID;

    APO_CONNECTION_PROPERTY outProp{};
    outProp.pBuffer = reinterpret_cast<UINT_PTR>(outBuffer.data());
    outProp.u32ValidFrameCount = 0;
    outProp.u32BufferFlags = BUFFER_INVALID;

    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

    // Warm-up run
    for (int i = 0; i < 10; ++i) {
        apo->APOProcess(1, inProps, 1, outProps);
    }

    // 1. Verify zero allocations during active processing
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 80.0f, 1);
    }

    alloc::beginTracking();
    for (int i = 0; i < 500; ++i) {
        apo->APOProcess(1, inProps, 1, outProps);
    }
    alloc::Snapshot activeSnap = alloc::endTracking();

    CHECK_MSG(activeSnap.allocations == 0, "APOProcess() active state must perform 0 allocations");
    CHECK_MSG(activeSnap.deallocations == 0, "APOProcess() active state must perform 0 deallocations");
    CHECK_MSG(activeSnap.bytes == 0, "APOProcess() active state must allocate 0 bytes");

    // 2. Verify zero allocations during crossfading / state changes
    alloc::beginTracking();
    for (int i = 0; i < 500; ++i) {
        if (apo->ipcChannel().isOpen()) {
            // Alternate bypass and active
            bool enabled = (i % 50) < 25;
            float intensity = 50.0f + static_cast<float>(i % 50);
            epicenter::ipc::writeControlState(apo->ipcChannel().state(), enabled ? 1 : 0, intensity, 1);
        }
        apo->APOProcess(1, inProps, 1, outProps);
    }
    alloc::Snapshot xfadeSnap = alloc::endTracking();

    CHECK_MSG(xfadeSnap.allocations == 0, "APOProcess() during parameter/bypass transitions must perform 0 allocations");
    CHECK_MSG(xfadeSnap.deallocations == 0, "APOProcess() during parameter/bypass transitions must perform 0 deallocations");
    CHECK_MSG(xfadeSnap.bytes == 0, "APOProcess() during parameter/bypass transitions must allocate 0 bytes");

    // 3. Verify zero allocations during BUFFER_SILENT handling
    inProp.u32BufferFlags = BUFFER_SILENT;
    alloc::beginTracking();
    for (int i = 0; i < 200; ++i) {
        apo->APOProcess(1, inProps, 1, outProps);
    }
    alloc::Snapshot silentSnap = alloc::endTracking();

    CHECK_MSG(silentSnap.allocations == 0, "APOProcess() with BUFFER_SILENT must perform 0 allocations");
    CHECK_MSG(silentSnap.deallocations == 0, "APOProcess() with BUFFER_SILENT must perform 0 deallocations");
    CHECK_MSG(silentSnap.bytes == 0, "APOProcess() with BUFFER_SILENT must allocate 0 bytes");

    apo->UnlockForProcess();
    apo->Release();
}

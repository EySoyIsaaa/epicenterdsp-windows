#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "../../epicenter-core/tests/support/Metrics.hpp"

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_bypass_when_disabled") {
    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(48000, 2, 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = 512;
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

    // Explicitly set enabled = 0 in IPC
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 0, 80.0f, 1);
    }

    Signal music = syntheticMusic(48000, 1.0);
    const size_t totalFrames = music.frames();
    std::vector<float> inInter(totalFrames * 2);
    std::vector<float> outInter(totalFrames * 2);

    for (size_t i = 0; i < totalFrames; ++i) {
        inInter[i * 2] = music.ch[0][i];
        inInter[i * 2 + 1] = music.ch[1][i];
    }

    // Process blocks
    for (size_t offset = 0; offset < totalFrames; offset += 512) {
        const UINT32 n = static_cast<UINT32>(std::min<size_t>(512, totalFrames - offset));

        APO_CONNECTION_PROPERTY inProp{};
        inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data() + offset * 2);
        inProp.u32ValidFrameCount = n;
        inProp.u32BufferFlags = BUFFER_VALID;

        APO_CONNECTION_PROPERTY outProp{};
        outProp.pBuffer = reinterpret_cast<UINT_PTR>(outInter.data() + offset * 2);
        outProp.u32ValidFrameCount = 0;
        outProp.u32BufferFlags = BUFFER_INVALID;

        APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
        APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

        apo->APOProcess(1, inProps, 1, outProps);
    }

    // Verify output is bit-identical to input
    for (size_t i = 0; i < totalFrames * 2; ++i) {
        CHECK_NEAR(outInter[i], inInter[i], 1e-7);
    }

    apo->UnlockForProcess();
    apo->Release();
}

TEST_CASE("apo_bypass_silent_buffer") {
    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(48000, 2, 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = 256;
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

    // Even if effect is enabled, BUFFER_SILENT must remain silent
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 100.0f, 1);
    }

    std::vector<float> silenceBuf(256 * 2, 0.0f);
    std::vector<float> outBuf(256 * 2, 0.5f);

    APO_CONNECTION_PROPERTY inProp{};
    inProp.pBuffer = reinterpret_cast<UINT_PTR>(silenceBuf.data());
    inProp.u32ValidFrameCount = 256;
    inProp.u32BufferFlags = BUFFER_SILENT;

    APO_CONNECTION_PROPERTY outProp{};
    outProp.pBuffer = reinterpret_cast<UINT_PTR>(outBuf.data());
    outProp.u32ValidFrameCount = 0;
    outProp.u32BufferFlags = BUFFER_INVALID;

    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

    apo->APOProcess(1, inProps, 1, outProps);

    CHECK((outProp.u32BufferFlags & BUFFER_SILENT) != 0);

    apo->UnlockForProcess();
    apo->Release();
}

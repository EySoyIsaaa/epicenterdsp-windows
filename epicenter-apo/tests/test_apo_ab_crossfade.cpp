#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "Crossfader.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("crossfader_smooth_ramp_and_bounds") {
    ClicklessCrossfader crossfader;
    const double sr = 48000.0;
    crossfader.prepare(sr, 8.0f); // 8ms = 384 frames

    CHECK_NEAR(crossfader.currentAlpha(), 0.0f, 1e-6);
    CHECK(!crossfader.isTransitioning());

    // Set target to Wet (1.0)
    crossfader.setTarget(1.0f);
    CHECK(crossfader.isTransitioning());

    std::vector<float> dry(384 * 2, 0.5f);
    std::vector<float> wet(384 * 2, -0.5f);
    std::vector<float> out(384 * 2, 0.0f);

    crossfader.process(dry.data(), wet.data(), out.data(), 2, 384);

    // After 384 frames (8 ms), crossfade should reach 1.0 (fully wet)
    CHECK_NEAR(crossfader.currentAlpha(), 1.0f, 1e-3);
    CHECK(!crossfader.isTransitioning());

    // Verify per-sample continuity (no step discontinuities)
    for (size_t i = 1; i < 384; ++i) {
        const float deltaL = std::fabs(out[i * 2] - out[(i - 1) * 2]);
        const float deltaR = std::fabs(out[i * 2 + 1] - out[(i - 1) * 2 + 1]);
        // Maximum step between adjacent frames must be bounded by ramp slope
        CHECK_MSG(deltaL < 0.01f, "Discontinuity detected in left channel crossfade");
        CHECK_MSG(deltaR < 0.01f, "Discontinuity detected in right channel crossfade");
    }
}

TEST_CASE("apo_ab_toggle_audio_continuity") {
    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(48000, 2, 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = 512;
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

    // Continuous 60 Hz sine wave
    Signal sineSig = sine(48000, 1.0, 60.0, 0.5, 2);
    const size_t totalFrames = sineSig.frames();

    std::vector<float> inInter(totalFrames * 2);
    std::vector<float> outInter(totalFrames * 2);
    for (size_t i = 0; i < totalFrames; ++i) {
        inInter[i * 2] = sineSig.ch[0][i];
        inInter[i * 2 + 1] = sineSig.ch[1][i];
    }

    // Start with B (Epicenter Active)
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 80.0f, 1);
    }

    // Process first 1000 frames active
    size_t offset = 0;
    while (offset < 4800) {
        APO_CONNECTION_PROPERTY inProp{};
        inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data() + offset * 2);
        inProp.u32ValidFrameCount = 256;
        inProp.u32BufferFlags = BUFFER_VALID;

        APO_CONNECTION_PROPERTY outProp{};
        outProp.pBuffer = reinterpret_cast<UINT_PTR>(outInter.data() + offset * 2);
        outProp.u32ValidFrameCount = 0;
        outProp.u32BufferFlags = BUFFER_INVALID;

        APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
        APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

        apo->APOProcess(1, inProps, 1, outProps);
        offset += 256;
    }

    // Now TOGGLE A/B to A (Original Bypass) while audio is playing
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 80.0f, 0); // abState = 0
    }

    while (offset < 9600) {
        APO_CONNECTION_PROPERTY inProp{};
        inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data() + offset * 2);
        inProp.u32ValidFrameCount = 256;
        inProp.u32BufferFlags = BUFFER_VALID;

        APO_CONNECTION_PROPERTY outProp{};
        outProp.pBuffer = reinterpret_cast<UINT_PTR>(outInter.data() + offset * 2);
        outProp.u32ValidFrameCount = 0;
        outProp.u32BufferFlags = BUFFER_INVALID;

        APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
        APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

        apo->APOProcess(1, inProps, 1, outProps);
        offset += 256;
    }

    // Check maximum derivative across the entire transition period
    double maxStep = 0.0;
    for (size_t i = 1; i < offset; ++i) {
        const double dL = std::fabs(static_cast<double>(outInter[i * 2] - outInter[(i - 1) * 2]));
        const double dR = std::fabs(static_cast<double>(outInter[i * 2 + 1] - outInter[(i - 1) * 2 + 1]));
        if (dL > maxStep) maxStep = dL;
        if (dR > maxStep) maxStep = dR;
    }

    // An abrupt pop or click produces step delta close to 1.0; smooth audio transitions remain small
    CHECK_MSG(maxStep < 0.20, "Audible pop or click detected during A/B crossfade toggle");

    apo->UnlockForProcess();
    apo->Release();
}

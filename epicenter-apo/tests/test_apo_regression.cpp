#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "../../epicenter-core/tests/support/Metrics.hpp"
#include "../../epicenter-core/tests/support/LegacyCore.hpp"
#include "EpicenterDSP.h"

#include <cmath>
#include <vector>

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_regression_identity_with_core_and_legacy") {
    const double sampleRate = 48000.0;
    const WORD channels = 2;
    const UINT32 blockSize = 256;
    const float intensities[] = { 60.0f, 100.0f };

    for (float intensity : intensities) {
        // 1. Prepare New Core directly
        epicenter::EpicenterDSP newCore;
        REQUIRE(newCore.prepare(sampleRate, channels, blockSize));
        newCore.setEnabled(true);
        newCore.setIntensity(intensity);

        // 2. Prepare Legacy Core directly
        LegacyCore legacyCore;
        legacyCore.prepare(sampleRate, channels, blockSize);
        legacyCore.configure(true, intensity);

        // 3. Prepare APO
        auto* apo = new CEpicenterApo();
        CMockAudioMediaType mediaType(static_cast<DWORD>(sampleRate), channels, 32);

        APO_CONNECTION_DESCRIPTOR inDesc{};
        inDesc.u32MaxFrameCount = blockSize;
        inDesc.pFormat = &mediaType;
        APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
        APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
        APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

        REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

        // Configure APO IPC: active effect
        if (apo->ipcChannel().isOpen()) {
            epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, intensity, 1);
        }

        // Generate synthetic bass signal: 55 Hz sine wave + 110 Hz harmonic
        Signal testSig = sine(static_cast<DWORD>(sampleRate), 1.0, 55.0, 0.5, channels);

        const size_t totalFrames = testSig.frames();
        std::vector<float> inInter(totalFrames * channels);
        std::vector<float> apoOutInter(totalFrames * channels);
        std::vector<float> coreOutInter(totalFrames * channels);
        std::vector<float> legacyOutInter(totalFrames * channels);

        for (size_t i = 0; i < totalFrames; ++i) {
            for (WORD c = 0; c < channels; ++c) {
                inInter[i * channels + c] = testSig.ch[c][i];
            }
        }

        // Process through New Core
        coreOutInter = inInter;
        for (size_t off = 0; off < totalFrames; off += blockSize) {
            const size_t n = std::min<size_t>(blockSize, totalFrames - off);
            newCore.processInterleaved(coreOutInter.data() + off * channels, channels, n);
        }

        // Process through Legacy Core
        Signal legacySig = testSig;
        legacyCore.processSignal(legacySig, blockSize);
        for (size_t i = 0; i < totalFrames; ++i) {
            for (WORD c = 0; c < channels; ++c) {
                legacyOutInter[i * channels + c] = legacySig.ch[c][i];
            }
        }

        // Process through APO
        // The APO starts in bypass and crossfades to active over ~8ms (384 frames @ 48kHz).
        for (size_t off = 0; off < totalFrames; off += blockSize) {
            const UINT32 n = static_cast<UINT32>(std::min<size_t>(blockSize, totalFrames - off));

            APO_CONNECTION_PROPERTY inProp{};
            inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data() + off * channels);
            inProp.u32ValidFrameCount = n;
            inProp.u32BufferFlags = BUFFER_VALID;

            APO_CONNECTION_PROPERTY outProp{};
            outProp.pBuffer = reinterpret_cast<UINT_PTR>(apoOutInter.data() + off * channels);
            outProp.u32ValidFrameCount = 0;
            outProp.u32BufferFlags = BUFFER_INVALID;

            APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
            APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

            apo->APOProcess(1, inProps, 1, outProps);
        }

        // After crossfade has fully settled (skip first 2048 frames ~ 42ms),
        // APO output and New Core output must be identical within float precision.
        const size_t settleFrames = 2048;
        REQUIRE(totalFrames > settleFrames);

        double maxDiffApoVsCore = 0.0;
        double maxDiffApoVsLegacy = 0.0;

        for (size_t i = settleFrames; i < totalFrames; ++i) {
            for (WORD c = 0; c < channels; ++c) {
                size_t idx = i * channels + c;
                double diffCore = std::abs(apoOutInter[idx] - coreOutInter[idx]);
                double diffLegacy = std::abs(apoOutInter[idx] - legacyOutInter[idx]);
                if (diffCore > maxDiffApoVsCore) maxDiffApoVsCore = diffCore;
                if (diffLegacy > maxDiffApoVsLegacy) maxDiffApoVsLegacy = diffLegacy;
            }
        }

        // Difference between APO and Core should be virtually 0 (< 1e-6)
        CHECK_MSG(maxDiffApoVsCore < 1e-6,
                  "Settled APO output differs from New Core output!");
        // Difference between APO and Legacy should be virtually 0 (< 1e-6)
        CHECK_MSG(maxDiffApoVsLegacy < 1e-6,
                  "Settled APO output differs from Legacy Golden Core output!");

        apo->UnlockForProcess();
        apo->Release();
    }
}

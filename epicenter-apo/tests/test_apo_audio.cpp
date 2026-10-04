#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "../../epicenter-core/tests/support/Metrics.hpp"

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_audio_processing_multirate_and_multichannel") {
    const DWORD sampleRates[] = { 44100, 48000, 96000 };
    const WORD channelCounts[] = { 1, 2 };
    const UINT32 blockSizes[] = { 32, 64, 128, 256, 512 };

    for (DWORD sr : sampleRates) {
        for (WORD chs : channelCounts) {
            auto* apo = new CEpicenterApo();
            CMockAudioMediaType mediaType(sr, chs, 32);

            APO_CONNECTION_DESCRIPTOR inDesc{};
            inDesc.u32MaxFrameCount = 512;
            inDesc.pFormat = &mediaType;
            APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
            APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
            APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

            REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

            // Configure IPC to active effect
            if (apo->ipcChannel().isOpen()) {
                epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 75.0f, 1);
            }

            Signal testSig = (chs == 1) ? toMono(sine(sr, 0.5, 75.0, 0.4, 2))
                                        : sine(sr, 0.5, 75.0, 0.4, 2);

            // Interleaved input & output buffers
            const size_t totalFrames = testSig.frames();
            std::vector<float> inInter(totalFrames * chs);
            std::vector<float> outInter(totalFrames * chs);

            for (size_t i = 0; i < totalFrames; ++i) {
                for (WORD c = 0; c < chs; ++c) {
                    inInter[i * chs + c] = testSig.ch[c][i];
                }
            }

            for (UINT32 block : blockSizes) {
                size_t offset = 0;
                while (offset < totalFrames) {
                    const UINT32 n = static_cast<UINT32>(std::min<size_t>(block, totalFrames - offset));

                    APO_CONNECTION_PROPERTY inProp{};
                    inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data() + offset * chs);
                    inProp.u32ValidFrameCount = n;
                    inProp.u32BufferFlags = BUFFER_VALID;

                    APO_CONNECTION_PROPERTY outProp{};
                    outProp.pBuffer = reinterpret_cast<UINT_PTR>(outInter.data() + offset * chs);
                    outProp.u32ValidFrameCount = 0;
                    outProp.u32BufferFlags = BUFFER_INVALID;

                    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
                    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

                    apo->APOProcess(1, inProps, 1, outProps);

                    CHECK(outProp.u32ValidFrameCount == n);
                    CHECK(outProp.u32BufferFlags == BUFFER_VALID);

                    offset += n;
                }
            }

            // Verify output is finite and bounded
            for (float sample : outInter) {
                CHECK(std::isfinite(sample));
                CHECK(sample >= -1.0001f && sample <= 1.0001f);
            }

            apo->UnlockForProcess();
            apo->Release();
        }
    }
}

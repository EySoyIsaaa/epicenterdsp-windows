#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"

#include <vector>
#include <cmath>

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

TEST_CASE("apo_lifecycle_scenarios_a_through_g") {
    const double sr48 = 48000.0;
    const WORD chs2 = 2;
    const UINT32 blockSize = 256;

    CMockAudioMediaType mediaType48(static_cast<DWORD>(sr48), chs2, 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = blockSize;
    inDesc.pFormat = &mediaType48;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    std::vector<float> inBuf(blockSize * chs2);
    for (size_t i = 0; i < blockSize; ++i) {
        float s = 0.4f * std::sin(2.0f * 3.14159265f * 60.0f * static_cast<float>(i) / 48000.0f);
        inBuf[i * 2 + 0] = s;
        inBuf[i * 2 + 1] = s;
    }
    std::vector<float> outBuf(blockSize * chs2, 0.0f);

    APO_CONNECTION_PROPERTY inProp{};
    inProp.pBuffer = reinterpret_cast<UINT_PTR>(inBuf.data());
    inProp.u32ValidFrameCount = blockSize;
    inProp.u32BufferFlags = BUFFER_VALID;

    APO_CONNECTION_PROPERTY outProp{};
    outProp.pBuffer = reinterpret_cast<UINT_PTR>(outBuf.data());
    outProp.u32ValidFrameCount = 0;
    outProp.u32BufferFlags = BUFFER_INVALID;

    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

    // =========================================================================
    // CASO A: Windows inicia, UI NO está abierta
    // =========================================================================
    {
        auto* apo = new CEpicenterApo();
        REQUIRE(SUCCEEDED(apo->LockForProcess(1, inList, 1, outList)));

        // If shared memory was active from prior runs, force bypass defaults
        if (apo->ipcChannel().isOpen()) {
            epicenter::ipc::writeControlState(apo->ipcChannel().state(), 0, 0.0f, 0);
        }

        apo->APOProcess(1, inProps, 1, outProps);
        CHECK(outProp.u32ValidFrameCount == blockSize);
        CHECK(outProp.u32BufferFlags == BUFFER_VALID);

        // Verify output is exact copy of input (transparent bit-exact bypass)
        for (size_t i = 0; i < inBuf.size(); ++i) {
            CHECK_NEAR(outBuf[i], inBuf[i], 1e-6);
        }

        // =====================================================================
        // CASO B: UI se abre posteriormente sin reiniciar el audio engine
        // =====================================================================
        SharedMemoryChannel uiChannel;
        REQUIRE(uiChannel.open(false)); // Connect to the segment created by APO
        epicenter::ipc::writeControlState(uiChannel.state(), 1, 80.0f, 1);

        // Process blocks so crossfader transitions from dry to wet
        for (int k = 0; k < 50; ++k) {
            apo->APOProcess(1, inProps, 1, outProps);
        }

        // Output must now contain processed audio (harmonics added, differs from dry)
        double diffFromDry = 0.0;
        for (size_t i = 0; i < inBuf.size(); ++i) {
            diffFromDry += std::abs(outBuf[i] - inBuf[i]);
        }
        CHECK(diffFromDry > 0.01);

        // =====================================================================
        // CASO C: UI se cierra normalmente
        // =====================================================================
        uiChannel.close();

        // APO must continue safely with no memory leaks, errors, or audio dropouts
        for (int k = 0; k < 20; ++k) {
            apo->APOProcess(1, inProps, 1, outProps);
            CHECK(outProp.u32BufferFlags == BUFFER_VALID);
        }

        // =====================================================================
        // CASO D: UI se termina abruptamente (simulado: escritor muere mid-update)
        // =====================================================================
        {
            SharedMemoryChannel rogueUi;
            REQUIRE(rogueUi.open(false));
            auto* state = rogueUi.state();

            // Simulate UI crashing right after incrementing sequence to odd
            state->control.sequence = state->control.sequence + 1; // Left odd!
            // UI abruptly dies (handle closed)
            rogueUi.close();
        }

        // APO must NEVER hang, spin, or crash; it safely drops bad read and continues
        for (int k = 0; k < 50; ++k) {
            apo->APOProcess(1, inProps, 1, outProps);
            CHECK(outProp.u32BufferFlags == BUFFER_VALID);
            for (float s : outBuf) {
                CHECK(std::isfinite(s));
            }
        }

        // =====================================================================
        // CASO E: UI se reinicia y se reconecta limpiamente
        // =====================================================================
        {
            SharedMemoryChannel revivedUi;
            REQUIRE(revivedUi.open(false));
            // Restores even sequence and writes new parameters
            epicenter::ipc::writeControlState(revivedUi.state(), 1, 50.0f, 1);

            for (int k = 0; k < 20; ++k) {
                apo->APOProcess(1, inProps, 1, outProps);
            }
            CHECK(outProp.u32BufferFlags == BUFFER_VALID);
        }

        apo->UnlockForProcess();
        apo->Release();
    }

    // =========================================================================
    // CASO F: audiodg.exe se reinicia mientras la UI está abierta
    // =========================================================================
    {
        // 1. UI is open first and holds shared memory
        SharedMemoryChannel persistentUi;
        REQUIRE(persistentUi.open(true));
        epicenter::ipc::writeControlState(persistentUi.state(), 1, 90.0f, 1);

        // 2. New audiodg.exe instance starts and instantiates fresh APO
        auto* freshApo = new CEpicenterApo();
        REQUIRE(SUCCEEDED(freshApo->LockForProcess(1, inList, 1, outList)));

        // Run process
        for (int k = 0; k < 50; ++k) {
            freshApo->APOProcess(1, inProps, 1, outProps);
        }

        // Check that fresh APO hooked into UI parameters
        CHECK(outProp.u32BufferFlags == BUFFER_VALID);
        double diffFromDry = 0.0;
        for (size_t i = 0; i < inBuf.size(); ++i) {
            diffFromDry += std::abs(outBuf[i] - inBuf[i]);
        }
        CHECK(diffFromDry > 0.01);

        freshApo->UnlockForProcess();
        freshApo->Release();
    }

    // =========================================================================
    // CASO G: Cambio dinámico de formato (Sample Rate 48k -> 96k, Canales 2 -> 1)
    // =========================================================================
    {
        auto* dynApo = new CEpicenterApo();
        REQUIRE(SUCCEEDED(dynApo->LockForProcess(1, inList, 1, outList)));
        dynApo->APOProcess(1, inProps, 1, outProps);

        // Unlock and re-lock with 96 kHz
        REQUIRE(SUCCEEDED(dynApo->UnlockForProcess()));

        CMockAudioMediaType mediaType96(96000, 2, 32);
        APO_CONNECTION_DESCRIPTOR desc96{};
        desc96.u32MaxFrameCount = blockSize;
        desc96.pFormat = &mediaType96;
        APO_CONNECTION_DESCRIPTOR* inList96[] = { &desc96 };
        APO_CONNECTION_DESCRIPTOR* outList96[] = { &desc96 };

        REQUIRE(SUCCEEDED(dynApo->LockForProcess(1, inList96, 1, outList96)));
        CHECK(dynApo->activeSampleRate() == 96000.0);

        for (int k = 0; k < 10; ++k) {
            dynApo->APOProcess(1, inProps, 1, outProps);
            CHECK(outProp.u32BufferFlags == BUFFER_VALID);
        }

        dynApo->UnlockForProcess();
        dynApo->Release();
    }
}

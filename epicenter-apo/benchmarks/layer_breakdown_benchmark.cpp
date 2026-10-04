#define _CRT_SECURE_NO_WARNINGS
#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "EpicenterDSP.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace epicenter::apo;
using namespace epicenter_test;

int main() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double toMicroseconds = 1.0e6 / static_cast<double>(freq.QuadPart);

    const int sr = 48000;
    const int chs = 2;
    const int block = 256;
    const int iterations = 50000;

    // 1. Prepare Core DSP alone
    epicenter::EpicenterDSP core;
    core.prepare(sr, chs, block);
    core.setEnabled(true);
    core.setIntensity(75.0f);

    // 2. Prepare Crossfader alone
    ClicklessCrossfader crossfader;
    crossfader.prepare(sr, 8.0f);
    crossfader.setTarget(1.0f);

    // 3. Prepare IPC alone
    SharedMemoryChannel ipcChan;
    ipcChan.open(true);
    auto* ipcState = ipcChan.state();
    epicenter::ipc::writeControlState(ipcState, 1, 75.0f, 1);

    // 4. Prepare full APO
    auto* apo = new CEpicenterApo();
    test::CMockAudioMediaType mediaType(sr, chs, 32);
    APO_CONNECTION_DESCRIPTOR desc{};
    desc.u32MaxFrameCount = block;
    desc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR* inDescList[] = { &desc };
    APO_CONNECTION_DESCRIPTOR* outDescList[] = { &desc };
    apo->LockForProcess(1, inDescList, 1, outDescList);
    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), 1, 75.0f, 1);
    }

    std::vector<float> dry(block * chs, 0.25f);
    std::vector<float> wet(block * chs, 0.35f);
    std::vector<float> out(block * chs, 0.0f);

    APO_CONNECTION_PROPERTY inProp{};
    inProp.pBuffer = reinterpret_cast<UINT_PTR>(dry.data());
    inProp.u32ValidFrameCount = block;
    inProp.u32BufferFlags = BUFFER_VALID;

    APO_CONNECTION_PROPERTY outProp{};
    outProp.pBuffer = reinterpret_cast<UINT_PTR>(out.data());
    outProp.u32ValidFrameCount = 0;
    outProp.u32BufferFlags = BUFFER_INVALID;

    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

    // Warm-up all caches
    for (int i = 0; i < 1000; ++i) {
        core.processInterleaved(wet.data(), chs, block);
        crossfader.process(dry.data(), wet.data(), out.data(), chs, block);
        apo->APOProcess(1, inProps, 1, outProps);
    }

    // Benchmark 1: Core DSP alone
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iterations; ++i) {
        core.processInterleaved(wet.data(), chs, block);
    }
    QueryPerformanceCounter(&t1);
    const double coreTotalUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMicroseconds;
    const double coreAvgUs = coreTotalUs / iterations;

    // Benchmark 2: IPC Read & Telemetry Write alone
    uint32_t en, ab;
    float inten;
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iterations; ++i) {
        epicenter::ipc::readControlState(ipcState, &en, &inten, &ab);
        epicenter::ipc::writeTelemetry(ipcState, sr, chs, block, 120.0f, -6.0f, -18.0f, 1);
    }
    QueryPerformanceCounter(&t1);
    const double ipcTotalUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMicroseconds;
    const double ipcAvgUs = ipcTotalUs / iterations;

    // Benchmark 3: Crossfader alone (transitioning path)
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iterations; ++i) {
        // Toggle target to keep crossfader calculating ramps
        crossfader.setTarget((i % 2 == 0) ? 0.0f : 1.0f);
        crossfader.process(dry.data(), wet.data(), out.data(), chs, block);
    }
    QueryPerformanceCounter(&t1);
    const double xfadeTransitionTotalUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMicroseconds;
    const double xfadeTransitionAvgUs = xfadeTransitionTotalUs / iterations;

    // Benchmark 3b: Crossfader alone (stationary wet fast-path)
    crossfader.prepare(sr, 8.0f);
    crossfader.setTarget(1.0f);
    for (int i = 0; i < 100; ++i) crossfader.process(dry.data(), wet.data(), out.data(), chs, block);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iterations; ++i) {
        crossfader.process(dry.data(), wet.data(), out.data(), chs, block);
    }
    QueryPerformanceCounter(&t1);
    const double xfadeStationaryTotalUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMicroseconds;
    const double xfadeStationaryAvgUs = xfadeStationaryTotalUs / iterations;

    // Benchmark 4: Full APOProcess()
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iterations; ++i) {
        apo->APOProcess(1, inProps, 1, outProps);
    }
    QueryPerformanceCounter(&t1);
    const double apoTotalUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMicroseconds;
    const double apoAvgUs = apoTotalUs / iterations;

    const double quantumDurationUs = (static_cast<double>(block) / sr) * 1.0e6;
    const double wrapperOverheadUs = std::max(0.0, apoAvgUs - coreAvgUs - ipcAvgUs - xfadeStationaryAvgUs);

    std::printf("\n=================================================================================\n");
    std::printf("                   EPICENTER APO DETAILED LAYER BREAKDOWN BENCHMARK\n");
    std::printf("                     (Config: 48,000 Hz, Stereo, 256 Samples/Block)\n");
    std::printf("=================================================================================\n");
    std::printf("Quantum Duration: %.2f us (5.33 ms)\n\n", quantumDurationUs);
    std::printf("| Subsystem / Layer                      | Avg Time (us) | %% of Quantum | %% of APO Total |\n");
    std::printf("|----------------------------------------|---------------|--------------|----------------|\n");
    std::printf("| 1. Epicenter Core DSP                  | %13.3f | %11.4f%% | %13.2f%% |\n",
                coreAvgUs, (coreAvgUs / quantumDurationUs) * 100.0, (coreAvgUs / apoAvgUs) * 100.0);
    std::printf("| 2. IPC (SeqLock Read + Telemetry Write)| %13.3f | %11.4f%% | %13.2f%% |\n",
                ipcAvgUs, (ipcAvgUs / quantumDurationUs) * 100.0, (ipcAvgUs / apoAvgUs) * 100.0);
    std::printf("| 3. Crossfader (Stationary Fast Path)   | %13.3f | %11.4f%% | %13.2f%% |\n",
                xfadeStationaryAvgUs, (xfadeStationaryAvgUs / quantumDurationUs) * 100.0, (xfadeStationaryAvgUs / apoAvgUs) * 100.0);
    std::printf("| 4. APO Framework Overhead              | %13.3f | %11.4f%% | %13.2f%% |\n",
                wrapperOverheadUs, (wrapperOverheadUs / quantumDurationUs) * 100.0, (wrapperOverheadUs / apoAvgUs) * 100.0);
    std::printf("|----------------------------------------|---------------|--------------|----------------|\n");
    std::printf("| TOTAL APO REALTIME PROCESS (Active)    | %13.3f | %11.4f%% |        100.00%% |\n",
                apoAvgUs, (apoAvgUs / quantumDurationUs) * 100.0);
    std::printf("=================================================================================\n");
    std::printf("Note: Crossfader during active 8ms transition: %.3f us (%.4f%% of quantum)\n\n",
                xfadeTransitionAvgUs, (xfadeTransitionAvgUs / quantumDurationUs) * 100.0);

    apo->UnlockForProcess();
    apo->Release();
    return 0;
}

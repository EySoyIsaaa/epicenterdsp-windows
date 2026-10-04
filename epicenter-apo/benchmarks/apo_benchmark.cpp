#define _CRT_SECURE_NO_WARNINGS
#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/Signals.hpp"
#include "EpicenterDSP.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace epicenter::apo;
using namespace epicenter::apo::test;
using namespace epicenter_test;

namespace {

double threadCpuSeconds() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0.0;
    auto toU64 = [](FILETIME f) {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<double>(toU64(k) + toU64(u)) * 1e-7;
#else
    return 0.0;
#endif
}

struct Result {
    std::string mode;
    int sampleRate = 0;
    int channels = 0;
    int block = 0;
    std::size_t calls = 0;
    std::size_t frames = 0;
    double avgUs = 0, p50Us = 0, p99Us = 0, p999Us = 0, maxUs = 0;
    double avgLoad = 0, worstLoad = 0, realtimeFactor = 0;
    double wallSec = 0, cpuSec = 0;
};

double percentile(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    const std::size_t idx = std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(idx), v.end());
    return v[idx];
}

Result runApoBenchmark(const char* modeName, bool activeEffect, const Signal& source, int block, double seconds, int runs) {
    const int sr = source.sampleRate;
    const int chs = source.channels();

    auto* apo = new CEpicenterApo();
    CMockAudioMediaType mediaType(sr, static_cast<WORD>(chs), 32);

    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.u32MaxFrameCount = static_cast<UINT32>(block);
    inDesc.pFormat = &mediaType;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    apo->LockForProcess(1, inList, 1, outList);

    if (apo->ipcChannel().isOpen()) {
        epicenter::ipc::writeControlState(apo->ipcChannel().state(), activeEffect ? 1 : 0, 60.0f, 1);
    }

    std::vector<float> inInter(block * chs);
    std::vector<float> outInter(block * chs);

    APO_CONNECTION_PROPERTY inProp{};
    inProp.pBuffer = reinterpret_cast<UINT_PTR>(inInter.data());
    inProp.u32ValidFrameCount = static_cast<UINT32>(block);
    inProp.u32BufferFlags = BUFFER_VALID;

    APO_CONNECTION_PROPERTY outProp{};
    outProp.pBuffer = reinterpret_cast<UINT_PTR>(outInter.data());
    outProp.u32ValidFrameCount = 0;
    outProp.u32BufferFlags = BUFFER_INVALID;

    APO_CONNECTION_PROPERTY* inProps[] = { &inProp };
    APO_CONNECTION_PROPERTY* outProps[] = { &outProp };

    const std::size_t srcFrames = source.frames();
    std::size_t cursor = 0;

    auto fill = [&](int n) {
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < chs; ++c) {
                inInter[i * chs + c] = source.ch[c][cursor];
            }
            cursor = (cursor + 1) % srcFrames;
        }
    };

    // Warm-up: 1 s. (Also allows crossfader to settle at target state 1.0 or 0.0)
    const std::size_t warmCalls = static_cast<std::size_t>(sr / block) + 1;
    for (std::size_t k = 0; k < warmCalls; ++k) {
        fill(block);
        apo->APOProcess(1, inProps, 1, outProps);
    }

    const std::size_t callsPerRun = static_cast<std::size_t>(seconds * sr / block);
    std::vector<double> times;
    times.reserve(callsPerRun * static_cast<std::size_t>(runs));
    double wall = 0.0;
    const double cpu0 = threadCpuSeconds();

    for (int r = 0; r < runs; ++r) {
        for (std::size_t k = 0; k < callsPerRun; ++k) {
            fill(block);
            const auto t0 = std::chrono::steady_clock::now();
            apo->APOProcess(1, inProps, 1, outProps);
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            times.push_back(us);
            wall += us * 1e-6;
        }
    }
    const double cpu1 = threadCpuSeconds();

    apo->UnlockForProcess();
    apo->Release();

    Result res;
    res.mode = modeName;
    res.sampleRate = sr;
    res.channels = chs;
    res.block = block;
    res.calls = times.size();
    res.frames = times.size() * static_cast<std::size_t>(block);
    double sum = 0.0;
    for (double t : times) sum += t;
    res.avgUs = sum / static_cast<double>(times.size());
    res.maxUs = *std::max_element(times.begin(), times.end());
    res.p50Us = percentile(times, 0.50);
    res.p99Us = percentile(times, 0.99);
    res.p999Us = percentile(times, 0.999);
    const double blockUs = 1e6 * block / sr;
    res.avgLoad = res.avgUs / blockUs * 100.0;
    res.worstLoad = res.maxUs / blockUs * 100.0;
    res.wallSec = wall;
    res.cpuSec = cpu1 - cpu0;
    res.realtimeFactor = (static_cast<double>(res.frames) / sr) / wall;
    return res;
}

} // namespace

int main(int argc, char** argv) {
    double seconds = 5.0;
    int runs = 2;
    std::string csvPath;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) csvPath = argv[++i];
    }

#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
#endif

    const int rates[] = { 44100, 48000, 96000 };
    const int blocks[] = { 64, 128, 256, 512 };
    std::vector<Result> results;

    std::fprintf(stderr, "Starting Epicenter APO Benchmark...\n");

    for (int sr : rates) {
        const Signal music = syntheticMusic(sr, 10.0);
        for (int block : blocks) {
            // Benchmark Bypass
            results.push_back(runApoBenchmark("apo_bypass", false, music, block, seconds, runs));
            const Result& rBypass = results.back();
            std::fprintf(stderr, "  done %s %d Hz block %d (avg: %.2f us, load: %.3f%%)\n",
                         rBypass.mode.c_str(), sr, block, rBypass.avgUs, rBypass.avgLoad);

            // Benchmark Active Effect
            results.push_back(runApoBenchmark("apo_active", true, music, block, seconds, runs));
            const Result& rActive = results.back();
            std::fprintf(stderr, "  done %s %d Hz block %d (avg: %.2f us, load: %.3f%%)\n",
                         rActive.mode.c_str(), sr, block, rActive.avgUs, rActive.avgLoad);
        }
    }

    std::printf("\n=========================================================================================================\n");
    std::printf("                                 EPICENTER APO PERFORMANCE BENCHMARK\n");
    std::printf("=========================================================================================================\n");
    std::printf("| mode | rate | ch | block | calls | avg us | p50 us | p99 us | p99.9 us | max us | avg load %% | worst load %% | x realtime |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& r : results) {
        std::printf("| %s | %d | %d | %d | %zu | %.3f | %.3f | %.3f | %.3f | %.3f | %.4f | %.3f | %.0f |\n",
                    r.mode.c_str(), r.sampleRate, r.channels, r.block, r.calls, r.avgUs, r.p50Us, r.p99Us, r.p999Us,
                    r.maxUs, r.avgLoad, r.worstLoad, r.realtimeFactor);
    }
    std::printf("=========================================================================================================\n\n");

    if (!csvPath.empty()) {
        if (std::FILE* f = std::fopen(csvPath.c_str(), "w")) {
            std::fprintf(f, "mode,rate,channels,block,calls,avg_us,p50_us,p99_us,p999_us,max_us,avg_load_pct,worst_load_pct,realtime_factor,wall_s,thread_cpu_s\n");
            for (const auto& r : results) {
                std::fprintf(f, "%s,%d,%d,%d,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.4f,%.1f,%.4f,%.4f\n",
                             r.mode.c_str(), r.sampleRate, r.channels, r.block, r.calls, r.avgUs, r.p50Us, r.p99Us, r.p999Us,
                             r.maxUs, r.avgLoad, r.worstLoad, r.realtimeFactor, r.wallSec, r.cpuSec);
            }
            std::fclose(f);
            std::fprintf(stderr, "Benchmark CSV exported to: %s\n", csvPath.c_str());
        }
    }

    return 0;
}

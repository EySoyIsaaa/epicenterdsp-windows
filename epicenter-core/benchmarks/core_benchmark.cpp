// EpicenterDSP Core benchmark (BEFORE = frozen legacy core, AFTER = new epicenter_core).
//
//   core_benchmark [--seconds S] [--runs R] [--which legacy|new|both] [--csv file]
//
// For every (sample rate, block size) it processes S seconds of the deterministic synthetic
// music fixture, R times, timing every process() call with std::chrono::steady_clock.
// Reported per configuration:
//   avg / p50 / p99 / p99.9 / max time per call (microseconds)
//   avg CPU load   = avg call time  / block duration
//   worst CPU load = max call time  / block duration
//   realtime factor = audio duration / total processing wall time
//   thread CPU time (Windows: GetThreadTimes) vs wall time
//
// Reproducibility: fixed input, fixed parameters (enabled, intensity 60), 1 s warm-up, thread
// priority raised and pinned to one core on Windows. Absolute numbers depend on the machine;
// compare BEFORE vs AFTER from the same run.

#include "LegacyCore.hpp"
#include "Signals.hpp"

#if defined(EPICENTER_BENCH_WITH_NEW_CORE)
#include "EpicenterDSP.h"
#endif

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

using namespace epicenter_test;

namespace {

double threadCpuSeconds() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0.0;
    auto toU64 = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(toU64(k) + toU64(u)) * 1e-7;
#else
    return 0.0;
#endif
}

struct Result {
    std::string core;
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

// Generic benchmark loop. `processBlock(float* const* ptrs, int frames)` processes one block.
template <typename ProcessFn>
Result runConfig(const char* coreName, const Signal& source, int block, double seconds, int runs, ProcessFn&& processBlock) {
    const int sr = source.sampleRate;
    const int chs = source.channels();
    std::vector<std::vector<float>> work(static_cast<std::size_t>(chs), std::vector<float>(static_cast<std::size_t>(block)));
    float* ptrs[2] = {work[0].data(), work[chs > 1 ? 1 : 0].data()};
    const std::size_t srcFrames = source.frames();
    std::size_t cursor = 0;

    auto fill = [&](int n) {
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < chs; ++c) work[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = source.ch[static_cast<std::size_t>(c)][cursor];
            cursor = (cursor + 1) % srcFrames;
        }
    };

    // Warm-up: 1 s.
    const std::size_t warmCalls = static_cast<std::size_t>(sr / block) + 1;
    for (std::size_t k = 0; k < warmCalls; ++k) { fill(block); processBlock(ptrs, block); }

    const std::size_t callsPerRun = static_cast<std::size_t>(seconds * sr / block);
    std::vector<double> times;
    times.reserve(callsPerRun * static_cast<std::size_t>(runs));
    double wall = 0.0;
    const double cpu0 = threadCpuSeconds();
    for (int r = 0; r < runs; ++r) {
        for (std::size_t k = 0; k < callsPerRun; ++k) {
            fill(block); // input copy is outside the timed region
            const auto t0 = std::chrono::steady_clock::now();
            processBlock(ptrs, block);
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            times.push_back(us);
            wall += us * 1e-6;
        }
    }
    const double cpu1 = threadCpuSeconds();

    Result res;
    res.core = coreName;
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
    res.cpuSec = cpu1 - cpu0; // includes the (untimed) input copy
    res.realtimeFactor = (static_cast<double>(res.frames) / sr) / wall;
    return res;
}

} // namespace

int main(int argc, char** argv) {
    double seconds = 20.0;
    int runs = 3;
    std::string which = "both";
    std::string csvPath;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--which") && i + 1 < argc) which = argv[++i];
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) csvPath = argv[++i];
    }
#if !defined(EPICENTER_BENCH_WITH_NEW_CORE)
    which = "legacy";
#endif

#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
#endif

    const int rates[] = {44100, 48000, 96000};
    const int blocks[] = {32, 64, 128, 256, 512, 1024};
    std::vector<Result> results;

    for (int sr : rates) {
        const Signal music = syntheticMusic(sr, 10.0);
        for (int block : blocks) {
            if (which == "legacy" || which == "both") {
                LegacyCore legacy;
                legacy.prepare(sr, 2, static_cast<std::size_t>(block));
                legacy.configure(true, 60.0f);
                results.push_back(runConfig("legacy", music, block, seconds, runs,
                    [&](float* const* p, int n) { legacy.process(p, static_cast<std::size_t>(n)); }));
            }
#if defined(EPICENTER_BENCH_WITH_NEW_CORE)
            if (which == "new" || which == "both") {
                epicenter::EpicenterDSP dsp;
                if (!dsp.prepare(sr, 2, block)) { std::fprintf(stderr, "prepare failed\n"); return 2; }
                dsp.setEnabled(true);
                dsp.setIntensity(60.0f);
                results.push_back(runConfig("new", music, block, seconds, runs,
                    [&](float* const* p, int n) { dsp.process(p, n); }));
            }
#endif
            const Result& r = results.back();
            std::fprintf(stderr, "  done %s %d Hz block %d\n", r.core.c_str(), sr, block);
        }
    }

    std::printf("| core | rate | ch | block | calls | avg us | p50 us | p99 us | p99.9 us | max us | avg load %% | worst load %% | x realtime | wall s | thread cpu s |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& r : results) {
        std::printf("| %s | %d | %d | %d | %zu | %.3f | %.3f | %.3f | %.3f | %.3f | %.4f | %.3f | %.0f | %.3f | %.3f |\n",
                    r.core.c_str(), r.sampleRate, r.channels, r.block, r.calls, r.avgUs, r.p50Us, r.p99Us, r.p999Us,
                    r.maxUs, r.avgLoad, r.worstLoad, r.realtimeFactor, r.wallSec, r.cpuSec);
    }
    if (!csvPath.empty()) {
        if (std::FILE* f = std::fopen(csvPath.c_str(), "w")) {
            std::fprintf(f, "core,rate,channels,block,calls,avg_us,p50_us,p99_us,p999_us,max_us,avg_load_pct,worst_load_pct,realtime_factor,wall_s,thread_cpu_s\n");
            for (const auto& r : results)
                std::fprintf(f, "%s,%d,%d,%d,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.4f,%.1f,%.4f,%.4f\n", r.core.c_str(), r.sampleRate,
                             r.channels, r.block, r.calls, r.avgUs, r.p50Us, r.p99Us, r.p999Us, r.maxUs, r.avgLoad,
                             r.worstLoad, r.realtimeFactor, r.wallSec, r.cpuSec);
            std::fclose(f);
        }
    }
    return 0;
}

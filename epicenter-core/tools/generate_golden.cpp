// Generates the GOLDEN reference outputs from the FROZEN LEGACY core (the "OLD CORE").
//
//   generate_golden <inputsDir> <goldenDir>
//
// For every fixture in <inputsDir> and every reference intensity, processes the signal with
// the legacy core (enabled, factory voicing, 512-frame blocks) and writes
//   <goldenDir>/<fixture>_i<intensity>.wav      float32 output
//   <goldenDir>/baseline_metrics.csv            level statistics of every output
//
// This must be run BEFORE any change to the algorithm; the new core is then validated against
// these files by tests/test_regression.cpp.

#include "LegacyCore.hpp"
#include "Metrics.hpp"
#include "Signals.hpp"
#include "WavIO.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    using namespace epicenter_test;
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <inputsDir> <goldenDir>\n", argv[0]);
        return 1;
    }
    const std::string inDir = argv[1];
    const std::string outDir = argv[2];
    const int intensities[] = {60, 100};

    const std::string csvPath = outDir + "/baseline_metrics.csv";
    std::FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (!csv) { std::fprintf(stderr, "cannot write %s\n", csvPath.c_str()); return 2; }
    std::fprintf(csv, "fixture,intensity,in_peak,in_rms,out_peak,out_rms,out_peak_dbfs,out_rms_dbfs,full_scale_samples,non_finite\n");

    for (const auto& f : fixtureSet(48000, 2.0)) {
        Signal input;
        const std::string inPath = inDir + "/" + f.name + ".wav";
        if (!readWav(inPath, input)) { std::fprintf(stderr, "cannot read %s\n", inPath.c_str()); return 3; }
        const LevelStats inStats = levelStats(input);
        for (int intensity : intensities) {
            Signal out = input;
            LegacyCore core;
            core.prepare(input.sampleRate, input.channels(), 512);
            core.configure(true, static_cast<float>(intensity));
            core.processSignal(out, 512);
            const std::string outPath = outDir + "/" + f.name + "_i" + std::to_string(intensity) + ".wav";
            if (!writeWavFloat32(outPath, out)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4; }
            const LevelStats st = levelStats(out);
            std::fprintf(csv, "%s,%d,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f,%zu,%zu\n", f.name.c_str(), intensity, inStats.peak,
                         inStats.rms, st.peak, st.rms, toDb(st.peak), toDb(st.rms), st.atFullScale, st.nonFinite);
            std::printf("%-16s i=%3d  peak %7.2f dBFS  rms %7.2f dBFS  fullscale=%zu\n", f.name.c_str(), intensity,
                        toDb(st.peak), toDb(st.rms), st.atFullScale);
        }
    }
    std::fclose(csv);
    return 0;
}

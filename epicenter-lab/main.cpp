// Epicenter lab CLI.
//
//   epicenter_lab <input.wav> [out_prefix] [key=value ...]
//
// Writes three WAVs so you can A/B on the computer:
//   <prefix>_epicenter.wav  -> current Epicenter core only (baseline = what the app does)
//   <prefix>_enhanced.wav   -> Epicenter core + new BassGlue stage (saturation + compression)
//   <prefix>_glue.wav       -> BassGlue stage only (to hear the new effect in isolation)
//
// Tunable keys (defaults in brackets):
//   Epicenter: intensity[60] sweep[45] width[50] balance[100] volume[100]
//   BassGlue:  drive[2.2] depth[0.6] mix[0.75] split[160] outgain[0]
//
#include "EpicenterDSPCore.hpp"
#include "BassGlue.hpp"
#include "BassGlueV2.hpp"
#include "wav.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

static float getf(const std::map<std::string, float>& m, const char* k, float def) {
    auto it = m.find(k);
    return it == m.end() ? def : it->second;
}

static void runEpicenter(wav::Audio& a, const std::map<std::string, float>& p) {
    epicenter::EpicenterDSPCore dsp;
    dsp.prepare(a.sampleRate, a.channels, 8192);
    dsp.setEnabled(true);
    dsp.setParameters(getf(p, "intensity", 60), getf(p, "sweep", 45),
                      getf(p, "width", 50), getf(p, "balance", 100), getf(p, "volume", 100));
    float* ptrs[2] = { a.ch[0].data(), a.channels > 1 ? a.ch[1].data() : a.ch[0].data() };
    dsp.process(ptrs, a.channels, a.frames());
}

static void runGlueV2(wav::Audio& a, const std::map<std::string, float>& p) {
    lab::BassGlueV2 g;
    g.prepare(a.sampleRate, a.channels);
    lab::BassGlueV2Params gp;
    gp.subGen = getf(p, "subgen", 1.0f);
    gp.subBoostDb = getf(p, "subboost", 12.0f);
    gp.scoopDb = getf(p, "scoop", 8.0f);
    gp.subDepth = getf(p, "subdepth", 0.85f);
    gp.subGenHz = getf(p, "subgenhz", 90.0f);
    gp.deepHz = getf(p, "deephz", 58.0f);
    gp.monoHz = getf(p, "monohz", 70.0f);
    gp.fundFadeStartHz = getf(p, "fadestart", 90.0f);
    gp.fundFadeEndHz = getf(p, "fadeend", 125.0f);
    g.setParameters(gp);
    float* ptrs[2] = { a.ch[0].data(), a.channels > 1 ? a.ch[1].data() : a.ch[0].data() };
    g.process(ptrs, a.channels, a.frames());
}

static void runGlue(wav::Audio& a, const std::map<std::string, float>& p) {
    lab::BassGlue glue;
    glue.prepare(a.sampleRate, a.channels);
    lab::BassGlueParams gp;
    gp.drive = getf(p, "drive", 2.2f);
    gp.depth = getf(p, "depth", 0.6f);
    gp.mix = getf(p, "mix", 0.75f);
    gp.splitHz = getf(p, "split", 160.0f);
    gp.outGainDb = getf(p, "outgain", 0.0f);
    glue.setParameters(gp);
    float* ptrs[2] = { a.ch[0].data(), a.channels > 1 ? a.ch[1].data() : a.ch[0].data() };
    glue.process(ptrs, a.channels, a.frames());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <input.wav> [out_prefix] [key=value ...]\n", argv[0]);
        return 1;
    }
    const std::string input = argv[1];
    std::string prefix = (argc >= 3 && std::string(argv[2]).find('=') == std::string::npos) ? argv[2] : "out";

    std::map<std::string, float> p;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        auto eq = s.find('=');
        if (eq != std::string::npos) p[s.substr(0, eq)] = std::strtof(s.substr(eq + 1).c_str(), nullptr);
    }

    wav::Audio src;
    if (!wav::read(input, src)) { std::printf("error: cannot read WAV %s (feed a 16/24/32-bit PCM or float WAV)\n", input.c_str()); return 2; }
    std::printf("loaded %s: %d Hz, %d ch, %.1f s\n", input.c_str(), src.sampleRate, src.channels, src.frames() / float(src.sampleRate));

    // Variant 1: Epicenter only.
    { wav::Audio a = src; runEpicenter(a, p); wav::write16(prefix + "_epicenter.wav", a); }
    // Variant 2: Epicenter + BassGlue.
    { wav::Audio a = src; runEpicenter(a, p); runGlue(a, p); wav::write16(prefix + "_enhanced.wav", a); }
    // Variant 3: BassGlue only.
    { wav::Audio a = src; runGlue(a, p); wav::write16(prefix + "_glue.wav", a); }
    // Variant 4: BassGlueV2 — the "YouTube match" (deep-sub synth + scoop + hard comp + mono).
    { wav::Audio a = src; runGlueV2(a, p); wav::write16(prefix + "_ytmatch.wav", a); }

    std::printf("wrote %s_epicenter.wav, %s_enhanced.wav, %s_glue.wav\n", prefix.c_str(), prefix.c_str(), prefix.c_str());
    std::printf("epicenter: intensity=%.0f sweep=%.0f width=%.0f | glue: drive=%.2f depth=%.2f mix=%.2f split=%.0f\n",
                getf(p, "intensity", 60), getf(p, "sweep", 45), getf(p, "width", 50),
                getf(p, "drive", 2.2f), getf(p, "depth", 0.6f), getf(p, "mix", 0.75f), getf(p, "split", 160.0f));
    return 0;
}

// PRIVATE implementation of the Epicenter algorithm.
//
// Provenance: moved from epicenter-lab/EpicenterDSPCore.cpp (legacy prototype). The per-sample
// arithmetic of processChunk(), the filter/envelope set-up and the reset logic are copied
// expression-for-expression; tests/test_regression.cpp proves bit-exact equivalence against
// the frozen legacy copy in tests/reference/legacy/. Changes vs legacy are structural only
// (see EPICENTER_CORE_CHANGELOG.md):
//   * run-time sweep/width/balance/volume parameters -> private factory voicing constants
//   * std::vector<ChannelState> grown on demand in process() -> fixed array sized at compile time
//     (removes the only heap allocation the legacy core could perform on the audio thread)
//   * parameters are passed in by the caller (atomics live in the public front-end)

#include "EpicenterProcessor.hpp"

#include <algorithm>
#include <cmath>

namespace epicenter {
namespace core_internal {
namespace {
// ---- Algorithm tuning (legacy values, unchanged) ---------------------------------------
constexpr float EPICENTER_INTENSITY_HEADROOM = 0.75f;
constexpr float EPICENTER_INTENSITY_MAX_SCALE = 0.65f;
constexpr float EPICENTER_VOLUME_MAX_SCALE = 0.75f;
constexpr float EPICENTER_OUTPUT_TRIM = 0.95f;
constexpr float SYNTH_DEPTH_GAIN = 1.18f;
constexpr float DEEP_EXTENSION_AMOUNT = 0.36f;
constexpr float GATE_DETECTOR_FLOOR = 0.40f;
constexpr float GATE_DETECTOR_AUTHORITY = 0.22f;
constexpr float OUTPUT_DC_HIGHPASS_HZ = 26.0f;
constexpr float DEEP_EXTENSION_SUBSONIC_HIGHPASS_HZ = 23.0f;
constexpr float DEEP_EXTENSION_MIX_BASE = 0.46f;
constexpr float DEEP_EXTENSION_MIX_VOICE = 0.58f;
// Below this intensity the legacy core takes the bypass path.
constexpr float BYPASS_INTENSITY_THRESHOLD = 0.01f;
// Minimum internal chunk size (legacy behaviour).
constexpr std::size_t MIN_INTERNAL_FRAMES = 128;

// ---- Factory voicing ----------------------------------------------------------------------
// Formerly public run-time parameters; every existing host passed exactly these values.
constexpr float VOICING_SWEEP_FREQ = 45.0f;
constexpr float VOICING_WIDTH = 50.0f;
constexpr float VOICING_BALANCE = 100.0f;
constexpr float VOICING_VOLUME = 100.0f;

const float SOFT_CLIP_09 = softClip(0.9f);
} // namespace

void EpicenterProcessor::prepare(double sampleRate, int channelCount, std::size_t maxFrames) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    channelCount_ = std::max(1, std::min(channelCount, kMaxChannels));
    maxFrames_ = std::max<std::size_t>(MIN_INTERNAL_FRAMES, maxFrames);
    subBuffer_.assign(maxFrames_, 0.0f);
    deepExtensionBuffer_.assign(maxFrames_, 0.0f);
    for (auto& plane : interleaveScratch_) plane.assign(maxFrames_, 0.0f);
    const auto d = getDerivedFrequencies(VOICING_SWEEP_FREQ, VOICING_WIDTH);
    for (auto& ch : channels_) ch = createChannelState(d);
    monoState_ = createMonoState(d);
}

void EpicenterProcessor::reset() {
    for (auto& ch : channels_) {
        ch.voiceHighpass.reset(); ch.voicePresenceHighpass.reset(); ch.bassLowpass.reset(); ch.lowMidBody.reset(); ch.lowMidDip.reset(); ch.subLowpass.reset(); ch.bassBoostShelf.reset(); ch.outputDcHighpass.reset(); ch.voiceEnv.reset();
    }
    monoState_ = createMonoState(getDerivedFrequencies(VOICING_SWEEP_FREQ, VOICING_WIDTH));
}

void EpicenterProcessor::process(float* const* channels, std::size_t frameCount, bool enabled, float intensity) {
    const int channelCount = channelCount_;
    if (!channels || channelCount <= 0 || frameCount == 0) return;
    if (!enabled || intensity <= BYPASS_INTENSITY_THRESHOLD) {
        for (int ch = 0; ch < channelCount; ++ch) for (std::size_t i = 0; i < frameCount; ++i) channels[ch][i] = denormalFloor(channels[ch][i]);
        return;
    }
    std::size_t offset = 0;
    while (offset < frameCount) {
        const std::size_t n = std::min(maxFrames_, frameCount - offset);
        float* ptrs[2] = { channels[0] + offset, (channelCount > 1 ? channels[1] + offset : channels[0] + offset) };
        processChunk(ptrs, channelCount, n, intensity);
        offset += n;
    }
}

EpicenterProcessor::DerivedFrequencies EpicenterProcessor::getDerivedFrequencies(float sweepFreq, float width) {
    const float sweepNorm = (clampf(sweepFreq, 27, 63) - 27.0f) / 36.0f;
    const float widthNorm = clampf(width, 0, 100) / 100.0f;
    return {55 + sweepNorm * 10, 75 + sweepNorm * 10, 100 + sweepNorm * 15, 105 + widthNorm * 30, 95 + sweepNorm * 20, 56 + widthNorm * 8, 48 + widthNorm * 8, 16 + sweepNorm * 4, 30 + widthNorm * 10};
}

EpicenterProcessor::ChannelState EpicenterProcessor::createChannelState(const DerivedFrequencies& d) const {
    ChannelState s;
    s.voiceHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, d.crossoverHz, 0.707f);
    s.voicePresenceHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, std::max(170.0f, d.crossoverHz + 40.0f), 0.707f);
    s.bassLowpass.prepare(sampleRate_, BiquadFilter::Type::Lowpass, d.crossoverHz * 1.15f, 0.707f);
    s.lowMidBody.prepare(sampleRate_, BiquadFilter::Type::Bandpass, d.bodyHz, 0.85f);
    s.lowMidDip.prepare(sampleRate_, BiquadFilter::Type::Bandpass, d.bodyHz * 1.18f, 1.1f);
    s.subLowpass.prepare(sampleRate_, BiquadFilter::Type::Lowpass, d.subTopHz, 0.707f);
    s.bassBoostShelf.prepare(sampleRate_);
    s.outputDcHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, OUTPUT_DC_HIGHPASS_HZ, 0.707f);
    s.voiceEnv.prepare(sampleRate_, 6, 110);
    return s;
}

EpicenterProcessor::MonoState EpicenterProcessor::createMonoState(const DerivedFrequencies& d) const {
    MonoState s;
    s.band60.prepare(sampleRate_, BiquadFilter::Type::Bandpass, d.detector60, 1.35f);
    s.band80.prepare(sampleRate_, BiquadFilter::Type::Bandpass, d.detector80, 1.55f);
    s.band110.prepare(sampleRate_, BiquadFilter::Type::Bandpass, d.detector110, 1.8f);
    s.monoLowpass.prepare(sampleRate_, BiquadFilter::Type::Lowpass, 120, 0.707f);
    s.diffHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, 140, 0.707f);
    s.synthHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, d.synthHighHz, 0.707f);
    s.synthLowpass.prepare(sampleRate_, BiquadFilter::Type::Lowpass, d.synthLowHz, 0.707f);
    s.deepExtensionLowpass.prepare(sampleRate_, BiquadFilter::Type::Lowpass, d.deepExtensionHz, 0.707f);
    s.deepExtensionSubsonicHighpass.prepare(sampleRate_, BiquadFilter::Type::Highpass, DEEP_EXTENSION_SUBSONIC_HIGHPASS_HZ, 0.707f);
    s.detectorEnv.prepare(sampleRate_, 7, 95); s.monoEnv.prepare(sampleRate_, 12, 160); s.diffEnv.prepare(sampleRate_, 12, 160);
    s.gateEnv.prepare(sampleRate_, 25, 240); s.synthLevelEnv.prepare(sampleRate_, 18, 180); s.deepExtensionEnv.prepare(sampleRate_, 24, 420);
    return s;
}

float EpicenterProcessor::computeGate(float monoEnv, float diffEnv, float weightedDetectorEnv) {
    const float musicRatio = diffEnv / (monoEnv + 1.0e-6f);
    const float detectorActivity = std::min(1.0f, weightedDetectorEnv * 10.5f);
    const float musicScore = clampf(musicRatio * 3.2f, 0, 1);
    const float musicalGate = detectorActivity * (GATE_DETECTOR_FLOOR + musicScore * (1.0f - GATE_DETECTOR_FLOOR));
    const float detectorSustain = std::max(0.0f, detectorActivity - 0.50f) * GATE_DETECTOR_AUTHORITY;
    return std::min(1.0f, std::max(musicalGate, detectorSustain));
}

void EpicenterProcessor::processChunk(float* const* input, int channelCount, std::size_t blockSize, float intensity) {
    const float intensityRawNorm = clampf(intensity, 0, 100) / 100.0f;
    const float intensityProgress = std::pow(intensityRawNorm, 0.82f);
    const float intensityScaledNorm = intensityProgress * EPICENTER_INTENSITY_MAX_SCALE;
    const float intensityNorm = intensityScaledNorm * EPICENTER_INTENSITY_HEADROOM;
    const float balanceNorm = clampf(VOICING_BALANCE, 0, 100) / 100.0f;
    const float widthNorm = clampf(VOICING_WIDTH, 0, 100) / 100.0f;
    const float volumeGain = clampf((VOICING_VOLUME / 100.0f) * EPICENTER_VOLUME_MAX_SCALE, 0, 1);
    const float bassBoostFreqHz = 48 + widthNorm * 8;
    const float bassBoostGainDb = intensityScaledNorm * 7.4f;
    const float synthAmount = (0.45f + intensityNorm * 1.24f) * 1.16f * SYNTH_DEPTH_GAIN;
    const float bassProgramAmount = 0.58f + balanceNorm * 0.26f;
    const float lowMidBodyAmount = 0.12f + balanceNorm * 0.08f;
    const float lowMidDipAmount = (0.08f + intensityNorm * 0.16f) * (0.45f + widthNorm * 0.3f);
    const int gateHoldSamples = static_cast<int>(std::floor(sampleRate_ * (0.025 + intensityNorm * 0.06)));

    for (std::size_t i = 0; i < blockSize; ++i) {
        const float left = denormalFloor(input[0][i]);
        const float right = channelCount > 1 ? denormalFloor(input[1][i]) : left;
        const float mono = denormalFloor((left + right) * 0.5f);
        const float diff = denormalFloor((left - right) * 0.5f);
        const float monoBand = monoState_.band60.process(mono) * 1.0f + monoState_.band80.process(mono) * 0.68f + monoState_.band110.process(mono) * 0.42f;
        const float weightedDetector = denormalFloor(monoBand * 0.6f + monoState_.monoLowpass.process(mono) * 0.12f);
        const float detectorEnv = monoState_.detectorEnv.process(weightedDetector);
        const float monoEnv = monoState_.monoEnv.process(mono);
        const float diffEnv = monoState_.diffEnv.process(monoState_.diffHighpass.process(diff));
        if (monoState_.lastDetector <= 0 && weightedDetector > 0) monoState_.flipState *= -1.0f;
        monoState_.lastDetector = weightedDetector;
        const float rawHalf = monoState_.flipState * detectorEnv;
        float synth = monoState_.synthLowpass.process(monoState_.synthHighpass.process(rawHalf));
        const float gateTarget = computeGate(monoEnv, diffEnv, detectorEnv);
        const float gateValue = monoState_.gateEnv.process(gateTarget);
        if (gateTarget > 0.3f) monoState_.holdSamples = gateHoldSamples; else if (monoState_.holdSamples > 0) monoState_.holdSamples--;
        const float remixGate = std::max(gateValue, (monoState_.holdSamples > 0 ? 1.0f : 0.0f) * 0.45f);
        const float leveledSynth = monoState_.synthLevelEnv.process(synth) * (synth < 0 ? -1.0f : 1.0f);
        const float protectedSynth = softClip((synth * 0.64f + leveledSynth * 0.36f) * 1.98f) * 0.71f;
        subBuffer_[i] = denormalFloor(protectedSynth * synthAmount * remixGate);
    }
    const float deepExtensionAmount = DEEP_EXTENSION_AMOUNT * intensityProgress * (0.74f + intensityScaledNorm * 0.26f);
    for (std::size_t i = 0; i < blockSize; ++i) {
        const float deepLow = monoState_.deepExtensionLowpass.process(subBuffer_[i]);
        const float deepProtected = monoState_.deepExtensionSubsonicHighpass.process(deepLow);
        const float deepSustain = monoState_.deepExtensionEnv.process(deepProtected) * (deepProtected < 0 ? -1.0f : 1.0f);
        deepExtensionBuffer_[i] = denormalFloor(softClip((deepProtected * 0.72f + deepSustain * 0.28f) * deepExtensionAmount));
    }
    for (int ch = 0; ch < channelCount; ++ch) {
        auto& s = channels_[ch];
        s.bassBoostShelf.update(bassBoostFreqHz, bassBoostGainDb);
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float sample = denormalFloor(input[ch][i]);
            const float voicePath = s.voiceHighpass.process(sample);
            const float cleanVoicePath = s.voicePresenceHighpass.process(voicePath);
            const float voicePresence = s.voiceEnv.process(cleanVoicePath);
            const float voiceProtection = std::max(0.56f, 1.0f - voicePresence * (0.9f + intensityNorm * 0.34f));
            const float bassProgram = s.bassLowpass.process(sample);
            const float body = s.lowMidBody.process(sample);
            const float dip = s.lowMidDip.process(sample);
            const float shapedBassProgram = bassProgram * bassProgramAmount + body * lowMidBodyAmount * (0.45f + voiceProtection * 0.55f) - dip * lowMidDipAmount;
            const float generatedSub = s.subLowpass.process(subBuffer_[i]) * (0.48f + voiceProtection * 0.62f) + deepExtensionBuffer_[i] * (DEEP_EXTENSION_MIX_BASE + voiceProtection * DEEP_EXTENSION_MIX_VOICE);
            float mixed = cleanVoicePath + shapedBassProgram + generatedSub;
            mixed = s.bassBoostShelf.process(mixed);
            const float protectionGain = 0.94f + voiceProtection * 0.06f;
            mixed *= volumeGain * protectionGain * EPICENTER_OUTPUT_TRIM;
            mixed = softClip(mixed * 0.9f) / SOFT_CLIP_09;
            mixed = s.outputDcHighpass.process(mixed);
            input[ch][i] = clampf(denormalFloor(mixed), -1.0f, 1.0f);
        }
    }
}

} // namespace core_internal
} // namespace epicenter

#include "EpicenterDSPCore.hpp"
#include <algorithm>
#include <cmath>

namespace epicenter {
namespace {
constexpr float DENORMAL_FLOOR = 1.0e-24f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float EPICENTER_INTENSITY_HEADROOM = 0.75f;
constexpr float EPICENTER_INTENSITY_MAX_SCALE = 0.65f;
constexpr float EPICENTER_VOLUME_MAX_SCALE = 0.75f;
constexpr float EPICENTER_OUTPUT_TRIM = 0.95f;
constexpr float SUB_DEPTH = 1.0f;
constexpr float DEEP_EXTENSION_AMOUNT = 0.36f;
constexpr float SYNTH_DEPTH_GAIN = 1.18f;
constexpr float GATE_DETECTOR_FLOOR = 0.40f;
constexpr float GATE_DETECTOR_AUTHORITY = 0.22f;
constexpr float OUTPUT_DC_HIGHPASS_HZ = 26.0f;
constexpr float DEEP_EXTENSION_SUBSONIC_HIGHPASS_HZ = 23.0f;
constexpr float DEEP_EXTENSION_MIX_BASE = 0.46f;
constexpr float DEEP_EXTENSION_MIX_VOICE = 0.58f;

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
inline float denormalFloor(float v) { return (std::fabs(v) < DENORMAL_FLOOR || !std::isfinite(v)) ? 0.0f : v; }
inline float softClip(float value) {
    const float x2 = value * value;
    return (value * (27.0f + x2)) / (27.0f + 9.0f * x2);
}
const float SOFT_CLIP_09 = softClip(0.9f);
inline float coeffFromMs(double sr, float ms) {
    const double samples = std::max(1.0, static_cast<double>(ms) * sr / 1000.0);
    return static_cast<float>(std::exp(-1.0 / samples));
}
}

void BiquadFilter::prepare(double sampleRate, Type type, float freq, float q) {
    sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    updateCoeffs(type, freq, q);
    reset();
}

void BiquadFilter::updateCoeffs(Type type, float freq, float q) {
    type_ = type;
    const float clampedFreq = clampf(freq, 10.0f, static_cast<float>(sr_ * 0.45));
    const float clampedQ = clampf(q, 0.2f, 12.0f);
    const float omega = TWO_PI * clampedFreq / static_cast<float>(sr_);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * clampedQ);
    float b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
        case Type::Lowpass:
            b0 = (1 - cosOmega) * 0.5f; b1 = 1 - cosOmega; b2 = (1 - cosOmega) * 0.5f;
            a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
            break;
        case Type::Highpass:
            b0 = (1 + cosOmega) * 0.5f; b1 = -(1 + cosOmega); b2 = (1 + cosOmega) * 0.5f;
            a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
            break;
        case Type::Bandpass:
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cosOmega; a2 = 1 - alpha;
            break;
    }
    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
}

float BiquadFilter::process(float sample) {
    const float clean = denormalFloor(sample);
    const float y0 = denormalFloor(b0_ * clean + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_);
    x2_ = denormalFloor(x1_); x1_ = clean; y2_ = denormalFloor(y1_); y1_ = y0;
    return y0;
}

void BiquadFilter::reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

void LowShelfFilter::prepare(double sampleRate) { sr_ = sampleRate > 1.0 ? sampleRate : 44100.0; reset(); }

void LowShelfFilter::update(float freqHz, float gainDb) {
    const float clampedFreq = clampf(freqHz, 20.0f, static_cast<float>(sr_ * 0.45));
    const float clampedGain = clampf(gainDb, 0.0f, 10.5f);
    if (std::fabs(clampedFreq - freqHz_) < 1.0e-3f && std::fabs(clampedGain - gainDb_) < 1.0e-3f) return;
    freqHz_ = clampedFreq; gainDb_ = clampedGain;
    const float A = std::pow(10.0f, clampedGain / 40.0f);
    const float w0 = TWO_PI * clampedFreq / static_cast<float>(sr_);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * 0.707f);
    const float sqrtA = std::sqrt(A);
    float b0 = A * ((A + 1) - (A - 1) * cosW0 + 2 * sqrtA * alpha);
    float b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
    float b2 = A * ((A + 1) - (A - 1) * cosW0 - 2 * sqrtA * alpha);
    const float a0 = (A + 1) + (A - 1) * cosW0 + 2 * sqrtA * alpha;
    float a1 = -2 * ((A - 1) + (A + 1) * cosW0);
    float a2 = (A + 1) + (A - 1) * cosW0 - 2 * sqrtA * alpha;
    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
}

float LowShelfFilter::process(float sample) {
    const float input = denormalFloor(sample);
    const float out = denormalFloor(b0_ * input + z1_);
    z1_ = denormalFloor(b1_ * input - a1_ * out + z2_);
    z2_ = denormalFloor(b2_ * input - a2_ * out);
    return out;
}
void LowShelfFilter::reset() { z1_ = z2_ = 0.0f; freqHz_ = -1.0f; gainDb_ = -999.0f; }

void EnvelopeFollower::prepare(double sampleRate, float attackMs, float releaseMs) {
    attackCoeff_ = coeffFromMs(sampleRate, attackMs);
    releaseCoeff_ = coeffFromMs(sampleRate, releaseMs);
    reset();
}
float EnvelopeFollower::process(float input) {
    const float x = std::fabs(denormalFloor(input));
    const float coeff = x > value_ ? attackCoeff_ : releaseCoeff_;
    value_ = denormalFloor(x + coeff * (value_ - x));
    return value_;
}
void EnvelopeFollower::reset() { value_ = 0.0f; }

EpicenterDSPCore::EpicenterDSPCore() = default;

void EpicenterDSPCore::prepare(double sampleRate, int channelCount, std::size_t maxFrames) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    channelCount_ = std::max(1, std::min(channelCount, 2));
    maxFrames_ = std::max<std::size_t>(128, maxFrames);
    subBuffer_.assign(maxFrames_, 0.0f);
    deepExtensionBuffer_.assign(maxFrames_, 0.0f);
    channels_.clear(); channels_.reserve(static_cast<std::size_t>(channelCount_));
    monoStateReady_ = false; lastSweepFreq_ = -1; lastWidth_ = -1;
    ensureState(channelCount_, sweepFreq_.load(), width_.load());
}
void EpicenterDSPCore::reset() {
    for (auto& ch : channels_) {
        ch.voiceHighpass.reset(); ch.voicePresenceHighpass.reset(); ch.bassLowpass.reset(); ch.lowMidBody.reset(); ch.lowMidDip.reset(); ch.subLowpass.reset(); ch.bassBoostShelf.reset(); ch.outputDcHighpass.reset(); ch.voiceEnv.reset();
    }
    monoState_ = createMonoState(getDerivedFrequencies(sweepFreq_.load(), width_.load()));
}
void EpicenterDSPCore::setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
void EpicenterDSPCore::setParameters(float intensity, float sweepFreq, float width, float balance, float volume) {
    intensity_.store(clampf(intensity, 0, 100), std::memory_order_release);
    sweepFreq_.store(clampf(sweepFreq, 27, 63), std::memory_order_release);
    width_.store(clampf(width, 0, 100), std::memory_order_release);
    balance_.store(clampf(balance, 0, 100), std::memory_order_release);
    volume_.store(clampf(volume, 0, 100), std::memory_order_release);
}
EpicenterDSPParameters EpicenterDSPCore::parameters() const {
    return { enabled_.load(), intensity_.load(), sweepFreq_.load(), width_.load(), balance_.load(), volume_.load() };
}

EpicenterDSPCalibration EpicenterDSPCore::calibration() const {
    return {
        SUB_DEPTH,
        DEEP_EXTENSION_AMOUNT,
        SYNTH_DEPTH_GAIN,
        GATE_DETECTOR_FLOOR,
        GATE_DETECTOR_AUTHORITY,
        OUTPUT_DC_HIGHPASS_HZ,
        DEEP_EXTENSION_SUBSONIC_HIGHPASS_HZ,
        DEEP_EXTENSION_MIX_BASE,
        DEEP_EXTENSION_MIX_VOICE,
    };
}

void EpicenterDSPCore::process(float* const* channels, int channelCount, std::size_t frameCount) {
    if (!channels || channelCount <= 0 || frameCount == 0) return;
    const auto params = parameters();
    if (!params.enabled || params.intensity <= 0.01f) {
        for (int ch = 0; ch < channelCount; ++ch) for (std::size_t i = 0; i < frameCount; ++i) channels[ch][i] = denormalFloor(channels[ch][i]);
        return;
    }
    std::size_t offset = 0;
    while (offset < frameCount) {
        const std::size_t n = std::min(maxFrames_, frameCount - offset);
        float* ptrs[2] = { channels[0] + offset, (channelCount > 1 ? channels[1] + offset : channels[0] + offset) };
        processChunk(ptrs, std::min(channelCount, 2), n, params);
        offset += n;
    }
}

EpicenterDSPCore::DerivedFrequencies EpicenterDSPCore::getDerivedFrequencies(float sweepFreq, float width) const {
    const float sweepNorm = (clampf(sweepFreq, 27, 63) - 27.0f) / 36.0f;
    const float widthNorm = clampf(width, 0, 100) / 100.0f;
    return {55 + sweepNorm * 10, 75 + sweepNorm * 10, 100 + sweepNorm * 15, 105 + widthNorm * 30, 95 + sweepNorm * 20, 56 + widthNorm * 8, 48 + widthNorm * 8, 16 + sweepNorm * 4, 30 + widthNorm * 10};
}

EpicenterDSPCore::ChannelState EpicenterDSPCore::createChannelState(const DerivedFrequencies& d) {
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

EpicenterDSPCore::MonoState EpicenterDSPCore::createMonoState(const DerivedFrequencies& d) {
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

void EpicenterDSPCore::ensureState(int channelCount, float sweepFreq, float width) {
    const auto d = getDerivedFrequencies(sweepFreq, width);
    while (static_cast<int>(channels_.size()) < channelCount) channels_.push_back(createChannelState(d));
    if (!monoStateReady_) { monoState_ = createMonoState(d); monoStateReady_ = true; lastSweepFreq_ = sweepFreq; lastWidth_ = width; return; }
    if (sweepFreq == lastSweepFreq_ && width == lastWidth_) return;
    for (auto& s : channels_) {
        s.voiceHighpass.updateCoeffs(BiquadFilter::Type::Highpass, d.crossoverHz, 0.707f);
        s.voicePresenceHighpass.updateCoeffs(BiquadFilter::Type::Highpass, std::max(170.0f, d.crossoverHz + 40.0f), 0.707f);
        s.bassLowpass.updateCoeffs(BiquadFilter::Type::Lowpass, d.crossoverHz * 1.15f, 0.707f);
        s.lowMidBody.updateCoeffs(BiquadFilter::Type::Bandpass, d.bodyHz, 0.85f);
        s.lowMidDip.updateCoeffs(BiquadFilter::Type::Bandpass, d.bodyHz * 1.18f, 1.1f);
        s.subLowpass.updateCoeffs(BiquadFilter::Type::Lowpass, d.subTopHz, 0.707f);
    }
    monoState_.band60.updateCoeffs(BiquadFilter::Type::Bandpass, d.detector60, 1.35f);
    monoState_.band80.updateCoeffs(BiquadFilter::Type::Bandpass, d.detector80, 1.55f);
    monoState_.band110.updateCoeffs(BiquadFilter::Type::Bandpass, d.detector110, 1.8f);
    monoState_.synthHighpass.updateCoeffs(BiquadFilter::Type::Highpass, d.synthHighHz, 0.707f);
    monoState_.synthLowpass.updateCoeffs(BiquadFilter::Type::Lowpass, d.synthLowHz, 0.707f);
    monoState_.deepExtensionLowpass.updateCoeffs(BiquadFilter::Type::Lowpass, d.deepExtensionHz, 0.707f);
    lastSweepFreq_ = sweepFreq; lastWidth_ = width;
}

float EpicenterDSPCore::computeGate(float monoEnv, float diffEnv, float weightedDetectorEnv) const {
    const float musicRatio = diffEnv / (monoEnv + 1.0e-6f);
    const float detectorActivity = std::min(1.0f, weightedDetectorEnv * 10.5f);
    const float musicScore = clampf(musicRatio * 3.2f, 0, 1);
    const float musicalGate = detectorActivity * (GATE_DETECTOR_FLOOR + musicScore * (1.0f - GATE_DETECTOR_FLOOR));
    const float detectorSustain = std::max(0.0f, detectorActivity - 0.50f) * GATE_DETECTOR_AUTHORITY;
    return std::min(1.0f, std::max(musicalGate, detectorSustain));
}

void EpicenterDSPCore::processChunk(float* const* input, int channelCount, std::size_t blockSize, const EpicenterDSPParameters& p) {
    ensureState(channelCount, p.sweepFreq, p.width);
    const float intensityRawNorm = clampf(p.intensity, 0, 100) / 100.0f;
    const float intensityProgress = std::pow(intensityRawNorm, 0.82f);
    const float intensityScaledNorm = intensityProgress * EPICENTER_INTENSITY_MAX_SCALE;
    const float intensityNorm = intensityScaledNorm * EPICENTER_INTENSITY_HEADROOM;
    const float balanceNorm = clampf(p.balance, 0, 100) / 100.0f;
    const float widthNorm = clampf(p.width, 0, 100) / 100.0f;
    const float volumeGain = clampf((p.volume / 100.0f) * EPICENTER_VOLUME_MAX_SCALE, 0, 1);
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
        auto& s = channels_[static_cast<std::size_t>(ch)];
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

} // namespace epicenter

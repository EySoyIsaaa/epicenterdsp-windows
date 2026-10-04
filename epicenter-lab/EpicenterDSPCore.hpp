#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace epicenter {

struct EpicenterDSPParameters {
    bool enabled = false;
    float intensity = 100.0f;
    float sweepFreq = 45.0f;
    float width = 50.0f;
    float balance = 100.0f;
    float volume = 100.0f;
};

struct EpicenterDSPCalibration {
    float subDepth = 1.0f;
    float deepExtensionAmount = 0.36f;
    float synthDepthGain = 1.18f;
    float gateDetectorFloor = 0.40f;
    float gateDetectorAuthority = 0.22f;
    float outputDcHighpassHz = 26.0f;
    float deepExtensionSubsonicHighpassHz = 23.0f;
    float deepExtensionMixBase = 0.46f;
    float deepExtensionMixVoice = 0.58f;
};

class BiquadFilter {
public:
    enum class Type { Lowpass, Highpass, Bandpass };
    void prepare(double sampleRate, Type type, float freq, float q = 0.707f);
    void updateCoeffs(Type type, float freq, float q = 0.707f);
    float process(float sample);
    void reset();
private:
    double sr_ = 44100.0;
    Type type_ = Type::Lowpass;
    float b0_ = 0, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

class LowShelfFilter {
public:
    void prepare(double sampleRate);
    void update(float freqHz, float gainDb);
    float process(float sample);
    void reset();
private:
    double sr_ = 44100.0;
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float z1_ = 0, z2_ = 0;
    float freqHz_ = -1, gainDb_ = -999;
};

class EnvelopeFollower {
public:
    void prepare(double sampleRate, float attackMs, float releaseMs);
    float process(float input);
    void reset();
private:
    float value_ = 0;
    float attackCoeff_ = 0;
    float releaseCoeff_ = 0;
};

class EpicenterDSPCore {
public:
    EpicenterDSPCore();
    void prepare(double sampleRate, int channelCount, std::size_t maxFrames = 8192);
    void reset();
    void setEnabled(bool enabled);
    void setParameters(float intensity, float sweepFreq, float width, float balance, float volume);
    EpicenterDSPParameters parameters() const;
    EpicenterDSPCalibration calibration() const;
    void process(float* const* channels, int channelCount, std::size_t frameCount);
private:
    struct ChannelState {
        BiquadFilter voiceHighpass;
        BiquadFilter voicePresenceHighpass;
        BiquadFilter bassLowpass;
        BiquadFilter lowMidBody;
        BiquadFilter lowMidDip;
        BiquadFilter subLowpass;
        LowShelfFilter bassBoostShelf;
        BiquadFilter outputDcHighpass;
        EnvelopeFollower voiceEnv;
    };
    struct MonoState {
        BiquadFilter band60;
        BiquadFilter band80;
        BiquadFilter band110;
        BiquadFilter monoLowpass;
        BiquadFilter diffHighpass;
        BiquadFilter synthHighpass;
        BiquadFilter synthLowpass;
        BiquadFilter deepExtensionLowpass;
        BiquadFilter deepExtensionSubsonicHighpass;
        EnvelopeFollower detectorEnv;
        EnvelopeFollower monoEnv;
        EnvelopeFollower diffEnv;
        EnvelopeFollower gateEnv;
        EnvelopeFollower synthLevelEnv;
        EnvelopeFollower deepExtensionEnv;
        float lastDetector = 0;
        float flipState = 1;
        int holdSamples = 0;
    };
    struct DerivedFrequencies {
        float detector60;
        float detector80;
        float detector110;
        float crossoverHz;
        float bodyHz;
        float subTopHz;
        float synthLowHz;
        float synthHighHz;
        float deepExtensionHz;
    };

    void processChunk(float* const* channels, int channelCount, std::size_t frameCount, const EpicenterDSPParameters& params);
    DerivedFrequencies getDerivedFrequencies(float sweepFreq, float width) const;
    void ensureState(int channelCount, float sweepFreq, float width);
    ChannelState createChannelState(const DerivedFrequencies& derived);
    MonoState createMonoState(const DerivedFrequencies& derived);
    float computeGate(float monoEnv, float diffEnv, float weightedDetectorEnv) const;

    double sampleRate_ = 44100.0;
    int channelCount_ = 2;
    std::size_t maxFrames_ = 8192;
    std::vector<ChannelState> channels_;
    MonoState monoState_;
    bool monoStateReady_ = false;
    std::vector<float> subBuffer_;
    std::vector<float> deepExtensionBuffer_;
    float lastSweepFreq_ = -1;
    float lastWidth_ = -1;

    std::atomic<bool> enabled_ { false };
    std::atomic<float> intensity_ { 100.0f };
    std::atomic<float> sweepFreq_ { 45.0f };
    std::atomic<float> width_ { 50.0f };
    std::atomic<float> balance_ { 100.0f };
    std::atomic<float> volume_ { 100.0f };
};

} // namespace epicenter

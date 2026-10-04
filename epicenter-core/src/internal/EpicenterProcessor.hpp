#pragma once
// PRIVATE — not installed, not part of the public API.
//
// The Epicenter low-frequency reconstruction algorithm. Signal flow (per process call):
//
//   1. Detector / synthesis (mono sum, shared by both channels)
//        mono = (L+R)/2, side = (L-R)/2
//        3 band-pass detectors around 60/80/110 Hz + mono low-pass  -> weighted detector
//        envelope of detector; zero-crossing flip-flop on the detector (frequency divider)
//        -> half-frequency square-ish carrier * detector envelope -> band-limited "synth"
//        music/voice gate from mono vs side envelopes + hold; level-follower + soft clip
//        -> subBuffer (generated sub-harmonic)
//   2. Deep extension: low-pass + subsonic high-pass + sustain envelope of subBuffer
//        -> deepExtensionBuffer
//   3. Per channel recombination
//        voice path (high-passed program), presence envelope -> voice protection
//        shaped bass program (low-pass + low-mid body - low-mid dip)
//        + generated sub (low-passed) + deep extension
//        -> low-shelf bass boost -> output gain/trim -> normalised soft clip
//        -> DC high-pass -> hard clamp to [-1, 1]
//
// Every tuning constant, filter frequency, Q, envelope time and gain is private to this file
// pair. The voicing that hosts used to pass at run time (sweep=45, width=50, balance=100,
// volume=100 — identical in every existing host) is now a compile-time factory voicing.
//
// Real-time contract: prepare() allocates; reset() and process() never allocate, lock, or do
// I/O. process() is block-size invariant for constant parameters.

#include "DspPrimitives.hpp"

#include <cstddef>
#include <vector>

namespace epicenter {
namespace core_internal {

class EpicenterProcessor {
public:
    static constexpr int kMaxChannels = 2;

    // Allocates all working memory. May throw std::bad_alloc. Not real-time safe.
    void prepare(double sampleRate, int channelCount, std::size_t maxFrames);
    // Clears all filter/envelope/detector state. Equivalent to a fresh prepare() with the same
    // arguments. Real-time safe (no allocation).
    void reset();
    // In-place processing of `channelCount` planar channels (as prepared). Real-time safe.
    // Any frameCount is accepted; blocks larger than the prepared size are split internally.
    void process(float* const* channels, std::size_t frameCount, bool enabled, float intensity);

    // Scratch planes of `maxFrames()` floats each, used by the interleaved front-end.
    float* scratch(int channel) { return interleaveScratch_[static_cast<std::size_t>(channel)].data(); }
    std::size_t maxFrames() const { return maxFrames_; }
    int channelCount() const { return channelCount_; }

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

    void processChunk(float* const* channels, int channelCount, std::size_t frameCount, float intensity);
    static DerivedFrequencies getDerivedFrequencies(float sweepFreq, float width);
    ChannelState createChannelState(const DerivedFrequencies& derived) const;
    MonoState createMonoState(const DerivedFrequencies& derived) const;
    static float computeGate(float monoEnv, float diffEnv, float weightedDetectorEnv);

    double sampleRate_ = 44100.0;
    int channelCount_ = 0;
    std::size_t maxFrames_ = 0;
    ChannelState channels_[kMaxChannels];
    MonoState monoState_;
    std::vector<float> subBuffer_;
    std::vector<float> deepExtensionBuffer_;
    std::vector<float> interleaveScratch_[kMaxChannels];
};

} // namespace core_internal
} // namespace epicenter

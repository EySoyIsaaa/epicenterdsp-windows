#pragma once
// Thin adapter that drives the FROZEN legacy core exactly the way every existing host does
// (factory voicing sweep=45, width=50, balance=100, volume=100). Tests/tools/benchmarks only.

#include "EpicenterDSPCore.hpp" // tests/reference/legacy
#include "Signals.hpp"

#include <algorithm>
#include <cstddef>

namespace epicenter_test {

class LegacyCore {
public:
    void prepare(double sampleRate, int channels, std::size_t maxFrames) {
        channels_ = channels;
        dsp_.prepare(sampleRate, channels, maxFrames);
    }
    void configure(bool enabled, float intensity) {
        dsp_.setEnabled(enabled);
        dsp_.setParameters(intensity, 45.0f, 50.0f, 100.0f, 100.0f);
    }
    void reset() { dsp_.reset(); }
    void process(float* const* channels, std::size_t frames) { dsp_.process(channels, channels_, frames); }

    // Processes a whole signal in place in blocks of `block` frames.
    void processSignal(Signal& s, std::size_t block) {
        for (std::size_t off = 0; off < s.frames(); off += block) {
            const std::size_t n = std::min(block, s.frames() - off);
            float* ptrs[2] = {s.ch[0].data() + off, s.ch[s.ch.size() > 1 ? 1 : 0].data() + off};
            dsp_.process(ptrs, channels_, n);
        }
    }

    epicenter::EpicenterDSPCore& raw() { return dsp_; }

private:
    epicenter::EpicenterDSPCore dsp_;
    int channels_ = 2;
};

} // namespace epicenter_test

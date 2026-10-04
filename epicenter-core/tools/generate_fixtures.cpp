// Writes the canonical input fixtures (float32 WAV, 48 kHz, stereo, 2 s) to <outDir>.
//
//   generate_fixtures <outDir>
//
// Fixtures are committed to tests/audio/inputs/ so that golden tests are independent of the
// libm used to synthesise them.

#include "Signals.hpp"
#include "WavIO.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <outDir>\n", argv[0]);
        return 1;
    }
    const std::string dir = argv[1];
    for (const auto& f : epicenter_test::fixtureSet(48000, 2.0)) {
        const std::string path = dir + "/" + f.name + ".wav";
        if (!epicenter_test::writeWavFloat32(path, f.signal)) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 2;
        }
        std::printf("wrote %s (%zu frames)\n", path.c_str(), f.signal.frames());
    }
    return 0;
}

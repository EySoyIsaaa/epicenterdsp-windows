#pragma once
// WAV I/O for tests and tools (NOT part of the Core).
// Reads PCM 16/24/32-bit and IEEE float32 (incl. WAVE_FORMAT_EXTENSIBLE); writes float32.

#include "Signals.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace epicenter_test {

namespace wavdetail {
inline std::uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
inline std::uint16_t rd16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
inline void w32(std::vector<unsigned char>& b, std::uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back(static_cast<unsigned char>((v >> (8 * k)) & 0xFF)); }
inline void w16(std::vector<unsigned char>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
} // namespace wavdetail

inline bool readWav(const std::string& path, Signal& out) {
    using namespace wavdetail;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
    std::uint16_t format = 1, numCh = 0, bits = 0;
    std::uint32_t sr = 0;
    std::size_t dataOff = 0, dataLen = 0, pos = 12;
    while (pos + 8 <= b.size()) {
        const std::uint32_t sz = rd32(b.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (std::memcmp(b.data() + pos, "fmt ", 4) == 0 && body + 16 <= b.size()) {
            format = rd16(b.data() + body);
            numCh = rd16(b.data() + body + 2);
            sr = rd32(b.data() + body + 4);
            bits = rd16(b.data() + body + 14);
            if (format == 0xFFFE && body + 26 <= b.size()) format = rd16(b.data() + body + 24);
        } else if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            dataOff = body;
            dataLen = std::min<std::size_t>(sz, b.size() - body);
            break;
        }
        pos = body + sz + (sz & 1);
    }
    if (dataOff == 0 || numCh == 0 || bits == 0) return false;
    const std::size_t bytes = bits / 8;
    const std::size_t n = dataLen / (bytes * numCh);
    out.sampleRate = static_cast<int>(sr);
    out.ch.assign(numCh, std::vector<float>(n, 0.0f));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < numCh; ++c) {
            const unsigned char* s = b.data() + dataOff + (i * numCh + c) * bytes;
            float v = 0.0f;
            if (format == 3 && bits == 32) { std::memcpy(&v, s, 4); }
            else if (bits == 16) { v = static_cast<float>(static_cast<std::int16_t>(rd16(s))) / 32768.0f; }
            else if (bits == 24) {
                std::int32_t x = s[0] | (s[1] << 8) | (s[2] << 16);
                if (x & 0x800000) x |= ~0xFFFFFF;
                v = static_cast<float>(x) / 8388608.0f;
            } else if (bits == 32) { v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(rd32(s))) / 2147483648.0); }
            else return false;
            out.ch[c][i] = v;
        }
    }
    return true;
}

inline bool writeWavFloat32(const std::string& path, const Signal& s) {
    using namespace wavdetail;
    const auto numCh = static_cast<std::uint16_t>(s.channels());
    const std::size_t n = s.frames();
    const std::uint32_t dataLen = static_cast<std::uint32_t>(n * numCh * 4);
    std::vector<unsigned char> b;
    b.reserve(44 + dataLen);
    b.insert(b.end(), {'R', 'I', 'F', 'F'}); w32(b, 36 + dataLen);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); w32(b, 16);
    w16(b, 3); w16(b, numCh); w32(b, static_cast<std::uint32_t>(s.sampleRate));
    w32(b, static_cast<std::uint32_t>(s.sampleRate) * numCh * 4); w16(b, static_cast<std::uint16_t>(numCh * 4)); w16(b, 32);
    b.insert(b.end(), {'d', 'a', 't', 'a'}); w32(b, dataLen);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t c = 0; c < numCh; ++c) {
            unsigned char tmp[4];
            std::memcpy(tmp, &s.ch[c][i], 4);
            b.insert(b.end(), tmp, tmp + 4);
        }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

} // namespace epicenter_test

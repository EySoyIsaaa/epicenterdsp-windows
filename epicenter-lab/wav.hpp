#pragma once
// Minimal WAV reader/writer for the lab. Reads PCM 16/24/32-bit and IEEE float32,
// returns planar float channels. Writes 16-bit PCM. Feed it via ffmpeg for mp3/flac.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wav {

struct Audio {
    int sampleRate = 44100;
    int channels = 2;
    std::vector<std::vector<float>> ch; // ch[channel][frame]
    std::size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

inline uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline bool read(const std::string& path, Audio& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
        return false;

    uint16_t audioFormat = 1, numCh = 2, bits = 16;
    uint32_t sr = 44100;
    std::size_t dataOff = 0, dataLen = 0;
    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const unsigned char* id = b.data() + pos;
        uint32_t sz = rd32(b.data() + pos + 4);
        std::size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && body + 16 <= b.size()) {
            audioFormat = rd16(b.data() + body);
            numCh = rd16(b.data() + body + 2);
            sr = rd32(b.data() + body + 4);
            bits = rd16(b.data() + body + 14);
            if (audioFormat == 0xFFFE && body + 26 <= b.size()) audioFormat = rd16(b.data() + body + 24);
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataOff = body;
            dataLen = std::min<std::size_t>(sz, b.size() - body);
            break;
        }
        pos = body + sz + (sz & 1);
    }
    if (dataOff == 0 || numCh == 0) return false;

    out.sampleRate = int(sr);
    out.channels = numCh;
    const int bytes = bits / 8;
    const std::size_t frameBytes = std::size_t(bytes) * numCh;
    const std::size_t n = frameBytes ? dataLen / frameBytes : 0;
    out.ch.assign(numCh, std::vector<float>(n, 0.0f));

    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < numCh; ++c) {
            const unsigned char* s = b.data() + dataOff + (i * numCh + c) * bytes;
            float v = 0.0f;
            if (audioFormat == 3 && bits == 32) {
                float fv; std::memcpy(&fv, s, 4); v = fv;
            } else if (bits == 16) {
                v = int16_t(rd16(s)) / 32768.0f;
            } else if (bits == 24) {
                int32_t x = (s[0]) | (s[1] << 8) | (s[2] << 16);
                if (x & 0x800000) x |= ~0xFFFFFF;
                v = x / 8388608.0f;
            } else if (bits == 32) {
                int32_t x = int32_t(rd32(s)); v = x / 2147483648.0f;
            }
            out.ch[c][i] = v;
        }
    }
    return true;
}

inline void w32(std::vector<unsigned char>& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF); }
inline void w16(std::vector<unsigned char>& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }

inline bool write16(const std::string& path, const Audio& a) {
    const int numCh = a.channels;
    const std::size_t n = a.frames();
    const uint32_t byteRate = uint32_t(a.sampleRate) * numCh * 2;
    const uint32_t dataLen = uint32_t(n * numCh * 2);
    std::vector<unsigned char> b;
    b.insert(b.end(), {'R','I','F','F'}); w32(b, 36 + dataLen);
    b.insert(b.end(), {'W','A','V','E','f','m','t',' '}); w32(b, 16);
    w16(b, 1); w16(b, uint16_t(numCh)); w32(b, uint32_t(a.sampleRate)); w32(b, byteRate);
    w16(b, uint16_t(numCh * 2)); w16(b, 16);
    b.insert(b.end(), {'d','a','t','a'}); w32(b, dataLen);
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < numCh; ++c) {
            float v = a.ch[c][i];
            v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            int s = int(v * 32767.0f);
            w16(b, uint16_t(int16_t(s)));
        }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    return true;
}

} // namespace wav

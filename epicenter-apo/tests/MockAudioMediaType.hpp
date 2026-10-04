#pragma once

/**
 * @file MockAudioMediaType.hpp
 * @brief Lightweight in-memory implementation of IAudioMediaType for unit testing APOs.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audiomediatype.h>
#include <ks.h>
#include <ksmedia.h>
#include <atomic>

namespace epicenter {
namespace apo {
namespace test {

class CMockAudioMediaType final : public IAudioMediaType {
public:
    CMockAudioMediaType(DWORD sampleRate = 48000, WORD channels = 2, WORD bitsPerSample = 32) {
        wfx_.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx_.Format.nChannels = channels;
        wfx_.Format.nSamplesPerSec = sampleRate;
        wfx_.Format.wBitsPerSample = bitsPerSample;
        wfx_.Format.nBlockAlign = (channels * bitsPerSample) / 8;
        wfx_.Format.nAvgBytesPerSec = sampleRate * wfx_.Format.nBlockAlign;
        wfx_.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wfx_.Samples.wValidBitsPerSample = bitsPerSample;
        wfx_.dwChannelMask = (channels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
        wfx_.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IAudioMediaType)) {
            *ppv = static_cast<IAudioMediaType*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (count == 0) delete this;
        return count;
    }

    // IAudioMediaType
    STDMETHODIMP IsCompressedFormat(BOOL* pfCompressed) override {
        if (!pfCompressed) return E_POINTER;
        *pfCompressed = FALSE;
        return S_OK;
    }

    STDMETHODIMP IsEqual(IAudioMediaType* pIAudioType, DWORD* pdwFlags) override {
        if (!pIAudioType || !pdwFlags) return E_POINTER;
        *pdwFlags = 0;
        const WAVEFORMATEX* other = pIAudioType->GetAudioFormat();
        if (!other) return S_FALSE;
        if (std::memcmp(&wfx_.Format, other, sizeof(WAVEFORMATEX)) == 0) return S_OK;
        return S_FALSE;
    }

    const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() override {
        return reinterpret_cast<const WAVEFORMATEX*>(&wfx_);
    }

    STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* pUncompressed) override {
        if (!pUncompressed) return E_POINTER;
        pUncompressed->guidFormatType = wfx_.SubFormat;
        pUncompressed->dwSamplesPerFrame = wfx_.Format.nChannels;
        pUncompressed->dwBytesPerSampleContainer = wfx_.Format.wBitsPerSample / 8;
        pUncompressed->dwValidBitsPerSample = wfx_.Samples.wValidBitsPerSample;
        pUncompressed->fFramesPerSecond = static_cast<FLOAT>(wfx_.Format.nSamplesPerSec);
        pUncompressed->dwChannelMask = wfx_.dwChannelMask;
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
    WAVEFORMATEXTENSIBLE wfx_{};
};

} // namespace test
} // namespace apo
} // namespace epicenter

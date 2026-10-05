#include "EpicenterApo.h"

#include <cmath>
#include <algorithm>
#include <ks.h>
#include <ksmedia.h>

namespace epicenter {
namespace apo {

namespace {
// {5FA00F27-ADD6-499a-8A9D-6B98521FA75B}
static const GUID IID_IAudioSystemEffects_Local =
    { 0x5FA00F27, 0xADD6, 0x499a, { 0x8A, 0x9D, 0x6B, 0x98, 0x52, 0x1F, 0xA7, 0x5B } };

// {B1176E34-BB7F-4f05-BEBD-1B18A534E097}
static const GUID IID_IAudioSystemEffects2_Local =
    { 0xBAFE99D2, 0x7436, 0x44CE, { 0x9E, 0x0E, 0x4D, 0x89, 0xAF, 0xBF, 0xFF, 0x56 } };

inline double toDb(double v) {
    return 20.0 * std::log10(std::max(v, 1.0e-12));
}
} // namespace

CEpicenterApo::CEpicenterApo() {
    QueryPerformanceFrequency(&qpcFreq_);
    // Start crossfader in dry (bypass)
    crossfader_.reset();
}

CEpicenterApo::~CEpicenterApo() {
    UnlockForProcess();
    ipcChannel_.close();
}

// ----------------------------------------------------------------------------
// IUnknown
// ----------------------------------------------------------------------------

STDMETHODIMP CEpicenterApo::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    if (riid == IID_IUnknown) {
        *ppvObject = static_cast<IAudioProcessingObject*>(this);
    } else if (riid == __uuidof(IAudioProcessingObject)) {
        *ppvObject = static_cast<IAudioProcessingObject*>(this);
    } else if (riid == __uuidof(IAudioProcessingObjectConfiguration)) {
        *ppvObject = static_cast<IAudioProcessingObjectConfiguration*>(this);
    } else if (riid == __uuidof(IAudioProcessingObjectRT)) {
        *ppvObject = static_cast<IAudioProcessingObjectRT*>(this);
    } else if (riid == IID_IAudioSystemEffects_Local) {
        *ppvObject = static_cast<IAudioSystemEffects2*>(this);
    } else if (riid == IID_IAudioSystemEffects2_Local || riid == __uuidof(IAudioSystemEffects2)) {
        *ppvObject = static_cast<IAudioSystemEffects2*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CEpicenterApo::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG) CEpicenterApo::Release() {
    ULONG count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (count == 0) {
        delete this;
    }
    return count;
}

// ----------------------------------------------------------------------------
// IAudioProcessingObject
// ----------------------------------------------------------------------------

STDMETHODIMP CEpicenterApo::Reset() {
    dspCore_.reset();
    crossfader_.reset();
    return S_OK;
}

STDMETHODIMP CEpicenterApo::GetLatency(HNSTIME* pTime) {
    if (!pTime) return E_POINTER;
    // Epicenter minimum-phase IIR core introduces 0 frames algorithmic latency
    *pTime = 0;
    return S_OK;
}

STDMETHODIMP CEpicenterApo::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) {
    if (!ppRegProps) return E_POINTER;

    const size_t propSize = sizeof(APO_REG_PROPERTIES) + sizeof(IID);
    auto* props = static_cast<APO_REG_PROPERTIES*>(CoTaskMemAlloc(propSize));
    if (!props) return E_OUTOFMEMORY;

    std::memset(props, 0, propSize);
    props->clsid = CLSID_EpicenterAPO;
    props->Flags = static_cast<APO_FLAG>(APO_FLAG_DEFAULT | APO_FLAG_INPLACE);
    wcscpy_s(props->szFriendlyName, 256, EPICENTER_APO_FRIENDLY_NAME);
    wcscpy_s(props->szCopyrightInfo, 256, L"Epicenter DSP Professional (c) 2026");
    props->u32MajorVersion = 1;
    props->u32MinorVersion = 0;
    props->u32MinInputConnections = 1;
    props->u32MaxInputConnections = 1;
    props->u32MinOutputConnections = 1;
    props->u32MaxOutputConnections = 1;
    props->u32MaxInstances = 32;
    props->u32NumAPOInterfaces = 1;
    props->iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);

    *ppRegProps = props;
    return S_OK;
}

STDMETHODIMP CEpicenterApo::Initialize(UINT32 cbDataSize, BYTE* pbyData) {
    (void)cbDataSize;
    (void)pbyData;
    isInitialized_ = true;
    return S_OK;
}

STDMETHODIMP CEpicenterApo::IsInputFormatSupported(
    IAudioMediaType* pOppositeFormat,
    IAudioMediaType* pRequestedInputFormat,
    IAudioMediaType** ppSupportedInputFormat)
{
    (void)pOppositeFormat;
    if (!pRequestedInputFormat) return E_POINTER;
    if (ppSupportedInputFormat) *ppSupportedInputFormat = nullptr;

    const WAVEFORMATEX* wfx = pRequestedInputFormat->GetAudioFormat();
    if (!wfx) return E_INVALIDARG;

    // Must be float32, 1 or 2 channels, 1000..192000 Hz
    bool isFloat = false;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
        isFloat = true;
    } else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
            isFloat = true;
        }
    }

    const bool supportedRate = (wfx->nSamplesPerSec >= 1000 && wfx->nSamplesPerSec <= 192000);
    const bool supportedChannels = (wfx->nChannels == 1 || wfx->nChannels == 2);

    if (isFloat && supportedRate && supportedChannels) {
        return S_OK;
    }

    return S_FALSE;
}

STDMETHODIMP CEpicenterApo::IsOutputFormatSupported(
    IAudioMediaType* pOppositeFormat,
    IAudioMediaType* pRequestedOutputFormat,
    IAudioMediaType** ppSupportedOutputFormat)
{
    // Output format must match input format (Endpoint effect constraint)
    return IsInputFormatSupported(pOppositeFormat, pRequestedOutputFormat, ppSupportedOutputFormat);
}

STDMETHODIMP CEpicenterApo::GetInputChannelCount(UINT32* pu32ChannelCount) {
    if (!pu32ChannelCount) return E_POINTER;
    *pu32ChannelCount = static_cast<UINT32>(channels_);
    return S_OK;
}

// ----------------------------------------------------------------------------
// IAudioProcessingObjectConfiguration
// ----------------------------------------------------------------------------

STDMETHODIMP CEpicenterApo::LockForProcess(
    UINT32 u32NumInputConnections,
    APO_CONNECTION_DESCRIPTOR** ppInputConnections,
    UINT32 u32NumOutputConnections,
    APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    if (u32NumInputConnections != 1 || u32NumOutputConnections != 1) return E_INVALIDARG;
    if (!ppInputConnections || !ppInputConnections[0] || !ppOutputConnections || !ppOutputConnections[0]) {
        return E_POINTER;
    }

    const auto* inDesc = ppInputConnections[0];
    if (!inDesc->pFormat) return E_INVALIDARG;

    const WAVEFORMATEX* wfx = inDesc->pFormat->GetAudioFormat();
    if (!wfx) return E_INVALIDARG;

    sampleRate_ = static_cast<double>(wfx->nSamplesPerSec);
    channels_ = static_cast<int>(wfx->nChannels);
    maxFrameCount_ = inDesc->u32MaxFrameCount ? inDesc->u32MaxFrameCount : 1024;

    // Check format compatibility
    if (channels_ >= 1 && channels_ <= 2 && wfx->wBitsPerSample == 32) {
        formatSupported_ = true;
        dspCore_.prepare(sampleRate_, channels_, maxFrameCount_);
        crossfader_.prepare(sampleRate_, 8.0f);
        wetScratchBuffer_.assign(maxFrameCount_ * static_cast<size_t>(channels_), 0.0f);
        dryCopyBuffer_.assign(maxFrameCount_ * static_cast<size_t>(channels_), 0.0f);
    } else {
        // Fallback: unsupported format -> mark unsupported so APOProcess passes through
        formatSupported_ = false;
    }

    // Connect to Shared Memory IPC
    ipcChannel_.open(true);

    isLocked_ = true;
    return S_OK;
}

STDMETHODIMP CEpicenterApo::UnlockForProcess() {
    isLocked_ = false;
    dspCore_.reset();
    crossfader_.reset();
    return S_OK;
}

// ----------------------------------------------------------------------------
// IAudioProcessingObjectRT (Real-Time Audio Thread Callback)
// ----------------------------------------------------------------------------

void STDMETHODCALLTYPE CEpicenterApo::APOProcess(
    UINT32 u32NumInputConnections,
    APO_CONNECTION_PROPERTY** ppInputConnections,
    UINT32 u32NumOutputConnections,
    APO_CONNECTION_PROPERTY** ppOutputConnections)
{
    // Fail-safe sanity checks
    if (u32NumInputConnections < 1 || u32NumOutputConnections < 1 ||
        !ppInputConnections || !ppInputConnections[0] ||
        !ppOutputConnections || !ppOutputConnections[0])
    {
        return;
    }

    const auto* inProp = ppInputConnections[0];
    auto* outProp = ppOutputConnections[0];

    const UINT32 frames = inProp->u32ValidFrameCount;
    outProp->u32ValidFrameCount = frames;
    outProp->u32BufferFlags = inProp->u32BufferFlags;

    if (frames == 0 || !inProp->pBuffer || !outProp->pBuffer) return;

    auto* outBuf = reinterpret_cast<float*>(outProp->pBuffer);
    const auto* inBuf = reinterpret_cast<const float*>(inProp->pBuffer);

    // If input is silent or format is unsupported -> transparent pass-through
    if ((inProp->u32BufferFlags & BUFFER_SILENT) || !formatSupported_ || !isLocked_) {
        if (outBuf != inBuf) {
            std::copy(inBuf, inBuf + frames * static_cast<size_t>(channels_), outBuf);
        }
        return;
    }

    // 1. Read IPC control parameters using lock-free SeqLock
    if (ipcChannel_.isOpen()) {
        uint32_t en = 0, ab = 1;
        float inten = 0.0f;
        if (epicenter::ipc::readControlState(ipcChannel_.state(), &en, &inten, &ab)) {
            cachedEnabled_ = en;
            cachedIntensity_ = std::max(0.0f, std::min(inten, 100.0f));
            cachedAbState_ = ab;
        }
    } else {
        // UI not running: default to clean bypass
        cachedEnabled_ = 0;
        cachedIntensity_ = 0.0f;
        cachedAbState_ = 0;
    }

    // Determine target wet:
    // Only wet if Master Enabled is ON, A/B state is B (1), and Intensity > 0.01%
    const bool isEffectActive = (cachedEnabled_ != 0 && cachedAbState_ == 1 && cachedIntensity_ > 0.01f);
    const float targetWet = isEffectActive ? 1.0f : 0.0f;
    crossfader_.setTarget(targetWet);

    // Fast path: In bypass and crossfade transition is finished
    if (targetWet <= 0.00001f && crossfader_.currentAlpha() <= 0.00001f) {
        if (outBuf != inBuf) {
            std::copy(inBuf, inBuf + frames * static_cast<size_t>(channels_), outBuf);
        }
        outProp->u32BufferFlags = BUFFER_VALID;

        // Telemetry update for bypass
        if (ipcChannel_.isOpen()) {
            epicenter::ipc::writeTelemetry(
                ipcChannel_.state(), static_cast<uint32_t>(sampleRate_),
                static_cast<uint32_t>(channels_), frames, 0.0f, -96.0f, -96.0f, 1);
        }
        return;
    }

    // 2. Active or Transitioning Path
    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);

    // Ensure working size
    const size_t totalSamples = frames * static_cast<size_t>(channels_);
    if (wetScratchBuffer_.size() < totalSamples) {
        // Should not happen if prepared correctly; fallback to bypass
        if (outBuf != inBuf) std::copy(inBuf, inBuf + totalSamples, outBuf);
        return;
    }

    // Copy input to wet scratch
    std::copy(inBuf, inBuf + totalSamples, wetScratchBuffer_.data());

    // Execute Core DSP
    dspCore_.setEnabled(true);
    dspCore_.setIntensity(cachedIntensity_);
    dspCore_.processInterleaved(wetScratchBuffer_.data(), channels_, frames);

    // Apply clickless crossfader (dry in inBuf, wet in wetScratchBuffer, output in outBuf)
    crossfader_.process(inBuf, wetScratchBuffer_.data(), outBuf, channels_, frames);

    QueryPerformanceCounter(&t1);
    outProp->u32BufferFlags = BUFFER_VALID;

    // 3. Telemetry metrics calculation (non-blocking)
    if (ipcChannel_.isOpen() && qpcFreq_.QuadPart > 0) {
        const double dspTimeUs = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1.0e6 /
                                 static_cast<double>(qpcFreq_.QuadPart);

        double peak = 0.0;
        double sumSq = 0.0;
        for (size_t i = 0; i < totalSamples; ++i) {
            const double a = std::fabs(static_cast<double>(outBuf[i]));
            if (a > peak) peak = a;
            sumSq += a * a;
        }
        const double rms = totalSamples ? std::sqrt(sumSq / static_cast<double>(totalSamples)) : 0.0;

        epicenter::ipc::writeTelemetry(
            ipcChannel_.state(), static_cast<uint32_t>(sampleRate_),
            static_cast<uint32_t>(channels_), frames,
            static_cast<float>(dspTimeUs), static_cast<float>(toDb(peak)),
            static_cast<float>(toDb(rms)), 1);
    }
}

UINT32 STDMETHODCALLTYPE CEpicenterApo::CalcInputFrames(UINT32 u32OutputFrameCount) {
    return u32OutputFrameCount;
}

UINT32 STDMETHODCALLTYPE CEpicenterApo::CalcOutputFrames(UINT32 u32InputFrameCount) {
    return u32InputFrameCount;
}

// ----------------------------------------------------------------------------
// IAudioSystemEffects2
// ----------------------------------------------------------------------------

STDMETHODIMP CEpicenterApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) {
    (void)Event;
    if (!ppEffectsIds || !pcEffects) return E_POINTER;

    auto* ids = static_cast<LPGUID>(CoTaskMemAlloc(sizeof(GUID)));
    if (!ids) return E_OUTOFMEMORY;

    ids[0] = CLSID_EpicenterAPO;
    *ppEffectsIds = ids;
    *pcEffects = 1;
    return S_OK;
}

} // namespace apo
} // namespace epicenter

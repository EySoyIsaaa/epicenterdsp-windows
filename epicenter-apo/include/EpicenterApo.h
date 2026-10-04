#pragma once

/**
 * @file EpicenterApo.h
 * @brief CEpicenterApo COM class declaration.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <unknwn.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>

#include "EpicenterApoGuids.h"
#include "EpicenterApoIpc.h"
#include "../src/Crossfader.hpp"
#include "../src/ApoIpcClient.hpp"
#include "../../epicenter-core/include/EpicenterDSP.h"

#include <atomic>
#include <vector>

namespace epicenter {
namespace apo {

class CEpicenterApo final :
    public IAudioProcessingObject,
    public IAudioProcessingObjectConfiguration,
    public IAudioProcessingObjectRT,
    public IAudioSystemEffects2
{
public:
    CEpicenterApo();
    ~CEpicenterApo();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioProcessingObject
    STDMETHODIMP Reset() override;
    STDMETHODIMP GetLatency(HNSTIME* pTime) override;
    STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override;
    STDMETHODIMP Initialize(UINT32 cbDataSize, BYTE* pbyData) override;
    STDMETHODIMP IsInputFormatSupported(IAudioMediaType* pOppositeFormat, IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat) override;
    STDMETHODIMP IsOutputFormatSupported(IAudioMediaType* pOppositeFormat, IAudioMediaType* pRequestedOutputFormat, IAudioMediaType** ppSupportedOutputFormat) override;
    STDMETHODIMP GetInputChannelCount(UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectConfiguration
    STDMETHODIMP LockForProcess(UINT32 u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                UINT32 u32NumOutputConnections, APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
    STDMETHODIMP UnlockForProcess() override;

    // IAudioProcessingObjectRT
    STDMETHODIMP_(void) APOProcess(UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
                                   UINT32 u32NumOutputConnections, APO_CONNECTION_PROPERTY** ppOutputConnections) override;
    STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 u32OutputFrameCount) override;
    STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 u32InputFrameCount) override;

    // IAudioSystemEffects2
    STDMETHODIMP GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) override;

    // Testing helper hooks (non-production)
    bool isLocked() const { return isLocked_; }
    double activeSampleRate() const { return sampleRate_; }
    int activeChannels() const { return channels_; }
    SharedMemoryChannel& ipcChannel() { return ipcChannel_; }

private:
    std::atomic<ULONG> refCount_{1};

    // State & configuration
    bool isInitialized_ = false;
    bool isLocked_ = false;
    double sampleRate_ = 48000.0;
    int channels_ = 2;
    UINT32 maxFrameCount_ = 1024;
    bool formatSupported_ = false;

    // Core DSP instance (pure C ABI / C++ wrapper)
    EpicenterDSP dspCore_;

    // Real-time Clickless Crossfader
    ClicklessCrossfader crossfader_;

    // Preallocated buffers for realtime safety
    std::vector<float> wetScratchBuffer_;
    std::vector<float> dryCopyBuffer_;

    // IPC Shared Memory
    SharedMemoryChannel ipcChannel_;

    // Cached control state for lock-free read
    uint32_t cachedEnabled_ = 0;
    float    cachedIntensity_ = 0.0f;
    uint32_t cachedAbState_ = 1;

    // Performance frequency for telemetry
    LARGE_INTEGER qpcFreq_{};
};

} // namespace apo
} // namespace epicenter

#include "AudioBridge.hpp"

#include "EpicenterDSPCore.hpp"
#include "PolicyConfig.hpp"

#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T* get() const { return value_; }
    T** put() {
        reset();
        return &value_;
    }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset() {
        if (value_) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_ = nullptr;
};

class CoInitScope {
public:
    explicit CoInitScope(DWORD model = COINIT_MULTITHREADED) {
        const HRESULT hr = CoInitializeEx(nullptr, model);
        ok_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        owns_ = SUCCEEDED(hr);
    }
    ~CoInitScope() {
        if (owns_) CoUninitialize();
    }
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
    bool owns_ = false;
};

struct EndpointInfo {
    std::wstring id;
    std::wstring name;
};

std::wstring hresultText(HRESULT hr) {
    wchar_t* message = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wostringstream stream;
    if (count && message) {
        std::wstring clean(message, count);
        while (!clean.empty() && (clean.back() == L'\r' || clean.back() == L'\n' || clean.back() == L' ')) clean.pop_back();
        stream << clean << L" (0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr) << L")";
        LocalFree(message);
    } else {
        stream << L"Error de Windows 0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    }
    return stream.str();
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

bool getEndpointId(IMMDevice* device, std::wstring& id) {
    LPWSTR rawId = nullptr;
    const HRESULT hr = device->GetId(&rawId);
    if (FAILED(hr) || !rawId) return false;
    id = rawId;
    CoTaskMemFree(rawId);
    return true;
}

bool getEndpointName(IMMDevice* device, std::wstring& name) {
    ComPtr<IPropertyStore> properties;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, properties.put());
    if (FAILED(hr)) return false;
    PROPVARIANT value;
    PropVariantInit(&value);
    hr = properties->GetValue(PKEY_Device_FriendlyName, &value);
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal) name = value.pwszVal;
    PropVariantClear(&value);
    return SUCCEEDED(hr) && !name.empty();
}

std::vector<EndpointInfo> enumerateEndpoints(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
    std::vector<EndpointInfo> result;
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put()))) return result;
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return result;
    result.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.put()))) continue;
        EndpointInfo info;
        if (getEndpointId(device.get(), info.id) && getEndpointName(device.get(), info.name)) result.push_back(std::move(info));
    }
    return result;
}

const EndpointInfo* findCableEndpoint(const std::vector<EndpointInfo>& endpoints, EDataFlow flow) {
    for (const auto& endpoint : endpoints) {
        const std::wstring name = lower(endpoint.name);
        const bool vbCable = name.find(L"vb-audio") != std::wstring::npos ||
                             name.find(L"virtual audio cable") != std::wstring::npos;
        const bool cable = name.find(L"cable") != std::wstring::npos;
        const bool direction = flow == eRender
            ? name.find(L"input") != std::wstring::npos
            : name.find(L"output") != std::wstring::npos;
        if (vbCable && cable && direction) return &endpoint;
    }
    return nullptr;
}

bool getDefaultEndpoint(IMMDeviceEnumerator* enumerator, ERole role, std::wstring& id, std::wstring* name = nullptr) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, role, device.put()))) return false;
    if (!getEndpointId(device.get(), id)) return false;
    if (name) getEndpointName(device.get(), *name);
    return true;
}

bool setDefaultEndpoints(const std::array<std::wstring, 3>& ids, std::wstring* error = nullptr) {
    constexpr std::array<ERole, 3> roles = {eConsole, eMultimedia, eCommunications};
    ComPtr<IPolicyConfig> policy;
    HRESULT createHr = CoCreateInstance(CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL,
                                        IID_IPolicyConfig, reinterpret_cast<void**>(policy.put()));
    ComPtr<IPolicyConfigVista> vistaPolicy;
    if (FAILED(createHr)) {
        createHr = CoCreateInstance(CLSID_PolicyConfigVistaClient, nullptr, CLSCTX_ALL,
                                    IID_IPolicyConfigVista, reinterpret_cast<void**>(vistaPolicy.put()));
    }
    if (FAILED(createHr)) {
        if (error) *error = L"Windows no permitió abrir el control de dispositivos: " + hresultText(createHr);
        return false;
    }

    bool success = true;
    HRESULT lastHr = S_OK;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].empty()) continue;
        const HRESULT hr = policy
            ? policy->SetDefaultEndpoint(ids[i].c_str(), roles[i])
            : vistaPolicy->SetDefaultEndpoint(ids[i].c_str(), roles[i]);
        if (FAILED(hr)) {
            success = false;
            lastHr = hr;
        }
    }
    if (!success && error) *error = L"No se pudo cambiar la salida predeterminada: " + hresultText(lastHr);
    return success;
}

std::filesystem::path stateDirectory() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)) || !localAppData) return {};
    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    path /= L"EpicenterDSP Windows";
    return path;
}

std::filesystem::path stateFile() {
    auto path = stateDirectory();
    if (!path.empty()) path /= L"routing-state.txt";
    return path;
}

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

bool writeRoutingState(const std::array<std::wstring, 3>& ids) {
    const auto directory = stateDirectory();
    if (directory.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::ofstream output(stateFile(), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << toUtf8(ids[0]) << '\n' << toUtf8(ids[1]) << '\n' << toUtf8(ids[2]) << '\n';
    output.flush();
    return output.good();
}

bool readRoutingState(std::array<std::wstring, 3>& ids) {
    std::ifstream input(stateFile(), std::ios::binary);
    if (!input) return false;
    std::string line;
    for (auto& id : ids) {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        id = fromUtf8(line);
    }
    return !ids[0].empty() || !ids[1].empty() || !ids[2].empty();
}

void clearRoutingState() {
    std::error_code ec;
    std::filesystem::remove(stateFile(), ec);
}

class StereoRingBuffer {
public:
    StereoRingBuffer(std::size_t capacityFrames, std::size_t targetFrames)
        : data_(capacityFrames * 2, 0.0f), capacity_(capacityFrames), target_(targetFrames) {}

    void push(float left, float right) {
        if (available() >= capacity_ - 2) readFrame_ = static_cast<double>(writeFrame_ - capacity_ + 3);
        const std::size_t index = static_cast<std::size_t>(writeFrame_ % capacity_) * 2;
        data_[index] = left;
        data_[index + 1] = right;
        ++writeFrame_;
    }

    void render(float* destination, std::size_t frames) {
        if (!primed_ && available() >= target_) primed_ = true;
        for (std::size_t i = 0; i < frames; ++i) {
            const std::size_t queued = available();
            if (!primed_ || queued < 2) {
                destination[i * 2] = 0.0f;
                destination[i * 2 + 1] = 0.0f;
                if (queued < 2) primed_ = false;
                continue;
            }

            const auto base = static_cast<std::uint64_t>(readFrame_);
            const float fraction = static_cast<float>(readFrame_ - static_cast<double>(base));
            const std::size_t a = static_cast<std::size_t>(base % capacity_) * 2;
            const std::size_t b = static_cast<std::size_t>((base + 1) % capacity_) * 2;
            destination[i * 2] = data_[a] + (data_[b] - data_[a]) * fraction;
            destination[i * 2 + 1] = data_[a + 1] + (data_[b + 1] - data_[a + 1]) * fraction;

            const double error = static_cast<double>(queued) - static_cast<double>(target_);
            const double correction = std::clamp((error / static_cast<double>(target_)) * 0.002, -0.002, 0.002);
            readFrame_ += 1.0 + correction;
        }
    }

private:
    std::size_t available() const {
        const auto read = static_cast<std::uint64_t>(readFrame_);
        return writeFrame_ > read ? static_cast<std::size_t>(writeFrame_ - read) : 0;
    }

    std::vector<float> data_;
    std::size_t capacity_;
    std::size_t target_;
    std::uint64_t writeFrame_ = 0;
    double readFrame_ = 0.0;
    bool primed_ = false;
};

WAVEFORMATEXTENSIBLE stereoFloatFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = 48000;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = static_cast<WORD>(format.Format.nChannels * sizeof(float));
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return format;
}

} // namespace

AudioBridge::AudioBridge() = default;

AudioBridge::~AudioBridge() {
    stop();
}

bool AudioBridge::probe(std::wstring& detail) {
    CoInitScope com(COINIT_APARTMENTTHREADED);
    if (!com.ok()) {
        detail = L"No se pudo iniciar el sistema de audio de Windows.";
        return false;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr)) {
        detail = L"No se pudieron enumerar los dispositivos: " + hresultText(hr);
        return false;
    }
    const auto render = enumerateEndpoints(enumerator.get(), eRender);
    const auto capture = enumerateEndpoints(enumerator.get(), eCapture);
    if (!findCableEndpoint(render, eRender) || !findCableEndpoint(capture, eCapture)) {
        detail = L"Falta VB-CABLE para interceptar todo el audio";
        return false;
    }
    std::wstring defaultId;
    std::wstring defaultName;
    if (!getDefaultEndpoint(enumerator.get(), eMultimedia, defaultId, &defaultName)) {
        detail = L"No hay una salida de audio predeterminada.";
        return false;
    }
    detail = L"Listo · salida: " + defaultName;
    return true;
}

bool AudioBridge::start(float intensity, std::wstring& error) {
    if (isRunning()) return true;
    setIntensity(intensity);
    stopRequested_.store(false, std::memory_order_release);
    healthy_.store(false, std::memory_order_release);
    setThreadError(L"");

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr)) {
        error = L"No se pudieron abrir los dispositivos de audio: " + hresultText(hr);
        return false;
    }

    const auto render = enumerateEndpoints(enumerator.get(), eRender);
    const auto capture = enumerateEndpoints(enumerator.get(), eCapture);
    const EndpointInfo* cableRender = findCableEndpoint(render, eRender);
    const EndpointInfo* cableCapture = findCableEndpoint(capture, eCapture);
    if (!cableRender || !cableCapture) {
        error = L"Para procesar todo el audio hace falta VB-CABLE (CABLE Input + CABLE Output).";
        return false;
    }

    std::wstring outputName;
    if (!getDefaultEndpoint(enumerator.get(), eConsole, originalConsoleId_)) originalConsoleId_.clear();
    if (!getDefaultEndpoint(enumerator.get(), eMultimedia, originalMultimediaId_, &outputName)) originalMultimediaId_.clear();
    if (!getDefaultEndpoint(enumerator.get(), eCommunications, originalCommunicationsId_)) originalCommunicationsId_.clear();
    if (originalMultimediaId_.empty()) originalMultimediaId_ = originalConsoleId_;
    if (originalConsoleId_.empty()) originalConsoleId_ = originalMultimediaId_;
    if (originalCommunicationsId_.empty()) originalCommunicationsId_ = originalMultimediaId_;
    if (originalMultimediaId_.empty()) {
        error = L"Windows no tiene una salida de audio predeterminada.";
        return false;
    }
    if (originalMultimediaId_ == cableRender->id) {
        error = L"CABLE Input ya es la salida predeterminada. Desactívala en Windows y vuelve a intentar para que Epicenter sepa a qué bocinas enviar el audio.";
        return false;
    }
    {
        std::lock_guard lock(stateMutex_);
        outputName_ = outputName;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_ || !readyEvent_) {
        error = L"No se pudieron crear los eventos del motor de audio.";
        if (stopEvent_) CloseHandle(static_cast<HANDLE>(stopEvent_));
        if (readyEvent_) CloseHandle(static_cast<HANDLE>(readyEvent_));
        stopEvent_ = readyEvent_ = nullptr;
        return false;
    }

    audioThread_ = std::thread(&AudioBridge::audioThreadMain, this, cableCapture->id, originalMultimediaId_);
    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(readyEvent_), 5000);
    if (wait != WAIT_OBJECT_0 || !healthy_.load(std::memory_order_acquire)) {
        error = wait == WAIT_TIMEOUT ? L"El motor de audio no respondió a tiempo." : lastError();
        SetEvent(static_cast<HANDLE>(stopEvent_));
        if (audioThread_.joinable()) audioThread_.join();
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        CloseHandle(static_cast<HANDLE>(readyEvent_));
        stopEvent_ = readyEvent_ = nullptr;
        return false;
    }

    const std::array<std::wstring, 3> originals = {
        originalConsoleId_, originalMultimediaId_, originalCommunicationsId_
    };
    if (!writeRoutingState(originals)) {
        error = L"No se pudo guardar el punto de restauración del audio. Epicenter no activará el ruteo por seguridad.";
        SetEvent(static_cast<HANDLE>(stopEvent_));
        if (audioThread_.joinable()) audioThread_.join();
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        CloseHandle(static_cast<HANDLE>(readyEvent_));
        stopEvent_ = readyEvent_ = nullptr;
        return false;
    }

    const std::array<std::wstring, 3> cableDefaults = {cableRender->id, cableRender->id, cableRender->id};
    if (!setDefaultEndpoints(cableDefaults, &error)) {
        setDefaultEndpoints(originals, nullptr);
        clearRoutingState();
        SetEvent(static_cast<HANDLE>(stopEvent_));
        if (audioThread_.joinable()) audioThread_.join();
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        CloseHandle(static_cast<HANDLE>(readyEvent_));
        stopEvent_ = readyEvent_ = nullptr;
        return false;
    }

    running_.store(true, std::memory_order_release);
    return true;
}

void AudioBridge::stop() {
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    if (wasRunning) {
        const std::array<std::wstring, 3> originals = {
            originalConsoleId_, originalMultimediaId_, originalCommunicationsId_
        };
        bool restored = false;
        for (int attempt = 0; attempt < 3 && !restored; ++attempt) {
            restored = setDefaultEndpoints(originals, nullptr);
            if (!restored) Sleep(50);
        }
        if (restored) clearRoutingState();
    }

    stopRequested_.store(true, std::memory_order_release);
    if (stopEvent_) SetEvent(static_cast<HANDLE>(stopEvent_));
    if (audioThread_.joinable()) audioThread_.join();
    healthy_.store(false, std::memory_order_release);

    // A final retry after WASAPI releases both endpoints handles drivers that
    // briefly reject a role change while their stream is shutting down.
    if (wasRunning && std::filesystem::exists(stateFile())) recoverStaleRouting();

    if (stopEvent_) CloseHandle(static_cast<HANDLE>(stopEvent_));
    if (readyEvent_) CloseHandle(static_cast<HANDLE>(readyEvent_));
    stopEvent_ = readyEvent_ = nullptr;
}

void AudioBridge::setIntensity(float intensity) {
    intensity_.store(std::clamp(intensity, 0.0f, 100.0f), std::memory_order_release);
}

std::wstring AudioBridge::lastError() const {
    std::lock_guard lock(stateMutex_);
    return lastError_;
}

std::wstring AudioBridge::outputName() const {
    std::lock_guard lock(stateMutex_);
    return outputName_;
}

void AudioBridge::setThreadError(const std::wstring& message) {
    std::lock_guard lock(stateMutex_);
    lastError_ = message;
}

bool AudioBridge::recoverStaleRouting() {
    std::array<std::wstring, 3> ids;
    if (!readRoutingState(ids)) return false;
    CoInitScope com;
    if (!com.ok()) return false;
    if (!setDefaultEndpoints(ids, nullptr)) return false;
    clearRoutingState();
    return true;
}

int AudioBridge::runWatchdog(unsigned long parentProcessId) {
    const HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentProcessId);
    if (!parent) return 2;
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
    recoverStaleRouting();
    return 0;
}

void AudioBridge::audioThreadMain(std::wstring captureId, std::wstring renderId) {
    CoInitScope com;
    auto fail = [&](const std::wstring& message) {
        setThreadError(message);
        healthy_.store(false, std::memory_order_release);
        if (readyEvent_) SetEvent(static_cast<HANDLE>(readyEvent_));
    };
    if (!com.ok()) {
        fail(L"No se pudo iniciar COM en el hilo de audio.");
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr)) {
        fail(L"No se pudo iniciar el enumerador WASAPI: " + hresultText(hr));
        return;
    }

    ComPtr<IMMDevice> captureDevice;
    ComPtr<IMMDevice> renderDevice;
    if (FAILED(hr = enumerator->GetDevice(captureId.c_str(), captureDevice.put())) ||
        FAILED(hr = enumerator->GetDevice(renderId.c_str(), renderDevice.put()))) {
        fail(L"Un dispositivo de audio dejó de estar disponible: " + hresultText(hr));
        return;
    }

    ComPtr<IAudioClient> captureClient;
    ComPtr<IAudioClient> renderClient;
    if (FAILED(hr = captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void**>(captureClient.put()))) ||
        FAILED(hr = renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(renderClient.put())))) {
        fail(L"No se pudo abrir un cliente WASAPI: " + hresultText(hr));
        return;
    }

    auto format = stereoFloatFormat();
    constexpr DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format.Format, nullptr);
    if (FAILED(hr)) {
        fail(L"CABLE Output no aceptó estéreo float a 48 kHz: " + hresultText(hr));
        return;
    }
    hr = renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format.Format, nullptr);
    if (FAILED(hr)) {
        fail(L"La salida física no aceptó el formato del puente: " + hresultText(hr));
        return;
    }

    HANDLE captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE renderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent || !renderEvent) {
        if (captureEvent) CloseHandle(captureEvent);
        if (renderEvent) CloseHandle(renderEvent);
        fail(L"No se pudieron crear los eventos WASAPI.");
        return;
    }
    const auto closeEvents = [&] {
        CloseHandle(captureEvent);
        CloseHandle(renderEvent);
    };

    if (FAILED(hr = captureClient->SetEventHandle(captureEvent)) ||
        FAILED(hr = renderClient->SetEventHandle(renderEvent))) {
        closeEvents();
        fail(L"WASAPI no aceptó el modo dirigido por eventos: " + hresultText(hr));
        return;
    }

    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    if (FAILED(hr = captureClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.put()))) ||
        FAILED(hr = renderClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.put())))) {
        closeEvents();
        fail(L"No se pudieron abrir los buffers WASAPI: " + hresultText(hr));
        return;
    }

    UINT32 captureBufferFrames = 0;
    UINT32 renderBufferFrames = 0;
    if (FAILED(hr = captureClient->GetBufferSize(&captureBufferFrames)) ||
        FAILED(hr = renderClient->GetBufferSize(&renderBufferFrames))) {
        closeEvents();
        fail(L"No se pudo consultar el tamaño de los buffers: " + hresultText(hr));
        return;
    }

    BYTE* prime = nullptr;
    if (SUCCEEDED(render->GetBuffer(renderBufferFrames, &prime))) {
        render->ReleaseBuffer(renderBufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    epicenter::EpicenterDSPCore dsp;
    dsp.prepare(48000.0, 2, std::max<UINT32>(captureBufferFrames, 512));
    dsp.setParameters(intensity_.load(std::memory_order_acquire), 45.0f, 50.0f, 100.0f, 100.0f);
    dsp.setEnabled(true);
    epicenter::LowShelfFilter liveBassLeft;
    epicenter::LowShelfFilter liveBassRight;
    liveBassLeft.prepare(48000.0);
    liveBassRight.prepare(48000.0);

    std::vector<float> left(captureBufferFrames);
    std::vector<float> right(captureBufferFrames);
    std::vector<float> dryLeft(captureBufferFrames);
    std::vector<float> dryRight(captureBufferFrames);
    StereoRingBuffer ring(48000 / 2, 48000 * 55 / 1000);
    float currentWet = 0.0f;
    bool bassBoostActive = false;

    if (FAILED(hr = renderClient->Start()) || FAILED(hr = captureClient->Start())) {
        renderClient->Stop();
        captureClient->Stop();
        closeEvents();
        fail(L"No se pudo arrancar el flujo de audio: " + hresultText(hr));
        return;
    }

    healthy_.store(true, std::memory_order_release);
    if (readyEvent_) SetEvent(static_cast<HANDLE>(readyEvent_));

    const std::array<HANDLE, 3> events = {
        static_cast<HANDLE>(stopEvent_), captureEvent, renderEvent
    };
    bool loop = true;
    while (loop && !stopRequested_.load(std::memory_order_acquire)) {
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, 1000);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            fail(L"Falló la espera del motor de audio: " + hresultText(HRESULT_FROM_WIN32(GetLastError())));
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            UINT32 packetFrames = 0;
            while (SUCCEEDED(hr = capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                BYTE* bytes = nullptr;
                DWORD bufferFlags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                hr = capture->GetBuffer(&bytes, &packetFrames, &bufferFlags, &devicePosition, &qpcPosition);
                if (FAILED(hr)) break;
                if (packetFrames > left.size()) {
                    capture->ReleaseBuffer(packetFrames);
                    hr = E_UNEXPECTED;
                    break;
                }

                const float* interleaved = reinterpret_cast<const float*>(bytes);
                const bool silent = (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                for (UINT32 i = 0; i < packetFrames; ++i) {
                    const float l = silent ? 0.0f : interleaved[i * 2];
                    const float r = silent ? 0.0f : interleaved[i * 2 + 1];
                    left[i] = dryLeft[i] = l;
                    right[i] = dryRight[i] = r;
                }

                const float intensity = intensity_.load(std::memory_order_acquire);
                const float normalized = std::clamp(intensity / 100.0f, 0.0f, 1.0f);
                // Live tuning: the original mobile core is deliberately conservative.
                // Give the single desktop knob a wetter curve, drive the core harder,
                // and reinforce only the processed sub-bass path. Zero remains a true
                // DSP bypass because none of these stages run when targetWet is zero.
                const float targetWet = normalized <= 0.0001f ? 0.0f : std::pow(normalized, 0.55f);
                if (targetWet > 0.0f) {
                    dsp.setEnabled(true);
                    const float drivenIntensity = std::min(100.0f, intensity * 1.28f);
                    dsp.setParameters(drivenIntensity, 45.0f, 50.0f, 100.0f, 100.0f);
                    float* planes[2] = {left.data(), right.data()};
                    dsp.process(planes, 2, packetFrames);
                    const float bassGainDb = 5.5f * std::pow(normalized, 0.72f);
                    liveBassLeft.update(62.0f, bassGainDb);
                    liveBassRight.update(62.0f, bassGainDb);
                    for (UINT32 i = 0; i < packetFrames; ++i) {
                        left[i] = liveBassLeft.process(left[i]);
                        right[i] = liveBassRight.process(right[i]);
                    }
                    bassBoostActive = true;
                } else {
                    dsp.setEnabled(false);
                    if (bassBoostActive) {
                        liveBassLeft.reset();
                        liveBassRight.reset();
                        bassBoostActive = false;
                    }
                }

                for (UINT32 i = 0; i < packetFrames; ++i) {
                    const float position = static_cast<float>(i + 1) / static_cast<float>(packetFrames);
                    const float wet = currentWet + (targetWet - currentWet) * position;
                    const float outLeft = std::clamp(dryLeft[i] + (left[i] - dryLeft[i]) * wet, -1.0f, 1.0f);
                    const float outRight = std::clamp(dryRight[i] + (right[i] - dryRight[i]) * wet, -1.0f, 1.0f);
                    ring.push(outLeft, outRight);
                }
                currentWet = targetWet;
                capture->ReleaseBuffer(packetFrames);
            }
            if (FAILED(hr)) {
                fail(L"Se perdió la captura de CABLE Output: " + hresultText(hr));
                loop = false;
            }
        }

        if (wait == WAIT_OBJECT_0 + 2 && loop) {
            UINT32 padding = 0;
            hr = renderClient->GetCurrentPadding(&padding);
            if (FAILED(hr)) {
                fail(L"Se perdió la salida física: " + hresultText(hr));
                break;
            }
            const UINT32 available = renderBufferFrames > padding ? renderBufferFrames - padding : 0;
            if (available > 0) {
                BYTE* bytes = nullptr;
                hr = render->GetBuffer(available, &bytes);
                if (FAILED(hr)) {
                    fail(L"No se pudo escribir a la salida física: " + hresultText(hr));
                    break;
                }
                ring.render(reinterpret_cast<float*>(bytes), available);
                render->ReleaseBuffer(available, 0);
            }
        }
    }

    captureClient->Stop();
    renderClient->Stop();
    closeEvents();
    healthy_.store(false, std::memory_order_release);
}

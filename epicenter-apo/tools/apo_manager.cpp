/**
 * @file apo_manager.cpp
 * @brief Command-line tool to inspect Windows audio endpoints, bind/unbind Epicenter APO,
 *        and query/control live IPC shared memory state.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../include/EpicenterApoGuids.h"
#include "../include/EpicenterApoIpc.h"
#include "../src/ApoIpcClient.hpp"

// PKEY_FX_EndpointEffectClsid: {D04E05A6-594B-4fb6-A80D-01AF5EED7D1D}, 7
static const PROPERTYKEY PKEY_FX_EndpointEffectClsid_Local = {
    { 0xd04e05a6, 0x594b, 0x4fb6, { 0xa8, 0x0d, 0x01, 0xaf, 0x5e, 0xed, 0x7d, 0x1d } }, 7
};

// PKEY_FX_FriendlyName: {D04E05A6-594B-4fb6-A80D-01AF5EED7D1D}, 4
static const PROPERTYKEY PKEY_FX_FriendlyName_Local = {
    { 0xd04e05a6, 0x594b, 0x4fb6, { 0xa8, 0x0d, 0x01, 0xaf, 0x5e, 0xed, 0x7d, 0x1d } }, 4
};

static void printHelp(const char* exe) {
    std::printf("Epicenter APO Manager (CLI Utility)\n");
    std::printf("Usage: %s [command] [options]\n\n", exe);
    std::printf("Commands:\n");
    std::printf("  --list                     List active audio endpoints and APO status\n");
    std::printf("  --status                   Display live IPC shared memory status & telemetry\n");
    std::printf("  --control <en> <int> <ab>  Send control params (en: 0/1, int: 0-100, ab: 0/1)\n");
    std::printf("  --help                     Show this help message\n");
}

static void listEndpoints() {
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) return;

    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum));
    if (FAILED(hr)) {
        std::printf("Error: Failed to create MMDeviceEnumerator (0x%08X)\n", hr);
        CoUninitialize();
        return;
    }

    IMMDeviceCollection* pCol = nullptr;
    hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCol);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        pCol->GetCount(&count);
        std::printf("Active Render Audio Endpoints (%u found):\n", count);
        std::printf("--------------------------------------------------------------------------------\n");

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* pDev = nullptr;
            if (SUCCEEDED(pCol->Item(i, &pDev))) {
                LPWSTR devId = nullptr;
                pDev->GetId(&devId);

                IPropertyStore* pStore = nullptr;
                std::wstring friendlyName = L"Unknown";
                bool apoActive = false;

                if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pStore))) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(pStore->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) {
                        friendlyName = pv.pwszVal;
                    }
                    PropVariantClear(&pv);

                    // Check for Epicenter APO
                    if (SUCCEEDED(pStore->GetValue(PKEY_FX_EndpointEffectClsid_Local, &pv)) && pv.vt == VT_LPWSTR) {
                        if (_wcsicmp(pv.pwszVal, EPICENTER_APO_CLSID_STRING) == 0) {
                            apoActive = true;
                        }
                    }
                    PropVariantClear(&pv);
                    pStore->Release();
                }

                std::printf("[%u] %ls\n", i, friendlyName.c_str());
                std::printf("    ID: %ls\n", devId ? devId : L"N/A");
                std::printf("    Epicenter APO: %s\n\n", apoActive ? "ENABLED" : "Not Bound");

                if (devId) CoTaskMemFree(devId);
                pDev->Release();
            }
        }
        pCol->Release();
    }
    pEnum->Release();
    CoUninitialize();
}

static void showStatus() {
    epicenter::apo::SharedMemoryChannel chan;
    if (!chan.open(false)) {
        std::printf("IPC Shared Memory is not currently open or active.\n");
        std::printf("State: No active APO instance or UI has created the segment.\n");
        return;
    }

    const auto* s = chan.state();
    std::printf("=== Epicenter APO IPC Live Status ===\n");
    std::printf("Magic:            0x%08X ('%c%c%c%c')\n",
                s->magic,
                (s->magic >> 24) & 0xFF, (s->magic >> 16) & 0xFF,
                (s->magic >> 8) & 0xFF, s->magic & 0xFF);
    std::printf("Protocol Version: %u\n", s->protocolVersion);
    std::printf("Initialized:      %s\n", s->initFlags ? "YES" : "NO");
    std::printf("\n--- Control State ---\n");
    std::printf("SeqLock:          %u\n", s->control.sequence);
    std::printf("Master Enabled:   %s\n", s->control.enabled ? "ACTIVE (ON)" : "BYPASS (OFF)");
    std::printf("Intensity:        %.1f %%\n", s->control.intensity);
    std::printf("A/B State:        %s\n", s->control.abState == 1 ? "B (Epicenter Active)" : "A (Original Dry)");
    std::printf("Target Preset:    %u\n", s->control.targetPreset);

    std::printf("\n--- Telemetry State ---\n");
    std::printf("Heartbeat:        %u\n", s->telemetry.apoHeartbeat);
    std::printf("Sample Rate:      %u Hz\n", s->telemetry.sampleRate);
    std::printf("Channels:         %u\n", s->telemetry.channelCount);
    std::printf("Buffer Size:      %u frames\n", s->telemetry.bufferFrameCount);
    std::printf("DSP Exec Time:    %.2f µs\n", s->telemetry.dspTimeMicroseconds);
    std::printf("Peak Level:       %.2f dBFS\n", s->telemetry.peakDbfs);
    std::printf("RMS Level:        %.2f dBFS\n", s->telemetry.rmsDbfs);
    std::printf("APO Status:       %s\n", s->telemetry.apoStatus == 1 ? "ARMED/ACTIVE" : "INACTIVE");
}

static void sendControl(uint32_t enabled, float intensity, uint32_t abState) {
    epicenter::apo::SharedMemoryChannel chan;
    if (!chan.open(true)) {
        std::printf("Failed to open or create IPC shared memory.\n");
        return;
    }

    epicenter::ipc::writeControlState(chan.state(), enabled, intensity, abState);
    std::printf("Updated control: enabled=%u, intensity=%.1f, abState=%u\n",
                enabled, intensity, abState);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printHelp(argv[0]);
        return 0;
    }

    const std::string cmd = argv[1];
    if (cmd == "--list") {
        listEndpoints();
    } else if (cmd == "--status") {
        showStatus();
    } else if (cmd == "--control") {
        if (argc < 5) {
            std::printf("Error: --control requires <enabled 0/1> <intensity 0-100> <abState 0/1>\n");
            return 1;
        }
        uint32_t en = static_cast<uint32_t>(std::atoi(argv[2]));
        float inten = static_cast<float>(std::atof(argv[3]));
        uint32_t ab = static_cast<uint32_t>(std::atoi(argv[4]));
        sendControl(en, inten, ab);
    } else {
        printHelp(argv[0]);
    }
    return 0;
}

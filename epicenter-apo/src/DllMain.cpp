#include "EpicenterApo.h"
#include "EpicenterApoGuids.h"

#include <windows.h>
#include <olectl.h>

namespace epicenter {
namespace apo {
IClassFactory* createClassFactory();
bool canServerUnload();
}
}

static HMODULE g_hModule = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (rclsid != CLSID_EpicenterAPO) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = epicenter::apo::createClassFactory();
    if (!factory) return E_OUTOFMEMORY;

    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return epicenter::apo::canServerUnload() ? S_OK : S_FALSE;
}

static HRESULT setRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    const size_t byteLen = (wcslen(data) + 1) * sizeof(wchar_t);
    status = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>(byteLen));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(status);
}

static HRESULT setRegistryDword(HKEY root, const wchar_t* subkey, const wchar_t* valueName, DWORD data) {
    HKEY hKey = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    status = RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(DWORD));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(status);
}

STDAPI DllRegisterServer() {
    wchar_t modulePath[MAX_PATH];
    const DWORD modulePathLength = GetModuleFileNameW(g_hModule, modulePath, MAX_PATH);
    if (!modulePathLength) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (modulePathLength >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    const wchar_t* clsidKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING;
    const wchar_t* inprocKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING L"\\InprocServer32";
    const wchar_t* progIdKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING L"\\ProgID";

    HRESULT hr = setRegistryString(HKEY_CLASSES_ROOT, clsidKey, nullptr, EPICENTER_APO_FRIENDLY_NAME);
    if (FAILED(hr)) return hr;

    hr = setRegistryString(HKEY_CLASSES_ROOT, inprocKey, nullptr, modulePath);
    if (FAILED(hr)) return hr;

    hr = setRegistryString(HKEY_CLASSES_ROOT, inprocKey, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    hr = setRegistryString(HKEY_CLASSES_ROOT, progIdKey, nullptr, EPICENTER_APO_PROGID);
    if (FAILED(hr)) return hr;

    // Legacy development registration. This does not associate the APO with an
    // endpoint and does not replace a signed componentized driver package.
    const wchar_t* apoKey = L"AudioEngine\\AudioProcessingObjects\\" EPICENTER_APO_CLSID_STRING;
    struct StringProperty { const wchar_t* name; const wchar_t* value; };
    const StringProperty strings[] = {
        {L"FriendlyName", EPICENTER_APO_FRIENDLY_NAME},
        {L"Copyright", L"Copyright (c) Epicenter DSP"},
        // IAudioProcessingObject, as in Microsoft's componentized APO sample.
        {L"APOInterface0", L"{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}"},
    };
    for (const auto& property : strings) {
        hr = setRegistryString(HKEY_CLASSES_ROOT, apoKey, property.name, property.value);
        if (FAILED(hr)) return hr;
    }
    struct DwordProperty { const wchar_t* name; DWORD value; };
    const DwordProperty dwords[] = {
        {L"MajorVersion", 1},
        {L"MinorVersion", 0},
        {L"Flags", static_cast<DWORD>(APO_FLAG_DEFAULT | APO_FLAG_INPLACE)},
        {L"MinInputConnections", 1},
        {L"MaxInputConnections", 1},
        {L"MinOutputConnections", 1},
        {L"MaxOutputConnections", 1},
        {L"MaxInstances", 32},
        {L"NumAPOInterfaces", 1},
    };
    for (const auto& property : dwords) {
        hr = setRegistryDword(HKEY_CLASSES_ROOT, apoKey, property.name, property.value);
        if (FAILED(hr)) return hr;
    }

    return S_OK;
}

STDAPI DllUnregisterServer() {
    const wchar_t* inprocKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING L"\\InprocServer32";
    const wchar_t* progIdKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING L"\\ProgID";
    const wchar_t* clsidKey = L"CLSID\\" EPICENTER_APO_CLSID_STRING;
    const wchar_t* apoKey = L"AudioEngine\\AudioProcessingObjects\\" EPICENTER_APO_CLSID_STRING;

    const wchar_t* keys[] = {inprocKey, progIdKey, clsidKey, apoKey};
    HRESULT result = S_OK;
    for (const auto* key : keys) {
        const LSTATUS status = RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            status != ERROR_PATH_NOT_FOUND && SUCCEEDED(result)) {
            result = HRESULT_FROM_WIN32(status);
        }
    }
    return result;
}

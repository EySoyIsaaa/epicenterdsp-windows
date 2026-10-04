#include "PolicyConfig.hpp"

#include <cstdio>

int main() {
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initHr)) {
        std::fprintf(stderr, "FAIL: CoInitializeEx 0x%08lX\n", static_cast<unsigned long>(initHr));
        return 1;
    }

    IPolicyConfig* policy = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL,
                                  IID_IPolicyConfig, reinterpret_cast<void**>(&policy));
    if (SUCCEEDED(hr) && policy) {
        policy->Release();
        CoUninitialize();
        std::puts("PASS: modern IPolicyConfig activated");
        return 0;
    }

    IPolicyConfigVista* vistaPolicy = nullptr;
    hr = CoCreateInstance(CLSID_PolicyConfigVistaClient, nullptr, CLSCTX_ALL,
                          IID_IPolicyConfigVista, reinterpret_cast<void**>(&vistaPolicy));
    if (SUCCEEDED(hr) && vistaPolicy) {
        vistaPolicy->Release();
        CoUninitialize();
        std::puts("PASS: Vista IPolicyConfig fallback activated");
        return 0;
    }

    std::fprintf(stderr, "FAIL: no compatible policy interface, HRESULT 0x%08lX\n", static_cast<unsigned long>(hr));
    CoUninitialize();
    return 2;
}


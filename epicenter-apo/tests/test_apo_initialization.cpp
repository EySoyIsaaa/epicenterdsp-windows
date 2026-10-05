#include "EpicenterApo.h"
#include "MockAudioMediaType.hpp"
#include "../../epicenter-core/tests/support/TestHarness.hpp"

using namespace epicenter::apo;
using namespace epicenter::apo::test;

TEST_CASE("apo_create_and_com_interfaces") {
    auto* apo = new CEpicenterApo();
    REQUIRE(apo != nullptr);

    IAudioProcessingObject* pApo = nullptr;
    HRESULT hr = apo->QueryInterface(__uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&pApo));
    CHECK(SUCCEEDED(hr));
    CHECK(pApo != nullptr);
    if (pApo) pApo->Release();

    IAudioProcessingObjectConfiguration* pConfig = nullptr;
    hr = apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), reinterpret_cast<void**>(&pConfig));
    CHECK(SUCCEEDED(hr));
    CHECK(pConfig != nullptr);
    if (pConfig) pConfig->Release();

    IAudioProcessingObjectRT* pRt = nullptr;
    hr = apo->QueryInterface(__uuidof(IAudioProcessingObjectRT), reinterpret_cast<void**>(&pRt));
    CHECK(SUCCEEDED(hr));
    CHECK(pRt != nullptr);
    if (pRt) pRt->Release();

    IAudioSystemEffects2* pSys = nullptr;
    hr = apo->QueryInterface(__uuidof(IAudioSystemEffects2), reinterpret_cast<void**>(&pSys));
    CHECK(SUCCEEDED(hr));
    CHECK(pSys != nullptr);
    if (pSys) pSys->Release();

    apo->Release();
}

TEST_CASE("apo_properties_and_latency") {
    auto* apo = new CEpicenterApo();
    REQUIRE(apo != nullptr);

    HNSTIME latency = -1;
    HRESULT hr = apo->GetLatency(&latency);
    CHECK(SUCCEEDED(hr));
    CHECK(latency == 0); // 0 algorithmic latency

    APO_REG_PROPERTIES* props = nullptr;
    hr = apo->GetRegistrationProperties(&props);
    CHECK(SUCCEEDED(hr));
    REQUIRE(props != nullptr);
    CHECK(props->clsid == CLSID_EpicenterAPO);
    CHECK((props->Flags & APO_FLAG_INPLACE) != 0);
    CHECK(props->u32MinInputConnections == 1);
    CHECK(props->u32MaxInputConnections == 1);
    CHECK(props->u32MaxInstances == 32);
    CHECK(props->u32NumAPOInterfaces == 1);
    CHECK(props->iidAPOInterfaceList[0] == __uuidof(IAudioProcessingObject));
    IUnknown* registeredInterface = nullptr;
    CHECK(SUCCEEDED(apo->QueryInterface(props->iidAPOInterfaceList[0],
        reinterpret_cast<void**>(&registeredInterface))));
    if (registeredInterface) registeredInterface->Release();
    CoTaskMemFree(props);

    apo->Release();
}

TEST_CASE("apo_format_support_and_lock") {
    auto* apo = new CEpicenterApo();
    REQUIRE(apo != nullptr);

    // 1. Supported format: 48000 Hz, 2 channels, 32-bit float
    CMockAudioMediaType validFormat(48000, 2, 32);
    HRESULT hr = apo->IsInputFormatSupported(nullptr, &validFormat, nullptr);
    CHECK(hr == S_OK);

    // 2. Unsupported format: 48000 Hz, 16-bit integer
    CMockAudioMediaType invalidFormat(48000, 2, 16);
    hr = apo->IsInputFormatSupported(nullptr, &invalidFormat, nullptr);
    CHECK(hr == S_FALSE);

    // 3. LockForProcess with valid format
    APO_CONNECTION_DESCRIPTOR inDesc{};
    inDesc.Type = APO_CONNECTION_BUFFER_TYPE_ALLOCATED;
    inDesc.u32MaxFrameCount = 512;
    inDesc.pFormat = &validFormat;

    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    APO_CONNECTION_DESCRIPTOR* inList[] = { &inDesc };
    APO_CONNECTION_DESCRIPTOR* outList[] = { &outDesc };

    hr = apo->LockForProcess(1, inList, 1, outList);
    CHECK(SUCCEEDED(hr));
    CHECK(apo->isLocked());
    CHECK(apo->activeSampleRate() == 48000.0);
    CHECK(apo->activeChannels() == 2);

    // Unlock
    hr = apo->UnlockForProcess();
    CHECK(SUCCEEDED(hr));
    CHECK(!apo->isLocked());

    apo->Release();
}

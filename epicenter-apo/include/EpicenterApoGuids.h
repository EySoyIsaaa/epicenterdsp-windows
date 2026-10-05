#pragma once

/**
 * @file EpicenterApoGuids.h
 * @brief COM GUIDs and property keys for Epicenter Windows APO.
 */

#include <initguid.h>
#include <guiddef.h>

#if defined(EPICENTER_APO_COMPONENTIZED_BUILD)
// HKR/componentized identity must not reuse the legacy globally registered CLSID.
// {8F7A7248-F617-45AF-ACDA-C1C10F99E4D8}
DEFINE_GUID(CLSID_EpicenterAPO,
    0x8f7a7248, 0xf617, 0x45af, 0xac, 0xda, 0xc1, 0xc1, 0x0f, 0x99, 0xe4, 0xd8);
#define EPICENTER_APO_CLSID_STRING L"{8F7A7248-F617-45AF-ACDA-C1C10F99E4D8}"
#define EPICENTER_APO_PROGID L"EpicenterDSP.ComponentizedAudioProcessingObject.1"
#else
// Legacy development identity remains compatible with previous scripts/builds.
// {68C32E69-906A-4A0E-9214-41A832A88877}
DEFINE_GUID(CLSID_EpicenterAPO,
    0x68c32e69, 0x906a, 0x4a0e, 0x92, 0x14, 0x41, 0xa8, 0x32, 0xa8, 0x88, 0x77);
#define EPICENTER_APO_CLSID_STRING L"{68C32E69-906A-4A0E-9214-41A832A88877}"
#define EPICENTER_APO_PROGID L"EpicenterDSP.AudioProcessingObject.1"
#endif

#define EPICENTER_APO_FRIENDLY_NAME       L"Epicenter DSP Professional APO"
#define EPICENTER_APO_DESCRIPTION         L"Real-time Subharmonic Bass Reconstruction Endpoint Effect"

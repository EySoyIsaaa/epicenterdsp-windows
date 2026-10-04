#pragma once

/**
 * @file EpicenterApoGuids.h
 * @brief COM GUIDs and property keys for Epicenter Windows APO.
 */

#include <initguid.h>
#include <guiddef.h>

// {68C32E69-906A-4A0E-9214-41A832A88877}
DEFINE_GUID(CLSID_EpicenterAPO,
    0x68c32e69, 0x906a, 0x4a0e, 0x92, 0x14, 0x41, 0xa8, 0x32, 0xa8, 0x88, 0x77);

#define EPICENTER_APO_FRIENDLY_NAME       L"Epicenter DSP Professional APO"
#define EPICENTER_APO_DESCRIPTION         L"Real-time Subharmonic Bass Reconstruction Endpoint Effect"
#define EPICENTER_APO_PROGID              L"EpicenterDSP.AudioProcessingObject.1"
#define EPICENTER_APO_CLSID_STRING        L"{68C32E69-906A-4A0E-9214-41A832A88877}"

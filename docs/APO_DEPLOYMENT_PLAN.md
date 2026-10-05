# APO deployment: verified boundaries and next implementation

Read-only investigation on 2026-10-04. No driver package, certificate, trust store,
boot configuration, registry permissions, or endpoint configuration was changed.

## Machine and selected endpoint

- Windows build 26200, display version 25H2. The legacy registry `ProductName`
  says Windows 10 Pro; use build/version, not that value, to choose packaging.
- Endpoint: Salida digital (High Definition Audio Device).
- Endpoint ID and per-machine PnP instance suffix are omitted from this public report.
- PnP parent model: `HDAUDIO\FUNC_01&VEN_10EC&DEV_0892&SUBSYS_18497893&REV_1003`.
- Parent hardware IDs include
  `HDAUDIO\FUNC_01&VEN_10EC&DEV_0892&SUBSYS_18497893`.
- Parent driver: Microsoft `hdaudio.inf`, section `HdAudModel`.
- Existing endpoint effects are Microsoft SFX/MFX; no Epicenter EFX association
  was observed. Its current property-page CLSID must also be preserved.
- Current Release `EpicenterAPO.dll` is unsigned.
- Initially installed Windows Kits included SDK headers but not the packaging
  tools in their `bin` folder. After user authorization, matched official WDK,
  SDK.CPP.x64 and SDK.BuildTools NuGet packages (10.0.26100.6584) were acquired
  under workspace `.tools`, with their Microsoft/NuGet signatures verified.
  InfVerif, Inf2Cat and SignTool are now runnable locally; no system installer was
  used and no PATH or Windows configuration was changed.

## Why the current INF does not solve the permission error

`epicenter-apo/inf/EpicenterAPO.inf` is an extension package matching `NT\Audio`,
with an empty `.Components` section and `HKR,"FxProperties"` registration.
It does not establish the documented audio-parent/APO-component relationship,
does not bind to the actual HDAUDIO hardware ID, and does not associate effects
through the audio filter/pin `FX\<association>` property store.

Windows 11 APO payloads use class `AudioProcessingObject`, GUID
`{5989fce8-9cd0-467d-8a6a-5419e31529d4}`. An extension INF is still a separate
part of componentized installation; changing the single existing INF's class
alone is insufficient.

For this documented model, the APO must be a PnP child of the audio device and
a sibling of its endpoints. Microsoft explicitly excludes a globally registered
componentized APO shared by unrelated drivers. Consequently, a supported
hardware-associated package is not a generic installer for every output device.

## Supported package work

1. Install matched SDK/WDK packaging tools in a development environment.
2. Inspect the actual audio interface reference strings and pin associations
   exposed by this parent driver. Do not assume the digital output is pin zero
   or that an `ANY` association affects only the selected endpoint.
3. Implement a hardware-specific extension INF matching the exact parent ID.
   It creates a unique Epicenter APO child using `AddComponent` and supplies the
   appropriate interface/pin effect association.
4. Implement a second APO-component INF for Windows 11. It copies the DLL to
   DIRID 13 and registers COM/audio-engine metadata under the component's HKR
   keys. Do not use `RegisterDlls`/global self-registration as a substitute.
5. Validate the INF pair using InfVerif and generate catalogs with Inf2Cat.
6. Obtain an accepted signature/trust deployment route. For a development
   machine, any proposed certificate enrollment or test-signing change requires
   its own clear authorization; none has been performed here. Production
   distribution needs its proper driver-signing and validation process.
7. Stage/install through supported driver-package tooling, verify the resulting
   PnP relation and endpoint association, then test actual audio/heartbeat.
8. Validate removal/rollback on the same machine and compare original effect
   properties and driver association before claiming success.

This is not yet a runtime-validated installable product. A generated INF that
has not been installed and verified in audiodg must not be reported as a working
installer. Do not take ownership of protected endpoint keys, change their ACLs,
run a SYSTEM helper, or bypass the access-denied result.

## Offline development package status

The componentized INF foundation uses an `AudioProcessingObject` component and
a separate `Extension` INF. Read-only inspection confirmed the actual digital
filter's reference string `eSpdifOutTopo` under KSCATEGORY_AUDIO and TOPOLOGY.
The extension is specific to the inspected ALC892 hardware ID, not `NT\Audio`
or every sound card. Its EFX/default-mode association is a deployment candidate
that still requires runtime validation; the existing Microsoft SFX/MFX entries
are not directly overwritten by our source INF.

Both source INFs passed official InfVerif `/w`. Inf2Cat generated two catalogs
for Windows 11 CO/NI/GE x64 with **no errors or warnings**. Staging DLL/catalogs
were signed offline with a file-only local development certificate. Catalog
CMS cryptographic signatures were independently checked successfully without
checking trust. SignTool `/pa` correctly rejects the DLL's untrusted root chain.
These results validate format/signability, not accepted installation or audio.

The development identity exists only under `.dev-signing`: an RSA-3072 ephemeral
key exported to an encrypted PFX and a random password protected using this
user's DPAPI. CurrentUser and LocalMachine My/Root/TrustedPublisher stores were
checked: the development certificate is absent from all six. No trust-store,
Secure Boot, test-signing, registry ACL, or driver installation changes were made.

Microsoft also requires updating CLSIDs when moving registration from HKCR to
HKR. The initial offline candidate reused the legacy CLSID and is explicitly
marked DO-NOT-INSTALL. This was corrected using a dedicated build target:

- Legacy `EpicenterAPO.dll`: `{68C32E69-906A-4A0E-9214-41A832A88877}` unchanged.
- Componentized `EpicenterAPOComponent.dll`:
  `{8F7A7248-F617-45AF-ACDA-C1C10F99E4D8}`.

Both development INFs now reference the new identity. The componentized DLL
exports only its class factory and unload entry points, not legacy global
registration. Its real exported factory passed the unregistered smoke test with
the new CLSID and no embedded manifest. A second isolated suite compiled with
the component identity passed **16/16** tests, including regression against Core
and legacy audio, without changing DSP/COM interface/production IPC contracts.

The corrected offline signed candidate is under
`artifacts/apo-development/package-<generated-id>`. Its two INFs again
passed InfVerif `/w`, and Inf2Cat completed without errors or warnings. It has
not been installed, trusted, or loaded by audiodg. Staging tools prefer the
component target in `build-validated` and reject a legacy DLL by verifying its
actual factory CLSID, even if a caller supplies the wrong `-DllPath`.

Reproducible tooling/preparation entry points:

- `scripts/Get-ApoDevelopmentTools.ps1`
- `scripts/New-ApoDevelopmentIdentity.ps1` (one-time file-only identity)
- `scripts/Build-ApoDevelopmentPackage.ps1 -Sign -DllPath <actual rebuilt DLL>`

Installing this candidate or adding its development certificate to trusted
Windows stores is a separate action requiring clear approval. An already
disabled protected-audio setting does not establish permission to change it or
to claim that an untrusted self-signed package is production-ready.

## DLL/build fixes independent of installation

- Corrected `DllMain.cpp`'s invalid `APOInterface0` IID to the Microsoft sample's
  `IAudioProcessingObject`, `{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}`.
- Corrected the single registration-metadata IID in `GetRegistrationProperties`
  to the same existing interface. No exposed COM interface was added or removed.
- `DllRegisterServer` now propagates every audio-engine metadata write failure.
  `DllUnregisterServer` now reports failed removals instead of false success.
- `MaxInstances` is consistently 32 in runtime and legacy registry metadata.
- The APO DLL now explicitly uses `/MANIFEST:NO`. Microsoft warns that an
  embedded manifest can prevent loading in protected audiodg even when signed.
  The generated Visual Studio project shows `GenerateManifest=false` for all
  configurations. Inspect the final DLL before packaging.
- `scripts/Test-ApoBinary.ps1` performs an unregistered, read-only smoke test of
  the built DLL's class factory, registration properties, and manifest resources.
  It never calls `DllRegisterServer` and is not an audiodg integration test.
- Supported DSP buffers are float32 mono/stereo. Digital compressed passthrough,
  exclusive-mode streams, multichannel formats, and mismatched connection
  formats are not validated by the mock suites.

## Primary references

- [Componentized APO registration and load-failure diagnosis](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
- [Windows 11 APO setup class](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/deploying-audio-processing-objects)
- [AddComponent creates a child of the current device](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addcomponent-directive)
- [HKR meanings depend on the INF section](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addreg-directive)
- [Endpoint-effect INF association](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/pkey-fx-endpointeffectclsid)

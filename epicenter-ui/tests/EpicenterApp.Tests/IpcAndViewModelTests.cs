using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using EpicenterApp.Models;
using EpicenterApp.Services;
using EpicenterApp.ViewModels;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicenterApp.Tests;

[TestClass]
public class IpcAndViewModelTests
{
    [TestMethod]
    public void Test_IpcStructOffsets_Match_CppApoLayout()
    {
        // Assert Control and Telemetry block sizes
        Assert.AreEqual(52, Marshal.SizeOf<EpicenterControlBlock>(), "EpicenterControlBlock size must be exactly 52 bytes");
        Assert.AreEqual(64, Marshal.SizeOf<EpicenterTelemetryBlock>(), "EpicenterTelemetryBlock size must be exactly 64 bytes");

        // Assert Header Offsets
        Assert.AreEqual(0, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("Magic"));
        Assert.AreEqual(4, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("ProtocolVersion"));
        Assert.AreEqual(8, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("StructSize"));
        Assert.AreEqual(12, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("InitFlags"));
        Assert.AreEqual(16, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("UiLastHeartbeatQpc"));
        Assert.AreEqual(24, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("UiProcessId"));
        Assert.AreEqual(28, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("HeaderReserved"));

        // Control block must start at offset 32
        Assert.AreEqual(32, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("Control"));

        // Telemetry block must start immediately after Control block: 32 + 52 = 84
        Assert.AreEqual(84, (int)Marshal.OffsetOf<EpicenterIpcSharedState>("Telemetry"));

        // Control internal offsets
        Assert.AreEqual(0, (int)Marshal.OffsetOf<EpicenterControlBlock>("Sequence"));
        Assert.AreEqual(4, (int)Marshal.OffsetOf<EpicenterControlBlock>("Enabled"));
        Assert.AreEqual(8, (int)Marshal.OffsetOf<EpicenterControlBlock>("Intensity"));
        Assert.AreEqual(12, (int)Marshal.OffsetOf<EpicenterControlBlock>("AbState"));
        Assert.AreEqual(16, (int)Marshal.OffsetOf<EpicenterControlBlock>("TargetPreset"));

        // Telemetry internal offsets
        Assert.AreEqual(0, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("ApoHeartbeat"));
        Assert.AreEqual(4, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("SampleRate"));
        Assert.AreEqual(8, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("ChannelCount"));
        Assert.AreEqual(12, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("BufferFrameCount"));
        Assert.AreEqual(16, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("DspTimeMicroseconds"));
        Assert.AreEqual(20, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("PeakDbfs"));
        Assert.AreEqual(24, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("RmsDbfs"));
        Assert.AreEqual(28, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("Underruns"));
        Assert.AreEqual(32, (int)Marshal.OffsetOf<EpicenterTelemetryBlock>("ApoStatus"));
    }

    [TestMethod]
    public void Test_PresetDefinitions_Match_ProductRequirements()
    {
        var presets = PresetItem.BuiltInPresets;
        Assert.AreEqual(5, presets.Length, "Must have exactly 5 presets");

        Assert.AreEqual(0, presets[0].Id);
        Assert.AreEqual("Default Studio", presets[0].Name);
        Assert.AreEqual(75.0f, presets[0].DefaultIntensity);

        Assert.AreEqual(1, presets[1].Id);
        Assert.AreEqual("Gentle", presets[1].Name);
        Assert.AreEqual(45.0f, presets[1].DefaultIntensity);

        Assert.AreEqual(2, presets[2].Id);
        Assert.AreEqual("Deep", presets[2].Name);
        Assert.AreEqual(85.0f, presets[2].DefaultIntensity);

        Assert.AreEqual(3, presets[3].Id);
        Assert.AreEqual("Power", presets[3].Name);
        Assert.AreEqual(100.0f, presets[3].DefaultIntensity);

        Assert.AreEqual(4, presets[4].Id);
        Assert.AreEqual("Custom", presets[4].Name);
    }

    [TestMethod]
    public void Test_ApoTelemetryData_CalculatedCpuPercent()
    {
        var telem = new ApoTelemetryData
        {
            SampleRate = 48000,
            BufferFrameCount = 480, // 480 frames @ 48kHz = 10 ms = 10,000 µs quantum
            DspTimeMicroseconds = 120.0f // 120 µs of DSP processing
        };

        // Expected CPU % = 120 / 10,000 * 100 = 1.2%
        double cpu = telem.DspCpuPercent;
        Assert.AreEqual(1.2, cpu, 0.01, "DSP CPU% calculation must match quantum ratio");
    }

    [TestMethod]
    public unsafe void Test_SeqLock_ReadWrite_Via_SharedMemory()
    {
        string mapName = "Local\\TestEpicenterDspIpc_" + Guid.NewGuid().ToString("N");
        using var mmf = MemoryMappedFile.CreateNew(mapName, 4096, MemoryMappedFileAccess.ReadWrite);
        using var accessor = mmf.CreateViewAccessor(0, 4096, MemoryMappedFileAccess.ReadWrite);

        byte* ptr = null;
        accessor.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);

        try
        {
            var state = (EpicenterIpcSharedState*)ptr;
            state->Magic = 0x45504943u; // 'EPIC'
            state->ProtocolVersion = 1;
            state->StructSize = (uint)sizeof(EpicenterIpcSharedState);
            state->InitFlags = 1;
            state->Control.Sequence = 0;
            state->Control.Enabled = 1;
            state->Control.Intensity = 50.0f;
            state->Control.AbState = 1;

            // Telemetry mock
            state->Telemetry.SampleRate = 96000;
            state->Telemetry.ChannelCount = 2;
            state->Telemetry.BufferFrameCount = 512;
            state->Telemetry.DspTimeMicroseconds = 45.0f;
            state->Telemetry.PeakDbfs = -3.2f;
            state->Telemetry.RmsDbfs = -14.5f;
            state->Telemetry.ApoStatus = 1;
            state->Telemetry.ApoHeartbeat = 12345;

            // Assert values readable
            Assert.AreEqual(1u, state->Control.Enabled);
            Assert.AreEqual(50.0f, state->Control.Intensity);
            Assert.AreEqual(1u, state->Control.AbState);

            Assert.AreEqual(96000u, state->Telemetry.SampleRate);
            Assert.AreEqual(2u, state->Telemetry.ChannelCount);
            Assert.AreEqual(512u, state->Telemetry.BufferFrameCount);
            Assert.AreEqual(-3.2f, state->Telemetry.PeakDbfs, 1e-4f);
            Assert.AreEqual(-14.5f, state->Telemetry.RmsDbfs, 1e-4f);
            Assert.AreEqual(12345u, state->Telemetry.ApoHeartbeat);
        }
        finally
        {
            if (ptr != null)
            {
                accessor.SafeMemoryMappedViewHandle.ReleasePointer();
            }
        }
    }

    [TestMethod]
    public void Test_MainViewModel_PowerAndAbLogic()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        // Initial defaults
        Assert.IsTrue(vm.IsEnabled);
        Assert.IsTrue(vm.IsAbEpicenter);
        Assert.AreEqual(75.0, vm.Intensity);
        Assert.AreEqual("Default Studio", vm.SelectedPreset.Name);

        // Toggle Power -> OFF
        vm.TogglePower();
        Assert.IsFalse(vm.IsEnabled);
        Assert.IsFalse(mockIpc.LastEnabled);
        Assert.AreEqual(ApoConnectionStatus.Bypass, vm.ConnectionStatus);
        Assert.AreEqual("○ BYPASS", vm.StatusBadgeText);

        // Toggle Power -> ON (returns to previous state A Original, which is Bypass)
        vm.TogglePower();
        Assert.IsTrue(vm.IsEnabled);
        Assert.IsTrue(mockIpc.LastEnabled);
        Assert.AreEqual(ApoConnectionStatus.Bypass, vm.ConnectionStatus);
        Assert.AreEqual("A ORIGINAL", vm.StatusBadgeText);

        // Select A (Original)
        vm.SelectOriginalA();
        Assert.IsFalse(vm.IsAbEpicenter);
        Assert.AreEqual(0u, mockIpc.LastAbState);
        Assert.AreEqual(ApoConnectionStatus.Bypass, vm.ConnectionStatus);
        Assert.AreEqual("A ORIGINAL", vm.StatusBadgeText);

        // Select B (Epicenter)
        vm.SelectEpicenterB();
        Assert.IsTrue(vm.IsAbEpicenter);
        Assert.AreEqual(1u, mockIpc.LastAbState);
        Assert.AreEqual(ApoConnectionStatus.Active, vm.ConnectionStatus);
        Assert.AreEqual("● ACTIVE", vm.StatusBadgeText);

        // Intensity bounds adjustment
        vm.Intensity = 50.0;
        vm.AdjustIntensity(+10.0);
        Assert.AreEqual(60.0, vm.Intensity);
        Assert.AreEqual(60.0f, mockIpc.LastIntensity);

        // Clamping at 100.0
        vm.AdjustIntensity(+50.0);
        Assert.AreEqual(100.0, vm.Intensity);

        // Clamping at 0.0
        vm.AdjustIntensity(-150.0);
        Assert.AreEqual(0.0, vm.Intensity);

        // Preset switching
        vm.ApplyPreset(PresetItem.BuiltInPresets[2]); // Deep (85%)
        Assert.AreEqual(85.0, vm.Intensity);
        Assert.AreEqual(2u, mockIpc.LastPreset);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_MainViewModel_KnobAdjustment_SwitchesToCustomPreset()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        // Start with Default Studio (75%)
        vm.ApplyPreset(PresetItem.BuiltInPresets[0]);
        Assert.AreEqual("Default Studio", vm.SelectedPreset.Name);
        Assert.AreEqual(75.0, vm.Intensity);

        // User moves knob to 82% (Requirement #13)
        vm.Intensity = 82.0;

        // UI must automatically switch to Custom
        Assert.AreEqual("Custom", vm.SelectedPreset.Name);
        Assert.AreEqual(82.0, vm.Intensity);

        // User selects Gentle (45%)
        vm.ApplyPreset(PresetItem.BuiltInPresets[1]);
        Assert.AreEqual("Gentle", vm.SelectedPreset.Name);
        Assert.AreEqual(45.0, vm.Intensity);

        // User adjusts knob by -3%
        vm.AdjustIntensity(-3.0);
        Assert.AreEqual(42.0, vm.Intensity);
        Assert.AreEqual("Custom", vm.SelectedPreset.Name);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_SettingsStorage_SaveLoad_And_CorruptFileRecovery()
    {
        string tempPath = Path.Combine(Path.GetTempPath(), "epicenter_test_settings_" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            var storage = new SettingsStorageService(tempPath);

            // 1. Loading non-existent file returns default settings safely
            var initial = storage.LoadSettings();
            Assert.IsNotNull(initial);
            Assert.AreEqual(75.0, initial.Intensity);
            Assert.AreEqual(0, initial.PresetId);
            Assert.IsTrue(initial.IsEnabled);

            // 2. Save settings and reload
            var toSave = new UserSettings
            {
                Intensity = 85.0,
                PresetId = 2,
                IsEnabled = false,
                IsAbEpicenter = false
            };
            storage.SaveSettings(toSave);

            var loaded = storage.LoadSettings();
            Assert.AreEqual(85.0, loaded.Intensity);
            Assert.AreEqual(2, loaded.PresetId);
            Assert.IsFalse(loaded.IsEnabled);
            Assert.IsFalse(loaded.IsAbEpicenter);

            // 3. Write corrupted/truncated JSON to file
            File.WriteAllText(tempPath, "{ corrupted json: invalid [[[[");

            // Must not throw, and must fall back gracefully to default settings
            var recovered = storage.LoadSettings();
            Assert.IsNotNull(recovered);
            Assert.AreEqual(75.0, recovered.Intensity);
            Assert.AreEqual(0, recovered.PresetId);
            Assert.IsTrue(recovered.IsEnabled);

            // 4. Test out-of-range sanitization
            File.WriteAllText(tempPath, "{\"Intensity\": 999.0, \"PresetId\": 88, \"IsEnabled\": true, \"IsAbEpicenter\": true}");
            var sanitized = storage.LoadSettings();
            Assert.AreEqual(100.0, sanitized.Intensity, "Out-of-range intensity must clamp to 100.0");
            Assert.AreEqual(0, sanitized.PresetId, "Out-of-range preset must clamp to 0");
        }
        finally
        {
            if (File.Exists(tempPath)) File.Delete(tempPath);
        }
    }

    [TestMethod]
    public void Test_Diagnostics_ReportGeneration_And_FileExport()
    {
        var diag = new DiagnosticsService();

        var telem = new ApoTelemetryData
        {
            SampleRate = 48000,
            ChannelCount = 2,
            BufferFrameCount = 256,
            DspTimeMicroseconds = 24.5f,
            PeakDbfs = -3.5f,
            RmsDbfs = -16.2f,
            ApoStatus = 1,
            ApoHeartbeat = 5555
        };

        var endpoint = new AudioEndpointInfo(
            Id: "{0.0.0.00000000}.{test}",
            Name: "Speakers (Studio USB DAC)",
            IsDefault: true,
            Status: "Active",
            IsApoBound: true
        );

        string report = diag.GenerateDiagnosticReport(
            ApoConnectionStatus.Active,
            isIpcConnected: true,
            telem,
            endpoint,
            currentIntensity: 75.0,
            isEnabled: true,
            isAbEpicenter: true,
            presetName: "Default Studio"
        );

        Assert.IsFalse(string.IsNullOrWhiteSpace(report));
        StringAssert.Contains(report, "EPICENTER DSP PROFESSIONAL 1.0 — SYSTEM DIAGNOSTIC REPORT");
        StringAssert.Contains(report, "Speakers (Studio USB DAC)");
        StringAssert.Contains(report, "48,000 Hz");
        StringAssert.Contains(report, "256 frames");
        StringAssert.Contains(report, "audiodg.exe");
        StringAssert.Contains(report, "ACTIVE");
        StringAssert.Contains(report, "Default Studio");

        // Export to disk test
        string tempLog = Path.Combine(Path.GetTempPath(), "test_diag_" + Guid.NewGuid().ToString("N") + ".log");
        try
        {
            string exportedPath = diag.ExportDiagnosticLogToFile(report, tempLog);
            Assert.IsTrue(File.Exists(exportedPath));
            string readContent = File.ReadAllText(exportedPath);
            Assert.AreEqual(report, readContent);
        }
        finally
        {
            if (File.Exists(tempLog)) File.Delete(tempLog);
        }
    }

    [TestMethod]
    public void Test_MainViewModel_DrawerNavigation_And_Tabs()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        // Initially closed
        Assert.IsFalse(vm.IsDrawerOpen);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Collapsed, vm.DrawerVisibility);

        // Open Tab 0 (Presets)
        vm.OpenDrawer(0);
        Assert.IsTrue(vm.IsDrawerOpen);
        Assert.AreEqual(0, vm.DrawerTab);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Visible, vm.DrawerVisibility);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Visible, vm.PresetsTabVisibility);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Collapsed, vm.AudioSettingsTabVisibility);

        // Open Tab 1 (Audio Settings)
        vm.OpenDrawer(1);
        Assert.AreEqual(1, vm.DrawerTab);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Collapsed, vm.PresetsTabVisibility);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Visible, vm.AudioSettingsTabVisibility);

        // Open Tab 2 (Diagnostics)
        vm.OpenDrawer(2);
        Assert.AreEqual(2, vm.DrawerTab);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Visible, vm.DiagnosticsTabVisibility);

        // Toggle Drawer Tab 2 (closes it)
        vm.ToggleDrawer(2);
        Assert.IsFalse(vm.IsDrawerOpen);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Collapsed, vm.DrawerVisibility);

        // CloseDrawer command
        vm.OpenDrawer(0);
        Assert.IsTrue(vm.IsDrawerOpen);
        vm.CloseDrawer();
        Assert.IsFalse(vm.IsDrawerOpen);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_MainViewModel_Strict_Power_And_Ab_Separation()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        // Initially Enabled=true, IsAbEpicenter=true
        Assert.IsTrue(vm.IsEnabled);
        Assert.IsTrue(vm.IsAbEpicenter);
        Assert.AreEqual("LISTEN: B — EPICENTER", vm.ListenStatusText);
        Assert.AreEqual("DSP ENGINE: ON", vm.DspPowerStatusText);
        Assert.AreEqual("● ACTIVE", vm.StatusBadgeText);

        // Turn Power OFF
        vm.TogglePower();
        Assert.IsFalse(vm.IsEnabled);
        // Requirement: Turning OFF must switch Listen state to A (Original)
        Assert.IsFalse(vm.IsAbEpicenter, "Disabling power must force A Original");
        Assert.AreEqual("LISTEN: A — ORIGINAL", vm.ListenStatusText);
        Assert.AreEqual("DSP ENGINE: OFF", vm.DspPowerStatusText);
        Assert.AreEqual("○ BYPASS", vm.StatusBadgeText);
        Assert.AreEqual(0u, mockIpc.LastAbState, "IPC AbState must be 0 when bypassed");

        // When currently OFF, user explicitly selects B (Epicenter)
        // Requirement: Selecting B must automatically turn Power ON
        vm.SelectEpicenterB();
        Assert.IsTrue(vm.IsEnabled, "Selecting B Epicenter must automatically turn Power ON");
        Assert.IsTrue(vm.IsAbEpicenter);
        Assert.AreEqual("LISTEN: B — EPICENTER", vm.ListenStatusText);
        Assert.AreEqual("DSP ENGINE: ON", vm.DspPowerStatusText);
        Assert.AreEqual("● ACTIVE", vm.StatusBadgeText);
        Assert.AreEqual(1u, mockIpc.LastAbState);
        Assert.IsTrue(mockIpc.LastEnabled);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_MainViewModel_EndpointChange_Notification_And_State()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        Assert.AreEqual("Speakers (Studio USB DAC)", vm.CurrentEndpointName);
        Assert.AreEqual("", vm.DeviceChangeNotificationText);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Collapsed, vm.DeviceChangeNotificationVisibility);

        // Simulate default audio device change event from Windows
        mockEndpoint.TriggerChange();

        // Notification must update and become visible
        Assert.AreEqual("OUTPUT CHANGED: Speakers (Studio USB DAC)", vm.DeviceChangeNotificationText);
        Assert.AreEqual(Microsoft.UI.Xaml.Visibility.Visible, vm.DeviceChangeNotificationVisibility);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_MainViewModel_RetryConnection_Reinitializes()
    {
        var mockIpc = new MockIpcService();
        var mockSettings = new MockSettingsService();
        var mockEndpoint = new MockEndpointService();
        var mockDiag = new DiagnosticsService();

        var vm = new MainViewModel(mockIpc, mockSettings, mockEndpoint, mockDiag);

        vm.RetryConnection();

        Assert.AreEqual(ApoConnectionStatus.Connecting, vm.ConnectionStatus);
        Assert.AreEqual("◌ CONNECTING...", vm.StatusBadgeText);
        Assert.AreEqual("Speakers (Studio USB DAC)", vm.CurrentEndpointName);

        vm.Dispose();
    }

    [TestMethod]
    public void Test_RotaryKnob_Precision_String_Formatting()
    {
        // Whole numbers should display as integer without decimal places
        Assert.AreEqual("75", Controls.RotaryKnob.FormatValue(75.0));
        Assert.AreEqual("0", Controls.RotaryKnob.FormatValue(0.0));
        Assert.AreEqual("100", Controls.RotaryKnob.FormatValue(100.0));
        Assert.AreEqual("45", Controls.RotaryKnob.FormatValue(45.0001));

        // Fractional fine-tuning numbers should display with 1 decimal place
        Assert.AreEqual("75.2", Controls.RotaryKnob.FormatValue(75.2));
        Assert.AreEqual("45.5", Controls.RotaryKnob.FormatValue(45.5));
        Assert.AreEqual("82.8", Controls.RotaryKnob.FormatValue(82.78));
        Assert.AreEqual("0.1", Controls.RotaryKnob.FormatValue(0.12));
    }

    [TestMethod]
    public void ZeroIntensity_ReportsDirectBypass_AndSanitizesNonFiniteInput()
    {
        var ipc = new MockIpcService();
        using var vm = new MainViewModel(ipc, new MockSettingsService(), new MockEndpointService(), new DiagnosticsService());
        vm.Intensity = 0;
        Assert.AreEqual(ApoConnectionStatus.Bypass, vm.ConnectionStatus);
        Assert.AreEqual("○ BYPASS · 0%", vm.StatusBadgeText);
        Assert.AreEqual("LISTEN: A — ORIGINAL", vm.ListenStatusText);
        Assert.IsFalse(vm.OutputMeterAvailable);
        Assert.AreEqual(0f, ipc.LastIntensity);
        vm.Intensity = double.NaN;
        Assert.AreEqual(0.0, vm.Intensity);
        vm.Intensity = double.PositiveInfinity;
        Assert.AreEqual(0.0, vm.Intensity);
    }

    [TestMethod]
    public void UnboundEndpoint_CannotReportActive_EvenWithOpenIpc()
    {
        using var vm = new MainViewModel(new MockIpcService(), new MockSettingsService(),
            new MockEndpointService { IsApoBound = false }, new DiagnosticsService());
        Assert.AreEqual(ApoConnectionStatus.NotBound, vm.ConnectionStatus);
        Assert.IsTrue(vm.IsAudioEngineUnavailable);
        Assert.IsFalse(vm.OutputMeterAvailable);
    }

    [TestMethod]
    public void OpenIpc_WithoutPreparedApo_CannotReportActive()
    {
        using var vm = new MainViewModel(new MockIpcService { TelemetryAvailable = false },
            new MockSettingsService(), new MockEndpointService(), new DiagnosticsService());
        Assert.AreEqual(ApoConnectionStatus.Connecting, vm.ConnectionStatus);
        Assert.IsTrue(vm.IsAudioEngineUnavailable);
        Assert.AreEqual("N/A CPU", vm.DspCpuText);
    }

    [TestMethod]
    public void ApoError_ReportsErrorBypass_NotActive()
    {
        using var vm = new MainViewModel(new MockIpcService { ApoStatus = 2 },
            new MockSettingsService(), new MockEndpointService(), new DiagnosticsService());
        Assert.AreEqual(ApoConnectionStatus.Error, vm.ConnectionStatus);
        StringAssert.Contains(vm.StatusBadgeText, "BYPASS");
        Assert.IsFalse(vm.OutputMeterAvailable);
    }

    [TestMethod]
    public void Retry_ReleasesOldMapping_AndDisposeRequestsBypass()
    {
        var ipc = new MockIpcService();
        var vm = new MainViewModel(ipc, new MockSettingsService(), new MockEndpointService(), new DiagnosticsService());
        vm.RetryConnection();
        Assert.AreEqual(1, ipc.DisconnectCalls);
        Assert.AreEqual(2, ipc.ConnectCalls);
        Assert.AreEqual(ApoConnectionStatus.Connecting, vm.ConnectionStatus);
        Assert.IsFalse(vm.OutputMeterAvailable);
        vm.Dispose();
        Assert.IsFalse(ipc.LastEnabled);
        Assert.AreEqual(0f, ipc.LastIntensity);
        Assert.AreEqual(0u, ipc.LastAbState);
    }

    [TestMethod]
    public void EffectAssociation_RecognizesBothBuilds_NotUiPropertyClsid()
    {
        Assert.IsTrue(WindowsAudioEndpointService.IsKnownEpicenterEffect("{68C32E69-906A-4A0E-9214-41A832A88877}"));
        Assert.IsTrue(WindowsAudioEndpointService.IsKnownEpicenterEffect("{8f7a7248-f617-45af-acda-c1c10f99e4d8}"));
        Assert.IsFalse(WindowsAudioEndpointService.IsKnownEpicenterEffect("{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}"));
        Assert.IsFalse(WindowsAudioEndpointService.IsKnownEpicenterEffect(null));
    }

    private class MockIpcService : IIpcBridgeService
    {
        public bool IsConnected => true;
        public bool TelemetryAvailable { get; set; } = true;
        public uint ApoStatus { get; set; } = 1;
        public int ConnectCalls { get; private set; }
        public int DisconnectCalls { get; private set; }
        public bool LastEnabled { get; private set; }
        public float LastIntensity { get; private set; }
        public uint LastAbState { get; private set; }
        public uint LastPreset { get; private set; }

        public void Connect() { ConnectCalls++; }
        public void Disconnect() { DisconnectCalls++; }
        public void SetParameters(bool enabled, float intensity, uint abState, uint targetPreset = 0)
        {
            LastEnabled = enabled;
            LastIntensity = intensity;
            LastAbState = abState;
            LastPreset = targetPreset;
        }

        public bool TryReadTelemetry(out ApoTelemetryData telemetry)
        {
            telemetry = new ApoTelemetryData
            {
                ApoHeartbeat = 1,
                SampleRate = 48000,
                ChannelCount = 2,
                BufferFrameCount = 256,
                DspTimeMicroseconds = 20.0f,
                PeakDbfs = -6.0f,
                RmsDbfs = -18.0f,
                ApoStatus = ApoStatus
            };
            return TelemetryAvailable;
        }

        public void Dispose() { }
    }

    private class MockSettingsService : ISettingsStorageService
    {
        public UserSettings CurrentSettings { get; set; } = new()
        {
            Intensity = 75.0,
            PresetId = 0,
            IsEnabled = true,
            IsAbEpicenter = true
        };

        public UserSettings LoadSettings() => CurrentSettings;
        public void SaveSettings(UserSettings settings) => CurrentSettings = settings;
    }

    private class MockEndpointService : IAudioEndpointService
    {
        public bool IsApoBound { get; set; } = true;
        public event EventHandler? DefaultDeviceChanged;

        public AudioEndpointInfo? GetDefaultRenderEndpoint() => new(
            Id: "mock_default",
            Name: "Speakers (Studio USB DAC)",
            IsDefault: true,
            Status: "Active",
            IsApoBound: IsApoBound
        );

        public IReadOnlyList<AudioEndpointInfo> GetRenderEndpoints() =>
        [
            GetDefaultRenderEndpoint()!
        ];

        public void Refresh() { }
        public void TriggerChange() => DefaultDeviceChanged?.Invoke(this, EventArgs.Empty);
        public void Dispose() { }
    }
}

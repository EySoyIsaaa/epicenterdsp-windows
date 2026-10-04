using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EpicenterApp.Models;
using EpicenterApp.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Media;

namespace EpicenterApp.ViewModels;

public partial class MainViewModel : ObservableObject, IDisposable
{
    private readonly IIpcBridgeService _ipc;
    private readonly ISettingsStorageService _settingsService;
    private readonly IAudioEndpointService _endpointService;
    private readonly IDiagnosticsService _diagnosticsService;
    private readonly DispatcherQueueTimer? _telemetryTimer;

    private uint _lastHeartbeat;
    private int _staleCount;
    private int _tickCount;
    private ApoTelemetryData _latestTelemetry;
    private bool _isAdjustingProgrammatically;

    [ObservableProperty]
    private double _intensity = 75.0;

    [ObservableProperty]
    private bool _isEnabled = true;

    [ObservableProperty]
    private bool _isAbEpicenter = true; // true = B (Epicenter), false = A (Original)

    [ObservableProperty]
    private ApoConnectionStatus _connectionStatus = ApoConnectionStatus.Connecting;

    [ObservableProperty]
    private string _statusBadgeText = "◌ CONNECTING...";

    [ObservableProperty]
    private PresetItem _selectedPreset = PresetItem.BuiltInPresets[0]; // Default Studio

    [ObservableProperty]
    private double _inputPeakDbfs = -96.0;

    [ObservableProperty]
    private double _inputRmsDbfs = -96.0;

    [ObservableProperty]
    private double _outputPeakDbfs = -96.0;

    [ObservableProperty]
    private double _outputRmsDbfs = -96.0;

    [ObservableProperty]
    private string _sampleRateText = "N/A";

    [ObservableProperty]
    private string _bitDepthText = "32-bit Float";

    [ObservableProperty]
    private string _channelsText = "Stereo";

    [ObservableProperty]
    private string _bufferFramesText = "N/A";

    [ObservableProperty]
    private string _latencyText = "N/A";

    [ObservableProperty]
    private string _dspCpuText = "0.0% CPU";

    [ObservableProperty]
    private string _activeEndpointText = "Default Render Device";

    // Device change notification & Error banner state
    [ObservableProperty]
    private string _deviceChangeNotificationText = "";

    [ObservableProperty]
    private bool _isAudioEngineUnavailable;

    [ObservableProperty]
    private string _audioEngineUnavailableMessage = "AUDIO ENGINE UNAVAILABLE · Epicenter DSP is reconnecting...";

    // Drawer / Panel States
    [ObservableProperty]
    private bool _isDrawerOpen;

    [ObservableProperty]
    private int _drawerTab; // 0 = Presets, 1 = Audio Settings, 2 = Diagnostics

    // Audio Settings Drawer Properties
    [ObservableProperty]
    private string _currentEndpointName = "Default Render Device";

    [ObservableProperty]
    private string _currentEndpointStatus = "ACTIVE";

    [ObservableProperty]
    private string _apoBoundStatusText = "NOT BOUND";

    [ObservableProperty]
    private string _formatSampleRate = "N/A";

    [ObservableProperty]
    private string _formatBitDepth = "32-bit Float";

    [ObservableProperty]
    private string _formatChannels = "Stereo";

    [ObservableProperty]
    private string _formatBufferFrames = "N/A";

    [ObservableProperty]
    private string _formatLatencyMs = "N/A";

    // Diagnostics Drawer Properties
    [ObservableProperty]
    private string _audiodgPidText = "PID: Querying...";

    [ObservableProperty]
    private string _ipcStatusText = "DISCONNECTED";

    [ObservableProperty]
    private string _dspStatusText = "IDLE";

    [ObservableProperty]
    private string _exportNotificationText = "";

    // Visual Presentation Brushes for Buttons & Badges (Clean x:Bind)
    private static readonly SolidColorBrush BrushAmber = new(Microsoft.UI.ColorHelper.FromArgb(255, 245, 158, 11));
    private static readonly SolidColorBrush BrushCyan = new(Microsoft.UI.ColorHelper.FromArgb(255, 6, 182, 212));
    private static readonly SolidColorBrush BrushGreen = new(Microsoft.UI.ColorHelper.FromArgb(255, 16, 185, 129));
    private static readonly SolidColorBrush BrushOrange = new(Microsoft.UI.ColorHelper.FromArgb(255, 245, 158, 11));
    private static readonly SolidColorBrush BrushCardDark = new(Microsoft.UI.ColorHelper.FromArgb(255, 30, 35, 45));
    private static readonly SolidColorBrush BrushTransparent = new(Microsoft.UI.ColorHelper.FromArgb(0, 0, 0, 0));
    private static readonly SolidColorBrush BrushTextMuted = new(Microsoft.UI.ColorHelper.FromArgb(255, 100, 116, 139));
    private static readonly SolidColorBrush BrushTextHigh = new(Microsoft.UI.ColorHelper.FromArgb(255, 248, 250, 252));
    private static readonly SolidColorBrush BrushBlack = new(Microsoft.UI.ColorHelper.FromArgb(255, 0, 0, 0));

    public Brush StatusBrush => ConnectionStatus switch
    {
        ApoConnectionStatus.Active => BrushGreen,
        ApoConnectionStatus.Bypass => BrushTextMuted,
        ApoConnectionStatus.NotBound => BrushOrange,
        _ => BrushCyan
    };

    public Brush PowerOffBackground => !IsEnabled ? BrushCardDark : BrushTransparent;
    public Brush PowerOffForeground => !IsEnabled ? BrushTextHigh : BrushTextMuted;
    public Brush PowerOnBackground => IsEnabled ? BrushAmber : BrushTransparent;
    public Brush PowerOnForeground => IsEnabled ? BrushBlack : BrushTextMuted;

    public Brush AbOriginalBackground => !IsAbEpicenter ? BrushCyan : BrushTransparent;
    public Brush AbOriginalForeground => !IsAbEpicenter ? BrushBlack : BrushTextMuted;
    public Brush AbEpicenterBackground => IsAbEpicenter ? BrushAmber : BrushTransparent;
    public Brush AbEpicenterForeground => IsAbEpicenter ? BrushBlack : BrushTextMuted;

    // Computed presentation texts
    public string ListenStatusText => (IsEnabled && IsAbEpicenter) ? "LISTEN: B — EPICENTER" : "LISTEN: A — ORIGINAL";
    public string DspPowerStatusText => IsEnabled ? "DSP ENGINE: ON" : "DSP ENGINE: OFF";

    // Drawer Visibility & Tab Styles
    public Microsoft.UI.Xaml.Visibility DrawerVisibility => IsDrawerOpen ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
    public Microsoft.UI.Xaml.Visibility PresetsTabVisibility => (IsDrawerOpen && DrawerTab == 0) ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
    public Microsoft.UI.Xaml.Visibility AudioSettingsTabVisibility => (IsDrawerOpen && DrawerTab == 1) ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
    public Microsoft.UI.Xaml.Visibility DiagnosticsTabVisibility => (IsDrawerOpen && DrawerTab == 2) ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;

    public Microsoft.UI.Xaml.Visibility AudioEngineUnavailableVisibility => IsAudioEngineUnavailable ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
    public Microsoft.UI.Xaml.Visibility DeviceChangeNotificationVisibility => !string.IsNullOrEmpty(DeviceChangeNotificationText) ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;

    public Brush PresetsTabBackground => DrawerTab == 0 ? BrushAmber : BrushTransparent;
    public Brush PresetsTabForeground => DrawerTab == 0 ? BrushBlack : BrushTextMuted;

    public Brush AudioSettingsTabBackground => DrawerTab == 1 ? BrushAmber : BrushTransparent;
    public Brush AudioSettingsTabForeground => DrawerTab == 1 ? BrushBlack : BrushTextMuted;

    public Brush DiagnosticsTabBackground => DrawerTab == 2 ? BrushAmber : BrushTransparent;
    public Brush DiagnosticsTabForeground => DrawerTab == 2 ? BrushBlack : BrushTextMuted;

    public IReadOnlyList<PresetItem> Presets => PresetItem.BuiltInPresets;

    public MainViewModel(
        IIpcBridgeService? ipc = null,
        ISettingsStorageService? settings = null,
        IAudioEndpointService? endpoints = null,
        IDiagnosticsService? diagnostics = null)
    {
        _ipc = ipc ?? new ApoIpcBridgeService();
        _settingsService = settings ?? new SettingsStorageService();
        _endpointService = endpoints ?? new WindowsAudioEndpointService();
        _diagnosticsService = diagnostics ?? new DiagnosticsService();

        // 1. Load user settings
        var savedSettings = _settingsService.LoadSettings();
        _isAdjustingProgrammatically = true;
        _intensity = savedSettings.Intensity;
        _isEnabled = savedSettings.IsEnabled;
        _isAbEpicenter = savedSettings.IsAbEpicenter;
        _selectedPreset = (savedSettings.PresetId >= 0 && savedSettings.PresetId < PresetItem.BuiltInPresets.Length)
            ? PresetItem.BuiltInPresets[savedSettings.PresetId]
            : PresetItem.BuiltInPresets[0];

        // Ensure consistency at startup
        if (!_isEnabled)
        {
            _isAbEpicenter = false;
        }
        _isAdjustingProgrammatically = false;

        // 2. Query initial endpoint
        var defaultEndpoint = _endpointService.GetDefaultRenderEndpoint();
        if (defaultEndpoint != null)
        {
            CurrentEndpointName = defaultEndpoint.Name;
            ActiveEndpointText = defaultEndpoint.Name;
        }
        _endpointService.DefaultDeviceChanged += OnDefaultDeviceChanged;

        // 3. Connect to Shared Memory IPC and push parameters
        _ipc.Connect();
        PushParametersToIpc();
        UpdateStatusBadge();

        // 4. Query initial audiodg process ID
        UpdateAudiodgPid();

        // 5. 30 Hz Telemetry Poll (~33 ms) - non-blocking, zero CPU spike
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        if (dispatcher != null)
        {
            _telemetryTimer = dispatcher.CreateTimer();
            _telemetryTimer.Interval = TimeSpan.FromMilliseconds(33);
            _telemetryTimer.Tick += OnTelemetryTimerTick;
            _telemetryTimer.Start();
        }
    }

    private void OnDefaultDeviceChanged(object? sender, EventArgs e)
    {
        var defaultEndpoint = _endpointService.GetDefaultRenderEndpoint();
        if (defaultEndpoint != null)
        {
            CurrentEndpointName = defaultEndpoint.Name;
            ActiveEndpointText = defaultEndpoint.Name;
            DeviceChangeNotificationText = $"OUTPUT CHANGED: {defaultEndpoint.Name}";
            OnPropertyChanged(nameof(DeviceChangeNotificationVisibility));

            // Auto-clear notification after 3 seconds
            var dispatcher = DispatcherQueue.GetForCurrentThread();
            if (dispatcher != null)
            {
                var timer = dispatcher.CreateTimer();
                timer.Interval = TimeSpan.FromSeconds(3);
                timer.Tick += (_, _) =>
                {
                    timer.Stop();
                    DeviceChangeNotificationText = "";
                    OnPropertyChanged(nameof(DeviceChangeNotificationVisibility));
                };
                timer.Start();
            }
        }

        // Reconnect IPC on device transition
        _ipc.Connect();
        PushParametersToIpc();
    }

    partial void OnIntensityChanged(double value)
    {
        if (!_isAdjustingProgrammatically)
        {
            // Requirement #10: If user manually changes intensity and value differs from selected preset,
            // automatically switch to Custom (Preset 4)
            if (SelectedPreset.Id != 4 && Math.Abs(value - SelectedPreset.DefaultIntensity) > 0.05)
            {
                _isAdjustingProgrammatically = true;
                SelectedPreset = PresetItem.BuiltInPresets[4]; // Custom
                _isAdjustingProgrammatically = false;
            }
        }

        PushParametersToIpc();
        SaveCurrentSettings();
    }

    partial void OnIsEnabledChanged(bool value)
    {
        if (!value)
        {
            // Requirement #8: When DSP is turned OFF, listening automatically reflects Original (Bypass)
            // preventing contradictory states where DSP is OFF while UI claims to listen to Epicenter
            _isAdjustingProgrammatically = true;
            IsAbEpicenter = false;
            _isAdjustingProgrammatically = false;
        }

        PushParametersToIpc();
        UpdateStatusBadge();
        OnPropertyChanged(nameof(PowerOffBackground));
        OnPropertyChanged(nameof(PowerOffForeground));
        OnPropertyChanged(nameof(PowerOnBackground));
        OnPropertyChanged(nameof(PowerOnForeground));
        OnPropertyChanged(nameof(AbOriginalBackground));
        OnPropertyChanged(nameof(AbOriginalForeground));
        OnPropertyChanged(nameof(AbEpicenterBackground));
        OnPropertyChanged(nameof(AbEpicenterForeground));
        SaveCurrentSettings();
    }

    partial void OnIsAbEpicenterChanged(bool value)
    {
        PushParametersToIpc();
        UpdateStatusBadge();
        OnPropertyChanged(nameof(AbOriginalBackground));
        OnPropertyChanged(nameof(AbOriginalForeground));
        OnPropertyChanged(nameof(AbEpicenterBackground));
        OnPropertyChanged(nameof(AbEpicenterForeground));
        SaveCurrentSettings();
    }

    partial void OnConnectionStatusChanged(ApoConnectionStatus value)
    {
        OnPropertyChanged(nameof(StatusBrush));
    }

    partial void OnIsDrawerOpenChanged(bool value)
    {
        OnPropertyChanged(nameof(DrawerVisibility));
        OnPropertyChanged(nameof(PresetsTabVisibility));
        OnPropertyChanged(nameof(AudioSettingsTabVisibility));
        OnPropertyChanged(nameof(DiagnosticsTabVisibility));
    }

    partial void OnDrawerTabChanged(int value)
    {
        OnPropertyChanged(nameof(PresetsTabVisibility));
        OnPropertyChanged(nameof(AudioSettingsTabVisibility));
        OnPropertyChanged(nameof(DiagnosticsTabVisibility));
        OnPropertyChanged(nameof(PresetsTabBackground));
        OnPropertyChanged(nameof(PresetsTabForeground));
        OnPropertyChanged(nameof(AudioSettingsTabBackground));
        OnPropertyChanged(nameof(AudioSettingsTabForeground));
        OnPropertyChanged(nameof(DiagnosticsTabBackground));
        OnPropertyChanged(nameof(DiagnosticsTabForeground));
    }

    partial void OnSelectedPresetChanged(PresetItem value)
    {
        if (!_isAdjustingProgrammatically)
        {
            if (value.Id != 4) // Not Custom
            {
                _isAdjustingProgrammatically = true;
                Intensity = value.DefaultIntensity;
                _isAdjustingProgrammatically = false;
            }
        }

        PushParametersToIpc();
        SaveCurrentSettings();
    }

    private void PushParametersToIpc()
    {
        uint abVal = (IsEnabled && IsAbEpicenter) ? 1u : 0u;
        _ipc.SetParameters(IsEnabled, (float)Intensity, abVal, (uint)SelectedPreset.Id);
    }

    private void SaveCurrentSettings()
    {
        _settingsService.SaveSettings(new UserSettings
        {
            Intensity = Intensity,
            PresetId = SelectedPreset.Id,
            IsEnabled = IsEnabled,
            IsAbEpicenter = IsAbEpicenter
        });
    }

    private void OnTelemetryTimerTick(DispatcherQueueTimer sender, object args)
    {
        _tickCount++;

        // Refresh audiodg PID every ~1 second (30 ticks)
        if (_tickCount % 30 == 0)
        {
            UpdateAudiodgPid();
        }

        if (_ipc.TryReadTelemetry(out var telem))
        {
            _latestTelemetry = telem;
            IpcStatusText = "CONNECTED";

            // Heartbeat liveness check
            if (telem.ApoHeartbeat != _lastHeartbeat)
            {
                _lastHeartbeat = telem.ApoHeartbeat;
                _staleCount = 0;
            }
            else
            {
                _staleCount++;
            }

            // Update Audio Diagnostics & Settings
            if (telem.SampleRate > 0)
            {
                double khz = telem.SampleRate / 1000.0;
                SampleRateText = (khz == Math.Floor(khz)) ? $"{khz:F0} kHz" : $"{khz:F1} kHz";
                FormatSampleRate = $"{telem.SampleRate:N0} Hz";
                ChannelsText = telem.ChannelCount == 1 ? "Mono" : "Stereo";
                FormatChannels = ChannelsText;
                BufferFramesText = $"{telem.BufferFrameCount} FRAMES";
                FormatBufferFrames = $"{telem.BufferFrameCount} frames";

                double latMs = (double)telem.BufferFrameCount / telem.SampleRate * 1000.0;
                LatencyText = $"{latMs:F2} ms";
                FormatLatencyMs = $"{latMs:F2} ms";

                ApoBoundStatusText = telem.ApoStatus == 1 ? "BOUND" : (telem.ApoStatus == 2 ? "ERROR BYPASS" : "NOT BOUND");
                DspStatusText = IsEnabled ? "RUNNING" : "BYPASS";
                IsAudioEngineUnavailable = false;
                OnPropertyChanged(nameof(AudioEngineUnavailableVisibility));
            }
            else
            {
                SampleRateText = "N/A";
                BufferFramesText = "N/A";
                LatencyText = "N/A";
                FormatSampleRate = "N/A";
                FormatBufferFrames = "N/A";
                FormatLatencyMs = "N/A";
                DspStatusText = "IDLE";
                ApoBoundStatusText = "NOT BOUND";
            }

            double cpu = telem.DspCpuPercent;
            DspCpuText = $"{cpu:F1}% CPU";

            // Update Meters
            OutputPeakDbfs = telem.PeakDbfs;
            OutputRmsDbfs = telem.RmsDbfs;

            if (!IsEnabled || !IsAbEpicenter)
            {
                InputPeakDbfs = telem.PeakDbfs;
                InputRmsDbfs = telem.RmsDbfs;
            }
            else
            {
                InputPeakDbfs = Math.Max(-96.0, telem.PeakDbfs - (Intensity * 0.04));
                InputRmsDbfs = Math.Max(-96.0, telem.RmsDbfs - (Intensity * 0.03));
            }

            if (_staleCount > 30) // ~1 second without heartbeat
            {
                ConnectionStatus = ApoConnectionStatus.Connecting;
                StatusBadgeText = "◌ RECONNECTING...";
                ApoBoundStatusText = "NOT BOUND";
                DspStatusText = "IDLE";
                IsAudioEngineUnavailable = true;
                AudioEngineUnavailableMessage = "AUDIO ENGINE UNAVAILABLE · Epicenter DSP is reconnecting...";
                OnPropertyChanged(nameof(AudioEngineUnavailableVisibility));
                _ipc.Connect();
            }
            else
            {
                UpdateStatusBadge();
            }
        }
        else
        {
            _staleCount++;
            IpcStatusText = "DISCONNECTED";
            if (_staleCount > 30)
            {
                ConnectionStatus = ApoConnectionStatus.NotBound;
                StatusBadgeText = "⚠ NOT BOUND";
                ApoBoundStatusText = "NOT BOUND";
                DspStatusText = "IDLE";
                FormatSampleRate = "N/A";
                FormatBufferFrames = "N/A";
                FormatLatencyMs = "N/A";
                IsAudioEngineUnavailable = true;
                AudioEngineUnavailableMessage = "AUDIO ENGINE UNAVAILABLE · Endpoint not bound to APO";
                OnPropertyChanged(nameof(AudioEngineUnavailableVisibility));
                _ipc.Connect();
            }
        }
    }

    private void UpdateAudiodgPid()
    {
        int? pid = _diagnosticsService.GetAudiodgProcessId();
        AudiodgPidText = pid.HasValue ? $"PID: {pid.Value}" : "PID: Not Running";
    }

    private void UpdateStatusBadge()
    {
        if (!_ipc.IsConnected)
        {
            ConnectionStatus = ApoConnectionStatus.Connecting;
            StatusBadgeText = "◌ CONNECTING...";
            IsAudioEngineUnavailable = true;
            AudioEngineUnavailableMessage = "AUDIO ENGINE UNAVAILABLE · Epicenter DSP is reconnecting...";
        }
        else if (!IsEnabled)
        {
            ConnectionStatus = ApoConnectionStatus.Bypass;
            StatusBadgeText = "○ BYPASS";
            IsAudioEngineUnavailable = false;
        }
        else if (!IsAbEpicenter)
        {
            ConnectionStatus = ApoConnectionStatus.Bypass;
            StatusBadgeText = "A ORIGINAL";
            IsAudioEngineUnavailable = false;
        }
        else
        {
            ConnectionStatus = ApoConnectionStatus.Active;
            StatusBadgeText = "● ACTIVE";
            IsAudioEngineUnavailable = false;
        }

        OnPropertyChanged(nameof(ListenStatusText));
        OnPropertyChanged(nameof(DspPowerStatusText));
        OnPropertyChanged(nameof(AudioEngineUnavailableVisibility));
    }

    [RelayCommand]
    public void TogglePower()
    {
        IsEnabled = !IsEnabled;
    }

    [RelayCommand]
    public void SelectOriginalA()
    {
        IsAbEpicenter = false;
    }

    [RelayCommand]
    public void SelectEpicenterB()
    {
        // When user explicitly selects B, turn on DSP if currently disabled
        if (!IsEnabled)
        {
            IsEnabled = true;
        }
        IsAbEpicenter = true;
    }

    public void AdjustIntensity(double delta)
    {
        Intensity = Math.Clamp(Intensity + delta, 0.0, 100.0);
    }

    // Drawer commands
    [RelayCommand]
    public void OpenDrawer(int tab)
    {
        DrawerTab = tab;
        IsDrawerOpen = true;
    }

    [RelayCommand]
    public void CloseDrawer()
    {
        IsDrawerOpen = false;
    }

    [RelayCommand]
    public void ToggleDrawer(int tab)
    {
        if (IsDrawerOpen && DrawerTab == tab)
        {
            IsDrawerOpen = false;
        }
        else
        {
            DrawerTab = tab;
            IsDrawerOpen = true;
        }
    }

    [RelayCommand]
    public void ApplyPreset(PresetItem preset)
    {
        _isAdjustingProgrammatically = true;
        SelectedPreset = preset;
        if (preset.Id != 4) // Not Custom
        {
            Intensity = preset.DefaultIntensity;
        }
        _isAdjustingProgrammatically = false;

        PushParametersToIpc();
        SaveCurrentSettings();
    }

    [RelayCommand]
    public void RetryConnection()
    {
        ConnectionStatus = ApoConnectionStatus.Connecting;
        StatusBadgeText = "◌ CONNECTING...";
        _ipc.Connect();
        PushParametersToIpc();
        var def = _endpointService.GetDefaultRenderEndpoint();
        if (def != null)
        {
            CurrentEndpointName = def.Name;
            ActiveEndpointText = def.Name;
        }
        UpdateAudiodgPid();
    }

    [RelayCommand]
    public void ExportDiagnosticLog()
    {
        try
        {
            var defaultEndpoint = _endpointService.GetDefaultRenderEndpoint();
            string report = _diagnosticsService.GenerateDiagnosticReport(
                ConnectionStatus,
                _ipc.IsConnected,
                _latestTelemetry,
                defaultEndpoint,
                Intensity,
                IsEnabled,
                IsAbEpicenter,
                SelectedPreset.Name
            );

            string path = _diagnosticsService.ExportDiagnosticLogToFile(report);
            ExportNotificationText = $"Log exportado exitosamente:\n{path}";
        }
        catch (Exception ex)
        {
            ExportNotificationText = $"Error al exportar log: {ex.Message}";
        }
    }

    public void Dispose()
    {
        _telemetryTimer?.Stop();
        _endpointService.DefaultDeviceChanged -= OnDefaultDeviceChanged;
        _endpointService.Dispose();
        _ipc.Dispose();
    }
}

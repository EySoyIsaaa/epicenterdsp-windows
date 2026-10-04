using System;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

public interface IDiagnosticsService
{
    int? GetAudiodgProcessId();
    string GenerateDiagnosticReport(
        ApoConnectionStatus apoStatus,
        bool isIpcConnected,
        ApoTelemetryData telemetry,
        AudioEndpointInfo? endpoint,
        double currentIntensity,
        bool isEnabled,
        bool isAbEpicenter,
        string presetName);
    string ExportDiagnosticLogToFile(string reportContent, string? destinationPath = null);
}

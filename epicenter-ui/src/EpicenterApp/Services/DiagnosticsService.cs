using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

public class DiagnosticsService : IDiagnosticsService
{
    public int? GetAudiodgProcessId()
    {
        try
        {
            var processes = Process.GetProcessesByName("audiodg");
            if (processes.Length > 0)
            {
                return processes[0].Id;
            }
        }
        catch
        {
            // Access denied or process query error in restricted environment
        }
        return null;
    }

    public string GenerateDiagnosticReport(
        ApoConnectionStatus apoStatus,
        bool isIpcConnected,
        ApoTelemetryData telemetry,
        AudioEndpointInfo? endpoint,
        double currentIntensity,
        bool isEnabled,
        bool isAbEpicenter,
        string presetName)
    {
        var sb = new StringBuilder();
        sb.AppendLine("================================================================================");
        sb.AppendLine("EPICENTER DSP PROFESSIONAL 1.0 — SYSTEM DIAGNOSTIC REPORT");
        sb.AppendLine("================================================================================");
        sb.AppendLine($"Timestamp (UTC):     {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss} UTC");
        sb.AppendLine($"Timestamp (Local):   {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        sb.AppendLine($"OS Version:          {Environment.OSVersion.VersionString} ({(Environment.Is64BitOperatingSystem ? "64-bit" : "32-bit")})");
        sb.AppendLine($".NET Runtime:        {Environment.Version}");
        sb.AppendLine($"UI Host Process PID: {Environment.ProcessId}");
        sb.AppendLine();

        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine("1. APO INTEGRATION & IPC STATUS");
        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine($"APO Connection:      {apoStatus.ToString().ToUpperInvariant()}");
        sb.AppendLine($"Shared Memory IPC:   {(isIpcConnected ? "CONNECTED" : "DISCONNECTED")}");
        sb.AppendLine($"Shared Memory Map:   Global\\EpicenterDspIpcSharedMemory");
        int? audiodgPid = GetAudiodgProcessId();
        sb.AppendLine($"Audio Engine Host:   audiodg.exe");
        sb.AppendLine($"audiodg PID:         {(audiodgPid.HasValue ? audiodgPid.Value.ToString() : "Not Running / Unavailable")}");
        sb.AppendLine($"Heartbeat Counter:   {telemetry.ApoHeartbeat}");
        sb.AppendLine();

        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine("2. AUDIO ENDPOINT & PIPELINE");
        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine($"Endpoint Name:       {endpoint?.Name ?? "N/A"}");
        sb.AppendLine($"Endpoint ID:         {endpoint?.Id ?? "N/A"}");
        sb.AppendLine($"Endpoint Status:     {endpoint?.Status ?? "N/A"}");
        sb.AppendLine($"Endpoint EFX:        {(endpoint?.IsApoBound == true ? "ASSOCIATED" : "NOT ASSOCIATED")}");
        sb.AppendLine($"APO Reported State:  {(telemetry.ApoStatus == 1 ? "PREPARED (not proof of current audio flow)" : (telemetry.ApoStatus == 2 ? "ERROR BYPASS" : "NOT BOUND"))}");
        sb.AppendLine();

        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine("3. AUDIO FORMAT & REALTIME TELEMETRY");
        sb.AppendLine("--------------------------------------------------------------------------------");
        if (telemetry.SampleRate > 0)
        {
            double latencyMs = telemetry.BufferFrameCount > 0
                ? (double)telemetry.BufferFrameCount / telemetry.SampleRate * 1000.0
                : 0.0;

            sb.AppendLine($"Sample Rate:         {telemetry.SampleRate:N0} Hz ({telemetry.SampleRate / 1000.0:F1} kHz)");
            sb.AppendLine($"Bit Depth:           32-bit Floating Point (IEEE 754)");
            sb.AppendLine($"Channel Count:       {telemetry.ChannelCount} ({(telemetry.ChannelCount == 1 ? "Mono" : "Stereo")})");
            sb.AppendLine($"Buffer Frame Count:  {telemetry.BufferFrameCount} frames");
            sb.AppendLine($"Block Duration:      {latencyMs:F2} ms (not end-to-end latency)");
            sb.AppendLine($"DSP Time per Block:  {telemetry.DspTimeMicroseconds:F1} µs");
            sb.AppendLine($"DSP CPU Load:        {telemetry.DspCpuPercent:F2}%");
            sb.AppendLine($"Peak Level (Output): {telemetry.PeakDbfs:F1} dBFS");
            sb.AppendLine($"RMS Level (Output):  {telemetry.RmsDbfs:F1} dBFS");
            sb.AppendLine($"Buffer Underruns:    {telemetry.Underruns}");
        }
        else
        {
            sb.AppendLine("Sample Rate:         N/A (Stream Inactive / Not Bound)");
            sb.AppendLine("Bit Depth:           N/A");
            sb.AppendLine("Channel Count:       N/A");
            sb.AppendLine("Buffer Frame Count:  N/A");
            sb.AppendLine("Latency:             N/A");
            sb.AppendLine("DSP CPU Load:        N/A");
        }
        sb.AppendLine();

        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine("4. CURRENT DSP CONFIGURATION");
        sb.AppendLine("--------------------------------------------------------------------------------");
        sb.AppendLine($"Master Power:        {(isEnabled ? "ON (requested)" : "OFF (Bypass requested)")}");
        sb.AppendLine($"A/B Switch:          {(isAbEpicenter ? "B (Epicenter DSP)" : "A (Original Audio)")}");
        sb.AppendLine($"Intensity:           {currentIntensity:F1}%");
        sb.AppendLine($"Active Preset:       {presetName}");
        sb.AppendLine("================================================================================");
        sb.AppendLine("END OF REPORT — NO PRIVATE OR SENSITIVE DATA RECORDED");
        sb.AppendLine("================================================================================");

        return sb.ToString();
    }

    public string ExportDiagnosticLogToFile(string reportContent, string? destinationPath = null)
    {
        string filePath;
        if (!string.IsNullOrWhiteSpace(destinationPath))
        {
            filePath = destinationPath;
        }
        else
        {
            string docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            if (string.IsNullOrWhiteSpace(docs) || !Directory.Exists(docs))
            {
                string appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                docs = Path.Combine(appData, "EpicenterDSP", "Logs");
            }

            if (!Directory.Exists(docs))
            {
                Directory.CreateDirectory(docs);
            }

            string timestamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
            filePath = Path.Combine(docs, $"EpicenterDSP_Diagnostics_{timestamp}.log");
        }

        string? dir = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
        {
            Directory.CreateDirectory(dir);
        }

        File.WriteAllText(filePath, reportContent, Encoding.UTF8);
        return filePath;
    }
}

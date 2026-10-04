using EpicenterApp.Models;

namespace EpicenterApp.Services;

public interface IIpcBridgeService : IDisposable
{
    bool IsConnected { get; }
    void Connect();
    void Disconnect();
    void SetParameters(bool enabled, float intensity, uint abState, uint targetPreset = 0);
    bool TryReadTelemetry(out ApoTelemetryData telemetry);
}

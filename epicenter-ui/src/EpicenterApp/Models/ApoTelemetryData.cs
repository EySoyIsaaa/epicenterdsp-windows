namespace EpicenterApp.Models;

public enum ApoConnectionStatus
{
    Connecting,
    Active,
    Bypass,
    NotBound,
    Error
}

public struct ApoTelemetryData
{
    public uint SampleRate;
    public uint ChannelCount;
    public uint BufferFrameCount;
    public float DspTimeMicroseconds;
    public float PeakDbfs;
    public float RmsDbfs;
    public uint Underruns;
    public uint ApoStatus;
    public uint ApoHeartbeat;

    public readonly double DspCpuPercent =>
        (SampleRate > 0 && BufferFrameCount > 0)
            ? (DspTimeMicroseconds / ((double)BufferFrameCount / SampleRate * 1_000_000.0)) * 100.0
            : 0.0;
}

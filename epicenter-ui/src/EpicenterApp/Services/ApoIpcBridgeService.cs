using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct EpicenterControlBlock
{
    public uint Sequence;
    public uint Enabled;
    public float Intensity;
    public uint AbState;
    public uint TargetPreset;
    public fixed uint Reserved[8];
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct EpicenterTelemetryBlock
{
    public uint ApoHeartbeat;
    public uint SampleRate;
    public uint ChannelCount;
    public uint BufferFrameCount;
    public float DspTimeMicroseconds;
    public float PeakDbfs;
    public float RmsDbfs;
    public uint Underruns;
    public uint ApoStatus;
    public fixed uint Reserved[7];
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct EpicenterIpcSharedState
{
    public uint Magic;
    public uint ProtocolVersion;
    public uint StructSize;
    public uint InitFlags;
    public ulong UiLastHeartbeatQpc;
    public volatile uint UiProcessId;
    public uint HeaderReserved;
    public EpicenterControlBlock Control;
    public EpicenterTelemetryBlock Telemetry;
}

public sealed unsafe class ApoIpcBridgeService : IIpcBridgeService
{
    private const string GlobalName = @"Global\EpicenterDspIpcSharedMemory";
    private const string LocalName = @"Local\EpicenterDspIpcSharedMemory";
    private const uint MagicExpected = 0x45504943u; // 'EPIC'

    private MemoryMappedFile? _mmf;
    private MemoryMappedViewAccessor? _accessor;
    private byte* _pointer;
    private readonly object _lock = new();
    private bool _disposed;
    private readonly string _globalName;
    private readonly string _localName;
    private bool _hasPendingParameters;
    private bool _pendingEnabled;
    private float _pendingIntensity;
    private uint _pendingAbState;
    private uint _pendingPreset;
    public string? ConnectionError { get; private set; }

    public ApoIpcBridgeService(string? mappingName = null)
    {
        _globalName = mappingName ?? GlobalName;
        _localName = mappingName ?? LocalName;
    }

    public bool IsConnected
    {
        get
        {
            lock (_lock)
            {
                if (_pointer == null) return false;
                var state = (EpicenterIpcSharedState*)_pointer;
                return state->Magic == MagicExpected;
            }
        }
    }

    public void Connect()
    {
        lock (_lock)
        {
            if (_disposed) return;
            if (_pointer != null) return;
            ConnectionError = null;

            try
            {
                // Try Global namespace first (standard Session 0 audiodg <-> user Session 1)
                _mmf = MemoryMappedFile.OpenExisting(_globalName, MemoryMappedFileRights.ReadWrite);
            }
            catch (FileNotFoundException)
            {
                try
                {
                    // Fallback to Local namespace
                    _mmf = MemoryMappedFile.OpenExisting(_localName, MemoryMappedFileRights.ReadWrite);
                }
                catch (Exception ex)
                {
                    _mmf = null;
                    ConnectionError = DescribeConnectionError(ex);
                }
            }
            catch (Exception ex)
            {
                _mmf = null;
                ConnectionError = DescribeConnectionError(ex);
            }

            if (_mmf == null) return;

            try
            {
                // Map the existing extent, not an assumed size. The frozen C++
                // v1 structure is 4088 bytes, although its page is 4096 bytes.
                _accessor = _mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.ReadWrite);
                if (_accessor.Capacity < sizeof(EpicenterIpcSharedState))
                    throw new InvalidDataException("Bloque IPC incompleto.");
                _accessor.SafeMemoryMappedViewHandle.AcquirePointer(ref _pointer);
                _pointer += _accessor.PointerOffset;

                var state = (EpicenterIpcSharedState*)_pointer;
                if (state->Magic != MagicExpected || state->ProtocolVersion != 1 || state->InitFlags != 1 ||
                    state->StructSize < sizeof(EpicenterIpcSharedState) || state->StructSize > _accessor.Capacity)
                {
                    ConnectionError = "El APO usa un contrato IPC incompatible o no inicializado.";
                    Disconnect();
                }
                else if (_hasPendingParameters)
                {
                    // The APO may load after the UI. Replay the latest desired
                    // control state on every newly opened mapping, not defaults.
                    SetParameters(_pendingEnabled, _pendingIntensity, _pendingAbState, _pendingPreset);
                }
            }
            catch (Exception ex)
            {
                ConnectionError = DescribeConnectionError(ex);
                Disconnect();
            }
        }
    }

    private static string DescribeConnectionError(Exception ex) => ex switch
    {
        FileNotFoundException => "Windows no ha cargado EpicenterAPO. Instala y vincula el APO a la salida activa.",
        UnauthorizedAccessException => "Windows denego acceso al IPC del APO. No se modificaron permisos.",
        _ => $"No se pudo abrir el IPC: {ex.Message}"
    };

    public void Disconnect()
    {
        lock (_lock)
        {
            if (_accessor != null)
            {
                if (_pointer != null)
                {
                    _accessor.SafeMemoryMappedViewHandle.ReleasePointer();
                    _pointer = null;
                }
                _accessor.Dispose();
                _accessor = null;
            }

            _mmf?.Dispose();
            _mmf = null;
        }
    }

    public void SetParameters(bool enabled, float intensity, uint abState, uint targetPreset = 0)
    {
        lock (_lock)
        {
            if (_disposed) return;
            _hasPendingParameters = true;
            _pendingEnabled = enabled;
            _pendingIntensity = float.IsFinite(intensity) ? Math.Clamp(intensity, 0f, 100f) : 0f;
            _pendingAbState = abState;
            _pendingPreset = targetPreset;
            if (_pointer == null)
            {
                Connect();
                if (_pointer == null) return;
            }

            var state = (EpicenterIpcSharedState*)_pointer;
            if (state->Magic != MagicExpected) return;

            // 1. Sanitize intensity
            float safeIntensity = intensity;
            if (float.IsNaN(safeIntensity) || float.IsInfinity(safeIntensity) || safeIntensity < 0.0f)
            {
                safeIntensity = 0.0f;
            }
            else if (safeIntensity > 100.0f)
            {
                safeIntensity = 100.0f;
            }

            // Single UI writer. Recover an odd counter left by a previously
            // interrupted write without changing the frozen SeqLock layout.
            uint writingSequence = unchecked((uint)Volatile.Read(ref *(int*)&state->Control.Sequence) + 1u) | 1u;
            Interlocked.Exchange(ref *(int*)&state->Control.Sequence, unchecked((int)writingSequence));
            Thread.MemoryBarrier();

            // 3. Write control fields
            state->Control.Enabled = enabled ? 1u : 0u;
            state->Control.Intensity = safeIntensity;
            state->Control.AbState = abState;
            state->Control.TargetPreset = targetPreset;

            Thread.MemoryBarrier();

            // 4. Advance SeqLock counter to even (stable)
            Interlocked.Exchange(ref *(int*)&state->Control.Sequence, unchecked((int)(writingSequence + 1u)));
        }
    }

    public bool TryReadTelemetry(out ApoTelemetryData telemetry)
    {
        telemetry = default;
        lock (_lock)
        {
            if (_pointer == null)
            {
                Connect();
                if (_pointer == null) return false;
            }

            var state = (EpicenterIpcSharedState*)_pointer;
            if (state->Magic != MagicExpected) return false;

            telemetry.SampleRate = state->Telemetry.SampleRate;
            telemetry.ChannelCount = state->Telemetry.ChannelCount;
            telemetry.BufferFrameCount = state->Telemetry.BufferFrameCount;
            telemetry.DspTimeMicroseconds = state->Telemetry.DspTimeMicroseconds;
            telemetry.PeakDbfs = state->Telemetry.PeakDbfs;
            telemetry.RmsDbfs = state->Telemetry.RmsDbfs;
            telemetry.Underruns = state->Telemetry.Underruns;
            telemetry.ApoStatus = state->Telemetry.ApoStatus;
            telemetry.ApoHeartbeat = state->Telemetry.ApoHeartbeat;

            return true;
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        // Do not leave an effect enabled with no control window. No IPC layout change.
        if (IsConnected) SetParameters(false, 0, 0);
        _disposed = true;
        Disconnect();
    }
}

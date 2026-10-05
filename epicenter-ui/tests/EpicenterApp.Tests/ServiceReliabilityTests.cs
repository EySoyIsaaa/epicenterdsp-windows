using System.IO.MemoryMappedFiles;
using EpicenterApp.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicenterApp.Tests;

[TestClass]
public class ServiceReliabilityTests
{
    [TestMethod]
    public void MissingApoDoesNotCreateMappingOrClaimConnection()
    {
        using var bridge = new ApoIpcBridgeService("Local\\EpicenterMissing_" + Guid.NewGuid().ToString("N"));
        bridge.Connect();
        Assert.IsFalse(bridge.IsConnected);
        Assert.IsNotNull(bridge.ConnectionError);
        Assert.IsFalse(bridge.TryReadTelemetry(out _));
    }

    [TestMethod]
    public unsafe void FrozenNative4088ByteMappingConnectsAndCloseSendsBypass()
    {
        var name = "Local\\Epicenter4088_" + Guid.NewGuid().ToString("N");
        using var mapping = MemoryMappedFile.CreateNew(name, 4088);
        using var view = mapping.CreateViewAccessor();
        byte* ptr = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);
        try
        {
            var state = (EpicenterIpcSharedState*)ptr;
            state->Magic = 0x45504943;
            state->ProtocolVersion = 1;
            state->StructSize = 4088;
            state->InitFlags = 1;
            state->Telemetry.SampleRate = 48000;
            state->Telemetry.ApoStatus = 1;
            state->Telemetry.ApoHeartbeat = 10;
            using var bridge = new ApoIpcBridgeService(name);
            bridge.Connect();
            Assert.IsTrue(bridge.IsConnected, bridge.ConnectionError);
            bridge.SetParameters(true, 82, 1);
            Assert.AreEqual(82f, state->Control.Intensity);
            Assert.AreEqual(1u, state->Control.Enabled);
            Assert.AreEqual(0u, state->Control.Sequence & 1);
            Assert.IsTrue(bridge.TryReadTelemetry(out var telemetry));
            Assert.AreEqual(48000u, telemetry.SampleRate);
            bridge.Disconnect();
            bridge.Connect();
            Assert.IsTrue(bridge.IsConnected);
            bridge.Dispose();
            Assert.AreEqual(0u, state->Control.Enabled);
            Assert.AreEqual(0u, state->Control.AbState);
            bridge.Connect();
            Assert.IsFalse(bridge.IsConnected);
        }
        finally { view.SafeMemoryMappedViewHandle.ReleasePointer(); }
    }

    [TestMethod]
    public unsafe void WrongIpcProtocolIsRejected()
    {
        var name = "Local\\EpicenterBadProtocol_" + Guid.NewGuid().ToString("N");
        using var mapping = MemoryMappedFile.CreateNew(name, 4096);
        using var view = mapping.CreateViewAccessor();
        byte* ptr = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);
        try
        {
            var state = (EpicenterIpcSharedState*)ptr;
            state->Magic = 0x45504943;
            state->ProtocolVersion = 2;
            state->StructSize = 4088;
            state->InitFlags = 1;
            using var bridge = new ApoIpcBridgeService(name);
            bridge.Connect();
            Assert.IsFalse(bridge.IsConnected);
            StringAssert.Contains(bridge.ConnectionError!, "incompatible");
        }
        finally { view.SafeMemoryMappedViewHandle.ReleasePointer(); }
    }

    [TestMethod]
    public unsafe void ApoLoadingAfterUi_ReplaysLatestControls_AndRecoversInterruptedWrite()
    {
        var name = "Local\\EpicenterLateLoad_" + Guid.NewGuid().ToString("N");
        using var bridge = new ApoIpcBridgeService(name);
        bridge.SetParameters(true, 63, 1, 4);
        Assert.IsFalse(bridge.IsConnected);
        using var mapping = MemoryMappedFile.CreateNew(name, 4088);
        using var view = mapping.CreateViewAccessor();
        byte* ptr = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);
        try
        {
            var state = (EpicenterIpcSharedState*)ptr;
            state->Magic = 0x45504943;
            state->ProtocolVersion = 1;
            state->StructSize = 4088;
            state->InitFlags = 1;
            state->Control.Sequence = 7; // Simulated previously interrupted UI write.
            Assert.IsTrue(bridge.TryReadTelemetry(out _)); // Auto-connect path.
            Assert.IsTrue(bridge.IsConnected);
            Assert.AreEqual(1u, state->Control.Enabled);
            Assert.AreEqual(63f, state->Control.Intensity);
            Assert.AreEqual(1u, state->Control.AbState);
            Assert.AreEqual(4u, state->Control.TargetPreset);
            Assert.AreEqual(0u, state->Control.Sequence & 1u);
            bridge.SetParameters(true, 78, 1);
            Assert.AreEqual(78f, state->Control.Intensity);
            Assert.AreEqual(0u, state->Control.Sequence & 1u);
        }
        finally { view.SafeMemoryMappedViewHandle.ReleasePointer(); }
    }
}

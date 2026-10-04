using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

public class WindowsAudioEndpointService : IAudioEndpointService, WindowsAudioEndpointService.IMMNotificationClient
{
    private const int EDataFlow_eRender = 0;
    private const int ERole_eConsole = 0;
    private const int DEVICE_STATE_ACTIVE = 0x00000001;
    private const int STGM_READ = 0;
    private const ushort VT_LPWSTR = 31;

    private static readonly PropertyKey PKEY_Device_FriendlyName = new()
    {
        fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"),
        pid = 14
    };

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PropVariant pvar);

    private readonly object _lock = new();
    private IMMDeviceEnumerator? _enumerator;
    private bool _isNotificationRegistered;
    private AudioEndpointInfo? _cachedDefaultEndpoint;
    private readonly List<AudioEndpointInfo> _cachedEndpoints = new();

    public event EventHandler? DefaultDeviceChanged;

    public WindowsAudioEndpointService()
    {
        try
        {
            var comObj = new MMDeviceEnumeratorComObject();
            _enumerator = (IMMDeviceEnumerator)comObj;
            int hr = _enumerator.RegisterEndpointNotificationCallback(this);
            _isNotificationRegistered = (hr == 0);
        }
        catch
        {
            _enumerator = null;
        }

        Refresh();
    }

    public AudioEndpointInfo? GetDefaultRenderEndpoint()
    {
        lock (_lock)
        {
            if (_cachedDefaultEndpoint == null)
            {
                Refresh();
            }
            return _cachedDefaultEndpoint;
        }
    }

    public IReadOnlyList<AudioEndpointInfo> GetRenderEndpoints()
    {
        lock (_lock)
        {
            if (_cachedEndpoints.Count == 0)
            {
                Refresh();
            }
            return _cachedEndpoints.ToArray();
        }
    }

    public void Refresh()
    {
        lock (_lock)
        {
            _cachedEndpoints.Clear();
            _cachedDefaultEndpoint = null;

            if (_enumerator == null)
            {
                _cachedDefaultEndpoint = new AudioEndpointInfo(
                    Id: "default",
                    Name: "Default Render Device",
                    IsDefault: true,
                    Status: "Active",
                    IsApoBound: true
                );
                _cachedEndpoints.Add(_cachedDefaultEndpoint);
                return;
            }

            string? defaultId = null;
            try
            {
                if (_enumerator.GetDefaultAudioEndpoint(EDataFlow_eRender, ERole_eConsole, out var defDevice) == 0 && defDevice != null)
                {
                    defDevice.GetId(out defaultId);
                    string name = GetDeviceFriendlyName(defDevice) ?? "Default Render Device";
                    _cachedDefaultEndpoint = new AudioEndpointInfo(
                        Id: defaultId ?? "default",
                        Name: name,
                        IsDefault: true,
                        Status: "Active",
                        IsApoBound: true
                    );
                    Marshal.ReleaseComObject(defDevice);
                }
            }
            catch
            {
                // Ignored
            }

            try
            {
                if (_enumerator.EnumAudioEndpoints(EDataFlow_eRender, DEVICE_STATE_ACTIVE, out var collection) == 0 && collection != null)
                {
                    collection.GetCount(out uint count);
                    for (uint i = 0; i < count; i++)
                    {
                        if (collection.Item(i, out var dev) == 0 && dev != null)
                        {
                            dev.GetId(out var id);
                            string name = GetDeviceFriendlyName(dev) ?? $"Audio Endpoint {i + 1}";
                            bool isDefault = (id != null && id == defaultId);

                            var info = new AudioEndpointInfo(
                                Id: id ?? $"ep_{i}",
                                Name: name,
                                IsDefault: isDefault,
                                Status: "Active",
                                IsApoBound: isDefault
                            );

                            _cachedEndpoints.Add(info);
                            if (isDefault && _cachedDefaultEndpoint == null)
                            {
                                _cachedDefaultEndpoint = info;
                            }
                            Marshal.ReleaseComObject(dev);
                        }
                    }
                    Marshal.ReleaseComObject(collection);
                }
            }
            catch
            {
                // Ignored
            }

            if (_cachedDefaultEndpoint == null && _cachedEndpoints.Count > 0)
            {
                _cachedDefaultEndpoint = _cachedEndpoints[0];
            }
            else if (_cachedDefaultEndpoint == null)
            {
                _cachedDefaultEndpoint = new AudioEndpointInfo(
                    Id: "default",
                    Name: "Default Render Device",
                    IsDefault: true,
                    Status: "Active",
                    IsApoBound: true
                );
                _cachedEndpoints.Add(_cachedDefaultEndpoint);
            }
        }
    }

    private static string? GetDeviceFriendlyName(IMMDevice device)
    {
        try
        {
            if (device.OpenPropertyStore(STGM_READ, out var store) == 0 && store != null)
            {
                var key = PKEY_Device_FriendlyName;
                if (store.GetValue(ref key, out var pv) == 0)
                {
                    string? name = null;
                    if (pv.vt == VT_LPWSTR && pv.pwszVal != IntPtr.Zero)
                    {
                        name = Marshal.PtrToStringUni(pv.pwszVal);
                    }
                    PropVariantClear(ref pv);
                    Marshal.ReleaseComObject(store);
                    return name;
                }
                Marshal.ReleaseComObject(store);
            }
        }
        catch
        {
            // Ignored
        }
        return null;
    }

    // IMMNotificationClient implementation
    void IMMNotificationClient.OnDeviceStateChanged(string pwstrDeviceId, uint dwNewState)
    {
        Refresh();
        DefaultDeviceChanged?.Invoke(this, EventArgs.Empty);
    }

    void IMMNotificationClient.OnDeviceAdded(string pwstrDeviceId)
    {
        Refresh();
        DefaultDeviceChanged?.Invoke(this, EventArgs.Empty);
    }

    void IMMNotificationClient.OnDeviceRemoved(string pwstrDeviceId)
    {
        Refresh();
        DefaultDeviceChanged?.Invoke(this, EventArgs.Empty);
    }

    void IMMNotificationClient.OnDefaultDeviceChanged(int flow, int role, string pwstrDefaultDeviceId)
    {
        if (flow == EDataFlow_eRender)
        {
            Refresh();
            DefaultDeviceChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    void IMMNotificationClient.OnPropertyValueChanged(string pwstrDeviceId, PropertyKey key)
    {
    }

    public void Dispose()
    {
        lock (_lock)
        {
            if (_enumerator != null)
            {
                if (_isNotificationRegistered)
                {
                    try { _enumerator.UnregisterEndpointNotificationCallback(this); } catch { }
                    _isNotificationRegistered = false;
                }
                Marshal.ReleaseComObject(_enumerator);
                _enumerator = null;
            }
        }
    }

    #region COM Interfaces & Structs

    [ComImport]
    [Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private class MMDeviceEnumeratorComObject { }

    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string pwstrId, out IMMDevice device);
        [PreserveSig] int RegisterEndpointNotificationCallback(IMMNotificationClient client);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IMMNotificationClient client);
    }

    [Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceCollection
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int Item(uint index, out IMMDevice device);
    }

    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int dwClsCtx, IntPtr pActivationParams, [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
        [PreserveSig] int OpenPropertyStore(int stgmAccess, out IPropertyStore properties);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string strId);
        [PreserveSig] int GetState(out uint state);
    }

    [Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant pv);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant pv);
        [PreserveSig] int Commit();
    }

    [Guid("7991EEC8-36A8-429D-9F65-DDE80986132A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IMMNotificationClient
    {
        void OnDeviceStateChanged([MarshalAs(UnmanagedType.LPWStr)] string pwstrDeviceId, uint dwNewState);
        void OnDeviceAdded([MarshalAs(UnmanagedType.LPWStr)] string pwstrDeviceId);
        void OnDeviceRemoved([MarshalAs(UnmanagedType.LPWStr)] string pwstrDeviceId);
        void OnDefaultDeviceChanged(int flow, int role, [MarshalAs(UnmanagedType.LPWStr)] string pwstrDefaultDeviceId);
        void OnPropertyValueChanged([MarshalAs(UnmanagedType.LPWStr)] string pwstrDeviceId, PropertyKey key);
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct PropertyKey
    {
        public Guid fmtid;
        public uint pid;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct PropVariant
    {
        [FieldOffset(0)] public ushort vt;
        [FieldOffset(2)] public ushort wReserved1;
        [FieldOffset(4)] public ushort wReserved2;
        [FieldOffset(6)] public ushort wReserved3;
        [FieldOffset(8)] public IntPtr pwszVal;
    }

    #endregion
}

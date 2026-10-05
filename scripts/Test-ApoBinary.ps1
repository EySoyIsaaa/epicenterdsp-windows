param(
    [string]$DllPath = (Join-Path $PSScriptRoot '..\epicenter-apo\build\Release\EpicenterAPO.dll'),
    [guid]$ExpectedClsid = '{68C32E69-906A-4A0E-9214-41A832A88877}'
)

# Read-only, unregistered COM smoke test. This never calls DllRegisterServer,
# writes the registry, opens an audio device, or claims audiodg has loaded it.
$ErrorActionPreference = 'Stop'
$resolvedDll = (Resolve-Path -LiteralPath $DllPath).Path
if (-not ('EpicenterBinaryCheck.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace EpicenterBinaryCheck {
    public sealed class Result {
        public string DllPath;
        public bool HasEmbeddedManifest;
        public bool FactoryCreated;
        public string RegisteredInterface;
        public uint MaxInstances;
        public uint Flags;
    }

    public static class Native {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern bool FreeLibrary(IntPtr module);
        delegate bool ResourceNameCallback(IntPtr module, IntPtr type, IntPtr name, IntPtr param);
        [DllImport("kernel32.dll", EntryPoint="EnumResourceNamesW", SetLastError=true)]
        static extern bool EnumResourceNames(IntPtr module, IntPtr type, ResourceNameCallback callback, IntPtr param);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        delegate int GetClassObject(ref Guid clsid, ref Guid iid, out IntPtr factory);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        delegate int CreateInstance(IntPtr factory, IntPtr outer, ref Guid iid, out IntPtr instance);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        delegate int GetProperties(IntPtr instance, out IntPtr properties);

        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        struct ApoProperties {
            public Guid Clsid;
            public uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string FriendlyName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string Copyright;
            public uint MajorVersion, MinorVersion;
            public uint MinInputs, MaxInputs, MinOutputs, MaxOutputs;
            public uint MaxInstances, NumInterfaces;
            public Guid FirstInterface;
        }

        static Delegate VtableMethod(IntPtr instance, int slot, Type type) {
            IntPtr vtable = Marshal.ReadIntPtr(instance);
            return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(vtable, slot * IntPtr.Size), type);
        }

        static void CheckHr(int hr, string operation) {
            if (hr < 0) throw new InvalidOperationException(operation + " failed: 0x" + hr.ToString("X8"));
        }

        public static Result Check(string path, Guid expectedClsid) {
            IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 0x00000100 | 0x00001000);
            if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot load APO DLL");
            IntPtr factory = IntPtr.Zero, apo = IntPtr.Zero, properties = IntPtr.Zero;
            try {
                var result = new Result();
                result.DllPath = path;
                ResourceNameCallback callback = delegate(IntPtr m, IntPtr t, IntPtr n, IntPtr p) {
                    result.HasEmbeddedManifest = true;
                    return true;
                };
                bool enumerated = EnumResourceNames(module, new IntPtr(24), callback, IntPtr.Zero);
                int resourceError = Marshal.GetLastWin32Error();
                if (!enumerated && resourceError != 1812 && resourceError != 1813 && resourceError != 1814)
                    throw new Win32Exception(resourceError, "Cannot inspect manifest resources");
                GC.KeepAlive(callback);

                IntPtr export = GetProcAddress(module, "DllGetClassObject");
                if (export == IntPtr.Zero) throw new InvalidOperationException("DllGetClassObject export missing");
                if (expectedClsid == new Guid("8F7A7248-F617-45AF-ACDA-C1C10F99E4D8") &&
                    (GetProcAddress(module, "DllRegisterServer") != IntPtr.Zero ||
                     GetProcAddress(module, "DllUnregisterServer") != IntPtr.Zero))
                    throw new InvalidOperationException("Componentized DLL must not export legacy global registration");
                var getFactory = (GetClassObject)Marshal.GetDelegateForFunctionPointer(export, typeof(GetClassObject));
                Guid clsid = expectedClsid;
                Guid factoryIid = new Guid("00000001-0000-0000-C000-000000000046");
                CheckHr(getFactory(ref clsid, ref factoryIid, out factory), "DllGetClassObject");
                if (factory == IntPtr.Zero) throw new InvalidOperationException("Null class factory");
                Guid apoIid = new Guid("FD7F2B29-24D0-4B5C-B177-592C39F9CA10");
                var create = (CreateInstance)VtableMethod(factory, 3, typeof(CreateInstance));
                CheckHr(create(factory, IntPtr.Zero, ref apoIid, out apo), "CreateInstance");
                if (apo == IntPtr.Zero) throw new InvalidOperationException("Null APO instance");
                var getProps = (GetProperties)VtableMethod(apo, 5, typeof(GetProperties));
                CheckHr(getProps(apo, out properties), "GetRegistrationProperties");
                if (properties == IntPtr.Zero) throw new InvalidOperationException("Null APO properties");
                var props = (ApoProperties)Marshal.PtrToStructure(properties, typeof(ApoProperties));
                if (props.Clsid != clsid || props.NumInterfaces != 1 || props.FirstInterface != apoIid ||
                    props.MaxInstances != 32 || props.Flags != 15)
                    throw new InvalidOperationException("Unexpected APO registration properties");
                if (result.HasEmbeddedManifest)
                    throw new InvalidOperationException("APO DLL contains an embedded manifest");
                result.FactoryCreated = true;
                result.RegisteredInterface = props.FirstInterface.ToString("B");
                result.MaxInstances = props.MaxInstances;
                result.Flags = props.Flags;
                return result;
            } finally {
                if (properties != IntPtr.Zero) Marshal.FreeCoTaskMem(properties);
                if (apo != IntPtr.Zero) Marshal.Release(apo);
                if (factory != IntPtr.Zero) Marshal.Release(factory);
                FreeLibrary(module);
            }
        }
    }
}
'@
}

$result = [EpicenterBinaryCheck.Native]::Check($resolvedDll, $ExpectedClsid)
$result | Format-List
Get-AuthenticodeSignature -LiteralPath $resolvedDll | Select-Object Status, StatusMessage | Format-List
Write-Host '[OK] DLL carga, crea su objeto COM y expone metadatos validos sin registrarla.'
Write-Host 'Esto NO confirma instalacion, vinculacion al endpoint ni audio en audiodg.'

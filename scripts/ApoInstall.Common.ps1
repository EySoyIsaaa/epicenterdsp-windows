# Windows PowerShell 5.1. Loading this helper performs no system changes.
Set-StrictMode -Version 2.0
$ApoClsid = '{68C32E69-906A-4A0E-9214-41A832A88877}'
$EfxProperty = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ApoDllPath = Join-Path $ProjectRoot 'epicenter-apo\build\Release\EpicenterAPO.dll'
$AppExePath = Join-Path $ProjectRoot 'dist\EpicenterDSP Windows Actualizado\EpicenterDSP Windows.exe'
if (!(Test-Path -LiteralPath $AppExePath)) {
    $AppExePath = Join-Path $ProjectRoot 'epicenter-ui\src\EpicenterApp\bin\x64\Release\net9.0-windows10.0.26100.0\win-x64\EpicenterDSP Windows.exe'
}
$RenderBase = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$AudioKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio'
$ComKey = "HKLM:\SOFTWARE\Classes\CLSID\$ApoClsid"
$ApoKey = "HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\$ApoClsid"
$BackupDirectory = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'EpicenterDSP-TestInstall'
$StateFile = Join-Path $BackupDirectory 'state.json'

function Assert-TestEnvironment {
    if (![Environment]::Is64BitProcess) { throw 'Se requiere Windows PowerShell x64.' }
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Ejecuta el BAT como administrador.' }
}
function Assert-X64Binary([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Cabecera MZ invalida.' }
        $stream.Position = 0x3C; $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length - 6)) { throw 'Cabecera PE invalida.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Se requiere una DLL PE x64.' }
    } finally { $reader.Dispose() }
}
function Get-RegistrySnapshot([string]$Path, [string]$Name) {
    $exists = $false; $kind = $null; $value = $null
    if (Test-Path -LiteralPath $Path) {
        $key = Get-Item -LiteralPath $Path
        try {
            $exists = $key.GetValueNames() -contains $Name
            if ($exists) {
                $kind = $key.GetValueKind($Name).ToString()
                $value = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            }
        } finally { $key.Close() }
    }
    [pscustomobject]@{ Exists = $exists; Kind = $kind; Value = $value }
}
function Restore-RegistrySnapshot([string]$Path, [string]$Name, $Snapshot) {
    # A denied write may have left the original value untouched. Verify first;
    # don't needlessly request write access during recovery.
    $before = Get-RegistrySnapshot $Path $Name
    if ($before.Exists -eq $Snapshot.Exists -and (!$Snapshot.Exists -or
        ($before.Kind -eq $Snapshot.Kind -and $before.Value -eq $Snapshot.Value))) { return }
    if ($Snapshot.Exists) {
        if (!(Test-Path -LiteralPath $Path)) { New-Item -Path $Path -Force | Out-Null }
        Set-ItemProperty -LiteralPath $Path -Name $Name -Value $Snapshot.Value -Type $Snapshot.Kind
    } elseif (Test-Path -LiteralPath $Path) {
        if ((Get-RegistrySnapshot $Path $Name).Exists) { Remove-ItemProperty -LiteralPath $Path -Name $Name }
    }
    $actual = Get-RegistrySnapshot $Path $Name
    if ($actual.Exists -ne $Snapshot.Exists -or ($Snapshot.Exists -and ($actual.Kind -ne $Snapshot.Kind -or $actual.Value -ne $Snapshot.Value))) {
        throw "Restauracion no verificada: $Path / $Name"
    }
}
function Get-TestEndpoints {
    foreach ($key in Get-ChildItem -LiteralPath $RenderBase) {
        $deviceState = Get-RegistrySnapshot $key.PSPath 'DeviceState'
        if (!$deviceState.Exists -or $deviceState.Value -ne 1) { continue }
        $propsPath = Join-Path $key.PSPath 'Properties'
        $nameValue = Get-RegistrySnapshot $propsPath '{a45c254e-df1c-4efd-8020-67d146a850e0},14'
        if (!$nameValue.Exists) { $nameValue = Get-RegistrySnapshot $propsPath '{a45c254e-df1c-4efd-8020-67d146a850e0},2' }
        $name = if ($nameValue.Exists) { [string]$nameValue.Value } else { $key.PSChildName }
        if ($name -match 'VB-Audio|VB-CABLE|CABLE\s+(Input|In\b)|Voicemeeter') { continue }
        [pscustomobject]@{ Id = $key.PSChildName; Name = $name; FxPath = "$RenderBase\$($key.PSChildName)\FxProperties" }
    }
}
function Test-RegistryWriteAccess([string]$Path) {
    # Check existing Windows permissions without writing, changing ACLs or taking ownership.
    if ($Path -notlike 'HKLM:\*') { throw 'La comprobacion solo admite HKLM.' }
    $subkey = $Path.Substring(6)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        while ($subkey) {
            $read = $base.OpenSubKey($subkey, $false)
            if ($read) {
                $read.Dispose()
                try {
                    $writable = $base.OpenSubKey($subkey, $true)
                    if (!$writable) { throw 'No se pudo abrir la clave.' }
                    $writable.Dispose()
                    return
                } catch { throw "Windows deniega escritura: HKLM:\$subkey. No se cambiaron permisos. $($_.Exception.Message)" }
            }
            $last = $subkey.LastIndexOf('\')
            if ($last -lt 0) { break }
            $subkey = $subkey.Substring(0, $last)
        }
        throw "No se encontro un padre verificable: $Path"
    } finally { $base.Dispose() }
}
function Write-InstallLog([string]$Message) {
    $line = [DateTime]::UtcNow.ToString('o') + ' ' + $Message
    Write-Host $Message
    [IO.File]::AppendAllText((Join-Path $BackupDirectory 'install.log'), $line + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
}
function Set-InstallStage($State, [string]$Stage) {
    $State.Stage = $Stage
    Save-InstallState $State
    Write-InstallLog "[PASO] $Stage"
}
function Initialize-BackupDirectory {
    if (!(Test-Path -LiteralPath $BackupDirectory)) { New-Item -ItemType Directory -Path $BackupDirectory | Out-Null }
    if ((Get-Item -LiteralPath $BackupDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'El respaldo no puede estar en un enlace.' }
    # An elevated restorer must not consume state writable by standard users.
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $identity = New-Object Security.Principal.SecurityIdentifier($sid)
        $rule = New-Object Security.AccessControl.FileSystemAccessRule($identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $BackupDirectory -AclObject $acl
}
function Save-InstallState($State) {
    $temporary = Join-Path $BackupDirectory 'state.pending.json'
    [IO.File]::WriteAllText($temporary, ($State | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $StateFile) { [IO.File]::Replace($temporary, $StateFile, (Join-Path $BackupDirectory 'state.previous.json')) }
    else { [IO.File]::Move($temporary, $StateFile) }
}
function Read-InstallState {
    foreach ($path in @($BackupDirectory, $StateFile)) {
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Respaldo enlazado: no se utilizara.' }
        $acl = Get-Acl -LiteralPath $path
        foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
            $writeMask = [Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership
            if ($rule.AccessControlType -eq 'Allow' -and ($rule.FileSystemRights -band $writeMask) -and $rule.IdentityReference.Value -notin @('S-1-5-18', 'S-1-5-32-544')) {
                throw 'Respaldo editable por usuarios no administradores: no se utilizara.'
            }
        }
    }
    $state = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
    Assert-InstallState $state
    return $state
}
function Assert-InstallState($State) {
    if ($State.Version -ne 1 -or $State.EndpointId -notmatch '^\{[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\}$') { throw 'Respaldo invalido.' }
    if (![IO.Path]::IsPathRooted($State.DllPath) -or [IO.Path]::GetFileName($State.DllPath) -ne 'EpicenterAPO.dll') { throw 'Ruta DLL invalida en respaldo.' }
    if ($State.OriginalEfx.Exists -isnot [bool] -or ($State.OriginalEfx.Exists -and $State.OriginalEfx.Kind -ne 'String')) { throw 'Respaldo EFX invalido.' }
    if ($State.Protection.Exists -isnot [bool] -or ($State.Protection.Exists -and $State.Protection.Kind -ne 'DWord')) { throw 'Respaldo de proteccion invalido.' }
}
function Invoke-ApoRegistration([string]$Path, [switch]$Unregister) {
    Assert-X64Binary $Path
    $arguments = if ($Unregister) { '/u /s "' + $Path + '"' } else { '/s "' + $Path + '"' }
    $process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "regsvr32 fallo: $($process.ExitCode)." }
}
function Assert-ApoRegistration([string]$Path) {
    $actual = Get-RegistrySnapshot "$ComKey\InprocServer32" ''
    if (!$actual.Exists -or $actual.Value -ne $Path -or !(Test-Path -LiteralPath $ApoKey)) { throw 'Registro COM/APO incompleto.' }
}
function Restart-TestAudio {
    $service = Get-Service -Name audiosrv
    $dependents = @($service.DependentServices | Where-Object { $_.Status -eq 'Running' } | Select-Object -ExpandProperty Name)
    try { Restart-Service -Name audiosrv -Force }
    finally { foreach ($name in $dependents) { if ((Get-Service -Name $name).Status -ne 'Running') { Start-Service -Name $name } } }
    (Get-Service -Name audiosrv).WaitForStatus('Running', [TimeSpan]::FromSeconds(15))
}
function Restore-TestInstall($State) {
    Assert-InstallState $State
    $fxPath = "$RenderBase\$($State.EndpointId)\FxProperties"
    $registration = Get-RegistrySnapshot "$ComKey\InprocServer32" ''
    if ($registration.Exists -and $registration.Value -ne $State.DllPath) { throw 'Registro COM cambiado por otro proceso. No se sobrescribira.' }
    $current = Get-RegistrySnapshot $fxPath $EfxProperty
    if ($current.Exists -and $current.Value -ne $ApoClsid -and (!$State.OriginalEfx.Exists -or $current.Value -ne $State.OriginalEfx.Value)) { throw 'EFX cambiado por otro proceso. No se sobrescribira.' }
    Restore-RegistrySnapshot $fxPath $EfxProperty $State.OriginalEfx
    Restore-RegistrySnapshot $AudioKey 'DisableProtectedAudioDG' $State.Protection
    Restart-TestAudio
    if ($State.RegistrationAttempted -and ((Test-Path -LiteralPath $ComKey) -or (Test-Path -LiteralPath $ApoKey))) {
        if (!(Test-Path -LiteralPath $State.DllPath)) { throw 'Falta DLL para desregistrar. Audio restaurado; conserva el respaldo.' }
        Invoke-ApoRegistration $State.DllPath -Unregister
        if ((Test-Path -LiteralPath $ComKey) -or (Test-Path -LiteralPath $ApoKey)) { throw 'Quedan entradas COM/APO. Conserva el respaldo.' }
    }
    $State.Status = 'Restored'
    Save-InstallState $State
    $archive = Join-Path $BackupDirectory 'state.restored.json'
    if (Test-Path -LiteralPath $archive) { [IO.File]::Replace($StateFile, $archive, (Join-Path $BackupDirectory 'state.restored.previous.json')) }
    else { [IO.File]::Move($StateFile, $archive) }
}

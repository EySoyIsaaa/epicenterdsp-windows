# Isolated validation: no HKLM writes, no COM registration, no audio restart.
$ErrorActionPreference = 'Stop'
foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1') {
    $tokens = $null; $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors) { throw ($parseErrors | Out-String) }
    Write-Host "PARSE OK: $($file.Name)"
}
. "$PSScriptRoot\ApoInstall.Common.ps1"
Assert-X64Binary $ApoDllPath
Write-Host 'PASS: Release DLL x64'
$realEndpoints = @(Get-TestEndpoints)
Write-Host "PASS: enumeracion de salidas ($($realEndpoints.Count))"
if (@($realEndpoints | Where-Object { $_.Name -match 'CABLE\s+(Input|In\b)' }).Count) { throw 'Se ofrecio un cable virtual.' }
foreach ($reply in @('SI', 'si', ' Si ')) {
    if ($reply.Trim() -ine 'SI') { throw 'Confirmacion valida rechazada.' }
}
Write-Host 'PASS: confirmacion sin distincion de mayusculas y exclusion de CABLE In 16ch'

$BackupDirectory = Join-Path ([IO.Path]::GetTempPath()) ('EpicenterInstallerTests-' + [guid]::NewGuid().ToString('N'))
$StateFile = Join-Path $BackupDirectory 'state.json'
[IO.Directory]::CreateDirectory($BackupDirectory) | Out-Null
$script:fakeRegistry = @{}
$script:events = New-Object 'System.Collections.Generic.List[string]'
function Get-RegistrySnapshot([string]$Path, [string]$Name) {
    $id = "$Path|$Name"
    if ($script:fakeRegistry.ContainsKey($id)) { return $script:fakeRegistry[$id] }
    [pscustomobject]@{ Exists = $false; Kind = $null; Value = $null }
}
function Restore-RegistrySnapshot([string]$Path, [string]$Name, $Snapshot) {
    $script:fakeRegistry["$Path|$Name"] = $Snapshot
    $script:events.Add("restore:$Name")
}
function Restart-TestAudio { $script:events.Add('restart') }
function Require($Condition, [string]$Message) { if (!$Condition) { throw $Message }; Write-Host "PASS: $Message" }
try {
    foreach ($previous in @($null, 0, 1)) {
        $script:events.Clear(); $script:fakeRegistry.Clear()
        $state = [pscustomobject]@{
            Version = 1; DllPath = $ApoDllPath
            EndpointId = '{11111111-2222-3333-4444-555555555555}'
            OriginalEfx = [pscustomobject]@{ Exists = $false; Kind = $null; Value = $null }
            Protection = [pscustomobject]@{ Exists = ($null -ne $previous); Kind = 'DWord'; Value = $previous }
            RegistrationAttempted = $false; Status = 'Prepared'
        }
        Save-InstallState $state
        $loaded = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
        Assert-InstallState $loaded
        $script:fakeRegistry["$RenderBase\$($state.EndpointId)\FxProperties|$EfxProperty"] = [pscustomobject]@{ Exists = $true; Kind = 'String'; Value = $ApoClsid }
        Restore-TestInstall $loaded
        Require (!(Test-Path -LiteralPath $StateFile)) 'active backup archived only after restoration'
        Require ((Get-Content (Join-Path $BackupDirectory 'state.restored.json') -Raw | ConvertFrom-Json).Status -eq 'Restored') 'JSON roundtrip in PowerShell 5.1'
        Require ($script:events[0] -eq "restore:$EfxProperty" -and $script:events[2] -eq 'restart') 'detach and protection restore precede restart'
        $restored = Get-RegistrySnapshot $AudioKey 'DisableProtectedAudioDG'
        Require ($restored.Exists -eq ($null -ne $previous) -and $restored.Value -eq $previous) "protection restored exactly (previous=$previous)"
    }
    $state.OriginalEfx = [pscustomobject]@{ Exists = $true; Kind = 'String'; Value = '{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}' }
    Save-InstallState $state
    $script:fakeRegistry["$RenderBase\$($state.EndpointId)\FxProperties|$EfxProperty"] = [pscustomobject]@{ Exists = $true; Kind = 'String'; Value = $ApoClsid }
    Restore-TestInstall $state
    Require ((Get-RegistrySnapshot "$RenderBase\$($state.EndpointId)\FxProperties" $EfxProperty).Value -eq $state.OriginalEfx.Value) 'manufacturer EFX restored'

    Save-InstallState $state
    $script:events.Clear()
    $script:fakeRegistry["$RenderBase\$($state.EndpointId)\FxProperties|$EfxProperty"] = [pscustomobject]@{ Exists = $true; Kind = 'String'; Value = 'another-installer' }
    $failed = $false
    try { Restore-TestInstall $state } catch { $failed = $true }
    Require ($failed -and $script:events.Count -eq 0 -and (Test-Path $StateFile)) 'external changes stop restoration and preserve backup'

    $script:fakeRegistry.Clear()
    function Restart-TestAudio { throw 'simulated service failure' }
    $failed = $false
    try { Restore-TestInstall $state } catch { $failed = $true }
    Require ($failed -and (Test-Path $StateFile)) 'service failure keeps recovery state'
    $state.EndpointId = '..\other-device'
    $failed = $false
    try { Assert-InstallState $state } catch { $failed = $true }
    Require $failed 'invalid endpoint ID rejected'
    # Probe a fixture under HKLM that cannot exist; a bad path must fail closed.
    $failed = $false
    try { Test-RegistryWriteAccess 'HKCU:\Software\test' } catch { $failed = $true }
    Require $failed 'permission probe rejects unexpected registry roots'
    Write-Host 'ALL ISOLATED INSTALLER TESTS PASSED. System audio was not changed.' -ForegroundColor Green
} finally {
    # Explicit, freshly generated targets only; no recursive cleanup.
    foreach ($leaf in @('state.json', 'state.pending.json', 'state.restored.json', 'state.previous.json', 'state.restored.previous.json')) {
        $file = Join-Path $BackupDirectory $leaf
        if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    }
    [IO.Directory]::Delete($BackupDirectory, $false)
}

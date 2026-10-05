param([switch]$Sign, [string]$DllPath)

# OFFLINE development packaging only. No pnputil/regsvr32, trust store, boot
# changes, device restart or installation. Development signatures are untrusted.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = Join-Path $projectRoot '.tools'
$infVerif = (Get-ChildItem -LiteralPath $toolsRoot -Recurse -File -Filter infverif.exe | Select-Object -First 1).FullName
$inf2Cat = (Get-ChildItem -LiteralPath $toolsRoot -Recurse -File -Filter inf2cat.exe | Select-Object -First 1).FullName
if (-not $infVerif -or -not $inf2Cat) { throw 'Acquire verified Microsoft development tools first.' }
$dll = $DllPath
if (-not $dll) {
    $candidatePaths = @(
        (Join-Path $projectRoot 'epicenter-apo\build-validated\Release\EpicenterAPOComponent.dll'),
        (Join-Path $projectRoot 'epicenter-apo\build\Release\EpicenterAPOComponent.dll')
    )
    $dll = $candidatePaths | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $dll) { throw 'A built Release APO DLL is required; provide -DllPath.' }
$dll = (Resolve-Path -LiteralPath $dll).Path
# Fail instead of packaging a stale legacy/HKCR build under the new HKR INF.
& (Join-Path $PSScriptRoot 'Test-ApoBinary.ps1') -DllPath $dll -ExpectedClsid '{8F7A7248-F617-45AF-ACDA-C1C10F99E4D8}'
$stageRoot = Join-Path $projectRoot ('artifacts\apo-development\package-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $stageRoot | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stageRoot 'EpicenterAPO.dll')
$infRoot = Join-Path $projectRoot 'epicenter-apo\inf\development'
Copy-Item -LiteralPath (Join-Path $infRoot 'EpicenterApoComponent.inf') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $infRoot 'EpicenterAlc892SpdifExtension.inf') -Destination $stageRoot
if ($Sign) {
    & (Join-Path $PSScriptRoot 'Sign-ApoDevelopmentFiles.ps1') -Path (Join-Path $stageRoot 'EpicenterAPO.dll')
}
foreach ($inf in Get-ChildItem -LiteralPath $stageRoot -File -Filter '*.inf') {
    & $infVerif /w /v $inf.FullName
    if ($LASTEXITCODE -ne 0) { throw "InfVerif rejected $($inf.Name)." }
}
& $inf2Cat "/driver:$stageRoot" /os:10_CO_X64,10_NI_X64,10_GE_X64 /uselocaltime /verbose
if ($LASTEXITCODE -ne 0) { throw 'Inf2Cat rejected the package; do not sign/install it.' }
$catalogs = @(Get-ChildItem -LiteralPath $stageRoot -File -Filter '*.cat')
if ($catalogs.Count -ne 2) { throw 'Expected one catalog per INF; incomplete package.' }
if ($Sign) {
    & (Join-Path $PSScriptRoot 'Sign-ApoDevelopmentFiles.ps1') -Path @($catalogs.FullName)
}
Write-Host "Development package prepared offline: $stageRoot"
Write-Host 'VALIDATION DOES NOT PROVE RUNTIME LOADING. DO NOT INSTALL until signing/trust deployment is explicitly authorized.'
[pscustomobject]@{ PackagePath=$stageRoot; SignedForDevelopment=[bool]$Sign; Installed=$false; EndpointRuntimeValidated=$false }

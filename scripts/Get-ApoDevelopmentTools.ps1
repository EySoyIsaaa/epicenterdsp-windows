param([string]$Version = '10.0.26100.6584')

# Official Microsoft WDK/SDK NuGet packages, local to this workspace. No
# installer, PATH modification, elevation, driver install, or trust change.
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^10\.0\.26100\.[0-9]+$') {
    throw 'Choose a matched release SDK/WDK version from the 26100 toolchain.'
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = Join-Path $projectRoot '.tools'
$downloadRoot = Join-Path $toolsRoot 'downloads'
New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$dotnet = (Get-Command dotnet.exe -ErrorAction Stop).Source

foreach ($packageId in @('microsoft.windows.wdk.x64', 'microsoft.windows.sdk.cpp.x64', 'microsoft.windows.sdk.buildtools')) {
    $archivePath = Join-Path $downloadRoot "$packageId.$Version.nupkg"
    $packageRoot = Join-Path $toolsRoot "$packageId.$Version"
    $packageUri = "https://api.nuget.org/v3-flatcontainer/$packageId/$Version/$packageId.$Version.nupkg"
    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading official Microsoft package $packageId $Version ..."
        Invoke-WebRequest -UseBasicParsing -Uri $packageUri -OutFile $archivePath
    }
    # Validate the signed package before extracting or executing its contents.
    & $dotnet nuget verify $archivePath --all
    if ($LASTEXITCODE -ne 0) { throw "NuGet signature validation failed for $packageId." }
    if (-not (Test-Path -LiteralPath $packageRoot)) {
        Write-Host "Extracting $packageId ..."
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $packageRoot)
    }
}

$requiredNames = @('infverif.exe', 'inf2cat.exe', 'signtool.exe')
foreach ($name in $requiredNames) {
    $found = @(Get-ChildItem -LiteralPath $toolsRoot -Recurse -File -Filter $name)
    if ($found.Count -eq 0) { throw "$name was not present in the verified Microsoft packages." }
    $found | Select-Object Name, FullName
}
Write-Host '[OK] Tools downloaded and extracted locally. Nothing was installed into Windows.'

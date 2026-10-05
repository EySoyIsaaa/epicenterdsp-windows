# Local compilation/tests only. Does not install drivers, change Windows or sign.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$uiOutput = Join-Path $projectRoot 'dist\EpicenterDSP Windows Actualizado'
$uiExe = Join-Path $uiOutput 'EpicenterDSP Windows.exe'
foreach ($process in @(Get-Process -Name 'EpicenterDSP Windows' -ErrorAction SilentlyContinue)) {
    if ($process.Path -eq $uiExe) { throw 'Cierra la version Actualizado antes de recompilar. No se cerrara por fuerza.' }
}

function Invoke-BuildStep([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program fallo con codigo $LASTEXITCODE." }
}

Push-Location $projectRoot
try {
    Invoke-BuildStep 'cmake' @('-S', 'epicenter-core', '-B', 'epicenter-core/build-validated', '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Invoke-BuildStep 'cmake' @('--build', 'epicenter-core/build-validated', '--config', 'Release')
    # Core regression fixtures are resolved relative to its source directory.
    Push-Location 'epicenter-core'
    try { Invoke-BuildStep '.\build-validated\Release\epicenter_test_suite.exe' @() }
    finally { Pop-Location }
    Invoke-BuildStep 'cmake' @('-S', 'epicenter-apo', '-B', 'epicenter-apo/build-validated', '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Invoke-BuildStep 'cmake' @('--build', 'epicenter-apo/build-validated', '--config', 'Release')
    Invoke-BuildStep 'ctest' @('--test-dir', 'epicenter-apo/build-validated', '-C', 'Release', '--output-on-failure')
    Invoke-BuildStep 'dotnet' @('build', 'epicenter-ui/src/EpicenterApp/EpicenterApp.csproj', '-c', 'Release', '-p:Platform=x64', "-p:OutputPath=$uiOutput\")
    Invoke-BuildStep 'dotnet' @('test', 'epicenter-ui/tests/EpicenterApp.Tests/EpicenterApp.Tests.csproj', '-c', 'Release', '-p:Platform=x64')
    Write-Host "[OK] Compilacion y pruebas completadas. Interfaz: $uiExe" -ForegroundColor Green
    Write-Host 'Esto NO instala el APO. La prueba de audio real requiere una instalacion autorizada y validada.'
} finally { Pop-Location }

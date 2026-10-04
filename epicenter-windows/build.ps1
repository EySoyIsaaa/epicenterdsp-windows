param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot 'build'
$DistDir = Join-Path $ProjectRoot 'dist'

$Gpp = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $Gpp) {
    throw 'No se encontró g++. Instala WinLibs/MinGW-w64 y vuelve a ejecutar este script.'
}

cmake -S $ProjectRoot -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración de CMake.' }

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación.' }

ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Fallaron las pruebas.' }

New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
$BuiltExe = Join-Path $BuildDir 'EpicenterDSP Windows.exe'
$FinalExe = Join-Path $DistDir 'EpicenterDSP Windows.exe'
Copy-Item -LiteralPath $BuiltExe -Destination $FinalExe -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'README.md') -Destination (Join-Path $DistDir 'LEEME.txt') -Force

$Exe = $FinalExe
$Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
Write-Host "`nLISTO: $Exe"
Write-Host "SHA256: $Hash"

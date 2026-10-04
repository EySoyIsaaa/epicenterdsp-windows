param(
    [string]$TargetDeviceId = $null
)

$ErrorActionPreference = "Continue"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "   EPICENTER DSP PROFESSIONAL 1.0 - INSTALADOR Y REGISTRO APO   " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ApoDllPath = Join-Path $ProjectRoot "epicenter-apo\build\Release\EpicenterAPO.dll"
$AppExePath = Join-Path $ProjectRoot "epicenter-ui\src\EpicenterApp\bin\Release\net9.0-windows10.0.26100.0\win-x64\EpicenterApp.exe"
if (-not (Test-Path $AppExePath)) {
    $AppExePath = Join-Path $ProjectRoot "epicenter-ui\src\EpicenterApp\bin\Debug\net9.0-windows10.0.26100.0\win-x64\EpicenterApp.exe"
}

if (-not (Test-Path $ApoDllPath)) {
    Write-Host "[ERROR] No se encontro EpicenterAPO.dll en: $ApoDllPath" -ForegroundColor Red
    return
}

$ApoClsid = "{68C32E69-906A-4A0E-9214-41A832A88877}"
$PropKeyEfx = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},3"

# 1. Registrar servidor COM
Write-Host "`n[1/5] Registrando EpicenterAPO.dll en el sistema COM..." -ForegroundColor Yellow
$regResult = Start-Process regsvr32.exe -ArgumentList "/s `"$ApoDllPath`"" -Wait -PassThru
if ($regResult.ExitCode -ne 0) {
    Write-Host "[ADVERTENCIA] regsvr32 devolvio codigo $($regResult.ExitCode)" -ForegroundColor Yellow
} else {
    Write-Host "      [OK] Servidor COM registrado exitosamente." -ForegroundColor Green
}

# 2. Configurar DisableProtectedAudioDG
Write-Host "`n[2/5] Configurando permisos de audiodg.exe..." -ForegroundColor Yellow
$audioKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio"
if (-not (Test-Path $audioKey)) {
    New-Item -Path $audioKey -Force | Out-Null
}
Set-ItemProperty -Path $audioKey -Name "DisableProtectedAudioDG" -Value 1 -Type DWord
Write-Host "      [OK] DisableProtectedAudioDG = 1 configurado." -ForegroundColor Green

# 3. Descubrir endpoints de render activos
Write-Host "`n[3/5] Identificando dispositivos de audio activos..." -ForegroundColor Yellow
$renderBase = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
$endpoints = @()

Get-ChildItem $renderBase | ForEach-Object {
    $id = $_.PSChildName
    $stateProp = Get-ItemProperty $_.PSPath -Name DeviceState -ErrorAction SilentlyContinue
    if ($stateProp -and $stateProp.DeviceState -eq 1) {
        $props = Get-ItemProperty "$($_.PSPath)\Properties" -ErrorAction SilentlyContinue
        $friendlyName = $props.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
        $deviceDesc = $props.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
        $dispName = if (![string]::IsNullOrEmpty($friendlyName)) { $friendlyName } else { $deviceDesc }
        if ([string]::IsNullOrEmpty($dispName)) { $dispName = "Dispositivo de audio" }

        $epObj = [PSCustomObject]@{
            Id = $id
            Name = $dispName
            Path = $_.PSPath
            FxPath = "$($_.PSPath)\FxProperties"
        }
        $endpoints += $epObj
    }
}

if ($endpoints.Count -eq 0) {
    Write-Host "[ERROR] No se encontraron dispositivos de audio de salida activos." -ForegroundColor Red
    return
}

$backupFile = Join-Path $ScriptDir "apo_backup.json"
$backupData = @{}
if (Test-Path $backupFile) {
    try {
        $jsonContent = Get-Content $backupFile -Raw
        $backupData = ConvertFrom-Json $jsonContent -AsHashtable
    } catch {
        $backupData = @{}
    }
}

Write-Host "      Dispositivos activos detectados:" -ForegroundColor Cyan
foreach ($ep in $endpoints) {
    Write-Host "      - [$($ep.Id)] $($ep.Name)" -ForegroundColor Gray
}

# 4. Vincular Epicenter APO a los dispositivos
Write-Host "`n[4/5] Vinculando Epicenter APO..." -ForegroundColor Yellow
$targets = $endpoints
if (![string]::IsNullOrEmpty($TargetDeviceId)) {
    $targets = $endpoints | Where-Object { $_.Id -eq $TargetDeviceId }
}

foreach ($ep in $targets) {
    if (-not (Test-Path $ep.FxPath)) {
        New-Item -Path $ep.FxPath -Force | Out-Null
    }

    $currentFx = Get-ItemProperty -Path $ep.FxPath -Name $PropKeyEfx -ErrorAction SilentlyContinue
    $originalEfx = if ($currentFx) { $currentFx.$PropKeyEfx } else { $null }

    if ($originalEfx -and $originalEfx -ne $ApoClsid) {
        $backupData[$ep.Id] = $originalEfx
        Set-ItemProperty -Path $ep.FxPath -Name "Epicenter_Original_EFX" -Value $originalEfx -Type String
    }

    Set-ItemProperty -Path $ep.FxPath -Name $PropKeyEfx -Value $ApoClsid -Type String
    Write-Host "      [OK] Vinculado a: $($ep.Name)" -ForegroundColor Green
}

# Guardar backup
try {
    $backupData | ConvertTo-Json | Set-Content $backupFile -Force
    Write-Host "      [OK] Respaldo guardado en apo_backup.json" -ForegroundColor Gray
} catch {
    Write-Host "      [ADVERTENCIA] No se pudo escribir apo_backup.json" -ForegroundColor Yellow
}

# 5. Reiniciar audiosrv
Write-Host "`n[5/5] Reiniciando servicio de audio de Windows (audiosrv)..." -ForegroundColor Yellow
try {
    Restart-Service -Name "audiosrv" -Force
    Start-Sleep -Seconds 2
    Write-Host "      [OK] Motor de audio de Windows reiniciado exitosamente." -ForegroundColor Green
} catch {
    Write-Host "      [ADVERTENCIA] No se pudo reiniciar audiosrv automaticamente: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host "`n=================================================================" -ForegroundColor Green
Write-Host "   EPICENTER APO VINCULADO CORRECTAMENTE EN TIEMPO REAL          " -ForegroundColor Green
Write-Host "=================================================================" -ForegroundColor Green
Write-Host "Todo el audio de Windows (YouTube, Spotify, juegos, etc.)" -ForegroundColor White
Write-Host "ahora pasa a traves del motor de reconstruccion armonica." -ForegroundColor White

if (Test-Path $AppExePath) {
    Write-Host "`nIniciando interfaz Epicenter DSP Professional 1.0..." -ForegroundColor Cyan
    $appWorkingDir = Split-Path -Parent $AppExePath
    Start-Process -FilePath $AppExePath -WorkingDirectory $appWorkingDir
}

Write-Host "`n[LISTO] Epicenter APO configurado y activo." -ForegroundColor Green
Write-Host "Puedes cerrar esta ventana cuando lo desees." -ForegroundColor Gray

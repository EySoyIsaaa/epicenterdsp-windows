<#
.SYNOPSIS
    Desvincula Epicenter DSP APO y restaura la configuración original de audio de Windows.
#>

$ErrorActionPreference = "Continue"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "   EPICENTER DSP - DESVINCULACION Y RESTAURACION DE AUDIO       " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ApoDllPath = Join-Path $ProjectRoot "epicenter-apo\build\Release\EpicenterAPO.dll"
$PropKeyEfx = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},3"
$ApoClsid = "{68C32E69-906A-4A0E-9214-41A832A88877}"

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

Write-Host "`n[1/3] Restaurando efectos originales en los endpoints..." -ForegroundColor Yellow
$renderBase = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
Get-ChildItem $renderBase | ForEach-Object {
    $id = $_.PSChildName
    $fxPath = "$($_.PSPath)\FxProperties"
    if (Test-Path $fxPath) {
        $current = Get-ItemProperty -Path $fxPath -Name $PropKeyEfx -ErrorAction SilentlyContinue
        if ($current -and $current.$PropKeyEfx -eq $ApoClsid) {
            if ($backupData.ContainsKey($id)) {
                $orig = $backupData[$id]
                Set-ItemProperty -Path $fxPath -Name $PropKeyEfx -Value $orig -Type String
                Write-Host "      [OK] Restaurado endpoint $id con su APO original." -ForegroundColor Green
            } else {
                Remove-ItemProperty -Path $fxPath -Name $PropKeyEfx -ErrorAction SilentlyContinue
                Write-Host "      [OK] Removido Epicenter APO de endpoint $id." -ForegroundColor Green
            }
            Remove-ItemProperty -Path $fxPath -Name "Epicenter_Original_EFX" -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "`n[2/3] Desregistrando servidor COM de EpicenterAPO.dll..." -ForegroundColor Yellow
if (Test-Path $ApoDllPath) {
    Start-Process regsvr32.exe -ArgumentList "/u /s `"$ApoDllPath`"" -Wait
    Write-Host "      [OK] Servidor COM desregistrado." -ForegroundColor Green
}

Write-Host "`n[3/3] Reiniciando servicio de audio de Windows (audiosrv)..." -ForegroundColor Yellow
try {
    Restart-Service -Name "audiosrv" -Force
    Start-Sleep -Seconds 2
    Write-Host "      [OK] Servicio de audio reiniciado exitosamente." -ForegroundColor Green
} catch {
    Write-Host "      [ADVERTENCIA] No se pudo reiniciar audiosrv automaticamente: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host "`n=================================================================" -ForegroundColor Green
Write-Host "   RESTAURACION COMPLETADA CON EXITO                             " -ForegroundColor Green
Write-Host "=================================================================" -ForegroundColor Green
Write-Host "El sistema de audio ha vuelto a su estado original." -ForegroundColor White
Write-Host "Puedes cerrar esta ventana cuando lo desees." -ForegroundColor Gray

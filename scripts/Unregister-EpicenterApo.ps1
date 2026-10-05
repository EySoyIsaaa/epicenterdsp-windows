$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\ApoInstall.Common.ps1"
try {
    Assert-TestEnvironment
    if (!(Test-Path -LiteralPath $StateFile)) { throw 'No hay respaldo de esta nueva prueba. No se borrara nada. apo_backup.json antiguo NO es un respaldo EFX.' }
    $state = Read-InstallState
    Assert-InstallState $state
    Restore-TestInstall $state
    Write-Host '[OK] Prueba desinstalada y valores previos restaurados y verificados.' -ForegroundColor Green
    Write-Host 'Respaldo conservado como state.restored.json.'
    Read-Host 'Enter para cerrar'
    exit 0
} catch {
    Write-Host ("[ERROR] " + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'No se declaro una restauracion exitosa. Conserva el respaldo y comparte este error.'
    Read-Host 'Enter para cerrar'
    exit 1
}

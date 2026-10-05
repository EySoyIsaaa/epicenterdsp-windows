param([string]$TargetDeviceId)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\ApoInstall.Common.ps1"
$stage = 'Comprobaciones iniciales'
$logReady = $false
try {
    Assert-TestEnvironment
    if (Test-Path -LiteralPath $StateFile) { throw 'Ya existe un respaldo. Ejecuta DESACTIVAR_EPICENTER_APO.bat antes de reinstalar.' }
    if (!(Test-Path -LiteralPath $ApoDllPath)) { throw "Falta DLL Release: $ApoDllPath" }
    Assert-X64Binary $ApoDllPath
    if (!(Test-Path -LiteralPath $AppExePath)) { throw "Falta interfaz Release x64: $AppExePath" }
    Assert-X64Binary $AppExePath
    if ((Test-Path -LiteralPath $ComKey) -or (Test-Path -LiteralPath $ApoKey)) { throw 'Registro APO anterior sin respaldo de esta prueba. Requiere revision; no se modificara.' }
    $endpoints = @(Get-TestEndpoints)
    if (!$endpoints.Count) { throw 'No hay salidas fisicas activas.' }
    if ($TargetDeviceId) {
        $selected = @($endpoints | Where-Object { $_.Id -eq $TargetDeviceId })
        if ($selected.Count -ne 1) { throw 'Dispositivo solicitado no disponible.' }
        $endpoint = $selected[0]
    } else {
        Write-Host "EPICENTER DSP - PRUEBA LOCAL (una sola salida)" -ForegroundColor Cyan
        for ($i = 0; $i -lt $endpoints.Count; $i++) { Write-Host ("{0}. {1}" -f ($i + 1), $endpoints[$i].Name) }
        $selection = 0
        $answer = Read-Host 'Numero de tus bocinas o audifonos (no VB-CABLE)'
        if (![int]::TryParse($answer, [ref]$selection) -or $selection -lt 1 -or $selection -gt $endpoints.Count) { throw 'Seleccion invalida. No se cambio nada.' }
        $endpoint = $endpoints[$selection - 1]
    }
    $originalFx = Get-RegistrySnapshot $endpoint.FxPath $EfxProperty
    if ($originalFx.Exists -and $originalFx.Kind -ne 'String') { throw 'Formato EFX no compatible con esta prueba.' }
    if ($originalFx.Exists -and $originalFx.Value -eq $ApoClsid) { throw 'EFX Epicenter anterior sin respaldo. No se sobrescribira.' }
    $legacyUi = Get-RegistrySnapshot $endpoint.FxPath '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},3'
    if ($legacyUi.Exists -and $legacyUi.Value -eq $ApoClsid) { throw 'Propiedad incorrecta del instalador antiguo detectada. Requiere recuperar apo_backup.json antes de esta prueba.' }
    $protection = Get-RegistrySnapshot $AudioKey 'DisableProtectedAudioDG'
    if ($protection.Exists -and $protection.Kind -ne 'DWord') { throw 'Configuracion de proteccion inesperada.' }
    Write-Host ("Salida: " + $endpoint.Name) -ForegroundColor Yellow
    Write-Host 'Se reemplazara temporalmente su EFX y se reiniciara el audio.'
    Write-Host 'DLL sin firma: DisableProtectedAudioDG=1 se habilitara solo para esta prueba de desarrollo.'
    Write-Host 'Cierra reproductores y baja el volumen. La prueba no es un instalador comercial.'
    if ($protection.Exists -and $protection.Value -eq 1) { Write-Warning 'La proteccion ya estaba deshabilitada. Se restaurara ese valor previo; no se puede inferir el estado anterior al instalador antiguo.' }
    if ((Read-Host 'Escribe SI para continuar, o cualquier otra cosa para cancelar').Trim() -ine 'SI') {
        Write-Host 'Cancelado, sin cambios.'
        Read-Host 'Enter para cerrar'
        exit 0
    }
    Initialize-BackupDirectory
    $logReady = $true
    $stage = 'Comprobar permisos existentes (sin modificar Windows)'
    Write-InstallLog "[PASO] $stage"
    foreach ($path in @($endpoint.FxPath, $AudioKey, 'HKLM:\SOFTWARE\Classes\CLSID', 'HKLM:\SOFTWARE\Classes\AudioEngine')) {
        Write-InstallLog "[COMPROBAR] $path"
        Test-RegistryWriteAccess $path
    }
    $state = [pscustomobject]@{
        Version = 1; CreatedUtc = [DateTime]::UtcNow.ToString('o')
        DllPath = $ApoDllPath; EndpointId = $endpoint.Id; EndpointName = $endpoint.Name
        OriginalEfx = $originalFx; Protection = $protection
        RegistrationAttempted = $false; Status = 'Prepared'; Stage = 'Respaldo guardado'
    }
    Save-InstallState $state
    try {
        $stage = 'Registrar DLL COM/APO'
        Set-InstallStage $state $stage
        $state.RegistrationAttempted = $true
        Save-InstallState $state
        Invoke-ApoRegistration $state.DllPath
        Assert-ApoRegistration $state.DllPath
        $stage = 'Configurar DisableProtectedAudioDG'
        Set-InstallStage $state $stage
        if (!(Test-Path -LiteralPath $AudioKey)) { New-Item -Path $AudioKey -Force | Out-Null }
        Set-ItemProperty -LiteralPath $AudioKey -Name 'DisableProtectedAudioDG' -Value 1 -Type DWord
        $actualProtection = Get-RegistrySnapshot $AudioKey 'DisableProtectedAudioDG'
        if (!$actualProtection.Exists -or $actualProtection.Value -ne 1) { throw 'No se verifico la configuracion de desarrollo.' }
        $stage = "Vincular EFX: $($endpoint.FxPath) / $EfxProperty"
        Set-InstallStage $state $stage
        if (!(Test-Path -LiteralPath $endpoint.FxPath)) { New-Item -Path $endpoint.FxPath -Force | Out-Null }
        Set-ItemProperty -LiteralPath $endpoint.FxPath -Name $EfxProperty -Value $ApoClsid -Type String
        $actual = Get-RegistrySnapshot $endpoint.FxPath $EfxProperty
        if (!$actual.Exists -or $actual.Value -ne $ApoClsid) { throw 'Vinculacion EFX no verificada.' }
        $stage = 'Reiniciar motor de audio'
        Set-InstallStage $state $stage
        Restart-TestAudio
        $state.Status = 'Installed'
        Save-InstallState $state
    } catch {
        $installError = "Paso: $stage. $($_.Exception.Message)"
        Write-InstallLog ("[FALLO] " + $installError + ' | ' + $_.InvocationInfo.PositionMessage)
        Write-Warning "Fallo la instalacion: $installError. Restaurando..."
        try { Restore-TestInstall $state }
        catch { throw "Instalacion fallida: $installError. Restauracion incompleta: $($_.Exception.Message). CONSERVA $StateFile" }
        throw "Instalacion cancelada y restaurada: $installError"
    }
    Write-Host ("[OK] Registro y EFX configurados para: " + $endpoint.Name) -ForegroundColor Green
    Write-Host 'Esto NO verifica todavia el procesamiento real en audiodg.'
    Write-Host "Respaldo: $StateFile"
    Write-Host "Registro de pasos: $(Join-Path $BackupDirectory 'install.log')"
    Write-Host 'Selecciona esa misma salida en Windows y abre la interfaz SIN administrador:'
    Write-Host $AppExePath -ForegroundColor Cyan
    Write-Host 'Para recuperar la configuracion: DESACTIVAR_EPICENTER_APO.bat'
    Read-Host 'Enter para cerrar'
    exit 0
} catch {
    $failure = "[ERROR] Paso: $stage. $($_.Exception.Message)"
    Write-Host $failure -ForegroundColor Red
    Write-Host $_.InvocationInfo.PositionMessage
    if ($logReady) {
        try { Write-InstallLog $failure } catch { Write-Warning 'No se pudo escribir el log; conserva el texto de esta ventana.' }
    }
    Write-Host 'No se declaro una instalacion exitosa. Conserva cualquier respaldo.'
    Read-Host 'Enter para cerrar'
    exit 1
}

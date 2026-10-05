param([ValidateSet('Install', 'Uninstall')][string]$Mode = 'Install')
$ErrorActionPreference = 'Stop'
try {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (![Environment]::Is64BitProcess) { throw 'Se requiere PowerShell x64.' }
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Mode ' + $Mode
        $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $process.ExitCode
    }
    $scriptName = if ($Mode -eq 'Install') { 'Register-EpicenterApo.ps1' } else { 'Unregister-EpicenterApo.ps1' }
    & (Join-Path $PSScriptRoot $scriptName)
} catch {
    Write-Host ("[ERROR] " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

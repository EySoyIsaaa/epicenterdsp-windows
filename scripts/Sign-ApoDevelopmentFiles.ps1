param([Parameter(Mandatory=$true)][string[]]$Path)

# Signs workspace staging files only with the local ephemeral-exported identity.
# Does not import certificates, change trust/boot settings, or install anything.
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\apo-development'))
$identityRoot = Join-Path $projectRoot '.dev-signing'
$tool = @(Get-ChildItem (Join-Path $projectRoot '.tools') -Recurse -File -Filter signtool.exe |
    Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1)
if ($tool.Count -ne 1) { throw 'A local verified x64 SignTool is required. Run Get-ApoDevelopmentTools.ps1.' }
$pfxPath = Join-Path $identityRoot 'EpicenterDSP-Development.pfx'
$protectedPassword = Join-Path $identityRoot 'pfx-password.dpapi'
if (-not (Test-Path -LiteralPath $pfxPath) -or -not (Test-Path -LiteralPath $protectedPassword)) {
    throw 'Create the file-only local development identity first.'
}

$files = foreach ($item in $Path) {
    $fullPath = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $item).Path)
    if (-not $fullPath.StartsWith($allowedRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Sign only staging copies below artifacts\apo-development, not source or installed binaries: $fullPath"
    }
    if ([System.IO.Path]::GetExtension($fullPath) -notin @('.dll', '.cat', '.exe')) {
        throw "Unsupported development signing target: $fullPath"
    }
    $fullPath
}
Add-Type -AssemblyName System.Security
$passwordBytes = [System.Security.Cryptography.ProtectedData]::Unprotect(
    [System.IO.File]::ReadAllBytes($protectedPassword),
    [System.Text.Encoding]::UTF8.GetBytes('EpicenterDSP.LocalDevelopment'),
    [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
try {
    $password = [System.Text.Encoding]::UTF8.GetString($passwordBytes)
    foreach ($file in $files) {
        # Never echo the password or this native command line.
        & $tool[0].FullName sign /fd SHA256 /f $pfxPath /p $password $file
        if ($LASTEXITCODE -ne 0) { throw "Development signing failed for $file." }
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if (-not $signature.SignerCertificate) { throw "No signature found after signing $file." }
        [pscustomobject]@{ Path=$file; SignatureStatus=$signature.Status; Signer=$signature.SignerCertificate.Subject }
    }
} finally {
    [Array]::Clear($passwordBytes, 0, $passwordBytes.Length)
    $password = $null
}
Write-Host 'Development signatures are NOT trusted by Windows. Nothing was installed or added to a trust store.'

param()

# Creates only local files and an ephemeral in-memory code-signing key.
# Does NOT add any certificate to CurrentUser or LocalMachine certificate stores.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$identityRoot = Join-Path $projectRoot '.dev-signing'
if (Test-Path -LiteralPath $identityRoot) {
    throw 'The local signing identity folder already exists. Reuse it; do not overwrite private keys.'
}
New-Item -ItemType Directory -Path $identityRoot | Out-Null
# Restrict only the newly created workspace secret directory to its current user.
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = New-Object System.Security.AccessControl.DirectorySecurity
$acl.SetOwner($identity)
$acl.SetAccessRuleProtection($true, $false)
$rule = New-Object System.Security.AccessControl.FileSystemAccessRule($identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
$acl.AddAccessRule($rule)
Set-Acl -LiteralPath $identityRoot -AclObject $acl
Add-Type -AssemblyName System.Security

if (-not ('EpicenterDevelopmentIdentity.Generator' -as [type])) {
    Add-Type -ReferencedAssemblies System.Security -TypeDefinition @'
using System;
using System.IO;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;

namespace EpicenterDevelopmentIdentity {
    public static class Generator {
        public static string Create(string folder) {
            byte[] random = new byte[32];
            using (var rng = RandomNumberGenerator.Create()) rng.GetBytes(random);
            string password = Convert.ToBase64String(random);
            Array.Clear(random, 0, random.Length);
            using (var rsa = new RSACng(3072)) {
                if (!rsa.Key.IsEphemeral) throw new InvalidOperationException("Signing key must remain ephemeral");
                var request = new CertificateRequest("CN=EpicenterDSP Labs LOCAL DEVELOPMENT ONLY", rsa,
                    HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
                request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
                request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature, true));
                var usages = new OidCollection();
                usages.Add(new Oid("1.3.6.1.5.5.7.3.3"));
                request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(usages, true));
                request.CertificateExtensions.Add(new X509SubjectKeyIdentifierExtension(request.PublicKey, false));
                using (var cert = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-5), DateTimeOffset.UtcNow.AddMonths(6))) {
                    File.WriteAllBytes(Path.Combine(folder, "EpicenterDSP-Development.pfx"),
                        cert.Export(X509ContentType.Pkcs12, password));
                    File.WriteAllBytes(Path.Combine(folder, "EpicenterDSP-Development.cer"), cert.Export(X509ContentType.Cert));
                    byte[] passwordBytes = Encoding.UTF8.GetBytes(password);
                    try {
                        byte[] protectedPassword = ProtectedData.Protect(passwordBytes,
                            Encoding.UTF8.GetBytes("EpicenterDSP.LocalDevelopment"), DataProtectionScope.CurrentUser);
                        File.WriteAllBytes(Path.Combine(folder, "pfx-password.dpapi"), protectedPassword);
                    } finally { Array.Clear(passwordBytes, 0, passwordBytes.Length); }
                    return cert.Thumbprint;
                }
            }
        }
    }
}
'@
}
$thumbprint = [EpicenterDevelopmentIdentity.Generator]::Create($identityRoot)
Write-Host "Local development identity created: $thumbprint"
Write-Host 'Private PFX and user-DPAPI-protected password are in .dev-signing. Do not share or commit them.'
Write-Host 'Certificate NOT imported or trusted. Drivers are NOT installed. Secure Boot/testsigning are unchanged.'

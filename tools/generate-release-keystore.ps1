param(
    [string]$KeystorePath = "$env:USERPROFILE/.android/dualscreen-launcher-release.jks",
    [string]$PropertiesPath = "keystore.properties",
    [string]$Alias = "dualscreen-launcher"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$propertiesFile = Join-Path $repositoryRoot $PropertiesPath
$resolvedKeystore = [System.IO.Path]::GetFullPath($KeystorePath)

if (Test-Path -LiteralPath $resolvedKeystore) {
    throw "Refusing to overwrite existing release keystore: $resolvedKeystore"
}
if (Test-Path -LiteralPath $propertiesFile) {
    throw "Refusing to overwrite existing signing properties: $propertiesFile"
}

$alphabet = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#%_-"
$random = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $bytes = New-Object byte[] 48
    $random.GetBytes($bytes)
    $password = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })
}
finally {
    $random.Dispose()
}

$parent = Split-Path -Parent $resolvedKeystore
$null = New-Item -ItemType Directory -Force -Path $parent
$keytool = Join-Path $env:JAVA_HOME "bin/keytool.exe"
if (-not (Test-Path -LiteralPath $keytool)) {
    $keytool = (Get-Command keytool.exe -ErrorAction Stop).Source
}

& $keytool -genkeypair -v -keystore $resolvedKeystore -storetype PKCS12 -storepass $password -keypass $password -alias $Alias -keyalg RSA -keysize 4096 -sigalg SHA256withRSA -validity 10000 -dname "CN=Dualscreen Launcher Release, O=Dualscreen Launcher"
if ($LASTEXITCODE -ne 0) {
    throw "keytool failed with exit code $LASTEXITCODE"
}

$propertyPath = $resolvedKeystore.Replace('\', '/')
$properties = @(
    "storeFile=$propertyPath"
    "storePassword=$password"
    "keyAlias=$Alias"
    "keyPassword=$password"
)
[System.IO.File]::WriteAllLines(
    $propertiesFile,
    $properties,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Host "Release keystore created: $resolvedKeystore"
Write-Host "Ignored signing properties created: $propertiesFile"
Write-Host "Back up both files securely. Passwords were not printed."

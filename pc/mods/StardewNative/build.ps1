[CmdletBinding()]
param(
    [string] $OriginalGameDll,
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'

$moduleDirectory = Split-Path -Parent $PSCommandPath
$repositoryRoot = Resolve-Path (Join-Path $moduleDirectory '..\..\..')
if ([string]::IsNullOrWhiteSpace($OriginalGameDll)) {
    $OriginalGameDll = Join-Path $repositoryRoot '.work\stardew-native\device-original\Stardew Valley.dll'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot 'artifacts\stardew-native-1.6.15.24356'
}

$nativeProject = Join-Path $moduleDirectory 'StardewDualScreen.Native.csproj'
$patcherProject = Join-Path $repositoryRoot 'tools\StardewNativePatcher\StardewNativePatcher.csproj'
$nativeOutput = Join-Path $moduleDirectory 'bin\Release\net6.0\StardewDualScreen.Native.dll'
$nativeSymbols = [System.IO.Path]::ChangeExtension($nativeOutput, '.pdb')
$patchedGame = Join-Path $OutputDirectory 'Stardew Valley.dll'
$referenceDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $OriginalGameDll)

dotnet build $nativeProject -c Release "-p:GameReferenceDirectory=$referenceDirectory"
if ($LASTEXITCODE -ne 0) { throw 'The native runtime build failed.' }

dotnet build $patcherProject -c Release
if ($LASTEXITCODE -ne 0) { throw 'The patcher build failed.' }

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
dotnet run --project $patcherProject -c Release --no-build -- `
    $OriginalGameDll $nativeOutput $patchedGame
if ($LASTEXITCODE -ne 0) { throw 'The Stardew Valley assembly patch failed.' }

Copy-Item -LiteralPath $nativeSymbols `
    -Destination (Join-Path $OutputDirectory 'StardewDualScreen.Native.pdb') -Force
Copy-Item -LiteralPath (Join-Path $moduleDirectory 'INSTALL.md') `
    -Destination (Join-Path $OutputDirectory 'INSTALL.md') -Force

$originalHash = (Get-FileHash -LiteralPath $OriginalGameDll -Algorithm SHA256).Hash
$patchedHash = (Get-FileHash -LiteralPath $patchedGame -Algorithm SHA256).Hash
$runtimeHash = (Get-FileHash -LiteralPath $nativeOutput -Algorithm SHA256).Hash
$distribution = switch ($originalHash) {
    '7F1E5B8E58D2758B78570BA771BBEB03D33522F62188BF6C32EDF0CF626DEAEE' { 'Steam'; break }
    '078C04DCF34FDD8DF3EC1DDCE062D3DFD4A34D94614D806466ADC78754A7693C' { 'GOG'; break }
    default { 'Unknown' }
}
$manifest = [ordered]@{
    gameVersion = '1.6.15.24356'
    distribution = $distribution
    originalGameSha256 = $originalHash
    patchedGameSha256 = $patchedHash
    embeddedRuntimeSha256 = $runtimeHash
    transport = 'original-managed-ui'
    inputTransport = 'websocket://127.0.0.1:7786'
    smapiCompatible = $true
    requiredRuntimeFiles = @('Stardew Valley.dll')
    optionalDeveloperFiles = @('StardewDualScreen.Native.pdb', 'INSTALL.md')
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content `
    -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding utf8

Write-Host "Native dual-screen build staged at $OutputDirectory"
Write-Host "Patched SHA-256: $patchedHash"

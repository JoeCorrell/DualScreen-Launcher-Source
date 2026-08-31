param(
    [Parameter(Mandatory = $false)]
    [string]$WemuRoot = "C:\Projects\New folder (10)"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$payloadRoot = Join-Path $projectRoot "launcher\src\main\assets\mods\stardew-valley"
$payload = Join-Path $payloadRoot "payload"
$deltaProject = Join-Path $projectRoot "tools\BinaryDeltaPacker\BinaryDeltaPacker.csproj"

$gogOriginal = Join-Path $WemuRoot ".work\stardew-native\device-original\Stardew Valley.dll"
$steamOriginal = Join-Path $WemuRoot ".work\stardew-native\steam-original\Stardew Valley.dll"
$gogPatched = Join-Path $WemuRoot "artifacts\stardew-native-1.6.15.24356\Stardew Valley.dll"
$steamPatched = Join-Path $WemuRoot "artifacts\stardew-native-steam-1.6.15.24356\Stardew Valley.dll"

$expected = [ordered]@{
    gogInput = "078c04dcf34fdd8df3ec1ddce062d3dfd4a34d94614d806466adc78754a7693c"
    gogOutput = "7579cca523d19470b4f842b9068b6bf9b8f96efe632b83471bc62cae7f7b3b90"
    steamInput = "7f1e5b8e58d2758b78570ba771bbeb03d33522f62188bf6c32edf0cf626deaee"
    steamOutput = "f3a92e73f1cdf7c0d4eb5f995b2555244fbe16d33429e26c2b425f6eb1480c63"
}

function Assert-Hash([string]$Path, [string]$ExpectedHash) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing Stardew native build input: $Path"
    }
    $observed = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($observed -ne $ExpectedHash) {
        throw "Unexpected Stardew build input hash for $Path`: $observed"
    }
}

Assert-Hash $gogOriginal $expected.gogInput
Assert-Hash $gogPatched $expected.gogOutput
Assert-Hash $steamOriginal $expected.steamInput
Assert-Hash $steamPatched $expected.steamOutput

if (Test-Path -LiteralPath $payloadRoot) {
    $resolved = (Resolve-Path -LiteralPath $payloadRoot).Path
    $assetRoot = [IO.Path]::GetFullPath(
        (Join-Path $projectRoot "launcher\src\main\assets\mods")
    ) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($assetRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace a payload outside $assetRoot"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $payload -Force | Out-Null

$gogPatch = Join-Path $payload "stardew-1.6.15.24356-gog.dsdiff.gz"
$steamPatch = Join-Path $payload "stardew-1.6.15.24356-steam.dsdiff.gz"
& dotnet run --project $deltaProject -c Release -- $gogOriginal $gogPatched $gogPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the GOG Stardew native delta" }
& dotnet run --project $deltaProject -c Release -- $steamOriginal $steamPatched $steamPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the Steam Stardew native delta" }

$patches = @(
    [ordered]@{
        source = [IO.Path]::GetFileName($gogPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $gogPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.gogInput
        outputSha256 = $expected.gogOutput
    },
    [ordered]@{
        source = [IO.Path]::GetFileName($steamPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $steamPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.steamInput
        outputSha256 = $expected.steamOutput
    }
)
$manifest = [ordered]@{
    version = "native-1.6.15.24356-2026.08.30"
    files = @()
    patches = $patches
    remove = @(
        "Mods/StardewDualScreen/StardewDualScreen.dll",
        "Mods/StardewDualScreen/manifest.json"
    )
} | ConvertTo-Json -Depth 6
$utf8WithoutBom = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText(
    (Join-Path $payloadRoot "manifest.json"),
    $manifest,
    $utf8WithoutBom
)

Write-Output "Imported verified Stardew native deltas into $payloadRoot"

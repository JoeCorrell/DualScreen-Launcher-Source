param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$BuildInputRoot
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$payloadRoot = Join-Path $projectRoot "launcher\src\main\assets\mods\stardew-valley"
$payload = Join-Path $payloadRoot "payload"
$deltaProject = Join-Path $projectRoot "tools\BinaryDeltaPacker\BinaryDeltaPacker.csproj"

$buildInputs = [IO.Path]::GetFullPath($BuildInputRoot)
$gogOriginal = Join-Path $buildInputs "device-original\Stardew Valley.dll"
$steamOriginal = Join-Path $buildInputs "steam-original\Stardew Valley.dll"
$gogDisplaySync = Join-Path $buildInputs "display-sync\gog\Stardew Valley.dll"
$steamDisplaySync = Join-Path $buildInputs "display-sync\steam\Stardew Valley.dll"
$gogRestored = Join-Path $buildInputs "restored\gog\Stardew Valley.dll"
$steamRestored = Join-Path $buildInputs "restored\steam\Stardew Valley.dll"

$expected = [ordered]@{
    gogInput = "078c04dcf34fdd8df3ec1ddce062d3dfd4a34d94614d806466adc78754a7693c"
    gogRestored = "f3ebc7c7cda1199c3919fd2d1619722b3e045b2148aece7cf0bb9c8e4d47d466"
    gogDisplaySync = "58fd97df1e77d03a4bf2196a69b7732eefbb1d1e1743535af1d2ab1162053539"
    steamInput = "7f1e5b8e58d2758b78570ba771bbeb03d33522f62188bf6c32edf0cf626deaee"
    steamRestored = "9bbcb7d5be1d059f863173802e68d9ce1a48a0800963a2cfbcc359f696035b9c"
    steamDisplaySync = "9b03cc5106f24e861e3bceb38e68ae6b286b77b83f4c804bd3818b16c1dbc172"
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
Assert-Hash $gogRestored $expected.gogRestored
Assert-Hash $gogDisplaySync $expected.gogDisplaySync
Assert-Hash $steamOriginal $expected.steamInput
Assert-Hash $steamRestored $expected.steamRestored
Assert-Hash $steamDisplaySync $expected.steamDisplaySync

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
$gogMigrationPatch = Join-Path $payload "stardew-1.6.15.24356-gog-display-sync-to-restored.dsdiff.gz"
$steamMigrationPatch = Join-Path $payload "stardew-1.6.15.24356-steam-display-sync-to-restored.dsdiff.gz"
& dotnet run --project $deltaProject -c Release -- $gogOriginal $gogRestored $gogPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the GOG Stardew native delta" }
& dotnet run --project $deltaProject -c Release -- $steamOriginal $steamRestored $steamPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the Steam Stardew native delta" }
& dotnet run --project $deltaProject -c Release -- $gogDisplaySync $gogRestored $gogMigrationPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the GOG Stardew rollback delta" }
& dotnet run --project $deltaProject -c Release -- $steamDisplaySync $steamRestored $steamMigrationPatch
if ($LASTEXITCODE -ne 0) { throw "Could not create the Steam Stardew rollback delta" }

$patches = @(
    [ordered]@{
        source = [IO.Path]::GetFileName($gogPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $gogPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.gogInput
        outputSha256 = $expected.gogRestored
    },
    [ordered]@{
        source = [IO.Path]::GetFileName($steamPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $steamPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.steamInput
        outputSha256 = $expected.steamRestored
    },
    [ordered]@{
        source = [IO.Path]::GetFileName($gogMigrationPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $gogMigrationPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.gogDisplaySync
        outputSha256 = $expected.gogRestored
    },
    [ordered]@{
        source = [IO.Path]::GetFileName($steamMigrationPatch)
        target = "Stardew Valley.dll"
        sha256 = (Get-FileHash -LiteralPath $steamMigrationPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        inputSha256 = $expected.steamDisplaySync
        outputSha256 = $expected.steamRestored
    }
)
$manifest = [ordered]@{
    version = "native-1.6.15.24356-2026.08.31-original-ui-restored-rollback-1"
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

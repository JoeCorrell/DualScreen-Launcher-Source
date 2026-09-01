# Builds floor-aware map rasters from the user's own Director's Cut archives.
# No extracted game artwork is stored in source control or downloaded.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Game,
    [string] $Output,
    [double] $Zoom = 4.0,
    [ValidateRange(512, 8192)]
    [int] $MaxDimension = 3072,
    [string] $Java = "java",
    [switch] $KeepPng,
    [int[]] $OnlyMap
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Output) {
    $Output = Join-Path $root "build\map-cache\MapCache"
}
$gamePath = [IO.Path]::GetFullPath($Game)
$outputPath = [IO.Path]::GetFullPath($Output)
$fileList = Join-Path $env:LOCALAPPDATA "DeusExSecondScreen\Tools\bigfile.filelist"
$toolRoot = Join-Path $env:LOCALAPPDATA "DeusExSecondScreen\Tools\FFDec\26.2.1"
$archive = Join-Path $toolRoot "ffdec_26.2.1.zip"
$ffdec = Join-Path $toolRoot "ffdec\ffdec.jar"
$download = "https://github.com/jindrapetrik/jpexs-decompiler/releases/download/version26.2.1/ffdec_26.2.1.zip"
$expectedSha256 = "0333b56998a55bd83f4e0deb678a811fcdc45607582b4f5dd438309c8c3ad5ce"
$fileListUrl = "https://raw.githubusercontent.com/rrika/dxhr/master/data/Deus%20Ex%203%20-%20Director%27s%20Cut/files/bigfile.filelist"
$fileListSha256 = "7d9089b2140bf94b1901899b6fcb8db6a13d0db305c056b06100b916e16575b1"

if (-not (Test-Path -LiteralPath (Join-Path $gamePath "BIGFILE.000") -PathType Leaf)) {
    throw "BIGFILE.000 was not found under $gamePath"
}
if ($Zoom -lt 0.25 -or $Zoom -gt 12.0) {
    throw "Zoom must be between 0.25 and 12.0"
}

$javaCommand = Get-Command $Java -ErrorAction SilentlyContinue
if (-not $javaCommand) {
    throw "Java 8 or newer is required for the offline FFDec export. Install a JRE/JDK, then rerun this command."
}
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) {
    $pythonCommand = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    throw "Python 3 was not found on PATH."
}

New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive -PathType Leaf) -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedSha256) {
    Write-Host "Downloading the pinned official FFDec 26.2.1 archive..."
    Invoke-WebRequest -Uri $download -OutFile $archive
}
$actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "FFDec archive SHA-256 mismatch; expected $expectedSha256, found $actualSha256"
}
if (-not (Test-Path -LiteralPath $ffdec -PathType Leaf)) {
    $extract = Join-Path $toolRoot "ffdec"
    if (Test-Path -LiteralPath $extract) {
        Remove-Item -LiteralPath $extract -Recurse -Force
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $extract
}

if (-not (Test-Path -LiteralPath $fileList -PathType Leaf) -or
    (Get-FileHash -LiteralPath $fileList -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fileListSha256) {
    Write-Host "Downloading the Director's Cut path index used to address the local BIGFILE..."
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $fileList) | Out-Null
    Invoke-WebRequest -Uri $fileListUrl -OutFile $fileList
}
if ((Get-FileHash -LiteralPath $fileList -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fileListSha256) {
    throw "Director's Cut file-list SHA-256 mismatch; refusing unpinned archive metadata."
}

$arguments = @(
    (Join-Path $PSScriptRoot "build_map_cache.py"),
    "--game", $gamePath,
    "--file-list", $fileList,
    "--ffdec", $ffdec,
    "--java", $javaCommand.Source,
    "--output", $outputPath,
    "--zoom", $Zoom.ToString([Globalization.CultureInfo]::InvariantCulture),
    "--max-dimension", [string] $MaxDimension
)
if ($KeepPng) { $arguments += "--keep-png" }
foreach ($map in $OnlyMap) { $arguments += @("--only-map", [string] $map) }

Write-Host "Building local vanilla map cache..."
& $pythonCommand.Source @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Map cache builder exited with code $LASTEXITCODE"
}
Write-Host "Map cache ready: $outputPath"

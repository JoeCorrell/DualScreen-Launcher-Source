param(
    [string]$Version = "2026.08.30.1",
    [string]$Repository = "JoeCorrell/DualScreen-Launcher-Payloads",
    [string]$Tag = "launcher-payloads-v1",
    [string]$OutputDirectory = "payload-release/launcher-payloads-v1"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$assetsRoot = Join-Path $repositoryRoot "launcher/src/main/assets"
$outputRoot = Join-Path $repositoryRoot $OutputDirectory
$null = New-Item -ItemType Directory -Force -Path $outputRoot

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function New-PayloadPack {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][System.IO.FileInfo[]]$Files,
        [Parameter(Mandatory = $true)][System.IO.Compression.CompressionLevel]$Compression
    )

    if ($Files.Count -eq 0) {
        throw "Payload pack $Name has no files."
    }

    $target = Join-Path $outputRoot $Name
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Force
    }
    $stream = [System.IO.File]::Open($target, [System.IO.FileMode]::CreateNew)
    try {
        $archive = New-Object System.IO.Compression.ZipArchive(
            $stream,
            [System.IO.Compression.ZipArchiveMode]::Create,
            $false
        )
        try {
            foreach ($file in ($Files | Sort-Object FullName)) {
                $relative = $file.FullName.Substring($assetsRoot.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
                $entry = $archive.CreateEntry($relative, $Compression)
                $entry.LastWriteTime = [DateTimeOffset]::Parse("1980-01-01T00:00:00Z")
                $input = $file.OpenRead()
                $output = $entry.Open()
                try {
                    $input.CopyTo($output, 1MB)
                }
                finally {
                    $output.Dispose()
                    $input.Dispose()
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }

    $result = Get-Item -LiteralPath $target
    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    [ordered]@{
        name = $result.Name
        bytes = $result.Length
        sha256 = $hash
        url = "https://github.com/$Repository/releases/download/$Tag/$($result.Name)"
    }
}

$componentFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $assetsRoot "components") -File |
        Where-Object { $_.Extension -eq ".tgz" }
)
$modFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $assetsRoot "mods") -Recurse -File |
        Where-Object { $_.FullName.Replace('\', '/') -match '/payload/' }
)

$packs = @(
    New-PayloadPack -Name "runtime-components-$Version.zip" -Files $componentFiles -Compression ([System.IO.Compression.CompressionLevel]::NoCompression)
    New-PayloadPack -Name "game-mods-$Version.zip" -Files $modFiles -Compression ([System.IO.Compression.CompressionLevel]::Optimal)
)

$manifest = [ordered]@{
    schema = 1
    version = $Version
    repository = "https://github.com/$Repository"
    tag = $Tag
    packs = $packs
}
$manifestPath = Join-Path $outputRoot "payload-bootstrap.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText(
    $manifestPath,
    $manifestJson,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Host "Payload release created at $outputRoot"
$packs | ForEach-Object {
    Write-Host ("  {0}: {1} bytes, sha256={2}" -f $_.name, $_.bytes, $_.sha256)
}

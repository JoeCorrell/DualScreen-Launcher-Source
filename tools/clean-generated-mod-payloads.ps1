param(
    [Parameter(Mandatory = $true)]
    [string]$ConfirmedArchive
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$modsRoot = [IO.Path]::GetFullPath(
    (Join-Path $projectRoot "launcher\src\main\assets\mods")
)
$archive = (Resolve-Path -LiteralPath $ConfirmedArchive -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    throw "The preserved payload archive does not exist: $archive"
}

$prefix = $modsRoot + [IO.Path]::DirectorySeparatorChar
$files = @(
    Get-ChildItem -LiteralPath $modsRoot -File -Recurse -Force |
        Where-Object { $_.FullName -like "*\payload\*" }
)
foreach ($file in $files) {
    $resolved = [IO.Path]::GetFullPath($file.FullName)
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a generated file outside $modsRoot"
    }
    if ($resolved -notlike "*\payload\*") {
        throw "Refusing to remove a non-payload file: $resolved"
    }
}

# Files are removed individually after every resolved target is checked. Empty
# directories are then removed without a recursive delete.
foreach ($file in $files) {
    Remove-Item -LiteralPath $file.FullName -Force
}
Get-ChildItem -LiteralPath $modsRoot -Directory -Recurse -Force |
    Sort-Object { $_.FullName.Length } -Descending |
    ForEach-Object {
        if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) {
            Remove-Item -LiteralPath $_.FullName -Force
        }
    }

Write-Output "Removed $($files.Count) generated mod payload files; preserved archive: $archive"

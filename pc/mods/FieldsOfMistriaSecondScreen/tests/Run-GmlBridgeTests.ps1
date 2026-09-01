param(
    [string]$ExactArchive
)

$ErrorActionPreference = 'Stop'
$python = Get-Command python -ErrorAction Stop
$root = Split-Path -Parent $PSScriptRoot

& $python.Source -m unittest discover -s (Join-Path $root 'tests') -p 'test_patch_assets.py' -v
if ($LASTEXITCODE -ne 0) {
    throw "GML bridge tests failed with exit code $LASTEXITCODE"
}

& (Join-Path $root 'deploy-assets.ps1') -SelfTest

if ($ExactArchive) {
    & $python.Source (Join-Path $root 'tools\patch_assets.py') `
        --input $ExactArchive `
        --dry-run
    if ($LASTEXITCODE -ne 0) {
        throw "Exact v1.0.4 dry-run failed with exit code $LASTEXITCODE"
    }
}

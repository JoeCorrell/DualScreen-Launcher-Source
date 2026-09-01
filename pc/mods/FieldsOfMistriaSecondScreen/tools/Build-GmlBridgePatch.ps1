[CmdletBinding(DefaultParameterSetName = 'Build')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InputArchive,

    [Parameter(Mandatory = $true, ParameterSetName = 'Build')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputArchive,

    [Parameter(ParameterSetName = 'Build')]
    [string]$Manifest,

    [Parameter(Mandatory = $true, ParameterSetName = 'DryRun')]
    [switch]$DryRun,

    [Parameter(ParameterSetName = 'Build')]
    [switch]$Replace,

    [Parameter(ParameterSetName = 'Build')]
    [switch]$SkipFullCrc
)

$ErrorActionPreference = 'Stop'
$python = Get-Command python -ErrorAction Stop
$tool = Join-Path $PSScriptRoot 'patch_assets.py'
$arguments = @($tool, '--input', $InputArchive)

if ($DryRun) {
    $arguments += '--dry-run'
} else {
    $arguments += @('--output', $OutputArchive)
    if ($Manifest) {
        $arguments += @('--manifest', $Manifest)
    }
    if ($Replace) {
        $arguments += '--replace'
    }
    if ($SkipFullCrc) {
        $arguments += '--skip-full-crc'
    }
}

& $python.Source @arguments
if ($LASTEXITCODE -ne 0) {
    throw "GML bridge patch generation failed with exit code $LASTEXITCODE"
}

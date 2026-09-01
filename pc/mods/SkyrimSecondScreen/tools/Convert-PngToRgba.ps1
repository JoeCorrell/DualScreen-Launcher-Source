[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $InputPath,
    [Parameter(Mandatory = $true)][string] $OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$resolved = (Resolve-Path -LiteralPath $InputPath).Path
$bitmap = [System.Drawing.Bitmap]::FromFile($resolved)
try {
    if ($bitmap.Width -le 0 -or $bitmap.Height -le 0 -or
        $bitmap.Width -gt 2048 -or $bitmap.Height -gt 2048) {
        throw "Unsafe image dimensions: $($bitmap.Width)x$($bitmap.Height)"
    }
    $bytes = New-Object byte[] (12 + $bitmap.Width * $bitmap.Height * 4)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes("RGBA"), 0, $bytes, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$bitmap.Width), 0, $bytes, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$bitmap.Height), 0, $bytes, 8, 4)
    $at = 12
    for ($y = 0; $y -lt $bitmap.Height; ++$y) {
        for ($x = 0; $x -lt $bitmap.Width; ++$x) {
            $pixel = $bitmap.GetPixel($x, $y)
            $bytes[$at++] = $pixel.R
            $bytes[$at++] = $pixel.G
            $bytes[$at++] = $pixel.B
            $bytes[$at++] = $pixel.A
        }
    }
    $parent = Split-Path -Parent $OutputPath
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllBytes($OutputPath, $bytes)
}
finally {
    $bitmap.Dispose()
}

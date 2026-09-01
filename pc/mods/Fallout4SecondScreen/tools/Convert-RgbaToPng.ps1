[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $InputPath,
    [Parameter(Mandatory = $true)][string] $OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$source = (Resolve-Path -LiteralPath $InputPath).Path
$bytes = [IO.File]::ReadAllBytes($source)
if ($bytes.Length -lt 12 -or
    [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'RGBA') {
    throw "Not a Fallout4SecondScreen RGBA asset"
}
$width = [BitConverter]::ToUInt32($bytes, 4)
$height = [BitConverter]::ToUInt32($bytes, 8)
$pixelBytes = [uint64]$width * [uint64]$height * 4
if (-not $width -or -not $height -or $width -gt 2048 -or $height -gt 2048 -or
    $pixelBytes -gt 16MB -or $bytes.Length -ne 12 + $pixelBytes) {
    throw "Unsafe RGBA dimensions or payload"
}
$bitmap = [Drawing.Bitmap]::new([int]$width, [int]$height,
    [Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    $rectangle = [Drawing.Rectangle]::new(0, 0, [int]$width, [int]$height)
    $data = $bitmap.LockBits($rectangle, [Drawing.Imaging.ImageLockMode]::WriteOnly,
                             [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bgra = New-Object byte[] ([int]$pixelBytes)
        for ($at = 0; $at -lt $pixelBytes; $at += 4) {
            $bgra[$at] = $bytes[12 + $at + 2]
            $bgra[$at + 1] = $bytes[12 + $at + 1]
            $bgra[$at + 2] = $bytes[12 + $at]
            $bgra[$at + 3] = $bytes[12 + $at + 3]
        }
        [Runtime.InteropServices.Marshal]::Copy($bgra, 0, $data.Scan0, $bgra.Length)
    }
    finally {
        $bitmap.UnlockBits($data)
    }
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $bitmap.Save([IO.Path]::GetFullPath($OutputPath), [Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $bitmap.Dispose()
}

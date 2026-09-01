[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $InputPath,
    [Parameter(Mandatory = $true)][string] $OutputDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$source = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $InputPath).Path)
try {
    [IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
    $names = @("character", "inventory", "map", "magic", "quests")
    for ($index = 0; $index -lt $names.Count; ++$index) {
        $left = [int][Math]::Floor($source.Width * $index / $names.Count)
        $right = [int][Math]::Floor($source.Width * ($index + 1) / $names.Count)
        $cellWidth = [Math]::Max(1, $right - $left)
        $target = New-Object System.Drawing.Bitmap 128, 128,
            ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($target)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                # Preserve the complete equal cell and add a small edge inset;
                # the generated sheet already centers every glyph vertically.
                $graphics.DrawImage(
                    $source,
                    (New-Object System.Drawing.Rectangle 4, 4, 120, 120),
                    (New-Object System.Drawing.Rectangle $left, 0, $cellWidth, $source.Height),
                    [System.Drawing.GraphicsUnit]::Pixel
                )
            } finally {
                $graphics.Dispose()
            }
            $destination = Join-Path $OutputDirectory ($names[$index] + ".png")
            $target.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $target.Dispose()
        }
    }
} finally {
    $source.Dispose()
}

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $InterfaceBa2,
    [Parameter(Mandatory = $true)][string] $OutputDirectory,
    [string] $Java = "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot\bin\java.exe",
    [string] $FfdecJar = "C:\Users\Shadow\AppData\Local\DeusExSecondScreen\Tools\FFDec\26.2.1\ffdec\ffdec.jar",
    [switch] $Replace
)

$ErrorActionPreference = "Stop"
$maximumFiles = 100000
$maximumPayloadBytes = 64MB

if (-not (Test-Path -LiteralPath $Java -PathType Leaf)) { throw "Java was not found at $Java" }
if (-not (Test-Path -LiteralPath $FfdecJar -PathType Leaf)) { throw "FFDec was not found at $FfdecJar" }
$archivePath = (Resolve-Path -LiteralPath $InterfaceBa2).Path
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$outputParent = Split-Path -Parent $outputPath
if (-not $outputParent) { throw "OutputDirectory must have a parent directory" }
New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$reusablePerkRoot = $null
$existingManifest = Join-Path $outputPath 'manifest.ini'
if ($Replace -and (Test-Path -LiteralPath $existingManifest -PathType Leaf)) {
    $oldManifest = [IO.File]::ReadAllText($existingManifest) -replace "`r", ""
    if ($oldManifest -match '(?m)^Format=Fallout4SecondScreenAssetsV1$' -and
        $oldManifest -match "(?m)^InterfaceBa2Sha256=$archiveHash$") {
        $candidate = Join-Path $outputPath 'perks'
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $reusablePerkRoot = $candidate
        }
    }
}

Add-Type -AssemblyName System.Drawing
$rasterSource = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class Fallout4UiRasterTools {
    private static Bitmap LoadArgb(string path) {
        using (var source = new Bitmap(path)) {
            var copy = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
            using (var graphics = Graphics.FromImage(copy)) {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.DrawImageUnscaled(source, 0, 0);
            }
            return copy;
        }
    }

    private static byte[] Bytes(Bitmap bitmap, out int stride) {
        var rectangle = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
        var data = bitmap.LockBits(rectangle, ImageLockMode.ReadOnly,
                                   PixelFormat.Format32bppArgb);
        try {
            stride = Math.Abs(data.Stride);
            var bytes = new byte[stride * bitmap.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            return bytes;
        } finally {
            bitmap.UnlockBits(data);
        }
    }

    public static long AlphaCoverage(string path) {
        using (var bitmap = LoadArgb(path)) {
            int stride;
            var bytes = Bytes(bitmap, out stride);
            long count = 0;
            for (int y = 0; y < bitmap.Height; ++y) {
                int row = y * stride;
                for (int x = 0; x < bitmap.Width; ++x) {
                    int at = row + x * 4;
                    if (bytes[at + 3] != 0 &&
                        (bytes[at] != 0 || bytes[at + 1] != 0 || bytes[at + 2] != 0))
                        ++count;
                }
            }
            return count;
        }
    }

    public static void WriteRgba(string input, string output, bool trim) {
        using (var bitmap = LoadArgb(input)) {
            int stride;
            var bytes = Bytes(bitmap, out stride);
            int left = 0, top = 0, right = bitmap.Width - 1, bottom = bitmap.Height - 1;
            if (trim) {
                left = bitmap.Width; top = bitmap.Height; right = -1; bottom = -1;
                for (int y = 0; y < bitmap.Height; ++y) {
                    int row = y * stride;
                    for (int x = 0; x < bitmap.Width; ++x) {
                        int at = row + x * 4;
                        if (bytes[at + 3] == 0 ||
                            (bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 0))
                            continue;
                        left = Math.Min(left, x); top = Math.Min(top, y);
                        right = Math.Max(right, x); bottom = Math.Max(bottom, y);
                    }
                }
                if (right < left || bottom < top) throw new InvalidDataException("Raster frame is blank");
                int padding = Math.Max(4, Math.Max(right - left + 1, bottom - top + 1) / 30);
                left = Math.Max(0, left - padding); top = Math.Max(0, top - padding);
                right = Math.Min(bitmap.Width - 1, right + padding);
                bottom = Math.Min(bitmap.Height - 1, bottom + padding);
            }
            int width = right - left + 1;
            int height = bottom - top + 1;
            Directory.CreateDirectory(Path.GetDirectoryName(output));
            using (var stream = new FileStream(output, FileMode.Create, FileAccess.Write, FileShare.None))
            using (var writer = new BinaryWriter(stream)) {
                writer.Write(new byte[] { (byte)'R', (byte)'G', (byte)'B', (byte)'A' });
                writer.Write((UInt32)width); writer.Write((UInt32)height);
                for (int y = top; y <= bottom; ++y) {
                    int row = y * stride;
                    for (int x = left; x <= right; ++x) {
                        int at = row + x * 4;
                        writer.Write(bytes[at + 2]);
                        writer.Write(bytes[at + 1]);
                        writer.Write(bytes[at]);
                        writer.Write(bytes[at + 3]);
                    }
                }
            }
        }
    }

    public static void WriteHeadLayer(string input, string output) {
        using (var head = LoadArgb(input))
        using (var canvas = new Bitmap(210, 290, PixelFormat.Format32bppArgb)) {
            using (var graphics = Graphics.FromImage(canvas)) {
                graphics.Clear(Color.Transparent);
                graphics.CompositingMode = CompositingMode.SourceOver;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                // Pipboy_StatsPage's frame-one Head_mc matrix. The patched
                // head SWF maps root (0,0) to source pixel (100,100), while
                // the widened body maps root (0,0) to canvas pixel (100,130).
                float a = 0.998016f, b = -0.0487366f;
                float c = 0.0487366f, d = 0.998016f;
                float tx = -19.75f, ty = -27.75f;
                float dx = 100.0f + tx - a * 100.0f - c * 100.0f;
                float dy = 130.0f + ty - b * 100.0f - d * 100.0f;
                using (var matrix = new Matrix(a, b, c, d, dx, dy)) {
                    graphics.Transform = matrix;
                    graphics.DrawImageUnscaled(head, 0, 0);
                }
            }
            string temporary = output + ".png";
            canvas.Save(temporary, ImageFormat.Png);
            try { WriteRgba(temporary, output, false); }
            finally { File.Delete(temporary); }
        }
    }

    public static UInt64 Fnv1a64(string value) {
        UInt64 hash = 14695981039346656037UL;
        foreach (char character in value) {
            if (character > 0x7f) throw new InvalidDataException("Non-ASCII archive path");
            hash ^= (byte)character;
            unchecked { hash *= 1099511628211UL; }
        }
        return hash;
    }
}
'@
Add-Type -TypeDefinition $rasterSource -ReferencedAssemblies @('System.Drawing.dll')

function Read-ExactBytes {
    param([IO.BinaryReader] $Reader, [int] $Count)
    $bytes = $Reader.ReadBytes($Count)
    if ($bytes.Length -ne $Count) { throw "Unexpected end of Interface.ba2" }
    return $bytes
}

function Invoke-Ffdec {
    param([string[]] $ArgumentList)
    $lines = @(& $Java -jar $FfdecJar @ArgumentList 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "FFDec failed: $($lines | Select-Object -Last 12 | Out-String)"
    }
}

function Canonical-Path {
    param([string] $Value)
    $result = $Value.Replace('/', '\').TrimStart('\').ToLowerInvariant()
    if (-not $result.StartsWith('interface\')) { $result = 'interface\' + $result }
    return $result
}

function Select-BestFrame {
    param([string] $Directory)
    $frames = @(Get-ChildItem -LiteralPath $Directory -Filter '*.png' -File -Recurse)
    if ($frames.Count -eq 0) { throw "FFDec produced no frames in $Directory" }
    $midpoint = ($frames.Count + 1) / 2.0
    $ranked = foreach ($frame in $frames) {
        $number = 0
        $null = [int]::TryParse($frame.BaseName, [ref]$number)
        [pscustomobject]@{
            File = $frame
            Coverage = [Fallout4UiRasterTools]::AlphaCoverage($frame.FullName)
            Distance = [Math]::Abs($number - $midpoint)
        }
    }
    return ($ranked | Sort-Object @{Expression='Coverage';Descending=$true},
                                  @{Expression='Distance';Descending=$false} |
            Select-Object -First 1).File.FullName
}

$stream = [IO.File]::Open($archivePath, [IO.FileMode]::Open,
    [IO.FileAccess]::Read, [IO.FileShare]::Read)
$reader = [IO.BinaryReader]::new($stream, [Text.Encoding]::UTF8, $true)
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("Fallout4SecondScreen-assets-" + [Guid]::NewGuid().ToString('N'))
$staging = Join-Path $outputParent `
    ('.Fallout4SecondScreen-assets-staging-' + [Guid]::NewGuid().ToString('N'))

try {
    $magic = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $reader 4))
    $version = $reader.ReadUInt32()
    $type = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $reader 4))
    $fileCount = $reader.ReadUInt32()
    $nameTableOffset = $reader.ReadUInt64()
    if ($magic -ne 'BTDX' -or $version -ne 8 -or $type -ne 'GNRL' -or
        $fileCount -eq 0 -or $fileCount -gt $maximumFiles -or
        $nameTableOffset -ge [uint64]$stream.Length) {
        throw "Unsupported or unsafe Fallout 4 Interface.ba2"
    }
    $entries = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $fileCount; ++$index) {
        $null = $reader.ReadUInt32(); $null = Read-ExactBytes $reader 4
        $null = $reader.ReadUInt32(); $null = $reader.ReadUInt32()
        $offset = $reader.ReadUInt64(); $packed = $reader.ReadUInt32()
        $unpacked = $reader.ReadUInt32(); $null = $reader.ReadUInt32()
        $entries.Add([pscustomobject]@{Offset=$offset;Packed=$packed;Unpacked=$unpacked;Name=''})
    }
    $stream.Position = [int64]$nameTableOffset
    foreach ($entry in $entries) {
        $length = $reader.ReadUInt16()
        if ($length -eq 0 -or $length -gt 4096) { throw "Unsafe BA2 filename length" }
        $entry.Name = [Text.Encoding]::UTF8.GetString((Read-ExactBytes $reader $length)).Replace('/','\')
    }
    $byName = @{}
    foreach ($entry in $entries) { $byName[$entry.Name.ToLowerInvariant()] = $entry }

    New-Item -ItemType Directory -Path $temporaryRoot,$staging -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $staging 'special'),
        (Join-Path $staging 'condition'),(Join-Path $staging 'perks') -Force | Out-Null

    function Extract-Entry {
        param([object] $Entry, [string] $Destination)
        if (-not $Entry -or $Entry.Packed -ne 0 -or $Entry.Unpacked -eq 0 -or
            $Entry.Unpacked -gt $maximumPayloadBytes -or
            $Entry.Offset + $Entry.Unpacked -gt [uint64]$stream.Length) {
            throw "Unsafe or compressed UI asset record"
        }
        $stream.Position = [int64]$Entry.Offset
        [IO.File]::WriteAllBytes($Destination,
            (Read-ExactBytes $reader ([int]$Entry.Unpacked)))
    }

    function Render-Thumbnail {
        param([object] $Entry, [string] $Destination)
        $key = [Fallout4UiRasterTools]::Fnv1a64((Canonical-Path $Entry.Name)).ToString('x16')
        $swf = Join-Path $temporaryRoot ($key + '.swf')
        $render = Join-Path $temporaryRoot ('render-' + $key)
        Extract-Entry $Entry $swf
        Invoke-Ffdec @('-onerror','abort','-select','1-','-format','frame:png',
                       '-ignorebackground','-export','frame',$render,$swf)
        $best = Select-BestFrame $render
        [Fallout4UiRasterTools]::WriteRgba($best, $Destination, $true)
        Remove-Item -LiteralPath $render -Recurse -Force
        Remove-Item -LiteralPath $swf -Force
    }

    $specialNames = @('Strength','Perception','Endurance','Charisma',
                      'Intelligence','Agility','Luck')
    for ($index = 0; $index -lt $specialNames.Count; ++$index) {
        $path = "interface\components\vaultboys\special\$($specialNames[$index]).swf".ToLowerInvariant()
        if (-not $byName.ContainsKey($path)) { throw "Missing SPECIAL asset: $path" }
        Write-Progress -Activity 'Preparing Fallout 4 Pip-Boy artwork' -Status $specialNames[$index]
        Render-Thumbnail $byName[$path] (Join-Path $staging "special\$index.rgba")
    }

    for ($body = 0; $body -le 16; ++$body) {
        $path = "interface\components\conditionclips\condition_body_$body.swf"
        if (-not $byName.ContainsKey($path)) { throw "Missing condition body: $path" }
        $source = Join-Path $temporaryRoot "condition-body-$body.swf"
        $wide = Join-Path $temporaryRoot "condition-body-$body-wide.swf"
        $render = Join-Path $temporaryRoot "condition-body-$body-render"
        Extract-Entry $byName[$path] $source
        Invoke-Ffdec @('-header','-set','displayrect','[-2000,-2600,2200,3200]',
                       $source,$wide)
        Invoke-Ffdec @('-onerror','abort','-select','1','-format','frame:png',
                       '-ignorebackground','-export','frame',$render,$wide)
        $frame = @(Get-ChildItem -LiteralPath $render -Filter '*.png' -File -Recurse)[0]
        [Fallout4UiRasterTools]::WriteRgba($frame.FullName,
            (Join-Path $staging "condition\body_$body.rgba"), $false)
    }

    $headPath = 'interface\components\conditionclips\condition_head.swf'
    if (-not $byName.ContainsKey($headPath)) { throw "Missing condition head" }
    $headSource = Join-Path $temporaryRoot 'condition-head.swf'
    $headWide = Join-Path $temporaryRoot 'condition-head-wide.swf'
    $headRender = Join-Path $temporaryRoot 'condition-head-render'
    Extract-Entry $byName[$headPath] $headSource
    Invoke-Ffdec @('-header','-set','displayrect','[-2000,-2000,2000,2000]',
                   $headSource,$headWide)
    Invoke-Ffdec @('-onerror','abort','-select','1-','-format','frame:png',
                   '-ignorebackground','-export','frame',$headRender,$headWide)
    $headFrames = @(Get-ChildItem -LiteralPath $headRender -Filter '*.png' -File -Recurse |
                    Sort-Object {[int]$_.BaseName})
    if ($headFrames.Count -ne 32) { throw "Expected 32 condition head frames" }
    for ($head = 0; $head -lt $headFrames.Count; ++$head) {
        [Fallout4UiRasterTools]::WriteHeadLayer($headFrames[$head].FullName,
            (Join-Path $staging "condition\head_$head.rgba"))
    }

    # `SWFFile` is not confined to VaultBoys\Perks. Magazine perks use the
    # sibling `Components\Magazine perks` directory (including filenames with
    # spaces), and vanilla loads those exact paths. Hash every known perk-art
    # source so a live SWFFile never selects an asset the installer omitted.
    $perkEntries = @($entries | Where-Object {
        $_.Name -match '(?i)^Interface\\Components\\(?:VaultBoys\\Perks|Magazine perks)\\.+\.swf$'
    })
    if ($perkEntries.Count -lt 100) { throw "Unexpectedly few Fallout 4 perk assets" }
    $done = 0
    foreach ($entry in $perkEntries) {
        ++$done
        Write-Progress -Activity 'Preparing Fallout 4 perk artwork' `
            -Status "$done / $($perkEntries.Count)" `
            -PercentComplete ([int](100 * $done / $perkEntries.Count))
        $canonical = Canonical-Path $entry.Name
        $key = [Fallout4UiRasterTools]::Fnv1a64($canonical).ToString('x16')
        $destination = Join-Path $staging "perks\$key.rgba"
        $reusable = if ($reusablePerkRoot) {
            Join-Path $reusablePerkRoot "$key.rgba"
        } else { $null }
        if ($reusable -and (Test-Path -LiteralPath $reusable -PathType Leaf)) {
            Copy-Item -LiteralPath $reusable -Destination $destination
        } else {
            Render-Thumbnail $entry $destination
        }
    }
    Write-Progress -Activity 'Preparing Fallout 4 perk artwork' -Completed

    $manifest = @(
        'Format=Fallout4SecondScreenAssetsV1'
        "InterfaceBa2Sha256=$archiveHash"
        "SpecialCount=$($specialNames.Count)"
        'ConditionBodyCount=17'
        'ConditionHeadCount=32'
        "PerkCount=$($perkEntries.Count)"
    ) -join "`n"
    [IO.File]::WriteAllText((Join-Path $staging 'manifest.ini'), "$manifest`n",
                            [Text.UTF8Encoding]::new($false))

    if (Test-Path -LiteralPath $outputPath) {
        if (-not $Replace) { throw "OutputDirectory already exists; use -Replace" }
        $backup = "$outputPath.backup-$(Get-Date -Format 'yyyyMMdd-HHmmssfff')"
        Move-Item -LiteralPath $outputPath -Destination $backup
        Write-Host "Previous generated cache moved to $backup"
    }
    Move-Item -LiteralPath $staging -Destination $outputPath
    Write-Host "Prepared user-owned Fallout 4 UI artwork cache: $outputPath"
}
finally {
    $reader.Dispose()
    $stream.Dispose()
    $safeTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ((Test-Path -LiteralPath $temporaryRoot) -and
        $temporaryRoot.StartsWith($safeTempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($temporaryRoot).StartsWith('Fallout4SecondScreen-assets-')) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
    if ((Test-Path -LiteralPath $staging) -and
        [IO.Path]::GetDirectoryName($staging) -eq [IO.Path]::GetFullPath($outputParent) -and
        [IO.Path]::GetFileName($staging).StartsWith('.Fallout4SecondScreen-assets-staging-')) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

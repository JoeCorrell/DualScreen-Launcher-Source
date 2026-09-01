[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Archive,
    [Parameter(Mandatory = $true)][string] $OutputDirectory,
    [Parameter(Mandatory = $true)][string[]] $Path
)

$ErrorActionPreference = "Stop"
$maximumFiles = 100000
$maximumNameBytes = 4096
$maximumPayloadBytes = 64MB

function Read-ExactBytes {
    param(
        [Parameter(Mandatory = $true)][IO.BinaryReader] $Reader,
        [Parameter(Mandatory = $true)][int] $Count
    )
    $bytes = $Reader.ReadBytes($Count)
    if ($bytes.Length -ne $Count) { throw "Unexpected end of BA2 archive" }
    return $bytes
}

function Normalize-ArchivePath {
    param([Parameter(Mandatory = $true)][string] $Value)
    $normalized = $Value.Replace('/', '\').TrimStart('\')
    if (-not $normalized -or $normalized.Contains('..') -or
        [IO.Path]::IsPathRooted($normalized)) {
        throw "Unsafe BA2 path: $Value"
    }
    return $normalized
}

$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$requested = @{}
foreach ($item in $Path) {
    $normalized = Normalize-ArchivePath $item
    $requested[$normalized.ToLowerInvariant()] = $normalized
}
if ($requested.Count -eq 0) { throw "At least one BA2 path is required" }

$stream = [IO.File]::Open($archivePath, [IO.FileMode]::Open,
    [IO.FileAccess]::Read, [IO.FileShare]::Read)
$reader = [IO.BinaryReader]::new($stream, [Text.Encoding]::UTF8, $true)
try {
    $magic = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $reader 4))
    $version = $reader.ReadUInt32()
    $type = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $reader 4))
    $fileCount = $reader.ReadUInt32()
    $nameTableOffset = $reader.ReadUInt64()
    if ($magic -ne 'BTDX' -or $type -ne 'GNRL') {
        throw "Only Bethesda GNRL BA2 archives are supported"
    }
    if ($version -lt 1 -or $version -gt 8 -or
        $fileCount -eq 0 -or $fileCount -gt $maximumFiles) {
        throw "Unsafe or unsupported BA2 header"
    }
    if ($nameTableOffset -ge [uint64]$stream.Length) {
        throw "BA2 name table is outside the archive"
    }

    $records = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $fileCount; ++$index) {
        $null = $reader.ReadUInt32()                 # name hash
        $null = Read-ExactBytes $reader 4            # extension
        $null = $reader.ReadUInt32()                 # directory hash
        $null = $reader.ReadUInt32()                 # flags
        $offset = $reader.ReadUInt64()
        $packed = $reader.ReadUInt32()
        $unpacked = $reader.ReadUInt32()
        $null = $reader.ReadUInt32()                 # alignment
        $records.Add([pscustomobject]@{
            Offset = $offset
            Packed = $packed
            Unpacked = $unpacked
            Name = ''
        })
    }

    $stream.Position = [int64]$nameTableOffset
    foreach ($record in $records) {
        $length = $reader.ReadUInt16()
        if ($length -eq 0 -or $length -gt $maximumNameBytes) {
            throw "Unsafe BA2 filename length"
        }
        $record.Name = [Text.Encoding]::UTF8.GetString(
            (Read-ExactBytes $reader $length)).Replace('/', '\').TrimStart('\')
    }

    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $found = @{}
    foreach ($record in $records) {
        $key = $record.Name.ToLowerInvariant()
        if (-not $requested.ContainsKey($key)) { continue }
        if ($record.Packed -ne 0) {
            throw "Compressed BA2 entry is not supported: $($record.Name)"
        }
        if ($record.Unpacked -eq 0 -or $record.Unpacked -gt $maximumPayloadBytes -or
            $record.Offset + $record.Unpacked -gt [uint64]$stream.Length) {
            throw "Unsafe BA2 payload range: $($record.Name)"
        }
        $stream.Position = [int64]$record.Offset
        $payload = Read-ExactBytes $reader ([int]$record.Unpacked)
        $relative = Normalize-ArchivePath $record.Name
        $destination = [IO.Path]::GetFullPath((Join-Path $outputRoot $relative))
        if (-not $destination.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar,
                                        [StringComparison]::OrdinalIgnoreCase)) {
            throw "BA2 output escaped the target directory: $relative"
        }
        $parent = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        [IO.File]::WriteAllBytes($destination, $payload)
        $found[$key] = $destination
        Write-Host "Extracted $relative"
    }

    foreach ($entry in $requested.GetEnumerator()) {
        if (-not $found.ContainsKey($entry.Key)) {
            throw "BA2 entry was not found: $($entry.Value)"
        }
    }
}
finally {
    $reader.Dispose()
    $stream.Dispose()
}

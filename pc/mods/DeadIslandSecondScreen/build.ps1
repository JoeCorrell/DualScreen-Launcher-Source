# Builds the second-screen proxy.
#
#   .\build.ps1
#   .\build.ps1 -Game "C:\Games\Dead Island Definitive Edition" -Install
#   .\build.ps1 -Clean
#   .\build.ps1 -ValidateOnly
#
# Invoke this script from PowerShell. It calls the native MinGW-w64 toolchain
# directly, avoiding the nested MSYS runtime failure mode that can otherwise
# look like a source error with no compiler diagnostics.

[CmdletBinding()]
param(
    [string] $Mingw32 = "C:\msys64\mingw64\bin",
    [string] $Game,
    [switch] $Install,
    [switch] $Clean,
    [switch] $Symbols,
    [switch] $ValidateOnly
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$src = Join-Path $root "src"
$out = Join-Path $root "build"
$target = Join-Path $out "dxgi.dll"

$expectedExports = @(
    "CreateDXGIFactory",
    "CreateDXGIFactory1",
    "CreateDXGIFactory2",
    "DXGID3D10CreateDevice",
    "DXGID3D10CreateLayeredDevice",
    "DXGID3D10GetLayeredDeviceSize",
    "DXGID3D10RegisterLayers",
    "DXGIDeclareAdapterRemovalSupport",
    "DXGIGetDebugInterface1"
)

function Read-U16([byte[]] $data, [long] $offset, [string] $what) {
    if ($offset -lt 0 -or $offset + 2 -gt $data.Length) {
        throw "$what is outside the file"
    }
    return [BitConverter]::ToUInt16($data, [int] $offset)
}

function Read-U32([byte[]] $data, [long] $offset, [string] $what) {
    if ($offset -lt 0 -or $offset + 4 -gt $data.Length) {
        throw "$what is outside the file"
    }
    return [BitConverter]::ToUInt32($data, [int] $offset)
}

function Read-AsciiZ([byte[]] $data, [long] $offset, [string] $what) {
    if ($offset -lt 0 -or $offset -ge $data.Length) {
        throw "$what is outside the file"
    }
    $end = $offset
    while ($end -lt $data.Length -and $data[$end] -ne 0) { $end++ }
    if ($end -ge $data.Length) { throw "$what is not null-terminated" }
    return [Text.Encoding]::ASCII.GetString($data, [int] $offset, [int] ($end - $offset))
}

function Convert-RvaToOffset(
    [uint32] $rva,
    [object[]] $sections,
    [uint32] $sizeOfHeaders,
    [long] $fileLength,
    [string] $what
) {
    if ($rva -lt $sizeOfHeaders) {
        if ($rva -ge $fileLength) { throw "$what points outside the file" }
        return [long] $rva
    }
    foreach ($section in $sections) {
        $span = [Math]::Max([uint64] $section.VirtualSize, [uint64] $section.RawSize)
        if ([uint64] $rva -ge $section.VirtualAddress -and
            [uint64] $rva -lt ([uint64] $section.VirtualAddress + $span)) {
            $delta = [uint64] $rva - $section.VirtualAddress
            if ($delta -ge $section.RawSize) { throw "$what points into unbacked section data" }
            $offset = [uint64] $section.RawAddress + $delta
            if ($offset -ge [uint64] $fileLength) { throw "$what points outside the file" }
            return [long] $offset
        }
    }
    throw ("{0} RVA 0x{1:x} is not in a section" -f $what, $rva)
}

function Test-ProxyBinary([string] $path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "no DLL to validate at $path"
    }

    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "output has no valid DOS/PE header"
    }

    $peOffset = Read-U32 $bytes 0x3C "PE header offset"
    if ((Read-U32 $bytes $peOffset "PE signature") -ne 0x00004550) {
        throw "output has no valid PE signature"
    }

    $machine = Read-U16 $bytes ($peOffset + 4) "machine"
    $numSections = Read-U16 $bytes ($peOffset + 6) "section count"
    $optSize = Read-U16 $bytes ($peOffset + 20) "optional-header size"
    $characteristics = Read-U16 $bytes ($peOffset + 22) "file characteristics"
    if ($machine -ne 0x8664) {
        throw ("built for machine 0x{0:x} - the game needs 0x8664 (64-bit x86_64)" -f $machine)
    }
    # IMAGE_FILE_DLL must be set. IMAGE_FILE_32BIT_MACHINE (0x0100) must NOT be:
    # it is a 32-bit-only flag, and requiring it here - as the 32-bit build
    # script correctly does - would reject every valid x64 DLL.
    if (($characteristics -band 0x2000) -eq 0) {
        throw ("output characteristics 0x{0:x} do not describe a DLL" -f $characteristics)
    }
    if (($characteristics -band 0x0100) -ne 0) {
        throw ("output characteristics 0x{0:x} claim a 32-bit machine" -f $characteristics)
    }
    if ($numSections -lt 1 -or $numSections -gt 96) {
        throw "implausible PE section count: $numSections"
    }

    $optHeader = $peOffset + 24
    if ($optSize -lt 112 -or $optHeader + $optSize -gt $bytes.Length) {
        throw "invalid PE optional header"
    }
    $magic = Read-U16 $bytes $optHeader "optional-header magic"
    # PE32+ (0x20B), not PE32 (0x10B). The x64 optional header is larger, which
    # is also why the data-directory offset below is 112 rather than 96.
    if ($magic -ne 0x20B) {
        throw ("output is not PE32+ (optional-header magic 0x{0:x})" -f $magic)
    }
    $dllCharacteristics = Read-U16 $bytes ($optHeader + 70) "DLL characteristics"
    if (($dllCharacteristics -band 0x40) -eq 0 -or ($dllCharacteristics -band 0x100) -eq 0) {
        throw ("output DLL characteristics 0x{0:x} lack ASLR or NX compatibility" -f $dllCharacteristics)
    }
    # SizeOfHeaders (+60) and DllCharacteristics (+70) sit at the same offset in
    # both PE32 and PE32+. NumberOfRvaAndSizes and the data directory do not:
    # ImageBase and the four stack/heap fields widen to 8 bytes on x64, pushing
    # the directory from +96 to +112 and its count from +92 to +108.
    $sizeOfHeaders = Read-U32 $bytes ($optHeader + 60) "header size"
    $directoryCount = Read-U32 $bytes ($optHeader + 108) "data-directory count"
    if ($directoryCount -lt 2) { throw "PE has no export/import data directories" }

    $sectionTable = $optHeader + $optSize
    if ($sectionTable + ([long] $numSections * 40) -gt $bytes.Length) {
        throw "PE section table is truncated"
    }
    $sections = @()
    for ($i = 0; $i -lt $numSections; $i++) {
        $s = $sectionTable + ($i * 40)
        $rawSize = Read-U32 $bytes ($s + 16) "section raw size"
        $rawAddress = Read-U32 $bytes ($s + 20) "section raw address"
        if ($rawSize -gt 0 -and [uint64] $rawAddress + $rawSize -gt [uint64] $bytes.Length) {
            throw "PE section data is truncated"
        }
        $sections += [pscustomobject]@{
            VirtualSize    = Read-U32 $bytes ($s + 8) "section virtual size"
            VirtualAddress = Read-U32 $bytes ($s + 12) "section virtual address"
            RawSize        = $rawSize
            RawAddress     = $rawAddress
        }
    }

    $exportRva = Read-U32 $bytes ($optHeader + 112) "export directory RVA"
    $exportSize = Read-U32 $bytes ($optHeader + 116) "export directory size"
    if ($exportRva -eq 0 -or $exportSize -lt 40) { throw "PE has no valid export directory" }
    $e = Convert-RvaToOffset $exportRva $sections $sizeOfHeaders $bytes.Length "export directory"
    if ($e + 40 -gt $bytes.Length) { throw "export directory is truncated" }

    $ordinalBase = Read-U32 $bytes ($e + 16) "export ordinal base"
    $functionCount = Read-U32 $bytes ($e + 20) "export function count"
    $nameCount = Read-U32 $bytes ($e + 24) "export name count"
    if ($ordinalBase -ne 1) { throw "expected export ordinal base 1, found $ordinalBase" }
    if ($functionCount -ne $expectedExports.Count -or $nameCount -ne $expectedExports.Count) {
        throw "expected $($expectedExports.Count) named exports, found $nameCount names and $functionCount functions"
    }
    $functionTable = Convert-RvaToOffset (Read-U32 $bytes ($e + 28) "export address-table RVA") $sections $sizeOfHeaders $bytes.Length "export address table"
    $nameTable = Convert-RvaToOffset (Read-U32 $bytes ($e + 32) "export name-table RVA") $sections $sizeOfHeaders $bytes.Length "export name table"
    $ordinalTable = Convert-RvaToOffset (Read-U32 $bytes ($e + 36) "export ordinal-table RVA") $sections $sizeOfHeaders $bytes.Length "export ordinal table"
    if ($functionTable + ([long] $functionCount * 4) -gt $bytes.Length -or
        $nameTable + ([long] $nameCount * 4) -gt $bytes.Length -or
        $ordinalTable + ([long] $nameCount * 2) -gt $bytes.Length) {
        throw "export tables are truncated"
    }

    $exports = @()
    $seenOrdinals = @{}
    for ($i = 0; $i -lt $nameCount; $i++) {
        $nameRva = Read-U32 $bytes ($nameTable + ($i * 4)) "export-name RVA"
        $nameOffset = Convert-RvaToOffset $nameRva $sections $sizeOfHeaders $bytes.Length "export name"
        $name = Read-AsciiZ $bytes $nameOffset "export name"
        $ordinalIndex = Read-U16 $bytes ($ordinalTable + ($i * 2)) "export ordinal"
        if ($ordinalIndex -ge $functionCount) { throw "export $name has an invalid ordinal" }
        if ($seenOrdinals.ContainsKey($ordinalIndex)) { throw "two names share export ordinal $ordinalIndex" }
        $seenOrdinals[$ordinalIndex] = $true
        $functionRva = Read-U32 $bytes ($functionTable + ($ordinalIndex * 4)) "export function RVA"
        if ($functionRva -eq 0) { throw "export $name has a null function RVA" }
        if ([uint64] $functionRva -ge $exportRva -and
            [uint64] $functionRva -lt ([uint64] $exportRva + $exportSize)) {
            throw "export $name is unexpectedly forwarded"
        }
        $exports += $name
    }
    $exports = @($exports | Sort-Object)
    $missing = @($expectedExports | Where-Object { $exports -notcontains $_ })
    $unexpected = @($exports | Where-Object { $expectedExports -notcontains $_ })
    if ($missing.Count -or $unexpected.Count) {
        throw "export mismatch; missing=[$($missing -join ', ')] unexpected=[$($unexpected -join ', ')]"
    }

    $importRva = Read-U32 $bytes ($optHeader + 120) "import directory RVA"
    $importSize = Read-U32 $bytes ($optHeader + 124) "import directory size"
    if ($importRva -eq 0 -or $importSize -lt 20) { throw "PE has no valid import directory" }
    $importTable = Convert-RvaToOffset $importRva $sections $sizeOfHeaders $bytes.Length "import directory"
    $imports = @()
    $importTerminated = $false
    for ($i = 0; $i -lt 256; $i++) {
        $descriptor = $importTable + ($i * 20)
        if ($descriptor + 20 -gt $bytes.Length) { throw "import directory is truncated" }
        $originalThunk = Read-U32 $bytes $descriptor "import thunk"
        $timeStamp = Read-U32 $bytes ($descriptor + 4) "import timestamp"
        $forwarder = Read-U32 $bytes ($descriptor + 8) "import forwarder"
        $nameRva = Read-U32 $bytes ($descriptor + 12) "import-name RVA"
        $firstThunk = Read-U32 $bytes ($descriptor + 16) "import address thunk"
        if (($originalThunk -bor $timeStamp -bor $forwarder -bor $nameRva -bor $firstThunk) -eq 0) {
            $importTerminated = $true
            break
        }
        if ($nameRva -eq 0) { throw "import descriptor has no DLL name" }
        $nameOffset = Convert-RvaToOffset $nameRva $sections $sizeOfHeaders $bytes.Length "import DLL name"
        $imports += Read-AsciiZ $bytes $nameOffset "import DLL name"
    }
    if (-not $importTerminated) { throw "import directory has no terminator" }
    if ($imports.Count -eq 0) { throw "PE import directory has no entries" }
    $imports = @($imports | Sort-Object -Unique)
    $dynamicRuntimes = @($imports | Where-Object {
        $_ -match '^(libgcc_s.*|libstdc\+\+-6|libwinpthread-1)\.dll$'
    })
    if ($dynamicRuntimes.Count) {
        throw "output is not self-contained; imports $($dynamicRuntimes -join ', ')"
    }

    Write-Host "machine   : 0x8664 (PE32+ x64 DLL, ASLR, NX)"
    Write-Host ("exports   : " + ($exports -join ", "))
    Write-Host ("imports   : " + ($imports -join ", "))
}

if ($Clean) {
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    Write-Host "cleaned"
    return
}

if ($ValidateOnly) {
    Test-ProxyBinary $target
    return
}

$gxx = Join-Path $Mingw32 "g++.exe"
$windres = Join-Path $Mingw32 "windres.exe"
if (-not (Test-Path $gxx)) {
    throw "No 32-bit toolchain at $Mingw32. Install it with: pacman -S mingw-w64-x86_64-gcc"
}

# g++.exe runs from anywhere, but the backend it spawns - cc1plus - is linked
# against DLLs beside it. Without this the compiler exits 1 with nothing on
# stderr.
if ($env:PATH -notlike "*$Mingw32*") { $env:PATH = "$Mingw32;$env:PATH" }

Write-Host ("toolchain : " + (& $gxx --version | Select-Object -First 1))
Write-Host "target    : 64-bit dxgi.dll proxy"

if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }

# -m64 is not negotiable: the game is a 64-bit process and will refuse anything
# else, silently. Static libgcc/libstdc++/winpthread keep the DLL self-contained
# - nothing in the game folder or in the Wine prefix WEMU builds supplies them.
$cxxflags = @(
    "-m64", "-std=c++17", "-O2",
    "-fno-exceptions", "-fno-rtti",
    "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-cast-function-type",
    "-DUNICODE", "-D_UNICODE", "-DWIN32_LEAN_AND_MEAN",
    "-D_WIN32_WINNT=0x0601"
)
if ($Symbols) { $cxxflags += @("-g", "-DDXS_DEBUG") }

$ldflags = @(
    "-m64", "-shared", "-static", "-static-libgcc", "-static-libstdc++",
    "-Wl,--kill-at", "-Wl,--enable-stdcall-fixup"
)
$libs = @("-ld3d11", "-ldxgi", "-lgdi32", "-luser32", "-lole32", "-luuid", "-lws2_32")

$sources = Get-ChildItem (Join-Path $src "*.cpp") | Sort-Object Name
$objects = @()
$failed = $false
foreach ($file in $sources) {
    $obj = Join-Path $out ($file.BaseName + ".o")
    $objects += $obj
    Write-Host ("  cc  " + $file.Name)
    & $gxx @cxxflags -c $file.FullName -o $obj
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}
if ($failed) { throw "compilation failed" }

$resourceSource = Join-Path $src "Assets.rc"
if (Test-Path -LiteralPath $resourceSource) {
    if (-not (Test-Path -LiteralPath $windres)) {
        throw "missing MinGW resource compiler at $windres"
    }
    $resourceObject = Join-Path $out "Assets.resource.o"
    Write-Host "  rc  Assets.rc"
    & $windres -I $src -i $resourceSource -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw "resource compilation failed" }
    $objects += $resourceObject
}

Write-Host "  link dxgi.dll"
& $gxx @ldflags -o $target @objects (Join-Path $src "dxgi.def") @libs
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Write-Host ""
Write-Host ("built $target (" + [math]::Round((Get-Item $target).Length / 1KB, 1) + " KB)")

Test-ProxyBinary $target

if ($Install) {
    if (-not $Game) { throw "pass -Game with the folder holding DeadIslandGame.exe" }
    if (-not (Test-Path -LiteralPath (Join-Path $Game "DeadIslandGame.exe") -PathType Leaf)) {
        throw "no DeadIslandGame.exe in $Game"
    }

    $destination = Join-Path $Game "dxgi.dll"
    $iniSource = Join-Path $root "DeadIslandSecondScreen.ini"
    $iniDestination = Join-Path $Game "DeadIslandSecondScreen.ini"
    if (-not (Test-Path -LiteralPath $iniSource -PathType Leaf)) {
        throw "missing $iniSource"
    }
    if (Test-Path -LiteralPath $destination -PathType Container) {
        throw "$destination is a directory, not a DLL"
    }
    if (Test-Path -LiteralPath $iniDestination -PathType Container) {
        throw "$iniDestination is a directory, not an INI file"
    }

    $sourceHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($destinationHash -eq $sourceHash) {
            Write-Host "dxgi.dll : already current"
        } else {
            $stamp = Get-Date -Format "yyyyMMdd-HHmmssfff"
            $backup = Join-Path $Game "dxgi.before-DeadIslandSecondScreen.$stamp.dll"
            $suffix = 1
            while (Test-Path -LiteralPath $backup) {
                $backup = Join-Path $Game "dxgi.before-DeadIslandSecondScreen.$stamp-$suffix.dll"
                $suffix++
            }
            Copy-Item -LiteralPath $destination -Destination $backup
            $backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
            if ($backupHash -ne $destinationHash) {
                throw "backup verification failed at $backup; existing dxgi.dll was not replaced"
            }
            Write-Host "backup    : $backup"
            Copy-Item -LiteralPath $target -Destination $destination -Force
        }
    } else {
        Copy-Item -LiteralPath $target -Destination $destination
    }
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "installed dxgi.dll failed verification"
    }

    if (Test-Path -LiteralPath $iniDestination) {
        Write-Host "ini       : preserved existing DeadIslandSecondScreen.ini"
    } else {
        Copy-Item -LiteralPath $iniSource -Destination $iniDestination
        Write-Host "ini       : installed DeadIslandSecondScreen.ini"
    }
    Write-Host "installed to $Game"
}

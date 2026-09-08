param(
    [string]$Compiler = 'C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build/payload')
)
# Full-payload build recipe, first compiled for the explicitly requested WEMU
# installation on 2026-09-06. This does not build or install an APK.
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'The i386 MinGW compiler is unavailable' }
$sources = @(
    'Probe.cpp', 'Runtime.cpp', 'FaultCapture.cpp', 'GameMemory.cpp',
    'StateAdapter.cpp', 'StateAdapterActions.cpp', 'StateAdapterDocuments.cpp',
    'StateAdapterMaps.cpp', 'Definitions.cpp', 'Panel.cpp', 'Ui.cpp', 'Touch.cpp', 'UpperViewport.cpp'
) | ForEach-Object { Join-Path $PSScriptRoot "src/$_" }
$sources += Join-Path $PSScriptRoot '../DeadIslandSecondScreen/src/Canvas.cpp'
foreach ($source in $sources) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing source: $source" }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = Join-Path $OutputDirectory 'dinput8.dll'
$temporary = Join-Path $OutputDirectory 'dinput8.pending.dll'
# Fingerprint the complete local source inputs, including generated geometry
# tables and the shared canvas. Paths are relative so moving the checkout does
# not change the identity. The header lives in the build directory only.
$identityFiles = @($sources) + @(
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File |
        Where-Object { $_.Extension -in @('.h', '.inc', '.def') } |
        ForEach-Object { $_.FullName }
) + @((Join-Path $PSScriptRoot '../DeadIslandSecondScreen/src/Canvas.h'), $PSCommandPath)
$identityRoot = [Uri]((Resolve-Path -LiteralPath $PSScriptRoot).Path + '/')
$identityInputs = @($identityFiles | Sort-Object -Unique | ForEach-Object {
    $resolved = (Resolve-Path -LiteralPath $_).Path
    [ordered]@{ name = $identityRoot.MakeRelativeUri([Uri]$resolved).ToString(); sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant() }
} | Sort-Object { $_.name })
$identityText = ($identityInputs | ForEach-Object { $_.name + ':' + $_.sha256 }) -join "`n"
$hasher = [Security.Cryptography.SHA256]::Create()
try { $buildIdentity = ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($identityText)))).Replace('-', '').ToLowerInvariant() }
finally { $hasher.Dispose() }
$identityHeader = Join-Path $OutputDirectory 'BuildIdentity.generated.h'
[IO.File]::WriteAllText($identityHeader, ('#define DISHONORED_BUILD_ID "' + $buildIdentity + '"' + "`n"), (New-Object Text.UTF8Encoding($false)))
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $previousPath
    $arguments = @('-std=c++17', '-m32', '-O2', '-Wall', '-Wextra', '-Wpedantic',
        '-Werror', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DDISHONORED_FULL_PAYLOAD',
        '-D_WIN32_WINNT=0x0601', '-finput-charset=UTF-8', '-shared', '-static',
        '-static-libgcc', '-static-libstdc++', '-include', $identityHeader) + $sources + @(
        (Join-Path $PSScriptRoot 'src/dinput8.def'), '-lbcrypt', '-lws2_32',
        '-lgdiplus', '-lgdi32', '-luser32', '-Wl,--no-insert-timestamp', '-o', $temporary)
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Dishonored full-payload compilation failed' }
    $objdump = Join-Path (Split-Path -Parent $Compiler) 'objdump.exe'
    $imports = & $objdump -p $temporary
    if ($LASTEXITCODE -ne 0 -or ($imports -join "`n") -match 'DLL Name: (libgcc|libstdc|libwinpthread)') {
        throw 'Unexpected native runtime dependencies'
    }
    if (-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($temporary)).Contains('[build] sourceSha256=' + $buildIdentity)) {
        throw 'Compiled payload is missing its source fingerprint'
    }
    Move-Item -LiteralPath $temporary -Destination $output -Force
    [ordered]@{ sourceSha256=$buildIdentity; dllSha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant(); inputs=$identityInputs } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build-identity.json') -Encoding UTF8
    Get-FileHash -Algorithm SHA256 -LiteralPath $output
    Write-Output 'Full-payload binary built locally. No assets staged, package published, APK rebuilt or device changed.'
} finally {
    $env:PATH = $previousPath
}

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceRoot = Join-Path $projectRoot 'src'
$buildRoot = Join-Path $projectRoot ('build\' + $Configuration.ToLowerInvariant())
$packageRoot = Join-Path $projectRoot 'package'
$compiler = 'C:\msys64\mingw32\bin\i686-w64-mingw32-g++.exe'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "32-bit MinGW compiler not found: $compiler"
}
# GCC launches cc1plus from its libexec directory. Put the MinGW DLL directory
# on this build process's search path so that child can resolve its toolchain
# runtime; this does not alter the user's persistent environment.
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
if ($Configuration -eq 'Release') {
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
}

$sources = @(
    (Join-Path $sourceRoot 'Proxy.cpp'),
    (Join-Path $sourceRoot 'Runtime.cpp'),
    (Join-Path $sourceRoot 'CombatLagFix.cpp'),
    (Join-Path $sourceRoot 'Engine.cpp'),
    (Join-Path $sourceRoot 'Version.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'),
    (Join-Path $sourceRoot 'Perf.cpp'),
    (Join-Path $sourceRoot 'LocalMapFrame.cpp'),
    (Join-Path $sourceRoot 'LocalMapCapture.cpp'),
    (Join-Path $sourceRoot 'VanillaUiAssets.cpp'),
    (Join-Path $sourceRoot 'Panel.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S')
)
$output = Join-Path $buildRoot 'dinput8.dll'
$compilerArguments = @(
    '-std=c++17', '-m32', '-shared', '-municode',
    '-Wall', '-Wextra', '-Wpedantic', '-Werror',
    '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
    '-static', '-static-libgcc', '-static-libstdc++',
    '-Wl,--kill-at', '-Wl,--enable-stdcall-fixup',
    '-o', $output
)
if ($Configuration -eq 'Release') {
    $compilerArguments += @('-O2', '-DNDEBUG')
} else {
    $compilerArguments += @('-O0', '-g3')
}
$compilerArguments += $sources
$compilerArguments += (Join-Path $sourceRoot 'dinput8.def')
$compilerArguments += @('-lws2_32', '-lgdi32', '-luser32', '-ladvapi32',
                        '-lole32', '-luuid', '-lmsimg32', '-lz')

& $compiler @compilerArguments
if ($LASTEXITCODE -ne 0) { throw "Compiler failed with exit code $LASTEXITCODE" }

$objdump = 'C:\msys64\mingw32\bin\objdump.exe'
if (-not (Test-Path -LiteralPath $objdump)) {
    $objdump = 'C:\msys64\mingw32\bin\i686-w64-mingw32-objdump.exe'
}
$headers = & $objdump -f $output
if ($LASTEXITCODE -ne 0 -or ($headers -join "`n") -notmatch 'pei-i386') {
    throw 'Output is not a 32-bit PE DLL.'
}
$exports = & $objdump -p $output
foreach ($name in @('DirectInput8Create', 'DllCanUnloadNow', 'DllGetClassObject',
                    'DllRegisterServer', 'DllUnregisterServer')) {
    if (($exports -join "`n") -notmatch [regex]::Escape($name)) {
        throw "Missing required dinput8 export: $name"
    }
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash.ToLowerInvariant()
if ($Configuration -eq 'Release') {
    Copy-Item -LiteralPath $output -Destination (Join-Path $packageRoot 'dinput8.dll') -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Fallout3SecondScreen.ini') `
              -Destination (Join-Path $packageRoot 'Fallout3SecondScreen.ini') -Force
    foreach ($profile in @('Fallout3Performance.ini',
                            'Fallout3PrefsPerformance.ini', 'dxvk.conf')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot $profile) `
                  -Destination (Join-Path $packageRoot $profile) -Force
    }
    $manifest = [ordered]@{
        configuration = 'Release'
        sha256 = $hash
        size = (Get-Item -LiteralPath $output).Length
        builtUtc = [DateTime]::UtcNow.ToString('o')
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $packageRoot 'manifest.json') -Encoding UTF8
}
Write-Host "Built $output"
Write-Host "SHA256 $hash"

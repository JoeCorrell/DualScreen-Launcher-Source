param(
    [string] $Mingw = 'C:\msys64\mingw64\bin',
    [string] $DependencyRoot = '',
    [string[]] $HeadNifs = @(),
    [switch] $Build
)
$ErrorActionPreference = 'Stop'
$modRoot = Split-Path -Parent $PSScriptRoot
$workspace = Split-Path (Split-Path (Split-Path $modRoot -Parent) -Parent) -Parent
if (-not $DependencyRoot) { $DependencyRoot = Join-Path $workspace 'artifacts' }
$DependencyRoot = (Resolve-Path -LiteralPath $DependencyRoot).Path
$nifly = Join-Path $DependencyRoot 'nifly'
$output = Join-Path $modRoot 'build\tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$env:PATH = "$Mingw;$env:PATH"
if ($Build) { & (Join-Path $modRoot 'build.ps1') -Mingw $Mingw -DependencyRoot $DependencyRoot }
$testExe = Join-Path $output 'PreviewRenderTests.exe'
& "$Mingw\g++.exe" -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter `
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static `
    "-I$nifly/include" "-I$nifly/external" `
    (Join-Path $PSScriptRoot 'PreviewRenderTests.cpp') `
    (Join-Path $nifly 'build-mingw\src\libnifly.a') `
    -o $testExe -ld3d11 -ldxgi -ld3dcompiler_47 -luser32
if ($LASTEXITCODE -ne 0) { throw 'Renderer test build failed' }
& $testExe $output @HeadNifs
if ($LASTEXITCODE -ne 0) { throw 'Renderer regressions failed' }
Push-Location $modRoot
try {
    foreach ($mode in @('loading','status','inventory','magic','quests','map-disabled')) {
        & .\build\ui-preview.exe $mode
        if ($LASTEXITCODE -ne 0) { throw "UI regression failed: $mode ($LASTEXITCODE)" }
        Write-Output "UI $mode PASS"
    }
} finally { Pop-Location }

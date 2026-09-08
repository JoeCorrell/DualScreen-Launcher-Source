param(
    [Parameter(Mandatory=$true)][string]$Assets,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Compiler = 'C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $previousPath
    $common = @('-std=c++17', '-m32', '-O2', '-Wall', '-Wextra', '-Werror', '-static')
    $source = Join-Path $PSScriptRoot '../src'
    $cases = @(
        @{ Name='map-objectives'; Sources=@("$PSScriptRoot/MapObjectivesTests.cpp"); Options=@() },
        @{ Name='map-calibration'; Sources=@("$PSScriptRoot/MapCalibrationTests.cpp"); Options=@() },
        @{ Name='journal'; Sources=@("$PSScriptRoot/JournalIntegrationTests.cpp"); Options=@() },
        @{ Name='presentation'; Sources=@("$PSScriptRoot/PanelPresentationTests.cpp", "$PSScriptRoot/../../DeadIslandSecondScreen/src/Canvas.cpp"); Options=@('-lgdi32', '-luser32') },
        @{ Name='memory'; Sources=@("$PSScriptRoot/MemorySampleCacheTests.cpp", "$source/GameMemory.cpp"); Options=@() },
        @{ Name='types'; Sources=@("$PSScriptRoot/TypeSampleCacheTests.cpp", "$source/GameMemory.cpp"); Options=@() },
        @{ Name='viewport'; Sources=@("$PSScriptRoot/UpperViewportTests.cpp"); Options=@('-luser32') },
        @{ Name='touch'; Sources=@("$PSScriptRoot/TouchProtocol.cpp", "$source/Touch.cpp"); Options=@('-lws2_32') },
        @{ Name='ui'; Sources=@("$PSScriptRoot/UiRenderTests.cpp", "$source/Ui.cpp", "$PSScriptRoot/../../DeadIslandSecondScreen/src/Canvas.cpp"); Options=@('-municode', '-lgdiplus', '-lgdi32', '-luser32') }
    )
    foreach ($case in $cases) {
        $executable = Join-Path $OutputDirectory ($case.Name + '-tests.exe')
        & $Compiler @common @($case.Sources) @($case.Options) -o $executable
        if ($LASTEXITCODE -ne 0) { throw "$($case.Name) test compilation failed" }
        if ($case.Name -eq 'ui') { & $executable $Assets (Join-Path $OutputDirectory 'renders') }
        else { & $executable }
        if ($LASTEXITCODE -ne 0) { throw "$($case.Name) tests failed" }
    }
} finally { $env:PATH = $previousPath }

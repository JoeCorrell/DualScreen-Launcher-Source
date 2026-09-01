[CmdletBinding()]
param(
    [string] $Mingw = "C:\msys64\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "build\f4se"
$cxx = Join-Path $Mingw "g++.exe"
$objdump = Join-Path $Mingw "objdump.exe"
if (-not (Test-Path -LiteralPath $cxx)) {
    throw "MinGW-w64 was not found at $Mingw"
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:PATH = "$Mingw;$env:PATH"

Push-Location $root
try {
    $compilerArgs = @(
        "-std=c++17", "-O2", "-DNDEBUG", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
        "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wframe-larger-than=65536",
        "-static", "-static-libgcc",
        "-static-libstdc++", "-shared", "-o", "build\f4se\Fallout4SecondScreenF4SE.dll",
        "f4se\Plugin.cpp",
        "-Isrc", "-Wl,--dynamicbase", "-Wl,--nxcompat",
        "-Wl,--no-insert-timestamp", "-lkernel32"
    )
    & $cxx @compilerArgs
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge build failed" }

    $info = (& $objdump -p "build\f4se\Fallout4SecondScreenF4SE.dll") -join "`n"
    if ($info -notmatch "pei-x86-64") { throw "F4SE bridge is not x64" }
    foreach ($export in @("F4SEPlugin_Load", "F4SEPlugin_Version", "FO4DS_GetBridgeV1")) {
        if ($info -notmatch [regex]::Escape($export)) {
            throw "F4SE bridge is missing export $export"
        }
    }
    & $cxx -std=c++17 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN -DNOMINMAX `
        -Wall -Wextra -Wpedantic -Werror -static -static-libgcc -static-libstdc++ `
        -o "build\f4se\F4SEBridgeHostTests.exe" "tests\F4SEBridgeHostTests.cpp" `
        -lkernel32
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge host-test build failed" }
    & "build\f4se\F4SEBridgeHostTests.exe" `
        "build\f4se\Fallout4SecondScreenF4SE.dll"
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge host tests failed" }

    Write-Host "Built exact-runtime F4SE bridge: $out\Fallout4SecondScreenF4SE.dll"
}
finally {
    Pop-Location
}

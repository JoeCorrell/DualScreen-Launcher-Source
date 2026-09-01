[CmdletBinding()]
param(
    [string] $Mingw = "C:\msys64\mingw64\bin",
    [switch] $TestsOnly,
    [switch] $Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "build"
if ($Clean) {
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    return
}
if (-not (Test-Path -LiteralPath "$Mingw\g++.exe")) {
    throw "MinGW-w64 was not found at $Mingw"
}
New-Item -ItemType Directory -Force -Path "$out\tests" | Out-Null
$env:PATH = "$Mingw;$env:PATH"
$cxx = "$Mingw\g++.exe"
$common = @(
    "-std=c++17", "-O2", "-DNDEBUG", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
    "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wframe-larger-than=65536", "-static",
    "-static-libgcc", "-static-libstdc++", "-Isrc"
)

Push-Location $root
try {
    & $cxx @common -UNDEBUG -o "build\tests\ProtocolTests.exe" `
        "tests\ProtocolTests.cpp" "src\PipboyProtocol.cpp" "src\Log.cpp" `
        -lws2_32
    if ($LASTEXITCODE -ne 0) { throw "ProtocolTests build failed" }
    & "build\tests\ProtocolTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "ProtocolTests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\NativePipboyBridgeTests.exe" `
        "tests\NativePipboyBridgeTests.cpp" `
        "src\PipboyProtocol.cpp" "src\Log.cpp" -lws2_32
    if ($LASTEXITCODE -ne 0) { throw "Native Pip-Boy bridge tests build failed" }
    & "build\tests\NativePipboyBridgeTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Native Pip-Boy bridge tests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\NativeActionsTests.exe" `
        "tests\NativeActionsTests.cpp" "src\NativeActions.cpp"
    if ($LASTEXITCODE -ne 0) { throw "Native action tests build failed" }
    & "build\tests\NativeActionsTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Native action tests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\ProxyAbiTests.exe" `
        "tests\ProxyAbiTests.cpp"
    if ($LASTEXITCODE -ne 0) { throw "DXGI proxy ABI tests build failed" }
    & "build\tests\ProxyAbiTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "DXGI proxy ABI tests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\PanelInputTests.exe" `
        "tests\PanelInputTests.cpp" "src\PanelInput.cpp"
    if ($LASTEXITCODE -ne 0) { throw "PanelInputTests build failed" }
    & "build\tests\PanelInputTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "PanelInputTests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\Fallout4UiAssetsTests.exe" `
        "tests\Fallout4UiAssetsTests.cpp" "src\Fallout4UiAssets.cpp" `
        "src\Log.cpp" -lgdi32 -luser32 -lmsimg32
    if ($LASTEXITCODE -ne 0) { throw "Fallout 4 UI asset tests build failed" }
    & "build\tests\Fallout4UiAssetsTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Fallout 4 UI asset tests failed" }

    & $cxx @common -UNDEBUG -o "build\tests\UiAdapterTests.exe" `
        "tests\UiAdapterTests.cpp" "src\UiAdapter.cpp" `
        "src\PipboyProtocol.cpp" "src\NativeActions.cpp" "src\Log.cpp" `
        -lws2_32
    if ($LASTEXITCODE -ne 0) { throw "UiAdapterTests build failed" }
    & "build\tests\UiAdapterTests.exe"
    if ($LASTEXITCODE -ne 0) { throw "UiAdapterTests failed" }

    & $cxx @common -shared -o "build\tests\F4SEBridgeTestProvider.dll" `
        "tests\F4SEBridgeTestProvider.cpp" "-Wl,--dynamicbase" "-Wl,--nxcompat"
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge test-provider build failed" }
    & $cxx @common -UNDEBUG -o "build\tests\F4SEBridgeClientTests.exe" `
        "tests\F4SEBridgeClientTests.cpp" "src\F4SEBridgeClient.cpp" `
        "src\Log.cpp" -lkernel32
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge client-test build failed" }
    & "build\tests\F4SEBridgeClientTests.exe" `
        "build\tests\F4SEBridgeTestProvider.dll"
    if ($LASTEXITCODE -ne 0) { throw "F4SE bridge client tests failed" }
    if ($TestsOnly) { return }

    New-Item -ItemType Directory -Force -Path "build\tools" | Out-Null
    & $cxx @common -municode -mwindows "-Wl,--stack,1048576" `
        -o "build\tools\ProxyRuntimeProbe.exe" `
        "tests\ProxyRuntimeProbe.cpp" -lkernel32 -lole32
    if ($LASTEXITCODE -ne 0) { throw "Proxy runtime probe build failed" }

    # Keep the host-side artifact under a neutral filename. Windows Defender
    # heuristically quarantines freshly linked, unsigned files named dxgi.dll
    # before objdump can validate them. The launcher manifest still installs
    # this exact PE as game-local dxgi.dll on Android.
    $proxyOutput = "build\dxgi.candidate.dll"
    $proxyArgs = $common + @(
        "-shared", "-o", $proxyOutput,
        "src\Proxy.cpp", "src\Mod.cpp", "src\Fallout3Ui.cpp",
        "src\Fallout4UiAssets.cpp",
        "src\PanelInput.cpp", "src\UiAdapter.cpp", "src\UiPerf.cpp",
        "src\PipboyProtocol.cpp", "src\NativePipboyBridge.cpp",
        "src\NativeActions.cpp", "src\Runtime.cpp", "src\Config.cpp",
        "src\Log.cpp",
        "src\dxgi.def", "-Wl,--dynamicbase", "-Wl,--nxcompat",
        "-Wl,--no-insert-timestamp",
        "-lws2_32", "-lgdi32", "-luser32", "-lmsimg32", "-lbcrypt", "-lversion"
    )
    & $cxx @proxyArgs
    if ($LASTEXITCODE -ne 0) { throw "DXGI proxy build failed" }

    $objdump = "$Mingw\objdump.exe"
    $info = (& $objdump -p $proxyOutput) -join "`n"
    if ($info -notmatch "pei-x86-64") { throw "DXGI proxy is not x64" }
    foreach ($export in @(
        "CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2",
        "DXGID3D10CreateDevice", "DXGID3D10CreateLayeredDevice",
        "DXGID3D10GetLayeredDeviceSize", "DXGID3D10RegisterLayers",
        "DXGIDeclareAdapterRemovalSupport", "DXGIDisableVBlankVirtualization",
        "DXGIGetDebugInterface1", "DXGIReportAdapterConfiguration"
    )) {
        if ($info -notmatch [regex]::Escape($export)) {
            throw "dxgi.dll is missing export $export"
        }
    }
    $targetOrdinals = [ordered]@{
        CreateDXGIFactory = 9
        CreateDXGIFactory1 = 10
        CreateDXGIFactory2 = 11
        DXGIDeclareAdapterRemovalSupport = 16
        DXGIGetDebugInterface1 = 17
    }
    foreach ($entry in $targetOrdinals.GetEnumerator()) {
        $pattern = "(?m)\+base\[\s*$($entry.Value)\].*$([regex]::Escape($entry.Key))\s*$"
        if ($info -notmatch $pattern) {
            throw "dxgi.dll target ordinal mismatch: $($entry.Key) must be $($entry.Value)"
        }
    }

    $probeDirectory = Join-Path $out "proxy-probe"
    if (Test-Path -LiteralPath $probeDirectory) {
        Remove-Item -LiteralPath $probeDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $probeDirectory | Out-Null
    Copy-Item -LiteralPath "build\tools\ProxyRuntimeProbe.exe" `
        -Destination "$probeDirectory\ProxyRuntimeProbe.exe"
    Copy-Item -LiteralPath $proxyOutput `
        -Destination "$probeDirectory\dxgi.candidate.dll"
    $probeProcess = Start-Process `
        -FilePath "$probeDirectory\ProxyRuntimeProbe.exe" `
        -Wait -PassThru -WindowStyle Hidden
    if ($probeProcess.ExitCode -ne 0) {
        throw "DXGI proxy runtime probe failed with exit code $($probeProcess.ExitCode)"
    }
    $probeReport = Join-Path $probeDirectory "Fallout4SecondScreenProbe.txt"
    if (-not (Test-Path -LiteralPath $probeReport -PathType Leaf)) {
        throw "DXGI proxy runtime probe did not produce a report"
    }
    $probeText = [IO.File]::ReadAllText($probeReport)
    foreach ($evidence in @("load=ok", "export=ok", "factory_call=end hr=0x00000000")) {
        if (-not $probeText.Contains($evidence)) {
            throw "DXGI proxy runtime probe is missing evidence: $evidence"
        }
    }
    $probeLog = Join-Path $probeDirectory "Fallout4SecondScreen.log"
    if (-not (Test-Path -LiteralPath $probeLog -PathType Leaf)) {
        throw "DXGI proxy runtime probe did not produce a proxy log"
    }
    $probeLogText = [IO.File]::ReadAllText($probeLog)
    foreach ($evidence in @(
        "[PROXY] system DXGI loaded",
        "[PROXY] CreateDXGIFactory exit hr=0x00000000",
        "[INIT] bootstrap thread entered",
        "[INIT] non-Fallout host; proxy remains transparent"
    )) {
        if (-not $probeLogText.Contains($evidence)) {
            throw "DXGI proxy runtime log is missing evidence: $evidence"
        }
    }

    $package = Join-Path $out "package"
    if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $package | Out-Null
    Copy-Item -LiteralPath $proxyOutput -Destination "$package\dxgi.candidate.dll"
    Copy-Item -LiteralPath "Fallout4SecondScreen.ini" -Destination "$package\Fallout4SecondScreen.ini"
    $packageDllHash = (Get-FileHash -LiteralPath "$package\dxgi.candidate.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
    $packageIniHash = (Get-FileHash -LiteralPath "$package\Fallout4SecondScreen.ini" -Algorithm SHA256).Hash.ToLowerInvariant()
    $ownerManifest = @(
        "Format=Fallout4SecondScreenOwnerV1"
        "DxgiSha256=$packageDllHash"
        "ConfigSha256=$packageIniHash"
    ) -join "`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $package "Fallout4SecondScreen.owner"),
        "$ownerManifest`n",
        [System.Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath "README.md" -Destination "$package\README.md"
    Copy-Item -LiteralPath "PROOF_MATRIX.md" -Destination "$package\PROOF_MATRIX.md"
    Write-Host "Built Fallout 4 second-screen diagnostic package: $package"
}
finally {
    Pop-Location
}

[CmdletBinding()]
param(
    [string] $OriginalExe,
    [string] $OriginalSdl,
    [string] $ExactAssetsDirectory,
    [string] $Mingw = 'C:\msys64\mingw64\bin'
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSCommandPath
$repository = Resolve-Path (Join-Path $project '..\..\..')
if ([string]::IsNullOrWhiteSpace($OriginalExe)) {
    $OriginalExe = Join-Path $repository '.work\mistria-recon\FieldsOfMistria.exe'
}
if ([string]::IsNullOrWhiteSpace($OriginalSdl)) {
    $OriginalSdl = Join-Path $repository '.work\mistria-recon\SDL3.dll'
}
if ([string]::IsNullOrWhiteSpace($ExactAssetsDirectory)) {
    $ExactAssetsDirectory = Join-Path $repository '.work\mistria-v104'
}
$OriginalExe = (Resolve-Path -LiteralPath $OriginalExe).Path
$OriginalSdl = (Resolve-Path -LiteralPath $OriginalSdl).Path
$cxx = Join-Path $Mingw 'g++.exe'
$objdump = Join-Path $Mingw 'objdump.exe'
$env:PATH = "$Mingw;$env:PATH"
foreach ($file in @($OriginalExe, $OriginalSdl, $cxx, $objdump)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing build input: $file" }
}

$expectedExe = 'aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e'
$expectedSdl = '532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157'
$exeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OriginalExe).Hash.ToLowerInvariant()
$sdlHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OriginalSdl).Hash.ToLowerInvariant()
if ($exeHash -ne $expectedExe) { throw "Unsupported FieldsOfMistria.exe SHA256 $exeHash" }
if ($sdlHash -ne $expectedSdl) { throw "Unsupported SDL3.dll SHA256 $sdlHash" }
if ((Get-Item -LiteralPath $OriginalExe).Length -ne 13657696) { throw 'Unexpected executable size' }
if ((Get-Item -LiteralPath $OriginalSdl).Length -ne 2811488) { throw 'Unexpected SDL size' }

$build = Join-Path $project 'build'
$generated = Join-Path $build 'generated'
$tests = Join-Path $build 'tests'
$runtime = Join-Path $build 'runtime-probe'
$package = Join-Path $build 'package'
New-Item -ItemType Directory -Force -Path $build, $generated, $tests | Out-Null

function Get-ExactExports([string] $Path) {
    $result = @()
    $seenNames = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    $seenOrdinals = [System.Collections.Generic.HashSet[int]]::new()
    $lines = & $objdump -p $Path
    if ($LASTEXITCODE -ne 0) { throw "objdump failed for $Path" }
    $pattern = '^\s*\[\s*\d+\]\s+\+base\[\s*(\d+)\]\s+[0-9a-fA-F]+\s+(\S+)\s*$'
    $inNameTable = $false
    foreach ($line in $lines) {
        if ($line -match '^\[Ordinal/Name Pointer\] Table') {
            $inNameTable = $true
            continue
        }
        if ($inNameTable -and $line -match '^The Function Table') { break }
        if (-not $inNameTable) { continue }
        $match = [regex]::Match($line, $pattern)
        if (-not $match.Success) { continue }
        $ordinal = [int]$match.Groups[1].Value
        $name = $match.Groups[2].Value
        if (-not $seenNames.Add($name)) { throw "Duplicate export name: $name" }
        if (-not $seenOrdinals.Add($ordinal)) { throw "Duplicate export ordinal: $ordinal" }
        $result += [pscustomobject]@{ Ordinal = $ordinal; Name = $name }
    }
    $result = @($result | Sort-Object Ordinal)
    if ($result.Count -ne 1271) { throw "Expected 1271 SDL exports, found $($result.Count)" }
    for ($index = 0; $index -lt $result.Count; $index++) {
        if ($result[$index].Ordinal -ne $index + 1) {
            throw "SDL export ordinal gap at index $index"
        }
    }
    return $result
}

$exports = @(Get-ExactExports $OriginalSdl)
$definition = @('LIBRARY SDL3.dll', 'EXPORTS')
foreach ($export in $exports) {
    $definition += "    $($export.Name)=SDL3_FieldsOfMistriaOriginal.$($export.Name) @$($export.Ordinal)"
}
$definitionPath = Join-Path $generated 'SDL3-forwarders.def'
$definition | Set-Content -LiteralPath $definitionPath -Encoding ASCII

$common = @(
    '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-DUNICODE', '-D_UNICODE',
    '-I', (Join-Path $project 'src'), '-I', (Join-Path $project 'tests')
)
function Invoke-Compile([string] $Output, [string[]] $Sources, [string[]] $Extra = @()) {
    & $cxx @common @Sources @Extra '-o' $Output
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Output" }
}

$compatibilityTest = Join-Path $tests 'CompatibilityTests.exe'
Invoke-Compile $compatibilityTest @(
    (Join-Path $project 'tests\CompatibilityTests.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp')
) @('-municode', '-lbcrypt', '-lversion')
& $compatibilityTest $OriginalExe $OriginalSdl
if ($LASTEXITCODE -ne 0) { throw 'CompatibilityTests failed' }

$hashTest = Join-Path $tests 'HashTests.exe'
Invoke-Compile $hashTest @(
    (Join-Path $project 'tests\HashTests.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp')
) @('-municode', '-lbcrypt', '-lversion')
& $hashTest $OriginalExe $OriginalSdl
if ($LASTEXITCODE -ne 0) { throw 'HashTests failed' }

$stateTest = Join-Path $tests 'StateStoreTests.exe'
Invoke-Compile $stateTest @(
    (Join-Path $project 'tests\StateStoreTests.cpp'),
    (Join-Path $project 'src\StateStore.cpp')
)
& $stateTest
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests failed' }

$liveStateTest = Join-Path $tests 'LiveStateTests.exe'
Invoke-Compile $liveStateTest @(
    (Join-Path $project 'tests\LiveStateTests.cpp'),
    (Join-Path $project 'src\LiveState.cpp'),
    (Join-Path $project 'src\Log.cpp')
)
& $liveStateTest
if ($LASTEXITCODE -ne 0) { throw 'LiveStateTests failed' }

$actionTransportTest = Join-Path $tests 'ActionTransportTests.exe'
Invoke-Compile $actionTransportTest @(
    (Join-Path $project 'tests\ActionTransportTests.cpp'),
    (Join-Path $project 'src\ActionTransport.cpp')
)
& $actionTransportTest
if ($LASTEXITCODE -ne 0) { throw 'ActionTransportTests failed' }

$assetProviderTest = Join-Path $tests 'InstalledAssetProviderTests.exe'
Invoke-Compile $assetProviderTest @(
    (Join-Path $project 'tests\InstalledAssetProviderTests.cpp'),
    (Join-Path $project 'src\InstalledAssetProvider.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp')
) @('-municode', '-DFOMDS_ASSET_PROVIDER_TESTING', '-lbcrypt', '-lversion')
$exactAssets = Join-Path $ExactAssetsDirectory 'assets.zip'
$exactAssetsExe = Join-Path $ExactAssetsDirectory 'FieldsOfMistria.exe'
if ((Test-Path -LiteralPath $exactAssets -PathType Leaf) -and
    (Test-Path -LiteralPath $exactAssetsExe -PathType Leaf)) {
    & $assetProviderTest $ExactAssetsDirectory
} else {
    & $assetProviderTest
}
if ($LASTEXITCODE -ne 0) { throw 'InstalledAssetProviderTests failed' }

$assetRendererTest = Join-Path $tests 'InstalledAssetRendererTests.exe'
Invoke-Compile $assetRendererTest @(
    (Join-Path $project 'tests\InstalledAssetRendererTests.cpp'),
    (Join-Path $project 'src\InstalledAssetRenderer.cpp'),
    (Join-Path $project 'src\InstalledAssetProvider.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp')
) @('-municode', '-DFOMDS_ASSET_PROVIDER_TESTING',
    '-DFOMDS_ASSET_RENDERER_TESTING', '-lbcrypt', '-lversion', '-lgdi32',
    '-lgdiplus', '-lole32')
if ((Test-Path -LiteralPath $exactAssets -PathType Leaf) -and
    (Test-Path -LiteralPath $exactAssetsExe -PathType Leaf)) {
    & $assetRendererTest $ExactAssetsDirectory
} else {
    & $assetRendererTest
}
if ($LASTEXITCODE -ne 0) { throw 'InstalledAssetRendererTests failed' }

$inputTest = Join-Path $tests 'PanelInputTests.exe'
Invoke-Compile $inputTest @(
    (Join-Path $project 'tests\PanelInputTests.cpp'),
    (Join-Path $project 'src\PanelInput.cpp')
)
& $inputTest
if ($LASTEXITCODE -ne 0) { throw 'PanelInputTests failed' }

$carouselTest = Join-Path $tests 'PageCarouselTests.exe'
Invoke-Compile $carouselTest @(
    (Join-Path $project 'tests\PageCarouselTests.cpp'),
    (Join-Path $project 'src\PageCarousel.cpp')
)
& $carouselTest
if ($LASTEXITCODE -ne 0) { throw 'PageCarouselTests failed' }

$lifecycleTest = Join-Path $tests 'PanelLifecycleTests.exe'
Invoke-Compile $lifecycleTest @(
    (Join-Path $project 'tests\PanelLifecycleTests.cpp')
)
& $lifecycleTest
if ($LASTEXITCODE -ne 0) { throw 'PanelLifecycleTests failed' }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $project 'deploy.ps1') -SelfTest
if ($LASTEXITCODE -ne 0) { throw 'Deploy self-test failed' }

$proxySources = @(
    (Join-Path $project 'src\Proxy.cpp'),
    (Join-Path $project 'src\Mod.cpp'),
    (Join-Path $project 'src\Panel.cpp'),
    (Join-Path $project 'src\PanelInput.cpp'),
    (Join-Path $project 'src\PageCarousel.cpp'),
    (Join-Path $project 'src\StateStore.cpp'),
    (Join-Path $project 'src\LiveState.cpp'),
    (Join-Path $project 'src\ActionTransport.cpp'),
    (Join-Path $project 'src\InstalledAssetProvider.cpp'),
    (Join-Path $project 'src\InstalledAssetRenderer.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp'),
    (Join-Path $project 'src\Config.cpp'),
    (Join-Path $project 'src\Log.cpp'),
    $definitionPath
)
$link = @(
    '-shared', '-static', '-static-libgcc', '-static-libstdc++',
    '-Wl,--no-insert-timestamp', '-Wl,--dynamicbase', '-Wl,--nxcompat',
    '-lbcrypt', '-lversion', '-lws2_32', '-lgdi32', '-lgdiplus', '-lole32',
    '-luser32', '-lkernel32'
)
$hostSources = @(
    (Join-Path $project 'src\PanelHostMain.cpp'),
    (Join-Path $project 'src\Panel.cpp'),
    (Join-Path $project 'src\PanelInput.cpp'),
    (Join-Path $project 'src\PageCarousel.cpp'),
    (Join-Path $project 'src\StateStore.cpp'),
    (Join-Path $project 'src\LiveState.cpp'),
    (Join-Path $project 'src\ActionTransport.cpp'),
    (Join-Path $project 'src\InstalledAssetProvider.cpp'),
    (Join-Path $project 'src\InstalledAssetRenderer.cpp'),
    (Join-Path $project 'src\BuildIdentity.cpp'),
    (Join-Path $project 'src\Config.cpp'),
    (Join-Path $project 'src\Log.cpp')
)
$hostLink = @(
    '-municode', '-mwindows', '-static', '-static-libgcc', '-static-libstdc++',
    '-Wl,--no-insert-timestamp', '-Wl,--dynamicbase', '-Wl,--nxcompat',
    '-lbcrypt', '-lversion', '-lws2_32', '-lgdi32', '-lgdiplus', '-lole32',
    '-lshell32', '-luser32', '-lkernel32'
)
$panelHost = Join-Path $build '.fomds-panel-host.exe'
Invoke-Compile $panelHost $hostSources $hostLink
$hostHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $panelHost).Hash.ToLowerInvariant()
$hostReference = Join-Path $build '.fomds-panel-host.deterministic-reference.exe'
Copy-Item -LiteralPath $panelHost -Destination $hostReference -Force
Invoke-Compile $panelHost $hostSources $hostLink
$deterministicHostHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $panelHost).Hash.ToLowerInvariant()
if ($hostHash -ne $deterministicHostHash) {
    throw "Non-deterministic panel host output: $hostHash != $deterministicHostHash"
}
$unexpectedHostRuntime = @(& $objdump -p $panelHost | Select-String -Pattern '^\s*DLL Name:' |
    ForEach-Object { ($_ -split ':', 2)[1].Trim().ToLowerInvariant() } |
    Where-Object { $_ -notin @('bcrypt.dll', 'gdi32.dll', 'gdiplus.dll',
        'kernel32.dll', 'msvcrt.dll', 'ole32.dll', 'shell32.dll', 'user32.dll',
        'version.dll', 'ws2_32.dll') })
if ($unexpectedHostRuntime.Count -ne 0) {
    throw "Unexpected non-system panel-host dependency: $($unexpectedHostRuntime -join ', ')"
}
$proxy = Join-Path $build 'SDL3.dll'
Invoke-Compile $proxy $proxySources $link
$proxyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash.ToLowerInvariant()
$deterministicReference = Join-Path $build 'SDL3.deterministic-reference.dll'
Copy-Item -LiteralPath $proxy -Destination $deterministicReference -Force
# Repeat the exact same compiler/linker invocation and output path. A different
# output filename changes PE metadata and is not a valid reproducibility test.
Invoke-Compile $proxy $proxySources $link
$deterministicHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash.ToLowerInvariant()
if ($proxyHash -ne $deterministicHash) {
    throw "Non-deterministic proxy output: $proxyHash != $deterministicHash"
}
$unexpectedRuntime = @(& $objdump -p $proxy | Select-String -Pattern '^\s*DLL Name:' |
    ForEach-Object { ($_ -split ':', 2)[1].Trim().ToLowerInvariant() } |
    Where-Object { $_ -notin @('bcrypt.dll', 'gdi32.dll', 'gdiplus.dll',
        'kernel32.dll', 'msvcrt.dll', 'ole32.dll', 'user32.dll', 'version.dll',
        'ws2_32.dll') })
if ($unexpectedRuntime.Count -ne 0) {
    throw "Unexpected non-system proxy runtime dependency: $($unexpectedRuntime -join ', ')"
}

$abiTest = Join-Path $tests 'ProxyAbiTests.exe'
Invoke-Compile $abiTest @((Join-Path $project 'tests\ProxyAbiTests.cpp')) @('-municode')
& $abiTest $OriginalSdl $proxy
if ($LASTEXITCODE -ne 0) { throw 'ProxyAbiTests failed' }

$runtimeProbe = Join-Path $tests 'ProxyRuntimeProbe.exe'
Invoke-Compile $runtimeProbe @((Join-Path $project 'tests\ProxyRuntimeProbe.cpp')) @('-municode')
if (Test-Path -LiteralPath $runtime) { Remove-Item -LiteralPath $runtime -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$runtimeProxy = Join-Path $runtime 'SDL3.dll'
$runtimeOriginal = Join-Path $runtime 'SDL3_FieldsOfMistriaOriginal.dll'
Copy-Item -LiteralPath $proxy -Destination $runtimeProxy
Copy-Item -LiteralPath $OriginalSdl -Destination $runtimeOriginal
Copy-Item -LiteralPath (Join-Path $project 'FieldsOfMistriaSecondScreen.ini') -Destination $runtime
& $runtimeProbe $runtimeOriginal $runtimeProxy
if ($LASTEXITCODE -ne 0) { throw 'ProxyRuntimeProbe failed' }

if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item -LiteralPath $proxy -Destination (Join-Path $package 'SDL3.dll')
Copy-Item -LiteralPath $panelHost -Destination (Join-Path $package '.fomds-panel-host.exe')
Copy-Item -LiteralPath (Join-Path $project 'FieldsOfMistriaSecondScreen.ini') -Destination $package
Copy-Item -LiteralPath (Join-Path $project 'FieldsOfMistriaSecondScreen.owner') -Destination $package
Copy-Item -LiteralPath (Join-Path $project 'README.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $project 'PROOF_MATRIX.md') -Destination $package
foreach ($privateName in @('FieldsOfMistria.exe', 'SDL3_FieldsOfMistriaOriginal.dll')) {
    if (Test-Path -LiteralPath (Join-Path $package $privateName)) {
        throw "Proprietary binary must not enter package: $privateName"
    }
}
$configHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package 'FieldsOfMistriaSecondScreen.ini')).Hash.ToLowerInvariant()
$ownerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package 'FieldsOfMistriaSecondScreen.owner')).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    schema = 1
    owner = 'FieldsOfMistriaSecondScreen:v1'
    targetExecutable = 'FieldsOfMistria.exe'
    targetVersion = '1.0.4'
    targetSha256 = $expectedExe
    originalSdlName = 'SDL3_FieldsOfMistriaOriginal.dll'
    originalSdlVersion = '3.4.8.0'
    originalSdlSha256 = $expectedSdl
    originalExportCount = 1271
    proxySha256 = $proxyHash
    panelHostName = '.fomds-panel-host.exe'
    panelHostSha256 = $hostHash
    panelProcess = 'isolated-helper'
    configSha256 = $configHash
    ownerSha256 = $ownerHash
    input = 'udp://127.0.0.1:7788'
    liveGameData = $true
    liveTopics = @(
        'session', 'status', 'progression', 'inventory', 'armor', 'hotbar',
        'location', 'player', 'map', 'villagers', 'animals', 'magic',
        'almanac', 'museum', 'renown', 'calendar_events', 'calendar', 'clock',
        'weather', 'quests'
    )
    actionsEnabled = @(
        'select_hotbar', 'swap_slots', 'equip_armor', 'unequip_armor',
        'consume_item', 'pin_spell', 'unpin_spell', 'cast_pinned_spell'
    )
    inputIsolation = 'loopback-only-noactivate-panel'
    installedAssetPolicy = 'verified-installed-archive-only'
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $package 'FieldsOfMistriaSecondScreen.manifest.json') -Encoding UTF8

Write-Host 'Fields of Mistria full static build gate passed.'
Write-Host "Exports : $($exports.Count) exact forwarders"
Write-Host "Proxy   : $proxyHash"
Write-Host "Host    : $hostHash"
Write-Host "Package : $package"

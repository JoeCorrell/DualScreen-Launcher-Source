param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceRoot = Join-Path $projectRoot 'src'
$buildRoot = Join-Path $projectRoot ('build\' + $Configuration.ToLowerInvariant())
$testRoot = Join-Path $projectRoot 'build\tests'
$packageRoot = Join-Path $projectRoot 'package'
$compiler = 'C:\msys64\mingw32\bin\i686-w64-mingw32-g++.exe'
$objdump = 'C:\msys64\mingw32\bin\objdump.exe'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "32-bit MinGW compiler not found: $compiler"
}
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
New-Item -ItemType Directory -Force -Path $buildRoot, $testRoot, $packageRoot | Out-Null

$common = @(
    '-std=c++17', '-m32', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
    '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-I', $sourceRoot,
    '-static', '-static-libgcc', '-static-libstdc++'
)
$optimization = if ($Configuration -eq 'Release') {
    @('-O2', '-DNDEBUG')
} else {
    @('-O0', '-g3')
}
$testOptimization = if ($Configuration -eq 'Release') { @('-O2') } else { @('-O0', '-g3') }

$panelTest = Join-Path $testRoot 'PanelInputTests.exe'
$panelArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\PanelInputTests.cpp'),
    (Join-Path $sourceRoot 'PanelInput.cpp'), '-o', $panelTest
)
& $compiler @panelArguments
if ($LASTEXITCODE -ne 0) { throw 'PanelInputTests compilation failed' }
& $panelTest
if ($LASTEXITCODE -ne 0) { throw 'PanelInputTests failed' }

$carouselTest = Join-Path $testRoot 'PageCarouselTests.exe'
$carouselArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\PageCarouselTests.cpp'),
    (Join-Path $sourceRoot 'PageCarousel.cpp'), '-o', $carouselTest
)
& $compiler @carouselArguments
if ($LASTEXITCODE -ne 0) { throw 'PageCarouselTests compilation failed' }
& $carouselTest
if ($LASTEXITCODE -ne 0) { throw 'PageCarouselTests failed' }

$liveStateTest = Join-Path $testRoot 'LiveStatePolicyTests.exe'
$liveStateArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\LiveStatePolicyTests.cpp'),
    '-o', $liveStateTest
)
& $compiler @liveStateArguments
if ($LASTEXITCODE -ne 0) { throw 'LiveStatePolicyTests compilation failed' }
& $liveStateTest
if ($LASTEXITCODE -ne 0) { throw 'LiveStatePolicyTests failed' }

$configTest = Join-Path $testRoot 'ConfigTests.exe'
$configArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\ConfigTests.cpp'),
    (Join-Path $sourceRoot 'Config.cpp'), '-o', $configTest
)
& $compiler @configArguments
if ($LASTEXITCODE -ne 0) { throw 'ConfigTests compilation failed' }
& $configTest
if ($LASTEXITCODE -ne 0) { throw 'ConfigTests failed' }

$panelStateTest = Join-Path $testRoot 'PanelStatePolicyTests.exe'
$panelStateArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\PanelStatePolicyTests.cpp'),
    '-o', $panelStateTest
)
& $compiler @panelStateArguments
if ($LASTEXITCODE -ne 0) { throw 'PanelStatePolicyTests compilation failed' }
& $panelStateTest
if ($LASTEXITCODE -ne 0) { throw 'PanelStatePolicyTests failed' }

$iatTest = Join-Path $testRoot 'IatHookTests.exe'
$iatArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\IatHookTests.cpp'),
    (Join-Path $sourceRoot 'IatHook.cpp'), '-o', $iatTest
)
& $compiler @iatArguments
if ($LASTEXITCODE -ne 0) { throw 'IatHookTests compilation failed' }
& $iatTest
if ($LASTEXITCODE -ne 0) { throw 'IatHookTests failed' }

$compatibilityTest = Join-Path $testRoot 'CompatibilityTests.exe'
$compatibilityArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\CompatibilityTests.cpp'),
    (Join-Path $sourceRoot 'Compatibility.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $compatibilityTest
)
& $compiler @compatibilityArguments
if ($LASTEXITCODE -ne 0) { throw 'CompatibilityTests compilation failed' }
& $compatibilityTest
if ($LASTEXITCODE -ne 0) { throw 'CompatibilityTests failed' }

$stateTest = Join-Path $testRoot 'StateStoreTests.exe'
$stateArguments = $common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\StateStoreTests.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'),
    (Join-Path $sourceRoot 'BuildIdentity.cpp'),
    '-o', $stateTest, '-lbcrypt'
)
& $compiler @stateArguments
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests compilation failed' }
& $stateTest
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests failed' }

$sources = @(
    (Join-Path $sourceRoot 'Proxy.cpp'),
    (Join-Path $sourceRoot 'Mod.cpp'),
    (Join-Path $sourceRoot 'Config.cpp'),
    (Join-Path $sourceRoot 'BuildIdentity.cpp'),
    (Join-Path $sourceRoot 'Compatibility.cpp'),
    (Join-Path $sourceRoot 'IatHook.cpp'),
    (Join-Path $sourceRoot 'GameApi.cpp'),
    (Join-Path $sourceRoot 'CharacterRenderer.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'),
    (Join-Path $sourceRoot 'PanelInput.cpp'),
    (Join-Path $sourceRoot 'PageCarousel.cpp'),
    (Join-Path $sourceRoot 'Panel.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S')
)
$output = Join-Path $buildRoot 'dxgi.dll'
$proxyArguments = $common + $optimization + @(
    '-shared', '-Wl,--kill-at', '-Wl,--enable-stdcall-fixup', '-o', $output
) + $sources + @(
    (Join-Path $sourceRoot 'dxgi.def'),
    '-lws2_32', '-lgdi32', '-luser32', '-lbcrypt'
)
& $compiler @proxyArguments
if ($LASTEXITCODE -ne 0) { throw 'Proxy DLL compilation failed' }

$fileHeader = & $objdump -f $output
if ($LASTEXITCODE -ne 0 -or ($fileHeader -join "`n") -notmatch 'pei-i386') {
    throw 'Output is not a 32-bit PE DLL.'
}
$pe = & $objdump -p $output
foreach ($name in @('CreateDXGIFactory', 'CreateDXGIFactory1',
                    'CreateDXGIFactory2', 'DXGID3D10RegisterLayers')) {
    if (($pe -join "`n") -notmatch [regex]::Escape($name)) {
        throw "Missing required DXGI export: $name"
    }
}
foreach ($runtime in @('libgcc_s', 'libstdc++', 'libwinpthread')) {
    if (($pe -join "`n") -match [regex]::Escape($runtime)) {
        throw "Unexpected dynamic MinGW dependency: $runtime"
    }
}

$proxyProbe = Join-Path $testRoot 'ProxyRuntimeProbe.exe'
$probeArguments = $common + $testOptimization + @(
    '-municode', (Join-Path $projectRoot 'tests\ProxyRuntimeProbe.cpp'),
    '-o', $proxyProbe, '-lole32'
)
& $compiler @probeArguments
if ($LASTEXITCODE -ne 0) { throw 'ProxyRuntimeProbe compilation failed' }
& $proxyProbe $output
if ($LASTEXITCODE -ne 0) { throw 'DXGI forwarding/fail-closed runtime probe failed' }

# If the ignored reconnaissance copies are present, bind the source catalog to
# the exact installed files on every build without packaging those files.
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\..'))
$reconRoot = Join-Path $workspaceRoot '.codex-temp\grim-dawn-recon\x86'
$reconExe = Join-Path $reconRoot 'Grim Dawn.exe'
$reconGame = Join-Path $reconRoot 'Game.dll'
$reconEngine = Join-Path $reconRoot 'Engine.dll'
$reconRenderer = Join-Path $reconRoot 'Direct3D11.dll'
if ((Test-Path -LiteralPath $reconExe) -and
    (Test-Path -LiteralPath $reconGame) -and
    (Test-Path -LiteralPath $reconEngine)) {
    $expectedReconHashes = @{
        $reconExe = 'aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8'
        $reconGame = 'fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f'
        $reconEngine = '687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580'
    }
    if (Test-Path -LiteralPath $reconRenderer) {
        $expectedReconHashes[$reconRenderer] = 'ca35c54598f23002cf3db8acd52a712e8d66523d0aef123013b3fea3d4952652'
    }
    foreach ($entry in $expectedReconHashes.GetEnumerator()) {
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Key).Hash.ToLowerInvariant()
        if ($actual -ne $entry.Value) { throw "Recon identity changed: $($entry.Key)" }
    }
    $gameExports = & $objdump -p $reconGame
    foreach ($decorated in @(
        '?gGameEngine@GAME@@3PAVGameEngine@1@A',
        '?Update@GameEngine@GAME@@QAEXH@Z',
        '?GetMainPlayer@GameEngine@GAME@@QBEPAVPlayer@2@XZ',
        '?GetCurrentLife@Character@GAME@@QBE?BNXZ',
        '?GetTotalCharAttribute@Character@GAME@@QBEMW4CharAttributeType@2@@Z',
        '?GetInventoryItems@Character@GAME@@QBEABV?$vector@I@mem@@XZ'
    )) {
        if (($gameExports -join "`n") -notmatch [regex]::Escape($decorated)) {
            throw "Exact Game.dll export missing: $decorated"
        }
    }
    $engineExports = & $objdump -p $reconEngine
    foreach ($decorated in @(
        '?gEngine@GAME@@3PAVEngine@1@A',
        '?PresentSurface@Engine@GAME@@QAEXXZ',
        '?GetGraphicsEngine@Engine@GAME@@QBEPAVGraphicsEngine@2@XZ',
        '?GetCanvas@GraphicsEngine@GAME@@QAEAAVGraphicsCanvas@2@XZ',
        '?GetCustomScene@GraphicsMTRenderer@GAME@@QAEPAVGraphicsScene@2@ABVCamera@2@PAVRegion@2@ABVViewport@2@ABVFrustum@2@W4RenderFlag@2@@Z',
        '?RenderCustomScene@GraphicsMTRenderer@GAME@@QAEXAAPAVGraphicsScene@2@@Z',
        '?IsRunningGameSimulation@Engine@GAME@@QBE_NXZ',
        '?IsInTeleportMode@Engine@GAME@@QBE_NXZ',
        '?GetAreaNameTag@Engine@GAME@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ'
    )) {
        if (($engineExports -join "`n") -notmatch [regex]::Escape($decorated)) {
            throw "Exact Engine.dll export missing: $decorated"
        }
    }
    $exeImports = & $objdump -p $reconExe
    if (($exeImports -join "`n") -notmatch
        [regex]::Escape('?Update@GameEngine@GAME@@QAEXH@Z')) {
        throw 'Exact executable no longer imports GameEngine::Update.'
    }
    if (($gameExports -join "`n") -notmatch
        [regex]::Escape('?AddToScene@Actor@GAME@@UAEXAAVGraphicsScene@2@PBVFrustum@2@@Z')) {
        throw 'Exact Game.dll no longer imports the passive Actor::AddToScene player-discovery seam.'
    }
    if (Test-Path -LiteralPath $reconRenderer) {
        $rendererImports = & $objdump -p $reconRenderer
        foreach ($decorated in @(
            '?CreateSecondarySurface@Direct3DDevice11@GAME@@UAEPAVRenderSurface@2@PAVEWindow@2@ABURenderDeviceSettings@2@@Z',
            '?CreateTexture@Direct3DDevice11@GAME@@UAEPAVRenderTexture@2@HHW4RenderTextureFormat@2@PBXW4TextureFlag@2@@Z',
            '?DestroyTexture@Direct3DDevice11@GAME@@UAEXAAPAVRenderTexture@2@@Z'
        )) {
            if (($rendererImports -join "`n") -notmatch [regex]::Escape($decorated)) {
                throw "Exact Direct3D11.dll export missing: $decorated"
            }
        }
        if (($rendererImports -join "`n") -notmatch 'CreateDXGIFactory1') {
            throw 'Direct3D11.dll no longer imports the selected DXGI bootstrap.'
        }
    }
    Write-Host 'Exact installed-build export/import audit passed'
}

Copy-Item -LiteralPath $output -Destination (Join-Path $packageRoot 'dxgi.dll') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'GrimDawnSecondScreen.ini') `
    -Destination (Join-Path $packageRoot 'GrimDawnSecondScreen.ini') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'GrimDawnSecondScreen.owner') `
    -Destination (Join-Path $packageRoot 'GrimDawnSecondScreen.owner') -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    mod = 'GrimDawnSecondScreen'
    configuration = $Configuration
    architecture = 'i386'
    gameVersion = '1.3.0.7'
    targetExecutableSha256 = 'aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8'
    targetGameDllSha256 = 'fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f'
    targetEngineDllSha256 = '687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580'
    targetDirect3D11DllSha256 = 'ca35c54598f23002cf3db8acd52a712e8d66523d0aef123013b3fea3d4952652'
    sha256 = $hash
    size = (Get-Item -LiteralPath $output).Length
    builtUtc = [DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $packageRoot 'manifest.json') -Encoding UTF8
Write-Host "Built $output"
Write-Host "SHA256 $hash"

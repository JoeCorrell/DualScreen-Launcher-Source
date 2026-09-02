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
    '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
    '-I', $sourceRoot, '-static', '-static-libgcc', '-static-libstdc++'
)
$optimization = if ($Configuration -eq 'Release') {
    @('-O2', '-DNDEBUG')
} else {
    @('-O0', '-g3')
}
$testOptimization = if ($Configuration -eq 'Release') {
    # Assertions are the native test oracle; keep them enabled even while
    # exercising the optimized code path used by the release DLL.
    @('-O2')
} else {
    $optimization
}

$stateTest = Join-Path $testRoot 'StateStoreTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\StateStoreTests.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'), '-o', $stateTest
))
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests compilation failed' }
& $stateTest
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests failed' }

$inputTest = Join-Path $testRoot 'PanelInputTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\PanelInputTests.cpp'),
    (Join-Path $sourceRoot 'PanelInput.cpp'), '-o', $inputTest
))
if ($LASTEXITCODE -ne 0) { throw 'PanelInputTests compilation failed' }
& $inputTest
if ($LASTEXITCODE -ne 0) { throw 'PanelInputTests failed' }

$iatHookTest = Join-Path $testRoot 'IatHookTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\IatHookTests.cpp'),
    (Join-Path $sourceRoot 'IatHook.cpp'), '-o', $iatHookTest
))
if ($LASTEXITCODE -ne 0) { throw 'IatHookTests compilation failed' }
& $iatHookTest
if ($LASTEXITCODE -ne 0) { throw 'IatHookTests failed' }

$sources = @(
    (Join-Path $sourceRoot 'Proxy.cpp'),
    (Join-Path $sourceRoot 'Mod.cpp'),
    (Join-Path $sourceRoot 'BuildIdentity.cpp'),
    (Join-Path $sourceRoot 'GameBridge.cpp'),
    (Join-Path $sourceRoot 'UnrealBridge.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'IatHook.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'),
    (Join-Path $sourceRoot 'PanelInput.cpp'),
    (Join-Path $sourceRoot 'Panel.cpp'),
    (Join-Path $sourceRoot 'AssetCache.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Invoke.S')
)
$output = Join-Path $buildRoot 'dinput8.dll'
& $compiler @($common + $optimization + @(
    '-shared', '-Wl,--kill-at', '-Wl,--enable-stdcall-fixup', '-o', $output
) + $sources + @(
    (Join-Path $sourceRoot 'dinput8.def'),
    '-lws2_32', '-lgdi32', '-lgdiplus', '-luser32', '-lbcrypt', '-ldinput8', '-ldxguid'
))
if ($LASTEXITCODE -ne 0) { throw 'Proxy DLL compilation failed' }

$header = & $objdump -f $output
if ($LASTEXITCODE -ne 0 -or ($header -join "`n") -notmatch 'pei-i386') {
    throw 'Output is not a 32-bit PE DLL.'
}
$pe = & $objdump -p $output
foreach ($export in @('DirectInput8Create', 'DllCanUnloadNow', 'DllGetClassObject')) {
    if (($pe -join "`n") -notmatch [regex]::Escape($export)) {
        throw "Missing proxy export: $export"
    }
}
foreach ($runtime in @('libgcc_s', 'libstdc++', 'libwinpthread')) {
    if (($pe -join "`n") -match [regex]::Escape($runtime)) {
        throw "Unexpected dynamic MinGW dependency: $runtime"
    }
}

$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\..'))
$researchExe = Join-Path $workspaceRoot '.research\transformers-wfc\device-original\Binaries\TWFC.exe'
if (Test-Path -LiteralPath $researchExe) {
    $item = Get-Item -LiteralPath $researchExe
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $researchExe).Hash.ToLowerInvariant()
    if ($item.Length -ne 37667328L -or
        $hash -ne '1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016') {
        throw 'Local research executable no longer matches the gated target build.'
    }
    $signatureChecks = @(
        [ordered]@{
            Name = 'UGameEngine::Tick'
            Start = '0x1169c130'
            Stop = '0x1169c13b'
            Patterns = @('1169c130:\s+55', '1169c131:\s+8b ec', '1169c133:\s+83 e4 f8')
        },
        [ordered]@{
            Name = 'MakeFName'
            Start = '0x116cc800'
            Stop = '0x116cc813'
            Patterns = @('116cc800:\s+55', '116cc801:\s+8b ec', '116cc803:\s+83 e4 f8',
                         '116cc806:\s+81 ec 04 01 00 00')
        },
        [ordered]@{
            Name = 'Invoke reflected function'
            Start = '0x116e6900'
            Stop = '0x116e690e'
            Patterns = @('116e6900:\s+55', '116e6901:\s+8b ec', '116e6903:\s+83 ec 44',
                         '116e6906:\s+53', '116e6907:\s+56', '116e6908:\s+57')
        },
        [ordered]@{
            Name = 'FString assignment'
            Start = '0x10901150'
            Stop = '0x10901164'
            Patterns = @('10901150:\s+56', '10901151:\s+57',
                         '10901152:\s+8b 7c 24 0c', '10901156:\s+8b f1')
        }
    )
    foreach ($check in $signatureChecks) {
        $disassembly = & $objdump -d "--start-address=$($check.Start)" `
            "--stop-address=$($check.Stop)" $researchExe
        if ($LASTEXITCODE -ne 0) {
            throw "$($check.Name) disassembly failed."
        }
        $text = $disassembly -join "`n"
        foreach ($pattern in $check.Patterns) {
            if ($text -notmatch $pattern) {
                throw "$($check.Name) exact-build signature evidence no longer matches."
            }
        }
    }

    $tickVtable = & $objdump -s '--start-address=0x1293ba1c' `
        '--stop-address=0x1293ba20' $researchExe
    if ($LASTEXITCODE -ne 0 -or
        ($tickVtable -join "`n") -notmatch '1293ba1c\s+30c16911') {
        throw 'UGameEngine::Tick exact-build vtable slot evidence no longer matches.'
    }

    $unrealBridgeSource = Get-Content -LiteralPath `
        (Join-Path $sourceRoot 'UnrealBridge.cpp') -Raw
    if ($unrealBridgeSource -notmatch `
        'kMakeFNameRva\s*=\s*0x00DCC800u' -or
        $unrealBridgeSource -notmatch `
        'kInvokeRva\s*=\s*0x00DE6900u' -or
        $unrealBridgeSource -notmatch `
        'kFStringAssignRva\s*=\s*0x00001150u') {
        throw 'The compiled reflection RVAs are not the verified exact-build addresses.'
    }
    $gameBridgeSource = Get-Content -LiteralPath `
        (Join-Path $sourceRoot 'GameBridge.cpp') -Raw
    if ($gameBridgeSource -notmatch `
        'kGameEngineTickVtableSlotRva\s*=\s*0x0203BA1Cu') {
        throw 'The compiled UGameEngine::Tick vtable slot is not the verified exact-build address.'
    }
}

Copy-Item -LiteralPath $output -Destination (Join-Path $packageRoot 'dinput8.dll') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'TransformersWfcSecondScreen.ini') `
    -Destination (Join-Path $packageRoot 'TransformersWfcSecondScreen.ini') -Force
$assetSource = Join-Path $projectRoot 'assets'
if (Test-Path -LiteralPath $assetSource) {
    $assetPackage = Join-Path $packageRoot 'assets'
    New-Item -ItemType Directory -Force -Path $assetPackage | Out-Null
    Copy-Item -Path (Join-Path $assetSource '*') -Destination $assetPackage `
        -Recurse -Force
}
$prerequisiteSource = Join-Path $projectRoot 'prerequisites'
$redistSource = Join-Path $projectRoot 'redist'
if (-not (Test-Path -LiteralPath $prerequisiteSource -PathType Container) -or
    -not (Test-Path -LiteralPath $redistSource -PathType Container)) {
    throw 'The Transformers prerequisite bootstrap or verified redistributable bundle is missing.'
}
$packagePrerequisites = Join-Path $packageRoot 'prerequisites'
New-Item -ItemType Directory -Force -Path $packagePrerequisites | Out-Null
Copy-Item -Path (Join-Path $prerequisiteSource '*') -Destination $packagePrerequisites `
    -Recurse -Force
$packageRedist = Join-Path $packagePrerequisites 'redist'
New-Item -ItemType Directory -Force -Path $packageRedist | Out-Null
Copy-Item -Path (Join-Path $redistSource '*') -Destination $packageRedist `
    -Recurse -Force
$outputHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    mod = 'TransformersWfcSecondScreen'
    configuration = $Configuration
    architecture = 'i386'
    game = 'Transformers: War for Cybertron'
    steamAppId = 42650
    targetExecutableSha256 = '1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016'
    targetExecutableBytes = 37667328
    sha256 = $outputHash
    size = (Get-Item -LiteralPath $output).Length
    builtUtc = [DateTime]::UtcNow.ToString('o')
}
$utf8WithoutBom = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText(
    (Join-Path $packageRoot 'manifest.json'),
    ($manifest | ConvertTo-Json),
    $utf8WithoutBom
)

# Keep the launcher's verified install payload in lockstep with this build.
$launcherModRoot = Join-Path $workspaceRoot 'launcher\src\main\assets\mods\transformers-wfc'
$launcherPayloadRoot = Join-Path $launcherModRoot 'payload'
New-Item -ItemType Directory -Force -Path $launcherPayloadRoot | Out-Null
$payloadFiles = @()
$dllTarget = 'Binaries/dinput8.dll'
Copy-Item -LiteralPath $output -Destination `
    (Join-Path $launcherPayloadRoot 'dinput8.dll') -Force
$payloadFiles += [ordered]@{
    source = 'dinput8.dll'
    target = $dllTarget
    sha256 = $outputHash
}
$iniSource = Join-Path $projectRoot 'TransformersWfcSecondScreen.ini'
$iniHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $iniSource).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $iniSource -Destination `
    (Join-Path $launcherPayloadRoot 'TransformersWfcSecondScreen.ini') -Force
$payloadFiles += [ordered]@{
    source = 'TransformersWfcSecondScreen.ini'
    target = 'Binaries/TransformersWfcSecondScreen.ini'
    sha256 = $iniHash
}
if (Test-Path -LiteralPath $assetSource) {
    $launcherAssetRoot = Join-Path $launcherPayloadRoot 'assets'
    New-Item -ItemType Directory -Force -Path $launcherAssetRoot | Out-Null
    foreach ($asset in Get-ChildItem -LiteralPath $assetSource -File | Sort-Object Name) {
        Copy-Item -LiteralPath $asset.FullName -Destination `
            (Join-Path $launcherAssetRoot $asset.Name) -Force
        $payloadFiles += [ordered]@{
            source = "assets/$($asset.Name)"
            target = "Binaries/assets/$($asset.Name)"
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $asset.FullName).Hash.ToLowerInvariant()
        }
    }
}
$launcherPrerequisiteRoot = Join-Path $launcherPayloadRoot 'prerequisites'
$launcherRedistRoot = Join-Path $launcherPrerequisiteRoot 'redist'
New-Item -ItemType Directory -Force -Path $launcherPrerequisiteRoot, $launcherRedistRoot | Out-Null
$bootstrapSource = Join-Path $prerequisiteSource 'WfcPrerequisiteBootstrap.cmd'
$bootstrapHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $bootstrapSource).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $bootstrapSource -Destination `
    (Join-Path $launcherPrerequisiteRoot 'WfcPrerequisiteBootstrap.cmd') -Force
$payloadFiles += [ordered]@{
    source = 'prerequisites/WfcPrerequisiteBootstrap.cmd'
    target = 'Binaries/SecondScreenPrerequisites/WfcPrerequisiteBootstrap.cmd'
    sha256 = $bootstrapHash
}
$redistIdentity = [Text.StringBuilder]::new()
foreach ($redistFile in Get-ChildItem -LiteralPath $redistSource -Recurse -File | Sort-Object FullName) {
    $relative = $redistFile.FullName.Substring($redistSource.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $redistFile.FullName).Hash.ToLowerInvariant()
    $destination = Join-Path $launcherRedistRoot $relative.Replace('/', '\')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $redistFile.FullName -Destination $destination -Force
    $payloadFiles += [ordered]@{
        source = "prerequisites/redist/$relative"
        target = "Binaries/SecondScreenPrerequisites/redist/$relative"
        sha256 = $hash
    }
    [void]$redistIdentity.Append($relative).Append('=').Append($hash).Append("`n")
}
$redistIdentityDigest = [Security.Cryptography.SHA256]::Create()
try {
    $redistIdentityHash = [BitConverter]::ToString(
        $redistIdentityDigest.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($redistIdentity.ToString())
        )
    ).Replace('-', '').ToLowerInvariant()
} finally {
    $redistIdentityDigest.Dispose()
}
$launcherManifest = [ordered]@{
    version = "wfc-$($outputHash.Substring(0, 12))-$($redistIdentityHash.Substring(0, 12))-$($bootstrapHash.Substring(0, 12))"
    files = $payloadFiles
}
[IO.File]::WriteAllText(
    (Join-Path $launcherModRoot 'manifest.json'),
    ($launcherManifest | ConvertTo-Json -Depth 5),
    $utf8WithoutBom
)
Write-Host "Built $output"
Write-Host "SHA256 $outputHash"

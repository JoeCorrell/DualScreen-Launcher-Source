param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipLauncherPayloadUpdate
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
$inlineRelocationTest = Join-Path $testRoot 'InlineHookRelocationTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\InlineHookRelocationTests.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $inlineRelocationTest
))
if ($LASTEXITCODE -ne 0) { throw 'InlineHookRelocationTests compilation failed' }
& $inlineRelocationTest
if ($LASTEXITCODE -ne 0) { throw 'InlineHookRelocationTests failed' }

& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\StateStoreTests.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'), '-o', $stateTest
))
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests compilation failed' }
& $stateTest
if ($LASTEXITCODE -ne 0) { throw 'StateStoreTests failed' }

$scratchTest = Join-Path $testRoot 'SwapScratchTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\SwapScratchTests.cpp'), '-o', $scratchTest
))
if ($LASTEXITCODE -ne 0) { throw 'SwapScratchTests compilation failed' }
& $scratchTest
if ($LASTEXITCODE -ne 0) { throw 'SwapScratchTests failed' }

# Audit the optimized production TU in both configurations, not just the scratch
# fixture. Unoptimized snapshot rendering has a larger pre-existing frame.
# A previous optimized capture required ~489 KB before game callbacks.
& $compiler @($common + @(
    '-O2', '-DNDEBUG',
    '-Wframe-larger-than=32768', '-fstack-usage', '-c',
    (Join-Path $sourceRoot 'GameBridge.cpp'),
    '-o', (Join-Path $testRoot 'GameBridgeStackAudit.o')
))
if ($LASTEXITCODE -ne 0) { throw 'GameBridge stack-frame audit failed' }

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

$characterSwapTest = Join-Path $testRoot 'CharacterSwapTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\CharacterSwapTests.cpp'),
    (Join-Path $sourceRoot 'CharacterSwap.cpp'), '-o', $characterSwapTest
))
if ($LASTEXITCODE -ne 0) { throw 'CharacterSwapTests compilation failed' }
& $characterSwapTest
if ($LASTEXITCODE -ne 0) { throw 'CharacterSwapTests failed' }

$checkpointGateTest = Join-Path $testRoot 'CheckpointGateTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\CheckpointGateTests.cpp'),
    (Join-Path $sourceRoot 'CheckpointGate.cpp'), '-o', $checkpointGateTest
))
if ($LASTEXITCODE -ne 0) { throw 'CheckpointGateTests compilation failed' }
& $checkpointGateTest
if ($LASTEXITCODE -ne 0) { throw 'CheckpointGateTests failed' }

$callPolicyTest = Join-Path $testRoot 'ReflectedCallPolicyTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\ReflectedCallPolicyTests.cpp'), '-o', $callPolicyTest
))
if ($LASTEXITCODE -ne 0) { throw 'ReflectedCallPolicyTests compilation failed' }
& $callPolicyTest
if ($LASTEXITCODE -ne 0) { throw 'ReflectedCallPolicyTests failed' }

$lookupTest = Join-Path $testRoot 'UnrealBridgeLookupTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\UnrealBridgeLookupTests.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Invoke.S'), '-o', $lookupTest
))
if ($LASTEXITCODE -ne 0) { throw 'UnrealBridgeLookupTests compilation failed' }
$cloneResearchExe = Join-Path $projectRoot '..\..\..\.research\transformers-wfc\device-original\Binaries\TWFC.exe'
if (Test-Path -LiteralPath $cloneResearchExe) { & $lookupTest $cloneResearchExe } else { & $lookupTest }
if ($LASTEXITCODE -ne 0) { throw 'UnrealBridgeLookupTests failed' }

$travelTest = Join-Path $testRoot 'NativeCampaignTravelTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeCampaignTravelTests.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $travelTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeCampaignTravelTests compilation failed' }
& $travelTest
if ($LASTEXITCODE -ne 0) { throw 'NativeCampaignTravelTests failed' }

$menuTest = Join-Path $testRoot 'NativeCampaignMenuTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeCampaignMenuTests.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $menuTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeCampaignMenuTests compilation failed' }
if (Test-Path -LiteralPath $cloneResearchExe) { & $menuTest $cloneResearchExe } else { & $menuTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeCampaignMenuTests failed' }

$shooterTest = Join-Path $testRoot 'NativeWeaponShooterTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponShooterTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $shooterTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponShooterTests compilation failed' }
$shooterResearchExe = Join-Path $projectRoot '..\..\..\.research\transformers-wfc\device-original\Binaries\TWFC.exe'
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $shooterTest $shooterResearchExe
} else {
    & $shooterTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponShooterTests failed' }

$movementTest = Join-Path $testRoot 'NativeAiMovementTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAiMovementTests.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $movementTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAiMovementTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $movementTest $shooterResearchExe
} else {
    & $movementTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeAiMovementTests failed' }

$playerServicesTest = Join-Path $testRoot 'NativePlayerServicesTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerServicesTests.cpp'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $playerServicesTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerServicesTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $playerServicesTest $shooterResearchExe
} else {
    & $playerServicesTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerServicesTests failed' }

$abilityHudTest = Join-Path $testRoot 'NativeAbilityHudTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAbilityHudTests.cpp'), '-o', $abilityHudTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAbilityHudTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $abilityHudTest $shooterResearchExe
} else {
    & $abilityHudTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeAbilityHudTests failed' }

$cameraTest = Join-Path $testRoot 'NativeCameraHandoffTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeCameraHandoffTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $cameraTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeCameraHandoffTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $cameraTest $shooterResearchExe
} else {
    & $cameraTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeCameraHandoffTests failed' }

$playerFormTest = Join-Path $testRoot 'NativePlayerFormStateTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerFormStateTests.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $playerFormTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerFormStateTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $playerFormTest $shooterResearchExe
} else {
    & $playerFormTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerFormStateTests failed' }

$playerInputTest = Join-Path $testRoot 'NativePlayerInputTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerInputTests.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'NativePlayerInputReset.S'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $playerInputTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerInputTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $playerInputTest $shooterResearchExe
} else {
    & $playerInputTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerInputTests failed' }

$detachTest = Join-Path $testRoot 'NativeControllerDetachTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeControllerDetachTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Log.cpp'), '-o', $detachTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeControllerDetachTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $detachTest $shooterResearchExe
} else {
    & $detachTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeControllerDetachTests failed' }

$aimTest = Join-Path $testRoot 'NativeAiAimingTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAiAimingTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $aimTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAiAimingTests compilation failed' }
$aimOriginalExe = Join-Path $projectRoot '..\..\..\.research\transformers-wfc\device-original\Binaries\TWFC.exe'
if (Test-Path -LiteralPath $aimOriginalExe) { & $aimTest $aimOriginalExe }
else { & $aimTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeAiAimingTests failed' }

$stopTest = Join-Path $testRoot 'NativeAiStopMovementTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAiStopMovementTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $stopTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAiStopMovementTests compilation failed' }
if (Test-Path -LiteralPath $aimOriginalExe) { & $stopTest $aimOriginalExe }
else { & $stopTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeAiStopMovementTests failed' }

$dialogTest = Join-Path $testRoot 'NativeDialogOwnerTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeDialogOwnerTests.cpp'), '-o', $dialogTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeDialogOwnerTests compilation failed' }
& $dialogTest
if ($LASTEXITCODE -ne 0) { throw 'NativeDialogOwnerTests failed' }

$poiTest = Join-Path $testRoot 'NativePointOfInterestTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePointOfInterestTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $poiTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePointOfInterestTests compilation failed' }
if (Test-Path -LiteralPath $aimOriginalExe) { & $poiTest $aimOriginalExe }
else { & $poiTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePointOfInterestTests failed' }

$collisionTest = Join-Path $testRoot 'NativeCollisionRolesTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeCollisionRolesTests.cpp'), '-o', $collisionTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeCollisionRolesTests compilation failed' }
& $collisionTest
if ($LASTEXITCODE -ne 0) { throw 'NativeCollisionRolesTests failed' }

$heatTest = Join-Path $testRoot 'NativeWeaponHeatTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponHeatTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $heatTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponHeatTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $heatTest $shooterResearchExe
} else {
    & $heatTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponHeatTests failed' }

$progressTest = Join-Path $testRoot 'NativeWeaponProgressTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponProgressTests.cpp'), '-o', $progressTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponProgressTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) {
    & $progressTest $shooterResearchExe
} else {
    & $progressTest
}
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponProgressTests failed' }

$modifierTest = Join-Path $testRoot 'NativeWeaponModifierTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponModifierTests.cpp'), '-o', $modifierTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponModifierTests compilation failed' }
& $modifierTest
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponModifierTests failed' }

$cacheTest = Join-Path $testRoot 'NativeWeaponDataCacheTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponDataCacheTests.cpp'), '-o', $cacheTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponDataCacheTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $cacheTest $shooterResearchExe }
else { & $cacheTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponDataCacheTests failed' }

$retentionTest = Join-Path $testRoot 'NativeWeaponRetentionTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponRetentionTests.cpp'), '-o', $retentionTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponRetentionTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $retentionTest $shooterResearchExe } else { & $retentionTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponRetentionTests failed' }

$animatorTest = Join-Path $testRoot 'NativeWeaponAnimatorTests.exe'
& (Join-Path $projectRoot 'tests\Verify-AnimatorIntegration.ps1')
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeWeaponAnimatorTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $animatorTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponAnimatorTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $animatorTest $shooterResearchExe } else { & $animatorTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponAnimatorTests failed' }

$identityTest = Join-Path $testRoot 'NativeCharacterIdentityTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeCharacterIdentityTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $identityTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeCharacterIdentityTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $identityTest $shooterResearchExe } else { & $identityTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeCharacterIdentityTests failed' }

$vehicleTest = Join-Path $testRoot 'NativeVehicleAiTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeVehicleAiTests.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.S'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $vehicleTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeVehicleAiTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $vehicleTest $shooterResearchExe } else { & $vehicleTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeVehicleAiTests failed' }

$worldStateTest = Join-Path $testRoot 'NativeAiWorldStateTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAiWorldStateTests.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.S'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $worldStateTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAiWorldStateTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $worldStateTest $shooterResearchExe } else { & $worldStateTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeAiWorldStateTests failed' }

$sensorTest = Join-Path $testRoot 'NativeAiSensorTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeAiSensorTests.cpp'),
    (Join-Path $sourceRoot 'ThisCall.S'), '-o', $sensorTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeAiSensorTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $sensorTest $shooterResearchExe } else { & $sensorTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeAiSensorTests failed' }

$rolePawnTest = Join-Path $testRoot 'NativeRolePawnTests.exe'
& $compiler @($common + $testOptimization + @(
    '-msse2', (Join-Path $projectRoot 'tests\NativeRolePawnTests.cpp'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'),
    (Join-Path $sourceRoot 'NativeRolePawn.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $rolePawnTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeRolePawnTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $rolePawnTest $shooterResearchExe } else { & $rolePawnTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeRolePawnTests failed' }

$weaponExposureTest = Join-Path $testRoot 'NativeWeaponExposureTests.exe'
& $compiler @($common + $testOptimization + @(
    '-msse2', (Join-Path $projectRoot 'tests\NativeWeaponExposureTests.cpp'),
    (Join-Path $projectRoot 'tests\NativeWeaponExposureTestCall.S'),
    (Join-Path $sourceRoot 'NativeWeaponExposure.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $weaponExposureTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponExposureTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $weaponExposureTest $shooterResearchExe } else { & $weaponExposureTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeWeaponExposureTests failed' }

$pawnDamageTest = Join-Path $testRoot 'NativePawnDamageTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePawnDamageTests.cpp'),
    (Join-Path $sourceRoot 'NativePawnDamage.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $pawnDamageTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePawnDamageTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $pawnDamageTest $shooterResearchExe } else { & $pawnDamageTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePawnDamageTests failed' }

$pawnTargetingTest = Join-Path $testRoot 'NativePawnTargetingTests.exe'
& $compiler @($common + $testOptimization + @(
    '-msse2', (Join-Path $projectRoot 'tests\NativePawnTargetingTests.cpp'),
    (Join-Path $sourceRoot 'NativePawnTargeting.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $pawnTargetingTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePawnTargetingTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $pawnTargetingTest $shooterResearchExe } else { & $pawnTargetingTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePawnTargetingTests failed' }

$pawnDownedTest = Join-Path $testRoot 'NativePawnDownedTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePawnDownedTests.cpp'),
    (Join-Path $sourceRoot 'NativePawnDowned.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $pawnDownedTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePawnDownedTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $pawnDownedTest $shooterResearchExe } else { & $pawnDownedTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePawnDownedTests failed' }

$gameDeathTest = Join-Path $testRoot 'NativeGameDeathTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeGameDeathTests.cpp'),
    (Join-Path $sourceRoot 'NativeGameDeath.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $gameDeathTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeGameDeathTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $gameDeathTest $shooterResearchExe } else { & $gameDeathTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeGameDeathTests failed' }

$pawnTransformTest = Join-Path $testRoot 'NativePawnTransformTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePawnTransformTests.cpp'),
    (Join-Path $sourceRoot 'NativePawnTransform.S'),
    (Join-Path $sourceRoot 'ThisCall.S'),
    (Join-Path $sourceRoot 'Hook.cpp'), '-o', $pawnTransformTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePawnTransformTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $pawnTransformTest $shooterResearchExe } else { & $pawnTransformTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePawnTransformTests failed' }

$playerCombatTest = Join-Path $testRoot 'NativePlayerCombatTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerCombatTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerCombatTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerCombatTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerCombatTest $shooterResearchExe } else { & $playerCombatTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerCombatTests failed' }

$playerMotionTest = Join-Path $testRoot 'NativePlayerMotionTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerMotionTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerMotionTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerMotionTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerMotionTest $shooterResearchExe } else { & $playerMotionTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerMotionTests failed' }

$playerFineAimTest = Join-Path $testRoot 'NativePlayerFineAimTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerFineAimTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerFineAimTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerFineAimTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerFineAimTest $shooterResearchExe } else { & $playerFineAimTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerFineAimTests failed' }

$playerTargetJumpTest = Join-Path $testRoot 'NativePlayerTargetJumpTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerTargetJumpTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerTargetJumpTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerTargetJumpTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerTargetJumpTest $shooterResearchExe } else { & $playerTargetJumpTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerTargetJumpTests failed' }

$meleeTargetTest = Join-Path $testRoot 'NativeMeleeTargetTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativeMeleeTargetTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $meleeTargetTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativeMeleeTargetTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $meleeTargetTest $shooterResearchExe } else { & $meleeTargetTest }
if ($LASTEXITCODE -ne 0) { throw 'NativeMeleeTargetTests failed' }

$playerOwnerTest = Join-Path $testRoot 'NativePlayerOwnerTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerOwnerTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerOwnerTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerOwnerTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerOwnerTest $shooterResearchExe } else { & $playerOwnerTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerOwnerTests failed' }

$playerNavigationTest = Join-Path $testRoot 'NativePlayerNavigationTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerNavigationTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerNavigationTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerNavigationTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerNavigationTest $shooterResearchExe } else { & $playerNavigationTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerNavigationTests failed' }

$playerAssignmentTest = Join-Path $testRoot 'NativePlayerAssignmentTests.exe'
& $compiler @($common + $testOptimization + @(
    (Join-Path $projectRoot 'tests\NativePlayerAssignmentTests.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $projectRoot 'tests\NativeRolePawnTestCall.S'), '-o', $playerAssignmentTest
))
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerAssignmentTests compilation failed' }
if (Test-Path -LiteralPath $shooterResearchExe) { & $playerAssignmentTest $shooterResearchExe } else { & $playerAssignmentTest }
if ($LASTEXITCODE -ne 0) { throw 'NativePlayerAssignmentTests failed' }

$sources = @(
    (Join-Path $sourceRoot 'Proxy.cpp'),
    (Join-Path $sourceRoot 'Mod.cpp'),
    (Join-Path $sourceRoot 'BuildIdentity.cpp'),
    (Join-Path $sourceRoot 'GameBridge.cpp'),
    (Join-Path $sourceRoot 'UnrealBridge.cpp'),
    (Join-Path $sourceRoot 'Hook.cpp'),
    (Join-Path $sourceRoot 'IatHook.cpp'),
    (Join-Path $sourceRoot 'StateStore.cpp'),
    (Join-Path $sourceRoot 'CharacterSwap.cpp'),
    (Join-Path $sourceRoot 'CampaignClassSystem.cpp'),
    (Join-Path $sourceRoot 'NativeCampaignTravel.cpp'),
    (Join-Path $sourceRoot 'NativeCampaignMenu.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponShooter.cpp'),
    (Join-Path $sourceRoot 'NativeAiPlanning.cpp'),
    (Join-Path $sourceRoot 'NativeAiMovement.cpp'),
    (Join-Path $sourceRoot 'NativeAiStopMovement.cpp'),
    (Join-Path $sourceRoot 'NativeAiAiming.cpp'),
    (Join-Path $sourceRoot 'NativePlayerServices.cpp'),
    (Join-Path $sourceRoot 'NativeDialogOwner.cpp'),
    (Join-Path $sourceRoot 'NativePointOfInterest.cpp'),
    (Join-Path $sourceRoot 'NativeCollisionRoles.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponHeat.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponProgress.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponRoleData.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponAmmo.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponModifiers.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponDataCache.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponRetention.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponAnimator.cpp'),
    (Join-Path $sourceRoot 'NativeAbilityHud.cpp'),
    (Join-Path $sourceRoot 'NativeCameraHandoff.cpp'),
    (Join-Path $sourceRoot 'NativePlayerFormState.cpp'),
    (Join-Path $sourceRoot 'NativePlayerInput.cpp'),
    (Join-Path $sourceRoot 'NativeControllerDetach.cpp'),
    (Join-Path $sourceRoot 'NativePawnOwnership.cpp'),
    (Join-Path $sourceRoot 'NativePlayerAdoption.cpp'),
    (Join-Path $sourceRoot 'NativeAiAdoption.cpp'),
    (Join-Path $sourceRoot 'NativeCharacterIdentity.cpp'),
    (Join-Path $sourceRoot 'NativeVehicleAi.cpp'),
    (Join-Path $sourceRoot 'NativeAiSensors.cpp'),
    (Join-Path $sourceRoot 'NativeAiWorldState.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.cpp'),
    (Join-Path $sourceRoot 'NativeRolePawn.S'),
    (Join-Path $sourceRoot 'NativeWeaponExposure.cpp'),
    (Join-Path $sourceRoot 'NativeWeaponExposure.S'),
    (Join-Path $sourceRoot 'NativePawnDamage.cpp'),
    (Join-Path $sourceRoot 'NativePawnDamage.S'),
    (Join-Path $sourceRoot 'NativePawnTargeting.cpp'),
    (Join-Path $sourceRoot 'NativePawnTargeting.S'),
    (Join-Path $sourceRoot 'NativePawnDowned.cpp'),
    (Join-Path $sourceRoot 'NativePawnDowned.S'),
    (Join-Path $sourceRoot 'NativeGameDeath.cpp'),
    (Join-Path $sourceRoot 'NativeGameDeath.S'),
    (Join-Path $sourceRoot 'NativePawnTransform.cpp'),
    (Join-Path $sourceRoot 'NativePawnTransform.S'),
    (Join-Path $sourceRoot 'NativePlayerCombat.cpp'),
    (Join-Path $sourceRoot 'NativeMeleeTargetGuard.cpp'),
    (Join-Path $sourceRoot 'NativePlayerCombat.S'),
    (Join-Path $sourceRoot 'NativePlayerInputReset.S'),
    (Join-Path $sourceRoot 'CheckpointGate.cpp'),
    (Join-Path $sourceRoot 'CharacterRoleAdapter.cpp'),
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
            Name = 'Squad setup uses the compiled EDI/no-argument buddy set-true specialization'
            Start = '0x10bc6d36'
            Stop = '0x10bc6d3d'
            Patterns = @('10bc6d36:\s+8b fe', '10bc6d38:\s+e8 03 dc fe ff')
        },
        [ordered]@{
            Name = 'AI squad spawning calls the same specialized buddy setter without a bool argument'
            Start = '0x10bc6eb2'
            Stop = '0x10bc6ec5'
            Patterns = @('10bc6eb2:\s+8b f8', '10bc6eb4:\s+83 ff 10',
                         '10bc6eb7:\s+75 07', '10bc6ec0:\s+e8 7b da fe ff')
        },
        [ordered]@{
            Name = 'Player form-name helper uses physical player-pawn cast before vehicle read'
            Start = '0x10b191b7'
            Stop = '0x10b191c2'
            Patterns = @('10b191b7:\s+e8 74 ef ff ff', '10b191bc:\s+8b 80 70 05 00 00')
        },
        [ordered]@{
            Name = 'Vehicle BeginState independently inlines the same unsafe physical-pawn cast'
            Start = '0x10b2813e'
            Stop = '0x10b28152'
            Patterns = @('10b2813e:\s+e8 ed 92 dd ff', '10b28147:\s+be 10 00 00 00',
                         '10b2814c:\s+8b b6 70 05 00 00')
        },
        [ordered]@{
            Name = 'Compiled campaign StartLevel (EAX script-this)'
            Start = '0x10a9b300'
            Stop = '0x10a9b30d'
            Patterns = @('10a9b300:\s+55', '10a9b301:\s+8b ec',
                         '10a9b303:\s+83 e4 f8', '10a9b306:\s+83 ec 1c',
                         '10a9b30b:\s+8b f0')
        },
        [ordered]@{
            Name = 'FinalCountdown compiled direct call bypasses reflection'
            Start = '0x10a9bc8e'
            Stop = '0x10a9bc95'
            Patterns = @('10a9bc8e:\s+8b c6', '10a9bc90:\s+e8 6b f6 ff ff')
        },
        [ordered]@{
            Name = 'Compiled-script executor context adjustment'
            Start = '0x116e6a59'
            Stop = '0x116e6a64'
            Patterns = @('116e6a59:\s+f6 47 54 20', '116e6a61:\s+8d 73 10')
        },
        [ordered]@{
            Name = 'Compiled delegate includes both native binding words'
            Start = '0x10bc8aa6'
            Stop = '0x10bc8ac1'
            Patterns = @('10bc8aa6:\s+8b 08', '10bc8aac:\s+89 4e 4c',
                         '10bc8aaf:\s+8b 48 04', '10bc8ab5:\s+8b 48 0c',
                         '10bc8abb:\s+8b 40 08')
        },
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
# Offline validation can deliberately leave the currently published/installed
# payload state alone (for example, while the mod is disabled for a baseline).
if (-not $SkipLauncherPayloadUpdate) {
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
}
Write-Host "Built $output"
Write-Host "SHA256 $outputHash"

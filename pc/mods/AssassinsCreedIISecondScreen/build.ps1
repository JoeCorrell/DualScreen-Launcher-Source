param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$GraphicsTests)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$compiler='C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe'
$env:PATH=(Split-Path -Parent $compiler)+';'+$env:PATH
$out=Join-Path $root ('build/'+$Configuration.ToLowerInvariant())
New-Item -ItemType Directory -Force -Path $out | Out-Null
$common=@('-std=c++17','-m32','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-misleading-indentation',
    '-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-static-libgcc','-static-libstdc++','-I',"$root/src")
if($Configuration -eq 'Release'){$common += '-O2'}else{$common += @('-O0','-g')}
$common += @('-I',"$root/third_party/lzo/include")
& $compiler @common "$root/tests/CharacterPreviewTargetTests.cpp" '-static' '-ld3d9' '-ld3dcompiler' '-luser32' '-o' "$out/CharacterPreviewTargetTests.exe"
if($LASTEXITCODE -ne 0){throw 'Preview target test compilation failed'}
if($GraphicsTests){
    & "$out/CharacterPreviewTargetTests.exe"
    if($LASTEXITCODE -ne 0){throw 'Preview target graphics tests failed or D3D9 device unavailable'}
}
& 'C:/msys64/mingw32/bin/i686-w64-mingw32-gcc.exe' -O2 -DNDEBUG -I "$root/third_party/lzo/include" -c "$root/third_party/lzo/src/lzo2a_d2.c" -o "$out/lzo2a_safe.o"
if($LASTEXITCODE -ne 0){throw 'LZO2A safe decoder compilation failed'}
foreach($test in @('ForgeAssetTests','AssetCacheTests','MapCacheTests','MapRasterTests','MapSpritesTests','MapMotionTests','MissionNavigationTests','PageSwipeTests','MapLegendTests')) {
    & $compiler @common "$root/tests/$test.cpp" "$out/lzo2a_safe.o" '-static' '-o' "$out/$test.exe"
    if($LASTEXITCODE -ne 0){throw "$test compilation failed"}
    & "$out/$test.exe"
    if($LASTEXITCODE -ne 0){throw "$test failed"}
}
& $compiler @common "$root/tests/ContractTests.cpp" '-static' '-o' "$out/ContractTests.exe"
if($LASTEXITCODE -ne 0){throw 'Contract test compilation failed'}
& "$out/ContractTests.exe"
if($LASTEXITCODE -ne 0){throw 'Contract tests failed'}
& $compiler @common "$root/tests/NativeReadTests.cpp" '-static' '-o' "$out/NativeReadTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native read test compilation failed'}
& "$out/NativeReadTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native read tests failed'}
& $compiler @common "$root/tests/NativeInventoryTests.cpp" '-static' '-o' "$out/NativeInventoryTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native inventory test compilation failed'}
& "$out/NativeInventoryTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native inventory tests failed'}
foreach($test in @('NativeReadTransactionTests','NativeWorkGateTests','NativeBridgeWorkerTests','NativeInventoryMenuTests','LocalizationTests','NativeLocalizationTests','NativeInventoryViewTests','NativeDnaTests','NativeDatabaseTests','NativeEquipmentTests','NativeWeaponStatsTests','NativeMapTests','NativeMapMarkersTests','NativeSynchronizationTests','NativeVillaTests','NativeCapesTests','NativeCapeActionTests','NativeMedicineActionTests','NativeWeaponSelectionTests','NativeGlyphsTests','NativeMissionTests','NativeHealthTests','NativeNotorietyTests','NativeCharacterSourceTests','NativeCharacterGeometryTests','NativeCharacterPaletteTests','NativeCharacterDynamicTests','NativeCharacterDrawCommandTests','CharacterFramePlanTests','NativeCharacterPassTraceTests','CharacterPreviewCameraTests','NativeCollectiblesTests')) {
    & $compiler @common "$root/tests/$test.cpp" '-static' '-o' "$out/$test.exe"
    if($LASTEXITCODE -ne 0){throw "$test compilation failed"}
    & "$out/$test.exe"
    if($LASTEXITCODE -ne 0){throw "$test failed"}
}
& $compiler @common "$root/tests/VisualStateTests.cpp" '-static' '-o' "$out/VisualStateTests.exe"
if($LASTEXITCODE -ne 0){throw 'Visual state test compilation failed'}
& "$out/VisualStateTests.exe"
if($LASTEXITCODE -ne 0){throw 'Visual state tests failed'}
& $compiler @common "$root/tests/NativeAffinityTests.cpp" '-static' '-o' "$out/NativeAffinityTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native affinity test compilation failed'}
& "$out/NativeAffinityTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native affinity tests failed'}
& $compiler @common "$root/tests/NativeSamplingTests.cpp" '-static' '-o' "$out/NativeSamplingTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native sampling test compilation failed'}
& "$out/NativeSamplingTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native sampling tests failed'}
& $compiler @common "$root/tests/UiNavigationTests.cpp" "$root/src/AnimusUi.cpp" -static -lgdiplus -lgdi32 -luser32 -o "$out/UiNavigationTests.exe"
if($LASTEXITCODE -ne 0){throw 'UI navigation test compilation failed'}
& "$out/UiNavigationTests.exe"
if($LASTEXITCODE -ne 0){throw 'UI navigation tests failed'}
& $compiler @common "$root/tests/RenderHarness.cpp" "$root/src/AnimusUi.cpp" -static `
    -lgdiplus -lgdi32 -luser32 -o "$out/RenderHarness.exe"
if($LASTEXITCODE -ne 0){throw 'Render harness compilation failed'}
& "$out/RenderHarness.exe" "$out/preview"
if($LASTEXITCODE -ne 0){throw 'Render harness failed'}
& $compiler @common -shared "$root/src/Runtime.cpp" "$root/src/AnimusUi.cpp" "$root/src/NativeBridge.cpp" "$root/src/dinput8.def" "$out/lzo2a_safe.o" `
    -static -lgdiplus -lgdi32 -luser32 -lws2_32 -lbcrypt -lole32 '-Wl,--no-insert-timestamp' -o "$out/dinput8.dll"
if($LASTEXITCODE -ne 0){throw 'Native proxy compilation failed'}
$imports=& 'C:/msys64/mingw32/bin/objdump.exe' -p "$out/dinput8.dll"
if($LASTEXITCODE -ne 0 -or ($imports -join "`n") -match 'DLL Name: (libgcc|libstdc|libwinpthread)'){
    throw 'Invalid native dependencies'
}
Write-Output "Development native payload built at $out. Native readers and guarded cape/medicine/weapon-selection actions verified locally. No device or launcher payload writes."


[CmdletBinding()]
param(
    [string] $Mingw = "C:\msys64\mingw64\bin",
    [string] $Skse = "C:\Users\Shadow\AppData\Local\Temp\skse64_2_00_20_extract\skse64_2_00_20",
    [string] $Adb = "C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string] $Device,
    [string] $DependencyRoot = "",
    [string] $RemoteGame = "/storage/emulated/0/Download/TESVSKYRIMSESteamRIP.com/The Elder Scrolls V Skyrim - Special Edition",
    [switch] $Install,
    [switch] $Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$src = Join-Path $root "src"
$out = Join-Path $root "build"
$shared = Join-Path (Split-Path $root -Parent) "DeadIslandSecondScreen\src"
$workspace = Split-Path (Split-Path (Split-Path $root -Parent) -Parent) -Parent
if (-not $DependencyRoot) { $DependencyRoot = Join-Path $workspace "artifacts" }
$DependencyRoot = (Resolve-Path -LiteralPath $DependencyRoot).Path
$assetRaster = Join-Path $DependencyRoot "skyrim-interface-assets\raster"
$nifly = Join-Path $DependencyRoot "nifly"

if ($Clean) {
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    return
}

if (-not (Test-Path -LiteralPath "$Mingw\g++.exe")) { throw "MinGW-w64 was not found at $Mingw" }
if (-not (Test-Path -LiteralPath "$Skse\skse64_1_5_97.dll")) { throw "Official SKSE 2.0.20 was not found at $Skse" }
New-Item -ItemType Directory -Path $out -Force | Out-Null

$cxx = "$Mingw\g++.exe"
$dlltool = "$Mingw\dlltool.exe"
$windres = "$Mingw\windres.exe"
$env:PATH = "$Mingw;$env:PATH"
$common = @(
    "-std=c++17", "-O2", "-DNDEBUG", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
    "-Wall", "-Wextra", "-Wno-unused-parameter", "-static", "-static-libgcc", "-static-libstdc++"
)

# Windows PowerShell 5's legacy native-argument marshaller can split an
# already-quoted absolute path at the workspace's spaces. Compile from the mod
# directory and use relative paths so MinGW receives each source as one token.
Push-Location $root
try {

$converter = Join-Path $root "tools\Convert-PngToRgba.ps1"
$nativeAssetOut = Join-Path $out "native_assets"
$nativeSources = @{
    "frame.rgba" = Join-Path $assetRaster "shared\DefineSprite_206_MessageBackground\1.png"
    "meter_track.rgba" = Join-Path $assetRaster "hud-images\716.png"
    "meter_fill.rgba" = Join-Path $assetRaster "hud-images\719.png"
    "marker.rgba" = Join-Path $assetRaster "map\DefineSprite_135_YouAreHereMarker\15.png"
    "arrow_left.rgba" = Join-Path $assetRaster "shared\DefineSprite_162_Left arrow\1.png"
    "arrow_right.rgba" = Join-Path $assetRaster "shared\DefineSprite_160_Right arrow\1.png"
    "quest_marker.rgba" = Join-Path $assetRaster "map\DefineSprite_202_QuestTargetMarker\15.png"
    "quest_door_marker.rgba" = Join-Path $assetRaster "map\DefineSprite_206_QuestTargetDoorMarker\15.png"
    "multiple_quest_marker.rgba" = Join-Path $assetRaster "map\DefineSprite_230_MultipleQuestTargetMarker\15.png"
    "diamond.rgba" = Join-Path $assetRaster "hud-images\115_DiamondMarker.png"
    "world_map.rgba" = Join-Path $assetRaster "map\tamriel_lod16.png"
    "player_set_marker.rgba" = Join-Path $assetRaster "map\DefineSprite_211_PlayerSetMarker\15.png"
    "page_character.rgba" = Join-Path $root "assets\generated\page-icons\character.png"
    "page_inventory.rgba" = Join-Path $root "assets\generated\page-icons\inventory.png"
    "page_map.rgba" = Join-Path $root "assets\generated\page-icons\map.png"
    "page_magic.rgba" = Join-Path $root "assets\generated\page-icons\magic.png"
    "page_quests.rgba" = Join-Path $root "assets\generated\page-icons\quests.png"
}
$locationMarkerBaseSpriteIds = @(
    321, 161, 188, 324, 327, 265, 222, 277, 182, 259,
    250, 280, 268, 137, 238, 256, 164, 286, 158, 176,
    167, 253, 318, 213, 330, 191, 247, 216, 262, 185,
    219, 225, 289, 179, 194, 197, 146, 149, 152, 155,
    170, 173, 241, 244, 140, 143, 232, 235, 270, 273,
    312, 315, 307, 304, 301, 298, 295, 292, 310
)
$locationMarkerUndiscoveredSpriteIds = @(
    118, 28, 46, 120, 122, 86, 60, 93, 42, 82,
    76, 95, 88, 126, 68, 80, 30, 128, 26, 38,
    32, 78, 116, 54, 124, 48, 74, 56, 84, 44,
    58, 62, 97, 40, 50, 52, 18, 20, 22, 24,
    34, 36, 70, 72, 14, 16, 64, 66, 89, 91,
    112, 114, 109, 107, 105, 103, 101, 99, 111
)
function Resolve-MapSpriteFrame([int] $SpriteId) {
    $mapAssets = Join-Path $assetRaster "map"
    $matches = @(Get-ChildItem -LiteralPath $mapAssets -Directory | Where-Object {
        $_.Name -eq "DefineSprite_$SpriteId" -or
        $_.Name -like "DefineSprite_${SpriteId}_*"
    })
    if ($matches.Count -ne 1) {
        throw "Expected one extracted map sprite for ID $SpriteId, found $($matches.Count)"
    }
    return Join-Path $matches[0].FullName "1.png"
}
for ($index = 0; $index -lt 59; ++$index) {
    $type = $index + 1
    $nativeSources[("location_{0:D2}.rgba" -f $type)] =
        Resolve-MapSpriteFrame $locationMarkerBaseSpriteIds[$index]
    $nativeSources[("location_undiscovered_{0:D2}.rgba" -f $type)] =
        Resolve-MapSpriteFrame $locationMarkerUndiscoveredSpriteIds[$index]
}
if (-not (Test-Path -LiteralPath $converter)) { throw "Missing native asset converter" }
New-Item -ItemType Directory -Path $nativeAssetOut -Force | Out-Null
foreach ($entry in $nativeSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        throw "Missing extracted Skyrim interface asset: $($entry.Value)"
    }
    $converted = Join-Path $nativeAssetOut $entry.Key
    $requiresConversion = -not (Test-Path -LiteralPath $converted)
    if (-not $requiresConversion) {
        $requiresConversion = (Get-Item -LiteralPath $entry.Value).LastWriteTimeUtc -gt `
            (Get-Item -LiteralPath $converted).LastWriteTimeUtc
    }
    if ($requiresConversion) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $converter `
            -InputPath $entry.Value -OutputPath $converted
        if ($LASTEXITCODE -ne 0) { throw "Could not convert Skyrim interface asset $($entry.Key)" }
    }
}
$terrainHeightSource = Join-Path $root "assets\tamriel_height.bin"
$terrainHeightOutput = Join-Path $nativeAssetOut "tamriel_height.bin"
if (-not (Test-Path -LiteralPath $terrainHeightSource)) {
    throw "Missing generated Tamriel 3D terrain resource: $terrainHeightSource"
}
Copy-Item -LiteralPath $terrainHeightSource -Destination $terrainHeightOutput -Force

if (-not (Test-Path -LiteralPath $windres)) { throw "MinGW resource compiler was not found" }
& $windres -I "src" -i "src\Assets.rc" -o "build\NativeAssets.resource.o"
if ($LASTEXITCODE -ne 0) { throw "Native Skyrim asset resource build failed" }

& $dlltool -d "$src\skse64_steam_loader.def" -l "$out\libskse64_steam_loader.a" -D "skse64_steam_loader.dll"
if ($LASTEXITCODE -ne 0) { throw "Could not create the SKSE loader import library" }

$proxySources = @(
    "src\Proxy.cpp",
    "src\Mod.cpp",
    "src\Ui.cpp",
    "src\NativeAssets.cpp",
    "..\DeadIslandSecondScreen\src\Canvas.cpp",
    "..\DeadIslandSecondScreen\src\Log.cpp"
)
$proxyArgs = $common + @(
    "-shared", "-o", "build\dxgi.dll"
) + $proxySources + @(
    "src\dxgi.def", "build\libskse64_steam_loader.a", "build\NativeAssets.resource.o",
    "-Wl,--dynamicbase", "-Wl,--nxcompat", "-Wl,--enable-auto-import",
    "-lws2_32", "-ld3d11", "-ldxgi", "-lgdi32", "-luser32", "-lole32", "-luuid"
)
$ErrorActionPreference = "Continue"
& $cxx @proxyArgs 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = "Stop"; throw "DXGI proxy build failed" }

$pluginArgs = $common + @(
    "-I$nifly/include", "-I$nifly/external",
    "-shared", "-o", "build\SkyrimDualScreen.dll", "src\SkyrimPlugin.cpp",
    "src\CustomPlayerPreview.cpp", (Join-Path $nifly "build-mingw\src\libnifly.a"),
    "-Wl,--dynamicbase", "-Wl,--nxcompat", "-Wl,--kill-at", "-luser32",
    "-ld3d11", "-ldxgi", "-ld3dcompiler_47"
)
& $cxx @pluginArgs 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = "Stop"; throw "SKSE plugin build failed" }

$previewArgs = $common + @(
    "-o", "build\ui-preview.exe",
    "tools\UiPreview.cpp", "src\Ui.cpp", "src\NativeAssets.cpp",
    "..\DeadIslandSecondScreen\src\Canvas.cpp",
    "..\DeadIslandSecondScreen\src\Log.cpp",
    "build\NativeAssets.resource.o", "-lgdi32", "-luser32"
)
& $cxx @previewArgs 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = "Stop"; throw "UI preview build failed" }
$ErrorActionPreference = "Stop"
}
finally {
    Pop-Location
}

$objdump = "$Mingw\objdump.exe"
$proxyInfo = (& $objdump -p "$out\dxgi.dll") -join "`n"
$pluginInfo = (& $objdump -p "$out\SkyrimDualScreen.dll") -join "`n"
if ($proxyInfo -notmatch "pei-x86-64" -or $pluginInfo -notmatch "pei-x86-64") { throw "One or more outputs are not x64" }
foreach ($name in @("CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2", "DXGID3D10CreateDevice", "DXGID3D10CreateLayeredDevice", "DXGID3D10GetLayeredDeviceSize", "DXGID3D10RegisterLayers", "DXGIDeclareAdapterRemovalSupport", "DXGIDisableVBlankVirtualization", "DXGIGetDebugInterface1", "DXGIReportAdapterConfiguration", "SDS_SubmitSnapshot", "SDS_PollCommand", "SDS_PollCommandSized", "SDS_HasPendingCommand", "SDS_ReportActionSuccess", "SDS_EnableRenderer", "SDS_SubmitItemPreview", "SDS_GetCharacterPreviewSurface", "SDS_SetCharacterPreviewReady", "SDS_SubmitCharacterPreview", "SDS_SubmitMapPreview")) {
    if ($proxyInfo -notmatch [regex]::Escape($name)) { throw "dxgi.dll is missing export $name" }
}
foreach ($name in @("SKSEPlugin_Query", "SKSEPlugin_Load")) {
    if ($pluginInfo -notmatch [regex]::Escape($name)) { throw "SkyrimDualScreen.dll is missing export $name" }
}

$package = Join-Path $out "package"
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
New-Item -ItemType Directory -Path "$package\Data\SKSE\Plugins" -Force | Out-Null
New-Item -ItemType Directory -Path "$package\Data\Scripts" -Force | Out-Null
Copy-Item -LiteralPath "$out\dxgi.dll" -Destination "$package\dxgi.dll"
Copy-Item -LiteralPath "$out\SkyrimDualScreen.dll" -Destination "$package\Data\SKSE\Plugins\SkyrimDualScreen.dll"
Copy-Item -LiteralPath "$root\SkyrimSecondScreen.ini" -Destination "$package\SkyrimSecondScreen.ini"
Copy-Item -LiteralPath "$root\SkyrimGamepad.ini" -Destination "$package\SkyrimGamepad.ini"
Copy-Item -LiteralPath "$Skse\skse64_1_5_97.dll" -Destination "$package\skse64_1_5_97.dll"
Copy-Item -LiteralPath "$Skse\skse64_steam_loader.dll" -Destination "$package\skse64_steam_loader.dll"
Copy-Item -Path "$Skse\Data\Scripts\*" -Destination "$package\Data\Scripts" -Recurse -Force

Write-Host "Built Skyrim dual-screen package: $package"

if ($Install) {
    if (-not $Device) { throw "-Device IP:PORT is required with -Install" }
    & $Adb connect $Device | Write-Host
    $state = (& $Adb -s $Device get-state 2>$null)
    if ($state -ne "device") { throw "ADB device $Device is not online" }
    & $Adb -s $Device shell "mkdir -p '$RemoteGame/Data/SKSE/Plugins' '$RemoteGame/Data/Scripts'"
    if ($LASTEXITCODE -ne 0) { throw "Could not create Skyrim mod directories" }
    foreach ($file in @("dxgi.dll", "SkyrimSecondScreen.ini", "SkyrimGamepad.ini", "skse64_1_5_97.dll", "skse64_steam_loader.dll")) {
        & $Adb -s $Device push "$package\$file" "$RemoteGame/$file"
        if ($LASTEXITCODE -ne 0) { throw "Failed to install $file" }
    }
    & $Adb -s $Device push "$package\Data\SKSE\Plugins\SkyrimDualScreen.dll" "$RemoteGame/Data/SKSE/Plugins/SkyrimDualScreen.dll"
    if ($LASTEXITCODE -ne 0) { throw "Failed to install the SKSE plugin" }
    & $Adb -s $Device push "$package\Data\Scripts\." "$RemoteGame/Data/Scripts/"
    if ($LASTEXITCODE -ne 0) { throw "Failed to install SKSE scripts" }
    Write-Host "Installed Skyrim dual-screen mod on $Device"
}

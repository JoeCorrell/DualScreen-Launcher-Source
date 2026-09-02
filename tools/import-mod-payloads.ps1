param(
    [Parameter(Mandatory = $false)]
    [string]$ModRoot = "",

    [Parameter(Mandatory = $false)]
    [string]$StardewBuildInputRoot = "",

    [Parameter(Mandatory = $false)]
    [string]$PayloadVersion = "launcher-2026.08.30-mod-sync-2"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot "launcher\src\main\assets\mods"
$modRoot = if ([string]::IsNullOrWhiteSpace($ModRoot)) {
    Join-Path $projectRoot "pc\mods"
} else {
    [IO.Path]::GetFullPath($ModRoot)
}
if (-not (Test-Path -LiteralPath $modRoot -PathType Container)) {
    throw "PC mod source root does not exist: $modRoot"
}
$importedEntries = 0

function Reset-Payload([string]$Id) {
    $directory = Join-Path $assetRoot $Id
    if (Test-Path -LiteralPath $directory) {
        $resolved = (Resolve-Path -LiteralPath $directory).Path
        $expected = [IO.Path]::GetFullPath($assetRoot) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolved.StartsWith($expected, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace payload outside $assetRoot"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Join-Path $directory "payload") -Force | Out-Null
    return $directory
}

function Add-PayloadFile(
    [System.Collections.Generic.List[object]]$Entries,
    [string]$Payload,
    [string]$Source,
    [string]$AssetPath,
    [string]$TargetPath
) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Missing deployable mod file: $Source"
    }
    $destination = Join-Path $Payload ($AssetPath -replace '/', '\')
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    $Entries.Add([ordered]@{ source = $AssetPath; target = $TargetPath; sha256 = $hash })
}

function Write-Manifest(
    [string]$Directory,
    [string]$Version,
    [System.Collections.Generic.List[object]]$Entries
) {
    $json = [ordered]@{ version = $Version; files = $Entries } |
        ConvertTo-Json -Depth 6
    $utf8WithoutBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText(
        (Join-Path $Directory "manifest.json"),
        $json,
        $utf8WithoutBom
    )
}

$version = $PayloadVersion

# Skyrim ships the complete tested SKSE package, preserving every relative path.
$directory = Reset-Payload "skyrim-se"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "SkyrimSecondScreen\build\package"
Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($sourceRoot.Length + 1).Replace('\', '/')
    Add-PayloadFile $entries $payload $_.FullName $relative $relative
}
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

# Fallout 3 uses a game-local dinput8 proxy. Keep it in the reproducible import
# path with the other native payloads so a payload release can never retain an
# older INI or accidentally omit the DLL override configuration.
$directory = Reset-Payload "fallout3"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "Fallout3SecondScreen\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dinput8.dll") "dinput8.dll" "dinput8.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "Fallout3SecondScreen.ini") "Fallout3SecondScreen.ini" "Fallout3SecondScreen.ini"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dxvk.conf") "dxvk.conf" "dxvk.conf"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$directory = Reset-Payload "fallout4"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "Fallout4SecondScreen\build\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dxgi.candidate.dll") "dxgi.candidate.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "Fallout4SecondScreen.ini") "Fallout4SecondScreen.ini" "Fallout4SecondScreen.ini"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "Fallout4SecondScreen.owner") "Fallout4SecondScreen.owner" "Fallout4SecondScreen.owner"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$directory = Reset-Payload "grim-dawn"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "GrimDawnSecondScreen\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "GrimDawnSecondScreen.ini") "GrimDawnSecondScreen.ini" "GrimDawnSecondScreen.ini"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "GrimDawnSecondScreen.owner") "GrimDawnSecondScreen.owner" "GrimDawnSecondScreen.owner"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$directory = Reset-Payload "deus-ex-hrdc"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeusExSecondScreen\dist\DeusExSecondScreen-AYN-Thor\dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeusExSecondScreen\dist\DeusExSecondScreen-AYN-Thor\DeusExSecondScreen.ini") "DeusExSecondScreen.ini" "DeusExSecondScreen.ini"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$directory = Reset-Payload "dead-island-de"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeadIslandSecondScreen\build\dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeadIslandSecondScreen\DeadIslandSecondScreen.ini") "DeadIslandSecondScreen.ini" "DeadIslandSecondScreen.ini"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$directory = Reset-Payload "valheim"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "ValheimSecondScreen\bin\Release\net471\ValheimSecondScreen.dll") "ValheimSecondScreen.dll" "BepInEx/plugins/ValheimSecondScreen/ValheimSecondScreen.dll"
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

# War for Cybertron uses a game-local dinput8 proxy beside Binaries/TWFC.exe.
# Keep the generated panel assets in the same reproducible payload as the DLL
# and INI so a clean import cannot publish a partially updated interface.
$directory = Reset-Payload "transformers-wfc"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "TransformersWfcSecondScreen\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dinput8.dll") "dinput8.dll" "Binaries/dinput8.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "TransformersWfcSecondScreen.ini") "TransformersWfcSecondScreen.ini" "Binaries/TransformersWfcSecondScreen.ini"
$assetSourceRoot = Join-Path $sourceRoot "assets"
Get-ChildItem -LiteralPath $assetSourceRoot -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($assetSourceRoot.Length + 1).Replace('\', '/')
    Add-PayloadFile $entries $payload $_.FullName "assets/$relative" "Binaries/assets/$relative"
}
$prerequisiteSourceRoot = Join-Path $sourceRoot "prerequisites"
Add-PayloadFile $entries $payload `
    (Join-Path $prerequisiteSourceRoot "WfcPrerequisiteBootstrap.cmd") `
    "prerequisites/WfcPrerequisiteBootstrap.cmd" `
    "Binaries/SecondScreenPrerequisites/WfcPrerequisiteBootstrap.cmd"
$redistSourceRoot = Join-Path $prerequisiteSourceRoot "redist"
Get-ChildItem -LiteralPath $redistSourceRoot -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($redistSourceRoot.Length + 1).Replace('\', '/')
    Add-PayloadFile $entries $payload $_.FullName `
        "prerequisites/redist/$relative" `
        "Binaries/SecondScreenPrerequisites/redist/$relative"
}
Write-Manifest $directory $version $entries
$importedEntries += $entries.Count

$stardewManifestPath = Join-Path $assetRoot "stardew-valley\manifest.json"
if (-not [string]::IsNullOrWhiteSpace($StardewBuildInputRoot)) {
    & (Join-Path $PSScriptRoot "import-stardew-native-payload.ps1") `
        -BuildInputRoot $StardewBuildInputRoot
    if ($LASTEXITCODE -ne 0) { throw "The Stardew native payload import failed" }
} elseif (-not (Test-Path -LiteralPath $stardewManifestPath -PathType Leaf)) {
    throw "The Stardew payload is missing. Supply -StardewBuildInputRoot to regenerate its verified binary deltas."
} else {
    Write-Output "Retaining the existing verified Stardew binary deltas. Supply -StardewBuildInputRoot to regenerate them."
}
$stardewManifest = Get-Content -Raw $stardewManifestPath | ConvertFrom-Json
$importedEntries += @($stardewManifest.files).Count + @($stardewManifest.patches).Count

# Switch dual-screen mods share the same signed payload update channel as PC mods. Each title
# owns an independent manifest and optional payload directory so future ROMFS/ExeFS updates can
# be delivered without replacing user-installed mods. Emulator-core changes still ship with the
# signed APK; data-driven game mod updates are applied atomically by SwitchModPayloadManager.
$switchModRoot = Join-Path $projectRoot "switch\mods"
$switchAssetRoot = Join-Path $assetRoot "switch"
if (-not (Test-Path -LiteralPath $switchModRoot -PathType Container)) {
    throw "Switch mod source root does not exist: $switchModRoot"
}
if (Test-Path -LiteralPath $switchAssetRoot) {
    Remove-Item -LiteralPath $switchAssetRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $switchAssetRoot -Force | Out-Null
Get-ChildItem -LiteralPath $switchModRoot -Directory | ForEach-Object {
    $sourceGame = $_.FullName
    $destinationGame = Join-Path $switchAssetRoot $_.Name
    Copy-Item -LiteralPath $sourceGame -Destination $destinationGame -Recurse
    $manifest = Get-Content -Raw (Join-Path $destinationGame "manifest.json") | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or [string]::IsNullOrWhiteSpace($manifest.titleId)) {
        throw "Invalid Switch mod manifest: $($_.Name)"
    }
    $importedEntries += @($manifest.files).Count
}

Write-Output "Imported $importedEntries payload entries into $assetRoot"

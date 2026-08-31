param(
    [Parameter(Mandatory = $false)]
    [string]$WemuRoot = "C:\Projects\New folder (10)"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot "launcher\src\main\assets\mods"
$modRoot = Join-Path $WemuRoot "dualscreen\mods"

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

$version = "wemu-current-2026.08.30-player-magic-input"

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

$directory = Reset-Payload "fallout4"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "Fallout4SecondScreen\build\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "Fallout4SecondScreen.ini") "Fallout4SecondScreen.ini" "Fallout4SecondScreen.ini"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "Fallout4SecondScreen.owner") "Fallout4SecondScreen.owner" "Fallout4SecondScreen.owner"
Write-Manifest $directory $version $entries

$directory = Reset-Payload "grim-dawn"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
$sourceRoot = Join-Path $modRoot "GrimDawnSecondScreen\package"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "GrimDawnSecondScreen.ini") "GrimDawnSecondScreen.ini" "GrimDawnSecondScreen.ini"
Add-PayloadFile $entries $payload (Join-Path $sourceRoot "GrimDawnSecondScreen.owner") "GrimDawnSecondScreen.owner" "GrimDawnSecondScreen.owner"
Write-Manifest $directory $version $entries

$directory = Reset-Payload "deus-ex-hrdc"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeusExSecondScreen\dist\DeusExSecondScreen-AYN-Thor\dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeusExSecondScreen\dist\DeusExSecondScreen-AYN-Thor\DeusExSecondScreen.ini") "DeusExSecondScreen.ini" "DeusExSecondScreen.ini"
Write-Manifest $directory $version $entries

$directory = Reset-Payload "dead-island-de"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeadIslandSecondScreen\build\dxgi.dll") "dxgi.dll" "dxgi.dll"
Add-PayloadFile $entries $payload (Join-Path $modRoot "DeadIslandSecondScreen\DeadIslandSecondScreen.ini") "DeadIslandSecondScreen.ini" "DeadIslandSecondScreen.ini"
Write-Manifest $directory $version $entries

$directory = Reset-Payload "valheim"
$payload = Join-Path $directory "payload"
$entries = [System.Collections.Generic.List[object]]::new()
Add-PayloadFile $entries $payload (Join-Path $modRoot "ValheimSecondScreen\bin\Release\net471\ValheimSecondScreen.dll") "ValheimSecondScreen.dll" "BepInEx/plugins/ValheimSecondScreen/ValheimSecondScreen.dll"
Write-Manifest $directory $version $entries

& (Join-Path $PSScriptRoot "import-stardew-native-payload.ps1") -WemuRoot $WemuRoot
if ($LASTEXITCODE -ne 0) { throw "The Stardew native payload import failed" }

Write-Output "Imported $($entries.Count) final payload entries into $assetRoot"

[CmdletBinding()]
param(
    [string] $Adb = "C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string] $Endpoint = "100.83.63.70:34023",
    [string] $RemoteGame = "/storage/emulated/0/Download/Fallout-4-SteamRIP.com/Fallout 4",
    [switch] $Commit
)

$ErrorActionPreference = "Stop"
$expectedExe = "061c1ab3738612dbc46d748f02c27795a1003501c90c15d1e649d8a07fcbbb4e"
$root = $PSScriptRoot
$package = Join-Path $root "build\package"
$localDll = Join-Path $package "dxgi.dll"
$localIni = Join-Path $package "Fallout4SecondScreen.ini"
$localMarker = Join-Path $package "Fallout4SecondScreen.owner"

function ConvertFrom-OwnerManifest {
    param([Parameter(Mandatory = $true)][string] $Text)
    $lines = ($Text -replace "`r", "").TrimEnd("`n").Split("`n")
    if ($lines.Count -ne 3 -or $lines[0] -ne "Format=Fallout4SecondScreenOwnerV1" -or
        $lines[1] -notmatch '^DxgiSha256=([0-9a-fA-F]{64})$' -or
        $lines[2] -notmatch '^ConfigSha256=([0-9a-fA-F]{64})$') {
        throw "Invalid Fallout4SecondScreen owner manifest"
    }
    return [pscustomobject]@{
        DllHash = $lines[1].Substring("DxgiSha256=".Length).ToLowerInvariant()
        IniHash = $lines[2].Substring("ConfigSha256=".Length).ToLowerInvariant()
    }
}
if ($RemoteGame -notmatch '^/[A-Za-z0-9._ /-]+$') {
    throw "RemoteGame must be an absolute device path containing only letters, digits, spaces, '.', '_', '/', or '-'"
}
$remoteSegments = $RemoteGame.Split('/', [StringSplitOptions]::RemoveEmptyEntries)
if ($RemoteGame.EndsWith('/') -or $RemoteGame.Contains('//') -or
    $remoteSegments.Count -lt 2 -or $remoteSegments -contains '.' -or
    $remoteSegments -contains '..') {
    throw "RemoteGame must be a normalized, non-root absolute device path without '.' or '..' segments"
}
if (-not (Test-Path -LiteralPath $Adb)) { throw "ADB was not found at $Adb" }
if (-not (Test-Path -LiteralPath $localDll) -or
    -not (Test-Path -LiteralPath $localIni) -or
    -not (Test-Path -LiteralPath $localMarker)) {
    throw "Build the package first with .\build.ps1"
}

& $Adb connect $Endpoint | Write-Host
if ((& $Adb -s $Endpoint get-state 2>$null) -ne "device") {
    throw "ADB device $Endpoint is not online"
}

function Get-RemoteRegularFileState {
    param([Parameter(Mandatory = $true)][string] $Path)
    $probe = "if test ! -e '$Path'; then printf absent; elif test -f '$Path' && test -r '$Path' && test ! -L '$Path'; then printf regular; else printf invalid; fi"
    $state = ((& $Adb -s $Endpoint shell $probe) -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or $state -notin @("absent", "regular")) {
        throw "Remote path is present but is not a readable, non-symlink regular file: $Path"
    }
    return $state
}

function Get-RemoteSha256 {
    param([Parameter(Mandatory = $true)][string] $Path)
    $line = ((& $Adb -s $Endpoint shell "sha256sum '$Path'") -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or $line -notmatch '^([0-9a-fA-F]{64})\s+') {
        throw "Could not obtain a valid SHA-256 hash for $Path"
    }
    return ($line -split '\s+')[0].ToLowerInvariant()
}

$remoteHashLine = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4.exe' 2>/dev/null") -join "`n").Trim()
if (-not $remoteHashLine) { throw "Fallout4.exe was not found at $RemoteGame" }
$remoteHash = ($remoteHashLine -split '\s+')[0].ToLowerInvariant()
if ($remoteHash -ne $expectedExe) {
    throw "Unsupported Fallout4.exe hash $remoteHash"
}

$busy = (& $Adb -s $Endpoint shell "ps -A -o PID,NAME,ARGS | grep -i -E 'Fallout|wineserver' | grep -v grep || true") -join "`n"
if ($busy.Trim()) {
    Write-Host "The shared Games prefix is active:" -ForegroundColor Yellow
    Write-Host $busy
    throw "Exit Fallout and the WEMU Games session normally before deployment"
}

$localDllHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localDll).Hash.ToLowerInvariant()
$localIniHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localIni).Hash.ToLowerInvariant()
$localMarkerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localMarker).Hash.ToLowerInvariant()
$localOwner = ConvertFrom-OwnerManifest ([System.IO.File]::ReadAllText($localMarker))
if ($localOwner.DllHash -ne $localDllHash -or $localOwner.IniHash -ne $localIniHash) {
    throw "The package owner manifest does not match the packaged DLL and config"
}
$dllPath = "$RemoteGame/dxgi.dll"
$iniPath = "$RemoteGame/Fallout4SecondScreen.ini"
$markerPath = "$RemoteGame/Fallout4SecondScreen.owner"
$dllState = Get-RemoteRegularFileState $dllPath
$iniState = Get-RemoteRegularFileState $iniPath
$markerState = Get-RemoteRegularFileState $markerPath
$hadDll = $dllState -eq "regular"
$hadIni = $iniState -eq "regular"
$hadMarker = $markerState -eq "regular"
$existingDllHash = if ($hadDll) { Get-RemoteSha256 $dllPath } else { "" }
$existingIniHash = if ($hadIni) { Get-RemoteSha256 $iniPath } else { "" }
$existingMarkerHash = if ($hadMarker) { Get-RemoteSha256 $markerPath } else { "" }
if ($hadDll -or $hadIni -or $hadMarker) {
    if (-not ($hadDll -and $hadIni -and $hadMarker)) {
        throw "A partial prior deployment exists; refusing to overwrite it without all three owned files"
    }
    $existingOwnerText = ((& $Adb -s $Endpoint shell "cat '$markerPath'") -join "`n")
    if ($LASTEXITCODE -ne 0) { throw "Could not read the existing owner manifest" }
    $existingOwner = ConvertFrom-OwnerManifest $existingOwnerText
    if ($existingOwner.DllHash -ne $existingDllHash -or
        $existingOwner.IniHash -ne $existingIniHash) {
        throw "Existing files no longer match their owner manifest; refusing to overwrite them"
    }
}

Write-Host "Preflight passed"
Write-Host "  Device:       $Endpoint"
Write-Host "  Game:         $RemoteGame"
Write-Host "  Runtime hash: $remoteHash"
Write-Host "  dxgi.dll:     $localDllHash"
Write-Host "  config:       $localIniHash"
Write-Host "  owner marker: $localMarkerHash"
Write-Host "  State source: native in-process Pip-Boy data (no companion service)"
Write-Host "  Wine DXGI:    global native,builtin already verified"

if (-not $Commit) {
    Write-Host "Dry run only. Re-run with -Commit after reviewing this preflight."
    return
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmssfff"
$backup = "$RemoteGame/.Fallout4SecondScreen-backup-$stamp"
$tempDll = "$RemoteGame/.Fallout4SecondScreen-dxgi-new-$stamp"
$tempIni = "$RemoteGame/.Fallout4SecondScreen-ini-new-$stamp"
$tempMarker = "$RemoteGame/.Fallout4SecondScreen-owner-new-$stamp"

& $Adb -s $Endpoint shell "mkdir '$backup'"
if ($LASTEXITCODE -ne 0) { throw "Could not create deployment backup directory" }
if ($hadDll) {
    & $Adb -s $Endpoint shell "cp '$RemoteGame/dxgi.dll' '$backup/dxgi.dll'"
    if ($LASTEXITCODE -ne 0) { throw "Could not back up the existing game-local dxgi.dll" }
    $backupDllHash = ((& $Adb -s $Endpoint shell "sha256sum '$backup/dxgi.dll'") -split '\s+')[0].ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $backupDllHash -ne $existingDllHash) {
        throw "The game-local dxgi.dll backup failed verification"
    }
}
if ($hadIni) {
    & $Adb -s $Endpoint shell "cp '$RemoteGame/Fallout4SecondScreen.ini' '$backup/Fallout4SecondScreen.ini'"
    if ($LASTEXITCODE -ne 0) { throw "Could not back up the existing Fallout4SecondScreen.ini" }
    $backupIniHash = ((& $Adb -s $Endpoint shell "sha256sum '$backup/Fallout4SecondScreen.ini'") -split '\s+')[0].ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $backupIniHash -ne $existingIniHash) {
        throw "The Fallout4SecondScreen.ini backup failed verification"
    }
}
if ($hadMarker) {
    & $Adb -s $Endpoint shell "cp '$RemoteGame/Fallout4SecondScreen.owner' '$backup/Fallout4SecondScreen.owner'"
    if ($LASTEXITCODE -ne 0) { throw "Could not back up the deployment owner marker" }
    $backupMarkerHash = ((& $Adb -s $Endpoint shell "sha256sum '$backup/Fallout4SecondScreen.owner'") -split '\s+')[0].ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $backupMarkerHash -ne $existingMarkerHash) {
        throw "The deployment owner marker backup failed verification"
    }
}
try {
    & $Adb -s $Endpoint push $localDll $tempDll | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not stage dxgi.dll" }
    & $Adb -s $Endpoint push $localIni $tempIni | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not stage Fallout4SecondScreen.ini" }
    & $Adb -s $Endpoint push $localMarker $tempMarker | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not stage the deployment owner marker" }
    $stagedDll = ((& $Adb -s $Endpoint shell "sha256sum '$tempDll'") -split '\s+')[0].ToLowerInvariant()
    $stagedIni = ((& $Adb -s $Endpoint shell "sha256sum '$tempIni'") -split '\s+')[0].ToLowerInvariant()
    $stagedMarker = ((& $Adb -s $Endpoint shell "sha256sum '$tempMarker'") -split '\s+')[0].ToLowerInvariant()
    if ($stagedDll -ne $localDllHash -or $stagedIni -ne $localIniHash -or
        $stagedMarker -ne $localMarkerHash) {
        throw "Staged file hash verification failed"
    }
    & $Adb -s $Endpoint shell "mv '$tempDll' '$RemoteGame/dxgi.dll' && mv '$tempIni' '$RemoteGame/Fallout4SecondScreen.ini' && mv '$tempMarker' '$RemoteGame/Fallout4SecondScreen.owner'"
    if ($LASTEXITCODE -ne 0) { throw "Could not publish game-local files" }

    $installedDll = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/dxgi.dll'") -split '\s+')[0].ToLowerInvariant()
    $installedIni = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4SecondScreen.ini'") -split '\s+')[0].ToLowerInvariant()
    $installedMarker = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4SecondScreen.owner'") -split '\s+')[0].ToLowerInvariant()
    if ($installedDll -ne $localDllHash -or $installedIni -ne $localIniHash -or
        $installedMarker -ne $localMarkerHash) {
        throw "Installed file hash verification failed"
    }
    Write-Host "Installed Fallout 4 second-screen diagnostic foundation." -ForegroundColor Green
    Write-Host "Backup: $backup"
}
catch {
    $originalError = $_
    $rollbackFailed = $false
    Write-Warning "Deployment failed; restoring game-local files"
    if ($hadDll) {
        & $Adb -s $Endpoint shell "cp '$backup/dxgi.dll' '$RemoteGame/dxgi.dll'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        $restoredDll = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/dxgi.dll' 2>/dev/null || true") -join "`n").Trim()
        $restoredDllHash = if ($restoredDll) { ($restoredDll -split '\s+')[0].ToLowerInvariant() } else { "" }
        if ($restoredDllHash -ne $existingDllHash) { $rollbackFailed = $true }
    } else {
        & $Adb -s $Endpoint shell "rm -f '$RemoteGame/dxgi.dll'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        & $Adb -s $Endpoint shell "test ! -e '$RemoteGame/dxgi.dll'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
    }
    if ($hadIni) {
        & $Adb -s $Endpoint shell "cp '$backup/Fallout4SecondScreen.ini' '$RemoteGame/Fallout4SecondScreen.ini'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        $restoredIni = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4SecondScreen.ini' 2>/dev/null || true") -join "`n").Trim()
        $restoredIniHash = if ($restoredIni) { ($restoredIni -split '\s+')[0].ToLowerInvariant() } else { "" }
        if ($restoredIniHash -ne $existingIniHash) { $rollbackFailed = $true }
    } else {
        & $Adb -s $Endpoint shell "rm -f '$RemoteGame/Fallout4SecondScreen.ini'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        & $Adb -s $Endpoint shell "test ! -e '$RemoteGame/Fallout4SecondScreen.ini'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
    }
    if ($hadMarker) {
        & $Adb -s $Endpoint shell "cp '$backup/Fallout4SecondScreen.owner' '$RemoteGame/Fallout4SecondScreen.owner'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        $restoredMarker = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4SecondScreen.owner' 2>/dev/null || true") -join "`n").Trim()
        $restoredMarkerHash = if ($restoredMarker) { ($restoredMarker -split '\s+')[0].ToLowerInvariant() } else { "" }
        if ($restoredMarkerHash -ne $existingMarkerHash) { $rollbackFailed = $true }
    } else {
        & $Adb -s $Endpoint shell "rm -f '$RemoteGame/Fallout4SecondScreen.owner'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
        & $Adb -s $Endpoint shell "test ! -e '$RemoteGame/Fallout4SecondScreen.owner'"
        if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
    }
    & $Adb -s $Endpoint shell "rm -f '$tempDll' '$tempIni' '$tempMarker'"
    if ($LASTEXITCODE -ne 0) { $rollbackFailed = $true }
    if ($rollbackFailed) {
        throw "Deployment failed and automatic rollback could not be verified. Original error: $originalError"
    }
    throw $originalError
}

[CmdletBinding()]
param(
    [string]$Serial = '100.83.63.70:44071',
    [string]$GamePath = '/storage/emulated/0/Dualscreen/Games/Grim Dawn/Game',
    [switch]$Commit
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$adb = 'C:\Android\Sdk\platform-tools\adb.exe'
if (-not (Test-Path -LiteralPath $adb)) {
    $adb = 'C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe'
}
$packageRoot = Join-Path $projectRoot 'package'
$localDll = Join-Path $packageRoot 'dxgi.dll'
$localIni = Join-Path $packageRoot 'GrimDawnSecondScreen.ini'
$localOwner = Join-Path $packageRoot 'GrimDawnSecondScreen.owner'
$localManifest = Join-Path $packageRoot 'manifest.json'
$expectedExe = 'aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8'
$expectedGame = 'fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f'
$expectedEngine = '687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580'
$expectedRenderer = 'ca35c54598f23002cf3db8acd52a712e8d66523d0aef123013b3fea3d4952652'
$ownerText = 'GrimDawnSecondScreen:v1'

$allowedGamePaths = @(
    '/storage/emulated/0/Dualscreen/Games/Grim Dawn/Game',
    '/storage/emulated/0/Games/Grim Dawn'
)
if ($GamePath -notin $allowedGamePaths) {
    throw 'Refusing an unrecognized target. This deployer is scoped to the exact Grim Dawn directory.'
}
if (-not (Test-Path -LiteralPath $adb)) { throw "ADB not found: $adb" }
foreach ($file in @($localDll, $localIni, $localOwner, $localManifest)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Package is incomplete; run build.ps1 first: $file"
    }
}

function Invoke-Adb([string[]]$Arguments) {
    # adb writes successful push progress to stderr. Under Stop that becomes a
    # PowerShell NativeCommandError before LASTEXITCODE can be inspected.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $adb -s $Serial @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($exitCode -ne 0) {
        throw "ADB failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Get-RemoteHash([string]$Path) {
    $output = Invoke-Adb @('shell', "sha256sum '$Path'")
    $line = ($output | Select-Object -First 1).ToString().Trim()
    if ($line -notmatch '^([0-9a-fA-F]{64})\s') {
        throw "Could not parse remote SHA-256 for $Path"
    }
    return $Matches[1].ToLowerInvariant()
}

function Test-RemoteFile([string]$Path) {
    $output = Invoke-Adb @('shell', "if [ -f '$Path' ]; then echo yes; else echo no; fi")
    return (($output -join '').Trim() -eq 'yes')
}

$state = Invoke-Adb @('get-state')
if (($state -join '').Trim() -ne 'device') { throw "ADB device $Serial is not ready." }
$remoteExe = "$GamePath/Grim Dawn.exe"
$remoteGame = "$GamePath/Game.dll"
$remoteEngine = "$GamePath/Engine.dll"
$remoteRenderer = "$GamePath/Direct3D11.dll"
if ((Get-RemoteHash $remoteExe) -ne $expectedExe -or
    (Get-RemoteHash $remoteGame) -ne $expectedGame -or
    (Get-RemoteHash $remoteEngine) -ne $expectedEngine -or
    (Get-RemoteHash $remoteRenderer) -ne $expectedRenderer) {
    throw 'Remote executable/Game/Engine/Direct3D11 hashes do not match the supported 1.3.0.7 I386 build.'
}

$processes = Invoke-Adb @('shell', 'ps -A -o ARGS')
if (($processes -join "`n") -match '(?i)wineserver|Grim Dawn\.exe') {
    throw 'A Wine guest is active. Exit the WEMU game session before deployment.'
}

$registryText = ''
foreach ($packageName in @('com.dualscreen.launcher', 'com.wemu')) {
    $registryPath = "/data/user/0/$packageName/files/imagefs/home/xuser-2/.wine/user.reg"
    $registryOutput = & $adb -s $Serial shell `
        "run-as $packageName grep -i -n dxgi '$registryPath'" 2>&1
    $registryText += ($registryOutput -join "`n") + "`n"
}
if ($registryText -notmatch '(?i)dxgi.*native') {
    throw 'The Games prefix does not show a native DXGI override. Refusing a proxy that Wine may silently ignore.'
}

$remoteDll = "$GamePath/dxgi.dll"
$remoteIni = "$GamePath/GrimDawnSecondScreen.ini"
$remoteOwner = "$GamePath/GrimDawnSecondScreen.owner"
$remoteManifest = "$GamePath/GrimDawnSecondScreen.manifest.json"
$remoteFiles = [ordered]@{
    $remoteDll = $localDll
    $remoteIni = $localIni
    $remoteOwner = $localOwner
    $remoteManifest = $localManifest
}
$hadExisting = @{}
foreach ($remote in $remoteFiles.Keys) {
    $hadExisting[$remote] = Test-RemoteFile $remote
}
if (Test-RemoteFile $remoteDll) {
    if (-not (Test-RemoteFile $remoteOwner)) {
        throw 'An unowned dxgi.dll already exists. Refusing to overwrite another mod or proxy.'
    }
    $remoteOwnerText = Invoke-Adb @('shell', "cat '$remoteOwner'")
    if (($remoteOwnerText -join '').Trim() -ne $ownerText) {
        throw 'The existing dxgi.dll owner marker is not GrimDawnSecondScreen:v1.'
    }
}

$localHashes = @{}
foreach ($entry in $remoteFiles.GetEnumerator()) {
    $localHashes[$entry.Key] =
        (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash.ToLowerInvariant()
}
$dllHash = $localHashes[$remoteDll]
Write-Host "Target verified: $remoteExe"
Write-Host "Package dxgi.dll SHA256: $dllHash"
Write-Host 'Wine native DXGI override: verified'
if (-not $Commit) {
    Write-Host 'Dry run complete. Re-run with -Commit after confirming Grim Dawn/Wine remains closed.'
    exit 0
}

$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$stageDll = "$GamePath/.gdds-$stamp-dxgi.dll"
$stageIni = "$GamePath/.gdds-$stamp.ini"
$stageOwner = "$GamePath/.gdds-$stamp.owner"
$stageManifest = "$GamePath/.gdds-$stamp.json"
$backup = "$GamePath/GrimDawnSecondScreen-backup-$stamp"
$backupCreated = $false
$finalMutationStarted = $false
try {
    Invoke-Adb @('push', $localDll, $stageDll) | Out-Null
    Invoke-Adb @('push', $localIni, $stageIni) | Out-Null
    Invoke-Adb @('push', $localOwner, $stageOwner) | Out-Null
    Invoke-Adb @('push', $localManifest, $stageManifest) | Out-Null
    $stagedFiles = [ordered]@{
        $stageDll = $localDll
        $stageIni = $localIni
        $stageOwner = $localOwner
        $stageManifest = $localManifest
    }
    foreach ($entry in $stagedFiles.GetEnumerator()) {
        $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash.ToLowerInvariant()
        if ((Get-RemoteHash $entry.Key) -ne $expected) {
            throw "Staged package hash mismatch: $($entry.Key)"
        }
    }
    if ($hadExisting.Values -contains $true) {
        Invoke-Adb @('shell', "mkdir '$backup'") | Out-Null
        $backupCreated = $true
        $finalMutationStarted = $true
        if ($hadExisting[$remoteDll]) {
            Invoke-Adb @('shell', "mv '$remoteDll' '$backup/dxgi.dll'") | Out-Null
        }
        foreach ($pair in @(
            @($remoteIni, "$backup/GrimDawnSecondScreen.ini"),
            @($remoteOwner, "$backup/GrimDawnSecondScreen.owner"),
            @($remoteManifest, "$backup/manifest.json")
        )) {
            if (Test-RemoteFile $pair[0]) {
                Invoke-Adb @('shell', "mv '$($pair[0])' '$($pair[1])'") | Out-Null
            }
        }
    }
    $finalMutationStarted = $true
    Invoke-Adb @('shell', "mv '$stageDll' '$remoteDll'") | Out-Null
    Invoke-Adb @('shell', "mv '$stageIni' '$remoteIni'") | Out-Null
    Invoke-Adb @('shell', "mv '$stageOwner' '$remoteOwner'") | Out-Null
    Invoke-Adb @('shell', "mv '$stageManifest' '$remoteManifest'") | Out-Null
    foreach ($entry in $remoteFiles.GetEnumerator()) {
        if ((Get-RemoteHash $entry.Key) -ne $localHashes[$entry.Key]) {
            throw "Committed package hash mismatch: $($entry.Key)"
        }
    }
    Write-Host "Deployment complete. Recoverable prior files: $backup"
} catch {
    $failure = $_
    if ($finalMutationStarted) {
        $rollbackFiles = @(
            @($remoteDll, "$backup/dxgi.dll"),
            @($remoteIni, "$backup/GrimDawnSecondScreen.ini"),
            @($remoteOwner, "$backup/GrimDawnSecondScreen.owner"),
            @($remoteManifest, "$backup/manifest.json")
        )
        foreach ($pair in $rollbackFiles) {
            try {
                if ($backupCreated -and (Test-RemoteFile $pair[1])) {
                    Invoke-Adb @('shell', "mv -f '$($pair[1])' '$($pair[0])'") | Out-Null
                } elseif (-not $hadExisting[$pair[0]] -and (Test-RemoteFile $pair[0])) {
                    Invoke-Adb @('shell', "rm -f '$($pair[0])'") | Out-Null
                }
            } catch {
                Write-Warning "Rollback failed for $($pair[0]): $_"
            }
        }
    }
    throw $failure
}
